#include "EchoModule.h"

void EchoModule::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    // Head 3 sits at 3x head 1's delay time — big enough to hold that at
    // the maximum Time setting (see setParameters' 2.5s clamp) without taps
    // silently falling outside the buffer.
    delayBuffer.setSize (static_cast<int> (spec.numChannels),
                         static_cast<int> (sampleRate * 8.0) + 4);
    delayBuffer.clear();
    toneState.assign (static_cast<size_t> (spec.numChannels), 0.0f);
    delaySamples.reset (sampleRate, 0.05);
    wetMix.reset (sampleRate, 0.02);
    feedbackValue.reset (sampleRate, 0.03);
    wobbleValue.reset (sampleRate, 0.03);
    driveValue.reset (sampleRate, 0.03);
    cachedToneHz = -1.0f;
    reset();
}

void EchoModule::reset()
{
    std::fill (toneState.begin(), toneState.end(), 0.0f);
    writeIndex = 0;
    validSamples = 0;
    lfoPhase = 0.0f;
    flutterPhase = 0.0f;
    delaySamples.setCurrentAndTargetValue (static_cast<float> (sampleRate * 0.375));
    wetMix.setCurrentAndTargetValue (0.0f);
    feedbackValue.setCurrentAndTargetValue (0.35f);
    toneCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                        * 7000.0f / static_cast<float> (sampleRate));
    cachedToneHz = -1.0f;
    wobbleValue.setCurrentAndTargetValue (0.0f);
    driveValue.setCurrentAndTargetValue (0.0f);
}

void EchoModule::setParameters (float timeMs, float repeats, float toneHz, float wobble,
                                float drive, float mix, bool enabled, int patternIndex)
{
    delaySamples.setTargetValue (juce::jlimit (0.04f, 2.5f, timeMs * 0.001f)
                                      * static_cast<float> (sampleRate));
    feedbackValue.setTargetValue (juce::jlimit (0.0f, 0.86f, repeats * 0.0086f));
    const auto limitedToneHz = juce::jlimit (1200.0f, 14000.0f, toneHz);
    if (std::abs (limitedToneHz - cachedToneHz) > 0.01f)
    {
        cachedToneHz = limitedToneHz;
        toneCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                            * limitedToneHz / static_cast<float> (sampleRate));
    }
    wobbleValue.setTargetValue (juce::jlimit (0.0f, 1.0f, wobble * 0.01f));
    driveValue.setTargetValue (juce::jlimit (0.0f, 1.0f, drive * 0.01f));
    // Give guitarists much finer control over the useful low end of the Mix
    // knob. 10% now means subtle ambience, while the endpoint remains 100% wet.
    const auto normalisedMix = juce::jlimit (0.0f, 1.0f, mix * 0.01f);
    wetMix.setTargetValue (enabled ? std::pow (normalisedMix, 1.55f) : 0.0f);
    pattern = juce::jlimit (0, 5, patternIndex);
}

float EchoModule::readDelay (int channel, float distance) const
{
    // Cubic interpolation also needs the sample immediately older than the
    // requested position. Never let that tap reach stale pre-reset memory.
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
    // Four-point Hermite interpolation avoids the slope discontinuities that
    // linear interpolation exposed as alternating clicks on modulated repeats.
    const auto c0 = y1;
    const auto c1 = 0.5f * (y2 - y0);
    const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * fraction + c2) * fraction + c1) * fraction + c0;
}

float EchoModule::processFeedbackTone (int channel, float sample)
{
    auto& state = toneState[static_cast<size_t> (channel)];
    state += toneCoefficient * (sample - state);
    return state;
}

void EchoModule::getPattern (float* ratios, float* gains, int& taps) const
{
    // Real RE-201 head ratios are exactly 1:2:3 off head 1's delay time
    // (fixed, equally-spaced tape heads) — these replace the previous
    // made-up ratios (0.5/0.67/0.75/0.38...) that didn't match the actual
    // hardware. Names stay the same; the head combination each now maps to:
    //   Straight -> head 1 alone
    //   Bounce   -> heads 1+2
    //   Gallop   -> heads 2+3 (skips head 1 for a different rhythmic feel)
    //   Cluster  -> heads 1+2+3 (all three, dense)
    //   Wash     -> heads 1+3 (widest spacing, most diffuse)
    taps = 1; ratios[0] = 1.0f; gains[0] = 1.0f;
    if (pattern == bounce)  { taps = 2; ratios[0] = 1.0f; ratios[1] = 2.0f; gains[0] = 0.85f; gains[1] = 0.7f; }
    if (pattern == gallop)  { taps = 2; ratios[0] = 2.0f; ratios[1] = 3.0f; gains[0] = 0.8f; gains[1] = 0.65f; }
    if (pattern == cluster) { taps = 3; ratios[0] = 1.0f; ratios[1] = 2.0f; ratios[2] = 3.0f; gains[0] = 0.8f; gains[1] = 0.62f; gains[2] = 0.48f; }
    if (pattern == wash)    { taps = 2; ratios[0] = 1.0f; ratios[1] = 3.0f; gains[0] = 0.75f; gains[1] = 0.55f; }
}

void EchoModule::process (juce::AudioBuffer<float>& buffer)
{
    if (! wetMix.isSmoothing() && wetMix.getCurrentValue() <= 0.00001f)
        return;

    float ratios[3] {}, gains[3] {}; int taps = 1;
    getPattern (ratios, gains, taps);
    const auto channels = juce::jmin (buffer.getNumChannels(), delayBuffer.getNumChannels());
    const auto lfoStep = juce::MathConstants<float>::twoPi * 0.55f / static_cast<float> (sampleRate);

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto baseDelay = delaySamples.getNextValue();
        const auto mix = wetMix.getNextValue();
        const auto feedback = feedbackValue.getNextValue();
        const auto wobbleDepth = std::pow (wobbleValue.getNextValue(), 0.78f);
        const auto driveAmount = driveValue.getNextValue();

        // One physical tape transport drives both channels. Independent L/R
        // phases made high-Mix repeats jump from side to side and made the
        // interpolation edges sound like alternating clicks.
        auto sharedModulation = 0.0f;
        if (wobbleDepth > 0.0001f)
        {
            const auto slowWobble = std::sin (lfoPhase) * 0.0048f;
            const auto gentleFlutter = std::sin (flutterPhase) * 0.00048f;
            sharedModulation = (slowWobble + gentleFlutter) * wobbleDepth
                             * static_cast<float> (sampleRate);
        }

        const auto smoothRail = [] (float value, float knee, float ceiling)
        {
            // A hard jlimit introduced a derivative discontinuity whenever a
            // loud feedback peak touched the rail. At high Mix that edge was
            // exposed as a click. This wide soft rail is transparent at
            // normal levels but remains bounded under runaway input.
            const auto magnitude = std::abs (value);
            if (magnitude <= knee)
                return value;
            const auto range = ceiling - knee;
            return std::copysign (knee + range * std::tanh ((magnitude - knee) / range), value);
        };

        if (pattern == pingPong && channels > 1)
        {
            // True stereo ping-pong: not an RE-201 characteristic (the real
            // unit is mono) — a modern bonus mode. Mono-summed input enters
            // channel 0's delay line only; each line's output feeds the
            // *other* line's input (crossed feedback), which is what makes
            // successive repeats alternate sides rather than both channels
            // just echoing identically in place.
            const auto monoIn = (buffer.getSample (0, sample) + buffer.getSample (1, sample)) * 0.5f;
            const auto distance = juce::jmax (1.0f, baseDelay + sharedModulation);
            const auto lineAOut = readDelay (0, distance);
            const auto lineBOut = readDelay (1, distance);

            auto toneA = processFeedbackTone (0, lineAOut);
            auto toneB = processFeedbackTone (1, lineBOut);
            if (driveAmount > 0.0001f)
            {
                const auto gain = 1.0f + driveAmount * 4.0f;
                toneA = std::tanh (toneA * gain) / gain;
                toneB = std::tanh (toneB * gain) / gain;
            }

            delayBuffer.setSample (0, writeIndex, smoothRail (monoIn + toneB * feedback, 1.0f, 2.0f));
            delayBuffer.setSample (1, writeIndex, smoothRail (toneA * feedback, 1.0f, 2.0f));

            const auto dryL = buffer.getSample (0, sample), dryR = buffer.getSample (1, sample);
            buffer.setSample (0, sample, dryL * (1.0f - mix) + smoothRail (lineAOut, 2.0f, 4.0f) * mix);
            buffer.setSample (1, sample, dryR * (1.0f - mix) + smoothRail (lineBOut, 2.0f, 4.0f) * mix);
        }
        else
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                float wet = 0.0f;
                for (int tap = 0; tap < taps; ++tap)
                    wet += readDelay (channel, juce::jmax (1.0f, baseDelay * ratios[tap] + sharedModulation)) * gains[tap];
                wet /= std::sqrt (static_cast<float> (taps));

                auto feedbackSample = processFeedbackTone (channel, wet);
                // Normalise by the pre-shaper gain, not tanh(gain). The
                // previous formula amplified quiet repeats inside the
                // feedback loop and could make driven multi-tap presets
                // regenerate unexpectedly.
                if (driveAmount > 0.0001f)
                {
                    const auto gain = 1.0f + driveAmount * 4.0f;
                    feedbackSample = std::tanh (feedbackSample * gain) / gain;
                }

                const auto input = buffer.getSample (channel, sample);
                const auto writeSample = input + feedbackSample * feedback;
                delayBuffer.setSample (channel, writeIndex, smoothRail (writeSample, 1.0f, 2.0f));
                const auto safeWet = smoothRail (wet, 2.0f, 4.0f);
                buffer.setSample (channel, sample, input * (1.0f - mix) + safeWet * mix);
            }
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
