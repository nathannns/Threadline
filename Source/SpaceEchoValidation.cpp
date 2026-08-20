// Numeric verification harness for "Satellite - 201" (Roland RE-201-style
// 3-head tape echo) -- not part of the shipping plugin (separate console-app
// target in CMakeLists.txt).
// Instantiates the real SpaceEchoModule (production code, no mocks) and
// sweeps time x repeats x bass x treble x wobble x drive x pattern x
// amplitude x frequency at 48/96/192k, flagging any NaN/Inf or unbounded
// output. The feedback loop, hysteresis drive (Jiles-Atherton-style) and
// wobble-modulated read position are the risky parts, so the sweep runs 64
// blocks to reach steady state across several patterns.
#include <JuceHeader.h>
#include "DSP/SpaceEchoModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const float times[]    = { 100.0f, 500.0f };
        static const float repeats[]  = { 0.0f, 100.0f };
        static const float basses[]   = { 0.0f, 100.0f };
        static const float trebles[]  = { 0.0f, 100.0f };
        static const float wobbles[]  = { 0.0f, 100.0f };
        static const float drives[]   = { 0.0f, 100.0f };
        static const int   patterns[] = { 0, 5 };
        static const float amps[]     = { 0.1f, 0.5f, 2.0f };
        static const float freqs[]    = { 40.0f, 440.0f, 4000.0f };

        float peak = 0.0f;
        for (float time : times)
            for (float repeats01 : repeats)
                for (float bass : basses)
                    for (float treble : trebles)
                        for (float wobble : wobbles)
                            for (float drive : drives)
                                for (int pattern : patterns)
                                    for (float amp : amps)
                                        for (float freq : freqs)
                                        {
                                            SpaceEchoModule mod;
                                            juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
                                            mod.prepare (spec);
                                            mod.setParameters (time, repeats01, bass, treble,
                                                               wobble, drive, 100.0f, true, pattern);
                                            for (int block = 0; block < 64; ++block)
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
                                                            std::printf ("  !! NaN/Inf at time=%.0f rep=%.0f bass=%.0f treble=%.0f wobble=%.0f drive=%.0f pattern=%d amp=%.1f freq=%.0f\n",
                                                                         time, repeats01, bass, treble, wobble, drive, pattern, amp, freq);
                                                            return false;
                                                        }
                                                        const auto mag = std::fabs (val);
                                                        if (mag > peak) peak = mag;
                                                        if (mag > ceiling)
                                                        {
                                                            std::printf ("  !! unbounded (%.2f > %.1f) at rep=%.0f drive=%.0f pattern=%d amp=%.1f\n",
                                                                         mag, ceiling, repeats01, drive, pattern, amp);
                                                            return false;
                                                        }
                                                    }
                                            }
                                        }
        std::printf ("  ok (no NaN/Inf; peak |out| = %.4f)\n", peak);
        return true;
    }
}

int main()
{
    std::printf ("SpaceEchoModule (Satellite-201, RE-201-style) numeric verification\n");
    std::printf ("================================================================\n\n");

    std::printf ("Stability sweep (NaN/Inf/blow-up), ceiling 100:\n");
    const double rates[] = { 48000.0, 96000.0, 192000.0 };
    const char* rateNames[] = { "48k", "96k", "192k" };
    for (int r = 0; r < 3; ++r)
    {
        std::printf ("  %s: ", rateNames[r]);
        if (! sweepForBlowup (rates[r], 100.0f))
            return 1;
    }
    std::printf ("\nAll checks passed.\n");
    return 0;
}
