#pragma once

#include <JuceHeader.h>

namespace CabStereoRouter
{
    // Routes two already-processed cab buffers into the plug-in output.
    // Both active on a stereo bus means A=left and B=right. A mono host
    // cannot expose that image, so it retains the former parallel blend.
    inline void route (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b,
                       juce::AudioBuffer<float>& output, bool useA, bool useB, float balance)
    {
        const auto samples = output.getNumSamples();
        const auto channels = output.getNumChannels();
        balance = juce::jlimit (0.0f, 1.0f, balance);

        if (useA && useB && channels >= 2)
        {
            const auto gainA = balance <= 0.5f ? 1.0f : 2.0f * (1.0f - balance);
            const auto gainB = balance >= 0.5f ? 1.0f : 2.0f * balance;
            output.copyFrom (0, 0, a, 0, 0, samples);
            output.copyFrom (1, 0, b, 1, 0, samples);
            output.applyGain (0, 0, samples, gainA);
            output.applyGain (1, 0, samples, gainB);
            return;
        }

        if (useA && useB)
        {
            for (int i = 0; i < samples; ++i)
            {
                output.setSample (0, i, a.getSample (0, i) * (1.0f - balance)
                                        + b.getSample (0, i) * balance);
            }
            return;
        }

        const auto& source = useA ? a : b;
        for (int ch = 0; ch < channels; ++ch)
        {
            output.copyFrom (ch, 0, source, ch, 0, samples);
        }
    }
}
