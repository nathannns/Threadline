#pragma once
#include <JuceHeader.h>
#include <BinaryData.h>

// Hall/Room reverb built from real Lexicon 480L impulse responses (3 spaces:
// Large Hall, Large Stage, Small Room) rather than a synthesized algorithm.
// These IRs already contain a complete,
// natural decay, so there's no tail synthesis — just pre-delay, convolution,
// damping, and (see setParameters/loadImpulse) decay-time shaping applied to
// the captured impulse itself.
//
// Decay is "shaped convolution": since this module convolves a continuous
// input stream rather than playing back a single triggered impulse, there's
// no per-sample "time since t=0" to decay a live envelope from — so, like
// real convolution reverbs that offer an adjustable decay/size control on a
// fixed captured IR, the Decay knob re-envelopes the loaded impulse itself
// (keep a leading portion at full level, taper the rest out with a smooth
// window) rather than shaping the live wet signal. That means changing Decay
// triggers an impulse reload (async, off the audio thread, same as switching
// models) rather than being instantaneously live like Tone or Mix — a real,
// deliberate trade-off: reprocessing convolution partitions on every sample
// isn't something any convolution engine does.
class HallRoomReverbModule : private juce::AsyncUpdater
{
public:
    ~HallRoomReverbModule() override { cancelPendingUpdate(); }

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // preDelay/decay/tone are normalised 0-1 (mapped internally to ms / tail
    // shape / Hz — decay: 1.0 = the room's full natural captured decay,
    // lower values trim the tail shorter); mix and width are 0-100 (width:
    // 50 = the IR's natural stereo image, 0 = mono, 100 = doubled side).
    void setParameters (float preDelayNormalised, float decayNormalised, float toneNormalised, float mix,
                        float widthPercent, bool enabled, int modelIndex);
    void process (juce::AudioBuffer<float>& buffer);

    static constexpr int numModels = 3;
    static const char* getModelName (int index)
    {
        static const char* names[numModels] { "Large Hall", "Large Stage", "Small Room" };
        return names[juce::jlimit (0, numModels - 1, index)];
    }

private:
    void handleAsyncUpdate() override;
    void loadImpulse (int index, float decay01);
    static int decayToStep (float decay01) { return juce::roundToInt (juce::jlimit (0.0f, 1.0f, decay01) * 24.0f); }

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
    std::atomic<int> requestedDecayStep { 24 };
    std::atomic<float> requestedDecay01 { 1.0f };
    int loadedImpulse = -1;
    int loadedDecayStep = -1;
    int maximumBlockSize = 0, channelCount = 0;
    double sampleRate = 44100.0;
    float cachedToneHz = -1.0f;
    float widthFactor = 1.0f; // 1.0 = the IR's own stereo image, unchanged
};
