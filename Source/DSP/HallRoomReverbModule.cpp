#include "HallRoomReverbModule.h"

namespace
{
    // FDN line tuning, in samples at 44.1kHz -- Freeverb's own classic
    // values, chosen to avoid coincident resonances. Reused here as the 8
    // shared FDN line lengths (scaled per Model/sample-rate in
    // prepareTank()) rather than for 8 independent per-channel combs.
    constexpr int lineTuning[HallRoomReverbModule::numLines] { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };

    // Dattorro's own published input-diffuser lengths and coefficients
    // (JAES 1997, "Effect Design Part 1") -- fixed regardless of Model, so
    // the input's own diffusion character stays consistent while the late
    // field (FDN line-length scale, RT60 range) is what actually varies
    // Room/Hall/Plate.
    constexpr int inputDiffuserTuning[HallRoomReverbModule::numInputDiffusers] { 142, 107, 379, 277 };
    constexpr float inputDiffuserCoeff[HallRoomReverbModule::numInputDiffusers] { 0.75f, 0.75f, 0.625f, 0.625f };

    constexpr int allpassTunings[HallRoomReverbModule::numAllpasses] { 556, 441, 341, 225 };
    constexpr int stereoSpread = 23;

    // Two rows of an 8-point Hadamard matrix -- mutually orthogonal +-1
    // patterns used to extract a decorrelated stereo pair from the FDN's 8
    // shared lines, rather than duplicating the whole network per channel.
    constexpr float outSignsL[HallRoomReverbModule::numLines] { 1, -1, 1, -1, 1, -1, 1, -1 };
    constexpr float outSignsR[HallRoomReverbModule::numLines] { 1, 1, -1, -1, 1, 1, -1, -1 };
    const float outputScale = 1.0f / std::sqrt (static_cast<float> (HallRoomReverbModule::numLines));

    // Exactly JUCE's own input gain constant (juce_Reverb.h: `gain = 0.015f`
    // when not frozen) -- proven gain-staging, not a guessed value.
    constexpr float inputGain = 0.015f;
    // JUCE's own wetScaleFactor: this module's single equal-power Mix knob
    // blends dry/wet more conservatively than JUCE's independent wet/dry
    // levels do, so this makeup gain is carried forward explicitly.
    constexpr float wetScaleFactor = 3.0f;

    // Model 0 = Room, 1 = Hall, 2 = Plate (matches getModelName()). Plate
    // gets both a bigger line-length scale AND a longer RT60 ceiling than
    // Hall, plus a little denser post-FDN allpass diffusion and a ratioBias
    // that keeps its high-frequency RT60 a bit closer to its low-frequency
    // one (brighter, "extended highs") than Room/Hall.
    constexpr float modelSizeScale[HallRoomReverbModule::numModels]      { 0.55f, 1.0f, 1.3f };
    constexpr float modelRt60LowMin[HallRoomReverbModule::numModels]     { 0.3f, 0.8f, 1.0f };
    constexpr float modelRt60LowMax[HallRoomReverbModule::numModels]     { 1.8f, 4.5f, 5.5f };
    constexpr float modelRatioBias[HallRoomReverbModule::numModels]      { 0.0f, 0.0f, 0.08f };
    constexpr float modelAllpassFeedback[HallRoomReverbModule::numModels] { 0.5f, 0.5f, 0.58f };

    // Freeverb's 8 tunings were chosen with a specific spread between them
    // so the 8 resonances land at different-enough frequencies to sound
    // diffuse rather than beating against each other. Uniformly scaling
    // every line down for Room shrinks that spread by the same amount,
    // audible as a "phasing"/comb-y quality -- fixed by scaling each
    // line's deviation from the mean tuning by an extra spreadBoost factor
    // (Room only) so the 8 lines stay decorrelated at a smaller overall
    // size. Matters even more here than in an independent-comb design,
    // since the Householder matrix explicitly couples all 8 lines together
    // every sample -- well-separated resonant frequencies are what keep
    // that coupling sounding diffuse rather than metallic.
    constexpr float modelSpreadBoost[HallRoomReverbModule::numModels] { 1.6f, 1.0f, 1.0f };

    // Early-reflection tap patterns (ms, gain), scaled down by erGainScale
    // so they sit alongside the tank's own (already-tuned) level rather
    // than overpowering it. Room: modest density, moderate spread. Hall:
    // sparser and more spread out, with a clearer gap before it thickens.
    // Plate: near-instantaneous and very dense, almost no gap.
    constexpr float erGainScale = 0.15f;
    constexpr float roomTapMs[8]     { 3.0f, 7.0f, 12.0f, 18.0f, 25.0f, 33.0f, 42.0f, 52.0f };
    constexpr float roomTapGain[8]   { 0.90f, 0.75f, 0.62f, 0.50f, 0.40f, 0.32f, 0.25f, 0.20f };
    constexpr float hallTapMs[8]     { 12.0f, 24.0f, 38.0f, 54.0f, 71.0f, 90.0f, 110.0f, 132.0f };
    constexpr float hallTapGain[8]   { 0.55f, 0.50f, 0.44f, 0.38f, 0.32f, 0.27f, 0.22f, 0.18f };
    constexpr float plateTapMs[10]   { 0.6f, 1.4f, 2.3f, 3.3f, 4.4f, 5.6f, 6.9f, 8.3f, 9.8f, 11.4f };
    constexpr float plateTapGain[10] { 0.95f, 0.92f, 0.88f, 0.85f, 0.82f, 0.78f, 0.75f, 0.72f, 0.68f, 0.65f };
    constexpr float erStereoStretch = 1.03f; // decorrelates ER between channels even for a mono dry signal

    // Post-FDN modulated-allpass depth/rate. Kept small (a fraction of a
    // millisecond) so it breaks up static ringing without reading as
    // audible pitch wobble; each instance gets its own rate/starting phase
    // so they decorrelate rather than wobbling in lockstep.
    constexpr float allpassModDepthMs = 0.4f;
    constexpr float allpassModRateHz[HallRoomReverbModule::numAllpasses] { 0.17f, 0.23f, 0.31f, 0.41f };

    // With correct gain-staging and g < 1 guaranteed by construction, this
    // should rarely actually trigger -- kept as a backstop, not a routine
    // level-setter.
    float smoothRail (float value, float knee, float ceiling)
    {
        const auto magnitude = std::abs (value);
        if (magnitude <= knee)
            return value;
        const auto range = ceiling - knee;
        return std::copysign (knee + range * std::tanh ((magnitude - knee) / range), value);
    }

    float meanOf (const int* values, int count)
    {
        float sum = 0.0f;
        for (int i = 0; i < count; ++i) sum += static_cast<float> (values[i]);
        return sum / static_cast<float> (count);
    }
}

void HallRoomReverbModule::prepareTank (Tank& tank, int modelIndex)
{
    const auto srScale = static_cast<float> (sampleRate / 44100.0);
    const auto scale = srScale * modelSizeScale[modelIndex];
    const auto spreadBoost = modelSpreadBoost[modelIndex];
    const auto spread = juce::roundToInt (static_cast<float> (stereoSpread) * scale);
    const auto allpassFeedback = modelAllpassFeedback[modelIndex];
    const auto lineMean = meanOf (lineTuning, numLines);

    for (int i = 0; i < numLines; ++i)
    {
        // Boost each line's deviation from the mean before scaling, so
        // shrinking the model doesn't also shrink the relative spread that
        // keeps the 8 resonances decorrelated -- see modelSpreadBoost above.
        const auto boostedTuning = lineMean + (static_cast<float> (lineTuning[i]) - lineMean) * spreadBoost;
        tank.lines[i].setSize (juce::roundToInt (boostedTuning * scale));
    }

    // Input diffusion: fixed regardless of Model, sample-rate scaled only.
    for (int i = 0; i < numInputDiffusers; ++i)
    {
        tank.inputDiffuser[i].setSize (juce::roundToInt (static_cast<float> (inputDiffuserTuning[i]) * srScale));
        tank.inputDiffuser[i].diffusion = inputDiffuserCoeff[i];
    }

    for (int i = 0; i < numAllpasses; ++i)
    {
        auto& left = tank.allpassL[i];
        auto& right = tank.allpassR[i];
        left.modDepthSamples = right.modDepthSamples = allpassModDepthMs * 0.001f * static_cast<float> (sampleRate);
        const auto rateHz = allpassModRateHz[i];
        left.modIncrement = right.modIncrement =
            juce::MathConstants<float>::twoPi * rateHz / static_cast<float> (sampleRate);
        left.modPhase = juce::MathConstants<float>::twoPi * static_cast<float> (i) / static_cast<float> (numAllpasses);
        right.modPhase = left.modPhase + juce::MathConstants<float>::pi * 0.5f;

        left.setSize (juce::roundToInt (static_cast<float> (allpassTunings[i]) * scale));
        left.feedback = allpassFeedback;
        right.setSize (juce::roundToInt (static_cast<float> (allpassTunings[i]) * scale) + spread);
        right.feedback = allpassFeedback;
    }

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
    setupEr (tank.erR, erStereoStretch);

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

    // Decay -> the low-frequency (DC) RT60 target in seconds, per model.
    const auto decay01 = juce::jlimit (0.0f, 1.0f, decayNormalised);
    rt60Low = juce::jmap (decay01, modelRt60LowMin[activeModel], modelRt60LowMax[activeModel]);

    // Tone -> the high-frequency RT60 as a fraction of rt60Low.
    const auto toneClamped = juce::jlimit (0.0f, 1.0f, toneNormalised);
    const auto ratio = juce::jlimit (0.02f, 0.98f,
        juce::jmap (toneClamped, 0.05f, 0.95f) + modelRatioBias[activeModel]);
    rt60High = rt60Low * ratio;

    updateDecayTimes();
}

void HallRoomReverbModule::updateDecayTimes()
{
    // For an orthogonally-mixed network, a uniform loop gain g combined
    // with the Householder matrix decays the network's total energy at
    // exactly rate g per round trip (the matrix preserves vector norm) --
    // the standard way real FDN reverbs control T60. Applied at the
    // network's characteristic (mean) line length, same Schroeder formula
    // as before -- see file header.
    auto& tank = tanks[activeModel];
    float meanLength = 0.0f;
    for (auto& line : tank.lines) meanLength += static_cast<float> (line.buffer.size());
    meanLength /= static_cast<float> (numLines);

    const auto exponent = -3.0f * meanLength / static_cast<float> (sampleRate);
    const auto gainLow = std::pow (10.0f, exponent / juce::jmax (0.02f, rt60Low));
    const auto gainHigh = std::pow (10.0f, exponent / juce::jmax (0.02f, rt60High));
    loopFeedback = juce::jlimit (0.0f, 0.999f, gainLow);
    const auto ratio = juce::jlimit (0.0001f, 1.0f, gainHigh / juce::jmax (0.0001f, gainLow));
    damp1 = juce::jlimit (0.0f, 0.999f, (1.0f - ratio) / (1.0f + ratio));
    damp2 = 1.0f - damp1;
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
    // tracking a single static delay.
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

    // The FDN core: mono-sum -> input diffusion -> 8 shared lines mixed by
    // a Householder matrix every sample -> stereo extracted via two
    // orthogonal (Hadamard-row) weighted sums of those same 8 lines. Early
    // reflections and post-FDN modulated diffusion are added afterward,
    // per-channel, from the genuinely-stereo dry signal.
    auto& tank = tanks[activeModel];
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto dryL = wetBuffer.getSample (0, sample);
        const auto dryR = channels > 1 ? wetBuffer.getSample (1, sample) : dryL;
        auto diffused = (dryL + dryR) * 0.5f * inputGain;
        for (auto& d : tank.inputDiffuser) diffused = d.process (diffused);

        float yd[numLines];
        float sum = 0.0f;
        for (int i = 0; i < numLines; ++i)
        {
            yd[i] = tank.lines[i].readDamped (damp1, damp2);
            sum += yd[i];
        }
        const auto reflected = sum * (2.0f / static_cast<float> (numLines));

        float tailL = 0.0f, tailR = 0.0f;
        for (int i = 0; i < numLines; ++i)
        {
            tailL += yd[i] * outSignsL[i];
            tailR += yd[i] * outSignsR[i];

            const auto z = yd[i] - reflected; // Householder-reflected (energy-preserving) mix
            tank.lines[i].writeBack (diffused + z * loopFeedback);
        }
        tailL *= outputScale * wetScaleFactor;
        tailR *= outputScale * wetScaleFactor;

        for (auto& a : tank.allpassL) tailL = a.process (tailL);
        for (auto& a : tank.allpassR) tailR = a.process (tailR);

        tailL += tank.erL.process (dryL);
        tailR += tank.erR.process (dryR);

        wetBuffer.setSample (0, sample, smoothRail (tailL, 2.5f, 6.0f));
        if (channels > 1)
            wetBuffer.setSample (1, sample, smoothRail (tailR, 2.5f, 6.0f));
    }

    juce::dsp::AudioBlock<float> wetBlock (wetBuffer);
    auto activeWet = wetBlock.getSubBlock (0, static_cast<size_t> (samples));
    juce::dsp::ProcessContextReplacing<float> wetContext (activeWet);
    rumbleFilter.process (wetContext);

    // Width: simple M/S scaling of the wet signal only (dry stays untouched),
    // so it reshapes the reverb's own stereo image rather than the guitar's.
    // Re-clamped after, since widthFactor can reach 2.0 ("doubled side"),
    // which can push mid+-side beyond what smoothRail guaranteed upstream.
    if (channels > 1 && std::abs (widthFactor - 1.0f) > 0.001f)
    {
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto left = wetBuffer.getSample (0, sample);
            const auto right = wetBuffer.getSample (1, sample);
            const auto mid = (left + right) * 0.5f;
            const auto side = (left - right) * 0.5f * widthFactor;
            wetBuffer.setSample (0, sample, smoothRail (mid + side, 2.5f, 6.0f));
            wetBuffer.setSample (1, sample, smoothRail (mid - side, 2.5f, 6.0f));
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
