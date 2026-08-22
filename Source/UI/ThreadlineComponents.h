#pragma once

#include <JuceHeader.h>
#include <BinaryData.h>
#include "ThreadlineFonts.h"

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

// A rotary knob rendered from the available pedal-specific photo assets.
class PhotoKnob : public juce::Slider
{
public:
    enum class Style { Modern, Comp, Bull, Breaker, Fangs, Bison, Growl, Dynamix,
                       Tape, Tremolo, July, Amp, Cab, ChannelEQ, Delay, Desk,
                       Dimension, Ensemble, JC, ParallelA, ParallelB, Redface,
                       Reverb, Satellite, Spring };

    PhotoKnob() : PhotoKnob (Style::Modern) {}
    explicit PhotoKnob (Style initialStyle) : style (initialStyle)
    {
        setSliderStyle (juce::Slider::RotaryVerticalDrag);
        // No built-in text box -- the value readout instead uses JUCE's
        // popup display (see setValueVisible below), a floating bubble that
        // only exists while actively dragging. That reserves no layout
        // space at all, so the knob graphic draws at its full component
        // size all the time rather than a permanently-shrunk one to make
        // room for a value row that's blank most of the time.
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    }

    void setStyle (Style newStyle) { style = newStyle; repaint(); }

    // Toggled globally by the eye icon in the editor header (see
    // ThreadlineAudioProcessorEditor::setAllKnobValuesVisible). Shows the
    // value in a floating popup bubble while the knob is being dragged
    // (JUCE's own Slider popup, positioned on the desktop so it isn't
    // clipped by this knob's own small bounds or its parent card), rather
    // than a permanent readout under the knob.
    void setValueVisible (bool visible)
    {
        if (valuesVisible == visible)
            return;
        valuesVisible = visible;
        setPopupDisplayEnabled (visible, false, nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        const auto normalised = (float) getNormalisableRange().convertTo0to1 ((float) getValue());
        auto& image = style == Style::ParallelA && normalised > 0.5f
                    ? getParallelBImage() : getImageForStyle (style);
        if (! image.isValid())
            return;

        auto bounds = getLocalBounds().toFloat();

        // Render at 75% of the original diameter (1.5x the reduced size)
        // while retaining the full component as the drag target.
        const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.645f;
        auto knobBounds = juce::Rectangle<float> (side, side).withCentre (bounds.getCentre());

        // The source PNGs contain transparent pixels around and within the
        // photographed hardware. A solid backing keeps light rack artwork
        // from showing through.
        g.setColour (juce::Colour (0xff211a16));
        g.fillEllipse (knobBounds.reduced (side * 0.08f));

        const auto rotary = getRotaryParameters();
        const auto angle = rotary.startAngleRadians + normalised * (rotary.endAngleRadians - rotary.startAngleRadians);

        const auto imgW = (float) image.getWidth();
        const auto imgH = (float) image.getHeight();
        const auto scale = juce::jmin (knobBounds.getWidth() / imgW, knobBounds.getHeight() / imgH);
        const auto pivotPxX = imgW * 0.5f;
        const auto pivotPxY = imgH * 0.5f;

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

    static const juce::Image& getModernImage()
    {
        static juce::Image image = juce::ImageCache::getFromMemory (BinaryData::knob_effects_png, BinaryData::knob_effects_pngSize);
        return image;
    }

    static const juce::Image& getReverbImage()
    {
        static juce::Image image = juce::ImageCache::getFromMemory (BinaryData::knob_reverb_png, BinaryData::knob_reverb_pngSize);
        return image;
    }

#define THREADLINE_KNOB_IMAGE(METHOD, NAME) \
    static const juce::Image& METHOD() { \
        static juce::Image image = juce::ImageCache::getFromMemory (BinaryData::NAME##_png, BinaryData::NAME##_pngSize); \
        return image; \
    }
    THREADLINE_KNOB_IMAGE (getCompImage, knob_comp)
    THREADLINE_KNOB_IMAGE (getBullImage, knob_bull)
    THREADLINE_KNOB_IMAGE (getBreakerImage, knob_breaker)
    THREADLINE_KNOB_IMAGE (getFangsImage, knob_fangs)
    THREADLINE_KNOB_IMAGE (getBisonImage, knob_bison)
    THREADLINE_KNOB_IMAGE (getGrowlImage, knob_growl)
    THREADLINE_KNOB_IMAGE (getDynamixImage, knob_dynamix)
    THREADLINE_KNOB_IMAGE (getTapeImage, knob_tape)
    THREADLINE_KNOB_IMAGE (getTremoloImage, knob_tremolo)
    THREADLINE_KNOB_IMAGE (getJulyImage, knob_july)
    THREADLINE_KNOB_IMAGE (getAmpImage, knob_amp)
    THREADLINE_KNOB_IMAGE (getCabImage, knob_cab)
    THREADLINE_KNOB_IMAGE (getChannelEQImage, knob_channeleq)
    THREADLINE_KNOB_IMAGE (getDelayImage, knob_delay)
    THREADLINE_KNOB_IMAGE (getDeskImage, knob_desk)
    THREADLINE_KNOB_IMAGE (getDimensionImage, knob_dimension)
    THREADLINE_KNOB_IMAGE (getEnsembleImage, knob_ensemble)
    THREADLINE_KNOB_IMAGE (getJCImage, knob_jc)
    THREADLINE_KNOB_IMAGE (getParallelAImage, knob_parallel_a)
    THREADLINE_KNOB_IMAGE (getParallelBImage, knob_parallel_b)
    THREADLINE_KNOB_IMAGE (getRedfaceImage, knob_redface)
    THREADLINE_KNOB_IMAGE (getSatelliteImage, knob_satelite)
    THREADLINE_KNOB_IMAGE (getSpringImage, knob_spring)
#undef THREADLINE_KNOB_IMAGE

    static const juce::Image& getImageForStyle (Style style)
    {
        switch (style)
        {
            case Style::Comp:     return getCompImage();
            case Style::Bull:     return getBullImage();
            case Style::Breaker:  return getBreakerImage();
            case Style::Fangs:    return getFangsImage();
            case Style::Bison:    return getBisonImage();
            case Style::Growl:    return getGrowlImage();
            case Style::Dynamix:  return getDynamixImage();
            case Style::Tape:     return getTapeImage();
            case Style::Tremolo:  return getTremoloImage();
            case Style::July:     return getJulyImage();
            case Style::Amp:      return getAmpImage();
            case Style::Cab:      return getCabImage();
            case Style::ChannelEQ:return getChannelEQImage();
            case Style::Delay:    return getDelayImage();
            case Style::Desk:     return getDeskImage();
            case Style::Dimension:return getDimensionImage();
            case Style::Ensemble: return getEnsembleImage();
            case Style::JC:       return getJCImage();
            case Style::ParallelA:return getParallelAImage();
            case Style::ParallelB:return getParallelBImage();
            case Style::Redface:  return getRedfaceImage();
            case Style::Reverb:   return getReverbImage();
            case Style::Satellite:return getSatelliteImage();
            case Style::Spring:   return getSpringImage();
            case Style::Modern:
            default:            return getModernImage();
        }
    }

private:
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
            g.setFont (ThreadlineFonts::semiBold (juce::jlimit (14.0f, 22.0f, (float) getHeight() * 0.46f)));
            g.drawFittedText (getButtonText(), bounds.reduced (3, 0).toNearestInt(),
                              wordmarkCentred ? juce::Justification::centred
                                              : juce::Justification::centredLeft, 1);
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
        g.setFont (ThreadlineFonts::medium (9.5f));
        const auto label = getButtonText().isNotEmpty() ? getButtonText() : (on ? "ON" : "OFF");
        g.drawText (label, bounds, juce::Justification::centred);
    }

private:
    bool wordmarkStyle = false;
    bool wordmarkCentred = false;
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
        setRepaintsOnMouseActivity (true);
    }

    void paint (juce::Graphics& g) override
    {
        static const auto offImage = juce::ImageCache::getFromMemory (
            BinaryData::rocker_off_png, BinaryData::rocker_off_pngSize);
        static const auto onImage = juce::ImageCache::getFromMemory (
            BinaryData::rocker_on_png, BinaryData::rocker_on_pngSize);

        const auto on = getToggleState();
        const auto& fullImage = on ? onImage : offImage;
        if (! fullImage.isValid())
            return;

        // The two source photos have very different canvas padding (and the
        // "on" shot has an amber glow baked in around the pill), so drawing
        // each whole canvas into the same bounds would make the switch
        // visibly change size between states. These are a one-time tight
        // crop (ImageMagick `-fuzz 10% -trim`) of each photo's actual pill
        // content in its own pixel coordinates; getClippedImage() extracts
        // just that region so both states draw the pill at a consistent
        // size regardless of the source canvases' differing padding/glow.
        const auto source = on ? juce::Rectangle<int> (225, 117, 574, 1261)
                                : juce::Rectangle<int> (630, 57, 399, 822);
        const auto image = fullImage.getClippedImage (source);

        auto bounds = getLocalBounds().toFloat();
        const auto sourceAspect = static_cast<float> (source.getWidth()) / static_cast<float> (source.getHeight());
        auto dest = juce::Rectangle<float> (bounds.getHeight() * sourceAspect, bounds.getHeight());
        if (dest.getWidth() > bounds.getWidth())
            dest = juce::Rectangle<float> (bounds.getWidth(), bounds.getWidth() / sourceAspect);
        dest = dest.withCentre (bounds.getCentre());

        g.drawImage (image, dest, juce::RectanglePlacement::stretchToFit);

        if (! isEnabled())
        {
            g.setColour (juce::Colour (0xff241912).withAlpha (0.5f));
            g.fillRoundedRectangle (dest, dest.getWidth() * 0.3f);
        }
    }
};
