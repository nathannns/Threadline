#pragma once

#include <JuceHeader.h>

// One entry in the user-reorderable pedalboard. Each concrete subclass wraps
// exactly one of the plugin's processing stages (or, for Cab, the whole A+B-
// plus-blend unit, and for Delay, the whole Plexer/Copier dual-engine unit)
// behind a uniform interface PedalChainRunner can call without knowing the
// wrapped module's own process()/setParameters() shape. See
// Source/DSP/PedalNodes/*.h for the concrete adapters and
// PedalChainRunner.h/.cpp for how these get sequenced.
class PedalNode
{
public:
    virtual ~PedalNode() = default;

    // Stable identity token -- this is what's persisted in the pedalboard
    // order list (see PedalboardOrder.h). NEVER change an existing id once
    // shipped; old sessions/presets reference it by string, the same
    // durability contract as an APVTS parameter id.
    virtual juce::Identifier getId() const = 0;

    virtual void prepare (const juce::dsp::ProcessSpec& spec) = 0;

    // Full reset of internal DSP state (filters, delay lines, envelopes,
    // oversampler history). Called by PedalChainRunner exactly once, right
    // before the first updateAndProcess() call after this node transitions
    // from "not in the runtime call list at all" to "being called again" --
    // i.e. genuine re-insertion after true removal -- so it never resumes
    // with arbitrarily stale state from whenever it was last active.
    virtual void reset() = 0;

    // Pulls this node's own parameters from the APVTS, decides whether it's
    // "active" (its own On toggle, ANDed with any page-level section bypass
    // it belongs to, ANDed with inTargetOrder), runs the wrapped module, and
    // applies click-free crossfading for the transition (in or out) via the
    // protected crossfadeToggle() helper below. Returns true if the node is
    // still doing meaningful work this block (on, or mid-fade) -- false once
    // fully settled off AND excluded from the target order, which tells the
    // runner it can drop this node from the runtime call list (true "not
    // called at all" going forward, not just an early-return).
    virtual bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) = 0;

    // 0 for everything except the oversampled stages (Klon/TS9/Amp). Always
    // reflects the node's own latency need regardless of its current on/off
    // toggle (matches AmpModule's own "reports latency even when disabled"
    // convention) -- only presence/absence in the target order changes what
    // PedalChainRunner sums for the host.
    virtual int getLatencySamples() const { return 0; }

protected:
    explicit PedalNode (juce::AudioProcessorValueTreeState& state) : apvts (state) {}

    float p (const char* paramId) const { return apvts.getRawParameterValue (paramId)->load(); }
    bool pBool (const char* paramId) const { return p (paramId) > 0.5f; }

    // Generic click-free toggle: snapshots `buffer` as dry, invokes `runWet`
    // (expected to run the wrapped module fully in place), then crossfades
    // the result back toward the dry snapshot over the same ~15ms ramp
    // Comp/Klon/TS9 already used before this class existed. Bit-identical to
    // that prior behaviour when `active == wasActive` and the ramp is
    // already settled: mix stays pinned at 1.0 (steady on: w[i]*1 + d[i]*0
    // == w[i] exactly) or the whole block is skipped (steady off). For
    // stages that previously hard-cut on toggle (Gate, Amp, Cab, Reverb,
    // EQ), this is a disclosed, intentional click-free improvement rather
    // than a neutral refactor.
    template <typename RunWetFn>
    bool crossfadeToggle (juce::AudioBuffer<float>& buffer, bool active, RunWetFn&& runWet)
    {
        if (! active && ! wasActive)
            return false;

        dryScratch.setSize (buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            dryScratch.copyFrom (ch, 0, buffer, ch, 0, buffer.getNumSamples());

        runWet (buffer);

        wetAmount.setTargetValue (active ? 1.0f : 0.0f);
        const auto channels = juce::jmin (buffer.getNumChannels(), dryScratch.getNumChannels());
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto m = wetAmount.getNextValue();
            for (int ch = 0; ch < channels; ++ch)
            {
                auto* w = buffer.getWritePointer (ch);
                const auto* d = dryScratch.getReadPointer (ch);
                w[i] = d[i] * (1.0f - m) + w[i] * m;
            }
        }
        wasActive = active || wetAmount.isSmoothing();
        return wasActive;
    }

    void prepareCrossfade (double sampleRate, bool startActive)
    {
        wetAmount.reset (sampleRate, 0.015);
        wetAmount.setCurrentAndTargetValue (startActive ? 1.0f : 0.0f);
        wasActive = startActive;
    }

    juce::AudioProcessorValueTreeState& apvts;

private:
    juce::AudioBuffer<float> dryScratch;
    juce::SmoothedValue<float> wetAmount;
    bool wasActive = false;
};
