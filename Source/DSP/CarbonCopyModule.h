#pragma once

#include <JuceHeader.h>
#include "Antialiasing.h"

// "Copier" is this plugin's own name for the effect (Carbon Copy is MXR/
// Dunlop's trademark, kept out of our UI/parameter names) -- modeled on the
// real MXR Carbon Copy: a bucket-brigade (BBD) analog delay with exactly 3
// knobs (Time, Regen, Mix) plus a Mod toggle, up to 600ms delay time. Real
// BBD chips require anti-alias filtering and are commonly wrapped in a
// compressor/expander. The Boss DM-2 and EHX Deluxe Memory Man schematics in
// el34world's archive both show an NE570 around the BBD plus reconstruction
// filters. Copier therefore stores a 2:1-compressed, band-limited signal and
// expands each read before mixing or feedback; each recirculation is encoded
// again, just as it re-enters the hardware BBD path.
// The Mod switch adds a slow, modest delay-time wobble (the real pedal's
// internal trim pots default to roughly 0.2-2.2Hz) for a chorus-like
// character on the repeats; off by default, same as the real unit's stock
// switch position. Regen reaches near-self-oscillation at maximum, same as
// the real pedal, bounded by a tanh safety rail.
//
// Below that near-self-oscillation zone, Regen's feedback curve is derived
// from an explicit target repeat count N rather than picked by eye: after
// N repeats the tail should be about -40dB down (g^N = 10^(-40/20) = 0.01),
// so g = 10^(-2/N). That maps the knob to "how many times do you want to
// hear it" instead of an arbitrary coefficient range. See
// CarbonCopyModule::setParameters.
class CarbonCopyModule
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setParameters (float timeMs, float regenPercent, float mixPercent, bool modOn, bool enabled);
    static float mapMix (float mix01)
    {
        return std::pow (juce::jlimit (0.0f, 1.0f, mix01), 1.7f);
    }
    void process (juce::AudioBuffer<float>& buffer);
    bool isWetTransitionActive() const noexcept
    {
        return wetMix.isSmoothing() || wetMix.getCurrentValue() > 0.00001f;
    }

private:
    float readDelay (int channel, float delaySamples) const;

    juce::AudioBuffer<float> delayBuffer;
    juce::SmoothedValue<float> delaySamples;
    juce::SmoothedValue<float> wetMix;
    juce::SmoothedValue<float> feedbackValue;
    // Fixed BBD-style darkening filter, inside the feedback path only --
    // each repeat passes through this again, so the tail gets darker with
    // every cycle rather than staying a constant colour.
    juce::dsp::IIR::Filter<float> darkenFilter[2];
    // ADAA'd (see Antialiasing.h) version of the write-side safety rail --
    // this pedal's only nonlinearity inside the feedback loop, so it's the
    // one place aliasing could recirculate and compound across repeats. It
    // only engages its nonlinear branch near self-oscillation, but that's
    // exactly the regime where harmonic content peaks.
    AdaaSmoothRail writeRail[2], readRail[2];
    float compressorEnvelope[2] { 0.0f, 0.0f };
    float expanderEnvelope[2] { 0.0f, 0.0f };
    float envelopeAttack = 0.0f, envelopeRelease = 0.0f;
    double sampleRate = 44100.0;
    int writeIndex = 0;
    int validSamples = 0;
    bool modEnabled = false;
    float modPhase = 0.0f;
};
