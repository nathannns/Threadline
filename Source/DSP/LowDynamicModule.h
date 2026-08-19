#pragma once
#include <JuceHeader.h>

// "Low Dynamic" -- a two-knob, no-threshold dynamics processor inspired by
// the general, publicly-known concept behind Waves' MV2 (a closed-source
// commercial plugin with no available source or algorithm to reference --
// nothing here is copied from or claims sonic equivalence to it). The
// widely-documented MV2 idea is simply: two knobs, Up and Down, working at
// once, with no visible threshold control -- Up lifts quiet passages,
// Down tames loud ones, both referenced against the program's own level
// rather than a fixed dB point the user has to set by hand.
//
// This is implemented here as an original design: a slow envelope follower
// tracks a floating "centre" (the material's own recent average level,
// standing in for a fixed threshold), and a fast envelope follower tracks
// the instantaneous level. Material quieter than the centre is pulled up
// toward it (amount set by Up); material louder than the centre is pulled
// down toward it (amount set by Down); both act simultaneously, each with
// its own independent strength, softened by a knee around the centre so
// there's no audible switch as material crosses it.
class LowDynamicModule
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        reset();
    }

    void reset()
    {
        envelopeGainDb = 0.0f;
        detectorPower = 0.0f;
        centreLevelDb = -30.0f;
    }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    // upPercent/downPercent: 0 (no effect) .. 100 (strongest pull toward
    // the floating centre) each, fully independent of one another --
    // matching MV2's simultaneous Up+Down operation. fastMode swaps in
    // quicker detector/attack/release times for percussive material.
    void setParameters (float upPercent, float downPercent, bool fastMode, float mixPercent)
    {
        up = juce::jlimit (0.0f, 1.0f, upPercent * 0.01f);
        down = juce::jlimit (0.0f, 1.0f, downPercent * 0.01f);
        mix = juce::jlimit (0.0f, 1.0f, mixPercent * 0.01f);
        const auto attackMs = fastMode ? 3.0f : 15.0f;
        const auto releaseMs = fastMode ? 60.0f : 220.0f;
        attackCoeff = timeCoefficient (attackMs);
        releaseCoeff = timeCoefficient (releaseMs);
        detectorCoeff = timeCoefficient (fastMode ? 5.0f : 12.0f);
        // The centre tracks far slower than the detector -- it represents
        // "the level this material has been sitting around lately", not
        // its instantaneous value, so short transients pull away from it
        // (and get acted on) rather than dragging it along with them.
        centreCoeff = timeCoefficient (fastMode ? 400.0f : 900.0f);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled || mix <= 0.00001f || (up <= 0.00001f && down <= 0.00001f))
        {
            currentGainDb = 0.0f;
            return;
        }

        const auto channels = juce::jmin (2, buffer.getNumChannels());
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            float peakPower = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto value = buffer.getSample (ch, sample);
                peakPower = juce::jmax (peakPower, value * value);
            }
            detectorPower += detectorCoeff * (peakPower - detectorPower);
            const auto levelDb = juce::Decibels::gainToDecibels (std::sqrt (detectorPower), -100.0f);
            centreLevelDb += centreCoeff * (levelDb - centreLevelDb);

            // deviation > 0: louder than the recent centre -> Down acts.
            // deviation < 0: quieter than the recent centre -> Up acts.
            // A soft knee straddles zero deviation so crossing the centre
            // is never an audible switch.
            const auto deviation = levelDb - centreLevelDb;
            constexpr float kneeDb = 4.0f;
            const auto softAbove = [] (float d, float knee)
            {
                if (d > knee * 0.5f) return d;
                if (d > -knee * 0.5f) { const auto k = d + knee * 0.5f; return k * k / (2.0f * knee); }
                return 0.0f;
            };
            const auto aboveAmount = softAbove (deviation, kneeDb);
            const auto belowAmount = softAbove (-deviation, kneeDb);

            // Down pulls loud material back toward the centre (negative
            // gain); Up pushes quiet material up toward the centre
            // (positive gain). Both are always live at once.
            const auto targetGainDb = (belowAmount * up) - (aboveAmount * down);

            const auto envelopeCoeff = std::abs (targetGainDb) > std::abs (envelopeGainDb) ? attackCoeff : releaseCoeff;
            envelopeGainDb += envelopeCoeff * (targetGainDb - envelopeGainDb);
            currentGainDb = envelopeGainDb;

            const auto wetGain = juce::Decibels::decibelsToGain (envelopeGainDb);
            const auto blendedGain = juce::jmap (mix, 1.0f, wetGain);
            for (int ch = 0; ch < channels; ++ch)
                buffer.setSample (ch, sample, buffer.getSample (ch, sample) * blendedGain);
        }
    }

    float getCurrentGainDb() const noexcept { return currentGainDb; }

private:
    float timeCoefficient (float milliseconds) const
    {
        return 1.0f - std::exp (-1.0f / static_cast<float> (sampleRate * milliseconds * 0.001));
    }

    double sampleRate = 44100.0;
    bool enabled = false;
    float up = 0.0f, down = 0.0f, mix = 1.0f;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f, detectorCoeff = 0.0f, centreCoeff = 0.0f;
    float detectorPower = 0.0f;
    float centreLevelDb = -30.0f;
    float envelopeGainDb = 0.0f, currentGainDb = 0.0f;
};
