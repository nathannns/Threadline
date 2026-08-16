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

    buildSection (echoSection, *this, processor.apvts, "Delay", "echoOn", {
        { "echoTime", "Time" }, { "echoRepeats", "Repeats" }, { "echoTone", "Tone" }, { "echoMix", "Mix" }
    }, false, SectionPlate::Delay);

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
    auto area = getLocalBounds().reduced (24, 16);
    const auto cardWidth = (area.getWidth() - 3 * SectionGrid::gap) / 4;
    layoutRackSection (tremSection, area.removeFromLeft (cardWidth));
    area.removeFromLeft (SectionGrid::gap);
    layoutRackSection (chorusSection, area.removeFromLeft (cardWidth));
    area.removeFromLeft (SectionGrid::gap);
    layoutRackSection (echoSection, area.removeFromLeft (cardWidth));
    area.removeFromLeft (SectionGrid::gap);

    auto reverbArea = area;
    reverbSection.bounds = reverbArea;
    reverbArea.reduce (12, 10);
    auto header = reverbArea.removeFromTop (28);
    reverbSection.toggle.setBounds (header.removeFromRight (48).reduced (3, 3));
    reverbSection.titleLabel.setBounds (header);
    reverbArea.removeFromTop (12);

    // Four knobs plus Type form a wrapped 3+2 control grid. This places Type
    // directly to the right of Width on the second row.
    constexpr int columns = 3;
    const auto rows = 2;
    const auto cellWidth = reverbArea.getWidth() / columns;
    const auto cellHeight = reverbArea.getHeight() / rows;
    for (int index = 0; index < (int) reverbSection.knobs.size(); ++index)
    {
        auto cell = juce::Rectangle<int> (reverbArea.getX() + (index % columns) * cellWidth,
                                          reverbArea.getY() + (index / columns) * cellHeight,
                                          cellWidth, cellHeight);
        reverbSection.knobs[(size_t) index]->slider.setBounds (cell.reduced (4, 2));
    }
    auto typeCell = juce::Rectangle<int> (reverbArea.getX() + cellWidth,
                                          reverbArea.getY() + cellHeight,
                                          2 * cellWidth, cellHeight).reduced (6, cellHeight / 3);
    reverbModelBox.setBounds (typeCell);
}
