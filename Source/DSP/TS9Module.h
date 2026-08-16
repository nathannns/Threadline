#pragma once

#include <JuceHeader.h>

// TS9-style overdrive: an op-amp gain stage driving silicon diodes clipped to
// virtual ground (symmetric, harder-edged than the Klon's germanium clip),
// followed by the TS9's signature mid-hump tone stack (~720 Hz bump) that
// gives it its honky, cutting-through-the-mix character.
class TS9Module
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        for (auto& f : midFilter)
            f.prepare (spec);
        updateFilter();
        reset();
    }

    void reset()
    {
        for (auto& f : midFilter)
            f.reset();
    }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    // drive/tone/level: 0-1. tone sweeps the mid-hump from dark to bright.
    void setParameters (float drive01, float tone01, float level01)
    {
        driveAmount = juce::jlimit (0.0f, 1.0f, drive01);
        outputLevel = juce::jlimit (0.0f, 2.0f, level01 * 2.0f);
        if (! juce::approximatelyEqual (tone01, lastTone01))
        {
            lastTone01 = tone01;
            updateFilter();
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
                auto x = data[i] * driveGain;

                // Symmetric silicon-diode-style hard-ish clip.
                auto clipped = std::tanh (x);
                clipped /= std::max (0.3f, std::sqrt (driveGain) * 0.5f);

                // Mid-hump voicing.
                auto voiced = midFilter[ch].processSample (clipped);

                data[i] = voiced * outputLevel;
            }
        }
    }

private:
    void updateFilter()
    {
        const auto gainDb = juce::jmap (lastTone01, 0.0f, 1.0f, -2.0f, 8.0f);
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            sampleRate, 720.0f, 0.9f, juce::Decibels::decibelsToGain (gainDb));
        for (auto& f : midFilter)
            *f.coefficients = *coeffs;
    }

    juce::dsp::IIR::Filter<float> midFilter[2];
    double sampleRate = 44100.0;
    float driveAmount = 0.3f;
    float outputLevel = 1.0f;
    float lastTone01 = -1.0f;
    bool enabled = false;
};
