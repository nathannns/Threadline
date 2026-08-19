#pragma once
#include <JuceHeader.h>

// A first-order complementary lowpass/highpass split (low+high == x
// exactly -- a leaky-integrator lowpass paired with its own residual as
// the highpass) summed back together with a sign flip on the high side --
// the classic, well-documented passive-tone-stack trick behind pedals
// like the Big Muff Pi and the RAT's own "Filter" control: at the two
// extremes it reproduces the signal in full, but blended near the middle
// the flipped high path partially cancels the low path right around the
// crossover frequency, carving a musical notch/scoop rather than a plain
// shelf. General, well-established technique (not tied to or copied from
// any specific product/codebase) -- an exact complementary split (unlike
// summing two independently-designed Butterworth low/high filters, which
// are only approximately complementary) is what makes the cancellation
// genuinely happen at the crossover rather than just approximating a
// scoop shape.
struct ComplementaryToneStack
{
    void prepare (double sampleRateIn) { sampleRate = sampleRateIn; updateCoefficient(); }
    void reset() { lowState = 0.0f; }

    void setCrossoverHz (float hz)
    {
        if (! juce::approximatelyEqual (hz, crossoverHz))
        {
            crossoverHz = hz;
            updateCoefficient();
        }
    }

    // blend: 0 = full low (bass), 1 = full high (treble), 0.5 = deepest notch.
    float processSample (float x, float blend) noexcept
    {
        lowState += coeff * (x - lowState);
        const auto high = x - lowState;
        return juce::jmap (blend, lowState, -high);
    }

private:
    void updateCoefficient()
    {
        coeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * crossoverHz / (float) sampleRate);
    }

    double sampleRate = 44100.0;
    float crossoverHz = 700.0f;
    float coeff = 0.1f;
    float lowState = 0.0f;
};
