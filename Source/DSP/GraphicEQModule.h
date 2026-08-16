#pragma once
#include <JuceHeader.h>
#include <array>

// 9-band graphic EQ (Neural-DSP-style: fixed frequencies, gain-only
// sliders) plus a standalone HPF/LPF pair, sitting after the wet effects
// and before the output stage.
//
// Centres follow the classic ISO 1-octave series from 62.5 Hz-16 kHz (the
// guitar-relevant slice of the standard 10-band series, dropping the
// 31.5 Hz sub-bass band nothing on guitar lives below). True-octave spacing
// gives a theoretical Q of ~1.41 ("All About Audio Equalization: Solutions
// and Frontiers"); Q here is nudged up a bit from that so adjacent bands
// don't smear together as much when several are boosted at once (the
// cascaded-filter interaction Liski & Valimaki's graphic-EQ papers
// describe) — at the cost of the textbook-flat sum a full parallel/
// corrected design solves for. This implementation compensates that cascade
// numerically: it measures the complete trial response at all nine centres and
// iteratively adjusts the internal gains until those responses follow the
// visible sliders. We retain the low-cost, stable biquad cascade while fixing
// the most noticeable adjacent-band interaction.
class GraphicEQModule
{
public:
    static constexpr int numBands = 9;

    static const std::array<float, numBands>& getCentreFrequencies()
    {
        static const std::array<float, numBands> freqs {
            62.5f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
        };
        return freqs;
    }

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        for (auto& ch : channelState)
        {
            for (auto& band : ch.bands)
                band.prepare (spec);
            ch.hpf.prepare (spec);
            ch.lpf.prepare (spec);
        }
        reset();
        updateBandCoefficients();
        updateFilterCoefficients();
    }

    void reset()
    {
        for (auto& ch : channelState)
        {
            for (auto& band : ch.bands)
                band.reset();
            ch.hpf.reset();
            ch.lpf.reset();
        }
    }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    // 9 values in dB, -12..+12.
    void setBandGains (const std::array<float, numBands>& bandGainsDb)
    {
        if (bandGainsDb != gainsDb)
        {
            gainsDb = bandGainsDb;
            updateBandCoefficients();
        }
    }

    void setHighPass (bool hpfOn, float frequencyHz)
    {
        frequencyHz = juce::jlimit (20.0f, 1000.0f, frequencyHz);
        if (hpfOn != hpfEnabled || std::abs (frequencyHz - hpfHz) > 0.01f)
        {
            hpfEnabled = hpfOn;
            hpfHz = frequencyHz;
            updateFilterCoefficients();
        }
    }

    void setLowPass (bool lpfOn, float frequencyHz)
    {
        frequencyHz = juce::jlimit (1000.0f, 20000.0f, frequencyHz);
        if (lpfOn != lpfEnabled || std::abs (frequencyHz - lpfHz) > 0.01f)
        {
            lpfEnabled = lpfOn;
            lpfHz = frequencyHz;
            updateFilterCoefficients();
        }
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), 2);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto block = juce::dsp::AudioBlock<float> (buffer).getSingleChannelBlock ((size_t) ch);
            juce::dsp::ProcessContextReplacing<float> context (block);
            auto& state = channelState[(size_t) ch];

            if (hpfEnabled)
                state.hpf.process (context);
            for (auto& band : state.bands)
                band.process (context);
            if (lpfEnabled)
                state.lpf.process (context);
        }
    }

private:
    void updateBandCoefficients()
    {
        std::array<float, numBands> correctedGains = gainsDb;
        std::array<juce::dsp::IIR::Coefficients<float>::Ptr, numBands> trial;

        // Three Newton-like correction passes are enough at this band count.
        // Each pass measures the actual sum of all cascaded sections at every
        // slider centre, then applies the residual error to that band's command.
        for (int iteration = 0; iteration < 3; ++iteration)
        {
            for (int b = 0; b < numBands; ++b)
            {
                const auto centre = juce::jmin (getCentreFrequencies()[(size_t) b],
                                                static_cast<float> (sampleRate * 0.45));
                trial[(size_t) b] = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                    sampleRate, centre, bandQ,
                    juce::Decibels::decibelsToGain (correctedGains[(size_t) b]));
            }

            for (int target = 0; target < numBands; ++target)
            {
                const auto frequency = juce::jmin (getCentreFrequencies()[(size_t) target],
                                                   static_cast<float> (sampleRate * 0.45));
                float actualDb = 0.0f;
                for (const auto& coefficients : trial)
                    actualDb += juce::Decibels::gainToDecibels (
                        static_cast<float> (coefficients->getMagnitudeForFrequency (frequency, sampleRate)), -60.0f);

                const auto residual = gainsDb[(size_t) target] - actualDb;
                correctedGains[(size_t) target] = juce::jlimit (-18.0f, 18.0f,
                    correctedGains[(size_t) target] + residual * 0.82f);
            }
        }

        for (int b = 0; b < numBands; ++b)
        {
            const auto centre = juce::jmin (getCentreFrequencies()[(size_t) b],
                                            static_cast<float> (sampleRate * 0.45));
            auto coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                sampleRate, centre, bandQ,
                juce::Decibels::decibelsToGain (correctedGains[(size_t) b]));
            for (auto& channel : channelState)
                *channel.bands[(size_t) b].coefficients = *coefficients;
        }
    }

    void updateFilterCoefficients()
    {
        auto hpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, hpfHz, 0.707f);
        auto lpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, lpfHz, 0.707f);
        for (auto& ch : channelState)
        {
            *ch.hpf.coefficients = *hpfCoeffs;
            *ch.lpf.coefficients = *lpfCoeffs;
        }
    }

    struct ChannelState
    {
        std::array<juce::dsp::IIR::Filter<float>, numBands> bands;
        juce::dsp::IIR::Filter<float> hpf, lpf;
    };

    std::array<ChannelState, 2> channelState;
    std::array<float, numBands> gainsDb {};
    bool enabled = false;
    bool hpfEnabled = false, lpfEnabled = false;
    float hpfHz = 80.0f, lpfHz = 8000.0f;
    static constexpr float bandQ = 1.6f;
    double sampleRate = 44100.0;
};
