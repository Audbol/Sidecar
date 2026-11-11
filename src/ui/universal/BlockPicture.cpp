#include "ui/universal/BlockPicture.h"

#include "ui/DelayUnits.h"
#include "ui/universal/Widgets.h"

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kTransferPoints = 61; // -60 .. 0 dB in 1 dB steps

    /** The first `n` parameters of a model as the front panel would read them. */
    juce::StringArray keyValues (const models::Model& spec, const float* v, int count, int n)
    {
        juce::StringArray out;
        for (int i = 0; i < (int) spec.params.size() && i < count && out.size() < n; ++i)
        {
            const auto& p = spec.params[(size_t) i];
            if (p.kind == models::ParamKind::Toggle && p.label.isEmpty())
                continue;
            out.add (p.label.toUpperCase().substring (0, 5) + " " + p.display (v[i]));
        }
        return out;
    }

    void drawCurve (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<double>& curve, bool graphic, juce::Colour colour, const Theme& theme)
    {
        g.setColour (theme.well);
        g.fillRect (r);
        g.setColour (theme.outline);
        for (float f : { 0.25f, 0.5f, 0.75f })
            g.fillRect (r.getX() + r.getWidth() * f, r.getY(), 1.0f, r.getHeight());
        g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);
        if (curve.size() < 2)
            return;
        const auto yOf = [&] (double db) { return r.getCentreY() - r.getHeight() * 0.45f * (float) juce::jlimit (-1.0, 1.0, db / 18.0); };
        const auto xOf = [&] (size_t i) { return r.getX() + r.getWidth() * (float) i / (float) (curve.size() - 1); };
        if (graphic)
        {
            // One bar to a sample of the curve (the faders' bands).
            const float barW = juce::jmax (1.5f, r.getWidth() / (float) curve.size() * 0.55f);
            g.setColour (colour.withAlpha (0.55f));
            for (size_t i = 0; i < curve.size(); ++i)
            {
                const float y = yOf (curve[i]);
                g.fillRect (juce::Rectangle<float> (xOf (i) - barW * 0.5f, juce::jmin (y, r.getCentreY()), barW, juce::jmax (1.0f, std::abs (y - r.getCentreY()))));
            }
        }
        juce::Path line, fill;
        fill.startNewSubPath (r.getX(), r.getCentreY());
        for (size_t i = 0; i < curve.size(); ++i)
        {
            fill.lineTo (xOf (i), yOf (curve[i]));
            if (i == 0)
                line.startNewSubPath (xOf (i), yOf (curve[i]));
            else
                line.lineTo (xOf (i), yOf (curve[i]));
        }
        fill.lineTo (r.getRight(), r.getCentreY());
        fill.closeSubPath();
        g.setColour (colour.withAlpha (0.3f));
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (line, juce::PathStrokeType (1.5f));
    }

    /** The static curve (output over input, -60 .. 0 dB), the 1:1 line behind it, and the gain reduction at the right. */
    void drawTransfer (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<float>& out, float reduction, juce::Colour colour, const Theme& theme)
    {
        auto bar = r.removeFromRight (juce::jmin (26.0f, r.getWidth() * 0.3f));
        g.setColour (theme.well);
        g.fillRect (r);
        g.setColour (theme.outline);
        for (float f : { 0.25f, 0.5f, 0.75f })
        {
            g.fillRect (r.getX() + r.getWidth() * f, r.getY(), 1.0f, r.getHeight());
            g.fillRect (r.getX(), r.getY() + r.getHeight() * f, r.getWidth(), 1.0f);
        }
        const auto at = [&] (float in, float o)
        {
            return juce::Point<float> (r.getX() + r.getWidth() * (in + 60.0f) / 60.0f,
                                       r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (o + 60.0f) / 60.0f));
        };
        g.setColour (theme.hw.tick);
        g.drawLine ({ at (-60.0f, -60.0f), at (0.0f, 0.0f) }, 1.0f);
        if (out.size() == (size_t) kTransferPoints)
        {
            juce::Path p;
            for (int i = 0; i < kTransferPoints; ++i)
            {
                const auto pt = at ((float) (i - 60), out[(size_t) i]);
                if (i == 0)
                    p.startNewSubPath (pt);
                else
                    p.lineTo (pt);
            }
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (1.8f));
        }
        // Gain reduction over 24 dB, down from the top, with its value.
        bar = bar.reduced (3.0f, 0.0f);
        g.setColour (theme.well);
        g.fillRect (bar);
        const float cut = bar.getHeight() * juce::jlimit (0.0f, 1.0f, reduction / 24.0f);
        g.setColour (theme.hw.ledWarn);
        g.fillRect (bar.withHeight (cut));
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kMicro));
        g.drawText (reduction > 0.05f ? "-" + juce::String (reduction, 1) : juce::String ("GR"), bar.translated (0.0f, 0.0f), juce::Justification::centredBottom, false);
    }

    void line (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, const Theme& theme, bool bright = true)
    {
        g.setColour (bright ? theme.text : theme.textDim);
        g.setFont (type::font (type::kMicro, bright));
        g.drawFittedText (text, r.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }

    juce::Colour pictureColour (bool on, const Theme& theme) { return on ? theme.hw.pointer : theme.hw.tick; }
} // namespace

BlockPicture::Glyph BlockPicture::glyphFor (const juce::String& n, models::Category c)
{
    const auto s = n.toLowerCase();
    const auto has = [&s] (std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (s.contains (w))
                return true;
        return false;
    };
    if (has ({ "reverb", "hall", "room", "plate", "chamber", "spring", "ambien", "gated", "verb" }))
        return Glyph::Reverb;
    if (has ({ "delay", "echo", "dly" }))
        return Glyph::Delay;
    if (has ({ "chorus", "flang", "phas", "rotary", "mod", "trem", "vibr", "wah" }))
        return Glyph::Modulation;
    if (has ({ "pitch", "harmon", "shift", "octav", "detun" }))
        return Glyph::Pitch;
    if (has ({ "amp", "cab", "guitar", "tube", "preamp", "dist", "overdr", "sat", "tape", "vintage" }))
        return Glyph::Amp;
    if (has ({ "comp", "limit", "gate", "expand", "dynam", "de-ess", "deess", "duck" }))
        return Glyph::Dynamics;
    if (has ({ "eq", "filter", "graphic" }))
        return Glyph::Eq;
    switch (c)
    {
        case models::Category::Gate:
        case models::Category::Compressor:
        case models::Category::Special: return Glyph::Dynamics;
        case models::Category::Eq:
        case models::Category::Filter: return Glyph::Eq;
        default: break;
    }
    return Glyph::Amp;
}

void BlockPicture::drawGlyph (juce::Graphics& g, juce::Rectangle<float> r, Glyph kind, juce::Colour colour)
{
    r = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())).reduced (1.0f);
    g.setColour (colour);
    juce::Path p;
    const auto x = [&] (float f) { return r.getX() + r.getWidth() * f; };
    const auto y = [&] (float f) { return r.getY() + r.getHeight() * f; };
    switch (kind)
    {
        case Glyph::Reverb: // a decaying tail
            for (int i = 0; i < 5; ++i)
            {
                const float h = 0.9f * std::pow (0.7f, (float) i);
                g.fillRect (x (0.05f + 0.2f * (float) i), y (0.5f - h * 0.5f), juce::jmax (1.5f, r.getWidth() * 0.1f), r.getHeight() * h);
            }
            return;
        case Glyph::Delay: // an echo's repeats
            for (int i = 0; i < 3; ++i)
            {
                const float h = 0.9f - 0.3f * (float) i;
                g.fillRect (x (0.1f + 0.33f * (float) i), y (1.0f - h), juce::jmax (1.5f, r.getWidth() * 0.16f), r.getHeight() * h);
            }
            return;
        case Glyph::Modulation:
            p.startNewSubPath (x (0.0f), y (0.5f));
            for (int i = 1; i <= 16; ++i)
                p.lineTo (x ((float) i / 16.0f), y (0.5f - 0.4f * std::sin ((float) i / 16.0f * juce::MathConstants<float>::twoPi)));
            break;
        case Glyph::Pitch:
            p.startNewSubPath (x (0.3f), y (0.9f));
            p.lineTo (x (0.3f), y (0.1f));
            p.lineTo (x (0.05f), y (0.4f));
            p.startNewSubPath (x (0.3f), y (0.1f));
            p.lineTo (x (0.55f), y (0.4f));
            p.startNewSubPath (x (0.75f), y (0.1f));
            p.lineTo (x (0.75f), y (0.9f));
            p.lineTo (x (0.5f), y (0.6f));
            p.startNewSubPath (x (0.75f), y (0.9f));
            p.lineTo (x (1.0f), y (0.6f));
            break;
        case Glyph::Dynamics: // a knee
            p.startNewSubPath (x (0.0f), y (1.0f));
            p.lineTo (x (0.5f), y (0.5f));
            p.lineTo (x (1.0f), y (0.25f));
            break;
        case Glyph::Eq: // a bell
            p.startNewSubPath (x (0.0f), y (0.65f));
            for (int i = 1; i <= 16; ++i)
            {
                const float t = (float) i / 16.0f;
                p.lineTo (x (t), y (0.65f - 0.5f * std::exp (-std::pow ((t - 0.5f) / 0.18f, 2.0f))));
            }
            break;
        case Glyph::Amp: // the amplifier triangle
            p.startNewSubPath (x (0.1f), y (0.1f));
            p.lineTo (x (0.1f), y (0.9f));
            p.lineTo (x (0.95f), y (0.5f));
            p.closeSubPath();
            break;
        case Glyph::None: return;
    }
    g.strokePath (p, juce::PathStrokeType (1.4f));
}

bool BlockPicture::update (UiContext& ctx, const BlockTarget& t, const std::vector<double>& freqs)
{
    std::vector<double> newCurve;
    std::vector<float> newTransfer, newLive;
    juce::String newName;
    juce::StringArray newValues;
    Glyph newGlyph = Glyph::None;
    bool newGraphic = false;
    int newMode = 2;

    const auto response = [&] (const BlockTarget& tt)
    {
        std::vector<double> db (freqs.size(), 0.0);
        if (tt.response && tt.response (freqs.data(), db.data(), (int) freqs.size()))
            return db;
        return std::vector<double>();
    };

    switch (t.kind)
    {
        case BlockTarget::Kind::Eq:
        case BlockTarget::Kind::Filter:
            newCurve = response (t);
            if (t.eq != nullptr)
            {
                const auto n = models::model (t.table, t.eq->model.load()).name.toUpperCase();
                newGraphic = n.contains ("GEQ") || n.contains ("GRAPHIC");
                newName = n;
            }
            newLive = { t.eq != nullptr ? (t.eq->on.load() ? 1.0f : 0.0f) : 1.0f };
            break;
        case BlockTarget::Kind::Dynamics:
            if (t.dynamics != nullptr)
            {
                std::vector<float> values ((size_t) ld::kMaxDynParams), in ((size_t) kTransferPoints);
                for (int i = 0; i < ld::kMaxDynParams; ++i)
                    values[(size_t) i] = t.dynamics->values[i].load();
                for (int i = 0; i < kTransferPoints; ++i)
                    in[(size_t) i] = (float) (i - 60);
                newTransfer.assign ((size_t) kTransferPoints, 0.0f);
                if (! models::dynStaticCurve (t.dynamics->model.load(), values.data(), (int) values.size(), in.data(), newTransfer.data(), kTransferPoints))
                {
                    newTransfer.clear();
                    newCurve = response (t); // no level curve: its key filter
                }
                newName = models::model (models::Dynamics, t.dynamics->model.load()).name.toUpperCase();
                newLive = { t.dynamics->on.load() ? 1.0f : 0.0f };
            }
            break;
        case BlockTarget::Kind::Input:
            if (t.input != nullptr)
                newLive = { t.input->trimDb.load(), t.input->balanceDb.load(), t.input->invert.load() ? 1.0f : 0.0f };
            break;
        case BlockTarget::Kind::Plugin:
            if (t.pluginOn != nullptr && t.strip != nullptr)
            {
                const auto info = ctx.engine.getFx().describeBlock (t.strip->getHandle(), t.uid);
                newName = (info.name.isNotEmpty() ? info.name : juce::String ("PLUG-IN")).toUpperCase() + (info.missing ? " (MISSING)" : juce::String());
                newLive = { t.pluginOn->load() ? 1.0f : 0.0f };
            }
            break;
        case BlockTarget::Kind::Delay:
            if (t.delay != nullptr && t.delayMode != nullptr && t.delayOn != nullptr)
            {
                newMode = t.delayMode->load();
                newLive = { t.delay->load(), t.delayOn->load() ? 1.0f : 0.0f };
            }
            break;
        case BlockTarget::Kind::Insert:
            if (t.insert != nullptr)
            {
                const int slot = t.insert->fx.load();
                newLive = { (float) slot, t.insert->on.load() ? 1.0f : 0.0f };
                const auto* fx = slot >= 0 ? ld::ld_fx_params (ctx.engine.getHandle(), slot) : nullptr;
                if (fx != nullptr)
                {
                    const int m = fx->model.load();
                    const auto& spec = models::model (models::Fx, m);
                    std::vector<float> v ((size_t) ld::kMaxFxParams);
                    for (int i = 0; i < ld::kMaxFxParams; ++i)
                        v[(size_t) i] = fx->values[i].load();
                    newName = spec.name.toUpperCase();
                    newGlyph = glyphFor (spec.name, spec.category);
                    newValues = keyValues (spec, v.data(), ld::kMaxFxParams, 3);
                    std::vector<float> f (freqs.begin(), freqs.end()), db (freqs.size(), 0.0f);
                    if (models::modelResponse (models::Fx, m, v.data(), (int) v.size(), f.data(), db.data(), (int) f.size()))
                    {
                        newCurve.assign (db.begin(), db.end());
                        newGraphic = newName.contains ("GEQ") || newName.contains ("GRAPHIC");
                    }
                    newLive.push_back ((float) m);
                    newLive.insert (newLive.end(), v.begin(), v.begin() + 3);
                }
            }
            break;
        case BlockTarget::Kind::None: break;
    }
    const bool same = newCurve == curve && newTransfer == transfer && newLive == live && newName == name && newValues == values && newGraphic == graphic && newMode == delayMode;
    curve = std::move (newCurve);
    transfer = std::move (newTransfer);
    live = std::move (newLive);
    name = std::move (newName);
    values = std::move (newValues);
    glyph = newGlyph;
    graphic = newGraphic;
    delayMode = newMode;
    return ! same;
}

void BlockPicture::paint (juce::Graphics& g, juce::Rectangle<float> r, const BlockTarget& t, const Theme& theme, bool on, float reduction) const
{
    if (r.getWidth() < 8.0f || r.getHeight() < 6.0f)
        return;
    const auto colour = pictureColour (on, theme);
    switch (t.kind)
    {
        case BlockTarget::Kind::Eq:
        case BlockTarget::Kind::Filter:
            drawCurve (g, r, curve, graphic, colour, theme);
            return;
        case BlockTarget::Kind::Dynamics:
            if (! transfer.empty())
                drawTransfer (g, r, transfer, reduction, colour, theme);
            else
            {
                drawCurve (g, r, curve, false, colour, theme);
                line (g, r.reduced (3.0f, 1.0f), name.isNotEmpty() ? name : juce::String ("DYNAMICS"), theme, false);
            }
            return;
        case BlockTarget::Kind::Input:
        {
            // The trim on a scale of -18 .. +18 dB with the polarity key and the balance beside it.
            const float trim = live.size() > 0 ? live[0] : 0.0f, balance = live.size() > 1 ? live[1] : 0.0f;
            const bool invert = live.size() > 2 && live[2] > 0.5f;
            auto top = r.removeFromTop (juce::jmax (10.0f, r.getHeight() * 0.5f));
            auto key = top.removeFromRight (22.0f).reduced (1.0f);
            g.setColour (invert ? theme.hw.ledWarn : theme.well);
            g.fillRoundedRectangle (key, 3.0f);
            g.setColour (invert ? theme.background : theme.textDim);
            g.setFont (type::font (type::kMicro, true));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xC3\x98")), key, juce::Justification::centred, false);
            auto bar = top.reduced (0.0f, 2.0f);
            g.setColour (theme.well);
            g.fillRect (bar);
            const float mid = bar.getCentreX(), per = bar.getWidth() / 36.0f;
            g.setColour (colour.withAlpha (0.7f));
            g.fillRect (juce::Rectangle<float> (juce::jmin (mid, mid + trim * per), bar.getY(), std::abs (trim * per), bar.getHeight()));
            g.setColour (theme.hw.tick);
            g.fillRect (mid - 0.5f, bar.getY(), 1.0f, bar.getHeight());
            g.setColour (theme.text);
            g.setFont (type::font (type::kMicro, true));
            g.drawText ("TRIM " + juce::String (trim, 1) + " dB", bar.reduced (3.0f, 0.0f), trim >= 0.0f ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
            if (r.getHeight() >= 8.0f)
                line (g, r, juce::String (invert ? "POLARITY INVERTED" : "POLARITY NORMAL") + "   BAL " + juce::String (balance, 1) + " dB", theme, invert || std::abs (balance) > 0.05f);
            return;
        }
        case BlockTarget::Kind::Delay:
        {
            const float value = live.size() > 0 ? live[0] : 0.0f;
            const bool active = live.size() > 1 && live[1] > 0.5f;
            const float ms = delayToMs (value, delayMode, 48000.0f);
            auto top = r.removeFromTop (juce::jmax (8.0f, r.getHeight() * 0.45f)).reduced (0.0f, 1.0f);
            g.setColour (theme.well);
            g.fillRect (top);
            // The time on a log scale from 0.3 ms to 500 ms.
            const float pos = juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (0.3f, ms) / 0.3f) / std::log (500.0f / 0.3f));
            g.setColour (active ? colour.withAlpha (0.8f) : theme.hw.tick);
            g.fillRect (top.withWidth (juce::jmax (2.0f, top.getWidth() * pos)));
            line (g, r.reduced (0.0f, 0.0f), delayText (value, delayMode) + (delayMode == 2 ? juce::String() : "  =  " + juce::String (ms, 1) + " ms") + (active ? "" : "  OFF"), theme, active);
            return;
        }
        case BlockTarget::Kind::Insert:
        {
            const int slot = live.size() > 0 ? (int) live[0] : -1;
            const bool active = live.size() > 1 && live[1] > 0.5f;
            auto row = r.removeFromTop (juce::jmin (r.getHeight(), juce::jmax (14.0f, r.getHeight() * 0.5f)));
            // IN --SEND-> [ device ] -RETURN-> OUT, the loop of an insert.
            const float cy = row.getCentreY();
            g.setColour (theme.hw.tick);
            g.drawLine (row.getX(), cy, row.getRight(), cy, 1.0f);
            auto box = row.reduced (row.getWidth() * 0.22f, 1.0f);
            g.setColour (theme.well);
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (active && slot >= 0 ? colour : theme.hw.tick);
            g.drawRoundedRectangle (box, 3.0f, 1.0f);
            for (float x : { box.getX() - 5.0f, box.getRight() + 5.0f }) // arrow heads on the way
            {
                juce::Path a;
                a.addTriangle (x - 3.0f, cy - 3.0f, x - 3.0f, cy + 3.0f, x + 2.0f, cy);
                g.fillPath (a);
            }
            g.setFont (type::font (type::kMicro));
            g.setColour (theme.textDim);
            g.drawText ("SEND", row.withRight (box.getX() - 6.0f), juce::Justification::centred, false);
            g.drawText ("RET", row.withLeft (box.getRight() + 6.0f), juce::Justification::centred, false);
            auto inside = box.reduced (3.0f, 0.0f);
            if (glyph != Glyph::None)
                drawGlyph (g, inside.removeFromLeft (inside.getHeight()), glyph, colour);
            line (g, inside, slot >= 0 ? name : juce::String ("NO DEVICE"), theme, slot >= 0 && active);
            if (r.getHeight() >= 7.0f)
            {
                if (! curve.empty() && r.getHeight() >= 16.0f)
                    drawCurve (g, r.reduced (0.0f, 1.0f), curve, graphic, colour, theme);
                else if (! values.isEmpty())
                    line (g, r, values.joinIntoString ("  "), theme, active);
                else if (slot >= 0)
                    line (g, r, "FX " + juce::String (slot + 1), theme, active);
            }
            return;
        }
        case BlockTarget::Kind::None: break;
    }
    line (g, r, name, theme, false);
}

} // namespace livedaw::ui::universal
