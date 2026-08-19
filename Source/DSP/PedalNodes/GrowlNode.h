#pragma once

#include "../PedalNode.h"
#include "../GrowlModule.h"

// Same 3-oversampling-instance pattern as KlonNode/TS9Node/FangsNode.
class GrowlNode : public PedalNode
{
public:
    explicit GrowlNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "growl"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        for (int mode = 0; mode < (int) growls.size(); ++mode)
            growls[(size_t) mode].prepare (spec, mode);
        prepareCrossfade (spec.sampleRate, pBool ("growlOn"));
    }
    void reset() override { for (auto& g : growls) g.reset(); }

    int getLatencySamples() const override { return growls.back().getLatencySamples(); }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("growlOn");
        auto& selected = growls[(size_t) juce::jlimit (0, 2, (int) p ("ampOversampling"))];
        return crossfadeToggle (buffer, active, [this, &selected] (juce::AudioBuffer<float>& b)
        {
            selected.setEnabled (true);
            selected.setParameters (p ("growlBias"), p ("growlFuzz"), p ("growlLevel"), p ("growlMix"));
            selected.process (b);
        });
    }

private:
    std::array<GrowlModule, 3> growls;
};
