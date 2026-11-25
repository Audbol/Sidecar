# Copying a console's settings into another (COPY FROM)

The owner's request (November 2026): carry a mix from one console to another, for example
a WING's channels onto a DM2000, without setting it up again by hand. Since issue #25 every
console Sidecar ran is kept in Sidecar's data folder (`consoles/<console>.lshow`); COPY FROM
takes one of those, or any show file, and copies the chosen parts into the running console.

## What the user sees

The Universal Mixer's SETUP page gets **COPY FROM** next to the console choice:

1. **From:** the kept consoles (each with the date it was kept), or **A SHOW FILE...**.
2. **What:** a checklist of the strip sections scenes already use (`console::section`):
   NAME (name, colour, icon), PREAMP (trim, polarity, delay, low and high cut), GATE, EQ,
   DYNAMICS, SENDS, FADER (level, pan, mute), ROUTING (input patch, stereo), GROUPS (DCA and
   mute group membership). NAME, PREAMP, EQ, DYNAMICS, FADER and SENDS are ticked to start with.
   ROUTING is not, because the two consoles may sit on different interfaces.
3. **Which strips:** all of them, or a range (channels 1-24 only, for example). A later step may
   add an offset (the WING's 1-40 onto the DM2000's 41-80).
4. **EQ and dynamics** when the consoles differ: **FIT TO THIS CONSOLE** (the default: the
   console's own EQ, set to sound as close as it can) or **KEEP THEIRS** (the other console's
   EQ block as it was, marked as foreign the way the Universal Mixer already marks it; exact,
   but the console's surface can't edit it on its own pages).
5. **COPY**, then a short report: what was copied, and what had no place (channels past this
   console's count, sends to buses it doesn't have, settings it has no control for).

COPY sets an undo point, so the recall UNDO brings the console back as it was. It is refused while
a real console is under remote control, like the other console changes. Scenes and effects are
never copied: they belong to the console they were made on.

## How it works

The pieces already exist; COPY FROM joins them.

- **Reading the other console:** a scratch `Engine` (no audio, `NoPlugins`) loads the source's
  `<Console>` with `load_console`, then `capture(engine, sections, None, ...)` gives a `<State>`
  of the chosen sections, the same element a scene stores.
- **Matching strips:** strips match by kind and number (channel 5 to channel 5, bus 3 to bus 3,
  main 1 to main 1). A strip the running console lacks is skipped and reported. Kinds with no
  counterpart (a WING's matrices on a 01V) are reported once, not strip by strip.
- **Applying:** `apply` with the chosen sections, as a scene recall does, onto the running
  console's strips. Sends to a bus number the running console lacks are dropped and reported.
  Input patching copies port numbers as they are.
- **EQ, FIT TO THIS CONSOLE:** a general curve fit, so no table per pair of consoles is
  needed. The source EQ's response is measured with its own `design()` (`EqDesign::response_db`)
  at about 60 frequencies, log-spaced from 20 Hz to 20 kHz. The target model's parameters are
  then searched to match it: each band placed from the source's peaks and shelves first, then
  coordinate descent on frequency, gain and Q within each parameter's range. Switch and shape
  parameters are tried value by value. The fit's worst error in dB is kept for the report
  ("EQ within 0.8 dB"). The same code fits any model to any other, the WING's character EQs
  included, and it runs on the message thread (tens of milliseconds for a full console).
- **Dynamics and gate:** fitted by meaning, not by curve. Each model's parameters are matched by
  label (threshold, ratio, attack, release, hold, knee, range, make-up gain), with units converted
  (ms and s, ratio as a number or as a list position) and values clamped to the target's range.
  Parameters with no counterpart keep the target's defaults and are named in the report.
- **Low and high cut:** in PREAMP, frequency and slope, the slope rounded to the target's nearest.

## Steps

1. **Session layer (Rust).** `copy_console(engine, source, plan) -> CopyReport` in
   `livedaw-session`: scratch engine, capture, strip matching, apply, report. Same-console copies
   first, with KEEP THEIRS for EQ and dynamics. Tests: WING to DM2000 (names, faders, sends 1-8
   kept, sends 9-16 reported), DM2000 to WING, a copy of channels 1-8 only, and a copy UNDO
   reverts.
2. **App.** FFI and `Show::copyFrom`; the SETUP page's COPY FROM window (source, checklist, strip
   range, EQ choice, report); the undo point; refused under remote control. CHANGELOG and
   USER_GUIDE.
3. **EQ fitting.** The curve fit in `livedaw-dsp`, with tests that fit every console's channel EQ
   to every other's on a few typical curves (a low shelf and two peaks; a high-pass and a
   presence lift). The test passes when the worst error stays within an agreed limit, about
   1 dB where the target has enough bands. FIT TO THIS CONSOLE becomes the default.
4. **Dynamics and gate mapping.** Label matching and unit conversion across the consoles'
   dynamics and gate models; a test per console pair checks that threshold, ratio and timing land
   where expected.
5. **Later, if wanted:** a strip offset, copying a single channel from another console from the
   strip's own menu, and COPY FROM on the consoles' own SETUP pages.

## Open questions for the owner

- Should COPY FROM also take any show file (recommended, it costs nothing extra), or only kept
  consoles?
- The fit's limit for the tests: 1 dB is proposed.
- Are mains copied by default? A WING's main 1 is a DM2000's STEREO, but their sends differ.
