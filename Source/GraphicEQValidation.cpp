// Numeric verification harness for the 9-band graphic EQ -- not part of the
// shipping plugin (separate console-app target in CMakeLists.txt).
// Instantiates the real GraphicEQModule (production code, no mocks) and
// sweeps band-gain pattern x HPF/LPF x amplitude x frequency at 48/96/192k for
// NaN/Inf/blow-up. The module is a linear parallel-bank of biquads (zero
// feedback, zero nonlinearity) so it is genuinely low-risk; this harness
// exists for completeness -- to confirm the all-+12dB / all--12dB / alternating
// extremes and the HPF/LPF corners never produce a non-finite sample.
#include <JuceHeader.h>
#include "DSP/GraphicEQModule.h"
#include <array>
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const float amps[]  = { 0.1f, 0.5f, 2.0f };
        static const float freqs[] = { 40.0f, 440.0f, 4000.0f, 16000.0f };
        static const bool  onOff[] = { false, true };

        float peak = 0.0f;
        for (int mode = 0; mode < 4; ++mode)
        {
            std::array<float, GraphicEQModule::numBands> gains {};
            for (int b = 0; b < GraphicEQModule::numBands; ++b)
                gains[(size_t) b] = (mode == 0) ? -12.0f
                                  : (mode == 1) ? 0.0f
                                  : (mode == 2) ? 12.0f
                                  : ((b % 2) ? 12.0f : -12.0f);

            for (bool hpfOn : onOff)
                for (bool lpfOn : onOff)
                    for (float amp : amps)
                        for (float freq : freqs)
                        {
                            GraphicEQModule mod;
                            juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
                            mod.prepare (spec);
                            mod.setEnabled (true);
                            mod.setBandGains (gains);
                            mod.setHighPass (hpfOn, 20.0f);
                            mod.setLowPass (lpfOn, 20000.0f);
                            for (int block = 0; block < 8; ++block)
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
                                            std::printf ("  !! NaN/Inf at mode=%d hpf=%d lpf=%d amp=%.1f freq=%.0f\n",
                                                         mode, (int) hpfOn, (int) lpfOn, amp, freq);
                                            return false;
                                        }
                                        const auto mag = std::fabs (val);
                                        if (mag > peak) peak = mag;
                                        if (mag > ceiling)
                                        {
                                            std::printf ("  !! unbounded (%.2f > %.1f) at mode=%d amp=%.1f freq=%.0f\n",
                                                         mag, ceiling, mode, amp, freq);
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
    std::printf ("GraphicEQModule (9-band graphic EQ) numeric verification\n");
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
