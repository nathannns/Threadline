#include "TapeModule.h"

void TapeModule::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    const auto channels = static_cast<int> (spec.numChannels);
    const auto channelCount = static_cast<size_t> (channels);
    wowBuffer.setSize (channels, static_cast<int> (sampleRate * 4.0 * 0.04) + 4);
    wowBuffer.clear();
    envelope.assign (channelCount, 0.0f); toneState.assign (channelCount, 0.0f);
    detectorLowState.assign (channelCount, 0.0f); magnetisationState.assign (channelCount, 0.0f);
    bassState.assign (channelCount, 0.0f); midState.assign (channelCount, 0.0f);
    previousDrivenState.assign (channelCount, 0.0f);
    directionSmoothState.assign (channelCount, 0.0f);
    oversampling2x = std::make_unique<juce::dsp::Oversampling<float>> (
        static_cast<size_t> (channels), 1,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampling4x = std::make_unique<juce::dsp::Oversampling<float>> (
        static_cast<size_t> (channels), 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampling2x->initProcessing (spec.maximumBlockSize);
    oversampling4x->initProcessing (spec.maximumBlockSize);
    wetMix.reset (sampleRate * 4.0, 0.02);
    reset();
}

void TapeModule::reset()
{
    writeIndex = 0; validSamples = 0; lfoPhase = 0.0f;
    std::fill (envelope.begin(), envelope.end(), 0.0f);
    std::fill (detectorLowState.begin(), detectorLowState.end(), 0.0f);
    std::fill (magnetisationState.begin(), magnetisationState.end(), 0.0f);
    std::fill (toneState.begin(), toneState.end(), 0.0f);
    std::fill (bassState.begin(), bassState.end(), 0.0f);
    std::fill (midState.begin(), midState.end(), 0.0f);
    std::fill (previousDrivenState.begin(), previousDrivenState.end(), 0.0f);
    std::fill (directionSmoothState.begin(), directionSmoothState.end(), 0.0f);
    wetMix.setCurrentAndTargetValue (0.0f);
    if (oversampling2x != nullptr) oversampling2x->reset();
    if (oversampling4x != nullptr) oversampling4x->reset();
}

void TapeModule::setParameters (float drive, float compression, float tone, float age,
                                float mix, float volume, bool enabled, int type, int oversamplingMode)
{
    const auto normalisedDrive = juce::jlimit (0.0f, 1.0f, drive * 0.01f);
    // A slower lower half gives useful clean headroom. The nonlinearity itself
    // is level-dependent, so no artificial playing-strength gate is needed.
    driveValue = std::pow (normalisedDrive, 1.05f);
    compValue = std::pow (juce::jlimit (0.0f, 1.0f, compression * 0.01f), 0.82f);
    toneValue = juce::jlimit (0.0f, 1.0f, tone * 0.01f);
    ageValue = juce::jlimit (0.0f, 1.0f, age * 0.01f);
    // Volume is a plain master trim (0-100% -> 0-1.0 gain), unity at 100% so
    // the default is a no-op. Linear on purpose: "50" reads as half.
    volumeValue = juce::jlimit (0.0f, 1.0f, volume * 0.01f);
    tapeType = juce::jlimit (0, 1, type);
    const auto requestedOversampling = juce::jlimit (0, 2, oversamplingMode);
    if (requestedOversampling != oversamplingChoice)
    {
        oversamplingChoice = requestedOversampling;
        writeIndex = validSamples = 0;
        lfoPhase = 0.0f;
        if (oversamplingChoice == 1 && oversampling2x != nullptr) oversampling2x->reset();
        if (oversamplingChoice == 2 && oversampling4x != nullptr) oversampling4x->reset();
    }
    wetMix.setTargetValue (enabled ? juce::jlimit (0.0f, 1.0f, mix * 0.01f) : 0.0f);
}

float TapeModule::readWow (int channel, float distance) const
{
    if (distance > static_cast<float> (validSamples))
        return 0.0f;
    const auto size = wowBuffer.getNumSamples();
    auto position = static_cast<float> (writeIndex) - distance;
    while (position < 0.0f) position += static_cast<float> (size);
    const auto first = static_cast<int> (position) % size;
    const auto second = (first + 1) % size;
    return juce::jmap (position - std::floor (position), wowBuffer.getSample (channel, first),
                      wowBuffer.getSample (channel, second));
}

void TapeModule::process (juce::AudioBuffer<float>& buffer)
{
    if (! wetMix.isSmoothing() && wetMix.getCurrentValue() <= 0.00001f)
        return;

    // At the calibrated neutral point this module is mathematically clean;
    // avoid running the record/repro model at all.
    if (driveValue <= 0.000001f && compValue <= 0.000001f
        && ageValue <= 0.000001f && std::abs (toneValue - 0.60f) <= 0.0001f)
        return;

    if (oversamplingChoice == 0)
    {
        processCore (buffer, sampleRate);
        return;
    }
    auto* oversampler = oversamplingChoice == 1 ? oversampling2x.get() : oversampling4x.get();
    if (oversampler == nullptr)
        return;
    juce::dsp::AudioBlock<float> inputBlock (buffer);
    auto oversampledBlock = oversampler->processSamplesUp (inputBlock);
    std::array<float*, 2> channelPointers {};
    const auto oversampledChannels = juce::jmin (channelPointers.size(),
                                                  oversampledBlock.getNumChannels());
    for (size_t channel = 0; channel < oversampledChannels; ++channel)
        channelPointers[channel] = oversampledBlock.getChannelPointer (channel);
    juce::AudioBuffer<float> oversampledBuffer (channelPointers.data(),
                                                static_cast<int> (oversampledChannels),
                                                static_cast<int> (oversampledBlock.getNumSamples()));
    processCore (oversampledBuffer, sampleRate * (oversamplingChoice == 1 ? 2.0 : 4.0));
    oversampler->processSamplesDown (inputBlock);
}

void TapeModule::processCore (juce::AudioBuffer<float>& buffer, double processingRate)
{
    const auto channels = juce::jmin (buffer.getNumChannels(), wowBuffer.getNumChannels());
    const auto cassetteMode = tapeType == cassette;
    constexpr float kneeDb = 8.0f;
    // Cassette is a deliberately different medium, not merely the Studio
    // curve with more Drive: narrower bandwidth and a stronger low-mid body
    // remain audible even with AGE at zero.
    const auto cutoffTop = cassetteMode ? 10500.0f : 20000.0f;
    const auto cutoffBottom = cassetteMode ? 1900.0f : 5000.0f;
    const auto colouredCutoff = juce::jmap (toneValue, cutoffBottom, cutoffTop)
                              * (1.0f - ageValue * (cassetteMode ? 0.68f : 0.42f));
    const auto transparentCutoff = juce::jmin (20000.0f, static_cast<float> (processingRate * 0.45));
    const auto transparentToneCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                                              * transparentCutoff / static_cast<float> (processingRate));
    const auto colouredToneCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                                           * colouredCutoff / static_cast<float> (processingRate));
    const auto bassCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                                   * (cassetteMode ? 155.0f : 85.0f)
                                                   / static_cast<float> (processingRate));
    const auto midCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                                  * 1250.0f / static_cast<float> (processingRate));
    const auto detectorLowCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                                          * 120.0f / static_cast<float> (processingRate));
    const auto magnetisationCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                                            * (cassetteMode ? 5200.0f : 7800.0f)
                                                            / static_cast<float> (processingRate));
    // Smooths the hysteresis direction sign below (see its own comment) --
    // 500Hz keeps essentially the full effect through most of the guitar's
    // played range (measured >99% of full swing up to ~330Hz, still ~90%
    // at 660Hz) while cutting a harmonic-rich driven signal's spurious
    // direction-flip energy by roughly 40% in a standalone harness, versus
    // the previous instantaneous sign.
    const auto directionSmoothCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                                              * 500.0f / static_cast<float> (processingRate));
    auto attack = 0.0f;
    auto release = 0.0f;
    if (compValue > 0.000001f)
    {
        const auto attackSeconds = juce::jmap (compValue, 0.014f, 0.006f);
        const auto releaseSeconds = juce::jmap (compValue, 0.145f, 0.300f);
        attack = std::exp (-1.0f / (static_cast<float> (processingRate) * attackSeconds));
        release = std::exp (-1.0f / (static_cast<float> (processingRate) * releaseSeconds));
    }
    const auto wowRate = cassetteMode ? 0.75f : 0.32f;
    // Keep wow subtle. Large values mixed with the dry path create audible
    // comb-filter amplitude movement that can be mistaken for tremolo.
    const auto wowDepthMs = std::pow (ageValue, 0.78f) * (cassetteMode ? 1.15f : 0.28f);
    // A tape insert has one series signal path. The physical record-to-repro
    // travel time is irrelevant to the timbre and must not be mixed against an
    // undelayed copy; doing so created the detached "distortion underneath"
    // sound. Only AGE introduces a very short transport displacement.
    const auto baseDelay = static_cast<float> (processingRate) * 0.00065f;
    const auto ageProcessingEnabled = ageValue > 0.0001f && driveValue > 0.0001f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto character = wetMix.getNextValue();
        // Perceptual taper: useful tape colour arrives before the upper half
        // of MIX, but zero still resolves to an exact clean path.
        const auto characterAmount = std::pow (character, 0.55f);
        const auto tapeAmount = characterAmount * driveValue;
        const auto effectiveAge = tapeAmount * ageValue;
        const auto effectiveComp = std::pow (character, 0.70f) * compValue;
        const auto thresholdDb = (cassetteMode ? -12.0f : -10.0f) - effectiveComp * 12.0f;
        const auto ratio = 1.0f + effectiveComp * 4.5f;
        const auto makeupDb = effectiveComp * (cassetteMode ? 10.0f : 11.0f);
        const auto toneCoefficient = juce::jmap (tapeAmount, transparentToneCoefficient,
                                                  colouredToneCoefficient);
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto dry = buffer.getSample (channel, sample);

            if (ageProcessingEnabled)
                wowBuffer.setSample (channel, writeIndex, dry);
            // Both channels share the same transport motion. A stereo phase
            // offset made mono guitars wander from left to right.
            auto x = dry;
            if (ageProcessingEnabled && effectiveAge > 0.0001f)
            {
                const auto wow = std::sin (lfoPhase) * wowDepthMs * effectiveAge * 0.001f
                               * static_cast<float> (processingRate);
                x = readWow (channel, baseDelay + wow);
            }

            // Drive represents record flux above a unity-calibrated clean
            // baseline. Increasing it lowers available headroom; inverse gain
            // compensation keeps the nominal output stable like a calibrated
            // recorder rather than behaving as an ordinary volume control.
            const auto inputDb = tapeAmount * (cassetteMode ? 29.0f : 23.0f);
            const auto inputGain = juce::Decibels::decibelsToGain (inputDb);
            // Computed once and reused below (was two separate decibelsToGain
            // calls for the exact same value) -- same result, one fewer
            // transcendental call per channel per sample.
            const auto inverseInputGain = 1.0f / inputGain;
            const auto driven = x * inputGain;
            const auto preSaturationBody = std::tanh (driven * 0.55f) * inverseInputGain;
            const auto distortionAmount = std::pow (tapeAmount, 1.12f);
            const auto bias = distortionAmount * (cassetteMode ? 0.105f : 0.032f);
            const auto shape = 1.0f + distortionAmount * (cassetteMode ? 3.65f : 2.60f)
                                     + effectiveAge * (cassetteMode ? 0.45f : 0.24f);

            // Real magnetic hysteresis is fundamentally direction-dependent --
            // a tape's response to a rising field differs from its response to
            // a falling one, and that asymmetry widens the harder the tape is
            // driven. Shift the curve's effective bias by the sign of the
            // field's rate of change, with a width that grows with Drive, so
            // the loop genuinely opens up (and gets more odd-harmonic/fuzzy)
            // at high Drive as an emergent property of the same curve, rather
            // than the direction-blind one-pole "magnetisation memory" alone.
            auto& previousDriven = previousDrivenState[static_cast<size_t> (channel)];
            const auto rawDirection = (driven - previousDriven) >= 0.0f ? 1.0f : -1.0f;
            previousDriven = driven;
            // Lowpassing the raw +-1 sign itself (rather than using it
            // directly, or lowpassing the delta before taking its sign)
            // means a fast, low-amplitude wiggle that flips back and forth
            // faster than the filter can track just averages toward zero,
            // while a genuine sustained direction change (a real half-cycle
            // of playing) still settles close to +-1 well within it -- see
            // directionSmoothCoefficient's comment above for why a hard
            // instantaneous sign here was audible as broadband "fizz".
            auto& directionSmooth = directionSmoothState[static_cast<size_t> (channel)];
            directionSmooth += directionSmoothCoefficient * (rawDirection - directionSmooth);
            const auto direction = directionSmooth;
            const auto hysteresisWidth = distortionAmount * (cassetteMode ? 0.14f : 0.09f);
            const auto directionalBias = bias + direction * hysteresisWidth;
            const auto biasTanh = std::tanh (directionalBias);
            const auto smallSignalSlope = shape * (1.0f - biasTanh * biasTanh);
            const auto anhysteretic = (std::tanh (driven * shape + directionalBias) - biasTanh)
                                    / juce::jmax (0.1f, smallSignalSlope);
            auto& magnetisation = magnetisationState[static_cast<size_t> (channel)];
            magnetisation += magnetisationCoefficient * (anhysteretic - magnetisation);
            const auto memoryMix = distortionAmount * (cassetteMode ? 0.25f : 0.12f)
                                 + effectiveAge * 0.08f;
            x = anhysteretic * (1.0f - memoryMix) + magnetisation * memoryMix;
            // Cancel the record gain for small signals. Drive therefore lowers
            // headroom instead of acting like a volume knob; only peaks that
            // approach the magnetic ceiling are compressed and saturated.
            x *= inverseInputGain;

            // Above roughly 78% Drive, blend in a small, level-compensated
            // magnetic-overload edge. It adds the requested fuzzy hair only
            // at the top of the existing response instead of redesigning the
            // low and mid Drive range.
            const auto fuzzAmount = juce::jlimit (0.0f, 1.0f,
                                                   (driveValue - 0.78f) / 0.22f)
                                  * characterAmount;
            if (fuzzAmount > 0.0001f)
            {
                const auto fuzzDrive = cassetteMode ? 5.8f : 4.6f;
                const auto asymmetry = cassetteMode ? 0.075f : 0.045f;
                const auto dc = std::tanh (asymmetry * fuzzDrive);
                const auto fuzzy = (std::tanh ((x + asymmetry) * fuzzDrive) - dc)
                                 / fuzzDrive;
                x = juce::jmap (fuzzAmount * (cassetteMode ? 0.15f : 0.11f), x, fuzzy);
            }

            auto& lp = toneState[static_cast<size_t> (channel)];
            lp += toneCoefficient * (x - lp);
            auto& bass = bassState[static_cast<size_t> (channel)];
            bass += bassCoefficient * (preSaturationBody - bass);
            auto& mids = midState[static_cast<size_t> (channel)];
            mids += midCoefficient * (preSaturationBody - mids);
            // Preserve body as Tone moves bright, while Age adds an audible
            // head-bump and increasingly worn, softened character.
            const auto brightBody = juce::jlimit (0.0f, 1.0f, (toneValue - 0.52f) / 0.48f);
            const auto darkBody = std::pow (juce::jlimit (0.0f, 1.0f,
                                                          (0.60f - toneValue) / 0.60f), 1.15f);
            const auto driveBody = distortionAmount;
            const auto midBody = tapeAmount * (cassetteMode ? 0.085f : 0.0f)
                               + brightBody * (cassetteMode ? 0.16f : 0.11f)
                               + driveBody * (cassetteMode ? 0.13f : 0.13f)
                               + darkBody * characterAmount * (cassetteMode ? 0.13f : 0.10f);
            const auto ageBump = effectiveAge * (cassetteMode ? 0.14f : 0.10f);
            const auto driveBass = driveBody * (cassetteMode ? 0.16f : 0.20f);
            x = lp + mids * midBody
                   + bass * (tapeAmount * (cassetteMode ? 0.075f : 0.085f)
                           + ageBump + driveBass
                           + darkBody * characterAmount * (cassetteMode ? 0.22f : 0.17f));

            // There is no dry/wet summation here. MIX morphs the strength of
            // this single calibrated tape path, so its harmonics belong to the
            // note instead of sounding like a second distorted track.
            auto compressed = tapeAmount <= 0.000001f ? dry : x;
            if (effectiveComp > 0.000001f)
            {
                auto& detectorLow = detectorLowState[static_cast<size_t> (channel)];
                detectorLow += detectorLowCoefficient * (compressed - detectorLow);
                const auto detectorSample = compressed - detectorLow * 0.76f;
                const auto level = std::abs (detectorSample);
                auto& env = envelope[static_cast<size_t> (channel)];
                env = level > env ? attack * env + (1.0f - attack) * level
                                  : release * env + (1.0f - release) * level;
                const auto envelopeDb = juce::Decibels::gainToDecibels (env, -120.0f);
                const auto overDb = envelopeDb - thresholdDb;
                float reductionDb = 0.0f;
                if (overDb >= kneeDb * 0.5f)
                    reductionDb = overDb * (1.0f - 1.0f / ratio);
                else if (overDb > -kneeDb * 0.5f)
                {
                    const auto kneePosition = overDb + kneeDb * 0.5f;
                    reductionDb = (1.0f - 1.0f / ratio)
                                * kneePosition * kneePosition / (2.0f * kneeDb);
                }
                const auto makeupActivity = juce::jlimit (0.0f, 1.0f,
                                                           (envelopeDb + 72.0f) / 30.0f);
                compressed *= juce::Decibels::decibelsToGain (
                    makeupDb * makeupActivity - reductionDb + effectiveComp * makeupActivity * 2.0f);
                compressed += detectorLow * effectiveComp * 0.16f;
            }
            // A calibrated recorder gets denser as it is driven, but it should
            // not become quieter. Add restrained post-tape level lift mostly
            // in the upper Drive range, after compression gain reduction.
            const auto driveLiftDb = std::pow (tapeAmount, 1.45f)
                                   * (cassetteMode ? 3.1f : 2.7f);
            compressed *= juce::Decibels::decibelsToGain (driveLiftDb);
            compressed *= volumeValue;
            buffer.setSample (channel, sample, compressed);
        }
        if (ageProcessingEnabled)
        {
            writeIndex = (writeIndex + 1) % wowBuffer.getNumSamples();
            validSamples = juce::jmin (validSamples + 1, wowBuffer.getNumSamples());
            lfoPhase += juce::MathConstants<float>::twoPi * wowRate / static_cast<float> (processingRate);
            if (lfoPhase >= juce::MathConstants<float>::twoPi)
                lfoPhase -= juce::MathConstants<float>::twoPi;
        }
    }
}
