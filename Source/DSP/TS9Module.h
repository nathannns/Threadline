#pragma once

#include <JuceHeader.h>

// TS9-style overdrive, modeled after the real TS808/9 signal path rather
// than a single clip-and-voice block:
//  1. A FIXED pre-clip highpass (~720Hz, always on, not user-controlled) —
//     this is where the TS9's famous "mid-hump, honky" character actually
//     comes from: rolling off bass *before* the clipper means the clipped
//     signal's harmonic content is inherently mid-forward. Real TS808/9
//     circuits have this baked into the input coupling network; it isn't
//     something the Tone knob touches.
//  2. A symmetric asinh diode clip: asinh(x) is the closed-form transfer
//     function of a resistor-fed diode pair to ground (the standard
//     real-time simplification of the diode's exponential I-V law),
//     turning on more smoothly than tanh — matched silicon diodes on both
//     polarities, unlike the Klon's asymmetric germanium/silicon pair.
//  3. A post-clip Tone control that's a genuine treble-cut lowpass sweep
//     (what the real TS9's Tone pot actually is), not a swept mid-bump —
//     turning Tone down darkens it, up brightens it, same as the pedal.
class TS9Module
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        for (auto& f : preClipHighpass)
            f.prepare (spec);
        for (auto& f : toneFilter)
            f.prepare (spec);
        updatePreClipFilter();
        updateToneFilter();
        reset();
    }

    void reset()
    {
        for (auto& f : preClipHighpass)
            f.reset();
        for (auto& f : toneFilter)
            f.reset();
    }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    // drive/tone/level: 0-1. tone now sweeps a treble-cut lowpass (dark<->bright).
    void setParameters (float drive01, float tone01, float level01)
    {
        driveAmount = juce::jlimit (0.0f, 1.0f, drive01);
        outputLevel = juce::jlimit (0.0f, 2.0f, level01 * 2.0f);
        if (! juce::approximatelyEqual (tone01, lastTone01))
        {
            lastTone01 = tone01;
            updateToneFilter();
        }
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), 2);
        const auto numSamples = buffer.getNumSamples();
        const auto driveGain = 1.0f + driveAmount * 40.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                // Bass rolled off before the clipper — see class comment.
                const auto preShaped = preClipHighpass[ch].processSample (data[i]);
                auto x = preShaped * driveGain;

                // Symmetric diode-physics knee with a tanh safety ceiling
                // (bounds output across the drive range; asinh defines the
                // actual knee shape, not the ceiling).
                const auto shaped = thermalVoltage * std::asinh (x / kneeScale);
                auto clipped = ceilingLimit * std::tanh (shaped / ceilingLimit);
                clipped /= std::max (0.4f, std::sqrt (driveGain) * 0.42f);

                data[i] = toneFilter[ch].processSample (clipped) * outputLevel;
            }
        }
    }

private:
    void updatePreClipFilter()
    {
        // Fixed corner — not swept by any control, matching the real
        // TS808/9 input coupling network.
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 720.0f, 0.707f);
        for (auto& f : preClipHighpass)
            *f.coefficients = *coeffs;
    }

    void updateToneFilter()
    {
        // Treble-cut sweep: fully dark around 1.2kHz, fully bright above
        // hearing-relevant range — the real Tone pot's behaviour.
        const auto cutoff = juce::jmap (lastTone01, 0.0f, 1.0f, 1200.0f, 11000.0f);
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, cutoff, 0.707f);
        for (auto& f : toneFilter)
            *f.coefficients = *coeffs;
    }

    juce::dsp::IIR::Filter<float> preClipHighpass[2], toneFilter[2];
    static constexpr float thermalVoltage = 0.58f;
    static constexpr float ceilingLimit = 1.0f;
    static constexpr float kneeScale = 0.75f; // matched silicon diodes, symmetric
    double sampleRate = 44100.0;
    float driveAmount = 0.3f;
    float outputLevel = 1.0f;
    float lastTone01 = -1.0f;
    bool enabled = false;
};
