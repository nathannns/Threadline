#pragma once

#include <JuceHeader.h>

// "July" is this plugin's own name for the effect (Julia is Walrus Audio's
// trademark, kept out of our UI/parameter names) — internally it's modeled
// on the real Julia's exact control surface: Rate, Depth, Lag (the LFO's
// center delay time), a Sine/Triangle waveform switch, and D-C-V
// (Dry-Chorus-Vibrato) — rather than the wider knob set (Width/Tone/
// Flanger modes) an earlier, Dimension-D-inspired version of this module
// had. One addition beyond the real Julia's own panel: a Mix knob, since a
// fixed 42%/100% wet amount per D-C-V stop left no way to actually dial in
// how much of the effect comes through.
//
// A single BBD-modeled delay line per channel, both channels driven by the
// same LFO phase (Julia itself isn't a dedicated stereo design — that's a
// different Walrus pedal, Julianna — so this doesn't invent a stereo-width
// control Julia doesn't have). D-C-V picks the character, not the amount:
// Dry always forces silence outright (selecting it means "off," full stop,
// regardless of Mix); Chorus is a modulated-delay copy blended with dry
// (which is what produces chorus's characteristic comb-filtered wobble);
// Vibrato is a modulated delay alone with no dry reference — pitch
// modulation with no comb filtering, since there's nothing left to comb
// against. Mix sets the actual dry/wet blend percentage whenever Chorus or
// Vibrato is selected.
class ChorusModule
{
public:
    enum class Waveform { sine = 0, triangle = 1 };

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setParameters (float rateHz, float depthPercent, float lagPercent,
                        Waveform waveform, float dcvPercent, bool enabled);
    void process (juce::AudioBuffer<float>& buffer);
    bool isWetTransitionActive() const noexcept
    {
        return dcvValue.isSmoothing() || dcvValue.getCurrentValue() > 0.00001f;
    }

private:
    float readDelay (int channel, float distance) const;

    juce::AudioBuffer<float> delayBuffer;
    juce::SmoothedValue<float> rateValue, depthValue, lagValue, dcvValue, waveformBlend;
    std::vector<float> companderEnvelope;
    double sampleRate = 44100.0;
    int writeIndex = 0;
    int validSamples = 0;
    float lfoPhase = 0.0f;
    float companderAttack = 1.0f;
    float companderRelease = 1.0f;
    // Fixed ~7kHz rolloff — a real BBD chip's own inherent HF loss, not a
    // user Tone control (Julia doesn't have one).
    std::vector<float> bbdToneState;
    float bbdToneCoefficient = 1.0f;
};
