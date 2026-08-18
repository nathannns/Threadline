#pragma once

#include <JuceHeader.h>

// Two selectable voices, sharing one LFO (Rate/Amount, same phase) so
// switching between them keeps the same speed and feel:
//
// Bias (default): tweed-era bias-modulation tremolo (Tremolux/Vibrolux-
// style, matching this amp's own lineage). A single LFO shifts the preamp
// tube's grid bias, and because a triode's transconductance falls off
// increasingly steeply as bias approaches cutoff, an even symmetric sine
// bias swing does NOT produce a symmetric gain swing: the half-cycle
// swinging toward cutoff dips with accelerating speed, while the half
// swinging back toward the tube's normal operating point recovers more
// gently, tapering as it nears the ceiling it can't exceed (gain is always
// <=1 — bias modulation only ever *reduces* gain from the unbiased point,
// never boosts past it). Mono: the same gain drives both channels, so this
// can never become autopan. See TremoloModule.cpp for the shaping curve.
//
// Harmonic: the later blackface/brownface mechanism (a different physical
// circuit, not a bias trick) -- an active crossover splits the signal into
// low and high bands, and each band's amplitude is modulated by the same
// LFO but 180 degrees out of phase with the other, so brightness swings
// back and forth between the two bands rather than the whole signal's level
// moving together (hence "harmonic," not amplitude, tremolo). The crossover
// (JUCE's LinkwitzRileyFilter, 4th-order/24dB-oct, low+high guaranteed to
// sum flat) is fixed at 700Hz, not user-adjustable, matching how every
// other "real hardware doesn't have this knob" filter in this plugin is
// handled. Stereo: left assigns the low band the LFO's "gainA" phase and
// the high band "gainB"; right swaps that assignment, so the two channels'
// spectral balance swings in opposite directions as the LFO cycles --
// genuine stereo width, not the Bias voice's identical-both-channels gain.
class TremoloModule
{
public:
    enum class Voice { bias = 0, harmonic = 1 };

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setAmount (float amountPercent);
    void setRate (float rateHz);
    void setVoice (Voice newVoice) { voice = newVoice; }
    void process (juce::AudioBuffer<float>& buffer);
    bool isWetTransitionActive() const noexcept
    {
        return amount.isSmoothing() || amount.getCurrentValue() > 0.00001f;
    }

private:
    juce::SmoothedValue<float> amount;
    juce::SmoothedValue<float> rate;
    juce::dsp::LinkwitzRileyFilter<float> crossover;
    double sampleRate = 44100.0;
    float phase = 0.0f;
    Voice voice = Voice::bias;
};
