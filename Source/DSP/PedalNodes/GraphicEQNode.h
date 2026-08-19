#pragma once

#include "../PedalNode.h"
#include "../GraphicEQModule.h"

class GraphicEQNode : public PedalNode
{
public:
    explicit GraphicEQNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "eq"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        eq.prepare (spec);
        prepareCrossfade (spec.sampleRate, pBool ("eqSectionOn") && pBool ("eqOn"));
    }
    void reset() override { eq.reset(); }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto eqSectionOn = pBool ("eqSectionOn");
        const auto active = inTargetOrder && eqSectionOn && pBool ("eqOn");
        return crossfadeToggle (buffer, active, [this, eqSectionOn] (juce::AudioBuffer<float>& b)
        {
            eq.setEnabled (true);
            std::array<float, GraphicEQModule::numBands> bandGains {
                p ("eqBand1"), p ("eqBand2"), p ("eqBand3"), p ("eqBand4"), p ("eqBand5"),
                p ("eqBand6"), p ("eqBand7"), p ("eqBand8"), p ("eqBand9")
            };
            eq.setBandGains (bandGains);
            eq.setHighPass (eqSectionOn && pBool ("eqHpfOn"), p ("eqHpfFreq"));
            eq.setLowPass (eqSectionOn && pBool ("eqLpfOn"), p ("eqLpfFreq"));
            eq.process (b);
        });
    }

private:
    GraphicEQModule eq;
};
