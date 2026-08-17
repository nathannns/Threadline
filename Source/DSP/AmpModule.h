#pragma once

#include <JuceHeader.h>

// Dynamic, oversampled 5E3-inspired model. Gain and memory are distributed
// through the circuit's major functional stages instead of one static
// clipper: input/interstage coupling caps, a two-stage 12AY7 preamp with
// grid-bias-shift memory (blocking distortion), a cathodyne-style phase
// inverter, an asymmetric push-pull power stage with a bass-weighted sag
// detector, and output-transformer core saturation distinct from sag —
// per Rob Robinette's 5E3 circuit writeup (robrobinette.com).

class AmpModule
{
public:
    // Vintage5E3 is the single-voice tweed circuit described above (one
    // passive-feeling Tone knob, per 5E3 authenticity). Modern3Band swaps
    // that single tone filter for an independent Bass/Mid/Treble stack
    // (low-shelf + mid-peak + high-shelf) placed at the exact same point in
    // the signal chain — everything upstream and downstream (preamp gain
    // stages, phase inverter, sag, output-transformer saturation) is
    // unchanged, so Modern3Band is the same amp "engine" wearing a
    // different, more flexible tone section rather than a different circuit.
    enum class Voice { vintage5E3 = 0, modern3Band = 1 };

    void prepare (const juce::dsp::ProcessSpec& spec, int oversamplingMode = 2)
    {
        baseSampleRate = spec.sampleRate;
        channelCount = juce::jlimit (1, 2, static_cast<int> (spec.numChannels));
        const auto stages = juce::jlimit (0, 2, oversamplingMode);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            static_cast<size_t> (channelCount), static_cast<size_t> (stages),
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversampling->initProcessing (spec.maximumBlockSize);
        const auto oversamplingFactor = oversampling->getOversamplingFactor();
        processingSampleRate = baseSampleRate * static_cast<double> (oversamplingFactor);

        const juce::dsp::ProcessSpec osSpec { processingSampleRate,
                                             static_cast<juce::uint32> (spec.maximumBlockSize * oversamplingFactor),
                                             static_cast<juce::uint32> (channelCount) };
        for (int ch = 0; ch < 2; ++ch)
        {
            inputCoupling[ch].prepare (osSpec);
            interstageCoupling[ch].prepare (osSpec);
            toneFilter[ch].prepare (osSpec);
            transformerLowPass[ch].prepare (osSpec);
            bassShelf[ch].prepare (osSpec);
            midPeak[ch].prepare (osSpec);
            trebleShelf[ch].prepare (osSpec);
        }
        sagAttack = std::exp (-1.0f / static_cast<float> (processingSampleRate * 0.045));
        sagRelease = std::exp (-1.0f / static_cast<float> (processingSampleRate * 0.240));
        // One-pole ~180Hz tap feeding the sag detector — bass notes draw far
        // more current than a bright single-note lead at the same peak level,
        // so weighting the detector toward low frequency content (rather than
        // full-band peak) is what actually makes sustained low chords "bloom"
        // and sag while a treble lead stays comparatively tight.
        sagDetectorLPCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 180.0f
            / static_cast<float> (processingSampleRate));
        outputGain.reset (baseSampleRate, 0.025);
        updateStaticFilters();
        updateToneFilter();
        updateModernToneFilters();
        reset();
    }

    void reset()
    {
        if (oversampling != nullptr)
            oversampling->reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            inputCoupling[ch].reset(); interstageCoupling[ch].reset();
            toneFilter[ch].reset(); transformerLowPass[ch].reset();
            bassShelf[ch].reset(); midPeak[ch].reset(); trebleShelf[ch].reset();
        }
        sagEnvelope = 0.0f;
        biasMemory.fill (0.0f);
        sagDetectorLP.fill (0.0f);
        outputGain.setCurrentAndTargetValue (targetOutputGain);
    }

    void setParameters (float drive01, float tone01, float outputDb, Voice voiceIn,
                        float bass01, float mid01, float treble01)
    {
        driveAmount = juce::jlimit (0.0f, 1.0f, drive01);
        targetOutputGain = juce::Decibels::decibelsToGain (outputDb);
        outputGain.setTargetValue (targetOutputGain);
        voice = voiceIn;
        tone01 = juce::jlimit (0.0f, 1.0f, tone01);
        if (! juce::approximatelyEqual (tone01, lastTone01))
        {
            lastTone01 = tone01;
            updateToneFilter();
        }
        bass01 = juce::jlimit (0.0f, 1.0f, bass01);
        mid01 = juce::jlimit (0.0f, 1.0f, mid01);
        treble01 = juce::jlimit (0.0f, 1.0f, treble01);
        if (! juce::approximatelyEqual (bass01, lastBass01)
            || ! juce::approximatelyEqual (mid01, lastMid01)
            || ! juce::approximatelyEqual (treble01, lastTreble01))
        {
            lastBass01 = bass01; lastMid01 = mid01; lastTreble01 = treble01;
            updateModernToneFilters();
        }
    }

    int getLatencySamples() const noexcept
    {
        return oversampling != nullptr ? juce::roundToInt (oversampling->getLatencyInSamples()) : 0;
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (oversampling == nullptr || buffer.getNumSamples() == 0)
            return;

        auto inputBlock = juce::dsp::AudioBlock<float> (buffer)
                              .getSubsetChannelBlock (0, static_cast<size_t> (channelCount));
        auto block = oversampling->processSamplesUp (inputBlock);
        const auto samples = static_cast<int> (block.getNumSamples());
        const auto channels = static_cast<int> (block.getNumChannels());
        const auto stage1Gain = 1.15f + driveAmount * 4.8f;
        const auto stage2Gain = 1.05f + driveAmount * 3.4f;
        const auto powerDrive = 1.15f + driveAmount * 2.7f;

        for (int i = 0; i < samples; ++i)
        {
            float detector = 0.0f;
            std::array<float, 2> phaseInverterOut {};
            for (int ch = 0; ch < channels; ++ch)
            {
                auto x = inputCoupling[ch].processSample (block.getSample (ch, i));
                constexpr float bias1 = 0.16f;
                auto triode1 = std::tanh (x * stage1Gain + bias1) - std::tanh (bias1);
                triode1 = interstageCoupling[ch].processSample (triode1);
                biasMemory[(size_t) ch] += 0.00055f * (triode1 - biasMemory[(size_t) ch]);
                constexpr float bias2 = -0.10f;
                auto stage2Input = triode1 - 0.13f * biasMemory[(size_t) ch];
                auto triode2 = std::tanh (stage2Input * stage2Gain + bias2) - std::tanh (bias2);
                float toned;
                if (voice == Voice::vintage5E3)
                {
                    toned = toneFilter[ch].processSample (triode2);
                }
                else
                {
                    auto shaped = bassShelf[ch].processSample (triode2);
                    shaped = midPeak[ch].processSample (shaped);
                    toned = trebleShelf[ch].processSample (shaped);
                }
                auto cathodyne = toned >= 0.0f ? std::tanh (toned * 1.18f)
                                               : 0.94f * std::tanh (toned * 1.32f);
                phaseInverterOut[(size_t) ch] = cathodyne;

                auto& bassTap = sagDetectorLP[(size_t) ch];
                bassTap += sagDetectorLPCoefficient * (cathodyne - bassTap);
                const auto weighted = std::abs (cathodyne) * 0.5f + std::abs (bassTap) * 0.5f;
                detector = juce::jmax (detector, weighted);
            }

            const auto coefficient = detector > sagEnvelope ? sagAttack : sagRelease;
            sagEnvelope = coefficient * sagEnvelope + (1.0f - coefficient) * detector;
            const auto sag = juce::jlimit (0.0f, 0.42f,
                sagEnvelope * (0.20f + 0.34f * driveAmount));
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto effectiveDrive = powerDrive * (1.0f - sag);
                const auto push = std::tanh (phaseInverterOut[(size_t) ch] * effectiveDrive + 0.035f);
                const auto pull = std::tanh (phaseInverterOut[(size_t) ch] * effectiveDrive - 0.035f);
                auto power = (push + pull) * 0.5f;
                power /= juce::jmax (0.8f, 0.72f + effectiveDrive * 0.28f);

                // Output-transformer core saturation — a second, distinct
                // compression mechanism from tube sag above (Robinette: "at
                // high volumes, transformer saturation compresses the
                // signal...loud notes are capped but softer notes are still
                // amplified"). Sag is a slow, envelope-driven gain reduction
                // of the drive; this is an instantaneous, level-dependent
                // soft-knee on the transformer's own output, so a hard
                // transient still gets capped even before sag has caught up.
                constexpr float otKnee = 0.65f;
                const auto otMagnitude = std::abs (power);
                if (otMagnitude > otKnee)
                {
                    const auto excess = otMagnitude - otKnee;
                    const auto headroom = 1.0f - otKnee;
                    const auto compressed = otKnee + headroom * std::tanh (excess / headroom);
                    power = std::copysign (compressed, power);
                }

                block.setSample (ch, i, transformerLowPass[ch].processSample (power));
            }
        }

        oversampling->processSamplesDown (inputBlock);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto gain = outputGain.getNextValue();
            for (int ch = 0; ch < channelCount; ++ch)
                buffer.setSample (ch, i, buffer.getSample (ch, i) * gain);
        }
    }

private:
    void updateStaticFilters()
    {
        auto inputHP = juce::dsp::IIR::Coefficients<float>::makeHighPass (processingSampleRate, 48.0f, 0.707f);
        auto couplingHP = juce::dsp::IIR::Coefficients<float>::makeHighPass (processingSampleRate, 72.0f, 0.707f);
        auto transformerLP = juce::dsp::IIR::Coefficients<float>::makeLowPass (processingSampleRate, 7600.0f, 0.72f);
        for (int ch = 0; ch < 2; ++ch)
        {
            *inputCoupling[ch].coefficients = *inputHP;
            *interstageCoupling[ch].coefficients = *couplingHP;
            *transformerLowPass[ch].coefficients = *transformerLP;
        }
    }

    void updateToneFilter()
    {
        if (processingSampleRate <= 0.0)
            return;
        const auto cutoff = 950.0f * std::pow (15.8f, lastTone01);
        auto coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (
            processingSampleRate,
            juce::jmin (cutoff, static_cast<float> (processingSampleRate * 0.42)), 0.62f);
        for (auto& filter : toneFilter)
            *filter.coefficients = *coefficients;
    }

    // Bass/Mid/Treble for Voice::modern3Band. Not an attempt at the exact
    // passive Fender/Marshall tone-stack transfer function (that's a single
    // 3-pole network where the three pots interact non-monotonically) — this
    // is the simpler, independently-adjustable shelf/peak approximation most
    // digital modelers use for a "modern full-EQ" voice: a low shelf, a mid
    // peak, and a high shelf in series, each pot mapped to +-12dB around a
    // flat (0dB) centre so 0.5 on every knob matches the Vintage voice's
    // roughly neutral starting point.
    void updateModernToneFilters()
    {
        if (processingSampleRate <= 0.0)
            return;
        const auto bassDb   = (lastBass01   - 0.5f) * 24.0f;
        const auto midDb    = (lastMid01    - 0.5f) * 24.0f;
        const auto trebleDb = (lastTreble01 - 0.5f) * 24.0f;

        auto bassCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            processingSampleRate, 130.0f, 0.707f, juce::Decibels::decibelsToGain (bassDb));
        auto midCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            processingSampleRate, 650.0f, 0.8f, juce::Decibels::decibelsToGain (midDb));
        auto trebleCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            processingSampleRate, 3200.0f, 0.707f, juce::Decibels::decibelsToGain (trebleDb));

        for (int ch = 0; ch < 2; ++ch)
        {
            *bassShelf[ch].coefficients = *bassCoeffs;
            *midPeak[ch].coefficients = *midCoeffs;
            *trebleShelf[ch].coefficients = *trebleCoeffs;
        }
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::IIR::Filter<float> inputCoupling[2], interstageCoupling[2];
    juce::dsp::IIR::Filter<float> toneFilter[2], transformerLowPass[2];
    juce::dsp::IIR::Filter<float> bassShelf[2], midPeak[2], trebleShelf[2];
    juce::SmoothedValue<float> outputGain;
    std::array<float, 2> biasMemory {};
    std::array<float, 2> sagDetectorLP {};
    double baseSampleRate = 44100.0, processingSampleRate = 176400.0;
    int channelCount = 2;
    Voice voice = Voice::vintage5E3;
    float driveAmount = 0.4f, lastTone01 = 0.6f;
    float lastBass01 = 0.5f, lastMid01 = 0.5f, lastTreble01 = 0.5f;
    float targetOutputGain = 1.0f, sagEnvelope = 0.0f;
    float sagAttack = 0.999f, sagRelease = 0.999f;
    float sagDetectorLPCoefficient = 0.01f;
};
