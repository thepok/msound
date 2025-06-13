#include "SSEServer.h"
#include <iostream>
#include <sstream>
#include <algorithm>

SSEServer::SSEServer() {
}

SSEServer::~SSEServer() {
    cleanup();
}

void SSEServer::addClient(socket_t clientSocket) {
    std::lock_guard<std::mutex> lock(clientsMutex);
    clients.push_back(clientSocket);
    sendSSEHeaders(clientSocket);
    std::cout << "SSE client connected. Total clients: " << clients.size() << std::endl;
    
    // Send initial state if callback is set
    if (initialStateCallback) {
        initialStateCallback(clientSocket);
    }
}

void SSEServer::removeClient(socket_t clientSocket) {
    std::lock_guard<std::mutex> lock(clientsMutex);
    clients.erase(std::remove(clients.begin(), clients.end(), clientSocket), clients.end());
    std::cout << "SSE client disconnected. Total clients: " << clients.size() << std::endl;
}

void SSEServer::broadcastParameterUpdate(const std::string& paramName, float paramValue) {
    std::ostringstream oss;
    oss << "{\"type\":\"param_update\","
        << "\"param\":\"" << paramName << "\","
        << "\"value\":" << paramValue << "}";
    broadcastSSEEvent(oss.str());
}

void SSEServer::broadcastVoiceChange(const std::string& voiceName) {
    std::ostringstream oss;
    oss << "{\"type\":\"voice_generator_change\","
        << "\"voiceGenerator\":\"" << voiceName << "\"}";
    broadcastSSEEvent(oss.str());
}

void SSEServer::setInitialStateCallback(InitialStateCallback callback) {
    initialStateCallback = callback;
}

void SSEServer::sendInitialState(socket_t clientSocket, const std::string& allParamsJson, const std::string& allVoicesJson) {
    // Send all parameters
    sendSSEEvent(clientSocket, allParamsJson);
    
    // Send all voices
    sendSSEEvent(clientSocket, allVoicesJson);
}

void SSEServer::cleanup() {
    std::lock_guard<std::mutex> lock(clientsMutex);
    for (auto client : clients) {
        closesocket(client);
    }
    clients.clear();
}

void SSEServer::sendSSEHeaders(socket_t clientSocket) {
    std::string headers = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";
    
    send(clientSocket, headers.c_str(), headers.length(), 0);
}

void SSEServer::sendSSEEvent(socket_t clientSocket, const std::string& data, const std::string& event) {
    if (!isSocketValid(clientSocket)) {
        return;
    }

    std::ostringstream oss;
    if (!event.empty()) {
        oss << "event: " << event << "\n";
    }
    oss << "data: " << data << "\n\n";
    
    std::string message = oss.str();
    int result = send(clientSocket, message.c_str(), message.length(), 0);
    
    if (result == SOCKET_ERROR) {
        std::cout << "Failed to send SSE event to client, will remove from list" << std::endl;
        // Note: We can't remove from clients list here due to mutex issues
        // The client will be removed when the connection is detected as broken
    }
}

void SSEServer::broadcastSSEEvent(const std::string& data, const std::string& event) {
    std::lock_guard<std::mutex> lock(clientsMutex);
    std::vector<socket_t> disconnectedClients;
    
    for (auto client : clients) {
        if (!isSocketValid(client)) {
            disconnectedClients.push_back(client);
            continue;
        }
        
        std::ostringstream oss;
        if (!event.empty()) {
            oss << "event: " << event << "\n";
        }
        oss << "data: " << data << "\n\n";
        
        std::string message = oss.str();
        int result = send(client, message.c_str(), message.length(), 0);
        
        if (result == SOCKET_ERROR) {
            disconnectedClients.push_back(client);
        }
    }
    
    // Remove disconnected clients
    for (auto disconnectedClient : disconnectedClients) {
        clients.erase(std::remove(clients.begin(), clients.end(), disconnectedClient), clients.end());
        closesocket(disconnectedClient);
    }
    
    if (!disconnectedClients.empty()) {
        std::cout << "Removed " << disconnectedClients.size() << " disconnected SSE clients. Active clients: " << clients.size() << std::endl;
    }
}

bool SSEServer::isSocketValid(socket_t clientSocket) {
    if (clientSocket == INVALID_SOCKET) {
        return false;
    }
    
    // Quick check by trying to send 0 bytes
    int result = send(clientSocket, "", 0, 0);
    return result != SOCKET_ERROR;
} 