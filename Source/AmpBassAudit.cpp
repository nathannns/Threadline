// Offline low-frequency staging audit for AmpModule. It measures the complete
// production path at a calibrated 100mV RMS DI and Gain/EQ noon. The source
// topology inventory printed first comes directly from AmpModule's filter
// construction; this target never changes or participates in shipping DSP.
#include <JuceHeader.h>
#include "DSP/AmpModule.h"
#include "DSP/GuitarSignalLevel.h"
#include <array>
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    constexpr double pi = 3.14159265358979323846;
    const char* voiceNames[] = { "Vintage", "Boutique", "Vox", "Deluxe",
                                 "JTM45", "Mesa", "JC-120" };
    constexpr std::array<float, 6> frequencies { 40.0f, 60.0f, 80.0f,
                                                 120.0f, 250.0f, 1000.0f };

    float measure (int voice, float frequency)
    {
        AmpModule amp;
        amp.prepare ({ sampleRate, blockSize, 2 }, 2);
        amp.setEnabled (true);
        amp.setParameters (0.5f, 0.5f, -18.0f,
                           static_cast<AmpModule::Voice> (voice),
                           0.5f, 0.5f, 0.5f);

        constexpr int settleBlocks = 160;
        constexpr int measureBlocks = 64;
        const auto peak = 0.100f * std::sqrt (2.0f)
                          * GuitarSignalLevel::digitalUnitsPerVolt;
        double sumSq = 0.0;
        int count = 0;
        for (int blockIndex = 0; blockIndex < settleBlocks + measureBlocks; ++blockIndex)
        {
            juce::AudioBuffer<float> block (2, blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                const auto n = blockIndex * blockSize + i;
                const auto sample = peak * (float) std::sin (2.0 * pi * frequency * n / sampleRate);
                block.setSample (0, i, sample);
                block.setSample (1, i, sample);
            }
            amp.process (block);
            if (blockIndex >= settleBlocks)
                for (int i = 0; i < blockSize; ++i)
                {
                    const auto sample = block.getSample (0, i);
                    sumSq += (double) sample * sample;
                    ++count;
                }
        }
        return (float) std::sqrt (sumSq / count);
    }

    float relativeDb (float value, float reference)
    {
        return juce::Decibels::gainToDecibels (value / juce::jmax (reference, 1.0e-12f), -160.0f);
    }
}

int main()
{
    std::printf ("AmpModule low-frequency staging audit\n");
    std::printf ("=====================================\n\n");
    std::printf ("Source topology inventory (AmpModule::updateStaticFilters):\n");
    std::printf ("  DI -> V1: 48Hz, Q=0.707 high-pass for every voice.\n");
    std::printf ("  V1 -> V2: 72Hz, Q=0.707 high-pass for every tube voice.\n");
    std::printf ("  Vintage then uses its interstage Tone low-pass.\n");
    std::printf ("  Boutique/JTM45/Mesa use their passive Bassman-family tone stacks.\n");
    std::printf ("  Vox uses its Bass/Treble network; Deluxe its AB763 stack.\n");
    std::printf ("  JC-120 clips in IC2a before its active Bass/Mid/Treble EQ.\n\n");

    std::printf ("Measured output relative to 1kHz; 100mV RMS DI, Gain/EQ noon, 4x:\n");
    std::printf ("voice          40Hz     60Hz     80Hz    120Hz    250Hz   1k RMS\n");
    for (int voice = 0; voice <= 6; ++voice)
    {
        std::array<float, frequencies.size()> rms {};
        for (size_t i = 0; i < frequencies.size(); ++i)
            rms[i] = measure (voice, frequencies[i]);
        std::printf ("%-10s", voiceNames[voice]);
        for (size_t i = 0; i + 1 < rms.size(); ++i)
            std::printf (" %8.2f", relativeDb (rms[i], rms.back()));
        std::printf (" %8.5f\n", rms.back());
    }

    std::printf ("\nMeasurement only. No bass filter, gain stage, or amp voice was retuned.\n");
    return 0;
}
