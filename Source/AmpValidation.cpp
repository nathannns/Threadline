// Numeric verification harness for the Amp (7 tube/SS amp voices) -- not
// part of the shipping plugin (separate console-app target in
// CMakeLists.txt).
// Instantiates the real AmpModule (production code, no mocks) and sweeps
// voice x drive x tone x output-level x amplitude x frequency at 48/96/192k,
// flagging any NaN/Inf or unbounded output. Each of the 7 voices has its own
// nonlinear per-sample tube solve (grid-charge / cathode-voltage Newton
// iterations), so this is the single highest-risk module for a solver to
// diverge; the sweep drives every voice to both drive extremes.
#include <JuceHeader.h>
#include "DSP/AmpModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const float drives[]  = { 0.0f, 1.0f };
        static const float tones[]   = { 0.0f, 1.0f };
        static const float outputs[] = { -12.0f, 12.0f };
        static const float amps[]    = { 0.1f, 0.5f, 2.0f };
        static const float freqs[]   = { 40.0f, 440.0f, 4000.0f };

        float peak = 0.0f;
        for (int v = 0; v <= 6; ++v)
            for (float drive : drives)
                for (float tone : tones)
                    for (float outputDb : outputs)
                        for (float amp : amps)
                            for (float freq : freqs)
                            {
                                AmpModule mod;
                                juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
                                mod.prepare (spec, 2); // 4x oversampling (production default)
                                mod.setEnabled (true);
                                const auto voice = static_cast<AmpModule::Voice> (v);
                                mod.setParameters (drive, tone, outputDb, voice, tone, tone, tone);
                                for (int block = 0; block < 8; ++block)
                                {
                                    juce::AudioBuffer<float> buf (2, 256);
                                    for (int i = 0; i < 256; ++i)
                                    {
                                        const double phase = 2.0 * kPi * (double) freq
                                                           * (double) (block * 256 + i) / sampleRate;
                                        const auto s = (float) (amp * std::sin (phase));
                                        buf.setSample (0, i, s);
                                        buf.setSample (1, i, s);
                                    }
                                    mod.process (buf);
                                    for (int ch = 0; ch < 2; ++ch)
                                        for (int i = 0; i < 256; ++i)
                                        {
                                            const auto val = buf.getSample (ch, i);
                                            if (! std::isfinite (val))
                                            {
                                                std::printf ("  !! NaN/Inf at voice=%d drive=%.1f tone=%.1f out=%.0f amp=%.1f freq=%.0f\n",
                                                             v, drive, tone, outputDb, amp, freq);
                                                return false;
                                            }
                                            const auto mag = std::fabs (val);
                                            if (mag > peak) peak = mag;
                                            if (mag > ceiling)
                                            {
                                                std::printf ("  !! unbounded (%.2f > %.1f) at voice=%d drive=%.1f out=%.0f amp=%.1f\n",
                                                             mag, ceiling, v, drive, outputDb, amp);
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
    std::printf ("AmpModule (7 tube/SS amp voices) numeric verification\n");
    std::printf ("====================================================\n\n");

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
