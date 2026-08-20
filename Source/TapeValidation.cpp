// Numeric verification harness for "Tape" (tape saturation/compression,
// ported from Rockalizer) -- not part of the shipping plugin (separate
// console-app target in CMakeLists.txt).
// Instantiates the real TapeModule (production code, no mocks) and sweeps
// drive x compression x tone x age x tape-type x amplitude x frequency at
// 48/96/192k, flagging any NaN/Inf or unbounded output. The only genuine
// blow-up risk is the hysteretic saturator's direction-state and the
// wow/flutter modulated read position, so the sweep drives the record-gain
// curve hard and runs both Studio and Cassette voices.
#include <JuceHeader.h>
#include "DSP/TapeModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const float drives[] = { 0.0f, 0.5f, 1.0f };
        static const float comps[]  = { 0.0f, 1.0f };
        static const float tones[]  = { 0.0f, 0.5f, 1.0f };
        static const float ages[]   = { 0.0f, 1.0f };
        static const int   types[]  = { 0, 1 }; // studio, cassette
        static const float amps[]   = { 0.1f, 0.5f, 2.0f };
        static const float freqs[]  = { 40.0f, 440.0f, 4000.0f };

        float peak = 0.0f;
        for (float drive : drives)
            for (float comp : comps)
                for (float tone : tones)
                    for (float age : ages)
                        for (int type : types)
                            for (float amp : amps)
                                for (float freq : freqs)
                                {
                                    TapeModule mod;
                                    juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
                                    mod.prepare (spec);
                                    mod.reset();
                                    mod.setParameters (drive, comp, tone, age, 1.0f, 1.0f, true, type, 2); // 4x OS, volume unity
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
                                                    std::printf ("  !! NaN/Inf at drive=%.1f comp=%.1f tone=%.1f age=%.1f type=%d amp=%.1f freq=%.0f\n",
                                                                 drive, comp, tone, age, type, amp, freq);
                                                    return false;
                                                }
                                                const auto mag = std::fabs (val);
                                                if (mag > peak) peak = mag;
                                                if (mag > ceiling)
                                                {
                                                    std::printf ("  !! unbounded (%.2f > %.1f) at drive=%.1f amp=%.1f\n",
                                                                 mag, ceiling, drive, amp);
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
    std::printf ("TapeModule (tape saturation/compression) numeric verification\n");
    std::printf ("============================================================\n\n");

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
