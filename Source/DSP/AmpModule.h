#pragma once

#include <JuceHeader.h>
#include "GuitarSignalLevel.h"

#if defined(THREADLINE_AMP_ANALYSIS_TAPS)
 #define THREADLINE_AMP_ANALYSIS_TAP(channel, stage, sample) \
    emitAnalysisTap ((channel), AnalysisStage::stage, (sample))
#else
 #define THREADLINE_AMP_ANALYSIS_TAP(channel, stage, sample) do { } while (false)
#endif

namespace
{
    // Audio-rate transcendental hotspot shared by every tube stage's Newton
    // iteration. softplus(x,k) = ln(1 + e^(kx)) / k, and the logistic
    // sigmoid 1/(1+e^-kx) is its own derivative (up to the k factor) -- the
    // two were previously computed separately, so each Newton iteration paid
    // TWO exp() calls (one inside softplus, one inside the sigmoid) plus a
    // log1p(). Folding them together halves the exp() count, and the two
    // saturated tails skip the transcendental work entirely: for kx > 6 the
    // sigmoid has saturated to 1 and softplus is linear in x (ln(1+e^-kx)
    // contributes < 0.05% of x), and for kx < -6 both collapse toward 0 with
    // a single exp() and no log1p(). The mid-band path stays exact (std::exp
    // + std::log1p), so the only numerical drift vs the original is < 0.05%
    // relative in the deep tails -- inaudible and below the validation
    // harness's blow-up/NaN thresholds.
    inline void softplusSigmoid (float x, float k, float& sp, float& sg) noexcept
    {
        const auto kx = k * x;
        if (kx > 6.0f)
        {
            sp = x;
            sg = 1.0f;
            return;
        }
        const auto e = std::exp (-std::abs (kx));   // in (0, 1]
        if (kx < -6.0f)
        {
            sp = e / k;                             // ln(1+e) ~= e when e << 1
            sg = e / (1.0f + e);
            return;
        }
        sg = kx >= 0.0f ? 1.0f / (1.0f + e) : e / (1.0f + e);
        sp = (std::max (kx, 0.0f) + std::log1p (e)) / k;
    }
}

// Dynamic, oversampled 5E3-inspired model. Gain and memory are distributed
// through the circuit's major functional stages instead of one static
// clipper: input/interstage coupling caps, a two-stage 12AY7/12AX7 preamp
// whose nonlinearity is now a genuine triode current model (TriodeStage
// below) rather than a curve-fit tanh, a Tone/Bassman-stack network sitting
// between the two preamp stages (matching the real 5E3's V1 -> Tone -> V2A
// signal order, confirmed against Rob Robinette's own annotated 5E3
// schematic: shared 1M tone pot, 0.005uF tone cap, feeding V2A's grid — not
// after both preamp stages), a real cathodyne (split-load) phase inverter --
// a genuine matched-Ra=Rk TriodeStage instance, not a curve-fit approximation
// -- and a genuinely differential push-pull power stage using two real
// 6V6GT beam-tetrode models (PentodeStage below, Koren's model), driven by
// the cathodyne's own two antiphase outputs and subtracted at the output
// transformer, the actual mechanism that cancels even-order harmonics for a
// matched pair, with a bass-weighted sag detector and output-transformer
// core saturation distinct from sag — per Rob Robinette's 5E3 circuit
// writeup and annotated schematic (robrobinette.com).
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
// V1's cathode is modeled unbypassed, matching the real 5E3 (Robinette
// schematic: 1.5k cathode resistor, no bypass cap, unlike V2A's bypassed
// stage) -- TriodeStage's cathodeUnbypassed mode solves the cathode voltage
// Vk self-consistently each sample from the SAME current equations above
// (Ik(Vgk,Va) == Vk/Rk, Vgk = Vg - Vk) via a warm-started Newton-Raphson
// iteration, and its DC operating point via a nested bisection at prepare()
// time (see TriodeStage::solveBiasPoint). Rising cathode current pulls Vk
// up, which pulls Vgk back down -- real local negative feedback, lowering
// V1's gain and raising its headroom versus a fixed-bias stage, exactly
// the effect the real unbypassed cathode is there for. V2A stays
// cathode-bypassed (fixed bias point), matching the real circuit.
//
// Phase inverter and power stage methodology: the cathodyne is a THIRD
// TriodeStage instance (same class, same Dempwolf-Zolzer current equations
// above), not a new struct -- a cathodyne is architecturally just another
// cathode-unbypassed common-cathode stage, except its plate resistor and
// cathode resistor are matched (Ra=Rk) instead of Ra>>Rk, which is what
// makes its two outputs come out roughly equal in amplitude. Component
// values (Ra=56k, Rk=56k+1.5k bias-balance padding, combined here as a
// single 57.5k lumped cathode resistor) are the real 5E3 V2B values, cross-
// confirmed from Robinette's schematic and independent tube-amp-DIY
// community sources (ampbooks.com's 5E3 circuit analysis; tdpri.com forum
// threads on 5E3 phase inverter balancing). getCathodeAc() exposes the
// cathode's own AC deviation (already tracked internally by
// cathodeUnbypassed) as this stage's second, antiphase output -- verified
// in a standalone harness to come out ~0.97:1 in amplitude versus the plate
// output and genuinely antiphase (negative cross-correlation measured
// directly, not assumed from the topology).
//
// The 6V6GT power tubes are a different physical device -- a beam tetrode,
// not a triode -- so they need a different equation family: PentodeStage
// below implements Norman Koren's pentode/beam-tetrode SPICE model (also
// widely used for other guitar-relevant power tubes: 6L6, EL34, EL84).
// Confirmed against an academic source that reproduces Koren's original
// 1996 Glass Audio equations verbatim (E1 = Vg2/Kp * ln(1+exp(Kp*(1/mu +
// Vg1/Vg2))), Ia = E1^Ex/Kg1 * atan(Va/Kvb) -- the arctangent "knee" term
// is what makes plate current largely Va-independent past a threshold set
// by Kvb, the actual physical signature of a screen grid shielding the
// plate from the control grid, rather than a triode's roughly-linear
// mu-scaled Va dependence). Real 6V6-GTA parameters (MU=10.70, EX=1.310,
// KG1=1672.0, KG2=4500, KP=41.16, KVB=12.7) are Koren's own published fit
// (his SPICE tube library, Koren_Tubes.cir, sourced from a GE datasheet),
// not guessed or curve-fit for this project. B+ (373V) and screen voltage
// (295V, held fixed -- see PentodeStage's own comment on scope) are
// Robinette's 5E3 schematic values; the shared 250-ohm cathode-bias
// resistor and 8k push-pull primary (reflected to 2k per tube: plate-to-
// plate impedance divides by 4 for a center-tapped winding) are likewise
// his. Verified in a standalone harness before wiring in: both stages'
// DC bias points converge with exact Ohm's-law closure (6V6 idle plate
// current lands at 37.1mA, squarely in a real 6V6's typical class-AB
// range), the calibration constants (cathodyneInputScale,
// powerStageInputScale, powerStageOutputScale below) were swept across the
// full realistic Drive x input-loudness range plus stress-test extremes
// well beyond what the signal path can actually deliver, and a hard-impulse
// stress test stayed bounded with no blow-up.

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
    //
    // VoxAC30 is a genuinely different circuit, not a retuned 5E3: two
    // 12AX7 preamp stages (reusing the same TriodeStage/Dempwolf-Zolzer
    // equations above -- research confirmed the AC30/6 "Top Boost" circuit
    // most players actually mean by "a Vox amp" -- the Beatles/Brian May
    // chime tone -- replaced the earlier AC30/4's troublesome, microphonic
    // EF86 pentode preamp with an all-12AX7 design; EF86 only appears in
    // the earlier, less common AC30/4), an independent Bass/Treble shelf
    // pair standing in for the real Top Boost tone network (that network
    // is actually wrapped in its own local negative-feedback loop around a
    // dedicated gain stage -- a genuinely different, active topology from
    // Fender/Marshall's simple passive insertion; modeling that NFB loop
    // itself is a topology change beyond this pass's scope, so this is an
    // honestly-simplified series insertion at the same point in the chain
    // instead, same spirit as the 12AY7 mu-substitution already flagged
    // above), a real long-tailed-pair (differential/cathode-coupled) phase
    // inverter (LongTailPairStage below -- Vox/Marshall-family amps' real
    // PI topology, genuinely different from the 5E3's cathodyne, and this
    // circuit's own most distinctive element), and a pair of real EL84
    // beam-tetrode power tubes (PentodeStage below, Koren's model again,
    // real EL84 parameters this time). Component values are real where a
    // source was found (LTP tail resistor 47k, EL84 output transformer 4k
    // plate-to-plate reflecting to 1k/tube, first preamp stage B+ 275V --
    // ampbooks.com's Vox AC30 circuit analysis), and reasonable, honestly-
    // approximate where one wasn't (preamp Ra=100k, matching this file's
    // own 5E3 preamp convention; EL84 cathode-bias resistor picked at 270R
    // per tube -- not sourced for this specific amp, chosen because it's
    // what a harness sweep showed lands the idle plate current at ~31mA,
    // squarely in a real EL84 class-A guitar amp's typical 25-35mA idle
    // range, rather than the ~70mA an initially-sourced-but-likely-
    // differently-measured 47R value produced). See LongTailPairStage's
    // own comment for that stage's specific methodology.
    // FenderAB763 is the blackface Fender Deluxe Reverb's normal channel
    // (reverb/vibrato circuitry excluded -- this project already has
    // dedicated Spring reverb and Tremolo pedals, so duplicating those
    // inside the amp voice itself would be redundant, same reasoning
    // already applied to every other voice here). Real, well-documented
    // component values throughout (this circuit is one of the most
    // thoroughly published in guitar amp history) -- sourced from Rob
    // Robinette's own AB763 circuit-analysis writeup (robrobinette.com,
    // already this file's established source for the 5E3) and Mercury
    // Magnetics' published transformer spec:
    //  - Preamp V1: 100k plate load, 1.5k unbypassed cathode (real values;
    //    the deliberately-unbypassed treatment mirrors the 5E3's own V1
    //    for the same local-negative-feedback/extra-headroom reason,
    //    consistent with the AB763's own reputation for more clean
    //    headroom than a tweed circuit at a given volume), ~415V B+.
    //  - Tone stack: the SAME general Fender/Marshall passive network
    //    Yeh & Smith's paper covers (BassmanToneStack above, now
    //    generalized to accept different component values) -- real
    //    blackface values where multiple independent sources agree
    //    (250pF treble cap, identical to the tweed Bassman's own C1; 0.1uF
    //    bass cap and 0.047uF mid cap, both genuinely different from
    //    Bassman's 20nF/20nF; 6.8k mid resistor, genuinely different from
    //    Bassman's 25k) -- R1/R2/R4 reused from the Bassman's own values
    //    where no blackface-specific difference was found sourced.
    //  - Phase inverter: a real long-tailed pair (12AX7 -- same tube and
    //    equations as this file's other 12AX7 stages, LongTailPairStage
    //    above, first built for the Vox voice), with the AB763's own
    //    well-documented ASYMMETRIC plate loads (82k/100k -- a genuine,
    //    deliberate real-circuit quirk balancing the two halves' differing
    //    input drive, not a mistake to "fix"), ~22k tail resistor, ~325V
    //    supply.
    //  - Power stage: real 6V6GT tubes -- literally the same physical tube
    //    (and so the exact same Koren SPICE parameters) already used for
    //    the 5E3's power stage, just in a genuinely different bias scheme:
    //    the AB763 is FIXED-bias (external ~-35V supply to the grids, no
    //    cathode resistor), not self-biased like the 5E3 -- see
    //    PentodeStage's own gridBiasOffset comment for how that's modeled
    //    with the same struct. ~420V plate supply, ~415V screen, ~6k
    //    plate-to-plate output transformer (Mercury Magnetics' published
    //    spec) reflecting to 1.5k/tube.
    enum class Voice { vintage5E3 = 0, modern3Band = 1, voxAC30 = 2, fenderAB763 = 3, jtm45 = 4, mesaMarkI = 5, rolandJC120 = 6 };

#if defined(THREADLINE_AMP_ANALYSIS_TAPS)
    // Analysis-only stage taps. The entire API and every call site compile
    // out of the shipping plugin; AmpStageProbe enables the macro in its own
    // target and writes samples into preallocated capture buffers.
    enum class AnalysisStage { v1Input, v1Output, v2Input, v2Output,
                               phaseInverterInput, phaseInverterOutput,
                               powerInput, powerOutput, count };
    using AnalysisTapCallback = void (*) (void*, AnalysisStage, float);
    void setAnalysisTapCallback (AnalysisTapCallback callback, void* context) noexcept
    {
        analysisTapCallback = callback;
        analysisTapContext = context;
    }
#endif

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
            triodeV1[ch].cathodeUnbypassed = true; // real 5E3 V1: unbypassed cathode, local NFB
            triodeV1[ch].prepare (processingSampleRate);
            triodeV2A[ch].prepare (processingSampleRate);
            // V2B cathodyne phase inverter: matched split-load (Ra=Rk),
            // the defining cathodyne trait -- see class comment.
            cathodyneStage[ch].Ra = 56000.0f;
            cathodyneStage[ch].Rk = 57500.0f;
            cathodyneStage[ch].cathodeUnbypassed = true;
            cathodyneStage[ch].prepare (processingSampleRate);
            powerTubeA[ch].prepare (processingSampleRate);
            powerTubeB[ch].prepare (processingSampleRate);

            // Vox AC30 voice -- see class comment for sourcing of each
            // value. Both preamp triodes: real ampbooks.com first-stage B+
            // (275V), otherwise TriodeStage's own 12AX7-fit defaults
            // (Ra=100k matching this file's own preamp convention,
            // cathodeUnbypassed=false/fixed-bias default -- Vox preamp
            // stages are bypassed-cathode for max gain, unlike the 5E3's
            // deliberately-unbypassed V1).
            triodeVoxV1[ch].Vb = 275.0f;
            triodeVoxV1[ch].prepare (processingSampleRate);
            triodeVoxV2[ch].Vb = 275.0f;
            triodeVoxV2[ch].prepare (processingSampleRate);
            voxInterstageCoupling[ch].prepare (osSpec);
            voxBassShelf[ch].prepare (osSpec);
            voxTrebleShelf[ch].prepare (osSpec);

            voxPI[ch].Ra1 = 100000.0f; voxPI[ch].Ra2 = 100000.0f;
            voxPI[ch].Rtail = 47000.0f; voxPI[ch].Vb = 275.0f;
            voxPI[ch].prepare (processingSampleRate);

            // Real EL84 (Koren, Norman Koren's published SPICE library) --
            // see class comment for the cathode-resistor derivation.
            for (auto* tube : { &powerTubeVoxA[ch], &powerTubeVoxB[ch] })
            {
                tube->mu = 21.29f; tube->ex = 1.240f; tube->kg1 = 401.7f;
                tube->kg2 = 4500.0f; tube->kp = 111.04f; tube->kvb = 17.9f;
                tube->screenVoltage = 300.0f; // EL84 datasheet max screen rating, standard application choice
                tube->Vb = 345.0f;            // ampbooks.com AC30 power-stage B+
                tube->Ra = 1000.0f;           // 4k plate-to-plate OT / 4 (ampbooks.com), same halving rule as the 6V6 pair
                tube->Rk = 270.0f;            // harness-derived (see class comment): lands idle current at ~31mA, real class-A EL84 territory
                tube->prepare (processingSampleRate);
            }

            // Fender AB763 voice -- see class comment for full sourcing.
            // Real Deluxe Reverb schematic (both Normal and Vibrato channel
            // V1 stages) shows a 1.5k cathode resistor WITH its own 25uF
            // bypass cap -- i.e. bypassed, standard full-gain Fender first
            // stage, unlike the 5E3's V1 above (that one's genuinely
            // unbypassed, a deliberate 5E3-specific design choice). Audit-
            // caught: this previously set cathodeUnbypassed=true here too,
            // likely carried over from the 5E3 case by mistake.
            //
            // Rk still has to be solved for correctly though -- it's what
            // sets the real DC operating point regardless of whether the
            // cathode is AC-bypassed. Two-pass trick: solve self-biased
            // first purely to find the equilibrium cathode voltage Rk
            // settles to, capture that as an equivalent fixed biasPoint,
            // then re-solve in the (correct, faster) fixed-bias mode with
            // that exact point baked in -- reuses TriodeStage's existing
            // self-bias solver as-is rather than adding a new one.
            triodeFenderV1[ch].Vb = 415.0f;
            triodeFenderV1[ch].Rk = 1500.0f;
            triodeFenderV1[ch].cathodeUnbypassed = true;
            triodeFenderV1[ch].prepare (processingSampleRate);
            triodeFenderV1[ch].biasPoint = -triodeFenderV1[ch].vk0;
            triodeFenderV1[ch].cathodeUnbypassed = false;
            triodeFenderV1[ch].prepare (processingSampleRate);
            triodeFenderV2[ch].Vb = 415.0f;
            triodeFenderV2[ch].prepare (processingSampleRate);
            fenderInterstageCoupling[ch].prepare (osSpec);

            fenderPI[ch].Ra1 = 82000.0f; fenderPI[ch].Ra2 = 100000.0f; // real, deliberately asymmetric (see class comment)
            fenderPI[ch].Rtail = 22000.0f;
            fenderPI[ch].Vb = 325.0f;
            fenderPI[ch].prepare (processingSampleRate);

            for (auto* tube : { &powerTubeFenderA[ch], &powerTubeFenderB[ch] })
            {
                // Real 6V6GT (Koren) -- identical parameters to the 5E3's
                // own power tubes above (literally the same physical tube).
                tube->mu = 10.70f; tube->ex = 1.310f; tube->kg1 = 1672.0f;
                tube->kg2 = 4500.0f; tube->kp = 41.16f; tube->kvb = 12.7f;
                tube->screenVoltage = 415.0f; // Robinette: B+2 ~415V
                tube->Vb = 420.0f;            // Robinette: B+1 ~420V loaded
                tube->Ra = 1500.0f;           // ~6k plate-to-plate OT (Mercury Magnetics) / 4
                // Fixed bias, not self-biased (see PentodeStage's own
                // gridBiasOffset comment) -- a near-zero Rk plus the real
                // ~-35V nominal fixed bias voltage (Robinette/community-
                // measured AB763 bias specs), instead of the 5E3's Rk=250
                // self-bias scheme.
                tube->Rk = 1.0f;
                tube->gridBiasOffset = -35.0f;
                tube->prepare (processingSampleRate);
            }

            // Marshall JTM45 voice -- see class comment for full sourcing.
            // Real component values/voltages read directly from the
            // "Basic Schematic for Marshall Trem Amps (Types 1961, 1962,
            // 1987T)" (drtube.com JTM45 archive) -- the same underlying
            // amp chassis the plain JTM45 uses, just with an added
            // tremolo channel this voice doesn't model (Tremolo already
            // exists as its own separate pedal, same reasoning as every
            // other voice not re-modeling effects it has a dedicated
            // pedal for). V1 (input pair, shared cathode) reads 310V at
            // its plate rail on the schematic's own circled B+ taps; the
            // second stage (feeding the tone stack) reads 380V.
            triodeJTM45V1[ch].Vb = 310.0f;
            triodeJTM45V1[ch].prepare (processingSampleRate);
            triodeJTM45V2[ch].Vb = 380.0f;
            triodeJTM45V2[ch].prepare (processingSampleRate);
            jtm45InterstageCoupling[ch].prepare (osSpec);

            jtm45PI[ch].Ra1 = 82000.0f; jtm45PI[ch].Ra2 = 82000.0f;
            jtm45PI[ch].Rtail = 10000.0f;
            jtm45PI[ch].Vb = 360.0f; // interpolated between the 380V/450V taps either side of it on the schematic
            jtm45PI[ch].prepare (processingSampleRate);

            for (auto* tube : { &powerTubeJTM45A[ch], &powerTubeJTM45B[ch] })
            {
                // Real KT66 (Koren-fit parameters, community-published
                // SPICE model derived from Norman Koren's own methodology
                // -- the same modeling approach every other tube in this
                // file already uses, just not a figure this codebase had
                // independently sourced before now).
                tube->mu = 11.68f; tube->ex = 1.197f; tube->kg1 = 510.9f;
                tube->kg2 = 4500.0f; tube->kp = 34.0f; tube->kvb = 22.3f;
                tube->screenVoltage = 400.0f; // KT66 datasheet-adjacent max screen rating
                tube->Vb = 455.0f;            // schematic's own circled 450V/460V rectifier-area taps
                tube->Ra = 2000.0f;           // ~8k plate-to-plate OT (commonly published JTM45 figure) / 4, same halving rule as elsewhere
                // JTM45 is fixed-bias, not cathode-biased. The old 130-ohm
                // cathode approximation biased this pair far too hot and was
                // the source of its near-constant clipping at tiny inputs.
                tube->Rk = 1.0f;
                tube->gridBiasOffset = -45.0f;
                tube->prepare (processingSampleRate);
            }

            // Mesa/Boogie Mark I Reissue, traced directly from the Gil Ayan
            // drawing in Boogie_mki_reissue.pdf. The normal input path is
            // V1A -> Vol 1 -> V1B -> Fender-derived tone stack -> Vol 2 ->
            // V3A -> PI. The previous implementation accidentally collapsed
            // that to two stages and therefore followed almost exactly the
            // same topology as JTM45. All three triodes on the drawing use
            // 100k plate / 1.5k cathode with a 15uF bypass capacitor. Model
            // the 1.5k DC bias by solving self-bias first, then retain that
            // operating point in the faster bypassed-cathode path.
            triodeMarkOneV1[ch].Vb = 380.0f;
            triodeMarkOneV1[ch].Rk = 1500.0f;
            triodeMarkOneV1[ch].cathodeUnbypassed = true;
            triodeMarkOneV1[ch].prepare (processingSampleRate);
            triodeMarkOneV1[ch].biasPoint = -triodeMarkOneV1[ch].vk0;
            triodeMarkOneV1[ch].cathodeUnbypassed = false;
            triodeMarkOneV1[ch].prepare (processingSampleRate);

            for (auto* stage : { &triodeMarkOneV1B[ch], &triodeMarkOneV3A[ch] })
            {
                stage->Vb = 400.0f;
                stage->Rk = 1500.0f;
                stage->cathodeUnbypassed = true;
                stage->prepare (processingSampleRate);
                stage->biasPoint = -stage->vk0;
                stage->cathodeUnbypassed = false;
                stage->prepare (processingSampleRate);
            }
            markOneInterstageCoupling[ch].prepare (osSpec);

            // Driver/PI -- both schematics show 220k plate resistors on
            // this stage (legible on both scans independently, a rare case
            // of full agreement); tail resistor wasn't legible on either,
            // so this uses the same community-published long-tail-pair
            // figure as JTM45's PI above rather than a guess.
            markOnePI[ch].Ra1 = 220000.0f; markOnePI[ch].Ra2 = 220000.0f;
            markOnePI[ch].Rtail = 10000.0f;
            markOnePI[ch].Vb = 400.0f;
            markOnePI[ch].prepare (processingSampleRate);

            for (auto* tube : { &powerTubeMarkOneA[ch], &powerTubeMarkOneB[ch] })
            {
                // Real 6L6GC (Koren-fit parameters, Cohen & Hélie DAFx-10
                // "Real-Time Simulation of a Guitar Power Amplifier" -- an
                // academic paper citing Koren/Duncan Amps methodology, same
                // kg2=4500 convention already shared by every other power
                // tube in this file). The real amp runs a matched quad (2
                // paralleled pairs), not the 2-tube push-pull this class
                // models per side -- same collapse-the-parallel-pair
                // simplification already used for JTM45/Fender above (this
                // struct represents "one side of the push-pull," already
                // implicitly standing in for whatever real tube count that
                // side has).
                tube->mu = 8.7f; tube->ex = 1.35f; tube->kg1 = 1460.0f;
                tube->kg2 = 4500.0f; tube->kp = 48.0f; tube->kvb = 12.0f;
                tube->screenVoltage = 440.0f; // standard fraction of the plate rail below, same convention as elsewhere
                tube->Vb = 500.0f;  // Boogie's well-documented "hot" house B+ -- every Mark-series 60-100W quad-6L6 amp runs close to this
                tube->Ra = 1000.0f; // ~4k plate-to-plate OT (commonly published Boogie quad-6L6 figure) / 4, same halving rule as elsewhere
                // Fixed bias (external trim pot, not cathode-biased) --
                // -52V is the exact bias-supply figure printed on the Gil
                // Ayan reissue scan's power section.
                tube->Rk = 1.0f;
                tube->gridBiasOffset = -52.0f;
                tube->prepare (processingSampleRate);
            }

            // Roland JC-120 voice -- see Voice::rolandJC120's own comment.
            // Genuinely active EQ (op-amp shelving/peaking, not the passive
            // Fender-family network every tube voice above shares), so
            // these get their own dedicated filters rather than a
            // BassmanToneStack instance. Corner frequencies/Q picked to a
            // plausible, tasteful active-EQ shape (the schematic's exact
            // pot/cap values weren't legible closely enough to transcribe
            // precisely) -- gains themselves are still driven live by
            // ampBass/ampMid/ampTreble via updateJCToneFilters().
            jcBassShelf[ch].prepare (osSpec);
            jcMidPeak[ch].prepare (osSpec);
            jcTrebleShelf[ch].prepare (osSpec);
        }
        // Real blackface component values (see class comment) -- R1/R2/R4
        // reused from the Bassman defaults where no blackface-specific
        // difference was found sourced.
        fenderToneStack.C1 = 0.25e-9; fenderToneStack.C2 = 0.1e-6; fenderToneStack.C3 = 0.047e-6;
        fenderToneStack.R3 = 6800.0;
        // Mark I drawing: 250p/.1u/.047u, 250k Treble, 1M Bass,
        // 10k Mid, and the 100k slope resistor feeding the stack.
        markOneToneStack.C1 = 0.25e-9; markOneToneStack.C2 = 0.1e-6; markOneToneStack.C3 = 0.047e-6;
        markOneToneStack.R1 = 250000.0; markOneToneStack.R2 = 1000000.0;
        markOneToneStack.R3 = 10000.0; markOneToneStack.R4 = 100000.0;
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
        fenderToneStack.updateCoefficients (processingSampleRate, lastBass01, lastMid01, lastTreble01);
        updateVoxToneFilters();
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
            cathodyneStage[ch].reset();
            powerTubeA[ch].reset(); powerTubeB[ch].reset();

            triodeVoxV1[ch].reset(); triodeVoxV2[ch].reset();
            voxInterstageCoupling[ch].reset();
            voxBassShelf[ch].reset(); voxTrebleShelf[ch].reset();
            voxPI[ch].reset();
            powerTubeVoxA[ch].reset(); powerTubeVoxB[ch].reset();

            triodeFenderV1[ch].reset(); triodeFenderV2[ch].reset();
            fenderInterstageCoupling[ch].reset();
            fenderPI[ch].reset();
            powerTubeFenderA[ch].reset(); powerTubeFenderB[ch].reset();

            triodeJTM45V1[ch].reset(); triodeJTM45V2[ch].reset();
            jtm45InterstageCoupling[ch].reset();
            jtm45PI[ch].reset();
            powerTubeJTM45A[ch].reset(); powerTubeJTM45B[ch].reset();

            triodeMarkOneV1[ch].reset(); triodeMarkOneV1B[ch].reset(); triodeMarkOneV3A[ch].reset();
            markOneInterstageCoupling[ch].reset();
            markOnePI[ch].reset();
            powerTubeMarkOneA[ch].reset(); powerTubeMarkOneB[ch].reset();

            jcBassShelf[ch].reset(); jcMidPeak[ch].reset(); jcTrebleShelf[ch].reset();
        }
        bassmanStack.reset();
        fenderToneStack.reset();
        jtm45ToneStack.reset();
        markOneToneStack.reset();
        sagEnvelope = 0.0f;
        sagDetectorLP.fill (0.0f);
        outputGain.setCurrentAndTargetValue (targetOutputGain);
    }

    void setParameters (float drive01, float tone01, float outputDb, Voice voiceIn,
                        float bass01, float mid01, float treble01)
    {
        driveAmount = juce::jlimit (0.0f, 1.0f, drive01);
        gainPotLevel = getGainPotLevel (driveAmount);
        powerDriveAmount = 1.15f + driveAmount * 2.7f;
        standardGainStageLevel = getStandardGainPotLevel (driveAmount);
        jcDistortionGain = 1.0f + 47.0f * driveAmount * driveAmount * driveAmount;
        jcOutputDrive = 0.5f + driveAmount * 1.2f;
        jcOutputNormalise = 1.0f / std::tanh (jcOutputDrive);
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
        // Each voice reads its own, entirely separate tone-stack coefficients
        // (Vintage 5E3: toneFilter, gated on tone01 above; Boutique:
        // bassmanStack; Vox: its dedicated shelf pair via
        // updateVoxToneFilters(); Deluxe 63: fenderToneStack) -- ampBass/
        // ampMid/ampTreble are shared params across the 3 voices that use
        // them, so recomputing all three stacks on every change regardless
        // of which voice is actually selected was pure waste (a real,
        // measurable per-change cost: BassmanToneStack::updateCoefficients
        // alone is a full bilinear-transform of a 3rd-order analog
        // transfer function, not a trivial one-liner -- see its own
        // comment). Only the live voice's own stack is recomputed now;
        // `lastToneStackVoice` also forces one recompute right after a
        // voice switch even when the knobs themselves didn't move, so a
        // freshly-selected voice never plays through stale coefficients
        // left over from whichever voice last actually changed them.
        const auto paramsChanged = ! juce::approximatelyEqual (bass01, lastBass01)
                                 || ! juce::approximatelyEqual (mid01, lastMid01)
                                 || ! juce::approximatelyEqual (treble01, lastTreble01);
        const auto voiceChanged = voice != lastToneStackVoice;
        if (paramsChanged || voiceChanged)
        {
            lastBass01 = bass01; lastMid01 = mid01; lastTreble01 = treble01;
            lastToneStackVoice = voice;
            if (voice == Voice::modern3Band)
                bassmanStack.updateCoefficients (processingSampleRate, lastBass01, lastMid01, lastTreble01);
            else if (voice == Voice::fenderAB763)
                fenderToneStack.updateCoefficients (processingSampleRate, lastBass01, lastMid01, lastTreble01);
            else if (voice == Voice::voxAC30)
                updateVoxToneFilters();
            else if (voice == Voice::jtm45)
                jtm45ToneStack.updateCoefficients (processingSampleRate, lastBass01, lastMid01, lastTreble01);
            else if (voice == Voice::mesaMarkI)
                markOneToneStack.updateCoefficients (processingSampleRate, lastBass01, lastMid01, lastTreble01);
            else if (voice == Voice::rolandJC120)
                updateJCToneFilters();
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
        // Input samples are converted using the Focusrite +12.25dBu=0dBFS
        // headroom reference below. The real amp volume pots sit after their
        // first input stage, so V1 always receives the guitar; Gain controls
        // the signal passed onward to V2 instead of redefining input
        // sensitivity.
        for (int i = 0; i < samples; ++i)
        {
            if (voice == Voice::voxAC30)
            {
                processVoxSample (block, i, channels);
                continue;
            }
            if (voice == Voice::fenderAB763)
            {
                processFenderSample (block, i, channels);
                continue;
            }
            if (voice == Voice::jtm45)
            {
                processJTM45Sample (block, i, channels);
                continue;
            }
            if (voice == Voice::mesaMarkI)
            {
                processMarkOneSample (block, i, channels);
                continue;
            }
            if (voice == Voice::rolandJC120)
            {
                processJCSample (block, i, channels);
                continue;
            }

            float detector = 0.0f;
            std::array<float, 2> phaseInverterPlate {}, phaseInverterCathode {};
            for (int ch = 0; ch < channels; ++ch)
            {
                auto x = inputCoupling[ch].processSample (block.getSample (ch, i));
                const auto v1Input = GuitarSignalLevel::toVolts (x);
                THREADLINE_AMP_ANALYSIS_TAP (ch, v1Input, v1Input);
                auto triode1 = triodeV1[ch].processSample (v1Input) * v1GainCompensation;
                triode1 = interstageCoupling[ch].processSample (triode1);
                THREADLINE_AMP_ANALYSIS_TAP (ch, v1Output, triode1);

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
                    // The real passive Fender/Marshall tone-stack network
                    // this models has substantial broadband insertion loss
                    // by its own nature (verified analytically: ~-11 to
                    // -12dB around 440Hz-1kHz at flat/noon settings, a
                    // genuine property of this passive RC topology, not a
                    // bug). That loss stays before the next nonlinear stage;
                    // voice loudness is handled only by final Output trim.
                    toned = bassmanStack.processSample (ch, triode1);

                // triodeV2A returns real plate-swing volts (can run to tens
                // of volts under drive); outputCalibration/safetyCeiling
                // bring that back to sample scale, harness-verified end to
                // end across driveAmount x input level (same role and same
                // backstop pattern as Klon/TS9's own output calibration).
                const auto v2Input = toned * gainPotLevel;
                THREADLINE_AMP_ANALYSIS_TAP (ch, v2Input, v2Input);
                const auto triode2Raw = triodeV2A[ch].processSample (v2Input);
                const auto triode2 = safetyCeiling * std::tanh (triode2Raw * outputCalibration / safetyCeiling);
                THREADLINE_AMP_ANALYSIS_TAP (ch, v2Output, triode2);

                // Real cathodyne (split-load) phase inverter: a genuine
                // TriodeStage instance with matched Ra=Rk (see prepare()),
                // whose plate and cathode outputs are the two anti-phase
                // drive signals -- verified in a standalone harness to come
                // out ~0.97:1 in amplitude and genuinely antiphase (negative
                // cross-correlation), not assumed.
                const auto piInput = triode2 * cathodyneInputScale;
                THREADLINE_AMP_ANALYSIS_TAP (ch, phaseInverterInput, piInput);
                const auto plateOut = cathodyneStage[ch].processSample (piInput);
                const auto cathOut = cathodyneStage[ch].getCathodeAc();
                THREADLINE_AMP_ANALYSIS_TAP (ch, phaseInverterOutput, plateOut);
                phaseInverterPlate[(size_t) ch] = plateOut;
                phaseInverterCathode[(size_t) ch] = cathOut;

                auto& bassTap = sagDetectorLP[(size_t) ch];
                bassTap += sagDetectorLPCoefficient * (plateOut - bassTap);
                const auto weighted = std::abs (plateOut) * 0.5f + std::abs (bassTap) * 0.5f;
                detector = juce::jmax (detector, weighted);
            }

            const auto coefficient = detector > sagEnvelope ? sagAttack : sagRelease;
            sagEnvelope = coefficient * sagEnvelope + (1.0f - coefficient) * detector;
            const auto sag = juce::jlimit (0.0f, 0.42f,
                sagEnvelope * (0.20f + 0.34f * driveAmount));
            for (int ch = 0; ch < channels; ++ch)
            {
                // Real push-pull: two genuine 6V6GT PentodeStage instances
                // (Koren beam-tetrode model, real 6V6-GTA parameters -- see
                // PentodeStage's class comment), each driven by one of the
                // cathodyne's two real, already-antiphase outputs -- no
                // artificial +V/-V construction needed anymore, the antiphase
                // relationship is now a genuine consequence of the split-load
                // stage above. The output transformer's opposite winding
                // still subtracts their plate outputs: for a matched pair
                // sharing one nonlinearity f, decomposing f = f_odd + f_even
                // gives f(V) - f(-V) = 2*f_odd(V), so even-order content
                // cancels and only odd-order content doubles -- the textbook
                // reason push-pull reads different from single-ended.
                // tubeMismatch (real 6V6 pairs are never perfectly matched)
                // is applied as the *same*-direction grid bias to both tubes
                // rather than mirrored, which breaks that cancellation just
                // enough to leave a little real even-order content rather
                // than a mathematically perfect one -- "asymmetric
                // push-pull", same idea as before, now acting on genuine
                // per-tube grid signals instead of a single shared value.
                const auto effectiveDrive = powerDriveAmount * (1.0f - sag);
                const auto driveScale = effectiveDrive * powerStageInputScale;
                constexpr float tubeMismatch = 0.035f;
                const auto powerInput = phaseInverterPlate[(size_t) ch] * driveScale + tubeMismatch;
                THREADLINE_AMP_ANALYSIS_TAP (ch, powerInput, powerInput);
                const auto tubeA = powerTubeA[ch].processSample (powerInput);
                const auto tubeB = powerTubeB[ch].processSample (
                    phaseInverterCathode[(size_t) ch] * driveScale + tubeMismatch);
                auto power = (tubeA - tubeB) * powerStageOutputScale;
                power /= juce::jmax (0.8f, 0.72f + effectiveDrive * 0.28f);

                // Output-transformer core saturation — a second, distinct
                // compression mechanism from tube sag above (Robinette: "at
                // high volumes, transformer saturation compresses the
                // signal...loud notes are capped but softer notes are still
                // amplified"). Sag is a slow, envelope-driven gain reduction
                // of the drive; this is an instantaneous, level-dependent
                // soft-knee on the transformer's own output, so a hard
                // transient still gets capped even before sag has caught up.
                // A harness sweep confirmed this path's raw output already
                // matches Vox/Deluxe 63's (~0.55 peak at max Drive/loudest
                // input, nearly identical) -- reported as still too quiet
                // regardless, so this is a direct, guaranteed level increase
                // (not a "found bug" fix) applied only to the original
                // Vintage 5E3/Boutique path. Applied BEFORE the OT knee
                // (not after) so the knee's own existing soft-clip still
                // catches the boosted signal at high Drive/loud input
                // rather than letting it clip hard, unclamped, past full
                // scale downstream.
                power *= vintageOutputBoost;
                THREADLINE_AMP_ANALYSIS_TAP (ch, powerOutput, power);

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
            // Per-voice calibration of the user-facing Output control only.
            // This fixed multiplier is after the complete amp model, so it
            // cannot change Gain, tone-stack drive, distortion character,
            // sag, power-stage behaviour, or latency. Deluxe is the 0dB
            // reference; the other offsets are measured from the combined
            // energy of the supplied unnormalised single-coil and P90 DIs at
            // Gain/EQ noon. This is a fixed control calibration, not AGC.
            const auto gain = outputGain.getNextValue()
                            * deluxeReferencedOutput[(size_t) voice];
            for (int ch = 0; ch < channelCount; ++ch)
            {
                auto v = buffer.getSample (ch, i) * gain;
                const auto mag = std::abs (v);
                if (mag > outputLimiterKnee)
                {
                    const auto excess = mag - outputLimiterKnee;
                    const auto headroom = 1.0f - outputLimiterKnee;
                    const auto compressed = outputLimiterKnee + headroom * std::tanh (excess / headroom);
                    v = std::copysign (compressed, v);
                }
                buffer.setSample (ch, i, v);
            }
        }
    }

private:
#if defined(THREADLINE_AMP_ANALYSIS_TAPS)
    void emitAnalysisTap (int channel, AnalysisStage stage, float sample) noexcept
    {
        if (channel == 0 && analysisTapCallback != nullptr)
            analysisTapCallback (analysisTapContext, stage, sample);
    }
#endif

    // Vox AC30 voice's per-sample signal path: two 12AX7 preamp stages
    // (triodeVoxV1/V2) around a Bass/Treble shelf pair, a real long-tailed-
    // pair phase inverter (voxPI), and a genuine EL84 push-pull power stage
    // (powerTubeVoxA/B) -- see Voice::voxAC30's class comment for the full
    // methodology and sourcing. Deliberately mirrors the 5E3 path's own
    // per-sample structure (same sag detector, same output-transformer
    // saturation knee, same shared calibration constants below -- a
    // standalone harness confirmed those same constants land this
    // different circuit's output in the same target range too, so no
    // duplicate Vox-specific calibration constants were needed) rather than
    // trying to unify the two into one parameterised function -- the actual
    // per-tube processing calls differ enough (different tube instances,
    // no tubeMismatch term since the LTP's own inherent asymmetry at
    // higher drive already produces real even-harmonic content on its own,
    // harness-verified) that forcing a shared implementation would obscure
    // more than it would save.
    void processVoxSample (juce::dsp::AudioBlock<float>& block, int i, int channels) noexcept
    {
        float detector = 0.0f;
        std::array<float, 2> plate1 {}, plate2 {};
        for (int ch = 0; ch < channels; ++ch)
        {
            auto x = inputCoupling[ch].processSample (block.getSample (ch, i));
            const auto v1Input = GuitarSignalLevel::toVolts (x);
            THREADLINE_AMP_ANALYSIS_TAP (ch, v1Input, v1Input);
            auto t1 = triodeVoxV1[ch].processSample (v1Input);
            t1 = voxInterstageCoupling[ch].processSample (t1) * standardGainStageLevel;
            THREADLINE_AMP_ANALYSIS_TAP (ch, v1Output, t1);

            auto toned = voxBassShelf[ch].processSample (t1);
            toned = voxTrebleShelf[ch].processSample (toned);

            THREADLINE_AMP_ANALYSIS_TAP (ch, v2Input, toned);
            const auto t2Raw = triodeVoxV2[ch].processSample (toned);
            const auto t2 = safetyCeiling * std::tanh (t2Raw * outputCalibration / safetyCeiling);
            THREADLINE_AMP_ANALYSIS_TAP (ch, v2Output, t2);

            float p1, p2;
            const auto piInput = t2 * cathodyneInputScale;
            THREADLINE_AMP_ANALYSIS_TAP (ch, phaseInverterInput, piInput);
            voxPI[ch].processSample (piInput, p1, p2);
            THREADLINE_AMP_ANALYSIS_TAP (ch, phaseInverterOutput, p1);
            plate1[(size_t) ch] = p1;
            plate2[(size_t) ch] = p2;

            auto& bassTap = sagDetectorLP[(size_t) ch];
            bassTap += sagDetectorLPCoefficient * (p1 - bassTap);
            const auto weighted = std::abs (p1) * 0.5f + std::abs (bassTap) * 0.5f;
            detector = juce::jmax (detector, weighted);
        }

        const auto coefficient = detector > sagEnvelope ? sagAttack : sagRelease;
        sagEnvelope = coefficient * sagEnvelope + (1.0f - coefficient) * detector;
        const auto sag = juce::jlimit (0.0f, 0.42f, sagEnvelope * (0.20f + 0.34f * driveAmount));

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto effectiveDrive = powerDriveAmount * (1.0f - sag);
            const auto driveScale = effectiveDrive * powerStageInputScale;
            const auto powerInput = plate1[(size_t) ch] * driveScale;
            THREADLINE_AMP_ANALYSIS_TAP (ch, powerInput, powerInput);
            const auto tubeA = powerTubeVoxA[ch].processSample (powerInput);
            const auto tubeB = powerTubeVoxB[ch].processSample (plate2[(size_t) ch] * driveScale);
            auto power = (tubeA - tubeB) * powerStageOutputScale;
            power /= juce::jmax (0.8f, 0.72f + effectiveDrive * 0.28f);
            THREADLINE_AMP_ANALYSIS_TAP (ch, powerOutput, power);

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

    // Fender AB763 voice's per-sample signal path -- see Voice::fenderAB763
    // for full methodology/sourcing. Structurally mirrors processVoxSample()
    // (same shared sag detector, OT saturation knee, and calibration
    // constants -- this circuit's real fixed-bias power stage runs a
    // genuinely lower idle current than the 5E3/Vox's self-biased tubes,
    // which in a real amp is exactly why the AB763 has more clean headroom
    // at a given volume; reusing the same calibration constants here is a
    // reasonable starting point rather than a freshly-tuned one, same
    // "first pass, needs auditioning" caveat as the Vox voice), but with
    // its own tube instances and its own real tone stack (fenderToneStack)
    // instead of a Bass/Treble shelf pair.
    void processFenderSample (juce::dsp::AudioBlock<float>& block, int i, int channels) noexcept
    {
        float detector = 0.0f;
        std::array<float, 2> plate1 {}, plate2 {};
        for (int ch = 0; ch < channels; ++ch)
        {
            auto x = inputCoupling[ch].processSample (block.getSample (ch, i));
            const auto v1Input = GuitarSignalLevel::toVolts (x);
            THREADLINE_AMP_ANALYSIS_TAP (ch, v1Input, v1Input);
            auto t1 = triodeFenderV1[ch].processSample (v1Input);
            t1 = fenderInterstageCoupling[ch].processSample (t1) * standardGainStageLevel;
            THREADLINE_AMP_ANALYSIS_TAP (ch, v1Output, t1);

            // Deluxe 63's own real component values give this network even
            // more loss than Boutique's (~-15 to -20dB around 440Hz-1kHz,
            // analytically verified). It deliberately remains before V2 so
            // the passive stack does not become a hidden V2 drive boost.
            const auto toned = fenderToneStack.processSample (ch, t1);

            THREADLINE_AMP_ANALYSIS_TAP (ch, v2Input, toned);
            const auto t2Raw = triodeFenderV2[ch].processSample (toned);
            const auto t2 = safetyCeiling * std::tanh (t2Raw * outputCalibration / safetyCeiling);
            THREADLINE_AMP_ANALYSIS_TAP (ch, v2Output, t2);

            float p1, p2;
            const auto piInput = t2 * cathodyneInputScale;
            THREADLINE_AMP_ANALYSIS_TAP (ch, phaseInverterInput, piInput);
            fenderPI[ch].processSample (piInput, p1, p2);
            THREADLINE_AMP_ANALYSIS_TAP (ch, phaseInverterOutput, p1);
            plate1[(size_t) ch] = p1;
            plate2[(size_t) ch] = p2;

            auto& bassTap = sagDetectorLP[(size_t) ch];
            bassTap += sagDetectorLPCoefficient * (p1 - bassTap);
            const auto weighted = std::abs (p1) * 0.5f + std::abs (bassTap) * 0.5f;
            detector = juce::jmax (detector, weighted);
        }

        const auto coefficient = detector > sagEnvelope ? sagAttack : sagRelease;
        sagEnvelope = coefficient * sagEnvelope + (1.0f - coefficient) * detector;
        const auto sag = juce::jlimit (0.0f, 0.42f, sagEnvelope * (0.20f + 0.34f * driveAmount));

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto effectiveDrive = powerDriveAmount * (1.0f - sag);
            const auto driveScale = effectiveDrive * powerStageInputScale;
            const auto powerInput = plate1[(size_t) ch] * driveScale;
            THREADLINE_AMP_ANALYSIS_TAP (ch, powerInput, powerInput);
            const auto tubeA = powerTubeFenderA[ch].processSample (powerInput);
            const auto tubeB = powerTubeFenderB[ch].processSample (plate2[(size_t) ch] * driveScale);
            auto power = (tubeA - tubeB) * powerStageOutputScale;
            power /= juce::jmax (0.8f, 0.72f + effectiveDrive * 0.28f);
            THREADLINE_AMP_ANALYSIS_TAP (ch, powerOutput, power);

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

    // Marshall JTM45 -- same overall topology shape as Deluxe 63's path
    // above (V1 -> interstage coupling -> shared tone-stack family -> V2 ->
    // long-tail-pair PI -> fixed-bias power pair -> OT saturation), just
    // with this voice's own dedicated stage instances/values (see the
    // Voice::jtm45 prepare() block for full sourcing). jtm45ToneStack reuses
    // BassmanToneStack's own default component values (JTM45's tone stack is
    // a near-verbatim copy of the 5F6-A Bassman's), including its passive
    // insertion loss before V2.
    void processJTM45Sample (juce::dsp::AudioBlock<float>& block, int i, int channels) noexcept
    {
        float detector = 0.0f;
        std::array<float, 2> plate1 {}, plate2 {};
        for (int ch = 0; ch < channels; ++ch)
        {
            auto x = inputCoupling[ch].processSample (block.getSample (ch, i));
            const auto v1Input = GuitarSignalLevel::toVolts (x);
            THREADLINE_AMP_ANALYSIS_TAP (ch, v1Input, v1Input);
            auto t1 = triodeJTM45V1[ch].processSample (v1Input);
            t1 = jtm45InterstageCoupling[ch].processSample (t1) * standardGainStageLevel;
            THREADLINE_AMP_ANALYSIS_TAP (ch, v1Output, t1);

            const auto toned = jtm45ToneStack.processSample (ch, t1);

            THREADLINE_AMP_ANALYSIS_TAP (ch, v2Input, toned);
            const auto t2Raw = triodeJTM45V2[ch].processSample (toned);
            const auto t2 = safetyCeiling * std::tanh (t2Raw * outputCalibration / safetyCeiling);
            THREADLINE_AMP_ANALYSIS_TAP (ch, v2Output, t2);

            float p1, p2;
            const auto piInput = t2 * cathodyneInputScale;
            THREADLINE_AMP_ANALYSIS_TAP (ch, phaseInverterInput, piInput);
            jtm45PI[ch].processSample (piInput, p1, p2);
            THREADLINE_AMP_ANALYSIS_TAP (ch, phaseInverterOutput, p1);
            plate1[(size_t) ch] = p1;
            plate2[(size_t) ch] = p2;

            auto& bassTap = sagDetectorLP[(size_t) ch];
            bassTap += sagDetectorLPCoefficient * (p1 - bassTap);
            const auto weighted = std::abs (p1) * 0.5f + std::abs (bassTap) * 0.5f;
            detector = juce::jmax (detector, weighted);
        }

        const auto coefficient = detector > sagEnvelope ? sagAttack : sagRelease;
        sagEnvelope = coefficient * sagEnvelope + (1.0f - coefficient) * detector;
        const auto sag = juce::jlimit (0.0f, 0.42f, sagEnvelope * (0.20f + 0.34f * driveAmount));

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto effectiveDrive = powerDriveAmount * (1.0f - sag);
            const auto driveScale = effectiveDrive * powerStageInputScale;
            const auto powerInput = plate1[(size_t) ch] * driveScale;
            THREADLINE_AMP_ANALYSIS_TAP (ch, powerInput, powerInput);
            const auto tubeA = powerTubeJTM45A[ch].processSample (powerInput);
            const auto tubeB = powerTubeJTM45B[ch].processSample (plate2[(size_t) ch] * driveScale);
            auto power = (tubeA - tubeB) * powerStageOutputScale;
            power /= juce::jmax (0.8f, 0.72f + effectiveDrive * 0.28f);
            THREADLINE_AMP_ANALYSIS_TAP (ch, powerOutput, power);

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

    // Mesa/Boogie Mark I Reissue normal-input path. Unlike the JTM45 branch,
    // this keeps the schematic's extra cascaded V1B stage before its own
    // Fender-derived tone stack and V3A stage after Volume 2. Gain maps to
    // the real Vol 1 position after V1A. Vol 2 is held at a fixed noon
    // audio-taper setting because Threadline exposes one circuit Gain plus a
    // separate, strictly post-model Output knob; this avoids tying two pots
    // into an artificial x^14 gain law while retaining the genuine cascade.
    void processMarkOneSample (juce::dsp::AudioBlock<float>& block, int i, int channels) noexcept
    {
        float detector = 0.0f;
        std::array<float, 2> plate1 {}, plate2 {};
        for (int ch = 0; ch < channels; ++ch)
        {
            auto x = inputCoupling[ch].processSample (block.getSample (ch, i));
            const auto v1Input = GuitarSignalLevel::toVolts (x);
            THREADLINE_AMP_ANALYSIS_TAP (ch, v1Input, v1Input);
            auto v1a = triodeMarkOneV1[ch].processSample (v1Input);
            v1a = markOneInterstageCoupling[ch].processSample (v1a) * standardGainStageLevel;
            THREADLINE_AMP_ANALYSIS_TAP (ch, v1Output, v1a);

            const auto v1b = triodeMarkOneV1B[ch].processSample (v1a);
            const auto toned = markOneToneStack.processSample (ch, v1b);
            const auto v3Input = toned * markOneVolumeTwoLevel;
            THREADLINE_AMP_ANALYSIS_TAP (ch, v2Input, v3Input);
            const auto v3Raw = triodeMarkOneV3A[ch].processSample (v3Input);
            const auto v3 = safetyCeiling * std::tanh (v3Raw * outputCalibration / safetyCeiling);
            THREADLINE_AMP_ANALYSIS_TAP (ch, v2Output, v3);

            float p1, p2;
            const auto piInput = v3 * cathodyneInputScale;
            THREADLINE_AMP_ANALYSIS_TAP (ch, phaseInverterInput, piInput);
            markOnePI[ch].processSample (piInput, p1, p2);
            THREADLINE_AMP_ANALYSIS_TAP (ch, phaseInverterOutput, p1);
            plate1[(size_t) ch] = p1;
            plate2[(size_t) ch] = p2;

            auto& bassTap = sagDetectorLP[(size_t) ch];
            bassTap += sagDetectorLPCoefficient * (p1 - bassTap);
            const auto weighted = std::abs (p1) * 0.5f + std::abs (bassTap) * 0.5f;
            detector = juce::jmax (detector, weighted);
        }

        const auto coefficient = detector > sagEnvelope ? sagAttack : sagRelease;
        sagEnvelope = coefficient * sagEnvelope + (1.0f - coefficient) * detector;
        // The Mark I drawing uses a four-diode solid-state rectifier, unlike
        // JTM45's GZ34. Retain supply movement, but make it substantially
        // stiffer than the Marshall's 42% ceiling/tube-rectifier bloom.
        const auto sag = juce::jlimit (0.0f, 0.12f, sagEnvelope * (0.06f + 0.10f * driveAmount));

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto effectiveDrive = powerDriveAmount * (1.0f - sag);
            const auto driveScale = effectiveDrive * powerStageInputScale;
            const auto powerInput = plate1[(size_t) ch] * driveScale;
            THREADLINE_AMP_ANALYSIS_TAP (ch, powerInput, powerInput);
            const auto tubeA = powerTubeMarkOneA[ch].processSample (powerInput);
            const auto tubeB = powerTubeMarkOneB[ch].processSample (plate2[(size_t) ch] * driveScale);
            auto power = (tubeA - tubeB) * powerStageOutputScale;
            power /= juce::jmax (0.8f, 0.72f + effectiveDrive * 0.28f);
            THREADLINE_AMP_ANALYSIS_TAP (ch, powerOutput, power);

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

    // Roland JC-120 -- entirely solid-state, no tube modeling at all,
    // unlike every other voice in this file. Sourced from the real Dec-
    // 1984 factory service-manual schematic (el34world Roland archive): an
    // op-amp input/gain stage (IC2a/IC2b), an active op-amp-driven Bass/
    // Middle/Treble EQ (IC3a/IC3b -- see updateJCToneFilters()'s own
    // comment for why that's modeled with dedicated filters rather than
    // the passive Fender-family stack every tube voice above shares), and
    // a solid-state push-pull output stage. Deliberately skips the shared
    // sagEnvelope/sagDetectorLP machinery every tube voice above uses -- a
    // transistor power supply genuinely doesn't sag under load the way a
    // tube rectifier/transformer does. The Dec-1984 schematic's IC2a
    // distortion stage (47k feedback path, VR1 50kC and antiparallel
    // two-diode strings D7-D10) is included below; chorus and spring remain
    // separate pedals elsewhere on the board.
    void processJCSample (juce::dsp::AudioBlock<float>& block, int i, int channels) noexcept
    {
        // IC2a's closed-loop maximum is approximately 1 + 47k/1k = 48.
        // VR1 is C-taper, represented by the cubic sweep, and the two series
        // silicon diodes in each direction place the feedback knee around
        // +/-1.2V. Convert the raw Focusrite DI to circuit volts, process it,
        // then return to digital scale.
        constexpr float diodeKneeVolts = 1.2f;

        std::array<float, 2> eqOut {};
        for (int ch = 0; ch < channels; ++ch)
        {
            auto x = inputCoupling[ch].processSample (block.getSample (ch, i));
            const auto v1Input = GuitarSignalLevel::toVolts (x);
            THREADLINE_AMP_ANALYSIS_TAP (ch, v1Input, v1Input);
            const auto amplifiedVolts = v1Input * jcDistortionGain;
            THREADLINE_AMP_ANALYSIS_TAP (ch, v1Output, amplifiedVolts);
            THREADLINE_AMP_ANALYSIS_TAP (ch, v2Input, amplifiedVolts);
            auto driven = diodeKneeVolts * std::tanh (amplifiedVolts / diodeKneeVolts)
                        * GuitarSignalLevel::digitalUnitsPerVolt;
            THREADLINE_AMP_ANALYSIS_TAP (ch, v2Output, driven);
            THREADLINE_AMP_ANALYSIS_TAP (ch, phaseInverterInput, driven);
            driven = jcBassShelf[ch].processSample (driven);
            driven = jcMidPeak[ch].processSample (driven);
            driven = jcTrebleShelf[ch].processSample (driven);
            THREADLINE_AMP_ANALYSIS_TAP (ch, phaseInverterOutput, driven);
            eqOut[(size_t) ch] = driven;
        }

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto powerInput = eqOut[(size_t) ch] * jcOutputDrive;
            THREADLINE_AMP_ANALYSIS_TAP (ch, powerInput, powerInput);
            const auto power = std::tanh (powerInput) * jcOutputNormalise;
            THREADLINE_AMP_ANALYSIS_TAP (ch, powerOutput, power);
            block.setSample (ch, i, transformerLowPass[ch].processSample (power));
        }
    }

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
            // Audit-caught: these were prepared but never given
            // coefficients, silently sitting at JUCE's default identity/
            // passthrough response despite their names and class comment
            // implying real DC-blocking/coupling work. Not currently
            // audible (TriodeStage's own output is already zero-mean AC-
            // only by construction), but real dead code masquerading as
            // live filtering -- same corner as the 5E3 path's own
            // interstageCoupling, since both play the identical role.
            *voxInterstageCoupling[ch].coefficients = *couplingHP;
            *fenderInterstageCoupling[ch].coefficients = *couplingHP;
            *jtm45InterstageCoupling[ch].coefficients = *couplingHP;
            *markOneInterstageCoupling[ch].coefficients = *couplingHP;
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

    // Vox voice's simplified Bass/Treble shelf pair.
    void updateVoxToneFilters()
    {
        if (processingSampleRate <= 0.0)
            return;
        const auto bassGainDb = juce::jmap (lastBass01, -12.0f, 12.0f);
        const auto trebleGainDb = juce::jmap (lastTreble01, -12.0f, 12.0f);
        auto bassCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            processingSampleRate, 200.0f, 0.65f, juce::Decibels::decibelsToGain (bassGainDb));
        auto trebleCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            processingSampleRate, 2200.0f, 0.65f, juce::Decibels::decibelsToGain (trebleGainDb));
        for (auto& f : voxBassShelf) *f.coefficients = *bassCoeffs;
        for (auto& f : voxTrebleShelf) *f.coefficients = *trebleCoeffs;
    }

    // Roland JC-120 voice's active Bass/Middle/Treble EQ -- see the
    // Voice::rolandJC120 prepare() block's own comment for why this is a
    // dedicated shelf/peak trio rather than a BassmanToneStack instance.
    void updateJCToneFilters()
    {
        if (processingSampleRate <= 0.0)
            return;
        const auto bassGainDb = juce::jmap (lastBass01, -10.0f, 10.0f);
        const auto midGainDb = juce::jmap (lastMid01, -8.0f, 8.0f);
        const auto trebleGainDb = juce::jmap (lastTreble01, -10.0f, 10.0f);
        auto bassCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            processingSampleRate, 150.0f, 0.65f, juce::Decibels::decibelsToGain (bassGainDb));
        auto midCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            processingSampleRate, 750.0f, 0.9f, juce::Decibels::decibelsToGain (midGainDb));
        auto trebleCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            processingSampleRate, 3200.0f, 0.65f, juce::Decibels::decibelsToGain (trebleGainDb));
        for (auto& f : jcBassShelf) *f.coefficients = *bassCoeffs;
        for (auto& f : jcMidPeak) *f.coefficients = *midCoeffs;
        for (auto& f : jcTrebleShelf) *f.coefficients = *trebleCoeffs;
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

        // Cathode-degeneration mode (V1 only -- see class comment). Rk is
        // the real, unbypassed 5E3 V1 cathode resistor (Robinette
        // schematic). Unused when cathodeUnbypassed is false (V2A).
        bool cathodeUnbypassed = false;
        float Rk = 1500.0f;
        float vk = 0.0f, vk0 = 0.0f;

        float vaAcPrev = 0.0f, gridCharge = 0.0f, iAcPrev = 0.0f;
        float Va0 = 150.0f, Ia0 = 0.0f, restingGridCharge = 0.0f;
        double sampleRate = 44100.0;
        // Grid-charge one-pole decay coefficient -- depends only on
        // sampleRate/Rg/Cin, none of which change between prepare() calls,
        // so this is cached there once rather than calling std::exp() on
        // every single processSample() (audio-rate, this ran millions of
        // times a second computing the exact same value every time).
        float gridChargeLeaky = 0.0f;

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
            return anodeCurrentGivenGridCurrent (vg, va, gridCurrent (vg));
        }

        // Same equation as anodeCurrent(), but takes an already-computed
        // gridCurrent(vg) instead of recomputing it -- processSample() below
        // needs gridCurrent(vg) for its own blocking-distortion charge
        // target AND for the anode current, with the same vg both times;
        // routing both through this shared helper means that (genuinely
        // costly: a softplus() -- exp + log1p -- plus a pow()) call happens
        // once per sample instead of twice.
        float anodeCurrentGivenGridCurrent (float vg, float va, float gc) const noexcept
        {
            const auto veff = vg + va / mu;
            return G * std::pow (softplus (veff, C), gamma) - gc;
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
            if (! cathodeUnbypassed)
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
                return;
            }

            // Self-biased (cathode-unbypassed) path: there is no external
            // biasPoint -- the resting Vgk instead EMERGES from Ik0*Rk, so
            // the plate current (through Ra) and cathode current (through
            // Rk) have to settle simultaneously. Nested bisection: the outer
            // search is over the cathode voltage Vk0, the inner search is
            // the same plate-voltage bisection the fixed-bias path uses
            // above, run fresh for each Vk0 trial. Rising Vk0 always pulls
            // Vgk0 (= -Vk0 here, grid resting at ~0V through Rg to ground)
            // down and therefore total cathode current down too, so both
            // searches are monotonic.
            restingGridCharge = 0.0f;
            for (int iter = 0; iter < 20; ++iter)
            {
                const auto vgActual0 = -restingGridCharge;
                float vkLo = 0.0f, vkHi = Vb;
                for (int outer = 0; outer < 40; ++outer)
                {
                    const auto vkMid = 0.5f * (vkLo + vkHi);
                    const auto vgk = vgActual0 - vkMid;
                    float lo = 0.0f, hi = Vb;
                    for (int i = 0; i < 60; ++i)
                    {
                        const auto mid = 0.5f * (lo + hi);
                        if (anodeCurrent (vgk, mid) > (Vb - mid) / Ra)
                            hi = mid;
                        else
                            lo = mid;
                    }
                    const auto vaTrial = 0.5f * (lo + hi);
                    // Total cathode current == anode + grid current, which
                    // is exactly G*softplus(...)^gamma -- the gridCurrent
                    // term anodeCurrent() subtracts out cancels back in.
                    const auto totalCathodeCurrent = G * std::pow (softplus (vgk + vaTrial / mu, C), gamma);
                    if (totalCathodeCurrent * Rk > vkMid)
                        vkLo = vkMid;
                    else
                        vkHi = vkMid;
                }
                vk0 = 0.5f * (vkLo + vkHi);
                const auto vgk0 = vgActual0 - vk0;
                float lo = 0.0f, hi = Vb;
                for (int i = 0; i < 60; ++i)
                {
                    const auto mid = 0.5f * (lo + hi);
                    if (anodeCurrent (vgk0, mid) > (Vb - mid) / Ra)
                        hi = mid;
                    else
                        lo = mid;
                }
                Va0 = 0.5f * (lo + hi);
                Ia0 = anodeCurrent (vgk0, Va0);
                restingGridCharge = gridCurrent (vgk0) * Rg;
            }
        }

        void prepare (double newSampleRate) noexcept
        {
            sampleRate = newSampleRate;
            gridChargeLeaky = std::exp (-1.0f / (float) (sampleRate * Rg * Cin));
            solveBiasPoint();
            reset();
        }

        void reset() noexcept { vaAcPrev = 0.0f; gridCharge = restingGridCharge; iAcPrev = 0.0f; vk = vk0; }

        // The cathode's own AC deviation -- only meaningful when
        // cathodeUnbypassed is set (V1, and the cathodyne phase inverter
        // below, which needs BOTH this and the plate output as its two
        // antiphase drive signals; V2A never reads it). Valid after
        // processSample() has run for this sample.
        float getCathodeAc() const noexcept { return vk - vk0; }

        // vin: signal voltage arriving at this stage's grid (real volts,
        // not a normalised sample). Returns the plate's AC voltage swing
        // (also real volts -- can run to tens of volts under drive), still
        // needing the caller's own output calibration back to sample scale.
        float processSample (float vin) noexcept
        {
            float vg;
            if (cathodeUnbypassed)
            {
                // Self-biasing cathode: solve Ik(Vgk,Va) == Vk/Rk for Vk via
                // a warm-started Newton-Raphson iteration (Vk barely moves
                // sample to sample at audio rates, so a handful of
                // iterations converges tightly). Va uses the PREVIOUS
                // sample's plate voltage, same one-sample delay the plate
                // network below already relies on, so this stays a 1-D solve
                // instead of a simultaneous 2-unknown (Vk, Va) one.
                // 2 iterations (was 4): these loops are ~2/3 of the whole
                // module's CPU (measured -- cutting them to 1 dropped 4x from
                // 56% to 18%), and the warm start means 2 converges to within
                // 0.05% RMS of the 4-iteration result across all 7 voices
                // (AmpIterFidelity sweep). Same reasoning sets the cathodyne
                // to 3 and the pentode to 4 below.
                const auto vgActual = vin - gridCharge;
                const auto va = Va0 + vaAcPrev;
                auto vkGuess = vk;
                for (int iter = 0; iter < 2; ++iter)
                {
                    const auto vgkTrial = vgActual - vkGuess;
                    const auto x = vgkTrial + va / mu;
                    float h, sigmoid;
                    softplusSigmoid (x, C, h, sigmoid);
                    const auto ik = G * std::pow (h, gamma);
                    // h^(gamma-1) == h^gamma / h (h > 0 always), so the
                    // derivative term is gamma*ik/h*sigmoid -- one division
                    // replaces a second std::pow() per Newton iteration.
                    const auto dIkDVgk = gamma * ik / h * sigmoid;
                    const auto g = ik - vkGuess / Rk;
                    const auto dg = -dIkDVgk - 1.0f / Rk;
                    if (std::abs (dg) > 1.0e-12f)
                        vkGuess -= g / dg;
                }
                vk = juce::jlimit (0.0f, Vb, vkGuess);
                vg = vgActual - vk;
            }
            else
            {
                vg = vin - gridCharge + biasPoint;
            }

            // Blocking distortion: grid current charges the input coupling
            // cap through Rg, chasing a target of Ig(Vg)*Rg (Ohm's law
            // across the grid-leak resistor) with time constant Rg*Cin.
            // Computed once and reused below -- see
            // anodeCurrentGivenGridCurrent()'s own comment.
            const auto gc = gridCurrent (vg);
            const auto targetCharge = gc * Rg;
            gridCharge = gridChargeLeaky * gridCharge + (1.0f - gridChargeLeaky) * targetCharge;

            const auto iaAc = anodeCurrentGivenGridCurrent (vg, Va0 + vaAcPrev, gc) - Ia0;

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

    // Long-tailed-pair (differential/cathode-coupled) phase inverter: two
    // triodes sharing one cathode resistor ("tail") to ground, one grid
    // driven by the signal, the other referenced to a fixed grid-bias
    // voltage (approximated here as a fixed DC value -- the real feedback-
    // derived bias network is a topology detail beyond this pass's scope,
    // same "honestly flagged" treatment as this file's other approximations).
    // Reuses the exact same Dempwolf-Zolzer 12AX7 current equations
    // TriodeStage above uses (this is architecturally still a 12AX7, just
    // two of them sharing a cathode node instead of one self-biased alone),
    // solving ONE shared cathode voltage from BOTH tubes' current sum via
    // Kirchhoff's current law at that node: Ik1(Vg1,Vk,Va1) +
    // Ik2(Vg2,Vk,Va2) == Vk/Rtail, via the same warm-started Newton-Raphson
    // technique TriodeStage's own self-bias mode already uses for one tube.
    // This is what makes the LTP's two plate outputs genuinely antiphase
    // from real circuit behaviour (grid1 rising pulls the shared cathode
    // up, which lowers grid2's effective Vgk and its current) rather than
    // a constructed +V/-V pair -- verified in a standalone harness across
    // a Drive sweep (0.05-8.0): the two plate outputs' cross-correlation
    // stayed negative (genuinely antiphase) at every level tested, the DC
    // bias-point solve converged to sane values (Vk0=3.15V, both plates
    // resting at ~271.7V with Vb=275V), and output stayed bounded with no
    // NaN/blow-up even well beyond realistic drive levels. No grid-current/
    // blocking-distortion model here (unlike TriodeStage) -- the PI stage
    // is driven by a much lower-level signal than the input stage typically
    // sees pre-clipping, so its own grid rarely approaches conduction in
    // normal operation, the same justification PentodeStage below gives for
    // skipping it on the power tubes.
    struct LongTailPairStage
    {
        float Ra1 = 100000.0f, Ra2 = 100000.0f;
        float Rtail = 47000.0f; // real AC30 LTP tail resistor (ampbooks.com)
        float Vb = 275.0f;
        // Both grids in a real capacitor-coupled LTP sit on an elevated DC
        // bias network; only their AC difference drives the pair. The old
        // model fixed both at 0V while returning the tail to ground, which
        // left both plates almost at B+ (near cutoff) and made even a 25mV
        // guitar signal produce double-digit THD. gridCommonBias is solved
        // at prepare() to centre the plates, standing in for that omitted DC
        // divider without touching the AC signal path.
        float gridCommonBias = 0.0f;
        float Cout = 220.0e-12f; // same numerical-stabiliser role as TriodeStage's own Cout

        float vk = 0.0f, vk0 = 0.0f;
        float va1AcPrev = 0.0f, ia1AcPrev = 0.0f, Va10 = 150.0f, Ia10 = 0.0f;
        float va2AcPrev = 0.0f, ia2AcPrev = 0.0f, Va20 = 150.0f, Ia20 = 0.0f;
        double sampleRate = 44100.0;

        static float softplus (float x, float k) noexcept
        {
            const auto kx = k * x;
            return (std::max (kx, 0.0f) + std::log1p (std::exp (-std::abs (kx)))) / k;
        }
        static float anodeCurrentStatic (float vg, float va) noexcept
        {
            constexpr float G = 1.371e-3f, mu = 96.2f, gamma = 1.349f, C = 3.917f;
            const auto veff = vg + va / mu;
            return G * std::pow (softplus (veff, C), gamma);
        }

        float solvePlate (float vgk, float Ra) const noexcept
        {
            float lo = 0.0f, hi = Vb;
            for (int i = 0; i < 60; ++i)
            {
                const auto mid = 0.5f * (lo + hi);
                if (anodeCurrentStatic (vgk, mid) > (Vb - mid) / Ra) hi = mid; else lo = mid;
            }
            return 0.5f * (lo + hi);
        }

        // Outer bisection over the shared cathode voltage Vk0; inner
        // bisection solves each tube's own quiescent plate voltage given
        // that Vk0 trial -- same nested structure TriodeStage's cathode-
        // unbypassed solve already uses, just summing two tubes' currents
        // for the tail-resistor balance instead of one.
        void solveBiasPointAtCurrentGridBias() noexcept
        {
            float vkLo = 0.0f, vkHi = Vb;
            for (int outer = 0; outer < 40; ++outer)
            {
                const auto vkMid = 0.5f * (vkLo + vkHi);
                const auto vgk1 = gridCommonBias - vkMid;
                const auto vgk2 = gridCommonBias - vkMid;
                const auto va1 = solvePlate (vgk1, Ra1);
                const auto va2 = solvePlate (vgk2, Ra2);
                const auto total = anodeCurrentStatic (vgk1, va1) + anodeCurrentStatic (vgk2, va2);
                if (total * Rtail > vkMid) vkLo = vkMid; else vkHi = vkMid;
            }
            vk0 = 0.5f * (vkLo + vkHi);
            const auto vgk1_0 = gridCommonBias - vk0, vgk2_0 = gridCommonBias - vk0;
            Va10 = solvePlate (vgk1_0, Ra1); Ia10 = anodeCurrentStatic (vgk1_0, Va10);
            Va20 = solvePlate (vgk2_0, Ra2); Ia20 = anodeCurrentStatic (vgk2_0, Va20);
        }

        void prepare (double newSampleRate) noexcept
        {
            sampleRate = newSampleRate;

            // Reconstruct the omitted bias divider from a centred target
            // plate current, then run the normal coupled operating-point
            // solve once. 60% of B+ leaves useful headroom in both swing
            // directions. This direct current calculation replaces an older
            // nested bias-search that was correct but needlessly expensive
            // during repeated plugin/validator preparation.
            constexpr float targetPlateFraction = 0.60f;
            const auto targetPlate = Vb * targetPlateFraction;
            const auto targetIa1 = (Vb - targetPlate) / Ra1;
            const auto targetIa2 = (Vb - targetPlate) / Ra2;
            const auto targetIaMean = 0.5f * (targetIa1 + targetIa2);

            float vgkLo = -20.0f, vgkHi = 5.0f;
            for (int iter = 0; iter < 40; ++iter)
            {
                const auto vgk = 0.5f * (vgkLo + vgkHi);
                if (anodeCurrentStatic (vgk, targetPlate) < targetIaMean)
                    vgkLo = vgk;
                else
                    vgkHi = vgk;
            }
            const auto targetVgk = 0.5f * (vgkLo + vgkHi);
            const auto targetCathode = (targetIa1 + targetIa2) * Rtail;
            gridCommonBias = targetCathode + targetVgk;
            solveBiasPointAtCurrentGridBias();
            reset();
        }
        void reset() noexcept
        {
            vk = vk0;
            va1AcPrev = va2AcPrev = 0.0f;
            ia1AcPrev = ia2AcPrev = 0.0f;
        }

        // vin: the driven grid's signal voltage (real volts). Writes both
        // plates' AC voltage swings -- genuinely antiphase, verified as
        // described in the class comment above.
        void processSample (float vin, float& plate1Out, float& plate2Out) noexcept
        {
            const auto vgActual1 = gridCommonBias + vin;
            const auto vgActual2 = gridCommonBias;
            const auto va1 = Va10 + va1AcPrev;
            const auto va2 = Va20 + va2AcPrev;
            constexpr float G = 1.371e-3f, mu = 96.2f, gamma = 1.349f, C = 3.917f;

            auto vkGuess = vk;
            for (int iter = 0; iter < 3; ++iter)
            {
                const auto vgk1 = vgActual1 - vkGuess;
                const auto vgk2 = vgActual2 - vkGuess;
                const auto x1 = vgk1 + va1 / mu, x2 = vgk2 + va2 / mu;
                float h1, h2, sig1, sig2;
                softplusSigmoid (x1, C, h1, sig1);
                softplusSigmoid (x2, C, h2, sig2);
                const auto ik1 = G * std::pow (h1, gamma), ik2 = G * std::pow (h2, gamma);
                // h^(gamma-1) == h^gamma / h (h > 0) -- one division per tube
                // replaces a std::pow() per Newton iteration.
                const auto dIk1 = gamma * ik1 / h1 * sig1;
                const auto dIk2 = gamma * ik2 / h2 * sig2;
                const auto g = ik1 + ik2 - vkGuess / Rtail;
                const auto dg = -dIk1 - dIk2 - 1.0f / Rtail;
                if (std::abs (dg) > 1.0e-12f) vkGuess -= g / dg;
            }
            vk = juce::jlimit (0.0f, Vb, vkGuess);

            const auto vgk1 = vgActual1 - vk, vgk2 = vgActual2 - vk;
            const auto ia1Ac = anodeCurrentStatic (vgk1, va1) - Ia10;
            const auto ia2Ac = anodeCurrentStatic (vgk2, va2) - Ia20;

            const double K1 = 2.0 * sampleRate * (double) Ra1 * (double) Cout;
            const double va1AcD = ((double) Ra1 * (-(double) ia1Ac + (double) ia1AcPrev)
                                   - (1.0 - K1) * (double) va1AcPrev) / (1.0 + K1);
            const double K2 = 2.0 * sampleRate * (double) Ra2 * (double) Cout;
            const double va2AcD = ((double) Ra2 * (-(double) ia2Ac + (double) ia2AcPrev)
                                   - (1.0 - K2) * (double) va2AcPrev) / (1.0 + K2);

            plate1Out = (float) va1AcD;
            plate2Out = (float) va2AcD;
            ia1AcPrev = -ia1Ac; va1AcPrev = plate1Out;
            ia2AcPrev = -ia2Ac; va2AcPrev = plate2Out;
        }
    };

    // 6V6GT beam-tetrode power tube: Norman Koren's pentode/beam-tetrode
    // SPICE model (E1 = Vg2/Kp * ln(1+exp(Kp*(1/mu + Vg1/Vg2))), Ia =
    // E1^Ex/Kg1 * atan(Va/Kvb), Ig2 = max(0, Vg2/mu + Vg1)^Ex / Kg2), a
    // different model family from TriodeStage above because a beam tetrode
    // is different physics: the screen grid shields the control grid from
    // the plate's influence, so plate current becomes nearly independent
    // of plate voltage once past the arctan "knee" (kVB) instead of rising
    // with it via mu the way a triode's does -- that's the mechanism behind
    // a pentode/tetrode power stage's higher output and different
    // compression character. Equation confirmed against an academic source
    // reproducing Koren's original 1996 Glass Audio formulas verbatim
    // (Vanderkooy-style SPICE tube modeling literature review); parameters
    // (MU=10.70, EX=1.310, KG1=1672.0, KG2=4500, KP=41.16, KVB=12.7) are
    // the real 6V6-GTA fit from Koren's own published SPICE library
    // (Koren_Tubes.cir, GE datasheet fit) -- not guessed or curve-fit here.
    // Self-biased (Rk=250, the real 5E3's shared 6V6-pair cathode resistor
    // per Robinette, applied per-tube as a reasonable approximation since
    // modeling the two tubes' actual shared-resistor coupling would be a
    // topology change beyond this pass's scope -- see the class comment).
    // Screen voltage is held fixed (295V, Robinette) rather than given its
    // own sag dynamics, for the same reason: the existing sag detector
    // already models supply sag heuristically, and changing that mechanism
    // is a topology change this pass doesn't make. Same one-sample-delayed
    // plate network technique as TriodeStage, and for the same reason
    // (avoids a simultaneous Va/Vk solve); the pentode's arctan(Va/Kvb)
    // term is far less Va-sensitive than a triode's linear mu term, so
    // this loop is if anything better-damped, not worse.
    struct PentodeStage
    {
        float mu = 10.70f, ex = 1.310f, kg1 = 1672.0f, kg2 = 4500.0f, kp = 41.16f, kvb = 12.7f;
        float screenVoltage = 295.0f;
        float Vb = 373.0f;
        float Ra = 2000.0f; // OT reflected per-tube plate load: 8k P-P primary / 4
        float Rk = 250.0f;
        float Rg = 1.0e6f, Cin = 0.02e-6f;
        float Cout = 220.0e-12f;
        // Fixed-bias support (0 = self-biased, the 5E3/Vox default): a real
        // fixed-bias power stage (no cathode resistor -- an external
        // negative supply feeds the grids directly, cathode near ground)
        // is modeled here by setting Rk to a near-zero value (forces the
        // self-bias solve's own Vk to converge to ~0V regardless of
        // current, same effect as a grounded cathode) and gridBiasOffset
        // to the real fixed bias voltage (negative) -- applied into both
        // the DC operating-point solve and the per-sample grid voltage
        // below, so the tube's actual quiescent point reflects the real
        // fixed bias rather than a self-derived one.
        float gridBiasOffset = 0.0f;

        float vaAcPrev = 0.0f, gridCharge = 0.0f, iAcPrev = 0.0f;
        float Va0 = 150.0f, Ia0 = 0.0f, restingGridCharge = 0.0f;
        float vk = 0.0f, vk0 = 0.0f;
        double sampleRate = 44100.0;

        static float softplus (float x, float k) noexcept
        {
            const auto kx = k * x;
            return (std::max (kx, 0.0f) + std::log1p (std::exp (-std::abs (kx)))) / k;
        }

        float e1 (float vg1) const noexcept { return screenVoltage * softplus (1.0f / mu + vg1 / screenVoltage, kp); }
        float plateCurrent (float vg1, float va) const noexcept
        {
            return std::pow (e1 (vg1), ex) / kg1 * std::atan (va / kvb);
        }
        float screenCurrent (float vg1) const noexcept
        {
            const auto y = screenVoltage / mu + vg1;
            return y > 0.0f ? std::pow (y, ex) / kg2 : 0.0f;
        }
        float cathodeCurrent (float vg1, float va) const noexcept
        {
            return plateCurrent (vg1, va) + screenCurrent (vg1);
        }

        // Nested bisection, same structure as TriodeStage's cathode-
        // unbypassed solve: outer over Vk0, inner over Va0, since plate
        // current (through Ra) and cathode current (through Rk, which for
        // a self-biased pentode/tetrode includes screen current too) must
        // settle simultaneously.
        void solveBiasPoint() noexcept
        {
            restingGridCharge = 0.0f;
            for (int iter = 0; iter < 20; ++iter)
            {
                const auto vgActual0 = gridBiasOffset - restingGridCharge;
                float vkLo = 0.0f, vkHi = Vb;
                for (int outer = 0; outer < 40; ++outer)
                {
                    const auto vkMid = 0.5f * (vkLo + vkHi);
                    const auto vgk = vgActual0 - vkMid;
                    float lo = 0.0f, hi = Vb;
                    for (int i = 0; i < 60; ++i)
                    {
                        const auto mid = 0.5f * (lo + hi);
                        if (plateCurrent (vgk, mid) > (Vb - mid) / Ra)
                            hi = mid;
                        else
                            lo = mid;
                    }
                    const auto vaTrial = 0.5f * (lo + hi);
                    if (cathodeCurrent (vgk, vaTrial) * Rk > vkMid)
                        vkLo = vkMid;
                    else
                        vkHi = vkMid;
                }
                vk0 = 0.5f * (vkLo + vkHi);
                const auto vgk0 = vgActual0 - vk0;
                float lo = 0.0f, hi = Vb;
                for (int i = 0; i < 60; ++i)
                {
                    const auto mid = 0.5f * (lo + hi);
                    if (plateCurrent (vgk0, mid) > (Vb - mid) / Ra)
                        hi = mid;
                    else
                        lo = mid;
                }
                Va0 = 0.5f * (lo + hi);
                Ia0 = plateCurrent (vgk0, Va0);
                // Grid current at these bias points is negligible (deep
                // negative grid at idle), so this converges trivially --
                // kept for structural parity with TriodeStage's solve.
                restingGridCharge = 0.0f;
            }
        }

        void prepare (double newSampleRate) noexcept
        {
            sampleRate = newSampleRate;
            solveBiasPoint();
            reset();
        }

        void reset() noexcept { vaAcPrev = 0.0f; gridCharge = restingGridCharge; iAcPrev = 0.0f; vk = vk0; }

        float processSample (float vin) noexcept
        {
            const auto vgActual = vin + gridBiasOffset - gridCharge;
            const auto va = Va0 + vaAcPrev;
            // Loop-invariant: va (and therefore atanVa) never changes across
            // the Newton-Raphson iterations below -- only vkGuess does. Was
            // being recomputed on every one of the 6 iterations even though
            // std::atan() only ever needed evaluating once per
            // processSample() call.
            const auto atanVa = std::atan (va / kvb);
            auto vkGuess = vk;
            for (int iter = 0; iter < 4; ++iter)
            {
                const auto vgk = vgActual - vkGuess;
                const auto x = 1.0f / mu + vgk / screenVoltage;
                float h, sig;
                softplusSigmoid (x, kp, h, sig);
                const auto e1v = screenVoltage * h;
                const auto ia = std::pow (e1v, ex) / kg1 * atanVa;
                // e1v^(ex-1) == e1v^ex / e1v (e1v > 0 always) -- reuse ia/e1v
                // instead of a second std::pow() per Newton iteration.
                const auto dIaDVgk = ex * ia / e1v * sig;

                const auto y = screenVoltage / mu + vgk;
                const auto ig2 = y > 0.0f ? std::pow (y, ex) / kg2 : 0.0f;
                // y^(ex-1) == y^ex / y (y > 0 in this branch) -- ig2/y instead
                // of a second std::pow().
                const auto dIg2DVgk = y > 0.0f ? ex * ig2 / y : 0.0f;

                const auto ik = ia + ig2;
                const auto dIkDVgk = dIaDVgk + dIg2DVgk;
                const auto g = ik - vkGuess / Rk;
                const auto dg = -dIkDVgk - 1.0f / Rk;
                if (std::abs (dg) > 1.0e-12f)
                    vkGuess -= g / dg;
            }
            vk = juce::jlimit (0.0f, Vb, vkGuess);
            const auto vg = vgActual - vk;

            // Grid current is negligible at these bias points (see
            // solveBiasPoint), so no blocking-distortion charge to track --
            // unlike the preamp triodes, a power pentode's grid never
            // approaches conduction in normal class-AB operation.
            const auto iaAc = plateCurrent (vg, va) - Ia0;
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
        // Real '59 Bassman component values (Yeh & Smith's Fig. 1) by
        // default -- override these BEFORE the first updateCoefficients()
        // call to reuse this same verified transfer-function derivation
        // for a different real amp in the same Fender/Marshall tone-stack
        // family (the AB763 voice below does exactly this, with its own
        // real, separately-sourced component values -- the underlying
        // symbolic derivation is general to this whole topology, not
        // specific to any one product's BOM).
        double C1 = 0.25e-9, C2 = 20e-9, C3 = 20e-9;
        double R1 = 250000.0, R2 = 1000000.0, R3 = 25000.0, R4 = 56000.0;

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
#if defined(THREADLINE_AMP_ANALYSIS_TAPS)
    AnalysisTapCallback analysisTapCallback = nullptr;
    void* analysisTapContext = nullptr;
#endif
    juce::dsp::IIR::Filter<float> inputCoupling[2], interstageCoupling[2];
    juce::dsp::IIR::Filter<float> toneFilter[2], transformerLowPass[2];
    TriodeStage triodeV1[2], triodeV2A[2], cathodyneStage[2];
    PentodeStage powerTubeA[2], powerTubeB[2];
    BassmanToneStack bassmanStack;
    // Vox AC30 voice's own preamp/PI/power-stage instances -- a genuinely
    // different circuit, not sharing the 5E3 path's tube instances above
    // (see Voice::voxAC30's own comment). inputCoupling/transformerLowPass
    // above are reused for this voice too (generic DC-block/output-filter
    // roles, not 5E3-specific), since only one voice's per-sample branch
    // ever runs in a given block.
    TriodeStage triodeVoxV1[2], triodeVoxV2[2];
    LongTailPairStage voxPI[2];
    PentodeStage powerTubeVoxA[2], powerTubeVoxB[2];
    juce::dsp::IIR::Filter<float> voxInterstageCoupling[2];
    juce::dsp::IIR::Filter<float> voxBassShelf[2], voxTrebleShelf[2];
    // Fender AB763 voice's own preamp/PI/power-stage instances -- see
    // Voice::fenderAB763's own comment. Reuses inputCoupling/
    // transformerLowPass above too, same reasoning as the Vox voice.
    TriodeStage triodeFenderV1[2], triodeFenderV2[2];
    LongTailPairStage fenderPI[2];
    PentodeStage powerTubeFenderA[2], powerTubeFenderB[2];
    juce::dsp::IIR::Filter<float> fenderInterstageCoupling[2];
    BassmanToneStack fenderToneStack;
    // Marshall JTM45 voice's own preamp/PI/power-stage instances -- see
    // Voice::jtm45's own comment. Reuses inputCoupling/transformerLowPass
    // above too, same reasoning as the Vox/Fender voices. jtm45ToneStack
    // is deliberately left at BassmanToneStack's own default component
    // values -- JTM45's tone stack is a well-documented near-verbatim
    // copy of the 5F6-A Bassman's (Marshall's first amps literally cloned
    // that circuit), so the same real '59 Bassman values apply here too.
    TriodeStage triodeJTM45V1[2], triodeJTM45V2[2];
    LongTailPairStage jtm45PI[2];
    PentodeStage powerTubeJTM45A[2], powerTubeJTM45B[2];
    juce::dsp::IIR::Filter<float> jtm45InterstageCoupling[2];
    BassmanToneStack jtm45ToneStack;
    // Mesa/Boogie Mark I voice's own V1A/V1B/V3A preamp cascade, PI and
    // power stage -- see processMarkOneSample() and its prepare block.
    TriodeStage triodeMarkOneV1[2], triodeMarkOneV1B[2], triodeMarkOneV3A[2];
    LongTailPairStage markOnePI[2];
    PentodeStage powerTubeMarkOneA[2], powerTubeMarkOneB[2];
    juce::dsp::IIR::Filter<float> markOneInterstageCoupling[2];
    BassmanToneStack markOneToneStack;
    // Roland JC-120 voice's own EQ instances -- see Voice::rolandJC120's
    // own comment. Reuses inputCoupling/transformerLowPass above too
    // (generic DC-block/output-filter roles), but deliberately does NOT
    // reuse sagDetectorLP/sagEnvelope (solid-state, no sag). The amp's dry
    // Normal channel is modeled; the BBD chorus line is intentionally
    // omitted (chorus is its own pedal elsewhere on the board).
    juce::dsp::IIR::Filter<float> jcBassShelf[2], jcMidPeak[2], jcTrebleShelf[2];
    juce::SmoothedValue<float> outputGain;
    std::array<float, 2> sagDetectorLP {};
    // V2A's raw plate-voltage output (real volts, tens to hundreds of volts
    // under drive) back to sample scale, plus a tanh safety rail backstopping
    // that empirical calibration -- harness-verified end to end (see
    // TriodeStage's class comment), same established pattern as Klon/TS9's
    // own output calibration constants. Raised twice: an initial 0.02 never
    // reached the safety rail at all (0% pinned even at max Drive/loud
    // input), 0.065 reached double-digit pinned percentages but user
    // feedback still wanted more headroom available -- 0.10 harness-verified
    // to reach up to ~80% pinned (genuinely heavy, fuzz-territory clipping)
    // at max Drive + loud input, while staying at 0% pinned for low
    // Drive/quiet playing, so touch sensitivity survives across the whole
    // range rather than the knob just being uniformly hotter everywhere.
    static constexpr float outputCalibration = 0.10f;
    static constexpr float safetyCeiling = 3.0f;
    // Focusrite 2i2 instrument-input calibration from the supplied interface
    // headroom table: 4th Gen clips at +12dBu, 3rd Gen at +12.5dBu with the
    // hardware gain at minimum. Use their +12.25dBu midpoint as 0dBFS:
    // 0.775 * 10^(12.25/20) = 3.175V RMS, or 4.49073V peak per digital unit.
    // This preserves real pickup differences; there is deliberately no AGC
    // or "make every guitar peak at -18dBFS" normalization.

    // Audio-taper interstage Volume/Gain pot. The floor preserves the
    // plugin's useful low-Gain range; fifth-power taper keeps the first half clean
    // and gives the upper half enough V2/power-stage drive for breakup.
    static float getGainPotLevel (float amount) noexcept
    {
        const auto x = juce::jlimit (0.0f, 1.0f, amount);
        const auto x2 = x * x;
        return 0.02f + 0.98f * x2 * x2 * x;
    }
    // The separate Volume pots used by the Vox/Deluxe/JTM/Mesa paths need a
    // deeper audio taper than the tweed-derived interstage control above.
    // The former shared 2% floor plus x^5 curve still delivered 5.1% at noon,
    // enough to drive several downstream stages into dense clipping. This
    // seventh-power law gives 0.2% at minimum, 0.98% at noon, and remains 100%
    // at maximum: clean settings gain real headroom without weakening the
    // top of the knob or changing what reaches V1.
    static float getStandardGainPotLevel (float amount) noexcept
    {
        const auto x = juce::jlimit (0.0f, 1.0f, amount);
        const auto x2 = x * x;
        const auto x3 = x2 * x;
        return 0.002f + 0.998f * x3 * x3 * x;
    }
    // Mark I's separate Vol 2 control is not exposed as the user-facing
    // Output knob (Output must remain post-circuit). Hold it at the same
    // deep audio taper's physical noon position: 0.2% + 99.8% * 0.5^7.
    static constexpr float markOneVolumeTwoLevel = 0.009796875f;
    // Direct output-level boost for the Vintage 5E3/Boutique path only
    // (~+6dB) -- see its own use in process() for why this exists. Applies
    // to Vintage too, which doesn't go through either lossy tone-stack
    // network below (toneFilter is a plain lowpass, negligible passband
    // loss), so this is the piece of the fix that isn't explained by tone-
    // stack insertion loss alone.
    static constexpr float vintageOutputBoost = 2.0f;
    // Compensates the gain V1 lost by switching to a genuinely unbypassed
    // (self-biased) cathode -- that local negative feedback is real and
    // intentional (see TriodeStage's cathodeUnbypassed mode), but it also
    // measurably quieted the whole amp at ordinary playing levels, which
    // isn't part of what that change was meant to do. Value is the exact
    // reciprocal of the small-signal gain ratio measured in a standalone
    // harness (no JUCE dependency) between the old fixed-bias V1 and the
    // new self-biased one -- a low-level 440Hz sine's settled RMS
    // output/input ratio, isolating the LINEAR gain from the nonlinear
    // compression a hotter sweep would conflate it with: old 35.94x vs new
    // 23.20x, ratio 1.549 (+3.80dB). A pure post-multiply, not a curve
    // change, so it restores the old loudness at normal playing levels
    // without touching the new model's extra headroom/compression
    // character at hard drive.
    static constexpr float v1GainCompensation = 1.549f;
    // Cathodyne + power-stage calibration constants, all empirically
    // derived in a standalone harness the same "measure, don't guess" way
    // as outputCalibration/v1GainCompensation above -- chained
    // cathodyne->two PentodeStage->subtract->existing otKnee soft-clip,
    // swept across the full realistic Drive x input-loudness range plus
    // stress-test extremes well beyond what triode2's own safetyCeiling
    // clamp can actually deliver. Confirmed: quiet playing stays clean
    // (peak <=0.02 at low Drive/quiet input), loud playing at max Drive
    // reaches solidly toward but not fully into the otKnee's saturated
    // asymptote (peak ~0.55 at Drive=1/loudest realistic input, versus
    // fully pinned only at stress-test-only input levels the real signal
    // path can't reach) -- headroom that's real and appropriate for a
    // beam-tetrode power stage, not a tuning miss: pentodes/tetrodes are
    // genuinely more current-limited/less Va-sensitive than the preamp
    // triodes, so most of this amp's dirt still comes from V1/V2A upstream,
    // matching how a real lower/medium-gain tweed circuit actually behaves.
    // Bounded and finite across the entire sweep, including a hard-impulse
    // stress test (peak 0.9934, no blow-up).
    static constexpr float cathodyneInputScale = 4.0f;
    static constexpr float powerStageInputScale = 0.02f;
    static constexpr float powerStageOutputScale = 0.20f;
    // Fixed Output-knob offsets relative to Deluxe. Derived from the combined
    // RMS energy of the supplied 59.6s single-coil and P90 Focusrite DIs at
    // their captured amplitudes (no source normalisation), with Gain/EQ at
    // noon. These sit outside every amp circuit, so pickup dynamics and each
    // voice's gain/distortion behaviour remain intact.
    static constexpr float deluxeReferencedOutput[7] {
        0.7935f, // Vintage  -2.01 dB
        1.7233f, // Boutique +4.73 dB
        0.2450f, // Vox     -12.22 dB
        1.0000f, // Deluxe    0.00 dB reference
        0.4267f, // JTM45    -7.40 dB
        3.0724f, // Mesa      +9.75 dB
        1.0333f  // JC-120   +0.28 dB
    };
    // Soft-knee threshold for the final output trim -- linear below it, soft-
    // clips toward 1.0 above, so a positive calibration trim on a naturally
    // quiet voice can't hard-clip past full scale at maximum Drive (the same tanh
    // soft-knee math as the per-voice otKnee upstream, moved to the trim).
    static constexpr float outputLimiterKnee = 0.90f;
    double baseSampleRate = 44100.0, processingSampleRate = 176400.0;
    int channelCount = 2;
    Voice voice = Voice::vintage5E3;
    // Which voice the Bass/Mid/Treble coefficients below were last computed
    // for -- see setParameters()'s own comment.
    Voice lastToneStackVoice = Voice::vintage5E3;
    float driveAmount = 0.4f, lastTone01 = 0.6f;
    float gainPotLevel = 0.030035f, powerDriveAmount = 2.23f;
    float standardGainStageLevel = 1.0f;
    float jcDistortionGain = 4.008f, jcOutputDrive = 0.98f, jcOutputNormalise = 1.329f;
    float lastBass01 = 0.5f, lastMid01 = 0.5f, lastTreble01 = 0.5f;
    float targetOutputGain = 1.0f, sagEnvelope = 0.0f;
    float sagAttack = 0.999f, sagRelease = 0.999f;
    float sagDetectorLPCoefficient = 0.01f;
    bool enabled = true;
};

#undef THREADLINE_AMP_ANALYSIS_TAP
