#pragma once

#include <JuceHeader.h>

// "July" is this plugin's own name for the effect (Julia is Walrus Audio's
// trademark, kept out of our UI/parameter names) — internally it's modeled
// on the real Julia's exact control surface: Rate, Depth, Lag (the LFO's
// center delay time), a Sine/Triangle waveform switch, and D-C-V
// (Dry-Chorus-Vibrato) — rather than the wider knob set (Width/Tone/
// Flanger modes) an earlier, Dimension-D-inspired version of this module had.
//
// A single BBD-modeled delay line per channel, both channels driven by the
// same LFO phase (Julia itself isn't a dedicated stereo design — that's a
// different Walrus pedal, Julianna — so this doesn't invent a stereo-width
// control Julia doesn't have). D-C-V is a genuine dry/wet crossfade: 0% is
// dry, the middle is traditional chorus (a modulated-delay copy blended
// with dry, which is what produces chorus's characteristic comb-filtered
// wobble), and 100% is pure vibrato (a modulated delay alone with no dry
// reference — pitch modulation with no comb filtering, since there's
// nothing left to comb against).
class ChorusModule
{
public:
    enum class Waveform { sine = 0, triangle = 1 };

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setParameters (float rateHz, float depthPercent, float lagPercent,
                        Waveform waveform, float dcvPercent, bool enabled);
    void process (juce::AudioBuffer<float>& buffer);

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
