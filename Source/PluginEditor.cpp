#include "PluginEditor.h"
#include <BinaryData.h>
#include "UI/ThreadlineFonts.h"

ThreadlineAudioProcessorEditor::ThreadlineAudioProcessorEditor (ThreadlineAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), tapTempoButton (p.apvts), pedalboard (p)
{
    setLookAndFeel (&buttonLookAndFeel);
    logoImage = juce::ImageCache::getFromMemory (BinaryData::threadline_logo_png, BinaryData::threadline_logo_pngSize);
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

    addAndMakeVisible (muteButton);
    muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "inputMute", muteButton);

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

    ampQualityLabel.setText ("AMP OVERSAMPLING", juce::dontSendNotification);
    ampQualityLabel.setColour (juce::Label::textColourId, ThreadlineColours::textDim);
    ampQualityLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold)); // matches TileKnob/TileCombo's own caption font
    ampQualityLabel.setJustificationType (juce::Justification::centredLeft);
    optionsGroup.addAndMakeVisible (ampQualityLabel);
    ampOversamplingBox.addItemList ({ "OFF", "2X", "4X" }, 1);
    ampOversamplingBox.setTooltip ("Amp oversampling quality");
    ampOversamplingBox.setColour (juce::ComboBox::backgroundColourId, ThreadlineColours::panelDark);
    ampOversamplingBox.setColour (juce::ComboBox::textColourId, ThreadlineColours::textCream);
    ampOversamplingBox.setColour (juce::ComboBox::outlineColourId, ThreadlineColours::cardBorder);
    optionsGroup.addAndMakeVisible (ampOversamplingBox);
    ampOversamplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "ampOversampling", ampOversamplingBox);

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
    muteButton.setBounds (header.removeFromRight (68).reduced (8, 25));

    // Sits between the preset bar and the mute button -- a physical-pedal-
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
    optionsGroup.setBounds (optionsMenuButton.getRight() - 353, headerHeight, 353, 165);
    input1Button.setBounds (15, 38, 90, 40);
    input2Button.setBounds (110, 38, 90, 40);
    inputSourceBox.setBounds (205, 38, 133, 40);
    ampQualityLabel.setBounds (18, 93, 178, 35);
    ampOversamplingBox.setBounds (205, 90, 133, 40);

    constexpr int bottomBarHeight = 175;
    // `bottomBarBounds` (the member) stays the FULL bar rect -- it's reused
    // by paint() for the background card, so all the left/right carving
    // below happens on a local copy instead of mutating it in place.
    bottomBarBounds = getLocalBounds().removeFromBottom (bottomBarHeight).reduced (20, 15);
    auto barContent = bottomBarBounds;

    auto inputArea = barContent.removeFromLeft (138).reduced (8, 0);
    auto inputLabelArea = inputArea.removeFromTop (20);
    auto inputMeterArea = inputArea.removeFromBottom (13);
    inputArea.removeFromBottom (5);
    inputGainKnob->label.setBounds (inputLabelArea);
    inputMeter.setBounds (inputMeterArea);
    inputGainKnob->slider.setBounds (inputArea);

    auto outputArea = barContent.removeFromRight (138).reduced (8, 0);
    auto outputLabelArea = outputArea.removeFromTop (20);
    auto outputMeterArea = outputArea.removeFromBottom (13);
    outputArea.removeFromBottom (5);
    outputGainKnob->label.setBounds (outputLabelArea);
    outputMeter.setBounds (outputMeterArea);
    outputGainKnob->slider.setBounds (outputArea);

    // Gate: pinned right after Input in the chain, shown centred in
    // whatever's left between the Input and Output widgets.
    auto gateArea = barContent.withSizeKeepingCentre (138, barContent.getHeight()).reduced (8, 0);
    auto gateHeader = gateArea.removeFromTop (23);
    gateBarLabel.setBounds (gateHeader.removeFromLeft (55));
    gateToggle->button.setBounds (gateHeader.removeFromRight (50));
    gateArea.removeFromTop (3);
    gateKnob->label.setBounds (gateArea.removeFromTop (captionHeight));
    gateKnob->slider.setBounds (gateArea);

    pedalboard.setBounds (getLocalBounds().withTrimmedTop (headerHeight).withTrimmedBottom (bottomBarHeight));
}
