// Numeric verification harness for "Plexer" (Maestro Echoplex EP-3-style
// tape echo) -- not part of the shipping plugin (separate console-app target
// in CMakeLists.txt).
// Instantiates the real EchoModule (production code, no mocks) and sweeps
// time x sustain x volume x mode x amplitude x frequency at 48/96/192k,
// flagging any NaN/Inf or unbounded output. This is the highest-risk delay:
// Sustain reaches genuine self-oscillation at maximum (Sound-on-Sound in
// particular runs near the feedback ceiling), bounded only by an ADAA write
// rail. The sweep runs 64 blocks so the feedback loop can reach steady state
// and confirm the rail actually holds.
#include <JuceHeader.h>
#include "DSP/EchoModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const float times[]     = { 50.0f, 300.0f, 600.0f };
        static const float sustains[]  = { 0.0f, 50.0f, 100.0f };
        static const float volumes[]   = { 0.0f, 100.0f };
        static const float amps[]      = { 0.1f, 0.5f, 2.0f };
        static const float freqs[]     = { 40.0f, 440.0f, 4000.0f };

        float peak = 0.0f;
        for (float time : times)
            for (float sustain : sustains)
                for (float volume : volumes)
                    for (int mode = 0; mode <= 1; ++mode)
                        for (float amp : amps)
                            for (float freq : freqs)
                            {
                                EchoModule mod;
                                juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
                                mod.prepare (spec);
                                mod.setParameters (time, sustain, volume, true,
                                                   static_cast<EchoModule::Mode> (mode));
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
                                                std::printf ("  !! NaN/Inf at time=%.0f sustain=%.0f vol=%.0f mode=%d amp=%.1f freq=%.0f\n",
                                                             time, sustain, volume, mode, amp, freq);
                                                return false;
                                            }
                                            const auto mag = std::fabs (val);
                                            if (mag > peak) peak = mag;
                                            if (mag > ceiling)
                                            {
                                                std::printf ("  !! unbounded (%.2f > %.1f) at sustain=%.0f mode=%d amp=%.1f\n",
                                                             mag, ceiling, sustain, mode, amp);
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
    std::printf ("EchoModule (Plexer, Echoplex-style tape echo) numeric verification\n");
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
