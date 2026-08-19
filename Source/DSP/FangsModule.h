#pragma once
#include <JuceHeader.h>
#include "WDFCore.h"
#include "ComplementaryToneStack.h"

// "Fangs" -- an original op-amp diode-feedback fuzz/distortion, the
// general archetype behind pedals like the ProCo Rat and MXR Distortion+
// (an inverting op-amp with a clipping diode pair inside its feedback
// loop): not a copy of either's specific schematic or component values --
// this project has no rights to reproduce a particular commercial pedal's
// traced BOM -- but an original circuit built from the same well-
// documented, decades-old textbook topology (an op-amp summing-junction
// clipper), reusing this project's own from-scratch WDF (Wave Digital
// Filter) core -- the same real circuit-simulation machinery already
// verified against Klon/TS9's published, MIT-licensed reference models
// (see WDFCore.h's own header for that provenance).
//
// What distinguishes this from TS9Module's clipper (an almost identical
// "current source + antiparallel diode pair in feedback" topology) is a
// capacitor added in parallel with the feedback resistor: at low Gain the
// resistor dominates the feedback path (a fairly clean, diode-limited
// clip), but as Gain rises, the same fixed capacitor becomes a
// proportionally bigger fraction of that feedback impedance at high
// frequencies, rolling off the harmonics reaching the clipper -- the
// real, well-known reason this whole pedal family gets progressively
// darker/tighter as gain increases. That's modeled here as an actual
// second reactive one-port (WDF::Capacitor, genuinely solved each
// sample), not a gain-dependent filter coefficient bolted on afterward.
//
// Post-clip "Filter" is a ComplementaryToneStack (see that file) -- the
// same general dark<->scoop<->bright sweep BisonModule's Tone control
// uses below.
class FangsModule
{
public:
    // Ideal-op-amp inverting clipper: input current Vin/Rin summed at the
    // virtual-ground node against a feedback network of R_gain parallel
    // with C_filter, clamped by an antiparallel silicon diode pair --
    // structurally TS9Clipper's tree (WDF::ResistiveCurrentSource +
    // WDF::DiodePair) with an added WDF::Capacitor leg, combined via the
    // same WDF::Parallel adaptor KlonClipper already uses elsewhere in
    // this codebase.
    struct FangsClipper
    {
        WDF::ResistiveCurrentSource feedbackR { 220000.0f };
        WDF::Capacitor feedbackC { 1.0e-9f, 44100.0 };
        WDF::Parallel<WDF::ResistiveCurrentSource, WDF::Capacitor> node { feedbackR, feedbackC };
        WDF::DiodePair<decltype (node)> dp { node, 4.352e-9f, 0.02585f * 1.906f };

        void prepare (double wdfSampleRate)
        {
            feedbackC.prepare (1.0e-9f, wdfSampleRate);
            node.calcImpedance();
            dp.calcImpedance();
            reset();
        }

        void reset()
        {
            feedbackC.reset();
            feedbackR.wdf.a = feedbackR.wdf.b = 0.0f;
        }

        void setFeedbackResistance (float ohms) noexcept
        {
            feedbackR.wdf.R = ohms;
            feedbackR.wdf.G = 1.0f / ohms;
            node.calcImpedance();
            dp.calcImpedance();
        }

        float processSample (float vin, float inputOneOverR) noexcept
        {
            feedbackR.setCurrent (-vin * inputOneOverR);
            dp.incident (node.reflected());
            node.incident (dp.reflected());
            return WDF::voltage (node.wdf);
        }
    };

    // oversamplingMode: 0 = off (1x), 1 = 2x, 2 = 4x -- same convention as
    // AmpModule/KlonModule/TS9Module's own oversamplingMode.
    void prepare (const juce::dsp::ProcessSpec& spec, int oversamplingMode = 1)
    {
        sampleRate = spec.sampleRate;
        channelCount = juce::jlimit (1, 2, (int) spec.numChannels);
        const auto stages = juce::jlimit (0, 2, oversamplingMode);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) channelCount, stages,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversampling->initProcessing (spec.maximumBlockSize);
        for (auto& c : clipper)
            c.prepare (sampleRate * (double) oversampling->getOversamplingFactor());
        for (auto& t : tone)
            t.prepare (sampleRate);

        dryDelay.prepare (spec);
        dryDelay.setMaximumDelayInSamples (64);
        dryDelay.setDelay ((float) getLatencySamples());

        reset();
    }

    void reset()
    {
        if (oversampling != nullptr)
            oversampling->reset();
        for (auto& c : clipper)
            c.reset();
        for (auto& t : tone)
            t.reset();
        dryDelay.reset();
    }

    int getLatencySamples() const noexcept
    {
        return oversampling != nullptr ? juce::roundToInt (oversampling->getLatencyInSamples()) : 0;
    }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    // gain/filter/level/mix: 0-1.
    void setParameters (float gain01, float filter01, float level01, float mix01)
    {
        gainAmount = juce::jlimit (0.0f, 1.0f, gain01);
        filterBlend = juce::jlimit (0.0f, 1.0f, filter01);
        outputLevel = juce::jlimit (0.0f, 2.0f, level01 * 2.0f);
        mix = juce::jlimit (0.0f, 1.0f, mix01);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled || oversampling == nullptr)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), channelCount);
        const auto numSamples = buffer.getNumSamples();

        // Real op-amp fuzz/distortion Gain pots run from a few kohm up to
        // ~1M -- low resistance = tight/low-gain feedback (mostly clean),
        // high resistance = huge loop gain overdriving the diodes hard.
        // Squared taper -- most of the pot's useful range sits in the
        // last third, matching how these pots feel in practice.
        const auto feedbackOhms = 4700.0f + gainAmount * gainAmount * 995300.0f;
        for (auto& c : clipper)
            c.setFeedbackResistance (feedbackOhms);

        dryDelay.setDelay ((float) getLatencySamples());

        // buffer itself stays the untouched dry reference throughout --
        // the wet path runs entirely in wetBuffer (a copy) until the final
        // mix write, same pattern KlonModule's preClipBuffer uses.
        wetBuffer.makeCopyOf (buffer, true);

        juce::dsp::AudioBlock<float> block (wetBuffer);
        auto osBlock = oversampling->processSamplesUp (block);
        const auto osSamples = (int) osBlock.getNumSamples();
        for (size_t ch = 0; ch < osBlock.getNumChannels(); ++ch)
        {
            for (int i = 0; i < osSamples; ++i)
            {
                const auto x = osBlock.getSample ((int) ch, i);
                // Real op-amp clipper output (volts) -- outputCalibration
                // brings that to a sensible audio range, same role as
                // Klon/TS9's own calibration constants. A wide tanh safety
                // rail backstops that guess (the diode pair itself already
                // self-limits, same as real hardware).
                auto clipped = clipper[ch].processSample (x, oneOverRin) * outputCalibration;
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
                const auto toned = tone[ch].processSample (wet[i], filterBlend);
                dryDelay.pushSample (ch, dry[i]);
                const auto delayedDry = dryDelay.popSample (ch);
                dry[i] = (delayedDry * (1.0f - mix) + toned * mix) * outputLevel;
            }
        }
    }

private:
    juce::dsp::DelayLine<float> dryDelay;
    ComplementaryToneStack tone[2];
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> wetBuffer;
    FangsClipper clipper[2];
    // 47k input resistor -- a harness sweep across candidate Rin values
    // confirmed this is what actually gives the Gain knob a meaningful
    // clean<->saturated range (a much lower Rin, e.g. 1k, was found to
    // already saturate the diode pair at *minimum* Gain for any
    // realistic playing level, leaving the knob almost inert).
    static constexpr float oneOverRin = 1.0f / 47000.0f;
    static constexpr float outputCalibration = 6.0f;
    static constexpr float safetyCeiling = 3.0f;
    double sampleRate = 44100.0;
    int channelCount = 2;
    float gainAmount = 0.3f, outputLevel = 1.0f, mix = 1.0f, filterBlend = 0.5f;
    bool enabled = false;
};
