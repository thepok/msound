#include "HTTPAPIHandler.h"
#include <iostream>
#include <sstream>
#include <stdexcept>

HTTPAPIHandler::HTTPAPIHandler() {
}

HTTPAPIHandler::~HTTPAPIHandler() {
}

void HTTPAPIHandler::setParameterUpdateCallback(ParameterUpdateCallback callback) {
    parameterUpdateCallback = callback;
}

void HTTPAPIHandler::setVoiceChangeCallback(VoiceChangeCallback callback) {
    voiceChangeCallback = callback;
}

void HTTPAPIHandler::setWaveformDataCallback(WaveformDataCallback callback) {
    waveformDataCallback = callback;
}

bool HTTPAPIHandler::handleAPIRequest(socket_t clientSocket, const std::string& method, const std::string& path, const std::string& body) {
    if (method == "GET" && path == "/api/waveform") {
        return handleWaveformRequest(clientSocket);
    }
    
    if (method != "POST") {
        sendErrorResponse(clientSocket, 405, "Method Not Allowed");
        return true;
    }

    if (path == "/api/parameter") {
        return handleParameterUpdate(clientSocket, body);
    } else if (path == "/api/voice") {
        return handleVoiceChange(clientSocket, body);
    } else {
        sendErrorResponse(clientSocket, 404, "API endpoint not found");
        return true;
    }
}

void HTTPAPIHandler::sendJSONResponse(socket_t clientSocket, int statusCode, const std::string& json) {
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode;
    
    switch (statusCode) {
        case 200: response << " OK"; break;
        case 400: response << " Bad Request"; break;
        case 404: response << " Not Found"; break;
        case 405: response << " Method Not Allowed"; break;
        case 500: response << " Internal Server Error"; break;
        default: response << " Unknown"; break;
    }
    
    response << "\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << json.length() << "\r\n";
    response << "Access-Control-Allow-Origin: *\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << json;

    std::string responseStr = response.str();
    send(clientSocket, responseStr.c_str(), responseStr.length(), 0);
}

void HTTPAPIHandler::sendErrorResponse(socket_t clientSocket, int statusCode, const std::string& message) {
    std::ostringstream json;
    json << "{\"error\":\"" << message << "\"}";
    sendJSONResponse(clientSocket, statusCode, json.str());
}

bool HTTPAPIHandler::handleParameterUpdate(socket_t clientSocket, const std::string& body) {
    try {
        std::string paramName = extractJSONValue(body, "param");
        std::string valueStr = extractJSONValue(body, "value");
        
        if (paramName.empty() || valueStr.empty()) {
            sendErrorResponse(clientSocket, 400, "Missing param or value in request");
            return true;
        }
        
        float paramValue = parseFloat(valueStr);
        
        if (parameterUpdateCallback) {
            parameterUpdateCallback(paramName, paramValue);
        }
        
        sendJSONResponse(clientSocket, 200, "{\"status\":\"success\"}");
        std::cout << "API: Parameter " << paramName << " updated to " << paramValue << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error handling parameter update: " << e.what() << std::endl;
        sendErrorResponse(clientSocket, 400, "Invalid request format");
    }
    
    return true;
}

bool HTTPAPIHandler::handleVoiceChange(socket_t clientSocket, const std::string& body) {
    try {
        std::string voiceName = extractJSONValue(body, "voiceGenerator");
        
        if (voiceName.empty()) {
            sendErrorResponse(clientSocket, 400, "Missing voiceGenerator in request");
            return true;
        }
        
        if (voiceChangeCallback) {
            voiceChangeCallback(voiceName);
        }
        
        sendJSONResponse(clientSocket, 200, "{\"status\":\"success\"}");
        std::cout << "API: Voice generator changed to " << voiceName << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error handling voice change: " << e.what() << std::endl;
        sendErrorResponse(clientSocket, 400, "Invalid request format");
    }
    
    return true;
}

std::string HTTPAPIHandler::extractJSONValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) {
        return "";
    }
    
    size_t valueStart = keyPos + searchKey.length();
    
    // Skip whitespace
    while (valueStart < json.length() && (json[valueStart] == ' ' || json[valueStart] == '\t')) {
        valueStart++;
    }
    
    if (valueStart >= json.length()) {
        return "";
    }
    
    // Check if value is a string (starts with quote)
    if (json[valueStart] == '"') {
        valueStart++; // Skip opening quote
        size_t valueEnd = json.find('"', valueStart);
        if (valueEnd == std::string::npos) {
            return "";
        }
        return json.substr(valueStart, valueEnd - valueStart);
    } else {
        // Numeric value - find end
        size_t valueEnd = valueStart;
        while (valueEnd < json.length() && 
               (std::isdigit(json[valueEnd]) || json[valueEnd] == '.' || json[valueEnd] == '-' || json[valueEnd] == '+')) {
            valueEnd++;
        }
        return json.substr(valueStart, valueEnd - valueStart);
    }
}

float HTTPAPIHandler::parseFloat(const std::string& str) {
    try {
        return std::stof(str);
    } catch (const std::invalid_argument& e) {
        throw std::runtime_error("Invalid float format: " + str);
    } catch (const std::out_of_range& e) {
        throw std::runtime_error("Float out of range: " + str);
    }
}

bool HTTPAPIHandler::handleWaveformRequest(socket_t clientSocket) {
    try {
        if (!waveformDataCallback) {
            sendErrorResponse(clientSocket, 500, "Waveform data not available");
            return true;
        }
        
        std::vector<float> waveformData = waveformDataCallback();
        
        std::ostringstream json;
        json << "{\"waveform\":[";
        for (size_t i = 0; i < waveformData.size(); ++i) {
            json << waveformData[i];
            if (i < waveformData.size() - 1) {
                json << ",";
            }
        }
        json << "]}";
        
        sendJSONResponse(clientSocket, 200, json.str());
        
    } catch (const std::exception& e) {
        std::cerr << "Error handling waveform request: " << e.what() << std::endl;
        sendErrorResponse(clientSocket, 500, "Internal server error");
    }
    
    return true;
} 