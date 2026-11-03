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
        profile names it (C ABI `RemotesC::midi` 1). Still to come: OSC over SysEx,
        snippets and cues by program change, a settings page for the devices.
   7. **Scenes and the interface**: `.scn` import and export; the Universal Mixer
      shows the X32 (its strip kinds, blocks and colours from the profile).
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
        meter, SELECT, SOLO and MUTE. The display shows the selected strip's
        inspector (HOME) and the Universal Mixer's ROUTING, SETUP, LIBRARY, EFFECTS
        and MONITOR views. A screenshot test (`x32-surface.png`) checks the layers.
      - 8c, the channel strip section (done): `x32::ChannelSection`
        (`src/ui/X32ChannelSection.*`) for the selected strip: CONFIG / PREAMP (GAIN,
        LOW CUT and its key, polarity), GATE (THRESHOLD, its key, the gain
        reduction), DYNAMICS (THRESHOLD, RATIO, COMP, the gain reduction), EQUALIZER
        (the band keys, 4 or 6, the band's TYPE, FREQ, GAIN and Q, EQ), BUS SENDS (four
        at a time, the bank keys), MAIN BUS (PAN, LR, M/C and its level); each
        section's VIEW key shows the strip on the display. The knobs are the WING
        screens' `DisplayKnob`s, bound through the console's models (an X32's gate,
        compressor and EQ).
      - 8d: the display's pages (the Universal Mixer's views inside it first).
      - 8e: polish, screenshots, the user guide. The X32 Compact (16 + 1 faders) as
        a model of it, like the WING COMPACT.

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
