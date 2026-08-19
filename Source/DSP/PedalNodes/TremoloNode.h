#pragma once

#include "../PedalNode.h"
#include "../TremoloModule.h"

// Drives TremoloModule's own existing fade-then-reset state machine
// (isWetTransitionActive()) directly rather than the generic
// crossfadeToggle wrapper -- that machinery already existed and is already
// proven click-free, so it's reused as-is instead of double-wrapping.
class TremoloNode : public PedalNode
{
public:
    explicit TremoloNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "tremolo"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        tremolo.prepare (spec);
        wasActive = false;
    }
    void reset() override { tremolo.reset(); wasActive = false; }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("wetFxSectionOn")
                          && pBool ("tremOn") && p ("tremAmount") > 0.001f;
        tremolo.setVoice (static_cast<TremoloModule::Voice> (juce::jlimit (0, 1, (int) p ("tremVoice"))));

        if (active)
        {
            tremolo.setAmount (p ("tremAmount"));
            tremolo.setRate (p ("tremRate"));
            tremolo.process (buffer);
            wasActive = true;
        }
        else if (wasActive)
        {
            tremolo.setAmount (0.0f);
            tremolo.setRate (p ("tremRate"));
            tremolo.process (buffer);
            if (! tremolo.isWetTransitionActive())
            {
                tremolo.reset();
                wasActive = false;
            }
        }
        return wasActive;
    }

private:
    TremoloModule tremolo;
    bool wasActive = false;
};
