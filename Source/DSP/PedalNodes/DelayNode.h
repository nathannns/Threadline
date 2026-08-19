#pragma once

#include "../PedalNode.h"
#include "../EchoModule.h"
#include "../CarbonCopyModule.h"
#include "../TapTempo.h"

// Composite/atomic unit, not decomposed into two orderable slots: Plexer
// (EchoModule) and Copier (CarbonCopyModule) share one Delay slot and one
// on/off toggle, with delayModel picking which engine is actually in the
// signal path -- exactly as before this refactor. Both engines keep driving
// their own existing fade-then-reset machinery directly, same reasoning as
// TremoloNode/ChorusNode, so flipping delayModel while enabled fades the
// old engine out while the new one fades in rather than cutting either
// abruptly.
class DelayNode : public PedalNode
{
public:
    explicit DelayNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "delay"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        echo.prepare (spec);
        copier.prepare (spec);
        echoWasActive = copierWasActive = false;
    }
    void reset() override { echo.reset(); copier.reset(); echoWasActive = copierWasActive = false; }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto delayOn = inTargetOrder && pBool ("wetFxSectionOn") && pBool ("echoOn");
        const auto plexerSelected = ((int) p ("delayModel")) == 0;
        const auto plexerActive = delayOn && plexerSelected;
        const auto copierActive = delayOn && ! plexerSelected;

        const auto echoMode = ((int) p ("echoMode")) == 1
            ? EchoModule::Mode::soundOnSound : EchoModule::Mode::echo;
        // Sync: time from the global tap tempo + each engine's own note-
        // division choice instead of its own Time knob (see TapTempo.h).
        const auto echoTimeMs = pBool ("echoSync")
            ? TapTempo::timeMsForDivision (p ("tapTempoBpm"), (int) p ("echoDivision"))
            : p ("echoTime");
        const auto carbonTimeMs = pBool ("carbonSync")
            ? TapTempo::timeMsForDivision (p ("tapTempoBpm"), (int) p ("carbonDivision"))
            : p ("carbonTime");
        if (plexerActive)
        {
            echo.setParameters (echoTimeMs, p ("echoSustain"), p ("echoVolume"), true, echoMode);
            echo.process (buffer);
            echoWasActive = true;
        }
        else if (echoWasActive)
        {
            echo.setParameters (echoTimeMs, p ("echoSustain"), p ("echoVolume"), false, echoMode);
            echo.process (buffer);
            if (! echo.isWetTransitionActive())
            {
                echo.reset();
                echoWasActive = false;
            }
        }

        if (copierActive)
        {
            copier.setParameters (carbonTimeMs, p ("carbonRegen"), p ("carbonMix"), pBool ("carbonMod"), true);
            copier.process (buffer);
            copierWasActive = true;
        }
        else if (copierWasActive)
        {
            copier.setParameters (carbonTimeMs, p ("carbonRegen"), p ("carbonMix"), pBool ("carbonMod"), false);
            copier.process (buffer);
            if (! copier.isWetTransitionActive())
            {
                copier.reset();
                copierWasActive = false;
            }
        }
        return echoWasActive || copierWasActive;
    }

private:
    EchoModule echo;
    CarbonCopyModule copier;
    bool echoWasActive = false, copierWasActive = false;
};
