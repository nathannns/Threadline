#pragma once

#include "SectionBuilder.h"
#include "../PluginProcessor.h"

// Page 1 — Compressor, Klon, Breaker.
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

    // Breaker circuit variant switch — a rotary knob with 3 fixed (detented)
    // positions rather than a dropdown or button row. JUCE's Slider already
    // snaps both the reported value AND the drawn rotation angle to the
    // parameter's integer steps (0/1/2), so a plain PhotoKnob bound to the
    // choice parameter behaves exactly like a real 3-position rotary
    // selector switch with no custom drag/snapping code needed — same
    // "trust JUCE's default behaviour" approach used for every other knob.
    PhotoKnob ts9VariantKnob;
    juce::Label ts9VariantLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ts9VariantAttachment;

    // Klon/Breaker order ahead of the Amp — same radio-group + timer-sync
    // pattern as the variant switch above.
    juce::TextButton odOrderButtons[2] { juce::TextButton ("Klon -> Breaker"), juce::TextButton ("Breaker -> Klon") };
};
