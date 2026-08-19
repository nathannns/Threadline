#pragma once
#include <JuceHeader.h>

// Shared note-division math for every pedal's optional tap-tempo Sync
// mode -- one place defining what "1/8." or "1/4T" actually means in
// milliseconds/Hz, rather than duplicating the conversion per pedal.
// Divisions are expressed as a fraction of a quarter note's own duration
// (the standard way delay/tremolo tap-tempo controls define subdivisions),
// so a time-based pedal (Delay/Satellite) multiplies the quarter-note
// period by this fraction, while a rate-based pedal (Tremolo/Chorus/
// Ensemble) divides its quarter-note rate by it -- halving the fraction
// doubles the delay time but doubles the modulation rate, which is the
// correct, opposite relationship between the two pedal families.
namespace TapTempo
{
    inline const juce::StringArray& getDivisionNames()
    {
        static const juce::StringArray names { "1/4", "1/8", "1/8T", "1/8.", "1/16", "1/4." };
        return names;
    }

    inline float getDivisionFraction (int index)
    {
        // 1/4, 1/8, 1/8 triplet, dotted 1/8, 1/16, dotted 1/4.
        static constexpr float fractions[] = { 1.0f, 0.5f, 1.0f / 3.0f, 0.75f, 0.25f, 1.5f };
        return fractions[juce::jlimit (0, 5, index)];
    }

    inline float quarterNoteMs (float bpm) noexcept { return 60000.0f / juce::jmax (1.0f, bpm); }

    // For Delay/Satellite: the actual delay time for this division, in ms.
    inline float timeMsForDivision (float bpm, int divisionIndex) noexcept
    {
        return quarterNoteMs (bpm) * getDivisionFraction (divisionIndex);
    }

    // For Tremolo/Chorus/Ensemble: the actual modulation rate for this
    // division, in Hz.
    inline float rateHzForDivision (float bpm, int divisionIndex) noexcept
    {
        const auto quarterHz = 1000.0f / quarterNoteMs (bpm);
        return quarterHz / getDivisionFraction (divisionIndex);
    }
}
