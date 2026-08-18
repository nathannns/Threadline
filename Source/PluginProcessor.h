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
#include "DSP/CarbonCopyModule.h"
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
    // Klon -> TS9 -> Amp -> Cab (IR) -> Tremolo -> Chorus (July) ->
    // Delay (Plexer or Copier, user-selectable) -> Reverb (Hall/Room) ->
    // 9-Band EQ -> Output Gain -> Output Meter.
    NoiseGateModule noiseGate;
    CompressorModule compressor;
    // 3 fully-prepared instances, one per oversampling mode, same
    // hot-switchable pattern as amps below -- PluginProcessor just picks
    // which one to run each block based on the corresponding parameter.
    std::array<KlonModule, 3> klons;
    std::array<TS9Module, 3> ts9s;
    // Prepared up front so changing quality never allocates on the audio thread.
    std::array<AmpModule, 3> amps;
    // Two IR slots processed in parallel from the same dry signal (like two
    // mics on one cab, not two cabs chained in series), blended by cabBlend.
    CabModule cabA, cabB;
    TremoloModule tremolo;
    ChorusModule chorus;
    // Two delay engines sharing one Delay section and one on/off toggle;
    // delayModel picks which one is actually in the signal path.
    EchoModule echo;
    CarbonCopyModule copier;
    HallRoomReverbModule hallRoomReverb;
    GraphicEQModule graphicEQ;

    PeakLevel inputLevel, outputLevel;

    double currentSampleRate = 44100.0;
    int lastCabAIRSelection = -1, lastCabBIRSelection = -1;
    bool tremoloWasActive = false, chorusWasActive = false, echoWasActive = false, copierWasActive = false;

    // Comp/Klon/TS9 used to hard-cut instantly when toggled (no click-free
    // fade, unlike Tremolo/July/Echo above) -- audible as a real pop/click
    // on toggle, since the output can jump between "processed" and "raw"
    // values with no transition. Same fix, applied one level up: rather
    // than adding fade-aware internals to three DSP modules with three
    // different architectures (touching the WDF clippers' carefully-tuned
    // internals wasn't worth the risk), each stage's dry input is snapshotted
    // before it runs, and the block-level output crossfades from that dry
    // snapshot to the stage's normal wet output over ~15ms whenever its
    // on/off state changes -- the stage itself is untouched.
    bool compWasActive = false, klonWasActive = false, ts9WasActive = false;
    juce::SmoothedValue<float> compWetAmount, klonWetAmount, ts9WetAmount;
    juce::AudioBuffer<float> dryScratchBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThreadlineAudioProcessor)
};
