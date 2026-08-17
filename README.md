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
- `AmpModule` — dynamic, oversampled 5E3-inspired model (Rob Robinette's 5E3
  circuit writeup): input/interstage coupling caps, a two-stage 12AY7 preamp
  with grid-bias-shift memory (blocking distortion -- sustained heavy drive
  shifts the operating point, causing a momentary "gasp" and recovery), a
  cathodyne-style phase inverter, a genuinely differential push-pull power
  stage (two tubes driven by +V/-V from the cathodyne, subtracted at the
  output transformer -- for a matched pair this cancels even-order harmonics
  exactly, leaving only odd-order content doubled; a small same-direction
  bias mismatch between the two tubes breaks that cancellation on purpose,
  since real 6V6 pairs are never perfectly matched), a bass-weighted sag
  detector (low chords draw more current and "bloom" more than a bright
  single-note lead at the same peak level), and output-transformer core
  saturation as a second, distinct compression mechanism from sag (sag is
  slow/envelope-driven; this is an instantaneous per-sample soft-knee, so a
  hard transient gets capped even before sag catches up). Two voices:
  **Vintage5E3** (the single passive-feeling Tone knob above, since the real
  5E3 only has one treble-cut control) and **Modern3Band**, which swaps that
  for the exact real passive Fender '59 Bassman tone-stack network (not
  three independent EQ bands -- the actual circuit's third-order transfer
  function per Yeh & Smith's DAFx-06 paper, so raising Mid measurably pulls
  down apparent Bass and Treble the way the real passive network does)
  placed at the identical point in the signal chain; everything else in the
  amp is unchanged between voices.
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
    as real hardware. A fixed lowpass sits inside the feedback path, after
    saturation, modeling the tape loop's own bandwidth loss on every pass —
    repeated saturation of an unfiltered signal was piling up harmonics
    each cycle and reading as a harsh metallic hiss rather than a warm tape
    repeat, especially in Sound-on-Sound (whose near-permanent feedback was
    also pulled back from 0.998 to 0.99 — close enough to unity that,
    layered over real playing, it built up into an undifferentiated wash
    faster than filtering alone could tame).
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
  simultaneously, though pulled back from an earlier, too-extreme brightness
  bias and allpass feedback that read as metallic/synthetic rather than
  like a real plate). Freeverb's 8 comb tunings were chosen with a specific
  spread between them so their resonances land at different-enough
  frequencies to sound diffuse; uniformly scaling every line down for Room
  shrank that spread too, which was audible as a "phasing"/comb-y quality
  especially at high Mix -- fixed by scaling each line's deviation from the
  mean tuning by an extra factor (Room only) so the 8 lines stay
  decorrelated even at a smaller overall size. (A Shimmer mode —
  pitch-shifted feedback — was tried and pulled after it produced a
  runaway-feedback squeal; may come back once the feedback loop gain is
  worked out properly against a real reference rather than guessed.) Two
  further stages layer on top without touching the RT60-verified comb math
  above: a per-channel multi-tap **early-reflection** generator (per-model
  pattern -- Room modest/dense, Hall sparse/spread, Plate near-instant/
  ultra-dense) runs in parallel with the tank, since a comb/allpass tank
  alone is a late-diffuse-field generator with no discrete-echo size/
  distance cue at all; and each of the 4 **allpasses now reads a slowly,
  independently modulated fractional delay** (a few tenths of a
  millisecond, different rate per instance) instead of a fixed integer
  lookback, the standard technique (Dattorro's plate topology among others)
  for keeping a diffuse tail smooth under sustained input instead of
  ringing at its own static resonances. The original Lexicon-480L-captured
  IRs this module used to convolve against are still in
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

(Both items previously listed here -- a single-voice amp with no 3-band EQ
option, and an unskinned UI with no custom knob art -- are done: AmpModule
has had a Modern3Band voice with the real Bassman tone-stack derivation for
a while, and the UI uses photographed knob/rack-plate art throughout. Ask
if you want a fresh look for genuinely open items rather than trusting this
section blindly -- it drifted out of date once already.)
- Cab IR loading is basic next to something like Ignite Amps' NadIR
  (dual-IR cab convolver): no Resonance control (speaker-cone/power-amp
  interaction, independent of whatever IR is loaded), no manual timing
  offset between the A/B slots beyond automatic alignment + a binary phase
  flip, no automatic phase-polarity detection on load (invert is manual-only
  right now). Discussed and deliberately not pursued yet.
