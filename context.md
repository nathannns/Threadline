# Threadline — working context

Not a technical doc (see `README.md` for that). This is reference links and
working preferences for continuing this project across sessions.

## Reference / source links

### Amps (schematics actually read this project)

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
- **Expandable bars open downward only.** The amp sim's expandable bar (and
  any future one) must expand downward like the other pedals, never upward
  — a stated UI rule to apply consistently.
