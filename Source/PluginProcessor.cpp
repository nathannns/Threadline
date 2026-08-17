#include "PluginProcessor.h"
#include "PluginEditor.h"

ThreadlineAudioProcessor::ThreadlineAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
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

    // --- Klon ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("klonOn"), "Klon On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("klonGain"), "Klon Gain",
        Range (0.0f, 1.0f, 0.001f), 0.4f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("klonTreble"), "Klon Treble",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("klonLevel"), "Klon Level",
        Range (0.0f, 1.0f, 0.001f), 0.5f));

    // --- Overdrive stage order: Klon and TS9 (Breaker) can run in either
    // order ahead of the Amp. Each stage keeps its own on/off toggle either
    // way — this only decides which one the guitar signal hits first.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("odOrder"), "Overdrive Order",
        juce::StringArray { "Klon -> Breaker", "Breaker -> Klon" }, 0));

    // --- TS9 ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("ts9On"), "TS9 On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ts9Drive"), "TS9 Drive",
        Range (0.0f, 1.0f, 0.001f), 0.4f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ts9Tone"), "TS9 Tone",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ts9Level"), "TS9 Level",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("ts9Variant"), "TS9 Variant",
        juce::StringArray { "TS9", "TS808", "TS10" }, 0));

    // --- Amp (5E3) ---
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
    // underneath, different tone section, not a different amp model.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("ampVoice"), "Amp Voice",
        juce::StringArray { "Vintage 5E3", "Boutique" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ampBass"), "Amp Bass",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ampMid"), "Amp Mid",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ampTreble"), "Amp Treble",
        Range (0.0f, 1.0f, 0.001f), 0.5f));

    // --- Cab: two IR slots processed in parallel (like two mics on the same
    // cab), each independently on/off with its own IR + internal wet/dry
    // mix, blended together by cabBlend (0 = all A, 100 = all B).
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("cabAOn"), "Cab A On", true));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("cabAIRSelect"), "Cab A IR",
        juce::StringArray { CabModule::getBuiltInIRName (0), CabModule::getBuiltInIRName (1),
                             CabModule::getBuiltInIRName (2), CabModule::getBuiltInIRName (3),
                             CabModule::getBuiltInIRName (4), CabModule::getBuiltInIRName (5) },
        2 /* default: Medium Mix */));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("cabAMix"), "Cab A Mix",
        Range (0.0f, 1.0f, 0.001f), 1.0f));
    // Onset alignment (CabModule::alignOnset) handles *timing* differences
    // between IRs automatically, but absolute polarity isn't detectable
    // from the IR data alone — this is the manual safety-net toggle for it.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("cabAPhase"), "Cab A Phase", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("cabBOn"), "Cab B On", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("cabBIRSelect"), "Cab B IR",
        juce::StringArray { CabModule::getBuiltInIRName (0), CabModule::getBuiltInIRName (1),
                             CabModule::getBuiltInIRName (2), CabModule::getBuiltInIRName (3),
                             CabModule::getBuiltInIRName (4), CabModule::getBuiltInIRName (5) },
        0 /* default: Bright Mix — deliberately different from A's default */));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("cabBMix"), "Cab B Mix",
        Range (0.0f, 1.0f, 0.001f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("cabBPhase"), "Cab B Phase", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("cabBlend"), "Cab A/B Blend",
        Range (0.0f, 100.0f, 0.1f), 50.0f));

    // --- Tremolo ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("tremOn"), "Tremolo On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("tremAmount"), "Tremolo Amount",
        Range (0.0f, 100.0f, 0.1f), 40.0f));

    // --- Chorus ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("chorusOn"), "Chorus On", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("chorusFlangerMode"), "Flanger Mode",
        juce::StringArray { "Off", "Mode I", "Mode II", "Mode III (I + II)" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("chorusRate"), "Chorus Rate",
        Range (0.05f, 5.0f, 0.01f, 0.35f), 0.32f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("chorusDepth"), "Chorus Depth",
        Range (0.0f, 100.0f, 0.1f), 42.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("chorusWidth"), "Chorus Width",
        Range (0.0f, 100.0f, 0.1f), 80.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("chorusTone"), "Chorus Tone",
        Range (1000.0f, 16000.0f, 1.0f, 0.35f), 8000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("chorusMix"), "Chorus Mix",
        Range (0.0f, 100.0f, 0.1f), 20.0f));

    // --- Echo (Delay) ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("echoOn"), "Delay On", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("echoSync"), "Delay Sync", false));
    // Names kept as the friendlier originals; underneath, each pattern's tap
    // ratios now match the Roland RE-201 Space Echo's actual fixed, equally-
    // spaced 3-head tape geometry (delay ratio exactly 1:2:3 off head 1's
    // time) instead of the previous made-up ratios. Ping-Pong is a new,
    // explicitly modern bonus mode — the real RE-201 is mono.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("echoPattern"), "Delay Pattern",
        juce::StringArray { "Straight", "Bounce", "Gallop", "Cluster", "Wash", "Ping-Pong" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("echoDivision"), "Delay Division",
        juce::StringArray { "1/4", "1/4 D", "1/8", "1/8 D", "1/8 T", "1/16", "1/16 D", "1/16 T" }, 2));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("echoTime"), "Delay Time",
        Range (40.0f, 1200.0f, 1.0f, 0.35f), 375.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("echoRepeats"), "Delay Repeats",
        Range (0.0f, 100.0f, 0.1f), 30.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("echoTone"), "Delay Tone",
        Range (1200.0f, 14000.0f, 1.0f, 0.35f), 6500.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("echoWobble"), "Delay Wobble",
        Range (0.0f, 100.0f, 0.1f), 30.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("echoDrive"), "Delay Drive",
        Range (0.0f, 100.0f, 0.1f), 30.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("echoMix"), "Delay Mix",
        Range (0.0f, 100.0f, 0.1f), 25.0f));

    // --- Reverb: 7 Lexicon 480L hall/room convolutions (HallRoomReverbModule).
    // The old Rockalizer spring-tank models (Space/9100/Echomixer) have been
    // retired — SpringModule is no longer part of the chain.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("reverbOn"), "Reverb On", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("reverbModel"), "Reverb Model",
        juce::StringArray { "Large Hall", "Large Stage", "Small Church", "Small Hall",
                             "Small Stage", "Large Room", "Small Room" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("reverbPreDelay"), "Reverb Pre-Delay",
        Range (0.0f, 1.0f, 0.001f), 0.0f));
    // Shapes the loaded IR's own tail (see HallRoomReverbModule) rather than
    // the live signal — there's no single "t=0" to decay from in a
    // continuously-fed convolution, so real convolution reverbs adjust decay
    // by re-enveloping the captured impulse itself ("shaped convolution").
    // 1.0 = the room's full natural captured decay; lower values trim the
    // tail shorter with a smooth taper — this can only shorten the space,
    // not lengthen it past what was actually recorded.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("reverbDecay"), "Reverb Decay",
        Range (0.0f, 1.0f, 0.001f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("reverbTone"), "Reverb Tone",
        Range (0.0f, 1.0f, 0.001f), 0.6f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("reverbMix"), "Reverb Mix",
        Range (0.0f, 100.0f, 0.1f), 25.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("reverbWidth"), "Reverb Width",
        Range (0.0f, 100.0f, 0.1f), 50.0f));

    // --- 9-Band Graphic EQ (after the wet effects, before output) ---
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

    return { params.begin(), params.end() };
}

void ThreadlineAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };

    noiseGate.prepare (spec);
    compressor.prepare (spec);
    klon.prepare (spec);
    ts9.prepare (spec);
    for (int mode = 0; mode < (int) amps.size(); ++mode)
        amps[(size_t) mode].prepare (spec, mode);
    // Klon and TS9 now also oversample their clip stage (see their headers),
    // so their latency adds to Amp's in series — all three sit one after
    // another in the chain. Amp's own latency is pinned to its 4x-mode
    // instance regardless of the currently selected oversampling setting
    // (see the comment further down where it's selected), so the reported
    // total stays constant across a live oversampling-mode switch too.
    setLatencySamples (klon.getLatencySamples() + ts9.getLatencySamples() + amps.back().getLatencySamples());
    cabA.prepare (spec);
    cabB.prepare (spec);
    // Load each slot's selected built-in cabinet IR.
    const auto cabASelection = (int) p ("cabAIRSelect");
    if (cabASelection < CabModule::numBuiltInIRs)
        cabA.loadBuiltInIR (cabASelection);
    lastCabAIRSelection = cabASelection;

    const auto cabBSelection = (int) p ("cabBIRSelect");
    if (cabBSelection < CabModule::numBuiltInIRs)
        cabB.loadBuiltInIR (cabBSelection);
    lastCabBIRSelection = cabBSelection;
    tremolo.prepare (spec);
    chorus.prepare (spec);
    echo.prepare (spec);
    hallRoomReverb.prepare (spec);
    graphicEQ.prepare (spec);
}

void ThreadlineAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // --- Master bypass: dry passthrough, skip the whole chain ---
    if (pBool ("masterBypass"))
    {
        inputLevel.updateFrom (buffer);
        outputLevel.updateFrom (buffer);
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

    // --- Noise Gate ---
    noiseGate.setEnabled (pBool ("gateOn"));
    noiseGate.setAmount (p ("gateAmount"));
    noiseGate.process (buffer);

    // --- Input Gain (+ Guitar/Line calibration offset) ---
    const auto calibrationDb = ((int) p ("inputSource") == 0) ? 12.0f : 0.0f; // Guitar : Line
    buffer.applyGain (juce::Decibels::decibelsToGain (p ("inputGain") + calibrationDb));

    // --- Input Meter (taps signal right here, pre-compressor) ---
    inputLevel.updateFrom (buffer);

    // --- Compressor ---
    compressor.setEnabled (pBool ("compOn"));
    compressor.setParameters (p ("compThreshold"), p ("compRatio"), p ("compAttack"),
                               p ("compRelease"), p ("compMakeup"));
    compressor.process (buffer);

    // --- Klon + TS9 (Breaker): order ahead of the Amp is swappable via
    // odOrder — each stage's own on/off toggle still applies regardless of
    // which one the signal hits first.
    klon.setEnabled (pBool ("klonOn"));
    klon.setParameters (p ("klonGain"), p ("klonTreble"), p ("klonLevel"));
    ts9.setEnabled (pBool ("ts9On"));
    ts9.setVariant (static_cast<TS9Module::Variant> (juce::jlimit (0, 2, (int) p ("ts9Variant"))));
    ts9.setParameters (p ("ts9Drive"), p ("ts9Tone"), p ("ts9Level"));

    if ((int) p ("odOrder") == 0)
    {
        klon.process (buffer);
        ts9.process (buffer);
    }
    else
    {
        ts9.process (buffer);
        klon.process (buffer);
    }

    // --- Amp ---
    auto& selectedAmp = amps[(size_t) juce::jlimit (0, 2, (int) p ("ampOversampling"))];
    selectedAmp.setParameters (p ("ampDrive"), p ("ampTone"), p ("ampOutput"),
        static_cast<AmpModule::Voice> (juce::jlimit (0, 1, (int) p ("ampVoice"))),
        p ("ampBass"), p ("ampMid"), p ("ampTreble"));
    selectedAmp.process (buffer);

    // --- Cab (IR): two slots processed in parallel from the same dry
    // signal (like two mics on one cab, not two cabs chained in series),
    // then blended — each slot's own on/off and internal mix still apply.
    {
        cabA.setEnabled (pBool ("cabAOn"));
        cabA.setMix (p ("cabAMix"));
        cabA.setPhaseInverted (pBool ("cabAPhase"));
        const auto cabASelection = (int) p ("cabAIRSelect");
        if (cabASelection != lastCabAIRSelection && cabASelection < CabModule::numBuiltInIRs)
            cabA.loadBuiltInIR (cabASelection);
        lastCabAIRSelection = cabASelection;

        cabB.setEnabled (pBool ("cabBOn"));
        cabB.setMix (p ("cabBMix"));
        cabB.setPhaseInverted (pBool ("cabBPhase"));
        const auto cabBSelection = (int) p ("cabBIRSelect");
        if (cabBSelection != lastCabBIRSelection && cabBSelection < CabModule::numBuiltInIRs)
            cabB.loadBuiltInIR (cabBSelection);
        lastCabBIRSelection = cabBSelection;

        juce::AudioBuffer<float> bufferB;
        bufferB.makeCopyOf (buffer);
        cabA.process (buffer);   // buffer now holds A's result (dry if A is off)
        cabB.process (bufferB);  // bufferB now holds B's result (dry if B is off)

        const auto blend = juce::jlimit (0.0f, 1.0f, p ("cabBlend") * 0.01f);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* a = buffer.getWritePointer (ch);
            auto* b = bufferB.getReadPointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                a[i] = a[i] * (1.0f - blend) + b[i] * blend;
        }
    }

    // --- Tremolo: Rockalizer activation/reset topology ---
    const auto tremoloActive = pBool ("tremOn") && p ("tremAmount") > 0.001f;
    if (tremoloActive)
    {
        tremolo.setAmount (p ("tremAmount"));
        tremolo.process (buffer);
    }
    else if (tremoloWasActive)
        tremolo.reset();
    tremoloWasActive = tremoloActive;

    // --- Chorus ---
    const auto chorusActive = pBool ("chorusOn");
    if (chorusActive)
    {
        chorus.setParameters (p ("chorusRate"), p ("chorusDepth"), p ("chorusWidth"),
                              p ("chorusTone"), p ("chorusMix"), true,
                              (int) p ("chorusFlangerMode"));
        chorus.process (buffer);
    }
    else if (chorusWasActive)
        chorus.reset();
    chorusWasActive = chorusActive;

    // --- Echo (Delay) ---
    const auto echoActive = pBool ("echoOn");
    auto echoTime = p ("echoTime");
    if (echoActive && pBool ("echoSync"))
    {
        auto bpm = 120.0;
        if (auto* playHead = getPlayHead())
            if (auto position = playHead->getPosition())
                if (auto hostBpm = position->getBpm()) bpm = *hostBpm;
        constexpr float beats[] { 1.0f, 1.5f, 0.5f, 0.75f, 1.0f / 3.0f,
                                  0.25f, 0.375f, 1.0f / 6.0f };
        const auto division = juce::jlimit (0, 7, (int) p ("echoDivision"));
        if (std::abs (bpm - cachedTempoBpm) > 0.0001 || division != cachedEchoDivision)
        {
            cachedTempoBpm = bpm;
            cachedEchoDivision = division;
            cachedSyncedEchoMs = static_cast<float> (60000.0 / bpm) * beats[division];
        }
        echoTime = cachedSyncedEchoMs;
    }
    if (echoActive)
    {
        echo.setParameters (echoTime, p ("echoRepeats"), p ("echoTone"), p ("echoWobble"),
                            p ("echoDrive"), p ("echoMix"), true, (int) p ("echoPattern"));
        echo.process (buffer);
        echoWasActive = true;
    }
    else if (echoWasActive)
    {
        echo.setParameters (echoTime, p ("echoRepeats"), p ("echoTone"), p ("echoWobble"),
                            p ("echoDrive"), p ("echoMix"), false, (int) p ("echoPattern"));
        echo.process (buffer);
        if (! echo.isWetTransitionActive())
        {
            echo.reset();
            echoWasActive = false;
        }
    }

    // --- Reverb (Hall/Room only — the spring models are retired) ---
    {
        const auto reverbEnabled = pBool ("reverbOn");
        hallRoomReverb.setParameters (p ("reverbPreDelay"), p ("reverbDecay"), p ("reverbTone"),
                                       p ("reverbMix"), p ("reverbWidth"), reverbEnabled,
                                       (int) p ("reverbModel"));
        hallRoomReverb.process (buffer);
    }

    // --- 9-Band Graphic EQ ---
    {
        graphicEQ.setEnabled (pBool ("eqOn"));
        std::array<float, GraphicEQModule::numBands> bandGains {
            p ("eqBand1"), p ("eqBand2"), p ("eqBand3"), p ("eqBand4"), p ("eqBand5"),
            p ("eqBand6"), p ("eqBand7"), p ("eqBand8"), p ("eqBand9")
        };
        graphicEQ.setBandGains (bandGains);
        graphicEQ.setHighPass (pBool ("eqHpfOn"), p ("eqHpfFreq"));
        graphicEQ.setLowPass (pBool ("eqLpfOn"), p ("eqLpfFreq"));
        graphicEQ.process (buffer);
    }

    // --- Output Gain ---
    buffer.applyGain (juce::Decibels::decibelsToGain (p ("outputGain")));

    // --- Output Meter ---
    outputLevel.updateFrom (buffer);
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
        apvts.replaceState (tree);
}

juce::AudioProcessorEditor* ThreadlineAudioProcessor::createEditor()
{
    return new ThreadlineAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ThreadlineAudioProcessor();
}
