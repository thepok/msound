#include "StaticServer.h"
#include "SSEServer.h"
#include "HTTPAPIHandler.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <unordered_map>

StaticServer::StaticServer(const std::string& rootDir, uint16_t port)
    : rootDirectory(rootDir), serverPort(port), running(false), listenSocket(INVALID_SOCKET) {
}

StaticServer::~StaticServer() {
    stop();
}

bool StaticServer::start() {
    if (running) {
        return true;
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "StaticServer: WSAStartup failed" << std::endl;
        return false;
    }
#endif

    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "StaticServer: Failed to create socket" << std::endl;
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    // Set socket to reuse address
    int opt = 1;
#ifdef _WIN32
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(serverPort);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "StaticServer: Failed to bind to port " << serverPort << std::endl;
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "StaticServer: Failed to listen on socket" << std::endl;
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    running = true;
    serverThread = std::thread(&StaticServer::serverLoop, this);
    
    std::cout << "StaticServer: Started on port " << serverPort << std::endl;
    return true;
}

void StaticServer::stop() {
    if (!running) {
        return;
    }

    running = false;

    if (listenSocket != INVALID_SOCKET) {
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
    }

    if (serverThread.joinable()) {
        serverThread.join();
    }

#ifdef _WIN32
    WSACleanup();
#endif

    std::cout << "StaticServer: Stopped" << std::endl;
}

void StaticServer::setSSEServer(std::shared_ptr<SSEServer> sseServerPtr) {
    sseServer = sseServerPtr;
}

void StaticServer::setHTTPAPIHandler(std::shared_ptr<HTTPAPIHandler> apiHandler) {
    httpAPIHandler = apiHandler;
}

void StaticServer::serverLoop() {
    while (running) {
        sockaddr_in clientAddr = {};
        socklen_t clientAddrLen = sizeof(clientAddr);
        
        socket_t clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            if (running) {
                std::cerr << "StaticServer: Accept failed" << std::endl;
            }
            break;
        }

        // Handle client in current thread for simplicity
        // For production, consider using a thread pool
        bool shouldCloseSocket = true;
        handleClient(clientSocket, shouldCloseSocket);
        if (shouldCloseSocket) {
            closesocket(clientSocket);
        }
    }
}

void StaticServer::handleClient(socket_t clientSocket, bool& shouldCloseSocket) {
    char buffer[4096] = {};
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytesReceived <= 0) {
        return;
    }

    std::string request(buffer, bytesReceived);
    std::string method, path, headers, body;
    
    std::string version = parseHTTPRequest(request, method, path, headers, body);
    if (version.empty()) {
        send500(clientSocket);
        return;
    }

    // Remove query parameters from path
    size_t queryPos = path.find('?');
    if (queryPos != std::string::npos) {
        path = path.substr(0, queryPos);
    }

    // URL decode path
    path = urlDecode(path);

    // Route the request
    shouldCloseSocket = handleHTTPRequest(clientSocket, method, path, headers, body);
    if (!shouldCloseSocket && path != "/events") {
        // If we're not closing the socket but it's not an SSE endpoint, something went wrong
        send500(clientSocket);
        shouldCloseSocket = true;
    }
}

bool StaticServer::handleHTTPRequest(socket_t clientSocket, const std::string& method, const std::string& path, const std::string& headers, const std::string& body) {
    // Handle SSE endpoint
    if (path == "/events" && method == "GET") {
        if (sseServer) {
            sseServer->addClient(clientSocket);
            // Send initial state to new client - this will be handled by a callback
            return false; // Don't close the socket, SSE will handle it
        } else {
            send500(clientSocket);
            return true;
        }
    }
    
    // Handle API endpoints
    if (path.substr(0, 5) == "/api/") {
        if (httpAPIHandler) {
            return httpAPIHandler->handleAPIRequest(clientSocket, method, path, body);
        } else {
            send500(clientSocket);
            return true;
        }
    }
    
    // Handle static files
    if (method == "GET") {
        handleStaticFile(clientSocket, path);
        return true;
    }
    
    // Method not allowed
    sendResponse(clientSocket, 405, "text/plain", "Method Not Allowed");
    return true;
}

void StaticServer::handleStaticFile(socket_t clientSocket, const std::string& path) {
    std::string filePath = path;
    
    // Map root to gui.html
    if (filePath == "/") {
        filePath = "/gui.html";
    }

    // Security check
    if (!isPathSafe(filePath)) {
        send404(clientSocket);
        return;
    }

    // Build full file path
    std::filesystem::path fullPath = std::filesystem::path(rootDirectory) / filePath.substr(1); // Remove leading '/'
    
    try {
        if (!std::filesystem::exists(fullPath) || !std::filesystem::is_regular_file(fullPath)) {
            send404(clientSocket);
            return;
        }

        std::ifstream file(fullPath, std::ios::binary);
        if (!file) {
            send500(clientSocket);
            return;
        }

        // Get file size
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        // Read file content
        std::string content(fileSize, '\0');
        file.read(&content[0], fileSize);

        // Get MIME type
        std::string extension = fullPath.extension().string();
        std::string mimeType = getMimeType(extension);

        sendResponse(clientSocket, 200, mimeType, content);

    } catch (const std::exception& e) {
        std::cerr << "StaticServer: Error serving file " << filePath << ": " << e.what() << std::endl;
        send500(clientSocket);
    }
}

std::string StaticServer::getMimeType(const std::string& extension) {
    static std::unordered_map<std::string, std::string> mimeTypes = {
        {".html", "text/html"},
        {".htm", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".json", "application/json"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".ico", "image/x-icon"},
        {".svg", "image/svg+xml"},
        {".wasm", "application/wasm"},
        {".txt", "text/plain"},
        {".xml", "application/xml"}
    };

    auto it = mimeTypes.find(extension);
    return (it != mimeTypes.end()) ? it->second : "application/octet-stream";
}

std::string StaticServer::urlDecode(const std::string& encoded) {
    std::string decoded;
    decoded.reserve(encoded.length());

    for (size_t i = 0; i < encoded.length(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.length()) {
            int value;
            std::istringstream is(encoded.substr(i + 1, 2));
            if (is >> std::hex >> value) {
                decoded += static_cast<char>(value);
                i += 2;
            } else {
                decoded += encoded[i];
            }
        } else if (encoded[i] == '+') {
            decoded += ' ';
        } else {
            decoded += encoded[i];
        }
    }

    return decoded;
}

bool StaticServer::isPathSafe(const std::string& path) {
    // Check for directory traversal attempts
    if (path.find("..") != std::string::npos) {
        return false;
    }
    
    // Path must start with /
    if (path.empty() || path[0] != '/') {
        return false;
    }

    // Check for null bytes
    if (path.find('\0') != std::string::npos) {
        return false;
    }

    return true;
}

void StaticServer::sendResponse(socket_t clientSocket, int statusCode, const std::string& contentType, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode;
    
    switch (statusCode) {
        case 200: response << " OK"; break;
        case 404: response << " Not Found"; break;
        case 500: response << " Internal Server Error"; break;
        default: response << " Unknown"; break;
    }
    
    response << "\r\n";
    response << "Content-Type: " << contentType << "\r\n";
    response << "Content-Length: " << body.length() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << body;

    std::string responseStr = response.str();
    send(clientSocket, responseStr.c_str(), responseStr.length(), 0);
}

void StaticServer::send404(socket_t clientSocket) {
    std::string body = "<html><body><h1>404 Not Found</h1></body></html>";
    sendResponse(clientSocket, 404, "text/html", body);
}

void StaticServer::send500(socket_t clientSocket) {
    std::string body = "<html><body><h1>500 Internal Server Error</h1></body></html>";
    sendResponse(clientSocket, 500, "text/html", body);
}

std::string StaticServer::parseHTTPRequest(const std::string& request, std::string& method, std::string& path, std::string& headers, std::string& body) {
    std::istringstream stream(request);
    std::string version;
    
    // Parse first line: METHOD PATH VERSION
    if (!(stream >> method >> path >> version)) {
        return "";
    }
    
    // Read rest of the first line
    std::string line;
    std::getline(stream, line);
    
    // Read headers until empty line
    std::ostringstream headerStream;
    while (std::getline(stream, line) && line != "\r" && !line.empty()) {
        headerStream << line << "\n";
    }
    headers = headerStream.str();
    
    // Read body (everything after headers)
    std::ostringstream bodyStream;
    while (std::getline(stream, line)) {
        bodyStream << line << "\n";
    }
    body = bodyStream.str();
    
    // Remove trailing newline from body if present
    if (!body.empty() && body.back() == '\n') {
        body.pop_back();
    }
    
    return version;
} 