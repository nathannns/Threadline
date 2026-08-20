// Numeric verification harness for "Bison" (two-stage cascaded fuzz, Big
// Muff Pi archetype) -- not part of the shipping plugin (separate console-
// app target in CMakeLists.txt).
// Instantiates the real BisonModule (production code, no mocks) and sweeps
// sustain x tone x level x mix x amplitude x frequency at 48/96/192k,
// flagging any NaN/Inf or unbounded output. The two WDF diode-in-feedback
// stages in cascade are the only place to blow up (each stage's diode pair
// saturating against a hot input from the stage before it), so the sweep
// drives sustain to max across the full tone/level range.
#include <JuceHeader.h>
#include "DSP/BisonModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const float sustains[] = { 0.0f, 0.5f, 1.0f };
        static const float tones[]    = { 0.0f, 0.5f, 1.0f };
        static const float levels[]   = { 0.5f, 1.0f };
        static const float mixes[]    = { 0.0f, 1.0f };
        static const float amps[]     = { 0.1f, 0.5f, 2.0f };
        static const float freqs[]    = { 40.0f, 440.0f, 4000.0f };

        float peak = 0.0f;
        for (float sustain : sustains)
            for (float tone : tones)
                for (float level : levels)
                    for (float mix : mixes)
                        for (float amp : amps)
                            for (float freq : freqs)
                            {
                                BisonModule mod;
                                juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
                                mod.prepare (spec, 1); // 2x oversampling
                                mod.setEnabled (true);
                                mod.setParameters (sustain, tone, level, mix);
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
                                                std::printf ("  !! NaN/Inf at sustain=%.1f tone=%.1f level=%.1f mix=%.1f amp=%.1f freq=%.0f\n",
                                                             sustain, tone, level, mix, amp, freq);
                                                return false;
                                            }
                                            const auto mag = std::fabs (val);
                                            if (mag > peak) peak = mag;
                                            if (mag > ceiling)
                                            {
                                                std::printf ("  !! unbounded (%.2f > %.1f) at sustain=%.1f level=%.1f amp=%.1f\n",
                                                             mag, ceiling, sustain, level, amp);
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
    std::printf ("BisonModule (two-stage cascaded fuzz) numeric verification\n");
    std::printf ("========================================================\n\n");

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
