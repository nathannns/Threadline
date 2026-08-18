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
    // Live cutoff-frequency readouts -- HPF/LPF are continuously adjustable
    // (unlike the 9 graphic-EQ bands' freqLabel, which just names each
    // band's fixed centre frequency), so these need to track the current
    // value rather than show static text. Always visible, not gated behind
    // the eye icon's drag-only popup every other knob uses -- the actual
    // cutoff matters here even when you're not actively turning the knob.
    juce::Label hpfValueLabel, lpfValueLabel;
    ToggleFootswitch hpfToggle, lpfToggle;
    PhotoKnob hpfKnob { PhotoKnob::Style::EQ }, lpfKnob { PhotoKnob::Style::EQ };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hpfToggleAttachment, lpfToggleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hpfKnobAttachment, lpfKnobAttachment;

    juce::Rectangle<int> cardBounds;
};
