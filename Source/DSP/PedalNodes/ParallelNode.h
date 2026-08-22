#pragma once

#include "../PedalNode.h"
#include "../PedalboardOrder.h"
#include <functional>

// A single fixed two-slot parallel-blend container: Slot A and Slot B each
// hold a *reference* to one of the other already-registered PedalNode
// singletons in PedalChainRunner's own registry (never a new instance), so
// no pedal's parameters ever exist twice. `resolver` (supplied by
// PedalChainRunner, which owns the registry) looks a chosen id up by
// string; PedalboardOrder::parallelSlotChoiceIds() maps each
// parallelSlotA/parallelSlotB AudioParameterChoice index (1-based, 0 =
// "None") back to that id.
//
// Whichever pedal is assigned into a slot keeps working exactly as it does
// when run serially -- its own On toggle, its own knobs, its own
// crossfade -- this box only decides *when* that pedal's updateAndProcess()
// gets called (here, on its own buffer copy, instead of inline in the main
// chain) and how the two results are blended back together.
//
// Correctness depends on the pedalboard UI (PedalboardComponent's add-menu
// + PedalTiles.h's ParallelTile) keeping every pedal id single-instance
// across the whole board -- a pedal assigned into a slot must never also
// sit in the main serial order at the same time, or its updateAndProcess()
// would run twice in one block (once here, once in PedalChainRunner's own
// loop), corrupting its internal crossfade smoothing for that block. Both
// UI surfaces enforce this from the same live state on a short poll timer;
// see ParallelTile's own comment for the (intentionally accepted) brief
// race window during a manual reassignment.
//
// Each branch is delay-compensated to the slower assigned node before the
// blend. Without this, pairing a zero-latency effect with an oversampled one
// produced phase/comb filtering even though host PDC correctly saw the
// container's maximum branch latency.
class ParallelNode : public PedalNode
{
public:
    ParallelNode (juce::AudioProcessorValueTreeState& state, std::function<PedalNode* (const juce::String&)> resolveFn)
        : PedalNode (state), resolver (std::move (resolveFn)) {}

    juce::Identifier getId() const override { return "parallel"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        lastSpec = spec;
        bufferA.setSize (static_cast<int> (spec.numChannels), static_cast<int> (spec.maximumBlockSize), false, false, true);
        bufferB.setSize (static_cast<int> (spec.numChannels), static_cast<int> (spec.maximumBlockSize), false, false, true);
        compensationA.prepare (spec);
        compensationB.prepare (spec);
        prepareCrossfade (spec, pBool ("parallelOn"));
    }

    void reset() override
    {
        compensationA.reset();
        compensationB.reset();
        lastCompensationA = lastCompensationB = -1;
    }

    int getLatencySamples() const override
    {
        auto* nodeA = resolveSlot ((int) p ("parallelSlotA"));
        auto* nodeB = resolveSlot (dedupedSlotB());
        const auto latA = nodeA != nullptr ? nodeA->getLatencySamples() : 0;
        const auto latB = nodeB != nullptr ? nodeB->getLatencySamples() : 0;
        return juce::jmax (latA, latB);
    }

    bool routesNode (const PedalNode* node) const
    {
        if (! pBool ("parallelOn") || node == nullptr)
            return false;
        return resolveSlot (static_cast<int> (p ("parallelSlotA"))) == node
            || resolveSlot (dedupedSlotB()) == node;
    }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("parallelOn");

        return crossfadeToggle (buffer, active, [this] (juce::AudioBuffer<float>& b)
        {
            auto* nodeA = resolveSlot ((int) p ("parallelSlotA"));
            auto* nodeB = resolveSlot (dedupedSlotB());

            if (nodeA == nullptr && nodeB == nullptr)
                return; // nothing assigned yet -- leave b untouched (dry)

            if (nodeA == nullptr) { nodeB->updateAndProcess (b, true); return; }
            if (nodeB == nullptr) { nodeA->updateAndProcess (b, true); return; }

            bufferA.makeCopyOf (b, true);
            bufferB.makeCopyOf (b, true);
            nodeA->updateAndProcess (bufferA, true);
            nodeB->updateAndProcess (bufferB, true);

            const auto latencyA = nodeA->getLatencySamples();
            const auto latencyB = nodeB->getLatencySamples();
            compensate (bufferA, compensationA, latencyB - latencyA, lastCompensationA);
            compensate (bufferB, compensationB, latencyA - latencyB, lastCompensationB);

            const auto blend = juce::jlimit (0.0f, 1.0f, p ("parallelBlend") / 100.0f);
            const auto channels = juce::jmin (b.getNumChannels(),
                                               juce::jmin (bufferA.getNumChannels(), bufferB.getNumChannels()));
            for (int ch = 0; ch < channels; ++ch)
            {
                auto* out = b.getWritePointer (ch);
                const auto* a = bufferA.getReadPointer (ch);
                const auto* wetB = bufferB.getReadPointer (ch);
                for (int i = 0; i < b.getNumSamples(); ++i)
                    out[i] = a[i] * (1.0f - blend) + wetB[i] * blend;
            }
        });
    }

private:
    static void compensate (juce::AudioBuffer<float>& buffer,
                            juce::dsp::DelayLine<float>& delay,
                            int delaySamples,
                            int& previousDelaySamples)
    {
        delaySamples = juce::jmax (0, delaySamples);
        if (delaySamples != previousDelaySamples)
        {
            delay.reset();
            previousDelaySamples = delaySamples;
        }
        delay.setDelay (static_cast<float> (delaySamples));
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                delay.pushSample (channel, buffer.getSample (channel, sample));
                buffer.setSample (channel, sample, delay.popSample (channel));
            }
    }

    PedalNode* resolveSlot (int choiceIndex) const
    {
        if (choiceIndex <= 0)
            return nullptr;
        const auto& ids = PedalboardOrder::parallelSlotChoiceIds();
        const auto idx = choiceIndex - 1;
        if (idx < 0 || idx >= ids.size())
            return nullptr;
        return resolver (ids[idx]);
    }

    // Slot B is silently treated as unassigned when it names the same
    // pedal as Slot A -- calling one pedal's updateAndProcess() twice in
    // the same block (once "as A", once "as B") would corrupt its own
    // crossfade smoothing, which assumes exactly one call per block.
    int dedupedSlotB() const
    {
        const auto a = (int) p ("parallelSlotA");
        const auto b = (int) p ("parallelSlotB");
        return b == a ? 0 : b;
    }

    std::function<PedalNode* (const juce::String&)> resolver;
    juce::AudioBuffer<float> bufferA, bufferB;
    juce::dsp::DelayLine<float> compensationA { 512 }, compensationB { 512 };
    int lastCompensationA = -1, lastCompensationB = -1;
    juce::dsp::ProcessSpec lastSpec {};
};
