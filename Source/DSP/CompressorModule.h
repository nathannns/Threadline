#pragma once
#include <JuceHeader.h>

// Diamond-inspired optical (vactrol) compressor. This is an original model,
// not a circuit clone, but its release stage models the one thing that
// actually makes optical compression sound different from VCA/OTA/FET
// designs: the light-dependent resistor's own recovery is not a single
// exponential. Documented on the archetypal vactrol compressor (LA-2A):
// roughly the first 50% of gain-reduction recovery happens fast (~40-80ms),
// while the remaining 50% creeps back over 1-15 seconds depending on how
// hard/long the cell was driven (its "memory effect"). A single release
// time constant can only approximate an *average* of those two very
// different rates; blending two independently-timed followers reproduces
// the actual two-stage shape instead. Attack stays a single, fast stage
// either way — the dual-time-constant behaviour is specific to release.
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
        detectorPower = 0.0f;
        fastGR = slowGR = gainReductionDb = currentGainReductionDb = 0.0f;
        exposure = 0.0f;
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
        // Detector (LED driver side): tracks the input quickly and plainly —
        // the characteristic two-stage lag belongs to the LDR's resistance
        // recovery below, not to how fast the cell's light source reacts.
        const auto detectorRelease = timeCoefficient (detectorReleaseMs);
        const auto ldrFastRelease = timeCoefficient (ldrFastReleaseMs);
        const auto exposureCoeff = timeCoefficient (exposureTimeMs);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            float linkedPower = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto value = buffer.getSample (ch, sample);
                linkedPower = juce::jmax (linkedPower, value * value);
            }
            const auto detectorCoeff = linkedPower > detectorPower ? attackCoeff : detectorRelease;
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

            // Fast stage: the first ~50% of the LDR's recovery, ~40-80ms.
            const auto fastCoeff = targetReduction > fastGR ? attackCoeff : ldrFastRelease;
            fastGR = fastCoeff * fastGR + (1.0f - fastCoeff) * targetReduction;

            // Slow stage: the remaining ~50%, over 1-15s. How long/hard the
            // cell has recently been driven (tracked here as a slow running
            // average of the applied reduction) sets where in that range it
            // falls right now — mirrors the documented "heavier or longer
            // compression -> slower release" behaviour.
            exposure = exposureCoeff * exposure + (1.0f - exposureCoeff) * gainReductionDb;
            const auto slowReleaseMs = juce::jmap (juce::jlimit (0.0f, 20.0f, exposure),
                                                   ldrSlowReleaseMinMs, ldrSlowReleaseMaxMs);
            const auto slowCoeff = targetReduction > slowGR ? attackCoeff : timeCoefficient (slowReleaseMs);
            slowGR = slowCoeff * slowGR + (1.0f - slowCoeff) * targetReduction;

            gainReductionDb = 0.5f * fastGR + 0.5f * slowGR;
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
    float outputGain = 1.0f;
    float detectorPower = 0.0f;
    // Two independently-timed followers, both chasing the same target
    // reduction, blended 50/50 — see process() for why.
    float fastGR = 0.0f, slowGR = 0.0f;
    float exposure = 0.0f;
    float gainReductionDb = 0.0f, currentGainReductionDb = 0.0f;
    static constexpr float detectorReleaseMs = 60.0f;
    static constexpr float ldrFastReleaseMs = 55.0f;     // mid of the documented 40-80ms range
    static constexpr float ldrSlowReleaseMinMs = 1000.0f;
    static constexpr float ldrSlowReleaseMaxMs = 15000.0f;
    static constexpr float exposureTimeMs = 2500.0f;
    std::array<juce::dsp::IIR::Filter<float>, 2> lowShelf, highShelf, midBell;
};
