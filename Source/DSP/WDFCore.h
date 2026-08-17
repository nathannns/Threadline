#pragma once

#include <cmath>
#include <type_traits>

// A small, from-scratch port of the Wave Digital Filter primitives needed
// by KlonModule and TS9Module's clipping stages -- real circuit simulation
// (each passive element's own frequency-dependent behaviour, and a genuine
// diode I-V law solved in closed form) in place of a hand-fit asinh curve.
//
// Ported from and verified against two real, published, MIT-licensed
// reference implementations by Jatin Chowdhury / Chowdhury DSP -- not
// derived from scratch, so the adaptor scattering rules and the Wright
// Omega diode solve below match a working, shipped implementation exactly:
//   - github.com/jatinchowdhury18/KlonCentaur (the exact circuit KlonModule
//     below is built from: ChowCentaur/GainStageProcessors/ClippingStage.h)
//   - github.com/Chowdhury-DSP/chowdsp_wdf (the general WDF template
//     library those plugins are built on: series/parallel adaptors,
//     resistor/capacitor/voltage-source one-ports, and the diode-pair
//     nonlinearity -- wdft_adaptors.h, wdft_one_ports.h, wdft_sources.h,
//     wdft_nonlinearities.h, math/omega.h)
// This is a deliberately smaller subset (float only, no SIMD, no generic
// R-type/multi-port adaptor) -- just what the two clipper topologies below
// actually need.
namespace WDF
{
    // Wright Omega function, solving w + ln(w) = x for w -- the closed-form
    // (no per-sample Newton-Raphson iteration needed) solution to the
    // diode pair's transcendental I-V equation. Stefano D'Angelo's 4th-order
    // rational/log approximation plus one Halley-style correction step, as
    // published in "An Efficient Real-Time Implementation of the Wright
    // Omega Function" (DAFx 2019) and used verbatim (same coefficients) in
    // both reference repos cited above.
    inline float wrightOmega (float x) noexcept
    {
        float y;
        if (x < -3.341459552768620f)
            y = 0.0f;
        else if (x < 8.0f)
        {
            // 3rd-order rational/polynomial region (Estrin's scheme, unrolled).
            constexpr float a = -1.314293149877800e-3f;
            constexpr float b = 4.775931364975583e-2f;
            constexpr float c = 3.631952663804445e-1f;
            constexpr float d = 6.313183464296682e-1f;
            y = d + x * (c + x * (b + x * a));
        }
        else
            y = x - std::log (x);

        // One correction step: refines the 3rd-order estimate to 4th-order
        // accuracy (the "omega4" step in both reference implementations).
        return y - (y - std::exp (x - y)) / (y + 1.0f);
    }

    // Common state every one-port element carries: adapted port resistance
    // R (and conductance G=1/R), plus this sample's incident/reflected waves.
    struct Port
    {
        float R = 1.0e-9f;
        float G = 1.0f / 1.0e-9f;
        float a = 0.0f, b = 0.0f;
    };

    // Ideal resistor: ohmic element that also *defines* the port's
    // reference resistance, so it absorbs all incident energy (b=0).
    struct Resistor
    {
        Port wdf;
        explicit Resistor (float ohms) { setResistance (ohms); }
        void setResistance (float ohms) { wdf.R = ohms; wdf.G = 1.0f / ohms; }
        void incident (float x) noexcept { wdf.a = x; }
        float reflected() noexcept { wdf.b = 0.0f; return wdf.b; }
    };

    // Capacitor via the bilinear transform: port resistance R=1/(2*fs*C),
    // and (the well-known WDF result for this discretisation) the reflected
    // wave is just last sample's incident wave -- a capacitor is a
    // one-sample delay in the wave domain.
    struct Capacitor
    {
        Port wdf;
        float z = 0.0f;
        float farads = 1.0e-6f;
        Capacitor (float capacitanceFarads, double sampleRate) { prepare (capacitanceFarads, sampleRate); }
        void prepare (float capacitanceFarads, double sampleRate)
        {
            farads = capacitanceFarads;
            wdf.R = 1.0f / (2.0f * farads * (float) sampleRate);
            wdf.G = 1.0f / wdf.R;
        }
        void reset() { z = 0.0f; wdf.a = wdf.b = 0.0f; }
        void incident (float x) noexcept { wdf.a = x; z = x; }
        float reflected() noexcept { wdf.b = z; return wdf.b; }
    };

    // Voltage source with series resistance Rs, adapted (port R = Rs) so
    // the reflected wave is simply the source voltage.
    struct ResistiveVoltageSource
    {
        Port wdf;
        float Vs = 0.0f;
        explicit ResistiveVoltageSource (float seriesOhms) { wdf.R = seriesOhms; wdf.G = 1.0f / seriesOhms; }
        void setVoltage (float v) noexcept { Vs = v; }
        void incident (float x) noexcept { wdf.a = x; }
        float reflected() noexcept { wdf.b = Vs; return wdf.b; }
    };

    // Current source with parallel resistance Rp (a Norton source) --
    // reflected wave is R*Is, same adapted-port trick as the voltage source.
    struct ResistiveCurrentSource
    {
        Port wdf;
        float Is = 0.0f;
        explicit ResistiveCurrentSource (float parallelOhms) { wdf.R = parallelOhms; wdf.G = 1.0f / parallelOhms; }
        void setCurrent (float current) noexcept { Is = current; }
        void incident (float x) noexcept { wdf.a = x; }
        float reflected() noexcept { wdf.b = wdf.R * Is; return wdf.b; }
    };

    // 3-port series adaptor (port1 and port2 combined into one parent
    // port, R_parent = R1+R2). Exact scattering rules ported from
    // chowdsp_wdf's WDFSeriesT (wdft_adaptors.h).
    template <typename Port1, typename Port2>
    struct Series
    {
        Port1& port1;
        Port2& port2;
        Port wdf;
        float port1Reflect = 1.0f;

        Series (Port1& p1, Port2& p2) : port1 (p1), port2 (p2) { calcImpedance(); }

        void calcImpedance()
        {
            wdf.R = port1.wdf.R + port2.wdf.R;
            wdf.G = 1.0f / wdf.R;
            port1Reflect = port1.wdf.R / wdf.R;
        }

        void incident (float x) noexcept
        {
            const auto b1 = port1.wdf.b - port1Reflect * (x + port1.wdf.b + port2.wdf.b);
            port1.incident (b1);
            port2.incident (-(x + b1));
            wdf.a = x;
        }

        float reflected() noexcept
        {
            wdf.b = -(port1.reflected() + port2.reflected());
            return wdf.b;
        }
    };

    // 3-port parallel adaptor (1/R_parent = 1/R1+1/R2). Exact scattering
    // rules ported from chowdsp_wdf's WDFParallelT (wdft_adaptors.h).
    template <typename Port1, typename Port2>
    struct Parallel
    {
        Port1& port1;
        Port2& port2;
        Port wdf;
        float port1Reflect = 1.0f;
        float bDiff = 0.0f;

        Parallel (Port1& p1, Port2& p2) : port1 (p1), port2 (p2) { calcImpedance(); }

        void calcImpedance()
        {
            wdf.G = port1.wdf.G + port2.wdf.G;
            wdf.R = 1.0f / wdf.G;
            port1Reflect = port1.wdf.G / wdf.G;
        }

        void incident (float x) noexcept
        {
            const auto b2 = wdf.b - port2.wdf.b + x;
            port1.incident (b2 + bDiff);
            port2.incident (b2);
            wdf.a = x;
        }

        float reflected() noexcept
        {
            port1.reflected();
            port2.reflected();
            bDiff = port2.wdf.b - port1.wdf.b;
            wdf.b = port2.wdf.b - port1Reflect * bDiff;
            return wdf.b;
        }
    };

    // Flips the sign of a source's voltage wave -- same port resistance,
    // used where a component needs to see the source with reversed polarity
    // (matches chowdsp_wdf's PolarityInverterT, needed to reproduce the
    // Klon clipper's exact topology).
    template <typename PortT>
    struct PolarityInverter
    {
        PortT& port1;
        Port wdf;
        explicit PolarityInverter (PortT& p) : port1 (p) { wdf.R = port1.wdf.R; wdf.G = 1.0f / wdf.R; }
        void incident (float x) noexcept { wdf.a = x; port1.incident (-x); }
        float reflected() noexcept { wdf.b = -port1.reflected(); return wdf.b; }
    };

    // Antiparallel diode pair (the root nonlinear element -- terminates the
    // WDF tree rather than being one leg of a Series/Parallel adaptor).
    // Solves the pair's transcendental I-V relation in closed form via the
    // Wright Omega function above ("Best"/eqn 39 quality from Werner et
    // al., "An Improved and Generalized Diode Clipper Model for Wave
    // Digital Filters" -- the same equation chowdsp_wdf's DiodePairT uses).
    template <typename NextT>
    struct DiodePair
    {
        NextT& next;
        float Is, Vt, twoVt, oneOverVt;
        float R_Is = 0.0f, R_Is_overVt = 0.0f, logR_Is_overVt = 0.0f;
        float a = 0.0f, b = 0.0f;

        DiodePair (NextT& n, float saturationCurrent, float thermalVoltage)
            : next (n), Is (saturationCurrent), Vt (thermalVoltage),
              twoVt (2.0f * Vt), oneOverVt (1.0f / Vt)
        {
            calcImpedance();
        }

        void calcImpedance()
        {
            R_Is = next.wdf.R * Is;
            R_Is_overVt = R_Is * oneOverVt;
            logR_Is_overVt = std::log (R_Is_overVt);
        }

        void incident (float x) noexcept { a = x; }

        float reflected() noexcept
        {
            const auto lambda = a < 0.0f ? -1.0f : 1.0f;
            const auto lambda_a_over_vt = lambda * a * oneOverVt;
            b = a - twoVt * lambda * (wrightOmega (logR_Is_overVt + lambda_a_over_vt)
                                     - wrightOmega (logR_Is_overVt - lambda_a_over_vt));
            return b;
        }
    };

    inline float voltage (const Port& p) noexcept { return (p.a + p.b) * 0.5f; }
    inline float current (const Port& p) noexcept { return (p.a - p.b) * (0.5f * p.G); }
}
