#pragma once

#include "../PedalNode.h"
#include "../SpringModule.h"

// Ported from Rockalizer's SpringModule (spring-tank reverb: a short IR
// onset per model, cross-faded into a synthesized dispersion/late-field
// tail) -- a second, architecturally distinct reverb pedal alongside the
// existing Hall/Room/Plate ReverbNode, not another mode of it. Same
// isWetTransitionActive()-driven fade pattern as TremoloNode/TapeNode.
class SpringNode : public PedalNode
{
public:
    explicit SpringNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "spring"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        spring.prepare (spec);
        wasActive = false;
    }
    void reset() override { spring.reset(); wasActive = false; }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("springOn");

        if (active)
        {
            spring.setParameters (p ("springDecay"), p ("springDwell"), p ("springTone"), p ("springDrip"),
                p ("springMix"), true, (int) p ("springModel"));
            spring.process (buffer);
            wasActive = true;
        }
        else if (wasActive)
        {
            spring.setParameters (p ("springDecay"), p ("springDwell"), p ("springTone"), p ("springDrip"),
                p ("springMix"), false, (int) p ("springModel"));
            spring.process (buffer);
            if (! spring.isWetTransitionActive())
            {
                spring.reset();
                wasActive = false;
            }
        }
        return wasActive;
    }

private:
    SpringModule spring;
    bool wasActive = false;
};
