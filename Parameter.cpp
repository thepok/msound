#pragma once

#include <string>
#include <functional>
#include <iostream>

class Parameter {
public:
    using Callback = std::function<void(float)>;

    Parameter(const std::string& name, float initialValue, float minValue, float maxValue, float stepSize, const std::string& unit, Callback onChange = nullptr)
        : name(name), currentValue(initialValue), minValue(minValue), maxValue(maxValue), stepSize(stepSize), unit(unit), onChange(onChange) {}

    float getValue() const { return currentValue; }
    void setValue(float value) {
        if (value < minValue || value > maxValue) {
            std::cerr << "Value out of range!" << std::endl;
            return;
        }
        currentValue = value;
        if (onChange) {
            onChange(currentValue);
        }
    }

    void increment() {
        setValue(currentValue + stepSize);
    }

    void decrement() {
        setValue(currentValue - stepSize);
    }

    float getMinValue() const { return minValue; }
    float getMaxValue() const { return maxValue; }
    float getStepSize() const { return stepSize; }
    std::string getUnit() const { return unit; }
    std::string getName() const { return name; }
    
    void setName(const std::string& newName) { name = newName; }
private:
    std::string name;
    float currentValue;
    float minValue;
    float maxValue;
    float stepSize;
    std::string unit;
    Callback onChange;
};