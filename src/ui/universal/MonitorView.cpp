#include "ui/universal/MonitorView.h"

#include "ui/universal/RoutingView.h"

#include <algorithm>

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kRow = 40, kTitle = 36;

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
} // namespace

//==============================================================================
MonitorView::Path::Path (UiContext& ctx, const Theme& t, bool isPhones)
    : source (t), sourceLevel (t), level (t), dimAmount (t), dim ("DIM", t.solo, t), mono ("MONO", t.accent, t), mute ("MUTE", t.mute, t),
      outputLeft (t), outputRight (t), meter (t), theme (t), phones (isPhones)
{
    auto& e = ctx.engine;
    auto& src = phones ? e.phones.source : e.speakers;
    source.caption = "SOURCE";
    source.device = devices::wing; // the WING's monitor source list
    source.items = []
    {
        juce::StringArray names;
        for (int i = 0; i < ld::kNumMonitorSources; ++i)
            names.add (MixerEngine::monitorSourceName (i));
        return names;
    };
    source.current = [&src] { return src.source.load(); };
    source.choose = [&src] (int i) { src.source = i; };
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
    bindOutput (outputLeft, ctx, "OUTPUT L", phones ? e.phones.outputLeft : e.monitorOutputLeft);
    bindOutput (outputRight, ctx, "OUTPUT R", phones ? e.phones.outputRight : e.monitorOutputRight);
    meter.setChannels (2);
    meter.level = [&ctx, isPhones] (int ch) { return isPhones ? ctx.meters.getPhones (ch) : ctx.meters.getMonitor (ch); };

    rows = { { &source, kRow }, { &sourceLevel, kRow }, { &level, kRow } };
    if (! phones)
        rows.push_back ({ nullptr, sizes::kTouch }); // DIM, MONO, MUTE
    rows.push_back ({ &dimAmount, kRow });
    rows.push_back ({ &outputLeft, kRow });
    rows.push_back ({ &outputRight, kRow });
    for (auto& [c, h] : rows)
        if (c != nullptr)
            addAndMakeVisible (c);
    if (! phones)
        for (auto* k : { &dim, &mono, &mute })
            addAndMakeVisible (k);
    addAndMakeVisible (meter);
}

void MonitorView::Path::refresh()
{
    source.refresh();
    sourceLevel.refresh();
    level.refresh();
    dimAmount.refresh();
    dim.refresh();
    mono.refresh();
    mute.refresh();
    outputLeft.refresh();
    outputRight.refresh();
    meter.refresh();
}

void MonitorView::Path::paint (juce::Graphics& g)
{
    g.setColour (theme.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), sizes::kRadius);
    g.setColour (theme.text);
    g.setFont (type::font (type::kLabel + 2.0f, true));
    g.drawText (phones ? "PHONES" : "SPEAKERS", getLocalBounds().removeFromTop (kTitle).reduced (sizes::kGap + 4, 0), juce::Justification::centredLeft);
}

void MonitorView::Path::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap);
    area.removeFromTop (kTitle - sizes::kGap);
    meter.setBounds (area.removeFromRight (16));
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
}

//==============================================================================
MonitorView::MonitorView (UiContext& c, const Theme& t)
    : speakers (c, t, false), phones (c, t, true), soloBus (t), soloMode (t), aflChannels ("AFL CHANNELS", t.accent, t),
      aflBuses ("AFL BUSES", t.accent, t), aflMains ("AFL MAINS", t.accent, t), aflMatrices ("AFL MATRICES", t.accent, t),
      clearSolo ("CLEAR SOLO", t.solo, t), ctx (c), theme (t)
{
    auto& e = ctx.engine;
    soloBus.caption = "SOLO TO";
    soloBus.device = devices::wing;
    soloBus.items = [] { return models::list (models::MonitorBuses); };
    soloBus.current = [&e] { return e.soloBus.load(); };
    soloBus.choose = [&e] (int i) { e.setSoloBus (i); };
    soloMode.caption = "SOLO MODE";
    soloMode.device = devices::wing; // LIVE, STUDIO, SIP
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
    for (auto* child : std::initializer_list<juce::Component*> { &speakers, &phones, &soloBus, &soloMode, &aflChannels, &aflBuses, &aflMains,
                                                                 &aflMatrices, &clearSolo })
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
}

void MonitorView::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    auto area = getLocalBounds().reduced (sizes::kGap);
    const int w = (area.getWidth() - 2 * sizes::kGap) / 3;
    auto solo = area.withTrimmedLeft (2 * (w + sizes::kGap)).withWidth (w);
    g.setColour (theme.panel);
    g.fillRoundedRectangle (solo.toFloat(), sizes::kRadius);
    g.setColour (theme.text);
    g.setFont (type::font (type::kLabel + 2.0f, true));
    g.drawText ("SOLO", solo.removeFromTop (kTitle).reduced (sizes::kGap + 4, 0), juce::Justification::centredLeft);
}

void MonitorView::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap);
    const int w = (area.getWidth() - 2 * sizes::kGap) / 3;
    speakers.setBounds (area.removeFromLeft (w));
    area.removeFromLeft (sizes::kGap);
    phones.setBounds (area.removeFromLeft (w));
    area.removeFromLeft (sizes::kGap);
    auto solo = area.withWidth (w).reduced (sizes::kGap);
    solo.removeFromTop (kTitle - sizes::kGap);
    layoutRows (solo, { { &soloBus, kRow }, { &soloMode, kRow }, { &aflChannels, sizes::kTouch }, { &aflBuses, sizes::kTouch },
                        { &aflMains, sizes::kTouch }, { &aflMatrices, sizes::kTouch }, { &clearSolo, sizes::kTouch } },
                sizes::kGapSmall);
}

} // namespace livedaw::ui::universal
