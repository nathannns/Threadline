#pragma once

#include <JuceHeader.h>

class ChorusModule
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setParameters (float rateHz, float depthPercent, float widthPercent,
                        float toneHz, float mixPercent, bool enabled, int flangerMode);
    void process (juce::AudioBuffer<float>& buffer);

private:
    float readDelay (int channel, float distance) const;

    juce::AudioBuffer<float> delayBuffer;
    juce::SmoothedValue<float> rateValue, depthValue, widthValue, wetMix, flangerBlend, aggressionValue;
    std::vector<float> toneState, warmBodyState, crossLowState, feedbackState, companderEnvelope;
    double sampleRate = 44100.0;
    int writeIndex = 0;
    int validSamples = 0;
    float lfoPhase = 0.0f;
    float secondaryPhase = 0.0f;
    float toneCoefficient = 1.0f;
    float warmBodyCoefficient = 1.0f;
    float crossLowCoefficient = 1.0f;
    float companderAttack = 1.0f;
    float companderRelease = 1.0f;
    float cachedToneHz = -1.0f;
};
