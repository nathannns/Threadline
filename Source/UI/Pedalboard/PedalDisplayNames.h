#pragma once

#include <JuceHeader.h>

// Friendly on-screen names for every pedal id -- shared by PedalTileFactory
// (top-level strip tiles) and PedalTiles.h's ParallelTile (its own nested
// Slot A/Slot B picker combos), so both surfaces always agree on naming.
namespace PedalDisplayNames
{
    inline juce::String displayNameFor (const juce::String& id)
    {
        static const std::map<juce::String, juce::String> names {
            { "noiseGate", "Gate" }, { "inputGain", "Input" }, { "compressor", "Comp" },
            { "lowDynamic", "Dynamix" },
            { "klon", "Bull" }, { "ts9", "Breaker" },
            { "fangs", "Fangs" }, { "bison", "Bison" }, { "growl", "Growl" },
            { "tape", "Tape" }, { "amp", "Amp" }, { "cab", "Cab" },
            { "tremolo", "Tremolo" }, { "chorus", "July" }, { "dimChorus", "Ensemble" },
            { "dimBbd", "Dimension" },
            { "delay", "Delay" }, { "spaceEcho", "Satellite - 201" },
            { "reverb", "Reverb" }, { "spring", "Spring" }, { "channelEQ", "Redface" },
            { "desk", "Desk" }, { "parallel", "Parallel" },
            { "eq", "EQ" }, { "outputGain", "Output" }
        };
        const auto it = names.find (id);
        return it != names.end() ? it->second : id;
    }
}
