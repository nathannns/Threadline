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
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("compThreshold"), "Comp Threshold",
        Range (-48.0f, 0.0f, 0.1f), -18.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("compRatio"), "Comp Ratio",
        Range (1.0f, 20.0f, 0.1f), 4.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("compAttack"), "Comp Attack",
        Range (0.1f, 100.0f, 0.1f), 5.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("compRelease"), "Comp Release",
        Range (10.0f, 500.0f, 1.0f), 80.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("compMakeup"), "Comp Makeup",
        Range (0.0f, 24.0f, 0.1f), 0.0f));

    // --- Klon ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("klonOn"), "Klon On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("klonGain"), "Klon Gain",
        Range (0.0f, 1.0f, 0.001f), 0.4f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("klonTreble"), "Klon Treble",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("klonLevel"), "Klon Level",
        Range (0.0f, 1.0f, 0.001f), 0.5f));

    // --- TS9 ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("ts9On"), "TS9 On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ts9Drive"), "TS9 Drive",
        Range (0.0f, 1.0f, 0.001f), 0.4f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ts9Tone"), "TS9 Tone",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ts9Level"), "TS9 Level",
        Range (0.0f, 1.0f, 0.001f), 0.5f));

    // --- Amp (5E3) ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ampDrive"), "Amp Drive",
        Range (0.0f, 1.0f, 0.001f), 0.4f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ampTone"), "Amp Tone",
        Range (0.0f, 1.0f, 0.001f), 0.6f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("ampOutput"), "Amp Output",
        Range (-24.0f, 12.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("ampOversampling"), "Amp Oversampling",
        juce::StringArray { "Off", "2x", "4x" }, 2));

    // --- Cab ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("cabOn"), "Cab On", true));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("cabIRSelect"), "Cab IR",
        juce::StringArray { CabModule::getBuiltInIRName (0), CabModule::getBuiltInIRName (1),
                             CabModule::getBuiltInIRName (2), CabModule::getBuiltInIRName (3),
                             CabModule::getBuiltInIRName (4), CabModule::getBuiltInIRName (5) },
        2 /* default: Medium Mix */));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("cabMix"), "Cab Mix",
        Range (0.0f, 1.0f, 0.001f), 1.0f));

    // --- Tremolo ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("tremOn"), "Tremolo On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("tremAmount"), "Tremolo Amount",
        Range (0.0f, 100.0f, 0.1f), 40.0f));

    // --- Chorus ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("chorusOn"), "Chorus On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("chorusRate"), "Chorus Rate",
        Range (0.05f, 5.0f, 0.01f), 0.6f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("chorusDepth"), "Chorus Depth",
        Range (0.0f, 100.0f, 0.1f), 40.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("chorusWidth"), "Chorus Width",
        Range (0.0f, 100.0f, 0.1f), 60.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("chorusTone"), "Chorus Tone",
        Range (500.0f, 8000.0f, 1.0f), 3500.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("chorusMix"), "Chorus Mix",
        Range (0.0f, 100.0f, 0.1f), 35.0f));

    // --- Echo (Delay) ---
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("echoOn"), "Delay On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("echoTime"), "Delay Time",
        Range (10.0f, 1200.0f, 1.0f), 350.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("echoRepeats"), "Delay Repeats",
        Range (0.0f, 100.0f, 0.1f), 30.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("echoTone"), "Delay Tone",
        Range (500.0f, 8000.0f, 1.0f), 3000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("echoMix"), "Delay Mix",
        Range (0.0f, 100.0f, 0.1f), 25.0f));

    // --- Reverb: 3 spring-tank models (SpringModule) + 7 Lexicon 480L
    // hall/room models (HallRoomReverbModule), picked from one list. Index
    // 0-2 route to the spring engine, 3-9 to the hall/room engine.
    params.push_back (std::make_unique<juce::AudioParameterBool> (pid ("reverbOn"), "Reverb On", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (pid ("reverbModel"), "Reverb Model",
        juce::StringArray { "Space", "9100", "Echomixer",
                             "Large Hall", "Large Stage", "Small Church", "Small Hall",
                             "Small Stage", "Large Room", "Small Room" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("reverbDecay"), "Reverb Decay",
        Range (0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("reverbTone"), "Reverb Tone",
        Range (0.0f, 1.0f, 0.001f), 0.6f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pid ("reverbMix"), "Reverb Mix",
        Range (0.0f, 100.0f, 0.1f), 25.0f));
    // Hall/Room engine only (M/S width of the convolved signal); ignored by
    // the spring models, which have their own built-in stereo dispersion.
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
    setLatencySamples (amps.back().getLatencySamples());
    cab.prepare (spec);
    // Load the selected built-in cabinet IR.
    const auto cabSelection = (int) p ("cabIRSelect");
    if (cabSelection < CabModule::numBuiltInIRs)
        cab.loadBuiltInIR (cabSelection);
    lastCabIRSelection = cabSelection;
    tremolo.prepare (spec);
    chorus.prepare (spec);
    echo.prepare (spec);
    spring.prepare (spec);
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

    // --- Klon ---
    klon.setEnabled (pBool ("klonOn"));
    klon.setParameters (p ("klonGain"), p ("klonTreble"), p ("klonLevel"));
    klon.process (buffer);

    // --- TS9 ---
    ts9.setEnabled (pBool ("ts9On"));
    ts9.setParameters (p ("ts9Drive"), p ("ts9Tone"), p ("ts9Level"));
    ts9.process (buffer);

    // --- Amp ---
    auto& selectedAmp = amps[(size_t) juce::jlimit (0, 2, (int) p ("ampOversampling"))];
    selectedAmp.setParameters (p ("ampDrive"), p ("ampTone"), p ("ampOutput"));
    selectedAmp.process (buffer);

    // --- Cab (IR) ---
    cab.setEnabled (pBool ("cabOn"));
    cab.setMix (p ("cabMix"));
    {
        const auto cabSelection = (int) p ("cabIRSelect");
        if (cabSelection != lastCabIRSelection && cabSelection < CabModule::numBuiltInIRs)
            cab.loadBuiltInIR (cabSelection);
        lastCabIRSelection = cabSelection;
    }
    cab.process (buffer);

    // --- Tremolo ---
    tremolo.setAmount (pBool ("tremOn") ? p ("tremAmount") : 0.0f);
    tremolo.process (buffer);

    // --- Chorus ---
    chorus.setParameters (p ("chorusRate"), p ("chorusDepth"), p ("chorusWidth"),
                           p ("chorusTone"), p ("chorusMix"), pBool ("chorusOn"), 0 /* no flanger */);
    chorus.process (buffer);

    // --- Echo (Delay) ---
    echo.setParameters (p ("echoTime"), p ("echoRepeats"), p ("echoTone"), 0.0f /* wobble */,
                         0.0f /* drive */, p ("echoMix"), pBool ("echoOn"), EchoModule::straight);
    echo.process (buffer);

    // --- Reverb: route to whichever engine the selected model belongs to.
    // Both are always ticked (each is a cheap no-op when its own wet mix is
    // at 0), so only the active one ever does real DSP work.
    {
        const auto reverbModelIndex = (int) p ("reverbModel");
        const auto reverbEnabled = pBool ("reverbOn");
        const auto springActive = reverbEnabled && reverbModelIndex < 3;
        const auto hallRoomActive = reverbEnabled && reverbModelIndex >= 3;

        spring.setParameters (p ("reverbDecay"), 0.2f /* dwell */, p ("reverbTone"), 0.0f /* drip */,
                               p ("reverbMix"), springActive, juce::jlimit (0, 2, reverbModelIndex));
        spring.process (buffer);

        hallRoomReverb.setParameters (p ("reverbDecay") /* reused as pre-delay */, p ("reverbTone"),
                                       p ("reverbMix"), p ("reverbWidth"), hallRoomActive, reverbModelIndex - 3);
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
