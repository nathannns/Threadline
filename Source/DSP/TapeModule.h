#pragma once
#include <JuceHeader.h>

class TapeModule
{
public:
    enum Type { studio, cassette };
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setParameters (float drive, float compression, float tone, float age,
                        float mix, float volume, bool enabled, int type, int oversamplingMode);
    void process (juce::AudioBuffer<float>& buffer);
    bool isWetTransitionActive() const noexcept
    {
        return wetMix.isSmoothing() || wetMix.getCurrentValue() > 0.00001f;
    }

    // Real group delay from the active oversampler was previously never
    // reported to the host at all -- an audit-caught DAW latency/PDC
    // accuracy bug. Pinned to the 4x instance's latency regardless of the
    // live oversampling setting (both instances are always fully prepared,
    // so this is always available to query), matching the same deliberate
    // "report the worst case, don't renegotiate latency on every settings
    // tweak" convention KlonNode/TS9Node/AmpNode/FangsNode/BisonNode/
    // GrowlNode already use -- latency is only re-read on pedalboard order
    // changes (see PedalChainRunner::publishOrder()), not on every param
    // change, so a value that could silently vary with the Oversampling
    // dropdown would risk going stale between those events.
    // Reports the actually-selected instance's real latency (not pinned to
    // 4x) -- PedalChainRunner::processChain() now watches "ampOversampling"
    // for changes and re-publishes latency accordingly.
    int getLatencySamples() const noexcept
    {
        if (oversamplingChoice == 1 && oversampling2x != nullptr)
            return juce::roundToInt (oversampling2x->getLatencyInSamples());
        if (oversamplingChoice == 2 && oversampling4x != nullptr)
            return juce::roundToInt (oversampling4x->getLatencyInSamples());
        return 0;
    }

private:
    void processCore (juce::AudioBuffer<float>& buffer, double processingRate);
    float readWow (int channel, float delayInSamples) const;
    juce::AudioBuffer<float> wowBuffer;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling2x, oversampling4x;
    juce::SmoothedValue<float> wetMix;
    std::vector<float> envelope, detectorLowState, magnetisationState;
    std::vector<float> toneState, bassState, midState;
    // Previous sample's driven (post-record-gain) value per channel, so the
    // saturation curve can tell whether the field is currently rising or
    // falling -- the actual defining signature of magnetic hysteresis.
    std::vector<float> previousDrivenState;
    // Lowpassed version of the raw rising/falling sign above (see
    // processCore's directionSmooth comment) -- a real tape's hysteresis
    // loop doesn't flip its direction state on every sample-to-sample
    // wiggle, so smoothing the instantaneous +-1 sign rather than using it
    // directly avoids injecting broadband, alias-prone "fizz" on harmonic-
    // rich, heavily-driven signal.
    std::vector<float> directionSmoothState;
    double sampleRate = 44100.0;
    int writeIndex = 0, tapeType = studio, oversamplingChoice = 0;
    int validSamples = 0;
    float driveValue = 0.0f, compValue = 0.0f, toneValue = 0.5f, ageValue = 0.0f;
    // Master output level (Volume knob), applied after the whole tape path so
    // it scales the pedal's output without touching its saturation character.
    // Unity at the default 100%, so sessions that never touch the knob are
    // unchanged. Not applied at the exact neutral point (drive/comp/age all
    // zero, tone at 60%) because process() short-circuits that transparent
    // passthrough before any tape path runs.
    float volumeValue = 1.0f;
    float lfoPhase = 0.0f;
};
