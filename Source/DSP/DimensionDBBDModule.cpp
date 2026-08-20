#include "DimensionDBBDModule.h"

void DimensionDBBDModule::getPresetSpec (int mode, float& centerSec, float& swingSec, float& rateHz)
{
    const auto& p = presets[(size_t) juce::jlimit (0, 3, mode)];
    centerSec = p.centerSec;
    swingSec = p.swingSec;
    rateHz = p.rateHz;
}

void DimensionDBBDModule::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    const auto sr = (float) sampleRate;

    // 16ms of delay covers the longest window (Mode I: 10ms centre + 2ms
    // swing + the ~0.25ms inter-line offset) with headroom at every rate the
    // plugin runs (48/96/192k), plus a few samples for interpolation.
    delayLine.setSize (2, (int) (sampleRate * 0.016) + 8);
    delayLine.clear();

    // Matched pre/de-emphasis shelves: the NE570 compander applies a fixed
    // high-frequency lift ahead of compression and the inverse cut after
    // expansion, so the boosted treble that buries BBD clock noise is
    // restored to flat at the output. Corner/boost follow the DC-2's
    // external emphasis RC (~4kHz, ~+4.5dB) -- a gentle, audible-brightness
    // lift, not an aggressive one.
    preEmphasis.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sr, 4000.0f, 0.7f, juce::Decibels::decibelsToGain (4.5f));
    deEmphasis.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sr, 4000.0f, 0.7f, juce::Decibels::decibelsToGain (-4.5f));

    // BBD anti-alias input LP and the two reconstruction LPs. The real DC-2
    // uses a 3rd-order AA and 4th/5th-order recon around the same ~7kHz
    // corner; a single 2nd-order section at that corner captures the audible
    // "dark BBD" band-limiting (the dominant effect), the extra order is
    // inaudible on guitar. Shared AA, one recon per line.
    aaFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, 7000.0f, 0.9f);
    reconA.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, 7000.0f, 0.9f);
    reconB.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, 7000.0f, 0.9f);

    // Input coupling cap -- a first-order HPF strips DC before the compander
    // rectifier so the envelope tracks signal, not offset.
    dcBlock.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 20.0f, 0.707f);

    // NE570 rectifier time constants: a few-ms attack and ~100ms release
    // (datasheet-recommended compander values set by the external rectifier
    // capacitor).
    compAttack = 1.0f - std::exp (-1.0f / (sr * 0.003f));
    compRelease = 1.0f - std::exp (-1.0f / (sr * 0.100f));
    expAttack = compAttack;
    expRelease = compRelease;

    for (auto* value : { &inputGain, &outputGain })
        value->reset (sampleRate, 0.05);
    reset();
}

void DimensionDBBDModule::reset()
{
    delayLine.clear();
    dcBlock.reset();
    preEmphasis.reset();
    deEmphasis.reset();
    aaFilter.reset();
    reconA.reset();
    reconB.reset();
    bbdSaturation.reset();
    writeIndex = 0;
    for (auto& phase : lfoPhase) phase = 0.0f;
    compEnv = 0.0f;
    expEnvA = 0.0f;
    expEnvB = 0.0f;
    inputGain.setCurrentAndTargetValue (1.4f);
    outputGain.setCurrentAndTargetValue (0.7f);
}

void DimensionDBBDModule::setParameters (int modeMaskIn, float inputLevel01, float outputLevel01)
{
    modeMask = modeMaskIn & 0xF;
    // Input level is the SDD-320's INPUT trim -- it drives the BBD harder as
    // it rises (into the tanh saturation), so 0-1 maps to 0-2x gain rather
    // than a straight 0-1x. Output level is a plain output trim.
    inputGain.setTargetValue (juce::jlimit (0.0f, 1.0f, inputLevel01) * 2.0f);
    outputGain.setTargetValue (juce::jlimit (0.0f, 1.0f, outputLevel01));
}

float DimensionDBBDModule::triangleWave (float phase) noexcept
{
    // Integrator/Schmitt LFO (the DC-2's IC7/IC6 arrangement) => a triangle
    // of constant absolute slope, not a sine. Normalised phase [0,2pi) maps
    // to [-1, 1]: -1 at 0, 0 at pi/2, +1 at pi, 0 at 3pi/2.
    const auto p = phase / juce::MathConstants<float>::twoPi;
    return p < 0.5f ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
}

float DimensionDBBDModule::readDelay (int line, float delaySamples) const
{
    // Linear interpolation is the faithful choice for a BBD: the real MN3007
    // stores a discrete charge packet per stage, so its delay is inherently
    // stepwise at the clock rate -- linear interpolation between neighbours
    // reproduces that gentle steppiness rather than smoothing it away
    // (Hermite would be "too clean" for a BBD; at the sub-ms swing rates
    // here the two are inaudibly close anyway).
    const auto size = delayLine.getNumSamples();
    auto position = (float) writeIndex - delaySamples;
    while (position < 0.0f) position += (float) size;
    while (position >= (float) size) position -= (float) size;
    const auto i0 = (int) position;
    const auto i1 = (i0 + 1) % size;
    const auto fraction = position - (float) i0;
    const auto y0 = delayLine.getSample (line, i0);
    const auto y1 = delayLine.getSample (line, i1);
    return y0 + (y1 - y0) * fraction;
}

void DimensionDBBDModule::process (juce::AudioBuffer<float>& buffer)
{
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();
    if (numSamples == 0 || numChannels == 0)
        return;

    const auto size = delayLine.getNumSamples();
    // Static base-delay offset between the two BBD lines -- the "two clock
    // oscillators at slightly different frequencies" from the DC-2, giving a
    // fixed ~0.5ms L/R spread that persists even at the LFO midpoint (where
    // the two lines would otherwise momentarily coincide and collapse to
    // mono).
    constexpr float lineOffset = 0.0005f;
    // NE570 nominal BBD operating level (the rectifier reference the 2:1 /
    // 1:2 gains are anchored to). Chosen so a ~0.5 peak guitar signal sits
    // right at the compander's transparent point.
    constexpr float referenceLevel = 0.5f;
    // Fixed dry/wet sum -- the Dimension D has no user mix control, its
    // "dimension" is a fixed sum of dry + delayed at these gains. dryGain is
    // held near unity so engaging the pedal doesn't audibly drop the dry
    // level; the wet sits ~-4dB under it (a subtle, width-adding chorus, not
    // a swimmy one).
    constexpr float dryGain = 0.85f;
    constexpr float wetGain = 0.55f;

    // How many of the four Dimension modes are engaged. The summed taps are
    // divided by this so stacking modes thickens rather than boosts -- the
    // combined-buttons RMS stays in line with the single-mode RMS the blend
    // was calibrated for.
    int activeCount = 0;
    for (int m = 0; m < 4; ++m)
        if ((modeMask >> m) & 1) ++activeCount;

    const auto chs = juce::jmin (2, numChannels);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto inGain = inputGain.getNextValue();
        const auto outGain = outputGain.getNextValue();

        // Mono input (guitar): average the first two channels into the wet
        // path while preserving each channel's dry signal for the blend.
        float dry[2] {};
        auto mono = 0.0f;
        for (int ch = 0; ch < chs; ++ch)
        {
            dry[ch] = buffer.getSample (ch, i);
            mono += dry[ch];
        }
        mono /= (float) chs;

        if (activeCount == 0)
        {
            // No mode engaged => dry passthrough (the pedal's own On toggle
            // remains the master bypass; this is just "no mode = no effect").
            if (numChannels >= 2)
            {
                buffer.setSample (0, i, dry[0] * dryGain);
                buffer.setSample (1, i, dry[1] * dryGain);
            }
            else
            {
                buffer.setSample (0, i, dry[0] * dryGain);
            }
            continue;
        }

        // --- NE570 compressor (2:1), rectified on the pre-emphasised input.
        const auto dc = dcBlock.processSample (mono * inGain);
        const auto emphasised = preEmphasis.processSample (dc);
        const auto absIn = std::abs (emphasised);
        compEnv += (absIn > compEnv ? compAttack : compRelease) * (absIn - compEnv);
        // 2:1 gain cell: g = sqrt(ref / env). Clamped so a silent input
        // can't demand infinite boost (and so the paired expander, g = env /
        // ref, stays within the same sane range). Product g_c * g_e ~ 1 =>
        // the compander is level-transparent, only the BBD noise floor is
        // pulled down during quiet passages.
        const auto compGain = juce::jlimit (0.1f, 3.0f, std::sqrt (referenceLevel / juce::jmax (compEnv, 0.001f)));
        const auto compressed = emphasised * compGain;

        // --- anti-alias LP, then the BBD's soft-saturated charge-transfer
        // cell, then write the same band-limited signal into both lines.
        const auto aa = aaFilter.processSample (compressed);
        const auto bbdIn = bbdSaturation.process (aa);
        delayLine.setSample (0, writeIndex, bbdIn);
        delayLine.setSample (1, writeIndex, bbdIn);

        // --- sum the active modes' modulated taps. Each mode runs its own
        // triangle LFO at its own rate around its own centre; summing several
        // is the SDD-320's "several mode buttons pressed at once" behaviour.
        auto wetA = 0.0f;
        auto wetB = 0.0f;
        for (int m = 0; m < 4; ++m)
        {
            if (! ((modeMask >> m) & 1))
                continue;
            const auto& preset = presets[(size_t) m];
            const auto tri = triangleWave (lfoPhase[m]);
            lfoPhase[m] += juce::MathConstants<float>::twoPi * preset.rateHz / (float) sampleRate;
            if (lfoPhase[m] >= juce::MathConstants<float>::twoPi)
                lfoPhase[m] -= juce::MathConstants<float>::twoPi;

            const auto delayA = (preset.centerSec - lineOffset * 0.5f + tri * preset.swingSec) * (float) sampleRate;
            const auto delayB = (preset.centerSec + lineOffset * 0.5f - tri * preset.swingSec) * (float) sampleRate;
            wetA += readDelay (0, delayA);
            wetB += readDelay (1, delayB);
        }
        wetA /= (float) activeCount;
        wetB /= (float) activeCount;

        // --- reconstruction LP + NE570 expander (1:2) on the summed wet.
        const auto reconOutA = reconA.processSample (wetA);
        const auto reconOutB = reconB.processSample (wetB);

        const auto absA = std::abs (reconOutA);
        const auto absB = std::abs (reconOutB);
        expEnvA += (absA > expEnvA ? expAttack : expRelease) * (absA - expEnvA);
        expEnvB += (absB > expEnvB ? expAttack : expRelease) * (absB - expEnvB);
        const auto expGainA = juce::jlimit (0.1f, 3.0f, expEnvA / referenceLevel);
        const auto expGainB = juce::jlimit (0.1f, 3.0f, expEnvB / referenceLevel);
        const auto outA = deEmphasis.processSample (reconOutA * expGainA);
        const auto outB = deEmphasis.processSample (reconOutB * expGainB);

        // --- fixed dry + delayed blend, then output trim.
        const auto wetL = outA * wetGain * outGain;
        const auto wetR = outB * wetGain * outGain;
        if (numChannels >= 2)
        {
            buffer.setSample (0, i, dry[0] * dryGain + wetL);
            buffer.setSample (1, i, dry[1] * dryGain + wetR);
        }
        else
        {
            buffer.setSample (0, i, dry[0] * dryGain + (wetL + wetR) * 0.5f);
        }

        writeIndex = (writeIndex + 1) % size;
    }
}
