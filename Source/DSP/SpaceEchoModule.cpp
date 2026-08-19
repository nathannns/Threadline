#include "SpaceEchoModule.h"

namespace
{
    constexpr float bassShelfHz = 150.0f;
    constexpr float trebleShelfHz = 4000.0f;
    constexpr float shelfQ = 0.707f;

    // Langevin function L(q) = coth(q) - 1/q -- the anhysteretic
    // (hysteresis-free) magnetisation curve a Jiles-Atherton-style model
    // chases. It has a removable singularity at q=0 (both terms blow up
    // but cancel); the Taylor series L(q) ~= q/3 - q^3/45 stands in near
    // zero, standard practice for this function.
    float langevin (float q)
    {
        if (std::abs (q) < 1.0e-3f)
            return q / 3.0f - (q * q * q) / 45.0f;
        return 1.0f / std::tanh (q) - 1.0f / q;
    }

    // d/dq of the Langevin function above, needed for the "reversible"
    // component of the hysteresis update -- same near-zero Taylor
    // treatment.
    float langevinDerivative (float q)
    {
        if (std::abs (q) < 1.0e-3f)
            return 1.0f / 3.0f - (q * q) / 15.0f;
        const auto csch = 1.0f / std::sinh (q);
        return 1.0f / (q * q) - csch * csch;
    }
}

void SpaceEchoModule::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    // Head 3 sits at 3x head 1's delay time — big enough to hold that at
    // the maximum Time setting (see setParameters' 2.5s clamp) without taps
    // silently falling outside the buffer.
    delayBuffer.setSize (static_cast<int> (spec.numChannels),
                         static_cast<int> (sampleRate * 8.0) + 4);
    delayBuffer.clear();
    hysteresisState.assign (static_cast<size_t> (spec.numChannels), 0.0f);
    previousInputState.assign (static_cast<size_t> (spec.numChannels), 0.0f);
    directionState.assign (static_cast<size_t> (spec.numChannels), 0.0f);
    delaySamples.reset (sampleRate, 0.05);
    wetMix.reset (sampleRate, 0.02);
    feedbackValue.reset (sampleRate, 0.03);
    wobbleValue.reset (sampleRate, 0.03);
    driveValue.reset (sampleRate, 0.03);
    // Same reasoning as TapeModule's own hysteresis stage: smoothing the
    // raw +-1 rising/falling sign (rather than using it directly) avoids
    // injecting broadband "fizz" from fast, low-amplitude wiggles while
    // still tracking a genuine sustained direction change.
    directionSmoothCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                                   * 500.0f / static_cast<float> (sampleRate));
    const juce::dsp::ProcessSpec monoSpec { sampleRate, spec.maximumBlockSize, 1 };
    for (auto& shelf : bassShelf) shelf.prepare (monoSpec);
    for (auto& shelf : trebleShelf) shelf.prepare (monoSpec);
    cachedBassGainDb = std::numeric_limits<float>::lowest();
    cachedTrebleGainDb = std::numeric_limits<float>::lowest();
    updateShelfCoefficients();
    reset();
}

void SpaceEchoModule::reset()
{
    std::fill (hysteresisState.begin(), hysteresisState.end(), 0.0f);
    std::fill (previousInputState.begin(), previousInputState.end(), 0.0f);
    std::fill (directionState.begin(), directionState.end(), 0.0f);
    for (auto& rail : writeRail) rail.reset();
    for (auto& shelf : bassShelf) shelf.reset();
    for (auto& shelf : trebleShelf) shelf.reset();
    writeIndex = 0;
    validSamples = 0;
    lfoPhase = 0.0f;
    flutterPhase = 0.0f;
    delaySamples.setCurrentAndTargetValue (static_cast<float> (sampleRate * 0.375));
    wetMix.setCurrentAndTargetValue (0.0f);
    feedbackValue.setCurrentAndTargetValue (0.35f);
    wobbleValue.setCurrentAndTargetValue (0.0f);
    driveValue.setCurrentAndTargetValue (0.0f);
}

void SpaceEchoModule::setParameters (float timeMs, float repeats, float bassPercent, float treblePercent,
                                float wobble, float drive, float mix, bool enabled, int patternIndex)
{
    delaySamples.setTargetValue (juce::jlimit (0.04f, 2.5f, timeMs * 0.001f)
                                      * static_cast<float> (sampleRate));
    feedbackValue.setTargetValue (juce::jlimit (0.0f, 0.86f, repeats * 0.0086f));
    // +-12dB shelf range, centred (0dB, flat) at the knob's 50% position.
    bassGainDb = (juce::jlimit (0.0f, 100.0f, bassPercent) - 50.0f) * 0.24f;
    trebleGainDb = (juce::jlimit (0.0f, 100.0f, treblePercent) - 50.0f) * 0.24f;
    updateShelfCoefficients();
    wobbleValue.setTargetValue (juce::jlimit (0.0f, 1.0f, wobble * 0.01f));
    driveValue.setTargetValue (juce::jlimit (0.0f, 1.0f, drive * 0.01f));
    // Give guitarists much finer control over the useful low end of the Mix
    // knob. 10% now means subtle ambience, while the endpoint remains 100% wet.
    const auto normalisedMix = juce::jlimit (0.0f, 1.0f, mix * 0.01f);
    wetMix.setTargetValue (enabled ? std::pow (normalisedMix, 1.55f) : 0.0f);
    pattern = juce::jlimit (0, 5, patternIndex);
}

void SpaceEchoModule::updateShelfCoefficients()
{
    if (std::abs (bassGainDb - cachedBassGainDb) > 0.01f)
    {
        cachedBassGainDb = bassGainDb;
        auto coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            sampleRate, bassShelfHz, shelfQ, juce::Decibels::decibelsToGain (bassGainDb));
        bassShelf[0].coefficients = coefficients;
        bassShelf[1].coefficients = coefficients;
    }
    if (std::abs (trebleGainDb - cachedTrebleGainDb) > 0.01f)
    {
        cachedTrebleGainDb = trebleGainDb;
        auto coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRate, trebleShelfHz, shelfQ, juce::Decibels::decibelsToGain (trebleGainDb));
        trebleShelf[0].coefficients = coefficients;
        trebleShelf[1].coefficients = coefficients;
    }
}

float SpaceEchoModule::readDelay (int channel, float distance) const
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

float SpaceEchoModule::processHysteresisDrive (int channel, float sample, float driveAmount)
{
    // An explicit-integration adaptation of the Jiles-Atherton magnetic
    // hysteresis model: hysteresisState ("magnetisation") chases an
    // anhysteretic target on the Langevin curve, at a rate that depends on
    // whether the input is currently driving it toward or away from that
    // target (the actual hysteretic behaviour -- a real tape's response to
    // a rising field differs from its response to a falling one). Solved
    // explicitly per sample (using the previous state to evaluate the
    // target, rather than an implicit/Newton solve for the new one) trades
    // a little accuracy for guaranteed stability in a real-time audio path.
    constexpr float domainDensity = 0.35f;   // "a": curve shape/steepness
    constexpr float interDomainCoupling = 0.05f; // "alpha"
    constexpr float reversibleMix = 0.55f;   // "c": reversible/irreversible balance

    const auto gain = 1.0f + driveAmount * 4.0f;
    const auto x = sample * gain;

    auto& previousInput = previousInputState[static_cast<size_t> (channel)];
    const auto inputRate = x - previousInput;
    previousInput = x;

    auto& direction = directionState[static_cast<size_t> (channel)];
    direction += directionSmoothCoefficient * ((inputRate >= 0.0f ? 1.0f : -1.0f) - direction);

    auto& magnetisation = hysteresisState[static_cast<size_t> (channel)];
    const auto saturation = 0.5f + driveAmount * 0.9f;
    const auto q = (x + interDomainCoupling * magnetisation) / domainDensity;
    const auto target = saturation * langevin (q);
    const auto targetSlope = (saturation / domainDensity) * langevinDerivative (q);

    // Only chase the target when the input's direction actually agrees
    // with where the target currently sits relative to the state -- the
    // "irreversible" (pinning) component of the loop.
    const auto movingTowardTarget = (direction >= 0.0f) == (target - magnetisation >= 0.0f);
    const auto irreversibleRate = movingTowardTarget
        ? (1.0f - reversibleMix) * (target - magnetisation) * inputRate : 0.0f;
    const auto reversibleRate = reversibleMix * targetSlope * inputRate;
    const auto denominator = juce::jmax (0.2f, 1.0f - reversibleMix * interDomainCoupling * targetSlope);

    magnetisation = juce::jlimit (-2.5f, 2.5f, magnetisation + (irreversibleRate + reversibleRate) / denominator);
    return magnetisation / gain;
}

void SpaceEchoModule::getPattern (float* ratios, float* gains, int& taps) const
{
    // Real RE-201 head ratios are exactly 1:2:3 off head 1's delay time
    // (fixed, equally-spaced tape heads). Names stay the same; the head
    // combination each maps to:
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

void SpaceEchoModule::process (juce::AudioBuffer<float>& buffer)
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

            // Combine what's entering each line's record head BEFORE
            // applying Bass/Treble/Drive -- line 0 gets fresh mono input
            // plus line B's crossed feedback, line 1 gets only line A's
            // crossed feedback -- then tone/drive the combined signal, not
            // just the feedback contribution. Previously the raw (untoned)
            // lineAOut/lineBOut reads were what actually reached the
            // output below, with toneA/toneB only feeding the cross-write,
            // so a note's very first repeat carried none of the Bass/
            // Treble/Drive shaping (an audit-caught bug); now every write
            // is toned, so a later read of it is automatically toned too.
            const auto toRecordA = monoIn + lineBOut * feedback;
            const auto toRecordB = lineAOut * feedback;
            auto colouredA = trebleShelf[0].processSample (bassShelf[0].processSample (toRecordA));
            auto colouredB = trebleShelf[1].processSample (bassShelf[1].processSample (toRecordB));
            if (driveAmount > 0.0001f)
            {
                colouredA = processHysteresisDrive (0, colouredA, driveAmount);
                colouredB = processHysteresisDrive (1, colouredB, driveAmount);
            }

            delayBuffer.setSample (0, writeIndex, writeRail[0].process (colouredA, 1.0f, 2.0f));
            delayBuffer.setSample (1, writeIndex, writeRail[1].process (colouredB, 1.0f, 2.0f));

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

                const auto input = buffer.getSample (channel, sample);

                // Bass/Treble/Drive model the record head's own tone/
                // saturation circuitry, which colours everything being
                // recorded to tape -- fresh input and recirculated
                // feedback combined first, same as this module's own
                // header documentation and EchoModule/Plexer's analogous
                // `coloured` pattern. Previously tone/drive was only
                // applied to the feedback contribution *after* combining
                // with raw input, and the output tap read the raw
                // (untoned) `wet` directly -- so a note's first echo
                // repeat carried almost none of the Bass/Treble/Drive
                // shaping, an audit-caught bug. Now every write is toned,
                // so a later tap read of it is automatically toned too.
                const auto toRecord = input + wet * feedback;
                auto coloured = trebleShelf[channel].processSample (bassShelf[channel].processSample (toRecord));
                if (driveAmount > 0.0001f)
                    coloured = processHysteresisDrive (channel, coloured, driveAmount);

                delayBuffer.setSample (channel, writeIndex, writeRail[channel].process (coloured, 1.0f, 2.0f));
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
