#pragma once

#include "../PedalNode.h"
#include "../NoiseGateModule.h"

class NoiseGateNode : public PedalNode
{
public:
    explicit NoiseGateNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "noiseGate"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        gate.prepare (spec);
        prepareCrossfade (spec.sampleRate, pBool ("gateOn"));
    }
    void reset() override { gate.reset(); }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("gateOn");
        return crossfadeToggle (buffer, active, [this] (juce::AudioBuffer<float>& b)
        {
            gate.setEnabled (true);
            gate.setAmount (p ("gateAmount"));
            gate.process (b);
        });
    }

private:
    NoiseGateModule gate;
};
