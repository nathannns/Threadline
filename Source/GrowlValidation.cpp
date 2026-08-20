// Numeric verification harness for "Growl" (germanium two-transistor fuzz,
// Fuzz Face archetype) -- not part of the shipping plugin (separate
// console-app target in CMakeLists.txt).
// Instantiates the real GrowlModule (production code, no mocks) and sweeps
// bias x fuzz x level x mix x amplitude x frequency at 48/96/192k,
// flagging any NaN/Inf or unbounded output. This module is the highest-risk
// of the three drives: Q1's base voltage is solved per-sample by a damped
// Newton-Raphson iteration whose undamped predecessor was found (by a prior
// harness sweep, now fixed) to diverge into a runaway state at several
// Bias/Fuzz combinations. The sweep deliberately drives Bias/Fuzz to both
// extremes to re-exercise that solve across the whole parameter grid.
#include <JuceHeader.h>
#include "DSP/GrowlModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const float biases[] = { 0.0f, 0.5f, 1.0f };
        static const float fuzzes[] = { 0.0f, 0.5f, 1.0f };
        static const float levels[] = { 0.5f, 1.0f };
        static const float mixes[]  = { 0.0f, 1.0f };
        static const float amps[]   = { 0.1f, 0.5f, 2.0f };
        static const float freqs[]  = { 40.0f, 440.0f, 4000.0f };

        float peak = 0.0f;
        for (float bias : biases)
            for (float fuzz : fuzzes)
                for (float level : levels)
                    for (float mix : mixes)
                        for (float amp : amps)
                            for (float freq : freqs)
                            {
                                GrowlModule mod;
                                juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
                                mod.prepare (spec, 1); // 2x oversampling
                                mod.setEnabled (true);
                                mod.setParameters (bias, fuzz, level, mix);
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
                                                std::printf ("  !! NaN/Inf at bias=%.1f fuzz=%.1f level=%.1f mix=%.1f amp=%.1f freq=%.0f\n",
                                                             bias, fuzz, level, mix, amp, freq);
                                                return false;
                                            }
                                            const auto mag = std::fabs (val);
                                            if (mag > peak) peak = mag;
                                            if (mag > ceiling)
                                            {
                                                std::printf ("  !! unbounded (%.2f > %.1f) at bias=%.1f fuzz=%.1f amp=%.1f\n",
                                                             mag, ceiling, bias, fuzz, amp);
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
    std::printf ("GrowlModule (germanium two-transistor fuzz) numeric verification\n");
    std::printf ("===============================================================\n\n");

    std::printf ("Stability sweep (NaN/Inf/blow-up), ceiling 20:\n");
    const double rates[] = { 48000.0, 96000.0, 192000.0 };
    const char* rateNames[] = { "48k", "96k", "192k" };
    for (int r = 0; r < 3; ++r)
    {
        std::printf ("  %s: ", rateNames[r]);
        if (! sweepForBlowup (rates[r], 20.0f))
            return 1;
    }
    std::printf ("\nAll checks passed.\n");
    return 0;
}
