#include "ui/Sq5Display.h"

#include "engine/AppPaths.h"
#include "engine/MixerEngine.h"
#include "engine/Models.h"
#include "engine/SqMidi.h"
#include "ui/MeterBank.h"

#include <cmath>
#include <tuple>

namespace livedaw::ui::sq5
{

using namespace universal;

namespace
{
    // The screens' blues, taken from the guide's pictures.
    const juce::Colour kScreenTop (0xff3a7aa8), kScreenBottom (0xff1c4f7c), kBar (0xff10283e), kColumn (0xff3f82b3), kColumnDim (0xff2c6490),
        kColumnSelected (0xff4aa56f), kWell (0xff071019), kLine (0xff8fbcd9), kYellow (0xfff4e23a), kGreen (0xff7ee02e), kMixTag (0xfff4e23a),
        kCurve (0xffe7e23a), kFill (0xff3f8a3a);
    constexpr float kOverviewWidth = 84.0f;

    juce::Font font (float size, bool bold = false) { return type::font (size, bold); }

    /** Text on a channel's colour (the SQ has an "Off" colour: black). */
    juce::Colour inkOn (juce::Colour background) { return background.getPerceivedBrightness() > 0.35f ? juce::Colours::black : juce::Colours::white; }

    void text (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, float size, juce::Colour c,
               juce::Justification j = juce::Justification::centred, bool bold = false)
    {
        g.setColour (c);
        g.setFont (font (size, bold));
        g.drawFittedText (t, r.getSmallestIntegerContainer(), j, 2, 0.75f);
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

    /** The EQ of a strip's table, its parameter `id`. */
    KnobBinding eqParam (Strip& s, const juce::String& id, const juce::String& label)
    {
        auto& eq = s.params.eq();
        return bindParam (s.isOutput() ? models::BusEq : models::ChannelEq, eq.model.load(), eq.values, id, label);
    }

    /** The source a strip takes, if it's a preamp socket (or nullptr). */
    ld::SourceParams* preampOf (MixerEngine& e, Strip& s)
    {
        const int in = s.params.inputLeft.load();
        return in >= 0 && in < 128 ? e.getSource (in) : nullptr;
    }

    struct Rows
    {
        bool preamp, hpf, gate, insert, geq, peq, comp, pan;
    };

    /** What a channel type has, top to bottom (the bank view, and the chains on p.126). */
    Rows rowsOf (const Slot& s)
    {
        switch (s.kind)
        {
            case Slot::Input:    return { true, true, true, true, false, true, true, true };
            case Slot::FxReturn: return { false, false, false, false, false, true, false, true };
            case Slot::FxSend:   return { false, false, false, false, false, true, false, false };
            case Slot::Mix:
            case Slot::Main:
            case Slot::Matrix:   return { true, false, false, true, true, true, true, true };
            case Slot::Dca:
            case Slot::None:     break;
        }
        return {};
    }

    /** The bank view's rows, top to bottom, inside a column's area. */
    struct Layout
    {
        juce::Rectangle<float> name, source, preamp, flags, hpf, gate, insert, geq, peq, comp, pan;
    };

    Layout layoutOf (juce::Rectangle<float> c, const Rows& rows)
    {
        Layout l;
        l.name = c.removeFromTop (22.0f);
        l.source = c.removeFromTop (14.0f);
        l.preamp = c.removeFromTop (42.0f);
        l.flags = c.removeFromTop (33.0f);
        const float blocks = c.getHeight() - 40.0f;
        const float unit = blocks / (rows.geq ? 5.6f : 5.0f);
        l.hpf = c.removeFromTop (unit * 0.9f);
        l.gate = c.removeFromTop (unit * 1.1f);
        l.insert = c.removeFromTop (unit * 0.7f);
        if (rows.geq)
            l.geq = c.removeFromTop (unit * 0.9f);
        l.peq = c.removeFromTop (unit * 1.2f);
        l.comp = c.removeFromTop (unit * 1.1f);
        l.pan = c;
        return l;
    }

    /** A little picture's well: `area`, dark, outlined. */
    juce::Rectangle<float> well (juce::Graphics& g, juce::Rectangle<float> area, bool active)
    {
        auto w = area.reduced (4.0f, 1.5f);
        g.setColour (kWell.withAlpha (active ? 1.0f : 0.45f));
        g.fillRoundedRectangle (w, 2.5f);
        g.setColour (kLine.withAlpha (active ? 0.8f : 0.3f));
        g.drawRoundedRectangle (w, 2.5f, 0.8f);
        return w.reduced (1.5f);
    }

    /** A value of 20 Hz .. 20 kHz at its place on a log axis. */
    float logX (juce::Rectangle<float> r, float hz) { return r.getX() + r.getWidth() * std::log10 (hz / 20.0f) / 3.0f; }

    /** A response in dB over a log frequency axis, -`range` to +`range` dB. */
    template <typename T>
    void drawCurve (juce::Graphics& g, juce::Rectangle<float> r, const T* db, const double* freqs, int n, float range, juce::Colour c, bool fill,
                    juce::Colour fillColour = kFill)
    {
        juce::Path p;
        const auto yOf = [&] (float d) { return r.getCentreY() - r.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, d / range); };
        for (int i = 0; i < n; ++i)
        {
            const auto x = logX (r, (float) freqs[i]), y = yOf ((float) db[i]);
            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
        if (fill)
        {
            auto f = p;
            f.lineTo (r.getRight(), yOf (0.0f));
            f.lineTo (r.getX(), yOf (0.0f));
            f.closeSubPath();
            g.setColour (fillColour.withAlpha (0.65f));
            g.fillPath (f);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.3f));
    }

    const std::array<double, 48>& curveFreqs()
    {
        static const auto f = [] {
            std::array<double, 48> a {};
            for (size_t i = 0; i < a.size(); ++i)
                a[i] = 20.0 * std::pow (1000.0, (double) i / (double) (a.size() - 1));
            return a;
        }();
        return f;
    }

    /** A dynamics block's static curve: input and output from -72 to +18 dB (a gate),
        -60 to 18 dB (a compressor). */
    void drawTransfer (juce::Graphics& g, juce::Rectangle<float> r, ld::DynSlotParams& d, juce::Colour c, float thick)
    {
        constexpr int n = 40;
        constexpr float lo = -72.0f, hi = 18.0f;
        std::array<float, n> in {}, out {};
        for (int i = 0; i < n; ++i)
            in[(size_t) i] = lo + (hi - lo) * (float) i / (n - 1);
        std::array<float, ld::kMaxDynParams> values {};
        for (size_t i = 0; i < values.size(); ++i)
            values[i] = d.values[i].load();
        if (! ld::ld_dyn_static_curve (d.model.load(), values.data(), (int32_t) values.size(), in.data(), out.data(), n))
            return;
        juce::Path p;
        for (int i = 0; i < n; ++i)
        {
            const auto x = r.getX() + r.getWidth() * (in[(size_t) i] - lo) / (hi - lo);
            const auto y = r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (out[(size_t) i] - lo) / (hi - lo));
            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thick));
    }

    /** A small vertical level bar. */
    void levelBar (juce::Graphics& g, juce::Rectangle<float> r, float gain)
    {
        g.setColour (juce::Colours::black);
        g.fillRect (r);
        const float db = juce::Decibels::gainToDecibels (gain, -60.0f), f = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        auto bar = r.withTop (r.getBottom() - r.getHeight() * f);
        g.setColour (db > -3.0f ? juce::Colours::red : db > -12.0f ? juce::Colours::yellow : kGreen);
        g.fillRect (bar);
    }

    /** A little pot: the preamp's and pan's in a column. */
    void miniKnob (juce::Graphics& g, juce::Rectangle<float> r, float fraction, juce::Colour c)
    {
        const auto cx = r.getCentreX(), cy = r.getCentreY(), radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
        g.setColour (juce::Colours::white);
        g.fillEllipse (r.withSizeKeepingCentre (radius * 2.0f, radius * 2.0f));
        const float a = juce::MathConstants<float>::pi * (-0.75f + 1.5f * fraction);
        g.setColour (c);
        g.drawLine (cx, cy, cx + std::sin (a) * radius * 0.95f, cy - std::cos (a) * radius * 0.95f, 2.2f);
    }

    juce::String gainText (float db) { return juce::String (juce::roundToInt (db)); }
} // namespace

Strip* stripOf (MixerEngine& e, const Slot& s)
{
    const auto at = [&s] (const MixerEngine::StripList& l, int i) -> Strip* { return juce::isPositiveAndBelow (i, (int) l.size()) ? l[(size_t) i].get() : nullptr; };
    switch (s.kind)
    {
        case Slot::Input:    return at (e.getChannels(), s.index);
        case Slot::FxReturn: return at (e.getAux(), s.index);
        case Slot::Mix:      return at (e.getBuses(), s.index);
        case Slot::FxSend:   return at (e.getBuses(), kMixes + s.index);
        case Slot::Main:     return at (e.getMains(), s.index);
        case Slot::Matrix:   return at (e.getMatrices(), s.index);
        case Slot::Dca:
        case Slot::None:     break;
    }
    return nullptr;
}

Slot slotOf (MixerEngine& e, const Strip* strip)
{
    if (strip == nullptr)
        return {};
    const auto index = [strip] (const MixerEngine::StripList& l)
    {
        for (int i = 0; i < (int) l.size(); ++i)
            if (l[(size_t) i].get() == strip)
                return i;
        return 0;
    };
    switch (strip->getKind())
    {
        case StripKind::Channel: return { Slot::Input, index (e.getChannels()) };
        case StripKind::Aux:     return { Slot::FxReturn, index (e.getAux()) };
        case StripKind::Bus:
        {
            const int i = index (e.getBuses());
            return i < kMixes ? Slot { Slot::Mix, i } : Slot { Slot::FxSend, i - kMixes };
        }
        case StripKind::Main:    return { Slot::Main, 0 };
        case StripKind::Matrix:  return { Slot::Matrix, index (e.getMatrices()) };
    }
    return {};
}

juce::String typeOf (const Slot& s)
{
    switch (s.kind)
    {
        case Slot::Input:    return "Ip";
        case Slot::FxReturn: return "FX Rtn";
        case Slot::Mix:      return isGroupMix (s.index) ? "Grp" : "Aux";
        case Slot::FxSend:   return "FX";
        case Slot::Main:     return "Main";
        case Slot::Matrix:   return "Mtx";
        case Slot::Dca:      return "DCA";
        case Slot::None:     break;
    }
    return {};
}

juce::String mixType (int mix) { return mix == 0 ? "Main" : mix <= kMixes ? (isGroupMix (mix - 1) ? "Grp" : "Aux") : "FX"; }

namespace
{
    int groupsNow = 0; // Bus Config: message thread
    constexpr int kFirstDeepComp = 3; // livedaw_dsp::sq::FIRST_DEEP_COMP

    /** The DEEP preamp block of an input channel, if it has one. */
    ld::DynSlotParams* preampModelOf (Strip& s)
    {
        const int uid = ld::ld_strip_preamp_block (s.getHandle());
        return uid >= 0 ? s.getBlock (uid).dynamics() : nullptr;
    }
} // namespace

int groupCount() noexcept { return groupsNow; }
void setGroupCount (int n) noexcept { groupsNow = juce::jlimit (0, kMixes, n); }
bool isGroupMix (int index) noexcept { return index >= kMixes - groupsNow && index < kMixes; }

juce::String mixName (MixerEngine& e, int mix)
{
    if (mix == 0)
        return "MainLR";
    auto* s = stripOf (e, slotOfMix (mix));
    return s != nullptr ? s->getName() : juce::String();
}

Slot slotOfMix (int mix)
{
    if (mix == 0)
        return { Slot::Main, 0 };
    return mix <= kMixes ? Slot { Slot::Mix, mix - 1 } : Slot { Slot::FxSend, mix - 1 - kMixes };
}

//==============================================================================
Display::Display (UiContext& c, const Theme& t) : ctx (c), theme (t)
{
    historyDb.fill (-90.0f);
    historyGr.fill (0.0f);
    setGroupCount (ctx.settings.getIntValue ("sq5.groups", 0));
    softEdit = soft::get (ctx.settings, 0);
    assignEditing = 0;
    gangSet = gangs::load (ctx.settings);
    loadSigGen();
    rebuild();
    startTimerHz (20);
}

Display::~Display()
{
    // Off with the surface: another console's surface uses it its own way.
    ld::ld_engine_console_osc_set (ctx.engine.getHandle(), false, ld::kOscSine, 1000.0f, -96.0f, false, false);
}

Strip* Display::selectedStrip() const { return ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr; }

void Display::pressScreenKey (Screen s)
{
    if (s == Screen::Processing || s == Screen::Routing)
    {
        if (screen == s && block != Block::Bank)
        {
            showBlock (Block::Bank);
            return;
        }
        if (screen == s && selectedStrip() != nullptr)
        {
            // The bank view: the channel is deselected, Home shows (p.12).
            if (ctx.selectStrip)
                ctx.selectStrip (nullptr);
            screen = Screen::Home;
            block = Block::Bank;
            rebuild();
            return;
        }
    }
    showScreen (s);
}

void Display::showScreen (Screen s)
{
    screen = s;
    block = Block::Bank;
    rebuild();
}

void Display::showBlock (Block b)
{
    libraryOpen = false;
    if (b != Block::Bank)
        screen = b == Block::DirOut || b == Block::MixSettings || b == Block::DcaMute || b == Block::Sends ? Screen::Routing : Screen::Processing;
    block = b;
    rebuild();
}

void Display::setLayer (const std::vector<Slot>& slots)
{
    layer = slots;
    bankFirst = juce::jlimit (0, juce::jmax (0, (int) layer.size() - kColumns), bankFirst);
    if ((screen == Screen::Processing || screen == Screen::Routing) && block == Block::Bank)
        rebuild();
}

void Display::setMix (int mix)
{
    selectedMix = mix;
    repaint();
}

void Display::setBankFirst (int first)
{
    const int f = juce::jlimit (0, juce::jmax (0, (int) layer.size() - kColumns), first);
    if (f != bankFirst)
    {
        bankFirst = f;
        rebuild();
    }
}

void Display::refreshStrip()
{
    // The selected channel in view (p.29).
    if (getSelectedSlot)
    {
        const auto s = getSelectedSlot();
        // FX Screen Follow Sel (p.63): an FX return shows its unit, an FX send its first four's.
        if (screen == Screen::Fx && prefs::get (ctx.settings, prefs::fxFollowSel, true)
            && (s.kind == Slot::FxReturn || s.kind == Slot::FxSend) && s.index != fxSlot)
        {
            fxSlot = s.index;
            rebuild();
        }
        for (int i = 0; i < (int) layer.size(); ++i)
            if (layer[(size_t) i] == s && (i < bankFirst || i >= bankFirst + kColumns))
                bankFirst = juce::jlimit (0, juce::jmax (0, (int) layer.size() - kColumns), i);
    }
    // Routing's details suit a channel, or a mix.
    if (screen == Screen::Routing && block != Block::Bank)
    {
        const auto kind = (getSelectedSlot ? getSelectedSlot() : Slot {}).kind;
        if (kind == Slot::None || kind == Slot::Dca)
            block = Block::Bank;
        else if (block == Block::DirOut && kind != Slot::Input)
            block = Block::MixSettings;
        else if (block == Block::MixSettings && kind == Slot::Input)
            block = Block::DirOut;
    }
    // A detail screen stays on the same block if this channel has it (p.29).
    if (screen == Screen::Processing && block != Block::Bank)
    {
        const auto rows = rowsOf (getSelectedSlot ? getSelectedSlot() : Slot {});
        const bool has = block == Block::Preamp ? rows.preamp
                         : block == Block::Hpf  ? rows.hpf
                         : block == Block::Gate ? rows.gate
                         : block == Block::Insert ? rows.insert
                         : block == Block::Geq  ? rows.geq
                         : block == Block::Peq  ? rows.peq
                                                : rows.comp;
        if (! has)
            block = Block::Bank;
    }
    historyDb.fill (-90.0f);
    historyGr.fill (0.0f);
    rebuild();
}

void Display::setLocked (bool l)
{
    locked = l;
    repaint();
}

int Display::findField (const juce::String& title) const
{
    for (int i = 0; i < (int) fields.size(); ++i)
        if (fields[(size_t) i].title == title)
            return i;
    return -1;
}

void Display::select (int index)
{
    selected = index;
    if (onRotaryChanged)
        onRotaryChanged();
    repaint();
}

KnobBinding Display::getRotaryBinding() const
{
    return juce::isPositiveAndBelow (selected, (int) fields.size()) ? fields[(size_t) selected].binding : KnobBinding {};
}

void Display::touchField (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) fields.size()))
        return;
    const auto& f = fields[(size_t) index];
    if (! f.enabled)
        return;
    if (f.binding.isValid())
    {
        select (index);
        return;
    }
    if (f.press)
    {
        // What it does can rebuild the fields, or (a show recalled, Utility > Data) replace
        // the whole surface.
        const auto press = f.press;
        juce::Component::SafePointer<Display> safe (this);
        press();
        if (safe != nullptr)
            repaint();
    }
}

void Display::setDataFolder (const juce::File& folder)
{
    dataFolder = folder;
    rebuild();
}

juce::File Display::getDataFolder() const
{
    // The LIBRARY's folder of the other surfaces (ShowPanel::libraryFolder).
    return dataFolder != juce::File() ? dataFolder : paths::documents().getChildFile ("Library");
}

const Display::ListArea* Display::listAt (juce::Point<float> at) const
{
    for (const auto& l : lists)
        if (l.area.contains (at))
            return &l;
    return nullptr;
}

//==============================================================================
Field& Display::add (Field::Kind kind, juce::Rectangle<float> area, const juce::String& title)
{
    Field f;
    f.kind = kind;
    f.area = area;
    f.title = title;
    fields.push_back (std::move (f));
    return fields.back();
}

Field& Display::addValue (juce::Rectangle<float> area, const juce::String& title, KnobBinding b)
{
    auto& f = add (Field::Value, area, title);
    f.enabled = b.isValid();
    f.binding = std::move (b);
    return f;
}

Field& Display::addToggle (Field::Kind kind, juce::Rectangle<float> area, const juce::String& title, std::atomic<bool>& value)
{
    auto& f = add (kind, area, title);
    auto* v = &value;
    f.lit = [v] { return v->load(); };
    f.press = [v] { v->store (! v->load()); };
    return f;
}

void Display::rebuild()
{
    fields.clear();
    lists.clear();
    const int was = selected;
    selected = -1;
    if (! hasLibrary())
        libraryOpen = false;
    switch (screen)
    {
        case Screen::Home:
            buildHome();
            break;
        case Screen::Processing:
            if (block == Block::Bank)
                buildBank();
            else
                buildDetail();
            break;
        case Screen::Routing:
            if (block == Block::Bank)
                buildRoutingBank();
            else
                buildDetail();
            break;
        case Screen::Meters:  buildMeters(); break;
        case Screen::Fx:      buildFx(); break;
        case Screen::Scenes:  buildScenes(); break;
        case Screen::Setup:   buildSetup(); break;
        case Screen::Utility: buildUtility(); break;
        case Screen::Io:      buildIo(); break;
    }
    if (was != selected && onRotaryChanged)
        onRotaryChanged();
    repaint();
}

void Display::buildHome()
{
    const float w = 75.0f, h = 64.0f, gap = 36.0f, y = 264.0f;
    float x = ((float) kWidth - (4.0f * w + 3.0f * gap)) * 0.5f;
    const auto next = [&] { juce::Rectangle<float> r (x, y, w, h); x += w + gap; return r; };
    add (Field::Button, next(), "Shut Down").enabled = false; // the app's window closes Sidecar
    auto& lock = add (Field::Button, next(), "Lock");
    lock.press = [this] { setLocked (true); };
    add (Field::Button, next(), "Change User").enabled = false; // Sidecar has no users
    add (Field::Button, next(), "Brightness").enabled = false;  // nor a backlight
}

void Display::buildBank()
{
    const float cw = (float) kWidth / kColumns;
    const juce::Rectangle<float> all (0.0f, (float) kTitleBar + 2.0f, cw, (float) kHeight - kTitleBar - 2.0f);
    for (int c = 0; c < kColumns; ++c)
    {
        const int at = bankFirst + c;
        if (at >= (int) layer.size())
            break;
        const auto slot = layer[(size_t) at];
        if (slot.kind == Slot::None)
            continue;
        const auto col = all.withX ((float) c * cw).reduced (1.5f, 0.0f);
        const auto rows = rowsOf (slot);
        const auto l = layoutOf (col, rows);
        const auto choose = [this, slot] { if (onSelectSlot) onSelectSlot (slot); };
        add (Field::Area, l.name.getUnion (l.source), {}).press = choose;
        const auto block_ = [&] (juce::Rectangle<float> r, bool has, Block b)
        {
            if (has)
                add (Field::Area, r, {}).press = [this, choose, b]
                {
                    choose();
                    showBlock (b);
                };
        };
        block_ (l.preamp.getUnion (l.flags), rows.preamp, Block::Preamp);
        block_ (l.hpf, rows.hpf, Block::Hpf);
        block_ (l.gate, rows.gate, Block::Gate);
        block_ (l.insert, rows.insert, Block::Insert);
        block_ (l.geq, rows.geq, Block::Geq);
        block_ (l.peq, rows.peq, Block::Peq);
        block_ (l.comp, rows.comp, Block::Comp);
    }
}

void Display::buildOverview()
{
    if (screen == Screen::Routing)
    {
        // Routing's details keep the channel's routing column at the left (p.46).
        const auto slot = getSelectedSlot ? getSelectedSlot() : Slot {};
        const float x = 1.5f, w = kOverviewWidth - 3.0f, top = (float) kTitleBar + 2.0f;
        const auto toggle = [this] (juce::Rectangle<float> r, Block b)
        {
            add (Field::Area, r, {}).press = [this, b] { showBlock (block == b ? Block::Bank : b); };
        };
        toggle ({ x, top + 24.0f, w, 22.0f }, slot.kind == Slot::Input ? Block::DirOut : Block::MixSettings);
        toggle ({ x, top + 50.0f, w, 66.0f }, Block::DcaMute);
        if (slot.kind != Slot::Dca)
            toggle ({ x, top + 120.0f, w, (float) kHeight - top - 120.0f }, Block::Sends);
        return;
    }
    if (screen != Screen::Processing)
        return;
    // The selected channel's overview, at the left of a detail (p.29); its blocks open.
    const juce::Rectangle<float> col (1.5f, (float) kTitleBar + 2.0f, kOverviewWidth - 3.0f, (float) kHeight - kTitleBar - 2.0f);
    const auto slot = getSelectedSlot ? getSelectedSlot() : Slot {};
    const auto rows = rowsOf (slot);
    const auto l = layoutOf (col, rows);
    const auto open = [&] (juce::Rectangle<float> r, bool has, Block b)
    {
        if (has)
            add (Field::Area, r, {}).press = [this, b] { showBlock (block == b ? Block::Bank : b); };
    };
    open (l.preamp.getUnion (l.flags), rows.preamp, Block::Preamp);
    open (l.hpf, rows.hpf, Block::Hpf);
    open (l.gate, rows.gate, Block::Gate);
    open (l.insert, rows.insert, Block::Insert);
    open (l.geq, rows.geq, Block::Geq);
    open (l.peq, rows.peq, Block::Peq);
    open (l.comp, rows.comp, Block::Comp);
}

void Display::buildDetail()
{
    buildOverview();
    auto* s = selectedStrip();
    if (s == nullptr)
    {
        block = Block::Bank;
        if (screen == Screen::Routing)
            buildRoutingBank();
        else
            buildBank();
        return;
    }
    if (libraryOpen)
    {
        buildLibrary (*s);
        return;
    }
    switch (block)
    {
        case Block::DirOut:      buildDirOut (*s); break;
        case Block::MixSettings: buildMixSettings (*s); break;
        case Block::DcaMute:     buildDcaMute (*s); break;
        case Block::Sends:       buildSends (*s); break;
        case Block::Preamp: buildPreamp (*s); break;
        case Block::Hpf:    buildHpf (*s); break;
        case Block::Gate:   buildGate (*s); break;
        case Block::Insert: buildInsert (*s); break;
        case Block::Geq:    buildGeq (*s); break;
        case Block::Peq:    buildPeq (*s); break;
        case Block::Comp:   buildComp (*s); break;
        case Block::Bank:   break;
    }
}

void Display::buildPreamp (Strip& s)
{
    // As p.30: Source Select over Socket Preamp, Stereo Image, Delay and Trim under them,
    // the meter on the right of those.
    auto& p = s.params;
    auto* src = preampOf (ctx.engine, s);
    add (Field::Header, { 92, 40, 300, 20 }, "Source Select");
    add (Field::Button, { 100, 68, 150, 34 }, src != nullptr ? "Local Socket" : "Unassigned").enabled = false;
    add (Field::Button, { 258, 68, 60, 34 }, juce::String (p.inputLeft.load() + 1)).enabled = false;
    add (Field::Header, { 92, 128, 300, 20 }, "Socket Preamp");
    if (src != nullptr)
    {
        addToggle (Field::Button, { 100, 162, 72, 34 }, "48V", src->phantom).colour = juce::Colours::red;
        add (Field::Button, { 196, 162, 72, 34 }, "Pad").enabled = false; // Sidecar's preamp has none
        auto gain = bindLinear ("Gain", src->gainDb, 0.0f, 60.0f, " dB", 0, 0.0f);
        gain.steps = 61.0f;
        addValue ({ 300, 150, 88, 48 }, "Gain", std::move (gain));
    }
    add (Field::Header, { 92, 226, 104, 20 }, "Stereo Image");
    add (Field::Header, { 200, 226, 92, 20 }, "Delay");
    add (Field::Header, { 296, 226, 96, 20 }, "Trim");
    add (Field::Value, { 100, 252, 88, 44 }, "Mode").enabled = false; // Sidecar's only mode: L/R
    auto width = bindLinear ("Width", p.width, 0.0f, 100.0f, " %", 0, 100.0f);
    addValue ({ 100, 304, 88, 44 }, "Width", std::move (width)).enabled = p.stereo.load();
    KnobBinding delay;
    {
        auto* in = &p.input;
        delay = bindLinear ("Delay", in->delay, 0.0f, s.isInput() ? 341.0f : 682.0f, " ms", 2, 0.0f);
        const auto inner = delay.set;
        delay.set = [in, inner] (float v) { in->delayMode.store (2); inner (v); }; // the engine's milliseconds
    }
    addValue ({ 204, 252, 84, 44 }, "Delay", std::move (delay));
    addToggle (Field::InKey, { 226, 310, 40, 40 }, "Delay In", p.input.delayOn);
    addToggle (Field::Button, { 300, 254, 88, 34 }, "Polarity", p.input.invert);
    addValue ({ 300, 304, 88, 44 }, "Trim", bindLinear ("Trim", p.input.trimDb, -24.0f, 24.0f, " dB", 1, 0.0f));

    // The DEEP preamp model the Library loads (p.32).
    if (auto* d = preampModelOf (s); d != nullptr && d->on.load())
    {
        const auto& m = models::model (models::Dynamics, d->model.load());
        add (Field::Header, { 412, 128, 256, 20 }, m.name);
        addToggle (Field::InKey, { 626, 156, 40, 40 }, "Preamp In", d->on);
        int i = 0;
        for (const auto& mp : m.params)
        {
            auto binding = bindModelParam (d->values[mp.index], mp);
            addValue ({ 416.0f + (float) i * 66.0f, 204.0f, 62.0f, 48.0f }, mp.label, std::move (binding));
            ++i;
        }
    }
}

void Display::buildLibrary (Strip& s)
{
    // The Library key's list on Preamp, GEQ and the Compressor (pp.32, 39, 43): choosing a
    // model loads it, as Recall does on the console.
    add (Field::Header, { 92, 40, 580, 20 }, block == Block::Preamp ? "Preamp Model Library" : block == Block::Geq ? "GEQ Library" : "Compressor Library");
    float x = 96.0f, y = 68.0f;
    const auto entry = [&] (const juce::String& name, std::function<bool()> lit, std::function<void()> recall)
    {
        auto& f = add (Field::Button, { x, y, 170, 40 }, name);
        f.colour = juce::Colours::white;
        f.lit = std::move (lit);
        f.press = [this, recall]
        {
            recall();
            libraryOpen = false;
            rebuild();
        };
        x += 178.0f;
        if (x > 500.0f)
        {
            x = 96.0f;
            y += 48.0f;
        }
    };
    auto* strip = &s;
    if (block == Block::Preamp)
    {
        if (auto* d = preampModelOf (s))
        {
            entry ("No Model", [d] { return ! d->on.load(); }, [d] { d->on = false; });
            entry (models::model (models::Dynamics, d->model.load()).name, [d] { return d->on.load(); }, [d] { d->on = true; });
        }
        return;
    }
    if (block == Block::Comp)
    {
        for (const auto& m : models::table (models::Dynamics))
        {
            if (m.category != models::Category::Compressor)
                continue;
            const int index = m.index;
            entry (m.name, [strip, index] { return strip->params.dynamics().model.load() == index; }, [strip, index] { strip->setModel (ModelBlock::Dynamics, index); });
        }
        return;
    }
    for (const auto& step : s.getChain())
    {
        if (! step.isBlock() || step.getBlockKind() != BlockKind::Eq)
            continue;
        auto* eq = s.getBlock (step.uid).eq();
        const int uid = step.uid;
        if (eq != nullptr)
            for (const auto& m : models::table (models::BusEq))
                if (m.index > 0 && m.params.size() == 28)
                {
                    const int index = m.index;
                    entry (m.name, [eq, index] { return eq->model.load() == index; }, [strip, uid, index] { strip->setBlockModel (uid, index); });
                }
        break;
    }
}

void Display::buildHpf (Strip& s)
{
    auto& f = s.params.filter();
    auto freq = bindFreq ("Freq", f.lowCutFreq, 20.0f, 2000.0f, 20.0f);
    freq.text = [text_ = freq.text] { return text_() + " Hz"; };
    addValue ({ 130, 330, 130, 54 }, "Freq", std::move (freq));
    auto slope = bindChoice ("Slope", f.lowCutSlope, { "6dB", "12dB Butterworth", "18dB Butterworth", "24dB Butterworth" }, 3);
    {
        // The SQ's slopes are 12, 18 and 24 dB (p.34).
        const auto inner = slope.set;
        slope.set = [inner] (float v) { inner (juce::jmax (1.0f / 3.0f, v)); };
    }
    addValue ({ 300, 330, 190, 54 }, "Slope", std::move (slope));
    addToggle (Field::InKey, { 604, 326, 44, 44 }, "In", f.lowCut);
}

void Display::buildGate (Strip& s)
{
    // As p.35: the side chain and its filter at the left, the timings over the graph,
    // Threshold and Depth at its right, the histogram under it.
    auto& g = s.params.gate();
    const auto table = models::Dynamics;
    const int m = g.model.load();
    add (Field::Header, { 92, 40, 100, 18 }, "Side Chain");
    add (Field::Button, { 98, 62, 88, 28 }, "Self Keyed").enabled = false;
    add (Field::Button, { 112, 110, 60, 32 }, "HPF").enabled = false;      // the key filter: not modelled here
    add (Field::Value, { 100, 146, 84, 40 }, "Freq").enabled = false;
    add (Field::InKey, { 120, 210, 40, 40 }, "Filter In").enabled = false;
    addValue ({ 256, 38, 74, 48 }, "Attack", bindParam (table, m, g.values, "att", "Attack"));
    addValue ({ 350, 38, 74, 48 }, "Hold", bindParam (table, m, g.values, "hld", "Hold"));
    addValue ({ 444, 38, 74, 48 }, "Release", bindParam (table, m, g.values, "rel", "Release"));
    addValue ({ 592, 100, 80, 48 }, "Threshold", bindParam (table, m, g.values, "thr", "Threshold"));
    addValue ({ 592, 160, 80, 48 }, "Depth", bindParam (table, m, g.values, "range", "Depth"));
    addToggle (Field::InKey, { 604, 326, 44, 44 }, "In", g.on);
}

void Display::buildInsert (Strip& s)
{
    // As p.37: Send over Return, the operating level and Unassign under them.
    auto& ins = s.params.preInsert();
    const auto where = [&ins] { return ins.fx.load() >= 0 ? "FX " + juce::String (ins.fx.load() + 1) : juce::String ("Unassigned"); };
    add (Field::Header, { 92, 40, 500, 20 }, "Send");
    add (Field::Button, { 106, 100, 126, 34 }, where()).enabled = false;
    add (Field::Button, { 240, 100, 58, 34 }, juce::String ("-")).enabled = false;
    add (Field::Header, { 92, 160, 500, 20 }, "Return");
    add (Field::Button, { 106, 228, 126, 34 }, where()).enabled = false;
    add (Field::Button, { 240, 228, 58, 34 }, juce::String ("-")).enabled = false;
    add (Field::Button, { 362, 312, 96, 30 }, "Digital").enabled = false;
    add (Field::Button, { 106, 360, 84, 30 }, "Unassign").enabled = false;
    add (Field::Button, { 262, 360, 60, 30 }, "Apply").enabled = false;
    add (Field::Button, { 416, 360, 60, 30 }, "Cancel").enabled = false;
    addToggle (Field::InKey, { 604, 326, 44, 44 }, "In", ins.on);
}

void Display::buildGeq (Strip& s)
{
    // The GEQ is the mix's added block (SQGEQ).
    for (const auto& step : s.getChain())
    {
        if (! step.isBlock() || step.getBlockKind() != BlockKind::Eq)
            continue;
        auto block_ = s.getBlock (step.uid);
        if (auto* eq = block_.eq())
        {
            const auto& m = models::model (models::BusEq, eq->model.load());
            add (Field::Header, { 92, 40, 540, 18 }, m.name);
            const int bands = juce::jmin (28, (int) m.params.size());
            const float x0 = 96.0f, bw = 532.0f / (float) bands;
            for (int b = 0; b < bands; ++b)
            {
                auto binding = bindModelParam (eq->values[b], m.params[(size_t) b]);
                binding.label = m.params[(size_t) b].label;
                auto& f = addValue ({ x0 + (float) b * bw, 64.0f, bw, 260.0f }, "GEQ " + m.params[(size_t) b].label, std::move (binding));
                f.kind = Field::Area;
            }
            addToggle (Field::InKey, { 604, 330, 44, 44 }, "In", eq->on);
        }
        break;
    }
}

void Display::buildPeq (Strip& s)
{
    // As p.40: the graph over four columns, Width, Frequency and Gain stacked in each,
    // the LF and HF mode keys on the outsides.
    auto& eq = s.params.eq();
    const juce::Colour lf (0xffd97cf0), lm (0xff78d6ec), hm (0xff5b9dff), hf (0xffff4d4d);
    const struct { const char* name; const char* prefix; float x; juce::Colour c; } bands[] = {
        { "LF", "l", 167, lf }, { "LM", "1", 273, lm }, { "HM", "2", 380, hm }, { "HF", "h", 486, hf }
    };
    static constexpr float rows[] = { 268.0f, 312.0f, 356.0f };
    for (const auto& b : bands)
    {
        const juce::String n (b.name), pre (b.prefix);
        const auto cell = [&] (int row, const juce::String& which, const char* id)
        {
            auto& f = addValue ({ b.x, rows[row], 62, 34 }, n + " " + which, eqParam (s, pre + id, n + " " + which));
            f.hideTitle = true;
            f.outline = b.c;
        };
        cell (0, "Width", "q");
        cell (1, "Freq", "f");
        cell (2, "Gain", "g");
    }
    const auto modeKeys = [this, &s] (const char* id, const char* name, float x, int shelfGlyph, int cutGlyph)
    {
        auto mode = eqParam (s, id, name);
        if (! mode.isValid())
            return;
        const int glyphs[] = { shelfGlyph, 2, cutGlyph };
        for (int k = 0; k < 3; ++k)
        {
            auto& f = add (Field::Button, { x, rows[k], 44, 34 }, juce::String (name) + " " + juce::String (k));
            f.glyph = glyphs[k];
            f.colour = juce::Colour (0xfff2f2f2);
            f.lit = [get = mode.get, k] { return juce::roundToInt (get() * 2.0f) == k; };
            f.press = [set = mode.set, k] { set ((float) k / 2.0f); };
        }
    };
    modeKeys ("lm", "LF Mode", 110, 1, 3);
    modeKeys ("hm", "HF Mode", 556, 4, 5);
    add (Field::Button, { 628, 274, 44, 34 }, "RTA").enabled = false;
    addToggle (Field::InKey, { 628, 334, 44, 44 }, "In", eq.on);
}

void Display::buildComp (Strip& s)
{
    // As p.41: the side chain at the left; RMS and Peak, Attack, Release and Soft Knee
    // along the top; Ratio, Threshold and Gain at the graph's right; parallel path below.
    auto& d = s.params.dynamics();
    const auto table = models::Dynamics;
    const int m = d.model.load();
    if (m >= kFirstDeepComp)
    {
        // A DEEP model: its own controls (Sidecar's reading of its panel).
        const auto& deep = models::model (table, m);
        add (Field::Header, { 92, 40, 580, 18 }, deep.name);
        int i = 0;
        for (const auto& mp : deep.params)
        {
            addValue ({ 96.0f + (float) (i % 6) * 92.0f, 66.0f + (float) (i / 6) * 60.0f, 88.0f, 52.0f }, mp.label, bindModelParam (d.values[mp.index], mp));
            ++i;
        }
        addToggle (Field::InKey, { 604, 326, 44, 44 }, "In", d.on);
        return;
    }
    add (Field::Header, { 92, 40, 100, 18 }, "Side Chain");
    add (Field::Button, { 98, 62, 88, 28 }, "Self Keyed").enabled = false;
    add (Field::Button, { 112, 110, 60, 32 }, "HPF").enabled = false;
    add (Field::Value, { 100, 146, 84, 40 }, "Freq").enabled = false;
    add (Field::InKey, { 120, 210, 40, 40 }, "Filter In").enabled = false;
    if (auto det = bindParam (table, m, d.values, "det", "Mode"); det.isValid())
    {
        auto& rms = add (Field::Tab, { 262, 44, 52, 20 }, "RMS");
        rms.lit = [get = det.get] { return get() < 0.5f; };
        rms.press = [set = det.set] { set (0.0f); };
        auto& peak = add (Field::Tab, { 316, 44, 52, 20 }, "Peak");
        peak.lit = [get = det.get] { return get() >= 0.5f; };
        peak.press = [set = det.set] { set (1.0f); };
    }
    addValue ({ 388, 38, 74, 48 }, "Attack", bindParam (table, m, d.values, "att", "Attack"));
    addValue ({ 484, 38, 74, 48 }, "Release", bindParam (table, m, d.values, "rel", "Release"));
    if (auto knee = bindParam (table, m, d.values, "soft", "Soft Knee"); knee.isValid())
    {
        auto& f = add (Field::Button, { 596, 42, 76, 34 }, "Soft Knee");
        f.lit = [get = knee.get] { return get() > 0.5f; };
        f.press = [get = knee.get, set = knee.set] { set (get() > 0.5f ? 0.0f : 1.0f); };
    }
    addValue ({ 592, 100, 80, 48 }, "Ratio", bindParam (table, m, d.values, "ratio", "Ratio"));
    addValue ({ 592, 160, 80, 48 }, "Threshold", bindParam (table, m, d.values, "thr", "Threshold"));
    addValue ({ 592, 220, 80, 48 }, "Gain", bindParam (table, m, d.values, "gain", "Gain"));
    addValue ({ 96, 348, 64, 44 }, "Dry", bindParam (table, m, d.values, "dry", "Dry"));
    addValue ({ 166, 348, 64, 44 }, "Wet", bindParam (table, m, d.values, "wet", "Wet"));
    auto parallel = bindParam (table, m, d.values, "par", "Parallel Path");
    if (parallel.isValid())
    {
        auto& f = add (Field::Button, { 96, 312, 134, 30 }, "Parallel Path");
        f.lit = [get = parallel.get] { return get() > 0.5f; };
        f.press = [get = parallel.get, set = parallel.set] { set (get() > 0.5f ? 0.0f : 1.0f); };
    }
    addToggle (Field::InKey, { 604, 326, 44, 44 }, "In", d.on);
}

//==============================================================================
void Display::resized() {}

void Display::timerCallback()
{
    if (sigType.load() != sigShown.type || (sigMute.load() ? 1.0f : 0.0f) != sigShown.mute
        || ! juce::approximatelyEqual (sigFreq.load(), sigShown.freq) || ! juce::approximatelyEqual (sigLevel.load(), sigShown.level))
        applySigGen();
    gangFollower.follow (ctx.engine, gangSet);
    // The history under the gate's and compressor's graphs.
    if (screen == Screen::Processing && (block == Block::Gate || block == Block::Comp))
        if (auto* s = selectedStrip())
        {
            const float level = ctx.meters.get (s, MeterPoint::Trim, 0);
            historyDb[(size_t) historyAt] = juce::Decibels::gainToDecibels (level, -90.0f);
            historyGr[(size_t) historyAt] = block == Block::Gate ? s->meters.gateReductionDb.load() : s->meters.dynReductionDb.load();
            historyAt = (historyAt + 1) % kHistory;
        }
    if (isShowing() && ((++timerTick % 2) == 0 || selected >= 0))
        repaint();
}

void Display::paintField (juce::Graphics& g, const Field& f, bool isSelected)
{
    const auto dim = f.enabled ? 1.0f : 0.4f;
    switch (f.kind)
    {
        case Field::Area:
            return;
        case Field::Label:
            g.setColour (juce::Colours::white.withMultipliedAlpha (dim));
            g.setFont (font (10.5f, true));
            g.drawFittedText (f.live ? f.live() : f.caption, f.area.getSmallestIntegerContainer(), f.centred ? juce::Justification::centred : juce::Justification::centredLeft, 9, 0.8f);
            return;
        case Field::Header:
            g.setColour (kBar);
            g.fillRect (f.area);
            text (g, f.area, f.title, 11.0f, juce::Colours::white, juce::Justification::centred, true);
            return;
        case Field::Value:
        {
            auto r = f.area;
            if (! f.hideTitle)
            {
                auto title = r.removeFromTop (16.0f);
                text (g, title, f.caption.isNotEmpty() ? f.caption : f.title, 10.5f, juce::Colours::white.withMultipliedAlpha (dim), juce::Justification::centred, true);
            }
            r.reduce (2.0f, 0.0f);
            auto box = r.removeFromTop (juce::jmin (22.0f, r.getHeight()));
            g.setColour (kWell.withMultipliedAlpha (dim));
            g.fillRoundedRectangle (box, 2.0f);
            g.setColour ((isSelected ? kYellow : ! f.outline.isTransparent() ? f.outline : kLine).withMultipliedAlpha (dim));
            g.drawRoundedRectangle (box, 2.0f, isSelected ? 1.5f : 0.8f);
            if (f.binding.isValid())
            {
                text (g, box, f.binding.text ? f.binding.text() : juce::String(), 11.0f, isSelected ? kYellow : juce::Colours::white);
                auto bar = r.reduced (2.0f, 2.0f).withHeight (4.0f);
                g.setColour (juce::Colours::black);
                g.fillRect (bar);
                g.setColour (isSelected ? kYellow : juce::Colours::white);
                g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, f.binding.get())));
            }
            else if (f.live)
            {
                // Something to read, not to set.
                text (g, box.reduced (3.0f, 0.0f), f.live(), 11.0f, juce::Colours::white.withMultipliedAlpha (dim), juce::Justification::centred, true);
            }
            return;
        }
        case Field::Tab:
        {
            const bool on = f.lit && f.lit();
            g.setColour ((on ? juce::Colours::white : juce::Colour (0xff24598a)).withMultipliedAlpha (dim));
            g.fillRoundedRectangle (f.area, 4.0f);
            g.setColour (kLine.withMultipliedAlpha (dim));
            g.drawRoundedRectangle (f.area, 4.0f, 0.8f);
            text (g, f.area.reduced (3.0f, 0.0f), f.caption.isNotEmpty() ? f.caption : f.title, 11.5f, (on ? juce::Colours::black : juce::Colours::white).withMultipliedAlpha (dim), juce::Justification::centred, true);
            return;
        }
        case Field::Button:
        {
            const bool on = f.lit && f.lit();
            if (f.row)
            {
                // A list's row (pp.67, 80): dark, the chosen one in the list's green.
                g.setColour ((on ? juce::Colour (0xffb4c24a) : juce::Colour (0xff102a40)).withMultipliedAlpha (dim));
                g.fillRoundedRectangle (f.area, 3.0f);
                g.setColour (kLine.withMultipliedAlpha (dim * 0.8f));
                g.drawRoundedRectangle (f.area, 3.0f, 0.8f);
                text (g, f.area.reduced (8.0f, 0.0f), f.live ? f.live() : f.caption.isNotEmpty() ? f.caption : f.title, 11.5f,
                      (on ? juce::Colours::black : juce::Colours::white).withMultipliedAlpha (dim), juce::Justification::centredLeft, true);
                return;
            }
            const auto top = on ? f.colour.brighter (0.25f) : juce::Colour (0xff4d8bb8), bottom = on ? f.colour.darker (0.25f) : juce::Colour (0xff24598a);
            g.setGradientFill (juce::ColourGradient (top.withMultipliedAlpha (dim), f.area.getX(), f.area.getY(), bottom.withMultipliedAlpha (dim),
                                                     f.area.getX(), f.area.getBottom(), false));
            g.fillRoundedRectangle (f.area, 4.0f);
            g.setColour (kLine.withMultipliedAlpha (dim));
            g.drawRoundedRectangle (f.area, 4.0f, 0.8f);
            if (f.glyph >= 6)
            {
                // The transport keys (pp.111-116): stop, play / pause, record, previous, next.
                const auto ink = (f.glyph == 8 ? juce::Colour (0xffff4a3a) : on ? inkOn (f.colour) : juce::Colours::white).withMultipliedAlpha (dim);
                const auto c = f.area.getCentre();
                const float u = juce::jmin (f.area.getWidth(), f.area.getHeight()) * 0.2f;
                g.setColour (ink);
                const auto triangle = [&g] (float x, float y, float w, float hh, bool right)
                {
                    juce::Path p;
                    if (right)
                        p.addTriangle (x, y - hh, x + w, y, x, y + hh);
                    else
                        p.addTriangle (x + w, y - hh, x, y, x + w, y + hh);
                    g.fillPath (p);
                };
                if (f.glyph == 6)
                    g.fillRect (juce::Rectangle<float> (2.0f * u, 2.0f * u).withCentre (c));
                else if (f.glyph == 7)
                {
                    triangle (c.x - 2.0f * u, c.y, 1.7f * u, u, true);
                    g.fillRect (c.x + 0.3f * u, c.y - u, 0.6f * u, 2.0f * u);
                    g.fillRect (c.x + 1.4f * u, c.y - u, 0.6f * u, 2.0f * u);
                }
                else if (f.glyph == 8)
                    g.fillEllipse (juce::Rectangle<float> (2.0f * u, 2.0f * u).withCentre (c));
                else
                {
                    const bool right = f.glyph == 10;
                    triangle (c.x - 1.6f * u, c.y, 1.6f * u, u, right);
                    triangle (c.x, c.y, 1.6f * u, u, right);
                }
                return;
            }
            if (f.glyph > 0)
            {
                // The PEQ's mode keys: a shape's outline, as p.40 draws them.
                auto r = f.area.reduced (8.0f, 8.0f);
                juce::Path p;
                const auto at = [&r] (float x, float y) { return juce::Point<float> (r.getX() + r.getWidth() * x, r.getY() + r.getHeight() * y); };
                p.startNewSubPath (at (0.0f, f.glyph == 3 ? 1.0f : f.glyph == 5 ? 0.15f : f.glyph == 2 ? 0.75f : 0.65f));
                if (f.glyph == 1)      { p.lineTo (at (0.35f, 0.65f)); p.lineTo (at (0.65f, 0.25f)); p.lineTo (at (1.0f, 0.25f)); }
                else if (f.glyph == 2) { p.lineTo (at (0.3f, 0.75f)); p.lineTo (at (0.5f, 0.15f)); p.lineTo (at (0.7f, 0.75f)); p.lineTo (at (1.0f, 0.75f)); }
                else if (f.glyph == 3) { p.lineTo (at (0.3f, 0.15f)); p.lineTo (at (1.0f, 0.15f)); }
                else if (f.glyph == 4) { p.lineTo (at (0.35f, 0.25f)); p.lineTo (at (0.65f, 0.65f)); p.lineTo (at (1.0f, 0.65f)); }
                else                   { p.lineTo (at (0.7f, 0.15f)); p.lineTo (at (1.0f, 1.0f)); }
                g.setColour ((on ? juce::Colours::black : juce::Colours::white).withMultipliedAlpha (dim));
                g.strokePath (p, juce::PathStrokeType (1.5f));
                return;
            }
            {
                const auto shown = f.live ? f.live() : ! on && f.captionOff.isNotEmpty() ? f.captionOff : f.caption.isNotEmpty() ? f.caption : f.title;
                text (g, f.area, shown, 11.0f, (on ? inkOn (f.colour) : juce::Colours::white).withMultipliedAlpha (dim), juce::Justification::centred, true);
            }
            return;
        }
        case Field::InKey:
        {
            const bool on = f.lit && f.lit();
            auto r = f.area.withSizeKeepingCentre (juce::jmin (f.area.getWidth(), f.area.getHeight()), juce::jmin (f.area.getWidth(), f.area.getHeight()));
            g.setColour ((on ? kGreen : juce::Colour (0xff24598a)).withMultipliedAlpha (dim));
            g.fillEllipse (r);
            g.setColour (juce::Colours::white.withMultipliedAlpha (dim));
            g.drawEllipse (r, 1.2f);
            text (g, r, "In", 11.0f, (on ? juce::Colours::black : juce::Colours::white).withMultipliedAlpha (dim), juce::Justification::centred, true);
            return;
        }
    }
}

void Display::paintTitleBar (juce::Graphics& g)
{
    auto bar = getLocalBounds().toFloat().removeFromTop ((float) kTitleBar);
    g.setColour (juce::Colour (0xff0c1a2b));
    g.fillRect (bar);

    // The selected channel: its tag, at the left, in its colour.
    auto* s = selectedStrip();
    const auto slot = getSelectedSlot ? getSelectedSlot() : Slot {};
    juce::Path tag;
    tag.startNewSubPath (0.0f, 0.0f);
    tag.lineTo (66.0f, 0.0f);
    tag.lineTo (82.0f, bar.getBottom());
    tag.lineTo (0.0f, bar.getBottom());
    tag.closeSubPath();
    const auto tagColour = s != nullptr ? s->getColour() : juce::Colour (0xff7ee02e);
    g.setColour (tagColour);
    g.fillPath (tag);
    text (g, { 5.0f, 2.0f, 60.0f, 15.0f }, typeOf (slot), 11.0f, inkOn (tagColour), juce::Justification::centredLeft, true);
    text (g, { 5.0f, 16.0f, 66.0f, 15.0f }, s != nullptr ? s->getName() : juce::String(), 11.0f, inkOn (tagColour), juce::Justification::centredLeft, true);

    // The screen's title, on its white tab.
    juce::String title;
    switch (screen)
    {
        case Screen::Home:       title = "Home"; break;
        case Screen::Processing:
            switch (block)
            {
                case Block::Bank:   title = "Processing"; break;
                case Block::Preamp: title = "Preamp"; break;
                case Block::Hpf:    title = "Filter"; break;
                case Block::Gate:   title = "Gate"; break;
                case Block::Insert: title = "Insert"; break;
                case Block::Geq:    title = "GEQ"; break;
                case Block::Peq:    title = "PEQ"; break;
                case Block::Comp:   title = "Compressor"; break;
                default:            break;
            }
            break;
        case Screen::Routing:
            title = block == Block::DirOut ? "Ch/Mix Settings" : block == Block::MixSettings ? "Ch/Mix Settings" : block == Block::DcaMute ? "DCA/MG Assign" : block == Block::Sends ? "Sends" : "Routing";
            break;
        case Screen::Meters:     title = juce::StringArray ({ "Input Meters", "FX Meters", "Mix Meters", "USB Meters", "RTA", "Channel Meters" })[meterTab]; break;
        case Screen::Fx:         title = "FX"; break;
        case Screen::Scenes:     title = juce::StringArray ({ "Scene Manager", "Cue List Editor", "Global Filter", "Safes" })[sceneTab]; break;
        case Screen::Setup:
            // As the guide's pictures have them (pp.84-108).
            title = setupTab == 0   ? juce::StringArray ({ "PAFL", "Talkback", "Signal Generator", "Digital I/O", "I/O Port Options" })[audioTab]
                    : setupTab == 2 ? juce::StringArray ({ "Input Stereo", "Mix Stereo", "Bus Config", "Role" })[configTab]
                    : setupTab == 3 ? juce::String ("Ganging")
                    : setupTab == 4 ? juce::String ("Network Setup")
                    : setupTab == 5 ? juce::StringArray ({ "User Settings", "Setup", "Parameters", "Control" })[usersTab]
                                    : juce::StringArray ({ "Strip Assign", "Soft Controls", "Footswitch", "Surface Prefs" })[surfaceTab];
            break;
        case Screen::Utility:
            title = utilityTab == 0   ? juce::StringArray ({ "Diagnostics", "MIDI", "Add-ons", "Calibration" })[generalTab]
                    : utilityTab == 1 ? juce::StringArray ({ "Show Manager", "Scenes", "Libraries", "Sync" })[dataTab]
                    : utilityTab == 3 ? juce::StringArray ({ "Stereo", "Multitrack" })[driveTab]
                                      : juce::StringArray ({ "General", "Data", "USB Utility", "SQ Drive", "AMM" })[utilityTab];
            break;
        case Screen::Io:         title = juce::StringArray ({ "Input Patch", "Output Patch", "Tie Lines" })[ioTab]; break;
    }
    // Its white tab, cut along the channel tag's slant (p.11).
    juce::Path tab;
    tab.startNewSubPath (76.0f, 5.0f);
    tab.lineTo (202.0f, 5.0f);
    tab.lineTo (202.0f, 24.0f);
    tab.lineTo (86.0f, 24.0f);
    tab.closeSubPath();
    g.setColour (juce::Colours::white);
    g.fillPath (tab);
    text (g, { 92.0f, 5.0f, 106.0f, 19.0f }, title, 12.0f, juce::Colours::black, juce::Justification::centredLeft, true);

    // Current and next scene.
    auto& show = ctx.show;
    const int current = show.getCurrentSceneIndex();
    const auto nameOf = [&show] (int i)
    {
        const auto scene = show.getScene (i);
        return scene.has_value() ? scene->name : juce::String();
    };
    const auto currentName = current >= 0 ? nameOf (current) : juce::String ("No Scene");
    const auto nextName = nameOf (current + 1);
    text (g, { 220.0f, 2.0f, 110.0f, 14.0f }, "Current:", 10.5f, juce::Colours::white, juce::Justification::centredLeft, true);
    text (g, { 220.0f, 16.0f, 130.0f, 14.0f }, currentName.isNotEmpty() ? currentName : "No Scene", 10.5f, juce::Colours::white, juce::Justification::centredLeft);
    text (g, { 366.0f, 2.0f, 110.0f, 14.0f }, "Next:", 10.5f, juce::Colours::white, juce::Justification::centredLeft, true);
    text (g, { 366.0f, 16.0f, 130.0f, 14.0f }, nextName.isNotEmpty() ? nextName : "No Scene", 10.5f, juce::Colours::white, juce::Justification::centredLeft);

    // The selected mix, at the far right, in its channel colour (yellow for Main, p.34).
    auto* mixStrip = stripOf (ctx.engine, slotOfMix (selectedMix));
    const auto mixColour = selectedMix != 0 && mixStrip != nullptr && ! mixStrip->getColour().isTransparent() ? mixStrip->getColour() : kMixTag;
    juce::Path mix;
    mix.startNewSubPath ((float) kWidth, 0.0f);
    mix.lineTo ((float) kWidth - 62.0f, 0.0f);
    mix.lineTo ((float) kWidth - 78.0f, bar.getBottom());
    mix.lineTo ((float) kWidth, bar.getBottom());
    mix.closeSubPath();
    g.setColour (mixColour);
    g.fillPath (mix);
    text (g, { (float) kWidth - 60.0f, 2.0f, 56.0f, 15.0f }, mixType (selectedMix), 11.0f, inkOn (mixColour), juce::Justification::centredRight, true);
    text (g, { (float) kWidth - 70.0f, 16.0f, 66.0f, 15.0f }, mixName (ctx.engine, selectedMix), 11.0f, inkOn (mixColour), juce::Justification::centredRight, true);

    g.setColour (kLine);
    g.drawHorizontalLine (kTitleBar, 0.0f, (float) kWidth);
}

void Display::paintHome (juce::Graphics& g)
{
    // The console's name large and faint behind the buttons, the version under it.
    text (g, { 0.0f, 118.0f, (float) kWidth, 110.0f }, "SQ-5", 96.0f, juce::Colours::white.withAlpha (0.14f), juce::Justification::centred, true);
    text (g, { 0.0f, 232.0f, (float) kWidth, 18.0f }, "Sidecar", 12.0f, juce::Colours::white.withAlpha (0.6f));
    text (g, { 0.0f, 358.0f, (float) kWidth, 18.0f }, "Current User: Admin", 11.0f, juce::Colours::white.withAlpha (0.65f));
}

void Display::paintColumn (juce::Graphics& g, juce::Rectangle<float> col, const Slot& slot, bool isSelected, bool overview)
{
    g.setColour (isSelected ? kColumnSelected : kColumn);
    g.fillRoundedRectangle (col, 3.0f);
    g.setColour (kLine.withAlpha (0.6f));
    g.drawRoundedRectangle (col, 3.0f, 0.8f);

    auto* s = stripOf (ctx.engine, slot);
    const auto rows = rowsOf (slot);
    const auto l = layoutOf (col, rows);

    // The name, on the channel's colour.
    if (slot.kind == Slot::Dca)
    {
        const auto& dca = ctx.engine.getDca (slot.index);
        const auto dcaColour = consoleColour (dca.colour.load());
        g.setColour (dcaColour);
        g.fillRect (l.name.reduced (1.0f, 1.0f));
        const auto name = ctx.engine.getDcaName (slot.index);
        text (g, l.name, name.isNotEmpty() ? name : "DCA" + juce::String (slot.index + 1), 12.0f, inkOn (dcaColour), juce::Justification::centred, true);
        // A DCA shows its members instead (p.49).
        auto rest = col.withTrimmedTop (l.name.getHeight() + 2.0f).reduced (4.0f, 2.0f);
        text (g, rest.removeFromTop (14.0f), "Members", 10.0f, juce::Colours::white, juce::Justification::centred, true);
        int count = 0;
        for (const auto& c : ctx.engine.getChannels())
            if (c->params.dcaMask.contains (slot.index))
            {
                if (++count > 28)
                {
                    text (g, rest.removeFromTop (10.0f), juce::String::fromUTF8 ("\xe2\x80\xa6"), 10.0f, juce::Colours::white);
                    break;
                }
                text (g, rest.removeFromTop (10.0f), c->getName(), 9.5f, juce::Colours::white);
            }
        return;
    }
    if (s == nullptr)
        return;

    const auto stripColour = s->getColour();
    g.setColour (stripColour);
    g.fillRect (l.name.reduced (1.0f, 1.0f));
    text (g, l.name, s->getName(), 12.0f, inkOn (stripColour), juce::Justification::centred, true);

    auto& p = s->params;
    // The source, then the preamp's gain and the input's meter.
    if (rows.preamp)
    {
        const int in = p.inputLeft.load();
        text (g, l.source, in >= 0 ? (s->isInput() ? "Local " + juce::String (in + 1) : juce::String ("Mix Ext In")) : juce::String ("Unassigned"), 10.0f, juce::Colours::white);
        auto r = l.preamp.reduced (4.0f, 2.0f);
        auto meter = r.removeFromRight (7.0f);
        levelBar (g, meter.withWidth (3.0f), ctx.meters.get (s, MeterPoint::Input, 0));
        levelBar (g, meter.withTrimmedLeft (4.0f), ctx.meters.get (s, MeterPoint::Input, 1));
        auto* src = preampOf (ctx.engine, *s);
        const float gain = src != nullptr ? src->gainDb.load() : p.input.trimDb.load();
        miniKnob (g, r.removeFromLeft (r.getHeight()).reduced (2.0f), src != nullptr ? gain / 60.0f : (gain + 24.0f) / 48.0f, juce::Colours::black);
        auto value = r;
        text (g, value.removeFromTop (r.getHeight() * 0.62f), gainText (gain), 15.0f, juce::Colours::white, juce::Justification::centred, true);
        text (g, value, "dB", 10.0f, juce::Colours::white);
        // 48V, PAD, POL, DLY, AMM and TIE.
        auto flagArea = l.flags.reduced (4.0f, 1.0f);
        const float flagRow = flagArea.getHeight() / 3.0f;
        auto rowA = flagArea.removeFromTop (flagRow), rowB = flagArea.removeFromTop (flagRow), rowC = flagArea;
        const auto flag = [&] (juce::Rectangle<float> r2, const char* name, bool on, juce::Colour c)
        {
            g.setColour (juce::Colours::black.withAlpha (on ? 0.9f : 0.3f));
            g.fillRect (r2.reduced (1.0f, 0.5f));
            text (g, r2, name, 9.0f, on ? c : juce::Colours::white.withAlpha (0.35f), juce::Justification::centred, true);
        };
        const auto w = rowA.getWidth() * 0.5f;
        flag (rowA.removeFromLeft (w), "48V", src != nullptr && src->phantom.load(), juce::Colours::red);
        flag (rowA, "PAD", false, kYellow);
        flag (rowB.removeFromLeft (w), "POL", p.input.invert.load(), kYellow);
        flag (rowB, "DLY", p.input.delayOn.load(), juce::Colours::orange);
        flag (rowC.removeFromLeft (w), "AMM", false, kYellow); // what Sidecar has no Automatic Mic Mixer for
        flag (rowC, "TS", false, kYellow);
    }
    else if (slot.kind == Slot::FxReturn || slot.kind == Slot::FxSend)
    {
        text (g, l.source, slot.kind == Slot::FxReturn ? "FX " + juce::String (slot.index + 1) : "FX Send", 10.0f, juce::Colours::white);
    }

    if (rows.hpf)
    {
        auto& f = p.filter();
        auto r = l.hpf.reduced (5.0f, 1.0f);
        auto label = r.removeFromTop (r.getHeight() * 0.45f);
        g.setColour (juce::Colours::white);
        g.fillRect (label);
        text (g, label, "HPF", 9.5f, juce::Colours::black, juce::Justification::centred, true);
        g.setColour (f.lowCut.load() ? kWell : kWell.withAlpha (0.4f));
        g.fillRect (r);
        text (g, r, formatHz (f.lowCutFreq.load()) + "Hz", 10.5f, f.lowCut.load() ? juce::Colours::white : juce::Colours::white.withAlpha (0.4f));
    }
    const auto dynamics = [&] (juce::Rectangle<float> r, ld::DynSlotParams& d, float reduction, bool rms)
    {
        const bool on = d.on.load();
        auto w = well (g, r, on);
        auto meter = w.removeFromLeft (4.0f);
        g.setColour (juce::Colours::black);
        g.fillRect (meter);
        g.setColour (juce::Colours::red);
        g.fillRect (meter.withHeight (meter.getHeight() * juce::jlimit (0.0f, 1.0f, reduction / 36.0f))); // gain reduction, from the top
        drawTransfer (g, w.reduced (1.0f), d, on ? kCurve : kCurve.withAlpha (0.35f), 1.2f);
        if (rms)
            text (g, w.removeFromTop (10.0f).reduced (2.0f, 0.0f), "RMS", 8.5f, juce::Colours::white.withAlpha (on ? 1.0f : 0.5f), juce::Justification::centredLeft, true);
    };
    if (rows.gate)
        dynamics (l.gate, p.gate(), s->meters.gateReductionDb.load(), false);
    if (rows.insert)
    {
        auto r = l.insert.reduced (5.0f, 1.5f);
        const bool on = p.preInsert().on.load();
        g.setColour (on ? kGreen.withAlpha (0.85f) : juce::Colour (0xff5595c0).withAlpha (0.55f));
        g.fillRoundedRectangle (r, 3.0f);
        auto dot = r.removeFromRight (r.getHeight()).reduced (4.0f);
        text (g, r, "Insert", 10.5f, on ? juce::Colours::black : juce::Colours::white.withAlpha (0.8f), juce::Justification::centred, true);
        g.setColour (on ? kGreen.brighter (0.3f) : juce::Colours::white.withAlpha (0.25f));
        g.fillEllipse (dot);
    }
    if (rows.peq || rows.geq)
    {
        const auto& freqs = curveFreqs();
        std::array<double, 48> db {};
        if (rows.geq)
        {
            auto w = well (g, l.geq, false);
            text (g, w, "GEQ", 9.5f, juce::Colours::white.withAlpha (0.55f));
        }
        const bool on = p.eq().on.load();
        auto w = well (g, l.peq, on);
        ld::ld_strip_response (s->getHandle(), 1, freqs.data(), db.data(), (int32_t) db.size(), 48000.0);
        drawCurve (g, w, db.data(), freqs.data(), (int) db.size(), 18.0f, on ? kCurve : kCurve.withAlpha (0.35f), on);
    }
    if (rows.comp)
    {
        const auto& d = p.dynamics();
        const auto& model = models::model (models::Dynamics, d.model.load());
        const int det = model.find ("det");
        dynamics (l.comp, p.dynamics(), s->meters.dynReductionDb.load(), det >= 0 && d.values[det].load() < 0.5f);
    }
    if (rows.pan)
    {
        auto r = l.pan.reduced (6.0f, 2.0f);
        text (g, r.removeFromBottom (11.0f), "MainLR", 9.5f, juce::Colours::white.withAlpha (0.8f));
        text (g, r.removeFromTop (11.0f), "Pan", 9.5f, juce::Colours::white, juce::Justification::centred, true);
        const float pan = p.pan.load() / 100.0f;
        auto arc = r.withTrimmedTop (-2.0f);
        const auto cx = arc.getCentreX(), base = arc.getBottom() + 12.0f, radius = arc.getWidth() * 0.55f;
        juce::Path back, front;
        const float a0 = -0.9f, a1 = 0.9f, at = 0.9f * pan;
        back.addCentredArc (cx, base, radius, radius, 0.0f, a0, a1, true);
        front.addCentredArc (cx, base, radius, radius, 0.0f, juce::jmin (0.0f, at), juce::jmax (0.0f, at), true);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (back, juce::PathStrokeType (5.0f));
        g.setColour (juce::Colours::white);
        g.strokePath (front, juce::PathStrokeType (5.0f));
        const float px = cx + std::sin (at) * radius, py = base - std::cos (at) * radius;
        g.fillEllipse (px - 2.5f, py - 2.5f, 5.0f, 5.0f);
    }
    juce::ignoreUnused (overview);
}

void Display::paintBank (juce::Graphics& g)
{
    const float cw = (float) kWidth / kColumns;
    const juce::Rectangle<float> all (0.0f, (float) kTitleBar + 2.0f, cw, (float) kHeight - kTitleBar - 2.0f);
    const auto sel = getSelectedSlot ? getSelectedSlot() : Slot {};
    for (int c = 0; c < kColumns; ++c)
    {
        const int at = bankFirst + c;
        if (at >= (int) layer.size())
            break;
        const auto& slot = layer[(size_t) at];
        const auto col = all.withX ((float) c * cw).reduced (1.5f, 0.0f);
        if (slot.kind == Slot::None)
        {
            g.setColour (kColumnDim.withAlpha (0.5f));
            g.fillRoundedRectangle (col, 3.0f);
            continue;
        }
        paintColumn (g, col, slot, slot == sel, false);
    }
}

void Display::paintResponse (juce::Graphics& g, juce::Rectangle<float> r, Strip& s, int blockId)
{
    g.setColour (kWell);
    g.fillRect (r);
    // The log grid and frequencies (pp.34, 40).
    for (float decade : { 10.0f, 100.0f, 1000.0f, 10000.0f })
        for (int k = 2; k < 10; ++k)
        {
            const float hz = decade * (float) k;
            if (hz < 20.0f || hz > 20000.0f)
                continue;
            const auto x = logX (r, hz);
            g.setColour (kLine.withAlpha (k == 2 || k == 5 ? 0.3f : 0.14f));
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        }
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const auto x = logX (r, hz);
        g.setColour (kLine.withAlpha (0.28f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        text (g, { x - 18.0f, r.getBottom() - 11.0f, 36.0f, 10.0f }, formatHz (hz), 8.5f, juce::Colours::white.withAlpha (0.7f));
    }
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const auto y = r.getCentreY() - r.getHeight() * 0.5f * db / 18.0f;
        g.setColour (kLine.withAlpha (db == 0.0f ? 0.6f : 0.25f));
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        text (g, { r.getX() + 4.0f, y - 11.0f, 26.0f, 10.0f }, (db > 0.0f ? "+" : "") + juce::String ((int) db), 8.5f, juce::Colours::white.withAlpha (0.7f), juce::Justification::centredLeft);
    }
    const auto& freqs = curveFreqs();
    std::array<double, 48> db {};
    if (blockId == 1 && s.params.filter().lowCut.load())
    {
        // The HPF's slope shows green under the PEQ's curve (p.40).
        std::array<double, 48> hpf {};
        ld::ld_strip_response (s.getHandle(), 3, freqs.data(), hpf.data(), (int32_t) hpf.size(), 48000.0);
        drawCurve (g, r, hpf.data(), freqs.data(), (int) hpf.size(), 18.0f, kFill.brighter (0.2f), true);
    }
    ld::ld_strip_response (s.getHandle(), blockId, freqs.data(), db.data(), (int32_t) db.size(), 48000.0);
    if (blockId == 1)
    {
        // Brightly coloured while the PEQ is in, greyed out while it's not (p.40).
        const bool on = s.params.eq().on.load();
        drawCurve (g, r, db.data(), freqs.data(), (int) db.size(), 18.0f, on ? kCurve : juce::Colours::white.withAlpha (0.4f), true, on ? juce::Colour (0xff6b4fd0) : juce::Colours::grey);
    }
    else
        drawCurve (g, r, db.data(), freqs.data(), (int) db.size(), 18.0f, kCurve, true);
}

void Display::paintTransfer (juce::Graphics& g, juce::Rectangle<float> r, ld::DynSlotParams& d)
{
    g.setColour (kWell);
    g.fillRect (r);
    for (int i = 1; i < 4; ++i)
    {
        g.setColour (kLine.withAlpha (0.25f));
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) i / 4.0f), r.getY(), r.getBottom());
        g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());
    }
    drawTransfer (g, r, d, kCurve, 1.6f);
}

void Display::paintHistory (juce::Graphics& g, juce::Rectangle<float> r, bool gate)
{
    g.setColour (kWell);
    g.fillRect (r);
    const float w = r.getWidth() / (float) kHistory;
    for (int i = 0; i < kHistory; ++i)
    {
        const size_t at = (size_t) ((historyAt + i) % kHistory);
        const float f = juce::jlimit (0.0f, 1.0f, (historyDb[at] + 72.0f) / 72.0f);
        const float x = r.getX() + (float) i * w;
        g.setColour (juce::Colour (0xffb03030));
        g.fillRect (x, r.getBottom() - r.getHeight() * f, w, r.getHeight() * f);
        const float gr = juce::jlimit (0.0f, 1.0f, historyGr[at] / 36.0f);
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.fillRect (x, r.getY(), w, r.getHeight() * gr);
    }
    juce::ignoreUnused (gate);
}

void Display::paintLevel (juce::Graphics& g, juce::Rectangle<float> r, Strip& s)
{
    levelBar (g, r.withWidth (5.0f), ctx.meters.get (&s, MeterPoint::Trim, 0));
    levelBar (g, r.withTrimmedLeft (6.0f).withWidth (5.0f), ctx.meters.get (&s, MeterPoint::Trim, 1));
}

void Display::paintGraph (juce::Graphics& g)
{
    if (block == Block::Comp)
        if (auto* strip = selectedStrip(); strip != nullptr && strip->params.dynamics().model.load() >= kFirstDeepComp)
            return; // a DEEP model's page is its controls
    auto* s = selectedStrip();
    if (s == nullptr)
        return;
    auto& p = s->params;
    switch (block)
    {
        case Block::Preamp:
            text (g, { 100.0f, 150.0f, 72.0f, 11.0f }, "(Hold)", 9.0f, juce::Colours::white, juce::Justification::centred, true);
            paintLevel (g, { 396.0f, 150.0f, 12.0f, 240.0f }, *s);
            break;
        case Block::Insert:
            // Each socket's caption (p.37).
            text (g, { 106.0f, 86.0f, 192.0f, 12.0f }, "Ip Insert Send, Left", 9.0f, juce::Colours::white.withAlpha (0.7f));
            text (g, { 106.0f, 214.0f, 192.0f, 12.0f }, "Ip Insert Return", 9.0f, juce::Colours::white.withAlpha (0.7f));
            text (g, { 200.0f, 318.0f, 150.0f, 18.0f }, "Operating Level:", 10.5f, juce::Colours::white, juce::Justification::centredRight, true);
            paintLevel (g, { 636.0f, 58.0f, 12.0f, 260.0f }, *s);
            break;
        case Block::Bank:
            paintLevel (g, { 636.0f, 58.0f, 12.0f, 330.0f }, *s);
            break;
        case Block::Hpf:
            paintResponse (g, { 92.0f, 38.0f, 546.0f, 274.0f }, *s, 3);
            paintLevel (g, { 646.0f, 38.0f, 12.0f, 274.0f }, *s);
            break;
        case Block::Gate:
            paintTransfer (g, { 240.0f, 100.0f, 290.0f, 180.0f }, p.gate());
            paintLevel (g, { 225.0f, 100.0f, 12.0f, 180.0f }, *s);
            paintHistory (g, { 240.0f, 312.0f, 290.0f, 76.0f }, true);
            break;
        case Block::Comp:
            paintTransfer (g, { 240.0f, 100.0f, 290.0f, 180.0f }, p.dynamics());
            paintLevel (g, { 225.0f, 100.0f, 12.0f, 180.0f }, *s);
            paintHistory (g, { 240.0f, 312.0f, 290.0f, 76.0f }, false);
            break;
        case Block::Peq:
        {
            paintResponse (g, { 92.0f, 36.0f, 546.0f, 206.0f }, *s, 1);
            paintLevel (g, { 646.0f, 36.0f, 12.0f, 206.0f }, *s);
            // The bands' panels and coloured names, under the graph (p.40).
            const struct { const char* name; float x, w; juce::Colour c; } panels[] = {
                { "LF", 92.0f, 154.0f, juce::Colour (0xffd97cf0) }, { "LM", 256.0f, 96.0f, juce::Colour (0xff78d6ec) },
                { "HM", 363.0f, 96.0f, juce::Colour (0xff5b9dff) }, { "HF", 470.0f, 152.0f, juce::Colour (0xffff4d4d) }
            };
            for (const auto& b : panels)
            {
                juce::Rectangle<float> panel (b.x, 246.0f, b.w, 150.0f);
                g.setColour (kColumnDim);
                g.fillRoundedRectangle (panel, 3.0f);
                g.setColour (kLine.withAlpha (0.7f));
                g.drawRoundedRectangle (panel, 3.0f, 0.8f);
                auto head = panel.removeFromTop (18.0f);
                g.setColour (kBar);
                g.fillRect (head.reduced (1.0f));
                text (g, head, b.name, 12.0f, b.c, juce::Justification::centred, true);
            }
            break;
        }
        case Block::DirOut:
            // The follow options' names and the source's caption (p.46).
            text (g, { 230.0f, 88.0f, 416.0f, 14.0f }, "Direct Out Source", 10.5f, juce::Colours::white, juce::Justification::centred, true);
            for (auto [name, x, y] : { std::tuple ("Follow Fader", 288.0f, 170.0f), std::tuple ("Follow Mute", 413.0f, 170.0f),
                                       std::tuple ("Follow DCA Fader", 288.0f, 236.0f), std::tuple ("Follow DCA Mute", 413.0f, 236.0f),
                                       std::tuple ("Follow Mute Group", 413.0f, 302.0f) })
                text (g, { x - 28.0f, y, 120.0f, 13.0f }, name, 10.0f, juce::Colours::white, juce::Justification::centred, true);
            break;
        case Block::MixSettings:
        case Block::DcaMute:
        case Block::Sends:
            break;
        case Block::Geq:
        {
            // The sliders on their grid (p.38).
            g.setColour (kWell);
            g.fillRect (juce::Rectangle<float> (96.0f, 64.0f, 532.0f, 260.0f));
            for (const auto& f : fields)
            {
                if (! f.title.startsWith ("GEQ ") || ! f.binding.isValid())
                    continue;
                auto r = f.area.reduced (f.area.getWidth() * 0.3f, 0.0f).withTrimmedBottom (16.0f);
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.fillRect (r.withSizeKeepingCentre (1.5f, r.getHeight()));
                const bool sel = selected >= 0 && &fields[(size_t) selected] == &f;
                const float y = r.getBottom() - r.getHeight() * f.binding.get();
                g.setColour (sel ? kYellow : juce::Colours::white);
                g.fillRect (r.getX() - 2.0f, y - 3.0f, r.getWidth() + 4.0f, 6.0f);
                text (g, f.area.withTop (f.area.getBottom() - 16.0f), f.binding.label, 8.0f, juce::Colours::white);
            }
            break;
        }
    }
}

void Display::paintDetail (juce::Graphics& g)
{
    if (libraryOpen)
        return;
    if (auto* s = selectedStrip(); s != nullptr && screen == Screen::Processing)
    {
        const juce::Rectangle<float> col (1.5f, (float) kTitleBar + 2.0f, kOverviewWidth - 3.0f, (float) kHeight - kTitleBar - 2.0f);
        paintColumn (g, col, slotOf (ctx.engine, s), true, true);
    }
    if (auto* s = selectedStrip(); s != nullptr && screen == Screen::Routing)
    {
        const juce::Rectangle<float> col (1.5f, (float) kTitleBar + 2.0f, kOverviewWidth - 3.0f, (float) kHeight - kTitleBar - 2.0f);
        paintRoutingColumn (g, col, slotOf (ctx.engine, s), true);
    }
    paintGraph (g);
}

void Display::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (kScreenTop, 0.0f, 0.0f, kScreenBottom, 0.0f, (float) kHeight, false));
    g.fillAll();
    paintTitleBar (g);

    switch (screen)
    {
        case Screen::Home:
            paintHome (g);
            break;
        case Screen::Processing:
            if (block == Block::Bank)
                paintBank (g);
            else
                paintDetail (g);
            break;
        case Screen::Routing:
            if (block == Block::Bank)
                paintRoutingBank (g);
            break;
        case Screen::Meters:
            paintMeters (g);
            break;
        case Screen::Scenes:
            paintScenes (g);
            break;
        case Screen::Utility:
            paintUtility (g);
            break;
        case Screen::Io:
            paintIo (g);
            break;
        case Screen::Setup:
            paintSetup (g);
            break;
        case Screen::Fx:
            break;
    }
    for (int i = 0; i < (int) fields.size(); ++i)
        paintField (g, fields[(size_t) i], i == selected);

    if (locked)
    {
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.fillRect (getLocalBounds().withTrimmedTop (kTitleBar));
        text (g, getLocalBounds().toFloat().withTrimmedTop ((float) kTitleBar), "Locked: touch to unlock", 22.0f, juce::Colours::white);
    }
}

//==============================================================================
void Display::mouseDown (const juce::MouseEvent& e)
{
    dragged = false;
    pressed = -1;
    dragStartBank = bankFirst;
    dragStartOther = screen == Screen::Scenes ? sceneFirst : screen == Screen::Io ? ioFirst : screen == Screen::Meters ? meterFirst : sendFirst;
    dragList = nullptr;
    if (locked)
    {
        setLocked (false);
        return;
    }
    const auto at = e.position;
    if (const auto* list = listAt (at))
    {
        dragList = list->first;
        dragListAcross = list->across;
        dragStartOther = *dragList;
    }
    for (int i = (int) fields.size() - 1; i >= 0; --i)
        if (fields[(size_t) i].area.contains (at) && fields[(size_t) i].enabled)
        {
            pressed = i;
            if (fields[(size_t) i].binding.isValid())
            {
                select (i);
                dragStartValue = fields[(size_t) i].binding.get();
            }
            return;
        }
}

void Display::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 4)
        return;
    dragged = true;
    if (juce::isPositiveAndBelow (pressed, (int) fields.size()) && fields[(size_t) pressed].binding.isValid())
    {
        // A value follows a drag up and down.
        const auto& f = fields[(size_t) pressed];
        const float span = juce::jmax (120.0f, f.area.getHeight());
        f.binding.set (juce::jlimit (0.0f, 1.0f, dragStartValue - (float) e.getDistanceFromDragStartY() / span));
        repaint();
        return;
    }
    // Drag left or right: all the channels of the layer (p.29), a channel's sends (p.50);
    // up and down: the scenes, the patch.
    const int stepX = juce::roundToInt ((float) e.getDistanceFromDragStartX() / ((float) kWidth / kColumns));
    const int stepY = juce::roundToInt ((float) e.getDistanceFromDragStartY() / 26.0f);
    if (dragList != nullptr)
    {
        // A list's rows, up and down (pp.80, 107), or Ganging's channels, left and right
        // (p.105); the page keeps it in range.
        const float rows = dragListAcross ? (float) e.getDistanceFromDragStartX() / 54.0f : (float) e.getDistanceFromDragStartY() / 34.0f;
        if (const int first = dragStartOther - juce::roundToInt (rows); first != *dragList)
        {
            *dragList = first;
            rebuild();
        }
    }
    else if ((screen == Screen::Processing || screen == Screen::Routing) && block == Block::Bank)
        setBankFirst (dragStartBank - stepX);
    else if (screen == Screen::Routing && block == Block::Sends && sendFirst != dragStartOther - stepX)
    {
        sendFirst = dragStartOther - stepX;
        rebuild();
    }
    else if (screen == Screen::Meters && meterFirst != dragStartOther - stepY / 5)
    {
        // Up and down through the rows of meters (p.53).
        meterFirst = dragStartOther - stepY / 5;
        repaint();
    }
    else if (screen == Screen::Scenes && sceneFirst != dragStartOther - stepY)
    {
        sceneFirst = dragStartOther - stepY;
        rebuild();
    }
    else if (screen == Screen::Io && ioFirst != dragStartOther - stepY)
    {
        ioFirst = dragStartOther - stepY;
        rebuild();
    }
}

void Display::mouseUp (const juce::MouseEvent&)
{
    const int touched = pressed;
    pressed = -1;
    dragList = nullptr;
    // Last: what a touch does can take the surface away (touchField).
    if (! dragged && juce::isPositiveAndBelow (touched, (int) fields.size()) && ! fields[(size_t) touched].binding.isValid())
        touchField (touched);
}

void Display::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (const auto* list = listAt (e.position))
    {
        *list->first += w.deltaY > 0 ? -1 : 1;
        rebuild();
        return;
    }
    const auto b = getRotaryBinding();
    if (! b.isValid())
        return;
    const float step = b.steps > 1.0f ? 1.0f / (b.steps - 1.0f) : 0.01f;
    b.set (juce::jlimit (0.0f, 1.0f, b.get() + (w.deltaY > 0 ? step : -step)));
    repaint();
}

} // namespace livedaw::ui::sq5
