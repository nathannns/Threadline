#pragma once

#include <JuceHeader.h>
#include <BinaryData.h>

// Speaker cab simulation via convolution. Ships with six built-in IRs
// (Tweed_Combo_1x12, mic'd multiple ways) selectable by index, plus
// loadImpulseResponseFile() for loading your own external IR on top.
//
// Built-in IR switches are dispatched through AsyncUpdater rather than
// decoding the WAV and building FFT partitions inline: the old code did
// that work synchronously on whatever thread called loadBuiltInIR(), which
// was the *audio* thread (PluginProcessor::processBlock calls it directly
// when the cabIRSelect parameter changes) — exactly the kind of blocking
// work that causes an audible dropout on a real-time thread. Every load now
// happens on the message thread; process() also fades the newly-swapped IR
// in over a few milliseconds, masking any residual internal-state
// discontinuity from the engine swap itself.
class CabModule : private juce::AsyncUpdater
{
public:
    ~CabModule() override { cancelPendingUpdate(); }


    static constexpr int numBuiltInIRs = 6;
    static const char* getBuiltInIRName (int index)
    {
        static const char* names[numBuiltInIRs] = {
            "Spark Blend", "Velvet Blend", "Balanced Blend", "Edge 57", "Air 87", "Silk 160"
        };
        return names[juce::jlimit (0, numBuiltInIRs - 1, index)];
    }

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        currentSpec = spec;
        convolution.prepare (spec);
        fadeInTotalSamples = juce::jmax (1, (int) (spec.sampleRate * 0.015)); // ~15ms
        fadeInRemaining = 0;
    }

    void reset() { convolution.reset(); }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    // mix: 0-1, dry/wet. Cabs are usually run fully wet (1.0), but a mix
    // knob is handy for blending in some raw preamp bite.
    void setMix (float mix01) { mixAmount = juce::jlimit (0.0f, 1.0f, mix01); }

    // Manual polarity flip. Onset alignment (see alignOnset()) fixes *timing*
    // misalignment between two different IR captures automatically, but
    // absolute polarity (was this particular mic wired in-phase?) isn't
    // something that's detectable from the IR data alone — if two blended
    // slots still sound thin/hollow after alignment, this is the escape
    // hatch, same as the phase button on a mixing console.
    void setPhaseInverted (bool shouldInvert) { phaseInverted = shouldInvert; }

    // Requests one of the 6 embedded Tweed_Combo_1x12 IRs. Non-blocking and
    // safe to call from the audio thread — the actual decode + FFT
    // partition build happens later on the message thread via
    // handleAsyncUpdate(), not here. Used from processBlock when the
    // "cabIRSelect" param changes, so it stays host-automatable/
    // preset-recallable without ever blocking real-time audio.
    bool loadBuiltInIR (int index)
    {
        index = juce::jlimit (0, numBuiltInIRs - 1, index);
        if (index == loadedBuiltInIndex && ! usingCustomFile)
            return true;

        requestedBuiltInIndex.store (index);
        triggerAsyncUpdate();
        return true;
    }

    // Call from the message thread (e.g. after a file chooser callback or a
    // combo-box selection) — already off the audio thread, so this decodes
    // and loads immediately rather than round-tripping through
    // triggerAsyncUpdate(), but still gets the same fade-in treatment.
    bool loadImpulseResponseFile (const juce::File& file)
    {
        if (! file.existsAsFile())
            return false;

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
        if (reader == nullptr)
            return false;

        applyImpulse (*reader);
        loadedFileName = file.getFileName();
        usingCustomFile = true;
        hasLoadedIR = true;
        return true;
    }

    bool isLoaded() const noexcept { return hasLoadedIR; }
    juce::String getLoadedFileName() const { return loadedFileName; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled || ! hasLoadedIR)
            return;

        // A fresh IR was just swapped in on the message thread — fade the
        // wet signal in over ~15ms rather than jumping straight to full
        // level, so any residual discontinuity in the convolution engine's
        // internal (overlap-save) history from the swap itself is masked
        // rather than heard as a click.
        juce::AudioBuffer<float> wet;
        wet.makeCopyOf (buffer);
        juce::dsp::AudioBlock<float> wetBlock (wet);
        juce::dsp::ProcessContextReplacing<float> wetContext (wetBlock);
        convolution.process (wetContext);

        const auto polarity = phaseInverted ? -1.0f : 1.0f;
        const auto numSamples = buffer.getNumSamples();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* dry = buffer.getWritePointer (ch);
            auto* w = wet.getWritePointer (ch);
            auto fadeRemainingForChannel = fadeInRemaining;
            for (int i = 0; i < numSamples; ++i)
            {
                auto wetGain = mixAmount;
                if (fadeRemainingForChannel > 0)
                {
                    wetGain *= 1.0f - (float) fadeRemainingForChannel / (float) fadeInTotalSamples;
                    --fadeRemainingForChannel;
                }
                dry[i] = dry[i] * (1.0f - wetGain) + (w[i] * polarity) * wetGain;
            }
        }
        fadeInRemaining = juce::jmax (0, fadeInRemaining - numSamples);
    }

    // Folder the "Custom..." IR list is scanned from, in place of a one-shot
    // OS file-chooser dialog — drop .wav files in here and they show up
    // alongside the built-ins, the way dedicated IR-loader plugins (e.g.
    // Cabinetron, Nembrini IR Loader) present a browsable library instead of
    // repeated ad-hoc file picks.
    static juce::File getUserIRFolder()
    {
        auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                          .getChildFile ("Threadline").getChildFile ("Cab IRs");
        folder.createDirectory();
        return folder;
    }

    static juce::Array<juce::File> getUserIRFiles()
    {
        juce::Array<juce::File> files;
        for (const auto& entry : juce::RangedDirectoryIterator (getUserIRFolder(), false, "*.wav"))
            files.add (entry.getFile());
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()) < 0;
        });
        return files;
    }

private:
    // Runs on the message thread in response to loadBuiltInIR()'s
    // triggerAsyncUpdate() — this is where the actual WAV decode + FFT
    // partition build happens, off the audio thread.
    void handleAsyncUpdate() override
    {
        const auto index = requestedBuiltInIndex.exchange (-1);
        if (index < 0)
            return;

        static const void* data[numBuiltInIRs] = {
            BinaryData::Tweed_Combo_1x12_Bright_Mix_wav,
            BinaryData::Tweed_Combo_1x12_Dark_Mix_wav,
            BinaryData::Tweed_Combo_1x12_Medium_Mix_wav,
            BinaryData::Tweed_Combo_1x12_Medium_57_wav,
            BinaryData::Tweed_Combo_1x12_Medium_87_wav,
            BinaryData::Tweed_Combo_1x12_Medium_160_wav
        };
        static const int sizes[numBuiltInIRs] = {
            BinaryData::Tweed_Combo_1x12_Bright_Mix_wavSize,
            BinaryData::Tweed_Combo_1x12_Dark_Mix_wavSize,
            BinaryData::Tweed_Combo_1x12_Medium_Mix_wavSize,
            BinaryData::Tweed_Combo_1x12_Medium_57_wavSize,
            BinaryData::Tweed_Combo_1x12_Medium_87_wavSize,
            BinaryData::Tweed_Combo_1x12_Medium_160_wavSize
        };

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (
            new juce::MemoryInputStream (data[index], (size_t) sizes[index], false), true));
        if (reader == nullptr)
            return;

        applyImpulse (*reader);
        loadedBuiltInIndex = index;
        usingCustomFile = false;
        loadedFileName = juce::String (getBuiltInIRName (index));
        hasLoadedIR = true;
    }

    // Shared by the built-in and custom-file paths: loads the impulse
    // (mono sources duplicated to a genuine 2-channel buffer, per the
    // Stereo::yes fix below), onset-aligned so two different IRs blended
    // together (Cab A/B) stay phase-coherent, and arms the fade-in in
    // process().
    void applyImpulse (juce::AudioFormatReader& reader)
    {
        auto impulse = alignOnset (makeGuaranteedStereoImpulse (reader));
        convolution.loadImpulseResponse (std::move (impulse), reader.sampleRate,
            juce::dsp::Convolution::Stereo::yes,
            juce::dsp::Convolution::Trim::yes,
            juce::dsp::Convolution::Normalise::yes);
        fadeInRemaining = fadeInTotalSamples;
    }

    // Different mic'd IRs (close vs. room, different mic models/distances)
    // naturally start at different points in the buffer — a few samples to
    // a few dozen samples of pure silence/pre-roll before the transient
    // actually hits. When two such IRs are convolved and summed (Cab A + Cab
    // B blend in PluginProcessor), that timing offset is exactly what
    // produces comb-filtering: the same signal arriving at two slightly
    // different times cancels at frequencies whose half-period matches the
    // offset. Trimming every loaded IR down to a small, consistent lead-in
    // ahead of its detected onset removes that source of misalignment
    // without needing any cross-instance coordination between cabA/cabB —
    // each independently normalizes to the same relative reference point.
    static juce::AudioBuffer<float> alignOnset (juce::AudioBuffer<float> impulse)
    {
        const auto numSamples = impulse.getNumSamples();
        if (numSamples <= 0)
            return impulse;

        float peak = 0.0f;
        for (int ch = 0; ch < impulse.getNumChannels(); ++ch)
        {
            auto* data = impulse.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                peak = juce::jmax (peak, std::abs (data[i]));
        }
        if (peak <= 0.0f)
            return impulse;

        const auto threshold = peak * 0.15f; // 15% of peak: standard onset-detection threshold
        int onset = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            bool crossed = false;
            for (int ch = 0; ch < impulse.getNumChannels() && ! crossed; ++ch)
                if (std::abs (impulse.getSample (ch, i)) >= threshold)
                    crossed = true;
            if (crossed) { onset = i; break; }
        }

        constexpr int leadInSamples = 8; // small guard so the attack's rise isn't chopped off
        const auto trimStart = juce::jmax (0, onset - leadInSamples);
        if (trimStart <= 0)
            return impulse;

        const auto trimmedLength = numSamples - trimStart;
        juce::AudioBuffer<float> trimmed (impulse.getNumChannels(), trimmedLength);
        for (int ch = 0; ch < impulse.getNumChannels(); ++ch)
            trimmed.copyFrom (ch, 0, impulse, ch, trimStart, trimmedLength);
        return trimmed;
    }

    // Reads the full IR from `reader` into a genuinely 2-channel buffer —
    // if the source is mono, both output channels get an identical copy of
    // it, rather than handing Convolution a 1-channel buffer and hoping it
    // duplicates it correctly under Stereo::yes.
    static juce::AudioBuffer<float> makeGuaranteedStereoImpulse (juce::AudioFormatReader& reader)
    {
        const auto length = juce::jmax (1, (int) reader.lengthInSamples);
        const auto sourceChannels = juce::jlimit (1, 2, (int) reader.numChannels);

        juce::AudioBuffer<float> source (sourceChannels, length);
        reader.read (&source, 0, length, 0, true, sourceChannels > 1);

        juce::AudioBuffer<float> stereo (2, length);
        stereo.copyFrom (0, 0, source, 0, 0, length);
        stereo.copyFrom (1, 0, source, sourceChannels > 1 ? 1 : 0, 0, length);
        return stereo;
    }

    // Non-uniform partitioning (short head block, larger tail blocks) keeps
    // latency on the direct/attack portion of the IR low — matters for a
    // guitar cab sim, where a laggy pick attack is immediately noticeable,
    // versus a single large uniform FFT block that would trade CPU for
    // latency evenly across the whole IR.
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 256 } };
    juce::dsp::ProcessSpec currentSpec {};
    bool enabled = true;
    bool phaseInverted = false;
    bool hasLoadedIR = false;
    bool usingCustomFile = false;
    int loadedBuiltInIndex = -1;
    float mixAmount = 1.0f;
    juce::String loadedFileName;

    std::atomic<int> requestedBuiltInIndex { -1 };
    int fadeInRemaining = 0, fadeInTotalSamples = 1;
};
