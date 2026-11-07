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
    /** The Behringer WING emulation. */
    inline const juce::String wing { "WING" };
} // namespace devices

/** Draws `device` as a small tag at the right of `area` (at its left with
    `atLeft`; nothing for none); returns the width it took, gap included. */
float drawDeviceTag (juce::Graphics&, juce::Rectangle<float> area, const juce::String& device, const Theme&, bool atLeft = false);
/** The room drawDeviceTag takes for `device` (0 for none). */
float deviceTagWidth (const juce::String& device);

/** A fader's travel and a meter's bars leave this much at either end, so a
    meter beside a fader of the same height lines up with its marks. */
constexpr float kTrackInset = 14.0f;

/** A fader in dB on the WING fader law (+10 dB at the top, 0 dB at 3/4, -oo at
    the bottom: ui::fader). */
class FaderControl final : public juce::Component
{
public:
    explicit FaderControl (const Theme&);

    /** The value it moves (nullptr: none, disabled). */
    void bind (std::atomic<float>* db);
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

    /** Drawn as a console's fader (a console surface's hardware look: its slot,
        its moulded cap, its silkscreen scale); null: the Universal Mixer's. */
    const hw::Look* look = nullptr;

private:
    juce::Rectangle<float> track() const noexcept;

    const Theme& theme;
    std::atomic<float>* value = nullptr;
    double position = 0.0, dragStart = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaderControl)
};

/** A level meter: one or two bars on the fader's scale (ui::fader, so the marks
    beside a fader read for its meter too), green to -18 dB, yellow to -6 dB,
    orange to 0 dBFS and red above, with a clip light. */
class MeterControl final : public juce::Component
{
public:
    explicit MeterControl (const Theme&);

    /** The level of side `channel` (linear), and how many sides there are. */
    std::function<float (int channel)> level;
    void setChannels (int numChannels);

    /** Where side `channel` stands now, 0 to 1. */
    float getPosition (int channel) const noexcept { return positions[(size_t) juce::jlimit (0, 1, channel)]; }
    void refresh();

    void paint (juce::Graphics&) override;

private:
    const Theme& theme;
    int channels = 1;
    std::array<float, 2> positions {};

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanControl)
};

/** A key that shows a state: lit in its colour while `isOn`; `isHalfOn` (a
    mute by a group, say) lights only its outline. */
class ToggleKey final : public juce::Button
{
public:
    ToggleKey (const juce::String& label, juce::Colour onColour, const Theme&);

    std::function<bool()> isOn, isHalfOn;
    /** The console the key's page comes from (devices::wing), as a tag in its corner. */
    juce::String device;
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
