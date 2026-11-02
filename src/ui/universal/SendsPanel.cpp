#include "ui/universal/SendsPanel.h"

#include <algorithm>

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kLine = 40, kRowGap = 6;

    juce::String destinationName (const Strip& s)
    {
        const auto name = s.getName();
        return s.getLabel() + (name.isNotEmpty() ? "   " + name : juce::String());
    }

    juce::String tapName (int id)
    {
        return "TAP " + juce::String (id - ld::kFirstUserTap + 1);
    }
} // namespace

//==============================================================================
SendsPanel::Row::Row (const Theme& t, const juce::String& dest)
    : on ("ON", t.accent, t), level (t), source (t), destination (dest), theme (t)
{
    source.caption = "FROM";
    source.device = devices::wing; // the WING's send modes (then the core's user taps)
    for (auto* c : std::initializer_list<juce::Component*> { &on, &level, &source })
        addAndMakeVisible (c);
    setTitle (dest);
}

void SendsPanel::Row::paint (juce::Graphics& g)
{
    g.setColour (theme.raised.withAlpha (0.5f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), sizes::kRadius);
    auto line = getLocalBounds().removeFromTop (kLine).reduced (sizes::kGap, 0);
    line.removeFromRight (on.getWidth() + source.getWidth() + sizes::kGap * 2);
    g.setColour (theme.text);
    g.setFont (type::font (type::kLabel, true));
    g.drawFittedText (destination, line, juce::Justification::centredLeft, 1, 0.8f);
}

void SendsPanel::Row::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGapSmall);
    auto line = area.removeFromTop (kLine - sizes::kGapSmall);
    on.setBounds (line.removeFromRight (64));
    line.removeFromRight (sizes::kGapSmall);
    source.setBounds (line.removeFromRight (128));
    area.removeFromTop (sizes::kGapSmall);
    level.setBounds (area);
}

//==============================================================================
SendsPanel::SendsPanel (UiContext& c, const Theme& t) : ctx (c), theme (t)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme.outline);
    addAndMakeVisible (viewport);
}

void SendsPanel::setStrip (Strip* s)
{
    strip = s;
    rebuild();
}

SendsPanel::Row* SendsPanel::getRow (int index) const noexcept
{
    return juce::isPositiveAndBelow (index, (int) rows.size()) ? rows[(size_t) index].get() : nullptr;
}

std::vector<int> SendsPanel::userTaps() const
{
    std::vector<int> ids;
    if (strip != nullptr)
        for (const auto& step : strip->getChain())
            if (step.isUserTap())
                ids.push_back (step.arg);
    std::sort (ids.begin(), ids.end());
    return ids;
}

void SendsPanel::rebuild()
{
    rows.clear();
    taps = userTaps();
    structureVersion = ctx.engine.getStructureVersion();
    if (strip != nullptr)
    {
        auto& s = *strip;
        const auto kind = s.getKind();
        const bool toBusesAndMains = kind == StripKind::Channel || kind == StripKind::Aux || kind == StripKind::Bus;

        // Where a send takes its signal: `fixed` (the send's own modes), then the strip's user taps.
        const auto bindSource = [this] (ChoiceBar& source, juce::StringArray fixed, std::atomic<int32_t>& tap, std::function<int()> mode,
                                        std::function<void (int)> setMode)
        {
            const int n = fixed.size();
            source.items = [this, fixed]
            {
                auto items = fixed;
                for (int id : taps)
                    items.add (tapName (id));
                return items;
            };
            source.current = [this, n, &tap, mode]
            {
                const int t = tap.load();
                if (t < ld::kFirstUserTap)
                    return mode();
                const auto it = std::find (taps.begin(), taps.end(), t);
                return it != taps.end() ? n + (int) (it - taps.begin()) : -1;
            };
            source.choose = [this, n, &tap, setMode] (int i)
            {
                if (i < n)
                {
                    tap = -1;
                    setMode (i);
                }
                else if (i - n < (int) taps.size())
                {
                    tap = taps[(size_t) (i - n)];
                }
            };
        };

        if (toBusesAndMains)
            for (const auto& bus : ctx.engine.getStrips (StripKind::Bus))
            {
                if (bus.get() == strip)
                    continue;
                auto& p = s.sends[(size_t) bus->getId()];
                auto row = std::make_unique<Row> (theme, destinationName (*bus));
                row->on.isOn = [&p] { return p.enabled.load(); };
                row->on.onClick = [&p] { p.enabled = ! p.enabled.load(); };
                row->level.bind (ValueBar::level ("LEVEL", p.levelDb));
                bindSource (row->source, models::list (models::SendModes), p.tap, [&p] { return (int) p.mode.load(); },
                            [&p] (int m) { p.mode = m; });
                rows.push_back (std::move (row));
            }
        const juce::StringArray postPre { "POST", "PRE" };
        if (toBusesAndMains)
            for (int i = 0; i < (int) ctx.engine.getMains().size(); ++i)
            {
                auto& p = s.mains[(size_t) i];
                auto row = std::make_unique<Row> (theme, destinationName (*ctx.engine.getMains()[(size_t) i]));
                row->on.isOn = [&p] { return p.on.load(); };
                row->on.onClick = [&p] { p.on = ! p.on.load(); };
                row->level.bind (ValueBar::level ("LEVEL", p.levelDb));
                bindSource (row->source, postPre, p.tap, [&p] { return p.pre.load() ? 1 : 0; }, [&p] (int m) { p.pre = m == 1; });
                rows.push_back (std::move (row));
            }
        if (kind != StripKind::Matrix)
            for (int i = 0; i < (int) ctx.engine.getMatrices().size(); ++i)
            {
                auto& p = s.matrix[(size_t) i];
                auto row = std::make_unique<Row> (theme, destinationName (*ctx.engine.getMatrices()[(size_t) i]));
                row->on.isOn = [&p] { return p.on.load(); };
                row->on.onClick = [&p] { p.on = ! p.on.load(); };
                row->level.bind (ValueBar::level ("LEVEL", p.levelDb));
                bindSource (row->source, postPre, p.tap, [&p] { return p.pre.load() ? 1 : 0; }, [&p] (int m) { p.pre = m == 1; });
                rows.push_back (std::move (row));
            }
    }
    for (auto& r : rows)
        content.addAndMakeVisible (*r);
    resized();
    refresh();
}

void SendsPanel::refresh()
{
    // Another console (a show load, a bus added): other destinations.
    if (ctx.engine.getStructureVersion() != structureVersion)
    {
        rebuild();
        return;
    }
    taps = userTaps();
    for (auto& r : rows)
    {
        r->on.refresh();
        r->level.refresh();
        r->source.refresh();
    }
}

void SendsPanel::paint (juce::Graphics& g)
{
    if (strip != nullptr && rows.empty())
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel));
        g.drawText ("A matrix sends nowhere.", getLocalBounds().reduced (sizes::kGap), juce::Justification::centredTop);
    }
}

void SendsPanel::resized()
{
    viewport.setBounds (getLocalBounds());
    const int width = juce::jmax (0, viewport.getWidth() - viewport.getScrollBarThickness() - sizes::kGapSmall);
    int y = 0;
    for (auto& r : rows)
    {
        r->setBounds (0, y, width, kLine * 2 + sizes::kGapSmall);
        y += r->getHeight() + kRowGap;
    }
    content.setSize (width, y);
}

} // namespace livedaw::ui::universal
