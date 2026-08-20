// Numeric verification harness for the speaker-cab IR loader -- not part of
// the shipping plugin (separate console-app target in CMakeLists.txt).
// Instantiates the real CabModule (production code, no mocks) and sweeps
// built-in-IR x mix x polarity x amplitude x frequency at 48/96/192k for
// NaN/Inf/blow-up. The cab is a normalised convolution (linear FIR, +6dB
// makeup) so it cannot blow up; this harness exists for completeness -- to
// confirm the built-in WAV decode + FFT-partition build and the fade-in path
// produce finite, bounded output across a few representative IRs. The IR loads
// asynchronously via AsyncUpdater, so the sweep drives the message-manager
// dispatch loop after loadBuiltInIR() (same pattern as SpringValidation).
#include <JuceHeader.h>
#include "DSP/CabModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        auto& mm = *juce::MessageManager::getInstance();

        static const int   irs[]    = { 0, 6, 11 };
        static const float mixes[]  = { 0.0f, 1.0f };
        static const bool  phases[] = { false, true };
        static const float amps[]   = { 0.1f, 0.5f, 2.0f };
        static const float freqs[]  = { 40.0f, 440.0f, 4000.0f };

        float peak = 0.0f;
        for (int ir : irs)
        {
            CabModule mod;
            juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
            mod.prepare (spec);
            mod.setEnabled (true);
            mod.loadBuiltInIR (ir);
            mm.runDispatchLoopUntil (100);                            // flush the async IR load
            mm.runDispatchLoopUntil (10);                             // settle

            for (float mix : mixes)
                for (bool phase : phases)
                    for (float amp : amps)
                        for (float freq : freqs)
                        {
                            mod.setMix (mix);
                            mod.setPhaseInverted (phase);
                            for (int block = 0; block < 8; ++block)
                            {
                                juce::AudioBuffer<float> buf (2, 256);
                                for (int i = 0; i < 256; ++i)
                                {
                                    const double phaseRad = 2.0 * kPi * (double) freq
                                                          * (double) (block * 256 + i) / sampleRate;
                                    const auto v = (float) (amp * std::sin (phaseRad));
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
                                            std::printf ("  !! NaN/Inf at ir=%d mix=%.1f phase=%d amp=%.1f freq=%.0f\n",
                                                         ir, mix, (int) phase, amp, freq);
                                            return false;
                                        }
                                        const auto mag = std::fabs (val);
                                        if (mag > peak) peak = mag;
                                        if (mag > ceiling)
                                        {
                                            std::printf ("  !! unbounded (%.2f > %.1f) at ir=%d mix=%.1f amp=%.1f\n",
                                                         mag, ceiling, ir, mix, amp);
                                            return false;
                                        }
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
    std::printf ("CabModule (speaker-cab IR loader) numeric verification\n");
    std::printf ("======================================================\n\n");

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
