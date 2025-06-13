#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
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
#include <vector>
#include <mutex>
#include <memory>
#include <functional>

class SSEServer {
public:
    using InitialStateCallback = std::function<void(socket_t)>;
    
    SSEServer();
    ~SSEServer();

    void addClient(socket_t clientSocket);
    void removeClient(socket_t clientSocket);
    void broadcastParameterUpdate(const std::string& paramName, float paramValue);
    void broadcastVoiceChange(const std::string& voiceName);
    void broadcastSSEEvent(const std::string& data, const std::string& event = "");
    void sendInitialState(socket_t clientSocket, const std::string& allParamsJson, const std::string& allVoicesJson);
    void setInitialStateCallback(InitialStateCallback callback);
    void cleanup();

private:
    std::vector<socket_t> clients;
    std::mutex clientsMutex;
    InitialStateCallback initialStateCallback;

    void sendSSEHeaders(socket_t clientSocket);
    void sendSSEEvent(socket_t clientSocket, const std::string& data, const std::string& event = "");
    bool isSocketValid(socket_t clientSocket);
}; 