#include "Page2Component.h"
#include <BinaryData.h>

namespace
{
    void setupAmpKnob (juce::Label& label, PhotoKnob& knob, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (12.0f));
        label.setColour (juce::Label::textColourId, juce::Colours::white);
        label.attachToComponent (&knob, false);
    }

    void setupCabSlot (SectionUI& section, juce::Component& parent, juce::AudioProcessorValueTreeState& apvts,
                        const juce::String& title, const char* onId, const char* mixId, int plateIndex)
    {
        buildSection (section, parent, apvts, title, onId, { { mixId, "Mix" } }, false, plateIndex);
    }
}

Page2Component::Page2Component (ThreadlineAudioProcessor& p) : processor (p)
{
    ampImage = juce::ImageCache::getFromMemory (BinaryData::tweed_amp_png, BinaryData::tweed_amp_pngSize);
    ampToggle.setRenderedImageStyle (false);
    ampToggle.setButtonText ("ON");
    ampToggle.setTitle ("Amp bypass");
    ampToggle.setHelpText ("Enable or bypass the amp stage");
    addAndMakeVisible (ampToggle);
    ampToggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "ampOn", ampToggle);

    setupAmpKnob (ampDriveLabel, ampDriveKnob, "Drive");
    setupAmpKnob (ampToneLabel, ampToneKnob, "Tone");
    setupAmpKnob (ampOutputLabel, ampOutputKnob, "Volume");
    addAndMakeVisible (ampDriveKnob);
    addAndMakeVisible (ampToneKnob);
    addAndMakeVisible (ampOutputKnob);
    addAndMakeVisible (ampDriveLabel);
    addAndMakeVisible (ampToneLabel);
    addAndMakeVisible (ampOutputLabel);

    ampDriveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "ampDrive", ampDriveKnob);
    ampToneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "ampTone", ampToneKnob);
    ampOutputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "ampOutput", ampOutputKnob);

    constexpr int ampVoiceRadioGroup = 9003;
    for (int i = 0; i < 2; ++i)
    {
        auto& button = ampVoiceButtons[i];
        button.setClickingTogglesState (true);
        button.setRadioGroupId (ampVoiceRadioGroup, juce::dontSendNotification);
        button.setColour (juce::TextButton::buttonColourId, ThreadlineColours::panelDark);
        button.setColour (juce::TextButton::buttonOnColourId, ThreadlineColours::accent);
        button.setColour (juce::TextButton::textColourOffId, ThreadlineColours::textDim);
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (button);
        button.onClick = [this, i]
        {
            if (auto* parameter = processor.apvts.getParameter ("ampVoice"))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) i));
        };
    }
    ampVoiceButtons[0].setToggleState (true, juce::dontSendNotification);

    setupAmpKnob (ampBassLabel, ampBassKnob, "Bass");
    setupAmpKnob (ampMidLabel, ampMidKnob, "Mid");
    setupAmpKnob (ampTrebleLabel, ampTrebleKnob, "Treble");
    addAndMakeVisible (ampBassKnob);
    addAndMakeVisible (ampMidKnob);
    addAndMakeVisible (ampTrebleKnob);
    addAndMakeVisible (ampBassLabel);
    addAndMakeVisible (ampMidLabel);
    addAndMakeVisible (ampTrebleLabel);
    ampBassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "ampBass", ampBassKnob);
    ampMidAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "ampMid", ampMidKnob);
    ampTrebleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "ampTreble", ampTrebleKnob);

    // Two IR slots, processed in parallel and blended — like two mics on
    // the same cab rather than two cabs chained one after another.
    setupCabSlot (cabASection, *this, processor.apvts, "Cab A", "cabAOn", "cabAMix", SectionPlate::Cab);
    setupCabSlot (cabBSection, *this, processor.apvts, "Cab B", "cabBOn", "cabBMix", SectionPlate::Cab);
    cabASection.toggle.setRenderedImageStyle (false);
    cabBSection.toggle.setRenderedImageStyle (false);
    cabASection.toggle.setButtonText ("ON");
    cabBSection.toggle.setButtonText ("ON");
    cabAPhaseButton.setButtonText ("POLARITY");
    cabBPhaseButton.setButtonText ("POLARITY");

    cabAIRBox.addItemList ({ "Spark Blend", "Velvet Blend", "Balanced Blend", "Edge 57", "Air 87", "Silk 160" }, 1);
    cabBIRBox.addItemList ({ "Spark Blend", "Velvet Blend", "Balanced Blend", "Edge 57", "Air 87", "Silk 160" }, 1);
    addAndMakeVisible (cabAIRBox);
    addAndMakeVisible (cabBIRBox);
    cabAIRAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "cabAIRSelect", cabAIRBox);
    cabBIRAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "cabBIRSelect", cabBIRBox);

    // Ø polarity-invert safety net — off by default, only needed if the two
    // blended IRs still sound thin/hollow after automatic onset alignment.
    for (auto* button : { &cabAPhaseButton, &cabBPhaseButton })
    {
        button->setClickingTogglesState (true);
        button->setTitle ("Invert phase");
        button->setTooltip ("Flip polarity if blending with the other slot sounds hollow");
        addAndMakeVisible (*button);
    }
    cabAPhaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "cabAPhase", cabAPhaseButton);
    cabBPhaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "cabBPhase", cabBPhaseButton);

    blendLabel.setText ("A / B", juce::dontSendNotification);
    blendLabel.setJustificationType (juce::Justification::centred);
    blendLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    blendLabel.setColour (juce::Label::textColourId, ThreadlineColours::textCream);
    blendLabel.attachToComponent (&blendKnob, false);
    addAndMakeVisible (blendKnob);
    addAndMakeVisible (blendLabel);
    blendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "cabBlend", blendKnob);

    updateAmpVoiceControls();
    startTimerHz (10);
}

void Page2Component::timerCallback()
{
    // Keeps the Voice buttons in sync with the actual parameter — it can
    // also change via preset load or automation, not just a click here.
    const auto current = (int) std::round (processor.apvts.getRawParameterValue ("ampVoice")->load());
    for (int i = 0; i < 2; ++i)
        if (ampVoiceButtons[i].getToggleState() != (i == current))
            ampVoiceButtons[i].setToggleState (i == current, juce::dontSendNotification);

    updateAmpVoiceControls();
}

void Page2Component::updateAmpVoiceControls()
{
    const auto voice = (int) std::round (processor.apvts.getRawParameterValue ("ampVoice")->load());
    if (voice == lastAmpVoice)
        return;

    lastAmpVoice = voice;
    const bool boutique = voice == 1;

    ampToneKnob.setVisible (! boutique);
    ampToneLabel.setVisible (! boutique);
    const std::array<juce::Component*, 6> boutiqueControls {
        &ampBassKnob, &ampMidKnob, &ampTrebleKnob, &ampBassLabel, &ampMidLabel, &ampTrebleLabel
    };
    for (auto* component : boutiqueControls)
        component->setVisible (boutique);

    resized();
    repaint (ampKnobFrameBounds);
}

void Page2Component::paint (juce::Graphics& g)
{
    if (ampImage.isValid() && ! ampImageFrameBounds.isEmpty())
    {
        auto placement = juce::RectanglePlacement (juce::RectanglePlacement::centred
                                                   | juce::RectanglePlacement::onlyReduceInSize);
        auto ampArea = ampImageFrameBounds.toFloat().reduced (18.0f);
        g.drawImage (ampImage, ampArea, placement);
    }

    paintCard (g, ampKnobFrameBounds);
    paintCard (g, cabASection.bounds);
    paintCard (g, cabBSection.bounds);
}

void Page2Component::resized()
{
    auto full = getLocalBounds().reduced (20, 16);

    // Cab A/B get a full-width row at the bottom rather than being confined
    // to the (narrower) knob column, so there's real room for two cards.
    const auto cabRowHeight = juce::roundToInt (full.getHeight() * 0.34f);
    auto cabRow = full.removeFromBottom (cabRowHeight);
    full.removeFromBottom (14);

    constexpr int columnGap = 14;
    const auto leftWidth = juce::roundToInt (static_cast<float> (full.getWidth() - columnGap) * 0.55f);
    ampImageFrameBounds = full.removeFromLeft (leftWidth);
    full.removeFromLeft (columnGap);
    ampKnobFrameBounds = full;

    auto knobArea = ampKnobFrameBounds.reduced (juce::jmax (6, ampKnobFrameBounds.getWidth() / 28), 8);
    auto ampToggleRow = knobArea.removeFromTop (22);
    ampToggle.setBounds (ampToggleRow.removeFromRight (50));
    knobArea.removeFromTop (juce::jmin (18, knobArea.getHeight() / 8));

    const bool boutique = (int) std::round (processor.apvts.getRawParameterValue ("ampVoice")->load()) == 1;

    // Drive and Volume never move when the voice changes; Boutique simply
    // leaves the Vintage Tone position empty.
    auto knobRow = knobArea.removeFromTop (juce::roundToInt (knobArea.getHeight() * 0.56f));
    auto knobWidth = knobRow.getWidth() / 3;
    const auto knobInset = juce::jmax (2, knobWidth / 14);
    ampDriveKnob.setBounds (knobRow.removeFromLeft (knobWidth).reduced (knobInset, 0));
    if (! boutique)
        ampToneKnob.setBounds (knobRow.removeFromLeft (knobWidth).reduced (knobInset, 0));
    else
        knobRow.removeFromLeft (knobWidth);
    ampOutputKnob.setBounds (knobRow.reduced (knobInset, 0));
    ampDriveKnob.toFront (false);
    ampToneKnob.toFront (false);
    ampOutputKnob.toFront (false);

    // Square voice selectors sit outside the amp-knob frame on its left.
    const auto voiceSize = juce::jlimit (34, 48, ampKnobFrameBounds.getHeight() / 5);
    const auto voiceX = ampKnobFrameBounds.getX() - voiceSize - 8;
    const auto voiceTop = ampKnobFrameBounds.getCentreY() - voiceSize - 3;
    ampVoiceButtons[0].setBounds (voiceX, voiceTop, voiceSize, voiceSize);
    ampVoiceButtons[1].setBounds (voiceX, voiceTop + voiceSize + 6, voiceSize, voiceSize);
    ampVoiceButtons[0].toFront (false);
    ampVoiceButtons[1].toFront (false);

    knobArea.removeFromTop (juce::jmin (32, knobArea.getHeight() / 4));
    knobArea.removeFromTop (juce::jmin (16, knobArea.getHeight() / 6));
    if (boutique)
    {
        auto knobRow2 = knobArea;
        auto knobWidth2 = knobRow2.getWidth() / 3;
        const auto knobInset2 = juce::jmax (2, knobWidth2 / 14);
        ampBassKnob.setBounds (knobRow2.removeFromLeft (knobWidth2).reduced (knobInset2, 0));
        ampMidKnob.setBounds (knobRow2.removeFromLeft (knobWidth2).reduced (knobInset2, 0));
        ampTrebleKnob.setBounds (knobRow2.reduced (knobInset2, 0));
        ampBassKnob.toFront (false);
        ampMidKnob.toFront (false);
        ampTrebleKnob.toFront (false);
    }

    // Cab A | blend knob | Cab B, left to right.
    constexpr int blendWidth = 116;
    const auto cabWidth = (cabRow.getWidth() - blendWidth) / 2;
    auto cabAArea = cabRow.removeFromLeft (cabWidth);
    auto blendArea = cabRow.removeFromLeft (blendWidth);
    auto cabBArea = cabRow;

    auto layoutCabSlot = [] (SectionUI& section, juce::ComboBox& irBox, juce::TextButton& phaseButton,
                             juce::Rectangle<int> area)
    {
        section.bounds = area;
        area.reduce (10, 8);
        // Tall enough for the Mix knob to sit at a genuinely normal size
        // below the title/toggle row, not squeezed into a short strip.
        auto header = area.removeFromTop (juce::jmin (120, area.getHeight()));

        auto topRow = header;
        section.toggle.setBounds (topRow.removeFromRight (48).withHeight (24).reduced (2, 0));
        phaseButton.setBounds (topRow.removeFromRight (78).withHeight (24).reduced (2, 0));
        topRow.removeFromRight (4);
        irBox.setBounds (topRow.removeFromRight (130).withHeight (24).reduced (4, 0));

        // Mix knob sits between the title wordmark and the cab-type
        // selector -- lower than the title/toggle row rather than spanning
        // the header's full height from the very top, and nudged left of
        // its reserved column.
        constexpr int mixKnobWidth = 88;
        constexpr int mixLeftNudge = 12;
        auto mixColumn = topRow.removeFromRight (mixKnobWidth);
        auto mixArea = mixColumn.withTrimmedTop (24).translated (-mixLeftNudge, 0);
        if (! section.knobs.empty())
            section.knobs[0]->slider.setBounds (mixArea.reduced (4, 0));

        section.titleLabel.setBounds (topRow.withHeight (24));
    };
    layoutCabSlot (cabASection, cabAIRBox, cabAPhaseButton, cabAArea);
    layoutCabSlot (cabBSection, cabBIRBox, cabBPhaseButton, cabBArea);

    blendKnob.setBounds (blendArea.withSizeKeepingCentre (
        juce::jmin (blendArea.getWidth(), 64), juce::jmin (blendArea.getHeight() - 30, 84))
        .withY (blendArea.getCentreY() - 10));
}
