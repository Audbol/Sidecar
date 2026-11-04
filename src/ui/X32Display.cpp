#include "ui/X32Display.h"

#include "engine/MidiPorts.h"
#include "engine/Models.h"
#include "engine/Show.h"
#include "engine/WingRemote.h"
#include "engine/X32Midi.h"
#include "ui/MeterBank.h"

namespace livedaw::ui::x32
{

using namespace universal;

namespace
{
    // The screen's look: a dark glass, its text light, the X32's amber for what is on.
    const juce::Colour kGlass (0xff0d1014), kPanel (0xff171b21), kLine (0xff2a3038), kDim (0xff8a93a0);
    const juce::Colour kAmber (0xfff0a020);

    constexpr int kStatus = 52, kTabs = 34, kEncoderRow = 66, kSideKeys = 112, kKnobs = 92;

    // The routing blocks' choices (OSC document p.22), as livedaw-x32 names them.
    const juce::StringArray kRoutingIn { "AN1-8", "AN9-16", "AN17-24", "AN25-32", "A1-8", "A9-16", "A17-24", "A25-32",
                                         "A33-40", "A41-48", "B1-8", "B9-16", "B17-24", "B25-32", "B33-40", "B41-48",
                                         "CARD1-8", "CARD9-16", "CARD17-24", "CARD25-32", "UIN1-8", "UIN9-16", "UIN17-24", "UIN25-32" };
    const juce::StringArray kRoutingAux { "AUX1-4", "AN1-2", "AN1-4", "AN1-6", "A1-2", "A1-4", "A1-6", "B1-2",
                                          "B1-4", "B1-6", "CARD1-2", "CARD1-4", "CARD1-6", "UIN1-2", "UIN1-4", "UIN1-6" };
    const char* const kInBlocks[] = { "1-8", "9-16", "17-24", "25-32", "AUX" };

    const char* const kScreenNames[] = { "HOME", "METERS", "ROUTING", "SETUP", "LIBRARY", "EFFECTS", "MUTE GRP", "UTILITY" };

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

    /** A key's state as a push: "GATE" lit while on. */
    Display::Encoder encoder (KnobBinding turn, const juce::String& pushLabel = {}, std::function<void()> push = {},
                              std::function<bool()> on = {})
    {
        return { std::move (turn), pushLabel, std::move (push), std::move (on) };
    }

    template <typename Atomic>
    std::function<void()> toggle (Atomic& a)
    {
        return [&a] { a = ! a.load(); };
    }

    template <typename Atomic>
    std::function<bool()> isOn (Atomic& a)
    {
        return [&a] { return a.load(); };
    }
} // namespace

Display::Display (UiContext& c, const Theme& t)
    : ctx (c), theme (t), pagePrev ("<", t.accent, t), pageNext (">", t.accent, t), layerUp ("LAYER ^", t.accent, t),
      layerDown ("LAYER v", t.accent, t), eqView (t), gateView (t), dynView (t)
{
    for (int s = 0; s < 8; ++s)
    {
        auto key = std::make_unique<ToggleKey> (kScreenNames[s], t.accent, t);
        key->isOn = [this, s] { return (int) screen == s; };
        key->onClick = [this, s] { showScreen ((Screen) s); };
        addAndMakeVisible (*key);
        screenKeys.push_back (std::move (key));
    }
    pagePrev.onClick = [this] { showTab (tab - 1); };
    pageNext.onClick = [this] { showTab (tab + 1); };
    layerUp.onClick = [this] { setLayer (layer - 1); };
    layerDown.onClick = [this] { setLayer (layer + 1); };
    for (auto* k : { &pagePrev, &pageNext, &layerUp, &layerDown })
        addAndMakeVisible (*k);
    for (int i = 0; i < 6; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.setHardware (true);
        k.setCap (DisplayKnob::Cap::Encoder);
        addAndMakeVisible (k);
        auto push = std::make_unique<ToggleKey> ("PUSH", kAmber, t);
        push->onClick = [this, i] { pushEncoder (i); };
        push->isOn = [this, i] { auto& e = encoders[(size_t) i]; return e.pushOn && e.pushOn(); };
        addAndMakeVisible (*push);
        pushKeys.push_back (std::move (push));
    }
    for (int g = 0; g < 6; ++g)
    {
        auto key = std::make_unique<ToggleKey> ("MUTE GROUP " + juce::String (g + 1), t.mute, t);
        key->isOn = [this, g] { return ctx.engine.muteGroups.contains (g); };
        key->onClick = [this, g] { ctx.engine.muteGroups.set (g, ! ctx.engine.muteGroups.contains (g)); };
        addChildComponent (*key);
        muteGroupKeys.push_back (std::move (key));
    }
    for (auto* v : std::initializer_list<juce::Component*> { &eqView, &gateView, &dynView })
        addChildComponent (v);
    gateView.reductionDb = [this] { auto* s = selected(); return s != nullptr ? s->meters.gateReductionDb.load() : 0.0f; };
    dynView.reductionDb = [this] { auto* s = selected(); return s != nullptr ? s->meters.dynReductionDb.load() : 0.0f; };
    eqView.response = [this] (const double* f, double* db, int n)
    {
        auto* s = selected();
        if (s == nullptr)
            return false;
        ld::ld_strip_response (s->getHandle(), 1, f, db, n, ctx.engine.getSampleRate() > 0 ? ctx.engine.getSampleRate() : 48000.0);
        return true;
    };
    eqView.handleSource = [this]
    {
        auto* s = selected();
        if (s == nullptr)
            return std::vector<ResponseView::Handle>();
        auto& eq = s->params.eq();
        return ResponseView::eqHandles (models::model (s->isOutput() ? models::BusEq : models::ChannelEq, eq.model.load()), eq.values);
    };
    refreshStrip();
    startTimerHz (15);
}

Display::~Display()
{
    stopTimer();
}

Strip* Display::selected() const
{
    return ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
}

juce::StringArray Display::getTabs() const
{
    switch (screen)
    {
        case Screen::Home:       return { "home", "config", "gate", "dyn", "eq", "sends", "main" };
        case Screen::Meters:     return { "channel", "mix bus", "aux / fx", "in / out" };
        case Screen::Routing:    return { "home" };
        case Screen::Setup:      return { "global", "remote" };
        case Screen::Library:    return { "scenes" };
        case Screen::Effects:    return { "home" };
        case Screen::MuteGroups: return { "mute grp" };
        case Screen::Utility:    return { "utility" };
    }
    return {};
}

int Display::numLayers() const
{
    if (screen == Screen::Setup)
        return tab == 1 ? 2 : 1; // remote: the MIDI kinds, then SysEx
    if (screen != Screen::Home)
        return 1;
    // HOME's home, gate and dynamics tabs have a second layer of encoders.
    return tab == 0 || tab == 2 || tab == 3 ? 2 : 1;
}

void Display::showScreen (Screen s, int t)
{
    screen = s;
    tab = 0;
    showTab (t);
}

void Display::showTab (int t)
{
    tab = juce::jlimit (0, getTabs().size() - 1, t);
    layer = 0;
    const bool home = screen == Screen::Home;
    eqView.setVisible (home && tab == 4);
    gateView.setVisible (home && tab == 2);
    dynView.setVisible (home && tab == 3);
    for (auto& k : muteGroupKeys)
        k->setVisible (screen == Screen::MuteGroups);
    assignEncoders();
    for (auto& k : screenKeys)
        k->refresh();
    resized();
    repaint();
}

void Display::setLayer (int l)
{
    layer = juce::jlimit (0, numLayers() - 1, l);
    assignEncoders();
    repaint();
}

void Display::refreshStrip()
{
    auto* s = selected();
    gateView.bind (s != nullptr ? &s->params.gate() : nullptr);
    dynView.bind (s != nullptr ? &s->params.dynamics() : nullptr);
    eqView.refresh();
    assignEncoders();
    repaint();
}

void Display::pushEncoder (int i)
{
    auto& e = encoders[(size_t) juce::jlimit (0, 5, i)];
    if (e.push)
        e.push();
    for (auto& k : pushKeys)
        k->refresh();
    repaint();
}

void Display::assignEncoders()
{
    for (auto& e : encoders)
        e = {};
    auto* s = selected();
    auto& engine = ctx.engine;

    if (screen == Screen::Home && s != nullptr)
    {
        auto& p = s->params;
        auto& gate = p.gate();
        auto& dyn = p.dynamics();
        auto& eq = p.eq();
        const auto eqTable = s->isOutput() ? models::BusEq : models::ChannelEq;
        const bool hasMc = engine.getMains().size() > 1;
        // The X32's process order: bit 1 the compressor PRE the EQ, bit 2 the insert PRE.
        auto orderBit = [&p] (int bit, const juce::String& label)
        {
            KnobBinding b;
            b.label = label;
            b.get = [&p, bit] { return (p.processOrder.load() & bit) != 0 ? 0.0f : 1.0f; };
            b.set = [&p, bit] (float v) { p.processOrder = v < 0.5f ? (p.processOrder.load() | bit) : (p.processOrder.load() & ~bit); };
            b.text = [&p, bit] { return (p.processOrder.load() & bit) != 0 ? juce::String ("PRE") : juce::String ("POST"); };
            b.steps = 2.0f;
            return b;
        };
        auto gainOf = [&] { return bindLinear ("GAIN", p.input.trimDb, -18.0f, 18.0f, " dB", 1, 0.0f); };
        auto faderOf = [&] { return bindFader ("FADER", p.faderDb); };
        switch (tab)
        {
            case 0: // home
                if (layer == 0)
                {
                    encoders[0] = encoder (gainOf());
                    encoders[1] = encoder (bindParam (models::Dynamics, gate.model.load(), gate.values, "thr", "GATE"), "GATE", toggle (gate.on), isOn (gate.on));
                    encoders[2] = encoder (orderBit (2, "INSERT"), "INSERT", toggle (p.preInsert().on), isOn (p.preInsert().on));
                    encoders[3] = encoder (orderBit (1, "DYN POS"), "EQ", toggle (eq.on), isOn (eq.on));
                    encoders[4] = encoder (bindParam (models::Dynamics, dyn.model.load(), dyn.values, "thr", "COMP"), "COMP", toggle (dyn.on), isOn (dyn.on));
                    encoders[5] = encoder (bindPan ("PAN", p.pan), "LR", toggle (s->mains[0].on), isOn (s->mains[0].on));
                }
                else
                {
                    encoders[1] = encoder ({}, "POLARITY", toggle (p.input.invert), isOn (p.input.invert));
                    encoders[4] = encoder ({}, "SOLO", toggle (p.solo), isOn (p.solo));
                    encoders[5] = encoder (faderOf(), "MUTE", toggle (p.mute), isOn (p.mute));
                }
                break;
            case 1: // config
                encoders[0] = encoder (gainOf());
                encoders[1] = encoder (bindFreq ("LOW CUT", p.filter().lowCutFreq, 20.0f, 400.0f, 20.0f), "LOW CUT", toggle (p.filter().lowCut), isOn (p.filter().lowCut));
                encoders[3] = encoder (bindLinear ("DELAY", p.input.delay, 0.3f, 500.0f, " ms", 1, 0.3f), "DELAY", toggle (p.input.delayOn), isOn (p.input.delayOn));
                encoders[4] = encoder (orderBit (2, "INSERT POS"), "INSERT", toggle (p.preInsert().on), isOn (p.preInsert().on));
                break;
            case 2: // gate
                if (layer == 0)
                {
                    encoders[0] = encoder (bindParam (models::Dynamics, gate.model.load(), gate.values, "thr", "THRESHOLD"), "GATE", toggle (gate.on), isOn (gate.on));
                    encoders[1] = encoder (bindChoice ("MODE", gate.model, { "EXP2", "EXP3", "EXP4", "GATE", "DUCK" }, 3));
                    encoders[2] = encoder (bindParam (models::Dynamics, gate.model.load(), gate.values, "att", "ATTACK"));
                    encoders[3] = encoder (bindParam (models::Dynamics, gate.model.load(), gate.values, "hld", "HOLD"));
                    encoders[4] = encoder (bindParam (models::Dynamics, gate.model.load(), gate.values, "rel", "RELEASE"));
                    encoders[5] = encoder (bindParam (models::Dynamics, gate.model.load(), gate.values, "range", "RANGE"));
                }
                else
                {
                    encoders[0] = encoder (bindParam (models::Dynamics, gate.model.load(), gate.values, "thr", "THRESHOLD"), "GATE", toggle (gate.on), isOn (gate.on));
                    encoders[3] = encoder (bindLog ("KEY FREQ", gate.scFreq, 20.0f, 20000.0f, " Hz", 1000.0f));
                }
                break;
            case 3: // dyn
                if (layer == 0)
                {
                    encoders[0] = encoder (bindParam (models::Dynamics, dyn.model.load(), dyn.values, "thr", "THRESHOLD"), "COMP", toggle (dyn.on), isOn (dyn.on));
                    encoders[1] = encoder (bindParam (models::Dynamics, dyn.model.load(), dyn.values, "ratio", "RATIO"), "COMP / EXP",
                                           [&dyn] { dyn.model = dyn.model.load() == 5 ? 6 : 5; }, [&dyn] { return dyn.model.load() == 6; });
                    encoders[2] = encoder (bindParam (models::Dynamics, dyn.model.load(), dyn.values, "att", "ATTACK"));
                    encoders[3] = encoder (bindParam (models::Dynamics, dyn.model.load(), dyn.values, "hld", "HOLD"));
                    encoders[4] = encoder (bindParam (models::Dynamics, dyn.model.load(), dyn.values, "rel", "RELEASE"));
                    encoders[5] = encoder (bindParam (models::Dynamics, dyn.model.load(), dyn.values, "mgain", "GAIN"));
                    for (auto [i, id, label] : { std::tuple { 2, "det", "PEAK / RMS" }, std::tuple { 3, "env", "LIN / LOG" }, std::tuple { 4, "auto", "AUTO" } })
                    {
                        const int k = models::model (models::Dynamics, dyn.model.load()).find (id);
                        if (k >= 0)
                        {
                            auto& v = dyn.values[k];
                            encoders[(size_t) i].pushLabel = label;
                            encoders[(size_t) i].push = [&v] { v = v.load() > 0.5f ? 0.0f : 1.0f; };
                            encoders[(size_t) i].pushOn = [&v] { return v.load() > 0.5f; };
                        }
                    }
                }
                else
                {
                    encoders[0] = encoder (bindParam (models::Dynamics, dyn.model.load(), dyn.values, "thr", "THRESHOLD"), "COMP", toggle (dyn.on), isOn (dyn.on));
                    encoders[1] = encoder (bindLinear ("MIX", dyn.mix, 0.0f, 100.0f, " %", 0, 100.0f));
                    encoders[2] = encoder (bindParam (models::Dynamics, dyn.model.load(), dyn.values, "knee", "KNEE"));
                    encoders[3] = encoder (orderBit (1, "POSITION"));
                }
                break;
            case 4: // eq
            {
                const int bands = s->isOutput() ? 6 : 4;
                eqBand = juce::jlimit (0, bands - 1, eqBand);
                const auto n = juce::String (eqBand + 1);
                if (! s->isOutput())
                    encoders[0] = encoder (bindFreq ("LOW CUT", p.filter().lowCutFreq, 20.0f, 400.0f, 20.0f), "LOW CUT", toggle (p.filter().lowCut), isOn (p.filter().lowCut));
                encoders[1] = encoder (bindParam (eqTable, eq.model.load(), eq.values, n + "f", "FREQ"));
                encoders[2] = encoder (bindParam (eqTable, eq.model.load(), eq.values, n + "g", "GAIN"));
                encoders[3] = encoder (bindParam (eqTable, eq.model.load(), eq.values, n + "q", "Q"));
                encoders[4] = encoder (bindParam (eqTable, eq.model.load(), eq.values, n + "t", "TYPE"));
                KnobBinding band;
                band.label = "BAND";
                band.get = [this, bands] { return bands > 1 ? (float) eqBand / (float) (bands - 1) : 0.0f; };
                band.set = [this, bands] (float v)
                {
                    const int b = juce::roundToInt (v * (float) (bands - 1));
                    if (b != eqBand)
                    {
                        eqBand = b;
                        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Display> (this)]
                                                         { if (safe != nullptr) safe->assignEncoders(); });
                    }
                };
                const char* const names4[] = { "LOW", "LO MID", "HI MID", "HIGH" };
                const char* const names6[] = { "LOW", "LOW 2", "LO MID", "HI MID", "HIGH 2", "HIGH" };
                band.text = [this, bands, names4, names6] { return juce::String (bands == 4 ? names4[eqBand] : names6[eqBand]); };
                band.steps = (float) bands;
                encoders[5] = encoder (band, "EQ", toggle (eq.on), isOn (eq.on));
                break;
            }
            case 5: // sends
            {
                const bool hasSends = s->isInput();
                for (int i = 0; i < 4 && hasSends; ++i)
                {
                    const int slot = 4 * sendBank + i;
                    if (slot >= (int) engine.getBuses().size())
                        break;
                    auto& send = s->sends[(size_t) slot];
                    encoders[(size_t) i] = encoder (bindFader ("BUS " + juce::String (slot + 1), send.levelDb), "ON", toggle (send.enabled), isOn (send.enabled));
                }
                KnobBinding bank;
                bank.label = "SENDS";
                bank.get = [this] { return (float) sendBank / 3.0f; };
                bank.set = [this] (float v)
                {
                    const int b = juce::roundToInt (v * 3.0f);
                    if (b != sendBank)
                    {
                        sendBank = b;
                        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Display> (this)]
                                                         { if (safe != nullptr) safe->assignEncoders(); });
                    }
                };
                bank.text = [this] { return juce::String (4 * sendBank + 1) + "-" + juce::String (4 * sendBank + 4); };
                bank.steps = 4.0f;
                encoders[5] = encoder (bank, "NEXT 4", [this] { sendBank = (sendBank + 1) % 4; assignEncoders(); });
                break;
            }
            case 6: // main
                encoders[0] = encoder (bindPan ("PAN", p.pan), "LR", toggle (s->mains[0].on), isOn (s->mains[0].on));
                if (hasMc)
                    encoders[1] = encoder (bindFader ("M/C LEVEL", s->mains[1].levelDb), "M/C", toggle (s->mains[1].on), isOn (s->mains[1].on));
                encoders[4] = encoder ({}, "SOLO", toggle (p.solo), isOn (p.solo));
                encoders[5] = encoder (faderOf(), "MUTE", toggle (p.mute), isOn (p.mute));
                break;
            default:
                break;
        }
    }
    else if (screen == Screen::Setup && tab == 1)
    {
        assignRemoteEncoders();
    }
    else if (screen == Screen::Routing)
    {
        assignRoutingEncoders();
    }
    else if (screen == Screen::Setup && getModel && setModel)
    {
        KnobBinding m;
        m.label = "MODEL";
        m.get = [this] { return (float) getModel(); };
        m.set = [this] (float v)
        {
            const int to = v < 0.5f ? 0 : 1;
            if (to != getModel())
                setModel (to);
        };
        m.text = [this] { return getModel() == 1 ? juce::String ("X32 COMPACT") : juce::String ("X32"); };
        m.steps = 2.0f;
        encoders[0] = encoder (m, "SWITCH", [this] { setModel (1 - getModel()); }, [] { return false; });
    }
    else if (screen == Screen::Library)
    {
        KnobBinding scene;
        scene.label = "SCENE";
        auto& show = ctx.show;
        scene.get = [this, &show] { return show.getNumScenes() > 1 ? (float) selectedScene / (float) (show.getNumScenes() - 1) : 0.0f; };
        scene.set = [this, &show] (float v) { selectedScene = juce::roundToInt (v * (float) juce::jmax (0, show.getNumScenes() - 1)); repaint(); };
        scene.text = [this] { return juce::String (selectedScene + 1).paddedLeft ('0', 2); };
        scene.steps = (float) juce::jmax (2, ctx.show.getNumScenes());
        encoders[5] = encoder (scene, "GO", [this, &show]
        {
            if (juce::isPositiveAndBelow (selectedScene, show.getNumScenes()))
                show.recallScene (selectedScene);
        });
        // X32 scene files: IMPORT keeps one as a new scene, EXPORT writes the console.
        const juce::Component::SafePointer<Display> safe (this);
        auto said = [safe] (const juce::String& status)
        {
            if (safe == nullptr)
                return;
            safe->libraryStatus = status;
            safe->selectedScene = juce::jmax (0, safe->ctx.show.getNumScenes() - 1);
            safe->assignEncoders();
        };
        encoders[0] = encoder ({}, "IMPORT .scn", [this, said] { sceneFiles.importScene (said); });
        encoders[1] = encoder ({}, "EXPORT .scn", [this, said] { sceneFiles.exportScene (said); });
    }

    for (int i = 0; i < 6; ++i)
    {
        knobs[(size_t) i].setBinding (encoders[(size_t) i].turn);
        auto& key = *pushKeys[(size_t) i];
        key.setButtonText (encoders[(size_t) i].pushLabel);
        key.setVisible (static_cast<bool> (encoders[(size_t) i].push));
        key.refresh();
    }
    layerUp.setEnabled (numLayers() > 1);
    layerDown.setEnabled (numLayers() > 1);
    repaint();
}

juce::String Display::x32Value (const juce::String& address) const
{
    int set = 0;
    const auto answer = ctx.engine.getWing().runX32Text (address, set);
    return answer.fromFirstOccurrenceOf (" ", false, false).trim();
}

void Display::assignRoutingEncoders()
{
    // Encoders 1-5: what feeds inputs 1-8 .. 25-32 and aux 1-6; 6: the
    // recording (IN) or playback (PLAY) blocks.
    const auto group = [this] { return x32Value ("/config/routing/routswitch") == "PLAY" ? juce::String ("PLAY") : juce::String ("IN"); };
    for (int b = 0; b < 5; ++b)
    {
        const auto& choices = b < 4 ? kRoutingIn : kRoutingAux;
        const auto path = [group, b] { return "/config/routing/" + group() + "/" + kInBlocks[b]; };
        KnobBinding k;
        k.label = b < 4 ? "IN " + juce::String (kInBlocks[b]) : juce::String ("AUX 1-6");
        const int n = choices.size();
        k.get = [this, path, &choices, n] { return (float) juce::jmax (0, choices.indexOf (x32Value (path()))) / (float) (n - 1); };
        k.set = [this, path, &choices, n] (float v)
        {
            int set = 0;
            ctx.engine.getWing().runX32Text (path() + " " + choices[juce::roundToInt (v * (float) (n - 1))], set);
            repaint();
        };
        k.text = [this, path] { return x32Value (path()); };
        k.steps = (float) n;
        encoders[(size_t) b] = encoder (k);
    }
    KnobBinding sw;
    sw.label = "SOURCE";
    sw.get = [group] { return group() == "PLAY" ? 1.0f : 0.0f; };
    sw.set = [this] (float v)
    {
        int set = 0;
        ctx.engine.getWing().runX32Text (v >= 0.5f ? "/config/routing/routswitch PLAY" : "/config/routing/routswitch REC", set);
        repaint();
    };
    sw.text = [group] { return group() == "PLAY" ? juce::String ("PLAY") : juce::String ("REC"); };
    sw.steps = 2.0f;
    encoders[5] = encoder (sw, "REC / PLAY", [this, group]
    {
        int set = 0;
        ctx.engine.getWing().runX32Text (group() == "PLAY" ? "/config/routing/routswitch REC" : "/config/routing/routswitch PLAY", set);
        assignEncoders();
    }, [group] { return group() == "PLAY"; });
}

void Display::paintRouting (juce::Graphics& g, juce::Rectangle<int> r)
{
    // What each block names, in the interface's numbering (Sidecar's ports).
    auto ports = [] (const juce::String& choice) -> juce::String
    {
        const auto digits = choice.retainCharacters ("0123456789-");
        const int first = digits.upToFirstOccurrenceOf ("-", false, false).getIntValue();
        const int last = digits.fromFirstOccurrenceOf ("-", false, false).getIntValue();
        int base = -1;
        if (choice.startsWith ("AN"))
            base = 0;
        else if (choice.startsWith ("A"))
            base = 32;
        else if (choice.startsWith ("B"))
            base = 80;
        else if (choice.startsWith ("CARD"))
            base = 128;
        if (base < 0 || first <= 0)
            return choice.startsWith ("UIN") ? "USER IN (NOT IN SIDECAR)" : juce::String();
        return "INTERFACE " + juce::String (base + first) + "-" + juce::String (base + last);
    };
    const bool play = x32Value ("/config/routing/routswitch") == "PLAY";
    const juce::String group = play ? "PLAY" : "IN";
    juce::StringArray left, right;
    for (int b = 0; b < 5; ++b)
    {
        const auto choice = x32Value ("/config/routing/" + group + "/" + kInBlocks[b]);
        left.add (b < 4 ? "INPUTS " + juce::String (kInBlocks[b]) : juce::String ("AUX IN 1-6"));
        const auto where = b < 4 ? ports (choice) : (choice == "AUX1-4" ? juce::String ("INTERFACE 161-166") : ports (choice) + ", THE REST AUX IN");
        right.add (choice + (where.isNotEmpty() ? "  (" + where + ")" : juce::String()));
    }
    left.add ("USB L / R");
    right.add ("USB PLAYER");
    left.add ("BLOCKS IN USE");
    right.add (play ? "PLAY (PLAYBACK)" : "IN (RECORDING)");
    paintRows (g, r, left, right);
}

X32Midi* Display::x32Midi() const
{
    return dynamic_cast<X32Midi*> (ctx.consoleMidi);
}

void Display::assignRemoteEncoders()
{
    auto& settings = ctx.settings;
    if (layer == 0)
    {
        // MIDI IN and OUT: turn to a device, push to use it.
        for (int io = 0; io < 2; ++io)
        {
            const bool input = io == 0;
            auto& items = midiChoices[(size_t) io];
            auto& values = midiValues[(size_t) io];
            items = { "NONE" };
            values = { juce::String() };
            if (MidiPorts::canCreateVirtualPorts())
            {
                items.add ("VIRTUAL: SIDECAR MIDI X32");
                values.add (MidiPorts::virtualDevice);
            }
            for (auto& d : input ? MidiPorts::getInputDevices() : MidiPorts::getOutputDevices())
            {
                items.add (d.toUpperCase());
                values.add (d);
            }
            const auto key = input ? X32Midi::inputKey : X32Midi::outputKey;
            const auto current = settings.getValue (key, MidiPorts::virtualDevice);
            int at = values.indexOf (current);
            if (at < 0)
            {
                // The default (the app's virtual port) where the platform has none (Windows).
                items.add (current == MidiPorts::virtualDevice ? juce::String ("VIRTUAL: UNAVAILABLE") : "NOT FOUND: " + current.toUpperCase());
                values.add (current);
                at = items.size() - 1;
            }
            midiPick[(size_t) io] = at;

            KnobBinding b;
            b.label = input ? "MIDI IN" : "MIDI OUT";
            const int n = items.size();
            b.get = [this, io, n] { return n > 1 ? (float) midiPick[(size_t) io] / (float) (n - 1) : 0.0f; };
            b.set = [this, io, n] (float v)
            {
                midiPick[(size_t) io] = juce::jlimit (0, n - 1, juce::roundToInt (v * (float) (n - 1)));
                repaint();
            };
            b.text = [this, io] { return midiChoices[(size_t) io][midiPick[(size_t) io]]; };
            b.steps = (float) juce::jmax (2, n);
            encoders[(size_t) io] = encoder (b, "USE", [this, io, key]
            {
                ctx.settings.setValue (key, midiValues[(size_t) io][midiPick[(size_t) io]]);
                if (ctx.consoleMidi != nullptr)
                    ctx.consoleMidi->loadSettings (ctx.settings);
                assignEncoders();
            }, [this, io, key] { return ctx.settings.getValue (key, MidiPorts::virtualDevice) == midiValues[(size_t) io][midiPick[(size_t) io]]; });
        }

        // Program changes, faders, mutes, pans: OFF, RX, TX or RX + TX.
        const char* const kinds[] = { "PROGRAM", "FADERS", "MUTES", "PANS" };
        for (int k = 0; k < 4; ++k)
        {
            const int rx = 1 << (2 * k), both = rx | (rx << 1);
            auto mode = [this, rx, both]
            {
                const int bits = ctx.settings.getIntValue (X32Midi::enableKey, X32Midi::All) & both;
                return (bits & rx ? 1 : 0) + (bits & (rx << 1) ? 2 : 0);
            };
            auto setMode = [this, rx, both] (int m)
            {
                int bits = ctx.settings.getIntValue (X32Midi::enableKey, X32Midi::All) & ~both;
                bits |= (m & 1 ? rx : 0) | (m & 2 ? rx << 1 : 0);
                ctx.settings.setValue (X32Midi::enableKey, bits);
                if (auto* m32 = x32Midi())
                    m32->setEnabled (bits);
                repaint();
            };
            KnobBinding b;
            b.label = kinds[k];
            b.get = [mode] { return (float) mode() / 3.0f; };
            b.set = [setMode] (float v) { setMode (juce::roundToInt (v * 3.0f)); };
            b.text = [mode] { return juce::StringArray { "OFF", "RX", "TX", "RX + TX" }[mode()]; };
            b.steps = 4.0f;
            encoders[(size_t) k + 2] = encoder (b, kinds[k], [mode, setMode] { setMode ((mode() + 1) % 4); }, [mode] { return mode() != 0; });
        }
        return;
    }

    // Layer 2: OSC over SysEx, and every value sent again.
    auto sysexOn = [this] { return (ctx.settings.getIntValue (X32Midi::enableKey, X32Midi::All) & X32Midi::SysEx) != 0; };
    auto setSysex = [this] (bool on)
    {
        int bits = ctx.settings.getIntValue (X32Midi::enableKey, X32Midi::All);
        bits = on ? bits | X32Midi::SysEx : bits & ~X32Midi::SysEx;
        ctx.settings.setValue (X32Midi::enableKey, bits);
        if (auto* m = x32Midi())
            m->setEnabled (bits);
        repaint();
    };
    KnobBinding b;
    b.label = "OSC SYSEX";
    b.get = [sysexOn] { return sysexOn() ? 1.0f : 0.0f; };
    b.set = [setSysex] (float v) { setSysex (v >= 0.5f); };
    b.text = [sysexOn] { return sysexOn() ? juce::String ("ON") : juce::String ("OFF"); };
    b.steps = 2.0f;
    encoders[0] = encoder (b, "SYSEX", [sysexOn, setSysex] { setSysex (! sysexOn()); }, sysexOn);
    encoders[5] = encoder ({}, "SEND ALL", [this]
    {
        if (ctx.consoleMidi != nullptr)
            ctx.consoleMidi->resendAll();
    });
}

void Display::paintRemote (juce::Graphics& g, juce::Rectangle<int> r)
{
    auto& settings = ctx.settings;
    const int bits = settings.getIntValue (X32Midi::enableKey, X32Midi::All);
    auto* midi = x32Midi();
    const auto status = midi != nullptr ? midi->getPortStatus (0) : MidiPorts::Status {};
    auto device = [&] (bool input)
    {
        const auto value = settings.getValue (input ? X32Midi::inputKey : X32Midi::outputKey, MidiPorts::virtualDevice);
        if (value.isEmpty())
            return juce::String ("NONE");
        const bool isVirtual = value == MidiPorts::virtualDevice;
        if (isVirtual && ! MidiPorts::canCreateVirtualPorts())
            return juce::String ("VIRTUAL: UNAVAILABLE ON THIS SYSTEM");
        const auto shown = isVirtual ? juce::String ("VIRTUAL: SIDECAR MIDI X32") : value.toUpperCase();
        if (input ? status.inputMissing : status.outputMissing)
            return "NOT FOUND: " + shown;
        return shown + ((input ? status.inputOpen : status.outputOpen) ? "" : " (CLOSED)");
    };
    auto mode = [bits] (int rx)
    {
        const bool in = (bits & rx) != 0, out = (bits & (rx << 1)) != 0;
        return in && out ? juce::String ("RX + TX") : in ? juce::String ("RX") : out ? juce::String ("TX") : juce::String ("OFF");
    };
    paintRows (g, r, { "MIDI IN", "MIDI OUT", "RECEIVED / SENT", "SCENES, SNIPPETS (PC, CH 1, 2)", "FADERS (CC, CH 1)",
                       "MUTES, MUTE GROUPS (CC, CH 2)", "PANS (CC, CH 3)", "OSC OVER SYSEX" },
               { device (true), device (false), juce::String (status.received) + " / " + juce::String (status.sent), mode (X32Midi::RxProgram),
                 mode (X32Midi::RxFader), mode (X32Midi::RxMute), mode (X32Midi::RxPan), (bits & X32Midi::SysEx) != 0 ? "ON" : "OFF" });
}

void Display::timerCallback()
{
    if (! isShowing())
        return;
    if (screen == Screen::Setup && tab == 1)
        repaint(); // the messages counted
    // A model, an order or a strip changed under the encoders: map them anew.
    juce::String now;
    if (auto* s = selected())
        now << juce::String::toHexString ((juce::pointer_sized_int) s) << s->params.gate().model.load() << "|" << s->params.dynamics().model.load()
            << "|" << s->params.eq().model.load() << "|" << s->params.processOrder.load();
    if (now != shownStatus)
    {
        shownStatus = now;
        refreshStrip();
    }
    for (auto& k : pushKeys)
        k->refresh();
    for (auto& k : muteGroupKeys)
        if (k->isVisible())
            k->refresh();
    if (eqView.isVisible())
        eqView.refresh();
    if (gateView.isVisible())
        gateView.refresh();
    if (dynView.isVisible())
        dynView.refresh();
    repaint (statusArea);
    repaint (encoderRow);
    if (screen == Screen::Meters || (screen == Screen::Home && (tab == 0 || tab == 5)))
        repaint (contentArea);
}

//==============================================================================
void Display::paint (juce::Graphics& g)
{
    // The bezel and the glass.
    g.setColour (juce::Colour (0xff101215));
    g.fillRoundedRectangle (screenArea.toFloat().expanded (6.0f), 8.0f);
    g.setColour (kGlass);
    g.fillRect (screenArea);
    paintStatus (g, statusArea);
    paintTabs (g, tabsArea);
    paintContent (g, contentArea);
    paintEncoderRow (g, encoderRow);
}

void Display::paintStatus (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (kPanel);
    g.fillRect (r);
    g.setColour (kLine);
    g.fillRect (r.removeFromBottom (1));
    r.reduce (10, 4);

    // The selected channel: its colour, number, name.
    if (auto* s = selected())
    {
        auto chip = r.removeFromLeft (8).reduced (0, 2);
        g.setColour (consoleColour (s->getColourIndex()));
        g.fillRect (chip);
        r.removeFromLeft (8);
        auto who = r.removeFromLeft (150);
        g.setColour (theme.text);
        g.setFont (type::font (type::kValue, true));
        g.drawText (s->getLabel(), who.removeFromTop (who.getHeight() / 2), juce::Justification::bottomLeft);
        g.setColour (kDim);
        g.setFont (type::font (type::kLabel));
        g.drawText (s->getName().isNotEmpty() ? s->getName() : juce::String ("-"), who, juce::Justification::topLeft);
    }
    else
        r.removeFromLeft (166);

    // The scene: its number (amber) and name, the next one.
    auto& show = ctx.show;
    const int current = show.getCurrentSceneIndex();
    auto scene = r.removeFromLeft (260);
    g.setColour (kAmber);
    g.setFont (type::font (type::kTitle, true));
    g.drawText (current >= 0 ? juce::String (current + 1).paddedLeft ('0', 2) : juce::String ("--"), scene.removeFromLeft (44), juce::Justification::centredLeft);
    const auto nameOf = [&show] (int i)
    {
        const auto s = show.getScene (i);
        return s.has_value() ? s->name : juce::String();
    };
    g.setColour (theme.text);
    g.setFont (type::font (type::kLabel, true));
    g.drawText (current >= 0 ? nameOf (current) : juce::String ("no scene"), scene.removeFromTop (scene.getHeight() / 2), juce::Justification::bottomLeft);
    g.setColour (kDim);
    g.setFont (type::font (type::kCaption));
    if (current + 1 < show.getNumScenes())
        g.drawText ("next: " + nameOf (current + 1), scene, juce::Justification::topLeft);

    // The clock and the sample rate, at the right.
    g.setColour (theme.text);
    g.setFont (type::font (type::kValue, true));
    g.drawText (juce::Time::getCurrentTime().formatted ("%H:%M:%S"), r.removeFromRight (90), juce::Justification::centredRight);
    g.setColour (kDim);
    g.setFont (type::font (type::kLabel, true));
    const double sr = ctx.engine.getSampleRate();
    g.drawText (sr > 0 ? juce::String (juce::roundToInt (sr / 1000.0)) + "K" : juce::String ("48K"), r.removeFromRight (60), juce::Justification::centredRight);
}

juce::Rectangle<int> Display::getTabBounds (int i) const
{
    const int w = juce::jmin (130, tabsArea.getWidth() / juce::jmax (1, getTabs().size()));
    return { tabsArea.getX() + i * w, tabsArea.getY(), w, tabsArea.getHeight() };
}

juce::Rectangle<int> Display::getEncoderCell (int i) const
{
    const int w = encoderRow.getWidth() / 6;
    return { encoderRow.getX() + i * w, encoderRow.getY(), w, encoderRow.getHeight() };
}

void Display::paintTabs (juce::Graphics& g, juce::Rectangle<int> r)
{
    const auto tabs = getTabs();
    g.setFont (type::font (type::kLabel, true));
    for (int i = 0; i < tabs.size(); ++i)
    {
        auto cell = getTabBounds (i).reduced (1, 3);
        const bool on = i == tab;
        g.setColour (on ? theme.accent : kPanel);
        g.fillRoundedRectangle (cell.toFloat(), 4.0f);
        g.setColour (on ? theme.textOnAccent : kDim);
        g.drawText (tabs[i], cell, juce::Justification::centred);
    }
    r.removeFromLeft (getTabBounds (tabs.size()).getX() - r.getX());
    if (numLayers() > 1)
    {
        g.setColour (kDim);
        g.setFont (type::font (type::kCaption, true));
        g.drawText ("layer " + juce::String (layer + 1) + "/" + juce::String (numLayers()), r.reduced (8, 0), juce::Justification::centredRight);
    }
}

void Display::paintRows (juce::Graphics& g, juce::Rectangle<int> r, const juce::StringArray& left, const juce::StringArray& right)
{
    const int h = juce::jmin (30, r.getHeight() / juce::jmax (1, left.size()));
    for (int i = 0; i < left.size(); ++i)
    {
        auto row = r.removeFromTop (h);
        g.setColour (i % 2 == 0 ? kPanel : kGlass);
        g.fillRect (row);
        g.setColour (kDim);
        g.setFont (type::font (type::kLabel, true));
        g.drawText (left[i], row.reduced (10, 0).removeFromLeft (row.getWidth() / 2), juce::Justification::centredLeft);
        g.setColour (theme.text);
        g.setFont (type::font (type::kLabel));
        g.drawText (right[i], row.reduced (10, 0), juce::Justification::centredRight);
    }
}

void Display::paintHomeFlow (juce::Graphics& g, juce::Rectangle<int> r, Strip& s)
{
    // The signal path as the X32's home tab draws it: input, gate, (insert),
    // EQ and dynamics in their order, the output.
    auto& p = s.params;
    const int order = p.processOrder.load();
    const bool dynPre = (order & 1) != 0, insPre = (order & 2) != 0;
    struct Block
    {
        juce::String name, value;
        bool on;
    };
    auto dbText = [] (float db) { return db <= -120.0f ? juce::String ("-oo") : juce::String (db, 1) + " dB"; };
    const auto gateThr = [&] { const int i = models::model (models::Dynamics, p.gate().model.load()).find ("thr"); return i >= 0 ? p.gate().values[i].load() : 0.0f; };
    const auto dynThr = [&] { const int i = models::model (models::Dynamics, p.dynamics().model.load()).find ("thr"); return i >= 0 ? p.dynamics().values[i].load() : 0.0f; };
    std::vector<Block> blocks { { "INPUT", dbText (p.input.trimDb.load()), true }, { "LOW CUT", (formatHz (p.filter().lowCutFreq.load()) + " Hz"), p.filter().lowCut.load() } };
    if (s.getKind() == StripKind::Channel)
        blocks.push_back ({ "GATE", dbText (gateThr()), p.gate().on.load() });
    const Block insert { "INSERT", p.preInsert().fx.load() >= 0 ? "FX " + juce::String (p.preInsert().fx.load() + 1) : juce::String ("-"), p.preInsert().on.load() };
    const Block eq { "EQ", {}, p.eq().on.load() }, dyn { "DYN", dbText (dynThr()), p.dynamics().on.load() };
    if (insPre)
        blocks.push_back (insert);
    blocks.push_back (dynPre ? dyn : eq);
    blocks.push_back (dynPre ? eq : dyn);
    if (! insPre)
        blocks.push_back (insert);
    blocks.push_back ({ "FADER", dbText (p.faderDb.load()), ! p.mute.load() });

    auto row = r.reduced (16, 0).withSizeKeepingCentre (r.getWidth() - 32, 96);
    const int n = (int) blocks.size();
    const int gap = 14, w = (row.getWidth() - (n - 1) * gap) / n;
    for (int i = 0; i < n; ++i)
    {
        auto cell = row.removeFromLeft (w);
        row.removeFromLeft (gap);
        const auto& b = blocks[(size_t) i];
        g.setColour (b.on ? kPanel.brighter (0.12f) : kPanel);
        g.fillRoundedRectangle (cell.toFloat(), 6.0f);
        g.setColour (b.on ? kAmber : kLine);
        g.drawRoundedRectangle (cell.toFloat().reduced (0.5f), 6.0f, b.on ? 2.0f : 1.0f);
        g.setColour (b.on ? theme.text : kDim);
        g.setFont (type::font (type::kLabel, true));
        g.drawText (b.name, cell.withHeight (cell.getHeight() / 2), juce::Justification::centredBottom);
        g.setFont (type::font (type::kCaption));
        g.drawText (b.value, cell.withTrimmedTop (cell.getHeight() / 2 + 4), juce::Justification::centredTop);
        if (i + 1 < n)
        {
            g.setColour (kLine.brighter (0.4f));
            const float y = (float) cell.getCentreY(), x = (float) cell.getRight();
            g.drawArrow ({ x + 2.0f, y, x + (float) gap - 2.0f, y }, 1.5f, 6.0f, 6.0f);
        }
    }
    // The output meter under the path.
    const float level = ctx.meters.get (&s, MeterPoint::Output, 0);
    auto bar = r.removeFromBottom (18).reduced (16, 4).toFloat();
    g.setColour (kPanel);
    g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (theme.meterLow);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, level)), 3.0f);
}

void Display::paintSends (juce::Graphics& g, juce::Rectangle<int> r, Strip& s)
{
    const int buses = juce::jmin (16, (int) ctx.engine.getBuses().size());
    if (! s.isInput() || buses == 0)
    {
        g.setColour (kDim);
        g.setFont (type::font (type::kLabel));
        g.drawText ("No bus sends on this strip", r, juce::Justification::centred);
        return;
    }
    r.reduce (16, 12);
    const int w = r.getWidth() / buses;
    for (int i = 0; i < buses; ++i)
    {
        auto col = r.removeFromLeft (w).reduced (3, 0);
        auto* send = s.sends.find (i);
        const bool on = send != nullptr && send->enabled.load();
        const float db = send != nullptr ? send->levelDb.load() : -144.0f;
        const float pos = fader::dbToPos (db);
        const bool focused = i / 4 == sendBank;
        auto label = col.removeFromBottom (22);
        g.setColour (focused ? kAmber : kDim);
        g.setFont (type::font (type::kCaption, true));
        g.drawText (juce::String (i + 1), label, juce::Justification::centred);
        g.setColour (kPanel);
        g.fillRoundedRectangle (col.toFloat(), 3.0f);
        g.setColour (on ? theme.accent : kLine);
        g.fillRoundedRectangle (col.toFloat().withTop ((float) col.getBottom() - (float) col.getHeight() * pos), 3.0f);
    }
}

void Display::paintMeters (juce::Graphics& g, juce::Rectangle<int> r)
{
    std::vector<Strip*> strips;
    auto add = [&strips] (const MixerEngine::StripList& list, int from, int count)
    {
        for (int i = from; i < from + count && i < (int) list.size(); ++i)
            strips.push_back (list[(size_t) i].get());
    };
    auto& e = ctx.engine;
    switch (tab)
    {
        case 0: add (e.getChannels(), 0, 32); break;
        case 1: add (e.getBuses(), 0, 16); add (e.getMatrices(), 0, 6); add (e.getMains(), 0, 2); break;
        case 2: add (e.getAux(), 0, 16); break;
        default: add (e.getChannels(), 0, 32); add (e.getAux(), 0, 16); break;
    }
    if (strips.empty())
        return;
    r.reduce (12, 10);
    const int w = juce::jmax (6, r.getWidth() / (int) strips.size());
    for (auto* s : strips)
    {
        auto col = r.removeFromLeft (w).reduced (2, 0);
        auto label = col.removeFromBottom (18);
        g.setColour (kDim);
        g.setFont (type::font (10.0f, true));
        g.drawFittedText (juce::String (s->getNumber()), label, juce::Justification::centred, 1, 0.6f);
        g.setColour (kPanel);
        g.fillRect (col);
        const float level = juce::jlimit (0.0f, 1.0f, fader::dbToPos (juce::Decibels::gainToDecibels (ctx.meters.get (s, MeterPoint::Output, 0), -144.0f)));
        const int h = juce::roundToInt ((float) col.getHeight() * level);
        auto lit = col.removeFromBottom (h);
        g.setColour (level > 0.75f ? theme.meterHigh : level > 0.6f ? theme.meterMid : theme.meterLow);
        g.fillRect (lit);
    }
}

void Display::paintContent (juce::Graphics& g, juce::Rectangle<int> r)
{
    auto* s = selected();
    switch (screen)
    {
        case Screen::Home:
            if (s == nullptr)
            {
                g.setColour (kDim);
                g.setFont (type::font (type::kValue));
                g.drawText ("Select a channel", r, juce::Justification::centred);
                return;
            }
            if (tab == 0)
                paintHomeFlow (g, r, *s);
            else if (tab == 1)
            {
                auto& p = s->params;
                paintRows (g, r.reduced (12), { "SOURCE", "GAIN", "LOW CUT", "DELAY", "INSERT" },
                           { p.inputLeft.load() >= 0 ? "IN " + juce::String (p.inputLeft.load() + 1) : juce::String ("OFF"),
                             juce::String (p.input.trimDb.load(), 1) + " dB",
                             (p.filter().lowCut.load() ? "ON  " : "OFF  ") + (formatHz (p.filter().lowCutFreq.load()) + " Hz"),
                             (p.input.delayOn.load() ? "ON  " : "OFF  ") + juce::String (p.input.delay.load(), 1),
                             ((p.processOrder.load() & 2) != 0 ? "PRE  " : "POST  ")
                                 + (p.preInsert().fx.load() >= 0 ? "FX " + juce::String (p.preInsert().fx.load() + 1) : juce::String ("-")) });
            }
            else if (tab == 5)
                paintSends (g, r, *s);
            else if (tab == 6)
            {
                auto& p = s->params;
                const bool mc = ctx.engine.getMains().size() > 1;
                paintRows (g, r.reduced (12), { "LR", "PAN", "M/C", "M/C LEVEL", "FADER" },
                           { s->mains[0].on.load() ? "ON" : "OFF", juce::String (juce::roundToInt (p.pan.load())),
                             mc && s->mains[1].on.load() ? "ON" : "OFF",
                             mc ? juce::String (s->mains[1].levelDb.load(), 1) + " dB" : juce::String ("-"),
                             juce::String (p.faderDb.load(), 1) + " dB" + (p.mute.load() ? "  MUTED" : "") });
            }
            break; // gate, dyn and eq: their graphs (child components)
        case Screen::Meters:
            paintMeters (g, r);
            break;
        case Screen::Library:
        {
            auto& show = ctx.show;
            const int n = show.getNumScenes();
            juce::StringArray left, right;
            // The rows that fit, around the scene the encoder points at.
            const int rows = juce::jmax (1, (r.getHeight() - 44) / 26);
            const int from = juce::jlimit (0, juce::jmax (0, n - rows), selectedScene - rows / 2);
            for (int i = from; i < juce::jmin (n, from + rows); ++i)
            {
                const auto scene = show.getScene (i);
                left.add ((i == selectedScene ? "> " : "  ") + juce::String (i + 1).paddedLeft ('0', 2));
                right.add (scene.has_value() ? scene->name : juce::String());
            }
            if (n == 0)
            {
                left.add ("No scenes");
                right.add ({});
            }
            auto list = r.reduced (12);
            if (libraryStatus.isNotEmpty())
            {
                g.setColour (kAmber);
                g.setFont (type::font (type::kCaption, true));
                g.drawFittedText (libraryStatus, list.removeFromBottom (20), juce::Justification::centredLeft, 1, 0.8f);
            }
            paintRows (g, list, left, right);
            break;
        }
        case Screen::Effects:
        {
            auto& fx = ctx.engine.getFx();
            juce::StringArray left, right;
            for (int i = 0; i < juce::jmin (8, fx.getNumSlots()); ++i)
            {
                left.add ("FX " + juce::String (i + 1));
                const int m = fx.getModel (i);
                right.add (m > ld::kFxExt ? models::model (models::Fx, m).name : juce::String ("-"));
            }
            paintRows (g, r.reduced (12), left, right);
            break;
        }
        case Screen::Routing:
            paintRouting (g, r.reduced (12));
            break;
        case Screen::Setup:
            if (tab == 1)
            {
                paintRemote (g, r.reduced (12));
                break;
            }
            paintRows (g, r.reduced (12), { "CONSOLE", "SURFACE", "SAMPLE RATE", "CHANNELS", "BUSES / MATRICES" },
                       { "X32 (Sidecar)", getModel && getModel() == 1 ? "X32 COMPACT" : "X32", juce::String (juce::roundToInt (ctx.engine.getSampleRate())) + " Hz",
                         juce::String ((int) ctx.engine.getChannels().size()),
                         juce::String ((int) ctx.engine.getBuses().size()) + " / " + juce::String ((int) ctx.engine.getMatrices().size()) });
            break;
        case Screen::MuteGroups:
            break; // its keys
        case Screen::Utility:
            g.setColour (kDim);
            g.setFont (type::font (type::kLabel));
            g.drawFittedText ("Copy, paste and presets: on the Universal Mixer for now (UNIVERSAL MIXER, top right).", r.reduced (24),
                              juce::Justification::centred, 3);
            break;
    }
}

void Display::paintEncoderRow (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (kPanel);
    g.fillRect (r);
    g.setColour (kLine);
    g.fillRect (r.removeFromTop (1));
    const int w = r.getWidth() / 6;
    for (int i = 0; i < 6; ++i)
    {
        auto cell = r.removeFromLeft (w).reduced (4, 4);
        const auto& e = encoders[(size_t) i];
        if (i > 0)
        {
            g.setColour (kLine);
            g.fillRect (cell.getX() - 4, cell.getY(), 1, cell.getHeight());
        }
        if (! e.turn.isValid() && ! e.push)
            continue;
        auto top = cell.removeFromTop (cell.getHeight() / 2);
        g.setFont (type::font (type::kCaption, true));
        if (e.push)
        {
            // What a push does, lit while on; the turn's name beside it.
            const bool on = e.pushOn && e.pushOn();
            auto chip = top.removeFromRight (juce::jmin (90, top.getWidth() / 2)).reduced (0, 2).toFloat();
            top.removeFromRight (4);
            g.setColour (on ? kAmber : kLine);
            g.fillRoundedRectangle (chip, 3.0f);
            g.setColour (on ? juce::Colours::black : kDim);
            g.drawFittedText (e.pushLabel, chip.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
        g.setColour (kDim);
        if (e.turn.isValid() && e.turn.label != e.pushLabel)
            g.drawFittedText (e.turn.label, top, juce::Justification::centredLeft, 1, 0.7f);
        if (e.turn.isValid())
        {
            g.setColour (theme.text);
            g.setFont (type::font (type::kValue, true));
            g.drawFittedText (e.turn.text ? e.turn.text() : juce::String(), cell, juce::Justification::centredLeft, 1, 0.75f);
            // How far it is turned.
            auto bar = cell.removeFromBottom (3).toFloat();
            g.setColour (kLine);
            g.fillRect (bar);
            g.setColour (theme.accent);
            g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, e.turn.get())));
        }
    }
}

//==============================================================================
void Display::mouseDown (const juce::MouseEvent& e)
{
    dragEncoder = -1;
    dragged = false;
    const auto p = e.getPosition();
    for (int t = 0; t < getTabs().size(); ++t)
        if (getTabBounds (t).contains (p))
        {
            showTab (t);
            return;
        }
    if (encoderRow.contains (p))
    {
        for (int i = 0; i < 6; ++i)
            if (getEncoderCell (i).contains (p))
                dragEncoder = i;
        if (dragEncoder < 0)
            return;
        dragStartY = (float) p.y;
        const auto& enc = encoders[(size_t) dragEncoder];
        dragStartValue = enc.turn.isValid() ? enc.turn.get() : 0.0f;
    }
}

void Display::mouseDrag (const juce::MouseEvent& e)
{
    if (dragEncoder < 0)
        return;
    auto& enc = encoders[(size_t) dragEncoder];
    if (! enc.turn.isValid())
        return;
    const float delta = (dragStartY - (float) e.getPosition().y) / 200.0f;
    if (std::abs (delta) > 0.01f)
        dragged = true;
    float v = juce::jlimit (0.0f, 1.0f, dragStartValue + delta);
    if (enc.turn.steps > 1.0f)
        v = std::round (v * (enc.turn.steps - 1.0f)) / (enc.turn.steps - 1.0f);
    enc.turn.set (v);
    repaint (encoderRow);
}

void Display::mouseUp (const juce::MouseEvent&)
{
    // A tap on the encoder row is the encoder's push.
    if (dragEncoder >= 0 && ! dragged)
        pushEncoder (dragEncoder);
    dragEncoder = -1;
}

void Display::resized()
{
    auto area = getLocalBounds();
    // The screen keys and the page and layer keys at the right of the bezel.
    auto side = area.removeFromRight (kSideKeys);
    area.removeFromRight (sizes::kGap);
    auto knobsRow = area.removeFromBottom (kKnobs);
    area.removeFromBottom (sizes::kGapSmall);
    screenArea = area.reduced (6);

    // Eight screen keys, PAGE < >, LAYER ^ and v.
    const int keyH = juce::jmin (36, (side.getHeight() - 10 * sizes::kGapSmall - sizes::kGap) / 11);
    for (auto& k : screenKeys)
    {
        k->setBounds (side.removeFromTop (keyH));
        side.removeFromTop (sizes::kGapSmall);
    }
    side.removeFromTop (sizes::kGap);
    auto pages = side.removeFromTop (keyH);
    pagePrev.setBounds (pages.removeFromLeft (pages.getWidth() / 2).reduced (1, 0));
    pageNext.setBounds (pages.reduced (1, 0));
    side.removeFromTop (sizes::kGapSmall);
    layerUp.setBounds (side.removeFromTop (keyH));
    side.removeFromTop (sizes::kGapSmall);
    layerDown.setBounds (side.removeFromTop (keyH));

    auto screenInner = screenArea;
    statusArea = screenInner.removeFromTop (kStatus);
    tabsArea = screenInner.removeFromTop (kTabs).reduced (8, 0);
    encoderRow = screenInner.removeFromBottom (kEncoderRow);
    contentArea = screenInner.reduced (0, 4);
    for (auto* v : std::initializer_list<juce::Component*> { &eqView, &gateView, &dynView })
        v->setBounds (contentArea.reduced (12, 4));
    {
        auto grid = contentArea.reduced (40, 30);
        const int w = grid.getWidth() / 3, h = grid.getHeight() / 2;
        for (int i = 0; i < 6; ++i)
            muteGroupKeys[(size_t) i]->setBounds (juce::Rectangle<int> (grid.getX() + (i % 3) * w, grid.getY() + (i / 3) * h, w, h).reduced (10));
    }

    // The six encoders under the screen, each with its push key.
    knobsRow.reduce (6, 0);
    const int w = knobsRow.getWidth() / 6;
    for (int i = 0; i < 6; ++i)
    {
        auto cell = knobsRow.removeFromLeft (w).reduced (8, 0);
        pushKeys[(size_t) i]->setBounds (cell.removeFromBottom (26));
        knobs[(size_t) i].setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), cell.getHeight()), cell.getHeight()));
    }
}

} // namespace livedaw::ui::x32
