#include "HallRoomReverbModule.h"

namespace
{
    // Classic Freeverb (Jezar) tank tuning, in samples at 44.1kHz -- chosen
    // to avoid coincident resonances between the 8 parallel combs. Scaled
    // per Model (size) and sample rate in prepareTank().
    constexpr int combTuningL[HallRoomReverbModule::numCombs] { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    constexpr int allpassTuningL[HallRoomReverbModule::numAllpasses] { 556, 441, 341, 225 };
    constexpr int stereoSpread = 23;

    // Model 0 = Room, 1 = Hall, 2 = Plate, 3 = Shimmer (matches
    // getModelName()). Size scales the comb/allpass line lengths (bigger =
    // sparser early build-up, bigger sense of space); the feedback range
    // sets each space's natural decay ceiling before the Decay knob is
    // applied. Plate runs a higher allpass feedback (denser diffusion) and
    // a brighter fixed damping bias, matching the real RV-6's "metallic,
    // extended high-frequency range" description. Shimmer reuses Hall's
    // tank -- its character comes from the external pitch-shift feedback
    // loop in process(), not from the tank tuning itself.
    constexpr float modelSizeScale[HallRoomReverbModule::numModels]      { 0.55f, 1.3f, 0.4f, 1.3f };
    constexpr float modelFeedbackMin[HallRoomReverbModule::numModels]    { 0.60f, 0.72f, 0.68f, 0.70f };
    constexpr float modelFeedbackMax[HallRoomReverbModule::numModels]    { 0.90f, 0.985f, 0.95f, 0.98f };
    constexpr float modelAllpassFeedback[HallRoomReverbModule::numModels] { 0.5f, 0.5f, 0.65f, 0.5f };
    constexpr float modelDamp1Bias[HallRoomReverbModule::numModels]      { 0.0f, 0.0f, -0.15f, 0.0f };

    // A comb sum can in principle build gain at coincident resonances even
    // though each individual comb is stable (feedback < 1) -- this keeps the
    // tank's output bounded by construction regardless of parameter
    // combination, the same tanh-rail technique used in EchoModule.
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
        tank.combsL[i].setSize (juce::roundToInt (static_cast<float> (combTuningL[i]) * scale));
        tank.combsR[i].setSize (juce::roundToInt (static_cast<float> (combTuningL[i]) * scale) + spread);
    }
    for (int i = 0; i < numAllpasses; ++i)
    {
        tank.allpassL[i].setSize (juce::roundToInt (static_cast<float> (allpassTuningL[i]) * scale));
        tank.allpassL[i].feedback = allpassFeedback;
        tank.allpassR[i].setSize (juce::roundToInt (static_cast<float> (allpassTuningL[i]) * scale) + spread);
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

    // ~70ms grains: long enough to sound smooth/ambient rather than choppy,
    // short enough to track pitch changes reasonably promptly.
    const auto grainSamples = static_cast<float> (sampleRate) * 0.07f;
    const auto shifterBufferSize = juce::roundToInt (grainSamples) + 16;
    shimmerShifterL.prepare (shifterBufferSize, grainSamples);
    shimmerShifterR.prepare (shifterBufferSize, grainSamples);

    wetMix.reset (spec.sampleRate, 0.03);
    reset();
}

void HallRoomReverbModule::reset()
{
    for (auto& tank : tanks) tank.reset();
    shimmerShifterL.reset();
    shimmerShifterR.reset();
    previousWetL = previousWetR = 0.0f;
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
        shimmerShifterL.reset();
        shimmerShifterR.reset();
        previousWetL = previousWetR = 0.0f;
        activeModel = modelIndex;
    }

    decayNormalised = juce::jlimit (0.0f, 1.0f, decayNormalised);
    const auto feedback = juce::jmap (decayNormalised, modelFeedbackMin[activeModel], modelFeedbackMax[activeModel]);

    // Darker (lower Tone) = more damping = highs decay faster than lows in
    // the tail, same as real air absorption; kept well short of 1.0 so full
    // dark still sounds like a darkened room, not a muffled thump. Plate's
    // fixed negative bias keeps its extended-highs character even at
    // matched Tone settings, per the real RV-6's Plate description.
    const auto darkness = 1.0f - juce::jlimit (0.0f, 1.0f, toneNormalised);
    const auto damp1 = juce::jlimit (0.02f, 0.9f,
        juce::jmap (darkness, 0.05f, 0.65f) + modelDamp1Bias[activeModel]);
    const auto damp2 = 1.0f - damp1;

    for (auto* combs : { tanks[activeModel].combsL, tanks[activeModel].combsR })
    {
        for (int i = 0; i < numCombs; ++i)
        {
            combs[i].feedback = feedback;
            combs[i].damp1 = damp1;
            combs[i].damp2 = damp2;
        }
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

    // The tank itself: 8 parallel combs summed, then 4 series allpasses,
    // run per-channel so any existing stereo width upstream (e.g. the dual
    // cab IR blend) carries through rather than being collapsed to mono.
    // Shimmer additionally pitch-shifts its own previous output up an
    // octave and feeds a portion back into this sample's tank input, so
    // the tail cascades upward on top of the normal reverberant decay.
    constexpr float inputGain = 0.03f;
    constexpr float shimmerFeedback = 0.5f;
    auto& tank = tanks[activeModel];
    for (int sample = 0; sample < samples; ++sample)
    {
        auto inputL = wetBuffer.getSample (0, sample) * inputGain;
        auto inputR = (channels > 1 ? wetBuffer.getSample (1, sample) : wetBuffer.getSample (0, sample)) * inputGain;

        if (activeModel == shimmerModel)
        {
            inputL += shimmerShifterL.process (previousWetL) * shimmerFeedback;
            inputR += shimmerShifterR.process (previousWetR) * shimmerFeedback;
        }

        float wetL = 0.0f, wetR = 0.0f;
        for (auto& c : tank.combsL) wetL += c.process (inputL);
        for (auto& c : tank.combsR) wetR += c.process (inputR);
        for (auto& a : tank.allpassL) wetL = a.process (wetL);
        for (auto& a : tank.allpassR) wetR = a.process (wetR);

        wetL = smoothRail (wetL, 1.2f, 3.5f);
        wetR = smoothRail (wetR, 1.2f, 3.5f);
        wetBuffer.setSample (0, sample, wetL);
        if (channels > 1)
            wetBuffer.setSample (1, sample, wetR);

        previousWetL = wetL;
        previousWetR = wetR;
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
