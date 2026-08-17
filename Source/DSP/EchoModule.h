#pragma once

#include <JuceHeader.h>

// "Plexer" is this plugin's own name for the effect (Echoplex is Maestro/
// Dunlop's trademark, kept out of our UI/parameter names) — modeled on the
// real Maestro Echoplex EP-3's exact control surface and behaviour, not the
// Roland RE-201 this module was originally built around.
//
// The EP-3 has 3 controls (Sustain, Volume, and a delay-time control — a
// physical slider on the real unit, a knob here) plus an Echo / Sound-on-
// Sound mode switch — a single tape loop and playback head, not the RE-201's
// three fixed heads. Its solid-state preamp is always in the signal path
// once engaged (many players used it purely for that boost/coloration with
// the echo volume all the way down), so it's modeled as an always-on stage
// here too, not a separate Tone knob the real unit doesn't have. Likewise
// Wobble and Drive aren't separate knobs on the real unit either — tape
// wow/flutter and the saturation that "intensifies over successive repeats
// when feedback is applied" (per the real circuit's bias-oscillator/tape-
// hysteresis behaviour) are both fixed, Sustain-linked characteristics here,
// not independent user controls.
//
// Sustain reaches genuine self-oscillation at maximum, same as the real
// unit — a soft-clip safety rail keeps that bounded (loud, chaotic, but not
// numerically infinite) rather than capping feedback below the real
// hardware's actual ceiling. Volume is additive, not a dry/wet crossfade:
// on the real unit, the direct (preamp-colored) signal is always present
// once patched in, and the knob adds echo volume on top of it rather than
// trading dry for wet.
//
// Below that self-oscillation zone, Sustain's feedback curve is derived
// from an explicit target repeat count N rather than picked by eye: after
// N repeats the tail should be about -40dB down (g^N = 10^(-40/20) = 0.01),
// so g = 10^(-2/N). That maps the knob to "how many times do you want to
// hear it" — the quantity Sustain actually controls perceptually — instead
// of an arbitrary coefficient range. See EchoModule::setParameters.
//
// A real tape loop's own bandwidth (plus the record/playback head response)
// rolls off highs a little on every single pass -- without that, repeated
// saturation of an unfiltered signal just piles up harmonics each cycle
// and reads as a harsh, metallic hiss rather than a warm tape repeat. A
// fixed lowpass sits inside the feedback path (after saturation, so it
// tames the harmonics saturation just generated) to model that loss.
//
// Saturation drive is keyed off the Sustain knob's own 0-1 position, not
// the internal feedback coefficient -- that coefficient means something
// different per mode (Echo mode spans roughly 0.01-1.03 across the whole
// knob; Sound-on-Sound spans only 0.90-0.99, since it needs a high floor
// for long sustain rather than headroom toward oscillation), so driving
// saturation from it directly meant Sound-on-Sound ran close to maximum
// drive at every Sustain setting, not just high ones -- audible as harsh
// treble distortion regardless of where the knob was.
class EchoModule
{
public:
    enum class Mode { echo = 0, soundOnSound = 1 };

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setParameters (float timeMs, float sustainPercent, float volumePercent,
                        bool enabled, Mode mode);
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
    juce::SmoothedValue<float> volumeValue;
    juce::SmoothedValue<float> saturationDrive;
    // Fixed (not user-adjustable) preamp coloring — "sweetens the treble,
    // fattens the mids" per the real EP-3's always-on solid-state preamp.
    juce::dsp::IIR::Filter<float> preampMidFilter[2], preampTrebleFilter[2];
    // Fixed tape-loop rolloff inside the feedback path -- see file header.
    juce::dsp::IIR::Filter<float> repeatDarkenFilter[2];
    double sampleRate = 44100.0;
    int writeIndex = 0;
    int validSamples = 0;
    Mode mode = Mode::echo;
    float lfoPhase = 0.0f;
    float flutterPhase = 0.0f;
};
