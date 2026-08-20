// Numeric verification harness for "Copier" (MXR Carbon Copy-style BBD
// analog delay) -- not part of the shipping plugin (separate console-app
// target in CMakeLists.txt).
// Instantiates the real CarbonCopyModule (production code, no mocks) and
// sweeps time x regen x mix x mod x amplitude x frequency at 48/96/192k,
// flagging any NaN/Inf or unbounded output. Regen reaches near-self-
// oscillation at maximum, bounded by an ADAA write rail inside the feedback
// path, so the sweep runs 64 blocks to let that loop reach steady state.
#include <JuceHeader.h>
#include "DSP/CarbonCopyModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const float times[]  = { 50.0f, 300.0f, 600.0f };
        static const float regens[] = { 0.0f, 50.0f, 100.0f };
        static const float mixes[]  = { 0.0f, 100.0f };
        static const bool  mods[]   = { false, true };
        static const float amps[]   = { 0.1f, 0.5f, 2.0f };
        static const float freqs[]  = { 40.0f, 440.0f, 4000.0f };

        float peak = 0.0f;
        for (float time : times)
            for (float regen : regens)
                for (float mix : mixes)
                    for (bool modOn : mods)
                        for (float amp : amps)
                            for (float freq : freqs)
                            {
                                CarbonCopyModule mod;
                                juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
                                mod.prepare (spec);
                                mod.setParameters (time, regen, mix, modOn, true);
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
                                                std::printf ("  !! NaN/Inf at time=%.0f regen=%.0f mix=%.0f mod=%d amp=%.1f freq=%.0f\n",
                                                             time, regen, mix, (int) modOn, amp, freq);
                                                return false;
                                            }
                                            const auto mag = std::fabs (val);
                                            if (mag > peak) peak = mag;
                                            if (mag > ceiling)
                                            {
                                                std::printf ("  !! unbounded (%.2f > %.1f) at regen=%.0f amp=%.1f\n",
                                                             mag, ceiling, regen, amp);
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
    std::printf ("CarbonCopyModule (Copier, BBD analog delay) numeric verification\n");
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
