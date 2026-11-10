#include "ui/universal/MixerView.h"

#include "dsp/DspUtils.h"
#include "ui/ChainSummary.h"
#include "ui/Knobs.h"

namespace livedaw::ui::universal
{

//==============================================================================
int StripLayout::defaultBlockHeight (const ChainStep& s) noexcept
{
    return s.kind == ld::kStepEq ? 84 : s.kind == ld::kStepFilter ? 58 : s.kind == ld::kStepGate || s.kind == ld::kStepDynamics ? 48 : 30;
}

int StripLayout::blockHeight (const ChainStep& s) const
{
    const auto it = blocks.find (blockKey (s));
    return it != blocks.end() ? it->second : defaultBlockHeight (s);
}

void StripLayout::setSection (int section, int height) noexcept
{
    constexpr int floor = 28;
    heights[(size_t) section] = height < (floor + kCollapsed) / 2 ? kCollapsed : juce::jlimit (floor, 240, height);
}

void StripLayout::setBlock (int key, int height)
{
    constexpr int floor = 34;
    blocks[key] = height < (floor + kCollapsed) / 2 ? kCollapsed : juce::jlimit (floor, 360, height);
}

void StripLayout::reset()
{
    *this = StripLayout();
}

juce::String StripLayout::toString() const
{
    juce::String text = juce::String (heights[Meters]) + " " + juce::String (widthMode);
    for (const auto& [key, height] : blocks)
        text << " " << key << "=" << height;
    return text;
}

void StripLayout::fromString (const juce::String& text)
{
    const auto parts = juce::StringArray::fromTokens (text, " ", "");
    if (parts.size() < 2)
        return;
    heights[Meters] = juce::jlimit (kCollapsed, 240, parts[0].getIntValue());
    widthMode = juce::jlimit (0, 2, parts[1].getIntValue());
    blocks.clear();
    for (int i = 2; i < parts.size(); ++i)
        if (parts[i].containsChar ('='))
            blocks[parts[i].upToFirstOccurrenceOf ("=", false, false).getIntValue()] = juce::jlimit (kCollapsed, 360, parts[i].fromFirstOccurrenceOf ("=", false, false).getIntValue());
}

//==============================================================================
StripBody::StripBody (UiContext& c, Strip& s, const Theme& t) : ctx (c), strip (s), theme (t)
{
    setViewportIgnoreDragFlag (true);
    setTitle ("Blocks");
    for (int i = 0; i < 32; ++i)
        freqs.push_back (20.0 * std::pow (1000.0, (double) i / 31.0));
    refresh();
}

void StripBody::setLayout (const StripLayout* newLayout)
{
    layout = newLayout;
    setScroll (scroll);
    repaint();
}

int StripBody::heightOf (const Block& b) const
{
    return layout != nullptr ? layout->blockHeight (b.step) : StripLayout::defaultBlockHeight (b.step);
}

int StripBody::getNaturalHeight() const
{
    int total = 0;
    for (const auto& b : blocks)
        total += heightOf (b);
    return total;
}

bool StripBody::blockOn (const Block& b) const
{
    const auto& t = b.target;
    if (t.eq != nullptr)
        return t.eq->on.load();
    if (t.dynamics != nullptr)
        return t.dynamics->on.load();
    if (t.insert != nullptr)
        return t.insert->on.load();
    if (t.filter != nullptr)
        return t.filter->lowCut.load() || t.filter->highCut.load();
    return t.kind != BlockTarget::Kind::None;
}

void StripBody::rebuild (const std::vector<ChainStep>& chain)
{
    blocks.clear();
    shownChain = chain;
    for (const auto& step : chain)
    {
        auto target = BlockTarget::of (ctx, strip, step);
        if (target.kind == BlockTarget::Kind::None)
            continue; // a marker of the path (the fader, a tap), not a block
        Block b;
        b.step = step;
        b.target = std::move (target);
        b.name = stepName (strip, step).toUpperCase();
        b.key = StripLayout::blockKey (step);
        blocks.push_back (std::move (b));
    }
    tick = 0;
    setScroll (scroll);
    if (auto* parent = getParentComponent())
        parent->resized();
}

void StripBody::refresh()
{
    auto chain = strip.getChain();
    const bool same = chain.size() == shownChain.size() && std::equal (chain.begin(), chain.end(), shownChain.begin(), [] (const ChainStep& a, const ChainStep& b)
    {
        return a.kind == b.kind && a.arg == b.arg && a.uid == b.uid && a.slot == b.slot;
    });
    if (! same)
        rebuild (chain);

    bool changed = ! same;
    const bool curves = (tick++ % 6) == 0;
    for (auto& b : blocks)
    {
        const bool on = blockOn (b);
        const float reduction = b.target.reductionDb ? std::abs (b.target.reductionDb()) : 0.0f;
        changed = changed || on != b.on || std::abs (reduction - b.reduction) > 0.2f;
        b.on = on;
        b.reduction = reduction;
        juce::String model;
        if (b.target.eq != nullptr)
            model = models::model (b.target.table, b.target.eq->model.load()).name;
        else if (b.target.dynamics != nullptr)
            model = models::model (b.target.table, b.target.dynamics->model.load()).name;
        model = model.toUpperCase().replace ("WING ", "");
        changed = changed || model != b.model;
        b.model = model;
        if (curves && b.target.response && b.target.dynamics == nullptr)
        {
            std::vector<double> db (freqs.size(), 0.0);
            if (b.target.response (freqs.data(), db.data(), (int) freqs.size()) && db != b.curve)
            {
                b.curve = std::move (db);
                changed = true;
            }
        }
    }
    if (changed)
        repaint();
}

void StripBody::paint (juce::Graphics& g)
{
    const float w = (float) getWidth();
    int y = -scroll;
    for (const auto& b : blocks)
    {
        const int h = heightOf (b);
        if (y + h > 0 && y < getHeight())
        {
            drawPlate (g, juce::Rectangle<float> (0.0f, (float) y, w, (float) h), theme.hw.plateTop, theme.hw.plateBottom, theme);
            auto r = juce::Rectangle<float> (0.0f, (float) y, w, (float) h).reduced (4.0f, 2.0f);
            // The line of the block: its LED, its name, the console it comes from.
            auto line = r.removeFromTop (14.0f);
            const auto led = juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ line.getX() + 3.0f, line.getCentreY() });
            g.setColour (b.on ? theme.hw.ledOk : theme.well);
            g.fillEllipse (led);
            g.setColour (theme.hw.capEdge);
            g.drawEllipse (led, 0.6f);
            line.removeFromLeft (8.0f);
            drawEngraved (g, b.name, line, juce::Justification::centredLeft, theme);
            if (h > StripLayout::kCollapsed)
            {
                // Under its name: its model, and the console it comes from at the right.
                if (r.getHeight() >= 12.0f)
                {
                    auto second = r.removeFromTop (12.0f);
                    second.removeFromRight (drawDeviceTag (g, second, b.target.device, theme));
                    if (b.model.isNotEmpty())
                        drawEngraved (g, b.model, second, juce::Justification::centredLeft, theme, false);
                }
                if (! b.curve.empty() && r.getHeight() >= 14.0f)
                {
                    // A filled response on a grid, for display.
                    g.setColour (theme.well);
                    g.fillRect (r);
                    g.setColour (theme.outline);
                    for (float f : { 0.25f, 0.5f, 0.75f })
                        g.fillRect (r.getX() + r.getWidth() * f, r.getY(), 1.0f, r.getHeight());
                    g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);
                    juce::Path line2, fill;
                    fill.startNewSubPath (r.getX(), r.getCentreY());
                    for (size_t i = 0; i < b.curve.size(); ++i)
                    {
                        const float px = r.getX() + r.getWidth() * (float) i / (float) (b.curve.size() - 1);
                        const float py = r.getCentreY() - r.getHeight() * 0.45f * (float) juce::jlimit (-1.0, 1.0, b.curve[i] / 18.0);
                        fill.lineTo (px, py);
                        if (i == 0)
                            line2.startNewSubPath (px, py);
                        else
                            line2.lineTo (px, py);
                    }
                    fill.lineTo (r.getRight(), r.getCentreY());
                    fill.closeSubPath();
                    const auto colour = b.on ? theme.hw.pointer : theme.hw.tick;
                    g.setColour (colour.withAlpha (0.3f));
                    g.fillPath (fill);
                    g.setColour (colour);
                    g.strokePath (line2, juce::PathStrokeType (1.5f));
                }
                else if (b.target.reductionDb && r.getHeight() >= 6.0f)
                {
                    // Gain reduction over 24 dB, from the right, as LEDs in a dark window, with its value.
                    auto display = r.reduced (0.0f, 1.0f);
                    g.setColour (theme.well);
                    g.fillRect (display);
                    g.setFont (type::font (type::kCaption));
                    drawGlowText (g, b.reduction > 0.05f ? "-" + juce::String (b.reduction, 1) : juce::String ("0.0"), display.removeFromRight (34.0f), juce::Justification::centred, theme.hw.ledWarn, theme);
                    const auto bar = display.reduced (2.0f, 3.0f);
                    const float cut = bar.getRight() - bar.getWidth() * juce::jlimit (0.0f, 1.0f, b.reduction / 24.0f);
                    for (float x = bar.getRight() - 3.0f; x >= bar.getX(); x -= 3.0f)
                    {
                        g.setColour (x >= cut ? theme.hw.ledWarn : theme.hw.ledWarn.withAlpha (0.1f));
                        g.fillRect (x, bar.getY(), 2.0f, bar.getHeight());
                    }
                }
            }
            // The grip of its lower edge.
            const float bottom = (float) (y + h) - 1.0f;
            g.setColour (theme.edgeDark());
            g.fillRect (0.0f, bottom, w, 1.0f);
            g.setColour (theme.hw.tick);
            for (int i = -1; i <= 1; ++i)
                g.fillRect (w * 0.5f + (float) i * 5.0f - 1.5f, bottom - 1.0f, 3.0f, 2.0f);
        }
        y += h;
    }
    const int total = getNaturalHeight();
    if (total > getHeight() && total > 0)
    {
        g.setColour (theme.hw.tick);
        g.fillRect (w - 2.0f, (float) scroll * (float) getHeight() / (float) total, 2.0f, (float) getHeight() * (float) getHeight() / (float) total);
    }
}

int StripBody::blockAt (int y) const
{
    int top = -scroll;
    for (int i = 0; i < (int) blocks.size(); ++i)
    {
        top += heightOf (blocks[(size_t) i]);
        if (y < top)
            return i;
    }
    return -1;
}

int StripBody::dividerAt (int y) const
{
    const int reach = isTouchInput() ? sizes::kTouch / 2 : 5;
    int top = -scroll, nearest = -1, best = reach + 1;
    for (int i = 0; i < (int) blocks.size(); ++i)
    {
        top += heightOf (blocks[(size_t) i]);
        if (const int d = std::abs (y - top); d < best)
        {
            best = d;
            nearest = i;
        }
    }
    return nearest;
}

void StripBody::setScroll (int newScroll)
{
    const int s = juce::jlimit (0, juce::jmax (0, getNaturalHeight() - getHeight()), newScroll);
    if (s != scroll)
    {
        scroll = s;
        repaint();
    }
}

void StripBody::mouseDown (const juce::MouseEvent& e)
{
    gesture = {};
    dragDivider = dividerAt (e.y);
    dragHeight = dragDivider >= 0 ? heightOf (blocks[(size_t) dragDivider]) : 0;
    dragScroll = scroll;
}

void StripBody::mouseDrag (const juce::MouseEvent& e)
{
    const int dy = e.getDistanceFromDragStartY();
    if (dragDivider >= 0)
    {
        if (onResize && juce::isPositiveAndBelow (dragDivider, (int) blocks.size()))
            onResize (blocks[(size_t) dragDivider].key, dragHeight + dy);
    }
    else if (! e.source.isTouch() || gesture.update (*this, e))
        setScroll (dragScroll - dy); // up and down the strip; sideways, the strips scroll
}

void StripBody::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown())
        return;
    // A tap on a grip that has not moved is still a tap on its block.
    const int i = blockAt (e.y);
    if (! juce::isPositiveAndBelow (i, (int) blocks.size()))
        return;
    if (heightOf (blocks[(size_t) i]) <= StripLayout::kCollapsed)
    {
        if (onResize)
            onResize (blocks[(size_t) i].key, StripLayout::defaultBlockHeight (blocks[(size_t) i].step));
    }
    else if (onOpen)
        onOpen (blocks[(size_t) i].step);
}

void StripBody::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    setScroll (scroll - juce::roundToInt (wheel.deltaY * 120.0f));
}

//==============================================================================
/** The grip between sections: a drag resizes the section above it on every strip, a tap opens
    one that is collapsed. A finger gets a larger grab area than the line it can see. */
class StripColumn::Divider final : public juce::Component
{
public:
    Divider (StripColumn& o, int s) : owner (o), section (s)
    {
        setViewportIgnoreDragFlag (true);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    bool hitTest (int, int y) override { return isTouchInput() || std::abs (y - getHeight() / 2) <= 5; }

    void paint (juce::Graphics& g) override
    {
        const auto y = (float) getHeight() * 0.5f;
        g.setColour (owner.theme.edgeDark());
        g.fillRect (0.0f, y - 0.5f, (float) getWidth(), 1.0f);
        g.setColour (owner.theme.hw.tick);
        for (int i = -1; i <= 1; ++i)
            g.fillRect ((float) getWidth() * 0.5f + (float) i * 5.0f - 1.5f, y - 1.0f, 3.0f, 2.0f);
    }

    void mouseDown (const juce::MouseEvent&) override { start = owner.sections[(size_t) section].getHeight(); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (owner.onResizeSection)
            owner.onResizeSection (section, start + e.getDistanceFromDragStartY());
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasDraggedSinceMouseDown() && owner.sections[(size_t) section].getHeight() <= StripLayout::kCollapsed && owner.onResizeSection)
            owner.onResizeSection (section, StripLayout().heights[(size_t) section]);
    }

private:
    StripColumn& owner;
    int section, start = 0;
};

StripColumn::StripColumn (UiContext& c, Strip& s, const Theme& t)
    : muteKey ("MUTE", t.mute, t), soloKey ("SOLO", t.solo, t), pan (t), fader (t), meter (t), body (c, s, t), sendKey ("SEND", t.accent, t), ctx (c), strip (s), theme (t)
{
    static const StripLayout defaultLayout;
    layout = &defaultLayout;
    muteKey.onClick = [this] { ctx.engine.pressMute (strip); };
    // Lit for its own mute; muted by a mute group, a DCA or SIP, the outline (the WING's key flashes).
    muteKey.isOn = [this] { return strip.params.mute.load(); };
    muteKey.isHalfOn = [this] { return strip.getMuteState() == ld::kMuteGroup; };
    soloKey.onClick = [this] { strip.params.solo = ! strip.params.solo.load(); };
    soloKey.isOn = [this] { return strip.params.solo.load(); };
    pan.bind (&strip.params.pan);
    fader.bind (&strip.params.faderDb);
    meter.level = [this] (int ch) { return ctx.meters.get (&strip, MeterPoint::Output, ch); };
    meter.inset = 8.0f;
    for (auto* child : std::initializer_list<juce::Component*> { &muteKey, &soloKey, &pan, &body, &fader, &meter })
        addAndMakeVisible (child);
    body.onOpen = [this] (const ChainStep& step)
    {
        if (onOpenBlock)
            onOpenBlock (strip, step);
    };
    body.onResize = [this] (int key, int height)
    {
        if (onResizeBlock)
            onResizeBlock (key, height);
    };
    for (int s = 0; s < StripLayout::kNumSections; ++s)
    {
        dividers[(size_t) s] = std::make_unique<Divider> (*this, s);
        addAndMakeVisible (*dividers[(size_t) s]);
    }
    sendKey.isOn = [this] { return sendOn != nullptr && sendOn->load(); };
    sendKey.onClick = [this]
    {
        if (sendOn != nullptr)
            sendOn->store (! sendOn->load());
    };
    addChildComponent (sendKey); // over the strip's body
    setTitle (strip.getLabel());
    refresh();
}

StripColumn::~StripColumn() = default;

void StripColumn::setLayout (const StripLayout* newLayout)
{
    layout = newLayout;
    body.setLayout (newLayout);
    resized();
    repaint();
}

void StripColumn::setSendDestination (Strip* destination)
{
    sendDestination = destination;
    sendLevel = nullptr;
    sendOn = nullptr;
    if (destination != nullptr && destination != &strip)
    {
        const auto kind = strip.getKind();
        const bool toBusesAndMains = kind == StripKind::Channel || kind == StripKind::Aux || kind == StripKind::Bus;
        const auto indexIn = [destination] (const auto& list)
        {
            for (size_t i = 0; i < list.size(); ++i)
                if (list[i].get() == destination)
                    return i;
            return list.size();
        };
        if (destination->getKind() == StripKind::Bus && toBusesAndMains)
        {
            auto& p = strip.sends[(size_t) destination->getId()];
            sendLevel = &p.levelDb;
            sendOn = &p.enabled;
        }
        else if (destination->getKind() == StripKind::Main && toBusesAndMains)
        {
            auto& p = strip.mains[indexIn (ctx.engine.getMains())];
            sendLevel = &p.levelDb;
            sendOn = &p.on;
        }
        else if (destination->getKind() == StripKind::Matrix && kind != StripKind::Matrix)
        {
            auto& p = strip.matrix[indexIn (ctx.engine.getMatrices())];
            sendLevel = &p.levelDb;
            sendOn = &p.on;
        }
    }
    fader.bind (destination == nullptr ? &strip.params.faderDb : sendLevel);
    fader.tint = destination != nullptr ? theme.kindColour (destination->getKind()).withAlpha (0.7f) : juce::Colour();
    sendKey.setButtonText (destination != nullptr ? destination->getLabel() : juce::String ("SEND"));
    sendKey.setVisible (sendOn != nullptr);
    sendKey.toFront (false);
    resized();
    refresh();
    repaint();
}

void StripColumn::refresh()
{
    meter.setChannels (strip.getNumChannels());
    muteKey.refresh();
    soloKey.refresh();
    pan.refresh();
    meter.refresh();
    if (faderShown)
        fader.refresh();
    if (sendKey.isVisible())
        sendKey.refresh();
    body.refresh();

    const auto newName = strip.getName();
    const int newColour = strip.getColourIndex();
    const float db = sendDestination == nullptr ? strip.params.faderDb.load() : (sendLevel != nullptr ? sendLevel->load() : shownDb);
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
    resized();
}

void StripColumn::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        fader.emphasised = selected;
        fader.repaint();
        repaint();
    }
}

juce::Rectangle<int> StripColumn::header() const noexcept
{
    return nameArea;
}

void StripColumn::paint (juce::Graphics& g)
{
    const auto w = (float) getWidth();
    const auto& hw = theme.hw;
    // The meter bridge, dark and running across the strips.
    g.setColour (theme.well);
    g.fillRect (sections[StripLayout::Meters]);
    // The plate carrying the strip's blocks, and the tray with its fader under the name tab.
    drawPlate (g, juce::Rectangle<float> (0.0f, (float) sections[StripLayout::Meters].getBottom(), w, (float) (nameArea.getY() - sections[StripLayout::Meters].getBottom())),
               hw.plateTop, hw.plateBottom, theme);
    drawPlate (g, trayArea.toFloat().withX (0.0f).withWidth (w), hw.trayTop, hw.trayBottom, theme);
    for (int s = 0; s < StripLayout::kNumSections; ++s)
        if (sections[(size_t) s].getHeight() <= StripLayout::kCollapsed)
            drawEngraved (g, "METERS", sections[(size_t) s].toFloat(), juce::Justification::centred, theme);

    drawPlate (g, panArea.toFloat().withX (0.0f).withWidth (w), hw.plateTop, hw.plateBottom, theme, true);

    // The name tab between them: the strip's colour, its label (by kind) and its name.
    auto h = nameArea.toFloat();
    g.setColour (hw.nameTab);
    g.fillRect (h);
    g.setColour (consoleColour (colour));
    g.fillRect (h.removeFromLeft (4.0f));
    g.setColour (theme.isLight ? hw.nameText : theme.kindColour (strip.getKind()));
    g.setFont (type::font (type::kMicro, true));
    auto labelArea = h.reduced (4.0f, 1.0f);
    g.drawText (strip.getLabel(), labelArea.removeFromTop (12.0f), juce::Justification::centredLeft);
    g.setColour (hw.nameText);
    g.setFont (type::font (type::kCaption, true));
    g.drawFittedText (name, labelArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    if (selected)
    {
        g.setColour (theme.accent);
        g.drawRect (getLocalBounds().toFloat(), 1.5f);
    }

    // The fader's value, lit from behind.
    g.setFont (type::font (type::kValue));
    const auto colourNow = sendDestination != nullptr && sendLevel != nullptr ? theme.kindColour (sendDestination->getKind()) : hw.ledOk;
    drawGlowText (g, sendDestination != nullptr && sendLevel == nullptr ? juce::String::fromUTF8 ("\xe2\x80\x94") : fader::text (shownDb) + " dB", readoutArea.toFloat(), juce::Justification::centred, colourNow, theme);
}

void StripColumn::resized()
{
    constexpr int kName = 30, kKeys = sizes::kTouch * 2 + sizes::kGap, kReadout = 24, kPan = 44, kSide = 4;
    // The fader comes first: nearly half the strip, 320 px where there's room for it.
    const int kFaderMin = juce::jlimit (100, 320, getHeight() * 44 / 100);
    auto area = getLocalBounds();
    const auto& l = *layout;
    int metersH = l.heights[StripLayout::Meters];
    int space = area.getHeight() - kName - kKeys - kReadout - kPan - 3 * sizes::kGapSmall - (faderShown ? kFaderMin : 0);
    // The blocks are the strip's body: the others give way first.
    constexpr int kBodyLeast = 90;
    if (metersH + kBodyLeast > space)
        metersH = juce::jmax (StripLayout::kCollapsed, space - kBodyLeast);
    const int bodyRoom = juce::jmax (0, space - metersH);
    const int bodyH = faderShown ? juce::jmin (body.getNaturalHeight(), bodyRoom) : bodyRoom;

    sections[StripLayout::Meters] = area.removeFromTop (metersH);
    meter.setBounds (sections[StripLayout::Meters].reduced (kSide, 0));
    meter.setVisible (metersH > StripLayout::kCollapsed);
    body.setBounds (area.removeFromTop (bodyH));
    panArea = area.removeFromTop (kPan);
    pan.setBounds (panArea.reduced (kSide, 0));
    nameArea = area.removeFromTop (kName);
    trayArea = area;
    area.reduce (kSide, sizes::kGapSmall);
    readoutArea = area.removeFromBottom (kReadout);
    muteKey.setBounds (area.removeFromTop (sizes::kTouch));
    area.removeFromTop (sizes::kGap); // mute and solo: a finger must not hit the wrong one
    soloKey.setBounds (area.removeFromTop (sizes::kTouch));
    area.removeFromTop (sizes::kGap);
    fader.grooveFromRight = (float) juce::jmax (22, juce::roundToInt ((float) area.getWidth() * 0.38f));
    fader.setBounds (area);
    for (int s = 0; s < StripLayout::kNumSections; ++s)
    {
        const auto& r = sections[(size_t) s];
        dividers[(size_t) s]->setBounds (0, r.getBottom() - sizes::kTouch / 2, getWidth(), sizes::kTouch);
    }
    sendKey.setBounds (body.getBounds().withHeight (juce::jmin (sizes::kTouch, juce::jmax (0, body.getHeight()))));
}

void StripColumn::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown() || ! ctx.selectStrip)
        return;
    // A tap on the name tab selects the strip.
    if (header().contains (e.getPosition()))
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

    constexpr int kBankBar = sizes::kTouch + 4, kJumpRow = sizes::kTouch;
} // namespace

//==============================================================================
/** The bank keys, drawn as one component: the bank at the left of the view is lit. */
class MixerView::BankBar final : public juce::Component
{
public:
    explicit BankBar (MixerView& o) : owner (o) {}

    juce::Rectangle<int> keyArea (int i) const
    {
        const int n = owner.getNumBanks();
        if (! juce::isPositiveAndBelow (i, n))
            return {};
        const int w = juce::jmin (132, getWidth() / juce::jmax (1, n));
        return { i * w, 0, w, getHeight() };
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.theme;
        const int current = owner.getCurrentBank();
        for (int i = 0; i < owner.getNumBanks(); ++i)
        {
            const auto& bank = *owner.getBank (i);
            const auto r = keyArea (i).toFloat().reduced (2.0f);
            const bool on = i == current;
            g.setColour (on ? t.raised : t.panel);
            g.fillRoundedRectangle (r, sizes::kRadius);
            if (on)
            {
                g.setColour (t.accent);
                g.fillRect (r.getX(), r.getBottom() - 3.0f, r.getWidth(), 3.0f);
            }
            g.setColour (bank.kind < kNumStripKinds ? t.kindColour (static_cast<StripKind> (bank.kind)) : bank.kind == kDcas ? t.accent : t.mute);
            g.setFont (type::font (type::kCaption, true));
            g.drawFittedText (bank.label, r.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown())
            return;
        for (int i = 0; i < owner.getNumBanks(); ++i)
            if (keyArea (i).contains (e.getPosition()))
            {
                owner.showBank (i);
                return;
            }
    }

private:
    MixerView& owner;
};

//==============================================================================
/** The JUMP list: a search line over every strip that matches it, by label and name. */
class MixerView::JumpList final : public juce::Component
{
public:
    explicit JumpList (MixerView& o) : owner (o), rows (*this)
    {
        const auto& t = owner.theme;
        search.setFont (type::font (type::kValue));
        search.setTextToShowWhenEmpty ("Number, label or name", t.textDim);
        search.setColour (juce::TextEditor::backgroundColourId, t.well);
        search.setColour (juce::TextEditor::textColourId, t.text);
        search.setColour (juce::TextEditor::outlineColourId, t.outline);
        search.setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
        search.setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.4f));
        search.setColour (juce::CaretComponent::caretColourId, t.text);
        search.onTextChange = [this] { find (search.getText()); };
        search.onReturnKey = [this] { choose(); };
        search.onEscapeKey = [this] { owner.setJumpListShown (false); };
        search.addKeyListener (&keys);
        addAndMakeVisible (search);
        view.setViewedComponent (&rows, false);
        view.setScrollBarsShown (true, false);
        view.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
        view.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, t.outline);
        addAndMakeVisible (view);
        find ({});
    }

    ~JumpList() override { search.removeKeyListener (&keys); }

    /** Every strip again, the search line empty and ready to type in. */
    void open()
    {
        setQuery ({});
        if (search.isShowing())
            search.grabKeyboardFocus();
    }

    void setQuery (const juce::String& q)
    {
        search.setText (q, juce::dontSendNotification);
        find (q);
    }

    /** The search again (the console's strips changed). */
    void again() { find (query); }

    void move (int delta)
    {
        if (found.empty())
            return;
        highlighted = juce::jlimit (0, (int) found.size() - 1, highlighted + delta);
        // The highlighted row stays in view.
        const int y = highlighted * kJumpRow;
        if (y < view.getViewPositionY())
            view.setViewPosition (0, y);
        else if (y + kJumpRow > view.getViewPositionY() + view.getViewHeight())
            view.setViewPosition (0, y + kJumpRow - view.getViewHeight());
        rows.repaint();
    }

    void choose()
    {
        if (! juce::isPositiveAndBelow (highlighted, (int) found.size()))
            return;
        auto* strip = found[(size_t) highlighted];
        owner.setJumpListShown (false);
        owner.jumpTo (*strip);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.theme;
        const auto area = getLocalBounds().toFloat();
        g.setColour (t.panel);
        g.fillRoundedRectangle (area, sizes::kRadius);
        g.setColour (t.accent);
        g.drawRoundedRectangle (area.reduced (0.75f), sizes::kRadius, 1.5f);
        auto inner = getLocalBounds().reduced (sizes::kGap);
        g.setColour (t.text);
        g.setFont (type::font (type::kLabel, true));
        g.drawText ("JUMP TO A STRIP", inner.removeFromTop (24), juce::Justification::centredLeft);
        g.setColour (t.textDim);
        g.setFont (type::font (type::kCaption));
        g.drawText (found.empty() ? juce::String ("No strip matches") : juce::String ((int) found.size()) + ((int) found.size() == 1 ? " strip" : " strips") + "   Up / Down, Return, Esc",
                    getLocalBounds().reduced (sizes::kGap).removeFromBottom (18), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (sizes::kGap);
        area.removeFromTop (24 + sizes::kGapSmall);
        search.setBounds (area.removeFromTop (36));
        area.removeFromTop (sizes::kGapSmall);
        area.removeFromBottom (18 + sizes::kGapSmall);
        view.setBounds (area);
        rows.setSize (juce::jmax (0, view.getWidth() - view.getScrollBarThickness()), (int) found.size() * kJumpRow);
    }

    std::vector<Strip*> found;
    int highlighted = -1;

private:
    /** The rows, scrolled; a tap goes to one. */
    class Rows final : public juce::Component
    {
    public:
        explicit Rows (JumpList& l) : list (l) {}

        void paint (juce::Graphics& g) override
        {
            const auto& t = list.owner.theme;
            const auto clip = g.getClipBounds();
            auto* selected = list.owner.ctx.getSelectedStrip ? list.owner.ctx.getSelectedStrip() : nullptr;
            for (int i = juce::jmax (0, clip.getY() / kJumpRow); i < juce::jmin ((int) list.found.size(), clip.getBottom() / kJumpRow + 1); ++i)
            {
                const auto& s = *list.found[(size_t) i];
                const auto r = juce::Rectangle<int> (0, i * kJumpRow, getWidth(), kJumpRow).toFloat().reduced (1.0f, 2.0f);
                const bool on = i == list.highlighted;
                g.setColour (on ? t.lifted (t.raised, 0.1f) : t.raised);
                g.fillRoundedRectangle (r, sizes::kRadius);
                if (on)
                {
                    g.setColour (t.accent);
                    g.drawRoundedRectangle (r.reduced (0.5f), sizes::kRadius, 2.0f);
                }
                auto inner = r.reduced (10.0f, 0.0f);
                g.setColour (t.kindColour (s.getKind()));
                g.setFont (type::font (type::kLabel, true));
                g.drawText (s.getLabel(), inner.removeFromLeft (84.0f), juce::Justification::centredLeft);
                if (&s == selected)
                {
                    g.setColour (t.textDim);
                    g.setFont (type::font (type::kCaption, true));
                    g.drawText ("SELECTED", inner.removeFromRight (70.0f), juce::Justification::centredRight);
                }
                const auto name = s.getName();
                g.setColour (t.text);
                g.setFont (type::font (type::kLabel));
                g.drawFittedText (name != s.getLabel() ? name : juce::String(), inner.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasDraggedSinceMouseDown() || ! juce::isPositiveAndBelow (e.y / kJumpRow, (int) list.found.size()))
                return;
            list.highlighted = e.y / kJumpRow;
            list.choose();
        }

    private:
        JumpList& list;
    };

    void find (const juce::String& q)
    {
        query = q;
        found = owner.findStrips (q);
        highlighted = found.empty() ? -1 : 0;
        rows.setSize (juce::jmax (0, view.getWidth() - view.getScrollBarThickness()), (int) found.size() * kJumpRow);
        view.setViewPosition (0, 0);
        rows.repaint();
        repaint();
    }

    /** Before the search line takes them: Up and Down move through the list. */
    struct Keys final : public juce::KeyListener
    {
        explicit Keys (JumpList& l) : list (l) {}

        bool keyPressed (const juce::KeyPress& key, juce::Component*) override
        {
            if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
            {
                list.move (key == juce::KeyPress::upKey ? -1 : 1);
                return true;
            }
            if (key == juce::KeyPress::pageUpKey || key == juce::KeyPress::pageDownKey)
            {
                list.move ((key == juce::KeyPress::pageUpKey ? -1 : 1) * juce::jmax (1, list.view.getViewHeight() / kJumpRow));
                return true;
            }
            return false;
        }

        JumpList& list;
    };

    MixerView& owner;
    juce::TextEditor search;
    juce::Viewport view;
    Rows rows;
    Keys keys { *this };
    juce::String query;
};

//==============================================================================
MixerView::MixerView (UiContext& c, const Theme& t)
    : ctx (c), theme (t), fadersKey ("FADERS", t.accent, t), channelKey ("CHANNEL", t.accent, t), sofKey ("SOF", t.accent, t), jumpKey ("JUMP", t.accent, t),
      channel (c, t), bankBar (std::make_unique<BankBar> (*this))
{
    layout.fromString (c.settings.getValue (StripLayout::kSetting));
    // JUMP: the list of every strip, to go to one by its number, label or name.
    jumpKey.isOn = [this] { return isJumpListShown(); };
    jumpKey.onClick = [this] { setJumpListShown (! isJumpListShown()); };
    jumpKey.setTooltip ("Jump to a strip by number, label or name (Ctrl+J or /)");
    addAndMakeVisible (jumpKey);
    addChildComponent (*bankBar);
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
    // SOF: a bus, main or matrix on the faders.
    sofKey.isOn = [this] { return ctx.sendsOnFader; };
    sofKey.onClick = [this] { showSofMenu(); };
    sofKey.setTooltip ("Sends on fader: put a bus, main or matrix on the faders");
    addAndMakeVisible (sofKey);
    channel.onOpenStep = [this] (int kind, int uid)
    {
        if (onOpenStep && channel.getStrip() != nullptr)
            onOpenStep (*channel.getStrip(), kind, uid);
    };
    for (int kind = -1; kind <= kDcas; ++kind)
    {
        auto key = std::make_unique<ToggleKey> (filterName (kind), theme.accent, theme);
        key->tab = true;
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
    if (extraPinned != nullptr)
        extraPinned->setFaderShown (shown);
    fadersKey.refresh();
}

void MixerView::setDcaSpill (int dca)
{
    spillDca = juce::jlimit (0, juce::jmax (0, ctx.engine.getNumDcas() - 1), dca);
    filter = kSpill;
    rebuild();
}

void MixerView::applyStripLayout()
{
    ctx.settings.setValue (StripLayout::kSetting, layout.toString());
    for (auto& c : columns)
        c->setLayout (&layout);
    layoutColumns();
    resized();
}

void MixerView::connect (StripColumn& column)
{
    column.setLayout (&layout);
    // A tap on a block: the strip selected, that block in the inspector.
    column.onOpenBlock = [this] (Strip& s, const ChainStep& step)
    {
        if (ctx.selectStrip)
            ctx.selectStrip (&s);
        if (onOpenStep)
            onOpenStep (s, (int) step.kind, step.uid);
    };
    column.onResizeSection = [this] (int section, int height)
    {
        layout.setSection (section, height);
        applyStripLayout();
    };
    column.onResizeBlock = [this] (int key, int height)
    {
        layout.setBlock (key, height);
        applyStripLayout();
    };
    column.onOpenStep = [this] (Strip& s, int kind, int uid)
    {
        if (onOpenStep)
            onOpenStep (s, kind, uid);
    };
    column.setFaderShown (fadersShown);
}

Strip* MixerView::getSofDestination() const
{
    if (! ctx.sendsOnFader)
        return nullptr;
    if (ctx.sofDestination.has_value())
        return state::findStrip (ctx.engine, *ctx.sofDestination);
    auto* selected = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    return selected != nullptr && selected->isOutput() ? selected : nullptr;
}

void MixerView::setSendsOnFader (Strip* destination)
{
    ctx.sendsOnFader = destination != nullptr;
    ctx.sofDestination = destination != nullptr ? std::optional<state::StripRef> (state::StripRef::of (*destination)) : std::nullopt;
    if (ctx.sofChanged)
        ctx.sofChanged();
    applySendsOnFader (false);
}

void MixerView::sendsOnFaderChanged()
{
    applySendsOnFader (false);
}

void MixerView::applySendsOnFader (bool force)
{
    auto* destination = getSofDestination();
    // New columns start on their own faders: with SOF off and staying off there is nothing to do.
    if (destination == appliedSof && (! force || destination == nullptr))
        return;
    appliedSof = destination;
    for (auto& c : columns)
        if (c.get() != pinnedMain) // the main stays its own fader, as a console's does
            c->setSendDestination (destination);
    sofKey.setButtonText (destination != nullptr ? "SOF " + destination->getLabel() : juce::String ("SOF"));
    sofKey.refresh();
    repaint();
}

void MixerView::showSofMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeelFor (theme));
    std::vector<Strip*> targets;
    const auto current = getSofDestination();
    menu.addItem (1, "Off", ctx.sendsOnFader, ! ctx.sendsOnFader);
    for (auto kind : { StripKind::Bus, StripKind::Main, StripKind::Matrix })
    {
        if (ctx.engine.getStrips (kind).empty())
            continue;
        menu.addSeparator();
        for (auto& s : ctx.engine.getStrips (kind))
        {
            targets.push_back (s.get());
            const auto name = s->getName();
            menu.addItem (100 + (int) targets.size() - 1, s->getLabel() + (name.isNotEmpty() && name != s->getLabel() ? "  " + name : juce::String()), true, s.get() == current);
        }
    }
    juce::Component::SafePointer<MixerView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sofKey).withStandardItemHeight (sizes::kTouch - 8).withMinimumWidth (220),
                        [safe, targets] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == 1)
                                safe->setSendsOnFader (nullptr);
                            else if (juce::isPositiveAndBelow (result - 100, (int) targets.size()))
                                safe->setSendsOnFader (targets[(size_t) (result - 100)]);
                        });
}

void MixerView::revealSelection()
{
    auto* selected = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    if (selected == revealed)
        return;
    revealed = selected;
    for (auto& c : columns)
        if (&c->getStrip() == selected && c.get() != pinnedMain)
        {
            const auto b = c->getBounds();
            const auto view = viewport.getViewPosition();
            if (b.getX() < view.x)
                viewport.setViewPosition (juce::jmax (0, b.getX() - sizes::kGapSmall), view.y);
            else if (b.getRight() > view.x + viewport.getViewWidth())
                viewport.setViewPosition (b.getRight() + sizes::kGapSmall - viewport.getViewWidth(), view.y);
            return;
        }
}

//==============================================================================
void MixerView::buildBanks()
{
    // The scrolled columns in runs of one kind (the pinned main isn't scrolled).
    struct Item
    {
        int kind;
        juce::String label;
        int x;
    };
    std::vector<std::vector<Item>> runs;
    const auto add = [&runs] (int kind, const juce::String& label, int x)
    {
        if (runs.empty() || runs.back().front().kind != kind)
            runs.emplace_back();
        runs.back().push_back ({ kind, label, x });
    };
    for (auto& c : columns)
        if (c.get() != pinnedMain)
            add ((int) c->getStrip().getKind(), c->getStrip().getLabel(), c->getX());
    for (auto& d : dcaColumns)
        add (kDcas, "DCA " + juce::String (d->getDca() + 1), d->getX());

    // Runs of 8, or of as many more as keeps the keys wide enough to read.
    const int room = juce::jmax (1, viewport.getWidth() / 76);
    const auto banksOf = [&runs, this] (int size)
    {
        int n = muteGroups != nullptr ? 1 : 0;
        for (const auto& r : runs)
            n += ((int) r.size() + size - 1) / size;
        return n;
    };
    int size = 8;
    while (size < 128 && banksOf (size) > room)
        size *= 2;

    banks.clear();
    for (const auto& r : runs)
        for (int i = 0; i < (int) r.size(); i += size)
        {
            const int n = juce::jmin (size, (int) r.size() - i);
            const auto& first = r[(size_t) i];
            Bank b;
            b.kind = first.kind;
            b.first = i;
            b.count = n;
            b.x = first.x;
            b.label = n == 1 ? first.label : first.label + "-" + r[(size_t) (i + n - 1)].label.fromLastOccurrenceOf (" ", false, false);
            banks.push_back (b);
        }
    if (muteGroups != nullptr)
    {
        Bank b;
        b.kind = kMuteGroupsBank;
        b.count = 1;
        b.x = muteGroups->getX();
        b.label = "MUTE GRPS";
        banks.push_back (b);
    }
    bankHint = -1;
}

bool MixerView::isBankBarShown() const noexcept
{
    return banks.size() > 1 && content.getWidth() > viewport.getWidth();
}

void MixerView::showBank (int index)
{
    if (const auto* bank = getBank (index))
    {
        viewport.setViewPosition (juce::jmax (0, bank->x - sizes::kGapSmall), 0);
        // The last banks can't reach the left of the view: the one asked for is the current one until the view moves.
        bankHint = index;
        bankHintX = viewport.getViewPositionX();
        bankBar->repaint();
    }
}

int MixerView::getCurrentBank() const noexcept
{
    const int x = viewport.getViewPositionX();
    if (juce::isPositiveAndBelow (bankHint, (int) banks.size()) && x == bankHintX)
        return bankHint;
    int current = banks.empty() ? -1 : 0;
    for (int i = 0; i < (int) banks.size(); ++i)
        if (banks[(size_t) i].x <= x + layout.columnWidth() / 2)
            current = i;
    return current;
}

void MixerView::stepBank (int delta)
{
    if (! banks.empty())
        showBank (juce::jlimit (0, (int) banks.size() - 1, getCurrentBank() + delta));
}

std::vector<Strip*> MixerView::findStrips (const juce::String& query) const
{
    const auto q = query.trim().toLowerCase();
    const juce::String kLetters ("abcdefghijklmnopqrstuvwxyz"), kDigits ("0123456789");
    const auto letters = q.retainCharacters (kLetters), digits = q.retainCharacters (kDigits);
    // "12", "bus 3", "m2": a strip's number, after (the start of) its label's letters.
    const bool labelLike = digits.isNotEmpty() && q.removeCharacters (" .") == letters + digits;
    std::vector<Strip*> ranked[3];
    for (int k = 0; k < kNumStripKinds; ++k)
        for (auto& s : ctx.engine.getStrips (static_cast<StripKind> (k)))
        {
            if (q.isEmpty())
            {
                ranked[0].push_back (s.get());
                continue;
            }
            const auto label = s->getLabel().toLowerCase(), name = s->getName().toLowerCase();
            int rank = -1;
            if (labelLike && label.retainCharacters (kLetters).startsWith (letters))
            {
                const auto number = label.retainCharacters (kDigits);
                rank = number == digits ? 0 : number.startsWith (digits) ? 1 : -1;
            }
            if (rank < 0 && (name.contains (q) || label.contains (q)))
                rank = name.startsWith (q) || label.startsWith (q) ? 1 : 2;
            if (rank >= 0)
                ranked[rank].push_back (s.get());
        }
    std::vector<Strip*> found;
    for (const auto& list : ranked)
        found.insert (found.end(), list.begin(), list.end());
    return found;
}

void MixerView::jumpTo (Strip& strip)
{
    const auto column = [this, &strip]() -> StripColumn*
    {
        for (auto& c : columns)
            if (&c->getStrip() == &strip)
                return c.get();
        return nullptr;
    };
    // A filter (or a spill) that leaves it out: every strip again.
    if (column() == nullptr)
    {
        filter = -1;
        rebuild();
    }
    if (ctx.selectStrip)
        ctx.selectStrip (&strip);
    selectionChanged();
    if (auto* c = column(); c != nullptr && c != pinnedMain)
        viewport.setViewPosition (juce::jmax (0, c->getX() - sizes::kGapSmall), 0);
    revealed = &strip;
}

void MixerView::setJumpListShown (bool shouldShow)
{
    if (shouldShow && jumpList == nullptr)
    {
        jumpList = std::make_unique<JumpList> (*this);
        addChildComponent (*jumpList);
    }
    if (jumpList == nullptr)
        return;
    jumpList->setVisible (shouldShow);
    if (shouldShow)
    {
        resized();
        jumpList->toFront (false);
        jumpList->open();
    }
    jumpKey.refresh();
}

bool MixerView::isJumpListShown() const noexcept
{
    return jumpList != nullptr && jumpList->isVisible();
}

void MixerView::setJumpQuery (const juce::String& query)
{
    if (jumpList != nullptr)
        jumpList->setQuery (query);
}

int MixerView::getNumJumpRows() const noexcept
{
    return jumpList != nullptr ? (int) jumpList->found.size() : 0;
}

Strip* MixerView::getJumpRow (int row) const noexcept
{
    return jumpList != nullptr && juce::isPositiveAndBelow (row, (int) jumpList->found.size()) ? jumpList->found[(size_t) row] : nullptr;
}

void MixerView::moveJumpHighlight (int delta)
{
    if (jumpList != nullptr)
        jumpList->move (delta);
}

void MixerView::chooseJumpRow()
{
    if (jumpList != nullptr)
        jumpList->choose();
}

void MixerView::selectNeighbour (int delta)
{
    if (columns.empty() || ! ctx.selectStrip)
        return;
    auto* selected = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    int index = -1;
    for (int i = 0; i < (int) columns.size(); ++i)
        if (&columns[(size_t) i]->getStrip() == selected)
            index = i;
    index = index < 0 ? 0 : juce::jlimit (0, (int) columns.size() - 1, index + delta);
    ctx.selectStrip (&columns[(size_t) index]->getStrip());
    selectionChanged();
}

bool MixerView::handleKey (const juce::KeyPress& key)
{
    if (isJumpListShown())
        return false; // its search line has the keyboard
    const auto mods = key.getModifiers();
    // With Ctrl held, X11 gives a control code as the text and the lowercase key as the code.
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    const auto k = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getKeyCode());
    if ((mods.isCommandDown() && (c == 'j' || k == 'j')) || (! mods.isAnyModifierKeyDown() && key.getTextCharacter() == '/'))
    {
        setJumpListShown (true);
        return true;
    }
    if (mods.isAnyModifierKeyDown())
        return false;
    if (key == juce::KeyPress::pageDownKey || key == juce::KeyPress::pageUpKey)
        stepBank (key == juce::KeyPress::pageDownKey ? 1 : -1);
    else if (key == juce::KeyPress::homeKey)
        showBank (0);
    else if (key == juce::KeyPress::endKey)
        showBank (getNumBanks() - 1);
    else if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
        selectNeighbour (key == juce::KeyPress::rightKey ? 1 : -1);
    else
        return false;
    return true;
}

//==============================================================================
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
    pinnedMain = nullptr;
    extraPinned.reset();
    columns.clear();
    dcaColumns.clear();
    muteGroups.reset();
    if (filter == kSpill && spillDca >= ctx.engine.getNumDcas())
        filter = -1; // the DCA went (a smaller console)
    for (int k = 0; k < kNumStripKinds; ++k)
    {
        if (filter >= 0 && filter != k && filter != kSpill)
            continue;
        for (auto& s : ctx.engine.getStrips (static_cast<StripKind> (k)))
        {
            if (filter == kSpill && ! s->params.dcaMask.contains (spillDca))
                continue;
            columns.push_back (std::make_unique<StripColumn> (ctx, *s, theme));
            connect (*columns.back());
            content.addAndMakeVisible (*columns.back());
        }
    }
    // The DCAs and the mute groups after the strips (ALL), or alone (DCAS); a spill's DCA after its strips.
    if (filter < 0 || filter == kDcas || filter == kSpill)
    {
        for (int d = 0; d < ctx.engine.getNumDcas(); ++d)
        {
            if (filter == kSpill && d != spillDca)
                continue;
            dcaColumns.push_back (std::make_unique<DcaColumn> (ctx, d, theme));
            dcaColumns.back()->onSpill = [safe = juce::Component::SafePointer<MixerView> (this)] (int dca)
            {
                // After the menu's callback has let go of the column.
                juce::MessageManager::callAsync ([safe, dca]
                {
                    if (safe != nullptr)
                        safe->setDcaSpill (dca);
                });
            };
            content.addAndMakeVisible (*dcaColumns.back());
        }
        if (ctx.engine.getNumMuteGroups() > 0 && filter != kSpill)
        {
            muteGroups = std::make_unique<MuteGroupsPanel> (ctx, theme);
            content.addAndMakeVisible (*muteGroups);
        }
    }
    // The first main in view whatever is scrolled (unless MAINS shows them all anyway).
    if (filter != (int) StripKind::Main && ! ctx.engine.getMains().empty())
    {
        auto* main = ctx.engine.getMains().front().get();
        for (auto& c : columns)
            if (&c->getStrip() == main)
                pinnedMain = c.get();
        if (pinnedMain == nullptr)
        {
            extraPinned = std::make_unique<StripColumn> (ctx, *main, theme);
            connect (*extraPinned);
            pinnedMain = extraPinned.get();
        }
        addAndMakeVisible (*pinnedMain); // out of the scrolled content
    }
    for (auto& key : filterKeys)
        key->refresh();
    fadersKey.refresh();
    resized();
    if (jumpList != nullptr)
        jumpList->again(); // other strips: the list's are these now
    selectionChanged();
    applySendsOnFader (true);
    repaint();
}

void MixerView::selectionChanged()
{
    auto* selected = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    for (auto& c : columns)
        c->setSelected (&c->getStrip() == selected);
    if (extraPinned != nullptr)
        extraPinned->setSelected (&extraPinned->getStrip() == selected);
    channel.setStrip (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr);
    revealSelection();
    applySendsOnFader (false); // it may follow the selected bus
    resized(); // the channel panel shows only for a selected strip
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
    applySendsOnFader (false);
    if (const int x = viewport.getViewPositionX(); x != shownScrollX)
    {
        shownScrollX = x; // the bank at the left is another one
        bankBar->repaint();
    }
    for (auto& c : columns)
        c->refresh();
    if (extraPinned != nullptr)
        extraPinned->refresh();
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
    bar.setRight ((float) jumpKey.getX() - sizes::kGap);
    g.setFont (type::font (type::kCaption, true));
    if (filter == kSpill)
    {
        // A spill isn't a filter key: say what is showing, and the way back.
        g.setColour (theme.accent);
        g.drawFittedText ("SPILL DCA " + juce::String (spillDca + 1) + "  (a filter key goes back)", bar.toNearestInt(), juce::Justification::centredRight, 1, 0.8f);
        return;
    }
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
    bar.removeFromRight (sizes::kGapSmall);
    sofKey.setBounds (bar.removeFromRight (120));
    bar.removeFromRight (sizes::kGapSmall);
    jumpKey.setBounds (bar.removeFromRight (88));
    // The filter keys share what's left (112 px each at most), so none goes under JUMP.
    bar.removeFromRight (sizes::kGap);
    const int n = juce::jmax (1, (int) filterKeys.size());
    const int keyW = juce::jlimit (60, 112, (bar.getWidth() - (n - 1) * sizes::kGapSmall) / n);
    for (auto& key : filterKeys)
    {
        key->setBounds (bar.removeFromLeft (keyW));
        bar.removeFromLeft (sizes::kGapSmall);
    }
    // On a short window the strips need the height: the panel leaves them to it (the CHANNEL
    // key stays lit and the panel comes back when the window is tall enough).
    channel.setVisible (channelShown && getHeight() >= 760 && ctx.getSelectedStrip && ctx.getSelectedStrip() != nullptr);
    if (channel.isVisible())
        channel.setBounds (area.removeFromTop (216));
    // The columns, under the bank keys when they don't all fit (laid out without them first, to know).
    const auto place = [this, area] (bool withBanks)
    {
        auto a = area;
        if (withBanks)
            bankBar->setBounds (a.removeFromTop (kBankBar).reduced (sizes::kGapSmall, 2));
        pinnedArea = {};
        if (pinnedMain != nullptr)
            pinnedArea = a.removeFromRight (layout.columnWidth() + sizes::kGapSmall).withTrimmedLeft (sizes::kGapSmall);
        viewport.setBounds (a);
        layoutColumns();
    };
    place (false);
    const bool banksShown = isBankBarShown();
    bankBar->setVisible (banksShown);
    if (banksShown)
        place (true);
    if (jumpList != nullptr)
    {
        // Under JUMP, over the columns.
        const int w = juce::jmin (380, getWidth() - 2 * sizes::kGap);
        const int y = sizes::kFilterBar;
        jumpList->setBounds (juce::jlimit (sizes::kGap, juce::jmax (sizes::kGap, getWidth() - w - sizes::kGap), jumpKey.getRight() - w), y, w,
                             juce::jmax (0, juce::jmin (560, getHeight() - y - sizes::kGap)));
    }
}

void MixerView::layoutColumns()
{
    const int height = juce::jmax (0, viewport.getHeight() - viewport.getScrollBarThickness());
    int x = sizes::kGapSmall;
    int lastKind = -1;
    for (auto& c : columns)
    {
        if (c.get() == pinnedMain)
            continue;
        const int kind = (int) c->getStrip().getKind();
        if (lastKind >= 0 && kind != lastKind)
            x += sizes::kKindGap;
        lastKind = kind;
        c->setBounds (x, 0, layout.columnWidth(), height);
        x += layout.columnWidth();
    }
    if (! dcaColumns.empty() && ! columns.empty())
        x += sizes::kKindGap;
    for (auto& d : dcaColumns)
    {
        d->setBounds (x, 0, layout.columnWidth(), height);
        x += layout.columnWidth();
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
    if (pinnedMain != nullptr)
        pinnedMain->setBounds (pinnedArea.getX(), pinnedArea.getY(), pinnedArea.getWidth(), height);
    buildBanks();
}

//==============================================================================
template <typename Visit>
void MixerHistory::visit (Visit&& v)
{
    for (int kind = 0; kind < kNumStripKinds; ++kind)
        for (auto& s : engine.getStrips (static_cast<StripKind> (kind)))
        {
            v (s->params.faderDb);
            v (s->params.pan);
            v (s->params.mute);
            v (s->params.solo);
            // The sends that exist, as StripColumn::setSendDestination finds them.
            const auto k = s->getKind();
            if (k == StripKind::Channel || k == StripKind::Aux || k == StripKind::Bus)
            {
                for (auto& bus : engine.getBuses())
                {
                    auto& p = s->sends[(size_t) bus->getId()];
                    v (p.levelDb);
                    v (p.enabled);
                }
                for (size_t i = 0; i < engine.getMains().size(); ++i)
                {
                    auto& p = s->mains[i];
                    v (p.levelDb);
                    v (p.on);
                }
            }
            if (k != StripKind::Matrix)
                for (size_t i = 0; i < engine.getMatrices().size(); ++i)
                {
                    auto& p = s->matrix[i];
                    v (p.levelDb);
                    v (p.on);
                }
        }
}

std::vector<float> MixerHistory::capture()
{
    std::vector<float> state;
    state.reserve (stable.size());
    visit ([&state] (auto& a) { state.push_back ((float) a.load()); });
    return state;
}

void MixerHistory::apply (const std::vector<float>& state)
{
    size_t i = 0;
    visit ([&] (auto& a) { a.store (static_cast<decltype (a.load())> (state[i++])); });
    stable = state;
    settledAt = 0;
}

void MixerHistory::tick (juce::uint32 now)
{
    auto state = capture();
    if (state.size() != stable.size())
    {
        // Another console, other strips: nothing to go back to.
        undoStack.clear();
        redoStack.clear();
        stable = std::move (state);
        latest.clear();
        settledAt = 0;
        return;
    }
    if (state == stable)
    {
        settledAt = 0;
        return;
    }
    if (state != latest)
    {
        // Still moving: one step for one gesture, so wait until it has stopped.
        latest = std::move (state);
        settledAt = now;
        return;
    }
    if (settledAt != 0 && now - settledAt >= kSettleMs)
    {
        undoStack.push_back (std::move (stable));
        if (undoStack.size() > kDepth)
            undoStack.erase (undoStack.begin());
        redoStack.clear();
        stable = latest;
        settledAt = 0;
    }
}

bool MixerHistory::undo()
{
    if (undoStack.empty())
        return false;
    redoStack.push_back (stable);
    apply (undoStack.back());
    undoStack.pop_back();
    return true;
}

bool MixerHistory::redo()
{
    if (redoStack.empty())
        return false;
    undoStack.push_back (stable);
    apply (redoStack.back());
    redoStack.pop_back();
    return true;
}

//==============================================================================
OutputBridge::OutputBridge (UiContext& c, const Theme& t) : ctx (c), theme (t)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (20);
}

void OutputBridge::timerCallback()
{
    if (! isShowing())
        return;
    const auto& mains = ctx.engine.getMains();
    for (int ch = 0; ch < 2; ++ch)
    {
        const float level = mains.empty() ? 0.0f : ctx.meters.get (mains.front().get(), MeterPoint::Output, ch);
        levels[(size_t) ch] = fader::dbToPos (dsp::gainToDb (level));
    }
    const float loudest = mains.empty() ? 0.0f : juce::jmax (ctx.meters.get (mains.front().get(), MeterPoint::Output, 0), ctx.meters.get (mains.front().get(), MeterPoint::Output, 1));
    const float db = dsp::gainToDb (loudest);
    held = db >= held ? db : juce::jmax (db, held - 0.4f);
    repaint();
}

void OutputBridge::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kMicro, true));
    g.drawText ("OUT", area.removeFromLeft (30.0f), juce::Justification::centredLeft);
    g.setColour (held >= 0.0f ? theme.meterClip : theme.text);
    g.setFont (type::font (type::kCaption));
    g.drawText (held <= -90.0f ? juce::String::fromUTF8 ("-\xe2\x88\x9e") : juce::String (held, 1), area.removeFromRight (44.0f), juce::Justification::centredRight);
    const float top = area.getCentreY() - 7.0f;
    static const float lowTop = fader::dbToPos (-18.0f), midTop = fader::dbToPos (-6.0f), highTop = fader::dbToPos (0.0f);
    for (int ch = 0; ch < 2; ++ch)
    {
        const juce::Rectangle<float> bar (area.getX(), top + (float) ch * 8.0f, area.getWidth() - sizes::kGap, 6.0f);
        g.setColour (theme.well);
        g.fillRect (bar);
        const float p = levels[(size_t) ch];
        const auto fill = [&] (float from, float to, juce::Colour c)
        {
            if (p > from)
            {
                g.setColour (c);
                g.fillRect (bar.getX() + bar.getWidth() * from, bar.getY(), bar.getWidth() * (juce::jmin (p, to) - from), bar.getHeight());
            }
        };
        fill (0.0f, lowTop, theme.meterLow);
        fill (lowTop, midTop, theme.meterMid);
        fill (midTop, highTop, theme.meterHigh);
        fill (highTop, 1.0f, theme.meterClip);
    }
}

} // namespace livedaw::ui::universal
