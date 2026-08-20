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

    // Resistor + capacitor in series (one port) -- exact equations ported
    // from chowdsp_wdf's ResistorCapacitorSeriesT (wdft_one_ports.h). This
    // is the TS9 feedback network's R4(4.7k)+C3(47nF) leg to ground, the
    // documented source of the Tube Screamer's mid-hump.
    struct ResistorCapacitorSeries
    {
        Port wdf;
        float R_value, C_value, T_over_T_plus_2RC = 0.0f, z = 0.0f, tt = 0.0f;
        ResistorCapacitorSeries (float ohms, float farads, double sampleRate) { R_value = ohms; C_value = farads; prepare (sampleRate); }
        void prepare (double sampleRate)
        {
            tt = 1.0f / (float) sampleRate;
            calcImpedance();
            reset();
        }
        void calcImpedance()
        {
            wdf.R = tt / (2.0f * C_value) + R_value;
            wdf.G = 1.0f / wdf.R;
            T_over_T_plus_2RC = tt / (2.0f * C_value * R_value + tt);
        }
        void reset() { z = 0.0f; wdf.a = wdf.b = 0.0f; }
        void incident (float x) noexcept { wdf.a = x; z -= T_over_T_plus_2RC * (wdf.a + z); }
        float reflected() noexcept { wdf.b = -z; return wdf.b; }
    };

    // Resistor + capacitor in parallel (one port), with a settable
    // resistance -- ported from chowdsp_wdf's ResistorCapacitorParallelT
    // (wdft_one_ports.h). This is the TS9 feedback resistor R6(51k)+Drive
    // pot(500k) bypassed by C4(51pF); Drive moves the resistance directly.
    struct ResistorCapacitorParallel
    {
        Port wdf;
        float R_value, C_value, twoRC_over_twoRC_plus_T = 0.0f, z = 0.0f, tt = 0.0f;
        ResistorCapacitorParallel (float ohms, float farads, double sampleRate) { R_value = ohms; C_value = farads; prepare (sampleRate); }
        void prepare (double sampleRate)
        {
            tt = 1.0f / (float) sampleRate;
            calcImpedance();
            reset();
        }
        void setResistanceValue (float newR)
        {
            R_value = newR;
            calcImpedance();
        }
        void calcImpedance()
        {
            const auto twoRC = 2.0f * C_value * R_value;
            wdf.R = R_value * tt / (twoRC + tt);
            wdf.G = 1.0f / wdf.R;
            twoRC_over_twoRC_plus_T = twoRC / (twoRC + tt);
        }
        void reset() { z = 0.0f; wdf.a = wdf.b = 0.0f; }
        void incident (float x) noexcept { wdf.a = x; z = wdf.b + wdf.a - z; }
        float reflected() noexcept { wdf.b = twoRC_over_twoRC_plus_T * z; return wdf.b; }
    };

    // Voltage source with a series capacitance -- ported from chowdsp_wdf's
    // CapacitiveVoltageSourceT (wdft_sources.h). The TS9's input coupling
    // cap C2(1uF): setVoltage() stages the drive signal, and the reflected
    // wave carries it through the capacitor's one-sample delay.
    struct CapacitiveVoltageSource
    {
        Port wdf;
        float C_value, z = 0.0f, v_0 = 0.0f, v_1 = 0.0f;
        double sampleRate = 44100.0;
        CapacitiveVoltageSource (float farads, double sr) { C_value = farads; prepare (sr); }
        void prepare (double sr)
        {
            sampleRate = sr;
            wdf.R = 1.0f / (2.0f * C_value * (float) sampleRate);
            wdf.G = 1.0f / wdf.R;
            reset();
        }
        void setVoltage (float newV) noexcept { v_0 = newV; }
        void reset() { z = 0.0f; v_0 = v_1 = 0.0f; wdf.a = wdf.b = 0.0f; }
        void incident (float x) noexcept { wdf.a = x; z = wdf.a; }
        float reflected() noexcept { wdf.b = z + v_0 - v_1; v_1 = v_0; return wdf.b; }
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

    // General multi-port (R-type) adaptor for the TS9's op-amp clipping
    // stage: models the op-amp's FINITE open-loop gain Ag, input resistance
    // Ri, and output resistance Ro as a single 4-port element with 1 adapted
    // (root-facing) port plus 3 down ports. This is the piece the previous
    // ideal-op-amp TS9Clipper deliberately skipped, because its 4x4
    // scattering matrix is the symbolic result of Chowdhury-DSP's R-Solver
    // tool (github.com/jatinchowdhury18/R-Solver) and had to be ported
    // character-for-character rather than hand-derived. That matrix is
    // below, copied verbatim from BYOD's TubeScreamerWDF.h (struct
    // ImpedanceCalc) -- not re-derived.
    //
    // Scattering convention matches chowdsp_wdf exactly: S[out][in], b = S a.
    // Port order (rows/cols A,B,C,D as in BYOD): 0 = adapted upward port,
    // 1 = port B (inverting input network, Rb), 2 = port C (feedback-ground
    // leg R4+C3, Rc), 3 = port D (output load RL, Rd). Constants Ag=100,
    // Ri=1e9, Ro=0.1 are BYOD's own op-amp model values.
    template <typename PortB, typename PortC, typename PortD>
    struct RTypeAdaptor
    {
        PortB& portB;
        PortC& portC;
        PortD& portD;
        Port wdf;
        float S[4][4] {};
        float avec[4] {};
        static constexpr float Ag = 100.0f;
        static constexpr float Ri = 1.0e9f;
        static constexpr float Ro = 0.1f;

        RTypeAdaptor (PortB& b, PortC& c, PortD& d) : portB (b), portC (c), portD (d) { calcImpedance(); }

        void calcImpedance()
        {
            const auto Rb = portB.wdf.R;
            const auto Rc = portC.wdf.R;
            const auto Rd = portD.wdf.R;

            // Shared denominators (each appears in several matrix entries).
            const auto den1 = (Rb + Rc) * Rd + Rd * Ri - (Rb + Rc + Ri) * Ro;
            const auto den2 = (Ag + 1.0f) * Rc * Rd * Ri + Rb * Rc * Rd - (Rb * Rc + (Rb + Rc) * Rd + (Rc + Rd) * Ri) * Ro;
            const auto den3 = (Ag + 1.0f) * Rc * Rd * Ri * Ri + ((Ag + 2.0f) * Rb * Rc + (Ag + 1.0f) * Rc * Rc) * Rd * Ri + (Rb * Rb * Rc + Rb * Rc * Rc) * Rd - (Rb * Rb * Rc + Rb * Rc * Rc + (Rc + Rd) * Ri * Ri + (Rb * Rb + 2.0f * Rb * Rc + Rc * Rc) * Rd + (2.0f * Rb * Rc + Rc * Rc + 2.0f * (Rb + Rc) * Rd) * Ri) * Ro;
            const auto den4 = (Ag + 1.0f) * Rc * Rd * Rd * Ri * Ri + ((Ag + 2.0f) * Rb * Rc + (Ag + 1.0f) * Rc * Rc) * Rd * Rd * Ri + (Rb * Rb * Rc + Rb * Rc * Rc) * Rd * Rd + (Rb * Rb * Rc + Rb * Rc * Rc + (Rc + Rd) * Ri * Ri + (Rb * Rb + 2.0f * Rb * Rc + Rc * Rc) * Rd + (2.0f * Rb * Rc + Rc * Rc + 2.0f * (Rb + Rc) * Rd) * Ri) * Ro * Ro - ((Rb * Rb + 2.0f * Rb * Rc + Rc * Rc) * Rd * Rd + ((Ag + 2.0f) * Rc * Rd + Rd * Rd) * Ri * Ri + 2.0f * (Rb * Rb * Rc + Rb * Rc * Rc) * Rd + (2.0f * (Rb + Rc) * Rd * Rd + ((Ag + 4.0f) * Rb * Rc + (Ag + 2.0f) * Rc * Rc) * Rd) * Ri) * Ro;
            const auto den5 = (Ag + 1.0f) * Rc * Rd * Rd * Ri + Rb * Rc * Rd * Rd + (Rb * Rc + (Rb + Rc) * Rd + (Rc + Rd) * Ri) * Ro * Ro - (2.0f * Rb * Rc * Rd + (Rb + Rc) * Rd * Rd + ((Ag + 2.0f) * Rc * Rd + Rd * Rd) * Ri) * Ro;

            S[0][0] = 0.0f;
            S[0][1] = (Ag * Rd * Ri - Rc * Rd + Rc * Ro) / den1;
            S[0][2] = -((Ag + 1.0f) * Rd * Ri + Rb * Rd - (Rb + Ri) * Ro) / den1;
            S[0][3] = -Ro / (Rd - Ro);

            S[1][0] = -(Rb * Rc * Rd - Rb * Rc * Ro) / den2;
            S[1][1] = ((Ag + 1.0f) * Rc * Rc * Rd * Ri + (Ag + 1.0f) * Rc * Rd * Ri * Ri - Rb * Rb * Rc * Rd + (Rb * Rb * Rc - (Rc + Rd) * Ri * Ri + (Rb * Rb - Rc * Rc) * Rd - (Rc * Rc + 2.0f * Rc * Rd) * Ri) * Ro) / den3;
            S[1][2] = ((Ag + 1.0f) * Rb * Rc * Rd * Ri + Rb * Rb * Rc * Rd - (Rb * Rb * Rc + 2.0f * (Rb * Rb + Rb * Rc) * Rd + (Rb * Rc + 2.0f * Rb * Rd) * Ri) * Ro) / den3;
            S[1][3] = -Rb * Rc * Ro / den2;

            S[2][0] = -(Rb * Rc * Rd + Rc * Rd * Ri - (Rb * Rc + Rc * Ri) * Ro) / den2;
            S[2][1] = (Ag * Rc * Rd * Ri * Ri + Rb * Rc * Rc * Rd + (Ag * Rb * Rc + (2.0f * Ag + 1.0f) * Rc * Rc) * Rd * Ri - (Rb * Rc * Rc + 2.0f * (Rb * Rc + Rc * Rc) * Rd + (Rc * Rc + 2.0f * Rc * Rd) * Ri) * Ro) / den3;
            S[2][2] = -((Ag + 1.0f) * Rc * Rc * Rd * Ri + Rb * Rc * Rc * Rd - (Rb * Rc * Rc - Rd * Ri * Ri - (Rb * Rb - Rc * Rc) * Rd + (Rc * Rc - 2.0f * Rb * Rd) * Ri) * Ro) / den3;
            S[2][3] = -(Rb * Rc + Rc * Ri) * Ro / den2;

            S[3][0] = (Ag * Rc * Rd * Ri - ((Rb + Rc) * Rd + Rd * Ri) * Ro) / den2;
            S[3][1] = ((Ag * Ag + 2.0f * Ag) * Rc * Rd * Rd * Ri * Ri + (2.0f * Ag * Rb * Rc + Ag * Rc * Rc) * Rd * Rd * Ri + (Rc * Rd * Ri + (Rb * Rc + Rc * Rc) * Rd) * Ro * Ro - ((Rb * Rc + Rc * Rc) * Rd * Rd + (2.0f * Ag * Rc * Rd + Ag * Rd * Rd) * Ri * Ri + ((Ag * Rb + (Ag + 1.0f) * Rc) * Rd * Rd + (2.0f * Ag * Rb * Rc + Ag * Rc * Rc) * Rd) * Ri) * Ro) / den4;
            S[3][2] = -(Ag * Rb * Rc * Rd * Rd * Ri + (Ag * Ag + Ag) * Rc * Rd * Rd * Ri * Ri - ((2.0f * Rb + Rc) * Rd * Ri + Rd * Ri * Ri + (Rb * Rb + Rb * Rc) * Rd) * Ro * Ro + ((Rb * Rb + Rb * Rc) * Rd * Rd - (Ag * Rc * Rd + (Ag - 1.0f) * Rd * Rd) * Ri * Ri - (Ag * Rb * Rc * Rd + ((Ag - 2.0f) * Rb + (Ag - 1.0f) * Rc) * Rd * Rd) * Ri) * Ro) / den4;
            S[3][3] = -((Ag + 1.0f) * Rc * Rd * Rd * Ri + Rb * Rc * Rd * Rd - (Rb * Rc + Rc * Ri) * Ro * Ro - ((Rb + Rc) * Rd * Rd + Rd * Rd * Ri) * Ro) / den5;

            // Adapted (upward) port resistance -- BYOD's Ra.
            wdf.R = den2 / den1;
            wdf.G = 1.0f / wdf.R;
        }

        void incident (float x) noexcept
        {
            avec[0] = x;
            portB.incident (S[1][0] * avec[0] + S[1][1] * avec[1] + S[1][2] * avec[2] + S[1][3] * avec[3]);
            portC.incident (S[2][0] * avec[0] + S[2][1] * avec[1] + S[2][2] * avec[2] + S[2][3] * avec[3]);
            portD.incident (S[3][0] * avec[0] + S[3][1] * avec[1] + S[3][2] * avec[2] + S[3][3] * avec[3]);
        }

        float reflected() noexcept
        {
            avec[1] = portB.reflected();
            avec[2] = portC.reflected();
            avec[3] = portD.reflected();
            wdf.b = S[0][0] * avec[0] + S[0][1] * avec[1] + S[0][2] * avec[2] + S[0][3] * avec[3];
            return wdf.b;
        }
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
