#pragma once

#include "../PedalNode.h"
#include "../ChannelEQModule.h"

class ChannelEQNode : public PedalNode
{
public:
    explicit ChannelEQNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "channelEQ"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        eq.prepare (spec);
        prepareCrossfade (spec, pBool ("channelEQOn"));
    }
    void reset() override { eq.reset(); }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("channelEQOn");
        return crossfadeToggle (buffer, active, [this] (juce::AudioBuffer<float>& b)
        {
            eq.setEnabled (true);
            eq.setParameters (p ("channelEQGain"), (int) p ("channelEQLowFreq"), p ("channelEQLowGain"),
                (int) p ("channelEQMidFreq"), p ("channelEQMidGain"), p ("channelEQHighGain"),
                (int) p ("channelEQHpfFreq"), pBool ("channelEQHpfOn"));
            eq.process (b);
        });
    }

private:
    ChannelEQModule eq;
};
