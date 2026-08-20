#pragma once

#include <JuceHeader.h>
#include "Antialiasing.h"

// Ported from Rockalizer's ChorusModule -- a Roland Dimension D/SDD-320
// (Boss CE-1-adjacent BBD ensemble) inspired chorus with a one-button
// Flanger blend, genuinely different DSP from Threadline's own "July"
// pedal (ChorusModule.h, modeled on the Walrus Julia's Dry-Chorus-Vibrato
// control surface instead). Renamed from ChorusModule to avoid colliding
// with that existing class.
//
// Section-name mapping: this is Threadline's "Ensemble" pedal, the SAME
// effect as Rockalizer's "chorus" (Rockalizer's ChorusModule.h) -- ported
// byte-for-byte, only the class name differs.
class DimensionChorusModule
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setParameters (float rateHz, float depthPercent, float widthPercent,
                        float toneHz, float mixPercent, bool enabled, int flangerMode);
    void process (juce::AudioBuffer<float>& buffer);
    bool isWetTransitionActive() const noexcept
    {
        return wetMix.isSmoothing() || wetMix.getCurrentValue() > 0.00001f;
    }

private:
    float readDelay (int channel, float distance) const;

    juce::AudioBuffer<float> delayBuffer;
    juce::SmoothedValue<float> rateValue, depthValue, widthValue, wetMix, flangerBlend, aggressionValue;
    std::vector<float> toneState, warmBodyState, crossLowState, feedbackState, companderEnvelope;
    // Antialiased (ADAA, see Antialiasing.h) versions of the two feedback-
    // loop nonlinearities: the BBD input saturation (always active
    // whenever Chorus is wet, every sample) and the Chorus-mode rounding
    // stage, whose output reaches the next sample's feedback via
    // feedbackState below.
    AdaaTanh bbdSaturation[2];
    AdaaTanh chorusRounding[2];
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
