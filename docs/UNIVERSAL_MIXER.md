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
4. **Channel and bus formats:** mono, stereo and LCR. (No surround or immersive for
   now; the engine shouldn't make adding them later impossible.)

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
  `WING_BUSES` as defaults).
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
   fixed counts with configurable ones, add LCR, introduce the block chain with the
   WING's fixed layout and process orders expressed as one configuration of it. The
   WING keeps working on every commit; tests stay green.
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
