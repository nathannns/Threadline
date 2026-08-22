#pragma once

#include "../PedalNode.h"
#include "../HallRoomReverbModule.h"

class ReverbNode : public PedalNode
{
public:
    explicit ReverbNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "reverb"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        reverb.prepare (spec);
        prepareCrossfade (spec, pBool ("wetFxSectionOn") && pBool ("reverbOn"));
    }
    void reset() override { reverb.reset(); }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("wetFxSectionOn") && pBool ("reverbOn");
        return crossfadeToggle (buffer, active, [this] (juce::AudioBuffer<float>& b)
        {
            reverb.setParameters (p ("reverbPreDelay"), p ("reverbDecay"), p ("reverbTone"),
                                   p ("reverbMix"), p ("reverbWidth"), true, (int) p ("reverbModel"));
            reverb.process (b);
        });
    }

private:
    HallRoomReverbModule reverb;
};
