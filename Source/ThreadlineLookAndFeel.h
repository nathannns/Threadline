#pragma once

#include <JuceHeader.h>

// Amp/pedal-styled LookAndFeel with clean, non-illuminated controls.
class ThreadlineLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ThreadlineLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha (0.85f));
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1a1a1a));
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff444444));
        setColour (juce::ComboBox::textColourId, juce::Colours::white);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                            float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                            juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
        auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        auto centre = bounds.getCentre();
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Outer chrome bezel.
        {
            juce::ColourGradient bezel (juce::Colour (0xffb8b8b8), centre.x - radius, centre.y - radius,
                                         juce::Colour (0xff3a3a3a), centre.x + radius, centre.y + radius, false);
            g.setGradientFill (bezel);
            g.fillEllipse (bounds);
        }

        // Inner face — dark brushed metal.
        auto faceBounds = bounds.reduced (radius * 0.12f);
        {
            juce::ColourGradient face (juce::Colour (0xff2c2c2c), faceBounds.getX(), faceBounds.getY(),
                                        juce::Colour (0xff141414), faceBounds.getRight(), faceBounds.getBottom(), false);
            g.setGradientFill (face);
            g.fillEllipse (faceBounds);
        }

        // Value arc (accent colour derived from the slider's colour, falls back to amber).
        auto arcRadius = radius * 0.86f;
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                            rotaryStartAngle, angle, true);
        auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId, true);
        if (accent == juce::Colours::transparentBlack)
            accent = juce::Colour (0xffe0a030);
        g.setColour (accent);
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Pointer.
        juce::Path pointer;
        auto pointerLength = radius * 0.62f;
        auto pointerThickness = 3.0f;
        pointer.addRoundedRectangle (-pointerThickness * 0.5f, -pointerLength, pointerThickness, pointerLength * 0.7f, 1.5f);
        pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillPath (pointer);

        // Centre cap.
        g.setColour (juce::Colour (0xff1a1a1a));
        g.fillEllipse (juce::Rectangle<float> (radius * 0.22f, radius * 0.22f).withCentre (centre));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                            bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        auto bounds = button.getLocalBounds().toFloat().reduced (2.0f);

        g.setColour (juce::Colour (0xff1a1a1a));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (juce::Colour (0xff444444));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

        if (button.getToggleState())
        {
            g.setColour (juce::Colour (0xffb9793c).withAlpha (0.32f));
            g.fillRoundedRectangle (bounds.reduced (2.0f), 3.0f);
        }

        g.setColour (juce::Colours::white.withAlpha (button.getToggleState() ? 0.95f : 0.65f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (button.getButtonText(), bounds.toNearestInt(), juce::Justification::centred);
    }
};
