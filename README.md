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
  shifts the operating point, causing a momentary "gasp" and recovery). The
  Tone/Bassman-stack network sits between those two preamp stages, matching
  the real 5E3's actual V1 -> Tone -> V2A signal order (confirmed against
  Robinette's own annotated schematic: a shared 1M tone pot and 0.005uF tone
  cap feeding V2A's grid, not a filter tacked on after both preamp stages --
  it used to sit there, so the second stage now clips whatever harmonic
  content Tone left behind rather than clipping the full-bandwidth signal
  and only filtering the already-clipped result). Then a cathodyne-style
  phase inverter, a genuinely differential push-pull power
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
    faster than filtering alone could tame). Sustain's dial-to-N mapping is
    squared (not linear) before the `g = 10^(-2/N)` formula above, and
    Volume's own curve rises slower than linear with no extra headroom
    multiplier — a linear dial-to-N map put the knob's midpoint at ~5.4x
    loop buildup already, and Volume was multiplying that already-hot
    signal on top, so low-to-mid Sustain/Volume settings read as "wet from
    barely touching the knob" and the whole dial had to be kept low to stay
    controlled. Both knobs now stay genuinely gentle through the first half
    of their travel and reserve the steep run-up toward self-oscillation
    for the last quarter or so, closer to how a real regen/volume knob
    feels.
  - `CarbonCopyModule` ("Copier" in the UI) — modeled on the MXR Carbon
    Copy's control surface: Time, Regen (feedback), Mix, and a Mod toggle.
    A fixed lowpass filter sits inside the feedback path itself (not a Tone
    knob the real unit doesn't have), so repeats get progressively darker
    with each pass — the defining trait of a real bucket-brigade analog
    delay versus a clean digital one. Mod adds a slow, modest chorus-like
    delay-time wobble when switched on. Regen reaches near-self-oscillation
    at maximum, same as real hardware. Mixing is a straightforward
    crossfade, unlike Plexer's additive/always-colored EP-3 behaviour.
- `HallRoomReverbModule` — algorithmic (not convolution) reverb: a genuine
  **Feedback Delay Network** (FDN, per Jot's original papers), not a bank of
  independent parallel combs. The earlier design (still described in git
  history if you want the comparison) ran 8 combs per channel that only ever
  interacted by being summed at the output -- structurally just 16
  independent one-pole-damped resonators, regardless of how the "reverb"
  math around them was dressed up. The current tank instead runs a single
  **shared** set of 8 delay lines (not duplicated per channel) through a
  Householder mixing matrix every sample -- each line's output feeds into
  *every* line's input, weighted by `H = I - (2/N)·ones(N,N)` -- so the tank
  is a genuinely coupled network the way a real room's modes couple, not 8
  parallel echoes. That matrix is orthogonal, which is what makes RT60
  control tractable at all: an orthogonal matrix preserves vector norm, so
  applying one *uniform* scalar loop gain `g` to all 8 lines decays the
  whole coupled network's total energy at exactly rate `g` per round trip,
  independent of the specific mixing pattern -- the standard way real FDN
  reverbs control decay time. `g` (and the damping split between it) is
  computed once from the network's characteristic (mean) line length via
  the same Schroeder `g = 10^(-3M / (RT60·fs))` result used before, since a
  genuinely coupled network doesn't have a simple closed form for treating
  each line's RT60 independently the way 8 non-interacting combs did.
  Stereo comes out of that single shared tank via two orthogonal
  (Hadamard-row, ±1) weighted sums of the same 8 lines, rather than running
  two separate networks. Ahead of the tank, a 4-stage **input diffuser**
  (Dattorro's exact published JAES-1997 topology: delays 142/107/379/277
  samples, coefficients 0.75/0.75/0.625/0.625, sample-rate-scaled but fixed
  across Room/Hall/Plate) smears the transient onset into the network,
  which is what a real room's early scattering does and a comb bank alone
  never modeled. 3 spaces: **Room** (warm, small/dense, RT60 0.3-1.8s),
  **Hall** (clear, spacious, RT60 0.8-4.5s), **Plate** (metallic, extended
  highs, RT60 1.0-5.5s, longer line-length scale and denser post-tank
  diffusion than Hall, the way a real plate's whole surface resonates almost
  simultaneously). The 8 lines keep Freeverb's classic tunings as a spacing
  pattern (chosen so their resonances land at different-enough frequencies
  to stay decorrelated -- doubly important now that the Householder matrix
  explicitly couples all 8 every sample), with Room's extra spread-boost
  factor preserved from the previous design so shrinking the tank for a
  small room doesn't also shrink that spacing into a "phasing"/comb-y
  quality. Two further stages layer on top of the FDN core, applied to its
  extracted stereo taps rather than inside the shared tank: a per-channel
  multi-tap **early-reflection** generator (per-model pattern -- Room
  modest/dense, Hall sparse/spread, Plate near-instant/ultra-dense), since
  an FDN alone is a late-diffuse-field generator with no discrete-echo
  size/distance cue; and 4 **post-tank allpasses per channel**, each reading
  a slowly, independently modulated fractional delay (a few tenths of a
  millisecond, different rate per instance) rather than a fixed integer
  lookback, keeping the diffuse tail smooth under sustained input instead of
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

## UI

The editor window is a taller 1200x760 canvas (was 1200x660). Every existing
element keeps its original absolute size — nothing was scaled up. Most of
the extra 100px went to the page content area (the effects rack, the amp
knobs/cab row, the EQ) so every page's content sits closer to the footer
strip instead of leaving a big empty gap above it (was a fixed 10px gap,
briefly 110px, now 40px).

The 4 tab icons (Pre-FX / Amp / Wet FX / EQ) do double duty: a single click
switches which page is visible, same as always; double-pressing one bypasses
*that whole page's section* (a `preFxSectionOn`/`ampSectionOn`/
`wetFxSectionOn`/`eqSectionOn` APVTS bool per tab), independent of switching
pages and independent of each module's own on/off toggle inside that page —
double-press again to restore it. A bypassed tab shows a diagonal strike
through its icon (works whether or not that page is the one currently
visible) so it stays legible while you're looking at another tab. The icons
themselves are 2x their original size, with the tab row and page content
area both re-split to fit (bottom edges unchanged, so the footer's position
is unaffected). The Amp itself also gained its own on/off toggle (`ampOn`,
defaults on) in the knob card's top-right corner, matching every other
module's toggle — it previously had none, since it was treated as
always-in-the-chain.

Page 2 (Amp) is laid out photo-on-top now, not photo-left/knobs-right: the
amp photo spans the full width (drawn 1.5x the size that would otherwise
just fit the frame, clipped so it can't bleed into the bar below), with a
single horizontal control bar below it (Voice switch, knobs, bypass) and
the two Cab slots below that — pattern borrowed from Neural DSP's Archetype
line (photo big, one compact control dock underneath). The knob bar
reserves 5 fixed-width slots; Drive and Volume always land in slots 0 and
1 regardless of voice, so they never visibly move when switching Vintage/
Boutique -- Tone (Vintage) lands in slot 2, the same slot Boutique's Bass
uses, rather than centred among the remaining 3 slots. The knob-bar/cab-row
heights were trimmed down (and pinned to the bottom) specifically to give
the photo more room, rather than splitting the page evenly, and the knob
bar itself is taller than its first pass.

Two controls that used to be a stacked or side-by-side button pair are now
a `RockerSwitch` (`Source/UI/ThreadlineComponents.h`) — a physical toggle
switch, rendered from two photographed reference images (off/on) rather
than drawn in vector, with a caption label below reading out the current
selection since the switch photo itself only shows on/off. Each photo's
actual pill content is tight-cropped in code (the two source photos have
very different canvas padding, and the "on" shot has an amber glow baked
in around the pill, so drawing either whole canvas into the same bounds
would make the switch visibly resize between states) and both states are
sized to fit the same target height, so the switch is a consistent size
regardless of which one is showing: the Amp page's Vintage/Boutique voice
switch, and Page 1's Klon-first/Breaker-first overdrive order switch.

The 9-band graphic EQ's faders are visibly wider now (track capped at 12px
instead of 7px, cap proportionally wider, and each band's reserved slot
keeps more of its width instead of two-fifths of it getting inset away).
Its rack plate was also missing the solid dark-brown backing fill and
rounded-corner clip every other page's plate has before drawing the plate
image (`paintSectionPlate` in `SectionBuilder.h`) -- without it, the plate
photo's own near-black edge pixels showed through as a stray black fringe
instead of blending into a consistent backing colour like every other
plate does.

`PhotoKnob`'s value readout (toggled globally by the eye icon) no longer
permanently reserves a bottom row for the number, which had made every
knob smaller than it used to be to make room for a value that's blank
most of the time. It now uses JUCE's own popup display instead -- a
floating value bubble that only exists while a knob is actively being
dragged, positioned on the desktop rather than inside the knob's own tiny
bounds -- so knobs draw at full size all the time.

The EQ page's HPF/LPF knobs use a third `PhotoKnob::Style` (`EQ`) built
from a user-provided photo of a full knob assembly (metal cap, tick-mark
bezel, and pointer baked into one image, rotated as a single unit rather
than a separate static ring + rotating cap). The source photo shipped with
an opaque white background and a soft drop shadow blended into it, not
real transparency -- flood-filled transparent from the four corners
(ImageMagick, high fuzz tolerance to eat the shadow gradient along with
the flat white, then a touch of alpha blur to soften the cut edge) before
being registered as `knob_eq.png`, otherwise the knob would have rendered
with a stray white box behind it against the gold rack plate.

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
