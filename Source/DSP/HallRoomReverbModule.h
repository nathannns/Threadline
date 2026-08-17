#pragma once
#include <JuceHeader.h>

// Algorithmic Room/Hall/Plate reverb, built on the exact Freeverb structure
// and gain-staging used by JUCE's own juce::Reverb class (itself what
// HISE's shipped SimpleReverb effect wraps rather than rolling its own):
// 0.015 input gain into the comb bank, allpass feedback of 0.5, damping
// inside each comb's own feedback path rather than a separate post-filter.
//
// On top of that proven gain-staging, feedback and damping are now derived
// from an explicit RT60 (reverberation time) model instead of a single
// shared coefficient applied uniformly to all 8 differently-sized comb
// lines. That uniform-coefficient approach (what both this module's first
// version and JUCE's own stock Reverb actually do) is mathematically
// inconsistent: a comb's decay rate in real time depends on both its
// feedback gain AND how often it loops (loop period = line length /
// sample rate), so the same feedback value gives DIFFERENT lines
// DIFFERENT real-world decay times -- shorter lines loop more often per
// second and so decay faster than longer ones at an identical gain. The
// fix is the standard Schroeder result: for a comb of length M samples to
// reach -60dB after RT60 seconds, its gain must satisfy
// g^(RT60*fs/M) = 10^-3, i.e. g = 10^(-3M / (RT60*fs)). Computing each
// comb's own g from its own M, from a single shared RT60 target, makes
// every line agree on when the tail is "gone" regardless of its length --
// a physically coherent decay instead of 8 independently-decaying combs
// that happen to overlap.
//
// The same idea extends to frequency-dependent damping: the comb's
// unity-DC-gain one-pole damping filter (1-d)/(1-d*z^-1) leaves the DC/
// low-frequency decay rate set purely by g, but attenuates the loop gain
// at Nyquist by a factor (1-d)/(1+d). Given a desired SHORTER RT60 at high
// frequency (real spaces and real damping absorb highs faster than lows),
// solving g*(1-d)/(1+d) = g_highFreq for d gives d = (1-r)/(1+r), where
// r = g_highFreq/g and g_highFreq is the same Schroeder formula evaluated
// at the high-frequency target instead of the low-frequency one. Tone now
// drives that high-frequency RT60 as a fraction of the low-frequency one
// (Decay), rather than an arbitrary damping-coefficient range, and both
// are computed per comb from its own length -- see prepareTank() and
// updateDecayTimes().
//
// 8 comb filters run in parallel per channel (accumulated), followed by 4
// allpass filters in series per channel -- exactly JUCE's topology, just
// duplicated into 3 pre-sized/pre-tuned tanks for Room/Hall/Plate rather
// than JUCE's single continuously-sized model, so switching Model is a
// same-thread index swap with no reload or click. Plate is deliberately
// tuned bigger than Hall (longer comb/allpass line-length scale, a longer
// RT60 ceiling, and denser diffusion via a higher allpass coefficient) --
// a real plate's whole surface resonates almost simultaneously, which
// reads as more enveloping than a room-shaped space despite the smaller
// physical device. A tanh safety rail is kept as a backstop since summed
// combs can in principle still build gain at coincident frequencies, but
// with gain values now derived to guarantee decay (all g < 1 by
// construction) rather than picked heuristically, it should essentially
// never actually engage.
//
// Two further stages, both additive rather than touching the comb/allpass
// math above (that's already RT60-verified; the goal here is to not
// disturb it):
//
// 1. Early reflections. The comb/allpass tank alone is a *late diffuse
//    field* generator -- it has no notion of discrete first-few echoes off
//    nearby boundaries, which is the primary perceptual cue for a space's
//    size and distance (a tank-only reverb, however well-tuned, reads as
//    "generically diffuse" rather than "a specific-sized room"). A
//    per-channel multi-tap delay runs in parallel with the tank, with a
//    per-model tap pattern -- Room: modest density, moderate spread; Hall:
//    sparser and more spread out, with a clearer gap before it thickens,
//    reading as a bigger physical distance to the walls; Plate: near-
//    instantaneous and very dense, almost no gap, the way a real plate's
//    whole surface resonates nearly simultaneously.
//
// 2. Modulated allpass diffusion. A static (unmodulated) delay-based
//    diffuser can ring at its own resonant frequencies under sustained
//    input, reading as a faint metallic/comby quality under the main
//    decay -- audible mainly on long, held notes. Each of the 4 allpasses
//    now reads its delay line at a slowly, independently modulated
//    fractional position (a few tenths of a millisecond of wobble,
//    different rate/phase per instance) instead of a fixed integer
//    lookback, the standard technique real diffuse-reverb designs
//    (Dattorro's plate topology among them) use to keep the tail smooth
//    rather than static. This only touches diffusion character, not decay
//    time, so it doesn't interact with the RT60 math above.
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
    // Exactly JUCE's CombFilter structurally (the damping one-pole lives
    // inside the feedback path itself, `last`, not a separate post-filter
    // stage) but feedback/damp1/damp2 are now this comb's OWN RT60-derived
    // values (see setDecayTimes()) rather than shared scalars passed in
    // from outside -- each of the 8 lines has a different length, so each
    // needs a different gain to agree on the same real-world decay time.
    struct Comb
    {
        std::vector<float> buffer;
        int index = 0;
        float last = 0.0f;
        float feedback = 0.7f;
        float damp1 = 0.2f, damp2 = 0.8f;

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

        // Schroeder's comb decay-time result: for this line's own length
        // (buffer.size() samples) to reach -60dB after rt60Low seconds,
        // gain must be 10^(-3*length/(rt60Low*sampleRate)) -- see file
        // header. rt60High (<= rt60Low) sets the same result evaluated at
        // the high-frequency target, from which the damping coefficient
        // that reconciles the two is solved algebraically.
        void setDecayTimes (float targetRt60Low, float targetRt60High, double currentSampleRate)
        {
            const auto length = static_cast<float> (buffer.size());
            const auto exponent = -3.0f * length / static_cast<float> (currentSampleRate);
            const auto gainLow = std::pow (10.0f, exponent / juce::jmax (0.02f, targetRt60Low));
            const auto gainHigh = std::pow (10.0f, exponent / juce::jmax (0.02f, targetRt60High));
            feedback = juce::jlimit (0.0f, 0.999f, gainLow);
            const auto ratio = juce::jlimit (0.0001f, 1.0f, gainHigh / juce::jmax (0.0001f, gainLow));
            damp1 = juce::jlimit (0.0f, 0.999f, (1.0f - ratio) / (1.0f + ratio));
            damp2 = 1.0f - damp1;
        }

        float process (float input)
        {
            const auto output = buffer[static_cast<size_t> (index)];
            last = (output * damp2) + (last * damp1);
            const auto temp = input + (last * feedback);
            buffer[static_cast<size_t> (index)] = temp;
            if (++index >= static_cast<int> (buffer.size())) index = 0;
            return output;
        }
    };

    // JUCE's AllPassFilter (fixed feedback per model rather than a single
    // hardcoded 0.5 everywhere -- Plate runs it higher for denser
    // diffusion, see file header), extended to read its delay line at a
    // slowly modulated fractional position instead of a fixed integer
    // lookback -- see file header point 2. buffer is sized with margin
    // for modDepthSamples on top of the nominal baseDelay.
    struct Allpass
    {
        std::vector<float> buffer;
        int writeIndex = 0;
        int baseDelay = 0;
        float feedback = 0.5f;
        float modDepthSamples = 0.0f;
        float modIncrement = 0.0f;
        float modPhase = 0.0f;

        void setSize (int size)
        {
            baseDelay = juce::jmax (1, size);
            const auto margin = juce::roundToInt (modDepthSamples) + 4;
            buffer.assign (static_cast<size_t> (baseDelay + margin), 0.0f);
            writeIndex = 0;
        }

        void reset()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            // Phase is deliberately not reset here -- prepareTank() staggers
            // each instance's starting phase once, and re-zeroing it on
            // every enable/model-switch would resynchronise all 8 lines to
            // the same modulation phase, defeating the point.
        }

        float process (float input)
        {
            const auto size = static_cast<int> (buffer.size());
            const auto distance = static_cast<float> (baseDelay) + modDepthSamples * std::sin (modPhase);
            auto position = static_cast<float> (writeIndex) - distance;
            while (position < 0.0f) position += static_cast<float> (size);
            while (position >= static_cast<float> (size)) position -= static_cast<float> (size);
            const auto index0 = static_cast<int> (position);
            const auto index1 = (index0 + 1) % size;
            const auto fraction = position - static_cast<float> (index0);
            const auto bufferedValue = buffer[static_cast<size_t> (index0)]
                + (buffer[static_cast<size_t> (index1)] - buffer[static_cast<size_t> (index0)]) * fraction;

            const auto temp = input + (bufferedValue * feedback);
            buffer[static_cast<size_t> (writeIndex)] = temp;
            if (++writeIndex >= size) writeIndex = 0;

            modPhase += modIncrement;
            if (modPhase >= juce::MathConstants<float>::twoPi)
                modPhase -= juce::MathConstants<float>::twoPi;

            return bufferedValue - input;
        }
    };

    // A fixed-tap delay line simulating the discrete early echoes off
    // nearby boundaries -- see file header point 1.
    struct EarlyReflections
    {
        static constexpr int maxTaps = 10;

        std::vector<float> buffer;
        int writeIndex = 0;
        int numActiveTaps = 0;
        int tapDelaySamples[maxTaps] {};
        float tapGain[maxTaps] {};

        void setBufferSize (int size)
        {
            buffer.assign (static_cast<size_t> (juce::jmax (8, size)), 0.0f);
            writeIndex = 0;
        }

        void reset()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writeIndex = 0;
        }

        float process (float input)
        {
            buffer[static_cast<size_t> (writeIndex)] = input;
            const auto size = static_cast<int> (buffer.size());
            float sum = 0.0f;
            for (int t = 0; t < numActiveTaps; ++t)
            {
                auto readPos = writeIndex - tapDelaySamples[t];
                while (readPos < 0) readPos += size;
                sum += buffer[static_cast<size_t> (readPos)] * tapGain[t];
            }
            if (++writeIndex >= size) writeIndex = 0;
            return sum;
        }
    };

    struct Tank
    {
        Comb combL[numCombs], combR[numCombs];
        Allpass allpassL[numAllpasses], allpassR[numAllpasses];
        EarlyReflections erL, erR;

        void reset()
        {
            for (auto& c : combL) c.reset();
            for (auto& c : combR) c.reset();
            for (auto& a : allpassL) a.reset();
            for (auto& a : allpassR) a.reset();
            erL.reset();
            erR.reset();
        }
    };

    void prepareTank (Tank& tank, int modelIndex);
    void updateDecayTimes();

    Tank tanks[numModels];
    int activeModel = 0;
    // Decay knob's target: the low-frequency (DC) RT60 in seconds. Tone
    // knob's target: the high-frequency RT60 as a fraction of that (always
    // <= rt60Low -- highs never outlast lows). See setDecayTimes() in Comb.
    float rt60Low = 1.0f;
    float rt60High = 0.5f;

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
