#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
    void setupUtilityKnob (juce::Label& label, PhotoKnob&, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ThreadlineColours::textCream);
        label.setJustificationType (juce::Justification::centredLeft);
    }
}

ThreadlineAudioProcessorEditor::ThreadlineAudioProcessorEditor (ThreadlineAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), page1 (p), page2 (p), page3 (p), page4 (p)
{
    setLookAndFeel (&buttonLookAndFeel);
    logoImage = juce::ImageCache::getFromMemory (BinaryData::threadline_logo_png, BinaryData::threadline_logo_pngSize);

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

    // --- Tab strip ---
    for (int i = 0; i < (int) tabPills.size(); ++i)
    {
        addAndMakeVisible (tabPills[(size_t) i]);
        tabPills[(size_t) i].onClick = [this, i] { switchToPage (i); };
    }
    tabPills[0].setToggleState (true, juce::dontSendNotification);

    // --- Persistent Gate / Input / Output strip ---
    buildSection (gateSection, *this, processor.apvts, "Gate", "gateOn", {
        { "gateAmount", "Amount" }
    }, false, SectionPlate::Gate);
    // The footer uses one wordmark above each knob; the gate parameter's
    // attached "Amount" caption would overlap its toggle and steal clicks.
    if (! gateSection.knobs.empty())
        gateSection.knobs[0]->label.setVisible (false);
    gateSection.titleLabel.setVisible (false);
    gateSection.toggle.setButtonText ("GATE");

    setupUtilityKnob (inputLabel, inputGainKnob, "Input");
    setupUtilityKnob (outputLabel, outputGainKnob, "Output");
    addAndMakeVisible (inputLabel);
    addAndMakeVisible (outputLabel);
    addAndMakeVisible (inputGainKnob);
    addAndMakeVisible (outputGainKnob);
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    inputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "inputGain", inputGainKnob);
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "outputGain", outputGainKnob);

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

    // --- Pages ---
    addAndMakeVisible (page1);
    addAndMakeVisible (page2);
    addAndMakeVisible (page3);
    addAndMakeVisible (page4);
    switchToPage (0);

    // Same reference canvas and resize policy as Rockalizer.
    setSize (1200, 660);
    setResizable (true, true);
    setResizeLimits (900, 495, 1600, 880);
    getConstrainer()->setFixedAspectRatio (1200.0 / 660.0);

    startTimerHz (30);
}

ThreadlineAudioProcessorEditor::~ThreadlineAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void ThreadlineAudioProcessorEditor::switchToPage (int pageIndex)
{
    currentPage = pageIndex;
    page1.setVisible (pageIndex == 0);
    page2.setVisible (pageIndex == 1);
    page3.setVisible (pageIndex == 2);
    page4.setVisible (pageIndex == 3);

    for (int i = 0; i < (int) tabPills.size(); ++i)
        tabPills[(size_t) i].setToggleState (i == pageIndex, juce::dontSendNotification);
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

    if (logoImage.isValid())
    {
        // Same on-screen box Rockalizer draws its own logo into (252x80 at
        // (46,10)), so Threadline's logo reads at the same size/weight.
        const auto scaleX = static_cast<float> (getWidth()) / 1200.0f;
        const auto scaleY = static_cast<float> (getHeight()) / 660.0f;
        auto logoArea = juce::Rectangle<float> (46.0f * scaleX, 10.0f * scaleY,
                                                252.0f * scaleX, 80.0f * scaleY);
        g.drawImage (logoImage, logoArea, juce::RectanglePlacement (juce::RectanglePlacement::centred
                                                                    | juce::RectanglePlacement::onlyReduceInSize));
    }

    paintCard (g, presetCardBounds, 8.0f);

    // Gate, Input and Output read as one compact global utility strip.
    paintCard (g, utilityFrameBounds);
}

void ThreadlineAudioProcessorEditor::timerCallback()
{
    inputMeter.setLevel (processor.getInputLevel());
    outputMeter.setLevel (processor.getOutputLevel());
}

void ThreadlineAudioProcessorEditor::resized()
{
    const auto scaleX = static_cast<float> (getWidth()) / 1200.0f;
    const auto scaleY = static_cast<float> (getHeight()) / 660.0f;
    const auto rect = [scaleX, scaleY] (int x, int y, int w, int h)
    {
        return juce::Rectangle<int> (juce::roundToInt (static_cast<float> (x) * scaleX),
                                     juce::roundToInt (static_cast<float> (y) * scaleY),
                                     juce::roundToInt (static_cast<float> (w) * scaleX),
                                     juce::roundToInt (static_cast<float> (h) * scaleY));
    };

    // Exact Rockalizer preset bar geometry.
    presetCardBounds = rect (342, 18, 530, 56);
    prevPresetButton.setBounds (rect (352, 28, 38, 36));
    presetBox.setBounds (rect (398, 28, 256, 36));
    presetDropdownButton.setBounds (rect (618, 28, 36, 36));
    presetDropdownButton.toFront (false);
    nextPresetButton.setBounds (rect (662, 28, 38, 36));
    addPresetButton.setBounds (rect (708, 28, 40, 36));
    savePresetButton.setBounds (rect (754, 28, 40, 36));
    deletePresetButton.setBounds (rect (800, 28, 40, 36));
    optionsMenuButton.setBounds (rect (1004, 18, 56, 56));
    powerButton.setBounds (rect (1080, 18, 60, 56));

    // Exact Rockalizer footer frame, positions and control sizes.
    utilityFrameBounds = rect (28, 546, 1144, 108);
    gateCardBounds = rect (60, 548, 110, 104);
    inputCardBounds = rect (545, 548, 110, 104);
    outputCardBounds = rect (1030, 548, 110, 104);
    gateSection.toggle.setBounds (rect (60, 550, 110, 22));
    if (! gateSection.knobs.empty())
        gateSection.knobs[0]->slider.setBounds (rect (60, 574, 110, 78));
    inputLabel.setJustificationType (juce::Justification::centred);
    inputLabel.setBounds (rect (545, 550, 110, 20));
    inputGainKnob.setBounds (rect (545, 574, 110, 78));
    outputLabel.setJustificationType (juce::Justification::centred);
    outputLabel.setBounds (rect (1030, 550, 110, 20));
    outputGainKnob.setBounds (rect (1030, 574, 110, 78));
    inputMeter.setBounds (rect (235, 610, 290, 12));
    outputMeter.setBounds (rect (720, 610, 290, 12));

    // Navigation and pages occupy the space between Rockalizer's header/footer.
    auto tabRow = rect (28, 88, 1144, 42);
    const auto pillWidth = juce::roundToInt (46 * scaleX);
    const auto pillGap = juce::roundToInt (8 * scaleX);
    const auto groupWidth = 4 * pillWidth + 3 * pillGap;
    auto tabArea = tabRow.withSizeKeepingCentre (groupWidth, tabRow.getHeight()).reduced (0, 6);
    for (auto& pill : tabPills)
    {
        pill.setBounds (tabArea.removeFromLeft (pillWidth));
        tabArea.removeFromLeft (pillGap);
    }

    // Floating Rockalizer-style options panel below the gear button.
    optionsGroup.setBounds (rect (898, 82, 282, 132));
    input1Button.setBounds (juce::roundToInt (12.0f * scaleX), juce::roundToInt (30.0f * scaleY),
                            juce::roundToInt (72.0f * scaleX), juce::roundToInt (32.0f * scaleY));
    input2Button.setBounds (juce::roundToInt (88 * scaleX), juce::roundToInt (30 * scaleY),
                            juce::roundToInt (72 * scaleX), juce::roundToInt (32 * scaleY));
    inputSourceBox.setBounds (juce::roundToInt (164 * scaleX), juce::roundToInt (30 * scaleY),
                              juce::roundToInt (106 * scaleX), juce::roundToInt (32 * scaleY));
    ampQualityLabel.setBounds (juce::roundToInt (14 * scaleX), juce::roundToInt (74 * scaleY),
                               juce::roundToInt (142 * scaleX), juce::roundToInt (28 * scaleY));
    ampOversamplingBox.setBounds (juce::roundToInt (164 * scaleX), juce::roundToInt (72 * scaleY),
                                  juce::roundToInt (106 * scaleX), juce::roundToInt (32 * scaleY));

    // --- Every page receives the same remaining content rectangle. ---
    auto content = rect (16, 130, 1168, 406);
    page1.setBounds (content);
    page2.setBounds (content);
    page3.setBounds (content);
    page4.setBounds (content);
}
