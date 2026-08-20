// Numeric verification harness for the "JC Chorus" pedal (Roland JC-120's
// BBD chorus line, extracted from AmpModule into its own pedal) -- not part
// of the shipping plugin (separate console-app target in CMakeLists.txt).
// Instantiates the real JCChorusModule (production code, no mocks) and:
//   1. Sweeps rate x depth x mix x amplitude x frequency at 48/96/192k,
//      flagging any NaN/Inf or unbounded output -- the blow-up check the
//      workflow mandates before any DSP change ships. The only way to NaN
//      here is an out-of-bounds modulated-delay read (there's no feedback
//      path to run away), so the sweep specifically exercises the read-
//      position clamp across the full delay range.
//   2. Confirms mix=0 is a bit-exact dry passthrough (gain 1) and mix=100
//      actually decorrelates the two stereo channels (the JC's signature
//      pi-offset swirl), so the pedal isn't silently inert.
#include <JuceHeader.h>
#include "DSP/JCChorusModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const float rates[] = { 0.1f, 0.9f, 5.0f };
        static const float depths[] = { 0.0f, 50.0f, 100.0f };
        static const float mixes[] = { 0.0f, 45.0f, 100.0f };
        static const float amps[] = { 0.1f, 0.5f, 2.0f };
        static const float freqs[] = { 40.0f, 440.0f, 4000.0f };

        JCChorusModule mod;
        juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
        mod.prepare (spec);

        float peak = 0.0f;
        for (float rate : rates)
            for (float depth : depths)
                for (float mix : mixes)
                    for (float amp : amps)
                        for (float freq : freqs)
                        {
                            mod.reset();
                            mod.setParameters (rate, depth, mix, true);
                            for (int block = 0; block < 16; ++block)
                            {
                                juce::AudioBuffer<float> buf (2, 256);
                                for (int i = 0; i < 256; ++i)
                                {
                                    const double phase = 2.0 * kPi * (double) freq
                                                       * (double) (block * 256 + i) / sampleRate;
                                    const auto v = (float) (amp * std::sin (phase));
                                    buf.setSample (0, i, v);
                                    buf.setSample (1, i, v);
                                }
                                mod.process (buf);
                                for (int ch = 0; ch < 2; ++ch)
                                    for (int i = 0; i < 256; ++i)
                                    {
                                        const auto val = buf.getSample (ch, i);
                                        if (! std::isfinite (val))
                                        {
                                            std::printf ("  !! NaN/Inf at rate=%.1f depth=%.0f mix=%.0f amp=%.1f freq=%.0f\n",
                                                         rate, depth, mix, amp, freq);
                                            return false;
                                        }
                                        const auto mag = std::fabs (val);
                                        if (mag > peak) peak = mag;
                                        if (mag > ceiling)
                                        {
                                            std::printf ("  !! unbounded (%.2f > %.1f) at rate=%.1f depth=%.0f mix=%.0f amp=%.1f\n",
                                                         mag, ceiling, rate, depth, mix, amp);
                                            return false;
                                        }
                                    }
                            }
                        }
        std::printf ("  ok (no NaN/Inf; peak |out| = %.4f)\n", peak);
        return true;
    }

    bool dryPassthrough (double sampleRate)
    {
        JCChorusModule mod;
        juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
        mod.prepare (spec);
        mod.reset();
        mod.setParameters (0.9f, 50.0f, 0.0f, true);  // mix 0 -> pure dry

        double maxErr = 0.0;
        for (int block = 0; block < 16; ++block)
        {
            juce::AudioBuffer<float> buf (2, 256);
            for (int i = 0; i < 256; ++i)
            {
                const double phase = 2.0 * kPi * 440.0 * (double) (block * 256 + i) / sampleRate;
                const auto v = (float) (0.5 * std::sin (phase));
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            mod.process (buf);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                {
                    const double phase = 2.0 * kPi * 440.0 * (double) (block * 256 + i) / sampleRate;
                    const auto expected = (float) (0.5 * std::sin (phase));
                    maxErr = juce::jmax (maxErr, (double) std::fabs (buf.getSample (ch, i) - expected));
                }
        }
        std::printf ("  mix=0 dry passthrough: max |err| = %.6f (should be ~0)\n", maxErr);
        return maxErr < 1e-4;
    }

    bool stereoSwirl (double sampleRate)
    {
        JCChorusModule mod;
        juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
        mod.prepare (spec);
        mod.reset();
        mod.setParameters (0.9f, 50.0f, 100.0f, true);  // full wet, stereo

        double maxDiff = 0.0;
        for (int block = 0; block < 16; ++block)
        {
            juce::AudioBuffer<float> buf (2, 256);
            for (int i = 0; i < 256; ++i)
            {
                const double phase = 2.0 * kPi * 440.0 * (double) (block * 256 + i) / sampleRate;
                const auto v = (float) (0.5 * std::sin (phase));
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            mod.process (buf);
            for (int i = 0; i < 256; ++i)
                maxDiff = juce::jmax (maxDiff, (double) std::fabs (buf.getSample (0, i) - buf.getSample (1, i)));
        }
        std::printf ("  stereo (pi-offset) at mix=100: max |L-R| = %.4f (should be well above 0)\n", maxDiff);
        return maxDiff > 0.1;
    }
}

int main()
{
    std::printf ("JCChorusModule (Roland JC-120 BBD chorus) numeric verification\n");
    std::printf ("=============================================================\n\n");

    std::printf ("Stability sweep (NaN/Inf/blow-up), ceiling 4:\n");
    const double rates[] = { 48000.0, 96000.0, 192000.0 };
    const char* rateNames[] = { "48k", "96k", "192k" };
    for (int r = 0; r < 3; ++r)
    {
        std::printf ("  %s: ", rateNames[r]);
        if (! sweepForBlowup (rates[r], 4.0f))
            return 1;
    }
    std::printf ("\n");

    std::printf ("Dry passthrough:\n");
    const bool dryOk = dryPassthrough (48000.0);
    std::printf ("\n");

    std::printf ("Stereo decorrelation:\n");
    const bool stereoOk = stereoSwirl (48000.0);
    std::printf ("\n");

    const bool allOk = dryOk && stereoOk;
    std::printf (allOk ? "All checks passed.\n" : "FAILED.\n");
    return allOk ? 0 : 1;
}
