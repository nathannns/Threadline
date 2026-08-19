#pragma once

#include "../PedalNode.h"
#include "../CabModule.h"

// A single IR loader (previously two, cabA/cabB, processed in parallel and
// blended -- simplified down to one at the user's request). cabA* param ids
// are kept internally (cabB*/cabBlend stay registered but deprecated in
// place -- never remove a shipped AudioParameter, same treatment as
// odOrder/klonOversampling/ts9Oversampling elsewhere in this codebase) so
// existing sessions/presets that reference them don't break, even though
// only cabA* now drives anything audible.
class CabUnitNode : public PedalNode
{
public:
    explicit CabUnitNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "cab"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        cab.prepare (spec);
        const auto sel = (int) p ("cabAIRSelect");
        if (sel < CabModule::numBuiltInIRs) cab.loadBuiltInIR (sel);
        lastIRSelection = sel;
        const auto ampSectionOn = pBool ("ampSectionOn");
        prepareCrossfade (spec.sampleRate, ampSectionOn && pBool ("cabAOn"));
    }
    void reset() override { cab.reset(); }

    int getLatencySamples() const override { return cab.getLatencySamples(); }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto ampSectionOn = pBool ("ampSectionOn");
        const auto active = inTargetOrder && ampSectionOn && pBool ("cabAOn");

        return crossfadeToggle (buffer, active, [this] (juce::AudioBuffer<float>& b)
        {
            cab.setEnabled (true);
            cab.setMix (p ("cabAMix"));
            cab.setPhaseInverted (pBool ("cabAPhase"));
            const auto sel = (int) p ("cabAIRSelect");
            if (sel != lastIRSelection && sel < CabModule::numBuiltInIRs) cab.loadBuiltInIR (sel);
            lastIRSelection = sel;
            cab.process (b);
        });
    }

private:
    CabModule cab;
    int lastIRSelection = -1;
};
