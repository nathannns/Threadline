#pragma once

#include <JuceHeader.h>
#include "Antialiasing.h"

// Roland RE-201 Space Echo-inspired: 3 tape playback heads at fixed,
// equally-spaced positions (delay ratio exactly 1:2:3 off head 1's time).
// Genuinely different DSP from Threadline's own "Plexer" delay engine
// (EchoModule.h, modeled on the Maestro Echoplex EP-3's single-head/tape-
// loop behaviour instead). Pattern names stay the original friendly ones
// (Straight/Bounce/Gallop/Cluster/Wash) -- see getPattern() in the .cpp for
// how each maps to a real head combination underneath. Ping-Pong is a new,
// explicitly modern bonus mode, not an RE-201 characteristic (the real
// unit is mono).
//
// The real unit's tone stack is a passive Bass/Treble shelving pair (not a
// single lowpass), and a tape record/playback path's saturation is
// genuinely hysteretic (the magnetic response to a rising field differs
// from a falling one) rather than a plain symmetric tanh -- see the
// Bass/Treble shelves and processHysteresisDrive() below, an explicit-
// integration Jiles-Atherton-style model, which replace the single
// one-pole "Tone" and plain tanh saturator this used to have.
class SpaceEchoModule
{
public:
    enum Pattern { straight, bounce, gallop, cluster, wash, pingPong };

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setParameters (float timeMs, float repeats, float bassPercent, float treblePercent,
                        float wobble, float drive, float mix, bool enabled, int patternIndex);
    void process (juce::AudioBuffer<float>& buffer);
    bool isWetTransitionActive() const noexcept
    {
        return wetMix.isSmoothing() || wetMix.getCurrentValue() > 0.00001f;
    }

private:
    float readDelay (int channel, float delaySamples) const;
    float processHysteresisDrive (int channel, float sample, float driveAmount);
    void getPattern (float* ratios, float* gains, int& taps) const;
    void updateShelfCoefficients();

    juce::AudioBuffer<float> delayBuffer;
    juce::SmoothedValue<float> delaySamples;
    juce::SmoothedValue<float> wetMix;
    juce::SmoothedValue<float> feedbackValue;
    juce::SmoothedValue<float> wobbleValue;
    juce::SmoothedValue<float> driveValue;
    // Fixed corner frequencies (typical of a tape-echo preamp tone stack),
    // gain set by the Bass/Treble knobs -- replaces the single one-pole
    // "Tone" filter this module used to have.
    juce::dsp::IIR::Filter<float> bassShelf[2], trebleShelf[2];
    float bassGainDb = 0.0f, trebleGainDb = 0.0f;
    float cachedBassGainDb = std::numeric_limits<float>::lowest();
    float cachedTrebleGainDb = std::numeric_limits<float>::lowest();
    // State for the hysteresis-style drive stage below (see
    // processHysteresisDrive in the .cpp) -- an explicit-integration
    // adaptation of the Jiles-Atherton magnetic hysteresis model (the same
    // general model Chowdhury's DAFx-19 tape paper adapts): hysteresisState
    // is the "magnetisation" the input chases along an anhysteretic
    // (Langevin-function) target curve; previousInputState lets each block
    // compute the input's instantaneous rate of change; directionState is
    // that rate's sign, lightly smoothed to avoid zipper noise from fast
    // low-amplitude wiggles flipping the direction gate every sample.
    std::vector<float> hysteresisState, previousInputState, directionState;
    float directionSmoothCoefficient = 1.0f;
    // Antialiased (ADAA, see Antialiasing.h) write-side safety rail --
    // indexed 0/1 per delay line regardless of whether that line is being
    // driven by the main (per-channel) path or Ping-Pong's explicit
    // line-0/line-1 addressing. The drive stage itself is the hysteresis
    // model above now, which is already a bounded, dynamics-aware
    // nonlinearity in its own right, so it no longer needs its own
    // separate ADAA saturator on top.
    AdaaSmoothRail writeRail[2];
    double sampleRate = 44100.0;
    int writeIndex = 0;
    int validSamples = 0;
    int pattern = straight;
    float lfoPhase = 0.0f;
    float flutterPhase = 0.0f;
};
