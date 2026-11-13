<p align="center">
  <img src="branding/github/sidecar-readme-banner.png" alt="Sidecar: live mixing consoles" width="100%">
</p>

<p align="center">
  <a href="https://github.com/Audbol/Sidecar/releases"><img src="https://img.shields.io/github/v/release/Audbol/Sidecar?include_prereleases&label=release&color=FF9220" alt="Latest release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/licence-AGPLv3-3A3F48" alt="Licence: AGPLv3"></a>
  <img src="https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux-3A3F48" alt="Windows, macOS, Linux">
  <a href="https://discord.gg/TEE8q979x"><img src="https://img.shields.io/badge/Discord-join-FF9220" alt="Discord"></a>
</p>

# Sidecar

Sidecar turns an ordinary computer and audio interface into a mixing console for live sound.
It emulates the surfaces, screens, processing and remote protocols of real digital consoles, with
analog mixers to follow, and adds the **Universal Mixer**, its own interface for a console of any
size. Inputs from your audio interface are channels on the console; they feed buses, mains and
matrices, and anything can be sent to any hardware output. It runs on Windows, macOS and Linux.

> **Sidecar is an independent hobby project, made for fun and practice, and you use it at your
> own risk.** It is not affiliated with, endorsed by or supported by any console maker. Product
> names are their owners' trademarks, used only to say which console is emulated. No console here is complete, and each can differ from the real one: the
> processing recreates each unit's controls and character from published information, not the
> makers' algorithms, and there is no remote preamp control, StageConnect, AES50 or Dante (see
> [Known limitations](#known-limitations)). The notice shown at start-up gives a rough
> completion figure for each console. Don't rely on Sidecar for a critical live show without
> testing it thoroughly first.

**Status: v0.3.0 beta.** Choose a console under SETUP > SIDECAR > INTERFACE. Expect rough edges,
and please [report what you find](#reporting-problems).

| Console | How it appears in Sidecar |
| --- | --- |
| **WING** | The most complete: its surface and screens, and its network protocols, which WING Edit works with |
| **X32** | Its faders and layers, channel strip section and display, its remote protocol and MIDI |
| **ProMix 01** | Its faders, LCD pages, scene memories and MIDI |
| **02R** | Its surface, LCD pages and MIDI |
| **01V** | Its surface and LCD, matched to photographs of a real console, and its MIDI |
| **DM2000** | Its surface, dot-matrix LCD pages and MIDI |
| **SQ-5** | Its surface and touchscreen, MIDI and MIDI over TCP |
| **dLive** | C3500 and C1500 surfaces and touchscreens, MIDI over TCP |
| **PRO2** | Its processing and effects, on the Universal Mixer until it has a surface |
| **XENYX Q1202USB** | An analog mixer: its panel, routing and blocks, plus console tape and scenes |
| **PLUGIN HOST** | Sidecar's own insert host for a real console: a rack of plug-ins per channel, sidechains, scenes, hot knobs and MIDI learn |
| **Universal Mixer** | Sidecar's own touch-friendly interface, for any size of console |

Under all of them is one mixer core without any one console's limits. A console's size is a
configuration (any number of channels, buses, mains and matrices, up to 256 DCAs and mute
groups, 64 FX slots), and each strip's processing is a chain that can hold extra blocks and tap
points, including those of any other console emulated here.

## Use cases

- **A console for small shows and rehearsals.** A laptop and a multichannel audio
  interface become a full mixer: a band's rehearsal room, a church service, a school
  play, a conference room or a club night, with the processing and effects of the
  console you choose on every channel.
- **Learning a console, and training a team.** Each console's surface, screens, layers
  and keys are drawn as on the real one: the WING, X32, dLive, SQ-5, DM2000, 01V and the
  others. Engineers and volunteers can practise on any computer, with the **Virtual (no
  hardware)** device and an oscillator when there is no interface at hand.
- **Preparing a show before the gig.** Patch, name, colour, set up processing, effects
  and scenes at home. Remote apps and protocols connect to Sidecar as they would to the
  console: the WING's apps, the X32's protocol, MIDI and MIDI over TCP.
- **A second mixer next to the main console.** Monitors, a broadcast or livestream
  mix, an overflow room or a recording mix, fed from a split of the stage inputs.
- **Plug-ins on a real console's inserts.** The PLUGIN HOST console runs a rack of
  plug-ins on each channel, with sidechains, scenes, hot knobs and MIDI learn.
- **Controlling a real console.** The Universal Mixer can connect to a real X32 and
  mirror it, with more consoles to follow.
- **Multitrack recording and virtual soundcheck.** Record every channel of a show,
  play it back through the console the next day to refine the mix without the band,
  and edit, import and export the tracks in the built-in multitrack editor.
- **Mixing with your plugins and your outboard gear.** VST3, AU and LV2 plugins sit
  in the FX rack and in any strip's chain, and an external effect puts a hardware
  compressor or reverb on an insert through the interface's outputs and inputs. The
  plugin overview keeps every plugin (its picture, state, meters, CPU) in view in a
  window of its own, on a second screen if you like.
- **Remote mixing from a tablet or phone.** The web remote shows the console's
  screen in a browser on the network; MIDI controllers and Mackie Control / HUI
  surfaces drive it too.

Sidecar is a hobby project, not a replacement for a tested console: try it well before
relying on it at a show (see [Known limitations](#known-limitations)).

## Screenshots

Every picture is one of Sidecar's own drawings, rendered at 1920 x 1080.

| | |
|---|---|
| ![**Universal Mixer, Flagship Black:** a band on a 32-channel console](docs/images/universal-mixer.png) | ![**Studio Slate:** the same console in another of the nine themes](docs/images/universal-mixer-slate.png) |
| **Universal Mixer, Flagship Black:** a band on a 32-channel console | **Studio Slate:** the same console in another of the nine themes |
| ![**CHANNEL panel, maximised:** EQ handles, RTA and the dynamics curves](docs/images/channel-panel-maximised.png) | ![**Foreign blocks:** a WING EQ on a ProMix 01 strip](docs/images/foreign-blocks.png) |
| **CHANNEL panel, maximised:** EQ handles, RTA and the dynamics curves | **Foreign blocks:** a WING EQ on a ProMix 01 strip |
| ![**User layers:** a layer of your own, with its editor](docs/images/user-layers.png) | ![**Start-up tile picker:** every console as a picture](docs/images/console-picker.png) |
| **User layers:** a layer of your own, with its editor | **Start-up tile picker:** every console as a picture |
| ![**Benchmark:** what this PC can run](docs/images/benchmark-results.png) | ![**Plug-in block:** a VST3, AU or LV2 plug-in in a strip's chain](docs/images/plugin-block.png) |
| **Benchmark:** what this PC can run | **Plug-in block:** a VST3, AU or LV2 plug-in in a strip's chain |
| ![**WING**](docs/images/console.png) | ![**WING COMPACT**](docs/images/compact.png) |
| **WING** | **WING COMPACT** |
| ![**WING RACK**](docs/images/wing-rack.png) | ![**X32**](docs/images/x32-surface.png) |
| **WING RACK** | **X32** |
| ![**ProMix 01**](docs/images/promix-surface.png) | ![**02R**](docs/images/y02r-surface.png) |
| **ProMix 01** | **02R** |
| ![**01V**](docs/images/y01v-surface.png) | ![**SQ-5**](docs/images/sq5-surface.png) |
| **01V** | **SQ-5** |
| ![**dLive C3500**](docs/images/dlive-c3500.png) | ![**dLive C1500**](docs/images/dlive-c1500.png) |
| **dLive C3500** | **dLive C1500** |
| ![**DM2000**](docs/images/dm2000.png) | ![**PRO2**](docs/images/pro2.png) |
| **DM2000** | **PRO2** |
| ![**XENYX Q1202USB**](docs/images/q1202usb.png) | ![**WING METERS screen**](docs/images/meters.png) |
| **XENYX Q1202USB** | **WING METERS screen** |
| ![**PLUGIN HOST**](docs/images/pluginhost-rack.png) | ![**PLUGIN HOST ROUTING:** the patch grid](docs/images/pluginhost-routing.png) |
| **PLUGIN HOST:** a rack of plug-ins | **PLUGIN HOST ROUTING:** the patch grid |

## What it does

Every console runs on one mixer core, with the same recorder, plugin hosting, shows
and scenes. Each console brings its own surface, processing, effects and remote
protocol, as in the table at the top; the [user guide](docs/USER_GUIDE.md) has a
chapter for each.

- **Each console's size and processing:** its channels, buses, mains, matrices, DCAs
  and mute groups, and its processing in its own order: preamp, filters, gate,
  dynamics, EQ and inserts, with its own models. Sizes can grow beyond the real
  console's (up to 64 FX slots, 256 DCAs and mute groups); the limit is your CPU.
- **Each console's surface on screen:** its faders, layers, keys, meters and display
  pages (touchscreens, LCDs, dot-matrix pages), resized to any screen. Displays can be
  shown on their own (F10).
- **Effects:** each console's effects in an FX rack, with faceplates whose meters and
  gain reduction move with the signal, plus VST3, AU (macOS) and LV2 (Linux) plugins,
  and an external effect for outboard gear through your interface.
- **Routing:** any input to any channel, any signal to any output, direct outs, and
  each console's own routing and patch pages.
- **Shows, scenes and snippets** with recall scope, recall safes, global safes, fades,
  presets and STARTUP files; scenes and snippets can be exported to files and imported
  into another show. Analog mixers get a scenes bar of their own.
- **Multitrack recording** of up to 64 tracks, playback into the console (virtual
  soundcheck) and a built-in multitrack editor that imports and exports audio files; a
  2/4-track recorder and player using a folder on disk.
- **Monitoring and talkback:** phones and speakers paths with their own sources, EQ,
  delay and limiter, talk keys, and an oscillator for line checks.
- **Remote control:** MIDI, Mackie Control / HUI, each console's own protocol where
  it is public (the WING's network protocols, the X32's OSC, MIDI over TCP for the SQ-5
  and dLive, the Yamaha consoles' MIDI), and a web remote for a tablet, phone or another
  computer (optional PIN).
- **Built for stability:** a realtime engine in Rust with no allocations or locks on
  the audio thread, plugin faults contained, an audio device watchdog, autosave
  every 30 s and a safe mode after a crash.

### The WING

The most complete console so far.

- **Three models:** the full-size WING (grey or black), the **WING COMPACT** (one
  12-fader section with its own BUS, DCA, MAIN / MATRIX and USER keys and the VIRTUAL
  section) and the **WING RACK**.
- **Its size and processing:** 40 input channels, 8 aux, 16 stereo buses, 4 stereo
  mains, 8 matrices, 16 DCAs, 8 mute groups and 16 FX slots; its 32 dynamics models,
  the WING EQ and its analog EQ models, two inserts and all of its tap points.
- **Its FX rack:** 26 premium effects, 32 standard effects, 5 channel strips and the
  EXTERNAL effect.
- **Its surface and screens:** three fader sections with layers, user layers and spill,
  the main display's screens (HOME, EFFECTS, METERS, ROUTING, SETUP, LIBRARY, VIEW,
  UTILITY), the channel strip, monitoring, 4-Channel and custom control sections.
- **Custom controls** with its functions (fader start, inserts, DAW layer, tap tempo,
  mute groups, scenes, the recorders).
- **Remote apps and files (experimental):** Sidecar answers the WING's network
  protocols (discovery, OSC and the native protocol), so WING remote software such as
  WING Edit can find and drive it; it imports and exports WING snapshots.

### The Universal Mixer

Sidecar's own interface, for a console of any size. It is touch friendly and sets up fast.

- **Themes:** nine, from Flagship Black to Studio Slate and High Contrast, under MENU > Theme.
- **Block swapping between consoles:** put a WING EQ on a ProMix 01 strip, or a DM2000
  compressor on an X32. Shows and scenes keep the swap.
- **User layers:** make your own layers, drag strips into them, and share them with the
  consoles' surfaces.
- **CHANNEL panel:** one strip whole, with EQ handles you drag, an RTA behind the curve and
  a cards-in-signal-order view of the chain. It can fill the window.
- **Plug-in blocks:** a VST3, AU or LV2 plug-in as a block in any strip's chain.
- **PLUGIN HOST racks:** a whole rack of plug-ins on any strip, as on the PLUGIN HOST console.
- **Benchmark:** SETUP > BENCHMARK measures what this PC can run, per block and per effect.
- **Remote control of a real X32:** CONNECT to its IP address and Sidecar mirrors it and drives it.
- **Analog mixers:** the XENYX Q1202USB, plus console tape (name strips) and scenes on
  the ProMix 01, 02R and 01V.
- **Start-up tile picker:** pick a console from pictures of their front panels.

The [user guide](docs/USER_GUIDE.md) describes every part in detail.

## System requirements

- **Windows** 10 or 11 (64-bit). ASIO drivers (your interface's own, or ASIO4ALL /
  FlexASIO) give the lowest latency; WASAPI also works.
- **macOS** 11 (Big Sur) or newer, CoreAudio.
- **Linux**, x86_64 with glibc 2.38 or newer, ALSA or JACK (or PipeWire's JACK
  layer) and X11.
- A multi-core CPU (strips are processed in parallel; a 40-channel console with
  processing on every channel uses a fraction of one core on a current machine),
  and a screen of at least 1366 x 768 at 100 % scaling (larger is better).
- An audio interface with as many inputs and outputs as you want to mix. Without one,
  the **Virtual (no hardware)** device lets you try the console.

## Download and install

Builds for all three platforms are attached to each release on the
[releases page](https://github.com/Audbol/Sidecar/releases). See the release page
for the files of each version.

- **Windows:** download the zip, extract it anywhere and run `Sidecar.exe` (no
  runtime needed), or use the installer if the release offers one. Windows
  SmartScreen may warn about an unknown publisher the first time: choose *More info*
  > *Run anyway*.
- **macOS** (Apple silicon and Intel): open the disk image and drag `Sidecar.app` to
  *Applications*. The app is not signed or notarized by Apple, so the first time
  **right-click (or Ctrl-click) it and choose Open**, then Open again (on macOS 15 and
  newer: open it once, then *System Settings > Privacy & Security > Open Anyway*).
  Allow microphone access when asked: that is how macOS lets Sidecar read your audio
  interface's inputs.
- **Linux arm64** (a Raspberry Pi 4 or 5 on 64-bit Raspberry Pi OS): unpack the
  `Linux-aarch64` tarball and run `Sidecar`. Experimental, and untested on a Pi so far.
- **Linux** (x86_64, glibc 2.38 or newer: Ubuntu 24.04+, Debian 13+, Fedora 39+ and
  similar): run the AppImage (`chmod +x Sidecar-*.AppImage`; it needs FUSE, or start
  it with `--appimage-extract-and-run`), or extract the tarball and run `Sidecar`;
  its `README-LINUX.txt` covers the runtime libraries, JACK / PipeWire and a menu
  entry. For low latency, add your user to the `audio` group and allow realtime
  scheduling (`/etc/security/limits.d/audio.conf`: `@audio - rtprio 95` and
  `@audio - memlock unlimited`), then log in again.

## First steps

Pick a console in the start-up tile picker. The steps below use the WING's keys; every
other console has the same functions on its own pages, as the [user guide](docs/USER_GUIDE.md)
describes.

1. **Choose the audio device:** on the main display, press **SETUP**, then **SIDECAR**.
   The **DEVICE** field opens the driver (ASIO, WASAPI, CoreAudio, ALSA, JACK...),
   the device and the buffer size; **CLOCK RATE** (SETUP > AUDIO) sets the sample rate. On Windows,
   Sidecar picks ASIO automatically when an ASIO driver is installed. A buffer of 64
   to 256 samples suits live use.
2. **Patch the inputs:** press **ROUTING**. Pick a channel tile, then its source (the
   inputs of your interface). **+1 AUTO** moves on to the next channel. Under
   **OUTPUTS**, pick a hardware output, then the signal that feeds it (Main 1 L / R
   for your PA, for example).
3. **Mix:** pick layers on each fader section, **SELECT** a strip and edit it on the
   **HOME** screen and in the channel strip section; the six controls under the
   display follow the screen. Assign FX devices on **EFFECTS** and put them on the
   strips' inserts.
4. **Save:** **LIBRARY > SHOW** saves and opens shows (`Ctrl+S`, `Ctrl+O`); **SNAP**
   saves scenes, **GO** / **GO NEXT** recall them. The console is also autosaved every
   30 seconds and restored at the next start.
5. **Record:** the status bar's **LIVE** icon opens the recorder. **RECORD ALL
   CHANNELS** arms a track per input channel; **REC** on the recorder (or the MULTI
   TRANSP custom-control layer) starts recording into
   `Documents/Sidecar/Recordings`. **VIRTUAL SOUNDCHECK** plays the recording back
   through the console. `Ctrl+E` opens the multitrack editor.
6. **Connect remote apps (optional, experimental):** each console's remote protocol is
   set up in its own SETUP or MIDI pages (see the user guide). On the WING, **SETUP > REMOTE > WING REMOTE >
   ENABLE** starts the WING network servers (UDP 2222 discovery, UDP 2223 OSC, TCP
   2222 native). Remote apps on the same network can then discover Sidecar under its
   console name. Allow Sidecar through your firewall. Tested with the WING
   Edit 3.3.4 (connect, MIXER -> PC and PC -> MIXER sync on both console models, live
   control both ways, FX and channel processing models, inserts), libwing's clients
   and the Bitfocus Companion WING module's OSC paths; testers use Mixing Station.
   WING Copilot has not been tried.

## Keyboard shortcuts

On macOS, use Cmd instead of Ctrl.

| Key | Action |
|---|---|
| F10 or Ctrl+Shift+F | Show only the main display (and back) |
| F11 | Full screen (and back) |
| Esc | Leave display-only, then full screen |
| Ctrl+S / Ctrl+Shift+S | Save the show / save it as |
| Ctrl+O | Open a show |
| Ctrl+G | Recall the next scene |
| Ctrl+E | Open / close the multitrack editor |
| Ctrl+L | Lock / unlock the surface and the touchscreen (as holding HOME alone) |
| Ctrl+P | Save a screenshot of the display to `Documents/Sidecar/screens` |
| Ctrl+Shift+P | Open the plugin overview (every FX slot's plugins in a window of its own) |

Surface keys with a second function act on a long press (half a second) or a
right-click. The multitrack editor has its own keys (space, R, L, M, S, Ctrl+Z...;
see the [user guide](docs/USER_GUIDE.md#multitrack-recording)).

## Files and folders

- `Documents/Sidecar/`: recordings (`Recordings`), the USB recorder's folder (`USB`),
  the library (`Library`) and screenshots (`screens`).
- App settings (`Sidecar.settings`, including the plugin list) and the autosave:
  `%APPDATA%\Sidecar` on Windows; `~/Library/Application Support/Sidecar` (settings)
  and `~/Library/Sidecar` (autosave) on macOS; `~/Sidecar` (settings) and
  `~/.config/Sidecar` (autosave) on Linux.
- Folders from before the rename (`Live DAW`, `LiveDAW`) keep being used if they exist.

## Known limitations

This list matches the notice Sidecar shows at start-up.

- **Sound:** the channel processing and FX models recreate each unit's controls and
  character from each console's published parameters, not the makers' exact algorithms.
- **Hardware:** your audio interface's inputs and outputs take the place of the
  console's. There is no StageConnect, AES50 or Dante, and no remote preamp control
  (HA REMOTE): source GAIN is digital and +48V is only stored.
- **Recording:** instead of a console's own recorder (SD cards or USB), a multitrack recorder writes to a
  folder on disk (with an editor); the USB recorder uses a folder of your choice
  instead of a USB stick. The MP3 encoder is basic (constant bit rate, a simple
  masking model).
- **Plugins** (VST3, AU, LV2) run inside the mixer process by default: a plugin that
  crashes takes Sidecar down (safe mode then restores the show without plugins). The
  experimental **plugin sandbox** (SETUP > SIDECAR > PLUGINS > RUN PLUGINS IN SANDBOX, off by
  default) runs each plugin in its own helper process instead: a crash or hang only
  bypasses that FX slot. It adds one audio block of latency (delay-compensated like a
  plugin's own), and the plugin's editor opens in a separate window.
  Delay compensation (SETUP > SIDECAR > PLUGINS) lines up the paths into buses, mains and
  matrices up to 8192 samples; solo, monitoring, direct outs and recorder taps are
  not compensated.
- **The Universal Mixer** (SETUP > SIDECAR > INTERFACE: UNIVERSAL) is a preview: custom
  controls are still on each console's own surface, and a console's own surface and
  remote apps reach only as many channels, buses, mains and matrices as the real
  console has.
- **WING remote apps:** the WING network protocols answer every parameter of the
  WING 3.1 tree but one; 93.2 % drive the engine or Sidecar's settings (of 79,326,
  counting every processing model's parameters and every custom control mode's
  function nodes); the rest answers the WING's factory value and is stored but does
  nothing. WING Edit 3.3.4 works with it (sync both ways, live control, FX,
  processing models, inserts, the EXTERNAL effect as an outboard insert; a plugin's
  FX slot shows as an EXTERNAL effect); the parameters WING Edit knows from firmware
  newer than 3.1 are ignored. WING Copilot has not been tried, and exported WING
  snapshots have not been loaded into a console.
- **MIDI and DAW remote control** (MCU / HUI) have not been tested with real hardware.
- **LIBRARY:** scene and snippet files are Sidecar's own (.lsnap / .lsnip), not the
  consoles' own files (WING snapshots and X32 scenes import and export), and the CLIP tab plays its clip on the USB player.
- **Drawn but disabled:** the GPIO and SD functions of the custom controls (there is
  no GPIO socket or SD card), and settings that only make sense on the hardware.

## Reporting problems

Please open an [issue](https://github.com/Audbol/Sidecar/issues/new/choose) with
your operating system, audio interface and driver, buffer size and sample rate, and
what happened. The event log (**SETUP > SIDECAR > LOG** or **UTILITY**) shows device restarts,
plugin faults and errors; a screenshot of it helps. When Sidecar is started from a
terminal, the same messages are printed there. Security issues: see
[SECURITY.md](SECURITY.md).

For questions, setup help and quick feedback, join the
[Sidecar Discord server](https://discord.gg/TEE8q979x); bugs are easier to track as issues.

## Building from source

You need:

- CMake 3.22 or newer
- a C++20 compiler: Visual Studio 2022 or 2026 (MSVC), Xcode 14+ or GCC 11+ / Clang 14+
- a stable Rust toolchain, 1.83 or newer ([rustup](https://rustup.rs))
- Git (CMake fetches JUCE 8 and Corrosion at configure time)

The audio engine, DSP and sessions are written in Rust (`rust/`) and linked into the
JUCE app as a static library through [Corrosion](https://github.com/corrosion-rs/corrosion).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure   # C++ tests (engine, shows, stability, plugins, every console)

cd rust && cargo test --workspace                       # Rust tests
```

The app is built at `build/LiveDAW_artefacts/Release/` (`Sidecar.exe`, `Sidecar.app`
or `Sidecar`).

### Linux

```bash
sudo apt install build-essential pkg-config cmake git libasound2-dev libjack-jackd2-dev \
  libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev \
  libxext-dev libxinerama-dev libxrandr-dev libxrender-dev
```

(Package names are Debian / Ubuntu ones; other distributions have equivalents.)

### Windows

Build from a *Developer Command Prompt* for VS 2022 or VS 2026 (or let CMake find
Visual Studio; for VS 2026, use the CMake that comes with it):

```bat
cmake -S . -B build
cmake --build build --config Release
```

ASIO is built in: JUCE (8.0.11 and later) bundles the ASIO SDK, which Steinberg
relicensed under GPLv3 in October 2025, so there is nothing to download. The `.exe`
uses the static MSVC runtime, so it runs on any Windows 10/11 PC without the VC++
redistributable.

### macOS

Install Xcode (or its command-line tools), CMake and Rust, then build as above
(`-G Xcode` also works). The minimum deployment target is macOS 11.

### CMake options

| Option | Default | Meaning |
|---|---|---|
| `LIVEDAW_JUCE_DIR` | (empty) | Use a local JUCE checkout instead of fetching JUCE |
| `LIVEDAW_JUCE_TAG` | `8.0.15` | JUCE tag to fetch |
| `LIVEDAW_BUILD_TESTS` | `ON` | Build the `LiveDAWTests` test runner |
| `LIVEDAW_ASIO` | `ON` | Windows: ASIO support (JUCE's bundled SDK) |
| `LIVEDAW_ASIO_SDK_DIR` | (empty) | Windows: use another copy of the ASIO SDK |
| `LIVEDAW_VST2_SDK_DIR` | (empty) | Enable VST2 hosting with your own copy of the legacy VST2 SDK (not distributable) |
| `LIVEDAW_SANITIZE` | (empty) | GCC / Clang sanitizers, e.g. `address,undefined` or `thread` |

### Sanitizer builds (GCC / Clang)

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DLIVEDAW_SANITIZE=address,undefined
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DLIVEDAW_SANITIZE=thread
# run the TSan tests with TSAN_OPTIONS=suppressions=tests/tsan.supp (the Rust part is not instrumented there)

# Rust engine under ThreadSanitizer (nightly):
cd rust && RUSTFLAGS=-Zsanitizer=thread cargo +nightly test -Zbuild-std --target x86_64-unknown-linux-gnu --workspace
```

### Developer tools

- `tools/wing/gen_tree.py` and `tools/wing/gen_models.py` regenerate the WING
  parameter tree (`rust/crates/livedaw-wing/data/wing_tree.bin`) and the processing
  model tables (`rust/crates/livedaw-dsp/src/wing/models_gen.rs`) from libwing's
  parameter sweep (`propmap.jsonl`); the models' defaults come from
  `tools/wing/model_defaults.json` (captured from WING Edit).
- `tools/wing/libwing_tools.sh DIR` builds libwing's command-line clients and
  `tools/wing/oscprobe.rs`; then
  `LIBWING_TOOLS=DIR/tools/target/release cargo test -p livedaw-wing --test libwing`
  tests the WING remote servers against them.
- `tools/wing/check_companion.py` checks every OSC path the Bitfocus Companion WING
  module builds against a running server.
- `cargo run -p livedaw-wing --example wing_emulator` runs the WING protocols without
  the app; `--example wing_coverage` prints how much of the WING tree maps to the
  engine; `--example wing_snap` imports or exports a WING snapshot headlessly;
  `--example render` runs a WAV file through channel 1 set up from WING parameter
  paths, offline (used by `tools/wing/audio_compare`, which compares Sidecar's sound
  with recordings of a WING).

## Project layout

```
rust/crates/livedaw-dsp/      DSP: every console's processing models and FX rack devices
rust/crates/livedaw-engine/   Realtime engine: the console's configuration (the WING's by default), strips and their chains (extra blocks, taps), sends, DCAs, mute groups, FX slots, solo, worker pool
rust/crates/livedaw-session/  Sessions: console state, Show > Scene > Snippet, fades, fader banks, show files
rust/crates/livedaw-wing/     WING parameter tree, its mapping onto the engine, the WING remote protocols, .snap/.show files
rust/crates/livedaw-recorder/ Multitrack recorder and the USB recorder / player (WAV, FLAC, AIFF, MP3 codecs, disk threads)
rust/crates/livedaw-ffi/      C ABI over the engine and model tables + C++ header (include/livedaw_engine.h)
src/engine/   C++ side: facades over the Rust code, audio devices, plugin hosting, MIDI and the consoles' remote protocols
src/daw/      Multitrack projects: tracks, clips, markers, undo, import / export
src/ui/       JUCE UI: every console's surface and screens, the Universal Mixer and the PLUGIN HOST
src/ui/daw/   The multitrack editor and the recorder screen
resources/    Barlow fonts (SIL Open Font License)
tools/wing/   Generators for the WING data, libwing test tools
tests/        C++ tests (run with ctest)
docs/         User guide and architecture notes
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the signal flow and the
realtime and threading design.

## Contributing

Bug reports, testing with real hardware and pull requests are welcome; see
[CONTRIBUTING.md](CONTRIBUTING.md). In short: keep the audio thread realtime-safe,
add tests, keep `cargo fmt`, `cargo clippy` and the C++ build warning-free, and never
add any maker's firmware, software, documents or artwork.

## Licence

Sidecar is free software: you can redistribute it and/or modify it under the terms
of the **GNU Affero General Public License, version 3**, as published by the Free
Software Foundation. It is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See [LICENSE](LICENSE) for the full text.

The source code is at <https://github.com/Audbol/Sidecar>. If you distribute
Sidecar, or modify it and let others use it over a network (for example through its
WING remote servers), the AGPLv3 asks you to offer them the corresponding source.

Sidecar is built on [JUCE](https://juce.com), which it uses under the AGPLv3. Windows
builds include the ASIO SDK (GPLv3). The UI fonts are Barlow (SIL Open Font
License 1.1), and the WING parameter data is derived from
[libwing](https://github.com/jerrythemaus/libwing) (Apache License 2.0). See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for every component and its licence.

Product names are trademarks of their owners. Sidecar is an
independent hobby project, not affiliated with or endorsed by any console
maker, and contains none of their firmware, software or artwork.
