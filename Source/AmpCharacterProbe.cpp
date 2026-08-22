// Guitar-relevant character regression probe for AmpModule. This is a
// separate console target and is never linked into the shipping plugin.
// It deliberately reports measurements instead of enforcing subjective
// pass/fail thresholds: checked-in baselines can be compared before any
// future circuit change without silently retuning production DSP.
#include <JuceHeader.h>
#include "DSP/AmpModule.h"
#include "DSP/GuitarSignalLevel.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    constexpr int fftOrder = 14;
    constexpr int captureSamples = 1 << fftOrder;
    constexpr double pi = 3.14159265358979323846;

    const char* voiceNames[] = { "Vintage", "Boutique", "Vox", "Deluxe",
                                 "JTM45", "Mesa", "JC-120" };

    struct Spectrum
    {
        std::vector<float> magnitude;
        double binWidth = sampleRate / captureSamples;

        float at (double frequency) const
        {
            const auto bin = juce::jlimit (0, (int) magnitude.size() - 1,
                                           (int) std::lround (frequency / binWidth));
            return magnitude[(size_t) bin];
        }

        double bandEnergy (double lowHz, double highHz) const
        {
            const auto first = juce::jlimit (0, (int) magnitude.size() - 1,
                                             (int) std::ceil (lowHz / binWidth));
            const auto last = juce::jlimit (first, (int) magnitude.size() - 1,
                                            (int) std::floor (highHz / binWidth));
            double energy = 0.0;
            for (int bin = first; bin <= last; ++bin)
                energy += (double) magnitude[(size_t) bin] * magnitude[(size_t) bin];
            return energy;
        }
    };

    float voltsRmsToDigitalPeak (float rmsVolts)
    {
        return rmsVolts * std::sqrt (2.0f) * GuitarSignalLevel::digitalUnitsPerVolt;
    }

    AmpModule makeAmp (int voice, float gain = 0.5f)
    {
        AmpModule amp;
        amp.prepare ({ sampleRate, blockSize, 2 }, 2);
        amp.setEnabled (true);
        // The diagnostic output trim prevents host-range clipping from hiding
        // circuit behaviour. It does not affect any nonlinear amp stage.
        amp.setParameters (gain, 0.5f, -18.0f,
                           static_cast<AmpModule::Voice> (voice),
                           0.5f, 0.5f, 0.5f);
        return amp;
    }

    using Generator = std::function<float (int)>;

    std::vector<float> renderContinuous (int voice, const Generator& generator,
                                         int settleBlocks = 160)
    {
        auto amp = makeAmp (voice);
        std::vector<float> captured;
        captured.reserve (captureSamples);
        const int captureBlocks = captureSamples / blockSize;

        for (int block = 0; block < settleBlocks + captureBlocks; ++block)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                const auto n = block * blockSize + i;
                const auto sample = generator (n);
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }
            amp.process (buffer);
            if (block >= settleBlocks)
                for (int i = 0; i < blockSize; ++i)
                    captured.push_back (buffer.getSample (0, i));
        }
        return captured;
    }

    std::vector<float> renderPluck (int voice, float rmsVolts)
    {
        auto amp = makeAmp (voice);
        juce::AudioBuffer<float> buffer (2, blockSize);

        // Let DC blockers, sag state and coupled stages reach their quiet
        // operating point before applying the transient.
        buffer.clear();
        for (int block = 0; block < 48; ++block)
            amp.process (buffer);

        const std::array<double, 6> frequencies { 82.4069, 123.4708, 164.8138,
                                                  207.6523, 246.9417, 329.6276 };
        std::vector<float> dry ((size_t) captureSamples, 0.0f);
        for (int n = 0; n < captureSamples; ++n)
        {
            const auto time = (double) n / sampleRate;
            double sample = 0.0;
            for (size_t string = 0; string < frequencies.size(); ++string)
                for (int harmonic = 1; harmonic <= 10; ++harmonic)
                {
                    const auto harmonicDecay = 0.58 / std::sqrt ((double) harmonic);
                    const auto amplitude = 1.0 / std::pow ((double) harmonic, 1.32);
                    sample += amplitude
                              * std::sin (2.0 * pi * frequencies[string] * harmonic * time
                                         + 0.31 * (double) string + 0.17 * harmonic)
                              * std::exp (-time / harmonicDecay);
                }
            // A finite pick attack avoids an impossible discontinuity while
            // retaining the upper partials of a normal DI transient.
            dry[(size_t) n] = (float) (sample * (1.0 - std::exp (-time / 0.0008)));
        }

        // Calibrate the first 100ms of the transient in volts. The later
        // decay must not cause normalization to exaggerate the pick attack.
        double attackSumSq = 0.0;
        constexpr int attackSamples = 4800;
        for (int n = 0; n < attackSamples; ++n)
            attackSumSq += (double) dry[(size_t) n] * dry[(size_t) n];
        const auto attackRms = std::sqrt (attackSumSq / attackSamples);
        const auto scale = rmsVolts * GuitarSignalLevel::digitalUnitsPerVolt
                           / juce::jmax (attackRms, 1.0e-12);
        for (auto& sample : dry)
            sample *= (float) scale;

        std::vector<float> captured;
        captured.reserve (captureSamples);

        for (int block = 0; block < captureSamples / blockSize; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto n = block * blockSize + i;
                const auto x = dry[(size_t) n];
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
            }
            amp.process (buffer);
            for (int i = 0; i < blockSize; ++i)
                captured.push_back (buffer.getSample (0, i));
        }
        return captured;
    }

    Spectrum spectrumOf (const std::vector<float>& samples)
    {
        jassert ((int) samples.size() == captureSamples);
        std::vector<float> fftData ((size_t) captureSamples * 2, 0.0f);
        std::copy (samples.begin(), samples.end(), fftData.begin());
        juce::dsp::WindowingFunction<float> window (captureSamples,
                                                    juce::dsp::WindowingFunction<float>::hann,
                                                    false);
        window.multiplyWithWindowingTable (fftData.data(), captureSamples);
        juce::dsp::FFT fft (fftOrder);
        fft.performFrequencyOnlyForwardTransform (fftData.data());
        fftData.resize ((size_t) captureSamples / 2 + 1);
        return { std::move (fftData), sampleRate / captureSamples };
    }

    float rmsOf (const std::vector<float>& samples)
    {
        double sum = 0.0;
        for (auto sample : samples)
            sum += (double) sample * sample;
        return (float) std::sqrt (sum / (double) std::max<size_t> (1, samples.size()));
    }

    float peakOf (const std::vector<float>& samples)
    {
        float peak = 0.0f;
        for (auto sample : samples)
            peak = juce::jmax (peak, std::abs (sample));
        return peak;
    }

    float dbRatio (double numerator, double denominator)
    {
        return (float) (20.0 * std::log10 (juce::jmax (numerator, 1.0e-15)
                                            / juce::jmax (denominator, 1.0e-15)));
    }

    void printDynamics (int voice)
    {
        auto measure = [voice] (float volts)
        {
            const auto peak = voltsRmsToDigitalPeak (volts);
            return rmsOf (renderContinuous (voice, [peak] (int n)
            {
                return peak * (float) std::sin (2.0 * pi * 187.5 * n / sampleRate);
            }));
        };

        const auto low = measure (0.025f);
        const auto nominal = measure (0.100f);
        const auto high = measure (0.200f);
        // An ideal linear circuit rises 18.06 dB from 25mV to 200mV.
        const auto compression = 20.0f * std::log10 (8.0f) - dbRatio (high, low);
        std::printf ("  %-9s %8.5f %8.5f %8.5f %10.2f\n",
                     voiceNames[voice], low, nominal, high, compression);
    }

    void printIntermodulation (int voice)
    {
        constexpr double f1 = 234.375;
        constexpr double f2 = 1828.125;
        const auto componentPeak = voltsRmsToDigitalPeak (0.100f / std::sqrt (2.0f));
        const auto spectrum = spectrumOf (renderContinuous (voice, [componentPeak] (int n)
        {
            return componentPeak * ((float) std::sin (2.0 * pi * f1 * n / sampleRate)
                                  + (float) std::sin (2.0 * pi * f2 * n / sampleRate));
        }));
        const auto reference = std::sqrt ((double) spectrum.at (f1) * spectrum.at (f1)
                                         + (double) spectrum.at (f2) * spectrum.at (f2));
        const auto products = std::sqrt ((double) spectrum.at (f2 - f1) * spectrum.at (f2 - f1)
                                        + (double) spectrum.at (f2 + f1) * spectrum.at (f2 + f1)
                                        + (double) spectrum.at (2.0 * f1) * spectrum.at (2.0 * f1)
                                        + (double) spectrum.at (2.0 * f2) * spectrum.at (2.0 * f2));
        std::printf ("  %-9s %9.2f dBc\n", voiceNames[voice], dbRatio (products, reference));
    }

    void printPluckSpectrum (int voice)
    {
        const auto samples = renderPluck (voice, 0.100f);
        const auto spectrum = spectrumOf (samples);
        const auto low = spectrum.bandEnergy (40.0, 250.0);
        const auto mid = spectrum.bandEnergy (250.0, 2500.0);
        const auto high = spectrum.bandEnergy (2500.0, 12000.0);
        const auto rms = rmsOf (samples);
        const auto peak = peakOf (samples);
        std::printf ("  %-9s %8.2f %9.2f %10.2f %10.2f\n", voiceNames[voice],
                     dbRatio (low, mid) * 0.5f, dbRatio (high, mid) * 0.5f,
                     dbRatio (peak, rms), peak);
    }

    void printAliasSentinel (int voice)
    {
        constexpr double fundamental = 7500.0;
        constexpr double fourthHarmonicAlias = 18000.0; // |48k - 4*7.5k|
        const auto peak = voltsRmsToDigitalPeak (0.100f);
        const auto spectrum = spectrumOf (renderContinuous (voice, [peak] (int n)
        {
            return peak * (float) std::sin (2.0 * pi * fundamental * n / sampleRate);
        }));
        std::printf ("  %-9s %9.2f dBc\n", voiceNames[voice],
                     dbRatio (spectrum.at (fourthHarmonicAlias), spectrum.at (fundamental)));
    }
}

int main()
{
    std::printf ("AmpModule guitar-character regression probe\n");
    std::printf ("===========================================\n");
    std::printf ("48kHz, Focusrite-calibrated physical DI, Gain noon, EQ noon.\n");
    std::printf ("Diagnostic output trim is -18dB; production DSP is unchanged.\n\n");

    std::printf ("Dynamic response (sine RMS at 25/100/200mV; +compression means saturation):\n");
    std::printf ("  voice       25mV    100mV    200mV  comp dB\n");
    for (int voice = 0; voice <= 6; ++voice) printDynamics (voice);

    std::printf ("\nTwo-tone IM products at 100mV RMS total (lower/more negative is cleaner):\n");
    for (int voice = 0; voice <= 6; ++voice) printIntermodulation (voice);

    std::printf ("\nSynthetic guitar pluck (energy relative to 250-2500Hz mid band):\n");
    std::printf ("  voice       low dB   high dB   crest dB       peak\n");
    for (int voice = 0; voice <= 6; ++voice) printPluckSpectrum (voice);

    std::printf ("\n7.5kHz alias sentinel at 18kHz (lower/more negative is better):\n");
    for (int voice = 0; voice <= 6; ++voice) printAliasSentinel (voice);

    std::printf ("\nInformational baseline only; no subjective pass/fail limits applied.\n");
    return 0;
}
