#pragma once
#include <JuceHeader.h>
#include "WDFCore.h"
#include "ComplementaryToneStack.h"

// "Bison" -- an original two-stage cascaded fuzz in the general archetype
// of the Big Muff Pi (two RC-coupled clipping stages feeding a passive
// "scooped mid" tone stack): not a copy of any specific traced schematic
// or component BOM -- this project has no rights to reproduce a
// particular commercial pedal's exact values -- but built from the same
// well-documented, decades-old topology family: a signal driven hard
// enough to swing a transistor stage's output into a pair of clamping
// diodes, twice in a row, through a coupling network that strips each
// stage's DC bias before the next, followed by a passive tone control
// that sums a lowpass and an inverted highpass -- the well-known reason
// this whole pedal family's tone control produces a genuine notch/scoop
// rather than a simple shelf (see ComplementaryToneStack.h).
//
// Each clipping stage reuses this project's existing WDF (Wave Digital
// Filter) antiparallel-diode-pair machinery (WDFCore.h -- the same real
// circuit-simulation element already verified against Klon/TS9's
// published, MIT-licensed reference models), fed by a linear gain stage
// standing in for the transistor's own small-signal voltage gain -- the
// same tier of simplification KlonModule/TS9Module already make for
// their own op-amps (ideal-gain-element limit, not a full transistor-
// level Ebers-Moll model). What makes this genuinely a *different*
// circuit from Klon/TS9's single-stage clippers -- not just the same
// clipper twice -- is the cascade itself: stage two clips whatever stage
// one already clipped and re-biased, which is exactly where a Muff-style
// fuzz's much denser, buzzier harmonic content over a simple one-stage
// overdrive actually comes from.
class BisonModule
{
public:
    // A shunt clamp: the stage's own (already-gain-scaled) open-circuit
    // output voltage, Norton-converted through its own loading resistance
    // Rc into a node clamped by an antiparallel diode pair to ground --
    // WDFCore's DiodePair rooted directly on a ResistiveCurrentSource,
    // the same tractable pattern TS9Clipper uses, just without a
    // feedback loop around an op-amp (this is a plain shunt stage, not an
    // inverting-amplifier feedback clipper).
    struct ClipStage
    {
        WDF::ResistiveCurrentSource node { 47000.0f };
        WDF::DiodePair<WDF::ResistiveCurrentSource> dp { node, 2.52e-9f, 0.02585f };

        void reset() noexcept { node.wdf.a = node.wdf.b = 0.0f; }

        float processSample (float theveninVoltage) noexcept
        {
            node.setCurrent (theveninVoltage / node.wdf.R);
            dp.incident (node.reflected());
            node.incident (dp.reflected());
            return WDF::voltage (node.wdf);
        }
    };

    void prepare (const juce::dsp::ProcessSpec& spec, int oversamplingMode = 1)
    {
        sampleRate = spec.sampleRate;
        channelCount = juce::jlimit (1, 2, (int) spec.numChannels);
        const auto stages = juce::jlimit (0, 2, oversamplingMode);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) channelCount, stages,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversampling->initProcessing (spec.maximumBlockSize);
        // couplingHighpass runs *inside* the oversampled sample loop below
        // (between stage1 and stage2, both of which process at the
        // oversampled rate) -- its own prepare()/coefficients must use that
        // same oversampled rate, not the base spec, or its corner silently
        // shifts down by the live oversampling factor (a real bug caught by
        // audit: was prepared at base rate while actually running at 2x/4x).
        oversampledRate = sampleRate * (double) oversampling->getOversamplingFactor();
        juce::dsp::ProcessSpec oversampledSpec { oversampledRate, spec.maximumBlockSize, spec.numChannels };
        for (auto& f : couplingHighpass)
            f.prepare (oversampledSpec);
        for (auto& t : tone)
            t.prepare (sampleRate);

        dryDelay.prepare (spec);
        dryDelay.setMaximumDelayInSamples (64);
        dryDelay.setDelay ((float) getLatencySamples());

        updateCouplingFilter();
        reset();
    }

    void reset()
    {
        if (oversampling != nullptr)
            oversampling->reset();
        for (auto& s : stage1)
            s.reset();
        for (auto& s : stage2)
            s.reset();
        for (auto& f : couplingHighpass)
            f.reset();
        for (auto& t : tone)
            t.reset();
        dryDelay.reset();
    }

    int getLatencySamples() const noexcept
    {
        return oversampling != nullptr ? juce::roundToInt (oversampling->getLatencyInSamples()) : 0;
    }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    // sustain/tone/level/mix: 0-1. tone: 0 = bass-heavy, 1 = treble-heavy,
    // 0.5 = deepest scoop (see ComplementaryToneStack).
    void setParameters (float sustain01, float tone01, float level01, float mix01)
    {
        sustainAmount = juce::jlimit (0.0f, 1.0f, sustain01);
        toneBlend = juce::jlimit (0.0f, 1.0f, tone01);
        outputLevel = juce::jlimit (0.0f, 2.0f, level01 * 2.0f);
        mix = juce::jlimit (0.0f, 1.0f, mix01);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled || oversampling == nullptr)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), channelCount);
        const auto numSamples = buffer.getNumSamples();
        dryDelay.setDelay ((float) getLatencySamples());

        wetBuffer.makeCopyOf (buffer, true);

        // Sustain drives both stages -- pushing the front end harder into
        // the first clip, which (after the inter-stage coupling network
        // strips its DC bias) also arrives hotter at the second.
        const auto driveGain = 1.0f + sustainAmount * sustainAmount * 800.0f;

        juce::dsp::AudioBlock<float> block (wetBuffer);
        auto osBlock = oversampling->processSamplesUp (block);
        const auto osSamples = (int) osBlock.getNumSamples();
        for (size_t ch = 0; ch < osBlock.getNumChannels(); ++ch)
        {
            for (int i = 0; i < osSamples; ++i)
            {
                const auto x = osBlock.getSample ((int) ch, i) * driveGain;
                const auto clipped1 = stage1[(size_t) ch].processSample (x) * interStageGain;
                const auto coupled = couplingHighpass[ch].processSample (clipped1);
                auto clipped2 = stage2[(size_t) ch].processSample (coupled) * outputCalibration;
                clipped2 = safetyCeiling * std::tanh (clipped2 / safetyCeiling);
                osBlock.setSample ((int) ch, i, clipped2);
            }
        }
        oversampling->processSamplesDown (block);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* dry = buffer.getWritePointer (ch);
            auto* wet = wetBuffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                const auto toned = tone[ch].processSample (wet[i], toneBlend);
                dryDelay.pushSample (ch, dry[i]);
                const auto delayedDry = dryDelay.popSample (ch);
                dry[i] = (delayedDry * (1.0f - mix) + toned * mix) * outputLevel;
            }
        }
    }

private:
    void updateCouplingFilter()
    {
        // Fixed corner -- the real coupling cap between the two stages,
        // stripping stage one's DC bias before stage two sees it. Computed
        // at the oversampled rate since that's where this filter actually
        // runs (see prepare()'s comment).
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (oversampledRate, 80.0f, 0.707f);
        for (auto& f : couplingHighpass)
            *f.coefficients = *coeffs;
    }

    juce::dsp::IIR::Filter<float> couplingHighpass[2];
    ComplementaryToneStack tone[2];
    juce::dsp::DelayLine<float> dryDelay;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> wetBuffer;
    ClipStage stage1[2], stage2[2];
    static constexpr float interStageGain = 6.0f;
    // Empirically-tuned via a standalone harness (same discipline as
    // Klon/TS9's own calibration constants) -- stage2's raw output peak
    // plateaus around 0.24-0.26 across the Sustain range (both stages'
    // diode pairs saturate quickly, same as real hardware -- consistent
    // with a real Big Muff's own well-known trait of staying fuzzy across
    // most of its Sustain range rather than scaling loudness with it), so
    // this brings max-drive settings up into the same "hot but not fully
    // pinned" ~2.0-2.5 (of the 3.0 safety ceiling) range Klon/TS9 target.
    static constexpr float outputCalibration = 9.0f;
    static constexpr float safetyCeiling = 3.0f;
    double sampleRate = 44100.0;
    double oversampledRate = 44100.0;
    int channelCount = 2;
    float sustainAmount = 0.3f, outputLevel = 1.0f, mix = 1.0f, toneBlend = 0.5f;
    bool enabled = false;
};
