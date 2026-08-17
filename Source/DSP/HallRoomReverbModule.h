#pragma once
#include <JuceHeader.h>

// Algorithmic hall/room reverb -- a parallel-comb + series-allpass tank
// (the classic Schroeder/Moorer "Freeverb" topology: 8 comb filters summed,
// then 4 series allpass filters for diffusion), tuned to emulate the Boss
// RV-6's Room / Hall / Plate / Shimmer modes rather than convolving against
// a captured space. The RV-6 (simpler than the RV-200 -- 3 knobs plus an
// 8-position mode switch, no deep menu-diving) describes these as: Room =
// warm, Hall = clear and spacious, Plate = metallic with extended highs,
// Shimmer = pitch-shifted "fantasy-like" overtones. Modeled here as four
// pre-sized/pre-tuned tanks rather than one continuously-sized model,
// matching this module's existing control surface.
//
// All four tanks are pre-allocated in prepare() so switching Model is just
// an index swap on the audio thread -- no reload, no async, no click (the
// newly-selected tank is reset first so no stale energy from a previous
// Model plays back). Plate gets a brighter fixed damping bias and higher
// allpass diffusion than Room/Hall/Shimmer, matching its "extended
// high-frequency range" character -- a fixed trait of the algorithm itself,
// not something the Tone knob should have to dial in by hand.
//
// Shimmer wraps the tank in an extra external feedback loop: each sample,
// the tank's own (already-diffused) previous output is pitch-shifted up an
// octave by a small two-tap granular shifter and fed back into the tank's
// input, so the tail cascades upward in pitch on top of the normal
// reverberant decay -- the same idea real shimmer reverbs use. Tone maps to
// each comb's internal damping coefficient (a one-pole lowpass in the
// feedback path, so highs decay faster than lows in the tail -- real
// air-absorption behaviour, not a post-filter). Decay maps to comb feedback
// within a per-model range. A tanh safety rail bounds the tank's output
// regardless of parameter combination, since summed resonant combs (and the
// shimmer feedback loop layered on top) can in principle build up gain at
// coincident frequencies.
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

    static constexpr int numModels = 4;
    static const char* getModelName (int index)
    {
        static const char* names[numModels] { "Room", "Hall", "Plate", "Shimmer" };
        return names[juce::jlimit (0, numModels - 1, index)];
    }

    static constexpr int numCombs = 8;
    static constexpr int numAllpasses = 4;
    static constexpr int shimmerModel = 3;

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
        float feedback = 0.5f; // per-model: higher for Plate's denser diffusion

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

    // A small two-tap granular pitch shifter, fixed an octave up -- used
    // only by the Shimmer model's feedback loop. Two read taps a half-grain
    // apart, each sped up 2x and equal-power (sin^2/cos^2) crossfaded, so
    // one tap's periodic reset (a jump back in the buffer) is masked by the
    // other tap being near its window's peak.
    struct PitchShifter
    {
        std::vector<float> buffer;
        int writeIndex = 0;
        float delayA = 0.0f, delayB = 0.0f;
        float grainSamples = 3000.0f;
        static constexpr float pitchRatio = 2.0f;

        void prepare (int bufferSize, float grainSamplesIn)
        {
            buffer.assign (static_cast<size_t> (juce::jmax (8, bufferSize)), 0.0f);
            grainSamples = juce::jmax (64.0f, grainSamplesIn);
            reset();
        }

        void reset()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writeIndex = 0;
            delayA = 0.0f;
            delayB = grainSamples * 0.5f;
        }

        static float readInterpolated (const std::vector<float>& buf, int writeIdx, float delay)
        {
            const auto size = static_cast<float> (buf.size());
            auto position = static_cast<float> (writeIdx) - delay;
            while (position < 0.0f) position += size;
            while (position >= size) position -= size;
            const auto index0 = static_cast<int> (position);
            const auto index1 = (index0 + 1) % static_cast<int> (size);
            const auto fraction = position - static_cast<float> (index0);
            return buf[static_cast<size_t> (index0)]
                 + (buf[static_cast<size_t> (index1)] - buf[static_cast<size_t> (index0)]) * fraction;
        }

        float process (float input)
        {
            buffer[static_cast<size_t> (writeIndex)] = input;

            delayA -= (pitchRatio - 1.0f);
            delayB -= (pitchRatio - 1.0f);
            if (delayA <= 0.0f) delayA += grainSamples;
            if (delayB <= 0.0f) delayB += grainSamples;

            const auto readA = readInterpolated (buffer, writeIndex, delayA);
            const auto readB = readInterpolated (buffer, writeIndex, delayB);
            const auto windowA = std::sin (juce::MathConstants<float>::pi * (delayA / grainSamples));
            const auto windowB = std::sin (juce::MathConstants<float>::pi * (delayB / grainSamples));
            const auto output = readA * windowA * windowA + readB * windowB * windowB;

            if (++writeIndex >= static_cast<int> (buffer.size())) writeIndex = 0;
            return output;
        }
    };

    void prepareTank (Tank& tank, int modelIndex);

    Tank tanks[numModels];
    int activeModel = 0;

    PitchShifter shimmerShifterL, shimmerShifterR;
    float previousWetL = 0.0f, previousWetR = 0.0f;

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
