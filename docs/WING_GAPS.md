# WING: what Sidecar has and hasn't, against the manual

An audit of the WING emulation against the WING series user manual, file dated 2025-10-20
(167 pages, from Behringer's product page; kept outside the repository, see
`Sidecar-reference/behringer-wing/SOURCES.md`). Chapters and page numbers are the manual's.
The project follows firmware 3.1, so where a picture in the manual differs from 3.1's screens,
3.1 wins. Written in our own words, November 2026.

Status words:

- **Complete**: there and working as described.
- **Partial**: there, but something is missing (it says what).
- **Missing**: not there, and it could be built without new hardware.
- **Impossible here**: needs hardware a computer doesn't have.

"Sidecar's choice" marks a place where Sidecar differs on purpose.

## The short version

The WING emulation covers nearly the whole manual. Going through every chapter, screen by
screen, found this:

Fixed by this audit:

| Page | What | Before | Now |
| --- | --- | --- | --- |
| 6.1 p. 69 | BALANCE | Cut one side only | Cuts one side and raises the other by the same amount |
| 5.5 p. 50 | REMOTE LOCK | Stored, did nothing | OSC and TCP writes are refused while locked |
| 4.8, CC EDIT | SD RECORDER keys | Greyed out | STOP, PLAY, RECORD, PAUSE, the two toggles, ADD / PREV / NEXT MARKER on the multitrack recorder |
| 4.8 p. 27 | GPIO key on CC EDIT | Lit, did nothing | Dimmed: there are no contacts here |

Still missing, biggest first:

1. **WING RACK** as a console model: its front panel, layers and VIEW behaviour (see 4.2, 4.3, 4.8, 4.9, 5.7).
2. **SD RECORDER's remaining keys**: marker recall (PLAYMARKER, GOMARKER), SELSESSION, previous / next session, the numbered SD-1 SESSION and SD-1 MARKER keys, and the SD encoder modes. The manual doesn't define them; only the WING's parameter tree and WING Edit list them.

Everything else left is hardware (AES50, StageConnect, AES/EBU, the cards and modules, GPIO, the USB link, preamps, lamps), or is Sidecar's choice and listed below.

A correction to the manual: its text lists an INPUTS layer key in the center fader section (p. 15), but its own panel drawing (Fig. 9, p. 14) has no such key. Sidecar follows the panel.

## 1. Introduction

Nothing to emulate.

## 2. Sources, channels, buses and outputs

| Item | Status | Notes |
| --- | --- | --- |
| Source name, colour, icon, tags | Complete | |
| Source gain, mute, polarity, +48V | Complete | Gain is digital; +48V is stored only, there is no preamp |
| Mono / stereo / mid-side sources | Complete | A channel patched to a pair becomes stereo; mid-side is decoded before processing |
| Local In | Complete | The audio interface's inputs (24 tiles, or more for bigger interfaces) |
| Aux In, AES/EBU, AES50 A, StageConnect, USB Audio, Module | Impossible here | Selectable, tiles read n/a |
| AES50 B / C | Partial | Sidecar's choice: they carry interface inputs 49-96 and 97-144; no real AES50 |
| Expansion card | Partial | WLIVE PLAY is the multitrack recorder; Dante, MADI and other cards are impossible here |
| USB Player | Complete | |
| 40 inputs and 8 aux, each with MAIN and ALT input | Complete | |
| 4 stereo mains with 4 pre / post sends per channel | Complete | |
| 16 stereo buses, TAP / POST / GROUP per send | Complete | |
| 8 matrices | Complete | |
| Buses, mains and matrices as sources | Complete | A block late rather than the WING's 38 samples (see 6.6) |
| 2 oscillators | Complete | |
| 24 user signals, stereo or mid-side pairs | Complete | |
| 32 user patches, stereo or mid-side pairs | Complete | |

## 3. Rear panel connections

| Item | Status | Notes |
| --- | --- | --- |
| Analog inputs and outputs, Midas preamps | Impossible here | The audio interface stands in |
| Headphone sockets | Impossible here | PHONES is an output like any other |
| Lamp socket, GLOW, PATCH | Impossible here | Their brightness settings are kept and can go on custom controls |
| 5-pin MIDI | Complete | DIN and USB are this computer's MIDI ports |
| GPIO sockets | Impossible here | |
| Ethernet remote control | Complete | WING Edit 3.3.4 works; WING Copilot hasn't been tried |
| Audio over IP from an internal module | Impossible here | |
| USB-B: 48 x 48 audio interface | Impossible here | Sidecar is itself the application the interface would feed |
| USB-B: MIDI DAW control | Complete | Not tried with real hardware |
| USB-B: firmware, data exchange | Impossible here | Files live in folders on the computer |
| AES50, StageConnect, AES/EBU | Impossible here | |
| WING-LIVE card | Partial | See chapter 8 |
| Other expansion cards | Impossible here | |

## 4. Control surface

### 4.1 Main display

| Item | Status | Notes |
| --- | --- | --- |
| Touch screen and seven screen keys | Complete | |
| Six knobs with touch highlight | Complete | |
| Fine knob, blank key, tap tempo | Complete | |
| Large stereo meter, CLR SOLO | Complete | |
| UTILITY key | Partial | Sidecar's choice: it opens the health and event log, rather than lighting only on screens with a function |

### 4.2 Fader sections and layers

| Item | Status | Notes |
| --- | --- | --- |
| WING: 12 + 8 + 4 faders and their layer keys | Complete | Follows the panel, not the page 15 list |
| Extra layer pages, shift keys, hold to jump | Complete | |
| User layers: channels, sends, FX parameters, MIDI CCs | Complete | |
| WING COMPACT: fader section, 16 keys, main fader, LCD | Complete | |
| WING RACK: front panel, encoders, four-channel LCD, pages | **Missing** | Largest gap in the audit; no new hardware needed |

### 4.3 VIEW keys

| Item | Status | Notes |
| --- | --- | --- |
| WING table (each section, hold for CONFIG, USB, channel strip) | Complete | |
| WING COMPACT table | Complete | |
| WING RACK table | **Missing** | With the RACK model |

### 4.4 Scribble strips, meters, SELECT

| Item | Status | Notes |
| --- | --- | --- |
| Scribble strip, colour bar | Complete | |
| Second SELECT press (SEL DBL CLICK) | Complete | |
| SOLO, MUTE | Complete | |
| Level meters, DYN and GATE LEDs | Complete | |
| RACK's LCD | **Missing** | With the RACK model |

### 4.5 Channel strip section

| Item | Status | Notes |
| --- | --- | --- |
| LED panel, small screen | Complete | |
| Slot keys and encoders: show, next parameter, hold for on / off | Complete | |
| EQ section: bands, SHIFT, GAIN / WIDTH / FREQ | Complete | |
| LISTEN | Complete | |
| BLEND / MIX, up to 125 % | Complete | |

### 4.6 4-Channel section

| Item | Status | Notes |
| --- | --- | --- |
| GAIN / 48V, GATE, COMP, FILTER, PAN / MAIN | Complete | +48V switches on a 2 s hold |
| F1-F3 sends, hold to choose the bus | Complete | |
| F1-F3 as custom controls | Complete | |

### 4.7 USB and monitoring

| Item | Status | Notes |
| --- | --- | --- |
| USB-A socket | Partial | Sidecar's choice: a folder of your choice stands in; no charging |
| PHONES, MONITOR and TALK LEVEL | Complete | |
| DIM (hold: mute), MONO (hold: swap), blinking | Complete | |
| TALK A / B | Complete | |

### 4.8 Custom controls

| Item | Status | Notes |
| --- | --- | --- |
| Layers 1-16 with 4 encoders and 8 keys | Complete | |
| U1-U4, M/M, D1-D4 | Complete | |
| Names, colours, `*` value and `\|` inverted name | Complete | |
| Hold a key and turn the encoder above it | Complete | |
| Lower layers: 2-TRACK, MULTI, MUTE GROUPS, SHOW CONTROL, USER | Complete | |
| Function lists | Partial | All of them act except GPIO; SD RECORDER has nine of its fourteen keys, the rest and the SD session / marker keys and encoders aren't built |
| RACK's CUST TRANS layer | **Missing** | With the RACK model |
| DAW control | Complete | MCU and HUI, not tried with real hardware |
| GPIO page, TGL / IN / OUT | Impossible here | The key is dimmed |

### 4.9 Sends on Fader

| Item | Status | Notes |
| --- | --- | --- |
| WING: standard and ALTERNATIVE SOF | Complete | |
| WING COMPACT | Complete | |
| WING RACK | **Missing** | With the RACK model |

## 5. Main screens

### 5.1 HOME

| Item | Status | Notes |
| --- | --- | --- |
| Channel Home for each strip type, Channel Select | Complete | |
| ICON / COLOR, light bar, LINK CUSTOMIZATION, NONE / ALL, COPY CUSTOMIZATION | Complete | |
| NAME with suggestions | Complete | |
| TAGS: DCAs, mute groups, TALK ON / TALK SOLO for A, B or both | Complete | |
| INIT and COPY, with scope and destinations | Complete | |
| RTA SETTINGS and EQ/RTA SETTINGS | Complete | |
| Shelves to parametric bands | Complete | |
| Feed grid and FEED CONFIGURATION | Complete | |

### 5.2 EFFECTS

| Item | Status | Notes |
| --- | --- | --- |
| 16 slots, premium and standard | Complete | Up to 64 slots is Sidecar's addition |
| FX1-8, STD and CH categories | Complete | |
| Slot meters | Complete | |

### 5.3 METERS

Complete.

### 5.4 ROUTING

| Item | Status | Notes |
| --- | --- | --- |
| CHANNELS: MAIN / ALT, +1 AUTO, lock, source group menu, COPY CUSTOMIZATION | Complete | |
| SOURCES: properties, multi-select, rename, icon / colour, tags | Complete | |
| REMOTE and RCV CUST | Impossible here | Need AES50 |
| OUTPUTS: any source to any output | Complete | |
| MONITOR, WLIVE REC, RECORDER groups | Partial | RECORDER and MONITOR are there; the multitrack recorder takes its sources per track rather than from a WLIVE REC output group, Sidecar's choice |
| Other output groups | Impossible here | Selectable, nothing behind them |

### 5.5 SETUP

**General**

| Item | Status | Notes |
| --- | --- | --- |
| INITIALIZE CONSOLE with scope | Complete | |
| SHUTDOWN | Complete | Quits the application |
| CONSOLE OPTIONS | Impossible here | Registers internal modules |
| Brightness, contrast | Complete | |
| Lamps | Partial | Stored; there is nothing to light |
| Time and date formats | Partial | Sidecar shows the computer's clock; the clock isn't set here |
| Console name | Complete | |
| UPDATE, WING OS, WING DATA | Impossible here | There's a data folder instead |
| CONFIRM LIBRARY LOAD, SHOW METER PAGE WHEN LOCKED, ALWAYS SHOW ACTIVE SCENE | Complete | |
| USE CRSR/WHEEL, TOUCH FADER SELECT, TOUCH FADER RES | Complete | |
| MOUSE DISABLES TOUCH, MOUSE SPEED | Impossible here | Kept as settings; the computer's pointer settings apply |
| USB HOST SPEED | Complete | FS limits the USB recorder |
| NETWORK CONFIG | Impossible here | Needs the AoIP module |

**Audio**

| Item | Status | Notes |
| --- | --- | --- |
| Clock rate | Complete | Set through the audio device |
| Sync source | Impossible here | The interface's own business |
| INPUT SELECT and its window | Complete | |
| MAIN LINK, DCA MUTEGROUPS, MUTEGROUP/SIP OVERRIDE, STARTUP MAIN MUTE, GLOBAL INPUT SELECT OVERRIDE | Complete | |
| SOURCE SOLO, TALKBACK | Complete | |
| SOLO SYSTEM: LIVE, STUDIO, SIP, PFL / AFL, solo preferences | Complete | |
| USB AUDIO channel count | Impossible here | The AUDIO DEVICE panel under SIDECAR takes its place |
| AUTOMIX enables | Complete | |
| AES50 PORTS error counters | Impossible here | Sidecar's ERRORS panel shows the interface's dropouts in the same place |

**Surface**

Complete, every METERING, GENERAL and FADERS row of the full-size WING and the COMPACT. The RACK's rows wait for the RACK model. The manual's own pictures show the COMPACT's rows (Fig. 40).

**Remote**

| Item | Status | Notes |
| --- | --- | --- |
| MIDI REMOTE CONTROL rows and their tables | Complete | |
| HA REMOTE | Impossible here | The keys are kept |
| NETWORK | Partial | Sidecar's choice: shows what this computer has; the addresses aren't set here |
| REMOTE LOCK | Complete | Fixed by this audit. A locked protocol can read and meter; writes are refused (Sidecar's reading of "block") |
| WING Edit, Copilot, Q | Partial | WING Edit works; Copilot and Q on real devices have not been tried |

**DAW**

Complete: DAW ENABLE, CONNECTION, EMULATION, USE UPPER CC FOR DAW, DISABLE WHEEL DURING PLAY, CONFIGURATION, PRESET and LOAD.

### 5.6 LIBRARY

| Item | Status | Notes |
| --- | --- | --- |
| INT, USB and SHOW file lists, tabs, greying by type | Complete | |
| STARTUP files, bypass | Complete | Hold SHIFT rather than LIBRARY |
| SHOW tab and its keys | Complete | |
| Scene tags #1-#128, SEND MIDI with D: and U: | Complete | |
| Snapshots, FX and CHAN presets, clips as scenes | Complete | What FX and CHAN scenes load into is still an open question for the owner |
| SNAP: SAVE, SAVE + SCOPE, UPDATE, PARTIAL UPDATE, EDIT SCOPE, LOAD, PARTIAL LOAD, NO GLOB, RESTORE | Complete | |
| SCOPE window | Complete | |
| GLOBAL safes | Complete | |
| SNIP: all keys, focus, counters | Complete | |
| CLIP | Complete | |
| FX | Complete | |
| CHAN and UPDATE SCOPE | Complete | |
| Scene file format | Partial | Sidecar's `.lsnap` / `.lsnip`; WING `.snap` / `.show` import, export not yet tried on a console |

### 5.7 OVERVIEW, 5.8 FADERS, 5.9 SENDS, 5.10 CONFIG, 5.11 MUTE GROUPS

| Item | Status | Notes |
| --- | --- | --- |
| Full-size WING and COMPACT layouts, including VIRTUAL | Complete | |
| RACK and virtual sections on the RACK | **Missing** | With the RACK model |
| FADERS: gate / comp meters, AUTO RIDER square, group assignments | Complete | |
| SENDS | Complete | |
| CONFIG: all item types, drag, clear, rename, customisation choice | Complete | |
| MUTE GROUPS | Complete | |

## 6. Channel processing

| Item | Status | Notes |
| --- | --- | --- |
| 24 process orders, 8 TAP points | Complete | |
| Input: MAIN / ALT, INDIVIDUAL / GLOBAL | Complete | |
| TRIM | Complete | |
| BALANCE | Complete | Fixed by this audit |
| INVERT, meter | Complete | |
| FILTER, tool filters, slopes | Complete | |
| DELAY: post-fader, four units | Complete | |
| GATE slot, every model | Complete | |
| ACCENT, KEY SOURCE, KEY FILTER, KEY SOLO, KEY TAP | Complete | |
| EQ slot, every model, FREEZE, MIX, LO CUT / HI CUT, FILTER, BAND SOLO | Complete | |
| COMP slot: GAIN, XOVER, DEPTH, FREQ, XOVER SOLO, AUTO ENV, DETECTOR | Complete | |
| INS 1 / INS 2, FX PROCESSOR, FX TYPE, RESET, FX SPILL, SET CUST | Complete | |
| AUTOMIX X / Y | Complete | Sidecar's own gain sharing, to the manual's description |
| MAIN SENDS, width -150 to +150 %, vector scope | Complete | |
| BUS / MATRIX SENDS, mode popup, IGNORE CHANNEL MUTE, PAN LINK, TAP WIDTH, TAP EQ | Complete | |
| Aux channels | Complete | |
| Bus channels | Complete | |
| Matrix channels, DIRECT INPUT | Partial | The AES/EBU choice is silent, there is no such input |
| Main channels | Complete | |

### 6.6 Latency

Partial, Sidecar's choice. Inserts add no latency and bus-to-bus, bus-as-source and similar back paths are a block late rather than the WING's 8, 32 or 38 samples. FX and plugin latency is compensated instead (see the user guide).

## 7. Monitors

Complete: TALK A / B, the three paths, DIM, MUTE, MONO, LR SWAP, SWITCH MODE, AUTO DIM, the TALK TO grids, USE SEND LEVELS, SOURCE, SRC LEVEL, SRC MIX, DIM, PFL DIM, BAND SOLO, DIRECT IN, EQ with PAN and WIDTH, INVERT / DELAY, LIMITER and the level fader.

## 8. WING-LIVE and the USB recorder

### 8.1 WING-LIVE

| Item | Status | Notes |
| --- | --- | --- |
| Recording and playing 64 tracks | Complete | Through Sidecar's multitrack recorder |
| Two SD cards, LINK CARDS, 8 / 16 / 32 channels per card, FORMAT | Partial | Sidecar's choice: one recorder with its own project folders; no cards to format or link |
| Sessions, markers, UPDATE | Complete | |
| PLAY, A->B, LOOP, knobs | Complete | |
| SHOW METERS, AUTO INPUT SELECT, STOP / PLAY / RECORD INPUT | Complete | |
| Reading and splitting card sessions | Complete | Imported by the editor |
| Battery backup | Impossible here | Projects autosave |

### 8.2 USB recorder

Complete: CREATE PLAYLIST, ADD, ADD ALL, REPEAT, REMOVE, SKIP, STOP and its hold, UP / DOWN, SAVE / CLOSE / OPEN, REC PATH, REC, PAUSE, STOP, NEW FILE, 2 or 4 tracks at 16 or 24 bit, cursor drag and previous / next. A folder on the computer stands in for the stick.

## 9. Firmware update

Impossible here.

## 10. Shortcuts

| Item | Status | Notes |
| --- | --- | --- |
| Touch off and on | Complete | |
| Reset touch panel, ghost click test, emergency boot, surface LED test | Impossible here | |
| Restart after SHUTDOWN | Impossible here | Quit and start the application again |
| Surface lock, with key password | Complete | |
| Initialize at power-up | Partial | Sidecar's safe mode covers a bad show; SETUP > INITIALIZE CONSOLE does the rest |
| Screenshot | Complete | |
| Bypass STARTUP files | Complete | Hold SHIFT |
| Optional hardware setup | Impossible here | |
| Display brightness with the knob | Complete | |

## 11. Internal effects

Complete: every heading in the chapter has its model, in the GATE, EQ and COMP slots and the FX rack, drawn on faceplates. Processing is recreated from the published parameters rather than modelled from circuits, as the user guide says. VSS3's preset list is still an open question for the owner. The owner settled one question: a bypassed reverb or delay's tail cuts.

## 12. Specifications

Don't apply: they describe the hardware.

## Outside the manual

From the project's known limits, not from the manual: the WING network protocols answer every parameter of the 3.1 tree but one, 93.2 % of them driving the engine or the settings and the rest stored; WING Edit sees FX 1-16 only and doesn't see the direct outs. Open questions for the owner are in `UNIVERSAL_MIXER.md`, "Still open from the WING work".
