#include "DimensionChorusModule.h"

void DimensionChorusModule::prepare (const juce::dsp::ProcessSpec& spec)
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

void DimensionChorusModule::reset()
{
    std::fill (toneState.begin(), toneState.end(), 0.0f);
    std::fill (warmBodyState.begin(), warmBodyState.end(), 0.0f);
    std::fill (crossLowState.begin(), crossLowState.end(), 0.0f);
    std::fill (feedbackState.begin(), feedbackState.end(), 0.0f);
    std::fill (companderEnvelope.begin(), companderEnvelope.end(), 0.0f);
    for (auto& saturation : bbdSaturation) saturation.reset();
    for (auto& rounding : chorusRounding) rounding.reset();
    writeIndex = 0;
    validSamples = 0;
    lfoPhase = 0.0f;
    secondaryPhase = 0.0f;
    rateValue.setCurrentAndTargetValue (0.32f);
    depthValue.setCurrentAndTargetValue (0.75f);
    widthValue.setCurrentAndTargetValue (0.75f);
    wetMix.setCurrentAndTargetValue (0.0f);
    flangerBlend.setCurrentAndTargetValue (0.0f);
    aggressionValue.setCurrentAndTargetValue (0.0f);
}

void DimensionChorusModule::setParameters (float rateHz, float depthPercent, float widthPercent,
                                  float toneHz, float mixPercent, bool enabled, int flangerMode)
{
    // Dimension-style range: the SDD-320 itself runs its slow chorus modes
    // (the two most commonly reached for -- the subtle "invisible width"
    // Mode 1 and the slightly deeper Mode 2) at roughly a 2-4 second LFO
    // cycle, ~0.25-0.5Hz. 0.20-0.55Hz keeps that ballpark as the centre of
    // the Chorus rate range without boxing the knob in.
    const auto normalisedRate = juce::jlimit (0.0f, 1.0f, (rateHz - 0.05f) / 4.95f);
    const auto flange = juce::jlimit (0, 3, flangerMode);
    const auto flangerEnabled = flange > 0;
    rateValue.setTargetValue (flange == 3
        ? 1.45f + std::pow (normalisedRate, 0.70f) * 2.80f
        : (flange == 2 ? 1.10f + std::pow (normalisedRate, 0.70f) * 2.40f
        : (flange == 1 ? 0.58f + std::pow (normalisedRate, 0.70f) * 1.30f
                       : 0.20f + std::pow (normalisedRate, 0.72f) * 0.35f)));
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

float DimensionChorusModule::readDelay (int channel, float distance) const
{
    // 4-point Hermite (matching SpaceEchoModule::readDelay): Chorus's whole
    // character rides on smooth, continuous delay-time modulation over a
    // ~0.2-2.3ms sweep, which makes it the module most exposed to the
    // slope discontinuities 2-point linear interpolation introduces --
    // audible as alternating clicks, per Echo's own header comment on why
    // it doesn't use linear either.
    if (distance + 1.0f > static_cast<float> (validSamples))
        return 0.0f;
    const auto size = delayBuffer.getNumSamples();
    auto position = static_cast<float> (writeIndex) - distance;
    while (position < 0.0f) position += static_cast<float> (size);
    while (position >= static_cast<float> (size)) position -= static_cast<float> (size);
    const auto index1 = static_cast<int> (position);
    const auto index0 = (index1 - 1 + size) % size;
    const auto index2 = (index1 + 1) % size;
    const auto index3 = (index1 + 2) % size;
    const auto fraction = position - static_cast<float> (index1);
    const auto y0 = delayBuffer.getSample (channel, index0);
    const auto y1 = delayBuffer.getSample (channel, index1);
    const auto y2 = delayBuffer.getSample (channel, index2);
    const auto y3 = delayBuffer.getSample (channel, index3);
    const auto c0 = y1;
    const auto c1 = 0.5f * (y2 - y0);
    const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * fraction + c2) * fraction + c1) * fraction + c0;
}

void DimensionChorusModule::process (juce::AudioBuffer<float>& buffer)
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
            delayBuffer.setSample (channel, writeIndex, bbdSaturation[channel].process (bbdInput * 1.06f));
        }

        const auto rate = rateValue.getNextValue();
        const auto depth = depthValue.getNextValue();
        const auto width = widthValue.getNextValue();
        const auto mix = wetMix.getNextValue();
        // Chorus depth is calibrated toward the SDD-320's documented ~8-12ms
        // sweep window (a ~2ms swing around a ~10ms centre) rather than the
        // shallower sweep this used to use.
        const auto depthSamples = static_cast<float> (sampleRate)
                                * juce::jmap (mode,
                                              0.00020f + depth * 0.00230f,
                                              0.00018f + depth * 0.00135f)
                                * (1.0f + aggression * 0.20f);

        // The real SDD-320 drives two BBD lines from one LFO, one line taking
        // the LFO signal and the other its exact inverse -- when one delay
        // stretches the other shrinks by the same amount around a shared
        // centre. That antiphase mirroring is what gives Dimension-style
        // chorus its "3D, motionless" character. At chorus mode's slow rate
        // (real units run ~0.25-0.5Hz) the two channels genuinely do sit
        // apart in level for multi-second stretches before the LFO carries
        // them back into balance -- that's the real unit's behaviour, not a
        // defect, so it's only meaningful to judge stereo balance over a
        // window long enough to average across the sweep.
        //
        // The Chorus-to-Flanger blend needs a relationship in between full
        // antiphase and fully in-phase, and that has to be a phase ROTATION
        // rather than an amplitude blend: scaling the right channel's voice
        // by a fraction shrinks its modulation depth relative to the left
        // channel's, which is a permanent, non-averaging channel imbalance
        // at any in-between blend, not just a slow-converging one. A
        // rotation keeps both channels' depth identical (sin/cos of a phase
        // stay unit magnitude) no matter how the two are aligned. At exactly
        // pi (chorus) that degenerates to plain negation -- the authentic,
        // rate-independent case above -- while flanger modes rotate toward
        // roughly in phase, converging quickly since their rate is much
        // faster than chorus's, so it stays a focused comb rather than
        // auto-panning.
        const auto stereoPhase = juce::jmap (mode,
            juce::MathConstants<float>::pi, juce::MathConstants<float>::pi * 0.015f);
        const auto stereoSin = std::sin (stereoPhase);
        const auto stereoCos = std::cos (stereoPhase);
        // Roland's LFO is documented as a soft-clipped/trapezoidal sine
        // rather than a pure one -- it spends more time near the delay
        // extremes and less lingering through the zero crossing, which is
        // part of the unit's recognisable tone.
        const auto shapeLfo = [] (float value)
        {
            constexpr float amount = 1.6f;
            return std::tanh (value * amount) / std::tanh (amount);
        };
        const auto phaseSin = shapeLfo (std::sin (lfoPhase));
        const auto phaseCos = shapeLfo (std::cos (lfoPhase));
        const auto slowSin = shapeLfo (std::sin (secondaryPhase));
        const auto slowCos = shapeLfo (std::cos (secondaryPhase));

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto voiceA = channel == 0 ? phaseSin
                                              : phaseSin * stereoCos + phaseCos * stereoSin;
            const auto voiceB = channel == 0 ? phaseCos
                                              : phaseCos * stereoCos - phaseSin * stereoSin;
            const auto voiceC = -voiceA;
            const auto voiceD = channel == 0 ? slowSin
                                              : slowSin * stereoCos + slowCos * stereoSin;
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
                const auto rounded = chorusRounding[channel].process (dimensionWet * 1.16f) / 1.16f;
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
