#pragma once

#include <JuceHeader.h>

// The pedalboard's order-list state: a small <PEDALBOARD><SLOT id="..."/>...
// </PEDALBOARD> child ValueTree living directly under apvts.state. This
// rides through the existing getStateInformation/setStateInformation and
// PresetManager save/load paths with zero new plumbing, since those already
// serialize apvts.state wholesale (see PluginProcessor.cpp/PresetManager.h).
namespace PedalboardOrder
{
    // NEVER reorder/rename/remove these tokens after ship -- persisted by
    // string in every saved session and preset, the same durability
    // contract as an APVTS parameter id. New pedal types append here.
    inline const juce::StringArray& allIdsInDefaultOrder()
    {
        static const juce::StringArray ids {
            "noiseGate", "inputGain", "compressor", "lowDynamic", "klon", "ts9",
            "fangs", "bison", "growl", "tape", "amp",
            "cab", "tremolo", "chorus", "dimChorus", "dimBbd", "delay", "spaceEcho", "reverb",
            "spring", "channelEQ", "desk", "parallel", "eq", "outputGain"
        };
        return ids;
    }

    // The pedal ids selectable into the Parallel Box's Slot A / Slot B choice
    // params (see ParallelNode.h / ParallelTile) -- every reorderable id
    // except the 3 permanently-pinned utility stages and "parallel" itself
    // (a box can't contain itself). This is also the fixed index order for
    // the parallelSlotA/parallelSlotB AudioParameterChoice params -- NEVER
    // reorder/remove entries after ship, append only, same durability
    // contract as allIdsInDefaultOrder() above. Index 0 in the *param's*
    // choice list is always a synthetic "None" prepended by the caller, so
    // param choice index N (N>=1) maps to parallelSlotChoiceIds()[N-1].
    inline const juce::StringArray& parallelSlotChoiceIds()
    {
        static const juce::StringArray ids {
            "compressor", "lowDynamic", "klon", "ts9", "fangs", "bison", "growl",
            "tape", "amp", "cab", "tremolo", "chorus", "dimChorus", "delay",
            "spaceEcho", "reverb", "spring", "channelEQ", "desk", "eq", "dimBbd"
        };
        return ids;
    }

    inline juce::ValueTree buildOrderTree (const juce::StringArray& orderedIds)
    {
        juce::ValueTree tree ("PEDALBOARD");
        tree.setProperty ("version", 1, nullptr);
        for (auto& id : orderedIds)
        {
            juce::ValueTree slot ("SLOT");
            slot.setProperty ("id", id, nullptr);
            tree.addChild (slot, -1, nullptr);
        }
        return tree;
    }

    // Reproduces the plugin's original fixed chain exactly, respecting the
    // deprecated odOrder param's stored value for Klon/TS9's relative
    // position -- what pre-pedalboard sessions/presets (no PEDALBOARD child
    // at all) resolve to, so they sound identical after upgrading.
    inline juce::ValueTree buildDefaultOrderTree (juce::AudioProcessorValueTreeState& apvts)
    {
        const auto breakerFirst = apvts.getRawParameterValue ("odOrder")->load() > 0.5f;
        juce::StringArray ids { "noiseGate", "inputGain", "compressor", "lowDynamic" };
        ids.add (breakerFirst ? "ts9" : "klon");
        ids.add (breakerFirst ? "klon" : "ts9");
        // Tape/Spring/Dimension Chorus/Space Echo/Redface/Desk all default
        // off like every other stomp here, but still occupy a sensible
        // default chain position: Low Dynamic alongside Compressor (both
        // dynamics utilities), Tape alongside the other pre-amp drives,
        // Dimension Chorus right after July as a second independent
        // modulation pedal, Space Echo right after Delay as a third
        // independent delay pedal, Spring alongside Reverb as a second
        // independent reverb pedal, Redface right before the graphic EQ as
        // a second, differently-shaped EQ stage, Desk right after that as
        // a final coloration stage before the output. Fangs/Bison/Growl
        // (all default off) sit right after Bull/Breaker as three more
        // gain-stage options in the same neighbourhood of the chain.
        ids.addArray ({ "fangs", "bison", "growl", "tape", "amp", "cab", "tremolo", "chorus", "dimChorus", "dimBbd",
                        "delay", "spaceEcho", "reverb", "spring", "channelEQ", "desk", "parallel",
                        "eq", "outputGain" });
        return buildOrderTree (ids);
    }

    inline juce::StringArray readOrder (const juce::ValueTree& apvtsState)
    {
        juce::StringArray ids;
        const auto board = apvtsState.getChildWithName ("PEDALBOARD");
        for (int i = 0; i < board.getNumChildren(); ++i)
            ids.add (board.getChild (i).getProperty ("id").toString());
        return ids;
    }

    // Idempotent -- no-ops if a PEDALBOARD child already exists. Call after
    // every apvts.replaceState(): setStateInformation, PresetManager::
    // loadPreset, and PresetManager's own construction.
    inline void ensureExists (juce::AudioProcessorValueTreeState& apvts)
    {
        if (! apvts.state.getChildWithName ("PEDALBOARD").isValid())
            apvts.state.addChild (buildDefaultOrderTree (apvts), -1, nullptr);
    }

    // Replaces any existing PEDALBOARD child on apvts.state with one built
    // from `orderedIds` -- used when a preset needs to explicitly override
    // the inherited default order (e.g. a factory preset authored with
    // Breaker running before Bull).
    inline void setOrder (juce::AudioProcessorValueTreeState& apvts, const juce::StringArray& orderedIds)
    {
        const auto existing = apvts.state.getChildWithName ("PEDALBOARD");
        if (existing.isValid())
            apvts.state.removeChild (existing, nullptr);
        apvts.state.addChild (buildOrderTree (orderedIds), -1, nullptr);
    }
}
