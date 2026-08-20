#pragma once

#include <JuceHeader.h>
#include "WDFCore.h"

// "Breaker" overdrive — one faithful Tube Screamer circuit, offered with
// three selectable "voicings" named after the TS9 / TS808 / TS10 family.
//
// The clipping stage is a genuine Wave Digital Filter simulation of the real
// op-amp clipping stage, ported from and verified against Chowdhury-DSP/
// BYOD's own Tube Screamer model (src/processors/drive/tube_screamer/
// TubeScreamerWDF.h): the classic non-inverting op-amp with diode feedback
// (input R5=10k, feedback R6=51k plus up to 500k from the Drive pot, real
// 1N4148 diode-pair parameters Is=4.352nA / Vt=25.85mV * 1.906 ideality),
// solved via the closed-form Wright Omega function KlonModule's clipper
// uses. The op-amp's finite gain (Ag=100), input impedance (Ri=1e9) and
// output impedance (Ro=0.1) are modeled with a full R-type multi-port
// adaptor (WDF::RTypeAdaptor) whose 4x4 scattering matrix is BYOD's R-Solver
// output, ported character-for-character. This carries the two feedback-
// network elements a simpler ideal-op-amp model cannot represent: C4 (51pF,
// across the feedback resistor) and the R4+C3 (4.7k + 47nF) leg to ground —
// the latter being the real Tube Screamer's mid-hump (~720Hz). No separate
// pre-clip highpass is needed any more: the mid-hump lives inside the
// circuit itself, exactly where the real one does.
//
// A post-clip Tone control is a genuine treble-cut lowpass sweep (what the
// real pedal's Tone pot is) — turning Tone down darkens it, up brightens it.
//
// setVoicing() selects one of three voicings. IMPORTANT: the underlying
// CLIPPING CIRCUIT IS SCHEMATICALLY IDENTICAL across all three (same op-amp,
// feedback network, R4+C3 mid-hump, diode pair and tone network — see the
// real Ibanez schematics, ElectroSmash's Tube Screamer analysis, or Geofex's
// "Technology of the Tube Screamer"). The voicing layer below therefore
// models only the *subjective* character each name is known for, not a
// circuit difference — and it is deliberately small, kept far from the
// mid-hump band, and documented as folklore:
//   - TS9:  the mid-forward "honky" baseline — no extra voicing.
//   - TS808: a touch warmer/rounder — a gentle +2dB low-shelf lift below
//     250Hz, applied *before* the clipper (more bass reaches the diodes).
//   - TS10: more low end plus the family's widely-noted wider Tone range —
//     a +4dB pre-clip low shelf and a broader tone sweep (900Hz..13.5kHz
//     vs the 1.2k..11k baseline).
// Like KlonModule, oversamples just the nonlinear clip stage (mode
// selectable, default 2x) — the voicing and tone filters are linear and only
// the clip itself generates the harmonics that need the higher rate. Unlike
// Klon there is no dry/wet blend (it's fully wet), so no parallel dry path
// to keep aligned with the oversampler's added latency.
class TS9Module
{
public:
    enum class Voicing { TS9, TS808, TS10 };

    // The real TS9 clipping stage, ported verbatim from Chowdhury-DSP/BYOD's
    // TubeScreamerWDF.h -- the non-inverting op-amp with diode feedback, with
    // the op-amp's FINITE gain/impedance modeled via a full R-type multi-port
    // adaptor (WDF::RTypeAdaptor) instead of the ideal-op-amp limit. Topology
    // and element values match BYOD exactly:
    //   - Port B (input): 1uF coupling cap (voltage source) || 10k (R5)
    //   - Port C (feedback to ground): 4.7k + 47nF (R4 + C3, the mid-hump)
    //   - Port D (output): 1M load
    //   - Port A (feedback): (51k + Drive pot 500k) || 51pF, plus the diode
    //     pair as the root -- Drive moves the feedback resistance directly.
    struct TS9Clipper
    {
        // Port C
        WDF::ResistorCapacitorSeries R4_ser_C3 { 4.7e3f, 0.047e-6f, 44100.0 };
        // Port D
        WDF::Resistor RL { 1.0e6f };
        // Port B
        WDF::CapacitiveVoltageSource Vin_C2 { 1.0e-6f, 44100.0 };
        WDF::Resistor R5 { 10.0e3f };
        WDF::Parallel<WDF::CapacitiveVoltageSource, WDF::Resistor> P1 { Vin_C2, R5 };
        // The op-amp itself: finite-gain R-type adaptor over ports B/C/D.
        WDF::RTypeAdaptor<decltype (P1), decltype (R4_ser_C3), decltype (RL)> R { P1, R4_ser_C3, RL };
        // Port A
        WDF::ResistorCapacitorParallel R6_P1_par_C4 { 51.0e3f, 51.0e-12f, 44100.0 };
        WDF::Parallel<WDF::ResistorCapacitorParallel, decltype (R)> P3 { R6_P1_par_C4, R };
        // Root: the antiparallel 1N4148 diode pair (Is=4.352nA, Vt=25.85mV
        // folded with nDiodes=1.906 ideality, exactly as BYOD passes it).
        WDF::DiodePair<decltype (P3)> dp { P3, 4.352e-9f, 0.02585f * 1.906f };

        void prepare (double clipSampleRate)
        {
            R4_ser_C3.prepare (clipSampleRate);
            Vin_C2.prepare (clipSampleRate);
            R6_P1_par_C4.prepare (clipSampleRate);
            // Recompute the impedance chain top-down now the one-ports' R
            // values are finalised at this rate.
            R.calcImpedance();
            P3.calcImpedance();
            dp.calcImpedance();
        }

        void setFeedbackResistance (float ohms) noexcept
        {
            R6_P1_par_C4.setResistanceValue (ohms);
            P3.calcImpedance();
            dp.calcImpedance();
        }

        void reset() noexcept
        {
            R4_ser_C3.reset();
            Vin_C2.reset();
            R6_P1_par_C4.reset();
            P1.wdf.a = P1.wdf.b = 0.0f;
            R.wdf.a = R.wdf.b = 0.0f;
            for (auto& x : R.avec)
                x = 0.0f;
            P3.wdf.a = P3.wdf.b = 0.0f;
            dp.a = dp.b = 0.0f;
        }

        float processSample (float vin) noexcept
        {
            Vin_C2.setVoltage (vin);
            dp.incident (P3.reflected());
            P3.incident (dp.reflected());
            return WDF::voltage (RL.wdf);
        }
    };

    void setVoicing (Voicing newVoicing)
    {
        if (newVoicing == voicing)
            return;
        voicing = newVoicing;
        updateVoicing();
    }
    // oversamplingMode: 0 = off (1x), 1 = 2x, 2 = 4x -- same convention as
    // AmpModule's own oversamplingMode.
    void prepare (const juce::dsp::ProcessSpec& spec, int oversamplingMode = 1)
    {
        sampleRate = spec.sampleRate;
        channelCount = juce::jlimit (1, 2, (int) spec.numChannels);
        for (auto& f : voicingBass)
            f.prepare (spec);
        for (auto& f : toneFilter)
            f.prepare (spec);
        const auto stages = juce::jlimit (0, 2, oversamplingMode);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) channelCount, stages,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversampling->initProcessing (spec.maximumBlockSize);
        // The clipper's reactive elements (caps) and the R-type adaptor's
        // scattering matrix depend on the sample rate, so prepare them at
        // the OVERsampled rate the clip stage actually runs at.
        const auto clipRate = sampleRate * (double) (1 << stages);
        for (auto& c : clipper)
            c.prepare (clipRate);
        updateVoicing();
        reset();
    }

    void reset()
    {
        for (auto& f : voicingBass)
            f.reset();
        for (auto& f : toneFilter)
            f.reset();
        if (oversampling != nullptr)
            oversampling->reset();
        for (auto& c : clipper)
            c.reset();
    }

    int getLatencySamples() const noexcept
    {
        return oversampling != nullptr ? juce::roundToInt (oversampling->getLatencyInSamples()) : 0;
    }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    // drive/tone/level: 0-1. tone now sweeps a treble-cut lowpass (dark<->bright).
    void setParameters (float drive01, float tone01, float level01)
    {
        driveAmount = juce::jlimit (0.0f, 1.0f, drive01);
        outputLevel = juce::jlimit (0.0f, 2.0f, level01 * 2.0f);
        if (! juce::approximatelyEqual (tone01, lastTone01))
        {
            lastTone01 = tone01;
            updateToneFilter();
        }
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled || oversampling == nullptr)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), channelCount);
        const auto numSamples = buffer.getNumSamples();

        // Real Drive-pot range: 51k fixed (R6) plus 0-500k (Pot1) -- the
        // knob directly sets the feedback resistor rather than pre-scaling
        // the input signal into a fixed clipper.
        const auto feedbackOhms = 51000.0f + driveAmount * 500000.0f;
        for (auto& c : clipper)
            c.setFeedbackResistance (feedbackOhms);

        // Voicing bass shelf, pre-clip, in place -- skipped entirely for the
        // TS9 baseline (0dB) so it costs nothing when the voicing is off.
        if (bassActive)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                for (int i = 0; i < numSamples; ++i)
                    d[i] = voicingBass[ch].processSample (d[i]);
            }

        // Only the nonlinear clip runs at the oversampled rate. The buffer is
        // fed straight into the oversampler (no scratch copy): processSamplesUp
        // reads it, processSamplesDown writes the clipped result back.
        juce::dsp::AudioBlock<float> block (buffer);
        block = block.getSubsetChannelBlock (0, (size_t) numChannels);
        auto osBlock = oversampling->processSamplesUp (block);
        const auto osSamples = (int) osBlock.getNumSamples();
        for (size_t ch = 0; ch < osBlock.getNumChannels(); ++ch)
        {
            for (int i = 0; i < osSamples; ++i)
            {
                const auto x = osBlock.getSample ((int) ch, i);
                // Real op-amp clipper output (volts) -- outputCalibration
                // brings that to a sensible audio range. A wide tanh safety
                // rail backstops the diode pair (which already self-limits,
                // same as real hardware -- this is only a numeric safety net).
                auto clipped = clipper[ch].processSample (x) * outputCalibration;
                clipped = safetyCeiling * std::tanh (clipped / safetyCeiling);
                osBlock.setSample ((int) ch, i, clipped);
            }
        }
        oversampling->processSamplesDown (block);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* out = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                out[i] = toneFilter[ch].processSample (out[i]) * outputLevel;
        }
    }

private:
    void updateVoicing()
    {
        // Subjective voicing, NOT a schematic difference (see class comment).
        // +2dB (TS808) / +4dB (TS10) of gentle low-end lift ahead of the
        // clipper; 0dB (TS9) bypasses the filter entirely.
        const auto bassDb = voicing == Voicing::TS808 ? 2.0f
                          : voicing == Voicing::TS10  ? 4.0f
                                                      : 0.0f;
        bassActive = bassDb > 0.1f;
        if (bassActive)
        {
            auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
                sampleRate, 250.0f, 0.707f, juce::Decibels::decibelsToGain (bassDb));
            for (auto& f : voicingBass)
                *f.coefficients = *coeffs;
        }
        updateToneFilter();
    }

    void updateToneFilter()
    {
        // Treble-cut sweep. TS10's Tone is consistently described (again in
        // folklore, not the schematic) as noticeably wider than TS9/808's.
        const auto darkHz = voicing == Voicing::TS10 ? 900.0f : 1200.0f;
        const auto brightHz = voicing == Voicing::TS10 ? 13500.0f : 11000.0f;
        const auto cutoff = juce::jmap (lastTone01, 0.0f, 1.0f, darkHz, brightHz);
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, cutoff, 0.707f);
        for (auto& f : toneFilter)
            *f.coefficients = *coeffs;
    }

    juce::dsp::IIR::Filter<float> voicingBass[2], toneFilter[2];
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    TS9Clipper clipper[2];
    bool bassActive = false;
    // Empirically-tuned, not physically derived -- see KlonModule's own
    // outputCalibration for why. Retuned for the finite-gain (non-inverting)
    // port: the TS9Validation harness shows this topology's diode clamp caps
    // the raw mid-band output around ~1.0V (vs ~0.5V for the old ideal-op-
    // amp/inverting model), so the old 4.5 multiplier pushed the tanh safety
    // rail into hard limiting. 2.25 maps ~1V raw -> 2.25 pre-tanh -> ~1.9
    // audio units, the same mid level the old 4.5 produced, while the real
    // mid-hump inside the clipper leaves the bass a touch lower (correct for
    // a real Tube Screamer). The tanh rail stays a backstop, not the limiter.
    static constexpr float outputCalibration = 2.25f;
    static constexpr float safetyCeiling = 3.0f;
    double sampleRate = 44100.0;
    int channelCount = 2;
    float driveAmount = 0.3f;
    float outputLevel = 1.0f;
    float lastTone01 = -1.0f;
    bool enabled = false;
    Voicing voicing = Voicing::TS9;
};
