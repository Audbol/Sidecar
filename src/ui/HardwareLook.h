#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace livedaw::ui::hw
{

/** How a console's hardware looks (its panel, silkscreen, keys, faders and
    knobs), for the console surfaces that evoke the real one (the X32's, the
    ProMix 01's, the 02R's). Drawn by Sidecar from the consoles' colours and
    shapes; no maker's artwork, logo or photo is used. */
struct Look
{
    // The panel: its surface (top and bottom of a gentle gradient), its frame,
    // the sections' panels and their outlines, the silkscreen.
    juce::Colour panelTop, panelBottom, frame, section, sectionLine, silk, silkDim;
    // Keys: unlit top and bottom, text; lit keys glow in their own colour
    // (`backlit`) or show a small LED above their text.
    juce::Colour keyTop, keyBottom, keyText;
    bool backlit = true;
    // Faders: the slot, the cap and its line; knobs: the cap, and the LED ring
    // of a console whose knobs are endless encoders (the X32's; transparent: none).
    juce::Colour slot, capTop, capBottom, capLine, knob, knobLine, knobRing;
    // The LCD: backlight and pixels.
    juce::Colour lcd, lcdPixel, lcdDim, lcdBezel;
    float keyRadius = 3.0f;
    /** A key's LED (a look that isn't backlit): its size, round where they're equal. */
    float ledWidth = 14.0f, ledHeight = 3.5f;

    /** A fader cap: a moulded block with grip ridges and a centre line (the
        Yamahas'), or a tall brushed-metal cap with a scooped face (the X32's). */
    enum class Cap
    {
        Block,
        Metal
    };
    Cap cap = Cap::Block;

    /** A knob: a plain knurled cap with a pointer line, a dome (the 02R's
        encoders), a big dimpled wheel. */
    enum class Knob
    {
        Plain,
        Dome,
        Wheel
    };
    Knob knobStyle = Knob::Plain;

    /** The Behringer X32's titanium frame, slate sections, light rubber keys. */
    static const Look& x32();
    /** The Yamaha ProMix 01's charcoal panel, cream keys, green LCD. */
    static const Look& promix();
    /** The Yamaha 02R's dark grey panel, cream keys, blue LCD. */
    static const Look& y02r();
};

/** The console's panel over `area`: a slight gradient, a fine brushed texture
    and a lit top edge. */
void panel (juce::Graphics&, juce::Rectangle<float> area, const Look&);
/** Wooden end cheeks (the 02R's), left and right of `area`. */
void cheeks (juce::Graphics&, juce::Rectangle<float> left, juce::Rectangle<float> right);
/** A section of the panel: its own darker panel and silkscreen outline, the
    title in the outline's top left (the X32's CONFIG / PREAMP...). */
void section (juce::Graphics&, juce::Rectangle<float> area, const juce::String& title, const Look&);
/** A silkscreen label. */
void silk (juce::Graphics&, juce::Rectangle<float> area, const juce::String& text, const Look&, float size = 11.0f,
           juce::Justification = juce::Justification::centred, bool dim = false);
/** A rubber key: `lit` glows in `colour` (or lights its LED). */
void key (juce::Graphics&, juce::Rectangle<float> area, const juce::String& text, bool lit, bool down, juce::Colour colour,
          const Look&, juce::Colour face = {}, bool led = true);
/** A fader's slot from `top` to `bottom` at `x`, and its cap centred on `y`
    (`colour`: a coloured cap, the ProMix's ST OUT; transparent: the look's). */
void faderSlot (juce::Graphics&, float x, float top, float bottom, const Look&);
void faderCap (juce::Graphics&, float x, float y, float width, const Look&, juce::Colour colour = {});
/** A knob cap in `area` turned to `fraction` (0..1 over 300 degrees), with an
    LED ring of `ring` around it if it isn't transparent (from the centre if `centred`). */
void knob (juce::Graphics&, juce::Rectangle<float> area, float fraction, const Look&, juce::Colour ring = {}, bool centred = false);
/** A knob of a style other than the look's own, its cap in `colour` (transparent: the look's). */
void knob (juce::Graphics&, juce::Rectangle<float> area, float fraction, const Look&, Look::Knob style, juce::Colour colour,
           juce::Colour ring = {}, bool centred = false);
/** An LCD's bezel and backlight over `area`; returns the screen inside. */
juce::Rectangle<float> lcd (juce::Graphics&, juce::Rectangle<float> area, const Look&);

/** A look-and-feel drawing rotary sliders as the look's knob caps, with its
    LED ring (lit from the centre for a range around zero) unless `ring` is false:
    a console's analog pots (the X32's MONITOR, PHONES, TALK). One per look. */
juce::LookAndFeel& knobLookAndFeel (const Look&, bool ring = true);

/** Gives every console key (universal::ToggleKey), fader (FaderControl) and
    knob (DisplayKnob) under `root` the look, but `except`. */
void applyTo (juce::Component& root, const Look&, const juce::Component* except = nullptr);

} // namespace livedaw::ui::hw
