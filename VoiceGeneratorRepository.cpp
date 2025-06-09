#pragma once

#include <vector>
#include <memory>
#include <string>
#include <functional>
#include "Voices.cpp"

class VoiceGeneratorRepository {
public:
    using VoiceFactory = std::function<std::shared_ptr<SoundGenerator>(float, float)>;

    void addVoiceGenerator(const std::string& name, VoiceFactory factory) {
        voiceGenerators.push_back({name, factory});
    }

    std::vector<std::string> getVoiceGeneratorNames() const {
        std::vector<std::string> names;
        for (const auto& vg : voiceGenerators) {
            names.push_back(vg.name);
        }
        return names;
    }

    VoiceFactory getVoiceGenerator(const std::string& name) const {
        for (const auto& vg : voiceGenerators) {
            if (vg.name == name) {
                return vg.factory;
            }
        }
        throw std::runtime_error("Voice generator not found: " + name);
    }

private:
    struct VoiceGeneratorEntry {
        std::string name;
        VoiceFactory factory;
    };

    std::vector<VoiceGeneratorEntry> voiceGenerators;
};