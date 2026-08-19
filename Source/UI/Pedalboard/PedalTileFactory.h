#pragma once

#include "PedalTiles.h"
#include "PedalDisplayNames.h"
#include "../../PluginProcessor.h"

// id -> tile registry. The ids themselves come from
// PedalboardOrder::allIdsInDefaultOrder() (Source/DSP/PedalboardOrder.h);
// this is the one place that knows how to turn each of those raw string
// tokens into an actual on-screen tile.
namespace PedalTileFactory
{
    inline juce::String displayNameFor (const juce::String& id)
    {
        return PedalDisplayNames::displayNameFor (id);
    }

    inline std::unique_ptr<PedalTileComponent> createTile (const juce::String& id, ThreadlineAudioProcessor& processor)
    {
        auto& apvts = processor.apvts;

        if (id == "compressor")
            return std::make_unique<GenericKnobsTile> (apvts, id, "Comp", "compOn",
                std::vector<std::pair<juce::String, juce::String>> {
                    { "compThreshold", "Comp" }, { "compRatio", "Attack" }, { "compAttack", "Tilt" },
                    { "compRelease", "Mid" }, { "compMakeup", "Level" } });

        // Single-Ratio expander/upward-compressor acting only below
        // Threshold -- see LowDynamicModule.h.
        if (id == "lowDynamic")
            return std::make_unique<LowDynamicTile> (apvts);

        // Oversampling for both overdrives now follows the global "Amp
        // Oversampling" setting in the Options panel (see KlonNode/TS9Node)
        // rather than each having its own -- no per-tile OS combo anymore.
        if (id == "klon")
            return std::make_unique<GenericKnobsTile> (apvts, id, "Bull", "klonOn",
                std::vector<std::pair<juce::String, juce::String>> {
                    { "klonGain", "Gain" }, { "klonTreble", "Treble" }, { "klonLevel", "Level" } });

        if (id == "ts9")
            return std::make_unique<GenericKnobsTile> (apvts, id, "Breaker", "ts9On",
                std::vector<std::pair<juce::String, juce::String>> {
                    { "ts9Drive", "Drive" }, { "ts9Tone", "Tone" }, { "ts9Level", "Level" } },
                std::vector<std::tuple<juce::String, juce::String, juce::StringArray>> {
                    { "ts9Variant", "Variant", juce::StringArray { "TS9", "TS808", "TS10" } } });

        // Original op-amp diode-feedback fuzz/distortion (RAT/Distortion+
        // archetype) -- see FangsModule.h. Oversampling follows the global
        // setting like Bull/Breaker.
        if (id == "fangs")
            return std::make_unique<GenericKnobsTile> (apvts, id, "Fangs", "fangsOn",
                std::vector<std::pair<juce::String, juce::String>> {
                    { "fangsGain", "Gain" }, { "fangsFilter", "Filter" },
                    { "fangsLevel", "Level" }, { "fangsMix", "Mix" } });

        // Original two-stage cascaded fuzz (Big Muff Pi archetype) -- see
        // BisonModule.h. Oversampling follows the global setting too.
        if (id == "bison")
            return std::make_unique<GenericKnobsTile> (apvts, id, "Bison", "bisonOn",
                std::vector<std::pair<juce::String, juce::String>> {
                    { "bisonSustain", "Sustain" }, { "bisonTone", "Tone" },
                    { "bisonLevel", "Level" }, { "bisonMix", "Mix" } });

        // Original germanium two-transistor fuzz (Fuzz Face archetype),
        // built around a genuine front-end input-loading solve -- see
        // GrowlModule.h. Oversampling follows the global setting too.
        if (id == "growl")
            return std::make_unique<GenericKnobsTile> (apvts, id, "Growl", "growlOn",
                std::vector<std::pair<juce::String, juce::String>> {
                    { "growlBias", "Bias" }, { "growlFuzz", "Fuzz" },
                    { "growlLevel", "Level" }, { "growlMix", "Mix" } });

        // Ported from Rockalizer -- tape saturation/compression, oversampling
        // follows the global setting like Bull/Breaker (see TapeNode).
        if (id == "tape")
            return std::make_unique<GenericKnobsTile> (apvts, id, "Tape", "tapeOn",
                std::vector<std::pair<juce::String, juce::String>> {
                    { "tapeDrive", "Drive" }, { "tapeCompression", "Comp" }, { "tapeTone", "Tone" },
                    { "tapeAge", "Age" }, { "tapeMix", "Mix" } },
                std::vector<std::tuple<juce::String, juce::String, juce::StringArray>> {
                    { "tapeType", "Type", juce::StringArray { "Studio", "Cassette" } } });

        if (id == "amp")
            return std::make_unique<AmpTile> (apvts);

        if (id == "cab")
            return std::make_unique<CabTile> (apvts);

        if (id == "tremolo")
            return std::make_unique<GenericKnobsTile> (apvts, id, "Tremolo", "tremOn",
                std::vector<std::pair<juce::String, juce::String>> { { "tremAmount", "Amount" }, { "tremRate", "Rate" } },
                std::vector<std::tuple<juce::String, juce::String, juce::StringArray>> {
                    { "tremVoice", "Voice", juce::StringArray { "Bias", "Harmonic" } } });

        if (id == "chorus")
            return std::make_unique<GenericKnobsTile> (apvts, id, "July", "chorusOn",
                std::vector<std::pair<juce::String, juce::String>> {
                    { "chorusRate", "Rate" }, { "chorusDepth", "Depth" }, { "chorusLag", "Lag" }, { "chorusMix", "Mix" } },
                std::vector<std::tuple<juce::String, juce::String, juce::StringArray>> {
                    { "chorusWaveform", "Wave", juce::StringArray { "Sine", "Triangle" } },
                    { "chorusDCV", "D-C-V", juce::StringArray { "Dry", "Chorus", "Vibrato" } } });

        // Ported from Rockalizer -- Dimension D/SDD-320-style ensemble
        // chorus with a one-button Flanger blend, a separate pedal from
        // July above (see DimensionChorusModule.h for why).
        if (id == "dimChorus")
            return std::make_unique<GenericKnobsTile> (apvts, id, "Ensemble", "dimChorusOn",
                std::vector<std::pair<juce::String, juce::String>> {
                    { "dimChorusRate", "Rate" }, { "dimChorusDepth", "Depth" },
                    { "dimChorusWidth", "Width" }, { "dimChorusTone", "Tone" }, { "dimChorusMix", "Mix" } },
                std::vector<std::tuple<juce::String, juce::String, juce::StringArray>> {
                    { "dimChorusFlangerMode", "Mode", juce::StringArray { "Off", "Mode I", "Mode II", "Mode III" } } });

        // Sync (tap-tempo) toggles + note-division reuse the global
        // "tapTempoBpm" set by the header bar's Tap button -- see
        // TapTempo.h. Scoped to Plexer/Copier only (Delay), not the other
        // modulation/delay pedals.
        if (id == "delay")
            return std::make_unique<DelayTile> (apvts);

        // Roland RE-201-style 3-head tape echo, a separate pedal from
        // Plexer/Copier above (see SpaceEchoModule.h for why). Bass/Treble
        // shelving pair replaces a single Tone knob, matching the real
        // unit's tone stack.
        if (id == "spaceEcho")
            return std::make_unique<GenericKnobsTile> (apvts, id, "Satellite", "spaceEchoOn",
                std::vector<std::pair<juce::String, juce::String>> {
                    { "spaceEchoTime", "Time" }, { "spaceEchoRepeats", "Repeats" },
                    { "spaceEchoBass", "Bass" }, { "spaceEchoTreble", "Treble" },
                    { "spaceEchoWobble", "Wobble" },
                    { "spaceEchoDrive", "Drive" }, { "spaceEchoMix", "Mix" } },
                std::vector<std::tuple<juce::String, juce::String, juce::StringArray>> {
                    { "spaceEchoPattern", "Pattern",
                      juce::StringArray { "Straight", "Bounce", "Gallop", "Cluster", "Wash", "Ping-Pong" } } });

        if (id == "reverb")
            return std::make_unique<GenericKnobsTile> (apvts, id, "Reverb", "reverbOn",
                std::vector<std::pair<juce::String, juce::String>> {
                    { "reverbPreDelay", "PreDelay" }, { "reverbDecay", "Decay" }, { "reverbTone", "Tone" },
                    { "reverbMix", "Mix" }, { "reverbWidth", "Width" } },
                std::vector<std::tuple<juce::String, juce::String, juce::StringArray>> {
                    { "reverbModel", "Model", juce::StringArray { "Room", "Hall", "Plate" } } });

        // Ported from Rockalizer -- spring-tank reverb, a separate pedal
        // from the algorithmic Hall/Room/Plate reverb above.
        if (id == "spring")
            return std::make_unique<GenericKnobsTile> (apvts, id, "Spring", "springOn",
                std::vector<std::pair<juce::String, juce::String>> {
                    { "springDecay", "Decay" }, { "springDwell", "Dwell" }, { "springTone", "Tone" },
                    { "springDrip", "Drip" }, { "springMix", "Mix" } },
                std::vector<std::tuple<juce::String, juce::String, juce::StringArray>> {
                    { "springModel", "Model", juce::StringArray { "Space", "9100", "Echomixer" } } });

        // Original 1073-style channel EQ (shelving low/high, swept-mid
        // peak, multi-stop HPF) built from AMS Neve's own public spec --
        // see ChannelEQModule.h.
        if (id == "channelEQ")
            return std::make_unique<ChannelEQTile> (apvts);

        // Console-summing-style coloration -- see DeskModule.h.
        if (id == "desk")
            return std::make_unique<GenericKnobsTile> (apvts, id, "Desk", "deskOn",
                std::vector<std::pair<juce::String, juce::String>> { { "deskAmount", "Amount" } },
                std::vector<std::tuple<juce::String, juce::String, juce::StringArray>> {
                    { "deskStyle", "Style", juce::StringArray { "Subtle", "Classic", "Hot" } } });

        if (id == "eq")
            return std::make_unique<EQTile> (apvts);

        // A single box holding two independently user-chosen pedals, run in
        // parallel on their own copy of the dry signal and blended back
        // together -- see ParallelNode.h. Needs the whole processor (not
        // just apvts) so it can query which ids are already in use
        // elsewhere on the board when building its own Slot A/Slot B
        // picker choices.
        if (id == "parallel")
            return std::make_unique<ParallelTile> (processor,
                [&processor] (const juce::String& childId) { return createTile (childId, processor); });

        // "inputGain"/"noiseGate"/"outputGain" are intentionally not
        // handled here -- they're pinned permanently (Input first, Gate
        // right after it, Output last) and rendered in the editor's
        // bottom bar instead of as ordinary strip tiles (see PluginEditor's
        // pinned Input/Gate/Output controls and PedalboardComponent's
        // `middleOrder`).
        jassertfalse; // unknown or non-tile pedal id
        return nullptr;
    }
}
