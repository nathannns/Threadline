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

    cabAIRBox.addItemList ({ "Bright Mix", "Dark Mix", "Medium Mix", "Medium 57", "Medium 87", "Medium 160" }, 1);
    cabBIRBox.addItemList ({ "Bright Mix", "Dark Mix", "Medium Mix", "Medium 57", "Medium 87", "Medium 160" }, 1);
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
    knobArea.removeFromTop (juce::jmin (18, knobArea.getHeight() / 8));

    const bool boutique = (int) std::round (processor.apvts.getRawParameterValue ("ampVoice")->load()) == 1;

    // Vintage: Drive / Tone / Volume. Boutique: Drive / Volume.
    auto knobRow = knobArea.removeFromTop (juce::roundToInt (knobArea.getHeight() * 0.56f));
    auto knobWidth = knobRow.getWidth() / (boutique ? 2 : 3);
    const auto knobInset = juce::jmax (2, knobWidth / 14);
    ampDriveKnob.setBounds (knobRow.removeFromLeft (knobWidth).reduced (knobInset, 0));
    if (! boutique)
        ampToneKnob.setBounds (knobRow.removeFromLeft (knobWidth).reduced (knobInset, 0));
    ampOutputKnob.setBounds (knobRow.reduced (knobInset, 0));
    ampDriveKnob.toFront (false);
    ampToneKnob.toFront (false);
    ampOutputKnob.toFront (false);

    // Voice selector, then Boutique's three-band tone stack when selected.
    knobArea.removeFromTop (juce::jmin (10, knobArea.getHeight() / 6));
    auto voiceRow = knobArea.removeFromTop (juce::jmin (22, knobArea.getHeight() / 3));
    const auto voiceButtonWidth = voiceRow.getWidth() / 2;
    ampVoiceButtons[0].setBounds (voiceRow.removeFromLeft (voiceButtonWidth).reduced (3, 1));
    ampVoiceButtons[1].setBounds (voiceRow.reduced (3, 1));

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
    constexpr int blendWidth = 84;
    const auto cabWidth = (cabRow.getWidth() - blendWidth) / 2;
    auto cabAArea = cabRow.removeFromLeft (cabWidth);
    auto blendArea = cabRow.removeFromLeft (blendWidth);
    auto cabBArea = cabRow;

    auto layoutCabSlot = [] (SectionUI& section, juce::ComboBox& irBox, juce::TextButton& phaseButton,
                             juce::Rectangle<int> area)
    {
        section.bounds = area;
        area.reduce (10, 8);
        auto header = area.removeFromTop (24);
        section.toggle.setBounds (header.removeFromRight (48).reduced (2, 0));
        phaseButton.setBounds (header.removeFromRight (78).reduced (2, 0));
        header.removeFromRight (4);
        irBox.setBounds (header.removeFromRight (130).reduced (4, 0));
        section.titleLabel.setBounds (header);

        area.removeFromTop (juce::jmin (16, area.getHeight() / 10));
        auto mixKnobArea = area.removeFromLeft (juce::jmin (90, area.getWidth()));
        if (! section.knobs.empty())
            section.knobs[0]->slider.setBounds (mixKnobArea.reduced (4, 0));
    };
    layoutCabSlot (cabASection, cabAIRBox, cabAPhaseButton, cabAArea);
    layoutCabSlot (cabBSection, cabBIRBox, cabBPhaseButton, cabBArea);

    blendKnob.setBounds (blendArea.withSizeKeepingCentre (
        juce::jmin (blendArea.getWidth(), 64), juce::jmin (blendArea.getHeight() - 30, 84))
        .withY (blendArea.getCentreY() - 10));
}
