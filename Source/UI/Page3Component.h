#pragma once

#include "SectionBuilder.h"
#include "../PluginProcessor.h"

// Page 3 — Tremolo, Chorus, Delay, Reverb.
class Page3Component : public juce::Component, private juce::Timer
{
public:
    explicit Page3Component (ThreadlineAudioProcessor& processor);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    ThreadlineAudioProcessor& processor;
    SectionUI tremSection, chorusSection, echoSection, reverbSection;

    // Julia's Sine/Triangle mini-toggle switch.
    juce::TextButton waveformButtons[2] { juce::TextButton ("Sine"), juce::TextButton ("Triangle") };
    juce::ComboBox echoPatternBox, echoDivisionBox;
    juce::ToggleButton echoSyncButton { "SYNC" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> echoPatternAttachment,
        echoDivisionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> echoSyncAttachment;

    juce::ComboBox reverbModelBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> reverbModelAttachment;
};
