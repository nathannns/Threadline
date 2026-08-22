// AmpModule per-voice output-level calibration probe -- not part of the
// shipping plugin (separate console-app target in CMakeLists.txt).
// Feeds sines at several representative frequencies through each of the 7
// voices (Output 0dB, flat EQ), lets sag/filters settle, then reports each
// voice's settled RMS and the scale needed to hit a target RMS. This is the
// measurement tool used to derive AmpModule's fixed Deluxe-referenced Output
// trims -- the "measure, don't guess" provenance for per-voice loudness.
// `--noon-level` is the quick nominal-DI/noon-Gain calibration check; the
// longer default sweep remains useful for seeing each real circuit's natural
// loudness-vs-Gain curve without inserting a Drive-dependent correction.
#include <JuceHeader.h>
#include "DSP/AmpModule.h"
#include <cmath>
#include <cstdio>
#include <vector>

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

    float measureThd (int voice, float drive, float diRmsVolts = 0.100f)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 256, diSettleBlocks = 160, diMeasureBlocks = 64;
        constexpr double frequency = 187.5; // exact integer cycles in 16384 samples
        // 100mV RMS guitar at Threadline's Focusrite +12.25dBu reference:
        // 0.100V / 4.49073V-per-digital-unit, converted RMS -> peak.
        const float nominalDiPeak = diRmsVolts * std::sqrt (2.0f)
                                       * GuitarSignalLevel::digitalUnitsPerVolt;

        AmpModule mod;
        mod.prepare ({ sampleRate, blockSize, 2 }, 2);
        mod.setEnabled (true);
        mod.setParameters (drive, 0.5f, -18.0f, static_cast<AmpModule::Voice> (voice), 0.5f, 0.5f, 0.5f);

        std::vector<float> captured;
        captured.reserve ((size_t) blockSize * diMeasureBlocks);
        for (int block = 0; block < diSettleBlocks + diMeasureBlocks; ++block)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                const auto n = block * blockSize + i;
                const auto x = nominalDiPeak * (float) std::sin (2.0 * kPi * frequency * n / sampleRate);
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
            }
            mod.process (buffer);
            if (block >= diSettleBlocks)
                for (int i = 0; i < blockSize; ++i)
                    captured.push_back (buffer.getSample (0, i));
        }

        double harmonicSq = 0.0, fundamental = 0.0;
        for (int harmonic = 1; harmonic <= 10; ++harmonic)
        {
            double re = 0.0, im = 0.0;
            for (size_t n = 0; n < captured.size(); ++n)
            {
                const auto phase = 2.0 * kPi * frequency * harmonic * (double) n / sampleRate;
                re += captured[n] * std::cos (phase);
                im -= captured[n] * std::sin (phase);
            }
            const auto magnitude = 2.0 * std::sqrt (re * re + im * im) / (double) captured.size();
            if (harmonic == 1) fundamental = magnitude;
            else harmonicSq += magnitude * magnitude;
        }
        return (float) (100.0 * std::sqrt (harmonicSq) / juce::jmax (1.0e-12, fundamental));
    }
}

int main (int argc, char** argv)
{
    std::printf ("AmpModule per-voice output level probe\n");
    std::printf ("======================================\n\n");

    if (argc > 1 && juce::String (argv[1]) == "--di-thd")
    {
        std::printf ("Focusrite-calibrated guitar DI (100mV RMS), THD %% by Gain:\n");
        std::printf ("  voice             0%%       50%%      100%%\n");
        for (int voice = 0; voice <= 6; ++voice)
            std::printf ("  %-12s %8.3f  %8.3f  %8.3f\n", voiceNames[voice],
                         measureThd (voice, 0.0f), measureThd (voice, 0.5f), measureThd (voice, 1.0f));
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "--di-thd-levels")
    {
        std::printf ("THD %% by physical guitar level, Gain=0 / 50%%:\n");
        std::printf ("  voice          25mV G0  50mV G0 100mV G0 200mV G0 | 25mV G5  50mV G5 100mV G5 200mV G5\n");
        for (int voice = 0; voice <= 6; ++voice)
        {
            std::printf ("  %-12s", voiceNames[voice]);
            for (float gain : { 0.0f, 0.5f })
                for (float volts : { 0.025f, 0.050f, 0.100f, 0.200f })
                    std::printf (" %8.3f", measureThd (voice, gain, volts));
            std::printf ("\n");
        }
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "--noon-level")
    {
        reference (48000.0, 0.50f, 0.0314917f, 0.500f);
        return 0;
    }
    reference (48000.0, 0.30f, 0.30f, 0.500f);
    reference (48000.0, 0.50f, 0.30f, 0.500f);
    reference (48000.0, 0.90f, 0.30f, 0.500f);

    // Finer drive sweep at a fixed input level -- prints the per-voice RMS
    // at each drive so the spread (and the drive point where it is worst)
    // can be read directly rather than inferred from three spot checks.
    std::printf ("drive sweep, input=0.30, per-voice RMS (avg 110/440/1760Hz):\n");
    std::printf ("  drive    ");
    for (int v = 0; v <= 6; ++v) std::printf ("%12s", voiceNames[v]);
    std::printf ("\n");
    for (int k = 0; k <= 10; ++k)
    {
        const auto drive = 0.1f * static_cast<float> (k);
        std::printf ("  %.2f    ", drive);
        for (int v = 0; v <= 6; ++v)
        {
            double sum = 0.0;
            for (float f : probeFreqs) sum += measureRms (v, 48000.0, drive, 0.30f, f);
            std::printf ("%12.4f", (float) (sum / 3.0));
        }
        std::printf ("\n");
    }
    return 0;
}
