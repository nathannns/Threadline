// AmpModule per-voice output-level calibration probe -- not part of the
// shipping plugin (separate console-app target in CMakeLists.txt).
// Feeds sines at several representative frequencies through each of the 7
// voices (Output 0dB, flat EQ), lets sag/filters settle, then reports each
// voice's settled RMS and the scale needed to hit a target RMS. This is the
// measurement tool that produced AmpModule::perVoiceNormalise's constants --
// the "measure, don't guess" provenance for the per-voice loudness trim.
#include <JuceHeader.h>
#include "DSP/AmpModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    const char* voiceNames[] = { "vintage5E3", "modern3Band", "voxAC30", "fenderAB763",
                                 "jtm45", "mesaMarkI", "rolandJC120" };
    const float probeFreqs[] = { 110.0f, 440.0f, 1760.0f };
    constexpr int settleBlocks = 300;
    constexpr int measureBlocks = 64;

    float measureRms (int voice, double sampleRate, float drive, float amp, float freq)
    {
        AmpModule mod;
        juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
        mod.prepare (spec, 2); // 4x oversampling (production default)
        mod.setEnabled (true);
        mod.setParameters (drive, 0.5f, 0.0f, static_cast<AmpModule::Voice> (voice), 0.5f, 0.5f, 0.5f);

        double sumSq = 0.0; int count = 0;
        for (int block = 0; block < settleBlocks + measureBlocks; ++block)
        {
            juce::AudioBuffer<float> buf (2, 256);
            for (int i = 0; i < 256; ++i)
            {
                const double phase = 2.0 * kPi * (double) freq * (double) (block * 256 + i) / sampleRate;
                const auto s = (float) (amp * std::sin (phase));
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            mod.process (buf);
            if (block >= settleBlocks)
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 256; ++i)
                    {
                        const auto val = buf.getSample (ch, i);
                        sumSq += (double) val * (double) val;
                        ++count;
                    }
        }
        return (float) std::sqrt (sumSq / (double) count);
    }

    void reference (double sampleRate, float drive, float amp, float targetRms)
    {
        std::printf ("reference drive=%.2f input=%.2f @%.0fHz, target rms=%.3f:\n", drive, amp, sampleRate, targetRms);
        for (int v = 0; v <= 6; ++v)
        {
            double sum = 0.0;
            for (float f : probeFreqs)
                sum += measureRms (v, sampleRate, drive, amp, f);
            const auto rms = (float) (sum / 3.0);
            const auto scale = targetRms / (rms + 1e-9f);
            std::printf ("  %-12s  rms=%6.4f  scale=%6.4f  (%.2f dB)\n",
                         voiceNames[v], rms, scale, 20.0f * std::log10 (scale));
        }
        std::printf ("\n");
    }
}

int main()
{
    std::printf ("AmpModule per-voice output level probe\n");
    std::printf ("======================================\n\n");
    reference (48000.0, 0.30f, 0.30f, 0.500f);
    reference (48000.0, 0.50f, 0.30f, 0.500f);
    reference (48000.0, 0.90f, 0.30f, 0.500f);
    return 0;
}
