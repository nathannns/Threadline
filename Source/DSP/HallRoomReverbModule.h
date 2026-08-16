#pragma once
#include <JuceHeader.h>
#include <BinaryData.h>

// Hall/Room reverb built from real Lexicon 480L impulse responses (7 spaces:
// Large/Small Hall, Large/Small Stage, Small Church, Large/Small Room) rather
// than a synthesized algorithm. Unlike SpringModule — which excites a
// physically-modeled spring network (dwell drive, dispersion lines, a
// synthesized late-field tail) — these IRs already contain a complete,
// natural decay, so this module is just pre-delay + convolution + damping,
// with no tail synthesis needed.
class HallRoomReverbModule : private juce::AsyncUpdater
{
public:
    ~HallRoomReverbModule() override { cancelPendingUpdate(); }

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // preDelay/tone are normalised 0-1 (mapped internally to ms / Hz); mix and
    // width are 0-100 (width: 50 = the IR's natural stereo image, 0 = mono,
    // 100 = doubled side-signal), matching the existing knobs' 0-100 convention.
    void setParameters (float preDelayNormalised, float toneNormalised, float mix,
                        float widthPercent, bool enabled, int modelIndex);
    void process (juce::AudioBuffer<float>& buffer);

    static constexpr int numModels = 7;
    static const char* getModelName (int index)
    {
        static const char* names[numModels] {
            "Large Hall", "Large Stage", "Small Church", "Small Hall",
            "Small Stage", "Large Room", "Small Room"
        };
        return names[juce::jlimit (0, numModels - 1, index)];
    }

private:
    void handleAsyncUpdate() override;
    void loadImpulse (int index);

    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 256 } };
    // Damping (lowpass) uses a TPT filter so it can be modulated per-sample
    // without zipper artifacts; the rumble filter is a fixed highpass that
    // keeps the tail from building up mud, independent of the Tone knob.
    juce::dsp::StateVariableTPTFilter<float> dampingFilter, rumbleFilter;
    juce::dsp::DelayLine<float> preDelayLine;
    juce::AudioBuffer<float> wetBuffer;
    juce::SmoothedValue<float> wetMix;

    // A true stereo IR captured once is otherwise static/narrow; a slow,
    // sub-millisecond pre-delay modulation per channel (out of phase L/R)
    // decorrelates the two sides slightly for a wider, less phasey image
    // without colouring the captured room's own character.
    float modPhaseLeft = 0.0f, modPhaseRight = juce::MathConstants<float>::pi;
    float basePreDelaySamples = 0.0f;

    std::atomic<int> requestedImpulse { 0 };
    int loadedImpulse = -1;
    int maximumBlockSize = 0, channelCount = 0;
    double sampleRate = 44100.0;
    float cachedToneHz = -1.0f;
    float widthFactor = 1.0f; // 1.0 = the IR's own stereo image, unchanged
};
