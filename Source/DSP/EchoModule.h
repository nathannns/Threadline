#pragma once

#include <JuceHeader.h>

class EchoModule
{
public:
    enum Pattern { straight, bounce, gallop, cluster, wash };

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setParameters (float timeMs, float repeats, float toneHz, float wobble,
                        float drive, float mix, bool enabled, int patternIndex);
    void process (juce::AudioBuffer<float>& buffer);
    bool isWetTransitionActive() const noexcept
    {
        return wetMix.isSmoothing() || wetMix.getCurrentValue() > 0.00001f;
    }

private:
    float readDelay (int channel, float delaySamples) const;
    float processFeedbackTone (int channel, float sample);
    void getPattern (float* ratios, float* gains, int& taps) const;

    juce::AudioBuffer<float> delayBuffer;
    juce::SmoothedValue<float> delaySamples;
    juce::SmoothedValue<float> wetMix;
    juce::SmoothedValue<float> feedbackValue;
    juce::SmoothedValue<float> wobbleValue;
    juce::SmoothedValue<float> driveValue;
    std::vector<float> toneState;
    double sampleRate = 44100.0;
    int writeIndex = 0;
    int validSamples = 0;
    int pattern = straight;
    float lfoPhase = 0.0f;
    float flutterPhase = 0.0f;
    float toneCoefficient = 1.0f;
    float cachedToneHz = -1.0f;
};
