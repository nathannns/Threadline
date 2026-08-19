#pragma once

#include <JuceHeader.h>
#include "PedalNode.h"
#include "PedalboardOrder.h"
#include "PeakLevel.h"
#include "PedalNodes/CompressorNode.h"

// Owns every PedalNode and sequences them each block according to the
// user's chosen pedalboard order. Reordering is published from the message
// thread (preset load, or the future pedalboard UI's drag/add/remove) via a
// small SpinLock-guarded pending buffer + generation counter; the audio
// thread only ever copies that tiny buffer, never touches apvts.state's
// tree structure directly. See PedalNode.h and PedalboardOrder.h for the
// per-node and persistence halves of this design.
class PedalChainRunner
{
public:
    explicit PedalChainRunner (juce::AudioProcessorValueTreeState& apvtsToUse);

    void prepare (const juce::dsp::ProcessSpec& spec);
    void processChain (juce::AudioBuffer<float>& buffer);

    // For master-bypass dry passthrough: the meters still tap the (dry)
    // signal even though processChain() itself isn't called.
    void updateMetersOnBypass (const juce::AudioBuffer<float>& buffer)
    {
        inputLevel.updateFrom (buffer);
        outputLevel.updateFrom (buffer);
    }

    // Called by the AudioProcessor after every apvts.replaceState() (preset
    // load, host state restore): publishes whatever order is now persisted
    // on apvts.state to the audio thread, and updates the reported latency
    // if it changed.
    void resyncFromPersistedOrder();

    // For the future pedalboard UI: persists `orderedIds` to apvts.state,
    // publishes it to the audio thread, and updates reported latency
    // synchronously (message-thread only -- never call from the audio
    // thread). Invalid/unknown ids are silently dropped; ids present in the
    // registry but absent from `orderedIds` are excluded (removed).
    void setActivePedalOrder (const juce::StringArray& orderedIds);
    juce::StringArray getActivePedalOrder() const { return PedalboardOrder::readOrder (apvts.state); }
    static const juce::StringArray& getAllPedalIds() { return PedalboardOrder::allIdsInDefaultOrder(); }

    float getCompressorGainReductionDb() const noexcept
    {
        return compressorNode != nullptr ? compressorNode->getCurrentGainReductionDb() : 0.0f;
    }

    // Owned here (rather than by AudioProcessor) since they're tightly
    // coupled to InputGainNode/OutputGainNode, which tap them at the exact
    // point Input/Output Gain applies.
    float getInputLevel() const noexcept { return inputLevel.getPeak(); }
    float getOutputLevel() const noexcept { return outputLevel.getPeak(); }

    juce::AudioProcessor* owningProcessor = nullptr; // set by AudioProcessor, used for setLatencySamples()

private:
    int computeLatencyFor (const juce::StringArray& orderedIds) const;
    void publishOrder (const juce::StringArray& orderedIds);
    void rebuildRuntimeOrder (const juce::StringArray& targetOrder);
    PedalNode* findById (const juce::String& id) const;

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<std::unique_ptr<PedalNode>> registry;
    CompressorNode* compressorNode = nullptr; // typed alias into registry, for the editor's meter
    PeakLevel inputLevel, outputLevel;

    // Audio-thread-only state.
    std::vector<PedalNode*> runtimeOrder;
    std::vector<PedalNode*> targetOrderNodes;
    int appliedGeneration = -1;
    int lastOversamplingMode = -1; // audio-thread-only; see processChain()'s own comment
    juce::dsp::ProcessSpec lastSpec {};

    // Cross-thread handoff.
    static constexpr int maxPedals = 32;
    struct PendingOrder
    {
        int count = 0;
        juce::String ids[maxPedals];
    };
    juce::SpinLock orderLock;
    PendingOrder pending;
    std::atomic<int> pendingGeneration { 0 };
    int lastReportedLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalChainRunner)
};
