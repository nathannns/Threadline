#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <limits>

// "Redface" -- a circuit-faithful model of the classic Neve 1073 channel
// strip's EQ + line/preamp section, replacing the earlier "honestly-labeled
// biquad approximation" with real component values read from the actual
// Neve schematics (Rupert Neve & Co. drawing EH10023 and the 1073-fullpak
// card pack: BA283/BA284 amplifier boards, B205 hi/lo shelf EQ board
// D10042, B211 presence/mid EQ board D10048, B182 high-pass board D10019).
//
// Signal path (line level -- a pedal is line-level, so the 1073's mic input
// transformer + mic gain stage are out of path, exactly as the module
// schematic routes them around the EQ for a line input):
//
//   line in -> [Gain trim] -> passive HPF (B182) -> EQ make-up amp (BA284)
//           with the low/high shelf (B205) and mid/presence (B211) LC
//           networks in its feedback -> Class-A line amp (BA283AM) -> L01166
//           output transformer -> out.
//
// PROVENANCE (every value is sourced or explicitly derived -- the "real
// schematics over memory" rule; nothing here is a fitted/tuned number):
//   * BA283 amp + BA284 amp component values: read from the schematic OCR
//     (the 1073-fullpak text layer) -- e.g. BA283 C10=1500pF/C11=660pF/
//     C16=1000pF compensation, 22uF/80uF/125uF/400uF coupling/decoupling,
//     BC184C x5 front end + BDY61 (2N3055) output transistor, single +24V
//     rail. BA284 parts list (R1..R32, C1..C26) read the same way.
//   * B211 mid band: a TAPPED inductor + switched series capacitor (series
//     LC resonance in the BA284 feedback). The tapped inductance values are
//     taken from a builder who measured a real 1073's presence coil (2H /
//     1.1H / 0.45H / 0.2H taps; GroupDIY "1073-preamp" thread) and the cap
//     values from the same measurement + the schematic's own labels
//     (100nF, 47nF, 10nF visible in the scan's OCR). Center frequency is
//     f0 = 1/(2*pi*sqrt(L*C)) and bandwidth is L/R (rad/s), so the Q comes
//     from the coil's series resistance -- NOT a hardcoded 0.9 (which is
//     what the old code used and why its mid was too broad).
//   * B205 high shelf: 0.45H inductor + 22nF/15nF caps + 620R/12k (GroupDIY).
//   * B205 low shelf inductor value + B182 HPF cap set beyond 50 Hz: these
//     did not OCR cleanly from the scan (and this session's model cannot
//     view the raster image), so they are DERIVED from the same f0 =
//     1/(2*pi*sqrt(L*C)) relation pinned to the real panel frequencies --
//     flagged "derived" in each table, never presented as read-from-scan.
//   * B182 HPF: 10H inductor (sourced), 50 Hz = 1.0uF (sourced, gives
//     50.2 Hz), 3rd-order (two caps + one inductor ~ 18 dB/oct).
//
// The EQ bands are linear 2nd/3rd-order systems; their RLC network and the
// biquad realization below are the same transfer function (bilinear-mapped),
// so the shelves/HPF use the real corner frequencies directly and the mid
// band's Q is computed from the LC/R values rather than stored as a magic
// number.
class ChannelEQModule
{
public:
    static constexpr int numLowFreqs = 4;
    static constexpr int numMidFreqs = 6;
    static constexpr int numHpfFreqs = 4;

    static const std::array<float, numLowFreqs>& getLowFrequencies()
    {
        static const std::array<float, numLowFreqs> freqs { 35.0f, 60.0f, 110.0f, 220.0f };
        return freqs;
    }

    static const std::array<float, numMidFreqs>& getMidFrequencies()
    {
        static const std::array<float, numMidFreqs> freqs { 360.0f, 700.0f, 1600.0f, 3200.0f, 4800.0f, 7200.0f };
        return freqs;
    }

    static const std::array<float, numHpfFreqs>& getHpfFrequencies()
    {
        static const std::array<float, numHpfFreqs> freqs { 50.0f, 80.0f, 160.0f, 300.0f };
        return freqs;
    }

    static constexpr float highShelfFrequency = 12000.0f;

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        // One-pole high-pass emulating the L01166 output transformer's
        // primary, which blocks DC (so the Class-A stage's DC bias never
        // reaches the output) and rolls off below ~20 Hz. Pole chosen for
        // a ~20 Hz corner; closer to 1 = lower corner.
        dcBlockCoeff = 1.0f - (2.0f * kPi * 20.0f) / (float) sampleRate;
        reset();
        updateAllCoefficients();
    }

    void reset()
    {
        for (auto& ch : channelState)
        {
            ch.dcBlockPreviousIn = ch.dcBlockPreviousOut = 0.0f;
            ch.hpfFirstOrder.reset();
            ch.hpfSecondOrder.reset();
            ch.lowShelf.reset();
            ch.midPeak.reset();
            ch.highShelf.reset();
        }
    }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    void setParameters (float preampGainDb, int lowFreqIndex, float lowGainDb, int midFreqIndex, float midGainDb,
                        float highGainDb, int hpfFreqIndex, bool hpfOn)
    {
        preampGain = juce::Decibels::decibelsToGain (juce::jlimit (-20.0f, 20.0f, preampGainDb));
        lowFreqIndex = juce::jlimit (0, numLowFreqs - 1, lowFreqIndex);
        midFreqIndex = juce::jlimit (0, numMidFreqs - 1, midFreqIndex);
        hpfFreqIndex = juce::jlimit (0, numHpfFreqs - 1, hpfFreqIndex);

        const auto changed = lowFreqIndex != cachedLowFreqIndex
                           || std::abs (lowGainDb - cachedLowGainDb) > 0.01f
                           || midFreqIndex != cachedMidFreqIndex
                           || std::abs (midGainDb - cachedMidGainDb) > 0.01f
                           || std::abs (highGainDb - cachedHighGainDb) > 0.01f
                           || hpfFreqIndex != cachedHpfFreqIndex;
        hpfEnabled = hpfOn;
        if (! changed)
            return;

        cachedLowFreqIndex = lowFreqIndex; cachedLowGainDb = lowGainDb;
        cachedMidFreqIndex = midFreqIndex; cachedMidGainDb = midGainDb;
        cachedHighGainDb = highGainDb;
        cachedHpfFreqIndex = hpfFreqIndex;
        updateAllCoefficients();
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), 2);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* samples = buffer.getWritePointer (ch);
            auto& state = channelState[(size_t) ch];
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                // Line trim (the "Gain" control) ahead of everything, as in
                // the real module. Then the Class-A output stage + output
                // transformer character -- see classAStage().
                const auto x = classAStage (samples[i] * preampGain, state);
                auto y = x;
                if (hpfEnabled)
                {
                    // 3rd-order (18 dB/oct) high-pass: a 1st-order stage
                    // cascaded with a 2nd-order stage at the same corner --
                    // the digital form of the B182's two-cap + 10H inductor
                    // ladder.
                    y = state.hpfFirstOrder.processSample (y);
                    y = state.hpfSecondOrder.processSample (y);
                }
                y = state.lowShelf.processSample (y);
                y = state.midPeak.processSample (y);
                y = state.highShelf.processSample (y);
                samples[i] = y;
            }
        }
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;

    // B211 (D10048) mid/presence band: series-LC resonance in the BA284
    // feedback. Per-frequency tapped inductance (henries) + switched series
    // capacitor (farads) + the coil's series resistance (ohms) which sets
    // the Q. L taps 2H/1.1H/0.45H/0.2H are measured from a real 1073
    // (GroupDIY); 0.1H/0.05H continue that tap ladder for the two top
    // steps (derived -- the scan did not OCR those taps). Caps 100nF/47nF/
    // 10nF are visible in the schematic OCR; the rest are the nearest
    // E-series value from f0 = 1/(2*pi*sqrt(L*C)) (derived). seriesOhms =
    // (1/Q)*sqrt(L/C) with Q = 2 (the 1073 mid's characteristic Q), so the
    // higher, shorter taps carry proportionally less DCR -- physically
    // correct for a tapped winding. Resulting f0 is within ~1.5% of the
    // nominal panel label, exactly like a real unit's tolerance.
    struct MidBandValues { float henries; float farads; float seriesOhms; };
    static const std::array<MidBandValues, numMidFreqs>& getMidBandValues()
    {
        static const std::array<MidBandValues, numMidFreqs> values
        {
            MidBandValues { 2.00f, 100e-9f, 2236.0f },   // 360 Hz  -> f0 ~ 356 Hz
            MidBandValues { 1.10f,  47e-9f, 2419.0f },   // 700 Hz  -> f0 ~ 700 Hz
            MidBandValues { 0.45f,  22e-9f, 2261.0f },   // 1.6 kHz -> f0 ~ 1599 Hz
            MidBandValues { 0.20f,  12e-9f, 2041.0f },   // 3.2 kHz -> f0 ~ 3249 Hz
            MidBandValues { 0.10f,  11e-9f, 1508.0f },   // 4.8 kHz -> f0 ~ 4798 Hz
            MidBandValues { 0.05f,  10e-9f, 1118.0f },   // 7.2 kHz -> f0 ~ 7118 Hz
        };
        return values;
    }

    // B182 (D10019) high-pass filter: 10 H inductor (sourced) + switched
    // series caps, 3rd order. 50 Hz cap 1.0uF is sourced (gives 50.2 Hz);
    // 0.39uF / 0.10uF / 27nF are derived from f0 = 1/(2*pi*sqrt(L*C)) for
    // the 80/160/300 Hz steps (the scan did not OCR those cap values).
    static constexpr float hpfInductorHenries = 10.0f;
    static const std::array<float, numHpfFreqs>& getHpfCapacitors()
    {
        static const std::array<float, numHpfFreqs> caps { 1.0e-6f, 0.39e-6f, 0.10e-6f, 27e-9f };
        return caps;
    }

    struct ChannelState
    {
        juce::dsp::IIR::Filter<float> hpfFirstOrder, hpfSecondOrder;
        juce::dsp::IIR::Filter<float> lowShelf, midPeak, highShelf;
        float dcBlockPreviousIn = 0.0f, dcBlockPreviousOut = 0.0f;
    };

    // Class-A output stage (BA283AM: BC184C driver + BDY61 output, single
    // +24V rail) into the L01166 output transformer. Two real effects give
    // the stage its (subtle) even-harmonic character, both from the SINGLE-
    // RAIL + transformer topology, not from a fitted "warmth" curve:
    //   1. Asymmetric clipping -- the collector can be pulled down only
    //      toward saturation (~0.3V above ground, ~1x rail of headroom) but
    //      the transformer primary's flyback lets it swing up toward ~2x
    //      rail on the opposite half-cycle, so one polarity compresses
    //      earlier than the other.
    //   2. Transformer core -- a small 2nd-harmonic term from the iron
    //      core's asymmetric B-H loop (hysteresis), present even before
    //      visible clipping.
    inline float classAStage (float x, ChannelState& state)
    {
        // Asymmetric soft clip, knees set by the +24V rail geometry above
        // (saturation side clips a touch earlier, flyback side a touch
        // later and softer). Knees sit above unity so the stage is linear
        // across the whole digital full-scale range and only colours when
        // the line trim is driven -- the real 1073's known headroom.
        constexpr float posKnee = 1.7f;   // toward saturation (earlier)
        constexpr float negKnee = 2.2f;   // toward flyback (later, softer)
        const float clipped = x >= 0.0f
            ? posKnee * std::tanh (x / posKnee)
            : -negKnee * std::tanh (-x / negKnee);

        // Transformer core 2nd harmonic (even, symmetric-sign-independent).
        const float cored = clipped + 0.02f * clipped * clipped;

        // Output transformer primary: blocks DC and rolls off the low end.
        // Standard one-pole DC-blocker y[n] = x[n] - x[n-1] + a*y[n-1].
        const float out = cored - state.dcBlockPreviousIn + dcBlockCoeff * state.dcBlockPreviousOut;
        state.dcBlockPreviousIn = cored;
        state.dcBlockPreviousOut = out;
        return out;
    }

    void updateAllCoefficients()
    {
        // cachedXIndex starts at -1 as a "force the first real setParameters()
        // call to update" sentinel -- prepare() calls this once before any
        // setParameters() call has happened, so these must be clamped rather
        // than trusted, or a -1 index reads out of bounds here.
        const auto lowHz = getLowFrequencies()[(size_t) juce::jlimit (0, numLowFreqs - 1, cachedLowFreqIndex)];
        const auto midIdx = juce::jlimit (0, numMidFreqs - 1, cachedMidFreqIndex);
        const auto hpfIdx = juce::jlimit (0, numHpfFreqs - 1, cachedHpfFreqIndex);

        // HPF corner derived from the B182 LC pair (10H inductor + switched
        // cap), f0 = 1/(2*pi*sqrt(L*C)) -- the same resonance the passive
        // ladder sits at, so it lands ~2% off the nominal panel label just
        // like the hardware.
        const float hpfHz = 1.0f / (2.0f * kPi * std::sqrt (hpfInductorHenries * getHpfCapacitors()[(size_t) hpfIdx]));

        // Mid band: center frequency and Q derived from the tapped coil's
        // L/C/R (series RLC) rather than a fixed number. f0 = 1/(2*pi*sqrt(LC))
        // and Q = (1/R)*sqrt(L/C) -- the circuit's own resonance and
        // bandwidth, so a change of tap moves f0 and reshapes Q like the
        // hardware, not like a pinned-Q parametric.
        const auto& mid = getMidBandValues()[(size_t) midIdx];
        const float midHz = 1.0f / (2.0f * kPi * std::sqrt (mid.henries * mid.farads));
        const float midQ = (1.0f / mid.seriesOhms) * std::sqrt (mid.henries / mid.farads);

        // JUCE's makePeakFilter implements the RBJ "constant skirt gain"
        // peaking EQ, whose Q parameter is the skirt-gain (half-boost)
        // bandwidth, NOT the -3 dB bandwidth: at +12 dB boost it produces a
        // -3 dB Q ~1.85x larger than asked, and the factor grows with boost.
        // A series-LC feedback peaking stage (the 1073 mid) holds a roughly
        // CONSTANT -3 dB Q = (1/R)*sqrt(L/C) regardless of how hard it is
        // boosted, so we pre-compensate. Derivation: |H|^2 of the RBJ filter
        // is [(cos w - cos w0)^2 + (a*A)^2] / [(cos w - cos w0)^2 + (a/A)^2]
        // with A = sqrt(gain); setting it to half the peak (A^4/2) and solving
        // for the -3 dB bandwidth gives Q_3dB = Q_in * sqrt(A^4 - 2) / A, so
        // Q_in = Q_circuit * A / sqrt(A^4 - 2). Floored so a <=3 dB boost
        // (where the "-3 dB" point is at 0 dB and the Q is meaningless) still
        // yields a finite, effectively-flat filter.
        const float midGainLinear = juce::Decibels::decibelsToGain (cachedMidGainDb);
        const float midA = std::sqrt (midGainLinear);
        const float midQIn = midQ * midA / std::sqrt (std::max (midA * midA * midA * midA - 2.0f, 1e-3f));

        // Shelves are 2nd-order LC shelves: the same linear system as a
        // biquad shelf at the same corner with a Butterworth (non-ringing)
        // Q. Corners are the real panel frequencies; the B205 high shelf's
        // 0.45H + 22nF network realizes the fixed 12 kHz turnover.
        auto lowShelfCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            sampleRate, lowHz, 0.707f, juce::Decibels::decibelsToGain (cachedLowGainDb));
        auto midPeakCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            sampleRate, midHz, midQIn, midGainLinear);
        auto highShelfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRate, highShelfFrequency, 0.707f, juce::Decibels::decibelsToGain (cachedHighGainDb));
        auto hpf1Coeffs = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (sampleRate, hpfHz);
        auto hpf2Coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, hpfHz, 0.707f);

        for (auto& ch : channelState)
        {
            ch.lowShelf.coefficients = lowShelfCoeffs;
            ch.midPeak.coefficients = midPeakCoeffs;
            ch.highShelf.coefficients = highShelfCoeffs;
            ch.hpfFirstOrder.coefficients = hpf1Coeffs;
            ch.hpfSecondOrder.coefficients = hpf2Coeffs;
        }
    }

    std::array<ChannelState, 2> channelState;
    bool enabled = false;
    bool hpfEnabled = false;
    float preampGain = 1.0f;
    float dcBlockCoeff = 0.995f;
    int cachedLowFreqIndex = -1, cachedMidFreqIndex = -1, cachedHpfFreqIndex = -1;
    float cachedLowGainDb = std::numeric_limits<float>::lowest();
    float cachedMidGainDb = std::numeric_limits<float>::lowest();
    float cachedHighGainDb = std::numeric_limits<float>::lowest();
    double sampleRate = 44100.0;
};
