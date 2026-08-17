# Threadline

Amp sim + drive stack, VST3 / AU / Standalone, one JUCE codebase.

**Chain:** Noise Gate → Input Gain → Compressor → Klon → Breaker (TS9/TS808/TS10) → Amp (5E3 Tweed Deluxe-inspired) → Cab (your own IR) → Tremolo → July (Chorus/Vibrato) → Plexy (Echo) → Reverb (Hall/Room) → 9-Band EQ → Output Gain

Klon and Breaker can run in either order ahead of the Amp (each keeps its own
on/off toggle regardless) via the Overdrive Order switch.

## Modules

- `NoiseGateModule` — inline gate shared with Rockalizer.
- `CompressorModule` — Diamond-style optical compressor with a two-stage
  vactrol release curve (fast initial recovery, slower tail).
- `KlonModule` — treble-boost pre-emphasis into asymmetric germanium-style
  soft clipping, blended against the clean signal (that clean/driven blend
  is what makes a Klon-style circuit sound "transparent" rather than fuzzy).
- `TS9Module` ("Breaker" in the UI) — switchable TS9/TS808/TS10 variants,
  each with the appropriate symmetric silicon-diode-style clipping and tone
  stack differences.
- `AmpModule` — two cascaded gain stages (12AY7-style preamp, brighter/
  harder clip → 6V6-style power stage, softer/more compressed clip with
  power-supply sag modeled as a slow envelope follower pulling down power
  stage gain under sustained drive) and a **single** tone knob, since the
  real 5E3 circuit only has one treble-cut control, not a 3-band EQ.
- `CabModule` — `juce::dsp::Convolution` loading whatever IR file you point
  it at via the in-UI file chooser. No bundled cab IRs. Two parallel IR
  slots (A/B) blend from the same dry signal, like two mics on one cab.
- `TremoloModule` — bias-modulation tremolo, modeled on the real tube gm
  curve's asymmetric throb (fast dip, gentler recovery), single Amount knob.
- `ChorusModule` ("July" in the UI) — modeled on the Julia pedal's exact
  control surface: Rate, Depth, Lag (LFO center delay time), a Sine/Triangle
  waveform switch, and a Dry/Chorus/Vibrato 3-way switch in place of a
  continuous knob.
- `EchoModule` ("Plexy" in the UI) — modeled on the Maestro Echoplex EP-3's
  control surface: Time, Sustain (feedback), Volume, and an Echo /
  Sound-on-Sound mode switch. The real unit's always-on preamp coloration,
  tape wow/flutter, and feedback-linked saturation are fixed characteristics
  here rather than separate Tone/Wobble/Drive knobs it doesn't have. Sustain
  reaches genuine self-oscillation at maximum, same as real hardware.
- `HallRoomReverbModule` — algorithmic (not convolution) reverb: a classic
  Freeverb-style parallel-comb + series-allpass tank, tuned to 4 spaces
  modeled on the Boss RV-6's modes: **Room** (warm, small/dense), **Hall**
  (clear, spacious), **Plate** (metallic, extended highs — higher allpass
  diffusion plus a fixed brightness bias, not just a Tone setting), and
  **Shimmer** (an extra external feedback loop pitch-shifts the tank's own
  previous output up an octave and feeds it back in, cascading the tail
  upward on top of the normal decay). Tone controls each comb's internal
  damping (highs decay faster than lows in the tail, real air-absorption
  behaviour); Decay controls comb feedback within a per-space ceiling, live
  and continuous. The original Lexicon-480L-captured IRs this module used to
  convolve against are still in `Resources/ImpulseResponses/HallRoom/` but
  are no longer loaded.
- `GraphicEQModule` — 9-band post-effects EQ plus switchable HPF/LPF.
- `PresetManager` — real save/load to disk-backed XML presets (one file per
  preset). Ships with 6 factory presets covering clean, edge-of-breakup,
  driven lead, vibrato, ambient, and tight rhythm tones.

Several modules started as direct ports from the Rockalizer repo
(`ChorusModule`, `EchoModule`, `TremoloModule`) but have since diverged
significantly — July and Plexy in particular are now built around specific
real pedals' control surfaces rather than Rockalizer's more generic
versions.

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

- **Amp model is a single voice.** No Bass/Treble/Middle 3-band stack by
  design (5E3 authenticity), but if you want an alternate "modern" voicing
  with full EQ as a second amp model, that's a separate `AmpModule` variant
  rather than a change to this one.
- UI is functional, not skinned — no custom knob art or pedal graphics like
  Rockalizer has. Given you already have that visual language built, it's
  probably worth porting those assets over once the DSP side feels right,
  rather than guessing at a look here.
