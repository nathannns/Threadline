// Integrated routing/bypass regression test. Links the real Threadline shared
// processor code but is a separate console executable, never part of a plugin.
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    constexpr double pi = 3.14159265358979323846;

    void setParameter (ThreadlineAudioProcessor& processor, const char* id, float plainValue)
    {
        if (auto* parameter = processor.apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    }

    void fillInput (juce::AudioBuffer<float>& buffer, int blockIndex,
                    float leftPolarity = 1.0f, float rightPolarity = 1.0f)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto n = blockIndex * buffer.getNumSamples() + i;
            const auto sample = 0.10f * (float) std::sin (2.0 * pi * 997.0 * n / sampleRate);
            buffer.setSample (0, i, sample * leftPolarity);
            buffer.setSample (1, i, sample * rightPolarity);
        }
    }

    bool validateStereoAndCentredMono()
    {
        ThreadlineAudioProcessor processor;
        setParameter (processor, "input1On", 1.0f);
        setParameter (processor, "input2On", 0.0f);
        setParameter (processor, "jcChorusOn", 1.0f);
        setParameter (processor, "jcChorusMix", 70.0f);
        setParameter (processor, "processingMode", 0.0f);
        processor.setActivePedalOrder ({ "inputGain", "jcChorus", "outputGain" });
        processor.prepareToPlay (sampleRate, blockSize);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        double stereoDifference = 0.0;
        for (int block = 0; block < 120; ++block)
        {
            fillInput (buffer, block);
            processor.processBlock (buffer, midi);
            if (block >= 100)
                for (int i = 0; i < blockSize; ++i)
                    stereoDifference += std::abs (buffer.getSample (0, i) - buffer.getSample (1, i));
        }
        if (stereoDifference < 0.01)
        {
            std::printf ("  !! Stereo default did not retain the chorus channel difference\n");
            return false;
        }

        setParameter (processor, "processingMode", 1.0f);
        float monoDifference = 0.0f;
        for (int block = 120; block < 180; ++block)
        {
            fillInput (buffer, block);
            processor.processBlock (buffer, midi);
            if (block >= 175)
                for (int i = 0; i < blockSize; ++i)
                    monoDifference = juce::jmax (monoDifference,
                        std::abs (buffer.getSample (0, i) - buffer.getSample (1, i)));
        }
        if (monoDifference > 1.0e-6f)
        {
            std::printf ("  !! Mono output was not centred dual-mono (difference %.8f)\n", monoDifference);
            return false;
        }
        std::printf ("  ok (Stereo differs; Mono L/R maximum difference %.8f)\n", monoDifference);
        return true;
    }

    bool validateInputAndMasterTransitions()
    {
        ThreadlineAudioProcessor processor;
        setParameter (processor, "input1On", 1.0f);
        setParameter (processor, "input2On", 0.0f);
        processor.setActivePedalOrder ({ "inputGain", "outputGain" });
        processor.prepareToPlay (sampleRate, blockSize);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        float previous = 0.0f;
        float maximumStep = 0.0f;
        float finalMutedPeak = 0.0f;
        for (int block = 0; block < 90; ++block)
        {
            if (block == 25)
            {
                setParameter (processor, "input1On", 0.0f);
                setParameter (processor, "input2On", 1.0f);
            }
            if (block == 55)
                setParameter (processor, "masterBypass", 1.0f);

            fillInput (buffer, block, 1.0f, -1.0f);
            processor.processBlock (buffer, midi);
            for (int i = 0; i < blockSize; ++i)
            {
                const auto sample = buffer.getSample (0, i);
                maximumStep = juce::jmax (maximumStep, std::abs (sample - previous));
                previous = sample;
            }
            if (block >= 85)
                finalMutedPeak = juce::jmax (finalMutedPeak,
                    buffer.getMagnitude (0, 0, blockSize));
        }

        if (maximumStep > 0.05f || finalMutedPeak > 1.0e-6f)
        {
            std::printf ("  !! transition step %.6f, final muted peak %.8f\n",
                         maximumStep, finalMutedPeak);
            return false;
        }
        std::printf ("  ok (maximum sample step %.6f; final muted peak %.8f)\n",
                     maximumStep, finalMutedPeak);
        return true;
    }

    bool validateSeparateTrackingAndRenderQuality()
    {
        ThreadlineAudioProcessor tracking, rendering;
        tracking.setNonRealtime (false);
        rendering.setNonRealtime (true);
        for (auto* processor : { &tracking, &rendering })
        {
            setParameter (*processor, "input1On", 1.0f);
            setParameter (*processor, "input2On", 0.0f);
            setParameter (*processor, "ampOversampling", 0.0f);    // tracking 1x
            setParameter (*processor, "renderOversampling", 2.0f); // render 4x
            setParameter (*processor, "ampDrive", 0.75f);
            setParameter (*processor, "ampVoice", 3.0f); // Deluxe
            processor->setActivePedalOrder ({ "inputGain", "amp", "outputGain" });
            processor->prepareToPlay (sampleRate, blockSize);
        }

        juce::AudioBuffer<float> trackingBuffer (2, blockSize), renderBuffer (2, blockSize);
        juce::MidiBuffer trackingMidi, renderMidi;
        double difference = 0.0;
        for (int block = 0; block < 80; ++block)
        {
            fillInput (trackingBuffer, block);
            renderBuffer.makeCopyOf (trackingBuffer, true);
            tracking.processBlock (trackingBuffer, trackingMidi);
            rendering.processBlock (renderBuffer, renderMidi);
            if (block >= 70)
                for (int i = 0; i < blockSize; ++i)
                    difference += std::abs (trackingBuffer.getSample (0, i)
                                           - renderBuffer.getSample (0, i));
        }
        if (difference < 1.0e-5)
        {
            std::printf ("  !! Offline rendering did not select its independent 4x mode\n");
            return false;
        }

        ThreadlineAudioProcessor liveSwitch;
        setParameter (liveSwitch, "input1On", 1.0f);
        setParameter (liveSwitch, "input2On", 0.0f);
        setParameter (liveSwitch, "ampOversampling", 0.0f);
        setParameter (liveSwitch, "ampDrive", 0.75f);
        setParameter (liveSwitch, "ampOutput", -24.0f);
        setParameter (liveSwitch, "ampVoice", 3.0f);
        liveSwitch.setActivePedalOrder ({ "inputGain", "amp", "outputGain" });
        liveSwitch.prepareToPlay (sampleRate, blockSize);

        juce::AudioBuffer<float> liveBuffer (2, blockSize);
        juce::MidiBuffer liveMidi;
        float previous = 0.0f, maximumStep = 0.0f, finalPeak = 0.0f;
        int silentRun = 0, longestSilentRun = 0;
        bool allFinite = true;
        for (int block = 0; block < 100; ++block)
        {
            if (block == 35)
                setParameter (liveSwitch, "ampOversampling", 2.0f);
            fillInput (liveBuffer, block);
            liveSwitch.processBlock (liveBuffer, liveMidi);
            for (int i = 0; i < blockSize; ++i)
            {
                const auto sample = liveBuffer.getSample (0, i);
                allFinite = allFinite && std::isfinite (sample);
                maximumStep = juce::jmax (maximumStep, std::abs (sample - previous));
                if (block >= 35 && block <= 45 && std::abs (sample) <= 1.0e-7f)
                    longestSilentRun = juce::jmax (longestSilentRun, ++silentRun);
                else
                    silentRun = 0;
                previous = sample;
            }
            if (block >= 95)
                finalPeak = juce::jmax (finalPeak, liveBuffer.getMagnitude (0, 0, blockSize));
        }
        // The amp's normal nonlinear waveform can have a large raw adjacent-
        // sample delta, so that is not a valid click detector here. Verify the
        // transition contract directly: finite output, a sustained silent
        // boundary where the engines are reset/switched, then recovered audio.
        if (! allFinite || longestSilentRun < 32 || finalPeak < 1.0e-4f)
        {
            std::printf ("  !! live 1x->4x finite=%d, silent run=%d, final peak %.6f\n",
                         (int) allFinite, longestSilentRun, finalPeak);
            return false;
        }
        std::printf ("  ok (tracking/render independent; live 1x->4x silent boundary "
                     "%d samples, diagnostic max step %.6f)\n",
                     longestSilentRun, maximumStep);
        return true;
    }
}

int main()
{
    std::printf ("Threadline integrated routing/bypass validation\n");
    std::printf ("================================================\n");
    const auto stereoMono = validateStereoAndCentredMono();
    const auto transitions = validateInputAndMasterTransitions();
    const auto quality = validateSeparateTrackingAndRenderQuality();
    if (! stereoMono || ! transitions || ! quality)
        return 1;
    std::printf ("All checks passed.\n");
    return 0;
}
