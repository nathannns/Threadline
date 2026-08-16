#include "ChorusModule.h"

void ChorusModule::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    const auto channels = static_cast<int> (spec.numChannels);
    delayBuffer.setSize (channels, static_cast<int> (sampleRate * 0.04) + 4);
    delayBuffer.clear();
    toneState.assign (static_cast<size_t> (channels), 0.0f);
    warmBodyState.assign (static_cast<size_t> (channels), 0.0f);
    crossLowState.assign (static_cast<size_t> (channels), 0.0f);
    feedbackState.assign (static_cast<size_t> (channels), 0.0f);
    companderEnvelope.assign (static_cast<size_t> (channels), 0.0f);
    crossLowCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                            * 180.0f / static_cast<float> (sampleRate));
    warmBodyCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                            * 720.0f / static_cast<float> (sampleRate));
    companderAttack = 1.0f - std::exp (-1.0f / (static_cast<float> (sampleRate) * 0.004f));
    companderRelease = 1.0f - std::exp (-1.0f / (static_cast<float> (sampleRate) * 0.140f));
    cachedToneHz = -1.0f;

    for (auto* value : { &rateValue, &depthValue, &widthValue, &wetMix, &flangerBlend, &aggressionValue })
        value->reset (sampleRate, 0.03);
    reset();
}

void ChorusModule::reset()
{
    std::fill (toneState.begin(), toneState.end(), 0.0f);
    std::fill (warmBodyState.begin(), warmBodyState.end(), 0.0f);
    std::fill (crossLowState.begin(), crossLowState.end(), 0.0f);
    std::fill (feedbackState.begin(), feedbackState.end(), 0.0f);
    std::fill (companderEnvelope.begin(), companderEnvelope.end(), 0.0f);
    writeIndex = 0;
    validSamples = 0;
    lfoPhase = 0.0f;
    secondaryPhase = juce::MathConstants<float>::halfPi;
    rateValue.setCurrentAndTargetValue (0.32f);
    depthValue.setCurrentAndTargetValue (0.75f);
    widthValue.setCurrentAndTargetValue (0.75f);
    wetMix.setCurrentAndTargetValue (0.0f);
    flangerBlend.setCurrentAndTargetValue (0.0f);
    aggressionValue.setCurrentAndTargetValue (0.0f);
}

void ChorusModule::setParameters (float rateHz, float depthPercent, float widthPercent,
                                  float toneHz, float mixPercent, bool enabled, int flangerMode)
{
    // Dimension-style range: slow, shallow dual modulation creates width and
    // depth without the obvious pitch sweep of a conventional chorus.
    const auto normalisedRate = juce::jlimit (0.0f, 1.0f, (rateHz - 0.05f) / 4.95f);
    const auto flange = juce::jlimit (0, 3, flangerMode);
    const auto flangerEnabled = flange > 0;
    rateValue.setTargetValue (flange == 3
        ? 1.45f + std::pow (normalisedRate, 0.70f) * 2.80f
        : (flange == 2 ? 1.10f + std::pow (normalisedRate, 0.70f) * 2.40f
        : (flange == 1 ? 0.58f + std::pow (normalisedRate, 0.70f) * 1.30f
                       : 0.08f + std::pow (normalisedRate, 0.72f) * 0.62f)));
    depthValue.setTargetValue (std::pow (juce::jlimit (0.0f, 1.0f, depthPercent * 0.01f), 0.72f));
    widthValue.setTargetValue (juce::jlimit (0.0f, 1.0f, widthPercent * 0.01f));
    const auto limitedToneHz = juce::jlimit (1800.0f, 16000.0f, toneHz);
    if (std::abs (limitedToneHz - cachedToneHz) > 0.01f)
    {
        cachedToneHz = limitedToneHz;
        toneCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                            * limitedToneHz / static_cast<float> (sampleRate));
    }
    const auto requestedMix = juce::jlimit (0.0f, 1.0f, mixPercent * 0.01f);
    // The one-button flanger is deliberately a finished sound: enough delayed
    // signal for audible comb notches even when the Chorus Mix is set low.
    const auto minimumFlangerMix = flange == 3 ? 0.64f : (flange == 2 ? 0.56f : 0.42f);
    // A perceptual taper makes low settings immediately useful, while 100%
    // reaches a clearly effected CE/JUNO-like ensemble instead of remaining
    // mostly dry as the earlier calibration did.
    const auto chorusMix = juce::jmin (1.0f, std::pow (requestedMix, 0.78f) * 1.15f);
    wetMix.setTargetValue (enabled ? (flangerEnabled ? juce::jmax (minimumFlangerMix, requestedMix * 0.82f)
                                                     : chorusMix)
                                   : 0.0f);
    flangerBlend.setTargetValue (flange >= 2 ? 1.0f : (flange == 1 ? 0.72f : 0.0f));
    aggressionValue.setTargetValue (flange == 3 ? 1.0f : 0.0f);
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
    if (! wetMix.isSmoothing() && wetMix.getCurrentValue() <= 0.00001f)
        return;

    const auto channels = juce::jmin (2, buffer.getNumChannels(), delayBuffer.getNumChannels());
    if (channels == 0)
        return;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto mode = flangerBlend.getNextValue();
        const auto aggression = aggressionValue.getNextValue();
        const auto baseDelayA = static_cast<float> (sampleRate) * juce::jmap (mode, 0.0078f, 0.00075f);
        const auto baseDelayB = static_cast<float> (sampleRate) * juce::jmap (mode, 0.0114f, 0.00185f);
        const auto baseDelayC = static_cast<float> (sampleRate) * juce::jmap (mode, 0.0152f, 0.00245f);
        const auto baseDelayD = static_cast<float> (sampleRate) * juce::jmap (mode, 0.0198f, 0.00295f);
        float original[2] {};
        float wet[2] {};
        for (int channel = 0; channel < channels; ++channel)
        {
            original[channel] = buffer.getSample (channel, sample);
            const auto other = channels >= 2 ? 1 - channel : channel;
            // Feed-forward voices provide the Chorus density. Feedback is
            // reserved for Flanger mode, preventing metallic ringing and
            // keeping long guitar notes smooth and even.
            const auto feedbackAmount = juce::jmin (0.72f,
                juce::jmap (mode, 0.0f, 0.58f) + aggression * 0.12f);
            const auto feedbackChannel = mode > 0.5f ? channel : other;
            const auto crossFeedback = feedbackState[static_cast<size_t> (feedbackChannel)]
                                     * feedbackAmount;
            auto& envelope = companderEnvelope[static_cast<size_t> (channel)];
            const auto magnitude = std::abs (original[channel]);
            const auto coefficient = magnitude > envelope ? companderAttack : companderRelease;
            envelope += coefficient * (magnitude - envelope);
            // A restrained compander drives the BBD path more evenly. The
            // matching expansion below restores transient shape, giving the
            // smooth, glued movement associated with the rack circuit.
            const auto compressorGain = 1.0f / std::sqrt (1.0f + envelope * 2.8f);
            const auto bbdInput = original[channel] * compressorGain + crossFeedback * 1.08f;
            delayBuffer.setSample (channel, writeIndex, std::tanh (bbdInput * 1.06f));
        }

        const auto rate = rateValue.getNextValue();
        const auto depth = depthValue.getNextValue();
        const auto width = widthValue.getNextValue();
        const auto mix = wetMix.getNextValue();
        const auto depthSamples = static_cast<float> (sampleRate)
                                * juce::jmap (mode,
                                              0.00010f + depth * 0.00134f,
                                              0.00018f + depth * 0.00135f)
                                * (1.0f + aggression * 0.20f);

        // Sine-derived voices have continuous velocity and acceleration. This
        // removes the slightly mechanical corners of the previous triangle
        // sweep while keeping three decorrelated Dimension-style delay taps.
        const auto phaseSin = std::sin (lfoPhase);
        const auto phaseCos = std::cos (lfoPhase);
        const auto slowSin = std::sin (secondaryPhase);
        // Chorus keeps broad stereo phase separation. Flanger converges to a
        // shared sweep so it remains focused rather than auto-panning.
        const auto stereoPhase = juce::MathConstants<float>::pi * 0.72f
                               * (1.0f - mode * 0.985f);
        const auto stereoSin = std::sin (stereoPhase);
        const auto stereoCos = std::cos (stereoPhase);

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto voiceA = channel == 0 ? phaseSin
                                              : phaseSin * stereoCos + phaseCos * stereoSin;
            const auto voiceB = channel == 0 ? phaseCos
                                              : phaseCos * stereoCos - phaseSin * stereoSin;
            const auto voiceC = -voiceA;
            const auto voiceD = channel == 0 ? slowSin : -slowSin;
            const auto tapA = readDelay (channel, juce::jmax (1.0f, baseDelayA + voiceA * depthSamples));
            const auto tapB = readDelay (channel, juce::jmax (1.0f, baseDelayB - voiceB * depthSamples * 0.68f));
            const auto tapC = readDelay (channel, juce::jmax (1.0f, baseDelayC + voiceC * depthSamples * 0.44f));
            const auto tapD = readDelay (channel, juce::jmax (1.0f, baseDelayD
                                                   + voiceD * depthSamples * 0.31f));
            auto dimensionWet = tapA * juce::jmap (mode, 0.30f, 0.76f)
                              + tapB * juce::jmap (mode, 0.28f, 0.24f)
                              + tapC * juce::jmap (mode, 0.23f, 0.0f)
                              + tapD * juce::jmap (mode, 0.19f, 0.0f);
            // Gentle BBD/compander rounding adds CE-1 warmth and glues the
            // three Dimension/JUNO-inspired delay voices into one ensemble.
            if (mode < 0.5f)
            {
                const auto rounded = std::tanh (dimensionWet * 1.16f) / 1.16f;
                dimensionWet = juce::jmap (0.30f, dimensionWet, rounded);
                // A restrained low-mid return resembles the gentle spectral
                // tilt of a BBD/compander path without muddying the dry guitar.
                dimensionWet += warmBodyState[static_cast<size_t> (channel)] * 0.075f;
                const auto expansion = 1.0f + juce::jmin (0.14f,
                    companderEnvelope[static_cast<size_t> (channel)] * 0.46f);
                dimensionWet *= expansion;
            }
            auto& body = warmBodyState[static_cast<size_t> (channel)];
            body += warmBodyCoefficient * (dimensionWet - body);
            auto& state = toneState[static_cast<size_t> (channel)];
            state += toneCoefficient * (dimensionWet - state);
            wet[channel] = state;
            feedbackState[static_cast<size_t> (channel)] = state;
        }

        if (channels >= 2 && width > 0.0001f)
        {
            const auto rawLeft = wet[0];
            const auto rawRight = wet[1];
            crossLowState[0] += crossLowCoefficient * (rawRight - crossLowState[0]);
            crossLowState[1] += crossLowCoefficient * (rawLeft - crossLowState[1]);
            const auto crossAmount = juce::jmap (mode, 0.035f + width * 0.16f,
                                                       0.005f + width * 0.01f);
            wet[0] = rawLeft - (rawRight - crossLowState[0]) * crossAmount;
            wet[1] = rawRight - (rawLeft - crossLowState[1]) * crossAmount;
        }

        for (int channel = 0; channel < channels; ++channel)
        {
            // Retain a stable direct anchor as Mix rises. The four decorrelated
            // wet voices provide density without relying on a loud, phasey
            // wet path that makes sustained notes breathe in and out.
            const auto chorusDryGain = 1.0f - mix * 0.18f;
            const auto chorusWetGain = mix * 1.10f;
            // Near-equal dry/delayed levels deepen the moving comb nulls that
            // define flanging. Output is trimmed to avoid a loudness jump.
            // Keep a stronger direct path in Flanger mode so the moving comb
            // does not scoop away the guitar's midrange and body.
            const auto dryGain = juce::jmap (mode, chorusDryGain, 0.85f);
            const auto wetGain = juce::jmap (mode, chorusWetGain, 0.58f);
            buffer.setSample (channel, sample,
                original[channel] * dryGain + wet[channel] * wetGain);
        }

        writeIndex = (writeIndex + 1) % delayBuffer.getNumSamples();
        validSamples = juce::jmin (validSamples + 1, delayBuffer.getNumSamples());
        lfoPhase += juce::MathConstants<float>::twoPi * rate / static_cast<float> (sampleRate);
        if (lfoPhase >= juce::MathConstants<float>::twoPi)
            lfoPhase -= juce::MathConstants<float>::twoPi;
        secondaryPhase += juce::MathConstants<float>::twoPi * rate * 0.37f
                        / static_cast<float> (sampleRate);
        if (secondaryPhase >= juce::MathConstants<float>::twoPi)
            secondaryPhase -= juce::MathConstants<float>::twoPi;
    }
}
