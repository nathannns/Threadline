# Threadline

Amp sim + drive stack, VST3 / AU / Standalone, one JUCE codebase.

**Chain:** Noise Gate → Klon → TS9 → Amp (5E3 Tweed Deluxe-inspired) → Cab (your own IR) → Tremolo → Chorus → Echo → Spring Reverb

## What's reused from Rockalizer vs. new

Pulled straight from your Rockalizer repo, unmodified:
- `ChorusModule`, `EchoModule`, `SpringModule` (+ its 3 embedded spring IRs), `TremoloModule`

Built new for Threadline:
- `NoiseGateModule` — same algorithm as Rockalizer's inline gate, just pulled
  out of `PluginProcessor` into its own reusable class so both projects can
  use it.
- `KlonModule` — treble-boost pre-emphasis into asymmetric germanium-style
  soft clipping, blended against the clean signal (that clean/driven blend
  is what makes a Klon-style circuit sound "transparent" rather than fuzzy).
- `TS9Module` — symmetric silicon-diode-style clipping plus the ~720 Hz
  mid-hump tone stack the TS9 is known for.
- `AmpModule` — two cascaded gain stages (12AY7-style preamp, brighter/
  harder clip → 6V6-style power stage, softer/more compressed clip with
  power-supply sag modeled as a slow envelope follower pulling down power
  stage gain under sustained drive) and a **single** tone knob, since the
  real 5E3 circuit only has one treble-cut control, not a 3-band EQ.
- `CabModule` — `juce::dsp::Convolution` loading whatever IR file you point
  it at via the in-UI file chooser. No bundled cab IRs.

## Build

You said you've already got JUCE, CMake, and Ninja — but this pins JUCE to
**8.0.15** via `FetchContent` to match the version the reused Rockalizer
modules were built against, rather than pointing at your local JUCE install
(version mismatches between JUCE 7.x/8.x can silently break `dsp::Convolution`
and `SmoothedValue` behavior, so pinning is the safer default here).

If you'd rather use your local JUCE anyway: open `CMakeLists.txt`, comment
out the `FetchContent` block, and uncomment the two lines above it that call
`find_package(JUCE CONFIG REQUIRED)` with your JUCE path.

```bash
cd Threadline
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

First configure clones JUCE (~500MB) — a few minutes. After that, Ninja
incremental builds are fast.

Standalone app: `build/Threadline_artefacts/Release/Standalone/Threadline.app`
VST3/AU are copied automatically to `~/Library/Audio/Plug-Ins/` — rescan in
Cubase afterward.

## Using the cab

Threadline ships with no bundled cab IRs. Click **Load Cab IR...** in the
plugin UI and point it at a `.wav` impulse response — from AmpliTube/TONEX
exports, a free IR pack, or your own captures. Cab defaults to 100% wet
(pure IR); the Mix knob lets you blend in some raw preamp signal if you want
more bite/presence than the IR alone gives you.

## Known gaps / next steps

- **Overdrive order is fixed** (Klon → TS9 → Amp). If you want them
  swappable or stackable in either order, that's a straightforward change —
  worth doing once you've heard which order you actually prefer.
- **Amp model is a single voice.** No Bass/Treble/Middle 3-band stack by
  design (5E3 authenticity), but if you want an alternate "modern" voicing
  with full EQ as a second amp model, that's a separate `AmpModule` variant
  rather than a change to this one.
- **Echo wobble/drive are hardcoded to 0** in `processBlock` right now (not
  exposed as knobs yet) — trivial to wire up if you want tape-style pitch
  wobble on the echo repeats.
- UI is functional, not skinned — no custom knob art or pedal graphics like
  Rockalizer has. Given you already have that visual language built, it's
  probably worth porting those assets over once the DSP side feels right,
  rather than guessing at a look here.
