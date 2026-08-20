// Numeric verification harness for "July" (Julia-style BBD chorus) -- not
// part of the shipping plugin (separate console-app target in
// CMakeLists.txt).
// Instantiates the real ChorusModule (production code, no mocks) and sweeps
// rate x depth x lag x waveform x dry/chorus/vibrato x amplitude x frequency
// at 48/96/192k, flagging any NaN/Inf or unbounded output. The only way to
// NaN here is an out-of-bounds modulated-delay read (there is no feedback
// path), so the sweep exercises the read-position clamp across the full
// delay/lag range.
#include <JuceHeader.h>
#include "DSP/ChorusModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const float rates[]   = { 0.1f, 2.0f, 10.0f };
        static const float depths[]  = { 0.0f, 100.0f };
        static const float lags[]    = { 0.0f, 100.0f };
        static const float dcvs[]    = { 0.0f, 50.0f, 100.0f };
        static const float amps[]    = { 0.1f, 0.5f, 2.0f };
        static const float freqs[]   = { 40.0f, 440.0f, 4000.0f };

        float peak = 0.0f;
        for (float rate : rates)
            for (float depth : depths)
                for (float lag : lags)
                    for (int wave = 0; wave <= 1; ++wave)
                        for (float dcv : dcvs)
                            for (float amp : amps)
                                for (float freq : freqs)
                                {
                                    ChorusModule mod;
                                    juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
                                    mod.prepare (spec);
                                    mod.setParameters (rate, depth, lag,
                                                       static_cast<ChorusModule::Waveform> (wave),
                                                       dcv, true);
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
                                                    std::printf ("  !! NaN/Inf at rate=%.1f depth=%.0f lag=%.0f wave=%d dcv=%.0f amp=%.1f freq=%.0f\n",
                                                                 rate, depth, lag, wave, dcv, amp, freq);
                                                    return false;
                                                }
                                                const auto mag = std::fabs (val);
                                                if (mag > peak) peak = mag;
                                                if (mag > ceiling)
                                                {
                                                    std::printf ("  !! unbounded (%.2f > %.1f) at rate=%.1f amp=%.1f\n",
                                                                 mag, ceiling, rate, amp);
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
    std::printf ("ChorusModule (July, Julia-style BBD chorus) numeric verification\n");
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
