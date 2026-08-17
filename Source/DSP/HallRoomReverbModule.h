#pragma once
#include <JuceHeader.h>

// Algorithmic Room/Hall/Plate reverb -- an 8-line Feedback Delay Network
// (FDN) with a Householder mixing matrix for the late, diffuse tail, plus a
// separate per-channel multi-tap early-reflection (ER) generator for the
// direct, discrete-echo onset that actually gives a space its sense of size
// and distance -- the previous version had no ER stage at all, just 8
// mutually-independent comb filters summed at the output (classic 1996
// Freeverb topology), which is why it read as thin/metallic rather than
// spacious: independent combs don't exchange energy with each other, so
// the diffuse buildup relies entirely on a following allpass chain, which
// is comparatively weak. A Householder matrix mixes every line's output
// into every other line's input each sample (all entries nonzero), which
// is specifically what maximizes echo density in FDN reverb design -- this
// is the standard architecture behind essentially every serious
// algorithmic reverb since Stautner & Puckette (1982), not just an
// incremental tweak.
//
// Three spaces -- Room (small/warm), Hall (larger/spacious), Plate
// (metallic, extended highs) -- modeled on the Boss RV-6's mode
// descriptions, as pre-allocated/pre-tuned tanks so switching Model is a
// same-thread index swap with no reload or click. Plate is deliberately
// tuned "bigger" than Hall: a longer FDN line-length scale, a higher decay
// ceiling, and an early-reflection pattern that's nearly instantaneous and
// extremely dense (no audible gap before the wash arrives) rather than
// Hall's sparser, more spread-out taps -- a real plate's whole surface
// resonates almost simultaneously, which reads as bigger/more enveloping
// than a room-shaped space even though the physical device is smaller.
//
// Tone maps to each line's internal damping coefficient (a one-pole
// lowpass in the feedback path, so highs decay faster than lows in the
// tail -- real air-absorption behaviour). Decay maps to the FDN's loop
// gain within a per-space ceiling. A tanh safety rail bounds the final
// output regardless of parameter combination; early-reflection gains are
// kept comfortably below its knee in normal use so the clamp stays a true
// safety net rather than something that flattens the intended level
// differences between spaces.
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

    static constexpr int numLines = 8;
    static constexpr int maxTaps = 10;

private:
    // One FDN delay line. readDamped() and writeBack() are split (rather
    // than one combined process() call, as the old independent-comb design
    // used) because the Householder mix needs every line's damped output
    // before any line can be written back to.
    struct Line
    {
        std::vector<float> buffer;
        int writeIndex = 0;
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

        float readDamped()
        {
            const auto raw = buffer[static_cast<size_t> (writeIndex)];
            filterStore = raw * damp2 + filterStore * damp1;
            return filterStore;
        }

        void writeBack (float value)
        {
            buffer[static_cast<size_t> (writeIndex)] = value;
            if (++writeIndex >= static_cast<int> (buffer.size())) writeIndex = 0;
        }
    };

    // A fixed-tap delay line simulating the discrete early echoes off
    // nearby boundaries -- the primary perceptual cue for a space's size
    // and distance, independent of the late diffuse tail.
    struct EarlyReflections
    {
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
        Line lines[numLines];
        EarlyReflections erL, erR;

        void reset()
        {
            for (auto& l : lines) l.reset();
            erL.reset();
            erR.reset();
        }
    };

    void prepareTank (Tank& tank, int modelIndex);

    Tank tanks[numModels];
    int activeModel = 0;
    float loopFeedback = 0.5f;

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
