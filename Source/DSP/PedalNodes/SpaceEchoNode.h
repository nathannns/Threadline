#pragma once

#include "../PedalNode.h"
#include "../SpaceEchoModule.h"

// Ported from Rockalizer's EchoModule -- a Roland RE-201-style 3-head tape
// echo, a separate pedal from Plexer/Copier (see SpaceEchoModule.h for
// why). Same isWetTransitionActive()-driven fade pattern as
// TremoloNode/TapeNode/SpringNode.
class SpaceEchoNode : public PedalNode
{
public:
    explicit SpaceEchoNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "spaceEcho"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        echo.prepare (spec);
        wasActive = false;
    }
    void reset() override { echo.reset(); wasActive = false; }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("spaceEchoOn");

        if (active)
        {
            echo.setParameters (p ("spaceEchoTime"), p ("spaceEchoRepeats"), p ("spaceEchoBass"),
                p ("spaceEchoTreble"), p ("spaceEchoWobble"), p ("spaceEchoDrive"), p ("spaceEchoMix"),
                p ("spaceEchoReverb"), true, (int) p ("spaceEchoPattern"));
            echo.process (buffer);
            wasActive = true;
        }
        else if (wasActive)
        {
            echo.setParameters (p ("spaceEchoTime"), p ("spaceEchoRepeats"), p ("spaceEchoBass"),
                p ("spaceEchoTreble"), p ("spaceEchoWobble"), p ("spaceEchoDrive"), p ("spaceEchoMix"),
                p ("spaceEchoReverb"), false, (int) p ("spaceEchoPattern"));
            echo.process (buffer);
            if (! echo.isWetTransitionActive())
            {
                echo.reset();
                wasActive = false;
            }
        }
        return wasActive;
    }

private:
    SpaceEchoModule echo;
    bool wasActive = false;
};
