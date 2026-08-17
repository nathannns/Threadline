#pragma once

#include "SectionBuilder.h"
#include "../PluginProcessor.h"

// Page 2 — Amp (big centered tweed photo) + Cab/IR (two parallel-blended
// slots, side by side).
class Page2Component : public juce::Component, private juce::Timer
{
public:
    explicit Page2Component (ThreadlineAudioProcessor& processor);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    ThreadlineAudioProcessor& processor;
    juce::Image ampImage;
    juce::Rectangle<int> ampImageFrameBounds;

    // Amp: no on/off toggle (always in the chain), 3 knobs drawn along the
    // bottom edge of the photo like the real panel's knob row.
    juce::Label ampDriveLabel, ampToneLabel, ampOutputLabel;
    // The amp's own knobs stay on the vintage chicken-head style — everything
    // else (Gate/Input/Output, Cab, and the other pages) defaults to the
    // modern brushed-disc knob now.
    PhotoKnob ampDriveKnob { PhotoKnob::Style::Vintage };
    PhotoKnob ampToneKnob { PhotoKnob::Style::Vintage };
    PhotoKnob ampOutputKnob { PhotoKnob::Style::Vintage };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ampDriveAttachment, ampToneAttachment, ampOutputAttachment;
    juce::Rectangle<int> ampKnobFrameBounds;

    // Voice: Vintage 5E3 (single Tone knob above) vs. Modern 3-Band (an
    // independent Bass/Mid/Treble stack — see AmpModule::Voice). Bass/Mid/
    // Treble stay visible either way, same as the Breaker variant buttons on
    // Page1 stay visible regardless of on/off state — they just only affect
    // the sound when Modern voice is selected.
    juce::TextButton ampVoiceButtons[2] { juce::TextButton ("Vintage 5E3"), juce::TextButton ("Modern 3-Band") };
    juce::Label ampBassLabel, ampMidLabel, ampTrebleLabel;
    PhotoKnob ampBassKnob, ampMidKnob, ampTrebleKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ampBassAttachment, ampMidAttachment, ampTrebleAttachment;

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
