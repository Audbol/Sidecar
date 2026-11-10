#include "ui/WingSurface.h"
#include "engine/AppPaths.h"
#include "ui/WingPopups.h"

namespace livedaw::ui
{

//==============================================================================
/** Surface lock: covers the surface (everything but the main display), ignores
    its input and shows a lock. */
class WingSurface::LockOverlay final : public juce::Component
{
public:
    explicit LockOverlay (WingSurface& o) : owner (o) {}

    bool hitTest (int x, int y) override { return ! owner.display->getBounds().contains (x, y); }

    void paint (juce::Graphics& g) override
    {
        g.excludeClipRegion (owner.display->getBounds());
        g.fillAll (juce::Colours::black.withAlpha (0.45f));
        const auto faders = owner.isRack() ? owner.rackSection->getBounds()
                                           : owner.sections[0]->getBounds().getUnion (owner.isCompact() ? owner.compactKeys->getBounds()
                                                                                                        : owner.sections[LayerSet::kNumSections - 1]->getBounds());
        auto badge = juce::Rectangle<int> (300, 64).withCentre (faders.getCentre());
        g.setColour (Colours::screenPanel.withAlpha (0.95f));
        g.fillRoundedRectangle (badge.toFloat(), 6.0f);
        drawIcon (g, Icon::LockClosed, badge.removeFromLeft (64).reduced (14).toFloat(), Colours::yellow);
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("LOCKED: HOLD HOME", badge, juce::Justification::centredLeft);
    }

private:
    WingSurface& owner;
};

//==============================================================================
WingSurface::WingSurface (UiContext& c) : universalKey ("UNIVERSAL MIXER", universal::Theme::dark().accent, universal::Theme::dark()), ctx (c)
{
    universalKey.onClick = [&context = ctx]
    {
        juce::MessageManager::callAsync ([&context]
        {
            if (context.setInterface)
                context.setInterface (InterfaceKind::Universal);
        });
    };
    universalKey.setTooltip ("The Universal Mixer");
    addAndMakeVisible (universalKey);
    // What the WING's screens and keys reach through the context.
    ctx.showScreen = [this] (Screen screen) { display->showScreen (screen); };
    ctx.showOverviewPage = [this] (int area, int bank) { display->showOverviewPage (cc::overviewSection (area), bank); };
    ctx.showHomeTab = [this] (HomeTab tab) { display->showHomeTab (tab); };
    ctx.editFx = [this] (int slot) { display->editFx (slot); };
    ctx.showFxSendOutputs = [this] (int slot) { display->showFxSendOutputs (slot); };
    ctx.showRoutingTab = [this] (int tab) { display->showRoutingTab (tab); };
    ctx.getScreen = [this] { return display != nullptr ? display->getScreen() : Screen::Home; };
    // The COMPACT's faders are the left section's, the RACK's four channels too.
    ctx.revealFader = [this] (LayerSet::Section section, const state::FaderRef& f)
    {
        if (isRack() && rackSection != nullptr)
            rackSection->showChannels();
        sections[isSmallModel (ctx.consoleModel) ? LayerSet::Left : section]->reveal (f);
    };
    ctx.takeScreenshot = [this] { saveScreenshot(); };
    ctx.holdHomeLock = [this] (const juce::String& held) { holdHomeLock (held); };
    ctx.settings.setValue (uiprefs::surfaceLock, juce::String()); // a new surface is unlocked, as the console after a restart
    ctx.isSurfaceLocked = [this] { return surfaceLock.isLocked(); };
    ctx.setScreenMaximized = [this] (bool maximized) { setScreenMaximized (maximized); };
    ctx.isScreenMaximized = [this] { return screenMaximized; };

    channelStrip = std::make_unique<ChannelStripSection> (ctx);
    display = std::make_unique<MainDisplay> (ctx);
    monitor = std::make_unique<MonitorSection> (ctx);
    for (int i = 0; i < LayerSet::kNumSections; ++i)
        sections[i] = std::make_unique<FaderSection> (ctx, static_cast<LayerSet::Section> (i));
    customControls = std::make_unique<CustomControlSection> (ctx);
    compactKeys = std::make_unique<CompactSection> (ctx);
    addChildComponent (*compactKeys);
    // The RACK's section lies under its channels (the left section's strips)
    // and the custom controls, which take their place on CUST, MUTE and TRANS.
    rackSection = std::make_unique<RackSection> (ctx, *sections[LayerSet::Left], *customControls);
    rackSection->onModeChanged = [this] { updateVisibility(); };
    addChildComponent (*rackSection);
    // The 4-Channel section works on the right fader section's four strips.
    fourChannel = std::make_unique<FourChannelSection> (ctx, [this] (int i) { return sections[LayerSet::Right] != nullptr ? sections[LayerSet::Right]->getStrip (i) : nullptr; });

    for (juce::Component* comp : { (juce::Component*) channelStrip.get(), (juce::Component*) display.get(), (juce::Component*) monitor.get(),
                                   (juce::Component*) customControls.get(), (juce::Component*) fourChannel.get() })
        addAndMakeVisible (comp);
    for (auto& sec : sections)
        addAndMakeVisible (*sec);
    // DAW mode: the DAW's strips over the fader sections (they show themselves).
    for (int i = 0; i < LayerSet::kNumSections; ++i)
    {
        dawSections[i] = std::make_unique<DawFaderSection> (ctx, static_cast<LayerSet::Section> (i));
        addChildComponent (*dawSections[i]);
    }

    splitter.setTooltip ("Drag to resize the screen and the fader sections");
    splitter.onDrag = [this] (int delta)
    {
        topFraction = juce::jlimit (0.3f, 0.7f, topFraction + (float) delta / (float) juce::jmax (1, getHeight()));
        resized();
    };
    splitter.onDragEnd = [this] { ctx.settings.setValue ("ui.topFraction", topFraction); };
    splitter.onDoubleClick = [this] { topFraction = 0.52f; resized(); };
    addAndMakeVisible (splitter);
    topFraction = (float) ctx.settings.getDoubleValue ("ui.topFraction", 0.52);

    consoleModelChanged (false);
    // Screen only, as the last session left it.
    if (ctx.settings.getBoolValue (uiprefs::screenMaximized, false))
        setScreenMaximized (true);
}

WingSurface::~WingSurface()
{
    // Nothing reaches this surface through the context any more.
    ctx.showScreen = [] (Screen) {};
    ctx.showOverviewPage = [] (int, int) {};
    ctx.showHomeTab = [] (HomeTab) {};
    ctx.editFx = [] (int) {};
    ctx.showFxSendOutputs = [] (int) {};
    ctx.showRoutingTab = [] (int) {};
    ctx.getScreen = [] { return Screen::Home; };
    ctx.revealFader = [] (LayerSet::Section, const state::FaderRef&) {};
    ctx.takeScreenshot = [] {};
    ctx.holdHomeLock = [] (const juce::String&) {};
    ctx.isSurfaceLocked = [] { return false; };
    ctx.setScreenMaximized = [] (bool) {};
    ctx.isScreenMaximized = [] { return false; };
    if (surfaceLock.isLocked())
        ctx.settings.setValue (uiprefs::surfaceLock, juce::String());

    lockOverlay.reset();
    fourChannel.reset();
    rackSection.reset(); // before the section and the custom controls it drives
    compactKeys.reset();
    for (auto& sec : dawSections)
        sec.reset();
    for (auto& sec : sections)
        sec.reset();
    customControls.reset();
    monitor.reset();
    display.reset();
    channelStrip.reset();
}

//==============================================================================
void WingSurface::refreshAll()
{
    refreshSections();
    channelStrip->refresh();
    display->refresh();
    display->refreshLibrary();
    customControls->refresh();
}

void WingSurface::refreshSections()
{
    layoutSections();
    for (auto& sec : sections)
        sec->refresh();
    for (int s = 0; s < LayerSet::kNumSections; ++s)
        if (dawSections[s] != nullptr)
            dawSections[s]->setNumStrips (sections[s]->getNumStrips());
    fourChannel->refresh();
    if (compactKeys != nullptr)
        compactKeys->refresh();
    if (rackSection != nullptr)
        rackSection->refresh();
}

void WingSurface::selectionChanged()
{
    channelStrip->refresh();
    display->refresh();
    if (ctx.sendsOnFader)
        sendsOnFaderChanged();
}

void WingSurface::sendsOnFaderChanged()
{
    for (auto& sec : sections)
        sec->refreshFaders();
    if (compactKeys != nullptr)
        compactKeys->refresh();
}

void WingSurface::showChanged()
{
    if (customControls != nullptr)
        customControls->refresh();
    if (display != nullptr)
        display->refreshLibrary();
}

void WingSurface::customControlsChanged()
{
    if (customControls != nullptr)
        customControls->refresh();
}

void WingSurface::consoleModelChanged (bool changed)
{
    monitor->setConsoleModel (ctx.consoleModel);
    // The RACK: no encoders under the screen, its channels on encoders.
    display->setRackPanel (isRack());
    rackSection->setActive (isRack());
    if (changed && isSmallModel (ctx.consoleModel))
        ctx.sendsOnFader = false; // the COMPACT's SOF is the BUS and MAIN MATRIX keys', the RACK's a custom control's
    updateVisibility();
    refreshSections();
    resized();
    repaint();
}

void WingSurface::setCovered (bool shouldBeCovered)
{
    Skin::setCovered (shouldBeCovered);
    updateVisibility();
}

void WingSurface::focusWindow()
{
    if (auto* window = getParentComponent())
        window->grabKeyboardFocus();
}

bool WingSurface::skinKeyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    // F10 (or Ctrl+Shift+F): only the screen, or back; Esc leaves it.
    if (key.getKeyCode() == juce::KeyPress::F10Key && ! mods.isAnyModifierKeyDown())
    {
        setScreenMaximized (! screenMaximized);
        return true;
    }
    if (key == juce::KeyPress::escapeKey && ! covered && screenMaximized)
    {
        setScreenMaximized (false);
        return true;
    }
    if (mods.isCommandDown())
    {
        // With Ctrl held, X11 gives a control code as the text and the lowercase key as the code.
        const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
        const auto k = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getKeyCode());
        if ((c == 'f' || k == 'f') && mods.isShiftDown())
        {
            setScreenMaximized (! screenMaximized);
            return true;
        }
        if ((c == 'p' || k == 'p') && ! mods.isShiftDown())
        {
            saveScreenshot();
            return true;
        }
        if (c == 'l' || k == 'l')
        {
            holdHomeLock (SurfaceLock::homeOnly()); // as holding HOME alone
            return true;
        }
    }
    return false;
}

//==============================================================================
void WingSurface::paint (juce::Graphics& g)
{
    paintSurface (g);
    if (! screenMaximized)
        surfacetopbar::paint (g, universal::Theme::dark(), getLocalBounds(),
                              ctx.consoleModel == ConsoleModel::Rack ? "WING RACK" : ctx.consoleModel == ConsoleModel::Compact ? "WING COMPACT" : "WING");
}

void WingSurface::paintSurface (juce::Graphics& g)
{
    if (isRack() && ! screenMaximized)
    {
        // The RACK's panel in a rack: the window around it dark, a rack ear
        // with two slots either side where there is room (Sidecar's drawing).
        g.fillAll (juce::Colour (0xff0a0a0b));
        g.setColour (surfaceColours().surface);
        g.fillRect (rackPanel);
        const int ear = juce::jmin (36, (getWidth() - rackPanel.getWidth()) / 2);
        if (ear >= 14)
            for (const int x : { rackPanel.getX() - ear, rackPanel.getRight() })
            {
                const auto plate = juce::Rectangle<int> (x, rackPanel.getY(), ear, rackPanel.getHeight());
                g.setColour (surfaceColours().surface);
                g.fillRect (plate);
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillRect (x == rackPanel.getRight() ? plate.withWidth (1) : plate.withLeft (plate.getRight() - 1));
                for (const int y : { plate.getY() + plate.getHeight() / 6, plate.getBottom() - plate.getHeight() / 6 })
                {
                    g.setColour (juce::Colours::black);
                    g.fillRoundedRectangle (juce::Rectangle<float> ((float) ear * 0.5f, (float) ear * 0.26f).withCentre ({ (float) plate.getCentreX(), (float) y }),
                                            (float) ear * 0.13f);
                }
            }
        return;
    }
    g.fillAll (screenMaximized ? juce::Colours::black : surfaceColours().surface);
}

juce::Rectangle<int> WingSurface::bodyArea() const
{
    return screenMaximized ? getLocalBounds() : getLocalBounds().withTrimmedTop (surfacetopbar::kHeight);
}

int WingSurface::topRowHeight() const
{
    const int h = bodyArea().getHeight();
    // Wide windows are short for their width: past 2:1 the top row (and with
    // it the screen, the most important part) takes up to 8 % more.
    const float aspect = (float) getWidth() / (float) juce::jmax (1, h);
    const float wideBoost = juce::jlimit (0.0f, 0.08f, (aspect - 2.0f) * 0.06f);
    // Short windows (a 1366 x 768 laptop) keep the screen legible, as tall as
    // the fader sections' compact layout (meters folded away) allows.
    const int legible = juce::jlimit (300, juce::jmax (300, h - 6 - FaderStrip::kMinCompactHeight),
                                      MainDisplay::heightForScreen (MainDisplay::kLegibleScreenHeight));
    return juce::jlimit (legible, juce::jmax (legible, h - 260), juce::roundToInt ((float) h * (topFraction + wideBoost)));
}

WingSurface::BottomRowPlan WingSurface::planBottomRow (int total, int height) const
{
    int fixedStrips = 0, autoCount = 0;
    for (int s = 0; s < LayerSet::kNumSections; ++s)
    {
        const int w = ctx.show.layers.getWidth (static_cast<LayerSet::Section> (s));
        if (w == LayerSet::kAutoWidth)
            ++autoCount;
        else
            fixedStrips += w;
    }

    // The custom controls section (between center and right) is about four
    // strips wide plus a key column. When the window is too narrow for usable
    // strips, its lower part moves under the monitoring panel instead. Strips
    // are at most about 0.15 of the row's height wide.
    const int banks = LayerSet::kNumSections * FaderSection::kBankColumnWidth;
    const int strips = juce::jmax (1, fixedStrips + autoCount * 12);
    constexpr int ccStrips = 4, minStripWithCc = 40;
    BottomRowPlan plan;
    plan.stripW = (total - banks - FaderSection::kBankColumnWidth) / (strips + ccStrips);
    plan.withCc = plan.stripW >= minStripWithCc;
    if (! plan.withCc)
        plan.stripW = (total - banks) / strips;
    plan.stripW = juce::jlimit (40, juce::jlimit (70, 120, height * 15 / 100), plan.stripW);
    return plan;
}

WingSurface::TopRowSides WingSurface::topRowSideWidths (int width)
{
    // About 18 %, 26 % and 12 % of the row (the console: 19, 28 and 13).
    return { juce::jlimit (220, 400, width * 18 / 100), juce::jlimit (320, 580, width * 26 / 100), juce::jlimit (190, 320, width * 12 / 100) };
}

juce::Rectangle<int> WingSurface::consoleArea() const
{
    const auto all = bodyArea();
    const int width = all.getWidth();
    const int topH = topRowHeight();

    // The bottom row at its widest strips (Auto width sections fill any width).
    int bottomW = 0;
    const auto plan = planBottomRow (width, all.getHeight() - topH - 6);
    for (int s = 0; s < LayerSet::kNumSections; ++s)
    {
        const int n = ctx.show.layers.getWidth (static_cast<LayerSet::Section> (s));
        if (n == LayerSet::kAutoWidth)
            return all;
        bottomW += FaderSection::widthFor (n, plan.stripW);
    }
    if (plan.withCc)
        bottomW += FaderSection::kBankColumnWidth + 4 * plan.stripW;

    // The top row: the monitoring panel, the channel strip and the 4-Channel
    // column at their usual size, the display unit as wide as its height wants.
    const int topW = topRowSideWidths (width).total() + MainDisplay::preferredWidth (topH);

    return all.withSizeKeepingCentre (juce::jmin (width, juce::jmax (bottomW, topW)), all.getHeight());
}

void WingSurface::placeDisplay (juce::Rectangle<int> bounds)
{
    // Screen only: the display takes the window (the rest keeps its layout,
    // hidden, so restoring brings it back unchanged). Past about 1100 px of
    // height it is laid out at that size and scaled up as a whole, so the
    // screens keep the proportions they were drawn for (all vector drawing,
    // so it stays sharp at 4K).
    if (screenMaximized)
    {
        const auto all = getLocalBounds();
        const float scale = juce::jmax (1.0f, (float) all.getHeight() / kMaxScreenOnlyLayoutHeight);
        display->setTransform (juce::AffineTransform::scale (scale));
        display->setBounds (0, 0, (int) std::ceil ((float) all.getWidth() / scale), (int) std::ceil ((float) all.getHeight() / scale));
    }
    else
    {
        display->setTransform ({});
        display->setBounds (bounds);
    }
}

void WingSurface::layoutCompact()
{
    const int topH = topRowHeight();
    const auto plan = layout::compact::plan (bodyArea(), topH, MainDisplay::preferredWidth (topH));
    monitor->setBounds (plan.panel);
    splitter.setBounds (plan.splitter);
    placeDisplay (plan.display);
    sections[LayerSet::Left]->setBounds (plan.faders);
    compactKeys->setStripWidth (plan.stripW);
    compactKeys->setBounds (plan.keys);
    // What the COMPACT doesn't have keeps no place on the surface.
    for (juce::Component* c : { (juce::Component*) channelStrip.get(), (juce::Component*) customControls.get(), (juce::Component*) fourChannel.get(),
                                (juce::Component*) sections[LayerSet::Center].get(), (juce::Component*) sections[LayerSet::Right].get() })
        c->setBounds ({});
    for (int s = 0; s < LayerSet::kNumSections; ++s)
        if (dawSections[s] != nullptr)
        {
            dawSections[s]->setBounds (sections[s]->getBounds());
            dawSections[s]->setNumStrips (sections[s]->getNumStrips());
        }
}

void WingSurface::layoutRack()
{
    const auto plan = layout::rack::plan (bodyArea(), [] (int h) { return MainDisplay::preferredWidth (h); });
    if (plan.panel != rackPanel)
    {
        rackPanel = plan.panel;
        repaint();
    }
    placeDisplay (plan.display);
    rackSection->setBounds (plan.section);
    // Its four channels, and the custom controls that take their place.
    const auto channels = rackSection->getChannelsArea() + plan.section.getPosition();
    sections[LayerSet::Left]->setBounds (channels);
    customControls->setBounds (channels);
    // What the RACK doesn't have keeps no place on the surface.
    for (juce::Component* c : { (juce::Component*) channelStrip.get(), (juce::Component*) monitor.get(), (juce::Component*) fourChannel.get(),
                                (juce::Component*) compactKeys.get(), (juce::Component*) sections[LayerSet::Center].get(),
                                (juce::Component*) sections[LayerSet::Right].get(), (juce::Component*) &splitter })
        c->setBounds ({});
    // (DAW mode's fader strips have no faders to lie over.)
    for (auto& sec : dawSections)
        if (sec != nullptr)
            sec->setBounds ({});
}

void WingSurface::layoutSections()
{
    if (isRack())
    {
        layoutRack();
        return;
    }
    if (isCompact())
    {
        layoutCompact();
        return;
    }
    auto b = consoleArea();
    b.removeFromTop (topRowHeight() + 6);

    auto& layers = ctx.show.layers;
    int widths[LayerSet::kNumSections];
    int autoCount = 0;
    for (int s = 0; s < LayerSet::kNumSections; ++s)
    {
        widths[s] = layers.getWidth (static_cast<LayerSet::Section> (s));
        if (widths[s] == LayerSet::kAutoWidth)
            ++autoCount;
    }

    const int total = b.getWidth();
    const auto plan = planBottomRow (total, b.getHeight());
    const int stripW = plan.stripW;
    const bool withCc = plan.withCc;
    const int ccW = withCc ? FaderSection::kBankColumnWidth + 4 * stripW : 0;
    if (withCc != ccInBottomRow)
    {
        ccInBottomRow = withCc;
        customControls->setCompact (! withCc);
        layoutTopRow();
    }

    int remaining = total - ccW;
    for (int s = 0; s < LayerSet::kNumSections; ++s)
        if (widths[s] != LayerSet::kAutoWidth)
            remaining -= FaderSection::widthFor (widths[s], stripW);

    // What the strips leave: a few pixels go to the last section; more (the
    // strips at their widest) is shared either side, the row centred.
    int x = b.getX();
    const bool centred = autoCount == 0 && remaining >= stripW;
    if (centred)
        x += remaining / 2;

    for (int s = 0; s < LayerSet::kNumSections; ++s)
    {
        if (s == LayerSet::Right && withCc)
        {
            customControls->setBounds (x, b.getY(), ccW, b.getHeight());
            x += ccW;
        }

        int w;
        if (widths[s] == LayerSet::kAutoWidth)
        {
            w = juce::jmax (FaderSection::kBankColumnWidth + FaderSection::kMinStripWidth, remaining / juce::jmax (1, autoCount));
            layers.setFittedWidth (static_cast<LayerSet::Section> (s), FaderSection::fitStrips (w));
        }
        else
        {
            w = FaderSection::widthFor (widths[s], stripW);
        }
        if (s == LayerSet::kNumSections - 1 && ! centred)
            w = juce::jmax (w, b.getRight() - x);
        sections[s]->setBounds (x, b.getY(), w, b.getHeight());
        x += w;
    }
    for (int s = 0; s < LayerSet::kNumSections; ++s)
        if (dawSections[s] != nullptr)
        {
            dawSections[s]->setBounds (sections[s]->getBounds());
            dawSections[s]->setNumStrips (sections[s]->getNumStrips());
        }
}

void WingSurface::layoutTopRow()
{
    if (isRack())
    {
        layoutRack();
        return;
    }
    if (isCompact())
    {
        layoutCompact();
        return;
    }
    auto b = consoleArea();
    auto top = b.removeFromTop (topRowHeight());
    splitter.setBounds (b.removeFromTop (6));

    // As on the WING: the WING panel with USB AUDIO/DATA and MONITORING/
    // TALKBACK at the left, the main display unit (keys, screen and its right
    // hand column), the channel strip section, and the 4-Channel section at
    // the right (it works on the right fader section below it). Narrow windows
    // put the lower part of the custom controls there instead. Width the
    // display unit can't use is shared either side of it.
    // Windows too narrow for that keep the display wide: the channel strip at
    // the left, the monitoring panel over the 4-Channel section at the right,
    // both narrowed (to a point) until the screen is legible.
    const auto sides = topRowSideWidths (top.getWidth());
    const int displayW = MainDisplay::preferredWidth (top.getHeight());
    const int legibleW = MainDisplay::preferredWidth (MainDisplay::heightForScreen (MainDisplay::kLegibleScreenHeight));
    juce::Rectangle<int> right;
    if (top.getWidth() - sides.total() >= juce::jmin (displayW, juce::jmax (legibleW, top.getWidth() * 40 / 100)))
    {
        monitor->setBounds (top.removeFromLeft (sides.monitor));
        right = top.removeFromRight (sides.fourChannel);
        channelStrip->setBounds (top.removeFromRight (sides.strip));
    }
    else
    {
        int stripW = juce::jlimit (330, 620, top.getWidth() * 29 / 100);
        int rightW = juce::jlimit (260, 460, top.getWidth() * 22 / 100);
        if (const int over = stripW + rightW + displayW - top.getWidth(); over > 0)
        {
            const int stripSpare = stripW - kMinCompactStripWidth, rightSpare = rightW - kMinCompactRightWidth;
            const int cut = juce::jmin (over, stripSpare + rightSpare);
            const int stripCut = cut * stripSpare / juce::jmax (1, stripSpare + rightSpare);
            stripW -= stripCut;
            rightW -= cut - stripCut;
        }
        channelStrip->setBounds (top.removeFromLeft (stripW));
        right = top.removeFromRight (rightW);
        monitor->setBounds (right.removeFromTop (right.getHeight() * (ccInBottomRow ? 46 : 55) / 100));
    }
    top = top.withSizeKeepingCentre (juce::jmin (top.getWidth(), MainDisplay::preferredWidth (top.getHeight())), top.getHeight());
    fourChannel->setVisible (ccInBottomRow && ! covered && ! screenMaximized);
    if (! ccInBottomRow)
        customControls->setBounds (right);
    else
        fourChannel->setBounds (right);
    placeDisplay (top);
}

void WingSurface::resized()
{
    surfacetopbar::place (universalKey, getWidth());
    layoutTopRow();

    // Auto-width sections may now hold a different number of faders.
    int before[LayerSet::kNumSections];
    for (int s = 0; s < LayerSet::kNumSections; ++s)
        before[s] = ctx.show.layers.getEffectiveWidth (static_cast<LayerSet::Section> (s));
    layoutSections();
    for (int s = 0; s < LayerSet::kNumSections; ++s)
        if (ctx.show.layers.getEffectiveWidth (static_cast<LayerSet::Section> (s)) != before[s])
            sections[s]->refresh();

    if (lockOverlay != nullptr)
        lockOverlay->setBounds (getLocalBounds());
}

void WingSurface::saveScreenshot()
{
    const auto dir = paths::documents().getChildFile ("screens");
    const auto file = dir.getChildFile ("screen-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".png");
    // At the size it's shown (scaled up when only the screen shows).
    const auto image = display->createComponentSnapshot (display->getLocalBounds(), true, juce::jmax (1.0f, display->getTransform().mat00));

    juce::PNGImageFormat png;
    bool ok = dir.createDirectory().wasOk();
    if (ok)
    {
        file.deleteFile();
        juce::FileOutputStream out (file);
        ok = out.openedOk() && png.writeImageToStream (image, out);
    }
    if (ok)
        ctx.log.info ("Screenshot saved to " + file.getFullPathName());
    else
        ctx.log.warning ("Couldn't save the screenshot to " + file.getFullPathName());
}

void WingSurface::holdHomeLock (const juce::String& held)
{
    const auto result = surfaceLock.holdHome (held);
    if (result == SurfaceLock::Result::WrongCode)
    {
        ctx.log.info ("Surface stays locked: hold HOME with the keys it was locked with");
        return;
    }
    const bool locked = result == SurfaceLock::Result::Locked;
    if (locked)
    {
        lockOverlay = std::make_unique<LockOverlay> (*this);
        addAndMakeVisible (*lockOverlay);
        lockOverlay->setBounds (getLocalBounds());
        lockOverlay->setVisible (! covered);
    }
    else
    {
        lockOverlay.reset();
    }
    // The touchscreen and the display's knobs too (SHOW METER PAGE WHEN LOCKED there).
    display->setLocked (locked);
    // The WING remote's /$ctl/$stat/cnslock.
    ctx.settings.setValue (uiprefs::surfaceLock, surfaceLock.getCode());
    ctx.log.info (locked ? "Surface locked" : "Surface unlocked");
}

void WingSurface::updateVisibility()
{
    // The console's parts stay alive (and keep their state) under the editor
    // and while the screen is maximized.
    const bool surface = ! covered && ! screenMaximized;
    universalKey.setVisible (surface);
    // The COMPACT has no channel strip, custom controls, 4-Channel, center
    // or right section; it has its key section instead. The RACK has its
    // four-channel section: the left section's strips as its channels, or
    // the custom controls in their place (CUST, MUTE groups, TRANS).
    const bool rack = isRack(), compact = isCompact();
    const bool full = ! rack && ! compact;
    const bool rackChannels = rack && rackSection->showsChannels();
    for (juce::Component* c : { (juce::Component*) monitor.get(), (juce::Component*) &splitter })
        c->setVisible (surface && ! rack);
    channelStrip->setVisible (surface && full);
    customControls->setVisible (surface && (full || (rack && ! rackChannels)));
    display->setVisible (! covered);
    fourChannel->setVisible (surface && ccInBottomRow && full);
    for (int s = 0; s < LayerSet::kNumSections; ++s)
        sections[s]->setVisible (surface && (full || (s == LayerSet::Left && (compact || rackChannels))));
    compactKeys->setVisible (surface && compact);
    rackSection->setVisible (surface && rack);
    // DAW mode's strips show themselves while the surface does (their timer).
    for (auto& sec : dawSections)
        if (sec != nullptr && ! surface)
            sec->setVisible (false);
    if (lockOverlay != nullptr)
        lockOverlay->setVisible (surface);
}

void WingSurface::setScreenMaximized (bool maximized)
{
    if (maximized == screenMaximized)
        return;
    screenMaximized = maximized;
    ctx.settings.setValue (uiprefs::screenMaximized, maximized);
    display->setMaximized (maximized);
    updateVisibility();
    layoutTopRow();
    repaint();
    if (! covered && isShowing())
        focusWindow();
}

} // namespace livedaw::ui
