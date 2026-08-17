#include "HallRoomReverbModule.h"

namespace
{
    // JUCE's own juce::Reverb tunings, in samples at 44.1kHz -- chosen to
    // avoid coincident resonances between the 8 parallel combs. Scaled per
    // Model (size) and sample rate in prepareTank().
    constexpr int combTunings[HallRoomReverbModule::numCombs]        { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    constexpr int allpassTunings[HallRoomReverbModule::numAllpasses] { 556, 441, 341, 225 };
    constexpr int stereoSpread = 23;

    // Exactly JUCE's own input gain constant (juce_Reverb.h: `gain = 0.015f`
    // when not frozen). This, not a guessed value, is what makes the tank's
    // resonant build-up land at a musically useful level in the first
    // place -- see file header.
    constexpr float inputGain = 0.015f;

    // Model 0 = Room, 1 = Hall, 2 = Plate (matches getModelName()). Hall
    // uses sizeScale 1.0 and the roomOffset/roomScale JUCE itself uses
    // (0.70/0.28, giving the exact same 0.70-0.98 feedback range as a
    // stock juce::Reverb) -- it's the least-modified of the three, closest
    // to an unmodified port. Room is scaled down and given a lower
    // feedback ceiling; Plate is scaled up, given a higher ceiling than
    // Hall, and a denser allpass diffusion coefficient plus a fixed
    // brightness bias -- deliberately bigger and more "extended-highs"
    // than Hall, per its real-world character.
    constexpr float modelSizeScale[HallRoomReverbModule::numModels]      { 0.55f, 1.0f, 1.3f };
    constexpr float modelRoomOffset[HallRoomReverbModule::numModels]     { 0.60f, 0.70f, 0.72f };
    constexpr float modelRoomScale[HallRoomReverbModule::numModels]      { 0.20f, 0.28f, 0.27f };
    constexpr float modelAllpassFeedback[HallRoomReverbModule::numModels] { 0.5f, 0.5f, 0.65f };
    constexpr float modelDampBias[HallRoomReverbModule::numModels]       { 0.0f, 0.0f, -0.15f };

    // With correct gain-staging this should rarely actually trigger --
    // kept as a backstop, not a routine level-setter, since resonant combs
    // can in principle still build gain at coincident frequencies for
    // pathological parameter combinations.
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
    const auto spread = juce::roundToInt (static_cast<float> (stereoSpread) * scale);
    const auto allpassFeedback = modelAllpassFeedback[modelIndex];

    for (int i = 0; i < numCombs; ++i)
    {
        tank.combL[i].setSize (juce::roundToInt (static_cast<float> (combTunings[i]) * scale));
        tank.combR[i].setSize (juce::roundToInt (static_cast<float> (combTunings[i]) * scale) + spread);
    }
    for (int i = 0; i < numAllpasses; ++i)
    {
        tank.allpassL[i].setSize (juce::roundToInt (static_cast<float> (allpassTunings[i]) * scale));
        tank.allpassL[i].feedback = allpassFeedback;
        tank.allpassR[i].setSize (juce::roundToInt (static_cast<float> (allpassTunings[i]) * scale) + spread);
        tank.allpassR[i].feedback = allpassFeedback;
    }
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

    // Exactly JUCE's own roomSize -> feedback mapping, per model (Decay
    // knob plays the role of their `parameters.roomSize`).
    const auto decay01 = juce::jlimit (0.0f, 1.0f, decayNormalised);
    feedback = decay01 * modelRoomScale[activeModel] + modelRoomOffset[activeModel];

    // Darker (lower Tone) = more damping = highs decay faster than lows in
    // the tail, same as real air absorption; kept well short of 1.0 so full
    // dark still sounds like a darkened room, not a muffled thump. Plate's
    // fixed negative bias keeps its extended-highs character even at
    // matched Tone settings.
    const auto darkness = 1.0f - juce::jlimit (0.0f, 1.0f, toneNormalised);
    damp = juce::jlimit (0.02f, 0.9f, juce::jmap (darkness, 0.05f, 0.65f) + modelDampBias[activeModel]);
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

    // The tank: exactly JUCE's own topology -- a single mono-summed input
    // (like a real Freeverb, not per-channel input) drives 8 parallel combs
    // per channel, accumulated, then 4 series allpasses per channel. Stereo
    // comes purely from the L/R comb/allpass tunings being offset by
    // stereoSpread, not from feeding different per-channel input.
    auto& tank = tanks[activeModel];
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto dryL = wetBuffer.getSample (0, sample);
        const auto dryR = channels > 1 ? wetBuffer.getSample (1, sample) : dryL;
        const auto input = (dryL + dryR) * 0.5f * inputGain;

        float outL = 0.0f, outR = 0.0f;
        for (auto& c : tank.combL) outL += c.process (input, damp, feedback);
        for (auto& c : tank.combR) outR += c.process (input, damp, feedback);
        for (auto& a : tank.allpassL) outL = a.process (outL);
        for (auto& a : tank.allpassR) outR = a.process (outR);

        // JUCE's own wetScaleFactor: our single equal-power Mix knob blends
        // dry/wet more conservatively than JUCE's independent wet/dry
        // levels do (their default wetLevel=0.33 becomes an almost-unity
        // wetGain1 once scaled by this same 3.0), so this makeup gain is
        // carried forward explicitly rather than assuming the tank's own
        // resonant build-up is loud enough on its own.
        constexpr float wetScaleFactor = 3.0f;
        outL *= wetScaleFactor;
        outR *= wetScaleFactor;

        wetBuffer.setSample (0, sample, smoothRail (outL, 2.5f, 6.0f));
        if (channels > 1)
            wetBuffer.setSample (1, sample, smoothRail (outR, 2.5f, 6.0f));
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
