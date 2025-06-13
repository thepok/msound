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

    voiceRepo.addVoiceGenerator("FM Voice", [](float frequency, float volume) {
        return std::make_shared<ADSRGenerator>(std::make_shared<FMVoice>(frequency, frequency / 2.111f, 0.75f));
    });

    voiceRepo.addVoiceGenerator("Bell", [](float frequency, float volume) {
        auto fmVoice = std::make_shared<FMVoice>(frequency, frequency * 1.22f, 0.82f, 0.3f);
        auto tremolo = std::make_shared<Tremolo>(fmVoice, 1.7f, 0.13f);
        return std::make_shared<ADSRGenerator>(
            tremolo,
            0.01f,  // Attack: Very short for a bell-like sound
            2.0f,   // Decay: Quick decay
            0.0f,   // Sustain: Lower sustain level for bell-like sound
            2.0f    // Release: Longer release for bell-like decay
        );
    });

    voiceRepo.addVoiceGenerator("Harmonic Tone", [](float frequency, float volume) {
        return std::make_shared<ADSRGenerator>(std::make_shared<HarmonicTone>(frequency, volume));
    });

    voiceRepo.addVoiceGenerator("Sine Oscillator", [](float frequency, float volume) {
        auto adsr = std::make_shared<ADSRGenerator>(
            std::make_shared<Oscillator>(frequency, volume),
            0.05f,  // Attack
            0.1f,   // Decay
            0.7f,   // Sustain
            0.3f    // Release
        );
        return adsr;
    });

    voiceRepo.addVoiceGenerator("Saw Oscillator", [](float frequency, float volume) {
        auto oscillator = std::make_shared<Oscillator>(frequency, volume, Waveform::Sawtooth);
        auto tremolo = std::make_shared<Tremolo>(oscillator, 5.0f, 0.3f);
        auto adsr = std::make_shared<ADSRGenerator>(
            tremolo,
            0.05f,  // Attack
            0.1f,   // Decay
            0.7f,   // Sustain
            0.3f    // Release
        );
        auto delay = std::make_shared<InterpolatedDelay>(adsr, 0.3f * 44100, 0.5f, 0.3f, 44100);
        return delay;
    });

    voiceRepo.addVoiceGenerator("Bass", [](float frequency, float volume) {
        auto fmVoice = std::make_shared<FMVoice>(
            frequency,
            frequency * 0.36f,  // Modulator Frequency Ratio: 0.36
            0.78f,              // Modulation Index: 0.78
            0.7f                // Self Modulation Index: 0.7
        );
        return std::make_shared<ADSRGenerator>(
            fmVoice,
            0.01f,  // Attack: 0.01s
            0.4f,   // Decay: 0.4s
            0.0f,   // Sustain: 0
            0.39f   // Release: 0.39s
        );
    });
    voiceRepo.addVoiceGenerator("Trio", [](float frequency, float volume) {
        const std::string mainSuffix = "(main)";
        const std::string harmonicSuffix = "(harmonic)";
        const std::string resonanceSuffix = "(resonance)";

        // Main voice - fundamental tone with bright attack
        auto fm0 = std::make_shared<FMVoice>(
            frequency,
            frequency * 2.0f,    // Increased modulator frequency for brighter attack
            0.3f,               // More pronounced modulation
            0.1f                // Slight self modulation for complexity
        );
        auto adsr0 = std::make_shared<ADSRGenerator>(
            fm0,
            0.001f,  // Nearly instantaneous attack
            0.8f,    // Faster initial decay
            0.2f,    // Slight sustain for longer notes
            0.6f     // Natural release
        );
        fm0->addSuffix(mainSuffix);
        adsr0->addSuffix(mainSuffix);

        // String harmonics simulation
        auto fm1 = std::make_shared<HarmonicTone>(frequency * 1.001f, volume); // Slight detuning
        auto adsr1 = std::make_shared<ADSRGenerator>(
            fm1,
            0.001f,  // Immediate attack
            1.2f,    // Longer decay for harmonics
            0.1f,    // Very slight sustain
            0.8f     // Longer release for natural decay
        );
        fm1->addSuffix(harmonicSuffix);
        adsr1->addSuffix(harmonicSuffix);

        // Sympathetic string resonance
        auto fm2 = std::make_shared<FMVoice>(
            frequency * 0.5f,    // Lower octave for body resonance
            frequency * 0.499f,  // Slight detuning for movement
            0.2f,               // Moderate modulation
            0.15f               // Increased self-modulation for complexity
        );
        auto adsr2 = std::make_shared<ADSRGenerator>(
            fm2,
            0.002f,  // Slightly delayed attack
            2.0f,    // Long decay for resonance
            0.05f,   // Minimal sustain
            1.2f     // Long release for natural resonance
        );
        fm2->addSuffix(resonanceSuffix);
        adsr2->addSuffix(resonanceSuffix);

        std::vector<std::shared_ptr<SoundGenerator>> sources = {adsr0, adsr1, adsr2};
        
        // Create mixer with adjusted volumes
        std::vector<std::string> suffixes = {mainSuffix, harmonicSuffix, resonanceSuffix};
        auto mixer = std::make_shared<Mixer>(sources, suffixes);
        mixer->getVolumeParam(0)->setValue(0.6f);    // Strong initial strike
        mixer->getVolumeParam(1)->setValue(0.25f);   // More pronounced harmonics
        mixer->getVolumeParam(2)->setValue(0.15f);   // Subtle but present resonance
        return mixer;
    });

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
