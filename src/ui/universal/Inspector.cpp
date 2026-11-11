#include "ui/universal/Inspector.h"

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kHeader = 60;
} // namespace

Inspector::Inspector (UiContext& c, const Theme& t)
    : chainTab ("CHAIN", t.accent, t), sendsTab ("SENDS", t.accent, t), stripTab ("STRIP", t.accent, t), closeKey ("CLOSE", t.accent, t), prevKey ("<", t.accent, t),
      nextKey (">", t.accent, t), chain (c, t),
      editor (c, t), sends (c, t), stripPage (c, t), ctx (c), theme (t)
{
    for (auto* k : { &chainTab, &sendsTab, &stripTab })
        k->tab = true;
    chainTab.isOn = [this] { return page == Page::Chain; };
    chainTab.onClick = [this] { showPage (Page::Chain); };
    sendsTab.isOn = [this] { return page == Page::Sends; };
    sendsTab.onClick = [this] { showPage (Page::Sends); };
    stripTab.isOn = [this] { return page == Page::Strip; };
    stripTab.onClick = [this] { showPage (Page::Strip); };
    stripScroller.setViewedComponent (&stripPage, false);
    stripScroller.setScrollBarsShown (true, false);
    stripScroller.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    closeKey.onClick = [this]
    {
        if (onClose)
            onClose();
    };
    // The selected row's editor (none for a marker or a tap).
    chain.onSelectionChanged = [this] { editSelectedStep(); };
    prevKey.onClick = [this] { stepStrip (-1); };
    nextKey.onClick = [this] { stepStrip (1); };
    prevKey.setTooltip ("The strip before this one");
    nextKey.setTooltip ("The strip after this one");
    for (auto* child : std::initializer_list<juce::Component*> { &chainTab, &sendsTab, &stripTab, &closeKey, &prevKey, &nextKey, &chain, &editor, &sends, &stripScroller })
        addChildComponent (child);
    for (auto* key : { &chainTab, &sendsTab, &stripTab, &closeKey, &prevKey, &nextKey })
        key->setVisible (true);
    showPage (Page::Chain);
    startTimerHz (30);
}

Inspector::~Inspector()
{
    stopTimer();
}

void Inspector::setStrip (Strip* s)
{
    if (s == strip)
    {
        // The same strip after a change elsewhere: its chain again, and the selected block's values.
        chain.refresh();
        editSelectedStep();
        return;
    }
    // Going through the strips stays on the block being worked on (the EQ of each, say).
    // An extra block or a tap is that strip's own: there the EQ.
    int keepKind = ld::kStepEq;
    if (const int row = chain.getSelectedRow(); juce::isPositiveAndBelow (row, (int) chain.getRows().size()))
        if (const auto& step = chain.getRows()[(size_t) row].step; ! step.isBlock() && ! step.isUserTap())
            keepKind = step.kind;
    // The editor lets go of the old strip's blocks first.
    editor.setTarget ({});
    strip = s;
    structureVersion = ctx.engine.getStructureVersion();
    chain.setStrip (s);
    if (s != nullptr && ! chain.selectKind (keepKind))
        chain.selectKind (ld::kStepEq);
    sends.setStrip (s);
    stripPage.setStrip (s);
    resized();
    setTitle (s != nullptr ? s->getLabel() : juce::String());
    repaint();
}

void Inspector::showPage (Page p)
{
    page = p;
    chain.setVisible (p == Page::Chain);
    editor.setVisible (p == Page::Chain);
    sends.setVisible (p == Page::Sends);
    stripScroller.setVisible (p == Page::Strip);
    chainTab.refresh();
    sendsTab.refresh();
    stripTab.refresh();
    if (p == Page::Sends)
        sends.refresh();
}

void Inspector::openStep (int stepKind, int uid)
{
    if (stepKind == kOpenSends)
    {
        showPage (Page::Sends);
        return;
    }
    showPage (Page::Chain);
    if (chain.selectKind (stepKind, uid))
        return;
    // A console whose chain has no INPUT row (a PRO2): SOURCE / ROUTING opens the input block's own
    // editor (source, trim, delay, routing) without a row, rather than a neighbour's block.
    if (stepKind == ld::kStepInput && strip != nullptr)
    {
        ChainStep input;
        input.kind = ld::kStepInput;
        editor.setTarget (BlockTarget::of (ctx, *strip, input));
        return;
    }
    if (uid >= 0)
        chain.selectKind (stepKind);
}

void Inspector::stepStrip (int delta)
{
    if (strip == nullptr || ! ctx.selectStrip)
        return;
    std::vector<Strip*> list;
    for (int k = 0; k < kNumStripKinds; ++k)
        for (auto& s : ctx.engine.getStrips (static_cast<StripKind> (k)))
            list.push_back (s.get());
    const int n = (int) list.size();
    for (int i = 0; i < n; ++i)
        if (list[(size_t) i] == strip)
        {
            ctx.selectStrip (list[(size_t) ((i + delta % n + n) % n)]);
            return;
        }
}

void Inspector::editSelectedStep()
{
    const int row = chain.getSelectedRow();
    if (strip == nullptr || ! juce::isPositiveAndBelow (row, (int) chain.getRows().size()))
    {
        editor.setTarget ({});
        return;
    }
    editor.setTarget (BlockTarget::of (ctx, *strip, chain.getRows()[(size_t) row].step));
}

void Inspector::refresh()
{
    // The console's strips changed (a show load, a strip added or removed): the
    // strip may be gone, so the selection again before anything reads it.
    if (ctx.engine.getStructureVersion() != structureVersion)
    {
        structureVersion = ctx.engine.getStructureVersion();
        setStrip (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr);
        sends.setStrip (strip);
    }
    if (strip == nullptr)
        return;
    // The chain first: a block that went takes its editor with it.
    chain.refresh();
    if (chain.preferredHeight() != chainHeight)
        resized(); // a block added or removed: as tall as its rows
    if (page == Page::Chain)
        editor.refresh();
    else if (page == Page::Sends)
        sends.refresh();
    // The STRIP page takes its values by itself (6 a second).
}

void Inspector::paint (juce::Graphics& g)
{
    g.fillAll (theme.panel);
    g.setColour (theme.outline);
    g.fillRect (0, 0, 1, getHeight());

    auto header = getLocalBounds().removeFromTop (kHeader).reduced (sizes::kGapLarge, sizes::kGap);
    header.setRight (prevKey.getX() - sizes::kGap);
    if (strip == nullptr)
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel));
        g.drawText ("Select a strip", header, juce::Justification::centredLeft);
        return;
    }
    g.setColour (consoleColour (strip->getColourIndex()));
    g.fillRoundedRectangle (header.removeFromLeft (6).toFloat(), sizes::kRadius);
    header.removeFromLeft (sizes::kGap);
    g.setColour (theme.kindColour (strip->getKind()));
    g.setFont (type::font (type::kCaption, true));
    g.drawText (strip->getLabel(), header.removeFromTop (header.getHeight() / 2), juce::Justification::bottomLeft);
    g.setColour (theme.text);
    g.setFont (type::font (type::kHeading, true));
    // Its name (a strip without one: a dash, its label is just above).
    const auto name = strip->getName();
    g.drawText (name.isNotEmpty() && name != strip->getLabel() ? name : juce::String::fromUTF8 ("\xe2\x80\x94"), header, juce::Justification::topLeft);
}

void Inspector::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap, 0);
    auto header = area.removeFromTop (kHeader).reduced (sizes::kGap, (kHeader - sizes::kTouch) / 2);
    closeKey.setBounds (header.removeFromRight (84));
    header.removeFromRight (sizes::kGapSmall);
    nextKey.setBounds (header.removeFromRight (sizes::kTouch));
    header.removeFromRight (sizes::kGapSmall);
    prevKey.setBounds (header.removeFromRight (sizes::kTouch));
    auto tabs = area.removeFromTop (sizes::kTabBar);
    const int tabW = tabs.getWidth() / 3;
    chainTab.setBounds (tabs.removeFromLeft (tabW).withTrimmedRight (sizes::kGapSmall / 2));
    sendsTab.setBounds (tabs.removeFromLeft (tabW).reduced (sizes::kGapSmall / 2, 0));
    stripTab.setBounds (tabs.withTrimmedLeft (sizes::kGapSmall / 2));
    area.removeFromTop (sizes::kGap);
    sends.setBounds (area);
    stripScroller.setBounds (area);
    const int pageW = area.getWidth() - stripScroller.getScrollBarThickness();
    stripPage.setSize (pageW, stripPage.heightFor (pageW));
    // The chain list above the selected block's editor: as tall as its rows, up to about half.
    chainHeight = chain.preferredHeight();
    chain.setBounds (area.removeFromTop (juce::jlimit (juce::jmin (180, area.getHeight()), juce::jmax (180, area.getHeight() * 55 / 100), chainHeight)));
    area.removeFromTop (sizes::kGap);
    editor.setBounds (area);
}

} // namespace livedaw::ui::universal
