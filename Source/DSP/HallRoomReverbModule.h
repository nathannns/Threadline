#pragma once
#include <JuceHeader.h>

// Algorithmic Room/Hall/Plate reverb -- a faithful port of the exact
// Freeverb structure and gain-staging used by JUCE's own juce::Reverb class
// (juce_audio_basics/utilities/juce_Reverb.h), which is itself what HISE's
// own shipped SimpleReverb effect wraps rather than rolling its own. Two
// earlier custom versions of this module (independent combs with a guessed
// input gain, then an 8-line Householder FDN) both needed Mix pushed
// unrealistically high to hear anything -- because their gain constants
// were guessed rather than taken from a working reference. This version
// uses JUCE's actual proven numbers instead of guessing: 0.015 input gain
// into the comb bank, allpass feedback of 0.5, and critically the same
// damping formula placement (a one-pole lowpass inside each comb's own
// feedback path, not a separate post-filter) -- these specific constants
// are why a stock juce::Reverb sounds full and correctly balanced at
// ordinary-looking Mix settings instead of needing to be driven hot.
//
// 8 comb filters run in parallel per channel (accumulated), followed by 4
// allpass filters in series per channel -- exactly JUCE's topology, just
// duplicated into 3 pre-sized/pre-tuned tanks for Room/Hall/Plate rather
// than JUCE's single continuously-sized model, so switching Model is a
// same-thread index swap with no reload or click. Plate is deliberately
// tuned bigger than Hall (longer comb/allpass line-length scale, a higher
// feedback ceiling, and denser diffusion via a higher allpass coefficient)
// -- a real plate's whole surface resonates almost simultaneously, which
// reads as more enveloping than a room-shaped space despite the smaller
// physical device. Tone drives each comb's damping coefficient (JUCE's own
// `damp`/`1-damp` blend), with a fixed brightness bias for Plate. Decay
// drives comb feedback within a per-space range. A tanh safety rail is
// kept as a backstop since resonant combs can in principle still build
// gain at coincident frequencies, but with real gain-staging this time it
// should rarely actually engage.
class HallRoomReverbModule
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // preDelay/decay/tone are normalised 0-1; mix and width are 0-100
    // (width: 50 = the tank's natural stereo image, 0 = mono, 100 = doubled
    // side).
    void setParameters (float preDelayNormalised, float decayNormalised, float toneNormalised, float mix,
                        float widthPercent, bool enabled, int modelIndex);
    void process (juce::AudioBuffer<float>& buffer);

    static constexpr int numModels = 3;
    static const char* getModelName (int index)
    {
        static const char* names[numModels] { "Room", "Hall", "Plate" };
        return names[juce::jlimit (0, numModels - 1, index)];
    }

    static constexpr int numCombs = 8;
    static constexpr int numAllpasses = 4;

private:
    // Exactly JUCE's CombFilter: the damping one-pole lives inside the
    // feedback path itself (`last`), not as a separate post-filter stage.
    struct Comb
    {
        std::vector<float> buffer;
        int index = 0;
        float last = 0.0f;

        void setSize (int size)
        {
            buffer.assign (static_cast<size_t> (juce::jmax (4, size)), 0.0f);
            index = 0;
        }

        void reset()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            last = 0.0f;
        }

        float process (float input, float dampAmount, float feedbackAmount)
        {
            const auto output = buffer[static_cast<size_t> (index)];
            last = (output * (1.0f - dampAmount)) + (last * dampAmount);
            const auto temp = input + (last * feedbackAmount);
            buffer[static_cast<size_t> (index)] = temp;
            if (++index >= static_cast<int> (buffer.size())) index = 0;
            return output;
        }
    };

    // Exactly JUCE's AllPassFilter (fixed feedback per model rather than a
    // single hardcoded 0.5 everywhere -- Plate runs it higher for denser
    // diffusion, see file header).
    struct Allpass
    {
        std::vector<float> buffer;
        int index = 0;
        float feedback = 0.5f;

        void setSize (int size)
        {
            buffer.assign (static_cast<size_t> (juce::jmax (4, size)), 0.0f);
            index = 0;
        }

        void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

        float process (float input)
        {
            const auto bufferedValue = buffer[static_cast<size_t> (index)];
            const auto temp = input + (bufferedValue * feedback);
            buffer[static_cast<size_t> (index)] = temp;
            if (++index >= static_cast<int> (buffer.size())) index = 0;
            return bufferedValue - input;
        }
    };

    struct Tank
    {
        Comb combL[numCombs], combR[numCombs];
        Allpass allpassL[numAllpasses], allpassR[numAllpasses];

        void reset()
        {
            for (auto& c : combL) c.reset();
            for (auto& c : combR) c.reset();
            for (auto& a : allpassL) a.reset();
            for (auto& a : allpassR) a.reset();
        }
    };

    void prepareTank (Tank& tank, int modelIndex);

    Tank tanks[numModels];
    int activeModel = 0;
    float damp = 0.2f;
    float feedback = 0.7f;

    // Fixed rumble cut on the wet tail, independent of the Tone knob --
    // keeps the tail from building up mud regardless of how bright Tone
    // is set.
    juce::dsp::StateVariableTPTFilter<float> rumbleFilter;
    juce::dsp::DelayLine<float> preDelayLine;
    juce::AudioBuffer<float> wetBuffer;
    juce::SmoothedValue<float> wetMix;

    // A slow, sub-millisecond pre-delay modulation per channel (out of
    // phase L/R) decorrelates the two sides slightly for a wider, less
    // phasey image on top of the tank's own inherent stereo spread.
    float modPhaseLeft = 0.0f, modPhaseRight = juce::MathConstants<float>::pi;
    float basePreDelaySamples = 0.0f;
    float widthFactor = 1.0f;

    int maximumBlockSize = 0, channelCount = 0;
    double sampleRate = 44100.0;
};
