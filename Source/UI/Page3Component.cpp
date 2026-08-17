#include "Page3Component.h"

Page3Component::Page3Component (ThreadlineAudioProcessor& p) : processor (p)
{
    buildSection (tremSection, *this, processor.apvts, "Tremolo", "tremOn", {
        { "tremAmount", "Amount" }
    }, false, SectionPlate::Tremolo);

    // Julia's exact control surface: Rate, Depth, Lag (LFO center delay
    // time), a Sine/Triangle waveform switch, and D-C-V (Dry-Chorus-Vibrato).
    buildSection (chorusSection, *this, processor.apvts, "Julia", "chorusOn", {
        { "chorusRate", "Rate" }, { "chorusDepth", "Depth" }, { "chorusLag", "Lag" },
        { "chorusDCV", "D-C-V" }
    }, false, SectionPlate::Chorus);
    constexpr int waveformRadioGroup = 9004;
    for (int i = 0; i < 2; ++i)
    {
        auto& button = waveformButtons[i];
        button.setClickingTogglesState (true);
        button.setRadioGroupId (waveformRadioGroup, juce::dontSendNotification);
        button.setColour (juce::TextButton::buttonColourId, ThreadlineColours::panelDark);
        button.setColour (juce::TextButton::buttonOnColourId, ThreadlineColours::accent);
        button.setColour (juce::TextButton::textColourOffId, ThreadlineColours::textDim);
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (button);
        button.onClick = [this, i]
        {
            if (auto* parameter = processor.apvts.getParameter ("chorusWaveform"))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) i));
        };
    }
    waveformButtons[0].setToggleState (true, juce::dontSendNotification);

    buildSection (echoSection, *this, processor.apvts, "Delay", "echoOn", {
        { "echoTime", "Time" }, { "echoRepeats", "Repeats" }, { "echoTone", "Tone" },
        { "echoWobble", "Wobble" }, { "echoDrive", "Drive" }, { "echoMix", "Mix" }
    }, false, SectionPlate::Delay);
    echoPatternBox.addItemList ({ "STRAIGHT", "BOUNCE", "GALLOP", "CLUSTER", "WASH", "PING-PONG" }, 1);
    echoDivisionBox.addItemList ({ "1/4", "1/4 D", "1/8", "1/8 D", "1/8 T", "1/16", "1/16 D", "1/16 T" }, 1);
    addAndMakeVisible (echoPatternBox);
    addAndMakeVisible (echoDivisionBox);
    addAndMakeVisible (echoSyncButton);
    echoPatternAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "echoPattern", echoPatternBox);
    echoDivisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "echoDivision", echoDivisionBox);
    echoSyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "echoSync", echoSyncButton);

    // Reverb: 3 Lexicon 480L hall/room convolutions (the old Rockalizer
    // spring-tank models are retired). Decay re-envelopes the loaded IR's
    // own tail rather than the live signal — see HallRoomReverbModule.
    buildSection (reverbSection, *this, processor.apvts, "Reverb", "reverbOn", {
        { "reverbPreDelay", "Pre-Delay" }, { "reverbDecay", "Decay" }, { "reverbTone", "Tone" },
        { "reverbMix", "Mix" }, { "reverbWidth", "Width" }
    }, false, SectionPlate::Reverb);
    reverbModelBox.addItemList ({ "Large Hall", "Large Stage", "Small Room" }, 1);
    addAndMakeVisible (reverbModelBox);
    reverbModelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "reverbModel", reverbModelBox);
    startTimerHz (15);
}

void Page3Component::timerCallback()
{
    const auto waveform = juce::roundToInt (processor.apvts.getRawParameterValue ("chorusWaveform")->load());
    for (int i = 0; i < 2; ++i)
        if (waveformButtons[i].getToggleState() != (i == waveform))
            waveformButtons[i].setToggleState (i == waveform, juce::dontSendNotification);
}

void Page3Component::paint (juce::Graphics& g)
{
    paintSectionPlate (g, tremSection);
    paintSectionPlate (g, chorusSection);
    paintSectionPlate (g, echoSection);
    paintSectionPlate (g, reverbSection);
}

void Page3Component::resized()
{
    auto area = getLocalBounds().reduced (24, 10);
    constexpr int gap = 6;
    const auto cardHeight = (area.getHeight() - 3 * gap) / 4;
    layoutHorizontalRackSection (tremSection, area.removeFromTop (cardHeight));
    area.removeFromTop (gap);
    layoutHorizontalRackSection (chorusSection, area.removeFromTop (cardHeight), 224);
    auto chorusBounds = chorusSection.bounds;
    chorusSection.toggle.setBounds (chorusBounds.getX() + 78, chorusBounds.getY() + 9, 128, 32);
    // Julia's Sine/Triangle mini-toggle switch.
    waveformButtons[0].setBounds (chorusBounds.getX() + 76, chorusBounds.getBottom() - 30, 66, 22);
    waveformButtons[1].setBounds (chorusBounds.getX() + 146, chorusBounds.getBottom() - 30, 74, 22);
    area.removeFromTop (gap);
    layoutHorizontalRackSection (echoSection, area.removeFromTop (cardHeight), 310);
    auto echoBounds = echoSection.bounds;
    echoSection.toggle.setBounds (echoBounds.getX() + 78, echoBounds.getY() + 9, 96, 32);
    const auto controlX = echoBounds.getX() + 184;
    const auto topRowY = echoBounds.getY() + 13;
    const auto bottomRowY = echoBounds.getBottom() - 35;
    echoPatternBox.setBounds (controlX, topRowY, 138, 24);
    echoDivisionBox.setBounds (controlX, bottomRowY, 64, 24);
    echoSyncButton.setBounds (controlX + 70, bottomRowY, 42, 24);
    area.removeFromTop (gap);

    auto reverbArea = area;
    layoutHorizontalRackSection (reverbSection, reverbArea);
    // Keep the wet controls in the right-hand half and leave a clear gap
    // before the model selector.
    const auto typeWidth = juce::jlimit (130, 176, reverbArea.getWidth() / 7);
    reverbModelBox.setBounds (reverbArea.getRight() - typeWidth - 14,
                              reverbArea.getCentreY() - 14, typeWidth, 28);
    if (reverbSection.knobs.size() == 5)
    {
        const auto controlsLeft = reverbArea.getX() + juce::jlimit (112, 210, reverbArea.getWidth() / 5) + 18;
        const auto controlsRight = reverbModelBox.getX() - 18;
        const auto controlsWidth = controlsRight - controlsLeft;
        const float positions[] { 0.08f, 0.28f, 0.48f, 0.68f, 0.88f };
        for (size_t i = 0; i < reverbSection.knobs.size(); ++i)
        {
            auto& slider = reverbSection.knobs[i]->slider;
            const auto width = slider.getWidth();
            slider.setTopLeftPosition (controlsLeft + juce::roundToInt (positions[i] * (float) controlsWidth) - width / 2,
                                       slider.getY());
        }
    }
}
