#pragma once

#include "../PedalNode.h"
#include "../DeskModule.h"

class DeskNode : public PedalNode
{
public:
    explicit DeskNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "desk"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        desk.prepare (spec);
        prepareCrossfade (spec.sampleRate, pBool ("deskOn"));
    }
    void reset() override { desk.reset(); }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("deskOn");
        return crossfadeToggle (buffer, active, [this] (juce::AudioBuffer<float>& b)
        {
            desk.setEnabled (true);
            // Style choice picks the curve's steepness (n in 1-(1-x)^n).
            const auto stylePower = 1.5f + p ("deskStyle") * 0.75f; // 3 stops: 1.5/2.25/3.0
            desk.setParameters (p ("deskAmount"), stylePower);
            desk.process (b);
        });
    }

private:
    DeskModule desk;
};
