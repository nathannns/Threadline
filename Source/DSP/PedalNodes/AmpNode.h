#pragma once

#include "../PedalNode.h"
#include "../AmpModule.h"

// Same 3-oversampling-instance pattern as KlonNode/TS9Node -- prepared up
// front so changing quality never allocates on the audio thread.
class AmpNode : public PedalNode
{
public:
    explicit AmpNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "amp"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        for (int mode = 0; mode < (int) amps.size(); ++mode)
            amps[(size_t) mode].prepare (spec, mode);
        prepareCrossfade (spec.sampleRate, pBool ("ampSectionOn") && pBool ("ampOn"));
    }
    void reset() override { for (auto& a : amps) a.reset(); }

    // Pinned to the 4x instance regardless of the live oversampling setting,
    // so the reported total stays constant across a live quality switch.
    // See KlonNode's own comment -- reports the live-selected instance's
    // real latency now that PedalChainRunner watches for oversampling
    // changes and re-publishes accordingly.
    int getLatencySamples() const override
    {
        return amps[(size_t) juce::jlimit (0, 2, (int) p ("ampOversampling"))].getLatencySamples();
    }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("ampSectionOn") && pBool ("ampOn");
        auto& selected = amps[(size_t) juce::jlimit (0, 2, (int) p ("ampOversampling"))];
        return crossfadeToggle (buffer, active, [this, &selected] (juce::AudioBuffer<float>& b)
        {
            selected.setEnabled (true);
            selected.setParameters (p ("ampDrive"), p ("ampTone"), p ("ampOutput"),
                static_cast<AmpModule::Voice> (juce::jlimit (0, 5, (int) p ("ampVoice"))),
                p ("ampBass"), p ("ampMid"), p ("ampTreble"));
            selected.process (b);
        });
    }

private:
    std::array<AmpModule, 3> amps;
};
