#pragma once

#include <JuceHeader.h>
#include "DSP/NoiseGateModule.h"
#include "DSP/CompressorModule.h"
#include "DSP/KlonModule.h"
#include "DSP/TS9Module.h"
#include "DSP/AmpModule.h"
#include "DSP/CabModule.h"
#include "DSP/ChorusModule.h"
#include "DSP/EchoModule.h"
#include "DSP/HallRoomReverbModule.h"
#include "DSP/GraphicEQModule.h"
#include "DSP/TremoloModule.h"
#include "DSP/PeakLevel.h"
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
    float getInputLevel() const noexcept { return inputLevel.getPeak(); }
    float getOutputLevel() const noexcept { return outputLevel.getPeak(); }
    float getCompressorGainReductionDb() const noexcept { return compressor.getCurrentGainReductionDb(); }

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager { apvts };

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float p (const char* paramId) const { return apvts.getRawParameterValue (paramId)->load(); }
    bool pBool (const char* paramId) const { return p (paramId) > 0.5f; }

    // Chain order: NoiseGate -> Input Gain -> Input Meter -> Compressor ->
    // Klon -> TS9 -> Amp -> Cab (IR) -> Tremolo -> Chorus -> Echo (Delay) ->
    // Reverb (Hall/Room) -> 9-Band EQ -> Output Gain -> Output Meter.
    NoiseGateModule noiseGate;
    CompressorModule compressor;
    KlonModule klon;
    TS9Module ts9;
    // Prepared up front so changing quality never allocates on the audio thread.
    std::array<AmpModule, 3> amps;
    CabModule cab;
    TremoloModule tremolo;
    ChorusModule chorus;
    EchoModule echo;
    HallRoomReverbModule hallRoomReverb;
    GraphicEQModule graphicEQ;

    PeakLevel inputLevel, outputLevel;

    double currentSampleRate = 44100.0;
    int lastCabIRSelection = -1;
    bool tremoloWasActive = false, chorusWasActive = false, echoWasActive = false;
    double cachedTempoBpm = -1.0;
    int cachedEchoDivision = -1;
    float cachedSyncedEchoMs = 375.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThreadlineAudioProcessor)
};
