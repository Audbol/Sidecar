// The PRO2's GUI screen: the status bar, the eight fast strips with the channel strip's detail area, and
// the Console Overview, Automation, Effects, Graphic EQ, Patching and VCA Groups screens. Sidecar's own
// drawing of what the Owner's Manual's pictures show (DOC02-DL2: pp.38-39, 121-124, 127-142, 143-146, 163,
// 239-262) and of the screen in the maker's press render; nothing of the console's software is used. The
// Effects screen puts the selected unit's controls beside the rack, where the console opens a window over it.

#include "ui/Pro2Surface.h"

#include "engine/MixerEngine.h"
#include "engine/Models.h"
#include "engine/Show.h"
#include "ui/MeterBank.h"

#include <cmath>

namespace livedaw::ui::pro2
{

namespace
{
    const juce::Colour kBack (0xff12163a), kStrip (0xff1a2058), kStripSel (0xff2836a2), kTile (0xff090b24), kLilac (0xff7c80d6), kText (0xfff2f4ff), kDim (0xff8d96c6), kCream (0xfff1eedf), kInk (0xff14182c);
    const juce::Colour kRed (0xffe0352e), kYellow (0xfff3cc24), kGreen (0xff2fb64e), kBlue (0xff3a8bea), kOrange (0xfff09a26), kPurple (0xff7a3fb4);

    juce::Colour textOn (juce::Colour c) { return c.getPerceivedBrightness() > 0.55f ? kInk : juce::Colours::white; }

    /** A strip kind's colour on the screen (p.38): inputs blue, aux returns dark green, aux sends light
        green, matrices and masters black. */
    juce::Colour kindColour (const Strip& s)
    {
        switch (s.getKind())
        {
            case StripKind::Channel: return kStrip;
            case StripKind::Aux:     return juce::Colour (0xff12402a);
            case StripKind::Bus:     return juce::Colour (0xff2c6a3a);
            case StripKind::Main:
            case StripKind::Matrix:  break;
        }
        return juce::Colour (0xff101116);
    }

    juce::Colour bandColour (const juce::String& id)
    {
        if (id == "t" || id == "5")
            return juce::Colour (0xff3d5cff);
        if (id == "h" || id == "4")
            return kGreen;
        if (id == "l" || id == "3")
            return kYellow;
        if (id == "2")
            return kOrange;
        if (id == "6")
            return juce::Colour (0xffe040d0);
        return kRed;
    }

    juce::String kindId (const Strip& s)
    {
        switch (s.getKind())
        {
            case StripKind::Channel: return "IN" + juce::String (s.getNumber());
            case StripKind::Aux:     return "AR" + juce::String (s.getNumber());
            case StripKind::Bus:     return "AS" + juce::String (s.getNumber());
            case StripKind::Matrix:  return "MX" + juce::String (s.getNumber());
            case StripKind::Main:    break;
        }
        return "MST";
    }

    /** Steps a choice round its list. */
    void cycle (const KnobBinding& b)
    {
        if (! b.isValid() || ! b.set || b.steps < 2.0f)
            return;
        const int n = juce::roundToInt (b.steps);
        b.set ((float) ((juce::roundToInt (b.get() * (float) (n - 1)) + 1) % n) / (float) (n - 1));
    }

    void toggle (std::atomic<bool>& v) { v = ! v.load(); }

    KnobBinding bindInt (std::function<int()> get, std::function<void (int)> set, int lo, int hi, std::function<juce::String (int)> text)
    {
        KnobBinding b;
        b.get = [get, lo, hi] { return hi > lo ? (float) (get() - lo) / (float) (hi - lo) : 0.0f; };
        b.set = [set, lo, hi] (float v) { set (lo + juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * (float) (hi - lo))); };
        b.text = [get, text] { return text (get()); };
        b.steps = (float) (hi - lo + 1);
        return b;
    }

    const char* const kPageNames[] = { "", "Console Overview", "Automation", "Effects", "Graphic EQs", "Patching", "VCA Groups" };
} // namespace

Screen::Screen (UiContext& c, Pro2Surface& s) : ctx (c), surface (s)
{
    setOpaque (true);
}

//==============================================================================
void Screen::showPage (Page p)
{
    if (p == Page::kCount)
        return;
    page = p;
    rebuild();
}

juce::String Screen::getTitle() const
{
    if (page != Page::Default)
        return kPageNames[(size_t) page];
    const int group = surface.getGroup();
    return group >= 0 && ! surface.isAreaB (group) ? surface.groupName (group) : surface.bankName (surface.getBank (Bay::Channel));
}

void Screen::rebuild()
{
    g = nullptr;
    render();
    repaint();
}

void Screen::refresh() { repaint(); }

void Screen::setFxUnit (int u)
{
    fxUnit = juce::jlimit (0, kFxUnits - 1, u);
    rebuild();
}

juce::Array<int> Screen::fxModels() const
{
    juce::Array<int> list;
    list.add (ld::kFxNone);
    const auto& t = models::table (models::Fx);
    for (int i = 0; i < (int) t.size(); ++i)
        if (t[(size_t) i].token.startsWith ("MP"))
            list.add (i);
    return list;
}

void Screen::setFxModel (int model)
{
    ctx.engine.getFx().setModel (fxUnit, model);
    rebuild();
}

void Screen::setCue (int c)
{
    cue = juce::jlimit (0, juce::jmax (0, ctx.show.getNumScenes()), c);
    rebuild();
}

void Screen::setPatchScroll (int first)
{
    patchScroll = juce::jlimit (0, juce::jmax (0, (int) ctx.engine.getChannels().size() - 28), first);
    rebuild();
}

int Screen::findSpot (const juce::String& name) const
{
    for (int i = (int) spots.size(); --i >= 0;)
        if (spots[(size_t) i].name == name)
            return i;
    return -1;
}

bool Screen::press (const juce::String& name)
{
    const int i = findSpot (name);
    if (i < 0 || ! spots[(size_t) i].press)
        return false;
    const auto f = spots[(size_t) i].press;
    f();
    return true;
}

void Screen::paint (juce::Graphics& graphics)
{
    g = &graphics;
    render();
    g = nullptr;
}

void Screen::mouseDown (const juce::MouseEvent& e)
{
    dragged = {};
    for (int i = (int) spots.size(); --i >= 0;)
    {
        const auto& s = spots[(size_t) i];
        if (! s.area.contains (e.position))
            continue;
        if (s.binding.isValid() && s.binding.set)
        {
            dragged = s.binding;
            draggedAbsolute = s.absolute;
            draggedArea = s.area;
            dragStart = dragged.get();
            if (draggedAbsolute)
                mouseDrag (e);
        }
        else if (s.press)
        {
            const auto f = s.press;
            f();
        }
        return;
    }
}

void Screen::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragged.isValid())
        return;
    const float v = draggedAbsolute ? (draggedArea.getBottom() - e.position.y) / juce::jmax (1.0f, draggedArea.getHeight())
                                    : dragStart - (float) e.getDistanceFromDragStartY() / 200.0f;
    dragged.set (juce::jlimit (0.0f, 1.0f, v));
    repaint();
}

void Screen::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    for (int i = (int) spots.size(); --i >= 0;)
    {
        const auto& s = spots[(size_t) i];
        if (s.area.contains (e.position) && s.binding.isValid() && s.binding.set)
        {
            const float step = s.binding.steps > 1.0f ? 1.0f / (s.binding.steps - 1.0f) : 0.01f;
            s.binding.set (juce::jlimit (0.0f, 1.0f, s.binding.get() + (w.deltaY > 0 ? step : -step)));
            repaint();
            return;
        }
    }
}

//==============================================================================
void Screen::fill (juce::Rectangle<float> r, juce::Colour c, float radius)
{
    if (g == nullptr)
        return;
    g->setColour (c);
    if (radius > 0.0f)
        g->fillRoundedRectangle (r, radius);
    else
        g->fillRect (r);
}

void Screen::frame (juce::Rectangle<float> r, juce::Colour c, float radius, float thickness)
{
    if (g == nullptr)
        return;
    g->setColour (c);
    if (radius > 0.0f)
        g->drawRoundedRectangle (r, radius, thickness);
    else
        g->drawRect (r, thickness);
}

void Screen::text (juce::Rectangle<float> r, const juce::String& s, juce::Colour c, float size, juce::Justification j, bool bold)
{
    if (g == nullptr || s.isEmpty())
        return;
    g->setColour (c);
    g->setFont (universal::type::font (size, bold));
    g->drawText (s, r, j, true);
}

Screen::Spot& Screen::spot (const juce::String& name, juce::Rectangle<float> r, std::function<void()> pressed, KnobBinding b)
{
    spots.push_back ({ name, r, std::move (pressed), std::move (b), false });
    return spots.back();
}

void Screen::button (juce::Rectangle<float> r, const juce::String& name, const juce::String& label, bool on, juce::Colour lit, std::function<void()> pressed)
{
    const auto c = on ? lit : lit.darker (0.62f).withMultipliedSaturation (0.55f);
    fill (r, c, 2.0f);
    frame (r, juce::Colours::black.withAlpha (0.55f), 2.0f);
    text (r, label, on ? textOn (c) : kText.withAlpha (0.75f), juce::jlimit (8.0f, 12.0f, r.getHeight() * 0.72f), juce::Justification::centred, true);
    if (pressed)
        spot (name, r, std::move (pressed));
}

void Screen::dial (juce::Rectangle<float> r, const juce::String& name, KnobBinding b, juce::Colour cap, bool legend)
{
    const bool valid = b.isValid();
    const float d = legend ? juce::jmin (r.getWidth() - 10.0f, r.getHeight() - 26.0f) : juce::jmin (r.getWidth(), r.getHeight());
    const auto knob = juce::Rectangle<float> (d, d).withCentre ({ r.getCentreX(), legend ? r.getY() + 3.0f + d * 0.5f : r.getCentreY() });
    if (g != nullptr)
    {
        const auto c = knob.getCentre();
        const float radius = d * 0.5f, v = valid ? b.get() : 0.5f;
        const float angle = (v - 0.5f) * juce::MathConstants<float>::pi * 5.0f / 3.0f;
        g->setColour (kText.withAlpha (valid ? 0.75f : 0.25f));
        for (int i = 0; i <= 10; ++i)
        {
            const float a = ((float) i / 10.0f - 0.5f) * juce::MathConstants<float>::pi * 5.0f / 3.0f;
            g->fillEllipse (juce::Rectangle<float> (2.2f, 2.2f).withCentre (c.getPointOnCircumference (radius - 1.2f, a)));
        }
        const float body = radius * 0.74f;
        const auto face = valid ? cap : cap.withMultipliedSaturation (0.3f).darker (0.5f);
        g->setGradientFill (juce::ColourGradient (face.brighter (0.35f), c.x - body * 0.5f, c.y - body * 0.7f, face.darker (0.45f), c.x + body * 0.5f, c.y + body * 0.8f, false));
        g->fillEllipse (juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (c));
        g->setColour (juce::Colours::white);
        g->drawLine (juce::Line<float> (c.getPointOnCircumference (body * 0.15f, angle), c.getPointOnCircumference (body * 0.95f, angle)), juce::jmax (1.6f, d * 0.07f));
    }
    if (legend)
    {
        text ({ r.getX(), knob.getBottom() + 1.0f, r.getWidth(), 11.0f }, b.label.toLowerCase(), valid ? kText : kDim, 9.5f);
        text ({ r.getX(), knob.getBottom() + 11.0f, r.getWidth(), 12.0f }, valid && b.text ? b.text() : juce::String(), kYellow, 10.0f, juce::Justification::centred, true);
    }
    if (valid)
        spot (name, r, {}, std::move (b));
}

void Screen::bar (juce::Rectangle<float> r, const juce::String& name, KnobBinding b, juce::Colour c)
{
    fill (r, juce::Colour (0xff04050f));
    if (! b.isValid())
        return;
    if (g != nullptr)
    {
        // A wedge that grows with the level, as the fast strips' bus sends are drawn.
        const float v = b.get();
        if (v > 0.001f)
        {
            juce::Path wedge;
            const float x1 = r.getX() + r.getWidth() * v, h = r.getHeight() - 2.0f;
            wedge.addTriangle (r.getX(), r.getBottom() - 1.0f, x1, r.getBottom() - 1.0f, x1, r.getBottom() - 1.0f - h * (0.35f + 0.65f * v));
            g->setColour (c);
            g->fillPath (wedge);
        }
    }
    spot (name, r, {}, std::move (b));
}

void Screen::slider (juce::Rectangle<float> r, const juce::String& name, KnobBinding b, juce::Colour c)
{
    if (g != nullptr)
    {
        g->setColour (juce::Colour (0xff03040c));
        g->fillRoundedRectangle (juce::Rectangle<float> (4.0f, r.getHeight()).withCentre (r.getCentre()), 2.0f);
        if (b.isValid())
        {
            const float y = r.getBottom() - r.getHeight() * b.get();
            const juce::Rectangle<float> cap (r.getX() + 1.0f, y - 7.0f, r.getWidth() - 2.0f, 14.0f);
            g->setColour (juce::Colour (0xff2a2c33));
            g->fillRoundedRectangle (cap, 3.0f);
            g->setColour (c);
            g->fillRect (cap.getX() + 2.0f, y - 1.5f, cap.getWidth() - 4.0f, 3.0f);
        }
    }
    if (b.isValid())
        spot (name, r.expanded (3.0f, 0.0f), {}, std::move (b)).absolute = true;
}

void Screen::meter (juce::Rectangle<float> r, float linear)
{
    if (g == nullptr)
        return;
    g->setColour (juce::Colour (0xff03040c));
    g->fillRect (r);
    const float db = juce::Decibels::gainToDecibels (linear, -90.0f);
    const int segments = juce::jmax (6, juce::roundToInt (r.getHeight() / 5.0f));
    const float h = r.getHeight() / (float) segments;
    for (int i = 0; i < segments; ++i)
    {
        const float at = -57.0f + 57.0f * (float) (i + 1) / (float) segments; // the segment's level, dBFS
        const auto c = at > -3.0f ? kRed : at > -12.0f ? kYellow : kGreen;
        g->setColour (db >= at ? c : c.darker (0.8f).withMultipliedSaturation (0.5f));
        g->fillRect (r.getX() + 1.0f, r.getBottom() - (float) (i + 1) * h + 0.6f, r.getWidth() - 2.0f, h - 1.2f);
    }
}

//==============================================================================
void Screen::eqGraph (Strip& s, juce::Rectangle<float> r, bool bands)
{
    fill (r, juce::Colour (0xff05061a));
    frame (r, kLilac.withAlpha (0.6f));
    if (g == nullptr)
        return;
    constexpr int n = 96;
    const auto xOf = [r] (float hz) { return r.getX() + r.getWidth() * std::log10 (hz / 20.0f) / 3.0f; };
    const auto yOf = [r] (float db) { return r.getCentreY() - juce::jlimit (-18.0f, 18.0f, db) / 18.0f * (r.getHeight() * 0.5f - 3.0f); };
    g->setColour (kLilac.withAlpha (0.25f));
    for (float hz : { 100.0f, 1000.0f, 10000.0f })
        g->fillRect (xOf (hz), r.getY(), 1.0f, r.getHeight());
    for (float db : { -12.0f, 0.0f, 12.0f })
        g->fillRect (r.getX(), yOf (db), r.getWidth(), 1.0f);

    auto& eq = s.params.eq();
    if (! eq.on.load())
    {
        text (r, "OFF", kText, juce::jmin (30.0f, r.getHeight() * 0.36f), juce::Justification::centred, true);
        return;
    }
    const auto table = s.isOutput() ? models::BusEq : models::ChannelEq;
    const auto& m = models::model (table, eq.model.load());
    std::vector<float> values (m.params.size());
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = eq.values[i].load();
    std::array<float, n> freqs {}, db {};
    for (int i = 0; i < n; ++i)
        freqs[(size_t) i] = 20.0f * std::pow (1000.0f, (float) i / (float) (n - 1));
    const auto curve = [&] (const std::vector<float>& v, juce::Path& p, bool closed)
    {
        if (! models::modelResponse (table, eq.model.load(), v.data(), (int) v.size(), freqs.data(), db.data(), n))
            return false;
        if (closed)
            p.startNewSubPath (r.getX(), yOf (0.0f));
        for (int i = 0; i < n; ++i)
        {
            const juce::Point<float> at (xOf (freqs[(size_t) i]), yOf (db[(size_t) i]));
            if (i == 0 && ! closed)
                p.startNewSubPath (at);
            else
                p.lineTo (at);
        }
        if (closed)
        {
            p.lineTo (r.getRight(), yOf (0.0f));
            p.closeSubPath();
        }
        return true;
    };
    const auto ids = eqBands (s);
    if (bands)
        for (const auto& id : ids)
        {
            // The band alone: the others' gains at zero.
            auto alone = values;
            const int own = m.find (id + "g");
            if (own < 0 || std::abs (values[(size_t) own]) < 0.05f)
                continue;
            for (const auto& other : ids)
                if (const int gi = m.find (other + "g"); gi >= 0 && other != id)
                    alone[(size_t) gi] = 0.0f;
            juce::Path p;
            if (curve (alone, p, true))
            {
                g->setColour (bandColour (id).withAlpha (0.6f));
                g->fillPath (p);
            }
        }
    juce::Path whole;
    if (curve (values, whole, false))
    {
        g->setColour (kText);
        g->strokePath (whole, juce::PathStrokeType (1.6f));
    }
}

void Screen::dynGraph (Strip& s, bool gate, juce::Rectangle<float> r)
{
    auto& d = gate ? s.params.gate() : s.params.dynamics();
    const bool on = d.on.load();
    fill (r, on ? juce::Colour (0xff1a1f8c) : juce::Colour (0xff6f73cc));
    if (g == nullptr)
        return;
    // The transfer curve over a dotted grid, the area under it shaded (p.252).
    g->setColour (kText.withAlpha (0.18f));
    for (int i = 1; i < 6; ++i)
    {
        g->fillRect (r.getX() + r.getWidth() * (float) i / 6.0f, r.getY(), 1.0f, r.getHeight());
        g->fillRect (r.getX(), r.getY() + r.getHeight() * (float) i / 6.0f, r.getWidth(), 1.0f);
    }
    const auto& m = models::model (models::Dynamics, d.model.load());
    std::vector<float> values (m.params.size());
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = d.values[i].load();
    constexpr int n = 48;
    constexpr float lo = -72.0f;
    std::array<float, n> in {}, out {};
    for (int i = 0; i < n; ++i)
        in[(size_t) i] = lo - lo * (float) i / (float) (n - 1);
    if (! models::dynStaticCurve (d.model.load(), values.data(), (int) values.size(), in.data(), out.data(), n))
        return;
    juce::Path line, area;
    area.startNewSubPath (r.getBottomLeft());
    for (int i = 0; i < n; ++i)
    {
        const juce::Point<float> at (r.getX() + r.getWidth() * (float) i / (float) (n - 1),
                                     r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (out[(size_t) i] - lo) / -lo));
        if (i == 0)
            line.startNewSubPath (at);
        else
            line.lineTo (at);
        area.lineTo (at);
    }
    area.lineTo (r.getBottomRight());
    area.closeSubPath();
    g->setColour ((on ? kLilac : juce::Colour (0xff4a4fb0)).withAlpha (0.55f));
    g->fillPath (area);
    g->setColour (gate ? kGreen : kRed);
    g->strokePath (line, juce::PathStrokeType (2.0f));
    // The signal on the curve.
    const float key = (gate ? s.meters.gateKeyDb : s.meters.dynKeyDb).load();
    if (on && key > lo)
    {
        const float x = r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, (key - lo) / -lo);
        g->setColour (kYellow);
        g->fillRect (x - 0.75f, r.getY(), 1.5f, r.getHeight());
    }
}

void Screen::filterGraph (Strip& s, juce::Rectangle<float> r)
{
    fill (r, juce::Colour (0xff05061a));
    frame (r, kLilac.withAlpha (0.6f));
    if (g == nullptr || s.getKind() != StripKind::Channel)
        return;
    constexpr int n = 64;
    std::array<double, n> freqs {}, db {};
    for (int i = 0; i < n; ++i)
        freqs[(size_t) i] = 10.0 * std::pow (2400.0, (double) i / (double) (n - 1));
    ld::ld_strip_response (s.getHandle(), 3, freqs.data(), db.data(), n, 48000.0);
    juce::Path p;
    for (int i = 0; i < n; ++i)
    {
        const juce::Point<float> at (r.getX() + r.getWidth() * (float) i / (float) (n - 1),
                                     r.getY() + 6.0f - (float) juce::jlimit (-36.0, 3.0, db[(size_t) i]) / 36.0f * (r.getHeight() - 10.0f));
        if (i == 0)
            p.startNewSubPath (at);
        else
            p.lineTo (at);
    }
    g->setColour (kText);
    g->strokePath (p, juce::PathStrokeType (1.6f));
}

void Screen::dialGrid (juce::Rectangle<float> r, std::vector<KnobBinding> list, int columns, float rowHeight, juce::Colour cap)
{
    const float w = r.getWidth() / (float) columns;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        auto& b = list[(size_t) i];
        const auto name = b.label.toLowerCase();
        dial ({ r.getX() + w * (float) (i % columns), r.getY() + rowHeight * (float) (i / columns), w, rowHeight }, name, std::move (b), cap);
    }
}

void Screen::select (Strip& s, Area a)
{
    if (surface.selected() != &s)
        surface.pressSelect (&s);
    surface.setArea (a);
}

//==============================================================================
void Screen::render()
{
    spots.clear();
    fill ({ 0.0f, 0.0f, (float) kWidth, (float) kHeight }, kBack);
    switch (page)
    {
        case Page::Default:    pageDefault(); break;
        case Page::Overview:   pageOverview(); break;
        case Page::Automation: pageAutomation(); break;
        case Page::Effects:    pageEffects(); break;
        case Page::Geq:        pageGeq(); break;
        case Page::Patching:   pagePatching(); break;
        case Page::Groups:     pageGroups(); break;
        case Page::kCount:     break;
    }
    statusBar();
}

void Screen::statusBar()
{
    // home, the screen's name, the current scene (p.39). Copy and paste are drawn only.
    const juce::Rectangle<float> status (0.0f, 0.0f, (float) kWidth, 44.0f);
    if (g != nullptr)
    {
        g->setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4d58), 0.0f, 0.0f, juce::Colour (0xff17181d), 0.0f, status.getBottom(), false));
        g->fillRect (status);
    }
    const juce::Rectangle<float> home (6.0f, 5.0f, 96.0f, 34.0f);
    fill (home, juce::Colour (0xff222a66), 3.0f);
    frame (home, kLilac, 3.0f);
    text (home, "home", kText, 15.0f, juce::Justification::centred, true);
    spot ("home", home, [this] { showPage (Page::Default); });
    for (int i = 0; i < 2; ++i)
    {
        const juce::Rectangle<float> nav (110.0f + (float) i * 30.0f, 10.0f, 26.0f, 24.0f);
        fill (nav, juce::Colour (0xff30333c), 12.0f);
        text (nav, i == 0 ? "<" : ">", kDim, 14.0f, juce::Justification::centred, true);
    }
    text ({ 180.0f, 0.0f, 400.0f, 44.0f }, getTitle(), kText, 25.0f, juce::Justification::centredLeft, true);

    const juce::Rectangle<float> scene (600.0f, 7.0f, 230.0f, 30.0f);
    fill (scene, juce::Colour (0xff06070c), 2.0f);
    const int current = ctx.show.getCurrentSceneIndex();
    const auto now = current >= 0 ? ctx.show.getScene (current) : std::optional<Show::Scene>();
    if (surface.isStorePending())
        text (scene, "Store Scene?  ok / cancel", kRed.brighter (0.3f), 13.0f, juce::Justification::centred, true);
    else
        text (scene.reduced (8.0f, 0.0f), now ? juce::String (current + 1) + "  " + now->name : juce::String ("no scene"), now ? kYellow : kDim, 14.0f, juce::Justification::centredLeft, true);
    for (int i = 0; i < 2; ++i)
    {
        const juce::Rectangle<float> r (850.0f + (float) i * 84.0f, 9.0f, 78.0f, 26.0f);
        fill (r, juce::Colour (0xff2c2f37), 3.0f);
        text (r, i == 0 ? "copy" : "paste", kDim, 12.0f);
    }
}

//==============================================================================
void Screen::fastStrip (Strip& s, juce::Rectangle<float> r, int index)
{
    const auto tag = juce::String (index + 1) + " ";
    const bool isSelected = &s == surface.selected();
    const auto area = surface.getArea();
    auto* strip = &s;
    auto& p = s.params;
    const bool channel = s.getKind() == StripKind::Channel, dyn = s.getKind() != StripKind::Aux;
    fill (r, isSelected ? kStripSel : kindColour (s));
    float y = r.getY();
    const auto take = [&y, r] (float h)
    {
        const juce::Rectangle<float> a (r.getX() + 2.0f, y + 1.0f, r.getWidth() - 4.0f, h - 2.0f);
        y += h;
        return a;
    };
    const auto section = [this, strip, tag, isSelected, area] (juce::Rectangle<float> a, const char* name, Area target, juce::Colour c)
    {
        fill (a, c, 2.0f);
        if (isSelected && area == target)
            frame (a, kYellow, 2.0f, 2.0f); // the cross-hair (p.39)
        spot (tag + name, a, [this, strip, target] { select (*strip, target); });
    };

    // Console gain / digital trim: 48V, the trim, phase (pp.239, 245).
    {
        const auto a = take (92.0f);
        section (a, "config", Area::Config, kTile);
        auto* src = s.isInput() && s.getInputLeft() >= 0 ? ctx.engine.getSource (MixerEngine::sourceIndex (s.getInputLeft())) : nullptr;
        if (src != nullptr)
            button ({ a.getX() + 3.0f, a.getY() + 3.0f, 28.0f, 12.0f }, tag + "48V", "48V", src->phantom.load(), kRed, [src] { toggle (src->phantom); });
        dial ({ a.getCentreX() - 24.0f, a.getY() + 16.0f, 48.0f, 48.0f }, tag + "trim", bindTrim (s), juce::Colour (0xff8a8f9c), false);
        text ({ a.getX(), a.getY() + 62.0f, a.getWidth(), 11.0f }, juce::String (p.input.trimDb.load(), 1) + " dB", kDim, 9.5f);
        button ({ a.getX() + 3.0f, a.getBottom() - 15.0f, 30.0f, 12.0f }, tag + "phase", juce::String::charToString ((juce::juce_wchar) 0xd8), p.input.invert.load(), kBlue, [strip] { toggle (strip->params.input.invert); });
    }
    // dir out: nothing to switch yet.
    {
        const auto a = take (20.0f);
        fill (a, kTile, 2.0f);
        text (a.reduced (5.0f, 0.0f), "dir out", kDim.withAlpha (0.6f), 10.0f, juce::Justification::centredRight);
    }
    // The compressor and the gate: ON, the threshold, the graph (p.239).
    for (int isGate = 0; isGate < 2; ++isGate)
    {
        const auto a = take (62.0f);
        const bool has = isGate == 1 ? channel : dyn;
        section (a, isGate == 1 ? "gate" : "comp", isGate == 1 ? Area::Gate : Area::Comp, has ? kLilac : kTile);
        if (! has)
            continue;
        auto& d = isGate == 1 ? p.gate() : p.dynamics();
        button ({ a.getX() + 3.0f, a.getY() + 3.0f, 30.0f, 12.0f }, tag + (isGate == 1 ? "gate ON" : "comp ON"), isGate == 1 ? "GTE" : "CMP", d.on.load(), kBlue, [&d] { toggle (d.on); });
        dial ({ a.getX() + 2.0f, a.getY() + 18.0f, 38.0f, 38.0f }, tag + (isGate == 1 ? "gate threshold" : "comp threshold"), bindDyn (s, isGate == 1, "thr"), kPurple, false);
        dynGraph (s, isGate == 1, { a.getX() + 43.0f, a.getY() + 3.0f, a.getWidth() - 46.0f, a.getHeight() - 6.0f });
    }
    // insert, the name, eq.
    {
        const auto a = take (20.0f);
        section (a, "insert", Area::Insert, kTile);
        button ({ a.getX() + 3.0f, a.getY() + 3.0f, 30.0f, 12.0f }, tag + "INS", "INS", p.preInsert().on.load(), kBlue, [strip] { toggle (strip->params.preInsert().on); });
        text (a.reduced (5.0f, 0.0f), "insert", kDim, 10.0f, juce::Justification::centredRight);
    }
    {
        const auto a = take (30.0f);
        const auto c = stripDisplayColour (s);
        fill (a, c);
        text (a, s.getName(), textOn (c), 15.0f, juce::Justification::centred, true);
        spot (tag + "name", a, [this, strip] { select (*strip, Area::Overview); });
    }
    {
        const auto a = take (22.0f);
        section (a, "eq", Area::Eq, juce::Colour (0xff4a4d58));
        button ({ a.getX() + 3.0f, a.getY() + 3.0f, 30.0f, 14.0f }, tag + "EQ", "EQ", p.eq().on.load(), kRed, [strip] { toggle (strip->params.eq().on); });
        text (a.reduced (5.0f, 0.0f), "eq", kText, 12.0f, juce::Justification::centredRight, true);
    }
    // The bus sends: the mix section's eight (p.260).
    {
        const auto a = take (172.0f);
        const int bank = surface.getMixBank();
        section (a, "mixes", bank == 2 ? Area::Matrix : Area::Aux, kTile);
        text ({ a.getX(), a.getY() + 1.0f, a.getWidth(), 13.0f }, bank == 0 ? "Auxes 1-8" : bank == 1 ? "Auxes 9-16" : "Matrix 1-8", kText, 10.0f, juce::Justification::centred, true);
        static const juce::Colour pairs[] = { kRed, kGreen, kYellow, juce::Colour (0xff35b8ee) };
        for (int k = 0; k < 8; ++k)
        {
            auto* mix = bank < 2 ? ctx.engine.getBusInSlot (bank * 8 + k) : k < (int) ctx.engine.getMatrices().size() ? ctx.engine.getMatrices()[(size_t) k].get() : nullptr;
            const auto send = mix != nullptr ? sendTo (s, *mix) : MixSend {};
            const juce::Rectangle<float> row (a.getX() + 2.0f, a.getY() + 16.0f + (float) k * 19.0f, a.getWidth() - 4.0f, 17.0f);
            const auto c = pairs[k / 2];
            text (row.withWidth (11.0f), juce::String (bank == 1 ? k + 9 : k + 1), c, 9.0f, juce::Justification::centred, true);
            if (! send.isValid())
            {
                fill (row.withTrimmedLeft (12.0f), juce::Colour (0xff04050f));
                continue;
            }
            const bool on = send.on->load();
            if (send.isGroup())
                fill (row.withTrimmedLeft (12.0f).withTrimmedRight (22.0f), on ? c.withAlpha (0.8f) : juce::Colour (0xff04050f));
            else
                bar (row.withTrimmedLeft (12.0f).withTrimmedRight (22.0f), tag + "mix " + juce::String (k + 1), bindSendLevel (mix->getName(), *send.level), on ? c : c.darker (0.7f));
            button (row.withLeft (row.getRight() - 20.0f), tag + "mix ON " + juce::String (k + 1), send.isGroup() ? "" : send.isPre() ? "pr" : "", on, c, [send] { toggle (*send.on); });
        }
    }
    // The masters: ST, MON, the mono level and pan, MUTE, SOLO, the fader and its meter (pp.261-263).
    {
        const juce::Rectangle<float> a (r.getX() + 2.0f, y + 1.0f, r.getWidth() - 4.0f, r.getBottom() - y - 2.0f);
        section (a, "masters", Area::Masters, isSelected ? kStripSel.darker (0.2f) : kTile);
        const bool toMasters = s.getKind() != StripKind::Main && s.getKind() != StripKind::Matrix;
        button ({ a.getX() + 3.0f, a.getY() + 4.0f, 28.0f, 13.0f }, {}, "SIS", false, kOrange, {});
        if (toMasters)
        {
            button ({ a.getRight() - 33.0f, a.getY() + 4.0f, 30.0f, 13.0f }, tag + "ST", "ST", s.mains[0].on.load(), kRed, [strip] { toggle (strip->mains[0].on); });
            if (ctx.engine.getMains().size() > 1)
            {
                button ({ a.getRight() - 37.0f, a.getY() + 34.0f, 34.0f, 13.0f }, tag + "MON", "MON", s.mains[1].on.load(), kYellow, [strip] { toggle (strip->mains[1].on); });
                dial ({ a.getX() + 2.0f, a.getY() + 22.0f, 38.0f, 38.0f }, tag + "mono level", bindSendLevel ("MONO", s.mains[1].levelDb), kOrange, false);
            }
            dial ({ a.getX() + 2.0f, a.getY() + 64.0f, 46.0f, 46.0f }, tag + "pan", bindPan ("PAN", p.pan), kRed, false);
        }
        button ({ a.getX() + 4.0f, a.getY() + 122.0f, 42.0f, 20.0f }, tag + "MUTE", "MUTE", p.mute.load(), kRed, [strip] { toggle (strip->params.mute); });
        button ({ a.getX() + 4.0f, a.getY() + 150.0f, 42.0f, 14.0f }, {}, "B", false, juce::Colour (0xffb8bcc8), {});
        button ({ a.getX() + 4.0f, a.getY() + 172.0f, 42.0f, 22.0f }, tag + "SOLO", "SOLO", p.solo.load(), kYellow, [this, strip] { surface.pressSolo (*strip); });
        const juce::Rectangle<float> f (a.getRight() - 30.0f, a.getY() + 70.0f, 24.0f, a.getHeight() - 82.0f);
        meter ({ f.getX() - 12.0f, f.getY(), 8.0f, f.getHeight() }, juce::jmax (ctx.meters.get (&s, MeterPoint::Output, 0), ctx.meters.get (&s, MeterPoint::Output, 1)));
        slider (f, tag + "fader", bindFader ("FADER", p.faderDb), juce::Colours::white);
    }
    if (isSelected)
        frame (r, kLilac.brighter (0.4f), 0.0f, 2.0f);
}

void Screen::pageDefault()
{
    for (int i = 0; i < 8; ++i)
    {
        const juce::Rectangle<float> r (2.0f + (float) i * 101.0f, 47.0f, 99.0f, 719.0f);
        if (auto* s = surface.stripAt (Bay::Channel, i))
            fastStrip (*s, r, i);
        else
            fill (r, kTile);
    }
    detailArea ({ 812.0f, 47.0f, 210.0f, 719.0f });
}

void Screen::detailGroup (juce::Rectangle<float> r)
{
    // The selected group's level in the corner (p.146).
    const int group = surface.getGroup();
    if (group < 0)
        return;
    fill (r, kTile, 3.0f);
    frame (r, kLilac, 3.0f);
    text (r.withHeight (16.0f), surface.groupName (group), kText, 12.0f, juce::Justification::centred, true);
    if (group < kVcas)
    {
        auto& d = ctx.engine.getDca (group);
        auto b = bindFader ("VCA", d.faderDb);
        text ({ r.getX(), r.getY() + 18.0f, r.getWidth() * 0.5f, 14.0f }, b.text ? b.text() : juce::String(), kYellow, 12.0f, juce::Justification::centred, true);
        dial ({ r.getCentreX(), r.getY() + 16.0f, r.getWidth() * 0.5f, r.getHeight() - 18.0f }, "vca level", std::move (b), kRed, false);
    }
    else
        text (r.withTrimmedTop (18.0f), juce::String ((int) surface.groupMembers (group).size()) + " channels", kDim, 11.0f);
}

void Screen::detailArea (juce::Rectangle<float> r)
{
    fill (r, kStrip);
    frame (r, kLilac.withAlpha (0.5f));
    auto* s = surface.selected();
    const auto groupBox = r.removeFromBottom (58.0f).reduced (4.0f, 3.0f);
    detailGroup (groupBox);
    if (s == nullptr)
    {
        text (r.withHeight (40.0f), "no channel selected", kDim, 12.0f);
        return;
    }
    auto* strip = s;
    auto& p = s->params;
    const bool channel = s->getKind() == StripKind::Channel, dyn = s->getKind() != StripKind::Aux;
    {
        const auto head = r.removeFromTop (34.0f).reduced (3.0f, 3.0f);
        const auto c = stripDisplayColour (*s);
        fill (head, c);
        text (head.reduced (6.0f, 0.0f).withWidth (40.0f), kindId (*s), textOn (c), 10.0f, juce::Justification::centredLeft, true);
        text (head.withTrimmedLeft (44.0f), s->getName(), textOn (c), 17.0f, juce::Justification::centredLeft, true);
    }
    const auto top = [&r] (float h) { return r.removeFromTop (h).reduced (4.0f, 2.0f); };
    const auto title = [this, &top] (const juce::String& t) { text (top (16.0f), t, kText, 11.0f, juce::Justification::centredLeft, true); };
    const auto sideChain = [this, &top, &title, s] (bool gate)
    {
        // The side chain filter: IN, the frequency, WIDTH's three (p.255).
        auto& d = gate ? s->params.gate() : s->params.dynamics();
        title ("side chain");
        auto row = top (62.0f);
        static const char* const widths[] = { "1/3 oct", "1 oct", "2 oct" };
        button ({ row.getX(), row.getY() + 2.0f, 40.0f, 16.0f }, "SC IN", "IN", sideChainIn (d), kBlue, [&d] { setSideChainIn (d, ! sideChainIn (d)); });
        button ({ row.getX(), row.getY() + 24.0f, 62.0f, 16.0f }, "WIDTH", widths[sideChainWidth (d)], sideChainIn (d), juce::Colour (0xffc9ccd8), [&d]
        {
            if (sideChainIn (d))
                setSideChainWidth (d, (sideChainWidth (d) + 1) % 3);
        });
        dial ({ row.getX() + 66.0f, row.getY(), 60.0f, 62.0f }, "sc freq", bindFreq ("FREQ", d.scFreq, 50.0f, 15000.0f, 1000.0f), juce::Colour (0xff8a8f9c));
        const juce::Rectangle<float> graph (row.getX() + 130.0f, row.getY() + 2.0f, row.getWidth() - 130.0f, 56.0f);
        fill (graph, juce::Colour (0xff05061a));
        if (g != nullptr && sideChainIn (d))
        {
            constexpr int n = 40;
            std::array<double, n> freqs {}, db {};
            for (int i = 0; i < n; ++i)
                freqs[(size_t) i] = 20.0 * std::pow (1000.0, (double) i / (double) (n - 1));
            ld::ld_strip_response (s->getHandle(), gate ? 5 : 6, freqs.data(), db.data(), n, 48000.0);
            juce::Path path;
            for (int i = 0; i < n; ++i)
            {
                const juce::Point<float> at (graph.getX() + graph.getWidth() * (float) i / (float) (n - 1), graph.getY() + 4.0f - (float) juce::jlimit (-30.0, 0.0, db[(size_t) i]) / 30.0f * (graph.getHeight() - 6.0f));
                if (i == 0)
                    path.startNewSubPath (at);
                else
                    path.lineTo (at);
            }
            g->setColour (kText);
            g->strokePath (path, juce::PathStrokeType (1.4f));
        }
    };

    switch (surface.getArea())
    {
        case Area::Overview:
        {
            title ("channel overview");
            eqGraph (*s, top (110.0f), true);
            if (dyn)
            {
                auto row = top (84.0f);
                dynGraph (*s, false, row.removeFromLeft (row.getWidth() * 0.5f).reduced (2.0f));
                if (channel)
                    dynGraph (*s, true, row.reduced (2.0f));
            }
            std::vector<KnobBinding> list { bindTrim (*s) };
            if (channel)
            {
                list.push_back (bindHighPass (*s));
                list.push_back (bindLowPass (*s));
            }
            list.push_back (bindDelayMs (*s));
            dialGrid (top (148.0f), std::move (list), 2, 74.0f, juce::Colour (0xff8a8f9c));
            break;
        }
        case Area::Config:
        {
            // Source, the filters with their slopes, the delay, the processing order (pp.242-247).
            title ("configuration");
            const int in = s->getInputLeft();
            text (top (16.0f), s->isInput() ? (in >= 0 ? routing::sourceLabel (in) : juce::String ("Unassigned")) : juce::String ("direct input: Unassigned"), kDim, 11.0f, juce::Justification::centredLeft);
            filterGraph (*s, top (96.0f));
            std::vector<KnobBinding> list { bindTrim (*s) };
            list.push_back (bindDelayMs (*s));
            if (channel)
            {
                list.push_back (bindHighPass (*s));
                list.push_back (bindLowPass (*s));
            }
            dialGrid (top (channel ? 148.0f : 74.0f), std::move (list), 2, 74.0f, juce::Colour (0xff8a8f9c));
            auto row = top (20.0f);
            button (row.removeFromLeft (48.0f).reduced (1.0f), "PHASE", juce::String::charToString ((juce::juce_wchar) 0xd8), p.input.invert.load(), kBlue, [strip] { toggle (strip->params.input.invert); });
            button (row.removeFromLeft (70.0f).reduced (1.0f), "DELAY ON", "delay", p.input.delayOn.load(), kBlue, [strip] { toggle (strip->params.input.delayOn); });
            if (channel)
            {
                auto filters = top (20.0f);
                const float w = filters.getWidth() / 4.0f;
                button (filters.removeFromLeft (w).reduced (1.0f), "HP IN", "hp in", p.filter().lowCut.load(), kBlue, [strip] { toggle (strip->params.filter().lowCut); });
                button (filters.removeFromLeft (w).reduced (1.0f), "HP SLOPE", steepSlope (*s, false) ? "24 dB" : "12 dB", steepSlope (*s, false), kBlue, [strip] { setSteepSlope (*strip, false, ! steepSlope (*strip, false)); });
                button (filters.removeFromLeft (w).reduced (1.0f), "LP IN", "lp in", p.filter().highCut.load(), kBlue, [strip] { toggle (strip->params.filter().highCut); });
                button (filters.reduced (1.0f), "LP SLOPE", steepSlope (*s, true) ? "12 dB" : "6 dB", steepSlope (*s, true), kBlue, [strip] { setSteepSlope (*strip, true, ! steepSlope (*strip, true)); });
            }
            title ("processing order");
            const bool dynFirst = p.processOrder.load() == 1;
            button (top (22.0f), "C/O", dyn ? (dynFirst ? "Dyn.  >  Ins.  >  EQ" : "EQ  >  Ins.  >  Dyn.") : "EQ  >  Ins.", dynFirst, kBlue, [strip]
            {
                strip->params.processOrder = strip->params.processOrder.load() == 1 ? 0 : 1;
            });
            button (top (20.0f), "SAFE", "automation safe", p.recallSafe.load(), kYellow, [strip] { toggle (strip->params.recallSafe); });
            break;
        }
        case Area::Comp:
        case Area::Gate:
        {
            const bool gate = surface.getArea() == Area::Gate;
            if (gate ? ! channel : ! dyn)
            {
                title (gate ? "gate: input channels only" : "no compressor on an aux return");
                break;
            }
            auto& d = gate ? p.gate() : p.dynamics();
            auto mode = bindDyn (*s, false, "mode");
            title (gate ? juce::String ("gate") : "compressor  " + (mode.text ? mode.text().toLowerCase() : juce::String()));
            dynGraph (*s, gate, top (150.0f));
            {
                // Gain reduction.
                auto bar2 = top (12.0f);
                fill (bar2, juce::Colour (0xff04050f));
                const float gr = juce::jlimit (0.0f, 1.0f, std::abs ((gate ? s->meters.gateReductionDb : s->meters.dynReductionDb).load()) / 24.0f);
                fill (bar2.withLeft (bar2.getRight() - bar2.getWidth() * gr), kRed);
            }
            std::vector<KnobBinding> list;
            const juce::StringArray ids = gate ? juce::StringArray { "thr", "range", "att", "hld", "rel" } : juce::StringArray { "thr", "ratio", "att", "rel", "gain", "clipl" };
            for (const auto& id : ids)
                if (auto b = bindDyn (*s, gate, id); b.isValid())
                    list.push_back (std::move (b));
            const float rows = (float) ((list.size() + 2) / 3);
            dialGrid (top (rows * 70.0f), std::move (list), 3, 70.0f, gate ? kGreen : kPurple);
            auto row = top (22.0f);
            button (row.removeFromLeft (40.0f).reduced (1.0f), "ON", "ON", d.on.load(), kBlue, [&d] { toggle (d.on); });
            if (! gate)
            {
                auto knee = bindDyn (*s, false, "knee");
                auto clip = bindDyn (*s, false, "clip");
                // MODE: four on an input, five (with Shimmer) on an output; KNEE's three (pp.250-251, 273).
                button (row.removeFromLeft (84.0f).reduced (1.0f), "MODE", mode.text ? mode.text() : juce::String ("MODE"), true, juce::Colour (0xffc9ccd8), [mode] { cycle (mode); });
                button (row.removeFromLeft (clip.isValid() ? 44.0f : 74.0f).reduced (1.0f), "KNEE", knee.text ? knee.text() : juce::String ("KNEE"), true, juce::Colour (0xffc9ccd8), [knee] { cycle (knee); });
                if (clip.isValid())
                    button (row.reduced (1.0f), "CLIP", "clip", clip.get() >= 0.5f, kOrange, [clip] { clip.set (clip.get() >= 0.5f ? 0.0f : 1.0f); });
            }
            sideChain (gate);
            break;
        }
        case Area::Eq:
        {
            title ("eq");
            eqGraph (*s, top (118.0f), true);
            if (s->isOutput())
            {
                // An output's PEQ can give way to a GEQ (p.276); four of the six bands show (p.277).
                auto row = top (20.0f);
                const bool geq = Pro2Surface::hasGeq (*s);
                button (row.removeFromLeft (50.0f).reduced (1.0f), "GEQ", "GEQ", geq, kBlue, [this, strip]
                {
                    Pro2Surface::setGeq (*strip, ! Pro2Surface::hasGeq (*strip));
                    surface.selectionChanged();
                });
                button (row.removeFromLeft (44.0f).reduced (1.0f), "UP", "UP", false, juce::Colour (0xffc9ccd8), [this] { surface.setEqBand (surface.getEqBand() - 1); });
                button (row.removeFromLeft (44.0f).reduced (1.0f), "DOWN", "DWN", false, juce::Colour (0xffc9ccd8), [this] { surface.setEqBand (surface.getEqBand() + 1); });
                if (geq)
                {
                    button (top (24.0f), "OPEN GEQ", "open the GEQ", false, juce::Colour (0xffc9ccd8), [this] { showPage (Page::Geq); });
                    break;
                }
            }
            const auto bands = eqBands (*s);
            const int selectedBand = surface.getEqBand(), first = juce::jlimit (0, juce::jmax (0, bands.size() - 4), selectedBand - 1);
            for (int k = first; k < juce::jmin (first + 4, bands.size()); ++k)
            {
                const auto id = bands[k];
                auto tile = top (104.0f);
                const bool active = k == selectedBand;
                fill (tile, active ? kCream : kTile, 3.0f); // the active band's cream ground (p.258)
                spot ("band " + juce::String (k), tile, [this, k] { surface.setEqBand (k); });
                const auto ink = active ? kInk : kText;
                auto shape = bindEqParam (*s, id + "s");
                text ({ tile.getX() + 4.0f, tile.getY() + 1.0f, tile.getWidth() - 8.0f, 12.0f }, eqBandName (*s, k), ink, 10.0f, juce::Justification::centredRight, true);
                fill ({ tile.getX() + 3.0f, tile.getY() + 3.0f, 8.0f, 8.0f }, bandColour (id), 4.0f);
                if (shape.isValid())
                {
                    // SHAPE: the minimum harmonic disruption shelves, or a filter on an output's bands (pp.292-294).
                    button ({ tile.getX() + 16.0f, tile.getY() + 2.0f, 70.0f, 14.0f }, "SHAPE " + juce::String (k), shape.text ? shape.text() : juce::String ("SHAPE"), shape.get() > 0.01f, kBlue, [this, shape, k]
                    {
                        cycle (shape);
                        surface.setEqBand (k);
                    });
                }
                const char* const parts[] = { "f", "g", "w" };
                const char* const names[] = { "freq", "gain", "width" };
                const float w = tile.getWidth() / 3.0f;
                for (int part = 0; part < 3; ++part)
                {
                    auto b = bindEqParam (*s, id + parts[part]);
                    b.label = names[part];
                    const juce::Rectangle<float> cell (tile.getX() + w * (float) part, tile.getY() + 17.0f, w, 86.0f);
                    const auto name = juce::String (names[part]) + " " + juce::String (k);
                    if (! active)
                    {
                        dial (cell, name, std::move (b), kBlue);
                        continue;
                    }
                    // On the cream ground the legends are dark.
                    const float d = juce::jmin (cell.getWidth() - 10.0f, cell.getHeight() - 26.0f);
                    text ({ cell.getX(), cell.getY() + 4.0f + d, cell.getWidth(), 11.0f }, names[part], b.isValid() ? kInk : kInk.withAlpha (0.4f), 9.5f);
                    text ({ cell.getX(), cell.getY() + 14.0f + d, cell.getWidth(), 12.0f }, b.isValid() && b.text ? b.text() : juce::String(), juce::Colour (0xff1c3cc0), 10.0f, juce::Justification::centred, true);
                    dial ({ cell.getCentreX() - d * 0.5f, cell.getY() + 3.0f, d, d }, name, std::move (b), kBlue, false);
                }
            }
            break;
        }
        case Area::Insert:
        {
            // The insert: its switch, and the effect it goes through (p.256; the console patches a send and a return).
            title ("insert");
            auto& ins = p.preInsert();
            button (top (22.0f).withWidth (60.0f), "INS", "INS", ins.on.load(), kBlue, [&ins] { toggle (ins.on); });
            auto b = bindInt ([&ins] { return (int) ins.fx.load(); }, [&ins] (int v) { ins.fx = v; }, -1, kFxUnits - 1, [this] (int v)
            {
                if (v < 0)
                    return juce::String ("Unassigned");
                const int model = ctx.engine.getFx().getModel (v);
                return "FX " + juce::String (v + 1) + (model > ld::kFxExt ? "  " + models::model (models::Fx, model).name : juce::String());
            });
            auto field = top (24.0f);
            fill (field, juce::Colour (0xff04050f), 2.0f);
            text (field.reduced (6.0f, 0.0f), b.text(), kText, 12.0f, juce::Justification::centredLeft);
            spot ("insert fx", field, {}, std::move (b));
            text (top (30.0f), "drag the field to choose the effect", kDim, 10.0f, juce::Justification::centredLeft);
            break;
        }
        case Area::Aux:
        case Area::Matrix:
        {
            // Eight contributions: ON, PRE and the level, off to +6 dB (p.259).
            const int bank = surface.getMixBank();
            title (bank == 2 ? "mtx sends" : "aux sends");
            for (int k = 0; k < 8; ++k)
            {
                auto* mix = bank < 2 ? ctx.engine.getBusInSlot (bank * 8 + k) : k < (int) ctx.engine.getMatrices().size() ? ctx.engine.getMatrices()[(size_t) k].get() : nullptr;
                const auto send = mix != nullptr ? sendTo (*s, *mix) : MixSend {};
                auto row = top (62.0f);
                fill (row, kTile, 3.0f);
                if (mix == nullptr)
                    continue;
                const auto n = juce::String (k + 1);
                text ({ row.getX() + 4.0f, row.getY() + 2.0f, 90.0f, 13.0f }, kindId (*mix) + "  " + mix->getName(), kText, 10.0f, juce::Justification::centredLeft, true);
                if (! send.isValid())
                    continue;
                button ({ row.getX() + 4.0f, row.getY() + 18.0f, 40.0f, 16.0f }, "ON " + n, send.isGroup() ? "ON" : "ON", send.on->load(), kBlue, [send] { toggle (*send.on); });
                if (! send.isGroup())
                {
                    button ({ row.getX() + 4.0f, row.getY() + 40.0f, 40.0f, 16.0f }, "PRE " + n, "PRE", send.isPre(), juce::Colour (0xffc9ccd8), [send] { send.setPre (! send.isPre()); });
                    auto b = bindSendLevel ("level", *send.level);
                    text ({ row.getX() + 50.0f, row.getY() + 24.0f, 70.0f, 14.0f }, b.text(), kYellow, 11.0f, juce::Justification::centred, true);
                    dial ({ row.getRight() - 64.0f, row.getY() + 3.0f, 56.0f, 56.0f }, "level " + n, std::move (b), k < 2 ? kRed : k < 4 ? kGreen : k < 6 ? kYellow : kBlue, false);
                }
                else
                    text ({ row.getX() + 50.0f, row.getY() + 24.0f, 120.0f, 14.0f }, "group: follows the fader", kDim, 10.0f, juce::Justification::centredLeft);
            }
            auto row = top (22.0f);
            button (row.removeFromLeft (60.0f).reduced (1.0f), "MIX UP", "UP", false, juce::Colour (0xffc9ccd8), [this] { surface.setMixBank (surface.getMixBank() - 1); });
            button (row.removeFromLeft (60.0f).reduced (1.0f), "MIX DOWN", "DWN", false, juce::Colour (0xffc9ccd8), [this] { surface.setMixBank (surface.getMixBank() + 1); });
            break;
        }
        case Area::Masters:
        {
            title ("masters");
            const bool toMasters = s->getKind() != StripKind::Main && s->getKind() != StripKind::Matrix;
            auto row = top (22.0f);
            button (row.removeFromLeft (50.0f).reduced (1.0f), {}, "SIS", false, kOrange, {});
            if (toMasters)
            {
                button (row.removeFromLeft (50.0f).reduced (1.0f), "ST", "ST", s->mains[0].on.load(), kRed, [strip] { toggle (strip->mains[0].on); });
                if (ctx.engine.getMains().size() > 1)
                    button (row.removeFromLeft (50.0f).reduced (1.0f), "MON", "MON", s->mains[1].on.load(), kYellow, [strip] { toggle (strip->mains[1].on); });
                std::vector<KnobBinding> list { bindPan ("pan", p.pan) };
                if (ctx.engine.getMains().size() > 1)
                    list.push_back (bindSendLevel ("mono level", s->mains[1].levelDb));
                dialGrid (top (84.0f), std::move (list), 2, 84.0f, kRed);
            }
            auto keys = top (24.0f);
            button (keys.removeFromLeft (64.0f).reduced (1.0f), "MUTE", "MUTE", p.mute.load(), kRed, [strip] { toggle (strip->params.mute); });
            button (keys.removeFromLeft (64.0f).reduced (1.0f), "SOLO", "SOLO", p.solo.load(), kYellow, [this, strip] { surface.pressSolo (*strip); });
            auto f = top (juce::jmax (120.0f, r.getHeight() - 30.0f));
            auto level = bindFader ("FADER", p.faderDb);
            text (f.withHeight (16.0f), level.text ? level.text() + " dB" : juce::String(), kYellow, 12.0f, juce::Justification::centred, true);
            f.removeFromTop (22.0f);
            meter ({ f.getCentreX() - 34.0f, f.getY(), 10.0f, f.getHeight() - 8.0f }, juce::jmax (ctx.meters.get (s, MeterPoint::Output, 0), ctx.meters.get (s, MeterPoint::Output, 1)));
            slider ({ f.getCentreX() - 4.0f, f.getY(), 30.0f, f.getHeight() - 8.0f }, "fader", std::move (level), juce::Colours::white);
            break;
        }
    }
}

//==============================================================================
void Screen::pageOverview()
{
    // Console Overview: every meter, all the time (p.241).
    const auto cell = [this] (Strip& s, juce::Rectangle<float> r)
    {
        const bool isSelected = &s == surface.selected();
        fill (r.reduced (1.0f), isSelected ? kStripSel : kindColour (s), 2.0f);
        const auto c = stripDisplayColour (s);
        const auto name = r.reduced (2.0f).removeFromBottom (26.0f);
        fill (name, c);
        text (name.withHeight (12.0f), kindId (s), textOn (c), 8.5f, juce::Justification::centred, true);
        text (name.withTrimmedTop (12.0f), s.getName(), textOn (c), 8.5f);
        const auto m = r.reduced (4.0f, 4.0f).withTrimmedBottom (40.0f);
        meter ({ m.getCentreX() - 5.0f, m.getY(), 10.0f, m.getHeight() }, juce::jmax (ctx.meters.get (&s, MeterPoint::Output, 0), ctx.meters.get (&s, MeterPoint::Output, 1)));
        const juce::Rectangle<float> lamps (r.getX() + 3.0f, r.getBottom() - 39.0f, r.getWidth() - 6.0f, 8.0f);
        fill (lamps.withWidth (lamps.getWidth() * 0.5f - 1.0f), s.params.mute.load() ? kRed : kRed.darker (0.8f));
        fill (lamps.withLeft (lamps.getCentreX() + 1.0f), s.params.solo.load() ? kYellow : kYellow.darker (0.8f));
        auto* strip = &s;
        spot (kindId (s), r, [this, strip] { surface.pressSelect (strip); });
    };
    const auto& channels = ctx.engine.getChannels();
    const float w = 1016.0f / 28.0f;
    for (int i = 0; i < juce::jmin (56, (int) channels.size()); ++i)
        cell (*channels[(size_t) i], { 4.0f + (float) (i % 28) * w, 52.0f + (float) (i / 28) * 236.0f, w, 232.0f });
    std::vector<Strip*> rest;
    for (auto kind : { StripKind::Aux, StripKind::Bus, StripKind::Matrix, StripKind::Main })
        for (const auto& s : ctx.engine.getStrips (kind))
            if (rest.size() < 36)
                rest.push_back (s.get());
    const float w2 = 1016.0f / 36.0f;
    for (int i = 0; i < (int) rest.size(); ++i)
        cell (*rest[(size_t) i], { 4.0f + (float) i * w2, 526.0f, w2, 238.0f });
    // The monitor's meters at the end of the row.
    for (int side = 0; side < 2; ++side)
        meter ({ 4.0f + ((float) rest.size() + 0.25f + (float) side * 0.8f) * w2, 532.0f, 12.0f, 200.0f }, ctx.engine.monitorPeak[side].load());
    if (rest.size() < 35)
        text ({ 4.0f + (float) rest.size() * w2, 736.0f, w2 * 2.0f, 14.0f }, "mon", kDim, 9.0f);
}

void Screen::pageAutomation()
{
    // The scene cue list (pp.163-166): the current scene in yellow, the next in green, the last in red.
    auto& show = ctx.show;
    const int count = show.getNumScenes(), current = show.getCurrentSceneIndex();
    const juce::Rectangle<float> head (6.0f, 50.0f, 1012.0f, 78.0f);
    fill (head, kTile, 3.0f);
    const auto now = current >= 0 ? show.getScene (current) : std::optional<Show::Scene>();
    fill ({ 12.0f, 56.0f, 380.0f, 66.0f }, juce::Colour (0xfff5e88a), 2.0f);
    text ({ 20.0f, 58.0f, 364.0f, 22.0f }, now ? juce::String (current + 1) + "   " + now->name : juce::String ("no scene recalled"), kInk, 16.0f, juce::Justification::centredLeft, true);
    text ({ 20.0f, 82.0f, 364.0f, 36.0f }, now ? now->notes : juce::String(), kInk, 11.0f, juce::Justification::topLeft);
    fill ({ 404.0f, 56.0f, 200.0f, 66.0f }, juce::Colour (0xff04050f), 2.0f);
    text ({ 404.0f, 56.0f, 200.0f, 40.0f }, juce::Time::getCurrentTime().formatted ("%H:%M:%S"), juce::Colour (0xff5cf0e0), 24.0f, juce::Justification::centred, true);
    button ({ 412.0f, 100.0f, 56.0f, 18.0f }, "LAST", "LAST", true, kRed, [this]
    {
        ctx.show.recallPrevious();
        setCue (ctx.show.getCurrentSceneIndex());
    });
    button ({ 476.0f, 100.0f, 56.0f, 18.0f }, "NOW", "NOW", true, kYellow, [this]
    {
        if (juce::isPositiveAndBelow (cue, ctx.show.getNumScenes()))
            ctx.show.recallScene (cue);
        rebuild();
    });
    button ({ 540.0f, 100.0f, 56.0f, 18.0f }, "NEXT", "NEXT", true, kGreen, [this]
    {
        ctx.show.recallNext();
        setCue (ctx.show.getCurrentSceneIndex());
    });
    button ({ 620.0f, 62.0f, 120.0f, 24.0f }, "STORE SCENE", "STORE SCENE", surface.isStorePending(), kYellow, [this] { surface.storePressed(); });
    button ({ 620.0f, 94.0f, 56.0f, 22.0f }, "OK", "OK", surface.isStorePending(), kGreen, [this] { surface.okPressed(); });
    button ({ 684.0f, 94.0f, 56.0f, 22.0f }, "CANCEL", "CANCEL", surface.isStorePending(), kRed, [this] { surface.cancelPressed(); });
    text ({ 752.0f, 62.0f, 260.0f, 56.0f }, juce::String (count) + (count == 1 ? " scene" : " scenes") + " in " + (show.getName().isNotEmpty() ? show.getName() : juce::String ("the show")), kDim, 12.0f, juce::Justification::centredLeft);

    const juce::Rectangle<float> columns (6.0f, 134.0f, 1012.0f, 20.0f);
    fill (columns, juce::Colour (0xff2c3070));
    text ({ 14.0f, 134.0f, 40.0f, 20.0f }, "No.", kText, 11.0f, juce::Justification::centredLeft, true);
    text ({ 64.0f, 134.0f, 300.0f, 20.0f }, "Name", kText, 11.0f, juce::Justification::centredLeft, true);
    text ({ 380.0f, 134.0f, 500.0f, 20.0f }, "Notes", kText, 11.0f, juce::Justification::centredLeft, true);
    constexpr int rows = 23;
    const int first = juce::jlimit (0, juce::jmax (0, count + 1 - rows), cue - rows / 2);
    for (int row = 0; row < rows; ++row)
    {
        const int i = first + row;
        const juce::Rectangle<float> r (6.0f, 156.0f + (float) row * 26.0f, 1012.0f, 24.0f);
        const auto scene = i < count ? show.getScene (i) : std::optional<Show::Scene>();
        const auto ground = i == current ? juce::Colour (0xfff5e88a) : i == current + 1 && current >= 0 && i < count ? juce::Colour (0xffbfe9c4)
                            : i == current - 1 ? juce::Colour (0xfff0c6c0) : row % 2 == 0 ? juce::Colour (0xffe9f0ee) : juce::Colour (0xffd6e4df);
        fill (r, i <= count ? ground : ground.withAlpha (0.25f));
        if (i == cue)
            frame (r, juce::Colour (0xff1c3cc0), 0.0f, 2.0f);
        if (i > count)
            continue;
        text ({ 14.0f, r.getY(), 40.0f, r.getHeight() }, juce::String (i + 1), kInk, 12.0f, juce::Justification::centredLeft, true);
        text ({ 64.0f, r.getY(), 300.0f, r.getHeight() }, scene ? scene->name : juce::String ("<empty scene>"), scene ? kInk : kInk.withAlpha (0.45f), 12.0f, juce::Justification::centredLeft, scene.has_value());
        text ({ 380.0f, r.getY(), 630.0f, r.getHeight() }, scene ? scene->notes : juce::String(), kInk, 11.0f, juce::Justification::centredLeft);
        spot ("scene " + juce::String (i + 1), r, [this, i] { setCue (i); });
    }
}

void Screen::pageEffects()
{
    // The rack of six (p.127), and the selected unit's controls from its model table.
    auto& fx = ctx.engine.getFx();
    const juce::Rectangle<float> rack (8.0f, 52.0f, 316.0f, 710.0f);
    fill (rack, juce::Colour (0xff23252b), 4.0f);
    fill (rack.reduced (10.0f, 8.0f), juce::Colour (0xff08090c));
    const auto face = [] (const juce::String& token)
    {
        if (token == "MPDN780")
            return juce::Colour (0xffc9ccd0);
        if (token == "MPFLG")
            return juce::Colour (0xff7a2f9a);
        if (token == "MPPHS")
            return juce::Colour (0xff1f8a48);
        if (token == "MPPIT")
            return juce::Colour (0xff4a5560);
        if (token == "MP3BC")
            return juce::Colour (0xff2aa0a8);
        if (token == "MPDEQ")
            return juce::Colour (0xff8a3ab0);
        return juce::Colour (0xff2c2e33);
    };
    for (int u = 0; u < kFxUnits; ++u)
    {
        const juce::Rectangle<float> unit (rack.getX() + 22.0f, rack.getY() + 12.0f + (float) u * 100.0f, rack.getWidth() - 44.0f, 96.0f);
        const int model = u < fx.getNumSlots() ? fx.getModel (u) : ld::kFxNone;
        const bool loaded = model > ld::kFxExt;
        const auto& spec = models::model (models::Fx, model);
        const auto c = loaded ? face (spec.token) : juce::Colour (0xff111216);
        fill (unit, c, 2.0f);
        if (g != nullptr)
        {
            g->setColour (juce::Colour (0xff9aa0a6));
            for (float x : { unit.getX() - 9.0f, unit.getRight() + 3.0f })
                for (float y : { unit.getY() + 12.0f, unit.getBottom() - 18.0f })
                    g->fillEllipse (x, y, 6.0f, 6.0f);
        }
        text ({ unit.getX() + 8.0f, unit.getY() + 4.0f, 200.0f, 18.0f }, "FX " + juce::String (u + 1) + "   " + (loaded ? spec.name : juce::String ("empty")), loaded ? textOn (c) : kDim, 13.0f, juce::Justification::centredLeft, true);
        if (loaded)
        {
            auto& params = fx.params (u);
            for (int i = 0; i < juce::jmin (8, (int) spec.params.size()); ++i)
            {
                auto b = bindModelParam (params.values[i], spec.params[(size_t) i]);
                const juce::Rectangle<float> k (unit.getX() + 8.0f + (float) i * 32.0f, unit.getY() + 30.0f, 28.0f, 28.0f);
                if (g != nullptr)
                {
                    g->setColour (juce::Colour (0xff15161a));
                    g->fillEllipse (k);
                    g->setColour (juce::Colours::white);
                    const float a = (b.get() - 0.5f) * juce::MathConstants<float>::pi * 5.0f / 3.0f;
                    g->drawLine (juce::Line<float> (k.getCentre(), k.getCentre().getPointOnCircumference (12.0f, a)), 2.0f);
                }
                text ({ k.getX() - 2.0f, k.getBottom() + 2.0f, 32.0f, 10.0f }, spec.params[(size_t) i].label.substring (0, 6).toLowerCase(), textOn (c), 7.5f);
            }
            fill ({ unit.getRight() - 16.0f, unit.getY() + 6.0f, 8.0f, 8.0f }, params.bypass.load() ? juce::Colour (0xff402020) : kGreen, 4.0f);
        }
        if (u == fxUnit)
            frame (unit.expanded (2.0f), kYellow, 3.0f, 2.5f);
        spot ("unit " + juce::String (u + 1), unit, [this, u] { setFxUnit (u); });
    }
    fill ({ rack.getX() + 22.0f, rack.getY() + 614.0f, rack.getWidth() - 44.0f, 84.0f }, juce::Colour (0xff111216), 2.0f);

    // The effect window.
    const juce::Rectangle<float> window (332.0f, 52.0f, 686.0f, 710.0f);
    fill (window, kStrip, 4.0f);
    frame (window, kLilac, 4.0f);
    const int model = fx.getModel (fxUnit);
    const bool loaded = model > ld::kFxExt;
    const auto& spec = models::model (models::Fx, model);
    fill ({ window.getX(), window.getY(), window.getWidth(), 30.0f }, juce::Colour (0xff2c3aa8), 4.0f);
    text ({ window.getX() + 10.0f, window.getY(), 400.0f, 30.0f }, "FX " + juce::String (fxUnit + 1) + ":  " + (loaded ? spec.name : juce::String ("no effect")), kText, 16.0f, juce::Justification::centredLeft, true);
    const auto list = fxModels();
    const int at = juce::jmax (0, list.indexOf (model));
    button ({ window.getRight() - 236.0f, window.getY() + 5.0f, 28.0f, 20.0f }, "type -", "<", false, juce::Colour (0xffc9ccd8), [this, list, at] { setFxModel (list[(at + list.size() - 1) % list.size()]); });
    text ({ window.getRight() - 204.0f, window.getY(), 60.0f, 30.0f }, "effect", kText, 11.0f);
    button ({ window.getRight() - 140.0f, window.getY() + 5.0f, 28.0f, 20.0f }, "type +", ">", false, juce::Colour (0xffc9ccd8), [this, list, at] { setFxModel (list[(at + 1) % list.size()]); });
    if (! loaded)
    {
        text (window.withTrimmedTop (40.0f).withHeight (40.0f), "Choose an effect with the arrows.", kDim, 13.0f);
        return;
    }
    auto& params = fx.params (fxUnit);
    button ({ window.getRight() - 100.0f, window.getY() + 5.0f, 90.0f, 20.0f }, "bypass", params.bypass.load() ? "BYPASSED" : "IN", ! params.bypass.load(), kGreen, [&params] { toggle (params.bypass); });
    // Its controls, as its model lists them (Owner's Manual pp.130-142).
    const int count = juce::jmin ((int) spec.params.size(), 48);
    const int columns = 8;
    const float w = (window.getWidth() - 12.0f) / (float) columns, h = count > 40 ? 108.0f : count > 32 ? 124.0f : 132.0f;
    for (int i = 0; i < count; ++i)
    {
        auto b = bindModelParam (params.values[i], spec.params[(size_t) i]);
        b.label = spec.params[(size_t) i].label.substring (0, 14);
        const juce::Rectangle<float> cellArea (window.getX() + 6.0f + w * (float) (i % columns), window.getY() + 40.0f + h * (float) (i / columns), w, juce::jmin (h, 92.0f));
        dial (cellArea, spec.params[(size_t) i].id, std::move (b), i % 2 == 0 ? juce::Colour (0xff8a8f9c) : juce::Colour (0xff6a7088));
    }
}

void Screen::pageGeq()
{
    // The output's GEQ (pp.121-124): 31 faders, the Q, the high and low pass, the two notches.
    auto* s = surface.selectedMix();
    if (s == nullptr && surface.selected() != nullptr && surface.selected()->isOutput())
        s = surface.selected();
    const juce::Rectangle<float> panel (8.0f, 52.0f, 1008.0f, 708.0f);
    fill (panel, juce::Colour (0xff1d1f24), 4.0f);
    if (s == nullptr)
    {
        text (panel.withHeight (80.0f), "Select an output (its SELECT key) to see its graphic EQ.", kDim, 14.0f);
        return;
    }
    auto* strip = s;
    const auto c = stripDisplayColour (*s);
    fill ({ 16.0f, 60.0f, 220.0f, 30.0f }, c);
    text ({ 22.0f, 60.0f, 210.0f, 30.0f }, kindId (*s) + "  " + s->getName(), textOn (c), 15.0f, juce::Justification::centredLeft, true);
    const bool geq = Pro2Surface::hasGeq (*s);
    button ({ 250.0f, 62.0f, 110.0f, 26.0f }, "GEQ", geq ? "GEQ in place" : "use the GEQ", geq, kBlue, [this, strip]
    {
        Pro2Surface::setGeq (*strip, ! Pro2Surface::hasGeq (*strip));
        surface.selectionChanged();
        surface.refreshAll();
    });
    if (! geq)
    {
        text ({ 16.0f, 110.0f, 990.0f, 40.0f }, "This output uses its six-band PEQ. The GEQ takes the PEQ's place.", kDim, 13.0f);
        return;
    }
    auto& eq = s->params.eq();
    const auto& m = models::model (models::BusEq, eq.model.load());
    const auto param = [&eq, &m] (const juce::String& id)
    {
        const int i = m.find (id);
        return i >= 0 ? bindModelParam (eq.values[i], m.params[(size_t) i]) : KnobBinding {};
    };
    button ({ 372.0f, 62.0f, 60.0f, 26.0f }, "EQ ON", "IN", eq.on.load(), kGreen, [&eq] { toggle (eq.on); });
    button ({ 440.0f, 62.0f, 70.0f, 26.0f }, "FLAT", "FLAT", false, juce::Colour (0xffc9ccd8), [&eq, &m]
    {
        for (int band = 1; band <= 31; ++band)
            if (const int i = m.find (juce::String (band)); i >= 0)
                eq.values[i] = 0.0f;
    });
    eqGraph (*s, { 16.0f, 100.0f, 992.0f, 130.0f }, false);

    // The faders, +/-12 dB.
    const juce::Rectangle<float> well (16.0f, 240.0f, 840.0f, 400.0f);
    fill (well, juce::Colour (0xff0c0d10), 3.0f);
    const float pitch = well.getWidth() / 31.0f;
    if (g != nullptr)
    {
        g->setColour (kText.withAlpha (0.2f));
        for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f })
            g->fillRect (well.getX(), well.getY() + 20.0f + (well.getHeight() - 60.0f) * (12.0f - db) / 24.0f, well.getWidth(), 1.0f);
    }
    const int onFaders = surface.getMixMode() == MixMode::Geq ? surface.getGeqFirst() : -1;
    for (int band = 0; band < 31; ++band)
    {
        const juce::Rectangle<float> f (well.getX() + pitch * (float) band + 3.0f, well.getY() + 20.0f, pitch - 6.0f, well.getHeight() - 60.0f);
        auto b = param (juce::String (band + 1));
        if (onFaders >= 0 && band >= onFaders && band < onFaders + 8)
            fill (f.expanded (2.0f, 14.0f), kYellow.withAlpha (0.18f)); // the eight on the mix bay's faders
        text ({ f.getX() - 4.0f, well.getBottom() - 34.0f, pitch + 2.0f, 12.0f }, b.label, kText, 8.5f);
        text ({ f.getX() - 4.0f, well.getBottom() - 20.0f, pitch + 2.0f, 12.0f }, b.isValid() ? juce::String (juce::jmap (b.get(), -12.0f, 12.0f), 1) : juce::String(), kYellow, 8.0f);
        slider (f, "band " + juce::String (band + 1), std::move (b), kRed);
    }

    // The filters and the Q.
    const juce::Rectangle<float> side (866.0f, 240.0f, 142.0f, 400.0f);
    fill (side, juce::Colour (0xff0c0d10), 3.0f);
    const auto isOn = [] (const KnobBinding& b) { return b.isValid() && b.get() > 0.5f; };
    auto q = param ("q");
    button ({ side.getX() + 8.0f, side.getY() + 8.0f, 126.0f, 20.0f }, "Q", "Q: " + (q.text ? q.text() : juce::String()), isOn (q), kBlue, [q] { cycle (q); });
    const std::pair<const char*, const char*> filters[] = { { "h", "HPF" }, { "l", "LPF" } };
    float y = side.getY() + 36.0f;
    for (const auto& [id, label] : filters)
    {
        auto on = param (juce::String (id) + "on"), slope = param (juce::String (id) + "s");
        button ({ side.getX() + 8.0f, y, 44.0f, 18.0f }, juce::String (label) + " IN", label, isOn (on), kBlue, [on] { cycle (on); });
        button ({ side.getX() + 8.0f, y + 24.0f, 44.0f, 18.0f }, juce::String (label) + " SLOPE", (slope.text ? slope.text() : juce::String()) + " dB", isOn (slope), juce::Colour (0xffc9ccd8), [slope] { cycle (slope); });
        dial ({ side.getX() + 62.0f, y - 2.0f, 72.0f, 80.0f }, juce::String (label).toLowerCase(), param (juce::String (id) + "f"), juce::Colour (0xff8a8f9c));
        y += 86.0f;
    }
    for (int notch = 1; notch <= 2; ++notch)
    {
        const auto id = "n" + juce::String (notch);
        auto on = param (id + "on");
        button ({ side.getX() + 8.0f, y + 10.0f, 44.0f, 18.0f }, "NOTCH " + juce::String (notch) + " IN", "N" + juce::String (notch), isOn (on), kBlue, [on] { cycle (on); });
        dial ({ side.getX() + 62.0f, y - 2.0f, 72.0f, 80.0f }, "notch " + juce::String (notch), param (id + "f"), juce::Colour (0xff8a8f9c));
        y += 86.0f;
    }
    text ({ 16.0f, 650.0f, 990.0f, 20.0f }, "GEQ (the advanced mix bay navigation key) puts eight bands on the mix bay's faders; its scroll keys move by four.", kDim, 11.0f, juce::Justification::centredLeft);
}

void Screen::pagePatching()
{
    // An overview of the patch (pp.47-67): what each input channel takes, and where each output goes. The
    // console's From and To panels with their connectors are not built.
    const auto& channels = ctx.engine.getChannels();
    const juce::Rectangle<float> from (8.0f, 52.0f, 600.0f, 708.0f), to (616.0f, 52.0f, 400.0f, 708.0f);
    fill (from, kTile, 4.0f);
    fill (to, kTile, 4.0f);
    fill (from.withHeight (24.0f), juce::Colour (0xff2c3aa8), 4.0f);
    fill (to.withHeight (24.0f), juce::Colour (0xff2c3aa8), 4.0f);
    text (from.withHeight (24.0f).reduced (8.0f, 0.0f), "To: Inputs   (drag a field to choose its source)", kText, 12.0f, juce::Justification::centredLeft, true);
    text (to.withHeight (24.0f).reduced (8.0f, 0.0f), "From: Busses", kText, 12.0f, juce::Justification::centredLeft, true);
    const int numSources = ctx.engine.getNumSources();
    for (int i = 0; i < 56 && patchScroll + i < (int) channels.size(); ++i)
    {
        auto* s = channels[(size_t) (patchScroll + i)].get();
        const juce::Rectangle<float> r (from.getX() + 6.0f + (float) (i / 28) * 298.0f, from.getY() + 30.0f + (float) (i % 28) * 24.0f, 290.0f, 22.0f);
        const auto c = stripDisplayColour (*s);
        fill (r.withWidth (130.0f), c);
        text (r.withWidth (130.0f).reduced (4.0f, 0.0f), kindId (*s) + "  " + s->getName(), textOn (c), 11.0f, juce::Justification::centredLeft, true);
        const auto field = r.withTrimmedLeft (134.0f);
        fill (field, juce::Colour (0xff04050f));
        auto b = bindInt ([s] { return (int) s->params.inputLeft.load(); }, [s] (int v) { s->params.inputLeft = v; }, -1, numSources - 1, [] (int v)
        {
            return v < 0 ? juce::String ("Unassigned") : routing::sourceLabel (v);
        });
        text (field.reduced (5.0f, 0.0f), b.text(), s->getInputLeft() >= 0 ? kText : kDim, 11.0f, juce::Justification::centredLeft);
        spot ("source " + juce::String (patchScroll + i + 1), field, {}, std::move (b));
    }
    const auto& names = ctx.engine.getHardwareOutputNames();
    std::vector<Strip*> outs;
    for (auto kind : { StripKind::Bus, StripKind::Matrix, StripKind::Main })
        for (const auto& s : ctx.engine.getStrips (kind))
            outs.push_back (s.get());
    for (int i = 0; i < juce::jmin (28, (int) outs.size()); ++i)
    {
        auto* s = outs[(size_t) i];
        const juce::Rectangle<float> r (to.getX() + 6.0f, to.getY() + 30.0f + (float) i * 24.0f, to.getWidth() - 12.0f, 22.0f);
        const auto c = stripDisplayColour (*s);
        fill (r.withWidth (170.0f), c);
        text (r.withWidth (170.0f).reduced (4.0f, 0.0f), kindId (*s) + "  " + s->getName(), textOn (c), 11.0f, juce::Justification::centredLeft, true);
        const int left = s->params.outputLeft.load(), right = s->params.outputRight.load();
        const auto port = [&names] (int n) { return juce::isPositiveAndBelow (n, names.size()) ? names[n] : "Out " + juce::String (n + 1); };
        fill (r.withTrimmedLeft (174.0f), juce::Colour (0xff04050f));
        text (r.withTrimmedLeft (180.0f), left < 0 ? juce::String ("Unassigned") : port (left) + (right >= 0 && right != left ? " / " + port (right) : juce::String()), left >= 0 ? kText : kDim, 11.0f, juce::Justification::centredLeft);
    }
}

void Screen::pageGroups()
{
    // VCA and POPulation groups (pp.143-146): the VCAs' level, MUTE and SOLO; each group's unfold and area B.
    for (int group = 0; group < kGroups; ++group)
    {
        const bool vca = group < kVcas;
        const juce::Rectangle<float> r = vca ? juce::Rectangle<float> (8.0f + (float) group * 126.0f, 52.0f, 122.0f, 420.0f)
                                             : juce::Rectangle<float> (8.0f + (float) (group - kVcas) * 168.0f, 480.0f, 164.0f, 280.0f);
        const bool isSelected = surface.getGroup() == group;
        fill (r, isSelected ? kStripSel : kStrip, 4.0f);
        const auto n = juce::String (group + 1);
        const auto c = vca ? consoleColour (ctx.engine.getDca (group).colour.load()) : juce::Colour (0xffd8dcff);
        const auto name = r.withHeight (30.0f).reduced (4.0f, 4.0f);
        fill (name, c);
        text (name, surface.groupName (group), textOn (c), 13.0f, juce::Justification::centred, true);
        auto row = juce::Rectangle<float> (r.getX() + 4.0f, r.getY() + 34.0f, r.getWidth() - 8.0f, 20.0f);
        button (row.withWidth (row.getWidth() * 0.5f - 1.0f), "unfold " + n, "unfold", isSelected, kBlue, [this, group] { surface.selectGroup (group); });
        button (row.withLeft (row.getCentreX() + 1.0f), "area B " + n, "area B", surface.isAreaB (group), kBlue, [this, group] { surface.setAreaB (group, ! surface.isAreaB (group)); });
        float listTop = r.getY() + 60.0f;
        if (vca)
        {
            auto& d = ctx.engine.getDca (group);
            button ({ r.getX() + 6.0f, r.getY() + 62.0f, 50.0f, 20.0f }, "vca MUTE " + n, "MUTE", d.mute.load(), kRed, [&d] { toggle (d.mute); });
            button ({ r.getX() + 6.0f, r.getY() + 88.0f, 50.0f, 20.0f }, "vca SOLO " + n, "SOLO", d.solo.load(), kYellow, [&d] { toggle (d.solo); });
            auto level = bindFader ("VCA", d.faderDb);
            text ({ r.getX() + 4.0f, r.getY() + 114.0f, 56.0f, 14.0f }, level.text ? level.text() : juce::String(), kYellow, 11.0f, juce::Justification::centred, true);
            slider ({ r.getRight() - 44.0f, r.getY() + 66.0f, 28.0f, 190.0f }, "vca " + n, std::move (level), kRed);
            listTop = r.getY() + 270.0f;
        }
        const auto members = surface.groupMembers (group);
        text ({ r.getX() + 6.0f, listTop, r.getWidth() - 12.0f, 14.0f }, juce::String ((int) members.size()) + (members.size() == 1 ? " channel" : " channels"), kDim, 10.0f, juce::Justification::centredLeft);
        const int shown = juce::jmax (0, (int) ((r.getBottom() - listTop - 20.0f) / 13.0f));
        for (int i = 0; i < juce::jmin (shown, (int) members.size()); ++i)
            text ({ r.getX() + 6.0f, listTop + 15.0f + (float) i * 13.0f, r.getWidth() - 12.0f, 13.0f }, kindId (*members[(size_t) i]) + "  " + members[(size_t) i]->getName(), i < 3 && surface.isAreaB (group) ? kYellow : kText, 10.0f, juce::Justification::centredLeft);
    }
}

} // namespace livedaw::ui::pro2
