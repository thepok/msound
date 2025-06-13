#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <windows.h>
#include <mmsystem.h>
#include <memory>
#include <unordered_map>
#include <string>
#include <iostream>
#include <thread>
#include <atomic>
#include <map>
#include <cmath>
#include "ActiveTones.cpp"
#include "civetweb.h"
#include "StaticServer.h"
#include "Parameter.cpp"      // Include the Parameter class definition
#include <sstream>          // For std::ostringstream
#include <string>           // For std::string
#include <iostream>         // For std::ostream (if needed)
#include "VoiceGeneratorRepository.cpp"

class ActiveTones;

class MidiHandler {
public:
    MidiHandler(std::shared_ptr<ActiveTones> activeTones)
        : activeTones(activeTones), hMidiIn(nullptr) {
        // Map MIDI controllers to parameter names
        midiToParamName[70] = "Attack";
        midiToParamName[71] = "Decay";
        midiToParamName[72] = "Sustain";
        midiToParamName[73] = "Release";
    }

    bool initialize() {
        UINT nMidiDeviceNum = midiInGetNumDevs();
        if (nMidiDeviceNum == 0) {
            std::cout << "No MIDI input devices available." << std::endl;
            return false;
        }

        MMRESULT result = midiInOpen(&hMidiIn, 0, reinterpret_cast<DWORD_PTR>(MidiInProcStatic), reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
        if (result != MMSYSERR_NOERROR) {
            std::cout << "Failed to open MIDI input device." << std::endl;
            return false;
        }

        result = midiInStart(hMidiIn);
        if (result != MMSYSERR_NOERROR) {
            std::cout << "Failed to start MIDI input." << std::endl;
            midiInClose(hMidiIn);
            return false;
        }

        std::cout << "Listening for MIDI messages." << std::endl;
        return true;
    }

    void shutdown() {
        if (hMidiIn) {
            midiInStop(hMidiIn);
            midiInClose(hMidiIn);
            hMidiIn = nullptr;
        }
    }

    void handleControlChange(BYTE controller, BYTE value) {
        auto params = activeTones->getParameters();
        auto it = midiToParamName.find(controller);
        if (it != midiToParamName.end()) {
            const std::string& paramName = it->second;
            for (auto& param : params) {
                if (param->getName() == paramName) {
                    float normalizedValue = static_cast<float>(value) / 127.0f;
                    float newValue = param->getMinValue() + normalizedValue * (param->getMaxValue() - param->getMinValue());
                    param->setValue(newValue);
                    std::cout << "Parameter " << paramName << " set to " << newValue << " " << param->getUnit() << std::endl;
                    break;
                }
            }
        }
    }

    void handleNoteOn(BYTE note, BYTE velocity, BYTE channel) {
        float frequency = 440.0f * powf(2.0f, (note - 69) / 12.0f); // Convert MIDI note to frequency
        float volume = static_cast<float>(velocity) / 127.0f; // Normalize velocity to [0.0, 1.0]
        activeTones->noteOn(note, channel, frequency, volume);
    }

    void handleNoteOff(BYTE note, BYTE channel) {
        activeTones->noteOff(note, channel);
    }

private:
    std::shared_ptr<ActiveTones> activeTones;
    HMIDIIN hMidiIn;
    std::unordered_map<int, std::string> midiToParamName;

    void MidiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
        if (wMsg == MIM_DATA) {
            BYTE status = dwParam1 & 0xFF;
            BYTE data1 = (dwParam1 >> 8) & 0xFF;
            BYTE data2 = (dwParam1 >> 16) & 0xFF;
            BYTE channel = (status & 0x0F) + 1;

            if ((status & 0xF0) == 0xB0) { // Control Change
                handleControlChange(data1, data2);
            } else if ((status & 0xF0) == 0x90) { // Note On
                if (data2 > 0) {
                    handleNoteOn(data1, data2, channel);
                } else {
                    handleNoteOff(data1, channel); // Note On with velocity 0 is treated as Note Off
                }
            } else if ((status & 0xF0) == 0x80) { // Note Off
                handleNoteOff(data1, channel);
            }
        }
    }

    static void CALLBACK MidiInProcStatic(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
        if (dwInstance) {
            MidiHandler* handler = reinterpret_cast<MidiHandler*>(dwInstance);
            handler->MidiInProc(hMidiIn, wMsg, dwParam1, dwParam2);
        }
    }
};
// KeyboardHandler class
class KeyboardHandler {
public:
    KeyboardHandler(std::shared_ptr<ActiveTones> activeTonesPtr)
        : activeTones(activeTonesPtr), running(false), selectedParameter(0) {}

    ~KeyboardHandler() {
        stop();
    }

    void start() {
        running = true;
        handlerThread = std::thread(&KeyboardHandler::processKeyboard, this);
    }

    void stop() {
        running = false;
        if (handlerThread.joinable()) {
            handlerThread.join();
        }
    }

private:
    std::shared_ptr<ActiveTones> activeTones;
    std::thread handlerThread;
    std::atomic<bool> running;
    size_t selectedParameter;

    // Define key to MIDI note mapping
    std::map<int, int> keyToMidi = {
        // Row 1 (QWERTY)
        { 'A', 60 }, // C4
        { 'W', 61 },
        { 'S', 62 },
        { 'E', 63 },
        { 'D', 64 },
        { 'F', 65 },
        { 'T', 66 },
        { 'G', 67 },
        { 'Y', 68 },
        { 'H', 69 },
        { 'U', 70 },
        { 'J', 71 },
        { 'K', 72 }  // C5
        // Add more mappings as needed
    };

    // Track the state of keys to detect key up/down
    std::map<int, bool> keyStates;

    void processKeyboard() {
        while (running) {
            for (const auto& [key, midiNote] : keyToMidi) {
                bool isPressed = (GetAsyncKeyState(key) & 0x8000) != 0;

                if (isPressed && !keyStates[key]) {
                    // Key Down Event
                    keyStates[key] = true;
                    float frequency = 440.0f * powf(2.0f, (midiNote - 69) / 12.0f);
                    float volume = 100.0f / 127.0f; // Normalize velocity 100 to [0.0, 1.0]
                    activeTones->noteOn(midiNote, 0, frequency, volume); // Channel 0 for keyboard
                }
                else if (!isPressed && keyStates[key]) {
                    // Key Up Event
                    keyStates[key] = false;
                    activeTones->noteOff(midiNote, 0); // Channel 0 for keyboard
                }
            }

            // Handle parameter selection and adjustment
            if (GetAsyncKeyState(VK_UP) & 0x8000) {
                selectedParameter = (selectedParameter - 1 + activeTones->getParameters().size()) % activeTones->getParameters().size();
                std::cout << "Selected Parameter: " << activeTones->getParameters()[selectedParameter]->getName() << std::endl;
                Sleep(200); // Debounce
            }
            if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
                selectedParameter = (selectedParameter + 1) % activeTones->getParameters().size();
                std::cout << "Selected Parameter: " << activeTones->getParameters()[selectedParameter]->getName() << std::endl;
                Sleep(200); // Debounce
            }
            if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
                adjustParameterValue(selectedParameter, -activeTones->getParameters()[selectedParameter]->getStepSize());
                Sleep(100); // Debounce
            }
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
                adjustParameterValue(selectedParameter, activeTones->getParameters()[selectedParameter]->getStepSize());
                Sleep(100); // Debounce
            }

            Sleep(10); // Polling interval
        }
    }

    void adjustParameterValue(size_t paramIndex, float delta) {
        auto& params = activeTones->getParameters();
        if (paramIndex >= 0 && paramIndex < params.size()) {
            auto& param = params[paramIndex]; // Cast away constness if necessary
            float currentValue = param->getValue();
            float newValue = std::clamp(currentValue + delta, param->getMinValue(), param->getMaxValue());
            param->setValue(newValue);
            std::cout << "Parameter " << param->getName() << " adjusted to " << newValue << " " << param->getUnit() << std::endl;
        }
    }
};


class ServerHandler {
public:
    ServerHandler(std::shared_ptr<SoundGenerator> soundGeneratorPtr,
                  const VoiceGeneratorRepository& voiceRepo,
                  std::shared_ptr<ActiveTones> activeTonesPtr)
        : soundGenerator(soundGeneratorPtr), voiceGeneratorRepo(voiceRepo),
          activeTones(activeTonesPtr), ctx(nullptr) {}

    bool initialize() {
        // Get the executable's path
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        
        // Extract the directory path
        char* lastSlash = strrchr(exePath, '\\');
        if (lastSlash) {
            *lastSlash = '\0'; // Remove the executable name
        }

        // Change to the executable's directory
        SetCurrentDirectoryA(exePath);

        // Start the static file server on port 8080
        staticServer = std::make_unique<StaticServer>("./", 8080);
        if (!staticServer->start()) {
            std::cerr << "Failed to start static server on port 8080" << std::endl;
            return false;
        }

        // Initialize CivetWeb for WebSocket handling only on port 8081
        mg_init_library(0);

        const char *options[] = {
            "listening_ports", "8082",
            nullptr
        };

        struct mg_callbacks callbacks = {0};
        struct mg_init_data mg_start_init_data = {0};
        mg_start_init_data.callbacks = &callbacks;
        mg_start_init_data.user_data = this;
        mg_start_init_data.configuration_options = options;

        struct mg_error_data mg_start_error_data = {0};
        char errtxtbuf[256] = {0};
        mg_start_error_data.text = errtxtbuf;
        mg_start_error_data.text_buffer_size = sizeof(errtxtbuf);

        ctx = mg_start2(&mg_start_init_data, &mg_start_error_data);
        if (!ctx) {
            std::cerr << "Cannot start WebSocket server: " << errtxtbuf << std::endl;
            staticServer->stop();
            return false;
        }

        mg_set_websocket_handler(ctx,
                                 "/ws",
                                 ws_connect_handler,
                                 ws_ready_handler,
                                 ws_data_handler,
                                 ws_close_handler,
                                 this);

        std::cout << "Static server started on port 8080." << std::endl;
        std::cout << "WebSocket server started on port 8082." << std::endl;
        
        // Open the default web browser
        openDefaultBrowser();

        return true;
    }

    void shutdown() {
        if (staticServer) {
            staticServer->stop();
            staticServer.reset();
        }
        if (ctx) {
            mg_stop(ctx);
            ctx = nullptr;
        }
        mg_exit_library();
    }

private:
    std::shared_ptr<SoundGenerator> soundGenerator;
    const VoiceGeneratorRepository& voiceGeneratorRepo;
    std::shared_ptr<ActiveTones> activeTones;
    struct mg_context *ctx;
    std::unique_ptr<StaticServer> staticServer;

    std::vector<struct mg_connection*> connections;
    std::mutex connectionsMutex;

    static int ws_connect_handler(const struct mg_connection *conn, void *user_data) {
        ServerHandler *handler = static_cast<ServerHandler*>(user_data);
        std::lock_guard<std::mutex> lock(handler->connectionsMutex);
        handler->connections.push_back(const_cast<struct mg_connection*>(conn));
        std::cout << "New WebSocket connection established." << std::endl;
        return 0;
    }

    static void ws_ready_handler(struct mg_connection *conn, void *user_data) {
        ServerHandler *handler = static_cast<ServerHandler*>(user_data);
        handler->sendAllParameters(conn);
        handler->sendAllVoices(conn);
    }

    static int ws_data_handler(struct mg_connection *conn, int opcode, char *data, size_t datasize, void *user_data) {
        ServerHandler *handler = static_cast<ServerHandler*>(user_data);
        std::string msg(data, datasize);
        std::cout << "Received message: [" << msg << "]" << std::endl;

        // Parse the JSON message
        std::string paramName;
        float paramValue = 0.0f;
        std::string voiceGeneratorName;

        size_t paramPos = msg.find("\"param\":\"");
        if (paramPos != std::string::npos) {
            size_t start = paramPos + 9; // Length of "\"param\":\""
            size_t end = msg.find("\"", start);
            if (end != std::string::npos) {
                paramName = msg.substr(start, end - start);
            }
        }

        size_t valuePos = msg.find("\"value\":");
        if (valuePos != std::string::npos) {
            size_t start = valuePos + 8; // Length of "\"value\":"
            size_t end = msg.find("}", start);
            if (end != std::string::npos) {
                try {
                    paramValue = std::stof(msg.substr(start, end - start));
                } catch (const std::invalid_argument& e) {
                    std::cerr << "Invalid value format: " << e.what() << std::endl;
                }
            }
        }

    size_t firstQuote = msg.find('"');
    size_t secondQuote = msg.find('"', firstQuote + 1);
    size_t thirdQuote = msg.find('"', secondQuote + 1);
    size_t fourthQuote = msg.find('"', thirdQuote + 1);

    if (thirdQuote != std::string::npos && fourthQuote != std::string::npos) {
        std::string key = msg.substr(firstQuote + 1, secondQuote - firstQuote - 1);
        std::string value = msg.substr(thirdQuote + 1, fourthQuote - thirdQuote - 1);
        
        if (key == "voiceGenerator") {
            std::cout << "Changing voice generator to: " << value << std::endl;
            handler->changeVoiceGenerator(value);
            std::cout << "Voice generator changed successfully to: " << value << std::endl;
        }
        // Add more key handling as needed
    } else {
        std::cout << "voiceGenerator key not found in the message" << std::endl;
    }

        if (!paramName.empty()) {
            handler->updateParameter(paramName, paramValue);
        } else if (!voiceGeneratorName.empty()) {
            std::cout << "Changing voice generator to: " << voiceGeneratorName << std::endl;
            handler->changeVoiceGenerator(voiceGeneratorName);
            std::cout << "Voice generator changed successfully to: " << voiceGeneratorName << std::endl;
        }

        return 1;
    }

    static void ws_close_handler(const struct mg_connection *conn, void *user_data) {
        ServerHandler *handler = static_cast<ServerHandler*>(user_data);
        std::lock_guard<std::mutex> lock(handler->connectionsMutex);
        handler->connections.erase(
            std::remove(handler->connections.begin(), handler->connections.end(), const_cast<struct mg_connection*>(conn)),
            handler->connections.end()
        );
        std::cout << "WebSocket connection closed." << std::endl;
    }

    void sendAllParameters(struct mg_connection *conn) {
        std::ostringstream oss;
        oss << "{\"type\":\"all_params\",\"params\":[";
    
        auto params = soundGenerator->getParameters();
        for (size_t i = 0; i < params.size(); ++i) {
            const auto& param = params[i];
            oss << "{\"name\":\"" << param->getName() << "\","
                << "\"value\":" << param->getValue() << ","
                << "\"min\":" << param->getMinValue() << ","
                << "\"max\":" << param->getMaxValue() << ","
                << "\"step\":" << param->getStepSize() << ","
                << "\"unit\":\"" << param->getUnit() << "\"}";
            if (i < params.size() - 1) {
                oss << ",";
            }
        }
        oss << "]}";

        std::string msg = oss.str();
        mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, msg.c_str(), msg.length());
    }

    void sendAllVoices(struct mg_connection *conn) {
        std::ostringstream oss;
        oss << "{\"type\":\"all_voices\",\"voiceGenerators\":[";

        auto voiceGenNames = voiceGeneratorRepo.getVoiceGeneratorNames();
        for (size_t i = 0; i < voiceGenNames.size(); ++i) {
            oss << "\"" << voiceGenNames[i] << "\"";
            if (i < voiceGenNames.size() - 1) {
                oss << ",";
            }
        }
        oss << "]}";

        std::string msg = oss.str();
        mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, msg.c_str(), msg.length());
    }

    void broadcastParameterUpdate(const std::string& paramName, float paramValue) {
        std::ostringstream oss;
        oss << "{\"type\":\"param_update\","
            << "\"param\":\"" << paramName << "\","
            << "\"value\":" << paramValue << "}";

        std::string msg = oss.str();
        std::lock_guard<std::mutex> lock(connectionsMutex);
        for (auto conn : connections) {
            mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, msg.c_str(), msg.length());
        }
    }

    void updateParameter(const std::string& paramName, float paramValue) {
        auto& params = soundGenerator->getParameters();
        for (auto& param : params) {
            if (param->getName() == paramName) {
                param->setValue(paramValue);
                std::cout << "Parameter " << paramName << " updated to " << paramValue << std::endl;
                broadcastParameterUpdate(paramName, paramValue);
                break;
            }
        }
    }

    void changeVoiceGenerator(const std::string& voiceGeneratorName) {
        try {
            auto newVoiceGenerator = voiceGeneratorRepo.getVoiceGenerator(voiceGeneratorName);
            activeTones->setVoiceGenerator(newVoiceGenerator);
            std::cout << "Voice generator changed to: " << voiceGeneratorName << std::endl;
            broadcastVoiceGeneratorChange(voiceGeneratorName);
            
            // Rebroadcast all parameters after voice change
            std::lock_guard<std::mutex> lock(connectionsMutex);
            for (auto conn : connections) {
                sendAllParameters(conn);
            }
        } catch (const std::runtime_error& e) {
            std::cerr << "Error changing voice generator: " << e.what() << std::endl;
        }
    }

    void broadcastVoiceGeneratorChange(const std::string& voiceGeneratorName) {
        std::ostringstream oss;
        oss << "{\"type\":\"voice_generator_change\","
            << "\"voiceGenerator\":\"" << voiceGeneratorName << "\"}";

        std::string msg = oss.str();
        std::lock_guard<std::mutex> lock(connectionsMutex);
        for (auto conn : connections) {
            mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, msg.c_str(), msg.length());
        }
    }

    void openDefaultBrowser() {
        const char* url = "http://localhost:8080/gui.html";
        ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
        std::cout << "Opening default web browser to " << url << std::endl;
    }
};
