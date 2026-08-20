#include "PedalChainRunner.h"
#include "PedalboardOrder.h"
#include "PedalNodes/NoiseGateNode.h"
#include "PedalNodes/InputGainNode.h"
#include "PedalNodes/LowDynamicNode.h"
#include "PedalNodes/KlonNode.h"
#include "PedalNodes/TS9Node.h"
#include "PedalNodes/FangsNode.h"
#include "PedalNodes/BisonNode.h"
#include "PedalNodes/GrowlNode.h"
#include "PedalNodes/TapeNode.h"
#include "PedalNodes/AmpNode.h"
#include "PedalNodes/CabUnitNode.h"
#include "PedalNodes/TremoloNode.h"
#include "PedalNodes/ChorusNode.h"
#include "PedalNodes/DimensionChorusNode.h"
#include "PedalNodes/DimensionDBBDNode.h"
#include "PedalNodes/DelayNode.h"
#include "PedalNodes/SpaceEchoNode.h"
#include "PedalNodes/ReverbNode.h"
#include "PedalNodes/SpringNode.h"
#include "PedalNodes/ChannelEQNode.h"
#include "PedalNodes/DeskNode.h"
#include "PedalNodes/GraphicEQNode.h"
#include "PedalNodes/OutputGainNode.h"
#include "PedalNodes/ParallelNode.h"

PedalChainRunner::PedalChainRunner (juce::AudioProcessorValueTreeState& apvtsToUse) : apvts (apvtsToUse)
{
    registry.push_back (std::make_unique<NoiseGateNode> (apvts));
    registry.push_back (std::make_unique<InputGainNode> (apvts, inputLevel));
    auto compressor = std::make_unique<CompressorNode> (apvts);
    compressorNode = compressor.get();
    registry.push_back (std::move (compressor));
    registry.push_back (std::make_unique<LowDynamicNode> (apvts));
    registry.push_back (std::make_unique<KlonNode> (apvts));
    registry.push_back (std::make_unique<TS9Node> (apvts));
    registry.push_back (std::make_unique<FangsNode> (apvts));
    registry.push_back (std::make_unique<BisonNode> (apvts));
    registry.push_back (std::make_unique<GrowlNode> (apvts));
    registry.push_back (std::make_unique<TapeNode> (apvts));
    registry.push_back (std::make_unique<AmpNode> (apvts));
    registry.push_back (std::make_unique<CabUnitNode> (apvts));
    registry.push_back (std::make_unique<TremoloNode> (apvts));
    registry.push_back (std::make_unique<ChorusNode> (apvts));
    registry.push_back (std::make_unique<DimensionChorusNode> (apvts));
    registry.push_back (std::make_unique<DimensionDBBDNode> (apvts));
    registry.push_back (std::make_unique<DelayNode> (apvts));
    registry.push_back (std::make_unique<SpaceEchoNode> (apvts));
    registry.push_back (std::make_unique<ReverbNode> (apvts));
    registry.push_back (std::make_unique<SpringNode> (apvts));
    registry.push_back (std::make_unique<ChannelEQNode> (apvts));
    registry.push_back (std::make_unique<DeskNode> (apvts));
    registry.push_back (std::make_unique<GraphicEQNode> (apvts));
    registry.push_back (std::make_unique<OutputGainNode> (apvts, outputLevel));
    // Constructed last so its resolver lambda can look up any other node in
    // `registry` by id at call time -- findById() searches the whole vector
    // regardless of a node's own position within it, so this ordering only
    // matters for readability, not correctness.
    registry.push_back (std::make_unique<ParallelNode> (apvts, [this] (const juce::String& id) { return findById (id); }));
}

PedalNode* PedalChainRunner::findById (const juce::String& id) const
{
    for (auto& node : registry)
        if (node->getId().toString() == id)
            return node.get();
    return nullptr;
}

int PedalChainRunner::computeLatencyFor (const juce::StringArray& orderedIds) const
{
    int total = 0;
    for (auto& id : orderedIds)
        if (auto* node = findById (id))
            total += node->getLatencySamples();
    return total;
}

void PedalChainRunner::prepare (const juce::dsp::ProcessSpec& spec)
{
    lastSpec = spec;
    for (auto& node : registry)
        node->prepare (spec);

    PedalboardOrder::ensureExists (apvts);
    runtimeOrder.clear();
    targetOrderNodes.clear();
    appliedGeneration = pendingGeneration.load() - 1; // force resync below to actually run
    lastReportedLatency = -1;
    resyncFromPersistedOrder();
}

void PedalChainRunner::publishOrder (const juce::StringArray& orderedIds)
{
    {
        const juce::SpinLock::ScopedLockType lock (orderLock);
        pending.count = juce::jmin ((int) orderedIds.size(), maxPedals);
        for (int i = 0; i < pending.count; ++i)
            pending.ids[i] = orderedIds[i];
    }
    pendingGeneration.fetch_add (1);

    const auto newLatency = computeLatencyFor (orderedIds);
    if (newLatency != lastReportedLatency && owningProcessor != nullptr)
    {
        owningProcessor->setLatencySamples (newLatency);
        lastReportedLatency = newLatency;
    }
}

void PedalChainRunner::resyncFromPersistedOrder()
{
    publishOrder (PedalboardOrder::readOrder (apvts.state));
}

void PedalChainRunner::setActivePedalOrder (const juce::StringArray& orderedIds)
{
    juce::StringArray validated;
    for (auto& id : orderedIds)
        if (findById (id) != nullptr && ! validated.contains (id))
            validated.add (id);

    PedalboardOrder::setOrder (apvts, validated);
    publishOrder (validated);
}

void PedalChainRunner::rebuildRuntimeOrder (const juce::StringArray& targetOrder)
{
    targetOrderNodes.clear();
    std::vector<PedalNode*> newRuntimeOrder;

    // Target-order nodes first, in the new order -- reusing the existing
    // instance (and its state) if it's already in runtimeOrder, otherwise a
    // genuine re-insertion after true removal, which gets a fresh reset()
    // before it's ever called again.
    for (auto& id : targetOrder)
    {
        auto* node = findById (id);
        if (node == nullptr)
            continue;
        targetOrderNodes.push_back (node);

        const auto alreadyRunning = std::find (runtimeOrder.begin(), runtimeOrder.end(), node) != runtimeOrder.end();
        if (! alreadyRunning)
            node->reset();
        newRuntimeOrder.push_back (node);
    }

    // Anything still running that's no longer in the target order is
    // appended after every target-order node, still fading toward
    // transparent bypass via its own crossfade/internal state machine --
    // see PedalChainRunner.h's class comment. Its position during that
    // few-millisecond tail differs from where it used to sit, but its
    // contribution to the signal is vanishing by construction during that
    // same window, so the brief positional difference is inaudible.
    for (auto* node : runtimeOrder)
        if (std::find (newRuntimeOrder.begin(), newRuntimeOrder.end(), node) == newRuntimeOrder.end())
            newRuntimeOrder.push_back (node);

    runtimeOrder = std::move (newRuntimeOrder);
}

void PedalChainRunner::processChain (juce::AudioBuffer<float>& buffer)
{
    const auto generation = pendingGeneration.load();
    if (generation != appliedGeneration)
    {
        juce::StringArray targetOrder;
        {
            const juce::SpinLock::ScopedLockType lock (orderLock);
            for (int i = 0; i < pending.count; ++i)
                targetOrder.add (pending.ids[i]);
        }
        rebuildRuntimeOrder (targetOrder);
        appliedGeneration = generation;
    }

    // Several nodes' own getLatencySamples() now reports whichever
    // oversampling instance is actually live (Klon/TS9/Amp/Fangs/Bison/
    // Growl/Tape), rather than a value pinned to the 4x instance regardless
    // of the real setting -- an audit-caught accuracy gap that only ever
    // showed up when the Options panel's Amp Oversampling dropdown wasn't
    // left at its 4x default. That only matters if this class actually
    // re-publishes latency when the setting changes, which nothing did
    // before: publishOrder() (the only path that calls
    // owningProcessor->setLatencySamples()) only ever runs on a pedal-order
    // change, not a plain parameter change. Reading the param here is
    // audio-thread-safe (the same atomic load every PedalNode's p()/pBool()
    // helper already does); only touching apvts.state's ValueTree itself
    // would need to stay message-thread-only, which this doesn't do.
    const auto currentOversamplingMode = (int) apvts.getRawParameterValue ("ampOversampling")->load();
    if (currentOversamplingMode != lastOversamplingMode)
    {
        lastOversamplingMode = currentOversamplingMode;
        int total = 0;
        for (auto* node : targetOrderNodes)
            total += node->getLatencySamples();
        if (total != lastReportedLatency && owningProcessor != nullptr)
        {
            owningProcessor->setLatencySamples (total);
            lastReportedLatency = total;
        }
    }

    // Each node is called exactly once per block. Its own return value
    // decides whether it's still needed next block -- true removal drops it
    // from runtimeOrder the block after it reports settled, so it costs
    // zero CPU from there on, not called again until genuinely re-added.
    std::vector<PedalNode*> stillNeeded;
    stillNeeded.reserve (runtimeOrder.size());
    for (auto* node : runtimeOrder)
    {
        const auto inTargetOrder = std::find (targetOrderNodes.begin(), targetOrderNodes.end(), node)
                                 != targetOrderNodes.end();
        const auto keep = node->updateAndProcess (buffer, inTargetOrder);
        if (keep || inTargetOrder)
            stillNeeded.push_back (node);
    }
    runtimeOrder = std::move (stillNeeded);
}
