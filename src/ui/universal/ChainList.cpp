#include "ui/universal/ChainList.h"

#include "engine/Models.h"

#include "ui/ChainSummary.h"
#include "ui/universal/BlockEditor.h"

#include <algorithm>

namespace livedaw::ui::universal
{

namespace
{
    bool sameStep (const ChainStep& a, const ChainStep& b)
    {
        return a.kind == b.kind && a.arg == b.arg && a.uid == b.uid && a.slot == b.slot;
    }
} // namespace

//==============================================================================
/** The rows, drawn and touched (a drag moves a movable row). */
class ChainList::Rows final : public juce::Component
{
public:
    explicit Rows (ChainList& o) : owner (o) { setViewportIgnoreDragFlag (true); }

    static constexpr int kRow = sizes::kTouch, kMarker = 24, kGap = 4, kLightWidth = 40;

    int heightOf (int row) const { return owner.rows[(size_t) row].marker ? kMarker : kRow; }

    int topOf (int row) const
    {
        int y = 0;
        for (int i = 0; i < row; ++i)
            y += heightOf (i) + kGap;
        return y;
    }

    int totalHeight() const { return topOf ((int) owner.rows.size()); }

    int rowAt (int y) const
    {
        for (int i = 0; i < (int) owner.rows.size(); ++i)
            if (y >= topOf (i) && y < topOf (i) + heightOf (i))
                return i;
        return -1;
    }

    /** The row a drop at `y` goes before (the number of rows: the end). */
    int dropAt (int y) const
    {
        for (int i = 0; i < (int) owner.rows.size(); ++i)
            if (y < topOf (i) + heightOf (i) / 2)
                return i;
        return (int) owner.rows.size();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.theme;
        for (int i = 0; i < (int) owner.rows.size(); ++i)
        {
            const auto& row = owner.rows[(size_t) i];
            const auto r = juce::Rectangle<int> (0, topOf (i), getWidth(), heightOf (i)).toFloat();
            if (row.marker)
            {
                // A marker: a line through its name.
                g.setColour (t.outline);
                g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);
                g.setColour (t.textDim);
                g.setFont (type::font (type::kCaption, true));
                const auto label = r.withSizeKeepingCentre (96.0f, r.getHeight());
                g.setColour (t.panel);
                g.fillRect (label);
                g.setColour (t.textDim);
                g.drawText (row.name, label, juce::Justification::centred);
                continue;
            }
            const bool sel = i == owner.selected;
            g.setColour (sel ? t.lifted (t.raised, 0.1f) : t.raised);
            g.fillRoundedRectangle (r.reduced (0.5f), sizes::kRadius);
            g.setColour (sel ? t.accent : t.outline);
            g.drawRoundedRectangle (r.reduced (0.5f), sizes::kRadius, sel ? 2.0f : 1.0f);

            auto content = r.reduced (12.0f, 0.0f);
            // Its ON as a light (a tap's mark instead).
            const auto dot = content.removeFromLeft (12.0f).withSizeKeepingCentre (10.0f, 10.0f);
            if (row.step.isUserTap())
            {
                g.setColour (t.solo);
                g.fillRect (dot.withSizeKeepingCentre (10.0f, 3.0f));
            }
            else if (row.step.kind != ld::kStepInput)
            {
                g.setColour (owner.lit[(size_t) i] ? t.meterLow : t.well);
                g.fillEllipse (dot);
            }
            content.removeFromLeft (10.0f);
            if (row.movable)
            {
                // A grip: it can be dragged.
                auto grip = content.removeFromRight (18.0f);
                g.setColour (t.textDim);
                for (int k = -1; k <= 1; ++k)
                    g.fillRect (grip.getX(), grip.getCentreY() + (float) k * 5.0f - 1.0f, grip.getWidth(), 2.0f);
            }
            // The console the block comes from (its WING template's), then a process order slot's place.
            content.removeFromRight (drawDeviceTag (g, content, row.device, t));
            if (row.step.slot >= 0)
            {
                // A process order slot: it moves with the order, not on its own.
                g.setColour (t.textDim);
                g.setFont (type::font (type::kCaption));
                g.drawText ("ORDER " + juce::String (row.step.slot + 1), content, juce::Justification::centredRight);
            }
            g.setColour (row.step.isBlock() || row.step.isUserTap() ? t.accent : t.text);
            g.setFont (type::font (type::kLabel, true));
            g.drawText (row.name, content, juce::Justification::centredLeft);
        }
        if (dragging)
        {
            const int y = drop < (int) owner.rows.size() ? topOf (drop) - kGap / 2 : totalHeight() - kGap / 2;
            g.setColour (t.accent);
            g.fillRect (0, y - 1, getWidth(), 3);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressed = rowAt (e.y);
        dragging = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (pressed < 0 || ! owner.rows[(size_t) pressed].movable)
            return;
        if (! dragging && e.getDistanceFromDragStart() < 8)
            return;
        dragging = true;
        drop = dropAt (e.y);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragging)
        {
            dragging = false;
            repaint();
            owner.moveRow (pressed, drop);
        }
        else if (pressed >= 0 && ! owner.rows[(size_t) pressed].marker)
        {
            // Its light is a switch: bypass a block without opening it.
            if (e.x < kLightWidth && owner.canToggle (pressed))
                owner.toggleRow (pressed);
            else
                owner.selectRow (pressed);
        }
        pressed = -1;
    }

private:
    ChainList& owner;
    int pressed = -1, drop = 0;
    bool dragging = false;
};

//==============================================================================
ChainList::ChainList (UiContext& c, const Theme& t)
    : addKey ("ADD", t.accent, t), removeKey ("REMOVE", t.mute, t), ctx (c), theme (t), rowsView (std::make_unique<Rows> (*this))
{
    viewport.setViewedComponent (rowsView.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme.outline);
    addAndMakeVisible (viewport);

    addKey.onClick = [this]
    {
        auto menu = buildAddMenu();
        juce::Component::SafePointer<ChainList> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addKey).withStandardItemHeight (sizes::kTouch).withMinimumWidth (220),
                            [safe] (int result)
                            {
                                if (safe == nullptr || result <= 0)
                                    return;
                                if (result == 101)
                                    safe->choosePlugin();
                                else if (result == 102)
                                    safe->addHostRack();
                                else if (result == 100)
                                    safe->add (std::nullopt);
                                else
                                    safe->add (static_cast<BlockKind> (result - 1));
                            });
    };
    removeKey.onClick = [this] { removeRow (selected); };
    addAndMakeVisible (addKey);
    addAndMakeVisible (removeKey);
}

ChainList::~ChainList() = default;

juce::PopupMenu ChainList::buildAddMenu() const
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeelFor (theme));
    int id = 1;
    for (auto kind : { BlockKind::Filter, BlockKind::Eq, BlockKind::Dynamics, BlockKind::Insert, BlockKind::Delay })
        menu.addItem (id++, stepName ({ ld::kStepBlock, (int) kind }));
    menu.addSeparator();
    // A hosted plugin (VST3, AU, LV2), not in an FX slot; a real console under remote control runs its own processing.
    if (! models::remoteHeld())
    {
        menu.addItem (101, "PLUG-IN...");
        // The PLUGIN HOST's rack as a device of this console's strip: the host's tiles, docked editors,
        // sidechain keys and hot knobs, on a plug-in block.
        menu.addItem (102, "PLUGIN HOST RACK");
    }
    menu.addItem (100, "USER TAP (for sends)");
    return menu;
}

void ChainList::setStrip (Strip* s)
{
    if (s == strip)
        return;
    strip = s;
    shownChain.clear();
    rows.clear();
    lit.clear();
    selected = -1;
    refresh();
    if (onSelectionChanged)
        onSelectionChanged();
}

bool ChainList::sameChain (const std::vector<ChainStep>& a, const std::vector<ChainStep>& b)
{
    return a.size() == b.size() && std::equal (a.begin(), a.end(), b.begin(), sameStep);
}

bool ChainList::isOn (const ChainStep& s) const
{
    return strip != nullptr && stepOn (*strip, s);
}

juce::String ChainList::deviceOf (const ChainStep& s, bool own) const
{
    if (strip != nullptr && s.isBlock() && s.getBlockKind() == BlockKind::Plugin && strip->isBlockHostRack (s.uid))
        return "PLUGIN HOST";
    const bool holdsModel = s.isBlock() || s.kind == ld::kStepGate || s.kind == ld::kStepEq || s.kind == ld::kStepDynamics;
    if (strip != nullptr && holdsModel)
        return BlockTarget::of (ctx, *strip, s).tag();
    return own ? devices::shown() : juce::String();
}

void ChainList::rebuild (const std::vector<ChainStep>& chain)
{
    const auto before = juce::isPositiveAndBelow (selected, (int) rows.size()) ? std::optional<ChainStep> (rows[(size_t) selected].step) : std::nullopt;
    shownChain = chain;
    rows.clear();
    for (int i = 0; i < (int) chain.size(); ++i)
    {
        const auto& s = chain[(size_t) i];
        if (! isShown (s))
            continue;
        const bool own = ! s.isBlock() && ! s.isUserTap() && ! isMarker (s);
        // A dLive's preamp is part of its channel: it can't be moved or removed.
        const bool preamp = strip != nullptr && s.isBlock() && s.uid == ld::ld_strip_preamp_block (strip->getHandle());
        rows.push_back ({ s, i, strip != nullptr ? stepName (*strip, s) : stepName (s), (s.isBlock() && ! preamp) || s.isUserTap(), isMarker (s), deviceOf (s, own) });
    }
    lit.assign (rows.size(), false);

    // The selection stays on its step if it's still there.
    selected = -1;
    if (before)
        for (int i = 0; i < (int) rows.size(); ++i)
            if (sameStep (rows[(size_t) i].step, *before))
                selected = i;
    rowsView->setSize (viewport.getWidth() - viewport.getScrollBarThickness(), rowsView->totalHeight());
    rowsView->repaint();
    if (before && selected < 0 && onSelectionChanged)
        onSelectionChanged();
}

void ChainList::refresh()
{
    if (strip == nullptr)
    {
        if (! rows.empty())
            rebuild ({});
    }
    else
    {
        const auto chain = strip->getChain();
        if (! sameChain (chain, shownChain))
            rebuild (chain);
    }
    bool changed = false;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const bool on = isOn (rows[i].step);
        if (on != lit[i])
        {
            lit[i] = on;
            changed = true;
        }
        // The tag follows the block's model: swapped for another console's, it names that console.
        const auto device = deviceOf (rows[i].step, ! rows[i].step.isBlock() && ! rows[i].step.isUserTap() && ! rows[i].marker);
        if (device != rows[i].device)
        {
            rows[i].device = device;
            changed = true;
        }
    }
    if (changed)
        rowsView->repaint();
    addKey.setEnabled (strip != nullptr);
    removeKey.setEnabled (juce::isPositiveAndBelow (selected, (int) rows.size()) && rows[(size_t) selected].movable);
}

void ChainList::selectRow (int row)
{
    const int r = juce::isPositiveAndBelow (row, (int) rows.size()) && ! rows[(size_t) row].marker ? row : -1;
    if (r == selected)
        return;
    selected = r;
    rowsView->repaint();
    removeKey.setEnabled (r >= 0 && rows[(size_t) r].movable);
    if (onSelectionChanged)
        onSelectionChanged();
}

bool ChainList::selectKind (int kind, int uid)
{
    for (int i = 0; i < (int) rows.size(); ++i)
        if (rows[(size_t) i].step.kind == kind && ! rows[(size_t) i].marker && (uid < 0 || rows[(size_t) i].step.uid == uid))
        {
            selectRow (i);
            return true;
        }
    return false;
}

int ChainList::preferredHeight() const
{
    return rowsView->totalHeight() + sizes::kGap + sizes::kTouch;
}

std::atomic<bool>* ChainList::switchOf (const ChainStep& s) const
{
    if (strip == nullptr)
        return nullptr;
    auto& p = strip->params;
    switch (s.kind)
    {
        case ld::kStepGate:       return &p.gate().on;
        case ld::kStepEq:         return &p.eq().on;
        case ld::kStepDynamics:   return &p.dynamics().on;
        case ld::kStepPreInsert:  return &p.preInsert().on;
        case ld::kStepPostInsert: return &p.postInsert().on;
        case ld::kStepDelay:      return &p.input.delayOn;
        case ld::kStepBlock:
        {
            const auto block = strip->getBlock (s.uid);
            if (! block.isValid())
                return nullptr;
            switch (block.getKind())
            {
                case BlockKind::Eq:       return &block.eq()->on;
                case BlockKind::Dynamics: return &block.dynamics()->on;
                case BlockKind::Insert:   return &block.insert()->on;
                case BlockKind::Delay:    return &block.delay()->on;
                case BlockKind::Plugin:   return block.plugin();
                case BlockKind::Filter:   break;
            }
            return nullptr;
        }
        default:
            return nullptr;
    }
}

bool ChainList::canToggle (int row) const
{
    return juce::isPositiveAndBelow (row, (int) rows.size()) && switchOf (rows[(size_t) row].step) != nullptr;
}

void ChainList::toggleRow (int row)
{
    if (! canToggle (row))
        return;
    auto* on = switchOf (rows[(size_t) row].step);
    on->store (! on->load());
    refresh();
}

void ChainList::selectStep (std::function<bool (const ChainStep&)> match)
{
    for (int i = 0; i < (int) rows.size(); ++i)
        if (match (rows[(size_t) i].step))
        {
            selectRow (i);
            return;
        }
}

int ChainList::insertionPoint() const
{
    int at = -1;
    if (juce::isPositiveAndBelow (selected, (int) rows.size()))
        at = rows[(size_t) selected].stepIndex + 1;
    else
        for (const auto& r : rows)
            if (r.step.kind == ld::kStepPfl)
                at = r.stepIndex;
    return at;
}

void ChainList::choosePlugin()
{
    if (strip == nullptr || models::remoteHeld())
        return;
    juce::Component::SafePointer<ChainList> safe (this);
    auto menu = pluginTypesMenu (ctx, [safe] (const juce::PluginDescription& desc)
    {
        if (safe != nullptr)
            safe->addPlugin (desc);
    });
    if (menu.getNumItems() == 0)
        menu.addItem ("Scan for plugins...", [this] { if (ctx.showPluginScanner) ctx.showPluginScanner(); });
    menu.setLookAndFeel (&lookAndFeelFor (theme));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addKey).withStandardItemHeight (sizes::kTouch));
}

bool ChainList::addPlugin (const juce::PluginDescription& desc)
{
    if (strip == nullptr)
        return false;
    const int uid = strip->insertBlock (BlockKind::Plugin, insertionPoint());
    if (uid < 0)
        return false;
    const int slot = FxSlots::blockSlotOf (strip->getHandle(), uid);
    if (slot < 0 || ! loadPluginIntoFx (ctx, slot, desc))
    {
        strip->removeBlock (uid); // nothing was loaded into it: no editor to close
        refresh();
        return false;
    }
    refresh();
    selectStep ([uid] (const ChainStep& s) { return s.isBlock() && s.uid == uid; });
    if (ctx.openPluginEditor)
        ctx.openPluginEditor (slot);
    return true;
}

bool ChainList::addHostRack()
{
    if (strip == nullptr)
        return false;
    const int uid = strip->insertBlock (BlockKind::Plugin, insertionPoint()); // refused under remote control
    if (uid < 0)
        return false;
    strip->setBlockHostRack (uid, true);
    refresh();
    selectStep ([uid] (const ChainStep& s) { return s.isBlock() && s.uid == uid; });
    return true;
}

bool ChainList::add (std::optional<BlockKind> kind)
{
    if (strip == nullptr)
        return false;
    const int at = insertionPoint();

    if (kind)
    {
        const int uid = strip->insertBlock (*kind, at);
        if (uid < 0)
            return false;
        refresh();
        selectStep ([uid] (const ChainStep& s) { return s.isBlock() && s.uid == uid; });
    }
    else
    {
        const int id = strip->insertTap (at);
        if (id < 0)
            return false;
        refresh();
        selectStep ([id] (const ChainStep& s) { return s.isUserTap() && s.arg == id; });
    }
    return true;
}

bool ChainList::moveRow (int row, int before)
{
    if (strip == nullptr || ! juce::isPositiveAndBelow (row, (int) rows.size()) || ! rows[(size_t) row].movable)
        return false;
    const auto step = rows[(size_t) row].step;
    const int from = rows[(size_t) row].stepIndex;
    const int to = juce::isPositiveAndBelow (before, (int) rows.size()) ? rows[(size_t) before].stepIndex : (int) shownChain.size();
    const int at = to > from ? to - 1 : to; // in the chain without it
    const bool moved = step.isBlock() ? strip->moveBlock (step.uid, at) : strip->moveTap (step.arg, at);
    refresh();
    selectStep ([step] (const ChainStep& s) { return sameStep (s, step); });
    return moved;
}

bool ChainList::removeRow (int row)
{
    if (strip == nullptr || ! juce::isPositiveAndBelow (row, (int) rows.size()) || ! rows[(size_t) row].movable)
        return false;
    const auto step = rows[(size_t) row].step;
    // Nothing may hold the block when it goes: deselect first (the editor lets go of it).
    selectRow (-1);
    // A plugin block: its plugin's window closes before the engine lets the plugin go.
    if (step.isBlock() && step.getBlockKind() == BlockKind::Plugin && ctx.pluginRemoved)
        if (auto* plugin = ctx.engine.getFx().getPlugin (FxSlots::blockSlotOf (strip->getHandle(), step.uid)))
            ctx.pluginRemoved (plugin);
    const bool removed = step.isBlock() ? strip->removeBlock (step.uid) : strip->removeTap (step.arg);
    refresh();
    return removed;
}

void ChainList::paint (juce::Graphics& g)
{
    g.fillAll (theme.panel);
}

void ChainList::resized()
{
    auto area = getLocalBounds();
    auto keys = area.removeFromBottom (sizes::kTouch);
    area.removeFromBottom (sizes::kGap);
    removeKey.setBounds (keys.removeFromRight (keys.getWidth() / 2).withTrimmedLeft (sizes::kGapSmall / 2));
    addKey.setBounds (keys.withTrimmedRight (sizes::kGapSmall / 2));
    viewport.setBounds (area);
    rowsView->setSize (viewport.getWidth() - viewport.getScrollBarThickness(), rowsView->totalHeight());
}

} // namespace livedaw::ui::universal
