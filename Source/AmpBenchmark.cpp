// Standalone timing harness for AmpModule -- not part of the shipping
// plugin (see CMakeLists.txt's separate AmpBenchmark executable target).
// Runs the real AmpModule::process() code on a fixed-size buffer many
// times and reports actual wall-clock cost against the real-time budget,
// so "is the amp heavy" has a concrete number instead of a guess.
#include <JuceHeader.h>
#include "DSP/AmpModule.h"
#include <chrono>
#include <cstdio>

namespace
{
    double benchmarkVoice (AmpModule::Voice voice, const char* name, int oversamplingMode,
                            double sampleRate, int blockSize, int numChannels, int numBlocks)
    {
        AmpModule amp;
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, (juce::uint32) numChannels };
        amp.prepare (spec, oversamplingMode);
        amp.setEnabled (true);
        amp.setParameters (0.5f, 0.5f, 0.0f, voice, 0.5f, 0.5f, 0.5f);

        juce::AudioBuffer<float> buffer (numChannels, blockSize);
        // Fixed test signal, not silence -- iteration counts in the tube
        // solves are fixed regardless of input, so silence would already
        // be representative, but a real signal rules out any accidental
        // early-out on zero input skewing the result.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < blockSize; ++i)
                data[i] = 0.3f * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) i / sampleRate);
        }

        // A few warm-up blocks so any one-time lazy init doesn't skew the
        // timed average.
        for (int i = 0; i < 8; ++i)
            amp.process (buffer);

        const auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < numBlocks; ++i)
            amp.process (buffer);
        const auto end = std::chrono::high_resolution_clock::now();

        const auto totalSeconds = std::chrono::duration<double> (end - start).count();
        const auto perBlockUs = (totalSeconds / numBlocks) * 1.0e6;
        const auto realTimeBudgetUs = (blockSize / sampleRate) * 1.0e6;
        const auto percentOfRealTime = (perBlockUs / realTimeBudgetUs) * 100.0;

        std::printf ("%-14s  oversampling=%dx  %8.2f us/block   budget=%8.2f us   %6.2f%% of real-time budget\n",
                     name, oversamplingMode == 0 ? 1 : (oversamplingMode == 1 ? 2 : 4),
                     perBlockUs, realTimeBudgetUs, percentOfRealTime);
        return percentOfRealTime;
    }
}

int main()
{
    const double sampleRate = 48000.0;
    const int blockSize = 256; // matches the 128-256 range reported in Ableton
    const int numChannels = 2;
    const int numBlocks = 4000; // ~21 seconds of audio's worth of blocks, timed

    std::printf ("AmpModule benchmark -- sampleRate=%.0f blockSize=%d channels=%d\n\n",
                 sampleRate, blockSize, numChannels);

    struct VoiceEntry { AmpModule::Voice voice; const char* name; };
    const VoiceEntry voices[] = {
        { AmpModule::Voice::vintage5E3,   "Vintage 5E3" },
        { AmpModule::Voice::modern3Band,  "Boutique" },
        { AmpModule::Voice::voxAC30,      "Vox Top Boost" },
        { AmpModule::Voice::fenderAB763,  "Deluxe 63" },
    };

    for (auto& v : voices)
        for (int os = 0; os <= 2; ++os)
            benchmarkVoice (v.voice, v.name, os, sampleRate, blockSize, numChannels, numBlocks);

    return 0;
}
