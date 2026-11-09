#include "ui/Sq5Surface.h"

#include "engine/MixerEngine.h"
#include "engine/Models.h"
#include "ui/HardwareLook.h"
#include "ui/MeterBank.h"

namespace livedaw::ui
{

using namespace universal;

namespace
{
    const hw::Look& look() { return hw::Look::sq(); }
    // The keys' colours, from the console's photographs.
    // Lit, then the faces they show when idle (their backlights are always on).
    const juce::Colour kMute (0xffff2a2a), kSel (0xff46f5c6), kPafl (0xffffa52e), kBlue (0xffbfe2ff), kWhite (0xfff5f5f5), kRed (0xffff5a5a),
        kScreenKey (0xffd2f7e6), kGreenLed (0xff8ff5c0), kPeach (0xffffc98a);
    const juce::Colour kFacePink (0xfff3b7b2), kFaceTeal (0xff0e8f76), kFaceGrey (0xff8c8f94), kFaceRed (0xffc61e26), kFaceBlue (0xff1a55c4),
        kFaceLayer (0xffc3ccd7), kFaceMixA (0xff90d9f1), kFaceMixB (0xff42b9ec), kFaceSoftGrey (0xffb8bbc0);
    // The jewels: what each rotary's centre is lit in.
    const juce::Colour kJewelRed (0xffff3a30), kJewelGreen (0xff4cf08a), kJewelOrange (0xffff9a2a), kJewelYellow (0xffffe05a), kJewelOff (0xff4a4d52);

    constexpr int kStripWidth = 94, kStripsX = 138, kGroupGap = 4;
    constexpr int kStripTop = 480, kStripBottom = 1072;

    int stripX (int i) { return kStripsX + i * kStripWidth + (i / 4) * kGroupGap; }

    KnobBinding bindParam (models::Table table, int model, std::atomic<float>* values, const juce::String& id, const juce::String& label)
    {
        const auto& m = models::model (table, model);
        const int i = m.find (id);
        if (i < 0)
            return {};
        auto b = bindModelParam (values[i], m.params[(size_t) i]);
        b.label = label;
        return b;
    }

    /** The default layers (Sidecar's reading: the guide says the strips are freely
        assignable, p.84, but not how a new console has them). */
    std::vector<std::vector<sq5::Slot>> defaultLayers()
    {
        using S = sq5::Slot;
        std::vector<std::vector<S>> layers (Sq5Surface::kLayers, std::vector<S> (Sq5Surface::kStrips));
        for (int i = 0; i < 16; ++i)
        {
            layers[0][(size_t) i] = { S::Input, i };
            layers[1][(size_t) i] = { S::Input, 16 + i };
            layers[2][(size_t) i] = { S::Input, 32 + i };
            if (i < 12)
                layers[4][(size_t) i] = { S::Mix, i };
            else
                layers[4][(size_t) i] = { S::FxSend, i - 12 };
        }
        for (int i = 0; i < 8; ++i)
        {
            layers[3][(size_t) i] = { S::FxReturn, i };
            layers[5][(size_t) i] = { S::Dca, i };
        }
        for (int i = 0; i < 3; ++i)
            layers[3][(size_t) (8 + i)] = { S::Matrix, i };
        return layers;
    }
} // namespace

namespace sq5
{
    FaderStrip::FaderStrip (Sq5Surface& s, UiContext& c, const Theme& theme, bool isMaster)
        : surface (s), ctx (c), master (isMaster), fader (theme), mute ("Mute", kMute, theme), sel ("Sel", kSel, theme), pafl ("PAFL", kPafl, theme)
    {
        addAndMakeVisible (fader);
        for (auto* k : { &mute, &sel, &pafl })
        {
            k->shape = ToggleKey::Shape::Oval;
            addAndMakeVisible (*k);
        }
        mute.cornerRadius = 6.0f;
        mute.face = kFacePink;
        sel.face = kFaceTeal;
        pafl.face = kFaceGrey;
        mute.isOn = [this] { return muteState (false); };
        mute.isHalfOn = [this] { return muteState (true); };
        mute.onClick = [this]
        {
            if (slot.kind == Slot::Dca)
            {
                auto& d = ctx.engine.getDca (slot.index);
                d.mute = ! d.mute.load();
            }
            else if (auto* strip = stripOf (ctx.engine, slot))
                strip->params.mute = ! strip->params.mute.load();
        };
        sel.onClick = [this] { pressSel(); };
        sel.isOn = [this]
        {
            auto* strip = stripOf (ctx.engine, slot);
            const int bus = busOfMix (surface.getMix());
            if ((surface.isAssignMode() || surface.isPreFadeMode()) && slot.isSendSource() && strip != nullptr)
            {
                // While Assign or Pre Fade is held the Sel keys show it (pp.13, 50).
                if (bus < 0)
                {
                    if (surface.isPreFadeMode())
                        return false;
                    return strip->mains[0].on.load();
                }
                auto& send = strip->sends[(size_t) bus];
                return surface.isAssignMode() ? send.enabled.load() : send.mode.load() == (int) SendMode::Pre;
            }
            return slot.kind != Slot::None && slot == surface.selectedSlot();
        };
        pafl.isOn = [this]
        {
            if (slot.kind == Slot::Dca)
                return ctx.engine.getDca (slot.index).solo.load();
            auto* strip = stripOf (ctx.engine, slot);
            return strip != nullptr && strip->params.solo.load();
        };
        pafl.onClick = [this]
        {
            if (slot.kind == Slot::Dca)
            {
                auto& d = ctx.engine.getDca (slot.index);
                d.solo = ! d.solo.load();
            }
            else if (auto* strip = stripOf (ctx.engine, slot))
                strip->params.solo = ! strip->params.solo.load();
        };
    }

    bool FaderStrip::muteState (bool byGroup) const
    {
        if (slot.kind == Slot::Dca)
            return ! byGroup && ctx.engine.getDca (slot.index).mute.load();
        auto* strip = stripOf (ctx.engine, slot);
        if (strip == nullptr)
            return false;
        if (! byGroup)
            return strip->params.mute.load();
        // Muted by a DCA or a mute group: the key's lit differently (p.49).
        const auto counts = ctx.engine.getCounts();
        for (int d = 0; d < counts.dcas; ++d)
            if (strip->params.dcaMask.contains (d) && ctx.engine.getDca (d).mute.load())
                return true;
        for (int m = 0; m < counts.muteGroups; ++m)
            if (strip->params.muteGroupMask.contains (m) && ctx.engine.muteGroups.contains (m))
                return true;
        return false;
    }

    void FaderStrip::pressSel()
    {
        auto* strip = stripOf (ctx.engine, slot);
        if (strip != nullptr && surface.applyClipboard (*strip))
            return;
        const int bus = busOfMix (surface.getMix());
        if (surface.isAssignMode() || surface.isPreFadeMode())
        {
            // Hold Assign or Pre Fade and press Sel (p.50): a channel to the selected mix,
            // or a mix's Sel for all channels to it.
            const bool assign = surface.isAssignMode();
            if (slot.isSendSource() && strip != nullptr)
            {
                if (bus < 0)
                {
                    if (assign)
                        strip->mains[0].on = ! strip->mains[0].on.load();
                }
                else
                {
                    auto& send = strip->sends[(size_t) bus];
                    if (assign)
                        send.enabled = ! send.enabled.load();
                    else
                        send.mode = send.mode.load() == (int) SendMode::Pre ? (int) SendMode::Post : (int) SendMode::Pre;
                }
                return;
            }
            if ((slot.kind == Slot::Mix || slot.kind == Slot::FxSend))
            {
                const int b = slot.kind == Slot::Mix ? slot.index : kMixes + slot.index;
                bool anyOn = false;
                for (const auto* list : { &ctx.engine.getChannels(), &ctx.engine.getAux() })
                    for (const auto& c : *list)
                        anyOn = anyOn || (assign ? c->sends[(size_t) b].enabled.load() : c->sends[(size_t) b].mode.load() == (int) SendMode::Pre);
                for (const auto* list : { &ctx.engine.getChannels(), &ctx.engine.getAux() })
                    for (const auto& c : *list)
                    {
                        if (assign)
                            c->sends[(size_t) b].enabled = ! anyOn;
                        else
                            c->sends[(size_t) b].mode = anyOn ? (int) SendMode::Post : (int) SendMode::Pre;
                    }
                return;
            }
        }
        if (strip != nullptr && ctx.selectStrip)
            ctx.selectStrip (strip);
    }

    void FaderStrip::setSlot (const Slot& s)
    {
        slot = s;
        rebind();
    }

    void FaderStrip::rebind()
    {
        // Fader Flip: a GEQ band on each strip (p.38).
        flipLabel = {};
        if (surface.getFaderFlip() > 0 && ! master)
        {
            auto* geq = surface.selectedGeq();
            const int band = (surface.getFaderFlip() - 1) * Sq5Surface::kStrips + index;
            if (geq != nullptr && band < 28)
            {
                const auto& m = models::model (models::BusEq, geq->model.load());
                fader.bindLinear (&geq->values[band], -12.0f, 12.0f);
                flipLabel = m.params[(size_t) band].label;
            }
            else
                fader.bind (nullptr);
            repaint();
            return;
        }
        std::atomic<float>* value = nullptr;
        if (slot.kind == Slot::Dca)
            value = &ctx.engine.getDca (slot.index).faderDb;
        else if (auto* strip = stripOf (ctx.engine, slot))
        {
            value = &strip->params.faderDb;
            const int bus = busOfMix (surface.getMix());
            if (! master && slot.isSendSource() && bus >= 0)
                value = &strip->sends[(size_t) bus].levelDb; // the send to the selected mix (p.9)
            if (! master && surface.isChToAll() && (slot.kind == Slot::Mix || slot.kind == Slot::FxSend))
                if (auto* sel_ = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr; sel_ != nullptr && sel_->isInput())
                    value = &sel_->sends[(size_t) (slot.kind == Slot::Mix ? slot.index : kMixes + slot.index)].levelDb; // CH to All Mix (p.50)
        }
        fader.bind (value);
        const bool used = slot.kind != Slot::None;
        for (auto* k : { &mute, &sel, &pafl })
            k->setEnabled (used);
        repaint();
    }

    juce::String FaderStrip::getLcdText() const
    {
        if (surface.getFaderFlip() > 0 && ! master)
            return flipLabel;
        if (slot.kind == Slot::Dca)
        {
            const auto n = ctx.engine.getDcaName (slot.index);
            return n.isNotEmpty() ? n : "DCA" + juce::String (slot.index + 1);
        }
        auto* strip = stripOf (ctx.engine, slot);
        return strip != nullptr ? strip->getName() : juce::String();
    }

    void FaderStrip::refresh()
    {
        fader.refresh();
        for (auto* k : { &mute, &sel, &pafl })
            k->refresh();

        juce::Colour colour;
        bool signal = false, peak = false;
        if (slot.kind == Slot::Dca)
            colour = consoleColour (ctx.engine.getDca (slot.index).colour.load());
        else if (auto* strip = stripOf (ctx.engine, slot))
        {
            colour = strip->getColour();
            const float level = juce::jmax (ctx.meters.get (strip, MeterPoint::Output, 0), ctx.meters.get (strip, MeterPoint::Output, 1));
            signal = level > 0.003f;
            peak = level > 0.89f;
        }
        const auto name = getLcdText();
        const auto value = surface.isViewAlt() ? fader::text ((float) fader::posToDb ((float) fader.getPosition())) : juce::String();
        if (name != shownName || colour != shownColour || signal != shownSignal || peak != shownPeak || value != shownValue)
        {
            shownName = name;
            shownColour = colour;
            shownSignal = signal;
            shownPeak = peak;
            shownValue = value;
            repaint();
        }
    }

    void FaderStrip::resized()
    {
        // In the panel's design size (kStripWidth wide): LCD, Mute, Sel, PAFL, LEDs, fader.
        const int cx = getWidth() / 2;
        const auto keyAt = [&] (int y, int w, int h) { return juce::Rectangle<int> (cx - w / 2, y, w, h); };
        mute.setBounds (keyAt (60, 44, 28));
        sel.setBounds (keyAt (104, 52, 28));
        pafl.setBounds (keyAt (146, 32, 32));
        fader.setBounds (getLocalBounds().withTrimmedTop (206).reduced (6, 2));
    }

    void FaderStrip::paint (juce::Graphics& g)
    {
        const auto& l = look();
        const auto w = (float) getWidth();
        hw::silk (g, { 0.0f, 47.0f, w, 12.0f }, "Mute", l, 10.5f);
        hw::silk (g, { 0.0f, 91.0f, w, 12.0f }, "Sel", l, 10.5f);
        hw::silk (g, { 0.0f, 135.0f, w, 12.0f }, "PAFL", l, 10.5f);

        // The Pk LED, the signal LED under it.
        const auto led = [&] (float y, bool on, juce::Colour c)
        {
            juce::Rectangle<float> r (w * 0.5f - 4.0f, y, 8.0f, 8.0f);
            g.setColour (on ? c : c.withMultipliedBrightness (0.22f));
            g.fillEllipse (r);
            g.setColour (juce::Colours::black);
            g.drawEllipse (r, 0.8f);
        };
        hw::silk (g, { w * 0.5f - 30.0f, 178.0f, 26.0f, 12.0f }, "Pk", l, 9.5f, juce::Justification::centredRight);
        led (180.0f, shownPeak, juce::Colours::red);
        led (193.0f, shownSignal, kGreenLed);

        if (master)
            return;
        // The LCD, backlit in the channel's colour.
        juce::Rectangle<float> lcd (3.0f, 2.0f, w - 6.0f, 42.0f);
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (lcd, 2.0f);
        if (slot.kind == Slot::None)
            return;
        auto screen = lcd.reduced (2.0f);
        // The backlight washes the colour out towards mint-white.
        g.setColour (shownColour.isTransparent() || shownColour.getPerceivedBrightness() < 0.05f ? juce::Colour (0xff101214) : shownColour.interpolatedWith (juce::Colour (0xffbffff0), 0.5f));
        g.fillRect (screen);
        g.setColour (juce::Colour (0xff0d2a20));
        g.setFont (type::font (screen.getHeight() * (shownValue.isNotEmpty() ? 0.42f : 0.56f), true));
        auto top = shownValue.isNotEmpty() ? screen.removeFromTop (screen.getHeight() * 0.55f) : screen;
        g.drawFittedText (shownName, top.getSmallestIntegerContainer(), juce::Justification::centred, 1, 0.6f);
        if (shownValue.isNotEmpty())
            g.drawFittedText (shownValue, screen.getSmallestIntegerContainer(), juce::Justification::centred, 1, 0.8f);
    }
} // namespace sq5

//==============================================================================
Sq5Surface::Sq5Surface (UiContext& c)
    : ctx (c), theme (Theme::dark()), universalKey ("UNIVERSAL MIXER", theme.accent, theme), display (c, theme), layers (defaultLayers())
{
    addAndMakeVisible (display);
    display.getSelectedSlot = [this] { return selectedSlot(); };
    display.onSelectSlot = [this] (const sq5::Slot& s)
    {
        if (auto* strip = sq5::stripOf (ctx.engine, s); strip != nullptr && ctx.selectStrip)
            ctx.selectStrip (strip);
    };
    display.onRotaryChanged = [this]
    {
        rotary.setBinding (display.getRotaryBinding());
        lightJewels();
    };
    display.getLayerSlots = [this] (int l) -> const std::vector<sq5::Slot>& { return getLayerSlots (l); };
    display.assignSlot = [this] (int l, int i, const sq5::Slot& s)
    {
        layers[(size_t) l][(size_t) i] = s;
        saveLayers();
        bindStrips();
    };
    loadLayers();

    for (int i = 0; i < kStrips; ++i)
    {
        auto& s = strips[(size_t) i];
        s = std::make_unique<sq5::FaderStrip> (*this, ctx, theme, false);
        s->setIndex (i);
        addAndMakeVisible (*s);
    }
    master = std::make_unique<sq5::FaderStrip> (*this, ctx, theme, true);
    addAndMakeVisible (*master);

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

    // The screen keys, under the touchscreen.
    const std::pair<const char*, sq5::Screen> screenKeys[] = { { "Processing", sq5::Screen::Processing }, { "Routing", sq5::Screen::Routing },
                                                               { "Meters", sq5::Screen::Meters },         { "FX", sq5::Screen::Fx },
                                                               { "Scenes", sq5::Screen::Scenes },         { "Setup", sq5::Screen::Setup },
                                                               { "Utility", sq5::Screen::Utility },       { "I/O", sq5::Screen::Io } };
    int at = 0;
    for (const auto& [name, screen] : screenKeys)
    {
        auto& k = panelKey (name, kScreenKey, { 610 + at * 80, 434, 60, 26 }, true, at < 2 ? kFaceTeal : kFaceGrey);
        k.cornerRadius = 6.0f;
        k.isOn = [this, s = screen] { return display.getScreen() == s; };
        k.onClick = [this, s = screen] { display.pressScreenKey (s); };
        ++at;
    }

    // The Processing section, left of the screen (the datasheet's p.2).
    panelKnob ("Preamp", { 217, 40, 62, 62 });
    auto& library = panelKey ("Library", kWhite, { 420, 62, 58, 30 });
    library.cornerRadius = 7.0f;
    library.isOn = [this] { return display.hasLibrary(); };
    library.onClick = [this] { display.pressLibrary(); };
    panelKnob ("HPF", { 217, 124, 62, 62 });
    panelKnob ("Gate", { 274, 200, 62, 62 });
    panelKnob ("Comp", { 354, 268, 62, 62 });
    panelKnob ("Pan", { 423, 352, 62, 62 });
    auto& insertKey = panelKey ("Insert Active", kWhite, { 420, 136, 58, 30 });
    insertKey.onClick = [this]
    {
        if (auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr)
        {
            auto& ins = s->params.preInsert();
            ins.on = ! ins.on.load();
        }
    };
    insertKey.isOn = [this]
    {
        auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
        return s != nullptr && s->params.preInsert().on.load();
    };
    const auto inKey = [this] (juce::Rectangle<int> r, std::function<std::atomic<bool>*(Strip&)> get, const juce::String& name)
    {
        auto& k = panelKey (name, kGreenLed, r.withSizeKeepingCentre (32, 32));
        k.isOn = [this, get]
        {
            auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
            if (s == nullptr)
                return false;
            auto* v = get (*s);
            return v != nullptr && v->load();
        };
        k.onClick = [this, get]
        {
            auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
            if (s == nullptr)
                return;
            if (auto* v = get (*s))
                v->store (! v->load());
        };
        return &k;
    };
    inKey ({ 120, 142, 44, 26 }, [] (Strip& s) { return s.isInput() ? &s.params.filter().lowCut : nullptr; }, "HPF In");
    inKey ({ 164, 218, 44, 26 }, [] (Strip& s) { return s.isInput() ? &s.params.gate().on : nullptr; }, "Gate In");
    inKey ({ 240, 288, 44, 26 }, [] (Strip& s) -> std::atomic<bool>* { return s.getKind() == StripKind::Aux ? nullptr : &s.params.dynamics().on; }, "Comp In");

    // The EQ, right of the screen.
    panelKnob ("Width", { 1534, 40, 62, 62 });
    panelKnob ("Freq", { 1534, 124, 62, 62 });
    panelKnob ("EQ Gain", { 1478, 200, 62, 62 });
    inKey ({ 1420, 76, 44, 26 }, [] (Strip& s) { return &s.params.eq().on; }, "EQ In");
    const std::pair<const char*, juce::Rectangle<int>> bands[] = { { "LF", { 1618, 232, 44, 26 } }, { "LM", { 1662, 180, 44, 26 } },
                                                                   { "HM", { 1662, 124, 44, 26 } }, { "HF", { 1662, 66, 44, 26 } } };
    for (int b = 0; b < 4; ++b)
    {
        auto& k = panelKey (bands[(size_t) b].first, kPeach, bands[(size_t) b].second.withSizeKeepingCentre (32, 32));
        k.isOn = [this, b] { return eqBand == b; };
        k.onClick = [this, b] { setEqBand (b); };
    }
    // The selected mix's GEQ: In and Fader Flip.
    auto& geqIn = panelKey ("GEQ In", kGreenLed, juce::Rectangle<int> { 1626, 330, 40, 24 }.withSizeKeepingCentre (28, 28));
    geqIn.isOn = [this] { auto* geq = selectedGeq(); return geq != nullptr && geq->on.load(); };
    geqIn.onClick = [this]
    {
        if (auto* geq = selectedGeq())
            geq->on = ! geq->on.load();
    };
    auto& flip = panelKey ("Fader Flip", kWhite, { 1670, 330, 56, 24 });
    flip.isOn = [this] { return faderFlip > 0; };
    flip.onClick = [this] { pressFaderFlip(); };

    // The PAFL meter, Talk and the touchscreen rotary.
    auto& talk = panelKey ("Talk", kPafl, { 1336, 312, 56, 30 }, true, kFaceTeal);
    talk.cornerRadius = 7.0f;
    talk.isOn = [this] { return ctx.engine.talk.paths[0].on.load(); };
    talk.onClick = [this]
    {
        auto& on = ctx.engine.talk.paths[0].on;
        on = ! on.load();
    };
    rotary.setHardware (true);
    addAndMakeVisible (rotary);
    rotary.setBounds (1330, 360, 62, 62);

    // SoftKeys 1-8: what they do is Setup > Surface > Soft Controls.
    const juce::Rectangle<int> softs[] = { { 40, 196, 56, 26 },  { 40, 250, 56, 26 },  { 40, 304, 56, 26 },  { 40, 358, 56, 26 },
                                           { 120, 408, 56, 26 }, { 196, 408, 56, 26 }, { 1560, 424, 56, 26 }, { 1650, 424, 56, 26 } };
    for (int i = 0; i < 8; ++i)
    {
        auto& k = panelKey ("Soft " + juce::String (i + 1), kRed, softs[(size_t) i], true, i < 6 ? kFacePink : kFaceSoftGrey);
        k.cornerRadius = 6.0f;
        k.onClick = [this, i] { pressSoftKey (i); };
        k.isOn = [this, i]
        {
            const int f = sq5::soft::get (ctx.settings, i);
            if (f == 3)
                return ctx.engine.talk.paths[0].on.load();
            return f >= 4 && ctx.engine.muteGroups.contains (f - 4);
        };
    }

    // The left column: View; Pre Fade, Assign, CH to All Mix; Copy, Paste, Reset; Layers.
    auto& view = panelKey ("View", kWhite, { 44, 498, 56, 28 });
    view.onStateChange = [this, &view] { viewAlt = view.isDown(); };
    auto& pre = panelKey ("Pre Fade", kBlue, { 44, 562, 56, 26 });
    pre.isOn = [this] { return preFadeMode; };
    pre.onClick = [this] { setPreFadeMode (! preFadeMode); };
    auto& assign = panelKey ("Assign", kBlue, { 44, 606, 56, 26 });
    assign.isOn = [this] { return assignMode; };
    assign.onClick = [this] { setAssignMode (! assignMode); };
    auto& all = panelKey ("CH to All Mix", kBlue, { 44, 650, 56, 26 }, true, kFaceBlue);
    all.isOn = [this] { return chToAll; };
    all.onClick = [this] { setChToAll (! chToAll); };
    int y = 720;
    int mode = 1;
    for (const char* name : { "Copy", "Paste", "Reset" })
    {
        auto& k = panelKey (name, kRed, { 44, y, 56, 26 }, true, mode == 1 ? kFaceGrey : kFaceRed);
        k.isOn = [this, mode] { return clipboardMode == mode; };
        k.onClick = [this, mode] { setClipboardMode (clipboardMode == mode ? 0 : mode); };
        y += 44;
        ++mode;
    }
    for (int l = 0; l < kLayers; ++l)
    {
        const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + l));
        auto& k = panelKey ("Layer " + letter, kGreenLed, { 44, 864 + l * 34, 56, 24 }, false, kFaceLayer);
        k.isOn = [this, l] { return layer == l; };
        k.onClick = [this, l] { setLayer (l); };
        silks.push_back ({ juce::Rectangle<int> (102, 864 + l * 34, 18, 24).toFloat(), letter, 12.0f, juce::Justification::centred });
    }

    // The right column: FX 1-4, LR, Mix 1-12.
    for (int f = 0; f < sq5::kFxSends; ++f)
    {
        auto& k = panelKey ("FX " + juce::String (f + 1), kBlue, { 1832, 286 + f * 44, 56, 26 }, false, kFaceBlue);
        k.isOn = [this, f] { return mix == 13 + f; };
        k.onClick = [this, f] { setMix (13 + f); };
        silks.push_back ({ juce::Rectangle<int> (1806, 286 + f * 44, 24, 26).toFloat(), juce::String (f + 1), 12.0f, juce::Justification::centred });
    }
    auto& lr = panelKey ("LR", kBlue, { 1808, 500, 52, 26 }, true, kFaceBlue);
    lr.isOn = [this] { return mix == 0; };
    lr.onClick = [this] { setMix (0); };
    const auto mixY = [] (int m) { return m < 4 ? 552 + m * 40 : m < 8 ? 720 + (m - 4) * 44 : 900 + (m - 8) * 40; };
    for (int m = 0; m < sq5::kMixes; ++m)
    {
        auto& k = panelKey ("Mix " + juce::String (m + 1), kBlue, { 1832, mixY (m), 56, 26 }, false, m < 4 ? kFaceMixA : m < 8 ? kFaceMixB : kFaceBlue);
        k.isOn = [this, m] { return mix == 1 + m; };
        k.onClick = [this, m] { setMix (1 + m); };
        silks.push_back ({ juce::Rectangle<int> (1806, mixY (m), 24, 26).toFloat(), juce::String (m + 1), 12.0f, juce::Justification::centred });
    }

    // Phones.
    panelKnob ("Phones", { 1820, 130, 56, 56 }).setBinding (bindFader ("Phones", ctx.engine.phones.levelDb));

    // The strips, their panel silkscreen.
    silks.push_back ({ { 1862.0f, 498.0f, 56.0f, 14.0f }, "Mix", 11.0f, juce::Justification::centred });
    silks.push_back ({ { 1862.0f, 512.0f, 56.0f, 14.0f }, "Select", 11.0f, juce::Justification::centred });
    silks.push_back ({ { 1810.0f, 264.0f, 70.0f, 16.0f }, "FX", 12.0f, juce::Justification::centred });
    silks.push_back ({ { 44.0f, 846.0f, 70.0f, 14.0f }, "Layers", 11.0f, juce::Justification::centredLeft });

    hw::applyTo (*this, look(), &universalKey);
    setLayer (0);
    bindKnobs();
    startTimerHz (20);
}

Sq5Surface::~Sq5Surface() = default;

ToggleKey& Sq5Surface::panelKey (const juce::String& name, juce::Colour colour, juce::Rectangle<int> r, bool labelAbove, juce::Colour face)
{
    auto k = std::make_unique<ToggleKey> (name, colour, theme);
    k->shape = ToggleKey::Shape::Oval;
    k->face = face.isTransparent() ? kFaceGrey : face;
    k->setBounds (r);
    addAndMakeVisible (*k);
    if (labelAbove)
        silks.push_back ({ juce::Rectangle<int> (r.getX() - 22, r.getY() - 15, r.getWidth() + 44, 14).toFloat(), name, 11.0f, juce::Justification::centred });
    keys.push_back (std::move (k));
    return *keys.back();
}

DisplayKnob& Sq5Surface::panelKnob (const juce::String& name, juce::Rectangle<int> r)
{
    auto k = std::make_unique<DisplayKnob>();
    k->setHardware (true);
    k->setBounds (r);
    addAndMakeVisible (*k);
    const auto below = name == "Phones";
    silks.push_back ({ (below ? juce::Rectangle<int> (r.getX() - 8, r.getBottom() + 2, r.getWidth() + 16, 14) : juce::Rectangle<int> (r.getRight() + 2, r.getY() + 2, 64, 16)).toFloat(),
                       name == "EQ Gain" ? juce::String ("Gain") : name, 13.0f, below ? juce::Justification::centred : juce::Justification::centredLeft });
    knobs.emplace_back (name, std::move (k));
    return *knobs.back().second;
}

ToggleKey* Sq5Surface::findKey (const juce::String& name) const
{
    for (const auto& k : keys)
        if (k->getButtonText() == name)
            return k.get();
    return nullptr;
}

DisplayKnob& Sq5Surface::getKnob (const juce::String& name)
{
    for (auto& [n, k] : knobs)
        if (n == name)
            return *k;
    return *knobs.front().second;
}

sq5::Slot Sq5Surface::selectedSlot() const
{
    return sq5::slotOf (ctx.engine, ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr);
}

void Sq5Surface::bindStrips()
{
    const auto& slots = layers[(size_t) layer];
    for (int i = 0; i < kStrips; ++i)
        strips[(size_t) i]->setSlot (slots[(size_t) i]);
    master->setSlot (sq5::slotOfMix (mix));
    display.setLayer (slots);
    display.setMix (mix);
}

void Sq5Surface::setLayer (int l)
{
    layer = juce::jlimit (0, kLayers - 1, l);
    bindStrips();
}

void Sq5Surface::setMix (int m)
{
    mix = juce::jlimit (0, sq5::kMixes + sq5::kFxSends, m);
    bindStrips();
}

ld::EqBlockParams* Sq5Surface::selectedGeq() const
{
    auto* s = sq5::stripOf (ctx.engine, sq5::slotOfMix (mix));
    if (s == nullptr)
        return nullptr;
    for (const auto& step : s->getChain())
        if (step.isBlock() && step.getBlockKind() == BlockKind::Eq)
            return s->getBlock (step.uid).eq();
    return nullptr;
}

void Sq5Surface::pressFaderFlip()
{
    faderFlip = (faderFlip + 1) % 3;
    if (selectedGeq() == nullptr)
        faderFlip = 0;
    bindStrips();
}

void Sq5Surface::setClipboardMode (int m)
{
    clipboardMode = m;
    if (m != 0)
        assignMode = preFadeMode = false;
}

bool Sq5Surface::applyClipboard (Strip& strip)
{
    if (clipboardMode == 0)
        return false;
    auto& to = strip.params;
    if (clipboardMode == 3)
    {
        // Reset: the channel's processing to its defaults.
        for (auto block : { ModelBlock::Gate, ModelBlock::Eq, ModelBlock::Dynamics })
            strip.setModel (block, block == ModelBlock::Gate ? to.gate().model.load() : block == ModelBlock::Eq ? to.eq().model.load() : to.dynamics().model.load());
        to.gate().on = false;
        to.eq().on = false;
        to.dynamics().on = false;
        to.filter().lowCut = false;
        to.input.invert = false;
        clipboardMode = 0;
        return true;
    }
    if (clipboardMode == 1)
    {
        clipboard = sq5::slotOf (ctx.engine, &strip);
        clipboardMode = 0;
        return true;
    }
    if (auto* from = sq5::stripOf (ctx.engine, clipboard); from != nullptr && from != &strip && from->getKind() == strip.getKind())
    {
        auto& p = from->params;
        const auto copyDyn = [] (ld::DynSlotParams& a, ld::DynSlotParams& b)
        {
            b.on = a.on.load();
            for (int i = 0; i < ld::kMaxDynParams; ++i)
                b.values[i] = a.values[i].load();
        };
        copyDyn (p.gate(), to.gate());
        copyDyn (p.dynamics(), to.dynamics());
        to.eq().on = p.eq().on.load();
        for (int i = 0; i < ld::kMaxEqParams; ++i)
            to.eq().values[i] = p.eq().values[i].load();
        to.filter().lowCut = p.filter().lowCut.load();
        to.filter().lowCutFreq = p.filter().lowCutFreq.load();
        to.filter().lowCutSlope = p.filter().lowCutSlope.load();
        to.input.trimDb = p.input.trimDb.load();
        to.input.invert = p.input.invert.load();
    }
    clipboardMode = 0;
    return true;
}

void Sq5Surface::pressSoftKey (int key)
{
    const int f = sq5::soft::get (ctx.settings, key);
    auto& show = ctx.show;
    if (f == 1)
        show.recallNext();
    else if (f == 2)
        show.recallPrevious();
    else if (f == 3)
    {
        auto& on = ctx.engine.talk.paths[0].on;
        on = ! on.load();
    }
    else if (f >= 4)
        ctx.engine.muteGroups.set (f - 4, ! ctx.engine.muteGroups.contains (f - 4));
}

void Sq5Surface::saveLayers()
{
    juce::StringArray out;
    for (const auto& l : layers)
        for (const auto& s : l)
            out.add (juce::String ((int) s.kind) + ":" + juce::String (s.index));
    ctx.settings.setValue ("sq5.layers", out.joinIntoString (","));
}

void Sq5Surface::loadLayers()
{
    const auto items = juce::StringArray::fromTokens (ctx.settings.getValue ("sq5.layers"), ",", {});
    if (items.size() != kLayers * kStrips)
        return;
    for (int i = 0; i < items.size(); ++i)
    {
        const int kind = items[i].upToFirstOccurrenceOf (":", false, false).getIntValue();
        const int index = items[i].fromFirstOccurrenceOf (":", false, false).getIntValue();
        layers[(size_t) (i / kStrips)][(size_t) (i % kStrips)] = { (sq5::Slot::Kind) juce::jlimit (0, (int) sq5::Slot::Dca, kind), index };
    }
}

void Sq5Surface::setAssignMode (bool on)
{
    assignMode = on;
    if (on)
        preFadeMode = false;
}

void Sq5Surface::setPreFadeMode (bool on)
{
    preFadeMode = on;
    if (on)
        assignMode = false;
}

void Sq5Surface::setChToAll (bool on)
{
    chToAll = on;
    bindStrips();
}

void Sq5Surface::setEqBand (int b)
{
    eqBand = juce::jlimit (0, 3, b);
    bindKnobs();
}

void Sq5Surface::bindKnobs()
{
    auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    boundStrip = s;
    for (auto& [name, k] : knobs)
        if (name != "Phones")
            k->setBinding ({});
    if (s == nullptr)
    {
        lightJewels();
        return;
    }
    auto& p = s->params;

    const int in = p.inputLeft.load();
    if (auto* src = in >= 0 && in < 128 ? ctx.engine.getSource (in) : nullptr)
    {
        auto b = bindLinear ("Preamp", src->gainDb, 0.0f, 60.0f, " dB", 0, 0.0f);
        b.steps = 61.0f;
        getKnob ("Preamp").setBinding (b);
    }
    else
        getKnob ("Preamp").setBinding (bindLinear ("Trim", p.input.trimDb, -24.0f, 24.0f, " dB", 1, 0.0f));
    getKnob ("Pan").setBinding (bindPan ("Pan", p.pan));

    if (s->getKind() == StripKind::Channel)
    {
        getKnob ("HPF").setBinding (bindFreq ("HPF", p.filter().lowCutFreq, 20.0f, 2000.0f, 20.0f));
        getKnob ("Gate").setBinding (bindParam (models::Dynamics, p.gate().model.load(), p.gate().values, "thr", "Gate"));
    }
    if (s->getKind() != StripKind::Aux)
        getKnob ("Comp").setBinding (bindParam (models::Dynamics, p.dynamics().model.load(), p.dynamics().values, "thr", "Comp"));

    const auto table = s->isOutput() ? models::BusEq : models::ChannelEq;
    const juce::String pre = eqBand == 0 ? "l" : eqBand == 1 ? "1" : eqBand == 2 ? "2" : "h";
    auto& eq = p.eq();
    getKnob ("Width").setBinding (bindParam (table, eq.model.load(), eq.values, pre + "q", "Width"));
    getKnob ("Freq").setBinding (bindParam (table, eq.model.load(), eq.values, pre + "f", "Freq"));
    getKnob ("EQ Gain").setBinding (bindParam (table, eq.model.load(), eq.values, pre + "g", "Gain"));
    lightJewels();
}

void Sq5Surface::lightJewels()
{
    // A rotary's centre lights when it has something to do (p.9).
    const std::pair<const char*, juce::Colour> jewels[] = { { "Preamp", kJewelRed }, { "HPF", kJewelGreen },     { "Gate", kJewelOrange }, { "Comp", kJewelOrange },
                                                            { "Pan", kJewelYellow }, { "Width", kJewelYellow }, { "Freq", kJewelGreen },  { "EQ Gain", kJewelRed },
                                                            { "Phones", kJewelOff } };
    for (const auto& [name, colour] : jewels)
    {
        auto& k = getKnob (name);
        const auto c = k.getBinding().isValid() ? colour : kJewelOff;
        k.getSlider().getProperties().set ("jewel", (juce::int64) c.getARGB());
        k.getSlider().repaint();
    }
    rotary.getSlider().getProperties().set ("jewel", (juce::int64) (rotary.getBinding().isValid() ? kJewelYellow : kJewelOff).getARGB());
    rotary.getSlider().repaint();
}

void Sq5Surface::refreshAll()
{
    bindStrips();
    bindKnobs();
    display.refreshStrip();
    for (auto& s : strips)
        s->refresh();
    master->refresh();
    for (auto& k : keys)
        k->refresh();
    repaint();
}

void Sq5Surface::selectionChanged()
{
    bindKnobs();
    if (chToAll)
        bindStrips();
    display.refreshStrip();
}

void Sq5Surface::timerCallback()
{
    if (isCovered())
        return;
    for (auto& s : strips)
        s->refresh();
    master->refresh();
    for (auto& k : keys)
        k->refresh();
    universalKey.refresh();
    if (boundStrip != (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr))
        selectionChanged();
}

void Sq5Surface::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    const auto scale = juce::jmin ((float) getWidth() / kDesignWidth, (float) getHeight() / kDesignHeight);
    design = juce::AffineTransform::scale (scale).translated (((float) getWidth() - scale * kDesignWidth) * 0.5f,
                                                              ((float) getHeight() - scale * kDesignHeight) * 0.5f);

    display.setBounds (570, 12, sq5::Display::kWidth, sq5::Display::kHeight);
    universalKey.setBounds (1806, 12, 104, 26);
    for (int i = 0; i < kStrips; ++i)
        strips[(size_t) i]->setBounds (stripX (i), kStripTop, kStripWidth, kStripBottom - kStripTop);
    master->setBounds (stripX (15) + kStripWidth + 12, kStripTop, kStripWidth, kStripBottom - kStripTop);

    for (auto* child : getChildren())
        child->setTransform (design);
}

juce::Path Sq5Surface::podOutline() const
{
    juce::Path p;
    p.startNewSubPath (100.0f, 14.0f);
    p.lineTo (1744.0f, 14.0f);
    p.lineTo (1744.0f, 232.0f);
    p.lineTo (1500.0f, 462.0f);
    p.lineTo (400.0f, 462.0f);
    p.lineTo (100.0f, 232.0f);
    p.closeSubPath();
    return p.createPathWithRoundedCorners (22.0f);
}

void Sq5Surface::paint (juce::Graphics& g)
{
    const auto& l = look();
    g.fillAll (juce::Colour (0xff0b0b0c));
    g.addTransform (design);
    hw::panel (g, juce::Rectangle<int> (kDesignWidth, kDesignHeight).toFloat(), l);

    // The top pod around the screen.
    juce::Path pod;
    pod = podOutline();
    g.setColour (l.section.brighter (0.05f));
    g.fillPath (pod);
    g.setColour (l.sectionLine.withAlpha (0.55f));
    g.strokePath (pod, juce::PathStrokeType (1.5f));

    // The white bands that link the rotaries, and the In keys' lines to them.
    const auto centre = [this] (const char* name) { return getKnob (name).getBounds().toFloat().getCentre(); };
    const auto band = [&g] (std::initializer_list<juce::Point<float>> points)
    {
        juce::Path p;
        bool first = true;
        for (auto at : points)
        {
            if (first)
                p.startNewSubPath (at);
            else
                p.lineTo (at);
            first = false;
        }
        g.setColour (juce::Colour (0xffe9ecef));
        g.strokePath (p.createPathWithRoundedCorners (40.0f), juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    band ({ centre ("Preamp"), centre ("HPF"), centre ("Gate"), centre ("Comp") });
    band ({ centre ("Width"), centre ("Freq"), centre ("EQ Gain") });
    g.setColour (l.sectionLine);
    for (auto [key, knob_] : { std::pair ("HPF In", "HPF"), std::pair ("Gate In", "Gate"), std::pair ("Comp In", "Comp") })
        if (auto* k = findKey (key))
            g.drawLine (juce::Line<float> (k->getBounds().toFloat().getCentre(), centre (knob_)), 1.0f);

    // Its sections, outlined in teal as on the console.
    const auto box = [&g, &l] (juce::Rectangle<float> r)
    {
        g.setColour (l.sectionLine);
        g.drawRoundedRectangle (r, 7.0f, 1.2f);
    };
    box ({ 28.0f, 170.0f, 80.0f, 272.0f });      // Soft 1-4
    box ({ 112.0f, 384.0f, 150.0f, 58.0f });     // Soft 5-6
    box ({ 1552.0f, 400.0f, 170.0f, 58.0f });    // Soft 7-8
    box ({ 1612.0f, 312.0f, 128.0f, 56.0f });    // GEQ
    box ({ 1808.0f, 112.0f, 84.0f, 104.0f });    // the phones
    box ({ 1800.0f, 262.0f, 112.0f, 206.0f });   // FX
    box ({ 30.0f, 480.0f, 88.0f, 58.0f });       // View
    box ({ 30.0f, 542.0f, 88.0f, 142.0f });      // Pre Fade, Assign, CH to All Mix
    box ({ 30.0f, 700.0f, 88.0f, 134.0f });      // Copy, Paste, Reset
    box ({ 30.0f, 842.0f, 104.0f, 228.0f });     // Layers
    box ({ 136.0f, 478.0f, 1522.0f, 50.0f });    // the LCDs
    box ({ 136.0f, 530.0f, 1522.0f, 156.0f });   // Mute, Sel, PAFL
    box ({ 136.0f, 690.0f, 1522.0f, 384.0f });   // the faders
    box ({ 1664.0f, 530.0f, 98.0f, 156.0f });    // the Master's keys
    box ({ 1664.0f, 690.0f, 98.0f, 384.0f });    // its fader
    box ({ 1800.0f, 476.0f, 112.0f, 598.0f });   // Mix Select

    // The Master's teal tag, and the lit arcs at the pod's top and bottom.
    const juce::Rectangle<float> tag (1672.0f, 694.0f, 82.0f, 15.0f);
    g.setColour (l.sectionLine);
    g.fillRect (tag);
    g.setColour (juce::Colours::black);
    g.setFont (type::font (11.0f, true));
    g.drawText ("Master", tag, juce::Justification::centred);
    g.setColour (l.sectionLine.brighter (0.4f));
    g.fillRoundedRectangle (juce::Rectangle<float> (640.0f, 460.0f, 540.0f, 4.0f), 2.0f);

    for (const auto& s : silks)
        hw::silk (g, s.area, s.text, l, s.size, s.justification);

    // Group rules between the fader groups, the Master's frame.
    g.setColour (l.sectionLine.withAlpha (0.35f));
    for (int i = 1; i < 4; ++i)
        g.drawVerticalLine (stripX (i * 4) - kGroupGap / 2, (float) kStripTop, (float) kStripBottom);

    // The PAFL meter, beside the screen.
    const auto pafl = [this] (int ch) { return juce::Decibels::gainToDecibels (ctx.meters.getPhones (ch), -60.0f); };
    juce::Rectangle<float> meter (1340.0f, 34.0f, 48.0f, 240.0f);
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (meter, 3.0f);
    const float marks[] = { 12.0f, 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -9.0f, -12.0f, -16.0f, -20.0f, -40.0f };
    for (int i = 0; i < 11; ++i)
    {
        const float y = meter.getY() + 10.0f + (float) i * 20.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const bool on = pafl (ch) >= marks[(size_t) i];
            const auto c = marks[(size_t) i] > 3.0f ? juce::Colours::red : marks[(size_t) i] > -6.0f ? juce::Colours::yellow : kGreenLed;
            g.setColour (on ? c : c.withMultipliedBrightness (0.18f));
            g.fillEllipse (ch == 0 ? meter.getX() + 4.0f : meter.getRight() - 12.0f, y, 8.0f, 8.0f);
        }
        const auto t = marks[(size_t) i] > 0.0f ? "+" + juce::String ((int) marks[(size_t) i]) : juce::String ((int) marks[(size_t) i]);
        hw::silk (g, { meter.getX() + 12.0f, y - 2.0f, 24.0f, 12.0f }, t, l, 8.5f);
    }
    hw::silk (g, { meter.getX(), meter.getBottom() + 2.0f, meter.getWidth(), 12.0f }, "PAFL", l, 10.0f);

    hw::silk (g, { 800.0f, 463.0f, 220.0f, 14.0f }, "SQ-5", l, 12.0f);
}

} // namespace livedaw::ui
