#pragma once

#include "../PedalNode.h"
#include "../DimensionChorusModule.h"

// Ported from Rockalizer's ChorusModule -- a Dimension D/SDD-320-style
// ensemble chorus with a one-button Flanger blend, a separate pedal from
// July (see DimensionChorusModule.h for why). Same isWetTransitionActive()-
// driven fade pattern as TremoloNode/TapeNode/SpringNode.
class DimensionChorusNode : public PedalNode
{
public:
    explicit DimensionChorusNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "dimChorus"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        chorus.prepare (spec);
        wasActive = false;
    }
    void reset() override { chorus.reset(); wasActive = false; }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("dimChorusOn");

        if (active)
        {
            chorus.setParameters (p ("dimChorusRate"), p ("dimChorusDepth"), p ("dimChorusWidth"),
                p ("dimChorusTone"), p ("dimChorusMix"), true, (int) p ("dimChorusFlangerMode"));
            chorus.process (buffer);
            wasActive = true;
        }
        else if (wasActive)
        {
            chorus.setParameters (p ("dimChorusRate"), p ("dimChorusDepth"), p ("dimChorusWidth"),
                p ("dimChorusTone"), p ("dimChorusMix"), false, (int) p ("dimChorusFlangerMode"));
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
    DimensionChorusModule chorus;
    bool wasActive = false;
};
