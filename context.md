# Threadline — working context

Not a technical doc (see `README.md` for that). This is reference links and
working preferences for continuing this project across sessions.

## Permanent amp-input rules

Treat these as non-negotiable project context for every future amp or
level-matching change:

1. The [Interface and Amp Sim Input Level Table](https://docs.google.com/spreadsheets/d/1bZHaapCiCg4RLIFqTS5KyUUVa4MwaqfxRCYk35Bvdrs/edit?gid=0#gid=0)
   is the calibration reference. Threadline targets the Focusrite instrument
   input at minimum hardware gain and uses `+12.25dBu = 0dBFS`.
2. Never normalize individual guitar DI signals or add arbitrary peak/RMS
   targeting, AGC, or automatic input compensation. Preserve pickup dynamics:
   humbuckers remain louder and drive the models harder than single coils.
3. Amp Gain operates inside the modeled circuit after its first input stage
   (V1 or the topology-equivalent). It must never change the expected interface
   sensitivity or act as a DI input calibration control.
4. All nonlinear pedals and amps share `GuitarSignalLevel.h`'s physical-voltage
   contract. Convert host sample to volts once on circuit entry and back once
   on circuit exit; never reinterpret a pedal output as a new interface DI.
5. Pedal Level is an output pot, not a hidden gain stage. Noon should measure
   near bypass loudness at representative Drive and maximum must not add an
   arbitrary post-circuit +6dB. Passive tone-stack insertion loss must not be
   compensated before a nonlinear tube; match voices only at final Output.
6. The Options input-calibration readout is informational only. It may show
   raw dBFS/volts and clipping risk, but must never become a target follower or
   alter audio. Tracking and offline Render oversampling remain separate
   1x/2x/4x choices, both defaulting to 4x. Stereo is the default; Mono always
   means centred dual-mono.
7. Live oversampling changes must not directly overlap differently delayed
   1x/2x/4x paths. Threadline fades the full chain to a 12ms silent switch
   boundary, resets the already-prepared nonlinear engines, and fades back in.
   Do not replace this with an unaligned crossfade.

## 2026-08-22 amp-stage findings

- Analysis-only taps (absent from production builds) locate the common
  Boutique/Deluxe/JTM45/Mesa low-mid rise at the Bassman/AB763 tone-stack
  output feeding V2. V1 and its 72Hz coupling path attenuate rather than add
  bass. Verify physical pot taper/mapping against the chosen schematic before
  changing the network; do not hide it with an arbitrary high-pass filter.
- Boutique's 7.5kHz sentinel first develops severe fourth-harmonic energy at
  the cathodyne phase-inverter output: -24.30dBc at 30kHz versus -78.05dBc at
  V2 output. The downsampled 18kHz alias is -58.98dBc. If this is corrected,
  refine that stage rather than increasing oversampling for every amp.
- Compact measurements are stored in
  `Tests/Baselines/AmpStageAudit-2026-08-22.txt`.

## Reference / source links

### Amps (schematics actually read this project)

- [Interface and Amp Sim Input Level Table](https://docs.google.com/spreadsheets/d/1bZHaapCiCg4RLIFqTS5KyUUVa4MwaqfxRCYk35Bvdrs/edit?gid=0#gid=0)
  - Focusrite 2i2 4th Gen maximum instrument input: +12dBu at minimum gain.
  - Focusrite 2i2 3rd Gen maximum instrument input: +12.5dBu at minimum gain.
  - Threadline uses their +12.25dBu midpoint and does not peak-normalize DI.

- [Neural DSP: Tips for using your plugin](https://neuraldsp.com/getting-started/tips-for-using-your-plugin)
  — official Hi-Z/minimum-interface-gain guidance and saturated-input warning.
- [STL AmpHub manual](https://www.stltones.com/pages/manuals) — official
  Input Level Listener guidance and GE.M.IN.I. multistage-interaction overview.
- [Two Notes GENOME input calibration](https://helpdesk.two-notes.com/portal/en/kb/articles/calibrating-genome-s-input-your-audio-interface)
  — interface maximum-input calibration is separate from presets and gain.
- [Line 6 Helix manual](https://line6.com/data/6/0a06439cb91a5609df67966ca/application/pdf/Helix%20Owners%20Manual%20%28REV%20B%29%20-%20English%20%28%20Rev%20B%20%29.pdf)
  — documents separate Drive/Master behavior, sag/bias controls and modeled
  input-impedance choices.
- [Yamaha THR manual](https://usa.yamaha.com/files/download/other_assets/4/331514/THR_ZV05630_R1_en_web.pdf)
  — separates preamp Gain, power-stage Master and final Guitar Output.
- [Native Instruments: making ICM](https://blog.native-instruments.com/the-making-of-icm/)
  — explains why measured hardware behavior and component interaction are
  required in addition to schematics.
- [IK AmpliTube 5](https://www.ikmultimedia.com/products/amplitube5/) and
  [TONEX AI Machine Modeling](https://www.ikmultimedia.com/products/tonexecosystem/index.php?p=aimm)
  — official references for power-amp impedance matching, measured cabinet/mic
  data and dry/wet guitar-reference capture.
- [Two Notes GENOME manual](https://media.two-notes.com/product_manuals/en/software/genome/genome_user_guide.pdf)
  — TSM amplifier/power-amp stages and DynIR cabinet/microphone architecture.

The public material above supports architecture and validation practices, not
the companies' proprietary algorithms. For Threadline the safe immediate use
is offline measurement: `AmpCharacterProbe` records physical-level dynamics,
two-tone IM products, guitar-pluck spectral balance and an alias sentinel. Do
not use marketing claims as permission to add unmeasured saturation stages.
- [Macak & Schimmel, DAFx-10: Real-Time Guitar Tube Amplifier Simulation](https://www.dafx.de/paper-archive/2010/DAFx10/MacakSchimmel_DAFx10_P12.pdf)
  — nonlinear tube stages, phase splitter, frequency response, feedback, and
  oversampling for alias reduction.
- [Cohen & Helie, DAFx-10: Real-Time Simulation of a Guitar Power Amplifier](https://dafx.de/paper-archive/2010/DAFx10/CohenHelie_DAFx10_P45.pdf)
  — nonlinear differential circuit formulation and stable real-time solution.

- [el34world.com Mesa Boogie archive](https://el34world.com/charts/Schematics/files/Mesa_boogie/Mesa_boogie_Schematics.htm)
  — index page.
  - [`Boogie_mki_reissue.pdf`](https://el34world.com/charts/Schematics/files/Mesa_boogie/Boogie_mki_reissue.pdf) — Mesa Mark I (used).
  - `Boogie_Lonestar_Special.pdf`, `Boogie_lonestar.pdf`, `Mesaboogiemark_v.pdf` — referenced during amp-selection discussion, not modeled yet.
- [el34world.com Roland archive](https://el34world.com/charts/Schematics/files/Roland/Roland_Schematics.htm)
  — index page.
  - [`Roland_jazz_chorus.pdf`](https://el34world.com/charts/Schematics/files/Roland/Roland_jazz_chorus.pdf) — JC-120, Dec-1984 factory service manual (used).
  - [`Roland_re_101_re_201_service_manual.pdf`](https://el34world.com/charts/Schematics/files/Roland/Roland_re_101_re_201_service_manual.pdf) — RE-201/Satellite (used).
- [drtube.com Marshall JTM45 archive](https://www.drtube.com/marshall-jmp/) — "Basic Schematic for Marshall Trem Amps (Types 1961, 1962, 1987T)" (used).
- [el34world.com Matchless archive](https://el34world.com/charts/Schematics/files/Matchless/Matchless_Schematics.htm) — referenced, not modeled yet.
- Fender 5E3 (Robinette's writeup), Fender AB763 (Deluxe/Concert layouts), Vox AC30/Top Boost 1961 — used earlier this project for the Vintage5E3/Boutique/Vox/Deluxe63 voices; exact URLs not retained verbatim, re-search if revisiting those voices.
- Real tube parameters (Koren-fit, cross-verified via web search, not guessed):
  KT66 (JTM45), 6L6GC via Cohen & Hélie's DAFx-10 "Real-Time Simulation of a
  Guitar Power Amplifier" paper (Mesa Mark I), 6V6GT/EL84 (5E3/Deluxe63/Vox,
  from Koren's own published `Koren_Tubes.cir` SPICE library).

### Stompboxes (all from el34world's Effects archive)

- [Index](https://el34world.com/charts/Schematics/files/Effects/Effects_Schematics.htm)
- [`Ibanez_ts9_tubescreamer.pdf`](https://el34world.com/charts/Schematics/files/Effects/Ibanez_ts9_tubescreamer.pdf) — TS9 (used; includes AMZ/Jack Orman's clean traced diagram with explicit TS9→TS808 component diffs).
- [`Proco_rat_dist.pdf`](https://el34world.com/charts/Schematics/files/Effects/Proco_rat_dist.pdf) — ProCo RAT (used, Fangs).
- [`Mxr_dist_plus.pdf`](https://el34world.com/charts/Schematics/files/Effects/Mxr_dist_plus.pdf) — MXR Distortion+ (used, Fangs).
- [`Eh_bigmuff.pdf`](https://el34world.com/charts/Schematics/files/Effects/Eh_bigmuff.pdf) — Big Muff Pi (used, Bison).
- [`Hendrix_fuzzface.pdf`](https://el34world.com/charts/Schematics/files/Effects/Hendrix_fuzzface.pdf) — Fuzz Face (used, Growl).
- Klon Centaur — [ElectroSmash's analysis](https://www.electrosmash.com/klon-centaur-analysis) named as a source by the user; not yet re-checked against `KlonModule` this pass (the module's own header already claims it was built from/verified against `jatinchowdhury18/KlonCentaur`'s traced schematic).
- Roland SDD-320 (Dimension D, for `DimensionChorusModule`) — user named "Roland SDD-320 original Service Notes" as a real source but no verbatim URL was ever pasted/fetched; needs an actual search before use, don't guess a URL.

The 2026-08-22 archive-wide audit also visually inspected Boss NF-1, TR-2,
CE-1, CE-2, DC-2, DM-2 and GE-7; Ibanez GE9; DOD 280A; MXR Dyna Comp;
EHX Soul Preacher and Deluxe Memory Man; and Danelectro 9100. The durable
coverage/result table is `Tests/Baselines/EffectSchematicAudit-2026-08-22.md`.
Do not treat the index as a universal source: seven-band EQ drawings do not
define Threadline's nine-band EQ, and a TR-2/RV-3 circuit does not define the
project's harmonic tremolo or algorithmic acoustic reverbs.

### Reference implementations (verified, MIT-licensed, ported/cross-checked, not guessed)

- [jatinchowdhury18/KlonCentaur](https://github.com/jatinchowdhury18/KlonCentaur) — Klon clipper source.
- [Chowdhury-DSP/chowdsp_wdf](https://github.com/Chowdhury-DSP/chowdsp_wdf) — general WDF adaptor library `WDFCore.h` is a subset-port of.
- [Chowdhury-DSP/BYOD](https://github.com/Chowdhury-DSP/BYOD) — `TubeScreamerWDF.h` specifically; fetch its *raw* source via `curl` (not WebFetch, which summarizes through a small model and loses exact numeric/code fidelity) when precision matters.

### Neve 1073 / Redface (sourced, not yet implemented — biggest remaining item)

- [AMS Neve 1073N official manual](https://www.ams-neve.com/wp-content/uploads/2021/08/1073n_user_manual_issue_1_4.pdf) — current specs/control ranges only, no schematics.
- [archive.org: EH10023](https://archive.org/details/neve_1073_channel_amplifer_schematic_EH10023) — real top-level channel-amp schematic.
- [archive.org: 1073 fullpak](https://archive.org/details/neve_1073-fullpak) — full card-level pack (BA283/BA284/BA182/BA205/BA211), 11 pages, only 6 read so far.

### Other inspo (not yet used)

- [mod-audio/mod-plugin-builder](https://github.com/mod-audio/mod-plugin-builder) — LV2 build/packaging tooling, not DSP inspiration; low relevance.
- [AnClark/ClassicReverb-RE04](https://github.com/AnClark/ClassicReverb-RE04) — Roland-style reverb plugin, possible reference for the algorithmic `HallRoomReverbModule` if revisited.
- [xunil-cloud/CloudReverb](https://github.com/xunil-cloud/CloudReverb) — Dattorro/CloudSeed-family algorithmic reverb, same possible use.

### Tape machines (sourced 2026-08-20)

- Studer A800 24-track 2" service manual — local:
  `/Users/nathanielsantiaji/Downloads/A800_Serv.pdf` (536 pp, image-only
  scan, no text layer; OCR'd with the Vision tool below). Extracted specs:
  bias 240 kHz / erase 80 kHz; NAB EQ 50/3180 µs (7.5/15 ips) and 17.5 µs
  (30 ips); CCIR/IEC 70/35/17.5 µs (7.5/15/30 ips); freq resp 30 Hz–20 kHz
  ±2 dB @ 15 ips; S/N ~70 dB (8/16ch) / 66 dB (24ch); distortion 1% max
  @ 1 kHz; wow/flutter 0.04–0.06%. Plug-in module architecture:
  microprocessor unit / tape-deck electronics / reproduce preamps mounted
  under the headblock / record amps.
- Tascam 244 Portastudio 4-track cassette — record/play amplifier PCB
  schematic: `/Users/nathanielsantiaji/Downloads/tascam_244_portastudio_schematics.pdf_2-1757079384.png`
  (the `_2` page). Extracted architecture: playback preamp `TA7136AP`,
  dbx encode/decode on `NJM4558D` (4558 dual op-amp) per channel, master
  bias oscillator, 4 channels (CH1–4).

## How schematic PDFs actually get read

`WebFetch` cannot extract text from scanned/vector PDFs (always returns a
"this is binary data" apology), but it **always saves the raw file locally**
regardless (`tool-results/webfetch-<id>.<ext>`). The `Read` tool **can**
render PDF pages as real viewable images (`pages` param for multi-page
PDFs). For oversized images (>2000×2000px), `sips -Z 1900 <in> --out
<out.png>` (macOS built-in) resizes first. When exact verbatim numbers/code
matter more than a visual read (e.g. cross-checking a WDF scattering
matrix), `curl` a raw source file directly instead of `WebFetch` — WebFetch
always summarizes through a small model, which loses exact fidelity.

**Non-vision model caveat (2026-08-20):** when the running model can't view
images, `Read` returns `[Unsupported Image]` for raster/PDF pages, so scans
can't be visually inspected and must be OCR'd instead. Working method:
`pdftoppm -r 150 -png` to rasterize PDF pages, then a compiled Swift +
Vision-framework tool (`VNRecognizeTextRequest`, `.accurate`, no language
correction) — `swiftc -O ocr.swift -o ocr`, run `ocr <image>` to get each
line with its normalized bounding box (keeps schematic labels
position-associated). Spec tables and block-diagram text OCR cleanly; tiny
schematic component values OCR noisily, so cross-check those against a
second source when exactness matters.

## Working preferences

### Current local asset/cab state (2026-08-21)

- The local folders are the source of truth; do not pull before working.
- Threadline's current Cab library is the 22-file set under
  `Resources/CabIR`: Deluxe/King/Modern/Rect/Rock/Tweed/Vox microphone
  variants (57/121/421) plus `ROCK.wav`.
- Cab A and B are independent convolution paths. On stereo buses A feeds
  left and B feeds right; on mono buses they blend in parallel.
- Cab IR popup choices retain their original parameter indices but are shown
  in Deluxe/King/Modern/Rect/Rock/Tweed/Vox folders, with unmatched captures
  under Other. Pedal title-click menus anchor to the title label, so they open
  immediately below the title rather than below the enclosure.
- New enclosure, knob, and LED art lives in the uppercase asset subfolders.
  `redface.png` is the Redface tile art and `channeleq_enclosure.png` is the
  graphic-EQ tile art. The supplied effect-specific knob images are wired
  into their matching tiles; Parallel's single Blend control changes between
  its A and B knob artwork across the midpoint.

- **After every code change**: build all 3 formats (Standalone/VST3/AU),
  relaunch Standalone to confirm it's stable, commit with a detailed
  message explaining the change and why, push to GitHub. No exceptions,
  every single time — this is the standing workflow, not a one-off ask.
- **Verify DSP changes numerically before shipping when possible** — a
  quick standalone (non-JUCE) harness sweeping the affected code path for
  NaN/Inf/blow-up, same discipline the existing codebase already uses for
  its own WDF clippers and tube models. Can't listen to audio, so this is
  the substitute for an ear-check, not a nice-to-have.
- **Real schematics over memory/folklore** before making a DSP-accuracy
  claim or change — fetch and actually read the source first.
- **Stompbox pedals stay "archetype only"** — Fangs/Bison/Growl (and by
  the same logic, presumably any future pedal named after itself rather
  than a real product) absorb real schematics' *topology* lessons without
  copying exact component/BOM values, an explicit, deliberate policy
  (contrast: amp voices — Vox AC30, JTM45, Mark I, JC-120, Deluxe 63 — ARE
  named after and built from exact real schematic values; that
  inconsistency between amps and stompboxes was raised once and the user
  chose to keep it as-is rather than reconcile it).
- **Large/ambiguous-scope requests**: ask how to sequence rather than
  assume — offered "tractable items first, defer the biggest" for the
  pedal-accuracy list and the user picked that.
- **Don't over-build**: SpaceEcho's embedded reverb was added, then
  removed at the user's own prompt once they realized it duplicated the
  standalone Spring pedal at lower quality — favor one clean way to do a
  thing over a redundant convenience copy.
- **Be honest about uncertainty/negative results** — e.g. the amp CPU
  optimization pass reported plainly that the first micro-opt fixes
  (removing ~40% of the transcendental calls) were noise-level (~1.8%)
  rather than overselling them, then profiled and found the real cost was
  the Newton iteration loops — cutting those 4/5/6→2/3/4 landed ~36% while
  keeping 4× oversampling and identical sound.
- **Never remove a shipped APVTS parameter** — deprecate in place
  (unregister from UI/processing, leave it registered) so old saved
  sessions/presets never break. Established pattern (`odOrder`, the
  `*SectionOn` params, `spaceEchoReverb`).
- Casual, low-punctuation communication style; prefers being offered a
  short multiple-choice question at a real decision fork rather than a
  wall of options to read through.
- **Expandable bars open downward only.** No dedicated "expandable bar"
  component actually exists in this codebase (confirmed 2026-08-20, full
  grep of `Source/`) — the real mechanism behind every dropdown/picker is
  `juce::PopupMenu::Options`, and only the preset dropdown
  (`showPresetMenu`) had already forced `.withPreferredPopupDirection
  (downwards)`. Applied the same option to the other two popup-based
  pickers (`PedalboardComponent::showAddMenu`, `ParallelTile::showSlotMenu`)
  so the rule holds everywhere, not just the preset bar. Keep applying it to
  any future `juce::PopupMenu::Options` call in this codebase.
