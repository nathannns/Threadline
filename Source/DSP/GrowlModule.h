#pragma once
#include <JuceHeader.h>
#include "WDFCore.h"

// "Growl" -- an original germanium two-transistor fuzz in the general
// archetype of circuits like the Arbiter/Dallas Fuzz Face (an NPN/PNP
// pair sharing one DC feedback path): not a copy of any specific
// schematic's traced component values -- this project has no rights to
// reproduce a particular commercial pedal's exact BOM -- but built from
// the real, well-documented physics of a bipolar transistor's base-
// emitter junction (the Shockley diode equation) and general germanium
// AF-transistor-typical parameters (low forward voltage, ~0.2-0.3V, and
// high saturation/leakage current relative to silicon -- textbook
// physics facts, not proprietary values).
//
// This exists specifically to model what neither FangsModule nor
// BisonModule can: a fuzz whose front end genuinely LOADS the incoming
// signal through a real nonlinear junction, rather than presenting a
// clean, near-infinite-impedance op-amp input. On a real pedalboard, a
// Fuzz Face's most famous quirk is that rolling back the *guitar's own*
// volume knob changes the source impedance its pickup presents to Q1's
// base -- which measurably shifts Q1's DC operating point and cleans the
// fuzz up. A plugin has no physical pickup or volume pot feeding it, so
// that interaction can't happen automatically the way it does on a
// pedalboard; the Bias control below stands in for it directly --
// deliberately exposing "how loaded/present the front end is" as a knob,
// so the same real effect (less current available to swing the junction
// -> a cleaner, more transparent response) is still something you dial
// in and hear, not lost just because there's no guitar cable involved
// anymore.
//
// Q1's base voltage is solved every sample from the real nonlinear node
// equation -- input current through the Bias resistor, balanced against
// the junction's own Shockley current and a fixed DC bias-feed current
// standing in for the real circuit's collector-to-base feedback resistor
// -- via a warm-started Newton-Raphson iteration: the same technique
// (and the same "warm-started" description) AmpModule's own cathode-
// voltage solve elsewhere in this codebase uses, an original from-
// scratch derivation here, not ported from any external source. Stage
// two reuses this project's existing WDF antiparallel-diode-pair
// machinery (WDFCore.h, verified against Klon/TS9's clippers) for a
// second, harder-clipping stage after Q1's own transistor gain.
class GrowlModule
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec, int oversamplingMode = 1)
    {
        sampleRate = spec.sampleRate;
        channelCount = juce::jlimit (1, 2, (int) spec.numChannels);
        const auto stages = juce::jlimit (0, 2, oversamplingMode);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) channelCount, stages,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversampling->initProcessing (spec.maximumBlockSize);
        for (auto& f : dcBlock)
            f.prepare (spec);

        dryDelay.prepare (spec);
        dryDelay.setMaximumDelayInSamples (64);
        dryDelay.setDelay ((float) getLatencySamples());

        updateDcBlockFilter();
        reset();
    }

    void reset()
    {
        if (oversampling != nullptr)
            oversampling->reset();
        for (auto& v : baseVoltage)
            v = 0.0f;
        for (auto& s : stage2)
            s.reset();
        for (auto& f : dcBlock)
            f.reset();
        dryDelay.reset();
    }

    int getLatencySamples() const noexcept
    {
        return oversampling != nullptr ? juce::roundToInt (oversampling->getLatencyInSamples()) : 0;
    }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    // bias01: 0 = heavily loaded (dark, compressed, "starved") .. 1 =
    // lightly loaded (cleans up, more transparent) -- see class comment.
    // fuzz01: sets the DC bias-feed current (how hard Q1's junction is
    // pushed into conduction) and stage-2's drive.
    void setParameters (float bias01, float fuzz01, float level01, float mix01)
    {
        bias01 = juce::jlimit (0.0f, 1.0f, bias01);
        fuzzAmount = juce::jlimit (0.0f, 1.0f, fuzz01);
        outputLevel = juce::jlimit (0.0f, 2.0f, level01 * 2.0f);
        mix = juce::jlimit (0.0f, 1.0f, mix01);
        // Audit-caught inefficiency: the quiescent-point solve below only
        // needs to re-run when Bias/Fuzz actually change (it doesn't
        // depend on level01/mix01 at all), same change-detection pattern
        // KlonModule's lastTreble01 already uses -- previously ran a full
        // Newton-Raphson solve every single block regardless.
        if (juce::approximatelyEqual (bias01, lastBias01) && juce::approximatelyEqual (fuzzAmount, lastFuzzAmount))
            return;
        lastBias01 = bias01;
        lastFuzzAmount = fuzzAmount;

        // Real Fuzz-Face-style source impedances span roughly 1k (heavily
        // loaded, "starved"/compressed) to 220k (barely loaded, near-
        // open-circuit -- transparent, cleans up readily on a light
        // touch).
        sourceResistance = juce::jmap (bias01, 1000.0f, 220000.0f);
        biasCurrent = 3.0e-6f + fuzzAmount * 40.0e-6f;
        // Checked a real Fuzz Face schematic directly (el34world archive,
        // Hendrix/Dunlop JH-2): Q1 and Q2 aren't two independently
        // cascaded stages RC-coupled together the way Bison's two clip
        // stages are -- the "Fuzz" pot sits in a feedback network shared
        // by BOTH transistors (Q1's emitter bridged to Q2's emitter
        // region through it), so turning Fuzz down reduces the whole
        // loop's gain, not just Q1's own bias point. stage2DriveScale
        // used to be a fixed constant that fuzzAmount never touched at
        // all -- stage 2 (this module's stand-in for Q2, see its own
        // struct comment on why it's a diode-pair rather than a second
        // real transistor solve) now scales with Fuzz too, so the knob's
        // effect is felt through the whole chain the way the real shared
        // loop behaves, not confined to Q1's DC bias alone.
        stage2DriveScale = 15.0f + fuzzAmount * 40.0f;

        // Quiescent (silent-input) collector current -- subtracted from the
        // running ic1 in process() so stage1Out carries only the AC
        // deviation, the same "iaAc = anodeCurrent(...) - Ia0" AC-coupling
        // pattern AmpModule's TriodeStage/PentodeStage already use. Without
        // this, stage1Out carried the FULL DC-biased collector current (up
        // to a couple mA through an 8.2k load, i.e. tens of real volts) into
        // stage two's Thevenin input -- a huge constant offset that swamped
        // the much smaller AC ripple riding on top of it once stage2's own
        // diode pair (with its own ~0.6V-scale linear range) saturated hard
        // against that offset. Solved once here (not per-sample) with its
        // own throwaway Newton-Raphson state, since the quiescent point only
        // changes when Bias/Fuzz change, not every sample.
        float quiescentVb = 0.0f;
        const auto ib1Quiescent = solveBaseCurrent (0.0f, quiescentVb);
        ic1Quiescent = ibKnee * beta * std::tanh (ib1Quiescent / ibKnee);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled || oversampling == nullptr)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), channelCount);
        const auto numSamples = buffer.getNumSamples();
        dryDelay.setDelay ((float) getLatencySamples());

        wetBuffer.makeCopyOf (buffer, true);

        // Q1's junction is genuinely nonlinear and needs the same anti-
        // aliasing care as the WDF clippers elsewhere, so it (and stage
        // two) run at the oversampled rate too.
        juce::dsp::AudioBlock<float> block (wetBuffer);
        auto osBlock = oversampling->processSamplesUp (block);
        const auto osSamples = (int) osBlock.getNumSamples();
        for (size_t ch = 0; ch < osBlock.getNumChannels(); ++ch)
        {
            for (int i = 0; i < osSamples; ++i)
            {
                const auto vin = osBlock.getSample ((int) ch, i) * inputScale;
                const auto ib1 = solveBaseCurrent (vin, baseVoltage[ch]);
                // Transistor current gain, soft-saturating at high drive
                // (real germanium AF transistors' beta falls off well
                // before a silicon device's would) -- a generic, well-
                // established large-signal saturating-gain shortcut
                // standing in for a full Ebers-Moll collector-current
                // model, the same tier of simplification KlonModule/
                // TS9Module already make for their own op-amps (ideal,
                // not modeled transistor-level).
                // ibKnee sets where the tanh's saturating "knee" sits
                // relative to Q1's actual base current -- found via the
                // math (not just audition): the quiescent (silent-input)
                // ib1 at this circuit's realistic Bias/Fuzz range already
                // sits around 3-48uA (biasCurrent alone, before any audio
                // signal), so a knee at the previous 6uA left the stage
                // ALREADY deep in tanh's flat region even at rest -- real
                // audio riding on top of that DC point barely moved ic1 at
                // all (tanh's derivative near saturation is close to zero),
                // which is what "everything is muted when Growl is on"
                // actually was: not silence from a broken signal path, but
                // a transistor stage with no headroom left to swing in.
                // 150uA keeps the knee comfortably above the realistic
                // quiescent range so normal playing stays in the responsive
                // part of the curve, only truly saturating on hard peaks --
                // the "soft-saturating at high drive" behaviour this was
                // always meant to have.
                const auto ic1 = ibKnee * beta * std::tanh (ib1 / ibKnee);
                // AC-coupled: subtract the quiescent collector current (see
                // setParameters()'s own comment) so only the signal-driven
                // deviation reaches stage two, not the full DC-biased
                // current riding on top of it.
                const auto stage1Out = -(ic1 - ic1Quiescent) * collectorLoadOhms;
                auto clipped = stage2[ch].processSample (stage1Out * stage2DriveScale) * outputCalibration;
                clipped = safetyCeiling * std::tanh (clipped / safetyCeiling);
                osBlock.setSample ((int) ch, i, clipped);
            }
        }
        oversampling->processSamplesDown (block);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* dry = buffer.getWritePointer (ch);
            auto* wet = wetBuffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                const auto blocked = dcBlock[ch].processSample (wet[i]);
                dryDelay.pushSample (ch, dry[i]);
                const auto delayedDry = dryDelay.popSample (ch);
                dry[i] = (delayedDry * (1.0f - mix) + blocked * mix) * outputLevel;
            }
        }
    }

private:
    void updateDcBlockFilter()
    {
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 30.0f, 0.707f);
        for (auto& f : dcBlock)
            *f.coefficients = *coeffs;
    }

    // Solves Q1's base voltage from
    //   (Vin - Vb)/Rsrc + Ibias - Is*(exp(Vb/(n*Vt)) - 1) = 0
    // via warm-started Newton-Raphson, and returns the actual current
    // drawn into the junction (which drives the transistor's own
    // collector-current gain above). Monotonic in Vb (both the resistive
    // and junction-conductance terms are strictly decreasing), so this
    // converges reliably from a warm start given a DAMPED step -- an
    // undamped tangent-line step can badly overshoot on the first
    // iteration (exp()'s curvature is severe relative to how small the
    // true equilibrium voltage is here, tens to low hundreds of mV), and
    // a standalone harness sweep confirmed that overshoot really did
    // happen and diverge into a runaway (non-crashing but physically
    // nonsensical, many-amp) state at several Bias/Fuzz combinations
    // before this per-iteration step clamp + hard vb range clamp were
    // added -- caught and fixed before this ever reached real audio.
    float solveBaseCurrent (float vin, float& vbState) noexcept
    {
        constexpr float Is = 8.0e-7f;             // germanium AF-transistor-typical base-emitter saturation current
        constexpr float nVt = 1.15f * 0.02585f;   // ideality-scaled thermal voltage (germanium-typical n ~ 1.1-1.2)
        auto vb = vbState;
        for (int iter = 0; iter < 8; ++iter)
        {
            const auto expTerm = std::exp (juce::jlimit (-40.0f, 40.0f, vb / nVt));
            const auto f = (vin - vb) / sourceResistance + biasCurrent - Is * (expTerm - 1.0f);
            const auto fPrime = -1.0f / sourceResistance - (Is / nVt) * expTerm;
            const auto step = juce::jlimit (-0.3f, 0.3f, f / fPrime);
            vb = juce::jlimit (-1.0f, 1.0f, vb - step);
            if (std::abs (step) < 1.0e-7f)
                break;
        }
        vbState = vb;
        return Is * (std::exp (juce::jlimit (-40.0f, 40.0f, vb / nVt)) - 1.0f);
    }

    // Stage two -- a second, harder clip after Q1's own gain, reusing
    // WDFCore's existing antiparallel-diode-pair machinery rather than a
    // second Newton-Raphson junction (the interactive front-end loading
    // this whole pedal is built around is specifically a Q1 phenomenon).
    struct Stage2Clipper
    {
        WDF::ResistiveCurrentSource node { 47000.0f };
        WDF::DiodePair<WDF::ResistiveCurrentSource> dp { node, 8.0e-7f, 1.15f * 0.02585f };
        void reset() noexcept { node.wdf.a = node.wdf.b = 0.0f; }
        float processSample (float theveninVoltage) noexcept
        {
            node.setCurrent (theveninVoltage / node.wdf.R);
            dp.incident (node.reflected());
            node.incident (dp.reflected());
            return WDF::voltage (node.wdf);
        }
    };

    juce::dsp::IIR::Filter<float> dcBlock[2];
    juce::dsp::DelayLine<float> dryDelay;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> wetBuffer;
    Stage2Clipper stage2[2];
    float baseVoltage[2] { 0.0f, 0.0f };
    static constexpr float inputScale = 0.02f; // line-level audio -> representative small-signal base-junction volts
    static constexpr float beta = 110.0f, ibKnee = 150.0e-6f;
    float ic1Quiescent = 0.0f; // see setParameters()'s own comment
    static constexpr float collectorLoadOhms = 8200.0f;
    float stage2DriveScale = 40.0f; // see setParameters()'s own comment on why this now tracks fuzzAmount
    // Empirically-tuned via a standalone harness (same discipline as
    // Klon/TS9's own calibration constants) -- stage2's raw output peak
    // plateaus around 0.23-0.26 across the full Bias x Fuzz grid (the
    // diode pair saturates quickly, same as real hardware), so this
    // brings max-drive settings up into the same "hot but not fully
    // pinned" ~2.0-2.5 (of the 3.0 safety ceiling) range Klon/TS9 target.
    static constexpr float outputCalibration = 9.0f;
    static constexpr float safetyCeiling = 3.0f;
    double sampleRate = 44100.0;
    int channelCount = 2;
    float sourceResistance = 47000.0f;
    float biasCurrent = 5.0e-6f;
    float lastBias01 = -1.0f, lastFuzzAmount = -1.0f; // sentinel forces the first setParameters() call through
    float fuzzAmount = 0.5f, outputLevel = 1.0f, mix = 1.0f;
    bool enabled = false;
};
