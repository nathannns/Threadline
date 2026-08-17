#pragma once

#include "SectionBuilder.h"
#include "../PluginProcessor.h"

// Page 3 — Tremolo, July (Chorus), Delay (Plexer or Copier), Reverb.
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

    // July's Sine/Triangle mini-toggle switch.
    juce::TextButton waveformButtons[2] { juce::TextButton ("Sine"), juce::TextButton ("Triangle") };
    // D-C-V as 3 explicit stops rather than a knob whose Dry/Chorus/Vibrato
    // range wasn't obvious.
    juce::TextButton dcvButtons[3] { juce::TextButton ("Dry"), juce::TextButton ("Chorus"), juce::TextButton ("Vibrato") };

    // Delay shares one card/on-off toggle between two engines. delayModelButtons
    // picks Plexer or Copier; only the active engine's knobs/secondary toggle
    // are shown (echoModeButtons + echoSection.knobs for Plexer, carbonKnobs +
    // carbonModButton for Copier).
    juce::TextButton delayModelButtons[2] { juce::TextButton ("Plexer"), juce::TextButton ("Copier") };
    // Plexer's Echo / Sound-on-Sound mode switch — the real EP-3's toggle.
    juce::TextButton echoModeButtons[2] { juce::TextButton ("Echo"), juce::TextButton ("Sound-on-Sound") };
    // Copier's Mod switch — the real Carbon Copy's toggle.
    juce::TextButton carbonModButton { "Mod" };
    // Copier's Time/Regen/Mix knobs, built by hand (not via buildSection)
    // since they share echoSection's card and knob positions rather than
    // getting their own section — only one of the two knob sets is visible
    // at a time, toggled in timerCallback() to follow delayModel.
    std::vector<std::unique_ptr<KnobUI>> carbonKnobs;
    bool lastDelayModelWasPlexer = true;

    juce::ComboBox reverbModelBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> reverbModelAttachment;
};
