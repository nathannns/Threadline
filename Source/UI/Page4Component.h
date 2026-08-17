#pragma once

#include "SectionBuilder.h"
#include "../PluginProcessor.h"
#include "../DSP/GraphicEQModule.h"

// Page 4 — 9-band graphic EQ + HPF/LPF, after the wet effects.
class Page4Component : public juce::Component
{
public:
    explicit Page4Component (ThreadlineAudioProcessor& processor);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct Band
    {
        EQBand slider;
        juce::Label freqLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    std::array<std::unique_ptr<Band>, GraphicEQModule::numBands> bands;

    juce::Label titleLabel;
    ToggleFootswitch eqToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eqToggleAttachment;

    juce::Label hpfLabel, lpfLabel;
    ToggleFootswitch hpfToggle, lpfToggle;
    PhotoKnob hpfKnob { PhotoKnob::Style::EQ }, lpfKnob { PhotoKnob::Style::EQ };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hpfToggleAttachment, lpfToggleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hpfKnobAttachment, lpfKnobAttachment;

    juce::Rectangle<int> cardBounds;
};
