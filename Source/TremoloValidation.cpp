// Numeric verification harness for Tremolo (bias + harmonic voices) -- not
// part of the shipping plugin (separate console-app target in
// CMakeLists.txt).
// Instantiates the real TremoloModule (production code, no mocks) and sweeps
// amount x rate x voice x amplitude x frequency at 48/96/192k, flagging any
// NaN/Inf or unbounded output. Amplitude modulation can only reduce gain
// (bias voice) or crossfade two flat-summing bands (harmonic voice), so this
// is a cheap guard against the LFO or crossover running away at extreme rate.
#include <JuceHeader.h>
#include "DSP/TremoloModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const float amounts[] = { 0.0f, 50.0f, 100.0f };
        static const float rates[]   = { 0.1f, 5.0f, 20.0f };
        static const float amps[]    = { 0.1f, 0.5f, 2.0f };
        static const float freqs[]   = { 40.0f, 440.0f, 4000.0f };

        float peak = 0.0f;
        for (float amount : amounts)
            for (float rate : rates)
                for (int voice = 0; voice <= 1; ++voice)
                    for (float amp : amps)
                        for (float freq : freqs)
                        {
                            TremoloModule mod;
                            juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
                            mod.prepare (spec);
                            mod.setVoice (static_cast<TremoloModule::Voice> (voice));
                            mod.setAmount (amount);
                            mod.setRate (rate);
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
                                            std::printf ("  !! NaN/Inf at amount=%.0f rate=%.1f voice=%d amp=%.1f freq=%.0f\n",
                                                         amount, rate, voice, amp, freq);
                                            return false;
                                        }
                                        const auto mag = std::fabs (val);
                                        if (mag > peak) peak = mag;
                                        if (mag > ceiling)
                                        {
                                            std::printf ("  !! unbounded (%.2f > %.1f) at amount=%.0f amp=%.1f\n",
                                                         mag, ceiling, amount, amp);
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
    std::printf ("TremoloModule (bias + harmonic voices) numeric verification\n");
    std::printf ("==========================================================\n\n");

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
