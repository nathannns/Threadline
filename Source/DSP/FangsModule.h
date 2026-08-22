#pragma once
#include <JuceHeader.h>
#include "WDFCore.h"
#include "GuitarSignalLevel.h"

// "Fangs" -- an original op-amp gain-stage-then-diode-clip distortion in
// the RAT/Distortion+ family. The signal flow is checked against the ProCo
// RAT and MXR Distortion+ drawings in el34world's Effects archive; Fangs
// keeps its own control taper and calibration rather than claiming to be
// either named pedal.
//
// Real RAT/Distortion+-family schematics (checked directly against both,
// not just remembered folklore) share one structural trait that TS9/Klon
// don't: the clipping diodes sit AFTER a clean, high-gain op-amp stage,
// clamped to ground through a series resistor -- not inside that stage's
// own feedback loop. That's a genuinely different, much harder/more
// abrupt clipping character than a diode-in-feedback design (the gain
// stage itself never "sees" the diodes and so never softens its own
// loop gain approaching the knee, unlike TS9Module's clipper), and it's
// general textbook circuit topology, not either pedal's specific BOM, so
// it's fair game to model structurally even while declining to copy
// their exact component values. The RAT drawing exposes a detail the old
// generic gain block missed: two feedback legs (560R/4.7uF and 47R/2.2uF)
// introduce gain shelves at about 60.5Hz and 1.54kHz. That keeps deep bass
// from receiving the same enormous gain as guitar mids. The one-pole states
// below implement those legs' linear transfer before the WDF silicon pair.
//
// Post-clip "Filter" is a genuine treble-cut lowpass sweep -- the real
// RAT-family "Filter" control is exactly this (darker at one end,
// progressively brighter/more open toward the other, same convention
// TS9Module's own Tone knob already uses), NOT a mid-scoop notch. An
// earlier version of this file used the shared ComplementaryToneStack
// (built for BisonModule's genuinely notch-shaped Tone control below)
// here too, which meant Filter's default center position applied a deep
// notch by default -- audibly "muffled/behind a wall" -- rather than
// reading as a neutral, open passthrough the way a RAT-style pedal
// actually behaves at noon on its Filter knob.
class FangsModule
{
public:
    struct FangsClipper
    {
        WDF::ResistiveVoltageSource clipSource { 1000.0f };
        WDF::DiodePair<decltype (clipSource)> dp { clipSource, 4.352e-9f, 0.02585f * 1.906f };

        void prepare (double wdfSampleRate)
        {
            lowShelfCoefficient = highPassCoefficient (wdfSampleRate, 560.0, 4.7e-6);
            presenceCoefficient = highPassCoefficient (wdfSampleRate, 47.0, 2.2e-6);
            dp.calcImpedance();
            reset();
        }

        void reset()
        {
            clipSource.wdf.a = clipSource.wdf.b = 0.0f;
            lowShelfInput = presenceInput = lowShelfOutput = presenceOutput = 0.0f;
        }

        float processSample (float vin, float gainResistance) noexcept
        {
            const auto lowShelf = lowShelfCoefficient * (lowShelfOutput + vin - lowShelfInput);
            const auto presence = presenceCoefficient * (presenceOutput + vin - presenceInput);
            lowShelfInput = presenceInput = vin;
            lowShelfOutput = lowShelf;
            presenceOutput = presence;

            const auto cleanGainOutput = vin
                + gainResistance * (lowShelf / 560.0f + presence / 47.0f);

            clipSource.setVoltage (cleanGainOutput);
            dp.incident (clipSource.reflected());
            clipSource.incident (dp.reflected());
            return WDF::voltage (clipSource.wdf);
        }

    private:
        static float highPassCoefficient (double rate, double resistance, double capacitance) noexcept
        {
            const auto rc = resistance * capacitance;
            return static_cast<float> (rc / (rc + 1.0 / rate));
        }

        float lowShelfCoefficient = 0.0f, presenceCoefficient = 0.0f;
        float lowShelfInput = 0.0f, presenceInput = 0.0f;
        float lowShelfOutput = 0.0f, presenceOutput = 0.0f;
    };

    // oversamplingMode: 0 = off (1x), 1 = 2x, 2 = 4x -- same convention as
    // AmpModule/KlonModule/TS9Module's own oversamplingMode.
    void prepare (const juce::dsp::ProcessSpec& spec, int oversamplingMode = 1)
    {
        sampleRate = spec.sampleRate;
        channelCount = juce::jlimit (1, 2, (int) spec.numChannels);
        const auto stages = juce::jlimit (0, 2, oversamplingMode);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) channelCount, stages,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversampling->initProcessing (spec.maximumBlockSize);
        wetBuffer.setSize (channelCount, static_cast<int> (spec.maximumBlockSize), false, false, true);
        for (auto& c : clipper)
            c.prepare (sampleRate * (double) oversampling->getOversamplingFactor());
        for (auto& f : filterLowpass)
            f.prepare (spec);
        updateFilter();

        dryDelay.prepare (spec);
        dryDelay.setMaximumDelayInSamples (64);
        dryDelay.setDelay ((float) getLatencySamples());

        reset();
    }

    void reset()
    {
        if (oversampling != nullptr)
            oversampling->reset();
        for (auto& c : clipper)
            c.reset();
        for (auto& f : filterLowpass)
            f.reset();
        dryDelay.reset();
    }

    int getLatencySamples() const noexcept
    {
        return oversampling != nullptr ? juce::roundToInt (oversampling->getLatencyInSamples()) : 0;
    }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    // gain/filter/level/mix: 0-1.
    void setParameters (float gain01, float filter01, float level01, float mix01)
    {
        gainAmount = juce::jlimit (0.0f, 1.0f, gain01);
        const auto level = juce::jlimit (0.0f, 1.0f, level01);
        outputLevel = std::pow (level, 2.50f);
        mix = juce::jlimit (0.0f, 1.0f, mix01);
        filter01 = juce::jlimit (0.0f, 1.0f, filter01);
        if (! juce::approximatelyEqual (filter01, lastFilter01))
        {
            lastFilter01 = filter01;
            updateFilter();
        }
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled || oversampling == nullptr)
            return;

        const auto numChannels = juce::jmin (buffer.getNumChannels(), channelCount);
        const auto numSamples = buffer.getNumSamples();

        // Move resistance in the source-derived feedback network rather
        // than pre-scaling the DI or driving a fixed clip curve. The deep
        // audio taper keeps the useful low-gain range available.
        const auto gainResistance = 100.0f + std::pow (gainAmount, 4.8f) * 149900.0f;

        dryDelay.setDelay ((float) getLatencySamples());

        // buffer itself stays the untouched dry reference throughout --
        // the wet path runs entirely in wetBuffer (a copy) until the final
        // mix write, same pattern KlonModule's preClipBuffer uses.
        wetBuffer.makeCopyOf (buffer, true);

        juce::dsp::AudioBlock<float> block (wetBuffer);
        auto osBlock = oversampling->processSamplesUp (block);
        const auto osSamples = (int) osBlock.getNumSamples();
        for (size_t ch = 0; ch < osBlock.getNumChannels(); ++ch)
        {
            for (int i = 0; i < osSamples; ++i)
            {
                const auto x = GuitarSignalLevel::toVolts (osBlock.getSample ((int) ch, i));
                // Real op-amp clipper output (volts) -- outputCalibration
                // brings that to a sensible audio range, same role as
                // Klon/TS9's own calibration constants. A wide tanh safety
                // rail backstops that guess (the diode pair itself already
                // self-limits, same as real hardware).
                auto clipped = clipper[ch].processSample (x, gainResistance) * outputCalibration;
                clipped = safetyCeiling * std::tanh (clipped / safetyCeiling);
                osBlock.setSample ((int) ch, i, GuitarSignalLevel::fromVolts (clipped));
            }
        }
        oversampling->processSamplesDown (block);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* dry = buffer.getWritePointer (ch);
            auto* wet = wetBuffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                const auto toned = filterLowpass[ch].processSample (wet[i]);
                dryDelay.pushSample (ch, dry[i]);
                const auto delayedDry = dryDelay.popSample (ch);
                dry[i] = (delayedDry * (1.0f - mix) + toned * mix) * outputLevel;
            }
        }
    }

private:
    void updateFilter()
    {
        if (sampleRate <= 0.0)
            return;
        // Source network: the 100k Filter pot feeds 1.5k/3.3nF. Preserve
        // Threadline's left=dark/right=bright convention while deriving
        // the actual endpoints from that circuit.
        const auto resistance = (1.0f - lastFilter01) * 100000.0f + 1500.0f;
        const auto analogueCutoff = 1.0f / (juce::MathConstants<float>::twoPi * resistance * 3.3e-9f);
        const auto cutoff = juce::jmin (analogueCutoff, static_cast<float> (sampleRate * 0.45));
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, cutoff, 0.707f);
        for (auto& f : filterLowpass)
            *f.coefficients = *coeffs;
    }

    juce::dsp::DelayLine<float> dryDelay;
    juce::dsp::IIR::Filter<float> filterLowpass[2];
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> wetBuffer;
    FangsClipper clipper[2];
    // Empirically-tuned, not physically derived (same role as Klon/TS9's own
    // calibration constants). 6.0 mapped the diode-pair clip point (~0.63V)
    // to ~2.5 peak at level=0.5 -- deep into hard clipping with the knob at
    // noon. Retuned (measured via PedalLevelProbe, same discipline as
    // AmpModule::perVoiceNormalise) to 1.48 so the clipped peak lands at ~0.9
    // internally; the measured output-pot taper above sets the chain level.
    static constexpr float outputCalibration = 1.48f;
    static constexpr float safetyCeiling = 3.0f;
    double sampleRate = 44100.0;
    int channelCount = 2;
    float gainAmount = 0.3f, outputLevel = 1.0f, mix = 1.0f, lastFilter01 = 0.5f;
    bool enabled = false;
};
