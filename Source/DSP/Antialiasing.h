#pragma once

#include <cmath>

// First-order antiderivative antialiasing (ADAA) for memoryless
// nonlinearities used inside a feedback loop (EchoModule's feedback
// saturation, EchoModule/CarbonCopyModule's feedback safety rail). Evaluating
// a nonlinearity like tanh() directly, one sample at a time, aliases: the
// nonlinearity's own harmonics extend well above what the current sample
// rate can represent whenever consecutive samples move it by very much, and
// inside a feedback loop that harmonic content recirculates and compounds on
// every repeat. Oversampling the whole delay line to fix this is awkward --
// the delay buffer's own indexing would need to track the oversampled rate
// too -- so this uses ADAA instead: the exact average slope of the
// nonlinearity's antiderivative F between the previous sample and this one,
// (F(x2)-F(x1))/(x2-x1), which is mathematically equivalent to bandlimiting
// the nonlinearity's output, at zero added latency (Parker, Zavalishin,
// Bilbao, Valimaki, "Antiderivative Antialiasing for Memoryless
// Nonlinearities," DAFx-16). Falls back to evaluating the nonlinearity
// directly at the analytic midpoint when consecutive samples are too close
// together for that secant to stay numerically well-conditioned, per the
// same paper's standard treatment.

// log(cosh(x)), the antiderivative of tanh(x) -- computed via the stable
// form |x| + log1p(exp(-2|x|)) - log(2) rather than naively logging cosh(x)
// itself, which overflows cosh(x) for even moderately large |x| (double-
// digit x is routine here under heavy Sustain/Regen self-oscillation).
inline float logCoshStable (float x) noexcept
{
    const auto ax = std::abs (x);
    return ax + std::log1p (std::exp (-2.0f * ax)) - 0.6931472f;
}

class AdaaTanh
{
public:
    void reset() noexcept { x1 = 0.0f; primed = false; }

    float process (float x2) noexcept
    {
        if (! primed)
        {
            x1 = x2;
            primed = true;
            return std::tanh (x2);
        }
        const auto denom = x2 - x1;
        const auto y = std::abs (denom) > 1.0e-4f
            ? (logCoshStable (x2) - logCoshStable (x1)) / denom
            : std::tanh (0.5f * (x1 + x2));
        x1 = x2;
        return y;
    }

private:
    float x1 = 0.0f;
    bool primed = false;
};

// Same ADAA treatment for the odd, C1 soft-knee limiter both delay modules
// use as their feedback-loop safety rail -- identity below `knee`, a
// tanh-shaped soft knee out to `ceiling` beyond it (matching first
// derivative at the seam, so it's smooth, not just continuous). It only
// engages its nonlinear branch near self-oscillation, but that's exactly the
// regime where the harmonic content -- and therefore the aliasing risk -- is
// highest.
class AdaaSmoothRail
{
public:
    void reset() noexcept { x1 = 0.0f; primed = false; }

    float process (float x2, float knee, float ceiling) noexcept
    {
        if (! primed)
        {
            x1 = x2;
            primed = true;
            return evaluate (x2, knee, ceiling);
        }
        const auto denom = x2 - x1;
        const auto y = std::abs (denom) > 1.0e-4f
            ? (antiderivative (x2, knee, ceiling) - antiderivative (x1, knee, ceiling)) / denom
            : evaluate (0.5f * (x1 + x2), knee, ceiling);
        x1 = x2;
        return y;
    }

private:
    static float evaluate (float value, float knee, float ceiling) noexcept
    {
        const auto magnitude = std::abs (value);
        if (magnitude <= knee)
            return value;
        const auto range = ceiling - knee;
        return std::copysign (knee + range * std::tanh ((magnitude - knee) / range), value);
    }

    // The rail is odd, so its antiderivative is even: H(x) = x^2/2 inside
    // the linear region, and knee^2/2 + knee*(|x|-knee) +
    // range^2*log(cosh((|x|-knee)/range)) beyond it -- the running total of
    // the linear region plus the integral of the tanh soft knee past `knee`.
    static float antiderivative (float value, float knee, float ceiling) noexcept
    {
        const auto magnitude = std::abs (value);
        if (magnitude <= knee)
            return 0.5f * magnitude * magnitude;
        const auto range = ceiling - knee;
        const auto excess = magnitude - knee;
        return 0.5f * knee * knee + knee * excess + range * range * logCoshStable (excess / range);
    }

    float x1 = 0.0f;
    bool primed = false;
};
