#include "ChorusModule.h"

void ChorusModule::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    const auto channels = static_cast<int> (spec.numChannels);
    // Max Lag (~30ms) + max Depth excursion (~8ms) + margin.
    delayBuffer.setSize (channels, static_cast<int> (sampleRate * 0.06) + 4);
    delayBuffer.clear();
    companderEnvelope.assign (static_cast<size_t> (channels), 0.0f);
    bbdToneState.assign (static_cast<size_t> (channels), 0.0f);
    bbdToneCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                           * 7000.0f / static_cast<float> (sampleRate));
    companderAttack = 1.0f - std::exp (-1.0f / (static_cast<float> (sampleRate) * 0.004f));
    companderRelease = 1.0f - std::exp (-1.0f / (static_cast<float> (sampleRate) * 0.140f));

    for (auto* value : { &rateValue, &depthValue, &lagValue, &dcvValue, &waveformBlend })
        value->reset (sampleRate, 0.03);
    reset();
}

void ChorusModule::reset()
{
    std::fill (companderEnvelope.begin(), companderEnvelope.end(), 0.0f);
    std::fill (bbdToneState.begin(), bbdToneState.end(), 0.0f);
    writeIndex = 0;
    validSamples = 0;
    lfoPhase = 0.0f;
    rateValue.setCurrentAndTargetValue (0.32f);
    depthValue.setCurrentAndTargetValue (0.5f);
    lagValue.setCurrentAndTargetValue (0.3f);
    dcvValue.setCurrentAndTargetValue (0.0f);
    waveformBlend.setCurrentAndTargetValue (0.0f);
}

void ChorusModule::setParameters (float rateHz, float depthPercent, float lagPercent,
                                  Waveform waveform, float dcvPercent, bool enabled)
{
    // Rate: slow lush chorus through fast vibrato flutter, same span
    // Julia's own copy describes ("turn clockwise... for a faster,
    // fluttering effect").
    const auto normalisedRate = juce::jlimit (0.0f, 1.0f, (rateHz - 0.05f) / 4.95f);
    rateValue.setTargetValue (0.08f + std::pow (normalisedRate, 0.72f) * 5.92f);
    depthValue.setTargetValue (std::pow (juce::jlimit (0.0f, 1.0f, depthPercent * 0.01f), 0.72f));
    lagValue.setTargetValue (juce::jlimit (0.0f, 1.0f, lagPercent * 0.01f));
    waveformBlend.setTargetValue (waveform == Waveform::triangle ? 1.0f : 0.0f);
    dcvValue.setTargetValue (enabled ? juce::jlimit (0.0f, 1.0f, dcvPercent * 0.01f) : 0.0f);
}

float ChorusModule::readDelay (int channel, float distance) const
{
    if (distance > static_cast<float> (validSamples))
        return 0.0f;
    const auto size = delayBuffer.getNumSamples();
    auto position = static_cast<float> (writeIndex) - distance;
    while (position < 0.0f) position += static_cast<float> (size);
    while (position >= static_cast<float> (size)) position -= static_cast<float> (size);
    const auto first = static_cast<int> (position);
    const auto second = (first + 1) % size;
    return juce::jmap (position - static_cast<float> (first),
                       delayBuffer.getSample (channel, first),
                       delayBuffer.getSample (channel, second));
}

void ChorusModule::process (juce::AudioBuffer<float>& buffer)
{
    if (! dcvValue.isSmoothing() && dcvValue.getCurrentValue() <= 0.00001f)
        return;

    const auto channels = juce::jmin (2, buffer.getNumChannels(), delayBuffer.getNumChannels());
    if (channels == 0)
        return;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        float original[2] {};
        for (int channel = 0; channel < channels; ++channel)
        {
            original[channel] = buffer.getSample (channel, sample);
            auto& envelope = companderEnvelope[static_cast<size_t> (channel)];
            const auto magnitude = std::abs (original[channel]);
            const auto coefficient = magnitude > envelope ? companderAttack : companderRelease;
            envelope += coefficient * (magnitude - envelope);
            // A restrained compander drives the BBD path more evenly; the
            // matching expansion below (on the wet output) restores
            // transient shape, giving the smooth, glued movement associated
            // with a real bucket-brigade circuit.
            const auto compressorGain = 1.0f / std::sqrt (1.0f + envelope * 2.8f);
            delayBuffer.setSample (channel, writeIndex, std::tanh (original[channel] * compressorGain * 1.06f));
        }

        const auto rate = rateValue.getNextValue();
        const auto depth = depthValue.getNextValue();
        const auto lag = lagValue.getNextValue();
        const auto dcv = dcvValue.getNextValue();
        const auto wave = waveformBlend.getNextValue();

        // Lag: the LFO's center delay time — "taut and fluid" at low
        // settings through "an all-out sluggish detune" at max, per Julia's
        // own description. 3-30ms spans a typical BBD chorus/vibrato's
        // tight-to-loose range.
        const auto centerDelaySamples = static_cast<float> (sampleRate)
                                      * juce::jmap (lag, 3.0f, 30.0f) * 0.001f;
        // Depth: excursion around that center, up to ~8ms — enough for a
        // genuine, audible wobble.
        const auto depthSamples = static_cast<float> (sampleRate) * depth * 0.008f;

        // A triangle wave built from asin(sin(x)) has no discontinuity at
        // the phase wrap, unlike a naive sawtooth-folded construction.
        const auto sineLfo = std::sin (lfoPhase);
        const auto triangleLfo = (2.0f / juce::MathConstants<float>::pi) * std::asin (sineLfo);
        const auto lfo = juce::jmap (wave, sineLfo, triangleLfo);

        const auto distance = juce::jmax (1.0f, centerDelaySamples + lfo * depthSamples);

        float wet[2] {};
        for (int channel = 0; channel < channels; ++channel)
        {
            auto raw = readDelay (channel, distance);
            auto& state = bbdToneState[static_cast<size_t> (channel)];
            state += bbdToneCoefficient * (raw - state);
            const auto expansion = 1.0f + juce::jmin (0.14f,
                companderEnvelope[static_cast<size_t> (channel)] * 0.46f);
            wet[channel] = state * expansion;
        }

        for (int channel = 0; channel < channels; ++channel)
        {
            // D-C-V: a genuine dry/wet crossfade (equal-power). 0% is dry;
            // the middle is traditional chorus (dry blended with the
            // modulated delay -- the comb-filtered wobble that mix creates
            // IS what "chorus" is); 100% is pure vibrato (modulated delay
            // alone, no dry reference left to comb against).
            const auto dryGain = std::sqrt (1.0f - dcv);
            const auto wetGain = std::sqrt (dcv);
            buffer.setSample (channel, sample, original[channel] * dryGain + wet[channel] * wetGain);
        }

        writeIndex = (writeIndex + 1) % delayBuffer.getNumSamples();
        validSamples = juce::jmin (validSamples + 1, delayBuffer.getNumSamples());
        lfoPhase += juce::MathConstants<float>::twoPi * rate / static_cast<float> (sampleRate);
        if (lfoPhase >= juce::MathConstants<float>::twoPi)
            lfoPhase -= juce::MathConstants<float>::twoPi;
    }
}
