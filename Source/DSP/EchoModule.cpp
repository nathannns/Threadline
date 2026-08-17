#include "EchoModule.h"

void EchoModule::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    // Real EP-3 delay time is documented at roughly 80-800ms depending on
    // the individual unit; a 1s buffer covers the full 60-800ms parameter
    // range plus wow/flutter modulation headroom with margin to spare.
    delayBuffer.setSize (static_cast<int> (spec.numChannels),
                         static_cast<int> (sampleRate * 1.0) + 4);
    delayBuffer.clear();

    for (int ch = 0; ch < 2; ++ch)
    {
        preampMidFilter[ch].prepare (spec);
        preampTrebleFilter[ch].prepare (spec);
        repeatDarkenFilter[ch].prepare (spec);
    }
    // "Sweetens the treble, fattens the mids" -- the real EP-3's always-on
    // solid-state preamp coloration, fixed rather than a user Tone knob.
    auto midCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        sampleRate, 650.0f, 0.9f, juce::Decibels::decibelsToGain (2.5f));
    auto trebleCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sampleRate, 3200.0f, 0.8f, juce::Decibels::decibelsToGain (2.0f));
    // The tape loop's own bandwidth loss, inside the feedback path -- see
    // file header for why repeats need this to sound warm rather than
    // harsh.
    auto darkenCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (
        sampleRate, 6000.0f, 0.707f);
    for (int ch = 0; ch < 2; ++ch)
    {
        *preampMidFilter[ch].coefficients = *midCoeffs;
        *preampTrebleFilter[ch].coefficients = *trebleCoeffs;
        *repeatDarkenFilter[ch].coefficients = *darkenCoeffs;
    }

    delaySamples.reset (sampleRate, 0.05);
    wetMix.reset (sampleRate, 0.02);
    feedbackValue.reset (sampleRate, 0.03);
    volumeValue.reset (sampleRate, 0.02);
    saturationDrive.reset (sampleRate, 0.03);
    reset();
}

void EchoModule::reset()
{
    writeIndex = 0;
    validSamples = 0;
    lfoPhase = 0.0f;
    flutterPhase = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        preampMidFilter[ch].reset();
        preampTrebleFilter[ch].reset();
        repeatDarkenFilter[ch].reset();
    }
    delaySamples.setCurrentAndTargetValue (static_cast<float> (sampleRate * 0.300));
    wetMix.setCurrentAndTargetValue (0.0f);
    feedbackValue.setCurrentAndTargetValue (0.0f);
    volumeValue.setCurrentAndTargetValue (0.0f);
    saturationDrive.setCurrentAndTargetValue (0.0f);
}

void EchoModule::setParameters (float timeMs, float sustainPercent, float volumePercent,
                                bool enabled, Mode modeIn)
{
    delaySamples.setTargetValue (juce::jlimit (60.0f, 800.0f, timeMs)
                                      * 0.001f * static_cast<float> (sampleRate));
    mode = modeIn;

    const auto sustain01 = juce::jlimit (0.0f, 1.0f, sustainPercent * 0.01f);
    float feedbackTarget;
    if (mode == Mode::soundOnSound)
    {
        // Sound-on-Sound disables the erase head on the real machine, so
        // layers persist far longer than any Echo-mode repeat regardless
        // of Sustain; still scaled by Sustain here so the knob stays
        // useful rather than becoming a no-op in that mode. Kept short of
        // eternal sustain (0.998 was close enough to unity that, layered
        // over real playing, energy piled up into an undifferentiated
        // wash faster than the tape-darkening filter could tame it).
        feedbackTarget = juce::jmap (sustain01, 0.90f, 0.99f);
    }
    else
    {
        // Below the crossover, Sustain targets an explicit number of
        // audible repeats N (1 to 40) rather than an arbitrary feedback
        // curve: after N repeats the tail should be down about -40dB
        // (g^N = 10^(-40/20) = 0.01), so g = 10^(-2/N) -- a knob position
        // maps to "how many times do you want to hear it," which is what
        // Sustain actually controls perceptually, rather than a raw
        // coefficient. Above the crossover, Sustain continues smoothly
        // into genuine self-oscillation, pushed slightly past unity same
        // as the real EP-3's actual ceiling rather than capped safely
        // below it.
        //
        // N was mapped *linearly* against dial position, but the loop's
        // steady-state buildup (1/(1-g)) is convex in N -- a linear
        // dial-to-N map put the knob's midpoint (N~23) at ~5.4x buildup
        // already, so anything past halfway was audibly "over" and the
        // whole dial had to be kept low to stay controlled. Squaring the
        // dial position before mapping to N keeps the first half of the
        // knob's travel gentle (halfway is now N~12, ~3x buildup) and
        // reserves the steep run-up toward self-oscillation for the last
        // quarter or so of the dial, matching how a real regen knob feels.
        constexpr float crossover = 0.9f;
        constexpr float maxRepeats = 40.0f;
        constexpr float repeatTaper = 2.2f;
        const auto feedbackAtCrossover = std::pow (10.0f, -2.0f / maxRepeats);
        if (sustain01 < crossover)
        {
            const auto tapered = std::pow (sustain01 / crossover, repeatTaper);
            const auto repeatCount = juce::jmap (tapered, 1.0f, maxRepeats);
            feedbackTarget = std::pow (10.0f, -2.0f / repeatCount);
        }
        else
        {
            const auto oscillationProgress = (sustain01 - crossover) / (1.0f - crossover);
            feedbackTarget = juce::jmap (oscillationProgress, feedbackAtCrossover, 1.03f);
        }
    }
    feedbackValue.setTargetValue (feedbackTarget);
    // Driven by the knob's own position, not feedbackTarget -- see file
    // header for why keying saturation off the internal coefficient made
    // Sound-on-Sound run hot at every Sustain setting instead of a graduated
    // range.
    saturationDrive.setTargetValue (sustain01);

    const auto volume01 = juce::jlimit (0.0f, 1.0f, volumePercent * 0.01f);
    // Sound-on-Sound's feedback range (0.90-0.99) gives a steady-state loop
    // gain of 1/(1-feedback) = 10x-100x -- Echo mode's feedback tops out
    // around 0.891 just before self-oscillation, i.e. ~9x, at most. So even
    // SoS's *minimum* Sustain setting circulates a signal an order of
    // magnitude denser than Echo mode's typical range, and the same Volume%
    // scales that much louder signal -- reading as "way too wet" from just
    // a small amount, not a Volume-mapping problem so much as SoS's
    // underlying signal genuinely being much hotter to begin with.
    const auto modeVolumeScale = mode == Mode::soundOnSound ? 0.2f : 1.0f;
    // Volume multiplies whatever the feedback loop is already circulating
    // (see readDelay/rawFeedback in process()), so it was never
    // independent of Sustain -- the old curve (pow*0.85, i.e. rising
    // *faster* than linear, on top of a full +30% headroom multiplier) meant
    // a middling Volume setting was already adding a big chunk of an
    // already-buildup-amplified signal on top of the dry, on top of Sustain
    // being retuned the same way above. Raising the exponent above 1 and
    // dropping the headroom multiplier keeps low-to-mid Volume settings
    // genuinely subtle, so the two knobs' effects add up predictably
    // instead of both needing to be kept near zero to avoid clipping.
    volumeValue.setTargetValue (std::pow (volume01, 1.3f) * 0.9f * modeVolumeScale);

    wetMix.setTargetValue (enabled ? 1.0f : 0.0f);
}

float EchoModule::readDelay (int channel, float distance) const
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

void EchoModule::process (juce::AudioBuffer<float>& buffer)
{
    if (! wetMix.isSmoothing() && wetMix.getCurrentValue() <= 0.00001f)
        return;

    const auto channels = juce::jmin (buffer.getNumChannels(), delayBuffer.getNumChannels());
    const auto lfoStep = juce::MathConstants<float>::twoPi * 0.55f / static_cast<float> (sampleRate);
    // Fixed (not user-adjustable) tape wow/flutter -- always present at a
    // modest, realistic amount on real tape transport hardware.
    constexpr float wowDepth = 0.22f;

    const auto smoothRail = [] (float value, float knee, float ceiling)
    {
        const auto magnitude = std::abs (value);
        if (magnitude <= knee)
            return value;
        const auto range = ceiling - knee;
        return std::copysign (knee + range * std::tanh ((magnitude - knee) / range), value);
    };

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto globalMix = wetMix.getNextValue();
        const auto baseDelay = delaySamples.getNextValue();
        const auto feedback = feedbackValue.getNextValue();
        const auto volumeGain = volumeValue.getNextValue();
        const auto drive = saturationDrive.getNextValue();

        const auto slowWobble = std::sin (lfoPhase) * 0.0048f;
        const auto gentleFlutter = std::sin (flutterPhase) * 0.00048f;
        const auto sharedModulation = (slowWobble + gentleFlutter) * wowDepth
                                     * static_cast<float> (sampleRate);
        const auto distance = juce::jmax (1.0f, baseDelay + sharedModulation);

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto input = buffer.getSample (channel, sample);
            // The real EP-3's preamp sits inline before the tape head, so
            // both the direct output and what gets recorded are colored --
            // not a separate "wet only" effect.
            auto coloured = preampMidFilter[channel].processSample (input);
            coloured = preampTrebleFilter[channel].processSample (coloured);

            const auto rawFeedback = readDelay (channel, distance);
            // Saturation intensifies with Sustain, matching the real
            // circuit's bias-oscillator/tape-hysteresis behaviour getting
            // more pronounced as more signal recirculates -- driven by the
            // knob's own 0-1 position (drive), not the feedback coefficient
            // itself, since that coefficient's range means something
            // different per mode (see file header). Kept gentler than
            // earlier (was up to 4.5x drive) since harder clipping every
            // single pass, with nothing rolling off the harmonics it
            // generates, was what made repeats sound like a harsh metallic
            // hiss instead of warm tape saturation.
            const auto satGain = 1.0f + drive * 2.0f;
            const auto saturatedFeedback = std::tanh (rawFeedback * satGain) / satGain;
            // The tape loop's own bandwidth loss, applied after saturation
            // so it's the harmonics saturation just added that get tamed --
            // this is what keeps repeats warm rather than accumulating an
            // ever-brighter, harsher edge with every pass.
            const auto darkened = repeatDarkenFilter[channel].processSample (saturatedFeedback);

            const auto writeSample = coloured + darkened * feedback;
            // A wide safety rail: self-oscillation should stay loud and
            // chaotic like the real unit, not numerically explode.
            delayBuffer.setSample (channel, writeIndex, smoothRail (writeSample, 1.4f, 3.2f));

            const auto safeWet = smoothRail (rawFeedback, 2.0f, 4.0f);
            // Additive, not a dry/wet crossfade: Volume adds echo level on
            // top of the always-present (colored) direct signal, the same
            // way the real unit's Volume knob works.
            const auto outputWet = coloured + safeWet * volumeGain;
            buffer.setSample (channel, sample, input * (1.0f - globalMix) + outputWet * globalMix);
        }

        writeIndex = (writeIndex + 1) % delayBuffer.getNumSamples();
        validSamples = juce::jmin (validSamples + 1, delayBuffer.getNumSamples());
        lfoPhase += lfoStep;
        if (lfoPhase >= juce::MathConstants<float>::twoPi)
            lfoPhase -= juce::MathConstants<float>::twoPi;
        flutterPhase += lfoStep * 5.35f;
        if (flutterPhase >= juce::MathConstants<float>::twoPi)
            flutterPhase -= juce::MathConstants<float>::twoPi;
    }
}
