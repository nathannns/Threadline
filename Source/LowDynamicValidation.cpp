// Numeric verification harness for "Low Dynamic" (single-ratio expander/
// upward-compressor, MV2-inspired) -- not part of the shipping plugin
// (separate console-app target in CMakeLists.txt).
// Instantiates the real LowDynamicModule (production code, no mocks) and
// sweeps up x down x fast-mode x mix x amplitude x frequency at 48/96/192k,
// flagging any NaN/Inf or unbounded output. The floating-centre envelope
// follower can push large make-up gain (Up), so the sweep covers a near-
// silent input (amp 0.001) to exercise that regime.
#include <JuceHeader.h>
#include "DSP/LowDynamicModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const float ups[]    = { 0.0f, 50.0f, 100.0f };
        static const float downs[]  = { 0.0f, 50.0f, 100.0f };
        static const bool  fasts[]  = { false, true };
        static const float mixes[]  = { 50.0f, 100.0f };
        static const float amps[]   = { 0.001f, 0.1f, 0.5f, 2.0f };
        static const float freqs[]  = { 40.0f, 440.0f, 4000.0f };

        float peak = 0.0f;
        for (float up : ups)
            for (float down : downs)
                for (bool fast : fasts)
                    for (float mix : mixes)
                        for (float amp : amps)
                            for (float freq : freqs)
                            {
                                LowDynamicModule mod;
                                juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
                                mod.prepare (spec);
                                mod.setEnabled (true);
                                mod.setParameters (up, down, fast, mix);
                                for (int block = 0; block < 32; ++block)
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
                                                std::printf ("  !! NaN/Inf at up=%.0f down=%.0f fast=%d mix=%.0f amp=%.3f freq=%.0f\n",
                                                             up, down, (int) fast, mix, amp, freq);
                                                return false;
                                            }
                                            const auto mag = std::fabs (val);
                                            if (mag > peak) peak = mag;
                                            if (mag > ceiling)
                                            {
                                                std::printf ("  !! unbounded (%.2f > %.1f) at up=%.0f amp=%.3f\n",
                                                             mag, ceiling, up, amp);
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
    std::printf ("LowDynamicModule (floating-centre expander) numeric verification\n");
    std::printf ("===============================================================\n\n");

    std::printf ("Stability sweep (NaN/Inf/blow-up), ceiling 200:\n");
    const double rates[] = { 48000.0, 96000.0, 192000.0 };
    const char* rateNames[] = { "48k", "96k", "192k" };
    for (int r = 0; r < 3; ++r)
    {
        std::printf ("  %s: ", rateNames[r]);
        if (! sweepForBlowup (rates[r], 200.0f))
            return 1;
    }
    std::printf ("\nAll checks passed.\n");
    return 0;
}
