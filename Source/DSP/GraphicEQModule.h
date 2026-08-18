#pragma once
#include <JuceHeader.h>
#include <array>

// 9-band graphic EQ (Neural-DSP-style: fixed frequencies, gain-only
// sliders) plus a standalone HPF/LPF pair, sitting after the wet effects
// and before the output stage.
//
// Centres follow the classic ISO 1-octave series from 62.5 Hz-16 kHz (the
// guitar-relevant slice of the standard 10-band series, dropping the
// 31.5 Hz sub-bass band nothing on guitar lives below).
//
// Parallel topology: each band is an independent constant-0dB-peak-gain
// bandpass filter (JUCE's makeBandPass -- unity gain at its own centre,
// falling away either side) that always sees the same dry, post-HPF
// signal, scaled by (linearGain - 1) and summed back onto that signal:
//   y = x + sum_i bandpass_i(x) * (gain_i - 1)
// At each band's own centre frequency the other eight bands' bandpass
// responses are already small, so that band's term contributes (gain_i-1)
// on top of the dry path's 1, landing on gain_i by construction -- exact,
// and independent of every other slider. A series biquad cascade (the
// previous design here) doesn't have that property: every band's setting
// shifts what every other band's centre actually measures, which is why
// that version needed an iterative numerical correction pass to stay
// close to the sliders. Bands left at 0 dB contribute exactly zero, so an
// all-flat EQ is bit-identical to off.
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
            auto* samples = buffer.getWritePointer (ch);
            auto& state = channelState[(size_t) ch];

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                auto x = samples[i];
                if (hpfEnabled)
                    x = state.hpf.processSample (x);

                auto wet = x;
                for (int b = 0; b < numBands; ++b)
                    wet += state.bands[(size_t) b].processSample (x) * bandGainMinusOne[(size_t) b];

                if (lpfEnabled)
                    wet = state.lpf.processSample (wet);

                samples[i] = wet;
            }
        }
    }

private:
    void updateBandCoefficients()
    {
        for (int b = 0; b < numBands; ++b)
        {
            const auto centre = juce::jmin (getCentreFrequencies()[(size_t) b],
                                            static_cast<float> (sampleRate * 0.45));
            auto coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRate, centre, bandQ);
            for (auto& channel : channelState)
                *channel.bands[(size_t) b].coefficients = *coefficients;
            bandGainMinusOne[(size_t) b] = juce::Decibels::decibelsToGain (gainsDb[(size_t) b]) - 1.0f;
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
    std::array<float, numBands> bandGainMinusOne {};
    bool enabled = false;
    bool hpfEnabled = false, lpfEnabled = false;
    float hpfHz = 80.0f, lpfHz = 8000.0f;
    static constexpr float bandQ = 1.6f;
    double sampleRate = 44100.0;
};
