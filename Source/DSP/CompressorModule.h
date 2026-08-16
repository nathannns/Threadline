#pragma once
#include <JuceHeader.h>

// Diamond-inspired optical compressor. This is an original model, not a circuit clone.
class CompressorModule
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        const juce::dsp::ProcessSpec monoSpec { spec.sampleRate, spec.maximumBlockSize, 1 };
        for (auto* bank : { &lowShelf, &highShelf, &midBell })
            for (auto& filter : *bank) filter.prepare (monoSpec);
        reset();
    }

    void reset()
    {
        detectorPower = gainReductionDb = currentGainReductionDb = 0.0f;
        for (auto* bank : { &lowShelf, &highShelf, &midBell })
            for (auto& filter : *bank) filter.reset();
    }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    void setParameters (float compressionPercent, float attackPercent, float tiltPercent,
                        float midDb, float levelDb)
    {
        const auto amount = juce::jlimit (0.0f, 1.0f, compressionPercent * 0.01f);
        thresholdDb = juce::jmap (amount, -6.0f, -38.0f);
        ratio = juce::jmap (amount, 1.5f, 4.0f);
        attackCoeff = timeCoefficient (juce::jmap (juce::jlimit (0.0f, 100.0f, attackPercent), 3.0f, 32.0f));
        baseReleaseMs = juce::jmap (amount, 180.0f, 480.0f);
        outputGain = juce::Decibels::decibelsToGain (levelDb);

        const auto tiltDb = juce::jlimit (-100.0f, 100.0f, tiltPercent) * 0.045f;
        const auto low = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            sampleRate, 900.0, 0.707f, juce::Decibels::decibelsToGain (-tiltDb));
        const auto high = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRate, 900.0, 0.707f, juce::Decibels::decibelsToGain (tiltDb));
        const auto mid = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            sampleRate, 800.0, 0.75f, juce::Decibels::decibelsToGain (midDb));
        for (int ch = 0; ch < 2; ++ch)
        {
            lowShelf[(size_t) ch].coefficients = low;
            highShelf[(size_t) ch].coefficients = high;
            midBell[(size_t) ch].coefficients = mid;
        }
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled) { currentGainReductionDb = 0.0f; return; }
        const auto channels = juce::jmin (2, buffer.getNumChannels());
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            float linkedPower = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto value = buffer.getSample (ch, sample);
                linkedPower = juce::jmax (linkedPower, value * value);
            }
            const auto release = timeCoefficient (baseReleaseMs + gainReductionDb * 28.0f);
            const auto detectorCoeff = linkedPower > detectorPower ? attackCoeff : release;
            detectorPower = detectorCoeff * detectorPower + (1.0f - detectorCoeff) * linkedPower;
            const auto levelDb = juce::Decibels::gainToDecibels (std::sqrt (detectorPower), -100.0f);

            constexpr float kneeDb = 8.0f;
            const auto over = levelDb - thresholdDb;
            const auto slope = 1.0f - 1.0f / ratio;
            float targetReduction = 0.0f;
            if (over > kneeDb * 0.5f)
                targetReduction = slope * (over - kneeDb * 0.25f);
            else if (over > -kneeDb * 0.5f)
            {
                const auto kneePosition = over + kneeDb * 0.5f;
                targetReduction = slope * kneePosition * kneePosition / (2.0f * kneeDb);
            }
            const auto grCoeff = targetReduction > gainReductionDb ? attackCoeff : release;
            gainReductionDb = grCoeff * gainReductionDb + (1.0f - grCoeff) * targetReduction;
            currentGainReductionDb = gainReductionDb;
            const auto gain = juce::Decibels::decibelsToGain (-gainReductionDb) * outputGain;
            for (int ch = 0; ch < channels; ++ch)
            {
                auto value = buffer.getSample (ch, sample) * gain;
                value = lowShelf[(size_t) ch].processSample (value);
                value = highShelf[(size_t) ch].processSample (value);
                value = midBell[(size_t) ch].processSample (value);
                buffer.setSample (ch, sample, value);
            }
        }
    }

    float getCurrentGainReductionDb() const noexcept { return currentGainReductionDb; }

private:
    float timeCoefficient (float milliseconds) const
    {
        return std::exp (-1.0f / static_cast<float> (sampleRate * milliseconds * 0.001));
    }
    double sampleRate = 44100.0;
    bool enabled = false;
    float thresholdDb = -20.0f, ratio = 3.0f, attackCoeff = 0.0f;
    float baseReleaseMs = 260.0f, outputGain = 1.0f;
    float detectorPower = 0.0f, gainReductionDb = 0.0f, currentGainReductionDb = 0.0f;
    std::array<juce::dsp::IIR::Filter<float>, 2> lowShelf, highShelf, midBell;
};
