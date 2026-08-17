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

    // Delay: one shared card/on-off toggle, two selectable engines. Plexer's
    // exact EP-3 knob set (Time, Sustain, Volume, Echo/Sound-on-Sound mode)
    // is built via buildSection as before; Copier's Time/Regen/Mix knobs
    // are added by hand below, sharing the same card and knob positions --
    // only one set is visible at a time (see timerCallback).
    buildSection (echoSection, *this, processor.apvts, "Delay", "echoOn", {
        { "echoTime", "Time" }, { "echoSustain", "Sustain" }, { "echoVolume", "Volume" }
    }, false, SectionPlate::Delay);
    constexpr int echoModeRadioGroup = 9006;
    for (int i = 0; i < 2; ++i)
    {
        auto& button = echoModeButtons[i];
        button.setClickingTogglesState (true);
        button.setRadioGroupId (echoModeRadioGroup, juce::dontSendNotification);
        button.setColour (juce::TextButton::buttonColourId, ThreadlineColours::panelDark);
        button.setColour (juce::TextButton::buttonOnColourId, ThreadlineColours::accent);
        button.setColour (juce::TextButton::textColourOffId, ThreadlineColours::textDim);
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (button);
        button.onClick = [this, i]
        {
            if (auto* parameter = processor.apvts.getParameter ("echoMode"))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) i));
        };
    }
    echoModeButtons[0].setToggleState (true, juce::dontSendNotification);

    for (auto& [paramId, labelText] : std::initializer_list<std::pair<const char*, const char*>> {
             { "carbonTime", "Time" }, { "carbonRegen", "Regen" }, { "carbonMix", "Mix" } })
    {
        auto knob = std::make_unique<KnobUI>();
        knob->label.setText (labelText, juce::dontSendNotification);
        knob->label.setJustificationType (juce::Justification::centred);
        knob->label.setFont (juce::FontOptions (12.0f));
        knob->label.setColour (juce::Label::textColourId, ThreadlineColours::textDim);
        knob->label.attachToComponent (&knob->slider, false);
        addAndMakeVisible (knob->slider);
        addAndMakeVisible (knob->label);
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.apvts, paramId, knob->slider);
        carbonKnobs.push_back (std::move (knob));
    }

    constexpr int delayModelRadioGroup = 9007;
    for (int i = 0; i < 2; ++i)
    {
        auto& button = delayModelButtons[i];
        button.setClickingTogglesState (true);
        button.setRadioGroupId (delayModelRadioGroup, juce::dontSendNotification);
        button.setColour (juce::TextButton::buttonColourId, ThreadlineColours::panelDark);
        button.setColour (juce::TextButton::buttonOnColourId, ThreadlineColours::accent);
        button.setColour (juce::TextButton::textColourOffId, ThreadlineColours::textDim);
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (button);
        button.onClick = [this, i]
        {
            if (auto* parameter = processor.apvts.getParameter ("delayModel"))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) i));
        };
    }
    delayModelButtons[0].setToggleState (true, juce::dontSendNotification);

    carbonModButton.setClickingTogglesState (true);
    carbonModButton.setColour (juce::TextButton::buttonColourId, ThreadlineColours::panelDark);
    carbonModButton.setColour (juce::TextButton::buttonOnColourId, ThreadlineColours::accent);
    carbonModButton.setColour (juce::TextButton::textColourOffId, ThreadlineColours::textDim);
    carbonModButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    addAndMakeVisible (carbonModButton);
    carbonModButton.onClick = [this]
    {
        if (auto* parameter = processor.apvts.getParameter ("carbonMod"))
            parameter->setValueNotifyingHost (carbonModButton.getToggleState() ? 1.0f : 0.0f);
    };

    // Plexer is the default-selected engine, so Copier's controls start
    // hidden (timerCallback only toggles visibility on a delayModel change).
    for (auto& knob : carbonKnobs)
    {
        knob->slider.setVisible (false);
        knob->label.setVisible (false);
    }
    carbonModButton.setVisible (false);

    // Reverb: 3 Lexicon 480L hall/room convolutions (the old Rockalizer
    // spring-tank models are retired). Decay re-envelopes the loaded IR's
    // own tail rather than the live signal — see HallRoomReverbModule.
    buildSection (reverbSection, *this, processor.apvts, "Reverb", "reverbOn", {
        { "reverbPreDelay", "Pre-Delay" }, { "reverbDecay", "Decay" }, { "reverbTone", "Tone" },
        { "reverbMix", "Mix" }, { "reverbWidth", "Width" }
    }, false, SectionPlate::Reverb);
    reverbModelBox.addItemList ({ "Room", "Hall", "Plate" }, 1);
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

    const auto echoMode = juce::roundToInt (processor.apvts.getRawParameterValue ("echoMode")->load());
    for (int i = 0; i < 2; ++i)
        if (echoModeButtons[i].getToggleState() != (i == echoMode))
            echoModeButtons[i].setToggleState (i == echoMode, juce::dontSendNotification);

    const auto delayModel = juce::roundToInt (processor.apvts.getRawParameterValue ("delayModel")->load());
    for (int i = 0; i < 2; ++i)
        if (delayModelButtons[i].getToggleState() != (i == delayModel))
            delayModelButtons[i].setToggleState (i == delayModel, juce::dontSendNotification);

    const auto carbonModOn = processor.apvts.getRawParameterValue ("carbonMod")->load() > 0.5f;
    if (carbonModButton.getToggleState() != carbonModOn)
        carbonModButton.setToggleState (carbonModOn, juce::dontSendNotification);

    // Only the active engine's knobs/secondary toggle are shown -- the
    // other engine's parameters stay live (and automatable) underneath,
    // just hidden.
    const auto plexerActive = delayModel == 0;
    if (plexerActive != lastDelayModelWasPlexer)
    {
        for (auto& knob : echoSection.knobs)
        {
            knob->slider.setVisible (plexerActive);
            knob->label.setVisible (plexerActive);
        }
        for (auto& button : echoModeButtons)
            button.setVisible (plexerActive);
        for (auto& knob : carbonKnobs)
        {
            knob->slider.setVisible (! plexerActive);
            knob->label.setVisible (! plexerActive);
        }
        carbonModButton.setVisible (! plexerActive);
        lastDelayModelWasPlexer = plexerActive;
    }
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

    // Delay: same treatment, but two rows on the right instead of one --
    // the Plexer/Copier model switch on top, the active engine's secondary
    // toggle (Echo/Sound-on-Sound or Mod) below it, same as July's
    // Waveform/D-C-V stack.
    layoutHorizontalRackSection (echoSection, area.removeFromTop (cardHeight));
    {
        auto echoBounds = echoSection.bounds;
        const auto controlWidth = juce::jlimit (150, 200, echoBounds.getWidth() / 6);
        const auto controlsLeft = echoBounds.getRight() - 14 - controlWidth;
        constexpr int rowHeight = 22;
        const auto rowY1 = echoBounds.getY() + 11;
        const auto rowY2 = echoBounds.getBottom() - rowHeight - 9;

        const auto modelWidth = (controlWidth - 4) / 2;
        delayModelButtons[0].setBounds (controlsLeft, rowY1, modelWidth, rowHeight);
        delayModelButtons[1].setBounds (controlsLeft + modelWidth + 4, rowY1, controlWidth - modelWidth - 4, rowHeight);

        const auto modeWidth = (controlWidth - 4) / 2;
        echoModeButtons[0].setBounds (controlsLeft, rowY2, modeWidth, rowHeight);
        echoModeButtons[1].setBounds (controlsLeft + modeWidth + 4, rowY2, controlWidth - modeWidth - 4, rowHeight);
        carbonModButton.setBounds (controlsLeft, rowY2, controlWidth, rowHeight);

        if (echoSection.knobs.size() == 3)
        {
            const auto knobsLeft = echoBounds.getX() + juce::jlimit (112, 210, echoBounds.getWidth() / 5) + 18;
            const auto knobsRight = controlsLeft - 18;
            const auto knobsWidth = knobsRight - knobsLeft;
            const float positions[] { 0.14f, 0.5f, 0.86f };
            for (size_t i = 0; i < echoSection.knobs.size(); ++i)
            {
                auto& slider = echoSection.knobs[i]->slider;
                const auto width = slider.getWidth();
                slider.setTopLeftPosition (knobsLeft + juce::roundToInt (positions[i] * (float) knobsWidth) - width / 2,
                                           slider.getY());
            }
            // Copier's knobs mirror Plexer's exact geometry -- only one set
            // is ever visible, so they can share the same slots outright.
            if (carbonKnobs.size() == echoSection.knobs.size())
                for (size_t i = 0; i < carbonKnobs.size(); ++i)
                    carbonKnobs[i]->slider.setBounds (echoSection.knobs[i]->slider.getBounds());
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
