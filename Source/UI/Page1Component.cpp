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

    constexpr int ts9VariantRadioGroup = 9001;
    for (int i = 0; i < 3; ++i)
    {
        auto& button = ts9VariantButtons[i];
        button.setClickingTogglesState (true);
        button.setRadioGroupId (ts9VariantRadioGroup, juce::dontSendNotification);
        button.setColour (juce::TextButton::buttonColourId, ThreadlineColours::panelDark);
        button.setColour (juce::TextButton::buttonOnColourId, ThreadlineColours::accent);
        button.setColour (juce::TextButton::textColourOffId, ThreadlineColours::textDim);
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (button);
        button.onClick = [this, i]
        {
            if (auto* parameter = processor.apvts.getParameter ("ts9Variant"))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) i));
        };
    }
    ts9VariantButtons[0].setToggleState (true, juce::dontSendNotification);

    startTimerHz (10);
}

void Page1Component::timerCallback()
{
    // Keeps the 3 buttons in sync with the actual parameter — it can also
    // change via preset load or automation, not just a click on one of them.
    const auto current = (int) std::round (processor.apvts.getRawParameterValue ("ts9Variant")->load());
    for (int i = 0; i < 3; ++i)
        if (ts9VariantButtons[i].getToggleState() != (i == current))
            ts9VariantButtons[i].setToggleState (i == current, juce::dontSendNotification);
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
    layoutHorizontalRackSection (klonSection, area.removeFromTop (cardHeight));
    area.removeFromTop (gap);
    layoutHorizontalRackSection (ts9Section, area, 294);

    const auto breakerBounds = ts9Section.bounds;
    // Stack the circuit variants immediately to the right of BREAKER. The
    // array remains parameter-index ordered (TS9, TS808, TS10), while the
    // requested visual order is TS9, TS10, TS808.
    auto variants = juce::Rectangle<int> (breakerBounds.getX() + 210,
                                          breakerBounds.getY() + 10, 126,
                                          breakerBounds.getHeight() - 20);
    const auto rowHeight = juce::jmax (20, (variants.getHeight() - 6) / 3);
    for (const auto index : { 0, 2, 1 })
    {
        ts9VariantButtons[index].setBounds (variants.removeFromTop (rowHeight).reduced (2, 1));
        variants.removeFromTop (2);
    }
}
