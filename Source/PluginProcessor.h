#pragma once

#include <JuceHeader.h>
#include "DSP/PedalChainRunner.h"
#include "PresetManager.h"

class ThreadlineAudioProcessor : public juce::AudioProcessor
{
public:
    ThreadlineAudioProcessor();
    ~ThreadlineAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Threadline"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    // Meters — editor polls these on a timer.
    float getInputLevel() const noexcept { return chainRunner.getInputLevel(); }
    float getRawInputLevel() const noexcept { return rawInputPeak.load (std::memory_order_relaxed); }
    float getOutputLevel() const noexcept { return chainRunner.getOutputLevel(); }
    float getCompressorGainReductionDb() const noexcept { return chainRunner.getCompressorGainReductionDb(); }

    // Pedalboard reorder API, for the future pedalboard UI (and usable today
    // via host automation / direct calls for the current 4-page UI).
    const juce::StringArray& getAllPedalIds() const { return PedalChainRunner::getAllPedalIds(); }
    juce::StringArray getActivePedalOrder() const { return chainRunner.getActivePedalOrder(); }
    void setActivePedalOrder (const juce::StringArray& orderedIds) { chainRunner.setActivePedalOrder (orderedIds); }

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager { apvts };

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float p (const char* paramId) const { return apvts.getRawParameterValue (paramId)->load(); }
    bool pBool (const char* paramId) const { return p (paramId) > 0.5f; }

    // Owns and sequences every processing stage per the user's chosen
    // pedalboard order -- see Source/DSP/PedalChainRunner.h/.cpp and
    // Source/DSP/PedalNode.h/PedalNodes/*.h. Default order (pre-pedalboard-
    // feature / fresh install) matches the plugin's original fixed chain:
    // NoiseGate -> Input Gain -> Compressor -> Klon -> TS9 -> Amp ->
    // Cab (IR) -> Tremolo -> Chorus (July) -> Delay (Plexer or Copier) ->
    // Reverb (Hall/Room) -> 9-Band EQ -> Output Gain.
    PedalChainRunner chainRunner { apvts };
    juce::SmoothedValue<float> masterOutput;
    juce::SmoothedValue<float> input1Mix, input2Mix, monoOutputMix;
    std::atomic<float> rawInputPeak { 0.0f };

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThreadlineAudioProcessor)
};
