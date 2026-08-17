#pragma once

#include <JuceHeader.h>

// Tweed-era bias-modulation tremolo (Tremolux/Vibrolux-style, matching this
// amp's own lineage — not the later blackface photocell/neon-bulb type,
// which is a different physical mechanism). A single LFO shifts the
// preamp tube's grid bias, and because a triode's transconductance falls
// off increasingly steeply as bias approaches cutoff, an even symmetric
// sine bias swing does NOT produce a symmetric gain swing: the half-cycle
// swinging toward cutoff dips with accelerating speed, while the half
// swinging back toward the tube's normal operating point recovers more
// gently, tapering as it nears the ceiling it can't exceed (gain is always
// <=1 — bias modulation only ever *reduces* gain from the unbiased point,
// never boosts past it). See TremoloModule.cpp for the shaping curve.
class TremoloModule
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setAmount (float amountPercent);
    void process (juce::AudioBuffer<float>& buffer);
    bool isWetTransitionActive() const noexcept
    {
        return amount.isSmoothing() || amount.getCurrentValue() > 0.00001f;
    }

private:
    juce::SmoothedValue<float> amount;
    double sampleRate = 44100.0;
    float phase = 0.0f;
};
