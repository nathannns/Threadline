#pragma once

#include "../PedalNode.h"
#include "../KlonModule.h"

// 3 fully-prepared instances, one per oversampling mode -- same hot-
// switchable pattern as AmpNode -- so changing quality never allocates on
// the audio thread; the active one is picked each block by its own param.
class KlonNode : public PedalNode
{
public:
    explicit KlonNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "klon"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        for (int mode = 0; mode < (int) klons.size(); ++mode)
            klons[(size_t) mode].prepare (spec, mode);
        prepareCrossfade (spec.sampleRate, pBool ("preFxSectionOn") && pBool ("klonOn"));
    }
    void reset() override { for (auto& k : klons) k.reset(); }

    // Pinned to the 4x instance regardless of the live oversampling setting,
    // matching the plugin's existing latency-reporting convention.
    int getLatencySamples() const override { return klons.back().getLatencySamples(); }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("preFxSectionOn") && pBool ("klonOn");
        // Follows the global Amp Oversampling setting (Options panel) --
        // klonOversampling stays registered but deprecated in place, same
        // treatment as odOrder, so old sessions/hosts don't break.
        auto& selected = klons[(size_t) juce::jlimit (0, 2, (int) p ("ampOversampling"))];
        return crossfadeToggle (buffer, active, [this, &selected] (juce::AudioBuffer<float>& b)
        {
            selected.setEnabled (true);
            selected.setParameters (p ("klonGain"), p ("klonTreble"), p ("klonLevel"));
            selected.process (b);
        });
    }

private:
    std::array<KlonModule, 3> klons;
};
