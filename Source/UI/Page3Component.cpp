#include "Page3Component.h"

Page3Component::Page3Component (ThreadlineAudioProcessor& p) : processor (p)
{
    buildSection (tremSection, *this, processor.apvts, "Tremolo", "tremOn", {
        { "tremAmount", "Amount" }
    }, false, SectionPlate::Tremolo);

    // July's exact control surface: Rate, Depth, Lag (LFO center delay
    // time), a Sine/Triangle waveform switch, and D-C-V (Dry-Chorus-Vibrato).
    buildSection (chorusSection, *this, processor.apvts, "July", "chorusOn", {
        { "chorusRate", "Rate" }, { "chorusDepth", "Depth" }, { "chorusLag", "Lag" }
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

    // D-C-V as 3 explicit stops rather than a knob whose Dry/Chorus/Vibrato
    // range wasn't obvious.
    constexpr int dcvRadioGroup = 9005;
    for (int i = 0; i < 3; ++i)
    {
        auto& button = dcvButtons[i];
        button.setClickingTogglesState (true);
        button.setRadioGroupId (dcvRadioGroup, juce::dontSendNotification);
        button.setColour (juce::TextButton::buttonColourId, ThreadlineColours::panelDark);
        button.setColour (juce::TextButton::buttonOnColourId, ThreadlineColours::accent);
        button.setColour (juce::TextButton::textColourOffId, ThreadlineColours::textDim);
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (button);
        button.onClick = [this, i]
        {
            if (auto* parameter = processor.apvts.getParameter ("chorusDCV"))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) i));
        };
    }
    dcvButtons[1].setToggleState (true, juce::dontSendNotification);

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

    const auto dcv = juce::roundToInt (processor.apvts.getRawParameterValue ("chorusDCV")->load());
    for (int i = 0; i < 3; ++i)
        if (dcvButtons[i].getToggleState() != (i == dcv))
            dcvButtons[i].setToggleState (i == dcv, juce::dontSendNotification);
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

    // July: same card size/identity-width as Tremolo and Reverb — its extra
    // controls (Waveform, D-C-V) live to the right of the knob row instead
    // of widening the header, the same technique Reverb's model box uses.
    layoutHorizontalRackSection (chorusSection, area.removeFromTop (cardHeight));
    {
        auto chorusBounds = chorusSection.bounds;
        const auto controlWidth = juce::jlimit (150, 200, chorusBounds.getWidth() / 6);
        const auto controlsLeft = chorusBounds.getRight() - 14 - controlWidth;
        constexpr int rowHeight = 22;
        const auto rowY1 = chorusBounds.getY() + 11;
        const auto rowY2 = chorusBounds.getBottom() - rowHeight - 9;

        const auto waveWidth = (controlWidth - 4) / 2;
        waveformButtons[0].setBounds (controlsLeft, rowY1, waveWidth, rowHeight);
        waveformButtons[1].setBounds (controlsLeft + waveWidth + 4, rowY1, controlWidth - waveWidth - 4, rowHeight);

        const auto dcvWidth = (controlWidth - 8) / 3;
        dcvButtons[0].setBounds (controlsLeft, rowY2, dcvWidth, rowHeight);
        dcvButtons[1].setBounds (controlsLeft + dcvWidth + 4, rowY2, dcvWidth, rowHeight);
        dcvButtons[2].setBounds (controlsLeft + (dcvWidth + 4) * 2, rowY2, controlWidth - (dcvWidth + 4) * 2, rowHeight);

        if (chorusSection.knobs.size() == 3)
        {
            const auto knobsLeft = chorusBounds.getX() + juce::jlimit (112, 210, chorusBounds.getWidth() / 5) + 18;
            const auto knobsRight = controlsLeft - 18;
            const auto knobsWidth = knobsRight - knobsLeft;
            const float positions[] { 0.14f, 0.5f, 0.86f };
            for (size_t i = 0; i < chorusSection.knobs.size(); ++i)
            {
                auto& slider = chorusSection.knobs[i]->slider;
                const auto width = slider.getWidth();
                slider.setTopLeftPosition (knobsLeft + juce::roundToInt (positions[i] * (float) knobsWidth) - width / 2,
                                           slider.getY());
            }
        }
    }
    area.removeFromTop (gap);

    // Delay: same treatment — pattern/division/sync move to the right of the
    // knob row instead of widening the header.
    layoutHorizontalRackSection (echoSection, area.removeFromTop (cardHeight));
    {
        auto echoBounds = echoSection.bounds;
        const auto controlWidth = juce::jlimit (150, 200, echoBounds.getWidth() / 6);
        const auto controlsLeft = echoBounds.getRight() - 14 - controlWidth;
        constexpr int rowHeight = 24;
        const auto rowY1 = echoBounds.getY() + 11;
        const auto rowY2 = echoBounds.getBottom() - rowHeight - 9;

        echoPatternBox.setBounds (controlsLeft, rowY1, controlWidth, rowHeight);
        constexpr int syncWidth = 46;
        const auto divWidth = controlWidth - syncWidth - 4;
        echoDivisionBox.setBounds (controlsLeft, rowY2, divWidth, rowHeight);
        echoSyncButton.setBounds (controlsLeft + divWidth + 4, rowY2, syncWidth, rowHeight);

        if (echoSection.knobs.size() == 6)
        {
            const auto knobsLeft = echoBounds.getX() + juce::jlimit (112, 210, echoBounds.getWidth() / 5) + 18;
            const auto knobsRight = controlsLeft - 18;
            const auto knobsWidth = knobsRight - knobsLeft;
            const float positions[] { 0.07f, 0.238f, 0.406f, 0.574f, 0.742f, 0.91f };
            for (size_t i = 0; i < echoSection.knobs.size(); ++i)
            {
                auto& slider = echoSection.knobs[i]->slider;
                const auto width = slider.getWidth();
                slider.setTopLeftPosition (knobsLeft + juce::roundToInt (positions[i] * (float) knobsWidth) - width / 2,
                                           slider.getY());
            }
        }
    }
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
