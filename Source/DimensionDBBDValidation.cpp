// Numeric verification harness for the circuit-faithful DC-2 "Dimension C"
// BBD port -- not part of the shipping plugin (separate console-app target in
// CMakeLists.txt). Instantiates the real DimensionDBBDModule (production
// code, no mocks) and:
//   1. Sweeps mode x input-level x input amplitude x frequency at 48/96/192k,
//      flagging any NaN/Inf or unbounded output -- the blow-up check the
//      workflow mandates before any DSP change ships. The dual BBD delay
//      lines + compander envelopes are recursive, so a mistranscribed gain
//      (e.g. an expander running away at high input level) shows up here.
//   2. Reports the per-mode delay window + LFO rate (from the module's own
//      preset table, so this prints exactly what each preset targets).
//   3. Reports small/hot-input RMS gain so the dry/wet blend and output
//      trims can be checked against a ~unity, non-boosting chorus.
#include <JuceHeader.h>
#include "DSP/DimensionDBBDModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const int modes[] = { 0, 1, 2, 3 };
        static const float inputs[] = { 0.25f, 0.7f, 1.0f };
        static const float amps[] = { 0.1f, 0.5f, 2.0f };
        static const float freqs[] = { 100.0f, 440.0f, 3000.0f };

        DimensionDBBDModule mod;
        juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
        mod.prepare (spec);

        float peak = 0.0f;
        for (int mode : modes)
            for (float input : inputs)
                for (float amp : amps)
                    for (float freq : freqs)
                    {
                        mod.reset();
                        mod.setParameters (mode, input, 0.7f);
                        // 16 blocks (~85ms at 48k) so the 50ms parameter
                        // smoothing ramp fully settles and the sweep actually
                        // exercises the target values, not the ramp toward
                        // them.
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
                                        std::printf ("  !! NaN/Inf at mode=%d input=%.2f amp=%.1f freq=%.0f\n",
                                                     mode, input, amp, freq);
                                        return false;
                                    }
                                    const auto mag = std::fabs (val);
                                    if (mag > peak) peak = mag;
                                    if (mag > ceiling)
                                    {
                                        std::printf ("  !! unbounded (%.2f > %.1f) at mode=%d input=%.2f amp=%.1f freq=%.0f\n",
                                                     mag, ceiling, mode, input, amp, freq);
                                        return false;
                                    }
                                }
                        }
                    }
        std::printf ("  ok (no NaN/Inf; peak |out| = %.4f)\n", peak);
        return true;
    }

    double measureRmsGain (double sampleRate, int mode, float input, float amp, float freq)
    {
        DimensionDBBDModule mod;
        juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
        mod.prepare (spec);
        mod.reset();
        mod.setParameters (mode, input, 0.7f);

        const int total = (int) sampleRate;    // 1 second
        const int start = total / 2;           // skip transient + smoothing ramp
        double sumInSq = 0.0, sumOutSq = 0.0;
        for (int block = 0; block < total / 256; ++block)
        {
            juce::AudioBuffer<float> buf (2, 256);
            for (int i = 0; i < 256; ++i)
            {
                const auto v = (float) (amp * std::sin (2.0 * kPi * (double) freq * (double) (block * 256 + i) / sampleRate));
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            mod.process (buf);
            for (int i = 0; i < 256; ++i)
            {
                const int n = block * 256 + i;
                if (n >= start)
                {
                    const auto in = (float) (amp * std::sin (2.0 * kPi * (double) freq * (double) n / sampleRate));
                    const auto out = 0.5f * (buf.getSample (0, i) + buf.getSample (1, i));
                    sumInSq += (double) in * (double) in;
                    sumOutSq += (double) out * (double) out;
                }
            }
        }
        return std::sqrt (sumOutSq / sumInSq);
    }

    void reportPresets()
    {
        static const char* roman[] = { "I", "II", "III", "IV" };
        std::printf ("  Per-mode delay window (centre +/- half-swing) and LFO rate:\n");
        for (int mode = 0; mode < 4; ++mode)
        {
            float center, swing, rateHz;
            DimensionDBBDModule::getPresetSpec (mode, center, swing, rateHz);
            std::printf ("    Mode %s: %.2f..%.2f ms, %.2f Hz\n", roman[mode],
                         (center - swing) * 1000.0, (center + swing) * 1000.0, rateHz);
        }
    }
}

int main()
{
    std::printf ("DimensionDBBDModule numeric verification\n==========================================\n\n");

    reportPresets();
    std::printf ("\nStability sweep (NaN/Inf/blow-up), ceiling 20:\n");

    const double rates[] = { 48000.0, 96000.0, 192000.0 };
    const char* rateNames[] = { "48k", "96k", "192k" };
    for (int r = 0; r < 3; ++r)
    {
        std::printf ("  %s: ", rateNames[r]);
        if (! sweepForBlowup (rates[r], 20.0f))
            return 1;
    }
    std::printf ("\n");

    std::printf ("  RMS gain (stereo, input=0.7, output=0.7):\n");
    std::printf ("    Mode I  @440Hz  small(0.1) %.3f  hot(0.5) %.3f\n",
                 measureRmsGain (48000.0, 0, 0.7f, 0.1f, 440.0f),
                 measureRmsGain (48000.0, 0, 0.7f, 0.5f, 440.0f));
    std::printf ("    Mode II @440Hz  small(0.1) %.3f  hot(0.5) %.3f\n",
                 measureRmsGain (48000.0, 1, 0.7f, 0.1f, 440.0f),
                 measureRmsGain (48000.0, 1, 0.7f, 0.5f, 440.0f));
    std::printf ("    Mode I  @1kHz   small(0.1) %.3f  hot(0.5) %.3f\n",
                 measureRmsGain (48000.0, 0, 0.7f, 0.1f, 1000.0f),
                 measureRmsGain (48000.0, 0, 0.7f, 0.5f, 1000.0f));
    std::printf ("    Mode I  @3kHz   small(0.1) %.3f  hot(0.5) %.3f\n",
                 measureRmsGain (48000.0, 0, 0.7f, 0.1f, 3000.0f),
                 measureRmsGain (48000.0, 0, 0.7f, 0.5f, 3000.0f));

    std::printf ("\nDone.\n");
    return 0;
}
