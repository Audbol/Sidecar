# Console research: what the public web adds

Researched 9 November 2026. Facts only, in our own words. Nothing from firmware, decompiled code or
leaked material. Companion to `DLIVE_RESEARCH.md` and `UNIVERSAL_MIXER.md`.

**Reliability labels**

| Label | Meaning |
| --- | --- |
| **[Maker]** | The maker's own document, read directly |
| **[Maker-x]** | The maker's document, but seen only as a search-engine excerpt (page not opened) |
| **[Review]** | Magazine review: good for what the reviewer saw, weak on internals |
| **[Photo]** | What can be seen in a photo, by our own look at it |
| **[Measured]** | Someone's stated measurement, with a method |
| **[Forum]** | Forum post, read directly. Hearsay |
| **[Forum-x]** | Forum post seen only as a search-engine excerpt. Weakest |

"Sibling" means the fact comes from a sister product and needs care.

Several sites refused automated reading (support.allen-heath.com, allen-heath.com pages,
manuals.plus, behringer.world, Reverb, vintagedigital.com.au, rapmag.com, blue-room.org.uk). Those
pages were left alone, so anything from them is excerpt-only or missing, and marked as such.

---

## 1. Yamaha 02R (original and Version 2)

**Sources**

- **SM**: Yamaha 02R Service Manual, PA 011295, printed December 1995, 205 pages.
  https://www.lost-manuals.com/en/manufacturer/yamaha/02r (saved, see end). Page numbers are as
  printed. It predates Version 2 (it says 64 scenes; Version 2 has 96). Read so far: cover, pp.2, 3,
  6, 18, 115, 117, 120 to 122. Not read: inspections (108 to 114), LSI pins (50 on), MIDI format (148).
- **SOS96**: Sound On Sound review, 1996, three pages. https://www.soundonsound.com/reviews/yamaha-02r
  (also `?page=2`, `?page=3`). Its photos are named `1996-02` (saved, see end).
- **SOS95**: Sound On Sound preview, August 1995. https://www.soundonsound.com/reviews/yamaha-02r-0
- **SOSF**: Sound On Sound forum thread on the 02R (2023).
  https://forum.soundonsound.com/phpbb/viewtopic.php?t=55293&p=890194&embed=true
- **AF**: Audiofanzine user reviews. https://en.audiofanzine.com/digital-mixer/yamaha/02R/user_reviews/

### The photos and what each shows **[Photo]**

Five photos from SOS96, the only clear public ones found. Page: the SOS96 URL. Files under
`https://dt7v1i9vyp3mf.cloudfront.net/styles/news_large/s3/imagelibrary/1/`:

| File | Shows |
| --- | --- |
| `1996-02-yamaha02r-1-N9ZhVGENCcYRskg6Mrg1Fgk5VH4WCvvn.jpg` | The whole console from above at an angle, powered, LCD lit. The best overall view |
| `1996-02-yamaha02r-2-TTii7MvSbKnf9c33J.TJunokykSFMmHe.jpg` | Rear panel, connectors and card slots |
| `1996-02-yamaha02r-3-PFW3lzz99xBRbyT3gylmbueadUpIaMDo.jpg` | Close-up of Selected Channel, Studio, Control Room, Talkback |
| `1996-02-yamaha02r-5-81ucMG0xKTp0VGfA.A9SUfQlnFW766Uz.jpg` | LCD close-up: the DYNAMICS page with its Library window open |
| `1996-02-yamaha02r-7-Cw7gAf9vxuavITixYNBRyfdLFyWuL0jw.jpg` | LCD close-up: the EQUALIZER page |

Other pictures: the SM cover has a small black-and-white photo; SM pp.28 to 33 are panel layout
drawings; lost-manuals' product page has one photo (not examined).

### Colours and panel **[Photo]**

These are 1996 magazine scans with a colour cast, so treat hues as right and any number as a rough
guide. The hex values are samples of flat areas, varying with the light.

- **Body:** dark green-tinged grey (about #7B887C), wooden end cheeks optional (W02SP, SM p.6).
- **LCD hood:** a raised trapezoid, darker grey (about #424844 to #4E5850).
- **Display Access block, left of the hood:** three groups of 8 keys (24 in all, agreeing with
  SOS96), on a light grey plate with white label strips: DISPLAY ACCESS keys cream/off-white (about
  #EBE7D8); CONFIGURATION keys orange/amber; MIXING and AUX keys pale blue (about #B9C9DC to #D7DFEA).
- **In the hood, left of the LCD:** a 2-digit red 7-segment SCENE MEMORY; FADER STATUS LEDs (INPUT,
  AUX, then 1 to 8); a 2-digit red 7-segment SELECTED CHANNEL; red LEDs MIC/LINE, TAPE/RTN, OUTPUT.
- **In the hood, right of the LCD:** a stereo L/R LED meter column with a dB scale from CLIP down, then
  a CONTRAST knob.
- **Selected Channel plate, right of the hood:** light grey (about #BDBFBE), white label strips.
  - ROUTING: keys 1 to 8 and DIRECT dark grey/black, **ST red**.
  - AUX: eight **light blue** keys (AUX 7 is EFF1, AUX 8 is EFF2), a light blue LEVEL knob beside a
    small vertical LED column, a white PRE key.
  - PAN: white L/MONO and R keys, a horizontal LED bar marked L/ODD to R/EVEN, a white knob.
  - EQ: **teal** keys LOW/HPF, LMID, HMID, HIGH/LPF and EQ ON (about #A2CCD5), three **teal** knobs,
    and three stacked red 3-digit 7-segment readouts (OCT; Hz/kHz with two LEDs; dB).
- **Far right:** STUDIO, CONTROL ROOM, TALKBACK and SOLO groups, all white keys with an LED dot, on a
  darker plate.
- **Encoders:** a row of 16 under the hood with knurled cream caps on dark collars.
- **Channel keys:** SEL keys dark, ON keys white with a red LED.
- **Fader caps:** channels 1 to 16 white/cream; 17/18 to 23/24 black; **STEREO red**.
- **Bottom right:** SCENE MEMORY arrow keys, a large dark data wheel with a finger dimple, ENTER,
  CURSOR keys in a cross.
- **Top strip:** pad and phantom switches, dark gain knobs, PEAK and SIGNAL LEDs per input (PEAK red,
  lit 3 dB below clipping; SIGNAL green, lit 10 dB below nominal: **[Maker: SM p.2]**).

### LCD **[Photo]**

Pale sky-blue background (about #A1D0DE) with darker blue line art and dark blue selection blocks, a
4:3-looking screen. Dark-on-light as photographed.

- **Header on both pages shown:** a number pair at left ("1 4"), a short text ("02R3", probably a
  scene title; unconfirmed), the page name in a box, the channel name at right ("MIC2").
- **Footer on both pages shown:** 16 small filled knob icons numbered 1 to 16, then two outlined
  ones, E1 and E2. SOS96 says this strip shows the 16 channels and two returns on the encoders.
- **EQUALIZER page:** a library (LIB) icon at top left; a wide response graph with gridlines at 100,
  1.0K and 10K; a two-bar meter at right (CLIP, -6, -12, -18, -30, -48). Below, four columns LOW, LO
  MID, HI MID, HIGH by three rows Q, F, G of knob icons with values under them; an ATT. knob at lower
  left; an inverted ON flag. As photographed: LOW L.SHLF, 105 Hz, 0 dB; LO MID Q 4.5, 1.00 kHz, 0
  dB; HI MID Q 4.5, 4.00 kHz, 0 dB; HIGH H.SHLF, 12.6 kHz, 0 dB; ATT -9.0 dB. That is one unit's
  state, not a proven initial state.
- **DYNAMICS page with Library open:** a status box (DYNAMICS on/off, KEYIN source and pre/post EQ,
  ST.LINK), a transfer-curve graph, a GR meter (0 to -18) beside the level meter. The Library window
  lists programs (numbers 5 to 12 visible: COMP HEAVY 1 and 2, LIMIT FAST, LIMIT SLOW, GATE FAST,
  GATE SLOW, DUCKING, BGM DUCKING), a preview curve with attack and release (0 ms, 4.61 s shown for
  LIMIT SLOW), a TITLE EDIT box, and soft keys STORE, RECALL, COPY, PASTE, INS., DEL., EXIT.

### EQ, dynamics, faders **[Maker: SM p.3]**

Input channels, as printed:

- **EQ:** four bands LOW/HPF, L-MID, H-MID, HIGH/LPF. Q 10 to 0.1 in 41 steps on all four; LOW also
  has low shelf and HPF, HIGH also has high shelf and LPF, the filters at 12 dB/oct. F 21 Hz to 20.1
  kHz in 1/12-octave steps. G ±18 dB in 0.5 dB steps. HPF and LPF have their own on/off.
- **Compressor / Expander:** threshold -54 to 0 dB in 1 dB; ratio 1, 1.1, 1.3, 1.5, 1.7, 2, 2.5, 3,
  4, 3.5, 5, 6, 8, 10, 20, infinity (16 points; **the table prints 4 before 3.5**, apparently a
  misprint, the companders' row has them in order); out gain 0 to +18 dB in 0.5; knee hard or 1 to 5
  (6 points); attack 0 to 120 ms in 1 ms; release 160 points, 5 ms to 42.3 s at 48 kHz, 6 ms to 46.0
  s at 44.1, 8 ms to 63.4 s at 32 (so the release table scales with sample rate).
- **Gate / Ducking:** threshold -54 to 0 in 1 dB; range -70 to 0 in 1 dB; attack 0 to 120 ms in 1 ms;
  hold 216 points, 0.02 ms to 1.96 s at 48 kHz (2.135 s at 44.1, 2.94 s at 32); decay as release.
- **CompanderH / CompanderS:** threshold -54 to 0; ratio 1 to 20 (15 points, no infinity); out gain
  -18 to 0 in 0.5; width 1 to 90 dB in 1 dB; attack 0 to 120 ms; release as above.
- **Other:** delay 0 to 2600 samples; attenuator 0 to -96 dB in 1 dB (p.2); phase normal or reverse.
- **Faders:** 100 mm motorised; +10 to -90 dB, then off, in 128 steps on the inputs; 0 to -120 dB,
  then off, in 128 steps on STEREO (p.2). Twenty channel faders plus STEREO; the channel faders switch
  between input level and Aux 1 to 8.
- **Libraries (p.6):** effects 40 factory and 88 user; dynamics 40 and 88; EQ 32 and 96; channel 64
  user. Agrees with SOS96.
- **Signal delay:** under 2.5 ms MIC/LINE in to STEREO out at 48 kHz. A/D 20-bit, 64x oversampling.
  Internal 44.1 or 48 kHz, external 32 kHz -6% to 48 kHz +6%.

**Block diagram [Maker: SM p.18].** Input: A/D, DC cut, ATT, PHASE, 4-band EQ, DYNAMICS, DELAY, ON,
fader, then routing (ATT before PHASE; the 01V's diagram draws them the other way round). Meter taps and a gain-reduction meter are drawn. Effect returns EFF1 and EFF2
have ATT, PHASE, EQ, delay and no dynamics, and **EFF1 can't be sent to AUX 7, nor EFF2 to AUX 8**.
Stereo out has ATT, EQ, dynamics, fader, dither.

**Test program [Maker: SM pp.115, 117, 120].** Hold STEREO SEL and cursor-right at power-on. Menu of
16: Initial, DSP, DSP & MEG, LCD, LED, Switch, Encoder, Communication, Fader, Slot, 2TR In/Out, two
blank (shown as ????), Fader Aging, Factory Preset, Exit. It can also be started by a SysEx message
(p.115, not transcribed). The LCD test flips the screen white and black. **Fader test:** after
automatic calibration, it times each fader's full run up and down; the sample screens read about
0.10 to 0.15 s. NG appears past a limit the page doesn't state.

### Behaviour

- Faders sometimes judder while moving (no effect on audio); unselected faders resist with their
  motors; selecting an Aux makes the faders jump to that send's levels. **[Review: SOS96 p.2]**
- No zipper noise or clicks heard on automation; console delay negligible. **[Review: SOS96 p.3]**
- High/Low become shelves at maximum bandwidth, by turning Q. SOS96 calls that 12 dB/oct; SM gives 12
  dB/oct for the HPF and LPF, so go by SM. Enter works as dynamics on/off while editing dynamics.
  **[Review: SOS96 p.3]**
- Scene fades up to 10 s per channel; Flip status is stored in scenes; automix undo is one level.
  **[Review: SOS96 p.2]**
- Yamaha's factory reset recalibrates the faders, finishing with all at the bottom. **[Forum: SOSF,
  quoting Yamaha]**
- Opinions on sound split, some finding it punchy and others gritty, with no measurements. One user
  found the EQ a little soft.
  **[Forum: SOSF; AF]**
- Backup battery CR2032, status on UTILITY page 4, replaced via the rear panel.
  **[Forum-x: https://www.controlbooth.com/threads/battery-change-o2r.8625/]**

### Couldn't find

- **The 02R's LCD resolution.** Careful: the "320 x 240" often quoted turned out to be the **03D's**
  (https://www.soundonsound.com/node/4926100) and the **02R96's**
  (https://www.soundonsound.com/node/4917765), both siblings. The SM pages read don't give it.
- The fader taper between its end points (only range and 128 steps are published).
- EQ filter design, compressor detector type or knee curves. SM p.3 gives only the control values.
- How the Version 2 panel differs from Version 1, if at all. No photos labelled V2 turned up.
- Any calibrated colours or Pantone-style references.

---

## 2. Yamaha 01V (original, not the 01V96)

**Sources**

- **SM**: Yamaha 01V Service Manual, PA 011401, printed March 1997, 145 pages.
  https://www.lost-manuals.com/en/manufacturer/yamaha/01v (saved, see end). Pages as printed. The
  scan's pages are stored mirrored and sideways, so a viewer may show them turned. Read so far: cover,
  pp.2, 20, 21, 22, 73, 74.
- **SOS**: Sound On Sound review, July 1998. https://www.soundonsound.com/reviews/yamaha-01v
- **FF**: forum thread on 60 mm faders.
  https://recording.org/forum-topic/pro-audio-repair-modifications/60-mm-fader-yamaha-03d-01v-promix-01-aw4416-aw2816

### Found

- **Fader resolution [Maker: SM p.2]:** 60 mm motorised, 15 of them, all 128 steps. Master faders
  except ST OUT: 0 to -72 dB then off. Other faders: +6 to -72 dB then off. ST OUT: 0 to -96 dB then off.
- **Hardware [Maker: SM p.21]:** LCD 320 x 80 dots. Main CPU SH7034 (20 MHz crystal). Four DSP3 chips:
  #1 to #3 on the main path (#3 with DRAM), **#4 labelled EFFECT, with its own DRAM**. LED meter 12 elements x
  2. Each fader has its own motor driver. Encoders: PAN, F, G, RETURN 1 and 2, PARAMETER.
- **Software block and level diagram [Maker: SM p.20]:** input path A/D, polarity, ATT, 4-band EQ,
  DYNAMICS, DELAY, ON, fader. The level diagram has analogue and digital dB scales and marks the DSP
  noise floor, with stages ADC, Phase, ATT, EQ, Dynamics, Delay, On, Level, Pan, Stereo bus, EQ,
  Master, Dynamics, Balance, Master On, Meter, DAC. The small print is worth reading off at full size.
- **Signal delay [Maker: SM p.2]:** under 2.5 ms CH IN to ST OUT at 44.1 kHz. Clock 44.1 kHz -10% to
  48 kHz +6%.
- **Fader test [Maker: SM pp.73, 74]:** calibrates, then times each fader up and down; the sample
  screens show 0.11 to 0.15 s.
- **EQ [Review: SOS]:** 21 Hz to 20 kHz in 1/12-octave steps, ±18 dB in 0.5, Q 10 to 0.1; outer bands
  gain a high/low-pass at the narrow end and a shelf at the broad end. Reset to flat by pressing the
  high and low band buttons together (not marked on the panel).
- **LCD pages [Review: SOS]:** EQ page has three virtual knobs, a response graph and a table of all
  12 parameters. Two dynamics pages with a transfer curve. Three effects pages (recall, save, edit,
  pre/post source).
- **Effects [Review: SOS]:** two engines, derived from ProR3 and REV500. 33 listed: Reverb Hall, Room,
  Stage, Plate; Early Reflections; Gate and Reverse Gate; Mono, Stereo, Modulated, LCR Delay; Echo;
  Chorus; Flange; Symphonic; Phaser; Autopan; Tremolo; HQ Pitch; Dual Pitch; Rotary; Ring Mod;
  Modulated Filter; Distortion; Amp Simulator; Dyna-Filter, Dyna-Flange, Dyna-Phaser; Rev/Chorus,
  Rev/Flange, Rev/Sympho, Rev/Pan; Delay/ER; Delay/Reverb; Distortion/Delay; Multi Filter; Freeze.
  **HQ Pitch and Freeze only on effect 2.**
- **Other figures [Review: SOS]:** channel delay to 250 ms (metres, samples or ms); output delay to
  300 ms on Omni and main; 40 EQ and 34 dynamics presets; 100 scenes with crossfades to 25 s; 32-bit
  fixed point; overall gain about 6 dB lower than the 03D's.
- **Quirks [Review: SOS]:**
  - Raising compressor out gain lowered the gain reduction shown on the meter (Yamaha said a fix was
    coming).
  - The main stereo Balance control behaves with an unusual 12 dB offset.
  - Monitoring is pre-fader, so moving the main fader changes the recording but not what you hear.
  - Swapping digital and analogue channels swaps the entire signal path.
  - Analogue controls (pads, phantom, gain) aren't stored in scenes.
- The same 60 mm motor fader is shared with the 03D, ProMix 01, AW4416 and AW2816, going by a repair
  thread's title. Part number VZ523400 was suggested as a replacement, unverified. **[Forum-x: FF]**
- User reports: T-DIF cards wouldn't take word clock sync, while ADAT timing was solid; the reviewer
  also preferred the 01V's effects to those of a later 02R V2. One opinion, 2001. **[Forum]**
  https://en.audiofanzine.com/digital-mixer/yamaha/01V/user_reviews/r.93796.html

### Couldn't find

- Any measurement of the fader law. Only range and steps are published.
- Effects algorithm internals. The SOS review notes only their ancestry.
- A full catalogue of LCD pages from an outside source (the owner's manual remains the reference).

---

## 3. Yamaha ProMix 01

**Sources**

- **SOS**: Sound On Sound review, January 1995. https://www.soundonsound.com/reviews/yamaha-promix-01
- **MEF**: repair thread (January 2013). https://music-electronics-forum.com/forum/music-electronics/32729-yamaha-01-mixer-repair-help

### Found

- LCD 240 x 64 dots, backlit. 12-segment LED pair for master level. **[Review: SOS]**
- 60 mm motorised faders, moving under MIDI. Four fader groups, any fader in any or all. **[Review: SOS]**
- Internal 24-bit linear, EQ 36-bit linear; 48 kHz only; A/D 20-bit 64x; stereo D/A 20-bit 8x, monitor
  and Aux 3/4 18-bit 8x. **[Review: SOS]**
- EQ: three parametric bands, 32 Hz to 18 kHz, Low and High shelving by default and switchable to
  peaking; 30 presets, 20 user. **[Review: SOS]**
- Three dynamics processors, patchable to inputs, effect sends or stereo out; 10 preset, 10 user
  programs. **[Review: SOS]**
- Two effects from Aux 1 and 2; 30 presets and 10 user; similar to SPX90/SPX900. Named by the
  reviewer: Reverb Hall 1, Reverb Vocal, Flange > Reverb, Vocal Doubler, Symphonic, Auto Pan.
  **[Review: SOS]**
- 50 scenes, recalled by front panel or MIDI Program Change. **[Review: SOS]**

**What the manual leaves out (from the review and users):**

- No solo keys; a single signal is heard by muting the others or with CUE. **[Review: SOS]**
- The faders feel light, and the unit delicate for road use. **[Review: SOS]**
- At review time, EQ couldn't be controlled from the computer through the Logic/Cubase mixer maps
  (a limit of the maps or the firmware then; not known which). **[Review: SOS]**
- Phantom is one switch for inputs 1 to 8. No proper multi-desk linking; it goes through the stereo
  input. **[Review: SOS]**
- With nothing plugged into the stereo-in jacks, the 2-track input feeds the stereo input channel,
  so a finished stereo mix can take EQ. **[Review: SOS]**
- A long-term user (8 to 9 years) names the fader belts as weak and calls the sound cold and a bit
  metallic. Machine-translated and partly ambiguous. **[Forum]**
  https://en.audiofanzine.com/digital-mixer/yamaha/Promix-01/user_reviews/r.44590.html
- Panel keys sit on a scan matrix, with channels 1 to 8 and 9 to 16 on rows SC4 and SC5 and columns
  SD0 to SD7. A service manual exists (one poster cites p.51, holding METER and CUE at power-on for a
  test mode, which didn't work for him). **[Forum: MEF]**

### Couldn't find

- A free, verifiable copy of the service manual. MEF's links are a dead koston.us PDF and an
  elektrotanya listing that wasn't findable; a paid copy is listed (see Documents to consider).
- Fader law measurements, block diagram information beyond the user guide, effects internals.

---

## 4. Allen & Heath dLive

Status of the questions in `DLIVE_RESEARCH.md`: **none is settled by public sources.** A&H's support
articles refused automated reading, so what follows is mainly forum posts and search excerpts.

### The two open conflicts

- **Does a mix's compressor go pre/post PEQ?** Not settled. Forum users describe the option on
  **input** channels (the order is PEQ/Comp or Comp/PEQ, and that decides which Mix Source tap sees
  compression). None mentions mixes.
  **[Forum]** https://forums.allen-heath.com/t/dlive-input-compressor-routing/8084
- **Where does AMM's gain sit?** Only more of the same. A&H's AMM article says it uses its own
  post-fade gain element, affects post-fade sends and not pre-fade ones, and detects before the
  insert. That agrees with the guide's text against the diagram. Nothing independent.
  **[Maker-x]** https://support.allen-heath.com/hc/en-gb/articles/39867820612625-AMM
  - An older thread has a user saying AMM seemed pre-fader (console and firmware unclear).
    **[Forum-x]** https://forums.allen-heath.com/t/automatic-mixer/9149
  - PFL didn't work on channels active in AMM for one user on V2.11; another says it does. Unresolved.
    **[Forum-x]** https://forums.allen-heath.com/t/pafl-needs-to-work-on-channels-when-amm-is-engaged/32298

### Factory defaults

- **Mix Source default:** not found. Tap point is per mix (Post Preamp, Post Insert A Return, Post PEQ,
  Post Delay). One user found Mixes 1 to 8 weren't set to Post Delay, which is no statement of a default.
  **[Forum]** the routing thread above, and https://forums.allen-heath.com/t/tap-points/4635
- Sibling: an A&H forum reply says the **SQ's** default is Post PEQ.
  **[Forum-x, sibling]** https://forums.allen-heath.com/t/sq-5-ducker-routing-issue/12658
- **New Aux sends pre or post-fade:** not found for dLive. Sibling: on the **iLive**, users say Auxes
  default post-fade. **[Forum, sibling]** https://forums.allen-heath.com/t/aux-pre-post/966 and
  https://forums.allen-heath.com/t/change-aux-sends-to-pre-fade/599 . The dLive Surface Getting
  Started Guide (Issue 4, p.12, held) covers toggling only, not the default.
- All Pre/Post at once: hold Pre/Post and press a master Mix or Sel.
  **[Forum]** https://forums.allen-heath.com/t/aux-sends-global-pre-post/11638

### Ranges and tapers

- **Dyn8:** no published ranges found. Effects add-ons on the SQ are said to come from the dLive
  range (see section 5), so the RackExtra numbers remain the nearest guide.
- **Effect knob ranges:** nothing beyond what `DLIVE_RESEARCH.md` already holds.
- **Manual compressor attack and release:** not found. Sibling figures: Qu-32 attack 300 µs to 300
  ms, release 100 ms to 2 s. **[Maker-x, sibling]**
  https://www.allen-heath.com/content/uploads/2023/06/Qu-32-v1.8-Chrome-Technical-Datasheet.pdf
- **Automatic compressors:** the designer (iLive, 2011) said the Vocal mode trades attack speed for
  low distortion, so it's slow on fast transients; for a fast compressor with acceptable distortion he
  suggests 16T; auto modes use smoothing and modelling, not fixed attack/release. No numbers.
  **[Forum, sibling]** https://forums.allen-heath.com/t/compressor-type-profile-specifics/890
- **Surface fader taper:** not found. Hints only:
  - On a **GLD** photo, list posters noted -20 to -30 takes much more travel than -10 to -20 and
    called it a custom taper. **[Forum-x, sibling]**
    https://lists.linuxaudio.org/archives/linux-audio-dev/2014-August/035159.html
  - A QLab post gives a fader-to-MIDI mapping, value = (dB + 54) / 64 x 127, console unstated. That's
    the protocol's scale, not a physical taper. **[Forum-x]**
    https://groups.google.com/g/qlab/c/mLLheQdHQtc

### What Director shows (not downloaded)

- Launch coverage (V1.2, June 2016): touch-friendly, mirrors the Surface layout, resizable fader
  banks; used to prepare shows offline or mix from a Windows tablet. **[Review]**
  https://audioxpress.com/news/allen-heath-introduces-surfaceless-mixing-and-preamp-modeling-for-dlive
- It copies the desk's layout, one fader bank at a time. Since V1.42 a "Windowed" option in the System
  menu lets faders and touchscreen view run on separate screens, and more than 12 faders fit.
  **[Forum]** https://forums.allen-heath.com/t/dlive-director-fader-screen/5790
- Channels not assigned to the Surface can't be edited; soft keys are large and take space; the I/O
  tab's zoom steps jump. **[Forum]** https://forums.allen-heath.com/t/where-is-it/4904
- Offline it's a full system; online it's a surface only; an offline system can't drive an online
  Surface. **[Forum]** https://forums.allen-heath.com/t/console-in-off-line-mode/6886
- No detailed public walkthrough of its screens was read.

### Also noticed

- A dLive forum thread (still active in 2026) complains the reference guide's PDF is gone, and A&H's
  old V1.9.2 PDF URL now returns 404. The web version may be the only official one.
  https://forums.allen-heath.com/c/dlive/7

### Couldn't find

Mix Source factory default; dLive's pre/post-fade default; Dyn8 ranges; effect knob ranges beyond
those held; the Surface's fader taper; manual compressor ranges; anything settling either conflict.
A console or Director would settle most of these.

---

## 5. Allen & Heath SQ-5

Nothing here is about the SQ-5+, which the owner keeps out.

- All DEEP and FX add-ons come from the dLive range, says a forum reply pointing users to the dLive
  guide's DynEQ4 section. The poster reads as A&H staff but isn't labelled so. **[Forum]**
  https://forums.allen-heath.com/t/user-manual-for-dyneq/13784
  So the RackExtra ranges in `DLIVE_RESEARCH.md` (MultiBD, DynEQ4, De-Esser, Bucket Brigade, Echo,
  Hypabass) are the best available, as a **sibling** figure.
- The SQ guide's add-ons chapter we hold is only about 1,100 characters of text and gives no ranges.
- DEEP compressors named as Opto, 16T, 16VU, PeakLimiter76, Mighty, OptTronik, Bus.
  **[Dealer listing, excerpt; SQ-Rack, sibling]** https://stagesupply.co.uk/?p=71
- MIDI over TCP: port 51325 for non-A&H clients. **[Maker-x]**
  https://support.allen-heath.com/hc/en-gb/articles/47793519540497-SQ-MIDI-Protocol-V1-6
- No authentication on that port, and sending the Bank + Program bytes for Scene 1 recalls it. From
  excerpts of these threads, exact post not pinned down. **[Forum-x]**
  https://forums.allen-heath.com/t/tcp-protocol-spec/13701 ,
  https://forums.allen-heath.com/t/sq-api-midi-tcp-ip-control-documentation/15136 ,
  https://forums.allen-heath.com/t/sq-preset-reacll-from-ahm/14150
- Bitfocus Companion SQ module, MIT licence (no protocol detail on its main page).
  **[Maker of module]** https://github.com/bitfocus/companion-module-allenheath-sq

### Couldn't find

Control ranges specific to the SQ's add-ons; ranges for the DEEP compressor models.

---

## 6. Behringer WING

Against the list in `UNIVERSAL_MIXER.md`, "Still open from the WING work".

**Source. EG:** WING Effects Guide, firmware 3.0 (V0.0), from Thomann's document store (saved, see
end). https://images.static-thomann.de/pics/atg/atgdata/document/info/599924_m_be_0603-aen_wing_effects_guide_dark1_ww.pdf

- **STARTUP-skip key: answered.** Hold **LIBRARY** while powering on: the console doesn't auto-load
  STARTUP show, snapshot or snippet files. **[Maker-x]** WING manual excerpts,
  https://www.manualowl.com/m/Behringer/WING/Manual/613436 and
  https://manuals.online/behringer/music-mixer/wing/user-manual/toyu (excerpts garbled, so check).
  The excerpts say Behringer's remote protocol document describes the same bypass. Also from them:
  hold **CLR SOLO** at power-up to skip the saved state (for boot loops).
- **VSS3 preset list: not found.** EG says only that it's based on the TC Electronic Reverb 6000, is
  true stereo, has presets on a menu loaded with Load, and has an expand arrow for early reflection,
  tail, modulation and general controls. It lists no names. **[Maker: EG]** A Russian community guide
  to 3.0.5 doesn't either. https://m.vk.ru/@tridentsound-rukovodstvo-po-effektam-wing-chast-1
- **Reverb or delay tail on bypass: not found.** Not covered in EG or in posts read.
- **EXTERNAL effect LATENCY unit: not found.** EG calls it a knob to time-align the wet and dry
  signals and states no unit. **[Maker: EG]** One reviewer measured about 1.14 ms extra through
  the External FX route with Waves SuperRack, which doesn't reveal the knob's unit.
  **[Measured, excerpt]** https://drewbrashler.com/?p=42827
- **Sources recalled with ROUTING: partly.** The manual's colour legend: blue is recalled with a
  snapshot, grey isn't, red is protected by Safe. Nothing specific to sources. **[Maker-x]** as above.
- **FX / CHAN preset show items:** a channel preset can go to any strip.
  **[Forum-x]** https://behringer.world/viewtopic.php?p=6375
  Show Control (firmware 2.0 on) groups snapshots, snippets, recordings, FX and channel presets, per
  a search excerpt whose page wasn't identified. Where each item loads isn't stated anywhere found.
- Other facts in EG: Premium effects need external memory and only run in slots 1 to 8; standard
  effects run in all 16. Gate, compressor and EQ plug-ins add no channel latency; effects in an insert
  may add a very small delay. **[Maker: EG]**

### Couldn't find

VSS3 preset names; tail behaviour; the LATENCY unit; WING Edit's COMPACT fallback; whether a real
console stores newer firmware's parameters or snaps values to steps; the DAW-mode escape. The Windows
repaint seam and the storage and snap questions are Sidecar's own, not web questions.

---

## 7. Behringer X32

- **Latency [Measured]** (pink noise, dual FFT, 48 kHz, X32 Compact, firmware 1.15, 2014):
  https://trbtr.de/pmw/pmwiki.php/Text/X32 . AD in to DA out 0.83 ms in every routing tried;
  tap point didn't matter; with a GEQ in any insert 1.50 ms; via S16 stage box AD 0.93 ms, S16 AD and
  DA 1.04 ms; Aux in to DA out 0.85 ms. **Note: X32 Compact, a close sibling.**
- Published figure: 0.8 ms local, 1.1 ms networked. Seen in a search excerpt of a product listing or
  manual, page not pinned down (possibly https://www.adorama.com/bex32k.html ). **[Maker-x]**
- **Not the X32's own effects:** Waves plug-ins through the X-WSG card add 0.67 ms (compressors) to
  1.42 ms. **[Blog tied to Waves, excerpt]** https://drewbrashler.com/2017/waves-x-wsg-latency/
- Round trip via USB: 8.9 ms at a 64-sample buffer at 48 kHz, against 4.4 ms for an RME Babyface.
  **[Measured, Forum-x]** https://forum.rme-audio.de/viewtopic.php?id=39427
- X-Dante to stage box, round trip 4.5 ms by Smaart. **[Measured, Forum-x]**
  https://www.behringer.world/viewtopic.php?p=7663
- **Fader law [Forum-x, transcribing Maillot's OSC document p.132]:** float f (0 to 1) to dB, in four
  straight pieces: f >= 0.5: 40f - 30; 0.25 to 0.5: 80f - 50; 0.0625 to 0.25: 160f - 70; below
  0.0625: 480f - 90 (bottom is off). Checked: 0.75 is 0 dB, 0.5 is -10, 0.25 is -30, 0.0625 is -60.
  https://behringer.world/viewtopic.php?p=8838 and https://behringer.world/viewtopic.php?p=11070
  (not openable here; confirm against the document).
- **Open-source and protocol projects:**
  - Patrick-Gilles Maillot's X32 tools and an OSC-only X32 emulator: GPL v3 or later per the README.
    https://github.com/pmaillot/X32-Behringer . Some compiled utilities on his site are separately
    freeware for non-commercial use. https://www.sites.google.com/site/patrickmaillot/x32
  - His unofficial OSC protocol document (updated June 2021), the de facto reference. See Documents
    to consider.
  - Bitfocus Companion X32 module, MIT. https://github.com/bitfocus/companion-module-behringer-x32
    (its changelog mentions a fader curve update, with no values on the page).

### Couldn't find

Any published measurement of the X32's EQ curves or its compressor behaviour (attack, knee, ratio).
Beware false matches: a "Wadia X-32" measurement page is a different product entirely.

---

## Saved to `C:\Users\schul\Claude\Sidecar-reference\`

Downloaded on the owner's later permission, each in its own new folder, none in the repository.

| Saved at | Source | Size |
| --- | --- | --- |
| `yamaha-02r\service-manual-1995\yamaha--02r--service--ID9851.pdf` | https://www.lost-manuals.com/pdf/yamaha--02r--service--ID9851.pdf (via https://www.lost-manuals.com/en/manufacturer/yamaha/02r) | 12.4 MB, 205 pp |
| `yamaha-01v\service-manual-1997\yamaha--01v--service--ID9893.pdf` | https://www.lost-manuals.com/pdf/yamaha--01v--service--ID9893.pdf (via https://www.lost-manuals.com/en/manufacturer/yamaha/01v) | 10.6 MB, 145 pp |
| `yamaha-02r\photos-sos-1996-02\` (5 JPEGs named in section 1) | Sound On Sound's image store, from https://www.soundonsound.com/reviews/yamaha-02r | 1.8 MB |
| `behringer-wing\effects-guide\wing_effects_guide_thomann_599924.pdf` | Thomann URL in section 6 | 12.7 MB |

Notes: lost-manuals says its manuals are for personal use and meant for qualified technicians.
Both service manuals are image-only scans with no text layer. The Sound On Sound photos are the
magazine's, for private reference only.

## Documents to consider

Not downloaded. Owner to decide.

| Document | URL | Size | Why it was left, and what it offers |
| --- | --- | --- | --- |
| Unofficial X32 OSC Protocol (Maillot, June 2021) | https://drive.google.com/file/d/1Yt_S1mpPt3CAzeq3Dnpe_IqctQ-1GlTz/view?usp=drive_link | Not shown | A personal Drive, not a maker's site or library. The standard X32 protocol reference, with the fader law at p.132 |
| X32 OSC Remote Protocol (Behringer) | http://www.behringer.com/assets/X32_OSC_Remote_Protocol.pdf | Not shown | Old forum link, not checked. Maker's own protocol document, if it still exists |
| OSC Remote Control Documentation for WING (Maillot, 2020) | No working URL found | Not shown | Named in search results only |
| VSS3 TDM manual (TC Electronic; sibling of WING's VSS3) | https://service-tcgroup.tcelectronic.com/media/VSS3_TDM_US.pdf | Not shown | Host didn't resolve from here. May show the original's preset names |
| ProMix 01 Service Manual | https://www.emanuals.com/yamaha-pro-mix-01-service-manual.html | Not shown | Paid storefront, unverified |
| dLive Firmware Reference Guide V2.1 (third-party copy) | https://manuals.plus/m/36e4e47b9bec34063dbe44a6efc3a6e00f4434016d5566daa6d6fb9cce10cd75 | Not shown | Not confirmed official, and refuses automated reading. Would help the dLive defaults search |
| dLive Firmware Reference Guide V1.9.2 (third-party copy) | https://manualmachine.com/allenheath/dm0/27244858-allenheath-dlive-firmware-reference-guide-v1-9-2 | Not shown | Older; A&H's own copy is now 404 |
| SQ MIDI Protocol, Firmware V2.0.3 (web article) | https://support.allen-heath.com/hc/en-gb/articles/47793537575953-SQ-MIDI-Protocol-Firmware-V2-0-3 | Web page | Newer than the V1.6 held, but it may belong to the SQ+ line. Owner to say whether it is wanted |
