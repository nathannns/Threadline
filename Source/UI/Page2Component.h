#pragma once

#include "SectionBuilder.h"
#include "../PluginProcessor.h"

// Page 2 — Amp (full-width photo on top) + a single horizontal control bar
// below it (Voice switch, knobs, bypass) + Cab/IR (two parallel-blended
// slots) along the bottom, stacked top to bottom rather than photo-left/
// knobs-right.
class Page2Component : public juce::Component, private juce::Timer
{
public:
    explicit Page2Component (ThreadlineAudioProcessor& processor);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateAmpVoiceControls();

    ThreadlineAudioProcessor& processor;
    juce::Image ampImage;
    juce::Rectangle<int> ampImageFrameBounds;

    // Amp control bar: Voice switch, then Drive/Volume in two fixed slots
    // that never move between voices, then Tone (Vintage) or Bass/Mid/
    // Treble (Boutique) in the remaining slots, then the on/off toggle --
    // one horizontal row below the photo instead of a separate side card.
    ToggleFootswitch ampToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ampToggleAttachment;
    juce::Label ampDriveLabel, ampToneLabel, ampOutputLabel;
    // The amp's own knobs stay on the vintage chicken-head style — everything
    // else (Gate/Input/Output, Cab, and the other pages) defaults to the
    // modern brushed-disc knob now.
    PhotoKnob ampDriveKnob { PhotoKnob::Style::Vintage };
    PhotoKnob ampToneKnob { PhotoKnob::Style::Vintage };
    PhotoKnob ampOutputKnob { PhotoKnob::Style::Vintage };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ampDriveAttachment, ampToneAttachment, ampOutputAttachment;
    juce::Rectangle<int> ampKnobFrameBounds;

    // Voice: a narrow rocker switch (off = Vintage 5E3, on = Boutique),
    // matching a physical amp-panel toggle rather than a wide button pair --
    // ampVoiceLabel below it is updated with the current selection's name
    // in updateAmpVoiceControls(), since the switch itself only shows
    // on/off, not which option that means. Vintage uses Tone; Boutique
    // swaps that for Bass/Mid/Treble.
    RockerSwitch ampVoiceSwitch;
    juce::Label ampVoiceLabel;
    juce::Label ampBassLabel, ampMidLabel, ampTrebleLabel;
    PhotoKnob ampBassKnob, ampMidKnob, ampTrebleKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ampBassAttachment, ampMidAttachment, ampTrebleAttachment;
    int lastAmpVoice = -1;

    // Two IR slots processed in parallel and blended (like two mics on one
    // cab) — each independently on/off with its own IR choice and mix.
    SectionUI cabASection, cabBSection;
    juce::ComboBox cabAIRBox, cabBIRBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cabAIRAttachment, cabBIRAttachment;

    // Manual polarity-invert safety net for the two blended IR slots — onset
    // alignment (CabModule::alignOnset) fixes timing automatically, but
    // absolute polarity isn't detectable from the IR data alone.
    juce::TextButton cabAPhaseButton { "Ø" }, cabBPhaseButton { "Ø" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cabAPhaseAttachment, cabBPhaseAttachment;

    juce::Label blendLabel;
    PhotoKnob blendKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> blendAttachment;
};
