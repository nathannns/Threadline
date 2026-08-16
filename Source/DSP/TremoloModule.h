#pragma once

#include <JuceHeader.h>

class TremoloModule
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setAmount (float amountPercent);
    void process (juce::AudioBuffer<float>& buffer);

private:
    juce::SmoothedValue<float> amount;
    double sampleRate = 44100.0;
    float phase = 0.0f;
};
