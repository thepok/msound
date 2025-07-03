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
#include <functional>

class HTTPAPIHandler {
public:
    // Callback function types
    using ParameterUpdateCallback = std::function<void(const std::string&, float)>;
    using VoiceChangeCallback = std::function<void(const std::string&)>;
    using WaveformDataCallback = std::function<std::vector<float>()>;

    HTTPAPIHandler();
    ~HTTPAPIHandler();

    void setParameterUpdateCallback(ParameterUpdateCallback callback);
    void setVoiceChangeCallback(VoiceChangeCallback callback);
    void setWaveformDataCallback(WaveformDataCallback callback);
    
    bool handleAPIRequest(socket_t clientSocket, const std::string& method, const std::string& path, const std::string& body);

private:
    ParameterUpdateCallback parameterUpdateCallback;
    VoiceChangeCallback voiceChangeCallback;
    WaveformDataCallback waveformDataCallback;

    void sendJSONResponse(socket_t clientSocket, int statusCode, const std::string& json);
    void sendErrorResponse(socket_t clientSocket, int statusCode, const std::string& message);
    bool handleParameterUpdate(socket_t clientSocket, const std::string& body);
    bool handleVoiceChange(socket_t clientSocket, const std::string& body);
    bool handleWaveformRequest(socket_t clientSocket);
    std::string extractJSONValue(const std::string& json, const std::string& key);
    float parseFloat(const std::string& str);
}; 