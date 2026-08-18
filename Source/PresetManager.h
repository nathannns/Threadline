#pragma once

#include <JuceHeader.h>

// Real preset save/load, backed by individual XML files on disk (one per
// preset, holding a copy of the APVTS ValueTree). Threadline ships with no
// bundled presets — this is a fresh preset library, not Rockalizer's.
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& stateToManage)
        : apvts (stateToManage)
    {
        getPresetsFolder().createDirectory();
        ensureTestingPresets();
    }

    static juce::File getPresetsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Threadline")
                   .getChildFile ("Presets");
    }

    // Sorted alphabetically (case-insensitive) so prev/next order is stable.
    juce::Array<juce::File> getAllPresets() const
    {
        juce::Array<juce::File> presets;
        for (const auto& entry : juce::RangedDirectoryIterator (getPresetsFolder(), false, "*.xml"))
            presets.add (entry.getFile());

        std::sort (presets.begin(), presets.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()) < 0;
        });
        return presets;
    }

    juce::String getCurrentPresetName() const { return currentPresetName; }

    // Updates the name the bar shows / the next Save will write to, without
    // touching disk — lets the user rename via the editable name field before
    // committing with Save.
    void setCurrentPresetName (const juce::String& newName) { currentPresetName = newName; }

    void savePreset (const juce::String& name)
    {
        if (name.isEmpty())
            return;

        auto file = getPresetsFolder().getChildFile (juce::File::createLegalFileName (name) + ".xml");
        if (auto state = apvts.copyState(); state.isValid())
        {
            if (auto xml = state.createXml())
                xml->writeTo (file);
        }
        currentPresetName = name;
    }

    bool loadPreset (const juce::File& file)
    {
        if (! file.existsAsFile())
            return false;

        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr)
            return false;

        auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.isValid())
            return false;

        // Hardware/session choices are global, not tone-preset parameters.
        // Preserve them across every preset load, including prev/next and
        // the automatic load following deletion.
        const auto masterBypass = apvts.getRawParameterValue ("masterBypass")->load();
        const auto oversampling = apvts.getRawParameterValue ("ampOversampling")->load();
        const auto inputSource = apvts.getRawParameterValue ("inputSource")->load();
        apvts.replaceState (tree);
        restoreGlobalParameter ("masterBypass", masterBypass);
        restoreGlobalParameter ("ampOversampling", oversampling);
        restoreGlobalParameter ("inputSource", inputSource);
        currentPresetName = file.getFileNameWithoutExtension();
        return true;
    }

    // Returns true if a preset was actually loaded (false when there are no
    // saved presets yet to step through).
    bool loadNext() { return stepPreset (1); }
    bool loadPrevious() { return stepPreset (-1); }

    // "Add" — create a fresh preset distinct from whatever's currently
    // loaded, rather than overwriting it (that's what Save is for). Auto-
    // names it to avoid colliding with an existing preset.
    juce::String addNewPreset()
    {
        auto name = generateUniqueName ("New Preset");
        savePreset (name);
        return name;
    }

    // "Delete" — removes the currently loaded preset from disk, then moves
    // to whatever preset is now first alphabetically (or resets to a blank
    // "Init" name if none are left).
    void deleteCurrentPreset()
    {
        auto file = getPresetsFolder().getChildFile (juce::File::createLegalFileName (currentPresetName) + ".xml");
        if (file.existsAsFile())
            file.deleteFile();

        auto remaining = getAllPresets();
        if (! remaining.isEmpty())
            loadPreset (remaining.getReference (0));
        else
            currentPresetName = "Init";
    }

    juce::String generateUniqueName (const juce::String& baseName) const
    {
        juce::StringArray existing;
        for (auto& f : getAllPresets())
            existing.add (f.getFileNameWithoutExtension());

        if (! existing.contains (baseName))
            return baseName;

        int suffix = 2;
        while (existing.contains (baseName + " " + juce::String (suffix)))
            ++suffix;
        return baseName + " " + juce::String (suffix);
    }

private:
    void restoreGlobalParameter (const char* id, float value)
    {
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    void ensureTestingPresets()
    {
        const auto original = apvts.copyState();
        const auto originalName = currentPresetName;
        const auto makePreset = [this, &original] (const juce::String& name,
                                        std::initializer_list<std::pair<const char*, float>> values)
        {
            apvts.replaceState (original);
            for (const auto& value : values)
                if (auto* parameter = apvts.getParameter (value.first))
                    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value.second));
            savePreset (name);
        };

        // Each factory sound deliberately exercises the current topology:
        // Diamond optical compressor (two-stage vactrol release), switchable
        // drive order, TS9/TS808/TS10 variants, Vintage/Boutique amp voices
        // (now the real triode preamp model, not a curve-fit tanh), parallel
        // dual-cab IRs (with a phase-invert showcase), both delay engines
        // (Plexer *and* Copier -- the original six only ever used Plexer),
        // the July-inspired chorus/vibrato, the bias-modulation tremolo, all
        // three reverb spaces, and the post-effects EQ.
        makePreset ("01 Clean Tweed", {
            { "compOn", 1.0f }, { "compThreshold", 28.0f }, { "compRatio", 24.0f },
            { "compAttack", 6.0f }, { "compRelease", 0.8f }, { "compMakeup", 1.2f },
            { "ampVoice", 0.0f }, { "ampDrive", 0.22f }, { "ampTone", 0.63f },
            { "ampOutput", -1.5f },
            { "cabAOn", 1.0f }, { "cabAIRSelect", 2.0f }, { "cabAMix", 1.0f },
            { "cabBOn", 0.0f }, { "cabBlend", 0.0f },
            // July kept light and taut — a shimmer, not a wobble.
            { "chorusOn", 1.0f }, { "chorusWaveform", 0.0f },
            { "chorusRate", 0.30f }, { "chorusDepth", 20.0f }, { "chorusLag", 25.0f },
            { "chorusDCV", 1.0f }, // Chorus
            { "reverbOn", 1.0f }, { "reverbModel", 0.0f }, { "reverbDecay", 0.48f },
            { "reverbTone", 0.62f }, { "reverbMix", 10.0f }, { "reverbWidth", 58.0f }
        });
        makePreset ("02 Edge of Breakup", {
            { "odOrder", 0.0f }, { "klonOn", 1.0f }, { "klonGain", 0.14f },
            { "klonTreble", 0.52f }, { "klonLevel", 0.61f },
            { "ampVoice", 0.0f }, { "ampDrive", 0.48f }, { "ampTone", 0.54f },
            { "ampOutput", -3.0f },
            { "cabAOn", 1.0f }, { "cabAIRSelect", 3.0f }, { "cabAMix", 1.0f },
            { "cabBOn", 1.0f }, { "cabBIRSelect", 1.0f }, { "cabBMix", 1.0f },
            { "cabBlend", 30.0f },
            // Bias-tremolo's asymmetric throb: a fast dip toward cutoff, a
            // gentler recovery. Kept subtle so it breathes under a chord.
            { "tremOn", 1.0f }, { "tremAmount", 20.0f },
            { "reverbOn", 1.0f }, { "reverbModel", 1.0f }, { "reverbDecay", 0.56f },
            { "reverbTone", 0.55f }, { "reverbMix", 9.0f }, { "reverbWidth", 54.0f }
        });
        makePreset ("03 Driven Lead", {
            { "compOn", 1.0f }, { "compThreshold", 42.0f }, { "compRatio", 34.0f },
            { "compAttack", 12.0f }, { "compRelease", 1.5f }, { "compMakeup", 1.0f },
            { "odOrder", 1.0f }, { "ts9On", 1.0f }, { "ts9Variant", 1.0f },
            { "ts9Drive", 0.22f }, { "ts9Tone", 0.47f }, { "ts9Level", 0.70f },
            { "klonOn", 1.0f }, { "klonGain", 0.10f }, { "klonTreble", 0.56f },
            { "klonLevel", 0.58f },
            { "ampVoice", 1.0f }, { "ampDrive", 0.62f }, { "ampBass", 0.56f },
            { "ampMid", 0.66f }, { "ampTreble", 0.54f }, { "ampOutput", -4.0f },
            { "cabAOn", 1.0f }, { "cabAIRSelect", 3.0f }, { "cabAMix", 1.0f },
            { "cabBOn", 1.0f }, { "cabBIRSelect", 4.0f }, { "cabBMix", 1.0f },
            { "cabBlend", 42.0f },
            // Plexer in Echo mode: a handful of clean-ish slapback repeats
            // behind the lead, not self-oscillating.
            { "echoOn", 1.0f }, { "delayModel", 0.0f }, { "echoMode", 0.0f },
            { "echoTime", 330.0f }, { "echoSustain", 30.0f }, { "echoVolume", 18.0f },
            { "reverbOn", 1.0f }, { "reverbModel", 0.0f }, { "reverbDecay", 0.66f },
            { "reverbMix", 12.0f }, { "reverbWidth", 62.0f }
        });
        makePreset ("04 Vibrato Swirl", {
            { "ampVoice", 0.0f }, { "ampDrive", 0.25f }, { "ampTone", 0.60f },
            { "ampOutput", -2.0f },
            { "cabAOn", 1.0f }, { "cabAIRSelect", 2.0f }, { "cabAMix", 1.0f },
            { "cabBOn", 0.0f }, { "cabBlend", 0.0f },
            // D-C-V set to Vibrato, with a brisk Rate and a long, sluggish
            // Lag — genuine full-wet vibrato, not diluted chorus.
            { "chorusOn", 1.0f }, { "chorusWaveform", 0.0f },
            { "chorusRate", 1.80f }, { "chorusDepth", 68.0f }, { "chorusLag", 70.0f },
            { "chorusDCV", 2.0f }, // Vibrato
            { "reverbOn", 1.0f }, { "reverbModel", 1.0f }, { "reverbPreDelay", 0.15f },
            { "reverbDecay", 0.70f }, { "reverbTone", 0.55f }, { "reverbMix", 18.0f },
            { "reverbWidth", 80.0f }
        });
        makePreset ("05 Ambient Hall", {
            { "ampVoice", 0.0f }, { "ampDrive", 0.28f }, { "ampTone", 0.61f },
            { "ampOutput", -3.0f },
            { "cabAOn", 1.0f }, { "cabAIRSelect", 2.0f }, { "cabAMix", 1.0f },
            { "cabBOn", 1.0f }, { "cabBIRSelect", 5.0f }, { "cabBMix", 1.0f },
            { "cabBlend", 35.0f },
            // Gentle July this time (slow Rate, moderate Lag, Chorus not
            // Vibrato) — air rather than the pronounced wobble of "04
            // Vibrato Swirl".
            { "chorusOn", 1.0f }, { "chorusWaveform", 0.0f },
            { "chorusRate", 0.22f }, { "chorusDepth", 28.0f }, { "chorusLag", 35.0f },
            { "chorusDCV", 1.0f }, // Chorus
            // Sound-on-Sound: layered, long-sustaining repeats that wash
            // into the reverb tail rather than distinct slapback.
            { "echoOn", 1.0f }, { "delayModel", 0.0f }, { "echoMode", 1.0f },
            { "echoTime", 450.0f }, { "echoSustain", 55.0f }, { "echoVolume", 26.0f },
            { "reverbOn", 1.0f }, { "reverbModel", 1.0f }, { "reverbPreDelay", 0.18f },
            { "reverbDecay", 0.82f }, { "reverbTone", 0.50f }, { "reverbMix", 29.0f },
            { "reverbWidth", 92.0f }
        });
        makePreset ("06 Tight Rhythm", {
            { "gateOn", 1.0f }, { "gateAmount", 30.0f },
            { "odOrder", 1.0f }, { "ts9On", 1.0f }, { "ts9Variant", 2.0f },
            { "ts9Drive", 0.10f }, { "ts9Tone", 0.57f }, { "ts9Level", 0.73f },
            { "ampVoice", 1.0f }, { "ampDrive", 0.55f }, { "ampBass", 0.43f },
            { "ampMid", 0.60f }, { "ampTreble", 0.58f }, { "ampOutput", -4.5f },
            { "cabAOn", 1.0f }, { "cabAIRSelect", 4.0f }, { "cabAMix", 1.0f },
            { "cabBOn", 1.0f }, { "cabBIRSelect", 0.0f }, { "cabBMix", 1.0f },
            // Phase-invert showcase: B's onset is already timing-aligned to A
            // (automatic), but flipping polarity here still tightens the low
            // end for this particular IR pairing.
            { "cabBPhase", 1.0f }, { "cabBlend", 24.0f },
            { "eqOn", 1.0f }, { "eqBand1", -2.5f }, { "eqBand2", -1.5f },
            { "eqBand3", -0.8f }, { "eqBand5", 1.4f }, { "eqBand6", 1.0f },
            { "eqBand8", 0.8f }, { "eqBand9", -1.8f },
            { "eqHpfOn", 1.0f }, { "eqHpfFreq", 76.0f },
            { "eqLpfOn", 1.0f }, { "eqLpfFreq", 9000.0f },
            { "reverbOn", 1.0f }, { "reverbModel", 0.0f }, { "reverbDecay", 0.38f },
            { "reverbMix", 6.0f }, { "reverbWidth", 48.0f }
        });
        // Bull alone, gain low, amp barely pushed -- the "transparent
        // boost" use case: raises level and adds a little upper-mid push
        // without the amp itself audibly breaking up.
        makePreset ("07 Transparent Boost", {
            { "odOrder", 0.0f }, { "klonOn", 1.0f }, { "klonGain", 0.08f },
            { "klonTreble", 0.46f }, { "klonLevel", 0.66f },
            { "ampVoice", 0.0f }, { "ampDrive", 0.15f }, { "ampTone", 0.58f },
            { "ampOutput", -0.5f },
            { "cabAOn", 1.0f }, { "cabAIRSelect", 2.0f }, { "cabAMix", 1.0f },
            { "cabBOn", 0.0f }, { "cabBlend", 0.0f },
            { "reverbOn", 1.0f }, { "reverbModel", 1.0f }, { "reverbDecay", 0.40f },
            { "reverbTone", 0.60f }, { "reverbMix", 6.0f }, { "reverbWidth", 45.0f }
        });
        // Both drives stacked hard into a cranked Boutique voice -- the
        // opposite end of the gain range from "07 Transparent Boost",
        // thickened by a wide chorus and a long Plate tail.
        makePreset ("08 Wall of Fuzz", {
            { "compOn", 1.0f }, { "compThreshold", 55.0f }, { "compRatio", 40.0f },
            { "compAttack", 10.0f }, { "compRelease", 2.0f }, { "compMakeup", 1.5f },
            { "odOrder", 1.0f }, { "ts9On", 1.0f }, { "ts9Variant", 0.0f },
            { "ts9Drive", 0.55f }, { "ts9Tone", 0.52f }, { "ts9Level", 0.62f },
            { "klonOn", 1.0f }, { "klonGain", 0.35f }, { "klonTreble", 0.60f },
            { "klonLevel", 0.55f },
            { "ampVoice", 1.0f }, { "ampDrive", 0.88f }, { "ampBass", 0.52f },
            { "ampMid", 0.44f }, { "ampTreble", 0.62f }, { "ampOutput", -6.5f },
            { "cabAOn", 1.0f }, { "cabAIRSelect", 3.0f }, { "cabAMix", 1.0f },
            { "cabBOn", 1.0f }, { "cabBIRSelect", 5.0f }, { "cabBMix", 1.0f },
            { "cabBlend", 45.0f },
            { "chorusOn", 1.0f }, { "chorusWaveform", 1.0f },
            { "chorusRate", 0.55f }, { "chorusDepth", 45.0f }, { "chorusLag", 40.0f },
            { "chorusDCV", 1.0f }, // Chorus
            { "reverbOn", 1.0f }, { "reverbModel", 2.0f }, { "reverbDecay", 0.60f },
            { "reverbTone", 0.45f }, { "reverbMix", 14.0f }, { "reverbWidth", 70.0f }
        });
        // Snappy optical comp, bright Vintage voice, Plexer's Echo mode at a
        // short slapback delay time -- the classic rockabilly slap, not a
        // wash of repeats.
        makePreset ("09 Slapback Rockabilly", {
            { "compOn", 1.0f }, { "compThreshold", 38.0f }, { "compRatio", 45.0f },
            { "compAttack", -20.0f }, { "compRelease", 1.0f }, { "compMakeup", 0.8f },
            { "odOrder", 0.0f }, { "ts9On", 1.0f }, { "ts9Variant", 1.0f },
            { "ts9Drive", 0.08f }, { "ts9Tone", 0.62f }, { "ts9Level", 0.68f },
            { "ampVoice", 0.0f }, { "ampDrive", 0.30f }, { "ampTone", 0.70f },
            { "ampOutput", -2.0f },
            { "cabAOn", 1.0f }, { "cabAIRSelect", 4.0f }, { "cabAMix", 1.0f },
            { "cabBOn", 0.0f }, { "cabBlend", 0.0f },
            { "echoOn", 1.0f }, { "delayModel", 0.0f }, { "echoMode", 0.0f },
            { "echoTime", 115.0f }, { "echoSustain", 12.0f }, { "echoVolume", 22.0f },
            { "reverbOn", 1.0f }, { "reverbModel", 0.0f }, { "reverbDecay", 0.30f },
            { "reverbTone", 0.65f }, { "reverbMix", 8.0f }, { "reverbWidth", 40.0f }
        });
        // Copier (Carbon-Copy-style BBD) gets its own showcase -- every
        // other preset here uses Plexer. Mod on for the characteristic
        // chorus-like wobble on the repeats, Plate reverb for a darker,
        // denser tail behind them.
        makePreset ("10 Copier Dreams", {
            { "ampVoice", 0.0f }, { "ampDrive", 0.32f }, { "ampTone", 0.58f },
            { "ampOutput", -2.5f },
            { "cabAOn", 1.0f }, { "cabAIRSelect", 2.0f }, { "cabAMix", 1.0f },
            { "cabBOn", 1.0f }, { "cabBIRSelect", 1.0f }, { "cabBMix", 1.0f },
            { "cabBlend", 30.0f },
            { "echoOn", 1.0f }, { "delayModel", 1.0f },
            { "carbonTime", 340.0f }, { "carbonRegen", 42.0f }, { "carbonMix", 32.0f },
            { "carbonMod", 1.0f },
            { "reverbOn", 1.0f }, { "reverbModel", 2.0f }, { "reverbPreDelay", 0.10f },
            { "reverbDecay", 0.58f }, { "reverbTone", 0.48f }, { "reverbMix", 16.0f },
            { "reverbWidth", 66.0f }
        });

        apvts.replaceState (original);
        currentPresetName = originalName;
    }

    bool stepPreset (int direction)
    {
        auto presets = getAllPresets();
        if (presets.isEmpty())
            return false;

        auto index = presets.indexOf (getPresetsFolder().getChildFile (currentPresetName + ".xml"));
        if (index < 0)
            index = direction > 0 ? -1 : 0; // not on a saved preset: next -> first, prev -> first

        index = (index + direction + presets.size()) % presets.size();
        return loadPreset (presets.getReference (index));
    }

    juce::AudioProcessorValueTreeState& apvts;
    juce::String currentPresetName { "Init" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};
