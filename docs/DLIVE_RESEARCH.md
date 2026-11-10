# dLive: what's still missing, and what the public sources say

Research notes for the items still open on the Allen & Heath dLive profile (see
`docs/UNIVERSAL_MIXER.md`). Facts only, in our own words: names, ranges and behaviour, each
with where it came from. No text, pictures or firmware from the maker are copied here.

The main source is the online **dLive Firmware Reference Guide** on
support.allen-heath.com. Careful: its sister Avantis guide has chapters with the same titles
(a search for "Routing" lands on the Avantis one).

Researched November 2026, against firmware V2.12 (January 2026), the newest found.

| Still missing | Public detail | What's left unknown |
| --- | --- | --- |
| Mixer Config, IP Stereo Config | Enough to build | Nothing important |
| Ext In to mixes | Enough to build | Nothing important |
| Per-mix send source points | Enough to build | Nothing important |
| AMM | Enough to build | The side-chain filters' ranges; the algorithms' details |
| Preamp models | Controls known | Knob ranges, the curves |
| NEQ-12 | Enough to build | Width range |
| Dyn8 | Built (`DLVDYN8`): controls known | Its own ranges (RackExtra's and the Avantis guide's picture are the nearest guide) |
| RackExtra FX (20) | Built (`dlive_fx`): controls known, some ranges | Most knob ranges, the algorithms |
| RackUltra FX (11) | Built (`dlive_fx/ultra`): controls known, some ranges | Most knob ranges, the algorithms |
| MIDI over TCP | Full protocol | Newer messages since V2.0, if any |
| Surface | Guides already held | Fader taper |

## Mixer Config (chapter MixRack Setup)

- 64 configurable mix busses. Set the count of mono and stereo Groups, RackExtra FX sends,
  Auxes and Matrices. Any combination, but mono busses change in pairs; a stereo bus uses 2.
- The 8 stereo RackUltra FX send busses are **extra**, on top of the 64, and only with that card.
- PAFL: number of stereo PAFL busses (1 for a normal system).
- Main modes: None, LR, LR+Msum, LR+M, LCR, LCR+, 5.1. (The Processing Specs table lists only
  five of them; the chapter says "5 modes" and then describes seven.)
  - LR+Msum: an extra output, the mono sum of LR pre-fade and pre-processing.
  - LR+M: LR and M are independent busses, channels assign to each.
  - LCR: at mid pan all signal goes to C, none to L/R; stereo channels feed LR only.
  - LCR+: Pan balances L/R, Divergence blends C against LR.
  - 5.1: each channel can be LR, LCR or LCR+ for the front three, plus a 5.0 position, LFE and
    Non-LFE levels.
- Main Fader Strips: LR as one combined strip (with Balance) or two individual strips.
- Screen shows busses and FX sends remaining and warns past the maximum. Applying resets mix
  parameters, everything else stays.

## IP Stereo Config

128 inputs, each pair switchable mono/stereo with Apply. A stereo channel has one strip and
linked processing. Separate from Ganging, which is for on-the-fly linking.

Stereo image modes on a stereo channel's Preamp page: L/R, R/L, L-Pol/R, R-Pol/L, Mono, L/L,
R/R, M/S (first input is Mid, second is Side), plus Width 0-100%.

## Ext In to mixes (chapter Processing Screen)

Any Mix channel can take an external source: sockets, Mix busses or PAFL. It sums **pre-insert**
and is affected by the mix processing and fader. Trim and Socket Preamp shown when assigned.
Talkback and SigGen toggles on the same page (SigGen to one or both sides of a stereo mix).

## Per-mix send source points (chapter Routing)

- **Mix Source**, set once per mix, for all channels feeding it: Post Preamp, Post Insert A
  Return, Post PEQ, Post Delay. Pre/post-fade is still per send.
- Direct Out source, one global setting: Post Preamp, Post LPF, Post Gate, Post Insert A Return,
  Post PEQ, Post Compressor, Post Insert B Return, Post Delay. Options: Follow Fader, Follow
  Mute, Follow All (DCA and mutes too).
- Follow Main Pan on each stereo mix, with Invert.
- Group to Group: source Post Comp or Post Delay per Group; adds 4 or 8 samples, not
  latency-compensated.

## NEQ-12 (Processing Screen, Processing Specs)

Each mix's NEQ slot is either a 28-band GEQ (4 types) or a 12-band PEQ, chosen with NEQ Type.

- 12 bands, shown three tabs of 4. Each band: In, Frequency, Width, Gain ±15 dB.
- The LF and HF bands can be shelf, bell or hi/lo-cut at 12, 24 or 48 dB/oct.
- Overall In button. That's 12 × 4 = 48 band controls, an overall In and two outer-band shapes:
  51 parameters. Built as `DLVNEQ12` (`livedaw_dsp::dlive`); `MAX_EQ_PARAMS` grew from 32 to 64.
- Width: the screen picture shows values such as 0.6, 0.3 and 1/9 (octaves). Its range isn't
  stated. The 4-band PEQ's is 1.5 to 1/9 octave.

### Built: `DLVDYN8`, `dlive_fx/dynamics/dyn8.rs`, and the RackUltra card, `dlive_fx/ultra`

Every algorithm is Sidecar's reading; each module's comment says what is stated and what isn't.
A dLive's table holds the 38 models (RackExtra 20, Dyn8, 11 RackUltra). `dlive_fx::slot_rule` keeps each to its own engines: RackExtra 0..16, RackUltra 16..24, Dyn8 24..88 (a Universal strip takes any: RackUltra FX 1-8, Dyn8 any slot).

Public but unreadable here: the support site refuses fetches; the search engine's extract of its RackUltra article gave the Shifter's Precision, Gridder's tolerances, Harmoniser Interval reading and five cabinet names (FDR 1x12, FV 2x10, VX 2x12, Bass 8x10B, Bass 1x15). Four cabinets stay numbered. The Avantis guide (held, `Sidecar-reference/allen-heath-avantis`) has Dyn8's page: its key Split/Wide meaning (same key to both sides / true stereo) is used here.

## Dyn8 (Processing Screen; RackExtra's Dynamic EQ and Multiband entries)

64 engines, inserted at either insert point or a mix's insert. Each is 4 bands of multiband
compression **and** 4 bands of dynamic EQ.

Multiband:
- Ballistics: 2 manual (Peak, RMS: attack and release per band) and 4 automatic (Punch, Opto,
  Slow, Fast).
- Crossover slope 6, 18 or 24 dB/oct. Knee Hard or Soft. Link and Rel (relative) modes.
- Per band: threshold, gain, ratio, In; Self or External key; Split or Wide key range.
- The RackExtra Multiband's numbers (likely the same design, to confirm): ratio 1:1 to 20:1,
  gain -6 to +18 dB, threshold -50 to +18 dBu.

Dynamic EQ:
- Ballistics per band: Std 9 (smooth, frequency-conscious release) or Fast 9.
- Per band: threshold, gain (maximum cut or boost), width, frequency, Above or Below.
- RackExtra DynEQ4's numbers: frequency 40 Hz-18 kHz; bell 2.25 to 1/9 oct on the mids, 2.1 to
  1/9 on bands 1 and 4, which become shelves at the widest; threshold -50 to +18 dBu; sidechain
  Cascade or Parallel; output trim ±12 dB.

## Preamp models (chapter DEEP Processing)

Both load in the Preamp page. Controls read from the panel picture as well as the text.

- **Dual-Stage Valve:** On/Off. Stage-1: Triode or Pentode, HARMONICS. Stage-2: Off, Triode or
  Pentode, BIAS, HiDrive on/off. HF LIFT (marked 0 to 6). OUTPUT.
  Triode leans to even harmonics, Pentode to odd (mainly 3rd). Bias sets the overdrive; HiDrive
  moves from break-up to continuous overdrive.
- **Tube Stage:** On/Off, a rotary mode selector, Drive, Level.

## AMM (chapters MixRack Setup, DEEP Processing)

Up to 64 members. 1, 2 or 4 AMMs (64, 32 or 16 members each). Its gain element sits just after
the fader; it affects post-fade sends only, not pre-fade. Detection is taken pre-insert, so PEQ,
compressor and fader don't affect it. Mode applies to all AMMs.

**NOM mode:** works as a gate with an adaptive threshold (it follows the room noise, there's no
threshold control). Each open mic is reduced equally according to how many are open. The last
open mic stays open (ambience). Controls: On per channel, Chair, Best Mic, Solo (exclusive);
sidechain HPF and LPF; Off Attenuation -10 to -90 dB; On Hold 1 to 5 s (not for the Chair); NOM
Attenuation 3 to 6 dB per doubling; Chair Sensitivity 1 to 10; Chair Duck -3 to -40 dB. Per AMM:
own Chair, Best Mic, Solo.

**D-Classic mode:** constant gain sharing, each mic's gain proportional to its input level.
Priority per channel -15 to +15 dB; sidechain HPF and LPF. No Best Mic, Solo or Chair.

Stored in scenes.

## RackExtra FX (chapter DEEP Processing; every model has a panel picture there)

16 engines. Each can be Unassigned, Inserted on a channel (some allow Dual Mono on two mono
channels) or Mix->Return. Each has a 4-band PEQ and Stereo Width. Global Tap Tempo is shared
by any delay locked to it.

| Model | Controls and stated ranges |
| --- | --- |
| SMR Reverb | Models Classic, Hall, Room, EMT. LF Cut 0-400 Hz and HF Cut 2-20 kHz (24 dB/oct, on the input), Predelay, Decay, HF Decay, HF Slope. Expert: Source Diffusion, Size, Shape (plate only), Ref Detail; Echo 1 and 2 with levels; Body and Tail Diffusion, Mod Depth and Speed; LF Decay, LF XOver, Colour, Colour Freq; Reflection and Decay levels |
| Stereo Tap Delay | Up to 2.7 s. BPM or ms; whole to 1/16 notes, dotted and triplet. Input HP and LP, Link, Local or Global Tap, feedback filter frequency and slope, Scatter (or ping-pong), Feedback, Width |
| Bucket Brigade Delay | Up to 2.7 s. BPM or ms. Input HP, Low and High Damp, feedback slope, Dist (selects the feedback non-linearity), Global Tap |
| 2-Tap Delay | Left out of the factory library since V1.5. 5-1360 ms in 1 ms steps, LF Cut 20 Hz-8 kHz, HF Cut 400 Hz-20 kHz (24 dB/oct), Link, Scatter, Feedback, feedback filter and slope, Width |
| Echo | Tape echo, stereo in and mono out. Input ±15 dB, Noise, Mode 1-7 (three heads, 4-7 are combinations), Bass, Treble, Repeat Rate, Intensity (oscillates past 75%), Echo Volume ±15 dB |
| Gated Verb | Types Classic, Panner, Powerbox. Lo cut 20 Hz-6 kHz (24 dB/oct), Hi cut 400 Hz-20 kHz (12 dB/oct), Predelay 0-170 ms, Attack, Hold, Release (500 ms at most together), Mono/Stereo/Wide, Diffusion |
| ADT Doubler | Delay Separation, Thickness, Double or Quad, Wide, Autopan with Depth and Speed |
| Chorus | Stereo-field switches LFO Split, Wide, MultiVoice; Rate, Depth, Sine or Rectified, Pan Depth |
| Symphonic Chorus | Frequency, Depth |
| Flanger | Types Ambient, Vintage, Wild. Stereo Split, Stereo Spread, LFO Manual, Sine, Rectified, Inverted Rectified, Triangle; Speed, Depth, Regenerate |
| MOO 12 Stage Phaser | Phasing Stages, Resonant Stages, Mix Resonance (to 10), LFO Rate 0.1-10 Hz, LFO shape (4), Mono or Split, Depth, Offset |
| Hypabass | Upper Freq Cut, LF Input Boost, Infra (18-36 Hz) and Sub (above 36 Hz) levels, Infra Bass or Lost Fundamental mode |
| De-Esser | Two channels and Link. Frequency 3-8 kHz, Reduction to 24 dB, Response Slow/Medium/Fast, filter Classic/Shelf/Band Pass, Listen. Auto-threshold |
| Rotator | Rotors on/off, Speed (from 0.1 Hz; chorale and tremolo marks), Width, Balance, Deflectors, Back Panel, Mic Select (three, with Separation), Tube Drive on/off and amount 1-11, Mains Hum, 50/60 |
| VS1 Vocal Shift | Two voices and Link. Semitones ±12, Cents, Oct Down, Flat, Width. Under 6 ms |
| Pitch Doubler | Two voices. Delay 0-25 ms, Pitch ±100 cents, Width, Level; pitch LFO Active, Rate 0.1-10 Hz, Depth 0-100. Under 6 ms |
| Dynamic EQ | See Dyn8 |
| Multiband Compressor | See Dyn8. Two types: 3-band and 4-band |
| Transient Controller | Two channels and Link. Attack ±15 dB, Sustain ±24 dB, Pre and Post Gain ±15 dB, In, Transient or Continuous |
| Dimension Chorus | Mode 1-4 and E, Output Wet Only or Dry+Wet |

## RackUltra FX (same chapter; needs the optional card)

8 more engines, each with a **stereo send and return outside the 64 mix busses**, so a console
with the card has 8 more stereo sends and 24 FX returns in all. Sidecar's FX return ceiling is 16
(`sources::NUM_SEND_FX`), so that would have to grow.

Each has a channel strip the RackExtra FX don't have: pre and post 4-band PEQ, pre and post
dynamics (Gate, Expander, Compressor or Ducker) keyed from self, one external source per FX, or
the FX's input or output, plus Stereo Width. A console-wide Global Key, also settable by MIDI.

| Model | Controls and stated ranges |
| --- | --- |
| Spaces Reverb Designer | Pre Delay 0-170 ms, Decay 0.1-60+ s. Tabs Home (Quick Setup), Space (Reflections model, Size, Stereo Spread, Direct Send, Early/Late/Sub levels; Sub is below 80 Hz), Echoes (6, each gain -40 to +10 dB, 0-200 ms, on/off), Texture (Impact, Density, Early/Mid/Late Diffusion 0-8, Modulation), Damping, Colour, Output EQ |
| Plate Reverb Designer | 11 types: Chrome, Steel, Bright, Dark, Dark Vox, XL, Vocal Focus, Small, Ambience, Classic, Impact. Decay 0.1-10 s, Diffusion, Size, Position, Shape, Width, Modulation, the same Echoes, Output EQ |
| Vocal Shifter | Left and right, Link. Semitone ±12, Formant, Delay to 80 ms, Gain 0 to -40 dB, Precision |
| Vocal Tuner | Global Key, Key and Scale (Major, Minor, Chromatic), Range (Default, Baritone E2-A4, Alto/Tenor C3-F5, Soprano A3-C6), Reference Pitch, Bypass, Correction, Retune, Transition, Tracking; five states per note; MIDI note snap and CCs. Tracking and Range are from V2.12 |
| Vocal Gridder | Global Key, Key and Scale, Bypass, Reference Pitch, Time Tolerance, Pitch Tolerance, Transition; MIDI CCs |
| Dual AutoKey Harmoniser | Manual, Global or Auto key (from a side-chain source, with a confidence meter). 2 voices: Interval ±8, Formant ±12, Gain 0 to -40 dB, Pan. Reverb, Chorus, Pitch and Time Variation, Auto Expand |
| Quad Voice Harmoniser | As above with 4 voices, no AutoKey |
| MIDI Harmoniser | Monophonic with Glide, or Polyphonic with 4 voices. Width, Chorus, Pitch Variation, Use Velocity |
| Amp+Cab Distortion | 5 amps (Deluxe, VX30, MPlex, BDS1, BassVT), 9 cabinets or None. Gain, Output, Thicken, EQ |
| Saturator | Modes A-E (triode, pentode, germanium, bipolar channel strip, tape). Bias, PAD (-30 dB in, +30 dB out), Tilt ±20 dB, LF Cut 20 Hz-1 kHz (12 dB/oct), LF Punch, HF Cut 1-24 kHz (6 or 12 dB/oct), Level -40 to +10 dB |
| Rhythm Delay | Simple: BPM, Dotted/Triplet, Interval, Repeats per bar, Amplitude Shape and amount, Auto Pan, LF Cut, Resonant and HF Cut in the feedback, Feedback, Drive. Advanced: position, gain and pan per repeat, Dry Repeat, Bar Length, Pattern Tap |

## MIDI over TCP

We already hold the newest published document, Protocol V2.0, for firmware V2.0 and later.

- Ports: MixRack 51325 (TLS 51327), Surface 51328 (TLS 51329). TLS starts with a user profile and
  password, answered "AuthOK". Default addresses 192.168.1.70 and .71.
- Base channel N, then offsets by type: inputs N; Groups N+1; Auxes N+2; Matrices N+3; FX sends and
  returns, Mains, DCAs, Mute Groups and UFX sends and returns all N+4, split by note number.
- Mutes by Note On, faders and main assign by NRPN, sends and names by SysEx, preamp gain by Pitch
  Bend, scenes by Bank and Program Change, running status throughout. Get messages exist for
  mute, fader, main assign, sends, preamp gain and PEQ.
- A second reference: Bitfocus Companion's dLive module (MIT), which agrees on ports and channel
  map. It says the Main assign has no Get, but the V2.0 document does list one, so go by the
  document.
- V2.11 added recalling an Action by MIDI. The V2.0 document doesn't mention Actions, and no newer
  version was found.

## Surface

The Getting Started guides and datasheets we hold have the layouts. The guide's Surface Setup
chapter covers Strip Assign, MIDI Strips, Actions, SoftKeys, PAFL, SIP and Metering Ballistics.

## Also found, not on the earlier list

Source Selectors (20, each with up to 20 sources), ABCD inputs, Gain Tracking, Group to Group
routing (V2.11), Mute Group override (V2.11), Direct Out source and follow options, Follow Main
Pan, stereo image modes and width, the PEQ's global "Analogue" frequency ranges (20-200 Hz, 35 Hz-1
kHz, 500 Hz-15 kHz, 2-20 kHz), two 61-band RTAs, MCAs (a chapter of their own).

## Block diagram, firmware V2.0 (DM32U datasheet, page 4)

Compared with the V1.6 diagram in the DM64 datasheet, and with Sidecar's `chain::DLIVE_*`.

- **Input channel:** Width and Mode (stereo only), Trim and Polarity, DEEP preamp, Filters,
  Gate/Exp/Ducker, Insert A, PEQ, Comp/Ducker, Insert B, Delay, AMM gain, Meter, Mute, Fader,
  Assign and Pan. A dashed arrow over the PEQ shows the compressor's move ahead of it. Sidecar's
  chain has the same order; the preamp model, Width and AMM gain would slot in where shown.
- **Mix master and Matrix master:** Ext In summed first (Trim and Polarity; from a socket, I/O Port
  or RackFX), Insert, PEQ, NEQ (GEQ or 12-band), Comp, Delay, Mute, Fader, Balance (Main LR only).
  Sidecar's mix chain has the same order.
- **Mix compressor order, still open:** the diagram draws no pre/post option on mixes and has one on
  inputs, so Sidecar's fixed order agrees with it. The Processing Specs table does list pre/post PEQ
  on mixes, but that table looks copied from the input's (it even says "Inputs" for the frequency
  range). A console would settle it.
- **RackExtra:** send (Mute, Fader), FX, PEQ, Width, return. Takes its feed from an FX Send, any mix
  or an input's Direct Out; goes to an FX Return, an input channel, a channel or mix insert, or the
  output patch.
- **RackUltra:** send (Mute, Fader), PEQ, Comp/Gate, FX, Comp/Gate, PEQ, Width, return.
- **New since V1.6:** the UFX path, Dyn8 ×64 shown as a block, NEQ with the 12-band option, 61-band
  RTAs (31 before), FX Outputs in the output patch, a direct IP to MTX send.
- **AMM, text against diagram:** the guide says its gain element is post-fade and doesn't touch
  pre-fade sends. The diagram draws it after the Delay, ahead of Meter, Mute and Fader, and the
  Post-Delay tap comes after it (the guide's meter option also says "Post Delay (Post AMM)"). The
  key comes from before Insert A, as the guide says. A console would settle where the gain sits.

## Structure, as built (Reference Guide V1.9)

Checked against the V1.9 PDF of the Firmware Reference Guide (Preamp, Ext In, Routing, MixRack Setup, Audio / Source
Select, block diagram); kept outside the repository in `Sidecar-reference/dlive/reference-guide-v1.9/`. It predates Group
to Group and Mute Group override (V2.11).

From the guide: the Main modes and what each does, PAFL busses in Mixer Config, Ext In from Mix and PAFL busses, Mix
Source on Aux, Main, Matrix and FX sends, the Direct Out's eight source points and three follow options, Follow / Invert on
each stereo mix, the stereo image modes and width (ahead of Trim), ABCD and its Actions, Source Selectors (20, each 20
sources, an Aux or Matrix output, on-levels, fades to 10 s, sources not in it unaffected).

Sidecar's reading, because the guide doesn't say: how many of the 64 busses a Main mode takes (one for each signal, Msum
too); the most PAFL busses (4: one per Surface); the pan and divergence curves (equal power); how Ext In from a mix is
delayed (one block, like every signal read back from a later stage); how a 5.0 position spreads (one front-to-rear
control, equal power); the fades' shape (linear in gain); that Invert without Follow copies the mirrored pans once; that
turning ABCD on gives A the channel's patch if it has none.

## Not published anywhere we could find

These need a real console, or stay Sidecar's reading:

- **The Surface's fader taper.** Only the MIDI table is published. A forum thread reports 0 dB on
  the fader as -18 dBFS on AES, which matches our +18 dBu = 0 dBFS.
- **The manual compressors' attack and release ranges.** The specs say only "type dependant".
- **The automatic compressors' ballistics.** The compressor's designer said on the old iLive forum
  that the auto modes don't map back to attack and release settings.
- **Most FX knob ranges and all the algorithms.** Panel pictures show the controls, not the numbers.

## Sources

- [dLive Firmware Reference Guide](https://support.allen-heath.com/hc/en-gb/sections/39786902142609-dLive-Firmware-Reference-Guide):
  [Processing Screen](https://support.allen-heath.com/hc/en-gb/articles/39790636807825),
  [FX](https://support.allen-heath.com/hc/en-gb/articles/39793907886609),
  [Routing](https://support.allen-heath.com/hc/en-gb/articles/39795277687441),
  [MixRack Setup](https://support.allen-heath.com/hc/en-gb/articles/39799812963729),
  [Surface Setup](https://support.allen-heath.com/hc/en-gb/articles/39821854915857),
  [DEEP Processing, RackExtra FX and RackUltra FX](https://support.allen-heath.com/hc/en-gb/articles/39827415078033),
  [Processing specs](https://support.allen-heath.com/hc/en-gb/articles/39827102349457)
- [Firmware Release Notes V2.12](https://support.allen-heath.com/hc/en-gb/articles/42059219284753-dLive-Firmware-Release-Notes-Firmware-Version-2-12)
- [Firmware Release Notes V2.11](https://www.allen-heath.com/content/uploads/2025/07/dLive-Release-Notes-V2.11.pdf)
  (names Group to Group and Mute Group override, without describing them)
- Firmware Reference Guide V1.9 as a PDF, a copy held by a distributor (see `Sidecar-reference/dlive/reference-guide-v1.9/SOURCES.md`)
- [DM32U datasheet](https://www.allen-heath.com/content/uploads/2023/07/DM32U-Datasheet.pdf)
  (kept outside the repository, in `Sidecar-reference/dlive/dm32u/`)
- [Bitfocus Companion dLive module](https://github.com/bitfocus/companion-module-allenheath-dlive)
- Forums: [0 dB and dBFS](https://community.allen-heath.com/?p=112570),
  [compressor types](https://forums.allen-heath.com/t/compressor-type-profile-specifics/890)
