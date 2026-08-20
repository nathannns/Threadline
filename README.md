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
  reverse-engineered schematic uses. Oversampling mode (Off/2x/4x, default
  2x) is now selectable via `klonOversampling`, matching AmpModule -- three
  fully-prepared instances (one per mode) live in `PluginProcessor::klons`,
  hot-switchable with no runtime Oversampling-object rebuild, same pattern
  as Amp's own `amps` array. Latency reported to the host is pinned to the
  4x instance regardless of the selected mode, so it stays constant across
  a live switch (most hosts don't tolerate a plugin's reported latency
  changing without a full reinit).
- `TS9Module` ("Breaker" in the UI) — switchable TS9/TS808/TS10 variants,
  each sharing a genuine WDF simulation of the real op-amp/diode-pair
  clipping stage (ideal-op-amp limit of Chowdhury-DSP/BYOD's own Tube
  Screamer model, with that model's real component/diode values: Rin=10k,
  Rf=51k+0-500k from Drive, 1N4148 Is=4.352nA/Vt=25.85mV×1.906) — Drive now
  moves the actual feedback resistor rather than pre-scaling the signal
  into a fixed curve. Per-variant differences still live in the pre-clip
  highpass corner and post-clip Tone range, same as before. Same
  selectable-oversampling treatment as Klon above (`ts9Oversampling`,
  `PluginProcessor::ts9s`). Rin was originally coded as 4.7k (an R4-for-R5
  mislabeling -- R4 is real, but it's part of the feedback network, not
  the input resistor); the feedback network's own 51pF cap and 4.7k+47nF
  branch, and BYOD's modeling of the op-amp's own finite gain/impedance,
  aren't ported yet -- see "Known gaps / next steps."
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
  and only filtering the already-clipped result). Then a real cathodyne
  (split-load) phase inverter -- a third `TriodeStage` instance with matched
  Ra=Rk instead of Ra>>Rk, verified to produce genuinely antiphase,
  ~equal-amplitude outputs, not a curve-fit approximation -- driving a
  genuinely differential push-pull power stage of two real 6V6GT beam-
  tetrode models (`PentodeStage`, Koren's model, real 6V6-GTA parameters --
  see "Phase inverter and power stage" below), subtracted at the output
  transformer -- for a matched pair this cancels even-order harmonics
  exactly, leaving only odd-order content doubled; a small same-direction
  bias mismatch between the two tubes breaks that cancellation on purpose,
  since real 6V6 pairs are never perfectly matched, a bass-weighted sag
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
  `process()` used to build its wet-signal scratch buffer via a fresh local
  `AudioBuffer` + `makeCopyOf()` every single call -- a default-constructed
  buffer is always 0-sized, so `setSize()`'s reuse-optimization never had a
  matching size to skip, meaning a real heap alloc+free on the audio thread
  every block, doubled since both Cab A and Cab B instances did it. Now a
  persistent member buffer sized once in `prepare()`, matching how
  `HallRoomReverbModule`'s own `wetBuffer` already did this correctly.
- `TremoloModule` — two selectable voices sharing one LFO (Rate/Amount, same
  phase), so switching voices keeps the same speed and feel:
  - **Bias** (default) — bias-modulation tremolo, modeled on the real tube
    gm curve's asymmetric throb (fast dip, gentler recovery). Mono: the same
    gain drives both channels, so it can never become autopan.
  - **Harmonic** — the later blackface/brownface mechanism, a genuinely
    different circuit rather than a variant of the same trick: a
    `juce::dsp::LinkwitzRileyFilter` crossover (4th-order/24dB-oct, fixed at
    700Hz, low+high guaranteed to sum flat) splits the signal into low/high
    bands, and each band's amplitude is modulated by the same LFO but 180°
    out of phase with the other, so brightness swings between the bands
    rather than the whole signal's level moving together — hence
    *harmonic*, not amplitude, tremolo. Genuinely stereo: left assigns the
    low band the LFO's "gainA" phase and the high band "gainB"; right swaps
    that assignment, so the two channels' spectral balance swings in
    opposite directions as the LFO cycles — real width, not dual-mono, and
    (since Tremolo sits after Amp/Cab, both effectively mono up to this
    point) the first point of genuine stereo divergence in the whole
    signal chain.
- `ChorusModule` ("July" in the UI) — modeled on the Julia pedal's exact
  control surface: Rate, Depth, Lag (LFO center delay time), a Sine/Triangle
  waveform switch, and a Dry/Chorus/Vibrato 3-way switch — plus one addition
  beyond Julia's own panel, a continuous Mix knob. D-C-V now picks the
  character only (Dry always forces silence outright; Chorus/Vibrato pick
  the modulation type), while Mix sets the actual dry/wet blend percentage
  for whichever of those two is selected, rather than each being pinned to
  one fixed blend amount (42%/100%) with no way to dial it in.
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

  Both engines' feedback-loop nonlinearities (Plexer's always-on saturation
  stage, and both engines' write-side safety rail, engaged near
  self-oscillation) used to evaluate tanh() directly once per sample --
  which aliases, since tanh's own harmonics extend well above what the
  sample rate can represent whenever consecutive samples move it by very
  much, and inside a feedback loop that harmonic content recirculates and
  compounds on every repeat, worst exactly at high Sustain/Regen where the
  harmonic content is highest to begin with. Oversampling the whole delay
  line to fix this the way Klon/TS9/Amp oversample their own nonlinearities
  is awkward here -- the delay buffer's own indexing would need to track the
  oversampled rate too -- so `Antialiasing.h`'s `AdaaTanh`/`AdaaSmoothRail`
  use antiderivative antialiasing (ADAA) instead: the exact average slope of
  the nonlinearity's antiderivative between the previous sample and this
  one, `(F(x2)-F(x1))/(x2-x1)`, mathematically equivalent to bandlimiting
  the nonlinearity's output at zero added latency (Parker, Zavalishin,
  Bilbao, Valimaki, "Antiderivative Antialiasing for Memoryless
  Nonlinearities," DAFx-16), falling back to a direct evaluation at the
  analytic midpoint when consecutive samples are too close together for
  that secant to stay well-conditioned. Verified in a standalone harness (no
  JUCE dependency) before wiring in: matches the exact secant formula,
  stays bounded under a stress test of large fast-changing jumps, and shows
  no discontinuity worth worrying about crossing the epsilon fallback
  boundary.
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
  simultaneously). The 8 lines originally kept Freeverb's classic tunings as
  a spacing pattern (1116/1188/1277/1356/1422/1491/1557/1617) -- fine for
  what Freeverb actually needed them for (8 *independent* per-channel combs,
  each only ever feeding back into itself) but wrong here: 7 of those 8
  numbers share a factor of 3, invisible in an independent comb bank but
  audible as pitched/metallic ringing once the Householder matrix genuinely
  couples all 8 lines together every sample (confirmed against Hall
  specifically sounding metallic, which its own "clear, spacious" design
  intent above says it shouldn't). Replaced with 8 primes spanning the same
  range and near-identical mean (1117/1193/1277/1361/1423/1493/1559/1619) --
  primes guarantee every pair is coprime, removing the shared-periodicity
  mechanism outright. Room's extra spread-boost factor is preserved from the
  previous design so shrinking the tank for a small room doesn't also
  shrink that spacing into a "phasing"/comb-y quality. Two further stages layer on top of the FDN core, applied to its
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
  preset). Ships with 9 factory presets; each preset's name is required to
  acknowledge every "wet"/character effect (Tremolo, July chorus/vibrato,
  Plexer/Copier delay, Reverb) it actually engages, so several are
  deliberately 100% dry rather than carrying modulation or space just for
  variety. `ensureTestingPresets()` only (re)creates a factory preset that's
  missing on disk — it used to unconditionally rebuild all of them on every
  launch, which would silently discard a user's edits to a factory preset
  saved under its original name.
  Two real bugs were found and fixed generating this set: `makePreset()`'s
  reset-to-baseline step (`apvts.replaceState(original)`) looked like a
  clean per-preset restart but wasn't one — `juce::ValueTree` has reference
  semantics, so that call aliased the APVTS's live state to the same
  underlying tree `original` pointed at, and every subsequent
  `setValueNotifyingHost()` mutated that shared tree, corrupting `original`
  itself for every later preset in the loop. In practice this meant one
  preset turning on July's Vibrato mode leaked into every preset generated
  after it, despite them never touching that parameter — exactly the
  "why does everything have vibrato" symptom it produced. Fixed with
  `original.createCopy()`, forcing a genuine independent deep copy each
  time. Separately, `inputMute` (the toolbar mute button) wasn't in the
  list of hardware/session parameters `loadPreset()` preserves across a
  preset switch (alongside `masterBypass`/`ampOversampling`/`inputSource`),
  so muting to switch presets quietly would un-mute the instant the new
  preset loaded — added to that list.

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
guess being off, not a routine level-setter. Both constants were originally
tuned conservatively (Bull 150,000, Breaker 1.2) and read as "not enough
gain/fuzz" even fully cranked — a harness sweep confirmed Breaker's ideal-
op-amp diode clamp caps its raw output around ~0.5-0.6V regardless of
amplitude or Drive (physically correct, matching a real TS9's diode clamp),
which the original 1.2 multiplier never brought past ~0.6-0.7 out of the
3.0 safety ceiling. Raised to Bull 650,000 / Breaker 4.5, both re-verified
to reach a genuinely hot ~1.9-2.0 at max Gain/Drive + loud input (real
~4x/~3.75x loudness increases) while staying proportionally quieter at low
settings/quiet input.

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

That first retune fixed loudness/tone but not feel — a follow-up harness
sweep of raw (uncalibrated) output against the drive knob's input scale
showed the triode's own current-limiting makes that curve steep only
across roughly scale 0.005–0.08 and much flatter beyond it (0.08–1.5 only
gains another ~60% on top), so a 0.035–0.285 taper sat almost entirely in
the flat region — the knob barely changed the sound across most of its
travel. Separately, the output calibration (0.02) never actually drove a
sample into the safety-ceiling tanh's hard-clip region even at max Drive
and loud input (0% pinned in the harness), so the amp could reach smooth
tube saturation but never genuine fuzz-level hard clipping. Retuned to
0.008–0.098 (spanning the real steep region) and 0.065 (harness-verified
to reach real double-digit-percent pinned time at high Drive + loud
input, while staying at 0% for low Drive/quiet playing so touch
sensitivity survives) — then re-verified for numerical safety across
every sample rate/oversampling combination the plugin actually offers
(44.1–192kHz base rates × 1x/2x/4x) plus noise-burst/impulse stress,
all finite and bounded. Still read as "not enough distortion/fuzz" against
the Klon/TS9 gain increases below, so `outputCalibration` was raised again
to 0.10 — harness-verified to reach up to ~80% pinned (genuinely heavy,
fuzz-territory clipping) at max Drive + loud input, still 0% pinned at
low Drive/quiet playing.

V1's cathode is modeled unbypassed, matching the real 5E3 (Robinette
schematic: 1.5k cathode resistor, no bypass cap on V1, unlike V2A's
bypassed stage). This was originally a deliberate, documented scope
boundary — both stages ran cathode-bypassed (a fixed bias point) — since an
unbypassed stage isn't a fixed operating point anymore: the cathode
voltage V_k floats with the tube's own instantaneous current, and V_k
feeds back into the very grid-cathode voltage V_gk that sets that current
(V_gk = V_g − V_k), a genuine algebraic loop rather than a lookup.

Closing that gap: `TriodeStage::solveBiasPoint()` gets a
`cathodeUnbypassed` path that solves the DC operating point with a nested
bisection — the same monotonic plate-voltage search the fixed-bias path
already used, run inside an outer search over V_k0, since plate current
(through R_a) and cathode current (through R_k) have to settle
simultaneously (rising V_k0 always pulls V_gk0 down and therefore total
cathode current down too, so both searches stay monotonic). At audio rate,
`processSample()` re-solves V_k every sample via a warm-started
Newton-Raphson iteration (seeded from the previous sample's V_k, which
barely moves at audio rates, so a handful of iterations converges tightly)
against the *previous* sample's plate voltage — the same one-sample delay
the plate-load network already relies on for its own stability, reused
here so this stays a 1-D solve instead of a simultaneous 2-unknown one.

Verified with a standalone harness (no JUCE dependency — `TriodeStage`'s
core math is pure `<cmath>`) before wiring it in: at V1's real operating
point (R_a=100k, R_k=1.5k, V_b=300V), the solve converges to V_a0≈149.7V,
V_k0≈2.25V — a realistic self-bias voltage for a 12AY7 — and both Ohm's-law
checks (`(V_b−V_a0)/R_a` vs `I_a0`, `V_k0/R_k` vs `I_k0`) match to the
harness's printed precision. A sine sweep at three input levels confirmed
genuine gain reduction from the added negative feedback versus the old
fixed-bias model (peak output ran 53–85% of the bypassed stage's,
strongest in the small-signal region and weakest once both stages are
current-limited-saturating anyway — exactly the shape real cathode
degeneration should have), stayed finite across all three levels, and an
impulse/step stress test held bounded with no blow-up. This genuinely
changes V1's gain structure (that's the point — lower gain, more headroom,
matching the real circuit), and that did make the whole amp read as
noticeably quieter than before at ordinary playing levels once wired in --
not the intended effect. `v1GainCompensation` fixes that: a fixed
post-multiply on V1's output, sized to exactly cancel the gain the new
model loses versus the old one at small signal (measured in the same
standalone harness -- a low-level 440Hz sine's settled RMS output/input
ratio, isolating the linear gain from the nonlinear compression a hotter
sweep would conflate it with: old 35.94x vs new 23.20x, so 1.549x /
+3.80dB restores parity). Being a pure linear post-multiply rather than a
curve change, it restores the old loudness at normal playing levels
without touching the new model's extra headroom/compression character at
hard drive -- so the Drive knob's *feel* (where it starts compressing,
how hard) should still read close to before, just no longer quieter
across the board.

## Phase inverter and power stage (AmpModule's cathodyne + 6V6GT)

V1/V2A got the real triode-current treatment above; the phase inverter and
power stage were still a curve-fit `tanh` (an asymmetric-tanh "cathodyne"
approximation, and `tanh(±v+mismatch)` per power tube). This closes that
gap the same way, but the phase inverter and power tube needed two
different equation families, since they're genuinely different devices.

**Cathodyne (split-load) phase inverter.** Architecturally this is just a
*third* `TriodeStage` instance -- the same Dempwolf-Zölzer current
equations already verified for V1/V2A -- not a new struct. A cathodyne is
a cathode-unbypassed common-cathode stage (the same physics V1 already
got), except its plate resistor and cathode resistor are matched
(`R_a=R_k`) instead of `R_a≫R_k`, which is what makes its plate and
cathode outputs come out roughly equal in amplitude and opposite in phase
-- the whole point of the stage, driving the two power-tube grids without
a transformer. `TriodeStage` gained one addition: `getCathodeAc()`,
exposing the cathode voltage's own AC deviation (`V_k − V_{k0}`, already
tracked internally by `cathodeUnbypassed` for V1's own purposes) as this
stage's second output.

Component values (`R_a=56k`, `R_k=56k` plus a `1.5k` bias-balance padding
resistor, combined here as a single lumped `57.5k`) are the real 5E3 V2B
values -- cross-confirmed from Rob Robinette's schematic and independent
tube-amp-DIY sources (ampbooks.com's 5E3 circuit analysis; tdpri.com forum
threads specifically on 5E3 phase-inverter resistor balancing) rather than
taken from one source alone.

Verified in a standalone harness before wiring in: the DC bias solve
converges (`V_a0≈296.9V`, `I_a0≈56µA`, `V_k0≈3.21V`, both Ohm's-law checks
exact), and — the property that actually matters for a cathodyne — driving
it with a small sine and measuring the two outputs gave a **0.97:1
amplitude ratio and a negative cross-correlation** (genuine antiphase),
measured directly rather than assumed from the topology. Stable under an
impulse/hot-input stress test.

**6V6GT power tube.** A beam tetrode is different physics from a triode --
the screen grid shields the control grid from the plate's influence, so
plate current becomes nearly independent of plate voltage past a knee,
instead of rising with it via `μ` the way a triode's does. `PentodeStage`
implements Norman Koren's pentode/beam-tetrode SPICE model (Koren, "Improved
Vacuum-Tube Models for SPICE Simulations," Glass Audio Vol. 8 No. 5, 1996;
equations confirmed against an academic source — J. Vanderkooy-style SPICE
tube-modeling literature — that reproduces Koren's original formulas
verbatim, cross-checked against Koren's own published SPICE tube library):

- `E1 = (V_{g2}/K_p)·ln(1+exp(K_p·(1/μ + V_{g1}/V_{g2})))`
- `I_a = E1^{Ex}/K_{g1} · atan(V_a/K_{vb})` — the `atan` term is the actual
  physical signature of a pentode/tetrode's "knee": once `V_a` clears a few
  multiples of `K_{vb}`, `atan` saturates toward `π/2` and plate current
  stops responding to further plate-voltage swing.
- `I_{g2} = max(0, V_{g2}/μ + V_{g1})^{Ex} / K_{g2}` (screen current, used
  here only inside the self-bias solve — see scope note below)

Real 6V6-GTA parameters (`μ=10.70, Ex=1.310, K_{g1}=1672.0, K_{g2}=4500,
K_p=41.16, K_{vb}=12.7`) are Koren's own published fit (his SPICE tube
library, `Koren_Tubes.cir`, itself sourced from a GE datasheet) — not
guessed or curve-fit for this project. `V_b=373V` and screen voltage
`=295V` (held fixed) are Robinette's 5E3 schematic values; the shared
`250Ω` cathode-bias resistor and `8k` push-pull output-transformer primary
(reflected to `2k` per tube — plate-to-plate impedance divides by 4 for a
center-tapped winding) are likewise his.

Two deliberate scope boundaries, called out honestly rather than silently
assumed: screen voltage is held fixed rather than given its own dynamic
sag (the existing sag detector already models supply sag heuristically,
and building real screen-supply dynamics would be a topology change beyond
this pass); and the self-bias solve treats each tube's `250Ω` cathode
resistor as if it belonged to that tube alone, rather than modeling the
real shared-resistor coupling between the two power tubes (also a topology
change beyond "replace the per-tube current equation" — see
`PentodeStage`'s own comment).

Verified in a standalone harness before wiring in: the DC bias solve
converges to **`I_a0≈37.1mA` idle plate current** — squarely inside a real
6V6's typical class-AB operating range — with both plate and cathode
Ohm's-law checks exact. Three new calibration constants
(`cathodyneInputScale`, `powerStageInputScale`, `powerStageOutputScale`)
were derived the same "measure, don't guess" way as `outputCalibration`/
`v1GainCompensation` above: swept across the full realistic Drive ×
input-loudness range plus stress-test extremes well beyond what the real
signal path can deliver (input levels the upstream `safetyCeiling` clamp
already rules out). Result: quiet playing stays clean, loud playing at max
Drive reaches solidly toward but not fully into the existing output-
transformer knee's saturated asymptote — real headroom, not a tuning miss,
since a beam-tetrode power stage genuinely is less Va-sensitive/more
current-limited than the preamp triodes ahead of it, so most of this amp's
dirt still comes from V1/V2A upstream, matching how a real lower/medium-
gain tweed circuit actually behaves. Bounded and finite across the entire
sweep, including a hard-impulse stress test (peak 0.9934, no blow-up).

## Amp CPU (Newton iteration reduction)

The three tube stages each re-solve their cathode/operating voltage every
sample with a warm-started Newton-Raphson iteration. "Warm-started" is the
math term for seeding the solve from the previous sample's answer — not tube
warm-up; the tube's thermal state is not modeled. Profiling on Apple Silicon
showed those iteration *loops*, not the transcendental calls, are ~2/3 of
`AmpModule`'s CPU. The shipped counts were `TriodeStage` 4, `CathodyneStage`
5, `PentodeStage` 6 iterations, and the warm start (the seed barely moves at
audio rates) means they were far past convergence.

Cutting them to 2/3/4 (triode/cathodyne/pentode) dropped the amp from
**56.65% → 35.99%** of the real-time budget at 4× oversampling (Vintage 5E3,
`AmpBenchmark`) — while *keeping* 4× oversampling; no oversampling-quality
trade was made. Fidelity was verified two ways: a steady-state sweep across all 7 voices ×
drive {0, 0.5, 1.0} gave a worst-case RMS drift of 0.048% (~0.004 dB,
inaudible), and a direct side-by-side A/B (both versions compiled into one
binary, fed the same steady / blocking-distortion / impulse inputs) came out
**bit-exact** on every voice — the warm-started Newton solve reaches float
precision within 2–3 iterations, so the removed iterations were no-ops and
the CPU cut is lossless. `AmpValidation` still passes (no NaN/Inf/blow-up)
at 48/96/192 kHz.

Two redundant-computation fixes shipped alongside for completeness; both are
real but measured <1.8% combined (the iteration cut is the actual win):

- `softplusSigmoid()` folds the per-sample `softplus` and `sigmoid` into one
  shared `exp()` with branchy tails (`kx > 6` / `kx < -6` return directly),
  since `sigmoid` is `softplus`'s derivative and both share the same `e`.
- `pow(h, γ−1) == pow(h, γ)/h` for `h > 0` — replaces a second `powf` with a
  divide.

The lesson (recorded in `PREFERENCES.md`) holds: an earlier pass over-sold
removing ~40% of the transcendental calls as a win when it moved <1.8% — on
this hardware the per-sample loops are the cost, so the real optimization is
fewer, still-converged iterations, not fewer expensive calls.

## UI

`layoutHorizontalRackSection()` colours a section's title/toggle text
dark-on-light or light-on-dark based on `plateIndex` (Compressor/Tremolo/
Reverb use the plugin's actual light cream/brass plate art, everything
else a dark plate) — but each knob's own caption label was coloured once
in `buildSection()` from a separate, never-correctly-set `lightLabels`
flag, so Compressor's captions ("Comp"/"Attack"/"Tilt"/"Mid"/"Level")
stayed the pale tan meant for dark plates, nearly invisible against its
actual cream background even though the section title right next to them
read fine. Fixed by having the per-knob colouring reuse the same
`lightFace` plate-brightness check the title already uses, rather than a
second, independently-set flag that only one of the two ever got right.

July's Waveform/D-C-V switches and Delay's Plexer-Copier/mode switches
were each pinned to their card's top and bottom edges, leaving most of the
card's height as dead space between two controls that are really one
paired cluster (which waveform + how wet; which engine + that engine's own
secondary toggle). Both now stack close together with a small fixed gap,
centred vertically in the card.

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
40. The Amp page's background is `tweed_main.png` (a full stage scene --
curtain, brick walls, spotlights, the amp itself already framed within
it), replacing the earlier `tweed_amp.png` close-up cutout. Drawn at its
own native pixel size, centred, with no scaling and no crop of any kind --
the knob bar and Cab A/B cards below are opaque and painted afterward, so
they still cleanly cover it wherever the two overlap.

The odOrder rocker switch (Bull-first/Breaker-first) was centred in the
whole remaining column height below its label, leaving a large, uneven gap
between the switch and its own "BULL FIRST"/"BREAKER FIRST" caption pinned
to the bottom of the card. Now sits directly above that caption with a
small fixed gap instead.

Klon is displayed as **Bull** throughout the UI and in every one of its
parameters' DAW-visible display names (`Bull On`, `Bull Gain`, `Bull
Treble`, `Bull Level`, `odOrder`'s "Bull -> Breaker"/"Breaker -> Bull"
choices) -- parameter IDs stay `klon*`/`odOrder` internally, and the
`KlonModule` class keeps its name too, so old presets and any code
referencing the module are unaffected. Same pattern as Plexer/Copier
already displaying EchoModule/CarbonCopyModule's real-pedal names while
keeping their own internal identifiers.

## Signal continuity on toggle (Comp/Klon/Breaker)

Whether a stage's signal is "buffered" here really means one specific
thing: does toggling it on/off introduce a discontinuity (a click/pop, or
an abrupt level jump) into the audio stream, or does the transition happen
smoothly. Tremolo/July/Echo/Copier were already fixed for this in an
earlier pass (fade the effect's own amount to 0, then reset, rather than
snapping to silence mid-cycle) — Compressor/Klon/Breaker were not, and
confirmed to still hard-cut instantly (`if (! enabled) return;`, no fade at
all) when investigated here. Concretely: a drive/distortion stage's wet
output can sit at a meaningfully different instantaneous value than the
dry signal at the moment of a toggle, so an instant switch between the two
is a real, audible discontinuity — not a hypothetical one, and not
specific to digital plugins either (it's the same reason "buffered
bypass" exists as a design choice in real pedal hardware, there to avoid a
switch-induced pop, load-related tone loss across a pedalboard, and
DC-offset/RF issues true-bypass switching is prone to).

Fixed by crossfading each stage's output from a snapshot of its own dry
input to its normal processed output over ~15ms whenever its on/off state
changes, rather than adding fade-aware internals to three DSP modules with
three different architectures — Compressor's gain-reduction envelope,
and Klon/TS9's oversampled WDF clippers, are unrelated pieces of state
that didn't need touching to fix this, and touching them risked
introducing a new bug into circuitry that had just been carefully
harness-verified. The crossfade lives one level up in `PluginProcessor`
instead: each stage's dry input is snapshotted immediately before it
runs, and `crossfadeToggle()` blends the stage's already-processed output
back toward that snapshot using a `juce::SmoothedValue` ramp. Each stage
only actually runs while active or still fading out, settling back to a
fully-skipped, zero-cost state once the fade completes -- toggling it
doesn't leave it silently running (and paying its oversampling cost, for
Klon/TS9) forever after being turned off.

Deliberately **not** done: making each stage's own *reported* latency
change with its on/off state, or running its oversampler at 100% duty
cycle just to keep the plugin's actual internal delay constant whether a
stage is bypassed or not. `setLatencySamples()` is set once in
`prepareToPlay` from a fixed worst-case sum (Klon + TS9 + Amp's
oversampling latency, regardless of which stages are currently on) and
never changes again during a session -- changing it dynamically would
force the host to renegotiate PDC (plugin delay compensation) on every
single toggle, which is a far more disruptive glitch (typically a brief
audio engine reset/dropout in most DAWs) than the sub-millisecond timing
drift that not doing so leaves on the table. A guitar stomp-pedal effect
being toggled mid-performance doesn't need sample-accurate phase alignment
against other tracks the way, say, parallel drum processing might; the
actually-audible problem was the click, and that's what got fixed.

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

(This section has drifted out of date more than once -- most of the module
descriptions above predate the pedalboard rewrite (one flat, reorderable
strip of tiles replacing the old 4-tab UI), the amp's Vox/Fender/JTM45/
Mark I/JC-120 voices, and the `FangsModule`/`BisonModule`/`GrowlModule`/
`DimensionChorusModule`/`ChannelEQModule`/`SpaceEchoModule` pedals, none of
which are documented in "Modules" above yet. Ask if you want that brought
current rather than trusting this file blindly.)

**Circuit-accuracy pass, checked against real schematics one pedal/amp at a
time (see "Schematic / reference sources" below for what was actually
read) -- current status:**
- Done: Marshall JTM45, Mesa/Boogie Mark I, and Roland JC-120 amp voices
  (new); Deluxe 63, RE-201/Satellite, and Tweed 5E3 amp accuracy upgrades.
  RE-201's built-in reverb was added then removed again -- redundant with
  the standalone Spring pedal, which does the same job with full user
  control over the same underlying convolution engine.
- Done: TS9's clipper input resistor was mislabeled (4.7k, real value
  10k -- confirmed against BYOD's own live source, not just the schematic
  image). `FangsModule` (RAT/Distortion+ archetype) restructured from
  diode-in-feedback to the real gain-stage-then-diode-clip-to-ground
  topology both real schematics show. `BisonModule` (Big Muff archetype)
  fixed the opposite way -- from a shunt clamp to the real diode-in-
  feedback topology -- plus recalibrated (the old constants left Sustain
  almost inert against the new topology). `GrowlModule` (Fuzz Face
  archetype): the Fuzz knob now also drives the second clip stage,
  matching the real circuit's single shared Q1/Q2 feedback loop instead
  of only touching Q1's bias current.
- Not done: TS9's feedback network is still missing a real 51pF cap and a
  4.7k+47nF branch (the documented source of the "mid-hump"); the real
  fix needs general N-port R-type-adaptor support added to `WDFCore.h`
  (only has 2-port Series/Parallel today) plus porting BYOD's own
  symbolically-derived scattering matrix (already pulled verbatim, not
  yet wired in).
- Not done: Klon -- not re-investigated this pass. `KlonModule`'s own
  header comment already claims it was built from and verified against
  jatinchowdhury18/KlonCentaur's traced schematic, so there may be little
  gap here, but that hasn't actually been re-checked against the real
  schematic the way the other five were.
- Not done: `DimensionChorusModule` (Roland SDD-320 Dimension D) -- a real
  source (Roland's original Service Notes) has been identified but not
  yet fetched/read.
- Not done (deliberately deferred as the largest single item):
  `ChannelEQModule` ("Redface", Neve 1073-style). Real schematics for the
  top-level channel amp (EH10023) and the BA283 card's full discrete-
  transistor circuit (EX/10283) are already in hand from an 11-page
  archive.org document, only 6 of 11 pages read so far -- still need
  BA284/BA182/BA205/BA211's own circuit diagrams, then real transfer
  functions for all 4 switched active-filter networks (HPF, Low-Freq
  shelf, Presence peak, HF shelf). Open question not yet settled: does
  the "archetype only, don't clone a commercial pedal's exact BOM" policy
  set for Fangs/Bison/Growl also apply to a rack-mount studio EQ clone,
  or is that a different case?
- Cab IR loading is basic next to something like Ignite Amps' NadIR
  (dual-IR cab convolver): no Resonance control (speaker-cone/power-amp
  interaction, independent of whatever IR is loaded), no manual timing
  offset between the A/B slots beyond automatic alignment + a binary phase
  flip, no automatic phase-polarity detection on load (invert is manual-only
  right now). Discussed and deliberately not pursued yet.

## Schematic / reference sources

Real schematics/documentation actually read (via WebFetch + rendering the
saved file as images, since none of these are text-searchable PDFs) for
the amp-voice and pedal-accuracy work above -- not worked from memory or
secondhand descriptions:

**Amps:**
- [el34world.com Mesa Boogie archive](https://el34world.com/charts/Schematics/files/Mesa_boogie/Mesa_boogie_Schematics.htm) --
  [`Boogie_mki_reissue.pdf`](https://el34world.com/charts/Schematics/files/Mesa_boogie/Boogie_mki_reissue.pdf)
  (Mark I), plus the Lonestar/Lonestar Special PDFs in the same archive
  (referenced during the amp-selection discussion, not modeled).
- [el34world.com Roland archive](https://el34world.com/charts/Schematics/files/Roland/Roland_Schematics.htm) --
  [`Roland_jazz_chorus.pdf`](https://el34world.com/charts/Schematics/files/Roland/Roland_jazz_chorus.pdf)
  (JC-120, Dec-1984 factory service manual) and
  [`Roland_re_101_re_201_service_manual.pdf`](https://el34world.com/charts/Schematics/files/Roland/Roland_re_101_re_201_service_manual.pdf)
  (RE-201/Satellite).
- [drtube.com Marshall JTM45 archive](https://www.drtube.com/marshall-jmp/) --
  "Basic Schematic for Marshall Trem Amps (Types 1961, 1962, 1987T)".
- Real KT66/6L6GC Koren-fit tube parameters cross-verified via web search
  against community-published SPICE models (KT66) and Cohen & Hélie's
  DAFx-10 "Real-Time Simulation of a Guitar Power Amplifier" paper
  (6L6GC), same methodology as the 6V6GT/EL84 parameters already cited
  above.

**Stompboxes** (all from
[el34world.com's Effects archive](https://el34world.com/charts/Schematics/files/Effects/Effects_Schematics.htm)):
- [`Ibanez_ts9_tubescreamer.pdf`](https://el34world.com/charts/Schematics/files/Effects/Ibanez_ts9_tubescreamer.pdf) --
  the real Ibanez TS-9 schematic, including Jack Orman (AMZ)'s clean
  traced-from-a-real-unit diagram with explicit TS9-to-TS808 component
  differences called out.
- [`Proco_rat_dist.pdf`](https://el34world.com/charts/Schematics/files/Effects/Proco_rat_dist.pdf) --
  ProCo RAT.
- [`Mxr_dist_plus.pdf`](https://el34world.com/charts/Schematics/files/Effects/Mxr_dist_plus.pdf) --
  MXR Distortion+.
- [`Eh_bigmuff.pdf`](https://el34world.com/charts/Schematics/files/Effects/Eh_bigmuff.pdf) --
  Electro-Harmonix Big Muff Pi.
- [`Hendrix_fuzzface.pdf`](https://el34world.com/charts/Schematics/files/Effects/Hendrix_fuzzface.pdf) --
  Jimi Hendrix (Dunlop JH-2) Fuzz Face.
- [Chowdhury-DSP/BYOD](https://github.com/Chowdhury-DSP/BYOD)'s
  `TubeScreamerWDF.h` -- fetched directly (verbatim, via `curl`, not the
  summarized WebFetch path) to cross-check TS9's WDF clipper topology
  against a real reference implementation rather than the schematic image
  alone; this is the same repo `WDFCore.h`'s own header already cites for
  Klon/TS9's original derivation.

**Not yet modeled, sourced for the still-pending Redface/Neve work:**
- [AMS Neve 1073N official user manual](https://www.ams-neve.com/wp-content/uploads/2021/08/1073n_user_manual_issue_1_4.pdf) --
  current specs/control ranges, no schematics (just a drawing-number index).
- [archive.org: Neve 1073 channel amplifier schematic, EH10023](https://archive.org/details/neve_1073_channel_amplifer_schematic_EH10023) --
  the real top-level channel-amp interconnect drawing.
- [archive.org: Neve 1073 fullpak](https://archive.org/details/neve_1073-fullpak) --
  the full card-level pack (BA283/BA284/BA182/BA205/BA211 schematics),
  11 pages, only 6 read so far.
