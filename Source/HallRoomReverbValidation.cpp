// Numeric verification harness for the algorithmic Room/Hall/Plate reverb
// (orthogonal-mixed 8-line FDN) -- not part of the shipping plugin (separate
// console-app target in CMakeLists.txt).
// Instantiates the real HallRoomReverbModule (production code, no mocks) and
// sweeps pre-delay x decay x tone x mix x width x model x amplitude x
// frequency at 48/96/192k, flagging any NaN/Inf or unbounded output. The FDN
// is bounded (g < 1 by construction) with a tanh backstop, but an
// orthogonally-mixed network can still build gain at coincident frequencies,
// so the sweep runs 64 blocks to reach steady state across all three tanks.
#include <JuceHeader.h>
#include "DSP/HallRoomReverbModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const float preDelays[] = { 0.0f, 1.0f };
        static const float decays[]    = { 0.0f, 1.0f };
        static const float tones[]     = { 0.0f, 1.0f };
        static const float mixes[]     = { 0.0f, 100.0f };
        static const float widths[]    = { 0.0f, 100.0f };
        static const float amps[]      = { 0.1f, 0.5f, 2.0f };
        static const float freqs[]     = { 40.0f, 440.0f, 4000.0f };

        float peak = 0.0f;
        for (float preDelay : preDelays)
            for (float decay : decays)
                for (float tone : tones)
                    for (float mix : mixes)
                        for (float width : widths)
                            for (int model = 0; model < 3; ++model)
                                for (float amp : amps)
                                    for (float freq : freqs)
                                    {
                                        HallRoomReverbModule mod;
                                        juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
                                        mod.prepare (spec);
                                        mod.setParameters (preDelay, decay, tone, mix, width, true, model);
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
                                                        std::printf ("  !! NaN/Inf at preDelay=%.1f decay=%.1f tone=%.1f mix=%.0f width=%.0f model=%d amp=%.1f freq=%.0f\n",
                                                                     preDelay, decay, tone, mix, width, model, amp, freq);
                                                        return false;
                                                    }
                                                    const auto mag = std::fabs (val);
                                                    if (mag > peak) peak = mag;
                                                    if (mag > ceiling)
                                                    {
                                                        std::printf ("  !! unbounded (%.2f > %.1f) at decay=%.1f model=%d amp=%.1f\n",
                                                                     mag, ceiling, decay, model, amp);
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
    std::printf ("HallRoomReverbModule (8-line FDN reverb) numeric verification\n");
    std::printf ("============================================================\n\n");

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
