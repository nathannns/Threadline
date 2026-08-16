#pragma once

#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

// Lock-free peak tracker: audio thread calls updateFrom() each block, message
// thread (editor timer) calls getPeak() to read it back for a meter widget.
class PeakLevel
{
public:
    void updateFrom (const juce::AudioBuffer<float>& buffer)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
        currentPeak.store (peak, std::memory_order_relaxed);
    }

    float getPeak() const noexcept { return currentPeak.load (std::memory_order_relaxed); }

private:
    std::atomic<float> currentPeak { 0.0f };
};
