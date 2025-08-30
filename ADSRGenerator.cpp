#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <iostream>
#include <functional>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "SoundGenerator.cpp"

class ADSRGenerator : public SoundGenerator {
public:
    ADSRGenerator(std::shared_ptr<SoundGenerator> source,
                  float attack = 0.1f,
                  float decay = 0.1f,
                  float sustain = 0.7f,
                  float release = 0.3f)
        : sourceGenerator(source),
          attackTime(attack),
          decayTime(decay),
          sustainLevel(sustain),
          releaseTime(release),
          stage(Stage::Idle),
          currentAmplitude(0.0f),
          velocityGain(1.0f),
          active(false),
          deactivationRequested(false) {
        
        // Initialize ADSR parameters with callbacks
        Attack = addParam(std::make_unique<Parameter>("Attack", attack, 0.01f, 10.0f, 0.01f, "s", [this](float newValue) {
            attackTime = newValue;
        }));
        Decay = addParam(std::make_unique<Parameter>("Decay", decay, 0.01f, 10.0f, 0.01f, "s", [this](float newValue) {
            decayTime = newValue;
        }));
        Sustain = addParam(std::make_unique<Parameter>("Sustain", sustain, 0.0f, 1.0f, 0.01f, "", [this](float newValue) {
            sustainLevel = newValue;
        }));
        Release = addParam(std::make_unique<Parameter>("Release", release, 0.01f, 10.0f, 0.01f, "s", [this](float newValue) {
            releaseTime = newValue;
        }));

        // Add child generator
        addChildGenerator(sourceGenerator);
    }

    float generateSample(float sampleRate) override {
        if (stage == Stage::Idle) {
            return 0.0f;
        }

        float sample = sourceGenerator->generateSample(sampleRate);

        // Update envelope on every sample for accurate timing
        updateEnvelope(sampleRate);

        return sample * currentAmplitude * velocityGain;
    }

    void noteOn(float velocity) override {
        velocityGain = std::clamp(velocity, 0.0f, 1.0f);
        sourceGenerator->noteOn(velocityGain);
        stage = Stage::Attack;
        attackStartAmplitude = currentAmplitude; // Start from current amplitude
        samplesSinceStageStart = 0;
        active = true;
    }

    void noteOff() override {
        sourceGenerator->noteOff();
        Stage preReleaseStage = stage;
        stage = Stage::Release;
        samplesSinceStageStart = 0;
        releaseStartAmplitude = (preReleaseStage == Stage::Sustain) ? sustainLevel : currentAmplitude;
    }

    bool isActive() const {
        return active;
    }

    // Expose ADSR parameters
    std::shared_ptr<Parameter> Attack;
    std::shared_ptr<Parameter> Decay;
    std::shared_ptr<Parameter> Sustain;
    std::shared_ptr<Parameter> Release;

private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    std::shared_ptr<SoundGenerator> sourceGenerator;

    float attackTime;
    float decayTime;
    float sustainLevel;
    float releaseTime;

    Stage stage;
    float currentAmplitude;
    float velocityGain;
    float releaseStartAmplitude;
    float attackStartAmplitude;
    float decayStartAmplitude;
    uint64_t samplesSinceStageStart;

    bool active;
    bool deactivationRequested;

    void updateEnvelope(float sampleRate) {
        Stage previousStage = stage;
        float elapsedSeconds = (sampleRate > 0.0f) ? (static_cast<float>(samplesSinceStageStart) / sampleRate) : 0.0f;

        switch (stage) {
            case Stage::Attack:
                if (attackTime > 0.0f) {
                    currentAmplitude = attackStartAmplitude + (1.0f - attackStartAmplitude) * (elapsedSeconds / attackTime);
                    if (currentAmplitude >= 1.0f) {
                        currentAmplitude = 1.0f;
                        stage = Stage::Decay;
                        samplesSinceStageStart = 0;
                        decayStartAmplitude = currentAmplitude; // Set decay start amplitude
                    }
                } else {
                    currentAmplitude = 1.0f;
                    stage = Stage::Decay;
                    samplesSinceStageStart = 0;
                    decayStartAmplitude = currentAmplitude; // Set decay start amplitude
                }
                break;
            case Stage::Decay:
                if (decayTime > 0.0f) {
                    currentAmplitude = decayStartAmplitude - (decayStartAmplitude - sustainLevel) * (elapsedSeconds / decayTime);
                    if (elapsedSeconds >= decayTime) {
                        currentAmplitude = sustainLevel;
                        stage = Stage::Sustain;
                    }
                } else {
                    currentAmplitude = sustainLevel;
                    stage = Stage::Sustain;
                }
                break;
            case Stage::Sustain:
                currentAmplitude = sustainLevel;
                break;
            case Stage::Release:
                if (releaseTime > 0.0f) {
                    currentAmplitude = releaseStartAmplitude * (1.0f - (elapsedSeconds / releaseTime));
                    if (elapsedSeconds >= releaseTime) {
                        currentAmplitude = 0.0f;
                        stage = Stage::Idle;
                        active = false;
                    }
                } else {
                    currentAmplitude = 0.0f;
                    stage = Stage::Idle;
                    active = false;
                }
                break;
            case Stage::Idle:
                currentAmplitude = 0.0f;
                break;
        }

        currentAmplitude = std::clamp(currentAmplitude, 0.0f, 1.0f);

        if (stage == previousStage && stage != Stage::Idle) {
            ++samplesSinceStageStart;
        }
    }
};
