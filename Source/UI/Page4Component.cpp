#include "Page4Component.h"

namespace
{
    juce::String formatFrequencyLabel (float hz)
    {
        if (hz >= 1000.0f)
        {
            const auto k = hz / 1000.0f;
            const auto isWhole = std::abs (k - std::round (k)) < 0.01f;
            return (isWhole ? juce::String ((int) std::round (k)) : juce::String (k, 1)) + "k";
        }
        return juce::String ((int) hz);
    }

    void setupFilterKnob (juce::Label& label, ToggleFootswitch& toggle, PhotoKnob& knob,
                           juce::Component& parent, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ThreadlineColours::textCream);
        parent.addAndMakeVisible (label);
        parent.addAndMakeVisible (toggle);
        parent.addAndMakeVisible (knob);
    }
}

Page4Component::Page4Component (ThreadlineAudioProcessor& p)
{
    titleLabel.setText ("9-Band EQ", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, ThreadlineColours::textCream);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    addAndMakeVisible (eqToggle);
    eqToggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        p.apvts, "eqOn", eqToggle);

    static const char* bandParamIds[GraphicEQModule::numBands] = {
        "eqBand1", "eqBand2", "eqBand3", "eqBand4", "eqBand5", "eqBand6", "eqBand7", "eqBand8", "eqBand9"
    };
    const auto& freqs = GraphicEQModule::getCentreFrequencies();
    for (int i = 0; i < GraphicEQModule::numBands; ++i)
    {
        auto band = std::make_unique<Band>();
        addAndMakeVisible (band->slider);

        band->freqLabel.setText (formatFrequencyLabel (freqs[(size_t) i]), juce::dontSendNotification);
        band->freqLabel.setJustificationType (juce::Justification::centred);
        band->freqLabel.setFont (juce::FontOptions (11.0f));
        band->freqLabel.setColour (juce::Label::textColourId, ThreadlineColours::textDim);
        addAndMakeVisible (band->freqLabel);

        band->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            p.apvts, bandParamIds[i], band->slider);

        bands[(size_t) i] = std::move (band);
    }

    setupFilterKnob (hpfLabel, hpfToggle, hpfKnob, *this, "HPF");
    hpfToggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        p.apvts, "eqHpfOn", hpfToggle);
    hpfKnobAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        p.apvts, "eqHpfFreq", hpfKnob);

    setupFilterKnob (lpfLabel, lpfToggle, lpfKnob, *this, "LPF");
    lpfToggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        p.apvts, "eqLpfOn", lpfToggle);
    lpfKnobAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        p.apvts, "eqLpfFreq", lpfKnob);
}

void Page4Component::paint (juce::Graphics& g)
{
    paintThreadlineBackground (g, getLocalBounds());
    static const auto eqPlate = juce::ImageCache::getFromMemory (
        BinaryData::plate_eq_png, BinaryData::plate_eq_pngSize);
    g.drawImage (eqPlate, cardBounds.toFloat(), juce::RectanglePlacement::fillDestination);
    g.setColour (juce::Colours::black.withAlpha (0.12f));
    g.fillRoundedRectangle (cardBounds.toFloat(), 10.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (cardBounds.toFloat(), 10.0f, 1.2f);
}

void Page4Component::resized()
{
    cardBounds = getLocalBounds().reduced (24, 16);
    auto area = cardBounds.reduced (18, 14);

    auto header = area.removeFromTop (24);
    eqToggle.setBounds (header.removeFromRight (50));
    titleLabel.setBounds (header);
    area.removeFromTop (14);

    auto hpfCol = area.removeFromLeft (86);
    area.removeFromLeft (12);
    auto lpfCol = area.removeFromRight (86);
    area.removeFromRight (12);

    auto layoutFilterColumn = [] (juce::Rectangle<int> col, juce::Label& label,
                                   ToggleFootswitch& toggle, PhotoKnob& knob)
    {
        label.setBounds (col.removeFromTop (18));
        col.removeFromTop (6);
        toggle.setBounds (col.removeFromTop (22).withSizeKeepingCentre (50, 22));
        col.removeFromTop (10);
        knob.setBounds (col.removeFromTop (juce::jmin (col.getHeight(), 100)));
    };
    layoutFilterColumn (hpfCol, hpfLabel, hpfToggle, hpfKnob);
    layoutFilterColumn (lpfCol, lpfLabel, lpfToggle, lpfKnob);

    const auto bandWidth = area.getWidth() / GraphicEQModule::numBands;
    for (auto& band : bands)
    {
        auto slot = area.removeFromLeft (bandWidth);
        band->freqLabel.setBounds (slot.removeFromBottom (16));
        slot.removeFromBottom (4);
        band->slider.setBounds (slot.reduced (juce::jmax (2, bandWidth / 5), 0));
    }
}
