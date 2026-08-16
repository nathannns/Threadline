#pragma once

#include "SectionBuilder.h"
#include "../PluginProcessor.h"

// Page 1 — Compressor, Klon, TS9.
class Page1Component : public juce::Component
{
public:
    explicit Page1Component (ThreadlineAudioProcessor& processor);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    SectionUI compSection, klonSection, ts9Section;
};
