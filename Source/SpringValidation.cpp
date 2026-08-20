// Numeric verification harness for "Rockalizer" (spring reverb) -- not part of
// the shipping plugin (separate console-app target in CMakeLists.txt).
// Instantiates the real SpringModule (production code, no mocks) and sweeps
// decay x dwell x tone x drip x mix x impulse-model x amplitude x frequency at
// 48/96/192k for NaN/Inf/blow-up. The risky parts are the two always-on
// feedback loops -- the per-spring dispersion-line drive and the 4-line
// orthogonal tail FDN, whose tailFeedback runs up to ~0.97 at long Decay --
// both ADAA-tanh rail-limited. The convolution IR is loaded asynchronously via
// AsyncUpdater, so the sweep drives the message-manager dispatch loop once per
// impulse model (after prepare + setParameters) to let loadImpulse() run
// before processing; the dispersive/tail network runs regardless, so blow-up
// detection does not depend on the IR arriving.
#include <JuceHeader.h>
#include "DSP/SpringModule.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    bool sweepForBlowup (double sampleRate, float ceiling)
    {
        // Console app: no dedicated message thread, so drive the dispatch loop
        // on this thread to let the module's async IR loads complete.
        auto& mm = *juce::MessageManager::getInstance();

        static const float decays[] = { 0.0f, 100.0f };
        static const float dwells[] = { 0.0f, 100.0f };
        static const float tones[]  = { 0.0f, 100.0f };
        static const float drips[]  = { 0.0f, 100.0f };
        static const float mixes[]  = { 0.0f, 100.0f };
        static const float amps[]   = { 0.1f, 0.5f, 2.0f };
        static const float freqs[]  = { 40.0f, 440.0f, 4000.0f };

        float peak = 0.0f;
        for (int impulseIndex = 0; impulseIndex < 3; ++impulseIndex)
        {
            SpringModule mod;
            juce::dsp::ProcessSpec spec { sampleRate, 256, 2 };
            mod.prepare (spec);                                       // queues async load (impulse 0)
            mod.setParameters (50.0f, 50.0f, 50.0f, 50.0f, 100.0f, true, impulseIndex);
            mm.runDispatchLoopUntil (100);                            // flush the IR load to completion
            mm.runDispatchLoopUntil (10);                             // settle any self-retrigger

            for (float decay : decays)
                for (float dwell : dwells)
                    for (float tone : tones)
                        for (float drip : drips)
                            for (float mix : mixes)
                                for (float amp : amps)
                                    for (float freq : freqs)
                                    {
                                        // impulseIndex unchanged -> no async reload per combo.
                                        mod.setParameters (decay, dwell, tone, drip, mix, true, impulseIndex);
                                        for (int block = 0; block < 64; ++block)
                                        {
                                            juce::AudioBuffer<float> buf (2, 256);
                                            for (int i = 0; i < 256; ++i)
                                            {
                                                const double phase = 2.0 * kPi * (double) freq
                                                                   * (double) (block * 256 + i) / sampleRate;
                                                const auto v = (float) (amp * std::sin (phase));
                                                buf.setSample (0, i, v);
                                                buf.setSample (1, i, v);
                                            }
                                            mod.process (buf);
                                            for (int ch = 0; ch < 2; ++ch)
                                                for (int i = 0; i < 256; ++i)
                                                {
                                                    const auto val = buf.getSample (ch, i);
                                                    if (! std::isfinite (val))
                                                    {
                                                        std::printf ("  !! NaN/Inf at decay=%.0f dwell=%.0f tone=%.0f drip=%.0f mix=%.0f model=%d amp=%.1f freq=%.0f\n",
                                                                     decay, dwell, tone, drip, mix, impulseIndex, amp, freq);
                                                        return false;
                                                    }
                                                    const auto mag = std::fabs (val);
                                                    if (mag > peak) peak = mag;
                                                    if (mag > ceiling)
                                                    {
                                                        std::printf ("  !! unbounded (%.2f > %.1f) at decay=%.0f mix=%.0f model=%d amp=%.1f\n",
                                                                     mag, ceiling, decay, mix, impulseIndex, amp);
                                                        return false;
                                                    }
                                                }
                                        }
                                    }
        }
        std::printf ("  ok (no NaN/Inf; peak |out| = %.4f)\n", peak);
        return true;
    }
}

int main()
{
    std::printf ("SpringModule (Rockalizer, spring reverb) numeric verification\n");
    std::printf ("=============================================================\n\n");

    std::printf ("Stability sweep (NaN/Inf/blow-up), ceiling 100:\n");
    const double rates[] = { 48000.0, 96000.0, 192000.0 };
    const char* rateNames[] = { "48k", "96k", "192k" };
    for (int r = 0; r < 3; ++r)
    {
        std::printf ("  %s: ", rateNames[r]);
        if (! sweepForBlowup (rates[r], 100.0f))
            return 1;
    }
    std::printf ("\nAll checks passed.\n");
    return 0;
}
