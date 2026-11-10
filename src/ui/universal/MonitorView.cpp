#include "ui/universal/MonitorView.h"

#include "ui/universal/RoutingView.h"

#include <algorithm>

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kRow = sizes::kTouch, kTitle = 36, kCaption = 22;

    /** OFF, then the output ports ROUTING offers ("LCL 3"). */
    void bindOutput (ChoiceBar& choice, UiContext& ctx, const juce::String& caption, std::atomic<int32_t>& port)
    {
        choice.caption = caption;
        choice.items = [&ctx]
        {
            juce::StringArray items { "OFF" };
            for (int p : outputPorts (ctx.engine))
                items.add (outputPortName (p));
            return items;
        };
        choice.current = [&ctx, &port]
        {
            const auto ports = outputPorts (ctx.engine);
            const auto it = std::find (ports.begin(), ports.end(), port.load());
            return it != ports.end() ? 1 + (int) (it - ports.begin()) : 0;
        };
        choice.choose = [&ctx, &port] (int i)
        {
            const auto ports = outputPorts (ctx.engine);
            port = juce::isPositiveAndBelow (i - 1, (int) ports.size()) ? ports[(size_t) (i - 1)] : -1;
        };
    }

    /** A monitor SOURCE or DIRECT IN choice: its name and the engine's index. */
    struct Choice
    {
        juce::String name;
        int index;
    };

    /** The console's strips of `kinds` as choices (a bus by its slot), in that
        order: a Universal console's past the WING's counts too (BUS.17). */
    std::vector<Choice> stripChoices (MixerEngine& e, std::initializer_list<StripKind> kinds, bool direct)
    {
        std::vector<Choice> list;
        for (auto kind : kinds)
        {
            const auto& strips = e.getStrips (kind);
            for (int i = 0; i < (int) strips.size(); ++i)
            {
                const int number = strips[(size_t) i]->isBus() ? strips[(size_t) i]->getId() : i;
                const int index = direct ? MixerEngine::directSourceIndex (kind, number) : MixerEngine::monitorSourceIndex (kind, number);
                if (index >= 0)
                    list.push_back ({ direct ? MixerEngine::directSourceName (index) : MixerEngine::monitorSourceName (index), index });
            }
        }
        return list;
    }

    std::vector<Choice> monitorSources (MixerEngine& e)
    {
        return stripChoices (e, { StripKind::Main, StripKind::Matrix, StripKind::Bus, StripKind::Aux }, false);
    }

    /** OFF, then every channel, aux, bus, main and matrix. */
    std::vector<Choice> directSources (MixerEngine& e)
    {
        auto list = stripChoices (e, { StripKind::Channel, StripKind::Aux, StripKind::Bus, StripKind::Main, StripKind::Matrix }, true);
        list.insert (list.begin(), { "OFF", -1 });
        return list;
    }

    void bindChoices (ChoiceBar& bar, std::function<std::vector<Choice>()> choices, std::atomic<int32_t>& value)
    {
        bar.items = [choices]
        {
            juce::StringArray names;
            for (const auto& c : choices())
                names.add (c.name);
            return names;
        };
        bar.current = [choices, &value]
        {
            const auto list = choices();
            const int v = value.load();
            for (size_t i = 0; i < list.size(); ++i)
                if (list[i].index == v)
                    return (int) i;
            return -1;
        };
        bar.choose = [choices, &value] (int i)
        {
            const auto list = choices();
            if (juce::isPositiveAndBelow (i, (int) list.size()))
                value = list[(size_t) i].index;
        };
    }

    // The monitor EQ (engine monitor.rs): L (low shelf), 1-6 (PEQ), H (high shelf);
    // their defaults are a WING's after INITIALIZE.
    constexpr float kEqDefaultFreq[ld::kMonitorEqBands] = { 60.14f, 129.88f, 299.25f, 699.49f, 1499.8f, 2992.5f, 6013.9f, 12000.0f };
    constexpr float kEqDefaultQ = 2.0f;

    std::vector<ResponseView::Handle> monitorEqHandles (ld::MonitorDspParams& p)
    {
        std::vector<ResponseView::Handle> handles;
        for (int b = 0; b < ld::kMonitorEqBands; ++b)
        {
            const bool shelf = b == 0 || b == ld::kMonitorEqBands - 1;
            ResponseView::Handle h;
            h.label = b == 0 ? juce::String ("L") : shelf ? juce::String ("H") : juce::String (b);
            h.freq = &p.eq[b].freq;
            h.gain = &p.eq[b].gainDb;
            h.q = shelf ? nullptr : &p.eq[b].q; // the shelves have no Q
            h.minFreq = b == ld::kMonitorEqBands - 1 ? 200.0f : 20.0f;
            h.maxFreq = b == 0 ? 2000.0f : 20000.0f;
            h.defaultFreq = kEqDefaultFreq[b];
            h.minGain = -15.0f;
            h.maxGain = 15.0f;
            h.minQ = 0.442f;
            h.defaultQ = kEqDefaultQ;
            handles.push_back (h);
        }
        return handles;
    }
} // namespace

//==============================================================================
MonitorView::Path::Path (UiContext& c, const Theme& t, bool isPhones)
    : source (t), sourceLevel (t), level (t), dimAmount (t), dim ("DIM", t.solo, t), mono ("MONO", t.accent, t), mute ("MUTE", t.mute, t),
      outputLeft (t), outputRight (t), meter (t), directIn (t), directLevel (t), eqOn ("EQ", t.accent, t), invert ("INVERT", t.accent, t),
      delayOn ("DELAY", t.accent, t), eq (t), pan (t), width (t), distance (t), limiter (t), ctx (c), theme (t), phones (isPhones)
{
    auto& e = c.engine;
    auto& src = phones ? e.phones.source : e.speakers;
    // The console's mains, matrices, buses and aux (the WING's order), past the WING's counts too.
    source.caption = "SOURCE";
    bindChoices (source, [&e] { return monitorSources (e); }, src.source);
    sourceLevel.bind (ValueBar::level ("SOURCE LEVEL", src.sourceLevelDb));
    level.bind (ValueBar::level ("LEVEL", phones ? e.phones.levelDb : e.monitorLevelDb));
    dimAmount.bind (ValueBar::linear ("DIM BY", phones ? e.phones.dimDb : e.monitorDimDb, 0.0f, 40.0f, 20.0f, " dB", 0));
    // DIM, MONO and MUTE (hold DIM on the console) act on the speakers.
    dim.isOn = [&e] { return e.monitorDim.load(); };
    dim.onClick = [&e] { e.monitorDim = ! e.monitorDim.load(); };
    mono.isOn = [&e] { return e.monitorMono.load(); };
    mono.onClick = [&e] { e.monitorMono = ! e.monitorMono.load(); };
    mute.isOn = [&e] { return e.monitorMute.load(); };
    mute.onClick = [&e] { e.monitorMute = ! e.monitorMute.load(); };
    bindOutput (outputLeft, c, "OUTPUT L", phones ? e.phones.outputLeft : e.monitorOutputLeft);
    bindOutput (outputRight, c, "OUTPUT R", phones ? e.phones.outputRight : e.monitorOutputRight);
    meter.setChannels (2);
    meter.level = [&bank = c.meters, isPhones] (int ch) { return isPhones ? bank.getPhones (ch) : bank.getMonitor (ch); };

    // The path's processing.
    auto& p = e.monitorPath (phones);
    directIn.caption = "DIRECT IN";
    bindChoices (directIn, [&e] { return directSources (e); }, p.directSource);
    directLevel.bind (ValueBar::level ("DIRECT LEVEL", p.directLevelDb));
    eqOn.isOn = [&p] { return p.eqOn.load(); };
    eqOn.onClick = [&p] { p.eqOn = ! p.eqOn.load(); };
    invert.isOn = [&p] { return p.invert.load(); };
    invert.onClick = [&p] { p.invert = ! p.invert.load(); };
    delayOn.isOn = [&p] { return p.delayOn.load(); };
    delayOn.onClick = [&p] { p.delayOn = ! p.delayOn.load(); };
    eq.handleSource = [&p] { return monitorEqHandles (p); };
    eq.response = [this, isPhones] (const double* freqs, double* outDb, int count)
    {
        if (! ctx.engine.monitorPath (isPhones).eqOn.load())
        {
            std::fill (outDb, outDb + count, 0.0);
            return true;
        }
        std::vector<float> f ((size_t) count), db ((size_t) count);
        for (int i = 0; i < count; ++i)
            f[(size_t) i] = (float) freqs[i];
        ctx.engine.getMonitorEqResponse (isPhones, -1, f.data(), db.data(), count);
        for (int i = 0; i < count; ++i)
            outDb[i] = db[(size_t) i];
        return true;
    };
    eq.rangeDb = 15.0f;
    pan.bind (ValueBar::linear ("PAN", p.pan, -100.0f, 100.0f, 0.0f, "", 0));
    width.bind (ValueBar::linear ("WIDTH", p.width, -150.0f, 150.0f, 100.0f, " %", 0));
    {
        // DIST: 0.1 to 100 m, on a log scale (most rooms are a few metres), with its delay in ms.
        ValueBar::Binding b;
        b.caption = "DISTANCE";
        b.value = &p.delayM;
        b.toPosition = [] (float m) { return (float) (std::log (juce::jlimit (0.1f, 100.0f, m) / 0.1f) / std::log (1000.0)); };
        b.fromPosition = [] (float pos) { return 0.1f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, pos)); };
        b.text = [] (float m) { return juce::String (m, m < 10.0f ? 1 : 0) + " m  " + juce::String (MixerEngine::monitorDelayMs (m), 1) + " ms"; };
        b.defaultValue = 0.1f;
        distance.bind (b);
    }
    limiter.bind (ValueBar::linear ("LIMITER", p.limiterDb, -40.0f, 0.0f, 0.0f, " dB", 1));

    rows = { { &source, kRow }, { &sourceLevel, kRow }, { &level, kRow } };
    if (! phones)
        rows.push_back ({ nullptr, sizes::kTouch }); // DIM, MONO, MUTE
    rows.push_back ({ &dimAmount, kRow });
    rows.push_back ({ &outputLeft, kRow });
    rows.push_back ({ &outputRight, kRow });
    for (auto& [row, h] : rows)
        if (row != nullptr)
            addAndMakeVisible (row);
    if (! phones)
        for (auto* k : { &dim, &mono, &mute })
            addAndMakeVisible (k);
    for (auto* child : std::initializer_list<juce::Component*> { &directIn, &directLevel, &eqOn, &invert, &delayOn, &eq, &pan, &width, &distance, &limiter })
        addAndMakeVisible (child);
    addAndMakeVisible (meter);
}

void MonitorView::Path::refresh()
{
    for (auto* c : { &source, &outputLeft, &outputRight, &directIn })
        c->refresh();
    for (auto* v : { &sourceLevel, &level, &dimAmount, &directLevel, &pan, &width, &distance, &limiter })
        v->refresh();
    for (auto* k : { &dim, &mono, &mute, &eqOn, &invert, &delayOn })
        k->refresh();
    eq.refresh();
    meter.refresh();
    // The limiter's gain reduction: the largest since the last look, held half a second.
    const float gr = -std::abs (ctx.engine.monitorPath (phones).limiterGrDb.exchange (0.0f));
    if (gr < limiterGr - 0.05f || ++heldTicks > 10)
    {
        if (std::abs (gr - limiterGr) > 0.05f)
            repaint (reductionArea);
        limiterGr = gr;
        heldTicks = 0;
    }
}

void MonitorView::Path::paint (juce::Graphics& g)
{
    g.setColour (theme.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), sizes::kRadius);
    g.setColour (theme.text);
    g.setFont (type::font (type::kHeading, true));
    g.drawText (phones ? "PHONES" : "SPEAKERS", getLocalBounds().removeFromTop (kTitle).reduced (sizes::kGap + 4, 0), juce::Justification::centredLeft);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption, true));
    g.drawText ("PROCESSING", processingCaption, juce::Justification::bottomLeft);
    // The limiter's gain reduction as a bar from the right, with its value.
    if (! reductionArea.isEmpty())
    {
        auto r = reductionArea.toFloat();
        g.setColour (theme.well);
        g.fillRoundedRectangle (r, sizes::kRadius);
        const float amount = juce::jlimit (0.0f, 1.0f, -limiterGr / 20.0f);
        g.setColour (theme.solo);
        g.fillRoundedRectangle (r.withLeft (r.getRight() - r.getWidth() * amount), sizes::kRadius);
        g.setColour (theme.textDim);
        g.drawText ("GR " + juce::String (limiterGr, 1) + " dB", reductionArea.reduced (6, 0), juce::Justification::centredLeft);
    }
}

int MonitorView::Path::preferredHeight() const
{
    int h = sizes::kGap + (kTitle - sizes::kGap);
    for (auto [c, rowH] : rows)
        h += rowH + sizes::kGapSmall;
    constexpr int kEqMin = 180;
    h += kCaption + sizes::kGapSmall + 2 * (kRow + sizes::kGapSmall) + sizes::kTouch + sizes::kGapSmall; // DIRECT IN, its level, EQ / INVERT / DELAY
    h += kEqMin + sizes::kGapSmall + 4 * kRow + 3 * sizes::kGapSmall + 22 + sizes::kGapSmall + sizes::kGap; // the EQ, PAN ... LIMITER, GR
    return h;
}

void MonitorView::Path::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap);
    area.removeFromTop (kTitle - sizes::kGap);
    auto meterArea = area.removeFromRight (16);
    area.removeFromRight (sizes::kGap);
    int y = area.getY();
    for (auto [c, h] : rows)
    {
        const juce::Rectangle<int> r (area.getX(), y, area.getWidth(), h);
        if (c != nullptr)
            c->setBounds (r);
        else
        {
            auto keys = r;
            const int w = (keys.getWidth() - 2 * sizes::kGapSmall) / 3;
            for (auto* k : { &dim, &mono, &mute })
            {
                k->setBounds (keys.removeFromLeft (w));
                keys.removeFromLeft (sizes::kGapSmall);
            }
        }
        y += h + sizes::kGapSmall;
    }
    meter.setBounds (meterArea.withBottom (y));

    // The processing under the levels: the EQ takes what the rows leave.
    area.setTop (y);
    processingCaption = area.removeFromTop (kCaption);
    area.removeFromTop (sizes::kGapSmall);
    directIn.setBounds (area.removeFromTop (kRow));
    area.removeFromTop (sizes::kGapSmall);
    directLevel.setBounds (area.removeFromTop (kRow));
    area.removeFromTop (sizes::kGapSmall);
    auto keys = area.removeFromTop (sizes::kTouch);
    const int kw = (keys.getWidth() - 2 * sizes::kGapSmall) / 3;
    for (auto* k : { &eqOn, &invert, &delayOn })
    {
        k->setBounds (keys.removeFromLeft (kw));
        keys.removeFromLeft (sizes::kGapSmall);
    }
    area.removeFromTop (sizes::kGapSmall);
    auto bottom = area.removeFromBottom (4 * kRow + 3 * sizes::kGapSmall + 22 + sizes::kGapSmall);
    eq.setBounds (area.withTrimmedBottom (sizes::kGapSmall));
    for (auto* v : { &pan, &width, &distance, &limiter })
    {
        v->setBounds (bottom.removeFromTop (kRow));
        bottom.removeFromTop (sizes::kGapSmall);
    }
    reductionArea = bottom.removeFromTop (22);
}

//==============================================================================
MonitorView::Talkback::Talkback (UiContext& c, const Theme& t)
    : channel (t), talkLevel (t), talkA ("TALK A", t.solo, t), talkB ("TALK B", t.solo, t), tabA ("A", t.accent, t), tabB ("B", t.accent, t), mode (t),
      monDim (t), busDim (t), useSendLevels ("USE SEND LEVELS", t.accent, t), destinations (t, t.solo), ctx (c), theme (t)
{
    auto& e = ctx.engine;
    // The talkback channel: OFF, or an input or aux channel.
    channel.caption = "TALK CHANNEL";
    channel.items = [&e]
    {
        juce::StringArray items { "OFF" };
        for (auto* list : { &e.getChannels(), &e.getAux() })
            for (auto& s : *list)
                items.add (s->getLabel() + (s->getName().isNotEmpty() && s->getName() != s->getLabel() ? "  " + s->getName() : juce::String()));
        return items;
    };
    channel.current = [&e]
    {
        auto* talk = e.getTalkStrip();
        int i = 1;
        for (auto* list : { &e.getChannels(), &e.getAux() })
            for (auto& s : *list)
            {
                if (s.get() == talk)
                    return i;
                ++i;
            }
        return 0;
    };
    channel.choose = [&e] (int index)
    {
        int i = 1;
        for (auto* list : { &e.getChannels(), &e.getAux() })
            for (auto& s : *list)
                if (i++ == index)
                {
                    e.setTalkStrip (s.get());
                    return;
                }
        e.setTalkStrip (nullptr);
    };
    talkLevel.bind (ValueBar::level ("TALK LEVEL", e.talk.levelDb));
    // TALK A / B: pressed and released as on the console (AUTO: held = push, a tap latches).
    for (int path = 0; path < 2; ++path)
    {
        auto& key = path == 0 ? talkA : talkB;
        key.isOn = [&e, path] { return e.isTalkOn (path); };
        key.onStateChange = [this, &key, path]
        {
            const bool down = key.isDown();
            bool& was = path == 0 ? downA : downB;
            if (down != was)
            {
                was = down;
                ctx.engine.talkKey (path, down);
            }
        };
    }
    tabA.isOn = [this] { return shown == 0; };
    tabA.onClick = [this] { showPath (0); };
    tabB.isOn = [this] { return shown == 1; };
    tabB.onClick = [this] { showPath (1); };
    mode.caption = "MODE";
    mode.items = [] { return juce::StringArray { "AUTO", "PUSH", "LATCH" }; };
    destinations.minKey = 46;
    destinations.count = [this] { return numDestinations(); };
    destinations.keyText = [this] (int i)
    {
        const auto [kind, n] = destination (i);
        return (kind == StripKind::Bus ? "B" : kind == StripKind::Main ? "M" : "X") + juce::String (n + 1);
    };
    bindPath();
    for (auto* child : std::initializer_list<juce::Component*> { &channel, &talkLevel, &talkA, &talkB, &tabA, &tabB, &mode, &monDim, &busDim,
                                                                 &useSendLevels, &destinations })
        addAndMakeVisible (child);
}

void MonitorView::Talkback::bindPath()
{
    auto& p = ctx.engine.talk.paths[shown];
    mode.current = [&p] { return p.mode.load(); };
    mode.choose = [&p] (int i) { p.mode = juce::jlimit (0, 2, i); };
    monDim.bind (ValueBar::linear ("MONITOR DIM", p.monDimDb, 0.0f, 40.0f, 10.0f, " dB", 0));
    busDim.bind (ValueBar::linear ("BUS DIM", p.busDimDb, 0.0f, 40.0f, 10.0f, " dB", 0));
    useSendLevels.isOn = [&p] { return p.individual.load(); };
    useSendLevels.onClick = [&p] { p.individual = ! p.individual.load(); };
    // TO: the console's buses (by slot), mains and matrices, as group sets.
    auto set = [this, &p] (int i) -> std::pair<ld::GroupMask*, int>
    {
        const auto [kind, n] = destination (i);
        return { kind == StripKind::Bus ? &p.buses : kind == StripKind::Main ? &p.mains : &p.matrices, n };
    };
    destinations.isOn = [set] (int i)
    {
        auto [s, n] = set (i);
        return s->contains (n);
    };
    destinations.toggle = [set] (int i)
    {
        auto [s, n] = set (i);
        s->toggle (n);
    };
}

int MonitorView::Talkback::numDestinations() const
{
    auto& e = ctx.engine;
    const auto capped = [] (size_t n) { return juce::jmin ((int) n, ld::kMaxGroups); };
    return capped (e.getBuses().size()) + capped (e.getMains().size()) + capped (e.getMatrices().size());
}

std::pair<StripKind, int> MonitorView::Talkback::destination (int i) const
{
    auto& e = ctx.engine;
    const int buses = juce::jmin ((int) e.getBuses().size(), ld::kMaxGroups);
    const int mains = juce::jmin ((int) e.getMains().size(), ld::kMaxGroups);
    if (i < buses)
        return { StripKind::Bus, e.getBuses()[(size_t) i]->getId() };
    if (i < buses + mains)
        return { StripKind::Main, i - buses };
    return { StripKind::Matrix, i - buses - mains };
}

void MonitorView::Talkback::showPath (int path)
{
    shown = juce::jlimit (0, 1, path);
    bindPath();
    refresh();
    repaint();
}

void MonitorView::Talkback::refresh()
{
    channel.refresh();
    mode.refresh();
    for (auto* v : { &talkLevel, &monDim, &busDim })
        v->refresh();
    for (auto* k : { &talkA, &talkB, &tabA, &tabB, &useSendLevels })
        k->refresh();
    destinations.refresh();
}

void MonitorView::Talkback::paint (juce::Graphics& g)
{
    g.setColour (theme.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), sizes::kRadius);
    auto title = getLocalBounds().removeFromTop (kTitle).reduced (sizes::kGap + 4, 0);
    g.setColour (theme.text);
    g.setFont (type::font (type::kHeading, true));
    g.drawText ("TALKBACK", title, juce::Justification::centredLeft);
    title.removeFromLeft (110);
    drawDeviceTag (g, title.toFloat(), devices::shown(), theme); // the WING's talk paths (AUTO / PUSH / LATCH, the dims)
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption, true));
    g.drawText ("TALK " + juce::String (shown == 0 ? "A" : "B") + " TO", toCaption, juce::Justification::bottomLeft);
}

int MonitorView::Talkback::preferredHeight (int width) const
{
    const int keysW = width - 2 * sizes::kGap;
    return sizes::kGap + (kTitle - sizes::kGap) + 2 * (kRow + sizes::kGapSmall) + sizes::kTouch + sizes::kGap + (sizes::kTouch - 8) + sizes::kGapSmall
           + 3 * (kRow + sizes::kGapSmall) + sizes::kTouch + kCaption + sizes::kGapSmall + destinations.heightFor (keysW) + sizes::kGap;
}

void MonitorView::Talkback::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap);
    area.removeFromTop (kTitle - sizes::kGap);
    channel.setBounds (area.removeFromTop (kRow));
    area.removeFromTop (sizes::kGapSmall);
    talkLevel.setBounds (area.removeFromTop (kRow));
    area.removeFromTop (sizes::kGapSmall);
    auto keys = area.removeFromTop (sizes::kTouch);
    talkA.setBounds (keys.removeFromLeft (keys.getWidth() / 2).withTrimmedRight (sizes::kGapSmall / 2));
    talkB.setBounds (keys.withTrimmedLeft (sizes::kGapSmall / 2));
    area.removeFromTop (sizes::kGap);
    // The chosen path's settings.
    auto tabs = area.removeFromTop (sizes::kTouch - 8);
    tabA.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2).withTrimmedRight (sizes::kGapSmall / 2));
    tabB.setBounds (tabs.withTrimmedLeft (sizes::kGapSmall / 2));
    area.removeFromTop (sizes::kGapSmall);
    mode.setBounds (area.removeFromTop (kRow));
    area.removeFromTop (sizes::kGapSmall);
    monDim.setBounds (area.removeFromTop (kRow));
    area.removeFromTop (sizes::kGapSmall);
    busDim.setBounds (area.removeFromTop (kRow));
    area.removeFromTop (sizes::kGapSmall);
    useSendLevels.setBounds (area.removeFromTop (sizes::kTouch));
    toCaption = area.removeFromTop (kCaption);
    area.removeFromTop (sizes::kGapSmall);
    destinations.setBounds (area.removeFromTop (juce::jmin (area.getHeight(), destinations.heightFor (area.getWidth()))));
}

//==============================================================================
MonitorView::MonitorView (UiContext& c, const Theme& t)
    : speakers (c, t, false), phones (c, t, true), soloBus (t), soloMode (t), aflChannels ("AFL CHANNELS", t.accent, t),
      aflBuses ("AFL BUSES", t.accent, t), aflMains ("AFL MAINS", t.accent, t), aflMatrices ("AFL MATRICES", t.accent, t),
      clearSolo ("CLEAR SOLO", t.solo, t), talkback (c, t), ctx (c), theme (t)
{
    auto& e = ctx.engine;
    soloBus.caption = "SOLO TO";
    soloBus.device = devices::shown();
    soloBus.items = [] { return models::list (models::MonitorBuses); };
    soloBus.current = [&e] { return e.soloBus.load(); };
    soloBus.choose = [&e] (int i) { e.setSoloBus (i); };
    soloMode.caption = "SOLO MODE";
    soloMode.device = devices::shown(); // LIVE, STUDIO, SIP
    soloMode.items = [] { return juce::StringArray { "LIVE", "STUDIO", "SIP" }; };
    soloMode.current = [&e] { return e.soloMode.load(); };
    soloMode.choose = [&e] (int i) { e.soloMode = i; };
    const auto afl = [&e] (ToggleKey& key, StripKind kind)
    {
        key.isOn = [&e, kind] { return e.isSoloAfl (kind); };
        key.onClick = [&e, kind] { e.setSoloAfl (kind, ! e.isSoloAfl (kind)); };
    };
    afl (aflChannels, StripKind::Channel);
    afl (aflBuses, StripKind::Bus);
    afl (aflMains, StripKind::Main);
    afl (aflMatrices, StripKind::Matrix);
    clearSolo.isOn = [&e] { return e.anySoloActive.load(); };
    clearSolo.onClick = [this] { clearAllSolos(); };
    for (auto [view, path] : { std::pair { &speakersView, &speakers }, { &phonesView, &phones } })
    {
        view->setViewedComponent (path, false);
        view->setScrollBarsShown (true, false);
        view->setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
        view->getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme.outline);
        addAndMakeVisible (view);
    }
    talkbackView.setViewedComponent (&talkback, false);
    talkbackView.setScrollBarsShown (true, false);
    talkbackView.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    talkbackView.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme.outline);
    for (auto* child : std::initializer_list<juce::Component*> { &soloBus, &soloMode, &aflChannels, &aflBuses, &aflMains, &aflMatrices, &clearSolo, &talkbackView })
        addAndMakeVisible (child);
    refresh();
    startTimerHz (20);
}

MonitorView::~MonitorView()
{
    stopTimer();
}

void MonitorView::clearAllSolos()
{
    // As the WING's CLR SOLO (MainDisplay::clearSolo), for every DCA in use.
    for (int k = 0; k < kNumStripKinds; ++k)
        for (auto& s : ctx.engine.getStrips (static_cast<StripKind> (k)))
            s->params.solo = false;
    for (int i = 0; i < ctx.engine.getNumDcas(); ++i)
        ctx.engine.getDca (i).solo = false;
    ctx.engine.getAnalysis().stopListening();
}

void MonitorView::refresh()
{
    speakers.refresh();
    phones.refresh();
    for (auto* c : { &soloBus, &soloMode })
        c->refresh();
    for (auto* k : { &aflChannels, &aflBuses, &aflMains, &aflMatrices, &clearSolo })
        k->refresh();
    talkback.refresh();
}

void MonitorView::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    g.setColour (theme.panel);
    g.fillRoundedRectangle (soloPanel.toFloat(), sizes::kRadius);
    g.setColour (theme.text);
    g.setFont (type::font (type::kHeading, true));
    g.drawText ("SOLO", soloPanel.withHeight (kTitle).reduced (sizes::kGap + 4, 0), juce::Justification::centredLeft);
}

void MonitorView::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap);
    const int w = (area.getWidth() - 2 * sizes::kGap) / 3;
    // Each path as tall as it needs (with an EQ worth having), scrolled when the view is shorter.
    for (auto [view, path] : { std::pair { &speakersView, &speakers }, { &phonesView, &phones } })
    {
        view->setBounds (area.removeFromLeft (w));
        area.removeFromLeft (sizes::kGap);
        const int needed = path->preferredHeight();
        const bool scrolls = needed > view->getHeight();
        path->setSize (view->getWidth() - (scrolls ? view->getScrollBarThickness() : 0), juce::jmax (view->getHeight(), needed));
    }
    // SOLO over TALKBACK: SOLO as tall as its rows.
    auto column = area.withWidth (w);
    const int soloH = kTitle + 2 * kRow + 5 * sizes::kTouch + 7 * sizes::kGapSmall + sizes::kGap;
    soloPanel = column.removeFromTop (soloH);
    column.removeFromTop (sizes::kGap);
    talkbackView.setBounds (column);
    const int needed = talkback.preferredHeight (column.getWidth());
    const bool scrolls = needed > column.getHeight();
    talkback.setSize (column.getWidth() - (scrolls ? talkbackView.getScrollBarThickness() : 0), juce::jmax (column.getHeight(), needed));
    auto solo = soloPanel.reduced (sizes::kGap);
    solo.removeFromTop (kTitle - sizes::kGap);
    layoutRows (solo, { { &soloBus, kRow }, { &soloMode, kRow }, { &aflChannels, sizes::kTouch }, { &aflBuses, sizes::kTouch },
                        { &aflMains, sizes::kTouch }, { &aflMatrices, sizes::kTouch }, { &clearSolo, sizes::kTouch } },
                sizes::kGapSmall);
}

} // namespace livedaw::ui::universal
