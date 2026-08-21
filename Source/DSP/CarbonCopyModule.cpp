#include "CarbonCopyModule.h"

void CarbonCopyModule::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    // Real Carbon Copy delay time tops out around 600ms; a 700ms buffer
    // covers that plus modulation headroom with margin to spare.
    delayBuffer.setSize (static_cast<int> (spec.numChannels),
                         static_cast<int> (sampleRate * 0.7) + 4);
    delayBuffer.clear();

    for (int ch = 0; ch < 2; ++ch)
        darkenFilter[ch].prepare (spec);
    // A real BBD chip's inherent anti-aliasing/companding rolloff -- fixed,
    // not a user Tone knob, and placed inside the feedback loop so it
    // compounds: each repeat is darker than the one before it.
    auto darkenCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 4200.0f, 0.707f);
    for (int ch = 0; ch < 2; ++ch)
        *darkenFilter[ch].coefficients = *darkenCoeffs;

    delaySamples.reset (sampleRate, 0.05);
    wetMix.reset (sampleRate, 0.02);
    feedbackValue.reset (sampleRate, 0.03);
    reset();
}

void CarbonCopyModule::reset()
{
    writeIndex = 0;
    validSamples = 0;
    modPhase = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        darkenFilter[ch].reset();
        writeRail[ch].reset();
    }
    delaySamples.setCurrentAndTargetValue (static_cast<float> (sampleRate * 0.300));
    wetMix.setCurrentAndTargetValue (0.0f);
    feedbackValue.setCurrentAndTargetValue (0.0f);
}

void CarbonCopyModule::setParameters (float timeMs, float regenPercent, float mixPercent, bool modOn, bool enabled)
{
    delaySamples.setTargetValue (juce::jlimit (20.0f, 600.0f, timeMs) * 0.001f * static_cast<float> (sampleRate));

    // Below the crossover, Regen targets an explicit number of audible
    // repeats N (1 to 35) rather than an arbitrary feedback curve: after N
    // repeats the tail should be down about -40dB (g^N = 10^(-40/20) =
    // 0.01), so g = 10^(-2/N) -- a knob position maps to "how many times
    // do you want to hear it," which is what Regen actually controls
    // perceptually, rather than a raw coefficient. Above the crossover,
    // Regen continues smoothly into near-self-oscillation, same as the
    // real pedal's actual ceiling -- bounded by the tanh safety rail in
    // process() rather than capped safely below unity.
    const auto regen01 = juce::jlimit (0.0f, 1.0f, regenPercent * 0.01f);
    constexpr float crossover = 0.9f;
    constexpr float maxRepeats = 35.0f;
    const auto feedbackAtCrossover = std::pow (10.0f, -2.0f / maxRepeats);
    float feedbackTarget;
    if (regen01 < crossover)
    {
        const auto repeatCount = juce::jmap (regen01 / crossover, 1.0f, maxRepeats);
        feedbackTarget = std::pow (10.0f, -2.0f / repeatCount);
    }
    else
    {
        const auto oscillationProgress = (regen01 - crossover) / (1.0f - crossover);
        feedbackTarget = juce::jmap (oscillationProgress, feedbackAtCrossover, 1.02f);
    }
    feedbackValue.setTargetValue (feedbackTarget);

    // Slow the first half of the physical Mix control without changing its
    // fully-wet endpoint; the equal-power law below still handles the
    // actual dry/wet crossfade after this perceptual knob taper.
    wetMix.setTargetValue (enabled ? mapMix (mixPercent * 0.01f) : 0.0f);
    modEnabled = modOn;
}

float CarbonCopyModule::readDelay (int channel, float distance) const
{
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

void CarbonCopyModule::process (juce::AudioBuffer<float>& buffer)
{
    if (! wetMix.isSmoothing() && wetMix.getCurrentValue() <= 0.00001f)
        return;

    const auto channels = juce::jmin (buffer.getNumChannels(), delayBuffer.getNumChannels());
    // Mod: a slow, modest chorus-like wobble on the repeats, matching the
    // real pedal's internal trim range (~0.2-2.2Hz) rather than Plexer's
    // always-on tape-style wow/flutter -- this one only runs when the Mod
    // switch is on.
    const auto modStep = juce::MathConstants<float>::twoPi * 0.6f / static_cast<float> (sampleRate);
    constexpr float modDepthMs = 2.2f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto mix = wetMix.getNextValue();
        const auto baseDelay = delaySamples.getNextValue();
        const auto feedback = feedbackValue.getNextValue();

        auto distance = baseDelay;
        if (modEnabled)
        {
            const auto wobble = std::sin (modPhase) * modDepthMs * 0.001f * static_cast<float> (sampleRate);
            distance = juce::jmax (1.0f, baseDelay + wobble);
        }

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto input = buffer.getSample (channel, sample);
            const auto delayed = readDelay (channel, distance);
            // The darkening filter models the record head's own band-
            // limiting, which a real BBD-based delay applies to everything
            // being written to the bucket-brigade line -- fresh input and
            // recirculated feedback combined first, same "combine then
            // colour" pattern Space Echo/Plexer use, rather than only
            // darkening the feedback contribution after the fact. Applied
            // again on every repeat since it sits in the write path (the
            // tail still gets progressively duller, not just uniformly
            // coloured once), but now the very first repeat carries it
            // too, matching real BBD hardware's own per-pass band-limiting
            // rather than reading as a clean first echo.
            const auto toRecord = input + delayed * feedback;
            const auto darkened = darkenFilter[channel].processSample (toRecord);

            delayBuffer.setSample (channel, writeIndex, writeRail[channel].process (darkened, 1.4f, 3.2f));

            // A crossfade, not Plexer's additive mixing -- real analog delay
            // pedals like this one just blend wet/dry. Equal-power (not
            // linear) for the same reason every other Mix knob in this
            // plugin uses it: dryGain^2+wetGain^2 stays at 1 across the
            // whole range, where a linear crossfade's sum drops as low as
            // 0.5 around the middle of the range -- an audible loudness dip
            // that was reading as "Copier is quieter than Plexer" (whose
            // additive mixing keeps dry at ~unity throughout, so it never
            // had this dip to begin with).
            const auto dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
            const auto wetGain = std::sin (mix * juce::MathConstants<float>::halfPi);
            buffer.setSample (channel, sample, input * dryGain + delayed * wetGain);
        }

        writeIndex = (writeIndex + 1) % delayBuffer.getNumSamples();
        validSamples = juce::jmin (validSamples + 1, delayBuffer.getNumSamples());
        modPhase += modStep;
        if (modPhase >= juce::MathConstants<float>::twoPi)
            modPhase -= juce::MathConstants<float>::twoPi;
    }
}
