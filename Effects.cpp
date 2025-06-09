#pragma once

#include <cmath>
#include <vector>
#include <random>
#include <algorithm>
#include "SoundGenerator.cpp"
#include "Parameter.cpp"
#include <memory>

class HighPassFilter : public SoundGenerator {
public:
    HighPassFilter(std::shared_ptr<SoundGenerator> source, float cutoffFrequency, float sampleRate)
        : SoundGenerator(), sourceGenerator(source), sampleRate(sampleRate) {
        addParam(std::make_unique<Parameter>("Highpass Cutoff", cutoffFrequency, 20.0f, 20000.0f, 1.0f, "Hz",
            [this](float value) { setCutoffFrequency(value); }));
        
        setCutoffFrequency(cutoffFrequency);
    }

    float generateSample(float sampleRate) override {
        float inputSample = sourceGenerator->generateSample(sampleRate);
        float outputSample = a0 * inputSample + a1 * x1 + a2 * x2 - b1 * y1 - b2 * y2;

        // Update delay buffers
        x2 = x1;
        x1 = inputSample;
        y2 = y1;
        y1 = outputSample;

        return outputSample;
    }

    void setCutoffFrequency(float frequency) {
        cutoffFrequency = frequency;
        calculateCoefficients();
    }

private:
    std::shared_ptr<SoundGenerator> sourceGenerator;
    float cutoffFrequency;
    float sampleRate;

    // Filter coefficients
    float a0, a1, a2, b1, b2;

    // Delay buffers
    float x1, x2, y1, y2;

    void calculateCoefficients() {
        float omega = 2.0f * PI * cutoffFrequency / sampleRate;
        float alpha = std::sin(omega) / (2.0f * std::sqrt(2.0f)); // Q = sqrt(2)/2 for Butterworth filter

        float cos_omega = std::cos(omega);
        float a0_inv = 1.0f / (1.0f + alpha);

        a0 = (1.0f + cos_omega) * 0.5f * a0_inv;
        a1 = -(1.0f + cos_omega) * a0_inv;
        a2 = a0;
        b1 = -2.0f * cos_omega * a0_inv;
        b2 = (1.0f - alpha) * a0_inv;
    }

    void reset() {
        x1 = x2 = y1 = y2 = 0.0f;
    }
};

class LowPassFilter : public SoundGenerator {
public:
    LowPassFilter(std::shared_ptr<SoundGenerator> source, float cutoffFrequency, float sampleRate)
        : SoundGenerator(), sourceGenerator(source), sampleRate(sampleRate) {
        addParam(std::make_unique<Parameter>("Lowpass Cutoff", cutoffFrequency, 20.0f, 20000.0f, 1.0f, "Hz",
            [this](float value) { setCutoffFrequency(value); }));
        
        setCutoffFrequency(cutoffFrequency);
    }

    float generateSample(float sampleRate) override {
        float inputSample = sourceGenerator->generateSample(sampleRate);
        float outputSample = a0 * inputSample + a1 * x1 + a2 * x2 - b1 * y1 - b2 * y2;

        // Update delay buffers
        x2 = x1;
        x1 = inputSample;
        y2 = y1;
        y1 = outputSample;

        return outputSample;
    }

    void setCutoffFrequency(float frequency) {
        cutoffFrequency = frequency;
        calculateCoefficients();
    }

private:
    std::shared_ptr<SoundGenerator> sourceGenerator;
    float cutoffFrequency;
    float sampleRate;

    // Filter coefficients
    float a0, a1, a2, b1, b2;

    // Delay buffers
    float x1, x2, y1, y2;

    void calculateCoefficients() {
        float omega = 2.0f * PI * cutoffFrequency / sampleRate;
        float alpha = std::sin(omega) / (2.0f * std::sqrt(2.0f)); // Q = sqrt(2)/2 for Butterworth filter

        float cos_omega = std::cos(omega);
        float a0_inv = 1.0f / (1.0f + alpha);

        a0 = ((1.0f - cos_omega) * 0.5f) * a0_inv;
        a1 = (1.0f - cos_omega) * a0_inv;
        a2 = a0;
        b1 = -2.0f * cos_omega * a0_inv;
        b2 = (1.0f - alpha) * a0_inv;
    }

    void reset() {
        x1 = x2 = y1 = y2 = 0.0f;
    }
};

class Delay : public SoundGenerator {
public:
    Delay(std::shared_ptr<SoundGenerator> source, int delaySamples, float feedback, float mix, float sampleRate)
        : SoundGenerator(), sourceGenerator(source), sampleRate(sampleRate), delayBuffer(sampleRate * 2, 0.0f), writeIndex(0) {
        addParam(std::make_unique<Parameter>("Delay Samples", static_cast<float>(delaySamples), 1.0f, sampleRate * 2, 1.0f, "samples",
            [this](float value) { setDelaySamples(static_cast<int>(value)); }));
        addParam(std::make_unique<Parameter>("Feedback", feedback, 0.0f, 0.99f, 0.01f, "",
            [this](float value) { this->feedback = value; }));
        addParam(std::make_unique<Parameter>("Mix", mix, 0.0f, 1.0f, 0.01f, "",
            [this](float value) { this->mix = value; }));

        setDelaySamples(delaySamples);
        this->feedback = feedback;
        this->mix = mix;
    }

    float generateSample(float sampleRate) override {
        float inputSample = sourceGenerator->generateSample(sampleRate);
        
        // Read from delay buffer
        float delaySample = delayBuffer[readIndex];
        
        // Write to delay buffer
        delayBuffer[writeIndex] = inputSample + delaySample * feedback;
        
        // Update indices
        writeIndex = (writeIndex + 1) % delayBuffer.size();
        readIndex = (readIndex + 1) % delayBuffer.size();
        
        // Mix dry and wet signals
        return inputSample * (1.0f - mix) + delaySample * mix;
    }

    void setDelaySamples(int newDelaySamples) {
        delaySamples = newDelaySamples;
        readIndex = (writeIndex - delaySamples + delayBuffer.size()) % delayBuffer.size();
    }

private:
    std::shared_ptr<SoundGenerator> sourceGenerator;
    float sampleRate;
    std::vector<float> delayBuffer;
    int writeIndex;
    int readIndex;
    
    int delaySamples;
    float feedback;
    float mix;
};

class InterpolatedDelay : public SoundGenerator {
public:
    InterpolatedDelay(std::shared_ptr<SoundGenerator> source, float delaySamples, float feedback, float mix, float sampleRate)
        : SoundGenerator(),
          sourceGenerator(source),
          sampleRate(sampleRate),
          delayBuffer(static_cast<int>(sampleRate * 2), 0.0f),
          writeIndex(0),
          feedback(feedback),
          mix(mix),
          currentDelaySamples(delaySamples) {
        addParam(std::make_unique<Parameter>("Delay Samples", delaySamples, 0.0f, sampleRate * 2, 0.1f, "samples",
            [this](float value) { setDelaySamples(value); }));
        addParam(std::make_unique<Parameter>("Feedback", feedback, 0.0f, 0.99f, 0.01f, "",
            [this](float value) { this->feedback = value; }));
        addParam(std::make_unique<Parameter>("Mix", mix, 0.0f, 1.0f, 0.01f, "",
            [this](float value) { this->mix = value; }));

        addChildGenerator(sourceGenerator);
    }

    // Add move constructor
    InterpolatedDelay(InterpolatedDelay&& other) noexcept
        : SoundGenerator(std::move(other)),
          sourceGenerator(std::move(other.sourceGenerator)),
          sampleRate(other.sampleRate),
          delayBuffer(std::move(other.delayBuffer)),
          writeIndex(other.writeIndex),
          feedback(other.feedback),
          mix(other.mix),
          currentDelaySamples(other.currentDelaySamples) {
    }

    float generateSample(float sampleRate) override {
        float inputSample = sourceGenerator->generateSample(sampleRate);

        // Calculate read position with fractional delay
        float readPos = static_cast<float>(writeIndex) - currentDelaySamples;
        while (readPos < 0.0f) readPos += delayBuffer.size();
        int index1 = static_cast<int>(readPos) % delayBuffer.size();
        int index2 = (index1 + 1) % delayBuffer.size();
        float frac = readPos - std::floor(readPos);

        // Linear interpolation
        float delaySample = (1.0f - frac) * delayBuffer[index1] + frac * delayBuffer[index2];

        // Write to buffer with feedback
        delayBuffer[writeIndex] = inputSample + delaySample * feedback;

        // Update write index
        writeIndex = (writeIndex + 1) % delayBuffer.size();

        // Mix dry and wet signals
        return inputSample * (1.0f - mix) + delaySample * mix;
    }

    void setDelaySamples(float newDelaySamples) {
        currentDelaySamples = newDelaySamples;
    }

private:
    std::shared_ptr<SoundGenerator> sourceGenerator;
    float sampleRate;
    std::vector<float> delayBuffer;
    int writeIndex;
    float feedback;
    float mix;
    float currentDelaySamples;
};

class InterpolatedChorus : public SoundGenerator {
public:
    InterpolatedChorus(std::shared_ptr<SoundGenerator> source, float rate, float depth, float mix, float sampleRate)
        : SoundGenerator(), sourceGenerator(source), sampleRate(sampleRate), phase(0.0f), rate(rate), depth(depth), mix(mix) {

        // Initialize parameters
        addParam(std::make_unique<Parameter>("Rate", rate, 0.01f, 2.0f, 0.01f, "Hz",
            [this](float value) { this->rate = value; }));

        addParam(std::make_unique<Parameter>("Depth", depth, 0.0f, 200.0f, 0.1f, "ms",
            [this](float value) { this->depth = value; }));

        addParam(std::make_unique<Parameter>("Mix", mix, 0.0f, 1.0f, 0.01f, "",
            [this](float value) { this->mix = value; }));

        // Initialize delay lines with interpolated delay
        for (int i = 0; i < numVoices; ++i) {
            delayLines.emplace_back(source, initialDelaySamples(), feedback, mix, sampleRate);
        }
    }

    float generateSample(float sampleRate) override {
        float inputSample = sourceGenerator->generateSample(sampleRate);
        float outputSample = 0.0f;

        // Update phase
        phase += rate / sampleRate;
        if (phase >= 1.0f) phase -= 1.0f;

        // Process each voice
        for (int i = 0; i < numVoices; ++i) {
            float voicePhase = phase + static_cast<float>(i) / numVoices;
            if (voicePhase >= 1.0f) voicePhase -= 1.0f;

            // Calculate delay in milliseconds for modulation
            float delayMs = depth * (0.5f + 0.5f * std::sin(2.0f * PI * voicePhase));

            // Convert milliseconds to samples
            float delaySamples = delayMs * sampleRate / 1000.0f;

            // Apply a minimum delay to prevent zero delay
            delaySamples = std::max(delaySamples, minimumDelaySamples);

            // Smoothly update delay time using interpolated delay
            delayLines[i].setDelaySamples(delaySamples);

            // Generate sample from delay line
            outputSample += delayLines[i].generateSample(sampleRate);
        }

        outputSample /= numVoices;

        // Mix dry and wet signals
        return inputSample * (1.0f - mix) + outputSample * mix;
    }

private:
    std::shared_ptr<SoundGenerator> sourceGenerator;
    float sampleRate;
    float phase;
    float rate;
    float depth;
    float mix;

    static const int numVoices = 3;
    static constexpr float minimumDelayMs = 1.0f; // 1 ms minimum delay to prevent artifacts
    float minimumDelaySamples = minimumDelayMs * sampleRate / 1000.0f;
    float feedback = 0.0f; // Adjust as needed

    std::vector<InterpolatedDelay> delayLines;

    float initialDelaySamples() const {
        return depth * 0.5f * sampleRate / 1000.0f; // Initial delay based on depth
    }
};

class Reverb : public SoundGenerator {
public:
    Reverb(std::shared_ptr<SoundGenerator> source, float roomSize, float damping, float wetMix, float dryMix, float sampleRate)
        : sourceGenerator(source), sampleRate(sampleRate), roomSize(roomSize), damping(damping), wetMix(wetMix), dryMix(dryMix) {
        
        // Initialize parameters
        addParam(std::make_unique<Parameter>("Room Size", roomSize, 0.1f, 1.0f, 0.01f, "",
            [this](float value) { setRoomSize(value); }));
        addParam(std::make_unique<Parameter>("Damping", damping, 0.0f, 1.0f, 0.01f, "",
            [this](float value) { setDamping(value); }));
        addParam(std::make_unique<Parameter>("Wet Mix", wetMix, 0.0f, 1.0f, 0.01f, "",
            [this](float value) { setWetMix(value); }));
        addParam(std::make_unique<Parameter>("Dry Mix", dryMix, 0.0f, 1.0f, 0.01f, "",
            [this](float value) { setDryMix(value); }));

        // Initialize comb filters with different delay times
        combFilters.emplace_back(createCombFilter(sampleRate * 0.0297f));
        combFilters.emplace_back(createCombFilter(sampleRate * 0.0371f));
        combFilters.emplace_back(createCombFilter(sampleRate * 0.0411f));
        combFilters.emplace_back(createCombFilter(sampleRate * 0.0437f));

        // Initialize all-pass filters
        allPassFilters.emplace_back(createAllPassFilter(sampleRate * 0.005f));
        allPassFilters.emplace_back(createAllPassFilter(sampleRate * 0.0017f));

        // Add source generator as a child
        addChildGenerator(sourceGenerator);
    }

    float generateSample(float sampleRate) override {
        float inputSample = sourceGenerator->generateSample(sampleRate);
        float processedSample = 0.0f;

        // Process through each comb filter
        for (auto& comb : combFilters) {
            processedSample += comb.process(inputSample);
        }
        processedSample /= combFilters.size();

        // Process through all-pass filters
        for (auto& allPass : allPassFilters) {
            processedSample = allPass.process(processedSample);
        }

        // Mix dry and wet signals
        return processedSample * wetMix + inputSample * dryMix;
    }

private:
    std::shared_ptr<SoundGenerator> sourceGenerator;
    float sampleRate;
    float roomSize;
    float damping;
    float wetMix;
    float dryMix;

    // Simple Comb Filter implementation
    class CombFilter {
    public:
        CombFilter(int delaySamples, float dampingFactor)
            : buffer(delaySamples, 0.0f), delaySamples(delaySamples), damping(dampingFactor), bufferIndex(0), feedback(0.7f) {}

        float process(float input) {
            float output = buffer[bufferIndex];
            buffer[bufferIndex] = input + output * feedback;
            bufferIndex = (bufferIndex + 1) % delaySamples;
            return output;
        }

        void setDamping(float dampingFactor) {
            damping = dampingFactor;
            feedback = 0.7f * (1.0f - damping);
        }

    private:
        std::vector<float> buffer;
        int delaySamples;
        float damping;
        int bufferIndex;
        float feedback;
    };

    // Simple All-Pass Filter implementation
    class AllPassFilter {
    public:
        AllPassFilter(int delaySamples, float feedbackFactor)
            : buffer(delaySamples, 0.0f), delaySamples(delaySamples), feedback(feedbackFactor), bufferIndex(0) {}

        float process(float input) {
            float buffered = buffer[bufferIndex];
            float output = -input + buffered;
            buffer[bufferIndex] = input + buffered * feedback;
            bufferIndex = (bufferIndex + 1) % delaySamples;
            return output;
        }

    private:
        std::vector<float> buffer;
        int delaySamples;
        float feedback;
        int bufferIndex;
    };

    std::vector<CombFilter> combFilters;
    std::vector<AllPassFilter> allPassFilters;

    CombFilter createCombFilter(float delayTime) {
        int delaySamples = static_cast<int>(delayTime);
        return CombFilter(delaySamples, damping);
    }

    AllPassFilter createAllPassFilter(float delayTime) {
        int delaySamples = static_cast<int>(delayTime);
        return AllPassFilter(delaySamples, 0.5f);
    }

    void setRoomSize(float size) {
        roomSize = size;
        // Adjust comb filter feedback based on room size
        for (auto& comb : combFilters) {
            comb.setDamping(1.0f - roomSize);
        }
    }

    void setDamping(float damp) {
        damping = damp;
        // Adjust comb filter damping
        for (auto& comb : combFilters) {
            comb.setDamping(damping);
        }
    }

    void setWetMix(float wet) {
        wetMix = wet;
    }

    void setDryMix(float dry) {
        dryMix = dry;
    }
};

class Tremolo : public SoundGenerator {
public:
    Tremolo(std::shared_ptr<SoundGenerator> source, float rate, float depth)
        : sourceGenerator(source), phase(0.0f), lastSample(0.0f), currentAmplitude(1.0f) {
        addParam(std::make_unique<Parameter>("Rate", rate, 0.1f, 20.0f, 0.1f, "Hz",
            [this](float value) { this->rate = value; }));
        addParam(std::make_unique<Parameter>("Depth", depth, 0.0f, 1.0f, 0.01f, "",
            [this](float value) { this->depth = value; }));

        this->rate = rate;
        this->depth = depth;

        // Add source generator as a child
        addChildGenerator(sourceGenerator);
    }

    float generateSample(float sampleRate) override {
        float sample = sourceGenerator->generateSample(sampleRate);

        // Update phase on every sample
        updatePhase(sampleRate);

        // Check for zero crossing in both directions
        if ((lastSample <= 0 && sample > 0) || (lastSample >= 0 && sample < 0)) 
        {
            updateAmplitude();
        }

        lastSample = sample;
        return sample * currentAmplitude;
    }

private:
    std::shared_ptr<SoundGenerator> sourceGenerator;
    float rate;
    float depth;
    float phase;
    float lastSample;
    float currentAmplitude;

    void updatePhase(float sampleRate) {
        phase += rate / sampleRate;
        if (phase >= 1.0f) phase -= 1.0f;
    }

    void updateAmplitude() {
        // Calculate new amplitude
        float modulation = 0.5f * (1.0f + std::sin(2.0f * PI * phase));
        currentAmplitude = 1.0f - depth * modulation;
    }
};