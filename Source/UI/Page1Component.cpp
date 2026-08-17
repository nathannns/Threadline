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
    // Fixed layout: TS9 (value 0) bottom-left, TS808 (value 1) top, TS10
    // (value 2) bottom-right — the standard "7 o'clock to 5 o'clock through
    // 12" rotary sweep. Set explicitly (rather than relying on PhotoKnob's
    // inherited default, even though it happens to match) since the 3
    // detented stops landing in these exact positions is the whole point.
    juce::Slider::RotaryParameters variantRotary;
    variantRotary.startAngleRadians = juce::MathConstants<float>::pi * 1.2f;
    variantRotary.endAngleRadians = juce::MathConstants<float>::pi * 2.8f;
    variantRotary.stopAtEnd = true;
    ts9VariantKnob.setRotaryParameters (variantRotary);
    ts9VariantKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (ts9VariantKnob);
    ts9VariantLabel.setJustificationType (juce::Justification::centred);
    ts9VariantLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    ts9VariantLabel.setColour (juce::Label::textColourId, ThreadlineColours::textCream);
    addAndMakeVisible (ts9VariantLabel);
    ts9VariantAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "ts9Variant", ts9VariantKnob);

    odOrderSwitch.setTitle ("Overdrive order");
    odOrderSwitch.setHelpText ("Klon first or Breaker first ahead of the Amp");
    addAndMakeVisible (odOrderSwitch);
    odOrderSwitch.onClick = [this]
    {
        if (auto* parameter = processor.apvts.getParameter ("odOrder"))
            parameter->setValueNotifyingHost (odOrderSwitch.getToggleState() ? 1.0f : 0.0f);
    };
    odOrderLabel.setJustificationType (juce::Justification::centred);
    odOrderLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    odOrderLabel.setColour (juce::Label::textColourId, ThreadlineColours::textCream);
    addAndMakeVisible (odOrderLabel);

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
    if (odOrderSwitch.getToggleState() != (currentOrder == 1))
        odOrderSwitch.setToggleState (currentOrder == 1, juce::dontSendNotification);
    odOrderLabel.setText (currentOrder == 1 ? "BREAKER FIRST" : "KLON FIRST", juce::dontSendNotification);
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
    // Order rocker switch sits in the same reserved title-row space the
    // Breaker variant switch below uses -- switch on top, label under it,
    // same arrangement as the Amp page's Voice switch. Height capped at 40
    // (was 54) *and* at whatever's actually available, same fix as the Amp
    // page's Voice switch (that fixed ceiling alone could exceed the real
    // available height and render cropped at the top).
    auto orderArea = juce::Rectangle<int> (klonBounds.getX() + 210,
                                           klonBounds.getY() + 14, 110,
                                           klonBounds.getHeight() - 28);
    auto orderLabelArea = orderArea.removeFromBottom (juce::jmin (16, orderArea.getHeight() / 4));
    odOrderSwitch.setBounds (orderArea.withSizeKeepingCentre (
        juce::jmin (22, orderArea.getWidth()), juce::jmin (40, orderArea.getHeight())));
    odOrderLabel.setBounds (orderLabelArea);

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
