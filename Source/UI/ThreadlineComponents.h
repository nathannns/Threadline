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
        // The value readout is drawn by this class's own paint() (see
        // valuesVisible below), not JUCE's built-in text box -- that let the
        // reserved space (and so the knob graphic's size/position) change
        // depending on whether the box was currently shown, which is exactly
        // what made the knob appear to jump when the eye-icon toggle fired.
        // valueRowHeight is reserved unconditionally instead, whether or not
        // the value is actually drawn this frame.
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    }

    void setStyle (Style newStyle) { style = newStyle; repaint(); }

    // Toggled globally by the eye icon in the editor header (see
    // ThreadlineAudioProcessorEditor::setAllKnobValuesVisible).
    void setValueVisible (bool visible)
    {
        if (valuesVisible == visible)
            return;
        valuesVisible = visible;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto& image = style == Style::Vintage ? getVintageImage() : getModernImage();
        if (! image.isValid())
            return;

        auto bounds = getLocalBounds().toFloat();
        auto valueArea = bounds.removeFromBottom (valueRowHeight);

        // Keep rotated photo corners and the lower shadow inside the component.
        // Vintage chicken-heads need a little more rotational clearance.
        const auto clearance = style == Style::Vintage ? 0.80f : 0.86f;
        auto side = juce::jmin (bounds.getWidth(), bounds.getHeight()) * clearance;
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

        if (valuesVisible)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (juce::FontOptions (juce::jmax (8.5f, valueRowHeight * 0.8f)));
            g.drawFittedText (getTextFromValue (getValue()), valueArea.toNearestInt(),
                              juce::Justification::centred, 1);
        }
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
    // Always subtracted from the drawing area, whether or not a value is
    // actually drawn there this frame -- see setValueVisible().
    static constexpr float valueRowHeight = 13.0f;

    Style style;
    bool valuesVisible = false;
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

        const auto trackWidth = juce::jmin (bounds.getWidth() * 0.32f, 12.0f);
        auto track = juce::Rectangle<float> (trackWidth, bounds.getHeight()).withCentre (bounds.getCentre());
        g.setColour (juce::Colour (0xff171411));
        g.fillRoundedRectangle (track, trackWidth * 0.5f);

        g.setColour (juce::Colour (0xff4d443a));
        for (int tick = 0; tick <= 4; ++tick)
        {
            const auto y = juce::jmap ((float) tick, 0.0f, 4.0f, track.getY(), track.getBottom());
            const auto half = tick == 2 ? bounds.getWidth() * 0.31f : bounds.getWidth() * 0.20f;
            g.drawLine (bounds.getCentreX() - half, y, bounds.getCentreX() + half, y,
                        tick == 2 ? 1.35f : 0.8f);
        }

        const auto normalised = (float) getNormalisableRange().convertTo0to1 ((float) getValue());
        const auto centreY = track.getY() + track.getHeight() * 0.5f; // 0 dB reference
        const auto capY = track.getBottom() - normalised * track.getHeight();

        g.setColour (juce::Colour (0xffc2ab86).withAlpha (0.55f));
        g.drawLine (bounds.getCentreX() - bounds.getWidth() * 0.34f, centreY,
                    bounds.getCentreX() + bounds.getWidth() * 0.34f, centreY, 1.0f);

        static const auto capImage = juce::ImageCache::getFromMemory (
            BinaryData::eq_slider_cap_png, BinaryData::eq_slider_cap_pngSize);
        const auto capHeight = juce::jmin (bounds.getWidth() * 0.96f, 34.0f);
        const auto capWidth = juce::jlimit (14.0f, 22.0f, trackWidth + 8.0f);
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
    ToggleFootswitch()
    {
        setClickingTogglesState (true);
        setWantsKeyboardFocus (true);
        setRepaintsOnMouseActivity (true);
    }

    void setWordmarkStyle (bool shouldUseWordmark)
    {
        wordmarkStyle = shouldUseWordmark;
        repaint();
    }

    void setRenderedImageStyle (bool shouldUseImages)
    {
        renderedImageStyle = shouldUseImages;
        repaint();
    }

    void setWordmarkCentred (bool shouldCentre)
    {
        wordmarkCentred = shouldCentre;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        const auto on = getToggleState();
        const auto hovered = isMouseOverOrDragging();
        const auto down = isDown();

        if (wordmarkStyle)
        {
            if (down) bounds.translate (0.0f, 1.0f);
            auto colour = findColour (on ? juce::ToggleButton::textColourId
                                         : juce::ToggleButton::tickDisabledColourId);
            if (hovered && isEnabled()) colour = colour.brighter (0.18f);
            if (! isEnabled()) colour = colour.withAlpha (0.35f);
            g.setColour (colour);
            g.setFont (juce::FontOptions (juce::jlimit (14.0f, 22.0f,
                                                       (float) getHeight() * 0.46f),
                                          juce::Font::bold));
            g.drawFittedText (getButtonText(), bounds.reduced (3, 0).toNearestInt(),
                              wordmarkCentred ? juce::Justification::centred
                                              : juce::Justification::centredLeft, 1);
            return;
        }

        static const auto off = juce::ImageCache::getFromMemory (
            BinaryData::button_off_png, BinaryData::button_off_pngSize);
        static const auto onImage = juce::ImageCache::getFromMemory (
            BinaryData::button_on_png, BinaryData::button_on_pngSize);

        const auto& image = on ? onImage : off;
        if (renderedImageStyle && image.isValid())
        {
            const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());
            auto imageBounds = juce::Rectangle<float> (side, side).withCentre (bounds.getCentre());
            g.drawImage (image, imageBounds,
                         juce::RectanglePlacement::stretchToFit);
            return;
        }

        if (down) bounds.translate (0.0f, 1.0f);
        if (! down)
        {
            g.setColour (juce::Colours::black.withAlpha (0.32f));
            g.fillRoundedRectangle (bounds.translated (0.0f, 1.5f), 5.0f);
        }
        auto face = on ? juce::Colour (0xffb9793c) : juce::Colour (0xff24201d);
        if (hovered && isEnabled()) face = face.brighter (0.10f);
        if (! isEnabled()) face = face.withAlpha (0.42f);
        g.setColour (face);
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour ((on || hovered) ? juce::Colour (0xffe7b46f) : juce::Colour (0xff665a50));
        g.drawRoundedRectangle (bounds, 5.0f, hovered ? 1.5f : 1.0f);
        if (hasKeyboardFocus (true))
        {
            g.setColour (juce::Colours::white.withAlpha (0.78f));
            g.drawRoundedRectangle (bounds.expanded (1.0f), 6.0f, 1.4f);
        }
        g.setColour (on ? juce::Colour (0xff171311) : juce::Colour (0xffc7b89f));
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        const auto label = getButtonText().isNotEmpty() ? getButtonText() : (on ? "ON" : "OFF");
        g.drawText (label, bounds, juce::Justification::centred);
    }

private:
    bool wordmarkStyle = false;
    bool wordmarkCentred = false;
    bool renderedImageStyle = true;
};

// Narrow vertical rocker switch, styled after a physical amp-panel toggle
// (e.g. a Bright switch) rather than a wide labeled button pair -- a small
// pill track with a thumb that slides between its two ends to show state.
// Doesn't draw its own caption; pair it with a Label naming the current
// selection, since a bare on/off dot alone doesn't say what the two throws
// mean for a 2-way *selector* (as opposed to a plain effect on/off).
class RockerSwitch : public juce::ToggleButton
{
public:
    RockerSwitch()
    {
        setClickingTogglesState (true);
        setWantsKeyboardFocus (true);
        setRepaintsOnMouseActivity (true);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        const auto on = getToggleState();
        const auto hovered = isMouseOverOrDragging();

        const auto trackWidth = juce::jmin (20.0f, bounds.getWidth());
        auto track = bounds.withSizeKeepingCentre (trackWidth, bounds.getHeight());

        g.setColour (juce::Colour (0xff1a1512));
        g.fillRoundedRectangle (track, trackWidth * 0.5f);
        // Matches ThreadlineColours::cardBorder -- can't reference that
        // namespace directly here, since SectionBuilder.h (where it's
        // defined) includes this file first, not the other way around.
        g.setColour (juce::Colour (0x997a5228));
        g.drawRoundedRectangle (track, trackWidth * 0.5f, 1.2f);

        const auto thumbSize = trackWidth - 5.0f;
        const auto thumbInset = 2.5f;
        const auto thumbTop = on ? track.getBottom() - thumbSize - thumbInset : track.getY() + thumbInset;
        juce::Rectangle<float> thumb (thumbSize, thumbSize);
        thumb.setPosition (track.getCentreX() - thumbSize * 0.5f, thumbTop);

        // Matches ThreadlineColours::accentBright (see note above).
        auto thumbColour = on ? juce::Colour (0xffd9a25a) : juce::Colour (0xff8c8078);
        if (hovered && isEnabled()) thumbColour = thumbColour.brighter (0.15f);
        if (! isEnabled()) thumbColour = thumbColour.withAlpha (0.4f);
        g.setColour (thumbColour);
        g.fillEllipse (thumb);

        if (hasKeyboardFocus (true))
        {
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.drawRoundedRectangle (track.expanded (2.0f), trackWidth * 0.5f + 2.0f, 1.3f);
        }
    }
};
