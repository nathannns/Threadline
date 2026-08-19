#pragma once

#include "../PedalNode.h"
#include "../FangsModule.h"

// Same 3-oversampling-instance pattern as KlonNode/TS9Node/AmpNode.
class FangsNode : public PedalNode
{
public:
    explicit FangsNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "fangs"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        for (int mode = 0; mode < (int) fangs.size(); ++mode)
            fangs[(size_t) mode].prepare (spec, mode);
        prepareCrossfade (spec.sampleRate, pBool ("fangsOn"));
    }
    void reset() override { for (auto& f : fangs) f.reset(); }

    // See KlonNode's own comment -- reports the live-selected instance's
    // real latency now that PedalChainRunner watches for oversampling
    // changes and re-publishes accordingly.
    int getLatencySamples() const override
    {
        return fangs[(size_t) juce::jlimit (0, 2, (int) p ("ampOversampling"))].getLatencySamples();
    }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("fangsOn");
        // Follows the global Amp Oversampling setting (Options panel),
        // same treatment as Bull/Breaker.
        auto& selected = fangs[(size_t) juce::jlimit (0, 2, (int) p ("ampOversampling"))];
        return crossfadeToggle (buffer, active, [this, &selected] (juce::AudioBuffer<float>& b)
        {
            selected.setEnabled (true);
            selected.setParameters (p ("fangsGain"), p ("fangsFilter"), p ("fangsLevel"), p ("fangsMix"));
            selected.process (b);
        });
    }

private:
    std::array<FangsModule, 3> fangs;
};
