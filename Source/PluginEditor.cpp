#include "PluginEditor.h"
#include <BinaryData.h>
#include "UI/ThreadlineFonts.h"

ThreadlineAudioProcessorEditor::ThreadlineAudioProcessorEditor (ThreadlineAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), pedalboard (p)
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
    ampQualityLabel.setFont (juce::FontOptions (11.5f, juce::Font::bold));
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

    // --- Pedalboard ---
    addAndMakeVisible (pedalboard);

    // --- Pinned Input / Gate / Output bottom bar ---
    inputGainKnob = makeTileKnob (*this, processor.apvts, "inputGain", "Input");
    outputGainKnob = makeTileKnob (*this, processor.apvts, "outputGain", "Output");
    gateKnob = makeTileKnob (*this, processor.apvts, "gateAmount", "Amount");
    gateToggle = makeTileToggle (*this, processor.apvts, "gateOn", "On");
    gateBarLabel.setText ("Gate", juce::dontSendNotification);
    gateBarLabel.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    gateBarLabel.setColour (juce::Label::textColourId, ThreadlineColours::textCream);
    gateBarLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (gateBarLabel);
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    setSize (1200, 760);
    setResizable (true, true);
    setResizeLimits (760, 700, 3000, 1200);

    startTimerHz (30);
}

ThreadlineAudioProcessorEditor::~ThreadlineAudioProcessorEditor()
{
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

void ThreadlineAudioProcessorEditor::timerCallback()
{
    inputMeter.setLevel (processor.getInputLevel());
    outputMeter.setLevel (processor.getOutputLevel());
}

void ThreadlineAudioProcessorEditor::resized()
{
    constexpr int headerHeight = 96;
    auto header = getLocalBounds().removeFromTop (headerHeight);

    logoComponent.setBounds (header.removeFromLeft (230).reduced (10));
    logoComponent.toFront (false);

    powerButton.setBounds (header.removeFromRight (68).reduced (6, 20));
    optionsMenuButton.setBounds (header.removeFromRight (54).reduced (6, 20));
    muteButton.setBounds (header.removeFromRight (54).reduced (6, 20));

    presetCardBounds = header.withSizeKeepingCentre (juce::jmin (530, header.getWidth() - 20), 56)
                             .withY (header.getY() + (headerHeight - 56) / 2);
    auto presetArea = presetCardBounds.reduced (10, 10);
    prevPresetButton.setBounds (presetArea.removeFromLeft (36));
    presetArea.removeFromLeft (6);
    presetBox.setBounds (presetArea.removeFromLeft (juce::jmax (80, presetArea.getWidth() - 190)));
    presetArea.removeFromLeft (6);
    presetDropdownButton.setBounds (presetArea.removeFromLeft (34));
    presetDropdownButton.toFront (false);
    presetArea.removeFromLeft (6);
    nextPresetButton.setBounds (presetArea.removeFromLeft (36));
    presetArea.removeFromLeft (6);
    addPresetButton.setBounds (presetArea.removeFromLeft (36));
    presetArea.removeFromLeft (4);
    savePresetButton.setBounds (presetArea.removeFromLeft (36));
    presetArea.removeFromLeft (4);
    deletePresetButton.setBounds (presetArea.removeFromLeft (36));

    // Floating options panel, anchored under the gear button.
    optionsGroup.setBounds (optionsMenuButton.getRight() - 282, headerHeight, 282, 132);
    input1Button.setBounds (12, 30, 72, 32);
    input2Button.setBounds (88, 30, 72, 32);
    inputSourceBox.setBounds (164, 30, 106, 32);
    ampQualityLabel.setBounds (14, 74, 142, 28);
    ampOversamplingBox.setBounds (164, 72, 106, 32);

    constexpr int bottomBarHeight = 140;
    // `bottomBarBounds` (the member) stays the FULL bar rect -- it's reused
    // by paint() for the background card, so all the left/right carving
    // below happens on a local copy instead of mutating it in place.
    bottomBarBounds = getLocalBounds().removeFromBottom (bottomBarHeight).reduced (16, 12);
    auto barContent = bottomBarBounds;

    auto inputArea = barContent.removeFromLeft (110).reduced (6, 0);
    auto inputLabelArea = inputArea.removeFromTop (16);
    auto inputMeterArea = inputArea.removeFromBottom (10);
    inputArea.removeFromBottom (4);
    inputGainKnob->label.setBounds (inputLabelArea);
    inputMeter.setBounds (inputMeterArea);
    inputGainKnob->slider.setBounds (inputArea);

    auto outputArea = barContent.removeFromRight (110).reduced (6, 0);
    auto outputLabelArea = outputArea.removeFromTop (16);
    auto outputMeterArea = outputArea.removeFromBottom (10);
    outputArea.removeFromBottom (4);
    outputGainKnob->label.setBounds (outputLabelArea);
    outputMeter.setBounds (outputMeterArea);
    outputGainKnob->slider.setBounds (outputArea);

    // Gate: pinned right after Input in the chain, shown centred in
    // whatever's left between the Input and Output widgets.
    auto gateArea = barContent.withSizeKeepingCentre (110, barContent.getHeight()).reduced (6, 0);
    auto gateHeader = gateArea.removeFromTop (18);
    gateBarLabel.setBounds (gateHeader.removeFromLeft (44));
    gateToggle->button.setBounds (gateHeader.removeFromRight (40));
    gateArea.removeFromTop (2);
    gateKnob->label.setBounds (gateArea.removeFromTop (14));
    gateKnob->slider.setBounds (gateArea);

    pedalboard.setBounds (getLocalBounds().withTrimmedTop (headerHeight).withTrimmedBottom (bottomBarHeight));
}
