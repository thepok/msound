#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <iostream>
#include <functional>
#include <cmath>
#include <random>
#include "SoundGenerator.cpp"

// Total number of MIDI notes
constexpr int MIDI_NOTE_COUNT = 128;

// Predefine maximum channels if needed
// constexpr int MAX_CHANNELS = 16;

class ActiveTones : public SoundGenerator {
public:
    using SoundGeneratorFactory = std::function<std::shared_ptr<SoundGenerator>(float frequency, float volume)>;

    ActiveTones(SoundGeneratorFactory factory) : smoothedGainFactor(1.0f) {
        setVoiceGenerator(factory);
    }

    float generateSample(float sampleRate) override {
        std::lock_guard<std::mutex> lock(tonesMutex);
        float sample = 0.0f;
        int loudToneCount = 0;

        for (auto& adsrGenerator : activeTones) {
            float temp_sample = adsrGenerator->generateSample(sampleRate);
            sample += temp_sample;
            if (std::fabs(temp_sample) > 1e-4f) { // gate out very quiet voices (~-80 dB)
                ++loudToneCount;
            }
        }

        // 1/sqrt(N_loud) normalization
        float targetGainFactor = 1.0f;
        if (loudToneCount > 0) {
            targetGainFactor = 1.0f / std::sqrt(static_cast<float>(loudToneCount));
        }

        // Time-constant smoothing (~10 ms), sample-rate aware
        const float tauSeconds = 0.010f;
        float alpha = 0.0f;
        if (sampleRate > 0.0f) {
            alpha = std::exp(-1.0f / (tauSeconds * sampleRate));
        }
        smoothedGainFactor = alpha * smoothedGainFactor + (1.0f - alpha) * targetGainFactor;

        sample *= smoothedGainFactor;
        return sample;
    }

    void noteOn(int midiNote, int channel, float frequency, float volume) {
        if (midiNote < 0 || midiNote >= MIDI_NOTE_COUNT) {
            std::cerr << "Invalid MIDI Note: " << midiNote << std::endl;
            return;
        }

        std::lock_guard<std::mutex> lock(tonesMutex);
        auto& adsrGenerator = activeTones[midiNote];
        adsrGenerator->noteOn(volume);
        std::cout << "Activated ADSRGenerator for MIDI Note " << midiNote << std::endl;
    }

    void noteOff(int midiNote, int channel) {
        if (midiNote < 0 || midiNote >= MIDI_NOTE_COUNT) {
            std::cerr << "Invalid MIDI Note: " << midiNote << std::endl;
            return;
        }

        std::lock_guard<std::mutex> lock(tonesMutex);
        auto& adsrGenerator = activeTones[midiNote];
        adsrGenerator->noteOff();
        std::cout << "Deactivated ADSRGenerator for MIDI Note " << midiNote << std::endl;
    }

    // Override base class virtual methods to avoid hiding warnings
    void noteOn(float velocity) override {
        // Default implementation - could be used for all notes or ignored
        // This overrides the base class virtual method
    }

    void noteOff() override {
        // Default implementation - could be used to turn off all notes or ignored
        // This overrides the base class virtual method
    }

    void setVoiceGenerator(const SoundGeneratorFactory& newVoiceGenerator) {
        std::lock_guard<std::mutex> lock(tonesMutex);
        
        parameterPointers.clear();
        parameters.clear();
        childGenerators.clear();
        // Reinitialize all SoundGenerators with the new factory
        for (int note = 0; note < MIDI_NOTE_COUNT; ++note) {
            float frequency = midiNoteToFrequency(note);
            
            // Add slight random frequency offset to reduce phase coherence and beating
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_real_distribution<float> detune(-0.001f, 0.001f);
            float randomDetune = detune(gen);
            float detuned_frequency = frequency * (1.0f + randomDetune);
            
            activeTones[note] = std::shared_ptr<SoundGenerator>(newVoiceGenerator(detuned_frequency, 1.0f));
        }
        
        // Group parameters by name
        std::unordered_map<std::string, std::vector<Parameter*>> paramGroups;
        for (auto& adsrGenerator : activeTones) {
            for (auto* param : adsrGenerator->getParameters()) {
                paramGroups[param->getName()].push_back(param);
            }
        }

        // Generate a Param for each group
        for (const auto& [paramName, params] : paramGroups) {
            if (!params.empty()) {
                auto& firstParam = *params[0];
                addParam(std::make_unique<Parameter>(
                    paramName,
                    firstParam.getValue(),
                    firstParam.getMinValue(),
                    firstParam.getMaxValue(),
                    firstParam.getStepSize(),
                    firstParam.getUnit(),
                    [this, paramName](float value) {
                        updateAllNotesParameter(paramName, value);
                    }
                ));
            }
        }

        // Debug: Print all parameters
        std::cout << "ActiveTones parameters after initialization:" << std::endl;
        for (const auto* param : getParameters()) {
            std::cout << "  " << param->getName() << " = " << param->getValue() << std::endl;
        }
    }
private:
    std::array<std::shared_ptr<SoundGenerator>, MIDI_NOTE_COUNT> activeTones;
    std::mutex tonesMutex;
    float smoothedGainFactor{1.0f};

    float midiNoteToFrequency(int midiNote) const {
        // Convert MIDI note number to frequency
        return 440.0f * std::pow(2.0f, (midiNote - 69) / 12.0f);
    }


    void updateAllNotesParameter(const std::string& paramName, float newValue) {
        // Update all notes, regardless of their active state

        //TODO can crash if note is disposed after loop starts
        for (auto& adsrGenerator : activeTones) {
            for (auto* param : adsrGenerator->getParameters()) {
                if (param->getName() == paramName) {
                    param->setValue(newValue);
                    break;
                }
            }
        }
    }
};
