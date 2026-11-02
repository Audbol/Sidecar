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
   Universal Mixer must be able to express all of them.
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

## Where the code stands (Sidecar 0.1.6)

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

## Architecture

Three layers:

1. **Universal core** (engine + show model), with no fixed counts:
   - Any number of input channels, aux / stereo inputs, buses, mains, matrices, DCAs
     (VCAs), mute groups, FX slots and monitor paths; mono, stereo or LCR strips and
     buses.
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

1. **Generalize the engine and show model** behind the current API: replace the
   fixed counts with configurable ones, introduce the block chain with the WING's
   fixed layout and process orders expressed as one configuration of it. The WING
   keeps working on every commit; tests stay green. Steps:
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
      - 3d: the blocks' parameters leave `StripParams` (which keeps the
        strip-level state); C++ and `map.rs` facades.
   4. **Show format 4**: the configuration, chains and sends; 0.1.x shows (format
      3) load through a converter.
   5. **A console that isn't a WING** in the tests (Rust and C++), and the docs.
2. **Universal Mixer UI**: a new interface over the core (strips, chains, routing
   matrix, sends, monitoring, scenes, recorder), its own look and layout.
3. **WING as a profile**: move the WING's limits, defaults, laws, surface, protocols
   and files into a WING profile over the core, and check it against the WING tests
   and WING Edit as today.
4. **Second emulation** (likely the X32 / M32, the closest to the WING), then Yamaha,
   Allen & Heath and DiGiCo, each extending the core where it needs something new.

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
