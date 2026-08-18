#pragma once

#include <JuceHeader.h>
#include "WDFCore.h"

// Klon-style "transparent" overdrive. The character comes from three things:
//  1. A pre-emphasis treble boost before the clipper (the Klon's distinctive
//     upper-mid/treble push, active even at Gain=0).
//  2. A genuine Wave Digital Filter simulation of the real clipping stage's
//     circuit (KlonClipper below) in place of a curve-fit asinh approximation
//     -- ported from and verified against jatinchowdhury18/KlonCentaur's own
//     traced-and-measured Klon Centaur model (ChowCentaur/GainStageProcessors/
//     ClippingStage.h): the actual C9/R13/C10/47k-bias network feeding a
//     matched diode pair (Is=15uA, Vt=25.85mV), solved via the closed-form
//     Wright Omega function rather than an approximated transfer curve.
//     Note: that traced circuit's diode pair is a single matched Is/Vt for
//     both diodes -- the popular "germanium + silicon asymmetric pair" story
//     about the real Klon Centaur isn't what the actual reverse-engineered
//     circuit (and this model) uses; this now reflects the traced circuit
//     rather than that story, which the previous asinh version was leaning on.
//  3. A clean/driven BLEND rather than a simple gain stage — Gain controls
//     how much clipped signal is mixed back in over the clean buffered
//     signal, which is what keeps it sounding "transparent" instead of
//     fuzzy even at higher settings.
// Oversamples just the nonlinear clip stage (mode selectable, default 2x):
// the treble pre-emphasis and clean/driven blend are linear operations (they
// don't generate new harmonic content), so only the clip itself needs the
// higher rate to keep the harmonics it generates from folding back as
// audible aliasing — AmpModule already does this the same way, including
// the same "keep 3 fully-prepared instances, pick one live" pattern for a
// hot-switchable mode (see PluginProcessor's klons array).
class KlonModule
{
public:
    // The Klon Centaur's actual clipping-stage circuit (per KlonCentaur's
    // traced schematic): input via C9/R13 into a node also fed by C10 to a
    // 47k bias resistor, clamped by a diode pair, current through C10 taken
    // as the stage's output. Self-referencing (each adaptor holds
    // references to its own sibling members) -- never copy or move an
    // instance once constructed, same restriction the reference
    // implementation carries.
    struct KlonClipper
    {
        WDF::ResistiveVoltageSource Vin { 1.0e-9f };
        WDF::Capacitor C9 { 1.0e-6f, 44100.0 };
        WDF::Resistor R13 { 1000.0f };
        WDF::PolarityInverter<WDF::ResistiveVoltageSource> I1 { Vin };
        WDF::Series<WDF::PolarityInverter<WDF::ResistiveVoltageSource>, WDF::Capacitor> S1 { I1, C9 };
        WDF::Series<decltype (S1), WDF::Resistor> S2 { S1, R13 };

        WDF::Capacitor C10 { 1.0e-6f, 44100.0 };
        WDF::ResistiveVoltageSource Vbias { 47000.0f };
        WDF::Series<WDF::Capacitor, WDF::ResistiveVoltageSource> S3 { C10, Vbias };

        WDF::Parallel<decltype (S2), decltype (S3)> P1 { S2, S3 };
        WDF::DiodePair<decltype (P1)> D23 { P1, 15.0e-6f, 0.02585f };

        KlonClipper() { Vbias.setVoltage (0.0f); }

        void prepare (double wdfSampleRate)
        {
            C9.prepare (1.0e-6f, wdfSampleRate);
            C10.prepare (1.0e-6f, wdfSampleRate);
            S1.calcImpedance();
            S2.calcImpedance();
            S3.calcImpedance();
            P1.calcImpedance();
            D23.calcImpedance();
            reset();
        }

        void reset()
        {
            C9.reset();
            C10.reset();
        }

        float processSample (float x) noexcept
        {
            Vin.setVoltage (x);
            D23.incident (P1.reflected());
            P1.incident (D23.reflected());
            return WDF::current (C10.wdf);
        }
    };

    // oversamplingMode: 0 = off (1x), 1 = 2x, 2 = 4x -- same convention as
    // AmpModule's own oversamplingMode.
    void prepare (const juce::dsp::ProcessSpec& spec, int oversamplingMode = 1)
    {
        sampleRate = spec.sampleRate;
        channelCount = juce::jlimit (1, 2, (int) spec.numChannels);
        for (auto& f : trebleFilter)
            f.prepare (spec);
        const auto stages = juce::jlimit (0, 2, oversamplingMode);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) channelCount, stages,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversampling->initProcessing (spec.maximumBlockSize);

        // The clipper runs at the oversampled rate (matches where it's
        // called in process() below).
        for (auto& c : clipper)
            c.prepare (sampleRate * (double) oversampling->getOversamplingFactor());

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
        for (auto& c : clipper)
            c.reset();
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
                // The WDF clipper reads/returns real circuit units (volts
                // in, amps of current through C10 out), not a pre-normalised
                // audio range -- outputCalibration is an empirical scalar
                // bringing that back to a sensible level. A wide tanh safety
                // rail backstops that calibration guess (the diode pair
                // itself already self-limits, same as real hardware, and a
                // standalone test sweeping drive x input amplitude confirmed
                // it does so gracefully -- the old asinh version's extra
                // driveGain-dependent divisor isn't needed on top of that
                // and would only make high-drive settings quieter than they
                // should be).
                auto clipped = clipper[ch].processSample (x) * outputCalibration;
                clipped = safetyCeiling * std::tanh (clipped / safetyCeiling);
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
    KlonClipper clipper[2];
    // Empirically-tuned, not physically derived -- the WDF stage's raw
    // output (current through C10, in amps) has no reason to already sit
    // in a sensible audio-sample range; this brings it there. Measured via
    // a standalone harness (same topology, swept drive/input amplitude):
    // raw output sits around 1-3 microamps. The original 150000 was chosen
    // to land around 0.5-ish at driveGain/input-amplitude extremes -- safe,
    // but read as "not enough gain" since it left most of the 3.0 safety
    // ceiling's headroom completely unused even fully cranked. Raised to
    // 650000, harness-confirmed to reach up to ~1.9-2.0 (genuinely hot,
    // clearly audible) at max Gain + loud input while staying proportionally
    // quieter at low Gain/quiet input -- Klon's own dry/wet blend (not this
    // constant) is still what keeps it reading as "transparent" rather than
    // fuzzy at moderate Gain settings.
    static constexpr float outputCalibration = 650000.0f;
    static constexpr float safetyCeiling = 3.0f;
    double sampleRate = 44100.0;
    int channelCount = 2;
    float gainAmount = 0.3f;
    float outputLevel = 1.0f;
    float lastTreble01 = -1.0f;
    bool enabled = false;
};
