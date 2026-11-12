#include "ui/PluginHostSurface.h"

#include "engine/FxSlots.h"
#include "ui/ChainSummary.h"

#include <cmath>

namespace livedaw::ui
{
using namespace universal;

namespace
{
    const juce::Colour kPlateTop (0xff1c2025), kPlateBottom (0xff15181c), kEdge (0xff2d343d), kScreenBezel (0xff07080a);
    const juce::Colour kAmber (0xffffb340);

    /** The strip meters' segments, bottom to top: green, then amber, orange and red at the top. */
    constexpr int kSegments = 14;

    void drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float value, bool fromTop, const Theme& t, bool dim)
    {
        g.setColour (t.well);
        g.fillRoundedRectangle (r.expanded (2.0f), 3.0f);
        const float h = r.getHeight() / (float) kSegments;
        for (int k = 0; k < kSegments; ++k)
        {
            const float y = fromTop ? r.getY() + h * (float) k : r.getBottom() - h * (float) (k + 1);
            const auto seg = juce::Rectangle<float> (r.getX(), y + 1.0f, r.getWidth(), h - 2.0f);
            const bool lit = ! dim && value > (float) k / (float) kSegments + 0.001f;
            const auto colour = fromTop ? t.meterMid : k < 9 ? t.meterLow : k < 12 ? t.meterMid : k < 13 ? t.meterHigh : t.meterClip;
            g.setColour (lit ? colour : t.outline.withAlpha (dim ? 0.25f : 0.5f));
            g.fillRect (seg);
        }
    }
} // namespace

namespace ph
{
    const hw::Look& look()
    {
        static const hw::Look l = []
        {
            hw::Look k;
            k.panelTop = kPlateTop;
            k.panelBottom = kPlateBottom;
            k.frame = juce::Colour (0xff0a0b0d);
            k.section = kPlateBottom;
            k.sectionLine = kEdge;
            k.silk = juce::Colour (0xffd2d9e2);
            k.silkDim = juce::Colour (0xff8892a0);
            k.keyTop = juce::Colour (0xff3a414c);
            k.keyBottom = juce::Colour (0xff2a3039);
            k.keyText = juce::Colour (0xffe8ecf1);
            k.backlit = true;
            k.slot = juce::Colour (0xff08090a);
            k.knob = juce::Colour (0xff3b414b);
            k.knobLine = juce::Colour (0xffe8ecf1);
            k.knobRing = juce::Colour (0xff566070);
            k.lcd = juce::Colour (0xff0a1317);
            k.lcdPixel = juce::Colour (0xffd5dde6);
            k.lcdDim = juce::Colour (0xff5b6772);
            k.lcdBezel = juce::Colour (0xff0b0c0e);
            k.keyRadius = 6.0f;
            k.knobStyle = hw::Look::Knob::Dome;
            return k;
        }();
        return l;
    }

    // ---- Key ------------------------------------------------------------------------------

    Key::Key (const juce::String& t, juce::Colour lit, Glyph gl) : text (t), litColour (lit), glyph (gl) {}

    void Key::setText (const juce::String& t)
    {
        if (t != text)
        {
            text = t;
            repaint();
        }
    }

    void Key::click()
    {
        if (onClick)
            onClick();
        refresh();
    }

    void Key::refresh()
    {
        const bool now = isOn && isOn();
        if (now != shownOn)
        {
            shownOn = now;
            repaint();
        }
    }

    void Key::paint (juce::Graphics& g)
    {
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        hw::key (g, area, glyph == Glyph::None ? text : juce::String(), shownOn, down, litColour, look(), {}, false);
        if (glyph != Glyph::None)
        {
            const auto c = area.getCentre().translated (0.0f, down ? 1.0f : 0.0f);
            const float h = juce::jmin (area.getHeight() * 0.32f, 12.0f), w = h * 0.8f, dir = glyph == Glyph::Left ? -1.0f : 1.0f;
            juce::Path p;
            p.addTriangle (c.x + dir * w, c.y, c.x - dir * w, c.y - h, c.x - dir * w, c.y + h);
            g.setColour (shownOn ? juce::Colour (0xff101010) : look().keyText);
            g.fillPath (p);
        }
    }

    void Key::mouseDown (const juce::MouseEvent&)
    {
        down = true;
        repaint();
    }

    void Key::mouseUp (const juce::MouseEvent& e)
    {
        down = false;
        repaint();
        if (getLocalBounds().contains (e.getPosition()))
            click();
    }

    // ---- Lcd ------------------------------------------------------------------------------

    void Lcd::refresh()
    {
        const auto now = getLines ? getLines() : juce::StringArray();
        if (now != lines)
        {
            lines = now;
            repaint();
        }
    }

    void Lcd::paint (juce::Graphics& g)
    {
        const auto& l = look();
        const auto r = getLocalBounds().toFloat();
        g.setColour (l.lcdBezel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colour (0xff2a3138));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        auto screen = r.reduced (6.0f, 4.0f);
        g.setColour (l.lcd);
        g.fillRoundedRectangle (screen, 3.0f);
        screen = screen.reduced (6.0f, 2.0f);
        for (int i = 0; i < lines.size(); ++i)
        {
            const float size = i == 0 ? firstSize : otherSize;
            auto row = screen.removeFromTop (size * 1.25f);
            g.setColour (i == 0 ? accent : l.lcdPixel);
            g.setFont (type::font (size, true));
            g.drawFittedText (lines[i], row.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        }
    }

    // ---- HotKnob --------------------------------------------------------------------------

    HotKnob::HotKnob() { setTooltip ("A hot knob: it takes the parameter you assign to it (CONTROL page, not built yet)"); }

    void HotKnob::paint (juce::Graphics& g)
    {
        // Not assigned: a dim ring, resting at its start.
        hw::knob (g, getLocalBounds().toFloat().reduced (1.0f), 0.0f, look(), hw::Look::Knob::Dome, look().knob, look().knobRing.withAlpha (0.5f), false);
    }

    void HotKnob::mouseUp (const juce::MouseEvent& e)
    {
        if (e.mouseWasClicked() && onClick)
            onClick();
    }

    // ---- DataWheel ------------------------------------------------------------------------

    void DataWheel::paint (juce::Graphics& g)
    {
        hw::knob (g, getLocalBounds().toFloat().reduced (2.0f), angle - std::floor (angle), look(), hw::Look::Knob::Wheel, juce::Colour (0xff2c323b), {}, false);
    }

    void DataWheel::turn (int steps)
    {
        if (steps == 0)
            return;
        angle += (float) steps / 24.0f;
        repaint();
        if (onTurn)
            onTurn (steps);
    }

    void DataWheel::mouseDown (const juce::MouseEvent& e)
    {
        lastX = e.position.x;
        lastY = e.position.y;
        carried = 0.0f;
    }

    void DataWheel::mouseDrag (const juce::MouseEvent& e)
    {
        // Right and up turn it forwards; a notch every 24 px.
        carried += (e.position.x - lastX) - (e.position.y - lastY);
        lastX = e.position.x;
        lastY = e.position.y;
        const int steps = (int) (carried / 24.0f);
        if (steps != 0)
        {
            carried -= (float) steps * 24.0f;
            turn (steps);
        }
    }

    void DataWheel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        const float d = w.deltaY != 0.0f ? w.deltaY : w.deltaX;
        if (d != 0.0f)
            turn (d > 0.0f ? 1 : -1);
    }

    // ---- StripUnit ------------------------------------------------------------------------

    StripUnit::StripUnit (PluginHostSurface& o, UiContext& c, const Theme& t, int position)
        : owner (o), ctx (c), theme (t), selectKey ("SELECT", t.accent), bypassKey ("BYPASS", kAmber)
    {
        juce::ignoreUnused (position);
        selectKey.isOn = [this] { return channel >= 0 && owner.getSelectedChannel() == channel; };
        selectKey.onClick = [this]
        {
            if (channel >= 0)
                owner.selectChannel (channel);
        };
        bypassKey.isOn = [this]
        {
            auto* s = strip();
            return s != nullptr && s->params.input.bypass.load();
        };
        bypassKey.onClick = [this]
        {
            if (auto* s = strip())
                s->params.input.bypass = ! s->params.input.bypass.load();
        };
        selectKey.setTooltip ("Select this channel: its chain shows on the RACK page");
        bypassKey.setTooltip ("Bypass the whole chain: the input passes straight through");
        lcd.accent = t.accent;
        lcd.firstSize = 20.0f;
        lcd.otherSize = 14.0f;
        lcd.getLines = [this]
        {
            juce::StringArray lines;
            auto* s = strip();
            if (s == nullptr)
                return lines;
            lines.add (juce::String (channel + 1).paddedLeft ('0', 2));
            const auto name = s->getOwnName();
            lines.add (name == juce::String (channel + 1) ? juce::String() : name);
            return lines;
        };
        lcd.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (selectKey);
        addAndMakeVisible (bypassKey);
        addAndMakeVisible (lcd);
        for (auto& k : knobs)
            addAndMakeVisible (k);
    }

    Strip* StripUnit::strip() const
    {
        const auto& list = ctx.engine.getChannels();
        return juce::isPositiveAndBelow (channel, (int) list.size()) ? list[(size_t) channel].get() : nullptr;
    }

    void StripUnit::setChannel (int index)
    {
        channel = index;
        refresh();
        repaint();
    }

    void StripUnit::refresh()
    {
        selectKey.refresh();
        bypassKey.refresh();
        lcd.refresh();
        auto* s = strip();
        float in = 0.0f, gr = 0.0f;
        bool available = false;
        if (s != nullptr)
        {
            in = juce::jmax (ctx.meters.get (s, MeterPoint::Input, 0), ctx.meters.get (s, MeterPoint::Input, 1));
            // The chain's gain reduction: the dynamics blocks'. A plug-in's own is not exposed by the engine.
            for (const auto& step : s->getChain())
                if (step.isBlock() && step.getBlockKind() == BlockKind::Dynamics)
                {
                    const auto block = s->getBlock (step.uid);
                    if (auto* m = block.getMeters())
                    {
                        available = true;
                        gr = juce::jmax (gr, m->reductionDb.load());
                    }
                }
        }
        const bool selected = channel >= 0 && owner.getSelectedChannel() == channel;
        const bool bypassed = s != nullptr && s->params.input.bypass.load();
        const auto db = in > 1.0e-6f ? 20.0f * std::log10 (in) : -120.0f;
        const float level = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        const bool changed = std::abs (level - inLevel) > 0.01f || std::abs (gr - grDb) > 0.1f || available != grAvailable || selected != selectedNow || bypassed != bypassedNow;
        inLevel = level;
        grDb = gr;
        grAvailable = available;
        selectedNow = selected;
        bypassedNow = bypassed;
        if (changed)
            repaint();
    }

    void StripUnit::resized()
    {
        selectKey.setBounds (8, 8, kWidth - 16, 48);
        lcd.setBounds (8, 62, kWidth - 16, 60);
        bypassKey.setBounds (8, 230, kWidth - 16, 48);
        knobs[0].setBounds (6, 284, 72, 64);
        knobs[1].setBounds (kWidth - 78, 284, 72, 64);
    }

    void StripUnit::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (kPlateTop, 0.0f, 0.0f, kPlateBottom, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (selectedNow ? theme.accent : kEdge);
        g.drawRoundedRectangle (r.reduced (selectedNow ? 1.0f : 0.5f), 8.0f, selectedNow ? 2.0f : 1.0f);
        if (channel < 0)
            return;

        const auto& l = look();
        // The meters: the input's and the chain's gain reduction (dim when the chain has none to show).
        const float top = 130.0f, height = 76.0f;
        drawMeter (g, { 36.0f, top, 22.0f, height }, inLevel, false, theme, bypassedNow);
        drawMeter (g, { (float) kWidth - 58.0f, top, 22.0f, height }, juce::jlimit (0.0f, 1.0f, grDb / 20.0f), true, theme, ! grAvailable);
        hw::silk (g, { 18.0f, top + height + 6.0f, 60.0f, 16.0f }, "IN", l, 12.0f, juce::Justification::centred, false);
        hw::silk (g, { (float) kWidth - 78.0f, top + height + 6.0f, 60.0f, 16.0f }, "GR", l, 12.0f, juce::Justification::centred, ! grAvailable);
        hw::silk (g, { 66.0f, top + height * 0.5f - 8.0f, (float) kWidth - 132.0f, 16.0f }, "dB", l, 11.0f, juce::Justification::centred, true);
    }
} // namespace ph

//==============================================================================

/** What a page that is not built yet shows. */
class PluginHostSurface::ComingSoon final : public juce::Component
{
public:
    explicit ComingSoon (const Theme& t) : theme (t) {}

    void setPage (Page p)
    {
        page = p;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme.background);
        static const std::pair<Page, const char*> notes[] = {
            { Page::Overview, "Every channel as a column of small tiles: a glance at all racks, tap to jump." },
            { Page::Control, "MIDI learn for plug-in parameters, and what the hot knobs control." },
            { Page::Browser, "Search, favourites, tags and recent plug-ins, and the scan report." },
            { Page::System, "Audio device and buffer, latency alignment, the CPU guard and the benchmark." }
        };
        juce::String note;
        for (const auto& [p, text] : notes)
            if (p == page)
                note = text;
        auto area = getLocalBounds().reduced (60, 0).withSizeKeepingCentre (getWidth() - 120, 200);
        g.setColour (theme.text);
        g.setFont (type::font (28.0f, true));
        g.drawText (juce::String (nameOf (page)) + "  -  COMING SOON", area.removeFromTop (48), juce::Justification::centred);
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kValue));
        g.drawFittedText (note, area.removeFromTop (60), juce::Justification::centredTop, 2);
        g.setColour (theme.accent);
        g.setFont (type::font (type::kLabel, true));
        g.drawText ("The RACK, ROUTING and SCENES pages are ready.", area.removeFromTop (30), juce::Justification::centred);
    }

private:
    const Theme& theme;
    Page page = Page::Overview;
};

//==============================================================================

PluginHostSurface::PluginHostSurface (UiContext& c)
    : ctx (c), theme (Theme::of (Theme::Look::ConsolePluginHost)), universalKey ("UNIVERSAL MIXER", theme.accent, theme),
      bankPrev ("", theme.accent, ph::Key::Glyph::Left), bankNext ("", theme.accent, ph::Key::Glyph::Right), scenePrev ("", theme.accent, ph::Key::Glyph::Left),
      sceneGoKey ("GO", theme.accent), sceneNext ("", theme.accent, ph::Key::Glyph::Right)
{
    setOpaque (true);
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

    // The touchscreen: the page tabs over the page.
    addAndMakeVisible (screenHost);
    for (int i = 0; i < 7; ++i)
    {
        const auto p = static_cast<Page> (i);
        auto key = std::make_unique<ToggleKey> (nameOf (p), theme.accent, theme);
        key->tab = true;
        key->isOn = [this, p] { return page == p; };
        key->onClick = [this, p] { showPage (p); };
        screenHost.addAndMakeVisible (*key);
        tabs[(size_t) i] = std::move (key);
    }
    rack = std::make_unique<ph::RackPage> (c, theme);
    screenHost.addAndMakeVisible (*rack);
    routing = std::make_unique<ph::RoutingPage> (c, theme);
    screenHost.addChildComponent (*routing);
    scenes = std::make_unique<ph::ScenesPage> (c, theme);
    scenes->getCue = [this] { return cue; };
    scenes->setCue = [this] (int index) { setCue (index); };
    scenes->go = [this] { sceneGo(); };
    screenHost.addChildComponent (*scenes);
    comingSoon = std::make_unique<ComingSoon> (theme);
    screenHost.addChildComponent (*comingSoon);

    for (int i = 0; i < kStrips; ++i)
    {
        strips[(size_t) i] = std::make_unique<ph::StripUnit> (*this, c, theme, i);
        addAndMakeVisible (*strips[(size_t) i]);
    }

    // Bank.
    for (auto* k : { &bankPrev, &bankNext, &scenePrev, &sceneGoKey, &sceneNext })
        addAndMakeVisible (*k);
    addAndMakeVisible (bankLcd);
    addAndMakeVisible (sceneLcd);
    addAndMakeVisible (readout);
    addAndMakeVisible (wheel);
    bankPrev.onClick = [this] { stepBank (-1); };
    bankNext.onClick = [this] { stepBank (1); };
    bankPrev.setTooltip ("The previous bank of eight channels");
    bankNext.setTooltip ("The next bank of eight channels");
    bankLcd.accent = theme.accent;
    bankLcd.firstSize = 18.0f;
    bankLcd.otherSize = 13.0f;
    bankLcd.getLines = [this]
    {
        return juce::StringArray { "BANK " + juce::String (bank + 1) + " / " + juce::String (getNumBanks()),
                                   "CH " + juce::String (bank * kStrips + 1) + " - " + juce::String (bank * kStrips + kStrips) };
    };
    bankLcd.setInterceptsMouseClicks (false, false);

    // Scenes: PREV and NEXT cue, GO recalls the cue.
    scenePrev.onClick = [this] { sceneStep (-1); };
    sceneNext.onClick = [this] { sceneStep (1); };
    sceneGoKey.onClick = [this] { sceneGo(); };
    scenePrev.setTooltip ("Cue the previous scene");
    sceneNext.setTooltip ("Cue the next scene");
    sceneGoKey.setTooltip ("Recall the cued scene");
    sceneGoKey.isOn = [this] { return cue >= 0 && cue == ctx.show.getCurrentSceneIndex(); };
    sceneLcd.accent = theme.accent;
    sceneLcd.firstSize = 15.0f;
    sceneLcd.otherSize = 18.0f;
    sceneLcd.getLines = [this]
    {
        const int n = ctx.show.getNumScenes();
        if (n == 0 || cue < 0)
            return juce::StringArray { "SCENES", "NONE STORED" };
        const auto scene = ctx.show.getScene (cue);
        return juce::StringArray { "CUE " + juce::String (cue + 1) + " / " + juce::String (n) + (cue == ctx.show.getCurrentSceneIndex() ? "   ACTIVE" : juce::String()),
                                   scene ? scene->name : juce::String() };
    };
    sceneLcd.setInterceptsMouseClicks (false, false);

    readout.accent = theme.accent;
    readout.firstSize = 14.0f;
    readout.otherSize = 22.0f;
    readout.getLines = [this]
    {
        const int ch = getSelectedChannel();
        auto* s = channelStrip (ch);
        const int samples = s != nullptr ? s->getLatency() : 0;
        const double rate = ctx.engine.getSampleRate();
        const auto ms = rate > 0.0 ? juce::String (1000.0 * samples / rate, 1) + " ms" : juce::String ("- ms");
        return juce::StringArray { "LATENCY  CH " + juce::String (ch + 1), juce::String (samples) + " smp   " + ms,
                                   "ENGINE CPU  " + juce::String (juce::roundToInt (ctx.deviceManager.getCpuUsage() * 100.0)) + " %" };
    };
    readout.setInterceptsMouseClicks (false, false);

    // The data wheel moves the selection through the channels.
    wheel.onTurn = [this] (int steps) { selectChannel (getSelectedChannel() + steps); };
    wheel.setTooltip ("Data wheel: turns the selection through the channels");

    refreshAll();
    startTimerHz (30);
}

PluginHostSurface::~PluginHostSurface()
{
    stopTimer();
}

juce::Component* PluginHostSurface::getComingSoon() noexcept
{
    return comingSoon.get();
}

const char* PluginHostSurface::nameOf (Page p)
{
    static const char* names[] = { "RACK", "OVERVIEW", "ROUTING", "SCENES", "CONTROL", "BROWSER", "SYSTEM" };
    return names[(int) p];
}

Strip* PluginHostSurface::channelStrip (int index) const
{
    const auto& list = ctx.engine.getChannels();
    return juce::isPositiveAndBelow (index, (int) list.size()) ? list[(size_t) index].get() : nullptr;
}

int PluginHostSurface::getNumBanks() const noexcept
{
    return juce::jmax (1, ((int) ctx.engine.getChannels().size() + kStrips - 1) / kStrips);
}

void PluginHostSurface::setBank (int b)
{
    b = juce::jlimit (0, getNumBanks() - 1, b);
    bank = b;
    bindStrips();
    bankLcd.refresh();
}

void PluginHostSurface::stepBank (int delta)
{
    const int banks = getNumBanks();
    setBank ((bank + delta + banks) % banks); // round the banks
}

void PluginHostSurface::bindStrips()
{
    const int channels = (int) ctx.engine.getChannels().size();
    for (int i = 0; i < kStrips; ++i)
    {
        const int ch = bank * kStrips + i;
        strips[(size_t) i]->setChannel (ch < channels ? ch : -1);
    }
}

int PluginHostSurface::getSelectedChannel() const
{
    const auto* selected = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    const auto& list = ctx.engine.getChannels();
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].get() == selected)
            return i;
    return 0;
}

void PluginHostSurface::selectChannel (int index)
{
    const int channels = (int) ctx.engine.getChannels().size();
    if (channels == 0)
        return;
    index = juce::jlimit (0, channels - 1, index);
    if (ctx.selectStrip)
        ctx.selectStrip (channelStrip (index));
    selectionChanged(); // the window tells the skin too; this keeps it right where it doesn't
}

void PluginHostSurface::selectionChanged()
{
    if (selecting)
        return;
    const juce::ScopedValueSetter<bool> busy (selecting, true);
    const int ch = getSelectedChannel();
    // The wheel (or the window) moved the selection out of the bank: the bank follows it.
    if (ch / kStrips != bank)
        setBank (ch / kStrips);
    if (rack->getChannel() != ch)
        rack->setChannel (ch);
    routing->setChannel (ch);
    for (auto& s : strips)
        s->refresh();
    updateReadouts();
}

void PluginHostSurface::dropPluginEditors (juce::AudioProcessor* p)
{
    rack->getDock().dropEditorFor (p);
}

void PluginHostSurface::showChanged()
{
    shownScenes = -1; // the timer reads them again
}

void PluginHostSurface::sceneStep (int delta)
{
    const int n = ctx.show.getNumScenes();
    if (n == 0)
        return;
    const int from = cue >= 0 ? cue : ctx.show.getCurrentSceneIndex();
    setCue (from + delta);
}

void PluginHostSurface::setCue (int index)
{
    const int n = ctx.show.getNumScenes();
    cue = n == 0 ? -1 : juce::jlimit (0, n - 1, index);
    sceneLcd.refresh();
    sceneGoKey.refresh();
    if (scenes != nullptr)
        scenes->refresh();
}

void PluginHostSurface::sceneGo()
{
    const int n = ctx.show.getNumScenes();
    if (n == 0 || cue < 0 || cue >= n)
        return;
    const auto result = ctx.show.recallScene (cue);
    logRecall (ctx, "scene " + juce::String (cue + 1), result);
    refreshAll();
}

void PluginHostSurface::refreshAll()
{
    const int channels = (int) ctx.engine.getChannels().size();
    // The first channel is selected when nothing (or a strip of another console) is.
    if (! selecting && channels > 0 && ctx.selectStrip)
    {
        const auto* selected = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
        bool isChannel = false;
        for (const auto& s : ctx.engine.getChannels())
            isChannel = isChannel || s.get() == selected;
        if (! isChannel)
        {
            const juce::ScopedValueSetter<bool> busy (selecting, true);
            ctx.selectStrip (channelStrip (0));
        }
    }
    bank = juce::jlimit (0, getNumBanks() - 1, bank);
    bindStrips();
    const juce::ScopedValueSetter<bool> busy (selecting, true);
    rack->setChannel (getSelectedChannel());
    rack->refresh();
    routing->setChannel (getSelectedChannel());
    routing->refresh();
    shownScenes = ctx.show.getNumScenes();
    if (cue < 0 || cue >= shownScenes)
        cue = juce::jmax (-1, ctx.show.getCurrentSceneIndex() >= 0 ? ctx.show.getCurrentSceneIndex() : (shownScenes > 0 ? 0 : -1));
    for (auto& s : strips)
        s->refresh();
    bankLcd.refresh();
    sceneLcd.refresh();
    sceneGoKey.refresh();
    scenes->refresh();
    updateReadouts();
    repaint();
}

void PluginHostSurface::showPage (Page p)
{
    page = p;
    rack->setVisible (p == Page::Rack);
    routing->setVisible (p == Page::Routing);
    if (p == Page::Routing)
        routing->refresh();
    scenes->setVisible (p == Page::Scenes);
    if (p == Page::Scenes)
        scenes->refresh();
    comingSoon->setVisible (p != Page::Rack && p != Page::Routing && p != Page::Scenes);
    comingSoon->setPage (p);
    for (auto& t : tabs)
        t->refresh();
    repaint();
}

void PluginHostSurface::updateReadouts()
{
    readout.refresh();
    sceneLcd.refresh();
    bankLcd.refresh();
}

void PluginHostSurface::timerCallback()
{
    if (ctx.show.getNumScenes() != shownScenes)
    {
        shownScenes = ctx.show.getNumScenes();
        cue = shownScenes > 0 ? juce::jlimit (0, shownScenes - 1, cue < 0 ? 0 : cue) : -1;
    }
    for (auto& s : strips)
        s->refresh();
    sceneGoKey.refresh();
    updateReadouts();
}

void PluginHostSurface::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff090a0c));
    g.addTransform (design);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff15181c), 0.0f, 0.0f, juce::Colour (0xff0d0f12), 0.0f, (float) kDesignHeight, false));
    g.fillRect (0, 0, kDesignWidth, kDesignHeight);

    // The top bar.
    g.setColour (juce::Colour (0xff1a1d22));
    g.fillRect (0, 0, kDesignWidth, kTopBar);
    g.setColour (kEdge);
    g.drawHorizontalLine (kTopBar - 1, 0.0f, (float) kDesignWidth);
    g.setColour (theme.text);
    g.setFont (type::font (type::kTitle, true));
    g.drawText ("PLUGIN HOST", juce::Rectangle<int> (kScreenX, 0, 300, kTopBar), juce::Justification::centredLeft);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption));
    g.drawText (juce::String (ctx.engine.getChannels().size()) + " channels   -   each input through its plug-ins to its output",
                juce::Rectangle<int> (kScreenX + 190, 0, 700, kTopBar), juce::Justification::centredLeft);

    // The screen's bezel.
    const juce::Rectangle<float> bezel ((float) kScreenX - 10.0f, (float) kScreenY - 8.0f, (float) kScreenW + 20.0f, (float) kScreenH + 16.0f);
    g.setColour (kScreenBezel);
    g.fillRoundedRectangle (bezel, 10.0f);
    g.setColour (kEdge);
    g.drawRoundedRectangle (bezel.reduced (0.5f), 10.0f, 1.0f);

    // The right section: bank, scenes, wheel, readout.
    const juce::Rectangle<float> plate (1410.0f, 716.0f, 470.0f, 356.0f);
    g.setGradientFill (juce::ColourGradient (kPlateTop, 0.0f, plate.getY(), kPlateBottom, 0.0f, plate.getBottom(), false));
    g.fillRoundedRectangle (plate, 8.0f);
    g.setColour (kEdge);
    g.drawRoundedRectangle (plate.reduced (0.5f), 8.0f, 1.0f);
    const auto& l = ph::look();
    hw::silk (g, { 1420.0f, 722.0f, 120.0f, 18.0f }, "BANK", l, 12.0f, juce::Justification::centredLeft, true);
    hw::silk (g, { 1420.0f, 800.0f, 120.0f, 18.0f }, "SCENE", l, 12.0f, juce::Justification::centredLeft, true);
    hw::silk (g, { 1716.0f, 898.0f, 164.0f, 18.0f }, "DATA", l, 12.0f, juce::Justification::centred, true);
    hw::silk (g, { 1420.0f, 938.0f, 200.0f, 18.0f }, "SELECTED CHANNEL / ENGINE", l, 12.0f, juce::Justification::centredLeft, true);
}

void PluginHostSurface::resized()
{
    const float s = juce::jmin ((float) getWidth() / (float) kDesignWidth, (float) getHeight() / (float) kDesignHeight);
    design = s > 0.0f ? juce::AffineTransform::scale (s).translated (((float) getWidth() - (float) kDesignWidth * s) * 0.5f, ((float) getHeight() - (float) kDesignHeight * s) * 0.5f)
                      : juce::AffineTransform();
    for (auto* c : getChildren())
        c->setTransform (design);

    universalKey.setBounds (kDesignWidth - kScreenX - 200, 6, 200, 44);

    // The screen: its tabs across the top, the page under them.
    screenHost.setBounds (kScreenX, kScreenY, kScreenW, kScreenH);
    for (int i = 0; i < 7; ++i)
        tabs[(size_t) i]->setBounds (i * 150, 0, 150, kTabHeight);
    const juce::Rectangle<int> content (0, kTabHeight, kScreenW, kScreenH - kTabHeight);
    rack->setBounds (content);
    routing->setBounds (content);
    scenes->setBounds (content);
    comingSoon->setBounds (content);

    for (int i = 0; i < kStrips; ++i)
        strips[(size_t) i]->setBounds (kScreenX + i * (ph::StripUnit::kWidth + 6), 720, ph::StripUnit::kWidth, ph::StripUnit::kHeight);

    bankPrev.setBounds (1420, 744, 72, 48);
    bankLcd.setBounds (1500, 744, 116, 48);
    bankNext.setBounds (1624, 744, 72, 48);
    sceneLcd.setBounds (1420, 822, 276, 54);
    scenePrev.setBounds (1420, 884, 84, 52);
    sceneGoKey.setBounds (1512, 884, 96, 52);
    sceneNext.setBounds (1616, 884, 80, 52);
    wheel.setBounds (1716, 728, 164, 164);
    readout.setBounds (1420, 958, 450, 106);
    maximiser.layout();
}

} // namespace livedaw::ui
