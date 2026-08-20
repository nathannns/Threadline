#pragma once

#include <JuceHeader.h>
#include "Antialiasing.h"

// Circuit-faithful digital port of the Roland/Boss DC-2 "Dimension C" BBD
// chorus -- the stompbox sibling of the SDD-320 "Dimension D" rack (same
// architecture family: dual MN3007 bucket-brigade delay lines, an NE570
// compander, and a slow LFO sweeping the two BBD clocks in opposition).
//
// Provenance: the SDD-320 service notes are not freely hosted (Gearspace and
// synth-diy block bots, vintagedigital sells them), so this is ported from
// the DC-2 schematic (freely mirrored -- the el34world Effects index carries
// the Boss DC-2 "Dimension C" under the same "Effects" tree, and it is
// widely reproduced) plus the MN3007/MN3101/NE570 datasheets. The four
// "Dimension" mode delay windows are calibrated to Roland's own published
// delay figures for the family (Mode I ~8-12ms, Mode II ~5-10ms, Modes
// III/IV ~6-9ms -- Gearspace measurement + Roland manual text). The DC-2 is
// the *sibling*, not the SDD-320 itself: it runs the same dual-BBD +
// compander + triangle-LFO topology on a single 9V rail rather than the
// SDD-320's 0V/-14V rack supply, which matters for analog headroom but not
// for the digital model's signal flow, filter corners, delay times, or LFO
// shape.
//
// This is a SEPARATE pedal from the existing DimensionChorusModule (the
// "Ensemble" pedal, a Rockalizer port): that one is an "inspired-by" 4-tap
// model, while this is the circuit-faithful BBD engine. The existing module
// stays shipped unchanged; this is a new node (see DimensionDBBDNode.h).
class DimensionDBBDModule
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setParameters (int mode, float inputLevel01, float outputLevel01);
    void process (juce::AudioBuffer<float>& buffer);

    // Exposed for the verification harness: the per-mode base delay window
    // (centre + half-swing, in seconds) and LFO rate, so the harness can
    // print exactly what delay range each preset targets without duplicating
    // the table.
    static void getPresetSpec (int mode, float& centerSec, float& swingSec, float& rateHz);

private:
    float readDelay (int line, float delaySamples) const;
    static float triangleWave (float phase) noexcept;

    struct Preset { float centerSec; float swingSec; float rateHz; };
    // Per-mode base delay window + LFO rate, calibrated to Roland's published
    // Dimension-family delay figures. centre+/-swing gives the sweep window
    // (Mode I: 10 +/- 2 = 8-12ms; Mode II: 7.5 +/- 2.5 = 5-10ms; Modes
    // III/IV: 7.5 +/- 1.5 = 6-9ms). C++17 constexpr => implicitly inline, so
    // no out-of-line definition is needed despite the private nested type.
    static constexpr std::array<Preset, 4> presets {{
        { 0.0100f, 0.0020f, 0.50f },   // Mode I  -- deep "invisible width"
        { 0.0075f, 0.0025f, 0.25f },   // Mode II -- widest, slowest
        { 0.0075f, 0.0015f, 0.50f },   // Mode III
        { 0.0075f, 0.0015f, 0.30f }    // Mode IV
    }};

    // The two MN3007 lines share one circular buffer (2 channels). Delay
    // time per line is 1024 stages / (2 * f_clk), reproduced directly as a
    // fractional sample count; the LFO modulates it by moving the read tap.
    juce::AudioBuffer<float> delayLine;

    // Fixed signal-path filters (see prepare() for each one's provenance).
    juce::dsp::IIR::Filter<float> dcBlock, preEmphasis, deEmphasis, aaFilter, reconA, reconB;

    // BBD input soft-saturation (the MN3007's charge-transfer cell clips
    // softly when overdriven by the input-level control) -- ADAA'd so the
    // nonlinearity doesn't alias inside the recursive BBD path.
    AdaaTanh bbdSaturation;

    juce::SmoothedValue<float> centerDelay, swing, rate, inputGain, outputGain;

    double sampleRate = 44100.0;
    int writeIndex = 0;
    float lfoPhase = 0.0f;

    // NE570 envelope detectors: the compressor rectifies the (pre-emphasised)
    // input, the two expanders each rectify their own BBD line's output.
    float compEnv = 0.0f;
    float expEnvA = 0.0f;
    float expEnvB = 0.0f;
    float compAttack = 1.0f, compRelease = 1.0f;
    float expAttack = 1.0f, expRelease = 1.0f;
};
