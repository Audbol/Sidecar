#pragma once

#include "ui/universal/Theme.h"

#include <array>
#include <atomic>
#include <functional>

namespace livedaw::ui::hw
{
struct Look;
}

namespace livedaw::ui::universal
{

/** The Universal Mixer's controls. Each is bound to an engine value and drawn
    with the theme's tokens; refresh() takes changes made elsewhere (another
    view, a scene, a remote) and repaints only what moved. Touch first: a drag
    moves a control by how far it travels (no jump to the touch), a double tap
    resets it, and a drag on one doesn't scroll the view it sits in. */

/** The console a part of the interface comes from (docs/UNIVERSAL_MIXER.md):
    the Universal Mixer will hold the emulations of several consoles, so a part
    that one of them defines (its blocks and process orders, its model tables
    and front panels, its port groups, its lists) shows that console's name as
    a tag. The Universal core's own parts (extra blocks, user taps, the patch
    matrix) have none. */
namespace devices
{
    /** The console the screens show (the shown profile), named as a tag: WING, X32, DM2000 and so on;
        none on the Universal Mixer's own profile, whose blocks are its own. */
    juce::String shown();

    /** The tag of a block holding model `token`: the console the model comes from when it is not the
        shown console's (a WING EQ on a ProMix: "WING"; another console's model on the Universal
        Mixer: its console), else `fallback` (the shown console, or none for the core's own blocks).
        `foreign`: the model is another console's on the shown one (models::Model::foreign). */
    juce::String of (const juce::String& token, bool foreign, const juce::String& fallback);
} // namespace devices

/** Draws `device` as a small tag at the right of `area` (at its left with
    `atLeft`; nothing for none); returns the width it took, gap included. */
float drawDeviceTag (juce::Graphics&, juce::Rectangle<float> area, const juce::String& device, const Theme&, bool atLeft = false);
/** The room drawDeviceTag takes for `device` (0 for none). */
float deviceTagWidth (const juce::String& device);

/** The look of what the views don't draw themselves (pop-up menus, dialogs, call-outs, scroll
    bars, text fields), in `theme`. Hand it to a PopupMenu: a menu doesn't take its target's. */
juce::LookAndFeel& lookAndFeelFor (const Theme& theme);

/** A brushed metal plate, its grain tiled over `top` to `bottom`; `screws`: one in each corner. */
void drawPlate (juce::Graphics&, juce::Rectangle<float>, juce::Colour top, juce::Colour bottom, const Theme&, bool screws = false);
/** A legend engraved in a plate. */
void drawEngraved (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, const Theme&, bool bold = true);
/** A readout lit from behind. */
void drawGlowText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, juce::Colour, const Theme&);
/** Is the input in use a finger (the small controls then open large)? */
bool isTouchInput();

/** A knob with its ring: `position` 0 to 1, the ring lit from the centre (`fromCentre`) or the start. */
void drawKnob (juce::Graphics&, juce::Rectangle<float> area, float position, bool fromCentre, const Theme&, bool enabled);

/** Open the inspector's SENDS page instead of a block of the chain (a step kind for the views' onOpenStep). */
constexpr int kOpenSends = -1;

/** A fader's travel and a meter's bars leave this much at either end, so a
    meter beside a fader of the same height lines up with its marks. */
constexpr float kTrackInset = 14.0f;
/** The same for the Universal Mixer's own fader: its taller cap stays inside the control. */
constexpr float kCapInset = 30.0f;

/** Whether a finger on a control is moving it or scrolling the strips: decided by its first
    few pixels, so that sweeping along a row of faders moves none of them. */
struct TouchGesture
{
    enum class Mode
    {
        Undecided,
        Value,
        Scroll
    };
    Mode mode = Mode::Undecided;
    juce::Component::SafePointer<juce::Viewport> view;
    juce::Point<int> viewStart;

    /** With a touch's drag: true when the control should take this event as a move. */
    bool update (juce::Component& control, const juce::MouseEvent&);
};

/** A fader in dB on the WING fader law (+10 dB at the top, 0 dB at 3/4, -oo at
    the bottom: ui::fader). */
class FaderControl final : public juce::Component
{
public:
    explicit FaderControl (const Theme&);

    /** The value it moves (nullptr: none, disabled). */
    void bind (std::atomic<float>* db);
    /** Moves a value from `min` to `max` over the whole travel instead (the SQ's GEQ on the faders). */
    void bindLinear (std::atomic<float>* value, float min, float max);
    /** 0 (-oo) to 1 (+10 dB). */
    double getPosition() const noexcept { return position; }
    /** Moves the fader as the user would: writes the value. */
    void setPositionFromUser (double newPosition);
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** How far the groove's centre is from the right edge. */
    float grooveFromRight = 18.0f;
    /** The level's colour when it isn't the accent (a fader moving a send); transparent: the accent. */
    juce::Colour tint;
    /** The selected strip's: the cap's index line in the accent. */
    bool emphasised = false;
    /** Drawn as a console's fader (a console surface's hardware look: its slot,
        its moulded cap, its silkscreen scale); null: the Universal Mixer's. */
    const hw::Look* look = nullptr;

private:
    juce::Rectangle<float> track() const noexcept;

    const Theme& theme;
    std::atomic<float>* value = nullptr;
    double position = 0.0;
    int lastDragY = 0;
    TouchGesture gesture;
    float linearMin = 0.0f, linearMax = 0.0f; // equal: the fader law
    double toPosition (float value) const noexcept;
    float fromPosition (double pos) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaderControl)
};

/** A level meter: one or two segmented bars on the fader's scale (ui::fader, so
    the marks beside a fader read for its meter too), green to -18 dB, yellow to
    -6 dB, orange to 0 dBFS and red above, with a held peak and a clip light that
    stays on for two seconds. */
class MeterControl final : public juce::Component
{
public:
    static constexpr int kMaxChannels = 8;

    explicit MeterControl (const Theme&);
    /** Its bars leave this much at either end. */
    float inset = kCapInset;

    /** The level of side `channel` (linear), and how many sides there are. */
    std::function<float (int channel)> level;
    void setChannels (int numChannels);

    /** Where side `channel` stands now, 0 to 1. */
    float getPosition (int channel) const noexcept { return positions[(size_t) juce::jlimit (0, kMaxChannels - 1, channel)]; }
    void refresh();

    void paint (juce::Graphics&) override;

private:
    const Theme& theme;
    int channels = 1;
    std::array<float, kMaxChannels> positions {}, peaks {};
    std::array<juce::uint32, kMaxChannels> peakAt {}, clipUntil {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterControl)
};

/** A pan (or balance) bar, -100 (left) to 100 (right), filled from the centre. */
class PanControl final : public juce::Component
{
public:
    explicit PanControl (const Theme&);

    void bind (std::atomic<float>* pan);
    float getPan() const noexcept { return shown; }
    void setPanFromUser (float newPan);
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    const Theme& theme;
    std::atomic<float>* value = nullptr;
    float shown = 0.0f, dragStart = 0.0f;
    int dragBase = 0;
    TouchGesture gesture;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanControl)
};

/** A key that shows a state: lit in its colour while `isOn`; `isHalfOn` (a
    mute by a group, say) lights only its outline. */
class ToggleKey final : public juce::Button
{
public:
    ToggleKey (const juce::String& label, juce::Colour onColour, const Theme&);

    std::function<bool()> isOn, isHalfOn;
    /** The console the key's page comes from (devices::shown()), as a tag in its corner. */
    juce::String device;
    /** A page tab (the Universal Mixer's): no key face, the name dim, a line under it while on. */
    bool tab = false;
    /** Drawn as a console's rubber key (a console surface's hardware look), on
        `face` if it has its own colour; null: the Universal Mixer's flat key. */
    const hw::Look* look = nullptr;
    juce::Colour face;
    /** With a look: the key blank, its name printed on the panel under it (the
        X32's keys), `belowText` if set, else its text. */
    bool labelBelow = false;
    juce::String belowText;
    /** With a look: the key blank, its name (`belowText` if set) printed and
        underlined on the panel above it (the ProMix 01's function keys). */
    bool labelAbove = false;
    /** With labelAbove: its colour, if not the panel's silkscreen (a label on a light plate). */
    juce::Colour labelColour;
    /** With a look whose keys have LEDs: false for a key without one. */
    bool showLed = true;
    /** With a look: the key's outline (the ProMix 01's oval ENTER, its
        triangular cursor keys); its text isn't drawn on a shaped key. */
    enum class Shape
    {
        Rect,
        Oval,
        Up,
        Down,
        Left,
        Right
    };
    Shape shape = Shape::Rect;
    /** An oval's corners, if not a full half of its height (the SQ's rounded oblong keys). */
    float cornerRadius = -1.0f;
    /** With a backlit look, on a shaped key: lit, a small window in the cap glows, not the
        whole cap (the SQ's keys: a slot in the rubber over the LED). */
    bool windowLit = false;
    /** With a look: a black key whose name glows in its colour (the X32's
        screen keys), brighter while on. */
    bool textLit = false;
    void refresh();

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    const Theme& theme;
    juce::Colour colour;
    bool lit = false, halfLit = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToggleKey)
};

//==============================================================================
/** A console's data wheel (the ProMix 01's PARAMETER wheel, the 02R's wheel): an
    endless rotary that counts steps. Its value only says how far it has turned;
    the surface takes the difference (onValueChange) and draws it (paint): the
    slider itself is only its touch. A drag turns it a step per so many pixels,
    a click of the mouse wheel a step. */
class DataWheel final : public juce::Slider
{
public:
    static constexpr int kPixelsPerStep = 4;

    /** `pixelsPerStep`: how far a drag goes for a step (the ProMix 01's wheel is slower). */
    explicit DataWheel (int pixelsPerStep = kPixelsPerStep);

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DataWheel)
};

} // namespace livedaw::ui::universal
