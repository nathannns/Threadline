#pragma once

#include <JuceHeader.h>
#include "DSP/PedalboardOrder.h"

// Real preset save/load, backed by individual XML files on disk (one per
// preset, holding a copy of the APVTS ValueTree). Threadline ships with no
// bundled presets — this is a fresh preset library, not Rockalizer's.
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& stateToManage)
        : apvts (stateToManage)
    {
        PedalboardOrder::ensureExists (apvts);
        getPresetsFolder().createDirectory();
    }

    // Wired up by ThreadlineAudioProcessor's constructor to
    // chainRunner.resyncFromPersistedOrder() -- PresetManager has no direct
    // reference to the chain runner, so loadPreset() calls this after every
    // apvts.replaceState() instead.
    std::function<void()> onOrderMayHaveChanged;

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
        // the automatic load following deletion. inputMute belongs in this
        // list for the same reason masterBypass does -- muting to switch
        // presets quietly shouldn't itself un-mute the instant the new
        // preset loads.
        const auto masterBypass = apvts.getRawParameterValue ("masterBypass")->load();
        const auto oversampling = apvts.getRawParameterValue ("ampOversampling")->load();
        const auto inputSource = apvts.getRawParameterValue ("inputSource")->load();
        const auto inputMute = apvts.getRawParameterValue ("inputMute")->load();
        apvts.replaceState (tree);
        restoreGlobalParameter ("masterBypass", masterBypass);
        restoreGlobalParameter ("ampOversampling", oversampling);
        restoreGlobalParameter ("inputSource", inputSource);
        restoreGlobalParameter ("inputMute", inputMute);
        // Pedalboard order is NOT in the preserved-globals list above --
        // each preset's own saved order loads normally, since the order
        // genuinely is part of what defines a preset/tone, not a global
        // hardware setting. Old presets predating this feature get a
        // synthesized default (odOrder-respecting) order here.
        PedalboardOrder::ensureExists (apvts);
        if (onOrderMayHaveChanged)
            onOrderMayHaveChanged();
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
