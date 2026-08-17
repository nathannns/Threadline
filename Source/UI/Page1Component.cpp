#include "Page1Component.h"

Page1Component::Page1Component (ThreadlineAudioProcessor& processor)
{
    buildSection (compSection, *this, processor.apvts, "Compressor", "compOn", {
        { "compThreshold", "Comp" }, { "compRatio", "Attack" },
        { "compAttack", "Tilt" }, { "compRelease", "Mid" }, { "compMakeup", "Level" }
    }, false, SectionPlate::Compressor);

    buildSection (klonSection, *this, processor.apvts, "Klon", "klonOn", {
        { "klonGain", "Gain" }, { "klonTreble", "Treble" }, { "klonLevel", "Level" }
    }, false, SectionPlate::Klon);

    buildSection (ts9Section, *this, processor.apvts, "TS9", "ts9On", {
        { "ts9Drive", "Drive" }, { "ts9Tone", "Tone" }, { "ts9Level", "Level" }
    }, false, SectionPlate::TS9);
}

void Page1Component::paint (juce::Graphics& g)
{
    paintThreadlineBackground (g, getLocalBounds());
    paintSectionPlate (g, compSection);
    paintSectionPlate (g, klonSection);
    paintSectionPlate (g, ts9Section);
}

void Page1Component::resized()
{
    auto area = getLocalBounds().reduced (24, 12);
    constexpr int gap = 8;
    const auto cardHeight = (area.getHeight() - 2 * gap) / 3;
    layoutHorizontalRackSection (compSection, area.removeFromTop (cardHeight));
    // The five-knob compressor reads better with slightly smaller controls
    // and more breathing room than the three-knob drive racks.
    for (auto& knob : compSection.knobs)
    {
        const auto current = knob->slider.getBounds();
        knob->slider.setBounds (current.reduced (current.getWidth() / 10, current.getHeight() / 12));
    }
    area.removeFromTop (gap);
    layoutHorizontalRackSection (klonSection, area.removeFromTop (cardHeight));
    area.removeFromTop (gap);
    layoutHorizontalRackSection (ts9Section, area);
}
