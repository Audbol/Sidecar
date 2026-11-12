# What's left to do

The working list, in the order we plan to do it. Written November 2026; cross things off as
they land. The detail behind each console is in `UNIVERSAL_MIXER.md` and `DLIVE_RESEARCH.md`.

## 1. Save and try what exists

- [ ] Commit the 01V, dLive and fast-build work. It's all uncommitted on one PC.
- [x] XENYX Q1202USB: blocks, routing, panel, console tape and scenes (first analog mixer). Still to do: measure a real mixer (compressor law, EQ widths, soft clip, fader law); the FX SEND and CTRL ROOM jack outputs on the panel.
- [ ] Push the commits. Pushing needs a sign-in as Audbol on this PC.
- [ ] Try test.6 by hand: the 01V's surface first, then the dLive.

## 2. Finish the 01V (while the console is on hand)

- [ ] LCD photos of the remaining pages.
- [x] MEMORY's scene page, RECALL UNDO (the engine's recall undo: `Show::undoRecall`).
- [x] Pages turn with a repeated key press. EFFECT edit, library and PRE/POST, AUX PRE/POST.
- [x] UTILITY: the oscillator (on the engine, with burst noise and bus priority), preferences.
- [x] OPTION page 2: CH 17-24's routing, sends with PRE/POST, attenuator and 2-band EQ.
- [x] MIDI's pages: SETUP, PROGRAM CHANGE, CONTROL CHANGE, BULK, LOCAL CONTROL (its OFF isn't built).
- [x] Pages: SETUP's five, OPTION's five, PAN/ROUT's bus masters and OMNI OUT, OUTPUT DELAY, the libraries' STORE and
      TITLE EDIT, the rest of the preferences. Fader and [ON] groups, pairs, REMOTE page 1, 2TR IN, CH 13/14's FLIP.
- [ ] Still open: the EQ and dynamics libraries' 40 presets, REMOTE's MMC and USER DEFINE, STEREO CASCADE, several monitor
      sources at once, the OMNI OUTs' delays, the dynamics on REMOTE, CH COPY.
- [x] MIDI: parameter changes, control changes, program changes, bulk (scene memories, setup, the
      two tables, a channel's current EQ and dynamics, an effect). Sidecar's bytes inside the dumps.
      Not yet: the EQ and dynamics libraries' MIDI (no such libraries on the 01V yet), MMC, Note On for
      effects, the fader and LCD remotes, cascade links and LOCAL CONTROL off.

## 3. Finish the dLive

Structure:

- [x] Mixer Config and IP Stereo Config.
- [x] Each mix's Ext In and Mix Source (Bus mixes, from sockets).
- [x] Main modes: LR+Msum, LR+M, LCR, LCR+, 5.1, None; PAFL busses counted in Mixer Config. The engine
      side: the screens don't offer them yet (FFI: `ld_show_apply_dlive_structure`).
- [x] Ext In from mixes and PAFL. Mix Source on the Matrices and Main.

Processing:

- [x] NEQ-12 (the engine's EQ parameter limit grew to 64).
- [x] Dyn8's DSP and model (`DLVDYN8`, `dlive_fx`), on Universal.
- [x] A dLive's 64 Dyn8 engines in the signal path (engine side, `livedaw_engine::ufx`): an array of their own,
      an insert names Dyn8 n as FX `64 + n`; each with a key source; in shows and scenes.
- [ ] Screens for the Dyn8 engines: picking one on an insert, its panel and key source (FFI: `ld_fx_params`,
      `ld_fx_set_key`, `ld_dyn8_in_use`; an insert's `fx` = `kDyn8FxBase + n`).
- [x] The two preamp models.
- [x] AMM, NOM and D-Classic.

Effects:

- [x] The 20 RackExtra effects. Ranges and algorithms are largely Sidecar's reading.
- [x] The RackUltra models and channel strip DSP (`dlive_fx/ultra`), the 11 models on Universal.
- [x] A dLive's 8 UFX engines in the engine: FX 17-24 (a dLive has 24 FX now), their strip around the device, sends and
      returns (24 FX returns), the Global Key, MIDI notes and key sources; in shows and scenes.
- [ ] Screens for the UFX strip, the key source and the Global Key (FFI: `ld_ufx_strip_*`, model table 5,
      `ld_fx_set_key`, `ld_engine_set_global_key`), and MIDI notes fed from the MIDI inputs (`ld_fx_midi_note`).
- [ ] The RackUltra card's 8 extra stereo send mixes and 8 return channels as strips of their own: for now a UFX
      engine is fed from any mix and returns on any channel or FX return.

Console features:

- [x] MIDI over TCP (`livedaw-ahmidi`): TCP 51325 (MixRack) and 51328 (Surface), the protocol's whole message set,
      replies and state feedback. Not done: its TLS ports (51327, 51329), Surface Cue Recall and Go/Next/Previous, the pad,
      UFX, Mains 2-6, MIDI Strips.
- [x] Engine and shows: Source Selectors, ABCD inputs, Direct Out options, Group to Group's source point,
      Follow Main Pan, stereo image modes.
- [x] Their screens (the guide's pictures; not yet seen against a console): the Processing screen's Ext In tab on a mix, ABCD
      and Stereo Image on Preamp, a Routing tab (a channel's Direct Out and the console's source point and follows; a mix's
      Mix Source and Follow Main Pan), MixRack > Audio > Source Selector live, the MIDI channel and the MIDI over TCP
      connections on MixRack > Network (the console has them on Utility > Control > MIDI). FFI getters for the selectors
      (`ld_selector_output`, `_source`, `_fades`).
- [ ] Still without a screen: Group to Group's source point, a channel's 5.1 panning (panning mode, the 5.0 picture, Divergence,
      LFE and Non-LFE levels), SigGen and Talkback on the Ext In tab, a Source Selector member's own name and colour, the
      Follow and ABCD settings behind a held Setup key.
- [x] The Main mode's strips on the surfaces: a C1500's mix layer ends on LR and makes room beside it for M or C, LFE and
      Ls/Rs; a C3500's last layer has the four. Strip Assign shows the same.
- [ ] Mute Group override: no public description of how it works (firmware V2.11).
- [ ] Group to Group's added delay of 4 or 8 samples, and which source point has which.
- [x] Its own surface, started from photos of a C1500. Its keys and rotaries work; its screen hosts
      the Universal pages.
- [x] The C3500's own surface, the default for a dLive, from photos of a real one: 24 strips in two
      banks, two touchscreens, soft rotaries. The C1500 stays, a key on each switches between them.
- [x] Copy, Paste, Reset, Safes, Freeze and the GEQ on the faders, on both surfaces. The held keys latch.
- [x] The left screen's PEQ page and the right screen's Scene Manager, matched to photos.
- [x] MixRack's Config and Audio tabs, from photos. Dimmed where Sidecar has no counterpart: Network's IP settings,
      User Profiles, Audio Sync.
- [x] The Meters page and its RTAs, from photos; the four PEQ bands on View. Sono, LF Damp, Dual View, the
      ballistics knobs and the User pages' Setup aren't built, and UFX stays idle until RackUltra.
- [ ] MixRack's Talkback, Mute Groups, SigGen and Controllers tabs, once photographed.
- [x] The C1500's screen in the same style: Processing, Scenes, Meters, MixRack and I/O in one frame, from photos. FX, Routing,
      Ganging, Util, Setup and Listen still fill the screen with the Universal pages.
- [x] SoftKeys and the custom rotaries assignable (Setup lit and the key, or the Surface page), Strip Assign, Help, the right
      screen's View, the Scene Manager's Lock and Copy/Paste. Strip Assign and the assignments live in Sidecar's settings, per
      surface, until the show file can hold them; Lock lasts while the show is open.
- [ ] The other processing tabs against photos (Gate, Comp, Preamp, Filter, Ins, Delay, Bank, Overview).
      They work, but no photo of them exists yet.
- [ ] The right screen's other pages in the console's own style; they host the Universal pages for now.
- [x] Scenes' other tabs and the I/O page from photos. Update stores into chosen scenes; Global Safes works by
      whole rows and columns; Outputs and Inputs patch for real.
- [ ] dLive routing and I/O grids: click and drag the page to move around and reach the whole matrix (owner).
- [ ] Scene Manager: Undo Go works (`Show::undoRecall`); still to do: Lock, Copy and
      Paste, Copy Filter, Embedded Recall, Custom MIDI, Actions. Cue lists, Import from a show, Tie Lines
      and Virtual SoundCheck are drawn only; Update's Auto Tracking and Relative too.
- [ ] The RTA on the PEQ graph. Help.
- [ ] The rest of the surface: SoftKeys, assigning the custom rotaries, Strip Assign.

## 4. Bring the older surfaces up to the 01V's standard

- [x] ProMix 01's panel and 240 x 64 dot LCD, from the owner's photos. Its LCD shares the 01V's fonts.
- [x] ProMix 01: METER, PAN, COMP and EQ dot for dot from the owner's LCD photos; panel colours sampled.
- [ ] ProMix 01: check the other LCD pages against the owner's console.
- [x] 02R's colours, plates and hood, from public photos (no console to photograph).
- [ ] 02R: its LCD as dots, once its resolution is known (the manuals and reviews don't say).

- [x] An accuracy pass over the 01V, ProMix 01, 02R, SQ-5 and X32, against photos and manuals.
- [ ] From that pass, still open: the 02R's LCD is about 2:1 where the real one is 4:3; the SQ-5's knobs should be
      fluted; 01V D routing, group boxes and KEYIN are drawn only.

## 5. The Universal Mixer's interface

The owner wants a lot of work done here; what comes first is theirs to say.

- [x] A first pass over every view: keys that stayed dark, collisions, the inspector's and the block editor's layout; the
      first main pinned, sends on fader, DCA spill, the show's keys beside the view keys, scene SKIP / LINK / tag / fade / notes,
      PATCH 1:1, strip copy and paste; CONTROLS (the custom controls, played here; CC EDIT embedded).
- [x] Second pass: scene scope and recall safes, a snippet's items, banks and JUMP, FX bypass on the tiles, ROUTING's levels,
      crosshair and range drag, light theme and MENU. Left: recorder and CC EDIT in this theme, LIBRARY / RECORDER / SETUP spacing.
- [x] Universal Mixer: a design pass so it looks and works like a professional tool (owner, November 2026): consistent type, spacing and colour across every view, polished meters, faders and keys, and every feature you would expect.
- [x] Any console's blocks on any console (`ProfileSpec::run_models`, `UNIVERSAL_MIXER.md` "Foreign models"): lists, tags, shows, remote-mode guard. Still to do: a surface's own pages for a foreign block (today inert or by parameter id), foreign models on aux channels' dynamics lists, and trying a swapped show on a real X32 (remote control hides them).
- [x] A built-in benchmark (owner, November 2026): measure this computer at the chosen sample rate and buffer size (DSP cost per block and per console, how many channels of each console's processing fit, effects, plug-ins, multitrack recording to disk, the interface's frame rate) and report it plainly with recommendations (buffer size, channel counts, what to switch off). Very thorough, and shows everything as it runs (owner): each test live with its progress, the measured times and loads, a log, and the full results kept and exportable.
- [x] A plug-in block type (owner, November 2026): a block in any strip's chain that hosts a VST3, AU or LV2 plug-in (or a chain of them) directly, without an FX slot: its editor, bypass, latency compensation, the sandbox option, saved with shows and scenes, and a card in the Universal Mixer.
- [ ] Universal Mixer user-defined layers (owner, November 2026; big; steps one and two done: the show's user layers (WING USER layers, coloured, exported and imported as .sidecarlayers) with header keys, an editor and drag editing in the Universal Mixer, and the SQ-5's layers A-F and the dLive C1500/C3500 layers shown and edited as the same layers, each layer marking its console bank; the X32, ProMix, 02R, 01V, PRO2, Q1202USB and DM2000 have no user layers; left: a layer editor on a touchscreen pass, a check against the real consoles): any strips in any order, a strip may sit in several layers, named and coloured, spacers; header keys to switch; drag to edit; saved with the show, with export and import between shows; the same layers as every console surface's user or custom layers (WING, X32, dLive, SQ-5, DM2000 remote layers and the others).
- [ ] Look at it again in the renders after that pass; then the list in `UNIVERSAL_MIXER.md`, "Interface pass".

## 6. The other consoles

- [x] SQ-5: MIDI over TCP (`livedaw-ahmidi`, TCP 51325), its Get messages.
- [x] SQ-5: Ganging, Network, Users, Digital I/O, I/O Port, Footswitch, Mix Stereo, Data, USB Utility, SQ-Drive, Calibration and the
      Soft Keys' SQ-Drive types are built; what stays inert is listed in `UNIVERSAL_MIXER.md`. To check in renders.
- [ ] WING: Mixing Station write coverage, the owner's open questions
      (`UNIVERSAL_MIXER.md`, "Still open from the WING work").
- [x] WING: page-by-page audit against the manual (`WING_GAPS.md`); BALANCE, REMOTE LOCK and the SD RECORDER keys fixed.
- [x] WING RACK as a console model: its front panel (the display and the four-channel section), its five layers, the mute
      groups, custom controls and transports on its eight keys, VIEW and HOME as the manual's RACK tables, `wing-rack` to remote clients.
- [ ] WING RACK, still open: look at it in a render (never built or drawn yet); the FADERS screen's left toolbar (manual 5.1
      p. 30, Fig. 72); the RACK's key combinations of chapter 10 (UTILITY, SETUP and CLR SOLO stand in for the blank key);
      `/$ctl/layer/RCK` for remote clients; its completion figure in `src/ui/StartupNotice.cpp`.
- [ ] X32 display menus, still open (drawn dimmed for now): SETUP's system controls, sync, time; the DAW protocol
      of SETUP > remote; LIBRARY's .efx / .rou import and export; USB's recorder trim. The Monitor Source list's LR+C/M
      and PFL / AFL act as LR. Kept for remotes only (`/-prefs`): lamp, card mode, network fields, iQ, most of config.
- [x] X32: measured corrections recovered from the original session (real X32 Compact, firmware 4.09), with tools and tables in `tools/x32`.

## 7. Release

- [x] 01V and DM2000: fader groups, mute groups and pairs save with scenes (owner checked a real 01V: they do).
- [x] First run: the start-up window asks which console (it already does: with nothing to continue, a console must be chosen).
- [ ] Analog mixers: reuse the Q1202USB's console tape and scene bar as the template.
- [ ] Consoles: controls Sidecar cannot back stay drawn but dim.
- [ ] Regenerate the README screenshots after the Universal Mixer design pass, before v0.3.0.
- [ ] With every update: review each console's completion percentage in `src/ui/StartupNotice.cpp`.
- [ ] v0.3.0, once the above is as far as we want it for that release.
      Release notes: brief, an overview of what changed and what was added (owner).
- [ ] 0.3.1: the plug-in host console, planned in `docs/PLUGIN_HOST.md`.
- [ ] Raspberry Pi: build the Linux arm64 package on Debian Bookworm (a container on the arm runner), so it
      runs on Raspberry Pi OS Bookworm. 0.3.0's needs glibc 2.38, GLIBCXX 3.4.32 and ALSA 1.2.10 (a user's
      Pi 5 could not start it); Raspberry Pi OS Trixie has them.

## 8. Consoles after these

Research is in `NEW_CONSOLES_RESEARCH.md`; documents and photos are outside the repository.

- [x] Yamaha DM2000: selectable (SETUP > SIDECAR > INTERFACE), on its own surface.
- [ ] Yamaha DM2000: its surface and LCD pages, its MIDI.
  - [x] LCD: MIDI SETUP, PGM and CTL ASGN, OUTPUT PATCH, Direct Outs, PAIR, the libraries, PREFER1-2, UTILITY, DIO, User Defined Keys, REMOTE (drawn); COPY/PASTE; the printed fader legends.
  - [x] Its processing blocks and profile (`dm2000`, `Profile::Dm2000`), the manual's order of
        processing, ranges and steps, on Universal strips as well.
  - [x] Its stereo effect types on the 01V's devices, ST REVERB, M.BAND DYNA.
  - [x] Its surface, the 320 x 240 dot LCD and its pages: EQ EDIT, ATT, GATE and COMP EDIT, the
        Insert page with POSITION (the twelve process orders), PHASE, DELAY in samples, PAN, ROUTING,
        BUS TO ST, AUX and MATRIX SEND, ENCODER ASSIGN, PARAMETER VIEW, FADER VIEW, METER, the fader and
        mute groups, GROUP MASTER, FX EDIT and TYPE, GEQ EDIT (six units), SCENE MEMORY, INPUT PATCH,
        MONITOR (`UNIVERSAL_MIXER.md`).
  - [ ] Its other pages: the libraries, OUTPUT PATCH, PAIR, MIDI, REMOTE, SETUP, UTILITY, Automix,
        Locator, Track Arming, User Defined Keys; the plug-in slots. A MIDI SETUP page (its transmit is off by default).
  - [x] Its MIDI (`livedaw-dm2000`, from Yamaha's V2 MIDI specification): parameter changes and requests, Control and
        Program Changes, Key Remote, meter data, MMC out. Not done: bulk dumps, NRPN numbers and effect parameter tables (unpublished).
  - [x] Automix (fader and ON takes against a clock or the scene), Track Arming and Locator on the multitrack recorder, Data on shows, monitor source keys, PLUG-INS, CHANNEL INSERTS, Output Insert In patch.
  - [ ] Still inert: PAN LINK/GRAB/EFFECT, surround, VERTICAL pair, DELAY MIX/FB, PRE EQ direct outs, 2TR monitor sources, Automix touch/relative/auto-rec, Bulk.
  - [ ] Surround-format effects and monitoring. VCM Add-On effects: proprietary, not planned.
- [ ] Midas PRO series: the PRO2 first (`midas_pro`, `Profile::MidasPro2`).
  - [x] Its processing blocks and profile, in the Normal (FOH) mode's state, on Universal strips as well.
  - [x] Its effects but SQ1 Dynamics (Delay, Virtual DN780, Flanger, Phaser, Pitch Shifter, 3-Band Compressor, Dynamic EQ).
  - [x] The C++ side of the profile and its surface (`Pro2Surface`, `Pro2Screen`), first pass.
  - [ ] MCA groups, the direct output, the safes but AUTO, the aux send modes, stereo-linked sends and matrices, SIS panning.
- [ ] DM2000: accuracy pass against the owner's photos (`photos-owner` in the reference folder), and its meter bridge.
- [x] Remote control, X32: tried on a real X32 by the owner (test.10, November 2026) and works. Still to do: meters from the console, the 01V and others. Done in code (livedaw-x32 client, RemoteSession, CONNECT keys; tested against Sidecar's own server only; meters from /meters and a check against a real console still to do): Later (owner: for sure): Universal Mixer as a remote for real consoles: audio off, the profile loaded, moves sent to the console and its changes shown, full state read on connect, a clear connected state. Start with the 01V (owner has one), then the X32 and WING, the MIDI Yamahas, partial SQ-5 and dLive. Not the PRO2 (no published protocol) or analog mixers.
- [ ] Analog mixers: Sidecar will branch out to cover them as well.
- [ ] Then, as wanted: Midas M32, Yamaha CL, QL, TF and DM7, Allen & Heath Avantis, DiGiCo SD and
      Quantum.

## Only a real console can settle

- dLive: its Surface's fader taper, whether a mix's compressor can go before its PEQ, where AMM's
  gain sits, its manual compressors' attack and release ranges, most effect knob ranges.
- 01V: PAD's exact behaviour in Sidecar's gain structure, the levels of the fader steps.
