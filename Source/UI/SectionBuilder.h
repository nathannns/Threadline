#pragma once

#include <JuceHeader.h>
#include "ThreadlineComponents.h"

// Shared amber/tan-on-dark-maroon palette used across the whole editor.
namespace ThreadlineColours
{
    static const juce::Colour background   { 0xff3a1a12 };
    static const juce::Colour panelDark    { 0xff2a120c };
    static const juce::Colour accent       { 0xffb37a3c };
    static const juce::Colour accentBright { 0xffd9a25a };
    static const juce::Colour accentDim    { 0xff7a5228 };
    static const juce::Colour textCream    { 0xfff2e6c9 };
    static const juce::Colour textDim      { 0xffcbb894 };

    // Charcoal-to-workbench-wood gradient used across the whole window.
    static const juce::Colour bgTop    { 0xff1c1712 };
    static const juce::Colour bgBottom { 0xff4a2d18 };

    static const juce::Colour cardFill   { 0x40000000 };
    static const juce::Colour cardBorder { 0x997a5228 };
}

namespace SectionGrid
{
    // Every plated section card — on Page1 or Page3 — is sized off the SAME
    // divisor (the larger page's section count) and the same gap, so cards
    // are pixel-identical between pages instead of each page independently
    // dividing its own height by its own section count. Page1 (3 sections)
    // just uses 3 of these 4 slots and centres the leftover as padding.
    constexpr int slotsPerPage = 4;
    constexpr int gap = 14;
}

// The dark-charcoal-to-warm-wood gradient every page and the main editor
// paint as their base background, so the whole window reads as one surface.
inline void paintThreadlineBackground (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    juce::ColourGradient gradient (ThreadlineColours::bgTop, (float) bounds.getX(), (float) bounds.getY(),
                                    ThreadlineColours::bgBottom, (float) bounds.getX(), (float) bounds.getBottom(), false);
    g.setGradientFill (gradient);
    g.fillRect (bounds);
}

// A floating "module card" backdrop — used behind Gate/Input/Output and the
// Cabinet section, in place of a flat panel fill.
inline void paintCard (juce::Graphics& g, juce::Rectangle<int> bounds, float cornerSize = 10.0f)
{
    if (bounds.isEmpty())
        return;
    auto b = bounds.toFloat();
    juce::ColourGradient fill (juce::Colour (0xff241b16), b.getX(), b.getY(),
                               juce::Colour (0xff171311), b.getRight(), b.getBottom(), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (b, cornerSize);
    g.setColour (ThreadlineColours::cardBorder);
    g.drawRoundedRectangle (b, cornerSize, 1.2f);
}

// One knob + its value-label, wired to a parameter.
struct KnobUI
{
    PhotoKnob slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A titled group of knobs with an optional footswitch enable toggle — the
// building block every DSP section (Compressor, Klon, Tremolo, ...) is made
// from, on every page.
struct SectionUI
{
    juce::Label titleLabel;
    ToggleFootswitch toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
    std::vector<std::unique_ptr<KnobUI>> knobs;
    bool hasToggle = true;

    // Set by layoutSection each resize, read back by paintSectionPlate — the
    // rack-plate photo backdrop is drawn behind this section's children.
    juce::Rectangle<int> bounds;
    int plateIndex = 0;
};

// A dedicated plate photo per DSP section (Cab has none of its own, so it
// reuses Compressor's — everything else is a direct 1:1 match).
namespace SectionPlate
{
    enum { Gate = 0, Compressor, Klon, TS9, Tremolo, Chorus, Delay, Reverb, count, Cab = Compressor };
}

// Shared image-independent module card. Keeping this vector-first makes every
// page coherent before a future optional skin is applied; the old plate assets
// remain available but no longer define geometry or readability.
inline void paintSectionPlate (juce::Graphics& g, const SectionUI& section)
{
    if (section.bounds.isEmpty())
        return;
    auto bounds = section.bounds.toFloat();
    static const juce::Colour rackColours[] {
        juce::Colour (0xff26201c), juce::Colour (0xffd0c3aa), juce::Colour (0xff62777a),
        juce::Colour (0xffa34d38), juce::Colour (0xffb5aa92), juce::Colour (0xff536f70),
        juce::Colour (0xff8e3c31), juce::Colour (0xffc2b69d)
    };
    const auto face = rackColours[juce::jlimit (0, 7, section.plateIndex)];
    juce::ColourGradient fill (face.brighter (0.10f), bounds.getX(), bounds.getY(),
                               face.darker (0.25f), bounds.getRight(), bounds.getBottom(), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (bounds, 9.0f);
    g.setColour (ThreadlineColours::cardBorder);
    g.drawRoundedRectangle (bounds.reduced (0.75f), 9.0f, 1.25f);
    g.setColour (juce::Colour (0xff2b2520).withAlpha (0.72f));
    constexpr float screwSize = 6.0f;
    g.fillEllipse (bounds.getX() + 7.0f, bounds.getCentreY() - screwSize * 0.5f, screwSize, screwSize);
    g.fillEllipse (bounds.getRight() - 13.0f, bounds.getCentreY() - screwSize * 0.5f, screwSize, screwSize);
}

inline void layoutHorizontalRackSection (SectionUI& section, juce::Rectangle<int> area)
{
    section.bounds = area;
    area.reduce (10, 7);
    auto identity = area.removeFromLeft (juce::jlimit (112, 210, area.getWidth() / 5));
    section.titleLabel.setJustificationType (juce::Justification::centred);
    section.titleLabel.setFont (juce::FontOptions (juce::jlimit (14.0f, 22.0f,
                                                   static_cast<float> (area.getHeight()) * 0.28f),
                                                   juce::Font::bold));
    const auto lightFace = section.plateIndex == SectionPlate::Compressor
                        || section.plateIndex == SectionPlate::Tremolo
                        || section.plateIndex == SectionPlate::Reverb;
    section.titleLabel.setColour (juce::Label::textColourId,
                                  lightFace ? juce::Colour (0xff352a22) : ThreadlineColours::textCream);
    section.titleLabel.setBounds (identity.removeFromTop (juce::jmax (24, identity.getHeight() - 24)));
    if (section.hasToggle)
        section.toggle.setBounds (identity.withSizeKeepingCentre (58, 20));

    area.removeFromLeft (10);
    const auto count = juce::jmax (1, (int) section.knobs.size());
    const auto cellWidth = area.getWidth() / count;
    for (auto& knob : section.knobs)
    {
        knob->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        auto cell = area.removeFromLeft (cellWidth);
        knob->slider.setBounds (cell.reduced (4, 3).withTrimmedTop (15));
    }
}

// Adds title/toggle/knobs as children of `parent` and wires them to `apvts`.
// Pass an empty toggleParamId for sections with no on/off switch (e.g. Amp).
// plateIndex is retained as semantic metadata for a future optional skin;
// the base UI intentionally uses one shared vector card.
inline void buildSection (SectionUI& section, juce::Component& parent,
                           juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& title, const juce::String& toggleParamId,
                           std::initializer_list<std::pair<const char*, const char*>> paramIdAndLabel,
                           bool lightLabels = false, int plateIndex = 0)
{
    section.plateIndex = plateIndex;
    section.titleLabel.setText (title, juce::dontSendNotification);
    section.titleLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    section.titleLabel.setColour (juce::Label::textColourId,
        lightLabels ? juce::Colours::white : ThreadlineColours::textCream);
    section.titleLabel.setJustificationType (juce::Justification::centredLeft);
    parent.addAndMakeVisible (section.titleLabel);

    section.hasToggle = toggleParamId.isNotEmpty();
    section.toggle.setVisible (section.hasToggle);
    if (section.hasToggle)
    {
        parent.addAndMakeVisible (section.toggle);
        section.toggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvts, toggleParamId, section.toggle);
    }

    for (auto& [paramId, labelText] : paramIdAndLabel)
    {
        auto knob = std::make_unique<KnobUI>();
        knob->label.setText (labelText, juce::dontSendNotification);
        knob->label.setJustificationType (juce::Justification::centred);
        knob->label.setFont (juce::FontOptions (12.0f));
        knob->label.setColour (juce::Label::textColourId,
            lightLabels ? juce::Colours::white : ThreadlineColours::textDim);
        knob->label.attachToComponent (&knob->slider, false);

        parent.addAndMakeVisible (knob->slider);
        parent.addAndMakeVisible (knob->label);

        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, paramId, knob->slider);

        section.knobs.push_back (std::move (knob));
    }
}

inline void layoutSection (SectionUI& section, juce::Rectangle<int> area)
{
    section.bounds = area;
    area.reduce (10, 8);
    auto header = area.removeFromTop (24);
    if (section.hasToggle)
        section.toggle.setBounds (header.removeFromRight (36).reduced (2, 0));
    section.titleLabel.setBounds (header);

    area.removeFromTop (16); // room for the knob value-labels (drawn above each slider)

    auto count = juce::jmax (1, (int) section.knobs.size());
    auto knobWidth = area.getWidth() / count;
    for (auto& knob : section.knobs)
        knob->slider.setBounds (area.removeFromLeft (knobWidth).reduced (6, 0));
}

// Tall rack-module layout inspired by hardware channel strips: modules sit
// side-by-side and their knobs wrap into a compact grid instead of shrinking
// into a few pixels inside short horizontal rows.
inline void layoutRackSection (SectionUI& section, juce::Rectangle<int> area)
{
    section.bounds = area;
    area.reduce (12, 10);
    auto header = area.removeFromTop (28);
    if (section.hasToggle)
        section.toggle.setBounds (header.removeFromRight (48).reduced (3, 3));
    section.titleLabel.setBounds (header);
    area.removeFromTop (12);

    const auto count = (int) section.knobs.size();
    if (count == 0)
        return;
    const auto columns = juce::jmin (3, count);
    const auto rows = (count + columns - 1) / columns;
    const auto cellWidth = area.getWidth() / columns;
    const auto cellHeight = area.getHeight() / rows;
    for (int index = 0; index < count; ++index)
    {
        auto cell = juce::Rectangle<int> (area.getX() + (index % columns) * cellWidth,
                                          area.getY() + (index / columns) * cellHeight,
                                          cellWidth, cellHeight);
        section.knobs[(size_t) index]->slider.setBounds (cell.reduced (4, 2));
    }
}
