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
            // The real SDD-320 lets you press several mode buttons at once;
            // the four bools fold into a 4-bit mask (bit 0 = Mode I ... bit
            // 3 = Mode IV) that the module sums into the wet signal.
            const auto mask = (pBool ("dimBbdMode1") ? 1 : 0)
                            | (pBool ("dimBbdMode2") ? 2 : 0)
                            | (pBool ("dimBbdMode3") ? 4 : 0)
                            | (pBool ("dimBbdMode4") ? 8 : 0);
            module.setParameters (mask,
                                  p ("dimBbdInput") * 0.01f,
                                  p ("dimBbdOutput") * 0.01f);
            module.process (b);
        });
    }

private:
    DimensionDBBDModule module;
};
