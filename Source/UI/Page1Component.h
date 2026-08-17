#pragma once

#include "SectionBuilder.h"
#include "../PluginProcessor.h"

// Page 1 — Compressor, Klon, TS9.
class Page1Component : public juce::Component, private juce::Timer
{
public:
    explicit Page1Component (ThreadlineAudioProcessor& processor);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    ThreadlineAudioProcessor& processor;
    SectionUI compSection, klonSection, ts9Section;

    // TS9/TS808/TS10 variant switch — 3 buttons rather than a dropdown per
    // request. JUCE's radio-group mechanism keeps them visually mutually
    // exclusive; the timer keeps them in sync with the actual parameter
    // value (which can also change via preset load, not just a click here).
    juce::TextButton ts9VariantButtons[3] { juce::TextButton ("TS9"), juce::TextButton ("TS808"), juce::TextButton ("TS10") };
};
