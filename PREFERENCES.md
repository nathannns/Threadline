# PREFERENCES.md

Portable working preferences for this project — written so a *different*
model/tool picking this project up (e.g. a DeepSeek-backed session) has the
same behavioral context a fresh Claude Code session would already have.
Pairs with `context.md` (reference links) and `README.md` (technical docs).

## Code style preferences

Observed directly from the existing codebase (`Source/**/*.h`, `.cpp`) —
match it, don't introduce a second style:

- **4-space indentation**, no tabs.
- **Allman brace style** — opening brace on its own line for functions,
  classes, structs, and control blocks:
  ```cpp
  void prepare (const juce::dsp::ProcessSpec& spec, int oversamplingMode = 1)
  {
      ...
  }
  ```
- **Space before the opening parenthesis** on function calls/definitions —
  `prepare (spec)`, `juce::jlimit (0.0f, 1.0f, x)`, `std::tanh (x)`, not
  `prepare(spec)`. This is JUCE-house-style and applied consistently
  throughout; don't drop the space just because it looks unusual coming
  from another codebase's convention.
- **Always use braces** on `for`/`if`, even single-statement bodies, e.g.
  `for (auto& f : dcBlock)` followed by a braced block, not a bare
  statement.
- **camelCase** for everything — member variables, local variables,
  functions, and `static constexpr` constants alike (`driveAmount`,
  `sampleRate`, `outputCalibration`, `safetyCeiling`) — no `ALL_CAPS`
  constants, no `snake_case`, no Hungarian prefixes (`m_foo`).
  Multi-variable constant declarations combine on one line where related:
  `static constexpr float beta = 110.0f, ibKnee = 150.0e-6f;`.
- **`const auto` for locals** wherever the type doesn't need to be spelled
  out and the value won't be reassigned.
- **Semicolons always**, standard C++; no stylistic omissions anywhere.
- **Comments are dense but purposeful, never decorative.** This codebase
  writes long comments — but every one explains a non-obvious *why*
  (a real schematic value's source, a physical/circuit reason, a bug that
  was caught and how, a deliberate scope boundary) — never restates *what*
  the code already says through naming. Don't add a comment unless
  removing it would genuinely lose information a future reader needs.
- Real physical/circuit constants get a comment citing where the number
  came from (a schematic, a datasheet, a paper, "empirically tuned via a
  standalone harness") — a bare magic number with no provenance is treated
  as a smell in this codebase, not the norm.

## Preferred response style

- **Concise, direct, low ceremony.** Short sentences, minimal punctuation,
  no filler preamble ("Great question!", "I'll now proceed to..."). State
  what changed and why in a couple of sentences, not a wall of prose.
- **Detailed reasoning belongs in commit messages and code comments, not
  chat replies.** The user reads chat for status/decisions; the *why* goes
  where it's permanently useful (git history, in-file comments) instead of
  scrolling past in a terminal.
- **Offer a short multiple-choice question at a real decision fork**
  rather than a long paragraph laying out options — this user consistently
  engages with 2-4-option questions and answers tersely.
- **Be honest about uncertainty, negative results, and things that didn't
  work** rather than overclaiming — this user explicitly values a
  transparent "this fix was real but the measured impact was
  noise-level" over a confident-sounding overstatement.
- **Casual, lowercase, terse input style from the user is normal, not a
  sign to mirror sloppiness back** — match their brevity, not their
  informality in technical explanations.

## Known approaches that failed (don't repeat these)

Circuit/DSP modeling mistakes, each real and each caught by verification
(a standalone numeric harness, or cross-checking a second real source) —
listed so the same category of mistake doesn't get re-made from a
different starting point:

- **Modeling a triode/transistor's AC signal as a bare zero-centered
  deviation, skipping the real DC operating point.** Broke negative
  feedback and clipped almost the entire waveform at sub-millivolt input
  (AmpModule's V1). The fix is always: solve the real quiescent
  voltage/current first (bisection or Newton-Raphson against the real
  supply voltage and load), then run the AC dynamics as a perturbation
  around *that* point.
- **Treating a plate-load resistor and an interstage coupling cap as if
  they were in parallel at the same node.** They're not — the coupling
  cap sits in *series* to the next stage's grid. The mistaken parallel-RC
  reading turned each stage into an unintended ~80Hz lowpass, cascading
  into a muffled/boxy sound. When two components look adjacent in a
  schematic, verify series-vs-parallel from the actual node topology, not
  visual proximity.
- **Removing a stabilizing capacitor entirely because it "isn't a real
  component."** A tiny (220pF) cap in AmpModule's plate-load network isn't
  modeling anything physical — it's numerically stabilizing a one-sample-
  delayed feedback loop. Deleting it went unstable (pinned to the safety
  ceiling) almost immediately. Some elements exist for numerical reasons,
  not circuit-fidelity reasons — check *why* something is there before
  assuming it's a modeling error.
- **Copying a resistor label without cross-checking which schematic
  element it actually names.** TS9Module's input resistor was coded as
  4.7k (labeled as "R4" in a stale comment) when the real input resistor
  is R5=10k — R4 is real, but it's part of the *feedback* network, not the
  input. Caught by fetching the reference implementation's raw source
  (`curl`, not `WebFetch` — see below) and cross-checking against it, not
  by re-reading the same schematic image more carefully.
- **Assuming a clipping topology from a pedal's general "family" reputation
  instead of checking its actual schematic.** Big Muff-archetype clip
  stages were built as a post-gain shunt clamp (correct for RAT/
  Distortion+, wrong for Big Muff), while the RAT/Distortion+-archetype
  clipper was built with diodes inside the feedback loop (correct for
  TS9/Klon, wrong for RAT/Distortion+). The two pedal families clip in
  opposite structural ways; check the real schematic per pedal, don't
  extrapolate from a similar-sounding pedal's topology.
- **Reusing calibration constants across a topology change.** After fixing
  Bison's clip-stage topology, the old `interStageGain`/`Rin` constants
  (tuned for the previous, wrong topology) left the Sustain knob almost
  fully saturated even at its minimum setting — a standalone harness sweep
  was needed to re-derive working constants; numbers tuned for one
  topology don't carry over to a structurally different one, even if the
  component names stay the same.
- **Undamped Newton-Raphson steps on a steep exponential (diode/transistor
  junction) equation.** An early version of Growl's base-current solve
  diverged into a runaway, physically-nonsensical (many-amp) state at
  several Bias/Fuzz settings — `exp()`'s curvature is severe relative to
  how small the true equilibrium voltage actually is here. Needed a
  per-iteration step clamp *and* a hard voltage-range clamp, not just a
  smaller learning rate.
- **Picking a saturation "knee" constant without checking where the
  quiescent (silent-input) operating point actually sits first.** Growl's
  `ibKnee` was originally set below the realistic quiescent bias current
  range, so the stage was already deep in its flat/saturated region at
  rest — real audio riding on top barely moved the output at all, which
  read as "everything is muted" rather than a broken signal path. Always
  check where silence lands on the curve before tuning how driven signal
  behaves on it.
- **Trusting `WebFetch`'s summary for anything requiring numeric/code
  exactness.** It always processes content through a small summarizing
  model, even for plain-text/code URLs — fine for prose, but it can
  silently paraphrase or drop precision from a scattering matrix or
  component value table. For schematic PDFs it also can't read
  scanned/vector images at all (always returns a "this is binary data"
  apology) — but it *does* still save the raw file locally regardless, so
  the real workflow is: `WebFetch` to save the file, then `Read` the saved
  path directly to view it as an image (or `curl` a raw source URL
  directly when the content is plain text/code and exactness matters).
- **A hardcoded, fixed-pair UI design where the actual requirement was
  flexible/generic.** An early Parallel Box design used a fixed Chorus+
  Tremolo pair; the actual want was any-pedal-type parallel routing with
  single-instance-per-pedal enforcement across the whole board. When a
  feature request says "a box that does X" without specifying exactly
  which two things, don't assume a hardcoded pair — ask, or default to the
  generic version.
- **A selection UI that disables "already in use" options without checking
  how many things default to being in use.** The Parallel Box's slot
  pickers were once entirely unselectable because the code disabled every
  pedal already occupying a main-strip tile — since nearly every pedal
  defaults to being on the main strip, almost all options ended up
  disabled. When an exclusion rule is added, sanity-check it against the
  *default* state, not just the empty-board case.
- **Expecting micro-optimizations to move a Newton-Raphson-heavy DSP
  path's CPU cost.** Three real, correct redundant-computation fixes in
  `AmpModule`'s tube stages (caching a per-block-invariant `exp()`,
  deduplicating a repeated `gridCurrent()` call, hoisting a loop-invariant
  `atan()`) measured under 1% difference in a proper A/B benchmark — the
  real cost is the per-sample Newton-Raphson solve itself, not incidental
  redundant calls. Don't oversell a "found and fixed a real bug" as a
  performance win without actually benchmarking before/after.
  **Resolution:** the fix that actually moved it was cutting the warm-started
  Newton *iteration counts* (`TriodeStage` 4→2, `CathodyneStage` 5→3,
  `PentodeStage` 6→4), landing ~36% at 4× (56.65%→35.99%) — the loops, not
  the calls, were the cost. A direct side-by-side A/B (both versions in one
  binary, steady + blocking-distortion + impulse inputs) came out bit-exact
  on every voice: the warm-started solve hits float precision within 2–3
  iterations, so the removed iterations were no-ops.

## Current task status

(Point-in-time snapshot — check `context.md`'s "Schematic / reference
links" section and `git log` for anything more recent than this file's
last edit.)

**Done and shipped:**
- **Local UI/cab asset migration (2026-08-21):** Threadline now embeds the
  replacement enclosure/knob/LED asset directories and the replacement
  22-IR cabinet library. Cab A/B are active again as independent convolution
  instances: A feeds left, B feeds right when both are enabled, Balance
  controls their relative level, and mono hosts retain a parallel blend.
  Available pedal-specific knobs are wired for Comp, Bull, Breaker, Fangs,
  Bison, Growl, Dynamix, Tape, Tremolo, and July; other controls use the
  shared effects knob. Pedal title chrome now sits above rather than over
  the enclosure image. `channel_enclosure.png` is the Redface tile art.
- **Low-risk verification pass (2026-08-21):** `CabValidation` now exercises
  the production dual-cab router (A-only, B-only, stereo A-left/B-right,
  both Balance endpoints, and mono fallback) in addition to the existing
  finite/bounded convolution sweep at 48/96/192k. The unchanged
  `AmpLevelProbe` reports near-identical synthetic RMS for all seven voices
  across Drive 0..1, despite the real-listening report that JC is quiet and
  JTM45 loud. That is a negative result: the present sine/RMS probe does not
  reproduce the perceptual imbalance and must not be used to dismiss it or
  justify a blind normalization change.
- All 6 originally-requested amp items: Marshall JTM45, Mesa/Boogie Mark I,
  Roland JC-120 (new voices); Deluxe 63, RE-201/Satellite, Tweed 5E3
  (accuracy upgrades). RE-201's embedded reverb was added, then removed
  again — redundant with the standalone Spring pedal.
- Pedalboard/header UI polish: Tap Tempo relocated to the header, `<`/`>`
  preset-nav buttons reordered adjacent, duplicate preset dropdown arrow
  removed, Satellite renamed to "Satellite - 201", pedalboard trailing
  margin.
- Pedal circuit-accuracy pass, 5 of the "tractable" batch:
  - **TS9**: fixed clipper Rin (4.7k → 10k, real value).
  - **Fangs** (RAT/Distortion+ archetype): restructured clipper to the
    real gain-stage-then-diode-clip-to-ground topology.
  - **Bison** (Big Muff archetype): restructured to the real diode-in-
    feedback topology (the opposite fix from Fangs), plus recalibrated.
  - **Growl** (Fuzz Face archetype): Fuzz knob now also drives stage 2,
    matching the real shared Q1/Q2 feedback loop.
- `context.md` and this file created.
- **Amp CPU optimization**: cut the warm-started Newton iteration counts in
  the three tube stages (`TriodeStage` 4→2, `CathodyneStage` 5→3,
  `PentodeStage` 6→4), dropping Vintage 5E3 from 56.65%→35.99% of the
  real-time budget at 4× oversampling while *keeping* 4×. Direct side-by-side
  A/B (steady + blocking-distortion + impulse) came out bit-exact on all 7
  voices; `AmpValidation` still clean at 48/96/192k. See README "Amp CPU".
- Amp voice loudness normalization across the Drive range (drive-dependent
  `perVoiceNormaliseByDrive[7][11]` table replacing the scalar per-voice
  gain), 583b1e0. The user has since re-flagged residual imbalance (Jazz
  Chorus still quiet, JTM45 still loud) — see README "Issues / next work" #1.
- Tape Volume knob added to both repos (Threadline c46c1ee, Rockalizer
  2e6b03f): `tapeVolume` 0–100%, default 100% unity, final linear scale in
  `TapeModule::processCore` after the drive trim.
- Tape research (2026-08-20): Studer A800 service manual + Tascam 244
  schematic OCR'd (image-only scans, Vision-framework OCR — see context.md).
  Extracted A800 specs (bias 240 kHz / erase 80 kHz, NAB/CCIR EQ time
  constants, ~1% THD, S/N ~66–70 dB) and the 244's architecture (TA7136AP
  preamp, NJM4558D dbx, bias osc). Upgrade direction set: real EQ curves +
  head bump + bias + grounded Studio/Cassette machine models (README #2).
- The forward-looking backlog (17 issues across both repos) now lives in
  README.md under "Issues / next work"; this file's queue below tracks only
  the original circuit-accuracy pass.
- **UI/cosmetic backlog sweep (2026-08-20)**, explicitly scoped to layout
  and interaction only — no DSP/topology/effect-behavior changes. Six of
  the 17 backlog items shipped, verified by a clean CMake build of both
  repos' Standalone targets plus a visual screenshot check of Rockalizer's
  Echo pedal:
  - `[T+R]` **Options button**: both editors now call
    `addMouseListener (this, true)` and close the Options panel from a new
    `mouseDown` override on any click outside the panel/gear button
    (previously only the gear button itself could close it).
  - `[T]` **Expandable bar opens downward only**: no dedicated "expandable
    bar" widget exists in this codebase — the real mechanism is
    `juce::PopupMenu` direction. Added
    `.withPreferredPopupDirection (downwards)` to the pedal-add/insert menu
    (`PedalboardComponent::showAddMenu`) and the Parallel-slot picker
    (`ParallelTile::showSlotMenu`), matching the preset dropdown's existing
    rule (`showPresetMenu`).
  - `[T]` **Pedal 'x' close button**: `removeButton` now disables itself
    synchronously inside its own `onClick`, before `removePedal`'s deferred
    `MessageManager::callAsync` rebuild runs, so a fast second click can't
    double-fire the removal.
  - `[T]` **Pedal name → swap**: clicking a tile's name label now opens a
    new `PedalboardComponent::showSwapMenu`, which replaces that pedal in
    place (same board position) instead of the "+ Add Pedal" picker's
    append-only behavior. Wired via a new `onNameClicked` callback on
    `PedalTileComponent`, hit-tested against `nameLabel`'s bounds inside the
    tile's existing `mouseDown` (the label itself still doesn't intercept
    clicks, same as before).
  - `[R]` **Echo UI tidy-up**: the ECHO title wordmark was 42px lower than
    Tape/Chorus/Spring's shared y=438 because Echo's 7th knob (Drive) and
    its Pattern/Sync selectors needed a whole extra row. Fixed by stacking
    Sync directly below Pattern in the row's left column (freeing the row
    Drive used to have alone) and moving Drive to the row's right column —
    title now lines up with its siblings.
  - `[R]` **UI consistency**: survey found Echo was the only outlier (all
    other pedal tiles already share tile/knob size and label conventions);
    covered by the Echo fix above. Flagged but *not* changed: Rockalizer's
    `resized()` hardcodes `tapeCardX`/`chorusCardX`/`echoCardX`/
    `springCardX` separately from `paint()`'s computed `cardWidth`/`gap` —
    currently in sync, not a visible bug, so left as a latent-risk note
    rather than a refactor.

**Not done — queued, in this order (user chose "tractable first, defer
the biggest"):**
1. **TS9's deeper fix** (explicitly deferred): feedback network still
   missing a real 51pF cap and 4.7k+47nF branch (the documented "mid-hump"
   source). Needs general N-port R-type-adaptor support added to
   `WDFCore.h` (only has 2-port Series/Parallel today) plus porting
   BYOD's own scattering matrix (already pulled verbatim via `curl`, not
   yet wired in).
2. **Klon** — not re-investigated this pass. `KlonModule`'s own header
   comment already claims it was verified against
   `jatinchowdhury18/KlonCentaur`'s traced schematic, so there may be
   little gap here — needs an actual re-check, not an assumption either
   way.
3. **`DimensionChorusModule`** (Roland SDD-320 Dimension D) — source named
   by the user but not yet actually fetched/verified.
4. **`ChannelEQModule`** ("Redface", Neve 1073-style) — deliberately
   deferred as the largest single item. Real schematics for the top-level
   channel amp (EH10023) and the BA283 card's discrete-transistor circuit
   (EX/10283) are already in hand; still need BA284/BA182/BA205/BA211's
   own diagrams (5 of 11 pages of the archive.org "fullpak" unread), then
   real transfer functions for 4 switched active-filter networks. Open
   question not yet settled: does the "archetype only, no exact BOM"
   policy set for Fangs/Bison/Growl apply here too, or is a studio-EQ
   clone a different case?
