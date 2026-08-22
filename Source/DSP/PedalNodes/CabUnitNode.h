#pragma once

#include "../PedalNode.h"
#include "../CabModule.h"
#include "../CabStereoRouter.h"

// Two independent convolution loaders fed from the same pre-cab signal.
// With both enabled, Cab A becomes the left microphone/cab and Cab B the
// right one, creating a genuine stereo cabinet image instead of summing two
// IRs back to dual mono. The existing cabBlend parameter acts as a stereo
// balance: 50 keeps both sides at unity; either end solos that side.
class CabUnitNode : public PedalNode
{
public:
    explicit CabUnitNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "cab"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        cabA.prepare (spec);
        cabB.prepare (spec);
        scratchA.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
        scratchB.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
        loadSelectionIfNeeded (cabA, "cabAIRSelect", lastASelection);
        loadSelectionIfNeeded (cabB, "cabBIRSelect", lastBSelection);
        const auto active = pBool ("ampSectionOn") && (pBool ("cabAOn") || pBool ("cabBOn"));
        prepareCrossfade (spec, active);
    }

    void reset() override { cabA.reset(); cabB.reset(); }
    int getLatencySamples() const override { return juce::jmax (cabA.getLatencySamples(), cabB.getLatencySamples()); }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("ampSectionOn")
                         && (pBool ("cabAOn") || pBool ("cabBOn"));
        return crossfadeToggle (buffer, active, [this] (juce::AudioBuffer<float>& source)
        {
            const auto useA = pBool ("cabAOn");
            const auto useB = pBool ("cabBOn");
            const auto channels = source.getNumChannels();
            const auto samples = source.getNumSamples();
            if (samples > scratchA.getNumSamples() || channels > scratchA.getNumChannels())
                return;

            copyIntoScratch (source, scratchA);
            copyIntoScratch (source, scratchB);

            configure (cabA, "cabAIRSelect", "cabAMix", "cabAPhase", lastASelection);
            configure (cabB, "cabBIRSelect", "cabBMix", "cabBPhase", lastBSelection);
            if (useA) cabA.process (scratchA);
            if (useB) cabB.process (scratchB);

            CabStereoRouter::route (scratchA, scratchB, source, useA, useB,
                                    p ("cabBlend") * 0.01f);
        });
    }

private:
    void loadSelectionIfNeeded (CabModule& cab, const char* selectionId, int& lastSelection)
    {
        const auto selection = (int) p (selectionId);
        if (selection != lastSelection && selection < CabModule::numBuiltInIRs)
            cab.loadBuiltInIR (selection);
        lastSelection = selection;
    }

    void configure (CabModule& cab, const char* selectionId, const char* mixId,
                    const char* phaseId, int& lastSelection)
    {
        cab.setEnabled (true);
        cab.setMix (p (mixId));
        cab.setPhaseInverted (pBool (phaseId));
        loadSelectionIfNeeded (cab, selectionId, lastSelection);
    }

    static void copyIntoScratch (const juce::AudioBuffer<float>& source, juce::AudioBuffer<float>& target)
    {
        for (int ch = 0; ch < source.getNumChannels(); ++ch)
            target.copyFrom (ch, 0, source, ch, 0, source.getNumSamples());
    }

    CabModule cabA, cabB;
    juce::AudioBuffer<float> scratchA, scratchB;
    int lastASelection = -1, lastBSelection = -1;
};
