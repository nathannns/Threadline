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
#include "PedalNodes/JCChorusNode.h"
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
    registry.push_back (std::make_unique<KlonNode> (apvts, qualityState));
    registry.push_back (std::make_unique<TS9Node> (apvts, qualityState));
    registry.push_back (std::make_unique<FangsNode> (apvts, qualityState));
    registry.push_back (std::make_unique<BisonNode> (apvts, qualityState));
    registry.push_back (std::make_unique<GrowlNode> (apvts, qualityState));
    registry.push_back (std::make_unique<TapeNode> (apvts, qualityState));
    registry.push_back (std::make_unique<AmpNode> (apvts, qualityState));
    registry.push_back (std::make_unique<CabUnitNode> (apvts));
    registry.push_back (std::make_unique<TremoloNode> (apvts));
    registry.push_back (std::make_unique<ChorusNode> (apvts));
    registry.push_back (std::make_unique<DimensionChorusNode> (apvts));
    registry.push_back (std::make_unique<DimensionDBBDNode> (apvts));
    registry.push_back (std::make_unique<JCChorusNode> (apvts));
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
    auto parallel = std::make_unique<ParallelNode> (apvts, [this] (const juce::String& id) { return findById (id); });
    parallelNode = parallel.get();
    registry.push_back (std::move (parallel));
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

void PedalChainRunner::handleAsyncUpdate()
{
    const auto latency = requestedLatency.load (std::memory_order_acquire);
    if (latency >= 0 && owningProcessor != nullptr)
        owningProcessor->setLatencySamples (latency);
}

void PedalChainRunner::prepare (const juce::dsp::ProcessSpec& spec)
{
    lastSpec = spec;
    const auto trackingMode = (int) apvts.getRawParameterValue ("ampOversampling")->load();
    const auto renderMode = (int) apvts.getRawParameterValue ("renderOversampling")->load();
    const auto initialMode = offlineRendering.load (std::memory_order_relaxed) ? renderMode : trackingMode;
    qualityState.setEffectiveOversamplingMode (initialMode);
    pendingOversamplingMode = initialMode;
    qualitySwitchPending = false;
    qualityTransitionGain.reset (spec.sampleRate, 0.012);
    qualityTransitionGain.setCurrentAndTargetValue (1.0f);
    for (auto& node : registry)
        node->prepare (spec);

    PedalboardOrder::ensureExists (apvts);
    runtimeOrder.clear();
    targetOrderNodes.clear();
    runtimeOrder.reserve (maxPedals);
    targetOrderNodes.reserve (maxPedals);
    rebuildScratch.reserve (maxPedals);
    stillNeededScratch.reserve (maxPedals);
    targetOrderScratch.ensureStorageAllocated (maxPedals);
    appliedGeneration = pendingGeneration.load() - 1; // force resync below to actually run
    lastReportedLatency.store (-1, std::memory_order_relaxed);
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
    if (newLatency != lastReportedLatency.load (std::memory_order_acquire) && owningProcessor != nullptr)
    {
        owningProcessor->setLatencySamples (newLatency);
        lastReportedLatency.store (newLatency, std::memory_order_release);
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
    rebuildScratch.clear();

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
        rebuildScratch.push_back (node);
    }

    // Anything still running that's no longer in the target order is
    // appended after every target-order node, still fading toward
    // transparent bypass via its own crossfade/internal state machine --
    // see PedalChainRunner.h's class comment. Its position during that
    // few-millisecond tail differs from where it used to sit, but its
    // contribution to the signal is vanishing by construction during that
    // same window, so the brief positional difference is inaudible.
    for (auto* node : runtimeOrder)
        if (std::find (rebuildScratch.begin(), rebuildScratch.end(), node) == rebuildScratch.end())
            rebuildScratch.push_back (node);

    runtimeOrder.swap (rebuildScratch);
}

void PedalChainRunner::processChain (juce::AudioBuffer<float>& buffer)
{
    const auto trackingMode = (int) apvts.getRawParameterValue ("ampOversampling")->load();
    const auto renderMode = (int) apvts.getRawParameterValue ("renderOversampling")->load();
    const auto requestedMode = juce::jlimit (0, 2,
        offlineRendering.load (std::memory_order_relaxed) ? renderMode : trackingMode);
    if (requestedMode != qualityState.getEffectiveOversamplingMode())
    {
        pendingOversamplingMode = requestedMode;
        qualitySwitchPending = true;
        qualityTransitionGain.setTargetValue (0.0f);
    }
    else if (qualitySwitchPending)
    {
        // The user returned to the currently-running mode before the fade
        // reached silence. Cancel the pending switch and come back up.
        qualitySwitchPending = false;
        qualityTransitionGain.setTargetValue (1.0f);
    }

    const auto generation = pendingGeneration.load();
    if (generation != appliedGeneration)
    {
        auto copiedPendingOrder = false;
        {
            const juce::SpinLock::ScopedTryLockType lock (orderLock);
            if (lock.isLocked())
            {
                targetOrderScratch.clearQuick();
                for (int i = 0; i < pending.count; ++i)
                    targetOrderScratch.add (pending.ids[i]);
                copiedPendingOrder = true;
            }
        }
        if (copiedPendingOrder)
        {
            rebuildRuntimeOrder (targetOrderScratch);
            appliedGeneration = generation;
        }
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
    const auto currentOversamplingMode = qualityState.getEffectiveOversamplingMode();
    const auto currentParallelSlotA = static_cast<int> (apvts.getRawParameterValue ("parallelSlotA")->load());
    const auto currentParallelSlotB = static_cast<int> (apvts.getRawParameterValue ("parallelSlotB")->load());
    if (currentOversamplingMode != lastOversamplingMode
        || currentParallelSlotA != lastParallelSlotA
        || currentParallelSlotB != lastParallelSlotB)
    {
        lastOversamplingMode = currentOversamplingMode;
        lastParallelSlotA = currentParallelSlotA;
        lastParallelSlotB = currentParallelSlotB;
        int total = 0;
        for (auto* node : targetOrderNodes)
            if (node == parallelNode || parallelNode == nullptr || ! parallelNode->routesNode (node))
                total += node->getLatencySamples();
        if (total != lastReportedLatency.load (std::memory_order_acquire) && owningProcessor != nullptr)
        {
            requestedLatency.store (total, std::memory_order_release);
            lastReportedLatency.store (total, std::memory_order_release);
            triggerAsyncUpdate();
        }
    }

    // Each node is called exactly once per block. Its own return value
    // decides whether it's still needed next block -- true removal drops it
    // from runtimeOrder the block after it reports settled, so it costs
    // zero CPU from there on, not called again until genuinely re-added.
    stillNeededScratch.clear();
    const auto parallelInTarget = parallelNode != nullptr
        && std::find (targetOrderNodes.begin(), targetOrderNodes.end(), parallelNode) != targetOrderNodes.end();
    for (auto* node : runtimeOrder)
    {
        const auto inTargetOrder = std::find (targetOrderNodes.begin(), targetOrderNodes.end(), node)
                                 != targetOrderNodes.end();
        if (parallelInTarget && node != parallelNode && parallelNode->routesNode (node))
        {
            stillNeededScratch.push_back (node);
            continue;
        }
        const auto keep = node->updateAndProcess (buffer, inTargetOrder);
        if (keep || inTargetOrder)
            stillNeededScratch.push_back (node);
    }
    runtimeOrder.swap (stillNeededScratch);

    // Different oversampling modes have different latency, so directly
    // overlapping their outputs would comb-filter. Fade the complete chain
    // to silence, reset/switch every affected prepared engine at the silent
    // boundary, then fade back up. No allocation, lock, or module prepare()
    // occurs on the audio thread.
    if (qualityTransitionGain.isSmoothing()
        || qualityTransitionGain.getCurrentValue() < 0.99999f)
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto gain = qualityTransitionGain.getNextValue();
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample (channel, sample, buffer.getSample (channel, sample) * gain);
        }
    }

    if (qualitySwitchPending && ! qualityTransitionGain.isSmoothing()
        && qualityTransitionGain.getCurrentValue() <= 0.00001f)
    {
        qualityState.setEffectiveOversamplingMode (pendingOversamplingMode);
        for (auto& node : registry)
            node->oversamplingModeChanged();
        qualitySwitchPending = false;
        qualityTransitionGain.setTargetValue (1.0f);
    }
}
