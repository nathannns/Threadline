#pragma once
#include <JuceHeader.h>

// Algorithmic Room/Hall/Plate reverb -- a genuine Feedback Delay Network
// (FDN), not a Freeverb-style bank of independent parallel combs (which is
// what every earlier version of this module was, even after RT60-deriving
// their feedback/damping). The distinction matters structurally, not just
// in tuning: in an independent-comb design, each of the 8 lines only ever
// interacts with itself -- its own delayed output is the only thing that
// feeds back into it, so the 8 "voices" are really just summed at the very
// end, not genuinely diffuse. A real FDN mixes every line's output into
// EVERY line's input each sample via an orthogonal matrix (Householder:
// H = I - (2/N)*ones(N,N)), so the network only has ONE tail, made of all
// 8 lines' energy continuously redistributing between each other -- this
// is what actually produces a dense, smooth, "one coherent space" decay
// instead of 8 audibly-separate resonators that happen to overlap.
//
// Two structural pieces this module never had before, both standard in
// real FDN/plate designs (Dattorro's topology among them) and both
// genuinely absent from a Freeverb-style bank:
//
// 1. Input diffusion. Before the input ever reaches the feedback network,
//    it passes through 4 series allpass filters (Dattorro's own published
//    lengths and coefficients -- 142/107/379/277 samples at 0.75/0.75/
//    0.625/0.625 diffusion, from the original 1997 paper) that smear a
//    sharp transient into a dense cluster immediately. Freeverb has no
//    equivalent stage at all -- input goes straight into the combs.
//
// 2. A shared, orthogonally-mixed 8-line core instead of 8 independent
//    per-channel combs (16 total). One mono-fed, mono-injected network
//    (matching a real Freeverb/FDN's own convention -- "a single
//    mono-summed input, not per-channel") generates BOTH channels by
//    extracting two different orthogonal (Hadamard-row) weighted sums of
//    the same 8 lines, rather than running two separate parallel networks
//    -- half the state, and the genuinely-coupled-diffusion character
//    above requires a shared network to mean anything (two independent
//    per-channel FDNs would just be two independent decays again).
//
// RT60 targeting: for a network where every line's output feeds back
// through EVERY line (not just itself), the individual per-line Schroeder
// derivation this module used for its old independent-comb design doesn't
// have a simple closed form any more -- the matrix genuinely couples lines
// of different lengths together each sample. What DOES have an exact,
// well-known closed form: for a uniform loop gain g (same scalar on every
// line) combined with an ORTHOGONAL mixing matrix, the whole network's
// total energy decays at exactly rate g per round trip regardless of the
// specific mixing pattern, since an orthogonal transform preserves vector
// norm (‖H*x‖ = ‖x‖ for any x). That's the standard, textbook way real FDN
// reverbs control T60 (Jot's papers, and most production implementations),
// used here with each model's MEAN line length as the network's
// characteristic round-trip time: g = 10^(-3*meanLength / (RT60*fs)) --
// the same Schroeder formula as before, now applied at the level the
// coupled network actually operates at, rather than per independent line.
// Frequency-dependent damping (Tone) uses the same two-target derivation
// (d = (1-r)/(1+r), r = gainHigh/gainLow) at that same characteristic
// length.
//
// 3 pre-sized/pre-tuned tanks for Room/Hall/Plate, so switching Model is a
// same-thread index swap with no reload or click. Plate is deliberately
// tuned bigger than Hall (longer line-length scale, a longer RT60 ceiling)
// -- a real plate's whole surface resonates almost simultaneously, which
// reads as more enveloping than a room-shaped space despite the smaller
// physical device.
//
// Two more stages, unchanged in concept from the previous version and
// still additive rather than touching the FDN math above:
//
// 1. Early reflections. The FDN core alone is a *late diffuse field*
//    generator -- it has no notion of discrete first-few echoes off nearby
//    boundaries, which is the primary perceptual cue for a space's size
//    and distance. A per-channel multi-tap delay runs in parallel with the
//    tank, with a per-model tap pattern -- Room: modest density, moderate
//    spread; Hall: sparser and more spread out, with a clearer gap before
//    it thickens; Plate: near-instantaneous and very dense, almost no gap.
//
// 2. Modulated diffusion. Applied to the FDN's two extracted stereo taps
//    (not inside the core network, so it can't perturb the RT60 math
//    above): 4 allpasses per channel, each reading its delay line at a
//    slowly, independently modulated fractional position instead of a
//    fixed integer lookback, so the tail stays smooth under sustained
//    input instead of ringing at a fixed set of resonances.
//
// A tanh safety rail is kept as a backstop since an orthogonally-mixed
// network can in principle still build gain at coincident frequencies for
// pathological parameter combinations, but with g < 1 guaranteed by
// construction it should essentially never actually engage.
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
    static constexpr int numInputDiffusers = 4;
    static constexpr int numAllpasses = 4;

private:
    // One FDN delay line. Damping (unity-DC-gain one-pole, same form as
    // before) lives inside the feedback path; feedback/damp1/damp2 are now
    // shared across all 8 lines per tank (see updateDecayTimes()) rather
    // than derived per-line -- see file header for why that's the correct
    // level to apply the RT60 math at for a genuinely coupled network.
    struct Line
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

        // Reads and damps this sample's output; does NOT write back or
        // advance the index -- the Householder mix needs every line's
        // damped output before any line can be written, so writeBack() is
        // a separate call made after the mix is computed.
        float readDamped (float dampLow, float dampHigh)
        {
            const auto output = buffer[static_cast<size_t> (index)];
            last = (output * dampHigh) + (last * dampLow);
            return last;
        }

        void writeBack (float value)
        {
            buffer[static_cast<size_t> (index)] = value;
            if (++index >= static_cast<int> (buffer.size())) index = 0;
        }
    };

    // Dattorro's own input-diffuser allpass (JAES 1997): a direct
    // realisation of y[n] = g*(x[n]-y[n-d])+x[n-d] using two parallel
    // delay buffers (input history, output history) rather than the
    // single-buffer Schroeder trick, so the published coefficients apply
    // exactly rather than to an equivalent-but-different realisation.
    struct InputDiffuser
    {
        std::vector<float> xBuf, yBuf;
        int index = 0;
        float diffusion = 0.7f;

        void setSize (int size)
        {
            xBuf.assign (static_cast<size_t> (juce::jmax (4, size)), 0.0f);
            yBuf.assign (static_cast<size_t> (juce::jmax (4, size)), 0.0f);
            index = 0;
        }

        void reset()
        {
            std::fill (xBuf.begin(), xBuf.end(), 0.0f);
            std::fill (yBuf.begin(), yBuf.end(), 0.0f);
        }

        float process (float x)
        {
            const auto xd = xBuf[static_cast<size_t> (index)];
            const auto yd = yBuf[static_cast<size_t> (index)];
            const auto y = diffusion * (x - yd) + xd;
            xBuf[static_cast<size_t> (index)] = x;
            yBuf[static_cast<size_t> (index)] = y;
            if (++index >= static_cast<int> (xBuf.size())) index = 0;
            return y;
        }
    };

    // Post-FDN diffusion allpass, reading its delay line at a slowly,
    // independently modulated fractional position -- see file header
    // point 2. Applied to the two extracted stereo taps, not inside the
    // FDN core, so it can't perturb the RT60 math.
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
    // nearby boundaries -- see file header point 1 (of the second group).
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
        InputDiffuser inputDiffuser[numInputDiffusers];
        Line lines[numLines];
        Allpass allpassL[numAllpasses], allpassR[numAllpasses];
        EarlyReflections erL, erR;

        void reset()
        {
            for (auto& d : inputDiffuser) d.reset();
            for (auto& l : lines) l.reset();
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
    // Decay knob's target: the low-frequency (DC) RT60 in seconds, applied
    // at the network's characteristic (mean) line length -- see file
    // header. Tone knob's target: the high-frequency RT60 as a fraction of
    // that (always <= rt60Low -- highs never outlast lows).
    float rt60Low = 1.0f;
    float rt60High = 0.5f;
    float loopFeedback = 0.7f;
    float damp1 = 0.2f, damp2 = 0.8f;

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
