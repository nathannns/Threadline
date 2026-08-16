#pragma once

#include <JuceHeader.h>

// Klon-style "transparent" overdrive. The character comes from three things:
//  1. A pre-emphasis treble boost before the clipper (the Klon's distinctive
//     upper-mid/treble push, active even at Gain=0).
//  2. An asinh-shaped diode clip, asymmetric per polarity: asinh(x) is the
//     closed-form transfer function of a resistor-fed pair of diodes to
//     ground (solving the diode's exponential I-V law while treating Vout's
//     own feedback into the diode current as second-order — the standard
//     real-time simplification for this circuit), which turns on smoothly
//     like a real diode rather than tanh's more abrupt saturation. The
//     positive half uses a lower knee (germanium: higher leakage current,
//     conducts earlier/softer), the negative half a higher one (silicon:
//     conducts later/harder) — the Klon's actual asymmetric diode pair.
//  3. A clean/driven BLEND rather than a simple gain stage — Gain controls
//     how much clipped signal is mixed back in over the clean buffered
//     signal, which is what keeps it sounding "transparent" instead of
//     fuzzy even at higher settings.
class KlonModule
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        for (auto& f : trebleFilter)
            f.prepare (spec);
        updateFilter();
        reset();
    }

    void reset()
    {
        for (auto& f : trebleFilter)
            f.reset();
    }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    // gain/treble/level: 0-1. gain = blend amount toward the clipped path.
    void setParameters (float gain01, float treble01, float level01)
    {
        gainAmount = juce::jlimit (0.0f, 1.0f, gain01);
        outputLevel = juce::jlimit (0.0f, 2.0f, level01 * 2.0f);
        if (! juce::approximatelyEqual (treble01, lastTreble01))
        {
            lastTreble01 = treble01;
            updateFilter();
        }
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), 2);
        const auto numSamples = buffer.getNumSamples();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                const auto dry = data[i];

                // Pre-emphasis treble boost feeding only the drive path.
                auto boosted = trebleFilter[ch].processSample (dry);

                // Diode-physics knee (see class comment) with a tanh safety
                // ceiling so output stays bounded across the whole Gain
                // range — the ceiling doesn't define the clip character,
                // asinh's knee shape does.
                const auto driveGain = 1.0f + gainAmount * 11.0f;
                auto x = boosted * driveGain;
                const auto kneeScale = x >= 0.0f ? 0.55f : 1.05f; // germanium : silicon
                const auto shaped = thermalVoltage * std::asinh (x / kneeScale);
                auto clipped = ceilingLimit * std::tanh (shaped / ceilingLimit);
                clipped /= std::max (0.5f, driveGain * 0.30f); // keep loudness sane across gain range

                // Blend clean and clipped — the "transparent" part.
                const auto wet = juce::jlimit (0.0f, 1.0f, gainAmount);
                auto out = dry * (1.0f - wet) + clipped * wet;

                data[i] = out * outputLevel;
            }
        }
    }

private:
    void updateFilter()
    {
        // Fixed-frequency treble boost shelf; sweeps from mild to pronounced.
        const auto gainDb = juce::jmap (lastTreble01, 0.0f, 1.0f, 1.5f, 9.0f);
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRate, 900.0f, 0.6f, juce::Decibels::decibelsToGain (gainDb));
        for (auto& f : trebleFilter)
            *f.coefficients = *coeffs;
    }

    juce::dsp::IIR::Filter<float> trebleFilter[2];
    static constexpr float thermalVoltage = 0.62f;
    static constexpr float ceilingLimit = 1.05f;
    double sampleRate = 44100.0;
    float gainAmount = 0.3f;
    float outputLevel = 1.0f;
    float lastTreble01 = -1.0f;
    bool enabled = false;
};
