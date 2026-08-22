#pragma once

#include <atomic>

// Per-plugin-instance handoff for the oversampling mode resolved by the chain
// runner. This avoids a process-global flag when several Threadline instances
// render at once. Nodes read it once per block; no allocation or lock occurs.
class ProcessingQualityState
{
public:
    void setEffectiveOversamplingMode (int mode) noexcept
    {
        effectiveOversamplingMode.store (mode < 0 ? 0 : (mode > 2 ? 2 : mode),
                                         std::memory_order_relaxed);
    }

    int getEffectiveOversamplingMode() const noexcept
    {
        return effectiveOversamplingMode.load (std::memory_order_relaxed);
    }

private:
    std::atomic<int> effectiveOversamplingMode { 2 };
};
