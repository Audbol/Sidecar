#include "ui/PluginHostOverview.h"

namespace livedaw::ui::ph
{
using namespace universal;

namespace
{
    constexpr int kScrollBar = 14;
} // namespace

//==============================================================================

/** What takes the touches and draws the columns; the viewport scrolls it sideways. */
class OverviewPage::Canvas final : public juce::Component
{
public:
    explicit Canvas (OverviewPage& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.theme;
        g.fillAll (t.background);
        const int selected = owner.selectedChannel();
        const int selectedUid = owner.getSelectedUid ? owner.getSelectedUid() : -1;
        for (int c = 0; c < owner.getNumColumns(); ++c)
        {
            const auto& col = owner.columns[(size_t) c];
            const bool isSelected = col.channel == selected;

            // The head: the channel, its name and its BYPASS state.
            const auto head = owner.getHeadBounds (c).toFloat();
            g.setColour (isSelected ? t.lifted (t.raised, 0.08f) : t.panel);
            g.fillRoundedRectangle (head, 6.0f);
            g.setColour (isSelected ? t.accent : t.outline);
            g.drawRoundedRectangle (head.reduced (1.0f), 6.0f, isSelected ? 2.5f : 1.0f);
            auto area = head.reduced (8.0f, 4.0f);
            g.setColour (col.bypassed ? t.textDim : t.text);
            g.setFont (type::font (type::kHeading, true));
            g.drawText ("CH " + juce::String (col.channel + 1), area.removeFromTop (22.0f), juce::Justification::centredLeft);
            g.setColour (t.textDim);
            g.setFont (type::font (type::kCaption));
            g.drawFittedText (col.name, area.removeFromTop (16.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            const auto pill = area.removeFromBottom (22.0f).reduced (0.0f, 1.0f);
            g.setColour (col.bypassed ? t.mute : t.well);
            g.fillRoundedRectangle (pill, 4.0f);
            g.setColour (col.bypassed ? t.mute : t.outline);
            g.drawRoundedRectangle (pill.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (col.bypassed ? t.textOnAccent : t.textDim);
            g.setFont (type::font (type::kMicro, true));
            g.drawText (col.bypassed ? "BYPASS ON" : "BYPASS", pill, juce::Justification::centred);

            // The blocks, in signal order.
            const int blocks = (int) col.tiles.size();
            const int shown = shownTiles (blocks);
            for (int i = 0; i < shown + (hasMore (blocks) ? 1 : 0); ++i)
            {
                const auto r = owner.getTileBounds (c, i).toFloat();
                if (i == shown && hasMore (blocks))
                {
                    g.setColour (t.well);
                    g.fillRoundedRectangle (r, 6.0f);
                    g.setColour (t.outline);
                    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
                    g.setColour (t.accent);
                    g.setFont (type::font (type::kLabel, true));
                    g.drawText ("+" + juce::String (blocks - shown) + " MORE", r, juce::Justification::centred);
                    continue;
                }
                const auto& tile = col.tiles[(size_t) i];
                const bool dim = col.bypassed || tile.bypassed || tile.missing || (tile.kind == BlockKind::Plugin && ! tile.loaded);
                const bool sel = isSelected && tile.uid == selectedUid;
                g.setColour (dim ? t.well : t.raised);
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (sel ? t.accent : t.outline);
                g.drawRoundedRectangle (r.reduced (sel ? 1.0f : 0.5f), 6.0f, sel ? 2.5f : 1.0f);
                auto inner = r.reduced (6.0f, 3.0f);
                g.setColour (dim ? t.textDim : t.text);
                g.setFont (type::font (type::kCaption, true));
                g.drawFittedText (tile.name, inner.removeFromTop (30.0f).toNearestInt(), juce::Justification::topLeft, 2, 0.8f);
                juce::String status;
                if (tile.missing)
                    status = "MISSING";
                else if (tile.kind == BlockKind::Plugin && ! tile.loaded)
                    status = "EMPTY";
                else if (tile.faulted)
                    status = "FAULT";
                else if (tile.bypassed)
                    status = "BYP";
                g.setFont (type::font (type::kMicro));
                g.setColour (t.textDim);
                const auto latency = tile.kind == BlockKind::Plugin ? juce::String (tile.latency) + " smp" : juce::String ("BUILT-IN");
                g.drawText (latency, inner.removeFromLeft (inner.getWidth() * 0.6f), juce::Justification::bottomLeft);
                g.setColour (tile.faulted || tile.missing ? t.mute : t.accent);
                g.setFont (type::font (type::kMicro, true));
                g.drawText (status, inner, juce::Justification::bottomRight);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressed = hit (e.position);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto was = pressed;
        pressed = { -1, -1 };
        if (e.mouseWasDraggedSinceMouseDown() || was.first < 0 || hit (e.position) != was)
            return;
        if (was.second < 0)
            owner.tapHead (was.first);
        else
            owner.tapTile (was.first, was.second);
    }

private:
    /** The column and the tile (-1: the head) at `p`; -1, -1 for nothing. */
    std::pair<int, int> hit (juce::Point<float> p) const
    {
        for (int c = 0; c < owner.getNumColumns(); ++c)
        {
            if (owner.getHeadBounds (c).toFloat().contains (p))
                return { c, -1 };
            const int blocks = (int) owner.columns[(size_t) c].tiles.size();
            for (int i = 0; i < shownTiles (blocks) + (hasMore (blocks) ? 1 : 0); ++i)
                if (owner.getTileBounds (c, i).toFloat().contains (p))
                    return { c, i };
        }
        return { -1, -1 };
    }

    OverviewPage& owner;
    std::pair<int, int> pressed { -1, -1 };
};

//==============================================================================

OverviewPage::OverviewPage (UiContext& c, const Theme& t) : ctx (c), theme (t), canvas (std::make_unique<Canvas> (*this))
{
    viewport.setViewedComponent (canvas.get(), false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (kScrollBar);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    viewport.getHorizontalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme.outline);
    addAndMakeVisible (viewport);
    refresh();
    startTimerHz (4);
}

OverviewPage::~OverviewPage()
{
    stopTimer();
}

juce::Component& OverviewPage::getCanvas() noexcept
{
    return *canvas;
}

int OverviewPage::selectedChannel() const
{
    const auto* selected = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    const auto& list = ctx.engine.getChannels();
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].get() == selected)
            return i;
    return -1;
}

void OverviewPage::refresh()
{
    std::vector<Column> now;
    const auto& list = ctx.engine.getChannels();
    now.reserve (list.size());
    for (int i = 0; i < (int) list.size(); ++i)
    {
        auto& strip = *list[(size_t) i];
        Column col;
        col.channel = i;
        const auto own = strip.getOwnName();
        col.name = own == juce::String (i + 1) ? juce::String() : own;
        col.bypassed = strip.params.input.bypass.load();
        for (const auto& step : strip.getChain())
            if (step.isBlock())
            {
                col.tiles.push_back (RackPage::describeTile (ctx, strip, step));
                if (col.tiles.back().kind == BlockKind::Plugin && col.tiles.back().loaded && ! col.tiles.back().bypassed && ! col.tiles.back().faulted)
                    col.latency += col.tiles.back().latency;
            }
        now.push_back (std::move (col));
    }
    auto sig = juce::String (selectedChannel()) + ":" + juce::String (getSelectedUid ? getSelectedUid() : -1);
    for (const auto& col : now)
    {
        sig << "|" << col.name << (col.bypassed ? "b" : "");
        for (const auto& t : col.tiles)
            sig << "," << t.uid << t.name << t.latency << (int) t.bypassed << (int) t.faulted << (int) t.missing << (int) t.loaded;
    }
    if (sig == signature && now.size() == columns.size())
        return;
    signature = sig;
    const bool resize = now.size() != columns.size();
    columns = std::move (now);
    if (resize)
        canvas->setSize (juce::jmax (viewport.getWidth(), 2 * kMargin + getNumColumns() * (kColumnWidth + kColumnGap)), juce::jmax (1, viewport.getHeight() - kScrollBar));
    canvas->repaint();
}

juce::Rectangle<int> OverviewPage::getHeadBounds (int column) const
{
    return { columnX (column), 6, kColumnWidth, kHeadHeight };
}

juce::Rectangle<int> OverviewPage::getTileBounds (int column, int tile) const
{
    return { columnX (column), 6 + kHeadHeight + kTileGap + tile * (kTileHeight + kTileGap), kColumnWidth, kTileHeight };
}

void OverviewPage::tapHead (int column)
{
    if (juce::isPositiveAndBelow (column, getNumColumns()) && onJump)
        onJump (columns[(size_t) column].channel, -1);
}

void OverviewPage::tapTile (int column, int tile)
{
    if (! juce::isPositiveAndBelow (column, getNumColumns()) || ! onJump)
        return;
    const auto& col = columns[(size_t) column];
    // The "+N MORE" tile takes the first block that is not shown.
    const int index = juce::jmin (tile, (int) col.tiles.size() - 1);
    onJump (col.channel, index >= 0 ? col.tiles[(size_t) index].uid : -1);
}

void OverviewPage::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
}

void OverviewPage::resized()
{
    viewport.setBounds (getLocalBounds());
    canvas->setSize (juce::jmax (viewport.getWidth(), 2 * kMargin + getNumColumns() * (kColumnWidth + kColumnGap)), juce::jmax (1, viewport.getHeight() - kScrollBar));
}

} // namespace livedaw::ui::ph
