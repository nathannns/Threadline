// Numeric verification harness for the ported TS9Clipper -- not part of the
// shipping plugin (see CMakeLists.txt's separate TS9Validation executable
// target). Directly instantiates TS9Module::TS9Clipper (the real production
// code, no mock) and:
//   1. Sweeps drive x amplitude x frequency at 1x/2x/4x rates, flagging any
//      NaN/Inf or unbounded output (the "blow-up" check the workflow mandates
//      before any DSP change ships).
//   2. Measures the small-signal frequency response (diodes not conducting),
//      which exercises the full finite-gain op-amp scattering matrix -- the
//      thing most likely to be mistranscribed in the BYOD port.
//   3. Confirms the DC gain is ~0 (the 1uF input coupling cap blocks DC) and
//      reports the polarity (BYOD's topology is non-inverting, so gain
//      should be POSITIVE -- the previous ideal-op-amp clipper was inverting).
#include <JuceHeader.h>
#include "DSP/TS9Module.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    // Least-squares signed small-signal gain = sum(out*in)/sum(in^2) over the
    // steady-state tail, after the reactive elements (C2/C3/C4) have settled.
    double measureGain (TS9Module::TS9Clipper& clipper, float drive, double freq,
                        double sampleRate, double amplitude)
    {
        clipper.setFeedbackResistance (51000.0f + drive * 500000.0f);
        clipper.reset();
        const int total = (int) sampleRate;    // 1 second of samples
        const int start = total / 2;           // steady state only
        double sumInSq = 0.0, sumOutIn = 0.0;
        for (int i = 0; i < total; ++i)
        {
            const float in = (float) (amplitude * std::sin (2.0 * kPi * freq * (double) i / sampleRate));
            const float out = clipper.processSample (in);
            if (i >= start)
            {
                sumOutIn += (double) out * (double) in;
                sumInSq += (double) in * (double) in;
            }
        }
        return sumOutIn / sumInSq;
    }

    // Returns false (and prints) if anything is NaN/Inf or exceeds ceiling.
    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        static const float drives[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        static const float amps[]   = { 0.0f, 0.1f, 0.5f, 1.0f, 4.0f };
        static const float freqs[]  = { 60.0f, 440.0f, 3000.0f };

        TS9Module::TS9Clipper clipper;
        clipper.prepare (sampleRate);

        float peak = 0.0f;
        for (float drive : drives)
        {
            clipper.setFeedbackResistance (51000.0f + drive * 500000.0f);
            for (float amp : amps)
                for (float freq : freqs)
                {
                    clipper.reset();
                    // Drive hard for a while (diodes fully conducting, worst case
                    // for the Wright-Omega solve), then read back.
                    for (int i = 0; i < 20000; ++i)
                    {
                        const float in = (float) (amp * std::sin (2.0 * kPi * (double) freq * (double) i / sampleRate));
                        const float out = clipper.processSample (in);
                        if (! std::isfinite (out))
                        {
                            std::printf ("  !! NaN/Inf at drive=%.2f amp=%.1f freq=%.0f\n", drive, amp, freq);
                            return false;
                        }
                        const float mag = std::fabs (out);
                        if (mag > peak)
                            peak = mag;
                        if (mag > ceiling)
                        {
                            std::printf ("  !! unbounded (%.2f > %.1f) at drive=%.2f amp=%.1f freq=%.0f\n",
                                         mag, ceiling, drive, amp, freq);
                            return false;
                        }
                    }
                }
        }
        std::printf ("  ok (no NaN/Inf; peak |out| = %.4f V)\n", peak);
        return true;
    }

    void printFrequencyResponse (const char* label, double sampleRate, float drive)
    {
        TS9Module::TS9Clipper clipper;
        clipper.prepare (sampleRate);

        std::printf ("  %s (drive=%.2f, small-signal):\n", label, drive);
        static const double freqs[] = { 50.0, 100.0, 200.0, 400.0, 720.0, 1000.0, 2000.0, 4000.0, 8000.0 };
        for (double freq : freqs)
        {
            const double g = measureGain (clipper, drive, freq, sampleRate, 0.001);
            const double db = 20.0 * std::log10 (std::fabs (g) + 1.0e-12);
            std::printf ("    %6.0f Hz  gain %8.3f  (%+7.2f dB)\n", freq, g, db);
        }

        // DC gain: feed a constant 0.5V, read the settled output (should be ~0).
        clipper.setFeedbackResistance (51000.0f + drive * 500000.0f);
        clipper.reset();
        float dcOut = 0.0f;
        for (int i = 0; i < (int) sampleRate; ++i)
            dcOut = clipper.processSample (0.5f);
        std::printf ("    DC in=0.5V -> settled out = %.5f V (expect ~0: input cap blocks DC)\n\n", dcOut);
    }
}

int main()
{
    std::printf ("TS9Clipper numeric verification\n================================\n\n");

    const double rates[] = { 48000.0, 96000.0, 192000.0 };
    const char* rateNames[] = { "1x (48k)", "2x (96k)", "4x (192k)" };

    std::printf ("Stability sweep (NaN/Inf/blow-up), ceiling 10V:\n");
    for (int r = 0; r < 3; ++r)
    {
        std::printf ("  %s: ", rateNames[r]);
        if (! sweepForBlowup (rates[r], 10.0f))
            return 1;
    }
    std::printf ("\n");

    std::printf ("Small-signal frequency response (48k):\n");
    printFrequencyResponse ("clipper", 48000.0, 0.0f);
    printFrequencyResponse ("clipper", 48000.0, 1.0f);

    // Raw peak output at a realistic hot-guitar level (0.5 V), to check what
    // the module's outputCalibration actually multiplies.
    std::printf ("Raw peak |out| at 0.5V input (48k):\n");
    for (float drive : { 0.0f, 0.5f, 1.0f })
    {
        TS9Module::TS9Clipper clipper;
        clipper.prepare (48000.0);
        clipper.setFeedbackResistance (51000.0f + drive * 500000.0f);
        for (float freq : { 100.0f, 1000.0f })
        {
            clipper.reset();
            float peak = 0.0f;
            for (int i = 0; i < 48000; ++i)
            {
                const float in = (float) (0.5 * std::sin (2.0 * kPi * (double) freq * (double) i / 48000.0));
                const float out = clipper.processSample (in);
                peak = std::max (peak, std::fabs (out));
            }
            std::printf ("  drive=%.2f  %5.0f Hz  peak %.4f V\n", drive, freq, peak);
        }
    }

    std::printf ("\nFull-module smoke test (voicing shelf + oversampled clipper + tone filter,\n"
                 "all in place on the input buffer):\n");
    for (auto voicing : { TS9Module::Voicing::TS9, TS9Module::Voicing::TS808, TS9Module::Voicing::TS10 })
    {
        TS9Module mod;
        juce::dsp::ProcessSpec spec { 48000.0, 512, 2 };
        mod.prepare (spec, 1); // 2x oversampling
        mod.setEnabled (true);
        mod.setVoicing (voicing);
        mod.setParameters (0.5f, 0.5f, 0.5f);

        float peak = 0.0f;
        bool finite = true;
        for (int block = 0; block < 16; ++block)
        {
            juce::AudioBuffer<float> buf (2, 512);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (ch, i, (float) (0.3 * std::sin (2.0 * kPi * 440.0 * (double) i / 48000.0)));
            mod.process (buf);
            for (int ch = 0; ch < 2 && finite; ++ch)
                for (int i = 0; i < 512; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    finite = std::isfinite (v);
                    peak = std::max (peak, std::fabs (v));
                    if (! finite)
                        break;
                }
            if (! finite)
                break;
        }
        std::printf ("  voicing=%d: %s (peak |out| = %.4f over 16 blocks)\n",
                     (int) voicing, finite ? "ok" : "!! NaN/Inf", peak);
        if (! finite)
            return 1;
    }

    std::printf ("\nDone.\n");
    return 0;
}
