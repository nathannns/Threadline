#pragma once

#include "../PedalNode.h"
#include "../TS9Module.h"

// Same 3-oversampling-instance pattern as KlonNode/AmpNode.
class TS9Node : public PedalNode
{
public:
    explicit TS9Node (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "ts9"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        for (int mode = 0; mode < (int) ts9s.size(); ++mode)
            ts9s[(size_t) mode].prepare (spec, mode);
        prepareCrossfade (spec.sampleRate, pBool ("preFxSectionOn") && pBool ("ts9On"));
    }
    void reset() override { for (auto& t : ts9s) t.reset(); }

    // See KlonNode's own comment -- reports the live-selected instance's
    // real latency now that PedalChainRunner watches for oversampling
    // changes and re-publishes accordingly.
    int getLatencySamples() const override
    {
        return ts9s[(size_t) juce::jlimit (0, 2, (int) p ("ampOversampling"))].getLatencySamples();
    }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("preFxSectionOn") && pBool ("ts9On");
        // Follows the global Amp Oversampling setting (Options panel) --
        // ts9Oversampling stays registered but deprecated in place, same
        // treatment as odOrder, so old sessions/hosts don't break.
        auto& selected = ts9s[(size_t) juce::jlimit (0, 2, (int) p ("ampOversampling"))];
        return crossfadeToggle (buffer, active, [this, &selected] (juce::AudioBuffer<float>& b)
        {
            selected.setEnabled (true);
            selected.setVoicing (static_cast<TS9Module::Voicing> (juce::jlimit (0, 2, (int) p ("ts9Variant"))));
            selected.setParameters (p ("ts9Drive"), p ("ts9Tone"), p ("ts9Level"));
            selected.process (b);
        });
    }

private:
    std::array<TS9Module, 3> ts9s;
};
