#pragma once

#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include "Parameter.cpp"
#include "math.cpp"

class SoundGenerator {
public:
    virtual ~SoundGenerator() = default;
    virtual float generateSample(float sampleRate) = 0;

    // Add these new virtual methods
    virtual void noteOn(float velocity) {
        for (auto& child : childGenerators) {
            child->noteOn(velocity);
        }
    }
    virtual void noteOff() {
        for (auto& child : childGenerators) {
            child->noteOff();
        }
    }

    const std::vector<Parameter*>& getParameters() {
        rebuildParameterPointers();
        return parameterPointers;
    }

    void addSuffix(const std::string& suffix) {
        for (auto& param : parameters) {
            param->setName(param->getName() + suffix);
        }
    }

protected:
    std::shared_ptr<Parameter> addParam(std::unique_ptr<Parameter> param) {
        parameters.push_back(std::move(param));
        return parameters.back();
    }

    void rebuildParameterPointers() {
        parameterPointers.clear();
        for (auto& param : parameters) {
            parameterPointers.push_back(param.get());
        }
        for (auto& child : childGenerators) {
            for (auto* childParam : child->getParameters()) {
                parameterPointers.push_back(childParam);
            }
        }
    }

    void addChildGenerator(std::shared_ptr<SoundGenerator> child) {
        childGenerators.push_back(std::move(child));
    }

    std::vector<std::shared_ptr<Parameter>> parameters;
    std::vector<Parameter*> parameterPointers;
    std::vector<std::shared_ptr<SoundGenerator>> childGenerators;
};
