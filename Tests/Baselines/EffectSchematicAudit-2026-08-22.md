# Threadline effect schematic audit — 2026-08-22

Primary source: [EL34World Effects schematic archive](https://el34world.com/charts/Schematics/files/Effects/Effects_Schematics.htm).
Every PDF named below was downloaded and visually inspected. A match means the
signal topology is applicable; it does not mean an original Threadline effect
has been renamed as a clone.

| Threadline block | Applicable source | Result |
| --- | --- | --- |
| Noise Gate | Boss NF-1 | The archive confirms a fast-open FET gate with a long 2M Decay path. Threadline retains its more stable three-band/hysteretic detector and now extends closing decay to 1.5s at maximum instead of chopping every tail at 180ms. |
| Comp | DOD 280A, EHX Soul Preacher, MXR Dyna Comp | DOD validates an LED/LDR feedback element; Soul Preacher and Dyna Comp show alternative FET/OTA detector paths. Threadline's explicitly optical design keeps its vactrol memory rather than incorrectly mixing the three circuits. |
| Dynamix | None | Original upward/downward program-relative dynamics processor. No schematic was forced onto it. |
| Bull | None in this archive | Existing traced Klon WDF remains sourced from ChowCentaur/BYOD. |
| Breaker | Ibanez TS9 | The production WDF already contains the source's 10k input, 51k+500k Drive feedback, 51pF compensation, 4.7k/47nF mid-hump leg and 1N4148 feedback pair. Its simplified post Tone stage remains the principal fidelity gap. |
| Fangs | ProCo RAT, MXR Distortion+ | Corrected from a generic frequency-independent gain approximation to the RAT's two feedback legs (560R/4.7uF and 47R/2.2uF), followed by a separate 1k/silicon shunt clipper. The Filter endpoints now come from the 100k + 1.5k / 3.3nF network. Fangs retains its own deep Gain taper and output calibration. |
| Bison | EHX Big Muff Pi | Existing two cascaded diode-in-feedback stages and low/high passive tone blend match the source topology. A transistor-level gain/bias solve would be a larger model replacement, not a safe component correction. |
| Growl | Hendrix Fuzz Face | The source confirms two directly coupled transistors with collector-to-base feedback and a shared emitter/Fuzz network. Threadline models nonlinear input loading and the first transistor, but its second-stage WDF diode stand-in is still the largest stompbox fidelity gap. |
| Tape | None | Studer A800 and Tascam 244 sources remain authoritative. |
| Tremolo | Boss TR-2 | TR-2 confirms buffered photo-FET amplitude control and Rate/Wave/Depth shaping. Threadline intentionally provides bias and harmonic tremolo voices, so replacing them with TR-2 would remove rather than upgrade functionality. |
| July | Boss CE-1, Boss CE-2 | The source confirms BBD delay, clock LFO, band-limiting and companding. Threadline already has a band-limited, compressed/expanded BBD path while retaining its Lag, waveform and D-C-V controls. |
| Ensemble | Boss CE-1/CE-2 family | Existing multi-tap inspired chorus is intentionally not presented as a circuit clone. |
| Dimension | Boss DC-2 | Existing dual antiphase BBD lines, pre/de-emphasis, reconstruction filters and NE570 companders match the source architecture. |
| JC Chorus | None in Effects archive | The Roland JC-120 service schematic remains authoritative. |
| Copier | Boss DM-2, EHX Deluxe Memory Man | Added the missing NE570-style 2:1 compressor/1:2 expander around the stored BBD signal. The existing write/repeat low-pass and bounded feedback remain. |
| Plexer | None directly applicable | Existing EP-3-derived tape echo remains separate from BBD delay schematics. |
| Satellite-201 | None in Effects archive | Roland RE-201 service manual remains authoritative. |
| Reverb | Boss RV-3 is not applicable | Room/Hall/Plate are algorithmic acoustic spaces, not the RV-3 digital implementation. No circuit was forced onto them. |
| Spring | Danelectro 9100 | Existing 9100 IR plus driven input and recovery coloration covers the source's tube-driver/tank/recovery architecture. |
| Redface | None | Neve 1073 drawings remain authoritative. |
| Desk | None | Original soft-clip utility. |
| 9-band EQ | Boss GE-7 and Ibanez GE9 are seven-band | Both inspected archive drawings have seven bands, so neither was incorrectly used to replace Threadline's nine-band ISO control surface. |
| Amp, Cab, Parallel, Input, Output | Not stomp effects | Their own amp/IR/routing specifications remain authoritative. |

## Remaining high-risk fidelity work

1. Replace Growl's second-stage diode stand-in with a stable, jointly coupled
   two-transistor solve including the 68k collector feedback and Fuzz emitter
   network. This needs DI listening and CPU measurement, not a blind swap.
2. Replace Bison's ideal gain elements with biased transistor stages while
   preserving its current useful Sustain range and output level.
3. Model Breaker's complete active Tone/output network and verify TS9/TS808
   differences without the undocumented pre-clip voicing shelves.

These are deliberately recorded rather than claimed complete: all three alter
the nonlinear transfer substantially and require guitar-DI regression files in
addition to sine stability tests.
