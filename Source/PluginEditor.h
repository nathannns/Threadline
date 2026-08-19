#pragma once

#include "PluginProcessor.h"
#include "UI/SectionBuilder.h"
#include "UI/Pedalboard/PedalboardComponent.h"
#include <JuceHeader.h>

// Top-right power/bypass ring — lit amber when the plugin is active, dims
// to grey when masterBypass is engaged.
class PowerButton : public juce::ToggleButton
{
public:
    PowerButton()
    {
        setClickingTogglesState (true);
        setWantsKeyboardFocus (true);
        setTitle ("Global bypass");
        setHelpText ("Bypass or enable all Threadline processing");
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (5.0f);
        if (isDown()) bounds.translate (0.0f, 1.0f);
        auto bypassed = getToggleState();
        auto colour = bypassed ? juce::Colour (0xff6a5a4e) : ThreadlineColours::accentBright;

        g.setColour (colour);
        g.drawEllipse (bounds, 3.1f);

        auto glyphBounds = bounds.reduced (bounds.getWidth() * 0.22f);
        juce::Path glyph;
        glyph.addCentredArc (glyphBounds.getCentreX(), glyphBounds.getCentreY(),
                              glyphBounds.getWidth() * 0.5f, glyphBounds.getHeight() * 0.5f, 0.0f,
                              juce::degreesToRadians (35.0f), juce::degreesToRadians (325.0f), true);
        g.strokePath (glyph, juce::PathStrokeType (3.1f));
        g.drawLine (glyphBounds.getCentreX(), glyphBounds.getY() - 2.5f,
                    glyphBounds.getCentreX(), glyphBounds.getCentreY(), 3.1f);
        if (hasKeyboardFocus (true))
            g.drawEllipse (bounds.expanded (2.5f), 1.75f);
    }
};

// Sits between the preset bar and the options/power cluster. Silences the
// input before it reaches any module -- for silent patch changes or
// checking dry level without the whole plugin's bypass state changing.
// Lights red when engaged (muted), matching the usual mixing-console
// convention, rather than the amber "active" colour every other toggle
// here uses -- muted should read as an alarm, not as "on".
class MuteButton : public juce::ToggleButton
{
public:
    MuteButton()
    {
        setClickingTogglesState (true);
        setWantsKeyboardFocus (true);
        setTitle ("Mute input");
        setHelpText ("Silence the input before it reaches any effect");
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (5.0f);
        if (isDown()) bounds.translate (0.0f, 1.0f);
        const auto muted = getToggleState();
        const auto colour = muted ? juce::Colour (0xffd8503f) : juce::Colour (0xff6a5a4e);
        g.setColour (colour);
        g.drawEllipse (bounds, 3.1f);

        auto glyph = bounds.reduced (bounds.getWidth() * 0.24f);
        juce::Path speaker;
        const auto boxW = glyph.getWidth() * 0.42f;
        speaker.addRectangle (glyph.getX(), glyph.getCentreY() - glyph.getHeight() * 0.18f,
                              boxW, glyph.getHeight() * 0.36f);
        speaker.startNewSubPath (glyph.getX() + boxW, glyph.getCentreY() - glyph.getHeight() * 0.18f);
        speaker.lineTo (glyph.getRight(), glyph.getY());
        speaker.lineTo (glyph.getRight(), glyph.getBottom());
        speaker.lineTo (glyph.getX() + boxW, glyph.getCentreY() + glyph.getHeight() * 0.18f);
        speaker.closeSubPath();
        g.strokePath (speaker, juce::PathStrokeType (2.5f));
        if (muted)
            g.drawLine (glyph.getX() - 2.5f, glyph.getBottom() + 2.5f,
                        glyph.getRight() + 2.5f, glyph.getY() - 2.5f, 3.0f);

        if (hasKeyboardFocus (true))
            g.drawEllipse (bounds.expanded (2.5f), 1.75f);
    }
};

// Copied from Rockalizer's preset header: compact vector icons remain crisp at
// every editor scale and avoid platform-dependent emoji rendering.
class PresetIconButton final : public juce::Button
{
public:
    enum class Icon { add, save, remove };
    PresetIconButton (const juce::String& name, Icon iconToDraw)
        : juce::Button (name), icon (iconToDraw)
    {
        setWantsKeyboardFocus (true);
        setTitle (name);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto colour = isEnabled() ? ThreadlineColours::textCream : juce::Colour (0xff555b60);
        if (highlighted && isEnabled()) colour = ThreadlineColours::accentBright;
        if (down) colour = colour.darker (0.18f);
        const auto b = getLocalBounds().toFloat().reduced (12.5f, 8.75f);
        g.setColour (colour);
        if (icon == Icon::add)
        {
            g.drawLine (b.getCentreX(), b.getY(), b.getCentreX(), b.getBottom(), 3.0f);
            g.drawLine (b.getX(), b.getCentreY(), b.getRight(), b.getCentreY(), 3.0f);
        }
        else if (icon == Icon::save)
        {
            g.drawRoundedRectangle (b, 2.5f, 2.5f);
            g.fillRect (b.getX() + b.getWidth() * 0.20f, b.getY(), b.getWidth() * 0.52f, b.getHeight() * 0.34f);
            g.setColour (ThreadlineColours::panelDark);
            g.fillRect (b.getX() + b.getWidth() * 0.30f, b.getY() + 2.5f,
                        b.getWidth() * 0.28f, b.getHeight() * 0.19f);
            g.setColour (colour);
            g.drawRoundedRectangle (b.reduced (b.getWidth() * 0.20f, b.getHeight() * 0.16f)
                                       .withTrimmedTop (b.getHeight() * 0.28f), 1.9f, 2.1f);
        }
        else
        {
            auto can = b.reduced (b.getWidth() * 0.18f, b.getHeight() * 0.12f);
            g.drawRoundedRectangle (can.withTrimmedTop (can.getHeight() * 0.22f), 1.9f, 2.5f);
            g.drawLine (can.getX() - 2.5f, can.getY() + can.getHeight() * 0.18f,
                        can.getRight() + 2.5f, can.getY() + can.getHeight() * 0.18f, 2.5f);
            g.drawLine (can.getCentreX() - 5.0f, can.getY(), can.getCentreX() + 5.0f, can.getY(), 2.5f);
        }
        if (hasKeyboardFocus (true))
        {
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.5f), 7.5f, 1.6f);
        }
    }

private:
    Icon icon;
};

class GearButton final : public juce::Button
{
public:
    GearButton() : juce::Button ("Options")
    {
        setWantsKeyboardFocus (true);
        setTooltip ("Options");
    }
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (11.0f);
        const auto centre = bounds.getCentre();
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.27f;
        const auto colour = down ? ThreadlineColours::accentBright
                                 : (highlighted ? ThreadlineColours::textCream : ThreadlineColours::textDim);
        g.setColour (colour);
        for (int tooth = 0; tooth < 8; ++tooth)
        {
            const auto angle = juce::MathConstants<float>::twoPi * static_cast<float> (tooth) / 8.0f;
            const auto direction = juce::Point<float> (std::cos (angle), std::sin (angle));
            g.drawLine ({ centre + direction * radius, centre + direction * (radius + 7.5f) }, 3.75f);
        }
        g.drawEllipse (bounds.withSizeKeepingCentre (radius * 2.2f, radius * 2.2f), 3.75f);
        g.fillEllipse (bounds.withSizeKeepingCentre (radius * 0.75f, radius * 0.75f));
        if (hasKeyboardFocus (true))
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.5f), 8.75f, 1.6f);
    }
};

// Options-panel tap-tempo control: click in rhythm to set the global
// "tapTempoBpm" param Plexer/Copier's own Sync toggle follows (see
// TapTempo.h). Averages the last few tap intervals; a gap over 2 seconds
// starts a fresh tap sequence instead of dragging a stale average forward
// (matching how real tap-tempo pedals reset after a pause).
class TapTempoButton : public juce::TextButton, private juce::Timer
{
public:
    explicit TapTempoButton (juce::AudioProcessorValueTreeState& apvtsIn)
        : juce::TextButton ("TAP"), apvts (apvtsIn)
    {
        setTooltip ("Tap in time to set the tempo Plexer/Copier's Sync mode follows");
        onClick = [this] { registerTap(); };
    }

private:
    void registerTap()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (lastTapMs > 0.0 && (now - lastTapMs) < 2000.0)
        {
            intervalsMs.add (now - lastTapMs);
            if (intervalsMs.size() > 8)
                intervalsMs.remove (0);
        }
        else
        {
            intervalsMs.clear();
        }
        lastTapMs = now;

        if (! intervalsMs.isEmpty())
        {
            double sum = 0.0;
            for (auto v : intervalsMs)
                sum += v;
            const auto bpm = juce::jlimit (40.0, 300.0, 60000.0 / (sum / intervalsMs.size()));
            if (auto* parameter = apvts.getParameter ("tapTempoBpm"))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) bpm));
        }

        // Brief flash so a tap reads as registered even before the
        // computed tempo settles.
        setColour (juce::TextButton::buttonColourId, ThreadlineColours::accentBright);
        startTimer (110);
    }

    void timerCallback() override
    {
        stopTimer();
        removeColour (juce::TextButton::buttonColourId);
    }

    juce::AudioProcessorValueTreeState& apvts;
    juce::Array<double> intervalsMs;
    double lastTapMs = 0.0;
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

class ThreadlineAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ThreadlineAudioProcessorEditor (ThreadlineAudioProcessor&);
    ~ThreadlineAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPresetMenu();
    void refreshPresetList();

    ThreadlineAudioProcessor& processor;
    // Declared before all controls so it outlives every component using it.
    ThreadlineButtonLookAndFeel buttonLookAndFeel;

    juce::Image logoImage;
    juce::ImageComponent logoComponent;
    juce::Rectangle<int> presetCardBounds;

    // --- Preset bar: Add (new) / Save (overwrite current) / Delete, plus prev/next ---
    juce::ComboBox presetBox;
    juce::TextButton prevPresetButton { "<" }, nextPresetButton { ">" };
    juce::TextButton presetDropdownButton { "v" };
    PresetIconButton addPresetButton { "New preset", PresetIconButton::Icon::add };
    PresetIconButton savePresetButton { "Save preset", PresetIconButton::Icon::save };
    PresetIconButton deletePresetButton { "Delete preset", PresetIconButton::Icon::remove };
    MuteButton muteButton;
    PowerButton powerButton;
    GearButton optionsMenuButton;
    OptionsPanel optionsGroup;
    bool optionsVisible = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment, muteAttachment;

    // Guitar/Line input calibration and amp oversampling quality -- global
    // engine settings, not per-pedal, so they live in the Options panel
    // rather than as a pedalboard tile.
    juce::ComboBox inputSourceBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> inputSourceAttachment;
    juce::ToggleButton input1Button { "INPUT 1" }, input2Button { "INPUT 2" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> input1Attachment, input2Attachment;
    juce::Label ampQualityLabel;
    juce::ComboBox ampOversamplingBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ampOversamplingAttachment;

    // Global tap tempo -- feeds Plexer/Copier's own Sync toggle (see
    // TapTempo.h). Lives in the Options panel alongside the other global
    // engine settings above, not as a pedalboard tile.
    juce::Label tapTempoLabel, tapTempoBpmLabel;
    TapTempoButton tapTempoButton;

    // --- The pedalboard: one flat, horizontally-scrolling strip of every
    // other active pedal's tile, replacing the old 4-tab UI entirely. ---
    PedalboardComponent pedalboard;

    // --- Pinned Input/Gate/Output: Input always first and Output always
    // last in the processing chain, Gate pinned right after Input (see
    // PedalboardComponent's middleOrder/fullOrderForProcessor) -- shown in
    // a persistent bottom bar, left-to-right Input > Gate > Output, rather
    // than as ordinary draggable strip tiles.
    std::unique_ptr<TileKnob> inputGainKnob, outputGainKnob, gateKnob;
    std::unique_ptr<TileToggle> gateToggle;
    juce::Label gateBarLabel;
    LevelMeterBar inputMeter, outputMeter;
    juce::Rectangle<int> bottomBarBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThreadlineAudioProcessorEditor)
};
