#pragma once
#include <JuceHeader.h>
#include <BinaryData.h>

// IBM Plex Sans (SIL Open Font License -- Resources/Fonts/LICENSE-IBMPlexSans.txt)
// is this plugin's single UI typeface, in the 4 weights actually used
// anywhere in the design: Regular for descriptions/values, Medium for
// controls/buttons, SemiBold for pedal/thread names and important
// headings, Bold reserved for rare emphasis. Embedded directly (not
// relying on the weight being installed on the host system) so the UI
// reads identically everywhere.
namespace ThreadlineFonts
{
    inline juce::Typeface::Ptr regularTypeface()
    {
        static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (
            BinaryData::IBMPlexSansRegular_ttf, (size_t) BinaryData::IBMPlexSansRegular_ttfSize);
        return tf;
    }
    inline juce::Typeface::Ptr mediumTypeface()
    {
        static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (
            BinaryData::IBMPlexSansMedium_ttf, (size_t) BinaryData::IBMPlexSansMedium_ttfSize);
        return tf;
    }
    inline juce::Typeface::Ptr semiBoldTypeface()
    {
        static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (
            BinaryData::IBMPlexSansSemiBold_ttf, (size_t) BinaryData::IBMPlexSansSemiBold_ttfSize);
        return tf;
    }
    inline juce::Typeface::Ptr boldTypeface()
    {
        static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (
            BinaryData::IBMPlexSansBold_ttf, (size_t) BinaryData::IBMPlexSansBold_ttfSize);
        return tf;
    }

    // Descriptions / values.
    inline juce::Font regular (float height) { return juce::Font (juce::FontOptions (height).withTypeface (regularTypeface())); }
    // Controls / buttons.
    inline juce::Font medium (float height) { return juce::Font (juce::FontOptions (height).withTypeface (mediumTypeface())); }
    // Pedal/thread names, important headings.
    inline juce::Font semiBold (float height) { return juce::Font (juce::FontOptions (height).withTypeface (semiBoldTypeface())); }
    // Rare emphasis only.
    inline juce::Font bold (float height) { return juce::Font (juce::FontOptions (height).withTypeface (boldTypeface())); }
}
