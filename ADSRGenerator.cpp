#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <iostream>
#include <functional>
#include <algorithm>
#include <chrono>
#include <cmath>
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

        return sample * currentAmplitude;
    }

    void noteOn(float velocity) override {
        sourceGenerator->noteOn(1.0f);
        stage = Stage::Attack;
        attackStartAmplitude = currentAmplitude; // Start from current amplitude
        stageStartTime = std::chrono::steady_clock::now();
        active = true;
    }

    void noteOff() override {
        sourceGenerator->noteOff();
        if (stage == Stage::Attack) {
            stage = Stage::Release;
            stageStartTime = std::chrono::steady_clock::now();
            releaseStartAmplitude = currentAmplitude;
        } else {
            stage = Stage::Release;
            stageStartTime = std::chrono::steady_clock::now();
            releaseStartAmplitude = (stage == Stage::Sustain) ? sustainLevel : currentAmplitude;
        }
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
    float releaseStartAmplitude;
    float attackStartAmplitude;
    float decayStartAmplitude;
    std::chrono::steady_clock::time_point stageStartTime;

    bool active;
    bool deactivationRequested;

    void updateEnvelope(float sampleRate) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - stageStartTime).count();

        switch (stage) {
            case Stage::Attack:
                if (attackTime > 0.0f) {
                    currentAmplitude = attackStartAmplitude + (1.0f - attackStartAmplitude) * (elapsed / attackTime);
                    if (currentAmplitude >= 1.0f) {
                        currentAmplitude = 1.0f;
                        stage = Stage::Decay;
                        stageStartTime = now;
                        decayStartAmplitude = currentAmplitude; // Set decay start amplitude
                    }
                } else {
                    currentAmplitude = 1.0f;
                    stage = Stage::Decay;
                    stageStartTime = now;
                    decayStartAmplitude = currentAmplitude; // Set decay start amplitude
                }
                break;
            case Stage::Decay:
                if (decayTime > 0.0f) {
                    currentAmplitude = decayStartAmplitude - (decayStartAmplitude - sustainLevel) * (elapsed / decayTime);
                    if (elapsed >= decayTime) {
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
                    currentAmplitude = releaseStartAmplitude * (1.0f - (elapsed / releaseTime));
                    if (elapsed >= releaseTime) {
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
    }
};
