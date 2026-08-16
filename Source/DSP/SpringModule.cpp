#include "SpringModule.h"

void SpringModule::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    maximumBlockSize = static_cast<int> (spec.maximumBlockSize);
    channelCount = static_cast<int> (spec.numChannels);
    wetBuffer.setSize (channelCount, maximumBlockSize);
    dripBuffer.setSize (channelCount, maximumBlockSize);
    dispersionBuffer.setSize (3, static_cast<int> (sampleRate * 0.020) + 4);
    dispersionBuffer.clear();
    // Four mutually prime-ish delay lines form the dense late field that
    // follows the recognisable IR onset. This is intentionally independent of
    // the host channel count so mono input receives the same rich decay.
    tailBuffer.setSize (4, static_cast<int> (sampleRate * 0.075) + 4);
    tailBuffer.clear();
    convolution.prepare (spec); toneFilter.prepare (spec); bodyFilter.prepare (spec); dripFilter.prepare (spec);
    toneFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    bodyFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    bodyFilter.setCutoffFrequency (115.0f);
    dripFilter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
    dripFilter.setCutoffFrequency (2450.0f); dripFilter.setResonance (0.76f);
    dripDetectorCoefficient = 1.0f - std::exp (-1.0f /
        (static_cast<float> (sampleRate) * 0.018f));
    envelopeAttack = 1.0f - std::exp (-1.0f /
        (static_cast<float> (sampleRate) * 0.006f));
    envelopeRelease = 1.0f - std::exp (-1.0f /
        (static_cast<float> (sampleRate) * 0.220f));
    cachedToneHz = -1.0f;
    inputEnvelope.assign (static_cast<size_t> (channelCount), 0.0f);
    dripEnvelope.assign (static_cast<size_t> (channelCount), 0.0f);
    dispersionDampingState.assign (3, 0.0f);
    tailDampingState.assign (4, 0.0f);
    wetMix.reset (spec.sampleRate, 0.03);
    reset();
    triggerAsyncUpdate();
}

void SpringModule::reset()
{
    convolution.reset(); toneFilter.reset(); bodyFilter.reset(); dripFilter.reset();
    std::fill (inputEnvelope.begin(), inputEnvelope.end(), 0.0f);
    std::fill (dripEnvelope.begin(), dripEnvelope.end(), 0.0f);
    std::fill (dispersionDampingState.begin(), dispersionDampingState.end(), 0.0f);
    std::fill (tailDampingState.begin(), tailDampingState.end(), 0.0f);
    tailBuffer.clear();
    dispersionBuffer.clear();
    dispersionWriteIndex = 0;
    tailWriteIndex = 0;
    tailModPhase = 0.0f;
    wetMix.setCurrentAndTargetValue (0.0f);
}

void SpringModule::setParameters (float decay, float dwell, float tone, float drip, float mix,
                                  bool enabled, int impulseIndex)
{
    decayAmount = juce::jlimit (0.0f, 1.0f, decay * 0.01f);
    dwellAmount = juce::jlimit (0.0f, 1.0f, dwell * 0.01f);
    dripAmount = juce::jlimit (0.0f, 1.0f, drip * 0.01f);
    toneAmount = juce::jlimit (0.0f, 1.0f, tone * 0.01f);
    const auto toneHz = juce::jmap (toneAmount,
                                    1400.0f, 12500.0f);
    if (std::abs (toneHz - cachedToneHz) > 0.01f)
    {
        cachedToneHz = toneHz;
        toneFilter.setCutoffFrequency (toneHz);
    }
    wetMix.setTargetValue (enabled ? juce::jlimit (0.0f, 1.0f, mix * 0.01f) : 0.0f);
    impulseIndex = juce::jlimit (0, 2, impulseIndex);
    currentModel = impulseIndex;
    if (cachedFilterModel != currentModel)
    {
        constexpr float bodyCutoffs[] { 135.0f, 92.0f, 118.0f };
        bodyFilter.setCutoffFrequency (bodyCutoffs[currentModel]);
        cachedFilterModel = currentModel;
    }
    const auto impulseChanged = requestedImpulse.exchange (impulseIndex) != impulseIndex;
    if (impulseChanged || loadedImpulse != impulseIndex)
        triggerAsyncUpdate();
}

void SpringModule::handleAsyncUpdate()
{
    const auto impulse = requestedImpulse.load();
    loadImpulse (impulse);
    if (impulse != requestedImpulse.load())
        triggerAsyncUpdate();
}

void SpringModule::loadImpulse (int index)
{
    const void* data[] { BinaryData::spring_space_clean_wav,
                         BinaryData::spring_9100_clean_wav,
                         BinaryData::spring_echomixer_clean_wav };
    const int sizes[] { BinaryData::spring_space_clean_wavSize,
                        BinaryData::spring_9100_clean_wavSize,
                        BinaryData::spring_echomixer_clean_wavSize };
    // Preserve a fixed physical onset for each tank. Decay is synthesized by
    // the spring network below, so moving the knob no longer rebuilds or
    // abruptly shortens convolution while audio is running.
    constexpr float onsetSeconds[] { 1.05f, 1.20f, 0.95f };
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (
        new juce::MemoryInputStream (data[index], static_cast<size_t> (sizes[index]), false), true));
    if (reader != nullptr)
    {
        const auto length = static_cast<int> (juce::jmin (
            static_cast<juce::int64> (reader->lengthInSamples),
            static_cast<juce::int64> (reader->sampleRate * onsetSeconds[index])));
        const auto channels = juce::jlimit (1, 2, static_cast<int> (reader->numChannels));
        juce::AudioBuffer<float> impulse (channels, juce::jmax (1, length));
        reader->read (&impulse, 0, length, 0, true, channels > 1);
        // Overlap the measured onset and the algorithmic late field so their
        // hand-off remains soft, even with long decay and high mix settings.
        const auto fadeSamples = juce::jmin (length, juce::roundToInt (reader->sampleRate * 0.34));
        for (int sample = juce::jmax (0, length - fadeSamples); sample < length; ++sample)
        {
            const auto position = static_cast<float> (length - sample)
                                / static_cast<float> (juce::jmax (1, fadeSamples));
            const auto taper = std::sin (position * juce::MathConstants<float>::halfPi);
            impulse.applyGain (sample, 1, taper);
        }
        convolution.loadImpulseResponse (std::move (impulse), reader->sampleRate,
            juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes,
            juce::dsp::Convolution::Normalise::yes);
    }
    loadedImpulse = index;
}

void SpringModule::process (juce::AudioBuffer<float>& buffer)
{
    if (! wetMix.isSmoothing() && wetMix.getCurrentValue() <= 0.00001f)
        return;

    const auto samples = buffer.getNumSamples();
    const auto channels = juce::jmin (buffer.getNumChannels(), channelCount);
    if (samples > maximumBlockSize || channels == 0) return;
    for (int channel = 0; channel < channels; ++channel)
        wetBuffer.copyFrom (channel, 0, buffer, channel, 0, samples);

    // Dwell drives the spring transducer before the IR. A parallel clean path
    // keeps low settings open, while the asymmetric soft clip adds the dense,
    // slightly compressed excitation of a harder-driven tank.
    if (dwellAmount > 0.0001f)
    {
        const auto dwellGain = 1.0f + dwellAmount * 3.6f;
        for (int channel = 0; channel < channels; ++channel)
            for (int sample = 0; sample < samples; ++sample)
            {
                const auto input = wetBuffer.getSample (channel, sample);
                const auto bias = 0.035f * dwellAmount;
                const auto dc = std::tanh (bias * dwellGain);
                const auto driven = (std::tanh ((input + bias) * dwellGain) - dc)
                                  / juce::jmax (1.0f, dwellGain * 0.72f);
                wetBuffer.setSample (channel, sample, juce::jmap (dwellAmount * 0.72f,
                                                                  input, driven)
                                                   * (1.0f + dwellAmount * 0.24f));
            }
    }

    // Drip is also an excitation of the tank, not a bright layer pasted onto
    // the convolved output. Detect pick onsets, shape them through the resonant
    // band and feed that energy into the same IR as the main guitar signal.
    const auto dripEnabled = dripAmount > 0.0001f;
    if (dripEnabled)
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            for (int sample = 0; sample < samples; ++sample)
            {
                const auto input = wetBuffer.getSample (channel, sample);
                auto& envelope = dripEnvelope[static_cast<size_t> (channel)];
                const auto magnitude = std::abs (input);
                envelope += dripDetectorCoefficient * (magnitude - envelope);
                const auto onset = juce::jlimit (0.0f, 1.0f, (magnitude - envelope) * 8.0f);
                dripBuffer.setSample (channel, sample, input * onset);
            }
        }
        juce::dsp::AudioBlock<float> dripBlock (dripBuffer);
        auto activeDrip = dripBlock.getSubBlock (0, static_cast<size_t> (samples));
        juce::dsp::ProcessContextReplacing<float> dripContext (activeDrip);
        dripFilter.process (dripContext);
        for (int channel = 0; channel < channels; ++channel)
            wetBuffer.addFrom (channel, 0, dripBuffer, channel, 0, samples,
                               dripAmount * 0.82f);
    }

    juce::dsp::AudioBlock<float> wetBlock (wetBuffer);
    auto activeWet = wetBlock.getSubBlock (0, static_cast<size_t> (samples));
    juce::dsp::ProcessContextReplacing<float> wetContext (activeWet);
    convolution.process (wetContext);
    bodyFilter.process (wetContext);
    toneFilter.process (wetContext);

    // Model-specific dispersive paths represent interacting physical springs.
    // 201 uses two darker springs, 9100 uses three balanced springs, and Tape
    // uses two more uneven paths for a characterful mechanical response.
    constexpr float dispersionTimesMs[3][3] {
        { 4.7f, 8.1f, 11.6f },
        { 3.8f, 6.5f, 10.2f },
        { 5.4f, 9.2f, 14.3f }
    };
    const auto springCount = currentModel == 1 ? 3 : 2;
    const auto dispersionSize = dispersionBuffer.getNumSamples();
    const auto dispersionFeedback = 0.16f + decayAmount * 0.15f
                                  + (currentModel == 2 ? 0.035f : 0.0f);
    const auto dispersionDamping = 0.16f + toneAmount * 0.34f;
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto wetLeft = wetBuffer.getSample (0, sample);
        const auto wetRight = channels > 1 ? wetBuffer.getSample (1, sample) : wetLeft;
        const auto excitation = (wetLeft + wetRight) * 0.5f;
        float springTaps[3] {};
        for (int spring = 0; spring < springCount; ++spring)
        {
            const auto distance = juce::jmin (dispersionSize - 1,
                juce::roundToInt (static_cast<float> (sampleRate)
                                  * dispersionTimesMs[currentModel][spring] * 0.001f));
            const auto readIndex = (dispersionWriteIndex - distance + dispersionSize) % dispersionSize;
            const auto delayed = dispersionBuffer.getSample (spring, readIndex);
            auto& damped = dispersionDampingState[static_cast<size_t> (spring)];
            damped += dispersionDamping * (delayed - damped);
            springTaps[spring] = damped;
            const auto drive = excitation * (0.52f + dripAmount * 0.10f)
                             + damped * dispersionFeedback;
            dispersionBuffer.setSample (spring, dispersionWriteIndex, std::tanh (drive));
        }
        const auto normalise = springCount == 3 ? 0.333f : 0.5f;
        const auto common = (springTaps[0] + springTaps[1] + springTaps[2]) * normalise;
        const auto side = (springTaps[0] - springTaps[1]
                         + springTaps[2] * 0.55f) * 0.16f;
        const auto dispersionLevel = 0.18f + dripAmount * 0.10f;
        wetBuffer.setSample (0, sample, wetLeft + (common + side) * dispersionLevel);
        if (channels > 1)
            wetBuffer.setSample (1, sample, wetRight + (common - side) * dispersionLevel);
        dispersionWriteIndex = (dispersionWriteIndex + 1) % dispersionSize;
    }

    // The IR supplies the mechanical attack and tank identity. A four-line
    // damped feedback matrix supplies a dense, smoothly decaying late field,
    // following the hybrid onset-plus-synthesised-tail design used by mature
    // spring processors. The orthogonal matrix prevents one channel or one
    // resonance from dominating the decay.
    const auto tailSize = tailBuffer.getNumSamples();
    const auto decaySeconds = 0.8f * std::pow (12.5f, decayAmount);
    // Feedback required for approximately -60 dB after the target decay time,
    // using the late network's representative 47 ms circulation time.
    const auto tailFeedback = std::pow (0.001f, 0.047f / decaySeconds);
    const auto tailDamping = 0.055f + toneAmount * 0.16f;
    constexpr float delayTimesMs[] { 29.7f, 37.1f, 47.9f, 61.3f };
    for (int sample = 0; sample < samples; ++sample)
    {
        float delayed[4] {};
        const auto modSin = std::sin (tailModPhase);
        const auto modCos = std::cos (tailModPhase);
        const float modulation[4] { modSin, modCos, -modSin, -modCos };
        const auto modulationDepthSamples = static_cast<float> (sampleRate)
            * (0.00016f + decayAmount * 0.00022f);
        for (int line = 0; line < 4; ++line)
        {
            const auto delaySamples = juce::jmin (static_cast<float> (tailSize - 2),
                static_cast<float> (sampleRate) * delayTimesMs[line] * 0.001f
                    + modulation[line] * modulationDepthSamples);
            auto readPosition = static_cast<float> (tailWriteIndex) - delaySamples;
            while (readPosition < 0.0f) readPosition += static_cast<float> (tailSize);
            const auto first = static_cast<int> (readPosition) % tailSize;
            const auto second = (first + 1) % tailSize;
            delayed[line] = juce::jmap (readPosition - static_cast<float> (first),
                                        tailBuffer.getSample (line, first),
                                        tailBuffer.getSample (line, second));
        }

        const float matrix[4] {
            (delayed[0] + delayed[1] + delayed[2] + delayed[3]) * 0.5f,
            (delayed[0] - delayed[1] + delayed[2] - delayed[3]) * 0.5f,
            (delayed[0] + delayed[1] - delayed[2] - delayed[3]) * 0.5f,
            (delayed[0] - delayed[1] - delayed[2] + delayed[3]) * 0.5f
        };
        const auto wetLeft = wetBuffer.getSample (0, sample);
        const auto wetRight = channels > 1 ? wetBuffer.getSample (1, sample) : wetLeft;
        const float excitation[4] { wetLeft, wetRight,
                                    (wetLeft + wetRight) * 0.7071f,
                                    (wetLeft - wetRight) * 0.7071f };
        for (int line = 0; line < 4; ++line)
        {
            auto& damped = tailDampingState[static_cast<size_t> (line)];
            damped += tailDamping * (matrix[line] - damped);
            tailBuffer.setSample (line, tailWriteIndex,
                std::tanh (excitation[line] * 0.40f + damped * tailFeedback));
        }
        const auto lateGain = 0.16f + decayAmount * 0.05f;
        const auto lateLeft = (delayed[0] + delayed[2] - delayed[1] * 0.35f) * lateGain;
        const auto lateRight = (delayed[1] + delayed[3] - delayed[0] * 0.35f) * lateGain;
        wetBuffer.setSample (0, sample, wetLeft + lateLeft);
        if (channels > 1)
            wetBuffer.setSample (1, sample, wetRight + lateRight);
        tailWriteIndex = (tailWriteIndex + 1) % tailSize;
        tailModPhase += juce::MathConstants<float>::twoPi * 0.085f
                      / static_cast<float> (sampleRate);
        if (tailModPhase >= juce::MathConstants<float>::twoPi)
            tailModPhase -= juce::MathConstants<float>::twoPi;
    }

    // Normalised IRs already carry ample tail energy. Avoid the previous gain
    // boost that made long decays swamp the source as Mix increased.
    const auto tailGain = juce::jmap (decayAmount, 0.94f, 1.18f);
    const auto mixIsSmoothing = wetMix.isSmoothing();
    const auto steadyMix = wetMix.getCurrentValue();
    // Rockalizer is an insert-style guitar effect, not a 100%-wet aux return.
    // Preserve enough direct signal at high Mix for the note and pick attack
    // to remain present while the spring grows into a dense wash.
    const auto mixGains = [] (float mix)
    {
        mix = juce::jlimit (0.0f, 1.0f, mix);
        const auto shapedMix = std::pow (mix, 0.82f);
        // Keep the familiar direct anchor through ordinary insert settings,
        // then transition deliberately toward a wet, ambient wash above 65%.
        const auto highMix = juce::jlimit (0.0f, 1.0f, (mix - 0.65f) / 0.35f);
        const auto wash = std::pow (highMix, 1.35f);
        const auto dryGain = 1.0f - mix * 0.18f - wash * 0.38f;
        const auto wetGain = shapedMix * (1.04f + wash * 0.08f);
        return std::pair { dryGain, wetGain };
    };
    const auto steadyGains = mixGains (steadyMix);
    const auto steadyDryGain = steadyGains.first;
    const auto steadyWetGain = steadyGains.second;
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto mix = mixIsSmoothing ? wetMix.getNextValue() : steadyMix;
        const auto gains = mixIsSmoothing ? mixGains (mix)
                                          : std::pair { steadyDryGain, steadyWetGain };
        const auto dryGain = gains.first;
        const auto wetGain = gains.second;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto dry = buffer.getSample (channel, sample);
            auto& inputEnv = inputEnvelope[static_cast<size_t> (channel)];
            const auto magnitude = std::abs (dry);
            const auto envelopeCoefficient = magnitude > inputEnv ? envelopeAttack : envelopeRelease;
            inputEnv += envelopeCoefficient * (magnitude - inputEnv);
            const auto wet = wetBuffer.getSample (channel, sample) * tailGain;
            // A small amount of input-aware ducking keeps pick attack and note
            // body forward; the spring naturally blooms as the note relaxes.
            const auto ducking = 1.0f - juce::jlimit (0.0f, 1.0f, inputEnv * 3.8f) * 0.08f;
            buffer.setSample (channel, sample, dry * dryGain + wet * wetGain * ducking);
        }
    }
}
