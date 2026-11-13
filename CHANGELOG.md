# Changelog

All notable changes to Sidecar are listed here. Versions follow
[semantic versioning](https://semver.org); 0.x releases are betas.

## [Unreleased]

- PLUGIN HOST (fifth step): the CONTROL page, hot knobs and MIDI learn. Each plug-in has up to two hot parameters; a channel strip's two knobs (endless, with LED rings showing the value, the LCD naming the parameter while touched) control those of the channel's focused plug-in: the one selected on the RACK page while that channel is shown, else the first in the chain that has any. ASSIGN HOT 1 and 2 in the RACK page's dock take the next parameter touched in the plug-in's own editor or its generic controls; the CONTROL page's HOT KNOBS panel offers a parameter list per plug-in. MIDI learn: LEARN, then tap or touch a plug-in parameter, a hot knob, a channel's SELECT or BYPASS or a scene key, then move a MIDI control (a controller, absolute or relative for an endless encoder, or a note that toggles). MAPPINGS lists them with DELETE and CLEAR ALL (asks twice); MIDI DEVICES chooses the inputs. A plug-in's hot knobs and maps are saved with the plug-in (so with the show, and they follow it when the rack is reordered and go with it, with a note, when it is removed), the others with the show; scene recalls leave them alone. Plug-ins in the sandbox are mapped through their mirrored parameters.
- PLUGIN HOST (fourth step): the SCENES page and whole-rack scenes. A scene recalls, per channel, each plug-in's whole state, its bypass, the channel's BYPASS, the rack's order and which plug-ins are there (one the scene lacks is taken out; one it has is made again from its description), the sidechain keys and the input and output patch. Every channel and every plug-in has a SAFE key: a safe channel is left alone altogether, a safe plug-in keeps its state, bypass and place and stays even if the scene lacks it; safes are saved with the show. Each scene has a fade mode and time: SNAP (the default) applies plug-in states at once, GLIDE moves the plug-ins' continuous parameters to the scene's values over the fade time and then applies the whole state (stepped parameters, and plug-ins in the sandbox, snap at the start). The page lists the scenes (number, name, mode, time, the active and the cued one) with STORE NEW, OVERWRITE and DELETE (each asks twice), RENAME on the on-screen keyboard, MOVE UP and DOWN, and follows the surface's PREV, GO and NEXT keys. PLUGIN HOST scenes also hold the patch.
- PLUGIN HOST (third step): the ROUTING page and sidechain keys. INPUTS and OUTPUTS are patch grids like the Q1202USB's (interface inputs across and channels down; channels across and interface outputs down), sized for 16, 32 or 64 channels and scrolling; a tap patches, one input per channel, and RESET 1:1 asks to be pressed twice. KEYS lists the selected channel's plug-ins that have a sidechain input and keys each from any channel's input (before its plug-ins) or output (after them); a channel cannot key itself after its own plug-ins. The key is chosen per plug-in, saved with the show and the scenes, and handed to the plug-in's sidechain bus by the engine, a block late only when the key channel runs after the keyed one. Plug-ins in the sandbox have no sidechain input and so no key.
- PLUGIN HOST (second step): its own surface, a dark rack unit drawn at 1920 x 1080 and scaled to the window: a touchscreen across the top (F10 or a double-click enlarges it) with page tabs (RACK, OVERVIEW, ROUTING, SCENES, CONTROL, BROWSER, SYSTEM; only RACK is built so far), eight channel strips under it (SELECT, name, input and gain-reduction meters, BYPASS, two hot knobs not yet assigned), BANK keys through 16, 32 or 64 channels, a data wheel, SCENE keys (PREV and NEXT cue, GO recalls) and a readout of the selected channel's latency and the engine's CPU. The RACK page shows the selected channel's chain as large tiles in signal order (name, maker, latency, BYPASS): drag to reorder, a long press for the menu (bypass, remove, copy, paste), the "+" tile adds a plug-in from the browser or a built-in block. The plug-in's editor docks in the right half of the screen with a PIN key that keeps it while the channel changes; a sandboxed plug-in, whose editor is a window of another process, shows its generic controls and an OPEN key. The Universal Mixer has its "back to surface" key for this console, and the PLUGIN HOST has a console theme.
- PLUGIN HOST (first step): a console of Sidecar's own for hosting plug-ins on a real console's inserts. 16, 32 or 64 channels; each takes one interface input through its plug-in chain to one interface output (both patchable; input N to output N at first), with a BYPASS that passes the input straight through. It mixes nothing: no buses, aux, matrices, sends or mains. It is a tile in the start-up chooser and in SETUP > PREFERENCES > INTERFACE, and the Universal Mixer shows it until its own surface is drawn.

## [0.3.0] - 2026-11-11

- Benchmark: a single Sidecar Mark (higher is better, about 1000 for a mid-range PC) with DSP, Effects, Recording and Interface scores and a rating, taken at 128 samples and 48 kHz so PCs compare; it is saved with each run, shown in COMPARE and in the text and CSV exports, and the QUICK TEST gives an estimate.
- dLive C1500: the surface is laid out as the real console, top to bottom: the PEQ and its four Gain/Freq pairs under the screen, the screen keys, PAFL meters, Assign and Talk right of it, USB sockets and Phones top right, the Display Function keys left of the strips, and no empty gap above them; the top section is centred on the panel, with Phones and the SoftKeys at the right edge.
- Surfaces: no console's fader caps cover the numbers printed beside the scale (the legend moves clear of a wide cap, a narrow strip gets a narrower cap: dLive, X32, SQ-5, 01V, ProMix, 02R, DM2000)
- dLive C1500 surface: SoftKey 11 sits below the seam of the upper panel, as on the console; the top bar's name no longer runs into the notice
- Q1202USB: a ROUTING page (the interface's inputs to the channels, the stereo pairs and 2-TRACK; MAIN, CTRL ROOM, PHONES and FX SEND to the interface's outputs) and an UNIVERSAL MIXER key in the top bar
- WING COMPACT: the window shows only the COMPACT's panel, the sides dark
- Every console surface with a display (X32, ProMix 01, 02R, 01V, SQ-5, dLive, DM2000, PRO2): F10 or a double-click on the display enlarges it to the window, again or Esc restores
- PRO2: the trackball moves a cursor on the screen, its keys click (and drag a control with a key held); the channel strip knobs go straight to the new strip's values instead of passing through the centre
- PRO2: SOURCE / ROUTING in the Universal Mixer's CHANNEL panel opens the input block's editor (the chain has no INPUT row)
- Universal Mixer: SETUP > PREFERENCES > INTERFACE lists every console; a gate's curve now shows its closed part along the floor with a range handle that stays on the graph; the dynamics' ratio handle sits further up the curve and the threshold wins where they are close; level bars (the sends' faders) use a new theme `fill` colour with at least 3:1 contrast against the panel on every theme; FADERS cycles faders, knobs, none; EDIT on an open card opens the block editor; a disabled EQ or band shows its curve fill in grey (colour when enabled); layer keys fold into the LAYER menu when the header row is too narrow; SOURCE / ROUTING falls back to the input's neighbours where a console's chain has no input row; a theme per console (WING, X32, ProMix 01, 02R, SQ-5, 01V, dLive, DM2000, PRO2, Q1202USB), the default when that console is loaded, listed in MENU > THEME under "Console themes", a choice remembered per console.
- Benchmark: SETUP > BENCHMARK (the Universal Mixer's and the dLive's SETUP) measures this PC offline, on a low-priority thread with engines of its own (no sound, the audio interface and the live engine untouched), at the current sample rate and buffer size plus any other sizes chosen: each block model per console and every effect device (mean, 99th percentile and worst block time as a percentage of the block's budget), how many channels of each console's processing fit under 70% (and with all its effects), the show's plug-ins (optional, with a warning), how many tracks the recording folder's disk sustains, and the interface's frame rate. It shows the running test, a live graph of block times against the budget, a log and a CANCEL key, then a plain summary, recommendations and every number in tables; runs are saved (Benchmarks folder in the app data) and can be compared side by side, exported as text or CSV. A full run takes about a minute; QUICK TEST a few seconds.
- Plug-in block: any strip's chain takes a PLUG-IN block (the ADD key in the Universal Mixer's inspector, not offered while a real console is under remote control) that hosts a VST3, AU or LV2 plug-in or a chain of them without an FX slot. It reuses the FX slots' hosting (the sandbox setting, crash handling that bypasses only the block, latency counted in the strip's delay compensation); a CHANNEL panel card shows its name, vendor, latency, ON, EDIT and PLUG-IN keys. Shows and scenes save the plug-in and its state; one that is missing leaves the block bypassed with its name. Console surfaces list it by name only.
- Universal Mixer CHANNEL panel: EQ handles on every console (frequency and gain by dragging, Q by wheel or pinch, a cut moves sideways only; listed-Hz EQs like the ProMix, 02R and 01V too), the panel follows the chain (an added EQ or dynamics block gets its own card, "EQ 2"), a drag bar and a MAXIMISE key (double tap restores) size it, height remembered.

- Console tape on the ProMix 01, 02R and 01V (their real panels have no name displays): a strip of tape at each fader, following the layer, tap to write the strip's name. The Universal Mixer's MIXER view drops the large label band at the top of each strip; the name tab above the fader is the only label, filled with the strip's own colour, and the blocks get the height.
- Universal Mixer user-defined layers, step two: drag editing and the other consoles' layers. A long press (or a mouse drag) on a strip's or DCA's name tab picks it up: in a layer it drops between columns to reorder, on a layer's header key (or the LAYER menu key) it is added, anywhere else it is taken out; from ALL it drops on a key to add it. The LAYERS editor drags the same way (reorder, onto another layer in its list, out) and has ADD DCA. Each layer says which console bank it occupies (BANK key; saved, exported and imported with the layer): the SQ-5's layers A-F and the dLive C1500's layers A-F and C3500's two banks of A-F show the same layers on their surfaces, Strip Assign edits there come back to the layer (what a slot cannot show, such as a send, stays in the layer), and a layer made in the Universal Mixer fills its bank. The WING takes layers on USER 1 and 2. The X32, ProMix, 02R, 01V, PRO2, Q1202USB and DM2000 have no user layers (their layers are fixed, or the DM2000's and 01V's REMOTE layers drive other equipment), so their surfaces are unchanged.
- Universal Mixer user-defined layers, step one: LAYERS in the header opens an editor (NEW, RENAME, COLOUR, DELETE, DUPLICATE, EXPORT, IMPORT; ADD SELECTED, SPACER, REMOVE, UP, DOWN) and each layer gets a header key next to BANK (one menu key once there are more than three); a layer shows exactly its strips in order, a spacer as a gap, and the filters and ALL work inside it. The layers are the show's user layers, so the WING surface's USER layers are the same ones (a layer made in either place shows in both), now with a colour, saved with the show (old shows load with none) and exported to or imported from a .sidecarlayers file.

- Universal Mixer CHANNEL panel: the EQ card (and the expanded one) shows an RTA of the selected strip behind the curve, soft accent bars in 1/6-octave bands with fast rise and slow fall; an RTA key on the card turns it off (on by default, saved), and the analyser runs only while the panel is shown.

- Universal Mixer: nine selectable themes in MENU > Theme (Flagship Black, Brushed Silver, Midnight Blue, Vintage Warm, Daylight, High Contrast, Studio Slate, Studio Graphite, Studio Olive; contrast-checked, 7:1 for High Contrast), applied at once and saved; the old dark and light settings map to the first two.

- Universal Mixer: every console's blocks now draw a picture on the strip blocks and the CHANNEL panel's CHAIN card, not only the WING's: EQ, filter and graphic EQ as the engine's magnitude curve (graphic EQ also as bars), gate, compressor, expander and limiter as their static transfer curve with the gain reduction, the input as its trim scale with polarity and balance, a delay as its time, an insert as a send/return loop with its device, and an FX device by its name, key values and a category glyph.

- Universal Mixer CHANNEL panel: a header (strip number, name and colour, MUTE, SOLO, polarity, SOF, COPY / PASTE) over block cards in signal order, each in its block colour; tap a card's title to open it in place, large, with EDIT for the inspector (smooth open and close); 44 px keys; INPUT / ROUTING in one card. The gate and dynamics threshold handle no longer jumps under the pointer (it sits on the 1:1 line, and a scrolling parent cannot take the drag), and the gate graph no longer draws a line along its bottom.

- Universal Mixer (MIXER view): blocks are coloured by kind (input, filter, gate, EQ, dynamics, insert, delay, FX) with an edge bar, LED and faint tint, the meter bridge, block area and fader tray are shaded, and every strip has a large channel number and name under the meter bridge on its own colour.

- Start-up window: the consoles are a grid of tiles (4 across, fewer when narrow, scrolling by wheel or finger) with a picture of the running console's surface, name and note; arrow keys and Enter pick, Esc continues.
- Universal Mixer: any console's processing blocks on any console's strips (a WING EQ on a ProMix 01, a DM2000 compressor on an X32, FX of any console in any rack): the model lists offer every console's models, the strip's own first; the block's tag follows its model; shows and scenes keep the swap; a surface shows a foreign block by its console ("WING EQ") and never offers them; none is offered or taken while a real console is under remote control.
- X32 display: ROUTING (BLOCK ROUTING and PATCH tabs, dark block lists with CONNECT, OUTPUT PATCH OVERVIEW, OUT/AUX/P16 patch pages with delay, an inert USER page) and SETUP > GLOBAL redrawn as on a real X32 (firmware 4.09), the METERS pages (channel, RTA, new AUTOMIX), the channel SENDS and MAIN tabs redrawn the same way, and the meters green to -18 dB, yellow above, red at the top.
- Remote control of a real X32 (OSC, UDP 10023, from P.-G. Maillot's public protocol document): CONNECT/DISCONNECT keys on the Universal Mixer header and the X32 surface's top bar, the IP remembered, status OFFLINE / CONNECTING / CONNECTED / LOST. The console is read into the engine first and wins (nothing is pushed on connect); then local changes send just that address and the console's changes (`/xremote`, scene recalls) update both UIs, without echo and without fighting a held control. This computer's audio is off while connected, local scene recall is refused, the local show is saved first and restored on disconnect. Meters are not shown. Untested against a real console.
- X32: processing and effects corrected against measurements of a real X32 Compact (firmware 4.09): factory values, ranges and laws of the 61 effects, gate envelopes, compressor detector, EQ shapes, reverb decay and level. Saved settings of M/S Fair, Dual Enhancer and Dual Pitch no longer line up.
- Surfaces and the Universal Mixer: the WING (all three models) has the shared top bar with the UNIVERSAL MIXER key; the swap is one click both ways and the Universal Mixer returns to the same view, filter, scroll and selected strip. The Universal Mixer's header is two rows (title and connection place, the views, back key, MENU; the strips' controls with the show's keys: scene GO, CLR SOLO, DIM, TALK), the second row can be put away (LESS/MORE, MENU), bank and filter fold into keys.
- 01V and DM2000: fader groups, [ON] groups and pairs save with shows and recall with scenes (the console text; the FADER, SENDS, GROUPS and DYNAMICS recall scope covers them). Shows without them load with nothing grouped.
- ProMix 01: METER, PAN, COMP and EQ now match photographs of a real console dot for dot (fonts, positions, tint); panel colours resampled.
- WING RACK: a third console model (SETUP > SIDECAR > CONSOLE MODEL RACK). The front panel in one row: the display without the six
  encoders and the four-channel section (LCD, four level encoders, MUTE and SOLO keys, INPUT AUX, BUSES MAIN, DCA MUTE, CUST TRANS,
  the shift keys, VIEW, PHONES LEVEL). 24 local preamps, no local aux I/O, a VIRTUAL section on the screens; remote clients see `wing-rack`.
- dLive: its 8 RackUltra (UFX) engines are FX 17-24, each with its channel strip (PEQ, dynamics, the FX,
  dynamics, PEQ, Width) and a send and return of its own, and its 64 Dyn8 engines insert on channels and mixes.
  Every FX engine can take an external key from a strip; the console has a Global Key and MIDI notes reach the
  MIDI Harmoniser and the Tuner. All of it is saved in shows and scenes. Engine side: the screens follow.
- Universal Mixer: a design pass in black anodised metal (brushed silver in the light theme): one type, spacing and radius scale, LED meter bridge with a bar per channel, peak hold and clip, ribbed fader caps, knurled knobs, backlit keys, themed pop-ups. A strip's body now shows its real chain, block by block in signal order, each section draggable to resize and collapse (the same on every strip, kept in the settings), and narrow, normal or wide strips. Undo and redo of the mix (Ctrl+Z, Ctrl+Y), a shortcut list, F1 to F9 for the views, a main output meter in the top bar. Touch: 44 px rows, more room between MUTE and SOLO, a finger sweeping along the strips scrolls them instead of moving a fader, dragging off a fader is a fine adjustment.
- Universal Mixer: scene recall scope and safes, snippet DCAs, mute groups and FX, banks and a JUMP list, FX bypass on the tiles, ROUTING levels and crosshair, a light theme.
- 01V: SETUP, OPTION output, OMNI OUT, OUTPUT DELAY and REMOTE pages; fader and [ON] groups, pairs, FLIP, 2TR IN; user library programs.
- X32 display: the menus against the manual: RTA page and EQ overlay, EQ on faders, device panels, SETUP pages, snippet scopes, iQ setup, DCA names, UNDO; `/-prefs` and `/-stat` on the remote.
- PRO2: its own surface (first pass, from the manual and press renders): the bays, VCA and POP group navigation, area B, the channel strip detail areas, outputs, scene keys and its screen.
- XENYX Q1202USB: Sidecar's first analog mixer (replacing the 1204USB: this is the owner's own console). Its panel, drawn from the owner's photographs and laid out as the real top view (channels as rows: 4 mono with GAIN, low cut, one-knob compressor, 3-band EQ, FX, PAN and LEVEL; 4 stereo pairs with BAL, FX and the +4/-10 switch; the main section with the FX SEND master, PHONES/CTRL ROOM, USB/2-TRACK keys, +48 V and one horizontal MAIN MIX fader), routing (one FX bus), blocks (the curves are Sidecar's reading), console tape at each channel and a scenes bar over the panel.

- DM2000: Automix (fader and ON takes), Track Arming and Locator drive the multitrack recorder (with the transport), Data saves and loads shows, PHONES/Control Room source keys, PLUG-INS and CHANNEL INSERTS keys, Output Insert In patch.
- DM2000 LCD: MIDI setup and tables, output patch and Direct Outs, pairs, libraries, preferences, oscillator, word clock, User Defined Keys; channel COPY/PASTE; the faders show the printed legends.
- DM2000: its own surface. 24 strips and the master, channel name displays, layers, encoder and fader
  modes, the Selected Channel, Display Access and the 320 x 240 dot LCD with the EQ, attenuator, gate, comp,
  insert POSITION, delay, pan, routing, bus-to-stereo, aux and matrix send, parameter and fader view, meter,
  group, GEQ (six at most), effects, scene and patch pages.
- Yamaha DM2000: its fader laws are now Yamaha's own 1024-step tables, and its MIDI works: parameter
  changes and requests for everything the engine has, Control and Program Changes, Key Remote, meter data and
  MMC, from Yamaha's V2 MIDI Specification.
- dLive: a C1500 surface beside the C3500 (SETUP chooses); Dyn8 and the RackUltra card's DSP, channel strip and 11 models, offered on
  Universal strips for now.
- Universal Mixer: a pass over every view: keys that stayed dark fixed, first main pinned, sends on fader, DCA spill, CONTROLS view
  for custom controls, scene SKIP/LINK/tag/fade/notes, PATCH 1:1, strip copy and paste.
- SQ-5: add-on effects and DEEP models, MIDI Group addresses and Soft Key notes, Soft Keys, Setup and Utility pages.
- SQ-5: Ganging, Mix Stereo, Network, Users, Data, SQ-Drive and Footswitch pages; lit keys light their window, not the whole cap.
- 01V: its MIDI (parameter changes, control changes with the three arrangements, program changes, bulk dumps) and its LCD pages; scene
  recall undo (RECALL UNDO), with safes and recall filter respected.
- Midas PRO2: processing blocks and profile in the engine (no surface yet).
- dLive: Main modes, PAFL busses, Ext In from mixes and PAFL, Source Selectors, ABCD inputs, Direct Out options, Follow Main
  Pan and stereo image in the engine and shows.
- MIDI over TCP for the dLive (TCP 51325, 51328) and SQ-5 (51325), with Gets and state feedback.
- dLive screens: a mix's Ext In tab, ABCD inputs and the stereo image on Preamp, a Routing tab (Direct Out, Mix Source, Follow
  Main Pan), MixRack's Source Selector live, the MIDI channel and connections on Network; the Main mode's strips on both surfaces.
- WING: BALANCE raises one side as it cuts the other; REMOTE LOCK now blocks OSC and TCP writes; the SD RECORDER custom-control
  keys drive the multitrack recorder. `docs/WING_GAPS.md` lists every chapter of the manual against Sidecar.
- X32: the display's menus redone against the manual and photographs: the LIBRARY (channel, effects,
  routing only, preset list with save slots), MONITOR, talkback and oscillator pages, ASSIGN (a home tab
  and the set tab's lists), METERS' groups and RTA controls, SETUP, HOME's config, main and sends pages,
  MUTE GRP over the page in view, UTILITY by screen, the status bar's recorder and link LEDs. Items with
  nothing behind them are shown dimmed.
- The start-up notice now covers every console: a hobby project, used at your own risk, none of the consoles
  complete, with a rough percentage for each.
- A crash when a control on a console's display rebuilt the controls while it was still
  being turned (seen on the X32's SCENES screen).
- The WING remote also accepts the Native protocol in a WebSocket on its TCP port, as
  web-based remote apps use it (experimental): WING-Q connects and shows the console.
- Custom controls: an encoder and its two keys each keep their own name (`bu/name`,
  `bd/name` were dropped), and their LCD shows them as a WING does: the encoder's name
  above a rule, then a row for each key.
- A start on the Allen & Heath SQ-5: its processing blocks (gate, compressor with parallel
  path, ducker, 4-band PEQ), offered on any Universal strip, and its console profile
  (counts, signal chains, fader law), its 28-band GEQ on every mix and its 11 RackExtra
  effects, and its MIDI (NRPN levels, mutes, pans and assignments, scenes). It can be
  chosen in the start-up window and SETUP's INTERFACE, and has its own surface and
  touchscreen screens drawn from the manual's pictures. C ABI 53. Its add-on effects,
  TCP MIDI, and several Setup and Utility pages follow.
- A start on the Yamaha 01V (the original): its 42 effect types, offered on any Universal
  strip, and its console profile (24 inputs on 22 channels, 4 auxes, 4 buses, 2 effect sends
  and returns, signal chains from the manual's block diagrams, 2-band EQ on channels 17-24,
  three fader laws). It can be chosen in the start-up window and SETUP's INTERFACE and runs
  on the Universal Mixer. C ABI 54. Its MIDI and its own surface follow.
- The dLive's Mixer Config, on SETUP's CONSOLE page: how its 64 busses are laid out as mono and
  stereo Groups, Aux, FX sends and Matrices, and its IP Stereo Config, which of the 64 input pairs
  are one stereo channel. APPLY resets the mixes and keeps the channels, FX and DCAs, as the
  console does (a pair that changes between mono and stereo starts on its defaults). C ABI 56.
- A start on the Yamaha DM2000: its console profile and processing (EQ Types I and II, dynamics, GEQ and 44 of its 52
  effect presets), offered on any Universal strip. It has no surface yet.
- An accuracy pass over the other surfaces and their screens, against photographs and the manuals' pictures:
  the 01V's LCD pages (Phase, Delay, View, Pan, Routing, Dynamics, Memory and more), the ProMix 01's body, keys
  and LCD, the 02R's plates, keys and readouts, the SQ-5's panel and every screen, the X32's pale screen pages.
- The dLive now opens on a C3500 surface, drawn from photographs of a real one: 24 motor faders in two banks
  of 12 with six layers each, two touchscreens, two blocks of soft rotaries. Its left screen is the selected
  channel's processing, with the PEQ's graph and boxes as the console has them; its right screen has Scene
  Manager, the other Scenes tabs, I/O patch grids, Meters and MixRack (Config and Audio). The PEQ graph has the RTA. The C1500 surface stays, one key away. Pages without a photograph are Sidecar's reading.
- On both dLive surfaces: Copy, Paste, Reset, Safes, Freeze in Layers and the GEQ on the faders work (the keys
  latch, as a mouse can't hold one key and press another), Assign takes a strip's Mix key and Pre its Sel key,
  rotary keys 1-4 have default functions, and the LCDs show what the console's do.
- The dLive has its own surface, drawn from photographs of a real C1500: 12 strips in three groups with
  colour LCDs, lit rotaries, banks A-F and the PEQ's rotaries, its touchscreen showing the mixer's
  pages. Some keys are drawn but don't work yet (see the docs).
- The dLive has its Automatic Mic Mixer: NOM and D-Classic, with up to four AMMs, Chair, Solo, Best Mic
  and Priority. SETUP has an AMM tab; each channel chooses its AMM on its strip panel.
- The dLive's input channels have its two preamp models, Dual-Stage Valve and Tube Stage, in a PREAMP
  block after the trim. Their ranges and curves are Sidecar's reading.
- The dLive's mixes can use its 12-band NEQ, alongside the GEQs; it's on any Universal console too.
- The dLive has all 20 of its RackExtra effects, in any of its 16 engines and on any Universal
  console: the four SMR reverbs, delays, modulation, pitch and dynamics processors. Where its guide is
  silent, the ranges and algorithms are Sidecar's reading.
- A dLive's mixes have the console's Ext In (a socket mixed in with its own trim and polarity) and
  Mix Source (the point in each channel its sends to that mix leave from). Sends start pre-fade to
  the Aux and post-fade to the Groups and FX sends.
- The Yamaha 01V has its own surface, drawn from the Owner's Manual and photographs of a real
  console: 14 motor faders and the STEREO master on their printed scales, SEL, SOLO and ON, the
  nine fader modes (HOME, AUX 1-4, EFFECT 1-2, OPTION I/O; REMOTE doesn't work yet), SELECTED
  CHANNEL, RETURN 1-2, GAIN and PAD, the L/R meter and a 320 x 80 dot LCD. The LCD's EQ/ATT,
  DYNAMICS, delay, pan, send, VIEW and MEMORY pages work, and so do EFFECT 1 and 2's edit,
  library and PRE/POST pages, AUX's PRE/POST, and UTILITY's oscillator and preferences. A function
  key pressed again goes to its next page. CH 17-24 have their controls on OPTION page 2 (routing,
  pan, fader, AUX 1-2 and EFFECT 1-2 with PRE/POST, attenuator and 2-band EQ). MIDI, SETUP and REMOTE
  are still to come.
- The ProMix 01's surface is redrawn from photographs of a real console: a true 240 x 64 dot LCD in
  its black window, ribbed waisted fader caps, plain cream keys with round LED holes and their names
  printed on the panel, the light plates behind SCENE MEMORY, and the raised plate around the LCD
  and wheel.
- The 02R's panel takes its colours from photographs of the console: the charcoal body and raised
  LCD hood, light grey plates for DISPLAY ACCESS and SELECTED CHANNEL, cream, orange and pale blue
  function keys, black SEL and cream ON keys, teal EQ keys and knobs, a red ST key, ribbed cream
  fader caps with black 17/18-23/24 and a red STEREO, and the LCD's pale blue.
- A start on the Allen & Heath dLive: its processing blocks, offered on any Universal strip.
  Its gate, ducker, dual-threshold and source expanders; ten compressor models (Manual Peak,
  Manual RMS, 16T, 16VU, Mighty, Opto, OptTronik, Peak Limiter 76, Bus, CompStortion); its
  4-band PEQ and four 28-band graphic EQs. Its console profile, as its FoH template lays it
  out: 128 inputs on 124 channels, 8 groups, 16 auxes, 8 FX sends, 16 FX and their returns,
  LR, 8 matrices, 24 DCAs and 8 mute groups, signal chains from its block diagram (the
  compressor before or after the PEQ on each input, a 28-band GEQ after each mix's PEQ), its
  fader table and sockets. It can be chosen in the start-up window and SETUP's INTERFACE and
  runs on the Universal Mixer, its FX slots on the SQ-5's effects for now. Buses past the
  16th can now feed outputs, channels and effects. C ABI 55. Mixer Config, its own effects,
  its 12-band EQ, its MIDI over TCP and its surface follow.


Three more consoles on the Universal Mixer's core, each a preview: the **Behringer
X32**, the **Yamaha ProMix 01** and the **Yamaha 02R**. A console's profile (the
console it emulates) is now part of its configuration and brings that console's
counts, signal chains, processing models, fader and pan laws, port groups, colours,
effects and remote protocols; the WING is one of these profiles and sounds as before.

- **X32:** its channels, buses, LR and M/C mains, gates, compressors and EQs, its 61
  effects (all with their algorithms) and factory rack, an OSC server that answers
  X32 apps, its MIDI map, .scn scene import and export, and the X32 surface with its
  display (HOME, ROUTING, EFFECTS, LIBRARY, SCENES, SETUP, METERS, MONITOR, USB...).
- **ProMix 01:** its 16 channels, ST IN and effect returns, 3-band EQ, three
  dynamics processors, two internal effects, its 531 MIDI parameters, scene memories
  by bulk dump, libraries, and the ProMix surface with its LCD and PARAMETER wheel.
- **02R (Version 2):** its 40 input channels, 4-band EQ, dynamics, two internal
  effects, EQ, dynamics, effects and channel libraries, scene memories, surround
  panning, MS decoding, its MIDI and SysEx, and the 02R surface with its display.

Sidecar now opens with a window: continue where you left off, or start a fresh WING
(experimental), Universal, X32, ProMix 01 or 02R (very experimental). Also: console
oscillators, dropout diagnostics (`LIVEDAW_TRACE_SLOW`), WING remote clients see the
console's selection, the console surfaces scale to the window, and fixes for a crash
when the Universal Mixer opened on an 02R console and for the ProMix 01's and 02R's
data wheels, which moved a value thousands of steps per pixel.

The Universal Mixer now runs a show without the WING surface for nearly everything:
monitor processing and talkback, SETUP > REMOTE, channel and FX presets, a LIBRARY
view of the Library folder, snippets with their strips and sections, DCAs and mute
groups, every console's processing blocks, and a 1280 x 720 window. Only custom
controls still need the WING surface. The X32 also gained its channel preset files
(.chn), INIT and a fuller ASSIGN editor, and the 02R's fader recall safe and fade times
now act on a recall.

The show format is still 4: shows from 0.2.0 open as before. Shows of an X32, ProMix
01 or 02R console need 0.3.0 (0.2.0 knows no console profiles).

### All changes

- Universal Mixer, SCENES: a snippet's strips and sections. + STRIP captures the
  selected strip into it, - STRIP drops it, and a key per section (NAME ... GROUPS)
  drops that section or captures it again.
- Universal Mixer: a LIBRARY view, the Library folder's scene, snippet, channel and FX
  preset files with filters, LOAD (onto the console, the selected strip or an FX slot),
  ADD TO SHOW, RENAME, DELETE and SAVE SCENE (the current scene as a file).
- X32: INIT on UTILITY's new init tab puts the selected strip back to an X32's
  factory settings (two pushes; the source stays).
- X32: the display's ASSIGN editor also sets effect parameters and the selected
  channel's controls on encoders, and a scene, PREV and NEXT on buttons; a
  control holding another code (MIDI, a page) shows it instead of "-".
- X32: the talkback channel on the display's MONITOR talk tabs (the sixth
  encoder, TALK CH), so an X32 show no longer needs the Universal Mixer for it.
- 02R: fader recall safe and fade times act on a scene recall. A safe fader keeps
  its level, and a fader with a fade time glides over it; a recall keeps these
  settings as they are. Any scene fade can now give a fader a time of its own
  (`Show::set_fader_recall`).
- X32: channel preset files (`.chn`). LIBRARY's channel tab imports one onto the
  selected strip (keeping its source) and exports the strip as one; mains, buses,
  matrices, aux and FX returns too. C ABI 52.
- Universal Mixer, every console's blocks: the ProMix 01's (compressor, gate,
  ducking, 3-band EQ, its 15 effects) and the 02R's (compressor, expander, the two
  companders, gate, ducking, 4-band EQ, its 17 effects) join the WING's and the X32's
  on a Universal console, named with their console ("PROMIX 01 Compressor").
- Universal Mixer, stand-alone basics: the top bar shows the show's name and its
  **SHOW** menu (NEW, OPEN, SAVE, SAVE AS); the MIXER view has a column per DCA (name,
  colour, MUTE, SOLO, fader) and the mute groups' keys (engage, ALL OFF) after the
  strips, or alone under the new **DCAS** key; the inspector's new **STRIP** page names
  and colours the strip (its source while LINK CUSTOMIZATION TO SOURCE is on, as on the
  WING) and assigns it to DCAs and mute groups. Any number of each, as configured.
- Universal Mixer, MONITORING: each monitor path's processing (DIRECT IN and its
  level, the 8-band EQ on its curve with handles, INVERT, PAN, WIDTH, DELAY by
  distance, the LIMITER with its gain reduction) and talkback (the talkback channel,
  TALK LEVEL, TALK A and TALK B pressed and released as on the console, each path's
  mode, dims, USE SEND LEVELS and destinations), which were on the WING surface only.
- Universal Mixer, SCENES: the selected scene's snippets beside its scenes: NEW (a snippet
  of the selected strip, every section), GO, STORE, RENAME, FIRE WITH SCENE, REMOVE.
- Universal Mixer: channel presets (SAVE PRESET / LOAD PRESET on the inspector's STRIP
  page) and FX presets (on FX RACK, for the selected slot), the same .lchn and .lfx files
  as the WING's LIBRARY, in the Library folder unless another is chosen.
- Talkback reaches every bus, main and matrix of the console (the first 256 of each),
  not only a WING's 16, 4 and 8: TALK A and B's destinations are group sets (shows store
  them as before while they fit, so older shows load as they were). The Universal
  Mixer's TALKBACK offers them all.
- Universal Mixer: a monitor path's SOURCE and DIRECT IN can be any strip of the
  console, past a WING's 16 buses, 4 mains and 8 matrices (a console's BUS 17-24, say):
  the engine numbers those choices past the WING's (`params::extended_source`), so the
  WING's keep their numbers in shows and for WING apps. C ABI 51.
- Universal Mixer, SETUP > REMOTE: the WING remote (enable, discovery, OSC and native
  servers with their ports, the console name, the clients), the web remote (enable,
  port, PIN, view only, the addresses) and MIDI (each function's route, the DIN and USB
  ports' devices, DAW control and its emulation, the DAW port's devices).
- Fixed: the network remotes (WING apps; an X32's OSC on an X32) started only when the
  WING surface was built, so Sidecar started on the Universal Mixer or a console's own
  surface left them off. They start with the app now, whatever the interface.
- Universal Mixer polish: a send's FROM no longer reads "PRE...", the DYNAMICS panel's
  model name no longer runs into its title, the FILTER graph shows "10k" whole.
- Universal Mixer, every console's blocks (`docs/UNIVERSAL_MIXER.md`, decision 6): a
  Universal console's strips and FX rack can hold the X32's models beside the WING's:
  its gates (expanders, gate, ducker), compressor and expander, 4-band and 6-band EQ
  on any strip, and its 61 effects (its send effects in FX 1-8). The lists name them
  with their console ("X32 Gate"); shows and scenes keep them. The WING's models keep
  their places, so WING shows and WING remote clients are unchanged; a WING client
  can't see an X32 model and reads its node's default. The X32, WING and other
  profiles still offer only their own models.
- Fixed: the dynamics transfer graph drew the WING's model at the same place in its
  list instead of the console's own (on an X32, the gate's graph was another model's).
- Fixed: an FX preset of an X32 effect showed "NONE" as its device.
- WING surface checked against the pictures of the WING User Manual (2025-10-20): HOME,
  EFFECTS, METERS, ROUTING, SETUP, LIBRARY, OVERVIEW, FADERS, SENDS, CONFIG, MUTE GROUPS
  and HOME > INPUT match its figures. Labels that were cut short now show whole, as on
  the console: CLIP on the meter scales, SOURCE GROUP and COPY CUSTOMIZATION on ROUTING,
  the selected tab (GENERAL), OVERVIEW and MUTEGRP in the status bar, the "37-40 / AUX
  IN" layer key, 0 dB and 0.1 m on INPUT's scales and METERS / SAMPLES on its delay
  units. The FADERS screen's names no longer run into each other on a 12-fader page.
  A test renders every main screen and HOME tab (`LIVEDAW_UI_SNAPSHOTS=<folder>`
  keeps the pictures, to compare with the manual's).
- More of the WING surface against the manual: FILTER SETUP, MAIN and BUS SENDS, the
  aux, bus, matrix and main HOME, the USB recorder and the WING EQ match. The knob
  bar's captions keep their words (LC FREQ, HC FREQ, SEND LVL), as do TAP WIDTH and
  the playlist keys (CREATE / OPEN / SAVE / CLOSE PLAYLIST); a bus's, matrix's and
  main's input grid on HOME is the console's size (it was half). Thirteen more labels
  that could be cut the same way (a font sized for squashing, then drawn unsquashed)
  are sized to fit.
- The Universal Mixer no longer crashes when it opens on an 02R console (or another
  whose strips' dynamics models change under it): the dynamics graph's handles use the
  model the graph was drawn for.
- Sidecar opens with a window: continue where you left off, or start a fresh console
  of a mixer (WING, UNIVERSAL, X32, PROMIX 01, 02R). A fresh start keeps the last
  autosave as `autosave-previous.lshow`. Safe mode and STARTUP files skip the window.
- The start-up window and SETUP > SIDECAR > INTERFACE say how finished each mixer is:
  WING (experimental); X32, PROMIX 01 and 02R (very experimental).
- The ProMix 01's PARAMETER wheel and the 02R's wheel can be used: a drag turns them
  a step per 6 pixels on the ProMix 01 and 4 on the 02R, and a click of the mouse
  wheel a step. A drag used to move a
  value about 8,000 steps per pixel (the whole field at once), and a long one wrapped
  round to the other end.
- The audio thread no longer allocates on its first block: the engine reads
  `LIVEDAW_TRACE_SLOW` when it is made, not when the first block asks (the WING
  soak test's audio-thread allocation check failed on it).
- CUSTOM CONTROLS: the lists' scroll bars work: their arrows move a row, the well
  above or below the thumb a page, and the thumb drags (#20).
- WING remote: clients see what is pressed and selected on the console, as on a
  WING. Selecting a strip sets `/$ctl/$stat/selidx` (1-76: channels, aux, buses,
  mains, matrices), and a client setting it selects the strip (#21). A MIDI key's
  or knob's VALUE (`/$ctl/user/.../val`) follows what it sent or received (a CC
  toggle 127 or 0, a push while held, a knob its CC's value), so subscribed OSC
  and Native clients get the event (#19). C ABI: `ld_wing_set_selection`,
  `ld_wing_take_selection`, `ld_wing_set_midi_values`.

Phase 3, the WING as a profile (`docs/UNIVERSAL_MIXER.md`), begins.

- The console's profile (the console it emulates: the WING, or the Universal for a
  console beyond a WING's counts) is part of its configuration (`Profile`,
  `ConsoleConfig::profile`). The engine keeps it, a show stores and restores it
  (`<Console profile="...">`; a show of 0.2.0 whose counts aren't a WING's loads as the
  Universal), and the app asks it instead of comparing the counts. C ABI 30:
  `ld_engine_profile`.
- A profile says what its console is (`ProfileSpec`): a new console's counts and its
  strips' chains and process orders; a strip runs its profile's chain.
- NEW SHOW keeps the console as it is configured (a console of 48 channels stays one)
  instead of always making a 40 channel WING.
- MIDI ports (devices, virtual ports, hot-plug, the input queue) are their own layer
  (`MidiPorts`), under the WING's MIDI protocol, ready for consoles controlled over
  MIDI.
- A profile names its fader and pan laws (`FaderLaw`, which can be stepped, and
  `PanLaw`; both the WING's for now). Strips pan, scene fades move and the app's
  faders draw by the console's law; the WING's MIDI and OSC keep the WING's. C ABI 31:
  `ld_engine_fader_law`.
- A profile names its port groups (`PortGroup`: the console's sources and outputs as
  its routing pages group them, as ranges of the engine's numbering); the WING's 25
  are the first. C ABI 31: `ld_engine_port_groups`.
- Processing models are described in console-neutral terms (`livedaw_dsp::model`:
  `ModelSpec`, `ParamSpec`, `Params`, `ModelTables`), and a profile names the models
  its blocks may hold (`ProfileSpec::models`; the WING's are `wing::MODELS`).
- A strip's own filter, gate, EQ, dynamics and inserts are reached only through
  accessors (`StripParams::filter()`... in Rust and C++), and are now chain blocks
  like the extra ones: parameters and DSP state together (`OwnBlocks`), run by the
  strip's template, `StripParams` pointing to them. Renders are identical sample for
  sample. C ABI 32: `StripParams` holds pointers to the blocks.
- A profile names its remote protocols (`Remotes`: its MIDI protocol and its network
  protocols; the WING's for the WING and the Universal). The app makes the console's
  MIDI protocol from it (`MidiProtocol`, of which the WING's MIDI remote is one) and
  runs only the network servers it names. C ABI 33: `ld_engine_remotes`.
- The routing pages show the console's profile's port groups instead of a table of
  their own, and the interface draws and offers the profile's colours (`Palette`; the
  WING's 18 for both profiles) and its strips' labels ("CH 1"). C ABI 34:
  `ld_profile_port_groups`, `ld_profile_colour_count`, `_rgb`, `_name` and `_at_grid`,
  `ld_profile_strip_label`.
- `livedaw-wing` speaks as the WING's profile (`livedaw_wing::profile`): its servers
  run only the network protocols the console's profile names. Phase 3 is complete;
  the WING's description stays shared with the Universal until a second console.

Phase 4, the X32 (`docs/UNIVERSAL_MIXER.md`), begins.

- An X32 profile: a console can be configured as an X32 (its counts, its fader law on
  1024 steps, its 16 colours; C ABI 35 `ld_engine_configure_profile`) and a show stores
  it. Its processing, sends and remote protocols are still the WING's or none, until
  the next steps.
- A profile can give strips from a slot on their own chain and label: the X32's aux 1-8
  (AUX) and FX returns (aux 9-16, "FX 1"-"FX 8") run trim, EQ and fader only. C ABI 35:
  `ld_profile_slot_ranges`.
- A gate, dynamics or EQ block holds its console's models (its profile's tables), so a
  console's own models can run, save and load; the WING's sound is unchanged. C ABI 36.
- The X32's processing models: its gate (EXP2, EXP3, EXP4, GATE, DUCK), its compressor
  and expander, and its 4-band (inputs) and 6-band (buses, mains, matrices) EQ, with its
  ranges and steps. An X32's strips run them, and the app's model menus show the
  console's models. C ABI 37: `ld_models_show_profile`. Their defaults and a new strip's
  colours are those of a real X32 (checked on an X32 Compact).
- The X32's signal chains: on channels the delay before the low cut, the compressor
  before or after the EQ and one insert before or after both; on buses, mains and
  matrices everything before the fader.
- An X32's mains: main 1 is the stereo LR, main 2 the mono M/C; channels feed LR, and
  its buses don't feed LR until routed (a factory WING's buses 9-16 do).
- A send can take its signal at any of its strip's own tap points; an X32's send types
  (IN/LC, <-EQ, EQ->, PRE, POST, GRP per bus pair) and its compressor and insert
  positions now shape the audio, set through its OSC server. Its sends start as on
  the console (on at -oo, EQ->), its buses send to matrices only.
- X32 links: over OSC, a linked odd/even pair shares what the console's link
  configuration says (preamp and delay, EQ, dynamics, fader and mute).
- An X32 OSC server (`livedaw-x32`, UDP 10023) that answers like an X32: info and
  status, gets and sets on the console's value steps, `/node` and `/` text, `/xremote`
  change updates and meters, over the engine's strips. The app runs it on UDP 10023 while
  the console is an X32 and the network remote (SETUP > REMOTE) is on. C ABI 38:
  `ld_x32_*`. Compared with a freshly initialised X32 Compact (`tools/x32/compare.py`):
  all 943 parameter groups answer the same, an X32's factory strips, names, colours,
  pans and DCAs included.
- X32 MIDI: faders, mutes, pans and mute groups by control change on MIDI channels 1-3,
  scenes by program change, both ways, as an X32's fixed MIDI map.
- The X32 surface: an X32 console opens on a skin laid out as an X32 (in Sidecar's own
  style): 16 input and 8 group faders in their layers, the main LR, scribble strips in
  the X32's colours, meters, SELECT, SOLO and MUTE, the channel strip section's knobs
  and keys for the selected strip (preamp and low cut, gate, compressor, the EQ's bands,
  bus sends, main bus), and the X32's display: its status bar, screen keys, tabs and six
  push-encoders mapped as an X32 maps them, the signal path, the gate, compressor and EQ
  graphs, sends, meters, scenes and mute groups. Unlike an X32, its screen takes touch.
  The X32 COMPACT is a model of it (8 input faders, in eight layers). SETUP > SIDECAR >
  INTERFACE offers the X32 and makes the console a fresh X32, asking first.
- X32 scene files: IMPORT .scn (on the X32 display's LIBRARY and the Universal Mixer's
  SCENES) sets the console from an X32 scene file and keeps it as a new scene; EXPORT
  .scn writes the console as one. What Sidecar doesn't emulate yet (FX slots, outputs,
  head amps) is skipped. C ABI 39: `ld_x32_scene_write`, `ld_x32_scene_read`.
- The FX rack runs its console's own list of devices (on an X32, premium devices in FX
  1-4); a slot holds 64 parameters (C ABI 40).
- The X32's effects: its 61 models with their parameters and its factory rack (Vintage
  Room, Hall, Stereo Delay, Stereo Chorus, graphic EQs), on the X32 remote (`/fx/1..8`)
  and in scene files. Their sound comes in the next steps: until then the reverbs,
  delays and modulation effects return silence and the insert effects pass the signal
  through.
- An X32's sends to buses 9-16 now start POST, as a freshly initialised X32's do
  (they started EQ->).
- An X32's FX 1-4 are send effects, as on the console: each is fed from a bus (factory:
  buses 13-16; any of MIX 1-16 or M/C, or INS to make it an insert) and plays back on
  the FX return channels (FX 1L/1R .. 4L/4R), one block late (C ABI 41). `/fx/1..4/source` on the
  X32 remote and in scene files sets them; show files keep them. The engine has a new
  source group for it, the FX returns (`FX_RETURN_SOURCE_BASE`).
- The X32's factory four effects make sound: Hall Reverb, Vintage Room and Stereo
  Chorus run on the WING's own Hall, Vintage Room and Chorus (the same designs, their
  controls mapped across), and Stereo Delay is the WING's stereo delay with a factor
  and a feedback per side and its own MIX.
- 15 more X32 effects make sound on the WING's devices of the same design: Ambiance,
  Room, Chamber, Plate, Rich Plate, Vintage Reverb, Gated and Reverse Reverb, Flanger,
  Phaser, Dimensional Chorus, Mood Filter, Rotary Speaker, Tremolo / Panner and Pitch
  Shifter. Dimensional Chorus now starts with MODE 1 pressed.
- The rest of the X32's send effects make sound: 3-Tap and 4-Tap Delay, Dual Pitch,
  Sub Octaver, the five combinations (Delay / Chamber, Chorus / Chamber, Flanger /
  Chamber, Delay / Chorus, Delay / Flanger) and Modulation Delay.
- 15 of the X32's insert effects work, on the WING's devices of the same design: the
  graphic EQs (GEQ and TruEQ, stereo and dual), the de-essers, the Precision Limiter,
  the Combinators, Enhancers, Exciters and Guitar Amps.
- The X32's compressors work: Fair (stereo, dual and M/S), Leisure and Ultimo
  (stereo and dual) run on the WING's dynamics models of the same designs, as does
  the Wave Designer.
- Every X32 effect now has its algorithm: the Program and Midrange EQs run on the two
  halves of the WING's PULSAR EQ, and Tube Stage, Stereo Imager, Edison EX1 and Sound
  Maxer are new devices of Sidecar's own.
- X32 MIDI: the X32 display's SETUP > remote tab chooses the MIDI input and output and
  what is received and sent (program changes, faders, mutes, pans, OSC over SysEx), as
  an X32's MIDI settings do. Program changes on MIDI channel 2 recall the current
  scene's snippets, a scene recalled on the console goes out as a program change, and
  OSC commands arrive as text in SysEx (`F0 00 20 32 32 <text> F7`; an address alone
  is answered with its values). C ABI 42: `ld_x32_sysex`.
- The X32's input routing: the audio interface's inputs are numbered as an X32 numbers
  its own (local 1-32, AES50 A 1-48, AES50 B 1-48, the card's 1-32, aux in 1-6), and
  `/config/routing/IN` and `PLAY` (the blocks of eight that feed inputs 1-32 and aux
  1-6), the channels' and aux ins' `config/source` and the head amps (`/headamp`, gain
  and phantom power on the interface's inputs; `/-ha`) work from X32 remote apps and
  scene files. As on a full-size X32, channels 1-32 take interface inputs 1-32; aux in
  1-6 now take the aux inputs (161-166) and aux in 7-8 the USB player's left and right.
- The X32's outputs: the interface's outputs are numbered as an X32 numbers its own
  (local 1-16, aux out 1-6, AES50 A and B 1-48, the card's 1-32, P16 1-16), and
  `/outputs` (each output signal's source and tap) and the output routing blocks
  (`/config/routing/OUT`, `AES50A`, `AES50B`, `CARD`) set them from X32 remote apps and
  scene files. A new X32 console has a full-size X32's factory outputs: buses 1-14 on
  outputs 1-14 and **Main LR on outputs 15-16** (no longer 1-2), the same on AES50 A
  and B, and the local inputs on the card's outputs. The output patch can now play a
  bus, main or matrix on several outputs at once.
- The X32 display's ROUTING screen has the X32's output tabs: out 1-16, aux out and P16
  out (each output's source, tap, invert and, for out 1-16, delay; LAYER: the local
  outputs' blocks), card out, AES50-A and AES50-B (what each block of eight plays).
- On an X32, the Universal Mixer's routing pages and the channels' port labels use the
  X32's port groups: LOCAL IN 1-32, AUX IN 1-6, AES50 A and B, CARD, LOCAL OUT 1-16, AUX
  OUT 1-6 and P16, numbered as the X32 numbers them ("A 1" is interface input 33).
- The X32 display's EFFECTS screen: home picks a slot (encoder 1), its effect (encoder
  2, FX 5-8 the insert effects only; its key EDIT opens the slot) and FX 1-4's SOURCE;
  fx1-fx8 list the slot's parameters and turn them six at a time (LAYER for the rest).
- The X32 surface's SENDS ON FADER: with a bus selected, the input faders move each
  strip's send to it; with a channel or aux selected, the BUS 1-8 / 9-16 layers move
  its sends to the buses (and with a bus selected, the MTX layer its matrix sends).
  The scribble strips mark the send in amber.
- The X32 surface's MONITOR / TALKBACK section, beside the display: the MONITOR and
  PHONES levels, DIM, MONO and MUTE, the TALK level and the TALK A and TALK B keys
  (latching). The channel strip's sections were re-proportioned to fit.
- The X32 surface's ASSIGN section, right of the main fader: sets A, B and C of four
  encoders and eight buttons, each doing what its X32 function code says (a fader, a
  pan, a send, an FX parameter, the selected channel's dynamics, a mute, an insert, a
  scene, a page of the display, a MIDI message). The codes are the X32's
  (`/config/userctrl`), set from X32 remote apps and kept in scene files.
- The X32 display's LIBRARY has a show tab (NEW SHOW, OPEN, SAVE, SAVE AS) and its
  UTILITY screen copies a strip's settings onto another (COPY, PASTE, and which
  sections: preamp and gate, dynamics, EQ, sends and fader). The X32 surface no
  longer needs the Universal Mixer for shows.
- The X32 display's ROUTING screen sets the input blocks (encoders 1-5) and switches
  between the recording and playback blocks (encoder 6), and shows which interface
  inputs each block takes. The channel strip's GAIN knob turns the head amp of the
  channel's input (-12 to +60 dB), or the digital trim for a channel without one.

Phase 5, the Yamaha ProMix 01 (`docs/UNIVERSAL_MIXER.md`), begins.

- 02R title bar and values as the V2 manual: the functions named SCENE, Φ/ATT, AUX 7 and
  VIEW 2/2, the MIC / TAPE halves not counted as pages (ATT 1/1, AUX 7 1/3, its effect
  EDIT 2/3), the channel box "MIC 1" and "EFF1 RTN", the scene box unpadded ("1 SCENE 01",
  "0 Initial Data"); the confirmation "Please Confirm." over "Store to Scene 1?"; values
  as the display prints them ("0dB", "5ms", "1:1", "hard", "L.SHLF", "H.SHLF"); METER's
  "PRE FAD" / "POST FAD"; an aux send that's off reads OFF.
- ProMix CUE for every strip as pp.52-54 (a channel, ST IN with its dual pan and sends,
  RTN1-2 and ST OUT with their balance): the groups A-D as squares, PHASE and PAN as
  headings, COMPn:ON when a processor is patched, the meters captioned with the strip
  (GRn beside them). SEND3 / SEND4 as p.45: "(FADER = SEND3)", the send's meter on the
  CLIP ... -40 scale, SEND: PRE at the bottom right; as a stereo pair "SEND 3-4", the
  pans as knobs and its balance.
- ProMix UTILITY and MIDI as menus (p.96's map: ENTER opens an item, EXIT or the key
  again comes back), UTILITY's SEND3,4 CONFIGURATION and OUTPUT COMP PATCH POINT ("POST
  FADER", STEREO) apart; MIDI SETUP drawn whether MIDI runs or not. SCENE MEMORY as
  pp.63-65: five memories going round, "01=InitData", MEMORY02 for an empty one, the
  Press "STORE" / "RECALL" banner, PROTECT; name pages keep their function's title, the
  character box over the name's line, CANCEL.
- ProMix LCD pages checked against the manual's pictures: COMP EDIT and EFFECT EDIT as
  lists ("Threshold= 0dB", "Ratio= 2:1", "[1]Rev.Time= 2.8s") under the program's name,
  COMP's with its gain curve, ON/OFF and PATCH, both with EXIT; SEND1-2's return meter on
  the CLIP ... -40 scale with "RTN1 L R"; LIBRARY's four programs ("1.RESET") with STORE
  and RECALL; a list's chosen program inverted under the cursor. The effects programs
  are named in capitals ("REVERB HALL 1"), the user programs "USER LIBRARY n" at first,
  PAIR's choices "MONO x 2", "ST (1->2)", "ST (2->1)", "ST (RESET)"; COMP's processor
  "NO.1".

- X32 ROUTING's out 1-16, aux out and P16 out tabs on the manual's encoders (pp.52-53):
  the output, the source's category (OFF, MAIN, MIX BUS, MATRIX, DIRECT OUT, MONITOR),
  the signal in it (push: ASSIGN), the tap point and an analog output's delay (push:
  on). HOME's gate and dyn second pages likewise (pp.47-48): the key filter's frequency
  (push: FILTER), its type (push: KEY SOLO), the source; dyn's KNEE pushed for PRE EQ.
- X32 MONITOR, talkback, oscillator and USB tabs on the manual's encoders (pp.60-62):
  the solo options, monitor delay and source trim; talkback's microphone, auto-dim,
  destinations (MIX BUS 1-16, LR, C/M) and latch, with `/config/talk` (A / B dim,
  latch and destinations on the engine's talk paths); one push switching the
  oscillator's F1 / F2; USB's STOP, PLAY, rewind, fast forward, REC and track list.

- Console oscillators play: the engine has a console's own oscillator, a tone or noise
  switched straight onto buses, mains and matrices (not a source patched to a channel,
  as the WING's OSC 1 and 2 are). The ProMix 01's (SEND3, SEND4, ST OUT), the 02R's
  (BUS 1-8, STEREO, AUX 1-8) and the X32's (`/config/osc` and `/-stat/osc/on`: level,
  F1 / F2, type and one destination, mix bus 1-16, L, R, L+R, M/C or matrix 1-6; the
  X32 screen's OSCILLATOR tab now drives it) follow their consoles' state through the
  panel, MIDI, OSC, shows and scenes.

- ProMix 01 scene memories by bulk dump: a request for scene 1-50 answers with the show's
  scene as it is stored, and a dump into one stores it there, neither recalling it (a
  dump past the show's last scene adds scenes up to it, "SCENE nn"; the scene's plugins
  are kept).

- The ProMix surface checked against photos of a console: its LCD laid out as the
  console's (FUNCTION, MEMORY with the scene's title, number and EDIT, and SEL CH down
  the left; EQ's curve over its G / F / Q table; COMP's lines, three programs, STORE,
  RECALL and EDIT, and its GR and OUT meters; PAN's knobs with a pair's over both; METER's
  scale), the LCD's yellow-green, red SEL and ON LEDs, and the fader caps' colours (ST IN
  light blue, RTN / SEND salmon, ST OUT dark grey). The dynamics presets are named in
  capitals, EQ gains read "0dB", 10 kHz "10kHz", and scene 00's title is "InitData".

- A ProMix 01 profile: a console can be configured as a ProMix 01 (16 channels; ST IN
  and the two effect returns; SEND1-4; ST OUT; no matrices, DCAs or mute groups; two
  FX slots), with its 128-step fader (+6 dB to -66 dB; the steps between are Sidecar's
  reading until measured), its port groups (INPUT 1-16, ST IN, 2TR IN; ST OUT, SEND
  OUT, REC OUT, MONITOR OUT, DIGITAL OUT) and names (ST IN, RTN1-2, SEND1-4, ST OUT).
  A show stores it; the Universal Mixer shows it. Its processing, effects and MIDI
  come with the next steps. C ABI 43: profile 3.
- The ProMix 01's processing: its 3-band parametric EQ (1/6 octave frequency steps,
  +-15 dB, Q from 1/6 to 3 octaves, the outer bands also shelving) on the inputs, ST
  IN, the effect returns and ST OUT, and its dynamics types (compressor, gate,
  ducking, with the User's Guide's ranges). A channel runs phase, EQ, dynamics and
  fader; on the sends and ST OUT the dynamics sit before or after the fader.
- A stepped fader's lowest step is a level of its own (a ProMix 01's -66 dB), not
  silence.
- `livedaw-promix`: the ProMix 01 over the engine. Its own state (the three dynamics
  processors patched into strips, channel pairs, fader groups A-D, SEND3-4 as a stereo
  pair, the oscillator), its panel's linked operations (a pair moves together, a
  group's faders follow the panel, a processor runs on the strip it is patched into
  with its key), its dynamics presets, and its 531 parameters as the User's Guide's
  Control Change table numbers them.
- The ProMix 01's effects: its two internal effects, fed by SEND1-2 and returning on
  RTN1-2, run its 15 effect types (reverb, delays, chorus, flange, phasing, pitch
  change, tremolo, auto pan and their combinations) with the User's Guide's
  parameters, on the WING's devices and a delay of Sidecar's own; its program
  libraries: the 30 preset effects programs, the dynamics presets and the 30 EQ
  presets, and the user programs.
- The ProMix 01's MIDI, its only remote: its 531 parameters by control change on the
  User's Guide's default map, in Channel or Register mode, with its value scaling and
  14-bit effect parameters; scenes by program change through its table; Yamaha bulk
  dumps and requests (the Edit Buffer, the program and control change tables, the
  setup, the user programs; the bytes inside are Sidecar's layout) and Key Remote.
  MIDI SETUP (channels, TX, RX, OMNI, ECHO) is kept in the app settings. C ABI 44:
  `ld_promix_*`.
- The ProMix 01's scene memories are the show's scenes, and keep its own state: a
  console's state beyond its strips (the ProMix's pairs, groups, dynamics processors,
  SEND3-4 mode) is the engine's (`Engine::console_text`), stored by shows and scenes
  (`<ConsoleState>`). Program change 128 recalls scene 00, the console's initial data.
- The ProMix 01 surface (a preview; SETUP > SIDECAR > INTERFACE: PROMIX 01, which makes the
  console a fresh ProMix 01 after asking): its 19 faders with SEL and ON (the console's 128
  steps; the inputs' send levels while a SEND function is shown; pairs and groups follow),
  its LCD and function keys (METER, PAN / PHASE, COMP, CUE, SEND1-4, EQ LOW / MID / HIGH,
  LIBRARY, GROUP, PAIR, UTILITY, MIDI), the cursor keys, ENTER and the PARAMETER wheel, the
  screen taking touch, SCENE MEMORY (STORE, RECALL, INC, DEC) and the stereo LED meter.

Phase 6, the Yamaha 02R (`docs/UNIVERSAL_MIXER.md`), begins.

- An 02R profile (Version 2): a console can be configured as an 02R:
  - 36 channels: MIC/LINE 1-16, LINE 17/18-23/24 stereo, TAPE 1-16
  - the two effect returns EFF1-2
  - 16 buses: BUS 1-8 and AUX 1-8, with AUX 7-8 feeding its two FX slots
  - STEREO
  - no matrices, DCAs or mute groups

  It has:
  - its 128-step faders: +10 to -90 dB on the channels, 0 to -120 dB on STEREO;
    the steps between are Sidecar's reading until measured
  - its port groups: INPUT 1-24, 2TR IN, the four card slots; STEREO OUT, AUX SEND
    1-6, 2TR OUT
  - its names: TAPE 1, EFF1, AUX 8, STEREO
  - its factory patch: the tape returns on card slots 1-2; BUS 1-8 on every slot's
    outputs

  A show stores it. Its processing, effects and MIDI come with the next steps. C ABI
  45: profile 4, and `ld_engine_fader_law_of` (a strip kind's fader law).
- The 02R's processing:
  - its 4-band parametric EQ: 120 frequencies in 1/12 octave steps, +-18 dB in
    0.5 dB steps, Q 10 to 0.1 in 41 steps, LOW also a shelf or a 12 dB/oct HPF,
    HIGH a shelf or an LPF
  - its dynamics processor's six types: compressor, expander, compander hard and
    soft, gate, ducking, with the guides' ranges; release, decay and hold on
    Sidecar's reading of their tables

  Its chains:
  - the inputs and tape returns: attenuator and phase, EQ, dynamics, delay, fader
  - the effect returns: no dynamics
  - BUS 1-8: dynamics and master
  - AUX 1-8: the master alone
  - STEREO: EQ, dynamics, fader
- `livedaw-02r`: the 02R over the engine.
  - Its own state: channel pairs (MIC and TAPE), aux and bus pairs, fader groups A-D
    and mute groups E-H, pan gangs, the stereo strips' two pans, the direct outs, the
    dynamics processors' KEY IN, ST.LINK and IN, fader recall safe and fade times, the
    oscillator.
  - Its panel's linked operations:
    - a pair copies one side and moves together, its processors linked
    - a fader group's faders move by as many steps
    - a mute group's ON keys flip
    - a gang's pans keep their offset
    - an aux pair's sends follow
  - Its values as the console has them: fader steps on the channels' and STEREO's
    laws, pans, attenuation, delay in samples, aux sends with EFF1 only to AUX 8 and
    EFF2 only to AUX 7, routing to BUS 1-8 and ST, EQ and dynamics steps, the
    processors' keys.
- The 02R's effects: its two internal effects, fed by AUX 7-8 and returning on
  EFF1-2, at first REVERB HALL1 and DELAY L-C-R 1.
  - 17 effect types with the guide's parameters and ranges:
    - reverb, early reflections, gate and reverse gate reverbs
    - L-C-R delay, echoes with cross feed, mono and stereo delays
    - chorus, flange, symphonic, phasing, tremolo, auto pan, pitch change
    - their combinations
  - They run on the ProMix 01's algorithms where the types are the same, and the
    WING's ambience, gated and reverse reverbs.
- The 02R's libraries:
  - the 40 EQ presets, the 40 dynamics presets and the 40 effects presets, with
    their values
  - user programs in the rest of each library's 128
  - recalling one onto a strip or an effect, and storing one
- The 02R's MIDI, its only remote:
  - SysEx parameter changes and requests on its edit buffer: Sidecar's addresses,
    since the guides give none; a byte or a bit.
  - Recall and store of scenes and library programs; key remote; fader and
    encoder remote.
  - Control changes through its CC assign table (the 02R preset map).
  - Scenes by program change through its table (97 recalls scene 0, the initial
    data).
  - Yamaha bulk dumps and requests: scenes, the edit buffer, the program change
    and CC tables, the setup, the library programs. The bytes inside are
    Sidecar's layout.
  - MIDI SETUP is kept in the app settings. The console's changes go out as
    parameter changes. C ABI 46: `ld_y02r_*`.
- Fixed: a frequency or time of 1000 or more in a model's value list (the ProMix
  01's and the 02R's EQ frequencies of 1 kHz and up) showed as "INF"; only a ratio's
  infinity does.
- The 02R's scene memories are the show's scenes (1-96; program changes and the
  SysEx recall and store reach them). They keep the console's own state: pairs,
  groups, gangs, keys, recall safe, fade times, the oscillator. Scene 0 is a new
  02R's mix. C ABI 46: `ld_y02r_state` / `ld_y02r_set_state` for that state.
- The 02R surface (a preview; SETUP > SIDECAR > INTERFACE: 02R, which makes the console
  a fresh 02R after asking):
  - its 21 faders with SEL and ON, on the console's 128 steps and scales; FLIP puts
    TAPE 1-16 on faders 1-16; in an AUX function they are the sends to it
  - the encoders of TAPE 1-16 and EFF1-2
  - the display with its CONFIGURATION, MIXING and AUX function keys: VIEW, phase
    / ATT, DELAY, PAN, ROUTING, EQ with its curve and library, DYNAMICS with its
    keys and library, METER, GROUP, PAIR, SCENE MEMORY, UTILITY, MIDI, AUX 1-8
    (AUX 7-8 with their effect's library)
  - the cursor keys, ENTER and the wheel
  - the SELECTED CHANNEL section: ROUTING, AUX and SEND LEVEL, PAN, EQ
  - SCENE MEMORY and the stereo meter
- The X32, ProMix 01 and 02R surfaces look like their consoles (compared with photos
  of the real ones; drawn by Sidecar, no maker's artwork):
  - the X32: its titanium frame, slate sections outlined in silkscreen, light rubber
    keys lit in their colours, knurled knobs, moulded fader caps
  - the ProMix 01: its charcoal panel between silver rack ears, the yellow-green LCD
    with dark pixels, cream keys with LEDs, the function keys in their colour groups,
    ivory caps (ST IN black, RTN / SEND blue-grey, ST OUT red)
  - the 02R: its dark grey panel between wooden cheeks, the blue LCD, cream keys
    with LEDs, ivory caps and STEREO's red, encoders with LED rings, the big wheel

  Their controls sit where the consoles have them:
  - the ProMix 01: the function keys in its 4x5 block left of the LCD (SCENE
    MEMORY's keys among them), the scene number on the LCD, the meter beside it,
    ENTER, the cursor keys and the PARAMETER wheel at the top right, the channel
    numbers above SEL
  - the 02R: the SCENE MEMORY and SELECTED CHANNEL readouts over the display
    access keys left of the display, the meter beside it, the SELECTED CHANNEL
    section, the TAPE and EFF encoders above their fader columns, SCENE MEMORY,
    the wheel, the cursor keys and ENTER at the bottom right
  - both: their analog rows (PAD, GAIN, MONITOR OUT, PHONES) along the top, drawn as
    on the panel; they aren't controls

  Their faders and knobs are the consoles' own:
  - the X32: tall brushed-aluminium caps; endless encoders in amber LED rings (lit
    from the centre for pan), MONITOR, PHONES and TALK pots with pointers; each
    strip top to bottom as on the console (SELECT, the meter, SOLO, the scribble
    strip, MUTE, the fader), its meter one LED column in a dark window: COMP,
    CLIP, -6 ... -60, GATE (COMP and GATE lit while the compressor or gate works)
  - the ProMix 01: moulded ivory block caps with the centre groove, the PARAMETER
    wheel knurled with its finger dimple
  - the 02R: a darker panel, light grey caps, its encoders smooth domes without
    rings (EQ Q, F and G green), the MIXING and AUX keys orange, CONFIGURATION's
    light grey, the dimpled data wheel

  The X32's screen looks like the X32's (compared with photos of its screen):
  black glass, light blue blocks with navy text and darker blue edges, yellow
  for what is on; the selected channel's tile, the scene number and clock in
  amber along the top, the tabs as folders; home's In, Gate, Ins, EQ,
  Dynamics, Out and Busses blocks with their graphs, check boxes, chips, value
  boxes and knob icons, the DCAs and mute groups under them; each encoder's
  cell with its icon, value and push; the main and M/C meters (CLIP to -57) in
  the bezel beside the screen; the six encoders under it without rings

  More of the X32: the SELECT keys blank with SELECT printed under them; the M/C
  meter beside the screen shows the solo bus while anything is soloed; the
  gate, dyn and eq graphs in the screen's look (orange curves on black, in a
  light blue block); the screen keys lit in their own colours (HOME green,
  METERS orange); a bus's meter has PRE in GATE's place, lit while an input
  sends to it pre-fader

  The X32 surface laid out after a photo of the whole console:
  - the keys blank with their names printed under them (the screen keys keep
    theirs); SELECT lit mint, SOLO yellow, MUTE red, the layer keys mint
  - CONFIG / PREAMP (GAIN, the LEVEL meter, FREQUENCY, 48V, polarity, LOW CUT)
    over GATE and DYNAMICS (THRESHOLD, GATE / DUCKER, COMP / EXP, the GR meter
    between); EQUALIZER (Q, FREQ, GAIN, MODE with the type LEDs HCUT ... LCUT,
    HIGH, HIGH MID, LOW MID, LOW, a second press for HIGH 2 / LOW 2 on a bus);
    BUS SENDS (four encoders, the bank keys beside them); MAIN BUS (LEVEL, MONO
    BUS, PAN / BAL, STEREO BUS); RATIO and TYPE are the screen's now
  - the display in its black housing, MONITOR and TALKBACK as two columns
  - the input layer keys down the left of the faders, SENDS ON FADER and the
    group layer keys between the banks, the strips' scribble strips as backlit
    LCDs and their faders in darker bays, the main's COMP LED and CLEAR SOLO
  - SCENES (PREV, NEXT, UNDO, GO, VIEW), ASSIGN (four encoders in a row, their
    LCDs, keys 5-12, SET A B C), MUTE GROUPS 1-6 and the palm rest at the right
- Fixed: the console surfaces' fader scales showed a garbled character for -inf.
- The X32 surface draws less: the display repaints its status bar, encoder row
  and bezel meters only when what they show changes, the meters keep their
  marks in an image, and the panel's texture, sections and silkscreen are
  drawn only where a repaint reaches. Idle on the X32 the app takes about a
  fifth less CPU than before the surface's new look (measured with
  SidecarMonkey --idle); the new look doesn't change the audio's overruns
  (idle and active A/B runs, same seeds).
- The console surfaces checked against their manuals' panel drawings:
  - X32 (User Manual pp.13-17, Quick Start p.7): the USB RECORDER block over
    MAIN BUS (its port, ACCESS LED, VIEW); REMOTE at the top of the input layer
    keys; LAYER's up and down beside the sixth encoder, PAGE SELECT under the
    screen keys; VIEW in MONITOR, TALKBACK and ASSIGN; the LAMP socket, MIN and
    MAX on the pots, PHONES' "(JACKS IN SIDE CAPS)"; the GR meter across the
    GATE and DYNAMICS boundary; EQUALIZER's type legend with each filter's
    shape, MODE beside it, HIGH 2 and LOW 2 bracketing their keys; the group
    strips' meters printed in every layer (PRE at the bottom, unlit on a DCA);
    "AUX IN / USB FX RETURNS" and "MATRIX 1-6 MAIN C" on two lines; the fader
    scale signed, with -50. The keys Sidecar has nothing behind (REMOTE, the
    new VIEWs, the recorder's) are dark.
  - ProMix 01 (User's Guide pp.2-4): the function and scene keys blank and
    without LEDs, their names printed and underlined over them (SEND 1, 2, 3, 4;
    EQ LOW, MID, HIGH; PAN/ø) in the panel's two dashed outlines; SEL and ON
    with round LEDs; FUNCTION, MEMORY, SEL CH and RTN 1 ... SEND 4 beside the
    LCD, its arrow at what the RTN/SEND strip is; the contrast knob; the meter's
    two columns of round LEDs with the scale between and L R under; ENTER an
    oval, the cursor keys triangles on the wheel's raised outline; each input's
    20dB PAD, its GAIN in a ring of ticks from -16 to -60; CUE/2TR IN; the
    outputs' 0, 10 and LEVEL; the vent over the faders; RTN/SEND named as
    printed; the fader scale's unlabelled ticks.
  - 02R (User's Guide pp.2-11, Getting Started p.4): DISPLAY ACCESS as its three
    boxes (CONFIGURATION with DIGITAL I/O, SETUP and AUTOMIX, dark; MIXING in
    the panel's order; AUX with EFF1 and EFF2), blank keys named under them; the
    scene and channel readouts, FADER STATUS and the MIC/LINE, TAPE/RTN and
    OUTPUT LEDs in the display's bezel; a 21 LED stereo meter, L STEREO R and
    CONTRAST; SELECTED CHANNEL's ROUTING, AUX (with its send level's LED column),
    PAN (L/ODD and R/EVEN choose the side it turns, an LED row) and EQUALIZER
    (LOW/HPF ... HIGH/LPF, Q, F and G on red readouts) with endless encoders;
    the monitor column (STUDIO, CONTROL ROOM, TALKBACK, SOLO, C-R LEVEL: MONO,
    DIM, SLATE, T/B, SOLO and C-R LEVEL work, the sources are dark); the analog
    inputs (+48V, A/B, 20dB, GAIN, PEAK, SIGNAL) and outputs (T/B, STUDIO and
    PHONES LEVEL, the phones jack); the tape row's names under its encoders,
    FLIP before EFF1 and EFF2 RTN; scribble strips; STEREO's cap like the
    others; STORE, up over down, RECALL in a row, the cursor keys on their oval.
  - The ProMix 01's and 02R's fader laws follow where their panels print the
    scale (measured on the drawings): 0 dB is step 100 on a ProMix 01 (was 115)
    and 101 on an 02R channel (was 103); STEREO's 0 dB stays at the top. Their
    MIDI follows the same law.
- The X32 surface's colours and keys checked against the photo of the whole
  console: a neutral dark grey top, its sections on black panels with their
  titles centred over them (GATE and DYNAMICS on one), each fader strip and
  layer column on its own black band; the keys mid-grey rubber (MUTE lit
  salmon, SET A and the mute groups the same); the encoders' rings red-orange;
  the screen keys black with their names in their colours; VIEW named in a
  small box over its key; DAW REMOTE; TALKBACK named under the LAMP socket;
  NO and YES under PAGE SELECT; MIC on the display's housing; EQUALIZER's
  printed loop joining Q, FREQ, GAIN and MODE; GO in its frame; MAIN boxed; a
  wider fader cap.
- More of the X32's panel: the scales printed round FREQUENCY and the EQ's FREQ,
  the dot scales round MONITOR, PHONES and TALK, the headphone sockets' marks
  and the speaker grille; the scribble strips laid out as the X32's (the number
  top left, the name along the bottom, the strip's icon at the right, the text
  smaller beside an icon); ASSIGN's numbers at its LCDs' left. MONITOR's and
  TALKBACK's VIEW open a MONITOR page on the display (the monitor and phones
  levels, DIM and its depth, MONO; the talkback level, TALK A, TALK B), and
  ASSIGN's VIEW an ASSIGN page (the set's encoders and keys, a push or a tab
  for SET A, B, C).
- The ProMix 01's LCD shows its 240 x 64 dot matrix; the 02R's STEREO strip is
  twice as wide as a channel's.
- The 02R display's pages (MAN chs. 8-12): a function's key again steps its
  pages, "n/N" in the title. SCENE MEMORY: the scenes' titles (ENTER recalls
  one), recall safe and fade times of every fader; GROUP: the fader groups
  A-D and the mute groups E-H, each on, and every channel's; PAIR: the
  channel pairs, then the aux and bus pairs; PHASE / ATT, DELAY, PAN and AUX
  1-8 for all channels (MIC/LINE, then TAPE, EFF and STEREO; ENTER flips the
  phase, the delay, PRE/POST), AUX's third page the selected strip's send,
  master and effect; METER's inputs, returns and outputs; the oscillator's
  assign to BUS 1-8, STEREO and AUX 1-8. DIGITAL I/O, SETUP and AUTOMIX open
  pages: SETUP's solo listen points (pre fader or after pan) are the engine's;
  DIGITAL I/O shows Sidecar's own clock and inputs, AUTOMIX that it isn't
  emulated.
- The ProMix 01's memory pages (pp.63-65): STORE and RECALL open the memory
  list (00-50, the current one marked "=", the chosen one moved by INC, DEC or
  the wheel); the key again or ENTER stores or recalls it, and a store opens
  TITLE EDIT (eight characters: the cursor keys choose one, the wheel changes
  it, ENTER keeps the title). UTILITY's pages: SEND3,4 CONFIGURATION and OUTPUT
  COMP PATCH POINT, OSCILLATOR, MEMORY PROTECT (memories 01-25 and 26-50; STORE
  refuses a protected one, "PROTECT" in the memory area) and BATTERY CHECK.
  MIDI's BULK page (the interval). CUE shows the selected channel at a glance
  (ON, group, phase, pan, EQ, fader, sends). GROUP pressed again steps A-D; an
  EQ band's key pressed again its F, G and Q.
- The X32's SCENES screen (manual p.65-66, SCENES' VIEW key): the scene list
  around the one the first encoder picks (the current one marked, skipped
  ones shown), GO, PREV, NEXT, SAVE, SKIP and NEW; PARAM SAFE, the sections a
  recall leaves alone. MONITOR's tabs (p.60-62): channel solo PFL or AFL and
  the monitor source; TALK A and TALK B (the key, auto dim, talk to MAIN or a
  bus); OSC (level, frequency, sine, pink or white, on). HOME's second layer
  (p.47): 48V of the strip's input, a DCA and a mute group picked and
  assigned by their pushes.
- The ProMix 01's libraries store user programs (pp.22, 36, 77): EQ LIBRARY, a
  SEND's effects list and COMP's list have a STORE field; the wheel picks a
  user program (EQ 31-50, effects 31-40, dynamics 11-20), ENTER opens its NAME
  (fifteen characters), ENTER again stores what's on the EQ strip, the effect
  or the processor. The user programs are the console's memory: the app keeps
  them in its settings, not in the show's scenes. C ABI 47:
  `ld_promix_store_program`, `ld_promix_library_text`,
  `ld_promix_set_library_text`.
- The 02R's libraries store user programs (MAN pp.52-60, 73-81, 95-111): EQ,
  DYNAMICS and AUX 7/8's library pages have a STORE field; the wheel picks a
  program a user may store into (EQ 33-128, dynamics and effects 41-128),
  ENTER opens its TITLE EDIT (sixteen characters), ENTER again stores what's
  on the strip or the effect. Kept in the app settings, as the ProMix 01's.
  C ABI 47: `ld_y02r_store`, `ld_y02r_library_text`, `ld_y02r_set_library_text`.
- The X32's USB screen (manual p.62-63; USB RECORDER's VIEW): Sidecar's USB
  recorder and player, PLAY, STOP, PREV, NEXT, REC, PAUSE REC, the file and the
  position; CONFIG the tracks and bits it records. ACCESS lights while it
  records or plays. SETUP's SCRIBBLE STRIP (the selected strip's colour and
  icon) and PREAMPS (four inputs at a time: gain, 48V) tabs (p.56-57).
- More console pages:
  - X32: the gate's and dynamics' keys on HOME's second layer (KEY FILTER,
    KEY SOLO, KEY SRC: SELF, a channel, or an aux, bus, main or matrix);
    METERS' RTA tab (the selected strip's bands and peaks, FREEZE); ASSIGN's
    set tabs edit the controls (an encoder's FADER, PAN or SEND with its
    target and bus; a key's MUTE, INSERT, MUTE GRP or SCENE GO).
  - ProMix 01: PROGRAM CHANGE ASSIGN (MIDI's third page, p.82), METER's PEAK
    HOLD (p.19), the effect returns' L and R meters beside a SEND's list.
  - 02R: Program Change Assign (MIDI's second page, MAN p.144), AUX 7/8's
    EFFECT EDIT (the effect's type and parameters).
  - The Program Change tables are kept in the app settings. C ABI 47:
    `ld_promix_pc_scene`, `ld_promix_set_pc_scene`, `ld_y02r_pc_scene`,
    `ld_y02r_set_pc_scene`.
- Control Change assign tables: the ProMix 01's CONTROL CHANGE ASSIGN (p.83,
  MIDI's fourth page: a parameter's bank and controller) and the 02R's (V2
  p.164, MIDI's third page: what a controller moves, FADER, ON, PAN or a SEND
  of a strip), kept in the app settings. The ProMix 01's MEMORY CONTROL CHANGE
  OUT (p.87: the edit buffer out as control changes) and its COMP page's gain
  reduction meter; the 02R METER's meter point (PRE EQ, POST EQ, POST FADER).
  The X32's LIBRARY has CHANNEL and EFFECTS tabs: the library's channel and FX
  presets, LOAD onto the selected strip or a slot, SAVE as a new one. C ABI 47:
  `ld_promix_cc_of`, `ld_promix_set_cc`, `ld_y02r_cc_target`,
  `ld_y02r_set_cc_target`.
- The 02R's CHANNEL LIBRARY (V2 MAN pp.48-50): VIEW's second page (VIEW
  again, or ENTER on VIEW's LIB): 64 programs of an input strip's settings
  (fader, ON, pan, attenuator, phase, delay, EQ, dynamics, routing, the AUX
  sends) in a scrolling list; RECALL onto the selected strip, STORE (its TITLE
  EDIT), CLEAR, EXIT. A recall, store or clear keeps what it replaced in the
  UNDO ("U:"), which RECALL puts back. The programs are kept in the app
  settings with the other libraries. C ABI 49: library 3 of `ld_y02r_recall`,
  `ld_y02r_store` and `ld_y02r_program_name`; `ld_y02r_clear_channel`.
- The 02R's SETUP has its four pages (V2 MAN pp.196-203): SOLO, AUX OUTPUT
  SELECT, and PREFERENCES 1 and 2, kept in the app settings. The preferences
  Sidecar acts on: AUTO EQ, PAN and ROUTING DISPLAY (the SELECTED CHANNEL's
  controls show their page; EQ and PAN on from the factory), AUTO SOLO
  DISPLAY ([SOLO] shows SETUP 1/4), STORE and RECALL CONFIRMATION (a scene's
  or a library program's store, clear or recall asks CANCEL / EXECUTE;
  CANCEL is the default, and a dialog left ten seconds cancels), INITIAL DATA
  NOMINAL (scene 0 puts the channel faders at 0 dB) and TOUCH SENSE SELECT
  (touching a fader or an encoder selects its channel). The hardware's (word
  clock, timecode, cards, automix) show as not emulated.
- The 02R's ENTER double-clicked on PHASE / ATT or DELAY (MAN pp.32, 34):
  every channel (MIC/LINE, TAPE, the effect returns; not STEREO) takes the
  attenuation and phase, or the delay, of the one with the cursor.
- The 02R's MS decoding (V2 MAN p.186): PAIR 1/2 has an MS switch beside each
  channel pair, VIEW 1/2 one for the selected channel's. On, the channels
  pair (if they weren't), the odd one is M and the even one S: L = M + S,
  R = M - S, the faders apart and both pans set to the centre to start from.
  Breaking the pair turns it off. In the engine, a strip's `ms_side` (a mono
  strip's right side panned with its polarity reversed), kept in shows and
  scenes. C ABI 50: `StripParams::msSide`; the 02R's state 18, `Ms`.
- The 02R's DELAY digit by digit (MAN pp.34-35): a UNIT field picks [Smpl] or
  [mSEC]; the cursor keys step through a channel's digits (the samples'
  thousands to ones, the ms' tens, ones and tenths; underlined) and on to the
  next channel, the wheel moves the digit under the cursor, and a tap on a
  digit puts the cursor on it. In ms, the nearest sample.
- Surround panning (`livedaw_dsp::surround`, for the 02R and later the
  DM2000): the 2+2, 3+1 and 3+2+1 modes and their speakers' buses, a
  constant-power law from a place on the pan graph (CSR splitting a front
  sound between the centre and the sides; 0.5 all three alike), the
  subwoofer's own level, and the seven trajectories. The guides give the
  behaviour, not the law: this one is Sidecar's.
- The 02R's SURROUND (V2 MAN pp.64-73): ROUTING 2/2 turns it on (always
  asked) in 2+2, 3+1 or 3+2+1. It takes BUS 1-4 (3+2+1: 1-6), each input's
  sends to them its speakers' gains, post fader; the channels' own routing to
  those buses waits and comes back when it goes off. ROUTING 3/4: an input's
  place (L / R, F / R), CSR, SW and whether it feeds the surround at all, on
  a pan graph with its group's places and the mode's speakers. ROUTING 4/4:
  a trajectory (left-right, front-rear, the two diagonals, the two
  semicircles, a circle), its WIDTH, DEPTH and OFFSET, MOVE the place along
  it, JUMP to a speaker. Kept in scenes with the console's state.
- The 02R's ST LINK (V2 MAN p.72) on ROUTING 4/4: a stereo channel's right
  side (LINE 17/18-23/24, the effect returns) or a made pair's other channel
  moves with it by one of the guide's eight patterns, in its order (the same
  way, mirrored front / rear, left / right or across either diagonal,
  opposite, a quarter turn either way; MIRROR L-R to start, Sidecar's
  choice), each channel keeping its CSR and subwoofer level; on by default. FAST moves four
  steps a click and isn't kept. Each speaker has its bus's output meter
  beside it. The surround buses sum to mono while SURROUND has them, so both
  sides of a stereo channel reach their speakers. A stereo channel with ST
  LINK off has both sides at one place.
- The ProMix 01's LCD pages closer to the manual's pictures: PHASE as N / R rings
  (R filled) beside the PAN link with the legend under them (p.20); SEND1 and SEND2's
  three programs, "(FADER = SEND1)", STORE, RECALL, EDIT and SEND: PRE / POST (p.33);
  CUE's channel view (p.52): the group, PHASE, PAN, CUE:, the fader and sends as
  faders under ON and PRE / POST, the EQ curve, the meter; GROUP's dot grid with the
  shown group's row inverted, the channels' numbers over it, BAL ADJ and SEL = GROUP
  (p.56); PAIR's boxes with the stereo mark and "CH1-2 :MONO x2" (p.58); EQ's ON/OFF
  boxed (p.21). The FUNCTION area's second line names GROUP's FADER, PAIR's STEREO
  and CUE's CH VIEW. UTILITY's and MIDI's pages (pp.81-91) as their pictures: the
  page's name in a hatched banner, SEND3,4 MODE:, the oscillator's ASSIGN lines, LEVEL
  fader and meters, MEMORY PROTECT's two lines, BATTERY CHECK's line, MIDI SETUP's
  table, PROGRAM CHANGE ASSIGN's "1= 01" table, CONTROL CHANGE ASSIGN's NO. NAME BANK
  NO. table, LOCAL ON/OFF's ruled cells, MEMORY CONTROL CHANGE OUT's lines.
- The 02R display drawn as the manual's pictures (V2 MAN pp.33-43, 107, 135-136,
  182-186): the title bar (the scene memory, the page boxed, the channel boxed) and
  the row of the 16 encoders and E1, E2 under every page; PHASE / ATT, PAN and AUX
  as knobs over their values and channel numbers (NOR / REV over ATT's, PRE / POST
  under AUX's; a stereo channel's two pans); DELAY as its table (Smpl and mSEC side
  by side, ON); ROUTING 1/2 as the matrix of every channel of the selected one's
  side against BUS 1-8, S and D (it was the selected channel's alone); GROUP's grids
  with ENABLE; PAIR's MS and hearts; SCENE MEMORY's framed fader recall safe and the
  fade time table; METER's scaled groups with the meter point as its three buttons.
  VIEW 1/2 (p.44): ATT with NOR / REV, PAN, GANG and MS at the left, the meter,
  fader, ON and groups, the eight sends' knobs, the EQ and dynamics thumbnails, LIB.
  SETUP's Solo (p.196) as its framed check boxes (Listen, the outputs' listen,
  Status; the solo safes said not emulated), PREFERENCES as their check-box lists,
  UTILITY's oscillator framed (p.209: On/Off, Level, Wave Form, BUS, Stereo and Aux
  Assign), DIGITAL I/O's fixed lines framed.
  EQ as its picture (p.51): the library at the top left, the curve with 100, 1K and
  10K beside the channel's meter, ATT and EQ ON, each band's Q, F and G knobs framed
  (HPF and LPF in LOW's and HIGH's); DYNAMICS (p.83): its settings framed, the
  transfer curve, the meter with GR, the processor's knobs framed with its type.
  The cursor's up and down move to the nearest field above or below. The other
  pages keep their tiles under the new title bar.
- X32 LIBRARY as the manual has it (p.58): CHANNEL's encoder 1 the elements a load
  recalls (head amp, configuration, gate, compressor, equalizer, sends), 2 a preset
  and LOAD, 3 SAVE (asks its name), 4 DELETE; EFFECTS the same with the FX slot on
  encoder 1; a new ROUTING tab saving and loading the routing (channel inputs,
  analog, aux, P16, card, AES50 and XLR outs, each on or off the recall list) as
  .lroute files in the library. SETUP > global: the sample rate (the audio
  interface's, pushed to confirm), the clock and the computer's time and date.
- X32 SCENES in the manual's six tabs (p.65-67): HOME (GO, SKIP a scene, NEW, a
  snippet of the current scene GO; PREV and NEXT on encoders 4 and 6, as Sidecar
  has no UNDO), SCENES (LOAD; SAVE into a slot with its name and note; EDIT a
  scene's name and note; IMPORT a library scene file, EXPORT one; save safes that
  leave sections out of a SAVE), SNIPPETS, PARAM SAFE as four columns of check
  boxes and CHAN SAFE as six (channels 1-16 and 17-32, aux and FX returns, mix
  buses, matrices, mains and DCAs, FX slots), an encoder each, and MIDI (a
  program change, control change or note sent when the scene is recalled).
  Sidecar's parameter safes hold for inputs and buses alike, so PARAM SAFE's third
  column holds the mute groups instead of the X32's mix buses.
- X32 SETUP > scribble strip and preamps as the manual's pictures (p.56): the
  scribble strip's five columns (channel, colour, icon, a list of names, the name),
  an encoder each, the list's name given by its push and the name typed with EDIT;
  the preamps' blocks of eight (Local 1-8 to 25-32), their gains in two rows of four,
  layer 1 the top row and layer 2 the bottom on encoders 2-5, pushed for +48V.
- X32 ROUTING: XLR OUT (the rear XLR outputs' blocks of four, p.54), formerly out 1-16's second layer; SETUP > config as the manual has it: encoder 1 the preferences (the solo's and the mono bus's), 4 what a link shares, 5 a scheme for the inputs' bus sends (all post, all pre, 1-8 or 9-16 pre; the schemes are Sidecar's).
- X32 SETUP in the console's tab order: CONFIG (the sample rate and sync, what a
  channel link shares: HA and delay, EQ, dynamics, fader and mute, as
  `/config/linkcfg`), NETWORK (this computer's addresses and the X32 OSC port)
  and CARD (the audio interface in the expansion card's place).
- X32 SCENES: SNIPPETS (the selected scene's snippets: GO, SAVE, NEW of the
  selected strip, FIRE with the scene, DELETE) and CHAN SAFE (each strip's
  recall safe, CLEAR ALL) beside HOME and PARAM SAFE.
- Dropout diagnostics: `LIVEDAW_TRACE_SLOW=<file>` logs every block that takes
  longer than it lasts (each stage's time and CPU cycles, the parallel stages'
  longest task and the wait for the workers); `LIVEDAW_WORKERS` sets the
  engine's worker count; SidecarMonkey `--cpus MASK` runs it on chosen
  processors. They found the monkey's dropouts on the Windows test server to
  be the machine's: the second socket's cores pause for up to 4 ms with
  nothing of Sidecar's running; on the first socket the monkey runs clean.
- The console surfaces (X32, ProMix 01, 02R) are laid out at 1920 x 1080 and
  scaled to the window, in proportion and centred: on a bigger screen (a 4K
  monitor) they show the same panel larger instead of stretching the gaps
  between fixed-size controls.
- X32: LINK, encoder 1's push on HOME's home and config tabs (p.45), links
  the selected strip's odd / even pair (`/config/chlink` and the others).
  Linked strips stay alike for what `/config/linkcfg` shares, also when the
  panel moves one of them; a pair just linked takes its odd strip's values.
  The config tab's SOURCE picks what feeds a channel or aux in (In 01-32, Aux
  1-6, USB L / R, the FX returns, the buses): turned, then patched by SELECT.
  Its sixth encoder picks the insert's slot (FX 1L-8R, AUX 1-6), put in by
  CONNECT.
  C ABI 48: `ld_x32_follow_links`.
- ProMix 01: GROUP's BAL ADJ (p.56): ENTER lets the group go so one fader can
  be set alone, the group's [SEL] keys flash, and it ends when GROUP goes.
  MIDI's LOCAL ON/OFF (p.86): with Local OFF and the page showing, the faders
  go out as their control changes (shown in hex) and CH1-16 [SEL] as program
  changes, moving nothing here. MEMORY CONTROL CHANGE OUT (p.87) picks its data
  types (ON/OFF, LEVEL, EQ, COMP, PAN, SEND, EFFECT, OTHERS) from the Edit
  Buffer. C ABI 48: `ld_promix_memory_cc_out`, `ld_promix_local_fader`,
  `ld_promix_local_select`, `ld_promix_bal_adj`, `ld_promix_set_bal_adj`.

  `ui::hw` (`src/ui/HardwareLook.*`) draws them. Keys, faders and knobs take a
  console's look when its surface asks; the Universal Mixer and the WING are
  unchanged.
- A new channel takes its console's factory settings, colour and name by its slot,
  not its id (a channel's id is 1-based and kept by shows).
- Fixed: the X32 surface's UNIVERSAL MIXER key crashed the app when the Universal
  Mixer hadn't been opened before (the FX rack's device faceplates were built from
  the X32's model tables instead of the WING's). SidecarMonkey can replay it:
  `--interface x32 --press "UNIVERSAL MIXER"`, and prints a crash's stack.

## [0.2.0] - beta

The first public beta of the Universal Mixer: Sidecar becomes a mixer core without one
console's limits (`docs/UNIVERSAL_MIXER.md`), on which the WING is an emulation and
other consoles will be. The engine and the show format take any configuration (any
number of channels, buses, mains and matrices, up to 256 DCAs and mute groups, 64 FX
slots), and a strip's processing is a chain that can hold extra blocks and tap points.
Its own interface, the **Universal Mixer**, is a preview beside the WING surface
(SETUP > SIDECAR > INTERFACE): a MIXER view for a console of any size with the selected
strip whole across the top, an inspector for a strip's chain and sends, curves with
handles, ROUTING, FX RACK, MONITORING, SCENES, RECORDER and SETUP. The WING surface
works as before and shows the Universal Mixer's blocks on HOME > CHAIN.

Also new: the audio interface as a device of its own in ROUTING, a SIDECAR tab on
SETUP for everything a WING doesn't have, a plugin overview window, more FX slots,
direct outs, multichannel WAV import in the multitrack editor; and fixes for a crash in
the plugin overview, OPEN EDITOR on the INS screen, the display jumping to the EQ, and
the WING DSP measured against a WING COMPACT.

Shows saved by this version (format 4) don't open in Sidecar 0.1.x; shows from 0.1.x
open as before.

### Added

- Universal core, Phase 1 step 1: the console's counts are a configuration
  (`ConsoleConfig`, `Engine::configure`; the WING's by default). Up to 256 DCAs and
  256 mute groups (the WING uses 16 and 8): strip memberships, engaged mute groups,
  source mute groups, recall scopes, global safes and snippets hold any of them, and
  shows store them (as before while only the first 64 are used). C ABI 25:
  `ld::GroupMask` / `ld::GroupBits`, `ld_engine_counts`, `ld_engine_configure`,
  `ld_engine_set_num_dcas`, `ld_engine_set_num_mute_groups`.
- Universal core, Phase 1 step 2: any number of mains and matrices (up to 256 each;
  the WING has 4 and 8). A strip's sends to them are stored by destination, like
  its bus sends, instead of a fixed block of 4 and 8. Shows save a strip's sends to
  the console's mains and matrices. C ABI 26: `ld_strip_main_send`,
  `ld_strip_matrix_send` (C++ `strip.mains[i]`, `strip.matrix[i]`).
- Universal core, Phase 1 step 3a: a strip runs its processing as a chain of
  blocks and tap points (`chain.rs`); the WING's strips and its 24 process orders
  are templates of that chain. The sound is unchanged (checked byte for byte).
- Universal core, Phase 1 step 3b: extra processing blocks (filter, EQ, dynamics,
  insert, delay; up to 32 per strip) anywhere in a strip's chain, saved with the show
  (`<Chain>`) and recalled by scenes with their section. Engine and show only for
  now; the C ABI and the new interface come later.
- Universal core, Phase 1 step 3c: tap points anywhere in a strip's chain (up to 32
  per strip); any send to a bus, main or matrix can take its signal at one. Saved with
  the show. C ABI 27: the sends' `tap`.
- Universal core, Phase 1 step 5: tests that run a console that isn't a WING (48
  channels, 24 buses, 2 mains, 6 matrices, 32 DCAs, 12 mute groups, extra blocks and
  taps) end to end, in Rust and in C++, through a show save and load.

- Universal Mixer, Phase 2 step 1: the window is a shell around an interface ("skin"):
  the WING surface, unchanged, or the Universal Mixer's own interface, a preview for
  now. SETUP > SIDECAR > INTERFACE switches between them at once (an app
  preference). A show of a console that isn't a WING opens in the Universal Mixer;
  choosing WING then offers to make it a fresh WING. The monkey test can start on the
  Universal Mixer (`--interface universal`).
- Universal Mixer, Phase 2 step 2: a strip's chain in the C ABI (version 28) and the
  C++ facades, for the Universal Mixer's chain editor: its steps in processing order
  (`ld_strip_chain`), its extra blocks with their parameters and meters
  (`ld_strip_block`), adding, moving and removing blocks and user taps, clearing the
  chain, a block's model and its curve (`ld_strip_block_response`), and
  `ld_engine_set_config`. C++: `Strip::getChain`, `getBlock`, `insertBlock`,
  `moveBlock`, `removeBlock`, `clearChain`, `insertTap`, `moveTap`, `removeTap`,
  `setBlockModel`, `getBlockResponse`; `MixerEngine::setConfig`.
- Universal Mixer, Phase 2 step 3: the Universal Mixer's first view. Its MIXER view
  has a column for every strip of the console, whatever its size (pan, MUTE, SOLO, the
  fader with its output meter, the strip's colour, label and name; a tap on the name
  selects the strip), scrolled sideways, with keys that show one kind of strip at a
  time. Touch first: controls move by how far a finger travels and never jump, a double
  tap resets them, keys are at least 44 px. It is drawn with a dark theme's design
  tokens (`src/ui/universal`). The top bar shows the console's counts and a key back
  to the WING surface.
- Universal Mixer, Phase 2 step 4: the inspector. Selecting a strip in the Universal
  Mixer opens it beside the mixer. CHAIN lists the strip's processing in order (its own
  blocks, the fader between PRE-FADER and POST, extra blocks and user taps): ADD puts a
  filter, EQ, dynamics, insert or delay block or a user tap after the selected row,
  REMOVE takes one away, and a drag moves it. The selected block's editor has its keys,
  model, curve, gain reduction and every parameter of its model, for the strip's own
  blocks and extra blocks alike. SENDS has a row per bus, main and matrix the strip can
  send to, with ON, level and where the send takes the signal (PRE, POST, GRP or one of
  the strip's user taps).
- Universal Mixer, Phase 2 step 5: ROUTING, FX RACK and MONITORING, shown by keys under
  the top bar. ROUTING is a patch matrix: INPUTS patches the channels' and aux's
  inputs from a group of sources, OUTPUTS feeds the interface's outputs from any bus,
  main or matrix side or the monitor paths, for a console of any size. FX RACK has a
  tile per FX slot and the selected slot's device, plugins and controls (the device's
  front panel or its parameter list). MONITORING has the speakers' and phones' paths
  and the solo settings, with CLEAR SOLO.
- Universal Mixer, Phase 2 step 6: SCENES (GO, PREV, NEXT, ADD, STORE, RENAME, UP,
  DOWN, REMOVE), RECORDER (the recorder pages and the multitrack editor) and SETUP: the
  console's configuration (channels, aux, buses, mains, matrices, DCAs, mute groups,
  FX slots) changes without a reset. New ones are added at the end and everything else
  stays; fewer take from the end after a warning, keeping a bus, main or matrix that
  something sends to, a DCA or mute group with members and an FX slot in use. AUDIO
  (the audio device) and PREFERENCES too. C ABI 29: `ld_engine_set_num_strips`.
- SETUP has a **SIDECAR** tab (gold) for everything a WING doesn't have, behind three
  keys: **SETTINGS** (the console's size, the surface's finish, the console model, the
  interface WING / UNIVERSAL, the audio device and the outputs' protection, which were on
  GENERAL and AUDIO), **PLUGINS** and **LOG** (the tabs they were). GENERAL's CONSOLE
  keeps the console's own name, software and data folders; UTILITY opens SIDECAR's LOG.
- The audio interface is a device of its own in ROUTING: a page named after its driver
  (ASIO, CORE AUDIO, ALSA, JACK, WASAPI; the WING surface's fourth ROUTING key, the
  Universal Mixer's third ROUTING tab) patches each interface input to a console input
  port (LOCAL IN, AES50 B / C) and each interface output to a console output port, with
  the driver's channel names. Straight through by default, as before; STRAIGHT puts it
  back. The patch is the computer's, kept per driver and device in the app settings.
- Universal Mixer: a **channel panel** across the top of MIXER shows the selected strip
  whole (input, filter, gate, EQ and dynamics with their curves, its chain in order, its
  sends a line each); **CHANNEL** hides or shows it. The curves have handles there and
  in the inspector: EQ bands (frequency and gain by dragging, Q with the wheel, default
  with a double tap), a filter's cuts, and a dynamics block's threshold and ratio (a
  gate's range) on its transfer curve, a new view with its gain reduction. MODEL and FX
  RACK's DEVICE open a grid of the models' front panels (the WING's pictures). Its
  screenshots are rendered at 1920 x 1080. A list's device tag no longer covers its value
  when the list is narrow.
- The WING surface shows the blocks and taps added in the Universal Mixer: HOME has a
  **CHAIN** tab of Sidecar's (after TAGS; **CHAIN +n** while the strip has n of them)
  listing the strip's processing top to bottom, a row per block with its model or
  setting and ON, the added ones in gold; the block column on the left has a gold **+n**
  notch under the block they follow. What a skin shows of a chain is shared (`ui/ChainSummary`), for the
  consoles to come.
- Universal Mixer, Phase 2 step 7 (Phase 2 complete): the user guide's chapter *The
  Universal Mixer (preview)*, view by view with screenshots; the README shows it, and the
  start-up notice says what it covers. The screenshots are rendered by a test category of
  their own (`LiveDAWTests Screenshots`) on a demo console.
- The Universal MIXER view's strips have an overview above a shorter fader: input
  level, GATE and DYNAMICS gain reduction, the EQ curve and output level. **FADERS**
  hides or shows the faders at any moment, the overview then taking the column.
- The Universal Mixer tags the parts that come from one console's emulation with its
  name (WING: the strip's own blocks, the model lists, front panels, routing groups,
  send modes, monitor sources, solo modes and the fader law), ready for the other
  consoles to come.
- SETUP > GENERAL > ABOUT (and the start-up notice) links the Sidecar Discord server
  for questions, setup help and feedback: a Community section and a **DISCORD** key
  that opens the invite in the browser.
- The multitrack editor imports multichannel WAVs, such as the takes an X32 (X-LIVE
  card) or a WING (WING-LIVE card) records to its SD cards: each channel becomes a
  mono track. A take the card split into 00000001.WAV, 00000002.WAV, ... is joined
  back into one clip per track, named after its session folder. Drop the session
  folder on the editor, choose it with FILE > Import a folder, or pick its WAVs with
  FILE > Import audio. Before, only the first two channels were imported.
- **More FX slots**: SETUP > SIDECAR > FX SLOTS sets 16 (WING, the
  default), 32, 48 or 64. EFFECTS, the inserts' SELECT FX PROCESSOR window, the scene
  SCOPE and GLOBAL SAFE windows, ROUTING's FX SEND group, custom controls and the plugin
  latency list follow, in banks of 16. Fewer slots never drop a slot in use; a show
  that uses more brings them along. The WING and MIDI remotes see FX 1-16.
- **Plugin overview** (SETUP > SIDECAR > PLUGINS > PLUGIN OVERVIEW, Ctrl+Shift+P): every plugin
  of every FX slot in a window of its own, for a second screen during a show. CARDS
  with a picture of each plugin's editor, or a compact LIST; each slot's state, the
  strips using it, IN / OUT meters, CPU and latency; faults and bypass stand out. The
  plugin windows take the pictures while they are open, CAPTURE ALL takes them all.
  The window remembers its screen, size, layout, ON TOP and text size, and reopens at
  start-up.
- **Direct outs**: ROUTING > OUTPUTS has the source groups CH DIRECT (channels and
  aux) and BUS DIRECT, each tapped PRE FADER or POST FADER, to any output or RECORDER
  port. Saved with the show, recalled with ROUTING.
- **Monkey tests**: a *Monkey* unit test (random FX slot, plugin, direct out, scene
  and plugin overview operations while audio runs) and `SidecarMonkey`, which drives
  the whole program's UI with random input on the virtual audio device and checks for
  crashes, non-finite audio, stalls and audio dropouts. `SIDECAR_HOME` puts Sidecar's
  settings and documents in another folder (test runs).

### Changed

- **Show format 4.** Shows now store the console's configuration (how many channels,
  aux, buses, mains, matrices, DCAs, mute groups and FX slots it has) and the profile
  they were made for (`wing`), and a load rebuilds that console first. Shows saved by
  this version don't open in Sidecar 0.1.x; shows from 0.1.x (format 3) open as a
  WING, as before.
- The C ABI is version 24: 64 FX slots in `EngineControls`, `ld_engine_num_fx` /
  `ld_engine_set_num_fx` / `ld_engine_fx_in_use`, 64-bit FX masks in `ScopeC`, and
  `kDirectSourceBase` direct-out codes.

### Fixed

- From a macOS user's report, all platforms:
  - A channel's INS screen: **OPEN EDITOR** did nothing. The effect's front panel,
    unused with a plugin in the slot, lay over the key and took the press; it now
    shows only for a device.
  - The display jumped to a channel's EQ (or another block) by itself: the pointer
    passing over one of the channel strip section's encoders counted as touching it,
    and CH STRIP TOUCH SELECT followed. Pointing at an encoder now only shows its slot
    on the section's small screen; a grab or a wheel turn is the touch.
  - EFFECTS, a plugin's slot: **OUTBOARD** unloaded the plugin at once; it now asks
    first. The plugin's name and its maker / format / LOAD line lay under the SOURCE
    keys; they are under them now. After a plugin was loaded the page still said NO
    PLUGIN LOADED until something else repainted it; it shows the plugin (and its
    LOAD) as it changes.
  - The tests now link the whole UI (the WING surface's screens and pages too), and a
    test of their own (*WING surface UI*) drives these as a user would.
- The plugin overview could crash Sidecar when a plugin was removed while it was open.
  It looks at the FX slots 15 times a second and draws what it saw; a paint that came
  between a plugin's removal and the next look asked the deleted plugin its name (to
  find its picture). It now uses the name it read at its look and never touches a
  plugin that may be gone. The *Monkey* test found it (seed 2 on Windows; CI's macOS
  runs with the default seed).
- CI's Linux and macOS test jobs had failed since 1 November, so no release could be
  published. On macOS it was the plugin overview's crash above. On Linux the tests that
  open windows (the plugin overview's, the *Windows* test) crashed without an X display;
  they now skip their windows there, and the *Monkey* test runs without the overview.
  The *Monkey* test also runs past its 4 s, up to a minute, until it has done 300
  operations: the sanitizer builds are slower and managed only 179. It gives the plugin
  overview up to 2 s to catch up before it counts a mismatch (on macOS its timer ran
  more than 120 ms late). The scribble strips' descender test, never run on macOS
  before, allows a row less of a "g"'s tail there, where CoreGraphics draws the text.
  A timing check of the WING effects (`tails_end_cleanly`: silence must cost no more
  than sound) took one measurement and failed when a shared runner hiccuped; it now
  takes the best of three.
- A crash of `LiveDAWTests` (a fault, an abort, an uncaught exception, a pure virtual
  call) prints the test it happened in and the stack, in a CI log too; on Windows the
  tests are built with symbols, so the stack names the functions.
- The macOS (AppleClang) build compiles without warnings. The strip and DCA name
  windows had a `setName` of their own (it names the strip, the DCA) that overrode
  JUCE's `Component::setName` by accident; it is `rename` now. The rest: lambda
  captures, parameters and loop bindings that hid other names, two unused members, the
  multitrack editor's FILE helpers private to their file, and the linker's note about
  the Rust library's duplicate `-lSystem` (quieted where the linker knows the flag).
- The Linux (gcc) build compiles without warnings again: the main and matrix send
  indices are cast where the sends became a bank of any size, the Universal Mixer's
  switches name every kind of chain step, and the MIXER view initialises its members
  in order.
- The Windows (MSVC) build compiles without warnings again: number conversions, local
  names that hid a class's members and a constant condition in the UI and the tests,
  which only a full rebuild showed, and the plugin sandbox's intended cache-line padding.
- The whole-program monkey test (`SidecarMonkey`) could crash itself (0xC0000409) on a
  click it sent to a pop-up's drop shadow. Windows sends a shadow's window no mouse
  input; sent one anyway, the press reached the window under it, dismissed the menu,
  and the shadow's window was deleted before the click's mouse-up went to it. The
  monkey now leaves windows that take no mouse input alone and sends nothing more to a
  window an event closed. On Windows its log also names the stack of an abort, an
  uncaught exception or another C runtime fail-fast (its build has symbols). Sidecar
  itself wasn't at fault; a test (*Windows*) shows the case.
- Importing a WING snapshot no longer patches two of its inputs to one port. The WING's
  LOCAL, AUX, StageConnect, USB, MOD and AES/EBU inputs land on the ports AES50 A (or B)
  has here, so a show with the drums on A 1-5 and the vocals on the console's LOCAL 1-5
  played the drums on the vocal channels, under the drums' names. The input that would
  share a port now moves to a free AES50 B / C port (a stereo pair to a free pair) with
  its name, colour, icon, gain and mode, and every patch to it (channel, ALT, outputs)
  follows; the import log lists each move.
- Windows: when Windows couldn't resolve the Documents folder (redirected to a
  network share or to OneDrive and not there), Sidecar put its folders at the root
  of the drive (`C:\Sidecar\Recordings`), which a standard user can't create, and
  recording failed with "Couldn't create ...". The default is now the `Documents`
  folder in the user's folder (or app data), and a project that can't be created
  says to choose another folder in FILE > Projects folder
  ([#12](https://github.com/Audbol/Live-Daw/issues/12)).
- WING REMOTE: OSC apps on the same computer as Sidecar (wosc and the like) get
  Sidecar's replies and changes. On Windows, an app could bind its own address on
  Sidecar's OSC or discovery port and then receive everything sent to it, so
  Sidecar never saw the app as a client. Sidecar now holds those ports
  exclusively ([#6](https://github.com/Audbol/Live-Daw/issues/6)).
- WING REMOTE: an OSC app that sends each request from a new port is listed once,
  under the port it asked replies on, not once per request. Its own changes are no
  longer pushed back to it when they arrive from a different port.
- Measured against a WING COMPACT (firmware 3.1.1) with `tools/wing/audio_compare`:
  - An FX inserted on a mono channel stays stereo after the insert, each side at the
    mono pan law, as on the WING (a plate came out summed to mono, the chorus lost
    its stereo sweep).
  - The compressor models work on the crossover band in DYN XOVER LO / HI: they key
    from the band and reduce only the band, which DEPTH doesn't limit. Before, the
    crossover was ignored and the whole signal was compressed.
  - The dynamics' GAIN applies to the processed signal ahead of MIX: COMP 4:1 at MIX
    50 % and GAIN +6 dB gains +3.5 dB below the threshold, as on the WING (it gained
    +6 dB).
  - The dynamics models that sense peaks (SOUL BUS, 76 LIMITER AMP and the other
    non-RMS ones) hold the peak for 5 ms instead of releasing in every trough of a
    tone: SOUL BUS reaches 63 % of a 40 dB step in 10.7 ms (WING 9.3 ms, before 19.3).
  - STEREO DELAY's FEED follows a square law: 40 % gives repeats 16 dB apart (0.40²),
    as on the WING; before, they were 8 dB apart.
  - STEREO CHORUS's DEPTH at 100 % sweeps +-50 % of the delay (it swept +-40 %).
- An insert pointed at an FX slot past the ones in use some other way than a show or
  scene (a channel preset, a copy, a remote) now brings that slot into use, so the
  EFFECTS rack always shows every slot that runs (found by the monkey test).
- The console's autosave rewrote the whole show, and its backup, every 30 seconds
  even when nothing had changed: constant writes to a USB stick or SD card while
  the console only plays audio. A save whose show is the same as the file on disk
  now leaves the file alone (also for presets and library files); a changed show is
  still written to a verified temporary file, with the previous version kept as
  `.bak`.
- The multitrack editor's autosave rewrote a project with unsaved changes every
  30 seconds until it was saved, even when nothing had changed since the last
  autosave. It is now written only when it changes, and SAVE of an unchanged
  project leaves the file alone.

## [0.1.6] - beta

The fourth round of matching Sidecar to a real WING, cross-checked against WING Edit
3.3.4, the WING manual (2025-10-20) and the remote protocols document, with answers
from an owner of a full-size WING and a WING COMPACT: the WING COMPACT is a second
console model with its own surface, keys and custom control sets; every effect was
measured against its original, and its page's meters, needles, gain reduction and
spectrum move with the signal; the routing gains user signals and patches, inputs
straight to outputs and live meters; meters use the WING's ballistics; the EXTERNAL
effect inserts outboard gear; the multitrack editor can keep imported files where
they are. The README has use cases and new screenshots.

### Added

- **The meters on the reverb, delay, modulation and pitch pages move with the
  effect**: VINTAGE ROOM's INPUTS / OUTPUTS ladders, DIMENSION CRS's OUTPUT ladder,
  TAPE DELAY's two VUs (the record level onto each tape), VINTAGE PLATE's VU (the
  drive into the plate), STEREO DELAY's level tube, DUAL PITCH's output LEDs,
  TREMOLO/PANNER's L-C-R needle, the envelope graphs of PHASER and TREMOLO/PANNER
  (filled to the envelope's level) and PITCH FIX's LED over the note it hears, through
  the FX slots' readouts (`ld_fx_readouts`). The modulation devices report their LFO's
  position, envelope and needle; an LFO graph shows where the LFO is.
- Tap tempo also sets OILCAN DELAY's TIME dial and BBD DELAY's DELAY dial (their
  delay time from the taps); the pitch devices' DELAY no longer takes the tap.
- **Live meters on the FX pages** (dynamics, EQ and utility devices, channel strips,
  EXTERNAL): C5-COMBINATOR's band screen shows each band at its gain as it is now
  (n-GAIN plus its live gain reduction and SBC makeup) over the input's third-octave
  spectrum; 2-BAND DEESSER's band LEDs light while their band de-esses; ULTRA ENHANCER's
  STEREO and MONO VUs, PSYCHO BASS's low-band and harmonics meters, TAPE MACHINE's
  record-level VU, SUB MONSTER's band meters, VELVET IMAGER's input / output / mid-side
  LED meters, correlation bar and LF vector scope, SPEAKER MANAGER's live DYN EQ curve
  (as set in blue, as it is now in orange) and the EXTERNAL EFFECT's SEND and RETURN
  meters all follow the signal. Each channel-strip section's meter shows its own
  block (the gate, the compressor, the 1176, the leveler...), the mastering strip's
  tape VU, enhancer VUs and limiter LEDs their units'. Levels move with the app's
  meter ballistics (full peak at once, 60 dB/s down); gain reduction meters follow the
  processor. The pages poll ~30 times a second and repaint only the meters that move.
- C ABI 23: `ld_fx_readouts` (an FX slot's input/output levels and its device's
  readouts); `ld_fx_latency` includes a built-in device's own latency.

- Multitrack editor: **FILE > Import: COPY TO PROJECT / KEEP IN PLACE / ASK EACH
  TIME** (kept in the app settings). KEEP IN PLACE plays imported WAV, AIFF and FLAC
  files where they are (another drive, a sample library) instead of copying them to
  the project's drive; a file the project can't play as it is (another sample rate,
  more than two channels, MP3 or another compressed format) still gets a converted
  copy, and the event log says why for each. ASK EACH TIME asks on every import and
  drop, with *Remember my choice*. Clips whose file is missing show **OFFLINE** and
  play silence without stopping playback, and play again when the file is back.
  **FILE > Collect files into project** copies the files from outside the project's
  folder into it and repoints the clips. **FILE > Projects folder** chooses where new
  projects and their recordings go (the default stays `Documents/Sidecar/Recordings`).
- WING COMPACT: SETUP > GENERAL > CONSOLE > CONSOLE MODEL **FULL** / **COMPACT** shows
  the surface of a WING COMPACT instead of the full-size WING, at once. The top row is
  the display unit with the monitoring panel at its right. Below: one fader section of
  12 faders (three modules of four) with the COMPACT's nine layer keys (`1-12`, `13-24`,
  `25-36`, `37-48 AUX IN`, `BUS MASTER`, `MAIN MATRIX`, `DCA`, `USER 1`, `USER 2`) and
  the shift keys, and the key section. **BUS**, **DCA**, **MAIN MATRIX** and **USER**
  choose what the 16 keys do. BUS: a key puts its bus on fader 13. MAIN
  MATRIX: keys 1-4 put MAIN 1-4 on fader 13, keys 9-16 MATRIX 1-8. DCA: a key spills
  its DCA and puts it on fader 13. USER: custom control keys (see the key sets below).
  **Fader 13** is the main fader, MAIN 1 at first; it has no scribble strip, only its
  meter, and the small LCD beside it names it and the 16 keys. There is no channel
  strip, custom controls, 4-Channel, center or right section, as on the console, and
  ROUTING has no AUX IN or AUX OUT (the COMPACT has no local aux I/O). The COMPACT is
  black: it shows the black finish. CH AUTOSELECT has one key, CMPCT. WING REMOTE:
  `/$ctl/cfg/mfdr`, `cscmode` and `autosel_CMPCT` are fader 13, the keys' mode and CH
  AUTOSELECT CMPCT, both ways. The model is an app preference, not saved in shows; it
  fits a 1366 x 768 window. LCL 1-24 (the COMPACT's 24 local preamps) all take LOCAL
  IN's GAIN range. C ABI version 16 (`ld_console_model_info`, `ld_layers_set_model`,
  `ld_wing_set_model`): each model's local I/O (24 local preamps and 1x2 GPIO on the
  COMPACT, 8 and 2x2 on the full size; no local aux I/O on the COMPACT).
- WING COMPACT key sets: the 16 keys play the COMPACT's own custom control sets (WING
  manual 4.2 and 4.8; WING Edit's CC EDIT on a Compact). USER mode: U1-U4 (keys 1-4
  ... 13-16). MAIN MATRIX mode: M/M on keys 5-8, which were empty. DCA mode with SETUP
  > SURFACE > USE DCA BUTTONS 1-16 AS CC: D1-D4. Each set has four keys with a
  function, a name and a colour, shown on the LCD. Holding **VIEW** opens CUSTOM
  CONTROLS on the keys' set (USER 1-4 .. DCA 13-16 and the DAW mode sets, ‹ › through them); VIEW
  pressed opens FADERS. Before, USER mode played the USER layer and the custom control
  layer shown. WING REMOTE: `/$ctl/user/U1`-`U4`, `MM` and `D1`-`D4` while the console
  is a COMPACT (the full-size console keeps `U1`-`U3` as the 4-Channel section's F1-F3
  layers). Stored with the show as custom control layers 24-32.
- WING COMPACT BUS, MAIN and MATRIX keys (WING manual 4.2, checked on a COMPACT): a
  BUS or MAIN key pressed turns on Sends on Fader to the bus or main and held spills
  it; a MATRIX key spills or sends as MATRIX SPILL says (see the matrix spill below).
  The bus, main or matrix still goes on fader 13. A selected key pressed again lets
  go: fader 13 goes back to MAIN FADER and the SOF or spill ends. SETUP > SURFACE on a
  COMPACT has MAIN FADER, KEEP MAIN FADER, DCA SPILL ON SELECT, BUS, MAIN and MATRIX
  SPILL (which of press and hold spills or sends) and USE DCA BUTTONS 1-16 AS CC; the full-size console's F1-F3, right section SOF, user layer
  link and DCA SPILL rows are hidden. WING REMOTE: `/$ctl/cfg/mfdrkeep`, `busspill`,
  `mainspill`, `mtxspill`, `dcaspill`, `dcacc`.
- Main spill: the channels, aux and buses feeding a main come onto the faders, as
  their own faders. On the COMPACT's MAIN keys (held), as FADER SPILL > MAIN on any
  custom control key, and as `/$ctl/layer/<L|C|R>/spidx` 57-60 (MAIN 1-4) on both
  models. C ABI version 17: spill kind 5, `ld_wing_set_black_finish`.
- WING REMOTE reports the WING's own model: discovery, `/?` and `/$syscfg/$cnsmdl` say
  `ngc-full` for the full-size console (it said `sidecar`) and `wing-compact` for the
  COMPACT, and `/$syscfg/$cnscfg` (where WING Edit takes its ACTIVE WING MODEL from)
  `wing-fullsize` or `wing-compact`, so WING Edit and Mixing Station show the right
  model. With SURFACE FINISH BLACK the full-size console reports `wing-bk` (WING
  Edit's name for the black WING) in all of them, and the finish changes it at once.
  Exported snapshots name the model as WING Edit's do. The serial
  `SIDECAR0001` and the firmware's `sidecar` still tell Sidecar apart.
- Inputs straight to outputs (WING manual 2 and 5.4 OUTPUTS: "route any Source to any
  digital or analog output"): on ROUTING > OUTPUTS every source group feeds outputs,
  LOCAL IN, AES50, OSCILLATOR, WLIVE PLAY, USB PLAYER and USER SIGNAL included, without
  a channel. The output plays the source after its gain, mute and polarity. An output
  has one source: a source replaces the strip or monitor path there, and the other way
  round. The RECORDER ports tap sources too. Saved with the show and recalled by scenes
  with ROUTING (`<Patch>`); WING REMOTE: `/io/out/<grp>/<n>/grp` and `in` take every
  input group.
- Buses, mains and matrices as channel sources (WING manual 2.6, "All Buses, Mains and
  Matrices can be selected as the Source for all Input and Aux channels"): ROUTING >
  CHANNELS' BUS, MAIN and MATRIX groups show two tiles per strip (BUS 1L, BUS 1R ...)
  with the strip's colour, name and icon; a touch patches the strip in stereo. The
  channel hears it one audio block later (the WING: 38 samples): a channel that sends
  to the bus it listens to is a feedback loop through that delay, as on the console.
  WING REMOTE: `/ch/<n>/in/conn/grp` BUS, MAIN, MTX (number 2k-1 = strip k L, 2k = R).
- User Signals and User Patches, the WING's USER SIGNAL group (WING manual 2.6): USR
  1-24 copy an input channel, aux, bus, main or matrix, PRE (its tap point) or POST
  (after its processing), its L, R or both mixed (L+R); USR 25-56 copy an external
  source (a port, a playback channel, the USB player), so two ports from anywhere make a
  stereo or mid/side source with their own gain and polarity. Both are sources: MUTE,
  POL, name, colour, icon, tags and pairs (STEREO, MID/SIDE). ROUTING > SOURCES' USER
  SIGNAL group sets them: SOURCE (the group, then the strip or port), L+R / L / R and
  TAP PRE / POST. Channels patch them on CHANNELS (a user signal a block late; a user
  patch in the same block), outputs on OUTPUTS (in the same block). WING REMOTE:
  `/io/in/USR/<n>/user/grp`, `in`, `lr`, `tap` and the sources' `name`, `col`, `icon`,
  `mute`, `pol`, `tags`, `mode`. Saved with the show (their settings with the sources).
- ROUTING > SOURCES selects several sources by dragging over their tiles (WING manual
  5.4: "Touch and drag to select multiple Sources. Parameter changes are applied to all
  selected Sources"): GAIN (each in its group's range), +48V, POL, MUTE, MONO / STEREO /
  MID/SIDE, INITIALIZE, colour, icon and mute groups go to all of them; the first one's
  settings show. A touch selects one source again.
- Bus to bus from any bus to any other bus (WING manual 5.10, BUS → BUS "from Bus 1-16
  to BUS 1-16"): the sends of every bus on HOME, SENDS, the fader strips' sends on
  fader and CONFIG's BUS → BUS. The engine runs a bus after the buses sending to it, so
  a chain (Bus 2 → Bus 5 → Bus 1) arrives in the same block; a loop (Bus 3 → Bus 7 →
  Bus 3) is cut at its lowest bus, whose loop send takes the previous block (the WING:
  8 samples on every bus to bus send). WING REMOTE: `/bus/<n>/send/<m>/on`, `lvl`,
  `pre` for every pair but a bus to itself.
- SOLO SAFE on Channel Home (manual 5.5, Fig. 37-39): the MAIN panel follows SOLO MODE.
  LIVE shows SOLO BUS; STUDIO the mains 1-4 in a row with their PRE badges; SIP the
  SOLO SAFE locks on input and aux channels. A closed lock keeps SIP from muting the
  channel. WING REMOTE: `/aux/n/solosafe` too.
- Dynamics KEY SOURCE on aux, buses, mains and matrices, as on the WING: SELF or any of
  BUS 1-16, MAIN 1-4, MTX 1-8, AUX 1-8, at the KEY TAP BUS, DYN, PRE FDR, POST FDR, EQ
  or INS 2 (the engine keys them; a key from a strip that runs later in the block is
  one block late). WING REMOTE: `dynsc/src` and `dynsc/tap` of aux, buses, mains and
  matrices, both ways.
- MAIN METER SOURCE (SETUP > SURFACE and the meter's right-click) has every source of
  the manual: MAIN 1-4, MTX 1-8, PHONES, SPEAKERS and SEL CH (the selected strip, at its
  kind's meter tap with AUTO). WING REMOTE: every `/cfg/mtr/mainmtr` item.
- Surface lock as in the manual (chapter 10): hold HOME to lock the surface and the
  touchscreen; other keys beside the display held with HOME are the password, and only
  HOME with the same keys unlocks. SHOW METER PAGE WHEN LOCKED works: METERS shows while
  locked. Ctrl+L is HOME alone. WING REMOTE: `/$ctl/$globals/lockmtr` and the read-only
  lock string `/$ctl/$stat/cnslock`.
- Tap tempo on the INS 1 / INS 2 screens of HOME, as on EFFECTS: the blank key taps
  the TIME of the delay the insert holds.
- Custom controls: the OTHER functions WING Edit lists that were missing: TALKBACK A /
  B (as the TALK keys), GLOBAL MAIN/ALT (manual 5.5 p. 44), **DAW LAYER** (DAW remote
  control on / off; on the COMPACT, which has no DAW key, the way into DAW mode, manual
  4.8 p. 27), MONITOR PHONES / SPEAKERS / PH+SPK, CH / BUS / MAIN / MTX SOLO TAP (PFL /
  AFL) and CH / BUS / MAIN / MTX / DCA METER TAP (PRE / POST). CC EDIT lists the
  functions and OTHER in WING Edit's order. MONITORING encoders can set PHONES LEVEL.
- Custom control keys: **CH INSERT PRE / POST ON/OFF** (a strip's INS 1 / INS 2),
  **FX PAR** (an FX slot's parameter or MIX; a press toggles a switch or steps a list)
  and **OVERVIEW PAGE** (the OVERVIEW screen of a section on a layer key or bank).
  Holding a key with a parameter gives it to the encoder above (on the COMPACT, the
  knob right of the display; manual 4.8 p. 25). An FX PAR key on a delay's TIME taps
  the tempo, and SETUP > GENERAL > TAP TEMPO FLASH works: OFF, 8X (eight beats after
  the time changes) or ON. It was only kept.
- WING COMPACT DCA assignment from the surface (manual 4.2 p. 16): press a DCA key,
  hold fader 13's SELECT and press the strips' SELECT keys to put them in the DCA or
  take them out; meanwhile those keys light on the DCA's members.
- WING COMPACT VIRTUAL section (manual 5.7 p. 61, Fig. 55): the VIEW screens have
  COMPACT and VIRTUAL at the foot of the layer column. VIRTUAL has eight layers of its
  own, eight faders wide, empty and unnamed at first; CONFIG fills and renames them.
  Saved with the show; the full-size console keeps them. WING REMOTE:
  `/$ctl/layer/VRT/1-8` (names and faders; `sel` and `ofs` on the COMPACT).
- VIEW > CONFIG with SENDS or BUS -> BUS: **SOURCE CUSTOMIZATION** / **BUS
  CUSTOMIZATION** (manual 5.10 p. 66, Fig. 60): send faders assigned show their
  source's or their destination's name, colour and icon, on the surface and the
  screens. SENDS starts with the **S** item (Fig. 59), the selected channel's send to
  the destination, which follows the selection. C ABI version 20 (`FaderRefC.flags`,
  `kFaderSelSend`).
- WING REMOTE: the custom controls' function nodes (`ch`, `fx`, `par`, `other`, `CCBK`,
  `area`, `bank`, `pg`, `tgt`, `btn`, `send`, `mgrp`, `dca`, `tag`, `action`, `usbpr`,
  the MIDI keys' `ch`, `cc`, `note`) are the show's, both ways, with the WING's node ids
  (one per position for every mode, captured from WING Edit 3.3.4), so WING Edit's CC
  EDIT shows and sets a function's target and option; before, only its mode. The new
  key functions are its INS1, INS2, FXPAR and VIEWPAGE modes and OTHER's TBA, TBB,
  ALTSRC, DAWSW, MONA, MONB, MONAB, CHSOLO ... DCAMTR.
- Recorder **SETTINGS** (status bar, as on the WING-LIVE screen, manual 8.1 p. 85-86):
  **SHOW METERS** shows or hides the meters of the armed tracks, and **AUTO INPUT
  SELECT** with **[STOP] / [PLAY] / [RECORD] INPUT** (KEEP, MAIN or ALT): when the
  recorder stops, plays or records, the global MAIN / ALT switch goes to MAIN or ALT.
  The channels follow it while INPUT SELECT is GLOBAL, as VIRTUAL SOUNDCHECK sets it,
  so a virtual soundcheck can hear the recording on PLAY and the band on STOP and
  RECORD. The switch happens before the first sample is recorded. WING REMOTE:
  `/cards/wlive/meters`, `autoin`, `auto_stop`, `auto_play`, `auto_rec`.
- Recorder **MODE A->B** (manual 8.1 fig. 75): MODE now steps PLAY (yellow), A->B
  (blue) and LOOP (green). A->B plays from the range's start and stops exactly at its
  end. The knobs under the display set the range (START and END, the loop borders in
  LOOP). The mode is kept in the project; older projects play as before. WING REMOTE:
  `/cards/wlive/1/cfg/playmode` (and `2`). C ABI version 18: `ld_rec_set_range`.
- Shows hold files as scenes (manual 5.6 p. 52, fig. 45): a snippet, an audio clip, an
  FX preset or a channel preset. **ADD ITEM TO SHOW** (SHOW tab; also **ADD** in the
  status bar) adds the file selected at INT or USB after the selected scene. A scene
  file comes in as a scene; an FX preset loads into the FX tab's slot, a channel
  preset onto the selected strip. **SAVE + ADD** on SNIP, FX and CHAN saves and adds
  in one go (on SNAP, SAVE already adds the scene). Recalling such a scene (GO, GO
  NEXT, a tag, MIDI, a WING remote) loads the file: the snippet (with the scene's
  fade), the preset, or the clip on the USB player. The scene list shows the type
  (SNIP, WAV, FX, CHAN) and, once renamed, the file's name. The show keeps the
  snippet and presets in itself and the clip's path. The SHOW tab shows the files
  that can be added normally. Older shows load unchanged; older Sidecar versions see
  such scenes as empty. C ABI version 18: `ld_scene_item_kind`, `ld_scene_item_text`,
  `ld_show_add_item`, `ld_show_add_snippet_scene`, `ld_show_take_clips`.
- **STARTUP** files (manual 5.6 p. 52 and chapter 10): a show, scene and snippet whose
  names start with `STARTUP` in the library's `STARTUP` folder
  (`Documents/Sidecar/Library/STARTUP`) load when Sidecar starts: the show first
  (Sidecar's `.lshow`, else a WING `.show`), then the scene (`.lsnap`, else a WING
  `.snap`) and the snippet (`.lsnip`). Holding SHIFT while Sidecar starts (the WING:
  holding LIBRARY) or `--no-startup` skips them. C ABI version 18:
  `ld_show_apply_library`.
- **Outboard inserts**: the WING's EXTERNAL effect (manual 11.11 p. 164, Fig. 169).
  The EXT device keeps hosting plugins and gets a **SOURCE**, **PLUGIN** or
  **OUTBOARD** (also in the FX slot menu: *Outboard insert (SEND / RETURN)*). An
  outboard slot sends its strip's insert signal to its **FX SEND** at **TRIM** in
  **FX MODE** MONO, STEREO or MID/SIDE, and returns the **RETURN** inputs (LCL, A, B,
  C, CRD or PLAY; the next input too in STEREO and MID/SIDE) in its place. Its
  latency, one audio block plus the interface's reported input and output latency
  plus **LATENCY** (samples, the gear's own), is compensated like a plugin's, and FX MIX
  delays the dry signal by it. Without a RETURN it passes the signal through. The
  EFFECTS editor shows the EXTERNAL EFFECT page as the console's (SEND opens ROUTING >
  OUTPUTS). ROUTING > OUTPUTS gets the **FX SEND** source group (FX 1L ... FX 16R,
  the WING's FX SEND 1-32), which showed n/a tiles. Shows, scenes and FX presets keep
  the settings (presets without the outputs); EXT slots of older shows stay plugin
  slots. WING REMOTE: `/fx/N/EXT/egrp`, `ein`, `emode`, `lat` and `trim` drive it
  (they were only stored), selecting `EXT` as `mdl` makes an outboard insert, and
  outputs patched to `SEND` n carry slot (n+1)/2's send, so WING Edit's EXTERNAL
  EFFECT editor works on it; a slot hosting a plugin still reads as EXT and keeps its
  plugin. C ABI version 21: `FxParams::sendLeft` / `sendRight`,
  `ld_engine_set_io_latency`, `ld_fx_state` 4 / 5 (outboard insert, with a RETURN);
  `ld_fx_latency` gives an outboard insert's latency.
- Source and output **GLOBAL safes** take effect (manual 5.6 p. 54-55): GLOBAL's
  SOURCE (LCL, A, B, C, CRD, PLAY, OSC) and OUTPUT (LCL, A, B, C) tiles, dimly lit
  when part of a group is safe, keep safe sources' settings and names and safe
  outputs' patches when a scene is loaded (RESTORE, NO GLOB and snippets ignore
  them). Scenes now recall the sources with ROUTING (scenes saved before leave them
  alone). The safes are the console's, saved with the show; WING REMOTE
  `/$ctl/safes/source/*` and `output/*` of those groups set them (they were only
  stored). C ABI version 21: `ld_engine_io_safe`, `ld_engine_set_io_safe`.

- Matrix spill (WING manual 5.5, BUS/MAIN/MATRIX SPILL): the channels, aux, buses and
  mains sending to a matrix come onto the faders as their send levels to it (MUTE
  switches the send), as a bus's spill shows its sources. On the COMPACT's MATRIX keys,
  as FADER SPILL > MTX 1-8 on any custom control key, and as `/$ctl/layer/<L|C|R>/spidx`
  49-56 on both models. BUS SoF/SPILL ACTIVATES SOLO solos a bus, main or matrix
  spilled, as it did for Sends on Fader. C ABI version 22: spill kind 6.
- WING COMPACT SETUP > SURFACE (manual 5.5, FADERS, in its order): **DCA SPILL ON
  SELECT** (`/$ctl/cfg/dcaspill`, off at first), the COMPACT's one DCA spill setting:
  on, a DCA strip's SELECT spills it, as the full-size WING's DCA SPILL. A DCA key
  always spills its DCA and puts it on fader 13, and selects it for the DCA assignment
  with fader 13's SELECT (a WING COMPACT's owner). **BUS SPILL**
  (`/$ctl/cfg/busspill`, new), **MAIN SPILL** and **MATRIX SPILL**: on, a key's press
  spills its bus, main or matrix and a hold turns on Sends on Fader; off, the other way
  round. MATRIX SPILL now works (Sidecar had no matrix spill): on, as a factory-set
  WING has it, a MATRIX key pressed spills the matrix and a hold turns on Sends on
  Fader to it; MAIN and BUS keys send on a press and spill on a hold.
- WING COMPACT in DAW mode (DAW LAYER on a custom control key, manual 4.8): the 16
  keys and the four mode keys do nothing until you assign them (a WING COMPACT's
  owner): they play custom control sets of their own, **DAW KEYS 1-4** .. **13-16**
  and **DAW MODE KEYS**, empty at first and edited in CC EDIT like the other COMPACT
  sets (VIEW held opens DAW KEYS 1-4); a key acts while held (DAW BUTTONS push) and is
  named on the LCD, which reads DAW. A key whose DAW control is empty but whose own
  set's control is DAW LAYER still leaves DAW mode. Stored with the show as custom
  control layers 33-37; the WING tree has no node for them.
- Custom controls: **FADER START** (WING `FSTART`): a key on a channel, aux, bus, main
  or matrix that lights while its fader is up (the fader start contact); **DAW
  V-POTS** on a key (`DAWENC`): M1-M8 / EXT1-16 pushed while the key is held, turned a
  step clockwise per press, or the jog wheel; the encoders' **OTHER**: LAMP, GLOW and
  PATCH BRIGHTNESS (SETUP > GENERAL's tiles, now also `/$ctl/cfg/lights/lamp`, `glow`,
  `patch`); MONITORING's **TALKBACK** level; USB PLAY/REC's **TOGGLE PLAY/STOP** and
  **TOGGLE PLAY/PAUSE**; GLOBAL PAGE's **SOURCE SETUP**, **OUTPUT ROUTING** (ROUTING's
  SOURCES and OUTPUTS; CHANNEL INPUTS opens CHANNELS) and **USB PLAY/REC**; FADER
  SPILL's **AREA** (LEFT, CENTER or RIGHT: the section it spills to; it always spilled
  to the left). WING REMOTE: modes FSTART, DAWENC and the encoders' OTHER, `pg` SRC,
  OUTS, 2TRK, `area` and `tgt` MTX1-8 of FADER SPILL, both ways (the toggles are newer
  than the 3.1 tree: they read as PLAY).
- Custom controls: a control's light (WING manual 4.8: "ensure COLOR → LIGHT is
  enabled"): CC EDIT's COLOR > NONE turns it off and keeps the colour, a colour turns
  it on. WING REMOTE: `/$ctl/user/.../led` (it was only stored); `col` keeps the light
  as it is.
- WING REMOTE: `$fname` of every custom control key and encoder reads the function's
  name as the console writes it (WING Edit 3.3.4's offline console; the factory DAW
  layers match the dump of a real WING): "|1 MUTE", "|B1 |SOF", "|DCA1 > L", "|FX3
  MIX", "|DAW STOP", "|DAW *TIME", "|1 |HOME", "|FDR L/1", "TALK A", "|SCN GO", "CC
  1:7", "|1 FDR", "SEND |B1". It read the function's long name ("CH MUTE").
- SETUP > AUDIO > **SEND PAGE ACTIVATES SOLO** works (`/$ctl/cfg/sfdr2solo`, "only
  applicable from the SENDS page"): choosing a destination on VIEW > SENDS solos it.
  It was only kept.
- ALTERNATIVE SOF MODE as in the manual (4.9 p. 28): SOF FLIP held starts the standard
  SOF on the selected channel, a press ends either SOF mode (SELECT held still starts
  the alternative one). Before, a press switched SOF in both modes.
- VIEW > FADERS: AUTO RIDER in the COMP slot shows its square, lit while it rides
  (manual 5.8 p. 63).

### Changed

- The 480L-style reverbs (HALL, ROOM, CHAMBER, PLATE, CONCERT, AMBIENCE) ring for
  their DECAY at the console's default SIZE (ROOM rang 1.6 times longer at its default
  50 m: a 2.5 s DECAY lasted about 4 s). SIZE still scales the decay from there.
- The reverbs start at PRE DLY: the early build-up (SHAPE / SPREAD) is heard from the
  pre-delay on instead of only once the tank answers, 20-80 ms later (HALL began about
  40 ms after PRE DLY, ROOM 75 ms). AMBIENCE's early sound reaches both sides from
  either input.

- A RATIO picked from a list reads "3:1" everywhere when the ratio is whole (an
  expander's "1:3"; a WING's owner), "2.5:1" or "1.43:1" when it isn't: also on the
  knobs, the FX rack's tiles and a fader strip with an FX parameter, which showed
  "3.00". The earlier "2.0:1" is gone.
- Display screens matched to screenshots of a WING (firmware 3.1), side by side at 1280 x 800:
  - Status bar: the WING-LIVE cards are white outlines with a clipped corner over a separate
    "LIVE" strip with the dots at its ends; the USB stick, the SETTINGS gear and the padlocks
    are outlines as on the console; the A / B / C / SC chips have black print; the clock's
    colon is set tight and the digits and date sit as on the console. ROUTING's SOURCES and
    OUTPUTS title tile is #585858 with the number, icon and name at the console's places.
  - ROUTING: the CHANNELS / SOURCES / OUTPUTS keys have the console's icons (two faders, a
    block arrow into and out of a ring); the right grid is no longer dimmed while the patch
    is locked; a channel tile prints its port ("PLAY 1/2"), not the source's name; the
    selection frame is 4 px; the SOURCE GROUP box has the console's icon and type sizes;
    SOURCES' POL key shows "+|-" and its GAIN sits in the parameter bar's last slot.
  - SETUP > AUDIO > INPUT SELECT is a floating 937 x 480 window as in the manual's Fig. 35:
    two framed panels, 52 px cells in pairs of rows with "CH" over a rule and the number.
  - LIBRARY: the INT / USB / SHOW keys and the date key have black faces and the console's
    icons (a console face, a USB stick on its side, a film frame); FOLDER, REMOVE, SAVE
    +SCOPE, UPDATE, PARTIAL UPDATE / LOAD, EDIT SCOPE, RESTORE, OPEN, CLOSE, EDIT TAG and SEND
    MIDI have their own icons; keys have a 1 px light frame (black while disabled); ADD ITEM
    TO SHOW, EDIT TAG, SEND MIDI and the SNIP item keys are printed on their panel; the
    toolbar keys sit at the console's places; GLOBAL moved to the right of the status bar;
    the SHOW key reads SHOW while the show has no scenes.
  - MONITOR: TALK A / B tabs show a speaking head; the headphones, speaker and PH+SPK icons
    are thin outlines; PFL DIM's bar fills from the right end.
  - METERS SETTINGS and every window's CLOSE key: a 32 px box (open at the corner where the
    tick ends) 16 px from the right, the caption 16 px from it. Unselected tabs use 14 px
    capitals (they were 17); chips print a little larger.
  - USB recorder: bigger transport glyphs, 14 px 2-CH / 4-CH and 16 / 24 bit keys.
  - VIEW: the layer title and MUTE GROUPS title are #585858; CONFIG's fields use the
    console's 14 / 20 px caption and value.
- HOME and the channel processing screens matched to screenshots of a WING (firmware 3.1),
  side by side at 1280 x 800:
  - GATE / COMP: the ENVELOPE centres the hold on the graph (it centred the whole shape, so
    the gate's sat 39 px left of the console's) with the WING's 8 px under its title; the
    transfer graph's level labels sit 8 px left of the plot, the threshold line starts right
    of the meter, the threshold handle is 39 px across at the edge; RATIO and KNEE in the
    console's amber (#f8c430), ATTACK / HOLD / RELEASE in its #18e460; a list RATIO reads
    "3:1" on the parameter bar as the console shows it ("2.5:1", "1.43:1" keep their digits).
  - The model list's GATE / DUCKER / COMPRESSOR / EXPANDER tiles are miniatures of the slot
    screen: a thin grey curve with an orange stretch, the threshold handle, a black ENVELOPE
    with its handles, drawn at the model's factory values (the list's pictures are fixed,
    the loaded model's too: they don't follow the strip's settings). Model names sit 2 px
    higher.
  - Fields: values in the console's heavier weight (knob boxes too), the drop-down triangle
    20 x 11 px light grey, the ">" key 14 x 24 px with 3 px strokes (also INS's key beside
    FX PROCESSOR). Screen keys have the console's black frame (2-3 px; 4 px round a slot's ON).
  - Drop-down lists (FILTER TYPE, XOVER MODE ...): rows #101010 in a 2 px #707070 frame, the
    chosen one gold 2 px inside its row, names in 14 px light grey capitals, as wide as the field.
  - FILTER SETUP: TOOL FILTER keys stacked in 2 px frames with the name centred right of the
    icon, a smaller TOOL FILTER caption, a larger page title and CLOSE (also the key filter and
    XOVER pages). EQ: unselected band names grey (#707070), as on the console.
  - The source tile (overview, CHANNEL INPUT, COPY's source) in a 4 px black frame; inside it
    the icon, signal ring, 48V / MUTE chips and port name where the console has them; a bolder
    +48V icon. The insert tap's grey line shows on the slot screens' block column too; the tap
    is a rounded square.
  - ICON/COLOR, NAME and TAGS: LEVEL exactly where the overview has it; a smaller footnote.
  - CHANNEL INIT / COPY: scope keys with the console's icons (plug, gear, band, the block keys'
    GATE / DYN / EQ / INS art, fader cap, knob) over SemiBold captions, framed only while off;
    destination tiles 60 px on a 66 px pitch with the channel's (or its source's) icon; the INIT
    / COPY key grey in a light grey frame.
  - SELECT FX PROCESSOR and the FX TYPE list over INS keep square WING pixels (they were
    squashed to the HOME page's height), with the console's type sizes. SELECT CHANNEL (KEY
    SOURCE): title and CLOSE in the console's sizes.
- FX pages measured from the manual's figures of the console's pages (chapter 11) and a
  real WING's EFFECTS screens: GATED and REVERSE REVERB (the WING digital reverb with its
  LCD, red value display and bar knobs), DELAY/REVERB ("Dual FX"), SHIMMER, SPRING,
  VINTAGE ROOM (value tiles round the data wheel), VINTAGE REVERB (levers and LED ladders
  you click), VSS3 (TC ICON remote: six faders, the preset wheel and the MAIN / ER / TAIL /
  MOD layers), DIMENSION CRS, STEREO FLANGER, ULTRATAP, TAPE DELAY, OILCAN, BBD, STEREO and
  DUAL PITCH, GRAPHIC EQ (TRUE CURVE switch on both pages), PIA 560, DOUBLE VOCAL, PITCH FIX
  (note keys), PRECISION LIMITER, 2-BAND DEESSER, C5-COMBINATOR (band screen; touch a band
  to edit it), ULTRA ENHANCER, EXCITER, PSYCHO BASS, ROTARY, PHASER and TREMOLO/PANNER
  (attack-hold-release graph), TAPE MACHINE, MOOD FILTER, SUB OCTAVER, SUB MONSTER, VELVET
  IMAGER, SPEAKER MANAGER (FILTER / DYN EQ / LIMITER), the five guitar amps and BODYREZ.
- The channel strips (EVEN, SOUL, VINTAGE, BUS CHANNEL, MASTERING) show each section as the
  unit it recreates (Even 88 gate, SOUL 9000, 76 limiter, Pulsar, LA leveler, Warmth, Bus
  comp, Tape, Mach EQ4, Enhancer, Precision limiter), a tab per section; their FX menu
  tiles are tinted collages of those panels, as on the console.
- The FX menu's HALL, ROOM, CHAMBER, PLATE, CONCERT and AMBIENCE tiles show the program's
  letters ("HA", "RO"...) and EXTERNAL shows the EXTERNAL EFFECT page, as on a WING.
- The EXTERNAL EFFECT page follows Fig. 169: SEND, FX MODE (MONO / STEREO / MID/SIDE rows,
  the mode in use green), RETURN, the meters' scales either side.
- An FX rack tile shows the control a WING shows: REVERSE RISE, DELAY/REVERB TIME, SPEAKER
  MANAGER TILT F, PRECISION LIMITER SQUEEZE, ROTARY SPD CTL, TAPE MACHINE DRIVE, PITCH FIX
  AMOUNT.
- The knob last touched on an FX page is framed by corner brackets, as on the console.

- A new EXTERNAL (EXT) effect starts at FX MODE ST and LATENCY 45, as on the WING (checked
  by a WING owner and in WING Edit 3.3.4); it started at M and 0. A plugin hosted in the
  slot still uses its own reported latency.
- Every processing and FX model starts with the WING's own defaults: 98 of the 113
  models (all but the WING GATE, WING COMPRESSOR, WING EQ and a few others) started
  with values of our own. The defaults are what WING Edit 3.3.4, which runs the
  console's firmware, gives each model (they match a factory-set WING and the manual's
  figures: a HALL at PRE DLY 50 ms, SIZE 50, DECAY 1.57 s, BASS MULT 0.95, DAMPING
  5k7 Hz, DIFFUSION 25, Fig. 124; DELAY/REVERB at 240 ms, Fig. 132). FX RESET and a
  newly chosen model use them; saved shows keep their values. Some that change the
  sound: the DUCKER at -20 dB, RANGE 20, ATTACK 100 ms; the analog EQs' frequencies;
  MACH EQ4 with AUTO on and AIR 40k; the channel strips (EVEN, SOUL, VINTAGE, BUS,
  MASTERING) with their EQ and dynamics blocks off; the MOOD FILTER's LFO at 100 %;
  PRECISION LIMITER (and MASTERING's) at OUT -0.5 dB with AUTO GAIN. The generator
  takes them from `tools/wing/model_defaults.json`.
- Model values read as the console writes them: each parameter with its own fixed
  decimals, kHz with the k as the decimal point and times in ms however long (DAMPING
  "5k7 Hz", a DYNAMIC EQ at "2k99 Hz" and "1216 ms", a DUCKER's RELEASE "1503 ms"),
  as WING Edit's DUCKER and DYNAMIC EQ and the manual's HALL and DELAY/REVERB show
  them. They read "5.68 kHz", "2.99 kHz" and "1.50 s", and integers such as PRE DLY
  had no unit ("50", now "50 ms"). The BLUE PLATE's second display says HZ.
- PITCH FIX: a note key switched on disables that note (the console starts with all
  keys off and every note corrected); it was the other way round. Shows and FX presets
  saved before keep the notes they corrected.
- Meters: one set of ballistics for every level meter (screens, fader strip and
  channel strip LEDs, the main meter, ROUTING's tiles, the USB recorder, the dynamics
  graph's output, the remote meter stream), as a WING owner measured on their console:
  the full peak at once and a 60 dB/s fall (a steady -6 dB tone pulled out leaves the
  strip LEDs dark in 0.9 s; the console: "about 1 second, maybe less"), with no peak
  hold on the strips or the main meter (after a drum hit they fall straight away) and
  no clip hold on the surface (CLIP goes out at once). Only the METERS screen holds a
  peak for 1 s (its dash, red for a held CLIP), then falls at the same rate. Before,
  most meters fell about 180 dB/s (a click barely showed on the surface) while the
  METERS screen's hold and the recorder fell at their own rates, so one signal read
  differently on different meters.
- The fader strips' LED meters follow SETUP > SURFACE > METERING instead of always
  showing the input, and DCA strips meter their loudest member (as the METERS
  screen). PRE is after TRIM and before the EQ and dynamics (on a WING, TRIM moves a
  PRE strip meter and the EQ does not), POST after the fader; the same PRE for METERS
  > SETTINGS, the main meter's MAIN METER TAP, the VIEW screens and the remote
  stream's DCA meters. The channel strip's LEVEL meter is after TRIM (it read the raw
  input).
- Meter colours: the main meter has no yellow (it was yellow from -4 to -1 dB): green
  below -18 dB, orange from -18 dB, red at CLIP, as the strip LEDs. The MONITOR
  screen's level meter takes the METERS screen's colours (green, amber from -18 dB
  instead of yellow from -12 dB).
- The METERS screen repaints only the strips whose meter moved, not the whole page.
- The HOME thumbnails' meters use the screens' meter scale (CLIP, -6, -12, -24 ... -60).

- F1-F3 of the 4-Channel section with USE F1-F3 AS CUSTOM CONTROLS mirror the custom
  control layers 1-3 (encoders and upper keys), as the manual describes (5.5 p. 48);
  they were three layers of their own. Shows saved before move those assignments to
  layers 1-3 where those controls are free. WING REMOTE: `/$ctl/user/U1`-`U4`, `MM`
  and `D1`-`D4` are the COMPACT's key sets on the full-size console too (WING Edit
  lists them there); `U1`-`U3` were the F1-F3 layers.
- SEL DBL CLICK = BUSFX opens INS 1 on mains and matrices too (it did on buses only),
  and INS 2 when INS 1 has no FX (manual 5.5 p. 48).
- The blank key's LED blinks at the tempo while it can tap one (manual 5.5 p. 48); it
  was lit steadily.
- Surface lock: holding HOME (or Ctrl+L) now also locks the touchscreen and the
  display's knobs, leaving only the keys beside the display to unlock (it locked the
  surface only).
- A matrix's DIRECT INPUT takes the WING's choices (WING manual 6.4: "AES/EBU, Phones,
  Speakers or Solo bus"): OFF, AES (no AES/EBU input here: silent), MON.PH, MON.SPK,
  MON.BUS, a block late (the monitor runs after the matrices), with ON, LEVEL and
  INVERT; PRE is gone. WING REMOTE: `/mtx/<n>/dir/in`. Shows and scenes saved before,
  whose direct input took a channel, give that channel a matrix send to the matrix
  instead (on, at the direct input's level, PRE or post-fader with PAN LINK) when the
  send is free; an inverted one (a mix-minus) loads not inverted, with a warning.
- ROUTING > OUTPUTS' USER SIGNAL group holds USR 1-56 (before: the channels' own
  outputs). Channels' direct outs from older shows still play and show on their
  outputs; new ones are made with user signals.
- C ABI version 19: `StripParams::directPre` is gone and `directSource` is the matrix
  DIRECT IN choice; `UserSignalParams`, `UserPatchParams`, `ld_engine_user_signals`,
  `ld_engine_user_patches`, `ld_source_set_user_patch`, `ld_engine_output_patch`,
  `ld_engine_route_output_source`, `ld_engine_clear_output`, `ld_strip_source_input`;
  `kNumSources` grows by the 56 USR sources.
- INPUT SELECT works as on the WING (manual 5.5 p. 44-45, 6.1 p. 68-69): each input
  and aux channel is **INDIVIDUAL** or **GLOBAL** on its own (HOME > INPUT's
  INDIVIDUAL / GLOBAL, or the new **INPUT SELECT** window that SETUP > AUDIO's channel
  grid opens, fig. 35: MAIN/ALT INPUT switches a channel, GLOBAL INPUT SELECT ENABLE
  puts it under the global switch). The global **MAIN / ALT** switch (SETUP > AUDIO,
  the GLOBAL MAIN/ALT custom control) switches the GLOBAL channels and leaves the
  others. **GLOBAL INPUT SELECT OVERRIDE** (on, as after INITIALIZE) lets a GLOBAL
  channel's own MAIN / ALT still switch it; off, only the global switch does. Before,
  INDIVIDUAL / GLOBAL was one choice for the whole console (shown as the override):
  GLOBAL made every channel follow the switch. AUTO INPUT SELECT switches the GLOBAL
  channels routed to or from the recorder (a playback channel as MAIN or ALT input, or
  a track recording the channel or its input), as the WING does on WLIVE REC / PLAY;
  VIRTUAL SOUNDCHECK puts its channels in GLOBAL INPUT SELECT ENABLE on ALT and gives
  them back as they were. Shows save each channel's choice, which scenes recall with
  ROUTING (CONN), and the override with the preferences (scenes leave it). Shows and
  scenes saved before load as they sounded: with INPUT SELECT GLOBAL every input and
  aux channel GLOBAL on the switch's MAIN / ALT, else every one INDIVIDUAL.
  WING REMOTE: `/ch/<n>/in/set/srcauto` and `/aux/<n>/in/set/srcauto` (only stored
  before), `/io/autoaltovr` is GLOBAL INPUT SELECT OVERRIDE, `/io/altsw` switches the
  GLOBAL channels (only when it changes, so a client's sync leaves them), and `altsrc`
  sets the switch as it stands (the override locks the console's keys). C ABI version
  22: `StripParams::srcAuto`, `EngineControls::inputSelectOverride` (was
  `inputSelectGlobal`), `ld_engine_set_global_alt`.
- METERS > SETTINGS has its own PRE / POST per strip type, as the WING's METERS screen
  (`/cfg/mtr/mtrpage`); SETUP > SURFACE > METERING keeps the surface's meters and the
  main meter's AUTO (`/cfg/mtr/mtrsfc`). They were one setting; the METERS screen
  starts at the console's factory taps.
- The PHONES and SPEAKERS **SOURCE** list starts with **OFF** (the WING's
  `/cfg/mon/<n>/src`): the path then plays only solos and its DIRECT IN.

- The custom controls' LCD text for a control without a name is the console's function
  name (as WING Edit shows it): "1 MUTE", "B1 SOF", "1 FDR", "SEND B1", "1 -> B1", "FX3
  TIME", "DCA", "MGRP MGRP.2", "FDR L/1", "VIEW C/3", "SCN GO", "SCENE 4", "TALK A",
  "MON DIM", "PL PLAY", "CC 1:7" (they read "CH1 FADER", "SOF BUS1", "TALKBACK A" ...).
  CC EDIT's lists follow WING Edit's names and order: CH PAGE (DYNAMICS before EQ),
  GLOBAL PAGE (EFFECTS, METERS, CHANNEL INPUTS, SOURCE SETUP ... FADER LAYER, Sidecar's
  HOME last), SHOW CONTROL (PREV SCENE ...), USB PLAY/REC (STOP (PLAYER), PLAY (PLAYER)
  ...), MONITORING (PHONES, SPEAKERS, TALKBACK, then Sidecar's DIM AMOUNT) and FADER
  SPILL (AREA, then one TARGET list: DCA, FX, BUS, MAIN, MTX, AUTOMIX, the selected
  channel).
### Fixed

- The multitrack editor's tracks showed a raw source number as their input after
  RECORD ALL CHANNELS from a channel patched to anything but a local input ("IN
  196609"); they show the source as ROUTING labels it ("IN OSC 1", "IN LCL 3").
- On Windows, stopping the WING remote (or quitting) with WING Edit or another
  Native client connected took two minutes: a shutdown doesn't end a blocked
  receive there. The Native reader on Windows now doesn't block and looks for
  data every 2 ms, and a write's stall is timed from its last progress.
- Turning a LO CUT / HI CUT on STEREO PITCH, DUAL PITCH, STEREO DELAY, ULTRATAP,
  CHORUS, FLANGER or OILCAN clicked (the filters jumped once per block); they glide
  smoothly now. GATED REVERB's DENSITY, the pitch devices' DELAY (a bend now, like a
  tape delay, instead of a smear) and the amps' tone knobs clicked too.
- The effects no longer slow down on silent tails where nothing else flushes
  denormal numbers (offline renders, the model response graphs).

- PRECISION LIMITER: its 1 ms look-ahead is now reported as the slot's latency (delay
  compensation) and FX MIX delays the dry signal by it; below 100 % MIX the dry and
  limited signals combed. The mastering strip keeps the look-ahead when its limiter is
  switched off, so its latency never changes.
- EXCITER: TIMBRE toward positive (even harmonics) generated odd harmonics like the
  negative side, so it hardly changed the sound; it now adds even harmonics.
- SUB MONSTER's readouts under the faders show each band's frequency (from TUNE), as on
  the plug-in, instead of the fader levels.
- TAPE MACHINE's VU scale reached past the end of its arc (+1 to +3 off the meter).
- WING REMOTE (Native, TCP 2222): a client that was reading and sending normally
  (WING Edit) could be disconnected at random on Windows. The server read and wrote
  each connection with socket timeouts (100 ms reads, 10 s writes), and on Windows a
  receive or send that times out leaves the connection in an undefined state, with
  errors std doesn't map as on Unix (an interrupted blocking call is WSAEINTR, which
  the server took for a broken connection). The connections now have no socket
  timeouts: the server times the idle rule (12 s) and the stalled-write rule (10 s of
  no progress) itself and shuts a connection down to end it, and only the end of the
  stream or a real error closes a client. The server also keeps why each Native
  connection ended (`Status::disconnects`: the client closed it, idle, write stalled,
  queue full, read or write error, server stopped).
- ETERNAL BLISS compressed a steady tone far less than its curve at short releases (at
  its default RELEASE 102 ms, 0.3 dB instead of 2 dB at 3 dB over the threshold): its
  constant-rate release climbed back through every trough of the waveform. The
  detector now holds a peak for 10 ms before releasing.
- WING REMOTE meters: the stream sent the latest UI meter window's raw peak (peaks
  between two frames were missed, levels flickered) and, without the app's meters
  running (the headless `wing_emulator`), every level stuck at its highest peak. The
  stream now has its own peaks and the meters' ballistics, on a fixed 50 ms frame
  clock (it drifted to about 19 frames a second). DCA meters send pre- and post-fader
  levels (they sent post four times), gate and dynamics keys their detector levels,
  the WING gate's gain reduction its 60 dB scale (protocol document, Meter Data), the
  monitor meters PHONES as MON 1 and SPEAKERS as MON 2, and FX meters their dynamic
  bands' gains instead of a dB value read as a level.

- BUS SoF/SPILL ACTIVATES SOLO solos a bus or main when it is spilled (manual 5.5
  p. 46-47); only Sends on Fader did.
- WING REMOTE: a processing or FX model changed right after a client connected (before
  the first change poll) now sends all of the new model's parameters, as later changes
  do; parameters that happened to equal the old model's could be left out.
- The factory DAW layers (DAW 1-4: STOP, PLAY, CYCLE, RECORD ...) show on the surface
  and on CC EDIT; the custom controls read from the show left out the controls still
  at their factory assignment, and the next edit saved them empty.
- ROUTING: the bus, main and matrix tiles (CHANNELS, SELECT MAIN INPUT) and the signal
  tiles on OUTPUTS have live meters, as the input tiles already had: a strip signal
  meters the strip's output, a source its peak, PHONES / SPEAKERS the monitor paths.
  Their meters were drawn but stayed empty.
- WING Edit (tested with 3.3.4 on both console models): its model request after
  connecting (it sets `/$syscfg/$cnsmdl` to "ModelRequest") is answered with the model,
  and any set Sidecar refuses over the Native protocol now answers the value that
  stands instead of nothing, so the client shows the console's value (an FX slot
  hosting a plugin answers EXT when WING Edit picks another model for it).
- WING Edit PC -> MIXER no longer unpatches LOCAL OUT: an output nothing feeds read
  the factory WING's patch (LCL 3 as BUS 5 but AES50 A 3 as OFF, one port in
  Sidecar), and writing both back left them OFF. Unpatched outputs read OFF.
- WING remote values are on the parameter's steps, as the console reports them:
  model defaults between two steps (HALL's decay 2.00 s, a Q of 2.00) read 2.03 s /
  1.996 as WING Edit shows them, instead of differing from it.
- A processing model changed in Sidecar or by another client (FX slot, GATE, EQ,
  DYNAMICS) sends all the new model's parameters to WING remote clients, also those
  equal to the old model's at the same position, which WING Edit otherwise left at its
  own defaults.
- A bus's spill and a strip's sends (SPILL on BUS SENDS, FADER SPILL > SEL CH) list
  every bus sending to every other bus, as the engine has had since any bus feeds any
  bus; they still listed only buses 9-16 into buses 1-8.

## [0.1.5] - beta

The third round of matching Sidecar to a real WING, from the factory parameter dump,
screenshots and answers from a console's owner: a factory-initialized show now starts
with the WING's own values; the SETUP, LIBRARY, USB, VIEW, MONITOR, HOME, GATE, EQ,
COMP, FILTER and ROUTING screens follow the console more closely; source GAIN has
each source group's real range and steps; ROUTING starts locked; user layers work
like the WING's (fixed slots, blanks, duplicates); and channels without a source show
no tag. The WING remote parameter tree is finished, and the dropouts with Mixing
Station and WING Edit are fixed. Every FX device and processing model has its own
front panel, the dynamic EQs and the BDX 902 de-esser were corrected against
measurements, and 1366 x 768 screens are supported. Known limitations: see the
[README](https://github.com/Audbol/Live-Daw/blob/main/README.md#known-limitations).

### Added

- Every FX device and processing model now has its own front panel, so none fall back
  to the generic knob grid. Before this, 49 of them did: 47 FX devices, the PSE/LA
  COMBO dynamics model and the PIA 560 bus EQ. Each new panel has the controls of
  every parameter, with the console's captions and value text, laid out in the
  colours, knobs and print of the unit it recreates. Units with many controls (VSS3,
  COMBINATOR, SPEAKER MANAGER, TRIPLE DEQ, the channel strips) have pages.
- WING REMOTE: the parameter tree is finished. Every parameter of the WING 3.1 tree
  but one now answers (68,555 of 68,556, up from 60,555, 88.3 %):
  - Newly driving Sidecar: the LOCAL IN sources (the same ports as AES50 A, as on
    ROUTING) and the USB player's (the `PLAY` group: name, colour, icon, mute,
    polarity, mode; channels patch to `PLAY` 1-4), LOCAL OUT (A 1-8), the USB
    recorder's RECORDER ports (`/io/out/REC/1-4`: a bus, main or matrix signal or a
    source), the buses, mains and matrices as sources (`/io/in/$BUS`, `$MAIN`, `$MTX`,
    read only) and `/$globals/clkrate` (the audio device's rate).
  - SETUP's options are Sidecar's settings, both ways (C ABI: `ld_wing_host_setting_keys`,
    `ld_wing_set_host_setting`, `ld_wing_take_host_setting`, `ld_wing_clear_host_settings`;
    `WingRemote::setHostSettings`): METERING's taps (`/cfg/mtr/mtrpage`, `mtrsfc`),
    MAIN METER and MAIN METER TAP, the GENERAL, SURFACE and SOLO SYSTEM options and
    brightness tiles (`/$ctl/cfg`, `/$ctl/$globals`), MIDI REMOTE CONTROL (`/$ctl/midi`)
    and DAW (`/$ctl/daw`). The rows of features Sidecar doesn't have (TAP TEMPO FLASH,
    SHOW METER PAGE WHEN LOCKED, MOUSE DISABLES TOUCH, SEND PAGE ACTIVATES SOLO, the
    OSC and TCP remote locks) are kept as settings, as SETUP keeps them.
  - What Sidecar has no equivalent for (cards, the console's hardware, network and
    status, other models' surfaces, input and output groups Sidecar doesn't have,
    bus-to-bus sends the engine doesn't have, source and output safes, the player's
    song tags...) answers what a factory-initialized WING answers
    (`livedaw-wing/data/wing_factory.tsv`, from a dump of a real console) and keeps
    what a client sets, read-only where the WING marks it. Unbound nodes and missing
    strips answer the factory value too, instead of a neutral one.
  - Left unmapped: `/$ctl/daw/config`, whose 3.1 choices (CC, MSTR) can't show
    Sidecar's MCU with extenders.
  - A new console and INITIALIZE CONSOLE now come up as a factory-initialized WING
    everywhere Sidecar has the setting: compared parameter by parameter with a real
    console's dump after INITIALIZE (29,862 parameters), 178 values still differ
    (3,506 before). What changed (Sidecar's old default in brackets):
    - SETUP: MAIN METER MAIN.1 (the monitor); MAIN METER TAP is a setting, AUTO (the
      MAIN METERS tap), PRE or POST, starting AUTO (fixed POST); RIGHT SECTION SENDS ON
      FADER off, L/C (ALL); EXCLUSIVE SOLO and SELECT FOLLOWS SOLO on (off);
      FADER/SCREEN LAYER LINK off (on).
    - Sends: bus sends off at -oo (0 dB); from channels and aux PRE to buses 1-8, GRP to
      buses 9-10 and POST to buses 11-16 (all PRE), PAN LINK on only for buses 9-16
      (all); buses 9-16 send POST to buses 1-8 (PRE). Matrix sends at -oo (0 dB)
      without PAN LINK (on), PRE from channels and aux (post-fader). A matrix's direct
      input at -oo (0 dB). A channel's TAP point "5", after the third block of the
      process order (PFL).
    - AUX 2 plays the USB player's PLAY 1/2 (not patched); the player's first pair is
      named "2TR", colour 8, icon 608, stereo (unnamed, Dark Blue, no icon, mono).
    - The RECORDER ports OFF (Main 1 L/R on 1-2); the player's REPEAT on (off); the
      oscillators at -6 dB (-20 dB); MUTEGROUP/SIP OVERRIDE on (off); INPUT SELECT
      GLOBAL (`/io/autoaltovr`, per channel).
    - The DAW layers DAW 1-4 of the custom controls are the console's (transport,
      markers, automation, FLIP, NAME/VALUE, V-POTS, RECRDY, SCRUB, ZOOM; empty).
    - WING remote: an unassigned custom control's `$fname` reads empty ("NOT
      ASSIGNED"); the default layer keys answer the console's names ("CH1-12",
      "BUSES"...), a section spilling nothing `SPILL`; `/$ctl/lib/$actidx` reads 0 with
      no scene active (-1).
    - INITIALIZE CONSOLE (everything) also resets SETUP's surface, solo, metering,
      general and lighting options, the RECORDER ports and REPEAT (MIDI REMOTE
      CONTROL and DAW keep their connection settings).
    - Saved shows load with their own values: shows from before keep the USB player's
      sources, the oscillators, the DAW layers, INPUT SELECT and MUTEGROUP/SIP
      OVERRIDE as they had them.
    - Still different: the local sources' icon (the WING gives LOCAL IN icon 1 and
      AES50 A icon 0; they are the same sources here, kept at 0), the channels' and
      outputs' patch (the audio interface's inputs on the channels, Main 1 on outputs
      1-2), AUX 1 on the WING's own USB audio (no such ports here), the user layers'
      names (USER 1, not USER1), the function names of assigned keys (`$fname`), the
      show name, and what is the machine's (IP, names, clock) or the console's
      hardware (fader and knob positions).
  - A unit test compares the description of every parameter a factory-set WING
    described (`tests/data/wing_desc.tsv`, 29,871 parameters) with Sidecar's; a
    protocol test sets and reads the new mappings over OSC and the native protocol.

### Changed

- A RATIO picked from a list reads as on the WING: "2.0:1" (an expander's "1:2.0"),
  "1.43:1" where the list has hundredths; Sidecar showed "2.00" or "2:1".
- VIEW > FADERS: touching a DCA's name no longer opens its LIGHT/ICON/NAME panel; as on
  the WING, that panel opens from the DCA OVERVIEW title.
- A channel without a source no longer shows "OFF" on its scribble strip: the tag is left
  out, as on the WING.
- Source GAIN now has the WING's range and steps for the source's group, everywhere
  it is set (HOME and INPUT knob and fader, ROUTING > SOURCES fader, knob and wheel,
  the parameter bar, WING REMOTE `/io/in/<grp>/<n>/g` and `/ch/<n>/in/set/$g`, WING
  snapshots, shows and scenes). It was -12 to +60 dB in 0.5 dB steps for every source.
  - LOCAL IN (the full-size WING's own preamps; the same ports as AES50 A here):
    -2.5 to +45 dB in 2.5 dB steps (20 values, `lin [-2.5 .. 45.0 dB], 20 steps`).
  - AES50 B / C (and interface ports past 48): -3 to +45.5 dB in 0.5 dB steps (98
    values, `lin [-3.0 .. 45.5 dB], 98 steps`).
  - WLIVE PLAY, the USB player and the oscillators have no GAIN on the WING, so they
    have none here either: they stay at 0 dB, and the GAIN control reads "-".
  - Gains outside the range, or off its steps, move to the nearest value, also in
    shows and scenes saved before. Sidecar's gain is still digital (it can't reach the
    interface's preamps). The gain thumb, knob and fader follow the range, so 0 dB sits
    near the bottom for LOCAL IN, as in the console's screenshots. C ABI:
    `ld_source_gain_range`.
- Channel, aux, bus, main and matrix delays convert between meters, feet, ms and
  samples as the WING does: 0.3 m per ms (the WING's DIST range, 0.1 to 150 m, is 1/3
  to 500 ms; Sidecar used 343 m/s), never shorter than 16 samples. 0.1 m now reads
  0.3 ft, 0.3 ms and 16 samples at 48 kHz, and delays by 16 samples (it read and
  delayed 14). SAMPLES starts at 16 at every rate (0.1 m is 32 samples at 96 kHz).
  WING REMOTE takes `in/set/dly` and `dly/dly` in the unit's range: up to 24,000
  samples at 48 kHz, where it stopped at 1,000. Saved delays keep their values; their
  time is now the WING's.
- ROUTING's lock starts locked (the closed lock lit red), as on the console: on a new
  console and again after INITIALIZE CONSOLE. It started unlocked.
- ROUTING > CHANNELS: the keys that change the patch, NONE and the new M / ST / M/S
  (the pairing of the channel's source, as in SELECT MAIN INPUT), show only while
  ROUTING is unlocked; locked hides them, as on the WING. They replace Sidecar's own ST
  key, which made the channel stereo without pairing its source. OUTPUTS' NONE too.
- Local inputs are named LCL 1, LCL 2 ... as on the WING (AES50 B / C ports B 1 ...,
  C 1 ...), where they read IN 1 ...: on HOME's source tile, SELECT MAIN INPUT, the ALT
  source list and the scribble strips' source tag (LCL7/8), as ROUTING and the mute
  groups' SOURCE tab already did. Names given to sources are kept.
- The channel TAP points are checked against the WING: they are the nodes of the
  channel's chain in its process order (IN, FILTER, then TAP 3 / 4 / 5 after the first,
  second and third of GATE, EQ, COMP and INS 1, PRE FDR after the fourth, POST FDR,
  POST PROC after INS 2), so the factory TAP 5 with GEDI sits between COMP and INS 1,
  where the WING's HOME screen draws it. The engine already took the sends, keys and
  delay compensation from there; a new test holds it to that for every order.
- Answers from a real WING:
  - The surface's blank strips are blank: a blank user-layer slot, a strip past the
    end of a layer, a strip whose target is gone, a DAW strip without a channel and
    the 4-Channel section's LCD over such a strip show no text at all and stay unlit,
    as on the console. (The DAW custom control section does the same for channels
    the controller doesn't have.)
  - SETUP draws the rows of features Sidecar doesn't have (SHOW METER PAGE WHEN
    LOCKED, MOUSE DISABLES TOUCH, MOUSE SPEED, SEND PAGE ACTIVATES SOLO, TAP TEMPO
    FLASH, the PATCH, GLOW and LAMP tiles, HA REMOTE, the OSC and TCP remote locks,
    SYNC SOURCE, IN/OUT) normally instead of dimmed. Their values are kept (and
    answer over WING REMOTE) but change nothing.
  - The dynamics thumbnails on HOME draw the GATE and COMP transfer curves blue while
    the gate or compressor is on and grey while it's off, as the GATE and COMP
    screens do (they were orange). The model list keeps its orange tiles.
  - GRAPHIC EQ (GEQ): the 31 faders with TRIM and the response curve are on pages of
    their own (FADERS, CURVE), the GEQ TYPE beside the curve.
  - GATED REVERB, REVERSE REVERB and DELAY/REVERB have panels of their own instead of
    the HALL's remote: the envelope (gated or reverse) or the echoes as a graph, beside
    sections for the envelope, the space or density and the EQ (the delay and the
    reverb for DELAY/REVERB), in the style of the other non-remote reverbs.
  - Scenes and snippets that recall a plugin's state also recall its zoom and
    collapse in the plugin window. Scenes stored before keep the current view.

- A third pass of matching the screens to a WING (firmware 3.1), side by side with
  the console's screenshots at 1280 x 800:
  - SETUP: the settings boxes are padded as on the console (4 px, none next to a
    dropdown field), ON keys are 48 x 40 in a 2 px frame 4 px under the top of their
    row with smaller print, dropdown fields (CLOCK RATE, DEVICE, CONSOLE NAME...)
    have the console's 2 px frame, 30 px key and caption / value sizes, MOUSE
    DISABLES TOUCH and MOUSE SPEED have their shorter rows, MONITORS and CC EDIT sit
    against the clock, the INPUT SELECT grid has the console's cell positions, the
    NETWORK IP ADDRESS caption is row-sized over light boxes, and the DAW disclaimer
    is the console's small grey print under the DAW CONTROL column.
  - CUSTOM CONTROLS: the layer map has the console's columns (82 px on a 90 px
    pitch), the encoder tile with a thick open ring, 40 px keys, grey captions and
    titles, a GPIO key drawn like USER, COLOR / NAME / CLR ALL as #303030 keys in a
    grey frame and the list's page keys as full-size triangles.
  - LIBRARY (INT / USB) and the USB recorder's browser: folder rows with the
    console's 32 px > key, chevron and folder sizes, the scroll column (arrows, a
    black track and a grey thumb) right of the rows, file rows with the type chip,
    date and time where the console has them and 20 px names, the TYPE / NAME and
    calendar sort keys as on the console, a blue SHOW key, and files sorted by
    name without their extension ("MARCUS DEFAULT" before "MARCUS DEFAULT 3.0").
    The USB browser fills the rows under the list with black boxes, as on the console.
  - USB player: the time readout is lit red with no clip chosen; the tape reels are
    drawn as the console's (hub ring, spokes, rollers and the head).
  - METERS: the number keys of strips without a source are no longer dimmed.
  - VIEW screens: the status title "C (layers) DCA" and MUTE GROUPS laid out as on
    the console; the DCA label under a DCA (OVERVIEW, FADERS) is black with 22 px
    caps; tiles of inputs without an icon of their own (CONFIG, the assign screens)
    show none.
  - MONITOR: the parameter bar ends at DIST while DIRECT IN is OFF (DIR LVL shows
    once a source is chosen); the LIMITER ring is empty at 0 dB.
- The channel's HOME screens, ROUTING, CHANNEL INIT / COPY and INITIALIZE CONSOLE matched
  again to a WING's (firmware 3.1, measured side by side with the console's screenshots at 1280 x
  800):
  - The block column: blocks 65 px tall at the console's 73 px pitch, the fader bar 16 px,
    the keys 68 px wide with a 44 px key in a black rim, and the console's icons for GATE,
    EQ, COMP, INS and the wrench. The thumbnails fill their block as on the console: the
    chips, LED and 11-segment meters where the console has them, the EQ curve in a plain
    black box, MAIN with numbered main squares and a stereo meter, and SENDS with the
    matrix sends over a black line and the bus sends in two groups. An insert with no FX
    slot no longer shows the premium diamond. The TAP line is drawn in the blocks' grey.
  - OVERVIEW, ICON/COLOR, NAME and TAGS: the panels, tabs (smaller text on unselected
    tabs, the tab bar behind them), SAFE, the LEVEL panel (MUTE, SOLO, the meter's evenly
    spaced CLIP to -60 scale, the fader well with its value tag), the MAIN ring, mains
    squares and SOLO BUS keys, the icon gallery and colour grid, the keyboard (2 px key
    outlines, grey letters, the console's rows) and the tag tiles are at the console's
    positions and sizes.
  - CHANNEL INPUT: the panels, the bracket, the MAIN column's frame and GAIN fader, the
    TRIM & BALANCE pad (to -18 dB, a grey trim handle, a blue balance handle), the trim
    meter with IN under it, INVERT, the FILTER keys and the DELAY panel (its well and value
    tag, larger unit names).
  - GATE / COMP: the transfer graph's scale and line weight as the console's, the curve
    running on to the bottom edge; DETECTOR / AUTO ENV captions. EQ: the response graph's
    ruler, RTA strip and grid, smaller grey rings, band lines that stop short of the
    edges, the console's shelf mark and the band keys' layout. FILTER SETUP: the LC / HC
    lines and rings, the slope keys' grey print.
  - The KEY FILTER and XOVER pages show no status keys (PREV CH ... COPY), as on the
    console. CHANNEL INIT / COPY sit where the console puts them, with an 80 px title, and
    blank the parameter bar; their INIT / COPY key stays lit with no destination chosen.
  - The EQ and DYNAMICS MODEL lists and SELECT CHANNEL (KEY SOURCE) dim only the screen
    they belong to: the block column stays lit and the parameter bar goes blank. ROUTING's
    group menus dim only their own side.
  - The HOME screens' drop-down fields use the console's frame, key and type sizes (as
    SETUP's); knob boxes the console's ring (open 290 degrees, dark grey,
    filled from the start, or from the top for TRIM) and larger captions; the parameter
    bar's text and track sit as on the console, the source GAIN is a gold thumb, TRIM and
    EQ gain fill from the centre, WIDTH fills either side of the centre and the EQ's Q is
    the green spread fill; the track is #707070. The HOME page ends at y 752, where the
    parameter bar starts.
  - ROUTING: the channel's number in the status bar unboxed on CHANNELS, the sources'
    and outputs' title spaced as on the console. INITIALIZE CONSOLE: the group titles in
    the console's size.

### Fixed

- WING REMOTE: the recall safes (`/$ctl/safes`) answer in the console's format, a "+"
  or a space per strip, DCA, mute group or FX slot (they were lists like `1,3-5`,
  which are still taken). A string set over OSC keeps its leading and trailing spaces.
  CH 3's input delay is described in meters like the other channels (the tree had
  caught it in ms). `/play/$actidx` reads 0 with nothing queued, inside its range.
- The panels have graphs where the unit has something to show:
  - EQ curves for the graphic EQs, PIA 560, SPEAKER MANAGER and the channel strips'
    EQ sections. These are computed by the DSP itself, using the new C ABI function
    `ld_model_response`. No structure changes.
  - Echoes for the delays.
  - Envelopes for the reverbs, including gated and reverse.
  - The LFO for FLANGER, PHASER and TREMOLO.
  - The filter of the MOOD FILTER.
  - Limiter and compressor curves.
  - Band levels for COMBINATOR and SUB MONSTER.
  - The dynamic EQs (DYNAMIC EQ, DUAL and TRIPLE) now show the range each band can
    move through and, for cuts, where the gain reduction has it now.
  - The BDX 902 DE-ESSER shows the band it works on and its live cut.
- A test checks that every model has a panel and that every parameter has a control
  on it that moves. With `SIDECAR_FACEPLATE_SNAPSHOT_DIR` set, it also saves every
  page as a picture. LiveDAWTests now takes one test's name as its argument
  (`LiveDAWTests Faceplates`).
- The BDX 902 DE-ESSER and the dynamic EQs, measured with tones, sibilance bursts
  over a low tone and level steps:
  - BDX 902, HF mode: the cut was a high pass subtracted out of phase. It boosted
    up to +0.6 dB just under FREQ and cut only about a third of RANGE above it. A
    6 dB reduction took 0.4 dB off a 4-8 kHz "s". It is now a 12 dB/oct high shelf
    that never boosts: at RANGE 12 it takes 5.2 dB off that "s" and leaves the tone
    under it untouched.
  - BDX 902: RANGE was only a ceiling. RANGE 6 and RANGE 12 both took 3.4 dB off the
    same sibilance. Now it sets the depth: 3.6 dB at 6, 7.2 dB at 12. The detector
    compares the power above FREQ with the whole signal's. It still has no
    threshold, so a vocal 30 dB quieter is de-essed the same.
  - BDX 902: in silence the meter read the full RANGE, because no signal counted
    as "all sibilance". It now reads 0. The first samples of a note are no longer
    cut by the full RANGE. The reduction no longer changes with the sample rate:
    at 8 kHz it read -11.3, -12.0 and -11.1 dB at 44.1, 48 and 96 kHz.
  - Dynamic EQ (DYNAMIC EQ, DUAL, the TRIPLE DEQ FX and the SPEAKER MANAGER's band):
    the LP12 and HP12 shelves rose +0.9 dB past FREQ when cutting and dipped -2.5 dB
    on the other side when boosting. They are now true second-order shelves that
    move monotonically from 0 dB to the gain.
  - The panels draw what the DSP does: the dynamic EQ bands' bells and shelves are
    the DSP's own curves (the bell was drawn about twice as wide), and the 902
    graph shows its detector's high pass and its HF shelf.
  - The DYNAMIC EQ, DUAL and TRIPLE DEQ graphs show each band's live movement,
    up as well as down. Before, a boosting band never moved, and DUAL / TRIPLE
    drew every cutting band at the slot's largest reduction. The DSP reports each
    band's signed gain. New C ABI functions: `ld_strip_dyn_band_gain` (GATE /
    DYNAMICS slot, bands 0-1) and `ld_fx_band_gain` (FX slot, bands 0-2). No
    struct layout changes, so the ABI version stays 15.
  - The WING comparison kit's DYNAMIC EQ test had GAIN 0, so it tested nothing. It
    now has GAIN -12. New tests: the 902 in FULL mode, DUAL DYNAMIC EQ shelves, and
    a FILTER OFF dynamic EQ for clean ATTACK / RELEASE timing.

- Rates below 10 Hz now show two decimals ("0.50 Hz"). LFO speeds used to read
  "0 Hz".
- BLUE PLATE's damping readout is labelled kHz. It read HZ.
- The infinity sign on the BDX 560 and PIA 2250 ratio scales now draws correctly.

- User layers work like the WING's: their slots are fixed, a slot can be blank, and
  the same channel can sit in two (or more) slots of one layer. On VIEW > CONFIG:
  - An item dragged (or tapped) onto a slot's body goes into exactly that slot,
    replacing what was there, instead of being packed to the left with everything
    after it shifting. An item the layer already holds gets another slot; the first
    copy stays where it is (it used to move).
  - Dropped near the boundary between two slots (a gold bar with arrow heads marks
    the spot), items are inserted there and the following slots move one place to
    the right, as on the WING. A trailing blank slot makes room; when the layer's
    last slot (1024) is in use the drop is refused with a LAYER FULL notice (red
    marker) instead of losing a fader.
  - A filled slot dragged onto another swaps with it (or moves into a blank one);
    dragged between two slots it is inserted there and its old slot turns blank;
    dragged off the slots it is blanked.
  - Right-click or long-press a slot for CLEAR SLOT and INSERT EMPTY SLOT (a blank
    slot inserted before it, the rest moving right).
  A blank slot is an inactive strip on the surface (dark scribble strip, light bar
  off, the fader and keys doing nothing), also on the VIEW screens, the custom
  controls, Sends on Fader, spill and the remote controls. Two slots holding the same
  strip are two faders on that strip and follow each other. Blank slots and
  duplicates are saved with the show (a blank as "-"); older shows load unchanged.
  The WING remote maps a blank slot to OFF both ways (OFF used to remove the fader and
  shift the rest) and each copy of a fader reads and sets on its own. C ABI 15:
  `kFaderEmpty` and the user layer slot functions (14), `ld_layers_insert_user_slots`,
  `ld_layers_max_slots`, `ld_layers_remove_from_user` taking the slot and
  `ld_layers_add_to_user` returning whether there was room (15). CONFIG's NEW and
  DELETE keys no longer cover the first slots of a 12-fader section.

- WING REMOTE: dropouts with Mixing Station and WING Edit, found with simulated
  clients run for minutes (a soak test, `livedaw-wing/tests/soak.rs`, now runs a
  short version in CI):
  - A Native client (WING Edit) that read slowly, on busy Wi-Fi or while syncing the
    tree, held up everything: every change pushed to it waited for its connection,
    so OSC replies (Mixing Station) stopped for seconds, meters stopped for every
    client and, after 2 s, the slow client was disconnected (and reconnected, and
    synced again...). Each Native client now has its own send queue and writer; a
    client is dropped only when its connection takes nothing for 10 s.
  - Two OSC remotes (Mixing Station on two tablets, or with Companion) took the
    event subscription from each other at every renewal, so each stopped seeing
    changes for up to 10 s at a time. Every OSC client that subscribes is now served,
    and one that subscribed with a reply port (`/%<port>/*S`) no longer gets its own
    changes echoed back.
  - A meter renewal sent as the protocol document shows it (`df d3 d4 <id>`, nothing
    after it) was applied only at the client's next channel switch: meters froze for
    about a second after the first 5 s, and again whenever the client was idle.
  - After a scene recall, the hundreds of OSC events left in one burst and overflowed
    the client's receive buffer (a quarter of them were lost even on the same
    computer): they are now paced at 8 per millisecond.
  - Subscriptions last 20% longer than the console's 10 s (OSC) and 5 s (meters), so
    a renewal delayed by the network doesn't open a gap.
  - Windows: a client that went away (its port unreachable) paused the OSC and
    discovery servers on each error report, delaying the other clients' replies.
- 1366 x 768 screens: the window opened at 1680 x 1000, off the top and left of a
  laptop screen with the start-up notice's buttons out of reach. It now opens fitted
  and centred in the main display's work area (a size saved on a bigger screen is
  fitted too, title bar included), and the smallest window is 1280 x 640 (a 1280 x 720
  screen less taskbar and title bar; was 1344 x 680).
- Short windows keep the main display's screen legible (360 px tall, body text about
  9 px, as in a 1680 x 1000 window) instead of shrinking it to about 285 px: the
  channel strip and the monitoring / 4-Channel column narrow to make room, the fader
  sections get the rest and fold their strips' meter blocks away below 100 px of
  fader travel (the screen, the channel strip and the main meter still show levels),
  and the layer keys pack closer so all of them stay on the surface. Large windows
  are unchanged.
- A sandboxed plugin's editor window larger than the screen opens with its title bar
  on screen (and shrinks to the work area when the editor is resizable).
- Found by testing with real plugins (the LSP plugins, VST3 and LV2, now covered by
  a test that runs them when they are installed):
  - Linux: a show with a VST3 plugin loaded in-process didn't load it back ("No
    compatible plug-in format exists"): the show stored the library inside the
    plugin's bundle. Such shows now load (also with the sandbox on), and shows now
    store the description the plugin was loaded from (the bundle). Sidecar no
    longer asks a loaded plugin for its description (the EFFECTS screen did on every
    repaint), which made LSP's VST3 plugins leak a small host object each.
  - A mono plugin (no stereo layout, such as LSP's "Mono" versions) on a stereo
    channel silenced the right side. It now gets both sides mixed to mono and its
    output feeds both (in-process, in a chain and in the sandbox).
  - The plugin window's zoom ended at FIXED for VST3 editors on Linux, which take
    their real size only once shown: they now shrink by the host scale. An editor
    that goes back to its own size after being resized smaller (LSP's LV2 UIs) now
    tries the host scale, then shows FIXED, instead of claiming the zoom.
  - The plugin window grew off the screen when the plugin's editor took its real
    size after the window was placed (a 1366 x 768 screen); it now stays on screen.
  - Sandbox: a parameter set right after a plugin loaded (a scene recall, a
    remote) snapped back to its old value in Sidecar 300 ms later, though the
    plugin had it.
  - Sandbox: the helper no longer asks a plugin whether it has an editor while
    loading it (Sidecar didn't use the answer); LSP's VST3 plugins crashed there
    without a display.

## [0.1.4] - beta

The second round of matching Sidecar to a real WING, from screenshots, photos and a
parameter dump of a console: the MONITOR / TALKBACK, MUTE GROUPS and DCA screens (with
renaming, the DCA SOLO BUS and the SOURCE tab), METERS with its PRE / POST settings, the
USB recorder and player, the EFFECTS rack and the COMP page with its KEY FILTER and
XOVER pages now follow the console's own screens; the monitor, solo and RTA settings
start as a factory-initialized WING; the surface's keys, LEDs and light bars are lit
like the console's, with a black finish beside the silver one; and a WING snapshot
loads from LIBRARY as on the console. New: plugin chains, up to 8 plugins in one FX
slot, shown side by side in one window with zoom, collapse and per-plugin fault
bypass. The screens use less CPU. Known limitations: see the
[README](https://github.com/Audbol/Live-Daw/blob/main/README.md#known-limitations).

### Added

- Plugin chains: an EXT FX slot runs up to 8 plugins in series (VST3, AU, LV2, each one
  sandboxed or not), shown in one window: a chain bar (IN ▸ chips ▸ OUT, +; a chip
  shows the name, a bypass dot, a fault mark and the latency, scrolls to its plugin,
  drags to reorder and has a right-click menu) over the editors as cards with a slim
  title row (collapse, name, BYP, zoom − % +, remove). ROW shows the editors side by
  side, FOCUS one at a time with the chips as tabs; the choice and the window size are
  kept per slot. Each plugin can be zoomed to 50-100 % (resized, host-scaled for VST3 /
  VST / LV2, or transform-scaled for generic and built-in editors; fixed-size native
  editors say FIXED) or collapsed, and FIT fits the whole chain to the window's
  height; zoom and collapse are saved per plugin with the show. A plugin that throws,
  outputs NaN/Inf or fails in the sandbox bypasses only itself: its input passes on,
  the rest of the chain keeps running, its header shows FAULT with CLEAR, and the
  event log names it ("FX 3: Pro-Q 3 (plugin 2 of 3) output NaN/Inf; bypassed, the rest
  of the chain keeps running"); a chain of one faults the slot as a single plugin does.
  The FX slot menu has Add to chain, and the EFFECTS tile names the chain ("A + B",
  "A +2"). A plugin's latency counts unless it is bypassed or faulted. Changing the
  chain keeps the other plugins running with their state. Shows, scenes, snippets and
  FX presets store and recall the whole chain; a single plugin is saved as before.
- `tools/wing/audio_compare` compares the sound of a WING's processing with Sidecar's:
  test signals for the WING's USB player (sweeps, level steps, level jumps, tone steps,
  noise, clicks, a synthetic band) with sync chirps and an ID code, a test plan of 65
  channel settings in order of importance (WING EQ, filters, COMP and GATE first, then
  the other EQ and dynamics models, key filter, crossover and FX), an optional OSC
  helper that sets the WING up and reads the values back, and an analysis that aligns
  the recordings (latency, clock drift, level) and reports the differences with
  tolerances: magnitude and phase, harmonics, static curves, attack / hold / release,
  reverb decay, delay echoes, modulation. Sidecar's side is rendered offline by the
  engine with the new `render` example of livedaw-wing (WING parameter paths in, WAV
  out).
- SETUP > GENERAL > CONSOLE > SURFACE FINISH: the surface view as the silver WING
  (default) or the black WING BK (black surface, printed frames, frosted light keys,
  silver fader caps). It is kept in the app settings, not the show, and switches the
  surface at once; the touchscreen is the same in both.
- Web remote: SETUP > REMOTE > WEB REMOTE serves Sidecar's screen to browsers on the
  local network (`http://<this computer>:8080/`, port configurable). The page mirrors
  the window and sends the mouse, touch, wheel and keys back; optional 4-8 digit PIN
  (checked by Sidecar, wrong guesses rate-limited) and VIEW ONLY. Off by default.

### Changed

- Lower CPU use with the VIEW screens (OVERVIEW, FADERS, SENDS, CONFIG, MUTE GROUPS),
  METERS and CHANNEL SELECT open: they redraw only when something they show changes,
  instead of the whole screen 15-25 times a second.
- The monitors start with a WING's factory settings, read from a console's parameter dump
  after INITIALIZE: SOLO MODE LIVE, the solo bus PH (phones), mains and matrices solo PFL,
  PFL DIM 12 dB and BAND SOLO 0 dB on both paths, the monitor EQs' bands at 60.1, 129.9,
  299.2, 699.5 Hz, 1.50, 2.99 and 6.01 kHz with Q 2.0, talkback AUTO DIM (BUS DIM, MON
  DIM) 0 dB, and the RTAs at 30 dB range, MED decay and PEAK detector. Saved shows keep
  their settings.
- DCAs have a SOLO BUS (`/dca/n/mon`: A = phones, B = speakers, A+B), saved with the
  show: in SOLO MODE LIVE a soloed DCA sends its members' solo there (their own solo
  still goes to their own SOLO BUS).
- Renaming DCAs and mute groups as on the WING, instead of touching the selected tab of an
  assign screen again: a DCA's title tile on its assign screen (and its name on FADERS)
  opens the DCA's panel (LIGHT and the 18 colours in the console's order, the icon
  gallery, SOLO BUS, the name field and the keyboard); a mute group's header on MUTE
  GROUPS opens the MUTE GROUP n window. The SOURCE tab of a mute group's assign screen
  shows one source group at a time, eight sources to a row, with the source groups as
  tiles on the right, instead of 64 sources a page.
- The DCA and mute group columns of OVERVIEW and MUTE GROUPS fill a member's cell in the
  member's own colour, and a DCA's "DCA" label (OVERVIEW, FADERS) turns yellow while the
  DCA is soloed. The layer column's section key is darker olive while a bank is shown.
- METERS, the USB recorder / player, the EFFECTS rack and a channel's COMP page with
  its KEY FILTER and XOVER pages laid out and coloured like a WING's (firmware 3.1,
  measured off the console's screens). METERS: the WING's group and scale geometry,
  61-segment meters (yellow from -18 dB), M keys, DCAs in pairs, and a **SETTINGS**
  key that sets PRE or POST per strip type (channels and DCAs PRE, the others POST, as
  a factory-set console; the METERS screen used to show every meter pre-fader). USB:
  the PLAYLIST tab, ADD ALL (with nothing selected, the folder shown), folder rows in
  blue with their own `>` key, upright red counters without colons, a yellow square
  while stopped, keys framed while enabled, and a blank player with PLAY off until a
  clip is chosen. EFFECTS: no parameter bar, the slots where the console has them, a
  scratched-metal rack, and a device inserted nowhere shown dimmed (no ON or source
  column); reverb decays show two decimals ("1.50 s"). COMP: the transfer curve blue
  while on, the ENVELOPE grey for the compressor whether on or not, the dot grid and
  ticks as the console's; the KEY FILTER / XOVER pages have the plot where the console
  has it, an open FREQ ring, Q and DEPTH boxes, DEPTH, Q and FREQ on knobs 3-5, and the
  XOVER curve for PEAK shows the band taken down by DEPTH (a dip at FREQ). Q on the
  parameter bar is a green fill either side of the centre, wider for a lower Q; the
  X/O tag lights blue; the ticks right of the COMP and GATE transfer graphs are plain grey
  lines, not an output meter, and the yellow level dot trails a short fading tail;
  double-clicking an EQ band's handle on the graph, or its band key, with the mouse resets
  the band to the model's defaults (touches don't, so taps can't reset one by accident); lit status-bar keys are #707070; the HOME screens keep 8 px
  margins left and right.
- MONITOR / TALKBACK, MUTE GROUPS and the VIEW screens with a DCA layer (OVERVIEW,
  FADERS, SENDS, CONFIG) are laid out like a WING's (firmware 3.1, measured off the
  console's screens): the layer column with bank keys (1-8 ... DCA 9-16), DCA and mute
  group columns showing their members, assign screens for a DCA's or mute group's members
  (tiles framed gold, UNASSIGN, CLOSE; a mute group's SOURCE tab), the talk side dim
  while no talkback channel is set, and a linear DIST slider. The monitor parameter bar
  no longer has TALK LVL (the surface's TALK LEVEL knob sets it).
- SETUP starts with the WING's factory settings (checked on a console after INITIALIZE):
  USE CRSR/WHEEL, SHOW SOF FRAME, CH STRIP TOUCH SELECT and TAP TEMPO FLASH on, SEL DBL
  CLICK BUSFX, MAIN METER TAP POST. Settings you've changed are kept.
- The 18 strip colours have the WING's numbers, read from a console swatch by swatch (they
  had been guessed apart from colour 1): shows and WING snapshots now come up in the
  console's colours, and new buses are Salmon (1-8), Lime (9-10) and Magenta (11-16).
- The surface's SELECT, SOLO, MUTE and SOF FLIP keys light as on the console: a dark face in
  a backlit rim, pale lavender unlit (light grey on the silver finish), the rim and print
  green, orange or red lit while the face stays dark. The meter block's GATE LED is red
  (gate closed) and its DYN LED orange (dynamics working); the center section's USER,
  2-TRACK, MULTI, MUTE GROUPS and SHOW CNTRL keys are light keys, as on the console.
- The surface's keys and LCDs as in photos of a silver WING: lit layer, function and mute
  group keys and the SOF FLIP rim are light orange (brighter in the middle), lit VIEW keys
  bright green; the screen keys beside the display (HOME, EFFECTS...) are backlit keys, a
  black face in a lavender rim with lit print, rim and print green for the screen shown,
  and CLR SOLO the same with an orange rim that blinks while anything is soloed; the light bars light DARK BLUE (colour 1, the
  inputs' and mains' default) plain blue, as the console's LEDs do; in MUTE GROUPS the custom controls' lower LCD shows "MUTE
  GROUPS" over the eight group names in four columns, the active ones inverted, over
  blank keys; the 4-Channel section's LCD leaves the key row empty under PAN for a main or
  matrix (no MAIN 1 send); the CHANNEL STRIP DETAILS and GROUPS LEDs are green. On the black finish the keys are backlit lavender with
  dark navy print (a key with nothing to do shows its grey cap), as on a black WING.
- A WING snapshot loads the console's way: on LIBRARY > SNAP, select the `.snap` file at INT
  or USB and press LOAD (or double-click it). IMPORT WING on the SHOW tab still works.
- The center fader section has no INPUTS layer key, as on the console (DCA, MAIN MATRIX,
  AUX IN, BUS MASTER, USER 1, USER 2); shows that had it selected there start on DCA.
- The channel number in the HOME header is drawn in exactly the strip's colour, as on the
  console (dark colours were lightened); the factory colour is named Dark Blue, as on the WING.
- WING COMP and WING GATE time and bend as recordings of a WING do: ENVELOPE LIN moves the
  gain in straight lines of 10 dB per ATTACK / RELEASE time and LOG exponentially with
  them as time constants, the release gets going over twice the HOLD time, AUTO has its own
  program-dependent attack and release, the RMS detector is faster (5 ms) and KNEE bends
  over 7.2 dB per step; the GATE opens and closes on a linear gain scale (ATTACK and
  RELEASE are 10-90 % times), ratio GATE closes in a straight line over RELEASE and RANGE
  60 closes completely.
- The channel filter's HIGH CUT keeps its slope up to the top of the band, as on the console
  (a 5 kHz cut at 6 dB/oct is -13 dB at 20 kHz, not -21).
- The FILTER tool's TILT goes the console's way and as far: TILT +3 lifts the lows by 3 dB
  and lowers the highs by 3 dB on a gentle slope through about 700 Hz (Sidecar tilted the
  other way, by half as much).
- The WING EQ's bells are as wide as a WING's: the set Q is the resonance of the boosting
  (or, for a cut, the cutting) section, so the filter's Q is the set Q over 10^(|gain| / 40)
  (measured on a console: Q 1.00 at +6 dB is as wide as a Q 0.707 peaking filter). High
  bells keep their shape up to the top of the band, as on the console (a 12 kHz bell no
  longer drops 1.8 dB short at 16-20 kHz). The shelves' frequency is where the boost (or
  cut) section turns, as on the console, so a shelf's midpoint lies 10^(|gain| / 80) further
  out (a +6 dB low shelf at 80 Hz is half way at 95 Hz), and their slope is always Q 0.707;
  a shelf band has no Q knob, as on the console.
- Scribble strip LCDs have one backlight colour on every strip, as on the console; only
  the light bar above each strip shows the strip's colour.
- The +48V keys (HOME OVERVIEW and INPUT, ROUTING > SOURCES, and the 4-Channel
  section's keys in GAIN / 48 V mode) switch phantom power only when held for 2 s, as
  on the console; the key fills up while held, and a tap does nothing. Remote control,
  snapshots and scenes still switch it at once.
- A patched channel's scribble strip with neither name nor icon shows its source's glyph
  (the XLR connector for a local input), as on the console.
- The control surface looks like the silver WING, matched to photos of a real console:
  a light grey surface with mid grey decks and a black display unit; light grey keys
  and dark grey SELECT / SOLO / MUTE / SOF FLIP keys in a light bevel, with the console's
  smaller, wider print; black fader caps with a white line and dark printed scales;
  frosted grey unlit LEDs with the printed dashes beside the meter labels; paler
  periwinkle scribble LCDs under thicker light bars; larger channel strip keys and
  captions; blue digits on the custom controls' layer display; framed TALK A / B
  letters. The 4-Channel LCD has no light bars, as on the console.
- A mono channel's pan follows the WING's law, measured on a console: unity on both
  sides at centre, the near side rising to +3 dB and the far side falling to off at the
  edge (Sidecar used a -3 dB centre).

### Fixed

- A slot's plugin window closes when its plugins are removed right after its chain was
  changed from elsewhere (it could stay open for a moment and outlive the editors' plugins).
- An FX slot auto-bypassed by a failing plugin ran that plugin again as soon as its
  chain was edited: zooming or collapsing the plugin in the plugin window, bypassing it,
  or adding or moving a plugin cleared the slot's fault. The slot now stays bypassed
  until you clear the fault.

## [0.1.3] - beta

The first round of matching Sidecar to a real WING, side by side with the console:
HOME, the input page, INSERT and the FX menus, the EFFECTS rack, ROUTING, SETUP,
LIBRARY, CUSTOM CONTROLS, CHANNEL COPY / INIT, a full INITIALIZE CONSOLE page, and the
GATE, EQ, FILTER and COMP pages now follow the console's own screens; the scribble
strips are lit like the console's; a new console starts as a factory-initialized WING
(faders, EQ, names, colours, gate, compressor, filter and EQ settings read from a real
console); and the WING remote answers in the console's exact format. A tool to compare
Sidecar with a WING parameter by parameter is included. Known limitations: see the
[README](https://github.com/Audbol/Live-Daw/blob/main/README.md#known-limitations).

### Added

- `tools/wing/wing_compare.py` reads every parameter from a WING and from Sidecar
  over OSC (read-only) and reports what differs, for checking Sidecar against a real
  console.
- INITIALIZE CONSOLE is a page: choose the strips, contents, DCAs, mute groups, FX
  slots and source groups to reset (everything: a fresh 40-channel console, as before).

### Changed

- A new console starts as a factory-initialized WING (checked against a parameter
  dump of one): every fader at -oo except buses 9-16 (0 dB, into Main 1), every EQ
  off, strips and DCAs unnamed (mute groups MGRP.1 ...), DCA lights off, channels and
  aux with LINK CUSTOMIZATION TO SOURCE on (the strip shows its source's name, colour
  and icon; ICON/COLOR and NAME then edit the source), and the input delay in METERS
  at 0.1 m (the WING's range, 0.1 to 150 m, in every unit). Shows saved before keep
  their strips' own customization. Engine ABI version 12.
- The 18 strip colours are the full-size WING's (measured on its ICON/COLOR screen and
  laid out as there); colour 1, the factory colour of channels, aux, mains, matrices,
  sources and DCAs, is Purple; buses start Pink (1-8), Olive (9-10) and Violet (11-16).
- HOME matches the console's screens more closely: the source's GAIN and +48V on the
  OVERVIEW and GAIN in the parameter bar of all four HOME tabs (which all have
  SETTINGS), the redrawn general icons, the WING's TAGS tiles (MGRP1 ...), the status
  tile's A B C / SC chips (the DSP load stays on UTILITY), the LEVEL scale without
  -40, the olive SOLO BUS keys, NAME's keyboard starting in capitals with source-based
  suggestions, an empty ALT tile with a plus, and SELECT MAIN INPUT (touch the MAIN
  tile on INPUT: source groups, NONE, M / ST / M/S, COPY CUSTOMIZATION) in place of the
  ST key.
- Scribble strips are lit in the strip's colour with large two-row names; an unlit
  colour light bar is light grey. The MAIN LCD brightness default no longer dims the
  screen.
- WING remote: OSC replies and node descriptions are formatted as on a real WING
  (firmware 3.1): no "r/o", nodes as "(node)", ranges and values with the
  parameter's fixed decimals, "+" on signed ranges and kHz notation ("lin [-15.0 ..
  +15.0 dB]", "log [20 .. 20k0 Hz]", "1k0"), units on integer ranges, float choices
  as `list [...]`, running times as "0:00", and an integer reply's number counted
  from the bottom of its range. Floats set over OSC, the Native protocol or from a
  snapshot land on the parameter's nearest step, as on the console, and the 0..1
  positions are the steps'. The fader law below -60 dB follows the console (-90 dB
  is the bottom of the travel).
- The strip LED meters light -6 to -18 in orange, as on the console.
- SET CUST names the channel after the device's short name (DIMCRS) and gives it the
  FX icon; the INS tile shows the premium diamond for a device in FX 1-8; the EFFECTS
  rack has no FX MIX knobs.
- INS 1 / INS 2, the FX TYPE list and the EFFECTS rack look like the WING's 3.1
  screens: FX PROCESSOR opens the SELECT FX PROCESSOR window (the 16 slots in four
  columns, who uses each), FX TYPE a list of device tiles with the FX1-8 / STD / CH
  tabs in the console's order, and FX TYPE, RESET and FX SPILL only show once a slot
  is chosen. SET CUST is on every strip's INS. A few device names follow the console's
  screen (DELAY/REVERB, DIMENSION CRS, 2-BAND DEESSER, TREMOLO/PANNER).
- **ROUTING** is laid out like the console's screen (firmware 3.1): its proportions,
  colours and fonts, the CHANNEL INPUT MAIN | ALT keys, +1 AUTO and the lock, the
  tiles (patched channels show their source's icon and label, unpatched ones only
  their number), the SOURCES settings boxes and the OUTPUTS tiles with their meter
  scale. The group selectors open the console's icon menus with its groups: the
  interface's inputs and outputs are **LOCAL IN** / **LOCAL OUT** (LCL 1 ...; ports
  past 48 in AES50 B / C), the recorder's playback is **WLIVE PLAY**, the channels'
  own outputs are under **USER SIGNAL**; groups Sidecar doesn't have show n/a tiles.
  +1 AUTO is off by default, as on the console.
- SETUP, LIBRARY, CUSTOM CONTROLS and CHANNEL COPY / INIT are laid out like the
  console's own screens at 1280 x 800: SETUP's 64 px headers, 52 / 56 px rows and
  key sizes, the GENERAL lighting tiles (CH STRIP, GLOW), the option order and colours
  of every tab (a lit OFF is grey, PFL / PRE red, HS and USB green), SEND PAGE
  ACTIVATES SOLO (not there yet); LIBRARY's toolbar, file rows, tabs, key bands and
  its orange title tile; the USER key on CUSTOM CONTROLS; CHANNEL COPY / INIT as a
  window over the page.
- SURFACE's section widths, SHIFT STEP and FADER VALUES moved to FADER SECTIONS (the
  SECTIONS key in the FADERS header); REMOTE's MIDI ports sit under MIDI REMOTE CONTROL
  and show whether each device is open.
- GATE, COMP, EQ and FILTER SETUP look like a WING at factory settings: the panel
  rows and positions, KEY TAP only while a channel keys the slot, the console's
  names (FLAT / LOWPASS ..., WING GATE, LMT COMPRESSOR ...), DETECTOR / AUTO ENV
  keys in blue, the EQ header's LO CUT / HI CUT / TILT EQ keys and FILTER field,
  band 4 selected, the TOOL FILTER icons and "0.00 dB" for TILT.
- The KEY FILTER and XOVER MODE `>` keys open GATE KEY FILTER / COMP XOVER (the
  filter's response filled green or blue, its type, the solo key, FREQ on the knobs);
  KEY SOURCE opens SELECT CHANNEL (SELF and the channel tiles); the dynamics and EQ
  model lists are the console's picture grids.
- New channels start with a factory-set WING's values: GATE -40 dB, 1:3, RANGE
  40 dB, ATTACK 10 ms, HOLD 10 ms; COMP -10 dB, KNEE 3, ATTACK 50 ms, HOLD 20 ms,
  AUTO ENV on; aux channels' dynamics slot the WING GATE at -36 dB; key filters 1k0 Hz
  Q 2.00, crossover 1k0 Hz Q 0.40 DEPTH 6 dB; LC 100.2 Hz at 24 dB/oct, HC 10k02 Hz;
  the WING EQ's bands at 80.2, 200, 601.4 Hz, 1k50, 3k99, 12k00 (buses, mains and
  matrices 60.1, 129.9, 299.2, 699.5 Hz, 1k50, 2k99, 6k01, 12k00), every band at
  Q 1.00; the TAP EQ at Q 1.00. The values are the console's own (on its parameter
  steps), read from a factory-set WING. Saved shows keep their values.

## [0.1.2] - beta

A fix for a crash in 0.1.1 and earlier: scrolling the start-up notice could close
Sidecar. Everything else is as in 0.1.1; see its notes below. Known limitations: see
the [README](https://github.com/Audbol/Live-Daw/blob/main/README.md#known-limitations).

### Fixed

- Scrolling the start-up notice (or the ABOUT text) could crash Sidecar: a scroll the
  text couldn't use (at its end, or sideways on a trackpad) went back and forth
  between the window and its scroll view until the stack ran out.

## [0.1.1] - beta

The second beta: plugin delay compensation, an optional plugin sandbox (experimental),
the WING's oscillator, scene and snippet files, the CLIP player, SEND MIDI, MIDI CC
faders, gate and dynamics KEY TAP, matrix send PAN LINK, much more of the WING remote
protocol (88.2 % of its parameters) and many fixes found by using the console end to
end. Known limitations: see the
[README](https://github.com/Audbol/Live-Daw/blob/main/README.md#known-limitations).

### Added

- The WING's **oscillator**: the OSCILLATOR source group on ROUTING > SOURCES has OSC 1
  and OSC 2, which generate a sine (20 Hz to 20 kHz), pink or white noise at -40 to
  -6 dBFS. Patch them to channels like any input; SOURCES shows SINE / PINK / WHITE,
  FREQ and LEVEL for them, MUTE switches them off and STEREO pairs them. Saved with
  the show and driven by WING remote apps (`/io/in/OSC`).
- LIBRARY > SHOW > **SEND MIDI**: MIDI data (hexadecimal bytes, `U:` / `D:` for the
  USB / DIN port) that a scene sends each time it is recalled, on the EDIT MIDI TEXT
  STRING window; it goes out of EXTERNAL MIDI CONTROL's port and is saved with the
  show.
- **MIDI CC faders** in the layers (VIEW > CONFIG, the MIDI CC field): a fader sends
  its CC out of EXTERNAL MIDI CONTROL and follows the CC coming in.
- **FADER SPILL** custom control keys: spill a DCA, a bus's sources, an FX slot,
  automix group X / Y or the selected channel's sends onto the left section.
- An **RTA on MONITORS EQ** (the RTA key): the monitor path's spectrum, PRE or POST
  its EQ, drawn as EQ/RTA SETTINGS say.
- LIBRARY: scene and snippet files. EXPORT on the SNAP and SNIP tabs saves the selected
  scene (with its snippets) or snippet as a `.lsnap` / `.lsnip` file in the INT or USB
  folder; IMPORT (or a double-click on the file) adds it to the show.
- LIBRARY: the CLIP tab plays an audio clip (WAV, FLAC, AIFF, MP3) chosen at INT / USB
  on the USB player, with PLAY, PAUSE, STOP, meter, time and format.
- WING remote: more of the WING 3.1 parameter tree drives the console: 88.2 % of its parameters
  (60,491 of 68,556, up from 82.8 % in 0.1.0). New: LISTEN from remote apps (BAND SOLO with
  its band, KEY SOLO of the GATE and DYNAMICS slots, XOVER SOLO on channels, aux,
  buses, mains and matrices), the inserts' state, which strip uses each FX slot, a
  channel's source GAIN and +48V, the active RTA source and settings, the monitor
  paths' actual level, scene control (`/$ctl/lib`: GO, GO NEXT, GO PREV, GO TAG,
  PREV / NEXT, the current scene and show), and the show's fader layers and custom
  controls both ways (`/$ctl/layer`, `/$ctl/user`: layer keys, spill, scroll
  position, user layer names and fader assignments, edited layer keys, each custom
  control's functions, name and colour, the CC layer).
- **Matrix send PAN LINK** (HOME > MATRIX SENDS, the pan ring's pop-up; FEED
  CONFIGURATION's SEND PANNING): a channel's or aux's matrix send pans with the
  channel (LINK REVERSED mirrors it), or with its own PAN when unlinked. Saved with
  shows and scenes.
- A DCA's **icon** and scribble strip **LIGHT** (VIEW > OVERVIEW, the DCA's name),
  shown on the fader strips and saved with the show.
- WING remote also drives the matrix sends' PAN LINK, `$muteovr` on every strip,
  DCA `icon` / `led`, the USB recorder and player (`/rec`, `/play`: transport, PLAYFILE, position,
  REPEAT, REC PATH, format, state and file) and the recall safes (`/$ctl/safes`:
  strips, DCAs, mute groups, FX).
- Plugin delay compensation: the latency each hosted plugin reports (and its
  changes) is compensated so that every source summed into a bus, main or matrix
  arrives in time, across bus stages; a plugin's FX MIX delays its dry signal to
  match. On by default; SETUP > PLUGINS turns it off and shows the total
  compensation and each loaded plugin's latency. Compensation stops at 8192
  samples; a plugin reporting more is logged and the limit is shown.
- Experimental plugin sandbox: SETUP > PLUGINS > RUN PLUGINS IN SANDBOX (off by
  default) runs each plugin in its own helper process (`SidecarPluginHost`, shipped
  next to the app). Audio goes through shared memory with a bounded wait, so a plugin
  that crashes or hangs only bypasses its FX slot (dry signal, event log entry); the
  helper is restarted with the plugin's last known state. State save / restore and
  parameter changes work through the sandbox; the plugin's own editor opens in a
  window of the helper process. The sandbox's one block of latency is reported like a
  plugin's own, so delay compensation covers it.
- GATE / DYNAMICS **KEY TAP**: a key from another channel (KEY SOURCE) is taken at
  that channel's tap point: IN (after the input trim), FILT, 3, 4, 5, PFL, AFL or
  POST, so a key taken AFL or POST follows the source's fader and mute. A key from a
  channel later in the console arrives in the same block (channels run in order of
  their keys); keys that form a cycle, or a channel keyed by itself, are one block
  late. Saved with shows and scenes and set from WING remote apps (`gatesc/tap`,
  `dynsc/tap` on channels). Until now a key from another channel was always that
  channel's raw input, before its trim.

### Changed

- The default console name on the network (WING discovery, MIDI identity) is now
  SIDECAR instead of LIVE-DAW, and the WING remote reports the model "sidecar". A
  name set on SETUP > GENERAL is kept.
- Matrix sends of input and aux channels pan instead of balancing the channel's
  panned output: linked (the default) they follow the channel's pan, unlinked they
  use their own. Shows from before load a centred send linked (it sounds as before)
  and a panned one with its pan. The engine's ABI version is 11.

- Scene fades also glide DCA faders, strip widths and the bus and matrix send pans
  (before: faders, pans and send levels only). Switches and settings still apply when
  the fade starts.
- Scene fades move levels (faders, sends, DCAs) along the fader's travel, as a moving
  fader does, instead of in a straight line in dB; a fade up from -oo is now heard from
  the start rather than only in its second half.
- Channel strip section with an analog EQ model: SHIFT pages through all of the
  model's parameters four at a time (1-4, 5-8, 9-12, 13-16), so models with 12-16
  parameters are reachable from the encoders; the captions show the page and the
  parameter on each encoder.
- Only the custom controls' GPIO and SD functions (no hardware) are still drawn
  disabled; the start-up notice and the known limitations say so.

### Fixed

- A scene fade overwrote a fader (or send, pan, DCA) moved while it ran until the
  fade ended. Moving one now takes it out of the fade, from the surface, the screen,
  MIDI, a DAW remote or a WING remote.
- The send pan pop-up (HOME > BUS / MATRIX SENDS, the pan ring) had only an on/off
  link key, so LINK REVERSED could only be set on FEED CONFIGURATION. Its PAN LINK is
  now OFF / LINK / LINK REVERSED.
- HI CUT reset (double-click) went to 10 kHz while a new or INITed channel has 18 kHz;
  it now resets to 18 kHz on the FILTER screen, the channel strip section and the
  4-Channel section. Double-clicking the LC, HC (or all-pass) line on the FILTER graph
  resets it too.
- ROUTING repainted its whole page ten times a second for the source meters; on a slow
  machine (or a debug build) with a large display the screen then stopped reacting to
  clicks. Only the meters that move are repainted now, the rest twice a second.
- The window reopens fitted to its screen: a size saved on a bigger screen (an external
  monitor since unplugged) left part of the surface off screen.
- Recorder: RECORD ALL CHANNELS (or any track) failed with "Input ... doesn't exist"
  for a channel patched to a USB PLAYER source; those sources always exist.
- HOME > NAME took no typing from the computer keyboard until the on-screen keyboard
  was clicked, and a space typed at the end of a name vanished; the default name
  ("Ch 2") now starts as an empty field, so typing replaces it.
- Scribble strips: a long name keeps its size, runs on to the right and breaks onto a
  second row at a space, as on the WING, instead of being squeezed onto one row.
- VIEW > CONFIG: deleting the user layer on USER 1 moved USER 2's layer onto USER 1 and
  left nothing to delete on USER 2. The USER keys now keep their layers, and new user
  layers are named after their key instead of copying the shown layer's name.
- The recorder screen's keys, labels and readouts fit their boxes at every window
  size, from 1152 x 680 to 4K and ultrawide.
- ROUTING > SOURCES: the GAIN knob set only the selected source of a stereo or
  mid/side pair; on the pair's second source it changed nothing you could hear (the
  pair plays at the first source's gain). It now sets both, in 0.5 dB steps, like
  the fader and the wheel.
- Spills (DCA SPILL, a bus's sources) didn't follow the console: a strip assigned to
  the spilled DCA, or a send switched on to the spilled bus, only showed up after
  another layer change. The spill now updates at once.
- HOME > NAME: the name field always drew its caret at the end and dropped a space
  typed at the end, so after the arrow keys moved the caret, typing went somewhere
  else than the caret showed. The field now shows the text as typed with the caret
  where typing goes.
- 4-Channel section: in GAIN / 48 V mode the keys under the encoders did nothing;
  they now switch the +48V of the strip's source (stored, as on HOME > INPUT and
  ROUTING > SOURCES) and light while it is on.
- ROUTING > SOURCES: switching a source to STEREO or MID/SIDE left the channels
  already patched to it mono (they only heard the left source), and switching back
  to MONO left them stereo. Channels and aux on the pair's first source now follow
  its mode, as on the console and as WING remote apps already did.
- HOME > OVERVIEW: the INPUT panel's source MUTE key was always disabled (from
  before sources were modelled). It now mutes the channel's source (both sources of
  a pair) as ROUTING > SOURCES does, and lights while the source is muted.
- VIEW > MUTE GROUPS: the SOURCES box always showed "-", even with sources tagged to
  the group on ROUTING > SOURCES > TAGS, and UNASSIGN left those tags. The box now
  shows the tagged source (or how many) and UNASSIGN clears them too.
- LIBRARY > SHOW > SAVE AS: the show takes the new name (it kept the old one, so the
  title bar and the LIBRARY showed the name of the show it was copied from).
- LIBRARY: renaming the open show's file (RENAME at INT / USB) left the show saving to
  the old file name, so the next SAVE brought the old file back.
- The parameter bar under the screen printed a long name and value over each other
  (recorder screen: POSITION, LOOP OUT).
- Two-line key captions were cut or wrapped onto a third line when the key was narrow
  (LIBRARY GO PREV / GO NEXT, recorder RECORD ALL CHANNELS); they now shrink to fit.
- Text cut in fixed-size boxes on large windows: EFFECTS slot numbers 10 and 14 showed
  "...", the recorder's marker times and the SNIP counter's PARAMETERS were cut.
- USB recorder / player: renaming a clip (or its folder) that is in the open playlist
  left the playlist pointing at the old file, so the clip showed as missing
  (1970-01-01) and couldn't play.
- SETUP > GENERAL > CONSOLE NAME only took effect after a restart: the running WING
  remote kept announcing the old name, and the MIDI identity (SysEx) kept it in both
  places it can be edited.
- FX: the Vintage Room's early reflections and the Concert reverb's reflection
  clusters now sit at their ER / REFL times as soon as the device is loaded (they
  stayed at the start until the time was changed).
- FILTER: HI CUT goes down to 50 Hz as on the WING (and as the engine allows) on the
  FILTER screen, its graph, the channel strip section and the 4-Channel section; it
  stopped at 200 Hz.
- INPUT: with a negative BALANCE the post-trim meter showed the right side boosted
  instead of turned down.
- EQ: with an analog EQ model, every 6th parameter (SOUL's LM GAIN and HF GAIN...)
  never came onto the knobs: the knob pages held five parameters but were chosen
  in sixes.
- CHANNEL COPY / INIT: CONN now copies the ALT input and the MAIN / ALT switch too,
  SEND copies TAP WIDTH and TAP EQ, and a MACH EQ4 isn't copied onto an aux channel
  (which doesn't offer it).
- Channel strip section: the INPUT encoder's DELAY now has the INPUT screen's range
  (up to 500 ms in every unit, at the current sample rate); it stopped at 150 m,
  500 ft or 24000 samples, so the two controls disagreed.
- INPUT: the TRIM & BALANCE pad's balance handle follows the ST key while the screen
  is open (it stayed disabled, or enabled, until the screen was opened again).
- Multitrack editor: an automatic crossfade stayed on both clips after they were
  moved apart (or one was deleted), so each faded out or in over nothing. Moves,
  nudges, trims, deletes and cuts now take back the fades of crossfades whose
  clips no longer overlap; nudges and trims also crossfade new overlaps, as moves
  did.
- Recorder: after the clock rate changed (SETUP > AUDIO), recording went on into the
  open project at its old rate, so the take was placed and played at the wrong
  length and speed (an octave off after a 48 / 96 kHz change). A project without
  audio now moves to the console's rate (markers and loop keep their times), and
  one with audio refuses to record and says why.
- Recorder screen: pressing VIRTUAL SOUNDCHECK again went back to MAIN but left
  SETUP > AUDIO > GLOBAL INPUT SELECT OVERRIDE on, so channels set to ALT on their
  own were stuck on MAIN. Leaving virtual soundcheck now puts INPUT SELECT back as
  it was.
- SETUP > GENERAL counted one DSP thread less (the workers only) than the event log
  and SETUP > LOG, which count the audio thread too.
- LIBRARY > SNAP / SNIP > EXPORT: the scene or snippet in the file kept its old name
  when it was exported under another one, so the list showed the file as "Scene 1x"
  but IMPORT added "Scene 1". The exported scene or snippet now takes the name it
  was saved under.
- LIBRARY (INT / USB) and the USB RECORDER / PLAYER browser didn't show files copied
  into, deleted from or renamed in their folder outside the app until the screen
  was opened again; they now follow such changes within a second.
- HOME INS screens: the FX PROCESSOR list shows NONE and all 16 slots without
  scrolling (FX 9-16 beside FX 1-8), and FX SPILL is unavailable while the insert
  has no FX processor.
- USB player: "1 SONG" instead of "1 SONGS"; the timeline shows the clip's length
  as soon as it is known (from the file's header before it has played), as the
  player panel does; the meters' CLIP label no longer gets cut in small windows.
- LIBRARY: the SNAP scene NOTES are in the screen's body type (they were tiny);
  SNIP's REC key reads white on red while recording; MONITORS shows SRC LEVEL,
  SRC MIX, TALK LVL and DIR LVL in dB.
- Small windows keep every section of the surface: the minimum window is now
  1344 x 680 (was 1152 x 680, which hid the 4-Channel section and the custom
  controls' upper part), the custom controls stay between the fader sections down
  to 40 px strips, and the top row keeps the WING's arrangement. Dropdown fields
  (VIEW > CONFIG and others) shrink their text instead of cutting it.
- Small windows: panel titles, ROUTING's fields and group selectors, the MONITORS
  tabs, fields and EQ key shrink their text instead of cutting it ("PHO...",
  "ERRO..."); METERS' two-digit strip numbers no longer overlap; SETUP > GENERAL's
  USB HOST SPEED row is no longer cut off; long event log entries wrap onto a
  second line (the tooltip shows the whole entry). MONITORS' unlit MUTE key reads
  on its dark red.
- Small windows, HOME and other screens: two-line keys no longer break a word
  ("PARTIA L UPDATE"); key labels (DELAY, FILTER), format chips, the dynamics
  graphs' level scale ("-2" for -20), the sends' TAP / POST and PAN LINK and the
  VIEW > SENDS values fit their space; the EQ screen's FILTER key keeps its room.
- Screen only on a large display: the SNAP scene's FADE and NOTES rows no longer
  overlap, slider values (CEILING, FADE, MOUSE SPEED) use the screen's type, and
  SETUP > AUDIO's INPUT SELECT captions grow with the screen (in a small window its
  cells are no longer squeezed out by them).
- Automix: a channel set to AUTO X / AUTO Y with its INS 2 ON key off was listed on
  the automix screen, spilled by AUTOMIX FADER SPILL and lit on the surface's AUTO
  LEDs, but not gain-shared. As on the console, INS 2's ON key switches automix in
  and out: only AUTO X / Y with INS 2 ON is a member, everywhere. The selected
  channel stays on its own automix screen while its INS 2 is off, drawn as not a
  member.
- Automix screen: the channel columns now follow other channels joining or leaving
  the groups (from their own INS 2, a scene or a remote app) and channels being added
  or removed, instead of only refreshing when the screen was opened.

## [0.1.0] - beta

The first public release. Sidecar was developed as "Live DAW" before it was renamed;
settings, shows and folders from those builds are picked up automatically.

### Console

- WING topology: 40 input channels, 8 aux, 16 stereo buses, 4 stereo mains, 8
  stereo matrices, 16 DCAs, 8 mute groups and 16 FX slots; more channels and buses
  can be added. Buses 9-16 feed buses 1-8; matrices take a direct input.
- Channel processing in the WING's order: INPUT (trim, balance, polarity, delay),
  FILTER (low / high cut, tilt, max, all-pass), a GATE and a DYNAMICS slot with the
  WING's 32 dynamics models, the 6-band WING EQ (8 bands on buses) and the analog
  EQ models, PRE and POST inserts, automix (groups X / Y), the 24 process orders and
  all bus send taps.
- FX rack with the WING's devices: 26 premium effects, 32 standard effects, 5
  channel strips, and VST3 / AU / LV2 plugins in any slot.
- Solo system (LIVE, STUDIO, SIP), mute groups, DCAs, Sends on Fader, source solo,
  talkback (TALK A / B) and the monitoring paths (phones and speakers, each with
  source, dim, EQ, delay and limiter).
- Sources with gain, polarity, mute, stereo / mid-side pairing and mute-group tags;
  MAIN / ALT input select per channel and globally.

### Surface and screens

- The WING control surface on screen, resizable to any window, with a display-only
  mode (F10) and full screen (F11).
- Three fader sections with the WING's layers, user layers, the shift keys, auto
  width and spill (DCA, sends, FX parameters); fader strips with scribble strips and
  meters as on the console's 3.x firmware.
- The main display's screens: HOME (with the slot screens and model faceplates),
  EFFECTS, METERS, ROUTING (CHANNELS / SOURCES / OUTPUTS), SETUP (GENERAL, AUDIO,
  SURFACE, REMOTE, DAW, PLUGINS, LOG), LIBRARY, the VIEW screens (OVERVIEW, FADERS,
  SENDS, CONFIG, MUTE GROUPS), MONITORS, CC EDIT and UTILITY.
- Channel strip section, monitoring panel, 4-Channel section and custom controls
  (16 layers, USER, 2-TRACK and MULTI transports, mute groups, show control).

### Shows and recording

- Shows with scenes (recall scope, safes, fades, partial load / update) and snippets;
  FX and channel presets; autosave every 30 s and atomic show files with backups.
- Multitrack recorder: up to 64 tracks recorded and played back into the console,
  record all channels, virtual soundcheck, markers, loop, WAV / AIFF / FLAC / MP3
  formats, and a multitrack editor (clips, fades, crossfades, import, export).
- The WING's 2/4-track USB recorder and player with playlists, using a folder on disk.

### Remote control and interoperability

- MIDI remote control (channel control, FX parameters, custom controls, SysEx, scene
  and show control) and DAW remote control (Mackie Control with extenders, HUI) with
  DAW mode on the surface. Virtual MIDI ports on macOS and Linux.
- Experimental WING remote: discovery, OSC and the native protocol on the WING 3.1
  parameter tree (82.8 % of its parameters drive the engine); import of WING
  snapshots and shows, export of WING snapshots.

### Stability

- Realtime engine in Rust: no allocations, locks or file I/O on the audio thread,
  parallel strip processing on a realtime worker pool, unused channels cost nothing.
- Plugin faults (NaN / Inf, exceptions) bypass the plugin on the spot; plugin
  scanning in a separate process; plugins that crash while loading are blacklisted;
  safe mode after a crash; audio device watchdog; output safety clip.
- Audio drivers: ASIO and WASAPI (Windows), CoreAudio (macOS), ALSA and JACK
  (Linux), plus a virtual device for machines without an audio interface.

### Known limitations

See the [README](README.md#known-limitations).
