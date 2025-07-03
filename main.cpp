#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <windows.h>
#include <inttypes.h>
#include <math.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdio.h>
#include <random>
#include <algorithm>
#include <cmath>
#include <mmsystem.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <map>
#include <memory>
#include <chrono>
#include <functional>
#include "Effects.cpp"
#include "SoundGenerator.cpp"
#include "Parameter.cpp"
#include "Voices.cpp"
#include "ADSRGenerator.cpp"
#include "ActiveTones.cpp"
#include <shellapi.h>
#include "math.cpp"
#include "VoiceGeneratorRepository.cpp"
#include "mixer.cpp"
#include "presets.cpp"

// Constants
#define BYTES_PER_CHANNEL sizeof(float)
#define CHANNELS_PER_SAMPLE 1
#define SAMPLES_PER_SECOND 44100
#define OSCILLATORS_PER_TONE 3
#define REVERB_BUFFER_SIZE (SAMPLES_PER_SECOND / 10)
#define NUM_REVERB_TAPS 64

// Forward Declarations
class SoundGenerator;
class Tone;
class AudioEngine;

// AudioEngine class with adjusted constructor and processAudio method
class AudioEngine {
public:
    AudioEngine(std::shared_ptr<SoundGenerator> generator)
        : soundGenerator(generator),
          running(true),
          enumerator(nullptr),
          endpoint(nullptr),
          audioClient(nullptr),
          renderClient(nullptr),
          mixFormat(nullptr),
          defaultDevicePeriod(0),
          minDevicePeriod(0),
          bufferSampleCount(0) {}

    ~AudioEngine() {
        shutdown();
    }

    bool initialize() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize COM library." << std::endl;
        return false;
    }

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    if (FAILED(hr)) {
        std::cerr << "Failed to create MMDeviceEnumerator instance." << std::endl;
        return false;
    }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint);
    if (FAILED(hr)) {
        std::cerr << "Failed to get default audio endpoint." << std::endl;
        return false;
    }

    hr = endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&audioClient);
    if (FAILED(hr)) {
        std::cerr << "Failed to activate audio client." << std::endl;
        return false;
    }

    hr = audioClient->GetMixFormat(&mixFormat);
    if (FAILED(hr)) {
        std::cerr << "Failed to get mix format." << std::endl;
        return false;
    }

    printMixFormat();

    // Free mixFormat as we will create our own format
    CoTaskMemFree(mixFormat);
    mixFormat = nullptr;

    WAVEFORMATEX* format = nullptr;
    format = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
    ZeroMemory(format, sizeof(WAVEFORMATEX));

    format->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format->nChannels = CHANNELS_PER_SAMPLE;
    format->nSamplesPerSec = SAMPLES_PER_SECOND;
    format->wBitsPerSample = BYTES_PER_CHANNEL * 8;
    format->nBlockAlign = (CHANNELS_PER_SAMPLE * BYTES_PER_CHANNEL);
    format->nAvgBytesPerSec = SAMPLES_PER_SECOND * format->nBlockAlign;
    format->cbSize = 0;

    hr = audioClient->GetDevicePeriod(&defaultDevicePeriod, &minDevicePeriod);
    if (FAILED(hr)) {
        std::cerr << "Failed to get device period." << std::endl;
        CoTaskMemFree(format);
        return false;
    }

    hr = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
        defaultDevicePeriod,
        0,
        format,
        0
    );
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize audio client." << std::endl;
        CoTaskMemFree(format);
        return false;
    }

    hr = audioClient->GetService(__uuidof(IAudioRenderClient), (void**)&renderClient);
    if (FAILED(hr)) {
        std::cerr << "Failed to get render client service." << std::endl;
        CoTaskMemFree(format);
        return false;
    }

    hr = audioClient->GetBufferSize(&bufferSampleCount);
    if (FAILED(hr)) {
        std::cerr << "Failed to get buffer size." << std::endl;
        CoTaskMemFree(format);
        return false;
    }
    std::cout << "Buffer size: " << bufferSampleCount << std::endl;

    hr = audioClient->Start();
    if (FAILED(hr)) {
        std::cerr << "Failed to start audio stream." << std::endl;
        CoTaskMemFree(format);
        return false;
    }

    // Free the format structure after initialization
    CoTaskMemFree(format);

    return true;
}

    void processAudio() {
        UINT32 prevAvailableSamples = 0;
        while (running) {
            UINT32 paddingSampleCount;
            HRESULT hr = audioClient->GetCurrentPadding(&paddingSampleCount);
            if (FAILED(hr)) {
                std::cerr << "Failed to get current padding." << std::endl;
                break;
            }

            UINT32 availableSamples = bufferSampleCount - paddingSampleCount;

            if (availableSamples > 100 && prevAvailableSamples > 100) {
                std::cout << "Überlastet" << std::endl;
            }

            BYTE* buffer;
            hr = renderClient->GetBuffer(availableSamples, &buffer);
            if (SUCCEEDED(hr)) {
                float* floatBuffer = reinterpret_cast<float*>(buffer);

                for (UINT32 i = 0; i < availableSamples; ++i) {
                    float outputSample = soundGenerator->generateSample(SAMPLES_PER_SECOND);

                    // Apply soft clipping
                    outputSample = std::tanh(outputSample);

                    floatBuffer[i] = outputSample;
                }

                hr = renderClient->ReleaseBuffer(availableSamples, 0);
                if (FAILED(hr)) {
                    std::cerr << "Failed to release buffer." << std::endl;
                    break;
                }

                // Allow other threads to run
                Sleep(0);
            }
            else {
                std::cout << "No buffer available" << std::endl;
                Sleep(0);
            }

            prevAvailableSamples = availableSamples;
        }
    }

    void shutdown() {
        running = false;
        if (audioClient) {
            audioClient->Stop();
        }
        if (renderClient) {
            renderClient->Release();
            renderClient = nullptr;
        }
        if (audioClient) {
            audioClient->Release();
            audioClient = nullptr;
        }
        if (endpoint) {
            endpoint->Release();
            endpoint = nullptr;
        }
        if (enumerator) {
            enumerator->Release();
            enumerator = nullptr;
        }
        if (mixFormat) {
            CoTaskMemFree(mixFormat);
            mixFormat = nullptr;
        }
        CoUninitialize();
    }

private:
    std::shared_ptr<SoundGenerator> soundGenerator;
    bool running;
    IMMDeviceEnumerator* enumerator;
    IMMDevice* endpoint;
    IAudioClient* audioClient;
    IAudioRenderClient* renderClient;
    WAVEFORMATEX* mixFormat;
    REFERENCE_TIME defaultDevicePeriod;
    REFERENCE_TIME minDevicePeriod;
    UINT32 bufferSampleCount;

    void printMixFormat() {
        std::cout << "Audio Format:" << std::endl;
        std::cout << "  Format Tag: " << mixFormat->wFormatTag << std::endl;
        std::cout << "  Channels: " << mixFormat->nChannels << std::endl;
        std::cout << "  Samples per Second: " << mixFormat->nSamplesPerSec << std::endl;
        std::cout << "  Bits per Sample: " << mixFormat->wBitsPerSample << std::endl;
        std::cout << "  Block Align: " << mixFormat->nBlockAlign << std::endl;
        std::cout << "  Average Bytes per Second: " << mixFormat->nAvgBytesPerSec << std::endl;
    }
};

#include "handlers.cpp"
// Main function
int main()
{
    // Use the chosen factory function to create ActiveTones
    VoiceGeneratorRepository voiceRepo;
    
    // Load presets from presets.cpp
    loadPresets(voiceRepo);

    // Use the first voice generator by default
    auto activeTones = std::make_shared<ActiveTones>(voiceRepo.getVoiceGenerator("Sine Oscillator"));

    auto tremolo = std::make_shared<Tremolo>(activeTones, 5.0f, 0.5f);
    auto interpolatedChorus = std::make_shared<InterpolatedChorus>(tremolo, 0.5f, 0.5f, 0.5f, 0.5f);
    auto final = activeTones;

    // Update the ServerHandler initialization to pass the VoiceGeneratorRepository and ActiveTones
    ServerHandler serverHandler(final, voiceRepo, activeTones);
    serverHandler.initialize();
    
    // Initialize KeyboardHandler
    KeyboardHandler keyboardHandler(activeTones);
    keyboardHandler.start();

    // Initialize MidiHandler
    MidiHandler midiHandler(activeTones);
    if (!midiHandler.initialize()) {
        std::cerr << "Failed to initialize MIDI handler." << std::endl;
    }
    // Initialize the audio engine
    AudioEngine audioEngine(final);
    if (!audioEngine.initialize()) {
        std::cerr << "Failed to initialize audio engine." << std::endl;
        return 1;
    }
    // Start audio processing in a separate thread
    std::thread audioThread(&AudioEngine::processAudio, &audioEngine);

    // Keep the main thread running until termination
    std::cout << "Press Enter to exit..." << std::endl;
    std::cin.get();

    // Shutdown procedures
    keyboardHandler.stop();
    midiHandler.shutdown();
    audioEngine.shutdown();
    serverHandler.shutdown();

    if (audioThread.joinable()) {
        audioThread.join();
    }

    return 0;
}
