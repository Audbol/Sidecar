# Adding an analog mixer

An analog mixer has no scene memory, no channel names and no network. Sidecar
gives it all three: a scenes bar over the panel, a strip of console tape under
each channel, and a routing page that patches the audio interface to it. The
first analog mixer is the XENYX Q1202USB (profile `Xenyx1202`, surface
`Xenyx1202Surface`); this guide uses it as the example. The parts every analog
mixer shares live in `AnalogSurface` (`src/ui/AnalogSurface.*`), so a new mixer
needs its profile, its panel drawing, its control layout and its routing labels,
and little else.

## 1. The profile and the blocks (Rust)

The engine side is the same as for any console.

1. `rust/crates/livedaw-dsp/src/<mixer>.rs`: the mixer's blocks (preamp gain
   scale, EQ, compressor, soft clip) and their `MODELS`, as
   `xenyx1202.rs` does. Say plainly in the comments what was measured and what is
   Sidecar's own reading.
2. `rust/crates/livedaw-engine/src/profile.rs`: a `Profile` variant, its name,
   `Counts` (how many mono and stereo channels, aux, buses, mains), the channel
   chain templates, the fader laws (the panel's printed scales), the port groups,
   the palette and labels, as the `XENYX1202` spec does. Mixers have no remotes
   (`Remotes { midi: None, network: &[] }`).
3. The FFI id in `livedaw-ffi/src/lib.rs` and `include/livedaw_engine.h`
   (`kProfileXenyx1202`), and the C++ `InterfaceKind`.
4. Rust tests next to the other profile tests: the counts, the laws, the
   blocks' behaviour.

What the engine does not know (sends live apart from the strip parameters, so a
mixer's fixed routing, such as "the FX knob is a post-level send to the one FX
bus") is set by the surface once on a new console; see `ensureRouting` in
`Xenyx1202Surface.cpp`, which keeps its "done" flag in a strip light the mixer
has no use for, so a scene keeps it.

## 2. The surface (C++)

Derive from `AnalogSurface` and supply four things.

```cpp
class MyMixerSurface final : public AnalogSurface
{
public:
    explicit MyMixerSurface (UiContext&);   // build() the controls, then start()
    ~MyMixerSurface() override;             // stopRefresh()
    void paintPanel (juce::Graphics&) override;   // the panel, in its own pixels

private:
    const hw::Look& panelLook() const override;   // the look its controls are drawn in
    void bindControls() override;                 // what each control does in the engine
    AnalogRoutingSpec routingSpec() override;     // the routing page's rows and columns
    void repaintLive() override;                  // optional: LEDs and meters
};
```

- **Panel drawing** (`paintPanel`): draw the whole panel in its own pixel space
  (the size you pass to the base constructor; the Q1202USB's is the
  photograph's). The base scales it to fit under the bar. Draw from the owner's
  photographs, in shapes and colours, with no maker's name, artwork or logo.
- **Control layout** (the constructor): `addKnob`, `addKey` and `addFader` make a
  control at a rectangle of the panel and give it a component ID (`"CH1 GAIN"`);
  `addTape (channel, seed, id, bounds)` puts console tape under a channel. The
  base owns them all. Jacks that are only pressed are `analog::Hotspot`
  members of your surface, parented to `getPanel()`. End the constructor with
  `start()`, which binds, fills the bar and starts the 30 Hz refresh; call
  `stopRefresh()` in the destructor.
- **Strip mapping** (`bindControls`): called at every `refreshAll`. Find each control
  with `findControl (id)` and give it a `KnobBinding` (knob and fader) or
  `isOn` and `onClick` (key). A binding reads and writes the strip's value
  through a lookup made at each call (`channel (i)`, `aux (i)`, `bus (i)`,
  `main()`), so a console that changes under the surface leaves the control idle
  instead of dangling. The tapes are bound by `addTape` already.
- **Routing labels** (`routingSpec`): the rows of the INPUTS grid (strips; a
  stereo return such as 2-TRACK has `stereoReturn` set and a name of its own),
  the columns of the OUTPUTS grid (a name and the strip's output port), and the
  line under the page's title for each.

## 3. What the template gives for free

- The scenes bar: PREV, NEXT, STORE, NEW and the scene list, working through the
  show (`storeScene`, `newScene`, `recallSelected`), kept in step with the show.
- Console tape under each channel with the name stored in the strip (so it is
  saved with the show), edited by a click.
- The panel host, scaled to fit under the bar at any window size.
- The control kinds `analog::Knob`, `Key` and `Fader` (drag, wheel, double-click
  to reset), their `KnobBinding`s and their refresh on every tick and every
  `refreshAll`.
- The ROUTING page over the panel: the interface's inputs to the channels and
  the mix's outputs to the interface, as patch grids.
- The UNIVERSAL MIXER key.
- `findControl` by component ID, `getTape`, `getRoutingGrid` and the other
  accessors the tests use.
- Ownership: controls and tapes are held in `std::vector<std::unique_ptr<...>>`;
  nothing is deleted by hand.

There is no maximise on the Q1202USB (the window is the panel), and so none in
the template.

## 4. Checklist

- [ ] Profile, blocks and tests in Rust (`cargo test -j 4 --workspace`).
- [ ] `InterfaceKind` in `UiContext.h`, the saved name and the profile mapping in
      `MainComponent.cpp` (look at every place `Xenyx1202` appears).
- [ ] Chooser tile: `MixerChooser.cpp` (label, picture in `resources/chooser`,
      listed in `CMakeLists.txt`).
- [ ] The surface on `AnalogSurface`, in `CMakeLists.txt`'s UI sources.
- [ ] Start-up notice: a line and a completion percentage in `StartupNotice.cpp`.
- [ ] Theme: the console look in `universal/Theme.cpp`, so the Universal Mixer
      wears the mixer's colours.
- [ ] Tests: the controls act on the engine, the tape names a strip, scenes
      recall, the routing page patches (copy the Q1202USB's in
      `tests/UniversalUiTests.cpp`); the surface in `tests/ScreenshotTests.cpp`.
- [ ] Render: run the screenshots (`LIVEDAW_SCREENSHOTS=<folder>`), look at the
      panel and the routing page against the photographs.
- [ ] `docs/PLAN.md` and `CHANGELOG.md`; the user guide's console list.
