#pragma once

#include <JuceHeader.h>

// Dynamic, oversampled 5E3-inspired model. Gain and memory are distributed
// through the circuit's major functional stages instead of one static
// clipper: input/interstage coupling caps, a two-stage 12AY7/12AX7 preamp
// whose nonlinearity is now a genuine triode current model (TriodeStage
// below) rather than a curve-fit tanh, a Tone/Bassman-stack network sitting
// between the two preamp stages (matching the real 5E3's V1 -> Tone -> V2A
// signal order, confirmed against Rob Robinette's own annotated 5E3
// schematic: shared 1M tone pot, 0.005uF tone cap, feeding V2A's grid — not
// after both preamp stages), a cathodyne-style phase inverter, a genuinely
// differential push-pull power stage (two tubes driven by +V/-V from the
// cathodyne and subtracted at the output transformer, the actual mechanism
// that cancels even-order harmonics for a matched pair) with a bass-weighted
// sag detector, and output-transformer core saturation distinct from sag —
// per Rob Robinette's 5E3 circuit writeup and annotated schematic
// (robrobinette.com).
//
// Preamp stage methodology: TriodeStage implements the cathode/grid current
// equations from R. Dempwolf, U. Zolzer, "A Physically-Motivated Triode
// Model for Circuit Simulations," DAFx-11 (full text read end-to-end) --
// I_k = G*h((1/mu)*V_a + V_g)^gamma, I_g = G_g*h(V_g)^xi + I_g0, with
// h(x) = softplus smoothing -- fitted from the paper's own measured-12AX7
// Table 1 ("RSD1" tube), the only real, published, measurement-fitted
// parameter set available for this equation family. No 12AY7-specific fit
// exists in the literature; V1 uses the same Table 1 shape parameters with
// mu swapped to 12AY7's real datasheet value (~44 vs 12AX7's ~96, per
// RCA/GE tube manuals) -- the one parameter with an independent, citable
// source for that tube, honestly flagged as an approximation rather than a
// second real fit. The plate-load network (Ra, coupling cap) is discretized
// via the same direct bilinear transform BassmanToneStack below already
// uses, verified against a sub-stepped Euler integration. The DC operating
// point (quiescent plate voltage/current, and the grid-leak resistor's own
// resting current) is solved self-consistently at prepare() time via
// bisection -- necessary because the paper's V_eff term uses the tube's
// real, large (~100-300V) plate voltage, not a small AC-only value, a
// distinction a first attempt at this got wrong and caught via the same
// numerical-harness-before-shipping discipline used for Klon/TS9's WDF
// clippers. Blocking distortion (grid current charging the input coupling
// cap and shifting the operating bias under heavy drive) now emerges from
// that same real grid-current equation feeding a genuine RC charge/discharge
// model, replacing the previous version's ad-hoc bias-memory heuristic.
// Known, deliberate simplification: both stages are modeled as
// cathode-bypassed (a fixed bias point) for tractability; the real 5E3's V1
// is unbypassed, which real hardware uses for extra local negative feedback
// and headroom -- this model captures the tube's real current-vs-voltage
// nonlinearity and grid-conduction/blocking behaviour, not that specific
// cathode-degeneration detail.

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
            triodeV1[ch].mu = 44.0f; // 12AY7 datasheet mu, real fit's other shape params
            triodeV1[ch].prepare (processingSampleRate);
            triodeV2A[ch].prepare (processingSampleRate);
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
            triodeV1[ch].reset(); triodeV2A[ch].reset();
        }
        bassmanStack.reset();
        sagEnvelope = 0.0f;
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

    // Unlike the stomp modules (Comp/Klon/TS9), this reports latency even
    // when disabled: the host's PDC needs a stable value regardless of
    // whether the amp itself is currently in the chain, or every toggle
    // would shift downstream sample alignment.
    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled || oversampling == nullptr || buffer.getNumSamples() == 0)
            return;

        auto inputBlock = juce::dsp::AudioBlock<float> (buffer)
                              .getSubsetChannelBlock (0, static_cast<size_t> (channelCount));
        auto block = oversampling->processSamplesUp (inputBlock);
        const auto samples = static_cast<int> (block.getNumSamples());
        const auto channels = static_cast<int> (block.getNumChannels());
        // Grid-signal scale reaching V1 -- the one driveAmount-controlled
        // gain in the preamp now (V2A's own drive comes naturally from
        // however hard V1's real current model already clipped, not a
        // second independent multiplier -- see class comment). A harness
        // sweep of raw (uncalibrated) output against this scale showed the
        // triode's own current-limiting makes it a STEEP curve only across
        // roughly scale=0.005-0.08 (raw output ~11 to ~98) and then a much
        // flatter one beyond that (0.08-1.5 only reaches ~157) -- an earlier
        // 0.035-0.285 taper sat almost entirely in that flat region, so the
        // knob barely did anything (matching the "drive isn't behaving
        // right" complaint) even though it wasn't the cause of the earlier
        // loudness/mud complaint (that was the plate-load topology bug
        // above). Retuned to actually span the steep part.
        const auto inputVoltsScale = 0.008f + driveAmount * 0.09f;
        const auto powerDrive = 1.15f + driveAmount * 2.7f;

        for (int i = 0; i < samples; ++i)
        {
            float detector = 0.0f;
            std::array<float, 2> phaseInverterOut {};
            for (int ch = 0; ch < channels; ++ch)
            {
                auto x = inputCoupling[ch].processSample (block.getSample (ch, i));
                auto triode1 = triodeV1[ch].processSample (x * inputVoltsScale);
                triode1 = interstageCoupling[ch].processSample (triode1);

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
                    toned = toneFilter[ch].processSample (triode1);
                else
                    toned = bassmanStack.processSample (ch, triode1);

                // triodeV2A returns real plate-swing volts (can run to tens
                // of volts under drive); outputCalibration/safetyCeiling
                // bring that back to sample scale, harness-verified end to
                // end across driveAmount x input level (same role and same
                // backstop pattern as Klon/TS9's own output calibration).
                const auto triode2Raw = triodeV2A[ch].processSample (toned);
                const auto triode2 = safetyCeiling * std::tanh (triode2Raw * outputCalibration / safetyCeiling);
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

    // A single common-cathode triode gain stage: real grid/cathode current
    // equations (Dempwolf & Zolzer, DAFx-11) driving a real plate-load RC
    // network (Ra plate resistor, Cout coupling cap), plus a physically
    // modeled blocking-distortion path (grid current charging the input
    // coupling cap through the grid-leak resistor Rg). See the class-level
    // comment above for the full methodology and its one honest
    // approximation (12AY7's mu substituted into a 12AX7-measured fit).
    struct TriodeStage
    {
        // Fitted/assumed parameters (Dempwolf-Zolzer Table 1, "RSD1" 12AX7;
        // mu overridden per-instance for the 12AY7 stage).
        float G = 1.371e-3f, mu = 96.2f, gamma = 1.349f, C = 3.917f;
        float Gg = 5.911e-4f, xi = 1.264f, Cg = 11.71f, Ig0 = 8.025e-8f;
        // Real 5E3 plate resistor (Robinette schematic) and grid-leak network.
        float Ra = 100000.0f, Rg = 1.0e6f, Cin = 0.02e-6f;
        float biasPoint = -1.5f, Vb = 300.0f;
        // NOT a real coupling-cap value -- see processSample()'s comment.
        // This exists purely to damp the one-sample-delayed feedback below;
        // 220pF against Ra gives a ~7.2kHz pole, chosen empirically (swept
        // 47pF-2nF against noise/impulse/extreme-drive stress) as comfortably
        // inside the stable region (unstable below ~50-90pF) while sitting
        // well above the audio band it must not be heard shaping.
        float Cout = 220.0e-12f;

        float vaAcPrev = 0.0f, gridCharge = 0.0f, iAcPrev = 0.0f;
        float Va0 = 150.0f, Ia0 = 0.0f, restingGridCharge = 0.0f;
        double sampleRate = 44100.0;

        // Numerically stable softplus: log(1+e^(kx))/k without ever
        // exponentiating a large positive argument.
        static float softplus (float x, float k) noexcept
        {
            const auto kx = k * x;
            return (std::max (kx, 0.0f) + std::log1p (std::exp (-std::abs (kx)))) / k;
        }

        float gridCurrent (float vg) const noexcept
        {
            return Gg * std::pow (softplus (vg, Cg), xi) + Ig0;
        }

        float anodeCurrent (float vg, float va) const noexcept
        {
            const auto veff = vg + va / mu;
            return G * std::pow (softplus (veff, C), gamma) - gridCurrent (vg);
        }

        // Self-consistent DC operating point: the quiescent plate
        // voltage/current (Ia(Vg0,Va0) = (Vb-Va0)/Ra, bisection -- Ia is
        // monotonic in Va) AND the grid-leak resistor's own resting current
        // (which shifts Vg0 itself, since Ig0's baseline leakage alone
        // produces a nonzero resting grid-cap charge) -- solved by iterating
        // both to a fixed point. Skipping the second part was the exact bug
        // a standalone harness caught: without it, the running model's
        // actual resting grid voltage never matches the point Va0/Ia0 were
        // solved for, so the "AC-only" current fed to the plate network
        // never settles to zero at rest and the stage finds a wrong,
        // wildly-offset equilibrium instead.
        void solveBiasPoint() noexcept
        {
            restingGridCharge = 0.0f;
            for (int iter = 0; iter < 20; ++iter)
            {
                const auto vg0 = biasPoint - restingGridCharge;
                float lo = 0.0f, hi = Vb;
                for (int i = 0; i < 60; ++i)
                {
                    const auto mid = 0.5f * (lo + hi);
                    if (anodeCurrent (vg0, mid) > (Vb - mid) / Ra)
                        hi = mid;
                    else
                        lo = mid;
                }
                Va0 = 0.5f * (lo + hi);
                Ia0 = anodeCurrent (vg0, Va0);
                restingGridCharge = gridCurrent (vg0) * Rg;
            }
        }

        void prepare (double newSampleRate) noexcept
        {
            sampleRate = newSampleRate;
            solveBiasPoint();
            reset();
        }

        void reset() noexcept { vaAcPrev = 0.0f; gridCharge = restingGridCharge; iAcPrev = 0.0f; }

        // vin: signal voltage arriving at this stage's grid (real volts,
        // not a normalised sample). Returns the plate's AC voltage swing
        // (also real volts -- can run to tens of volts under drive), still
        // needing the caller's own output calibration back to sample scale.
        float processSample (float vin) noexcept
        {
            const auto vg = vin - gridCharge + biasPoint;

            // Blocking distortion: grid current charges the input coupling
            // cap through Rg, chasing a target of Ig(Vg)*Rg (Ohm's law
            // across the grid-leak resistor) with time constant Rg*Cin.
            const auto targetCharge = gridCurrent (vg) * Rg;
            const auto leaky = std::exp (-1.0f / (float) (sampleRate * Rg * Cin));
            gridCharge = leaky * gridCharge + (1.0f - leaky) * targetCharge;

            const auto iaAc = anodeCurrent (vg, Va0 + vaAcPrev) - Ia0;

            // The plate load is really just Ra (Ohm's law: va_ac = -Ra*iaAc)
            // -- there is no real capacitor in parallel with it in the actual
            // circuit; the interstage coupling cap sits in SERIES to the next
            // stage's grid instead, which needs no separate simulation here
            // since va_ac is already a pure zero-mean AC deviation by
            // construction (Va0/Ia0 are subtracted out above), i.e. already
            // "coupled". An earlier version modeled Cout as parallel with Ra
            // by mistake, which turned each stage into an unintended ~80Hz
            // lowpass -- cascaded across two stages that's exactly what made
            // the whole amp sound muffled/boxy ("behind a wall") with the
            // drive's harmonic content chopped off before it could read as
            // clear. Pure Ohm's law (Cout removed outright) is what the real
            // circuit does, but it also removes the only damping in this
            // one-sample-delayed feedback loop and goes numerically unstable
            // almost immediately (verified: blows up to the safety ceiling
            // regardless of input). So Cout stays, but only as a deliberately
            // tiny numerical stabiliser (bilinear R=Ra/C=Cout, same technique
            // BassmanToneStack below uses, accumulated in double for the same
            // reason: K grows into the millions at oversampled rates and
            // float precision on (1-K)/(1+K) alone isn't enough headroom) --
            // its value is chosen for stability margin, not to model any real
            // component (see Cout's own comment above).
            const double K = 2.0 * sampleRate * (double) Ra * (double) Cout;
            const double vaAcD = ((double) Ra * (-(double) iaAc + (double) iAcPrev)
                                   - (1.0 - K) * (double) vaAcPrev) / (1.0 + K);
            const auto vaAc = (float) vaAcD;
            iAcPrev = -iaAc;
            vaAcPrev = vaAc;
            return vaAc;
        }
    };

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
    TriodeStage triodeV1[2], triodeV2A[2];
    BassmanToneStack bassmanStack;
    juce::SmoothedValue<float> outputGain;
    std::array<float, 2> sagDetectorLP {};
    // V2A's raw plate-voltage output (real volts, tens to hundreds of volts
    // under drive) back to sample scale, plus a tanh safety rail backstopping
    // that empirical calibration -- harness-verified end to end (see
    // TriodeStage's class comment), same established pattern as Klon/TS9's
    // own output calibration constants. Raised from an initial 0.02 (which
    // stayed graceful/smooth but never actually reached the safety rail --
    // 0% of samples pinned even at max Drive/loud input, i.e. no genuine
    // hard-clipped "fuzz" was reachable) to 0.065, which the same harness
    // sweep confirmed reaches real double-digit-percent pinned time at high
    // Drive+loud input while staying at 0% for low Drive/quiet playing, so
    // touch sensitivity is preserved rather than trading one problem for
    // another.
    static constexpr float outputCalibration = 0.065f;
    static constexpr float safetyCeiling = 3.0f;
    double baseSampleRate = 44100.0, processingSampleRate = 176400.0;
    int channelCount = 2;
    Voice voice = Voice::vintage5E3;
    float driveAmount = 0.4f, lastTone01 = 0.6f;
    float lastBass01 = 0.5f, lastMid01 = 0.5f, lastTreble01 = 0.5f;
    float targetOutputGain = 1.0f, sagEnvelope = 0.0f;
    float sagAttack = 0.999f, sagRelease = 0.999f;
    float sagDetectorLPCoefficient = 0.01f;
    bool enabled = true;
};
