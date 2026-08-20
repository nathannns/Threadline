#pragma once

#include <JuceHeader.h>

// "JC Chorus" -- the Roland JC-120's built-in BBD chorus line, extracted
// from the amp into a standalone stereo pedal. The real amp feeds one BBD
// (MN3007) chorus line to its two physically separate power amps/speakers
// with their modulation phase-offset from each other, which is the whole of
// its signature glassy stereo swirl; this models that same "one shared LFO,
// two channels offset by pi" method rather than a generic dual-LFO chorus.
// The centre delay (12ms), depth (4ms), mix (45%) and rate (0.9Hz) defaults
// reproduce exactly what AmpModule's JC-120 voice shipped with before its
// chorus was removed so the amp could run dry -- this pedal is that chorus
// as its own thing. (Those values were themselves a clearly-approximate
// tasteful default rather than figures transcribed from the schematic's
// dense component list, same honesty standard as the rest of this file.)
//
// Deliberately simpler than Ensemble/Dimension: a single modulated delay
// line per channel with linear interpolation and no compander/BBD-saturation
// feedback path -- the JC-120's chorus is a clean, "shimmery" chorus with
// no feedback regeneration, not a dense multi-tap ensemble.
class JCChorusModule
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        const auto channels = static_cast<int> (spec.numChannels);
        delayBuffer.setSize (channels, static_cast<int> (sampleRate * 0.05) + 4);  // 50ms headroom
        delayBuffer.clear();
        for (auto* value : { &rateValue, &depthValue, &mixValue })
            value->reset (sampleRate, 0.03);
        reset();
    }

    void reset()
    {
        delayBuffer.clear();
        writeIndex = 0;
        lfoPhase = 0.0f;
        rateValue.setCurrentAndTargetValue (0.9f);
        depthValue.setCurrentAndTargetValue (0.5f);
        mixValue.setCurrentAndTargetValue (0.0f);
    }

    void setParameters (float rateHz, float depthPercent, float mixPercent, bool enabled)
    {
        // depthPercent 0..100 -> depthMs 0..8, so 50% reproduces the amp's
        // fixed 4ms depth. rate clamped to a sane LFO range; mix 0..1.
        rateValue.setTargetValue (juce::jlimit (0.1f, 5.0f, rateHz));
        depthValue.setTargetValue (juce::jlimit (0.0f, 1.0f, depthPercent * 0.01f));
        mixValue.setTargetValue (enabled ? juce::jlimit (0.0f, 1.0f, mixPercent * 0.01f) : 0.0f);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! mixValue.isSmoothing() && mixValue.getCurrentValue() <= 0.00001f)
            return;

        const auto channels = juce::jmin (2, buffer.getNumChannels(), delayBuffer.getNumChannels());
        if (channels == 0)
            return;

        const auto size = delayBuffer.getNumSamples();
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto rate = rateValue.getNextValue();
            const auto depth = depthValue.getNextValue();
            const auto mix = mixValue.getNextValue();

            const auto depthMs = depth * 8.0f;  // 0..8ms swing around the 12ms centre
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto input = buffer.getSample (channel, sample);
                delayBuffer.setSample (channel, writeIndex, input);

                const auto lfoOffset = channel == 0 ? 0.0f : juce::MathConstants<float>::pi;
                const auto modulated = std::sin (lfoPhase + lfoOffset);
                const auto delaySamples = (centreDelayMs + modulated * depthMs) * 0.001f * (float) sampleRate;
                const auto clamped = juce::jlimit (1.0f, static_cast<float> (size) - 1.0f, delaySamples);
                auto readPos = static_cast<float> (writeIndex) - clamped;
                while (readPos < 0.0f)
                    readPos += static_cast<float> (size);

                const auto i0 = static_cast<int> (readPos);
                const auto frac = readPos - static_cast<float> (i0);
                const auto i1 = (i0 + 1) % size;
                const auto delayed = delayBuffer.getSample (channel, i0)
                                   + frac * (delayBuffer.getSample (channel, i1) - delayBuffer.getSample (channel, i0));

                buffer.setSample (channel, sample, input * (1.0f - mix) + delayed * mix);
            }

            writeIndex = (writeIndex + 1) % size;
            lfoPhase += juce::MathConstants<float>::twoPi * rate / static_cast<float> (sampleRate);
            if (lfoPhase >= juce::MathConstants<float>::twoPi)
                lfoPhase -= juce::MathConstants<float>::twoPi;
        }
    }

    bool isWetTransitionActive() const noexcept
    {
        return mixValue.isSmoothing() || mixValue.getCurrentValue() > 0.00001f;
    }

private:
    juce::AudioBuffer<float> delayBuffer;
    juce::SmoothedValue<float> rateValue, depthValue, mixValue;
    double sampleRate = 44100.0;
    int writeIndex = 0;
    float lfoPhase = 0.0f;
    float centreDelayMs = 12.0f;  // fixed, from the amp's JC-120 voice
};
