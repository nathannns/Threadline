#include "Page1Component.h"

Page1Component::Page1Component (ThreadlineAudioProcessor& p) : processor (p)
{
    buildSection (compSection, *this, processor.apvts, "Compressor", "compOn", {
        { "compThreshold", "Comp" }, { "compRatio", "Attack" },
        { "compAttack", "Tilt" }, { "compRelease", "Mid" }, { "compMakeup", "Level" }
    }, false, SectionPlate::Compressor);

    buildSection (klonSection, *this, processor.apvts, "Klon", "klonOn", {
        { "klonGain", "Gain" }, { "klonTreble", "Treble" }, { "klonLevel", "Level" }
    }, false, SectionPlate::Klon);

    buildSection (ts9Section, *this, processor.apvts, "Breaker", "ts9On", {
        { "ts9Drive", "Drive" }, { "ts9Tone", "Tone" }, { "ts9Level", "Level" }
    }, false, SectionPlate::TS9);

    ts9VariantKnob.setRange (0.0, 2.0, 1.0);
    ts9VariantKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (ts9VariantKnob);
    ts9VariantLabel.setJustificationType (juce::Justification::centred);
    ts9VariantLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    ts9VariantLabel.setColour (juce::Label::textColourId, ThreadlineColours::textCream);
    addAndMakeVisible (ts9VariantLabel);
    ts9VariantAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "ts9Variant", ts9VariantKnob);

    constexpr int odOrderRadioGroup = 9002;
    const char* odOrderLabels[2] { "Klon First", "Breaker First" };
    for (int i = 0; i < 2; ++i)
    {
        auto& button = odOrderButtons[i];
        button.setButtonText (odOrderLabels[i]);
        button.setClickingTogglesState (true);
        button.setRadioGroupId (odOrderRadioGroup, juce::dontSendNotification);
        button.setColour (juce::TextButton::buttonColourId, ThreadlineColours::panelDark);
        button.setColour (juce::TextButton::buttonOnColourId, ThreadlineColours::accent);
        button.setColour (juce::TextButton::textColourOffId, ThreadlineColours::textDim);
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (button);
        button.onClick = [this, i]
        {
            if (auto* parameter = processor.apvts.getParameter ("odOrder"))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) i));
        };
    }
    odOrderButtons[0].setToggleState (true, juce::dontSendNotification);

    startTimerHz (10);
}

void Page1Component::timerCallback()
{
    // The knob's own rotation already reflects the parameter (it can only
    // ever be at one of the 3 detented positions), but a plain rotary knob
    // doesn't spell out which position that is the way the old labelled
    // buttons did — this label fills that in.
    static const char* variantNames[3] { "TS9", "TS808", "TS10" };
    const auto currentVariant = juce::jlimit (0, 2,
        (int) std::round (processor.apvts.getRawParameterValue ("ts9Variant")->load()));
    ts9VariantLabel.setText (variantNames[currentVariant], juce::dontSendNotification);

    const auto currentOrder = (int) std::round (processor.apvts.getRawParameterValue ("odOrder")->load());
    for (int i = 0; i < 2; ++i)
        if (odOrderButtons[i].getToggleState() != (i == currentOrder))
            odOrderButtons[i].setToggleState (i == currentOrder, juce::dontSendNotification);
}

void Page1Component::paint (juce::Graphics& g)
{
    paintSectionPlate (g, compSection);
    paintSectionPlate (g, klonSection);
    paintSectionPlate (g, ts9Section);
}

void Page1Component::resized()
{
    auto area = getLocalBounds().reduced (24, 12);
    constexpr int gap = 8;
    const auto cardHeight = (area.getHeight() - 2 * gap) / 3;
    layoutHorizontalRackSection (compSection, area.removeFromTop (cardHeight));
    // The five-knob compressor reads better with slightly smaller controls
    // and more breathing room than the three-knob drive racks.
    for (auto& knob : compSection.knobs)
    {
        const auto current = knob->slider.getBounds();
        knob->slider.setBounds (current.reduced (current.getWidth() / 10, current.getHeight() / 12));
    }
    area.removeFromTop (gap);
    layoutHorizontalRackSection (klonSection, area.removeFromTop (cardHeight), 300);
    area.removeFromTop (gap);
    layoutHorizontalRackSection (ts9Section, area, 294);

    const auto klonBounds = klonSection.bounds;
    // Order toggle sits in the same reserved title-row space the Breaker
    // variant switch below uses, just with 2 rows instead of 3.
    auto orderArea = juce::Rectangle<int> (klonBounds.getX() + 210,
                                           klonBounds.getY() + 10, 126,
                                           klonBounds.getHeight() - 20);
    const auto orderRowHeight = juce::jmax (20, (orderArea.getHeight() - 4) / 2);
    for (int i = 0; i < 2; ++i)
    {
        odOrderButtons[i].setBounds (orderArea.removeFromTop (orderRowHeight).reduced (2, 1));
        orderArea.removeFromTop (2);
    }

    const auto breakerBounds = ts9Section.bounds;
    // Fixed-position rotary selector, immediately to the right of BREAKER,
    // in the same reserved area the old 3-button stack used.
    auto variantArea = juce::Rectangle<int> (breakerBounds.getX() + 210,
                                             breakerBounds.getY() + 10, 126,
                                             breakerBounds.getHeight() - 20);
    ts9VariantLabel.setBounds (variantArea.removeFromTop (18));
    const auto knobSide = juce::jmin (variantArea.getWidth(), variantArea.getHeight());
    ts9VariantKnob.setBounds (juce::Rectangle<int> (knobSide, knobSide).withCentre (variantArea.getCentre()));
}
