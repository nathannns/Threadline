#pragma once

#include <JuceHeader.h>
#include <BinaryData.h>

// Small horizontal-bar peak meter. Polled by the editor's timer.
class LevelMeterBar : public juce::Component
{
public:
    void setLevel (float newPeak) { peak = newPeak; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (bounds, 3.0f);

        const auto db = juce::Decibels::gainToDecibels (peak, -60.0f);
        const auto normalised = juce::jlimit (0.0f, 1.0f, juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f));
        auto fill = bounds.reduced (2.0f);
        fill.setWidth (fill.getWidth() * normalised);

        auto colour = normalised > 0.92f ? juce::Colours::red
                    : normalised > 0.75f ? juce::Colours::orange
                                         : juce::Colours::limegreen;
        g.setColour (colour);
        g.fillRoundedRectangle (fill, 2.0f);
    }

private:
    float peak = 0.0f;
};

// A rotary knob rendered from a real knob photo. Two styles are shot from
// different reference photos with different geometry:
//   - Vintage: the tweed amp's chicken-head knob — reserved for the Amp
//     page's own Drive/Tone/Output, mirroring the real 5E3 panel. The photo
//     is taller than it is wide (the pointer extends above the round base),
//     so it rotates around the base's centre, well below image-centre.
//   - Modern: the brushed dark/bronze disc knob used for every other
//     ("effects") control — Gate, Input/Output, Compressor, Klon, TS9, Cab
//     Mix, Tremolo, Chorus, Delay, Reverb. Symmetric, so it rotates around
//     its own image centre.
class PhotoKnob : public juce::Slider
{
public:
    enum class Style { Vintage, Modern };

    PhotoKnob() : PhotoKnob (Style::Modern) {}
    explicit PhotoKnob (Style initialStyle) : style (initialStyle)
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        setColour (juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha (0.85f));
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    void setStyle (Style newStyle) { style = newStyle; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto& image = style == Style::Vintage ? getVintageImage() : getModernImage();
        if (! image.isValid())
            return;

        auto bounds = getLocalBounds().toFloat();
        if (getTextBoxPosition() == juce::Slider::TextBoxBelow)
            bounds.removeFromBottom ((float) getTextBoxHeight());

        auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());
        auto knobBounds = juce::Rectangle<float> (side, side).withCentre (bounds.getCentre());

        // The source PNGs contain transparent pixels around and within the
        // photographed hardware. A solid backing keeps light rack artwork
        // from showing through the compressor and other effect knobs.
        if (style == Style::Modern)
        {
            g.setColour (juce::Colour (0xff211a16));
            g.fillEllipse (knobBounds.reduced (side * 0.08f));
        }

        const auto rotary = getRotaryParameters();
        const auto normalised = (float) getNormalisableRange().convertTo0to1 ((float) getValue());
        const auto angle = rotary.startAngleRadians + normalised * (rotary.endAngleRadians - rotary.startAngleRadians);

        const auto imgW = (float) image.getWidth();
        const auto imgH = (float) image.getHeight();
        const auto scale = juce::jmin (knobBounds.getWidth() / imgW, knobBounds.getHeight() / imgH);
        const auto pivotYRatio = style == Style::Vintage ? vintagePivotYRatio : 0.5f;
        const auto pivotPxX = imgW * 0.5f;
        const auto pivotPxY = imgH * pivotYRatio;

        auto transform = juce::AffineTransform::translation (-pivotPxX, -pivotPxY)
                              .scaled (scale)
                              .rotated (angle)
                              .translated (knobBounds.getCentreX(), knobBounds.getCentreY());

        // Shadow: same trick Rockalizer's own editor uses for its logo —
        // draw the image offset with the alpha channel filled solid black
        // (a silhouette), then the real image on top with no offset.
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawImageTransformed (image, transform.translated (2.5f, 3.5f), true);

        g.drawImageTransformed (image, transform);
    }

    static const juce::Image& getVintageImage()
    {
        static juce::Image image = juce::ImageCache::getFromMemory (BinaryData::knob_pointer_png, BinaryData::knob_pointer_pngSize);
        return image;
    }

    static const juce::Image& getModernImage()
    {
        static juce::Image image = juce::ImageCache::getFromMemory (BinaryData::knob_effects_png, BinaryData::knob_effects_pngSize);
        return image;
    }

private:
    // Measured shaft centre in the 279x417 crop. The previous 0.665 pivot was
    // below the physical shaft and made the knob orbit like a wiper.
    static constexpr float vintagePivotYRatio = 222.0f / 417.0f;

    Style style;
};

// One vertical fader of the 9-band graphic EQ. No numeric readout by
// design — like a real graphic-EQ pedal, the fader position itself is the
// feedback, and 9 numeric boxes would be cramped and noisy at this width.
class EQBand : public juce::Slider
{
public:
    EQBand()
    {
        setSliderStyle (juce::Slider::LinearVertical);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        const auto trackWidth = juce::jmin (bounds.getWidth() * 0.32f, 10.0f);
        auto track = juce::Rectangle<float> (trackWidth, bounds.getHeight()).withCentre (bounds.getCentre());
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (track, trackWidth * 0.5f);

        const auto normalised = (float) getNormalisableRange().convertTo0to1 ((float) getValue());
        const auto centreY = track.getY() + track.getHeight() * 0.5f; // 0 dB reference
        const auto capY = track.getBottom() - normalised * track.getHeight();

        auto fillTop = juce::jmin (capY, centreY);
        auto fillBottom = juce::jmax (capY, centreY);
        g.setColour (juce::Colour (0xffd9a25a).withAlpha (0.85f)); // matches ThreadlineColours::accentBright
        g.fillRoundedRectangle (track.withY (fillTop).withHeight (fillBottom - fillTop), trackWidth * 0.5f);

        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.drawLine (bounds.getX(), centreY, bounds.getRight(), centreY, 1.0f);

        static const auto capImage = juce::ImageCache::getFromMemory (
            BinaryData::eq_slider_cap_png, BinaryData::eq_slider_cap_pngSize);
        const auto capWidth = juce::jmin (bounds.getWidth() * 0.96f, 34.0f);
        const auto capHeight = juce::jlimit (9.0f, 15.0f, capWidth / 3.0f);
        auto cap = juce::Rectangle<float> (capWidth, capHeight).withCentre ({ bounds.getCentreX(), capY });
        g.setColour (juce::Colour (0xff211a16));
        g.fillRoundedRectangle (cap, 3.0f);
        g.drawImage (capImage, cap, juce::RectanglePlacement::stretchToFit);
    }
};

// Compact image-free rack bypass. No jewel, lamp, or LED-style state control.
class ToggleFootswitch : public juce::ToggleButton
{
public:
    ToggleFootswitch() { setClickingTogglesState (true); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        const auto on = getToggleState();
        g.setColour (on ? juce::Colour (0xffb9793c) : juce::Colour (0xff24201d));
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (on ? juce::Colour (0xffe7b46f) : juce::Colour (0xff665a50));
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
        g.setColour (on ? juce::Colour (0xff171311) : juce::Colour (0xffc7b89f));
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        const auto label = getButtonText().isNotEmpty() ? getButtonText() : (on ? "ON" : "OFF");
        g.drawText (label, bounds, juce::Justification::centred);
    }
};
