#pragma once

#include "../PedalNode.h"
#include "../JCChorusModule.h"

// "JC Chorus" (Roland JC-120's BBD chorus line as its own pedal) -- same
// fade-then-reset bypass as DimensionChorusNode: the module's own mix
// smoothing drives the click-free in/out transition, and reset() is only
// called once the fade has fully settled (snapping the wet mix to 0 earlier
// would click).
class JCChorusNode : public PedalNode
{
public:
    explicit JCChorusNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "jcChorus"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        chorus.prepare (spec);
        wasActive = false;
    }
    void reset() override { chorus.reset(); wasActive = false; }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("jcChorusOn");

        if (active)
        {
            chorus.setParameters (p ("jcChorusRate"), p ("jcChorusDepth"), p ("jcChorusMix"),
                                  true, static_cast<int> (p ("jcChorusMode")));
            chorus.process (buffer);
            wasActive = true;
        }
        else if (wasActive)
        {
            chorus.setParameters (p ("jcChorusRate"), p ("jcChorusDepth"), p ("jcChorusMix"),
                                  false, static_cast<int> (p ("jcChorusMode")));
            chorus.process (buffer);
            if (! chorus.isWetTransitionActive())
            {
                chorus.reset();
                wasActive = false;
            }
        }
        return wasActive;
    }

private:
    JCChorusModule chorus;
    bool wasActive = false;
};
