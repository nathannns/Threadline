// PedalModule output-level calibration probe -- not part of the shipping
// plugin (separate console-app target in CMakeLists.txt).
// Feeds a 440 Hz sine through each of the 5 overdrive/fuzz pedals (TS9,
// Klon, Fangs, Growl, Bison) at a common reference (level=0.5, mix=1.0,
// input 0.3) across their drive knob, and reports settled RMS + peak. This
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
    const float amp = 0.30f, freq = 440.0f;
    const float drives[] = { 0.0f, 0.5f, 0.9f };
    const char* names[] = { "TS9", "Klon", "Fangs", "Growl", "Bison" };

    std::printf ("Pedal output (rms / peak) @ level=0.5 mix=1.0 input=0.3 440Hz 48k\n");
    std::printf ("%-8s %16s %16s %16s\n", "pedal", "d=0.0", "d=0.5", "d=0.9");

    auto measureOne = [&](int which, float d, float& outRms, float& outPeak)
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
                    const auto s = (float)(amp * std::sin (ph));
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
            measureOne (p, drives[k], rms, peak);
            std::printf (" %7.3f/%6.3f", rms, peak);
        }
        std::printf ("\n");
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
