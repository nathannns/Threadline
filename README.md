# Threadline

Amp sim + drive stack, VST3 / AU / Standalone, one JUCE codebase.

**Chain:** Noise Gate → Input Gain → Compressor → Klon → Breaker (TS9/TS808/TS10) → Amp (5E3 Tweed Deluxe-inspired) → Cab (your own IR) → Tremolo → July (Chorus/Vibrato) → Delay (Plexer or Copier, user-selectable) → Reverb (Hall/Room) → 9-Band EQ → Output Gain

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
- **Delay** — one shared section/on-off toggle, two selectable engines
  (`delayModel`); only the active engine's knobs matter for the sound. Both
  engines' feedback knobs (Sustain / Regen) are derived the same way below
  their self-oscillation zone: a target repeat count `N` (how many times
  you want to hear it before it's gone, roughly -40dB down) gives feedback
  `g = 10^(-2/N)`, rather than an arbitrary coefficient curve:
  - `EchoModule` ("Plexer" in the UI) — modeled on the Maestro Echoplex
    EP-3's control surface: Time, Sustain (feedback), Volume, and an Echo /
    Sound-on-Sound mode switch. The real unit's always-on preamp coloration,
    tape wow/flutter, and feedback-linked saturation are fixed
    characteristics here rather than separate Tone/Wobble/Drive knobs it
    doesn't have. Sustain reaches genuine self-oscillation at maximum, same
    as real hardware.
  - `CarbonCopyModule` ("Copier" in the UI) — modeled on the MXR Carbon
    Copy's control surface: Time, Regen (feedback), Mix, and a Mod toggle.
    A fixed lowpass filter sits inside the feedback path itself (not a Tone
    knob the real unit doesn't have), so repeats get progressively darker
    with each pass — the defining trait of a real bucket-brigade analog
    delay versus a clean digital one. Mod adds a slow, modest chorus-like
    delay-time wobble when switched on. Regen reaches near-self-oscillation
    at maximum, same as real hardware. Mixing is a straightforward
    crossfade, unlike Plexer's additive/always-colored EP-3 behaviour.
- `HallRoomReverbModule` — algorithmic (not convolution) reverb: a faithful
  port of JUCE's own `juce::Reverb` (itself what HISE's shipped SimpleReverb
  effect wraps rather than rolling its own) -- 8 parallel combs + 4 series
  allpasses per channel, using its exact proven constants (0.015 input gain,
  0.5 allpass feedback, the same `wetScaleFactor`/damping-in-the-feedback-
  path placement) rather than guessed ones. Two earlier custom versions of
  this module (independent combs with a guessed input gain, then an 8-line
  Householder FDN) both needed Mix pushed unrealistically high to hear
  anything and still didn't sound quite right -- tracing that back to a real
  reference implementation instead of inventing more constants fixed both
  problems at once. On top of that, each comb's feedback and damping are
  now derived from an explicit RT60 (reverberation time) target instead of
  one coefficient shared across all 8 differently-sized lines -- since a
  comb's real-time decay rate depends on both its gain and how often it
  loops (loop period = line length / sample rate), a shared coefficient
  gives every line a *different* actual decay time. The fix is the
  standard Schroeder result: a comb of length `M` samples needs gain
  `g = 10^(-3M / (RT60·fs))` to reach -60dB after `RT60` seconds, computed
  per comb from its own length so every line agrees on the same decay time
  instead of 8 lines quietly disagreeing. The same idea sets the damping
  coefficient from a target high-frequency RT60 as a fraction of the
  low-frequency one (Tone), rather than an arbitrary damping range -- see
  the derivation in `HallRoomReverbModule.h`. 3 spaces: **Room** (warm,
  small/dense, RT60 0.3-1.8s), **Hall** (clear, spacious, RT60 0.8-4.5s --
  the least-modified of the three), **Plate** (metallic, extended highs,
  RT60 1.0-5.5s and deliberately tuned *bigger* than Hall -- longer
  comb/allpass line-length scale, a longer RT60 ceiling, and denser
  allpass diffusion, the way a real plate's whole surface resonates almost
  simultaneously). (A Shimmer mode — pitch-shifted feedback — was tried
  and pulled after it produced a runaway-feedback squeal; may come back
  once the feedback loop gain is worked out properly against a real
  reference rather than guessed.) The original Lexicon-480L-captured IRs
  this module used to convolve against are still in
  `Resources/ImpulseResponses/HallRoom/` but are no longer loaded.
- `GraphicEQModule` — 9-band post-effects EQ plus switchable HPF/LPF.
- `PresetManager` — real save/load to disk-backed XML presets (one file per
  preset). Ships with 6 factory presets covering clean, edge-of-breakup,
  driven lead, vibrato, ambient, and tight rhythm tones.

Several modules started as direct ports from the Rockalizer repo
(`ChorusModule`, `EchoModule`, `TremoloModule`) but have since diverged
significantly — July and Plexer in particular are now built around specific
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
