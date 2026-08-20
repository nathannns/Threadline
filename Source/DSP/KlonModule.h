#pragma once

#include <JuceHeader.h>
#include "WDFCore.h"

// Klon Centaur overdrive — a full, faithful Wave Digital Filter port of the
// traced-and-measured gain stage from jatinchowdhury18/KlonCentaur
// (ChowCentaur/GainStageProcessors/{PreAmpStage, AmpStage, ClippingStage,
// FeedForward2, SummingAmp}.h|.cpp), not the hand-fit "treble shelf + drive
// gain + clean/driven blend" simplification that lived here before.
//
// The reference's GainStageProc is a five-block chain, reproduced here
// block-for-block (values, topology, scattering order, and the exact
// per-sample incident/reflected dance — not just the component values):
//
//   1. PreAmpWDF   — WDF pre-emphasis stage (C3/C5/C16 + R6/R7 + gain-driven
//                    Vbias). Its main output is voltage(Vbias)+voltage(R6);
//                    a side output current(Vbias2) is tapped as the "FF1"
//                    feed-forward path.
//   2. AmpStage    — a 2nd-order transposed-direct-form-II IIR (the C7/C8/
//                    R10b/R11/R12 network), R10b a gain-driven, smoothed
//                    resistance; op-amp rail clip at +/-4.5V.
//   3. ClippingWDF — the already-ported diode-pair clipper (see KlonClipper),
//                    run at 2x the base rate via oversampling.
//   4. FeedForward2WDF — a second WDF stage fed the *dry* input copy, its
//                    gain pots RVTop/RVBot moved by the Gain knob; output
//                    current(R16) is the "FF2" path.
//   5. SummingAmp  — a 1st-order IIR (R20/C13) that sums main + FF1 + FF2
//                    and clips at -13.1/+11.7V (the summing op-amp rails).
//
// Faithfulness notes (things that differ from the reference and why):
//   - One-ports are float, not the reference's double. The topology, values
//     and math are identical; float only introduces ~1e-7 relative noise,
//     far below audibility and below the validation harness's thresholds.
//   - Impedance changes (setGain) are propagated by manually re-running
//     calcImpedance() leaf-up on the affected adaptors, because our one-ports
//     carry no parent pointers. The final scattering coefficients are exactly
//     what the reference's parent-chain propagation computes.
//   - The reference's InputBuffer/OutputStage/ToneFilter/DC-blocker and final
//     inverting-amplifier stages are NOT ported here: those live outside the
//     gain stage in the reference. The klonTreble knob keeps driving a simple
//     post-gain 900Hz shelf (a stand-in for the reference's separate tone
//     filter), and klonLevel stays a plain output gain. Both are Threadline's
//     own controls, not part of this gain-stage port.
//   - The gain stage outputs raw circuit volts (~+/-13V rails), so a single
//     outputNormalization scalar brings that back to a sane audio level.

namespace KlonDsp
{
    // Bilinear transform helpers — exact port of chowdsp::Bilinear from the
    // pinned chowdsp_utils commit 4830738 (DSP/Filters/chowdsp_BilinearUtils.h).
    inline float calcPoleFreq (float a, float b, float c) noexcept
    {
        const auto radicand = b * b - 4.0f * a * c;
        if (radicand >= 0.0f)
            return 0.0f;
        return std::sqrt (-radicand) / (2.0f * a);
    }

    // First-order (N=2 arrays): SummingAmp.
    inline void bilinear1 (float (&b)[2], float (&a)[2], const float (&bs)[2], const float (&as)[2], float K) noexcept
    {
        const auto a0 = as[0] * K + as[1];
        b[0] = (bs[0] * K + bs[1]) / a0;
        b[1] = (-bs[0] * K + bs[1]) / a0;
        a[0] = 1.0f;
        a[1] = (-as[0] * K + as[1]) / a0;
    }

    // Second-order (N=3 arrays): AmpStage.
    inline void bilinear2 (float (&b)[3], float (&a)[3], const float (&bs)[3], const float (&as)[3], float K) noexcept
    {
        const auto KSq = K * K;
        const auto a0 = as[0] * KSq + as[1] * K + as[2];
        a[0] = 1.0f;
        a[1] = 2.0f * (as[2] - as[0] * KSq) / a0;
        a[2] = (as[0] * KSq - as[1] * K + as[2]) / a0;
        b[0] = (bs[0] * KSq + bs[1] * K + bs[2]) / a0;
        b[1] = 2.0f * (bs[2] - bs[0] * KSq) / a0;
        b[2] = (bs[0] * KSq - bs[1] * K + bs[2]) / a0;
    }

    // High-precision Wright omega (principal branch), used only where omega4
    // has its largest error (near the branch point, |x| <= 0.5 — the same
    // threshold the reference's CustomDiodePairT uses for its lookup table).
    // omega4's 3rd-order estimate is already within ~0.6% there, so a few
    // Halley steps on w + ln(w) = x converge to machine precision, matching
    // the exact wrightomega() the reference tables, without a transcendental
    // library. (f = w + ln w - x, f' = 1 + 1/w, f'' = -1/w^2.)
    inline float wrightOmegaExact (float x) noexcept
    {
        float w = WDF::wrightOmega (x);
        for (int i = 0; i < 4; ++i)
        {
            const float f = w + std::log (w) - x;
            const float fp = 1.0f + 1.0f / w;
            const float fpp = -1.0f / (w * w);
            w -= (2.0f * f * fp) / (2.0f * fp * fp - f * fpp);
        }
        return w;
    }
} // namespace KlonDsp

class KlonModule
{
public:
    // The Klon Centaur's clipping stage — the antiparallel diode pair after
    // the C9/R13/C10/47k-bias network. Faithful to ChowCentaur's ClippingWDF
    // + CustomDiodePairT (ClippingStage.h + DiodePair.h): the SCALAR diode
    // pair formula b = a + 2*lambda*(R_Is - Vt*W(log(R_Is/Vt) + lambda*a/Vt
    // + R_Is/Vt)), with the exact Wright omega substituted for omega4 where
    // |wrightIn| <= 0.5 (omega4's near-branch-point error is audible on quiet
    // signals — the reason the reference uses an LUT there). Note: the shared
    // WDF::DiodePair used by TS9/Bison/Growl/Fangs is chowdsp_wdf's SIMD
    // branch of this same equation (a difference of two omegas, no R_Is
    // offset) — a ~0.3% approximation we leave untouched for those modules;
    // the Klon clipper uses this exact scalar form.
    template <typename NextT>
    struct KlonDiodePair
    {
        NextT& next;
        float Is, Vt, oneOverVt;
        float R_Is = 0.0f, R_Is_overVt = 0.0f, logR_Is_overVt = 0.0f;
        float a = 0.0f, b = 0.0f;

        KlonDiodePair (NextT& n, float saturationCurrent, float thermalVoltage)
            : next (n), Is (saturationCurrent), Vt (thermalVoltage), oneOverVt (1.0f / thermalVoltage)
        {
            calcImpedance();
        }

        void calcImpedance()
        {
            R_Is = next.wdf.R * Is;
            R_Is_overVt = R_Is * oneOverVt;
            logR_Is_overVt = std::log (R_Is_overVt);
        }

        void incident (float x) noexcept { a = x; }

        float reflected() noexcept
        {
            const auto lambda = a < 0.0f ? -1.0f : 1.0f;
            const auto wrightIn = logR_Is_overVt + lambda * a * oneOverVt + R_Is_overVt;
            const auto w = std::abs (wrightIn) > 0.5f ? WDF::wrightOmega (wrightIn)
                                                      : KlonDsp::wrightOmegaExact (wrightIn);
            b = a + 2.0f * lambda * (R_Is - Vt * w);
            return b;
        }
    };

    // Self-referencing (each adaptor holds references to its own sibling
    // members) — never copy or move an instance once constructed, same
    // restriction the reference implementation carries.
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
        KlonDiodePair<decltype (P1)> D23 { P1, 15.0e-6f, 0.02585f };

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

    // PreAmpWDF — faithful port of ChowCentaur's PreAmpStage. Component values
    // and tree topology from PreAmpStage.h; the exact per-sample order (upward
    // scatter, read output, then the manual downward scatter through the ideal
    // voltage source) is what makes the read of voltage(Vbias)+voltage(R6)
    // land on the *previous* sample's incident waves, matching the reference.
    struct PreAmpWDF
    {
        WDF::Capacitor C3, C5, C16;
        WDF::Resistor R6 { 10000.0f };
        WDF::Resistor R7 { 1500.0f };
        WDF::ResistiveVoltageSource Vbias2 { 15000.0f };
        WDF::ResistiveVoltageSource Vbias; // default 1e-9; setGain moves it

        WDF::Parallel<WDF::Capacitor, WDF::Resistor> P1 { C5, R6 };
        WDF::Series<decltype (P1), WDF::ResistiveVoltageSource> S1 { P1, Vbias };
        WDF::Parallel<WDF::ResistiveVoltageSource, WDF::Capacitor> P2 { Vbias2, C16 };
        WDF::Series<decltype (P2), WDF::Resistor> S2 { P2, R7 };
        WDF::Parallel<decltype (S1), decltype (S2)> P3 { S1, S2 };
        WDF::Series<decltype (P3), WDF::Capacitor> S3 { P3, C3 };
        WDF::PolarityInverter<decltype (S3)> I1 { S3 };
        WDF::IdealVoltageSource Vin;

        void prepare (double sr)
        {
            C3.prepare (0.1e-6f, sr);
            C5.prepare (68.0e-9f, sr);
            C16.prepare (1.0e-6f, sr);
            P1.calcImpedance();
            S1.calcImpedance();
            P2.calcImpedance();
            S2.calcImpedance();
            P3.calcImpedance();
            S3.calcImpedance();
            reset();
        }

        void reset()
        {
            Vbias.setVoltage (0.0f);
            Vbias2.setVoltage (0.0f);
            C3.reset();
            C5.reset();
            C16.reset();
        }

        void setGain (float gain)
        {
            Vbias.setResistanceValue (gain * 100.0e3f);
            S1.calcImpedance();
            P3.calcImpedance();
            S3.calcImpedance();
        }

        float getFF1() noexcept { return WDF::current (Vbias2.wdf); }

        float processSample (float x) noexcept
        {
            Vin.setVoltage (x);
            Vin.incident (I1.reflected());
            const auto y = WDF::voltage (Vbias.wdf) + WDF::voltage (R6.wdf);
            I1.incident (Vin.reflected());
            return y;
        }
    };

    // AmpStage — the gain stage's 2nd-order IIR (C7/C8/R10b/R11/R12), ported
    // from ChowCentaur's AmpStage. R10b is gain-driven and multiplicatively
    // smoothed over 50ms (the reference's exact choice) to avoid zipper noise;
    // while smoothing, coefficients are recomputed every sample.
    struct AmpStage
    {
        float b[3] { 0.0f, 0.0f, 0.0f };
        float a[3] { 1.0f, 0.0f, 0.0f };
        float z[3] { 0.0f, 0.0f, 0.0f };
        static constexpr float R11 = 15.0e3f;
        static constexpr float R12 = 422.0e3f;
        float fs = 44100.0f;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> r10bSmooth;

        AmpStage() { r10bSmooth.setCurrentAndTargetValue (2000.0f); }

        void setGain (float gain)
        {
            const auto newR10b = (1.0f - gain) * 100000.0f + 2000.0f;
            r10bSmooth.setTargetValue (juce::jlimit (2000.0f, 102000.0f, newR10b));
        }

        void prepare (float sr)
        {
            reset();
            fs = sr;
            r10bSmooth.setCurrentAndTargetValue (r10bSmooth.getTargetValue());
            r10bSmooth.reset (sr, 0.05);
            calcCoefs (r10bSmooth.getTargetValue());
        }

        void reset() { std::fill (z, z + 3, 0.0f); }

        void calcCoefs (float curR10b)
        {
            constexpr float C7 = 82.0e-9f;
            constexpr float C8 = 390.0e-12f;

            float as[3], bs[3];
            as[0] = C7 * C8 * curR10b * R11 * R12;
            as[1] = C7 * curR10b * R11 + C8 * R12 * (curR10b + R11);
            as[2] = curR10b + R11;
            bs[0] = as[0];
            bs[1] = C7 * R11 * R12 + as[1];
            bs[2] = R12 + as[2];

            const auto wc = KlonDsp::calcPoleFreq (as[0], as[1], as[2]);
            const auto K = wc == 0.0f ? 2.0f * fs : wc / std::tan (wc / (2.0f * fs));
            KlonDsp::bilinear2 (b, a, bs, as, K);
        }

        float processSample (float x) noexcept
        {
            const auto y = z[1] + x * b[0];
            z[1] = z[2] + x * b[1] - y * a[1];
            z[2] = x * b[2] - y * a[2];
            return y;
        }

        void processBlock (float* block, int numSamples)
        {
            if (r10bSmooth.isSmoothing())
            {
                for (int n = 0; n < numSamples; ++n)
                {
                    calcCoefs (r10bSmooth.getNextValue());
                    block[n] = processSample (block[n]);
                }
            }
            else
            {
                for (int n = 0; n < numSamples; ++n)
                    block[n] = processSample (block[n]);
            }
        }
    };

    // FeedForward2WDF — faithful port of ChowCentaur's FeedForward2. Fed the
    // dry input copy; RVTop/RVBot are the two halves of the Gain pot. Note the
    // per-sample order differs from PreAmp: the downward scatter runs BEFORE
    // the read, so current(R16) uses this sample's incident wave.
    struct FeedForward2WDF
    {
        WDF::Resistor R5 { 5100.0f };
        WDF::Resistor R8 { 1500.0f };
        WDF::Resistor R9 { 1000.0f };
        WDF::Resistor RVTop { 50000.0f };
        WDF::Resistor RVBot { 50000.0f };
        WDF::Resistor R15 { 22000.0f };
        WDF::Resistor R16 { 47000.0f };
        WDF::Resistor R17 { 27000.0f };
        WDF::Resistor R18 { 12000.0f };
        WDF::ResistiveVoltageSource Vbias;
        WDF::Capacitor C4, C6, C11, C12;

        WDF::Series<WDF::Capacitor, WDF::Resistor> S1 { C12, R18 };
        WDF::Parallel<decltype (S1), WDF::Resistor> P1 { S1, R17 };
        WDF::Series<WDF::Capacitor, WDF::Resistor> S2 { C11, R15 };
        WDF::Series<decltype (S2), WDF::Resistor> S3 { S2, R16 };
        WDF::Parallel<decltype (S3), decltype (P1)> P2 { S3, P1 };
        WDF::Parallel<decltype (P2), WDF::Resistor> P3 { P2, RVBot };
        WDF::Series<decltype (P3), WDF::Resistor> S4 { P3, RVTop };
        WDF::Series<WDF::Capacitor, WDF::Resistor> S5 { C6, R9 };
        WDF::Parallel<decltype (S4), decltype (S5)> P4 { S4, S5 };
        WDF::Parallel<decltype (P4), WDF::Resistor> P5 { P4, R8 };
        WDF::Series<decltype (P5), WDF::ResistiveVoltageSource> S6 { P5, Vbias };
        WDF::Parallel<WDF::Resistor, WDF::Capacitor> P6 { R5, C4 };
        WDF::Series<decltype (P6), decltype (S6)> S7 { P6, S6 };
        WDF::PolarityInverter<decltype (S7)> I1 { S7 };
        WDF::IdealVoltageSource Vin;

        void prepare (double sr)
        {
            C4.prepare (68.0e-9f, sr);
            C6.prepare (390.0e-9f, sr);
            C11.prepare (2.2e-9f, sr);
            C12.prepare (27.0e-9f, sr);
            recomputeAll();
            reset();
        }

        void reset()
        {
            Vbias.setVoltage (0.0f);
            C4.reset();
            C6.reset();
            C11.reset();
            C12.reset();
        }

        void setGain (float gain)
        {
            RVTop.setResistanceValue (juce::jmax (gain * 100.0e3f, 1.0f));
            RVBot.setResistanceValue (juce::jmax ((1.0f - gain) * 100.0e3f, 1.0f));
            recomputeAll();
        }

        // Leaf-up recompute of every adaptor's scattering coefficients after
        // a resistance change (RVTop/RVBot) or a capacitor sample-rate change
        // (prepare). Equivalent to the reference's parent-chain propagation.
        void recomputeAll()
        {
            S1.calcImpedance();
            P1.calcImpedance();
            S2.calcImpedance();
            S3.calcImpedance();
            P2.calcImpedance();
            P3.calcImpedance();
            S4.calcImpedance();
            S5.calcImpedance();
            P4.calcImpedance();
            P5.calcImpedance();
            S6.calcImpedance();
            P6.calcImpedance();
            S7.calcImpedance();
        }

        float processSample (float x) noexcept
        {
            Vin.setVoltage (x);
            Vin.incident (I1.reflected());
            I1.incident (Vin.reflected());
            return WDF::current (R16.wdf);
        }
    };

    // SummingAmp — the gain stage's final 1st-order IIR (R20/C13), which sums
    // main + FF1 + FF2 and clips at the op-amp rails (-13.1/+11.7V). Ported
    // from ChowCentaur's SummingAmp.
    struct SummingAmp
    {
        float b[2] { 0.0f, 0.0f };
        float a[2] { 1.0f, 0.0f };
        float z[2] { 0.0f, 0.0f };
        static constexpr float R20 = 392.0e3f;
        static constexpr float C13 = 820.0e-12f;
        float fs = 44100.0f;

        void prepare (float sr)
        {
            reset();
            fs = sr;
            calcCoefs();
        }

        void reset() { std::fill (z, z + 2, 0.0f); }

        void calcCoefs()
        {
            float as[2], bs[2];
            bs[0] = 0.0f;
            bs[1] = R20;
            as[0] = C13 * R20;
            as[1] = 1.0f;
            const auto K = 2.0f * fs;
            KlonDsp::bilinear1 (b, a, bs, as, K);
        }

        float processSample (float x) noexcept
        {
            const auto y = z[1] + x * b[0];
            z[1] = x * b[1] - y * a[1];
            return y;
        }

        void processBlock (float* block, int numSamples)
        {
            for (int n = 0; n < numSamples; ++n)
                block[n] = processSample (block[n]);
        }
    };

    // oversamplingMode: 0 = off (1x), 1 = 2x, 2 = 4x — same convention as
    // AmpModule/TS9Module. The reference runs the clipper at 2x; mode 1 is
    // the faithful default.
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

        // Linear gain-stage blocks run at the base rate; only the clipper's
        // reactive elements run at the oversampled rate.
        for (auto& p : preAmp)
            p.prepare (sampleRate);
        for (auto& s : amp)
            s.prepare ((float) sampleRate);
        for (auto& f : ff2)
            f.prepare (sampleRate);
        for (auto& s : sumAmp)
            s.prepare ((float) sampleRate);
        for (auto& c : clipper)
            c.prepare (sampleRate * (double) oversampling->getOversamplingFactor());

        updateFilter();
        reset();
    }

    void reset()
    {
        for (auto& f : trebleFilter)
            f.reset();
        if (oversampling != nullptr)
            oversampling->reset();
        for (auto& p : preAmp)
            p.reset();
        for (auto& s : amp)
            s.reset();
        for (auto& f : ff2)
            f.reset();
        for (auto& s : sumAmp)
            s.reset();
        for (auto& c : clipper)
            c.reset();
    }

    int getLatencySamples() const noexcept
    {
        return oversampling != nullptr ? juce::roundToInt (oversampling->getLatencyInSamples()) : 0;
    }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    // gain/treble/level: 0-1. gain drives the reference's triple-ganged Gain
    // control (PreAmp Vbias, AmpStage R10b, FeedForward2 RVTop/RVBot);
    // treble is Threadline's post-gain shelf; level is a plain output gain.
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

        ff1Buffer.setSize (numChannels, numSamples, false, false, true);
        ff2Buffer.setSize (numChannels, numSamples, false, false, true);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* x = buffer.getWritePointer (ch);
            auto* x1 = ff1Buffer.getWritePointer (ch);
            auto* x2 = ff2Buffer.getWritePointer (ch);

            // Reference input scaling (processInternalBuffer: x *= 0.5), and
            // the dry copy the FeedForward2 side-chain is fed.
            juce::FloatVectorOperations::multiply (x, 0.5f, numSamples);
            juce::FloatVectorOperations::copy (x2, x, numSamples);

            // PreAmpWDF -> main path + FF1 side output.
            preAmp[ch].setGain (gainAmount);
            for (int n = 0; n < numSamples; ++n)
            {
                x[n] = preAmp[ch].processSample (x[n]);
                x1[n] = preAmp[ch].getFF1();
            }

            // AmpStage -> op-amp rail clip.
            amp[ch].setGain (gainAmount);
            amp[ch].processBlock (x, numSamples);
            juce::FloatVectorOperations::clip (x, x, -4.5f, 4.5f, numSamples);
        }

        // Only the nonlinear clipper runs at the oversampled rate.
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numChannels, 0, (size_t) numSamples);
        auto osBlock = oversampling->processSamplesUp (block);
        const auto osSamples = (int) osBlock.getNumSamples();
        for (size_t ch = 0; ch < osBlock.getNumChannels(); ++ch)
        {
            auto* x = osBlock.getChannelPointer ((size_t) ch);
            for (int n = 0; n < osSamples; ++n)
                x[n] = clipper[ch].processSample (x[n]);
        }
        oversampling->processSamplesDown (block);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* x = buffer.getWritePointer (ch);
            auto* x1 = ff1Buffer.getWritePointer (ch);
            auto* x2 = ff2Buffer.getWritePointer (ch);

            // FeedForward2 on the dry copy.
            ff2[ch].setGain (gainAmount);
            for (int n = 0; n < numSamples; ++n)
                x2[n] = ff2[ch].processSample (x2[n]);

            // Summing amp: main + FF1 + FF2, then the op-amp rail clip.
            juce::FloatVectorOperations::add (x, x1, numSamples);
            juce::FloatVectorOperations::add (x, x2, numSamples);
            sumAmp[ch].processBlock (x, numSamples);
            juce::FloatVectorOperations::clip (x, x, -13.1f, 11.7f, numSamples);
        }

        // Post-gain: raw-volt -> audio normalization, treble shelf, level.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* x = buffer.getWritePointer (ch);
            for (int n = 0; n < numSamples; ++n)
            {
                auto y = x[n] * outputNormalization;
                y = trebleFilter[ch].processSample (y);
                x[n] = y * outputLevel;
            }
        }
    }

private:
    void updateFilter()
    {
        // Post-gain treble shelf (stand-in for the reference's separate tone
        // filter); sweeps from mild to pronounced.
        const auto gainDb = juce::jmap (lastTreble01, 0.0f, 1.0f, 1.5f, 9.0f);
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRate, 900.0f, 0.6f, juce::Decibels::decibelsToGain (gainDb));
        for (auto& f : trebleFilter)
            *f.coefficients = *coeffs;
    }

    juce::dsp::IIR::Filter<float> trebleFilter[2];
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> ff1Buffer;
    juce::AudioBuffer<float> ff2Buffer;
    KlonClipper clipper[2];
    PreAmpWDF preAmp[2];
    AmpStage amp[2];
    FeedForward2WDF ff2[2];
    SummingAmp sumAmp[2];
    // Empirical scalar bringing the gain stage's raw circuit-volt output
    // (clipped at -13.1/+11.7 V) down to audio float units. The gain stage
    // itself has ~15 dB of fixed gain even with the Gain knob fully off, so a
    // rails-only normalisation (1/12 -> +/-1.0) leaves the clean boost ~-11 dB
    // -- the pedal would sound brokenly quiet in a chain. Calibrated instead
    // via KlonValidation so that gain=0, level=0.5 passes ~unity (the Klon's
    // documented "transparent clean boost"): 0.3 * 12 * (measured 0.28 at
    // 1/12) ~= 1.0. This is a Threadline level-staging choice, not part of the
    // faithful gain-stage port (the reference leaves raw circuit volts and
    // hands final volume to its own level control + DAW gain staging); kept as
    // a plain constant rather than pretending it is derived from the schematic.
    static constexpr float outputNormalization = 0.3f;
    double sampleRate = 44100.0;
    int channelCount = 2;
    float gainAmount = 0.3f;
    float outputLevel = 1.0f;
    float lastTreble01 = -1.0f;
    bool enabled = false;
};
