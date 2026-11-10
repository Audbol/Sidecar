# New consoles: public reference material

Researched 9 November 2026. Facts only, in our own words. Nothing from firmware, decompiled code or
leaked material, and no editor or console software was fetched. Companion to `CONSOLE_RESEARCH.md`, whose
reliability labels are reused here. The documents themselves are outside the repository, in
`Sidecar-reference\`, one folder per console family, each with a `SOURCES.md` (file, page, URL, terms).

| Label | Meaning |
| --- | --- |
| **[Maker]** | The maker's own document, read directly |
| **[Maker-x]** | A maker's document seen only as a search-engine excerpt (page not opened) |
| **[Review]** | Magazine review: good for what the reviewer saw, weak on internals |
| **[Photo]** | What can be seen in a photo, by our own look at it |
| **[Forum-x]** | Forum post or similar seen only as an excerpt. Weakest |

"Sibling" means the fact comes from a sister product and needs care.

**Not read:** several sites refused automated reading and were left alone (usa.yamaha.com pages and
pl.yamaha.com; support.allen-heath.com, as already noted in `CONSOLE_RESEARCH.md`). Yamaha's PDFs on
data.yamaha.com and Midas's PDFs on its CDN served normally.

**Block diagrams:** this machine has text extraction for PDFs but no way to render a page to an image.
Yamaha's diagrams are vector drawings, so their labels could be read as text, but the wiring between
labels could not be followed. Midas's are images with almost no text layer. Where a signal order is given
below, it comes from the manual's prose or spec tables unless it says otherwise.

Page numbers are the document's printed page unless marked PDF.

---

## 1. Yamaha DM2000 (with DM2000VCM/V2; DM1000 and 02R96 siblings noted)

**Documents (`yamaha-dm2000\pdf\`):** `dm2000v2_en_om_g0.pdf` (V2 Owner's Manual, 403 PDF pages: the main
source, called OM-V2 below), `DM2000E.pdf` (original manual), `dm2000_v22_supplementary.pdf`,
`dm2000_quick_guide_en.pdf`, `StudioManagerE.pdf` and `DM2000E4.pdf` (Studio Manager manuals only),
`brochure_dm_0_vcm_series.pdf`; `dm2000v2_midi_specification\` (Yamaha's "DM2000 V2 MIDI Specification": the
parameter change list with element, parameter and channel numbers, the PRM and CH tables, Key Remote, Remote Meter,
meter data, MMC; retrieved 9 November 2026, called SPEC below).

### Structure and capacity

| Item | Fact | Source |
| --- | --- | --- |
| Inputs | 96 Input Channels, each with a Direct Out. Gates on all 96, compressors on all 96 | OM-V2 pp. 17-18 [Maker] |
| Outputs | 8 Bus Outs (routable to Stereo for subgrouping), 12 Aux Sends, 4 stereo Matrix Sends (22 x 8 matrix), Stereo Out | OM-V2 p. 17 [Maker] |
| Physical I/O | 24 mic/line (XLR and TRS, +48 V, 26 dB pad) with 24 analog inserts; 6 mini-YGDAI slots (48 in / 48 out); 8 Omni outs; 2 AES + 1 coax 2-track in and out; 2 analog 2-track in | OM-V2 p. 17 [Maker] |
| Cascade | Up to four DM2000s, 384 channels | OM-V2 p. 17 [Maker] |
| Rates | 44.1, 48, 88.2, 96 kHz. All 96 channels stay available at 96 kHz | OM-V2 p. 341 [Maker]; [Review] (Sound On Sound, Nov 2002) |
| Word length | 32-bit processing, 58-bit accumulator; 24-bit 128x oversampling converters | OM-V2 p. 17 [Maker] |
| Latency | Under 2.3 ms at 48 kHz, under 1.2 ms at 96 kHz (input to Stereo Out) | OM-V2 p. 341 [Maker] |
| Dynamic range | 108 dB typ. AD+DA to Stereo Out at 48 kHz, 106 dB at 96 kHz; 110 dB DA | OM-V2 p. 341 [Maker] |
| Level diagram | +4 dBu nominal, max +24 dBu, 0 dBFS relates to +24 dBu (labels only) | OM-V2 Level Diagram, PDF p. 400 [Maker] |
| Fader groups | 8 input + 4 output Fader groups, same for Mute groups; 4 + 4 EQ groups, 4 + 4 Compressor groups | OM-V2 p. 18 [Maker] |

Siblings: Sound On Sound says the DM2000 and 02R96 share the same DSP technology and architecture, differing
mainly in I/O count and control layout (DM2000 96 channels and 24 mic inputs vs 56 and 16) [Review]. The DM1000
has 48 channels, 8 auxes and 4 effects processors [Review].

### Channel processing, order and ranges

- **Order on an Input Channel.** The block diagram labels run Gate, attenuator, 4-band EQ, delay, then the
  on/level stage and pan (OM-V2 PDF p. 399, labels only, not the wiring) [Maker]. The attenuator (-96 to +12 dB,
  0.1 dB steps) is pre-EQ (OM-V2 p. 130, p. 342) [Maker]. The compressor and the insert can each be set pre-EQ,
  pre-fader or post-fader, and COMP ORDER chooses which comes first if both share a position (OM-V2 p. 136) [Maker].
- **EQ:** 4 bands, Q 0.1 to 10.0 (41 points), frequency 21.2 Hz to 20 kHz in 1/12-octave steps, gain +/-18 dB
  in 0.1 dB steps. Low band alternatively low shelf or HPF, high band high shelf or LPF. Type I (legacy Yamaha
  curve) or Type II (new algorithm), selectable per channel (OM-V2 p. 134, p. 345) [Maker]. Sound On Sound calls
  the lack of dedicated HPF/LPF "shortsighted" and reports a dual-notch artefact when bands are stacked at the same
  frequency with full cut [Review].
- **Gate:** threshold -54 to 0 dB (0.1), range -70 to 0 dB (1), attack 0 to 120 ms (1), hold 0.02 ms to 1.96 s
  (216 points at 48 kHz), decay 5 ms to 42.3 s (160 points at 48 kHz). Key in: 12-channel groups or Aux 1-12.
  Times scale with rate; the 44.1, 88.2 and 96 kHz tables are on the same page. Ducking has the same ranges
  (OM-V2 pp. 342, 345) [Maker].
- **Compressor:** threshold -54 to 0 dB, ratio 1, 1.1, 1.3, 1.5, 1.7, 2, 2.5, 3, 3.5, 4, 5, 6, 8, 10, 20, infinity
  (16 points), output gain 0 to +18 dB, knee Hard or 1 to 5 (soft, 6 steps), attack 0 to 120 ms, release 5 ms to 42.3 s
  at 48 kHz. Expander uses the same ranges. Compander-Hard and Compander-Soft types exist, adding a Width parameter
  (OM-V2 p. 345 and comp preset table p. 137) [Maker]. Sound On Sound notes compressor and expander share one set of
  controls, which makes setup fiddly [Review].
- **Delay:** 0 to 43,400 samples on every Input and Output Channel (OM-V2 pp. 342-344) [Maker].
- **Sends and pan:** Aux sends pre or post fader; Pan 127 positions; surround pan 127 x 127 positions; LFE level
  infinity, -96 to +10 dB in 256 steps; Matrix sends level infinity, -96 to +10 dB with 127-position pan; Bus to Stereo
  level infinity, -130 to 0 dB (OM-V2 pp. 342-344) [Maker]. The spec table's text extraction shifts values against
  labels, so these were read by re-aligning rows; check them against the page. Aux send level range not extracted.
- **Oscillator:** sine at 100 Hz, 1 kHz, 10 kHz; pink noise; burst noise (OM-V2 p. 343) [Maker].
- **Surround:** 3-1, 5.1 and 6.1 modes, joystick, bass management, downmix (OM-V2 p. 19) [Maker].

### Faders and metering

- 25 touch-sensitive 100 mm motorised faders (24 + Stereo), 10-bit fader data: input faders +10 to -138 dB and
  infinity, master fader 0 to -138 dB and infinity (OM-V2 p. 341) [Maker]. The taper: SPEC's PRM TABLE #05-2 (channels; 0 dB on value
  823) and #15-2 (outputs) list all 1024 values, and are straight lines between (1, -138), (15, -96), (33, -78), (223, -40), (423, -20), (1023,
  +10) and (1, -138), (3, -134), (35, -102), (83, -78), (223, -50), (423, -30), (1023, 0) [Maker].
- 24 encoders for Pan, Aux/Matrix sends or assigned parameters; layers: 1-24, 25-48, 49-72, 73-96, Master, 4 Remote
  (OM-V2 p. 19) [Maker]; encoder has four assignable modes, faders in three blocks of eight [Review][Photo].
- Metering points pre-EQ, pre-fader, post-fader, independently for inputs and outputs. Peak Hold on all meters.
  24- and 48-channel meter pages, and from V2 the meter pages show Gate and Compressor gain reduction (OM-V2
  pp. 127-129) [Maker]. LCD 320 x 240 with fluorescent backlight (OM-V2 p. 19) [Maker].
- MB2000 optional peak meter bridge: per-channel bar meters plus Stereo L/R. Stereo scale marks, read from the photo:
  OVER, 0, -2, -4, -6, -8, -10, -12, -14, -18, -24, -30, -36, -42, -48, -56, -72 [Photo]. It selects Input 1-24, 25-48,
  49-72, 73-96 or the outputs, also has 1-48 and 49-96 buttons, its own metering-position buttons, peak hold, and a timecode display
  (OM-V2 p. 387; `Mikser_cyfrowy_DM2000V2k_firmy_Yamaha.jpg`) [Maker] [Photo].

### GEQ and effects

- Six 31-band GEQs patchable to Output Channels; band range selectable +/-15, 12, 6 or 24 dB; 128 user memories
  (OM-V2 pp. 18, 184, 344) [Maker].
- 8 internal effects processors; #1 and #2 can run the multichannel types, 8 in / 8 out. REVERB 5.1 and similar use
  four DSPs and reduce the total by three (OM-V2 pp. 175-176) [Maker].
- Preset types (OM-V2 pp. 174-176, parameters pp. 310-328) [Maker]: Reverb Hall/Room/Stage/Plate, Early Ref., Gate Reverb,
  Reverse Gate, Mono/Stereo/Mod Delay, Delay LCR, Echo, Chorus, Flange, Symphonic, Phaser (16-stage), Auto Pan, Tremolo,
  HQ.Pitch, Dual Pitch, Rotary, Ring Mod., Mod.Filter, Distortion, Amp Simulate, Dyna.Filter, Dyna.Flange, Dyna.Phaser,
  Rev+/->Chorus, Rev+/->Flange, Rev+/->Sympho., Rev->Pan, Delay+/->ER., Delay+/->Rev, Dist->Delay, Multi.Filter, Freeze,
  Stereo Reverb, Reverb 5.1, Octa Reverb (7.1), Auto Pan 5.1, Chorus 5.1, Flange 5.1, Sympho. 5.1, M.Band Dyna., Comp 5.1,
  Compand 5.1. Numbers 53 to 61 are reserved for Add-On Effects in V2 (Comp276/S, Comp260/S, Equalizer601, OpenDeck, REV-X
  Hall/Room/Plate); V2.2 extends this to 67 (Room ER, Auto Doppler, Field Rotation, Max100, Vintage Phaser, Dual Phaser)
  (`dm2000_v22_supplementary.pdf`) [Maker].
- **DM2000VCM vs V2:** a VCM console is a V2 console with the Add-On Effect packs (Channel Strip AE-011, Master Strip
  AE-021, Reverb AE-031, Surround Post AE-041, Vintage Stomp AE-051) (`brochure_dm_0_vcm_series.pdf`) [Maker]. The VCM
  models are proprietary circuit models and are not described in the public documents.
- Libraries: EQ 40 + 160, Gate 4 + 124, Comp 36 + 92, Effects 61 + 67, GEQ 128, Channel 127, Automix 16, Surround Monitor 32
  (OM-V2 pp. 18-19) [Maker]. A Waves Y56K effects plug-in card is an option (OM-V2 p. 18) [Maker].

### Scenes and automation

- 99 Scenes plus #0 (read-only initial data) and #U (undo/redo of recall and store). Stored: Input and Output Channel settings,
  Effects, GEQ, Group and Pair settings, Fade Time, title. Edit Buffer retained at power-off; Edit indicator shows
  divergence. Auto Update stores edits in a Shadow memory (OM-V2 pp. 185-187) [Maker].
- Fade Time per channel, 0 to 30 s in 0.1 s steps, optionally applied to all Scenes. Recall Safe per channel and parameter. Global
  Paste copies parts of the current scene into others (OM-V2 pp. 8, 189-191) [Maker].
- Automix: dynamic, 1/4-frame accuracy, 16 memories, Scene recall inside Automix, fader Touch Sense punch (OM-V2 p. 18) [Maker].

### MIDI and remote

- **MIDI ports:** MIDI, USB TO HOST (multiport) and SERIAL TO HOST (OM-V2 p. 19) [Maker].
- **Channel messages (OM-V2 p. 369, MIDI Implementation Chart PDF p. 401)** [Maker]: Note On/Off control the internal effects;
  Control Change 0-95 and 102-119 are assignable via a table or NRPN; Program Change 0-127 selects Scenes via a 99-entry table
  (p. 352). Initial CC assignments are published in full (pp. 353-368).
- **NRPN mode:** when selected, parameters use CC 62h/63h for the number and 06h/26h for data (p. 370) [Maker].
- **SysEx (Appendix C, pp. 369-385)** [Maker]. Up to 4096 bytes. Device number equals MIDI channel.
  - Bulk dump `F0 43 0n 7E ... F7`, request `F0 43 2n 7E ... F7`. Data types in the printed order: m Scene, S Setup, L User layer,
    I User plug-in, V User keys, U Assignable layer, C CC table, P Program table, Q EQ lib, Y Comp lib, G Gate lib, E Effect lib, F GEQ lib,
    H Channel lib, R Input patch lib, O Output patch lib, J Bus-to-Stereo lib, K Surround Monitor lib, a Automix, N Plug-in card (scene and
    Automix are compressed).
  - Parameter change `F0 43 1n 3E 06 tt ee pp cc dd.. F7`, request `F0 43 3n 3E 06 ...`, and a Universal variant with model ID 7F. The 3E is the
    digital-mixer group and 06 the DM2000 model ID.
  - Type byte tt: 01 Edit Buffer, 02 Patch, 03 Setup, 04 Backup, 10 recall/store/title/clear, 11 pair, 12 effect, 13 sort, 14 attribute/link, 20 Key
    remote, 21 Remote meter, 22 Remote time counter, 23 Automix status. Element and channel are the next bytes; element 0 extends to two bytes.
  - **The element, parameter and channel numbers:** not in the manual (13.4.5 says to consult your dealer) but in SPEC's Parameter Change List:
    every row gives the element and parameter numbers, range, initial value and full `F0 43 1n 3E ...` string [Maker]. Data is four 7-bit bytes of a
    28-bit two's complement value. Edit-buffer elements use model `7F` type `01`, the patches, setup and backup model `06` types `02`-`04`. The
    element byte in the edit-buffer strings is one more than the list's Element number; the other types count from 01 again.
  - DM1000 sibling: model ID 0C (`F0 43 1n 3E 0C`), same format (manual excerpt on ManualsLib) [Maker-x].
- **MMC, MTC, MIDI Clock, Song Position** for Automix sync (p. 369) [Maker].
- **DAW layers:** Pro Tools Remote Layer emulates three 8-fader HUI-type controllers on three virtual MIDI ports over USB [Maker-x] (Apple's Logic Pro
  control surface guide, not a Yamaha document) and OM-V2 Chapter 19 (p. 221) [Maker]. General DAW, Nuendo, Cubase SX and Yamaha's Advanced DAW protocol
  layers exist; user-defined remote layers can send arbitrary MIDI (OM-V2 pp. 11, 253, 306) [Maker]. **Studio Manager's wire protocol was not found in the documents searched.**
- Machine control MMC and Sony P2, GPI, and AD8HR/AD824 head-amp remote (OM-V2 p. 19) [Maker]. SmartMedia card storage (p. 19) [Maker].

### Panels

Control surface layout is drawn in OM-V2's panel chapter; the owner has a three-quarter view with the meter bridge
(`photos-web\Mikser_cyfrowy_...jpg`, CC BY-SA 4.0) and an in-use FOH shot (CC BY 2.0). Sections visible [Photo]: top row of
24 encoders and name displays, left routing and display-access block, the central LCD, the Selected Channel strip (blue section:
Gate/Dynamics/EQ/Pan), Layer and Scene sections, Automix and Locator on the right, 24+1 faders in three blocks of eight, jog wheel.

### What an emulation still needs (DM2000)

1. ~~Element and parameter address map~~ and ~~the fader taper (10-bit position to dB)~~: answered by SPEC (`livedaw-dm2000`, `FaderLaw::DM2000`). Bulk dump bodies
   (compressed scene data) are still not published, nor are the NRPN numbers of NRPN mode, nor the effect parameter tables behind `kEffectParam1..32` ("see effect specifications").
2. The exact EQ responses (Type I vs II, shelf and HPF shapes). Would need measurements or a unit.
3. Studio Manager protocol and display layouts (LCD pages only appear as small drawings in the manual).
4. Straight-on, lit top-panel photo and section close-ups. Not obtained.
5. The internals of VCM Add-On models. Proprietary, not in the public documents.

---

## 2. Midas PRO Series (PRO1, PRO2, PRO2C, PRO3, PRO6, PRO9, PRO X)

**Documents (`midas-pro\pdf\`):** `PRO_X_M_EN.pdf` (PRO X User Manual, the richest source), `PRO2-PRO2C-Operators-Manual_OM_EN.pdf`,
`PRO1-Operators-Manual_OM_EN.pdf`, `PRO3_CC_IP_P0ART_MANUAL_EN.pdf` (Owner's Manual for PRO3/6/9), `PRO6-Control-Centre-Manual_CCM_EN.pdf`, quick
start guides. All Midas product pages mark these consoles as legacy (no longer made) [Maker]. **No firmware, offline editor or MIXTENDER app was fetched.**

### Family capacity

| | PRO1 | PRO2 / PRO2C | PRO3 | PRO6 | PRO9 | PRO X |
| --- | --- | --- | --- | --- | --- | --- |
| Input channels | 40 + 8 aux in = 48 | 56 + 8 aux in = 64 | 48 + 8 = 56 | 56 + 8 = 64 | 80 + 8 = 88 | 144 + 24 = 168 |
| Mix buses (aux+matrix+3 master) | 16+8+3 = 27 | 16+8+3 = 27 | 16+8+3 = 27 | 16+16+3 = 35 | 16+16+3 = 35 (see note) | 72+24+3 = 99 |
| Max simultaneous mix channels | | | 72 | 80 | 104 | 240 |
| VCA / POP groups | 8 / 6 | 8 / 6 | 10 / 6 | 10 / 6 | 10 / 6 | 10 / 8 |
| FX devices | 6 | 6 | 6 | 8 | 8 | 24 (see note) |
| Max KT GEQs | 28 | 28 | 28 | 36 | 36 | 36 |
| Motor faders | 18 | PRO2 28 / PRO2C 20 | 29 | 29 | 29 | 29 |
| Screens | one 15-inch | one 15-inch | two 15-inch | two 15-inch | two 15-inch | two 15-inch |

Sources: PRO X manual pp. 20-23 (Table 1, PRO3/6/9/X) [Maker]; PRO2 manual Chapter 2 (PDF pp. 24-30), PRO1 manual Chapter 2 [Maker]; product pages
for faders, screens, VCA/POP counts [Maker]. Note: Table 1 prints "6" auxes for PRO9, but its total of 32 monitor mixes only adds up with 16, so 6 reads as
a typo. All run at 96 kHz (product pages [Maker]; PRO X manual p. 217 [Maker]). 40-bit floating point on PRO2 and PRO9 product pages [Maker].

**PRO X effects count conflict:** manual p. 19 says up to eight stereo effects units, p. 20 lists 24 devices, p. 23 says up to 24 processors, p. 95 calls the
effects screen an eight-unit virtual rack, spec p. 219 says 8 available, and the product page says both 24 and 40 [Maker]. Treat the user-visible rack as eight
slots; confirm the rest before building. GEQs consume effects DSP, so effects and GEQ counts trade off, set in Preferences (Effects and GEQs drop-down) (pp. 23, 145) [Maker].

### Signal path and buses

Each input (up to 144 on PRO X) has analogue and digital gain, phase, delay, swept HPF, swept LPF, a compressor with four styles, a gate
with external side chain, an insert, four-band parametric EQ, routing via level controls to the mix buses, pan to left/right masters, mono master, a SISTM
pan option, and a direct output (PRO X manual p. 23) [Maker]. Auxes have subgroup, aux or mix-minus modes, dual-mono or stereo-pair modes, six-band PEQ or
31-band GEQ, a five-style compressor, insert (p. 23) [Maker]. Aux inputs work as effects returns or as input channels (no dynamics) (p. 22) [Maker]. Buses are
time-aligned; alignment is defeatable for minimum latency (p. 22) [Maker]. Six solo buses on PRO X (two on PRO1/PRO2) [Maker]. Surround: Quad, LCRS, 5.1 via matrices 1-6, plus SISTM three-channel
pan (p. 24) [Maker]. Automatic delay compensation and a latency table (Chapter 28) [Maker]; under 2 ms input to master with no compensation (p. 210) [Maker]. Functional block diagrams
appear in PRO2 manual Appendix B (printed pp. 295-304: mono input, returns, master, mono mix, monitor, comms and dynamics signal flow) [Maker]; images, not readable here.

### Processing ranges (PRO X manual pp. 205-208, 219)

- **Input HPF** 10-400 Hz, 12 or 24 dB/oct; **LPF** 2-20 kHz, 6 or 12 dB/oct [Maker].
- **Input EQ** four bands: Treble 1-25 kHz, Hi-Mid 320 Hz-8 kHz, Lo-Mid 80 Hz-2 kHz, Bass 16-400 Hz, all +/-16 dB, width 0.1-3 octaves. Treble shelf modes Soft, Classic, Bright; Bass shelf
  modes Warm, Classic, Deep; classic is modelled on the XL4 and has some undershoot, Bright and Deep have none (pp. 207, 219) [Maker]. The EQ is described as phase-shifting and fully interpolated (p. 19) [Maker].
- **Output EQ** six bands, 16 Hz-25 kHz, +/-16 dB, width 0.1-3 octaves, all parametric; band 1 also high pass (6 or 12 dB/oct) or soft shelf, band 2 also high pass (24 dB/oct), band 6 also low pass (6 or 12 dB/oct) or soft shelf (pp. 208, 219) [Maker]. Alternatively a 31-band 1/3-octave GEQ, proportional Q, with 2-20 kHz LPF and 20-500 Hz HPF (p. 219), 12 dB range per band (Appendix C) [Maker]. The PRO X GEQ has 31 bands against the DN370's 30, with an extra 20 Hz band (p. 221) [Maker]. Spec p. 219 says 32 effect-channel GEQs and 16 in place of PEQ; other pages say 36 in total, so the exact pool needs checking.
- **Compressor** Input threshold -50 to +20 dBu, output -50 to +25 dBu; attack 200 microseconds to 20 ms; release 50 ms to 3 s; ratio 25:1 to 1:1; knee 4, 12 or 40 dB; gain 0 to +24 dB; side chain filter 50 Hz-15 kHz, bandwidth 1/3, 1 or 2 octaves. Soft clip after make-up gain [Maker].
  - Envelope modes (p. 206): Corrective (exponential peak, fast), Adaptive (exponential RMS), Creative (linear peak, slow, linear attack, second-order release), Vintage (adaptive peak), Shimmer (overshoot peak, outputs only) [Maker]. Described in words only.
- **Gate** peak mode; threshold -50 to +20 dBu; attack 20 microseconds to 20 ms; hold 5 ms to 2 s; release 5 ms to 2 s; range infinity to 0 dB; side chain filter as compressor (p. 219) [Maker].
- **Effects** (pp. 95-117) [Maker]: Ambience, Chamber, Hall, Plate, Vintage Room reverbs, Virtual DN780 (emulation of the Klark Teknik unit, p. 223), Stereo Chorus, Flanger, Phaser, Variable Phase, Delay, Dual Stereo Delay, Pitch Shifter, Tape Saturation, Submonster, Dynamic EQ, 3-Band Compressor, SQ1 Dynamics, Stereo Graphic EQ, Midas Spectrum Analyser, DN60 Spectrum Analyser. Parameter details are on those pages.
- **Analogue:** input gain -25 to +60 dB; 24-bit, 128x oversampling converters (p. 217-218) [Maker].

### Faders, metering, screens

- Faders marked to +10 dB; aux bus levels go only to +6 dB (p. 65) [Maker]. Law not published.
- Surface LED meters: 20 segments, +21 to -36 dB in 3 dB steps, 7-segment gain reduction; screen meters 105 x 20-segment, 112 x 7-segment GR (pp. 219-220) [Maker]. GUI meter attack 0-10 ms, delay 0-0.5 s, decay 10-25 ms, peak hold 0 to infinity (Preferences, p. 144) [Maker]. Screens 1024 x 768 [Maker].
- 59 LCD switches with 36 x 24 pixel displays (p. 220) [Maker]. Bays and zones described in Chapters 3, 6 and 7 [Maker]. Surface close-ups and screen renders in `midas-pro\photos-web\` [Photo].

### Scenes and automation (Chapters 20-23)

- Up to 1000 scenes, plus point scenes: 10 per scene and 10 more per point scene. Global scene events, event triggers, crossfades, Scope (what each scene recalls) and Safes (what is protected, per channel and global) (pp. 121-135, Appendices K-P pp. 253-393) [Maker]. Show Editor copies parameters through scenes (Appendix P) [Maker]. Appendices K-P are the most complete per-parameter lists in the public documents.

### MIDI and remote

- MIDI In, Out and Thru on the control centre rear and on I/O boxes (PRO2 manual PDF p. 38; PRO X pp. 131, 155-156) [Maker].
- **MIDI is event-based, not a parameter protocol.** Incoming or outgoing Program Change, Control Change, Aftertouch, Pressure, Note On/Off, Pitch Wheel can fire or be sent by scene events, on channel 1-16 or Any; the source can be the control centre's MIDI port or an I/O box ID. Conditions include Scene Exit, Recall and Entry; actions include Last, Next, Now, Jump, GPIO TX (PRO X pp. 131-135) [Maker].
- GPIO on the DL351/DL451 boxes (p. 131) [Maker]. KVM switch for external computers, external video, USB and Ethernet tunnelling through the network (pp. 24-25) [Maker].
- **Remote control server** for Midas's iPad app (MIXTENDER on PRO2C page): enable in Preferences, set IP or DHCP; 192.168.20.x is reserved for console internals (PRO2 manual p. 213) [Maker]. **No protocol is published in these documents.** A web search found no HiQnet, OSC, RS232 or API documentation for the PRO family either (absence of evidence only).
- A keyword search of the PRO X manual found no Mackie or HUI emulation [Maker].

### What an emulation still needs (PRO series)

1. Any control protocol (iPad server wire format, MIDI implementation beyond scene events). None public.
2. Exact shelf-mode responses and compressor envelope equations: only described in words. Would need measurement.
3. Fader law and detailed gain structure. The manuals give ranges, not a mapping.
4. Readable block diagrams: PRO2 Appendix B needs a person's eyes (images). PRO X manual refers to a "Functional Block Diagrams" appendix but its printed Appendix B is the Technical Specification; no diagram located for PRO X.
5. Which features exist on which model beyond the Table 1 and product-page numbers. PRO3/6/9 manual (Sep 2010) is older than PRO X's and should be compared chapter by chapter.

---

## 3. Lighter pass: other planned consoles

### Midas M32 (`midas-m32\`)

`M32_M_EN.pdf` User Manual, `M32_QSG_EN.pdf`, three renders. 40 input channels and 25 mix buses, 8 DCA, 6 mute groups, 8 stereo FX, AES50 96/96, 32 x 32 USB, 25 faders, 7-inch TFT, 192 kHz converters, 40-bit float DSP (product page 0603-ADX) [Maker]; the manual names 16 Mix Buses, Matrix 1-6 and Main C (manual section 1) [Maker]. MIDI: Program Change 1-100 on channel 1 loads scenes if Enable MIDI Scene Recall is set and the scene is valid; recall scope follows Safes; Remote mode HUI or MCU emulation over DIN MIDI, card MIDI or RTP MIDI (manual Appendix B and Setup/Remote) [Maker]. OSC is not documented in the manual; Sidecar's `UNIVERSAL_MIXER.md` already cites P.-G. Maillot's unofficial X32/M32 OSC guide. Closest sibling to the X32 work.

### Yamaha CL, QL, TF (`yamaha-cl-ql-tf\`)

- **CL5/CL3/CL1** (`cl5_3_1_en_om_f0.pdf`, `cl5_en_rm_c0.pdf`): 72 / 64 / 48 mono input channels; MIX 1-24, MATRIX 1-8; STEREO L/R and MONO buses; Centralogic fader banks; Premium Rack and GEQ Rack (OM model table; RM patch lists) [Maker]. Parameter Change SysEx and a MIDI Data Format chapter (RM pp. 157 and 264) [Maker]. Yamaha also publishes a "MIDI Parameter Change List (CL/QL)", seen only on a page that refuses automated reading [Maker-x].
- **QL5/QL1** (`ql5_1_en_om_d1.pdf`, `ql5_1_en_rm_c0.pdf`, `ql5_v4_block.pdf`): 64 / 32 mono inputs, 32 / 16 analog in, MIX 1-16, MATRIX 1-8 with 7-8 as CUE B, Dante 64 or 32 (OM; block diagram labels) [Maker].
- **TF5/TF3/TF1** (`TF-Series-Reference-Manual-fullcompass.pdf`, a dealer copy): AUX1-8, stereo AUX 9/10-19/20, SUB, groups, FX1, FX2, GEQ. No MIDI appears in the reference manual's text [Maker]; channel counts were not extracted.

### Yamaha DM7 (`yamaha-dm7\`)

`DM7_RreferenceManual_EN_E0.pdf`, `dm7_en_om_a0.pdf`. DM7 120 mono input channels, 32 analog in, 4-in/4-out AES, 24 + 4 channel strips, 28 faders; DM7 Compact 72, 16, 12 + 4, 16 faders; under 1.5 ms latency at 96 kHz (OM) [Maker]. 48 MIX + 12 MATRIX + 2 STEREO [Maker-x]. Reference Manual covers Scene List (Comment, Focus, Fade Time, GPI, Playback Link, Scene Link), MIDI/GPI screens (Program Change, Control Change, GPI, Fader Start), Theatre and Broadcast packages [Maker]. A separate data list or MIDI implementation document was not found.

### Allen & Heath Avantis (`allen-heath-avantis\`)

Only an older guide: `avantis-firmware-reference-guide-v1.20-musson.pdf` (firmware V1.20; V2 exists). 12 RackFX, 16 Dyn8 engines with dPack; two inserts per input (post-Gate, post-PEQ/Comp); Gate or Ducker, PEQ, GEQ (mixes), Compressor; Scene Manager and Global Safes; MIDI Strips with default CC/Note mapping and a separate MIDI Control Driver for HUI/Mackie [Maker]. TCP control port 51325 (TLS 51327), MIDI-format messages, Avantis firmware V2.0 and later [Maker-x]. 64 channels and 42 buses appear on a retail listing title only [Forum-x]. Refused or missing: the MIDI TCP/IP protocol article (403).

### DiGiCo SD and Quantum (`digico\`)

`SD-Quantum-Software-Reference-Issue-E-V1528.pdf` (Feb 2022, V1528+), `DiGiCo-Quantum-338-Data-Sheet-prggear.pdf`. Channel EQ 20 Hz-20 kHz, +/-18 dB with Q per band; Dynamics 1 is Compressor, Multi-band or De-esser; Dynamics 2 is Gate, Ducker or Compressor; Snapshots with Scope, Recall Times, Crossfades, MIDI Program Change, MIDI devices, GPO; External Control includes Generic OSC (Issue E index and channel chapters) [Maker]. Generic OSC: user-written messages over Ethernet, 8 rotaries and 8 switches per input, stored in snapshots [Maker] (DiGiCo press page). Quantum 338: 128 inputs, 64 Aux/Group busses, 24 x 24 Matrix, 36 Mustard strips, 64 Nodal, 38 faders, 3 x 17-inch touchscreens (datasheet, dealer copy) [Maker]. The newest manual is Issue H (June 2025, V20+) [Maker-x]; DiGiCo's full OSC address list was not found.

---

## Documents worth a look, not downloaded

| Document | Why not |
| --- | --- |
| Yamaha DM2000 service manual (lost-manuals.com, ManualsLib 1413017, ManualMachine): schematics, overall block diagram, LSI pin descriptions, service MIDI format | Third-party uploads of a manual restricted by its own notice to authorised Yamaha service personnel. Not public, so left alone. Owner's call |
| Yamaha "MIDI Parameter Change List (CL/QL)" and CL block diagram | Listed only on usa.yamaha.com pages that refuse automated reading. Worth fetching by hand |
| Newer manual revisions: CL V5.8 Reference Manual, DiGiCo Issue H, current Avantis Firmware Reference Guide, Avantis MIDI TCP/IP Protocol | Refused automated reading, or only seen in search excerpts |
| Sound On Sound DM2000 and 02R96 reviews | Read online; only low-resolution images, so nothing kept |
| DM1000 and 02R96 manuals | Sibling; useful to compare MIDI formats later |
| ManualsLib and similar mirrors of the manuals above | Unofficial and duplicates; the maker's own files are in the reference folders |
| Unofficial OSC notes (Stagehacks DiGiCo page, KLANG DiGiCo macro notes) | Community write-ups, not official; only worth reading when DiGiCo work starts |
