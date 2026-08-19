#pragma once

#include "../PedalNode.h"
#include "../BisonModule.h"

// Same 3-oversampling-instance pattern as KlonNode/TS9Node/FangsNode.
class BisonNode : public PedalNode
{
public:
    explicit BisonNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "bison"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        for (int mode = 0; mode < (int) bisons.size(); ++mode)
            bisons[(size_t) mode].prepare (spec, mode);
        prepareCrossfade (spec.sampleRate, pBool ("bisonOn"));
    }
    void reset() override { for (auto& b : bisons) b.reset(); }

    // See KlonNode's own comment -- reports the live-selected instance's
    // real latency now that PedalChainRunner watches for oversampling
    // changes and re-publishes accordingly.
    int getLatencySamples() const override
    {
        return bisons[(size_t) juce::jlimit (0, 2, (int) p ("ampOversampling"))].getLatencySamples();
    }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("bisonOn");
        auto& selected = bisons[(size_t) juce::jlimit (0, 2, (int) p ("ampOversampling"))];
        return crossfadeToggle (buffer, active, [this, &selected] (juce::AudioBuffer<float>& b)
        {
            selected.setEnabled (true);
            selected.setParameters (p ("bisonSustain"), p ("bisonTone"), p ("bisonLevel"), p ("bisonMix"));
            selected.process (b);
        });
    }

private:
    std::array<BisonModule, 3> bisons;
};
