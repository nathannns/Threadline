// Real-DI file runner for AmpModule. This diagnostic executable is not linked
// into Threadline. It preserves the file's captured amplitude exactly: no
// normalisation, peak targeting, AGC, or pickup compensation is performed.
#include <JuceHeader.h>
#include "DSP/AmpModule.h"
#include <cmath>
#include <cstdio>
#include <memory>

namespace
{
    constexpr int blockSize = 256;
    constexpr double maximumSeconds = 60.0;
    const char* voiceNames[] = { "Vintage", "Boutique", "Vox", "Deluxe",
                                 "JTM45", "Mesa", "JC-120" };

    struct Metrics
    {
        double sumSq = 0.0, lowSq = 0.0, midSq = 0.0, highSq = 0.0;
        float peak = 0.0f;
        int samples = 0;
    };

    struct BandMeters
    {
        void prepare (double sampleRate)
        {
            const juce::dsp::ProcessSpec spec { sampleRate, blockSize, 1 };
            for (auto* filter : { &low, &midHighPass, &midLowPass, &high })
                filter->prepare (spec);
            *low.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 250.0);
            *midHighPass.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 250.0);
            *midLowPass.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 2500.0);
            *high.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 2500.0);
        }

        void measure (float sample, Metrics& metrics)
        {
            const auto lowSample = low.processSample (sample);
            const auto midSample = midLowPass.processSample (midHighPass.processSample (sample));
            const auto highSample = high.processSample (sample);
            metrics.lowSq += (double) lowSample * lowSample;
            metrics.midSq += (double) midSample * midSample;
            metrics.highSq += (double) highSample * highSample;
        }

        juce::dsp::IIR::Filter<float> low, midHighPass, midLowPass, high;
    };

    float dbEnergyRatio (double numerator, double denominator)
    {
        return (float) (10.0 * std::log10 (juce::jmax (numerator, 1.0e-20)
                                            / juce::jmax (denominator, 1.0e-20)));
    }

    Metrics analyseAmp (const juce::AudioBuffer<float>& monoInput, double sampleRate,
                        int voice, float gain, float outputDb)
    {
        AmpModule amp;
        amp.prepare ({ sampleRate, blockSize, 2 }, 2);
        amp.setEnabled (true);
        amp.setParameters (gain, 0.5f, outputDb,
                           static_cast<AmpModule::Voice> (voice),
                           0.5f, 0.5f, 0.5f);

        BandMeters bands;
        bands.prepare (sampleRate);
        Metrics metrics;
        juce::AudioBuffer<float> block (2, blockSize);

        for (int offset = 0; offset < monoInput.getNumSamples(); offset += blockSize)
        {
            const auto count = juce::jmin (blockSize, monoInput.getNumSamples() - offset);
            block.clear();
            block.copyFrom (0, 0, monoInput, 0, offset, count);
            block.copyFrom (1, 0, monoInput, 0, offset, count);
            amp.process (block);
            for (int i = 0; i < count; ++i)
            {
                const auto sample = block.getSample (0, i);
                metrics.sumSq += (double) sample * sample;
                metrics.peak = juce::jmax (metrics.peak, std::abs (sample));
                bands.measure (sample, metrics);
                ++metrics.samples;
            }
        }
        return metrics;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("Usage: AmpDIProbe <focusrite-di.wav|aiff> [gain-0..1] [output-dB]\n");
        std::printf ("Reads channel 1 exactly as captured; never normalises it.\n");
        return 2;
    }

    const juce::File inputFile (juce::String::fromUTF8 (argv[1]));
    const auto gain = argc >= 3
        ? juce::jlimit (0.0f, 1.0f, (float) std::atof (argv[2]))
        : 0.5f;
    const auto outputDb = argc >= 4
        ? juce::jlimit (-24.0f, 12.0f, (float) std::atof (argv[3]))
        : -18.0f;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inputFile));
    if (reader == nullptr || reader->numChannels == 0 || reader->sampleRate <= 0.0)
    {
        std::fprintf (stderr, "Could not read audio file: %s\n", inputFile.getFullPathName().toRawUTF8());
        return 1;
    }

    const auto maximumSamples = (juce::int64) std::llround (reader->sampleRate * maximumSeconds);
    const auto samplesToRead64 = juce::jmin (reader->lengthInSamples, maximumSamples);
    if (samplesToRead64 <= 0 || samplesToRead64 > std::numeric_limits<int>::max())
    {
        std::fprintf (stderr, "The selected file contains no analysable samples.\n");
        return 1;
    }

    juce::AudioBuffer<float> input (1, (int) samplesToRead64);
    if (! reader->read (&input, 0, input.getNumSamples(), 0, true, false))
    {
        std::fprintf (stderr, "Failed while reading: %s\n", inputFile.getFullPathName().toRawUTF8());
        return 1;
    }

    double drySumSq = 0.0;
    float dryPeak = 0.0f;
    for (int i = 0; i < input.getNumSamples(); ++i)
    {
        const auto sample = input.getSample (0, i);
        drySumSq += (double) sample * sample;
        dryPeak = juce::jmax (dryPeak, std::abs (sample));
    }
    const auto dryRms = std::sqrt (drySumSq / input.getNumSamples());

    std::printf ("Threadline real-DI amp analysis\n");
    std::printf ("File: %s\n", inputFile.getFileName().toRawUTF8());
    std::printf ("Rate: %.0f Hz, analysed: %.2f s, channel: 1, Gain: %.3f, Output: %+.1f dB\n",
                 reader->sampleRate, input.getNumSamples() / reader->sampleRate, gain, outputDb);
    std::printf ("Dry RMS %.6f, peak %.6f (amplitude preserved; no normalisation)\n\n",
                 dryRms, dryPeak);
    std::printf ("voice          RMS      peak   crest dB  low/mid dB  high/mid dB\n");

    for (int voice = 0; voice <= 6; ++voice)
    {
        const auto metrics = analyseAmp (input, reader->sampleRate, voice, gain, outputDb);
        const auto rms = std::sqrt (metrics.sumSq / juce::jmax (1, metrics.samples));
        const auto crest = 20.0 * std::log10 (juce::jmax ((double) metrics.peak, 1.0e-15)
                                               / juce::jmax (rms, 1.0e-15));
        std::printf ("%-10s %8.5f %8.5f %10.2f %11.2f %12.2f\n",
                     voiceNames[voice], rms, metrics.peak, crest,
                     dbEnergyRatio (metrics.lowSq, metrics.midSq),
                     dbEnergyRatio (metrics.highSq, metrics.midSq));
    }
    return 0;
}
