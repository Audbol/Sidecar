// The DM2000's panel. Laid out from a straight-on photograph of a real console and the V2 Owner's
// Manual's control-surface chapter (pp.20-44), unkeystoned by hand; colours and shapes from close-up
// photographs. Sidecar's own drawing: no maker's logo or artwork.

#include "ui/Dm2000Surface.h"

#include "daw/Session.h"
#include "engine/Dm2000Midi.h"
#include "engine/MixerEngine.h"
#include "engine/Models.h"
#include "engine/Show.h"
#include "ui/DotFont.h"
#include "ui/HardwareLook.h"
#include "ui/MeterBank.h"

#include <cmath>

namespace livedaw::ui
{

using namespace universal;
using namespace dm2k;

namespace
{
    // The panel's colours, from the photographs.
    const juce::Colour kPanel (0xff232a3a), kPanelLow (0xff1d2331), kBlack (0xff17181c), kBlueTop (0xff2c5cb0), kBlueLow (0xff24509a), kGrey (0xff8c94a1), kGreyDark (0xff757d8a), kLine (0xff5c677a);
    const juce::Colour kSilk (0xffdde2ea), kSilkDim (0xff9aa3b2), kSilkDark (0xff14161b), kWood (0xff5d3320), kWoodLight (0xff7d4a30);
    const juce::Colour kVfdGlass (0xff2a1014), kVfdGlassEdge (0xff120708), kWriteStrip (0xffe8e4d6);
    const juce::Colour kKeySel (0xffbfd1d4), kKeySolo (0xff8a8f94), kKeyOn (0xffcfc6a8), kKeyDark (0xff3b3d43), kKeyGrey (0xff9b9f9f), kDiamond (0xffc4b896), kKeyCream (0xffc9c0a6);
    const juce::Colour kLedYellow (0xffffe23a), kLedGreen (0xff6dff4a), kLedOrange (0xffffa52c), kLedRed (0xffff4030), kLedClear (0x00000000);
    const juce::Colour kSkirtTaupe (0xff8d866f), kTopTaupe (0xffaba490), kSkirtDark (0xff2f3136), kTopDark (0xff565a60), kSkirtTeal (0xff37433f), kTopTeal (0xff6fa79a), kRingTeal (0xff5cc0ad), kSkirtEnc (0xff3f4d4d), kTopEnc (0xff8aa4a6);

    constexpr int kTopBar = 40;
    constexpr int kHaTop = 40, kCenterTop = 146, kCenterBottom = 520, kVfdY = 704, kVfdH = 62, kWriteY = 774, kFaderY = 792, kFaderH = 284;
    constexpr int kLcdX = 529, kLcdY = 160, kLcdW = 400, kLcdH = 300;
    constexpr int kSelX = 961, kSelW = 539;

    constexpr int kAuxSlot = 8;

    int stripCx (int i) { return 108 + (i % 8) * 49 + (i / 8) * 445; }
    float haCx (int i) { return 108.0f + (float) i * 53.6f; }

    juce::Rectangle<int> centred (int cx, int y, int w, int h) { return { cx - w / 2, y, w, h }; }
} // namespace

namespace dm2k
{
    //==========================================================================
    Key::Key (const juce::String& name, juce::Colour f, juce::Colour l, Shape s) : juce::Button (name), face (f), led (l), shape (s) {}

    void Key::refresh()
    {
        const bool on = isOn && isOn();
        if (on != shownOn)
        {
            shownOn = on;
            repaint();
        }
    }

    void Key::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        const auto area = getLocalBounds().toFloat();
        if (shape == Shape::Diamond)
        {
            juce::Path d;
            const auto c = area.getCentre();
            const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 1.0f;
            d.addQuadrilateral (c.x, c.y - r, c.x + r, c.y, c.x, c.y + r, c.x - r, c.y);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillPath (d, juce::AffineTransform::translation (0.0f, 1.5f));
            const auto top = down ? face.darker (0.35f) : face;
            g.setGradientFill (juce::ColourGradient (top.brighter (0.25f), c.x, c.y - r, top.darker (0.25f), c.x, c.y + r, false));
            g.fillPath (d);
            g.setColour (juce::Colours::white.withAlpha (0.3f));
            g.strokePath (d, juce::PathStrokeType (0.7f));
            if (highlighted)
            {
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.fillPath (d);
            }
            return;
        }
        const float radius = shape == Shape::Big ? 4.0f : 2.5f;
        // The well, then the key's sloped skirt and flat top.
        g.setColour (juce::Colour (0xff0d0e11));
        g.fillRoundedRectangle (area, radius);
        auto body = area.reduced (1.5f);
        if (down)
            body = body.translated (0.0f, 1.0f);
        g.setGradientFill (juce::ColourGradient (face.brighter (0.28f), 0.0f, body.getY(), face.darker (0.45f), 0.0f, body.getBottom(), false));
        g.fillRoundedRectangle (body, radius);
        auto top = body.reduced (shape == Shape::Big ? 4.0f : 2.5f, shape == Shape::Big ? 3.0f : 2.0f).withTrimmedBottom (1.5f);
        g.setGradientFill (juce::ColourGradient (face.brighter (0.1f), 0.0f, top.getY(), face.darker (0.08f), 0.0f, top.getBottom(), false));
        g.fillRoundedRectangle (top, radius * 0.6f);

        if (! led.isTransparent())
        {
            const float w = shape == Shape::Square ? body.getWidth() * 0.62f : body.getWidth() * 0.46f, h = shape == Shape::Block ? 4.5f : 3.5f;
            const juce::Rectangle<float> slit (body.getCentreX() - w * 0.5f, body.getY() + (shape == Shape::Square ? 4.0f : 3.5f), w, h);
            if (shownOn)
            {
                g.setColour (led.withAlpha (0.33f));
                g.fillRoundedRectangle (slit.expanded (4.0f, 3.0f), 3.0f);
                g.setColour (led);
                g.fillRoundedRectangle (slit, 1.2f);
                g.setColour (juce::Colours::white.withAlpha (0.65f));
                g.fillRoundedRectangle (slit.reduced (w * 0.2f, 1.1f), 0.6f);
            }
            else
            {
                g.setColour (face.darker (0.6f).withMultipliedSaturation (0.4f));
                g.fillRoundedRectangle (slit, 1.2f);
            }
        }
        if (glyph.isNotEmpty())
        {
            g.setColour (kSilkDark);
            g.setFont (type::font (body.getHeight() * 0.42f, true));
            g.drawText (glyph, body, juce::Justification::centred, false);
        }
        if (highlighted)
        {
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRoundedRectangle (body, radius);
        }
    }

    //==========================================================================
    Knob::Knob (juce::Colour s, juce::Colour t, juce::Colour r) : skirt (s), top (t), ring (r) {}

    void Knob::setBinding (KnobBinding b)
    {
        binding = std::move (b);
        shown = -1.0f;
        repaint();
    }

    void Knob::refresh()
    {
        const float v = binding.isValid() ? binding.get() : 0.5f;
        if (std::abs (v - shown) > 0.001f)
        {
            shown = v;
            repaint();
        }
    }

    void Knob::paint (juce::Graphics& g)
    {
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const auto c = area.getCentre();
        const float position = shown < 0.0f ? 0.5f : shown;
        const float angle = (position - 0.5f) * juce::MathConstants<float>::pi * 5.0f / 3.0f;

        g.setColour (juce::Colours::black.withAlpha (0.42f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c.translated (1.5f, 2.5f)));
        if (! ring.isTransparent())
        {
            g.setColour (ring);
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        }
        const float skirtR = r * (ring.isTransparent() ? 0.98f : 0.86f);
        g.setGradientFill (juce::ColourGradient (skirt.brighter (0.3f), c.x - skirtR * 0.6f, c.y - skirtR * 0.8f, skirt.darker (0.4f), c.x + skirtR * 0.6f, c.y + skirtR * 0.8f, false));
        g.fillEllipse (juce::Rectangle<float> (skirtR * 2.0f, skirtR * 2.0f).withCentre (c));
        // The knurling around the skirt.
        g.setColour (skirt.darker (0.55f));
        for (int i = 0; i < 28; ++i)
        {
            const float a = (float) i * juce::MathConstants<float>::twoPi / 28.0f;
            g.drawLine (juce::Line<float> (c.getPointOnCircumference (skirtR * 0.8f, a), c.getPointOnCircumference (skirtR, a)), 1.0f);
        }
        // The top.
        const float topR = skirtR * 0.62f;
        g.setGradientFill (juce::ColourGradient (top.brighter (0.28f), c.x - topR * 0.5f, c.y - topR * 0.8f, top.darker (0.25f), c.x + topR * 0.5f, c.y + topR * 0.8f, false));
        g.fillEllipse (juce::Rectangle<float> (topR * 2.0f, topR * 2.0f).withCentre (c));
        g.setColour (juce::Colour (0xfff1ead8));
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (topR * 0.35f, angle), c.getPointOnCircumference (skirtR * 0.96f, angle)), juce::jmax (1.5f, r * 0.13f));
    }

    void Knob::moveTo (float p)
    {
        if (binding.isValid() && binding.set)
        {
            binding.set (juce::jlimit (0.0f, 1.0f, p));
            if (onTouch)
                onTouch();
        }
    }

    void Knob::mouseDown (const juce::MouseEvent&)
    {
        dragStart = binding.isValid() ? binding.get() : 0.0f;
        dragged = false;
    }

    void Knob::mouseDrag (const juce::MouseEvent& e)
    {
        if (e.getDistanceFromDragStart() > 2)
            dragged = true;
        if (dragged)
            moveTo (dragStart - (float) e.getDistanceFromDragStartY() / 220.0f);
    }

    void Knob::mouseUp (const juce::MouseEvent&)
    {
        if (! dragged && onPress)
            onPress();
    }

    void Knob::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (binding.isValid() && binding.reset)
        {
            binding.reset();
            if (onTouch)
                onTouch();
        }
    }

    void Knob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (binding.isValid())
        {
            const float step = binding.steps > 1.0f ? 1.0f / (binding.steps - 1.0f) : 0.01f;
            moveTo (binding.get() + (w.deltaY > 0 ? step : -step));
        }
    }

    //==========================================================================
    void Wheel::paint (juce::Graphics& g)
    {
        const auto area = getLocalBounds().toFloat().reduced (2.0f);
        const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const auto c = area.getCentre();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c.translated (2.5f, 4.0f)));
        juce::ColourGradient metal (juce::Colour (0xffe6e4dd), c.x - r * 0.7f, c.y - r * 0.8f, juce::Colour (0xffa9a69b), c.x + r * 0.7f, c.y + r * 0.8f, false);
        metal.addColour (0.5, juce::Colour (0xffd2d0c7));
        g.setGradientFill (metal);
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        g.setColour (juce::Colours::black.withAlpha (0.14f));
        for (int i = 0; i < 60; ++i)
        {
            const float a = (float) i * juce::MathConstants<float>::twoPi / 60.0f;
            g.drawLine (juce::Line<float> (c.getPointOnCircumference (r * 0.94f, a), c.getPointOnCircumference (r, a)), 1.0f);
        }
        const auto dimple = c.getPointOnCircumference (r * 0.56f, angle);
        const float dr = r * 0.24f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8a877c), dimple.x, dimple.y - dr, juce::Colour (0xfff1efe7), dimple.x, dimple.y + dr, false));
        g.fillEllipse (juce::Rectangle<float> (dr * 2.0f, dr * 2.0f).withCentre (dimple));
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawEllipse (juce::Rectangle<float> (dr * 2.0f, dr * 2.0f).withCentre (dimple), 1.0f);
    }

    void Wheel::mouseDown (const juce::MouseEvent& e)
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        last = c.getAngleToPoint (e.position);
        pending = 0.0f;
    }

    void Wheel::mouseDrag (const juce::MouseEvent& e)
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        const float a = c.getAngleToPoint (e.position);
        float d = a - last;
        if (d > juce::MathConstants<float>::pi)
            d -= juce::MathConstants<float>::twoPi;
        if (d < -juce::MathConstants<float>::pi)
            d += juce::MathConstants<float>::twoPi;
        last = a;
        angle += d;
        pending += d;
        const int clicks = (int) (pending / 0.14f);
        if (clicks != 0)
        {
            pending -= (float) clicks * 0.14f;
            if (onTurn)
                onTurn (clicks);
        }
        repaint();
    }

    void Wheel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        angle += w.deltaY > 0 ? 0.14f : -0.14f;
        if (onTurn)
            onTurn (w.deltaY > 0 ? 1 : -1);
        repaint();
    }

    //==========================================================================
    const std::vector<std::pair<float, float>>& printedLegend (bool master)
    {
        // Read off a straight-on photograph of a real console (the lines' places between the top line
        // and the -oo line, to about half a percent of the travel).
        // Yamaha's 10-bit fader table, which the fader
        // law follows, is straight from +10 to -20 dB, so a level sits lower on the legend than its
        // number from 10 down: -20 dB by the law is where the panel prints 15, -30 dB where it prints 20.
        static const std::vector<std::pair<float, float>> channel { { 1.0f, 10.0f }, { 0.904f, 5.0f }, { 0.809f, 0.0f }, { 0.708f, -5.0f }, { 0.570f, -10.0f }, { 0.427f, -15.0f }, { 0.320f, -20.0f }, { 0.180f, -30.0f }, { 0.097f, -40.0f }, { 0.051f, -50.0f } };
        static const std::vector<std::pair<float, float>> output { { 1.0f, 0.0f }, { 0.906f, -5.0f }, { 0.796f, -10.0f }, { 0.697f, -15.0f }, { 0.587f, -20.0f }, { 0.425f, -30.0f }, { 0.301f, -40.0f }, { 0.195f, -50.0f }, { 0.121f, -60.0f }, { 0.091f, -70.0f } };
        return master ? output : channel;
    }

    Fader::Fader (bool m, const FaderLaw& l) : master (m), law (l) {}

    void Fader::bind (std::atomic<float>* v, const FaderLaw& l)
    {
        value = v;
        law = l;
        refresh();
    }

    void Fader::refresh()
    {
        const double p = value != nullptr ? (double) law.toPosition (value->load()) : 0.0;
        if (std::abs (p - position) > 0.0005)
        {
            position = p;
            repaint();
        }
    }

    void Fader::setPositionFromUser (double p)
    {
        if (value == nullptr)
            return;
        position = juce::jlimit (0.0, 1.0, p);
        *value = law.toDb ((float) position);
        repaint();
        if (onTouch)
            onTouch();
    }

    juce::Rectangle<float> Fader::track() const noexcept
    {
        return getLocalBounds().toFloat().withTrimmedTop (16.0f).withTrimmedBottom (34.0f);
    }

    void Fader::paint (juce::Graphics& g)
    {
        const auto t = track();
        const float w = (float) getWidth(), cx = w * 0.5f + (master ? 7.0f : 3.0f);
        const auto infinity = juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e"));
        // A legend: each level's line with its number, where the panel prints it (printedLegend). A
        // channel strip has its own, bold, left of the slot with the numbers over the lines, and the
        // Output Channels', dimmer, right of it; the STEREO fader has lines both sides, numbers left.
        const auto legend = [&] (bool output, bool left, juce::Colour colour, float size, bool bold)
        {
            g.setFont (type::font (size, bold));
            const auto mark = [&] (float position, const juce::String& text)
            {
                const float y = t.getBottom() - t.getHeight() * position;
                g.setColour (colour);
                if (master)
                {
                    g.fillRect (15.0f, y - 0.5f, cx - 19.0f, 1.0f);
                    g.fillRect (cx + 4.0f, y - 0.5f, 13.0f, 1.0f);
                    g.setColour (kSilk);
                    g.drawText (text, juce::Rectangle<float> (0.0f, y - 5.0f, 13.5f, 10.0f), juce::Justification::centredRight, false);
                    return;
                }
                const float x0 = left ? 1.0f : cx + 4.0f, x1 = left ? cx - 4.0f : w - 1.0f;
                g.fillRect (x0, y - 0.5f, x1 - x0, 1.0f);
                g.drawText (text, juce::Rectangle<float> (left ? 1.0f : w - 19.0f, y - size - 0.5f, 18.0f, size), left ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
            };
            for (const auto& [position, db] : printedLegend (output))
                mark (position, db >= 10.0f ? juce::String ("+10") : juce::String (juce::roundToInt (std::abs (db))));
            mark (0.0f, infinity);
        };
        if (master)
            legend (true, true, kSilkDark.withAlpha (0.8f), 9.0f, false);
        else
        {
            legend (true, false, kSilkDim.withAlpha (0.7f), 7.0f, false);
            legend (false, true, kSilk.withAlpha (0.9f), 8.5f, true);
        }

        // The slot, then the cap: chromed, with its finger ledge white.
        g.setColour (juce::Colour (0xff08090b));
        g.fillRoundedRectangle (juce::Rectangle<float> (cx - 2.5f, t.getY() - 6.0f, 5.0f, t.getHeight() + 14.0f), 2.0f);
        const float y = t.getBottom() - t.getHeight() * (float) position;
        hw::ribbedFaderCap (g, cx, y, 13.0f, 17.0f, 9.0f, juce::Colour (0xffb4b7bb));
        g.setColour (juce::Colour (0xfff1f1ec));
        g.fillRoundedRectangle (juce::Rectangle<float> (cx - 13.0f, y + 7.0f, 26.0f, 9.0f), 1.5f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (cx - 13.0f, y - 0.5f, 26.0f, 1.0f);
    }

    void Fader::mouseDown (const juce::MouseEvent&) { dragStart = position; }

    void Fader::mouseDrag (const juce::MouseEvent& e)
    {
        setPositionFromUser (dragStart - (double) e.getDistanceFromDragStartY() / (double) track().getHeight());
    }

    void Fader::mouseDoubleClick (const juce::MouseEvent&) { setPositionFromUser ((double) law.toPosition (0.0f)); }

    void Fader::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        setPositionFromUser (position + (w.deltaY > 0 ? 0.0125 : -0.0125));
    }

    //==========================================================================
    bool StripDisplay::State::operator== (const State& o) const
    {
        return name == o.name && value == o.value && arc == o.arc && bipolar == o.bipolar && selected == o.selected && eq == o.eq && ins == o.ins && dly == o.dly && comp == o.comp && gate == o.gate && muted == o.muted && std::abs (position - o.position) < 0.004f && routes == o.routes;
    }

    void StripDisplay::set (const State& s)
    {
        if (! (s == state))
        {
            state = s;
            repaint();
        }
    }

    void StripDisplay::paint (juce::Graphics& g)
    {
        static const juce::Colour orange (0xffff8a30), orangeOff (0x18ff7a20), cyan (0xff74c6ff), cyanOff (0x10a0c8ff);
        const float w = (float) getWidth(), h = (float) getHeight();
        if (state.selected)
        {
            g.setColour (orange);
            g.drawRect (juce::Rectangle<float> (1.0f, 1.0f, w - 2.0f, h - 2.0f), 1.0f);
        }
        // The encoder's arc: a thin circle open at the bottom, a dot at the value.
        const float cx = 13.0f, cy = 16.0f, r = 10.0f;
        const float start = -juce::MathConstants<float>::pi * 5.0f / 6.0f, end = juce::MathConstants<float>::pi * 5.0f / 6.0f;
        {
            juce::Path arc;
            arc.addCentredArc (cx, cy, r, r, 0.0f, start, end, true);
            g.setColour (state.arc ? orange : orangeOff);
            g.strokePath (arc, juce::PathStrokeType (1.3f));
        }
        if (state.arc)
        {
            const float a = start + (end - start) * state.position;
            g.setColour (orange);
            const auto p = juce::Point<float> (cx, cy).getPointOnCircumference (r, a);
            g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre (p));
            g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre (juce::Point<float> (cx, cy)));
        }
        // The indicators (p.55): routing in two columns, then EQ, INS, DLY, COMP, GATE.
        const auto badge = [&] (float x, float y, float bw, const juce::String& text, bool on)
        {
            const juce::Rectangle<float> rr (x, y, bw, 6.2f);
            g.setColour (on ? orange : orangeOff);
            g.fillRect (rr);
            g.setColour (on ? kVfdGlass : juce::Colour (0x30ff7a20));
            g.setFont (type::font (5.8f, true));
            g.drawText (text, rr, juce::Justification::centred, false);
        };
        for (int i = 0; i < 10; ++i)
        {
            const char* routes[] = { "1", "2", "3", "4", "5", "6", "7", "8", "S", "D" };
            badge (25.0f + (float) (i % 2) * 5.6f, 3.0f + (float) (i / 2) * 6.8f, 5.0f, routes[i], ((state.routes >> i) & 1) != 0);
        }
        const char* tags[] = { "EQ", "INS", "DLY", "COM", "GAT" };
        const bool lit[] = { state.eq, state.ins, state.dly, state.comp, state.gate };
        for (int i = 0; i < 5; ++i)
            badge (36.5f, 3.0f + (float) i * 6.8f, 11.0f, tags[i], lit[i]);

        // The name, or the value that's being moved: blue-white dots, 4 x 7-segment-ish cells of 5 x 7.
        const juce::String s = state.value.isNotEmpty() ? state.value : state.name;
        const float pitch = 1.9f, cellW = pitch * 6.0f, x0 = (w - cellW * (float) juce::jmin (4, s.length())) * 0.5f, y0 = 42.0f;
        for (int i = 0; i < juce::jmin (4, s.length()); ++i)
        {
            const auto c = s[i];
            const int index = c >= 32 && c <= 126 ? (int) c - 32 : 0;
            // Each dot is a small square; the unlit dots ghost faintly, as a VFD's do.
            for (int col = 0; col < 5; ++col)
                for (int row = 0; row < 7; ++row)
                {
                    const bool isLit = (dots::kFont5x7[index][col] >> row) & 1;
                    g.setColour (isLit ? cyan : cyanOff);
                    g.fillRect (juce::Rectangle<float> (x0 + (float) i * cellW + (float) col * pitch, y0 + (float) row * pitch, pitch * 0.72f, pitch * 0.72f));
                }
        }
        if (state.muted)
        {
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillRect (getLocalBounds());
        }
    }

    //==========================================================================
    Readout::Readout (int d) : digits (d) {}

    void Readout::refresh()
    {
        const auto s = text ? text() : juce::String();
        if (s != shown)
        {
            shown = s;
            repaint();
        }
    }

    void Readout::paint (juce::Graphics& g)
    {
        static const uint8_t segments[11] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x40 };
        const auto red = juce::Colour (0xffff3020), dark = juce::Colour (0x18ff3020);
        const float w = (float) getWidth(), h = (float) getHeight();
        const float cell = w / (float) digits, sw = juce::jmax (1.3f, h * 0.08f);

        // Right-aligned digits, a point lighting its digit's corner.
        juce::Array<int> glyphs;
        juce::Array<bool> points;
        for (int i = 0; i < shown.length(); ++i)
        {
            const auto c = shown[i];
            if (c == '.')
            {
                if (! points.isEmpty())
                    points.setUnchecked (points.size() - 1, true);
                continue;
            }
            glyphs.add (c >= '0' && c <= '9' ? (int) (c - '0') : c == '-' ? 10 : -1);
            points.add (false);
        }
        for (int d = 0; d < digits; ++d)
        {
            const int from = glyphs.size() - digits + d;
            const int glyph = from >= 0 ? glyphs[from] : -1;
            const float x = (float) d * cell + sw, gw = cell - sw * 2.2f, top = sw * 0.5f, mid = h * 0.5f, bot = h - sw * 0.5f;
            const std::array<juce::Line<float>, 7> lines { juce::Line<float> { x + sw, top, x + gw, top }, juce::Line<float> { x + gw, top, x + gw, mid }, juce::Line<float> { x + gw, mid, x + gw, bot }, juce::Line<float> { x + sw, bot, x + gw, bot }, juce::Line<float> { x, mid, x, bot }, juce::Line<float> { x, top, x, mid }, juce::Line<float> { x + sw, mid, x + gw, mid } };
            const uint8_t bits = glyph >= 0 ? segments[glyph] : 0;
            for (int s = 0; s < 7; ++s)
            {
                g.setColour (((bits >> s) & 1) != 0 ? red : dark);
                g.drawLine (lines[(size_t) s], sw);
            }
            if (from >= 0 && points[from])
            {
                g.setColour (red);
                g.fillEllipse (x + gw + sw * 0.3f, h - sw * 2.0f, sw * 1.9f, sw * 1.9f);
            }
        }
    }
} // namespace dm2k

//==============================================================================
Key& Dm2000Surface::key (const juce::String& name, juce::Colour face, juce::Colour led, Key::Shape shape, juce::Rectangle<int> r)
{
    auto k = std::make_unique<Key> (name, face, led, shape);
    k->setBounds (r);
    addAndMakeVisible (*k);
    keys.emplace_back (name, std::move (k));
    return *keys.back().second;
}

Key* Dm2000Surface::findKey (const juce::String& name) const
{
    for (const auto& [n, k] : keys)
        if (n == name)
            return k.get();
    return nullptr;
}

Knob& Dm2000Surface::knob (const juce::String& name, juce::Colour skirt, juce::Colour top, juce::Rectangle<int> r, juce::Colour ring)
{
    auto k = std::make_unique<Knob> (skirt, top, ring);
    k->setBounds (r);
    addAndMakeVisible (*k);
    knobs.emplace_back (name, std::move (k));
    return *knobs.back().second;
}

Knob* Dm2000Surface::findKnob (const juce::String& name) const
{
    for (const auto& [n, k] : knobs)
        if (n == name)
            return k.get();
    return nullptr;
}

Readout* Dm2000Surface::findReadout (const juce::String& name) const
{
    for (const auto& [n, r] : readouts)
        if (n == name)
            return r.get();
    return nullptr;
}

Key& Dm2000Surface::displayKey (const juce::String& name, Group g, juce::Rectangle<int> r)
{
    auto& k = key (name, kDiamond, kLedClear, Key::Shape::Diamond, r);
    k.onClick = [this, g] { display.pressGroup (g); };
    return k;
}

void Dm2000Surface::silk (float x, float y, float w, float h, const juce::String& text, float size, juce::Colour c, juce::Justification j, bool bold)
{
    silks.push_back ({ { x, y, w, h }, text, size, c, j, bold });
}

void Dm2000Surface::drawnKeys (juce::Rectangle<int> first, int cols, int rows, int dx, int dy)
{
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols; ++x)
            dummyKeys.push_back (first.translated (x * dx, y * dy).toFloat());
}

Dm2000Surface::Dm2000Surface (UiContext& c)
    : ctx (c), theme (Theme::dark()), universalKey ("UNIVERSAL MIXER", theme.accent, theme), display (c)
{
    using Shape = Key::Shape;
    // A panel key that does something: dark, with an orange lamp.
    const auto live = [this] (const juce::String& name, juce::Rectangle<int> r, std::function<void()> click, std::function<bool()> on = {}) -> Key&
    {
        auto& k = key (name, kKeyDark, kLedOrange, Shape::Square, r);
        k.onClick = std::move (click);
        if (on)
            k.isOn = std::move (on);
        return k;
    };
    universalKey.onClick = [&context = ctx]
    {
        juce::MessageManager::callAsync ([&context]
        {
            if (context.setInterface)
                context.setInterface (InterfaceKind::Universal);
        });
    };
    universalKey.setTooltip ("The Universal Mixer");
    addAndMakeVisible (universalKey);

    addAndMakeVisible (display);
    display.changed = [this]
    {
        bindStrips();
        bindSoft();
    };
    addAndMakeVisible (wheel);
    wheel.onTurn = [this] (int d) { display.turn (d); };

    // The 24 channels of analog inputs along the top (pp.22-24).
    for (int i = 0; i < 24; ++i)
    {
        const int cx = juce::roundToInt (haCx (i));
        auto& phantom = key ("48V " + juce::String (i + 1), kKeyDark, kLedRed, Shape::Square, centred (cx - 11, 46, 16, 15));
        auto* src = ctx.engine.getSource (i);
        phantom.isOn = [src] { return src != nullptr && src->phantom.load(); };
        phantom.onClick = [src] { if (src != nullptr) src->phantom = ! src->phantom.load(); };
        silk ((float) cx + 1.0f, 46.0f, 25.0f, 15.0f, "+48V", 7.5f, kSilkDim, juce::Justification::centredLeft);
        auto& pad = key ("PAD " + juce::String (i + 1), kKeyDark, kLedClear, Shape::Square, centred (cx - 16, 70, 14, 13));
        juce::ignoreUnused (pad);
        auto& gain = knob ("GAIN " + juce::String (i + 1), kSkirtTaupe, kTopTaupe, centred (cx + 3, 66, 34, 34));
        if (src != nullptr)
        {
            auto b = bindLinear ("GAIN", src->gainDb, 0.0f, 60.0f, " dB", 0, 16.0f);
            gain.setBinding (b);
        }
        silk ((float) cx - 24.0f, 104.0f, 48.0f, 9.0f, "+10 GAIN -34", 7.0f, kSilkDim);
        silk ((float) cx - 24.0f, 114.0f, 48.0f, 10.0f, juce::String (i + 1), 10.0f, kSilk);
        silk ((float) cx - 24.0f, 124.0f, 48.0f, 8.0f, "INSERT", 7.0f, kSilkDim);
    }
    {
        auto& talk = knob ("TALKBACK LEVEL", kSkirtTaupe, kTopTaupe, { 1500, 46, 30, 30 });
        juce::ignoreUnused (talk);
        silk (1480.0f, 78.0f, 70.0f, 9.0f, "TALKBACK LEVEL", 7.5f, kSilkDim);
        auto& studio = knob ("STUDIO LEVEL", kSkirtTaupe, kTopTaupe, { 1620, 60, 34, 34 });
        juce::ignoreUnused (studio);
        silk (1596.0f, 96.0f, 82.0f, 9.0f, "STUDIO LEVEL", 7.5f, kSilkDim);
        knob ("PHONES LEVEL", kSkirtTaupe, kTopTaupe, { 1710, 60, 34, 34 }).setBinding (bindFader ("PHONES", ctx.engine.phones.levelDb));
        silk (1686.0f, 96.0f, 82.0f, 9.0f, "PHONES LEVEL", 7.5f, kSilkDim);
        knob ("SMALL TRIM", kSkirtTaupe, kTopTaupe, { 1540, 94, 30, 30 });
        silk (1520.0f, 124.0f, 70.0f, 9.0f, "SMALL TRIM", 7.5f, kSilkDim);
    }

    // The strips: encoder, AUTO, SEL, SOLO, ON, its number, its display, its fader.
    const auto inputLaw = ctx.engine.getFaderLaw ((int) StripKind::Channel);
    for (int i = 0; i < kStrips; ++i)
    {
        const int cx = stripCx (i);
        auto enc = std::make_unique<Knob> (kSkirtEnc, kTopEnc);
        enc->setBounds (centred (cx, 526, 32, 32));
        enc->onPress = [this, i] { movedAt[(size_t) i] = juce::Time::getMillisecondCounter(); };
        addAndMakeVisible (*enc);
        encoders.push_back (std::move (enc));

        key ("AUTO " + juce::String (i), kKeyDark, kLedGreen, Shape::Square, centred (cx, 562, 30, 18));
        auto& sel = key ("SEL " + juce::String (i), kKeySel, kLedOrange, Shape::Block, centred (cx, 589, 40, 25));
        auto& solo = key ("SOLO " + juce::String (i), kKeySolo, kLedYellow, Shape::Block, centred (cx, 624, 40, 24));
        auto& on = key ("ON " + juce::String (i), kKeyOn, kLedYellow, Shape::Block, centred (cx, 658, 40, 24));
        sel.isOn = [this, i] { auto* s = stripAt (i); return s != nullptr && s == selected(); };
        sel.onClick = [this, i] { if (auto* s = stripAt (i); s != nullptr && ctx.selectStrip) ctx.selectStrip (s); };
        solo.isOn = [this, i] { auto* s = stripAt (i); return s != nullptr && s->params.solo.load(); };
        solo.onClick = [this, i]
        {
            if (auto* s = stripAt (i))
            {
                s->params.solo = ! s->params.solo.load();
                autoSelect (i);
            }
        };
        on.isOn = [this, i] { auto* s = stripAt (i); return s != nullptr && ! s->params.mute.load(); };
        on.onClick = [this, i]
        {
            if (auto* s = stripAt (i))
            {
                s->params.mute = ! s->params.mute.load();
                autoSelect (i);
            }
        };
        silk ((float) cx - 20.0f, 580.0f, 40.0f, 9.0f, "AUTO", 8.0f, kSilk);
        silk ((float) cx - 20.0f, 614.0f, 40.0f, 10.0f, "SEL", 8.0f, kSilk);
        silk ((float) cx - 20.0f, 648.0f, 40.0f, 10.0f, "SOLO", 8.0f, kSilk);
        silk ((float) cx - 20.0f, 682.0f, 40.0f, 9.0f, "ON", 8.0f, kSilk);
        silk ((float) cx - 20.0f, 690.0f, 40.0f, 12.0f, juce::String (i + 1), 11.0f, kSilk, juce::Justification::centred, true);
        silk ((float) cx - 24.0f, 1062.0f, 48.0f, 14.0f, juce::String (i + 1), 11.0f, kSilk);

        auto name = std::make_unique<StripDisplay>();
        name->setBounds (centred (cx, kVfdY, 49, kVfdH));
        addAndMakeVisible (*name);
        names.push_back (std::move (name));

        auto f = std::make_unique<Fader> (false, inputLaw);
        f->setBounds (centred (cx, kFaderY, 49, kFaderH));
        f->onTouch = [this, i]
        {
            movedAt[(size_t) i] = juce::Time::getMillisecondCounter();
            autoSelect (i);
        };
        addAndMakeVisible (*f);
        faders.push_back (std::move (f));
    }

    // LAYER (p.58).
    {
        const char* layerNames[] = { "1-24", "25-48", "49-72", "73-96", "MASTER" };
        for (int i = 0; i < 5; ++i)
        {
            auto& k = key (juce::String ("LAYER ") + layerNames[i], kKeyDark, kLedGreen, Shape::Square, { 1391, 530 + i * 34, 30, 22 });
            k.isOn = [this, i] { return layer == i; };
            k.onClick = [this, i] { setLayer (i); };
            silk (1391.0f, 552.0f + (float) i * 34.0f, 30.0f, 10.0f, layerNames[i], 7.5f, kSilkDark);
        }
        for (int i = 0; i < 4; ++i)
        {
            auto& k = key ("LAYER REMOTE " + juce::String (i + 1), kKeyDark, kLedGreen, Shape::Square, { 1456, 530 + i * 34, 30, 22 });
            k.isOn = [this, i] { return layer == LayerRemote1 + i; };
            k.onClick = [this, i] { setLayer (LayerRemote1 + i); };
            silk (1449.0f, 552.0f + (float) i * 34.0f, 44.0f, 10.0f, "REMOTE " + juce::String (i + 1), 7.5f, kSilkDark);
        }
    }

    // STEREO.
    {
        key ("STEREO AUTO", kKeyDark, kLedGreen, Shape::Square, centred (1438, 714, 28, 16));
        auto& sel = key ("STEREO SEL", kKeySel, kLedOrange, Shape::Block, centred (1438, 742, 38, 20));
        auto& on = key ("STEREO ON", kKeyOn, kLedYellow, Shape::Block, centred (1438, 770, 38, 18));
        sel.isOn = [this] { auto* s = stereoStrip(); return s != nullptr && s == selected(); };
        sel.onClick = [this] { if (auto* s = stereoStrip(); s != nullptr && ctx.selectStrip) ctx.selectStrip (s); };
        on.isOn = [this] { auto* s = stereoStrip(); return s != nullptr && ! s->params.mute.load(); };
        on.onClick = [this] { if (auto* s = stereoStrip()) s->params.mute = ! s->params.mute.load(); };
        silk (1415.0f, 731.0f, 46.0f, 9.0f, "AUTO", 7.5f, kSilkDark);
        silk (1415.0f, 762.0f, 46.0f, 8.0f, "SEL", 7.5f, kSilkDark);
        silk (1415.0f, 788.0f, 46.0f, 8.0f, "ON", 7.5f, kSilkDark);
        stereoFader = std::make_unique<Fader> (true, ctx.engine.getFaderLaw ((int) StripKind::Main));
        stereoFader->setBounds (centred (1438, kFaderY + 8, 49, kFaderH - 18));
        addAndMakeVisible (*stereoFader);
        silk (1400.0f, 1058.0f, 76.0f, 14.0f, "STEREO", 10.0f, kSilkDark, juce::Justification::centred, true);
    }

    // The left block: MATRIX SELECT, AUX SELECT, ENCODER MODE, FADER MODE (pp.25-27).
    const auto selectKey = [this] (const juce::String& name, juce::Colour led, juce::Rectangle<int> r, std::function<bool()> isOn, std::function<void()> onClick, const juce::String& label)
    {
        auto& k = key (name, kKeyDark, led, Shape::Square, r);
        k.isOn = std::move (isOn);
        k.onClick = std::move (onClick);
        silk ((float) r.getX() - 6.0f, (float) r.getBottom(), (float) r.getWidth() + 12.0f, 9.0f, label, 7.0f, kSilk);
    };
    displayKey ("DISPLAY MATRIX", Group::MatrixSend, { 34, 208, 28, 28 });
    silk (28.0f, 238.0f, 40.0f, 9.0f, "DISPLAY", 7.0f, kSilk);
    silk (90.0f, 205.0f, 170.0f, 12.0f, "MATRIX SELECT", 9.5f, kSilk, juce::Justification::centredLeft, true);
    for (int i = 0; i < 4; ++i)
        selectKey ("MATRIX " + juce::String (i + 1), kLedOrange, { 92 + i * 41, 220, 32, 20 }, [this, i] { return display.getMatrix() == i; },
                   [this, i]
                   {
                       display.setMatrix (i);
                       bindStrips();
                       bindSelected();
                   },
                   "MATRIX " + juce::String (i + 1));

    displayKey ("DISPLAY AUX", Group::AuxSend, { 34, 262, 28, 28 });
    silk (28.0f, 292.0f, 40.0f, 9.0f, "DISPLAY", 7.0f, kSilk);
    silk (90.0f, 256.0f, 170.0f, 12.0f, "AUX SELECT", 9.5f, kSilk, juce::Justification::centredLeft, true);
    for (int i = 0; i < 12; ++i)
    {
        const int a = i;
        selectKey ("AUX " + juce::String (i + 1), kLedOrange, { 92 + (i % 4) * 41, 271 + (i / 4) * 37, 32, 20 }, [this, a] { return display.getAux() == a; },
                   [this, a]
                   {
                       display.setAux (a);
                       if (faderMode == FaderMode::AuxMtrx || encoderMode == EncoderMode::AuxMtrx)
                           bindStrips();
                   },
                   "AUX " + juce::String (i + 1));
    }

    displayKey ("DISPLAY ENCODER", Group::EncoderMode, { 34, 398, 28, 28 });
    silk (28.0f, 428.0f, 40.0f, 9.0f, "DISPLAY", 7.0f, kSilk);
    silk (90.0f, 388.0f, 110.0f, 12.0f, "ENCODER MODE", 9.5f, kSilk, juce::Justification::centredLeft, true);
    silk (170.0f, 388.0f, 110.0f, 12.0f, "FADER MODE", 9.5f, kSilk, juce::Justification::centred, true);
    selectKey ("ENC PAN", kLedOrange, { 92, 408, 32, 20 }, [this] { return encoderMode == EncoderMode::Pan; }, [this] { setEncoderMode (EncoderMode::Pan); }, "PAN");
    selectKey ("ENC AUX", kLedOrange, { 133, 408, 32, 20 }, [this] { return encoderMode == EncoderMode::AuxMtrx; }, [this] { setEncoderMode (EncoderMode::AuxMtrx); }, "AUX/MTRX");
    selectKey ("FADER FADER", kLedOrange, { 196, 408, 32, 20 }, [this] { return faderMode == FaderMode::Fader; }, [this] { setFaderMode (FaderMode::Fader); }, "FADER");
    selectKey ("FADER AUX", kLedOrange, { 237, 408, 32, 20 }, [this] { return faderMode == FaderMode::AuxMtrx; }, [this] { setFaderMode (FaderMode::AuxMtrx); }, "AUX/MTRX");
    for (int i = 0; i < 4; ++i)
        selectKey ("ENC ASSIGN " + juce::String (i + 1), kLedOrange, { 92 + i * 41, 448, 32, 20 }, [this, i] { return encoderMode == (EncoderMode) ((int) EncoderMode::Assign1 + i); },
                   [this, i]
                   {
                       setEncoderMode ((EncoderMode) ((int) EncoderMode::Assign1 + i));
                       display.setAssignKey (i);
                   },
                   "ASSIGN " + juce::String (i + 1));

    // DISPLAY ACCESS (p.28) and EFFECTS/PLUG-INS (p.29).
    {
        silk (296.0f, 146.0f, 170.0f, 12.0f, "DISPLAY ACCESS", 9.5f, kSilk, juce::Justification::centred, true);
        const std::pair<const char*, Group> access[] = { { "DATA", Group::Data }, { "DIO", Group::Dio }, { "SETUP", Group::Setup }, { "UTILITY", Group::Utility }, { "MIDI", Group::Midi }, { "REMOTE", Group::Remote }, { "METER", Group::Meter }, { "VIEW", Group::View }, { "PAIR", Group::Pair }, { "GROUP", Group::FaderGroup }, { "INPUT PATCH", Group::InputPatch }, { "OUTPUT PATCH", Group::OutputPatch } };
        for (int i = 0; i < 12; ++i)
        {
            const int x = 307 + (i % 4) * 41, y = 160 + (i / 4) * 49;
            displayKey (access[i].first, access[i].second, { x, y, 26, 26 });
            const juce::String label (access[i].first);
            silk ((float) x - 8.0f, (float) y + 26.0f, 42.0f, 14.0f, label.replace (" ", "\n"), 7.0f, kSilk);
        }
        displayKey ("DISPLAY EFFECTS", Group::Effects, { 294, 318, 28, 28 });
        silk (288.0f, 348.0f, 40.0f, 9.0f, "DISPLAY", 7.0f, kSilk);
        silk (330.0f, 310.0f, 170.0f, 12.0f, "EFFECTS / PLUG-INS", 9.5f, kSilk, juce::Justification::centredLeft, true);
        const auto unitsIn = [this]
        {
            return display.getGroup() == Group::Effects && display.getTabName (display.getPage()) == "GEQ EDIT";
        };
        const char* tops[] = { "INTERNAL\nEFFECTS", "GRAPHIC\nEQUALIZERS", "PLUG-INS", "CHANNEL\nINSERTS" };
        for (int i = 0; i < 4; ++i)
        {
            auto& k = key (juce::String ("FX MODE ") + juce::String (i + 1), kKeyDark, kLedOrange, Shape::Square, { 338 + i * 41, 326, 32, 20 });
            silk ((float) 330 + (float) i * 41.0f, 347.0f, 48.0f, 18.0f, tops[i], 6.8f, kSilk);
            if (i == 0)
            {
                k.isOn = [this, unitsIn] { return display.getGroup() == Group::Effects && ! unitsIn(); };
                k.onClick = [this] { display.showPage (Group::Effects, 0); };
            }
            else if (i == 1)
            {
                k.isOn = [unitsIn] { return unitsIn(); };
                k.onClick = [this] { display.showPage (Group::Effects, 2); };
            }
            else if (i == 2)
                k.onClick = [this]
                {
                    // The plug-in cards are Sidecar's plug-ins: their overview window.
                    if (ctx.showPluginOverview)
                        ctx.showPluginOverview();
                    else
                        display.showMessage ("No plug-in card: they aren't in Sidecar.");
                };
            else
                // CHANNEL INSERTS (p.26): the effects processor inserted in the selected channel; nothing
                // can be patched at an insert here, so it's always the console's warning.
                k.onClick = [this]
                {
                    auto* s = selected();
                    if (s != nullptr && s->params.preInsert().on.load() && s->params.preInsert().fx.load() >= 0)
                        display.setFxUnit (s->params.preInsert().fx.load()), display.showPage (Group::Effects, 0);
                    else
                        display.showMessage ("Nothing is inserted in this channel.");
                };
        }
        for (int i = 0; i < 8; ++i)
        {
            const int u = i;
            auto& k = key ("FX UNIT " + juce::String (i + 1), kKeyDark, kLedOrange, Shape::Square, { 338 + (i % 4) * 41, 370 + (i / 4) * 34, 32, 20 });
            k.isOn = [this, u, unitsIn] { return unitsIn() ? display.getGeqUnit() == u : display.getFxUnit() == u; };
            k.onClick = [this, u, unitsIn]
            {
                if (unitsIn())
                    display.setGeqUnit (juce::jmin (u, 5));
                else
                    display.setFxUnit (u);
            };
            silk ((float) 338 + (float) (i % 4) * 41.0f, 390.0f + (float) (i / 4) * 34.0f, 32.0f, 9.0f, juce::String (i + 1), 7.5f, kSilk);
        }
    }

    // F1-F4, the tab arrows, the four knobs under the display (p.30).
    {
        const int tabX[] = { 598, 685, 773, 860 };
        for (int i = 0; i < 4; ++i)
        {
            auto& k = key ("F" + juce::String (i + 1), kKeyDark, kLedClear, Shape::Square, centred (tabX[i], 466, 34, 18));
            k.onClick = [this, i] { display.pressF (i); };
            silk ((float) tabX[i] - 44.0f, 468.0f, 12.0f, 12.0f, "F" + juce::String (i + 1), 8.0f, kSilk, juce::Justification::centredRight);
            auto& soft = knob ("SOFT " + juce::String (i + 1), kSkirtTaupe, kTopTaupe, centred (tabX[i], 488, 32, 32));
            juce::ignoreUnused (soft);
        }
        auto& l = key ("TAB LEFT", kKeyDark, kLedClear, Shape::Square, centred (541, 466, 26, 18));
        l.setGlyph (juce::String::charToString (juce::juce_wchar (0x25c0)));
        l.onClick = [this] { display.scrollTabs (-1); };
        auto& r = key ("TAB RIGHT", kKeyDark, kLedClear, Shape::Square, centred (916, 466, 26, 18));
        r.setGlyph (juce::String::charToString (juce::juce_wchar (0x25b6)));
        r.onClick = [this] { display.scrollTabs (1); };
    }

    // SELECTED CHANNEL (pp.32-37).
    const int sx = kSelX;
    const auto bluekey = [this] (const juce::String& name, juce::Rectangle<int> r, juce::Colour led = kLedOrange) -> Key& { return key (name, kKeyDark, led, Shape::Square, r); };

    // ROUTING.
    displayKey ("DISPLAY ROUTING", Group::Routing, { sx + 8, 166, 24, 24 });
    silk ((float) sx + 2.0f, 192.0f, 36.0f, 8.0f, "DISPLAY", 6.5f, kSilk);
    silk ((float) sx + 44.0f, 161.0f, 90.0f, 11.0f, "ROUTING", 8.5f, kSilk, juce::Justification::centred, true);
    for (int i = 0; i < 8; ++i)
    {
        const int b = i;
        auto& k = bluekey ("ROUTE " + juce::String (i + 1), { sx + 54 + (i % 2) * 48, 175 + (i / 2) * 25, 32, 18 });
        k.isOn = [this, b] { auto* s = selected(); return s != nullptr && s->isInput() && s->sends[(size_t) b].enabled.load() && s->sends[(size_t) b].levelDb.load() > -89.5f; };
        k.onClick = [this, b]
        {
            if (auto* s = selected(); s != nullptr && s->isInput())
            {
                auto& send = s->sends[(size_t) b];
                const bool wasOn = send.enabled.load() && send.levelDb.load() > -89.5f;
                send.levelDb = 0.0f;
                send.enabled = ! wasOn;
                autoShow (Group::Routing);
            }
        };
        silk ((float) sx + 54.0f + (float) (i % 2) * 48.0f, 193.0f + (float) (i / 2) * 25.0f, 32.0f, 7.0f, juce::String (i + 1), 7.0f, kSilk);
    }
    {
        auto& f = bluekey ("FOLLOW PAN", { sx + 8, 272, 32, 18 });
        f.isOn = [this] { auto* s = selected(); return s != nullptr && s->isInput() && s->sends[0].panLink.load(); };
        f.onClick = [this]
        {
            if (auto* s = selected(); s != nullptr && s->isInput())
            {
                const bool on = ! s->sends[0].panLink.load();
                for (int b = 0; b < 8; ++b)
                    s->sends[(size_t) b].panLink = on;
            }
        };
        silk ((float) sx + 2.0f, 290.0f, 44.0f, 8.0f, "FOLLOW PAN", 6.5f, kSilk);
        auto& st = bluekey ("ROUTE STEREO", { sx + 54, 272, 32, 18 });
        st.isOn = [this] { auto* s = selected(); return s != nullptr && s->isInput() && s->mains[0].on.load(); };
        st.onClick = [this]
        {
            if (auto* s = selected(); s != nullptr && s->isInput())
            {
                s->mains[0].levelDb = 0.0f;
                s->mains[0].on = ! s->mains[0].on.load();
                autoShow (Group::Routing);
            }
        };
        silk ((float) sx + 48.0f, 290.0f, 44.0f, 8.0f, "STEREO", 6.5f, kSilk);
        // [DIRECT]: the channel's Direct Out, to the destination OUTPUT PATCH's D.OUT pages give it.
        auto& direct = bluekey ("ROUTE DIRECT", { sx + 102, 272, 32, 18 });
        direct.isOn = [this] { auto* s = selected(); return s != nullptr && s->getKind() == StripKind::Channel && display.isDirectOn (s->getNumber() - 1); };
        direct.onClick = [this]
        {
            auto* s = selected();
            if (s == nullptr || s->getKind() != StripKind::Channel)
                return;
            const int channel = s->getNumber() - 1;
            if (display.getDirectOut (channel) == 0)
                display.showMessage ("No Direct Out destination: OUTPUT PATCH.");
            else
                display.setDirectOn (channel, ! display.isDirectOn (channel));
            autoShow (Group::Routing);
        };
        silk ((float) sx + 96.0f, 290.0f, 44.0f, 8.0f, "DIRECT", 6.5f, kSilk);
    }

    // PHASE / INSERT and DELAY.
    displayKey ("DISPLAY PHASE", Group::PhaseInsert, { sx + 164, 164, 24, 24 });
    silk ((float) sx + 158.0f, 190.0f, 36.0f, 8.0f, "DISPLAY", 6.5f, kSilk);
    silk ((float) sx + 200.0f, 161.0f, 120.0f, 11.0f, "PHASE / INSERT", 8.5f, kSilk, juce::Justification::centred, true);
    {
        auto& phase = bluekey ("PHASE", { sx + 208, 174, 32, 18 });
        phase.isOn = [this] { auto* s = selected(); return s != nullptr && s->isInput() && s->params.input.invert.load(); };
        phase.onClick = [this]
        {
            if (auto* s = selected(); s != nullptr && s->isInput())
            {
                s->params.input.invert = ! s->params.input.invert.load();
                // From off to on, the Phase pages (p.274).
                if (s->params.input.invert.load() && display.autoDisplay (Group::PhaseInsert) && display.getGroup() != Group::PhaseInsert)
                    display.showPage (Group::PhaseInsert, s->getNumber() > 48 ? 1 : 0);
            }
        };
        silk ((float) sx + 202.0f, 192.0f, 44.0f, 8.0f, juce::String (juce::CharPointer_UTF8 ("\xc3\x98")), 7.0f, kSilk);
        auto& ins = bluekey ("INSERT ON", { sx + 272, 174, 32, 18 });
        ins.isOn = [this] { auto* s = selected(); return s != nullptr && s->params.preInsert().on.load(); };
        ins.onClick = [this]
        {
            if (auto* s = selected())
            {
                s->params.preInsert().on = ! s->params.preInsert().on.load();
                if (display.autoDisplay (Group::PhaseInsert))
                    display.showPage (Group::PhaseInsert, 2);
            }
        };
        silk ((float) sx + 262.0f, 192.0f, 52.0f, 8.0f, "INSERT ON", 6.5f, kSilk);
    }
    displayKey ("DISPLAY DELAY", Group::Delay, { sx + 336, 164, 24, 24 });
    silk ((float) sx + 330.0f, 190.0f, 36.0f, 8.0f, "DISPLAY", 6.5f, kSilk);
    silk ((float) sx + 370.0f, 161.0f, 60.0f, 11.0f, "DELAY", 8.5f, kSilk, juce::Justification::centred, true);
    {
        auto& on = bluekey ("DELAY ON", { sx + 372, 174, 32, 18 });
        on.isOn = [this] { auto* s = selected(); return s != nullptr && s->params.input.delayOn.load(); };
        on.onClick = [this] { if (auto* s = selected()) s->params.input.delayOn = ! s->params.input.delayOn.load(); };
        silk ((float) sx + 368.0f, 192.0f, 40.0f, 8.0f, "ON", 6.5f, kSilk);
        knob ("DELAY TIME", kSkirtTaupe, kTopTaupe, { sx + 420, 161, 34, 34 });
        silk ((float) sx + 417.0f, 196.0f, 40.0f, 8.0f, "TIME", 6.5f, kSilk);
        knob ("DELAY MIX", kSkirtTaupe, kTopTaupe, { sx + 480, 161, 34, 34 });
        silk ((float) sx + 465.0f, 196.0f, 70.0f, 8.0f, "MIX    FB", 6.5f, kSilk);
    }

    // AUX / MATRIX SEND.
    displayKey ("DISPLAY AUX SEND", Group::AuxSend, { sx + 164, 216, 24, 24 });
    silk ((float) sx + 158.0f, 242.0f, 36.0f, 8.0f, "DISPLAY", 6.5f, kSilk);
    silk ((float) sx + 200.0f, 203.0f, 200.0f, 11.0f, "AUX / MATRIX SEND", 8.5f, kSilk, juce::Justification::centredLeft, true);
    {
        auto& bank = bluekey ("AUX BANK", { sx + 200, 240, 30, 16 });
        bank.onClick = [this] { setAuxBank ((auxBank + 1) % 3); };
        silk ((float) sx + 194.0f, 256.0f, 42.0f, 8.0f, "BANK", 6.5f, kSilk);
        const char* legends[3][4] = { { "AUX 1 / MATRIX 1", "AUX 2 / MATRIX 2", "AUX 3 / MATRIX 3", "AUX 4 / MATRIX 4" }, { "AUX 5", "AUX 6", "AUX 7", "AUX 8" }, { "AUX 9", "AUX 10", "AUX 11", "AUX 12" } };
        for (int k = 0; k < 4; ++k)
        {
            const int cx = sx + 262 + k * 74;
            knob ("LEVEL " + juce::String (k + 1), kSkirtTaupe, kTopTaupe, centred (cx, 208, 32, 32));
            silk ((float) cx - 26.0f, 240.0f, 52.0f, 8.0f, "LEVEL", 6.5f, kSilk);
            auto& on = bluekey ("SEND ON " + juce::String (k + 1), centred (cx, 248, 30, 16), kLedOrange);
            on.onClick = [this, k]
            {
                auto* s = selected();
                if (s == nullptr)
                    return;
                if (s->isInput())
                {
                    auto& send = s->sends[(size_t) (kAuxSlot + auxBank * 4 + k)];
                    send.enabled = ! send.enabled.load();
                }
                else if (auxBank == 0)
                {
                    auto& send = s->matrix[(size_t) k];
                    send.on = ! send.on.load();
                }
            };
            on.isOn = [this, k]
            {
                auto* s = selected();
                if (s == nullptr)
                    return false;
                if (s->isInput())
                    return s->sends[(size_t) (kAuxSlot + auxBank * 4 + k)].enabled.load();
                return auxBank == 0 && s->matrix[(size_t) k].on.load();
            };
            silk ((float) cx - 16.0f, 264.0f, 32.0f, 8.0f, "ON", 6.5f, kSilk);
            for (int bankRow = 0; bankRow < 3; ++bankRow)
                silk ((float) cx - 38.0f, 271.0f + (float) bankRow * 8.0f, 76.0f, 8.0f, legends[bankRow][k], 6.2f, kSilkDark);
        }
    }

    // DYNAMICS.
    displayKey ("DISPLAY DYNAMICS", Group::Dynamics, { sx + 8, 314, 24, 24 });
    silk ((float) sx + 2.0f, 340.0f, 36.0f, 8.0f, "DISPLAY", 6.5f, kSilk);
    silk ((float) sx + 40.0f, 304.0f, 100.0f, 11.0f, "DYNAMICS", 8.5f, kSilk, juce::Justification::centred, true);
    {
        auto& gate = bluekey ("GATE ON", { sx + 54, 326, 32, 16 });
        gate.isOn = [this] { auto* s = selected(); return s != nullptr && s->isInput() && s->params.gate().on.load(); };
        gate.onClick = [this] { if (auto* s = selected(); s != nullptr && s->isInput()) s->params.gate().on = ! s->params.gate().on.load(); };
        silk ((float) sx + 44.0f, 342.0f, 52.0f, 8.0f, "GATE ON", 6.5f, kSilk);
        auto& comp = bluekey ("COMP ON", { sx + 102, 326, 32, 16 });
        comp.isOn = [this] { auto* s = selected(); return s != nullptr && s->params.dynamics().on.load(); };
        comp.onClick = [this] { if (auto* s = selected()) s->params.dynamics().on = ! s->params.dynamics().on.load(); };
        silk ((float) sx + 92.0f, 342.0f, 52.0f, 8.0f, "COMP ON", 6.5f, kSilk);
        auto& pick = bluekey ("GATE COMP", { sx + 148, 308, 30, 16 }, kLedClear);
        pick.onClick = [this] { setDynamicsShowsComp (! showComp); };
        silk ((float) sx + 136.0f, 325.0f, 54.0f, 8.0f, "GATE / COMP", 6.5f, kSilk);
        const char* gateRow[] = { "THRESHOLD", "RANGE", "ATTACK", "DECAY", "HOLD" };
        const char* compRow[] = { "THRESHOLD", "RATIO", "ATTACK", "RELEASE", "GAIN" };
        for (int k = 0; k < 5; ++k)
        {
            const int cx = sx + 206 + k * 50;
            knob ("DYN " + juce::String (k + 1), kSkirtDark, kTopDark, centred (cx, 304, 30, 30));
            silk ((float) cx - 25.0f, 336.0f, 50.0f, 8.0f, gateRow[k], 6.0f, kSilk);
            silk ((float) cx - 25.0f, 345.0f, 50.0f, 8.0f, compRow[k], 6.0f, kSilk);
        }
    }

    // PAN / SURROUND.
    displayKey ("DISPLAY PAN", Group::Pan, { sx + 8, 360, 24, 24 });
    silk ((float) sx + 2.0f, 385.0f, 36.0f, 8.0f, "DISPLAY", 6.5f, kSilk);
    silk ((float) sx + 44.0f, 353.0f, 120.0f, 11.0f, "PAN / SURROUND", 8.5f, kSilk, juce::Justification::centred, true);
    {
        // [L] and [R] (p.31): the odd and the even channel of the selected one's horizontal pair.
        for (int right = 0; right < 2; ++right)
        {
            auto& k = bluekey (right != 0 ? "PAN R" : "PAN L", { sx + 54 + right * 34, 368, 26, 16 });
            k.isOn = [this, right]
            {
                auto* s = selected();
                return s != nullptr && (s->getKind() == StripKind::Channel || s->getKind() == StripKind::Bus) && (s->getNumber() % 2 == 0) == (right != 0);
            };
            k.onClick = [this, right]
            {
                auto* s = selected();
                if (s == nullptr || ! ctx.selectStrip || (s->getKind() != StripKind::Channel && s->getKind() != StripKind::Bus))
                    return;
                const auto& list = s->getKind() == StripKind::Channel ? ctx.engine.getChannels() : ctx.engine.getBuses();
                const int index = (s->getNumber() - 1) / 2 * 2 + right;
                if (juce::isPositiveAndBelow (index, (int) list.size()))
                    ctx.selectStrip (list[(size_t) index].get());
            };
        }
        silk ((float) sx + 54.0f, 385.0f, 26.0f, 8.0f, "L", 6.5f, kSilk);
        silk ((float) sx + 88.0f, 385.0f, 26.0f, 8.0f, "R", 6.5f, kSilk);
        knob ("PAN", kSkirtTaupe, kTopTaupe, { sx + 250, 357, 34, 34 });
        for (int i = 0; i < 3; ++i)
        {
            const char* n[] = { "LINK", "GRAB", "EFFECT" };
            bluekey (juce::String ("PAN ") + n[i], { sx + 310 + i * 40, 368, 30, 16 });
            silk ((float) sx + 310.0f + (float) i * 40.0f - 6.0f, 385.0f, 42.0f, 8.0f, n[i], 6.5f, kSilk);
        }
    }

    // EQUALIZER and CHANNEL.
    displayKey ("DISPLAY EQ", Group::Equalizer, { sx + 8, 404, 24, 24 });
    silk ((float) sx + 2.0f, 430.0f, 36.0f, 8.0f, "DISPLAY", 6.5f, kSilk);
    silk ((float) sx + 44.0f, 398.0f, 120.0f, 11.0f, "EQUALIZER", 8.5f, kSilk, juce::Justification::centred, true);
    {
        silk ((float) sx + 2.0f, 446.0f, 42.0f, 10.0f, "CHANNEL", 7.0f, kSilk, juce::Justification::centred, true);
        bluekey ("COPY", { sx + 8, 458, 28, 16 }).onClick = [this] { display.copyChannel(); };
        silk ((float) sx + 2.0f, 474.0f, 40.0f, 8.0f, "COPY", 6.5f, kSilk);
        bluekey ("PASTE", { sx + 8, 488, 28, 16 }).onClick = [this] { display.pasteChannel(); };
        silk ((float) sx + 2.0f, 504.0f, 40.0f, 8.0f, "PASTE", 6.5f, kSilk);
        knob ("ATT", kSkirtTaupe, kTopTaupe, { sx + 56, 410, 36, 36 });
        silk ((float) sx + 54.0f, 447.0f, 40.0f, 8.0f, "ATT", 6.5f, kSilk);
        auto& eqOn = bluekey ("EQ ON", { sx + 56, 468, 34, 18 });
        eqOn.isOn = [this] { auto* s = selected(); return s != nullptr && s->params.eq().on.load(); };
        eqOn.onClick = [this] { if (auto* s = selected()) s->params.eq().on = ! s->params.eq().on.load(); };
        silk ((float) sx + 50.0f, 486.0f, 46.0f, 8.0f, "EQ ON", 6.5f, kSilk);
        const char* bandNames[] = { "LOW", "LOW-MID", "HIGH-MID", "HIGH" };
        for (int b = 0; b < 4; ++b)
        {
            const int x = sx + 108 + b * 108, band = b;
            silk ((float) x + 30.0f, 403.0f, 72.0f, 10.0f, bandNames[b], 8.0f, kSilk, juce::Justification::centredRight, true);
            auto& f = knob ("EQ F " + juce::String (b + 1), kSkirtTeal, kTopTeal, { x + 4, 410, 34, 34 }, kRingTeal);
            auto& g = knob ("EQ G " + juce::String (b + 1), kSkirtTeal, kTopTeal, { x + 56, 426, 38, 38 }, kRingTeal);
            f.onPress = [this, band]
            {
                display.setFreqNotQ (band, ! display.isFreqNotQ (band));
                display.setBand (band);
                bindSelected();
                display.rebuild();
            };
            juce::ignoreUnused (g);
            silk ((float) x + 2.0f, 445.0f, 62.0f, 8.0f, "FREQUENCY", 6.5f, kSilk);
            silk ((float) x + 2.0f, 454.0f, 40.0f, 8.0f, "Q", 6.5f, kSilk);
            silk ((float) x + 54.0f, 465.0f, 42.0f, 8.0f, "GAIN", 6.5f, kSilk);
            auto readout = std::make_unique<Readout> (3);
            readout->setBounds (x + 8, 479, 52, 24);
            addAndMakeVisible (*readout);
            readouts.emplace_back ("EQ " + juce::String (b + 1), std::move (readout));
            auto* lamp = readouts.back().second.get();
            lamp->text = [this, band]
            {
                auto* s = selected();
                if (s == nullptr)
                    return juce::String ("---");
                auto& eq = s->params.eq();
                const auto table = s->isOutput() ? models::BusEq : models::ChannelEq;
                const auto& m = models::model (table, eq.model.load());
                const auto id = [band] (const char* suffix) { const char* pre[] = { "l", "lm", "hm", "h" }; return juce::String (pre[band]) + suffix; };
                const auto value = [&] (const char* suffix) { const int i = m.find (id (suffix)); return i < 0 ? juce::String ("---") : m.params[(size_t) i].display (eq.values[i].load()); };
                const bool recently = juce::Time::getMillisecondCounter() - eqGainMoved[(size_t) band] < 2000;
                if (recently)
                {
                    const int i = m.find (id ("g"));
                    return i >= 0 ? juce::String (eq.values[i].load(), 1) : juce::String ("0.0");
                }
                if (! display.isFreqNotQ (band))
                    return value ("q").replace ("L.SHELF", "---").replace ("H.SHELF", "---").replace ("HPF", "---").replace ("LPF", "---");
                auto text = value ("f");
                const bool k = text.containsChar ('k');
                return juce::String (text.getFloatValue(), k ? 2 : 0);
            };
        }
    }

    // TRACK ARMING, SCENE MEMORY, AUTOMIX, USER DEFINED KEYS, LOCATOR.
    {
        const int x = 1520;
        displayKey ("DISPLAY ARMING", Group::TrackArming, { x, 152, 24, 24 });
        silk ((float) x + 34.0f, 153.0f, 120.0f, 11.0f, "TRACK ARMING", 8.5f, kSilk, juce::Justification::centredLeft, true);
        for (int i = 0; i < 24; ++i)
        {
            const int t = i;
            live ("ARM " + juce::String (i + 1), { x + 14 + (i % 8) * 27, 184 + (i / 8) * 22, 22, 16 },
                  [this, t] { display.toggleTrackArm (display.getArmPage() * 24 + t); },
                  [this, t] { return display.isTrackArmed (display.getArmPage() * 24 + t); });
        }
        drawnKeys ({ x + 14, 258, 22, 16 }, 4, 1, 27, 0);
        live ("ARM ALL CLEAR", { x + 158, 258, 22, 16 }, [this]
        {
            if (ctx.recording != nullptr)
                ctx.recording->armAll (false);
        });
        drawnKeys ({ x + 208, 258, 22, 16 }, 1, 1, 0, 0);
        silk ((float) x + 8.0f, 276.0f, 120.0f, 8.0f, "TRACK ARMING GROUP", 6.5f, kSilk);
        silk ((float) x + 146.0f, 276.0f, 48.0f, 8.0f, "ALL CLEAR", 6.5f, kSilk);
        silk ((float) x + 196.0f, 276.0f, 48.0f, 8.0f, "MASTER", 6.5f, kSilk);

        displayKey ("DISPLAY SCENE", Group::SceneMemory, { x, 298, 24, 24 });
        silk ((float) x + 34.0f, 299.0f, 120.0f, 11.0f, "SCENE MEMORY", 8.5f, kSilk, juce::Justification::centredLeft, true);
        auto scene = std::make_unique<Readout> (2);
        scene->setBounds (x + 30, 324, 36, 28);
        scene->text = [this] { return juce::String (display.getMemory()).paddedLeft ('0', 2); };
        addAndMakeVisible (*scene);
        readouts.emplace_back ("SCENE", std::move (scene));
        auto& store = key ("STORE", kKeyGrey, kLedClear, Shape::Big, { x + 82, 322, 38, 32 });
        store.onClick = [this] { display.storeScene(); };
        silk ((float) x + 76.0f, 355.0f, 50.0f, 8.0f, "STORE", 6.5f, kSilk);
        auto& up = key ("SCENE UP", kKeyGrey, kLedClear, Shape::Big, { x + 130, 312, 30, 24 });
        up.setGlyph (juce::String::charToString (juce::juce_wchar (0x25b2)));
        up.onClick = [this] { display.setMemory (display.getMemory() + 1); };
        auto& down = key ("SCENE DOWN", kKeyGrey, kLedClear, Shape::Big, { x + 130, 340, 30, 24 });
        down.setGlyph (juce::String::charToString (juce::juce_wchar (0x25bc)));
        down.onClick = [this] { display.setMemory (display.getMemory() - 1); };
        auto& recall = key ("RECALL", kKeyGrey, kLedClear, Shape::Big, { x + 172, 322, 38, 32 });
        recall.onClick = [this] { display.recallScene(); };
        silk ((float) x + 166.0f, 355.0f, 50.0f, 8.0f, "RECALL", 6.5f, kSilk);

        displayKey ("DISPLAY AUTOMIX", Group::Automix, { x, 372, 24, 24 });
        silk ((float) x + 34.0f, 373.0f, 120.0f, 11.0f, "AUTOMIX", 8.5f, kSilk, juce::Justification::centredLeft, true);
        const char* autos[] = { "ENABLE", "REC", "ABORT/UNDO", "AUTO-REC", "RETURN", "RELATIVE", "TOUCH SENSE" };
        using AK = Display::AutomixKey;
        const AK autoKeys[] = { AK::Enable, AK::Rec, AK::Undo };
        const char* autoNames[] = { "AUTOMIX ENABLE", "AUTOMIX REC", "AUTOMIX ABORT/UNDO" };
        for (int i = 0; i < 3; ++i)
            live (autoNames[i], { x + 8 + i * 32, 392, 22, 16 }, [this, k = autoKeys[i]] { display.automixPress (k); }, [this, k = autoKeys[i]] { return display.automixIs (k); });
        drawnKeys ({ x + 8 + 3 * 32, 392, 22, 16 }, 4, 1, 32, 0);
        for (int i = 0; i < 7; ++i)
            silk ((float) x + 8.0f + (float) i * 32.0f - 8.0f, 409.0f, 38.0f, 12.0f, autos[i], 5.8f, kSilk);
        silk ((float) x + 60.0f, 422.0f, 120.0f, 10.0f, "OVERWRITE", 8.0f, kSilk, juce::Justification::centred, true);
        const char* over[] = { "FADER", "ON", "PAN", "SURROUND", "AUX", "AUX ON", "EQ" };
        live ("OVERWRITE FADER", { x + 8, 434, 22, 14 }, [this] { display.automixPress (AK::Fader); }, [this] { return display.automixIs (AK::Fader); });
        live ("OVERWRITE ON", { x + 40, 434, 22, 14 }, [this] { display.automixPress (AK::On); }, [this] { return display.automixIs (AK::On); });
        drawnKeys ({ x + 72, 434, 22, 14 }, 5, 1, 32, 0);
        for (int i = 0; i < 7; ++i)
            silk ((float) x + 8.0f + (float) i * 32.0f - 8.0f, 449.0f, 38.0f, 8.0f, over[i], 5.8f, kSilk);

        displayKey ("DISPLAY USER", Group::UserKeys, { x, 464, 24, 24 });
        silk ((float) x + 34.0f, 465.0f, 170.0f, 11.0f, "USER DEFINED KEYS", 8.5f, kSilk, juce::Justification::centredLeft, true);
        for (int i = 0; i < 16; ++i)
        {
            auto& k = key ("USER " + juce::String (i + 1), kKeyDark, kLedOrange, Shape::Square, { x + 12 + (i % 8) * 27, 490 + (i / 8) * 34, 22, 16 });
            k.isOn = [this, i] { return display.isUserKeyOn (i); };
            k.onClick = [this, i] { display.pressUserKey (i); };
            silk ((float) x + 12.0f + (float) (i % 8) * 27.0f, 506.0f + (float) (i / 8) * 34.0f, 22.0f, 8.0f, juce::String (i + 1), 6.5f, kSilk);
        }

        displayKey ("DISPLAY LOCATOR", Group::Locator, { x, 566, 24, 24 });
        silk ((float) x + 34.0f, 567.0f, 120.0f, 11.0f, "LOCATOR", 8.5f, kSilk, juce::Justification::centredLeft, true);
        silk ((float) x + 60.0f, 585.0f, 120.0f, 10.0f, "LOCATE MEMORY", 8.0f, kSilk, juce::Justification::centred, true);
        for (int i = 0; i < 8; ++i)
            live ("LOCATE " + juce::String (i + 1), { x + 12 + i * 27, 598, 22, 16 }, [this, i] { display.locateMemory (i); });
        for (int i = 0; i < 8; ++i)
            silk ((float) x + 12.0f + (float) i * 27.0f, 614.0f, 22.0f, 8.0f, juce::String (i + 1), 6.5f, kSilk);
        drawnKeys ({ x + 12, 636, 22, 16 }, 7, 1, 27, 0);
        drawnKeys ({ x + 12, 678, 22, 16 }, 7, 1, 27, 0);
        live ("RETURN TO ZERO", { x + 12, 722, 22, 16 }, [this] { if (ctx.recording != nullptr) ctx.recording->locate (0); });
        live ("LOCATE END", { x + 39, 722, 22, 16 }, [this] { if (ctx.recording != nullptr) ctx.recording->locate (ctx.recording->getProject().getLength()); });
        drawnKeys ({ x + 66, 722, 22, 16 }, 1, 1, 0, 0);
        live ("LOCATE LOOP", { x + 93, 722, 22, 16 }, [this] { if (ctx.recording != nullptr) ctx.recording->setLoopEnabled (! ctx.recording->isLoopEnabled()); },
              [this] { return ctx.recording != nullptr && ctx.recording->isLoopEnabled(); });
        drawnKeys ({ x + 120, 722, 22, 16 }, 1, 1, 0, 0);
        live ("ROLL BACK", { x + 147, 722, 22, 16 }, [this] { if (ctx.recording != nullptr) ctx.recording->locate (ctx.recording->getPosition() - juce::int64 (5.0 * ctx.recording->getRecorder().getSampleRate())); });
        drawnKeys ({ x + 174, 722, 22, 16 }, 2, 1, 27, 0);
        const char* locTop[] = { "AUDITION", "PRE", "IN", "OUT", "POST", "SET", "MTR" };
        const char* locBot[] = { "RETURN TO ZERO", "END", "ONLINE", "LOOP", "QUICK PUNCH", "ROLL BACK", "REHEARSAL", "MASTER" };
        for (int i = 0; i < 7; ++i)
            silk ((float) x + 12.0f + (float) i * 27.0f - 6.0f, 654.0f, 34.0f, 16.0f, locTop[i], 5.8f, kSilk);
        for (int i = 0; i < 8; ++i)
            silk ((float) x + 12.0f + (float) i * 27.0f - 6.0f, 738.0f, 34.0f, 16.0f, locBot[i], 5.8f, kSilk);
    }

    // MONITOR.
    {
        const int x = 1759, w = 88;
        displayKey ("DISPLAY MONITOR", Group::Monitor, { x + 4, 150, 22, 22 });
        silk ((float) x + 26.0f, 151.0f, 60.0f, 10.0f, "MONITOR", 8.0f, kSilkDark, juce::Justification::centredLeft, true);
        silk ((float) x, 174.0f, (float) w, 10.0f, "STUDIO", 7.5f, kSilkDark, juce::Justification::centred, true);
        // The studio path (PHONES) listens to the Stereo Bus or an Aux; CONTROL ROOM has nothing to follow here.
        const auto sourceKey = [this, &live] (const juce::String& name, juce::Rectangle<int> r, ld::MonitorSourceParams& path, StripKind kind, int number)
        {
            const int index = MixerEngine::monitorSourceIndex (kind, number);
            live (name, r, [&path, index] { if (index >= 0) path.source = index; }, [&path, index] { return index >= 0 && path.source.load() == index; });
        };
        drawnKeys ({ x + 10, 186, 24, 15 }, 1, 1, 0, 0);
        sourceKey ("STUDIO STEREO", { x + 50, 186, 24, 15 }, ctx.engine.phones.source, StripKind::Main, 0);
        sourceKey ("STUDIO AUX 11", { x + 10, 208, 24, 15 }, ctx.engine.phones.source, StripKind::Aux, 10);
        sourceKey ("STUDIO AUX 12", { x + 50, 208, 24, 15 }, ctx.engine.phones.source, StripKind::Aux, 11);
        const char* top[] = { "CONTROL ROOM", "STEREO", "AUX 11", "AUX 12" };
        for (int i = 0; i < 4; ++i)
            silk ((float) x + 2.0f + (float) (i % 2) * 42.0f, 202.0f + (float) (i / 2) * 22.0f, 42.0f, 7.0f, top[i], 5.8f, kSilkDark);
        silk ((float) x, 232.0f, (float) w, 10.0f, "SOLO", 7.5f, kSilkDark, juce::Justification::centred, true);
        knob ("SOLO CONTRAST", kSkirtTaupe, kTopTaupe, centred (x + w / 2, 243, 28, 28));
        silk ((float) x, 271.0f, (float) w, 8.0f, "SOLO CONTRAST", 6.0f, kSilkDark);
        auto& clear = key ("SOLO CLEAR", kKeyDark, kLedYellow, Shape::Square, centred (x + w / 2, 281, 24, 15));
        clear.onClick = [this]
        {
            for (const auto* list : { &ctx.engine.getChannels(), &ctx.engine.getBuses(), &ctx.engine.getMains(), &ctx.engine.getMatrices() })
                for (const auto& s : *list)
                    s->params.solo = false;
        };
        clear.isOn = [this]
        {
            for (const auto* list : { &ctx.engine.getChannels(), &ctx.engine.getBuses(), &ctx.engine.getMains(), &ctx.engine.getMatrices() })
                for (const auto& s : *list)
                    if (s->params.solo.load())
                        return true;
            return false;
        };
        silk ((float) x, 296.0f, (float) w, 7.0f, "CLEAR", 6.0f, kSilkDark);
        silk ((float) x, 306.0f, (float) w, 10.0f, "CONTROL ROOM", 7.5f, kSilkDark, juce::Justification::centred, true);
        drawnKeys ({ x + 10, 320, 24, 15 }, 2, 2, 40, 25);
        drawnKeys ({ x + 10, 370, 24, 15 }, 1, 1, 0, 0);
        sourceKey ("CR STEREO", { x + 50, 370, 24, 15 }, ctx.engine.speakers, StripKind::Main, 0);
        drawnKeys ({ x + 10, 395, 24, 15 }, 2, 1, 40, 0);
        const char* crs[] = { "2TR D1", "2TR A1", "2TR D2", "2TR A2", "2TR D3", "STEREO", "ASSIGN 1", "ASSIGN 2" };
        for (int i = 0; i < 8; ++i)
            silk ((float) x + 2.0f + (float) (i % 2) * 42.0f, 336.0f + (float) (i / 2) * 25.0f, 42.0f, 7.0f, crs[i], 5.8f, kSilkDark);
        silk ((float) x, 424.0f, (float) w, 9.0f, "SURROUND", 7.0f, kSilkDark, juce::Justification::centred, true);
        drawnKeys ({ x + 10, 436, 24, 15 }, 2, 1, 40, 0);
        drawnKeys ({ x + 50, 456, 24, 15 }, 1, 1, 0, 0);
        knob ("SURROUND LEVEL", kSkirtTaupe, kTopTaupe, centred (x + w / 2, 472, 34, 34));
        silk ((float) x, 508.0f, (float) w, 14.0f, "SURROUND\nMONITOR LEVEL", 5.8f, kSilkDark);
        auto& mono = key ("MONO", kKeyDark, kLedOrange, Shape::Square, { x + 10, 526, 24, 15 });
        mono.isOn = [this] { return ctx.engine.monitorMono.load(); };
        mono.onClick = [this] { ctx.engine.monitorMono = ! ctx.engine.monitorMono.load(); };
        silk ((float) x + 2.0f, 542.0f, 42.0f, 7.0f, "MONO", 5.8f, kSilkDark);
        auto& dim = key ("DIMMER", kKeyDark, kLedOrange, Shape::Square, { x + 50, 526, 24, 15 });
        dim.isOn = [this] { return ctx.engine.monitorDim.load(); };
        dim.onClick = [this] { ctx.engine.monitorDim = ! ctx.engine.monitorDim.load(); };
        silk ((float) x + 44.0f, 542.0f, 42.0f, 7.0f, "DIMMER", 5.8f, kSilkDark);
        drawnKeys ({ x + 10, 554, 24, 15 }, 1, 1, 0, 0);
        silk ((float) x + 2.0f, 570.0f, 42.0f, 7.0f, "SMALL", 5.8f, kSilkDark);
        knob ("CONTROL ROOM LEVEL", kSkirtTaupe, juce::Colour (0xffc7c0a8), centred (x + w / 2, 584, 40, 40)).setBinding (bindFader ("CR", ctx.engine.monitorLevelDb));
        silk ((float) x, 626.0f, (float) w, 14.0f, "CONTROL ROOM\nLEVEL", 5.8f, kSilkDark);
        silk ((float) x, 646.0f, (float) w, 10.0f, "TALKBACK", 7.5f, kSilkDark, juce::Justification::centred, true);
        drawnKeys ({ x + 30, 660, 28, 15 }, 1, 1, 0, 0);
        silk ((float) x, 677.0f, (float) w, 7.0f, "SLATE", 5.8f, kSilkDark);
        auto& talk = key ("TALKBACK", kKeyGrey, kLedRed, Shape::Big, centred (x + w / 2, 692, 42, 46));
        talk.isOn = [this] { return ctx.engine.isTalkOn (0); };
        talk.onClick = [this] { ctx.engine.talk.paths[0].on = ! ctx.engine.talk.paths[0].on.load(); };
        silk ((float) x, 740.0f, (float) w, 8.0f, "TALKBACK", 6.2f, kSilkDark);
    }

    // The transport, the wheel, the cursor keys (pp.42-44).
    {
        const int x = 1520;
        // The transport: its MMC commands on the console's MIDI (p.256's MMC; no machine answers here).
        const char* names2[] = { "REW", "FF", "STOP", "PLAY" };
        const int commands[] = { 0x05, 0x04, 0x01, 0x02 };
        for (int i = 0; i < 4; ++i)
            key (names2[i], kKeyGrey, kLedClear, Shape::Big, { x + 34 + i * 60, 806, 40, 34 }).onClick = [this, command = commands[i]] { sendMmc (command); display.transport (command); };
        auto& rec = key ("REC", kKeyGrey, kLedRed, Shape::Big, { x + 276, 806, 40, 34 });
        rec.onClick = [this] { sendMmc (0x06); display.transport (0x06); };
        rec.isOn = [this] { return ctx.recording != nullptr && ctx.recording->isRecording(); };
        for (int i = 0; i < 4; ++i)
            silk ((float) x + 34.0f + (float) i * 60.0f, 842.0f, 40.0f, 10.0f, names2[i], 7.5f, kSilk);
        silk ((float) x + 276.0f, 842.0f, 40.0f, 10.0f, "REC", 7.5f, kSilk);

        silk ((float) x + 42.0f, 862.0f, 104.0f, 10.0f, "DISPLAY HISTORY", 7.0f, kSilk);
        auto& back = key ("BACK", kKeyDark, kLedClear, Shape::Square, { x + 54, 876, 28, 18 });
        back.onClick = [this] { display.history (-1); };
        silk ((float) x + 46.0f, 895.0f, 44.0f, 8.0f, "BACK", 6.5f, kSilk);
        auto& fwd = key ("FORWARD", kKeyDark, kLedClear, Shape::Square, { x + 104, 876, 28, 18 });
        fwd.onClick = [this] { display.history (1); };
        silk ((float) x + 96.0f, 895.0f, 44.0f, 8.0f, "FORWARD", 6.5f, kSilk);
        drawnKeys ({ x + 36, 912, 26, 18 }, 1, 2, 0, 34);
        silk ((float) x + 28.0f, 931.0f, 42.0f, 8.0f, "SHUTTLE", 6.5f, kSilk);
        silk ((float) x + 28.0f, 965.0f, 42.0f, 8.0f, "SCRUB", 6.5f, kSilk);

        wheel.setBounds (x + 84, 914, 134, 134);

        const auto arrow = [this] (const juce::String& name, const juce::String& glyph, juce::Rectangle<int> r, std::function<void()> f)
        {
            auto& k = key (name, kKeyGrey, kLedClear, Shape::Big, r);
            k.setGlyph (glyph);
            k.onClick = std::move (f);
            return &k;
        };
        arrow ("DEC", {}, { x + 240, 900, 44, 36 }, [this] { display.turn (-1); });
        silk ((float) x + 240.0f, 937.0f, 44.0f, 8.0f, "DEC", 7.0f, kSilk);
        arrow ("INC", {}, { x + 330, 900, 44, 36 }, [this] { display.turn (1); });
        silk ((float) x + 330.0f, 937.0f, 44.0f, 8.0f, "INC", 7.0f, kSilk);
        const auto ch = [] (int u) { return juce::String::charToString ((juce::juce_wchar) u); };
        arrow ("UP", ch (0x25b2), { x + 286, 942, 40, 34 }, [this] { display.cursor (0, -1); });
        arrow ("LEFT", ch (0x25c0), { x + 240, 980, 40, 34 }, [this] { display.cursor (-1, 0); });
        arrow ("RIGHT", ch (0x25b6), { x + 332, 980, 40, 34 }, [this] { display.cursor (1, 0); });
        arrow ("DOWN", ch (0x25bc), { x + 286, 1018, 40, 34 }, [this] { display.cursor (0, 1); });
        auto* enter = arrow ("ENTER", {}, { x + 138, 1052, 44, 22 }, [this] { display.enter(); });
        juce::ignoreUnused (enter);
        silk ((float) x + 186.0f, 1056.0f, 44.0f, 12.0f, "ENTER", 7.5f, kSilk, juce::Justification::centredLeft);
    }

    bindStrips();
    bindSelected();
    refreshNames();
    startTimerHz (30);
}

Dm2000Surface::~Dm2000Surface() = default;

bool Dm2000Surface::sendMmc (int command)
{
    if (auto* midi = dynamic_cast<Dm2000Midi*> (ctx.consoleMidi))
    {
        midi->mmc (command);
        return true;
    }
    display.showMessage ("The console's MIDI isn't running.");
    return false;
}

void Dm2000Surface::autoShow (Group g)
{
    if (display.getGroup() != g && display.autoDisplay (g))
        display.showGroup (g);
}

void Dm2000Surface::autoSelect (int strip)
{
    // Auto Channel Select (p.275): a fader moved, a [SOLO] or an [ON] pressed selects its channel.
    if (auto* s = stripAt (strip); s != nullptr && s != selected() && ctx.selectStrip && display.preference (Display::Pref::AutoChannelSelect))
        ctx.selectStrip (s);
}

//==============================================================================
Strip* Dm2000Surface::stripAt (int i) const
{
    if (! juce::isPositiveAndBelow (i, kStrips))
        return nullptr;
    if (layer <= Layer73)
    {
        const auto& c = ctx.engine.getChannels();
        const int n = layer * kStrips + i;
        return n < (int) c.size() ? c[(size_t) n].get() : nullptr;
    }
    if (layer == LayerMaster)
    {
        const auto& b = ctx.engine.getBuses();
        if (i < (int) b.size())
            return b[(size_t) i].get();
        const auto& m = ctx.engine.getMatrices();
        const int n = i - (int) b.size();
        return n < (int) m.size() ? m[(size_t) n].get() : nullptr;
    }
    return nullptr; // the remote layers have no targets on Sidecar
}

Strip* Dm2000Surface::stereoStrip() const
{
    const auto& m = ctx.engine.getMains();
    return m.empty() ? nullptr : m[0].get();
}

void Dm2000Surface::setLayer (int l)
{
    layer = juce::jlimit (0, (int) LayerRemote4, l);
    bindStrips();
    repaint();
}

void Dm2000Surface::setFaderMode (FaderMode m)
{
    faderMode = m;
    bindStrips();
}

void Dm2000Surface::setEncoderMode (EncoderMode m)
{
    encoderMode = m;
    bindStrips();
}

void Dm2000Surface::setAuxBank (int bank)
{
    auxBank = juce::jlimit (0, 2, bank);
    bindSelected();
    repaint();
}

void Dm2000Surface::setDynamicsShowsComp (bool c)
{
    showComp = c;
    bindSelected();
    repaint();
}

void Dm2000Surface::bindStrips()
{
    const auto inputLaw = ctx.engine.getFaderLaw ((int) StripKind::Channel);
    const auto masterLaw = ctx.engine.getFaderLaw ((int) StripKind::Bus);
    const int aux = kAuxSlot + display.getAux(), matrix = display.getMatrix();
    for (int i = 0; i < kStrips; ++i)
    {
        auto* s = stripAt (i);
        std::atomic<float>* value = nullptr;
        KnobBinding enc;
        const auto& law = s != nullptr && s->isOutput() ? masterLaw : inputLaw;
        if (s != nullptr)
        {
            const bool sends = faderMode == FaderMode::AuxMtrx;
            if (! sends)
                value = &s->params.faderDb;
            else if (s->isInput())
                value = &s->sends[(size_t) aux].levelDb;
            else if (s->getKind() == StripKind::Bus)
                value = &s->matrix[(size_t) matrix].levelDb;

            switch (encoderMode)
            {
                case EncoderMode::Pan:
                    if (s->isInput())
                        enc = bindPanYamaha (*s);
                    else if (s->getKind() == StripKind::Matrix)
                        enc = bindLinear ("BALANCE", s->params.input.balanceDb, -12.0f, 12.0f, " dB", 1, 0.0f);
                    break;
                case EncoderMode::AuxMtrx:
                    if (s->isInput())
                        enc = bindFader ("AUX", s->sends[(size_t) aux].levelDb);
                    else if (s->getKind() == StripKind::Bus)
                        enc = bindFader ("MATRIX", s->matrix[(size_t) matrix].levelDb);
                    break;
                case EncoderMode::Assign1:
                case EncoderMode::Assign2:
                case EncoderMode::Assign3:
                case EncoderMode::Assign4:
                    enc = bindAssign (ctx, *s, display.getAssign ((int) encoderMode - (int) EncoderMode::Assign1));
                    break;
            }
        }
        faders[(size_t) i]->bind (value, law);
        encoders[(size_t) i]->setBinding (std::move (enc));
    }
    if (auto* s = stereoStrip())
        stereoFader->bind (&s->params.faderDb, ctx.engine.getFaderLaw ((int) StripKind::Main));
}

void Dm2000Surface::bindSelected()
{
    auto* s = selected();
    boundStrip = s;
    const auto set = [this] (const juce::String& name, KnobBinding b) { if (auto* k = findKnob (name)) k->setBinding (std::move (b)); };

    // Touching a control brings its page up when the group isn't showing, if PREFER1's Auto ...
    // Display for it is on (p.274).
    const auto autoGroup = [this] (KnobBinding b, Group g)
    {
        if (b.isValid() && b.set)
            b.set = [this, set2 = b.set, g] (float v)
            {
                set2 (v);
                autoShow (g);
            };
        return b;
    };

    if (s == nullptr)
    {
        for (const auto* name : { "DELAY TIME", "ATT", "PAN" })
            set (name, {});
        return;
    }
    auto& p = s->params;
    set ("DELAY TIME", autoGroup (bindDelay (*s), Group::Delay));
    set ("ATT", autoGroup (bindAttenuator (*s), Group::Equalizer));
    set ("PAN", s->isInput() ? autoGroup (bindPanYamaha (*s), Group::Pan) : KnobBinding {});

    // AUX / MATRIX SEND's four LEVELs (p.33).
    for (int k = 0; k < 4; ++k)
    {
        if (s->isInput())
            set ("LEVEL " + juce::String (k + 1), autoGroup (bindFader ("LEVEL", s->sends[(size_t) (kAuxSlot + auxBank * 4 + k)].levelDb), Group::AuxSend));
        else if (auxBank == 0)
            set ("LEVEL " + juce::String (k + 1), autoGroup (bindFader ("LEVEL", s->matrix[(size_t) k].levelDb), Group::MatrixSend));
        else
            set ("LEVEL " + juce::String (k + 1), {});
    }

    // DYNAMICS: GATE's or COMP's five.
    {
        const auto knobFor = [this, s, &p, set, autoGroup] (int k, const char* gateId, const char* compId)
        {
            auto& d = showComp || ! s->isInput() ? p.dynamics() : p.gate();
            const auto* id = showComp || ! s->isInput() ? compId : gateId;
            const auto& m = models::model (models::Dynamics, d.model.load());
            const int i = m.find (id);
            set ("DYN " + juce::String (k + 1), i >= 0 ? autoGroup (bindModelParam (d.values[i], m.params[(size_t) i]), Group::Dynamics) : KnobBinding {});
        };
        knobFor (0, "thr", "thr");
        knobFor (1, "range", "ratio");
        knobFor (2, "att", "att");
        knobFor (3, "dcy", "rel");
        knobFor (4, "hld", "gain");
    }

    // EQUALIZER: F or Q on the push knob, G.
    {
        auto& eq = p.eq();
        const auto table = s->isOutput() ? models::BusEq : models::ChannelEq;
        const auto& m = models::model (table, eq.model.load());
        const char* ids[] = { "l", "lm", "hm", "h" };
        for (int b = 0; b < 4; ++b)
        {
            const int fi = m.find (juce::String (ids[b]) + (display.isFreqNotQ (b) ? "f" : "q")), gi = m.find (juce::String (ids[b]) + "g");
            set ("EQ F " + juce::String (b + 1), fi >= 0 ? autoGroup (bindModelParam (eq.values[fi], m.params[(size_t) fi]), Group::Equalizer) : KnobBinding {});
            if (gi >= 0)
            {
                auto g = autoGroup (bindModelParam (eq.values[gi], m.params[(size_t) gi]), Group::Equalizer);
                g.set = [this, b, set2 = g.set] (float v)
                {
                    set2 (v);
                    eqGainMoved[(size_t) b] = juce::Time::getMillisecondCounter();
                };
                set ("EQ G " + juce::String (b + 1), std::move (g));
            }
            else
                set ("EQ G " + juce::String (b + 1), {});
        }
    }
}

void Dm2000Surface::bindSoft()
{
    for (int i = 0; i < 4; ++i)
        if (auto* soft = findKnob ("SOFT " + juce::String (i + 1)))
            soft->setBinding (display.getSoftBinding (i));
}

void Dm2000Surface::refreshNames()
{
    const auto now = juce::Time::getMillisecondCounter();
    for (int i = 0; i < kStrips; ++i)
    {
        StripDisplay::State st;
        auto* s = stripAt (i);
        if (s != nullptr)
        {
            auto& p = s->params;
            st.name = s->getName();
            st.selected = s == selected();
            st.muted = p.mute.load();
            st.eq = p.eq().on.load();
            st.ins = p.preInsert().on.load();
            st.dly = p.input.delayOn.load();
            st.comp = p.dynamics().on.load();
            st.gate = s->isInput() && p.gate().on.load();
            if (s->isInput())
            {
                for (int b = 0; b < 8; ++b)
                    if (auto* send = s->sends.find (b))
                        st.routes |= send->enabled.load() && send->levelDb.load() > -89.5f ? 1 << b : 0;
                if (s->mains[0].on.load())
                    st.routes |= 1 << 8;
            }
            const auto& b = encoders[(size_t) i]->getBinding();
            st.arc = b.isValid();
            if (st.arc)
            {
                st.position = b.get();
                st.bipolar = b.bipolar;
            }
            if (now - movedAt[(size_t) i] < 1000)
            {
                if (encoderMode == EncoderMode::Pan || b.isValid())
                    st.value = lcdValue (b).substring (0, 4);
                if (faders[(size_t) i]->isBound() && faders[(size_t) i]->getPosition() >= 0.0 && st.value.isEmpty())
                    st.value = fader::text (fader::posToDb ((float) faders[(size_t) i]->getPosition())).substring (0, 4);
            }
        }
        names[(size_t) i]->set (st);
    }
}

void Dm2000Surface::refreshAll()
{
    bindStrips();
    bindSelected();
    display.rebuild();
    repaint();
}

void Dm2000Surface::selectionChanged()
{
    bindSelected();
    display.selectionChanged();
}

void Dm2000Surface::showChanged() { display.refresh(); }

void Dm2000Surface::timerCallback()
{
    if (isCovered())
        return;
    if (boundStrip != selected())
        selectionChanged();
    display.automixTick();
    display.followPairs();
    for (auto& f : faders)
        f->refresh();
    stereoFader->refresh();
    for (auto& k : encoders)
        k->refresh();
    for (auto& [n, k] : keys)
        k->refresh();
    for (auto& [n, k] : knobs)
        k->refresh();
    for (auto& [n, r] : readouts)
        r->refresh();
    refreshNames();
    display.refresh();
    universalKey.refresh();
}

void Dm2000Surface::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d0e12));
    g.addTransform (design);

    const auto top = juce::Rectangle<float> (0.0f, 0.0f, (float) kDesignWidth, (float) kTopBar);
    g.setColour (theme.panel);
    g.fillRect (top);
    g.setColour (theme.text);
    g.setFont (type::font (type::kTitle, true));
    g.drawText ("DM2000", top.reduced ((float) sizes::kGapLarge, 0.0f), juce::Justification::centredLeft);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption));
    g.drawText ("Not affiliated with or endorsed by any console maker; product names are their owners' trademarks.", top.reduced ((float) sizes::kGapLarge, 0.0f).withTrimmedLeft (120.0f).withTrimmedRight (260.0f), juce::Justification::centredLeft);

    // The panel: blue-black, with wooden cheeks.
    const juce::Rectangle<float> body (0.0f, (float) kTopBar, (float) kDesignWidth, (float) (kDesignHeight - kTopBar));
    g.setGradientFill (juce::ColourGradient (kPanel, 0.0f, body.getY(), kPanelLow, 0.0f, body.getBottom(), false));
    g.fillRect (body);
    for (float x : { 0.0f, (float) kDesignWidth - 18.0f })
    {
        const juce::Rectangle<float> cheek (x, body.getY(), 18.0f, body.getHeight());
        g.setGradientFill (juce::ColourGradient (kWoodLight, cheek.getX(), 0.0f, kWood, cheek.getRight(), 0.0f, false));
        g.fillRect (cheek);
    }

    // Lines dividing the strips, top to bottom, in pale silkscreen.
    g.setColour (kLine.withAlpha (0.5f));
    for (int i = 0; i < kStrips; ++i)
        if (i % 8 != 0)
            g.fillRect ((float) stripCx (i) - 24.5f, 524.0f, 1.0f, (float) kVfdY - 526.0f);
    for (int i = 0; i < kStrips; ++i)
        if (i % 8 != 0)
            g.fillRect ((float) stripCx (i) - 24.5f, (float) kWriteY, 1.0f, (float) kFaderH);
    for (int i = 1; i < 24; ++i)
        g.fillRect (haCx (i) - 26.5f, (float) kHaTop + 4.0f, 1.0f, (float) (kCenterTop - kHaTop) - 8.0f);

    // Sections on the panel: black panels on the left and in the middle, the blue SELECTED CHANNEL.
    const auto blackPanel = [&g] (juce::Rectangle<float> r)
    {
        g.setColour (kBlack);
        g.fillRoundedRectangle (r, 3.0f);
    };
    blackPanel ({ 26.0f, 200.0f, 256.0f, 310.0f });
    blackPanel ({ 290.0f, 148.0f, 200.0f, 362.0f });
    blackPanel ({ 505.0f, 148.0f, 448.0f, 366.0f });

    // SmartMedia card slot.
    {
        const juce::Rectangle<float> slot (96.0f, 150.0f, 130.0f, 42.0f);
        g.setColour (juce::Colour (0xff0a0b0e));
        g.fillRoundedRectangle (slot, 3.0f);
        g.setColour (kLine);
        g.drawRoundedRectangle (slot, 3.0f, 1.0f);
        g.setColour (kSilkDim);
        g.setFont (type::font (8.0f));
        g.drawText ("3.3V CARD", slot.withTrimmedTop (44.0f).withHeight (10.0f), juce::Justification::centred, false);
    }

    // SELECTED CHANNEL.
    const juce::Rectangle<float> sel ((float) kSelX, (float) kCenterTop + 2.0f, (float) kSelW, (float) (kCenterBottom - kCenterTop - 2));
    g.setGradientFill (juce::ColourGradient (kBlueTop, 0.0f, sel.getY(), kBlueLow, 0.0f, sel.getBottom(), false));
    g.fillRoundedRectangle (sel, 3.0f);
    g.setColour (juce::Colour (0xffb6bcc4));
    g.fillRect (sel.getX(), sel.getY(), sel.getWidth(), 11.0f);
    hw::silk (g, { sel.getX(), sel.getY(), sel.getWidth(), 11.0f }, "SELECTED CHANNEL", hw::Look::y02r(), 8.5f, juce::Justification::centred);
    // The sections' black rules.
    g.setColour (kBlack.withAlpha (0.85f));
    g.fillRect ((float) kSelX + 150.0f, sel.getY() + 11.0f, 3.0f, 133.0f);
    g.fillRect ((float) kSelX + 150.0f, 200.0f, (float) kSelW - 150.0f, 3.0f);
    g.fillRect ((float) kSelX + 322.0f, sel.getY() + 11.0f, 3.0f, 42.0f);
    g.fillRect (sel.getX(), 300.0f, sel.getWidth(), 3.0f);
    g.fillRect (sel.getX(), 351.0f, sel.getWidth(), 3.0f);
    g.fillRect (sel.getX(), 396.0f, sel.getWidth(), 3.0f);
    // The bars the AUX bank's and GATE/COMP's legends sit on.
    {
        g.setColour (juce::Colour (0xffb6bcc4).withAlpha (0.65f));
        g.fillRect ((float) kSelX + 194.0f, 272.0f, (float) kSelW - 194.0f, 26.0f);
        g.setColour (kLedOrange);
        for (int b = 0; b < 3; ++b)
        {
            g.setColour (b == auxBank ? kLedOrange : juce::Colour (0xff3a2a14));
            g.fillEllipse ((float) kSelX + 186.0f, 273.0f + (float) b * 8.0f, 5.0f, 5.0f);
        }
        g.setColour (showComp ? juce::Colour (0xff3a2a14) : kLedOrange);
        g.fillEllipse ((float) kSelX + 186.0f, 337.0f, 5.0f, 5.0f);
        g.setColour (showComp ? kLedOrange : juce::Colour (0xff3a2a14));
        g.fillEllipse ((float) kSelX + 186.0f, 346.0f, 5.0f, 5.0f);
    }
    // PAN's LED bar.
    {
        auto* s = selected();
        const float pan = s != nullptr ? s->params.pan.load() : 0.0f;
        const juce::Rectangle<float> bar ((float) kSelX + 130.0f, 370.0f, 100.0f, 14.0f);
        g.setColour (juce::Colour (0xff1a1c20));
        g.fillRect (bar);
        const int lit = juce::roundToInt ((pan + 100.0f) / 200.0f * 12.0f);
        for (int i = 0; i < 13; ++i)
        {
            g.setColour (i == lit ? kLedOrange : juce::Colour (0xff4a3a24));
            g.fillRect (bar.getX() + 2.0f + (float) i * 7.4f, bar.getY() + 2.0f, 5.5f, 10.0f);
        }
        g.setColour (kSilk);
        g.setFont (type::font (6.5f));
        g.drawText ("L ODD", juce::Rectangle<float> (bar.getX() - 22.0f, bar.getY(), 22.0f, 14.0f), juce::Justification::centredRight, false);
        g.drawText ("R EVEN", juce::Rectangle<float> (bar.getRight() + 1.0f, bar.getY(), 28.0f, 14.0f), juce::Justification::centredLeft, false);
    }
    // The EQ's F/Q lamps, its readouts' windows, the joystick.
    for (int b = 0; b < 4; ++b)
    {
        const float x = (float) (kSelX + 108 + b * 108);
        g.setColour (juce::Colour (0xff0d0e12));
        g.fillRoundedRectangle (x + 2.0f, 474.0f, 100.0f, 34.0f, 3.0f);
        g.setColour (display.isFreqNotQ (b) ? kLedOrange : juce::Colour (0xff3a2a14));
        g.fillEllipse (x - 6.0f, 447.0f, 5.0f, 5.0f);
        g.setColour (display.isFreqNotQ (b) ? juce::Colour (0xff3a2a14) : kLedOrange);
        g.fillEllipse (x - 6.0f, 456.0f, 5.0f, 5.0f);
        g.setColour (kSilk);
        g.setFont (type::font (6.5f));
        for (int row = 0; row < 3; ++row)
        {
            static const char* const u[] = { "dB", "Hz", "kHz" };
            g.setColour (kKeyGrey);
            g.fillRect (x + 70.0f, 478.0f + (float) row * 9.5f, 8.0f, 6.0f);
            g.setColour (kSilk);
            g.drawText (u[row], juce::Rectangle<float> (x + 80.0f, 476.0f + (float) row * 9.5f, 22.0f, 9.5f), juce::Justification::centredLeft, false);
        }
    }
    {
        const juce::Rectangle<float> joy ((float) kSelX + 448.0f, 305.0f, 86.0f, 88.0f);
        g.setColour (juce::Colour (0xff121419));
        g.fillRoundedRectangle (joy, 4.0f);
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (joy.reduced (22.0f, 16.0f), 3.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffdedfe0), joy.getCentreX() - 6.0f, joy.getCentreY() - 30.0f, juce::Colour (0xff77797c), joy.getCentreX() + 6.0f, joy.getCentreY(), false));
        g.fillRoundedRectangle (juce::Rectangle<float> (14.0f, 44.0f).withCentre (joy.getCentre().translated (0.0f, -12.0f)), 6.0f);
    }

    // The LCD's rim: black housing, its bright cyan margin.
    {
        const juce::Rectangle<float> rim ((float) kLcdX - 8.0f, (float) kLcdY - 8.0f, (float) kLcdW + 16.0f, (float) kLcdH + 16.0f);
        g.setColour (juce::Colour (0xff25262a));
        g.fillRoundedRectangle (rim.expanded (6.0f), 4.0f);
        g.setColour (juce::Colour (0xff35c8d8));
        g.fillRect (rim);
    }

    // Layer, Stereo and the right-hand columns.
    const auto greyPanel = [&g] (juce::Rectangle<float> r)
    {
        g.setGradientFill (juce::ColourGradient (kGrey, r.getX(), r.getY(), kGreyDark, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 2.0f);
    };
    greyPanel ({ 1376.0f, 522.0f, 126.0f, 180.0f });
    greyPanel ({ 1376.0f, 704.0f, 126.0f, 92.0f });
    greyPanel ({ 1370.0f, 800.0f, 132.0f, 272.0f });
    hw::silk (g, { 1376.0f, 522.0f, 126.0f, 10.0f }, "LAYER", hw::Look::y02r(), 8.5f, juce::Justification::centred);
    hw::silk (g, { 1376.0f, 704.0f, 126.0f, 10.0f }, "STEREO", hw::Look::y02r(), 8.5f, juce::Justification::centred);
    blackPanel ({ 1508.0f, 148.0f, 246.0f, 648.0f });
    greyPanel ({ 1756.0f, 146.0f, 96.0f, 650.0f });

    // The transport's panel, with its printed line.
    {
        const juce::Rectangle<float> r (1508.0f, 800.0f, 386.0f, 272.0f);
        g.setColour (kPanelLow.brighter (0.06f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (kLine.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        blackPanel ({ r.getX() + 24.0f, r.getY() + 2.0f, 310.0f, 54.0f });
        g.setColour (kLine.withAlpha (0.6f));
        g.drawRect (juce::Rectangle<float> (1558.0f, 870.0f, 112.0f, 38.0f), 1.0f);
    }

    // The drawn keys: functions Sidecar doesn't have, shown so the panel is the console's.
    for (const auto& r : dummyKeys)
    {
        g.setColour (juce::Colour (0xff0d0e11));
        g.fillRoundedRectangle (r, 2.5f);
        auto body = r.reduced (1.5f);
        g.setGradientFill (juce::ColourGradient (kKeyDark.brighter (0.2f), 0.0f, body.getY(), kKeyDark.darker (0.4f), 0.0f, body.getBottom(), false));
        g.fillRoundedRectangle (body, 2.0f);
        g.setColour (kKeyDark.darker (0.6f));
        g.fillRoundedRectangle (juce::Rectangle<float> (body.getCentreX() - 5.0f, body.getY() + 3.0f, 10.0f, 2.5f), 1.0f);
    }

    // The fluorescent name windows and the white write-on strips.
    for (int group = 0; group < 3; ++group)
    {
        const float x0 = (float) stripCx (group * 8) - 24.5f, x1 = (float) stripCx (group * 8 + 7) + 24.5f;
        const juce::Rectangle<float> w (x0, (float) kVfdY, x1 - x0, (float) kVfdH);
        g.setGradientFill (juce::ColourGradient (kVfdGlass, 0.0f, w.getY(), kVfdGlassEdge, 0.0f, w.getBottom(), false));
        g.fillRect (w);
        g.setColour (juce::Colours::black);
        g.drawRect (w, 1.0f);
        g.setColour (kWriteStrip);
        g.fillRect (x0 + 2.0f, (float) kWriteY, x1 - x0 - 4.0f, 16.0f);
        g.setColour (kSilkDim.withAlpha (0.8f));
        for (int i = 1; i < 8; ++i)
            g.fillRect (x0 + (float) i * 49.0f - 0.5f, (float) kWriteY, 1.0f, 16.0f);
    }

    for (const auto& s : silks)
    {
        if (s.text.isEmpty())
            continue;
        g.setColour (s.colour);
        g.setFont (type::font (s.size, s.bold));
        if (s.text.containsChar ('\n'))
            g.drawFittedText (s.text, s.area.toNearestInt(), s.justification, 3);
        else
            g.drawText (s.text, s.area, s.justification, false);
    }
}

void Dm2000Surface::resized()
{
    const float s = juce::jmin ((float) getWidth() / (float) kDesignWidth, (float) getHeight() / (float) kDesignHeight);
    design = s > 0.0f ? juce::AffineTransform::scale (s).translated (((float) getWidth() - (float) kDesignWidth * s) * 0.5f, ((float) getHeight() - (float) kDesignHeight * s) * 0.5f)
                      : juce::AffineTransform();
    for (auto* c : getChildren())
        c->setTransform (design);
    universalKey.setBounds (juce::Rectangle<int> (kDesignWidth - 190, 0, 190, kTopBar).reduced (sizes::kGap, 5));
    display.setBounds (0, 0, Display::kWidth, Display::kHeight);
    display.setTransform (juce::AffineTransform::scale ((float) kLcdW / (float) Display::kWidth).translated ((float) kLcdX, (float) kLcdY).followedBy (design));
}

} // namespace livedaw::ui
