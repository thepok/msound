#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
typedef SOCKET socket_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

#include <string>
#include <atomic>
#include <thread>
#include <memory>

// Forward declarations
class SSEServer;
class HTTPAPIHandler;

class StaticServer {
public:
    StaticServer(const std::string& rootDir, uint16_t port = 8080);
    ~StaticServer();

    bool start();
    void stop();
    
    // Set handlers for SSE and API
    void setSSEServer(std::shared_ptr<SSEServer> sseServer);
    void setHTTPAPIHandler(std::shared_ptr<HTTPAPIHandler> apiHandler);

private:
    std::string rootDirectory;
    uint16_t serverPort;
    std::atomic<bool> running;
    std::thread serverThread;
    socket_t listenSocket;
    std::shared_ptr<SSEServer> sseServer;
    std::shared_ptr<HTTPAPIHandler> httpAPIHandler;

    void serverLoop();
    void handleClient(socket_t clientSocket, bool& shouldCloseSocket);
    bool handleHTTPRequest(socket_t clientSocket, const std::string& method, const std::string& path, const std::string& headers, const std::string& body);
    void handleStaticFile(socket_t clientSocket, const std::string& path);
    std::string getMimeType(const std::string& extension);
    std::string urlDecode(const std::string& encoded);
    bool isPathSafe(const std::string& path);
    void sendResponse(socket_t clientSocket, int statusCode, const std::string& contentType, const std::string& body);
    void send404(socket_t clientSocket);
    void send500(socket_t clientSocket);
    std::string parseHTTPRequest(const std::string& request, std::string& method, std::string& path, std::string& headers, std::string& body);
}; 