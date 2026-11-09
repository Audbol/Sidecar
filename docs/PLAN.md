# What's left to do

The working list, in the order we plan to do it. Written November 2026; cross things off as
they land. The detail behind each console is in `UNIVERSAL_MIXER.md` and `DLIVE_RESEARCH.md`.

## 1. Save and try what exists

- [ ] Commit the 01V, dLive and fast-build work. It's all uncommitted on one PC.
- [ ] Push the commits. Pushing needs a sign-in as Audbol on this PC.
- [ ] Try test.6 by hand: the 01V's surface first, then the dLive.

## 2. Finish the 01V (while the console is on hand)

- [ ] LCD photos of the remaining pages.
- [x] MEMORY's scene page (RECALL UNDO not yet).
- [x] Pages turn with a repeated key press. EFFECT edit, library and PRE/POST, AUX PRE/POST.
- [x] UTILITY: the oscillator (on the engine, with burst noise and bus priority), preferences.
- [x] OPTION page 2: CH 17-24's routing, sends with PRE/POST, attenuator and 2-band EQ.
- [ ] Pages: MIDI, SETUP (monitor and solo, fader and mute groups, pairing), OPTION's outputs,
      REMOTE, and the other functions' later pages.
- [ ] REMOTE fader mode.
- [ ] Fader groups, [ON] groups, FLIP and CASCADE, 2TR IN, OMNI OUT, CH 17-24's send limits.
- [ ] MIDI: parameter changes, control changes, program changes, bulk.

## 3. Finish the dLive

Structure:

- [x] Mixer Config and IP Stereo Config.
- [x] Each mix's Ext In and Mix Source (Bus mixes, from sockets).
- [ ] Main modes: LR+Msum, LR+M, LCR, LCR+, 5.1, None. PAFL busses.
- [ ] Ext In from mixes and PAFL. Mix Source on the Matrices and Main.

Processing:

- [x] NEQ-12 (the engine's EQ parameter limit grew to 64).
- [ ] Dyn8, 64 engines.
- [x] The two preamp models.
- [x] AMM, NOM and D-Classic.

Effects:

- [x] The 20 RackExtra effects. Ranges and algorithms are largely Sidecar's reading.
- [ ] The RackUltra card: 8 engines, their channel strip, 11 effects. Tuners and harmonisers last.

Console features:

- [ ] MIDI over TCP.
- [ ] Source Selectors, ABCD inputs, Direct Out options, Group to Group, Mute Group override,
      Follow Main Pan, stereo image modes.
- [x] Its own surface, started from photos of a C1500. Its keys and rotaries work; its screen hosts
      the Universal pages.
- [x] The C3500's own surface, the default for a dLive, from photos of a real one: 24 strips in two
      banks, two touchscreens, soft rotaries. The C1500 stays, a key on each switches between them.
- [x] Copy, Paste, Reset, Safes, Freeze and the GEQ on the faders, on both surfaces. The held keys latch.
- [x] The left screen's PEQ page and the right screen's Scene Manager, matched to photos.
- [x] MixRack's Config and Audio tabs, from photos. Dimmed where Sidecar has no counterpart: PAFL and Main
      modes, Network, User Profiles, Audio Sync, Source Selector (the feature itself is still to do).
- [ ] MixRack's Talkback, Mute Groups, SigGen and Controllers tabs, once photographed.
- [ ] The C1500's screen in the same style: it still hosts the Universal pages.
- [ ] The other processing tabs against photos (Gate, Comp, Preamp, Filter, Ins, Delay, Bank, Overview).
      They work, but no photo of them exists yet.
- [ ] The right screen's other pages in the console's own style; they host the Universal pages for now.
- [ ] Scene Manager: Undo Go (needs a recall undo in the engine, the 01V wants it too), Lock, Copy and
      Paste, Copy Filter, Embedded Recall, Custom MIDI, Actions, and the tabs beside Scene Manager.
- [ ] The RTA on the PEQ graph. Help.
- [ ] The rest of the surface: SoftKeys, assigning the custom rotaries, Strip Assign.

## 4. Bring the older surfaces up to the 01V's standard

- [x] ProMix 01's panel and 240 x 64 dot LCD, from the owner's photos. Its LCD shares the 01V's fonts.
- [ ] ProMix 01: check every LCD page against the owner's console.
- [x] 02R's colours, plates and hood, from public photos (no console to photograph).
- [ ] 02R: its LCD as dots, once its resolution is known (the manuals and reviews don't say).

- [x] An accuracy pass over the 01V, ProMix 01, 02R, SQ-5 and X32, against photos and manuals.
- [ ] From that pass, still open: the 02R's LCD is about 2:1 where the real one is 4:3; X32 idle screen-key
      text should be white and its knob pointers lit red; the SQ-5's lit keys should light a slot, not the
      whole key, and its knobs should be fluted; 01V D routing, group boxes, KEYIN and RECALL UNDO are drawn only.

## 5. The Universal Mixer's interface

The owner wants a lot of work done here; what comes first is theirs to say.

- [ ] Go over every view against `UNIVERSAL_MIXER.md` and the screenshots: layout, collisions, consistency.

## 6. The other consoles

- [ ] SQ-5: add-on effects and DEEP models, Group addresses, MIDI over TCP, Soft Keys, Setup and
      Utility pages.
- [ ] WING: Mixing Station write coverage, WING-Q on a real device, the owner's open questions
      (`UNIVERSAL_MIXER.md`, "Still open from the WING work").
- [ ] X32: redo the lost sound measurements, committing tools and results as they're made.

## 7. Release

- [ ] v0.3.0, once the above is as far as we want it for that release.

## 8. Consoles after these

Research is in `NEW_CONSOLES_RESEARCH.md`; documents and photos are outside the repository.

- [ ] Yamaha DM2000.
- [ ] Midas PRO series.
- [ ] Then, as wanted: Midas M32, Yamaha CL, QL, TF and DM7, Allen & Heath Avantis, DiGiCo SD and
      Quantum.

## Only a real console can settle

- dLive: its Surface's fader taper, whether a mix's compressor can go before its PEQ, where AMM's
  gain sits, its manual compressors' attack and release ranges, most effect knob ranges.
- 01V: PAD's exact behaviour in Sidecar's gain structure, the levels of the fader steps.
