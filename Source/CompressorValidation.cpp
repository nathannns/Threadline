// Numeric verification harness for "Compressor" (Diamond-inspired optical/
// vactrol compressor) -- not part of the shipping plugin (separate console-
// app target in CMakeLists.txt).
// Instantiates the real CompressorModule (production code, no mocks) and
// sweeps compression x attack x tilt x mid x output-level x amplitude x
// frequency at 48/96/192k, flagging any NaN/Inf or unbounded output. The
// dual-time-constant release follower and soft-knee gain computer are all
// bounded by construction, so this mostly guards the dB<->linear round-trip
// and the envelope followers against a stray log(0) or runaway coefficient.
#include <JuceHeader.h>
#include "DSP/CompressorModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const float comps[]  = { 0.0f, 50.0f, 100.0f };
        static const float attacks[] = { 0.0f, 100.0f };
        static const float tilts[]  = { -100.0f, 0.0f, 100.0f };
        static const float mids[]   = { -12.0f, 12.0f };
        static const float levels[] = { -12.0f, 12.0f };
        static const float amps[]   = { 0.1f, 0.5f, 2.0f };
        static const float freqs[]  = { 40.0f, 1000.0f, 4000.0f };

        float peak = 0.0f;
        for (float comp : comps)
            for (float attack : attacks)
                for (float tilt : tilts)
                    for (float mid : mids)
                        for (float level : levels)
                            for (float amp : amps)
                                for (float freq : freqs)
                                {
                                    CompressorModule mod;
                                    juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
                                    mod.prepare (spec);
                                    mod.setEnabled (true);
                                    mod.setParameters (comp, attack, tilt, mid, level);
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
                                                    std::printf ("  !! NaN/Inf at comp=%.0f attack=%.0f tilt=%.0f mid=%.0f level=%.0f amp=%.1f freq=%.0f\n",
                                                                 comp, attack, tilt, mid, level, amp, freq);
                                                    return false;
                                                }
                                                const auto mag = std::fabs (val);
                                                if (mag > peak) peak = mag;
                                                if (mag > ceiling)
                                                {
                                                    std::printf ("  !! unbounded (%.2f > %.1f) at comp=%.0f level=%.0f amp=%.1f\n",
                                                                 mag, ceiling, comp, level, amp);
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
    std::printf ("CompressorModule (optical/vactrol compressor) numeric verification\n");
    std::printf ("==================================================================\n\n");

    std::printf ("Stability sweep (NaN/Inf/blow-up), ceiling 50:\n");
    const double rates[] = { 48000.0, 96000.0, 192000.0 };
    const char* rateNames[] = { "48k", "96k", "192k" };
    for (int r = 0; r < 3; ++r)
    {
        std::printf ("  %s: ", rateNames[r]);
        if (! sweepForBlowup (rates[r], 50.0f))
            return 1;
    }
    std::printf ("\nAll checks passed.\n");
    return 0;
}
