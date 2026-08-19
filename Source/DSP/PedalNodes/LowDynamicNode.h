#pragma once

#include "../PedalNode.h"
#include "../LowDynamicModule.h"

class LowDynamicNode : public PedalNode
{
public:
    explicit LowDynamicNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "lowDynamic"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        lowDynamic.prepare (spec);
        prepareCrossfade (spec.sampleRate, pBool ("lowDynamicOn"));
    }
    void reset() override { lowDynamic.reset(); }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("lowDynamicOn");
        return crossfadeToggle (buffer, active, [this] (juce::AudioBuffer<float>& b)
        {
            lowDynamic.setEnabled (true);
            lowDynamic.setParameters (p ("lowDynamicUp"), p ("lowDynamicDown"),
                pBool ("lowDynamicFast"), p ("lowDynamicMix"));
            lowDynamic.process (b);
        });
    }

private:
    LowDynamicModule lowDynamic;
};
