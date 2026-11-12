# The plug-in host console (plan, for 0.3.1)

A console of Sidecar's own design, not modelled on any maker's: a touch-first unit that hosts plug-ins
on a real console's inserts. Named **PLUGIN HOST**. Profile 11, `kProfileHost`.

## What it is (owner's answers, November 2026)

- An insert host for a real console: audio in from the console, through plug-ins, back out. Not a
  standalone mixer: no buses, mains or sends.
- Plug-in windows dock inside Sidecar.
- No shared racks: each channel has its own.
- Sidechain keying from any channel.
- MIDI learn for plug-in parameters, and hot knobs on the surface.
- Scenes recall whole plug-in states, with a safe per plug-in.
- A drawn hardware look like the other consoles, touch-first.
- 16, 32 or 64 channels, chosen in SYSTEM (and when the console is loaded).
- The Universal Mixer offers it as a device too: any console's strip can use a PLUGIN HOST rack.

## The unit

Drawn as a piece of hardware: a wide touchscreen with a row of eight channel strips under it, each with
a SELECT key, a channel name on console tape or an LCD, an input meter, a gain-reduction meter, BYPASS,
and two hot knobs (endless encoders with LED rings). Bank keys move the strips across 16, 32 or 64
channels. A data wheel, SCENE keys (previous, GO, next) and a CPU / latency readout sit to the right.

## The screen's pages

1. **RACK** (home): the selected channel's chain as large tiles, in signal order: name, maker, latency,
   CPU, bypass, wet/dry, in and out gain. Drag to reorder, tap + to add (the browser), long press for
   the menu (replace, copy, paste, save preset, safe). The docked plug-in window fills the right or lower
   half; a PIN keeps it up while channels change.
2. **OVERVIEW**: every channel as a column of small tiles: a glance at all racks, tap to jump.
3. **ROUTING**: per channel, the interface input it takes and the output it returns to; a sidechain
   source per plug-in that has a key input (any channel's input, pre or post its rack). A patch grid
   like the Q1202USB's.
4. **SCENES**: list, store, recall, crossfade time, the safes. Whole states, not parameter snapshots.
5. **CONTROL**: MIDI learn (tap a parameter, move a control), the hot knobs' assignments per plug-in,
   the MIDI devices.
6. **BROWSER**: search, favourites, tags, recent; the scan report (what failed and why).
7. **SYSTEM**: audio device and buffer, latency compensation, the CPU guard, the benchmark.

## Latency

The thing that matters most on inserts. Each channel shows its round trip (converter plus buffer plus
plug-ins). Two modes: **as is** (each channel only its own delay, the default) and **aligned** (every
channel delayed to the longest, for phase-related sources such as drum mics), with per-channel opt-out.

## Safety

Every plug-in sandboxed (exists); a crash bypasses only that plug-in and it comes back (exists). A CPU
guard from the benchmark's figures warns before a rack is added that would overrun the buffer. A
channel's BYPASS passes the input straight through, always.

## What exists and what is new

| Exists | New |
| --- | --- |
| `PluginChain` (chains, state, latency, sandbox) | the profile and its engine shape (inputs and outputs only) |
| plug-in block, scanner, browser list | sidechain inputs to plug-ins |
| scenes with fade times and safes | per-plug-in safes, whole-chain recall |
| MIDI for console controls | MIDI learn for plug-in parameters, hot knobs |
| user layers, Universal Mixer cards | the drawn unit and its seven pages |
| benchmark | the CPU guard; latency alignment |

## Steps

1. Engine: the profile (16, 32 or 64 channels, input → chain → output, no mix), routing per channel,
   tests. Universal Mixer shows it.
2. The drawn unit and the RACK page with docked windows.
3. ROUTING and sidechains (the engine's key inputs, then the page).
4. SCENES: whole states, safes, crossfade.
5. CONTROL: MIDI learn, hot knobs.
6. OVERVIEW, BROWSER, SYSTEM; latency alignment; the CPU guard.
7. The Universal Mixer: PLUGIN HOST racks offered on any console's strips.
8. Screenshots, user guide, start-up tile, completion percentage.
