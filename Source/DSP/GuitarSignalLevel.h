#pragma once

// Threadline's shared voltage-domain contract for every guitar-facing
// nonlinear block. A host sample is an ADC-normalised representation of the
// physical voltage captured by the supported Focusrite instrument input; it
// is not itself a voltage. Circuit models must enter/leave the voltage domain
// through these functions exactly once so a pedal feeding an amp is not
// accidentally converted a second time.
namespace GuitarSignalLevel
{
    // Midpoint of Scarlett 2i2 4th Gen (+12.0 dBu) and 3rd Gen (+12.5 dBu)
    // maximum instrument input at minimum hardware gain:
    // 0.775 * 10^(12.25/20) = 3.175 V RMS = 4.49073 V peak.
    inline constexpr float voltsPerDigitalUnit = 4.49073f;
    inline constexpr float digitalUnitsPerVolt = 1.0f / voltsPerDigitalUnit;

    inline constexpr float toVolts (float sample) noexcept
    {
        return sample * voltsPerDigitalUnit;
    }

    inline constexpr float fromVolts (float volts) noexcept
    {
        return volts * digitalUnitsPerVolt;
    }
}
