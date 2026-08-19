#pragma once

#include "PedalTile.h"
#include "PedalDisplayNames.h"
#include "../../DSP/CabModule.h"
#include "../../DSP/GraphicEQModule.h"
#include "../../DSP/TapTempo.h"
#include "../../DSP/PedalboardOrder.h"
#include "../../PluginProcessor.h"

// One or more knobs (+ optional combos below them) -- covers every pedal
// whose whole control surface is "N continuous knobs and maybe a couple of
// discrete choices": NoiseGate, Compressor, Klon, TS9, Tremolo, Chorus,
// Reverb. See PedalTileFactory.h for each one's exact param list.
class GenericKnobsTile : public PedalTileComponent
{
public:
    GenericKnobsTile (juce::AudioProcessorValueTreeState& apvtsIn, const juce::String& id,
                       const juce::String& displayName, const juce::String& toggleParamId,
                       std::vector<std::pair<juce::String, juce::String>> knobParams,
                       std::vector<std::tuple<juce::String, juce::String, juce::StringArray>> comboParams = {})
        : PedalTileComponent (apvtsIn, id, displayName, toggleParamId)
    {
        for (auto& [paramId, label] : knobParams)
            knobs.push_back (makeTileKnob (*this, apvtsIn, paramId, label));
        for (auto& [paramId, label, choices] : comboParams)
            combos.push_back (makeTileCombo (*this, apvtsIn, paramId, label, choices));
    }

    // Narrow, real-stompbox-like proportions: knobs pack into a 2-column
    // grid (1 column only when there's just a single knob) rather than
    // spreading into one wide row, so the tile stays tall and narrow like
    // a real pedal instead of short and wide.
    int getPreferredWidth() const override
    {
        const auto columns = juce::jmin (2, (int) knobs.size());
        return juce::jmax (188, columns * 119 + 38);
    }

protected:
    void resizedBody (juce::Rectangle<int> body) override
    {
        auto comboArea = combos.empty() ? juce::Rectangle<int>() : body.removeFromBottom (55 * (int) combos.size());
        layoutTileKnobRow (knobs, body, juce::jmin (2, (int) knobs.size()));
        if (! combos.empty())
        {
            for (auto& combo : combos)
            {
                auto cell = comboArea.removeFromTop (55).reduced (4);
                combo->label.setBounds (cell.removeFromTop (15));
                combo->box.setBounds (cell);
            }
        }
    }

private:
    std::vector<std::unique_ptr<TileKnob>> knobs;
    std::vector<std::unique_ptr<TileCombo>> combos;
};

// Amp: Drive/Volume always shown, plus either Tone (Vintage 5E3) or
// Bass/Mid/Treble (Boutique) depending on the live ampVoice choice --
// visibility is polled on a timer and swapped without any image involved.
class AmpTile : public PedalTileComponent, private juce::Timer
{
public:
    explicit AmpTile (juce::AudioProcessorValueTreeState& apvtsIn)
        : PedalTileComponent (apvtsIn, "amp", "Amp", "ampOn")
    {
        driveKnob = makeTileKnob (*this, apvtsIn, "ampDrive", "Drive");
        outputKnob = makeTileKnob (*this, apvtsIn, "ampOutput", "Volume");
        toneKnob = makeTileKnob (*this, apvtsIn, "ampTone", "Tone");
        bassKnob = makeTileKnob (*this, apvtsIn, "ampBass", "Bass");
        midKnob = makeTileKnob (*this, apvtsIn, "ampMid", "Mid");
        trebleKnob = makeTileKnob (*this, apvtsIn, "ampTreble", "Treble");
        voiceCombo = makeTileCombo (*this, apvtsIn, "ampVoice", "", { "Vintage 5E3", "Boutique", "Vox Top Boost", "Deluxe 63" });
        updateVoiceVisibility (true);
        startTimerHz (15);
    }

    int getPreferredWidth() const override { return 425; }

protected:
    void resizedBody (juce::Rectangle<int> body) override
    {
        auto comboArea = body.removeFromTop (43);
        voiceCombo->label.setBounds (comboArea.removeFromTop (15));
        voiceCombo->box.setBounds (comboArea.reduced (55, 0));
        body.removeFromTop (10);

        std::vector<TileKnob*> visible { driveKnob.get(), outputKnob.get() };
        if (voiceIndex == 1 || voiceIndex == 3)
            visible.insert (visible.end(), { bassKnob.get(), midKnob.get(), trebleKnob.get() });
        else if (voiceIndex == 2)
            visible.insert (visible.end(), { bassKnob.get(), trebleKnob.get() });
        else
            visible.push_back (toneKnob.get());

        // Bounded rows of up to 3 knobs each, real-stompbox-sized rather
        // than one wide row stretched across the tile's full height.
        const auto columns = juce::jmin (3, (int) visible.size());
        const auto rows = (int) ((visible.size() + (size_t) columns - 1) / (size_t) columns);
        const auto cellW = body.getWidth() / juce::jmax (1, columns);
        const auto cellH = juce::jmin (175, body.getHeight() / juce::jmax (1, rows));
        for (size_t i = 0; i < visible.size(); ++i)
        {
            auto cell = juce::Rectangle<int> (body.getX() + (int) (i % (size_t) columns) * cellW,
                                              body.getY() + (int) (i / (size_t) columns) * cellH, cellW, cellH).reduced (5, 3);
            visible[i]->label.setBounds (cell.removeFromTop (18));
            visible[i]->slider.setBounds (cell);
        }
    }

private:
    void timerCallback() override { updateVoiceVisibility (false); }

    void updateVoiceVisibility (bool force)
    {
        const auto nowVoiceIndex = juce::jlimit (0, 3, (int) apvts.getRawParameterValue ("ampVoice")->load());
        if (! force && nowVoiceIndex == voiceIndex)
            return;
        voiceIndex = nowVoiceIndex;
        const auto showTone = voiceIndex == 0;
        const auto showBass = voiceIndex == 1 || voiceIndex == 2 || voiceIndex == 3;
        const auto showMid = voiceIndex == 1 || voiceIndex == 3;
        const auto showTreble = voiceIndex == 1 || voiceIndex == 2 || voiceIndex == 3;
        toneKnob->slider.setVisible (showTone); toneKnob->label.setVisible (showTone);
        bassKnob->slider.setVisible (showBass); bassKnob->label.setVisible (showBass);
        midKnob->slider.setVisible (showMid); midKnob->label.setVisible (showMid);
        trebleKnob->slider.setVisible (showTreble); trebleKnob->label.setVisible (showTreble);
        resized();
    }

    std::unique_ptr<TileKnob> driveKnob, outputKnob, toneKnob, bassKnob, midKnob, trebleKnob;
    std::unique_ptr<TileCombo> voiceCombo;
    int voiceIndex = 0;
};

// Cab: a single IR loader (previously two parallel-blended slots, cabA/
// cabB, simplified down to one at the user's request -- see CabUnitNode's
// own comment). cabA* param ids are reused directly as this tile's only
// controls, following the same "base-class bypass toggle + a couple of
// extra independent controls" pattern as ChannelEQTile/LowDynamicTile.
class CabTile : public PedalTileComponent
{
public:
    explicit CabTile (juce::AudioProcessorValueTreeState& apvtsIn)
        : PedalTileComponent (apvtsIn, "cab", "Cab", "cabAOn")
    {
        juce::StringArray irNames;
        for (int i = 0; i < CabModule::numBuiltInIRs; ++i)
            irNames.add (CabModule::getBuiltInIRName (i));

        phase = makeTileToggle (*this, apvtsIn, "cabAPhase", juce::CharPointer_UTF8 ("\xc3\x98"));
        irSelect = makeTileCombo (*this, apvtsIn, "cabAIRSelect", "IR", irNames);
        mix = makeTileKnob (*this, apvtsIn, "cabAMix", "Mix");
    }

    int getPreferredWidth() const override { return 238; }

protected:
    void resizedBody (juce::Rectangle<int> body) override
    {
        auto row = body.removeFromTop (28);
        phase->button.setBounds (row.removeFromRight (33));
        irSelect->box.setBounds (row.reduced (3, 0));
        body.removeFromTop (10);
        mix->label.setBounds (body.removeFromTop (18));
        mix->slider.setBounds (body.removeFromTop (163).reduced (5, 0));
    }

private:
    std::unique_ptr<TileToggle> phase;
    std::unique_ptr<TileCombo> irSelect;
    std::unique_ptr<TileKnob> mix;
};

// Delay: one card shared between two engines (Plexer / Copier), matching
// PedalNode's DelayNode which wraps both the same way -- only the active
// engine's knobs are shown, swapped on a timer polling delayModel.
class DelayTile : public PedalTileComponent, private juce::Timer
{
public:
    explicit DelayTile (juce::AudioProcessorValueTreeState& apvtsIn)
        : PedalTileComponent (apvtsIn, "delay", "Delay", "echoOn")
    {
        modelCombo = makeTileCombo (*this, apvtsIn, "delayModel", "", { "Plexer", "Copier" });
        echoTime = makeTileKnob (*this, apvtsIn, "echoTime", "Time");
        echoSustain = makeTileKnob (*this, apvtsIn, "echoSustain", "Sustain");
        echoVolume = makeTileKnob (*this, apvtsIn, "echoVolume", "Volume");
        echoMode = makeTileCombo (*this, apvtsIn, "echoMode", "Mode", { "Echo", "Sound-on-Sound" });
        echoSync = makeTileToggle (*this, apvtsIn, "echoSync", "Sync");
        echoDivision = makeTileCombo (*this, apvtsIn, "echoDivision", "Div", TapTempo::getDivisionNames());
        carbonTime = makeTileKnob (*this, apvtsIn, "carbonTime", "Time");
        carbonRegen = makeTileKnob (*this, apvtsIn, "carbonRegen", "Regen");
        carbonMix = makeTileKnob (*this, apvtsIn, "carbonMix", "Mix");
        carbonMod = makeTileToggle (*this, apvtsIn, "carbonMod", "Mod");
        carbonSync = makeTileToggle (*this, apvtsIn, "carbonSync", "Sync");
        carbonDivision = makeTileCombo (*this, apvtsIn, "carbonDivision", "Div", TapTempo::getDivisionNames());
        updateModelVisibility (true);
        startTimerHz (15);
    }

    int getPreferredWidth() const override { return 325; }

protected:
    void resizedBody (juce::Rectangle<int> body) override
    {
        auto modelArea = body.removeFromTop (43);
        modelCombo->label.setBounds (modelArea.removeFromTop (15));
        modelCombo->box.setBounds (modelArea.reduced (70, 0));

        // Tap-tempo Sync + note-division, own row -- Plexer/Copier only
        // (not the other modulation/delay pedals).
        auto syncArea = body.removeFromTop (33);
        auto* sync = plexer ? echoSync.get() : carbonSync.get();
        auto* division = plexer ? echoDivision.get() : carbonDivision.get();
        sync->button.setBounds (syncArea.removeFromLeft (syncArea.getWidth() / 2).withSizeKeepingCentre (75, 28));
        division->box.setBounds (syncArea.withSizeKeepingCentre (juce::jmin (syncArea.getWidth() - 5, 100), 28));

        if (plexer)
        {
            auto modeArea = body.removeFromBottom (38);
            echoMode->label.setBounds (modeArea.removeFromTop (15));
            echoMode->box.setBounds (modeArea.reduced (43, 0));
            std::vector<TileKnob*> ks { echoTime.get(), echoSustain.get(), echoVolume.get() };
            layoutRow (ks, body);
        }
        else
        {
            auto modArea = body.removeFromBottom (38);
            carbonMod->button.setBounds (modArea.withSizeKeepingCentre (75, 28));
            std::vector<TileKnob*> ks { carbonTime.get(), carbonRegen.get(), carbonMix.get() };
            layoutRow (ks, body);
        }
    }

private:
    static void layoutRow (std::vector<TileKnob*>& ks, juce::Rectangle<int> area)
    {
        auto row = area.removeFromTop (juce::jmin (188, area.getHeight()));
        const auto cw = row.getWidth() / juce::jmax (1, (int) ks.size());
        for (size_t i = 0; i < ks.size(); ++i)
        {
            auto cell = juce::Rectangle<int> (row.getX() + (int) i * cw, row.getY(), cw, row.getHeight()).reduced (5, 3);
            ks[i]->label.setBounds (cell.removeFromTop (18));
            ks[i]->slider.setBounds (cell);
        }
    }

    void timerCallback() override { updateModelVisibility (false); }

    void updateModelVisibility (bool force)
    {
        const auto nowPlexer = apvts.getRawParameterValue ("delayModel")->load() < 0.5f;
        if (! force && nowPlexer == plexer)
            return;
        plexer = nowPlexer;
        echoTime->slider.setVisible (plexer); echoTime->label.setVisible (plexer);
        echoSustain->slider.setVisible (plexer); echoSustain->label.setVisible (plexer);
        echoVolume->slider.setVisible (plexer); echoVolume->label.setVisible (plexer);
        echoMode->box.setVisible (plexer); echoMode->label.setVisible (plexer);
        echoSync->button.setVisible (plexer);
        echoDivision->box.setVisible (plexer);
        carbonTime->slider.setVisible (! plexer); carbonTime->label.setVisible (! plexer);
        carbonRegen->slider.setVisible (! plexer); carbonRegen->label.setVisible (! plexer);
        carbonMix->slider.setVisible (! plexer); carbonMix->label.setVisible (! plexer);
        carbonMod->button.setVisible (! plexer);
        carbonSync->button.setVisible (! plexer);
        carbonDivision->box.setVisible (! plexer);
        resized();
    }

    std::unique_ptr<TileCombo> modelCombo, echoMode, echoDivision, carbonDivision;
    std::unique_ptr<TileKnob> echoTime, echoSustain, echoVolume, carbonTime, carbonRegen, carbonMix;
    std::unique_ptr<TileToggle> carbonMod, echoSync, carbonSync;
    bool plexer = true;
};

// 9-band graphic EQ + HPF/LPF. eqOn (the tile's own bypass, provided by the
// base class) gates the bands; eqHpfOn/eqLpfOn are separate, independent
// on/off states for the two filters.
class EQTile : public PedalTileComponent
{
public:
    explicit EQTile (juce::AudioProcessorValueTreeState& apvtsIn)
        : PedalTileComponent (apvtsIn, "eq", "EQ", "eqOn")
    {
        const auto& freqs = GraphicEQModule::getCentreFrequencies();
        for (int i = 0; i < GraphicEQModule::numBands; ++i)
        {
            auto band = std::make_unique<TileKnob>();
            band->slider.setSliderStyle (juce::Slider::LinearVertical);
            band->slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            band->slider.setScrollWheelEnabled (false);
            band->label.setText (formatFreq (freqs[(size_t) i]), juce::dontSendNotification);
            band->label.setJustificationType (juce::Justification::centred);
            band->label.setFont (juce::FontOptions (9.5f));
            band->label.setColour (juce::Label::textColourId, ThreadlineColours::textDim);
            addAndMakeVisible (band->slider);
            addAndMakeVisible (band->label);
            band->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvtsIn, "eqBand" + juce::String (i + 1), band->slider);
            bands.push_back (std::move (band));
        }
        hpfToggle = makeTileToggle (*this, apvtsIn, "eqHpfOn", "HPF");
        hpfKnob = makeTileKnob (*this, apvtsIn, "eqHpfFreq", "Hz");
        lpfToggle = makeTileToggle (*this, apvtsIn, "eqLpfOn", "LPF");
        lpfKnob = makeTileKnob (*this, apvtsIn, "eqLpfFreq", "Hz");
    }

    int getPreferredWidth() const override { return 538; }

protected:
    void resizedBody (juce::Rectangle<int> body) override
    {
        auto hpfCol = body.removeFromLeft (88);
        auto lpfCol = body.removeFromRight (88);
        hpfToggle->button.setBounds (hpfCol.removeFromTop (25));
        hpfKnob->label.setBounds (hpfCol.removeFromTop (15));
        hpfKnob->slider.setBounds (hpfCol.removeFromTop (163).reduced (5, 0));
        lpfToggle->button.setBounds (lpfCol.removeFromTop (25));
        lpfKnob->label.setBounds (lpfCol.removeFromTop (15));
        lpfKnob->slider.setBounds (lpfCol.removeFromTop (163).reduced (5, 0));

        body.removeFromLeft (5);
        body.removeFromRight (5);
        const auto cw = body.getWidth() / juce::jmax (1, (int) bands.size());
        for (size_t i = 0; i < bands.size(); ++i)
        {
            auto cell = juce::Rectangle<int> (body.getX() + (int) i * cw, body.getY(), cw, body.getHeight());
            bands[i]->label.setBounds (cell.removeFromBottom (18));
            bands[i]->slider.setBounds (cell.reduced (3, 0));
        }
    }

private:
    static juce::String formatFreq (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + "k" : juce::String ((int) hz);
    }

    std::vector<std::unique_ptr<TileKnob>> bands;
    std::unique_ptr<TileToggle> hpfToggle, lpfToggle;
    std::unique_ptr<TileKnob> hpfKnob, lpfKnob;
};

// Redface: shelving Low + swept-mid peaking + shelving High, plus an
// independently-switchable multi-stop HPF ahead of all three (see
// ChannelEQModule.h). The tile's own bypass toggle (from the base class)
// covers the whole EQ; hpfToggle is a second, independent on/off just for
// the HPF stage, same "more than one on/off state per pedal" pattern
// EQTile already uses for its own HPF/LPF.
class ChannelEQTile : public PedalTileComponent
{
public:
    explicit ChannelEQTile (juce::AudioProcessorValueTreeState& apvtsIn)
        : PedalTileComponent (apvtsIn, "channelEQ", "Redface", "channelEQOn")
    {
        gain = makeTileKnob (*this, apvtsIn, "channelEQGain", "Gain");
        lowFreq = makeTileCombo (*this, apvtsIn, "channelEQLowFreq", "Low Freq",
            juce::StringArray { "35 Hz", "60 Hz", "110 Hz", "220 Hz" });
        lowGain = makeTileKnob (*this, apvtsIn, "channelEQLowGain", "Low");
        midFreq = makeTileCombo (*this, apvtsIn, "channelEQMidFreq", "Mid Freq",
            juce::StringArray { "360 Hz", "700 Hz", "1.6k", "3.2k", "4.8k", "7.2k" });
        midGain = makeTileKnob (*this, apvtsIn, "channelEQMidGain", "Mid");
        highGain = makeTileKnob (*this, apvtsIn, "channelEQHighGain", "High (12k)");
        hpfToggle = makeTileToggle (*this, apvtsIn, "channelEQHpfOn", "HPF");
        hpfFreq = makeTileCombo (*this, apvtsIn, "channelEQHpfFreq", "HPF Freq",
            juce::StringArray { "50 Hz", "80 Hz", "160 Hz", "300 Hz" });
    }

    int getPreferredWidth() const override { return 375; }

protected:
    void resizedBody (juce::Rectangle<int> body) override
    {
        // Gain sits first (top), matching the real module's own front
        // panel layout -- the preamp trim knob above the EQ bands.
        auto gainRow = body.removeFromTop (138);
        gain->label.setBounds (gainRow.removeFromTop (18));
        gain->slider.setBounds (gainRow.reduced (body.getWidth() / 3, 0));
        body.removeFromTop (5);

        auto hpfRow = body.removeFromBottom (55);
        hpfToggle->button.setBounds (hpfRow.removeFromLeft (75).reduced (3));
        hpfFreq->label.setBounds (hpfRow.removeFromTop (15));
        hpfFreq->box.setBounds (hpfRow.reduced (5, 3));
        body.removeFromBottom (8);

        auto comboRow = body.removeFromBottom (53);
        const auto comboWidth = comboRow.getWidth() / 2;
        auto lowFreqArea = comboRow.removeFromLeft (comboWidth).reduced (4);
        auto midFreqArea = comboRow.reduced (4);
        lowFreq->label.setBounds (lowFreqArea.removeFromTop (15));
        lowFreq->box.setBounds (lowFreqArea);
        midFreq->label.setBounds (midFreqArea.removeFromTop (15));
        midFreq->box.setBounds (midFreqArea);

        std::vector<std::unique_ptr<TileKnob>*> knobs { &lowGain, &midGain, &highGain };
        const auto cw = body.getWidth() / (int) knobs.size();
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto cell = juce::Rectangle<int> (body.getX() + (int) i * cw, body.getY(), cw, body.getHeight()).reduced (5, 3);
            (*knobs[i])->label.setBounds (cell.removeFromTop (18));
            (*knobs[i])->slider.setBounds (cell);
        }
    }

private:
    std::unique_ptr<TileCombo> lowFreq, midFreq, hpfFreq;
    std::unique_ptr<TileKnob> gain, lowGain, midGain, highGain;
    std::unique_ptr<TileToggle> hpfToggle;
};

// Low Dynamic: Up/Down/Mix knobs -- Up lifts quiet material toward a
// floating, auto-tracked centre level, Down pulls loud material back down
// toward it, both acting simultaneously with no user-facing threshold
// (see LowDynamicModule.h) -- plus a Fast attack/release toggle, same
// "second independent on/off beyond the tile's own bypass" pattern
// ChannelEQTile/EQTile already use for their own extra toggles.
class LowDynamicTile : public PedalTileComponent
{
public:
    explicit LowDynamicTile (juce::AudioProcessorValueTreeState& apvtsIn)
        : PedalTileComponent (apvtsIn, "lowDynamic", "Dynamix", "lowDynamicOn")
    {
        up = makeTileKnob (*this, apvtsIn, "lowDynamicUp", "Up");
        down = makeTileKnob (*this, apvtsIn, "lowDynamicDown", "Down");
        mix = makeTileKnob (*this, apvtsIn, "lowDynamicMix", "Mix");
        fastToggle = makeTileToggle (*this, apvtsIn, "lowDynamicFast", "Fast");
    }

    int getPreferredWidth() const override { return 275; }

protected:
    void resizedBody (juce::Rectangle<int> body) override
    {
        auto fastRow = body.removeFromBottom (35);
        fastToggle->button.setBounds (fastRow.withSizeKeepingCentre (100, 30));

        std::vector<std::unique_ptr<TileKnob>*> knobs { &up, &down, &mix };
        const auto columns = 2;
        const auto rows = (int) ((knobs.size() + (size_t) columns - 1) / (size_t) columns);
        const auto cw = body.getWidth() / columns;
        const auto ch = body.getHeight() / juce::jmax (1, rows);
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto cell = juce::Rectangle<int> (body.getX() + (int) (i % (size_t) columns) * cw,
                                              body.getY() + (int) (i / (size_t) columns) * ch, cw, ch).reduced (5, 3);
            (*knobs[i])->label.setBounds (cell.removeFromTop (18));
            (*knobs[i])->slider.setBounds (cell);
        }
    }

private:
    std::unique_ptr<TileKnob> up, down, mix;
    std::unique_ptr<TileToggle> fastToggle;
};

// A single box holding two independently user-chosen pedals side by side --
// each processed in parallel on its own copy of the dry signal and blended
// back together via a shared Blend knob (see ParallelNode.h). An empty slot
// shows a plain "+" (like the strip's own "+ Add Pedal", but always
// visible, not hover-only -- there's no risk of it being "in the way" the
// way the between-tiles insert buttons are); picking a pedal from its popup
// menu writes straight into that slot's AudioParameterChoice param
// (parallelSlotA/parallelSlotB) and swaps the "+" for that pedal's own
// fully-working nested tile, embedded live via `createChildTile` (a
// callback into PedalTileFactory::createTile, injected at construction to
// avoid a header cycle between this file and PedalTileFactory.h).
//
// A pedal chosen into a slot here must never simultaneously sit in the
// main strip, or its DSP would run twice in one block (see ParallelNode.h)
// -- enforced by both this tile (picking one pulls it out of the main
// strip automatically, see PedalboardComponent::timerCallback()) and the
// main strip's own "+ Add Pedal" menu excluding whatever a slot here
// already holds, resynced on a 4Hz timer (mirrors AmpTile/DelayTile's
// existing live-poll-and-swap pattern elsewhere in this file). That timer
// means a pedal freed from one side takes up to ~250ms to reappear as
// pickable on the other -- fine for a deliberate manual reassignment,
// never hit by anything automated.
class ParallelTile : public PedalTileComponent, private juce::Timer
{
public:
    ParallelTile (ThreadlineAudioProcessor& processorIn,
                  std::function<std::unique_ptr<PedalTileComponent> (const juce::String&)> makeChildTile)
        : PedalTileComponent (processorIn.apvts, "parallel", "Parallel", "parallelOn"),
          processor (processorIn), createChildTile (std::move (makeChildTile))
    {
        for (auto* b : { &addSlotAButton, &addSlotBButton })
        {
            b->setButtonText ("+");
            b->setColour (juce::TextButton::buttonColourId, ThreadlineColours::panelDark);
            b->setColour (juce::TextButton::textColourOffId, ThreadlineColours::accentBright);
            addAndMakeVisible (b);
        }
        addSlotAButton.onClick = [this] { showSlotMenu (addSlotAButton, "parallelSlotA", "parallelSlotB"); };
        addSlotBButton.onClick = [this] { showSlotMenu (addSlotBButton, "parallelSlotB", "parallelSlotA"); };

        blend = makeTileKnob (*this, processor.apvts, "parallelBlend", "Blend");

        rebuildChildIfNeeded (lastSlotA, childTileA, "parallelSlotA");
        rebuildChildIfNeeded (lastSlotB, childTileB, "parallelSlotB");
        startTimerHz (4);
    }

    // Wide rather than tall -- the strip already scrolls horizontally, so
    // two slots side by side (each getting roughly a normal tile's own
    // body room) reads far better than stacking them, which previously
    // either clipped the lower slot or pushed it off-screen.
    int getPreferredWidth() const override { return 675; }
    // No height override -- this box is the same height as every other
    // tile in the strip (PedalTileComponent's own default, -1, means "use
    // the strip's standard row height"), so it sits flush in the row
    // instead of sticking up taller than its neighbors. Each nested pedal
    // still fits: positionScaledChild() scales it to fill whatever room
    // its slot actually has.

protected:
    void resizedBody (juce::Rectangle<int> body) override
    {
        // Cancels out the base class's own 8px horizontal / 5px vertical
        // margin around `body` -- Effect A/B are meant to sit flush against
        // this box's own left/right/bottom edges, with no visible gap and
        // no dead space below them, so this tile alone gets the full
        // post-header area back rather than the usual inset every other
        // tile's body gets.
        auto full = body.expanded (8, 5);

        constexpr int centerWidth = 80;
        auto leftArea = full.removeFromLeft ((full.getWidth() - centerWidth) / 2);
        auto centerArea = full.removeFromLeft (centerWidth);
        auto rightArea = full;

        layoutSlot (leftArea, addSlotAButton, childTileA.get());
        layoutSlot (rightArea, addSlotBButton, childTileB.get());

        // Blend lives in the slim center column between A and B, not in
        // its own row below both.
        auto blendArea = centerArea.withSizeKeepingCentre (centerWidth, 125);
        blend->label.setBounds (blendArea.removeFromTop (18));
        blend->slider.setBounds (blendArea);
    }

private:
    // Lays `child` out at its own natural, undistorted proportions (the
    // same ~375px-tall reference every top-level tile is designed to
    // render correctly at -- see PedalboardComponent::tileHeight), then
    // uniformly scales it via an AffineTransform to FILL `slotArea` as much
    // as possible on whichever axis is the tighter fit -- deliberately not
    // capped at 1x, since each slot now has roughly the same aspect ratio
    // as a normal tile's own body, so this fills (rather than just fits
    // inside) the slot with only the unavoidable sub-pixel-scale leftover
    // on the other axis. JUCE transforms painting and mouse hit-testing
    // together, so every knob/combo/toggle inside stays fully draggable/
    // clickable regardless.
    static void positionScaledChild (PedalTileComponent& child, juce::Rectangle<int> slotArea)
    {
        constexpr int naturalHeight = 375;
        const auto naturalWidth = juce::jmax (150, child.getPreferredWidth());
        child.setBounds (0, 0, naturalWidth, naturalHeight);
        const auto scale = juce::jmin ((float) slotArea.getWidth() / (float) naturalWidth,
                                        (float) slotArea.getHeight() / (float) naturalHeight);
        // Whichever axis isn't the binding constraint on `scale` (almost
        // always width here, since naturalHeight is what caps it) leaves
        // leftover room in `slotArea` -- centered on both axes, rather
        // than anchored at the slot's top-left, so that leftover space is
        // split evenly instead of all piling up on one side (previously:
        // invisibly absorbed into the inter-slot gap for the left slot,
        // but glaring dead space against the box's own edge for the right
        // slot, since nothing sat beyond it to hide it).
        const auto scaledWidth = (float) naturalWidth * scale;
        const auto scaledHeight = (float) naturalHeight * scale;
        const auto offsetX = (float) slotArea.getX() + ((float) slotArea.getWidth() - scaledWidth) * 0.5f;
        const auto offsetY = (float) slotArea.getY() + ((float) slotArea.getHeight() - scaledHeight) * 0.5f;
        child.setTransform (juce::AffineTransform::scale (scale).translated (offsetX, offsetY));
    }

    static void layoutSlot (juce::Rectangle<int> area, juce::TextButton& addButton, PedalTileComponent* child)
    {
        if (child != nullptr)
        {
            addButton.setVisible (false);
            positionScaledChild (*child, area);
        }
        else
        {
            addButton.setVisible (true);
            addButton.setBounds (area.withSizeKeepingCentre (juce::jmin (88, area.getWidth()),
                                                              juce::jmin (88, area.getHeight())));
        }
    }

    juce::String slotIdFor (const char* paramId) const
    {
        const auto choiceIndex = (int) processor.apvts.getRawParameterValue (paramId)->load();
        if (choiceIndex <= 0)
            return {};
        const auto& ids = PedalboardOrder::parallelSlotChoiceIds();
        const auto idx = choiceIndex - 1;
        return idx >= 0 && idx < ids.size() ? ids[idx] : juce::String();
    }

    void rebuildChildIfNeeded (juce::String& lastId, std::unique_ptr<PedalTileComponent>& child, const char* paramId)
    {
        const auto currentId = slotIdFor (paramId);
        if (currentId == lastId)
            return;
        lastId = currentId;
        child.reset();
        if (currentId.isNotEmpty())
        {
            child = createChildTile (currentId);
            if (child != nullptr)
            {
                // Clicking the nested tile's own "X" clears this slot back
                // to "None" (back to a plain "+") -- reads as "remove from
                // the box", the same meaning that button has everywhere
                // else in the strip.
                child->onRemoveClicked = [this, paramId] (const juce::String&)
                {
                    if (auto* parameter = processor.apvts.getParameter (paramId))
                        parameter->setValueNotifyingHost (parameter->convertTo0to1 (0.0f));
                };
                addAndMakeVisible (*child);
            }
        }
        resized();
    }

    // Any pedal is freely pickable into either slot -- picking one that's
    // currently a tile in the main strip silently pulls it out from there
    // (see PedalboardComponent::timerCallback()); the only real constraint
    // is A and B can't both name the same pedal (also enforced at the DSP
    // level as a safety net, see ParallelNode::dedupedSlotB()), so that's
    // simply left out of whichever slot's own popup menu.
    void showSlotMenu (juce::Component& anchor, const char* targetParamId, const char* otherParamId)
    {
        const auto otherChoice = (int) processor.apvts.getRawParameterValue (otherParamId)->load();
        const auto& ids = PedalboardOrder::parallelSlotChoiceIds();

        juce::PopupMenu menu;
        std::vector<int> menuChoiceIndices;
        int itemId = 1;
        for (int i = 0; i < ids.size(); ++i)
        {
            const auto choiceIndex = i + 1; // choice 0 is reserved for "None"
            if (choiceIndex == otherChoice)
                continue;
            menu.addItem (itemId, PedalDisplayNames::displayNameFor (ids[i]));
            menuChoiceIndices.push_back (choiceIndex);
            ++itemId;
        }
        if (menuChoiceIndices.empty())
        {
            menu.addItem (1, "No pedals available", false, false);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor));
            return;
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor),
            [safe = juce::Component::SafePointer<ParallelTile> (this), targetParamId, menuChoiceIndices] (int result)
            {
                if (safe == nullptr || result <= 0 || result > (int) menuChoiceIndices.size())
                    return;
                if (auto* parameter = safe->processor.apvts.getParameter (targetParamId))
                    parameter->setValueNotifyingHost (
                        parameter->convertTo0to1 ((float) menuChoiceIndices[(size_t) result - 1]));
            });
    }

    void timerCallback() override
    {
        rebuildChildIfNeeded (lastSlotA, childTileA, "parallelSlotA");
        rebuildChildIfNeeded (lastSlotB, childTileB, "parallelSlotB");
    }

    ThreadlineAudioProcessor& processor;
    std::function<std::unique_ptr<PedalTileComponent> (const juce::String&)> createChildTile;

    juce::TextButton addSlotAButton, addSlotBButton;
    std::unique_ptr<PedalTileComponent> childTileA, childTileB;
    juce::String lastSlotA, lastSlotB;
    std::unique_ptr<TileKnob> blend;
};
