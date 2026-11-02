#include "ui/universal/MixerView.h"

#include "ui/Knobs.h"

namespace livedaw::ui::universal
{

//==============================================================================
StripColumn::StripColumn (UiContext& c, Strip& s, const Theme& t)
    : muteKey ("MUTE", t.mute, t), soloKey ("SOLO", t.solo, t), pan (t), fader (t), meter (t), ctx (c), strip (s), theme (t)
{
    muteKey.onClick = [this] { ctx.engine.pressMute (strip); };
    // Lit for its own mute; muted by a mute group, a DCA or SIP, the outline (the WING's key flashes).
    muteKey.isOn = [this] { return strip.params.mute.load(); };
    muteKey.isHalfOn = [this] { return strip.getMuteState() == ld::kMuteGroup; };
    soloKey.onClick = [this] { strip.params.solo = ! strip.params.solo.load(); };
    soloKey.isOn = [this] { return strip.params.solo.load(); };
    pan.bind (&strip.params.pan);
    fader.bind (&strip.params.faderDb);
    meter.level = [this] (int ch) { return ctx.meters.get (&strip, MeterPoint::Output, ch); };
    for (auto* child : std::initializer_list<juce::Component*> { &muteKey, &soloKey, &pan, &fader, &meter })
        addAndMakeVisible (child);
    setTitle (strip.getLabel());
    refresh();
}

void StripColumn::refresh()
{
    meter.setChannels (strip.getNumChannels());
    muteKey.refresh();
    soloKey.refresh();
    pan.refresh();
    fader.refresh();
    meter.refresh();

    const auto newName = strip.getName();
    const int newColour = strip.getColourIndex();
    const float db = strip.params.faderDb.load();
    if (newName != name || newColour != colour || std::abs (db - shownDb) > 0.005f)
    {
        name = newName;
        colour = newColour;
        shownDb = db;
        repaint();
    }
}

void StripColumn::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

juce::Rectangle<int> StripColumn::header() const noexcept
{
    return getLocalBounds().reduced (sizes::kGapSmall).removeFromTop (58);
}

void StripColumn::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (selected ? theme.raised : theme.panel);
    g.fillRoundedRectangle (area, sizes::kRadius);
    if (selected)
    {
        g.setColour (theme.accent);
        g.drawRoundedRectangle (area.reduced (0.5f), sizes::kRadius, 2.0f);
    }

    // The strip's colour, its label (by kind) and its name.
    auto h = header().toFloat();
    g.setColour (wingColour (colour));
    g.fillRoundedRectangle (h.removeFromTop (6.0f), 3.0f);
    h.removeFromTop (4.0f);
    g.setColour (theme.kindColour (strip.getKind()));
    g.setFont (type::font (type::kCaption, true));
    g.drawText (strip.getLabel(), h.removeFromTop (16.0f), juce::Justification::centred);
    g.setColour (theme.text);
    g.setFont (type::font (type::kLabel, true));
    g.drawFittedText (name, h.toNearestInt(), juce::Justification::centred, 2, 0.8f);

    // The fader's value under it.
    auto bottom = getLocalBounds().reduced (sizes::kGapSmall).removeFromBottom (24);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kValue));
    g.drawText (fader::text (shownDb) + " dB", bottom, juce::Justification::centred);
}

void StripColumn::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGapSmall);
    area.removeFromTop (header().getHeight() + sizes::kGapSmall);
    area.removeFromBottom (24);
    pan.setBounds (area.removeFromTop (34));
    area.removeFromTop (sizes::kGapSmall);
    muteKey.setBounds (area.removeFromTop (sizes::kTouch));
    area.removeFromTop (sizes::kGapSmall);
    soloKey.setBounds (area.removeFromTop (sizes::kTouch));
    area.removeFromTop (sizes::kGap);
    meter.setBounds (area.removeFromRight (12)); // as tall as the fader: they line up
    area.removeFromRight (sizes::kGapSmall);
    fader.setBounds (area);
}

void StripColumn::mouseUp (const juce::MouseEvent& e)
{
    // A tap on the header selects the strip.
    if (header().contains (e.getPosition()) && ! e.mouseWasDraggedSinceMouseDown() && ctx.selectStrip)
        ctx.selectStrip (&strip);
}

//==============================================================================
namespace
{
    const char* filterName (int kind)
    {
        switch (kind)
        {
            case (int) StripKind::Channel: return "CHANNELS";
            case (int) StripKind::Aux:     return "AUX";
            case (int) StripKind::Bus:     return "BUSES";
            case (int) StripKind::Main:    return "MAINS";
            case (int) StripKind::Matrix:  return "MATRICES";
            default:                       break;
        }
        return "ALL";
    }
} // namespace

MixerView::MixerView (UiContext& c, const Theme& t) : ctx (c), theme (t)
{
    for (int kind = -1; kind < kNumStripKinds; ++kind)
    {
        auto key = std::make_unique<ToggleKey> (filterName (kind), theme.accent, theme);
        key->onClick = [this, kind] { setFilter (kind); };
        key->isOn = [this, kind] { return filter == kind; };
        addAndMakeVisible (*key);
        filterKeys.push_back (std::move (key));
    }
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    // Touch drags scroll the view (a mouse uses the scroll bar); a control's own drag doesn't.
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    viewport.getHorizontalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme.outline);
    addAndMakeVisible (viewport);
    rebuild();
    startTimerHz (30);
}

MixerView::~MixerView()
{
    stopTimer();
}

void MixerView::setFilter (int kind)
{
    const int f = juce::jlimit (-1, kNumStripKinds - 1, kind);
    if (f == filter)
        return;
    filter = f;
    rebuild();
}

void MixerView::rebuild()
{
    structureVersion = ctx.engine.getStructureVersion();
    columns.clear();
    for (int k = 0; k < kNumStripKinds; ++k)
    {
        if (filter >= 0 && filter != k)
            continue;
        for (auto& s : ctx.engine.getStrips (static_cast<StripKind> (k)))
        {
            columns.push_back (std::make_unique<StripColumn> (ctx, *s, theme));
            content.addAndMakeVisible (*columns.back());
        }
    }
    for (auto& key : filterKeys)
        key->refresh();
    layoutColumns();
    selectionChanged();
}

void MixerView::selectionChanged()
{
    auto* selected = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    for (auto& c : columns)
        c->setSelected (&c->getStrip() == selected);
}

void MixerView::refresh()
{
    // A structural change made elsewhere (a show load, a channel added): new columns.
    if (ctx.engine.getStructureVersion() != structureVersion)
    {
        rebuild();
        return;
    }
    for (auto& c : columns)
        c->refresh();
}

StripColumn* MixerView::getColumn (int index) const noexcept
{
    return juce::isPositiveAndBelow (index, (int) columns.size()) ? columns[(size_t) index].get() : nullptr;
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    // The faders' law is the WING's (ui::fader).
    auto bar = getLocalBounds().removeFromTop (sizes::kFilterBar).reduced (sizes::kGapLarge, 0).toFloat();
    bar.removeFromRight (drawDeviceTag (g, bar, devices::wing, theme));
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption, true));
    g.drawText ("FADER LAW", bar, juce::Justification::centredRight);
}

void MixerView::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop (sizes::kFilterBar).reduced (sizes::kGap, sizes::kGapSmall);
    for (auto& key : filterKeys)
    {
        key->setBounds (bar.removeFromLeft (112));
        bar.removeFromLeft (sizes::kGapSmall);
    }
    viewport.setBounds (area);
    layoutColumns();
}

void MixerView::layoutColumns()
{
    const int height = juce::jmax (0, viewport.getHeight() - viewport.getScrollBarThickness());
    int x = sizes::kGapSmall;
    int lastKind = -1;
    for (auto& c : columns)
    {
        const int kind = (int) c->getStrip().getKind();
        if (lastKind >= 0 && kind != lastKind)
            x += sizes::kKindGap;
        lastKind = kind;
        c->setBounds (x, 0, sizes::kColumnWidth, height);
        x += sizes::kColumnWidth;
    }
    content.setSize (x + sizes::kGapSmall, height);
}

} // namespace livedaw::ui::universal
