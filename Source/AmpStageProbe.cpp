// Analysis-only amp stage probe. THREADLINE_AMP_ANALYSIS_TAPS exposes callback
// points in this target's AmpModule compilation only; the shipping plugin is
// compiled without the macro and contains neither callbacks nor their cost.
#include <JuceHeader.h>
#include "DSP/AmpModule.h"
#include "DSP/GuitarSignalLevel.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double hostRate = 48000.0;
    constexpr double analysisRate = hostRate * 4.0;
    constexpr int blockSize = 256;
    constexpr int hostCaptureSamples = 16384;
    constexpr int stageCaptureSamples = hostCaptureSamples * 4;
    constexpr double pi = 3.14159265358979323846;
    constexpr size_t stageCount = (size_t) AmpModule::AnalysisStage::count;

    const char* voiceNames[] = { "Vintage", "Boutique", "Vox", "Deluxe",
                                 "JTM45", "Mesa", "JC-120" };
    const char* stageNames[] = { "V1 in", "V1 out", "V2 in", "V2 out",
                                 "PI/EQ in", "PI/EQ out", "Power in", "Power out" };

    struct Capture
    {
        std::array<std::vector<float>, stageCount> stages;
        bool enabled = false;

        Capture()
        {
            for (auto& stage : stages)
                stage.reserve (stageCaptureSamples);
        }

        void clearAndEnable()
        {
            for (auto& stage : stages)
                stage.clear();
            enabled = true;
        }

        static void receive (void* context, AmpModule::AnalysisStage stage, float sample)
        {
            auto& self = *static_cast<Capture*> (context);
            auto& destination = self.stages[(size_t) stage];
            if (self.enabled && destination.size() < stageCaptureSamples)
                destination.push_back (sample);
        }
    };

    struct RenderResult
    {
        Capture capture;
        std::vector<float> output;
    };

    RenderResult render (int voice, float frequency, float gain = 0.5f)
    {
        RenderResult result;
        result.output.reserve (hostCaptureSamples);

        AmpModule amp;
        amp.prepare ({ hostRate, blockSize, 2 }, 2);
        amp.setEnabled (true);
        amp.setParameters (gain, 0.5f, -18.0f,
                           static_cast<AmpModule::Voice> (voice),
                           0.5f, 0.5f, 0.5f);
        amp.setAnalysisTapCallback (&Capture::receive, &result.capture);

        constexpr int settleBlocks = 160;
        constexpr int captureBlocks = hostCaptureSamples / blockSize;
        const auto peak = 0.100f * std::sqrt (2.0f)
                          * GuitarSignalLevel::digitalUnitsPerVolt;
        for (int blockIndex = 0; blockIndex < settleBlocks + captureBlocks; ++blockIndex)
        {
            if (blockIndex == settleBlocks)
                result.capture.clearAndEnable();

            juce::AudioBuffer<float> block (2, blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                const auto n = blockIndex * blockSize + i;
                const auto sample = peak * (float) std::sin (2.0 * pi * frequency * n / hostRate);
                block.setSample (0, i, sample);
                block.setSample (1, i, sample);
            }
            amp.process (block);
            if (blockIndex >= settleBlocks)
                for (int i = 0; i < blockSize; ++i)
                    result.output.push_back (block.getSample (0, i));
        }
        result.capture.enabled = false;
        return result;
    }

    float rms (const std::vector<float>& samples)
    {
        double sumSq = 0.0;
        for (auto sample : samples)
            sumSq += (double) sample * sample;
        return samples.empty() ? 0.0f : (float) std::sqrt (sumSq / (double) samples.size());
    }

    float relativeDb (float value, float reference)
    {
        return juce::Decibels::gainToDecibels (value / juce::jmax (reference, 1.0e-15f), -180.0f);
    }

    float spectralMagnitude (const std::vector<float>& samples, double rate, double frequency)
    {
        const auto order = samples.size() >= (size_t) stageCaptureSamples ? 16 : 14;
        const auto size = 1 << order;
        std::vector<float> fftData ((size_t) size * 2, 0.0f);
        std::copy_n (samples.begin(), juce::jmin ((size_t) size, samples.size()), fftData.begin());
        juce::dsp::WindowingFunction<float> window (
            (size_t) size, juce::dsp::WindowingFunction<float>::hann, false);
        window.multiplyWithWindowingTable (fftData.data(), (size_t) size);
        juce::dsp::FFT fft (order);
        fft.performFrequencyOnlyForwardTransform (fftData.data());
        const auto bin = juce::jlimit (0, size / 2,
            (int) std::lround (frequency / (rate / size)));
        return fftData[(size_t) bin];
    }

    void bassAudit()
    {
        constexpr std::array<float, 5> lowFrequencies { 40.0f, 60.0f, 80.0f, 120.0f, 250.0f };
        std::printf ("Stage-input bass relative to 1kHz (100mV RMS DI, Gain/EQ noon):\n");
        for (int voice = 0; voice <= 6; ++voice)
        {
            const auto reference = render (voice, 1000.0f);
            std::printf ("\n%s\n", voiceNames[voice]);
            std::printf ("stage          40Hz     60Hz     80Hz    120Hz    250Hz\n");
            std::array<RenderResult, lowFrequencies.size()> measurements {
                render (voice, lowFrequencies[0]), render (voice, lowFrequencies[1]),
                render (voice, lowFrequencies[2]), render (voice, lowFrequencies[3]),
                render (voice, lowFrequencies[4])
            };
            for (size_t stage = 0; stage < stageCount; ++stage)
            {
                std::printf ("%-11s", stageNames[stage]);
                const auto referenceRms = rms (reference.capture.stages[stage]);
                for (const auto& measurement : measurements)
                    std::printf (" %8.2f", relativeDb (rms (measurement.capture.stages[stage]), referenceRms));
                std::printf ("\n");
            }
        }
    }

    void boutiqueAliasAudit()
    {
        constexpr double fundamental = 7500.0;
        constexpr double generatedFourth = 30000.0;
        constexpr double outputAlias = 18000.0;
        const auto boutique = render (1, (float) fundamental);
        std::printf ("\nBoutique 7.5kHz alias-source audit (100mV RMS DI, Gain noon):\n");
        std::printf ("stage          30kHz fourth harmonic\n");
        for (size_t stage = 0; stage < stageCount; ++stage)
        {
            const auto& samples = boutique.capture.stages[stage];
            const auto fundamentalMagnitude = spectralMagnitude (samples, analysisRate, fundamental);
            const auto fourthMagnitude = spectralMagnitude (samples, analysisRate, generatedFourth);
            std::printf ("%-11s %9.2f dBc\n", stageNames[stage],
                         relativeDb (fourthMagnitude, fundamentalMagnitude));
        }
        std::printf ("Downsampled output alias at 18kHz: %.2f dBc\n",
                     relativeDb (spectralMagnitude (boutique.output, hostRate, outputAlias),
                                 spectralMagnitude (boutique.output, hostRate, fundamental)));
    }
}

int main()
{
    std::printf ("AmpModule analysis-only stage probe\n");
    std::printf ("===================================\n");
    bassAudit();
    boutiqueAliasAudit();
    std::printf ("\nNo production DSP was changed by this measurement target.\n");
    return 0;
}
