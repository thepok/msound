#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <iostream>
#include <functional>
#include <cmath>
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
        int activeToneCount = 0;

        for (auto& adsrGenerator : activeTones) {
            float temp_sample = adsrGenerator->generateSample(sampleRate);
            if (temp_sample != 0.0f) {
                sample += temp_sample;
                ++activeToneCount;
            }
        }

        float targetGainFactor = (activeToneCount > 0) ? 1.0f / std::sqrt(activeToneCount) : 1.0f;
        
        // Smooth the gain factor transition
        float smoothingFactor = 0.001f;  // Adjust this value to control smoothing speed
        smoothedGainFactor += smoothingFactor * (targetGainFactor - smoothedGainFactor);

        sample *= smoothedGainFactor;
        sample = std::tanh(sample);
        // std::cout << "sample: " << sample << std::endl;

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

    void setVoiceGenerator(const SoundGeneratorFactory& newVoiceGenerator) {
        std::lock_guard<std::mutex> lock(tonesMutex);
        
        parameterPointers.clear();
        parameters.clear();
        childGenerators.clear();
        // Reinitialize all SoundGenerators with the new factory
        for (int note = 0; note < MIDI_NOTE_COUNT; ++note) {
            float frequency = midiNoteToFrequency(note);
            activeTones[note] = std::shared_ptr<SoundGenerator>(newVoiceGenerator(frequency, 1.0f));
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
