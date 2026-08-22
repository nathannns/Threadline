#pragma once

#include "../PedalNode.h"
#include "../PeakLevel.h"

// Same "not really a bypassable pedal, but modeled as an ordinary PedalNode
// anyway" reasoning as InputGainNode -- see its own comment.
class OutputGainNode : public PedalNode
{
public:
    OutputGainNode (juce::AudioProcessorValueTreeState& state, PeakLevel& meter)
        : PedalNode (state), outputLevel (meter) {}
    juce::Identifier getId() const override { return "outputGain"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override { prepareCrossfade (spec, true); }
    void reset() override {}

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto result = crossfadeToggle (buffer, inTargetOrder, [this] (juce::AudioBuffer<float>& b)
        {
            // Match Rockalizer's calibrated nominal output so the same shared
            // effects at the same settings do not become 1.8 dB quieter merely
            // because they are hosted by Threadline.
            constexpr float outputCalibrationDb = 1.8f;
            b.applyGain (juce::Decibels::decibelsToGain (p ("outputGain") + outputCalibrationDb));
        });
        outputLevel.updateFrom (buffer);
        return result;
    }

private:
    PeakLevel& outputLevel;
};
