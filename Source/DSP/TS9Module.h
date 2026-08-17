#pragma once

#include <JuceHeader.h>
#include "WDFCore.h"

// TS9-style overdrive, modeled after the real TS808/9 signal path rather
// than a single clip-and-voice block:
//  1. A FIXED pre-clip highpass (~720Hz, always on, not user-controlled) —
//     this is where the TS9's famous "mid-hump, honky" character actually
//     comes from: rolling off bass *before* the clipper means the clipped
//     signal's harmonic content is inherently mid-forward. Real TS808/9
//     circuits have this baked into the input coupling network; it isn't
//     something the Tone knob touches.
//  2. A genuine Wave Digital Filter simulation of the real op-amp clipping
//     stage (TS9Clipper below), in place of a curve-fit asinh approximation
//     — ported from and verified against Chowdhury-DSP/BYOD's own Tube
//     Screamer model (src/processors/drive/tube_screamer/TubeScreamerWDF.h):
//     the classic inverting-op-amp-with-diode-feedback clipper (input
//     resistor Rin=4.7k, feedback Rf=51k plus up to 500k from the Drive
//     pot, real 1N4148 diode-pair parameters Is=4.352nA/Vt=25.85mV*1.906
//     ideality), solved via the same closed-form Wright Omega function
//     KlonModule's clipper uses. BYOD's reference circuit models the
//     op-amp's own finite gain/impedance via a full R-type multi-port
//     adaptor derived by their R-Solver tool; this uses the ideal-op-amp
//     limit of that same circuit (infinite gain, zero output impedance —
//     the standard simplifying assumption for this kind of clipper) rather
//     than hand-porting that adaptor's large symbolically-derived
//     scattering matrix, since a transcription error there would be far
//     more likely and far harder to catch than in the simpler adapted
//     series/parallel tree Klon's clipper uses.
//  3. A post-clip Tone control that's a genuine treble-cut lowpass sweep
//     (what the real TS9's Tone pot actually is), not a swept mid-bump —
//     turning Tone down darkens it, up brightens it, same as the pedal.
//
// setVariant() switches between the three well-known Tube Screamer-family
// pedals by retuning the same topology above, per their widely-documented
// differences: TS808 (the 1979 original, JRC4558D op-amp) is generally
// described as warmer/smoother with a touch more bass reaching the clipper
// than the TS9 reissue; TS9 is the mid-forward "honky" baseline; TS10 (the
// later Classic Series pedal) is consistently noted for a wider Tone range
// and noticeably more low end than either — its input network passes more
// bass before clipping. The clipper circuit itself (Rin/Rf/diode values)
// stays the same across all three — there's no similarly well-documented
// per-variant difference there to model with the same confidence, so that
// differentiation still lives entirely in the pre-clip highpass corner and
// post-clip Tone range below, same as before this rewrite.
// Like KlonModule, 2x-oversamples just the nonlinear clip stage — the
// pre-clip highpass and post-clip tone filter are linear (no new harmonic
// content), only the clip itself needs the higher rate to avoid aliasing
// the harmonics it generates. Unlike Klon, TS9 has no dry/wet blend (it's
// fully wet), so there's no parallel dry path to keep aligned with the
// oversampler's added latency — one less thing to compensate for.
class TS9Module
{
public:
    enum class Variant { TS9, TS808, TS10 };

    // The classic inverting op-amp clipper, ideal-op-amp limit: the input
    // resistor's current (Vin/Rin) is a Norton current injected into a node
    // formed by the feedback resistor Rf in parallel with the diode pair —
    // exactly what an ideal virtual-ground op-amp reduces this topology to.
    // Rf is itself the Drive pot's value (51k fixed + up to 500k of pot),
    // so Drive directly modulates the real circuit element rather than
    // pre-scaling the signal into a fixed clipper.
    struct TS9Clipper
    {
        WDF::ResistiveCurrentSource feedback { 51000.0f };
        WDF::DiodePair<WDF::ResistiveCurrentSource> dp { feedback, 4.352e-9f, 0.02585f * 1.906f };

        void setFeedbackResistance (float ohms) noexcept
        {
            feedback.wdf.R = ohms;
            feedback.wdf.G = 1.0f / ohms;
            dp.calcImpedance();
        }

        void reset() noexcept { feedback.wdf.a = feedback.wdf.b = 0.0f; }

        float processSample (float vin, float inputOneOverR) noexcept
        {
            feedback.setCurrent (-vin * inputOneOverR);
            dp.incident (feedback.reflected());
            feedback.incident (dp.reflected());
            return WDF::voltage (feedback.wdf);
        }
    };

    void setVariant (Variant newVariant)
    {
        if (newVariant == variant)
            return;
        variant = newVariant;
        updatePreClipFilter();
        updateToneFilter();
    }
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        channelCount = juce::jlimit (1, 2, (int) spec.numChannels);
        for (auto& f : preClipHighpass)
            f.prepare (spec);
        for (auto& f : toneFilter)
            f.prepare (spec);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) channelCount, 1 /* 1 stage = 2x */,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversampling->initProcessing (spec.maximumBlockSize);
        updatePreClipFilter();
        updateToneFilter();
        reset();
    }

    void reset()
    {
        for (auto& f : preClipHighpass)
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

        // Bass rolled off before the clipper (linear, stays at base rate) —
        // see class comment.
        preClipBuffer.setSize (numChannels, numSamples, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* dst = preClipBuffer.getWritePointer (ch);
            auto* src = buffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                dst[i] = preClipHighpass[ch].processSample (src[i]);
        }

        // Only the nonlinear clip runs at 2x.
        juce::dsp::AudioBlock<float> block (preClipBuffer);
        auto osBlock = oversampling->processSamplesUp (block);
        const auto osSamples = (int) osBlock.getNumSamples();
        for (size_t ch = 0; ch < osBlock.getNumChannels(); ++ch)
        {
            for (int i = 0; i < osSamples; ++i)
            {
                const auto x = osBlock.getSample ((int) ch, i);
                // Real op-amp clipper output (volts) -- outputCalibration
                // is an empirical scalar bringing that to a sensible audio
                // range, same role the old asinh version's driveGain-based
                // divisor played. A wide tanh safety rail backstops that
                // guess (the diode pair itself already self-limits, same
                // as real hardware -- this is only a numeric safety net).
                auto clipped = clipper[ch].processSample (x, oneOverRin) * outputCalibration;
                clipped = safetyCeiling * std::tanh (clipped / safetyCeiling);
                osBlock.setSample ((int) ch, i, clipped);
            }
        }
        oversampling->processSamplesDown (block);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* out = buffer.getWritePointer (ch);
            auto* clippedPtr = preClipBuffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                out[i] = toneFilter[ch].processSample (clippedPtr[i]) * outputLevel;
        }
    }

private:
    void updatePreClipFilter()
    {
        // Fixed corner — not swept by any control, matching the real
        // input coupling network. TS9 = baseline (most mid-forward); TS808
        // lets a touch more bass through (warmer); TS10 lets the most bass
        // through of the three (its widely-noted extra low end).
        const auto corner = variant == Variant::TS808 ? 640.0f
                           : variant == Variant::TS10  ? 480.0f
                                                        : 720.0f;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, corner, 0.707f);
        for (auto& f : preClipHighpass)
            *f.coefficients = *coeffs;
    }

    void updateToneFilter()
    {
        // Treble-cut sweep. TS10's Tone control is documented as having a
        // noticeably wider range than TS9/808 — darker at minimum, brighter
        // at maximum.
        const auto darkHz = variant == Variant::TS10 ? 900.0f : 1200.0f;
        const auto brightHz = variant == Variant::TS10 ? 13500.0f : 11000.0f;
        const auto cutoff = juce::jmap (lastTone01, 0.0f, 1.0f, darkHz, brightHz);
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, cutoff, 0.707f);
        for (auto& f : toneFilter)
            *f.coefficients = *coeffs;
    }

    juce::dsp::IIR::Filter<float> preClipHighpass[2], toneFilter[2];
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> preClipBuffer;
    TS9Clipper clipper[2];
    // Real Tube Screamer input resistor (R4 in BYOD's traced schematic).
    static constexpr float oneOverRin = 1.0f / 4700.0f;
    // Empirically-tuned, not physically derived -- see KlonModule's own
    // outputCalibration for why. Measured via a standalone harness: unlike
    // Klon's current-based output, this stage's raw output is a real op-amp
    // *voltage* swing, which already lands close to a sensible audio range
    // on its own (~0.4-0.5 for typical input), so this only needs a small
    // nudge rather than a large rescale.
    static constexpr float outputCalibration = 1.2f;
    static constexpr float safetyCeiling = 3.0f;
    double sampleRate = 44100.0;
    int channelCount = 2;
    float driveAmount = 0.3f;
    float outputLevel = 1.0f;
    float lastTone01 = -1.0f;
    bool enabled = false;
    Variant variant = Variant::TS9;
};
