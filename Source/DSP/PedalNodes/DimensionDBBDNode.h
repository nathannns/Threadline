#pragma once

#include "../PedalNode.h"
#include "../DimensionDBBDModule.h"

// Circuit-faithful Roland/Boss DC-2 "Dimension C" BBD chorus -- a separate
// pedal from the existing "Ensemble" (DimensionChorusModule, a Rockalizer
// "inspired-by" 4-tap model). See DimensionDBBDModule.h for the provenance
// (DC-2 schematic + MN3007/MN3101/NE570 datasheets, calibrated to Roland's
// published delay figures). Click-free toggle via crossfadeToggle, same
// pattern as KlonNode/TS9Node.
class DimensionDBBDNode : public PedalNode
{
public:
    explicit DimensionDBBDNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "dimBbd"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        module.prepare (spec);
        prepareCrossfade (spec.sampleRate, pBool ("dimBbdOn"));
    }
    void reset() override { module.reset(); }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("dimBbdOn");
        return crossfadeToggle (buffer, active, [this] (juce::AudioBuffer<float>& b)
        {
            module.setParameters ((int) p ("dimBbdMode"),
                                  p ("dimBbdInput") * 0.01f,
                                  p ("dimBbdOutput") * 0.01f);
            module.process (b);
        });
    }

private:
    DimensionDBBDModule module;
};
