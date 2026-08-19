#pragma once
#include <JuceHeader.h>

// "Desk" -- a console-summing-style coloration stage. Its core shaping
// curve is an inverse-power soft-clip/compand function -- for x >= 0,
// y = 1-(1-x)^n; for x < 0, mirrored (odd symmetry) -- generalizing the
// specific n=2 "inverse square" curve used by Airwindows' Console-series
// plugins (Copyright (c) airwindows, MIT license; that exact 1-(1-x)^2 /
// 1-(1-x)^0.5 formulation is itself credited in Airwindows' own source to
// "torridgristle" under the MIT license) to a continuously variable
// steepness selected by the Style control, blended against dry by Amount.
// This is an original architecture/implementation around that small
// credited technique, not a port of any specific Airwindows plugin file.
class DeskModule
{
public:
    void prepare (const juce::dsp::ProcessSpec&) {}
    void reset() {}

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    // amountPercent: 0-100, dry/wet blend of the shaping curve.
    // stylePower: the curve's exponent n -- higher n compands harder.
    void setParameters (float amountPercent, float stylePower)
    {
        amount = juce::jlimit (0.0f, 1.0f, amountPercent * 0.01f);
        power = juce::jlimit (1.2f, 3.5f, stylePower);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled || amount <= 0.00001f)
            return;

        const auto channels = buffer.getNumChannels();
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* samples = buffer.getWritePointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                samples[i] = juce::jmap (amount, samples[i], shape (samples[i]));
        }
    }

private:
    float shape (float x) const
    {
        const auto clamped = juce::jlimit (-1.0f, 1.0f, x);
        if (clamped >= 0.0f)
            return 1.0f - std::pow (1.0f - clamped, power);
        return -(1.0f - std::pow (1.0f + clamped, power));
    }

    bool enabled = false;
    float amount = 0.0f;
    float power = 2.0f;
};
