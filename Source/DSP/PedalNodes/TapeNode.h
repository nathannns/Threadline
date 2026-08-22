#pragma once

#include "../PedalNode.h"
#include "../TapeModule.h"

// Ported from Rockalizer's TapeModule (tape saturation/compression, not an
// overdrive). Drives the module's own existing fade-then-reset state
// machine (isWetTransitionActive()) directly rather than the generic
// crossfadeToggle wrapper, same reasoning as TremoloNode. Oversampling
// follows the global "ampOversampling" setting, same as Bull/Breaker --
// no separate Tape oversampling control.
class TapeNode : public PedalNode
{
public:
    TapeNode (juce::AudioProcessorValueTreeState& state, ProcessingQualityState& quality)
        : PedalNode (state, &quality) {}
    juce::Identifier getId() const override { return "tape"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        tape.prepare (spec);
        wasActive = false;
    }
    void reset() override { tape.reset(); wasActive = false; }
    void oversamplingModeChanged() override { tape.reset(); wasActive = false; }

    int getLatencySamples() const override { return tape.getLatencySamples(); }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("tapeOn");
        const auto selectedOversamplingMode = oversamplingMode();

        if (active)
        {
            tape.setParameters (p ("tapeDrive"), p ("tapeCompression"), p ("tapeTone"), p ("tapeAge"),
                p ("tapeMix"), p ("tapeVolume"), true, (int) p ("tapeType"), selectedOversamplingMode);
            tape.process (buffer);
            wasActive = true;
        }
        else if (wasActive)
        {
            tape.setParameters (p ("tapeDrive"), p ("tapeCompression"), p ("tapeTone"), p ("tapeAge"),
                p ("tapeMix"), p ("tapeVolume"), false, (int) p ("tapeType"), selectedOversamplingMode);
            tape.process (buffer);
            if (! tape.isWetTransitionActive())
            {
                tape.reset();
                wasActive = false;
            }
        }
        return wasActive;
    }

private:
    TapeModule tape;
    bool wasActive = false;
};
