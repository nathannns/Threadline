// PedalModule output-level calibration probe -- not part of the shipping
// plugin (separate console-app target in CMakeLists.txt).
// Feeds a 440 Hz sine through each of the 5 overdrive/fuzz pedals (TS9,
// Klon, Fangs, Growl, Bison) at a common reference (level=0.5, mix=1.0,
// physical 100mV RMS Focusrite-calibrated guitar input) across their drive
// knob, and reports settled RMS + peak. This
// is the measurement tool that produced TS9Module::outputCalibration=1.25 and
// FangsModule::outputCalibration=1.48 (retuned so the clip-limited peak stays
// ~0.9 at level=0.5), and its Growl bias x fuzz grid doubles as a regression
// check for the fuzz-collapse bug (output going to ~silence at fuzz >= 0.5).
#include <JuceHeader.h>
#include "DSP/TS9Module.h"
#include "DSP/KlonModule.h"
#include "DSP/FangsModule.h"
#include "DSP/GrowlModule.h"
#include "DSP/BisonModule.h"
#include "DSP/AmpModule.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr int settleBlocks = 300;
    constexpr int measureBlocks = 64;
}

int main()
{
    const double sr = 48000.0;
    // 100mV RMS physical guitar under the shared +12.25dBu Focusrite
    // contract, converted to digital sine peak without normalising pickups.
    const float amp = 0.100f * std::sqrt (2.0f) * GuitarSignalLevel::digitalUnitsPerVolt;
    const float freq = 440.0f;
    const float drives[] = { 0.0f, 0.5f, 0.9f };
    const char* names[] = { "TS9", "Klon", "Fangs", "Growl", "Bison" };

    std::printf ("Pedal output (rms / peak) @ level=0.5 mix=1.0, physical 100mV RMS DI, 440Hz 48k\n");
    std::printf ("%-8s %16s %16s %16s\n", "pedal", "d=0.0", "d=0.5", "d=0.9");

    auto measureOne = [&](int which, float d, float inputPeak, float& outRms, float& outPeak)
    {
        double sumSq = 0.0; int count = 0; float peak = 0.0f;

        auto runBlocks = [&](auto& mod)
        {
            for (int block = 0; block < settleBlocks + measureBlocks; ++block)
            {
                juce::AudioBuffer<float> buf (2, 256);
                for (int i = 0; i < 256; ++i)
                {
                    const double ph = 2.0 * kPi * (double) freq * (double)(block*256+i) / sr;
                    const auto s = (float)(inputPeak * std::sin (ph));
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                mod.process (buf);
                if (block >= settleBlocks)
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < 256; ++i)
                        {
                            const auto v = buf.getSample (ch, i);
                            sumSq += (double)v * (double)v; ++count;
                            peak = std::max (peak, std::fabs (v));
                        }
            }
        };

        juce::dsp::ProcessSpec spec { sr, 256, 2 };
        if (which == 0) { TS9Module m; m.prepare (spec); m.setEnabled(true); m.setParameters (d, 0.5f, 0.5f); runBlocks (m); }
        else if (which == 1) { KlonModule m; m.prepare (spec); m.setEnabled(true); m.setParameters (d, 0.5f, 0.5f); runBlocks (m); }
        else if (which == 2) { FangsModule m; m.prepare (spec); m.setEnabled(true); m.setParameters (d, 0.5f, 0.5f, 1.0f); runBlocks (m); }
        else if (which == 3) { GrowlModule m; m.prepare (spec); m.setEnabled(true); m.setParameters (0.5f, d, 0.5f, 1.0f); runBlocks (m); }
        else { BisonModule m; m.prepare (spec); m.setEnabled(true); m.setParameters (d, 0.5f, 0.5f, 1.0f); runBlocks (m); }

        outRms = (float)std::sqrt (sumSq / (double)count);
        outPeak = peak;
    };

    for (int p = 0; p < 5; ++p)
    {
        std::printf ("%-8s", names[p]);
        for (int k = 0; k < 3; ++k)
        {
            float rms = 0.0f, peak = 0.0f;
            measureOne (p, drives[k], amp, rms, peak);
            std::printf (" %7.3f/%6.3f", rms, peak);
        }
        std::printf ("\n");
    }

    // A fuzz can be level-matched on normal guitar while still magnifying
    // interface/pickup noise far more than its peers. Measure the local
    // small-signal slope with a physical 100uV RMS sine (captured at the
    // same fixed Focusrite reference), well below a played note but above
    // float quantisation. No synthetic noise is injected by any module.
    const auto tinyInputPeak = 100.0e-6f * std::sqrt (2.0f)
                             * GuitarSignalLevel::digitalUnitsPerVolt;
    const auto tinyInputRms = tinyInputPeak / std::sqrt (2.0f);
    std::printf ("\nSmall-signal gain @ drive/noon, physical 100uV RMS input:\n");
    float growlSmallSignalGainDb = 0.0f;
    for (int p = 0; p < 5; ++p)
    {
        float rms = 0.0f, peak = 0.0f;
        measureOne (p, 0.5f, tinyInputPeak, rms, peak);
        const auto gainDb = juce::Decibels::gainToDecibels (rms / tinyInputRms, -160.0f);
        if (p == 3)
            growlSmallSignalGainDb = gainDb;
        std::printf ("  %-8s %+7.2f dB  out rms=%.8f\n", names[p], gainDb, rms);
    }
    if (growlSmallSignalGainDb > 16.0f)
    {
        std::printf ("  FAIL: Growl small-signal/noise-floor gain exceeds +16dB\n");
        return 1;
    }

    // End-to-end voltage-contract regression: each nonlinear pedal at noon
    // into Deluxe at noon. This catches the historical double conversion
    // where a pedal's already-amplified output was interpreted as a fresh
    // full-scale interface DI and became several physical volts at the amp.
    std::printf ("\nPedal -> Deluxe chain @ both Gain/Level noon, physical 100mV RMS DI:\n");
    auto measureChain = [&](auto& pedal, float& outRms, float& outPeak)
    {
        juce::dsp::ProcessSpec spec { sr, 256, 2 };
        AmpModule amplifier;
        amplifier.prepare (spec, 1);
        amplifier.setEnabled (true);
        amplifier.setParameters (0.5f, 0.5f, -18.0f, AmpModule::Voice::fenderAB763,
                                 0.5f, 0.5f, 0.5f);
        double sumSq = 0.0;
        int count = 0;
        float peak = 0.0f;
        for (int block = 0; block < settleBlocks + measureBlocks; ++block)
        {
            juce::AudioBuffer<float> buf (2, 256);
            for (int i = 0; i < 256; ++i)
            {
                const double ph = 2.0 * kPi * (double) freq * (double) (block * 256 + i) / sr;
                const auto s = amp * (float) std::sin (ph);
                buf.setSample (0, i, s); buf.setSample (1, i, s);
            }
            pedal.process (buf);
            amplifier.process (buf);
            if (block >= settleBlocks)
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 256; ++i)
                    {
                        const auto v = buf.getSample (ch, i);
                        sumSq += (double) v * v;
                        ++count;
                        peak = std::max (peak, std::abs (v));
                    }
        }
        outRms = (float) std::sqrt (sumSq / (double) count);
        outPeak = peak;
    };

    for (int p = 0; p < 5; ++p)
    {
        float rms = 0.0f, peak = 0.0f;
        juce::dsp::ProcessSpec spec { sr, 256, 2 };
        if (p == 0) { TS9Module m; m.prepare (spec); m.setEnabled (true); m.setParameters (0.5f, 0.5f, 0.5f); measureChain (m, rms, peak); }
        else if (p == 1) { KlonModule m; m.prepare (spec); m.setEnabled (true); m.setParameters (0.5f, 0.5f, 0.5f); measureChain (m, rms, peak); }
        else if (p == 2) { FangsModule m; m.prepare (spec); m.setEnabled (true); m.setParameters (0.5f, 0.5f, 0.5f, 1.0f); measureChain (m, rms, peak); }
        else if (p == 3) { GrowlModule m; m.prepare (spec); m.setEnabled (true); m.setParameters (0.5f, 0.5f, 0.5f, 1.0f); measureChain (m, rms, peak); }
        else { BisonModule m; m.prepare (spec); m.setEnabled (true); m.setParameters (0.5f, 0.5f, 0.5f, 1.0f); measureChain (m, rms, peak); }
        std::printf ("  %-8s rms=%7.4f peak=%7.4f  %s\n", names[p], rms, peak,
                     std::isfinite (rms) && std::isfinite (peak) && peak < 1.0f ? "ok" : "FAIL");
    }

    // Growl regression grid -- guards the fuzz-collapse bug: before the
    // Newton solve converged properly, fuzz >= 0.5 drove the output to
    // ~silence (~0.0006 RMS) across all bias settings. Every cell here must
    // stay well above zero.
    std::printf ("\nGrowl bias x fuzz RMS @ input=0.1, 440Hz (regression):\n");
    std::printf ("%-6s %8s %8s %8s\n", "bias", "f=0.0", "f=0.5", "f=0.9");
    for (float bias : { 0.2f, 0.5f, 0.8f })
    {
        std::printf ("%-6.1f", bias);
        for (float f : { 0.0f, 0.5f, 0.9f })
        {
            juce::dsp::ProcessSpec spec { sr, 256, 2 };
            GrowlModule m; m.prepare (spec); m.setEnabled (true);
            m.setParameters (bias, f, 0.5f, 1.0f);
            double sumSq = 0.0; int count = 0;
            for (int block = 0; block < settleBlocks + measureBlocks; ++block)
            {
                juce::AudioBuffer<float> buf (2, 256);
                for (int i = 0; i < 256; ++i)
                {
                    const double ph = 2.0 * kPi * 440.0 * (double)(block*256+i) / sr;
                    const auto s = (float)(0.1 * std::sin (ph));
                    buf.setSample (0, i, s); buf.setSample (1, i, s);
                }
                m.process (buf);
                if (block >= settleBlocks)
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < 256; ++i)
                        { const auto v = buf.getSample (ch, i); sumSq += (double)v*(double)v; ++count; }
            }
            std::printf (" %8.4f", (float)std::sqrt (sumSq / count));
        }
        std::printf ("\n");
    }
    return 0;
}
