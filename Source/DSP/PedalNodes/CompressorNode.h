#pragma once

#include "../PedalNode.h"
#include "../CompressorModule.h"

class CompressorNode : public PedalNode
{
public:
    explicit CompressorNode (juce::AudioProcessorValueTreeState& state) : PedalNode (state) {}
    juce::Identifier getId() const override { return "compressor"; }

    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        compressor.prepare (spec);
        prepareCrossfade (spec.sampleRate, pBool ("preFxSectionOn") && pBool ("compOn"));
    }
    void reset() override { compressor.reset(); }

    bool updateAndProcess (juce::AudioBuffer<float>& buffer, bool inTargetOrder) override
    {
        const auto active = inTargetOrder && pBool ("preFxSectionOn") && pBool ("compOn");
        return crossfadeToggle (buffer, active, [this] (juce::AudioBuffer<float>& b)
        {
            compressor.setEnabled (true);
            // Legacy param ids kept so old sessions resolve, while the
            // controls now drive the Diamond-inspired optical topology --
            // compThreshold/compRatio/compAttack/compRelease/compMakeup map
            // to compressionPercent/attackPercent/tiltPercent/midDb/levelDb.
            compressor.setParameters (p ("compThreshold"), p ("compRatio"), p ("compAttack"),
                                       p ("compRelease"), p ("compMakeup"));
            compressor.process (b);
        });
    }

    float getCurrentGainReductionDb() const noexcept { return compressor.getCurrentGainReductionDb(); }

private:
    CompressorModule compressor;
};
