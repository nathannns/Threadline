#include "HallRoomReverbModule.h"

namespace
{
    // FDN line tuning, in samples at 44.1kHz -- chosen to avoid coincident
    // resonances between the 8 lines. Scaled per Model (size) and sample
    // rate in prepareTank().
    constexpr int lineTuning[HallRoomReverbModule::numLines] { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };

    // Model 0 = Room, 1 = Hall, 2 = Plate (matches getModelName()). Size
    // scales the FDN line lengths (bigger = sparser early build-up in the
    // late tail, bigger sense of space); Plate is deliberately given a
    // bigger scale AND a higher decay ceiling than Hall -- see file header.
    constexpr float modelSizeScale[HallRoomReverbModule::numModels]   { 0.55f, 1.3f, 1.6f };
    constexpr float modelFeedbackMin[HallRoomReverbModule::numModels] { 0.60f, 0.72f, 0.75f };
    constexpr float modelFeedbackMax[HallRoomReverbModule::numModels] { 0.90f, 0.985f, 0.99f };
    // Plate's fixed brightness bias -- "extended high-frequency range" is a
    // trait of the algorithm itself, not something Tone should have to
    // compensate for.
    constexpr float modelDamp1Bias[HallRoomReverbModule::numModels]   { 0.0f, 0.0f, -0.15f };

    // Early-reflection tap patterns (ms, gain), scaled down by erGainScale
    // so they sit comfortably under the safety rail's knee in normal use --
    // the tanh clamp should rarely actually engage, or it would flatten the
    // level differences these tables are designed to create. Room: modest
    // density, moderate spread. Hall: sparser and more spread out, with a
    // clearer gap before the reflections thicken -- reads as a bigger
    // physical distance to the walls. Plate: near-instantaneous and very
    // dense with almost no gap -- a real plate's whole surface resonates
    // almost simultaneously, which is what makes it read as bigger/more
    // enveloping than Hall despite the shorter individual tap delays.
    constexpr float erGainScale = 0.18f;
    constexpr float roomTapMs[8]   { 3.0f, 7.0f, 12.0f, 18.0f, 25.0f, 33.0f, 42.0f, 52.0f };
    constexpr float roomTapGain[8] { 0.90f, 0.75f, 0.62f, 0.50f, 0.40f, 0.32f, 0.25f, 0.20f };
    constexpr float hallTapMs[8]   { 12.0f, 24.0f, 38.0f, 54.0f, 71.0f, 90.0f, 110.0f, 132.0f };
    constexpr float hallTapGain[8] { 0.55f, 0.50f, 0.44f, 0.38f, 0.32f, 0.27f, 0.22f, 0.18f };
    constexpr float plateTapMs[10]   { 0.6f, 1.4f, 2.3f, 3.3f, 4.4f, 5.6f, 6.9f, 8.3f, 9.8f, 11.4f };
    constexpr float plateTapGain[10] { 0.95f, 0.92f, 0.88f, 0.85f, 0.82f, 0.78f, 0.75f, 0.72f, 0.68f, 0.65f };
    constexpr float rStereoStretch = 1.03f; // decorrelates ER between channels even for a mono dry signal

    // Two rows of an 8-point Hadamard matrix -- mutually orthogonal +-1
    // patterns used to extract a decorrelated stereo pair from the FDN's 8
    // shared lines (rather than duplicating the whole network per channel).
    constexpr float outSignsL[HallRoomReverbModule::numLines] { 1, -1, 1, -1, 1, -1, 1, -1 };
    constexpr float outSignsR[HallRoomReverbModule::numLines] { 1, 1, -1, -1, 1, 1, -1, -1 };
    const float outputScale = 1.0f / std::sqrt (static_cast<float> (HallRoomReverbModule::numLines));

    // A Householder-mixed FDN is already energy-preserving/lossless before
    // the loop-gain scalar is applied, so unlike the old independent-comb
    // design this rarely needs the clamp in normal use -- it's here for
    // pathological parameter combinations, not routine operation.
    float smoothRail (float value, float knee, float ceiling)
    {
        const auto magnitude = std::abs (value);
        if (magnitude <= knee)
            return value;
        const auto range = ceiling - knee;
        return std::copysign (knee + range * std::tanh ((magnitude - knee) / range), value);
    }
}

void HallRoomReverbModule::prepareTank (Tank& tank, int modelIndex)
{
    const auto srScale = static_cast<float> (sampleRate / 44100.0);
    const auto scale = srScale * modelSizeScale[modelIndex];

    for (int i = 0; i < numLines; ++i)
        tank.lines[i].setSize (juce::roundToInt (static_cast<float> (lineTuning[i]) * scale));

    const float* tapMs = roomTapMs; const float* tapGains = roomTapGain; int tapCount = 8;
    if (modelIndex == 1) { tapMs = hallTapMs; tapGains = hallTapGain; tapCount = 8; }
    else if (modelIndex == 2) { tapMs = plateTapMs; tapGains = plateTapGain; tapCount = 10; }

    auto setupEr = [this, tapMs, tapGains, tapCount] (EarlyReflections& er, float stretch)
    {
        er.numActiveTaps = tapCount;
        auto maxSamples = 0;
        for (int t = 0; t < tapCount; ++t)
        {
            er.tapDelaySamples[t] = juce::roundToInt (tapMs[t] * stretch * 0.001f * static_cast<float> (sampleRate));
            er.tapGain[t] = tapGains[t] * erGainScale;
            maxSamples = juce::jmax (maxSamples, er.tapDelaySamples[t]);
        }
        er.setBufferSize (maxSamples + 16);
    };
    setupEr (tank.erL, 1.0f);
    setupEr (tank.erR, rStereoStretch);

    tank.reset();
}

void HallRoomReverbModule::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    maximumBlockSize = static_cast<int> (spec.maximumBlockSize);
    channelCount = static_cast<int> (spec.numChannels);
    wetBuffer.setSize (channelCount, maximumBlockSize);

    rumbleFilter.prepare (spec);
    rumbleFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    rumbleFilter.setCutoffFrequency (85.0f); // fixed: keeps the tail from building up mud

    preDelayLine.prepare (spec);
    preDelayLine.setMaximumDelayInSamples (static_cast<int> (sampleRate * 0.1) + 32); // up to 100ms + modulation headroom

    for (int model = 0; model < numModels; ++model)
        prepareTank (tanks[model], model);
    activeModel = 0;

    wetMix.reset (spec.sampleRate, 0.03);
    reset();
}

void HallRoomReverbModule::reset()
{
    for (auto& tank : tanks) tank.reset();
    rumbleFilter.reset();
    preDelayLine.reset();
    modPhaseLeft = 0.0f;
    modPhaseRight = juce::MathConstants<float>::pi;
    wetMix.setCurrentAndTargetValue (0.0f);
}

void HallRoomReverbModule::setParameters (float preDelayNormalised, float decayNormalised, float toneNormalised,
                                          float mix, float widthPercent, bool enabled, int modelIndex)
{
    widthFactor = juce::jmap (juce::jlimit (0.0f, 100.0f, widthPercent), 0.0f, 100.0f, 0.0f, 2.0f);

    basePreDelaySamples = static_cast<float> (sampleRate)
        * juce::jmap (juce::jlimit (0.0f, 1.0f, preDelayNormalised), 0.0f, 80.0f) * 0.001f;

    wetMix.setTargetValue (enabled ? juce::jlimit (0.0f, 1.0f, mix * 0.01f) : 0.0f);

    modelIndex = juce::jlimit (0, numModels - 1, modelIndex);
    if (modelIndex != activeModel)
    {
        tanks[modelIndex].reset(); // no stale energy from the previous space
        activeModel = modelIndex;
    }

    decayNormalised = juce::jlimit (0.0f, 1.0f, decayNormalised);
    loopFeedback = juce::jmap (decayNormalised, modelFeedbackMin[activeModel], modelFeedbackMax[activeModel]);

    // Darker (lower Tone) = more damping = highs decay faster than lows in
    // the tail, same as real air absorption; kept well short of 1.0 so full
    // dark still sounds like a darkened room, not a muffled thump.
    const auto darkness = 1.0f - juce::jlimit (0.0f, 1.0f, toneNormalised);
    const auto damp1 = juce::jlimit (0.02f, 0.9f,
        juce::jmap (darkness, 0.05f, 0.65f) + modelDamp1Bias[activeModel]);
    const auto damp2 = 1.0f - damp1;

    for (auto& line : tanks[activeModel].lines)
    {
        line.damp1 = damp1;
        line.damp2 = damp2;
    }
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

    // The tank: an 8-line Householder-mixed FDN for the late diffuse tail,
    // plus a per-channel multi-tap early-reflection generator running in
    // parallel -- see file header for why both stages matter.
    constexpr float inputGain = 0.06f;
    auto& tank = tanks[activeModel];
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto dryL = wetBuffer.getSample (0, sample);
        const auto dryR = channels > 1 ? wetBuffer.getSample (1, sample) : dryL;
        const auto inputL = dryL * inputGain;
        const auto inputR = dryR * inputGain;

        float yd[numLines];
        float sum = 0.0f;
        for (int i = 0; i < numLines; ++i)
        {
            yd[i] = tank.lines[i].readDamped();
            sum += yd[i];
        }
        const auto reflected = sum * (2.0f / static_cast<float> (numLines));

        float tailL = 0.0f, tailR = 0.0f;
        for (int i = 0; i < numLines; ++i)
        {
            tailL += yd[i] * outSignsL[i];
            tailR += yd[i] * outSignsR[i];

            const auto z = yd[i] - reflected; // Householder-reflected (energy-preserving) mix
            const auto injected = (i % 2 == 0) ? inputL : inputR;
            tank.lines[i].writeBack (injected + z * loopFeedback);
        }
        tailL *= outputScale;
        tailR *= outputScale;

        const auto erOutL = tank.erL.process (dryL);
        const auto erOutR = tank.erR.process (dryR);

        const auto wetL = smoothRail (tailL + erOutL, 1.2f, 3.5f);
        const auto wetR = smoothRail (tailR + erOutR, 1.2f, 3.5f);
        wetBuffer.setSample (0, sample, wetL);
        if (channels > 1)
            wetBuffer.setSample (1, sample, wetR);
    }

    juce::dsp::AudioBlock<float> wetBlock (wetBuffer);
    auto activeWet = wetBlock.getSubBlock (0, static_cast<size_t> (samples));
    juce::dsp::ProcessContextReplacing<float> wetContext (activeWet);
    rumbleFilter.process (wetContext);

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
