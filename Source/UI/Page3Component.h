#pragma once

#include "SectionBuilder.h"
#include "../PluginProcessor.h"

// Page 3 — Tremolo, Chorus, Delay, Reverb.
class Page3Component : public juce::Component
{
public:
    explicit Page3Component (ThreadlineAudioProcessor& processor);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    SectionUI tremSection, chorusSection, echoSection, reverbSection;

    juce::ComboBox chorusModeBox, echoPatternBox, echoDivisionBox;
    juce::ToggleButton echoSyncButton { "SYNC" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chorusModeAttachment,
        echoPatternAttachment, echoDivisionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> echoSyncAttachment;

    juce::ComboBox reverbModelBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> reverbModelAttachment;
};
