#include "ui/X32Surface.h"
#include "ui/HardwareLook.h"

#include "ui/MeterBank.h"

namespace livedaw::ui
{

using namespace universal;

namespace
{
    constexpr int kTopBar = 48;
    constexpr int kLayerColumn = 58;  // the layer keys' columns
    constexpr int kRightPanel = 300;  // SCENES, ASSIGN, MUTE GROUPS
    const juce::Colour kLayerLit (0xff62f0a6), kKeyLit (0xffffc83a);
    // The keys unlit: pale translucent rubber (the strips' and keys' close-ups).
    const juce::Colour kKeyFace (0xffc9ccca);

    /** An input layer: its key, the strips (0 channels, 1 aux, 2 buses) from the
        first, and the name printed under its key where it isn't the key's. */
    struct InputLayerSpec
    {
        const char* name;
        int list, first;
        const char* printed = nullptr;
    };
    constexpr InputLayerSpec kFullLayers[] = { { "CH 1-16", 0, 0 },
                                               { "CH 17-32", 0, 16 },
                                               { "AUX / FX RTN", 1, 0, "AUX IN / USB\nFX RETURNS" },
                                               { "BUS MASTER", 2, 0 } };
    constexpr InputLayerSpec kCompactLayers[] = { { "CH 1-8", 0, 0 },        { "CH 9-16", 0, 8 }, { "CH 17-24", 0, 16 }, { "CH 25-32", 0, 24 },
                                                  { "AUX IN / USB", 1, 0 }, { "FX RTN", 1, 8 },  { "BUS 1-8", 2, 0 },   { "BUS 9-16", 2, 8 } };

    std::vector<InputLayerSpec> layersOf (X32Surface::Model m)
    {
        if (m == X32Surface::Model::Compact)
            return { std::begin (kCompactLayers), std::end (kCompactLayers) };
        return { std::begin (kFullLayers), std::end (kFullLayers) };
    }
} // namespace

using x32::kKeyHeight;
using x32::kScribble;


//==============================================================================
X32Surface::X32Surface (UiContext& c)
    : ctx (c), theme (Theme::dark()), universalKey ("UNIVERSAL MIXER", theme.accent, theme),
      sendsOnFaderKey ("SENDS ON FADER", kLayerLit, theme), clearSolo ("CLEAR SOLO", x32::kSoloLit, theme), remoteKey ("REMOTE", kLayerLit, theme), channel (c, theme), monitor (c, theme),
      assign (c, theme), display (c, theme)
{
    for (auto& s : inputs)
    {
        s = std::make_unique<x32::FaderStrip> (ctx, theme);
        addAndMakeVisible (*s);
    }
    for (auto& s : groups)
    {
        s = std::make_unique<x32::FaderStrip> (ctx, theme);
        s->setGroupBank();
        addAndMakeVisible (*s);
    }
    main = std::make_unique<x32::FaderStrip> (ctx, theme);
    addAndMakeVisible (*main);

    model = ctx.settings.getValue (modelKey) == "COMPACT" ? Model::Compact : Model::Full;
    buildInputLayerKeys();
    for (int i = 0; i < 16; ++i)
        inputs[(size_t) i]->setVisible (i < getNumInputFaders());
    const std::pair<const char*, GroupLayer> groupLayers[] = { { "DCA 1-8", GroupLayer::Dca },
                                                               { "BUS 1-8", GroupLayer::Bus1to8 },
                                                               { "BUS 9-16", GroupLayer::Bus9to16 },
                                                               { "MTX 1-6 / M/C", GroupLayer::MatrixMain } };
    for (auto [name, layer] : groupLayers)
    {
        auto key = std::make_unique<ToggleKey> (name, kLayerLit, theme);
        key->belowText = layer == GroupLayer::Dca ? "GROUP DCA 1-8" : layer == GroupLayer::MatrixMain ? "MATRIX 1-6\nMAIN C" : name;
        key->isOn = [this, l = layer] { return groupLayer == l; };
        key->onClick = [this, l = layer] { setGroupLayer (l); };
        addAndMakeVisible (*key);
        groupLayerKeys.push_back (std::move (key));
    }

    // Switching rebuilds the window's skin: let the click return first.
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
    sendsOnFaderKey.isOn = [this] { return sendsOnFader; };
    sendsOnFaderKey.onClick = [this] { setSendsOnFader (! sendsOnFader); };
    sendsOnFaderKey.setTooltip ("Sends on fader: select a bus for the inputs' sends to it, or a channel for its sends to the buses");
    addAndMakeVisible (sendsOnFaderKey);

    // CLEAR SOLO, lit while anything is soloed.
    clearSolo.isOn = [this] { return ctx.engine.anySoloActive.load(); };
    clearSolo.onClick = [this] { clearSolos(); };
    addAndMakeVisible (clearSolo);
    remoteKey.belowText = "DAW\nREMOTE";
    remoteKey.setEnabled (false);
    remoteKey.setTooltip ("DAW remote: Sidecar has none");
    addAndMakeVisible (remoteKey);
    main->setMeterShown (false);
    // SCENES: PREV and NEXT recall the scene before or after, GO the current
    // again, UNDO the console as it was before the last recall, VIEW the library.
    const char* const sceneNames[] = { "PREV", "NEXT", "UNDO", "GO", "VIEW" };
    for (int k = 0; k < 5; ++k)
    {
        auto key = std::make_unique<ToggleKey> (sceneNames[k], kKeyLit, theme);
        key->onClick = [this, k]
        {
            auto& show = ctx.show;
            const int current = show.getCurrentSceneIndex();
            if (k == 4)
            {
                display.showScreen (x32::Display::Screen::Scenes);
                return;
            }
            if (k == 2)
            {
                if (show.canUndoRecall())
                    show.undoRecall();
                return;
            }
            const int to = k == 0 ? current - 1 : k == 1 ? current + 1 : juce::jmax (0, current);
            if (juce::isPositiveAndBelow (to, show.getNumScenes()))
                show.recallScene (to);
        };
        addAndMakeVisible (*key);
        sceneKeys.push_back (std::move (key));
    }
    // MUTE GROUPS 1-6.
    for (int m = 0; m < 6; ++m)
    {
        auto key = std::make_unique<ToggleKey> ("MUTE GROUP " + juce::String (m + 1), x32::kMuteLit, theme);
        key->belowText = juce::String (m + 1);
        key->isOn = [this, m] { return ctx.engine.muteGroups.contains (m); };
        key->onClick = [this, m] { ctx.engine.muteGroups.set (m, ! ctx.engine.muteGroups.contains (m)); };
        addAndMakeVisible (*key);
        muteGroupKeys.push_back (std::move (key));
    }

    addAndMakeVisible (channel);
    addAndMakeVisible (monitor);
    addAndMakeVisible (assign);
    // An assign button's page jump: the display's screen and tab, a strip selected.
    assign.onJump = [this] (int screen, int tab, Strip* strip)
    {
        if (strip != nullptr && ctx.selectStrip)
            ctx.selectStrip (strip);
        display.showScreen ((x32::Display::Screen) screen, tab);
    };
    // MONITOR's and TALKBACK's VIEW: the display's MONITOR page; ASSIGN's its page.
    monitor.onView = [this] (int tab) { display.showScreen (x32::Display::Screen::Monitor, tab); };
    assign.onView = [this] { display.showScreen (x32::Display::Screen::Assign, assign.getSet() + 1); };
    display.assignLines = [this] { return assign.describeSet(); };
    display.assignCode = [this] (int i) { return assign.getCode (i); };
    display.setAssignCode = [this] (int i, const juce::String& code) { assign.setCode (i, code); };
    display.getAssignSet = [this] { return assign.getSet(); };
    display.setAssignSet = [this] (int s) { assign.setSet (s); };
    display.assignCodeOf = [this] (int set, int i) { return assign.codeOf (set, i); };
    display.assignBinding = [this] (int i) { return assign.getEncoder (i).getBinding(); };
    // A VIEW key opens its section's tab of the display's HOME screen.
    channel.onView = [this] (int section)
    {
        if (section == 6)
            display.showScreen (x32::Display::Screen::Usb);
        else
            display.showScreen (x32::Display::Screen::Home, section + 1);
    };
    display.setGeqOnFaders = [this] (int slot, int first)
    {
        geqSlot = slot;
        geqBand = first;
        applySendsOnFader(); // the faders their own again, then the bands
        applyGeqOnFaders();
    };
    display.getModel = [this] { return (int) model; };
    display.setModel = [this] (int m) { setModel (m == 1 ? Model::Compact : Model::Full); };
    addAndMakeVisible (display);
    // The panel's keys, faders and knobs are the X32's: light rubber keys lit
    // in their colours, moulded caps, knurled knobs.
    hw::applyTo (*this, hw::Look::x32(), &universalKey);
    // Its keys are blank, their names printed under them (not the display's).
    std::function<void (juce::Component&)> namesBelow = [&] (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (child == &display || child == &universalKey)
                continue;
            if (auto* k = dynamic_cast<ToggleKey*> (child))
            {
                // VIEW keys have their name in a small box over them.
                const bool view = k->getButtonText() == "VIEW";
                k->labelBelow = ! view;
                k->labelAbove = view;
                k->face = kKeyFace;
            }
            namesBelow (*child);
        }
    };
    namesBelow (*this);
    // The encoders turn endlessly in their LED rings; MONITOR, PHONES and TALK
    // are pots, and the six under the screen have no rings.
    for (const char* pot : { "MONITOR", "PHONES", "TALK" })
        if (auto* k = monitor.knob (pot))
            k->getSlider().setLookAndFeel (&hw::knobLookAndFeel (hw::Look::x32(), false));
    for (auto* child : display.getChildren())
        if (auto* k = dynamic_cast<DisplayKnob*> (child))
            k->getSlider().setLookAndFeel (&hw::knobLookAndFeel (hw::Look::x32(), false));
    refreshAll();
    startTimerHz (30);
}

X32Surface::~X32Surface()
{
    stopTimer();
}

x32::FaderStrip& X32Surface::getInputStrip (int i)
{
    return *inputs[(size_t) juce::jlimit (0, getNumInputFaders() - 1, i)];
}

x32::FaderStrip& X32Surface::getGroupStrip (int i)
{
    return *groups[(size_t) juce::jlimit (0, 7, i)];
}

x32::FaderStrip& X32Surface::getMainStrip()
{
    return *main;
}

juce::StringArray X32Surface::getInputLayerNames() const
{
    juce::StringArray names;
    for (auto& l : layersOf (model))
        names.add (l.name);
    return names;
}

void X32Surface::buildInputLayerKeys()
{
    inputLayerKeys.clear();
    const auto layers = layersOf (model);
    for (int i = 0; i < (int) layers.size(); ++i)
    {
        auto key = std::make_unique<ToggleKey> (layers[(size_t) i].name, kLayerLit, theme);
        key->look = &hw::Look::x32();
        key->face = kKeyFace;
        key->labelBelow = true;
        if (layers[(size_t) i].printed != nullptr)
            key->belowText = layers[(size_t) i].printed;
        key->isOn = [this, i] { return inputLayer == i; };
        key->onClick = [this, i] { setInputLayer (i); };
        addAndMakeVisible (*key);
        inputLayerKeys.push_back (std::move (key));
    }
}

void X32Surface::setModel (Model m)
{
    if (m == model)
        return;
    model = m;
    ctx.settings.setValue (modelKey, m == Model::Compact ? "COMPACT" : "X32");
    // The same strips stay up where the new model has them (CH 17-32: CH 17-24).
    const auto from = layersOf (m == Model::Compact ? Model::Full : Model::Compact)[(size_t) inputLayer];
    const auto layers = layersOf (m);
    inputLayer = 0;
    for (int i = 0; i < (int) layers.size(); ++i)
        if (layers[(size_t) i].list == from.list && layers[(size_t) i].first == from.first)
            inputLayer = i;
    buildInputLayerKeys();
    for (int i = 0; i < 16; ++i)
        inputs[(size_t) i]->setVisible (i < getNumInputFaders());
    assignBanks();
    resized();
    repaint();
}

void X32Surface::setInputLayer (int l)
{
    inputLayer = juce::jlimit (0, (int) layersOf (model).size() - 1, l);
    assignBanks();
}

void X32Surface::setGroupLayer (GroupLayer l)
{
    groupLayer = l;
    assignBanks();
}

void X32Surface::assignBanks()
{
    auto& e = ctx.engine;
    auto at = [] (const MixerEngine::StripList& list, int i) -> Strip*
    { return juce::isPositiveAndBelow (i, (int) list.size()) ? list[(size_t) i].get() : nullptr; };

    const auto layer = layersOf (model)[(size_t) inputLayer];
    const auto& list = layer.list == 0 ? e.getChannels() : layer.list == 1 ? e.getAux() : e.getBuses();
    for (int i = 0; i < 16; ++i)
        inputs[(size_t) i]->setStrip (i < getNumInputFaders() ? at (list, layer.first + i) : nullptr);
    for (int i = 0; i < 8; ++i)
    {
        auto& g = *groups[(size_t) i];
        switch (groupLayer)
        {
            case GroupLayer::Dca:
                if (i < e.getNumDcas())
                    g.setDca (i);
                else
                    g.clear();
                break;
            case GroupLayer::Bus1to8:  g.setStrip (at (e.getBuses(), i)); break;
            case GroupLayer::Bus9to16: g.setStrip (at (e.getBuses(), 8 + i)); break;
            case GroupLayer::MatrixMain:
                // Matrices 1-6, then the M/C (main 2) on the last fader.
                g.setStrip (i < 6 ? at (e.getMatrices(), i) : i == 7 ? at (e.getMains(), 1) : nullptr);
                break;
        }
    }
    main->setStrip (at (e.getMains(), 0));
    applySendsOnFader();
    applyGeqOnFaders();
    for (auto* keys : { &inputLayerKeys, &groupLayerKeys })
        for (auto& k : *keys)
            k->refresh();
    timerCallback(); // the faders, meters and keys at once
}

void X32Surface::setSendsOnFader (bool on)
{
    sendsOnFader = on;
    assignBanks();
    sendsOnFaderKey.refresh();
}

void X32Surface::applyGeqOnFaders()
{
    if (geqSlot < 0)
        return;
    const auto bands = display.geqBands (geqSlot);
    auto& values = ctx.engine.getFx().params (geqSlot).values;
    for (int i = 0; i < 8; ++i)
        if (const auto at = (size_t) (geqBand + i); at < bands.size())
            groups[(size_t) i]->setBand (&values[bands[at]], x32::Display::geqBandName ((geqBand + i) % 31));
}

void X32Surface::applySendsOnFader()
{
    auto* sel = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    sofShown = sel;
    for (int i = 0; i < getNumInputFaders(); ++i)
        inputs[(size_t) i]->setSend (nullptr, {});
    for (auto& g : groups)
        g->setSend (nullptr, {});
    if (! sendsOnFader || sel == nullptr)
        return;
    if (sel->getKind() == StripKind::Bus)
    {
        // The inputs' sends to the selected bus.
        const int slot = sel->getId();
        const auto to = "BUS " + juce::String (slot + 1);
        for (int i = 0; i < getNumInputFaders(); ++i)
            if (auto* s = inputs[(size_t) i]->getStrip(); s != nullptr && s->isInput())
                inputs[(size_t) i]->setSend (&s->sends[(size_t) slot].levelDb, to);
        // A bus's sends to the matrices, on the MTX layer.
        if (groupLayer == GroupLayer::MatrixMain)
            for (int i = 0; i < juce::jmin (6, (int) ctx.engine.getMatrices().size()); ++i)
                groups[(size_t) i]->setSend (&sel->matrix[(size_t) i].levelDb, "FROM BUS " + juce::String (slot + 1));
        return;
    }
    if (sel->isInput() && (groupLayer == GroupLayer::Bus1to8 || groupLayer == GroupLayer::Bus9to16))
    {
        // The selected channel's sends to buses 1-8 or 9-16.
        const int first = groupLayer == GroupLayer::Bus1to8 ? 0 : 8;
        for (int i = 0; i < 8; ++i)
            if (groups[(size_t) i]->getStrip() != nullptr)
                groups[(size_t) i]->setSend (&sel->sends[(size_t) (first + i)].levelDb, "FROM " + sel->getLabel());
    }
}

void X32Surface::refreshAll()
{
    assignBanks();
    channel.setStrip (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr);
    display.refreshStrip();
    repaint();
}

void X32Surface::selectionChanged()
{
    channel.setStrip (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr);
    if (sendsOnFader && sofShown != (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr))
    {
        applySendsOnFader(); // the sends follow the selection
        applyGeqOnFaders();
    }
    timerCallback(); // the SELECT keys
    display.refreshStrip();
    repaintDesign (channelArea);
}

void X32Surface::timerCallback()
{
    ctx.engine.getWing().followX32Links(); // LINK: the panel's changes reach the pair's other strip
    if (isCovered())
        return;
    for (int i = 0; i < getNumInputFaders(); ++i)
        inputs[(size_t) i]->refresh();
    for (auto& s : groups)
        s->refresh();
    main->refresh();
    sendsOnFaderKey.refresh();
    clearSolo.refresh();
    for (auto& k : muteGroupKeys)
        k->refresh();
    // The main strip's COMP LED.
    repaintDesign (main->getBounds().withHeight (main->getMeterArea().getY() + 8));
}

void X32Surface::clearSolos()
{
    auto& e = ctx.engine;
    for (auto* list : { &e.getChannels(), &e.getAux(), &e.getBuses(), &e.getMains(), &e.getMatrices() })
        for (auto& s : *list)
            s->params.solo = false;
    for (int d = 0; d < e.getNumDcas(); ++d)
        e.getDca (d).solo = false;
}

//==============================================================================
juce::Rectangle<int> X32Surface::channelSectionArea() const
{
    return channelArea;
}

void X32Surface::paint (juce::Graphics& g)
{
    // The console: its dark grey top, the sections on black panels, its
    // silkscreen (the X32's own look, drawn by Sidecar).
    g.fillAll (juce::Colour (0xff17191c));
    g.addTransform (design);
    const auto& look = hw::Look::x32();
    hw::panel (g, designBounds().withTrimmedTop (kTopBar).toFloat(), look);

    // The top bar: what this is.
    auto bar = designBounds().removeFromTop (kTopBar).reduced (sizes::kGapLarge, 0);
    if (g.clipRegionIntersects (bar))
    {
        g.setColour (theme.text);
        g.setFont (type::font (type::kTitle, true));
        g.drawText ("X32", bar.removeFromLeft (60), juce::Justification::centredLeft);
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel));
        g.drawText ("Sidecar's X32 surface  -  not affiliated with Behringer", bar.removeFromLeft (420), juce::Justification::centredLeft);
    }

    // The display's housing: a raised black bezel.
    g.setColour (juce::Colour (0xff1a1b1c));
    g.fillRoundedRectangle (displayArea.toFloat().expanded (10.0f), 14.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (displayArea.toFloat().expanded (9.5f), 14.0f, 1.0f);

    // The fader panel: each strip and layer column on its own black band, the
    // panel's grey between them.
    const auto column = [&g] (juce::Rectangle<int> r)
    {
        g.setColour (juce::Colour (0xff141516));
        g.fillRoundedRectangle (r.toFloat(), 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 5.0f, 1.0f);
    };
    const int top = inputs.front()->getY() - 6, bottom = inputs.front()->getBottom() + 18;
    const auto band = [&] (const juce::Component& c) { column (juce::Rectangle<int> (c.getX() + 2, top, c.getWidth() - 4, bottom - top)); };
    // Every second strip has the black band, the others the panel's own grey (the strips' close-up).
    for (int i = 1; i < getNumInputFaders(); i += 2)
        band (*inputs[(size_t) i]);
    for (int i = 1; i < (int) groups.size(); i += 2)
        band (*groups[(size_t) i]);
    band (*main);
    for (auto* keys : { &inputLayerKeys, &groupLayerKeys })
        if (! keys->empty())
            column (juce::Rectangle<int> (keys->front()->getX() - 2, top, keys->front()->getWidth() + 4, bottom - top));
    // The grille: two fields of fine holes.
    if (g.clipRegionIntersects (grille))
    {
        g.setColour (juce::Colour (0xff1a1b1c));
        const float gap = 180.0f, half = ((float) grille.getWidth() - gap) / 2.0f;
        for (int side = 0; side < 2; ++side)
        {
            const float x0 = (float) grille.getX() + (half + gap) * (float) side;
            for (float y = (float) grille.getY(); y < (float) grille.getBottom(); y += 3.0f)
                for (float x = x0 + (std::fmod (y, 6.0f) < 1.0f ? 0.0f : 1.5f); x < x0 + half; x += 3.0f)
                    g.fillRect (x, y, 1.6f, 1.6f);
        }
    }
    // The headphone sockets' marks at the panel's sides, between the upper panel and the faders.
    {
        const auto phones = [&g, &look] (juce::Point<float> c, bool left)
        {
            g.setColour (look.silk);
            juce::Path p;
            p.addCentredArc (c.x, c.y, 5.0f, 5.0f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.strokePath (p, juce::PathStrokeType (1.4f));
            g.fillRoundedRectangle (c.x - 6.5f, c.y - 1.0f, 3.0f, 5.0f, 1.0f);
            g.fillRoundedRectangle (c.x + 3.5f, c.y - 1.0f, 3.0f, 5.0f, 1.0f);
            juce::Path arrow;
            const float ax = left ? c.x - 10.0f : c.x + 10.0f;
            arrow.addTriangle (ax, c.y, ax + (left ? 4.0f : -4.0f), c.y - 3.0f, ax + (left ? 4.0f : -4.0f), c.y + 3.0f);
            g.fillPath (arrow);
        };
        const float y = (float) fadersArea.getY() - 7.0f;
        phones ({ (float) fadersArea.getX() + 22.0f, y }, true);
        phones ({ (float) fadersArea.getRight() - 22.0f, y }, false);
    }
    // The display housing's MIC, at its bottom left.
    {
        auto mic = juce::Rectangle<float> ((float) displayArea.getX() - 2.0f, (float) displayArea.getBottom() - 2.0f, 60.0f, 12.0f);
        g.setColour (juce::Colour (0xff3a3b3c));
        for (int k = 0; k < 3; ++k)
            g.fillRect (mic.getX() + 2.0f + 4.0f * (float) k, mic.getY() + 2.0f, 2.0f, 8.0f);
        hw::silk (g, mic.withTrimmedLeft (16.0f), "MIC", look, 8.5f, juce::Justification::centredLeft);
    }

    // The main strip: its COMP LED where the others have their meters.
    {
        auto* m = main->getStrip();
        const bool comp = m != nullptr && std::abs (m->meters.dynReductionDb.load()) >= 0.5f;
        const auto r = main->getBounds().getY() + main->getMeterArea().getY();
        auto led = juce::Rectangle<float> ((float) main->getX() + 10.0f, (float) r + 2.0f, 8.0f, 4.0f);
        g.setColour (comp ? juce::Colour (0xffff9a3c) : juce::Colour (0xffff9a3c).withAlpha (0.15f));
        g.fillRoundedRectangle (led, 1.0f);
        hw::silk (g, led.withX (led.getRight() + 3.0f).withWidth (40.0f).expanded (0.0f, 4.0f), "COMP", look, 9.0f, juce::Justification::centredLeft);
        auto name = main->getBounds().toFloat().removeFromBottom (14.0f).translated (0.0f, 12.0f).withSizeKeepingCentre (40.0f, 13.0f);
        g.setColour (look.silkDim);
        g.drawRoundedRectangle (name, 2.0f, 1.0f);
        hw::silk (g, name, "MAIN", look, 9.0f);
    }

    // SCENES and MUTE GROUPS, and the palm rest beside them.
    hw::section (g, scenesArea.toFloat().withTrimmedTop (6.0f), "SCENES", look);
    // GO in its printed frame.
    g.setColour (look.silkDim);
    g.drawRoundedRectangle (sceneKeys[3]->getBounds().toFloat().expanded (5.0f, 3.0f).withTrimmedBottom (16.0f), 2.0f, 1.2f);
    hw::section (g, muteGroupsArea.toFloat().withTrimmedTop (6.0f), "MUTE GROUPS", look);
    if (! palmRest.isEmpty() && g.clipRegionIntersects (palmRest))
    {
        auto rest = palmRest.toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2d2f), rest.getX(), rest.getY(), juce::Colour (0xff151617), rest.getX(), rest.getBottom(), false));
        g.fillRoundedRectangle (rest, 16.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (rest.reduced (0.5f), 16.0f, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        for (float y = rest.getY() + 14.0f; y < rest.getBottom() - 10.0f; y += 3.0f)
            g.drawHorizontalLine ((int) y, rest.getX() + 10.0f, rest.getRight() - 10.0f);
    }
}

void X32Surface::resized()
{
    // Laid out at the design size, then scaled to the window.
    {
        const float s = juce::jmin ((float) getWidth() / (float) kDesignWidth, (float) getHeight() / (float) kDesignHeight);
        design = s > 0.0f ? juce::AffineTransform::scale (s).translated (((float) getWidth() - (float) kDesignWidth * s) * 0.5f,
                                                                         ((float) getHeight() - (float) kDesignHeight * s) * 0.5f)
                          : juce::AffineTransform();
        for (auto* c : getChildren())
            c->setTransform (design);
    }
    auto area = designBounds();
    auto bar = area.removeFromTop (kTopBar).reduced (sizes::kGapLarge, 8);
    universalKey.setBounds (bar.removeFromRight (170));
    area.reduce (sizes::kGapLarge, 0);
    area.removeFromBottom (sizes::kGapLarge);

    // The upper panel as on the X32: the channel strip section, the display in
    // its housing, MONITOR and TALKBACK.
    // The speaker grille along the console's top edge.
    grille = area.removeFromTop (12).reduced (220, 3);
    auto upper = area.removeFromTop (juce::roundToInt (area.getHeight() * 0.42f));
    area.removeFromTop (sizes::kGapLarge);
    monitor.setBounds (upper.removeFromRight (226).reduced (0, 4));
    upper.removeFromRight (sizes::kGapLarge + 4);
    displayArea = upper.removeFromRight (juce::roundToInt (upper.getWidth() * 0.415f)).reduced (10);
    upper.removeFromRight (sizes::kGapLarge + 4);
    channelArea = upper.reduced (0, 4);
    channel.setBounds (channelArea);
    display.setBounds (displayArea);

    // The faders: the input layer keys, the inputs, SENDS ON FADER and the group
    // layer keys, the groups, the main; SCENES, ASSIGN and MUTE GROUPS at the right.
    fadersArea = area;
    auto faders = area.reduced (8, 10);
    auto right = faders.removeFromRight (kRightPanel);
    faders.removeFromRight (sizes::kGap);
    scenesArea = right.removeFromTop (92);
    right.removeFromTop (sizes::kGap);
    auto lower = right.removeFromBottom (juce::jmin (200, right.getHeight() / 3));
    right.removeFromBottom (sizes::kGap);
    assign.setBounds (right);
    muteGroupsArea = lower.removeFromLeft (128);
    lower.removeFromLeft (sizes::kGap);
    palmRest = lower.reduced (4, 2);
    {
        auto r = scenesArea.reduced (10, 8).withTrimmedTop (16);
        auto view = r.removeFromRight (40);
        sceneKeys[4]->setBounds (view.withSizeKeepingCentre (34, x32::kKeyHeight));
        const int w = r.getWidth() / 4;
        for (int k = 0; k < 4; ++k)
            sceneKeys[(size_t) k]->setBounds (juce::Rectangle<int> (r.getX() + w * k, r.getY(), w, r.getHeight())
                                                  .withSizeKeepingCentre (k == 3 ? 52 : 36, k == 3 ? x32::kKeyHeight + 10 : x32::kKeyHeight));
    }
    {
        auto r = muteGroupsArea.reduced (14, 8).withTrimmedTop (18);
        const int h = r.getHeight() / 3, w = r.getWidth() / 2;
        for (int m = 0; m < 6; ++m)
            muteGroupKeys[(size_t) m]->setBounds (
                juce::Rectangle<int> (r.getX() + (m % 2) * w, r.getY() + (m / 2) * h, w, h).withSizeKeepingCentre (40, x32::kKeyHeight));
    }

    const int stripWidth = (faders.getWidth() - 2 * kLayerColumn - 2 * sizes::kGap) / (getNumInputFaders() + 9);
    auto leftColumn = faders.removeFromLeft (kLayerColumn);
    for (int i = 0; i < getNumInputFaders(); ++i)
        inputs[(size_t) i]->setBounds (faders.removeFromLeft (stripWidth));
    auto midColumn = faders.removeFromLeft (kLayerColumn);
    for (auto& s : groups)
        s->setBounds (faders.removeFromLeft (stripWidth));
    faders.removeFromLeft (2 * sizes::kGap);
    main->setBounds (faders.removeFromLeft (stripWidth));
    clearSolo.setBounds (main->getBounds().getX() + 8, main->getY() + main->getMeterArea().getY() + 22, stripWidth - 16, x32::kKeyHeight);

    // The layer keys down their columns from the MUTE keys' row; SENDS ON FADER at the top of the middle one.
    const auto layerKeys = [&] (juce::Rectangle<int> col, std::vector<std::unique_ptr<ToggleKey>>& keys)
    {
        col.removeFromTop (inputs.front()->getMuteTop());
        const int pitch = juce::jmin (56, col.getHeight() / juce::jmax (1, (int) keys.size()));
        for (auto& k : keys)
            k->setBounds (col.removeFromTop (pitch).removeFromTop (x32::kKeyHeight + (k->belowText.containsChar ('\n') ? 16 : 6)).reduced (2, 0));
    };
    layerKeys (leftColumn, inputLayerKeys);
    layerKeys (midColumn, groupLayerKeys);
    sendsOnFaderKey.setBounds (midColumn.withHeight (x32::kKeyHeight + 6).reduced (2, 0));
    remoteKey.setBounds (leftColumn.withHeight (x32::kKeyHeight + 16).reduced (2, 0));
}

} // namespace livedaw::ui
