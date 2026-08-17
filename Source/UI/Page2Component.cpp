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

    buildSection (cabSection, *this, processor.apvts, "Cabinet (IR)", "cabOn", {
        { "cabMix", "Mix" }
    }, false, SectionPlate::Cab);

    cabIRBox.addItemList ({ "Bright Mix", "Dark Mix", "Medium Mix", "Medium 57",
                             "Medium 87", "Medium 160" }, 1);
    addAndMakeVisible (cabIRBox);
    cabIRAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "cabIRSelect", cabIRBox);
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

    // Two clean translucent cards over the global background. There is no
    // baked-in divider or oversized decorative frame competing with the
    // actual amp/cab geometry.
    paintCard (g, ampKnobFrameBounds);
    paintCard (g, cabSection.bounds);
}

void Page2Component::resized()
{
    auto area = getLocalBounds().reduced (20, 16);
    constexpr int columnGap = 14;
    const auto leftWidth = juce::roundToInt (static_cast<float> (area.getWidth() - columnGap) * 0.55f);
    ampImageFrameBounds = area.removeFromLeft (leftWidth);
    area.removeFromLeft (columnGap);

    constexpr int frameGap = 14;
    const auto frameHeight = (area.getHeight() - frameGap) / 2;
    ampKnobFrameBounds = area.removeFromTop (frameHeight);
    area.removeFromTop (frameGap);

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

    auto cabRow = area;
    cabSection.bounds = cabRow;
    cabRow.reduce (10, 8);
    auto header = cabRow.removeFromTop (24);
    cabSection.toggle.setBounds (header.removeFromRight (36).reduced (2, 0));
    cabIRBox.setBounds (header.removeFromRight (130).reduced (4, 0));
    cabSection.titleLabel.setBounds (header);

    cabRow.removeFromTop (juce::jmin (16, cabRow.getHeight() / 10));
    auto mixKnobArea = cabRow.removeFromLeft (juce::jmin (90, cabRow.getWidth()));
    if (! cabSection.knobs.empty())
        cabSection.knobs[0]->slider.setBounds (mixKnobArea.reduced (4, 0));

}
