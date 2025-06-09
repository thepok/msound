#pragma once

#include <vector>
#include <cmath>
#include <memory>
#include "SoundGenerator.cpp"
#include "math.cpp"
#include <functional>

// Enum for different waveform types
enum class Waveform {
    Sine,
    Square,
    Triangle,
    Sawtooth
};

// Oscillator class modified to inherit from SoundGenerator and support multiple waveforms
class Oscillator : public SoundGenerator {
public:
    Oscillator(float frequency = 440.0f, float volume = 1.0f, Waveform waveform = Waveform::Sine)
        : frequency(frequency), volume(volume), phase(0.0f), waveform(waveform) {}

    float generateSample(float sampleRate) override {
        float sampleValue = 0.0f;

        switch (waveform) {
            case Waveform::Sine:
                sampleValue = std::sin(phase) * volume;
                break;
            case Waveform::Square:
                sampleValue = (std::sin(phase) >= 0.0f ? 1.0f : -1.0f) * volume;
                break;
            case Waveform::Triangle:
                sampleValue = (2.0f / PI) * std::asin(std::sin(phase)) * volume;
                break;
            case Waveform::Sawtooth:
                sampleValue = (2.0f / PI) * (phase - PI) * volume;
                break;
            default:
                sampleValue = std::sin(phase) * volume;
                break;
        }

        phase += 2.0f * PI * frequency / sampleRate;
        if (phase >= 2.0f * PI)
            phase -= 2.0f * PI;

        return sampleValue;
    }

    void setFrequency(float freq) { frequency = freq; }
    float getFrequency() const { return frequency; }
    void setVolume(float vol) { volume = vol; }
    float getVolume() const { return volume; }
    void resetPhase() { phase = 0.0f; }

    void setWaveform(Waveform wf) { waveform = wf; }
    Waveform getWaveform() const { return waveform; }

private:
    float frequency;
    float volume;
    float phase;
    Waveform waveform;
};

// Tone class adjusted to use oscillators as SoundGenerators
class Tone : public SoundGenerator {
public:
    Tone(float frequency = 440.0f, float volume = 1.0f, int oscillatorsPerTone = 3, float detuneFactor = 0.001f)
        : frequency(frequency), volume(volume), oscillatorsPerTone(oscillatorsPerTone), detuneFactor(detuneFactor) {
        // Add fundamental frequency oscillators
        for (int i = 0; i < oscillatorsPerTone; ++i) {
            float detune = (i - (oscillatorsPerTone - 1) / 2.0f) * detuneFactor;
            auto osc = std::make_shared<Oscillator>(frequency * (1.0f + detune));
            oscillators.push_back(osc);
            addChildGenerator(osc);
        }

        // Initialize parameters
        addParam(std::make_unique<Parameter>("Oscillators", static_cast<float>(oscillatorsPerTone), 1.0f, 10.0f, 1.0f, "",
            [this](float value) { setOscillatorsPerTone(static_cast<int>(value)); }));
        addParam(std::make_unique<Parameter>("Detune Factor", detuneFactor, 0.0f, 0.1f, 0.0001f, "",
            [this](float value) { setDetuneFactor(value); }));
    }

    float generateSample(float sampleRate) override {
        float sample = 0.0f;
        for (auto& osc : oscillators) {
            sample += osc->generateSample(sampleRate);
        }
        return sample * volume / oscillators.size();
    }

    void setFrequency(float freq) {
        frequency = freq;
        updateOscillators();
    }

    void setVolume(float vol) { volume = vol; }

    void setOscillatorsPerTone(int count) {
        oscillatorsPerTone = count;
        updateOscillators();
    }

    void setDetuneFactor(float factor) {
        detuneFactor = factor;
        for (long long unsigned int i = 0; i < oscillators.size(); ++i) {
            float detune = (i - (oscillators.size() - 1) / 2.0f) * detuneFactor;
            oscillators[i]->setFrequency(frequency * (1.0f + detune));
        }
    }

private:
    float frequency;
    float volume;
    int oscillatorsPerTone;
    float detuneFactor;
    std::vector<std::shared_ptr<Oscillator>> oscillators;

    void updateOscillators() {
        oscillators.clear();
        childGenerators.clear();
        for (int i = 0; i < oscillatorsPerTone; ++i) {
            float detune = (i - (oscillatorsPerTone - 1) / 2.0f) * detuneFactor;
            auto osc = std::make_shared<Oscillator>(frequency * (1.0f + detune));
            oscillators.push_back(osc);
            addChildGenerator(osc);
        }
    }
};

class HarmonicTone : public SoundGenerator {
public:
    HarmonicTone(float frequency = 440.0f, float volume = 1.0f)
        : frequency(frequency), volume(volume), detuneFactor(0.0f) {
        initializeTones();

        addParam(std::make_unique<Parameter>("Detune", detuneFactor, -0.1f, 0.1f, 0.001f, "",
            [this](float value) { setDetuneFactor(value); }));
    }

    float generateSample(float sampleRate) override {
        float sample = 0.0f;
        for (auto& tone : tones) {
            sample += tone->generateSample(sampleRate);
        }
        return std::tanh(sample/std::sqrt(tones.size()));
    }

private:
    float frequency;
    float volume;
    float detuneFactor;
    std::vector<std::shared_ptr<Tone>> tones;

    void initializeTones() {
        // Add the main tone
        auto mainTone = std::make_shared<Tone>(frequency, volume);
        tones.push_back(mainTone);
        addChildGenerator(mainTone);

        // Add harmonic tones
        addHarmonicTone(1.5f, 0.5f);
        addHarmonicTone(2.0f, 0.4f);
        addHarmonicTone(2.5f, 0.3f);
        addHarmonicTone(3.0f, 0.2f);
        addHarmonicTone(3.5f, 0.1f);
    }

    void addHarmonicTone(float freqMultiplier, float volMultiplier) {
        auto tone = std::make_shared<Tone>(frequency * freqMultiplier, volume * volMultiplier);
        tones.push_back(tone);
        //addChildGenerator(tone);
    }

    void setDetuneFactor(float factor) {
        detuneFactor = factor;
        updateTonesDetune();
    }

    void updateTonesDetune() {
        for (size_t i = 1; i < tones.size(); ++i) {
            tones[i]->setDetuneFactor(detuneFactor);
        }
    }
};

class FMVoice : public SoundGenerator {
public:
    FMVoice(float carrierFreq = 440.0f, float modulatorFreq = 220.0f, float modulationIndex = 1.0f, float selfModulationIndex = 0.7f)
        : carrierFrequency(carrierFreq), modulatorFrequency(modulatorFreq), modulationIndex(modulationIndex),
          selfModulationIndex(selfModulationIndex), carrierOscillator(carrierFreq), modulatorOscillator(modulatorFreq) {
        
        addParam(std::make_unique<Parameter>("Modulator Frequency Ratio", modulatorFrequency / carrierFrequency, 0.1f, 10.0f, 0.01f, "", [this](float value) {
            this->modulatorFrequency = value * this->carrierFrequency;
            this->modulatorOscillator.setFrequency(this->modulatorFrequency);
        }));
        addParam(std::make_unique<Parameter>("Modulation Index", modulationIndex, 0.0f, 10.0f, 0.01f, "", [this](float value) {
            this->modulationIndex = value;
        }));
        addParam(std::make_unique<Parameter>("Self Modulation Index", selfModulationIndex, 0.0f, 10.0f, 0.01f, "", [this](float value) {
            this->selfModulationIndex = value;
        }));


    }

    float generateSample(float sampleRate) override {
        float modulatorSample = modulatorOscillator.generateSample(sampleRate);
        float selfModulatedFrequency = modulatorFrequency + selfModulationIndex * modulatorSample * modulatorFrequency;
        modulatorOscillator.setFrequency(selfModulatedFrequency);

        float modulatedFrequency = carrierFrequency + modulationIndex * modulatorSample * carrierFrequency;
        carrierOscillator.setFrequency(modulatedFrequency);
        return carrierOscillator.generateSample(sampleRate);
    }

private:
    float carrierFrequency;
    float modulatorFrequency;
    float modulationIndex;
    float selfModulationIndex;
    Oscillator carrierOscillator;
    Oscillator modulatorOscillator;
};
