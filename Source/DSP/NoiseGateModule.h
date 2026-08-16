#pragma once

#include <JuceHeader.h>

// Same three-band, hysteresis + hold noise gate used in Rockalizer, pulled out
// of PluginProcessor into a standalone reusable module. Frequency-trimmed low/
// mid/high detector bands mean pick attack and upper harmonics can open the
// gate as readily as low fundamentals, with hold + hysteresis to stop chatter.
class NoiseGateModule
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        reset();
    }

    void reset()
    {
        bandEnvelope.fill (0.0f);
        lowState = midState = 0.0f;
        gain = 1.0f;
        holdSamples = 0;
        gateOpen = true;
    }

    // amountPercent: 0-100, same meaning as Rockalizer's "Noise Cut" knob.
    void setAmount (float amountPercent) { amount = juce::jlimit (0.0f, 100.0f, amountPercent) * 0.01f; }
    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled || amount <= 0.0001f)
        {
            reset();
            return;
        }

        const auto openThresholdDb = juce::jmap (amount, -78.0f, -32.0f);
        const auto closeThresholdDb = openThresholdDb - 6.0f;
        const auto detectorAttack = std::exp (-1.0f / static_cast<float> (sampleRate * 0.0012));
        const auto detectorRelease = std::exp (-1.0f / static_cast<float> (sampleRate * 0.085));
        const auto gateAttack = std::exp (-1.0f / static_cast<float> (sampleRate * 0.0008));
        const auto gateRelease = std::exp (-1.0f / static_cast<float> (sampleRate * 0.180));
        const auto lowCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 180.0f
                                                     / static_cast<float> (sampleRate));
        const auto midCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 2600.0f
                                                     / static_cast<float> (sampleRate));
        const auto holdLength = juce::roundToInt (sampleRate * 0.045);
        const std::array<float, 3> bandOffsetsDb { 2.0f, 0.0f, -2.0f };

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            float detector = 0.0f;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                const auto candidate = buffer.getSample (channel, sample);
                if (std::abs (candidate) > std::abs (detector))
                    detector = candidate;
            }

            lowState += lowCoefficient * (detector - lowState);
            midState += midCoefficient * (detector - midState);
            const std::array<float, 3> bands {
                std::abs (lowState),
                std::abs (midState - lowState),
                std::abs (detector - midState)
            };

            float activityDb = -120.0f;
            for (size_t band = 0; band < bands.size(); ++band)
            {
                auto& envelope = bandEnvelope[band];
                const auto coefficient = bands[band] > envelope ? detectorAttack : detectorRelease;
                envelope = coefficient * envelope + (1.0f - coefficient) * bands[band];
                activityDb = juce::jmax (activityDb,
                    juce::Decibels::gainToDecibels (envelope, -120.0f) - bandOffsetsDb[band]);
            }

            if (activityDb >= openThresholdDb)
            {
                gateOpen = true;
                holdSamples = holdLength;
            }
            else if (holdSamples > 0)
                --holdSamples;
            else if (activityDb < closeThresholdDb)
                gateOpen = false;

            const auto floorDb = juce::jmap (amount, -18.0f, -72.0f);
            const auto belowDb = juce::jmax (0.0f, closeThresholdDb - activityDb);
            const auto closedGainDb = juce::jmax (floorDb, -belowDb * 2.2f);
            const auto targetGain = gateOpen ? 1.0f : juce::Decibels::decibelsToGain (closedGainDb);
            const auto gainCoefficient = targetGain > gain ? gateAttack : gateRelease;
            gain = gainCoefficient * gain + (1.0f - gainCoefficient) * targetGain;

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample (channel, sample, buffer.getSample (channel, sample) * gain);
        }
    }

private:
    std::array<float, 3> bandEnvelope { 0.0f, 0.0f, 0.0f };
    float lowState = 0.0f, midState = 0.0f, gain = 1.0f;
    int holdSamples = 0;
    bool gateOpen = true;
    bool enabled = false;
    float amount = 0.0f;
    double sampleRate = 44100.0;
};
