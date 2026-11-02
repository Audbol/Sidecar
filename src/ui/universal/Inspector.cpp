#include "ui/universal/Inspector.h"

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kHeader = 60;
} // namespace

Inspector::Inspector (UiContext& c, const Theme& t)
    : chainTab ("CHAIN", t.accent, t), sendsTab ("SENDS", t.accent, t), closeKey ("CLOSE", t.accent, t), chain (c, t), editor (c, t), sends (c, t),
      ctx (c), theme (t)
{
    chainTab.isOn = [this] { return page == Page::Chain; };
    chainTab.onClick = [this] { showPage (Page::Chain); };
    sendsTab.isOn = [this] { return page == Page::Sends; };
    sendsTab.onClick = [this] { showPage (Page::Sends); };
    closeKey.onClick = [this]
    {
        if (onClose)
            onClose();
    };
    // The selected row's editor (none for a marker or a tap).
    chain.onSelectionChanged = [this] { editSelectedStep(); };
    for (auto* child : std::initializer_list<juce::Component*> { &chainTab, &sendsTab, &closeKey, &chain, &editor, &sends })
        addChildComponent (child);
    chainTab.setVisible (true);
    sendsTab.setVisible (true);
    closeKey.setVisible (true);
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
    // The editor lets go of the old strip's blocks first.
    editor.setTarget ({});
    strip = s;
    structureVersion = ctx.engine.getStructureVersion();
    chain.setStrip (s);
    sends.setStrip (s);
    setTitle (s != nullptr ? s->getLabel() : juce::String());
    repaint();
}

void Inspector::showPage (Page p)
{
    page = p;
    chain.setVisible (p == Page::Chain);
    editor.setVisible (p == Page::Chain);
    sends.setVisible (p == Page::Sends);
    chainTab.refresh();
    sendsTab.refresh();
    if (p == Page::Sends)
        sends.refresh();
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
    if (page == Page::Chain)
        editor.refresh();
    else
        sends.refresh();
}

void Inspector::paint (juce::Graphics& g)
{
    g.fillAll (theme.panel);
    g.setColour (theme.outline);
    g.fillRect (0, 0, 1, getHeight());

    auto header = getLocalBounds().removeFromTop (kHeader).reduced (sizes::kGapLarge, sizes::kGap);
    header.removeFromRight (closeKey.getWidth() + sizes::kGap);
    if (strip == nullptr)
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel));
        g.drawText ("Select a strip", header, juce::Justification::centredLeft);
        return;
    }
    g.setColour (wingColour (strip->getColourIndex()));
    g.fillRoundedRectangle (header.removeFromLeft (6).toFloat(), 3.0f);
    header.removeFromLeft (sizes::kGap);
    g.setColour (theme.kindColour (strip->getKind()));
    g.setFont (type::font (type::kCaption, true));
    g.drawText (strip->getLabel(), header.removeFromTop (header.getHeight() / 2), juce::Justification::bottomLeft);
    g.setColour (theme.text);
    g.setFont (type::font (type::kLabel + 2.0f, true));
    const auto name = strip->getName();
    g.drawText (name.isNotEmpty() ? name : strip->getLabel(), header, juce::Justification::topLeft);
}

void Inspector::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap, 0);
    auto header = area.removeFromTop (kHeader).reduced (sizes::kGap, (kHeader - sizes::kTouch) / 2);
    closeKey.setBounds (header.removeFromRight (96));
    auto tabs = area.removeFromTop (sizes::kTabBar);
    chainTab.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2).withTrimmedRight (sizes::kGapSmall / 2));
    sendsTab.setBounds (tabs.withTrimmedLeft (sizes::kGapSmall / 2));
    area.removeFromTop (sizes::kGap);
    sends.setBounds (area);
    // The chain list above the selected block's editor.
    chain.setBounds (area.removeFromTop (juce::jmax (220, area.getHeight() * 2 / 5)));
    area.removeFromTop (sizes::kGap);
    editor.setBounds (area);
}

} // namespace livedaw::ui::universal
