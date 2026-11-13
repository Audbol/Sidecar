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

## Sidechains

A hosted plug-in that runs in Sidecar's process and has a second input bus can be keyed from any
channel's input (before its plug-ins) or output (after them). The choice is stored per plug-in
(`PluginChain::Key`) and saved with the show and the scenes (`keyChannel` and `keyPost` on the chain's
`MEMBER`, so a keyed plug-in is stored as a chain of one). A plug-in without such a bus, and any plug-in
in the sandbox (its buses are stereo only), has no key choice.

The engine does the work, with the machinery the dynamics' KEY SOURCE uses (`keytap.rs`): the keying
channel writes its IN or POST tap into a buffer of its own, the keyed channel runs in a later wave, and
the block's process call carries the key as a third, mono channel, which `PluginChain` copies onto the
plug-in's sidechain channels. When the key channel runs after the keyed one (two channels keying each
other, or a chain of keys deeper than four) the key is one block late, never half written. A channel
cannot key itself after its own plug-ins (it would feed itself): the engine refuses it. A block has one
key: every plug-in of its chain that has a sidechain takes it.

## Scenes

A scene is the show's ordinary scene, with the whole recall scope (a PLUGIN HOST scene holds the patch too:
the other consoles leave the routing out). What it holds, per channel:

- each plug-in's whole state (its state blob), and its bypass; the channel's BYPASS;
- the rack's order, and which plug-ins are in it. A plug-in the scene lacks is taken out of the rack
  (its editor is closed first); one the scene has and the rack lacks is made again from its description,
  with the block id it had, so the next recall finds it;
- each plug-in's sidechain key;
- the input and output patch.

**Safes.** Two kinds, both saved with the show. A safe channel (`recallSafe`, as on the other consoles) is
left alone altogether. A safe plug-in (the rack tile's block, `Block::safe`; a block that holds a chain of
several plug-ins is safe as a whole) keeps its state, its bypass, its key and its place, and stays when the
scene lacks it. The chain's key is one for the block, so a safe plug-in that has a sidechain keeps the key of
the block it is in. A RESTORE (the whole scene, safes included) ignores both.

**Fade.** Plug-in states cannot be crossfaded in general, so each scene has a mode with its fade time:

- SNAP (the default): the states apply at once.
- GLIDE: for a plug-in that runs in this process, the continuous parameters (those that are not stepped) move
  from where they are to the scene's values over the fade time, at 60 Hz on the message thread, and the whole
  state is applied at the end, so what the parameters do not hold (a program, a patch name, a sample) arrives
  then. Stepped parameters (a choice, a switch) take the scene's value when the recall starts. A plug-in in the
  sandbox, or without parameters, snaps at the start; a fade time of 0 snaps. The scene's values are read from a
  second copy of the plug-in that has taken the state (a live plug-in would jump to them for a moment), so a
  GLIDE costs one more instance of each plug-in for the length of the call. A new recall ends a glide that is
  running. The engine's own values (trim, the fader) keep using the show's fade.

**Off the audio thread.** The recall runs on the message thread, as adding a block does. Taking blocks out and
putting them back is the engine's `remove_block`, `restore_block` and `move_block`, which publish a new
chain layout the audio thread picks up between blocks (the old one is freed by the garbage collector, after
the audio thread can no longer reach it). A plug-in is made by `PluginHost::createInstance` on the message
thread and installed with `FxSlots::setPlugin`, the same swap a hosted plug-in's replacement uses; a state
is applied with `setStateInformation` on the message thread, as the other consoles do for their FX slots. The
audio thread only ever sees a finished plug-in.

**The SCENES page.** A list of the scenes (number, name, mode, time, and a mark on the active and the cued
one) with RECALL, STORE NEW, OVERWRITE, RENAME (on-screen keyboard), DELETE, MOVE UP and DOWN, and the cued
scene's SNAP / GLIDE and time. OVERWRITE and DELETE ask twice (the key turns to CONFIRM for a few seconds). The
SAFES panel lists every channel and, under it, every plug-in of its rack, each with a SAFE key. The cue is the
surface's: PREV and NEXT move it, a tap on a row cues the scene, GO and RECALL recall it, and a store, a delete
or a move leaves it on the scene concerned.

## Hot knobs and MIDI learn

**Hot knobs.** A plug-in has up to two parameters assigned as its hot knobs (`PluginChain::Control`, kept on the
chain's member beside the sidechain key: `hot1`, `hot1Name`, `hot2`, `hot2Name` on the `MEMBER`, so a plug-in with
any is stored as a chain of one). A parameter is kept by its index in the plug-in and its name, so a reload finds
it again. The two knobs of a channel strip control the hot parameters of the channel's *focused* plug-in: the one
selected on RACK while that channel is the one RACK shows; otherwise (another channel, or the selected plug-in has
none) the first plug-in of the chain that has hot parameters. The ring shows the parameter's value (dim and at rest
for an unassigned knob), and the strip's LCD shows its short name and value while the knob is touched and for a
moment after. A drag turns a knob (200 px for the whole range), the wheel in fine steps; a stepped parameter moves
a step once the turns add up to half of one.

Assigning: on RACK, the dock's header has ASSIGN HOT 1 and ASSIGN HOT 2. Tap one (it reads TOUCH A CONTROL), then
touch or move a control of the plug-in; the dock listens to the plug-in's parameter *gestures* (what its editor, a
generic slider, or a sandboxed plug-in's generic control starts on a touch), so one tap on a slider is enough. A
plug-in whose editor changes values without a gesture is not caught; use the parameter list. The CONTROL page's HOT
KNOBS panel lists the selected channel's plug-ins, each with its two hot parameters; a tap opens the plug-in's
parameter list (and NONE). A parameter is on one knob only. A scene recall of the same plug-ins leaves the
assignments as they are; a plug-in a scene makes again takes the assignments it was stored with.

**MIDI.** Sidecar's existing MIDI layer is `MidiPorts` (every console's remote uses it): the devices' callbacks only
queue messages, which arrive on the message thread. The PLUGIN HOST adds no second stack: `ph::Control` owns a
`MidiPorts` (up to eight inputs, chosen on the CONTROL page, an app setting `pluginhost.midi.inputs`) and everything
runs on the message thread; parameters are set with `setValueNotifyingHost` there, never from the MIDI thread.
The console remotes have no learn and no relative or 14-bit controllers (only the ProMix 01 and the SQ's NRPNs), so
learn is new: a controller (absolute, 7-bit; relative, an endless encoder: 1-63 up, 65-127 down, set per mapping with
MODE) or a note (each note on toggles; a note off does nothing). 14-bit pairs are not supported.

LEARN: tap LEARN, tap or move a plug-in parameter (in the dock, or choose one in the CONTROL page's picker), or tap a
hot knob, a channel's SELECT or BYPASS, or the scene PREV, GO or NEXT keys (a key tapped while LEARN waits is taken
and does not act), then move a MIDI control. One control has one target, and a target one control: learning again
replaces. What a mapping does: a parameter is set (a note toggles it between its ends); a hot knob moves the
parameter its knob follows; SELECT and the scene keys fire on a note or on a controller crossing 64 upwards; BYPASS
follows a controller (64 and over is on) or toggles on a note.

Where mappings live. A parameter's mapping is kept with the plug-in (`MAP` children of the `MEMBER`): it follows
the plug-in when the rack is reordered, is saved with the show, and goes with the plug-in when it is removed or
replaced (the CONTROL page says "N mappings went with ..."). The mappings of hot knobs, SELECT, BYPASS and the scene
keys are kept in the show (`<PluginHostControl>` in the show file; `Show::getHostControlXml`). Scenes don't change
any of them. Plug-ins in the sandbox expose their parameters mirrored from the helper, and `setValueNotifyingHost`
reaches the helper through them, so they map like the others (a sandboxed plug-in that exposes none is listed as
offering no parameters).

**The CONTROL page.** Three panels: MIDI DEVICES (the inputs, a tap switches one on; one that is on but missing says
NOT FOUND), MAPPINGS (source -> target, LEARN, MODE, DELETE, CLEAR ALL asking twice, a line saying what LEARN waits for)
and HOT KNOBS. Rows are 48 px, keys 52 px in the design space.

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
| scenes with fade times and safes | per-plug-in safes, whole-rack recall, GLIDE |
| MIDI ports for console controls (`MidiPorts`) | MIDI learn for plug-in parameters, hot knobs |
| user layers, Universal Mixer cards | the drawn unit and its seven pages |
| benchmark | the CPU guard; latency alignment |

## Steps

1. Engine: the profile (16, 32 or 64 channels, input → chain → output, no mix), routing per channel,
   tests. Universal Mixer shows it.
2. The drawn unit and the RACK page with docked windows.
3. ROUTING and sidechains (the engine's key inputs, then the page).
4. SCENES: whole states, safes, crossfade. (Done.)
5. CONTROL: MIDI learn, hot knobs. (Done.)
6. OVERVIEW, BROWSER, SYSTEM; latency alignment; the CPU guard.
7. The Universal Mixer: PLUGIN HOST racks offered on any console's strips.
8. Screenshots, user guide, start-up tile, completion percentage.
