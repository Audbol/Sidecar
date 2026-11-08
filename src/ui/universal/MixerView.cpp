#include "ui/universal/MixerView.h"

#include "dsp/DspUtils.h"
#include "ui/Knobs.h"

namespace livedaw::ui::universal
{

//==============================================================================
StripOverview::StripOverview (UiContext& c, Strip& s, const Theme& t) : ctx (c), strip (s), theme (t)
{
    setInterceptsMouseClicks (false, false);
    for (int i = 0; i < 24; ++i)
        eqFreqs.push_back (20.0 * std::pow (1000.0, (double) i / 23.0));
}

void StripOverview::refresh()
{
    const auto level = [this] (MeterPoint point)
    {
        const float peak = juce::jmax (ctx.meters.get (&strip, point, 0), ctx.meters.get (&strip, point, 1));
        return fader::dbToPos (dsp::gainToDb (peak));
    };
    const float in = level (MeterPoint::Trim), out = level (MeterPoint::Output);
    const auto& p = strip.params;
    const float g = std::abs (strip.meters.gateReductionDb.load()), d = std::abs (strip.meters.dynReductionDb.load());
    const bool go = p.gate().on.load(), dO = p.dynamics().on.load(), eo = p.eq().on.load();
    bool changed = std::abs (in - input) > 0.005f || std::abs (out - output) > 0.005f || std::abs (g - gateGr) > 0.2f || std::abs (d - dynGr) > 0.2f
                   || go != gateOn || dO != dynOn || eo != eqOn;
    input = in;
    output = out;
    gateGr = g;
    dynGr = d;
    gateOn = go;
    dynOn = dO;
    eqOn = eo;

    // The EQ's curve, only when the EQ changed (its model, its values, ON, MIX).
    std::vector<float> state { (float) p.eq().model.load(), eo ? 1.0f : 0.0f, p.eq().mix.load() };
    for (const auto& v : p.eq().values)
        state.push_back (v.load());
    if (state != eqState)
    {
        eqState = std::move (state);
        eqCurve.assign (eqFreqs.size(), 0.0);
        const double sr = ctx.engine.getSampleRate() > 0.0 ? ctx.engine.getSampleRate() : 48000.0;
        ld::ld_strip_response (strip.getHandle(), 1, eqFreqs.data(), eqCurve.data(), (int) eqFreqs.size(), sr);
        changed = true;
    }
    if (changed)
        repaint();
}

void StripOverview::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto bar = [&] (const char* label, bool lit, float fill, juce::Colour colour, bool fromRight)
    {
        auto row = area.removeFromTop (18.0f);
        area.removeFromTop (4.0f);
        g.setColour (lit ? theme.text : theme.textDim);
        g.setFont (type::font (type::kCaption - 1.0f, true));
        g.drawText (label, row.removeFromLeft (30.0f), juce::Justification::centredLeft);
        const auto well = row.withSizeKeepingCentre (row.getWidth(), 8.0f);
        g.setColour (theme.well);
        g.fillRoundedRectangle (well, 2.0f);
        const float w = well.getWidth() * juce::jlimit (0.0f, 1.0f, fill);
        g.setColour (colour);
        g.fillRoundedRectangle (fromRight ? well.withLeft (well.getRight() - w) : well.withWidth (w), 2.0f);
    };
    // Levels on the fader's scale; gain reduction over 24 dB, from the right.
    bar ("IN", true, input, theme.meterLow, false);
    bar ("GATE", gateOn, gateGr / 24.0f, theme.solo, true);
    bar ("DYN", dynOn, dynGr / 24.0f, theme.solo, true);

    // The EQ, +-15 dB.
    auto eq = area.withTrimmedBottom (22.0f);
    if (eq.getHeight() > 16.0f)
    {
        g.setColour (theme.well);
        g.fillRoundedRectangle (eq, 4.0f);
        g.setColour (theme.outline);
        g.fillRect (eq.getX(), eq.getCentreY(), eq.getWidth(), 1.0f);
        g.setColour (eqOn ? theme.text : theme.textDim);
        g.setFont (type::font (type::kCaption - 1.0f, true));
        g.drawText ("EQ", eq.reduced (5.0f, 3.0f), juce::Justification::topLeft);
        if (! eqCurve.empty())
        {
            juce::Path path;
            for (size_t i = 0; i < eqCurve.size(); ++i)
            {
                const float x = eq.getX() + eq.getWidth() * (float) i / (float) (eqCurve.size() - 1);
                const float y = eq.getCentreY() - eq.getHeight() * 0.45f * (float) juce::jlimit (-1.0, 1.0, eqCurve[i] / 15.0);
                if (i == 0)
                    path.startNewSubPath (x, y);
                else
                    path.lineTo (x, y);
            }
            g.setColour (eqOn ? theme.accent : theme.textDim);
            g.strokePath (path, juce::PathStrokeType (1.5f));
        }
    }
    area = area.removeFromBottom (18.0f);
    bar ("OUT", true, output, theme.meterLow, false);
}

//==============================================================================
StripColumn::StripColumn (UiContext& c, Strip& s, const Theme& t)
    : muteKey ("MUTE", t.mute, t), soloKey ("SOLO", t.solo, t), pan (t), overview (c, s, t), fader (t), meter (t), ctx (c), strip (s), theme (t)
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
    for (auto* child : std::initializer_list<juce::Component*> { &muteKey, &soloKey, &pan, &overview, &fader, &meter })
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
    overview.refresh();
    if (faderShown)
    {
        fader.refresh();
        meter.refresh();
    }

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

void StripColumn::setFaderShown (bool shown)
{
    if (shown == faderShown)
        return;
    faderShown = shown;
    fader.setVisible (shown);
    meter.setVisible (shown);
    resized();
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
    g.setColour (consoleColour (colour));
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
    if (! faderShown)
    {
        overview.setBounds (area); // the overview takes the column
        return;
    }
    // The fader no taller than it needs: the rest is the overview's.
    constexpr int kOverview = 176, kMaxFader = 340;
    const int faderHeight = juce::jlimit (0, kMaxFader, area.getHeight() - kOverview - sizes::kGap);
    auto faderArea = area.removeFromBottom (faderHeight);
    area.removeFromBottom (sizes::kGap);
    overview.setBounds (area);
    meter.setBounds (faderArea.removeFromRight (12)); // as tall as the fader: they line up
    faderArea.removeFromRight (sizes::kGapSmall);
    fader.setBounds (faderArea);
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
            case MixerView::kDcas:         return "DCAS";
            default:                       break;
        }
        return "ALL";
    }
} // namespace

MixerView::MixerView (UiContext& c, const Theme& t)
    : ctx (c), theme (t), fadersKey ("FADERS", t.accent, t), channelKey ("CHANNEL", t.accent, t), channel (c, t)
{
    // CHANNEL shows or hides the selected strip's panel across the top; an app setting.
    channelShown = ctx.settings.getBoolValue (uiprefs::universalChannel, true);
    channelKey.isOn = [this] { return channelShown; };
    channelKey.onClick = [this]
    {
        setChannelShown (! channelShown);
        ctx.settings.setValue (uiprefs::universalChannel, channelShown);
    };
    addAndMakeVisible (channelKey);
    addChildComponent (channel);
    channel.setVisible (channelShown);
    // FADERS shows or hides the faders at any moment (the overviews then take the columns); an app setting.
    fadersShown = ctx.settings.getBoolValue (uiprefs::universalFaders, true);
    fadersKey.isOn = [this] { return fadersShown; };
    fadersKey.onClick = [this]
    {
        setFadersShown (! fadersShown);
        ctx.settings.setValue (uiprefs::universalFaders, fadersShown);
    };
    addAndMakeVisible (fadersKey);
    for (int kind = -1; kind <= kDcas; ++kind)
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

void MixerView::setFadersShown (bool shown)
{
    fadersShown = shown;
    for (auto& c : columns)
        c->setFaderShown (shown);
    fadersKey.refresh();
}

void MixerView::setFilter (int kind)
{
    const int f = juce::jlimit (-1, (int) kDcas, kind);
    if (f == filter)
        return;
    filter = f;
    rebuild();
}

void MixerView::rebuild()
{
    structureVersion = ctx.engine.getStructureVersion();
    columns.clear();
    dcaColumns.clear();
    muteGroups.reset();
    for (int k = 0; k < kNumStripKinds; ++k)
    {
        if (filter >= 0 && filter != k)
            continue;
        for (auto& s : ctx.engine.getStrips (static_cast<StripKind> (k)))
        {
            columns.push_back (std::make_unique<StripColumn> (ctx, *s, theme));
            columns.back()->setFaderShown (fadersShown);
            content.addAndMakeVisible (*columns.back());
        }
    }
    // The DCAs and the mute groups after the strips (ALL), or alone (DCAS).
    if (filter < 0 || filter == kDcas)
    {
        for (int d = 0; d < ctx.engine.getNumDcas(); ++d)
        {
            dcaColumns.push_back (std::make_unique<DcaColumn> (ctx, d, theme));
            content.addAndMakeVisible (*dcaColumns.back());
        }
        if (ctx.engine.getNumMuteGroups() > 0)
        {
            muteGroups = std::make_unique<MuteGroupsPanel> (ctx, theme);
            content.addAndMakeVisible (*muteGroups);
        }
    }
    for (auto& key : filterKeys)
        key->refresh();
    fadersKey.refresh();
    layoutColumns();
    selectionChanged();
}

void MixerView::selectionChanged()
{
    auto* selected = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    for (auto& c : columns)
        c->setSelected (&c->getStrip() == selected);
    channel.setStrip (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr);
}

void MixerView::setChannelShown (bool shown)
{
    channelShown = shown;
    channel.setVisible (shown);
    channelKey.repaint();
    resized();
}

void MixerView::refresh()
{
    // A structural change made elsewhere (a show load, a channel added): new columns.
    if (ctx.engine.getStructureVersion() != structureVersion)
    {
        rebuild();
        return;
    }
    // The panel's curves ten times a second (the columns' meters take all thirty).
    if (channelShown && ++ticks % 3 == 0)
    {
        channel.setStrip (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr);
        channel.refresh();
    }
    for (auto& c : columns)
        c->refresh();
    for (auto& d : dcaColumns)
        d->refresh();
    if (muteGroups != nullptr)
        muteGroups->refresh();
}

StripColumn* MixerView::getColumn (int index) const noexcept
{
    return juce::isPositiveAndBelow (index, (int) columns.size()) ? columns[(size_t) index].get() : nullptr;
}

DcaColumn* MixerView::getDcaColumn (int index) const noexcept
{
    return juce::isPositiveAndBelow (index, (int) dcaColumns.size()) ? dcaColumns[(size_t) index].get() : nullptr;
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    // The strips' blocks (the overview's GATE, DYN, EQ) and the faders' law are the WING's.
    // Between the last filter key and CHANNEL, where there's room for it.
    auto bar = getLocalBounds().removeFromTop (sizes::kFilterBar).toFloat();
    bar.setLeft ((float) (filterKeys.empty() ? 0 : filterKeys.back()->getRight()) + sizes::kGap);
    bar.setRight ((float) channelKey.getX() - sizes::kGap);
    g.setFont (type::font (type::kCaption, true));
    const auto caption = juce::String ("BLOCKS AND FADER LAW");
    if (bar.getWidth() < juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), caption) + deviceTagWidth (devices::wing) + 12.0f)
        return;
    bar.removeFromRight (drawDeviceTag (g, bar, devices::wing, theme));
    g.setColour (theme.textDim);
    g.drawText (caption, bar, juce::Justification::centredRight);
}

void MixerView::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop (sizes::kFilterBar).reduced (sizes::kGap, sizes::kGapSmall);
    fadersKey.setBounds (bar.removeFromRight (120));
    bar.removeFromRight (sizes::kGapSmall);
    channelKey.setBounds (bar.removeFromRight (120));
    // The filter keys share what's left (112 px each at most), so none goes under CHANNEL.
    bar.removeFromRight (sizes::kGap);
    const int n = juce::jmax (1, (int) filterKeys.size());
    const int keyW = juce::jlimit (60, 112, (bar.getWidth() - (n - 1) * sizes::kGapSmall) / n);
    for (auto& key : filterKeys)
    {
        key->setBounds (bar.removeFromLeft (keyW));
        bar.removeFromLeft (sizes::kGapSmall);
    }
    if (channelShown)
        channel.setBounds (area.removeFromTop (juce::jlimit (200, 300, area.getHeight() * 32 / 100)));
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
    if (! dcaColumns.empty() && ! columns.empty())
        x += sizes::kKindGap;
    for (auto& d : dcaColumns)
    {
        d->setBounds (x, 0, sizes::kColumnWidth, height);
        x += sizes::kColumnWidth;
    }
    if (muteGroups != nullptr)
    {
        if (x > sizes::kGapSmall)
            x += sizes::kKindGap;
        // Two keys across (four once there are more than 16 groups).
        const int width = (ctx.engine.getNumMuteGroups() > 16 ? 4 : 2) * muteGroups->groups.minKey + 2 * sizes::kGap + 8;
        muteGroups->setBounds (x, 0, width, height);
        x += width;
    }
    content.setSize (x + sizes::kGapSmall, height);
}

} // namespace livedaw::ui::universal
