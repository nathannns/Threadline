#pragma once

#include <JuceHeader.h>

// Dynamic, oversampled 5E3-inspired model. Gain and memory are distributed
// through the circuit's major functional stages instead of one static
// clipper: input/interstage coupling caps, a two-stage 12AY7 preamp with
// grid-bias-shift memory (blocking distortion), a Tone/Bassman-stack network
// sitting between the two preamp stages (matching the real 5E3's V1 -> Tone
// -> V2A signal order, confirmed against Rob Robinette's own annotated 5E3
// schematic: shared 1M tone pot, 0.005uF tone cap, feeding V2A's grid — not
// after both preamp stages), a cathodyne-style phase inverter, a genuinely
// differential push-pull power stage (two tubes driven by +V/-V from the
// cathodyne and subtracted at the output transformer, the actual mechanism
// that cancels even-order harmonics for a matched pair) with a bass-weighted
// sag detector, and output-transformer core saturation distinct from sag —
// per Rob Robinette's 5E3 circuit writeup and annotated schematic
// (robrobinette.com).

class AmpModule
{
public:
    // Vintage5E3 is the single-voice tweed circuit described above (one
    // passive-feeling Tone knob, per 5E3 authenticity). Modern3Band swaps
    // that single tone filter for a full Bass/Mid/Treble stack — not three
    // independent EQ bands, but the real passive Fender/Marshall tone-stack
    // network (see BassmanToneStack below), placed at the exact same point
    // in the signal chain. Everything upstream and downstream (preamp gain
    // stages, phase inverter, sag, output-transformer saturation) is
    // unchanged, so Modern3Band is the same amp "engine" wearing a
    // different, more flexible — but still physically-derived — tone
    // section rather than a different circuit.
    enum class Voice { vintage5E3 = 0, modern3Band = 1 };

    void prepare (const juce::dsp::ProcessSpec& spec, int oversamplingMode = 2)
    {
        baseSampleRate = spec.sampleRate;
        channelCount = juce::jlimit (1, 2, static_cast<int> (spec.numChannels));
        const auto stages = juce::jlimit (0, 2, oversamplingMode);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            static_cast<size_t> (channelCount), static_cast<size_t> (stages),
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversampling->initProcessing (spec.maximumBlockSize);
        const auto oversamplingFactor = oversampling->getOversamplingFactor();
        processingSampleRate = baseSampleRate * static_cast<double> (oversamplingFactor);

        const juce::dsp::ProcessSpec osSpec { processingSampleRate,
                                             static_cast<juce::uint32> (spec.maximumBlockSize * oversamplingFactor),
                                             static_cast<juce::uint32> (channelCount) };
        for (int ch = 0; ch < 2; ++ch)
        {
            inputCoupling[ch].prepare (osSpec);
            interstageCoupling[ch].prepare (osSpec);
            toneFilter[ch].prepare (osSpec);
            transformerLowPass[ch].prepare (osSpec);
        }
        sagAttack = std::exp (-1.0f / static_cast<float> (processingSampleRate * 0.045));
        sagRelease = std::exp (-1.0f / static_cast<float> (processingSampleRate * 0.240));
        // One-pole ~180Hz tap feeding the sag detector — bass notes draw far
        // more current than a bright single-note lead at the same peak level,
        // so weighting the detector toward low frequency content (rather than
        // full-band peak) is what actually makes sustained low chords "bloom"
        // and sag while a treble lead stays comparatively tight.
        sagDetectorLPCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 180.0f
            / static_cast<float> (processingSampleRate));
        outputGain.reset (baseSampleRate, 0.025);
        updateStaticFilters();
        updateToneFilter();
        bassmanStack.updateCoefficients (processingSampleRate, lastBass01, lastMid01, lastTreble01);
        reset();
    }

    void reset()
    {
        if (oversampling != nullptr)
            oversampling->reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            inputCoupling[ch].reset(); interstageCoupling[ch].reset();
            toneFilter[ch].reset(); transformerLowPass[ch].reset();
        }
        bassmanStack.reset();
        sagEnvelope = 0.0f;
        biasMemory.fill (0.0f);
        sagDetectorLP.fill (0.0f);
        outputGain.setCurrentAndTargetValue (targetOutputGain);
    }

    void setParameters (float drive01, float tone01, float outputDb, Voice voiceIn,
                        float bass01, float mid01, float treble01)
    {
        driveAmount = juce::jlimit (0.0f, 1.0f, drive01);
        targetOutputGain = juce::Decibels::decibelsToGain (outputDb);
        outputGain.setTargetValue (targetOutputGain);
        voice = voiceIn;
        tone01 = juce::jlimit (0.0f, 1.0f, tone01);
        if (! juce::approximatelyEqual (tone01, lastTone01))
        {
            lastTone01 = tone01;
            updateToneFilter();
        }
        bass01 = juce::jlimit (0.0f, 1.0f, bass01);
        mid01 = juce::jlimit (0.0f, 1.0f, mid01);
        treble01 = juce::jlimit (0.0f, 1.0f, treble01);
        if (! juce::approximatelyEqual (bass01, lastBass01)
            || ! juce::approximatelyEqual (mid01, lastMid01)
            || ! juce::approximatelyEqual (treble01, lastTreble01))
        {
            lastBass01 = bass01; lastMid01 = mid01; lastTreble01 = treble01;
            bassmanStack.updateCoefficients (processingSampleRate, lastBass01, lastMid01, lastTreble01);
        }
    }

    int getLatencySamples() const noexcept
    {
        return oversampling != nullptr ? juce::roundToInt (oversampling->getLatencyInSamples()) : 0;
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (oversampling == nullptr || buffer.getNumSamples() == 0)
            return;

        auto inputBlock = juce::dsp::AudioBlock<float> (buffer)
                              .getSubsetChannelBlock (0, static_cast<size_t> (channelCount));
        auto block = oversampling->processSamplesUp (inputBlock);
        const auto samples = static_cast<int> (block.getNumSamples());
        const auto channels = static_cast<int> (block.getNumChannels());
        const auto stage1Gain = 1.15f + driveAmount * 4.8f;
        const auto stage2Gain = 1.05f + driveAmount * 3.4f;
        const auto powerDrive = 1.15f + driveAmount * 2.7f;

        for (int i = 0; i < samples; ++i)
        {
            float detector = 0.0f;
            std::array<float, 2> phaseInverterOut {};
            for (int ch = 0; ch < channels; ++ch)
            {
                auto x = inputCoupling[ch].processSample (block.getSample (ch, i));
                constexpr float bias1 = 0.16f;
                auto triode1 = std::tanh (x * stage1Gain + bias1) - std::tanh (bias1);
                triode1 = interstageCoupling[ch].processSample (triode1);
                biasMemory[(size_t) ch] += 0.00055f * (triode1 - biasMemory[(size_t) ch]);
                auto stage2Input = triode1 - 0.13f * biasMemory[(size_t) ch];

                // The real 5E3's Tone control sits between the two preamp
                // gain stages -- V1 (this plugin's stage 1) feeds the shared
                // Tone pot/cap network, which then drives V2A's grid (stage
                // 2), not after both stages as this used to model it (per
                // the annotated schematic: V1 -> Tone -> V2A -> V2B phase
                // inverter). That matters for more than bookkeeping: stage 2
                // now clips whatever harmonic content Tone left behind
                // rather than clipping the full-bandwidth signal and only
                // filtering the result, so Tone changes what stage 2 has to
                // work with, not just the final brightness.
                float toned;
                if (voice == Voice::vintage5E3)
                    toned = toneFilter[ch].processSample (stage2Input);
                else
                    toned = bassmanStack.processSample (ch, stage2Input);

                constexpr float bias2 = -0.10f;
                auto triode2 = std::tanh (toned * stage2Gain + bias2) - std::tanh (bias2);
                auto cathodyne = triode2 >= 0.0f ? std::tanh (triode2 * 1.18f)
                                                 : 0.94f * std::tanh (triode2 * 1.32f);
                phaseInverterOut[(size_t) ch] = cathodyne;

                auto& bassTap = sagDetectorLP[(size_t) ch];
                bassTap += sagDetectorLPCoefficient * (cathodyne - bassTap);
                const auto weighted = std::abs (cathodyne) * 0.5f + std::abs (bassTap) * 0.5f;
                detector = juce::jmax (detector, weighted);
            }

            const auto coefficient = detector > sagEnvelope ? sagAttack : sagRelease;
            sagEnvelope = coefficient * sagEnvelope + (1.0f - coefficient) * detector;
            const auto sag = juce::jlimit (0.0f, 0.42f,
                sagEnvelope * (0.20f + 0.34f * driveAmount));
            for (int ch = 0; ch < channels; ++ch)
            {
                // Real push-pull drives two tubes with opposite-polarity
                // signals from the cathodyne (+V and -V), and the output
                // transformer's opposite winding subtracts their outputs.
                // For a matched pair sharing one nonlinearity f, decomposing
                // f = f_odd + f_even gives f(V) - f(-V) = 2*f_odd(V): the
                // even-order content cancels exactly, leaving only odd-order
                // content doubled -- the textbook reason push-pull reads
                // different from a single-ended stage. tubeMismatch (real
                // 6V6 pairs are never perfectly matched) is applied as the
                // *same*-direction bias to both tubes rather than mirrored,
                // which breaks that cancellation just enough to leave a
                // little real even-order content rather than a
                // mathematically perfect one -- "asymmetric push-pull".
                const auto effectiveDrive = powerDrive * (1.0f - sag);
                const auto v = phaseInverterOut[(size_t) ch] * effectiveDrive;
                constexpr float tubeMismatch = 0.035f;
                const auto tubeA = std::tanh (v + tubeMismatch);
                const auto tubeB = std::tanh (-v + tubeMismatch);
                auto power = (tubeA - tubeB) * 0.5f;
                power /= juce::jmax (0.8f, 0.72f + effectiveDrive * 0.28f);

                // Output-transformer core saturation — a second, distinct
                // compression mechanism from tube sag above (Robinette: "at
                // high volumes, transformer saturation compresses the
                // signal...loud notes are capped but softer notes are still
                // amplified"). Sag is a slow, envelope-driven gain reduction
                // of the drive; this is an instantaneous, level-dependent
                // soft-knee on the transformer's own output, so a hard
                // transient still gets capped even before sag has caught up.
                constexpr float otKnee = 0.65f;
                const auto otMagnitude = std::abs (power);
                if (otMagnitude > otKnee)
                {
                    const auto excess = otMagnitude - otKnee;
                    const auto headroom = 1.0f - otKnee;
                    const auto compressed = otKnee + headroom * std::tanh (excess / headroom);
                    power = std::copysign (compressed, power);
                }

                block.setSample (ch, i, transformerLowPass[ch].processSample (power));
            }
        }

        oversampling->processSamplesDown (inputBlock);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto gain = outputGain.getNextValue();
            for (int ch = 0; ch < channelCount; ++ch)
                buffer.setSample (ch, i, buffer.getSample (ch, i) * gain);
        }
    }

private:
    void updateStaticFilters()
    {
        auto inputHP = juce::dsp::IIR::Coefficients<float>::makeHighPass (processingSampleRate, 48.0f, 0.707f);
        auto couplingHP = juce::dsp::IIR::Coefficients<float>::makeHighPass (processingSampleRate, 72.0f, 0.707f);
        auto transformerLP = juce::dsp::IIR::Coefficients<float>::makeLowPass (processingSampleRate, 7600.0f, 0.72f);
        for (int ch = 0; ch < 2; ++ch)
        {
            *inputCoupling[ch].coefficients = *inputHP;
            *interstageCoupling[ch].coefficients = *couplingHP;
            *transformerLowPass[ch].coefficients = *transformerLP;
        }
    }

    void updateToneFilter()
    {
        if (processingSampleRate <= 0.0)
            return;
        const auto cutoff = 950.0f * std::pow (15.8f, lastTone01);
        auto coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (
            processingSampleRate,
            juce::jmin (cutoff, static_cast<float> (processingSampleRate * 0.42)), 0.62f);
        for (auto& filter : toneFilter)
            *filter.coefficients = *coefficients;
    }

    // Closed-form emulation of the real passive Fender '59 Bassman tone
    // stack (Bass/Mid/Treble pots + R1-R4/C1-C3 RC network) for
    // Voice::modern3Band — not three independent EQ bands, but the actual
    // circuit's third-order transfer function, so the controls interact the
    // way the physical stack does (e.g. raising Mid measurably pulls down
    // apparent Bass and Treble, the classic "scooped mids" behaviour).
    //
    // Component values and the symbolic H(s) = (b1*s + b2*s^2 + b3*s^3) /
    // (a0 + a1*s + a2*s^2 + a3*s^3) derivation, plus its exact bilinear-
    // transform discretization (c = 2*fs, matching the paper's own choice
    // for accuracy near DC through the audio band), are from:
    //   D.T. Yeh, J.O. Smith, "Discretization of the '59 Fender Bassman
    //   Tone Stack," Proc. DAFx-06, Montreal
    //   (https://ccrma.stanford.edu/~dtyeh/papers/yeh06_dafx.pdf).
    // t/m/l below are that paper's Treble/Middle/Bass control names (each
    // 0-1, matching our knob range directly — no taper conversion needed
    // since these are already linear 0-1 digital parameters).
    struct BassmanToneStack
    {
        void reset() { state.fill (ChannelState {}); }

        void updateCoefficients (double sampleRate, double bass01, double mid01, double treble01)
        {
            if (sampleRate <= 0.0)
                return;
            auto l = juce::jlimit (0.0, 1.0, bass01);
            const auto m = juce::jlimit (0.0, 1.0, mid01);
            const auto t = juce::jlimit (0.0, 1.0, treble01);
            // Denominator coefficients depend only on Bass/Mid (per the
            // paper — Treble only repositions zeros). At the single exact
            // corner Bass=0, Mid=1, the analog cubic's a3 term cancels to
            // zero identically (verified algebraically, independent of
            // component values — a genuine order-reduction of the physical
            // network at that corner, not a rounding artifact), which the
            // bilinear transform maps to a discrete pole sitting exactly on
            // the unit circle (marginal/undamped). Nudging Bass a hair off
            // zero only in that corner's neighbourhood keeps every pole
            // strictly inside the unit circle everywhere else in the full
            // knob range untouched.
            if (l < 1.0e-3 && m > 1.0 - 1.0e-3)
                l = 1.0e-3;

            // '59 Bassman component values, per the paper's Fig. 1.
            constexpr double C1 = 0.25e-9, C2 = 20e-9, C3 = 20e-9;
            constexpr double R1 = 250000.0, R2 = 1000000.0, R3 = 25000.0, R4 = 56000.0;

            const auto b1 = t*C1*R1 + m*C3*R3 + l*(C1*R2 + C2*R2) + (C1*R3 + C2*R3);
            const auto b2 = t*(C1*C2*R1*R4 + C1*C3*R1*R4) - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                           + m*(C1*C3*R1*R3 + C1*C3*R3*R3 + C2*C3*R3*R3)
                           + l*(C1*C2*R1*R2 + C1*C2*R2*R4 + C1*C3*R2*R4)
                           + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                           + (C1*C2*R1*R3 + C1*C2*R3*R4 + C1*C3*R3*R4);
            const auto b3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                           - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                           + m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                           + t*C1*C2*C3*R1*R3*R4 - t*m*C1*C2*C3*R1*R3*R4
                           + t*l*C1*C2*C3*R1*R2*R4;

            constexpr double a0 = 1.0;
            const auto a1 = (C1*R1 + C1*R3 + C2*R3 + C2*R4 + C3*R4) + m*C3*R3 + l*(C1*R2 + C2*R2);
            const auto a2 = m*(C1*C3*R1*R3 - C2*C3*R3*R4 + C1*C3*R3*R3 + C2*C3*R3*R3)
                           + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                           - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                           + l*(C1*C2*R2*R4 + C1*C2*R1*R2 + C1*C3*R2*R4 + C2*C3*R2*R4)
                           + (C1*C2*R1*R4 + C1*C3*R1*R4 + C1*C2*R3*R4 + C1*C2*R1*R3 + C1*C3*R3*R4 + C2*C3*R3*R4);
            const auto a3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                           - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                           + m*(C1*C2*C3*R3*R3*R4 + C1*C2*C3*R1*R3*R3 - C1*C2*C3*R1*R3*R4)
                           + l*C1*C2*C3*R1*R2*R4
                           + C1*C2*C3*R1*R3*R4;

            const auto c  = 2.0 * sampleRate;
            const auto c2 = c * c;
            const auto c3 = c2 * c;

            const auto B0 = -b1*c - b2*c2 - b3*c3;
            const auto B1 = -b1*c + b2*c2 + 3.0*b3*c3;
            const auto B2 =  b1*c + b2*c2 - 3.0*b3*c3;
            const auto B3 =  b1*c - b2*c2 + b3*c3;

            const auto A0 = -a0 - a1*c - a2*c2 - a3*c3;
            const auto A1 = -3.0*a0 - a1*c + a2*c2 + 3.0*a3*c3;
            const auto A2 = -3.0*a0 + a1*c + a2*c2 - 3.0*a3*c3;
            const auto A3 = -a0 + a1*c - a2*c2 + a3*c3;

            // Normalise by A0 up front so the per-sample recurrence below
            // needs no division.
            const auto invA0 = 1.0 / A0;
            coeffB0 = B0 * invA0; coeffB1 = B1 * invA0; coeffB2 = B2 * invA0; coeffB3 = B3 * invA0;
            coeffA1 = A1 * invA0; coeffA2 = A2 * invA0; coeffA3 = A3 * invA0;
        }

        float processSample (int channel, float input)
        {
            auto& s = state[(size_t) channel];
            const auto x0 = static_cast<double> (input);
            const auto y0 = coeffB0*x0 + coeffB1*s.x1 + coeffB2*s.x2 + coeffB3*s.x3
                           - coeffA1*s.y1 - coeffA2*s.y2 - coeffA3*s.y3;
            s.x3 = s.x2; s.x2 = s.x1; s.x1 = x0;
            s.y3 = s.y2; s.y2 = s.y1; s.y1 = y0;
            return static_cast<float> (y0);
        }

    private:
        // Direct Form I state: coefficient magnitudes span many orders (the
        // c^3 terms dominate at oversampled rates), so accumulating in
        // double keeps the recurrence well-conditioned; only the final
        // sample is narrowed back to float.
        struct ChannelState { double x1 = 0, x2 = 0, x3 = 0, y1 = 0, y2 = 0, y3 = 0; };
        std::array<ChannelState, 2> state {};
        double coeffB0 = 0, coeffB1 = 0, coeffB2 = 0, coeffB3 = 0;
        double coeffA1 = 0, coeffA2 = 0, coeffA3 = 0;
    };

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::IIR::Filter<float> inputCoupling[2], interstageCoupling[2];
    juce::dsp::IIR::Filter<float> toneFilter[2], transformerLowPass[2];
    BassmanToneStack bassmanStack;
    juce::SmoothedValue<float> outputGain;
    std::array<float, 2> biasMemory {};
    std::array<float, 2> sagDetectorLP {};
    double baseSampleRate = 44100.0, processingSampleRate = 176400.0;
    int channelCount = 2;
    Voice voice = Voice::vintage5E3;
    float driveAmount = 0.4f, lastTone01 = 0.6f;
    float lastBass01 = 0.5f, lastMid01 = 0.5f, lastTreble01 = 0.5f;
    float targetOutputGain = 1.0f, sagEnvelope = 0.0f;
    float sagAttack = 0.999f, sagRelease = 0.999f;
    float sagDetectorLPCoefficient = 0.01f;
};
