#include "ui/PluginOverviewWindow.h"
#include "ui/LookAndFeel.h"
#include "ui/PluginPictures.h"
#include "ui/WingWidgets.h"

namespace livedaw::ui
{

namespace
{
    constexpr const char* stateKey = "pluginOverview.window"; // getWindowStateAsString: place, size, screen
    constexpr const char* onTopKey = "pluginOverview.onTop";
    constexpr const char* zoomKey = "pluginOverview.zoom";     // text size, percent
    constexpr const char* layoutKey = "pluginOverview.layout"; // "cards" or "list"
    constexpr int kMinZoom = 75, kMaxZoom = 250, kZoomStep = 25;
    constexpr int kPollHz = 15;

    /** A slot's state as the overview shows it, worst first. */
    enum class SlotState
    {
        faulted,     // the slot auto-bypassed (its plugin threw, output NaN/inf or missed its block)
        doubleUsed,  // two inserts use it: the second passes its signal through
        memberFault, // a plugin of its chain faulted on its own (passed through)
        bypassed,    // the slot's BYPASS
        unused,      // no insert uses it
        running
    };

    juce::Colour colourOf (SlotState s)
    {
        switch (s)
        {
            case SlotState::faulted:
            case SlotState::doubleUsed:  return Colours::tapRed;
            case SlotState::memberFault: return Colours::amber;
            case SlotState::bypassed:
            case SlotState::unused:      return Colours::chipOff;
            case SlotState::running:     break;
        }
        return Colours::green;
    }

    juce::String captionOf (SlotState s)
    {
        switch (s)
        {
            case SlotState::faulted:     return "FAULT";
            case SlotState::doubleUsed:  return "DOUBLE";
            case SlotState::memberFault: return "PLUGIN FAULT";
            case SlotState::bypassed:    return "BYPASS";
            case SlotState::unused:      return "NOT USED";
            case SlotState::running:     break;
        }
        return "RUNNING";
    }

    struct Member
    {
        const juce::AudioProcessor* plugin = nullptr;
        juce::String name;
        bool bypassed = false, faulted = false;
    };

    struct User
    {
        juce::String label;
        juce::Colour colour;
    };

    /** What the overview shows of an FX slot holding plugins. */
    struct Slot
    {
        int slot = 0;
        SlotState state = SlotState::running;
        std::vector<Member> members;
        std::vector<User> users;
        bool sandboxed = false;
        float load = 0.0f; // share of the block time
        int latency = 0;   // samples
        float levels[ld::kFxLevels] {}; // in L, in R, out L, out R (linear)
    };

    float toDb (float linear) { return juce::Decibels::gainToDecibels (linear, -60.0f); }

    juce::Colour meterColour (float db)
    {
        return db > -0.5f ? Colours::tapRed : (db > -9.0f ? Colours::amber : Colours::green);
    }

    /** "12 S", "4 MIN", "2 H": how old a picture is. */
    juce::String ageOf (juce::Time taken)
    {
        const auto s = juce::jmax ((juce::int64) 0, (juce::int64) (juce::Time::getCurrentTime() - taken).inSeconds());
        if (s < 60)
            return juce::String (s) + " S";
        if (s < 3600)
            return juce::String (s / 60) + " MIN";
        return juce::String (s / 3600) + " H";
    }
} // namespace

//==============================================================================
class PluginOverviewWindow::Content final : public juce::Component, private juce::Timer
{
public:
    explicit Content (UiContext& c) : ctx (c)
    {
        zoom = juce::jlimit (kMinZoom, kMaxZoom, ctx.settings.getIntValue (zoomKey, 100));
        layout = ctx.settings.getValue (layoutKey, "cards") == "list" ? Layout::list : Layout::cards;

        capture.setTooltip ("Opens each FX slot's plugin window in turn to picture its plugins (the windows it opens close again)");
        capture.onClick = [this] { captureAll(); };
        cardsKey.isOn = [this] { return layout == Layout::cards; };
        listKey.isOn = [this] { return layout == Layout::list; };
        cardsKey.setTooltip ("A card per slot with its plugins' pictures");
        listKey.setTooltip ("A compact row per slot");
        cardsKey.onClick = [this] { setLayout (Layout::cards); };
        listKey.onClick = [this] { setLayout (Layout::list); };
        onTop.isOn = [this] { auto* w = findParentComponentOfClass<juce::DocumentWindow>(); return w != nullptr && w->isAlwaysOnTop(); };
        onTop.setTooltip ("Keep the overview above other windows");
        onTop.onClick = [this]
        {
            if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            {
                w->setAlwaysOnTop (! w->isAlwaysOnTop());
                ctx.settings.setValue (onTopKey, w->isAlwaysOnTop());
            }
            onTop.repaint();
        };
        smaller.setTooltip ("Smaller");
        larger.setTooltip ("Larger (to read it from further away)");
        smaller.onClick = [this] { setZoom (zoom - kZoomStep); };
        larger.onClick = [this] { setZoom (zoom + kZoomStep); };
        for (auto* k : { &capture, &cardsKey, &listKey, &onTop, &smaller, &larger })
            addAndMakeVisible (k);

        view.setViewedComponent (&body, false);
        view.setScrollBarsShown (true, false);
        view.setScrollBarThickness (10);
        addAndMakeVisible (view);
        poll();
        startTimerHz (kPollHz);
    }

    ~Content() override { view.setViewedComponent (nullptr, false); }

    Layout getLayout() const noexcept { return layout; }

    void setLayout (Layout l)
    {
        layout = l;
        ctx.settings.setValue (layoutKey, l == Layout::list ? "list" : "cards");
        for (auto* k : { &cardsKey, &listKey })
            k->repaint();
        layoutBody();
        body.repaint();
    }

    std::vector<int> getShownSlots() const
    {
        std::vector<int> out;
        for (auto& s : slots)
            out.push_back (s.slot);
        return out;
    }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Colours::screen);
        g.setColour (Colours::screenHeader);
        g.fillRect (header);
        g.setColour (Colours::screenLine);
        g.fillRect (header.getX(), header.getBottom() - 1, header.getWidth(), 1);
        paintSummary (g);
        paintStatus (g);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        header = r.removeFromTop (px (52.0f));
        status = r.removeFromBottom (px (24.0f));
        view.setBounds (r);

        auto keys = header.reduced (px (10.0f), px (10.0f));
        const int h = keys.getHeight();
        larger.setBounds (keys.removeFromRight (h + px (6.0f)));
        keys.removeFromRight (px (4.0f));
        smaller.setBounds (keys.removeFromRight (h + px (6.0f)));
        keys.removeFromRight (px (12.0f));
        onTop.setBounds (keys.removeFromRight (px (84.0f)));
        keys.removeFromRight (px (12.0f));
        listKey.setBounds (keys.removeFromRight (px (64.0f)));
        cardsKey.setBounds (keys.removeFromRight (px (70.0f)));
        keys.removeFromRight (px (12.0f));
        capture.setBounds (keys.removeFromRight (px (124.0f)));
        summaryArea = keys.withTrimmedRight (px (12.0f)).withLeft (header.getX() + px (14.0f));
        layoutBody();
    }

private:
    //==============================================================================
    /** The slots, in the viewport: cards in a grid, or rows. */
    class Body final : public juce::Component
    {
    public:
        explicit Body (Content& c) : owner (c) {}

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Colours::screen);
            if (owner.slots.empty())
            {
                owner.paintEmpty (g, owner.view.getViewArea().withPosition (0, 0));
                return;
            }
            for (size_t i = 0; i < owner.slots.size(); ++i)
            {
                const auto area = owner.areaOf ((int) i);
                if (! g.clipRegionIntersects (area))
                    continue;
                if (owner.layout == Layout::cards)
                    owner.paintCard (g, owner.slots[i], area, (int) i == hover);
                else
                    owner.paintRow (g, owner.slots[i], area, (int) i == hover);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override { setHover (owner.indexAt (e.getPosition())); }
        void mouseExit (const juce::MouseEvent&) override { setHover (-1); }

        void mouseUp (const juce::MouseEvent& e) override
        {
            // Touching a slot opens its plugin window.
            const int i = owner.indexAt (e.getPosition());
            if (e.mouseWasClicked() && i >= 0 && owner.ctx.openPluginEditor)
                owner.ctx.openPluginEditor (owner.slots[(size_t) i].slot);
        }

    private:
        void setHover (int i)
        {
            if (i == hover)
                return;
            if (juce::isPositiveAndBelow (hover, (int) owner.slots.size()))
                repaint (owner.areaOf (hover));
            hover = i;
            setMouseCursor (i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            if (i >= 0)
                repaint (owner.areaOf (i));
        }

        Content& owner;
        int hover = -1;
    };

    //==============================================================================
    void timerCallback() override { poll(); }

    /** Reads the slots; repaints when what's shown changed (the meters to the dB). */
    void poll()
    {
        auto& fx = ctx.engine.getFx();
        std::vector<Slot> now;
        for (int slot = 0; slot < kMaxFx; ++slot)
        {
            if (! fx.hasPlugin (slot))
                continue;
            Slot s;
            s.slot = slot;
            for (int i = 0; i < fx.getNumChainMembers (slot); ++i)
                if (auto* p = fx.getChainMember (slot, i))
                    s.members.push_back ({ p, p->getName(), fx.isChainMemberBypassed (slot, i), fx.isChainMemberFaulted (slot, i) });
            for (auto kind : { StripKind::Channel, StripKind::Aux, StripKind::Bus, StripKind::Main, StripKind::Matrix })
                for (auto& st : ctx.engine.getStrips (kind))
                {
                    auto& p = st->params;
                    const bool pre = p.preInsert().on.load() && p.preInsert().fx.load() == slot;
                    const bool post = p.postInsert().on.load() && p.postInsert().fx.load() == slot && p.postInsert().mode.load() == 0;
                    if (! pre && ! post)
                        continue;
                    auto label = st->getLabel(); // CH 1, BUS 3 ...
                    if (const auto name = st->getName();
                        name.isNotEmpty() && ! name.removeCharacters (" ").equalsIgnoreCase (label.removeCharacters (" ")))
                        label << "  " << name;
                    s.users.push_back ({ label.toUpperCase() + (post && ! pre ? "  POST" : ""), stripDisplayColour (*st) });
                }
            s.sandboxed = fx.isSandboxed (slot);
            s.load = fx.getLoad (slot);
            s.latency = fx.getLatency (slot);
            fx.getReadouts (slot, s.levels, ld::kFxLevels);
            if (fx.isFaulted (slot))
                s.state = SlotState::faulted;
            else if (fx.isDoubleAssigned (slot))
                s.state = SlotState::doubleUsed;
            else if (fx.getNumFaultedChainMembers (slot) > 0)
                s.state = SlotState::memberFault;
            else if (fx.params (slot).bypass.load())
                s.state = SlotState::bypassed;
            else if (s.users.empty())
                s.state = SlotState::unused;
            now.push_back (std::move (s));
        }

        juce::String sig;
        sig << juce::roundToInt (ctx.engine.getDspLoad() * 100.0f) << '|' << ctx.engine.getTotalCompensation() << '|'
            << (int) ctx.engine.isLatencyCompensationOn() << '|' << (ctx.pluginPictures != nullptr ? ctx.pluginPictures->getGeneration() : 0) << '|'
            << (int) (juce::Time::getCurrentTime().toMilliseconds() / 10000); // the pictures' ages
        for (auto& s : now)
        {
            sig << '#' << s.slot << '|' << (int) s.state << '|' << (int) s.sandboxed << '|' << juce::roundToInt (s.load * 1000.0f) << '|' << s.latency;
            for (auto& m : s.members)
                sig << '|' << (juce::pointer_sized_int) m.plugin << m.name << (int) m.bypassed << (int) m.faulted;
            for (auto& u : s.users)
                sig << '|' << u.label << (int) u.colour.getARGB();
            for (float l : s.levels)
                sig << '|' << juce::roundToInt (toDb (l));
        }
        if (sig == shown)
            return;
        shown = sig;
        const bool relayout = now.size() != slots.size();
        slots = std::move (now);
        if (relayout)
            layoutBody();
        repaint (header);
        repaint (status);
        body.repaint();
    }

    //==============================================================================
    void captureAll()
    {
        if (capturing || ! ctx.capturePluginPictures)
            return;
        capturing = true;
        capture.setEnabled (false);
        captureDone = 0;
        captureTotal = (int) slots.size();
        capture.repaint();
        repaint (status);
        ctx.capturePluginPictures ([safe = juce::Component::SafePointer<Content> (this)] (int done, int total)
        {
            if (safe == nullptr)
                return;
            safe->captureDone = done;
            safe->captureTotal = total;
            safe->capturing = done < total;
            safe->capture.setEnabled (! safe->capturing);
            safe->capture.repaint();
            safe->repaint (safe->status);
            // Back in front once the plugin windows are closed.
            if (! safe->capturing)
                if (auto* w = safe->findParentComponentOfClass<juce::DocumentWindow>())
                    w->toFront (false);
        });
    }

    void setZoom (int z)
    {
        zoom = juce::jlimit (kMinZoom, kMaxZoom, z);
        ctx.settings.setValue (zoomKey, zoom);
        resized();
        repaint();
    }

    float fpx (float wingPx) const { return wingPx * (float) zoom / 100.0f; }
    int px (float wingPx) const { return juce::roundToInt (fpx (wingPx)); }

    //==============================================================================
    // Layout of the body.

    int gap() const { return px (10.0f); }
    int columns() const { return juce::jmax (1, (body.getWidth() - gap()) / (px (360.0f) + gap())); }
    int cardWidth() const { return (body.getWidth() - gap() * (columns() + 1)) / columns(); }
    int pictureHeight() const { return juce::roundToInt ((float) cardWidth() * 0.5f); }
    int cardHeight() const { return px (34.0f) + px (22.0f) + pictureHeight() + px (44.0f); }
    int rowHeight() const { return px (68.0f); }

    juce::Rectangle<int> areaOf (int i) const
    {
        if (layout == Layout::list)
            return { 0, gap() / 2 + i * rowHeight(), body.getWidth(), rowHeight() };
        const int col = i % columns(), row = i / columns();
        return { gap() + col * (cardWidth() + gap()), gap() + row * (cardHeight() + gap()), cardWidth(), cardHeight() };
    }

    int indexAt (juce::Point<int> p) const
    {
        for (int i = 0; i < (int) slots.size(); ++i)
            if (areaOf (i).contains (p))
                return i;
        return -1;
    }

    void layoutBody()
    {
        const int w = juce::jmax (1, view.getMaximumVisibleWidth());
        body.setSize (w, 1); // the width first: the grid depends on it
        const int n = (int) slots.size();
        int h = 0;
        if (n > 0)
            h = layout == Layout::list ? gap() + n * rowHeight() : gap() + ((n + columns() - 1) / columns()) * (cardHeight() + gap());
        body.setSize (w, juce::jmax (view.getHeight(), h));
    }

    //==============================================================================
    // Painting.

    void paintSummary (juce::Graphics& g) const
    {
        auto h = summaryArea;
        g.setColour (Colours::caption);
        g.setFont (wingFontPx (fpx (14.0f), WingWeight::SemiBold));
        const juce::String title ("PLUGIN OVERVIEW");
        g.drawText (title, h, juce::Justification::centredLeft, false);
        h.removeFromLeft ((int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title) + px (22.0f));

        int plugins = 0, faults = 0;
        for (auto& s : slots)
        {
            plugins += (int) s.members.size();
            faults += s.state == SlotState::faulted || s.state == SlotState::doubleUsed ? 1 : 0;
            for (auto& m : s.members)
                faults += m.faulted ? 1 : 0;
        }
        g.setFont (wingFontPx (fpx (10.5f)));
        auto chip = [&g, &h, this] (const juce::String& text, juce::Colour fill, juce::Colour ink)
        {
            const int w = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + px (18.0f);
            if (w > h.getWidth())
                return;
            const auto r = h.removeFromLeft (w).withSizeKeepingCentre (w, px (24.0f)).toFloat();
            h.removeFromLeft (px (8.0f));
            g.setColour (fill);
            g.fillRoundedRectangle (r, fpx (3.0f));
            g.setColour (ink);
            g.drawText (text, r, juce::Justification::centred, false);
        };
        const auto dim = Colours::screenTile;
        chip (juce::String (plugins) + (plugins == 1 ? " PLUGIN" : " PLUGINS") + "  /  " + juce::String ((int) slots.size())
                  + (slots.size() == 1 ? " SLOT" : " SLOTS"),
              dim, Colours::text);
        if (faults == 0)
            chip ("NO FAULTS", Colours::green, juce::Colours::black);
        else
            chip (juce::String (faults) + (faults == 1 ? " FAULT" : " FAULTS"), Colours::tapRed, juce::Colours::black);
        const float dsp = ctx.engine.getDspLoad();
        chip ("DSP " + juce::String (juce::roundToInt (dsp * 100.0f)) + " %", dsp > 0.8f ? Colours::tapRed : (dsp > 0.6f ? Colours::amber : dim),
              dsp > 0.6f ? juce::Colours::black : Colours::text);
        chip (ctx.engine.isLatencyCompensationOn() ? "PDC " + juce::String (ctx.engine.getTotalCompensation()) + " SMP" : juce::String ("PDC OFF"), dim,
              Colours::caption);
    }

    void paintStatus (juce::Graphics& g) const
    {
        g.setColour (Colours::screenHeader);
        g.fillRect (status);
        g.setColour (Colours::screenLine);
        g.fillRect (status.getX(), status.getY(), status.getWidth(), 1);
        auto r = status.reduced (px (14.0f), 0);
        g.setFont (wingFontPx (fpx (8.5f)));
        if (capturing)
        {
            g.setColour (Colours::gold);
            g.drawText ("CAPTURING PICTURES  " + juce::String (captureDone) + " / " + juce::String (captureTotal) + "  (THE PLUGIN WINDOWS OPEN IN TURN)", r,
                        juce::Justification::centredLeft, false);
            return;
        }
        int plugins = 0, pictured = 0;
        for (auto& s : slots)
            for (auto& m : s.members)
            {
                ++plugins;
                pictured += picture (m) != nullptr ? 1 : 0;
            }
        g.setColour (Colours::textDim);
        if (plugins > 0)
            g.drawText ("PICTURES " + juce::String (pictured) + " OF " + juce::String (plugins)
                            + (pictured < plugins ? "  -  CAPTURE ALL (OR OPENING A PLUGIN WINDOW) TAKES THE MISSING ONES" : ""),
                        r, juce::Justification::centredLeft, false);
        g.drawText ("TOUCH A SLOT TO OPEN ITS PLUGIN WINDOW", r, juce::Justification::centredRight, false);
    }

    void paintEmpty (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        auto r = area.withSizeKeepingCentre (juce::jmin (area.getWidth() - px (40.0f), px (520.0f)), px (90.0f));
        g.setColour (Colours::screenPanel);
        g.fillRoundedRectangle (r.toFloat(), fpx (4.0f));
        g.setColour (Colours::caption);
        g.setFont (wingFontPx (fpx (13.0f), WingWeight::SemiBold));
        g.drawText ("NO PLUGINS LOADED", r.removeFromTop (r.getHeight() / 2).withTrimmedTop (px (10.0f)), juce::Justification::centred, false);
        g.setColour (Colours::textDim);
        g.setFont (wingFontPx (fpx (9.0f)));
        g.drawFittedText ("Load a plugin into an FX slot (EFFECTS, or a strip's insert) and it shows here.", r.reduced (px (12.0f), 0),
                          juce::Justification::centredTop, 2, 0.8f);
    }

    /** The member's picture. Its plugin may have gone since the last poll (a paint can
        come first): only its address and the name read with it are used. */
    const PluginPictures::Picture* picture (const Member& m) const
    {
        return ctx.pluginPictures != nullptr ? ctx.pluginPictures->find (m.plugin, m.name) : nullptr;
    }

    /** A pill: a colour with black capitals. */
    void paintPill (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, juce::Colour fill) const
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r.toFloat(), (float) r.getHeight() * 0.5f);
        g.setColour (juce::Colours::black);
        g.setFont (wingFontPx (fpx (7.5f), WingWeight::SemiBold));
        g.drawText (text, r, juce::Justification::centred, false);
    }

    int pillWidth (const juce::String& text) const
    {
        return (int) juce::GlyphArrangement::getStringWidth (wingFontPx (fpx (7.5f), WingWeight::SemiBold), text) + px (18.0f);
    }

    /** The strips using the slot: a colour mark and the label each, as many as fit. */
    void paintUsers (juce::Graphics& g, const Slot& s, juce::Rectangle<int> line) const
    {
        g.setFont (wingFontPx (fpx (8.5f)));
        if (s.users.empty())
        {
            g.setColour (Colours::textDim);
            g.drawText ("NO INSERT USES IT", line, juce::Justification::centredLeft, false);
            return;
        }
        for (size_t i = 0; i < s.users.size(); ++i)
        {
            const auto& u = s.users[i];
            const int w = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), u.label) + px (16.0f);
            if (w > line.getWidth())
            {
                g.setColour (Colours::textDim);
                g.drawText ("+" + juce::String ((int) (s.users.size() - i)), line, juce::Justification::centredLeft, false);
                break;
            }
            auto chip = line.removeFromLeft (w);
            line.removeFromLeft (px (8.0f));
            g.setColour (u.colour);
            g.fillRect (chip.removeFromLeft (px (4.0f)).withSizeKeepingCentre (px (4.0f), px (14.0f)));
            g.setColour (Colours::caption);
            g.drawText (u.label, chip.withTrimmedLeft (px (6.0f)), juce::Justification::centredLeft, false);
        }
    }

    /** CPU and latency, right-aligned in two lines. */
    void paintNumbers (juce::Graphics& g, const Slot& s, juce::Rectangle<int> r) const
    {
        const double rate = juce::jmax (1.0, ctx.engine.getSampleRate());
        g.setFont (wingFontPx (fpx (8.5f)));
        g.setColour (s.load > 0.5f ? Colours::tapRed : (s.load > 0.25f ? Colours::amber : Colours::caption));
        g.drawText ("CPU " + juce::String (s.load * 100.0f, 1) + " %", r.removeFromTop (r.getHeight() / 2), juce::Justification::centredRight, false);
        g.setColour (s.latency > 0 ? Colours::caption : Colours::textDim);
        g.drawText (s.latency > 0 ? juce::String (s.latency) + " SMP  " + juce::String (1000.0 * s.latency / rate, 1) + " MS" : juce::String ("NO LATENCY"), r,
                    juce::Justification::centredRight, false);
    }

    /** IN and OUT, two thin bars each (L, R), -60 to 0 dB, with ticks at -18 and -6. */
    void paintMeters (juce::Graphics& g, const Slot& s, juce::Rectangle<int> area, bool sideBySide) const
    {
        const int labelW = px (26.0f);
        for (int pair = 0; pair < 2; ++pair)
        {
            juce::Rectangle<int> part;
            if (sideBySide)
            {
                const int w = (area.getWidth() - px (16.0f)) / 2;
                part = pair == 0 ? area.withWidth (w) : area.withTrimmedLeft (area.getWidth() - w);
            }
            else
                part = pair == 0 ? area.withHeight (area.getHeight() / 2) : area.withTrimmedTop (area.getHeight() / 2);
            auto line = part.reduced (0, px (2.0f));
            g.setColour (Colours::textDim);
            g.setFont (wingFontPx (fpx (7.0f), WingWeight::SemiBold));
            g.drawText (pair == 0 ? "IN" : "OUT", line.removeFromLeft (labelW), juce::Justification::centredLeft, false);
            const int barH = juce::jmax (2, juce::jmin (px (6.0f), (line.getHeight() - px (2.0f)) / 2));
            for (int side = 0; side < 2; ++side)
            {
                const auto bar = juce::Rectangle<int> (line.getX(), line.getCentreY() - barH - px (1.0f) + side * (barH + px (2.0f)), line.getWidth(), barH);
                g.setColour (Colours::screenDark);
                g.fillRect (bar);
                const float db = toDb (s.levels[pair * 2 + side]);
                const float fill = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
                g.setColour (meterColour (db));
                g.fillRect (bar.withWidth (juce::roundToInt ((float) bar.getWidth() * fill)));
                g.setColour (Colours::screenLine);
                for (float tick : { -18.0f, -6.0f })
                    g.fillRect (bar.getX() + juce::roundToInt ((float) bar.getWidth() * (tick + 60.0f) / 60.0f), bar.getY(), 1, bar.getHeight());
            }
        }
    }

    /** One plugin's picture in `r`: fitted, with its name, the picture's age and its state. */
    void paintTile (juce::Graphics& g, const Member& m, juce::Rectangle<int> r, bool slotBypassed, bool withCaption) const
    {
        g.setColour (Colours::screenDark);
        g.fillRect (r);
        auto inner = r.reduced (px (3.0f));
        const auto captionBar = withCaption ? inner.removeFromBottom (px (18.0f)) : juce::Rectangle<int>();
        const auto* pic = picture (m);
        if (pic != nullptr)
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (inner);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImageWithin (pic->image, inner.getX(), inner.getY(), inner.getWidth(), inner.getHeight(), juce::RectanglePlacement::centred);
        }
        const bool dimmed = m.bypassed || slotBypassed;
        const bool small = inner.getHeight() < px (70.0f);
        if (pic == nullptr)
        {
            // No picture yet: the name over a hint (a small tile: the name only).
            g.setColour (Colours::screenPanel.brighter (0.05f));
            g.fillRect (inner);
            if (! dimmed)
            {
                g.setColour (Colours::caption);
                g.setFont (wingFontPx (fpx (small ? 7.5f : 10.0f), WingWeight::SemiBold));
                if (small)
                    g.drawFittedText (m.name.toUpperCase(), inner.reduced (px (3.0f)), juce::Justification::centred, 3, 0.7f);
                else
                {
                    g.drawFittedText (m.name.toUpperCase(), inner.withTrimmedBottom (inner.getHeight() / 2).reduced (px (4.0f), 0),
                                      juce::Justification::centredBottom, 2, 0.7f);
                    g.setColour (Colours::textDim);
                    g.setFont (wingFontPx (fpx (7.5f)));
                    g.drawFittedText ("NO PICTURE YET", inner.withTrimmedTop (inner.getHeight() / 2 + px (4.0f)), juce::Justification::centredTop, 1, 0.8f);
                }
            }
        }

        if (dimmed)
        {
            g.setColour (juce::Colours::black.withAlpha (0.62f));
            g.fillRect (inner);
            const juce::String text (small ? "BYP" : (m.bypassed ? "BYPASSED" : "SLOT BYPASS"));
            const int w = juce::jmin (pillWidth (text), inner.getWidth() - px (4.0f));
            paintPill (g, inner.withSizeKeepingCentre (w, px (small ? 15.0f : 18.0f)), text, Colours::caption);
        }
        if (pic != nullptr)
        {
            // The picture's age, top right.
            g.setFont (wingFontPx (fpx (7.0f)));
            const auto age = ageOf (pic->taken);
            const int w = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), age) + px (10.0f);
            const auto badge = inner.withTrimmedLeft (inner.getWidth() - w).withHeight (px (14.0f));
            g.setColour (juce::Colours::black.withAlpha (0.65f));
            g.fillRect (badge);
            g.setColour (Colours::caption);
            g.drawText (age, badge, juce::Justification::centred, false);
        }
        if (withCaption)
        {
            g.setColour (Colours::screenHeader);
            g.fillRect (captionBar);
            g.setColour (m.faulted ? Colours::tapRed : (m.bypassed || slotBypassed ? Colours::textDim : Colours::text));
            g.setFont (wingFontPx (fpx (8.0f), WingWeight::SemiBold));
            g.drawFittedText (m.name.toUpperCase(), captionBar.reduced (px (5.0f), 0), juce::Justification::centredLeft, 1, 0.7f);
        }
        if (m.faulted)
        {
            g.setColour (Colours::tapRed);
            g.drawRect (r, juce::jmax (2, px (2.0f)));
            paintPill (g, { r.getX() + px (6.0f), r.getY() + px (6.0f), pillWidth ("FAULT"), px (16.0f) }, "FAULT", Colours::tapRed);
        }
    }

    void paintCard (juce::Graphics& g, const Slot& s, juce::Rectangle<int> area, bool hovered) const
    {
        const auto state = colourOf (s.state);
        const auto cardF = area.toFloat();
        g.setColour (hovered ? Colours::screenTile : Colours::screenPanel);
        g.fillRoundedRectangle (cardF, fpx (4.0f));
        g.setColour (hovered ? Colours::gold : Colours::screenLine);
        g.drawRoundedRectangle (cardF.reduced (0.5f), fpx (4.0f), hovered ? 1.5f : 1.0f);
        // The state's colour along the top.
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (area.withHeight (px (4.0f)));
            g.setColour (state);
            g.fillRoundedRectangle (cardF, fpx (4.0f));
        }

        auto r = area.reduced (px (10.0f), 0).withTrimmedTop (px (4.0f));
        // Header: FX n, the state, SANDBOX; CPU and latency at the right.
        auto head = r.removeFromTop (px (30.0f));
        paintNumbers (g, s, head.removeFromRight (px (130.0f)).reduced (0, px (2.0f)));
        g.setColour (Colours::text);
        g.setFont (wingFontPx (fpx (13.0f), WingWeight::SemiBold));
        const auto title = "FX " + juce::String (s.slot + 1);
        g.drawText (title, head, juce::Justification::centredLeft, false);
        head.removeFromLeft ((int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title) + px (10.0f));
        const auto caption = captionOf (s.state);
        paintPill (g, head.removeFromLeft (pillWidth (caption)).withSizeKeepingCentre (pillWidth (caption), px (17.0f)), caption, state);
        if (s.sandboxed)
        {
            head.removeFromLeft (px (6.0f));
            paintPill (g, head.removeFromLeft (pillWidth ("SANDBOX")).withSizeKeepingCentre (pillWidth ("SANDBOX"), px (17.0f)), "SANDBOX",
                       Colours::caption);
        }

        paintUsers (g, s, r.removeFromTop (px (22.0f)));

        // The plugins' pictures, side by side in chain order.
        auto pictures = r.removeFromTop (pictureHeight());
        const int n = (int) s.members.size(), shownN = juce::jmin (n, 4);
        const int tileGap = px (4.0f);
        const int tileW = (pictures.getWidth() - tileGap * (shownN - 1)) / juce::jmax (1, shownN);
        for (int i = 0; i < shownN; ++i)
        {
            auto tile = pictures.removeFromLeft (tileW);
            pictures.removeFromLeft (tileGap);
            paintTile (g, s.members[(size_t) i], tile, s.state == SlotState::bypassed, true);
            if (i == shownN - 1 && n > shownN)
            {
                const auto more = "+" + juce::String (n - shownN) + " MORE";
                paintPill (g, { tile.getRight() - pillWidth (more) - px (6.0f), tile.getBottom() - px (44.0f), pillWidth (more), px (16.0f) }, more,
                           Colours::caption);
            }
        }

        // Footer: IN / OUT meters.
        paintMeters (g, s, r.reduced (0, px (10.0f)), true);
    }

    void paintRow (juce::Graphics& g, const Slot& s, juce::Rectangle<int> area, bool hovered) const
    {
        const auto state = colourOf (s.state);
        auto box = area.reduced (px (10.0f), px (3.0f));
        g.setColour (hovered ? Colours::screenTile : Colours::screenPanel);
        g.fillRoundedRectangle (box.toFloat(), fpx (3.0f));
        g.setColour (hovered ? Colours::gold : Colours::screenLine);
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), fpx (3.0f), hovered ? 1.5f : 1.0f);
        g.setColour (state);
        g.fillRect (box.removeFromLeft (px (5.0f)).reduced (0, px (1.0f)));

        auto r = box.reduced (px (10.0f), px (6.0f));
        // FX n over its state.
        auto slotArea = r.removeFromLeft (px (96.0f));
        g.setColour (Colours::text);
        g.setFont (wingFontPx (fpx (13.0f), WingWeight::SemiBold));
        g.drawText ("FX " + juce::String (s.slot + 1), slotArea.removeFromTop (slotArea.getHeight() / 2), juce::Justification::centredLeft, false);
        const auto caption = captionOf (s.state);
        const int pillW = juce::jmin (slotArea.getWidth(), pillWidth (caption));
        paintPill (g, slotArea.withWidth (pillW).withSizeKeepingCentre (pillW, px (16.0f)), caption, state);

        // The first plugin's picture.
        if (! s.members.empty())
        {
            const int h = r.getHeight();
            paintTile (g, s.members.front(), r.removeFromLeft (h * 16 / 10), s.state == SlotState::bypassed, false);
            r.removeFromLeft (px (12.0f));
        }

        paintNumbers (g, s, r.removeFromRight (px (130.0f)));
        r.removeFromRight (px (14.0f));
        paintMeters (g, s, r.removeFromRight (px (120.0f)), false);
        r.removeFromRight (px (14.0f));

        paintUsers (g, s, r.removeFromTop (r.getHeight() * 45 / 100));
        // The chain: dim when bypassed, red when faulted.
        auto chain = r;
        if (s.sandboxed)
            paintPill (g, chain.removeFromRight (pillWidth ("SANDBOX")).withSizeKeepingCentre (pillWidth ("SANDBOX"), px (16.0f)), "SANDBOX", Colours::caption);
        g.setFont (wingFontPx (fpx (11.0f), WingWeight::SemiBold));
        for (size_t i = 0; i < s.members.size(); ++i)
        {
            const auto& m = s.members[i];
            auto name = m.name.toUpperCase();
            if (m.faulted)
                name << " (FAULT)";
            else if (m.bypassed)
                name << " (BYP)";
            if (i + 1 < s.members.size())
                name << "  >  ";
            const int w = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + px (4.0f);
            g.setColour (m.faulted ? Colours::tapRed : (m.bypassed || s.state == SlotState::bypassed ? Colours::textDim : Colours::text));
            if (w > chain.getWidth())
            {
                g.drawFittedText (name, chain, juce::Justification::centredLeft, 1, 0.6f);
                break;
            }
            g.drawText (name, chain.removeFromLeft (w), juce::Justification::centredLeft, false);
        }
    }

    //==============================================================================
    UiContext& ctx;
    int zoom = 100;
    Layout layout = Layout::cards;
    std::vector<Slot> slots;
    juce::String shown;
    bool capturing = false;
    int captureDone = 0, captureTotal = 0;
    juce::Rectangle<int> header, status, summaryArea;
    WingKey capture { "CAPTURE ALL" }, cardsKey { "CARDS" }, listKey { "LIST" }, onTop { "ON TOP" }, smaller { "-" }, larger { "+" };
    Body body { *this };
    juce::Viewport view;
};

//==============================================================================
PluginOverviewWindow::PluginOverviewWindow (UiContext& c, std::function<void()> close)
    : juce::DocumentWindow ("Sidecar - Plugin overview", Colours::screen, closeButton | minimiseButton | maximiseButton),
      ctx (c),
      content (std::make_unique<Content> (c)),
      onClose (std::move (close))
{
    setUsingNativeTitleBar (true);
    setContentNonOwned (content.get(), false);
    setResizable (true, false);
    setResizeLimits (560, 260, 16384, 16384);
    // Where it was last time (on whichever screen), else in the middle.
    if (! restoreWindowStateFromString (ctx.settings.getValue (stateKey)))
        centreWithSize (1180, 640);
    setAlwaysOnTop (ctx.settings.getBoolValue (onTopKey, false));
    remembering = true;
    ctx.settings.setValue (openKey, true);
    setVisible (true);
}

PluginOverviewWindow::~PluginOverviewWindow()
{
    clearContentComponent();
}

PluginOverviewWindow::Layout PluginOverviewWindow::getLayout() const noexcept { return content->getLayout(); }
void PluginOverviewWindow::setLayout (Layout l) { content->setLayout (l); }
std::vector<int> PluginOverviewWindow::getShownSlots() const { return content->getShownSlots(); }

void PluginOverviewWindow::remember()
{
    if (remembering && isOnDesktop())
        ctx.settings.setValue (stateKey, getWindowStateAsString());
}

void PluginOverviewWindow::moved()
{
    juce::DocumentWindow::moved();
    remember();
}

void PluginOverviewWindow::resized()
{
    juce::DocumentWindow::resized();
    remember();
}

void PluginOverviewWindow::closeButtonPressed()
{
    ctx.settings.setValue (openKey, false);
    juce::MessageManager::callAsync ([w = juce::Component::SafePointer<PluginOverviewWindow> (this)]
    {
        if (w != nullptr && w->onClose)
            w->onClose();
    });
}

} // namespace livedaw::ui
