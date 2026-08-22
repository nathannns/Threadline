#pragma once
#include <JuceHeader.h>
#include "WDFCore.h"
#include "ComplementaryToneStack.h"
#include "GuitarSignalLevel.h"

// "Bison" -- an original two-stage cascaded fuzz in the general archetype
// of the Big Muff Pi (two RC-coupled clipping stages feeding a passive
// "scooped mid" tone stack): not a copy of any specific traced schematic
// or component BOM -- this project has no rights to reproduce a
// particular commercial pedal's exact values -- but built from the same
// well-documented, decades-old topology family: a transistor gain stage
// with an antiparallel diode pair clamping its OWN feedback path (base to
// collector), twice in a row, through a coupling network that strips each
// stage's DC bias before the next, followed by a passive tone control
// that sums a lowpass and an inverted highpass -- the well-known reason
// this whole pedal family's tone control produces a genuine notch/scoop
// rather than a simple shelf (see ComplementaryToneStack.h).
//
// Checked a real Big Muff Pi schematic directly (el34world archive) to
// confirm that feedback-clamp structure -- both of its two clipping
// stages show their diode pair wired in parallel with the stage's own
// 470k feedback resistor, base to collector, the same general "diode
// clamps a gain stage's own feedback loop" family TS9/Klon's clippers
// already use (just built around a bare transistor instead of an op-amp)
// rather than a post-gain shunt clamp to ground -- so ClipStage below is
// now structured the same way TS9Clipper is (Norton current injection
// into a feedback resistance terminated by the diode pair), not copying
// any of the real circuit's actual component values, just its topology.
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
    // Diode-in-feedback clamp -- structurally identical to TS9Clipper
    // (Norton current injection representing Vin/Rin, summed against a
    // feedback resistance terminated by the diode pair), see class
    // comment for why that's the right shape for this stage.
    struct ClipStage
    {
        WDF::ResistiveCurrentSource feedbackR { 47000.0f };
        WDF::DiodePair<WDF::ResistiveCurrentSource> dp { feedbackR, 2.52e-9f, 0.02585f };

        void reset() noexcept { feedbackR.wdf.a = feedbackR.wdf.b = 0.0f; }

        float processSample (float vin, float inputOneOverR) noexcept
        {
            feedbackR.setCurrent (-vin * inputOneOverR);
            dp.incident (feedbackR.reflected());
            feedbackR.incident (dp.reflected());
            return WDF::voltage (feedbackR.wdf);
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
        wetBuffer.setSize (channelCount, static_cast<int> (spec.maximumBlockSize), false, false, true);
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
        const auto level = juce::jlimit (0.0f, 1.0f, level01);
        outputLevel = std::pow (level, 1.93f);
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

        // Sustain drives both stages, but reserve the steep rise for the last
        // third of the knob. The former x^2 * 800 curve was already at 201x
        // at noon, so the cascaded diode stages reached nearly their full
        // 1.23 peak there and left most of the upper half sounding identical.
        // This measured taper reaches 16x at noon and 88x at 90%, retaining
        // the dense maximum while exposing the useful transition into it.
        const auto driveGain = 1.0f + std::pow (sustainAmount, 3.0f) * 120.0f;

        juce::dsp::AudioBlock<float> block (wetBuffer);
        auto osBlock = oversampling->processSamplesUp (block);
        const auto osSamples = (int) osBlock.getNumSamples();
        for (size_t ch = 0; ch < osBlock.getNumChannels(); ++ch)
        {
            for (int i = 0; i < osSamples; ++i)
            {
                const auto x = GuitarSignalLevel::toVolts (osBlock.getSample ((int) ch, i)) * driveGain;
                const auto clipped1 = stage1[(size_t) ch].processSample (x, oneOverRin) * interStageGain;
                const auto coupled = couplingHighpass[ch].processSample (clipped1);
                auto clipped2 = stage2[(size_t) ch].processSample (coupled, oneOverRin) * outputCalibration;
                clipped2 = safetyCeiling * std::tanh (clipped2 / safetyCeiling);
                osBlock.setSample ((int) ch, i, GuitarSignalLevel::fromVolts (clipped2));
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
    // Generic input-resistor magnitude feeding each clip stage's feedback
    // network -- not a traced real value (see class comment on why this
    // stays generic rather than copying a BOM). Re-tuned via a standalone
    // harness after the topology change above: the old 10k (paired with
    // the old shunt-clamp topology's own calibration) left the diode pair
    // already fully saturated at Sustain=0, leaving the knob almost inert
    // -- a sweep across Rin/interStageGain confirmed 100k/1.0 gives a
    // meaningful clean(-ish)-to-hot range across the full Sustain sweep
    // instead (maxAbs ~0.52 at Sustain=0 up to ~1.38 at Sustain=1, of the
    // 3.0 safety ceiling -- see FangsClipper-style calibration comments
    // elsewhere in this file for the same discipline).
    static constexpr float oneOverRin = 1.0f / 100000.0f;
    // No longer boosting between stages -- each diode-in-feedback clip
    // stage already carries its own real gain via feedbackR/Rin, unlike
    // the old shunt-clamp stages which needed an external multiplier to
    // reach a useful clipping range.
    static constexpr float interStageGain = 1.0f;
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
