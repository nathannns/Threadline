#pragma once

#include "PluginProcessor.h"
#include "UI/SectionBuilder.h"
#include "UI/Page1Component.h"
#include "UI/Page2Component.h"
#include "UI/Page3Component.h"
#include "UI/Page4Component.h"
#include <JuceHeader.h>

// Top-right power/bypass ring — lit amber when the plugin is active, dims
// to grey when masterBypass is engaged.
class PowerButton : public juce::ToggleButton
{
public:
    PowerButton() { setClickingTogglesState (true); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (4.0f);
        auto bypassed = getToggleState();
        auto colour = bypassed ? juce::Colour (0xff6a5a4e) : ThreadlineColours::accentBright;

        g.setColour (colour);
        g.drawEllipse (bounds, 2.5f);

        auto glyphBounds = bounds.reduced (bounds.getWidth() * 0.22f);
        juce::Path glyph;
        glyph.addCentredArc (glyphBounds.getCentreX(), glyphBounds.getCentreY(),
                              glyphBounds.getWidth() * 0.5f, glyphBounds.getHeight() * 0.5f, 0.0f,
                              juce::degreesToRadians (35.0f), juce::degreesToRadians (325.0f), true);
        g.strokePath (glyph, juce::PathStrokeType (2.5f));
        g.drawLine (glyphBounds.getCentreX(), glyphBounds.getY() - 2.0f,
                    glyphBounds.getCentreX(), glyphBounds.getCentreY(), 2.5f);
    }
};

// Copied from Rockalizer's preset header: compact vector icons remain crisp at
// every editor scale and avoid platform-dependent emoji rendering.
class PresetIconButton final : public juce::Button
{
public:
    enum class Icon { add, save, remove };
    PresetIconButton (const juce::String& name, Icon iconToDraw)
        : juce::Button (name), icon (iconToDraw) {}

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto colour = isEnabled() ? ThreadlineColours::textCream : juce::Colour (0xff555b60);
        if (highlighted && isEnabled()) colour = ThreadlineColours::accentBright;
        if (down) colour = colour.darker (0.18f);
        const auto b = getLocalBounds().toFloat().reduced (10.0f, 7.0f);
        g.setColour (colour);
        if (icon == Icon::add)
        {
            g.drawLine (b.getCentreX(), b.getY(), b.getCentreX(), b.getBottom(), 2.4f);
            g.drawLine (b.getX(), b.getCentreY(), b.getRight(), b.getCentreY(), 2.4f);
        }
        else if (icon == Icon::save)
        {
            g.drawRoundedRectangle (b, 2.0f, 2.0f);
            g.fillRect (b.getX() + b.getWidth() * 0.20f, b.getY(), b.getWidth() * 0.52f, b.getHeight() * 0.34f);
            g.setColour (ThreadlineColours::panelDark);
            g.fillRect (b.getX() + b.getWidth() * 0.30f, b.getY() + 2.0f,
                        b.getWidth() * 0.28f, b.getHeight() * 0.19f);
            g.setColour (colour);
            g.drawRoundedRectangle (b.reduced (b.getWidth() * 0.20f, b.getHeight() * 0.16f)
                                       .withTrimmedTop (b.getHeight() * 0.28f), 1.5f, 1.7f);
        }
        else
        {
            auto can = b.reduced (b.getWidth() * 0.18f, b.getHeight() * 0.12f);
            g.drawRoundedRectangle (can.withTrimmedTop (can.getHeight() * 0.22f), 1.5f, 2.0f);
            g.drawLine (can.getX() - 2.0f, can.getY() + can.getHeight() * 0.18f,
                        can.getRight() + 2.0f, can.getY() + can.getHeight() * 0.18f, 2.0f);
            g.drawLine (can.getCentreX() - 4.0f, can.getY(), can.getCentreX() + 4.0f, can.getY(), 2.0f);
        }
    }

private:
    Icon icon;
};

class GearButton final : public juce::Button
{
public:
    GearButton() : juce::Button ("Options") {}
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (9.0f);
        const auto centre = bounds.getCentre();
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.27f;
        const auto colour = down ? ThreadlineColours::accentBright
                                 : (highlighted ? ThreadlineColours::textCream : ThreadlineColours::textDim);
        g.setColour (colour);
        for (int tooth = 0; tooth < 8; ++tooth)
        {
            const auto angle = juce::MathConstants<float>::twoPi * static_cast<float> (tooth) / 8.0f;
            const auto direction = juce::Point<float> (std::cos (angle), std::sin (angle));
            g.drawLine ({ centre + direction * radius, centre + direction * (radius + 6.0f) }, 3.0f);
        }
        g.drawEllipse (bounds.withSizeKeepingCentre (radius * 2.2f, radius * 2.2f), 3.0f);
        g.fillEllipse (bounds.withSizeKeepingCentre (radius * 0.75f, radius * 0.75f));
    }
};

class OptionsPanel final : public juce::GroupComponent
{
public:
    OptionsPanel() : juce::GroupComponent ("optionsPanel", "OPTIONS") {}
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff171311).withAlpha (0.985f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 9.0f);
        juce::GroupComponent::paint (g);
    }
};

// Compact icon-only page navigation. The accessible label remains available
// as the button name and tooltip without adding another visual wordmark.
class TabPill : public juce::Button
{
public:
    enum class Icon { Dirt, Amp, Wet, EQ };

    TabPill (const juce::String& labelText, Icon iconToShow)
        : juce::Button (labelText), icon (iconToShow)
    {
        setTooltip (labelText);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool /*down*/) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (2.0f);
        const auto selected = getToggleState();

        if (selected)
        {
            g.setColour (ThreadlineColours::accent.withAlpha (0.30f));
            g.fillRoundedRectangle (bounds, 8.0f);
            g.setColour (ThreadlineColours::accentBright.withAlpha (0.85f));
            g.drawRoundedRectangle (bounds, 8.0f, 1.4f);
        }
        else if (highlighted)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (bounds, 8.0f);
        }

        auto glyph = bounds.withSizeKeepingCentre (bounds.getHeight(), bounds.getHeight())
                           .reduced (bounds.getHeight() * 0.25f);
        g.setColour (selected ? juce::Colours::white.withAlpha (0.95f) : ThreadlineColours::textDim);

        switch (icon)
        {
            case Icon::Dirt:
            {
                // Stomp-box: rounded rect body + a single knob dot.
                juce::Rectangle<float> box (glyph.getX(), glyph.getCentreY() - glyph.getHeight() * 0.3f,
                                             glyph.getWidth(), glyph.getHeight() * 0.6f);
                g.drawRoundedRectangle (box, 2.5f, 1.8f);
                g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (box.getCentre().translated (0, -box.getHeight() * 0.1f)));
                break;
            }
            case Icon::Amp:
            {
                // Combo cabinet: rect body with two speaker circles.
                g.drawRoundedRectangle (glyph, 2.0f, 1.8f);
                auto speakerR = glyph.getWidth() * 0.18f;
                g.drawEllipse (juce::Rectangle<float> (speakerR * 2, speakerR * 2)
                                    .withCentre ({ glyph.getX() + glyph.getWidth() * 0.3f, glyph.getCentreY() }), 1.6f);
                g.drawEllipse (juce::Rectangle<float> (speakerR * 2, speakerR * 2)
                                    .withCentre ({ glyph.getX() + glyph.getWidth() * 0.7f, glyph.getCentreY() }), 1.6f);
                break;
            }
            case Icon::Wet:
            {
                // Modulation waveform.
                juce::Path wave;
                wave.startNewSubPath (glyph.getX(), glyph.getCentreY());
                wave.cubicTo (glyph.getX() + glyph.getWidth() * 0.25f, glyph.getY(),
                               glyph.getX() + glyph.getWidth() * 0.25f, glyph.getY(),
                               glyph.getCentreX(), glyph.getCentreY());
                wave.cubicTo (glyph.getX() + glyph.getWidth() * 0.75f, glyph.getBottom(),
                               glyph.getX() + glyph.getWidth() * 0.75f, glyph.getBottom(),
                               glyph.getRight(), glyph.getCentreY());
                g.strokePath (wave, juce::PathStrokeType (1.8f));
                break;
            }
            case Icon::EQ:
            {
                // Graphic-EQ bars of varying height.
                constexpr float heights[5] { 0.5f, 0.85f, 0.35f, 1.0f, 0.6f };
                const auto barWidth = glyph.getWidth() / 5.0f * 0.6f;
                for (int i = 0; i < 5; ++i)
                {
                    auto barHeight = glyph.getHeight() * heights[i];
                    juce::Rectangle<float> bar (barWidth, barHeight);
                    bar.setX (glyph.getX() + glyph.getWidth() * ((float) i + 0.5f) / 5.0f - barWidth * 0.5f);
                    bar.setY (glyph.getBottom() - barHeight);
                    g.fillRoundedRectangle (bar, barWidth * 0.3f);
                }
                break;
            }
        }
    }

private:
    Icon icon;
};

class ThreadlineAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ThreadlineAudioProcessorEditor (ThreadlineAudioProcessor&);
    ~ThreadlineAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void switchToPage (int pageIndex);
    void showPresetMenu();
    void refreshPresetList();

    ThreadlineAudioProcessor& processor;

    juce::Image logoImage;
    juce::Rectangle<int> presetCardBounds;

    // --- Preset bar: Add (new) / Save (overwrite current) / Delete, plus prev/next ---
    juce::ComboBox presetBox;
    juce::TextButton prevPresetButton { "<" }, nextPresetButton { ">" };
    juce::TextButton presetDropdownButton { "v" };
    PresetIconButton addPresetButton { "New preset", PresetIconButton::Icon::add };
    PresetIconButton savePresetButton { "Save preset", PresetIconButton::Icon::save };
    PresetIconButton deletePresetButton { "Delete preset", PresetIconButton::Icon::remove };
    PowerButton powerButton;
    GearButton optionsMenuButton;
    OptionsPanel optionsGroup;
    bool optionsVisible = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // --- Tab strip: Dirt (Comp/Klon/TS9), Amp (Amp/Cab), Wet (Trem/Chorus/Delay/Reverb), EQ (9-band + HPF/LPF) ---
    std::array<TabPill, 4> tabPills {
        TabPill ("Dirt", TabPill::Icon::Dirt), TabPill ("Amp", TabPill::Icon::Amp),
        TabPill ("Wet FX", TabPill::Icon::Wet), TabPill ("EQ", TabPill::Icon::EQ)
    };
    int currentPage = 0;

    // --- Persistent side rails: Gate+Input stacked on the left, Output on
    // the right, flanking the page content for every tab (not just Amp).
    SectionUI gateSection;
    juce::Label inputLabel, outputLabel;
    PhotoKnob inputGainKnob, outputGainKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputGainAttachment, outputGainAttachment;
    LevelMeterBar inputMeter, outputMeter;
    juce::Rectangle<int> utilityFrameBounds, gateCardBounds, inputCardBounds, outputCardBounds;

    // Guitar/Line input calibration (see PluginProcessor::processBlock).
    juce::ComboBox inputSourceBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> inputSourceAttachment;
    juce::ToggleButton input1Button { "INPUT 1" }, input2Button { "INPUT 2" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> input1Attachment, input2Attachment;
    juce::Label ampQualityLabel;
    juce::ComboBox ampOversamplingBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ampOversamplingAttachment;

    // --- Pages ---
    Page1Component page1;
    Page2Component page2;
    Page3Component page3;
    Page4Component page4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThreadlineAudioProcessorEditor)
};
