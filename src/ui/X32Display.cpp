#include "ui/X32Display.h"

#include "engine/Analysis.h"
#include "engine/MidiPorts.h"
#include "engine/UsbRecorder.h"
#include "engine/Models.h"
#include "engine/Show.h"
#include "engine/WingRemote.h"
#include "engine/X32Midi.h"
#include "ui/HardwareLook.h"
#include "ui/HomeScreens.h"
#include "ui/Icons.h"
#include "ui/ShowPanel.h"
#include "ui/X32AssignSection.h"
#include "ui/MeterBank.h"

namespace livedaw::ui::x32
{

using namespace universal;

namespace
{
    // The screen's look, as the X32's: black glass, its blocks light blue with
    // navy text and darker blue edges, yellow for what is on, the scene and the
    // clock in amber.
    const juce::Colour kGlass (0xff000000), kSky (0xff66c6f2), kSkyAlt (0xff52b4e4), kSkyLine (0xff2a6db5);
    const juce::Colour kNavy (0xff0f2747), kDeep (0xff1c4687), kDeepText (0xffa9d2f5), kYellow (0xffffd02a);
    const juce::Colour kAmber (0xffffc21a), kWell (0xff111315), kCurve (0xffffa21f), kGr (0xffe5321e), kBox (0xfff4f4f2);
    const juce::Colour kDim (0xff6f9fd0);

    /** The theme the screen's graphs (EQ, gate, dynamics) draw with. */
    Theme screenThemeFrom (const Theme& t)
    {
        auto s = t;
        s.well = kWell;
        s.outline = juce::Colour (0xff3a4048);
        s.textDim = kDeepText;
        s.text = juce::Colours::white;
        s.background = kNavy;
        s.accent = kCurve;
        s.meterMid = kGr;
        return s;
    }

    /** The screen keys' colours when lit, as on the console: HOME green, METERS
        amber, MUTE GRP red, UTILITY green, the others white. */
    const juce::Colour kScreenKeyColours[] = { juce::Colour (0xff3ddc5a), juce::Colour (0xffffb21f), juce::Colour (0xffeef2f4),
                                               juce::Colour (0xffeef2f4), juce::Colour (0xffeef2f4), juce::Colour (0xffeef2f4),
                                               juce::Colour (0xffff4b3e), juce::Colour (0xff3ddc5a) };

    constexpr int kStatus = 46, kTabs = 24, kEncoderRow = 74, kSideKeys = 104, kKnobs = 92, kBezelMeters = 64;

    /** A block of the screen: light blue, its darker edge, its title along the top. */
    juce::Rectangle<float> block (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
    {
        g.setColour (kSky);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (kSkyLine);
        g.drawRoundedRectangle (r.reduced (0.75f), 4.0f, 1.5f);
        auto inner = r.reduced (3.0f, 2.0f);
        if (title.isNotEmpty())
        {
            g.setColour (kNavy);
            g.setFont (type::font (10.0f, true));
            g.drawText (title, inner.removeFromTop (13.0f), juce::Justification::centred);
        }
        return inner;
    }

    /** A switch's chip: yellow while on, deep blue while off. */
    void chip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool on)
    {
        g.setColour (on ? kYellow : kDeep);
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (on ? kNavy : kDeepText);
        g.setFont (type::font (juce::jmin (10.5f, r.getHeight() * 0.8f), true));
        g.drawFittedText (text, r.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }

    /** A value in its white box. */
    void valueBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text)
    {
        g.setColour (kBox);
        g.fillRect (r);
        g.setColour (kNavy.withAlpha (0.6f));
        g.drawRect (r, 1.0f);
        g.setColour (kNavy);
        g.setFont (type::font (juce::jmin (11.0f, r.getHeight() * 0.8f), true));
        g.drawFittedText (text, r.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }

    /** A knob's icon: a navy ring, its value a yellow arc (from the top if centred). */
    void knobIcon (juce::Graphics& g, juce::Point<float> c, float radius, float fraction, bool centred = false)
    {
        constexpr float span = juce::MathConstants<float>::pi * 5.0f / 3.0f;
        fraction = juce::jlimit (0.0f, 1.0f, fraction);
        const float a0 = centred ? 0.0f : -span * 0.5f, a1 = -span * 0.5f + span * fraction;
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, radius + 2.0f, radius + 2.0f, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
        g.setColour (kYellow);
        g.strokePath (arc, juce::PathStrokeType (3.0f));
        g.setColour (kNavy);
        g.drawEllipse (juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (c), 1.5f);
        g.drawLine (c.x + radius * 0.2f * std::sin (a1), c.y - radius * 0.2f * std::cos (a1), c.x + radius * std::sin (a1),
                    c.y - radius * std::cos (a1), 1.5f);
    }

    /** A check box and its label. */
    void checkBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, bool on)
    {
        auto box = r.removeFromLeft (r.getHeight()).reduced (1.5f);
        g.setColour (on ? kCurve : kBox);
        g.fillRect (box);
        g.setColour (kNavy);
        g.drawRect (box, 1.0f);
        g.setFont (type::font (juce::jmin (10.0f, r.getHeight()), false));
        g.drawFittedText (label, r.withTrimmedLeft (3.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }

    /** A level meter in its dark well (linear level), from the bottom. */
    void levelBar (juce::Graphics& g, juce::Rectangle<float> r, float level)
    {
        g.setColour (kWell);
        g.fillRect (r);
        const float pos = juce::jlimit (0.0f, 1.0f, fader::dbToPos (juce::Decibels::gainToDecibels (level, -144.0f)));
        auto lit = r.reduced (1.0f).withTrimmedTop ((1.0f - pos) * (r.getHeight() - 2.0f));
        g.setGradientFill (juce::ColourGradient (kGr, r.getX(), r.getY(), juce::Colour (0xff3ddc5a), r.getX(), r.getBottom(), false));
        g.fillRect (lit);
    }

    // The routing blocks' choices (OSC document p.22), as livedaw-x32 names them.
    const juce::StringArray kRoutingIn { "AN1-8", "AN9-16", "AN17-24", "AN25-32", "A1-8", "A9-16", "A17-24", "A25-32",
                                         "A33-40", "A41-48", "B1-8", "B9-16", "B17-24", "B25-32", "B33-40", "B41-48",
                                         "CARD1-8", "CARD9-16", "CARD17-24", "CARD25-32", "UIN1-8", "UIN9-16", "UIN17-24", "UIN25-32" };
    const juce::StringArray kRoutingAux { "AUX1-4", "AN1-2", "AN1-4", "AN1-6", "A1-2", "A1-4", "A1-6", "B1-2",
                                          "B1-4", "B1-6", "CARD1-2", "CARD1-4", "CARD1-6", "UIN1-2", "UIN1-4", "UIN1-6" };
    const char* const kInBlocks[] = { "1-8", "9-16", "17-24", "25-32", "AUX" };
    // The output blocks' choices: local outputs in fours (OUT/1-4 and 9-12, then
    // 5-8 and 13-16), AES50 and the card in eights.
    const juce::StringArray kRoutingOutA { "AN1-4", "AN9-12", "AN17-20", "AN25-28", "A1-4", "A9-12", "A17-20", "A25-28", "A33-36",
                                           "A41-44", "B1-4", "B9-12", "B17-20", "B25-28", "B33-36", "B41-44", "CARD1-4", "CARD9-12",
                                           "CARD17-20", "CARD25-28", "OUT1-4", "OUT9-12", "P161-4", "P169-12", "AUX/CR", "AUX/TB",
                                           "UOUT1-4", "UOUT9-12", "UOUT17-20", "UOUT25-28", "UOUT33-36", "UOUT41-44", "UIN1-4",
                                           "UIN9-12", "UIN17-20", "UIN25-28" };
    const juce::StringArray kRoutingOutB { "AN5-8", "AN13-16", "AN21-24", "AN29-32", "A5-8", "A13-16", "A21-24", "A29-32", "A37-40",
                                           "A45-48", "B5-8", "B13-16", "B21-24", "B29-32", "B37-40", "B45-48", "CARD5-8", "CARD13-16",
                                           "CARD21-24", "CARD29-32", "OUT5-8", "OUT13-16", "P165-8", "P1613-16", "AUX/CR", "AUX/TB",
                                           "UOUT5-8", "UOUT13-16", "UOUT21-24", "UOUT29-32", "UOUT37-40", "UOUT45-48", "UIN5-8",
                                           "UIN13-16", "UIN21-24", "UIN29-32" };
    const juce::StringArray kRoutingNet { "AN1-8", "AN9-16", "AN17-24", "AN25-32", "A1-8", "A9-16", "A17-24", "A25-32", "A33-40",
                                          "A41-48", "B1-8", "B9-16", "B17-24", "B25-32", "B33-40", "B41-48", "CARD1-8", "CARD9-16",
                                          "CARD17-24", "CARD25-32", "OUT1-8", "OUT9-16", "P161-8", "P169-16", "AUX1-6/Mon",
                                          "AuxIN1-6/TB", "UOUT1-8", "UOUT9-16", "UOUT17-24", "UOUT25-32", "UOUT33-40", "UOUT41-48",
                                          "UIN1-8", "UIN9-16", "UIN17-24", "UIN25-32" };
    const juce::StringArray kOutputTaps { "IN/LC", "IN/LC+M", "<-EQ", "<-EQ+M", "EQ->", "EQ->+M", "PRE", "PRE+M", "POST" };
    const char* const kOutBlocks[] = { "1-4", "5-8", "9-12", "13-16" };
    const char* const kNetBlocks[] = { "1-8", "9-16", "17-24", "25-32", "33-40", "41-48" };

    /** An output signal's sources (`/outputs/.../src` 0-76), as the screen names them. */
    const juce::StringArray& outputSources()
    {
        static const juce::StringArray names = []
        {
            juce::StringArray s { "OFF", "MAIN L", "MAIN R", "M/C" };
            for (int i = 1; i <= 16; ++i)
                s.add ("MIXBUS " + juce::String (i).paddedLeft ('0', 2));
            for (int i = 1; i <= 6; ++i)
                s.add ("MATRIX " + juce::String (i));
            for (int i = 1; i <= 32; ++i)
                s.add ("CH " + juce::String (i).paddedLeft ('0', 2) + " DIRECT");
            for (int i = 1; i <= 8; ++i)
                s.add ("AUX " + juce::String (i) + " DIRECT");
            for (int i = 1; i <= 4; ++i)
                s.addArray (juce::StringArray { "FX " + juce::String (i) + "L DIRECT", "FX " + juce::String (i) + "R DIRECT" });
            s.addArray (juce::StringArray { "MONITOR L", "MONITOR R", "TALKBACK" });
            return s;
        }();
        return names;
    }

    /** ROUTING's signal tabs: the `/outputs` group and its count (tabs 1-3). */
    struct SignalTab
    {
        const char* group;
        int count;
        const char* label;
    };
    const SignalTab kSignalTabs[] = { { "main", 16, "OUT" }, { "aux", 6, "AUX OUT" }, { "p16", 16, "P16" } };
    /** Its block tabs (4-6): the node, the blocks, the interface's first output. */
    struct BlockTab
    {
        const char* node;
        int blocks;
        int firstPort;
        const char* label;
    };
    const BlockTab kBlockTabs[] = { { "CARD", 4, 128, "CARD" }, { "AES50A", 6, 32, "AES50 A" }, { "AES50B", 6, 80, "AES50 B" } };

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
    : ctx (c), theme (t), screenTheme (screenThemeFrom (t)), pagePrev (juce::String::fromUTF8 ("\xe2\x97\x80"), t.accent, t),
      pageNext (juce::String::fromUTF8 ("\xe2\x96\xb6"), t.accent, t), layerUp (juce::String::fromUTF8 ("\xe2\x96\xb2"), t.accent, t),
      layerDown (juce::String::fromUTF8 ("\xe2\x96\xbc"), t.accent, t), eqView (screenTheme), gateView (screenTheme), dynView (screenTheme)
{
    for (int s = 0; s < 8; ++s)
    {
        auto key = std::make_unique<ToggleKey> (kScreenNames[s], kScreenKeyColours[s], t);
        key->textLit = true; // black keys, their names in their colours
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
    if (rtaShown)
        ctx.engine.getAnalysis().hideRta();
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
        case Screen::Meters:     return { "channel", "mix bus", "aux / fx", "in / out", "rta" };
        case Screen::Routing:    return { "home", "out 1-16", "aux out", "p16 out", "card out", "aes50-a", "aes50-b" };
        case Screen::Setup:      return { "global", "remote", "scribble strip", "preamps" };
        case Screen::Library:    return { "scenes", "show", "channel", "effects" };
        case Screen::Effects:    return { "home", "fx1", "fx2", "fx3", "fx4", "fx5", "fx6", "fx7", "fx8" };
        case Screen::MuteGroups: return { "mute grp" };
        case Screen::Utility:    return { "copy" };
        case Screen::Monitor:    return { "monitor", "talk a", "talk b", "osc" };
        case Screen::Assign:     return { "set a", "set b", "set c" };
        case Screen::Scenes:     return { "home", "param safe" };
        case Screen::Usb:        return { "home", "config" };
    }
    return {};
}

int Display::numLayers() const
{
    if (screen == Screen::Setup)
        return tab == 1 ? 2 : 1; // remote: the MIDI kinds, then SysEx
    if (screen == Screen::Routing)
        return tab == 1 ? 2 : 1; // out 1-16: the signals, then the local outputs' blocks
    if (screen == Screen::Effects && tab > 0)
    {
        // A slot's parameters, six per layer.
        const int m = ctx.engine.getFx().getModel (tab - 1);
        const int n = m > ld::kFxExt ? (int) models::model (models::Fx, m).params.size() : 0;
        return juce::jmax (1, (n + 5) / 6);
    }
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
    // METERS' RTA: the selected strip's while the tab shows.
    {
        auto analysis = ctx.engine.getAnalysis();
        if (screen == Screen::Meters && tab == 4)
        {
            analysis.showRta (selected(), ld::kRtaViewHome);
            rtaShown = true;
        }
        else if (rtaShown)
        {
            analysis.hideRta();
            rtaShown = false;
        }
    }
    if (screen == Screen::Assign && setAssignSet)
        setAssignSet (tab); // a set's tab shows (and picks) the set
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
    if (rtaShown)
        ctx.engine.getAnalysis().showRta (s, ld::kRtaViewHome); // the RTA follows the selection
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
        // A dynamics slot's KEY SOURCE: SELF, a channel (an input's), or an
        // aux, bus, main or matrix (an output's).
        auto keySource = [&engine, s] (std::atomic<int32_t>& src)
        {
            const bool out = s->isOutput();
            const int n = out ? MixerEngine::kNumOutputKeySources : (int) engine.getChannels().size();
            KnobBinding b;
            b.label = "KEY SRC";
            b.steps = (float) (n + 1);
            b.get = [&src, out, n]
            {
                const int v = src.load();
                const int i = v < 0 ? 0 : 1 + (out ? MixerEngine::outputKeySourceIndex (v) : v);
                return (float) juce::jlimit (0, n, i) / (float) n;
            };
            b.set = [&src, out, n] (float v)
            {
                const int i = juce::roundToInt (v * (float) n);
                src = i == 0 ? -1 : out ? MixerEngine::outputKeySource (i - 1) : i - 1;
            };
            b.text = [&src] { return MixerEngine::keySourceName (src.load()); };
            return b;
        };
        const auto keySolo = [&engine, s] (int mode)
        {
            return std::make_pair (std::function<void()> ([&engine, s, mode] { engine.getAnalysis().toggleListen (*s, mode); }),
                                   std::function<bool()> ([&engine, s, mode] { return engine.getAnalysis().isListening (*s, mode); }));
        };
        const juce::StringArray keyFilters { "FLAT", "LOWPASS", "HIGHPASS", "BANDPASS", "NOTCH" };
        auto faderOf = [&] { return bindFader ("FADER", p.faderDb); };
        // LINK (encoder 1's push, p.45): the strip's odd / even pair linked.
        std::function<void()> linkPush;
        std::function<bool()> linkOn;
        if (const auto path = linkAddress (*s); path.isNotEmpty())
        {
            linkOn = [this, path] { return x32Value (path) == "ON"; };
            linkPush = [this, path, linkOn]
            {
                int set = 0;
                ctx.engine.getWing().runX32Text (path + (linkOn() ? " OFF" : " ON"), set);
                ctx.engine.getWing().followX32Links(); // the even strip takes the odd one's values now
                repaint();
            };
        }
        switch (tab)
        {
            case 0: // home
                if (layer == 0)
                {
                    encoders[0] = encoder (gainOf(), linkPush ? "LINK" : "", linkPush, linkOn);
                    encoders[1] = encoder (bindParam (models::Dynamics, gate.model.load(), gate.values, "thr", "GATE"), "GATE", toggle (gate.on), isOn (gate.on));
                    encoders[2] = encoder (orderBit (2, "INSERT"), "INSERT", toggle (p.preInsert().on), isOn (p.preInsert().on));
                    encoders[3] = encoder (orderBit (1, "DYN POS"), "EQ", toggle (eq.on), isOn (eq.on));
                    encoders[4] = encoder (bindParam (models::Dynamics, dyn.model.load(), dyn.values, "thr", "COMP"), "COMP", toggle (dyn.on), isOn (dyn.on));
                    encoders[5] = encoder (bindPan ("PAN", p.pan), "LR", toggle (s->mains[0].on), isOn (s->mains[0].on));
                }
                else
                {
                    // 48V on the strip's input; a DCA and a mute group picked, PUSH
                    // assigns the strip to it (p.47).
                    const int input = p.inputLeft.load();
                    if (auto* src = input >= 0 && input < 128 ? engine.getSource (input) : nullptr)
                        encoders[0] = encoder ({}, "48V", toggle (src->phantom), isOn (src->phantom));
                    encoders[1] = encoder ({}, "POLARITY", toggle (p.input.invert), isOn (p.input.invert));
                    const auto picker = [this] (const char* label, int& value, int count)
                    {
                        KnobBinding b;
                        b.label = label;
                        b.get = [&value, count] { return count > 1 ? (float) value / (float) (count - 1) : 0.0f; };
                        b.set = [this, &value, count] (float v) { value = juce::roundToInt (v * (float) juce::jmax (0, count - 1)); assignEncoders(); };
                        b.text = [&value] { return juce::String (value + 1); };
                        b.steps = (float) juce::jmax (2, count);
                        return b;
                    };
                    if (engine.getNumDcas() > 0)
                        encoders[2] = encoder (picker ("DCA", homeDca, engine.getNumDcas()), "DCA", [&p, this] { p.dcaMask.toggle (homeDca); },
                                               [&p, this] { return p.dcaMask.contains (homeDca); });
                    encoders[3] = encoder (picker ("MUTE GRP", homeMuteGroup, 6), "MUTE GRP", [&p, this] { p.muteGroupMask.toggle (homeMuteGroup); },
                                           [&p, this] { return p.muteGroupMask.contains (homeMuteGroup); });
                    encoders[4] = encoder ({}, "SOLO", toggle (p.solo), isOn (p.solo));
                    encoders[5] = encoder (faderOf(), "MUTE", toggle (p.mute), isOn (p.mute));
                }
                break;
            case 1: // config
            {
                encoders[0] = encoder (gainOf(), linkPush ? "LINK" : "", linkPush, linkOn);
                encoders[1] = encoder (bindFreq ("LOW CUT", p.filter().lowCutFreq, 20.0f, 400.0f, 20.0f), "LOW CUT", toggle (p.filter().lowCut), isOn (p.filter().lowCut));
                // SOURCE (p.45): turned through the X32's list, SELECT (the push) patches it.
                const auto address = x32Address (*s);
                if (address.startsWith ("/ch/") || address.startsWith ("/auxin/"))
                {
                    const auto path = address + "/config/source";
                    const auto shown = [this, path, s] { return pendingSource >= 0 && pendingSourceStrip == s ? pendingSource : x32Value (path).getIntValue(); };
                    KnobBinding source;
                    source.label = "SOURCE";
                    source.steps = 65.0f;
                    source.get = [shown] { return (float) shown() / 64.0f; };
                    source.set = [this, s] (float v)
                    {
                        pendingSource = juce::jlimit (0, 64, juce::roundToInt (v * 64.0f));
                        pendingSourceStrip = s;
                        repaint();
                    };
                    source.text = [shown] { return sourceName (shown()); };
                    encoders[2] = encoder (source, "SELECT", [this, path, s]
                    {
                        if (pendingSource < 0 || pendingSourceStrip != s)
                            return;
                        int set = 0;
                        ctx.engine.getWing().runX32Text (path + " " + juce::String (pendingSource), set);
                        pendingSource = -1;
                        repaint();
                    }, [this, s] { return pendingSource >= 0 && pendingSourceStrip == s; });
                }
                encoders[3] = encoder (bindLinear ("DELAY", p.input.delay, 0.3f, 500.0f, " ms", 1, 0.3f), "DELAY", toggle (p.input.delayOn), isOn (p.input.delayOn));
                encoders[4] = encoder (orderBit (2, "INSERT POS"), "INSERT", toggle (p.preInsert().on), isOn (p.preInsert().on));
                break;
            }
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
                    // The key (p.48): its filter, frequency, KEY SOLO and source.
                    encoders[2] = encoder (bindChoice ("KEY FILTER", gate.scFilter, keyFilters));
                    encoders[3] = encoder (bindLog ("KEY FREQ", gate.scFreq, 20.0f, 20000.0f, " Hz", 1000.0f));
                    const auto [solo, soloOn] = keySolo (ld::kListenGateKey);
                    encoders[4] = encoder ({}, "KEY SOLO", solo, soloOn);
                    encoders[5] = encoder (keySource (gate.scSource));
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
                    // The key (p.48): its filter on the fifth (KEY SOLO its push), its source.
                    const auto [solo, soloOn] = keySolo (ld::kListenDynKey);
                    encoders[4] = encoder (bindChoice ("KEY FILTER", dyn.scFilter, keyFilters), "KEY SOLO", solo, soloOn);
                    encoders[5] = encoder (keySource (dyn.scSource));
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
    else if (screen == Screen::Setup && tab == 2)
    {
        // SCRIBBLE STRIP (p.56): the selected strip's colour and icon.
        if (s != nullptr)
        {
            KnobBinding colour;
            colour.label = "COLOR";
            colour.get = [s] { return (float) (juce::jlimit (1, 16, s->getColourIndex()) - 1) / 15.0f; };
            colour.set = [this, s] (float v) { s->setColourIndex (1 + juce::roundToInt (v * 15.0f)); repaint(); };
            colour.text = [s] { return juce::String (juce::jlimit (1, 16, s->getColourIndex())); };
            colour.steps = 16.0f;
            encoders[1] = encoder (colour);
            const auto icons = wingIconNumbers();
            KnobBinding icon;
            icon.label = "ICON";
            icon.get = [s, icons]
            {
                const auto at = std::find (icons.begin(), icons.end(), s->params.icon.load());
                return icons.size() > 1 && at != icons.end() ? (float) (at - icons.begin()) / (float) (icons.size() - 1) : 0.0f;
            };
            icon.set = [this, s, icons] (float v)
            {
                if (! icons.empty())
                    s->params.icon = icons[(size_t) juce::roundToInt (v * (float) (icons.size() - 1))];
                repaint();
            };
            icon.text = [s] { return juce::String (s->params.icon.load()); };
            icon.steps = (float) juce::jmax (2, (int) icons.size());
            encoders[2] = encoder (icon, "NO ICON", [this, s] { s->params.icon = 0; repaint(); });
        }
    }
    else if (screen == Screen::Setup && tab == 3)
    {
        // PREAMPS (p.57): a block of four inputs, each its gain, PUSH its 48V.
        KnobBinding block;
        block.label = "INPUTS";
        block.get = [this] { return (float) preampBlock / 7.0f; };
        block.set = [this] (float v) { preampBlock = juce::roundToInt (v * 7.0f); assignEncoders(); };
        block.text = [this] { return juce::String (4 * preampBlock + 1) + "-" + juce::String (4 * preampBlock + 4); };
        block.steps = 8.0f;
        encoders[0] = encoder (block);
        for (int k = 0; k < 4; ++k)
            if (auto* src = engine.getSource (4 * preampBlock + k))
            {
                auto b = bindLinear ("IN " + juce::String (4 * preampBlock + k + 1), src->gainDb, -12.0f, 60.0f, " dB", 1, 0.0f);
                b.steps = 145.0f;
                encoders[(size_t) (k + 1)] = encoder (b, "48V", toggle (src->phantom), isOn (src->phantom));
            }
    }
    else if (screen == Screen::Usb && tab == 0)
    {
        // USB (p.62): the player's transport, and the recorder's.
        auto& usb = engine.getUsbRecorder();
        using P = UsbRecorder::PlayState;
        using R = UsbRecorder::RecState;
        encoders[0] = encoder ({}, "PLAY", [&usb] { usb.togglePlayPause(); }, [&usb] { return usb.getPlayState() == P::Playing; });
        encoders[1] = encoder ({}, "STOP", [&usb] { usb.stop(); });
        encoders[2] = encoder ({}, "PREV", [&usb] { usb.previous(); });
        encoders[3] = encoder ({}, "NEXT", [&usb] { usb.next(); });
        encoders[4] = encoder ({}, "REC", [&usb] { if (usb.isRecording()) usb.stopRecording(); else usb.record(); },
                               [&usb] { return usb.getRecState() == R::Recording; });
        encoders[5] = encoder ({}, "PAUSE REC", [&usb] { usb.toggleRecPause(); }, [&usb] { return usb.getRecState() == R::Paused; });
    }
    else if (screen == Screen::Usb)
    {
        // CONFIG: what it records.
        auto& usb = engine.getUsbRecorder();
        encoders[0] = encoder ({}, "4 TRACKS", [&usb] { usb.setRecChannels (usb.getRecChannels() == 4 ? 2 : 4); }, [&usb] { return usb.getRecChannels() == 4; });
        encoders[1] = encoder ({}, "24 BIT", [&usb] { usb.setRecBits (usb.getRecBits() == 24 ? 16 : 24); }, [&usb] { return usb.getRecBits() == 24; });
    }
    else if (screen == Screen::Setup && tab == 1)
    {
        assignRemoteEncoders();
    }
    else if (screen == Screen::Routing)
    {
        assignRoutingEncoders();
    }
    else if (screen == Screen::Effects)
    {
        assignEffectsEncoders();
    }
    else if (screen == Screen::Setup && tab == 0 && getModel && setModel)
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
    else if (screen == Screen::Library && tab >= 2)
    {
        // CHANNEL and EFFECTS (p.58): a preset picked, LOAD onto the selected
        // strip (or an FX slot), SAVE the strip (the slot) as a new one; the
        // library's presets, as the WING's library keeps them.
        const bool channel = tab == 2;
        const auto list = presets (channel ? Show::channelPresetExtension : Show::fxPresetExtension);
        presetChoice = juce::jlimit (0, juce::jmax (0, list.size() - 1), presetChoice);
        KnobBinding pick;
        pick.label = "PRESET";
        const int n = list.size();
        pick.get = [this, n] { return n > 1 ? (float) presetChoice / (float) (n - 1) : 0.0f; };
        pick.set = [this, n] (float v) { presetChoice = juce::roundToInt (v * (float) juce::jmax (0, n - 1)); repaint(); };
        pick.text = [this, list] { return list.isEmpty() ? juce::String ("-") : juce::String (presetChoice + 1); };
        pick.steps = (float) juce::jmax (2, n);
        auto& show = ctx.show;
        if (channel)
        {
            encoders[0] = encoder (pick, "LOAD", [this, list, &show]
            {
                if (auto* st = selected(); st != nullptr && juce::isPositiveAndBelow (presetChoice, list.size()))
                    libraryStatus = show.loadChannelPreset (state::StripRef::of (*st), list[presetChoice]).wasOk() ? "Loaded " + list[presetChoice].getFileNameWithoutExtension() : juce::String ("Load failed");
                refreshStrip();
            });
            encoders[1] = encoder ({}, "SAVE", [this, &show]
            {
                auto* st = selected();
                if (st == nullptr)
                    return;
                const auto name = st->getName().isNotEmpty() ? st->getName() : st->getLabel();
                const auto file = ShowPanel::libraryFolder().getNonexistentChildFile (juce::File::createLegalFileName (name), Show::channelPresetExtension);
                const auto r = show.saveChannelPreset (state::StripRef::of (*st), state::AllSections & ~(juce::uint32) state::Routing, file, name);
                libraryStatus = r.wasOk() ? "Saved " + file.getFileNameWithoutExtension() : r.getErrorMessage();
                assignEncoders();
            });
        }
        else
        {
            KnobBinding slot;
            slot.label = "FX SLOT";
            slot.get = [this] { return (float) presetSlot / 7.0f; };
            slot.set = [this] (float v) { presetSlot = juce::roundToInt (v * 7.0f); repaint(); };
            slot.text = [this] { return juce::String (presetSlot + 1); };
            slot.steps = 8.0f;
            encoders[0] = encoder (pick, "LOAD", [this, list, &show]
            {
                if (juce::isPositiveAndBelow (presetChoice, list.size()))
                    libraryStatus = show.loadFxPreset (presetSlot, list[presetChoice]).wasOk() ? "Loaded " + list[presetChoice].getFileNameWithoutExtension() : juce::String ("Load failed");
                repaint();
            });
            encoders[1] = encoder (slot, "SAVE", [this, &show]
            {
                const auto name = "FX" + juce::String (presetSlot + 1);
                const auto file = ShowPanel::libraryFolder().getNonexistentChildFile (name, Show::fxPresetExtension);
                const auto r = show.saveFxPreset (presetSlot, file, name);
                libraryStatus = r.wasOk() ? "Saved " + file.getFileNameWithoutExtension() : r.getErrorMessage();
                assignEncoders();
            });
        }
    }
    else if (screen == Screen::Library && tab == 1)
    {
        // The show file: NEW, OPEN, SAVE, SAVE AS (the app's, as on every skin).
        auto call = [] (const std::function<void()>& f) { return [f] { if (f) f(); }; };
        encoders[0] = encoder ({}, "NEW SHOW", call (ctx.newShow));
        encoders[1] = encoder ({}, "OPEN", call (ctx.openShow));
        encoders[2] = encoder ({}, "SAVE", [this] { if (ctx.saveShow) ctx.saveShow (false); });
        encoders[3] = encoder ({}, "SAVE AS", [this] { if (ctx.saveShow) ctx.saveShow (true); });
    }
    else if (screen == Screen::Monitor && tab == 0)
    {
        // MONITOR (p.60): the speakers' and phones' levels, DIM and its depth,
        // MONO, the channels' solo (PFL or AFL), and the source.
        encoders[0] = encoder (bindFader ("MONITOR", engine.monitorLevelDb));
        encoders[1] = encoder (bindFader ("PHONES", engine.phones.levelDb));
        encoders[2] = encoder (bindLinear ("DIM ATT", engine.monitorDimDb, 0.0f, 40.0f, " dB", 0, 20.0f), "DIM", toggle (engine.monitorDim),
                               isOn (engine.monitorDim));
        encoders[3] = encoder ({}, "MONO", toggle (engine.monitorMono), isOn (engine.monitorMono));
        encoders[4] = encoder ({}, "AFL", toggle (engine.soloAfl), isOn (engine.soloAfl));
        juce::StringArray sources;
        for (int i = 0; i < ld::kNumMonitorSources; ++i)
            sources.add (MixerEngine::monitorSourceName (i));
        encoders[5] = encoder (bindChoice ("SOURCE", engine.speakers.source, sources));
    }
    else if (screen == Screen::Monitor && tab < 3)
    {
        // TALKBACK A or B (p.61): the talk level, the key, its auto-dim, and
        // where it talks to (MAIN, a bus picked by the fifth encoder).
        auto& path = engine.talk.paths[(size_t) (tab - 1)];
        encoders[0] = encoder (bindFader ("TALK", engine.talk.levelDb));
        encoders[1] = encoder ({}, tab == 1 ? "TALK A" : "TALK B", toggle (path.on), isOn (path.on));
        encoders[2] = encoder (bindLinear ("AUTO DIM", path.monDimDb, 0.0f, 40.0f, " dB", 0, 20.0f));
        encoders[3] = encoder ({}, "TO MAIN", [&path] { path.mains = path.mains.load() ^ 1u; }, [&path] { return (path.mains.load() & 1u) != 0; });
        KnobBinding bus;
        bus.label = "BUS";
        bus.get = [this] { return (float) talkBus / 15.0f; };
        bus.set = [this] (float v) { talkBus = juce::roundToInt (v * 15.0f); assignEncoders(); };
        bus.text = [this] { return juce::String (talkBus + 1); };
        bus.steps = 16.0f;
        encoders[4] = encoder (bus, "TO BUS", [this, &path] { path.buses = path.buses.load() ^ (1u << talkBus); },
                               [this, &path] { return (path.buses.load() & (1u << talkBus)) != 0; });
    }
    else if (screen == Screen::Monitor)
    {
        // OSCILLATOR (p.62): its level, frequency and kind; on, its source's mute off.
        if (auto* osc = engine.getOscillator (0))
        {
            encoders[0] = encoder (bindLinear ("LEVEL", osc->levelDb, -96.0f, 0.0f, " dB", 1, -20.0f));
            encoders[1] = encoder (bindFreq ("FREQ", osc->freqHz, 20.0f, 20000.0f, 1000.0f));
            encoders[2] = encoder (bindChoice ("TYPE", osc->mode, { "SINE", "PINK", "WHITE" }));
            if (auto* src = engine.getSource (MixerEngine::oscSource (0)))
                encoders[5] = encoder ({}, "OSC ON", [src] { src->mute = ! src->mute.load(); }, [src] { return ! src->mute.load(); });
        }
    }
    else if (screen == Screen::Meters && tab == 4)
    {
        encoders[0] = encoder ({}, "FREEZE", [&engine] { auto a = engine.getAnalysis(); a.setFrozen (! a.isFrozen()); },
                               [&engine] { return engine.getAnalysis().isFrozen(); });
    }
    else if (screen == Screen::Scenes && tab == 0)
    {
        // SCENES (p.65): a scene picked by the first encoder, GO; PREV, NEXT,
        // SAVE into it, SKIP it, a NEW one at the end.
        auto& show = ctx.show;
        KnobBinding scene;
        scene.label = "SCENE";
        scene.get = [this, &show] { return show.getNumScenes() > 1 ? (float) selectedScene / (float) (show.getNumScenes() - 1) : 0.0f; };
        scene.set = [this, &show] (float v) { selectedScene = juce::roundToInt (v * (float) juce::jmax (0, show.getNumScenes() - 1)); repaint(); };
        scene.text = [this] { return juce::String (selectedScene + 1).paddedLeft ('0', 2); };
        scene.steps = (float) juce::jmax (2, show.getNumScenes());
        const auto valid = [this, &show] { return juce::isPositiveAndBelow (selectedScene, show.getNumScenes()); };
        encoders[0] = encoder (scene, "GO", [&show, this, valid] { if (valid()) show.recallScene (selectedScene); });
        encoders[1] = encoder ({}, "PREV", [&show] { show.recallPrevious(); });
        encoders[2] = encoder ({}, "NEXT", [&show] { show.recallNext(); });
        encoders[3] = encoder ({}, "SAVE", [&show, this, valid] { if (valid()) show.storeScene (selectedScene); });
        encoders[4] = encoder ({}, "SKIP", [&show, this, valid]
        {
            if (valid())
                show.setSceneSkip (selectedScene, ! show.getScene (selectedScene)->skip);
        }, [&show, this, valid] { return valid() && show.getScene (selectedScene)->skip; });
        encoders[5] = encoder ({}, "NEW", [&show, this]
        {
            selectedScene = show.addScene ("Scene " + juce::String (show.getNumScenes() + 1).paddedLeft ('0', 2));
            assignEncoders();
        });
    }
    else if (screen == Screen::Scenes)
    {
        // PARAM SAFE (p.66): the sections a recall leaves alone.
        KnobBinding section;
        section.label = "SECTION";
        section.get = [this] { return (float) safeSection / (float) (state::kNumSections - 1); };
        section.set = [this] (float v) { safeSection = juce::roundToInt (v * (float) (state::kNumSections - 1)); repaint(); };
        section.text = [this] { return juce::String (state::sectionName (state::sectionAt (safeSection))).toUpperCase(); };
        section.steps = (float) state::kNumSections;
        auto& show = ctx.show;
        encoders[0] = encoder (section, "SAFE", [this, &show]
        {
            auto safe = show.getGlobalSafe();
            safe.sections ^= (juce::uint32) state::sectionAt (safeSection);
            show.setGlobalSafe (safe);
        }, [this, &show] { return (show.getGlobalSafe().sections & (juce::uint32) state::sectionAt (safeSection)) != 0; });
    }
    else if (screen == Screen::Assign && assignCode && setAssignCode)
    {
        // A set's tab (p.64): the control, what kind of thing it does, its
        // target (and a send's bus); a change writes the control's code.
        const bool button = assignControl >= 4;
        const juce::StringArray kinds = button ? juce::StringArray { "-", "MUTE", "INSERT", "MUTE GRP", "SCENE GO" } : juce::StringArray { "-", "FADER", "PAN", "SEND" };
        const auto code = assignCode (assignControl);
        const int xx = code.substring (1, 3).getIntValue();
        const int kind = code == "-" || code.isEmpty() ? 0
                       : button ? (code[0] == 'O' ? (xx >= 80 ? 3 : 1) : code[0] == 'I' ? 2 : code.startsWith ("S9") ? 4 : 0)
                                : (code[0] == 'F' ? 1 : code[0] == 'P' ? 2 : code[0] == 'S' ? 3 : 0);
        const int bus = code.length() >= 5 ? code.substring (3, 5).getIntValue() : 0;
        const auto make = [button] (int k, int target, int b) -> juce::String
        {
            const auto two = [] (int v) { return juce::String (v).paddedLeft ('0', 2); };
            if (button)
                return k == 1 ? "O" + two (target) : k == 2 ? "I" + two (target) : k == 3 ? "O" + two (80 + juce::jlimit (0, 5, target)) : k == 4 ? juce::String ("S93") : juce::String ("-");
            return k == 1 ? "F" + two (target) : k == 2 ? "P" + two (target) : k == 3 ? "S" + two (target) + two (b) : juce::String ("-");
        };
        const int target = kind == 3 && button ? xx - 80 : xx;
        const auto picker = [] (const juce::String& label, int value, int count, std::function<juce::String (int)> text, std::function<void (int)> set)
        {
            KnobBinding b;
            b.label = label;
            b.get = [value, count] { return count > 1 ? (float) value / (float) (count - 1) : 0.0f; };
            b.set = [count, set] (float v) { set (juce::roundToInt (v * (float) (count - 1))); };
            b.text = [value, text] { return text (value); };
            b.steps = (float) juce::jmax (2, count);
            return b;
        };
        encoders[0] = encoder (picker ("CONTROL", assignControl, 12, [] (int v) { return v < 4 ? "ENC " + juce::String (v + 1) : "KEY " + juce::String (v + 1); },
                                       [this] (int v) { assignControl = v; assignEncoders(); }));
        encoders[1] = encoder (picker ("TYPE", kind, kinds.size(), [kinds] (int v) { return kinds[v]; },
                                       [this, make, target, bus] (int v) { setAssignCode (assignControl, make (v, juce::jmax (0, target), bus)); assignEncoders(); }));
        if (kind != 0 && ! (button && kind == 4))
        {
            const int count = button && kind == 3 ? 6 : 72;
            encoders[2] = encoder (picker ("TARGET", juce::jlimit (0, count - 1, target), count,
                                           [button, kind] (int v)
                                           {
                                               return button && kind == 3 ? "MUTE G" + juce::String (v + 1) : x32::AssignSection::describe (juce::String ("F") + juce::String (v).paddedLeft ('0', 2), false).fromFirstOccurrenceOf ("FDR ", false, false);
                                           },
                                           [this, make, kind, bus] (int v) { setAssignCode (assignControl, make (kind, v, bus)); assignEncoders(); }));
        }
        if (! button && kind == 3)
            encoders[3] = encoder (picker ("BUS", bus, 16, [] (int v) { return juce::String (v + 1); },
                                           [this, make, kind, target] (int v) { setAssignCode (assignControl, make (kind, target, v)); assignEncoders(); }));
    }
    else if (screen == Screen::Utility)
    {
        // COPY the selected strip, PASTE its chosen sections onto the selected one.
        encoders[0] = encoder ({}, "COPY", [this]
        {
            copySource = selected();
            utilityStatus = copySource != nullptr ? "Copied " + copySource->getLabel() : juce::String ("Select a strip to copy");
            repaint();
        });
        encoders[1] = encoder ({}, "PASTE", [this]
        {
            auto* to = selected();
            if (copySource == nullptr || ! isStrip (copySource))
                utilityStatus = "Copy a strip first";
            else if (to == nullptr || to == copySource)
                utilityStatus = "Select the strip to paste onto";
            else
            {
                copyStripSections (ctx.engine, *copySource, *to, copySections);
                utilityStatus = "Pasted " + copySource->getLabel() + " onto " + to->getLabel();
                refreshStrip();
            }
            repaint();
        });
        const std::pair<juce::uint32, const char*> parts[] = { { kCopyPreamp | kCopyGate, "PRE / GATE" },
                                                               { kCopyDynamics, "DYN" },
                                                               { kCopyEq, "EQ" },
                                                               { kCopySends | kCopyMix, "SENDS / MIX" } };
        for (int i = 0; i < 4; ++i)
        {
            const auto bits = parts[i].first;
            encoders[(size_t) i + 2] = encoder ({}, parts[i].second, [this, bits]
            {
                copySections = (copySections & bits) == bits ? copySections & ~bits : copySections | bits;
                repaint();
            }, [this, bits] { return (copySections & bits) == bits; });
        }
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

juce::Array<juce::File> Display::presets (const char* extension)
{
    auto files = ShowPanel::libraryFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    return files;
}

juce::String Display::x32Address (const Strip& s) const
{
    auto& e = ctx.engine;
    const auto at = [&s] (const MixerEngine::StripList& list)
    {
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].get() == &s)
                return (int) i;
        return -1;
    };
    const auto two = [] (int n) { return juce::String (n).paddedLeft ('0', 2); };
    if (const int i = at (e.getChannels()); i >= 0)
        return "/ch/" + two (i + 1);
    if (const int i = at (e.getAux()); i >= 0)
        return i < 8 ? "/auxin/" + two (i + 1) : "/fxrtn/" + two (i - 7);
    if (at (e.getBuses()) >= 0)
        return "/bus/" + two (s.getId() + 1);
    if (const int i = at (e.getMatrices()); i >= 0)
        return "/mtx/" + two (i + 1);
    return {};
}

juce::String Display::linkAddress (const Strip& s) const
{
    const auto address = x32Address (s);
    const auto kind = address.fromFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("/", false, false);
    const int n = address.fromLastOccurrenceOf ("/", false, false).getIntValue();
    const juce::String node = kind == "ch" ? "chlink" : kind == "auxin" ? "auxlink" : kind == "fxrtn" ? "fxlink" : kind == "bus" ? "buslink" : kind == "mtx" ? "mtxlink" : "";
    if (n < 1 || node.isEmpty())
        return {};
    const int odd = (n - 1) / 2 * 2 + 1;
    return "/config/" + node + "/" + juce::String (odd) + "-" + juce::String (odd + 1);
}

juce::String Display::sourceName (int k)
{
    const auto two = [] (int n) { return juce::String (n).paddedLeft ('0', 2); };
    if (k >= 1 && k <= 32)
        return "In " + two (k);
    if (k >= 33 && k <= 38)
        return "Aux " + juce::String (k - 32);
    if (k == 39 || k == 40)
        return k == 39 ? "USB L" : "USB R";
    if (k >= 41 && k <= 48)
        return "Fx " + juce::String ((k - 41) / 2 + 1) + ((k - 41) % 2 == 0 ? "L" : "R");
    if (k >= 49 && k <= 64)
        return "Bus " + two (k - 48);
    return "OFF";
}

juce::String Display::x32Value (const juce::String& address) const
{
    int set = 0;
    const auto answer = ctx.engine.getWing().runX32Text (address, set);
    return answer.fromFirstOccurrenceOf (" ", false, false).trim();
}

KnobBinding Display::enumKnob (const juce::String& label, const juce::String& path, const juce::StringArray& choices)
{
    KnobBinding k;
    k.label = label;
    const int n = choices.size();
    k.get = [this, path, &choices, n] { return (float) juce::jmax (0, choices.indexOf (x32Value (path))) / (float) juce::jmax (1, n - 1); };
    k.set = [this, path, &choices, n] (float v)
    {
        int set = 0;
        ctx.engine.getWing().runX32Text (path + " " + choices[juce::roundToInt (v * (float) (n - 1))], set);
        repaint();
    };
    k.text = [this, path] { return x32Value (path); };
    k.steps = (float) n;
    return k;
}

std::vector<int> Display::fxModelsFor (int slot) const
{
    std::vector<int> list;
    const auto& table = models::table (models::Fx);
    for (const auto& m : table)
        if (m.token.startsWith ("X32") && (slot < 4 || m.category != models::Category::FxPremium))
            list.push_back (m.index);
    return list;
}

void Display::assignEffectsEncoders()
{
    auto& fx = ctx.engine.getFx();
    auto run = [this] (const juce::String& text)
    {
        int set = 0;
        ctx.engine.getWing().runX32Text (text, set);
        repaint();
    };
    if (tab == 0)
    {
        // Encoder 1 the slot, 2 its model (push: edit it), 3-4 FX 1-4's SOURCE L / R.
        KnobBinding pick;
        pick.label = "FX";
        pick.get = [this] { return (float) selectedFx / 7.0f; };
        pick.set = [this] (float v)
        {
            selectedFx = juce::jlimit (0, 7, juce::roundToInt (v * 7.0f));
            assignEncoders();
        };
        pick.text = [this] { return juce::String (selectedFx + 1); };
        pick.steps = 8.0f;
        encoders[0] = encoder (pick);

        const int slot = selectedFx;
        const auto list = fxModelsFor (slot);
        KnobBinding type;
        type.label = "TYPE";
        const int n = (int) list.size();
        type.get = [&fx, slot, list, n]
        {
            const auto at = std::find (list.begin(), list.end(), fx.getModel (slot));
            return at == list.end() || n < 2 ? 0.0f : (float) (at - list.begin()) / (float) (n - 1);
        };
        type.set = [slot, list, n, run] (float v)
        {
            if (n == 0)
                return;
            const auto& m = models::model (models::Fx, list[(size_t) juce::roundToInt (v * (float) (n - 1))]);
            run ("/fx/" + juce::String (slot + 1) + "/type " + m.token.fromFirstOccurrenceOf ("X32", false, false));
        };
        type.text = [&fx, slot]
        {
            const int m = fx.getModel (slot);
            return m > ld::kFxExt ? models::model (models::Fx, m).name : juce::String ("-");
        };
        type.steps = (float) juce::jmax (2, n);
        encoders[1] = encoder (type, "EDIT", [this, slot] { showScreen (Screen::Effects, slot + 1); }, [] { return false; });
        if (slot < 4)
        {
            static const juce::StringArray sources { "INS", "MIX1", "MIX2", "MIX3", "MIX4", "MIX5", "MIX6", "MIX7", "MIX8", "MIX9",
                                                     "MIX10", "MIX11", "MIX12", "MIX13", "MIX14", "MIX15", "MIX16", "M/C" };
            const auto base = "/fx/" + juce::String (slot + 1) + "/source/";
            encoders[2] = encoder (enumKnob ("SOURCE L", base + "l", sources));
            encoders[3] = encoder (enumKnob ("SOURCE R", base + "r", sources));
        }
        return;
    }
    // fx1-fx8: the slot's parameters, six per layer.
    const int slot = tab - 1;
    const int m = fx.getModel (slot);
    if (m <= ld::kFxExt)
        return;
    const auto& model = models::model (models::Fx, m);
    auto& values = fx.params (slot).values;
    layer = juce::jlimit (0, numLayers() - 1, layer); // a new model may have fewer
    for (int e = 0; e < 6; ++e)
    {
        const int k = layer * 6 + e;
        if (k < (int) model.params.size())
            encoders[(size_t) e] = encoder (bindModelParam (values[k], model.params[(size_t) k]));
    }
}

void Display::paintEffects (juce::Graphics& g, juce::Rectangle<int> r)
{
    auto& fx = ctx.engine.getFx();
    const auto modelName = [&fx] (int slot)
    {
        const int m = fx.getModel (slot);
        return m > ld::kFxExt ? models::model (models::Fx, m).name : juce::String ("-");
    };
    if (tab == 0)
    {
        juce::StringArray left, right;
        for (int i = 0; i < juce::jmin (8, fx.getNumSlots()); ++i)
        {
            left.add ((i == selectedFx ? "> FX " : "FX ") + juce::String (i + 1));
            auto text = modelName (i);
            if (i < 4)
            {
                const auto l = x32Value ("/fx/" + juce::String (i + 1) + "/source/l");
                const auto rr = x32Value ("/fx/" + juce::String (i + 1) + "/source/r");
                text << "   " << (l == "INS" ? juce::String ("INSERT") : l == rr ? "FROM " + l : "FROM " + l + " / " + rr);
            }
            right.add (text);
        }
        paintRows (g, r, left, right);
        return;
    }
    // A slot: its model, then every parameter in two columns, this layer's marked.
    const int slot = tab - 1;
    const int m = fx.getModel (slot);
    g.setColour (theme.text);
    g.setFont (type::font (type::kLabel, true));
    g.drawText ("FX " + juce::String (slot + 1) + "   " + modelName (slot), r.removeFromTop (22), juce::Justification::centredLeft);
    if (m <= ld::kFxExt)
        return;
    const auto& model = models::model (models::Fx, m);
    auto& values = fx.params (slot).values;
    const int n = (int) model.params.size();
    const int perColumn = (n + 1) / 2;
    for (int c = 0; c < 2; ++c)
    {
        auto area = r.removeFromLeft (c == 0 ? r.getWidth() / 2 : r.getWidth()).reduced (c == 0 ? 0 : 4, 0);
        juce::StringArray left, right;
        for (int k = c * perColumn; k < juce::jmin (n, (c + 1) * perColumn); ++k)
        {
            const auto& p = model.params[(size_t) k];
            left.add ((k / 6 == layer ? "> " : "") + p.label);
            right.add (p.display (values[k].load()));
        }
        paintRows (g, area, left, right);
    }
}

void Display::assignOutputEncoders()
{
    auto run = [this] (const juce::String& text)
    {
        int set = 0;
        ctx.engine.getWing().runX32Text (text, set);
        repaint();
    };
    if (tab >= 4)
    {
        // card out, AES50-A, AES50-B: what each block of eight outputs plays.
        const auto& t = kBlockTabs[tab - 4];
        for (int b = 0; b < t.blocks; ++b)
            encoders[(size_t) b] = encoder (enumKnob (juce::String (t.label) + " " + kNetBlocks[b],
                                                      "/config/routing/" + juce::String (t.node) + "/" + kNetBlocks[b], kRoutingNet));
        return;
    }
    if (tab == 1 && layer == 1)
    {
        // The local outputs 1-16, four at a time.
        for (int b = 0; b < 4; ++b)
            encoders[(size_t) b] = encoder (enumKnob ("LOCAL " + juce::String (kOutBlocks[b]), "/config/routing/OUT/" + juce::String (kOutBlocks[b]),
                                                      b % 2 == 0 ? kRoutingOutA : kRoutingOutB));
        return;
    }
    // out 1-16, aux out, P16 out: the output signals, one chosen at a time.
    const auto& t = kSignalTabs[tab - 1];
    selectedOutput = juce::jlimit (0, t.count - 1, selectedOutput);
    const auto path = [t] (int n) { return "/outputs/" + juce::String (t.group) + "/" + juce::String (n + 1).paddedLeft ('0', 2); };
    KnobBinding pick;
    pick.label = t.label;
    pick.get = [this, t] { return (float) selectedOutput / (float) (t.count - 1); };
    pick.set = [this, t] (float v)
    {
        selectedOutput = juce::roundToInt (v * (float) (t.count - 1));
        assignEncoders();
    };
    pick.text = [this] { return juce::String (selectedOutput + 1); };
    pick.steps = (float) t.count;
    encoders[0] = encoder (pick);

    const auto p = path (selectedOutput);
    KnobBinding src;
    src.label = "SOURCE";
    const int n = outputSources().size();
    src.get = [this, p, n] { return (float) x32Value (p + "/src").getIntValue() / (float) (n - 1); };
    src.set = [p, n, run] (float v) { run (p + "/src " + juce::String (juce::roundToInt (v * (float) (n - 1)))); };
    src.text = [this, p] { return outputSources()[x32Value (p + "/src").getIntValue()]; };
    src.steps = (float) n;
    encoders[1] = encoder (src);
    encoders[2] = encoder (enumKnob ("TAP", p + "/pos", kOutputTaps));
    const auto inverted = [this, p] { return x32Value (p + "/invert") == "ON"; };
    encoders[3] = encoder ({}, "INVERT", [inverted, run, p] { run (p + "/invert " + (inverted() ? "OFF" : "ON")); }, inverted);
    if (juce::String (t.group) == "main")
    {
        // The output's delay, 0.3 to 500 ms.
        KnobBinding delay;
        delay.label = "DELAY";
        const auto time = [this, p] { return x32Value (p + "/delay/time").getFloatValue(); };
        delay.get = [time] { return (time() - 0.3f) / 499.7f; };
        delay.set = [run, p] (float v) { run (p + "/delay/time " + juce::String (0.3f + juce::jlimit (0.0f, 1.0f, v) * 499.7f, 1)); };
        delay.text = [time] { return juce::String (time(), 1) + " ms"; };
        const auto on = [this, p] { return x32Value (p + "/delay/on") == "ON"; };
        encoders[4] = encoder (delay, "DELAY", [on, run, p] { run (p + "/delay/on " + (on() ? "OFF" : "ON")); }, on);
    }
}

void Display::paintOutputs (juce::Graphics& g, juce::Rectangle<int> r)
{
    if (tab >= 4)
    {
        const auto& t = kBlockTabs[tab - 4];
        juce::StringArray left, right;
        for (int b = 0; b < t.blocks; ++b)
        {
            left.add (juce::String (t.label) + " " + kNetBlocks[b] + "  (INTERFACE " + juce::String (t.firstPort + 8 * b + 1) + "-"
                      + juce::String (t.firstPort + 8 * b + 8) + ")");
            right.add (x32Value ("/config/routing/" + juce::String (t.node) + "/" + kNetBlocks[b]));
        }
        paintRows (g, r, left, right);
        return;
    }
    if (tab == 1 && layer == 1)
    {
        juce::StringArray left, right;
        for (int b = 0; b < 4; ++b)
        {
            left.add ("LOCAL OUTPUTS " + juce::String (kOutBlocks[b]) + "  (INTERFACE " + kOutBlocks[b] + ")");
            right.add (x32Value ("/config/routing/OUT/" + juce::String (kOutBlocks[b])));
        }
        paintRows (g, r, left, right);
        return;
    }
    // The output signals, in two columns of up to eight; the chosen one marked.
    const auto& t = kSignalTabs[tab - 1];
    const int perColumn = juce::jmin (8, t.count);
    auto columns = r;
    for (int c = 0; c * perColumn < t.count; ++c)
    {
        auto area = columns.removeFromLeft (r.getWidth() / ((t.count + perColumn - 1) / perColumn)).reduced (c == 0 ? 0 : 4, 0);
        juce::StringArray left, right;
        for (int i = c * perColumn; i < juce::jmin (t.count, (c + 1) * perColumn); ++i)
        {
            const auto p = "/outputs/" + juce::String (t.group) + "/" + juce::String (i + 1).paddedLeft ('0', 2);
            left.add ((i == selectedOutput ? "> " : "") + juce::String (t.label) + " " + juce::String (i + 1));
            right.add (outputSources()[x32Value (p + "/src").getIntValue()] + "  " + x32Value (p + "/pos"));
        }
        paintRows (g, area, left, right);
    }
}

void Display::assignRoutingEncoders()
{
    if (tab > 0)
    {
        assignOutputEncoders();
        return;
    }
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

bool Display::isStrip (const Strip* s) const
{
    for (int k = 0; k < kNumStripKinds; ++k)
        for (auto& strip : ctx.engine.getStrips (static_cast<StripKind> (k)))
            if (strip.get() == s)
                return true;
    return false;
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
    if (screen == Screen::Effects)
    {
        // The FX slots' models: their parameters on the encoders follow a change.
        auto& fx = ctx.engine.getFx();
        for (int i = 0; i < juce::jmin (8, fx.getNumSlots()); ++i)
            now << "|fx" << fx.getModel (i);
        repaint(); // values set elsewhere
    }
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
    // The status bar, the encoder row and the bezel meters only when what they show changed.
    {
        juce::String status = juce::Time::getCurrentTime().formatted ("%H:%M:%S");
        const int current = ctx.show.getCurrentSceneIndex();
        status << "|" << current << "|" << ctx.show.getNumScenes();
        for (int i : { current, current + 1 })
            if (const auto scene = ctx.show.getScene (i); scene.has_value())
                status << "|" << scene->name;
        if (auto* s = selected())
            status << "|" << s->getLabel() << "|" << s->getName() << "|" << s->getColourIndex();
        status << "|" << ctx.engine.getSampleRate();
        if (status != shownStatusBar)
        {
            shownStatusBar = status;
            repaint (statusArea);
        }
        juce::String row;
        for (auto& e : encoders)
        {
            if (e.turn.isValid())
                row << (e.turn.text ? e.turn.text() : juce::String()) << "@" << juce::roundToInt (e.turn.get() * 200.0f);
            row << (e.pushOn && e.pushOn() ? "+" : "-") << ";";
        }
        if (row != shownEncoderRow)
        {
            shownEncoderRow = row;
            repaint (encoderRow);
        }
        // The bezel meters: which LED each column lights (a dB each).
        const auto& mains = ctx.engine.getMains();
        juce::String leds;
        leds << (int) ctx.engine.anySoloActive.load();
        for (int ch = 0; ch < 2; ++ch)
            leds << "|" << juce::roundToInt (ctx.meters.getMonitor (ch) * 1000.0f);
        for (size_t m = 0; m < juce::jmin ((size_t) 2, mains.size()); ++m)
            for (int ch = 0; ch < 2; ++ch)
                leds << "|" << juce::roundToInt (juce::Decibels::gainToDecibels (ctx.meters.get (mains[m].get(), MeterPoint::Output, ch), -144.0f));
        if (leds != shownBezel)
        {
            shownBezel = leds;
            repaint (bezelMeters);
        }
    }
    if (screen == Screen::Meters || (screen == Screen::Home && (tab == 0 || tab == 5)))
        repaint (contentArea);
}

//==============================================================================
void Display::paint (juce::Graphics& g)
{
    // The bezel, the glass, and the main / solo meters in the bezel beside it.
    g.setColour (juce::Colour (0xff141516));
    g.fillRoundedRectangle (screenArea.getUnion (bezelMeters).toFloat().expanded (8.0f), 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (screenArea.getUnion (bezelMeters).toFloat().expanded (7.5f), 10.0f, 1.0f);
    g.setColour (kGlass);
    g.fillRect (screenArea);
    // PAGE SELECT and LAYER, printed beside their keys.
    if (g.clipRegionIntersects (pageLabel.getUnion (layerLabel).getUnion (pagePrev.getBounds().getUnion (pageNext.getBounds()).withHeight (pagePrev.getHeight() + 14))))
    {
        hw::silk (g, pageLabel.toFloat(), "PAGE SELECT", hw::Look::x32(), 8.5f);
        hw::silk (g, layerLabel.toFloat(), "LAYER", hw::Look::x32(), 8.5f, juce::Justification::centredRight);
        // NO and YES under PAGE SELECT's keys.
        hw::silk (g, pagePrev.getBounds().toFloat().withY ((float) pagePrev.getBottom() + 1.0f).withHeight (11.0f), "NO", hw::Look::x32(), 8.0f);
        hw::silk (g, pageNext.getBounds().toFloat().withY ((float) pageNext.getBottom() + 1.0f).withHeight (11.0f), "YES", hw::Look::x32(), 8.0f);
    }
    // Only the parts being repainted (the clock and meters repaint often).
    if (g.clipRegionIntersects (statusArea))
        paintStatus (g, statusArea);
    if (g.clipRegionIntersects (tabsArea))
        paintTabs (g, tabsArea);
    if (g.clipRegionIntersects (contentArea))
        paintContent (g, contentArea);
    if (g.clipRegionIntersects (encoderRow))
        paintEncoderRow (g, encoderRow);
    if (g.clipRegionIntersects (bezelMeters))
        paintBezelMeters (g, bezelMeters);
}

void Display::paintStatus (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto r = area.toFloat().reduced (4.0f, 3.0f);

    // The selected channel's tile: its number and name, its colour along the right.
    auto tile = r.removeFromLeft (118.0f);
    g.setColour (kDeep);
    g.fillRoundedRectangle (tile, 4.0f);
    g.setColour (kSkyLine);
    g.drawRoundedRectangle (tile.reduced (0.5f), 4.0f, 1.0f);
    if (auto* s = selected())
    {
        g.setColour (consoleColour (s->getColourIndex()));
        g.fillRect (tile.removeFromRight (6.0f).reduced (0.0f, 3.0f));
        auto text = tile.reduced (6.0f, 3.0f);
        g.setColour (kDeepText);
        g.setFont (type::font (10.0f, true));
        g.drawText (s->getLabel(), text.removeFromTop (text.getHeight() * 0.42f), juce::Justification::centredLeft);
        g.setColour (juce::Colours::white);
        g.setFont (type::font (12.5f, true));
        g.drawFittedText (s->getName().isNotEmpty() ? s->getName() : s->getLabel(), text.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }
    r.removeFromLeft (8.0f);

    // The scene: its number large in amber, its name, the next one under it.
    auto& show = ctx.show;
    const int current = show.getCurrentSceneIndex();
    auto scene = r.removeFromLeft (190.0f);
    g.setColour (kAmber);
    g.setFont (type::font (27.0f, true));
    g.drawText (current >= 0 ? juce::String (current + 1).paddedLeft ('0', 2) : juce::String ("--"), scene.removeFromLeft (40.0f),
                juce::Justification::centredLeft);
    const auto nameOf = [&show] (int i)
    {
        const auto s = show.getScene (i);
        return s.has_value() ? s->name : juce::String();
    };
    g.setFont (type::font (12.5f, true));
    g.drawFittedText (current >= 0 ? nameOf (current) : juce::String ("no scene"), scene.removeFromTop (scene.getHeight() * 0.55f).toNearestInt(),
                      juce::Justification::bottomLeft, 1, 0.7f);
    g.setColour (juce::Colour (0xff3f74c4));
    g.setFont (type::font (10.5f, true));
    if (current + 1 < show.getNumScenes())
        g.drawFittedText (juce::String (current + 2).paddedLeft ('0', 2) + ": " + nameOf (current + 1), scene.toNearestInt(),
                          juce::Justification::topLeft, 1, 0.7f);

    // The clock at the right, the seconds small; the sample rate in its box.
    auto clock = r.removeFromRight (104.0f);
    const auto now = juce::Time::getCurrentTime();
    g.setColour (kAmber);
    g.setFont (type::font (11.0f, true));
    g.drawText (now.formatted (":%S"), clock.removeFromRight (22.0f).withTrimmedTop (8.0f), juce::Justification::centredLeft);
    g.setFont (type::font (24.0f, true));
    g.drawText (now.formatted ("%H:%M"), clock, juce::Justification::centredRight);
    auto box = r.removeFromRight (70.0f).reduced (4.0f, 6.0f);
    g.setColour (kDeep);
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (kSkyLine);
    g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.0f);
    const double sr = ctx.engine.getSampleRate();
    g.setColour (kDeepText);
    g.setFont (type::font (11.0f, true));
    g.drawText (sr > 0 ? juce::String (juce::roundToInt (sr / 1000.0)) + "K" : juce::String ("48K"), box, juce::Justification::centred);
}

void Display::paintBezelMeters (juce::Graphics& g, juce::Rectangle<int> area)
{
    // The X32's main meters beside the screen: M/C (and the solo bus), the
    // scale, L and R; a red CLIP, orange to -8 dB, then green.
    static constexpr float marks[] = { 0.0f, -1.0f, -2.0f, -3.0f, -4.0f, -6.0f, -8.0f, -10.0f, -12.0f, -15.0f, -18.0f, -21.0f,
                                       -24.0f, -27.0f, -30.0f, -33.0f, -36.0f, -39.0f, -42.0f, -45.0f, -48.0f, -51.0f, -54.0f, -57.0f };
    constexpr int n = (int) std::size (marks);
    auto r = area.toFloat();
    auto names = r.removeFromBottom (22.0f);
    const float rowH = r.getHeight() / (float) n;
    const auto& mains = ctx.engine.getMains();
    const auto levelDb = [this, &mains] (int main, int ch)
    {
        if (main >= (int) mains.size())
            return -144.0f;
        return juce::Decibels::gainToDecibels (ctx.meters.get (mains[(size_t) main].get(), MeterPoint::Output, ch), -144.0f);
    };
    const float ledW = 8.0f;
    const float xMc = r.getX() + 2.0f, xL = r.getRight() - 2.0f * ledW - 5.0f, xR = r.getRight() - ledW - 1.0f;
    const bool solo = ctx.engine.anySoloActive.load();
    const float mc = solo ? juce::Decibels::gainToDecibels (juce::jmax (ctx.meters.getMonitor (0), ctx.meters.getMonitor (1)), -144.0f) : levelDb (1, 0);
    const float dbs[] = { mc, levelDb (0, 0), levelDb (0, 1) };
    const float xs[] = { xMc, xL, xR };
    g.setFont (type::font (8.0f, true));
    for (int k = 0; k < n; ++k)
    {
        const float y = r.getY() + rowH * (float) k;
        const auto colour = k == 0 ? juce::Colour (0xffff3b30) : k <= 6 ? juce::Colour (0xffff9a1f) : juce::Colour (0xff3ddc5a);
        for (int m = 0; m < 3; ++m)
        {
            const bool on = dbs[m] >= marks[k] && dbs[m] > -90.0f;
            g.setColour (on ? colour : colour.withAlpha (0.12f));
            g.fillRoundedRectangle (juce::Rectangle<float> (xs[m], y + 1.0f, ledW, juce::jmax (2.0f, rowH - 2.5f)), 1.0f);
        }
        g.setColour (juce::Colour (0xffe8e8e8));
        g.drawText (k == 0 ? juce::String ("CLIP") : juce::String ((int) marks[k]),
                    juce::Rectangle<float> (xMc + ledW + 1.0f, y - 1.0f, xL - xMc - ledW - 3.0f, rowH + 2.0f), juce::Justification::centredRight);
    }
    g.setColour (juce::Colour (0xffe8e8e8));
    g.setFont (type::font (8.5f, true));
    g.drawFittedText ("M/C\nSOLO", names.withWidth (30.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.7f);
    g.drawText ("L", juce::Rectangle<float> (xL, names.getY(), ledW, 12.0f), juce::Justification::centred);
    g.drawText ("R", juce::Rectangle<float> (xR, names.getY(), ledW, 12.0f), juce::Justification::centred);
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
    // The tabs as folders along the top of the page: the shown one light blue.
    const auto tabs = getTabs();
    g.setFont (type::font (12.0f, true));
    for (int i = 0; i < tabs.size(); ++i)
    {
        auto cell = getTabBounds (i).toFloat().reduced (1.0f, 0.0f).withTrimmedTop (2.0f);
        const bool on = i == tab;
        juce::Path folder;
        folder.addRoundedRectangle (cell.getX(), cell.getY(), cell.getWidth(), cell.getHeight(), 6.0f, 6.0f, true, true, false, false);
        g.setColour (on ? kSky : kDeep);
        g.fillPath (folder);
        g.setColour (kSkyLine);
        g.strokePath (folder, juce::PathStrokeType (1.0f));
        g.setColour (on ? kNavy : kDeepText);
        g.drawText (tabs[i], cell, juce::Justification::centred);
    }
    g.setColour (kSky);
    g.fillRect (r.removeFromBottom (2));
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
    // A list page: light blue rows, the names and values in navy, the chosen one marked yellow.
    const int h = juce::jmin (26, r.getHeight() / juce::jmax (1, left.size()));
    g.setColour (kSkyLine);
    g.fillRoundedRectangle (r.withHeight (h * left.size()).toFloat().expanded (1.5f), 4.0f);
    for (int i = 0; i < left.size(); ++i)
    {
        auto row = r.removeFromTop (h);
        g.setColour (i % 2 == 0 ? kSky : kSkyAlt);
        g.fillRect (row);
        const bool marked = left[i].startsWith (">");
        if (marked)
        {
            g.setColour (kYellow);
            g.fillRect (row.withWidth (5));
        }
        g.setColour (kNavy);
        g.setFont (type::font (juce::jmin (11.5f, (float) h * 0.82f), true));
        g.drawText (marked ? left[i].substring (1).trimStart() : left[i], row.reduced (10, 0).removeFromLeft (row.getWidth() / 2),
                    juce::Justification::centredLeft);
        g.setFont (type::font (juce::jmin (11.5f, (float) h * 0.82f)));
        g.drawText (right[i], row.reduced (10, 0), juce::Justification::centredRight);
    }
}

void Display::paintHomeFlow (juce::Graphics& g, juce::Rectangle<int> area, Strip& s)
{
    // The home tab as the X32 draws it: the channel's blocks in a row (In, Gate,
    // Ins, EQ, Dynamics, Out, Busses), its DCAs and mute groups along the bottom.
    auto& p = s.params;
    auto r = area.toFloat().reduced (4.0f, 2.0f);
    auto groups = r.removeFromBottom (20.0f);
    r.removeFromBottom (4.0f);
    struct Part
    {
        const char* name;
        float weight;
    };
    std::vector<Part> parts { { "In", 1.25f } };
    if (s.getKind() == StripKind::Channel)
        parts.push_back ({ "Gate", 1.55f });
    parts.push_back ({ "Ins", 0.55f });
    parts.push_back ({ "EQ", 1.9f });
    parts.push_back ({ "Dynamics", 1.55f });
    parts.push_back ({ "Out", 1.15f });
    if (s.isInput())
        parts.push_back ({ "Busses", 1.5f });
    float total = 0.0f;
    for (auto& part : parts)
        total += part.weight;
    const float gap = 3.0f, unit = (r.getWidth() - gap * (float) (parts.size() - 1)) / total;

    // A dynamics block's threshold and where it stands in its range.
    const auto threshold = [] (DynSlotParams& d) -> std::pair<float, float>
    {
        const auto& m = models::model (models::Dynamics, d.model.load());
        const int i = m.find ("thr");
        if (i < 0)
            return { 0.0f, 0.0f };
        const auto& spec = m.params[(size_t) i];
        const float v = d.values[i].load();
        return { v, spec.max > spec.min ? (v - spec.min) / (spec.max - spec.min) : 0.0f };
    };
    // Its graph: the transfer curve in orange, the gain reduction's red bar.
    const auto transfer = [&g] (juce::Rectangle<float> well, TransferView& view, bool bound, float grDb)
    {
        g.setColour (kWell);
        g.fillRect (well);
        auto gr = well.removeFromRight (7.0f);
        if (bound)
        {
            juce::Path curve;
            for (int k = 0; k <= 40; ++k)
            {
                const float in = -60.0f + 60.0f * (float) k / 40.0f;
                const float out = juce::jlimit (-60.0f, 0.0f, view.getCurveAt (in));
                const juce::Point<float> pt (well.getX() + well.getWidth() * (float) k / 40.0f, well.getBottom() - well.getHeight() * (out + 60.0f) / 60.0f);
                if (k == 0)
                    curve.startNewSubPath (pt);
                else
                    curve.lineTo (pt);
            }
            g.setColour (kCurve);
            g.strokePath (curve, juce::PathStrokeType (1.5f));
        }
        g.setColour (kGr);
        g.fillRect (gr.withHeight (gr.getHeight() * juce::jlimit (0.0f, 1.0f, std::abs (grDb) / 30.0f)));
    };
    // A knob's icon, its name under it.
    const auto knobWithName = [&g] (juce::Rectangle<float> in, const juce::String& name, float fraction, bool centred)
    {
        g.setColour (kNavy);
        g.setFont (type::font (9.0f, true));
        g.drawText (name, in.removeFromBottom (10.0f), juce::Justification::centred);
        knobIcon (g, in.getCentre(), juce::jmin (8.0f, in.getHeight() * 0.32f), fraction, centred);
    };

    for (const auto& part : parts)
    {
        const juce::String name (part.name);
        auto cell = r.removeFromLeft (unit * part.weight);
        r.removeFromLeft (gap);
        auto in = block (g, cell, name);
        if (name == "In")
        {
            auto top = in.removeFromTop (in.getHeight() * 0.46f);
            levelBar (g, top.removeFromLeft (9.0f).reduced (0.0f, 2.0f), ctx.meters.get (&s, MeterPoint::Trim, 0));
            top.removeFromLeft (4.0f);
            const int source = s.getInputLeft() >= 0 ? MixerEngine::sourceIndex (s.getInputLeft()) : -1;
            const auto* src = ctx.engine.getSource (source);
            const float rowH = top.getHeight() / 4.0f;
            checkBox (g, top.removeFromTop (rowH), "48V", src != nullptr && src->phantom.load());
            checkBox (g, top.removeFromTop (rowH), juce::String::fromUTF8 ("\xce\xa6"), p.input.invert.load());
            checkBox (g, top.removeFromTop (rowH), "Delay", p.input.delayOn.load());
            checkBox (g, top.removeFromTop (rowH), "Lo Cut", p.filter().lowCut.load());
            in.removeFromTop (3.0f);
            const auto link = linkAddress (s);
            chip (g, in.removeFromTop (12.0f).reduced (4.0f, 0.0f), "Link", link.isNotEmpty() && x32Value (link) == "ON");
            in.removeFromTop (3.0f);
            const float trim = p.input.trimDb.load();
            valueBox (g, in.removeFromTop (13.0f).reduced (2.0f, 0.0f), (trim > 0.0f ? "+" : "") + juce::String (trim, 1));
            knobWithName (in, "Gain", (trim + 18.0f) / 36.0f, true);
        }
        else if (name == "Gate" || name == "Dynamics")
        {
            const bool gate = name == "Gate";
            auto& d = gate ? p.gate() : p.dynamics();
            transfer (in.removeFromTop (in.getHeight() * 0.44f), gate ? gateView : dynView, d.model.load() > 0,
                      gate ? s.meters.gateReductionDb.load() : s.meters.dynReductionDb.load());
            in.removeFromTop (3.0f);
            chip (g, in.removeFromTop (12.0f).reduced (6.0f, 0.0f), gate ? "Gate" : "Dyn", d.on.load());
            in.removeFromTop (3.0f);
            const auto [thr, frac] = threshold (d);
            valueBox (g, in.removeFromTop (13.0f).reduced (6.0f, 0.0f), juce::String (thr, 1));
            knobWithName (in, "Thresh", frac, false);
        }
        else if (name == "Ins")
        {
            g.setColour (kNavy.withAlpha (0.7f));
            for (float y = in.getY(); y < in.getBottom(); y += 5.0f)
                g.drawLine (in.getCentreX(), y, in.getCentreX(), juce::jmin (in.getBottom(), y + 2.5f), 1.0f);
            chip (g, juce::Rectangle<float> (in.getWidth(), 13.0f).withCentre (in.getCentre()), "Ins",
                  p.preInsert().on.load() && p.preInsert().fx.load() >= 0);
        }
        else if (name == "EQ")
        {
            auto well = in.removeFromTop (in.getHeight() * 0.64f);
            g.setColour (kWell);
            g.fillRect (well);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawHorizontalLine ((int) well.getCentreY(), well.getX(), well.getRight());
            constexpr int pts = 48;
            std::array<double, pts> freqs {}, db {};
            for (int k = 0; k < pts; ++k)
                freqs[(size_t) k] = 20.0 * std::pow (1000.0, (double) k / (pts - 1));
            if (eqView.response && eqView.response (freqs.data(), db.data(), pts))
            {
                juce::Path curve;
                for (int k = 0; k < pts; ++k)
                {
                    const float y = well.getCentreY() - (float) juce::jlimit (-18.0, 18.0, db[(size_t) k]) / 18.0f * well.getHeight() * 0.45f;
                    const juce::Point<float> pt (well.getX() + well.getWidth() * (float) k / (pts - 1), y);
                    if (k == 0)
                        curve.startNewSubPath (pt);
                    else
                        curve.lineTo (pt);
                }
                g.setColour (p.eq().on.load() ? kCurve : kCurve.withAlpha (0.4f));
                g.strokePath (curve, juce::PathStrokeType (2.0f));
            }
            in.removeFromTop (6.0f);
            chip (g, in.removeFromTop (13.0f).withSizeKeepingCentre (juce::jmin (46.0f, in.getWidth()), 13.0f), "EQ", p.eq().on.load());
        }
        else if (name == "Out")
        {
            auto top = in.removeFromTop (in.getHeight() * 0.4f);
            levelBar (g, top.removeFromRight (9.0f).reduced (0.0f, 2.0f), ctx.meters.get (&s, MeterPoint::Output, 0));
            top.removeFromRight (3.0f);
            const bool mc = ctx.engine.getMains().size() > 1 && s.mains[1].on.load();
            checkBox (g, top.removeFromTop (top.getHeight() / 2.0f).reduced (0.0f, 2.0f), "Mono", mc);
            checkBox (g, top.reduced (0.0f, 2.0f), "Mute", p.mute.load());
            in.removeFromTop (3.0f);
            chip (g, in.removeFromTop (12.0f).reduced (4.0f, 0.0f), "LR", s.mains[0].on.load());
            in.removeFromTop (3.0f);
            const int pan = juce::roundToInt (p.pan.load());
            valueBox (g, in.removeFromTop (13.0f).reduced (2.0f, 0.0f), pan == 0 ? juce::String ("C") : (pan > 0 ? "+" : "") + juce::String (pan));
            knobWithName (in, "Pan", (p.pan.load() + 100.0f) / 200.0f, true);
        }
        else if (name == "Busses")
        {
            const int buses = juce::jmin (16, (int) ctx.engine.getBuses().size());
            const float rowH = in.getHeight() / 16.0f;
            g.setFont (type::font (juce::jmin (8.5f, rowH), true));
            for (int b = 0; b < buses; ++b)
            {
                auto row = in.removeFromTop (rowH).reduced (0.0f, 0.5f);
                auto num = row.removeFromLeft (13.0f);
                auto* send = s.sends.find (b);
                const bool on = send != nullptr && send->enabled.load();
                g.setColour (on ? kBox : kSkyAlt);
                g.fillRect (num);
                g.setColour (kNavy);
                g.drawText (juce::String (b + 1), num, juce::Justification::centred);
                row.removeFromLeft (2.0f);
                g.setColour (kNavy.withAlpha (0.85f));
                g.fillRect (row);
                const float pos = send != nullptr ? fader::dbToPos (send->levelDb.load()) : 0.0f;
                g.setColour (on ? kYellow : kDeepText.withAlpha (0.5f));
                g.fillRect (row.reduced (1.0f).withWidth ((row.getWidth() - 2.0f) * pos));
            }
        }
    }

    // DCA 1-8 and mute groups 1-6: the strip's memberships lit.
    g.setColour (kSky);
    g.fillRoundedRectangle (groups, 3.0f);
    g.setColour (kSkyLine);
    g.drawRoundedRectangle (groups.reduced (0.5f), 3.0f, 1.0f);
    auto line = groups.reduced (6.0f, 3.0f);
    const auto dcas = p.dcaMask.load(), mutes = p.muteGroupMask.load();
    const auto row = [&g, &line] (const juce::String& title, float titleWidth, int count, uint64_t bits)
    {
        g.setColour (kNavy);
        g.setFont (type::font (10.0f, true));
        g.drawText (title, line.removeFromLeft (titleWidth), juce::Justification::centredLeft);
        for (int k = 0; k < count; ++k)
        {
            auto box = line.removeFromLeft (16.0f).reduced (1.5f, 0.0f);
            const bool on = ((bits >> k) & 1u) != 0;
            g.setColour (on ? kYellow : kNavy);
            g.fillRect (box);
            g.setColour (on ? kNavy : kDeepText);
            g.drawText (juce::String (k + 1), box, juce::Justification::centred);
        }
        line.removeFromLeft (14.0f);
    };
    row ("DCA", 30.0f, 8, dcas.words[0]);
    row ("Mute Groups", 76.0f, 6, mutes.words[0]);
}

void Display::paintSends (juce::Graphics& g, juce::Rectangle<int> area, Strip& s)
{
    const int buses = juce::jmin (16, (int) ctx.engine.getBuses().size());
    if (! s.isInput() || buses == 0)
    {
        g.setColour (kDim);
        g.setFont (type::font (type::kLabel));
        g.drawText ("No bus sends on this strip", area, juce::Justification::centred);
        return;
    }
    auto r = block (g, area.toFloat().reduced (8.0f, 4.0f), "Bus Sends").reduced (4.0f, 2.0f);
    const float w = r.getWidth() / (float) buses;
    for (int i = 0; i < buses; ++i)
    {
        auto col = r.removeFromLeft (w).reduced (3.0f, 0.0f);
        auto* send = s.sends.find (i);
        const bool on = send != nullptr && send->enabled.load();
        const float pos = fader::dbToPos (send != nullptr ? send->levelDb.load() : -144.0f);
        const bool focused = i / 4 == sendBank;
        auto label = col.removeFromBottom (16.0f);
        g.setColour (focused ? kYellow : kSkyAlt);
        g.fillRect (label.reduced (0.0f, 1.0f));
        g.setColour (kNavy);
        g.setFont (type::font (10.0f, true));
        g.drawText (juce::String (i + 1), label, juce::Justification::centred);
        g.setColour (kWell);
        g.fillRect (col);
        g.setColour (on ? kYellow : kDeepText.withAlpha (0.5f));
        g.fillRect (col.reduced (1.0f).withTrimmedTop ((col.getHeight() - 2.0f) * (1.0f - pos)));
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
        g.setColour (kSky);
        g.setFont (type::font (10.0f, true));
        g.drawFittedText (juce::String (s->getNumber()), label, juce::Justification::centred, 1, 0.6f);
        g.setColour (kWell);
        g.fillRect (col);
        const float level = juce::jlimit (0.0f, 1.0f, fader::dbToPos (juce::Decibels::gainToDecibels (ctx.meters.get (s, MeterPoint::Output, 0), -144.0f)));
        const int h = juce::roundToInt ((float) col.getHeight() * level);
        auto lit = col.removeFromBottom (h);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff3b30), 0.0f, (float) col.getY(), juce::Colour (0xff3ddc5a), 0.0f,
                                                 (float) col.getBottom(), false));
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
                const auto address = x32Address (*s), link = linkAddress (*s);
                juce::String source = p.inputLeft.load() >= 0 ? "IN " + juce::String (p.inputLeft.load() + 1) : juce::String ("OFF");
                if (address.startsWith ("/ch/") || address.startsWith ("/auxin/"))
                    source = sourceName (x32Value (address + "/config/source").getIntValue())
                             + (pendingSource >= 0 && pendingSourceStrip == s ? "  >  " + sourceName (pendingSource) + " (SELECT)" : juce::String());
                paintRows (g, r.reduced (12), { "SOURCE", "LINK", "GAIN", "LOW CUT", "DELAY", "INSERT" },
                           { source, link.isEmpty() ? juce::String ("-") : x32Value (link) + "  (" + link.fromLastOccurrenceOf ("/", false, false) + ")",
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
            // gate, dyn and eq: their graphs (child components) in a block.
            if (tab >= 2 && tab <= 4)
                block (g, r.reduced (12, 6).toFloat(), tab == 2 ? "Gate" : tab == 3 ? "Dynamics" : "Equalizer");
            break;
        case Screen::Meters:
            if (tab == 4)
            {
                // RTA: the selected strip's bands, their peaks over them.
                Analysis::Bands bands;
                ctx.engine.getAnalysis().read (bands);
                auto area = r.reduced (16, 12).toFloat();
                g.setColour (kWell);
                g.fillRect (area);
                const float w = area.getWidth() / (float) Analysis::numBands;
                const auto yOf = [&area] (float db) { return area.getBottom() - area.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 90.0f) / 90.0f); };
                for (int k = 0; k < Analysis::numBands; ++k)
                {
                    const float x = area.getX() + w * (float) k;
                    g.setColour (juce::Colour (0xffff9a1f));
                    g.fillRect (juce::Rectangle<float> (x + 0.5f, yOf (bands.levels[(size_t) k]), juce::jmax (1.0f, w - 1.0f), area.getBottom() - yOf (bands.levels[(size_t) k])));
                    g.setColour (kSky);
                    g.fillRect (juce::Rectangle<float> (x + 0.5f, yOf (bands.peaks[(size_t) k]) - 1.0f, juce::jmax (1.0f, w - 1.0f), 2.0f));
                }
                g.setColour (kSky);
                g.setFont (type::font (10.0f, true));
                g.drawText (selected() != nullptr ? "RTA  " + selected()->getLabel() : juce::String ("RTA: select a strip"), area.reduced (6.0f, 4.0f),
                            juce::Justification::topLeft);
                break;
            }
            paintMeters (g, r);
            break;
        case Screen::Library:
        {
            if (tab >= 2)
            {
                // The presets around the one picked.
                const auto list = presets (tab == 2 ? Show::channelPresetExtension : Show::fxPresetExtension);
                juce::StringArray left, right;
                const int rows = juce::jmax (1, (r.getHeight() - 50) / 26);
                const int from = juce::jlimit (0, juce::jmax (0, list.size() - rows), presetChoice - rows / 2);
                for (int i = from; i < juce::jmin (list.size(), from + rows); ++i)
                {
                    left.add ((i == presetChoice ? "> " : "  ") + juce::String (i + 1).paddedLeft ('0', 2));
                    right.add (list[i].getFileNameWithoutExtension());
                }
                if (list.isEmpty())
                {
                    left.add ("No presets");
                    right.add (tab == 2 ? "SAVE stores the selected strip" : "SAVE stores the FX slot");
                }
                auto area = r.reduced (12);
                if (libraryStatus.isNotEmpty())
                {
                    g.setColour (kSky);
                    g.setFont (type::font (11.0f, true));
                    g.drawText (libraryStatus, area.removeFromBottom (20), juce::Justification::centredLeft);
                }
                paintRows (g, area, left, right);
                break;
            }
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
            paintEffects (g, r.reduced (12));
            break;
        case Screen::Routing:
            if (tab > 0)
                paintOutputs (g, r.reduced (12));
            else
                paintRouting (g, r.reduced (12));
            break;
        case Screen::Setup:
            if (tab == 1)
            {
                paintRemote (g, r.reduced (12));
                break;
            }
            if (tab == 2)
            {
                paintRows (g, r.reduced (12), { "STRIP", "NAME", "COLOR", "ICON" },
                           { s != nullptr ? s->getLabel() : juce::String ("-"), s != nullptr ? s->getName() : juce::String ("-"),
                             s != nullptr ? juce::String (s->getColourIndex()) : juce::String ("-"),
                             s != nullptr && s->params.icon.load() > 0 ? wingIconName (s->params.icon.load()) : juce::String ("NONE") });
                break;
            }
            if (tab == 3)
            {
                juce::StringArray left, right;
                for (int k = 0; k < 4; ++k)
                    if (auto* src = ctx.engine.getSource (4 * preampBlock + k))
                    {
                        left.add ("INPUT " + juce::String (4 * preampBlock + k + 1));
                        right.add (juce::String (src->gainDb.load(), 1) + " dB" + (src->phantom.load() ? "   48V" : ""));
                    }
                paintRows (g, r.reduced (12), left, right);
                break;
            }
            paintRows (g, r.reduced (12), { "CONSOLE", "SURFACE", "SAMPLE RATE", "CHANNELS", "BUSES / MATRICES" },
                       { "X32 (Sidecar)", getModel && getModel() == 1 ? "X32 COMPACT" : "X32", juce::String (juce::roundToInt (ctx.engine.getSampleRate())) + " Hz",
                         juce::String ((int) ctx.engine.getChannels().size()),
                         juce::String ((int) ctx.engine.getBuses().size()) + " / " + juce::String ((int) ctx.engine.getMatrices().size()) });
            break;
        case Screen::MuteGroups:
            break; // its keys
        case Screen::Monitor:
        {
            auto& e = ctx.engine;
            const auto onOff = [] (bool on) { return on ? juce::String ("ON") : juce::String ("OFF"); };
            if (tab == 0)
                paintRows (g, r.reduced (12), { "MONITOR LEVEL", "PHONES LEVEL", "DIM", "DIM ATTENUATION", "MONO", "CHANNEL SOLO", "SOURCE" },
                           { fader::text (e.monitorLevelDb.load()) + " dB", fader::text (e.phones.levelDb.load()) + " dB", onOff (e.monitorDim.load()),
                             juce::String (e.monitorDimDb.load(), 0) + " dB", onOff (e.monitorMono.load()), e.soloAfl.load() ? juce::String ("AFL") : juce::String ("PFL"),
                             e.anySoloActive.load() ? juce::String ("SOLO BUS") : MixerEngine::monitorSourceName (e.speakers.source.load()) });
            else if (tab < 3)
            {
                auto& path = e.talk.paths[(size_t) (tab - 1)];
                juce::StringArray to;
                if ((path.mains.load() & 1u) != 0)
                    to.add ("MAIN");
                for (int b = 0; b < 16; ++b)
                    if ((path.buses.load() & (1u << b)) != 0)
                        to.add ("BUS " + juce::String (b + 1));
                paintRows (g, r.reduced (12), { "TALK LEVEL", tab == 1 ? "TALK A" : "TALK B", "AUTO DIM", "DESTINATIONS" },
                           { fader::text (e.talk.levelDb.load()) + " dB", onOff (path.on.load()), juce::String (path.monDimDb.load(), 0) + " dB",
                             to.isEmpty() ? juce::String ("NONE") : to.joinIntoString (", ") });
            }
            else if (auto* osc = e.getOscillator (0))
            {
                const char* kinds[] = { "SINE", "PINK", "WHITE" };
                auto* src = e.getSource (MixerEngine::oscSource (0));
                paintRows (g, r.reduced (12), { "OSCILLATOR", "LEVEL", "FREQUENCY", "TYPE" },
                           { onOff (src != nullptr && ! src->mute.load()), juce::String (osc->levelDb.load(), 1) + " dB", formatHz (osc->freqHz.load()) + " Hz",
                             kinds[juce::jlimit (0, 2, (int) osc->mode.load())] });
            }
            break;
        }
        case Screen::Usb:
        {
            auto& usb = ctx.engine.getUsbRecorder();
            using P = UsbRecorder::PlayState;
            using R = UsbRecorder::RecState;
            const auto time = [] (int64_t samples, double rate)
            {
                const int s = rate > 0.0 ? (int) ((double) samples / rate) : 0;
                return juce::String (s / 60).paddedLeft ('0', 2) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
            };
            const double rate = juce::jmax (1.0, usb.getSampleRate());
            const auto play = usb.getPlayState();
            const auto rec = usb.getRecState();
            if (tab == 0)
                paintRows (g, r.reduced (12), { "PLAYER", "FILE", "POSITION", "RECORDER", "RECORDING" },
                           { play == P::Playing ? "PLAYING" : play == P::Paused ? "PAUSED" : play == P::Error ? "ERROR" : "STOPPED",
                             usb.getCurrentFile().getFileName().isEmpty() ? juce::String ("-") : usb.getCurrentFile().getFileName(),
                             time (usb.getPosition(), rate) + " / " + time (usb.getLength(), rate),
                             rec == R::Recording ? "RECORDING" : rec == R::Paused ? "PAUSED" : rec == R::Error ? "ERROR" : "STOPPED",
                             usb.getRecFile().getFileName().isEmpty() ? juce::String ("-") : usb.getRecFile().getFileName() });
            else
                paintRows (g, r.reduced (12), { "TRACKS", "BITS", "FOLDER" },
                           { juce::String (usb.getRecChannels()), juce::String (usb.getRecBits()), usb.getStorageFolder().getFullPathName() });
            break;
        }
        case Screen::Scenes:
        {
            auto& show = ctx.show;
            juce::StringArray left, right;
            if (tab == 0)
            {
                // The scenes around the one the first encoder points at; the current one marked.
                const int n = show.getNumScenes(), current = show.getCurrentSceneIndex();
                const int rows = juce::jmax (1, (r.getHeight() - 24) / 26);
                const int from = juce::jlimit (0, juce::jmax (0, n - rows), selectedScene - rows / 2);
                for (int i = from; i < juce::jmin (n, from + rows); ++i)
                {
                    const auto scene = show.getScene (i);
                    left.add ((i == selectedScene ? "> " : "  ") + juce::String (i + 1).paddedLeft ('0', 2) + (i == current ? "  *" : ""));
                    right.add ((scene.has_value() ? scene->name : juce::String()) + (scene.has_value() && scene->skip ? "  (SKIP)" : ""));
                }
                if (n == 0)
                {
                    left.add ("No scenes");
                    right.add ("NEW adds one");
                }
            }
            else
            {
                const auto safe = show.getGlobalSafe().sections;
                for (int k = 0; k < state::kNumSections; ++k)
                {
                    const auto section = state::sectionAt (k);
                    left.add ((k == safeSection ? "> " : "  ") + juce::String (state::sectionName (section)).toUpperCase());
                    right.add ((safe & (juce::uint32) section) != 0 ? "SAFE" : "-");
                }
            }
            paintRows (g, r.reduced (12), left, right);
            break;
        }
        case Screen::Assign:
        {
            const auto lines = assignLines ? assignLines() : juce::StringArray();
            juce::StringArray left, right;
            for (auto& l : lines)
            {
                left.add (l.upToFirstOccurrenceOf ("  ", false, false));
                right.add (l.fromFirstOccurrenceOf ("  ", false, false));
            }
            paintRows (g, r.reduced (12), left, right);
            break;
        }
        case Screen::Utility:
        {
            const bool copied = copySource != nullptr && isStrip (copySource);
            auto* sel = selected();
            juce::StringArray right { copied ? copySource->getLabel() + "  " + copySource->getName() : juce::String ("-"),
                                      sel != nullptr ? sel->getLabel() + "  " + sel->getName() : juce::String ("-") };
            juce::StringArray what;
            const std::pair<juce::uint32, const char*> names[] = { { kCopyPreamp, "PREAMP" }, { kCopyGate, "GATE" }, { kCopyDynamics, "DYN" },
                                                                   { kCopyEq, "EQ" }, { kCopySends, "SENDS" }, { kCopyMix, "FADER / MUTE / PAN" } };
            for (auto [bits, name] : names)
                if ((copySections & bits) != 0)
                    what.add (name);
            right.add (what.isEmpty() ? juce::String ("NOTHING") : what.joinIntoString (", "));
            right.add (utilityStatus);
            paintRows (g, r.reduced (12), { "COPIED FROM", "PASTE ONTO (SELECTED)", "SECTIONS", "" }, right);
            break;
        }
    }
}

void Display::paintEncoderRow (juce::Graphics& g, juce::Rectangle<int>)
{
    // The six encoders' cells: what each turns (its icon, name and value), and
    // along the bottom what a push does, yellow while on.
    for (int i = 0; i < 6; ++i)
    {
        auto cell = getEncoderCell (i).toFloat().reduced (2.0f, 3.0f);
        const auto& e = encoders[(size_t) i];
        if (! e.turn.isValid() && ! e.push)
        {
            g.setColour (kDeep.withAlpha (0.45f));
            g.fillRoundedRectangle (cell, 4.0f);
            continue;
        }
        g.setColour (kSky);
        g.fillRoundedRectangle (cell, 4.0f);
        g.setColour (kSkyLine);
        g.drawRoundedRectangle (cell.reduced (0.75f), 4.0f, 1.5f);
        auto in = cell.reduced (3.0f, 3.0f);
        auto band = in.removeFromBottom (15.0f);
        chip (g, band, e.push ? e.pushLabel : juce::String(), e.push && e.pushOn && e.pushOn());
        in.removeFromBottom (2.0f);
        if (! e.turn.isValid())
            continue;
        auto icon = in.removeFromLeft (juce::jmin (in.getHeight(), 30.0f));
        knobIcon (g, icon.getCentre(), juce::jmin (10.0f, icon.getWidth() * 0.32f), e.turn.get());
        g.setColour (kNavy);
        g.setFont (type::font (10.0f, true));
        g.drawFittedText (e.turn.label, in.removeFromTop (in.getHeight() * 0.45f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        valueBox (g, in.reduced (2.0f, 1.0f), e.turn.text ? e.turn.text() : juce::String());
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
    area.reduce (8, 8);
    bezelMeters = area.removeFromRight (kBezelMeters).reduced (4, 2);
    area.removeFromRight (8);
    screenArea = area;

    // Eight screen keys, PAGE SELECT's left and right under them; LAYER's up
    // over down at the bottom, beside the sixth encoder, its name to their left.
    auto layers = side.removeFromBottom (kKnobs).reduced (0, 8);
    {
        auto keys = layers.removeFromRight (44);
        const int lh = (keys.getHeight() - 6) / 2;
        layerUp.setBounds (keys.removeFromTop (lh));
        keys.removeFromTop (6);
        layerDown.setBounds (keys.removeFromTop (lh));
        layerLabel = layers.withTrimmedRight (4);
    }
    const int keyH = juce::jmin (36, (side.getHeight() - 8 * sizes::kGapSmall - sizes::kGap - 14) / 9);
    for (auto& k : screenKeys)
    {
        k->setBounds (side.removeFromTop (keyH));
        side.removeFromTop (sizes::kGapSmall);
    }
    side.removeFromTop (sizes::kGap);
    pageLabel = side.removeFromTop (14);
    auto pages = side.removeFromTop (keyH - 4);
    pagePrev.setBounds (pages.removeFromLeft (pages.getWidth() / 2).reduced (3, 0));
    pageNext.setBounds (pages.reduced (3, 0));

    auto screenInner = screenArea;
    statusArea = screenInner.removeFromTop (kStatus);
    tabsArea = screenInner.removeFromTop (kTabs).reduced (4, 0);
    encoderRow = screenInner.removeFromBottom (kEncoderRow);
    contentArea = screenInner.reduced (0, 4);
    for (auto* v : std::initializer_list<juce::Component*> { &eqView, &gateView, &dynView })
        v->setBounds (contentArea.reduced (12, 6).withTrimmedTop (14).reduced (4, 3));
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
