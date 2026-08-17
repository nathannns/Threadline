#pragma once

#include <JuceHeader.h>

// "Plexy" is this plugin's own name for the effect (Echoplex is Maestro/
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
    // Fixed (not user-adjustable) preamp coloring — "sweetens the treble,
    // fattens the mids" per the real EP-3's always-on solid-state preamp.
    juce::dsp::IIR::Filter<float> preampMidFilter[2], preampTrebleFilter[2];
    double sampleRate = 44100.0;
    int writeIndex = 0;
    int validSamples = 0;
    Mode mode = Mode::echo;
    float lfoPhase = 0.0f;
    float flutterPhase = 0.0f;
};
