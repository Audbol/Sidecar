// The SQ-5 touchscreen's other screens: Routing (Reference Guide pp.45-51), Meters
// (pp.52-59), FX (pp.60-65), Scenes (pp.66-79), Setup (pp.84-110), Utility (pp.12, 111-125)
// and I/O (pp.23-28). A control Sidecar doesn't model is drawn dim and does nothing.

#include "ui/Sq5Display.h"

#include "engine/MixerEngine.h"
#include "engine/Models.h"
#include "engine/SqMidi.h"
#include "ui/MeterBank.h"

#include <cmath>

namespace livedaw::ui::sq5
{

using namespace universal;

namespace
{
    const juce::Colour kWell (0xff071019), kLine (0xff8fbcd9), kColumn (0xff3f82b3), kColumnSelected (0xff4aa56f), kPink (0xffe05cc8), kGreenBar (0xff7ee02e),
        kCurrent (0xff8fd14f), kNext (0xffb4c24a), kOffCell (0xff2c5f8a), kOnCell (0xffd9e6f2);
    constexpr float kTabWidth = 70.0f;
    /** I/O's Outputs: LR's sides, then Mix 1-12 (their first side). */
    constexpr int outputRows = 2 + kMixes;
    int outputRowSource (int row) { return row < 2 ? MixerEngine::stripSource (StripKind::Main, 0, row) : MixerEngine::stripSource (StripKind::Bus, row - 2, 0); }

    void text (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, float size, juce::Colour c,
               juce::Justification j = juce::Justification::centred, bool bold = false)
    {
        g.setColour (c);
        g.setFont (type::font (size, bold));
        g.drawFittedText (t, r.getSmallestIntegerContainer(), j, 2, 0.75f);
    }

    juce::Colour inkOn (juce::Colour background) { return background.getPerceivedBrightness() > 0.35f ? juce::Colours::black : juce::Colours::white; }

    /** The body beside the side tabs. */
    juce::Rectangle<float> bodyArea() { return { kTabWidth + 6.0f, (float) Display::kTitleBar + 4.0f, (float) Display::kWidth - kTabWidth - 10.0f, (float) Display::kHeight - Display::kTitleBar - 8.0f }; }

    void meterBar (juce::Graphics& g, juce::Rectangle<float> r, float gain)
    {
        g.setColour (juce::Colours::black);
        g.fillRect (r);
        const float db = juce::Decibels::gainToDecibels (gain, -60.0f), f = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        g.setColour (db > -3.0f ? juce::Colours::red : db > -12.0f ? juce::Colours::yellow : kGreenBar);
        g.fillRect (r.withTop (r.getBottom() - r.getHeight() * f));
    }

    /** A send's bar in the Routing bank view: filled if assigned, outlined if not, pink
        post-fade and green pre-fade (p.45). */
    void sendBar (juce::Graphics& g, juce::Rectangle<float> r, float db, bool assigned, bool pre)
    {
        const float f = (float) fader::dbToPos (db);
        const auto c = pre ? kGreenBar : kPink;
        const auto bar = r.withTop (r.getBottom() - r.getHeight() * f);
        if (assigned)
        {
            g.setColour (c);
            g.fillRect (bar);
        }
        else
        {
            g.setColour (c.withAlpha (0.8f));
            g.drawRect (bar, 0.8f);
        }
    }

    juce::StringArray softKeyNames()
    {
        juce::StringArray names { "Unassigned", "Scene Go Next", "Scene Go Previous", "Talk" };
        for (int m = 1; m <= 8; ++m)
            names.add ("Mute Group " + juce::String (m));
        return names;
    }
} // namespace

namespace soft
{
    juce::StringArray functions() { return softKeyNames(); }

    int get (juce::PropertiesFile& settings, int key)
    {
        auto list = juce::StringArray::fromTokens (settings.getValue (settingsKey), ",", {});
        return juce::isPositiveAndBelow (key, list.size()) ? list[key].getIntValue() : 0;
    }

    void set (juce::PropertiesFile& settings, int key, int function)
    {
        auto list = juce::StringArray::fromTokens (settings.getValue (settingsKey), ",", {});
        while (list.size() < 8)
            list.add ("0");
        list.set (key, juce::String (function));
        settings.setValue (settingsKey, list.joinIntoString (","));
    }
} // namespace soft

//==============================================================================
Field& Display::addTab (juce::Rectangle<float> area, const juce::String& title, int& tab, int value)
{
    auto& f = add (Field::Tab, area, title);
    auto* t = &tab;
    f.lit = [t, value] { return *t == value; };
    f.press = [this, t, value]
    {
        *t = value;
        rebuild();
    };
    return f;
}

void Display::addSideTabs (const juce::StringArray& names, int& tab)
{
    const float top = (float) kTitleBar + 4.0f, h = ((float) kHeight - top - 4.0f) / (float) juce::jmax (6, names.size());
    for (int i = 0; i < names.size(); ++i)
        addTab ({ 2.0f, top + (float) i * h, kTabWidth, h - 3.0f }, names[i], tab, i);
}

//==============================================================================
// Routing

void Display::buildRoutingBank()
{
    const float cw = (float) kWidth / kColumns;
    const float top = (float) kTitleBar + 2.0f;
    for (int c = 0; c < kColumns; ++c)
    {
        const int at = bankFirst + c;
        if (at >= (int) layer.size())
            break;
        const auto slot = layer[(size_t) at];
        if (slot.kind == Slot::None)
            continue;
        const float x = (float) c * cw + 1.5f, w = cw - 3.0f;
        const auto choose = [this, slot] { if (onSelectSlot) onSelectSlot (slot); };
        add (Field::Area, { x, top, w, 22.0f }, {}).press = choose;
        if (slot.kind == Slot::Dca)
            continue;
        const auto open = [this, choose] (Block b)
        {
            return [this, choose, b]
            {
                choose();
                showBlock (b);
            };
        };
        add (Field::Area, { x, top + 24.0f, w, 22.0f }, {}).press = open (slot.kind == Slot::Input ? Block::DirOut : Block::MixSettings);
        add (Field::Area, { x, top + 50.0f, w, 66.0f }, {}).press = open (Block::DcaMute);
        add (Field::Area, { x, top + 120.0f, w, (float) kHeight - top - 120.0f }, {}).press = open (Block::Sends);
    }
}

void Display::paintRoutingBank (juce::Graphics& g)
{
    const float cw = (float) kWidth / kColumns;
    const float top = (float) kTitleBar + 2.0f, height = (float) kHeight - top;
    const auto sel = getSelectedSlot ? getSelectedSlot() : Slot {};
    for (int c = 0; c < kColumns; ++c)
    {
        const int at = bankFirst + c;
        if (at >= (int) layer.size())
            break;
        const auto slot = layer[(size_t) at];
        juce::Rectangle<float> col ((float) c * cw + 1.5f, top, cw - 3.0f, height);
        g.setColour (slot == sel && slot.kind != Slot::None ? kColumnSelected : kColumn);
        g.fillRoundedRectangle (col, 3.0f);
        if (slot.kind == Slot::None)
            continue;
        auto* s = stripOf (ctx.engine, slot);
        auto r = col;
        auto name = r.removeFromTop (22.0f);
        const auto colour = s != nullptr ? s->getColour() : consoleColour (ctx.engine.getDca (slot.index).colour.load());
        g.setColour (colour);
        g.fillRect (name.reduced (1.0f));
        const auto label = s != nullptr ? s->getName() : ctx.engine.getDcaName (slot.index).isNotEmpty() ? ctx.engine.getDcaName (slot.index) : "DCA" + juce::String (slot.index + 1);
        text (g, name, label, 12.0f, inkOn (colour), juce::Justification::centred, true);
        if (s == nullptr)
            continue;
        // Dir Out, or the mix's type.
        auto button = r.removeFromTop (24.0f).reduced (6.0f, 3.0f);
        g.setColour (juce::Colour (0xff24598a));
        g.fillRoundedRectangle (button, 3.0f);
        g.setColour (kLine);
        g.drawRoundedRectangle (button, 3.0f, 0.8f);
        text (g, button, slot.kind == Slot::Input ? "Dir Out" : slot.kind == Slot::Mix ? "Aux Snd" : slot.kind == Slot::FxSend ? "FX Snd" : "Main Snd", 10.5f, juce::Colours::white, juce::Justification::centred, true);

        // DCA and mute group assignments, the numbers filled when assigned (p.45).
        const auto group = [&] (const char* title, ld::GroupMask& mask)
        {
            text (g, r.removeFromTop (12.0f), title, 9.5f, juce::Colours::white, juce::Justification::centred, true);
            auto grid = r.removeFromTop (20.0f).reduced (5.0f, 1.0f);
            const float w = grid.getWidth() / 4.0f, h = grid.getHeight() * 0.5f;
            for (int i = 0; i < 8; ++i)
            {
                juce::Rectangle<float> cell (grid.getX() + (float) (i % 4) * w, grid.getY() + (float) (i / 4) * h, w, h);
                const bool on = mask.contains (i);
                g.setColour (on ? juce::Colours::white : juce::Colour (0xff24598a));
                g.fillRect (cell.reduced (0.6f));
                if (on)
                    text (g, cell, juce::String (i + 1), 8.5f, juce::Colours::black, juce::Justification::centred, true);
            }
        };
        group ("DCA Assign", s->params.dcaMask);
        group ("Mute Assign", s->params.muteGroupMask);

        if (s->isOutput() && slot.kind != Slot::Mix)
            continue;
        // Main, Mix and FX sends (only the channels send to them).
        const bool sends = s->isInput();
        text (g, r.removeFromTop (12.0f), "Main Sends", 9.5f, juce::Colours::white, juce::Justification::centred, true);
        {
            auto bar = r.removeFromTop (32.0f).reduced (24.0f, 1.0f);
            g.setColour (kWell);
            g.fillRect (bar);
            const auto& lr = s->mains[0];
            sendBar (g, bar.reduced (2.0f, 1.0f), s->params.faderDb.load(), lr.on.load(), lr.pre.load());
            text (g, { bar.getX(), bar.getBottom() - 11.0f, bar.getWidth(), 10.0f }, "LR", 8.5f, juce::Colours::white);
        }
        if (! sends)
            continue;
        const auto sendRow = [&] (const char* title, int first, int count)
        {
            text (g, r.removeFromTop (12.0f), title, 9.5f, juce::Colours::white, juce::Justification::centred, true);
            auto bars = r.removeFromTop (70.0f).reduced (5.0f, 1.0f);
            g.setColour (kWell);
            g.fillRect (bars);
            const float w = bars.getWidth() / (float) count;
            for (int i = 0; i < count; ++i)
            {
                auto& send = s->sends[(size_t) (first + i)];
                sendBar (g, juce::Rectangle<float> (bars.getX() + (float) i * w + 0.5f, bars.getY() + 1.0f, w - 1.0f, bars.getHeight() - 2.0f),
                         send.levelDb.load(), send.enabled.load(), send.mode.load() == (int) SendMode::Pre);
            }
        };
        sendRow ("Mix Sends", 0, kMixes);
        sendRow ("FX Sends", kMixes, kFxSends);
    }
}

void Display::buildDirOut (Strip& s)
{
    juce::ignoreUnused (s);
    add (Field::Header, { 92, 40, 540, 18 }, "Global Direct Out Settings");
    add (Field::Button, { 210, 70, 210, 30 }, "Post Compressor").enabled = false;
    add (Field::Value, { 120, 150, 90, 48 }, "Level").enabled = false;
    for (auto [title, x, y] : { std::tuple ("Follow Fader", 240, 120), std::tuple ("Follow Mute", 370, 120), std::tuple ("Follow DCA Fader", 240, 190),
                                std::tuple ("Follow DCA Mute", 370, 190), std::tuple ("Follow Mute Group", 370, 260) })
        add (Field::Button, { (float) x, (float) y, 110, 36 }, title).enabled = false;
}

void Display::buildMixSettings (Strip& s)
{
    const auto slot = getSelectedSlot ? getSelectedSlot() : Slot {};
    const int bus = slot.kind == Slot::Mix ? slot.index : slot.kind == Slot::FxSend ? kMixes + slot.index : -1;
    add (Field::Header, { 92, 40, 540, 18 }, slot.kind == Slot::Main ? "Main Settings" : slot.kind == Slot::Matrix ? "Matrix Settings" : typeOf (slot) + " Send Settings");
    addValue ({ 120, 110, 110, 48 }, "Output Bal", bindPan ("Output Bal", s.params.pan));
    if (bus < 0)
        return;
    add (Field::Button, { 280, 70, 210, 30 }, "Post Compressor").enabled = false; // the channel source point
    const auto allOf = [this, bus] (std::function<void (ld::SendParams&)> action)
    {
        return [this, bus, action]
        {
            for (const auto* list : { &ctx.engine.getChannels(), &ctx.engine.getAux() })
                for (const auto& c : *list)
                    action (c->sends[(size_t) bus]);
        };
    };
    add (Field::Button, { 280, 130, 90, 34 }, "All On").press = allOf ([] (ld::SendParams& p) { p.enabled = true; });
    add (Field::Button, { 400, 130, 90, 34 }, "All Off").press = allOf ([] (ld::SendParams& p) { p.enabled = false; });
    add (Field::Button, { 280, 200, 90, 34 }, "All Pre").press = allOf ([] (ld::SendParams& p) { p.mode = (int) SendMode::Pre; });
    add (Field::Button, { 400, 200, 90, 34 }, "All Post").press = allOf ([] (ld::SendParams& p) { p.mode = (int) SendMode::Post; });
}

void Display::buildDcaMute (Strip& s)
{
    add (Field::Header, { 92, 40, 540, 18 }, "DCA");
    add (Field::Header, { 92, 212, 540, 18 }, "Mute Groups");
    const float x0 = 92.0f, cw = 540.0f / 8.0f;
    const auto row = [&] (ld::GroupMask& mask, float y, const juce::String& prefix)
    {
        for (int i = 0; i < 8; ++i)
        {
            const float x = x0 + (float) i * cw;
            auto* m = &mask;
            auto& f = add (Field::Button, { x + 3, y + 22, cw - 6, 40 }, prefix + juce::String (i + 1) + " Assign");
            f.lit = [m, i] { return m->contains (i); };
            f.press = [m, i] { m->set (i, ! m->contains (i)); };
        }
    };
    row (s.params.dcaMask, 62.0f, "DCA");
    row (s.params.muteGroupMask, 234.0f, "MGrp");
}

void Display::buildSends (Strip& s)
{
    // The selected channel's send to every mix (p.50): drag left or right for all of them.
    const float x0 = 92.0f, cw = 544.0f / 7.0f, top = 40.0f;
    const int total = 1 + kMixes + kFxSends;
    sendFirst = juce::jlimit (0, juce::jmax (0, total - 7), sendFirst);
    for (int c = 0; c < 7; ++c)
    {
        const int mix = sendFirst + c;
        if (mix >= total)
            break;
        const float x = x0 + (float) c * cw;
        add (Field::Header, { x + 1, top, cw - 2, 20 }, mix == 0 ? juce::String ("MainLR") : mixName (ctx.engine, mix));
        if (! s.isInput())
            continue;
        if (mix == 0)
        {
            auto* send = &s.mains[0];
            auto& pre = add (Field::Button, { x + 8, top + 28, cw - 16, 32 }, "Post");
            pre.enabled = false;
            auto& on = add (Field::Button, { x + 8, top + 110, cw - 16, 34 }, "On");
            on.lit = [send] { return send->on.load(); };
            on.press = [send] { send->on = ! send->on.load(); };
            addValue ({ x + 6, top + 240, cw - 12, 50 }, "Level", bindFader ("Level", s.params.faderDb));
            continue;
        }
        auto* send = &s.sends[(size_t) (mix - 1)];
        auto& pre = add (Field::Button, { x + 8, top + 28, cw - 16, 32 }, send->mode.load() == (int) SendMode::Pre ? "Pre" : "Post");
        pre.colour = kGreenBar;
        pre.lit = [send] { return send->mode.load() == (int) SendMode::Pre; };
        pre.press = [this, send]
        {
            send->mode = send->mode.load() == (int) SendMode::Pre ? (int) SendMode::Post : (int) SendMode::Pre;
            rebuild();
        };
        auto& on = add (Field::Button, { x + 8, top + 110, cw - 16, 34 }, "On");
        on.lit = [send] { return send->enabled.load(); };
        on.press = [send] { send->enabled = ! send->enabled.load(); };
        addValue ({ x + 6, top + 174, cw - 12, 50 }, "Pan", bindPan ("Pan", send->pan));
        addValue ({ x + 6, top + 240, cw - 12, 50 }, "Level", bindFader ("Level", send->levelDb));
    }
}

//==============================================================================
// Meters

void Display::buildMeters()
{
    addSideTabs ({ "Input Meters", "FX Meters", "Mix Meters", "USB Meters", "RTA", "Channel Meters" }, meterTab);
    for (int i = 3; i < 6; ++i)
        fields[(size_t) i].enabled = false; // USB, RTA and the chromatic thresholds: not yet
}

void Display::paintMeters (juce::Graphics& g)
{
    auto body = bodyArea();
    const std::vector<Strip*> list = [this]
    {
        std::vector<Strip*> v;
        const auto add_ = [&v] (const MixerEngine::StripList& l) { for (const auto& s : l) v.push_back (s.get()); };
        if (meterTab == 0)
            add_ (ctx.engine.getChannels());
        else if (meterTab == 1)
            add_ (ctx.engine.getAux());
        else
        {
            add_ (ctx.engine.getBuses());
            add_ (ctx.engine.getMains());
            add_ (ctx.engine.getMatrices());
        }
        return v;
    }();
    const int perRow = meterTab == 0 ? 24 : juce::jmax (12, (int) list.size());
    const int rows = meterTab == 0 ? 2 : 1;
    const float rowH = body.getHeight() / (float) rows, cw = body.getWidth() / (float) perRow;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        auto* s = list[(size_t) i];
        const int col = i % perRow, row = i / perRow;
        if (row >= rows)
            break;
        juce::Rectangle<float> cell (body.getX() + (float) col * cw, body.getY() + (float) row * rowH, cw, rowH);
        auto r = cell.reduced (1.5f, 3.0f);
        g.setColour (kWell);
        g.fillRect (r);
        auto label = r.removeFromBottom (32.0f);
        auto bars = r.reduced (2.0f, 2.0f);
        const float w = bars.getWidth() * 0.33f;
        meterBar (g, bars.withWidth (w), ctx.meters.get (s, meterTab == 0 ? MeterPoint::Trim : MeterPoint::Output, 0));
        // The gain reduction from the top; the gate's dot when it's open (p.52).
        auto gr = bars.withX (bars.getX() + w + 1.0f).withWidth (w * 0.8f);
        g.setColour (juce::Colours::orange);
        g.fillRect (gr.withHeight (gr.getHeight() * juce::jlimit (0.0f, 1.0f, s->meters.dynReductionDb.load() / 36.0f)));
        if (s->getKind() == StripKind::Channel && s->params.gate().on.load() && s->meters.gateActive.load())
        {
            g.setColour (kGreenBar);
            g.fillEllipse (bars.getRight() - 4.0f, bars.getY(), 4.0f, 4.0f);
        }
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, label.getCentreX(), label.getCentreY()));
        const auto rotated = juce::Rectangle<float> (label.getCentreX() - label.getHeight() * 0.5f, label.getCentreY() - label.getWidth() * 0.5f, label.getHeight(), label.getWidth());
        text (g, rotated, s->getName(), 9.0f, juce::Colours::white, juce::Justification::centredLeft);
    }
}

//==============================================================================
// FX

void Display::pressLibrary()
{
    if (screen == Screen::Fx)
    {
        libraryOpen = ! libraryOpen;
        rebuild();
    }
}

bool Display::hasLibrary() const noexcept { return screen == Screen::Fx; }

void Display::buildFx()
{
    auto& fx = ctx.engine.getFx();
    const auto& models_ = models::table (models::Fx);
    const float top = (float) kTitleBar + 4.0f, h = ((float) kHeight - top - 4.0f) / 8.0f;
    for (int i = 0; i < 8; ++i)
    {
        const auto& m = models_[(size_t) juce::jlimit (0, (int) models_.size() - 1, fx.getModel (i))];
        addTab ({ 2.0f, top + (float) i * h, kTabWidth, h - 3.0f }, m.name.isNotEmpty() && fx.getModel (i) != 0 ? m.name : "Empty", fxSlot, i);
    }

    if (libraryOpen)
    {
        // The FX Library's list: the SQ's RackFX (a unit type, by recalling a preset of it).
        add (Field::Header, { 92, 40, 540, 18 }, "FX Library");
        float x = 96.0f, y = 66.0f;
        for (const auto& m : models_)
        {
            if (models::consoleOf (m.token) != "SQ-5" || m.params.empty())
                continue;
            auto& f = add (Field::Button, { x, y, 170, 40 }, m.name);
            const int index = m.index;
            f.lit = [this, index, &fx] { return fx.getModel (fxSlot) == index; };
            f.press = [this, index, &fx]
            {
                fx.setModel (fxSlot, index);
                libraryOpen = false;
                rebuild();
            };
            x += 178.0f;
            if (x > 500.0f)
            {
                x = 96.0f;
                y += 48.0f;
            }
        }
        return;
    }

    const int model = fx.getModel (fxSlot);
    const auto& m = models_[(size_t) juce::jlimit (0, (int) models_.size() - 1, model)];
    add (Field::Header, { 92, 40, 540, 18 }, "FX Unit " + juce::String (fxSlot + 1) + (model != 0 ? ": " + m.name : juce::String()));
    auto& params = fx.params (fxSlot);
    const float cellW = 88.0f, cellH = 54.0f;
    int i = 0;
    for (const auto& p : m.params)
    {
        if (i >= 14)
            break;
        auto binding = bindModelParam (params.values[p.index], p);
        const float x = 96.0f + (float) (i % 6) * (cellW + 2.0f), y = 66.0f + (float) (i / 6) * (cellH + 2.0f);
        addValue ({ x, y, cellW, cellH }, p.label, std::move (binding));
        ++i;
    }

    // The return's level and mute, with the FX send's (p.60); the PEQ on its return.
    const auto& returns = ctx.engine.getAux();
    const auto& buses = ctx.engine.getBuses();
    if (juce::isPositiveAndBelow (fxSlot, (int) returns.size()))
    {
        auto* r = returns[(size_t) fxSlot].get();
        if (fxSlot < kFxSends && kMixes + fxSlot < (int) buses.size())
        {
            auto* send = buses[(size_t) (kMixes + fxSlot)].get();
            add (Field::Header, { 92, 296, 200, 16 }, send->getName());
            addValue ({ 96, 316, 100, 46 }, "Send Level", bindFader ("Send Level", send->params.faderDb));
            addToggle (Field::Button, { 204, 322, 80, 34 }, "Send Mute", send->params.mute).colour = juce::Colours::red;
        }
        auto& peq = add (Field::Button, { 306, 322, 74, 34 }, "PEQ");
        peq.press = [this, r]
        {
            if (ctx.selectStrip)
                ctx.selectStrip (r);
            showBlock (Block::Peq);
        };
        add (Field::Header, { 400, 296, 232, 16 }, r->getName());
        addValue ({ 404, 316, 100, 46 }, "Return Level", bindFader ("Return Level", r->params.faderDb));
        addToggle (Field::Button, { 516, 322, 90, 34 }, "Return Mute", r->params.mute).colour = juce::Colours::red;
    }
}

//==============================================================================
// Scenes

void Display::buildScenes()
{
    addSideTabs ({ "Scene Manager", "Cue List Editor", "Global Filter", "Safes" }, sceneTab);
    fields[1].enabled = false; // the Cue List Editor and Global Filter: not yet
    fields[2].enabled = false;
    if (sceneTab == 3)
    {
        buildSafes();
        return;
    }

    // All Scenes: the list, then what the selected one can do (p.67).
    auto& show = ctx.show;
    const int count = show.getNumScenes();
    const auto body = bodyArea();
    sceneSelected = juce::jlimit (0, juce::jmax (0, count - 1), sceneSelected);
    const int visible = (int) ((body.getHeight() - 8.0f) / 26.0f);
    sceneFirst = juce::jlimit (0, juce::jmax (0, count - visible), sceneFirst);
    for (int i = 0; i < visible && sceneFirst + i < count; ++i)
    {
        const int at = sceneFirst + i;
        juce::Rectangle<float> row (body.getX(), body.getY() + 4.0f + (float) i * 26.0f, 280.0f, 24.0f);
        auto& f = add (Field::Area, row, {});
        f.press = [this, at]
        {
            sceneSelected = at;
            rebuild();
        };
    }
    const float x = body.getX() + 300.0f, w = body.getWidth() - 300.0f;
    add (Field::Header, { x, body.getY(), w, 18 }, "Recall");
    auto& go = add (Field::Button, { x + 12, body.getY() + 28, 90, 40 }, "Go");
    go.enabled = count > 0;
    go.press = [this, &show]
    {
        show.recallScene (sceneSelected);
        if (autoIncrement)
            sceneSelected = juce::jmin (sceneSelected + 1, juce::jmax (0, show.getNumScenes() - 1));
        rebuild();
    };
    auto& inc = add (Field::Button, { x + 124, body.getY() + 28, 90, 40 }, "Auto Increment");
    inc.lit = [this] { return autoIncrement; };
    inc.press = [this] { autoIncrement = ! autoIncrement; };
    add (Field::Button, { x + 12, body.getY() + 86, 90, 40 }, "Recall Filter").enabled = false;
    add (Field::Header, { x, body.getY() + 196, w, 18 }, "Modify");
    auto& store = add (Field::Button, { x + 12, body.getY() + 224, 90, 40 }, "Store");
    store.enabled = count > 0;
    store.press = [this, &show] { show.storeScene (sceneSelected); };
    auto& del = add (Field::Button, { x + 124, body.getY() + 224, 90, 40 }, "Delete");
    del.enabled = count > 0;
    del.press = [this, &show]
    {
        show.removeScene (sceneSelected);
        rebuild();
    };
    auto& addNew = add (Field::Button, { x + 12, body.getY() + 280, 90, 40 }, "Add New");
    addNew.press = [this, &show]
    {
        sceneSelected = show.addScene ("Scene " + juce::String (show.getNumScenes() + 1));
        rebuild();
    };
}

void Display::paintScenes (juce::Graphics& g)
{
    if (sceneTab != 0)
        return;
    auto& show = ctx.show;
    const auto body = bodyArea();
    const int count = show.getNumScenes(), current = show.getCurrentSceneIndex();
    const int visible = (int) ((body.getHeight() - 8.0f) / 26.0f);
    g.setColour (kWell.withAlpha (0.55f));
    g.fillRoundedRectangle (juce::Rectangle<float> (body.getX() - 2.0f, body.getY(), 284.0f, body.getHeight()), 3.0f);
    for (int i = 0; i < visible && sceneFirst + i < count; ++i)
    {
        const int at = sceneFirst + i;
        juce::Rectangle<float> row (body.getX(), body.getY() + 4.0f + (float) i * 26.0f, 280.0f, 24.0f);
        const auto colour = at == sceneSelected ? kNext : at == current ? kCurrent.withAlpha (0.7f) : juce::Colour (0xff102a40);
        g.setColour (colour);
        g.fillRoundedRectangle (row, 3.0f);
        const auto scene = show.getScene (at);
        text (g, row.reduced (8.0f, 0.0f), juce::String (at + 1) + ")  " + (scene.has_value() ? scene->name : juce::String()), 12.0f,
              at == sceneSelected ? juce::Colours::black : juce::Colours::white, juce::Justification::centredLeft, true);
    }
    if (count == 0)
        text (g, juce::Rectangle<float> (body.getX(), body.getY() + 40.0f, 280.0f, 40.0f), "No scenes: touch Add New", 13.0f, juce::Colours::white);
}

void Display::buildSafes()
{
    // Input, FX and Mix safes (p.78): a grid of Safe keys, per channel.
    const auto body = bodyArea();
    addTab ({ body.getX(), body.getY(), 180, 22 }, "Inputs", safesTab, 0);
    addTab ({ body.getX() + 184, body.getY(), 180, 22 }, "FX", safesTab, 1);
    addTab ({ body.getX() + 368, body.getY(), 180, 22 }, "Mix", safesTab, 2);
    std::vector<Strip*> list;
    const auto append = [&list] (const MixerEngine::StripList& l) { for (const auto& s : l) list.push_back (s.get()); };
    if (safesTab == 0)
        append (ctx.engine.getChannels());
    else if (safesTab == 1)
        append (ctx.engine.getAux());
    else
    {
        append (ctx.engine.getBuses());
        append (ctx.engine.getMains());
        append (ctx.engine.getMatrices());
    }
    const int cols = 8, rows = juce::jmax (6, ((int) list.size() + cols - 1) / cols);
    const float cw = body.getWidth() / (float) cols, ch = (body.getHeight() - 28.0f) / (float) rows;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        juce::Rectangle<float> cell (body.getX() + (float) (i % cols) * cw + 1.0f, body.getY() + 28.0f + (float) (i / cols) * ch + 1.0f, cw - 2.0f, ch - 2.0f);
        auto& f = addToggle (Field::Button, cell, list[(size_t) i]->getName() + " Safe", list[(size_t) i]->params.recallSafe);
        f.colour = juce::Colours::white;
    }
}

//==============================================================================
// Setup

void Display::buildSetup()
{
    addSideTabs ({ "Audio", "Surface", "Mixer Config", "Ganging", "Network Setup", "Users" }, setupTab);
    for (int i : { 2, 3, 4, 5 })
        fields[(size_t) i].enabled = false;

    const auto body = bodyArea();
    if (setupTab == 0)
    {
        // Audio: Talkback (p.96): its source mic's level, the mixes it goes to.
        add (Field::Header, { body.getX(), body.getY(), body.getWidth(), 18 }, "Talkback");
        auto& talk = ctx.engine.talk;
        addValue ({ body.getX() + 12, body.getY() + 30, 110, 50 }, "Level", bindFader ("Level", talk.levelDb));
        addToggle (Field::Button, { body.getX() + 140, body.getY() + 36, 110, 38 }, "Talk", talk.paths[0].on).colour = juce::Colours::orange;
        return;
    }

    // Surface: Strip Assign, Soft Controls (pp.84, 88).
    addTab ({ body.getX(), body.getY(), 180, 22 }, "Strip Assign", surfaceTab, 0);
    addTab ({ body.getX() + 184, body.getY(), 180, 22 }, "Soft Controls", surfaceTab, 1);
    if (surfaceTab == 1)
    {
        for (int k = 0; k < 8; ++k)
        {
            const float x = body.getX() + 8.0f + (float) (k / 4) * 280.0f, y = body.getY() + 34.0f + (float) (k % 4) * 58.0f;
            add (Field::Button, { x, y, 70, 38 }, "Soft " + juce::String (k + 1)).enabled = false;
            auto& f = add (Field::Button, { x + 80, y, 180, 38 }, soft::functions()[soft::get (ctx.settings, k)]);
            f.press = [this, k]
            {
                // The next function, each touch.
                soft::set (ctx.settings, k, (soft::get (ctx.settings, k) + 1) % soft::functions().size());
                rebuild();
            };
        }
        return;
    }

    // Strip Assign: the channels above, the layer's strips below; touch a channel, then a
    // strip (the console drags one to the other).
    const float cw = body.getWidth() / 12.0f, top = body.getY() + 28.0f;
    const Slot::Kind kinds[] = { Slot::Input, Slot::FxReturn, Slot::Mix, Slot::FxSend, Slot::Main, Slot::Matrix, Slot::Dca };
    const char* const names[] = { "Ip", "FX Rtn", "Aux", "FX", "LR", "Mtx", "DCA" };
    const int counts[] = { 48, 8, kMixes, kFxSends, 1, 3, 8 };
    for (int k = 0; k < 7; ++k)
        addTab ({ body.getX() + (float) k * 60.0f, top, 58, 22 }, names[k], assignKind, k);
    assignFirst = juce::jlimit (0, juce::jmax (0, counts[assignKind] - 12), assignFirst);
    for (int i = 0; i < 12 && assignFirst + i < counts[assignKind]; ++i)
    {
        const Slot slot { kinds[assignKind], assignFirst + i };
        auto* strip = stripOf (ctx.engine, slot);
        const auto title = strip != nullptr ? strip->getName() : "DCA" + juce::String (slot.index + 1);
        auto& f = add (Field::Button, { body.getX() + (float) i * cw + 1, top + 30, cw - 2, 56 }, title);
        f.lit = [this, slot] { return assignPicked && assignChoice == slot; };
        f.press = [this, slot]
        {
            assignChoice = slot;
            assignPicked = true;
        };
    }
    if (getLayerSlots == nullptr || assignSlot == nullptr)
        return;
    for (int l = 0; l < 6; ++l)
    {
        const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + l));
        addTab ({ body.getX() + (float) l * 60.0f, top + 104, 58, 22 }, letter, assignLayer, l);
    }
    const auto& slots = getLayerSlots (assignLayer);
    const float sw = body.getWidth() / 16.0f;
    for (int i = 0; i < (int) slots.size(); ++i)
    {
        const auto slot = slots[(size_t) i];
        auto* strip = stripOf (ctx.engine, slot);
        const auto title = slot.kind == Slot::None ? juce::String ("-") : strip != nullptr ? strip->getName() : "DCA" + juce::String (slot.index + 1);
        auto& f = add (Field::Button, { body.getX() + (float) i * sw + 1, top + 134, sw - 2, 86 }, title);
        f.press = [this, i]
        {
            // A strip touched with no channel chosen is emptied.
            assignSlot (assignLayer, i, assignPicked ? assignChoice : Slot {});
            assignPicked = false;
            rebuild();
        };
    }
}

//==============================================================================
// Utility

void Display::buildUtility()
{
    addSideTabs ({ "General", "Data", "USB Utility", "SQ Drive", "Auto Mic Mixer" }, utilityTab);
    for (int i : { 1, 2, 3, 4 })
        fields[(size_t) i].enabled = false;
    const auto body = bodyArea();
    addTab ({ body.getX(), body.getY(), 180, 24 }, "Diagnostics", generalTab, 0);
    addTab ({ body.getX() + 184, body.getY(), 180, 24 }, "MIDI", generalTab, 1);
    addTab ({ body.getX() + 368, body.getY(), 180, 24 }, "Add-ons", generalTab, 2);
    fields.back().enabled = false;
    if (generalTab != 1)
        return;
    // MIDI (p.112): the channel, and the NRPN fader law.
    const auto setup = juce::StringArray::fromTokens (ctx.settings.getValue (SqMidi::setupKey, "1 0"), " ", {});
    const int channel = juce::jlimit (1, 16, setup[0].getIntValue()), audioTaper = setup[1].getIntValue();
    const auto write = [this] (int c, int law)
    {
        ctx.settings.setValue (SqMidi::setupKey, juce::String (c) + " " + juce::String (law));
        rebuild();
    };
    auto& ch = add (Field::Button, { body.getX() + 12, body.getY() + 44, 180, 40 }, "MIDI Channel " + juce::String (channel));
    ch.press = [write, channel, audioTaper] { write (channel % 16 + 1, audioTaper); };
    auto& law = add (Field::Button, { body.getX() + 212, body.getY() + 44, 180, 40 }, audioTaper != 0 ? "NRPN: Audio Taper" : "NRPN: Linear Taper");
    law.press = [write, channel, audioTaper] { write (channel, audioTaper != 0 ? 0 : 1); };
}

void Display::paintUtility (juce::Graphics& g)
{
    if (generalTab != 0)
        return;
    const auto body = bodyArea().withTrimmedTop (32.0f);
    g.setColour (kWell.withAlpha (0.6f));
    g.fillRoundedRectangle (body, 3.0f);
    auto r = body.reduced (14.0f, 10.0f);
    const auto counts = ctx.engine.getCounts();
    auto* device = ctx.deviceManager.getCurrentAudioDevice();
    const std::pair<juce::String, juce::String> lines[] = {
        { "Firmware Version:", "Sidecar (SQ-5 emulation)" },
        { "Sample Rate:", device != nullptr ? juce::String (device->getCurrentSampleRate(), 0) + " Hz" : juce::String ("no audio device") },
        { "Channels:", juce::String (counts.channels) + " inputs, " + juce::String (counts.buses) + " mixes, " + juce::String (counts.matrices) + " matrices" },
        { "DCAs / Mute Groups:", juce::String (counts.dcas) + " / " + juce::String (counts.muteGroups) },
    };
    for (const auto& [k, v] : lines)
    {
        auto line = r.removeFromTop (22.0f);
        text (g, line.removeFromLeft (180.0f), k, 13.0f, juce::Colours::white, juce::Justification::centredLeft, true);
        text (g, line, v, 13.0f, juce::Colours::white, juce::Justification::centredLeft);
    }
}

//==============================================================================
// I/O

void Display::buildIo()
{
    addSideTabs ({ "Inputs", "Outputs", "Tie Lines" }, ioTab);
    fields[2].enabled = false;

    // A grid of crosspoints: 14 sockets across, 11 rows, drag the rows up and down (p.23).
    const auto r = matrixArea();
    const int rows = ioTab == 0 ? 48 : outputRows, cols = ioTab == 0 ? 22 : 16;
    ioFirst = juce::jlimit (0, juce::jmax (0, rows - kIoRows), ioFirst);
    ioSocketFirst = juce::jlimit (0, juce::jmax (0, cols - kIoCols), ioSocketFirst);
    const float cw = r.getWidth() / (float) kIoCols, ch = r.getHeight() / (float) kIoRows;
    for (int row = 0; row < kIoRows && ioFirst + row < rows; ++row)
        for (int col = 0; col < kIoCols && ioSocketFirst + col < cols; ++col)
        {
            const int y = ioFirst + row, x = ioSocketFirst + col;
            auto& f = add (Field::Area, { r.getX() + (float) col * cw, r.getY() + (float) row * ch, cw, ch }, {});
            f.press = [this, x, y]
            {
                if (ioTab == 0)
                {
                    auto* s = ctx.engine.getChannels()[(size_t) y].get();
                    s->params.inputLeft = s->params.inputLeft.load() == x ? -1 : x;
                }
                else
                {
                    const int source = outputRowSource (y);
                    if (ctx.engine.getOutputSource (x) == source)
                        ctx.engine.clearOutput (x);
                    else
                        ctx.engine.routeOutputSource (x, source);
                }
            };
        }
}

juce::Rectangle<float> Display::matrixArea() const
{
    const auto body = bodyArea();
    return { body.getX() + 76.0f, body.getY() + 44.0f, body.getWidth() - 76.0f, body.getHeight() - 44.0f };
}

void Display::paintIo (juce::Graphics& g)
{
    const auto r = matrixArea();
    const float cw = r.getWidth() / (float) kIoCols, ch = r.getHeight() / (float) kIoRows;
    const auto body = bodyArea();
    text (g, { body.getX(), body.getY(), body.getWidth(), 20.0f }, ioTab == 0 ? "Local" : "Local Outputs", 14.0f, juce::Colours::white, juce::Justification::centred, true);
    const int rows = ioTab == 0 ? 48 : outputRows;
    const int cols = ioTab == 0 ? 22 : 16;
    for (int col = 0; col < kIoCols && ioSocketFirst + col < cols; ++col)
    {
        juce::Rectangle<float> head (r.getX() + (float) col * cw, r.getY() - 22.0f, cw, 20.0f);
        text (g, head, juce::String (ioSocketFirst + col + 1), 11.0f, juce::Colours::white, juce::Justification::centred, true);
    }
    for (int row = 0; row < kIoRows && ioFirst + row < rows; ++row)
    {
        const int y = ioFirst + row;
        juce::Rectangle<float> label (body.getX(), r.getY() + (float) row * ch, 74.0f, ch);
        g.setColour (juce::Colour (0xff7d96bf));
        g.fillRect (label.reduced (0.0f, 0.6f));
        const auto name = ioTab == 0 ? ctx.engine.getChannels()[(size_t) y]->getName() : y < 2 ? juce::String (y == 0 ? "LR L" : "LR R") : ctx.engine.getBuses()[(size_t) (y - 2)]->getName();
        text (g, label, name, 11.0f, juce::Colours::white, juce::Justification::centred, true);
        const int source = ioTab == 1 ? outputRowSource (y) : -1;
        for (int col = 0; col < kIoCols && ioSocketFirst + col < cols; ++col)
        {
            const int x = ioSocketFirst + col;
            const bool on = ioTab == 0 ? ctx.engine.getChannels()[(size_t) y]->params.inputLeft.load() == x : ctx.engine.getOutputSource (x) == source;
            juce::Rectangle<float> cell (r.getX() + (float) col * cw, r.getY() + (float) row * ch, cw, ch);
            g.setColour (on ? kOnCell : kOffCell);
            g.fillRect (cell.reduced (0.7f));
        }
    }
}

} // namespace livedaw::ui::sq5
