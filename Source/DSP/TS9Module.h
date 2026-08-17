#pragma once

#include <JuceHeader.h>

// TS9-style overdrive, modeled after the real TS808/9 signal path rather
// than a single clip-and-voice block:
//  1. A FIXED pre-clip highpass (~720Hz, always on, not user-controlled) —
//     this is where the TS9's famous "mid-hump, honky" character actually
//     comes from: rolling off bass *before* the clipper means the clipped
//     signal's harmonic content is inherently mid-forward. Real TS808/9
//     circuits have this baked into the input coupling network; it isn't
//     something the Tone knob touches.
//  2. A symmetric asinh diode clip: asinh(x) is the closed-form transfer
//     function of a resistor-fed diode pair to ground (the standard
//     real-time simplification of the diode's exponential I-V law),
//     turning on more smoothly than tanh — matched silicon diodes on both
//     polarities, unlike the Klon's asymmetric germanium/silicon pair.
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
// bass before clipping. This isn't a claim of exact measured component
// values (those vary by production run/clone anyway), just a defensible
// characterization of the three pedals' known family resemblance and
// differences applied to the same physically-motivated clipper.
// Like KlonModule, 2x-oversamples just the nonlinear clip stage — the
// pre-clip highpass and post-clip tone filter are linear (no new harmonic
// content), only the asinh/tanh clip itself needs the higher rate to avoid
// aliasing the harmonics it generates. Unlike Klon, TS9 has no dry/wet
// blend (it's fully wet), so there's no parallel dry path to keep aligned
// with the oversampler's added latency — one less thing to compensate for.
class TS9Module
{
public:
    enum class Variant { TS9, TS808, TS10 };

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
        const auto driveGain = 1.0f + driveAmount * 40.0f;

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
                auto x = osBlock.getSample ((int) ch, i) * driveGain;

                // Diode-physics knee with a tanh safety ceiling (bounds
                // output across the drive range; asinh defines the actual
                // knee shape, not the ceiling). TS9/TS10 use a matched
                // symmetric pair; TS808 gets a mild positive/negative
                // asymmetry, reflecting its "creamier/rounder" reputation
                // versus the TS9's harder-edged symmetric clip.
                const auto kneeScale = (variant == Variant::TS808 && x >= 0.0f) ? kneeScaleBase * 1.12f
                                                                                 : kneeScaleBase;
                const auto shaped = thermalVoltage * std::asinh (x / kneeScale);
                auto clipped = ceilingLimit * std::tanh (shaped / ceilingLimit);
                clipped /= std::max (0.4f, std::sqrt (driveGain) * 0.42f);
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
    static constexpr float thermalVoltage = 0.58f;
    static constexpr float ceilingLimit = 1.0f;
    static constexpr float kneeScaleBase = 0.75f; // matched silicon diodes, baseline symmetric
    double sampleRate = 44100.0;
    int channelCount = 2;
    float driveAmount = 0.3f;
    float outputLevel = 1.0f;
    float lastTone01 = -1.0f;
    bool enabled = false;
    Variant variant = Variant::TS9;
};
