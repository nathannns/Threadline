#include "Page3Component.h"

Page3Component::Page3Component (ThreadlineAudioProcessor& processor)
{
    buildSection (tremSection, *this, processor.apvts, "Tremolo", "tremOn", {
        { "tremAmount", "Amount" }
    }, false, SectionPlate::Tremolo);

    buildSection (chorusSection, *this, processor.apvts, "Chorus", "chorusOn", {
        { "chorusRate", "Rate" }, { "chorusDepth", "Depth" }, { "chorusWidth", "Width" },
        { "chorusTone", "Tone" }, { "chorusMix", "Mix" }
    }, false, SectionPlate::Chorus);
    chorusModeBox.addItemList ({ "CHORUS", "FLANGER I", "FLANGER II", "FLANGER I+II" }, 1);
    addAndMakeVisible (chorusModeBox);
    chorusModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "chorusFlangerMode", chorusModeBox);

    buildSection (echoSection, *this, processor.apvts, "Delay", "echoOn", {
        { "echoTime", "Time" }, { "echoRepeats", "Repeats" }, { "echoTone", "Tone" },
        { "echoWobble", "Wobble" }, { "echoDrive", "Drive" }, { "echoMix", "Mix" }
    }, false, SectionPlate::Delay);
    echoPatternBox.addItemList ({ "STRAIGHT", "BOUNCE", "GALLOP", "CLUSTER", "WASH" }, 1);
    echoDivisionBox.addItemList ({ "1/4", "1/4 D", "1/8", "1/8 D", "1/8 T", "1/16", "1/16 D", "1/16 T" }, 1);
    addAndMakeVisible (echoPatternBox);
    addAndMakeVisible (echoDivisionBox);
    addAndMakeVisible (echoSyncButton);
    echoPatternAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "echoPattern", echoPatternBox);
    echoDivisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "echoDivision", echoDivisionBox);
    echoSyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "echoSync", echoSyncButton);

    // Reverb: 3 spring-tank models + 7 Lexicon 480L hall/room models sharing
    // one Decay/Tone/Mix set. Decay works as pre-delay when a hall/room model
    // is selected (springs have no true pre-delay stage).
    buildSection (reverbSection, *this, processor.apvts, "Reverb", "reverbOn", {
        { "reverbDecay", "Decay" }, { "reverbTone", "Tone" }, { "reverbMix", "Mix" }, { "reverbWidth", "Width" }
    }, false, SectionPlate::Reverb);
    reverbModelBox.addItemList ({ "Space", "9100", "Echomixer",
                                   "Large Hall", "Large Stage", "Small Church", "Small Hall",
                                   "Small Stage", "Large Room", "Small Room" }, 1);
    addAndMakeVisible (reverbModelBox);
    reverbModelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "reverbModel", reverbModelBox);
}

void Page3Component::paint (juce::Graphics& g)
{
    paintThreadlineBackground (g, getLocalBounds());
    paintSectionPlate (g, tremSection);
    paintSectionPlate (g, chorusSection);
    paintSectionPlate (g, echoSection);
    paintSectionPlate (g, reverbSection);
}

void Page3Component::resized()
{
    auto area = getLocalBounds().reduced (24, 10);
    constexpr int gap = 6;
    const auto cardHeight = (area.getHeight() - 3 * gap) / 4;
    layoutHorizontalRackSection (tremSection, area.removeFromTop (cardHeight));
    area.removeFromTop (gap);
    layoutHorizontalRackSection (chorusSection, area.removeFromTop (cardHeight));
    auto chorusBounds = chorusSection.bounds;
    chorusModeBox.setBounds (chorusBounds.getX() + 14, chorusBounds.getBottom() - 30,
                             juce::jmin (170, chorusBounds.getWidth() / 5 - 20), 24);
    area.removeFromTop (gap);
    layoutHorizontalRackSection (echoSection, area.removeFromTop (cardHeight));
    auto echoBounds = echoSection.bounds;
    const auto selectorY = echoBounds.getBottom() - 29;
    echoSyncButton.setBounds (echoBounds.getX() + 12, selectorY, 44, 22);
    echoPatternBox.setBounds (echoBounds.getX() + 60, selectorY, 82, 22);
    echoDivisionBox.setBounds (echoBounds.getX() + 146, selectorY, 62, 22);
    area.removeFromTop (gap);

    auto reverbArea = area;
    layoutHorizontalRackSection (reverbSection, reverbArea);
    // Type follows the rightmost Width control.
    const auto typeWidth = juce::jlimit (120, 180, reverbArea.getWidth() / 7);
    reverbModelBox.setBounds (reverbArea.getRight() - typeWidth - 12,
                              reverbArea.getCentreY() - 14, typeWidth, 28);
    if (! reverbSection.knobs.empty())
    {
        auto& width = reverbSection.knobs.back()->slider;
        width.setBounds (width.getBounds().translated (-typeWidth / 2, 0));
    }
}
