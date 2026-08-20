// Numeric verification harness for the full-faithful Klon gain-stage port --
// not part of the shipping plugin (separate console-app target in
// CMakeLists.txt). Instantiates the real KlonModule (production code, no
// mocks) and:
//   1. Sweeps gain/treble/level x input amplitude x frequency at 1x/2x/4x
//      oversampling, flagging any NaN/Inf or unbounded output -- the
//      blow-up check the workflow mandates before any DSP change ships. The
//      WDF stages (PreAmp/FeedForward2/Clipper) are recursive scattering
//      networks, so a mistranscribed adaptor topology shows up here.
//   2. Confirms the two IIR stages (AmpStage, SummingAmp) have stable poles
//      across the full gain sweep (their coefficients are gain-driven).
//   3. Reports the small-signal / hot-input output levels so the
//      outputNormalization constant can be checked against the pedal's
//      documented "transparent at low gain" character.
#include <JuceHeader.h>
#include "DSP/KlonModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    // Runs the whole chain hard and flags NaN/Inf or anything exceeding
    // `ceiling`. Returns false (and prints) on the first failure.
    bool sweepForBlowup (double sampleRate, int oversamplingMode, float ceiling)
    {
        static const float gains[]   = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        static const float trebles[] = { 0.0f, 0.5f, 1.0f };
        static const float levels[]  = { 0.25f, 1.0f };
        static const float amps[]    = { 0.0f, 0.1f, 0.5f, 1.0f, 4.0f };
        static const float freqs[]   = { 60.0f, 440.0f, 3000.0f };

        KlonModule mod;
        juce::dsp::ProcessSpec spec { sampleRate, 256, 1 };
        mod.prepare (spec, oversamplingMode);
        mod.setEnabled (true);

        float peak = 0.0f;
        for (float gain : gains)
            for (float treble : trebles)
                for (float level : levels)
                    for (float amp : amps)
                        for (float freq : freqs)
                        {
                            mod.setParameters (gain, treble, level);
                            mod.reset();
                            for (int block = 0; block < 8; ++block)
                            {
                                juce::AudioBuffer<float> buf (1, 256);
                                for (int i = 0; i < 256; ++i)
                                {
                                    const double phase = 2.0 * kPi * (double) freq
                                                       * (double) (block * 256 + i) / sampleRate;
                                    buf.setSample (0, i, (float) (amp * std::sin (phase)));
                                }
                                mod.process (buf);
                                for (int i = 0; i < 256; ++i)
                                {
                                    const float v = buf.getSample (0, i);
                                    if (! std::isfinite (v))
                                    {
                                        std::printf ("  !! NaN/Inf at gain=%.2f treble=%.2f level=%.2f amp=%.1f freq=%.0f\n",
                                                     gain, treble, level, amp, freq);
                                        return false;
                                    }
                                    const float mag = std::fabs (v);
                                    if (mag > peak) peak = mag;
                                    if (mag > ceiling)
                                    {
                                        std::printf ("  !! unbounded (%.2f > %.1f) at gain=%.2f treble=%.2f level=%.2f amp=%.1f freq=%.0f\n",
                                                     mag, ceiling, gain, treble, level, amp, freq);
                                        return false;
                                    }
                                }
                            }
                        }
        std::printf ("  ok (no NaN/Inf; peak |out| = %.4f)\n", peak);
        return true;
    }

    // IIR stability: for a TDF2 filter with a[0]=1, the poles are inside the
    // unit circle iff |a[order]| < 1 and |a[1]| < 1 + a[order] (order 2).
    bool checkIirStability (double sampleRate)
    {
        std::printf ("  AmpStage pole stability vs gain:\n");
        for (float gain : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            KlonModule::AmpStage amp;
            amp.setGain (gain);
            amp.prepare ((float) sampleRate);
            const bool stable = std::fabs (amp.a[2]) < 1.0f && std::fabs (amp.a[1]) < 1.0f + amp.a[2];
            std::printf ("    gain=%.2f  a1=%+.4f a2=%+.4f  %s\n", gain, amp.a[1], amp.a[2], stable ? "stable" : "!! UNSTABLE");
            if (! stable) return false;
        }
        KlonModule::SummingAmp sum;
        sum.prepare ((float) sampleRate);
        const bool sumStable = std::fabs (sum.a[1]) < 1.0f;
        std::printf ("  SummingAmp: a1=%+.4f  %s\n", sum.a[1], sumStable ? "stable" : "!! UNSTABLE");
        return sumStable;
    }

    // Steady-state RMS ratio out/in over the tail of a long run. Uses RMS
    // (magnitude) so oversampling latency / phase don't matter -- we just
    // want "how loud is the output relative to the input".
    double measureRmsGain (double sampleRate, float gain, float treble, float level,
                           float amp, float freq, int oversamplingMode)
    {
        KlonModule mod;
        juce::dsp::ProcessSpec spec { sampleRate, 256, 1 };
        mod.prepare (spec, oversamplingMode);
        mod.setEnabled (true);
        mod.setParameters (gain, treble, level);
        mod.reset();

        const int total = (int) sampleRate;    // 1 second
        const int start = total / 2;           // skip transient + latency
        double sumInSq = 0.0, sumOutSq = 0.0;
        for (int block = 0; block < total / 256; ++block)
        {
            juce::AudioBuffer<float> buf (1, 256);
            for (int i = 0; i < 256; ++i)
                buf.setSample (0, i, (float) (amp * std::sin (2.0 * kPi * (double) freq * (double) (block * 256 + i) / sampleRate)));
            mod.process (buf);
            for (int i = 0; i < 256; ++i)
            {
                const int n = block * 256 + i;
                if (n >= start)
                {
                    const float in = (float) (amp * std::sin (2.0 * kPi * (double) freq * (double) n / sampleRate));
                    const float out = buf.getSample (0, i);
                    sumInSq += (double) in * (double) in;
                    sumOutSq += (double) out * (double) out;
                }
            }
        }
        return std::sqrt (sumOutSq / sumInSq);
    }

    void reportLevels (double sampleRate)
    {
        std::printf ("  RMS gain (48k, 2x oversampling, level=0.5, treble=0.5):\n");
        std::printf ("    small signal (0.001 V):\n");
        for (float gain : { 0.0f, 0.5f, 1.0f })
            std::printf ("      gain=%.2f  @440Hz  %.3f  @1kHz  %.3f\n", gain,
                         measureRmsGain (sampleRate, gain, 0.5f, 0.5f, 0.001f, 440.0f, 1),
                         measureRmsGain (sampleRate, gain, 0.5f, 0.5f, 0.001f, 1000.0f, 1));
        std::printf ("    hot signal (0.5 V):\n");
        for (float gain : { 0.0f, 0.5f, 1.0f })
            std::printf ("      gain=%.2f  @440Hz  %.3f  @1kHz  %.3f\n", gain,
                         measureRmsGain (sampleRate, gain, 0.5f, 0.5f, 0.5f, 440.0f, 1),
                         measureRmsGain (sampleRate, gain, 0.5f, 0.5f, 0.5f, 1000.0f, 1));
    }
}

int main()
{
    std::printf ("KlonModule gain-stage numeric verification\n===========================================\n\n");

    const double rates[] = { 48000.0, 96000.0, 192000.0 };
    const char* rateNames[] = { "1x (48k)", "2x (96k)", "4x (192k)" };
    const int osModes[] = { 0, 1, 2 };
    const char* osNames[] = { "no oversampling", "2x oversampling", "4x oversampling" };

    std::printf ("Stability sweep (NaN/Inf/blow-up), ceiling 20:\n");
    for (int r = 0; r < 3; ++r)
        for (int m = 0; m < 3; ++m)
        {
            std::printf ("  %s, %s: ", rateNames[r], osNames[m]);
            if (! sweepForBlowup (rates[r], osModes[m], 20.0f))
                return 1;
        }
    std::printf ("\n");

    std::printf ("IIR stage stability:\n");
    if (! checkIirStability (48000.0))
        return 1;
    std::printf ("\n");

    reportLevels (48000.0);

    std::printf ("\nDone.\n");
    return 0;
}
