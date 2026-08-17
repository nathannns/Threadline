#pragma once

#include "SectionBuilder.h"
#include "../PluginProcessor.h"

// Page 2 — Amp (big centered tweed photo, per the layout mockup) + Cab/IR.
class Page2Component : public juce::Component
{
public:
    explicit Page2Component (ThreadlineAudioProcessor& processor);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    ThreadlineAudioProcessor& processor;
    juce::Image ampImage;
    juce::Image ampSectionBackground;
    juce::Image ampControlsBackground;
    juce::Rectangle<int> ampImageFrameBounds;

    // Amp: no on/off toggle (always in the chain), 3 knobs drawn along the
    // bottom edge of the photo like the real panel's knob row.
    juce::Label ampDriveLabel, ampToneLabel, ampOutputLabel;
    // The amp's own knobs stay on the vintage chicken-head style — everything
    // else (Gate/Input/Output, Cab Mix, and the other pages) defaults to the
    // modern brushed-disc knob now.
    PhotoKnob ampDriveKnob { PhotoKnob::Style::Vintage };
    PhotoKnob ampToneKnob { PhotoKnob::Style::Vintage };
    PhotoKnob ampOutputKnob { PhotoKnob::Style::Vintage };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ampDriveAttachment, ampToneAttachment, ampOutputAttachment;
    juce::Rectangle<int> ampKnobFrameBounds; // same size as cabSection.bounds — see resized()

    SectionUI cabSection; // reuses the toggle + Mix knob
    juce::ComboBox cabIRBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cabIRAttachment;

    // cabIRBox is the single visible indication of the active IR.
};
