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
    setupAmpKnob (ampOutputLabel, ampOutputKnob, "Output");
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

    // Two IR slots, processed in parallel and blended — like two mics on
    // the same cab rather than two cabs chained one after another.
    setupCabSlot (cabASection, *this, processor.apvts, "Cab A", "cabAOn", "cabAMix", SectionPlate::Cab);
    setupCabSlot (cabBSection, *this, processor.apvts, "Cab B", "cabBOn", "cabBMix", SectionPlate::Cab);

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

    auto knobRow = ampKnobFrameBounds.reduced (juce::jmax (6, ampKnobFrameBounds.getWidth() / 28), 8);
    knobRow.removeFromTop (juce::jmin (18, knobRow.getHeight() / 8));
    auto knobWidth = knobRow.getWidth() / 3;
    const auto knobInset = juce::jmax (2, knobWidth / 14);
    ampDriveKnob.setBounds (knobRow.removeFromLeft (knobWidth).reduced (knobInset, 0));
    ampToneKnob.setBounds (knobRow.removeFromLeft (knobWidth).reduced (knobInset, 0));
    ampOutputKnob.setBounds (knobRow.reduced (knobInset, 0));
    ampDriveKnob.toFront (false);
    ampToneKnob.toFront (false);
    ampOutputKnob.toFront (false);

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
        section.toggle.setBounds (header.removeFromRight (36).reduced (2, 0));
        phaseButton.setBounds (header.removeFromRight (26).reduced (1, 0));
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
