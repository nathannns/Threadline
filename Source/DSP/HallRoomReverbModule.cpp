#include "HallRoomReverbModule.h"

void HallRoomReverbModule::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    maximumBlockSize = static_cast<int> (spec.maximumBlockSize);
    channelCount = static_cast<int> (spec.numChannels);
    wetBuffer.setSize (channelCount, maximumBlockSize);

    convolution.prepare (spec);
    dampingFilter.prepare (spec);
    rumbleFilter.prepare (spec);
    dampingFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    rumbleFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    rumbleFilter.setCutoffFrequency (85.0f); // fixed: keeps the tail from building up mud

    preDelayLine.prepare (spec);
    preDelayLine.setMaximumDelayInSamples (static_cast<int> (sampleRate * 0.1) + 32); // up to 100ms + modulation headroom

    cachedToneHz = -1.0f;
    wetMix.reset (spec.sampleRate, 0.03);
    reset();
    triggerAsyncUpdate();
}

void HallRoomReverbModule::reset()
{
    convolution.reset();
    dampingFilter.reset();
    rumbleFilter.reset();
    preDelayLine.reset();
    modPhaseLeft = 0.0f;
    modPhaseRight = juce::MathConstants<float>::pi;
    wetMix.setCurrentAndTargetValue (0.0f);
}

void HallRoomReverbModule::setParameters (float preDelayNormalised, float toneNormalised, float mix,
                                          float widthPercent, bool enabled, int modelIndex)
{
    widthFactor = juce::jmap (juce::jlimit (0.0f, 100.0f, widthPercent), 0.0f, 100.0f, 0.0f, 2.0f);

    basePreDelaySamples = static_cast<float> (sampleRate)
        * juce::jmap (juce::jlimit (0.0f, 1.0f, preDelayNormalised), 0.0f, 80.0f) * 0.001f;

    const auto toneHz = juce::jmap (juce::jlimit (0.0f, 1.0f, toneNormalised), 1400.0f, 15000.0f);
    if (std::abs (toneHz - cachedToneHz) > 0.01f)
    {
        cachedToneHz = toneHz;
        dampingFilter.setCutoffFrequency (toneHz);
    }

    wetMix.setTargetValue (enabled ? juce::jlimit (0.0f, 1.0f, mix * 0.01f) : 0.0f);

    modelIndex = juce::jlimit (0, numModels - 1, modelIndex);
    const auto impulseChanged = requestedImpulse.exchange (modelIndex) != modelIndex;
    if (impulseChanged || loadedImpulse != modelIndex)
        triggerAsyncUpdate();
}

void HallRoomReverbModule::handleAsyncUpdate()
{
    const auto impulse = requestedImpulse.load();
    loadImpulse (impulse);
    if (impulse != requestedImpulse.load())
        triggerAsyncUpdate();
}

void HallRoomReverbModule::loadImpulse (int index)
{
    const void* data[numModels] {
        BinaryData::large_hall_wav, BinaryData::large_stage_wav, BinaryData::small_church_wav,
        BinaryData::small_hall_wav, BinaryData::small_stage_wav, BinaryData::large_room_wav,
        BinaryData::small_room_wav
    };
    const int sizes[numModels] {
        BinaryData::large_hall_wavSize, BinaryData::large_stage_wavSize, BinaryData::small_church_wavSize,
        BinaryData::small_hall_wavSize, BinaryData::small_stage_wavSize, BinaryData::large_room_wavSize,
        BinaryData::small_room_wavSize
    };

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (
        new juce::MemoryInputStream (data[index], static_cast<size_t> (sizes[index]), false), true));
    if (reader != nullptr)
    {
        // Unlike the spring IRs, these Lexicon 480L captures are complete,
        // natural decays — load the whole thing rather than a short onset
        // plus a synthesized tail. Convolution::Trim removes any near-silent
        // head/tail without touching the audible decay itself.
        const auto length = static_cast<int> (reader->lengthInSamples);
        const auto channels = juce::jlimit (1, 2, static_cast<int> (reader->numChannels));
        juce::AudioBuffer<float> impulse (channels, juce::jmax (1, length));
        reader->read (&impulse, 0, length, 0, true, channels > 1);
        convolution.loadImpulseResponse (std::move (impulse), reader->sampleRate,
            juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes,
            juce::dsp::Convolution::Normalise::yes);
    }
    loadedImpulse = index;
}

void HallRoomReverbModule::process (juce::AudioBuffer<float>& buffer)
{
    if (! wetMix.isSmoothing() && wetMix.getCurrentValue() <= 0.00001f)
        return;

    const auto samples = buffer.getNumSamples();
    const auto channels = juce::jmin (buffer.getNumChannels(), channelCount);
    if (samples > maximumBlockSize || channels == 0) return;

    for (int channel = 0; channel < channels; ++channel)
        wetBuffer.copyFrom (channel, 0, buffer, channel, 0, samples);

    // Pre-delay, with a slow sub-millisecond modulation that's inverted
    // between L/R so the two channels decorrelate slightly instead of
    // tracking a single static delay — see file header for why.
    constexpr auto modRateHz = 0.11f;
    constexpr auto modDepthMs = 0.35f;
    const auto modDepthSamples = static_cast<float> (sampleRate) * modDepthMs * 0.001f;
    const auto modIncrement = juce::MathConstants<float>::twoPi * modRateHz / static_cast<float> (sampleRate);
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto leftDelay = basePreDelaySamples + std::sin (modPhaseLeft) * modDepthSamples;
        const auto rightDelay = basePreDelaySamples + std::sin (modPhaseRight) * modDepthSamples;

        if (channels > 0)
        {
            preDelayLine.setDelay (juce::jmax (0.0f, leftDelay));
            preDelayLine.pushSample (0, wetBuffer.getSample (0, sample));
            wetBuffer.setSample (0, sample, preDelayLine.popSample (0));
        }
        if (channels > 1)
        {
            preDelayLine.setDelay (juce::jmax (0.0f, rightDelay));
            preDelayLine.pushSample (1, wetBuffer.getSample (1, sample));
            wetBuffer.setSample (1, sample, preDelayLine.popSample (1));
        }

        modPhaseLeft += modIncrement;
        modPhaseRight += modIncrement;
        if (modPhaseLeft >= juce::MathConstants<float>::twoPi) modPhaseLeft -= juce::MathConstants<float>::twoPi;
        if (modPhaseRight >= juce::MathConstants<float>::twoPi) modPhaseRight -= juce::MathConstants<float>::twoPi;
    }

    juce::dsp::AudioBlock<float> wetBlock (wetBuffer);
    auto activeWet = wetBlock.getSubBlock (0, static_cast<size_t> (samples));
    juce::dsp::ProcessContextReplacing<float> wetContext (activeWet);
    convolution.process (wetContext);
    rumbleFilter.process (wetContext);
    dampingFilter.process (wetContext);

    // Width: simple M/S scaling of the wet signal only (dry stays untouched),
    // so it reshapes the reverb's own stereo image rather than the guitar's.
    if (channels > 1 && std::abs (widthFactor - 1.0f) > 0.001f)
    {
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto left = wetBuffer.getSample (0, sample);
            const auto right = wetBuffer.getSample (1, sample);
            const auto mid = (left + right) * 0.5f;
            const auto side = (left - right) * 0.5f * widthFactor;
            wetBuffer.setSample (0, sample, mid + side);
            wetBuffer.setSample (1, sample, mid - side);
        }
    }

    // Equal-power crossfade reads as a smoother, more natural blend across
    // the Mix range than a linear one (no perceived dip or hump at 50%).
    const auto mixIsSmoothing = wetMix.isSmoothing();
    const auto steadyMix = wetMix.getCurrentValue();
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto mix = mixIsSmoothing ? wetMix.getNextValue() : steadyMix;
        const auto dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
        const auto wetGain = std::sin (mix * juce::MathConstants<float>::halfPi);
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto dry = buffer.getSample (channel, sample);
            const auto wet = wetBuffer.getSample (channel, sample);
            buffer.setSample (channel, sample, dry * dryGain + wet * wetGain);
        }
    }
}
