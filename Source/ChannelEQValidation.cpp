// Numeric verification harness for the circuit-faithful Neve 1073 "Redface"
// channel-EQ port -- not part of the shipping plugin (separate console-app
// target in CMakeLists.txt). Instantiates the real ChannelEQModule
// (production code, no mocks) and:
//   1. Sweeps gain x frequency x amplitude at 48/96/192k, flagging any
//      NaN/Inf or unbounded output -- the blow-up check the workflow
//      mandates before any DSP change ships. The Class-A stage (asymmetric
//      clip + 2nd-harmonic core term) and the LC-derived mid peaking filter
//      are the recursive/nonlinear parts that could run away if a gain or Q
//      were mistranscribed.
//   2. Measures the mid band's actual peak frequency and Q for each of the
//      6 positions -- this is the point of the rewrite: the Q must come out
//      of the LC/R network (~2), not the old hardcoded 0.9.
//   3. Reports shelf corners and the HPF slope so the 2nd-order shelves and
//      3rd-order HPF can be checked against the real switch positions.
//   4. Confirms the output has no DC (the output-transformer DC blocker).
#include <JuceHeader.h>
#include "DSP/ChannelEQModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    // Steady-state RMS gain at a single frequency. Runs 1 second, ignores the
    // first half so the DC blocker + IIR transients settle, then averages.
    double gainAt (double sampleRate, float freq, float preampDb, int lowIdx, float lowDb,
                   int midIdx, float midDb, float highDb, int hpfIdx, bool hpfOn)
    {
        ChannelEQModule mod;
        juce::dsp::ProcessSpec spec { sampleRate, 256, 1 };
        mod.prepare (spec);
        mod.setEnabled (true);
        mod.reset();
        mod.setParameters (preampDb, lowIdx, lowDb, midIdx, midDb, highDb, hpfIdx, hpfOn);

        const int total = (int) sampleRate;
        const int start = total / 2;
        constexpr double amp = 0.1;   // small-signal: keep the Class-A stage linear
        double sumInSq = 0.0, sumOutSq = 0.0;
        for (int block = 0; block < total / 256; ++block)
        {
            juce::AudioBuffer<float> buf (1, 256);
            for (int i = 0; i < 256; ++i)
            {
                const int n = block * 256 + i;
                const auto v = (float) (amp * std::sin (2.0 * kPi * (double) freq * (double) n / sampleRate));
                buf.setSample (0, i, v);
            }
            mod.process (buf);
            for (int i = 0; i < 256; ++i)
            {
                const int n = block * 256 + i;
                if (n >= start)
                {
                    const auto in = amp * std::sin (2.0 * kPi * (double) freq * (double) n / sampleRate);
                    const auto out = buf.getSample (0, i);
                    sumInSq += in * in;
                    sumOutSq += (double) out * (double) out;
                }
            }
        }
        return std::sqrt (sumOutSq / sumInSq);
    }

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const float gains[] = { -20.0f, 0.0f, 20.0f };
        static const float bands[] = { -16.0f, 0.0f, 16.0f };
        static const float amps[] = { 0.1f, 0.5f, 2.0f };
        static const float freqs[] = { 40.0f, 440.0f, 4000.0f };
        static const int midIdx[] = { 0, 3, 5 };
        static const int hpfIdx[] = { 0, 3 };

        ChannelEQModule mod;
        juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
        mod.prepare (spec);
        mod.setEnabled (true);

        float peak = 0.0f;
        for (float gain : gains)
            for (float band : bands)
                for (float amp : amps)
                    for (float freq : freqs)
                        for (int mid : midIdx)
                            for (int hpf : hpfIdx)
                            {
                                mod.reset();
                                mod.setParameters (gain, 1, band, mid, band, band, hpf, true);
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
                                                std::printf ("  !! NaN/Inf at gain=%.0f band=%.0f amp=%.1f freq=%.0f mid=%d hpf=%d\n",
                                                             gain, band, amp, freq, mid, hpf);
                                                return false;
                                            }
                                            const auto mag = std::fabs (val);
                                            if (mag > peak) peak = mag;
                                            if (mag > ceiling)
                                            {
                                                std::printf ("  !! unbounded (%.2f > %.1f) at gain=%.0f band=%.0f amp=%.1f freq=%.0f\n",
                                                             mag, ceiling, gain, band, amp, freq);
                                                return false;
                                            }
                                        }
                                }
                            }
        std::printf ("  ok (no NaN/Inf; peak |out| = %.4f)\n", peak);
        return true;
    }

    void reportMidBand (double sampleRate, int midIdx)
    {
        // Set ONLY the mid band (0 dB elsewhere, HPF off, 0 dB trim), sweep
        // frequency, and locate the peak + the two -3 dB points to read the
        // realised f0 and Q. midDb = +12 so the -3 dB skirt is well above the
        // noise/rounding floor.
        const auto nominal = ChannelEQModule::getMidFrequencies()[(size_t) midIdx];
        double peakGain = 0.0, peakFreq = 0.0;
        for (int k = 0; k < 48; ++k)
        {
            const double f = nominal * std::pow (10.0, (k - 24) * 0.025);  // ~0.25..4x nominal
            const double g = gainAt (sampleRate, (float) f, 0.0f, 1, 0.0f, midIdx, 12.0f, 0.0f, 0, false);
            if (g > peakGain) { peakGain = g; peakFreq = f; }
        }
        // -3 dB point in LINEAR gain is peakGain * 10^(-3/20), i.e. the
        // point 3 dB below the peak, not peakGain - 3 (a linear subtraction
        // would read a far-wider bandwidth and a nonsense Q).
        const double minus3dB = peakGain * std::pow (10.0, -3.0 / 20.0);
        auto below = [&] (double dir)
        {
            double prev = peakFreq, prevG = peakGain;
            for (int k = 1; k < 48; ++k)
            {
                const double f = peakFreq * std::pow (10.0, dir * k * 0.01);
                const double g = gainAt (sampleRate, (float) f, 0.0f, 1, 0.0f, midIdx, 12.0f, 0.0f, 0, false);
                if (g <= minus3dB)
                {
                    const double t = (minus3dB - prevG) / (g - prevG);
                    return prev + t * (f - prev);
                }
                prev = f; prevG = g;
            }
            return peakFreq * dir;  // never reached in practice
        };
        const double lo = below (-1.0), hi = below (+1.0);
        const double q = peakFreq / (hi - lo);
        std::printf ("    %-5.0f Hz: peak %.0f Hz (+%.1f dB), Q = %.2f\n",
                     nominal, peakFreq, 20.0 * std::log10 (peakGain), q);
    }

    void reportShelvesAndHpf (double sampleRate)
    {
        std::printf ("  Low shelf (+12 dB @35 Hz): 20 Hz %.1f dB, 1 kHz %.1f dB\n",
                     20.0 * std::log10 (gainAt (sampleRate, 20.0f, 0.0f, 0, 12.0f, 0, 0.0f, 0.0f, 0, false)),
                     20.0 * std::log10 (gainAt (sampleRate, 1000.0f, 0.0f, 0, 12.0f, 0, 0.0f, 0.0f, 0, false)));
        std::printf ("  High shelf (+12 dB @12 kHz): 20 kHz %.1f dB, 1 kHz %.1f dB\n",
                     20.0 * std::log10 (gainAt (sampleRate, 20000.0f, 0.0f, 1, 0.0f, 0, 0.0f, 12.0f, 0, false)),
                     20.0 * std::log10 (gainAt (sampleRate, 1000.0f, 0.0f, 1, 0.0f, 0, 0.0f, 12.0f, 0, false)));
        // HPF: 3rd-order (18 dB/oct) so an octave below corner is ~-21 dB.
        std::printf ("  HPF @50 Hz: 50 Hz %.1f dB, 25 Hz %.1f dB, 12.5 Hz %.1f dB\n",
                     20.0 * std::log10 (gainAt (sampleRate, 50.0f, 0.0f, 1, 0.0f, 0, 0.0f, 0.0f, 0, true)),
                     20.0 * std::log10 (gainAt (sampleRate, 25.0f, 0.0f, 1, 0.0f, 0, 0.0f, 0.0f, 0, true)),
                     20.0 * std::log10 (gainAt (sampleRate, 12.5f, 0.0f, 1, 0.0f, 0, 0.0f, 0.0f, 0, true)));
    }

    bool dcRejection (double sampleRate)
    {
        // Feed a constant DC level; the output-transformer DC blocker must
        // drive the mean to ~0 regardless of the Class-A stage's 2nd-harmonic
        // DC bias. Without it, the 0.02*x*x term would sit on a DC offset.
        ChannelEQModule mod;
        juce::dsp::ProcessSpec spec { sampleRate, 256, 1 };
        mod.prepare (spec);
        mod.setEnabled (true);
        mod.reset();
        mod.setParameters (0.0f, 1, 0.0f, 0, 0.0f, 0.0f, 0, false);

        double sum = 0.0;
        const int total = (int) sampleRate;
        const int start = total / 2;   // ignore the DC-blocker's startup transient
        int counted = 0;
        for (int block = 0; block < total / 256; ++block)
        {
            juce::AudioBuffer<float> buf (1, 256);
            buf.clear();
            for (int i = 0; i < 256; ++i) buf.setSample (0, i, 0.5f);   // +0.5 DC in
            mod.process (buf);
            for (int i = 0; i < 256; ++i)
            {
                const int n = block * 256 + i;
                if (n >= start) { sum += buf.getSample (0, i); ++counted; }
            }
        }
        const double mean = sum / counted;
        std::printf ("  DC-in +0.5 -> steady-state output mean %.5f (should be ~0)\n", mean);
        return std::fabs (mean) < 1e-3;
    }
}

int main()
{
    std::printf ("ChannelEQModule (Neve 1073) numeric verification\n");
    std::printf ("================================================\n\n");

    std::printf ("Stability sweep (NaN/Inf/blow-up), ceiling 20:\n");
    const double rates[] = { 48000.0, 96000.0, 192000.0 };
    const char* rateNames[] = { "48k", "96k", "192k" };
    for (int r = 0; r < 3; ++r)
    {
        std::printf ("  %s: ", rateNames[r]);
        if (! sweepForBlowup (rates[r], 20.0f))
            return 1;
    }
    std::printf ("\n");

    std::printf ("Mid band peak frequency + Q (derived from LC/R, target Q ~2):\n");
    for (int m = 0; m < ChannelEQModule::numMidFreqs; ++m)
        reportMidBand (48000.0, m);
    std::printf ("\n");

    reportShelvesAndHpf (48000.0);
    std::printf ("\n");

    std::printf ("Output-transformer DC rejection:\n");
    const bool dcOk = dcRejection (48000.0);
    std::printf ("\n");

    std::printf (dcOk ? "All checks passed.\n" : "DC rejection FAILED.\n");
    return dcOk ? 0 : 1;
}
