#include "ui/Y01VSurface.h"

#include "engine/DirectOut.h"
#include "engine/MixerEngine.h"
#include "engine/Models.h"
#include "engine/Y01VMidi.h"
#include "engine/Yamaha01V.h"
#include "ui/DotFont.h"
#include "ui/MeterBank.h"
#include "ui/universal/Groups.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace livedaw::ui
{

using namespace universal;

namespace
{
    // The panel's colours, from photographs of a real 01V.
    const juce::Colour kPanelTop (0xffb9bcbd), kPanelBottom (0xffa6a9ab), kPlate (0xff46494e), kPlateLine (0xff303236), kNavy (0xff2a3a5c),
        kWhiteSilk (0xffe9ebee), kBlueSilk (0xff6f8fc4);
    const juce::Colour kCream (0xffead9b1), kSoloGrey (0xffaeab9c), kOnGrey (0xff4e4d4c), kDark (0xff5a5651), kBlueGrey (0xff93a5b1),
        kAmber (0xfff0ab2d), kSeafoam (0xff72b89e), kMemory (0xfff0603f), kPadGrey (0xff6f7070);
    const juce::Colour kCapCream (0xffe9d8ad), kCapDark (0xff5a5856), kCapRed (0xffe9553a), kLedRed (0xffff3b6b), kKnobDark (0xff4c4c4a),
        kKnobTeal (0xff5fae95), kKnobBlue (0xff8c9fad);
    const juce::Colour kLcd (0xffb2e31e), kLcdPixel (0xff2a5206);

    const juce::Colour kSilverStripe (0xffc8cacb);

    constexpr int kTopBar = 56, kStripPitch = 94, kFirstCx = 78, kMasterCx = 1404, kReturnCx = 1504, kReturnPitch = 90, kRightCx = 1790;
    /** Where the 960 x 240 LCD sits on the panel. */
    constexpr int kLcdX = 451, kLcdY = 168;
    constexpr int kKeyW = 62, kKeyH = 38;
    // The strips' rows, down the panel.
    constexpr int kSelY = 496, kSoloY = 552, kOnY = 608, kHatchY = 654, kFaderY = 692, kFaderBottom = 1040, kBandTop = 156, kBandBottom = 432;

    int stripCx (int i) { return kFirstCx + i * kStripPitch; }

    /** The first bus slot of AUX 1-4 and of EFFECT 1-2 (livedaw_engine::profile::y01v). */
    constexpr int kAuxBus = 4, kEffectBus = 8;
    /** The channels 17-24 (0-based, after the 12 mono and two stereo). */
    constexpr int kOptionChannel = 14;

    int busOf (y01v::FaderMode m) noexcept
    {
        using M = y01v::FaderMode;
        switch (m)
        {
            case M::Aux1:
            case M::Aux2:
            case M::Aux3:
            case M::Aux4:    return kAuxBus + (int) m - (int) M::Aux1;
            case M::Effect1:
            case M::Effect2: return kEffectBus + (int) m - (int) M::Effect1;
            case M::Home:
            case M::OptionIO:
            case M::Remote:  break;
        }
        return -1;
    }

    KnobBinding bindBool (const juce::String& label, std::atomic<bool>& v, const juce::String& on = "ON", const juce::String& off = "OFF")
    {
        KnobBinding b;
        b.label = label;
        b.get = [&v] { return v.load() ? 1.0f : 0.0f; };
        b.set = [&v] (float x) { v = x >= 0.5f; };
        b.text = [&v, on, off] { return v.load() ? on : off; };
        b.steps = 2.0f;
        return b;
    }

    KnobBinding bindParam (models::Table table, int model, std::atomic<float>* values, const juce::String& id, const juce::String& label)
    {
        const auto& m = models::model (table, model);
        const int i = m.find (id);
        if (i < 0)
            return {};
        auto b = bindModelParam (values[i], m.params[(size_t) i]);
        b.label = label;
        return b;
    }

    /** A value as the LCD prints it: "125", "1.00k", "-15.5dB". */
    juce::String lcdValue (const KnobBinding& b)
    {
        if (! b.isValid() || ! b.text)
            return {};
        auto s = b.text().replace (" kHz", "k").replace ("kHz", "k").replace (" Hz", "").replace ("Hz", "").replace (" ", "");
        // Three figures: 10.08k prints 10.0k. A gain has its sign, a Q under 1 two decimals.
        if (s.endsWithChar ('k') && s.getFloatValue() >= 10.0f && s.fromFirstOccurrenceOf (".", false, false).length() > 2)
            s = s.upToFirstOccurrenceOf (".", true, false) + s.fromFirstOccurrenceOf (".", false, false).substring (0, 1) + "k";
        if (s.endsWith ("dB") && s.getFloatValue() > 0.0f && ! s.startsWithChar ('+'))
            s = "+" + s;
        if (s.length() == 3 && s.startsWith ("0."))
            s << "0";
        return s;
    }

    /** A routing switch: a bus has no send level on a 01V, it takes the channel at 0 dB. */
    KnobBinding bindRoute (ld::SendParams& bus, const juce::String& name)
    {
        KnobBinding b;
        b.label = "ROUTE " + name;
        b.steps = 2.0f;
        b.get = [&bus] { return bus.enabled.load() && bus.levelDb.load() > -89.5f ? 1.0f : 0.0f; };
        b.set = [&bus] (float v)
        {
            bus.levelDb = 0.0f;
            bus.enabled = v >= 0.5f;
        };
        b.text = [name] { return name; };
        return b;
    }

    void stepBy (const KnobBinding& b, int delta)
    {
        if (! b.isValid() || ! b.set)
            return;
        const float step = b.steps > 1.0f ? 1.0f / (b.steps - 1.0f) : 0.01f;
        b.set (juce::jlimit (0.0f, 1.0f, b.get() + (float) delta * step));
    }

    using dots::kFont5x7;
    using dots::tinyGlyph;
} // namespace

namespace y01v
{
    //==========================================================================
    Key::Key (const juce::String& name, juce::Colour f, bool withLed) : juce::Button (name), face (f), led (withLed) {}

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
        const auto well = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff2c2a28));
        g.fillRoundedRectangle (well, 4.0f);
        auto body = well.reduced (2.0f);
        if (down)
            body = body.translated (0.0f, 1.0f);
        g.setGradientFill (juce::ColourGradient (face.brighter (0.12f), 0.0f, body.getY(), face.darker (0.12f), 0.0f, body.getBottom(), false));
        g.fillRoundedRectangle (body, 3.5f);

        // The moulded bar near the foot.
        const juce::Rectangle<float> bar (body.getX() + body.getWidth() * 0.16f, body.getBottom() - 8.0f, body.getWidth() * 0.68f, 2.5f);
        g.setColour (face.darker (0.35f));
        g.fillRoundedRectangle (bar, 1.2f);
        g.setColour (face.brighter (0.3f).withAlpha (0.6f));
        g.fillRect (bar.translated (0.0f, 2.5f).withHeight (1.0f));

        if (led)
        {
            const juce::Rectangle<float> window (body.getCentreX() - 7.0f, body.getY() + 6.0f, 14.0f, 5.0f);
            if (shownOn)
            {
                g.setColour (kLedRed.withAlpha (0.35f));
                g.fillRoundedRectangle (window.expanded (5.0f, 3.5f), 5.0f);
                g.setColour (kLedRed);
                g.fillRoundedRectangle (window, 2.5f);
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.fillRoundedRectangle (window.reduced (3.5f, 1.5f), 1.0f);
            }
            else
            {
                g.setColour (face.darker (0.55f).withMultipliedSaturation (0.3f));
                g.fillRoundedRectangle (window, 2.5f);
            }
        }
        if (! led && shownOn)
        {
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillRoundedRectangle (body, 3.5f);
        }
        if (highlighted)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (body, 3.5f);
        }
    }

    //==========================================================================
    Knob::Knob (juce::Colour c, juce::Colour p) : cap (c), pointer (p) {}

    void Knob::setBinding (KnobBinding b)
    {
        binding = std::move (b);
        shown = -1.0f;
        refresh();
    }

    void Knob::refresh()
    {
        const float v = binding.isValid() ? binding.get() : 0.0f;
        if (! juce::approximatelyEqual (v, shown))
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

        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c.translated (2.5f, 3.5f)));
        g.setColour (juce::Colour (0xff1d1c1b));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));

        const float capR = r * 0.86f;
        g.setGradientFill (juce::ColourGradient (cap.brighter (0.22f), c.x - capR * 0.4f, c.y - capR * 0.6f, cap.darker (0.28f), c.x + capR * 0.5f, c.y + capR, true));
        g.fillEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c));

        const float position = shown < 0.0f ? 0.5f : shown;
        const float angle = (position - 0.5f) * juce::MathConstants<float>::pi * 5.0f / 3.0f;
        // The flutes: twelve notches around the scalloped top.
        g.setColour (cap.darker (0.5f));
        for (int i = 0; i < 12; ++i)
        {
            const float a = angle + (float) i * juce::MathConstants<float>::twoPi / 12.0f;
            const auto at = c.getPointOnCircumference (capR * 0.8f, a);
            g.fillEllipse (juce::Rectangle<float> (r * 0.17f, r * 0.17f).withCentre (at));
        }
        if (! pointer.isTransparent())
        {
            g.setColour (pointer);
            g.drawLine (juce::Line<float> (c.getPointOnCircumference (capR * 0.22f, angle), c.getPointOnCircumference (capR * 0.98f, angle)), 3.0f);
        }
    }

    void Knob::moveTo (float p)
    {
        if (! binding.isValid() || ! binding.set)
            return;
        p = juce::jlimit (0.0f, 1.0f, p);
        if (binding.steps > 1.0f)
            p = std::round (p * (binding.steps - 1.0f)) / (binding.steps - 1.0f);
        binding.set (p);
        refresh();
    }

    void Knob::mouseDown (const juce::MouseEvent&) { dragStart = binding.isValid() ? binding.get() : 0.0f; }
    void Knob::mouseDrag (const juce::MouseEvent& e) { moveTo (dragStart - (float) e.getDistanceFromDragStartY() / 220.0f); }

    void Knob::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (binding.reset)
            binding.reset();
        refresh();
    }

    void Knob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (binding.isValid())
            stepBy (binding, w.deltaY > 0 ? 1 : -1);
        refresh();
    }

    //==========================================================================
    void Wheel::paint (juce::Graphics& g)
    {
        const auto area = getLocalBounds().toFloat().reduced (2.0f);
        const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const auto c = area.getCentre();

        g.setColour (juce::Colour (0xff1a1918));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        const float top = r * 0.93f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3e3d3c), c.x - top * 0.5f, c.y - top * 0.7f, juce::Colour (0xff201f1e), c.x + top * 0.5f, c.y + top, true));
        g.fillEllipse (juce::Rectangle<float> (top * 2.0f, top * 2.0f).withCentre (c));

        // The finger dimple: glossy, near the edge.
        const auto dimple = c.getPointOnCircumference (top * 0.48f, angle);
        const float dr = top * 0.3f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0d0c0c), dimple.x, dimple.y - dr, juce::Colour (0xff4a4948), dimple.x, dimple.y + dr, false));
        g.fillEllipse (juce::Rectangle<float> (dr * 2.0f, dr * 2.0f).withCentre (dimple));
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.fillEllipse (juce::Rectangle<float> (dr * 0.5f, dr * 0.3f).withCentre (dimple.translated (dr * 0.2f, dr * 0.45f)));
    }

    float Wheel::angleAt (juce::Point<float> p) const
    {
        return getLocalBounds().toFloat().getCentre().getAngleToPoint (p);
    }

    void Wheel::turnBy (float radians)
    {
        angle += radians;
        pending += radians;
        constexpr float kStep = juce::MathConstants<float>::twoPi / 24.0f;
        const int steps = (int) (pending / kStep);
        if (steps != 0)
        {
            pending -= (float) steps * kStep;
            if (onTurn)
                onTurn (steps);
        }
        repaint();
    }

    void Wheel::mouseDown (const juce::MouseEvent& e)
    {
        lastAngle = angleAt (e.position);
        pending = 0.0f;
    }

    void Wheel::mouseDrag (const juce::MouseEvent& e)
    {
        const float now = angleAt (e.position);
        float delta = now - lastAngle;
        if (delta > juce::MathConstants<float>::pi)
            delta -= juce::MathConstants<float>::twoPi;
        else if (delta < -juce::MathConstants<float>::pi)
            delta += juce::MathConstants<float>::twoPi;
        lastAngle = now;
        turnBy (delta);
    }

    void Wheel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        angle += w.deltaY > 0 ? 0.26f : -0.26f;
        if (onTurn)
            onTurn (w.deltaY > 0 ? 1 : -1);
        repaint();
    }

    //==========================================================================
    Fader::Fader (juce::Colour cap, juce::Colour line, bool masterScale, const FaderLaw& printedLaw)
        : capColour (cap), lineColour (line), master (masterScale), printed (printedLaw), law (printedLaw)
    {
    }

    void Fader::bind (std::atomic<float>* db, const FaderLaw& newLaw)
    {
        value = db;
        knob = {};
        law = newLaw;
        refresh();
    }

    void Fader::bind (KnobBinding binding)
    {
        value = nullptr;
        knob = std::move (binding);
        refresh();
    }

    juce::Rectangle<float> Fader::track() const noexcept
    {
        return getLocalBounds().toFloat().reduced (0.0f, 44.0f);
    }

    void Fader::refresh()
    {
        const double p = knob.isValid() ? (double) knob.get() : value != nullptr ? (double) law.toPosition (value->load()) : 0.0;
        if (! juce::approximatelyEqual (p, position))
        {
            position = p;
            repaint();
        }
    }

    void Fader::setPositionFromUser (double p)
    {
        if (knob.isValid() && knob.set)
        {
            float to = (float) juce::jlimit (0.0, 1.0, p);
            if (knob.steps > 1.0f)
                to = std::round (to * (knob.steps - 1.0f)) / (knob.steps - 1.0f);
            knob.set (to);
        }
        else if (value != nullptr)
            *value = law.toDb (law.snap ((float) juce::jlimit (0.0, 1.0, p)));
        refresh();
    }

    void Fader::paint (juce::Graphics& g)
    {
        const auto area = getLocalBounds().toFloat();
        const auto t = track();
        const float cx = std::round (area.getWidth() * 0.5f);

        // The stripes: navy left of the slot, a lighter silver right of it.
        g.setColour (kNavy);
        g.fillRect (area.withRight (cx));
        g.setColour (kSilverStripe);
        g.fillRect (area.withLeft (cx).withTrimmedRight (3.0f));

        const auto yOf = [&] (float db) { return t.getBottom() - t.getHeight() * printed.toPosition (db); };
        const auto major = [&] (float db, const juce::String& label)
        {
            const float y = yOf (db);
            g.setColour (kWhiteSilk);
            g.fillRect (cx - 21.0f, y - 0.75f, 15.0f, 1.5f);
            g.setFont (type::font (14.0f));
            g.drawText (label, juce::Rectangle<float> (0.0f, y - 8.0f, cx - 24.0f, 16.0f), juce::Justification::centredRight, false);
            g.setColour (kNavy);
            g.fillRect (cx + 7.0f, y - 0.75f, 16.0f, 1.5f);
        };
        const auto minor = [&] (float db)
        {
            const float y = yOf (db);
            g.setColour (kWhiteSilk);
            g.fillRect (cx - 11.0f, y - 0.5f, 5.0f, 1.0f);
            g.setColour (kNavy);
            g.fillRect (cx + 7.0f, y - 0.5f, 10.0f, 1.0f);
        };
        const juce::String infinity (juce::CharPointer_UTF8 ("\xe2\x88\x9e"));
        if (master)
        {
            for (float db : { 0.0f, -5.0f, -10.0f, -15.0f, -20.0f, -30.0f, -40.0f, -50.0f, -70.0f })
                major (db, juce::String ((int) db));
        }
        else
        {
            for (auto [db, label] : std::initializer_list<std::pair<float, const char*>> { { 6.0f, "6" }, { 0.0f, "0" }, { -5.0f, "5" }, { -10.0f, "10" }, { -20.0f, "20" }, { -40.0f, "40" }, { -60.0f, "60" } })
                major (db, label);
            for (float db : { 3.0f, -2.5f, -7.5f, -15.0f, -30.0f })
                minor (db);
        }
        // The bottom of the travel: silence.
        {
            const float y = t.getBottom();
            g.setColour (kWhiteSilk);
            g.fillRect (cx - 21.0f, y - 0.75f, 15.0f, 1.5f);
            g.setFont (type::font (14.0f));
            g.drawText (master ? "-" + infinity : infinity, juce::Rectangle<float> (0.0f, y - 8.0f, cx - 24.0f, 16.0f), juce::Justification::centredRight, false);
            g.setColour (kNavy);
            g.fillRect (cx + 7.0f, y - 0.75f, 16.0f, 1.5f);
        }

        // The slot.
        const juce::Rectangle<float> slot (cx - 3.5f, t.getY() - 16.0f, 7.0f, t.getHeight() + 32.0f);
        g.setColour (juce::Colour (0xff141312));
        g.fillRoundedRectangle (slot, 2.5f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillRect (slot.withLeft (slot.getRight() - 1.0f));

        // The cap: ribbed across, waisted at the middle.
        constexpr float kCapW = 44.0f, kCapH = 82.0f, kPinch = 5.0f; // narrower than the scale: its numbers stay in view
        const float capY = t.getBottom() - t.getHeight() * (float) position;
        const float hw = kCapW * 0.5f, top = capY - kCapH * 0.5f, bottom = capY + kCapH * 0.5f;
        juce::Path shape;
        shape.startNewSubPath (cx - hw, top + 4.0f);
        shape.quadraticTo (cx - hw, top, cx - hw + 4.0f, top);
        shape.lineTo (cx + hw - 4.0f, top);
        shape.quadraticTo (cx + hw, top, cx + hw, top + 4.0f);
        shape.quadraticTo (cx + hw - kPinch * 2.0f, capY, cx + hw, bottom - 4.0f);
        shape.quadraticTo (cx + hw, bottom, cx + hw - 4.0f, bottom);
        shape.lineTo (cx - hw + 4.0f, bottom);
        shape.quadraticTo (cx - hw, bottom, cx - hw, bottom - 4.0f);
        shape.quadraticTo (cx - hw + kPinch * 2.0f, capY, cx - hw, top + 4.0f);
        shape.closeSubPath();

        g.setColour (juce::Colours::black.withAlpha (0.38f));
        g.fillPath (shape, juce::AffineTransform::translation (4.0f, 6.0f));

        g.setGradientFill (juce::ColourGradient (capColour.darker (0.28f), cx - hw, 0.0f, capColour.darker (0.28f), cx + hw, 0.0f, false));
        juce::ColourGradient across (capColour.darker (0.3f), cx - hw, 0.0f, capColour.darker (0.3f), cx + hw, 0.0f, false);
        across.addColour (0.3, capColour.brighter (0.1f));
        across.addColour (0.7, capColour.brighter (0.1f));
        g.setGradientFill (across);
        g.fillPath (shape);
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (shape);
            // The ends slope away: lit at the top, shaded at the foot.
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.fillRect (cx - hw, top, kCapW, 7.0f);
            g.setColour (juce::Colours::black.withAlpha (0.16f));
            g.fillRect (cx - hw, bottom - 7.0f, kCapW, 7.0f);
            // The ribs.
            for (float y = top + 10.0f; y < bottom - 8.0f; y += 7.0f)
            {
                if (std::abs (y - capY) < 4.0f)
                    continue;
                g.setColour (capColour.darker (0.5f));
                g.fillRect (cx - hw, y, kCapW, 2.0f);
                g.setColour (capColour.brighter (0.28f).withAlpha (0.7f));
                g.fillRect (cx - hw, y + 2.0f, kCapW, 1.0f);
            }
            g.setColour (lineColour);
            g.fillRect (cx - hw, capY - 1.5f, kCapW, 3.0f);
        }
    }

    void Fader::mouseDown (const juce::MouseEvent&) { dragStart = position; }

    void Fader::mouseDrag (const juce::MouseEvent& e)
    {
        setPositionFromUser (dragStart - (double) e.getDistanceFromDragStartY() / (double) track().getHeight());
    }

    void Fader::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (knob.isValid())
        {
            if (knob.reset)
                knob.reset();
            refresh();
        }
        else
            setPositionFromUser ((double) law.toPosition (0.0f));
    }

    void Fader::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        setPositionFromUser (position + (w.deltaY > 0 ? 1.0 : -1.0) / 127.0);
    }

    //==========================================================================
    namespace
    {
        const std::array<std::pair<const char*, float>, 12> kMeterRows { {
            { "CLIP", 0.0f }, { "-3", -3.0f }, { "-6", -6.0f }, { "-9", -9.0f }, { "-12", -12.0f }, { "-15", -15.0f },
            { "-18", -18.0f }, { "-24", -24.0f }, { "-30", -30.0f }, { "-36", -36.0f }, { "-42", -42.0f }, { "-48", -48.0f },
        } };
    } // namespace

    Meter::Meter (UiContext& c) : ctx (c) {}

    void Meter::refresh()
    {
        if (ctx.engine.getMains().empty())
            return;
        std::array<int, 2> now {};
        for (int side = 0; side < 2; ++side)
        {
            const float db = juce::Decibels::gainToDecibels (ctx.meters.get (&ctx.engine.getMain (0), MeterPoint::Output, side), -100.0f);
            int lit = 0;
            for (auto it = kMeterRows.rbegin(); it != kMeterRows.rend() && db >= it->second; ++it)
                ++lit;
            now[(size_t) side] = lit;
        }
        if (now != shown)
        {
            shown = now;
            repaint();
        }
    }

    void Meter::paint (juce::Graphics& g)
    {
        const auto area = getLocalBounds().toFloat();
        const float rowH = area.getHeight() / (float) kMeterRows.size();
        const float leftX = 30.0f, rightX = area.getWidth() - 30.0f;

        // The two recessed slots.
        g.setColour (juce::Colour (0xff36383c));
        for (float x : { leftX, rightX })
            g.fillRoundedRectangle (x - 20.0f, 0.0f, 40.0f, area.getHeight(), 2.0f);

        for (int i = 0; i < (int) kMeterRows.size(); ++i)
        {
            const float y = (float) i * rowH + rowH * 0.5f;
            const bool clip = i == 0, amber = i > 0 && i <= 6;
            const auto lit = clip ? juce::Colour (0xffff3030) : amber ? juce::Colour (0xffffc23a) : juce::Colour (0xff3cf06a);
            const auto dark = clip ? juce::Colour (0xff6a1020) : amber ? juce::Colour (0xff8f7026) : juce::Colour (0xff0f5a30);
            g.setColour (kWhiteSilk);
            g.setFont (type::font (12.5f, true));
            g.drawText (kMeterRows[(size_t) i].first, juce::Rectangle<float> (leftX + 18.0f, y - 8.0f, rightX - leftX - 36.0f, 16.0f), juce::Justification::centred, false);
            for (int side = 0; side < 2; ++side)
            {
                const float x = side == 0 ? leftX + 8.0f : rightX - 8.0f;
                const bool on = (int) kMeterRows.size() - i <= shown[(size_t) side];
                g.setColour (juce::Colour (0xff24262a));
                g.fillRoundedRectangle (side == 0 ? x - 26.0f : x + 8.0f, y - 1.5f, 18.0f, 3.0f, 1.0f);
                if (on)
                {
                    g.setColour (lit.withAlpha (0.3f));
                    g.fillEllipse (x - 8.0f, y - 8.0f, 16.0f, 16.0f);
                }
                g.setColour (on ? lit : dark);
                g.fillEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f);
                g.setColour (juce::Colours::white.withAlpha (on ? 0.6f : 0.25f));
                g.fillEllipse (x - 2.5f, y - 3.5f, 3.0f, 2.5f);
            }
        }
    }

    //==========================================================================
    namespace
    {
        constexpr int kLeftColumn = 54, kRightColumn = 294;
        /** The EQ table's column edges (the manual's picture, p.63). */
        constexpr int kEqCols[5] = { 110, 156, 202, 248, 294 };
        constexpr juce::juce_wchar kInfinity = 0x221E;
        /** The rows each of the 5x7 font's seven becomes: the function's name, and titles. */
        constexpr int kNameRows[7] = { 1, 2, 2, 1, 2, 2, 1 }, kTitleRows[7] = { 1, 1, 2, 1, 2, 1, 1 };
        /** The preference DELAY UNITS (p.203): 0 msec, 1 meter, 2 sample. */
        constexpr const char* kDelayUnitsKey = "y01vDelayUnits";
        /** The 2TR IN jacks' input ports (livedaw_engine's Y01V_PORT_GROUPS). */
        constexpr int kTwoTrackInput = 24;

        /** A level as the LCD prints it under a fader or a PRE/POST switch. */
        juce::String lcdLevel (float db)
        {
            if (db <= -89.5f)
                return "-" + juce::String::charToString (kInfinity);
            return juce::String (juce::roundToInt (db));
        }

        /** The pan's 33 positions, as the 01V shows them: L16 .. CENTER .. R16 (p.47). */
        KnobBinding bindPan01V (const juce::String& label, std::atomic<float>& v)
        {
            auto b = bindPan (label, v);
            b.steps = 33.0f;
            b.set = [&v] (float p) { v = (float) juce::roundToInt ((juce::jlimit (0.0f, 1.0f, p) - 0.5f) * 32.0f) * 100.0f / 16.0f; };
            b.text = [&v]
            {
                const int n = juce::roundToInt (v.load() * 16.0f / 100.0f);
                return n == 0 ? juce::String ("CENTER") : juce::String (n < 0 ? "L" : "R") + juce::String (std::abs (n));
            };
            return b;
        }

        /** A delay in ms (an input channel's: 250 at most; an output's 300), the number alone, in
            the display's DELAY UNITS: its page names the unit (pp.45, 203). */
        template <typename Input>
        KnobBinding delayBinding (Input& in, const Display* display, float maxMs = 250.0f)
        {
            KnobBinding b;
            b.label = "DELAY";
            b.steps = maxMs * 10.0f + 1.0f;
            b.get = [&in, maxMs] { return juce::jlimit (0.0f, 1.0f, in.delay.load() / maxMs); };
            b.set = [&in, maxMs] (float x)
            {
                in.delayMode = 2; // MS
                in.delay = x * maxMs;
            };
            b.text = [&in, display] { return display != nullptr ? display->delayText (in.delay.load()) : juce::String (in.delay.load(), 1); };
            return b;
        }

        template <typename Params>
        KnobBinding onBinding (Params& p)
        {
            KnobBinding on;
            on.label = "ON";
            on.steps = 2.0f;
            on.get = [&p] { return p.mute.load() ? 0.0f : 1.0f; };
            on.set = [&p] (float v) { p.mute = v < 0.5f; };
            on.text = [&p] { return juce::String (p.mute.load() ? "OFF" : "ON"); };
            return on;
        }

        KnobBinding prePostBinding (ld::SendParams& send, const char* post = "PST")
        {
            KnobBinding b;
            b.label = "PRE/POST";
            b.steps = 2.0f;
            b.get = [&send] { return send.mode.load() == (int) SendMode::Pre ? 1.0f : 0.0f; };
            b.set = [&send] (float v) { send.mode = (int) (v >= 0.5f ? SendMode::Pre : SendMode::Post); };
            b.text = [&send, post] { return juce::String (send.mode.load() == (int) SendMode::Pre ? "PRE" : post); };
            return b;
        }
    }

    Display::Display (UiContext& c) : ctx (c), dots ((size_t) (kDotsW * kDotsH), 0), image (juce::Image::RGB, kWidth, kHeight, true)
    {
        loadPanel();
        if (const auto text = ctx.engine.getConsoleText(); text.isEmpty() || ! adoptGroupsText (text))
            seenConsole = ctx.engine.setConsoleText (groupsText());
        else
            seenConsole = ctx.engine.getConsoleVersion();
        loadLibrary();
        relink();
        rebuild();
    }

    void Display::clear() { std::fill (dots.begin(), dots.end(), (uint8_t) 0); }

    void Display::dot (int x, int y, bool on)
    {
        if (x >= 0 && y >= 0 && x < kDotsW && y < kDotsH)
            dots[(size_t) (y * kDotsW + x)] = on ? 1 : 0;
    }

    bool Display::isDotOn (int x, int y) const noexcept
    {
        return x >= 0 && y >= 0 && x < kDotsW && y < kDotsH && dots[(size_t) (y * kDotsW + x)] != 0;
    }

    void Display::hLine (int x0, int x1, int y, bool on)
    {
        for (int x = x0; x <= x1; ++x)
            dot (x, y, on);
    }

    void Display::vLine (int x, int y0, int y1, bool on)
    {
        for (int y = y0; y <= y1; ++y)
            dot (x, y, on);
    }

    void Display::box (juce::Rectangle<int> r, bool on)
    {
        hLine (r.getX(), r.getRight() - 1, r.getY(), on);
        hLine (r.getX(), r.getRight() - 1, r.getBottom() - 1, on);
        vLine (r.getX(), r.getY(), r.getBottom() - 1, on);
        vLine (r.getRight() - 1, r.getY(), r.getBottom() - 1, on);
    }

    void Display::fill (juce::Rectangle<int> r, bool on)
    {
        for (int y = r.getY(); y < r.getBottom(); ++y)
            hLine (r.getX(), r.getRight() - 1, y, on);
    }

    void Display::dottedH (int x0, int x1, int y)
    {
        for (int x = x0; x <= x1; x += 2)
            dot (x, y);
    }

    int Display::textWidth (const juce::String& s, bool tiny, int scale) const
    {
        if (s.isEmpty())
            return 0;
        const int pitch = (tiny ? 4 : 6) * scale;
        const int infinities = s.length() - s.removeCharacters (juce::String::charToString (kInfinity)).length();
        return pitch * s.length() - scale + infinities * (8 - pitch);
    }

    void Display::text (int x, int y, const juce::String& s, bool tiny, int scale, bool on)
    {
        for (auto p = s.getCharPointer(); ! p.isEmpty();)
        {
            const auto c = p.getAndAdvance();
            if (c == kInfinity)
            {
                static const uint8_t glyph[7] = { 0x06, 0x09, 0x09, 0x06, 0x09, 0x09, 0x06 };
                for (int col = 0; col < 7; ++col)
                    for (int row = 0; row < 5; ++row)
                        if ((glyph[col] >> row) & 1)
                            dot (x + col, y + row, on);
                x += 8;
                continue;
            }
            if (tiny)
            {
                const auto* glyph = tinyGlyph (c);
                for (int col = 0; col < 3; ++col)
                    for (int row = 0; row < 5; ++row)
                        if ((glyph[col] >> row) & 1)
                            dot (x + col, y + row, on);
                x += 4;
                continue;
            }
            static const uint8_t plainZero[5] = { 0x3E, 0x41, 0x41, 0x41, 0x3E };
            const int index = c >= 32 && c <= 126 ? (int) c - 32 : 0;
            const auto* glyph = c == '0' && scale > 1 ? plainZero : kFont5x7[index];
            for (int col = 0; col < 5; ++col)
                for (int row = 0; row < 7; ++row)
                    if ((glyph[col] >> row) & 1)
                        fill ({ x + col * scale, y + row * scale, scale, scale }, on);
            x += 6 * scale;
        }
    }

    void Display::textTall (int x, int y, const juce::String& s, const int* repeat, float pitch, bool bold, bool on)
    {
        static const uint8_t plainZero[5] = { 0x3E, 0x41, 0x41, 0x41, 0x3E };
        int n = 0;
        for (auto p = s.getCharPointer(); ! p.isEmpty(); ++n)
        {
            const auto c = p.getAndAdvance();
            const int index = c >= 32 && c <= 126 ? (int) c - 32 : 0;
            const auto* glyph = c == '0' ? plainZero : kFont5x7[index];
            const int left = x + juce::roundToInt ((float) n * pitch);
            int rowY = y;
            for (int row = 0; row < 7; ++row)
                for (int times = 0; times < repeat[row]; ++times, ++rowY)
                    for (int col = 0; col < 5; ++col)
                        if ((glyph[col] >> row) & 1)
                        {
                            dot (left + col, rowY, on);
                            if (bold)
                                dot (left + col + 1, rowY, on);
                        }
        }
    }

    void Display::textFlipped (int x, int y, const juce::String& s)
    {
        for (auto p = s.getCharPointer(); ! p.isEmpty();)
        {
            const auto* glyph = tinyGlyph (p.getAndAdvance());
            for (int col = 0; col < 3; ++col)
                for (int row = 0; row < 5; ++row)
                    if ((glyph[col] >> row) & 1)
                        dot (x + col, y + row, ! isDotOn (x + col, y + row));
            x += 4;
        }
    }

    void Display::textCentred (juce::Rectangle<int> r, const juce::String& s, bool tiny, bool on)
    {
        text (r.getCentreX() - textWidth (s, tiny) / 2, r.getCentreY() - (tiny ? 2 : 3), s, tiny, 1, on);
    }

    void Display::dial (int cx, int cy, int r, float value, bool filled)
    {
        if (filled)
            for (int y = -r; y <= r; ++y)
                for (int x = -r; x <= r; ++x)
                    if (x * x + y * y <= r * r)
                        dot (cx + x, cy + y);
        for (int i = 0; i < 64; ++i)
        {
            const float a = (float) i * juce::MathConstants<float>::twoPi / 64.0f;
            dot (cx + juce::roundToInt ((float) r * std::sin (a)), cy - juce::roundToInt ((float) r * std::cos (a)));
        }
        // The pointer: from the centre to the rim, two dots thick.
        const float a = (value - 0.5f) * juce::MathConstants<float>::pi * 1.5f;
        const bool steep = std::abs (std::cos (a)) > 0.5f;
        for (int i = 0; i <= r; ++i)
        {
            const int x = cx + juce::roundToInt ((float) i * std::sin (a)), y = cy - juce::roundToInt ((float) i * std::cos (a));
            dot (x, y, ! filled);
            dot (x + (steep ? 1 : 0), y + (steep ? 0 : 1), ! filled);
        }
    }

    void Display::dottedV (int x, int y0, int y1)
    {
        for (int y = y0; y <= y1; y += 2)
            dot (x, y);
    }

    void Display::arrowBox (int x, int y, bool right)
    {
        box ({ x, y, 9, 12 });
        for (int i = 0; i < 4; ++i)
            vLine (right ? x + 2 + i : x + 6 - i, y + 2 + i, y + 9 - i);
    }

    void Display::header (const juce::String& s)
    {
        // Open at the top, a double line under it and down its right.
        const int w = textWidth (s, false) + 8;
        const int x = kLeftColumn + 2;
        text (x + 4, 0, s);
        vLine (x, 0, 7);
        vLine (x + w, 0, 7);
        vLine (x + w + 1, 0, 8);
        hLine (x + 1, x + w, 7);
        hLine (x + 2, x + w + 1, 8);
    }

    void Display::channelMeter (Strip& s, const juce::String& left, const juce::String& right)
    {
        static const std::pair<const char*, float> marks[] = { { "CLIP", 0.0f }, { "-6", -6.0f }, { "-12", -12.0f }, { "-18", -18.0f }, { "-30", -30.0f }, { "-48", -48.0f } };
        for (int i = 0; i < 6; ++i)
        {
            dottedH (253, 272, i * 6 + 2);
            text (290 - textWidth (marks[i].first, true), i * 6, marks[i].first, true);
        }
        for (int side = 0; side < 2; ++side)
        {
            const float level = juce::Decibels::gainToDecibels (ctx.meters.get (&s, MeterPoint::PreFader, side), -100.0f);
            for (int i = 5; i >= 0 && level >= marks[i].second; --i)
                fill ({ 256 + side * 10, i * 6, 6, 5 });
        }
        text (258, 37, left, true);
        text (268, 37, right, true);
    }

    Display::~Display()
    {
        ld::ld_engine_console_osc_set (ctx.engine.getHandle(), false, ld::kOscSine, 1000.0f, -96.0f, false, false);
    }

    void Display::setFunction (Function f)
    {
        if (f == function)
            return;
        lastPages[(size_t) function] = page;
        function = f;
        page = juce::jlimit (0, getNumPages() - 1, lastPages[(size_t) f]);
        focus = 0;
        fxScroll = 0;
        library = -1;
        rebuild();
    }

    void Display::showFirstPage (Function f)
    {
        if (f != function)
        {
            lastPages[(size_t) f] = 0;
            setFunction (f);
        }
        else if (page != 0)
        {
            page = 0;
            focus = 0;
            fxScroll = 0;
            library = -1;
            rebuild();
        }
    }

    void Display::pressFunction (Function f)
    {
        if (f == function)
            nextPage();
        else
            setFunction (f);
    }

    void Display::nextPage()
    {
        page = (page + 1) % juce::jmax (1, getNumPages());
        focus = 0;
        fxScroll = 0;
        library = -1;
        rebuild();
    }

    int Display::getNumPages() const
    {
        switch (function)
        {
            case Function::Send:    return sendBus() < 0 ? 1 : effectSlot() >= 0 ? 3 : 2;
            case Function::Utility: return 3;
            case Function::OptionIO: return 5;   // IN METER, CH 17-24, OUT METER, OUT SELECT, INPUT SWAP (pp.51, 58, 85, 219)
            case Function::PanRouting: return 4; // PANPOT, routing, BUS MASTER, OMNI OUT SELECT (pp.47, 49, 109, 116)
            case Function::PhaseDelay: return 4; // PHASE, the delays of CH 1-8 and 9-16, OUTPUT DELAY (pp.42, 45, 117)
            case Function::Setup:   return 5;    // WORD CLOCK, MONITOR and SOLO, GROUP, PAIR, DITHER (pp.52, 55, 78, 209, 212)
            case Function::EqAtt:
            case Function::Dynamics: return 2;   // the edit page, the library (pp.67, 173)
            case Function::Remote:  return 3;    // INTERNAL PARAMETER, MMC CONTROL, USER DEFINE (pp.194, 236, 238)
            case Function::View:    return 2;
            case Function::Midi:    return 5; // MIDI SETUP, PROGRAM CHANGE, CONTROL CHANGE, BULK, LOCAL CONTROL (pp.224-234)
            default:                return 1;
        }
    }

    bool Display::preference (const char* key, bool initial) const
    {
        return ctx.settings.getBoolValue (key, initial);
    }

    void Display::setPreference (const char* key, bool on)
    {
        ctx.settings.setValue (key, on);
    }

    void Display::incDec (int delta)
    {
        if (function != Function::Memory || ! preference ("y01vIncDecRecall", false))
        {
            turn (delta);
            return;
        }
        // The next memory that holds a scene (p.203).
        for (int m = memory + delta; m >= 1 && m <= ctx.show.getNumScenes(); m += delta)
            if (memoryTitle (m).isNotEmpty())
            {
                setMemory (m);
                ctx.show.recallScene (m - 1);
                break;
            }
        refresh();
    }

    void Display::setFaderMode (FaderMode m)
    {
        if (m != mode)
        {
            page = 0;
            focus = 0;
            fxScroll = 0;
            library = -1;
        }
        mode = m;
        rebuild();
    }

    int Display::effectSlot() const noexcept
    {
        const int bus = sendBus();
        return bus >= kEffectBus ? bus - kEffectBus : -1;
    }

    void Display::setSelectedBindings (KnobBinding pan, KnobBinding freq, KnobBinding gain)
    {
        selectedBindings = { std::move (pan), std::move (freq), std::move (gain) };
    }

    void Display::setContrast (float c)
    {
        contrast = c;
        shownDots.clear(); // draw it again
    }

    int Display::sendBus() const noexcept { return busOf (mode); }

    Strip* Display::selected() const { return ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr; }

    juce::String Display::stripName (const Strip* s) const
    {
        if (s == nullptr)
            return "--";
        const auto indexIn = [s] (const MixerEngine::StripList& list)
        {
            for (int i = 0; i < (int) list.size(); ++i)
                if (list[(size_t) i].get() == s)
                    return i;
            return -1;
        };
        switch (s->getKind())
        {
            case StripKind::Channel:
            {
                const int i = indexIn (ctx.engine.getChannels());
                return "CH" + juce::String (i < 12 ? i + 1 : i < kOptionChannel ? 13 + 2 * (i - 12) : 17 + i - kOptionChannel);
            }
            case StripKind::Aux: return "RTN" + juce::String (indexIn (ctx.engine.getAux()) + 1);
            case StripKind::Bus:
            {
                const int i = indexIn (ctx.engine.getBuses());
                return i < kAuxBus ? "BUS" + juce::String (i + 1) : i < kEffectBus ? "AUX" + juce::String (i - kAuxBus + 1) : "EFF" + juce::String (i - kEffectBus + 1);
            }
            case StripKind::Main:   return "ST";
            case StripKind::Matrix: break;
        }
        return "--";
    }

    juce::String Display::functionName() const
    {
        switch (function)
        {
            case Function::View:       return "VIEW";
            case Function::Dynamics:   return "DYNAMICS";
            case Function::EqAtt:      return "EQ";
            case Function::PhaseDelay: return "DELAY";
            case Function::PanRouting: return "PAN/ROUT";
            case Function::Utility:    return "UTILITY";
            case Function::Midi:       return "MIDI";
            case Function::Setup:      return "SETUP";
            case Function::OptionIO:   return "OPTION";
            case Function::Remote:     return "REMOTE";
            case Function::Memory:     return "MEMORY";
            case Function::Send:
            {
                const int bus = sendBus();
                return bus < 0 ? juce::String ("SEND") : bus < kEffectBus ? "AUX" + juce::String (bus - kAuxBus + 1) : "EFFECT" + juce::String (bus - kEffectBus + 1);
            }
        }
        return {};
    }

    Display::Cell& Display::add (Cell::Kind kind, juce::Rectangle<int> area, KnobBinding binding, const juce::String& caption)
    {
        Cell c;
        c.kind = kind;
        c.area = area;
        c.binding = std::move (binding);
        c.caption = caption;
        cells.push_back (std::move (c));
        return cells.back();
    }

    void Display::rebuild()
    {
        cells.clear();
        auto* s = selected();
        switch (function)
        {
            case Function::EqAtt:
                if (page == 1)
                    buildLibrary (0);
                else if (s != nullptr)
                    buildEq (*s);
                break;
            case Function::Dynamics:
                if (page == 1)
                    buildLibrary (1);
                else if (s != nullptr)
                    buildDynamics (*s);
                break;
            case Function::PhaseDelay:
                if (page == 0)
                    buildPhase();
                else if (page == 3)
                    buildOutputDelay();
                else
                    buildDelay();
                break;
            case Function::View:
                if (s != nullptr)
                    buildView (*s);
                break;
            case Function::PanRouting:
                if (page == 0)
                    buildPan();
                else if (page == 1)
                    buildRouting();
                else if (page == 2)
                    buildBusMaster();
                else
                    buildOmni();
                break;
            case Function::Send:
                // EFFECT: EDIT, LIBRARY, PRE/POST (pp.134-136). AUX: PRE/POST first (p.97).
                if (sendBus() >= 0 && page == (effectSlot() >= 0 ? 2 : 0))
                    buildPrePost();
                else if (effectSlot() >= 0)
                    page == 0 ? buildEffectEdit() : buildLibrary (2);
                else
                    buildSend();
                break;
            case Function::Memory:     buildMemory(); break;
            case Function::Utility:
                page == 0 ? buildOscillator() : buildPreferences();
                break;
            case Function::OptionIO:   buildOption(); break;
            case Function::Midi:
                if (page == 0)
                    buildMidiSetup();
                else if (page == 1)
                    buildMidiProgram();
                else if (page == 2)
                    buildMidiControl();
                else if (page == 3)
                    buildMidiBulk();
                break; // page 5's LOCAL CONTROL: drawn, its OFF isn't built
            case Function::Setup:
                if (page == 1)
                    buildMonitorSetup();
                else if (page == 2)
                    buildGroups();
                else if (page == 3)
                    buildPairs();
                break; // WORD CLOCK and DITHER: drawn
            case Function::Remote:     buildRemote(); break;
        }
        focus = juce::jlimit (0, juce::jmax (0, (int) cells.size() - 1), focus);
        applyOscillator();
        shownDots.clear();
        refresh();
        if (onRemoteChanged)
            onRemoteChanged();
    }

    void Display::buildEq (Strip& s)
    {
        auto& p = s.params;
        auto& eq = p.eq();
        const auto table = s.isOutput() ? models::BusEq : models::ChannelEq;
        add (Cell::Switch, { 65, 24, 28, 12 }, bindBool ("EQ", eq.on));
        if (s.isInput())
        {
            auto att = bindLinear ("ATT", p.input.trimDb, -96.0f, 0.0f, "dB", 0, 0.0f);
            att.steps = 97.0f; // 1 dB steps
            att.text = [&p] { return juce::String (juce::roundToInt (p.input.trimDb.load())) + "dB"; };
            add (Cell::Dial, { 62, 55, 34, 21 }, std::move (att));
        }

        const char* prefixes[] = { "l", "lm", "hm", "h" };
        const char* rows[] = { "f", "g", "q" };
        for (int band = 0; band < 4; ++band)
            for (int row = 0; row < 3; ++row)
            {
                auto b = bindParam (table, eq.model.load(), eq.values, juce::String (prefixes[band]) + rows[row], {});
                if (b.isValid())
                    add (Cell::Text, { kEqCols[band] + 1, 51 + row * 10, kEqCols[band + 1] - kEqCols[band] - 1, 9 }, std::move (b));
            }
    }

    void Display::buildDynamics (Strip& s)
    {
        auto& dyn = s.params.dynamics();
        add (Cell::Switch, { 57, 21, 34, 12 }, bindBool ("COMP", dyn.on));
        const auto& m = models::model (models::Dynamics, dyn.model.load());
        // The columns read THRESHOLD/ATTACK, OUT GAIN/RELEASE, KNEE/RATIO (p.163).
        static const char* const order[] = { "THRESHOLD", "ATTACK", "OUT GAIN", "RELEASE", "KNEE", "RATIO" };
        std::vector<int> params;
        for (const auto* name : order)
            for (int i = 0; i < (int) m.params.size(); ++i)
                if (m.params[(size_t) i].label.toUpperCase() == name)
                    params.push_back (i);
        for (int i = 0; i < (int) m.params.size(); ++i)
            if (std::find (params.begin(), params.end(), i) == params.end())
                params.push_back (i);
        for (int n = 0; n < juce::jmin (6, (int) params.size()); ++n)
        {
            const auto i = (size_t) params[(size_t) n];
            add (Cell::DialLine, { 60 + (n / 2) * 84, 44 + (n % 2) * 18, 76, 17 }, bindModelParam (dyn.values[i], m.params[i]), m.params[i].label.toUpperCase());
        }
    }

    void Display::drawDynamics (Strip& s)
    {
        auto& dyn = s.params.dynamics();
        const auto& m = models::model (models::Dynamics, dyn.model.load());
        header (stripName (&s) + "  DYNAMICS EDIT");
        text (100, 18, "TYPE");
        box ({ 126, 12, 58, 13 });
        textCentred ({ 126, 12, 58, 13 }, m.name.toUpperCase().replace ("COMPRESSOR", "COMP").substring (0, 9));
        if (s.isInput())
        {
            text (94, 33, "KEYIN");
            const juce::Rectangle<int> key (126, 27, 58, 12);
            dottedH (key.getX(), key.getRight() - 1, key.getY());
            dottedH (key.getX(), key.getRight() - 1, key.getBottom() - 1);
            dottedV (key.getX(), key.getY(), key.getBottom() - 1);
            dottedV (key.getRight() - 1, key.getY(), key.getBottom() - 1);
            text (130, 29, "SELF");
        }
        // The transfer curve.
        const juce::Rectangle<int> graph (187, 1, 41, 40);
        box (graph);
        float threshold = 0.0f, ratio = 1.0f;
        for (int i = 0; i < (int) m.params.size(); ++i)
        {
            const auto label = m.params[(size_t) i].label.toUpperCase();
            if (label == "THRESHOLD")
                threshold = dyn.values[(size_t) i].load();
            else if (label == "RATIO")
                ratio = juce::jmax (1.0f, dyn.values[(size_t) i].load());
        }
        for (int x = 1; x < graph.getWidth() - 1; ++x)
        {
            const float in = -60.0f + 60.0f * (float) x / (float) (graph.getWidth() - 1);
            const float out = in <= threshold ? in : threshold + (in - threshold) / ratio;
            dot (graph.getX() + x, graph.getBottom() - 2 - juce::roundToInt ((out + 60.0f) / 60.0f * (float) (graph.getHeight() - 3)));
        }
        channelMeter (s, {}, {});
        const char* gr[] = { "0", "-2", "-4", "-6", "-8", "-18" };
        for (int i = 0; i < 6; ++i)
            text (243 - textWidth (gr[i], true), i * 6, gr[i], true);
        text (240, 37, "GR 1 2", true);
        dottedV (137, 44, 79);
        dottedV (221, 44, 79);
    }

    void Display::buildPhase()
    {
        const auto& channels = ctx.engine.getChannels();
        for (int i = 0; i < juce::jmin (14, (int) channels.size()); ++i)
        {
            const int col = i < 12 ? i % 8 : 4 + (i - 12) * 2, row = i < 8 ? 0 : 1;
            int x = 73 + col * 80 / 3;
            if (i >= 12) // one switch for the pair, between its two numbers
                x += 40 / 3;
            add (Cell::Switch, { x, 25 + row * 21, 19, 9 }, bindBool ("PHASE", channels[(size_t) i]->params.input.invert, "REV", "NOR"));
        }
    }

    void Display::drawPhase()
    {
        header ("PHASE");
        for (int i = 0; i < 16; ++i)
        {
            const int col = i % 8, y = i < 8 ? 17 : 38;
            const int cx = 73 + col * 80 / 3 + 9;
            if (i < 12)
            {
                const juce::String n (i + 1);
                text (cx - textWidth (n, false) / 2, y, n);
            }
            if (col < 7)
                dottedV (73 + col * 80 / 3 + 23, 14, 56);
        }
        for (int pair = 0; pair < 2; ++pair)
        {
            const juce::String label (pair == 0 ? "13 - 14" : "15 - 16");
            const int cx = 73 + (4 + pair * 2) * 80 / 3 + 9 + 40 / 3;
            text (cx - textWidth (label, false) / 2, 38, label);
        }
        box ({ 79, 65, 191, 13 });
        box ({ 88, 67, 21, 9 });
        textCentred ({ 88, 67, 21, 9 }, "NOR", true);
        text (111, 68, "=NORMAL PHASE");
        fill ({ 192, 67, 21, 9 });
        textCentred ({ 192, 67, 21, 9 }, "REV", true, false);
        text (215, 68, "=REVERSED");
    }

    void Display::buildDelay()
    {
        const auto& channels = ctx.engine.getChannels();
        const int first = page == 1 ? 0 : 8, count = page == 1 ? 8 : 6;
        for (int i = 0; i < count && first + i < (int) channels.size(); ++i)
        {
            auto& in = channels[(size_t) (first + i)]->params.input;
            const int col = page == 2 && i >= 4 ? (i == 4 ? 1 : 3) : i % 4, row = i / 4;
            const int x = 120 + col * 42, y = 2 + row * 41;
            add (Cell::Switch, { x, y, 19, 9 }, bindBool ("DELAY", in.delayOn));
            add (Cell::Text, { x - 18, y + 10, 37, 9 }, delayBinding (in, this));
        }
    }

    void Display::drawDelay()
    {
        const int first = page == 1 ? 1 : 9;
        for (int row = 0; row < 2; ++row)
        {
            const int y = 2 + row * 41;
            text (65, y + 3, "DELAY");
            text (65, y + 12, delayUnitLabel(), true);
            text (65, y + 20, "MIX");
            text (65, y + 30, "FB.GAIN");
        }
        for (int i = 0; i < (page == 1 ? 8 : 6); ++i)
        {
            const int col = page == 2 && i >= 4 ? (i == 4 ? 1 : 3) : i % 4, row = i / 4;
            const int x = 120 + col * 42, y = 2 + row * 41;
            const auto number = page == 2 && i >= 4 ? juce::String (13 + (i - 4) * 2) + "-" + juce::String (14 + (i - 4) * 2) : juce::String (first + i);
            text (x - 3 - textWidth (number, false), y + 1, number);
            // MIX and FB.GAIN: this delay's are fixed at all delay, no feedback.
            text (x + 19 - textWidth ("+100", false), y + 20, "+100");
            text (x + 19 - textWidth ("0%", false), y + 30, "0%");
        }
        for (int x : { 144, 186, 228 })
            dottedV (x, 0, kDotsH - 1);
        if (page == 2)
            arrowBox (56, 13, false);
        arrowBox (281, 56, true);
    }

    void Display::buildPan()
    {
        const auto& channels = ctx.engine.getChannels();
        const auto& aux = ctx.engine.getAux();
        const bool option = mode == FaderMode::OptionIO;
        for (int i = 0; i < 16; ++i)
        {
            Strip* s = nullptr;
            if (option)
            {
                if (i < 8 && kOptionChannel + i < (int) channels.size())
                    s = channels[(size_t) (kOptionChannel + i)].get();
            }
            else if (i < 14 && i < (int) channels.size())
                s = channels[(size_t) i].get();
            else if (i >= 14 && i - 14 < (int) aux.size())
                s = aux[(size_t) (i - 14)].get();
            if (s != nullptr)
            {
                const int cx = 71 + (i % 8) * 286 / 10, row = i / 8;
                add (Cell::Dial, { cx - 14, row == 0 ? 20 : 56, 28, row == 0 ? 26 : 24 }, bindPan01V ("PAN", s->params.pan));
            }
        }
    }

    void Display::drawPan()
    {
        header ("PANPOT");
        text (195, 1, "MODE=");
        const juce::Rectangle<int> mark (227, 0, 62, 9);
        dottedH (mark.getX(), mark.getRight() - 1, mark.getY());
        dottedH (mark.getX(), mark.getRight() - 1, mark.getBottom() - 1);
        dottedV (mark.getX(), mark.getY(), mark.getBottom() - 1);
        dottedV (mark.getRight() - 1, mark.getY(), mark.getBottom() - 1);
        text (229, 1, "INDIVIDUAL");
        const bool option = mode == FaderMode::OptionIO;
        for (int i = 0; i < 16; ++i)
        {
            if (option && i >= 8)
                break;
            const auto name = option ? juce::String (17 + i)
                                     : i < 12 ? juce::String (i + 1)
                                     : i < 14 ? juce::String (13 + 2 * (i - 12)) + "-" + juce::String (14 + 2 * (i - 12))
                                              : "RTN" + juce::String (i - 13);
            const int cx = 71 + (i % 8) * 286 / 10;
            text (cx - textWidth (name, name.length() > 4) / 2, i < 8 ? 13 : 49, name, name.length() > 4);
        }
        for (int c = 0; c < 7; ++c)
            dottedV (85 + c * 286 / 10, 12, kDotsH - 1);
    }

    void Display::buildSend()
    {
        const int bus = sendBus();
        if (bus < 0)
            return;
        const auto& channels = ctx.engine.getChannels();
        const auto& aux = ctx.engine.getAux();
        for (int i = 0; i < 16; ++i)
        {
            Strip* s = nullptr;
            juce::String name;
            if (i < 14 && i < (int) channels.size())
            {
                s = channels[(size_t) i].get();
                name = i < 12 ? juce::String (i + 1) : juce::String (13 + 2 * (i - 12));
            }
            else if (i >= 14 && i - 14 < (int) aux.size() && bus != kEffectBus + (i - 14)) // a return can't feed its own effect (p.35)
            {
                s = aux[(size_t) (i - 14)].get();
                name = "R" + juce::String (i - 13);
            }
            if (s != nullptr)
                add (Cell::Bar, { 58 + i * 14, 18, 13, 60 }, bindFader (name, s->sends[(size_t) bus].levelDb), name);
        }
    }

    void Display::buildView (Strip& s)
    {
        auto& p = s.params;
        if (page == 1)
        {
            // FADER, then the sends of an input channel (p.57).
            add (Cell::Bar, { 71, 24, 13, 33 }, bindFader ("FADER", p.faderDb));
            add (Cell::Switch, { 69, 69, 16, 9 }, onBinding (p));
            if (s.isInput())
            {
                const int cx[] = { 106, 126, 146, 166, 195, 215 };
                const int bus[] = { kAuxBus, kAuxBus + 1, kAuxBus + 2, kAuxBus + 3, kEffectBus, kEffectBus + 1 };
                for (int i = 0; i < 6; ++i)
                {
                    auto& send = s.sends[(size_t) bus[i]];
                    add (Cell::Bar, { cx[i] - 6, 24, 13, 33 }, bindFader ("SEND", send.levelDb));
                    add (Cell::Switch, { cx[i] - 8, 69, 16, 9 }, prePostBinding (send));
                }
            }
            return;
        }
        if (s.isInput())
        {
            auto& in = p.input;
            add (Cell::Switch, { 102, 14, 16, 9 }, bindBool ("DELAY", in.delayOn));
            add (Cell::Text, { 95, 27, 31, 9 }, delayBinding (in, this)).dotted = true;
            for (int b = 0; b < 4; ++b)
                add (Cell::Switch, { 138 + (b % 2) * 12, 11 + (b / 2) * 11, 10, 9 }, bindRoute (s.sends[(size_t) b], juce::String (b + 1)));
            add (Cell::Switch, { 138, 33, 10, 9 }, bindBool ("ST", s.mains[0].on, "ST", "ST"));
            add (Cell::Dial, { 135, 58, 30, 22 }, bindPan01V ("PAN", p.pan));
            add (Cell::Switch, { 214, 7, 16, 9 }, bindBool ("PHASE", in.invert, "R", "N"));
        }
        add (Cell::Switch, { 173, 33, 16, 9 }, bindBool ("DYNA", p.dynamics().on));
        add (Cell::Switch, { 175, 64, 16, 9 }, bindBool ("EQ", p.eq().on));
        if (s.isInput())
        {
            auto att = bindLinear ("ATT", p.input.trimDb, -96.0f, 0.0f, "dB", 0, 0.0f);
            att.steps = 97.0f;
            att.text = [&p] { return juce::String (juce::roundToInt (p.input.trimDb.load())) + "dB"; };
            add (Cell::Dial, { 190, 58, 30, 22 }, std::move (att));
        }
    }

    void Display::drawView (Strip& s)
    {
        header (stripName (&s) + "  VIEW");
        channelMeter (s, {}, {});
        const char* gr[] = { "0", "-2", "-4", "-6", "-8", "-18" };
        for (int i = 0; i < 6; ++i)
            text (243 - textWidth (gr[i], true), i * 6, gr[i], true);
        text (240, 37, "GR 1 2", true);
        if (page == 1)
        {
            text (66, 16, "FADER");
            if (s.isInput())
            {
                hLine (96, 124, 12);
                hLine (148, 176, 12);
                text (127, 9, "AUX");
                text (190, 9, "EFFECT");
                for (int i = 0; i < 4; ++i)
                    text (104 + i * 20, 17, juce::String (i + 1), true);
                for (int i = 0; i < 2; ++i)
                    text (193 + i * 20, 17, juce::String (i + 1), true);
            }
            const auto under = [this] (int cx, float db)
            {
                const auto t = lcdLevel (db);
                text (cx - textWidth (t, true) / 2, 60, t, true);
            };
            under (77, s.params.faderDb.load());
            if (s.isInput())
            {
                const int cx[] = { 106, 126, 146, 166, 195, 215 };
                const int bus[] = { kAuxBus, kAuxBus + 1, kAuxBus + 2, kAuxBus + 3, kEffectBus, kEffectBus + 1 };
                for (int i = 0; i < 6; ++i)
                    under (cx[i], s.sends[(size_t) bus[i]].levelDb.load());
            }
            dottedV (92, 9, 79);
            dottedV (180, 9, 79);
            return;
        }
        box ({ 58, 13, 33, 10 });
        textCentred ({ 58, 13, 33, 10 }, "DELAY");
        if (s.isInput())
            text (60, 29, delayUnitLabel(), true);
        hLine (55, 129, 37);
        // The groups and the pair: printed as the page has them; not kept here.
        const auto group = [this] (const char* upper, int y, const char* letters)
        {
            text (60, y, upper, true);
            text (60, y + 6, "GROUP", true);
            for (int i = 0; i < 3; ++i)
            {
                const juce::Rectangle<int> r (91 + i * 12, y + 2, 9, 8);
                box (r);
                hLine (r.getX() + 1, r.getRight(), r.getBottom());
                vLine (r.getRight(), r.getY() + 1, r.getBottom());
                textCentred (r, juce::String::charToString (letters[i]), true);
            }
        };
        if (s.isInput())
        {
            group ("FADER", 40, "ABC");
            group ("MUTE", 53, "DEF");
        }
        text (60, 69, "PAIR", true);
        box ({ 90, 67, 33, 10 });
        textCentred ({ 90, 67, 33, 10 }, "MONOx2", true);
        vLine (130, 0, 79);
        box ({ 133, 1, 32, 9 });
        textCentred ({ 133, 1, 32, 9 }, "ROUTE");
        box ({ 138, 46, 22, 9 });
        textCentred ({ 138, 46, 22, 9 }, "PAN");
        if (s.isInput())
        {
            box ({ 150, 33, 10, 9 });
            textCentred ({ 150, 33, 10, 9 }, "D", true);
        }
        vLine (167, 0, 79);
        box ({ 169, 1, 27, 9 });
        textCentred ({ 169, 1, 27, 9 }, "PHASE", true);
        hLine (167, 229, 19);
        box ({ 169, 21, 29, 9 });
        textCentred ({ 169, 21, 29, 9 }, "DYNA.", true);
        box ({ 205, 27, 21, 22 });
        for (int i = 0; i < 19; ++i) // a compressor's curve
            dot (206 + i, 47 - (i < 10 ? i * 2 : 18 + (i - 10) / 4));
        vLine (230, 0, 51);
        hLine (167, kRightColumn - 1, 52);
        box ({ 172, 54, 20, 9 });
        textCentred ({ 172, 54, 20, 9 }, "EQ");
        if (s.isInput())
            text (200, 54, "ATT");
        box ({ 218, 54, 74, 24 });
        hLine (218, 291, 66);
        for (int x : { 236, 254, 272 })
            vLine (x, 54, 77);
    }

    // MEMORY, page 1 (p.186): the list of scene memories and what to do with the selected one.
    namespace
    {
        constexpr int kMaxMemory = 99;
        const char* const kProtectedKey = Y01VMidi::protectKey;
    } // namespace

    juce::String Display::memoryTitle (int m) const
    {
        if (m == 0)
            return "InitData";
        const auto scene = m - 1 < ctx.show.getNumScenes() ? ctx.show.getScene (m - 1) : std::nullopt;
        return scene ? scene->name : juce::String();
    }

    bool Display::isProtected (int m) const
    {
        return m == 0 || juce::StringArray::fromTokens (ctx.settings.getValue (kProtectedKey), " ", "").contains (juce::String (m));
    }

    void Display::setProtected (int m, bool on)
    {
        if (m == 0)
            return; // 00 is read-only, always
        auto list = juce::StringArray::fromTokens (ctx.settings.getValue (kProtectedKey), " ", "");
        list.removeString (juce::String (m));
        if (on)
            list.add (juce::String (m));
        ctx.settings.setValue (kProtectedKey, list.joinIntoString (" "));
    }

    void Display::setMemory (int m)
    {
        memory = juce::jlimit (0, juce::jmin (kMaxMemory, ctx.show.getNumScenes() + 1), m);
    }

    void Display::storeMemory()
    {
        if (isProtected (memory))
            return;
        const int index = memory - 1;
        const auto store = [safe = juce::Component::SafePointer<Display> (this), index] (const juce::String& title)
        {
            if (safe == nullptr)
                return;
            auto& show = safe->ctx.show;
            if (index < show.getNumScenes())
                show.storeScene (index);
            else
                show.addScene (title);
            if (index < show.getNumScenes())
                show.setSceneName (index, title);
            safe->refresh();
        };
        const auto number = "SCENE " + juce::String (memory).paddedLeft ('0', 2);
        const auto title = index < ctx.show.getNumScenes() ? memoryTitle (memory) : juce::String (number);
        // With STORE CONFIRMATION, the title dialog is the confirmation (p.203).
        if (preference ("y01vStoreConfirm", true))
            askForText (*this, "TITLE EDIT", "Scene " + juce::String (memory).paddedLeft ('0', 2) + ":", title, [store] (const juce::String& t) { store (t.substring (0, 8)); });
        else
            store (title);
    }

    void Display::recallMemory()
    {
        if (memory >= 1 && memory - 1 < ctx.show.getNumScenes())
        {
            const auto recall = [safe = juce::Component::SafePointer<Display> (this), index = memory - 1]
            {
                if (safe != nullptr)
                {
                    safe->ctx.show.recallScene (index);
                    safe->refresh();
                }
            };
            if (! preference ("y01vRecallConfirm", false))
            {
                recall();
                return;
            }
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withTitle ("SCENE RECALL")
                                              .withMessage ("Recall scene " + juce::String (memory).paddedLeft ('0', 2) + "?")
                                              .withButton ("RECALL")
                                              .withButton ("CANCEL")
                                              .withAssociatedComponent (this),
                                          [recall] (int result)
                                          {
                                              if (result == 1)
                                                  recall();
                                          });
            return;
        }
        if (memory != 0)
            return;
        // Scene 00: every setting back to its initial value (p.185), so it asks first.
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withTitle ("RECALL 00")
                                          .withMessage ("This resets every mix setting to its initial value.")
                                          .withButton ("RECALL")
                                          .withButton ("CANCEL")
                                          .withAssociatedComponent (this),
                                      [&context = ctx] (int result)
                                      {
                                          if (result != 1)
                                              return;
                                          juce::MessageManager::callAsync ([&context]
                                          {
                                              context.show.markRecallUndo();
                                              context.engine.configureProfile (ld::kProfileYamaha01V);
                                              if (auto* midi = dynamic_cast<Y01VMidi*> (context.consoleMidi))
                                                  midi->initialSceneRecalled();
                                              if (context.structureChanged)
                                                  context.structureChanged();
                                          });
                                      });
    }

    void Display::undoRecall()
    {
        if (! ctx.show.canUndoRecall())
            return;
        ctx.show.undoRecall();
        // The list follows the scene the console is back on.
        if (const int scene = ctx.show.getCurrentSceneIndex(); scene >= 0)
            setMemory (scene + 1);
        rebuild();
    }

    void Display::editMemoryTitle()
    {
        if (memory < 1 || memory - 1 >= ctx.show.getNumScenes() || isProtected (memory))
            return;
        const int index = memory - 1;
        askForText (*this, "TITLE EDIT", "Scene " + juce::String (memory).paddedLeft ('0', 2) + ":", memoryTitle (memory),
                    [safe = juce::Component::SafePointer<Display> (this), index] (const juce::String& title)
                    {
                        if (safe != nullptr)
                        {
                            safe->ctx.show.setSceneName (index, title.substring (0, 8));
                            safe->refresh();
                        }
                    });
    }

    //==========================================================================
    // MIDI (pp.224-234).
    void Display::saveMidiSettings()
    {
        auto& y = ctx.engine.getYamaha01V();
        ctx.settings.setValue (Y01VMidi::setupKey, y.getSetupText());
        ctx.settings.setValue (Y01VMidi::pcKey, y.getPcText());
        ctx.settings.setValue (Y01VMidi::ccKey, y.getCcText());
    }

    KnobBinding Display::midiSwitch (int what, const juce::String& label)
    {
        KnobBinding b;
        b.label = label;
        b.steps = 2.0f;
        b.get = [this, what] { return ctx.engine.getYamaha01V().getSetup ((Yamaha01V::Setup) what) != 0 ? 1.0f : 0.0f; };
        b.set = [this, what] (float v)
        {
            ctx.engine.getYamaha01V().setSetup ((Yamaha01V::Setup) what, v >= 0.5f ? 1 : 0);
            saveMidiSettings();
        };
        b.text = [this, what] { return juce::String (ctx.engine.getYamaha01V().getSetup ((Yamaha01V::Setup) what) != 0 ? "ON" : "OFF"); };
        return b;
    }

    KnobBinding Display::midiChannel (int what, const juce::String& label)
    {
        KnobBinding b;
        b.label = label;
        b.steps = 16.0f;
        b.get = [this, what] { return (float) ctx.engine.getYamaha01V().getSetup ((Yamaha01V::Setup) what) / 15.0f; };
        b.set = [this, what] (float v)
        {
            ctx.engine.getYamaha01V().setSetup ((Yamaha01V::Setup) what, juce::roundToInt (v * 15.0f));
            saveMidiSettings();
        };
        b.text = [this, what] { return juce::String (ctx.engine.getYamaha01V().getSetup ((Yamaha01V::Setup) what) + 1); };
        return b;
    }

    void Display::buildMidiSetup()
    {
        // p.224.
        auto& tx = add (Cell::Text, { 63, 28, 25, 9 }, midiChannel (Yamaha01V::TxChannel, "Tx CH"));
        tx.dotted = tx.raw = true;
        auto& rx = add (Cell::Text, { 63, 61, 25, 9 }, midiChannel (Yamaha01V::RxChannel, "Rx CH"));
        rx.dotted = rx.raw = true;

        const int xs[] = { 144, 173, 200, 227 }, ys[] = { 15, 31, 47, 63 };
        const int rows[4][4] = { { Yamaha01V::PcTx, Yamaha01V::PcRx, Yamaha01V::PcOmni, Yamaha01V::PcEcho },
                                 { Yamaha01V::CcTx, Yamaha01V::CcRx, Yamaha01V::CcOmni, Yamaha01V::CcEcho },
                                 { Yamaha01V::ParamTx, Yamaha01V::ParamRx, -1, Yamaha01V::ParamEcho },
                                 { -1, Yamaha01V::BulkRx, -1, -1 } };
        const char* columns[] = { "Tx", "Rx", "OMNI", "ECHO" };
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                if (rows[r][c] >= 0)
                    add (Cell::Switch, { xs[c], ys[r], 23, 10 }, midiSwitch (rows[r][c], columns[c]));
    }

    void Display::drawMidiSetup()
    {
        header ("MIDI SETUP");
        const char* columns[] = { "Tx", "Rx", "OMNI", "ECHO" };
        const int xs[] = { 144, 173, 200, 227 }, centers[] = { 155, 184, 211, 238 };
        for (int c = 0; c < 4; ++c)
            textCentred ({ xs[c], 5, 23, 8 }, columns[c]);

        box ({ 58, 12, 34, 32 });
        box ({ 60, 14, 30, 8 });
        textCentred ({ 60, 14, 30, 8 }, "Tx CH");
        box ({ 58, 45, 34, 32 });
        box ({ 60, 47, 30, 8 });
        textCentred ({ 60, 47, 30, 8 }, "Rx CH");

        box ({ 95, 12, 160, 65 });
        vLine (139, 12, 76);
        for (int y : { 29, 45, 60 })
            hLine (95, 254, y);
        const char* names[][2] = { { "PROGRAM", "CHANGE" }, { "CONTROL", "CHANGE" }, { "PARAM.", "CHANGE" } };
        const int nameY[] = { 13, 30, 46 };
        for (int r = 0; r < 3; ++r)
        {
            textCentred ({ 96, nameY[r], 43, 7 }, names[r][0]);
            textCentred ({ 96, nameY[r] + 7, 43, 7 }, names[r][1]);
        }
        textCentred ({ 96, 64, 43, 8 }, "BULK");
        // The dots where a kind of message has no such switch.
        fill ({ centers[2] - 1, 51, 2, 2 });
        for (int c : { 0, 2, 3 })
            fill ({ centers[c] - 1, 67, 2, 2 });

        box ({ 257, 12, 33, 32 });
        box ({ 259, 14, 29, 8 });
        textCentred ({ 259, 14, 29, 8 }, "PORT");
        dottedH (261, 286, 27);
        dottedH (261, 286, 37);
        dottedV (261, 27, 37);
        dottedV (286, 27, 37);
        textCentred ({ 261, 28, 26, 9 }, "MIDI"); // Sidecar has the one port.
        box ({ 257, 45, 33, 32 });
        box ({ 259, 47, 29, 15 });
        textCentred ({ 259, 47, 29, 7 }, "LINK");
        textCentred ({ 259, 54, 29, 7 }, "PORT");
        textCentred ({ 259, 65, 29, 8 }, "HOST");
    }

    void Display::buildMidiProgram()
    {
        // p.227. The program's number 1-128, MIDI's 0-127.
        auto& init = add (Cell::Button, { 234, 7, 54, 10 }, {}, "INITIALIZE");
        init.press = [this] { initializeTable (true); };

        KnobBinding no;
        no.label = "PGM CHG.";
        no.steps = 128.0f;
        no.get = [this] { return (float) midiProgram / 127.0f; };
        no.set = [this] (float v) { midiProgram = juce::roundToInt (v * 127.0f); };
        no.text = [this] { return juce::String (midiProgram + 1); };
        auto& pc = add (Cell::Text, { 108, 49, 23, 8 }, no);
        pc.dotted = pc.raw = true;

        KnobBinding scene;
        scene.label = "SCENE";
        scene.steps = 101.0f;
        scene.get = [this] { return ((float) ctx.engine.getYamaha01V().pcScene (midiProgram) + 1.0f) / 100.0f; };
        scene.set = [this] (float v)
        {
            ctx.engine.getYamaha01V().setPcScene (midiProgram, juce::roundToInt (v * 100.0f) - 1);
            saveMidiSettings();
        };
        scene.text = [this]
        {
            const int m = ctx.engine.getYamaha01V().pcScene (midiProgram);
            if (m < 0)
                return juce::String ("--.[ NONE ]");
            const auto title = memoryTitle (m);
            return juce::String (m).paddedLeft ('0', 2) + "." + (title.isEmpty() ? juce::String ("[ ---- ]") : title.substring (0, 8));
        };
        auto& s = add (Cell::Text, { 148, 49, 70, 8 }, scene);
        s.dotted = s.raw = true;
    }

    void Display::drawMidiProgram()
    {
        header ("PROGRAM CHANGE");
        text (92, 18, "PGM CHG.");
        hLine (82, 137, 25);
        text (151, 18, "NO. TITLE");
        hLine (140, 254, 25);
        for (int row = 0; row < 2; ++row)
        {
            const int pc = midiProgram + 2 - row, y = 29 + row * 10;
            if (pc > 127)
                continue;
            const auto number = juce::String (pc + 1);
            text (131 - textWidth (number, false), y, number);
            text (137, y, "=");
            const int m = ctx.engine.getYamaha01V().pcScene (pc);
            const auto title = memoryTitle (m);
            text (151, y, m < 0 ? juce::String ("--.[ NONE ]") : juce::String (m).paddedLeft ('0', 2) + "." + (title.isEmpty() ? juce::String ("[ ---- ]") : title.substring (0, 8)));
        }
        text (86, 50, "NO.");
        text (137, 50, "=");
    }

    std::vector<int> Display::ccChoices (int column, int code) const
    {
        // Every target, as Yamaha01V::ccLabel names it, in the manual's order.
        static const std::vector<int> all = []
        {
            std::vector<int> v { 0 };
            const auto add = [&v] (int kind, int strip, int extra = 0) { v.push_back (kind | strip << 8 | extra << 16); };
            for (int s = 0; s < 35; ++s)
                add (1, s);
            for (int n = 0; n < 6; ++n)
                for (int s = 0; s < 24; ++s)
                    if (! (s >= 14 && s < 22 && (n == 2 || n == 3))) // CH 17-24 have no AUX 3-4 (p.264)
                        add (4, s, n);
            for (int s = 0; s < 35; ++s)
                add (2, s);
            for (int s = 0; s < 24; ++s)
            {
                add (3, s);
                if (s == 12 || s == 13 || s == 22 || s == 23)
                    add (3, s, 1);
            }
            add (3, 34);
            return v;
        }();
        std::vector<int> out;
        juce::StringArray seen;
        const auto first = Yamaha01V::ccLabel (code, 0), second = Yamaha01V::ccLabel (code, 1);
        for (int c : all)
        {
            if (column >= 1 && Yamaha01V::ccLabel (c, 0) != first)
                continue;
            if (column >= 2 && Yamaha01V::ccLabel (c, 1) != second)
                continue;
            const auto label = Yamaha01V::ccLabel (c, column);
            if (column == 2 || ! seen.contains (label))
            {
                seen.add (label);
                out.push_back (c);
            }
        }
        return out;
    }

    void Display::buildMidiControl()
    {
        // p.229: controllers 0-95 and 102-119.
        auto& init = add (Cell::Button, { 234, 7, 54, 10 }, {}, "INITIALIZE");
        init.press = [this] { initializeTable (false); };

        const auto indexOf = [] (int cc) { return cc < 96 ? cc : cc - 6; };
        const auto ccOf = [] (int index) { return index < 96 ? index : index + 6; };
        KnobBinding no;
        no.label = "CTL CHG.";
        no.steps = 114.0f;
        no.get = [this, indexOf] { return (float) indexOf (midiControl) / 113.0f; };
        no.set = [this, ccOf] (float v) { midiControl = ccOf (juce::roundToInt (v * 113.0f)); };
        no.text = [this] { return juce::String (midiControl); };
        auto& pc = add (Cell::Text, { 78, 45, 22, 8 }, no);
        pc.dotted = pc.raw = true;

        for (int column = 0; column < 3; ++column)
        {
            KnobBinding b;
            b.label = column == 0 ? "PARAMETER" : column == 1 ? "GROUP" : "CHANNEL";
            b.steps = 256.0f;
            b.get = [this, column]
            {
                const int code = ctx.engine.getYamaha01V().ccCode (midiControl);
                const auto choices = ccChoices (column, code);
                const auto label = Yamaha01V::ccLabel (code, column);
                int at = 0;
                for (int i = 0; i < (int) choices.size(); ++i)
                    if (Yamaha01V::ccLabel (choices[(size_t) i], column) == label)
                        at = i;
                return (float) at / 255.0f;
            };
            b.set = [this, column] (float v)
            {
                auto& y = ctx.engine.getYamaha01V();
                const auto choices = ccChoices (column, y.ccCode (midiControl));
                if (choices.empty())
                    return;
                y.setCcCode (midiControl, choices[(size_t) juce::jlimit (0, (int) choices.size() - 1, juce::roundToInt (v * 255.0f))]);
                saveMidiSettings();
            };
            b.text = [this, column] { return Yamaha01V::ccLabel (ctx.engine.getYamaha01V().ccCode (midiControl), column); };
            const int x[] = { 109, 172, 232 }, w[] = { 60, 58, 60 };
            auto& c = add (Cell::Text, { x[column], 45, w[column], 8 }, b);
            c.dotted = c.raw = true;
        }
    }

    void Display::drawMidiControl()
    {
        header ("CONTROL CHANGE");
        text (59, 18, "CTL CHG.");
        hLine (58, 105, 25);
        text (166, 18, "PARAMETER");
        hLine (108, 291, 25);
        for (int row = 0; row < 2; ++row)
        {
            int cc = midiControl + 2 - row;
            if (cc >= 96 && cc < 102)
                cc += 6; // 96-101 can't be assigned
            if (cc > 119)
                continue;
            const int y = 29 + row * 8, code = ctx.engine.getYamaha01V().ccCode (cc);
            const auto number = juce::String (cc);
            text (100 - textWidth (number, false), y, number);
            text (103, y, "=");
            const int x[] = { 109, 172, 232 }, w[] = { 60, 58, 60 };
            for (int column = 0; column < 3; ++column)
                textCentred ({ x[column], y - 1, w[column], 8 }, Yamaha01V::ccLabel (code, column));
        }
        text (58, 46, "No.");
        text (103, 46, "=");
    }

    void Display::initializeTable (bool program)
    {
        auto options = juce::MessageBoxOptions()
                           .withTitle (program ? "CONFIRMATION" : "TABLE INITIALIZE")
                           .withMessage (program ? "Will Initialize PGM Change table.\nARE YOU SURE?" : "Choose an Arrangement.")
                           .withAssociatedComponent (this);
        if (program)
            options = options.withButton ("OK").withButton ("CANCEL");
        else
            options = options.withButton ("DEFAULT").withButton ("P.MIX 01").withButton ("03D").withButton ("CANCEL");
        juce::AlertWindow::showAsync (options,
                                      [safe = juce::Component::SafePointer<Display> (this), program] (int result)
                                      {
                                          if (safe == nullptr)
                                              return;
                                          auto& y = safe->ctx.engine.getYamaha01V();
                                          if (program && result == 1)
                                              y.initProgramTable();
                                          else if (! program && result >= 1 && result <= 3)
                                              y.initControlTable (result - 1);
                                          else
                                              return;
                                          safe->saveMidiSettings();
                                          safe->rebuild();
                                      });
    }

    void Display::buildMidiBulk()
    {
        // p.232.
        add (Cell::Button, { 100, 4, 44, 11 }, {}, "REQUEST").press = [this] { bulk (false); };
        add (Cell::Button, { 155, 4, 44, 11 }, {}, "TRANSMIT").press = [this] { bulk (true); };

        KnobBinding interval;
        interval.label = "INTERVAL";
        interval.steps = 128.0f;
        interval.get = [this] { return (float) ctx.engine.getYamaha01V().getSetup (Yamaha01V::Interval) / 127.0f; };
        interval.set = [this] (float v)
        {
            ctx.engine.getYamaha01V().setSetup (Yamaha01V::Interval, juce::roundToInt (v * 127.0f));
            saveMidiSettings();
        };
        interval.text = [this] { return juce::String (ctx.engine.getYamaha01V().getSetup (Yamaha01V::Interval) * 10); };
        auto& i = add (Cell::Text, { 251, 5, 22, 8 }, interval);
        i.dotted = i.raw = true;

        const struct { const char* name; juce::Rectangle<int> area; } categories[] = {
            { "ALL", { 64, 24, 56, 11 } },         { "SETUP MEM.", { 64, 38, 56, 11 } }, { "PGM. TABLE", { 64, 51, 56, 11 } },
            { "CTL. TABLE", { 64, 64, 56, 11 } },  { "SCENE MEM.", { 124, 38, 57, 11 } }
        };
        for (int k = 0; k < 5; ++k)
        {
            KnobBinding b;
            b.label = categories[k].name;
            b.steps = 2.0f;
            b.get = [this, k] { return bulkCategory == k ? 1.0f : 0.0f; };
            b.set = [this, k] (float) { bulkCategory = k; };
            b.text = [name = juce::String (categories[k].name)] { return name; };
            auto& c = add (Cell::Switch, categories[k].area, b);
            c.raw = true;
        }

        // SCENE MEM.'s FROM and TO: ALL, EDIT, then the memories 1-99.
        KnobBinding from;
        from.label = "FROM";
        from.steps = 101.0f;
        from.get = [this] { return (float) bulkFrom / 100.0f; };
        from.set = [this] (float v) { bulkFrom = juce::roundToInt (v * 100.0f); };
        from.text = [this] { return bulkFrom == 0 ? juce::String ("ALL") : bulkFrom == 1 ? juce::String ("EDIT") : juce::String (bulkFrom - 1); };
        auto& f = add (Cell::Text, { 224, 38, 26, 8 }, from);
        f.dotted = f.raw = true;
        KnobBinding to;
        to.label = "TO";
        to.steps = 99.0f;
        to.get = [this] { return (float) (bulkTo - 1) / 98.0f; };
        to.set = [this] (float v) { bulkTo = juce::roundToInt (v * 98.0f) + 1; };
        to.text = [this] { return bulkFrom < 2 ? juce::String ("---") : juce::String (juce::jmax (bulkTo, bulkFrom - 1)); };
        auto& t = add (Cell::Text, { 259, 38, 26, 8 }, to);
        t.dotted = t.raw = true;
    }

    void Display::drawMidiBulk()
    {
        header ("BULK");
        text (204, 5, "INTERVAL");
        hLine (204, 222, 13);
        vLine (222, 13, 16);
        text (276, 5, "ms");

        // CATEGORY.
        box ({ 58, 20, 232, 57 });
        fill ({ 149, 17, 51, 8 }, false);
        box ({ 149, 17, 51, 8 });
        textCentred ({ 149, 17, 51, 8 }, "CATEGORY");
        text (229, 34, "FROM", true);
        text (268, 34, "TO", true);
        text (196, 46, "TYPE", true);
        hLine (181, 223, 42);
        for (int y : { 42, 55, 68 })
        {
            hLine (250, 258, y);
            if (y != 42)
                hLine (216, 223, y);
        }
        hLine (181, 189, 55);
        hLine (181, 189, 68);

        // LIBRARY and REMOTE: Sidecar's 01V has no user library or REMOTE page yet.
        for (auto [name, y] : { std::pair { "LIBRARY", 51 }, std::pair { "REMOTE", 64 } })
        {
            box ({ 124, y, 57, 11 });
            hLine (125, 181, y + 11);
            vLine (181, y + 1, y + 11);
            textCentred ({ 124, y, 57, 11 }, name);
            for (int yy = y + 1; yy < y + 10; ++yy)
                for (int x = 125; x < 180; ++x)
                    if (((x + yy) & 1) == 0)
                        dot (x, yy, false);
        }
        const auto dimmedField = [this] (juce::Rectangle<int> r, const juce::String& s)
        {
            dottedH (r.getX() - 1, r.getRight(), r.getY() - 1);
            dottedH (r.getX() - 1, r.getRight(), r.getBottom());
            dottedV (r.getX() - 1, r.getY() - 1, r.getBottom());
            dottedV (r.getRight(), r.getY() - 1, r.getBottom());
            textCentred (r, s, true);
        };
        dimmedField ({ 190, 51, 26, 8 }, "EQ");
        dimmedField ({ 190, 64, 26, 8 }, "INTP");
        for (int y : { 51, 64 })
        {
            dimmedField ({ 224, y, 26, 8 }, "ALL");
            dimmedField ({ 259, y, 26, 8 }, "---");
        }
    }

    void Display::bulk (bool transmit)
    {
        auto* midi = dynamic_cast<Y01VMidi*> (ctx.consoleMidi);
        if (midi == nullptr)
            return;
        const auto send = [midi, transmit] (char kind, int number)
        {
            if (transmit)
                midi->transmit (kind, number);
            else
                midi->request (kind, number);
        };
        const auto scenes = [&] (int first, int last)
        {
            for (int m = first; m <= last; ++m)
                send ('M', m);
        };
        const int lastScene = transmit ? juce::jmin (99, ctx.show.getNumScenes()) : 99;
        switch (bulkCategory)
        {
            case 0: // ALL: everything below
                send ('S', 0);
                send ('P', 0);
                send ('C', 0);
                send ('M', 127);
                scenes (1, lastScene);
                break;
            case 1: send ('S', 0); break;
            case 2: send ('P', 0); break;
            case 3: send ('C', 0); break;
            default:
                if (bulkFrom == 0)
                {
                    send ('M', 127);
                    scenes (1, lastScene);
                }
                else if (bulkFrom == 1)
                    send ('M', 127);
                else
                    scenes (bulkFrom - 1, juce::jmin (juce::jmax (bulkTo, bulkFrom - 1), lastScene));
                break;
        }
    }

    void Display::drawMidiLocal()
    {
        // p.234: as the page stands with LOCAL CONTROL ON. Its OFF (the panel works
        // a remote 01V over MIDI) isn't built.
        header ("LOCAL CONTROL");
        text (151, 7, "LOCAL CONTROL:");
        fill ({ 236, 5, 33, 10 });
        textCentred ({ 236, 5, 33, 10 }, "ON", false, false);
        for (int y : { 19, 20 })
            hLine (58, 291, y);
        box ({ 58, 22, 134, 54 });

        text (63, 24, "CONTROLLER");
        box ({ 64, 31, 47, 38 });
        const int xs[] = { 71, 81, 92, 102 };
        for (int x : xs)
        {
            dial (x, 37, 3, 0.5f);
            box ({ x - 4, 45, 8, 5 });
            vLine (x, 52, 67);
            fill ({ x - 2, 58, 5, 4 });
        }
        for (int y : { 51, 52, 53 })
            hLine (113, 137, y);
        for (int i = 0; i < 7; ++i)
            vLine (138 + i, 48 + i, 56 - i);

        textCentred ({ 139, 25, 54, 7 }, "INTERNAL");
        textCentred ({ 139, 32, 54, 7 }, "PARAMETER");
        box ({ 145, 40, 44, 26 });
        for (int x = 147; x < 188; x += 3)
        {
            vLine (x, 41, 43);
            vLine (x, 63, 65);
        }
        box ({ 150, 45, 34, 16 });
        for (int x : { 160, 168 })
        {
            vLine (x + 3, 48, 57);
            fill ({ x, 56, 4, 3 });
        }
        hLine (163, 171, 48);
    }

    void Display::buildMemory()
    {
        setMemory (memory);
        add (Cell::Button, { 61, 14, 35, 17 }, {}, "TITLE|EDIT").press = [this] { editMemoryTitle(); };
        add (Cell::Button, { 61, 36, 35, 15 }, {}, "RECALL").press = [this] { recallMemory(); };
        add (Cell::Button, { 61, 58, 35, 15 }, {}, "STORE").press = [this] { storeMemory(); };
        // Gray until a scene has been recalled (p.189).
        auto& undo = add (Cell::Button, { 254, 57, 35, 17 }, {}, "RECALL|UNDO");
        undo.tiny = true;
        undo.enabled = [this] { return ctx.show.canUndoRecall(); };
        undo.press = [this] { undoRecall(); };

        KnobBinding list;
        list.label = "MEMORY";
        list.steps = (float) kMaxMemory + 1.0f;
        list.get = [this] { return (float) memory / (float) kMaxMemory; };
        list.set = [this] (float v) { setMemory (juce::roundToInt (v * (float) kMaxMemory)); };
        list.text = [this] { return juce::String (memory).paddedLeft ('0', 2); };
        add (Cell::List, { 105, 9, 138, 35 }, list);

        KnobBinding protect;
        protect.label = "PROTECT";
        protect.steps = 2.0f;
        protect.get = [this] { return isProtected (memory) ? 1.0f : 0.0f; };
        protect.set = [this] (float v) { setProtected (memory, v >= 0.5f); };
        protect.text = [this] { return isProtected (memory) ? juce::String ("ON") : juce::String ("OFF"); };
        add (Cell::Switch, { 255, 33, 34, 12 }, protect);
    }

    void Display::drawMemory()
    {
        header ("MEMORY");
        text (116, 1, "No.");
        text (150, 1, "TITLE");
        // The list's frame, with PROTECT in a bay on its right (p.182).
        vLine (105, 9, 79);
        hLine (105, 243, 9);
        vLine (243, 9, 31);
        hLine (243, 250, 31);
        vLine (250, 27, 31);
        hLine (250, kRightColumn - 1, 27);
        hLine (250, kRightColumn - 1, 49);
        vLine (250, 45, 49);
        hLine (243, 250, 45);
        vLine (243, 45, 79);
        text (252, 20, "PROTECT");

        for (int row = 0; row < 3; ++row)
        {
            const int m = memory + 2 - row, y = 11 + row * 11;
            if (m > kMaxMemory)
                continue;
            const auto title = memoryTitle (m);
            const auto line = juce::String (m).paddedLeft ('0', 2) + "." + (title.isEmpty() ? juce::String ("[ ---- ]") : title.substring (0, 8));
            const bool selected = row == 2;
            if (selected)
                fill ({ 115, y - 1, 79, 11 });
            textTall (116, y, line, kTitleRows, 7.0f, false, ! selected);
            if (selected)
            {
                for (int i = 0; i < 5; ++i)
                    vLine (108 + i, y + i - 1, y + 9 - i);
                if (isProtected (m))
                {
                    fill ({ 197, y - 1, 44, 11 });
                    text (199, y + 2, "READ ONLY", true, 1, false);
                    for (int i = 0; i < 4; ++i)
                        vLine (247 - i, y + 1 + i, y + 7 - i);
                }
            }
        }
    }

    // EFFECT page 1 (p.136): the effect's parameters, nine at a time.
    void Display::buildEffectEdit()
    {
        auto& fx = ctx.engine.getFx();
        const int slot = effectSlot(), model = fx.getModel (slot);
        if (model <= ld::kFxExt)
            return;
        const auto& spec = models::model (models::Fx, model);
        const int first = fxScroll * 3;
        for (int i = first; i < juce::jmin ((int) spec.params.size(), first + 9); ++i)
        {
            const int n = i - first;
            auto& c = add (Cell::Text, { 72 + (n % 3) * 59, 29 + (n / 3) * 19, 32, 9 }, bindModelParam (fx.params (slot).values[i], spec.params[(size_t) i]), spec.params[(size_t) i].label.toUpperCase());
            c.dotted = true;
        }
    }

    void Display::drawEffectEdit()
    {
        const int slot = effectSlot(), model = ctx.engine.getFx().getModel (slot);
        header ("EFFECT " + juce::String (slot + 1) + " EDIT");
        if (model <= ld::kFxExt)
        {
            textCentred ({ kLeftColumn + 1, 34, kRightColumn - kLeftColumn - 1, 12 }, "No effect here.");
            return;
        }
        const auto& spec = models::model (models::Fx, model);
        text (143, 1, "TITLE");
        text (149, 11, "TYPE");
        box ({ 172, 0, 77, 10 });
        box ({ 172, 10, 77, 10 });
        text (174, 2, spec.name.substring (0, 12));
        text (174, 12, spec.name.toUpperCase().substring (0, 12));
        // The arrows, when there's more above or below.
        if (fxScroll > 0)
            for (int i = 0; i < 4; ++i)
                hLine (61 - i, 61 + i, 60 + i);
        if ((fxScroll + 3) * 3 < (int) spec.params.size())
            for (int i = 0; i < 4; ++i)
                hLine (61 - i, 61 + i, 74 - i);
        // The effect's return.
        const auto& aux = ctx.engine.getAux();
        if (slot < (int) aux.size())
            channelMeter (*aux[(size_t) slot], "L", "R");
    }

    // EFFECT page 2, the library (p.134): the 01V's types, in its list order.
    juce::Array<int> Display::libraryModels() const
    {
        juce::Array<int> list;
        const auto& table = models::table (models::Fx);
        for (int i = 0; i < (int) table.size(); ++i)
        {
            const auto& token = table[(size_t) i].token;
            if (models::consoleOf (token) != "01V")
                continue;
            // HQ.PITCH, the dual pitch and FREEZE run on Effect 2 only (p.134).
            if (effectSlot() == 0 && (token == "O1HQP" || token == "O1DUALP" || token == "O1FRZ"))
                continue;
            list.add (i);
        }
        return list;
    }

    void Display::recallLibrary()
    {
        const auto list = libraryModels();
        if (! juce::isPositiveAndBelow (library, list.size()) || effectSlot() < 0)
            return;
        const int slot = effectSlot(), model = list[library];
        const auto recall = [safe = juce::Component::SafePointer<Display> (this), slot, model]
        {
            if (safe != nullptr)
            {
                safe->ctx.engine.getFx().setModel (slot, model);
                safe->fxScroll = 0;
                safe->rebuild();
            }
        };
        if (! preference ("y01vRecallConfirm", false))
        {
            recall();
            return;
        }
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withTitle ("EFFECT RECALL")
                                          .withMessage ("Recall " + models::model (models::Fx, model).name + "?")
                                          .withButton ("RECALL")
                                          .withButton ("CANCEL")
                                          .withAssociatedComponent (this),
                                      [recall] (int result)
                                      {
                                          if (result == 1)
                                              recall();
                                      });
    }

    Strip* Display::sendColumn (int i, juce::String& name) const
    {
        const auto& channels = ctx.engine.getChannels();
        const auto& aux = ctx.engine.getAux();
        if (i < 14 && i < (int) channels.size())
        {
            name = i < 12 ? juce::String (i + 1) : juce::String (13 + 2 * (i - 12)) + "-" + juce::String (14 + 2 * (i - 12));
            return channels[(size_t) i].get();
        }
        if (i >= 14 && i - 14 < (int) aux.size() && sendBus() != kEffectBus + (i - 14)) // a return can't feed its own effect
        {
            name = (effectSlot() >= 0 ? "EFF" : "RTN") + juce::String (i - 13);
            return aux[(size_t) (i - 14)].get();
        }
        return nullptr;
    }

    // The PRE/POST pages: EFFECT page 3 (p.125), AUX page 1 (p.97).
    void Display::buildPrePost()
    {
        const int bus = sendBus();
        for (int i = 0; i < 16; ++i)
        {
            juce::String name;
            auto* s = sendColumn (i, name);
            if (s == nullptr)
                continue;
            add (Cell::Switch, { 62 + (i % 8) * 29, 25 + (i / 8) * 35, 18, 9 }, prePostBinding (s->sends[(size_t) bus], "POST"));
        }
    }

    void Display::drawPrePost()
    {
        const int bus = sendBus();
        const auto& buses = ctx.engine.getBuses();
        header (functionName().replace ("EFFECT", "EFFECT ") + " PRE/POST");
        if (juce::isPositiveAndBelow (bus, (int) buses.size()))
        {
            const float db = buses[(size_t) bus]->params.faderDb.load();
            const auto what = effectSlot() >= 0 ? juce::String ("SEND MASTER= ") : functionName() + " MASTER= ";
            text (175, 1, what + juce::String (db, 1) + "dB");
        }
        for (int i = 0; i < 16; ++i)
        {
            juce::String name;
            auto* s = sendColumn (i, name);
            const int x = 62 + (i % 8) * 29, row = i / 8, cx = x + 9;
            if (s == nullptr)
                continue;
            const bool tiny = name.length() > 4;
            text (cx - textWidth (name, tiny) / 2, 15 + row * 35, name, tiny);
            const auto value = lcdLevel (s->sends[(size_t) bus].levelDb.load());
            text (cx - textWidth (value, true) / 2, 39 + row * 35, value, true);
        }
        for (int c = 0; c < 7; ++c)
            dottedV (87 + c * 29, 13, kDotsH - 1);
    }

    // UTILITY page 1, the oscillator (p.204).
    namespace
    {
        const char* const kWaves[] = { "100Hz", "1kHz", "10kHz", "PINK", "BURST" };
    } // namespace

    void Display::applyOscillator()
    {
        const auto* e = ctx.engine.getHandle();
        const float freqs[] = { 100.0f, 1000.0f, 10000.0f, 1000.0f, 1000.0f };
        const auto gain = [this] (int i) { return oscAssign[(size_t) i] ? 1.0f : 0.0f; };
        for (int i = 0; i < 4; ++i)
        {
            ld::ld_engine_console_osc_assign (e, (int) StripKind::Bus, kAuxBus + i, gain (i), gain (i));
            ld::ld_engine_console_osc_assign (e, (int) StripKind::Bus, i, gain (4 + i), gain (4 + i));
        }
        for (int i = 0; i < 2; ++i)
            ld::ld_engine_console_osc_assign (e, (int) StripKind::Bus, kEffectBus + i, gain (8 + i), gain (8 + i));
        ld::ld_engine_console_osc_assign (e, (int) StripKind::Main, 0, gain (10), gain (10));
        ld::ld_engine_console_osc_set (e, oscOn, oscWave >= 3 ? ld::kOscPink : ld::kOscSine, freqs[oscWave], oscLevelDb, oscWave == 4, true);
    }

    void Display::buildOscillator()
    {
        const auto flag = [this] (const juce::String& label, std::function<bool()> get, std::function<void (bool)> set, const juce::String& on, const juce::String& off)
        {
            KnobBinding b;
            b.label = label;
            b.steps = 2.0f;
            b.get = [get] { return get() ? 1.0f : 0.0f; };
            b.set = [this, set] (float v)
            {
                set (v >= 0.5f);
                applyOscillator();
            };
            b.text = [get, on, off] { return get() ? on : off; };
            return b;
        };
        add (Cell::Switch, { 72, 23, 34, 12 }, flag ("OSC", [this] { return oscOn; }, [this] (bool v) { oscOn = v; }, "ON", "OFF"));

        KnobBinding wave;
        wave.label = "WAVEFORM";
        wave.steps = 5.0f;
        wave.get = [this] { return (float) oscWave / 4.0f; };
        wave.set = [this] (float v)
        {
            oscWave = juce::roundToInt (v * 4.0f);
            applyOscillator();
        };
        wave.text = [this] { return juce::String (kWaves[oscWave]); };
        auto& waveCell = add (Cell::Text, { 62, 59, 55, 10 }, std::move (wave));
        waveCell.dotted = true;
        waveCell.raw = true;

        KnobBinding level;
        level.label = "LEVEL";
        level.steps = 97.0f;
        level.get = [this] { return 1.0f + oscLevelDb / 96.0f; };
        level.set = [this] (float v)
        {
            oscLevelDb = (float) juce::roundToInt (96.0f * (v - 1.0f));
            applyOscillator();
        };
        level.text = [this] { return juce::String (juce::roundToInt (oscLevelDb)) + "dB"; };
        add (Cell::Dial, { 122, 54, 32, 24 }, std::move (level));

        const auto assign = [this, &flag] (int i, const juce::String& name, juce::Rectangle<int> area)
        {
            add (Cell::Switch, area, flag (name, [this, i] { return oscAssign[(size_t) i]; }, [this, i] (bool v) { oscAssign[(size_t) i] = v; }, name, name));
        };
        for (int i = 0; i < 4; ++i)
        {
            assign (i, juce::String (i + 1), { 180 + i * 15, 56, 12, 9 });
            assign (4 + i, juce::String (i + 1), { 180 + i * 15, 67, 12, 9 });
        }
        for (int i = 0; i < 2; ++i)
            assign (8 + i, juce::String (i + 1), { 260 + i * 16, 67, 12, 9 });
        assign (10, "STEREO", { 250, 56, 36, 9 });
    }

    void Display::meterColumn (int x, int y, float db)
    {
        // Six segments, CLIP at the top (p.204's picture).
        const float marks[] = { 0.0f, -6.0f, -12.0f, -18.0f, -30.0f, -48.0f };
        for (int i = 0; i < 6; ++i)
        {
            if (db >= marks[i])
            {
                hLine (x, x + 4, y + i * 6);
                hLine (x, x + 4, y + i * 6 + 1);
            }
            else
                dottedH (x, x + 4, y + i * 6);
        }
    }

    void Display::meterScale (int x, int y)
    {
        const char* marks[] = { "CLIP", "-6", "-12", "-18", "-30", "-48" };
        for (int i = 0; i < 6; ++i)
            text (x + 16 - textWidth (marks[i], true), y + i * 6 - 2, marks[i], true);
    }

    void Display::drawOscillator()
    {
        header ("OSCILLATOR");
        // The meters: AUX, BUS and the two returns, the scale down both sides.
        const auto level = [this] (Strip* s, int side) { return s == nullptr ? -100.0f : juce::Decibels::gainToDecibels (ctx.meters.get (s, MeterPoint::Output, side), -100.0f); };
        meterScale (126, 2);
        meterScale (274, 2);
        const auto& buses = ctx.engine.getBuses();
        const auto& aux = ctx.engine.getAux();
        for (int i = 0; i < 4; ++i)
        {
            meterColumn (143 + i * 9, 2, level (kAuxBus + i < (int) buses.size() ? buses[(size_t) (kAuxBus + i)].get() : nullptr, 0));
            meterColumn (188 + i * 9, 2, level (i < (int) buses.size() ? buses[(size_t) i].get() : nullptr, 0));
        }
        for (int i = 0; i < 2; ++i)
            for (int side = 0; side < 2; ++side)
                meterColumn (232 + i * 25 + side * 10, 2, level (i < (int) aux.size() ? aux[(size_t) i].get() : nullptr, side));
        text (152, 37, "AUX", true);
        text (201, 37, "BUS", true);
        text (234, 37, "RTN1", true);
        text (258, 37, "RTN2", true);
        // The frames, each with its name in a box.
        box ({ 57, 44, 62, 36 });
        box ({ 120, 44, 37, 36 });
        box ({ 158, 44, 133, 36 });
        const auto tab = [this] (juce::Rectangle<int> r, const char* name)
        {
            box (r);
            textCentred (r, name);
        };
        tab ({ 62, 45, 53, 10 }, "WAVEFORM");
        tab ({ 121, 45, 34, 10 }, "LEVEL");
        tab ({ 162, 45, 43, 10 }, "ASSIGN");
        text (162, 58, "AUX", true);
        text (162, 69, "BUS", true);
        text (242, 69, "EFF", true);
    }

    // PAN/ROUT page 2 (p.55's picture): each channel's and return's routing switches.
    void Display::buildRouting()
    {
        const auto& channels = ctx.engine.getChannels();
        const auto& aux = ctx.engine.getAux();
        for (int i = 0; i < 16; ++i)
        {
            Strip* s = i < 14 ? (i < (int) channels.size() ? channels[(size_t) i].get() : nullptr) : (i - 14 < (int) aux.size() ? aux[(size_t) (i - 14)].get() : nullptr);
            if (s == nullptr)
                continue;
            const int x = 61 + (i % 8) * 29, y = 7 + (i / 8) * 40;
            for (int b = 0; b < 4; ++b)
                add (Cell::Switch, { x + (b % 2) * 12, y + (b / 2) * 11, 10, 9 }, bindRoute (s->sends[(size_t) b], juce::String (b + 1)));
            add (Cell::Switch, { x, y + 22, 10, 9 }, bindBool ("ST", s->mains[0].on, "ST", "ST"));
        }
    }

    void Display::drawRouting()
    {
        for (int i = 0; i < 16; ++i)
        {
            const auto name = i < 12 ? juce::String (i + 1) : i < 14 ? juce::String (13 + 2 * (i - 12)) + "-" + juce::String (14 + 2 * (i - 12)) : "RTN" + juce::String (i - 13);
            const bool tiny = name.length() > 4;
            const int x = 61 + (i % 8) * 29, row = i / 8;
            text (x + 11 - textWidth (name, tiny) / 2, row * 40, name, tiny);
            // CH 1-12's direct-out switch: set by OPTION's output assignment on the console (p.49).
            if (i < 12)
            {
                const juce::Rectangle<int> r (x + 12, 7 + row * 40 + 22, 10, 9);
                box (r);
                hLine (r.getX() + 1, r.getRight(), r.getBottom());
                vLine (r.getRight(), r.getY() + 1, r.getBottom());
                textCentred (r, "D", true);
            }
        }
        for (int c = 0; c < 7; ++c)
            dottedV (87 + c * 29, 0, kDotsH - 1);
    }

    // OPTION page 2 (p.43's and p.49's pictures): what CH 17-24 have instead of a strip's keys.
    Strip* Display::optionStrip() const
    {
        const auto& channels = ctx.engine.getChannels();
        auto* s = selected();
        for (int i = kOptionChannel; i < (int) channels.size(); ++i)
            if (channels[(size_t) i].get() == s)
                return s;
        return kOptionChannel < (int) channels.size() ? channels[(size_t) kOptionChannel].get() : nullptr;
    }

    void Display::buildOption()
    {
        auto* s = optionStrip();
        if (page == 3)
            buildOptionOut();
        else if (page == 4)
            buildSwap();
        if (s == nullptr || page != 1)
            return;
        auto& p = s->params;
        for (int i = 0; i < 4; ++i)
            add (Cell::Switch, { 57 + (i % 2) * 12, 17 + (i / 2) * 12, 10, 9 }, bindRoute (s->sends[(size_t) i], juce::String (i + 1)));
        add (Cell::Switch, { 57, 41, 10, 9 }, bindBool ("ST", s->mains[0].on, "ST", "ST"));
        add (Cell::Dial, { 55, 58, 26, 22 }, bindPan01V ("PAN", p.pan));

        add (Cell::Bar, { 90, 23, 13, 39 }, bindFader ("FADER", p.faderDb));
        add (Cell::Switch, { 88, 69, 16, 9 }, onBinding (p));
        // Only AUX 1-2 and EFFECT 1-2: all that CH 17-24 can reach (p.264).
        const int sendBuses[] = { kAuxBus, kAuxBus + 1, kEffectBus, kEffectBus + 1 };
        const int sendX[] = { 113, 132, 157, 176 };
        for (int i = 0; i < 4; ++i)
        {
            auto& send = s->sends[(size_t) sendBuses[i]];
            add (Cell::Bar, { sendX[i], 23, 13, 39 }, bindFader ("SEND", send.levelDb));
            add (Cell::Switch, { sendX[i] - 2, 69, 16, 9 }, prePostBinding (send));
        }

        auto& eq = p.eq();
        add (Cell::Switch, { 222, 31, 16, 10 }, bindBool ("EQ", eq.on));
        auto att = bindLinear ("ATT", p.input.trimDb, -96.0f, 0.0f, "dB", 0, 0.0f);
        att.steps = 97.0f;
        att.text = [&p] { return juce::String (juce::roundToInt (p.input.trimDb.load())) + "dB"; };
        add (Cell::DialLine, { 247, 28, 46, 17 }, std::move (att), "ATT");
        const char* bands[] = { "l", "h" };
        const char* rows[] = { "f", "g", "q" };
        for (int band = 0; band < 2; ++band)
            for (int row = 0; row < 3; ++row)
            {
                auto b = bindParam (models::ChannelEq, eq.model.load(), eq.values, juce::String (bands[band]) + rows[row], {});
                if (b.isValid())
                    add (Cell::Text, { 206 + band * 43, 54 + row * 9, 42, 8 }, std::move (b));
            }
    }

    void Display::drawOption()
    {
        auto* s = optionStrip();
        if (page != 1)
        {
            if (page == 3)
                drawOptionOut();
            else if (page == 4)
                drawSwap();
            else
                drawOptionMeter (page == 2);
            return;
        }
        if (s == nullptr)
            return;
        header (stripName (s) + " CONTROL");
        text (57, 10, "ROUTE", true);
        text (88, 10, "FADER", true);
        text (119, 10, "AUX", true);
        text (163, 10, "EFFECT", true);
        for (int i = 0; i < 2; ++i)
        {
            text (117 + i * 19, 17, juce::String (i + 1), true);
            text (161 + i * 19, 17, juce::String (i + 1), true);
        }
        text (61, 53, "PAN", true);
        for (int x : { 108, 151, 193 })
            dottedV (x, 9, kDotsH - 1);
        const auto under = [this] (int cx, float db)
        {
            const auto t = lcdLevel (db);
            text (cx - textWidth (t, true) / 2, 61, t, true);
        };
        under (96, s->params.faderDb.load());
        const int sendBuses[] = { kAuxBus, kAuxBus + 1, kEffectBus, kEffectBus + 1 };
        const int sendCx[] = { 119, 138, 163, 182 };
        for (int i = 0; i < 4; ++i)
            under (sendCx[i], s->sends[(size_t) sendBuses[i]].levelDb.load());

        text (198, 11, "EQ");
        box ({ 215, 1, 72, 26 });
        hLine (215, 286, 14);
        for (int x : { 233, 251, 269 })
            vLine (x, 1, 26);
        text (198, 34, "EQ ON", true);
        hLine (205, 293, 46);
        for (int y : { 53, 62, 71, 79 })
            hLine (198, 293, y);
        vLine (198, 53, 79);
        for (int x : { 205, 248 })
            vLine (x, 46, 79);
        const char* bands[] = { "LOW", "HIGH" };
        for (int band = 0; band < 2; ++band)
            textCentred ({ 206 + band * 43, 46, 42, 7 }, bands[band]);
        const char* rows[] = { "F", "G", "Q" };
        for (int row = 0; row < 3; ++row)
            text (200, 55 + row * 9, rows[row], true);
    }

    // UTILITY pages 2 and 3, the preferences (p.203).
    namespace
    {
        struct Pref
        {
            const char* key;
            const char* first;
            const char* second;
            bool initial;
        };
        const Pref kPrefs[][4] = {
            { { "y01vEqAutoScreen", "EQ", "AUTO SCREEN", true },
              { "y01vPanAutoScreen", "PANPOT", "AUTO SCREEN", true },
              { "y01vStoreConfirm", "STORE", "CONFIRMATION", true },
              { "y01vRecallConfirm", "RECALL", "CONFIRMATION", false } },
            { { "y01vIncDecRecall", "INC/DEC", "SCENE RECALL", false },
              { "y01vLibraryUp", "LIBRARY", "LIST ORDER", true },
              { nullptr, nullptr, nullptr, false },
              { nullptr, nullptr, nullptr, false } },
        };
    } // namespace

    void Display::buildPreferences()
    {
        const auto& prefs = kPrefs[page - 1];
        for (int i = 0; i < 4 && prefs[i].key != nullptr; ++i)
        {
            const auto& p = prefs[i];
            const bool isOrder = juce::String (p.key) == "y01vLibraryUp";
            KnobBinding b;
            b.label = p.first;
            b.steps = 2.0f;
            b.get = [this, p] { return preference (p.key, p.initial) ? 1.0f : 0.0f; };
            b.set = [this, p] (float v) { setPreference (p.key, v >= 0.5f); };
            b.text = [this, p, isOrder] { return juce::String (preference (p.key, p.initial) ? (isOrder ? "UP" : "ON") : (isOrder ? "DOWN" : "OFF")); };
            add (Cell::Switch, { isOrder ? 129 : 144, 13 + i * 18, isOrder ? 32 : 16, 9 }, std::move (b));
        }
        if (page != 1)
            return;
        // The right half of PREFERENCES-1 (p.203). The two switches are kept and do nothing here:
        // Sidecar has no digital input to lose its wordclock, and shows no "SELECTED CH" message.
        const Pref right[] = { { "y01vSyncCaution", "DIGITAL IN", "SYNC CAUTION", true }, { "y01vSelectedChMessage", "\"SELECTED CH\"", "MESSAGE", true } };
        for (int i = 0; i < 2; ++i)
        {
            const auto p = right[i];
            KnobBinding b;
            b.label = p.first;
            b.steps = 2.0f;
            b.get = [this, p] { return preference (p.key, p.initial) ? 1.0f : 0.0f; };
            b.set = [this, p] (float v) { setPreference (p.key, v >= 0.5f); };
            b.text = [this, p] { return juce::String (preference (p.key, p.initial) ? "ON" : "OFF"); };
            add (Cell::Switch, { 263, 13 + i * 18, 16, 9 }, std::move (b));
        }
        const char* units[] = { "msec", "meter", "sample" };
        for (int i = 0; i < 3; ++i)
        {
            KnobBinding b;
            b.label = "DELAY UNITS";
            b.steps = 2.0f;
            b.get = [this, i] { return ctx.settings.getIntValue (kDelayUnitsKey, 0) == i ? 1.0f : 0.0f; };
            b.set = [this, i] (float)
            {
                ctx.settings.setValue (kDelayUnitsKey, i);
                shownDots.clear();
            };
            b.text = [name = juce::String (units[i])] { return name; };
            auto& c = add (Cell::Switch, { 180 + i * 37, 59, 34, 9 }, std::move (b));
            c.raw = c.tiny = true;
        }
    }

    void Display::drawPreferences()
    {
        header ("PREFERENCES-" + juce::String (page));
        const auto& prefs = kPrefs[page - 1];
        for (int i = 0; i < 4 && prefs[i].key != nullptr; ++i)
        {
            text (62, 11 + i * 18, prefs[i].first);
            text (62, 18 + i * 18, prefs[i].second);
            vLine (167, 9 + i * 18, 25 + i * 18);
            hLine (58, 167, 25 + i * 18);
        }
        if (page != 1)
            return;
        // The right half of PREFERENCES-1 (p.203): its switches are fields.
        const auto right = [this] (int row, const char* a, const char* b)
        {
            text (180, 11 + row * 18, a);
            text (180, 18 + row * 18, b);
            vLine (287, 9 + row * 18, 25 + row * 18);
            hLine (178, 287, 25 + row * 18);
        };
        right (0, "DIGITAL IN", "SYNC CAUTION");
        right (1, "\"SELECTED CH\"", "MESSAGE");
        text (180, 45, "DELAY UNITS");
        vLine (287, 45, 79);
        hLine (178, 287, 79);
    }

    //==========================================================================
    // The second pass: SETUP, the outputs, REMOTE and the libraries. The layouts are the
    // Owner's Manual's pictures of these pages; what each field does is its text. What the
    // engine doesn't keep (groups, pairs, REMOTE's banks, the user programs) is the surface's
    // own, in the app's settings: not in a scene and not on MIDI.
    namespace
    {
        constexpr const char* kPanelKey = "y01v.panel";
        constexpr const char* kLibraryKey = "y01v.library";
        /** The output ports (livedaw_engine::profile's y01v_factory_outputs): OMNI OUT 1-4, the card's eight. */
        constexpr int kOmniPort = 2, kOptionPort = 16;
        /** The input ports: the card's eight after CH 1-16's jacks, 2TR IN, the coaxial DIGITAL IN. */
        constexpr int kOptionInput = 16, kDigitalInput = 26;
        /** AUX 1-4, BUS 1-4, ST L, ST R, CH 1-16 (p.116). */
        constexpr int kNumOutputChoices = 26;
        constexpr int kGroupRows[6] = { 10, 21, 32, 47, 58, 69 };
        constexpr int kGroupCx[14] = { 112, 121, 130, 139, 153, 162, 171, 180, 194, 203, 212, 221, 236, 252 };

        juce::String outputChoiceName (int choice)
        {
            if (choice < 0)
                return "----";
            if (choice < 4)
                return "AUX" + juce::String (choice + 1);
            if (choice < 8)
                return "BUS" + juce::String (choice - 3);
            if (choice < 10)
                return choice == 8 ? "ST L" : "ST R";
            return "CH" + juce::String (choice - 9);
        }

        /** The engine's source of an output's choice. A channel's is its direct out after the fader
            (p.116); 13/14 and 15/16 are the two sides of a stereo strip. */
        int outputChoiceSource (int choice)
        {
            if (choice < 4)
                return MixerEngine::stripSource (StripKind::Bus, kAuxBus + choice, 0);
            if (choice < 8)
                return MixerEngine::stripSource (StripKind::Bus, choice - 4, 0);
            if (choice < 10)
                return MixerEngine::stripSource (StripKind::Main, 0, choice - 8);
            const int ch = choice - 10;
            return DirectOut { StripKind::Channel, ch < 12 ? ch + 1 : 13 + (ch - 12) / 2, ch < 12 ? 0 : (ch - 12) % 2, true }.code();
        }

        // REMOTE page 1's parameters (pp.195-196). A strip here is a code: 0-21 the channels,
        // 22-23 the returns, 24-27 AUX 1-4, 28-31 BUS 1-4, 32-33 EFFECT 1-2, 34 ST OUT.
        enum RemoteWhat
        {
            rNone,
            rFader,
            rSend,
            rDelayTime,
            rEq,
            rAtt,
            rFx,
            rPan,
            rOn,
            rBusToSt,
            rPhase,
            rPrePost,
            rDelayOn,
            rEqOn,
            rDynOn
        };

        struct RemoteTarget
        {
            juce::String a, b, c;
            int what, strip, n;
        };

        juce::String remoteStripName (int s)
        {
            if (s < 12)
                return "CH" + juce::String (s + 1);
            if (s < 14)
                return "CH" + juce::String (13 + 2 * (s - 12)) + "-" + juce::String (14 + 2 * (s - 12));
            if (s < 22)
                return "CH" + juce::String (s + 3);
            if (s < 24)
                return "RETURN" + juce::String (s - 21);
            if (s < 28)
                return "AUX" + juce::String (s - 23);
            if (s < 32)
                return "BUS" + juce::String (s - 27);
            if (s < 34)
                return "EFFECT" + juce::String (s - 31);
            return "ST OUT";
        }

        const std::vector<RemoteTarget>& remoteTargets (bool on)
        {
            static const char* const sends[] = { "AUX1 SEND", "AUX2 SEND", "AUX3 SEND", "AUX4 SEND", "EFF1 SEND", "EFF2 SEND" };
            // CH 17-24 reach AUX 1-2 and the effects only; a return can't feed its own effect.
            const auto sendable = [] (int s, int n) { return ! (s >= 14 && s < 22 && (n == 2 || n == 3)) && ! (s >= 22 && n == 4 + (s - 22)); };
            static const std::vector<RemoteTarget> faders = [sendable]
            {
                std::vector<RemoteTarget> v { { "NO ASSIGN", "-", "-", rNone, 0, 0 } };
                for (int s = 0; s < 24; ++s)
                    v.push_back ({ "FADER", "CHANNEL", remoteStripName (s), rFader, s, 0 });
                for (int s = 24; s < 35; ++s)
                    v.push_back ({ "FADER", "MASTER", remoteStripName (s), rFader, s, 0 });
                for (int n = 0; n < 6; ++n)
                    for (int s = 0; s < 24; ++s)
                        if (sendable (s, n))
                            v.push_back ({ "FADER", sends[n], remoteStripName (s), rSend, s, n });
                for (int s = 0; s < 14; ++s)
                    v.push_back ({ "DELAY", "TIME", remoteStripName (s), rDelayTime, s, 0 });
                v.push_back ({ "DELAY", "TIME", "ST OUT", rDelayTime, 34, 0 });
                static const char* const bands[] = { "LOW", "L-MID", "H-MID", "HIGH" };
                static const char* const rows[] = { "F ", "G ", "Q " };
                for (int band = 0; band < 4; ++band)
                    for (int row = 0; row < 3; ++row)
                        for (int s = 0; s < 35; ++s)
                        {
                            const bool has = s < 28 || s == 34;                             // not BUS, not EFFECT
                            const bool twoBand = s >= 14 && s < 22 && (band == 1 || band == 2); // CH 17-24: LOW and HIGH
                            if (has && ! twoBand)
                                v.push_back ({ "EQ", juce::String (rows[row]) + bands[band], remoteStripName (s), rEq, s, band * 3 + row });
                        }
                for (int s = 0; s < 22; ++s)
                    v.push_back ({ "EQ", "ATT", remoteStripName (s), rAtt, s, 0 });
                for (int slot = 0; slot < 2; ++slot)
                    for (int n = 0; n < 19; ++n)
                        v.push_back ({ "EFFECT", "EFFECT" + juce::String (slot + 1), "PARAM" + juce::String (n + 1), rFx, slot, n });
                for (int s = 0; s < 24; ++s)
                    v.push_back ({ "PAN", "CHANNEL", remoteStripName (s), rPan, s, 0 });
                for (int s = 28; s < 32; ++s)
                    v.push_back ({ "PAN", "BUS TO ST", remoteStripName (s), rPan, s, 0 });
                return v;
            }();
            static const std::vector<RemoteTarget> keys = [sendable]
            {
                std::vector<RemoteTarget> v { { "NO ASSIGN", "-", "-", rNone, 0, 0 } };
                for (int s = 0; s < 24; ++s)
                    v.push_back ({ "ON", "CHANNEL", remoteStripName (s), rOn, s, 0 });
                for (int s = 24; s < 35; ++s)
                    v.push_back ({ "ON", "MASTER", remoteStripName (s), rOn, s, 0 });
                for (int s = 28; s < 32; ++s)
                    v.push_back ({ "BUS TO ST", "-", remoteStripName (s), rBusToSt, s, 0 });
                for (int s = 0; s < 14; ++s)
                    v.push_back ({ "PHASE", "CHANNEL", remoteStripName (s), rPhase, s, 0 });
                for (int n = 0; n < 6; ++n)
                    for (int s = 0; s < 24; ++s)
                        if (sendable (s, n))
                            v.push_back ({ "PRE/POST", sends[n], remoteStripName (s), rPrePost, s, n });
                for (int s = 0; s < 14; ++s)
                    v.push_back ({ "DELAY", "CHANNEL", remoteStripName (s), rDelayOn, s, 0 });
                v.push_back ({ "DELAY", "OUTPUT", "ST OUT", rDelayOn, 34, 0 });
                for (int s = 0; s < 24; ++s)
                    v.push_back ({ "EQ", "CHANNEL", remoteStripName (s), rEqOn, s, 0 });
                for (int s : { 24, 25, 26, 27, 34 })
                    v.push_back ({ "EQ", "OUTPUT", remoteStripName (s), rEqOn, s, 0 });
                for (int s = 0; s < 14; ++s)
                    v.push_back ({ "DYNAMICS", "CHANNEL", remoteStripName (s), rDynOn, s, 0 });
                for (int s : { 24, 25, 26, 27, 34 })
                    v.push_back ({ "DYNAMICS", "OUTPUT", remoteStripName (s), rDynOn, s, 0 });
                return v;
            }();
            return on ? keys : faders;
        }

        int findRemoteTarget (bool on, const juce::String& a, const juce::String& b, const juce::String& c)
        {
            const auto& list = remoteTargets (on);
            for (int i = 0; i < (int) list.size(); ++i)
                if (list[(size_t) i].a == a && list[(size_t) i].b == b && list[(size_t) i].c == c)
                    return i;
            return 0;
        }

        const juce::String& remoteColumnOf (const RemoteTarget& t, int column) { return column == 0 ? t.a : column == 1 ? t.b : t.c; }

        /** The targets a column offers: one for each of its names, among those sharing the columns before it. */
        std::vector<int> remoteChoices (bool on, int column, int current)
        {
            const auto& list = remoteTargets (on);
            const auto& now = list[(size_t) juce::jlimit (0, (int) list.size() - 1, current)];
            std::vector<int> out;
            juce::StringArray seen;
            for (int i = 0; i < (int) list.size(); ++i)
            {
                const auto& t = list[(size_t) i];
                if ((column >= 1 && t.a != now.a) || (column >= 2 && t.b != now.b))
                    continue;
                const auto& label = remoteColumnOf (t, column);
                if (! seen.contains (label))
                {
                    seen.add (label);
                    out.push_back (i);
                }
            }
            return out;
        }

        juce::String remoteControlName (int control)
        {
            return control < 12 ? juce::String (control + 1) : control == 12 ? juce::String ("13-14") : control == 13 ? juce::String ("15-16") : juce::String ("MAS");
        }
    } // namespace

    //==========================================================================
    void Display::header (const juce::String& s, int y)
    {
        const int w = textWidth (s, false) + 8, x = kLeftColumn + 2;
        text (x + 4, y, s);
        vLine (x, y, y + 7);
        vLine (x + w, y, y + 7);
        vLine (x + w + 1, y, y + 8);
        hLine (x + 1, x + w, y + 7);
        hLine (x + 2, x + w + 1, y + 8);
    }

    juce::String Display::delayText (float ms) const
    {
        switch (ctx.settings.getIntValue (kDelayUnitsKey, 0))
        {
            case 1:  return juce::String (ms * 0.34067f, 1); // 102.2 m for 300 ms (p.117)
            case 2:  return juce::String (juce::roundToInt ((double) ms * (ctx.engine.getSampleRate() > 0.0 ? ctx.engine.getSampleRate() : 48000.0) / 1000.0));
            default: break;
        }
        return juce::String (ms, 1);
    }

    juce::String Display::delayUnitLabel() const
    {
        const int units = ctx.settings.getIntValue (kDelayUnitsKey, 0);
        return units == 1 ? "[meter]" : units == 2 ? "[sample]" : "[ msec]";
    }

    //==========================================================================
    // The panel's own state.
    void Display::savePanel()
    {
        juce::StringArray v { "1" };
        for (int g : panel.faderGroup)
            v.add (juce::String (g));
        for (int g : panel.muteGroup)
            v.add (juce::String (g));
        for (bool on : panel.groupOn)
            v.add (on ? "1" : "0");
        for (bool on : panel.pair)
            v.add (on ? "1" : "0");
        v.add (panel.soloOn ? "1" : "0");
        v.add (panel.lastSolo ? "1" : "0");
        v.add (juce::String (panel.bank));
        for (const auto& bank : panel.remote)
            for (int t : bank)
                v.add (juce::String (t));
        ctx.settings.setValue (kPanelKey, v.joinIntoString (" "));
        seenConsole = ctx.engine.setConsoleText (groupsText());
    }

    // The fader groups, [ON] groups, group enables and pairs as the show and scenes keep them.
    juce::String Display::groupsText() const
    {
        juce::StringArray v { "y01vgroups1" };
        for (int g : panel.faderGroup)
            v.add (juce::String (g));
        for (int g : panel.muteGroup)
            v.add (juce::String (g));
        for (bool on : panel.groupOn)
            v.add (on ? "1" : "0");
        for (bool on : panel.pair)
            v.add (on ? "1" : "0");
        return v.joinIntoString (" ");
    }

    bool Display::adoptGroupsText (const juce::String& text)
    {
        juce::StringArray v;
        v.addTokens (text, " ", "");
        v.removeEmptyStrings();
        if (v.size() != 1 + 14 + 14 + 6 + 10 || v[0] != "y01vgroups1")
            return false;
        int at = 1;
        const auto next = [&v, &at] { return v[at++].getIntValue(); };
        for (auto& g : panel.faderGroup)
            g = juce::jlimit (0, 3, next());
        for (auto& g : panel.muteGroup)
            g = juce::jlimit (0, 3, next());
        for (auto& on : panel.groupOn)
            on = next() != 0;
        for (auto& on : panel.pair)
            on = next() != 0;
        return true;
    }

    // A show load or a scene recall changed the engine's text: the groups and pairs follow it.
    // Text a show never had means nothing grouped.
    void Display::adoptEngineGroups()
    {
        const auto version = ctx.engine.getConsoleVersion();
        if (version == seenConsole)
            return;
        const auto text = ctx.engine.getConsoleText();
        if (text.isEmpty())
        {
            panel.faderGroup.fill (0);
            panel.muteGroup.fill (0);
            panel.groupOn.fill (true);
            panel.pair.fill (false);
        }
        else if (! adoptGroupsText (text))
        {
            seenConsole = version;
            return;
        }
        savePanel();
        relink();
        refresh();
    }

    void Display::loadPanel()
    {
        panel = {};
        for (int bank = 0; bank < 4; ++bank)
            initRemoteBank (bank);
        juce::StringArray v;
        v.addTokens (ctx.settings.getValue (kPanelKey), " ", "");
        v.removeEmptyStrings();
        if (v.size() != 1 + 14 + 14 + 6 + 10 + 3 + 120 || v[0] != "1")
            return;
        int at = 1;
        const auto next = [&v, &at] { return v[at++].getIntValue(); };
        for (auto& g : panel.faderGroup)
            g = juce::jlimit (0, 3, next());
        for (auto& g : panel.muteGroup)
            g = juce::jlimit (0, 3, next());
        for (auto& on : panel.groupOn)
            on = next() != 0;
        for (auto& on : panel.pair)
            on = next() != 0;
        panel.soloOn = next() != 0;
        panel.lastSolo = next() != 0;
        panel.bank = juce::jlimit (0, 3, next());
        for (auto& bank : panel.remote)
            for (int i = 0; i < 30; ++i)
                bank[(size_t) i] = juce::jlimit (0, (int) remoteTargets (i >= 15).size() - 1, next());
    }

    int Display::getGroup (int fader, bool mute) const noexcept
    {
        if (! juce::isPositiveAndBelow (fader, 14))
            return 0;
        return mute ? panel.muteGroup[(size_t) fader] : panel.faderGroup[(size_t) fader];
    }

    void Display::setGroup (int fader, bool mute, int group)
    {
        if (! juce::isPositiveAndBelow (fader, 14))
            return;
        auto& groups = mute ? panel.muteGroup : panel.faderGroup;
        groups[(size_t) fader] = juce::jlimit (0, 3, group);
        // A pair goes in and out together (p.55).
        if (fader < 12 && panel.pair[(size_t) (fader / 2)])
            groups[(size_t) (fader ^ 1)] = groups[(size_t) fader];
        savePanel();
        refresh();
    }

    bool Display::isGroupOn (int group) const noexcept { return juce::isPositiveAndBelow (group, 6) && panel.groupOn[(size_t) group]; }

    void Display::setGroupOn (int group, bool on)
    {
        if (juce::isPositiveAndBelow (group, 6))
        {
            panel.groupOn[(size_t) group] = on;
            savePanel();
        }
    }

    bool Display::isPaired (int pair) const noexcept { return juce::isPositiveAndBelow (pair, 10) && panel.pair[(size_t) pair]; }

    namespace
    {
        /** The two strips of a pair: 0-5 CH 1/2 to 11/12, 6-7 AUX 1/2 and 3/4, 8-9 BUS 1/2 and 3/4. */
        std::pair<Strip*, Strip*> pairStrips (MixerEngine& engine, int pair)
        {
            const auto& channels = engine.getChannels();
            const auto& buses = engine.getBuses();
            const auto of = [] (const MixerEngine::StripList& list, int first) -> std::pair<Strip*, Strip*>
            {
                if (first + 1 < (int) list.size())
                    return { list[(size_t) first].get(), list[(size_t) first + 1].get() };
                return { nullptr, nullptr };
            };
            if (pair < 6)
                return of (channels, pair * 2);
            return of (buses, pair < 8 ? kAuxBus + (pair - 6) * 2 : (pair - 8) * 2);
        }
    } // namespace

    void Display::linkStrips (Strip& a, Strip& b, bool input)
    {
        const auto f = [this] (std::atomic<float>& x, std::atomic<float>& y)
        {
            y = x.load();
            twinFloats.push_back ({ &x, &y, x.load() });
        };
        const auto t = [this] (std::atomic<bool>& x, std::atomic<bool>& y)
        {
            y = x.load();
            twinBools.push_back ({ &x, &y, x.load() });
        };
        const auto n = [this] (std::atomic<int32_t>& x, std::atomic<int32_t>& y)
        {
            y = x.load();
            twinInts.push_back ({ &x, &y, x.load() });
        };
        auto& pa = a.params;
        auto& pb = b.params;
        // The dynamics' type first: choosing one loads its initial values.
        if (pa.dynamics().model.load() != pb.dynamics().model.load())
            b.setModel (ModelBlock::Dynamics, pa.dynamics().model.load());
        twinModels.push_back ({ &a, &b, pa.dynamics().model.load() });
        f (pa.faderDb, pb.faderDb);
        t (pa.mute, pb.mute);
        t (pa.eq().on, pb.eq().on);
        for (int i = 0; i < ld::kMaxEqParams; ++i)
            f (pa.eq().values[i], pb.eq().values[i]);
        t (pa.dynamics().on, pb.dynamics().on);
        for (int i = 0; i < ld::kMaxDynParams; ++i)
            f (pa.dynamics().values[i], pb.dynamics().values[i]);
        if (! input)
            return;
        f (pa.input.trimDb, pb.input.trimDb);
        f (pa.input.delay, pb.input.delay);
        t (pa.input.delayOn, pb.input.delayOn);
        for (int bus = kAuxBus; bus < kEffectBus + 2; ++bus)
        {
            f (a.sends[(size_t) bus].levelDb, b.sends[(size_t) bus].levelDb);
            n (a.sends[(size_t) bus].mode, b.sends[(size_t) bus].mode);
        }
    }

    void Display::relink()
    {
        twinFloats.clear();
        twinBools.clear();
        twinInts.clear();
        twinModels.clear();
        for (int pair = 0; pair < 10; ++pair)
            if (panel.pair[(size_t) pair])
                if (const auto [a, b] = pairStrips (ctx.engine, pair); a != nullptr && b != nullptr)
                    linkStrips (*a, *b, pair < 6);
        linksPrimed = false;
    }

    void Display::setPair (int pair, bool on, bool fromSecond)
    {
        if (! juce::isPositiveAndBelow (pair, 10))
            return;
        if (on && fromSecond)
            if (const auto [a, b] = pairStrips (ctx.engine, pair); a != nullptr && b != nullptr)
                linkStrips (*b, *a, pair < 6); // copies the second onto the first; relink() makes the pair
        panel.pair[(size_t) pair] = on;
        if (on && pair < 6)
        {
            // The second follows the first into its groups.
            panel.faderGroup[(size_t) pair * 2 + 1] = panel.faderGroup[(size_t) pair * 2];
            panel.muteGroup[(size_t) pair * 2 + 1] = panel.muteGroup[(size_t) pair * 2];
        }
        savePanel();
        relink();
        refresh();
    }

    void Display::confirmPair (int pair)
    {
        if (isPaired (pair))
        {
            setPair (pair, false);
            return;
        }
        const int first = pair < 6 ? pair * 2 + 1 : ((pair - 6) % 2) * 2 + 1;
        const juce::String what (pair < 6 ? "CH" : pair < 8 ? "AUX" : "BUS");
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withTitle (what + " PAIR")
                                          .withMessage ("Which settings does the pair take?")
                                          .withButton (what + " " + juce::String (first) + " -> " + juce::String (first + 1))
                                          .withButton (what + " " + juce::String (first + 1) + " -> " + juce::String (first))
                                          .withButton ("CANCEL")
                                          .withAssociatedComponent (this),
                                      [safe = juce::Component::SafePointer<Display> (this), pair] (int result)
                                      {
                                          if (safe != nullptr && (result == 1 || result == 2))
                                              safe->setPair (pair, true, result == 2);
                                      });
    }

    void Display::tickLinks()
    {
        adoptEngineGroups();
        const auto& channels = ctx.engine.getChannels();
        const int n = juce::jmin (14, (int) channels.size());
        const auto snapshot = [this, &channels, n]
        {
            for (int i = 0; i < n; ++i)
            {
                lastFader[(size_t) i] = channels[(size_t) i]->params.faderDb.load();
                lastMute[(size_t) i] = channels[(size_t) i]->params.mute.load();
            }
        };
        if (! linksPrimed)
        {
            snapshot();
            linksPrimed = true;
            return;
        }

        // A group follows the one fader or [ON] key that moved. Several at once is a scene
        // recall or the like: nothing follows that.
        const auto law = ctx.engine.getFaderLaw ((int) StripKind::Channel);
        for (int g = 1; g <= 3; ++g)
        {
            int moved = -1, count = 0;
            for (int i = 0; i < n; ++i)
                if (panel.faderGroup[(size_t) i] == g && ! juce::exactlyEqual (channels[(size_t) i]->params.faderDb.load(), lastFader[(size_t) i]))
                {
                    moved = i;
                    ++count;
                }
            if (count == 1 && panel.groupOn[(size_t) (g - 1)])
            {
                // By the same travel: Sidecar's reading (the manual says only that one fader works the group).
                const float delta = law.toPosition (channels[(size_t) moved]->params.faderDb.load()) - law.toPosition (lastFader[(size_t) moved]);
                for (int i = 0; i < n; ++i)
                    if (i != moved && panel.faderGroup[(size_t) i] == g)
                    {
                        auto& db = channels[(size_t) i]->params.faderDb;
                        db = law.toDb (juce::jlimit (0.0f, 1.0f, law.toPosition (db.load()) + delta));
                    }
            }

            moved = -1;
            count = 0;
            for (int i = 0; i < n; ++i)
                if (panel.muteGroup[(size_t) i] == g && channels[(size_t) i]->params.mute.load() != lastMute[(size_t) i])
                {
                    moved = i;
                    ++count;
                }
            if (count == 1 && panel.groupOn[(size_t) (g + 2)])
                for (int i = 0; i < n; ++i)
                    if (i != moved && panel.muteGroup[(size_t) i] == g)
                    {
                        // Those on go off, those off come on (p.56).
                        auto& mute = channels[(size_t) i]->params.mute;
                        mute = ! mute.load();
                    }
        }

        // The pairs: whichever side changed, the other takes it.
        for (auto& t : twinModels)
        {
            const int a = t.a->params.dynamics().model.load(), b = t.b->params.dynamics().model.load();
            if (a != t.last)
            {
                t.b->setModel (ModelBlock::Dynamics, a);
                t.last = a;
            }
            else if (b != t.last)
            {
                t.a->setModel (ModelBlock::Dynamics, b);
                t.last = b;
            }
        }
        for (auto& t : twinFloats)
        {
            const float a = t.a->load(), b = t.b->load();
            if (! juce::exactlyEqual (a, t.last))
            {
                *t.b = a;
                t.last = a;
            }
            else if (! juce::exactlyEqual (b, t.last))
            {
                *t.a = b;
                t.last = b;
            }
        }
        for (auto& t : twinBools)
        {
            const bool a = t.a->load(), b = t.b->load();
            if (a != t.last)
            {
                *t.b = a;
                t.last = a;
            }
            else if (b != t.last)
            {
                *t.a = b;
                t.last = b;
            }
        }
        for (auto& t : twinInts)
        {
            const int32_t a = t.a->load(), b = t.b->load();
            if (a != t.last)
            {
                *t.b = a;
                t.last = a;
            }
            else if (b != t.last)
            {
                *t.a = b;
                t.last = b;
            }
        }
        snapshot();
    }

    bool Display::pressSel (int fader)
    {
        if (function == Function::Setup && page == 2 && juce::isPositiveAndBelow (fader, 14) && juce::isPositiveAndBelow (focus, 6))
        {
            const bool mute = focus >= 3;
            const int group = focus % 3 + 1;
            setGroup (fader, mute, getGroup (fader, mute) == group ? 0 : group);
            return true;
        }
        if (isRemoteActive() && juce::isPositiveAndBelow (fader, 15))
        {
            remoteChannel = fader;
            refresh();
            return true;
        }
        return false;
    }

    //==========================================================================
    // SETUP page 1 (p.209): as the page stands on the internal clock. Sidecar's clock is the audio device's.
    void Display::drawWordClock()
    {
        header ("WORD CLOCK SELECT");
        const double rate = ctx.engine.getSampleRate() > 0.0 ? ctx.engine.getSampleRate() : 48000.0;
        const auto fs = juce::String (rate / 1000.0, std::fmod (rate, 1000.0) < 1.0 ? 0 : 1) + "k";
        box ({ 59, 21, 173, 46 });
        box ({ 125, 21, 43, 10 });
        textCentred ({ 125, 21, 43, 10 }, "SOURCE");
        box ({ 64, 34, 57, 29 });
        box ({ 70, 39, 45, 19 });
        textCentred ({ 70, 40, 45, 8 }, "AUTO");
        textCentred ({ 70, 48, 45, 8 }, "NAVIGATE", true);
        fill ({ 127, 40, 30, 18 });
        textCentred ({ 127, 40, 30, 9 }, "INT", false, false);
        textCentred ({ 127, 49, 30, 9 }, fs, textWidth (fs, false) > 28, false);
        const auto other = [this] (juce::Rectangle<int> r, const char* first, const char* second)
        {
            box (r);
            hLine (r.getX() + 1, r.getRight(), r.getBottom());
            vLine (r.getRight(), r.getY() + 1, r.getBottom());
            textCentred (r.withHeight (9), first, true);
            textCentred (r.withTrimmedTop (8), second, true);
        };
        other ({ 161, 39, 30, 18 }, "ST IN", "DIGITAL");
        other ({ 196, 39, 30, 18 }, "OPTION", "SLOT");
        box ({ 236, 21, 54, 46 });
        box ({ 252, 21, 22, 10 });
        textCentred ({ 252, 21, 22, 10 }, "Fs");
        box ({ 242, 38, 42, 20 });
        textCentred ({ 242, 38, 42, 20 }, fs);
    }

    // SETUP page 2 (pp.78, 80).
    void Display::buildMonitorSetup()
    {
        auto& e = ctx.engine;
        // One source at a time here: the engine's monitor has one (the 01V mixes those chosen).
        const auto source = [this] (const juce::String& name, StripKind kind, int number, juce::Rectangle<int> area)
        {
            const int index = MixerEngine::monitorSourceIndex (kind, number);
            KnobBinding b;
            b.label = "SOURCE";
            b.steps = 2.0f;
            b.get = [this, index] { return ctx.engine.speakers.source.load() == index ? 1.0f : 0.0f; };
            b.set = [this, index] (float)
            {
                ctx.engine.speakers.source = index;
                ctx.engine.phones.source.source = index; // the phones carry the monitor's signal (p.77)
            };
            b.text = [name] { return name; };
            add (Cell::Switch, area, std::move (b)).raw = true;
        };
        source ("STEREO OUT", StripKind::Main, 0, { 66, 23, 66, 10 });
        for (int i = 0; i < 4; ++i)
            source (juce::String (i + 1), StripKind::Bus, kAuxBus + i, { 155 + i * 15, 23, 12, 10 });
        for (int i = 0; i < 4; ++i)
            source (juce::String (i + 1), StripKind::Bus, i, { 155 + i * 15, 35, 12, 10 });

        auto trim = bindLinear ("MONI TRIM", e.speakers.sourceLevelDb, -60.0f, 6.0f, "dB", 0, 0.0f);
        trim.steps = 67.0f;
        trim.text = [&e] { return juce::String (juce::roundToInt (e.speakers.sourceLevelDb.load())) + "dB"; };
        auto& t = add (Cell::Text, { 259, 1, 31, 9 }, std::move (trim));
        t.dotted = t.raw = true;
        add (Cell::Switch, { 266, 23, 26, 10 }, bindBool ("MONO", e.monitorMono, "MONO", "ST")).raw = true;

        const auto choice = [this] (const juce::String& label, std::function<bool()> get, std::function<void (bool)> set, const char* on, const char* off, juce::Rectangle<int> area, bool plain)
        {
            KnobBinding b;
            b.label = label;
            b.steps = 2.0f;
            b.get = [get] { return get() ? 1.0f : 0.0f; };
            b.set = [set] (float v) { set (v >= 0.5f); };
            b.text = [get, on, off] { return juce::String (get() ? on : off); };
            auto& c = add (Cell::Switch, area, std::move (b));
            c.raw = true;
            c.plain = plain;
        };
        choice ("SOLO", [this] { return panel.soloOn; },
                [this] (bool v)
                {
                    panel.soloOn = v;
                    if (! v) // nothing is soloed while it's disabled
                        for (auto* list : { &ctx.engine.getChannels(), &ctx.engine.getAux() })
                            for (const auto& s : *list)
                                s->params.solo = false;
                    savePanel();
                },
                "ENABLE", "DISABLE", { 61, 69, 42, 10 }, false);
        // RECORDING SOLO is to the monitor alone; MIXDOWN SOLO mutes the others on the stereo bus.
        choice ("SOLO MODE", [&e] { return e.soloMode.load() == (int) ld::kSoloSip; },
                [&e] (bool v) { e.soloMode = (int32_t) (v ? ld::kSoloSip : ld::kSoloStudio); },
                "MIXDOWN SOLO", "RECORDING SOLO", { 112, 69, 72, 10 }, true);
        choice ("SEL MODE", [this] { return panel.lastSolo; },
                [this] (bool v)
                {
                    panel.lastSolo = v;
                    savePanel();
                },
                "LAST SOLO", "MIX SOLO", { 193, 69, 54, 10 }, true);
        choice ("LISTEN", [&e] { return e.soloAfl.load(); }, [&e] (bool v) { e.soloAfl = v; }, "AFL", "PFL", { 258, 69, 32, 10 }, true);
    }

    void Display::drawMonitorSetup()
    {
        header ("MONITOR SETUP");
        box ({ 62, 12, 44, 10 });
        textCentred ({ 62, 12, 44, 10 }, "SOURCE");
        text (137, 25, "AUX");
        text (137, 37, "BUS");
        text (190, 2, "MONI TRIM =");
        box ({ 217, 12, 40, 10 });
        textCentred ({ 217, 12, 40, 10 }, "LISTEN");
        box ({ 261, 12, 29, 10 });
        textCentred ({ 261, 12, 29, 10 }, "MONO");
        // ST CASCADE IN, the monitor's own LISTEN and SOLO TRIM: drawn as the page has them. The
        // engine has no stereo cascade input, and its monitor takes its source after the fader.
        const auto plain = [this] (juce::Rectangle<int> r, const juce::String& s)
        {
            box (r);
            hLine (r.getX() + 1, r.getRight(), r.getBottom());
            vLine (r.getRight(), r.getY() + 1, r.getBottom());
            textCentred (r, s, textWidth (s, false) > r.getWidth() - 1);
        };
        plain ({ 66, 35, 66, 10 }, "ST CASCADE IN");
        fill ({ 222, 23, 32, 10 });
        textCentred ({ 222, 23, 32, 10 }, "AFL", false, false);

        header ("SOLO SETUP", 48);
        text (190, 49, "SOLO TRIM =");
        dottedH (258, 290, 46);
        dottedH (258, 290, 56);
        dottedV (258, 46, 56);
        dottedV (290, 46, 56);
        text (289 - textWidth ("0dB", false), 48, "0dB");
        const auto label = [this] (juce::Rectangle<int> r, const char* s)
        {
            box (r);
            textCentred (r, s);
        };
        label ({ 59, 58, 30, 10 }, "SOLO");
        label ({ 110, 58, 60, 10 }, "SOLO MODE");
        label ({ 191, 58, 53, 10 }, "SEL MODE");
        label ({ 253, 58, 40, 10 }, "LISTEN");
    }

    // SETUP page 3 (pp.55-56): the cursor is on a group; a [SEL] key puts its fader in or takes it out.
    void Display::buildGroups()
    {
        for (int g = 0; g < 6; ++g)
        {
            KnobBinding b;
            b.label = "ENABLE";
            b.steps = 2.0f;
            b.get = [this, g] { return panel.groupOn[(size_t) g] ? 1.0f : 0.0f; };
            b.set = [this, g] (float v) { setGroupOn (g, v >= 0.5f); };
            b.text = [g] { return juce::String::charToString ((juce::juce_wchar) ('A' + g)); };
            add (Cell::Switch, { 270, kGroupRows[g], 16, 9 }, std::move (b)).raw = true;
        }
    }

    void Display::drawGroups()
    {
        header ("GROUP");
        for (int i = 0; i < 14; ++i)
        {
            if (i < 12)
            {
                const auto name = juce::String (i + 1);
                text (kGroupCx[i] - textWidth (name, false) / 2, 2, name, false);
                continue;
            }
            // A channel pair, its two numbers stacked so that neighbours don't run together.
            const auto a = juce::String (13 + 2 * (i - 12)), c = juce::String (14 + 2 * (i - 12));
            text (kGroupCx[i] - textWidth (a, true) / 2, 0, a, true);
            text (kGroupCx[i] - textWidth (c, true) / 2, 6, c, true);
        }
        text (267, 3, "ENABLE", true);
        box ({ 58, 19, 36, 10 });
        textCentred ({ 58, 19, 36, 10 }, "FADER");
        box ({ 62, 57, 29, 10 });
        textCentred ({ 62, 57, 29, 10 }, "MUTE");
        for (int x : { 98, 261 })
        {
            vLine (x, 10, 40);
            vLine (x, 47, 77);
        }
        for (int g = 0; g < 6; ++g)
        {
            const int cy = kGroupRows[g] + 4;
            text (101, cy - 3, juce::String::charToString ((juce::juce_wchar) ('A' + g)));
            for (int i = 0; i < 14; ++i)
            {
                const int cx = kGroupCx[i];
                if (getGroup (i, g >= 3) == g % 3 + 1)
                {
                    fill ({ cx - 2, cy - 3, 5, 7 });
                    vLine (cx - 3, cy - 2, cy + 2);
                    vLine (cx + 3, cy - 2, cy + 2);
                }
                else
                    for (int d = -2; d <= 2; d += 2)
                        dot (cx + d, cy + 2);
            }
        }
    }

    // SETUP page 4 (pp.52, 101, 110).
    void Display::buildPairs()
    {
        for (int pair = 0; pair < 10; ++pair)
        {
            const int column = pair < 6 ? pair : pair - 5;
            auto& c = add (Cell::Button, { 56 + column * 40, pair < 6 ? 28 : 65, 36, 10 }, {});
            c.custom = true;
            c.press = [this, pair] { confirmPair (pair); };
        }
    }

    void Display::drawPairs()
    {
        header ("PAIR");
        hLine (61, 158, 11);
        hLine (191, 286, 11);
        text (162, 8, "INPUT");
        hLine (97, 123, 48);
        hLine (145, 171, 48);
        text (126, 45, "AUX");
        hLine (177, 203, 48);
        hLine (225, 251, 48);
        text (206, 45, "BUS");
        // A heart between the two numbers: whole when they're a pair, broken when not.
        static const uint8_t heart[6] = { 0x36, 0x7F, 0x7F, 0x3E, 0x1C, 0x08 };
        for (int pair = 0; pair < 10; ++pair)
        {
            const int column = pair < 6 ? pair : pair - 5;
            const juce::Rectangle<int> r (56 + column * 40, pair < 6 ? 28 : 65, 36, 10);
            const int first = pair < 6 ? pair * 2 + 1 : ((pair - 6) % 2) * 2 + 1, y = r.getY() - 9;
            const bool paired = isPaired (pair);
            text (r.getX() + 4, y, juce::String (first));
            text (r.getRight() - 3 - textWidth (juce::String (first + 1), false), y, juce::String (first + 1));
            for (int row = 0; row < 6; ++row)
                for (int col = 0; col < 7; ++col)
                    if ((heart[row] >> (6 - col)) & 1)
                    {
                        if (paired)
                            dot (r.getX() + 15 + col, y + row);
                        else if (col != 3) // two halves, a gap between
                            dot (r.getX() + 15 + col + (col < 3 ? -1 : 1), y + row + (col < 3 ? 0 : 1));
                    }
            if (column > (pair < 6 ? 0 : 1))
                dottedV (r.getX() - 2, y, r.getBottom());
            if (paired)
            {
                fill (r);
                textCentred (r, "STEREO", true, false);
            }
            else
            {
                box (r);
                hLine (r.getX() + 1, r.getRight(), r.getBottom());
                vLine (r.getRight(), r.getY() + 1, r.getBottom());
                textCentred (r, "MONOx2", true);
            }
        }
    }

    // SETUP page 5 (p.212): drawn as the page stands. Sidecar's outputs aren't dithered.
    void Display::drawDither()
    {
        header ("DITHER");
        text (108, 17, "ON/OFF");
        text (162, 17, "WORD LENGTH (BIT)");
        for (int y : { 24, 48, 72 })
        {
            hLine (62, 105, y);
            hLine (109, 137, y);
            hLine (141, 288, y);
        }
        text (64, 30, "DIGITAL");
        text (67, 38, "ST OUT");
        text (67, 54, "OPTION");
        text (76, 62, "OUT");
        const auto key = [this] (juce::Rectangle<int> r, const juce::String& s, bool on)
        {
            if (on)
                fill (r);
            else
            {
                box (r);
                hLine (r.getX() + 1, r.getRight(), r.getBottom());
                vLine (r.getRight(), r.getY() + 1, r.getBottom());
            }
            textCentred (r, s, s.length() == 2, ! on);
        };
        for (int row = 0; row < 2; ++row)
        {
            key ({ 114, 30 + row * 24, 20, 12 }, "OFF", false);
            for (int i = 0; i < 9; ++i)
                key ({ 144 + i * 16, 30 + row * 24, 12, 12 }, juce::String (16 + i), i == 4);
        }
    }

    //==========================================================================
    // PAN/ROUT page 3 (p.109): the bus masters, each with its switch and pan to the stereo bus.
    void Display::buildBusMaster()
    {
        const auto& buses = ctx.engine.getBuses();
        for (int i = 0; i < juce::jmin (4, (int) buses.size()); ++i)
        {
            auto& s = *buses[(size_t) i];
            const int x0 = 58 + i * 58;
            add (Cell::Bar, { x0 + 9, 23, 13, 41 }, bindFader ("BUS", s.params.faderDb));
            add (Cell::Switch, { x0 + 7, 69, 16, 9 }, onBinding (s.params));
            add (Cell::Switch, { x0 + 35, 29, 14, 9 }, bindBool ("TO ST", s.mains[0].on, "ST", "ST"));
            add (Cell::Dial, { x0 + 29, 52, 26, 24 }, bindPan01V ("PAN", s.params.pan));
        }
    }

    void Display::drawBusMaster()
    {
        header ("BUS MASTER");
        const auto& buses = ctx.engine.getBuses();
        for (int i = 0; i < juce::jmin (4, (int) buses.size()); ++i)
        {
            const int x0 = 58 + i * 58;
            textCentred ({ x0, 10, 58, 9 }, "BUS" + juce::String (i + 1));
            if (i > 0)
                vLine (x0, 11, 77);
            dottedV (x0 + 28, 22, 77);
            text (x0 + 34, 21, "TO ST", true);
            text (x0 + 37, 45, "PAN", true);
            const auto level = lcdLevel (buses[(size_t) i]->params.faderDb.load());
            text (x0 + 15 - textWidth (level, true) / 2, 60, level, true);
        }
    }

    // PAN/ROUT page 4 (pp.116, 213): the OMNI OUTs' sources; CH 13/14 from its jacks or the coaxial
    // DIGITAL IN. The STEREO CASCADE switch is drawn open: the engine has no input to the stereo bus.
    int Display::outputChoice (int port) const
    {
        const int source = ctx.engine.getOutputSource (port);
        if (source >= 0)
            for (int c = 0; c < kNumOutputChoices; ++c)
                if (outputChoiceSource (c) == source)
                    return c;
        return -1;
    }

    void Display::setOutputChoice (int port, int choice)
    {
        ctx.engine.clearOutput (port);
        // Onto the patch itself: a bus may feed several outputs (AUX 1 on OMNI OUT 1 and the card's 5).
        ctx.engine.setOutputSource (port, outputChoiceSource (juce::jlimit (0, kNumOutputChoices - 1, choice)));
    }

    Strip* Display::outputStrip (int port, int& side) const
    {
        const int choice = outputChoice (port);
        const auto& buses = ctx.engine.getBuses();
        const auto& channels = ctx.engine.getChannels();
        side = 0;
        if (choice < 0)
            return nullptr;
        if (choice < 8)
        {
            const int slot = choice < 4 ? kAuxBus + choice : choice - 4;
            return slot < (int) buses.size() ? buses[(size_t) slot].get() : nullptr;
        }
        if (choice < 10)
        {
            side = choice - 8;
            return ctx.engine.getMains().empty() ? nullptr : &ctx.engine.getMain (0);
        }
        const int ch = choice - 10, strip = ch < 12 ? ch : 12 + (ch - 12) / 2;
        side = ch < 12 ? 0 : (ch - 12) % 2;
        return strip < (int) channels.size() ? channels[(size_t) strip].get() : nullptr;
    }

    void Display::buildOmni()
    {
        for (int i = 0; i < 4; ++i)
        {
            const int port = kOmniPort + i;
            KnobBinding b;
            b.label = "OMNI" + juce::String (i + 1);
            b.steps = (float) kNumOutputChoices;
            b.get = [this, port] { return (float) juce::jmax (0, outputChoice (port)) / (float) (kNumOutputChoices - 1); };
            b.set = [this, port] (float v) { setOutputChoice (port, juce::roundToInt (v * (float) (kNumOutputChoices - 1))); };
            b.text = [this, port] { return outputChoiceName (outputChoice (port)); };
            auto& c = add (Cell::Text, { 59 + i * 60, 23, 51, 9 }, std::move (b));
            c.dotted = c.raw = c.centred = true;
        }
        const auto& channels = ctx.engine.getChannels();
        if (12 < (int) channels.size())
        {
            auto& p = channels[12]->params;
            KnobBinding flip;
            flip.label = "FLIP";
            flip.steps = 2.0f;
            flip.get = [&p] { return p.inputLeft.load() == kDigitalInput ? 1.0f : 0.0f; };
            flip.set = [&p] (float v)
            {
                const int first = v >= 0.5f ? kDigitalInput : 12;
                p.inputLeft = first;
                p.inputRight = first + 1;
            };
            flip.text = [&p] { return juce::String (p.inputLeft.load() == kDigitalInput ? "FLIP" : "NORMAL"); };
            add (Cell::Switch, { 164, 48, 31, 29 }, std::move (flip)).custom = true;
        }
    }

    void Display::drawOmni()
    {
        header ("OMNI OUT SELECT");
        for (int i = 0; i < 4; ++i)
        {
            textCentred ({ 59 + i * 60, 11, 51, 9 }, "OMNI" + juce::String (i + 1));
            if (i > 0)
                dottedV (54 + i * 60, 11, 33);
        }
        // The second heading, in the small type: the page's own is narrower than this font's.
        text (61, 39, "CH13-14 FLIP", true);
        vLine (57, 38, 45);
        vLine (112, 38, 45);
        vLine (113, 38, 46);
        hLine (58, 112, 45);
        hLine (59, 113, 46);
        const auto arrow = [this] (int x, int y)
        {
            for (int k = 0; k < 4; ++k)
                vLine (x + k, y - 3 + k, y + 3 - k);
        };
        const auto& channels = ctx.engine.getChannels();
        const bool flipped = 12 < (int) channels.size() && channels[12]->params.inputLeft.load() == kDigitalInput;
        // The two inputs.
        box ({ 116, 38, 45, 20 });
        textCentred ({ 116, 39, 45, 9 }, "CH13-14");
        textCentred ({ 116, 48, 45, 9 }, "ANALOG");
        dottedH (62, 108, 60);
        dottedH (62, 108, 77);
        dottedV (62, 60, 77);
        dottedV (108, 60, 77);
        textCentred ({ 62, 61, 47, 9 }, "COAXIAL");
        hLine (139, 139, 58);
        vLine (139, 58, 61);
        hLine (139, 163, 61);
        hLine (109, 163, 72);
        // The switch: straight through, or crossed.
        text (172, 39, "FLIP");
        box ({ 164, 48, 31, 29 });
        for (int y : { 61, 72 })
        {
            box ({ 168, y - 1, 3, 3 });
            box ({ 188, y - 1, 3, 3 });
        }
        for (int x = 171; x <= 187; ++x)
        {
            const int across = 61 + (x - 171) * 11 / 16;
            dot (x, flipped ? across : 61);
            dot (x, flipped ? 61 + 72 - across : 72);
        }
        // To CH 13/14, and to the stereo bus.
        fill ({ 204, 49, 44, 9 });
        textCentred ({ 204, 49, 44, 9 }, "CH13-14", false, false);
        hLine (195, 284, 61);
        arrow (285, 61);
        fill ({ 204, 63, 44, 7 });
        textCentred ({ 204, 63, 44, 7 }, "ST CASCADE", true, false);
        hLine (195, 249, 72);
        text (252, 49, "ON/OFF", true);
        box ({ 250, 63, 28, 15 });
        box ({ 254, 71, 3, 3 });
        box ({ 271, 71, 3, 3 });
        for (int k = 0; k < 12; ++k)
            dot (257 + k, 71 - k / 2);
        hLine (278, 284, 72);
        arrow (285, 72);
    }

    // DELAY page 4 (pp.91, 117). The STEREO OUT's delay is one for both sides here; the OMNI OUTs'
    // aren't in the engine and are drawn off.
    void Display::buildOutputDelay()
    {
        if (ctx.engine.getMains().empty())
            return;
        auto& in = ctx.engine.getMain (0).params.input;
        for (int side = 0; side < 2; ++side)
        {
            add (Cell::Switch, { 146 + side * 42, 15, 20, 9 }, bindBool ("DELAY", in.delayOn));
            add (Cell::Text, { 130 + side * 42, 28, 37, 9 }, delayBinding (in, this, 300.0f));
        }
    }

    void Display::drawOutputDelay()
    {
        header ("OUTPUT DELAY");
        arrowBox (56, 11, false);
        text (68, 16, "STEREO OUT");
        text (134, 16, "L");
        text (176, 16, "R");
        text (68, 48, "OMNI OUT");
        for (int row = 0; row < 2; ++row)
        {
            text (94, 30 + row * 32, "DELAY");
            text (96, 39 + row * 32, delayUnitLabel(), true);
        }
        for (int x : { 169, 211 })
            dottedV (x, 15, 37);
        for (int x : { 169, 211, 253 })
            dottedV (x, 47, 69);
        for (int i = 0; i < 4; ++i)
        {
            const juce::Rectangle<int> r (146 + i * 42, 47, 20, 9);
            text (r.getX() - 12, 48, juce::String (i + 1));
            box (r);
            hLine (r.getX() + 1, r.getRight(), r.getBottom());
            vLine (r.getRight(), r.getY() + 1, r.getBottom());
            textCentred (r, "OFF", true);
            const auto zero = delayText (0.0f);
            text (r.getRight() - textWidth (zero, false), 61, zero);
        }
    }

    //==========================================================================
    // OPTION pages 1 and 3 (p.85): the card's eight inputs (CH 17-24, before their faders) and what
    // feeds its eight outputs. PEAK HOLD is drawn off: these meters don't hold.
    void Display::drawOptionMeter (bool outputs)
    {
        header (outputs ? "OPTION OUT METER" : "OPTION IN METER");
        text (209, 2, "PEAK HOLD");
        box ({ 267, 1, 23, 10 });
        textCentred ({ 267, 1, 23, 10 }, "OFF");
        // The card's name: Sidecar has none fitted, its eight ins and outs are the computer's.
        box ({ 59, 14, 46, 24 });
        textCentred ({ 59, 16, 46, 9 }, "OPTION");
        textCentred ({ 59, 26, 46, 9 }, "I/O");
        static const std::pair<const char*, float> marks[] = { { "CLIP", 0.0f }, { "- 6", -6.0f }, { "-12", -12.0f }, { "-18", -18.0f }, { "-30", -30.0f }, { "-48", -48.0f } };
        static const int ys[] = { 19, 27, 35, 43, 51, 61 };
        for (int k = 0; k < 6; ++k)
        {
            text (125 - textWidth (marks[k].first, false), ys[k] - 3, marks[k].first);
            dottedH (128, 288, ys[k]);
        }
        const auto& channels = ctx.engine.getChannels();
        for (int i = 0; i < 8; ++i)
        {
            const int cx = 137 + i * 21;
            int side = 0;
            Strip* s = outputs ? outputStrip (kOptionPort + i, side)
                               : (kOptionChannel + i < (int) channels.size() ? channels[(size_t) (kOptionChannel + i)].get() : nullptr);
            const float level = s == nullptr ? -100.0f : juce::Decibels::gainToDecibels (ctx.meters.get (s, outputs ? MeterPoint::Output : MeterPoint::PreFader, side), -100.0f);
            for (int k = 0; k < 6; ++k)
                if (level >= marks[k].second)
                {
                    fill ({ cx - 4, ys[k], 9, 62 - ys[k] });
                    break;
                }
            const auto name = outputs ? outputChoiceName (outputChoice (kOptionPort + i)) : lcdLevel (level < -60.0f ? -100.0f : level);
            text (cx - textWidth (name, true) / 2, 65, name, true);
            const auto number = juce::String (outputs ? i + 1 : 17 + i);
            text (cx - textWidth (number, false) / 2, 72, number);
        }
    }

    // OPTION page 4 (p.219): each of the card's outputs takes one of five.
    void Display::buildOptionOut()
    {
        for (int i = 0; i < 8; ++i)
        {
            const int choices[] = { 4 + i % 4, 10 + i, 18 + i, i % 4, 8 + i % 2 };
            const juce::String names[] = { "BUS" + juce::String (i % 4 + 1), "CH" + juce::String (i + 1).paddedLeft (' ', 2), "CH" + juce::String (i + 9).paddedLeft (' ', 2),
                                           "AUX" + juce::String (i % 4 + 1), i % 2 == 0 ? "ST L" : "ST R" };
            for (int row = 0; row < 5; ++row)
            {
                const int port = kOptionPort + i, choice = choices[row];
                KnobBinding b;
                b.label = "OUT " + juce::String (i + 1);
                b.steps = 2.0f;
                b.get = [this, port, choice] { return outputChoice (port) == choice ? 1.0f : 0.0f; };
                b.set = [this, port, choice] (float) { setOutputChoice (port, choice); };
                b.text = [name = names[row]] { return name; };
                auto& c = add (Cell::Switch, { 61 + i * 29, 21 + row * 12, 23, 9 }, std::move (b));
                c.raw = c.tiny = true;
            }
        }
    }

    void Display::drawOptionOut()
    {
        header ("OPTION OUT SELECT");
        for (int i = 0; i < 8; ++i)
        {
            const int x0 = 61 + i * 29;
            textCentred ({ x0, 10, 23, 8 }, juce::String (i + 1));
            hLine (x0 - 1, x0 + 24, 18);
            if (i > 0)
                vLine (x0 - 3, 11, 77);
        }
    }

    // OPTION page 5 (p.51): CH 1-8 and CH 17-24 change inputs, one by one or all together.
    bool Display::isSwapped (int channel) const
    {
        const auto& channels = ctx.engine.getChannels();
        return juce::isPositiveAndBelow (channel, 8) && kOptionChannel + channel < (int) channels.size()
            && channels[(size_t) channel]->params.inputLeft.load() == kOptionInput + channel;
    }

    void Display::swapInputs (int channel)
    {
        const auto& channels = ctx.engine.getChannels();
        if (! juce::isPositiveAndBelow (channel, 8) || kOptionChannel + channel >= (int) channels.size())
            return;
        const bool swap = ! isSwapped (channel);
        channels[(size_t) channel]->params.inputLeft = swap ? kOptionInput + channel : channel;
        channels[(size_t) (kOptionChannel + channel)]->params.inputLeft = swap ? channel : kOptionInput + channel;
    }

    void Display::buildSwap()
    {
        const auto swapSwitch = [this] (std::function<bool()> get, std::function<void (bool)> set, juce::Rectangle<int> area)
        {
            KnobBinding b;
            b.label = "SWAP";
            b.steps = 2.0f;
            b.get = [get] { return get() ? 1.0f : 0.0f; };
            b.set = [set] (float v) { set (v >= 0.5f); };
            b.text = [] { return juce::String ("SWAP"); };
            add (Cell::Switch, area, std::move (b)).raw = true;
        };
        const auto all = [this]
        {
            for (int i = 0; i < 8; ++i)
                if (! isSwapped (i))
                    return false;
            return true;
        };
        swapSwitch (all, [this] (bool v)
                    {
                        for (int i = 0; i < 8; ++i)
                            if (isSwapped (i) != v)
                                swapInputs (i);
                    },
                    { 120, 24, 29, 10 });
        for (int i = 0; i < 8; ++i)
            swapSwitch ([this, i] { return isSwapped (i); },
                        [this, i] (bool v)
                        {
                            if (isSwapped (i) != v)
                                swapInputs (i);
                        },
                        { 162 + (i % 4) * 32, 12 + (i / 4) * 22, 27, 10 });
    }

    void Display::drawSwap()
    {
        header ("INPUT SWAP");
        box ({ 59, 14, 46, 24 });
        textCentred ({ 59, 16, 46, 9 }, "OPTION");
        textCentred ({ 59, 26, 46, 9 }, "I/O");
        text (118, 14, "GLOBAL");
        for (int i = 0; i < 8; ++i)
            textCentred ({ 162 + (i % 4) * 32, 4 + (i / 4) * 22, 27, 8 }, juce::String (17 + i));
        // The signal's way, as the page pictures it: the swap is before the EQ.
        const auto block = [this] (juce::Rectangle<int> r, const char* s)
        {
            fill (r, false);
            box (r);
            textCentred (r, s, true);
        };
        text (60, 53, "INPUT");
        text (56, 67, "OPTION");
        hLine (96, 284, 56);
        hLine (96, 284, 70);
        text (140, 44, "SWAP", true);
        block ({ 104, 52, 13, 9 }, "AD");
        block ({ 134, 50, 31, 26 }, "");
        hLine (139, 159, 56);
        hLine (139, 159, 70);
        block ({ 171, 52, 15, 9 }, "EQ");
        block ({ 188, 52, 21, 9 }, "DYN.");
        block ({ 211, 52, 27, 9 }, "DELAY");
        block ({ 171, 66, 15, 9 }, "EQ");
        text (246, 46, "ON FADER", true);
        for (int y : { 56, 70 })
        {
            block ({ 266, y - 5, 7, 10 }, "");
            for (int k = 0; k < 4; ++k)
                vLine (285 + k, y - 3 + k, y + 3 - k);
        }
    }

    //==========================================================================
    // REMOTE page 1 (pp.194-200): what the 14 faders, the master and their [ON] keys work while the
    // page shows, in four banks. Sidecar's reading where the manual gives only the lists: a fader
    // works the whole of its parameter's range. The dynamics' parameters, the delays' MIX and
    // FB GAIN, the aux pans and BALANCE aren't offered; MMC (page 2) and USER DEFINE (page 3) aren't built.
    void Display::initRemoteBank (int bank)
    {
        auto& b = panel.remote[(size_t) juce::jlimit (0, 3, bank)];
        b.fill (0);
        static const char* const masters[] = { "BUS1", "BUS2", "BUS3", "BUS4", "AUX1", "AUX2", "AUX3", "AUX4", "EFFECT1", "EFFECT2" };
        for (int i = 0; i < 15; ++i)
            switch (bank)
            {
                case 0:
                    if (i < 10 || i == 14)
                    {
                        const juce::String name (i == 14 ? "ST OUT" : masters[i]);
                        b[(size_t) i] = findRemoteTarget (false, "FADER", "MASTER", name);
                        b[(size_t) (15 + i)] = findRemoteTarget (true, "ON", "MASTER", name);
                    }
                    break;
                case 1:
                    if (i < 14)
                    {
                        b[(size_t) i] = findRemoteTarget (false, "DELAY", "TIME", remoteStripName (i));
                        b[(size_t) (15 + i)] = findRemoteTarget (true, "DELAY", "CHANNEL", remoteStripName (i));
                    }
                    break;
                default:
                    b[(size_t) i] = findRemoteTarget (false, "EFFECT", "EFFECT" + juce::String (bank - 1), "PARAM" + juce::String (i + 1));
                    break;
            }
    }

    Strip* Display::remoteStrip (int code) const
    {
        const auto& channels = ctx.engine.getChannels();
        const auto& aux = ctx.engine.getAux();
        const auto& buses = ctx.engine.getBuses();
        const auto at = [] (const MixerEngine::StripList& list, int i) { return juce::isPositiveAndBelow (i, (int) list.size()) ? list[(size_t) i].get() : nullptr; };
        if (code < 22)
            return at (channels, code);
        if (code < 24)
            return at (aux, code - 22);
        if (code < 28)
            return at (buses, kAuxBus + code - 24);
        if (code < 32)
            return at (buses, code - 28);
        if (code < 34)
            return at (buses, kEffectBus + code - 32);
        return ctx.engine.getMains().empty() ? nullptr : &ctx.engine.getMain (0);
    }

    KnobBinding Display::remoteBinding (bool on, int target)
    {
        const auto& list = remoteTargets (on);
        if (! juce::isPositiveAndBelow (target, (int) list.size()))
            return {};
        const auto& t = list[(size_t) target];
        if (t.what == rNone)
            return {};
        if (t.what == rFx)
        {
            auto& fx = ctx.engine.getFx();
            const int model = fx.getModel (t.strip);
            if (model <= ld::kFxExt)
                return {};
            const auto& spec = models::model (models::Fx, model);
            return t.n < (int) spec.params.size() ? bindModelParam (fx.params (t.strip).values[t.n], spec.params[(size_t) t.n]) : KnobBinding();
        }
        auto* s = remoteStrip (t.strip);
        if (s == nullptr)
            return {};
        auto& p = s->params;
        const int bus = t.n < 4 ? kAuxBus + t.n : kEffectBus + t.n - 4;
        switch (t.what)
        {
            case rFader:     return bindFader ("FADER", p.faderDb);
            case rSend:      return bindFader ("SEND", s->sends[(size_t) bus].levelDb);
            case rDelayTime: return delayBinding (p.input, this, t.strip == 34 ? 300.0f : 250.0f);
            case rEq:
            {
                static const char* const prefixes[] = { "l", "lm", "hm", "h" };
                static const char* const rows[] = { "f", "g", "q" };
                return bindParam (s->isOutput() ? models::BusEq : models::ChannelEq, p.eq().model.load(), p.eq().values, juce::String (prefixes[t.n / 3]) + rows[t.n % 3], t.b);
            }
            case rAtt:
            {
                auto att = bindLinear ("ATT", p.input.trimDb, -96.0f, 0.0f, "dB", 0, 0.0f);
                att.steps = 97.0f;
                return att;
            }
            case rPan:     return bindPan01V ("PAN", p.pan);
            case rOn:      return onBinding (p);
            case rBusToSt: return bindBool ("TO ST", s->mains[0].on);
            case rPhase:   return bindBool ("PHASE", p.input.invert, "REV", "NOR");
            case rPrePost: return prePostBinding (s->sends[(size_t) bus]);
            case rDelayOn: return bindBool ("DELAY", p.input.delayOn);
            case rEqOn:    return bindBool ("EQ", p.eq().on);
            case rDynOn:   return bindBool ("DYNAMICS", p.dynamics().on);
            default:       break;
        }
        return {};
    }

    KnobBinding Display::remoteFader (int control)
    {
        return juce::isPositiveAndBelow (control, 15) ? remoteBinding (false, panel.remote[(size_t) panel.bank][(size_t) control]) : KnobBinding();
    }

    KnobBinding Display::remoteOn (int control)
    {
        return juce::isPositiveAndBelow (control, 15) ? remoteBinding (true, panel.remote[(size_t) panel.bank][(size_t) (15 + control)]) : KnobBinding();
    }

    juce::String Display::remoteLabel (bool on, int control, int column) const
    {
        const auto& list = remoteTargets (on);
        const int target = panel.remote[(size_t) panel.bank][(size_t) (juce::jlimit (0, 14, control) + (on ? 15 : 0))];
        return remoteColumnOf (list[(size_t) juce::jlimit (0, (int) list.size() - 1, target)], column);
    }

    void Display::setRemoteBank (int bank)
    {
        panel.bank = juce::jlimit (0, 3, bank);
        savePanel();
        if (onRemoteChanged)
            onRemoteChanged();
    }

    KnobBinding Display::remoteColumn (bool on, int column)
    {
        const auto slot = [this, on] () -> int& { return panel.remote[(size_t) panel.bank][(size_t) (remoteChannel + (on ? 15 : 0))]; };
        KnobBinding b;
        b.label = column == 0 ? "PARAMETER" : column == 1 ? "GROUP" : "CHANNEL";
        b.steps = 256.0f;
        b.get = [this, on, column, slot]
        {
            const auto choices = remoteChoices (on, column, slot());
            const auto label = remoteLabel (on, remoteChannel, column);
            int at = 0;
            for (int i = 0; i < (int) choices.size(); ++i)
                if (remoteColumnOf (remoteTargets (on)[(size_t) choices[(size_t) i]], column) == label)
                    at = i;
            return (float) at / 255.0f;
        };
        b.set = [this, on, column, slot] (float v)
        {
            const auto choices = remoteChoices (on, column, slot());
            if (choices.empty())
                return;
            slot() = choices[(size_t) juce::jlimit (0, (int) choices.size() - 1, juce::roundToInt (v * 255.0f))];
            savePanel();
            if (onRemoteChanged)
                onRemoteChanged();
        };
        b.text = [this, on, column] { return remoteLabel (on, remoteChannel, column); };
        return b;
    }

    void Display::buildRemote()
    {
        if (page != 0)
            return;
        KnobBinding channel;
        channel.label = "CONTROL";
        channel.steps = 15.0f;
        channel.get = [this] { return (float) remoteChannel / 14.0f; };
        channel.set = [this] (float v) { remoteChannel = juce::jlimit (0, 14, juce::roundToInt (v * 14.0f)); };
        channel.text = [this] { return remoteControlName (remoteChannel); };
        auto& c = add (Cell::Text, { 64, 21, 36, 9 }, std::move (channel));
        c.dotted = c.raw = c.centred = true;

        auto& init = add (Cell::Button, { 142, 20, 62, 11 }, {}, "INITIALIZE");
        init.press = [this]
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withTitle ("CONFIRMATION")
                                              .withMessage ("Will Initialize Bank " + juce::String (panel.bank + 1) + ".\nARE YOU SURE?")
                                              .withButton ("OK")
                                              .withButton ("CANCEL")
                                              .withAssociatedComponent (this),
                                          [safe = juce::Component::SafePointer<Display> (this)] (int result)
                                          {
                                              if (safe == nullptr || result != 1)
                                                  return;
                                              safe->initRemoteBank (safe->panel.bank);
                                              safe->savePanel();
                                              safe->rebuild();
                                          });
        };
        for (int bank = 0; bank < 4; ++bank)
        {
            KnobBinding b;
            b.label = "BANK";
            b.steps = 2.0f;
            b.get = [this, bank] { return panel.bank == bank ? 1.0f : 0.0f; };
            b.set = [this, bank] (float) { setRemoteBank (bank); };
            b.text = [bank] { return juce::String (bank + 1); };
            add (Cell::Switch, { 219 + bank * 18, 21, 12, 11 }, std::move (b)).raw = true;
        }
        const int x[] = { 101, 162, 224 }, w[] = { 58, 59, 58 };
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 3; ++column)
            {
                auto& field = add (Cell::Text, { x[column], row == 0 ? 42 : 62, w[column], 9 }, remoteColumn (row == 0, column));
                field.dotted = field.raw = field.centred = true;
            }
    }

    void Display::drawRemote()
    {
        if (page != 0)
        {
            header (page == 1 ? "MMC CONTROL" : "USER DEFINE");
            textCentred ({ kLeftColumn + 1, 34, kRightColumn - kLeftColumn - 1, 12 }, "This page isn't built yet.");
            return;
        }
        header ("INTERNAL PARAMETER");
        box ({ 207, 6, 84, 31 });
        fill ({ 236, 6, 29, 9 }, false);
        box ({ 236, 6, 29, 9 });
        textCentred ({ 236, 6, 29, 9 }, "BANK");
        // The [ON] key and the fader, each beside what it works.
        text (78, 33, "ON", true);
        box ({ 75, 40, 12, 10 });
        hLine (78, 83, 47);
        fill ({ 80, 43, 2, 2 });
        text (92, 43, "=");
        text (71, 54, "FADER", true);
        vLine (80, 60, 78);
        vLine (82, 60, 78);
        fill ({ 78, 62, 7, 9 });
        for (int y : { 64, 66, 68 })
            hLine (79, 83, y, false);
        text (92, 63, "=");
    }

    //==========================================================================
    // The libraries (pp.67-70, 130-133, 173-176): EQ and DYNAMICS page 2, EFFECT page 2. The effects'
    // presets are the 01V's types; the EQ's and the dynamics' 40 presets aren't here yet, so those
    // lists start at the first user program, 41. A user program is the block's model and its values.
    int Display::libraryKind() const noexcept
    {
        if (function == Function::EqAtt && page == 1)
            return 0;
        if (function == Function::Dynamics && page == 1)
            return 1;
        if (function == Function::Send && effectSlot() >= 0 && page == 1)
            return 2;
        return -1;
    }

    int Display::libraryPresets (int kind) const { return kind == 2 ? libraryModels().size() : 0; }
    int Display::libraryLast (int kind) const { return kind == 2 ? 99 : 80; }

    namespace
    {
        /** A library's first number: the effects' presets start at 1, the others' user programs at 41. */
        int libraryFirst (int kind) { return kind == 2 ? 1 : 41; }
    } // namespace

    int Display::getLibraryNumber() const { return libraryFirst (libraryKind()) + juce::jmax (0, library); }

    const Display::Program* Display::userProgram (int kind, int number) const
    {
        if (! juce::isPositiveAndBelow (kind, 3))
            return nullptr;
        const auto it = programs[(size_t) kind].find (number);
        return it != programs[(size_t) kind].end() ? &it->second : nullptr;
    }

    juce::String Display::getLibraryTitle (int kind, int number) const
    {
        if (kind == 2 && number >= 1 && number <= libraryPresets (2))
            return models::model (models::Fx, libraryModels()[number - 1]).name;
        const auto* p = userProgram (kind, number);
        return p != nullptr ? p->title : juce::String();
    }

    void Display::saveLibrary()
    {
        juce::StringArray lines;
        for (int kind = 0; kind < 3; ++kind)
            for (const auto& [number, p] : programs[(size_t) kind])
            {
                juce::StringArray values;
                for (float v : p.values)
                    values.add (juce::String (v, 4));
                lines.add (juce::String (kind) + "|" + juce::String (number) + "|" + p.token + "|" + values.joinIntoString (";") + "|" + p.title);
            }
        ctx.settings.setValue (kLibraryKey, lines.joinIntoString ("\n"));
    }

    void Display::loadLibrary()
    {
        for (auto& list : programs)
            list.clear();
        for (const auto& line : juce::StringArray::fromLines (ctx.settings.getValue (kLibraryKey)))
        {
            juce::StringArray parts;
            parts.addTokens (line, "|", "");
            if (parts.size() < 5)
                continue;
            const int kind = parts[0].getIntValue(), number = parts[1].getIntValue();
            if (! juce::isPositiveAndBelow (kind, 3) || number < 1 || number > 99)
                continue;
            Program p;
            p.token = parts[2];
            for (const auto& v : juce::StringArray::fromTokens (parts[3], ";", ""))
                p.values.push_back (v.getFloatValue());
            parts.removeRange (0, 4);
            p.title = parts.joinIntoString ("|");
            programs[(size_t) kind][number] = std::move (p);
        }
    }

    void Display::storeLibrary (int kind)
    {
        const int number = getLibraryNumber();
        if (kind < 0 || number <= libraryPresets (kind) || number > libraryLast (kind))
            return; // a preset can't be written over
        Program p;
        if (kind == 2)
        {
            auto& fx = ctx.engine.getFx();
            const int model = fx.getModel (effectSlot());
            if (model <= ld::kFxExt)
                return;
            const auto& spec = models::model (models::Fx, model);
            p.token = spec.token;
            p.title = spec.name;
            for (int i = 0; i < (int) spec.params.size(); ++i)
                p.values.push_back (fx.params (effectSlot()).values[i].load());
        }
        else
        {
            auto* s = selected();
            if (s == nullptr)
                return;
            const auto table = kind == 1 ? models::Dynamics : s->isOutput() ? models::BusEq : models::ChannelEq;
            const int model = kind == 1 ? s->params.dynamics().model.load() : s->params.eq().model.load();
            const auto& spec = models::model (table, model);
            p.token = spec.token;
            p.title = kind == 1 ? spec.name : stripName (s) + " EQ";
            for (int i = 0; i < (int) spec.params.size(); ++i)
                p.values.push_back (kind == 1 ? s->params.dynamics().values[i].load() : s->params.eq().values[i].load());
        }
        if (const auto* old = userProgram (kind, number))
            p.title = old->title;
        auto store = [safe = juce::Component::SafePointer<Display> (this), kind, number, p] (const juce::String& title) mutable
        {
            if (safe == nullptr)
                return;
            p.title = title.substring (0, 12);
            safe->programs[(size_t) kind][number] = p;
            safe->saveLibrary();
            safe->refresh();
        };
        // With STORE CONFIRMATION, the title dialog is the confirmation (p.203).
        if (preference ("y01vStoreConfirm", true))
            askForText (*this, "TITLE EDIT", "Program " + juce::String (number).paddedLeft ('0', 2) + ":", p.title, store);
        else
            store (p.title);
    }

    void Display::editLibraryTitle (int kind)
    {
        const int number = getLibraryNumber();
        const auto* p = userProgram (kind, number);
        if (p == nullptr)
            return;
        askForText (*this, "TITLE EDIT", "Program " + juce::String (number).paddedLeft ('0', 2) + ":", p->title,
                    [safe = juce::Component::SafePointer<Display> (this), kind, number] (const juce::String& title)
                    {
                        if (safe == nullptr)
                            return;
                        const auto it = safe->programs[(size_t) kind].find (number);
                        if (it != safe->programs[(size_t) kind].end())
                        {
                            it->second.title = title.substring (0, 12);
                            safe->saveLibrary();
                            safe->refresh();
                        }
                    });
    }

    void Display::recallUserProgram (int kind, int number)
    {
        const auto* p = userProgram (kind, number);
        if (p == nullptr)
            return;
        if (kind == 2)
        {
            auto& fx = ctx.engine.getFx();
            const int model = models::findModel (models::Fx, p->token), slot = effectSlot();
            if (model < 0 || slot < 0)
                return;
            fx.setModel (slot, model);
            for (int i = 0; i < juce::jmin ((int) p->values.size(), (int) ld::kMaxFxParams); ++i)
                fx.params (slot).values[i] = p->values[(size_t) i];
            fxScroll = 0;
        }
        else if (auto* s = selected())
        {
            // Only onto a block of its kind: a 4-band program doesn't go onto CH 17-24's two bands.
            const auto table = kind == 1 ? models::Dynamics : s->isOutput() ? models::BusEq : models::ChannelEq;
            const int model = models::findModel (table, p->token);
            if (model < 0)
                return;
            if (kind == 1)
            {
                if (s->params.dynamics().model.load() != model)
                    s->setModel (ModelBlock::Dynamics, model);
                for (int i = 0; i < juce::jmin ((int) p->values.size(), (int) ld::kMaxDynParams); ++i)
                    s->params.dynamics().values[i] = p->values[(size_t) i];
            }
            else
            {
                if (s->params.eq().model.load() != model)
                    return;
                for (int i = 0; i < juce::jmin ((int) p->values.size(), (int) ld::kMaxEqParams); ++i)
                    s->params.eq().values[i] = p->values[(size_t) i];
            }
        }
        rebuild();
    }

    void Display::buildLibrary (int kind)
    {
        if (kind != 2 && selected() == nullptr)
            return;
        const int presets = libraryPresets (kind), count = libraryLast (kind) - libraryFirst (kind) + 1;
        if (library < 0)
            library = kind == 2 ? juce::jmax (0, libraryModels().indexOf (ctx.engine.getFx().getModel (effectSlot()))) : 0;
        library = juce::jlimit (0, count - 1, library);
        add (Cell::Button, { 119, 15, 34, 15 }, {}, "RECALL").press = [this, kind, presets]
        {
            if (kind == 2 && library < presets)
            {
                recallLibrary();
                return;
            }
            const int number = getLibraryNumber();
            if (userProgram (kind, number) == nullptr)
                return;
            if (! preference ("y01vRecallConfirm", false))
            {
                recallUserProgram (kind, number);
                return;
            }
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withTitle ("LIBRARY RECALL")
                                              .withMessage ("Recall " + getLibraryTitle (kind, number) + "?")
                                              .withButton ("RECALL")
                                              .withButton ("CANCEL")
                                              .withAssociatedComponent (this),
                                          [safe = juce::Component::SafePointer<Display> (this), kind, number] (int result)
                                          {
                                              if (safe != nullptr && result == 1)
                                                  safe->recallUserProgram (kind, number);
                                          });
        };

        KnobBinding b;
        b.label = "PROGRAM";
        b.steps = (float) juce::jmax (2, count);
        b.get = [this, count] { return count > 1 ? (float) library / (float) (count - 1) : 0.0f; };
        b.set = [this, count] (float v) { library = juce::roundToInt (v * (float) (count - 1)); };
        b.text = [this] { return juce::String (getLibraryNumber()).paddedLeft ('0', 2); };
        // Under RECALL, so that the cursor goes up to it.
        add (Cell::List, { 62, 45, 148, 24 }, b);

        add (Cell::Button, { 73, 15, 34, 15 }, {}, "STORE").press = [this, kind] { storeLibrary (kind); };
        add (Cell::Button, { 164, 15, 34, 15 }, {}, "TITLE|EDIT").press = [this, kind] { editLibraryTitle (kind); };
    }

    void Display::drawLibrary (int kind)
    {
        auto* s = selected();
        if (kind != 2 && s == nullptr)
            return;
        header (kind == 2 ? "EFFECT " + juce::String (effectSlot() + 1) + " LIBRARY" : stripName (s) + (kind == 0 ? "  EQ LIBRARY" : "  DYNAMICS LIBRARY"));
        text (71, 36, "No.");
        text (92, 36, "TITLE");
        if (kind != 0)
            text (199, 36, "TYPE");
        const int listRight = kind == 0 ? 193 : kind == 1 ? 256 : 273;
        box ({ 57, 44, listRight - 56, 35 });
        const int presets = libraryPresets (kind), first = libraryFirst (kind), last = libraryLast (kind);
        const bool up = preference ("y01vLibraryUp", true);
        for (int row = 0; row < 2; ++row)
        {
            const int number = first + (up ? library + 1 - row : library + row);
            if (number < first || number > last)
                continue;
            const bool preset = number <= presets;
            const auto* user = preset ? nullptr : userProgram (kind, number);
            const auto title = preset || user != nullptr ? getLibraryTitle (kind, number) : juce::String ("No Data!");
            const int y = 47 + row * 12;
            textTall (69, y, juce::String (number).paddedLeft ('0', 2) + "." + title.substring (0, kind == 0 ? 12 : 13), kTitleRows, 7.0f, false);
            if (preset) // read only
            {
                fill ({ 174, y, 8, 9 });
                text (176, y + 1, "R", true, 1, false);
            }
            if (kind != 0 && (preset || user != nullptr))
            {
                const auto table = kind == 1 ? models::Dynamics : models::Fx;
                const int model = preset ? libraryModels()[number - 1] : models::findModel (table, user->token);
                if (model >= 0)
                    text (194, y + 1, "=" + models::model (table, model).name.toUpperCase().replace ("COMPRESSOR", "COMP").substring (0, kind == 1 ? 9 : 12));
            }
            if (number == getLibraryNumber())
                for (int d = 0; d < 5; ++d)
                {
                    vLine (61 + d, y + d, y + 8 - d);
                    vLine (190 - d, y + d, y + 8 - d);
                }
        }
        if (kind == 0)
        {
            // The program's curve, 20 Hz to 20 kHz, 18 dB each way.
            box ({ 199, 44, 92, 35 });
            fill ({ 221, 44, 49, 3 }, false);
            text (222, 41, "EQ CURVE");
            const juce::Rectangle<int> graph (207, 51, 74, 26);
            box (graph);
            const int mid = graph.getCentreY();
            hLine (graph.getX(), graph.getRight() - 1, mid);
            const auto* user = userProgram (0, getLibraryNumber());
            const auto table = s->isOutput() ? models::BusEq : models::ChannelEq;
            const int model = user != nullptr ? models::findModel (table, user->token) : -1;
            if (model >= 0 && user->values.size() == models::model (table, model).params.size())
            {
                const int n = graph.getWidth() - 2;
                std::vector<float> freqs ((size_t) n), db ((size_t) n);
                for (int i = 0; i < n; ++i)
                    freqs[(size_t) i] = 20.0f * std::pow (1000.0f, (float) i / (float) (n - 1));
                if (models::modelResponse (table, model, user->values.data(), (int) user->values.size(), freqs.data(), db.data(), n))
                    for (int i = 0; i < n; ++i)
                    {
                        const int y = mid - juce::roundToInt (juce::jlimit (-18.0f, 18.0f, db[(size_t) i]) * 11.0f / 18.0f);
                        vLine (graph.getX() + 1 + i, juce::jmin (y, mid), juce::jmax (y, mid));
                    }
            }
        }
        if (kind == 2)
        {
            const auto& aux = ctx.engine.getAux();
            if (effectSlot() < (int) aux.size())
                channelMeter (*aux[(size_t) effectSlot()], "L", "R");
        }
        else
            channelMeter (*s, {}, {});
    }

    void Display::cursor (int dx, int dy)
    {
        if (cells.empty())
            return;
        const auto from = cells[(size_t) focus].area.getCentre();
        int best = -1, bestScore = std::numeric_limits<int>::max();
        for (int i = 0; i < (int) cells.size(); ++i)
        {
            if (i == focus)
                continue;
            const auto to = cells[(size_t) i].area.getCentre();
            const int along = dx != 0 ? (to.x - from.x) * dx : (to.y - from.y) * dy;
            const int across = dx != 0 ? std::abs (to.y - from.y) : std::abs (to.x - from.x);
            if (along < 6) // a field beside this one isn't "that way"
                continue;
            const int score = along + across * 3;
            if (score < bestScore)
            {
                bestScore = score;
                best = i;
            }
        }
        if (best >= 0)
            focus = best;
        else if (dy != 0 && function == Function::Send && page == 0 && effectSlot() >= 0)
        {
            // The arrows on an effect's page: more parameters, a row at a time.
            const int params = (int) models::model (models::Fx, ctx.engine.getFx().getModel (effectSlot())).params.size();
            if (dy > 0 ? (fxScroll + 3) * 3 < params : fxScroll > 0)
            {
                fxScroll += dy;
                rebuild();
                return;
            }
        }
        refresh();
    }

    void Display::turn (int delta)
    {
        if (juce::isPositiveAndBelow (focus, (int) cells.size()))
            stepBy (cells[(size_t) focus].binding, delta);
        refresh();
    }

    void Display::enter()
    {
        if (! juce::isPositiveAndBelow (focus, (int) cells.size()))
            return;
        const auto& b = cells[(size_t) focus].binding;
        if (cells[(size_t) focus].kind == Cell::Switch && b.isValid())
            b.set (b.get() > 0.5f ? 0.0f : 1.0f);
        if (cells[(size_t) focus].press)
            cells[(size_t) focus].press();
        refresh();
    }

    juce::String Display::getFocusText() const
    {
        return juce::isPositiveAndBelow (focus, (int) cells.size()) ? lcdValue (cells[(size_t) focus].binding) : juce::String();
    }

    void Display::drawLeft()
    {
        // The page's tab, the tabs of the pages behind it, on a line that stops at this one.
        hLine (0, kLeftColumn - 1, 7);
        for (int p = 0; p < getNumPages(); ++p)
        {
            const int x0 = p * 11;
            hLine (x0 + 2, x0 + 7, 0);
            dot (x0 + 1, 1);
            dot (x0 + 8, 1);
            vLine (x0, 2, 7);
            vLine (x0 + 9, 2, 7);
            if (p == page)
            {
                hLine (x0 + 1, x0 + 8, 7, false);
                text (x0 + 4, 2, juce::String (p + 1), true);
            }
            else
            {
                for (int y = 1; y < 7; ++y)
                    for (int x = x0 + 1; x < x0 + 9; ++x)
                        if (((x + y) & 1) == 0)
                            dot (x, y);
            }
        }

        // The function's name: tall and bold.
        const auto name = functionName();
        const float pitch = name.length() > 7 ? 54.0f / (float) name.length() : 7.0f;
        const int width = juce::roundToInt (pitch * (float) (name.length() - 1)) + 6;
        textTall ((kLeftColumn - width) / 2, 10, name, kNameRows, pitch, true);
        hLine (0, kLeftColumn - 1, 22);

        // The scene: its title, a padlock when it can't be written, its number.
        const int current = ctx.show.getCurrentSceneIndex();
        const auto scene = current >= 0 ? ctx.show.getScene (current) : std::nullopt;
        const auto title = scene ? scene->name.substring (0, 8) : juce::String ("InitData");
        textTall (2, 24, title, kTitleRows, 6.0f, false);
        if (! scene || isProtected (current + 1))
        {
            hLine (50, 51, 25);
            vLine (49, 26, 27);
            vLine (52, 26, 27);
            fill ({ 48, 28, 7, 5 });
            dot (50, 30, false);
        }
        text (1, 33, juce::String (current + 1).paddedLeft ('0', 2), false, 3);
        fill ({ 35, 46, 19, 8 });
        text (37, 48, "EDIT", true, 1, false);
        hLine (0, kLeftColumn - 1, 54);

        // The two effects over their return levels; the one the faders work is filled (p.29).
        for (int i = 0; i < 2; ++i)
        {
            const int m = ctx.engine.getFx().getModel (i);
            juce::String fxName ("----");
            if (m > 0)
            {
                const auto words = juce::StringArray::fromTokens (models::model (models::Fx, m).name.toUpperCase(), " ", "");
                fxName = (words.size() > 1 && words[0] == "REVERB" ? words[1] : words[0]).substring (0, 6);
            }
            const int cx = 13 + i * 28;
            text (cx - textWidth (fxName, true) / 2, 57, fxName, true);
            const auto& aux = ctx.engine.getAux();
            const float level = i < (int) aux.size() ? fader::dbToPos (aux[(size_t) i]->params.faderDb.load()) : 0.0f;
            dial (cx, 72, 6, level, effectSlot() == i);
        }
        vLine (kLeftColumn, 0, kDotsH - 1);
    }

    void Display::drawRight()
    {
        vLine (kRightColumn, 0, kDotsH - 1);
        const int cx = 307;
        const auto row = [&] (int i, int y, int valueY, bool rightAligned)
        {
            const auto& b = selectedBindings[(size_t) i];
            dial (cx, y, 6, b.isValid() ? b.get() : 0.5f);
            const auto s = lcdValue (b).replace ("dB", "");
            text (rightAligned ? 319 - textWidth (s, true) : cx - textWidth (s, true) / 2, valueY, s, true);
        };
        // An AUX or EFFECT send has no pan, so no PAN icon (p.97's pictures).
        auto* strip = selected();
        if (strip == nullptr || strip->getKind() != StripKind::Bus)
            row (0, 8, 16, false);
        row (1, 32, 40, false);
        row (2, 56, 65, true);
        fill ({ kRightColumn + 1, 72, kDotsW - kRightColumn - 1, 8 });
        const auto name = stripName (strip);
        text (cx - textWidth (name, false) / 2, 73, name, false, 1, false);
    }

    void Display::drawCell (const Cell& c, bool focused)
    {
        const auto& b = c.binding;
        if (c.custom)
        {
            if (focused)
            {
                box (c.area.expanded (1));
                box (c.area.expanded (2));
            }
            return;
        }
        switch (c.kind)
        {
            case Cell::Text:
            {
                auto value = c.raw && b.isValid() && b.text ? b.text() : lcdValue (b);
                if (! c.caption.isEmpty())
                {
                    // A parameter box's name is printed full size over it.
                    if (c.dotted)
                        text (c.area.getX(), c.area.getY() - 8, c.caption);
                    else
                        text (c.area.getX() + 1, c.area.getY() - 7, c.caption, true);
                }
                juce::String unit;
                if (c.dotted && ! c.raw && value.isNotEmpty() && (juce::CharacterFunctions::isDigit (value[0]) || value[0] == '-' || value[0] == '+'))
                {
                    // The number in the box, its unit outside (p.30).
                    int split = value.length();
                    while (split > 0 && ! juce::CharacterFunctions::isDigit (value[split - 1]))
                        --split;
                    unit = value.substring (split);
                    value = value.substring (0, split);
                }
                if (c.dotted)
                {
                    dottedH (c.area.getX() - 1, c.area.getRight(), c.area.getY() - 1);
                    dottedH (c.area.getX() - 1, c.area.getRight(), c.area.getBottom());
                    dottedV (c.area.getX() - 1, c.area.getY() - 1, c.area.getBottom());
                    dottedV (c.area.getRight(), c.area.getY() - 1, c.area.getBottom());
                }
                if (textWidth (value, false) > c.area.getWidth())
                    value = value.replace ("dB", "");
                if (c.centred)
                    textCentred (c.area, value, textWidth (value, false) > c.area.getWidth());
                else
                    text (c.area.getRight() - textWidth (value, false) - 1, c.area.getY() + 1, value);
                if (unit.isNotEmpty())
                    text (c.area.getRight() + 2, c.area.getY() + 1, unit);
                break;
            }
            case Cell::Switch:
            {
                const bool on = ! c.plain && b.isValid() && b.get() > 0.5f;
                if (! c.caption.isEmpty())
                    text (c.area.getX() + 1, c.area.getY() - 7, c.caption, true);
                if (on)
                    fill (c.area);
                else
                {
                    // A shadow on the right and below, as the switches have (p.30).
                    box (c.area);
                    hLine (c.area.getX() + 1, c.area.getRight(), c.area.getBottom());
                    vLine (c.area.getRight(), c.area.getY() + 1, c.area.getBottom());
                }
                {
                    const auto value = c.raw && b.isValid() && b.text ? b.text() : lcdValue (b);
                    textCentred (c.area, value, c.tiny || textWidth (value, false) > c.area.getWidth() - 1, ! on);
                }
                break;
            }
            case Cell::Dial:
            {
                const int cx = c.area.getCentreX();
                dial (cx, c.area.getY() + 7, 6, b.isValid() ? b.get() : 0.5f);
                const auto value = lcdValue (b);
                const bool tiny = textWidth (value, false) > c.area.getWidth();
                if (! c.caption.isEmpty())
                {
                    text (cx - textWidth (c.caption, true) / 2, c.area.getY() + 15, c.caption, true);
                    text (cx - textWidth (value, tiny) / 2, c.area.getY() + 22, value, tiny);
                }
                else
                {
                    text (cx - textWidth (value, tiny) / 2, c.area.getBottom() - 6, value, tiny);
                }
                break;
            }
            case Cell::DialLine:
            {
                dial (c.area.getX() + 7, c.area.getY() + 8, 6, b.isValid() ? b.get() : 0.5f);
                text (c.area.getX() + 16, c.area.getY(), c.caption);
                text (c.area.getX() + 16, c.area.getY() + 9, lcdValue (b));
                break;
            }
            case Cell::Button:
            {
                box (c.area);
                hLine (c.area.getX() + 1, c.area.getRight(), c.area.getBottom());
                vLine (c.area.getRight(), c.area.getY() + 1, c.area.getBottom());
                const auto lines = juce::StringArray::fromTokens (c.caption, "|", "");
                for (int i = 0; i < lines.size(); ++i)
                    textCentred ({ c.area.getX(), c.area.getY() + (c.area.getHeight() - 8 * lines.size()) / 2 + i * 8, c.area.getWidth(), 8 }, lines[i], c.tiny);
                if (c.enabled && ! c.enabled())
                    for (int y = c.area.getY() + 1; y < c.area.getBottom() - 1; ++y)
                        for (int x = c.area.getX() + 1; x < c.area.getRight() - 1; ++x)
                            if (((x + y) & 1) == 0)
                                dot (x, y, false);
                break;
            }
            case Cell::List:
                break;
            case Cell::Bar:
            {
                const int cx = c.area.getCentreX();
                const int top = c.area.getY(), bottom = c.area.getBottom() - 9;
                // The slot, an arrow at the nominal level, the ribbed knob (p.30).
                vLine (cx - 1, top, bottom);
                vLine (cx + 1, top, bottom);
                dot (cx, top);
                dot (cx, bottom);
                const auto yOf = [&] (float position) { return bottom - juce::roundToInt (position * (float) (bottom - top)); };
                const int nominal = yOf (fader::dbToPos (0.0f));
                for (int i = 0; i < 3; ++i)
                    vLine (cx - 4 - i, nominal - i, nominal + i);
                const int y = yOf (b.isValid() ? b.get() : 0.0f);
                fill ({ cx - 3, y - 4, 7, 9 });
                for (int row = -2; row <= 2; row += 2)
                    hLine (cx - 2, cx + 2, y + row, false);
                if (c.caption.isNotEmpty())
                    text (cx - textWidth (c.caption, true) / 2, bottom + 3, c.caption, true);
                break;
            }
        }
        if (focused)
        {
            box (c.area.expanded (1));
            box (c.area.expanded (2));
        }
    }

    void Display::drawEq (Strip& s)
    {
        auto& eq = s.params.eq();
        header (stripName (&s) + "  EQ");
        box ({ 61, 11, 35, 10 });
        textCentred ({ 61, 11, 35, 10 }, "EQ ON");
        if (s.isInput())
        {
            box ({ 61, 43, 35, 10 });
            textCentred ({ 61, 43, 35, 10 }, "ATT");
        }

        // The response, filled from the 0 dB line to the curve. The axis (from the
        // manual's picture) runs from 12 Hz to 30 kHz, 40.5 dots a decade.
        const juce::Rectangle<int> graph (111, 0, 139, 41);
        const int mid = 20;
        const auto xOf = [&graph] (float hz) { return graph.getX() + juce::roundToInt (std::log10 (hz / 12.0f) * 40.5f); };
        const auto table = s.isOutput() ? models::BusEq : models::ChannelEq;
        const auto& m = models::model (table, eq.model.load());
        std::vector<float> values (m.params.size());
        for (size_t i = 0; i < values.size(); ++i)
            values[i] = eq.values[i].load();
        const int n = graph.getWidth();
        std::vector<float> freqs ((size_t) n), db ((size_t) n);
        for (int i = 0; i < n; ++i)
            freqs[(size_t) i] = 12.0f * std::pow (10.0f, (float) i / 40.5f);
        std::vector<int> curve ((size_t) n, mid);
        if (models::modelResponse (table, eq.model.load(), values.data(), (int) values.size(), freqs.data(), db.data(), n))
            for (int i = 0; i < n; ++i)
            {
                const int y = mid - juce::roundToInt (juce::jlimit (-24.0f, 24.0f, db[(size_t) i]) * 15.0f / 18.0f);
                curve[(size_t) i] = y;
                vLine (graph.getX() + i, juce::jmax (0, juce::jmin (y, mid)), juce::jmin (graph.getBottom() - 1, juce::jmax (y, mid)));
            }
        vLine (graph.getX(), graph.getY(), graph.getBottom() - 1);
        vLine (graph.getRight() - 1, graph.getY(), graph.getBottom() - 1);
        hLine (graph.getX(), graph.getRight() - 1, mid);
        for (int level = -3; level <= 3; ++level) // the dB ticks up both sides, every 5 dots
        {
            const int y = mid + level * 5, length = level % 2 == 0 ? 4 : 2;
            hLine (graph.getX(), graph.getX() + length, y);
            hLine (graph.getRight() - 1 - length, graph.getRight() - 1, y);
        }
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
            vLine (xOf (hz), graph.getY(), graph.getBottom() - 1);
        // The dotted line: the frequency of the band F and G are working.
        {
            const char* prefixes[] = { "l", "lm", "hm", "h" };
            const int f = m.find (juce::String (prefixes[juce::jlimit (0, 3, eqBand)]) + "f");
            if (f >= 0)
                dottedV (juce::jlimit (graph.getX() + 1, graph.getRight() - 2, xOf (eq.values[f].load())), graph.getY(), graph.getBottom() - 1);
        }
        for (auto [hz, label] : std::initializer_list<std::pair<float, const char*>> { { 100.0f, "100" }, { 1000.0f, "1.0K" }, { 10000.0f, "10K" } })
        {
            // The grid line stops for its label; the label is reversed over the fill.
            const int w = textWidth (label, true), x0 = xOf (hz) - w / 2, y0 = 24;
            for (int x = x0 - 1; x <= x0 + w; ++x)
                for (int y = y0 - 1; y <= y0 + 5; ++y)
                {
                    const int c = curve[(size_t) juce::jlimit (0, n - 1, x - graph.getX())];
                    dot (x, y, y >= juce::jmin (c, mid) && y <= juce::jmax (c, mid));
                }
            textFlipped (x0, y0, label);
        }

        // The channel's meter against its marks, the pair's numbers under it.
        const auto name = stripName (&s);
        juce::String left ("L"), right ("R");
        if (name.startsWith ("CH"))
        {
            const int first = name.getTrailingIntValue() - (name.getTrailingIntValue() - 1) % 2;
            left = juce::String (first);
            right = juce::String (first + 1);
        }
        else if (name.startsWith ("AUX") || name.startsWith ("EFF") || name.startsWith ("BUS"))
        {
            left = "1";
            right = "2";
        }
        channelMeter (s, left, right);

        // The table.
        const char* bands[] = { "LOW", "LO-MID", "HI-MID", "HIGH" };
        for (int i = 0; i < 4; ++i)
            textCentred ({ kEqCols[i], 42, kEqCols[i + 1] - kEqCols[i], 8 }, bands[i]);
        hLine (kEqCols[0], kRightColumn, 41);
        for (int y : { 50, 60, 70, 79 })
            hLine (100, kRightColumn, y);
        for (int x : kEqCols)
            vLine (x, 41, 79);
        vLine (100, 50, 79);
        text (102, 52, "F");
        text (102, 62, "G");
        text (102, 72, "Q");
    }

    void Display::drawPage()
    {
        auto* s = selected();
        switch (function)
        {
            case Function::EqAtt:
                if (page == 1)
                    drawLibrary (0);
                else if (s != nullptr)
                    drawEq (*s);
                break;
            case Function::Dynamics:
                if (page == 1)
                    drawLibrary (1);
                else if (s != nullptr)
                    drawDynamics (*s);
                break;
            case Function::PhaseDelay:
                if (page == 0)
                    drawPhase();
                else if (page == 3)
                    drawOutputDelay();
                else
                    drawDelay();
                break;
            case Function::View:
                if (s != nullptr)
                    drawView (*s);
                break;
            case Function::PanRouting:
                if (page == 0)
                    drawPan();
                else if (page == 1)
                    drawRouting();
                else if (page == 2)
                    drawBusMaster();
                else
                    drawOmni();
                break;
            case Function::Send:
                if (sendBus() >= 0 && page == (effectSlot() >= 0 ? 2 : 0))
                {
                    drawPrePost();
                    break;
                }
                if (effectSlot() >= 0)
                {
                    page == 0 ? drawEffectEdit() : drawLibrary (2);
                    break;
                }
                header (functionName() + " SEND");
                if (juce::isPositiveAndBelow (focus, (int) cells.size()))
                {
                    const auto value = cells[(size_t) focus].caption + "  " + lcdValue (cells[(size_t) focus].binding);
                    text (kRightColumn - textWidth (value, false) - 3, 1, value);
                }
                break;
            case Function::Memory:
                drawMemory();
                break;
            case Function::Utility:
                page == 0 ? drawOscillator() : drawPreferences();
                break;
            case Function::OptionIO:
                drawOption();
                break;
            case Function::Midi:
                if (page == 0)
                    drawMidiSetup();
                else if (page == 1)
                    drawMidiProgram();
                else if (page == 2)
                    drawMidiControl();
                else if (page == 3)
                    drawMidiBulk();
                else
                    drawMidiLocal();
                break;
            case Function::Setup:
                if (page == 0)
                    drawWordClock();
                else if (page == 1)
                    drawMonitorSetup();
                else if (page == 2)
                    drawGroups();
                else if (page == 3)
                    drawPairs();
                else
                    drawDither();
                break;
            case Function::Remote:
                drawRemote();
                break;
        }
        for (int i = 0; i < (int) cells.size(); ++i)
            drawCell (cells[(size_t) i], i == focus);
    }

    void Display::refresh()
    {
        clear();
        drawLeft();
        drawPage();
        drawRight();
        if (dots != shownDots)
        {
            shownDots = dots;
            renderImage();
            repaint();
        }
    }

    void Display::renderImage()
    {
        const auto pixel = kLcdPixel.interpolatedWith (kLcd, 1.0f - juce::jlimit (0.25f, 1.0f, contrast + 0.25f));
        const auto edge = pixel.interpolatedWith (kLcd, 0.35f), grid = kLcd.darker (0.04f);
        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < kDotsH; ++y)
            for (int x = 0; x < kDotsW; ++x)
            {
                const bool on = dots[(size_t) (y * kDotsW + x)] != 0;
                for (int py = 0; py < kDot; ++py)
                    for (int px = 0; px < kDot; ++px)
                    {
                        const bool last = px == kDot - 1 || py == kDot - 1;
                        data.setPixelColour (x * kDot + px, y * kDot + py, on ? (last ? edge : pixel) : (last ? grid : kLcd));
                    }
            }
    }

    void Display::paint (juce::Graphics& g) { g.drawImageAt (image, 0, 0); }

    void Display::pick (juce::Point<int> pixel)
    {
        const juce::Point<int> at (pixel.x / kDot, pixel.y / kDot);
        for (int i = 0; i < (int) cells.size(); ++i)
            if (cells[(size_t) i].area.expanded (1).contains (at))
                focus = i;
    }

    void Display::mouseDown (const juce::MouseEvent& e)
    {
        pick (e.getPosition());
        dragStart = juce::isPositiveAndBelow (focus, (int) cells.size()) && cells[(size_t) focus].binding.isValid() ? cells[(size_t) focus].binding.get() : 0.0f;
        refresh();
    }

    void Display::mouseDrag (const juce::MouseEvent& e)
    {
        if (! juce::isPositiveAndBelow (focus, (int) cells.size()))
            return;
        const auto& b = cells[(size_t) focus].binding;
        if (b.isValid() && b.set)
            b.set (juce::jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() / 300.0f));
        refresh();
    }

    void Display::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        pick (e.getPosition());
        turn (w.deltaY > 0 ? 1 : -1);
    }
} // namespace y01v

//==============================================================================
Y01VSurface::Y01VSurface (UiContext& c)
    : ctx (c), theme (Theme::dark()), universalKey ("UNIVERSAL MIXER", theme.accent, theme), display (c), meter (c)
{
    using namespace y01v;
    addAndMakeVisible (display);
    addAndMakeVisible (meter);

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

    // The faders.
    const auto inputLaw = ctx.engine.getFaderLaw ((int) StripKind::Channel);
    for (int i = 0; i < kStrips; ++i)
    {
        const bool stereo = i >= 12;
        faders.push_back (std::make_unique<Fader> (stereo ? kCapDark : kCapCream, stereo ? kCream : juce::Colour (0xff1e1d1c), false, inputLaw));
        addAndMakeVisible (*faders.back());
    }
    masterFader = std::make_unique<Fader> (kCapRed, juce::Colour (0xff1e1d1c), true, ctx.engine.getFaderLaw ((int) StripKind::Main));
    addAndMakeVisible (*masterFader);
    for (int i = 0; i <= kStrips; ++i)
    {
        tapes.push_back (std::make_unique<ConsoleTape> (i + 1));
        auto& t = *tapes.back();
        t.setComponentID ("TAPE" + juce::String (i + 1));
        const auto strip = [this, i] { return i < kStrips ? stripAt (i) : masterStrip(); };
        t.getText = [strip] { auto* s = strip(); return s != nullptr ? s->getOwnName() : juce::String(); };
        t.setText = [strip] (const juce::String& n) { if (auto* s = strip()) s->setName (n); };
        addAndMakeVisible (t);
    }

    // The strips' keys.
    for (int i = 0; i < kStrips; ++i)
    {
        const int x = stripCx (i) - kKeyW / 2;
        auto& sel = key ("SEL " + juce::String (i), kCream, true, { x, kSelY, kKeyW, kKeyH });
        auto& solo = key ("SOLO " + juce::String (i), kSoloGrey, true, { x, kSoloY, kKeyW, kKeyH });
        auto& on = key ("ON " + juce::String (i), kOnGrey, true, { x, kOnY, kKeyW, kKeyH });
        sel.isOn = [this, i] { auto* s = stripAt (i); return s != nullptr && ctx.getSelectedStrip && s == ctx.getSelectedStrip(); };
        sel.onClick = [this, i]
        {
            // SETUP's GROUP page and REMOTE take the [SEL] keys (pp.55, 194).
            if (display.pressSel (i))
                return;
            if (auto* s = stripAt (i); s != nullptr && ctx.selectStrip)
                ctx.selectStrip (s);
        };
        solo.isOn = [this, i] { auto* s = stripAt (i); return s != nullptr && s->params.solo.load(); };
        solo.onClick = [this, i] { pressSolo (stripAt (i)); };
        on.isOn = [this, i]
        {
            if (remoteWorks())
                return remoteOn[(size_t) i].isValid() && remoteOn[(size_t) i].get() > 0.5f;
            auto* s = stripAt (i);
            return s != nullptr && ! s->params.mute.load();
        };
        on.onClick = [this, i]
        {
            if (remoteWorks())
            {
                if (const auto& b = remoteOn[(size_t) i]; b.isValid() && b.set)
                    b.set (b.get() > 0.5f ? 0.0f : 1.0f);
            }
            else if (auto* s = stripAt (i))
                s->params.mute = ! s->params.mute.load();
        };
    }
    {
        const int x = kMasterCx - kKeyW / 2;
        auto& sel = key ("SEL MASTER", kCream, true, { x, kSelY, kKeyW, kKeyH });
        auto& on = key ("ON MASTER", kOnGrey, true, { x, kOnY, kKeyW, kKeyH });
        sel.isOn = [this] { auto* s = masterStrip(); return s != nullptr && ctx.getSelectedStrip && s == ctx.getSelectedStrip(); };
        sel.onClick = [this]
        {
            if (display.pressSel (kStrips))
                return;
            if (auto* s = masterStrip(); s != nullptr && ctx.selectStrip)
                ctx.selectStrip (s);
        };
        on.isOn = [this]
        {
            if (remoteWorks())
                return remoteOn[(size_t) kStrips].isValid() && remoteOn[(size_t) kStrips].get() > 0.5f;
            auto* s = masterStrip();
            return s != nullptr && ! s->params.mute.load();
        };
        on.onClick = [this]
        {
            if (remoteWorks())
            {
                if (const auto& b = remoteOn[(size_t) kStrips]; b.isValid() && b.set)
                    b.set (b.get() > 0.5f ? 0.0f : 1.0f);
            }
            else if (auto* s = masterStrip())
                s->params.mute = ! s->params.mute.load();
        };
    }
    for (int i = 0; i < 2; ++i)
    {
        const int x = kReturnCx + i * kReturnPitch - kKeyW / 2;
        const auto strip = [this, i]() -> Strip* { const auto& a = ctx.engine.getAux(); return i < (int) a.size() ? a[(size_t) i].get() : nullptr; };
        auto& sel = key ("SEL RTN" + juce::String (i + 1), kCream, true, { x, kSelY, kKeyW, kKeyH });
        auto& solo = key ("SOLO RTN" + juce::String (i + 1), kSoloGrey, true, { x, kSoloY, kKeyW, kKeyH });
        auto& on = key ("ON RTN" + juce::String (i + 1), kOnGrey, true, { x, kOnY, kKeyW, kKeyH });
        sel.isOn = [this, strip] { auto* s = strip(); return s != nullptr && ctx.getSelectedStrip && s == ctx.getSelectedStrip(); };
        sel.onClick = [this, strip] { if (auto* s = strip(); s != nullptr && ctx.selectStrip) ctx.selectStrip (s); };
        solo.isOn = [strip] { auto* s = strip(); return s != nullptr && s->params.solo.load(); };
        solo.onClick = [this, strip] { pressSolo (strip()); };
        on.isOn = [strip] { auto* s = strip(); return s != nullptr && ! s->params.mute.load(); };
        on.onClick = [strip] { if (auto* s = strip()) s->params.mute = ! s->params.mute.load(); };
    }

    // The function keys.
    const auto functionKey = [this] (const char* name, Function f, juce::Colour face, int col, int y)
    {
        auto& k = key (name, face, false, { 46 + col * 78, y, 66, 34 });
        k.onClick = [this, f] { display.pressFunction (f); };
    };
    functionKey ("UTILITY", Function::Utility, kCream, 0, 167);
    functionKey ("MIDI", Function::Midi, kCream, 1, 167);
    functionKey ("SETUP", Function::Setup, kCream, 2, 167);
    functionKey ("VIEW", Function::View, kCream, 3, 167);
    functionKey ("DYNAMICS", Function::Dynamics, kAmber, 0, 227);
    functionKey ("EQ/ATT", Function::EqAtt, kSeafoam, 1, 227);
    functionKey ("PHASE/DELAY", Function::PhaseDelay, kCream, 2, 227);
    functionKey ("PAN/ROUTING", Function::PanRouting, kCream, 3, 227);

    // The fader mode keys.
    const auto modeKey = [this] (const char* name, FaderMode m, juce::Colour face, juce::Rectangle<int> r, std::optional<Function> f)
    {
        auto& k = key (name, face, true, r);
        k.isOn = [this, m] { return mode == m; };
        k.onClick = [this, m, f]
        {
            const bool again = mode == m && f && display.getFunction() == *f;
            setFaderMode (m);
            if (again)
                display.nextPage();
            else if (f)
                display.setFunction (*f);
        };
    };
    modeKey ("EFFECT 1", FaderMode::Effect1, kBlueGrey, { 46, 296, 66, 34 }, Function::Send);
    modeKey ("EFFECT 2", FaderMode::Effect2, kBlueGrey, { 124, 296, 66, 34 }, Function::Send);
    modeKey ("OPTION I/O", FaderMode::OptionIO, kCream, { 202, 296, 66, 34 }, Function::OptionIO);
    modeKey ("REMOTE", FaderMode::Remote, kCream, { 280, 296, 66, 34 }, Function::Remote);
    modeKey ("AUX 1", FaderMode::Aux1, kBlueGrey, { 46, 346, 66, 34 }, Function::Send);
    modeKey ("AUX 2", FaderMode::Aux2, kBlueGrey, { 124, 346, 66, 34 }, Function::Send);
    modeKey ("AUX 3", FaderMode::Aux3, kBlueGrey, { 202, 346, 66, 34 }, Function::Send);
    modeKey ("AUX 4", FaderMode::Aux4, kBlueGrey, { 280, 346, 66, 34 }, Function::Send);
    modeKey ("HOME", FaderMode::Home, kCream, { 46, 398, 66, 34 }, std::nullopt);

    // SELECTED CHANNEL.
    knob ("PAN", kKnobTeal, {}, { 1482, 187, 54, 54 });
    knob ("F", kKnobTeal, {}, { 1482, 269, 54, 54 });
    knob ("G", kCream, {}, { 1482, 351, 54, 54 });
    const char* bandNames[] = { "LOW", "LO-MID", "HI-MID", "HIGH" };
    for (int band = 0; band < 4; ++band)
    {
        auto& k = key (bandNames[band], kSeafoam, false, { 1616, 192 + (3 - band) * 58, 74, 38 });
        k.onClick = [this, band]
        {
            setEqBand (band);
            if (display.preference ("y01vEqAutoScreen"))
                display.showFirstPage (Function::EqAtt);
        };
    }

    // The RETURN knobs, the LCD's contrast.
    knob ("RETURN 1", kKnobBlue, {}, { kReturnCx - 17, 434, 34, 34 });
    knob ("RETURN 2", kKnobBlue, {}, { kReturnCx + kReturnPitch - 17, 434, 34, 34 });
    {
        auto& k = knob ("CONTRAST", juce::Colour (0xff2a2a2a), juce::Colour (0xff8a8a8a), { 1775, 382, 40, 40 });
        KnobBinding b;
        b.label = "CONTRAST";
        b.get = [this] { return contrastValue.load(); };
        b.set = [this] (float v)
        {
            contrastValue = v;
            display.setContrast (v);
        };
        k.setBinding (b);
    }

    // The input row: GAIN and PAD (the connectors above them do nothing here).
    for (int i = 0; i < kStrips; ++i)
    {
        knob ("GAIN " + juce::String (i), kKnobDark, kCream, { stripCx (i) - 33, 64, 46, 46 });
        if (i < (int) pad.size())
        {
            auto& k = key ("PAD " + juce::String (i), kPadGrey, false, { stripCx (i) + 21, 66, 24, 18 });
            k.isOn = [this, i] { return pad[(size_t) i]; };
            k.onClick = [this, i] { pressPad (i); };
        }
    }
    {
        // The 15/16 - 2TR IN switch (p.77), where CH 1-12 have their PAD.
        auto& k = key ("2TR IN", kPadGrey, false, { stripCx (13) + 21, 66, 24, 18 });
        k.isOn = [this]
        {
            const auto& channels = ctx.engine.getChannels();
            return 13 < (int) channels.size() && channels[13]->params.inputLeft.load() == y01v::kTwoTrackInput;
        };
        k.onClick = [this] { pressTwoTrack(); };
    }
    knob ("MONITOR", kCream, kKnobDark, { kMasterCx - 26, 64, 52, 52 });
    getKnob ("MONITOR").setBinding (bindFader ("MONITOR", ctx.engine.monitorLevelDb));
    knob ("PHONES", kCream, kKnobDark, { kReturnCx - 26, 64, 52, 52 });
    getKnob ("PHONES").setBinding (bindFader ("PHONES", ctx.engine.phones.levelDb));

    // The data entry section.
    auto& memory = key ("MEMORY", kMemory, true, { kRightCx - 33, 582, 66, 40 });
    memory.isOn = [this] { return display.getFunction() == Function::Memory; };
    memory.onClick = [this] { display.setFunction (Function::Memory); };
    key ("-1/DEC", kDark, false, { 1690, 668, 54, 40 }).onClick = [this] { display.incDec (-1); };
    key ("+1/INC", kDark, false, { 1836, 668, 54, 40 }).onClick = [this] { display.incDec (1); };
    addAndMakeVisible (wheel);
    wheel.onTurn = [this] (int steps) { display.turn (steps); };
    key ("CURSOR UP", kDark, false, { kRightCx - 32, 880, 64, 40 }).onClick = [this] { display.cursor (0, -1); };
    key ("CURSOR DOWN", kDark, false, { kRightCx - 32, 942, 64, 40 }).onClick = [this] { display.cursor (0, 1); };
    key ("CURSOR LEFT", kDark, false, { kRightCx - 102, 911, 64, 40 }).onClick = [this] { display.cursor (-1, 0); };
    key ("CURSOR RIGHT", kDark, false, { kRightCx + 38, 911, 64, 40 }).onClick = [this] { display.cursor (1, 0); };
    key ("ENTER", kDark, false, { 1700, 1004, 64, 40 }).onClick = [this] { display.enter(); };

    display.onRemoteChanged = [this]
    {
        if (mode == y01v::FaderMode::Remote)
            bindStrips();
    };
    refreshAll();
    startTimerHz (30);
}

Y01VSurface::~Y01VSurface() = default;

y01v::Key& Y01VSurface::key (const juce::String& name, juce::Colour face, bool led, juce::Rectangle<int> r)
{
    auto k = std::make_unique<y01v::Key> (name, face, led);
    k->setBounds (r);
    addAndMakeVisible (*k);
    keys.emplace_back (name, std::move (k));
    return *keys.back().second;
}

y01v::Knob& Y01VSurface::knob (const juce::String& name, juce::Colour cap, juce::Colour pointer, juce::Rectangle<int> r)
{
    auto k = std::make_unique<y01v::Knob> (cap, pointer);
    k->setBounds (r);
    addAndMakeVisible (*k);
    knobs.emplace_back (name, std::move (k));
    return *knobs.back().second;
}

y01v::Key* Y01VSurface::findKey (const juce::String& name) const
{
    for (const auto& [n, k] : keys)
        if (n == name)
            return k.get();
    return nullptr;
}

y01v::Knob& Y01VSurface::getKnob (const juce::String& name)
{
    for (const auto& [n, k] : knobs)
        if (n == name)
            return *k;
    jassertfalse;
    return *knobs.front().second;
}

int Y01VSurface::sendBus() const noexcept { return busOf (mode); }

Strip* Y01VSurface::stripAt (int i) const
{
    const auto& channels = ctx.engine.getChannels();
    // REMOTE page 1 gives the faders and [ON] keys other work; on its other pages they do their own (p.194).
    if (remoteWorks())
        return nullptr;
    const int index = mode == y01v::FaderMode::OptionIO ? (i < 8 ? kOptionChannel + i : -1) : i;
    return juce::isPositiveAndBelow (index, (int) channels.size()) ? channels[(size_t) index].get() : nullptr;
}

bool Y01VSurface::remoteWorks() const noexcept { return mode == y01v::FaderMode::Remote && display.isRemoteActive(); }

Strip* Y01VSurface::masterStrip() const
{
    if (remoteWorks())
        return nullptr;
    const int bus = sendBus();
    const auto& buses = ctx.engine.getBuses();
    if (bus >= 0)
        return bus < (int) buses.size() ? buses[(size_t) bus].get() : nullptr;
    return ctx.engine.getMains().empty() ? nullptr : &ctx.engine.getMain (0);
}

void Y01VSurface::setFaderMode (y01v::FaderMode m)
{
    mode = m;
    display.setFaderMode (m);
    bindStrips();
}

void Y01VSurface::setEqBand (int band)
{
    eqBand = juce::jlimit (0, 3, band);
    bindKnobs();
    repaint();
}

void Y01VSurface::bindStrips()
{
    const auto inputLaw = ctx.engine.getFaderLaw ((int) StripKind::Channel);
    const int bus = sendBus();
    if (remoteWorks())
    {
        for (int i = 0; i <= kStrips; ++i)
        {
            (i < kStrips ? *faders[(size_t) i] : *masterFader).bind (display.remoteFader (i));
            remoteOn[(size_t) i] = display.remoteOn (i);
        }
    }
    else
    {
        for (int i = 0; i < kStrips; ++i)
        {
            auto* s = stripAt (i);
            faders[(size_t) i]->bind (s == nullptr ? nullptr : bus >= 0 ? &s->sends[(size_t) bus].levelDb : &s->params.faderDb, inputLaw);
        }
        auto* master = masterStrip();
        masterFader->bind (master != nullptr ? &master->params.faderDb : nullptr, ctx.engine.getFaderLaw ((int) (bus >= 0 ? StripKind::Bus : StripKind::Main)));
    }

    // The RETURN knobs: the returns' levels, or their sends to the fader mode's mix (p.35).
    const auto& aux = ctx.engine.getAux();
    for (int i = 0; i < 2; ++i)
    {
        auto& k = getKnob ("RETURN " + juce::String (i + 1));
        if (i >= (int) aux.size() || mode == y01v::FaderMode::OptionIO || mode == y01v::FaderMode::Remote || bus == kEffectBus + i)
            k.setBinding ({});
        else
            k.setBinding (bindFader ("RETURN", bus >= 0 ? aux[(size_t) i]->sends[(size_t) bus].levelDb : aux[(size_t) i]->params.faderDb));
    }
}

void Y01VSurface::bindInputs()
{
    // The row above the display: always CH 1-16, whatever the faders do. CH 1-12's GAIN
    // is printed -16 to -60 (PAD out) or +10 to -34 (PAD in), 13-16's +10 to -20.
    const auto& channels = ctx.engine.getChannels();
    for (int i = 0; i < kStrips; ++i)
    {
        auto& k = getKnob ("GAIN " + juce::String (i));
        if (i >= (int) channels.size())
        {
            k.setBinding ({});
            continue;
        }
        auto& p = channels[(size_t) i]->params;
        const int in = p.inputLeft.load();
        auto* src = in >= 0 && in < 128 ? ctx.engine.getSource (in) : nullptr;
        if (src == nullptr)
        {
            k.setBinding (bindLinear ("GAIN", p.input.trimDb, -24.0f, 24.0f, " dB", 1, 0.0f));
            continue;
        }
        if (i >= (int) pad.size())
        {
            k.setBinding (bindLinear ("GAIN", src->gainDb, -10.0f, 20.0f, " dB", 0, 0.0f));
            continue;
        }
        pad[(size_t) i] = src->gainDb.load() < 16.0f;
        KnobBinding b;
        b.label = "GAIN";
        const auto offset = [this, i] { return pad[(size_t) i] ? 26.0f : 0.0f; };
        b.get = [src, offset] { return juce::jlimit (0.0f, 1.0f, (src->gainDb.load() + offset() - 16.0f) / 44.0f); };
        b.set = [src, offset] (float v) { src->gainDb = 16.0f + 44.0f * v - offset(); };
        b.text = [src] { return juce::String (src->gainDb.load(), 0) + " dB"; };
        k.setBinding (b);
    }
}

void Y01VSurface::pressSolo (Strip* s)
{
    // SETUP page 2 (p.80): SOLO's DISABLE, and LAST SOLO's one channel at a time.
    if (s == nullptr || ! display.isSoloEnabled())
        return;
    const bool on = ! s->params.solo.load();
    if (on && display.isLastSolo())
        for (auto* list : { &ctx.engine.getChannels(), &ctx.engine.getAux() })
            for (const auto& other : *list)
                other->params.solo = false;
    s->params.solo = on;
}

void Y01VSurface::pressTwoTrack()
{
    const auto& channels = ctx.engine.getChannels();
    if (13 >= (int) channels.size())
        return;
    auto& p = channels[13]->params;
    const int first = p.inputLeft.load() == y01v::kTwoTrackInput ? 14 : y01v::kTwoTrackInput;
    p.inputLeft = first;
    p.inputRight = first + 1;
}

void Y01VSurface::pressPad (int i)
{
    const auto& channels = ctx.engine.getChannels();
    if (! juce::isPositiveAndBelow (i, (int) juce::jmin (channels.size(), pad.size())))
        return;
    const int in = channels[(size_t) i]->params.inputLeft.load();
    if (auto* src = in >= 0 && in < 128 ? ctx.engine.getSource (in) : nullptr)
    {
        pad[(size_t) i] = ! pad[(size_t) i];
        src->gainDb = src->gainDb.load() + (pad[(size_t) i] ? -26.0f : 26.0f);
    }
}

void Y01VSurface::bindKnobs()
{
    auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    boundStrip = s;
    KnobBinding pan, freq, gain;
    if (s != nullptr)
    {
        auto& eq = s->params.eq();
        const auto table = s->isOutput() ? models::BusEq : models::ChannelEq;
        const char* prefixes[] = { "l", "lm", "hm", "h" };
        const juce::String pre (prefixes[eqBand]);
        pan = y01v::bindPan01V ("PAN", s->params.pan);
        freq = bindParam (table, eq.model.load(), eq.values, pre + "f", "F");
        gain = bindParam (table, eq.model.load(), eq.values, pre + "g", "G");
    }
    display.setEqBand (eqBand);
    display.setSelectedBindings (pan, freq, gain);
    using y01v::Function;
    // Moving one of these brings up its page, as the preferences say (p.203).
    const auto autoScreen = [this] (KnobBinding b, const char* preference, Function f)
    {
        if (b.isValid())
            b.set = [this, set = b.set, preference, f] (float v)
            {
                set (v);
                if (display.preference (preference))
                    display.showFirstPage (f);
            };
        return b;
    };
    getKnob ("PAN").setBinding (autoScreen (pan, "y01vPanAutoScreen", Function::PanRouting));
    getKnob ("F").setBinding (autoScreen (freq, "y01vEqAutoScreen", Function::EqAtt));
    getKnob ("G").setBinding (autoScreen (gain, "y01vEqAutoScreen", Function::EqAtt));
}

void Y01VSurface::refreshAll()
{
    // CH 17-24 reach AUX 1-2 and the effects only (p.264): nothing of theirs on AUX 3-4.
    const auto& channels = ctx.engine.getChannels();
    for (int i = kOptionChannel; i < (int) channels.size(); ++i)
        for (int bus : { kAuxBus + 2, kAuxBus + 3 })
            if (bus < (int) ctx.engine.getBuses().size())
                channels[(size_t) i]->sends[(size_t) bus].levelDb = fader::kMinDb;
    display.relink();
    bindStrips();
    bindInputs();
    bindKnobs();
    display.rebuild();
    repaint();
}

void Y01VSurface::selectionChanged()
{
    bindKnobs();
    display.rebuild();
}

void Y01VSurface::timerCallback()
{
    if (isCovered())
        return;
    if (boundStrip != (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr))
        selectionChanged();
    display.tickLinks();
    for (auto& f : faders)
        f->refresh();
    masterFader->refresh();
    for (auto& t : tapes)
        t->refresh();
    for (auto& [name, k] : keys)
        k->refresh();
    for (auto& [name, k] : knobs)
        k->refresh();
    universalKey.refresh();
    meter.refresh();
    display.refresh();

    // The panel's SOLO light: anything soloed.
    bool anySolo = false;
    for (auto kind : { &ctx.engine.getChannels(), &ctx.engine.getAux() })
        for (const auto& s : *kind)
            anySolo = anySolo || s->params.solo.load();
    if (anySolo != soloLit)
    {
        soloLit = anySolo;
        repaint();
    }
}

void Y01VSurface::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    const auto scale = juce::jmin ((float) getWidth() / kDesignWidth, (float) getHeight() / kDesignHeight);
    design = juce::AffineTransform::scale (scale).translated (((float) getWidth() - scale * kDesignWidth) * 0.5f,
                                                              ((float) getHeight() - scale * kDesignHeight) * 0.5f);

    universalKey.setBounds (1806, 14, 104, 28);
    display.setBounds (kLcdX, kLcdY, y01v::Display::kWidth, y01v::Display::kHeight);
    meter.setBounds (1730, 186, 130, 184);
    wheel.setBounds (kRightCx - 65, 716, 130, 130);
    for (int i = 0; i < kStrips; ++i)
        faders[(size_t) i]->setBounds (stripCx (i) - 44, kFaderY, 88, kFaderBottom - kFaderY);
    masterFader->setBounds (kMasterCx - 44, kFaderY, 88, kFaderBottom - kFaderY);
    for (int i = 0; i <= kStrips; ++i) // the tape under each fader, on the bare panel below it
        tapes[(size_t) i]->setBounds ((i < kStrips ? stripCx (i) : kMasterCx) - 42, kFaderBottom + 6, 84, 30);

    for (auto* child : getChildren())
        child->setTransform (design);
}

void Y01VSurface::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    g.addTransform (design);

    const auto silk = [&g] (const juce::String& s, float x, float y, float w, float h, float size, juce::Colour c = kNavy, juce::Justification j = juce::Justification::centred, bool bold = false)
    {
        g.setColour (c);
        g.setFont (type::font (size, bold));
        g.drawText (s, juce::Rectangle<float> (x, y, w, h), j, false);
    };
    /** Printed white on a navy block (SOLO's, PARAMETER's). */
    const auto inverse = [&] (const juce::String& s, float x, float y, float w, float h, float size)
    {
        g.setColour (kNavy);
        g.fillRect (x, y, w, h);
        silk (s, x, y, w, h, size, kWhiteSilk, juce::Justification::centred, true);
    };

    // The panel.
    const auto body = juce::Rectangle<int> (0, kTopBar, kDesignWidth, kDesignHeight - kTopBar).toFloat();
    g.setGradientFill (juce::ColourGradient (kPanelTop, 0.0f, body.getY(), kPanelBottom, 0.0f, body.getBottom(), false));
    g.fillRect (body);

    // The input row.
    g.setColour (kNavy.withAlpha (0.8f));
    g.fillRect (20.0f, 152.0f, (float) kDesignWidth - 40.0f, 1.0f);
    for (int i = 0; i < kStrips; ++i)
    {
        const auto cx = (float) stripCx (i);
        const auto knobCx = cx - 10.0f;
        const juce::String name = i < 12 ? juce::String (i + 1) : juce::String (13 + 2 * (i - 12)) + "/" + juce::String (14 + 2 * (i - 12));

        // The ticks around the GAIN knob, its printed range and the two PAD ranges.
        g.setColour (kNavy);
        for (int t = 0; t < 11; ++t)
        {
            const float a = juce::degreesToRadians (-135.0f + (float) t * 27.0f);
            const juce::Point<float> c (knobCx, 87.0f);
            g.drawLine (juce::Line<float> (c.getPointOnCircumference (25.0f, a), c.getPointOnCircumference (29.0f, a)), 1.2f);
        }
        if (i < 12)
        {
            silk ("-16", knobCx - 38.0f, 101.0f, 22.0f, 11.0f, 9.5f);
            silk ("-60", knobCx + 16.0f, 101.0f, 22.0f, 11.0f, 9.5f);
            inverse ("+10", knobCx - 35.0f, 115.0f, 20.0f, 11.0f, 9.5f);
            silk ("GAIN", knobCx - 15.0f, 114.0f, 30.0f, 13.0f, 11.5f);
            inverse ("-34", knobCx + 15.0f, 115.0f, 20.0f, 11.0f, 9.5f);
            silk ("26dB", cx + 16.0f, 85.0f, 34.0f, 12.0f, 10.0f);
        }
        else
        {
            silk ("+10", knobCx - 38.0f, 101.0f, 22.0f, 11.0f, 9.5f);
            silk ("-20", knobCx + 16.0f, 101.0f, 22.0f, 11.0f, 9.5f);
            silk ("GAIN", knobCx - 25.0f, 114.0f, 50.0f, 13.0f, 11.5f);
            if (i == 13)
                silk ("2TR IN", cx + 14.0f, 85.0f, 38.0f, 12.0f, 9.5f);
        }
        silk (name, knobCx - 30.0f, 124.0f, 60.0f, 27.0f, i < 12 ? 21.0f : 15.0f);
        g.setColour (kNavy.withAlpha (0.7f));
        g.fillRect (cx + (float) kStripPitch * 0.5f, 62.0f, 1.0f, 86.0f);
    }
    silk ("LEVEL", (float) kMasterCx - 45.0f, 116.0f, 90.0f, 14.0f, 12.0f);
    silk ("MONITOR OUT", (float) kMasterCx - 60.0f, 130.0f, 120.0f, 16.0f, 13.0f);
    silk ("LEVEL", (float) kReturnCx - 45.0f, 116.0f, 90.0f, 14.0f, 12.0f);
    silk ("PHONES", (float) kReturnCx - 60.0f, 130.0f, 120.0f, 16.0f, 13.0f);

    // The fader mode panel: it stops halfway down the AUX keys and, on the left, HOME's.
    {
        juce::Path p;
        p.startNewSubPath (36.0f, 266.0f);
        p.lineTo (362.0f, 266.0f);
        p.lineTo (362.0f, 363.0f);
        p.lineTo (118.0f, 363.0f);
        p.lineTo (118.0f, 415.0f);
        p.lineTo (36.0f, 415.0f);
        p.closeSubPath();
        g.setColour (kNavy);
        g.fillPath (p.createPathWithRoundedCorners (5.0f));
    }

    // The function keys' names, the fader mode keys' names.
    const char* row0[] = { "UTILITY", "MIDI", "SETUP", "VIEW" };
    const char* row1[] = { "DYNAMICS", "EQ/ATT", "\xc3\x98/DELAY", nullptr };
    const char* row2[] = { "EFFECT 1", "EFFECT 2", "OPTION I/O", "REMOTE" };
    const char* row3[] = { "AUX 1", "AUX 2", "AUX 3", "AUX 4" };
    for (int i = 0; i < 4; ++i)
    {
        const float x = 36.0f + (float) i * 78.0f;
        silk (row0[i], x, 153.0f, 86.0f, 14.0f, 13.0f);
        if (row1[i] != nullptr)
            silk (juce::String (juce::CharPointer_UTF8 (row1[i])), x, 212.0f, 86.0f, 14.0f, 13.0f);
        silk (row2[i], x, 281.0f, 86.0f, 14.0f, 13.0f, kWhiteSilk);
        silk (row3[i], x, 331.0f, 86.0f, 14.0f, 13.0f, kWhiteSilk);
    }
    // PAN/ROUTING is printed on two lines.
    silk ("PAN/", 36.0f + 3.0f * 78.0f, 202.0f, 86.0f, 12.0f, 12.0f);
    silk ("ROUTING", 36.0f + 3.0f * 78.0f, 213.0f, 86.0f, 13.0f, 12.0f);
    silk ("FADER MODE", 36.0f, 267.0f, 326.0f, 15.0f, 14.0f, kWhiteSilk, juce::Justification::centred, true);
    silk ("HOME", 40.0f, 384.0f, 76.0f, 14.0f, 13.0f, kWhiteSilk);

    // The charcoal plate: the LCD, SELECTED CHANNEL and the meter.
    {
        juce::Path p;
        p.startNewSubPath (372.0f, (float) kBandTop);
        p.lineTo ((float) kDesignWidth - 10.0f, (float) kBandTop);
        p.lineTo ((float) kDesignWidth - 10.0f, (float) kBandBottom);
        p.lineTo (396.0f, (float) kBandBottom);
        p.closeSubPath();
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillPath (p.createPathWithRoundedCorners (14.0f), juce::AffineTransform::translation (0.0f, 4.0f));
        g.setColour (kPlate);
        g.fillPath (p.createPathWithRoundedCorners (14.0f));
    }
    // The LCD's black surround, with the marks printed on it.
    g.setColour (juce::Colour (0xff0f0f10));
    g.fillRoundedRectangle (436.0f, 160.0f, 1022.0f, 270.0f, 3.0f);
    const auto lcdY = [] (int dotRow) { return (float) kLcdY + (float) dotRow * (float) y01v::Display::kDot; };
    const auto lcdX = [] (int dotColumn) { return (float) kLcdX + (float) dotColumn * (float) y01v::Display::kDot; };
    silk ("FUNCTION", 374.0f, lcdY (18) - 7.0f, 60.0f, 14.0f, 10.0f, kWhiteSilk, juce::Justification::centredRight);
    silk ("MEMORY", 374.0f, lcdY (42) - 7.0f, 60.0f, 14.0f, 10.0f, kWhiteSilk, juce::Justification::centredRight);
    g.setColour (kBlueSilk);
    for (int row : { 18, 42 })
        g.fillEllipse (440.0f, lcdY (row) - 3.0f, 6.0f, 6.0f);
    for (int column : { 13, 41 })
        g.fillEllipse (lcdX (column) - 1.5f, 411.0f, 6.0f, 6.0f);
    silk ("1", lcdX (13) - 8.5f, 417.0f, 20.0f, 12.0f, 11.0f, kBlueSilk);
    silk ("RETURN", lcdX (13) + 11.5f, 417.0f, lcdX (41) - lcdX (13) - 20.0f, 12.0f, 11.0f, kBlueSilk);
    silk ("2", lcdX (41) - 8.5f, 417.0f, 20.0f, 12.0f, 11.0f, kBlueSilk);
    silk ("PAN", 1415.0f, lcdY (7) - 7.0f, 40.0f, 14.0f, 11.0f, kBlueSilk, juce::Justification::centredLeft);
    silk ("F", 1415.0f, lcdY (30) - 7.0f, 34.0f, 14.0f, 11.0f, kBlueSilk, juce::Justification::centredLeft);
    silk ("G", 1415.0f, lcdY (52) - 7.0f, 34.0f, 14.0f, 11.0f, kBlueSilk, juce::Justification::centredLeft);

    // SELECTED CHANNEL.
    {
        const juce::Rectangle<float> section (1466.0f, 168.0f, 238.0f, 254.0f);
        g.setColour (kPlateLine);
        g.drawRoundedRectangle (section, 5.0f, 2.0f);
        // The printed arc around a knob, an arrowhead at each end pointing on round it.
        const auto arc = [&] (float cx, float cy, float from, float to)
        {
            const float radius = 33.0f;
            juce::Path path;
            path.addCentredArc (cx, cy, radius, radius, 0.0f, juce::degreesToRadians (from), juce::degreesToRadians (to), true);
            g.setColour (kWhiteSilk);
            g.strokePath (path, juce::PathStrokeType (1.5f));
            for (int end = 0; end < 2; ++end)
            {
                const float a = juce::degreesToRadians (end == 0 ? from : to);
                const float direction = end == 0 ? -1.0f : 1.0f;
                const juce::Point<float> at (cx + radius * std::sin (a), cy - radius * std::cos (a));
                const juce::Point<float> tangent (std::cos (a) * direction, std::sin (a) * direction);
                const juce::Point<float> normal (-tangent.y, tangent.x);
                juce::Path head;
                head.addTriangle (at + tangent * 7.0f, at + normal * 3.5f, at - normal * 3.5f);
                g.fillPath (head);
            }
        };
        const float knobCx = 1482.0f + 27.0f;
        arc (knobCx, 214.0f, -100.0f, 100.0f);
        g.setColour (kWhiteSilk);
        g.fillEllipse (knobCx - 2.0f, 214.0f - 44.0f, 4.0f, 4.0f);
        arc (knobCx, 296.0f, 25.0f, 155.0f);
        arc (knobCx, 378.0f, 25.0f, 155.0f);
        silk ("PAN", 1548.0f, 207.0f, 40.0f, 18.0f, 14.0f, kWhiteSilk, juce::Justification::centredLeft, true);
        silk ("F", 1548.0f, 268.0f, 20.0f, 18.0f, 14.0f, kWhiteSilk, juce::Justification::centredLeft, true);
        silk ("G", 1548.0f, 350.0f, 20.0f, 18.0f, 14.0f, kWhiteSilk, juce::Justification::centredLeft, true);
        silk ("SELECTED CHANNEL", 1504.0f, 406.0f, 180.0f, 14.0f, 10.5f, juce::Colour (0xff1e1f22));
        g.setColour (kWhiteSilk);
        g.fillRect (1574.0f, 176.0f, 32.0f, 15.0f);
        silk ("EQ", 1574.0f, 176.0f, 32.0f, 15.0f, 12.0f, kPlate, juce::Justification::centred, true);
        const char* bandNames[] = { "HIGH", "HI-MID", "LO-MID", "LOW" };
        g.setColour (kPlateLine);
        g.fillRect (1580.0f, 211.0f, 1.5f, 174.0f);
        // F's and G's lines into the tree.
        g.fillRect (1552.0f, 295.25f, 28.0f, 1.5f);
        g.fillRect (1552.0f, 384.25f, 28.0f, 1.5f);
        for (int row = 0; row < 4; ++row)
        {
            const float keyY = 192.0f + (float) row * 58.0f;
            silk (bandNames[row], 1614.0f, keyY - 15.0f, 78.0f, 14.0f, 12.0f, kWhiteSilk, juce::Justification::centred, true);
            const float y = keyY + 19.0f;
            g.setColour (kPlateLine);
            g.fillRect (1580.0f, y - 0.75f, 16.0f, 1.5f);
            const bool on = eqBand == 3 - row;
            if (on)
            {
                g.setColour (juce::Colour (0xff4ef07a).withAlpha (0.3f));
                g.fillEllipse (1592.0f, y - 11.0f, 22.0f, 22.0f);
            }
            g.setColour (on ? juce::Colour (0xff4ef07a) : juce::Colour (0xff17603a));
            g.fillEllipse (1597.0f, y - 6.0f, 12.0f, 12.0f);
        }
    }

    // The meter's heading, the contrast mark.
    silk ("L  STEREO  R", 1730.0f, 167.0f, 130.0f, 16.0f, 13.0f, kWhiteSilk, juce::Justification::centred, true);
    {
        const juce::Rectangle<float> mark (1826.0f, 394.0f, 16.0f, 16.0f);
        g.setColour (kWhiteSilk);
        g.drawEllipse (mark, 1.5f);
        juce::Path half;
        half.addPieSegment (mark, 0.0f, juce::MathConstants<float>::pi, 0.0f);
        g.fillPath (half);
    }

    // The strips.
    const auto stripLegend = [&] (float cx, const juce::String& name, const juce::String& under, float underWidth = 30.0f)
    {
        g.setColour (kNavy);
        g.fillRect (cx - 44.0f, 1042.0f, 88.0f, 20.0f);
        silk (name, cx - 44.0f, 1042.0f, 88.0f, 20.0f, 16.0f, kWhiteSilk, juce::Justification::centred, true);
        g.setColour (kNavy);
        for (int line = 0; line < 3; ++line)
            g.fillRect (cx - 44.0f, 1065.0f + (float) line * 4.0f, 88.0f, 1.2f);
        if (under.isNotEmpty())
        {
            g.setColour (kPanelBottom);
            g.fillRect (cx - underWidth * 0.5f, 1064.0f, underWidth, 14.0f);
            silk (under, cx - underWidth * 0.5f, 1064.0f, underWidth, 14.0f, 12.0f);
        }
    };
    const auto hatch = [&] (float cx, float w)
    {
        g.setColour (kNavy.withAlpha (0.85f));
        for (int line = 0; line < 8; ++line)
            g.fillRect (cx - w * 0.5f, (float) kHatchY + (float) line * 4.8f, w, 1.3f);
    };
    for (int i = 0; i < kStrips; ++i)
    {
        const auto cx = (float) stripCx (i);
        const juce::String name = i < 12 ? juce::String (i + 1) : juce::String (13 + 2 * (i - 12)) + "/" + juce::String (14 + 2 * (i - 12));
        silk (name, cx - 45.0f, 436.0f, 90.0f, 28.0f, i < 12 ? 23.0f : 21.0f);
        if (i < 8)
        {
            g.setColour (kNavy);
            g.drawRect (cx - 26.0f, 464.0f, 52.0f, 16.0f, 1.0f);
            silk (juce::String (17 + i), cx - 26.0f, 464.0f, 52.0f, 16.0f, 12.5f);
        }
        silk ("SEL", cx - 40.0f, 481.0f, 80.0f, 14.0f, 14.0f);
        inverse ("SOLO", cx - 21.0f, 537.0f, 42.0f, 14.0f, 11.5f);
        silk ("ON", cx - 40.0f, 593.0f, 80.0f, 14.0f, 14.0f);
        g.setColour (kNavy.withAlpha (0.8f));
        g.fillRect (cx + (float) kStripPitch * 0.5f, 440.0f, 1.0f, 208.0f);
        hatch (cx, 84.0f);
        stripLegend (cx, name, i < 8 ? juce::String (17 + i) : juce::String());
    }
    {
        const auto cx = (float) kMasterCx;
        silk ("STEREO", cx - 50.0f, 438.0f, 100.0f, 22.0f, 18.0f);
        g.setColour (kNavy);
        g.drawRect (cx - 34.0f, 462.0f, 68.0f, 17.0f, 1.0f);
        silk ("MASTER", cx - 34.0f, 462.0f, 68.0f, 17.0f, 12.0f);
        silk ("SEL", cx - 40.0f, 481.0f, 80.0f, 14.0f, 14.0f);
        silk ("ON", cx - 40.0f, 593.0f, 80.0f, 14.0f, 14.0f);
        hatch (cx, 88.0f);
        stripLegend (cx, "STEREO", "MASTER", 58.0f);
    }

    // RETURN 1 and 2.
    {
        const float x1 = (float) kReturnCx - 45.0f, x2 = (float) kReturnCx + (float) kReturnPitch + 45.0f;
        g.setColour (kNavy);
        g.fillRect (x1, 445.0f, 1.2f, 30.0f);
        g.fillRect (x2, 445.0f, 1.2f, 30.0f);
        g.fillRect (x1, 445.0f, 26.0f, 1.2f);
        g.fillRect (x2 - 26.0f, 445.0f, 26.0f, 1.2f);
        g.fillRect (x1, 474.0f, 44.0f, 1.2f);
        g.fillRect (x2 - 44.0f, 474.0f, 44.0f, 1.2f);
        silk ("1 RETURN 2", x1, 467.0f, x2 - x1, 14.0f, 12.5f);
        for (int i = 0; i < 2; ++i)
        {
            const float cx = (float) (kReturnCx + i * kReturnPitch);
            silk ("SEL", cx - 40.0f, 481.0f, 80.0f, 14.0f, 14.0f);
            inverse ("SOLO", cx - 21.0f, 537.0f, 42.0f, 14.0f, 11.5f);
            silk ("ON", cx - 40.0f, 593.0f, 80.0f, 14.0f, 14.0f);
        }
        g.setColour (kNavy.withAlpha (0.8f));
        g.fillRect ((float) kReturnCx + (float) kReturnPitch * 0.5f, 500.0f, 1.0f, 148.0f);
        g.fillRect (x1, 650.0f, x2 - x1, 1.0f);
    }

    // The data entry section.
    {
        const auto cx = (float) kRightCx;
        inverse ("SOLO", cx - 30.0f, 470.0f, 60.0f, 18.0f, 13.0f);
        if (soloLit)
        {
            g.setColour (kLedRed.withAlpha (0.35f));
            g.fillEllipse (cx - 11.0f, 494.0f, 22.0f, 22.0f);
        }
        g.setColour (soloLit ? juce::Colour (0xffff3a3a) : juce::Colour (0xff8a1a26));
        g.fillEllipse (cx - 6.0f, 499.0f, 12.0f, 12.0f);
        g.setColour (kNavy);
        g.fillRect (cx - 0.6f, 514.0f, 1.2f, 18.0f);
        g.fillRect (1690.0f, 531.0f, cx - 1690.0f, 1.2f);

        silk ("MEMORY", cx - 50.0f, 566.0f, 100.0f, 15.0f, 13.0f);
        silk ("-1/DEC", 1686.0f, 652.0f, 62.0f, 14.0f, 12.0f);
        silk ("+1/INC", 1832.0f, 652.0f, 62.0f, 14.0f, 12.0f);
        // PARAMETER, its comb of lines fanning towards the wheel.
        inverse ("PARAMETER", cx - 41.0f, 660.0f, 82.0f, 17.0f, 11.0f);
        g.setColour (kNavy);
        for (int i = 0; i <= 20; ++i)
        {
            const float x = cx - 41.0f + (float) i * (82.0f / 20.0f);
            const float a = (x - cx) / 41.0f;
            g.fillRect (x, 678.0f, 1.3f, 16.0f + 20.0f * a * a);
        }
        g.fillRect (cx - 45.0f, 660.0f, 1.3f, 50.0f);
        g.fillRect (cx + 44.0f, 660.0f, 1.3f, 50.0f);

        // A dished recess for the wheel.
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        g.fillEllipse (cx - 72.0f, 709.0f, 144.0f, 144.0f);

        // CURSOR: four keys on a navy ellipse, white arrows between them.
        silk ("CURSOR", cx + 40.0f, 862.0f, 74.0f, 16.0f, 13.0f);
        g.setColour (kNavy);
        g.fillEllipse (cx - 112.0f, 886.0f, 224.0f, 90.0f);
        juce::Path arrows;
        arrows.addTriangle (cx - 32.0f, 931.0f, cx - 22.0f, 925.0f, cx - 22.0f, 937.0f);
        arrows.addTriangle (cx + 32.0f, 931.0f, cx + 22.0f, 925.0f, cx + 22.0f, 937.0f);
        arrows.addTriangle (cx, 922.0f, cx - 7.0f, 930.0f, cx + 7.0f, 930.0f);
        arrows.addTriangle (cx, 940.0f, cx - 7.0f, 932.0f, cx + 7.0f, 932.0f);
        g.setColour (kWhiteSilk);
        g.fillPath (arrows);
        silk ("ENTER", 1700.0f, 988.0f, 64.0f, 15.0f, 13.0f);
    }

    // The title bar.
    auto top = juce::Rectangle<int> (0, 0, kDesignWidth, kTopBar);
    g.setColour (theme.panel);
    g.fillRect (top);
    g.setColour (theme.text);
    g.setFont (type::font (type::kTitle, true));
    g.drawText ("01V", top.reduced (sizes::kGapLarge, 0), juce::Justification::centredLeft);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption));
    g.drawText ("Not affiliated with or endorsed by any console maker; product names are their owners' trademarks.",
                top.reduced (sizes::kGapLarge, 0).withTrimmedLeft (90).withTrimmedRight (200), juce::Justification::centredLeft);
}

} // namespace livedaw::ui
