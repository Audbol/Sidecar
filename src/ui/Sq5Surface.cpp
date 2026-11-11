#include "ui/Sq5Surface.h"

#include "daw/Session.h"
#include "engine/MidiRemote.h"
#include "engine/Layers.h"
#include "engine/MixerEngine.h"
#include "engine/Models.h"
#include "engine/SqMidi.h"
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
    const juce::Colour kMute (0xfff06a78), kSel (0xff46f5c6), kPafl (0xffffa52e), kBlue (0xffbfe2ff), kWhite (0xfff5f5f5), kRed (0xffff5a5a),
        kScreenKey (0xffd2f7e6), kGreenLed (0xff8ff5c0), kPeach (0xffffc98a);
    // Unlit a key is its rubber's colour: Mute, the SoftKeys and the EQ bands white
    // (lit red behind white, they photograph pink), Sel and two screen keys teal.
    const juce::Colour kFaceWhite (0xffd7dade), kFaceTeal (0xff0e8f76), kFaceGrey (0xff8c8f94), kFaceRed (0xffc61e26), kFaceBlue (0xff1a55c4),
        kFaceLayer (0xffc3ccd7), kFaceMixA (0xff90d9f1), kFaceMixB (0xff42b9ec);
    // The jewels: what each rotary's centre is lit in.
    const juce::Colour kJewelRed (0xffff3a30), kJewelGreen (0xff4cf08a), kJewelOrange (0xffff9a2a), kJewelYellow (0xffffe05a), kJewelOff (0xff4a4d52);

    const juce::Colour kLedRed (0xffff3a30), kLedOrange (0xffffa52e), kLedGreen (0xff55e66e);

    constexpr int kStripWidth = 94, kStripsX = 138, kGroupGap = 4;
    constexpr int kStripTop = 480, kStripBottom = 1072;

    /** The PAFL LED meter, beside the screen. */
    const juce::Rectangle<float> kPaflMeter (1340.0f, 30.0f, 48.0f, 238.0f);

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
            k->windowLit = true;
            addAndMakeVisible (*k);
        }
        mute.cornerRadius = 6.0f;
        mute.face = kFaceWhite;
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
        pafl.onClick = [this] { surface.pressPafl (slot); };
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
        // A DCA's Sel: out of its spill, or into one with Spill On Sel (p.104).
        if (slot.kind == Slot::Dca && (surface.getSpill() == slot.index || surface.isSpillOnSel()))
        {
            surface.setSpill (surface.getSpill() == slot.index ? -1 : slot.index);
            return;
        }
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
            // The send to the selected mix (p.9); a Group has an assignment, no send level.
            if (! master && slot.isSendSource() && bus >= 0 && ! (bus < kMixes && isGroupMix (bus)))
                value = &strip->sends[(size_t) bus].levelDb;
            if (! master && surface.isChToAll() && (slot.kind == Slot::Mix || slot.kind == Slot::FxSend))
                if (auto* sel_ = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr; sel_ != nullptr && sel_->isInput())
                    value = &sel_->sends[(size_t) (slot.kind == Slot::Mix ? slot.index : kMixes + slot.index)].levelDb; // CH to All Mix (p.50)
        }
        if (master && surface.isListenMode())
            value = &ctx.engine.monitorLevelDb; // Master Fader Mode: Listen Level (p.103)
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
    display.onSurfaceChanged = [this]
    {
        bindStrips();
        bindKnobs();
    };
    SqMidi::onSoftKey = [this] (int key, bool down) { pressSoftKey (key, down); };
    display.assignSlot = [this] (int l, int i, const sq5::Slot& s)
    {
        layers[(size_t) l][(size_t) i] = s;
        saveLayers();
        pushShowLayer (l);
        bindStrips();
    };
    loadLayers();
    pullShowLayers();

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
    const auto inKey = [this] (juce::Rectangle<int> r, std::function<std::atomic<bool>*(Strip&)> get, const juce::String& name)
    {
        auto& k = panelKey (name, kGreenLed, r.withSizeKeepingCentre (32, 32), false, kFaceWhite);
        silks.push_back ({ juce::Rectangle<int> (r.getCentreX() - 20, r.getCentreY() - 32, 40, 14).toFloat(), "In", 11.0f, juce::Justification::centred });
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
    inKey ({ 1476, 100, 44, 26 }, [] (Strip& s) { return &s.params.eq().on; }, "EQ In");
    const std::pair<const char*, juce::Rectangle<int>> bands[] = { { "LF", { 1618, 232, 44, 26 } }, { "LM", { 1662, 180, 44, 26 } },
                                                                   { "HM", { 1662, 124, 44, 26 } }, { "HF", { 1662, 66, 44, 26 } } };
    for (int b = 0; b < 4; ++b)
    {
        auto& k = panelKey (bands[(size_t) b].first, kPeach, bands[(size_t) b].second.withSizeKeepingCentre (32, 32), true, kFaceWhite);
        k.isOn = [this, b] { return eqBand == b; };
        k.onClick = [this, b] { setEqBand (b); };
    }
    // The selected mix's GEQ: its In and Pk LEDs are drawn with the panel, Fader Flip
    // is the section's only key (p.38).
    auto& flip = panelKey ("Fader Flip", kWhite, { 1672, 354, 56, 22 }, true, kFaceWhite);
    flip.isOn = [this] { return faderFlip > 0; };
    flip.onClick = [this] { pressFaderFlip(); };

    // The PAFL meter, Talk and the touchscreen rotary.
    auto& talk = panelKey ("Talk", kPafl, { 1336, 312, 56, 30 });
    talk.cornerRadius = 7.0f;
    talk.isOn = [this] { return ctx.engine.talk.paths[0].on.load(); };
    // Held or latched, by Setup's Momentary + Latch (p.96).
    talk.onStateChange = [this, &talk]
    {
        const bool down = talk.isDown();
        if (down != talkHeld)
        {
            talkHeld = down;
            ctx.engine.talkKey (0, down);
        }
    };
    rotary.setHardware (true);
    addAndMakeVisible (rotary);
    rotary.setBounds (1330, 360, 62, 62);

    // SoftKeys 1-8: what they do is Setup > Surface > Soft Controls.
    const juce::Rectangle<int> softs[] = { { 40, 246, 56, 26 },  { 40, 300, 56, 26 },  { 40, 354, 56, 26 },  { 40, 408, 56, 26 },
                                           { 120, 408, 56, 26 }, { 196, 408, 56, 26 }, { 1590, 410, 56, 26 }, { 1670, 410, 56, 26 } };
    for (int i = 0; i < 8; ++i)
    {
        auto& k = panelKey ("Soft " + juce::String (i + 1), kMute, softs[(size_t) i], true, kFaceWhite);
        k.cornerRadius = 6.0f;
        k.onClick = [this, i]
        {
            pressSoftKey (i, true);
            pressSoftKey (i, false);
        };
        k.isOn = [this, i] { return isSoftKeyLit (i); };
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
        silks.push_back ({ juce::Rectangle<int> (100, 858 + l * 34, 18, 16).toFloat(), letter, 12.0f, juce::Justification::centred });
    }

    // The right column: FX 1-4, LR, Mix 1-12.
    for (int f = 0; f < sq5::kFxSends; ++f)
    {
        auto& k = panelKey ("FX " + juce::String (f + 1), kBlue, { 1832, 286 + f * 44, 56, 26 }, false, kFaceBlue);
        k.isOn = [this, f] { return mix == 13 + f; };
        k.onClick = [this, f] { setMix (13 + f); };
        silks.push_back ({ juce::Rectangle<int> (1806, 286 + f * 44, 24, 26).toFloat(), juce::String (f + 1), 12.0f, juce::Justification::centred });
    }
    auto& lr = panelKey ("LR", kBlue, { 1687, 498, 52, 24 }, true, kFaceBlue);
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
    panelKnob ("Phones", { 1820, 148, 56, 56 }).setBinding (bindFader ("Phones", ctx.engine.phones.levelDb));

    // The strips, their panel silkscreen.
    silks.push_back ({ { 1760.0f, 486.0f, 60.0f, 14.0f }, "Mix", 11.0f, juce::Justification::centred });
    silks.push_back ({ { 1760.0f, 500.0f, 60.0f, 14.0f }, "Select", 11.0f, juce::Justification::centred });
    silks.push_back ({ { 1832.0f, 536.0f, 56.0f, 14.0f }, "Mix", 11.0f, juce::Justification::centred });
    silks.push_back ({ { 1808.0f, 44.0f, 84.0f, 16.0f }, "ST3 In", 11.0f, juce::Justification::centred });
    silks.push_back ({ { 1810.0f, 264.0f, 70.0f, 16.0f }, "FX", 12.0f, juce::Justification::centred });
    silks.push_back ({ { 44.0f, 846.0f, 70.0f, 14.0f }, "Layers", 11.0f, juce::Justification::centredLeft });

    hw::applyTo (*this, look(), &universalKey);
    setLayer (0);
    bindKnobs();
    startTimerHz (20);
}

Sq5Surface::~Sq5Surface()
{
    SqMidi::onSoftKey = nullptr;
}

ToggleKey& Sq5Surface::panelKey (const juce::String& name, juce::Colour colour, juce::Rectangle<int> r, bool labelAbove, juce::Colour face)
{
    auto k = std::make_unique<ToggleKey> (name, colour, theme);
    k->shape = ToggleKey::Shape::Oval;
    k->windowLit = true;
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
    // Printed as on the datasheet's p.2: to the right of the processing rotaries with
    // what they turn under the name, to the left of the EQ's, under Pan.
    const bool left = name == "Width" || name == "Freq" || name == "EQ Gain", below = name == "Pan";
    const juce::Rectangle<int> labelArea = below  ? juce::Rectangle<int> (r.getX() - 8, r.getBottom() + 2, r.getWidth() + 16, 15)
                                           : left ? juce::Rectangle<int> (r.getX() - 68, r.getY() + 12, 66, 16)
                                                  : juce::Rectangle<int> (r.getRight() + 2, r.getY() + 8, 64, 16);
    if (name != "Phones")
        silks.push_back ({ labelArea.toFloat(), name == "EQ Gain" ? juce::String ("Gain") : name, 13.0f,
                           below ? juce::Justification::centred : left ? juce::Justification::centredRight : juce::Justification::centredLeft });
    const char* under = name == "Preamp" ? "Gain" : name == "HPF" ? "Freq" : name == "Gate" || name == "Comp" ? "Thres" : nullptr;
    if (under != nullptr)
        silks.push_back ({ juce::Rectangle<int> (r.getRight() + 2, r.getY() + 25, 40, 12).toFloat(), under, 9.5f, juce::Justification::centredLeft });
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

void Sq5Surface::setSpill (int dca)
{
    spill = dca;
    bindStrips();
}

bool Sq5Surface::isListenMode() const { return sq5::prefs::get (ctx.settings, sq5::prefs::listen, false); }

void Sq5Surface::pressPafl (const sq5::Slot& slot)
{
    using namespace sq5;
    auto& e = ctx.engine;
    const bool dca = slot.kind == Slot::Dca;
    auto* strip = stripOf (e, slot);
    if (! dca && strip == nullptr)
        return;
    const bool on = ! (dca ? e.getDca (slot.index).solo.load() : strip->params.solo.load());
    const auto forAll = [&e] (auto&& action)
    {
        for (const auto* list : { &e.getChannels(), &e.getAux(), &e.getBuses(), &e.getMains(), &e.getMatrices() })
            for (const auto& s : *list)
                action (*s);
    };
    if (on && ! prefs::get (ctx.settings, prefs::additive, false))
    {
        // Not additive: this replaces whatever was listened to.
        forAll ([] (Strip& s) { s.params.solo = false; });
        for (int d = 0; d < e.getCounts().dcas; ++d)
            e.getDca (d).solo = false;
    }
    if (dca)
        e.getDca (slot.index).solo = on;
    else
        strip->params.solo = on;
    if (! dca && on && prefs::get (ctx.settings, prefs::selFollow, true) && ctx.selectStrip)
        ctx.selectStrip (strip);
    if (prefs::get (ctx.settings, prefs::overrideMix, true) && prefs::get (ctx.settings, prefs::additive, false))
    {
        // An input's PAFL takes the mixes off the bus; the last input off brings them back.
        const auto isMix = [] (const Strip& s) { return s.isOutput(); };
        bool anyInput = false;
        forAll ([&anyInput, &isMix] (Strip& s) { anyInput = anyInput || (s.params.solo.load() && ! isMix (s)); });
        if (! dca && strip->isInput() && on)
        {
            forAll ([this, &isMix] (Strip& s)
                    {
                        if (isMix (s) && s.params.solo.load())
                        {
                            s.params.solo = false;
                            overriddenMixes.push_back (&s);
                        }
                    });
        }
        else if (! anyInput)
        {
            for (auto* s : overriddenMixes)
                s->params.solo = true;
            overriddenMixes.clear();
        }
    }
}

void Sq5Surface::tapTempo (int slot)
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    auto& last = lastTapMs[(size_t) (slot < 0 ? 8 : slot)];
    const double interval = now - last;
    last = now;
    if (interval > 3000.0 || interval < 40.0)
        return; // the first tap, or a slip
    auto& fx = ctx.engine.getFx();
    for (int i = 0; i < 8; ++i)
    {
        if (slot >= 0 && slot != i)
            continue;
        const auto& m = models::model (models::Fx, fx.getModel (i));
        if (const int p = m.find ("time"); p >= 0)
            fx.params (i).values[p] = (float) interval;
    }
}

bool Sq5Surface::isSoftKeyLit (int key) const
{
    using namespace sq5;
    const auto a = soft::get (ctx.settings, key);
    auto& e = ctx.engine;
    switch (a.type)
    {
        case soft::ChannelMute:
            if (auto* s = stripOf (e, soft::channelAt (a.option2)))
                return s->params.mute.load();
            return false;
        case soft::ChannelPafl:
            if (auto* s = stripOf (e, soft::channelAt (a.option2)))
                return s->params.solo.load();
            return false;
        case soft::DcaMute:   return e.getDca (a.option2).mute.load();
        case soft::DcaPafl:   return e.getDca (a.option2).solo.load();
        case soft::DcaSpill:  return a.option2 == 0 ? spillOnSel : spill == a.option2 - 1;
        case soft::MuteGroup: return e.muteGroups.contains (a.option2);
        case soft::Talk:      return e.talk.paths[0].on.load();
        case soft::MixSelect: return mix == a.option2;
        case soft::AmmChannel:
            if (auto* amm = ld::ld_strip_amm (e.getChannels()[(size_t) juce::jlimit (0, 47, a.option2)]->getHandle()))
                return amm->amm.load() >= 0 && amm->on.load();
            return false;
        // SQ Drive: the Start / Pause key while it runs, the Arm keys while it records.
        case soft::DriveMultitrack:
            if (ctx.recording == nullptr)
                return false;
            return a.option2 == 2 ? ctx.recording->isRolling() : (a.option2 == 3 || a.option2 == 5) && ctx.recording->isRecording();
        case soft::DrivePlay:   return a.option2 == 2 && e.getUsbRecorder().getPlayState() == UsbRecorder::PlayState::Playing;
        case soft::DriveRecord:
            return a.option2 == 1 ? e.getUsbRecorder().getRecState() == UsbRecorder::RecState::Recording : a.option2 >= 2 && e.getUsbRecorder().isRecording();
        default:              return false;
    }
}

void Sq5Surface::bindStrips()
{
    using sq5::Slot;
    auto slots = layers[(size_t) layer];
    // A stereo pair's even channel goes off the surface (p.85).
    const auto& channels = ctx.engine.getChannels();
    for (auto& s : slots)
        if (s.kind == Slot::Input && (s.index & 1) != 0 && juce::isPositiveAndBelow (s.index, (int) channels.size()) && channels[(size_t) (s.index - 1)]->params.stereo.load())
            s = {};
    if (spill >= 0)
    {
        // Spill (p.104): the DCA's members in order, the DCA itself on the last strip.
        std::vector<Slot> members;
        const auto add = [this, &members] (const MixerEngine::StripList& list, Slot::Kind kind, int first, int count)
        {
            for (int i = 0; i < count && first + i < (int) list.size(); ++i)
                if (list[(size_t) (first + i)]->params.dcaMask.contains (spill))
                    members.push_back ({ kind, i });
        };
        add (ctx.engine.getChannels(), Slot::Input, 0, 48);
        add (ctx.engine.getAux(), Slot::FxReturn, 0, 8);
        add (ctx.engine.getBuses(), Slot::Mix, 0, sq5::kMixes);
        add (ctx.engine.getBuses(), Slot::FxSend, sq5::kMixes, sq5::kFxSends);
        for (int i = 0; i < kStrips; ++i)
            slots[(size_t) i] = i < (int) members.size() && i < kStrips - 1 ? members[(size_t) i] : Slot {};
        slots[(size_t) (kStrips - 1)] = { Slot::Dca, spill };
    }
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
    const int next = juce::jlimit (0, sq5::kMixes + sq5::kFxSends, m);
    // PAFL Follow Mix (p.94): the master strip's PAFL goes with the mix.
    if (next != mix && sq5::prefs::get (ctx.settings, sq5::prefs::followMix, true))
    {
        auto* was = sq5::stripOf (ctx.engine, sq5::slotOfMix (mix));
        auto* now = sq5::stripOf (ctx.engine, sq5::slotOfMix (next));
        if (was != nullptr && now != nullptr && was->params.solo.load())
        {
            was->params.solo = false;
            now->params.solo = true;
        }
    }
    mix = next;
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

void Sq5Surface::pressSoftKey (int key, bool down)
{
    runAssign (sq5::soft::get (ctx.settings, key), down);
}

void Sq5Surface::pressFootswitch (int which, bool down)
{
    runAssign (sq5::soft::getFootswitch (ctx.settings, which), down);
}

void Sq5Surface::runAssign (const sq5::soft::Assign& a, bool down)
{
    using namespace sq5;
    auto& e = ctx.engine;
    auto* midi = SqMidi::current();
    if (a.type == soft::DawControl)
    {
        // Bank Up and Bank Down (p.117) go to the DAW the app's DAW remote controls, where
        // the console has one: its BK > and BK < keys. The SQ's own message to A&H MIDI
        // Control isn't in the MIDI protocol document.
        if (ctx.midi != nullptr)
            if (const int command = DawRemote::findCommand (a.option2 == 0 ? "N7" : "N6"); command >= 0)
                ctx.midi->getDaw().pressCommand (command, down);
        return;
    }
    // Those that act on the release too.
    if (a.type == soft::Talk)
    {
        e.talkKey (0, down);
        return;
    }
    if (a.type == soft::MidiNote)
    {
        if (midi != nullptr)
            midi->sendMessage (down ? juce::MidiMessage::noteOn (a.option1 + 1, a.option2, (juce::uint8) 127) : juce::MidiMessage::noteOff (a.option1 + 1, a.option2));
        return;
    }
    if (! down)
        return;
    switch (a.type)
    {
        case soft::AmmChannel:
            if (auto* amm = ld::ld_strip_amm (e.getChannels()[(size_t) juce::jlimit (0, 47, a.option2)]->getHandle()))
                amm->on = ! amm->on.load();
            break;
        case soft::AmmInOut:
        {
            bool in = false;
            for (const auto& c : e.getChannels())
                if (auto* amm = ld::ld_strip_amm (c->getHandle()); amm != nullptr && amm->amm.load() == a.option2)
                    in = in || amm->on.load();
            for (const auto& c : e.getChannels())
                if (auto* amm = ld::ld_strip_amm (c->getHandle()); amm != nullptr && amm->amm.load() == a.option2)
                    amm->on = ! in;
            break;
        }
        case soft::ChannelMute:
            if (auto* s = stripOf (e, soft::channelAt (a.option2)))
                s->params.mute = ! s->params.mute.load();
            break;
        case soft::ChannelPafl:
            pressPafl (soft::channelAt (a.option2));
            break;
        case soft::DcaMute:
            e.getDca (a.option2).mute = ! e.getDca (a.option2).mute.load();
            break;
        case soft::DcaPafl:
            pressPafl ({ Slot::Dca, a.option2 });
            break;
        case soft::DcaSpill:
            if (a.option2 == 0)
            {
                spillOnSel = ! spillOnSel;
                if (! spillOnSel)
                    setSpill (-1); // deactivating it exits any spill (p.104)
            }
            else
                setSpill (spill == a.option2 - 1 ? -1 : a.option2 - 1);
            break;
        case soft::MidiProgram:
            if (midi != nullptr)
                midi->sendMessage (juce::MidiMessage::programChange (a.option1 + 1, a.option2));
            break;
        case soft::MixSelect:
            setMix (a.option2);
            break;
        case soft::Mmc:
            if (midi != nullptr)
            {
                const juce::MidiMessage::MidiMachineControlCommand commands[] = { juce::MidiMessage::mmc_rewind, juce::MidiMessage::mmc_play, juce::MidiMessage::mmc_pause,
                                                                                 juce::MidiMessage::mmc_stop, juce::MidiMessage::mmc_fastforward, juce::MidiMessage::mmc_recordStart };
                midi->sendMessage (juce::MidiMessage::midiMachineControlCommand (commands[juce::jlimit (0, 5, a.option2)]));
            }
            break;
        case soft::MuteGroup:
            e.muteGroups.set (a.option2, ! e.muteGroups.contains (a.option2));
            break;
        case soft::PaflClear:
            for (const auto* list : { &e.getChannels(), &e.getAux(), &e.getBuses(), &e.getMains(), &e.getMatrices() })
                for (const auto& s : *list)
                    s->params.solo = false;
            for (int d = 0; d < e.getCounts().dcas; ++d)
                e.getDca (d).solo = false;
            overriddenMixes.clear();
            break;
        case soft::RecallScene:
            if (a.option2 < ctx.show.getNumScenes())
                ctx.show.recallScene (a.option2);
            break;
        case soft::SceneNext:
            display.stepSceneSelection (1);
            break;
        case soft::ScenePrevious:
            display.stepSceneSelection (-1);
            break;
        case soft::SceneGo:
            display.recallSelectedScene (a.option1 == 1);
            break;
        case soft::SceneStore:
            if (const int at = ctx.show.getCurrentSceneIndex(); at >= 0)
                ctx.show.storeScene (at);
            break;
        case soft::Select:
        {
            const auto slot = getLayerSlots (a.option1)[(size_t) juce::jlimit (0, kStrips - 1, a.option2)];
            if (auto* s = stripOf (e, slot); s != nullptr && ctx.selectStrip)
                ctx.selectStrip (s);
            break;
        }
        case soft::TapTempo:
            tapTempo (a.option2);
            break;
        case soft::GlobalTap:
            tapTempo (-1);
            break;
        // SQ Drive (p.89's lists): Sidecar's multitrack recorder and its USB recorder, by the
        // display's transports. Their options to the transports' actions: 0 previous, 1 stop,
        // 2 start / pause, 3 arm, 4 next, 5 arm / record / stop.
        case soft::DriveMultitrack:
            display.driveMultitrack (juce::jlimit (0, 5, a.option2));
            break;
        case soft::DrivePlay:
        {
            const int actions[] = { 0, 1, 2, 4 };
            display.driveStereoPlay (actions[juce::jlimit (0, 3, a.option2)]);
            break;
        }
        case soft::DriveRecord:
        {
            const int actions[] = { 1, 2, 3, 5 };
            display.driveStereoRecord (actions[juce::jlimit (0, 3, a.option2)]);
            break;
        }
        default:
            break;
    }
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

namespace
{
    sq5::Slot sq5SlotOf (MixerEngine& e, const state::FaderRef& f)
    {
        if (f.dca)
            return { sq5::Slot::Dca, f.dcaIndex };
        if (! f.isStrip())
            return {};
        auto* strip = state::findStrip (e, f.strip);
        if (strip == nullptr)
            return {};
        const auto s = sq5::slotOf (e, strip);
        return sq5::stripOf (e, s) == strip ? s : sq5::Slot {}; // a main past the first has no slot
    }

    state::FaderRef sq5FaderOf (MixerEngine& e, const sq5::Slot& s)
    {
        if (s.kind == sq5::Slot::None)
            return state::FaderRef::blank();
        if (s.kind == sq5::Slot::Dca)
            return state::FaderRef::ofDca (s.index);
        if (auto* strip = sq5::stripOf (e, s))
            return state::FaderRef::of (state::StripRef::of (*strip));
        return state::FaderRef::blank();
    }
} // namespace

bool Sq5Surface::pullShowLayers()
{
    bool changed = false;
    for (int l = 0; l < kLayers; ++l)
        if (const auto* u = layerlink::layerFor (ctx.show.layers.getUserLayers(), "sq5", juce::String (l)))
            changed |= layerlink::pullInto (*u, layers[(size_t) l], [this] (const state::FaderRef& f) { return sq5SlotOf (ctx.engine, f); });
    if (changed)
        saveLayers();
    return changed;
}

void Sq5Surface::pushShowLayer (int l)
{
    if (const auto* u = layerlink::layerFor (ctx.show.layers.getUserLayers(), "sq5", juce::String (l)))
    {
        const auto copy = *u; // the setter refreshes the snapshot
        layerlink::pushFrom (ctx.show.layers, copy, layers[(size_t) l], [this] (const sq5::Slot& s) { return sq5FaderOf (ctx.engine, s); },
                             [this] (const state::FaderRef& f) { return sq5SlotOf (ctx.engine, f); });
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
    // Preamp On Surface off: the rotary is Trim's (p.92).
    auto* src = in >= 0 && in < 128 && sq5::prefs::get (ctx.settings, sq5::prefs::preampOnSurface, true) ? ctx.engine.getSource (in) : nullptr;
    if (src != nullptr)
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
                                                            { "Pan", kJewelOrange }, { "Width", kJewelOrange }, { "Freq", kJewelGreen },  { "EQ Gain", kJewelRed },
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
    if (++linkTick % 4 == 0 && pullShowLayers()) // a layer made or edited in the Universal Mixer or on another surface
    {
        if (display.onSurfaceChanged)
            display.onSurfaceChanged(); // rebinds the strips and knobs
    }
    for (auto& s : strips)
        s->refresh();
    master->refresh();
    for (auto& k : keys)
        k->refresh();
    universalKey.refresh();
    if (boundStrip != (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr))
        selectionChanged();
    const auto repaintDesign = [this] (juce::Rectangle<float> r) { repaint (r.transformedBy (design).getSmallestIntegerContainer().expanded (1)); };
    if (const int leds = panelLeds(); leds != shownLeds)
    {
        shownLeds = leds;
        repaintDesign ({ 100.0f, 14.0f, 1644.0f, 400.0f });
    }
    repaintDesign (kPaflMeter);
}

int Sq5Surface::panelLeds() const
{
    // The panel's little LEDs, one bit each.
    int leds = 0;
    const auto light = [&leds] (int bit, bool on) { leds |= on ? (1 << bit) : 0; };
    if (auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr)
    {
        const float input = juce::jmax (ctx.meters.get (s, MeterPoint::Input, 0), ctx.meters.get (s, MeterPoint::Input, 1));
        const float output = juce::jmax (ctx.meters.get (s, MeterPoint::Output, 0), ctx.meters.get (s, MeterPoint::Output, 1));
        light (0, input > 0.89f);                                                         // Preamp Pk
        light (1, s->getKind() == StripKind::Channel && s->meters.gateReductionDb.load() > 1.0f); // Gate GR
        light (2, s->meters.dynReductionDb.load() > 1.0f);                                 // Comp GR
        light (3, output > 0.89f);                                                         // Comp Pk
        light (7, output > 0.89f);                                                         // EQ Pk
        const float pan = s->params.pan.load();
        light (4, pan < -3.0f);                                                            // Pan L, centre, R
        light (5, std::abs (pan) <= 3.0f);
        light (6, pan > 3.0f);
        light (10, s->params.preInsert().on.load() && s->params.preInsert().fx.load() >= 0); // Insert Active (p.37)
    }
    if (auto* geq = selectedGeq())
        light (9, geq->on.load());                                                         // GEQ In (p.38)
    if (auto* m = sq5::stripOf (ctx.engine, sq5::slotOfMix (mix)))
        light (8, juce::jmax (ctx.meters.get (m, MeterPoint::Output, 0), ctx.meters.get (m, MeterPoint::Output, 1)) > 0.89f); // GEQ Pk
    light (11, ctx.engine.anySoloActive.load());                                           // PAFL (p.52)
    return leds;
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
    maximiser.layout();
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
    {
        // SoftKeys 1-4 in a column, 5 and 6 on along the bottom: one outline, an L.
        juce::Path softs;
        softs.startNewSubPath (28.0f, 226.0f);
        softs.lineTo (108.0f, 226.0f);
        softs.lineTo (108.0f, 386.0f);
        softs.lineTo (262.0f, 386.0f);
        softs.lineTo (262.0f, 442.0f);
        softs.lineTo (28.0f, 442.0f);
        softs.closeSubPath();
        g.setColour (l.sectionLine);
        g.strokePath (softs.createPathWithRoundedCorners (7.0f), juce::PathStrokeType (1.2f));
    }
    box ({ 1576.0f, 392.0f, 150.0f, 56.0f });    // Soft 7-8
    box ({ 1330.0f, 294.0f, 68.0f, 52.0f });     // Talk
    box ({ 1808.0f, 108.0f, 84.0f, 114.0f });    // the phones
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
    box ({ 1664.0f, 476.0f, 248.0f, 54.0f });    // LR and Mix Select
    box ({ 1800.0f, 532.0f, 112.0f, 542.0f });   // the Mix keys

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

    // The panel's little LEDs: dim until they have something to say.
    const int leds = panelLeds();
    const auto led = [&g, leds] (float x, float y, int bit, juce::Colour c)
    {
        const bool on = (leds & (1 << bit)) != 0;
        g.setColour (on ? c : c.withMultipliedBrightness (0.22f));
        g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
        g.setColour (juce::Colours::black);
        g.drawEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f, 0.8f);
    };
    const auto tiny = [&g, &l] (juce::Rectangle<float> r, const juce::String& t, juce::Justification j = juce::Justification::centredLeft) { hw::silk (g, r, t, l, 9.5f, j); };
    tiny ({ 190.0f, 92.0f, 18.0f, 12.0f }, "Pk");
    led (214.0f, 98.0f, 0, kLedRed);        // Preamp Pk, down and left of the rotary
    led (378.0f, 216.0f, 1, kLedRed);       // Gate GR
    tiny ({ 384.0f, 210.0f, 20.0f, 12.0f }, "GR");
    led (468.0f, 284.0f, 2, kLedRed);       // Comp GR
    tiny ({ 474.0f, 278.0f, 20.0f, 12.0f }, "GR");
    tiny ({ 318.0f, 328.0f, 18.0f, 12.0f }, "Pk");
    led (342.0f, 334.0f, 3, kLedRed);       // Comp Pk
    tiny ({ 1449.0f, 254.0f, 18.0f, 12.0f }, "Pk");
    led (1472.0f, 260.0f, 7, kLedRed);      // EQ Pk

    // Pan: three LEDs over the rotary, L and R beside them.
    tiny ({ 424.0f, 334.0f, 14.0f, 14.0f }, "L", juce::Justification::centred);
    tiny ({ 470.0f, 334.0f, 14.0f, 14.0f }, "R", juce::Justification::centred);
    led (444.0f, 343.0f, 4, kLedGreen);
    led (454.0f, 339.0f, 5, kLedGreen);
    led (464.0f, 343.0f, 6, kLedGreen);

    // Insert Active: an LED in a printed box, not a key (p.37).
    {
        const juce::Rectangle<float> insert (420.0f, 138.0f, 72.0f, 30.0f);
        g.setColour (l.silk.withAlpha (0.7f));
        g.drawRoundedRectangle (insert, 3.0f, 1.0f);
        hw::silk (g, { 424.0f, 141.0f, 44.0f, 12.0f }, "Insert", l, 9.5f, juce::Justification::centredLeft);
        hw::silk (g, { 424.0f, 153.0f, 44.0f, 12.0f }, "Active", l, 9.5f, juce::Justification::centredLeft);
        led (482.0f, 153.0f, 10, kLedGreen);
    }

    // The EQ's band keys in their chain, and its name.
    if (auto* hf = findKey ("HF"))
    {
        g.setColour (l.sectionLine);
        const auto keyCentre = [this] (const char* n) { return findKey (n)->getBounds().toFloat().getCentre(); };
        g.drawLine (juce::Line<float> (hf->getBounds().toFloat().getCentre(), keyCentre ("HM")), 1.0f);
        g.drawLine (juce::Line<float> (keyCentre ("HM"), keyCentre ("LM")), 1.0f);
        g.drawLine (juce::Line<float> (keyCentre ("LM"), keyCentre ("LF")), 1.0f);
    }
    hw::silk (g, { 1548.0f, 270.0f, 44.0f, 20.0f }, "EQ", l, 16.0f, juce::Justification::centred);

    // GEQ: its printed shape, the slant along the pod's edge, its band; Pk and In are
    // LEDs (p.38).
    {
        juce::Path outline;
        outline.startNewSubPath (1630.0f, 380.0f);
        outline.lineTo (1630.0f, 354.0f);
        outline.lineTo (1677.0f, 308.0f);
        outline.lineTo (1740.0f, 308.0f);
        outline.lineTo (1740.0f, 380.0f);
        outline.closeSubPath();
        g.setColour (l.sectionLine);
        g.strokePath (outline.createPathWithRoundedCorners (6.0f), juce::PathStrokeType (1.2f));
        const juce::Rectangle<float> label (1654.0f, 328.0f, 86.0f, 12.0f);
        g.setColour (juce::Colours::white);
        g.fillRect (label);
        g.setColour (juce::Colours::black);
        g.setFont (type::font (10.5f, true));
        g.drawText ("GEQ", label, juce::Justification::centred);
        tiny ({ 1707.0f, 313.0f, 18.0f, 12.0f }, "Pk");
        led (1730.0f, 319.0f, 8, kLedRed);
        tiny ({ 1642.0f, 344.0f, 22.0f, 12.0f }, "In", juce::Justification::centred);
        led (1653.0f, 364.0f, 9, kLedGreen);
    }

    // Pre Fade and Assign join to the Sel keys: printed "+" and bracket.
    {
        g.setColour (l.sectionLine);
        g.drawLine (100.0f, 575.0f, 112.0f, 575.0f, 1.0f);
        g.drawLine (100.0f, 619.0f, 112.0f, 619.0f, 1.0f);
        g.drawLine (112.0f, 575.0f, 112.0f, 619.0f, 1.0f);
        g.drawLine (112.0f, 597.0f, 158.0f, 597.0f, 1.0f);
        hw::silk (g, { 113.0f, 584.0f, 16.0f, 12.0f }, "+", l, 11.0f);
    }

    // LR's line to the Mix Select label.
    g.setColour (l.sectionLine);
    g.drawLine (1739.0f, 507.0f, 1762.0f, 507.0f, 1.0f);

    // Top right: ST3 In, the USB port, the phones socket and its scale.
    {
        const auto socket = [&g] (float cx, float cy, float radius)
        {
            g.setColour (juce::Colour (0xff9ea2a6));
            g.fillEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
            g.setColour (juce::Colour (0xff15161a));
            g.fillEllipse (cx - radius * 0.55f, cy - radius * 0.55f, radius * 1.1f, radius * 1.1f);
        };
        socket (1826.0f, 72.0f, 6.0f);
        // The USB port, drawn plainly.
        const juce::Rectangle<float> usb (1840.0f, 62.0f, 36.0f, 18.0f);
        g.setColour (juce::Colour (0xff222428));
        g.fillRoundedRectangle (usb, 2.0f);
        g.setColour (juce::Colour (0xff9ea2a6));
        g.drawRoundedRectangle (usb, 2.0f, 1.0f);
        g.fillRect (usb.getX() + 5.0f, usb.getY() + 6.0f, usb.getWidth() - 10.0f, 6.0f);
        hw::silk (g, { 1840.0f, 84.0f, 36.0f, 12.0f }, "USB", l, 9.0f);
        // The phones socket, and a headphone mark.
        socket (1848.0f, 128.0f, 11.0f);
        juce::Path phones;
        phones.addCentredArc (1878.0f, 127.0f, 7.0f, 7.0f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.setColour (l.silk);
        g.strokePath (phones, juce::PathStrokeType (1.4f));
        g.fillRoundedRectangle (1869.5f, 125.0f, 3.0f, 7.0f, 1.0f);
        g.fillRoundedRectangle (1883.0f, 125.0f, 3.0f, 7.0f, 1.0f);
        // The phones rotary's scale, 0 to 10.
        g.setColour (l.sectionLine.brighter (0.4f));
        const float cx = 1848.0f, cy = 176.0f;
        for (int t = 0; t <= 8; ++t)
        {
            const float a = juce::MathConstants<float>::pi * (-0.75f + 1.5f * (float) t / 8.0f);
            g.drawLine (cx + std::sin (a) * 31.0f, cy - std::cos (a) * 31.0f, cx + std::sin (a) * 35.0f, cy - std::cos (a) * 35.0f, 1.0f);
        }
        tiny ({ 1814.0f, 205.0f, 16.0f, 12.0f }, "0", juce::Justification::centred);
        tiny ({ 1866.0f, 205.0f, 18.0f, 12.0f }, "10", juce::Justification::centred);
    }

    // Group rules between the fader groups, the Master's frame.
    g.setColour (l.sectionLine.withAlpha (0.35f));
    for (int i = 1; i < 4; ++i)
        g.drawVerticalLine (stripX (i * 4) - kGroupGap / 2, (float) kStripTop, (float) kStripBottom);

    // The PAFL meter, beside the screen.
    const auto pafl = [this] (int ch) { return juce::Decibels::gainToDecibels (ctx.meters.getPhones (ch), -60.0f); };
    const juce::Rectangle<float> meter = kPaflMeter;
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (meter, 3.0f);
    hw::silk (g, { meter.getX() + 1.0f, meter.getY() + 3.0f, 14.0f, 12.0f }, "L", l, 9.0f);
    hw::silk (g, { meter.getRight() - 15.0f, meter.getY() + 3.0f, 14.0f, 12.0f }, "R", l, 9.0f);
    // The 12 segments, the peak row on top (p.52): red, three amber, the rest green.
    const char* const scale[] = { "Pk", "+12", "+6", "+3", "0", "-3", "-6", "-9", "-12", "-16", "-20", "-40" };
    const float marks[] = { 17.0f, 12.0f, 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -9.0f, -12.0f, -16.0f, -20.0f, -40.0f };
    const float rowH = (meter.getHeight() - 20.0f) / 12.0f;
    for (int i = 0; i < 12; ++i)
    {
        const float y = meter.getY() + 17.0f + (float) i * rowH;
        for (int ch = 0; ch < 2; ++ch)
        {
            const bool on = pafl (ch) >= marks[(size_t) i];
            const auto c = i == 0 ? juce::Colours::red : i <= 3 ? kLedOrange : kLedGreen;
            g.setColour (on ? c : c.withMultipliedBrightness (0.18f));
            g.fillEllipse (ch == 0 ? meter.getX() + 4.0f : meter.getRight() - 12.0f, y, 8.0f, 8.0f);
        }
        hw::silk (g, { meter.getX() + 12.0f, y - 2.0f, 24.0f, 12.0f }, scale[i], l, 8.5f);
    }
    led (meter.getCentreX(), meter.getBottom() + 9.0f, 11, kLedOrange);
    hw::silk (g, { meter.getX(), meter.getBottom() + 14.0f, meter.getWidth(), 12.0f }, "PAFL", l, 10.0f);

    hw::silk (g, { 800.0f, 463.0f, 220.0f, 14.0f }, "SQ-5", l, 12.0f);
}

} // namespace livedaw::ui
