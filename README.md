# Threadline

Amp sim + drive stack, VST3 / AU / Standalone, one JUCE codebase.

**Chain:** Noise Gate → Input Gain → Compressor → Klon → Breaker (TS9/TS808/TS10) → Amp (5E3 Tweed Deluxe-inspired) → Cab (your own IR) → Tremolo → July (Chorus/Vibrato) → Delay (Plexer or Copier, user-selectable) → Reverb (Hall/Room) → 9-Band EQ → Output Gain

Klon and Breaker can run in either order ahead of the Amp (each keeps its own
on/off toggle regardless) via the Overdrive Order switch.

## Modules

- `NoiseGateModule` — inline gate shared with Rockalizer.
- `CompressorModule` — Diamond-style optical compressor with a two-stage
  vactrol release curve (fast initial recovery, slower tail).
- `KlonModule` — treble-boost pre-emphasis into a genuine Wave Digital
  Filter simulation of the real Klon Centaur clipping stage's circuit
  (`Source/DSP/WDFCore.h`, ported from and verified against
  jatinchowdhury18/KlonCentaur's own traced-and-measured model), blended
  against the clean signal (that clean/driven blend is what makes a
  Klon-style circuit sound "transparent" rather than fuzzy). The real
  traced circuit's diode pair turned out to be matched (single Is/Vt for
  both diodes), not the popular "germanium + silicon asymmetric pair"
  story this module used to model — corrected to match what the actual
  reverse-engineered schematic uses.
- `TS9Module` ("Breaker" in the UI) — switchable TS9/TS808/TS10 variants,
  each sharing a genuine WDF simulation of the real op-amp/diode-pair
  clipping stage (ideal-op-amp limit of Chowdhury-DSP/BYOD's own Tube
  Screamer model, with that model's real component/diode values: Rin=4.7k,
  Rf=51k+0-500k from Drive, 1N4148 Is=4.352nA/Vt=25.85mV×1.906) — Drive now
  moves the actual feedback resistor rather than pre-scaling the signal
  into a fixed curve. Per-variant differences still live in the pre-clip
  highpass corner and post-clip Tone range, same as before.
- `AmpModule` — dynamic, oversampled 5E3-inspired model (Rob Robinette's 5E3
  circuit writeup): input/interstage coupling caps, a two-stage 12AY7/12AX7
  preamp whose nonlinearity is a genuine triode current model (`TriodeStage`
  -- see "Triode preamp model" below) rather than a curve-fit tanh, including
  real physically-modeled blocking distortion (grid current charging the
  input coupling cap under sustained heavy drive, causing a momentary "gasp"
  and recovery, now an emergent result of the real grid-current equation
  rather than a hand-tuned heuristic). The Tone/Bassman-stack network sits
  between those two preamp stages, matching
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

## Wave Digital Filter clippers (Klon/Bull, Breaker/TS9)

`Source/DSP/WDFCore.h` is a small, from-scratch port of the Wave Digital
Filter primitives Bull's and Breaker's clipping stages are built from
(one-port resistor/capacitor/voltage-source/current-source elements, series
and parallel scattering adaptors, and a diode-pair root element solved via
the closed-form Wright Omega function) — real circuit simulation of each
stage's actual passive network and diode I-V law, in place of the
hand-fit-asinh curve both modules used before. Not derived from scratch:
every adaptor's scattering rule and the Wright Omega diode solve were
copied from and checked against two real, working, MIT-licensed reference
implementations by Jatin Chowdhury —
[KlonCentaur](https://github.com/jatinchowdhury18/KlonCentaur) (Bull's
clipper is that repo's own traced Klon Centaur circuit, component values
included) and [chowdsp_wdf](https://github.com/Chowdhury-DSP/chowdsp_wdf) /
[BYOD](https://github.com/Chowdhury-DSP/BYOD) (the general adaptor library,
and Breaker's clipper is the ideal-op-amp limit of BYOD's own Tube Screamer
model). Before wiring either clipper into the plugin, both were exercised
in a standalone harness outside JUCE entirely — sweeping drive and input
amplitude across several orders of magnitude (including deliberately
extreme values well past any real playing level) and checking for
NaN/Inf/unbounded output — since a WDF implementation can be subtly wrong
in ways that still sound plausible; a numerical sweep catches instability
an ear can't necessarily catch on the first listen. Both clippers' outputs
are in real circuit units (Bull's is a current in amps, Breaker's a
voltage) rather than a pre-normalised audio range, so each has its own
empirically-measured `outputCalibration` constant (documented in-file)
bringing that back to a sensible level, plus a wide tanh safety rail as a
backstop — the diode pair itself is what actually limits the level, same
as in real hardware; the safety rail is insurance against the calibration
guess being off, not a routine level-setter.

`AmpModule`'s Bassman/Tone-stack network was **not** touched in this pass —
it's already exactly circuit-derived (Yeh & Smith's transfer function),
mathematically equivalent to a WDF simulation of the same linear network, so
re-implementing it as one wouldn't change its accuracy, only its cost. The
preamp's own nonlinear stages *were* upgraded, in a later pass, once real
reference material for that different kind of circuit (grid conduction
against the plate curve, not a diode clipper) had actually been read — see
"Triode preamp model" below.

## Triode preamp model (AmpModule's V1/V2A)

`AmpModule`'s two preamp gain stages (V1, V2A) previously used a curve-fit
`tanh` plus a hand-tuned "bias memory" heuristic for blocking distortion.
Both are now `TriodeStage` (nested in `Source/DSP/AmpModule.h`): a genuine
triode current model driving a real plate-load RC network, built from
actually reading — not working from memory of — R. Dempwolf, U. Zölzer,
["A Physically-Motivated Triode Model for Circuit
Simulations"](https://dafx.de/paper-archive/2011/Papers/76_e.pdf) (DAFx-11),
full text extracted via the paper's own saved PDF. Its two headline
equations are used directly:

- Cathode/anode current: `I_a = G·h((1/μ)·V_a + V_g)^γ − I_g`
- Grid current: `I_g = G_g·h(V_g)^ξ + I_g0`, where `h(x) = softplus(x)` is
  a tunable-knee smoothing function the paper uses so both currents stay
  non-negative and differentiable through the conduction knee.

Parameters are the paper's own Table 1 measured fit for a real 12AX7
("RSD1"), used as-is for V2A. No 12AY7-specific fit exists in the
literature; V1 uses the same Table 1 shape parameters with only `μ`
substituted for 12AY7's real datasheet value (~44, vs the 12AX7's ~96, per
RCA/GE tube manuals) — the one parameter with an independently citable
source for that tube, called out honestly as an approximation rather than
a second real measured fit.

Two things came out of actually verifying this numerically (same
standalone-harness-before-shipping discipline as the WDF clippers above)
that are worth recording because they're the kind of mistake that *sounds*
plausible until measured:

1. **The DC operating point is not optional.** The paper's `V_eff = V_g +
   V_a/μ` term uses the tube's real, large (~100–300V) plate voltage — not
   a small zero-centered AC value. A first attempt modeled `V_a` as a bare
   AC deviation from zero, which broke the stage's negative feedback and
   made it clip almost the entire waveform at sub-millivolt input. The fix:
   at `prepare()`, solve the quiescent plate voltage/current by bisection
   (`I_a(V_g0, V_a0) = (V_b − V_a0) / R_a`, monotonic, so bisection is
   robust), then run the AC dynamics as a perturbation around that real
   operating point — the standard large-signal/small-signal split, just
   solved here rather than assumed.
2. **The grid-leak resistor's own resting current has to be in that same
   solve.** `I_g0` (leakage) alone produces a small nonzero resting
   grid-cap charge even at silence; solving the plate bias point without
   accounting for it left a permanent mismatch between the assumed and
   actual resting grid voltage, which meant the running model never
   settled to zero at rest and instead found a wrong, wildly-offset
   equilibrium. Fixed by iterating the plate-bias solve and the resting
   grid-current solve to a joint fixed point. Verified by driving the
   isolated stage with a DC step and confirming it settles to the same
   gain an independent closed-form small-signal calculation predicts
   (both landed on ≈−36×, to three figures) — and confirming a silent
   input produces exactly zero output at rest, not a slowly-drifting one.
3. **The plate load is not an RC network.** A first version modeled the
   plate resistor `R_a` and the interstage coupling cap as if they sat in
   parallel at the plate node (an `R_a`-and-coupling-cap "RC network"), the
   same bilinear-transform pattern used elsewhere in this file. That's
   wrong: the coupling cap actually sits in *series* to the next stage's
   grid, not in parallel with the plate resistor, and va_ac (the plate's AC
   deviation, `V_a0`/`I_a0` already subtracted out) is a pure zero-mean
   signal by construction — already "coupled," no capacitor needed to make
   it so. The mistaken parallel RC turned each stage into an unintended
   ~80Hz lowpass; cascaded through two stages, that's exactly what made
   the whole amp sound muffled and boxy, with the drive's harmonic content
   chopped off before it could read as clear. The real fix is Ohm's law —
   `V_a_ac = -R_a·I_a_ac` — but that alone removes the only damping in the
   one-sample-delayed feedback this stage uses (see BYOD's quasi-static
   pattern, cited above) and goes numerically unstable almost immediately
   (verified: pins to the safety ceiling regardless of input). A tiny
   capacitor stays in the plate-load bilinear transform purely as a
   numerical stabiliser for that feedback loop, not to model any real
   component — its value (220pF against `R_a`, a ~7.2kHz pole) was found
   by sweeping 47pF–2nF against noise-burst/impulse/extreme-drive stress
   tests and picking a value with comfortable margin above where it goes
   unstable (~50–90pF) while sitting well above the audio band it must
   not audibly filter. Verified with a frequency sweep after the fix:
   output magnitude now varies under 5% from 100Hz to 8kHz, versus the
   drastic rolloff the mistaken 80Hz-per-stage version had.

Blocking distortion is the grid current genuinely charging the input
coupling cap through the grid-leak resistor (chasing a target of
`I_g(V_g)·R_g`, Ohm's law, with time constant `R_g·C_in`) rather than a
fixed-rate heuristic — an early version used a placeholder coupling-cap
value roughly 40,000× too small, giving a sub-millisecond "blocking" time
constant that clamped almost the entire signal; corrected to a real
preamp-scale coupling cap once the harness made the mismatch obvious.

The drive knob's input-gain floor and the output calibration were also
re-derived after fixing the topology bug above — removing the unintended
lowpass let substantially more real signal level through, so the
calibration constants needed re-measuring from fresh harness numbers
rather than being carried over.

Known, deliberate simplification: both stages are modeled cathode-bypassed
(a fixed bias point). The real 5E3's V1 is unbypassed, which real hardware
uses for extra local negative feedback and headroom; this model captures
the tube's real current-vs-voltage nonlinearity and grid-conduction/
blocking behaviour, not that specific cathode-degeneration detail — a
scope boundary, not an oversight.

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

Every other effect section now has its own photographed knob too, the same
way: `PhotoKnob::Style::{Compressor,Klon,Breaker,Tremolo,Chorus,Delay,
Reverb}` for that section's card (including the Breaker page's 3-position
variant selector, and both of Delay's engines -- Plexer's `echoSection`
knobs and Copier's hand-built `carbonKnobs`), and `Style::Gold` as the
catch-all for what's left on the plain "Modern" knob before (Gate/Input/
Output in the persistent footer, both Cab slots' Mix knobs, and the A/B
blend knob). `buildSection()` always creates plain-style knobs, so each
page applies its section's style in a loop right after building it rather
than threading a style parameter through that shared helper. One of the
eight source photos (Reverb) needed the same white-background flood-fill
treatment as the EQ knob; the rest already had real alpha transparency.
The Compressor photo is chicken-head shaped like the Vintage style's own
image (handle extending past the circular body) rather than a plain disc,
but measuring its actual rotational centre landed within a rounding error
of the canvas's geometric middle, so it didn't need Vintage's custom pivot
offset -- the standard centre pivot already used for every non-Vintage
style works for it too.

`RockerSwitch` no longer takes keyboard focus (it was drawing a white
focus-ring rectangle around itself whenever clicked, since clicking a
JUCE button grabs focus by default -- removed by not asking for focus in
the first place rather than just not drawing the ring). The persistent
Gate/Input/Output footer is shorter (84px, was 108) with every control
inside it scaled down by the same ratio rather than clipped, and the page
content area above it grew into the space that freed up, on top of what
was already reserved -- the gap right above the footer is 16px now, was
40. The Amp page's photo is drawn bigger again (1.7x its fitted size, was
1.5x) with a real gap (28px) between it and the knob bar below, instead of
them almost touching; the clip region extends a little past the photo
frame's own bottom edge into that gap so the larger photo has real room to
spill into without touching the bar.

Klon is displayed as **Bull** throughout the UI and in every one of its
parameters' DAW-visible display names (`Bull On`, `Bull Gain`, `Bull
Treble`, `Bull Level`, `odOrder`'s "Bull -> Breaker"/"Breaker -> Bull"
choices) -- parameter IDs stay `klon*`/`odOrder` internally, and the
`KlonModule` class keeps its name too, so old presets and any code
referencing the module are unaffected. Same pattern as Plexer/Copier
already displaying EchoModule/CarbonCopyModule's real-pedal names while
keeping their own internal identifiers.

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
