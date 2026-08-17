#include "Page3Component.h"

Page3Component::Page3Component (ThreadlineAudioProcessor& p) : processor (p)
{
    buildSection (tremSection, *this, processor.apvts, "Tremolo", "tremOn", {
        { "tremAmount", "Amount" }
    }, false, SectionPlate::Tremolo);

    buildSection (chorusSection, *this, processor.apvts, "Chorus", "chorusOn", {
        { "chorusRate", "Rate" }, { "chorusDepth", "Depth" }, { "chorusWidth", "Width" },
        { "chorusTone", "Tone" }, { "chorusMix", "Mix" }
    }, false, SectionPlate::Chorus);
    flangerMode1Button.setButtonText ("I");
    flangerMode2Button.setButtonText ("II");
    flangerMode1Button.setTooltip ("Flanger I: independent warm sweep; enable I + II together for Mode III");
    flangerMode2Button.setTooltip ("Flanger II: independent faster sweep; enable I + II together for Mode III");
    const auto toggleFlangerBit = [this] (int bit)
    {
        const auto current = juce::roundToInt (processor.apvts.getRawParameterValue ("chorusFlangerMode")->load());
        if (auto* parameter = processor.apvts.getParameter ("chorusFlangerMode"))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) (current ^ bit)));
            parameter->endChangeGesture();
        }
    };
    flangerMode1Button.onClick = [toggleFlangerBit] { toggleFlangerBit (1); };
    flangerMode2Button.onClick = [toggleFlangerBit] { toggleFlangerBit (2); };
    addAndMakeVisible (flangerMode1Button);
    addAndMakeVisible (flangerMode2Button);

    buildSection (echoSection, *this, processor.apvts, "Delay", "echoOn", {
        { "echoTime", "Time" }, { "echoRepeats", "Repeats" }, { "echoTone", "Tone" },
        { "echoWobble", "Wobble" }, { "echoDrive", "Drive" }, { "echoMix", "Mix" }
    }, false, SectionPlate::Delay);
    echoPatternBox.addItemList ({ "STRAIGHT", "BOUNCE", "GALLOP", "CLUSTER", "WASH" }, 1);
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

    // Reverb: 7 Lexicon 480L hall/room convolutions (the old Rockalizer
    // spring-tank models are retired). Decay re-envelopes the loaded IR's
    // own tail rather than the live signal — see HallRoomReverbModule.
    buildSection (reverbSection, *this, processor.apvts, "Reverb", "reverbOn", {
        { "reverbPreDelay", "Pre-Delay" }, { "reverbDecay", "Decay" }, { "reverbTone", "Tone" },
        { "reverbMix", "Mix" }, { "reverbWidth", "Width" }
    }, false, SectionPlate::Reverb);
    reverbModelBox.addItemList ({ "Large Hall", "Large Stage", "Small Church", "Small Hall",
                                   "Small Stage", "Large Room", "Small Room" }, 1);
    addAndMakeVisible (reverbModelBox);
    reverbModelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "reverbModel", reverbModelBox);
    startTimerHz (15);
}

void Page3Component::timerCallback()
{
    const auto mode = juce::roundToInt (processor.apvts.getRawParameterValue ("chorusFlangerMode")->load());
    flangerMode1Button.setToggleState ((mode & 1) != 0, juce::dontSendNotification);
    flangerMode2Button.setToggleState ((mode & 2) != 0, juce::dontSendNotification);
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
    flangerMode1Button.setBounds (chorusBounds.getX() + 112, chorusBounds.getBottom() - 30, 42, 22);
    flangerMode2Button.setBounds (chorusBounds.getX() + 160, chorusBounds.getBottom() - 30, 42, 22);
    area.removeFromTop (gap);
    layoutHorizontalRackSection (echoSection, area.removeFromTop (cardHeight), 310);
    auto echoBounds = echoSection.bounds;
    echoSection.toggle.setBounds (echoBounds.getX() + 78, echoBounds.getY() + 9, 96, 32);
    const auto controlX = echoBounds.getX() + 184;
    const auto topRowY = echoBounds.getY() + 13;
    const auto bottomRowY = echoBounds.getBottom() - 35;
    echoPatternBox.setBounds (controlX, topRowY, 112, 24);
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
