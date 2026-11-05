# The Universal Mixer: plan and decisions

Sidecar is moving from a WING emulator to an emulator of several consoles. The first
step is the **Universal Mixer**: a complete, feature-rich mixer without the limits of
any one console, on which the emulations (the WING first, then others) are built.

This file records what was decided and the plan, so work can carry on in any session
or environment. Keep it up to date as phases land.

## Decisions (from the project owner, November 2026)

1. **The Universal Mixer has its own new interface**: a clean, modern mixer UI made
   for it (strips, routing matrix, processing editor, scenes), built from reusable
   parts. The WING surface becomes the WING emulation's skin.
2. **The WING keeps working throughout.** The refactor goes step by step; every build
   keeps the WING emulation working, and the WING becomes the first emulation built on
   the Universal core.
3. **Consoles to emulate after the WING:** Behringer X32 / Midas M32, Yamaha (CL / QL /
   TF / DM7), Allen & Heath (SQ / dLive / Avantis) and DiGiCo (SD / Quantum). The
   Universal Mixer must be able to express all of them. Named next (November 2026):
   the **X32**, and the Yamaha **ProMix 01** and **02R**, which are controlled over
   MIDI (control and program changes, SysEx), so a profile brings its own MIDI
   protocol as well as network ones.
4. **Channel and bus formats:** mono and stereo. LCR was planned at first and then
   dropped (November 2026); no LCR, surround or immersive for now. A strip's format
   is a field (`format`: mono, stereo), so another one can be added later without
   changing the show format again.
5. **Phase 1 details** (November 2026):
   - Realtime caps: up to 256 DCAs, 256 mute groups and 32 processing blocks per
     strip (fixed-size tables the audio thread reads without allocating).
   - Shows saved by the Universal core (show format 4) don't open in 0.1.x; Phase 1
     is released as **0.2.0**.
   - The stage order stays Bus → Main → Matrix (buses send to buses in waves); no
     main → main or matrix → matrix sends until an emulation needs them.

## Where the code stood (Sidecar 0.1.6, before Phase 1)

Already general, reused as is:

- The realtime engine core: worker pool, parallel stages, deferred frees, return
  rings, delay compensation (`livedaw-engine`).
- The recorder, the USB recorder / player and the multitrack editor
  (`livedaw-recorder`, `src/daw`, `src/ui/daw`).
- Plugin hosting and the plugin sandbox, audio devices, the web remote, MIDI / DAW
  remote, stability (watchdog, autosave, safe mode).
- Show > Scene > Snippet with scope, safes and fades (`livedaw-session`).
- The processing models and FX devices as a library: each is a parameter table plus
  DSP (`livedaw-dsp/src/wing`, `models_gen.rs`, `FX_MODELS`).
- Channel and bus counts can already grow (`SendBank`, `MAX_BUSES` 16384).

WING-specific today, to generalize:

- Fixed counts in `livedaw-engine/src/params.rs`: `NUM_MAINS` 4, `NUM_MATRICES` 8,
  `NUM_AUX` 8, `NUM_DCAS` 16, `NUM_MUTE_GROUPS` 8, `NUM_FX` 16 (and `WING_CHANNELS`,
  `WING_BUSES` as defaults). (Since Phase 1 steps 1 and 2 every count is
  configured; the `WING_*` constants are the WING's values.)
- A fixed strip layout: `StripParams` is input, filter, gate, EQ, dynamics, two
  inserts, with the WING's 24 process orders and fixed tap points (`TAP_POINTS`,
  `keytap.rs`); aux, bus, main and matrix strips each have their own fixed chain.
- `StripKind` has exactly Channel, Aux, Bus, Main, Matrix, all mono or stereo.
- Source numbering in WING groups (`0x10000` playback, `0x20000` USB player, `0x30000`
  oscillators, `0x40000` strip signals, `0x50000` USR), the WING's monitor and solo
  systems, the WING pan and fader laws.
- The UI is the WING surface and screens (`src/ui`), the remote protocols and files are
  the WING's (`livedaw-wing`).
- The show format stores WING-shaped state; the C ABI (version 23) mirrors the fixed
  `#[repr(C)]` blocks.

Phase 1 (below) generalized the counts, the sends, the strip chain and the show
format; what is still the WING's is listed under Phase 1's *Left for later*.

## Architecture

Three layers:

1. **Universal core** (engine + show model), with no fixed counts:
   - Any number of input channels, aux / stereo inputs, buses, mains, matrices, DCAs
     (VCAs), mute groups, FX slots and monitor paths; mono or stereo strips and buses
     (decision 4).
   - A strip's processing is a **chain of blocks**: any number of filters, EQs,
     dynamics, inserts (FX slot or plugin), delays and utility blocks, in any order,
     each block holding any model of its kind; **taps** (sends, direct outs, meters,
     keys, solo) at any point of the chain.
   - Sends from any strip to any bus or matrix at any tap, bus to bus, mains to
     matrices; a patchbay from any source to any strip input or output; user signals.
   - Metering, pan and fader laws, ballistics and solo behaviour as settings, so an
     emulation can choose the console's.
   - A show format that describes this generally (versioned; 0.1.x shows load into it).
2. **Universal Mixer**: its own interface and feature set over the core: the "without
   limits" mixer.
3. **Emulations** (profiles): each limits and dresses the core: the console's counts,
   strip types and formats, allowed models per block, process orders and taps,
   defaults, laws and ballistics, its surface and screens, its remote protocols and its
   file formats. The WING becomes the first profile, built from today's code.

## Phases

1. **Generalize the engine and show model** (done, November 2026) behind the current
   API: replace the fixed counts with configurable ones, introduce the block chain with
   the WING's fixed layout and process orders expressed as one configuration of it.
   The WING kept working on every commit, its sound byte for byte the same. Steps:
   1. **Counts** (done): `ConsoleConfig` / `Counts` (`livedaw-engine/src/config.rs`)
      with `ConsoleConfig::wing()`; `Engine::configure`, `counts`,
      `set_num_dcas`, `set_num_mute_groups` (and the FX slots' `set_num_fx`). DCA
      and mute group memberships, the engaged mute groups and the sources' mute
      groups are group sets of up to 256 (`groups.rs`: `GroupSet`,
      `AtomicGroupSet`; C++ `ld::GroupBits`, `ld::GroupMask`); `EngineControls`
      holds 256 DCAs. Shows store a set as before (a decimal mask while only the
      first 64 are used, else four hex words). C ABI 25 (`ld_engine_counts`,
      `ld_engine_configure`, ...).
   2. **Sends** (done): a strip's sends to mains and matrices left `StripParams`
      for paged banks on the strip, by the destination's index, like its bus sends
      (`sends::Bank`; `Strip::mains`, `Strip::matrices`, `main_send`,
      `matrix_send`; C++ `strip.mains[i]`, `strip.matrix[i]`), so a console has
      any number of mains and matrices (up to 256 each). The engine creates a
      strip's sends to the console's mains and matrices with the strip. C ABI 26
      (`ld_strip_main_send`, `ld_strip_matrix_send`). Still the WING's: the strip
      sources (`$BUS`/`$MAIN`/`$MTX`), the monitor SOURCE and DIRECT IN lists and
      the talkback destinations cover its 16 buses, 4 mains and 8 matrices. The
      tap reference on a send comes with the taps (step 3).
   3. **Block chain and taps**, in four commits:
      - 3a (done): a strip runs a chain of nodes (`chain.rs`: blocks, tap
        markers, RTA markers) instead of fixed code; the WING's strips are
        templates of it, the 24 process orders the four `Slot` nodes of the
        channel template resolved through `PROCESS_ORDERS` each block (no
        allocation). The WING's output is byte for byte the same (`render`
        example, three set-ups, before and after).
      - 3b (done): extra blocks (filter, EQ, dynamics, insert, delay; up to 32
        per strip) anywhere in a strip's chain (`blocks.rs`): a published
        `ChainLayout` (the template's nodes with the blocks among them)
        replaces the template; `Engine::insert_block`, `move_block`,
        `remove_block`, `clear_blocks`. Blocks are shared between layouts, so
        they keep their settings and state when the chain changes; FX slot
        use and delay compensation walk the chain. Shows store them as
        `<Chain>` (structure with the console, settings recalled by scenes
        with their section). Not yet in the C ABI (with 3d).
      - 3c (done): user taps (`taps.rs`, ids 16-47, up to 32 per strip) placed
        anywhere in a strip's chain (`Engine::insert_tap`, `remove_tap`); any
        send to a bus, main or matrix can take its source's signal at one
        (its `tap` field, C ABI 27), as a pre-fader send. A strip writes a
        user tap into a buffer only while a send reads it (`update_taps`).
        Shows store the taps (`<Chain><T id at>`) and the sends' `tap`.
        Still to come: keys and direct outs at user taps, and their delay
        compensation (user taps are not compensated, like solo and direct
        outs).
      - 3d, moved to Phase 3 (the owner's choice): the WING's own blocks' parameters
        leave `StripParams` (which keeps the strip-level state), with C++ and
        `map.rs` facades: that is what making the WING a profile does anyway.
   4. **Show format 4** (done): `<LiveDAWShow version="4">` with `<Console
      version="4" profile="wing">` and its `<Config>` (the counts), restored first
      (`Engine::set_config`) before the strips, chains and sends. Format 3 shows
      (0.1.x) load as a WING; the legacy converter applies below version 3; a
      newer format is refused (and 0.1.x refuses format 4). Another profile loads
      as a WING with a warning. The FX slots in use stay an app setting.
   5. **A console that isn't a WING** (done) in the tests:
      `livedaw-session/tests/universal.rs` (48 channels, 24 buses, 2 mains, 6
      matrices, 32 DCAs, 12 mute groups; audio through BUS 21, MAIN 2 with an
      extra EQ block and MTX 6 with a user tap, DCA 30 and mute group 12; saved
      and loaded into a fresh engine, which becomes the same console and sounds the
      same) and the C++ `ShowTests` ("A console that isn't a WING", through the
      C++ facades and the C++ show).

   *Left for later* (each where it is needed): keys and direct outs at user taps,
   and their delay compensation; strip sources, the monitor SOURCE / DIRECT IN
   lists and the talkback destinations beyond the WING's buses, mains and
   matrices; pan and fader laws, ballistics and solo behaviour as settings; the
   console profile beyond the `profile="wing"` a show records (Phase 3). The UI
   is the WING surface whatever the configuration.
2. **Universal Mixer UI**: a new interface over the core (strips, chains, routing
   matrix, sends, monitoring, scenes, recorder), its own look and layout. Decisions
   (November 2026):
   - One shell, two skins: `MainComponent` keeps what both share (engine, show,
     plugin host, recorder session and multitrack editor, remotes, event log,
     `UiContext`'s services); the **WING skin** is today's surface unchanged, the
     **Universal skin** the new interface. An app preference (`interface`) picks one.
   - The Universal Mixer drives any configuration, the WING's included, and the
     skins switch live; switching to the WING skin on a console that isn't a WING
     offers to reconfigure it as a WING (the WING skin addresses the WING's ranges).
   - The WING skin stays the default; the Universal Mixer is an opt-in preview until
     Phase 2 is complete.
   - Touch-friendly from the start (large hit targets, no hover-only actions), good
     with mouse and keyboard too; a dark theme first, built on colour tokens so a
     light one can follow.
   - The configuration changes without a reset where it can: adding mains,
     matrices, buses or groups keeps everything else; removing takes only unused
     ones, with a warning.
   - The web remote keeps mirroring the WING display in Phase 2; mirroring the
     Universal Mixer comes later.
   - A part of the interface that comes from one console's emulation is tagged with
     that console's name (WING today: its template blocks, model tables, front
     panels, port groups and lists, fader law); the Universal core's own parts are
     not (the owner, November 2026: "label each component with the device it comes
     from").

   Layout: a top bar (show, scene GO / NEXT, recorder transport, DSP load, clock,
   menu), a navigator (strips by kind, DCAs, filters, banks), a main view with tabs
   (MIXER: strip columns for any count; ROUTING matrix; FX RACK; MONITORING; SCENES;
   RECORDER; SETUP) and an inspector for the selected strip (its chain: blocks and
   taps, drag to reorder, add; a block's editor; its sends with their taps). Reusable
   parts in `src/ui/common`: meter ballistics, the EQ and dynamics graphs, the model
   parameter bindings, the plugin windows, and new widgets (fader, meter, knob,
   button, a parameter control bound through the model tables).

   Steps (each keeps the WING skin working, every check green):
   1. The shell and the WING skin split, no behaviour change; the `interface`
      preference (done): `MainComponent` is the shell, `Skin.h` the interface a
      skin implements, `WingSurface` today's surface moved over unchanged,
      `UniversalSkin` a placeholder (the console it runs, a key back to the WING
      surface); SETUP > SIDECAR > INTERFACE; a console that isn't a WING
      opens in the Universal Mixer.
   2. C ABI 28: a strip's chain (steps, blocks with their params and meters, insert /
      move / remove / clear, taps) and `set_config`; C++ facades and tests (done):
      `livedaw-ffi/src/chain.rs` (`ld_strip_chain` and `LdChainStep`, process order
      slots with the block they run now; `ld_strip_block` and `LdBlock`, a block's
      parameters by kind and its `BlockMeters`; `ld_engine_insert_block`,
      `move_block`, `remove_block`, `clear_chain`; `ld_engine_insert_tap`,
      `move_tap`, `remove_tap` (`Engine::move_tap` is new); `ld_strip_set_block_model`;
      `ld_strip_block_response`, the curves of EQ, filter and dynamics blocks) and
      `ld_engine_set_config`. C++: `Strip::getChain`, `getBlock` (`ChainBlock`),
      `insertBlock` and the rest, `MixerEngine::setConfig`. Tests: the C ABI
      (`chain.rs`) and the C++ facades (`ShowTests`: a filter block and a user tap
      that sound, through a show save and load).
   3. Theme, widgets, the strip column and the MIXER view for any configuration
      (done): `src/ui/universal/` (`Theme`: colour, size and type tokens, dark first,
      a contrast check; `FaderControl`, `MeterControl`, `PanControl`, `ToggleKey`;
      `StripColumn`, `MixerView` with filter keys by kind); `UniversalSkin` is a top
      bar over the MIXER view. Tests: `UniversalUiTests` (the theme's contrast, the
      fader's law and gestures, a column per strip of a 48-channel console and of a
      WING, a column's keys, pan, fader and meter, the skin's picture).
   4. The inspector: chain editor with block editors and taps; the sends panel
      (done): `Inspector` beside the MIXER view for the selected strip. CHAIN:
      `ChainList` (the chain's blocks, user taps and the fader's markers; ADD after
      the selected row, REMOVE, drag to reorder) over `BlockEditor` (`BlockTarget`:
      the strip's own blocks and extra ones alike; model, curve, gain reduction and
      the model's parameters through `ValueControls`). SENDS: `SendsPanel`, a row
      per bus, main and matrix the strip can feed, with ON, level and its source
      (PRE / POST / GRP or a user tap). Tests in `UniversalUiTests`.
   5. ROUTING matrix, FX RACK (generic parameter panels, the WING faceplates where a
      device has one) and MONITORING (done): view keys under the top bar;
      `RoutingView` (a `PatchGrid` for INPUTS: channels and aux against a source
      group, and OUTPUTS: output ports against every bus, main and matrix side and
      the monitor paths), `FxRackView` (slot tiles; DEVICE, PLUGIN, the front panel
      or the parameter list), `MonitorView` (SPEAKERS, PHONES, SOLO); device tags
      (`devices::wing`) on the parts that come from the WING. Still the WING's: the
      source groups' strip signals and the monitor source list cover its 16 buses,
      4 mains and 8 matrices; the monitor paths' processing (EQ, delay, limiter) and
      talkback stay on the WING surface.
   6. SCENES, RECORDER (the recorder page and the multitrack editor) and SETUP
      (audio device, preferences, a configuration editor with the non-destructive
      changes) (done): `ScenesView`, `RecorderView` (the WING display's recorder
      pages, tagged WING), `SetupView` (CONSOLE: the counts with APPLY, through
      `Engine::set_num_strips`, C ABI 29; AUDIO; PREFERENCES). Also, from the owner's
      review of the MIXER view: the faders were too tall for the room a strip's
      processing deserves, so a column has an overview (input, GATE and DYNAMICS gain
      reduction, the EQ curve, output) and the FADERS key hides or shows the faders
      at any moment.
   7. Docs, a user-guide chapter and screenshots (done): the user guide's chapter *The
      Universal Mixer (preview)*, rendered screenshots (`tests/ScreenshotTests.cpp`,
      `docs/images/universal-*.png`), the README, the start-up notice.

   Phase 2 is complete (November 2026). Still on the WING surface: the monitor paths'
   processing, talkback, custom controls, the LIBRARY, snippets and the remotes'
   settings. The interface still says PREVIEW: dropping it, as the decision above
   foresees once Phase 2 is complete, waits for the owner's go.
3. **WING as a profile**: move the WING's limits, defaults, laws, surface, protocols
   and files into a WING profile over the core (including Phase 1's step 3d: the
   WING's blocks out of `StripParams`), and check it against the WING tests and WING
   Edit as today.

   Steps (each behaviour-neutral, every check green, the WING the only profile until
   Phase 4):
   1. **The profile is part of the configuration** (done): `ConsoleConfig::profile`
      (`Profile`: `Wing`, or `Universal` for no console's limits); the engine makes a
      console the Universal when its counts leave its profile's (`Profile::fits`), and
      it stays the Universal (a WING again only by reconfiguring); the show stores and
      restores it (an unknown one is a warning and loads as what fits); C ABI 30
      `ld_engine_profile`, `MixerEngine::getProfile`, which the app asks instead of
      comparing the counts.
   2. **A profile value** (done for the counts, chains and process orders):
      `ProfileSpec` (`livedaw-engine/src/profile.rs`), one place for what a console
      is: its name, a new console's counts, its strip kinds' chain templates and
      pre-fader orders; a strip keeps its console's and runs its template. NEW SHOW
      keeps the console (its configured channels) instead of making a 40 channel
      WING. The factory defaults, colours and names follow with the steps that move
      them (3, 5, 7).
   3. **Laws and ranges from the profile**: the fader and pan laws, and the WING's
      source and destination ranges, behind the profile; the C++ widgets ask the
      engine instead of using the WING's law directly. The laws are done: a profile
      names its `FaderLaw` (breakpoints and steps, so the ProMix 01's 128 position
      fader fits) and its `PanLaw`; strips, scene fades and the UI's faders use the
      console's (C ABI 31 `ld_engine_fader_law`), the WING's protocols keep the
      WING's. The ranges are done on the engine's side: a profile names its port
      groups (`PortGroup`, `ports.rs`: ranges of the engine's numbering, inputs,
      outputs or both), read through `ld_engine_port_groups`; the routing pages show
      them since step 7. Still to
      come with the consoles that need them: a fader law per strip kind (the 02R's
      master runs 0 to -120 dB, its channels +10 to -90).
   4. **Model tables out of `wing`** (done): the model description (`ModelSpec`,
      `ParamSpec`, `Params`...) is console-neutral in `livedaw_dsp::model`; each
      profile names the models its blocks may hold (`ProfileSpec::models`, a
      `ModelTables`: dynamics, aux dynamics, channel and bus EQ, filter tools, FX).
      The WING's tables stay in `wing` as `wing::MODELS`; the strips still read them
      directly until step 5, the interface until step 7.
   5. **The WING's blocks out of `StripParams`** (Phase 1's 3d): call sites move to
      facades first, then the blocks become chain blocks; the sound must stay the same
      sample for sample.
      - 5a, the facades (done): the strip's own filter, gate, EQ, dynamics and inserts
        are private to `StripParams`, reached through `filter()`, `gate()`, `eq()`,
        `dynamics()`, `pre_insert()` and `post_insert()` (Rust) and the same accessors
        on the C++ `StripParams` (`filter()`, ..., `preInsert()`, `postInsert()`; the
        members are `filter_`...). No caller names where the blocks are stored;
        the layout and the sound are unchanged.
      - 5b, the storage (done): the six are chain blocks (`Block`, as the extra
        ones: parameters and DSP state together), kept by `OwnBlocks` and run by the
        template's nodes; `StripParams` points to their parameters (C ABI 32), so
        the C++ accessors read them there. Renders of every block, model and process
        order are byte-identical before and after. Still the WING's: which blocks a
        strip has, and the slots' meters in `StripMeters`; a profile's own set of
        blocks comes with Phase 4's consoles.
   6. **Remotes per profile** (done): the MIDI remote is a port layer (`MidiPorts`:
      devices, hot-plug, the queue) under a protocol per profile, and the profile owns
      its network protocols too. A `ProfileSpec` names its `Remotes` (a `MidiProtocol`,
      or none, and its `NetworkProtocol`s), read through C ABI 33 `ld_engine_remotes`.
      The app makes the console's MIDI protocol (`MidiProtocol::create`, the interface
      it drives whatever the console; `MidiRemote` is the WING's) and remakes it when
      the console's changes; the WING remote's servers run only the network protocols
      the profile names. The WING and the Universal speak the WING's: MIDI, discovery,
      OSC and the native protocol. Next, with their emulations: the X32's OSC and the
      Yamaha consoles' MIDI (the 02R's and ProMix 01's CC maps), and their settings
      pages (step 7).
   7. **The shared UI asks the profile** for its tags, port groups, colours and names.
      - 7a, port groups (done): the routing pages show the console's profile's groups
        (C ABI 34 `ld_profile_port_groups`, `routing::setProfile`): names, prefixes,
        sizes and the tiles' ports, the menus' arrangement still the WING's.
      - 7b, colours (done): a profile names its `Palette`; the interface draws and
        offers the console's colours (`palette::setProfile`, `consoleColour`...,
        `ld_profile_colour_*`), the WING's 18 for the WING and the Universal.
      - 7c, labels (done): a profile names its strips' labels ("CH" of "CH 1"); the
        interface shows the console's (`setShownProfile`, `label`,
        `ld_profile_strip_label`), the WING's for both profiles.
      - Still the WING's, with the WING surface: the group menus' arrangement, the
        ICON/COLOR page's grid of 18, the tags (HOME > TAGS, talk tags) and the WING
        pages' texts; they follow the consoles that differ (Phase 4).
   8. **`livedaw-wing` becomes the WING profile**, checked against the WING tests and
      WING Edit (`tests/wingedit.rs` replays it). Done in its light form (the owner's
      choice, November 2026): `livedaw_wing::profile` is the crate as the WING's
      profile, serving the network protocols its `Remotes` name
      (`Config::limited_to`, applied when the servers start). The WING's description
      stays the engine's `Profile::Wing` spec, which the Universal shares; it moves
      into `livedaw-wing` once a second console (Phase 4) shows what is shared and
      what is the WING's.

   Phase 3 is complete (November 2026).
4. **Second emulation** (the X32 / M32, the closest to the WING), then Yamaha,
   Allen & Heath and DiGiCo, each extending the core where it needs something new.

   The X32, from the owner's reference notes (kept outside the repository: the
   manual, the quick start guide and P.-G. Maillot's *Unofficial X32/M32 OSC Remote
   Protocol*). What it needs that the core doesn't have yet: a third input kind (8 FX
   returns: EQ only, fed by FX 1-4), mono strips made stereo by linking odd/even
   pairs, a mono M/C main beside the stereo LR, matrices fed only by buses and mains,
   fixed blocks (its gate with 5 modes, its compressor before or after the EQ, a
   4-band EQ on inputs and 6 bands on buses, mains and matrices, one insert), its
   level law (four segments, 1024 fader steps, 161 for sends), 16 colours, OSC on
   UDP 10023 and a fixed MIDI map. Steps, each with every check green and the WING
   unchanged:
   1. **The X32 profile value** (done): `Profile::X32` (show id `x32`, C ABI 35
      profile 2, `ld_engine_configure_profile`) with its counts (`Counts::X32`: 32
      channels, 16 aux for the 8 aux in and the 8 FX returns, 16 buses, 2 mains, 6
      matrices, 8 DCAs, 6 mute groups, 8 FX slots, which an X32 keeps exactly), its
      fader law (`FaderLaw::X32`, 1024 steps), its 16 colours (Sidecar's RGB; new strips
      OFF) and no remote protocol yet. It runs the WING's chains and models, pan law,
      labels and port groups until the steps below replace them; the app shows it in
      the Universal Mixer.
   2. **Strips per slot**: a profile's chain template per strip slot, not only per
      kind, so aux 9-16 can be FX returns; mono-pair links (`/config/chlink`...)
      over the core's mono and stereo strips; the M/C main.
      - 2a, slots (done): a profile's `SlotRange`s give strips of a kind from a slot
        on their own chain and label (`template_at`, `label_at`; C ABI 35
        `ld_profile_slot_ranges`, `labelOf`). The X32's aux 1-8 are AUX and 9-16 FX
        returns ("FX 1"), both EQ only (`chain::X32_AUX`).
      - The mono-pair links and the M/C main move to step 4: both are about how
        strips feed the mix.
   3. **The X32's processing models** (`livedaw_dsp::x32`): gate, compressor /
      expander, low cut, the 4- and 6-band EQ, clean-room from the documented
      ranges and laws; the chain's dynamics PRE / POST EQ and insert PRE / POST.
      - 3a (done): a dynamics processor runs its console's model table
        (`WingDynamics::set_table`, the profile's `ModelTables::dynamics`, as the EQ
        already took its table), so a console's own gate and compressor models can
        use the dynamics DSP. The WING's renders are byte-identical.
      - 3b-1 (done): a gate / dynamics slot and an EQ carry their model table
        (`DynSlotParams::table`, `EqBlockParams::table`; C ABI 36 mirrors it as two
        words): `set_model`, `model_index`, `param` and `model_spec` read it, the
        strip runs it, a show saves and loads by it, and a strip's blocks take their
        console's (`StripParams::new_with`, `BlockParams::new_in`).
      - 3b-2 (done): the X32's models (`livedaw_dsp::x32::MODELS`): the gate's five
        modes and the compressor / expander as one dynamics table, with the X32's
        ranges and steps, on the dynamics DSP (the compressor is the WING's with a
        makeup GAIN); the 4-band and 6-band EQ (each band LCut, LShv, PEQ, VEQ, HShv
        or HCut; Q only on the bells), on the EQ DSP. An X32's strips start on its
        GATE, its compressor and its EQ (`ModelTables::gate_default`,
        `comp_default`). The app's model calls describe the shown console's (C ABI
        37 `ld_models_show_profile`, from `setShownProfile`; `models::table` keeps a
        set per profile). Sidecar's choices where the references are silent: the
        expander ratios of EXP2-4, the vintage bell's shape. The defaults (the gate,
        the compressor, the EQ bands, a new strip's colour by kind) are what the
        owner's X32 Compact shows on untouched strips.
      - 3c (done): the X32's chains. Channels (`chain::X32_CHANNEL`): the delay
        before the low cut (the filter block's), the gate, then the compressor PRE or
        POST the EQ and the one insert PRE or POST both, by the process order
        (`chain::X32_ORDERS`: GEDI, the X32's default, GDEI, GIED, GIDE), the fader;
        no TAP EQ or INS 2. Buses, mains and matrices (`X32_OUTPUT`): EQ, compressor,
        insert, all before the fader. An end-to-end test runs an X32 channel to the
        main through its EQ and compressor.
   4. **Sends and mains as the X32's**: channels to buses (tap points per bus pair,
      GRP), LR and M/C assigns (main 1 the stereo LR, main 2 the mono M/C), buses and
      mains to matrices only; odd/even links (`/config/chlink`...) over mono strips.
      - 4a, the mains (done): factory sends follow the console (`BankItem::factory`
        takes its `ProfileSpec`; `buses_to_main`, the WING's subgroups 9-16 or none
        on an X32; `mono_mains`): on an X32 main 1 is the stereo LR and main 2 the
        mono M/C, channels feed LR and not M/C, and no bus feeds LR at first (all
        as on the owner's X32). Its channels show their own name and colour, not
        their source's (`ProfileSpec::source_link`, the WING's LINK CUSTOMIZATION
        TO SOURCE).
      - 4b, sends (done): a send can take any of its strip's own tap points
        (`taps::send_tap`, 0-9 as well as user taps), so an X32's send types reach the
        audio: IN/LC the low cut's tap, <-EQ and EQ-> the taps around the EQ in the
        channel's order, PRE the strip's tap point (an X32's PFL, `tap_point`), POST,
        GRP; the X32 server sets them per bus pair and follows the compressor and
        insert PRE / POST into the process order, the sends keeping their types. An
        X32's input sends start on at -oo, EQ-> (`sends_on`, `send_tap`); its buses
        send to matrices only; its M/C level starts at -oo, its DCAs WH.
      - 4c, links (done, in the X32 server): `/config/chlink`, `auxlink`, `fxlink`,
        `buslink` and `mtxlink` pair odd and even strips; a set on one goes to both
        for what `/config/linkcfg` shares (preamp and delay, EQ, dynamics, fader and
        mute). The engine has no links of its own: the Universal Mixer's interface
        doesn't mirror a linked pair yet, and a linked pair stays two mono strips.
   5. **X32 OSC** (`livedaw-x32`, UDP 10023): `/info`, `/xinfo`, `/status`,
      `/xremote`, get / set with the console's value steps, `/node` and `/`, the
      meters; tests replay the documented traffic (X32-Edit itself isn't run).
      - 5a (done, the server only): `livedaw_x32::X32Server` answers `/info`,
        `/xinfo` and `/status`; gets and sets of the channels, aux in, FX returns,
        buses, matrices, mains and DCAs (config, mix and sends, groups, the channel's
        preamp, delay, gate, dynamics, EQ and insert), floats on the console's steps,
        enums by name or number; `/node` and `/` in real units; `/xremote` (4
        clients, 10 s); `/meters` 0, 1, 2, 6 and 13. Gate, dynamics and EQ are the
        X32 models; what the engine has no place for yet (icons, sources, key
        sources and filters, links, automix) the server keeps and answers back
        (send types and the dynamics and insert positions reach the audio since
        4b). The profile names it (`NetworkProtocol::X32Osc`, bit 8).
      - 5b, in the app (done): C ABI 38 `ld_x32_*`; the network remote
        (`WingRemote`, SETUP > REMOTE's switch and console name) runs it on UDP
        10023 while the console is an X32, and the WING's servers while it is a
        WING or the Universal.
      - 5c, compared with a real X32 (done): `tools/x32/compare.py` asks Sidecar
        (`cargo run -p livedaw-x32 --example serve`) and a console the same `/node`
        questions, read-only. Against the owner's freshly initialised X32 Compact all
        943 groups now match. The comparison brought `/config/mono` and
        `/config/solo`; the X32's factory strip (`ProfileSpec::factory`,
        `factory_name`, `Palette::X32`'s factory colours, `dca_names`): channels' low
        cut at 20 Hz; aux in 7-8 "USB L/R" (YE) and the FX returns (at 0 dB) linked
        pairs panned apart, their sends too; buses 1-12 "MixBus n" (CY), 13-16 "Fx n"
        (MG, at 0 dB: they feed FX 1-4); buses', matrices' and mains' compressors on
        RMS; DCAs "DCA n" at 0 dB; the aux in sends at EQ->, the FX returns' POST, pan
        follow off; the buses' and mains' matrix sends on. Its FX rack's factory
        devices (FX 1-4 fed by buses 13-16, GEQs in 5-8) come with the X32's FX.
   6. **X32 MIDI** (`MidiProtocol::X32`): the fixed CC map (faders, mutes, pans on
      channels 1-3, scenes by program change) and OSC over SysEx.
      - 6a (done): `X32Midi` (`src/engine/X32Midi.*`): CC 0-79 on MIDI channels 1-3
        are the faders (95 = 0 dB on the X32's law), mutes and pans of channels 1-32,
        aux in, FX returns, buses, matrices, LR, M/C and (fader, mute) DCAs 1-8;
        channel 2 CC 80-85 the mute groups; channel 1 program changes recall scenes.
        Both ways: console changes go out, received values don't echo, nothing is
        sent at start. One port, its devices in the app settings (`x32midi.input`,
        `x32midi.output`; the app's virtual port where the platform has them); the
        profile names it (C ABI `RemotesC::midi` 1).
      - 6b (done): the X32 display's SETUP > remote tab: MIDI IN and OUT (turn to a
        device, push USE), and per kind (program changes, faders, mutes, pans) OFF,
        RX, TX or RX + TX, with OSC over SysEx on the second layer, as an X32's
        `/-prefs/remote/ioenable` bits (`x32midi.enable`). Channel 2 program changes
        recall the current scene's snippets (an X32's snippets are the show's,
        Sidecar's a scene's); a scene recalled on the console goes out on channel 1.
        OSC over SysEx (`F0 00 20 32 32 <text> F7`): `livedaw_x32::sysex` runs the
        text as the `/` command does; an address alone is a query, answered by
        SysEx with its `/node` line (Sidecar's reading). C ABI 42 (`ld_x32_sysex`).
        Not done: cues by program change (channels 3-6; Sidecar has no cue list),
        the user-assign controls' MIDI.
   7. **Scenes and the interface**: `.scn` import and export (done): `livedaw-x32::scn`
      writes the console as the server's `/node` answers it (`/config` first, the
      header `#2.7# "name" "note" %000000000 1`) and reads a file as its `/` command
      sets values (a linked pair's partner too); what Sidecar doesn't emulate (the FX
      slots until 9b, outputs, head amps...) is left out of a written file and skipped in a
      read one. C ABI 39 (`ld_x32_scene_write`, `ld_x32_scene_read`); the X32
      display's LIBRARY and the Universal Mixer's SCENES have IMPORT .scn (a new
      scene of the show) and EXPORT .scn (`x32::SceneFiles`). A factory console's
      file matches a freshly initialised X32 Compact's `/node` answers line for line
      (`tests/scn.rs`, with `LIVEDAW_X32_SNAPSHOT` naming the dump; that test found
      the sends to buses 9-16 start POST, not EQ->). The Universal Mixer shows the
      X32 (its strip kinds, blocks and colours from the profile).
   8. **An X32 skin** (the owner's choice, November 2026: "make it look good"): a
      surface laid out as an X32, the way the WING surface is the WING's skin, drawn
      in Sidecar's own style (no Behringer artwork). From the X32's manual: 16 input
      faders in four layers (Ch 1-16, Ch 17-32, Aux / FX returns, bus masters), 8
      group faders in four (DCAs, buses 1-8, buses 9-16, matrices and M/C), the main
      LR fader; a scribble strip, meter, mute, solo and select on every fader; the
      channel strip section for the selected strip (config / preamp, gate, dynamics,
      EQ, bus sends, each with its VIEW key); the colour display with its page keys
      (HOME, METERS, ROUTING, SETUP, LIBRARY, EFFECTS, MUTE GRP, UTILITY); sends on
      fader, mute groups, the assignable controls, monitoring and talkback.
      - 8a and 8b (done): `InterfaceKind::X32`, `X32Surface` (`src/ui/X32Surface.*`, a
        `Skin`): an X32 console opens on it (MainComponent), the Universal Mixer's
        switch key leads back to it (X32 SURFACE), the settings keep it (`x32`). The
        16 input faders and their layers (CH 1-16, CH 17-32, AUX / FX RTN, BUS
        MASTER), the 8 group faders and theirs (DCA 1-8, BUS 1-8, BUS 9-16, MTX 1-6 /
        M/C), the main LR; on each fader its scribble strip (`x32::FaderStrip`: the
        X32's colours, the inverted ones as coloured text on black; LR and M/C), a
        meter, SELECT, SOLO and MUTE. A screenshot test (`x32-surface.png`) checks
        the layers.
      - 8c, the channel strip section (done): `x32::ChannelSection`
        (`src/ui/X32ChannelSection.*`) for the selected strip: CONFIG / PREAMP (GAIN,
        LOW CUT and its key, polarity), GATE (THRESHOLD, its key, the gain
        reduction), DYNAMICS (THRESHOLD, RATIO, COMP, the gain reduction), EQUALIZER
        (the band keys, 4 or 6, the band's TYPE, FREQ, GAIN and Q, EQ), BUS SENDS (four
        at a time, the bank keys), MAIN BUS (PAN, LR, M/C and its level); each
        section's VIEW key opens its tab of the display's HOME screen. The knobs are the WING
        screens' `DisplayKnob`s, bound through the console's models (an X32's gate,
        compressor and EQ).
      - 8d, the display (done): `x32::Display` (`src/ui/X32Display.*`), the X32's
        screen in Sidecar's style, as the WING surface draws the WING's: the status
        bar (the selected strip, the scene and the next, the sample rate, the clock),
        the eight screen keys (HOME, METERS, ROUTING, SETUP, LIBRARY, EFFECTS, MUTE
        GRP, UTILITY), PAGE < > for the tabs, LAYER for HOME's second layers, and six
        push-encoders mapped per tab as the X32's manual maps them (HOME: home,
        config, gate, dyn, eq, sends, main). Its HOME tab draws the signal path in the
        strip's process order; gate, dyn and eq their graphs; sends the 16 sends;
        LIBRARY recalls scenes; MUTE GRP its six keys. Unlike an X32's, the screen
        takes touch: a tap on a tab, a drag on the encoder row turns, a tap pushes,
        the graphs drag. Deep editing (patching, FX, show files) stays on the
        Universal Mixer for now. The X32's factory delays are times (0.3 ms).
      - 8e (done): the X32 COMPACT as a model of the surface (`X32Surface::Model`, the
        app setting `ui.x32Model`, chosen by the display's SETUP encoder): 8 input
        faders in eight layers (CH 1-8 .. CH 25-32, AUX IN / USB, FX RTN, BUS 1-8, BUS
        9-16), the group faders and the main as the X32's. SETUP > INTERFACE offers the
        X32 on both skins (and makes the console a fresh X32, asking first). The
        display's polish (rows and values fitted, scenes scrolled to the selected one,
        no tabs it doesn't draw), screenshots (`x32-surface.png`, `x32-display-home.png`,
        `x32-compact.png`), the user guide's chapter, the README and the start-up
        notice.
   9. **The X32's effects** (the owner's choice, November 2026: the X32's own models
      first, not stand-ins from the WING's catalogue):
      - 9a, the rack per console (done): the FX rack runs its console's FX table
        (`ModelTables::fx`), as the strips' blocks run theirs (`FxDevice` with a
        table; the slot rules from the profile: on an X32, slots 1-4 take any model,
        5-8 the insert models only). `Engine::fx_models`, `FxParams::*_in`,
        `FxDevice::new_in`, `ModelTables::fx_premium_slots` (the WING's 8, the X32's
        4); show files read and write a slot's device in the console's table. A slot
        holds 64 parameters (C ABI 40), for the X32's dual GEQ.
        The devices are measured against the owner's X32 Compact (from 9d): the
        computer cabled to it over USB plays test signals through its effects and
        records them.
      - 9b, the catalogue (done): the 61 models (`livedaw_dsp::x32::FX_MODELS`,
        tokens `X32HALL`...), generated by `tools/x32/gen_fx_models.py` from
        `tools/x32/fx_catalogue.json` (the OSC document's parameter maps, pp. 85-115,
        transcribed and checked against each model's type signature; the document's
        typos noted and corrected there) and the factory values of the owner's X32
        Compact. `/fx/1..8` on the X32 remote and in scene files: `type` (FX 1-4 the
        61, FX 5-8 the 34 insert models, their own numbering), `source/l|r` (9c),
        `par/01..64` typed by the slot's model (`Console::ty`) and written as the
        console writes them ("97", "10k4", "1.94", "0.0"). A factory console's scene
        file matches the owner's console line for line, its FX rack included. Where
        the console contradicts the document (VRM's SIZE and ER LEFT / RIGHT hold 24,
        28, 34; the document says 0-10) the range is widened to 0-100, to be checked
        when the FX tests run on the console. The other 56 models' defaults are
        Sidecar's choice until they are read from a console.
      - 9c, the factory rack (done): FX 1-4 Vintage Room, Hall, Stereo Delay, Stereo
        Chorus fed by buses 13-16 and returning on the FX returns; FX 5-7 Dual GEQ, FX 8
        Stereo GEQ (as a freshly initialised X32 Compact has them;
        `ProfileSpec::fx_factory`, `fx_send_buses`). A slot with a SOURCE
        (`FxParams::source_left` / `source_right`, strip signals; -1 = INS) is a send
        effect: before the channels run, the engine processes its sources' last block
        (as a channel patched to a bus hears it) into the FX return source group
        (`sources::FX_RETURN_SOURCE_BASE + 2 * slot + side`), which the X32's FX return
        channels are patched to. Inserted on a strip, a send effect passes the signal
        through. `/fx/1..4/source/l|r` sets it (INS on either side makes an insert).
      - 9d, the factory four (done): Hall, Vintage Room and Stereo Chorus run on the
        WING's devices of the same design (`wing::fx::x32_voices`: the X32 model's
        values mapped onto the WING device's controls over both ranges; Hall's LEVEL
        an output gain, Vintage Room's SIZE at half); Stereo Delay is the WING's
        (`delay::stereo`, X32DLY) with FACTOR L / R, FEED LEFT / RIGHT and MIX. Tests
        hold each to the WING device's output sample for sample. The mappings where
        the ranges differ are to be measured on the owner's X32.
      - 9e, the shared designs (part 1 done): Ambiance, Room, Chamber, Plate (the
        WING's Blue Plate design), Rich Plate (the WING's Plate), Vintage Reverb,
        Gated, Reverse, Flanger, Phaser, Dimensional Chorus, Mood Filter, Rotary,
        Tremolo / Panner and Pitch Shifter run on the WING's devices, mapped over the
        WING device's ranges (`x32_voices` lists each mapping where the units
        differ). Part 2 (done): the dual effects (Sub Octaver, Dual Pitch: two WING
        devices, one per side, `x32_voices::Dual`), the combinations (Delay /
        Chamber, Chorus / Chamber, Flanger / Chamber, Delay / Chorus, Delay / Flanger:
        two WING devices side by side, BALANCE and MIX; `x32_combos`), Modulation
        Delay (delay into chorus, the reverb after or beside them) and the 3- and
        4-tap delays (`delay::x32taps`). Their structure is Sidecar's reading of the
        controls, to be measured on the owner's X32.
      - 9f, the insert models (part 1 done): the graphic EQs (GEQ the WING GEQ's
        STD type, TruEQ its TRU), de-essers, Precision Limiter, Combinators,
        Enhancers, Exciters and Guitar Amps on the WING's FX devices
        (`wing::fx::x32_inserts`; the dual models one device per side). Part 2
        (done): the compressors (Fair, Leisure, Ultimo) and Wave Designer on the
        WING's channel dynamics models of the same designs (F670, LA, 76LA, WAVE),
        run by its dynamics engine as an FX device (`wing::fx::x32_dynamics`:
        stereo linked, dual one unit per side, FAC1M on mid and side).
      - 9g, the rest (done): Program EQ and Midrange EQ on the WING's PULSAR EQ
        model (its EQ1 and EQ5 halves, run by its EQ engine: `wing::fx::x32_eqs`);
        Tube Stage, Stereo Imager, Edison EX1 and Sound Maxer as devices of
        Sidecar's own from their controls (`wing::fx::x32_misc`). Every X32 model
        now has its algorithm. Still to do, with the FX testing setup on the
        owner's X32: measure each against the console (decay, taps, curves, gain
        reduction) and correct the mappings marked as Sidecar's reading.
   10. **The X32's routing** (its patching, so it no longer needs the Universal
       Mixer): the audio interface's ports numbered as the X32 numbers its own
       (OSC document p.22, the `/headamp` and `/-ha` indexes): inputs 1-32 the
       local inputs (AN), 33-80 AES50 A, 81-128 AES50 B, 129-160 the card, 161-166
       aux in; the outputs likewise (local 1-16, aux 1-6, P16, AES50 A and B, card).
      - 10a, the inputs (done): `/config/routing/IN` and `PLAY` (the blocks of
        eight that feed inputs 1-32 and aux 1-6; `routswitch` picks which) and the
        channels' and aux ins' `config/source` bound to their inputs through them
        (`map::ports`, `in_block_port`, `aux_port`; a block's change re-patches the
        strips on their sources); `/headamp/NNN` gain and phantom the sources'
        (interface input NNN); `/-ha/NN/index` the input a channel takes. The output
        blocks are 10b's. The factory routing is a full-size X32's (the owner's
        choice, November 2026: inputs 1-32 local; a Compact takes 17-32 from AES50
        A); `x32_factory` patches aux in 1-6 to 161-166, 7-8 to the USB player; a
        console's factory now runs after the WING's defaults (an X32's aux in 2
        isn't the WING's USB player). The routing pages still show the WING's port
        groups (10c). UIN (the user in routing) and AUX's AN1-2 ... are Sidecar's
        reading: the first two, four or six aux in from that group.
      - 10b, the outputs (done): the interface's outputs numbered as the X32's
        (`livedaw_engine::profile::x32_ports`: local 1-16, aux out 1-6, AES50 A
        33-80, AES50 B 81-128, card 129-160, P16 161-176). `/outputs/main|aux|p16`
        (source and tap; invert, delay and P16's iQ kept), `aes|rec` (kept), and the
        output blocks `/config/routing/OUT`, `AES50A|B`, `CARD` (`Bind::Outputs`):
        a change sets the engine's output patch for all of them (`apply_outputs`,
        so the X32's routing owns those outputs). An output signal is the
        engine's: mains, buses and matrices as their strip signals (the output
        patch now plays a bus, main or matrix on any number of outputs, after the
        mix, in the same block), a bus before its fader and the direct outs as
        `DirectSource`s (PRE for every tap but POST: Sidecar's reading); the
        monitor and talkback signals and the user routing (UOUT / UIN) aren't
        Sidecar's and play nothing. Factory (`ProfileSpec::factory_outputs`,
        `X32_FACTORY_OUTPUTS`): outputs 1-14 MixBus 1-14, 15-16 Main L / R (the
        owner's full-size X32; the Compact's are on 7-8), on the local outputs and
        AES50 A and B; the card records the local inputs. The scene file test
        skips the lines where the Compact's factory differs (`FULL_SIZE`).
      - 10c, the display's ROUTING screen: its home tab (done) sets the IN / PLAY
        blocks and `routswitch` through the X32 map's text commands
        (`WingRemote::runX32Text`, as OSC over SysEx does) and shows the interface
        inputs each block takes; the channel strip's GAIN turns the head amp of
        the strip's input (interface inputs 1-128), else the trim. Its output tabs
        (done): out 1-16, aux out, P16 out (encoder 1 picks the output; source, tap,
        invert, out 1-16's delay; out 1-16's second layer the `OUT` blocks), card
        out, AES50-A, AES50-B (their blocks). The Universal Mixer's routing pages
        (done) show the X32's port groups (`ports::X32_PORT_GROUPS`, in the WING's
        places so the menus keep their layout: CARD in AES50 C's, P16 in ST
        CONNECT's); the app finds an interface port's group from the profile's
        list (`routing::hardwareGroupOf`, `inputPortName`, `outputPorts`) instead
        of the WING's blocks of 48.
   11. **The X32 display's EFFECTS screen** (done): home (encoder 1 the slot, 2 its
       model from the slot's list, `Display::fxModelsFor`: FX 5-8 not the
       premium ones; its key EDIT; 3-4 FX 1-4's SOURCE L / R through the X32
       map's text commands), fx1-fx8 (the slot's parameters, six per layer,
       bound to the engine's `FxParams` by the model's table, every value listed
       in two columns).
   12. **The rest of the X32's surface**, from step 8's list:
      - 12a, sends on fader (done): `X32Surface::setSendsOnFader`, its key in the
        top bar. A bus selected: the input faders bind to each input strip's send
        to it, and on the MTX layer the bus's sends to the matrices; a channel or
        aux selected: the BUS 1-8 / 9-16 layers bind to its sends. The faders'
        SOLO, MUTE and SELECT stay the strips' (`FaderStrip::setSend`, the
        scribble strip's amber band); the sends follow the selection.
      - 12b, monitoring and talkback (done): `x32::MonitorSection`
        (`src/ui/X32MonitorSection.*`), at the right of the display (as on an X32):
        MONITOR, PHONES, DIM, MONO, MUTE, TALK and TALK A / B (latching) on the
        engine's monitor and talk controls. Where talkback goes and which input
        it takes are set on the Universal Mixer for now.
      - 12c, the assign section (done): `/config/userctrl/A|B|C` (colour, enc
        1-4, btn 5-12: the function codes of the OSC document pp.116-121, kept by
        the map, factory "-"; the colours are the owner's console's) and
        `x32::AssignSection` (`src/ui/X32AssignSection.*`, right of the main
        fader): encoders F, P, S, X, D and M (CC, note, program change through
        `X32Midi::sendUser`); buttons O (strips, DCAs, mute groups), I, X (an FX
        parameter between its ends), M / m (MIDI push, toggle), S0 (scene), S9
        (PREV, NEXT, GO), P (a page of the display). Remote, X-Live!, the USB
        recorder and cues do nothing. Buttons are numbered 5-12 as on a full-size
        X32 (a Compact's 1-8 are the same codes). No editor for the codes on the
        display yet: they're set from X32 remote apps or scene files.
      - 12d, show files and UTILITY (done): LIBRARY's show tab calls the app's
        show actions (`UiContext::newShow`, `openShow`, `saveShow`, and
        `getShowFile` for its rows); UTILITY's copy tab COPYs the selected strip
        and PASTEs its chosen sections onto the selected one with the WING's
        CHANNEL COPY (`copyStripSections`, `HomeScreens.h`). Not done: INIT (the
        WING's defaults would give an X32 strip WING models) and the X32's channel
        presets (`.chn` library files).

5. **Third emulation: the Yamaha ProMix 01** (1994), the first console controlled
   only over MIDI. From the owner's reference notes (kept outside the repository:
   Yamaha's *ProMix 01 User's Guide*, pages cited as printed, `Add-n` for its
   Additions, and its Control Change table as a CSV). A small digital mixer:
   16 mono input channels (odd / even pairs can be made stereo), a stereo input
   (ST IN), two stereo effect returns (RTN1, RTN2) from its two internal
   effects, four sends (SEND1-2 feed the effects and have no masters; SEND3-4
   are outputs with a fader, ON and PRE / POST, usable as a stereo pair), one
   stereo output (ST OUT); no buses besides, no matrices, DCAs or mute groups.
   Every input, the returns and ST OUT have a 3-band fully parametric EQ (1/6
   octave frequency steps, 1 dB gain steps, Q in octaves or shelf on the outer
   bands, p.21); three stereo dynamics processors (compressor, gate or ducker)
   are a pool patched into a channel, SEND3, SEND4 or ST OUT (pp.68-78); four
   fader groups (linked faders, p.56); 50 scene memories and the read-only scene
   00 (p.62). Faders have 128 steps from +6 dB to -66 dB and -oo, pans 33
   positions (pp.25-26, p.105). Its MIDI (pp.80-87, Add-16 to Add-29): scenes by
   program change through an assignable table; 531 parameters on control changes
   in 16 banks of 96, in Channel mode (bank = MIDI channel offset) or Register
   mode (NRPN LSB selects the bank), values scaled from the internal range onto
   0-127 (the effects' parameters 14-bit); Yamaha bulk dumps and requests (scenes,
   the Edit Buffer, the PC and CC tables, setup), Key Remote. Not in the guide,
   so Sidecar's reading until measured on a console: the fader's position-to-dB
   table, the byte map of a scene dump, the effects' algorithms. Steps, each
   with every check green and the WING and X32 unchanged:
   1. **The ProMix 01 profile value** (done): `Profile::ProMix01` (show id `promix01`,
      C ABI profile 3) with its counts (`Counts::PROMIX01`: 16 channels; 3 aux,
      ST IN and the two returns; 4 buses, SEND1-4; 1 main, ST OUT; no matrices,
      DCAs or mute groups; 2 FX slots), its fader law (`FaderLaw::PROMIX01`, 128
      steps), its labels and slot ranges (aux 1 "ST IN", 2-3 "RTN"; buses "SEND"),
      its port groups (the 16 inputs, ST IN, 2TR IN; ST OUT, SEND3-4, REC OUT,
      MONITOR, DIGITAL), a palette (the console has no strip colours: one per
      kind), its factory strip (SEND1-2 POST, SEND3-4 PRE, the returns fed by the
      effects); no remote yet. Consoles with no matrices, DCAs or mute groups
      carry audio (`tests/engine.rs`). The engine keeps the WING's 16 FX slots at
      least (as on an X32); the console addresses two. Its port groups sit in the
      WING's places (`PROMIX01_PORT_GROUPS`); a new console plays ST OUT on outputs
      1-2, 5-6 and 9-10, SEND3-4 on 3-4. The fader's steps between +6 and -66 dB are
      Sidecar's reading of the panel's scale (0 dB on step 100, measured on the User's Guide's drawing;
      the owner's photos of a console's panel print the scale in the same places, within a step or two).
   2. **Its processing** (done): the 3-band EQ (`livedaw_dsp::promix`, token
      `PMEQ`: LOW 32 Hz-1 kHz, MID 32 Hz-18 kHz, HIGH 1-18 kHz in 1/6 octave steps
      named as the guide's presets name them; gain +-15 dB in 1 dB steps; Q 1/6 to
      3 octaves by the guide's table of decimal Qs, LOW and HIGH also SHELF;
      scene 00's 80 Hz SHELF, 2.0 kHz 3/2 oct, 10 kHz SHELF) on inputs, ST IN, the
      returns and ST OUT; the dynamics models (`PMCOMP`: threshold -40..+18 dB,
      ratio, attack 0-120 ms, release 0.1-6.0 s, OUT GAIN +-18 dB; `PMGATE` and
      `PMDUCK`: threshold -50..+10 dB, attack, hold 3-341 ms, decay 85 ms-10.9 s,
      range -80..0 dB; pp.69-71, their defaults the presets TOTAL COMP, GATE FAST
      and DUCKING of p.78) on the dynamics DSP. Its chains: channels phase, EQ,
      the dynamics (post-EQ, pre-fader: p.72), the fader; ST IN and the returns
      EQ and fader; the sends and ST OUT (ST OUT with its EQ first) the dynamics
      before or after the fader by the strip's process order
      (`chain::PROMIX01_OUTPUT_ORDERS`, POST at first: p.74; a profile's outputs
      may now have several orders, `ProfileSpec::output_orders`); no gate, filter,
      delay or insert. Sidecar's reading where the guide is silent: the
      compressor's ratio steps (1:1 ... 20:1, oo as 100:1, as later Yamaha
      consoles list them), the gate's HOLD and DECAY as 128 even steps (the
      guide's preset values fall on them), the shelves' slope. The dynamics
      block is off on every strip: the three processors (step 3) turn on the ones
      they are patched to.
   3. **The console's own state** (done; `livedaw-promix`, as `livedaw-x32`'s map
      is the X32's): `State` holds what the engine has no place for: the three
      dynamics processors (ON, PATCH, KEY IN, IN, the program and its values), the
      pairs, the fader groups A-D, GANG / INDIVIDUAL, SEND3-4 stereo, the stereo
      strips' dual pans, the oscillator and the CUE mode, written and read as text
      (`to_text`, `from_text`) for the show (step 6). `Console` is the console over
      the engine, doing what its panel does: a pair links PHASE, EQ, ON, fader,
      CUE and SEND1-4 (pp.58-59; ST 1->2, 2->1 and RESET copy or reset, pairing
      leaves groups and cancels a processor's patch); a group's faders move by as
      many steps from the panel, a control change moves its channel alone
      (Add-17); a processor is patched into a channel (with its partner: IN
      BOTH, ODD or EVEN), SEND3, SEND4 or ST OUT, writing its type, values and key
      onto that strip's dynamics block and turning the others off (a patch in use
      is refused); SEND1-4 PRE / POST for all the inputs; SEND3-4 stereo makes
      SEND3's bus stereo and plays its right side on SEND4's output, the inputs'
      SEND3-4 pans their sends' pans, pairs hard left and right; the output patch
      points the strips' process orders. The dynamics library's 10 presets
      (p.78, `library::DYN_PRESETS`) and 10 user programs. `params`: the 531
      parameters (`param`, `steps`, `get`, `set`, `name`, `has_cc`), each an
      internal value 0..N-1 as the CC table numbers them; the Execute
      parameters act, the flags change only the flag. Sidecar's reading: a key
      the engine can't take on that strip (a channel keyed by SEND3, ST-L / ST-R
      apart) keys itself or both sides; the oscillator is kept, not heard yet.
      The effects' parameters (404-445) come with step 4.
   4. **Its effects** (done): the two slots fed by SEND1-2 and returning on
      RTN1-2, at first REVERB HALL 1 and DELAY L-C-R (Sidecar's choice: the guide
      doesn't say what scene 00 holds). The 15 effect types of the 30 preset
      programs (`livedaw_dsp::promix::fx`, `PMREV`...: reverb; chorus and flange
      into reverb; L-C-R, mono, two-stage and karaoke delays and their chorus
      combinations; stereo pitch change; chorus, symphonic, flange, phasing,
      tremolo, auto pan) with the guide's parameters, ranges and steps
      (pp.37-44), each type's defaults its first program. Their algorithms are
      Sidecar's, on the WING's ROOM, CHORUS, FLANGER, PHASER, PITCH and PANNER and a
      delay line of Sidecar's own (taps, feedback tap, HIGH RATIO a low-pass in
      the feedback); the combinations run in series, REV DEPTH mixing the reverb
      in. Unmodelled: the chorus' AM DEPTH, the pitch changers' feedback, auto
      pan's F/R DEPTH and DIRECTION; the WING devices' ranges limit some values
      (a chorus faster than 5 Hz). The libraries (`livedaw_promix::library`): the
      30 effects presets and 10 user programs, the dynamics' 10 and 10, the EQ
      library's 30 presets (pp.23-24: each printed column lists its 15 G / F / Q
      triples per page in program order) and 20 user programs; recalling one
      sets an effect slot (`Internal Effect n Type`), a processor or a strip's
      EQ; the effects' parameters (404-445) read and set a slot's values by
      their steps. The guide's HUM REDUCE 60Hz preset puts LOW at 60 Hz, so the
      EQ's 1/6 octave step between 56 and 70 Hz is 60 Hz.
   5. **ProMix MIDI** (done; `MidiProtocol::ProMix01`, C ABI 44): the protocol in
      Rust (`livedaw_promix::midi`), the app's `ProMixMidi` a thin port over it
      (`src/engine/ProMixMidi.*`; the MixerEngine's `ProMix` holds the state,
      `ld_promix_*`). Control changes: the 531 parameters on the default map
      (`CcMap`: p / 96, p % 96; CUE ON and the CUE mode none; the guide's CC 23-27
      for 321-325 taken as its misprint), in Channel mode (bank = MIDI channel
      offset from the Tx / Rx channel) or Register mode (CC 98 the bank, one
      channel); values scaled as Add-17 says (pan: `(x - 15) / 3`, the middle on
      64; the effects' parameters in 14 bits over their high- and low-byte
      parameters); a received value isn't echoed, a group isn't followed
      (Add-17), the Executes act and the flags change alone; the console's
      changes go out (a poll every 50 ms; nothing at start). Program changes
      through the table (`PcTable`: 1-50 the scenes, 128 scene 00) recall the
      show's scenes 1-50 (scene 00 with step 6); a scene recalled sends its
      program change. MIDI SETUP's Tx / Rx channels and TX, RX, OMNI and ECHO per
      kind (`promixmidi.setup` in the app settings; the page comes with the
      skin). SysEx: Yamaha's bulk dumps and requests (`F0 43 0n 7E`, `F0 43 2n
      7E`, header `LM  8A41`, data as hex digits, the two's complement
      checksum) of the Edit Buffer (memory 127), the program change table, the
      control change table (576 bank / controller pairs), the setup and the
      effects, dynamics and EQ user programs, the All request, Memory Control
      Change Out's (`Y`); Key Remote presses ON (CH1-16, ST IN, ST OUT), the
      other buttons go to the app. The bytes inside the dumps are Sidecar's
      layout (`midi::scene_bytes`: the Edit Buffer in the guide's 408 bytes,
      the switches as bits, the groups two to a byte), since the guide gives
      only the formats: a dump goes back into Sidecar, not into a ProMix 01.
      Not done: the scene memories' dumps (step 6), Local OFF (the panel's,
      step 7), the bulk interval, the Error report, the Backup Memory.
   6. **Scene memories** (done): scenes 1-50 are the show's scenes 1-50 (program
      changes through the table recall them; a recall on the console sends its
      program change), and they keep the console's own state: the engine holds a
      console's state beyond its strips as text (`Engine::console_text`, with a
      version; a reset clears it), which shows and scenes store
      (`<ConsoleState profile="promix01" state="...">`, the lines `;`-separated,
      with the FADER, SENDS, GROUPS and DYNAMICS sections; only for the console
      it was saved from) and the ProMix handle reads again whenever it changes
      and writes after its own changes (`ld_promix_*`), so a scene brings back
      its pairs, groups, processors and SEND3-4 mode. Scene 00, the initial data
      (p.62), is the mix of a new ProMix 01 (`midi::initial_bytes`, its scene
      bytes), recalled by program change 128 or `ld_promix_recall_initial`
      without rebuilding the console. Not done: scene memories as bulk dumps
      (a request for scene n, a dump into it: they need the scene's mix without
      recalling it), the program and user libraries in the show.
   7. **A ProMix skin** (done): `ProMixSurface` (`src/ui/ProMixSurface.*`, a
      `Skin`, `InterfaceKind::ProMix`, the setting `promix`; SETUP > INTERFACE
      offers PROMIX 01 on every skin and makes the console a fresh ProMix 01,
      asking first; the Universal Mixer's PROMIX SURFACE key comes back), the
      ProMix's panel in Sidecar's style: 16 channel faders, ST IN, RTN / SEND (its
      SEL steps through RTN1, RTN2, SEND3, SEND4) and ST OUT with SEL and ON, the
      faders of the console's 128 steps and scale moving ProMix parameters (so
      pairs and groups follow), the inputs' send levels while a SEND function is
      shown (p.25); the LCD (`promix::Display`) with METER, PAN / PHASE, COMP (its
      three processors, EDIT), CUE, SEND1-4 (program lists, EDIT, PRE / POST,
      SEND3-4 pans), EQ LOW / MID / HIGH (the curve and nine values), LIBRARY, GROUP
      (SEL adds and removes), PAIR, UTILITY and MIDI (MIDI SETUP); the cursor keys,
      ENTER and the PARAMETER wheel, touch on the screen; SCENE MEMORY (the
      number, STORE, RECALL, INC, DEC; 00 the initial data); the stereo LED meter.
      `ld_promix_param_text`, `_label`, `ld_promix_value_text`,
      `ld_promix_program_name`, `ld_promix_recall_eq` give the LCD its texts. A
      screenshot test (`promix-surface.png`, `promix-display-comp.png`). Not
      done: GAIN and PAD (analog on the console), Local OFF, the oscillator's
      sound, METER's peak hold, the EQ LIBRARY's STORE and names (the user
      programs are recalled, not edited, on the surface).

   Phase 5 is complete (November 2026) as far as the User's Guide goes: what it
   doesn't give (the fader table, the dumps' byte layout, the effects'
   algorithms, the compressor's ratio steps) is Sidecar's reading, to be checked on
   a console.

   The owner's photos of a ProMix 01 (November 2026, kept with the reference
   notes) corrected the surface: the LCD laid out as the console's (FUNCTION,
   MEMORY with the scene's title, its number and EDIT, and SEL CH down the left;
   EQ's curve over its G / F / Q table; COMP's processor number, ON/OFF, PATCH and
   KEYIN as lines over three programs and STORE, RECALL, EDIT, with GR and OUT
   meters; PAN's knobs, a pair's one knob over both, and the selected channel's
   pan under them; METER's scale and PEAK HOLD), the LCD's yellow-green, red SEL
   and ON LEDs, and the fader caps (ST IN light blue, RTN / SEND salmon, ST OUT dark
   grey). The LCD shows the dynamics presets in capitals ("TOTAL COMP"), EQ gains
   as "0dB" and "-6dB", 10 kHz as "10kHz", and scene 00's title as "InitData". The
   fader scale printed on the panel matches the one measured on the guide's drawing.

6. **Fourth emulation: the Yamaha 02R** (1995), the ProMix 01's big brother, also
   controlled over MIDI. From the owner's reference notes (kept outside the
   repository: Yamaha's *02R User's Guide* (MAN) and its Version 2 guide (V2),
   pages cited as printed, the V2 Control Change maps as CSVs). Sidecar emulates
   Version 2, a superset of the first: it receives control changes (V2 p.164) and
   has 96 scenes (V2 p.128). A digital recording console:
   - **Inputs:** 24 inputs: MIC/LINE 1-16 mono, and LINE 17-24 as four stereo
     channels (17/18 to 23/24) with one fader each.
   - **Tape returns:** 16 (TAPE 1-16), full channels on encoders, or on the
     faders with FLIP.
   - **Effect returns:** two stereo (EFF1, EFF2) from its two internal effects.
   - **Buses:** BUS 1-8.
   - **Aux sends:** AUX 1-8, each pre or post fader per channel. AUX 7 feeds Effect
     1 and AUX 8 Effect 2; EFF1 returns only to AUX 8 and EFF2 only to AUX 7.
   - **Main:** one STEREO output.
   - **Not on the console:** matrices, DCAs (MAN pp.192-197).
   
   Every input, tape return, effect return and STEREO has:
   - an attenuator (0 to -96 dB; STEREO +12 to -96)
   - a 4-band parametric EQ:
     - LOW: peak, low shelf or 12 dB/oct HPF
     - L-MID, H-MID: peak
     - HIGH: peak, high shelf or LPF
     - frequency 21 Hz-20.1 kHz in 1/12 octave steps, gain +-18 dB in 0.5 dB steps,
       Q 10 to 0.1 in 41 steps (MAN p.50)
   - a delay of 0-2600 samples (inputs and returns only)
   - phase, except on STEREO

   The inputs, tape returns, BUS 1-8 and STEREO each have one dynamics processor of
   six types: compressor, expander, compander hard and soft, gate, ducking
   (MAN pp.63-67).
   
   Other features:
   - Pans have 33 positions.
   - Fader groups A-D and mute groups E-H (MAN pp.148-149). A mute group flips its
     members' ON keys.
   - Pairs, which link most of a channel pair's settings (V2 p.184).
   - 128-step 100 mm faders: +10 to -90 dB and -oo on the channels, 0 to -120 dB
     and -oo on STEREO (MAN p.192).
   - Scene memories 1-96 and the read-only scene 0 (V2 p.128).
   - The EQ, dynamics, effects and channel libraries (40 presets each, the
     channel library user-only).
   
   Its MIDI (MAN pp.141-146, pp.206-224; V2 pp.159-164, pp.255-287):
   - Scenes by program change through a table.
   - Up to 114 parameters on control changes through an assignable map (receive
     only).
   - Yamaha SysEx, model 3D:
     - parameter change by address (type 0)
     - library and scene recall and store (type 24)
     - key remote (type 25)
     - fader and encoder remote (type 26)
     - the multiple-02R link (type 27)
   - Bulk dumps and requests (header `LM  8A67`, data as nibbles).
   
   Not in either guide, so Sidecar's reading until measured on a console:
   - the faders' step-to-dB tables
   - the pan law
   - 8 of the 41 Q values (the presets use 33)
   - the release, decay and hold tables
   - the type 0 address map and the key remote numbers
   - the byte layouts inside scenes and library programs
   - the effects' algorithms

   Left out:
   - surround panning (V2 pp.63-73)
   - AUTOMIX (timecode automation)
   - cascading
   - the MIDI Remote pages
   
   The engine's strips:
   - channels 1-16 are MIC/LINE 1-16, 17-20 LINE 17/18-23/24 (stereo), 21-36
     TAPE 1-16
   - aux 1-2 are EFF1-2
   - buses 1-8 are BUS 1-8, 9-16 AUX 1-8
   - main 1 is STEREO
   - FX 1-2 are its effects, fed by AUX 7-8
   
   Steps, each with every check green and the WING, X32 and ProMix 01 unchanged:
   1. **The 02R profile value** (done):
      - `Profile::Yamaha02R`: show id `02r`, C ABI 45 profile 4.
      - Its counts (`Counts::Y02R`): 36 channels, 2 aux, 16 buses, 1 main, no
        matrices, DCAs or mute groups, 2 FX slots.
      - Its fader laws: the channels' `FaderLaw::Y02R`, and STEREO's own
        (`FaderLaw::Y02R_STEREO`), so a profile's mains can now have their own law
        (`ProfileSpec::main_fader`, `fader_for`; scene fades use it; C ABI 45
        `ld_engine_fader_law_of`). The steps between the ends are Sidecar's
        reading of the panels' scales: 0 dB on step 101 of the channels' faders,
        on the top of STEREO's.
      - Its labels and slot ranges: CH 1-16, LINE 1-4, TAPE 1-16, EFF, BUS, AUX
        (bus slots 9-16), STEREO; the names TAPE 1, LINE 17/18, EFF1, AUX 8.
      - Its port groups (`Y02R_PORT_GROUPS`, in the WING's places):
        - INPUT 1-24, 2TR IN D1-D2
        - SLOT 1-4 (8 each, both ways)
        - STEREO OUT, AUX SEND 1-6, 2TR OUT D1-D2
      - Its palette: the ProMix 01's eight colours, one per strip kind.
      - Its factory strip: LINE 17-24 stereo on inputs 17-24, TAPE 1-16 on card
        slots 1-2, EFF1-2 on FX 1-2, BUS and AUX mono at 0 dB.
      - Its output patch: STEREO on STEREO OUT and 2TR OUT D1-D2, AUX 1-6 on AUX
        SEND 1-6, BUS 1-8 on every slot's outputs.
      - It still runs the WING's chains and models and has no remote.
      - A strip's factory settings, colour and name now come from its slot
        (`Strip::of_profile_in`), not from a channel's 1-based id.
   2. **Its processing** (done; `livedaw_dsp::y02r`):
      - The 4-band EQ (`Y2EQ`):
        - frequencies: 120 in 1/12 octave steps, 1 kHz x 2^((n - 67) / 12); every
          preset's frequency falls on them
        - gain: +-18 dB in 0.5 dB steps
        - Q: the 41 values, 8 of them Sidecar's reading
        - LOW past its last Q: L.SHELF, then HPF; HIGH: H.SHELF, then LPF, 12
          dB/oct, switched by `hpf` / `lpf` (the band's gain knob on the console)
        - defaults: the EQ reset (L.SHELF 125 Hz, 1 kHz and 4 kHz Q 0.7, H.SHELF
          10 kHz, flat; MAN p.51)
      - The six dynamics types (`Y2CMP`, `Y2EXP`, `Y2CPH`, `Y2CPS`, `Y2GAT`,
        `Y2DUK`), with the guides' ranges (MAN pp.63-67, p.194):
        - the compressor's defaults are scene 0's (flat, off); the others' their
          first preset's
        - the compander is a new curve (`Curve::Compand`): compress above the
          threshold, expand 5:1 (hard) or 1.5:1 (soft) under it by WIDTH
        - Sidecar's reading: RELEASE / DECAY (160 points) and HOLD (216) evenly
          spaced in log time between the guide's ends at 48 kHz; KNEE 2 dB a step;
          the expander's and compander's range 96 dB
      - The chains (`chain::Y02R_*`; the delay's place before the fader is
        Sidecar's reading):
        - inputs and tape returns: attenuator and phase, EQ, dynamics, delay,
          fader
        - effect returns: EQ, delay, fader
        - BUS 1-8: dynamics and fader
        - AUX 1-8: fader alone
        - STEREO: EQ, dynamics, fader
   3. **The console's own state** (done; `livedaw-02r`, as `livedaw-promix` is the
      ProMix 01's, whose value steps and pans it reuses). `State` holds what is
      below, and `Console` (`Y2Strip`) works it on the engine's strips:
      - a pair copies one side (V2 p.184's list: not the attenuator, phase or
        pan) and moves together, its processors linked
      - a fader group's faders move by as many steps from the panel; a group
        that is off doesn't
      - a mute group's ON keys flip, each from its own state
      - a gang's pans keep their offset (MAN p.38's example)
      - an aux pair's sends follow
      - aux sends: EFF1 only to AUX 8, EFF2 only to AUX 7
      - routing: a post-fader send at 0 dB following the pan for BUS 1-8; the
        main send for ST
      - KEY IN as the engine's `sc_source` / `sc_tap`: before the EQ (tap 1)
        or after (2); LEFT the odd neighbour; AUX 1-2, BUS 1-8 and STEREO
        their PFL; LEFT refused on MIC 1, TAPE 1, BUS 1 and STEREO
      - the parameters the control changes and the type 26 remote reach come
        with step 5

      What `State` holds:
      - pairs, fader groups A-D, mute groups E-H, pan gang
      - routing to BUS 1-8, ST and DIRECT
      - aux pre / post and the aux and bus pairs
      - the dynamics' KEY IN, ST.LINK and IN
      - recall safe and fade time
      - the oscillator
      - the parameters the control changes and the type 26 remote reach

      All of it is written and read as text for the show.
   4. **Its effects** (done; `livedaw_dsp::y02r::fx`, `livedaw_02r::library`):
      - The two slots: fed by AUX 7-8, at first REVERB HALL1 and DELAY L-C-R 1
        (Sidecar's choice).
      - The 40 preset programs' 17 types (MAN pp.98-111), each with the guide's
        parameters and ranges, its defaults its first program's:
        - reverb; chorus and flange into reverb
        - early reflections; gate and reverse gate reverbs
        - delay into early reflections
        - L-C-R delay, alone and after a chorus
        - mono delay into chorus; stereo initial-delay echo; echo with L->R and
          R->L cross feed
        - chorus (with AM, so tremolo too), flange, symphonic, auto pan,
          phasing, pitch change
      - The algorithms are Sidecar's:
        - the ProMix 01's where the types are the same (its `Stage` and `Taps`,
          now shared, the delay lines a second long, the echo's lines able to
          cross-feed)
        - the WING's AMBI for early reflections, GATED and REVERSE for the gate
          reverbs
      - Steps: 0.1 ms, 0.1 s and 0.05 Hz are Sidecar's reading.
      - Unmodelled:
        - the reverbs' DENSITY, REV DLY and REV BAL
        - the early reflection patterns beyond a room size, and ER NUM
        - the delays' filters and INI DLY
        - the modulation types' EQ
        - auto pan's F/R DEPTH and direction
        - phasing's stages
      - The libraries (`library`):
        - the 40 EQ presets (pp.56-60)
        - the 40 dynamics presets (pp.76-81; their 44.1 kHz times on the
          nearest 48 kHz step)
        - the 40 effects presets
        - user programs up to 128 (EQ from 33, the others from 41: MAN p.211)
        - `Console::recall_eq`, `recall_dyn`, `recall_fx`, `store_*`
        - not done: the channel library
   5. **02R MIDI** (done; `MidiProtocol::Yamaha02R`, C ABI 46 `ld_y02r_*`;
      `livedaw_02r::midi` and `params`, the app's `Y02RMidi` a thin port over
      them). The edit buffer is 2016 bytes in Sidecar's layout: 55 strips of 28
      (fader, ON, pans, attenuator, the switches as bits, delay, EQ, dynamics,
      routing), then the inputs' aux sends. A type 0 change sets a byte (as
      nibbles) or a bit. Type 26 moves the 21 panel faders and the encoders of
      TAPE 1-16 and EFF1-2; its other encoders and type 25 go to the app.
      Received values aren't echoed; the console's changes go out as type 0.
      MIDI SETUP lives in `y02rmidi.setup`. Not done:
      - a scene memory's dump on request (it needs the scene's mix without
        recalling it)
      - the key remote's keys (their numbers aren't in the guides)
      - the multiple-02R link (type 27)

      What it does:
      - program changes through the table
      - control changes through the CC map (the 02R preset, editable)
      - SysEx types 24 (recall / store), 26 (fader / encoder remote), 25 (key
        remote, Sidecar's numbering)
      - the bulk dumps and requests, sharing the ProMix 01's Yamaha framing; the
        dump bytes in Sidecar's layout
      - type 0 parameter change on Sidecar's address map, until the real one is
        known
   6. **Scene memories** (done):
      - Scenes 1-96 are the show's scenes 1-96. Program changes, SysEx recall
        and store, and the surface reach them.
      - A scene stored by SysEx, or received as a dump, grows the show to its
        number.
      - They keep the console's own state through the engine's console text
        (`<ConsoleState profile="02r">`, as the ProMix 01's): pairs, groups,
        gangs, the dynamics' keys, recall safe, fade times, the oscillator.
      - Scene 0, the initial data, is a new 02R's edit buffer
        (`params::initial_bytes`), recalled without rebuilding the console.
      - `livedaw_02r::panel` numbers that state for the host (C ABI 46
        `ld_y02r_state` / `ld_y02r_set_state`).
      - Not done: fader recall safe and fade times are kept, not yet applied
        to a recall.
   7. **An 02R skin** (done): `Y02RSurface` (`src/ui/Y02RSurface.*`, a `Skin`,
      `InterfaceKind::Yamaha02R`, the setting `02r`; SETUP > INTERFACE offers 02R
      on every skin and makes the console a fresh 02R, asking first; the
      Universal Mixer's 02R SURFACE key comes back). Its values are the edit
      buffer's bytes (`ld_y02r_get` / `set`, the panel's groups and gangs
      following) and the console's state values (`ld_y02r_state`). A
      screenshot test (`y02r-surface.png`, `y02r-display-dynamics.png`). Not
      done:
      - the analog controls (gain, pad, phantom)
      - DIGITAL I/O, SETUP, AUTOMIX
      - solo, the monitor keys
      - the channel library
      - the libraries' STORE and titles on the surface

      The panel in Sidecar's style:
      - 20 channel faders and STEREO, with FLIP to the tape returns
      - the encoders for TAPE 1-16 and EFF1-2
      - the display with its function keys
      - the SELECTED CHANNEL section (routing, aux, pan, EQ)
      - scene memory
      - the stereo meter

   Phase 6 is complete (November 2026) as far as the User's Guides go. What they
   don't give is Sidecar's reading, to be checked on a console:
   - the fader tables, 8 of the Q values, the time tables
   - the SysEx addresses and the dumps' byte layout
   - the effects' algorithms

Each emulation keeps the independent-project disclaimer: not affiliated with or
endorsed by the console's maker, their trademarks used only to say what is emulated,
none of their firmware, software, documents or artwork included.

## Still open from the WING work

Questions for the owner that the WING emulation still waits on:

- VSS3: the list of presets on a real WING.
- When a reverb or delay is bypassed, does its tail cut or ring out?
- Windows: is there a faint seam around a meter on an FX page when only the meters
  repaint (ENHANCE, *MASTER*)? A test showed Direct2D partial repaints a few levels off.
- FX / CHAN preset show items: what they load into; the STARTUP-skip key; the EXTERNAL
  effect's LATENCY unit; sources recalled with ROUTING; WING Edit's COMPACT fallback;
  whether to store parameters of firmware newer than 3.1; whether to snap values to the
  WING's steps; keeping the DAW-mode escape (an empty DAW key with DAW LAYER exits).

## Working on Sidecar

- Branch: `claude/live-daw-app-jujzep`; `main` follows releases.
- Build and test (see README, *Building from source*):
  `cd rust && cargo fmt --all --check && cargo clippy --workspace --all-targets -- -D warnings && cargo test --workspace`,
  then `cmake --build build --target LiveDAW SidecarPluginHost LiveDAWTests` (no
  warnings) and run `LiveDAWTests`.
- Releases: the CI workflow (`.github/workflows/ci.yml`) run by hand with
  `release_tag` (e.g. `v0.1.7-test.1`, or `v0.1.7`) and `prerelease`; it publishes only
  when every job passes. Release notes come from the version's CHANGELOG section
  (`packaging/release_notes.py`).
- Keep CHANGELOG, README, the user guide, ARCHITECTURE.md and the start-up notice in
  step with the code.
