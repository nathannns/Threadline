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
// 2x-oversamples just the nonlinear clip stage: the treble pre-emphasis and
// clean/driven blend are linear operations (they don't generate new harmonic
// content), so only the asinh/tanh clip itself needs the higher rate to keep
// the harmonics it generates from folding back as audible aliasing —
// AmpModule already does this for the same reason; Klon and TS9 didn't.
class KlonModule
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        channelCount = juce::jlimit (1, 2, (int) spec.numChannels);
        for (auto& f : trebleFilter)
            f.prepare (spec);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) channelCount, 1 /* 1 stage = 2x */,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversampling->initProcessing (spec.maximumBlockSize);

        dryDelay.prepare (spec);
        dryDelay.setMaximumDelayInSamples (64);
        dryDelay.setDelay ((float) getLatencySamples());

        updateFilter();
        reset();
    }

    void reset()
    {
        for (auto& f : trebleFilter)
            f.reset();
        if (oversampling != nullptr)
            oversampling->reset();
        dryDelay.reset();
    }

    int getLatencySamples() const noexcept
    {
        return oversampling != nullptr ? juce::roundToInt (oversampling->getLatencyInSamples()) : 0;
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
        if (! enabled || oversampling == nullptr)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), channelCount);
        const auto numSamples = buffer.getNumSamples();
        const auto driveGain = 1.0f + gainAmount * 11.0f;
        const auto wet = juce::jlimit (0.0f, 1.0f, gainAmount);

        // Pre-emphasis treble boost (linear — stays at base rate) feeding
        // only the drive path; dry is kept untouched for the blend below.
        preClipBuffer.setSize (numChannels, numSamples, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* dst = preClipBuffer.getWritePointer (ch);
            auto* src = buffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                dst[i] = trebleFilter[ch].processSample (src[i]);
        }

        // Only the nonlinear clip itself runs at 2x — that's the stage that
        // generates the high-order harmonics that can alias.
        juce::dsp::AudioBlock<float> block (preClipBuffer);
        auto osBlock = oversampling->processSamplesUp (block);
        const auto osSamples = (int) osBlock.getNumSamples();
        for (size_t ch = 0; ch < osBlock.getNumChannels(); ++ch)
        {
            for (int i = 0; i < osSamples; ++i)
            {
                auto x = osBlock.getSample ((int) ch, i) * driveGain;
                const auto kneeScale = x >= 0.0f ? 0.55f : 1.05f; // germanium : silicon
                const auto shaped = thermalVoltage * std::asinh (x / kneeScale);
                auto clipped = ceilingLimit * std::tanh (shaped / ceilingLimit);
                clipped /= std::max (0.5f, driveGain * 0.30f); // keep loudness sane across gain range
                osBlock.setSample ((int) ch, i, clipped);
            }
        }
        oversampling->processSamplesDown (block);

        // Blend clean and clipped — the "transparent" part. The oversampled
        // clip path now carries the oversampler's reported latency that the
        // dry path doesn't, so dry is pushed through a matching delay line
        // first — otherwise the blend would smear transients (dry and wet
        // arriving at slightly different times) instead of cleanly mixing.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* dry = buffer.getWritePointer (ch);
            auto* clippedPtr = preClipBuffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                dryDelay.pushSample (ch, dry[i]);
                const auto delayedDry = dryDelay.popSample (ch);
                dry[i] = (delayedDry * (1.0f - wet) + clippedPtr[i] * wet) * outputLevel;
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
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::DelayLine<float> dryDelay;
    juce::AudioBuffer<float> preClipBuffer;
    static constexpr float thermalVoltage = 0.62f;
    static constexpr float ceilingLimit = 1.05f;
    double sampleRate = 44100.0;
    int channelCount = 2;
    float gainAmount = 0.3f;
    float outputLevel = 1.0f;
    float lastTreble01 = -1.0f;
    bool enabled = false;
};
