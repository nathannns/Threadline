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

        makePreset ("01 Clean Tweed", {
            { "compOn", 1.0f }, { "compThreshold", 30.0f }, { "compRatio", 28.0f },
            { "compAttack", 8.0f }, { "ampDrive", 0.20f }, { "ampTone", 0.64f },
            { "ampOutput", -1.5f }, { "cabIRSelect", 2.0f }, { "cabMix", 1.0f },
            { "reverbOn", 1.0f }, { "reverbModel", 0.0f }, { "reverbMix", 12.0f }
        });
        makePreset ("02 Edge of Breakup", {
            { "klonOn", 1.0f }, { "klonGain", 0.16f }, { "klonTreble", 0.53f },
            { "klonLevel", 0.62f }, { "ampDrive", 0.48f }, { "ampTone", 0.54f },
            { "ampOutput", -3.0f }, { "cabIRSelect", 0.0f }, { "reverbOn", 1.0f },
            { "reverbModel", 1.0f }, { "reverbMix", 10.0f }
        });
        makePreset ("03 Driven Lead", {
            { "compOn", 1.0f }, { "compThreshold", 52.0f }, { "compRatio", 42.0f },
            { "ts9On", 1.0f }, { "ts9Variant", 1.0f }, { "ts9Drive", 0.24f },
            { "ts9Tone", 0.46f }, { "ts9Level", 0.68f }, { "ampDrive", 0.66f },
            { "ampTone", 0.58f }, { "ampOutput", -4.0f }, { "cabIRSelect", 3.0f },
            { "echoOn", 1.0f }, { "echoTime", 340.0f }, { "echoRepeats", 18.0f },
            { "echoTone", 4800.0f }, { "echoMix", 14.0f }, { "reverbOn", 1.0f },
            { "reverbModel", 6.0f }, { "reverbMix", 14.0f }
        });
        makePreset ("04 Ambient Hall", {
            { "ampDrive", 0.30f }, { "ampTone", 0.60f }, { "cabIRSelect", 2.0f },
            { "chorusOn", 1.0f }, { "chorusRate", 0.22f }, { "chorusDepth", 34.0f },
            { "chorusWidth", 92.0f }, { "chorusTone", 7200.0f }, { "chorusMix", 18.0f },
            { "echoOn", 1.0f }, { "echoSync", 1.0f }, { "echoPattern", 4.0f },
            { "echoDivision", 3.0f }, { "echoRepeats", 42.0f }, { "echoMix", 24.0f },
            { "reverbOn", 1.0f }, { "reverbModel", 3.0f }, { "reverbDecay", 0.64f },
            { "reverbTone", 0.52f }, { "reverbMix", 30.0f }, { "reverbWidth", 88.0f }
        });
        makePreset ("05 Tight Rhythm", {
            { "gateOn", 1.0f }, { "gateAmount", 32.0f }, { "ts9On", 1.0f },
            { "ts9Variant", 2.0f }, { "ts9Drive", 0.12f }, { "ts9Tone", 0.58f },
            { "ts9Level", 0.72f }, { "ampDrive", 0.56f }, { "ampTone", 0.48f },
            { "ampOutput", -4.5f }, { "cabIRSelect", 4.0f }, { "eqOn", 1.0f },
            { "eqBand1", -2.0f }, { "eqBand2", -1.0f }, { "eqBand5", 1.5f },
            { "eqBand8", 1.0f }, { "eqBand9", -1.5f }, { "eqHpfOn", 1.0f },
            { "eqHpfFreq", 72.0f }, { "eqLpfOn", 1.0f }, { "eqLpfFreq", 9200.0f }
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
