#pragma once
#include <JuceHeader.h>
#include <array>

// "Redface" -- an original implementation of a classic British console
// channel strip's preamp+EQ band structure (the shape popularised by
// Neve's 1073 preamp module and, decades later, plugins modeled after it
// like Waves' Scheps 73): a Gain (preamp trim) stage first in the signal
// path -- a smooth, continuous control here rather than the real
// hardware's stepped switch, since a pedal's line-level signal doesn't
// need mic-preamp-style fixed dB stops -- then a shelving low band and a
// shelving high band flanking a single swept-frequency peaking mid band,
// plus a switchable multi-stop high-pass filter. Band frequencies/ranges
// and the Gain stage's position ahead of the EQ follow the well-
// documented, publicly published stock 1073 spec/panel layout (AMS
// Neve's own product literature) -- not any third-party plugin or code;
// the actual filter implementation (JUCE's own shelf/peak/highpass IIR
// coefficient design, cascaded per band) and the Gain stage's soft-clip
// character are original to this project.
//
// Unlike GraphicEQModule's parallel-bandpass topology (built for a fixed
// bank of *many* narrow bands that must stay independent of each other),
// this is a small, deliberately *interactive* cascade of just 3 shaped
// bands plus a filter -- much closer to how the real hardware's inductor
// stages sit in series, and appropriate at only 3 bands where mutual
// interaction between neighbouring bands is part of the classic sound
// rather than something to engineer away.
class ChannelEQModule
{
public:
    static constexpr int numLowFreqs = 4;
    static constexpr int numMidFreqs = 6;
    static constexpr int numHpfFreqs = 4;

    static const std::array<float, numLowFreqs>& getLowFrequencies()
    {
        static const std::array<float, numLowFreqs> freqs { 35.0f, 60.0f, 110.0f, 220.0f };
        return freqs;
    }

    static const std::array<float, numMidFreqs>& getMidFrequencies()
    {
        static const std::array<float, numMidFreqs> freqs { 360.0f, 700.0f, 1600.0f, 3200.0f, 4800.0f, 7200.0f };
        return freqs;
    }

    static const std::array<float, numHpfFreqs>& getHpfFrequencies()
    {
        static const std::array<float, numHpfFreqs> freqs { 50.0f, 80.0f, 160.0f, 300.0f };
        return freqs;
    }

    static constexpr float highShelfFrequency = 12000.0f;

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        reset();
        updateAllCoefficients();
    }

    void reset()
    {
        for (auto& ch : channelState)
        {
            ch.dcBlockPreviousIn = ch.dcBlockPreviousOut = 0.0f;
            ch.hpfFirstOrder.reset();
            ch.hpfSecondOrder.reset();
            ch.lowShelf.reset();
            ch.midPeak.reset();
            ch.highShelf.reset();
        }
    }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    void setParameters (float preampGainDb, int lowFreqIndex, float lowGainDb, int midFreqIndex, float midGainDb,
                        float highGainDb, int hpfFreqIndex, bool hpfOn)
    {
        preampGain = juce::Decibels::decibelsToGain (juce::jlimit (-20.0f, 20.0f, preampGainDb));
        lowFreqIndex = juce::jlimit (0, numLowFreqs - 1, lowFreqIndex);
        midFreqIndex = juce::jlimit (0, numMidFreqs - 1, midFreqIndex);
        hpfFreqIndex = juce::jlimit (0, numHpfFreqs - 1, hpfFreqIndex);

        const auto changed = lowFreqIndex != cachedLowFreqIndex
                           || std::abs (lowGainDb - cachedLowGainDb) > 0.01f
                           || midFreqIndex != cachedMidFreqIndex
                           || std::abs (midGainDb - cachedMidGainDb) > 0.01f
                           || std::abs (highGainDb - cachedHighGainDb) > 0.01f
                           || hpfFreqIndex != cachedHpfFreqIndex;
        hpfEnabled = hpfOn;
        if (! changed)
            return;

        cachedLowFreqIndex = lowFreqIndex; cachedLowGainDb = lowGainDb;
        cachedMidFreqIndex = midFreqIndex; cachedMidGainDb = midGainDb;
        cachedHighGainDb = highGainDb;
        cachedHpfFreqIndex = hpfFreqIndex;
        updateAllCoefficients();
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), 2);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* samples = buffer.getWritePointer (ch);
            auto& state = channelState[(size_t) ch];
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                // Preamp gain -- first in the signal path, ahead of the EQ,
                // matching the real module's own front panel (Gain knob
                // above the EQ bands). Beyond a plain symmetric soft-clip,
                // this models the general, well-established character of
                // a transformer-coupled tube preamp stage: a small even-
                // harmonic term (the squared component, always positive
                // regardless of input polarity -- real transformer cores
                // genuinely respond asymmetrically to positive vs negative
                // excursions) plus an asymmetric tanh knee (a touch softer
                // on the positive half-cycle than the negative one). The
                // squared term biases the mean value upward, so a one-pole
                // DC blocker removes that bias before the signal reaches
                // the EQ bands below.
                auto x = samples[i] * preampGain;
                constexpr float evenHarmonicAmount = 0.06f;
                const auto coloured = x + evenHarmonicAmount * x * x;
                constexpr float positiveKnee = 0.78f, negativeKnee = 0.68f;
                float shaped;
                if (coloured >= 0.0f)
                    shaped = coloured <= positiveKnee ? coloured
                        : positiveKnee + (1.0f - positiveKnee) * std::tanh ((coloured - positiveKnee) / (1.0f - positiveKnee));
                else
                {
                    const auto magnitude = -coloured;
                    const auto shapedMagnitude = magnitude <= negativeKnee ? magnitude
                        : negativeKnee + (1.0f - negativeKnee) * std::tanh ((magnitude - negativeKnee) / (1.0f - negativeKnee));
                    shaped = -shapedMagnitude;
                }
                x = shaped - state.dcBlockPreviousIn + 0.995f * state.dcBlockPreviousOut;
                state.dcBlockPreviousIn = shaped;
                state.dcBlockPreviousOut = x;
                if (hpfEnabled)
                {
                    // 3rd-order (18dB/oct) high-pass, matching the real
                    // unit's spec: a 1st-order stage cascaded with a 2nd-
                    // order Butterworth stage at the same corner.
                    x = state.hpfFirstOrder.processSample (x);
                    x = state.hpfSecondOrder.processSample (x);
                }
                x = state.lowShelf.processSample (x);
                x = state.midPeak.processSample (x);
                x = state.highShelf.processSample (x);
                samples[i] = x;
            }
        }
    }

private:
    void updateAllCoefficients()
    {
        // cachedXIndex starts at -1 as a "force the first real setParameters()
        // call to update" sentinel -- prepare() calls this once before any
        // setParameters() call has happened, so these must be clamped rather
        // than trusted, or a -1 index reads out of bounds here.
        const auto lowHz = getLowFrequencies()[(size_t) juce::jlimit (0, numLowFreqs - 1, cachedLowFreqIndex)];
        const auto midHz = getMidFrequencies()[(size_t) juce::jlimit (0, numMidFreqs - 1, cachedMidFreqIndex)];
        const auto hpfHz = getHpfFrequencies()[(size_t) juce::jlimit (0, numHpfFreqs - 1, cachedHpfFreqIndex)];

        auto lowShelfCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            sampleRate, lowHz, 0.707f, juce::Decibels::decibelsToGain (cachedLowGainDb));
        // Fixed, moderate Q -- broad, "musical" bump/cut rather than a
        // surgical parametric notch, matching a fixed-Q inductor-peaking
        // stage rather than a general-purpose parametric EQ band.
        auto midPeakCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            sampleRate, midHz, 0.9f, juce::Decibels::decibelsToGain (cachedMidGainDb));
        auto highShelfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRate, highShelfFrequency, 0.707f, juce::Decibels::decibelsToGain (cachedHighGainDb));
        auto hpf1Coeffs = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (sampleRate, hpfHz);
        auto hpf2Coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, hpfHz, 0.707f);

        for (auto& ch : channelState)
        {
            ch.lowShelf.coefficients = lowShelfCoeffs;
            ch.midPeak.coefficients = midPeakCoeffs;
            ch.highShelf.coefficients = highShelfCoeffs;
            ch.hpfFirstOrder.coefficients = hpf1Coeffs;
            ch.hpfSecondOrder.coefficients = hpf2Coeffs;
        }
    }

    struct ChannelState
    {
        juce::dsp::IIR::Filter<float> hpfFirstOrder, hpfSecondOrder;
        juce::dsp::IIR::Filter<float> lowShelf, midPeak, highShelf;
        float dcBlockPreviousIn = 0.0f, dcBlockPreviousOut = 0.0f;
    };

    std::array<ChannelState, 2> channelState;
    bool enabled = false;
    bool hpfEnabled = false;
    float preampGain = 1.0f;
    int cachedLowFreqIndex = -1, cachedMidFreqIndex = -1, cachedHpfFreqIndex = -1;
    float cachedLowGainDb = std::numeric_limits<float>::lowest();
    float cachedMidGainDb = std::numeric_limits<float>::lowest();
    float cachedHighGainDb = std::numeric_limits<float>::lowest();
    double sampleRate = 44100.0;
};
