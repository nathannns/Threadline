#pragma once

#include <JuceHeader.h>
#include <BinaryData.h>

// Speaker cab simulation via convolution. Ships with six built-in IRs
// (Tweed_Combo_1x12, mic'd multiple ways) selectable by index, plus
// loadImpulseResponseFile() for loading your own external IR on top.
class CabModule
{
public:
    static constexpr int numBuiltInIRs = 6;
    static const char* getBuiltInIRName (int index)
    {
        static const char* names[numBuiltInIRs] = {
            "Bright Mix", "Dark Mix", "Medium Mix", "Medium 57", "Medium 87", "Medium 160"
        };
        return names[juce::jlimit (0, numBuiltInIRs - 1, index)];
    }

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        currentSpec = spec;
        convolution.prepare (spec);
    }

    void reset() { convolution.reset(); }

    void setEnabled (bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    // mix: 0-1, dry/wet. Cabs are usually run fully wet (1.0), but a mix
    // knob is handy for blending in some raw preamp bite.
    void setMix (float mix01) { mixAmount = juce::jlimit (0.0f, 1.0f, mix01); }

    // Loads one of the 6 embedded Tweed_Combo_1x12 IRs. Safe to call from
    // the audio thread (JUCE's Convolution loads impulse data in the
    // background) — used from processBlock when the "cabIRSelect" param
    // changes, so it stays host-automatable/preset-recallable.
    bool loadBuiltInIR (int index)
    {
        index = juce::jlimit (0, numBuiltInIRs - 1, index);
        if (index == loadedBuiltInIndex && ! usingCustomFile)
            return true;

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

        // Same decode-then-load pattern as SpringModule::loadImpulse: read
        // the embedded wav via AudioFormatReader into an AudioBuffer, then
        // hand that buffer to Convolution (rather than guessing at a raw
        // pointer overload).
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (
            new juce::MemoryInputStream (data[index], (size_t) sizes[index], false), true));
        if (reader == nullptr)
            return false;

        // All 6 built-in Tweed_Combo IRs are mono (single-mic recordings).
        // Passing a 1-channel buffer straight to Convolution with
        // Stereo::yes is relying on undocumented mono-source-with-stereo-
        // request behaviour — build a genuine 2-channel buffer (duplicating
        // the mono data to both sides) so both output channels definitely
        // get identical, correct IR data.
        convolution.loadImpulseResponse (makeGuaranteedStereoImpulse (*reader), reader->sampleRate,
            juce::dsp::Convolution::Stereo::yes,
            juce::dsp::Convolution::Trim::yes,
            juce::dsp::Convolution::Normalise::yes);

        loadedBuiltInIndex = index;
        usingCustomFile = false;
        loadedFileName = juce::String (getBuiltInIRName (index));
        hasLoadedIR = true;
        return true;
    }

    // Returns true on success. Call from the message thread (e.g. after a
    // file chooser callback) — JUCE's Convolution handles the background
    // loading itself, safe to call while audio is running.
    bool loadImpulseResponseFile (const juce::File& file)
    {
        if (! file.existsAsFile())
            return false;

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
        if (reader == nullptr)
            return false;

        // Same mono-source guarantee as the built-in IRs — a user's own
        // mono IR file gets duplicated to both channels rather than relying
        // on Convolution's undocumented handling of a 1-channel buffer.
        convolution.loadImpulseResponse (makeGuaranteedStereoImpulse (*reader), reader->sampleRate,
            juce::dsp::Convolution::Stereo::yes,
            juce::dsp::Convolution::Trim::yes,
            juce::dsp::Convolution::Normalise::yes);
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

        if (mixAmount >= 0.999f)
        {
            juce::dsp::AudioBlock<float> block (buffer);
            juce::dsp::ProcessContextReplacing<float> context (block);
            convolution.process (context);
            return;
        }

        // Partial mix: convolve a copy, blend back with dry.
        juce::AudioBuffer<float> wet;
        wet.makeCopyOf (buffer);
        juce::dsp::AudioBlock<float> wetBlock (wet);
        juce::dsp::ProcessContextReplacing<float> wetContext (wetBlock);
        convolution.process (wetContext);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* dry = buffer.getWritePointer (ch);
            auto* w = wet.getWritePointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                dry[i] = dry[i] * (1.0f - mixAmount) + w[i] * mixAmount;
        }
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
    bool hasLoadedIR = false;
    bool usingCustomFile = false;
    int loadedBuiltInIndex = -1;
    float mixAmount = 1.0f;
    juce::String loadedFileName;
};
