#pragma once

#include <JuceHeader.h>

// Standard feedforward peak-detector compressor: threshold, ratio,
// attack/release times, and makeup gain. Placed after the input gain/meter
// and before the drive pedals, so it's evening out pick dynamics before
// anything downstream (Klon/TS9/Amp) reacts to the transient.
class CompressorModule
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        reset();
    }

    void reset()
    {
        envelope = 0.0f;
        currentGainReductionDb = 0.0f;
    }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    // thresholdDb: level above which compression kicks in.
    // ratio: e.g. 4.0 = 4:1.
    // attackMs/releaseMs: envelope follower speed.
    // makeupDb: gain applied after compression to restore level.
    void setParameters (float thresholdDb, float ratio, float attackMs, float releaseMs, float makeupDb)
    {
        threshold = thresholdDb;
        compressionRatio = juce::jmax (1.0f, ratio);
        attackCoeff = std::exp (-1.0f / static_cast<float> (sampleRate * (attackMs * 0.001f)));
        releaseCoeff = std::exp (-1.0f / static_cast<float> (sampleRate * (releaseMs * 0.001f)));
        makeupGain = juce::Decibels::decibelsToGain (makeupDb);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled)
        {
            currentGainReductionDb = 0.0f;
            return;
        }

        const auto numChannels = buffer.getNumChannels();
        const auto numSamples = buffer.getNumSamples();

        for (int i = 0; i < numSamples; ++i)
        {
            // Stereo-linked peak detection across channels.
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                peak = juce::jmax (peak, std::abs (buffer.getSample (ch, i)));

            const auto coeff = peak > envelope ? attackCoeff : releaseCoeff;
            envelope = coeff * envelope + (1.0f - coeff) * peak;

            const auto envelopeDb = juce::Decibels::gainToDecibels (envelope, -100.0f);
            float gainReductionDb = 0.0f;
            if (envelopeDb > threshold)
                gainReductionDb = (envelopeDb - threshold) * (1.0f - 1.0f / compressionRatio);

            currentGainReductionDb = gainReductionDb;
            const auto gain = juce::Decibels::decibelsToGain (-gainReductionDb) * makeupGain;

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample (ch, i, buffer.getSample (ch, i) * gain);
        }
    }

    // For an optional gain-reduction meter in the UI.
    float getCurrentGainReductionDb() const noexcept { return currentGainReductionDb; }

private:
    double sampleRate = 44100.0;
    bool enabled = false;
    float threshold = -18.0f;
    float compressionRatio = 4.0f;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f;
    float makeupGain = 1.0f;
    float envelope = 0.0f;
    float currentGainReductionDb = 0.0f;
};
