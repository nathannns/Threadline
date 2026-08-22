#include "PluginEditor.h"
#include <BinaryData.h>
#include "DSP/GuitarSignalLevel.h"
#include "UI/ThreadlineFonts.h"

ThreadlineAudioProcessorEditor::ThreadlineAudioProcessorEditor (ThreadlineAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), tapTempoButton (p.apvts), pedalboard (p)
{
    setLookAndFeel (&buttonLookAndFeel);
    logoImage = juce::ImageCache::getFromMemory (BinaryData::threadline_logo_transparent_png,
                                                 BinaryData::threadline_logo_transparent_pngSize);
    logoComponent.setImage (logoImage, juce::RectanglePlacement (juce::RectanglePlacement::centred
                                                                  | juce::RectanglePlacement::onlyReduceInSize));
    logoComponent.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (logoComponent);

    // --- Preset bar: same control structure and vector icons as Rockalizer ---
    presetBox.setEditableText (true);
    presetBox.setJustificationType (juce::Justification::centred);
    presetBox.setColour (juce::ComboBox::backgroundColourId, ThreadlineColours::panelDark);
    presetBox.setColour (juce::ComboBox::textColourId, ThreadlineColours::textCream);
    presetBox.setColour (juce::ComboBox::outlineColourId, ThreadlineColours::cardBorder);
    presetBox.setLookAndFeel (&presetBoxLookAndFeel);
    addAndMakeVisible (presetBox);

    presetDropdownButton.setColour (juce::TextButton::buttonColourId, ThreadlineColours::panelDark);
    presetDropdownButton.setColour (juce::TextButton::textColourOffId, ThreadlineColours::textCream);
    presetDropdownButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetDropdownButton);

    for (auto* button : { &prevPresetButton, &nextPresetButton })
    {
        button->setColour (juce::TextButton::buttonColourId, ThreadlineColours::panelDark);
        button->setColour (juce::TextButton::textColourOffId, ThreadlineColours::textCream);
    }

    addPresetButton.setTooltip ("Add: save the current settings as a new preset");
    savePresetButton.setTooltip ("Save: overwrite the current preset");
    deletePresetButton.setTooltip ("Delete the current preset");
    presetBox.onChange = [this]
    {
        const auto files = processor.presetManager.getAllPresets();
        const auto index = presetBox.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (index, files.size()))
            processor.presetManager.loadPreset (files.getReference (index));
    };
    addPresetButton.onClick = [this]
    {
        presetBox.setSelectedId (0, juce::dontSendNotification);
        presetBox.setText ("NEW PRESET", juce::dontSendNotification);
        presetBox.grabKeyboardFocus();
    };
    prevPresetButton.onClick = [this] { processor.presetManager.loadPrevious(); refreshPresetList(); };
    nextPresetButton.onClick = [this] { processor.presetManager.loadNext(); refreshPresetList(); };
    savePresetButton.onClick = [this]
    {
        auto name = presetBox.getText().trim();
        if (name.isEmpty() || name == "NEW PRESET")
            name = processor.presetManager.generateUniqueName ("User Preset");
        processor.presetManager.savePreset (name);
        refreshPresetList();
    };
    deletePresetButton.onClick = [this]
    {
        processor.presetManager.deleteCurrentPreset();
        refreshPresetList();
    };
    addAndMakeVisible (addPresetButton);
    addAndMakeVisible (prevPresetButton);
    addAndMakeVisible (nextPresetButton);
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (deletePresetButton);
    refreshPresetList();

    addAndMakeVisible (powerButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "masterBypass", powerButton);

    // --- Rockalizer-style Options panel and gear behavior ---
    optionsGroup.setColour (juce::GroupComponent::outlineColourId, ThreadlineColours::cardBorder);
    optionsGroup.setColour (juce::GroupComponent::textColourId, ThreadlineColours::textDim);
    addAndMakeVisible (optionsGroup);
    optionsGroup.setVisible (false);
    optionsMenuButton.setTooltip ("Options");
    optionsMenuButton.onClick = [this]
    {
        optionsVisible = ! optionsVisible;
        optionsGroup.setVisible (optionsVisible);
        if (optionsVisible)
            optionsGroup.toFront (false);
        optionsMenuButton.toFront (false);
        powerButton.toFront (false);
    };
    addAndMakeVisible (optionsMenuButton);
    addMouseListener (this, true);

    for (auto* button : { &input1Button, &input2Button })
    {
        button->setColour (juce::ToggleButton::textColourId, ThreadlineColours::textCream);
        button->setColour (juce::ToggleButton::tickColourId, ThreadlineColours::accentBright);
        optionsGroup.addAndMakeVisible (*button);
    }
    input1Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "input1On", input1Button);
    input2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "input2On", input2Button);

    inputSourceBox.addItemList ({ "INSTRUMENT", "LINE" }, 1);
    inputSourceBox.setTooltip ("Instrument applies guitar-input calibration; Line uses unity calibration");
    inputSourceBox.setColour (juce::ComboBox::backgroundColourId, ThreadlineColours::panelDark);
    inputSourceBox.setColour (juce::ComboBox::textColourId, ThreadlineColours::textCream);
    inputSourceBox.setColour (juce::ComboBox::outlineColourId, ThreadlineColours::cardBorder);
    optionsGroup.addAndMakeVisible (inputSourceBox);
    inputSourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "inputSource", inputSourceBox);

    inputCalibrationLabel.setText ("FOCUSRITE +12.25 dBu  |  NO SIGNAL", juce::dontSendNotification);
    inputCalibrationLabel.setColour (juce::Label::textColourId, ThreadlineColours::textDim);
    inputCalibrationLabel.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    inputCalibrationLabel.setJustificationType (juce::Justification::centredLeft);
    inputCalibrationLabel.setTooltip ("Raw selected input before Threadline's Input knob; Instrument mode assumes Focusrite gain at minimum");
    optionsGroup.addAndMakeVisible (inputCalibrationLabel);

    ampQualityLabel.setText ("TRACKING OS", juce::dontSendNotification);
    ampQualityLabel.setColour (juce::Label::textColourId, ThreadlineColours::textDim);
    ampQualityLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold)); // matches TileKnob/TileCombo's own caption font
    ampQualityLabel.setJustificationType (juce::Justification::centredLeft);
    optionsGroup.addAndMakeVisible (ampQualityLabel);
    ampOversamplingBox.addItemList ({ "1X", "2X", "4X" }, 1);
    ampOversamplingBox.setTooltip ("Shared amp, drive, fuzz, and tape oversampling. 4X remains the default.");
    ampOversamplingBox.setColour (juce::ComboBox::backgroundColourId, ThreadlineColours::panelDark);
    ampOversamplingBox.setColour (juce::ComboBox::textColourId, ThreadlineColours::textCream);
    ampOversamplingBox.setColour (juce::ComboBox::outlineColourId, ThreadlineColours::cardBorder);
    optionsGroup.addAndMakeVisible (ampOversamplingBox);
    ampOversamplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "ampOversampling", ampOversamplingBox);

    renderQualityLabel.setText ("RENDER OS", juce::dontSendNotification);
    renderQualityLabel.setColour (juce::Label::textColourId, ThreadlineColours::textDim);
    renderQualityLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    renderQualityLabel.setJustificationType (juce::Justification::centredLeft);
    optionsGroup.addAndMakeVisible (renderQualityLabel);
    renderOversamplingBox.addItemList ({ "1X", "2X", "4X" }, 1);
    renderOversamplingBox.setTooltip ("Used automatically during a DAW offline bounce. 4X is the default.");
    renderOversamplingBox.setColour (juce::ComboBox::backgroundColourId, ThreadlineColours::panelDark);
    renderOversamplingBox.setColour (juce::ComboBox::textColourId, ThreadlineColours::textCream);
    renderOversamplingBox.setColour (juce::ComboBox::outlineColourId, ThreadlineColours::cardBorder);
    optionsGroup.addAndMakeVisible (renderOversamplingBox);
    renderOversamplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "renderOversampling", renderOversamplingBox);

    processingModeLabel.setText ("OUTPUT", juce::dontSendNotification);
    processingModeLabel.setColour (juce::Label::textColourId, ThreadlineColours::textDim);
    processingModeLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    processingModeLabel.setJustificationType (juce::Justification::centredLeft);
    optionsGroup.addAndMakeVisible (processingModeLabel);
    processingModeBox.addItemList ({ "STEREO", "MONO (CENTRED)" }, 1);
    processingModeBox.setTooltip ("Stereo is the default. Mono sums the processed signal and copies it to both left and right outputs.");
    processingModeBox.setColour (juce::ComboBox::backgroundColourId, ThreadlineColours::panelDark);
    processingModeBox.setColour (juce::ComboBox::textColourId, ThreadlineColours::textCream);
    processingModeBox.setColour (juce::ComboBox::outlineColourId, ThreadlineColours::cardBorder);
    optionsGroup.addAndMakeVisible (processingModeBox);
    processingModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "processingMode", processingModeBox);

    tapTempoButton.setColour (juce::TextButton::buttonColourId, ThreadlineColours::panelDark);
    tapTempoButton.setColour (juce::TextButton::textColourOffId, ThreadlineColours::textCream);
    addAndMakeVisible (tapTempoButton);
    tapTempoBpmLabel.setColour (juce::Label::textColourId, ThreadlineColours::textCream);
    tapTempoBpmLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (tapTempoBpmLabel);

    // --- Pedalboard ---
    addAndMakeVisible (pedalboard);

    // --- Pinned Input / Gate / Output bottom bar ---
    inputGainKnob = makeTileKnob (*this, processor.apvts, "inputGain", "Input");
    outputGainKnob = makeTileKnob (*this, processor.apvts, "outputGain", "Output");
    gateKnob = makeTileKnob (*this, processor.apvts, "gateAmount", "Amount");
    gateToggle = makeTileToggle (*this, processor.apvts, "gateOn", "On");
    gateBarLabel.setText ("Gate", juce::dontSendNotification);
    gateBarLabel.setFont (juce::FontOptions (17.0f, juce::Font::bold)); // matches PedalTileComponent's own nameLabel font
    gateBarLabel.setColour (juce::Label::textColourId, ThreadlineColours::textCream);
    gateBarLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (gateBarLabel);
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    // 1200 was sized against the pedalboard's original (smaller) tile
    // dimensions -- after this session's 3:4 ratio + 1.25x scaling pass,
    // that width only fit ~3.5 tiles before the strip needed scrolling.
    // 1600 shows roughly 5 at a glance instead, a better first impression
    // of "here's your board" without immediately reaching for the
    // scrollbar.
    setSize (1600, 780);
    setResizable (true, true);
    setResizeLimits (760, 700, 3000, 1200);

    startTimerHz (30);
}

ThreadlineAudioProcessorEditor::~ThreadlineAudioProcessorEditor()
{
    presetBox.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void ThreadlineAudioProcessorEditor::refreshPresetList()
{
    const auto files = processor.presetManager.getAllPresets();
    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("USER PRESETS");
    int selectedId = 0;
    for (int i = 0; i < files.size(); ++i)
    {
        const auto name = files.getReference (i).getFileNameWithoutExtension();
        presetBox.addItem (name, i + 1);
        if (name == processor.presetManager.getCurrentPresetName())
            selectedId = i + 1;
    }
    if (selectedId > 0)
        presetBox.setSelectedId (selectedId, juce::dontSendNotification);
    else
        presetBox.setText (processor.presetManager.getCurrentPresetName(), juce::dontSendNotification);
    deletePresetButton.setEnabled (selectedId > 0);
}

void ThreadlineAudioProcessorEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    const auto files = processor.presetManager.getAllPresets();
    menu.addSectionHeader ("PRESETS");
    for (int index = 0; index < files.size(); ++index)
    {
        const auto name = files.getReference (index).getFileNameWithoutExtension();
        menu.addItem (index + 1, name, true,
                      name == processor.presetManager.getCurrentPresetName());
    }

    const auto below = presetBox.localPointToGlobal (juce::Point<int> { 0, presetBox.getHeight() });
    const auto target = juce::Rectangle<int> { below.x, below.y, presetBox.getWidth(), 1 };
    const auto options = juce::PopupMenu::Options()
        .withTargetScreenArea (target)
        .withMinimumWidth (presetBox.getWidth())
        .withMaximumNumColumns (1)
        .withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::downwards);

    menu.showMenuAsync (options,
        [safe = juce::Component::SafePointer<ThreadlineAudioProcessorEditor> (this)] (int result)
        {
            if (safe == nullptr || result <= 0)
                return;
            const auto presets = safe->processor.presetManager.getAllPresets();
            const auto index = result - 1;
            if (juce::isPositiveAndBelow (index, presets.size())
                && safe->processor.presetManager.loadPreset (presets.getReference (index)))
                safe->presetBox.setSelectedId (result, juce::dontSendNotification);
        });
}

void ThreadlineAudioProcessorEditor::paint (juce::Graphics& g)
{
    paintThreadlineBackground (g, getLocalBounds());
    paintCard (g, presetCardBounds, 8.0f);
    paintCard (g, bottomBarBounds, 8.0f);
}

void ThreadlineAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! optionsVisible)
        return;

    // Click-outside-to-close: registered with addMouseListener(this, true) so
    // every child's mouseDown is also delivered here. Ignore clicks that
    // originate on the panel itself or its own gear toggle (which already
    // handles its own open/close).
    auto* originator = e.eventComponent;
    if (originator == &optionsGroup || optionsGroup.isParentOf (originator))
        return;
    if (originator == &optionsMenuButton)
        return;

    optionsVisible = false;
    optionsGroup.setVisible (false);
}

void ThreadlineAudioProcessorEditor::timerCallback()
{
    inputMeter.setLevel (processor.getInputLevel());
    outputMeter.setLevel (processor.getOutputLevel());

    const auto latestRawPeak = processor.getRawInputLevel();
    if (latestRawPeak >= heldRawInputPeak)
    {
        heldRawInputPeak = latestRawPeak;
        rawInputPeakHoldFrames = 30; // one-second peak hold at the 30Hz UI timer
    }
    else if (rawInputPeakHoldFrames > 0)
    {
        --rawInputPeakHoldFrames;
    }
    else
    {
        heldRawInputPeak *= 0.90f;
    }
    const auto rawPeak = heldRawInputPeak;
    if (rawPeak < 0.001f)
    {
        inputCalibrationLabel.setText ("FOCUSRITE +12.25 dBu  |  NO SIGNAL", juce::dontSendNotification);
        inputCalibrationLabel.setColour (juce::Label::textColourId, ThreadlineColours::textDim);
    }
    else
    {
        const auto dbfs = juce::Decibels::gainToDecibels (rawPeak, -100.0f);
        const auto peakVolts = rawPeak * GuitarSignalLevel::voltsPerDigitalUnit;
        const auto status = dbfs > -1.0f ? "CLIP RISK" : (dbfs > -3.0f ? "HOT" : "SAFE");
        const auto sourceIsInstrument = processor.apvts.getRawParameterValue ("inputSource")->load() < 0.5f;
        inputCalibrationLabel.setText (
            (sourceIsInstrument ? "FOCUSRITE +12.25 dBu" : "LINE")
              + juce::String ("  |  ") + juce::String (peakVolts, 3) + " Vpk  |  "
              + juce::String (dbfs, 1) + " dBFS  |  " + status,
            juce::dontSendNotification);
        inputCalibrationLabel.setColour (juce::Label::textColourId,
            dbfs > -3.0f ? ThreadlineColours::accentBright : ThreadlineColours::textCream);
    }

    // Keeps the readout correct even when tapTempoBpm changes some way
    // other than tapping the button (preset load, host automation).
    const auto bpm = processor.apvts.getRawParameterValue ("tapTempoBpm")->load();
    tapTempoBpmLabel.setText (juce::String (bpm, 0), juce::dontSendNotification);
}

void ThreadlineAudioProcessorEditor::resized()
{
    // Header/options/bottom-bar chrome scaled by the same 1.25x the
    // pedalboard tiles got -- previously left at its original size, which
    // made it look mismatched (too small/dense) against the bigger tiles.
    constexpr int headerHeight = 120;
    auto header = getLocalBounds().removeFromTop (headerHeight);

    logoComponent.setBounds (header.removeFromLeft (288).reduced (13));
    logoComponent.toFront (false);

    powerButton.setBounds (header.removeFromRight (85).reduced (8, 25));
    optionsMenuButton.setBounds (header.removeFromRight (68).reduced (8, 25));
    // Sits between the preset bar and the power button -- a physical-pedal-
    // style control worth reaching for often, not tucked away in Options.
    auto tapTempoArea = header.removeFromRight (110).reduced (8, 30);
    tapTempoButton.setBounds (tapTempoArea.removeFromLeft (58));
    tapTempoArea.removeFromLeft (6);
    tapTempoBpmLabel.setBounds (tapTempoArea);

    presetCardBounds = header.withSizeKeepingCentre (juce::jmin (663, header.getWidth() - 25), 70)
                             .withY (header.getY() + (headerHeight - 70) / 2);
    auto presetArea = presetCardBounds.reduced (13, 13);
    prevPresetButton.setBounds (presetArea.removeFromLeft (45));
    presetArea.removeFromLeft (4);
    nextPresetButton.setBounds (presetArea.removeFromLeft (45));
    presetArea.removeFromLeft (8);
    // Reserves exactly the trailing fixed-width controls' own footprint --
    // gap8 + dropdown43 + gap8 + (addPreset45 + gap5 + save45 + gap5 +
    // delete45) = 204 -- so presetBox always gets whatever's left over.
    presetBox.setBounds (presetArea.removeFromLeft (juce::jmax (100, presetArea.getWidth() - 204)));
    presetArea.removeFromLeft (8);
    presetDropdownButton.setBounds (presetArea.removeFromLeft (43));
    presetDropdownButton.toFront (false);
    presetArea.removeFromLeft (8);
    addPresetButton.setBounds (presetArea.removeFromLeft (45));
    presetArea.removeFromLeft (5);
    savePresetButton.setBounds (presetArea.removeFromLeft (45));
    presetArea.removeFromLeft (5);
    deletePresetButton.setBounds (presetArea.removeFromLeft (45));

    // Floating options panel, anchored under the gear button.
    optionsGroup.setBounds (optionsMenuButton.getRight() - 390, headerHeight, 390, 320);
    input1Button.setBounds (15, 38, 90, 40);
    input2Button.setBounds (110, 38, 90, 40);
    inputSourceBox.setBounds (220, 38, 153, 40);
    inputCalibrationLabel.setBounds (18, 80, 355, 30);
    ampQualityLabel.setBounds (18, 122, 178, 35);
    ampOversamplingBox.setBounds (220, 119, 153, 40);
    renderQualityLabel.setBounds (18, 174, 178, 35);
    renderOversamplingBox.setBounds (220, 171, 153, 40);
    processingModeLabel.setBounds (18, 226, 178, 35);
    processingModeBox.setBounds (220, 223, 153, 40);

    // Rockalizer's compact rack-footer geometry: a 108px faceplate with a
    // 12px gap to the pedal cards. Only Threadline's own Gate/Input/Output
    // controls are present -- Tremolo/Doubler and their DSP are not copied.
    constexpr int bottomBarHeight = 126;
    bottomBarBounds = { 20, getHeight() - 114, getWidth() - 40, 108 };

    // Rockalizer lays its footer out against a 1144px usable reference area.
    // Scaling the same offsets keeps the sparse rack composition intact at
    // every resizable width instead of redistributing the controls evenly.
    constexpr float referenceBarWidth = 1144.0f;
    const auto scaleX = static_cast<float> (bottomBarBounds.getWidth()) / referenceBarWidth;
    const auto scaleRect = [this, scaleX] (float x, float y, float w, float h)
    {
        return juce::Rectangle<int> (
            bottomBarBounds.getX() + juce::roundToInt (x * scaleX),
            bottomBarBounds.getY() + juce::roundToInt (y),
            juce::roundToInt (w * scaleX), juce::roundToInt (h));
    };

    // Gate occupies Rockalizer's left utility slot. Its compact header uses
    // the same row as the On switch; the amount value remains in the knob's
    // normal text box, so a second caption row is unnecessary.
    auto gateHeader = scaleRect (22.0f, 2.0f, 110.0f, 20.0f);
    gateBarLabel.setBounds (gateHeader.removeFromLeft (juce::roundToInt (56.0f * scaleX)));
    gateToggle->button.setBounds (gateHeader.removeFromRight (juce::roundToInt (50.0f * scaleX)));
    gateKnob->label.setBounds (0, 0, 0, 0);
    gateKnob->slider.setBounds (scaleRect (22.0f, 24.0f, 110.0f, 82.0f));

    inputGainKnob->label.setBounds (scaleRect (277.0f, 2.0f, 110.0f, 20.0f));
    inputGainKnob->slider.setBounds (scaleRect (277.0f, 24.0f, 110.0f, 82.0f));
    inputMeter.setBounds (scaleRect (147.0f, 48.0f, 115.0f, 10.0f));

    outputGainKnob->label.setBounds (scaleRect (1032.0f, 2.0f, 110.0f, 20.0f));
    outputGainKnob->slider.setBounds (scaleRect (1032.0f, 24.0f, 110.0f, 82.0f));
    outputMeter.setBounds (scaleRect (909.0f, 48.0f, 115.0f, 10.0f));

    pedalboard.setBounds (getLocalBounds().withTrimmedTop (headerHeight).withTrimmedBottom (bottomBarHeight));
}
