#pragma once
#include <JuceHeader.h>

// Algorithmic hall/room reverb -- a parallel-comb + series-allpass tank
// (the classic Schroeder/Moorer "Freeverb" topology: 8 comb filters summed,
// then 4 series allpass filters for diffusion), tuned to emulate the Boss
// RV-200's Room vs Hall character rather than convolving against a captured
// space. The RV-200's Room and Hall algorithms are two genuinely distinct,
// size-selectable spaces (Room: smaller/denser, Hall: bigger/smoother) --
// modeled here as three pre-sized tanks (Large Hall, Large Stage as an
// in-between size, Small Room) rather than one continuously-sized model,
// matching this module's existing 3-space control surface.
//
// All three tanks are pre-allocated in prepare() so switching Model is just
// an index swap on the audio thread -- no reload, no async, no click (the
// newly-selected tank is reset first so no stale energy from a previous
// Model plays back).
//
// Tone maps to each comb's internal damping coefficient (a one-pole
// lowpass in the feedback path, so highs decay faster than lows in the
// tail -- real air-absorption behaviour, not a post-filter over the whole
// wet signal). Decay maps to comb feedback within a per-model range (bigger
// spaces get a higher natural ceiling). A tanh safety rail bounds the tank
// output regardless of parameter combination, since summed resonant combs
// can in principle build up gain at coincident frequencies.
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
        static const char* names[numModels] { "Large Hall", "Large Stage", "Small Room" };
        return names[juce::jlimit (0, numModels - 1, index)];
    }

    static constexpr int numCombs = 8;
    static constexpr int numAllpasses = 4;

private:
    struct Comb
    {
        std::vector<float> buffer;
        int writeIndex = 0;
        float feedback = 0.5f;
        float damp1 = 0.2f, damp2 = 0.8f;
        float filterStore = 0.0f;

        void setSize (int size)
        {
            buffer.assign (static_cast<size_t> (juce::jmax (4, size)), 0.0f);
            writeIndex = 0;
        }

        void reset()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            filterStore = 0.0f;
        }

        float process (float input)
        {
            const auto output = buffer[static_cast<size_t> (writeIndex)];
            filterStore = output * damp2 + filterStore * damp1;
            buffer[static_cast<size_t> (writeIndex)] = input + filterStore * feedback;
            if (++writeIndex >= static_cast<int> (buffer.size())) writeIndex = 0;
            return output;
        }
    };

    struct Allpass
    {
        std::vector<float> buffer;
        int writeIndex = 0;
        static constexpr float feedback = 0.5f;

        void setSize (int size)
        {
            buffer.assign (static_cast<size_t> (juce::jmax (4, size)), 0.0f);
            writeIndex = 0;
        }

        void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

        float process (float input)
        {
            const auto bufOut = buffer[static_cast<size_t> (writeIndex)];
            const auto output = -input + bufOut;
            buffer[static_cast<size_t> (writeIndex)] = input + bufOut * feedback;
            if (++writeIndex >= static_cast<int> (buffer.size())) writeIndex = 0;
            return output;
        }
    };

    struct Tank
    {
        Comb combsL[numCombs], combsR[numCombs];
        Allpass allpassL[numAllpasses], allpassR[numAllpasses];

        void reset()
        {
            for (auto& c : combsL) c.reset();
            for (auto& c : combsR) c.reset();
            for (auto& a : allpassL) a.reset();
            for (auto& a : allpassR) a.reset();
        }
    };

    void prepareTank (Tank& tank, int modelIndex);

    Tank tanks[numModels];
    int activeModel = 0;

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
