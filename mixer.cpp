#include <vector>
#include <memory>
#include "SoundGenerator.cpp"


class Mixer : public SoundGenerator {
public:
    Mixer(const std::vector<std::shared_ptr<SoundGenerator>>& sources, 
          const std::vector<std::string>& suffixes = std::vector<std::string>())
        : SoundGenerator(), sources(sources) {
        // Create volume parameters for each source
        for (size_t i = 0; i < sources.size(); ++i) {
            std::string suffix = (i < suffixes.size()) ? suffixes[i] : "";
            auto param = std::make_unique<Parameter>(
                "Channel " + std::to_string(i + 1) + " Volume" + suffix,
                0.3f,  // initial value
                0.0f,  // min value
                2.0f,  // max value
                0.01f, // step size
                "",    // unit
                [this, i](float value) { setVolume(i, value); }
            );
            volumeParams.push_back(param.get()); // Store raw pointer before moving
            addParam(std::move(param));
            volumes.push_back(1.0f);
            
            // Add source as child generator
            addChildGenerator(sources[i]);
        }
    }

    float generateSample(float sampleRate) override {
        float mixedSample = 0.0f;
        
        // Mix all sources with their respective volumes
        for (size_t i = 0; i < sources.size(); ++i) {
            mixedSample += sources[i]->generateSample(sampleRate) * volumes[i];
        }
        
        return mixedSample;
    }

    // Get parameter by index
    Parameter* getVolumeParam(size_t index) {
        if (index < volumeParams.size()) {
            return volumeParams[index];
        }
        return nullptr;
    }

private:
    std::vector<std::shared_ptr<SoundGenerator>> sources;
    std::vector<float> volumes;
    std::vector<Parameter*> volumeParams; // Store raw pointers to parameters

    void setVolume(size_t channel, float volume) {
        if (channel < volumes.size()) {
            volumes[channel] = volume;
        }
    }
};
