#pragma once

#include "../PedalNode.h"
#include "../ChorusModule.h"

// Same "keep the module's own existing fade-then-reset machinery" reasoning
// as TremoloNode -- calling reset() directly instead would snap D-C-V's wet
// mix to 0 instantly, an audible click at Vibrato (100% wet, no dry
// reference to soften the cut).
class ChorusNode : public PedalNode
{
public:
    explicit ChorusNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "chorus"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        chorus.prepare (spec);
        wasActive = false;
    }
    void reset() override { chorus.reset(); wasActive = false; }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("wetFxSectionOn") && pBool ("chorusOn");
        const auto waveform = ((int) p ("chorusWaveform")) == 1
            ? ChorusModule::Waveform::triangle : ChorusModule::Waveform::sine;

        if (active)
        {
            // D-C-V picks the character (Dry always forces silence outright,
            // regardless of Mix -- selecting Dry means "off"); Chorus and
            // Vibrato both hand the actual wet amount to the Mix knob.
            const auto dcvIndex = juce::jlimit (0, 2, (int) p ("chorusDCV"));
            const auto wetPercent = dcvIndex == 0 ? 0.0f : p ("chorusMix");
            chorus.setParameters (p ("chorusRate"), p ("chorusDepth"), p ("chorusLag"),
                                  waveform, wetPercent, true);
            chorus.process (buffer);
            wasActive = true;
        }
        else if (wasActive)
        {
            chorus.setParameters (p ("chorusRate"), p ("chorusDepth"), p ("chorusLag"),
                                  waveform, 0.0f, false);
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
    ChorusModule chorus;
    bool wasActive = false;
};
