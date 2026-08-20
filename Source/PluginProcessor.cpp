#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DSP/CabModule.h"
#include "DSP/GraphicEQModule.h"
#include "DSP/PedalboardOrder.h"
#include "DSP/TapTempo.h"

ThreadlineAudioProcessor::ThreadlineAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    chainRunner.owningProcessor = this;
    presetManager.onOrderMayHaveChanged = [this] { chainRunner.resyncFromPersistedOrder(); };
}

juce::AudioProcessorValueTreeState::ParameterLayout ThreadlineAudioProcessor::createParameterLayout()
{
    using Range = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    auto pid = [] (const char* id) { return juce::ParameterID { id, 1 }; };

    // --- Noise Gate ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("gateOn"), "Gate On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("gateAmount"), "Gate Amount",
        Range (0.0f, 100.0f, 0.1f), 0.0f));

    // --- Input ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("inputGain"), "Input Gain",
        Range (-24.0f, 24.0f, 0.1f), 0.0f));
    // Guitar-level pickups sit well below line level — this applies a fixed
    // calibration offset on top of the manual trim above, so plugging
    // straight in and switching to "Guitar" gets you in the right ballpark
    // before you touch the knob at all.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("inputSource"), "Input Source",
        juce::StringArray { "Guitar", "Line" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("input1On"), "Input 1 On", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("input2On"), "Input 2 On", true));

    // --- Pre-FX page bypass: gates Comp/Klon/TS9 together, independent of
    // each stage's own On toggle -- driven by double-pressing the Pre-FX
    // tab icon in the UI (see ThreadlineAudioProcessorEditor::TabPill), the
    // page-level counterpart to each stage's individual bypass.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("preFxSectionOn"), "Pre-FX Section On", true));

    // --- Compressor ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("compOn"), "Comp On", false));
    // Keep the legacy IDs so old sessions resolve, while the controls now
    // drive the Diamond-inspired optical topology.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("compThreshold"), "Compression",
        Range (0.0f, 100.0f, 0.1f), 42.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("compRatio"), "Attack Character",
        Range (0.0f, 100.0f, 0.1f), 35.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("compAttack"), "Tilt EQ",
        Range (-100.0f, 100.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("compRelease"), "Mid EQ",
        Range (-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("compMakeup"), "Comp Level",
        Range (-12.0f, 12.0f, 0.1f), 0.0f));

    // --- Low Dynamic --- a two-knob, no-threshold dynamics processor:
    // Up lifts quiet material toward a floating, auto-tracked centre level,
    // Down pulls loud material back down toward it, both acting at once.
    // See LowDynamicModule.h. Grouped with Compressor since both are
    // dynamics utilities, but freely reorderable like every other pedal.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("lowDynamicOn"), "Low Dynamic On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("lowDynamicUp"), "Low Dynamic Up",
        Range (0.0f, 100.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("lowDynamicDown"), "Low Dynamic Down",
        Range (0.0f, 100.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("lowDynamicFast"), "Low Dynamic Fast", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("lowDynamicMix"), "Low Dynamic Mix",
        Range (0.0f, 100.0f, 0.1f), 100.0f));

    // --- Bull (Klon-style, param IDs kept as "klon*" internally) ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("klonOn"), "Bull On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("klonGain"), "Bull Gain",
        Range (0.0f, 1.0f, 0.001f), 0.4f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("klonTreble"), "Bull Treble",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("klonLevel"), "Bull Level",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    // Deprecated: kept registered (never remove a shipped
    // AudioParameterChoice) but no longer read anywhere in the live
    // processing path -- KlonNode now follows the single global
    // "ampOversampling" setting instead, so overdrives and the amp always
    // share one oversampling quality rather than each having their own.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("klonOversampling"), "Bull Oversampling",
        juce::StringArray { "Off", "2x", "4x" }, 1));

    // --- Overdrive stage order: deprecated. Kept registered (never remove a
    // shipped AudioParameterChoice -- breaks host automation-lane identity
    // across saves) but no longer read anywhere in the live processing
    // path; general pedalboard reordering (PedalboardOrder.h) subsumes it.
    // Its only remaining job is the one-time migration read in
    // PedalboardOrder::buildDefaultOrderTree() for old sessions/presets
    // saved before the pedalboard feature existed.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("odOrder"), "Overdrive Order",
        juce::StringArray { "Bull -> Breaker", "Breaker -> Bull" }, 0));

    // --- TS9 ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("ts9On"), "TS9 On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ts9Drive"), "TS9 Drive",
        Range (0.0f, 1.0f, 0.001f), 0.4f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ts9Tone"), "TS9 Tone",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ts9Level"), "TS9 Level",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("ts9Variant"), "TS9 Voicing",
        juce::StringArray { "TS9", "TS808", "TS10" }, 0));
    // Deprecated: kept registered (never remove a shipped
    // AudioParameterChoice) but no longer read anywhere in the live
    // processing path -- TS9Node now follows the single global
    // "ampOversampling" setting instead, so overdrives and the amp always
    // share one oversampling quality rather than each having their own.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("ts9Oversampling"), "TS9 Oversampling",
        juce::StringArray { "Off", "2x", "4x" }, 1));

    // --- Fangs (FangsModule) --- an original op-amp diode-feedback
    // fuzz/distortion in the general archetype of the ProCo Rat/MXR
    // Distortion+ family (see FangsModule.h). Oversampling follows the
    // global "ampOversampling" setting, same as Bull/Breaker.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("fangsOn"), "Fangs On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("fangsGain"), "Fangs Gain",
        Range (0.0f, 1.0f, 0.001f), 0.4f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("fangsFilter"), "Fangs Filter",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("fangsLevel"), "Fangs Level",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("fangsMix"), "Fangs Mix",
        Range (0.0f, 1.0f, 0.001f), 1.0f));

    // --- Bison (BisonModule) --- an original two-stage cascaded fuzz in
    // the general archetype of the Big Muff Pi family (see
    // BisonModule.h). Oversampling follows "ampOversampling" too.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("bisonOn"), "Bison On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("bisonSustain"), "Bison Sustain",
        Range (0.0f, 1.0f, 0.001f), 0.4f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("bisonTone"), "Bison Tone",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("bisonLevel"), "Bison Level",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("bisonMix"), "Bison Mix",
        Range (0.0f, 1.0f, 0.001f), 1.0f));

    // --- Growl (GrowlModule) --- an original germanium two-transistor
    // fuzz in the general archetype of the Fuzz Face family, built around
    // a genuine per-sample nonlinear solve of the front-end input-loading
    // interaction real germanium fuzzes are known for (see
    // GrowlModule.h). Oversampling follows "ampOversampling" too.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("growlOn"), "Growl On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("growlBias"), "Growl Bias",
        Range (0.0f, 1.0f, 0.001f), 0.4f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("growlFuzz"), "Growl Fuzz",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("growlLevel"), "Growl Level",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("growlMix"), "Growl Mix",
        Range (0.0f, 1.0f, 0.001f), 1.0f));

    // --- Tape (ported from Rockalizer's TapeModule) --- tape saturation
    // and record/reproduce-style compression, not an overdrive/screamer
    // like Bull/Breaker; a texture/character stage. Oversampling also
    // follows the global "ampOversampling" setting, same as Bull/Breaker.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("tapeOn"), "Tape On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("tapeDrive"), "Tape Drive",
        Range (0.0f, 100.0f, 0.1f), 30.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("tapeCompression"), "Tape Compression",
        Range (0.0f, 100.0f, 0.1f), 25.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("tapeTone"), "Tape Tone",
        Range (0.0f, 100.0f, 0.1f), 60.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("tapeAge"), "Tape Age",
        Range (0.0f, 100.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("tapeMix"), "Tape Mix",
        Range (0.0f, 100.0f, 0.1f), 100.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("tapeVolume"), "Tape Volume",
        Range (0.0f, 100.0f, 0.1f), 100.0f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("tapeType"), "Tape Type",
        juce::StringArray { "Studio", "Cassette" }, 0));

    // --- Amp (5E3) --- defaults true (unlike the stomps above, which
    // default off): the amp is the plugin's core sound-shaping stage, and
    // this toggle exists so it can be A/B'd out or bypassed for a DI/reamp
    // chain, not because it should start disabled the way an optional pedal
    // does.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("ampOn"), "Amp On", true));
    // Page-level bypass for the whole Amp tab (Amp + Cab A/B together),
    // independent of ampOn/cabAOn/cabBOn -- same double-press pattern as
    // preFxSectionOn above.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("ampSectionOn"), "Amp Section On", true));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ampDrive"), "Amp Drive",
        Range (0.0f, 1.0f, 0.001f), 0.4f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ampTone"), "Amp Tone",
        Range (0.0f, 1.0f, 0.001f), 0.6f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ampOutput"), "Amp Volume",
        Range (-24.0f, 12.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("ampOversampling"), "Amp Oversampling",
        juce::StringArray { "Off", "2x", "4x" }, 2));
    // Vintage 5E3 keeps the single passive-feeling Tone knob above. Modern
    // 3-Band swaps that for an independent Bass/Mid/Treble stack (see
    // AmpModule::updateModernToneFilters) — same preamp/power-stage circuit
    // underneath, different tone section, not a different amp model. Vox
    // Top Boost IS a genuinely different circuit (12AX7 preamp into a
    // Bass/Treble shelf pair, a real long-tailed-pair phase inverter, EL84
    // push-pull power stage) — see AmpModule::Voice::voxAC30's own comment.
    // Fender AB763 is likewise a genuinely different circuit (blackface
    // Deluxe Reverb normal channel: 12AX7 preamp, its own real tone stack,
    // a real long-tailed-pair PI, fixed-bias 6V6 power stage) — see
    // AmpModule::Voice::fenderAB763's own comment.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("ampVoice"), "Amp Voice",
        juce::StringArray { "Vintage 5E3", "Boutique", "Vox Top Boost", "Deluxe 63", "JTM45", "Mark I", "Jazz Chorus" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ampBass"), "Amp Bass",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ampMid"), "Amp Mid",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ampTreble"), "Amp Treble",
        Range (0.0f, 1.0f, 0.001f), 0.5f));

    // --- Cab: a single IR loader (previously two, cabA/cabB, processed in
    // parallel and blended by cabBlend -- simplified down to one at the
    // user's request). cabB*/cabBlend stay registered below but deprecated
    // in place (never remove a shipped AudioParameter, same treatment as
    // odOrder/klonOversampling/ts9Oversampling) — CabUnitNode no longer
    // reads them.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("cabAOn"), "Cab On", true));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("cabAIRSelect"), "Cab IR",
        juce::StringArray { CabModule::getBuiltInIRName (0), CabModule::getBuiltInIRName (1),
                             CabModule::getBuiltInIRName (2), CabModule::getBuiltInIRName (3),
                             CabModule::getBuiltInIRName (4), CabModule::getBuiltInIRName (5),
                             CabModule::getBuiltInIRName (6), CabModule::getBuiltInIRName (7),
                             CabModule::getBuiltInIRName (8), CabModule::getBuiltInIRName (9),
                             CabModule::getBuiltInIRName (10), CabModule::getBuiltInIRName (11) },
        9 /* default: Tweed */));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("cabAMix"), "Cab Mix",
        Range (0.0f, 1.0f, 0.001f), 1.0f));
    // Onset alignment (CabModule::alignOnset) handles *timing* differences
    // between IRs automatically, but absolute polarity isn't detectable
    // from the IR data alone — this is the manual safety-net toggle for it.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("cabAPhase"), "Cab Phase", false));

    // Deprecated, unused (see comment above) — kept only so old sessions/
    // presets that reference these ids don't break.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("cabBOn"), "Cab B On", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("cabBIRSelect"), "Cab B IR",
        juce::StringArray { CabModule::getBuiltInIRName (0), CabModule::getBuiltInIRName (1),
                             CabModule::getBuiltInIRName (2), CabModule::getBuiltInIRName (3),
                             CabModule::getBuiltInIRName (4), CabModule::getBuiltInIRName (5),
                             CabModule::getBuiltInIRName (6), CabModule::getBuiltInIRName (7),
                             CabModule::getBuiltInIRName (8) },
        0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("cabBMix"), "Cab B Mix",
        Range (0.0f, 1.0f, 0.001f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("cabBPhase"), "Cab B Phase", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("cabBlend"), "Cab A/B Blend",
        Range (0.0f, 100.0f, 0.1f), 50.0f));

    // --- Wet FX page bypass: gates Tremolo/July/Delay/Reverb together,
    // independent of each effect's own On toggle -- same double-press
    // pattern as preFxSectionOn above.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("wetFxSectionOn"), "Wet FX Section On", true));

    // --- Tap Tempo: one global tempo, tapped via the header bar's Tap
    // button (see TapTempoButton). Scoped to Plexer/Copier only (Delay) --
    // each gets its own Sync toggle + note-division choice; when Sync is
    // on, that engine computes its Time from this BPM + division instead
    // of reading its own Time knob (see TapTempo.h for the shared
    // division math). The knob itself stays live/visible either way, so
    // turning Sync back off picks up wherever it was left.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("tapTempoBpm"), "Tap Tempo",
        Range (40.0f, 300.0f, 0.1f), 120.0f));

    // --- Tremolo ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("tremOn"), "Tremolo On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("tremAmount"), "Tremolo Amount",
        Range (0.0f, 100.0f, 0.1f), 40.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("tremRate"), "Tremolo Rate",
        Range (0.5f, 10.0f, 0.01f, 0.5f), 3.20f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("tremVoice"), "Tremolo Voice",
        juce::StringArray { "Bias", "Harmonic" }, 0));

    // --- July (Chorus/Vibrato) --- our name for the effect (Julia is
    // Walrus Audio's trademark); the control surface exactly matches the
    // real Julia's: Rate, Depth, Lag, a Sine/Triangle waveform switch, and
    // D-C-V (Dry-Chorus-Vibrato).
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("chorusOn"), "July On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("chorusRate"), "July Rate",
        Range (0.05f, 5.0f, 0.01f, 0.35f), 0.32f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("chorusDepth"), "July Depth",
        Range (0.0f, 100.0f, 0.1f), 42.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("chorusLag"), "July Lag",
        Range (0.0f, 100.0f, 0.1f), 30.0f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("chorusWaveform"), "July Waveform",
        juce::StringArray { "Sine", "Triangle" }, 0));
    // A continuous 0-100 knob on its own made it unclear where "Dry",
    // "Chorus", and "Vibrato" actually fell on the sweep, so D-C-V stays
    // three explicit character stops (Dry always forces silence outright;
    // Chorus/Vibrato pick the modulation type -- comb-filtered wobble
    // against a dry reference, vs modulated delay alone with no dry
    // reference left to comb against). chorusMix below is the actual
    // continuous wet/dry amount for whichever of those two is selected --
    // decoupling "which character" from "how much of it" rather than
    // baking one fixed blend percentage into each D-C-V stop.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("chorusDCV"), "July D-C-V",
        juce::StringArray { "Dry", "Chorus", "Vibrato" }, 1));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("chorusMix"), "July Mix",
        Range (0.0f, 100.0f, 0.1f), 42.0f));

    // --- Dimension Chorus (ported from Rockalizer's ChorusModule) --- a
    // Roland Dimension D/SDD-320-inspired BBD ensemble with a one-button
    // Flanger blend, genuinely different DSP from July above (which models
    // the Walrus Julia's D-C-V control surface instead) -- a separate
    // pedal, not another July mode.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("dimChorusOn"), "Dimension On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("dimChorusRate"), "Dimension Rate",
        Range (0.05f, 5.0f, 0.01f, 0.5f), 0.32f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("dimChorusDepth"), "Dimension Depth",
        Range (0.0f, 100.0f, 0.1f), 60.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("dimChorusWidth"), "Dimension Width",
        Range (0.0f, 100.0f, 0.1f), 75.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("dimChorusTone"), "Dimension Tone",
        Range (1800.0f, 16000.0f, 1.0f, 0.4f), 8000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("dimChorusMix"), "Dimension Mix",
        Range (0.0f, 100.0f, 0.1f), 55.0f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("dimChorusFlangerMode"), "Dimension Flanger Mode",
        juce::StringArray { "Off", "Mode I", "Mode II", "Mode III" }, 0));

    // --- Dimension BBD (circuit-faithful DC-2 "Dimension C" port) --- a
    // second, separate Dimension-style pedal from Ensemble above: this one is
    // the faithful dual-MN3007 BBD + NE570 compander engine (see
    // DimensionDBBDModule.h), exposing the real unit's 4-button Mode switch
    // plus input/output level trims rather than Ensemble's continuous
    // rate/depth/width/tone/mix surface. The four modes are independent
    // toggles (dimBbdMode1..4) so they can be engaged in combination, matching
    // the SDD-320's documented press-several-buttons-at-once behavior.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("dimBbdOn"), "Dimension BBD On", false));
    // dimBbdMode is DEPRECATED in place -- the mode selector was originally a
    // single 4-position choice, replaced by the four independent dimBbdMode1..4
    // toggles above the comment below. Kept registered so old sessions/presets/
    // host automation lanes keep their identity; it no longer drives the DSP.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("dimBbdMode"), "Dimension BBD Mode",
        juce::StringArray { "Mode I", "Mode II", "Mode III", "Mode IV" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("dimBbdMode1"), "Dimension BBD Mode I", true));
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("dimBbdMode2"), "Dimension BBD Mode II", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("dimBbdMode3"), "Dimension BBD Mode III", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("dimBbdMode4"), "Dimension BBD Mode IV", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("dimBbdInput"), "Dimension BBD Input",
        Range (0.0f, 100.0f, 0.1f), 70.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("dimBbdOutput"), "Dimension BBD Output",
        Range (0.0f, 100.0f, 0.1f), 70.0f));

    // --- JC Chorus (Roland JC-120's BBD chorus line, extracted from the Amp
    // voice into its own pedal) --- a fourth, separate chorus from July/
    // Ensemble/Dimension: a single modulated BBD delay per channel with the
    // two channels' LFO phase offset by pi (the amp's glassy stereo swirl),
    // no feedback/compander. Defaults reproduce the amp's old chorus (0.9Hz,
    // 4ms depth, 45% mix) -- see JCChorusModule.h.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("jcChorusOn"), "JC Chorus On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("jcChorusRate"), "JC Chorus Rate",
        Range (0.1f, 5.0f, 0.01f, 0.5f), 0.9f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("jcChorusDepth"), "JC Chorus Depth",
        Range (0.0f, 100.0f, 0.1f), 50.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("jcChorusMix"), "JC Chorus Mix",
        Range (0.0f, 100.0f, 0.1f), 45.0f));

    // --- Delay --- two selectable engines sharing one on/off toggle and one
    // Delay section: Plexer (our name for an Echoplex-style tape echo —
    // Echoplex is Maestro/Dunlop's trademark) and Copier (our name for a
    // Carbon-Copy-style BBD analog delay — Carbon Copy is MXR/Dunlop's
    // trademark). delayModel picks which engine is actually in the signal
    // path; only that engine's knobs matter for the current sound.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("echoOn"), "Delay On", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("delayModel"), "Delay Model",
        juce::StringArray { "Plexer", "Copier" }, 0));

    // Plexer's control surface matches the real EP-3's exactly: Time (a
    // slider on the real unit), Sustain, Volume, and an Echo / Sound-on-
    // Sound mode switch. No separate Tone/Wobble/Drive/Sync knobs — the
    // real unit doesn't have them (see EchoModule for how those
    // characteristics are modeled as fixed, always-on behaviour instead).
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("echoTime"), "Plexer Time",
        Range (60.0f, 800.0f, 1.0f, 0.5f), 375.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("echoSync"), "Plexer Sync", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("echoDivision"), "Plexer Division",
        TapTempo::getDivisionNames(), 0));
    // Reaches genuine self-oscillation at maximum, same as the real unit —
    // see EchoModule::setParameters.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("echoSustain"), "Plexer Sustain",
        Range (0.0f, 100.0f, 0.1f), 30.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("echoVolume"), "Plexer Volume",
        Range (0.0f, 100.0f, 0.1f), 25.0f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("echoMode"), "Plexer Mode",
        juce::StringArray { "Echo", "Sound-on-Sound" }, 0));

    // Copier's control surface matches the real Carbon Copy's exactly:
    // Time, Regen (feedback), Mix, and a Mod toggle for a chorus-like
    // wobble on the repeats. No Tone knob — the real unit's BBD darkening
    // is a fixed characteristic, not user-adjustable (see CarbonCopyModule).
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("carbonTime"), "Copier Time",
        Range (20.0f, 600.0f, 1.0f, 0.5f), 375.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("carbonSync"), "Copier Sync", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("carbonDivision"), "Copier Division",
        TapTempo::getDivisionNames(), 0));
    // Reaches near-self-oscillation at maximum, same as the real unit —
    // see CarbonCopyModule::setParameters.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("carbonRegen"), "Copier Regen",
        Range (0.0f, 100.0f, 0.1f), 35.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("carbonMix"), "Copier Mix",
        Range (0.0f, 100.0f, 0.1f), 35.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("carbonMod"), "Copier Mod", false));

    // --- Space Echo (ported from Rockalizer's EchoModule) --- a Roland
    // RE-201-inspired 3-head tape echo, genuinely different DSP from
    // Plexer/Copier above -- a third, separate delay pedal, not another
    // Delay Model choice.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("spaceEchoOn"), "Space Echo On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("spaceEchoTime"), "Space Echo Time",
        Range (40.0f, 2500.0f, 1.0f, 0.4f), 375.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("spaceEchoRepeats"), "Space Echo Repeats",
        Range (0.0f, 100.0f, 0.1f), 40.0f));
    // Bass/Treble shelving pair, matching the real RE-201's tone stack --
    // replaces a single "Tone" lowpass this used to have (see
    // SpaceEchoModule's Bass/Treble shelf filters). 50 = flat/neutral.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("spaceEchoBass"), "Space Echo Bass",
        Range (0.0f, 100.0f, 0.1f), 50.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("spaceEchoTreble"), "Space Echo Treble",
        Range (0.0f, 100.0f, 0.1f), 50.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("spaceEchoWobble"), "Space Echo Wobble",
        Range (0.0f, 100.0f, 0.1f), 15.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("spaceEchoDrive"), "Space Echo Drive",
        Range (0.0f, 100.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("spaceEchoMix"), "Space Echo Mix",
        Range (0.0f, 100.0f, 0.1f), 30.0f));
    // Deprecated in place, never wired to anything anymore -- briefly drove
    // a built-in reverb tank on the Space Echo pedal, pulled since it was a
    // strictly worse, fixed-parameter copy of the standalone Spring pedal.
    // Left registered (rather than removed) so no saved session/preset that
    // already references this id breaks -- same discipline as odOrder/the
    // *SectionOn params.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("spaceEchoReverb"), "Space Echo Reverb",
        Range (0.0f, 100.0f, 0.1f), 20.0f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("spaceEchoPattern"), "Space Echo Pattern",
        juce::StringArray { "Straight", "Bounce", "Gallop", "Cluster", "Wash", "Ping-Pong" }, 0));

    // --- Reverb: 3 Lexicon 480L hall/room convolutions (HallRoomReverbModule).
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("reverbOn"), "Reverb On", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("reverbModel"), "Reverb Model",
        juce::StringArray { "Room", "Hall", "Plate" }, 1));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("reverbPreDelay"), "Reverb Pre-Delay",
        Range (0.0f, 1.0f, 0.001f), 0.0f));
    // Maps to the reverb tank's comb feedback (see HallRoomReverbModule) --
    // 1.0 = each space's own natural decay ceiling, lower values shorten the
    // tail continuously and live, unlike the old convolution-based design.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("reverbDecay"), "Reverb Decay",
        Range (0.0f, 1.0f, 0.001f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("reverbTone"), "Reverb Tone",
        Range (0.0f, 1.0f, 0.001f), 0.6f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("reverbMix"), "Reverb Mix",
        Range (0.0f, 100.0f, 0.1f), 25.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("reverbWidth"), "Reverb Width",
        Range (0.0f, 100.0f, 0.1f), 50.0f));

    // --- Spring (ported from Rockalizer's SpringModule) --- spring-tank
    // reverb (IR onset + synthesized dispersion/late-field tail), distinct
    // from the algorithmic Hall/Room/Plate reverb above -- a separate
    // pedal, not another Reverb Model choice, since the two have entirely
    // different architectures and can be run together or independently.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("springOn"), "Spring On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("springDecay"), "Spring Decay",
        Range (0.0f, 100.0f, 0.1f), 45.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("springDwell"), "Spring Dwell",
        Range (0.0f, 100.0f, 0.1f), 20.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("springTone"), "Spring Tone",
        Range (0.0f, 100.0f, 0.1f), 55.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("springDrip"), "Spring Drip",
        Range (0.0f, 100.0f, 0.1f), 15.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("springMix"), "Spring Mix",
        Range (0.0f, 100.0f, 0.1f), 30.0f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("springModel"), "Spring Model",
        juce::StringArray { "Space", "9100", "Echomixer" }, 0));

    // --- Redface (ChannelEQModule) --- an original implementation of a
    // classic British console channel EQ's band structure (shelving low +
    // swept-mid peak + shelving high, plus a multi-stop HPF), at the
    // well-documented, publicly published stock frequency/gain spec of
    // that circuit -- see ChannelEQModule.h.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("channelEQOn"), "Redface On", false));
    // Preamp Gain -- first in the signal path, ahead of the EQ bands,
    // matching the real module's own front panel layout. A smooth
    // continuous control here (the real hardware's is a stepped switch,
    // but that only matters for a mic-preamp use case a line-level pedal
    // signal doesn't have).
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("channelEQGain"), "Redface Gain",
        Range (-20.0f, 20.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("channelEQLowFreq"), "Redface Low Freq",
        juce::StringArray { "35 Hz", "60 Hz", "110 Hz", "220 Hz" }, 1));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("channelEQLowGain"), "Redface Low Gain",
        Range (-16.0f, 16.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("channelEQMidFreq"), "Redface Mid Freq",
        juce::StringArray { "360 Hz", "700 Hz", "1.6 kHz", "3.2 kHz", "4.8 kHz", "7.2 kHz" }, 2));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("channelEQMidGain"), "Redface Mid Gain",
        Range (-18.0f, 18.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("channelEQHighGain"), "Redface High Gain",
        Range (-16.0f, 16.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("channelEQHpfOn"), "Redface HPF On", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("channelEQHpfFreq"), "Redface HPF Freq",
        juce::StringArray { "50 Hz", "80 Hz", "160 Hz", "300 Hz" }, 0));

    // --- Desk (DeskModule) --- a console-summing-style coloration stage.
    // Its core curve is a small, explicitly MIT-licensed technique
    // (Copyright (c) airwindows; the exact 1-(1-x)^2 inverse-square
    // formulation credited in Airwindows' own source to "torridgristle"
    // under the MIT license), generalized here to a continuously variable
    // steepness (Style) blended by Amount -- see DeskModule.h. Not a port
    // of any specific Airwindows plugin file.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("deskOn"), "Desk On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("deskAmount"), "Desk Amount",
        Range (0.0f, 100.0f, 0.1f), 35.0f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("deskStyle"), "Desk Style",
        juce::StringArray { "Subtle", "Classic", "Hot" }, 1));

    // --- Parallel Box (ParallelModule via ParallelNode) --- a single fixed
    // two-slot container: any two OTHER pedals can be assigned into Slot A /
    // Slot B, each processed on its own copy of the dry signal and blended
    // back together via Blend, instead of only ever running in series. The
    // pedalboard UI (PedalboardComponent's add-menu + ParallelTile's own
    // slot pickers) keeps a pedal id single-instance across the whole board
    // -- once assigned into a slot here it's removed from the normal
    // serial "+ Add Pedal" pool, and vice versa -- so no pedal id is ever
    // live in two places at once and every param id stays globally unique,
    // exactly like every other pedal already assumes.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("parallelOn"), "Parallel On", false));
    {
        juce::StringArray slotChoices { "None" };
        slotChoices.addArray (PedalboardOrder::parallelSlotChoiceIds());
        params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("parallelSlotA"), "Parallel Slot A",
            slotChoices, 0));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("parallelSlotB"), "Parallel Slot B",
            slotChoices, 0));
    }
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("parallelBlend"), "Parallel Blend",
        Range (0.0f, 100.0f, 0.1f), 50.0f));

    // --- 9-Band Graphic EQ (after the wet effects, before output) ---
    // Page-level bypass for the whole EQ tab (9 bands + HPF/LPF together),
    // independent of eqOn/eqHpfOn/eqLpfOn -- same double-press pattern as
    // preFxSectionOn above.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("eqSectionOn"), "EQ Section On", true));
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("eqOn"), "EQ On", false));
    {
        static const char* bandIds[GraphicEQModule::numBands] = {
            "eqBand1", "eqBand2", "eqBand3", "eqBand4", "eqBand5", "eqBand6", "eqBand7", "eqBand8", "eqBand9"
        };
        const auto& freqs = GraphicEQModule::getCentreFrequencies();
        for (int i = 0; i < GraphicEQModule::numBands; ++i)
        {
            auto freqLabel = freqs[(size_t) i] >= 1000.0f
                ? juce::String (freqs[(size_t) i] / 1000.0f, 1) + "kHz"
                : juce::String ((int) freqs[(size_t) i]) + "Hz";
            params.push_back (std::make_unique<juce::AudioParameterFloat> (pid (bandIds[i]), "EQ " + freqLabel,
                Range (-12.0f, 12.0f, 0.1f), 0.0f));
        }
    }
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("eqHpfOn"), "EQ HPF On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("eqHpfFreq"), "EQ HPF Freq",
        Range (20.0f, 1000.0f, 1.0f), 80.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("eqLpfOn"), "EQ LPF On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("eqLpfFreq"), "EQ LPF Freq",
        Range (1000.0f, 20000.0f, 1.0f), 8000.0f));

    // --- Output ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("outputGain"), "Output Gain",
        Range (-24.0f, 24.0f, 0.1f), 0.0f));

    // --- Master bypass (preset bar power button) ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("masterBypass"), "Bypass", false));

    // --- Mute input (preset bar mute button) ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("inputMute"), "Mute Input", false));

    return { params.begin(), params.end() };
}

void ThreadlineAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    chainRunner.prepare (spec);
}

void ThreadlineAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // --- Master bypass: dry passthrough, skip the whole chain ---
    if (pBool ("masterBypass"))
    {
        chainRunner.updateMetersOnBypass (buffer);
        return;
    }

    // --- Input routing (matches Rockalizer): interface inputs 1/2 are the
    // left/right connectors, then the chosen connector(s) feed both channels.
    if (buffer.getNumChannels() >= 2)
    {
        const auto useInput1 = pBool ("input1On");
        const auto useInput2 = pBool ("input2On");
        auto* left = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        const auto gain = useInput1 && useInput2 ? 0.5f : 1.0f;
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto mono = ((useInput1 ? left[sample] : 0.0f)
                             + (useInput2 ? right[sample] : 0.0f)) * gain;
            left[sample] = mono;
            right[sample] = mono;
        }
    }
    else if (buffer.getNumChannels() == 1 && ! pBool ("input1On"))
    {
        // A mono host exposes connector 1 only; connector 2 is unavailable.
        buffer.clear();
    }

    // --- Mute Input: silence what feeds the chain from here on, without
    // resetting any module's internal state -- a delay/reverb tail already
    // in flight keeps decaying naturally instead of cutting off abruptly.
    if (pBool ("inputMute"))
        buffer.clear();

    // --- The whole pedalboard: Noise Gate through Output Gain, in whatever
    // order the user has chosen (default order matches the plugin's
    // original fixed chain) -- see PedalChainRunner.
    chainRunner.processChain (buffer);
}

bool ThreadlineAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
}

void ThreadlineAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        juce::MemoryOutputStream stream (destData, true);
        state.writeToStream (stream);
    }
}

void ThreadlineAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto tree = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (tree.isValid())
    {
        apvts.replaceState (tree);
        PedalboardOrder::ensureExists (apvts);
        chainRunner.resyncFromPersistedOrder();
    }
}

juce::AudioProcessorEditor* ThreadlineAudioProcessor::createEditor()
{
    return new ThreadlineAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ThreadlineAudioProcessor();
}
