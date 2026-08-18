#include "TremoloModule.h"

void TremoloModule::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    amount.reset (sampleRate, 0.025);
    rate.reset (sampleRate, 0.05);
    crossover.prepare (spec);
    crossover.setCutoffFrequency (700.0f);
    reset();
}

void TremoloModule::reset()
{
    amount.setCurrentAndTargetValue (0.0f);
    rate.setCurrentAndTargetValue (3.20f);
    phase = 0.0f;
    crossover.reset();
}

void TremoloModule::setAmount (float amountPercent)
{
    amount.setTargetValue (juce::jlimit (0.0f, 1.0f, amountPercent * 0.01f));
}

void TremoloModule::setRate (float rateHz)
{
    rate.setTargetValue (juce::jlimit (0.5f, 10.0f, rateHz));
}

void TremoloModule::process (juce::AudioBuffer<float>& buffer)
{
    if (! amount.isSmoothing() && amount.getCurrentValue() <= 0.00001f)
        return;

    const auto channels = buffer.getNumChannels();
    if (channels == 0)
        return;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto intensity = amount.getNextValue();
        const auto rateHz = rate.getNextValue();

        if (voice == Voice::harmonic)
        {
            // Same 0-1 LFO position as Bias below, just a different, milder
            // shaping curve -- a plain raised-cosine AM, appropriate for this
            // voice's simpler amplitude-modulated-band mechanism rather than
            // Bias's grid-cutoff-derived asymmetric curve.
            const auto depth = intensity * 0.9f;
            const auto cosPhase = std::cos (phase);
            const auto gainA = 1.0f - depth * 0.5f * (1.0f - cosPhase);
            const auto gainB = 1.0f - depth * 0.5f * (1.0f + cosPhase); // 180 degrees from gainA
            for (int channel = 0; channel < channels; ++channel)
            {
                float low = 0.0f, high = 0.0f;
                crossover.processSample (channel, buffer.getSample (channel, sample), low, high);
                // Left (or mono): low band follows gainA, high follows gainB.
                // Right: swapped -- see class comment for why that's what
                // makes this voice genuinely stereo rather than dual-mono.
                const auto isRight = channel == 1;
                const auto gainLow = isRight ? gainB : gainA;
                const auto gainHigh = isRight ? gainA : gainB;
                buffer.setSample (channel, sample, low * gainLow + high * gainHigh);
            }
        }
        else
        {
            const auto depth = intensity * 0.78f;
            const auto x = std::sin (phase); // -1 (toward cutoff) .. +1 (away from cutoff)

            // Asymmetric halves, same idiom used for the amp's push-pull stage:
            // toward cutoff (x<0) is shaped with an exponent >1 -- a convex curve
            // that stays gentle near the zero-crossing then accelerates hard into
            // the trough, mirroring gm collapsing near cutoff. Away from cutoff
            // (x>0) uses an exponent <1 -- a concave curve that rises quickly off
            // zero then flattens out approaching the tube's normal ceiling.
            const auto fall = x < 0.0f ? -std::pow (-x, 1.6f) : 0.0f;
            const auto rise = x > 0.0f ?  std::pow ( x, 0.7f) : 0.0f;
            const auto shapedNormalised = (fall + rise + 1.0f) * 0.5f; // 0 (trough) .. 1 (ceiling)
            const auto gain = 1.0f - depth * (1.0f - shapedNormalised);
            // The same gain drives both channels, so this voice can never
            // become autopan (unlike Harmonic above, by design).
            for (int channel = 0; channel < channels; ++channel)
                buffer.setSample (channel, sample, buffer.getSample (channel, sample) * gain);
        }

        phase += juce::MathConstants<float>::twoPi * rateHz / static_cast<float> (sampleRate);
        if (phase >= juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;
    }
}
