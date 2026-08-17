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
        for (auto& knob : section.knobs)
            knob->slider.setStyle (PhotoKnob::Style::Gold);
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

    // Voice rocker switch: off = Vintage 5E3, on = Boutique. The switch
    // itself only shows on/off; ampVoiceLabel (updated in
    // updateAmpVoiceControls) reads out which one that currently means.
    ampVoiceSwitch.setTitle ("Amp voice");
    ampVoiceSwitch.setHelpText ("Vintage 5E3 or Boutique amp voice");
    addAndMakeVisible (ampVoiceSwitch);
    ampVoiceSwitch.onClick = [this]
    {
        if (auto* parameter = processor.apvts.getParameter ("ampVoice"))
            parameter->setValueNotifyingHost (ampVoiceSwitch.getToggleState() ? 1.0f : 0.0f);
    };
    ampVoiceLabel.setJustificationType (juce::Justification::centred);
    ampVoiceLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    ampVoiceLabel.setColour (juce::Label::textColourId, ThreadlineColours::textCream);
    addAndMakeVisible (ampVoiceLabel);

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
    blendKnob.setStyle (PhotoKnob::Style::Gold);
    addAndMakeVisible (blendKnob);
    addAndMakeVisible (blendLabel);
    blendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "cabBlend", blendKnob);

    updateAmpVoiceControls();
    startTimerHz (10);
}

void Page2Component::timerCallback()
{
    // Keeps the Voice switch in sync with the actual parameter — it can
    // also change via preset load or automation, not just a click here.
    const auto current = (int) std::round (processor.apvts.getRawParameterValue ("ampVoice")->load());
    if (ampVoiceSwitch.getToggleState() != (current == 1))
        ampVoiceSwitch.setToggleState (current == 1, juce::dontSendNotification);

    updateAmpVoiceControls();
}

void Page2Component::updateAmpVoiceControls()
{
    const auto voice = (int) std::round (processor.apvts.getRawParameterValue ("ampVoice")->load());
    if (voice == lastAmpVoice)
        return;

    lastAmpVoice = voice;
    const bool boutique = voice == 1;

    ampVoiceLabel.setText (boutique ? "BOUTIQUE" : "VINTAGE 5E3", juce::dontSendNotification);

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
        // `onlyReduceInSize` capped the photo at its native pixel size once
        // the frame grew past that, so "give the photo more room" stopped
        // having any visible effect -- draw it explicitly bigger than the
        // size that would otherwise fit the frame instead. Clip region
        // extends a bit past the frame's own bottom edge into the (now
        // deliberately larger) gap reserved before the knob bar in
        // resized(), so the boosted photo has real room to spill into
        // without touching the bar itself.
        const auto frame = ampImageFrameBounds.toFloat().reduced (18.0f);
        const auto imageWidth = static_cast<float> (ampImage.getWidth());
        const auto imageHeight = static_cast<float> (ampImage.getHeight());
        const auto fitScale = juce::jmin (frame.getWidth() / imageWidth, frame.getHeight() / imageHeight);
        constexpr float sizeBoost = 1.7f;
        const auto drawWidth = imageWidth * fitScale * sizeBoost;
        const auto drawHeight = imageHeight * fitScale * sizeBoost;
        auto imageBounds = juce::Rectangle<float> (drawWidth, drawHeight).withCentre (frame.getCentre());

        auto clipRegion = ampImageFrameBounds;
        clipRegion.setBottom (ampImageFrameBounds.getBottom() + 22);

        g.saveState();
        g.reduceClipRegion (clipRegion);
        g.drawImage (ampImage, imageBounds, juce::RectanglePlacement::stretchToFit);
        g.restoreState();
    }

    paintCard (g, ampKnobFrameBounds);
    paintCard (g, cabASection.bounds);
    paintCard (g, cabBSection.bounds);
}

void Page2Component::resized()
{
    auto full = getLocalBounds().reduced (20, 16);

    // Cab A/B and the knob bar are kept compact and pinned to the bottom,
    // so the amp photo above gets as much of the remaining height as
    // possible rather than the three areas splitting it evenly.
    const auto cabRowHeight = juce::jlimit (80, 140, juce::roundToInt (full.getHeight() * 0.24f));
    auto cabRow = full.removeFromBottom (cabRowHeight);
    full.removeFromBottom (10);

    const auto knobBarHeight = juce::jlimit (110, 150, juce::roundToInt (full.getHeight() * 0.26f));
    auto knobBar = full.removeFromBottom (knobBarHeight);
    // A real gap between the photo and the control bar below it, rather
    // than them almost touching (was 8px).
    full.removeFromBottom (28);
    ampImageFrameBounds = full;
    ampKnobFrameBounds = knobBar;

    auto bar = ampKnobFrameBounds.reduced (14, 10);

    // Voice rocker switch on the left, with its own caption below it. The
    // switch's height is capped at 40 (was 54) *and* at whatever's actually
    // left in voiceArea after the label -- it was previously clamped only
    // against the fixed ceiling, which could exceed the real available
    // height on a shorter bar and render cropped at the top.
    auto voiceArea = bar.removeFromLeft (juce::jmin (70, bar.getWidth() / 8));
    bar.removeFromLeft (14);
    auto voiceLabelArea = voiceArea.removeFromBottom (juce::jmin (16, voiceArea.getHeight() / 4));
    const auto voiceSwitchHeight = juce::jmin (40, voiceArea.getHeight());
    ampVoiceSwitch.setBounds (voiceArea.withSizeKeepingCentre (
        juce::jmin (22, voiceArea.getWidth()), voiceSwitchHeight));
    ampVoiceLabel.setBounds (voiceLabelArea);
    ampVoiceSwitch.toFront (false);

    // Bypass toggle on the right.
    auto toggleArea = bar.removeFromRight (60);
    bar.removeFromRight (14);
    ampToggle.setBounds (toggleArea.withSizeKeepingCentre (50, 24));

    // Knobs fill the remaining middle space in 5 fixed-width slots. Drive
    // (slot 0) and Volume (slot 1) always land in the same two slots
    // regardless of voice -- they never move when Vintage/Boutique is
    // switched. Vintage's Tone sits in slot 2 -- the same slot Boutique's
    // Bass uses -- rather than centred among the 3 remaining slots, so the
    // first knob after Volume lands in the same spot either way.
    bar.removeFromTop (juce::jmin (20, bar.getHeight() / 6));
    const bool boutique = (int) std::round (processor.apvts.getRawParameterValue ("ampVoice")->load()) == 1;
    constexpr int numSlots = 5;
    std::array<juce::Rectangle<int>, numSlots> slots;
    {
        auto slotsArea = bar;
        const auto slotWidth = slotsArea.getWidth() / numSlots;
        for (int i = 0; i < numSlots; ++i)
            slots[(size_t) i] = slotsArea.removeFromLeft (i == numSlots - 1 ? slotsArea.getWidth() : slotWidth);
    }
    const auto placeInSlot = [&slots] (PhotoKnob& knob, int slotIndex)
    {
        auto cell = slots[(size_t) slotIndex];
        const auto inset = juce::jmax (2, cell.getWidth() / 10);
        knob.setBounds (cell.reduced (inset, 0));
        knob.toFront (false);
    };
    placeInSlot (ampDriveKnob, 0);
    placeInSlot (ampOutputKnob, 1);
    if (! boutique)
        placeInSlot (ampToneKnob, 2);
    else
    {
        placeInSlot (ampBassKnob, 2);
        placeInSlot (ampMidKnob, 3);
        placeInSlot (ampTrebleKnob, 4);
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
