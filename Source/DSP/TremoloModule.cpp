#include "TremoloModule.h"

void TremoloModule::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    amount.reset (sampleRate, 0.025);
    reset();
}

void TremoloModule::reset()
{
    amount.setCurrentAndTargetValue (0.0f);
    phase = 0.0f;
}

void TremoloModule::setAmount (float amountPercent)
{
    amount.setTargetValue (juce::jlimit (0.0f, 1.0f, amountPercent * 0.01f));
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
        const auto depth = intensity * 0.78f;
        // Fender-style bias tremolo: a rounded, slightly compressed sine at a
        // classic amp speed. It pulses warmly without chopping fully to zero.
        // The same gain drives both channels, so this can never become autopan.
        constexpr auto rateHz = 3.20f;
        constexpr auto curve = 1.25f;
        const auto sine = std::sin (phase);
        const auto wave = 0.5f + 0.5f * std::tanh (curve * sine) / std::tanh (curve);
        const auto gain = 1.0f - depth * (1.0f - wave);
        for (int channel = 0; channel < channels; ++channel)
            buffer.setSample (channel, sample, buffer.getSample (channel, sample) * gain);

        phase += juce::MathConstants<float>::twoPi * rateHz / static_cast<float> (sampleRate);
        if (phase >= juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;
    }
}
