#pragma once

#include <JuceHeader.h>

// Klon-style "transparent" overdrive. The character comes from three things:
//  1. A pre-emphasis treble boost before the clipper (the Klon's distinctive
//     upper-mid/treble push, active even at Gain=0).
//  2. Germanium-style soft clipping with a low knee (~0.3 lower threshold
//     than silicon), so it rounds off peaks gently rather than slicing them.
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

                // Asymmetric germanium-style soft clip: positive half clips
                // slightly earlier/softer than negative half, characteristic
                // of a single-ended germanium diode pair.
                const auto driveGain = 1.0f + gainAmount * 11.0f;
                auto x = boosted * driveGain;
                auto clipped = x >= 0.0f ? std::tanh (x * 0.8f) * 0.92f
                                          : std::tanh (x * 0.95f);
                clipped /= std::max (0.2f, driveGain * 0.35f); // keep loudness sane across gain range

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
    double sampleRate = 44100.0;
    float gainAmount = 0.3f;
    float outputLevel = 1.0f;
    float lastTreble01 = -1.0f;
    bool enabled = false;
};
