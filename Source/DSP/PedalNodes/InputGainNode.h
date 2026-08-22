#pragma once

#include "../PedalNode.h"
#include "../PeakLevel.h"

// Not a bypassable "pedal" in the traditional sense (no On/Off knob of its
// own) -- just a gain trim -- but made into an ordinary PedalNode anyway so
// the chain runner needs zero position-coupling special cases: it's always
// "active" whenever present in the board, and crossfades to a transparent
// bypass like everything else when removed.
class InputGainNode : public PedalNode
{
public:
    InputGainNode (juce::AudioProcessorValueTreeState& state, PeakLevel& meter)
        : PedalNode (state), inputLevel (meter) {}
    juce::Identifier getId() const override { return "inputGain"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override { prepareCrossfade (spec, true); }
    void reset() override {}

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto result = crossfadeToggle (buffer, inTargetOrder, [this] (juce::AudioBuffer<float>& b)
        {
            // Guitar-mode offset derived from a real interface gain-staging
            // table (average of Focusrite Scarlett 2i2 4th Gen: +12.0dBu,
            // and 3rd Gen: +12.5dBu max instrument input level, gain trim at
            // minimum -> +12.25dBu average), the same methodology used by
            // other amp-sim plugins to calibrate against an interface's
            // max-input-at-0dBFS spec rather than guessing a flat number.
            // Four independent amp-sim vendors in that same table (Neural
            // DSP, Nembrini/Plugin Alliance, Bogren Digital, UAD UAFX)
            // converge on the same +12.2dBu internal reference level, which
            // Threadline targets too: offset = interfaceMaxInputDbu -
            // referenceDbu = 12.25 - 12.2 = +0.05dB.
            constexpr float focusriteAverageMaxInputDbu = (12.0f + 12.5f) * 0.5f; // 2i2 4th Gen, 3rd Gen
            constexpr float ampSimReferenceDbu = 12.2f; // Neural DSP / Nembrini / Bogren / UAD UAFX convention
            constexpr float guitarCalibrationDb = focusriteAverageMaxInputDbu - ampSimReferenceDbu;
            const auto calibrationDb = ((int) p ("inputSource") == 0) ? guitarCalibrationDb : 0.0f; // Guitar : Line
            b.applyGain (juce::Decibels::decibelsToGain (p ("inputGain") + calibrationDb));
        });
        // Taps at the exact same point the meter always has: right after
        // Input Gain applies, regardless of whether this node is present.
        inputLevel.updateFrom (buffer);
        return result;
    }

private:
    PeakLevel& inputLevel;
};
