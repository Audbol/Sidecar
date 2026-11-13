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
#include "ui/MidiRemoteUi.h"

namespace livedaw::ui::x32
{

using namespace universal;

namespace
{
    // The screen's look, as the screen photographs (HOME, METERS, LIBRARY, EFFECTS):
    // black glass around a pale page, pale blue blocks with navy text and darker blue
    // edges, orange for what is on, the scene in amber, the clock on an orange tile.
    const juce::Colour kGlass (0xff000000), kPage (0xffe6f1f9), kSky (0xffb4dcf6), kSkyAlt (0xffa0d0ef), kSkyLine (0xff2a6db5);
    const juce::Colour kNavy (0xff0f2747), kDeep (0xff1c4687), kDeepText (0xffa9d2f5), kYellow (0xfff6b34a);
    const juce::Colour kAmber (0xfff29a2e), kWell (0xff111315), kCurve (0xffffa21f), kGr (0xffe5321e), kBox (0xfff4f4f2);
    const juce::Colour kDim (0xff40678f), kTabOff (0xff7fb2dc), kClockTile (0xffef9a3e);

    /** An encoder's name as the screen prints it: "GAIN" is "Gain"; initials stay. */
    juce::String titled (const juce::String& s)
    {
        static const juce::StringArray keep { "DCA", "EQ", "FX", "LR", "M/C", "RTA", "USB", "MIDI", "OSC", "IP", "AES50", "HA", "BPM", "LFO", "XLR", "P16" };
        auto words = juce::StringArray::fromTokens (s, " ", {});
        for (auto& w : words)
        {
            if (keep.contains (w) || w.containsAnyOf ("0123456789") || w != w.toUpperCase())
                continue;
            juce::String out;
            for (int i = 0; i < w.length(); ++i)
                out += juce::String::charToString (i == 0 || w[i - 1] == '/' ? w[i] : juce::CharacterFunctions::toLowerCase (w[i]));
            w = out;
        }
        return words.joinIntoString (" ");
    }

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

    constexpr int kStatus = 42, kTabs = 22, kEncoderRow = 64, kSideKeys = 104, kKnobs = 92, kBezelMeters = 64;

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

    /** A page's frame as the photographs show it: a blue rule around the page, the inside back. */
    juce::Rectangle<int> pageFrame (juce::Graphics& g, juce::Rectangle<int> r)
    {
        auto f = r.reduced (6, 4);
        g.setColour (kSkyLine);
        g.drawRoundedRectangle (f.toFloat().reduced (0.5f), 4.0f, 1.5f);
        return f.reduced (8, 6);
    }

    /** A real menu item with nothing behind it in Sidecar: shown, dimmed, inert. */
    Display::Encoder dimmedEncoder (const juce::String& label, const juce::String& value, const juce::String& pushLabel)
    {
        Display::Encoder e;
        e.turn.label = label;
        e.turn.text = [value] { return value; };
        e.pushLabel = pushLabel;
        e.dimmed = true;
        return e;
    }

    /** A title over a list of rows, the one at `pointed` outlined and the one at `on` orange. */
    void listColumn (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, const juce::StringArray& items, int pointed, int on, bool dimmed = false)
    {
        g.setColour (kNavy);
        g.setFont (type::font (10.5f, true));
        g.drawFittedText (title, r.removeFromTop (16.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        const float rowH = juce::jmin (15.0f, r.getHeight() / (float) juce::jmax (1, items.size()));
        for (int k = 0; k < items.size(); ++k)
        {
            auto row = r.removeFromTop (rowH);
            g.setColour (k == on ? kCurve : kBox);
            g.fillRect (row);
            g.setColour (dimmed ? kDim.withAlpha (0.6f) : kNavy.withAlpha (0.5f));
            g.drawRect (row, 1.0f);
            if (k == pointed)
            {
                g.setColour (kGr);
                g.drawRect (row, 1.5f);
            }
            g.setColour (dimmed ? kDim.withAlpha (0.8f) : kNavy);
            g.setFont (type::font (juce::jmin (10.5f, rowH * 0.92f), k == on));
            g.drawFittedText (items[k], row.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
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
    const juce::StringArray kInserts { "OFF",  "FX1L", "FX1R", "FX2L", "FX2R", "FX3L", "FX3R", "FX4L",
                                       "FX4R", "FX5L", "FX5R", "FX6L", "FX6R", "FX7L", "FX7R", "FX8L",
                                       "FX8R", "AUX1", "AUX2", "AUX3", "AUX4", "AUX5", "AUX6" };
    struct BusScheme
    {
        const char* name;
        int pre, post;
    };
    const BusScheme kBusSchemes[] = { { "8 Pre / 8 Post", 8, 8 }, { "8 Pre / 4 Post / 4 Sub", 8, 4 }, { "6 Pre / 4 Post / 6 Sub", 6, 4 },
                                      { "4 Pre / 4 Post / 8 Sub", 4, 4 }, { "10 Pre / 6 Post", 10, 6 } };
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

    /** The output sources' categories (p.52): their names and ranges in `outputSources()`. */
    struct SourceCategory
    {
        const char* name;
        int first, last;
    };
    const SourceCategory kSourceCategories[] = { { "OFF", 0, 0 }, { "MAIN", 1, 3 }, { "MIX BUS", 4, 19 }, { "MATRIX", 20, 25 },
                                                 { "DIRECT OUT", 26, 73 }, { "MONITOR", 74, 76 } };

    int categoryOf (int source)
    {
        for (int c = 0; c < 6; ++c)
            if (source >= kSourceCategories[c].first && source <= kSourceCategories[c].last)
                return c;
        return 0;
    }

    /** ROUTING's signal tabs: the `/outputs` group and its count (tabs 1-3). */
    struct SignalTab
    {
        const char* group;
        int count;
        const char* label;
    };
    const SignalTab kSignalTabs[] = { { "main", 16, "OUT" }, { "aux", 8, "AUX OUT" }, { "p16", 16, "P16" } };

    /** An output signal's X32 address; the aux tab's last two are the AES/EBU pair. */
    juce::String signalPath (const SignalTab& t, int n)
    {
        const bool aes = juce::String (t.group) == "aux" && n >= 6;
        return "/outputs/" + juce::String (aes ? "aes" : t.group) + "/" + juce::String (aes ? n - 5 : n + 1).paddedLeft ('0', 2);
    }
    juce::String signalName (const SignalTab& t, int n)
    {
        return juce::String (t.group) == "aux" && n >= 6 ? (n == 6 ? juce::String ("AES L") : juce::String ("AES R")) : juce::String (t.label) + " " + juce::String (n + 1);
    }
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

    /** `/-stat/screen/screen` (OSC document p.57) for one of the display's screens, and its page's node. */
    struct StatScreen
    {
        int number;
        const char* node;
    };
    StatScreen statScreen (Display::Screen s)
    {
        using S = Display::Screen;
        switch (s)
        {
            case S::Home:       return { 0, "CHAN" };
            case S::Meters:     return { 1, "METER" };
            case S::Routing:    return { 2, "ROUTE" };
            case S::Setup:      return { 3, "SETUP" };
            case S::Library:    return { 4, "LIB" };
            case S::Effects:    return { 5, "FX" };
            case S::Monitor:    return { 6, "MON" };
            case S::Usb:        return { 7, "USB" };
            case S::Scenes:     return { 8, "SCENE" };
            case S::Assign:     return { 9, "ASSIGN" };
            case S::MuteGroups:
            case S::Utility:    break;
        }
        return { -1, "" };
    }

    // Lists the screens and the X32's `/-prefs` share (OSC document pp.50-53).
    const char* const kIpNodes[] = { "addr", "mask", "gateway" };
    const juce::StringArray& rtaOptions()
    {
        static const juce::StringArray names { "Pre EQ", "Spectrograph", "Use RTA Source", "Post GEQ", "Spectrograph", "Solo Priority" };
        return names;
    }
    const juce::StringArray& rtaVisibilities()
    {
        static const juce::StringArray names { "OFF", "25%", "30%", "35%", "40%", "45%", "50%", "55%", "60%", "65%", "70%", "75%", "80%" };
        return names;
    }
    const juce::StringArray& iqSpeakers()
    {
        static const juce::StringArray names { "none", "iQ8", "iQ10", "iQ12", "iQ15", "iQ15B", "iQ18B" };
        return names;
    }
    const juce::StringArray& haOptions()
    {
        static const juce::StringArray names { "Lock Stagebox", "HA Split Local", "HA Split AES50 A", "HA Split AES50 B" };
        return names;
    }
    const juce::StringArray& cardModes()
    {
        static const juce::StringArray names { "32in/32out", "16in/16out", "32in/8out", "8in/32out", "8in/8out", "2in/2out" };
        return names;
    }
    const juce::StringArray& scribbleColours()
    {
        static const juce::StringArray names { "OFF", "RD", "GN", "YE", "BL", "MG", "CY", "WH", "OFFi", "RDi", "GNi", "YEi", "BLi", "MGi", "CYi", "WHi" };
        return names;
    }

    /** SETUP > preamps' blocks of eight inputs: Local 1-8 .. 25-32, AES50 A 1-8 .. 41-48, then B's
        (interface inputs 1-128 in that order); `brief` as the encoder shows it ("1-8", "A17-24"). */
    juce::String preampBlockName (int block, bool brief)
    {
        const int b = block < 4 ? block : (block - 4) % 6;
        const auto range = juce::String (8 * b + 1) + "-" + juce::String (8 * b + 8);
        if (block < 4)
            return brief ? range : "Local " + range;
        return juce::String (brief ? "" : "AES50 ") + (block < 10 ? "A" : "B") + range;
    }
    /** Preamp `k` (0-7) of a block, as numbered within its port (AES50 A17-24's first is 17). */
    int preampNumber (int block, int k)
    {
        return 8 * (block < 4 ? block : (block - 4) % 6) + k + 1;
    }

    /** A graphic EQ's band 0-30 as an encoder names it ("8.00 kHz": the ISO thirds). */
    juce::String geqBandHz (int band)
    {
        static const char* const names[] = { "20 Hz",    "25 Hz",    "31.5 Hz",  "40 Hz",    "50 Hz",    "63 Hz",    "80 Hz",    "100 Hz",
                                             "125 Hz",   "160 Hz",   "200 Hz",   "250 Hz",   "315 Hz",   "400 Hz",   "500 Hz",   "630 Hz",
                                             "800 Hz",   "1.00 kHz", "1.25 kHz", "1.60 kHz", "2.00 kHz", "2.50 kHz", "3.15 kHz", "4.00 kHz",
                                             "5.00 kHz", "6.30 kHz", "8.00 kHz", "10.0 kHz", "12.5 kHz", "16.0 kHz", "20.0 kHz" };
        return names[juce::jlimit (0, 30, band)];
    }

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

    /** A value the console shows that Sidecar takes from elsewhere (the computer, the device): turning it does nothing. */
    KnobBinding readout (const juce::String& label, std::function<juce::String()> text)
    {
        KnobBinding k;
        k.label = label;
        k.get = [] { return 0.0f; };
        k.set = [] (float) {};
        k.text = std::move (text);
        return k;
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
        key->isOn = [this, s] { return (int) screen == s || (screen == Screen::MuteGroups && (int) underScreen == s); };
        key->onClick = [this, s] { if (s == (int) Screen::Utility) pressUtility(); else showScreen ((Screen) s); };
        addAndMakeVisible (*key);
        screenKeys.push_back (std::move (key));
    }
    pagePrev.onClick = [this] { showTab (shownTab() - 1); };
    pageNext.onClick = [this] { showTab (shownTab() + 1); };
    // LAYER up and down: the encoders' next set; on HOME > eq the band (manual 7.2.5).
    const auto stepLayer = [this] (int by)
    {
        if (screen == Screen::Home && tab == 4)
        {
            auto* s = selected();
            eqBand = juce::jlimit (0, s != nullptr && s->isOutput() ? 5 : 3, eqBand + by);
            assignEncoders();
        }
        else
            setLayer (layer + by);
    };
    layerUp.onClick = [stepLayer] { stepLayer (-1); };
    layerDown.onClick = [stepLayer] { stepLayer (1); };
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
        addChildComponent (*push); // the X32's encoders push: no keys under them on the panel
        pushKeys.push_back (std::move (push));
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
    if (publishedSel < 0)
        publishedSel = x32Int ("/-stat/selidx", 0); // nothing selected yet: a remote's selection isn't a change
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
    switch (screen == Screen::MuteGroups ? underScreen : screen)
    {
        case Screen::Home:       return { "home", "config", "gate", "dyn", "eq", "sends", "main" };
        case Screen::Meters:     return { "channel", "mix bus", "aux/fx", "in/out", "rta", "automix" };
        case Screen::Routing:    return { "input", "out", "aux", "p16", "card", "aes a", "aes b", "xlr", "user" };
        case Screen::Setup:      return { "global", "config", "remote", "network", "name/icon", "preamps", "card" };
        case Screen::Library:    return { "channel", "effects", "routing" };
        case Screen::Effects:    return { "home", "fx1", "fx2", "fx3", "fx4", "fx5", "fx6", "fx7", "fx8" };
        case Screen::MuteGroups: return { "mute grp" };
        case Screen::Utility:    return { "copy", "init" };
        case Screen::Monitor:    return { "monitor", "talkback A", "talkback B", "oscillator" };
        case Screen::Assign:     return { "home", "set a", "set b", "set c" };
        case Screen::Scenes:     return { "home", "scenes", "snippets", "param safe", "chan safe", "MIDI" };
        case Screen::Usb:        return { "home", "config" };
    }
    return {};
}

//==============================================================================
// LIBRARY's presets (p.58): channel, effects and routing

namespace
{
    const char* presetExtensionFor (int libraryTab)
    {
        return libraryTab == 0 ? Show::channelPresetExtension : libraryTab == 1 ? Show::fxPresetExtension : Display::routingPresetExtension;
    }

    /** The channel elements' sections, in channelElements()'s order. */
    const juce::uint32 kChannelElementSections[] = { state::Preamp, state::Name, state::Gate, state::Dynamics, state::Eq, state::Sends };
}

const juce::StringArray& Display::channelElements()
{
    static const juce::StringArray names { "Head Amp", "Configuration", "Gate", "Compressor", "Equalizer", "Sends" };
    return names;
}

const juce::StringArray& Display::routingElements()
{
    static const juce::StringArray names { "Channel Inputs", "Analog Out", "Auxiliary Out", "P16 Out", "Card Out", "AES50 Out", "XLR Out" };
    return names;
}

juce::StringArray Display::routingPresetLines() const
{
    // Each element's X32 addresses and their values now (those the map answers).
    juce::StringArray lines;
    const auto add = [this, &lines] (int element, const juce::String& address)
    {
        const auto v = x32Value (address);
        if (v.isNotEmpty())
            lines.add (juce::String (element) + " " + address + " " + v);
    };
    for (const char* group : { "IN", "PLAY" })
        for (auto* b : kInBlocks)
            add (0, "/config/routing/" + juce::String (group) + "/" + b);
    add (0, "/config/routing/routswitch");
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < kSignalTabs[k].count; ++i)
        {
            const auto p = signalPath (kSignalTabs[k], i);
            add (1 + k, p + "/src");
            add (1 + k, p + "/pos");
        }
    for (int k = 0; k < 3; ++k)
        for (int b = 0; b < kBlockTabs[k].blocks; ++b)
            add (k == 0 ? 4 : 5, "/config/routing/" + juce::String (kBlockTabs[k].node) + "/" + kNetBlocks[b]);
    for (auto* b : kOutBlocks)
        add (6, "/config/routing/OUT/" + juce::String (b));
    return lines;
}

juce::Result Display::savePreset (int libraryTab, const juce::String& name)
{
    auto& show = ctx.show;
    const auto legal = juce::File::createLegalFileName (name.isEmpty() ? juce::String ("Preset") : name);
    // SAVE at an existing slot under its own name replaces it.
    const auto before = presets (presetExtensionFor (libraryTab));
    const bool replace = juce::isPositiveAndBelow (presetSaveSlot, before.size()) && before[presetSaveSlot].getFileNameWithoutExtension() == legal;
    const auto file = replace ? before[presetSaveSlot] : ShowPanel::libraryFolder().getNonexistentChildFile (legal, presetExtensionFor (libraryTab), false);
    juce::Result r = juce::Result::ok();
    if (libraryTab == 0)
    {
        auto* st = selected();
        if (st == nullptr)
            return juce::Result::fail ("Select a strip");
        r = show.saveChannelPreset (state::StripRef::of (*st), state::AllSections & ~(juce::uint32) state::Routing, file, name);
    }
    else if (libraryTab == 1)
        r = show.saveFxPreset (presetSlot, file, name);
    else if (! ShowPanel::libraryFolder().createDirectory().wasOk() || ! file.replaceWithText (routingPresetLines().joinIntoString ("\n") + "\n"))
        r = juce::Result::fail ("Couldn't write " + file.getFullPathName());
    libraryStatus = r.wasOk() ? "Saved " + file.getFileNameWithoutExtension() : r.getErrorMessage();
    const auto list = presets (presetExtensionFor (libraryTab));
    presetChoice = juce::jmax (0, list.indexOf (file));
    assignEncoders();
    repaint();
    return r;
}

void Display::assignLibraryPresetEncoders()
{
    // CHANNEL (p.58): encoder 1 the elements a load recalls, each pushed on or
    // off; 2 a preset, LOAD onto the selected strip; 3 SAVE the strip as a
    // preset (asks its name); 4 DELETE the preset. EFFECTS: encoder 1 the FX
    // slot instead. ROUTING: the routing's elements, LOAD, SAVE and DELETE
    // the same way.
    auto& show = ctx.show;
    const int libraryTab = tab;
    const auto list = presets (presetExtensionFor (tab));
    const int n = list.size();
    presetChoice = juce::jlimit (0, juce::jmax (0, n - 1), presetChoice);
    if (tab == 1)
    {
        auto& fx = ctx.engine.getFx();
        KnobBinding slot;
        slot.label = "SELECT";
        slot.get = [this] { return (float) presetSlot / 7.0f; };
        slot.set = [this] (float v) { presetSlot = juce::roundToInt (v * 7.0f); repaint(); };
        slot.text = [this, &fx]
        {
            const int m = fx.getModel (presetSlot);
            return "FX " + juce::String (presetSlot + 1) + (m > ld::kFxExt ? " " + models::model (models::Fx, m).name : juce::String());
        };
        slot.steps = 8.0f;
        encoders[0] = encoder (slot);
    }
    else
    {
        const auto& names = tab == 0 ? channelElements() : routingElements();
        auto& mask = tab == 0 ? channelRecall : routingRecall;
        const int count = names.size();
        libraryElement = juce::jlimit (0, count - 1, libraryElement);
        KnobBinding element;
        element.label = "SELECT";
        element.get = [this, count] { return (float) libraryElement / (float) (count - 1); };
        element.set = [this, count] (float v) { libraryElement = juce::roundToInt (v * (float) (count - 1)); repaint(); };
        element.text = [this, &names] { return names[libraryElement]; };
        element.steps = (float) count;
        encoders[0] = encoder (element, "ON / OFF", [this, &mask] { mask ^= 1u << libraryElement; repaint(); },
                               [this, &mask] { return (mask & (1u << libraryElement)) != 0; });
    }
    KnobBinding pick;
    pick.label = "SCROLL";
    pick.get = [this, n] { return n > 1 ? (float) presetChoice / (float) (n - 1) : 0.0f; };
    pick.set = [this, n] (float v) { presetChoice = juce::roundToInt (v * (float) juce::jmax (0, n - 1)); repaint(); };
    pick.text = [this, list] { return list.isEmpty() ? juce::String ("-") : list[presetChoice].getFileNameWithoutExtension(); };
    pick.steps = (float) juce::jmax (2, n);
    encoders[1] = encoder (pick, "LOAD PRESET", [this, list, &show, libraryTab]
    {
        if (! juce::isPositiveAndBelow (presetChoice, list.size()))
            return;
        const auto& file = list[presetChoice];
        juce::Result r = juce::Result::ok();
        if (libraryTab == 0)
        {
            auto* st = selected();
            if (st == nullptr)
                return;
            juce::uint32 scope = 0;
            for (int k = 0; k < channelElements().size(); ++k)
                if ((channelRecall & (1u << k)) != 0)
                    scope |= kChannelElementSections[k];
            r = show.loadChannelPreset (state::StripRef::of (*st), file, scope);
            refreshStrip();
        }
        else if (libraryTab == 1)
            r = show.loadFxPreset (presetSlot, file);
        else
        {
            // Each line's address set to its value, when its element is recalled.
            juce::StringArray lines;
            lines.addLines (file.loadFileAsString());
            for (auto& line : lines)
            {
                const int element = line.upToFirstOccurrenceOf (" ", false, false).getIntValue();
                const auto command = line.fromFirstOccurrenceOf (" ", false, false).trim();
                if (command.isNotEmpty() && (routingRecall & (1u << element)) != 0)
                {
                    int set = 0;
                    ctx.engine.getWing().runX32Text (command, set);
                }
            }
        }
        libraryStatus = r.wasOk() ? "Loaded " + file.getFileNameWithoutExtension() : r.getErrorMessage();
        repaint();
    });
    presetSaveSlot = juce::jlimit (0, n, presetSaveSlot);
    KnobBinding saveSlot;
    saveSlot.label = "SCROLL";
    saveSlot.get = [this, n] { return n > 0 ? (float) presetSaveSlot / (float) n : 0.0f; };
    saveSlot.set = [this, n] (float v) { presetSaveSlot = juce::roundToInt (v * (float) n); repaint(); };
    saveSlot.text = [this, list] { return juce::isPositiveAndBelow (presetSaveSlot, list.size()) ? list[presetSaveSlot].getFileNameWithoutExtension() : juce::String ("New"); };
    saveSlot.steps = (float) juce::jmax (2, n + 1);
    encoders[2] = encoder (saveSlot, "SAVE PRESET", [this, libraryTab, list]
    {
        auto* st = selected();
        const auto suggested = juce::isPositiveAndBelow (presetSaveSlot, list.size()) ? list[presetSaveSlot].getFileNameWithoutExtension()
                             : libraryTab == 0 && st != nullptr ? (st->getName().isNotEmpty() ? st->getName() : st->getLabel())
                             : libraryTab == 1 ? "FX" + juce::String (presetSlot + 1) : juce::String ("Routing");
        auto* w = new juce::AlertWindow ("SAVE PRESET", "Name:", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", suggested);
        w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<Display> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, libraryTab] (int result)
        {
            if (result != 0 && safe != nullptr)
                safe->savePreset (libraryTab, w->getTextEditorContents ("name").trim());
        }), true);
    });
    encoders[3] = encoder ({}, "DELETE", [this, list]
    {
        if (! juce::isPositiveAndBelow (presetChoice, list.size()))
            return;
        const auto& file = list[presetChoice];
        libraryStatus = file.moveToTrash() || file.deleteFile() ? "Deleted " + file.getFileNameWithoutExtension() : "Couldn't delete " + file.getFileName();
        assignEncoders();
        repaint();
    });
    const juce::Component::SafePointer<Display> safe (this);
    auto said = [safe] (const juce::String& status)
    {
        if (safe == nullptr)
            return;
        safe->libraryStatus = status;
        safe->refreshStrip();
        safe->repaint();
    };
    KnobBinding drive;
    drive.label = "USB DRIVE";
    if (tab != 0)
    {
        // The X32's .efx (the effect in the slot shown) and .rou (the routing) files:
        // IMPORT sets from one, EXPORT writes one.
        const auto kind = tab == 1 ? WingRemote::X32Library::Effect : WingRemote::X32Library::Routing;
        const auto slot = [this, kind] { return kind == WingRemote::X32Library::Effect ? "/fx/" + juce::String (presetSlot + 1) : juce::String(); };
        encoders[4] = encoder (drive, "IMPORT", [this, said, kind, slot] { sceneFiles.importLibrary (kind, slot(), said); });
        encoders[5] = encoder (drive, "EXPORT", [this, said, kind, slot]
        {
            const auto name = kind == WingRemote::X32Library::Effect ? "FX" + juce::String (presetSlot + 1) : juce::String ("Routing");
            sceneFiles.exportLibrary (kind, slot(), name, said);
        });
    }
    else
    {
        // The X32's own channel preset files (.chn): IMPORT loads one onto the
        // selected strip (its source stays), EXPORT writes the strip as one.
        encoders[4] = encoder (drive, "IMPORT", [this, said]
        {
            auto* st = selected();
            sceneFiles.importChannel (st != nullptr ? x32Address (*st) : juce::String(), said);
        });
        encoders[5] = encoder (drive, "EXPORT", [this, said]
        {
            auto* st = selected();
            const auto name = st == nullptr ? juce::String() : st->getName().isNotEmpty() ? st->getName() : st->getLabel();
            sceneFiles.exportChannel (st != nullptr ? x32Address (*st) : juce::String(), name, said);
        });
    }
}

void Display::paintLibraryPresets (juce::Graphics& g, juce::Rectangle<int> r)
{
    // As the LIBRARY photograph: a narrow column beside the preset list (the FX
    // slots, or the elements a load recalls), the list on navy, the chosen preset orange.
    const auto list = presets (presetExtensionFor (tab));
    auto area = pageFrame (g, r);
    if (libraryStatus.isNotEmpty())
    {
        g.setColour (kNavy);
        g.setFont (type::font (10.5f, true));
        g.drawFittedText (libraryStatus, area.removeFromBottom (16), juce::Justification::centredLeft, 1, 0.8f);
    }
    auto side = area.removeFromLeft (juce::jmax (120, area.getWidth() / 5));
    area.removeFromLeft (10);
    g.setColour (kNavy);
    g.setFont (type::font (10.5f, true));
    g.drawText (tab == 1 ? "Select FX Slot" : "Recall", side.removeFromTop (18), juce::Justification::centredLeft);
    if (tab == 1)
    {
        auto& fx = ctx.engine.getFx();
        const float rowH = juce::jmin (15.0f, (float) (side.getHeight() - 32) / 8.0f);
        for (int s = 0; s < 8; ++s)
        {
            auto row = side.removeFromTop (juce::roundToInt (rowH)).toFloat();
            auto dot = row.removeFromLeft (rowH).reduced (2.5f);
            g.setColour (s == presetSlot ? kCurve : kBox);
            g.fillEllipse (dot);
            g.setColour (kNavy);
            g.drawEllipse (dot, 1.0f);
            g.setFont (type::font (juce::jmin (10.5f, rowH * 0.9f), false));
            g.drawText ("Effect " + juce::String (s + 1), row.withTrimmedLeft (3.0f), juce::Justification::centredLeft);
        }
        const int m = fx.getModel (presetSlot);
        auto unit = side.removeFromBottom (28).toFloat();
        g.setColour (kSky);
        g.fillRoundedRectangle (unit, 3.0f);
        g.setColour (kNavy);
        g.setFont (type::font (10.0f, true));
        g.drawFittedText (m > ld::kFxExt ? models::model (models::Fx, m).name : juce::String ("-"), unit.reduced (4.0f, 2.0f).toNearestInt(), juce::Justification::centred, 2, 0.7f);
    }
    else
    {
        const auto& names = tab == 0 ? channelElements() : routingElements();
        const auto mask = tab == 0 ? channelRecall : routingRecall;
        const float rowH = juce::jmin (16.0f, (float) side.getHeight() / (float) names.size());
        for (int k = 0; k < names.size(); ++k)
        {
            auto row = side.removeFromTop (juce::roundToInt (rowH)).toFloat();
            if (k == libraryElement)
            {
                g.setColour (kDeep.withAlpha (0.25f));
                g.fillRect (row);
            }
            g.setColour (kNavy);
            checkBox (g, row.reduced (1.0f), names[k], (mask & (1u << k)) != 0);
        }
    }
    // The list: number, name and (a new slot past the last) "New".
    auto panel = area.removeFromLeft (juce::roundToInt ((float) area.getWidth() * 0.72f)).toFloat();
    g.setColour (kNavy);
    g.fillRoundedRectangle (panel, 3.0f);
    auto inner = panel.reduced (6.0f, 3.0f);
    g.setColour (juce::Colours::white);
    g.setFont (type::font (10.0f, true));
    g.drawText ("Preset Library", inner.removeFromTop (14.0f), juce::Justification::centredLeft);
    const int shown = juce::jmax (1, juce::roundToInt (inner.getHeight() / 15.0f));
    const int first = juce::jlimit (0, juce::jmax (0, list.size() + 1 - shown), presetChoice - shown / 2);
    for (int i = 0; i < shown && first + i <= list.size(); ++i)
    {
        const int at = first + i;
        auto row = inner.removeFromTop (15.0f);
        const bool isNew = at == list.size();
        if (at == presetChoice && ! isNew)
        {
            g.setColour (kAmber);
            g.fillRect (row);
        }
        else if (at == presetSaveSlot)
        {
            g.setColour (juce::Colours::white);
            g.drawRect (row, 1.0f);
        }
        g.setColour (at == presetChoice && ! isNew ? kNavy : juce::Colours::white);
        g.setFont (type::font (10.5f, true));
        g.drawText (juce::String (at + 1).paddedLeft ('0', 2), row.removeFromLeft (26.0f), juce::Justification::centred);
        g.drawFittedText (isNew ? juce::String ("(new slot)") : list[at].getFileNameWithoutExtension(), row.reduced (2.0f, 0.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.8f);
    }
    if (list.isEmpty())
    {
        g.setColour (kDeepText);
        g.setFont (type::font (10.0f, false));
        g.drawText (tab == 0 ? "Save Preset stores the selected strip" : tab == 1 ? "Save Preset stores the FX slot" : "Save Preset stores the routing",
                    panel.reduced (8.0f).withTrimmedTop (20.0f), juce::Justification::centred);
    }
}

//==============================================================================
// SCENES (p.65-67)

namespace
{
    /** A list encoder over `n` entries, its index in `at`. */
    KnobBinding pickKnob (const juce::String& label, int& at, int n, std::function<juce::String()> text, std::function<void()> changed)
    {
        at = juce::jlimit (0, juce::jmax (0, n - 1), at);
        KnobBinding k;
        k.label = label;
        k.get = [&at, n] { return n > 1 ? (float) at / (float) (n - 1) : 0.0f; };
        k.set = [&at, n, changed] (float v) { at = juce::roundToInt (v * (float) juce::jmax (0, n - 1)); changed(); };
        k.text = std::move (text);
        k.steps = (float) juce::jmax (2, n);
        return k;
    }

    juce::String two (int v) { return juce::String (v).paddedLeft ('0', 2); }
}

void Display::assignScenesEncoders()
{
    auto& show = ctx.show;
    const int n = show.getNumScenes();
    const auto sceneName = [&show] (int i)
    {
        const auto sc = show.getScene (i);
        return sc.has_value() ? two (i + 1) + " " + sc->name : juce::String ("-");
    };
    const auto refresh = [this] { assignEncoders(); repaint(); };
    const auto valid = [&show] (int i) { return juce::isPositiveAndBelow (i, show.getNumScenes()); };

    if (tab == 1 && layer == 1)
    {
        // The show file (Sidecar's; the X32 keeps its show in the console): NEW, OPEN,
        // SAVE, SAVE AS, as on every skin.
        auto call = [] (const std::function<void()>& f) { return [f] { if (f) f(); }; };
        encoders[0] = encoder ({}, "NEW SHOW", call (ctx.newShow));
        encoders[1] = encoder ({}, "OPEN", call (ctx.openShow));
        encoders[2] = encoder ({}, "SAVE", [this] { if (ctx.saveShow) ctx.saveShow (false); });
        encoders[3] = encoder ({}, "SAVE AS", [this] { if (ctx.saveShow) ctx.saveShow (true); });
        return;
    }
    if (tab == 0)
    {
        // HOME (p.65): GO the scene picked; SKIP one; NEW (the X32's cue); a
        // snippet of the current scene, GO; PREV and NEXT on 4 and 6 (no UNDO).
        encoders[0] = encoder (pickKnob ("SCENE", selectedScene, n, [this, sceneName] { return sceneName (selectedScene); }, refresh), "GO",
                               [this, &show, valid]
                               {
                                   if (! valid (selectedScene))
                                       return;
                                   show.recallScene (selectedScene);
                                   // Scene Go Next (SETUP > config, `/-prefs/sceneadvance`): on to the next one.
                                   if (x32On ("/-prefs/sceneadvance") && valid (selectedScene + 1))
                                       ++selectedScene;
                                   repaint();
                               });
        encoders[1] = encoder (pickKnob ("SKIP", skipScene, n, [this, sceneName] { return sceneName (skipScene); }, refresh), "SKIP",
                               [this, &show, valid] { if (valid (skipScene)) show.setSceneSkip (skipScene, ! show.getScene (skipScene)->skip); repaint(); },
                               [this, &show, valid] { return valid (skipScene) && show.getScene (skipScene)->skip; });
        encoders[2] = encoder ({}, "NEW", [this, &show]
        {
            selectedScene = show.addScene ("Scene " + two (show.getNumScenes() + 1));
            assignEncoders();
        });
        encoders[3] = encoder ({}, "PREV", [this, &show] { show.recallPrevious(); repaint(); });
        const int current = show.getCurrentSceneIndex();
        const auto scene = show.getScene (current);
        const int snippets = scene.has_value() ? scene->numSnippets : 0;
        encoders[4] = encoder (pickKnob ("SNIPPET", homeSnippet, snippets, [this, &show, current, snippets]
                               {
                                   const auto s = show.getSnippet (current, homeSnippet);
                                   return snippets > 0 && s.has_value() ? two (homeSnippet + 1) + " " + s->name : juce::String ("-");
                               }, refresh), "GO",
                               [this, &show, current, snippets] { if (homeSnippet < snippets) show.recallSnippet (current, homeSnippet); repaint(); });
        // 6 UNDO (p.65): the console as it was before the last scene's recall. (NEXT is the panel's key.)
        encoders[5] = encoder ({}, "UNDO", [this, &show] { if (show.canUndoRecall()) show.undoRecall(); repaint(); });
    }
    else if (tab == 1)
    {
        // SCENES (p.65): LOAD a scene; SAVE into a slot (asks its name and
        // note); EDIT one's name and note; IMPORT a library scene file, EXPORT
        // the loaded one's; the save safes (sections left out of a SAVE).
        encoders[0] = encoder (pickKnob ("SCENE", selectedScene, n, [this, sceneName] { return sceneName (selectedScene); }, refresh), "LOAD",
                               [this, &show, valid] { if (valid (selectedScene)) show.recallScene (selectedScene); repaint(); });
        encoders[1] = encoder (pickKnob ("SLOT", sceneSlot, n + 1, [this, n, sceneName] { return sceneSlot >= n ? two (sceneSlot + 1) + " (NEW)" : sceneName (sceneSlot); }, refresh),
                               "SAVE", [this, &show]
        {
            const auto sc = show.getScene (sceneSlot);
            auto* w = new juce::AlertWindow ("SAVE SCENE", "Scene " + two (sceneSlot + 1) + ":", juce::MessageBoxIconType::NoIcon, this);
            w->addTextEditor ("name", sc.has_value() ? sc->name : "Scene " + two (sceneSlot + 1), "Name:");
            w->addTextEditor ("notes", sc.has_value() ? sc->notes : juce::String(), "Note:");
            w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<Display> safe (this);
            const int slot = sceneSlot;
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, slot] (int result)
            {
                if (result != 0 && safe != nullptr)
                    safe->saveScene (slot, w->getTextEditorContents ("name").trim(), w->getTextEditorContents ("notes"));
            }), true);
        });
        encoders[2] = encoder (pickKnob ("EDIT", editScene, n, [this, sceneName] { return sceneName (editScene); }, refresh), "EDIT", [this, &show, valid]
        {
            const auto sc = show.getScene (editScene);
            if (! sc.has_value())
                return;
            auto* w = new juce::AlertWindow ("EDIT SCENE", "Scene " + two (editScene + 1) + ":", juce::MessageBoxIconType::NoIcon, this);
            w->addTextEditor ("name", sc->name, "Name:");
            w->addTextEditor ("notes", sc->notes, "Note:");
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<Display> safe (this);
            const int index = editScene;
            auto* showPtr = &show;
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, index, showPtr] (int result)
            {
                if (result != 0 && safe != nullptr)
                {
                    showPtr->setSceneName (index, w->getTextEditorContents ("name").trim());
                    showPtr->setSceneNotes (index, w->getTextEditorContents ("notes"));
                    safe->assignEncoders();
                    safe->repaint();
                }
            }), true);
        });
        const auto files = presets (Show::sceneFileExtension);
        encoders[3] = encoder (pickKnob ("FILE", importFile, files.size(), [this, files]
                               { return juce::isPositiveAndBelow (importFile, files.size()) ? files[importFile].getFileNameWithoutExtension() : juce::String ("NONE"); }, refresh),
                               "IMPORT", [this, &show, files]
        {
            if (! juce::isPositiveAndBelow (importFile, files.size()))
                return;
            juce::String error;
            const int at = show.importScene (files[importFile], -1, error);
            sceneStatus = at >= 0 ? "Imported " + files[importFile].getFileName() + " as scene " + two (at + 1) : error;
            assignEncoders();
            repaint();
        });
        encoders[4] = encoder ({}, "EXPORT", [this, &show, valid]
        {
            if (! valid (selectedScene))
                return;
            const auto name = juce::File::createLegalFileName (show.getScene (selectedScene)->name);
            const auto file = ShowPanel::libraryFolder().getNonexistentChildFile (name.isEmpty() ? juce::String ("Scene") : name, Show::sceneFileExtension, false);
            const auto result = show.exportScene (selectedScene, file);
            sceneStatus = result.wasOk() ? "Exported to " + file.getFileName() : result.getErrorMessage();
            assignEncoders();
            repaint();
        });
        encoders[5] = encoder (pickKnob ("SAVE SAFE", saveSafeRow, state::kNumSections,
                               [this] { return juce::String (state::sectionName (state::sectionAt (saveSafeRow))).toUpperCase(); }, refresh),
                               "SAFE", [this] { saveSafes ^= (juce::uint32) state::sectionAt (saveSafeRow); repaint(); },
                               [this] { return (saveSafes & (juce::uint32) state::sectionAt (saveSafeRow)) != 0; });
    }
    else if (tab == 2)
    {
        assignSnippetEncoders();
    }
    else if (tab == 3 || tab == 4)
    {
        // PARAM SAFE (p.66): four columns, CHAN SAFE six; each encoder moves
        // through its column, pushed to check the box.
        const auto columns = tab == 3 ? paramSafeColumns() : chanSafeColumns();
        for (size_t c = 0; c < columns.size() && c < encoders.size(); ++c)
        {
            const auto items = columns[c].items;
            if (items.empty())
                continue;
            int& row = safeRows[(tab == 3 ? 0 : 4) + c];
            encoders[c] = encoder (pickKnob ("SELECT", row, (int) items.size(), [&row, items] { return items[(size_t) row].label; }, [this] { repaint(); }),
                                   "SAFE", [this, &row, items] { items[(size_t) row].flip(); repaint(); },
                                   [&row, items] { return items[(size_t) row].on(); });
        }
    }
    else
    {
        // MIDI (p.67): a scene, then what its recall sends: a program change,
        // a control change or a note, its channel, number and value.
        encoders[0] = encoder (pickKnob ("SCENE", selectedScene, n, [this, sceneName] { return sceneName (selectedScene); }, refresh));
        if (! valid (selectedScene))
            return;
        const auto midi = [this, &show] { return parseSceneMidi (show.getScene (selectedScene)->midi); };
        const auto set = [this, &show] (const SceneMidi& m) { show.setSceneMidi (selectedScene, sceneMidiText (m)); repaint(); };
        static const juce::StringArray kinds { "OFF", "PRG CHANGE", "CTRL CHANGE", "NOTE" };
        KnobBinding kind;
        kind.label = "TYPE";
        kind.get = [midi] { return (float) juce::jmax (0, midi().kind) / 3.0f; };
        kind.set = [midi, set, this] (float v) { auto m = midi(); m.kind = juce::roundToInt (v * 3.0f); set (m); assignEncoders(); };
        kind.text = [midi] { const int k = midi().kind; return k < 0 ? juce::String ("CUSTOM") : kinds[k]; };
        kind.steps = 4.0f;
        encoders[1] = encoder (kind);
        const int k = midi().kind;
        if (k <= 0)
            return;
        const auto field = [midi, set] (const char* label, int SceneMidi::*member, int lo, int hi)
        {
            KnobBinding b;
            b.label = label;
            b.get = [midi, member, lo, hi] { return (float) (midi().*member - lo) / (float) (hi - lo); };
            b.set = [midi, set, member, lo, hi] (float v) { auto m = midi(); m.*member = lo + juce::roundToInt (v * (float) (hi - lo)); set (m); };
            b.text = [midi, member] { return juce::String (midi().*member); };
            b.steps = (float) (hi - lo + 1);
            return b;
        };
        encoders[2] = encoder (field ("CHANNEL", &SceneMidi::channel, 1, 16));
        encoders[3] = encoder (field (k == 1 ? "PROGRAM" : k == 2 ? "CONTROL" : "NOTE", &SceneMidi::number, 0, 127));
        if (k > 1)
            encoders[4] = encoder (field (k == 2 ? "VALUE" : "VELOCITY", &SceneMidi::value, 0, 127));
    }
}

int Display::saveScene (int slot, const juce::String& name, const juce::String& notes)
{
    auto& show = ctx.show;
    int index = slot;
    if (slot >= show.getNumScenes())
        index = show.addScene (name);
    else
        show.storeScene (slot);
    show.setSceneScope (index, state::AllSections & ~saveSafes);
    show.setSceneName (index, name);
    show.setSceneNotes (index, notes);
    sceneStatus = "Saved scene " + two (index + 1);
    assignEncoders();
    repaint();
    return index;
}

Display::SceneMidi Display::parseSceneMidi (const juce::String& text)
{
    // One message of hexadecimal bytes ("C005", "B0 07 64", "90,3C,7F"); else -1.
    SceneMidi m;
    const auto hex = text.retainCharacters ("0123456789abcdefABCDEF");
    if (text.trim().isEmpty())
        return m;
    if (hex.length() != text.removeCharacters (" ,").length() || hex.length() % 2 != 0)
        return { -1 };
    juce::Array<int> bytes;
    for (int i = 0; i < hex.length(); i += 2)
        bytes.add (hex.substring (i, i + 2).getHexValue32());
    const int status = bytes[0] & 0xf0;
    m.channel = (bytes[0] & 0x0f) + 1;
    if (status == 0xc0 && bytes.size() == 2)
        m.kind = 1;
    else if (status == 0xb0 && bytes.size() == 3)
        m.kind = 2;
    else if (status == 0x90 && bytes.size() == 3)
        m.kind = 3;
    else
        return { -1 };
    m.number = bytes[1] & 0x7f;
    if (bytes.size() > 2)
        m.value = bytes[2] & 0x7f;
    return m;
}

juce::String Display::sceneMidiText (const SceneMidi& m)
{
    if (m.kind <= 0)
        return {};
    const auto byte = [] (int v) { return juce::String::toHexString (v & 0xff).paddedLeft ('0', 2).toUpperCase(); };
    const int status = (m.kind == 1 ? 0xc0 : m.kind == 2 ? 0xb0 : 0x90) | (juce::jlimit (1, 16, m.channel) - 1);
    auto t = byte (status) + " " + byte (m.number);
    if (m.kind > 1)
        t << " " << byte (m.value);
    return t;
}

std::vector<Display::SafeColumn> Display::paramSafeColumns()
{
    // The X32's four columns over Sidecar's safes, which hold for every strip
    // (inputs and buses alike): the strips' sections, their sends, the mute
    // groups, and the console's settings and routing.
    auto& show = ctx.show;
    const auto section = [&show] (const juce::String& label, juce::uint32 bits) -> SafeItem
    {
        return { label, [&show, bits] { return (show.getGlobalSafe().sections & bits) == bits; },
                 [&show, bits]
                 {
                     auto s = show.getGlobalSafe();
                     s.sections = (s.sections & bits) == bits ? s.sections & ~bits : s.sections | bits;
                     show.setGlobalSafe (s);
                 } };
    };
    using namespace state;
    std::vector<SafeColumn> columns {
        { "Input Channels", { section ("HA", Preamp), section ("Configuration", Name), section ("Gate", Gate), section ("Equalizer", Eq),
                              section ("Compressor", Dynamics), section ("Insert", Inserts), section ("Groups", Groups),
                              section ("Fader, Pan, Mute", Fader) } },
        { "Mix Sends", { section ("Mix Sends 1-16", Sends) } },
        { "Mute Groups", {} },
        { "Console", {} },
    };
    columns[2].items.push_back ({ "Mute Groups 1-6", [&show] { return (show.getGlobalSafe().muteGroups.low() & 0x3fu) == 0x3fu; }, [&show]
    {
        auto s = show.getGlobalSafe();
        const bool on = (s.muteGroups.low() & 0x3fu) == 0x3fu;
        s.muteGroups = on ? (s.muteGroups & ~GroupBits::mask (0x3fu)) : (s.muteGroups | GroupBits::mask (0x3fu));
        show.setGlobalSafe (s);
    } });
    columns[3].items.push_back ({ "Configuration, Solo", [&show] { return show.getGlobalSafe().console; }, [&show]
    {
        auto s = show.getGlobalSafe();
        s.console = ! s.console;
        show.setGlobalSafe (s);
    } });
    columns[3].items.push_back (section ("Routing, Output Patch", Routing));
    return columns;
}

std::vector<Display::SafeColumn> Display::chanSafeColumns()
{
    // Channels 1-16 and 17-32, aux and FX returns, mix buses, matrices, mains
    // and DCAs, FX slots: a strip's recall safe, a DCA's or FX slot's global safe.
    auto& e = ctx.engine;
    auto& show = ctx.show;
    const auto strip = [] (Strip* s) -> SafeItem
    {
        return { s->getLabel(), [s] { return s->params.recallSafe.load(); }, [s] { s->params.recallSafe = ! s->params.recallSafe.load(); } };
    };
    std::vector<SafeColumn> columns { { "Channels", {} }, { "Channels", {} }, { "Aux / FX Returns", {} }, { "Mix Buses", {} }, { "Main / Matrix / DCA", {} }, { "Effects", {} } };
    const auto& channels = e.getChannels();
    for (size_t i = 0; i < channels.size(); ++i)
        columns[i < 16 ? 0 : 1].items.push_back (strip (channels[i].get()));
    for (auto& s : e.getAux())
        columns[2].items.push_back (strip (s.get()));
    for (auto& s : e.getBuses())
        columns[3].items.push_back (strip (s.get()));
    for (auto& s : e.getMatrices())
        columns[4].items.push_back (strip (s.get()));
    for (auto& s : e.getMains())
        columns[4].items.push_back (strip (s.get()));
    for (int d = 0; d < 8; ++d)
        columns[4].items.push_back ({ "DCA " + juce::String (d + 1), [&show, d] { return (show.getGlobalSafe().dcas.low() & (juce::uint64 (1) << d)) != 0; }, [&show, d]
        {
            auto s = show.getGlobalSafe();
            const auto bit = GroupBits::mask (juce::uint64 (1) << d);
            s.dcas = (s.dcas.low() & (juce::uint64 (1) << d)) != 0 ? (s.dcas & ~bit) : (s.dcas | bit);
            show.setGlobalSafe (s);
        } });
    for (int f = 0; f < 8; ++f)
        columns[5].items.push_back ({ "FX " + juce::String (f + 1), [&show, f] { return (show.getGlobalSafe().fx & (juce::uint64 (1) << f)) != 0; }, [&show, f]
        {
            auto s = show.getGlobalSafe();
            s.fx ^= juce::uint64 (1) << f;
            show.setGlobalSafe (s);
        } });
    return columns;
}

void Display::paintSafeColumns (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<SafeColumn>& columns, const int* pointed, const juce::String& footer)
{
    // A column of check boxes per encoder, the one it points at marked.
    if (footer.isNotEmpty())
    {
        g.setColour (kDeep);
        g.setFont (type::font (10.0f));
        g.drawFittedText (footer, r.removeFromBottom (14.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        r.removeFromBottom (2.0f);
    }
    const float w = r.getWidth() / (float) columns.size();
    size_t most = 1;
    for (auto& c : columns)
        most = std::max (most, c.items.size());
    for (size_t c = 0; c < columns.size(); ++c)
    {
        auto col = block (g, r.removeFromLeft (w).reduced (2.0f, 0.0f), columns[c].title).reduced (2.0f, 1.0f);
        const auto& items = columns[c].items;
        // Every box when they fit at 9 px or more; else scrolled at 14.
        const float fit = col.getHeight() / (float) most;
        const float rowH = fit >= 9.0f ? juce::jmin (14.0f, fit) : 14.0f;
        const int rows = juce::jmax (1, (int) (col.getHeight() / rowH));
        const int at = pointed[c];
        const int n = (int) items.size();
        const int from = juce::jlimit (0, juce::jmax (0, n - rows), at - rows / 2);
        for (int i = from; i < juce::jmin (n, from + rows); ++i)
        {
            auto row = col.removeFromTop (rowH);
            if (i == at)
            {
                g.setColour (kDeep.withAlpha (0.35f));
                g.fillRect (row);
            }
            checkBox (g, row.reduced (1.0f), items[(size_t) i].label, items[(size_t) i].on());
        }
    }
}

void Display::paintScenes (juce::Graphics& g, juce::Rectangle<int> r)
{
    auto& show = ctx.show;
    juce::StringArray left, right;
    if (tab == 0 || tab == 1)
    {
        // The scenes around the one picked; the current one starred, the
        // others' encoders' picks named.
        const int n = show.getNumScenes(), current = show.getCurrentSceneIndex();
        auto area = r.reduced (12);
        if (tab == 1)
        {
            // The save safes beside the list, the import file and status below.
            auto side = area.removeFromRight (area.getWidth() / 3).toFloat();
            area.removeFromRight (8);
            auto box = block (g, side, "SAVE SAFES").reduced (3.0f, 1.0f);
            const float rowH = juce::jmin (14.0f, box.getHeight() / (float) state::kNumSections);
            for (int k = 0; k < state::kNumSections; ++k)
            {
                auto row = box.removeFromTop (rowH);
                if (k == saveSafeRow)
                {
                    g.setColour (kDeep.withAlpha (0.35f));
                    g.fillRect (row);
                }
                checkBox (g, row.reduced (1.0f), juce::String (state::sectionName (state::sectionAt (k))), (saveSafes & (juce::uint32) state::sectionAt (k)) != 0);
            }
            if (sceneStatus.isNotEmpty())
            {
                g.setColour (kDeep);
                g.setFont (type::font (10.0f));
                g.drawFittedText (sceneStatus, area.removeFromBottom (14), juce::Justification::centredLeft, 1, 0.8f);
            }
        }
        const int rows = juce::jmax (1, (area.getHeight() - 4) / 22);
        const int from = juce::jlimit (0, juce::jmax (0, n + (tab == 1 ? 1 : 0) - rows), selectedScene - rows / 2);
        for (int i = from; i < juce::jmin (n + (tab == 1 ? 1 : 0), from + rows); ++i)
        {
            const auto scene = show.getScene (i);
            juce::StringArray marks;
            if (tab == 0 && i == skipScene)
                marks.add ("SKIP?");
            if (tab == 1 && i == sceneSlot)
                marks.add ("SAVE");
            if (tab == 1 && i == editScene)
                marks.add ("EDIT");
            left.add ((i == selectedScene ? "> " : "  ") + two (i + 1) + (i == current ? "  *" : "") + "  " + (scene.has_value() ? scene->name : juce::String ("(empty)")));
            right.add ((scene.has_value() && scene->skip ? "SKIPPED  " : "") + marks.joinIntoString (" "));
        }
        if (left.isEmpty())
        {
            left.add ("No scenes");
            right.add (tab == 0 ? "NEW adds one" : "SAVE into 01");
        }
        paintRows (g, area, left, right);
        return;
    }
    if (tab == 2)
    {
        paintSnippets (g, r);
        return;
    }
    if (tab == 3)
    {
        paintSafeColumns (g, r.reduced (10).toFloat(), paramSafeColumns(), safeRows.data(), "Sidecar's parameter safes hold for inputs and buses alike.");
        return;
    }
    if (tab == 4)
    {
        paintSafeColumns (g, r.reduced (10).toFloat(), chanSafeColumns(), safeRows.data() + 4, {});
        return;
    }
    // MIDI: each scene and what its recall sends.
    const int n = show.getNumScenes();
    const int rows = juce::jmax (1, (r.getHeight() - 24) / 24);
    const int from = juce::jlimit (0, juce::jmax (0, n - rows), selectedScene - rows / 2);
    for (int i = from; i < juce::jmin (n, from + rows); ++i)
    {
        const auto scene = show.getScene (i);
        const auto m = parseSceneMidi (scene->midi);
        left.add ((i == selectedScene ? "> " : "  ") + two (i + 1) + "  " + scene->name);
        right.add (m.kind < 0 ? scene->midi
                   : m.kind == 0 ? juce::String ("-")
                   : (m.kind == 1 ? "PRG " : m.kind == 2 ? "CC " : "NOTE ") + juce::String (m.number) + (m.kind > 1 ? " / " + juce::String (m.value) : juce::String())
                         + "  CH " + juce::String (m.channel));
    }
    if (n == 0)
    {
        left.add ("No scenes");
        right.add ("-");
    }
    paintRows (g, r.reduced (12), left, right);
}

const juce::StringArray& Display::scribbleNames()
{
    // Names for the usual sources, the drums first (Sidecar's list).
    static const juce::StringArray names { "Kick", "Kick In", "Kick Out", "Snare", "Snare Top", "Snare Bot", "Hi-Hat", "Tom 1", "Tom 2",
                                           "Floor Tom", "OH L", "OH R", "Ride", "Bass", "Bass DI", "Gtr 1", "Gtr 2", "Ac Gtr", "Keys L",
                                           "Keys R", "Piano", "Organ", "Synth", "Lead Vox", "Vox 1", "Vox 2", "Vox 3", "BGV", "Choir", "Talk",
                                           "Brass", "Sax", "Trumpet", "Strings", "Violin", "Cello", "Perc", "Click", "Playback", "Ambience" };
    return names;
}

void Display::editStripName (Strip& strip)
{
    auto* w = new juce::AlertWindow ("NAME", strip.getLabel() + ":", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", strip.getName());
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<Display> safe (this);
    Strip* target = &strip;
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, target] (int result)
    {
        if (result != 0 && safe != nullptr)
        {
            target->setName (w->getTextEditorContents ("name").trim().substring (0, 12));
            safe->repaint();
        }
    }), true);
}

void Display::paintScribble (juce::Graphics& g, juce::Rectangle<float> r, Strip* s)
{
    // Scribble Strip Setup: Channel, Color, Icon, Snippets and Name, each a
    // column of its encoder's choices around the chosen one.
    auto area = block (g, r, "SCRIBBLE STRIP SETUP").reduced (6.0f, 4.0f);
    const float w = area.getWidth() / 5.0f;
    const float rowH = 14.0f;
    const int rows = juce::jlimit (3, 11, (int) ((area.getHeight() - 16.0f) / rowH)) | 1; // odd, the chosen one in the middle
    const auto column = [&] (const juce::String& title)
    {
        auto c = area.removeFromLeft (w).reduced (5.0f, 0.0f);
        g.setColour (kNavy);
        g.setFont (type::font (10.5f, true));
        g.drawText (title, c.removeFromTop (16.0f), juce::Justification::centred);
        return c.withHeight (rowH * (float) rows);
    };
    // A list of n entries, `at` chosen, scrolled to show it.
    const auto list = [&] (juce::Rectangle<float> c, int n, int at, const std::function<void (juce::Rectangle<float>, int, bool)>& paintEntry)
    {
        g.setColour (kNavy);
        g.drawRect (c.expanded (1.0f), 1.0f);
        const int from = juce::jlimit (0, juce::jmax (0, n - rows), at - rows / 2);
        for (int i = from; i < juce::jmin (n, from + rows); ++i)
            paintEntry (c.removeFromTop (rowH), i, i == at);
    };
    const auto text = [&g] (juce::Rectangle<float> row, const juce::String& t, bool chosen)
    {
        g.setColour (chosen ? kDeep : kBox);
        g.fillRect (row);
        g.setColour (chosen ? juce::Colours::white : kNavy);
        g.setFont (type::font (juce::jmin (11.0f, row.getHeight() * 0.8f), chosen));
        g.drawFittedText (t, row.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
    };

    const auto strips = recallStrips();
    const int numStrips = (int) strips.size();
    const int at = scribbleDca >= 0 ? numStrips + scribbleDca : (int) (std::find (strips.begin(), strips.end(), s) - strips.begin());
    list (column ("Channel"), numStrips + ctx.engine.getNumDcas(), at, [&] (juce::Rectangle<float> row, int i, bool chosen)
          { text (row, i < numStrips ? strips[(size_t) i]->getLabel() : "DCA " + juce::String (i - numStrips + 1), chosen); });
    const int colour = scribbleDca >= 0 ? 1 + juce::jmax (0, scribbleColours().indexOf (x32Value ("/dca/" + juce::String (scribbleDca + 1) + "/config/color")))
                     : s != nullptr ? juce::jlimit (1, 16, s->getColourIndex()) : 0;
    list (column ("Color"), 16, colour - 1, [&] (juce::Rectangle<float> row, int i, bool chosen)
    {
        g.setColour (consoleColour (i + 1));
        g.fillRect (row.reduced (0.0f, 0.5f));
        if (chosen)
        {
            g.setColour (juce::Colours::white);
            g.drawRect (row, 2.0f);
        }
    });
    const auto icons = wingIconNumbers();
    const auto found = s != nullptr && scribbleDca < 0 ? std::find (icons.begin(), icons.end(), s->params.icon.load()) : icons.end();
    const int icon = found != icons.end() ? (int) (found - icons.begin()) : -1;
    {
        // Icon: the chosen one large, its neighbours either side.
        auto c = column ("Icon");
        g.setColour (kSkyLine);
        g.fillRect (c);
        const auto label = c.removeFromBottom (14.0f);
        const float side = juce::jmin (c.getWidth() / 3.0f, c.getHeight());
        for (int d = -1; d <= 1; ++d)
        {
            const int i = icon + d;
            if (icon < 0 && d != 0)
                continue;
            auto box = juce::Rectangle<float> (side, side).withCentre ({ c.getCentreX() + (float) d * side, c.getCentreY() });
            if (d == 0)
            {
                g.setColour (kDeep);
                g.fillRect (box);
            }
            if (juce::isPositiveAndBelow (i, (int) icons.size()))
                drawWingIcon (g, icons[(size_t) i], box.reduced (side * 0.15f), d == 0 ? juce::Colours::white : kNavy);
        }
        g.setColour (kNavy);
        g.setFont (type::font (10.0f, true));
        g.drawFittedText (juce::isPositiveAndBelow (icon, (int) icons.size()) ? wingIconName (icons[(size_t) icon]) : juce::String ("NO ICON"),
                          label.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }
    const auto& names = scribbleNames();
    list (column ("Snippets"), names.size(), scribbleName, [&] (juce::Rectangle<float> row, int i, bool chosen) { text (row, names[i], chosen); });
    auto name = column ("Name");
    valueBox (g, name.removeFromTop (rowH + 2.0f), scribbleDca >= 0 ? dcaName (scribbleDca) : s != nullptr ? s->getName() : juce::String ("-"));
}

void Display::paintPreamps (juce::Graphics& g, juce::Rectangle<float> r)
{
    // The preamp blocks, the chosen block's eight preamps in two rows of four
    // (this layer's row marked), and the devices connected (none on AES50).
    const float side = juce::jmax (96.0f, r.getWidth() * 0.17f);
    auto blocks = block (g, r.removeFromLeft (side), "PREAMP BLOCKS").reduced (3.0f, 2.0f);
    r.removeFromLeft (6.0f);
    auto devices = block (g, r.removeFromRight (side), "CONNECTED DEVICES").reduced (3.0f, 2.0f);
    r.removeFromRight (6.0f);
    auto controls = block (g, r, "PREAMP CONTROLS").reduced (6.0f, 4.0f);

    {
        // The sixteen blocks (the manual's picture): the four local ones, AES50 A's six, B's six.
        const float blockH = juce::jmin (18.0f, blocks.getHeight() / 16.0f);
        for (int b = 0; b < 16; ++b)
        {
            auto row = blocks.removeFromTop (blockH);
            g.setColour (b == preampBlock ? kDeep : kBox);
            g.fillRect (row.reduced (0.0f, 0.5f));
            g.setColour (b == preampBlock ? juce::Colours::white : kNavy);
            g.setFont (type::font (juce::jmin (11.0f, blockH * 0.85f), b == preampBlock));
            g.drawFittedText (preampBlockName (b, false), row.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
    }

    g.setColour (kNavy);
    g.setFont (type::font (10.5f, true));
    {
        // The devices on the AES50 ports (none: the interface's inputs stand in), then the options.
        auto ports = devices.removeFromTop (juce::jmin (46.0f, devices.getHeight() * 0.4f));
        for (const char* bus : { "AES50 A", "AES50 B" })
        {
            auto d = ports.removeFromTop (ports.getHeight() * (bus[6] == 'A' ? 0.5f : 1.0f));
            g.setFont (type::font (10.0f, true));
            g.drawText (juce::String (bus) + ": none", d, juce::Justification::centredLeft);
        }
        const int flags = x32Bits (x32Value ("/-prefs/haflags"));
        const float rowH = juce::jmin (14.0f, devices.getHeight() / 4.0f);
        for (int k = 0; k < 4; ++k)
        {
            auto row = devices.removeFromTop (rowH);
            if (k == haOption)
            {
                g.setColour (kGr);
                g.drawRect (row, 1.0f);
            }
            g.setColour (kNavy);
            checkBox (g, row.reduced (1.0f), haOptions()[k], (flags & (1 << k)) != 0);
        }
    }

    const float rowH = controls.getHeight() / 2.0f;
    for (int half = 0; half < 2; ++half)
    {
        auto row = controls.removeFromTop (rowH).reduced (0.0f, 3.0f);
        if (half == layer)
        {
            g.setColour (kYellow);
            g.fillRect (row.withWidth (4.0f));
        }
        row.removeFromLeft (8.0f);
        const float w = row.getWidth() / 4.0f;
        for (int k = 0; k < 4; ++k)
        {
            const int input = 8 * preampBlock + 4 * half + k;
            auto tile = row.removeFromLeft (w).reduced (6.0f, 0.0f);
            g.setColour (kSkyLine);
            g.drawRoundedRectangle (tile, 3.0f, 1.0f);
            auto* src = ctx.engine.getSource (input);
            auto top = tile.removeFromTop (18.0f).reduced (2.0f);
            g.setColour (kNavy);
            g.setFont (type::font (10.0f, true));
            g.drawText (juce::String (preampNumber (preampBlock, 4 * half + k)), top.removeFromLeft (16.0f), juce::Justification::centred);
            if (src == nullptr)
            {
                g.setFont (type::font (10.0f));
                g.drawFittedText ("(no preamp)", tile.toNearestInt(), juce::Justification::centred, 1, 0.7f);
                continue;
            }
            // Its gain, then its knob and A/D-Gain when there's room, +48V along the bottom.
            valueBox (g, top, juce::String (src->gainDb.load(), 1));
            chip (g, tile.removeFromBottom (16.0f).reduced (6.0f, 1.0f), "+48V", src->phantom.load());
            if (tile.getHeight() >= 34.0f)
            {
                g.setColour (kNavy);
                g.setFont (type::font (9.5f));
                g.drawText ("A/D-Gain", tile.removeFromBottom (12.0f), juce::Justification::centred);
                knobIcon (g, tile.getCentre(), juce::jmin (12.0f, tile.getHeight() * 0.38f), (src->gainDb.load() + 12.0f) / 72.0f);
            }
        }
    }
}

const std::vector<Display::ConfigItem>& Display::configPrefs()
{
    // SETUP > config's preferences, by the groups the X32 OSC document names for this
    // page (p.50): Confirm Pop-ups, View Preferences, General Prefs, Mute System; then
    // the solo's and the mono bus's. Sidecar acts on Scene Go Next (SCENES > home's GO)
    // and 12h Clock Mode (the status bar); the others are kept, for a remote to read.
    // (The solo options themselves are MONITOR's.)
    static const std::vector<ConfigItem> items {
        { "CONFIRM GENERAL", "/-prefs/confirm_general", "ON", "OFF" },
        { "CONFIRM OVERWRITE", "/-prefs/confirm_overwrite", "ON", "OFF" },
        { "CONFIRM SCENE LOAD", "/-prefs/confirm_sceneload", "ON", "OFF" },
        { "RETURN TO LAST", "/-prefs/viewrtn", "ON", "OFF" },
        { "SEL FOLLOWS BANK", "/-prefs/selfollowsbank", "ON", "OFF" },
        { "AUTO SELECT", "/-prefs/autosel", "ON", "OFF" },
        { "SCENE GO NEXT", "/-prefs/sceneadvance", "ON", "OFF" },
        { "SAFE MAIN LEVELS", "/-prefs/safe_masterlevels", "ON", "OFF" },
        { "12H CLOCK MODE", "/-prefs/clockmode", "12h", "24h" },
        { "HARD MUTES", "/-prefs/hardmute", "ON", "OFF" },
        { "DCA GROUPS MUTE", "/-prefs/dcamute", "ON", "OFF" },
        { "INVERT MUTE LEDS", "/-prefs/invertmutes", "INV", "NORM" },
        { "DIM ON PFL", "/config/solo/dimpfl", "ON", "OFF" },
        { "MONO LINKED TO LR", "/config/mono/link", "ON", "OFF" },
        { "MONO AS CENTER (LCR)", "/config/mono/mode", "LCR", "LR+M" },
    };
    return items;
}

const std::vector<Display::ConfigItem>& Display::configLinks()
{
    // What a channel link shares (`/config/linkcfg`).
    static const std::vector<ConfigItem> items {
        { "LINK HA + DELAY", "/config/linkcfg/hadly", "ON", "OFF" },
        { "LINK EQ", "/config/linkcfg/eq", "ON", "OFF" },
        { "LINK DYNAMICS", "/config/linkcfg/dyn", "ON", "OFF" },
        { "LINK FADER + MUTE", "/config/linkcfg/fdrmute", "ON", "OFF" },
    };
    return items;
}

std::vector<Strip*> Display::recallStrips() const
{
    // The console's strips in the X32's order: channels, aux and FX returns, buses, matrices, mains.
    std::vector<Strip*> out;
    auto& e = ctx.engine;
    for (const auto* list : { &e.getChannels(), &e.getAux(), &e.getBuses(), &e.getMatrices(), &e.getMains() })
        for (const auto& s : *list)
            out.push_back (s.get());
    return out;
}

int Display::numLayers() const
{
    if (screen == Screen::Assign)
        return tab == 0 ? 3 : 1; // home's layers are the sets (LAYER ^ v, p.63)
    if (screen == Screen::Scenes)
        return tab == 1 || tab == 2 ? 2 : 1; // scenes' second layer: the show file; snippets': FIRE and DELETE (both Sidecar's)
    if (screen == Screen::Setup)
        return tab == 2 || tab == 5 ? 2 : 1; // remote: the MIDI kinds, then SysEx; preamps: the top four, the bottom four
    if (screen == Screen::Routing)
        return 1; // none of its tabs has a second layer (p.51)
    if (screen == Screen::Effects && tab > 0)
    {
        // A graphic EQ: its two sides (a dual one); else a slot's parameters, six per layer.
        if (isGraphicEq (tab - 1))
            return geqBands (tab - 1).size() > 31 ? 2 : 1;
        const int m = ctx.engine.getFx().getModel (tab - 1);
        const int n = m > ld::kFxExt ? (int) models::model (models::Fx, m).params.size() : 0;
        return juce::jmax (1, (n + 5) / 6);
    }
    if (screen == Screen::Usb)
        return tab == 1 ? 2 : 1; // config's second layer: Sidecar's own recording format
    if (screen != Screen::Home)
        return 1;
    // HOME's home, gate, dynamics and sends tabs have a second layer of encoders.
    return tab == 0 || tab == 2 || tab == 3 || tab == 5 ? 2 : 1;
}

void Display::showScreen (Screen s, int t)
{
    if (s == Screen::MuteGroups && screen == Screen::MuteGroups)
    {
        // MUTE GRP again: back to the page it covered.
        const int back = underTab;
        screen = underScreen;
        tab = 0;
        showTab (back);
        return;
    }
    if (s == Screen::MuteGroups)
    {
        underScreen = screen;
        underTab = tab;
        screen = s;
        tab = 0;
        showTab (underTab);
        return;
    }
    screen = s;
    tab = 0;
    showTab (t);
}

void Display::pressUtility()
{
    // Manual 7.9: UTILITY is a shortcut into the screen in view. Where the real
    // console has none, Sidecar's copy and init pages show.
    switch (screen)
    {
        case Screen::Home:    showScreen (Screen::Library, 0); break;
        case Screen::Routing: showScreen (Screen::Library, 2); break;
        case Screen::Effects: showScreen (Screen::Library, 1); break;
        case Screen::Usb:     showScreen (Screen::Usb, 1); break;
        case Screen::Scenes:  showScreen (Screen::Scenes, 1); break;
        default:              showScreen (Screen::Utility); break;
    }
}

void Display::showTab (int t)
{
    (screen == Screen::MuteGroups ? underTab : tab) = juce::jlimit (0, getTabs().size() - 1, t);
    layer = 0;
    const auto shown = screen == Screen::MuteGroups ? underScreen : screen;
    const int shownT = shownTab();
    iqSetup = false;
    initArmedItem = -1;
    netPending.fill (-1);
    pendingBlock.fill (-1);
    cardMode = -1;
    rtaSourcePick = -1;
    // METERS' RTA and the EQ's overlay: analysed while their tab shows.
    updateRta();
    publishScreen();
    if (screen == Screen::Assign && tab > 0 && setAssignSet)
        setAssignSet (tab - 1); // a set's tab shows (and picks) the set
    if (screen == Screen::Assign && tab == 0 && getAssignSet)
        layer = getAssignSet();
    {
        // HOME's graphs: the gate's for a channel, the dynamics' for a channel or an output.
        const bool home = shown == Screen::Home;
        auto* s = selected();
        const bool hasGate = s != nullptr && s->getKind() == StripKind::Channel;
        eqView.setVisible (home && shownT == 4);
        gateView.setVisible (home && shownT == 2 && hasGate);
        dynView.setVisible (home && shownT == 3 && s != nullptr && (hasGate || s->isOutput()));
    }
    assignEncoders();
    for (auto& k : screenKeys)
        k->refresh();
    resized();
    repaint();
}

void Display::setLayer (int l)
{
    layer = juce::jlimit (0, numLayers() - 1, l);
    if (screen == Screen::Assign && tab == 0 && setAssignSet)
        setAssignSet (layer);
    if (geqSlot >= 0 && screen == Screen::Effects && tab == geqSlot + 1)
        showGeqOnFaders (geqSlot, geqFirst); // a dual EQ's other side on the faders
    assignEncoders();
    repaint();
}

void Display::refreshStrip()
{
    auto* s = selected();
    gateView.bind (s != nullptr ? &s->params.gate() : nullptr);
    dynView.bind (s != nullptr ? &s->params.dynamics() : nullptr);
    {
        const bool home = (screen == Screen::MuteGroups ? underScreen : screen) == Screen::Home;
        const bool hasGate = s != nullptr && s->getKind() == StripKind::Channel;
        gateView.setVisible (home && shownTab() == 2 && hasGate);
        dynView.setVisible (home && shownTab() == 3 && s != nullptr && (hasGate || s->isOutput()));
    }
    eqView.refresh();
    updateRta(); // the RTA follows the selection
    // The selection, for a remote (`/-stat/selidx`).
    if (s != nullptr)
        if (const int index = x32Index (*s); index >= 0 && index != publishedSel)
        {
            publishedSel = index;
            int set = 0;
            ctx.engine.getWing().runX32Text ("/-stat/selidx " + juce::String (index), set);
        }
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
    if (encoderCallbacks > 0)
    {
        encodersWanted = true;
        return;
    }
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
        // What the strip has (an X32's aux ins and FX returns: an EQ only; no gate on a bus).
        const bool hasGate = s->getKind() == StripKind::Channel;
        const bool hasDyn = hasGate || s->isOutput();
        const auto stripAddress = x32Address (*s);
        const bool isLr = stripAddress == "/main/st";
        const bool toMains = s->isInput() || s->getKind() == StripKind::Bus;
        // A key filter's frequency, its push switching the filter on (the last
        // type, BANDPASS at first) and off (p.47-48).
        auto keyFreq = [this] (auto& slot)
        {
            return encoder (bindLog ("KEY FREQ", slot.scFreq, 20.0f, 20000.0f, " Hz", 1000.0f), "FILTER",
                            [this, &slot]
                            {
                                if (slot.scFilter.load() != 0)
                                {
                                    lastKeyFilter = slot.scFilter.load();
                                    slot.scFilter = 0;
                                }
                                else
                                    slot.scFilter = juce::jmax (1, lastKeyFilter);
                            },
                            [&slot] { return slot.scFilter.load() != 0; });
        };
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
                    encoders[0] = encoder (gainBinding (*s), linkPush ? "LINK" : "", linkPush, linkOn);
                    if (hasGate)
                        encoders[1] = encoder (bindParam (models::Dynamics, gate.model.load(), gate.values, "thr", "GATE"), "GATE", toggle (gate.on), isOn (gate.on));
                    encoders[2] = encoder (orderBit (2, "INSERT"), "INSERT", toggle (p.preInsert().on), isOn (p.preInsert().on));
                    encoders[3] = encoder (hasDyn ? orderBit (1, "DYN POS") : KnobBinding(), "EQ", toggle (eq.on), isOn (eq.on));
                    if (hasDyn)
                        encoders[4] = encoder (bindParam (models::Dynamics, dyn.model.load(), dyn.values, "thr", "COMP"), "COMP", toggle (dyn.on), isOn (dyn.on));
                    // PAN and the LR assign: inputs and mix buses; the LR's own pan is its balance; a matrix and the M/C have neither.
                    if (toMains)
                        encoders[5] = encoder (bindPan ("PAN", p.pan), "LR", toggle (s->mains[0].on), isOn (s->mains[0].on));
                    else if (isLr)
                        encoders[5] = encoder (bindPan ("PAN", p.pan));
                }
                else
                {
                    // 48V on the strip's input; a DCA and a mute group picked, PUSH
                    // assigns the strip to it (p.47).
                    const int input = p.inputLeft.load();
                    const auto strips = recallStrips();
                    const int numStrips = (int) strips.size();
                    const int at = (int) (std::find (strips.begin(), strips.end(), s) - strips.begin());
                    KnobBinding pickStrip;
                    pickStrip.label = "SELECT";
                    pickStrip.get = [numStrips, at] { return numStrips > 1 && at < numStrips ? (float) at / (float) (numStrips - 1) : 0.0f; };
                    pickStrip.set = [this, strips, numStrips] (float v)
                    {
                        if (numStrips > 0 && ctx.selectStrip)
                            ctx.selectStrip (strips[(size_t) juce::roundToInt (v * (float) (numStrips - 1))]);
                    };
                    pickStrip.text = [s] { return s->getLabel(); };
                    pickStrip.steps = (float) juce::jmax (2, numStrips);
                    encoders[0] = encoder (pickStrip);
                    if (auto* src = input >= 0 && input < 128 ? engine.getSource (input) : nullptr)
                    {
                        encoders[0].pushLabel = "48V";
                        encoders[0].push = toggle (src->phantom);
                        encoders[0].pushOn = isOn (src->phantom);
                    }
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
                encoders[0] = encoder (gainBinding (*s), linkPush ? "LINK" : "", linkPush, linkOn);
                if (hasGate) // the low cut is a channel's
                    encoders[1] = encoder (bindFreq ("LOW CUT", p.filter().lowCutFreq, 20.0f, 400.0f, 20.0f), "LOW CUT", toggle (p.filter().lowCut), isOn (p.filter().lowCut));
                // SOURCE (p.45): turned through the X32's list, SELECT (the push) patches it.
                const auto address = stripAddress;
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
                if (hasGate) // and so is the delay (an output's is on ROUTING)
                    encoders[3] = encoder (bindLinear ("DELAY", p.input.delay, 0.3f, 500.0f, " ms", 1, 0.3f), "DELAY", toggle (p.input.delayOn), isOn (p.input.delayOn));
                encoders[4] = encoder (orderBit (2, "INSERT POS"), "INSERT", toggle (p.preInsert().on), isOn (p.preInsert().on));
                // The insert's slot (p.45): turned, then CONNECT (the push) puts it in.
                if (address.startsWith ("/ch/") || address.startsWith ("/bus/") || address.startsWith ("/mtx/"))
                {
                    static const auto& inserts = kInserts;
                    const auto path = address + "/insert/sel";
                    const auto shown = [this, path, s]
                    {
                        return pendingInsert >= 0 && pendingSourceStrip == s ? pendingInsert : juce::jmax (0, inserts.indexOf (x32Value (path)));
                    };
                    KnobBinding slot;
                    slot.label = "INSERT";
                    slot.steps = (float) inserts.size();
                    slot.get = [shown] { return (float) shown() / (float) (inserts.size() - 1); };
                    slot.set = [this, s] (float v)
                    {
                        pendingInsert = juce::jlimit (0, inserts.size() - 1, juce::roundToInt (v * (float) (inserts.size() - 1)));
                        pendingSourceStrip = s;
                        repaint();
                    };
                    slot.text = [shown] { return inserts[shown()]; };
                    encoders[5] = encoder (slot, "CONNECT", [this, path, s]
                    {
                        if (pendingInsert < 0 || pendingSourceStrip != s)
                            return;
                        int set = 0;
                        ctx.engine.getWing().runX32Text (path + " " + inserts[pendingInsert], set);
                        pendingInsert = -1;
                        repaint();
                    }, [this, s] { return pendingInsert >= 0 && pendingSourceStrip == s; });
                }
                break;
            }
            case 2: // gate
                if (! hasGate)
                    break; // a channel's only
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
                    // Page 2 (p.47): 1 and 2 as page 1; 4 the key filter's frequency
                    // (push: FILTER), 5 its type (push: KEY SOLO), 6 the key source.
                    encoders[0] = encoder (bindParam (models::Dynamics, gate.model.load(), gate.values, "thr", "THRESHOLD"), "GATE", toggle (gate.on), isOn (gate.on));
                    encoders[1] = encoder (bindChoice ("MODE", gate.model, { "EXP2", "EXP3", "EXP4", "GATE", "DUCK" }, 3));
                    encoders[3] = keyFreq (gate);
                    const auto [solo, soloOn] = keySolo (ld::kListenGateKey);
                    encoders[4] = encoder (bindChoice ("FILTER", gate.scFilter, keyFilters), "KEY SOLO", solo, soloOn);
                    encoders[5] = encoder (keySource (gate.scSource));
                }
                break;
            case 3: // dyn
                if (! hasDyn)
                    break;
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
                    // Page 2 (p.48): 2 MIX; 3 KNEE (push: before / after the EQ); 4 the
                    // key filter's frequency (push: FILTER); 5 its type (push: KEY
                    // SOLO); 6 the key source.
                    encoders[1] = encoder (bindLinear ("MIX", dyn.mix, 0.0f, 100.0f, " %", 0, 100.0f));
                    encoders[2] = encoder (bindParam (models::Dynamics, dyn.model.load(), dyn.values, "knee", "KNEE"), "PRE EQ",
                                           [&p] { p.processOrder = p.processOrder.load() ^ 1; }, [&p] { return (p.processOrder.load() & 1) != 0; });
                    encoders[3] = keyFreq (dyn);
                    const auto [solo, soloOn] = keySolo (ld::kListenDynKey);
                    encoders[4] = encoder (bindChoice ("FILTER", dyn.scFilter, keyFilters), "KEY SOLO", solo, soloOn);
                    encoders[5] = encoder (keySource (dyn.scSource));
                }
                break;
            case 4: // eq
            {
                const int bands = s->isOutput() ? 6 : 4;
                eqBand = juce::jlimit (0, bands - 1, eqBand);
                const auto n = juce::String (eqBand + 1);
                if (hasGate)
                    encoders[0] = encoder (bindFreq ("LOW CUT", p.filter().lowCutFreq, 20.0f, 400.0f, 20.0f), "LOW CUT", toggle (p.filter().lowCut), isOn (p.filter().lowCut));
                // Pushes (p.45): the RTA overlay as a bar graph or a spectrograph, and
                // before or after the EQ (`/-stat/rtamodeeq`, `rtaeqpre`); RESET a band, RESET all.
                // The band, for a remote (`/-stat/eqband`: Low, Low2, Lo-Mid, Hi-Mid, High2, High).
                {
                    static constexpr int four[] = { 0, 2, 3, 5 };
                    const int band = bands == 4 ? four[eqBand] : eqBand;
                    if (band != publishedBand)
                    {
                        publishedBand = band;
                        int set = 0;
                        ctx.engine.getWing().runX32Text ("/-stat/eqband " + juce::String (band), set);
                    }
                }
                const auto resetBand = [&eq, eqTable] (int b)
                {
                    const auto& m = models::model (eqTable, eq.model.load());
                    const auto prefix = juce::String (b + 1);
                    for (int k = 0; k < (int) m.params.size(); ++k)
                        if (m.params[(size_t) k].id.startsWith (prefix) && m.params[(size_t) k].id.length() == prefix.length() + 1)
                            eq.values[k] = m.params[(size_t) k].defaultValue;
                };
                encoders[1] = encoder (bindParam (eqTable, eq.model.load(), eq.values, n + "f", "FREQ"), "RTA SPEC",
                                       [this] { setRtaOption (1, x32Value ("/-stat/rtamodeeq") != "SPEC"); }, [this] { return x32Value ("/-stat/rtamodeeq") == "SPEC"; });
                encoders[2] = encoder (bindParam (eqTable, eq.model.load(), eq.values, n + "g", "GAIN"), "RTA PRE",
                                       [this] { setRtaOption (0, ! x32On ("/-stat/rtaeqpre")); }, [this] { return x32On ("/-stat/rtaeqpre"); });
                encoders[3] = encoder (bindParam (eqTable, eq.model.load(), eq.values, n + "q", "Q"), "RESET", [this, resetBand] { resetBand (eqBand); });
                encoders[4] = encoder (bindParam (eqTable, eq.model.load(), eq.values, n + "t", "TYPE"), "RESET ALL", [bands, resetBand] { for (int b = 0; b < bands; ++b) resetBand (b); });
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
                if (s->getKind() == StripKind::Bus || s->getKind() == StripKind::Main)
                {
                    // A mix bus's and a main's sends go to the matrices: 1-4, then 3-6.
                    const int numMtx = juce::jmin (6, (int) engine.getMatrices().size());
                    sendBank = juce::jlimit (0, 1, sendBank);
                    const int firstMtx = sendBank > 0 ? juce::jmax (0, numMtx - 4) : 0;
                    for (int i = 0; i < 4 && firstMtx + i < numMtx; ++i)
                    {
                        auto& send = s->matrix[(size_t) (firstMtx + i)];
                        encoders[(size_t) i] = encoder (bindFader ("MTX " + juce::String (firstMtx + i + 1), send.levelDb), "MUTE", [&send] { send.on = ! send.on.load(); },
                                                        [&send] { return ! send.on.load(); });
                    }
                    if (numMtx > 4)
                        encoders[5] = encoder ({}, "NEXT 4", [this] { sendBank = 1 - juce::jlimit (0, 1, sendBank); assignEncoders(); });
                    break;
                }
                const bool hasSends = s->isInput();
                const auto addressOf = x32Address (*s);
                const int first = 2 * sendBank;
                if (layer == 0)
                    for (int i = 0; i < 4 && hasSends; ++i)
                    {
                        const int slot = first + i;
                        if (slot >= (int) engine.getBuses().size())
                            break;
                        auto& send = s->sends[(size_t) slot];
                        encoders[(size_t) (i + 2)] = encoder (bindFader ("Level " + juce::String (i + 1), send.levelDb), "MUTE", [&send] { send.enabled = ! send.enabled.load(); },
                                                        [&send] { return ! send.enabled.load(); });
                    }
                else if (addressOf.startsWith ("/ch/") || addressOf.startsWith ("/auxin/") || addressOf.startsWith ("/fxrtn/"))
                {
                    // Page 2 (p.46): each pair's tap point, on encoders 1 and 3.
                    static const juce::StringArray taps { "IN/LC", "<-EQ", "EQ->", "PRE", "POST", "GRP" };
                    for (int k = 0; k < 2; ++k)
                    {
                        const int bus = first + 2 * k + 1;
                        if (bus <= (int) engine.getBuses().size())
                            encoders[(size_t) (2 * k + 2)] = encoder (enumKnob ("TAP " + juce::String (bus) + "/" + juce::String (bus + 1),
                                                                            addressOf + "/mix/" + juce::String (bus).paddedLeft ('0', 2) + "/type", taps));
                    }
                }
                KnobBinding bank;
                bank.label = "Scroll";
                bank.get = [this] { return (float) sendBank / 6.0f; };
                bank.set = [this] (float v)
                {
                    const int b = juce::roundToInt (v * 6.0f);
                    if (b != sendBank)
                    {
                        sendBank = b;
                        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Display> (this)]
                                                         { if (safe != nullptr) safe->assignEncoders(); });
                    }
                };
                bank.text = [this] { return juce::String (2 * sendBank + 1) + ".." + juce::String (2 * sendBank + 4); };
                bank.steps = 7.0f;
                encoders[0] = encoder (bank, "JUMP +4", [this] { sendBank = (sendBank + 2) % 7; assignEncoders(); });
                break;
            }
            case 6: // main
                if (toMains)
                    encoders[0] = encoder (bindPan ("Pan", p.pan), "LR", toggle (s->mains[0].on), isOn (s->mains[0].on));
                else if (isLr)
                    encoders[0] = encoder (bindPan ("PAN", p.pan));
                if (hasMc && toMains)
                    encoders[1] = encoder (bindFader ("Level", s->mains[1].levelDb), "SUB", toggle (s->mains[1].on), isOn (s->mains[1].on));
                encoders[4] = encoder ({}, "SOLO", toggle (p.solo), isOn (p.solo));
                encoders[5] = encoder (faderOf(), "MUTE", toggle (p.mute), isOn (p.mute));
                break;
            default:
                break;
        }
    }
    else if (screen == Screen::Setup && tab == 0)
    {
        // GLOBAL (p.55): encoder 2 the sample rate, pushed to confirm (the audio
        // interface's rates; it is the clock). The system controls, initialize
        // and the brightness are the console's own, the time and date the computer's.
        // Encoder 1 is Sidecar's: the surface, an X32 or an X32 COMPACT.
        if (getModel && setModel)
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
            encoders[0] = encoder (m, "GO", [this] { setModel (1 - getModel()); }, [] { return false; });
        }
        assignSetupGlobalEncoders();
        auto* device = ctx.deviceManager.getCurrentAudioDevice();
        const auto rates = device != nullptr ? device->getAvailableSampleRates() : juce::Array<double>();
        if (! rates.isEmpty())
        {
            const int n = rates.size();
            if (! juce::isPositiveAndBelow (globalRate, n))
                globalRate = juce::jmax (0, rates.indexOf (device->getCurrentSampleRate()));
            KnobBinding rate;
            rate.label = "Select";
            rate.get = [this, n] { return n > 1 ? (float) globalRate / (float) (n - 1) : 0.0f; };
            rate.set = [this, n] (float v) { globalRate = juce::roundToInt (v * (float) (n - 1)); repaint(); };
            rate.text = [this, rates] { return juce::String (rates[globalRate] / 1000.0, 1); };
            rate.steps = (float) juce::jmax (2, n);
            encoders[1] = encoder (rate, "SET", [this, rates]
            {
                auto setup = ctx.deviceManager.getAudioDeviceSetup();
                setup.sampleRate = rates[globalRate];
                ctx.deviceManager.setAudioDeviceSetup (setup, true);
                // The X32's own two rates, for a remote (`/-prefs/clockrate`).
                if (const int rate = juce::roundToInt (rates[globalRate]); rate == 48000 || rate == 44100)
                    x32Run (rate == 48000 ? "/-prefs/clockrate 48K" : "/-prefs/clockrate 44K1");
                assignEncoders();
                repaint();
            }, [this, rates] { auto* d = ctx.deviceManager.getCurrentAudioDevice(); return d != nullptr && d->getCurrentSampleRate() == rates[globalRate]; });
        }
    }
    else if (screen == Screen::Setup && tab == 1)
    {
        // CONFIG (p.55): encoder 1 the preferences, 4 the link preferences
        // (what a channel link shares), each pushed on or off; 5 a scheme for
        // the inputs' bus sends, pushed to apply (the schemes are Sidecar's).
        auto run = [this] (const juce::String& text) { int set = 0; ctx.engine.getWing().runX32Text (text, set); repaint(); };
        const auto list = [this, run] (const char* label, int& index, const std::vector<ConfigItem>& items)
        {
            const int n = (int) items.size();
            index = juce::jlimit (0, n - 1, index);
            KnobBinding k;
            k.label = label;
            k.get = [&index, n] { return (float) index / (float) (n - 1); };
            k.set = [this, &index, n] (float v) { index = juce::roundToInt (v * (float) (n - 1)); assignEncoders(); };
            k.text = [&index, &items] { return juce::String (items[(size_t) index].name); };
            k.steps = (float) n;
            const auto item = items[(size_t) index];
            const auto isOn = [this, item] { return x32Value (item.path) == item.on; };
            return encoder (k, "TOGGLE", [isOn, run, item] { run (juce::String (item.path) + " " + (isOn() ? item.off : item.on)); }, isOn);
        };
        encoders[0] = list ("PREFERENCE", configPref, configPrefs());
        encoders[3] = list ("LINK", configLink, configLinks());
        // 6 what PREV, NEXT and GO step through (`/-prefs/show_control`; Sidecar's shows hold scenes).
        static const juce::StringArray showControl { "CUES", "SCENES", "SNIPPETS" };
        encoders[5] = encoder (enumKnob ("SHOW CONTROL", "/-prefs/show_control", showControl));
        // 3 the word clock: the audio interface's own (no AES50 or card to lock to).
        // The interface's clock is the only one: shown from the device, nothing to choose.
        encoders[2] = encoder (readout ("SYNC", [] { return juce::String ("Internal"); }));
        KnobBinding bus;
        bus.label = "BUS CONFIG";
        bus.get = [this] { return (float) configBus / 4.0f; };
        bus.set = [this] (float v) { configBus = juce::roundToInt (v * 4.0f); repaint(); };
        bus.text = [this] { return juce::String (kBusSchemes[configBus].name); };
        bus.steps = 5.0f;
        encoders[4] = encoder (bus, "APPLY", [this, run]
        {
            // Every input's (channels, aux in, FX returns) send pairs: pre, post,
            // then subgroup, as the manual's five bus configurations give them.
            const auto& sch = kBusSchemes[configBus];
            const std::pair<const char*, int> inputs[] = { { "ch", 32 }, { "auxin", 8 }, { "fxrtn", 8 } };
            for (const auto& [node, count] : inputs)
                for (int i = 1; i <= count; ++i)
                    for (int send = 1; send <= 16; send += 2)
                    {
                        const int bus = send - 1;
                        run ("/" + juce::String (node) + "/" + juce::String (i).paddedLeft ('0', 2) + "/mix/" + juce::String (send).paddedLeft ('0', 2)
                             + "/type " + (bus < sch.pre ? "PRE" : bus < sch.pre + sch.post ? "POST" : "GRP"));
                    }
        });
    }
    else if (screen == Screen::Setup && tab == 4)
    {
        // SCRIBBLE STRIP (p.56): five columns, an encoder each: the strip, its
        // colour, its icon, a name from the list (pushed to give it), its name
        // (EDIT asks for it).
        const auto strips = recallStrips();
        // The strips, then the DCAs (p.56), which can't be selected: kept here.
        const int n = (int) strips.size(), total = n + engine.getNumDcas();
        const int at = scribbleDca >= 0 ? n + scribbleDca : (int) (std::find (strips.begin(), strips.end(), s) - strips.begin());
        KnobBinding pick;
        pick.label = "SELECT";
        pick.get = [total, at] { return total > 1 && at < total ? (float) at / (float) (total - 1) : 0.0f; };
        pick.set = [this, strips, n, total] (float v)
        {
            const int to = juce::roundToInt (v * (float) (total - 1));
            scribbleDca = to >= n ? to - n : -1;
            if (to < n && ctx.selectStrip)
                ctx.selectStrip (strips[(size_t) to]);
            assignEncoders();
        };
        pick.text = [this, s] { return scribbleDca >= 0 ? "DCA " + juce::String (scribbleDca + 1) : s != nullptr ? s->getLabel() : juce::String ("-"); };
        pick.steps = (float) juce::jmax (2, total);
        encoders[0] = encoder (pick);
        if (scribbleDca >= 0)
        {
            // A DCA: `/dca/N/config` (name, icon, colour).
            const juce::String node = "/dca/" + juce::String (scribbleDca + 1) + "/config/";
            encoders[1] = encoder (enumKnob ("SELECT", node + "color", scribbleColours()));
            encoders[2] = encoder (intKnob ("SELECT", node + "icon", 1, 74, [] (int v) { return juce::String (v); }));
            const auto& names = scribbleNames();
            scribbleName = juce::jlimit (0, names.size() - 1, scribbleName);
            KnobBinding name;
            name.label = "SELECT";
            name.get = [this, &names] { return (float) scribbleName / (float) (names.size() - 1); };
            name.set = [this, &names] (float v) { scribbleName = juce::roundToInt (v * (float) (names.size() - 1)); repaint(); };
            name.text = [this, &names] { return names[scribbleName]; };
            name.steps = (float) names.size();
            encoders[3] = encoder (name, "SELECT", [this, node, &names] { x32Run (node + "name \"" + names[scribbleName] + "\""); });
            encoders[4] = encoder ({}, "EDIT", [this, node]
            {
                auto* w = new juce::AlertWindow ("NAME", "DCA " + juce::String (scribbleDca + 1) + ":", juce::MessageBoxIconType::NoIcon, this);
                w->addTextEditor ("name", dcaName (scribbleDca));
                w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                juce::Component::SafePointer<Display> safe (this);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, node] (int result)
                {
                    if (result != 0 && safe != nullptr)
                        safe->x32Run (node + "name \"" + w->getTextEditorContents ("name").trim().substring (0, 12) + "\"");
                }), true);
            });
        }
        else if (s != nullptr)
        {
            KnobBinding colour;
            colour.label = "SELECT";
            colour.get = [s] { return (float) (juce::jlimit (1, 16, s->getColourIndex()) - 1) / 15.0f; };
            colour.set = [this, s] (float v) { s->setColourIndex (1 + juce::roundToInt (v * 15.0f)); repaint(); };
            colour.text = [s] { return juce::String (juce::jlimit (1, 16, s->getColourIndex())); };
            colour.steps = 16.0f;
            encoders[1] = encoder (colour);
            const auto icons = wingIconNumbers();
            KnobBinding icon;
            icon.label = "SELECT";
            icon.get = [s, icons]
            {
                const auto found = std::find (icons.begin(), icons.end(), s->params.icon.load());
                return icons.size() > 1 && found != icons.end() ? (float) (found - icons.begin()) / (float) (icons.size() - 1) : 0.0f;
            };
            icon.set = [this, s, icons] (float v)
            {
                if (! icons.empty())
                    s->params.icon = icons[(size_t) juce::roundToInt (v * (float) (icons.size() - 1))];
                repaint();
            };
            icon.text = [s] { const auto iconName = wingIconName (s->params.icon.load()); return iconName.isNotEmpty() ? iconName.toUpperCase() : juce::String ("NONE"); };
            icon.steps = (float) juce::jmax (2, (int) icons.size());
            encoders[2] = encoder (icon);
            const auto& names = scribbleNames();
            scribbleName = juce::jlimit (0, names.size() - 1, scribbleName);
            KnobBinding name;
            name.label = "SELECT";
            name.get = [this, &names] { return (float) scribbleName / (float) (names.size() - 1); };
            name.set = [this, &names] (float v) { scribbleName = juce::roundToInt (v * (float) (names.size() - 1)); repaint(); };
            name.text = [this, &names] { return names[scribbleName]; };
            name.steps = (float) names.size();
            encoders[3] = encoder (name, "SELECT", [this, s, &names] { s->setName (names[scribbleName]); repaint(); });
            encoders[4] = encoder ({}, "EDIT", [this, s] { editStripName (*s); });
        }
    }
    else if (screen == Screen::Setup && tab == 5)
    {
        // PREAMPS (p.56-57): encoder 1 the block of eight inputs, 2-5 the gains
        // of its top four (layer 1) or bottom four (layer 2), pushed for 48V.
        // The blocks: Local 1-8 .. 25-32, AES50 A 1-8 .. 41-48, AES50 B (interface inputs 1-128).
        KnobBinding block;
        block.label = "SELECT";
        block.get = [this] { return (float) preampBlock / 15.0f; };
        block.set = [this] (float v) { preampBlock = juce::roundToInt (v * 15.0f); assignEncoders(); };
        block.text = [this] { return preampBlockName (preampBlock, true); };
        block.steps = 16.0f;
        encoders[0] = encoder (block);
        for (int k = 0; k < 4; ++k)
            if (auto* src = engine.getSource (8 * preampBlock + 4 * layer + k))
            {
                auto b = bindLinear ("GAIN " + juce::String (preampNumber (preampBlock, 4 * layer + k)), src->gainDb, -12.0f, 60.0f, " dB", 1, 0.0f);
                b.steps = 145.0f;
                encoders[(size_t) (k + 1)] = encoder (b, "+48V", toggle (src->phantom), isOn (src->phantom));
            }
        // 6 the options (`/-prefs/haflags`): Lock Stagebox (kept: no stagebox here), and HA
        // gain split for the local inputs and each AES50 port: with it a strip's GAIN is its
        // own trim, and the head amp is set only on this page.
        KnobBinding option;
        option.label = "SELECT";
        option.get = [this] { return (float) haOption / 3.0f; };
        option.set = [this] (float v) { haOption = juce::roundToInt (v * 3.0f); repaint(); };
        option.text = [this] { return haOptions()[haOption]; };
        option.steps = 4.0f;
        const auto flags = [this] { return x32Bits (x32Value ("/-prefs/haflags")); };
        encoders[5] = encoder (option, "SET", [this, flags] { x32Run ("/-prefs/haflags " + juce::String (flags() ^ (1 << haOption))); },
                               [this, flags] { return (flags() & (1 << haOption)) != 0; });
    }
    else if (screen == Screen::Usb && tab == 0)
    {
        // USB (p.62): 1 STOP, 2 PLAY / PAUSE, 3 rewind, 4 fast forward (five
        // seconds a push), 5 REC; 6 the tracks, push to play the one shown.
        auto& usb = engine.getUsbRecorder();
        using P = UsbRecorder::PlayState;
        using R = UsbRecorder::RecState;
        const auto jump = [&usb] (double seconds)
        {
            const auto step = (int64_t) (seconds * juce::jmax (1.0, usb.getSampleRate()));
            usb.seek (juce::jlimit ((int64_t) 0, juce::jmax ((int64_t) 0, usb.getLength()), usb.getPosition() + step));
        };
        encoders[0] = encoder ({}, "STOP", [&usb] { usb.stop(); });
        encoders[1] = encoder ({}, "PLAY", [&usb] { usb.togglePlayPause(); }, [&usb] { return usb.getPlayState() == P::Playing; });
        encoders[2] = encoder ({}, "REW", [jump] { jump (-5.0); });
        encoders[3] = encoder ({}, "FFWD", [jump] { jump (5.0); });
        encoders[4] = encoder ({}, "REC", [&usb] { if (usb.isRecording()) usb.stopRecording(); else usb.record(); },
                               [&usb] { return usb.getRecState() == R::Recording; });
        const int tracks = (int) usb.getQueue().size();
        KnobBinding track;
        track.label = "TRACK";
        track.get = [this, tracks] { return tracks > 1 ? (float) usbTrack / (float) (tracks - 1) : 0.0f; };
        track.set = [this, tracks] (float v) { usbTrack = juce::roundToInt (v * (float) juce::jmax (0, tracks - 1)); repaint(); };
        track.text = [this, &usb]
        {
            const auto queue = usb.getQueue();
            return juce::isPositiveAndBelow (usbTrack, (int) queue.size()) ? queue[(size_t) usbTrack].file.getFileNameWithoutExtension() : juce::String ("-");
        };
        track.steps = (float) juce::jmax (2, tracks);
        encoders[5] = encoder (track, "PLAY", [this, &usb]
        {
            const auto queue = usb.getQueue();
            if (juce::isPositiveAndBelow (usbTrack, (int) queue.size()))
                usb.playEntry (queue[(size_t) usbTrack].id);
        });
    }
    else if (screen == Screen::Usb)
    {
        // CONFIG: what it records.
        auto& usb = engine.getUsbRecorder();
        if (layer == 1)
        {
            encoders[0] = encoder ({}, "4 TRACKS", [&usb] { usb.setRecChannels (usb.getRecChannels() == 4 ? 2 : 4); }, [&usb] { return usb.getRecChannels() == 4; });
            encoders[1] = encoder ({}, "24 BIT", [&usb] { usb.setRecBits (usb.getRecBits() == 24 ? 16 : 24); }, [&usb] { return usb.getRecBits() == 24; });
        }
        else
        {
            // Manual p.62-63: 1 the folder recordings go to; 2-5 the recorder's input;
            // 6 playback folder mode.
            assignUsbConfigEncoders();
            encoders[0] = encoder ({}, "SELECT FOLDER", [this, &usb]
            {
                usbFolderChooser = std::make_unique<juce::FileChooser> ("Folder for recordings", usb.getRecPath());
                usbFolderChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                               [this, &usb] (const juce::FileChooser& fc)
                                               {
                                                   if (fc.getResult().isDirectory())
                                                       usb.setRecPath (fc.getResult());
                                                   repaint();
                                               });
            });
            encoders[5] = encoder ({}, "FOLDER MODE", [this, &usb] { usbFolderMode = ! usbFolderMode; usb.setStopAll (! usbFolderMode); }, [this] { return usbFolderMode; });
        }
    }
    else if (screen == Screen::Setup && tab == 2)
    {
        assignRemoteEncoders();
    }
    else if (screen == Screen::Setup && tab == 3)
    {
        assignNetworkEncoders();
    }
    else if (screen == Screen::Setup && tab == 6)
    {
        // CARD (p.56): the X-USB card's channel configuration, the second encoder, SET by
        // its push (`/-prefs/card/USBmode`, kept for remotes: the audio interface has its own channels).
        static const juce::StringArray brief { "32/32", "16/16", "32/8", "8/32", "8/8", "2/2" };
        const auto current = [this] { return juce::jmax (0, cardModes().indexOf (x32Value ("/-prefs/card/USBmode"))); };
        if (cardMode < 0)
            cardMode = current();
        KnobBinding mode;
        mode.label = "SELECT";
        mode.get = [this] { return (float) cardMode / 5.0f; };
        mode.set = [this] (float v) { cardMode = juce::jlimit (0, 5, juce::roundToInt (v * 5.0f)); repaint(); };
        mode.text = [this] { return brief[juce::jlimit (0, 5, cardMode)]; };
        mode.steps = 6.0f;
        encoders[1] = encoder (mode, "SET", [this] { x32Run ("/-prefs/card/USBmode " + juce::String (juce::jlimit (0, 5, cardMode))); },
                               [this, current] { return current() == cardMode; });
    }
    else if (screen == Screen::Routing)
    {
        assignRoutingEncoders();
    }
    else if (screen == Screen::Effects)
    {
        assignEffectsEncoders();
    }
    else if (screen == Screen::Library)
    {
        assignLibraryPresetEncoders();
    }
    else if (screen == Screen::MuteGroups)
    {
        // Manual 7.8: over the page in view, an encoder for each group; a push
        // mutes or unmutes it.
        for (int g = 0; g < 6; ++g)
        {
            KnobBinding name;
            name.label = "MUTE GROUP " + juce::String (g + 1);
            encoders[(size_t) g] = encoder (name, "MUTE", [this, g] { ctx.engine.muteGroups.set (g, ! ctx.engine.muteGroups.contains (g)); },
                                            [this, g] { return ctx.engine.muteGroups.contains (g); });
        }
    }
    else if (screen == Screen::Assign && tab == 0)
    {
        // HOME (p.63): 1 the next set, 2-5 the set's encoders, 6 EDIT that set.
        encoders[0] = encoder ({}, "NEXT SET", [this]
        {
            if (getAssignSet && setAssignSet)
            {
                setAssignSet ((getAssignSet() + 1) % 3);
                layer = getAssignSet();
                assignEncoders();
                repaint();
            }
        });
        if (assignBinding)
            for (int i = 0; i < 4; ++i)
            {
                auto b = assignBinding (i);
                if (b.isValid())
                    encoders[(size_t) (i + 1)] = encoder (b);
            }
        encoders[5] = encoder ({}, "EDIT", [this] { if (getAssignSet) showTab (getAssignSet() + 1); });
    }
    else if (screen == Screen::Monitor && tab == 0)
    {
        // MONITOR (p.60-61): 1 the monitor's gain; 2 the solo options, push to
        // switch the one shown; 3 the monitor delay, push on; 4 the dim depth,
        // push DIM; 5 the source's trim, push MONO; 6 the source. (PHONES is the
        // panel's knob.) What the engine doesn't do is kept as `/config/solo`.
        const auto run = [this] (const juce::String& text) { int set = 0; ctx.engine.getWing().runX32Text (text, set); repaint(); };
        encoders[0] = encoder (bindFader ("LEVEL", engine.monitorLevelDb), "MUTE", toggle (engine.monitorMute), isOn (engine.monitorMute));
        KnobBinding solo;
        solo.label = "SELECT";
        solo.get = [this] { return (float) soloOption / (float) (soloOptions().size() - 1); };
        solo.set = [this] (float v) { soloOption = juce::roundToInt (v * (float) (soloOptions().size() - 1)); assignEncoders(); repaint(); };
        solo.text = [this] { static const juce::StringArray shortNames { "Excl", "SoloSel", "SelSolo", "Ch AFL", "Bus AFL", "DCA AFL", "Master" }; return shortNames[soloOption]; };
        solo.steps = (float) soloOptions().size();
        encoders[1] = encoder (solo, "SET", [this] { toggleSoloOption (soloOption); }, [this] { return soloOptionOn (soloOption); });
        KnobBinding delay;
        delay.label = "DELAY";
        delay.get = [this] { return (x32Value ("/config/solo/delaytime").getFloatValue() - 0.3f) / 499.7f; };
        delay.set = [run] (float v) { run ("/config/solo/delaytime " + juce::String (0.3f + v * 499.7f, 1)); };
        delay.text = [this] { return x32Value ("/config/solo/delaytime") + " ms"; };
        delay.steps = 4998.0f;
        encoders[2] = encoder (delay, "DELAY", [this, run] { run (juce::String ("/config/solo/delay ") + (x32Value ("/config/solo/delay") == "ON" ? "OFF" : "ON")); },
                               [this] { return x32Value ("/config/solo/delay") == "ON"; });
        KnobBinding dim;
        dim.label = "DIM ATTEN";
        dim.get = [&engine] { return engine.monitorDimDb.load() / 40.0f; };
        dim.set = [&engine, run] (float v)
        {
            engine.monitorDimDb = v * 40.0f;
            run ("/config/solo/dimatt " + juce::String (-v * 40.0f, 0));
        };
        dim.text = [&engine] { return "-" + juce::String (juce::roundToInt (engine.monitorDimDb.load())) + " dB"; };
        dim.steps = 41.0f;
        encoders[3] = encoder (dim, "DIM", toggle (engine.monitorDim), isOn (engine.monitorDim));
        KnobBinding trim;
        trim.label = "SOURCE";
        trim.get = [this] { return (x32Value ("/config/solo/sourcetrim").getFloatValue() + 18.0f) / 36.0f; };
        trim.set = [&engine, run] (float v)
        {
            run ("/config/solo/sourcetrim " + juce::String (-18.0f + v * 36.0f, 1));
            if (engine.speakers.sourceLevelDb.load() > -100.0f)
                engine.speakers.sourceLevelDb = -18.0f + v * 36.0f;
        };
        trim.text = [this] { return x32Value ("/config/solo/sourcetrim") + " dB"; };
        trim.steps = 73.0f;
        encoders[4] = encoder (trim, "MONO", toggle (engine.monitorMono), isOn (engine.monitorMono));
        // 6: the Monitor Source list (p.61), set by the push.
        static const juce::StringArray sourceCodes { "OFF", "LR", "LR+C", "LRPFL", "LRAFL", "AUX56", "AUX78" };
        static const juce::StringArray sourceShort { "OFF", "LR", "LR+C/M", "LR P", "LR A", "A 5/6", "A 7/8" };
        const auto current = [this] { return juce::jmax (0, sourceCodes.indexOf (x32Value ("/config/solo/source"))); };
        if (monitorSourcePick < 0)
            monitorSourcePick = current();
        KnobBinding source;
        source.label = "SELECT";
        source.get = [this] { return (float) monitorSourcePick / 6.0f; };
        source.set = [this] (float v) { monitorSourcePick = juce::jlimit (0, 6, juce::roundToInt (v * 6.0f)); repaint(); };
        source.text = [this] { return sourceShort[monitorSourcePick]; };
        source.steps = 7.0f;
        encoders[5] = encoder (source, "SET", [this, &engine, run]
        {
            // The list's choices reach the engine: LR; LR+C/M as LR with the mono bus (Main 2)
            // added on the path's DIRECT IN; LR P and LR A as LR kept audible under a solo
            // (it replaces LR otherwise), with the channel solo's tap (PFL or AFL) set to match;
            // the two aux pairs.
            run ("/config/solo/source " + sourceCodes[monitorSourcePick]);
            const float trimDb = x32Value ("/config/solo/sourcetrim").getFloatValue();
            engine.speakers.sourceLevelDb = monitorSourcePick == 0 ? -144.0f : trimDb;
            engine.speakers.sourceMixDb = monitorSourcePick == 3 || monitorSourcePick == 4 ? trimDb : -144.0f;
            auto& direct = engine.monitorPath (false);
            const int mono = MixerEngine::directSourceIndex (StripKind::Main, 1);
            if (monitorSourcePick == 2)
            {
                direct.directSource = mono;
                direct.directLevelDb = 0.0f;
            }
            else if (direct.directSource.load() == mono)
                direct.directSource = -1;
            if (monitorSourcePick == 3 || monitorSourcePick == 4)
            {
                engine.soloAfl = monitorSourcePick == 4;
                run (juce::String ("/config/solo/chmode ") + (monitorSourcePick == 4 ? "AFL" : "PFL"));
            }
            if (monitorSourcePick == 5 || monitorSourcePick == 6)
                engine.speakers.source = MixerEngine::monitorSourceIndex (StripKind::Aux, monitorSourcePick == 5 ? 4 : 6);
            else if (monitorSourcePick != 0)
                engine.speakers.source = MixerEngine::monitorSourceIndex (StripKind::Main, 0);
        }, [this, current] { return current() == monitorSourcePick; });
    }
    else if (screen == Screen::Monitor && tab < 3)
    {
        // TALKBACK A or B (p.61): 1 push talk; 2 push the external microphone;
        // 3 the talk gain, push AUTO DIM; 4 the destinations (MIX BUS 1-16, LR,
        // C/M), push to switch the one shown; 5 push LATCH. (6, the lamp, is the
        // console's lamp socket: none here.) `/config/talk`.
        const auto run = [this] (const juce::String& text) { int set = 0; ctx.engine.getWing().runX32Text (text, set); repaint(); };
        const juce::String node = tab == 1 ? "/config/talk/A" : "/config/talk/B";
        auto& path = engine.talk.paths[(size_t) (tab - 1)];
        const auto flip = [this, run] (const juce::String& address) { run (address + (x32Value (address) == "ON" ? " OFF" : " ON")); };
        const auto on = [this] (const juce::String& address) { return [this, address] { return x32Value (address) == "ON"; }; };
        encoders[0] = encoder ({}, tab == 1 ? "TALK A" : "TALK B", toggle (path.on), isOn (path.on));
        encoders[2] = encoder (bindFader ("TALK", engine.talk.levelDb), "AUTO DIM", [flip, node] { flip (node + "/dim"); }, on (node + "/dim"));
        KnobBinding dest;
        dest.label = "DEST";
        dest.get = [this] { return (float) talkBus / 17.0f; };
        dest.set = [this] (float v) { talkBus = juce::roundToInt (v * 17.0f); assignEncoders(); };
        dest.text = [this] { return talkBus < 16 ? "BUS " + juce::String (talkBus + 1) : juce::String (talkBus == 16 ? "LR" : "C/M"); };
        dest.steps = 18.0f;
        const auto bit = [this] { return 1 << talkBus; };
        encoders[3] = encoder (dest, "ASSIGN",
                               [this, run, node, bit] { run (node + "/destmap " + juce::String (x32Bits (x32Value (node + "/destmap")) ^ bit())); },
                               [this, node, bit] { return (x32Bits (x32Value (node + "/destmap")) & bit()) != 0; });
        encoders[4] = encoder ({}, "LATCH", [flip, node] { flip (node + "/latch"); }, on (node + "/latch"));
        // 2's turn: the channel the talkback microphone comes in on (Sidecar's: an X32
        // has its own talkback microphone and socket), OFF or an input, aux in or FX return.
        auto strips = std::make_shared<std::vector<Strip*>>();
        for (auto* list : { &engine.getChannels(), &engine.getAux() })
            for (auto& st : *list)
                strips->push_back (st.get());
        const int count = (int) strips->size() + 1;
        KnobBinding channel;
        channel.label = "TALK CH";
        channel.steps = (float) count;
        const auto current = [&engine, strips]
        {
            const auto it = std::find (strips->begin(), strips->end(), engine.getTalkStrip());
            return it == strips->end() ? 0 : (int) (it - strips->begin()) + 1;
        };
        channel.get = [current, count] { return (float) current() / (float) (count - 1); };
        channel.set = [this, &engine, strips, count] (float v)
        {
            const int i = juce::roundToInt (v * (float) (count - 1));
            engine.setTalkStrip (i > 0 ? (*strips)[(size_t) i - 1] : nullptr);
            repaint();
        };
        channel.text = [current, strips]
        {
            const int i = current();
            return i == 0 ? juce::String ("OFF") : (*strips)[(size_t) i - 1]->getLabel();
        };
        encoders[1] = encoder (channel, "EXT MIC", [this, run] { run (juce::String ("/config/talk/source ") + (x32Value ("/config/talk/source") == "EXT" ? "INT" : "EXT")); },
                               [this] { return x32Value ("/config/talk/source") == "EXT"; });
        // 6: the lamp's brightness, pushed on and off (`/-prefs/lamp`, `lampon`: kept, a
        // computer has no lamp socket).
        KnobBinding lamp;
        lamp.label = "LAMP";
        lamp.get = [this] { return juce::jlimit (0.0f, 1.0f, (x32Value ("/-prefs/lamp").getFloatValue() - 10.0f) / 90.0f); };
        lamp.set = [this] (float v) { x32Run ("/-prefs/lamp " + juce::String (10 + 10 * juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 9.0f))); };
        lamp.text = [this] { return x32Value ("/-prefs/lamp") + "%"; };
        lamp.steps = 10.0f;
        encoders[5] = encoder (lamp, "LAMP", [this] { x32Run (juce::String ("/-prefs/lampon ") + (x32On ("/-prefs/lampon") ? "OFF" : "ON")); },
                               [this] { return x32On ("/-prefs/lampon"); });
    }
    else if (screen == Screen::Monitor)
    {
        // OSCILLATOR (p.62): its level (push: on / off), F1, F2 (push: switch
        // between the two), its type and its destination; `/config/osc` and
        // `/-stat/osc/on`, which the engine's console oscillator plays.
        const auto run = [this] (const juce::String& text) { int set = 0; ctx.engine.getWing().runX32Text (text, set); repaint(); };
        const auto on = [this] { return x32Value ("/-stat/osc/on") == "ON"; };
        KnobBinding level;
        level.label = "LEVEL";
        level.get = [this] { return fader::dbToPos (x32Level (x32Value ("/config/osc/level"))); };
        level.set = [run] (float v) { run ("/config/osc/level " + juce::String (fader::posToDb (v), 1)); };
        level.text = [this] { return x32Value ("/config/osc/level") + " dB"; };
        level.steps = 161.0f;
        encoders[0] = encoder (level, "OSC ON", [run, on] { run (juce::String ("/-stat/osc/on ") + (on() ? "OFF" : "ON")); }, on);
        for (int k = 0; k < 2; ++k)
        {
            const juce::String path = k == 0 ? "/config/osc/f1" : "/config/osc/f2";
            KnobBinding f;
            f.label = k == 0 ? "F1" : "F2";
            f.get = [this, path] { return std::log (x32Hz (x32Value (path)) / 20.0f) / std::log (1000.0f); };
            f.set = [run, path] (float v) { run (path + " " + juce::String (20.0f * std::pow (1000.0f, v), 1)); };
            f.text = [this, path] { return x32Value (path) + " Hz"; };
            f.steps = 121.0f;
            if (k == 0)
                encoders[1] = encoder (f);
            else
                encoders[2] = encoder (f, "F1 / F2", [this, run] { run (x32Value ("/config/osc/fsel") == "F1" ? "/config/osc/fsel F2" : "/config/osc/fsel F1"); },
                                       [this] { return x32Value ("/config/osc/fsel") == "F2"; });
        }
        static const juce::StringArray types { "SINE", "PINK", "WHITE" };
        encoders[3] = encoder (enumKnob ("TYPE", "/config/osc/type", types));
        KnobBinding dest;
        dest.label = "DEST";
        dest.get = [this] { return (float) x32Value ("/config/osc/dest").getIntValue() / 25.0f; };
        dest.set = [run] (float v) { run ("/config/osc/dest " + juce::String (juce::roundToInt (v * 25.0f))); };
        dest.text = [this] { return x32OscDestination (x32Value ("/config/osc/dest").getIntValue()); };
        dest.steps = 26.0f;
        encoders[5] = encoder (dest);
    }
    else if (screen == Screen::Meters && tab == 5)
    {
        assignAutomixEncoders();
    }
    else if (screen == Screen::Meters && tab == 4)
    {
        assignRtaEncoders();
    }
    else if (screen == Screen::Scenes)
    {
        assignScenesEncoders();
    }
    else if (screen == Screen::Assign && assignCode && setAssignCode)
    {
        static const std::tuple<int, int, const char*> pages[] { { 0, 0, "HOME" }, { 0, 1, "CONFIG" }, { 0, 2, "GATE" }, { 0, 3, "DYN" }, { 0, 4, "EQ" }, { 0, 5, "SENDS" }, { 0, 6, "MAIN" },
                                                                 { 1, 0, "METERS" }, { 2, 0, "ROUTING" }, { 3, 0, "SETUP" }, { 4, 0, "LIBRARY" }, { 5, 0, "EFFECTS" } };
        static constexpr int numPages = 12;
        // A set's tab (p.64): the control, what kind of thing it does, its
        // target (and a send's bus or an effect's parameter); a change writes
        // the control's code. A code of another kind (MIDI...) shows as itself.
        const bool button = assignControl >= 4;
        const juce::StringArray kinds = button ? juce::StringArray { "-", "MUTE", "INSERT", "MUTE GRP", "SCENE GO", "SCENE", "PREV", "NEXT", "PAGE" }
                                               : juce::StringArray { "-", "FADER", "PAN", "SEND", "FX PARAM", "SELECTED" };
        const auto code = assignCode (assignControl);
        const auto two = [] (int v) { return juce::String (v).paddedLeft ('0', 2); };
        const int xx = code.substring (1, 3).getIntValue();
        int kind = 0, target = 0, second = 0;
        if (code.isNotEmpty() && code != "-")
        {
            kind = -1;
            if (button)
            {
                if (code[0] == 'O')
                {
                    kind = xx >= 80 ? 3 : 1;
                    target = xx >= 80 ? xx - 80 : xx;
                }
                else if (code[0] == 'I')
                {
                    kind = 2;
                    target = xx;
                }
                else if (code == "S93" || code == "S90" || code == "S91")
                    kind = code == "S93" ? 4 : code == "S90" ? 6 : 7;
                else if (code.startsWith ("S0"))
                {
                    kind = 5;
                    target = code.substring (2).getIntValue();
                }
                else if (code[0] == 'P')
                {
                    kind = 8;
                    const int y = code.substring (3, 4).getIntValue(), z = code.substring (4, 5).getIntValue();
                    for (int k = 0; k < numPages; ++k)
                        if (std::get<0> (pages[k]) == y && std::get<1> (pages[k]) == z)
                            target = k;
                }
            }
            else
            {
                switch (code[0])
                {
                    case 'F': kind = 1; target = xx; break;
                    case 'P': kind = 2; target = xx; break;
                    case 'S': kind = 3; target = xx; second = code.substring (3, 5).getIntValue(); break;
                    case 'X': kind = 4; target = code.substring (1, 2).getIntValue(); second = code.substring (2, 4).getIntValue(); break;
                    case 'D': kind = 5; target = code[1] == '@' ? 0 : (int) (code[1] - 'A') + 1; break;
                    default: break;
                }
            }
        }
        const auto make = [button, two] (int k, int t, int s) -> juce::String
        {
            if (button)
                switch (k)
                {
                    case 1: return "O" + two (t);
                    case 2: return "I" + two (t);
                    case 3: return "O" + two (80 + juce::jlimit (0, 5, t));
                    case 4: return "S93";
                    case 5: return "S0" + two (juce::jlimit (0, 99, t));
                    case 6: return "S90";
                    case 7: return "S91";
                    case 8: return "P99" + juce::String (std::get<0> (pages[juce::jlimit (0, numPages - 1, t)])) + juce::String (std::get<1> (pages[juce::jlimit (0, numPages - 1, t)]));
                    default: return "-";
                }
            switch (k)
            {
                case 1: return "F" + two (t);
                case 2: return "P" + two (t);
                case 3: return "S" + two (t) + two (s);
                case 4: return "X" + juce::String (juce::jlimit (0, 7, t)) + two (juce::jlimit (0, 63, s));
                case 5: return t <= 0 ? juce::String ("D@") : "D" + juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, 11, t - 1)));
                default: return "-";
            }
        };
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
        // 1 the set's colour (the section's LCDs have one): dimmed; 2 the control,
        // 3 its type, 4 and 5 its target (p.64).
        {
            // The set's LCD colour (`/config/userctrl/A/color`): the eight colours turned, the
            // push inverts text and background.
            const juce::String colourPath = "/config/userctrl/" + juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, 2, tab - 1))) + "/color";
            const auto current = [this, colourPath] { return juce::jmax (0, scribbleColours().indexOf (x32Value (colourPath))); };
            KnobBinding colour;
            colour.label = "SET COLOR";
            colour.get = [current] { return (float) (current() % 8) / 7.0f; };
            colour.set = [this, colourPath, current] (float v)
            {
                x32Run (colourPath + " " + scribbleColours()[(current() / 8) * 8 + juce::jlimit (0, 7, juce::roundToInt (v * 7.0f))]);
            };
            colour.text = [current] { return scribbleColours()[current()]; };
            colour.steps = 8.0f;
            encoders[0] = encoder (colour, "INVERT", [this, colourPath, current] { x32Run (colourPath + " " + scribbleColours()[(current() + 8) % 16]); },
                                   [current] { return current() >= 8; });
        }
        encoders[1] = encoder (picker ("CONTROL", assignControl, 12, [] (int v) { return v < 4 ? "ENC " + juce::String (v + 1) : "KEY " + juce::String (v + 1); },
                                       [this] (int v) { assignControl = v; assignEncoders(); }));
        const auto shown = x32::AssignSection::describe (code, button);
        encoders[2] = encoder (picker ("TYPE", juce::jmax (0, kind), kinds.size(), [kinds, kind, shown] (int v) { return kind < 0 && v == 0 ? shown : kinds[v]; },
                                       [this, make, target, second] (int v) { setAssignCode (assignControl, make (v, juce::jmax (0, target), second)); assignEncoders(); }));
        // The target: a strip (or a DCA's fader), a mute group, a scene, an FX slot, the selected channel's control.
        int count = 0;
        std::function<juce::String (int)> text;
        const auto strip = [] (int v) { return x32::AssignSection::describe ("F" + juce::String (v).paddedLeft ('0', 2), false).fromFirstOccurrenceOf ("FDR ", false, false); };
        if (button)
        {
            if (kind == 1 || kind == 2)
            {
                count = 72;
                text = strip;
            }
            else if (kind == 3)
            {
                count = 6;
                text = [] (int v) { return "MUTE G" + juce::String (v + 1); };
            }
            else if (kind == 5)
            {
                count = 100;
                text = [] (int v) { return x32::AssignSection::describe ("S0" + juce::String (v).paddedLeft ('0', 2), true); };
            }
            else if (kind == 8)
            {
                count = numPages;
                text = [] (int v) { return juce::String (std::get<2> (pages[juce::jlimit (0, numPages - 1, v)])); };
            }
        }
        else if (kind >= 1 && kind <= 3)
        {
            count = kind == 1 ? 80 : 72;
            text = strip;
        }
        else if (kind == 4)
        {
            count = 8;
            text = [] (int v) { return "FX" + juce::String (v + 1); };
        }
        else if (kind == 5)
        {
            count = 13;
            text = [make] (int v) { return x32::AssignSection::describe (make (5, v, 0), false); };
        }
        if (count > 0)
            encoders[3] = encoder (picker ("TARGET", juce::jlimit (0, count - 1, target), count, text,
                                           [this, make, kind, second] (int v) { setAssignCode (assignControl, make (kind, v, second)); assignEncoders(); }));
        if (! button && (kind == 3 || kind == 4))
            encoders[4] = encoder (picker (kind == 3 ? "BUS" : "PARAM", juce::jlimit (0, kind == 3 ? 15 : 63, second), kind == 3 ? 16 : 64,
                                           [] (int v) { return juce::String (v + 1); },
                                           [this, make, kind, target] (int v) { setAssignCode (assignControl, make (kind, target, v)); assignEncoders(); }));
        assignKinds = kinds;
        assignKind = juce::jmax (0, kind);
        assignTargetCount = count;
        assignTargetAt = juce::jlimit (0, juce::jmax (0, count - 1), target);
        assignTargetText = text;
    }
    else if (screen == Screen::Utility && tab == 1)
    {
        // INIT: the selected strip back to an X32's factory values (its source
        // stays); a second push confirms.
        encoders[0] = encoder ({}, "INIT", [this]
        {
            auto* st = selected();
            const auto address = st != nullptr ? x32Address (*st) : juce::String();
            if (address.isEmpty())
                utilityStatus = "Select a strip to initialise";
            else if (initArmed != st)
            {
                initArmed = st;
                utilityStatus = "Push INIT again to initialise " + st->getLabel();
            }
            else
            {
                initArmed = nullptr;
                utilityStatus = ctx.engine.getWing().initX32Channel (address) ? "Initialised " + st->getLabel() : juce::String ("The console isn't an X32");
                refreshStrip();
            }
            repaint();
        });
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

    // Each turn and push runs guarded: see encoderCallbacks.
    const auto finish = [this]
    {
        if (--encoderCallbacks == 0 && encodersWanted)
        {
            encodersWanted = false;
            assignEncoders();
        }
    };
    for (auto& e : encoders)
    {
        if (e.turn.set)
            e.turn.set = [this, finish, inner = e.turn.set] (float v)
            {
                ++encoderCallbacks;
                inner (v);
                finish();
            };
        if (e.push)
            e.push = [this, finish, inner = e.push]
            {
                ++encoderCallbacks;
                inner();
                finish();
            };
    }
    for (int i = 0; i < 6; ++i)
    {
        knobs[(size_t) i].setBinding (encoders[(size_t) i].turn);
        auto& key = *pushKeys[(size_t) i];
        key.setButtonText (encoders[(size_t) i].pushLabel);
        key.setVisible (static_cast<bool> (encoders[(size_t) i].push));
        key.refresh();
    }
    const bool layers = numLayers() > 1 || (screen == Screen::Home && tab == 4); // HOME > eq: the band
    layerUp.setEnabled (layers);
    layerDown.setEnabled (layers);
    repaint();
}

void Display::paintMonitor (juce::Graphics& g, juce::Rectangle<int> r)
{
    // As the MONITOR photograph: the monitor level, the solo options, the delay
    // in three units, dim, the source's trim and the source list.
    auto& e = ctx.engine;
    auto area = pageFrame (g, r).toFloat();
    auto helpArea = area.removeFromBottom (26.0f);
    const float unit = area.getWidth() / 100.0f;

    auto meter = area.removeFromLeft (20.0f * unit);
    area.removeFromLeft (2.0f * unit);
    {
        auto strip = meter.removeFromLeft (meter.getWidth() * 0.55f);
        const auto scale = strip.removeFromRight (20.0f);
        g.setColour (kNavy);
        g.setFont (type::font (8.5f, true));
        static const char* const marks[] = { "Clip", "-6", "-18", "-30", "-60" };
        for (int k = 0; k < 5; ++k)
            g.drawText (marks[k], scale.withTrimmedTop ((float) k * scale.getHeight() / 5.0f).withHeight (scale.getHeight() / 5.0f), juce::Justification::centredLeft);
        for (int ch = 0; ch < 2; ++ch)
            levelBar (g, strip.removeFromLeft (strip.getWidth() / (ch == 0 ? 2.0f : 1.0f)).reduced (2.0f, 0.0f), ctx.meters.getMonitor (ch));
        valueBox (g, meter.removeFromTop (16.0f).reduced (2.0f, 1.0f), fader::text (e.monitorLevelDb.load()));
        g.setColour (kNavy);
        g.setFont (type::font (9.5f, false));
        g.drawFittedText ("Monitor Level", meter.removeFromBottom (24.0f).toNearestInt(), juce::Justification::centred, 2, 0.7f);
        knobIcon (g, meter.getCentre(), juce::jmin (10.0f, meter.getHeight() * 0.32f), fader::dbToPos (e.monitorLevelDb.load()));
    }

    auto solo = area.removeFromLeft (27.0f * unit);
    area.removeFromLeft (2.0f * unit);
    g.setColour (kNavy);
    g.setFont (type::font (10.5f, true));
    g.drawText ("Solo Options", solo.removeFromTop (18.0f), juce::Justification::centredLeft);
    const float rowH = juce::jmin (17.0f, solo.getHeight() / 7.0f);
    for (int k = 0; k < soloOptions().size(); ++k)
    {
        auto row = solo.removeFromTop (rowH);
        if (k == soloOption)
        {
            g.setColour (kGr);
            g.drawRect (row, 1.0f);
        }
        checkBox (g, row.reduced (1.0f), soloOptions()[k], soloOptionOn (k));
    }

    const auto column = [&area, unit] (float widthPercent)
    {
        auto c = area.removeFromLeft (widthPercent * unit);
        area.removeFromLeft (2.0f * unit);
        return c;
    };
    const auto knobAndChip = [&g] (juce::Rectangle<float> c, float fraction, const juce::String& name, const juce::String& chipText, bool on)
    {
        auto chipArea = c.removeFromBottom (15.0f);
        chip (g, chipArea.reduced (6.0f, 0.0f), chipText, on);
        g.setColour (kNavy);
        g.setFont (type::font (9.5f, false));
        g.drawFittedText (name, c.removeFromBottom (24.0f).toNearestInt(), juce::Justification::centred, 2, 0.7f);
        knobIcon (g, c.getCentre(), juce::jmin (11.0f, c.getHeight() * 0.3f), fraction);
    };
    const float ms = x32Value ("/config/solo/delaytime").getFloatValue();
    {
        auto c = column (14.0f);
        for (auto [text, suffix] : { std::pair<juce::String, const char*> { juce::String (ms * 1.125f, 1), "ft" },
                                     std::pair<juce::String, const char*> { juce::String (ms * 0.343f, 2), "m" },
                                     std::pair<juce::String, const char*> { juce::String (ms, 1), "ms" } })
        {
            auto row = c.removeFromTop (15.0f);
            g.setColour (kNavy);
            g.setFont (type::font (9.5f, false));
            g.drawText (suffix, row.removeFromRight (16.0f), juce::Justification::centredLeft);
            valueBox (g, row.reduced (1.0f, 1.0f), text);
        }
        knobAndChip (c, (ms - 0.3f) / 499.7f, "Delay", "Delay", x32Value ("/config/solo/delay") == "ON");
    }
    {
        auto c = column (14.0f);
        valueBox (g, c.removeFromTop (16.0f).reduced (6.0f, 1.0f), "-" + juce::String (juce::roundToInt (e.monitorDimDb.load())));
        knobAndChip (c, e.monitorDimDb.load() / 40.0f, "Dim Attenuation", "Dim", e.monitorDim.load());
    }
    {
        auto c = column (14.0f);
        const float trim = x32Value ("/config/solo/sourcetrim").getFloatValue();
        valueBox (g, c.removeFromTop (16.0f).reduced (6.0f, 1.0f), juce::String (trim, 1));
        knobAndChip (c, (trim + 18.0f) / 36.0f, "Source Trim", "Mono", e.monitorMono.load());
    }
    static const juce::StringArray sourceNames { "OFF", "LR Bus", "LR+C/M", "LR PFL", "LR AFL", "Aux 5/6", "Aux 7/8" };
    static const juce::StringArray sourceCodes { "OFF", "LR", "LR+C", "LRPFL", "LRAFL", "AUX56", "AUX78" };
    listColumn (g, area, "Monitor Source", sourceNames, monitorSourcePick, juce::jmax (0, sourceCodes.indexOf (x32Value ("/config/solo/source"))));

    static const char* const help[][2] = {
        { "A new solo replaces the one before.", "Solos add up; each key switches its own channel." },
        { "The selected channel is soloed automatically.", "Select and solo stay separate." },
        { "A soloed channel also becomes the selected one.", "Solo leaves the selection alone." },
        { "Channel solos listen after the fader.", "Channel solos listen before the fader." },
        { "Mix bus solos listen after the fader.", "Mix bus solos listen before the fader." },
        { "DCA solos listen after the fader.", "DCA solos listen before the fader." },
        { "The main fader and mute also set the solo / monitor output.", "The monitor section alone sets the output." } };
    g.setColour (kDim);
    g.setFont (type::font (9.5f, false));
    auto lines = helpArea;
    g.drawText (juce::String ("[ON]   ") + help[soloOption][0], lines.removeFromTop (12.0f), juce::Justification::centredLeft);
    g.drawText (juce::String ("[OFF]  ") + help[soloOption][1], lines.removeFromTop (12.0f), juce::Justification::centredLeft);
}

void Display::paintTalkback (juce::Graphics& g, juce::Rectangle<int> r)
{
    // Manual p.61: level and gain-reduction meters, the keys, the talk level and
    // the destinations (Mix Bus 1-16, LR, C/M).
    auto& e = ctx.engine;
    const juce::String node = tab == 1 ? "/config/talk/A" : "/config/talk/B";
    auto& path = e.talk.paths[(size_t) (tab - 1)];
    const int bits = x32Bits (x32Value (node + "/destmap"));
    const auto* talk = e.getTalkStrip();
    auto area = pageFrame (g, r).toFloat();
    const float unit = area.getWidth() / 100.0f;

    auto meters = area.removeFromLeft (14.0f * unit);
    area.removeFromLeft (3.0f * unit);
    {
        auto strip = meters.removeFromTop (meters.getHeight() - 14.0f);
        auto level = strip.removeFromLeft (strip.getWidth() * 0.5f).reduced (3.0f, 0.0f);
        levelBar (g, level, talk != nullptr ? ctx.meters.get (talk, MeterPoint::Output, 0) : 0.0f);
        // The talk channel has no gain reduction of its own: the well stays dark.
        g.setColour (kWell);
        g.fillRect (strip.reduced (3.0f, 0.0f));
        g.setColour (kNavy);
        g.setFont (type::font (9.0f, true));
        g.drawText ("Level     GR", meters, juce::Justification::centred);
    }
    auto keys = area.removeFromLeft (24.0f * unit);
    area.removeFromLeft (3.0f * unit);
    g.setColour (kNavy);
    g.setFont (type::font (10.5f, true));
    g.drawText (tab == 1 ? "Talkback A" : "Talkback B", keys.removeFromTop (18.0f), juce::Justification::centredLeft);
    const float chipH = juce::jmin (20.0f, (keys.getHeight() - 6.0f) / 4.0f);
    const auto keyRow = [&] (const juce::String& text, bool on) { chip (g, keys.removeFromTop (chipH).reduced (0.0f, 2.0f), text, on); };
    keyRow ("Talk", path.on.load());
    keyRow (x32Value ("/config/talk/source") == "EXT" ? "Ext Mic" : "Int Mic", x32Value ("/config/talk/source") == "EXT");
    keyRow ("Auto Dim", x32Value (node + "/dim") == "ON");
    keyRow ("Latch", x32Value (node + "/latch") == "ON");

    auto level = area.removeFromLeft (16.0f * unit);
    area.removeFromLeft (3.0f * unit);
    {
        valueBox (g, level.removeFromTop (18.0f).reduced (6.0f, 1.0f), fader::text (e.talk.levelDb.load()));
        g.setColour (kNavy);
        g.setFont (type::font (9.5f, false));
        g.drawText ("Talk Level", level.removeFromBottom (14.0f), juce::Justification::centred);
        knobIcon (g, level.getCentre(), juce::jmin (12.0f, level.getHeight() * 0.3f), fader::dbToPos (e.talk.levelDb.load()));
    }
    // Sidecar's talkback channel (an X32 has its own socket).
    g.setColour (kNavy);
    g.setFont (type::font (10.5f, true));
    g.drawText ("Destinations", area.removeFromTop (18.0f), juce::Justification::centredLeft);
    auto grid = area.removeFromTop (juce::jmin (area.getHeight(), 136.0f));
    const float cw = grid.getWidth() / 4.0f, ch = grid.getHeight() / 5.0f;
    const auto destCell = [&] (int b) { return juce::Rectangle<float> (grid.getX() + cw * (float) (b % 4), grid.getY() + ch * (float) (b / 4), cw, ch).reduced (2.0f); };
    for (int b = 0; b < 18; ++b)
    {
        auto c = destCell (b);
        chip (g, c, b < 16 ? juce::String (b + 1) : juce::String (b == 16 ? "LR" : "C/M"), (bits & (1 << b)) != 0);
        if (b == talkBus)
        {
            g.setColour (kGr);
            g.drawRect (c, 1.5f);
        }
    }
    g.setColour (kDim);
    g.setFont (type::font (9.5f, false));
    g.drawFittedText ("Talk channel: " + (talk != nullptr ? talk->getLabel() : juce::String ("OFF")) + ".  Lamp: " + x32Value ("/-prefs/lamp") + "% "
                          + (x32On ("/-prefs/lampon") ? "on" : "off") + " (kept: no lamp socket on a computer).",
                      area.toNearestInt(), juce::Justification::centredLeft, 2, 0.7f);
}

void Display::paintOscillator (juce::Graphics& g, juce::Rectangle<int> r)
{
    // Manual p.61: the level meter, the two frequencies, the type and the destination list.
    const bool on = x32Value ("/-stat/osc/on") == "ON";
    const float levelDb = x32Level (x32Value ("/config/osc/level"));
    auto area = pageFrame (g, r).toFloat();
    const float unit = area.getWidth() / 100.0f;
    auto meter = area.removeFromLeft (10.0f * unit);
    area.removeFromLeft (3.0f * unit);
    levelBar (g, meter.reduced (6.0f, 0.0f), on ? juce::Decibels::decibelsToGain (levelDb) : 0.0f);

    const auto knobColumn = [&] (const juce::String& name, const juce::String& value, float fraction, const juce::String& chipText, bool chipOn)
    {
        auto c = area.removeFromLeft (17.0f * unit);
        area.removeFromLeft (2.0f * unit);
        valueBox (g, c.removeFromTop (18.0f).reduced (4.0f, 1.0f), value);
        auto chipArea = c.removeFromBottom (15.0f);
        chip (g, chipArea.reduced (6.0f, 0.0f), chipText, chipOn);
        g.setColour (kNavy);
        g.setFont (type::font (9.5f, false));
        g.drawText (name, c.removeFromBottom (14.0f), juce::Justification::centred);
        knobIcon (g, c.getCentre(), juce::jmin (13.0f, c.getHeight() * 0.32f), fraction);
    };
    const bool useF2 = x32Value ("/config/osc/fsel") == "F2";
    knobColumn ("Level", x32Value ("/config/osc/level"), fader::dbToPos (levelDb), "Osc On", on);
    knobColumn ("Frequency 1", x32Value ("/config/osc/f1"), std::log (x32Hz (x32Value ("/config/osc/f1")) / 20.0f) / std::log (1000.0f), useF2 ? "" : "In Use", ! useF2);
    knobColumn ("Frequency 2", x32Value ("/config/osc/f2"), std::log (x32Hz (x32Value ("/config/osc/f2")) / 20.0f) / std::log (1000.0f), useF2 ? "In Use" : "", useF2);
    static const juce::StringArray typeNames { "SINE", "PINK", "WHITE" };
    auto typeList = area.removeFromLeft (14.0f * unit);
    area.removeFromLeft (2.0f * unit);
    listColumn (g, typeList, "Type", { "Sine", "Pink Noise", "White Noise" }, -1, juce::jmax (0, typeNames.indexOf (x32Value ("/config/osc/type"))));
    juce::StringArray dests;
    const int dest = x32Value ("/config/osc/dest").getIntValue();
    for (int i = juce::jmax (0, dest - 4); i < juce::jmin (26, juce::jmax (0, dest - 4) + 9); ++i)
        dests.add (x32OscDestination (i));
    listColumn (g, area, "Destination", dests, -1, juce::jlimit (0, dests.size() - 1, dest - juce::jmax (0, dest - 4)));
}

void Display::paintAssign (juce::Graphics& g, juce::Rectangle<int> r)
{
    auto area = pageFrame (g, r).toFloat();
    if (tab == 0)
    {
        // HOME (p.63): all three sets at once, the one on the keys marked.
        const int shownSet = getAssignSet ? getAssignSet() : 0;
        static const char* const setNames[] = { "Set A", "Set B", "Set C" };
        const float w = area.getWidth() / 3.0f;
        for (int set = 0; set < 3; ++set)
        {
            auto col = area.removeFromLeft (w).reduced (4.0f, 0.0f);
            if (set == shownSet)
            {
                g.setColour (kAmber.withAlpha (0.35f));
                g.fillRoundedRectangle (col.expanded (2.0f), 3.0f);
            }
            g.setColour (kNavy);
            g.setFont (type::font (10.5f, true));
            g.drawText (setNames[set], col.removeFromTop (16.0f), juce::Justification::centred);
            const float rowH = juce::jmin (15.0f, col.getHeight() / 12.0f);
            for (int i = 0; i < 12; ++i)
            {
                auto row = col.removeFromTop (rowH);
                const auto code = assignCodeOf ? assignCodeOf (set, i) : juce::String ("-");
                g.setColour (kNavy);
                g.setFont (type::font (9.5f, true));
                g.drawText ((i < 4 ? "Enc " : "Btn ") + juce::String (i + 1), row.removeFromLeft (38.0f), juce::Justification::centredLeft);
                g.setFont (type::font (9.5f, false));
                g.drawFittedText (code == "-" ? juce::String ("not assigned") : x32::AssignSection::describe (code, i >= 4), row.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
            }
        }
        return;
    }
    // A set's tab (manual p.64): the set colour, the control, its target type and parameter.
    juce::StringArray controls;
    for (int i = 0; i < 12; ++i)
        controls.add ((i < 4 ? "Encoder " : "Button ") + juce::String (i + 1));
    juce::StringArray parameters;
    int paramPointed = -1, paramFirst = 0;
    if (assignTargetCount > 0 && assignTargetText)
    {
        paramFirst = juce::jlimit (0, juce::jmax (0, assignTargetCount - 12), assignTargetAt - 6);
        for (int i = paramFirst; i < juce::jmin (assignTargetCount, paramFirst + 12); ++i)
            parameters.add (assignTargetText (i));
        paramPointed = assignTargetAt - paramFirst;
    }
    g.setColour (kNavy);
    g.setFont (type::font (10.5f, true));
    g.drawText ("Assignable Controls Configuration", area.removeFromTop (18.0f), juce::Justification::centredLeft);
    const float w = area.getWidth() / 4.0f;
    auto first = area.removeFromLeft (w).reduced (6.0f, 0.0f);
    {
        // Set Color (the manual's picture): the eight colours, the set's own framed; "inv" when inverted.
        const int colour = juce::jmax (0, scribbleColours().indexOf (x32Value ("/config/userctrl/" + juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, 2, tab - 1))) + "/color")));
        g.setColour (kNavy);
        g.setFont (type::font (10.5f, true));
        g.drawFittedText (colour >= 8 ? "Set Color (inv)" : "Set Color", first.removeFromTop (16.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        const float rowH = juce::jmin (15.0f, first.getHeight() / 8.0f);
        for (int k = 0; k < 8; ++k)
        {
            auto row = first.removeFromTop (rowH);
            g.setColour (k == 0 ? juce::Colour (0xff101214) : consoleColour (k + 1));
            g.fillRect (row.reduced (0.0f, 0.5f));
            if (k == colour % 8)
            {
                g.setColour (kNavy);
                g.drawRect (row, 2.0f);
            }
        }
    }
    listColumn (g, area.removeFromLeft (w).reduced (6.0f, 0.0f), "Select Control", controls, assignControl, assignControl);
    listColumn (g, area.removeFromLeft (w).reduced (6.0f, 0.0f), "Target Type", assignKinds, assignKind, assignKind);
    listColumn (g, area.reduced (6.0f, 0.0f), "Parameter", parameters, paramPointed, paramPointed);
}

void Display::paintHomeConfig (juce::Graphics& g, juce::Rectangle<int> r, Strip& s)
{
    // The manual's Config picture (p.41): gain and link, the low cut, the source
    // list, the delay in three units, the insert and its position.
    auto& p = s.params;
    const auto address = x32Address (s);
    auto area = pageFrame (g, r).toFloat();
    const float unit = area.getWidth() / 100.0f;
    const auto column = [&area, unit] (float widthPercent)
    {
        auto c = area.removeFromLeft (widthPercent * unit);
        area.removeFromLeft (2.0f * unit);
        return c;
    };
    const auto titleOf = [&g] (juce::Rectangle<float>& c, const juce::String& name)
    {
        g.setColour (kNavy);
        g.setFont (type::font (10.5f, true));
        g.drawText (name, c.removeFromTop (15.0f), juce::Justification::centred);
    };
    const auto knobValue = [&g] (juce::Rectangle<float> c, const juce::String& value, float fraction, const juce::String& caption)
    {
        valueBox (g, c.removeFromTop (16.0f).reduced (6.0f, 1.0f), value);
        g.setColour (kNavy);
        g.setFont (type::font (9.5f, false));
        g.drawText (caption, c.removeFromBottom (13.0f), juce::Justification::centred);
        knobIcon (g, c.getCentre(), juce::jmin (11.0f, c.getHeight() * 0.3f), fraction);
    };
    {
        auto c = column (13.0f);
        titleOf (c, "Link");
        const auto link = linkAddress (s);
        chip (g, c.removeFromTop (14.0f).reduced (4.0f, 0.0f), "Link", link.isNotEmpty() && x32Value (link) == "ON");
        if (const auto gain = gainBinding (s); gain.isValid())
            knobValue (c, gain.text().upToFirstOccurrenceOf (" ", false, false), gain.get(), gainIsHeadAmp (s) ? "Gain" : "Trim");
    }
    {
        auto c = column (19.0f);
        titleOf (c, "Lo Cut");
        chip (g, c.removeFromTop (14.0f).reduced (4.0f, 0.0f), "Lo Cut", p.filter().lowCut.load());
        c.removeFromTop (2.0f);
        auto curve = c.removeFromTop (juce::jmin (46.0f, c.getHeight() - 18.0f));
        g.setColour (kWell);
        g.fillRect (curve);
        g.setColour (kCurve);
        const float x = curve.getX() + curve.getWidth() * (std::log (p.filter().lowCutFreq.load() / 20.0f) / std::log (20.0f));
        juce::Path pth;
        pth.startNewSubPath (curve.getX() + 2.0f, curve.getBottom() - 3.0f);
        pth.lineTo (x, curve.getBottom() - 3.0f);
        pth.lineTo (x + 10.0f, curve.getCentreY() - 8.0f);
        pth.lineTo (curve.getRight() - 2.0f, curve.getCentreY() - 8.0f);
        if (p.filter().lowCut.load())
            g.strokePath (pth, juce::PathStrokeType (1.5f));
        valueBox (g, c.removeFromTop (16.0f).reduced (6.0f, 1.0f), formatHz (p.filter().lowCutFreq.load()) + " Hz");
    }
    {
        auto c = column (21.0f);
        juce::StringArray items;
        int on = 0, pointed = -1;
        if (address.startsWith ("/ch/") || address.startsWith ("/auxin/"))
        {
            const int cur = pendingSource >= 0 && pendingSourceStrip == &s ? pendingSource : x32Value (address + "/config/source").getIntValue();
            const int first = juce::jlimit (0, juce::jmax (0, 65 - 7), cur - 3);
            for (int k = 0; k < 7 && first + k <= 64; ++k)
                items.add (sourceName (first + k));
            on = x32Value (address + "/config/source").getIntValue() - first;
            pointed = cur - first;
        }
        else
            items.add ("-");
        listColumn (g, c, "Source", items, pointed, on);
    }
    {
        auto c = column (15.0f);
        titleOf (c, "Delay");
        const float ms = p.input.delay.load();
        chip (g, c.removeFromTop (14.0f).reduced (4.0f, 0.0f), "Delay", p.input.delayOn.load());
        for (auto [text, suffix] : { std::pair<juce::String, const char*> { juce::String (ms * 1.125f, 1), "ft" },
                                     std::pair<juce::String, const char*> { juce::String (ms * 0.343f, 2), "m" },
                                     std::pair<juce::String, const char*> { juce::String (ms, 1), "ms" } })
        {
            auto row = c.removeFromTop (15.0f);
            g.setColour (kNavy);
            g.setFont (type::font (9.5f, false));
            g.drawText (suffix, row.removeFromRight (16.0f), juce::Justification::centredLeft);
            valueBox (g, row.reduced (1.0f, 1.0f), text);
        }
    }
    {
        juce::StringArray items;
        int on = 0, pointed = -1;
        if (address.startsWith ("/ch/") || address.startsWith ("/bus/") || address.startsWith ("/mtx/"))
        {
            const int current = juce::jmax (0, kInserts.indexOf (x32Value (address + "/insert/sel")));
            const int cur = pendingInsert >= 0 && pendingSourceStrip == &s ? pendingInsert : current;
            const int first = juce::jlimit (0, juce::jmax (0, kInserts.size() - 6), cur - 2);
            for (int k = 0; k < 6 && first + k < kInserts.size(); ++k)
                items.add (kInserts[first + k]);
            on = current - first;
            pointed = cur - first;
        }
        else
            items.add ("-");
        auto column2 = area.withTrimmedBottom (18.0f);
        listColumn (g, column2, "Insert", items, pointed, on);
        auto pos = area.removeFromBottom (16.0f);
        const bool pre = (p.processOrder.load() & 2) != 0;
        chip (g, pos.removeFromLeft (pos.getWidth() * 0.5f).reduced (1.0f, 1.0f), "Pre", pre);
        chip (g, pos.reduced (1.0f, 1.0f), "Post", ! pre);
    }
}

namespace
{
    void drawBar (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, float size = 11.0f);
}

void Display::paintHomeMain (juce::Graphics& g, juce::Rectangle<int> r, Strip& s)
{
    // The MAIN tab as the console draws it: the assign (LR + SUB), the stereo and M/C buses'
    // pan and level at the left, and the LEFT, SUB and RIGHT meters with their faders.
    auto& p = s.params;
    const bool mc = ctx.engine.getMains().size() > 1;
    auto area = r.reduced (8, 4);
    auto panels = area.removeFromRight (area.getWidth() * 56 / 100);
    area.removeFromRight (4);
    // The assign: LR + SUB, or LCR (Sidecar's mains are a stereo bus and the M/C bus).
    {
        auto line = area.removeFromTop (16).withTrimmedLeft (area.getWidth() / 5);
        g.setFont (type::font (11.0f, true));
        int k = 0;
        for (auto text : { "LR + SUB", "LCR" })
        {
            auto row = k == 0 ? line : area.removeFromTop (16).withTrimmedLeft (area.getWidth() / 5);
            g.setColour (kNavy);
            g.drawText (text, row.removeFromLeft (row.getWidth() * 55 / 100), juce::Justification::centredLeft);
            auto dot = row.removeFromLeft (14).withSizeKeepingCentre (10, 10).toFloat();
            g.setColour (k == 0 ? kAmber : kDim);
            g.fillEllipse (dot);
            ++k;
        }
    }
    area.removeFromTop (10);
    const float cw = (float) area.getWidth() / 2.0f;
    const auto group = [&] (juce::Rectangle<float> c, const juce::String& title, const juce::String& chipText, bool on, const juce::String& value, float fraction, const juce::String& knobName)
    {
        g.setColour (kDim);
        g.setFont (type::font (9.5f, true));
        g.drawText (title, c.removeFromTop (14.0f), juce::Justification::centred);
        chip (g, c.removeFromTop (14.0f).reduced (14.0f, 0.0f), chipText, on);
        c.removeFromTop (3.0f);
        valueBox (g, c.removeFromTop (14.0f).reduced (12.0f, 0.0f), value);
        auto name = c.removeFromBottom (14.0f);
        g.setColour (kDim);
        g.drawText (knobName, name, juce::Justification::centred);
        knobIcon (g, c.getCentre(), juce::jmin (12.0f, c.getHeight() * 0.4f), fraction);
    };
    auto left = area.toFloat();
    group (left.removeFromLeft (cw), "STEREO BUS", "LR", s.mains[0].on.load(), juce::String (juce::roundToInt (p.pan.load())), (p.pan.load() + 100.0f) / 200.0f, "PAN");
    group (left, "M/C BUS", "SUB", mc && s.mains[1].on.load(), mc ? fader::text (s.mains[1].levelDb.load()) + " dB" : juce::String ("-"),
           mc ? fader::dbToPos (s.mains[1].levelDb.load()) : 0.0f, "LEVEL");
    // LEFT, SUB, RIGHT: each a fader and a meter on its own scale (Clip, -2 ... -60).
    const float w = (float) panels.getWidth() / 3.0f;
    const char* const names[] = { "LEFT", "SUB", "RIGHT" };
    for (int k = 0; k < 3; ++k)
    {
        auto col = juce::Rectangle<float> ((float) panels.getX() + w * (float) k, (float) panels.getY(), w, (float) panels.getHeight()).reduced (2.0f, 0.0f);
        drawBar (g, col.removeFromTop (16.0f).toNearestInt(), names[k], 10.0f);
        g.setColour (juce::Colour (0xff2b2f3a));
        g.fillRect (col);
        auto well = col.reduced (4.0f, 6.0f);
        auto slot = well.removeFromLeft (well.getWidth() * 0.3f).reduced (well.getWidth() * 0.1f, 0.0f);
        const float db = k == 1 ? (mc ? s.mains[1].levelDb.load() : -144.0f) : p.faderDb.load();
        const float pos = juce::jlimit (0.0f, 1.0f, fader::dbToPos (db));
        g.setColour (juce::Colour (0xff10131a));
        g.fillRect (slot.withSizeKeepingCentre (3.0f, slot.getHeight()));
        g.setColour (juce::Colour (0xffe09a1f));
        g.fillRect (slot.getCentreX() - 1.5f, slot.getBottom() - slot.getHeight() * pos, 3.0f, slot.getHeight() * pos);
        g.setColour (juce::Colour (0xffdfe3ee));
        g.fillRect (slot.getCentreX() - 7.0f, slot.getBottom() - slot.getHeight() * pos - 4.0f, 14.0f, 8.0f);
        auto meter = well.removeFromLeft (well.getWidth() * 0.5f).reduced (2.0f, 0.0f);
        auto scale = well;
        const float level = k == 1 ? 0.0f : juce::jlimit (0.0f, 1.0f, fader::dbToPos (juce::Decibels::gainToDecibels (ctx.meters.get (&s, MeterPoint::Output, k == 0 ? 0 : 1), -144.0f)));
        g.setColour (juce::Colour (0xffff3b30));
        g.fillRect (meter.getX(), meter.getY(), meter.getWidth(), 3.0f);
        const auto m = meter.withTrimmedTop (4.0f);
        for (float yy = m.getBottom(); yy > m.getY() + 2.0f; yy -= 3.0f)
        {
            const float up = (m.getBottom() - yy) / m.getHeight();
            g.setColour (up > level ? juce::Colour (0xff3a3f4c) : up < fader::dbToPos (-18.0f) ? juce::Colour (0xff3ddc5a) : juce::Colour (0xfff2d51f));
            g.fillRect (m.getX(), yy - 2.0f, m.getWidth(), 2.0f);
        }
        g.setColour (juce::Colour (0xffc8d0ea));
        g.setFont (type::font (7.5f, true));
        g.drawText ("Clip", scale.withHeight (9.0f), juce::Justification::centredLeft);
        for (float mark : { -2.0f, -6.0f, -12.0f, -18.0f, -30.0f, -42.0f, -60.0f })
            g.drawText (juce::String ((int) mark), juce::Rectangle<float> (scale.getX(), m.getBottom() - m.getHeight() * fader::dbToPos (mark) - 4.0f, scale.getWidth(), 9.0f), juce::Justification::centredLeft);
    }
}

void Display::paintSetupConfig (juce::Graphics& g, juce::Rectangle<int> r)
{
    auto area = pageFrame (g, r).toFloat();
    const float w = area.getWidth() / 3.0f;
    const auto checks = [&] (juce::Rectangle<float> c, const juce::String& title, const std::vector<ConfigItem>& items, int pointed)
    {
        g.setColour (kNavy);
        g.setFont (type::font (10.5f, true));
        g.drawText (title, c.removeFromTop (16.0f), juce::Justification::centredLeft);
        // Every box when they fit at 11 px or more; else scrolled around the one pointed at.
        const int n = (int) items.size();
        const float fit = c.getHeight() / (float) juce::jmax (1, n);
        const float rowH = fit >= 11.0f ? juce::jmin (16.0f, fit) : 13.0f;
        const int rows = juce::jmax (1, (int) (c.getHeight() / rowH));
        const int from = juce::jlimit (0, juce::jmax (0, n - rows), pointed - rows / 2);
        for (int k = from; k < juce::jmin (n, from + rows); ++k)
        {
            auto row = c.removeFromTop (rowH);
            if (k == pointed)
            {
                g.setColour (kGr);
                g.drawRect (row, 1.0f);
            }
            g.setColour (kNavy);
            checkBox (g, row.reduced (1.0f), titled (items[(size_t) k].name), x32Value (items[(size_t) k].path) == items[(size_t) k].on);
        }
    };
    checks (area.removeFromLeft (w).reduced (4.0f, 0.0f), "Preferences", configPrefs(), configPref);
    checks (area.removeFromLeft (w).reduced (4.0f, 0.0f), "Channel Link", configLinks(), configLink);
    // The bus pre-configuration: each mix bus's kind under the scheme the encoder points at.
    auto c = area.reduced (4.0f, 0.0f);
    const auto& sch = kBusSchemes[configBus];
    g.setColour (kNavy);
    g.setFont (type::font (10.5f, true));
    g.drawText ("Bus Pre-Configuration", c.removeFromTop (16.0f), juce::Justification::centredLeft);
    g.setFont (type::font (10.0f, false));
    g.drawFittedText (sch.name, c.removeFromTop (16.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
    // Show Control along the bottom (what PREV, NEXT and GO step through).
    {
        auto line = c.removeFromBottom (15.0f);
        g.setFont (type::font (10.0f, true));
        g.drawText ("Show Control", line.removeFromLeft (line.getWidth() * 0.55f), juce::Justification::centredLeft);
        valueBox (g, line.reduced (1.0f), titled (x32Value ("/-prefs/show_control")));
        g.setColour (kNavy);
    }
    const float cw = c.getWidth() / 8.0f, ch = juce::jmin (c.getHeight() / 2.0f, 26.0f);
    for (int b = 0; b < 16; ++b)
    {
        auto cell = juce::Rectangle<float> (c.getX() + cw * (float) (b % 8), c.getY() + ch * (float) (b / 8), cw, ch).reduced (1.5f);
        const bool sub = b >= sch.pre + sch.post;
        g.setColour (sub ? kDeep : b < sch.pre ? kCurve : kSky);
        g.fillRoundedRectangle (cell, 2.0f);
        g.setColour (sub ? juce::Colours::white : kNavy);
        g.setFont (type::font (8.5f, true));
        g.drawFittedText (juce::String (b + 1) + "\n" + (sub ? "SUB" : b < sch.pre ? "PRE" : "POST"), cell.toNearestInt(), juce::Justification::centred, 2, 0.6f);
    }
}

void Display::paintNetwork (juce::Graphics& g, juce::Rectangle<int> r)
{
    // Manual p.54: IP address, subnet mask and gateway, each four numbers. They're
    // the computer's and read-only here; below, what X32 remotes need.
    juce::String ip ("0.0.0.0");
    for (auto& a : juce::IPAddress::getAllAddresses (false))
        if (a.address[0] != 127 && (a.address[0] < 224 || a.address[0] > 239) && ! (a.address[0] == 169 && a.address[1] == 254) && ! a.isNull())
        {
            ip = a.toString();
            break;
        }
    auto area = pageFrame (g, r).toFloat();
    const auto fields = [&] (const juce::String& name, const juce::StringArray& values, bool real)
    {
        auto row = area.removeFromTop (28.0f);
        g.setColour (real ? kNavy : kDim);
        g.setFont (type::font (10.5f, true));
        g.drawText (name, row.removeFromLeft (120.0f), juce::Justification::centredLeft);
        for (int k = 0; k < 4; ++k)
        {
            auto box = row.removeFromLeft (64.0f).reduced (4.0f, 4.0f);
            valueBox (g, box, values[k]);
        }
    };
    // The three fields as `/-prefs/ip` keeps them, the one being edited marked and showing its edits.
    static const char* const names[] = { "IP Address", "Subnet Mask", "Gateway" };
    for (int f = 0; f < 3; ++f)
    {
        juce::StringArray values;
        for (int k = 0; k < 4; ++k)
            values.add (juce::String (f == netField && netPending[(size_t) k] >= 0 ? netPending[(size_t) k]
                                                                                 : x32Int (juce::String ("/-prefs/ip/") + kIpNodes[f] + "/" + juce::String (k), 0)));
        const auto top = area.getY();
        fields (names[f], values, true);
        if (f == netField)
        {
            g.setColour (kGr);
            g.drawRect (juce::Rectangle<float> (area.getX(), top + 2.0f, 120.0f + 4.0f * 64.0f, 24.0f), 1.0f);
        }
    }
    area.removeFromTop (6.0f);
    g.setColour (kNavy);
    g.setFont (type::font (10.5f, true));
    g.drawText ("This computer: " + ip, area.removeFromTop (16.0f), juce::Justification::centredLeft);
    auto& wing = ctx.engine.getWing();
    g.setColour (kNavy);
    g.setFont (type::font (10.5f, true));
    g.drawText ("Host: " + juce::SystemStats::getComputerName(), area.removeFromTop (16.0f), juce::Justification::centredLeft);
    g.drawText ("X32 OSC: " + (wing.isX32Running() ? juce::String ("running on UDP ") + juce::String (WingRemote::x32Port) : juce::String ("off (set in Setup > Remote)")),
                area.removeFromTop (16.0f), juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (type::font (10.0f, false));
    g.drawText ("The fields are kept for X32 remotes; the computer's own address is set in its network settings.", area.removeFromTop (16.0f),
                juce::Justification::centredLeft);
}

void Display::paintCard (juce::Graphics& g, juce::Rectangle<int> r)
{
    auto* device = ctx.deviceManager.getCurrentAudioDevice();
    auto area = pageFrame (g, r).toFloat();
    auto left = area.removeFromLeft (area.getWidth() * 0.32f);
    area.removeFromLeft (10.0f);
    // Configuration (as the manual's picture): the mode pointed at, the one in use orange.
    listColumn (g, left, "Configuration", { "32in / 32out", "16in / 16out", "32in / 8out", "8in / 32out", "8in / 8out", "2in / 2out" }, cardMode,
                juce::jmax (0, cardModes().indexOf (x32Value ("/-prefs/card/USBmode"))));
    const auto bits = [] (juce::BigInteger b) { return b.countNumberOfSetBits(); };
    const auto row = [&] (const juce::String& name, const juce::String& value)
    {
        auto line = area.removeFromTop (18.0f);
        g.setColour (kNavy);
        g.setFont (type::font (10.5f, true));
        g.drawText (name, line.removeFromLeft (line.getWidth() * 0.42f), juce::Justification::centredLeft);
        g.setFont (type::font (10.5f, false));
        g.drawFittedText (value, line.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    };
    row ("Card", "Audio Interface");
    row ("Device", device != nullptr ? device->getName() : juce::String ("None"));
    row ("Inputs / Outputs", device != nullptr ? juce::String (bits (device->getActiveInputChannels())) + " / " + juce::String (bits (device->getActiveOutputChannels())) : juce::String ("-"));
    row ("Buffer", device != nullptr ? juce::String (device->getCurrentBufferSizeSamples()) + " samples" : juce::String ("-"));
    row ("Latency In / Out", device != nullptr ? juce::String (device->getInputLatencyInSamples()) + " / " + juce::String (device->getOutputLatencyInSamples()) + " samples" : juce::String ("-"));
}

void Display::paintUsb (juce::Graphics& g, juce::Rectangle<int> r)
{
    // Manual p.62: meters for the recorder and the player, the transport state and the tracks.
    auto& usb = ctx.engine.getUsbRecorder();
    using P = UsbRecorder::PlayState;
    using R = UsbRecorder::RecState;
    const auto clock = [] (int64_t samples, double rate)
    {
        const int secs = rate > 0.0 ? (int) ((double) samples / rate) : 0;
        return juce::String (secs / 60).paddedLeft ('0', 2) + ":" + juce::String (secs % 60).paddedLeft ('0', 2);
    };
    const double rate = juce::jmax (1.0, usb.getSampleRate());
    const auto play = usb.getPlayState();
    const auto rec = usb.getRecState();
    auto area = pageFrame (g, r).toFloat();
    const float unit = area.getWidth() / 100.0f;
    auto meters = area.removeFromLeft (20.0f * unit);
    area.removeFromLeft (3.0f * unit);
    {
        auto recCol = meters.removeFromLeft (meters.getWidth() * 0.5f);
        const auto pair = [&g] (juce::Rectangle<float> c, const juce::String& name, float l, float rr)
        {
            g.setColour (kNavy);
            g.setFont (type::font (9.0f, true));
            g.drawText (name, c.removeFromBottom (12.0f), juce::Justification::centred);
            levelBar (g, c.removeFromLeft (c.getWidth() * 0.5f).reduced (2.0f, 0.0f), l);
            levelBar (g, c.reduced (2.0f, 0.0f), rr);
        };
        const auto toGain = [] (float db) { return juce::Decibels::decibelsToGain (db, -144.0f); };
        pair (recCol, "Rec", toGain (usb.getRecLevel (0)), toGain (usb.getRecLevel (1)));
        pair (meters, "Play", usb.getPlayLevel (0), usb.getPlayLevel (1));
    }
    if (tab == 0)
    {
        auto info = area.removeFromLeft (area.getWidth() * 0.44f);
        area.removeFromLeft (8.0f);
        const auto row = [&info, &g] (const juce::String& name, const juce::String& value)
        {
            auto line = info.removeFromTop (16.0f);
            g.setColour (kNavy);
            g.setFont (type::font (10.0f, true));
            g.drawText (name, line.removeFromLeft (64.0f), juce::Justification::centredLeft);
            g.setFont (type::font (10.0f, false));
            g.drawFittedText (value, line.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        };
        row ("Player", play == P::Playing ? "Playing" : play == P::Paused ? "Paused" : play == P::Error ? "Error" : "Stopped");
        row ("File", usb.getCurrentFile().getFileName().isEmpty() ? juce::String ("-") : usb.getCurrentFile().getFileName());
        row ("Position", clock (usb.getPosition(), rate) + " / " + clock (usb.getLength(), rate));
        row ("Recorder", rec == R::Recording ? "Recording" : rec == R::Paused ? "Paused" : rec == R::Error ? "Error" : "Stopped");
        row ("Recording", usb.getRecFile().getFileName().isEmpty() ? juce::String ("-") : usb.getRecFile().getFileName());
        const auto queue = usb.getQueue();
        juce::StringArray names;
        int on = -1;
        for (size_t i = 0; i < queue.size() && names.size() < 12; ++i)
        {
            if (queue[i].id == usb.getCurrentEntry())
                on = names.size();
            names.add (queue[i].file.getFileNameWithoutExtension());
        }
        listColumn (g, area, "Tracks", names.isEmpty() ? juce::StringArray { "No files" } : names, usbTrack, on);
    }
    else
    {
        const auto row = [&area, &g] (const juce::String& name, const juce::String& value)
        {
            auto line = area.removeFromTop (17.0f);
            g.setColour (kNavy);
            g.setFont (type::font (10.0f, true));
            g.drawText (name, line.removeFromLeft (112.0f), juce::Justification::centredLeft);
            g.setFont (type::font (10.0f, false));
            g.drawFittedText (value, line.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        };
        row ("Record Folder", usb.getRecPath().getFullPathName());
        row ("Storage", usb.getStorageFolder().getFullPathName());
        row ("Playback", usbFolderMode ? "Folder: on to the next file" : "Stop after the file");
        row ("Tracks / Bits", juce::String (usb.getRecChannels()) + " / " + juce::String (usb.getRecBits()));
        // The recorder's two inputs as `/outputs/rec` keeps them (OFF: as patched on Sidecar's routing).
        for (int side = 0; side < 2; ++side)
        {
            const juce::String p = "/outputs/rec/" + two (side + 1);
            const int source = juce::jlimit (0, outputSources().size() - 1, x32Int (p + "/src", 0));
            row (juce::String (side == usbRecSide ? "> " : "") + (side == 0 ? "Input Left" : "Input Right"),
                 source == 0 ? juce::String ("as patched on Sidecar's routing") : outputSources()[source] + "  " + x32Value (p + "/pos"));
        }
    }
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
    if (const int i = at (e.getMains()); i == 0 || i == 1)
        return i == 0 ? "/main/st" : "/main/m";
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

int Display::x32Bits (const juce::String& text)
{
    // "%000000000000000101": the last digit bit 0.
    int bits = 0;
    for (auto c : text.trimCharactersAtStart ("%"))
        bits = (bits << 1) | (c == '1' ? 1 : 0);
    return bits;
}

const juce::StringArray& Display::soloOptions()
{
    // MONITOR's solo options (p.60-61), in the manual's order.
    static const juce::StringArray options { "Exclusive (last)", "Solo follows Select", "Select follows Solo", "Ch Solo AFL", "MixBus Solo AFL", "DCA Solo AFL", "Use Master Fader" };
    return options;
}

bool Display::soloOptionOn (int k) const
{
    switch (k)
    {
        case 0:  return x32Value ("/config/solo/exclusive") == "ON";
        case 1:  return x32Value ("/config/solo/followsel") == "ON";
        case 2:  return x32Value ("/config/solo/followsolo") == "ON";
        case 3:  return ctx.engine.soloAfl.load();
        case 4:  return x32Value ("/config/solo/busmode") == "AFL";
        case 5:  return x32Value ("/config/solo/dcamode") == "AFL";
        default: return x32Value ("/config/solo/masterctrl") == "ON";
    }
}

void Display::toggleSoloOption (int k)
{
    // CHANNEL AFL is the engine's solo; the rest are kept as `/config/solo`.
    int set = 0;
    auto& wing = ctx.engine.getWing();
    const bool on = ! soloOptionOn (k);
    const juce::String onOff = on ? "ON" : "OFF", mode = on ? "AFL" : "PFL";
    switch (k)
    {
        case 0:  wing.runX32Text ("/config/solo/exclusive " + onOff, set); break;
        case 1:  wing.runX32Text ("/config/solo/followsel " + onOff, set); break;
        case 2:  wing.runX32Text ("/config/solo/followsolo " + onOff, set); break;
        case 3:  ctx.engine.soloAfl = on; wing.runX32Text ("/config/solo/chmode " + mode, set); break;
        case 4:  wing.runX32Text ("/config/solo/busmode " + mode, set); break;
        case 5:  wing.runX32Text ("/config/solo/dcamode " + mode, set); break;
        default: wing.runX32Text ("/config/solo/masterctrl " + onOff, set); break;
    }
    repaint();
}

float Display::x32Level (const juce::String& text)
{
    // "-oo" at the bottom, else dB.
    return text.startsWith ("-oo") ? fader::kMinDb : text.getFloatValue();
}

float Display::x32Hz (const juce::String& text)
{
    // "124.7", "1k97", "10k02": the k in place of the point, in kHz.
    return juce::jlimit (20.0f, 20000.0f, text.containsChar ('k') ? text.replaceCharacter ('k', '.').getFloatValue() * 1000.0f : text.getFloatValue());
}

juce::String Display::x32OscDestination (int dest)
{
    // The X32's choices (MAN p.62): Mix Bus 1-16, Main L, R, L+R, M/C, Matrix 1-6.
    if (dest < 16)
        return "MIX BUS " + juce::String (juce::jmax (0, dest) + 1);
    if (dest < 20)
        return juce::StringArray { "MAIN L", "MAIN R", "MAIN L+R", "MAIN M/C" }[dest - 16];
    return "MATRIX " + juce::String (juce::jmin (25, dest) - 19);
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
        // As the manual gives them (p.58): 1 and 2 FX 1-4's sources L and R, 5 the
        // effect (ASSIGN puts it in), 6 the slot (push: EDIT).
        KnobBinding pick;
        pick.label = "FX";
        pick.get = [this] { return (float) selectedFx / 7.0f; };
        pick.set = [this] (float v)
        {
            selectedFx = juce::jlimit (0, 7, juce::roundToInt (v * 7.0f));
            pendingFxModel = -1;
            assignEncoders();
        };
        pick.text = [this] { return juce::String (selectedFx + 1); };
        pick.steps = 8.0f;
        encoders[5] = encoder (pick, "EDIT", [this] { showScreen (Screen::Effects, selectedFx + 1); });

        const int slot = selectedFx;
        const auto list = fxModelsFor (slot);
        KnobBinding type;
        type.label = "TYPE";
        const int n = (int) list.size();
        const auto shownModel = [this, &fx, slot] { return pendingFxModel >= 0 ? pendingFxModel : fx.getModel (slot); };
        type.get = [shownModel, list, n]
        {
            const auto at = std::find (list.begin(), list.end(), shownModel());
            return at == list.end() || n < 2 ? 0.0f : (float) (at - list.begin()) / (float) (n - 1);
        };
        type.set = [this, list, n] (float v)
        {
            if (n == 0)
                return;
            pendingFxModel = list[(size_t) juce::roundToInt (v * (float) (n - 1))];
            repaint();
        };
        type.text = [shownModel]
        {
            const int m = shownModel();
            return m > ld::kFxExt ? models::model (models::Fx, m).name : juce::String ("-");
        };
        type.steps = (float) juce::jmax (2, n);
        encoders[4] = encoder (type, "ASSIGN", [this, slot, run]
        {
            if (pendingFxModel < 0)
                return;
            const auto& m = models::model (models::Fx, pendingFxModel);
            run ("/fx/" + juce::String (slot + 1) + "/type " + m.token.fromFirstOccurrenceOf ("X32", false, false));
            pendingFxModel = -1;
        }, [this] { return pendingFxModel >= 0; });
        if (slot < 4)
        {
            static const juce::StringArray sources { "INS", "MIX1", "MIX2", "MIX3", "MIX4", "MIX5", "MIX6", "MIX7", "MIX8", "MIX9",
                                                     "MIX10", "MIX11", "MIX12", "MIX13", "MIX14", "MIX15", "MIX16", "M/C" };
            const auto base = "/fx/" + juce::String (slot + 1) + "/source/";
            encoders[0] = encoder (enumKnob ("SOURCE L", base + "l", sources));
            encoders[1] = encoder (enumKnob ("SOURCE R", base + "r", sources));
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
    if (isGraphicEq (slot))
    {
        // A graphic EQ (as its page photographs): 1 Scroll, the four bands on encoders
        // 2-5 (push: GEQ ON FADERS, the manual's tip, p.58); 2-5 their gains (push: RESET);
        // 6 the side's level (push: FX HOME). A dual EQ's layers are its sides A and B.
        const auto bands = geqBands (slot);
        const int side = bands.size() > 31 ? juce::jlimit (0, 1, layer) : 0;
        geqFirst = juce::jlimit (0, 27, geqFirst);
        KnobBinding scroll;
        scroll.label = "SCROLL";
        scroll.get = [this] { return (float) geqFirst / 27.0f; };
        scroll.set = [this, slot] (float v)
        {
            geqFirst = juce::jlimit (0, 27, juce::roundToInt (v * 27.0f));
            if (geqSlot == slot)
                showGeqOnFaders (slot, geqFirst);
            assignEncoders();
        };
        scroll.text = [this] { return juce::String (geqFirst); };
        scroll.steps = 28.0f;
        encoders[0] = encoder (scroll, "GEQ ON FADERS", [this, slot] { showGeqOnFaders (geqSlot == slot ? -1 : slot, geqFirst); }, [this, slot] { return geqSlot == slot; });
        for (int k = 0; k < 4; ++k)
        {
            const size_t at = (size_t) (side * 31 + geqFirst + k);
            if (at >= bands.size())
                break;
            const int index = bands[at];
            auto b = bindModelParam (values[index], model.params[(size_t) index]);
            b.label = geqBandHz (geqFirst + k);
            auto* value = &values[index];
            encoders[(size_t) k + 1] = encoder (b, "RESET", [value] { value->store (0.0f); });
        }
        const int master = model.find (bands.size() > 31 ? (side == 0 ? "master_a" : "master_b") : "master");
        KnobBinding level;
        if (master >= 0)
        {
            level = bindModelParam (values[master], model.params[(size_t) master]);
            level.label = "LEVEL";
        }
        encoders[5] = encoder (level, "FX HOME", [this] { showScreen (Screen::Effects, 0); });
        return;
    }
    for (int e = 0; e < 6; ++e)
    {
        const int k = layer * 6 + e;
        if (k < (int) model.params.size())
            encoders[(size_t) e] = encoder (bindModelParam (values[k], model.params[(size_t) k]));
    }
    // Manual p.58: the sixth encoder's push goes back to home.
    encoders[5].pushLabel = "FX HOME";
    encoders[5].push = [this] { showScreen (Screen::Effects, 0); };
}

//==============================================================================
// ROUTING's pages as the console draws them (photographs of an X32, firmware 4.09):
// dark lists in columns, the chosen row filled orange, the cursor framed.

namespace
{
    const juce::Colour kRowDark (0xff1d2029), kBarDark (0xff2a2d36), kPicked (0xff6b3410), kFrame (0xfff29a2e);

    /** The livedaw-x32 block names as the screen prints them. */
    juce::String routPrefix (const juce::String& c, juce::String& rest)
    {
        static const struct { const char* p; const char* name; } table[] = {
            { "UOUT", "User Out " }, { "UIN", "User In " }, { "CARD", "Card " }, { "OUT", "Output " }, { "P16", "P16 " }, { "AuxIN", "Aux In " },
            { "AUX/", "Aux/" }, { "AUX", "Aux " }, { "AN", "Local " }, { "A", "AES50 A" }, { "B", "AES50 B" } };
        for (auto& t : table)
            if (c.startsWith (t.p))
            {
                rest = c.substring ((int) std::strlen (t.p));
                return t.name;
            }
        rest = c;
        return {};
    }

    juce::String routName (const juce::String& c, bool aux = false)
    {
        if (aux && c == "AUX1-4")
            return "Aux Ins";
        juce::String rest;
        const auto name = routPrefix (c, rest);
        const int first = rest.getIntValue();
        const bool bracket = (c.startsWith ("AN") && first > 16)
                             || (c.startsWith ("CARD") && (first > 8 || (aux && rest.fromFirstOccurrenceOf ("-", false, false).getIntValue() > 2)));
        return bracket ? "[" + name + rest + "]" : name + rest;
    }

    /** What channel `i` of a block connected to `choice` carries ("Local In 3"). */
    juce::String routChannel (const juce::String& choice, int i)
    {
        juce::String rest;
        auto name = routPrefix (choice, rest);
        if (name.isEmpty() || ! juce::CharacterFunctions::isDigit (rest[0]))
            return routName (choice);
        if (name == "Local " || name == "Card ")
            name += "In ";
        return name + juce::String (rest.getIntValue() + i);
    }

    juce::String sigName (int s)
    {
        if (s <= 0)
            return "OFF";
        if (s <= 2)
            return s == 1 ? "Main L" : "Main R";
        if (s == 3)
            return "Main M/C";
        if (s <= 19)
            return "MixBus " + juce::String (s - 3);
        if (s <= 25)
            return "Matrix " + juce::String (s - 19);
        if (s <= 57)
            return "DirOut Ch " + juce::String (s - 25);
        if (s <= 65)
            return "DirOut Aux " + juce::String (s - 57);
        if (s <= 73)
            return "DirOut FX " + juce::String ((s - 66) / 2 + 1) + ((s - 66) % 2 != 0 ? "R" : "L");
        return s == 74 ? "Monitor L" : s == 75 ? "Monitor R" : "Talkback";
    }

    const char* const kCategoryNames[] = { "OFF", "Main (LRC)", "Mix Bus", "Matrix", "Direct Out", "Monitor/TB" };

    juce::String tapName (const juce::String& pos)
    {
        if (pos == "POST")
            return "Post Fader";
        if (pos == "PRE")
            return "Pre Fader";
        if (pos == "<-EQ")
            return "PreEQ";
        if (pos == "EQ->")
            return "PostEQ";
        return pos == "IN/LC" ? juce::String ("In/LC") : pos;
    }

    int windowFrom (int sel, int n, int rows) { return juce::jlimit (0, juce::jmax (0, n - rows), sel - (rows - 1)); }

    void drawBar (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, float size)
    {
        g.setColour (kBarDark);
        g.fillRect (r);
        g.setColour (juce::Colours::white);
        g.setFont (type::font (size, true));
        g.drawFittedText (text, r.reduced (3, 0), juce::Justification::centred, 1, 0.7f);
    }

    void drawHead (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text)
    {
        g.setColour (juce::Colour (0xff3b4a7a));
        g.setFont (type::font (10.5f, true));
        g.drawFittedText (text, r.reduced (14, 0), juce::Justification::centred, 2, 0.7f);
        const auto line = r.getBottom() - 1;
        g.drawHorizontalLine (line, (float) r.getX() + 3.0f, (float) r.getX() + 11.0f);
        g.drawHorizontalLine (line, (float) r.getRight() - 11.0f, (float) r.getRight() - 3.0f);
        g.drawVerticalLine (r.getX() + 3, (float) line - 4.0f, (float) line);
        g.drawVerticalLine (r.getRight() - 4, (float) line - 4.0f, (float) line);
    }

    /** Dark rows; `current` filled orange-brown with orange text, `cursor` framed orange. */
    void drawList (juce::Graphics& g, juce::Rectangle<int> r, const juce::StringArray& items, int rowH, int cursor, int current)
    {
        for (int i = 0; i < items.size(); ++i)
        {
            auto row = r.removeFromTop (rowH).reduced (0, 1);
            g.setColour (i == current ? kPicked : kRowDark);
            g.fillRect (row);
            g.setColour (i == current || i == cursor ? kFrame : juce::Colours::white);
            g.setFont (type::font (juce::jmin (12.0f, (float) rowH * 0.78f), true));
            g.drawText (items[i], row.reduced (4, 0), juce::Justification::centredLeft);
            if (i == cursor || i == current)
                g.drawRect (row, 1);
        }
    }

    struct BlockColumn
    {
        juce::String head;
        juce::StringArray names;
        int current = -1, cursor = -1;
    };

    void paintBlockColumns (juce::Graphics& g, juce::Rectangle<int> area, const std::vector<BlockColumn>& cols)
    {
        const int n = (int) cols.size();
        const int rowH = juce::jlimit (8, 20, (area.getHeight() - 18) / 12);
        for (int c = 0; c < n; ++c)
        {
            auto cell = area.withX (area.getX() + c * area.getWidth() / n).withWidth (area.getWidth() / n).reduced (2, 0);
            drawHead (g, cell.removeFromTop (18), cols[(size_t) c].head);
            drawList (g, cell, cols[(size_t) c].names, rowH, cols[(size_t) c].cursor, cols[(size_t) c].current);
        }
    }

    struct OverviewRow
    {
        juce::String number, name, tap;
    };

    void paintOverview (juce::Graphics& g, juce::Rectangle<int> r, const std::vector<OverviewRow>& rows, int first)
    {
        drawBar (g, r.removeFromTop (20), "OUTPUT PATCH OVERVIEW", 10.0f);
        const int rowH = juce::jmax (8, juce::jmin (20, r.getHeight() / 8));
        for (int i = 0; i < 8 && first + i < (int) rows.size(); ++i)
        {
            auto row = r.removeFromTop (rowH).reduced (0, 1);
            const auto& o = rows[(size_t) (first + i)];
            g.setColour (kRowDark);
            g.fillRect (row);
            if (i == 0)
            {
                g.setColour (kFrame);
                g.drawRect (row, 1);
            }
            g.setColour (juce::Colours::white);
            g.setFont (type::font (juce::jmin (12.0f, (float) rowH * 0.78f), true));
            g.drawText (o.number, row.removeFromLeft (28), juce::Justification::centred);
            g.drawText (o.name, row.removeFromLeft (row.getWidth() * 58 / 100), juce::Justification::centredLeft);
            g.drawText (o.tap, row, juce::Justification::centredLeft);
        }
    }

    //==========================================================================
    // The ROUTING tab strip: BLOCK (INPUT, AES A, AES B, CARD, XLR) and PATCH
    // (OUT, AUX, P16, USER), the shown group's name over its tabs.
    struct StripCell
    {
        juce::String text;
        int tab; // the tab it shows
        int kind; // 0 the group's name, 1 a tab, 2 the other group
        juce::Rectangle<int> bounds;
    };

    std::vector<StripCell> routingStrip (juce::Rectangle<int> area, int shown)
    {
        const bool block = shown == 0 || (shown >= 4 && shown <= 7);
        std::vector<StripCell> v;
        int x = area.getX();
        const auto add = [&] (const char* text, int tab, int kind, int w)
        {
            v.push_back ({ text, tab, kind, { x, area.getY(), w, area.getHeight() } });
            x += w;
        };
        if (block)
        {
            add ("BLOCK", -1, 0, 70);
            for (auto [text, tab] : { std::pair<const char*, int> { "INPUT", 0 }, { "AES A", 5 }, { "AES B", 6 }, { "CARD", 4 }, { "XLR", 7 } })
                add (text, tab, 1, 64);
            x += 10;
            add ("PATCH POINTS", 1, 2, 122);
        }
        else
        {
            add ("BLOCK ROUTING", 0, 2, 122);
            x += 10;
            add ("PATCH", -1, 0, 64);
            for (auto [text, tab] : { std::pair<const char*, int> { "OUT", 1 }, { "AUX", 2 }, { "P16", 3 }, { "USER", 8 } })
                add (text, tab, 1, 58);
        }
        return v;
    }
}

Display::Encoder Display::blockEncoder (int i, const juce::String& label, std::function<juce::String()> path, const juce::StringArray& choices, const juce::String& pushLabel)
{
    const int n = choices.size();
    const auto slot = (size_t) i;
    const auto shown = [this, slot, path, &choices] { return pendingBlock[slot] >= 0 ? choices[pendingBlock[slot]] : x32Value (path()); };
    KnobBinding k;
    k.label = label;
    k.get = [shown, &choices, n] { return (float) juce::jmax (0, choices.indexOf (shown())) / (float) (n - 1); };
    k.set = [this, slot, n] (float v) { pendingBlock[slot] = juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * (float) (n - 1)); repaint(); };
    k.text = shown;
    k.steps = (float) n;
    return encoder (k, pushLabel, [this, slot, path, &choices]
    {
        if (pendingBlock[slot] < 0)
            return;
        x32Run (path() + " " + choices[pendingBlock[slot]]);
        pendingBlock[slot] = -1;
    }, [this, slot] { return pendingBlock[slot] >= 0; });
}

void Display::assignOutputEncoders()
{
    auto run = [this] (const juce::String& text)
    {
        int set = 0;
        ctx.engine.getWing().runX32Text (text, set);
        repaint();
    };
    // The OUTPUT PATCH OVERVIEW's scroll (its push assigns nothing here).
    const auto overviewEncoder = [this] (int last)
    {
        KnobBinding k;
        k.label = "Scroll";
        k.get = [this, last] { return (float) juce::jlimit (0, last, overviewFirst) / (float) last; };
        k.set = [this, last] (float v) { overviewFirst = juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * (float) last); repaint(); };
        k.text = [this] { return juce::String (overviewFirst + 1); };
        k.steps = (float) (last + 1);
        return encoder (k, "ASSIGN");
    };
    overviewFirst = 0;
    if (tab >= 4 && tab < 7)
    {
        // card out, AES50-A, AES50-B: what each block of eight outputs plays.
        const auto& t = kBlockTabs[tab - 4];
        for (int b = 0; b < t.blocks; ++b)
        {
            const auto address = "/config/routing/" + juce::String (t.node) + "/" + kNetBlocks[b];
            encoders[(size_t) b] = blockEncoder (b, "Select", [address] { return address; }, kRoutingNet, "CONNECT");
        }
        if (tab == 4)
            encoders[5] = overviewEncoder (24);
        return;
    }
    if (tab == 7)
    {
        // XLR OUT (p.54): the rear panel's XLR outputs 1-16, four at a time.
        for (int b = 0; b < 4; ++b)
        {
            const auto address = "/config/routing/OUT/" + juce::String (kOutBlocks[b]);
            encoders[(size_t) b] = blockEncoder (b, "Select", [address] { return address; }, b % 2 == 0 ? kRoutingOutA : kRoutingOutB, "CONNECT");
        }
        encoders[5] = overviewEncoder (8);
        return;
    }
    if (tab == 3 && iqSetup)
    {
        assignIqEncoders();
        return;
    }
    // out 1-16, aux out, P16 out: the output signals, one chosen at a time.
    const auto& t = kSignalTabs[tab - 1];
    selectedOutput = juce::jlimit (0, t.count - 1, selectedOutput);
    const auto path = [t] (int n) { return signalPath (t, n); };
    KnobBinding pick;
    pick.label = "Select";
    pick.get = [this, t] { return (float) selectedOutput / (float) (t.count - 1); };
    pick.set = [this, t] (float v)
    {
        selectedOutput = juce::roundToInt (v * (float) (t.count - 1));
        pendingOutputSource = -1;
        assignEncoders();
    };
    pick.text = [this, t] { return signalName (t, selectedOutput); };
    pick.steps = (float) t.count;
    encoders[0] = encoder (pick, "NEXT", [this, t] { selectedOutput = (selectedOutput + 1) % t.count; pendingOutputSource = -1; assignEncoders(); });
    encoders[1] = dimmedEncoder (" ", " ", "INVERT"); // the polarity: nothing behind it in Sidecar
    if (juce::String (t.group) == "p16") // the sixth push: iQ SETUP (p.51)
        encoders[5] = encoder ({}, "EDIT IQ SETUP", [this] { iqSetup = true; assignEncoders(); });

    // 3 the source's category, 4 the source in it (push: ASSIGN), 5 the tap
    // point, 6 the delay of an analog output (push: on) (pp.52-53).
    const auto p = path (selectedOutput);
    const auto shown = [this, p] { return pendingOutputSource >= 0 ? pendingOutputSource : x32Value (p + "/src").getIntValue(); };
    KnobBinding category;
    category.label = "Select";
    category.get = [shown] { return (float) categoryOf (shown()) / 5.0f; };
    category.set = [this] (float v)
    {
        pendingOutputSource = kSourceCategories[juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 5.0f)].first;
        assignEncoders();
        repaint();
    };
    category.text = [shown] { return juce::String (kCategoryNames[categoryOf (shown())]); };
    category.steps = 6.0f;
    encoders[2] = encoder (category);
    const auto& c = kSourceCategories[categoryOf (shown())];
    const int span = c.last - c.first;
    KnobBinding signal;
    signal.label = "Select";
    signal.get = [shown, c, span] { return span > 0 ? (float) (shown() - c.first) / (float) span : 0.0f; };
    signal.set = [this, c, span] (float v) { pendingOutputSource = c.first + juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * (float) span); repaint(); };
    signal.text = [shown] { return sigName (shown()); };
    signal.steps = (float) (span + 1);
    encoders[3] = encoder (signal, "ASSIGN", [this, run, p]
    {
        if (pendingOutputSource >= 0)
            run (p + "/src " + juce::String (pendingOutputSource));
        pendingOutputSource = -1;
    }, [this, p] { return pendingOutputSource >= 0 && pendingOutputSource != x32Value (p + "/src").getIntValue(); });
    encoders[4] = encoder (enumKnob ("TAP", p + "/pos", kOutputTaps));
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
        encoders[5] = encoder (delay, "DELAY", [on, run, p] { run (p + "/delay/on " + (on() ? "OFF" : "ON")); }, on);
    }
}

namespace
{
    BlockColumn blockColumn (const juce::String& head, const juce::String& value, int pending, const juce::StringArray& choices, bool aux)
    {
        BlockColumn c;
        c.head = head;
        const int n = choices.size(), now = choices.indexOf (value);
        const int cursor = pending >= 0 ? pending : now;
        const int from = windowFrom (juce::jmax (0, cursor), n, 12);
        for (int i = from; i < juce::jmin (n, from + 12); ++i)
            c.names.add (routName (choices[i], aux));
        c.current = now >= 0 ? now - from : -1;
        c.cursor = cursor >= 0 ? cursor - from : -1;
        return c;
    }

    void deviceBox (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& letter, const juce::String& text)
    {
        g.setColour (juce::Colour (0xff8a96bd));
        g.fillRect (r);
        g.setColour (juce::Colour (0xff6b789f));
        g.drawRect (r, 1);
        g.setColour (juce::Colours::white);
        g.setFont (type::font (11.0f, true));
        g.drawText (letter, r.reduced (4, 2), juce::Justification::topLeft);
        g.setColour (juce::Colour (0xffd8def0));
        g.drawText (text, r, juce::Justification::centred);
    }

    juce::StringArray numbered (const juce::String& prefix, int first, int count)
    {
        juce::StringArray s;
        for (int i = 0; i < count; ++i)
            s.add (prefix + juce::String (first + i));
        return s;
    }
}

void Display::paintOutputs (juce::Graphics& g, juce::Rectangle<int> r)
{
    if (tab == 3 && iqSetup)
    {
        paintIqSetup (g, r);
        return;
    }
    if (tab == 8)
    {
        paintUser (g, r);
        return;
    }
    if (tab >= 1 && tab <= 3)
    {
        paintPatch (g, r);
        return;
    }
    if (tab >= 4 && tab < 7)
    {
        // CARD, AES A, AES B: what each block of eight outputs plays.
        const auto& t = kBlockTabs[tab - 4];
        const bool card = tab == 4;
        auto right = card ? r.removeFromRight (juce::jmin (230, r.getWidth() * 32 / 100)) : juce::Rectangle<int>();
        drawBar (g, r.removeFromTop (22), card ? "EXPANSION CARD  OUTPUTS" : tab == 5 ? "AES50 A OUTPUTS" : "AES50 B OUTPUTS");
        r.removeFromTop (3);
        std::vector<BlockColumn> cols;
        for (int b = 0; b < t.blocks; ++b)
            cols.push_back (blockColumn (kNetBlocks[b], x32Value ("/config/routing/" + juce::String (t.node) + "/" + kNetBlocks[b]), pendingBlock[(size_t) b], kRoutingNet, false));
        paintBlockColumns (g, r, cols);
        if (card)
        {
            right.removeFromLeft (4);
            drawBar (g, right.removeFromTop (22), "CONNECTED DEVICES");
            auto well = right.removeFromTop (juce::jmax (30, right.getHeight() * 28 / 100)).reduced (0, 3);
            g.setColour (juce::Colour (0xff3b4a7a));
            g.setFont (type::font (11.0f, true));
            g.drawFittedText ("NO EXPANSION CARD", well, juce::Justification::centred, 1, 0.7f);
            std::vector<OverviewRow> rows;
            for (int k = 0; k < 32; ++k)
                rows.push_back ({ juce::String (k + 1), routChannel (x32Value ("/config/routing/CARD/" + juce::String (kNetBlocks[k / 8])), k % 8), {} });
            paintOverview (g, right, rows, juce::jlimit (0, 24, overviewFirst));
        }
        return;
    }
    // XLR OUTPUTS: the rear panel's outputs in fours, and what each plays.
    auto right = r.removeFromRight (juce::jmin (250, r.getWidth() * 34 / 100));
    drawBar (g, r.removeFromTop (22), "XLR OUTPUTS");
    r.removeFromTop (3);
    std::vector<BlockColumn> cols;
    for (int b = 0; b < 4; ++b)
        cols.push_back (blockColumn (kOutBlocks[b], x32Value ("/config/routing/OUT/" + juce::String (kOutBlocks[b])), pendingBlock[(size_t) b],
                                     b % 2 == 0 ? kRoutingOutA : kRoutingOutB, false));
    paintBlockColumns (g, r, cols);
    right.removeFromLeft (4);
    std::vector<OverviewRow> rows;
    for (int k = 0; k < 16; ++k)
    {
        const auto p = signalPath (kSignalTabs[0], k);
        rows.push_back ({ juce::String (k + 1), sigName (x32Value (p + "/src").getIntValue()), tapName (x32Value (p + "/pos")) });
    }
    paintOverview (g, right, rows, juce::jlimit (0, 8, overviewFirst));
}

void Display::paintPatch (juce::Graphics& g, juce::Rectangle<int> r)
{
    // PATCH > OUT, AUX, P16: the outputs at the left, the signal one plays in the middle.
    const auto& t = kSignalTabs[tab - 1];
    const bool isMain = juce::String (t.group) == "main";
    const int rowH = juce::jlimit (8, 20, (r.getHeight() - 56) / 12);
    auto left = r.removeFromLeft (juce::jmin (150, r.getWidth() / 5));
    r.removeFromLeft (4);
    drawHead (g, left.removeFromTop (34), isMain ? "ANALOG OUTPUT" : tab == 2 ? "AUX OUTPUT" : "ULTRANET P16 OUTPUT");
    {
        juce::StringArray names;
        const int from = windowFrom (selectedOutput, t.count, 12);
        for (int i = from; i < juce::jmin (t.count, from + 12); ++i)
            names.add (isMain ? "Output " + juce::String (i + 1)
                              : tab == 2 ? (i >= 6 ? (i == 6 ? juce::String ("AES/EBU L") : juce::String ("AES/EBU R")) : "Aux Out " + juce::String (i + 1))
                                         : juce::String (i + 1).paddedLeft ('0', 2) + " @ -");
        drawList (g, left, names, rowH, selectedOutput - from, selectedOutput - from);
    }
    drawBar (g, r.removeFromTop (22), "EDIT OUTPUT ASSIGNMENT");
    r.removeFromTop (3);
    const auto p = signalPath (t, selectedOutput);
    const int actual = x32Value (p + "/src").getIntValue();
    const int shown = pendingOutputSource >= 0 ? pendingOutputSource : actual;
    const int cat = categoryOf (shown);
    const int w = r.getWidth();
    auto setting = r.removeFromLeft (w * 24 / 100).reduced (4, 0);
    auto category = r.removeFromLeft (w * 23 / 100).reduced (4, 0);
    auto signal = r.removeFromLeft (w * 25 / 100).reduced (4, 0);
    drawHead (g, setting.removeFromTop (18), "SETTING");
    for (auto text : { sigName (actual), tapName (x32Value (p + "/pos")) })
    {
        auto box = setting.removeFromTop (rowH + 2).reduced (0, 1);
        g.setColour (juce::Colour (0xff7b88b0));
        g.fillRect (box);
        g.setColour (juce::Colours::white);
        g.setFont (type::font (11.5f, true));
        g.drawFittedText (text, box, juce::Justification::centred, 1, 0.7f);
    }
    {
        auto well = setting.removeFromTop (juce::jmax (30, rowH * 4));
        g.setColour (juce::Colour (0xff4a4d58));
        g.fillRect (well);
        g.setColour (juce::Colour (0xfff6e24a));
        g.fillRect (well.removeFromRight (5).reduced (0, 6));
        g.setColour (juce::Colours::white);
        g.setFont (type::font (11.5f, true));
        g.drawFittedText (sigName (actual), well.removeFromBottom (rowH + 2), juce::Justification::centred, 1, 0.7f);
        setting.removeFromTop (6);
        auto invert = setting.removeFromTop (rowH + 4);
        g.setColour (juce::Colours::white);
        g.drawEllipse (invert.removeFromLeft (rowH).reduced (2).toFloat(), 1.3f);
        g.setFont (type::font (11.0f, true));
        g.drawText ("INVERT", invert.reduced (4, 0), juce::Justification::centredLeft);
    }
    drawHead (g, category.removeFromTop (18), "CATEGORY");
    {
        juce::StringArray names;
        for (auto* n : kCategoryNames)
            names.add (n);
        drawList (g, category, names, rowH, cat, categoryOf (actual));
    }
    drawHead (g, signal.removeFromTop (18), "OUT SIGNAL");
    {
        juce::StringArray names;
        const int from = windowFrom (shown, outputSources().size(), 12);
        for (int i = from; i < juce::jmin (outputSources().size(), from + 12); ++i)
            names.add (sigName (i));
        drawList (g, signal, names, rowH, shown - from, actual - from);
    }
    if (isMain)
    {
        // The output's delay: feet, metres and milliseconds, and its knob.
        auto delay = r.reduced (6, 0);
        const float ms = x32Value (p + "/delay/time").getFloatValue();
        g.setColour (juce::Colour (0xffb8c0d8));
        g.fillRect (delay.removeFromTop (rowH + 2).withWidth (56));
        g.setColour (juce::Colour (0xff3b4a7a));
        g.setFont (type::font (10.5f, true));
        g.drawText ("DELAY", delay.getX(), delay.getY() - rowH - 2, 56, rowH + 2, juce::Justification::centred);
        delay.removeFromTop (6);
        const juce::String values[] = { juce::String (ms * 0.343f * 3.28084f, 1), juce::String (ms * 0.343f, 2), juce::String (ms, 1) };
        const char* const units[] = { "ft", "m", "ms" };
        for (int k = 0; k < 3; ++k)
        {
            auto line = delay.removeFromTop (rowH + 2);
            auto box = line.removeFromLeft (56).reduced (0, 1);
            g.setColour (juce::Colour (0xffe9ecf4));
            g.fillRect (box);
            g.setColour (juce::Colour (0xff22262e));
            g.setFont (type::font (11.0f, false));
            g.drawText (values[k], box.reduced (3, 0), juce::Justification::centredRight);
            g.setColour (juce::Colours::white);
            g.drawText (units[k], line.reduced (3, 0), juce::Justification::centredLeft);
        }
        auto knob = delay.removeFromTop (46).withSizeKeepingCentre (34, 34).toFloat();
        g.setColour (juce::Colour (0xffe9e9ea));
        g.fillEllipse (knob);
        g.setColour (juce::Colour (0xfff29a2e));
        g.drawEllipse (knob.reduced (1.0f), 2.0f);
        const float angle = juce::degreesToRadians (-135.0f + 270.0f * juce::jlimit (0.0f, 1.0f, (ms - 0.3f) / 499.7f));
        const auto c = knob.getCentre();
        g.setColour (juce::Colour (0xff22262e));
        g.drawLine (c.x, c.y, c.x + std::sin (angle) * 12.0f, c.y - std::cos (angle) * 12.0f, 2.0f);
        g.setColour (juce::Colours::white);
        g.setFont (type::font (10.5f, true));
        g.drawText ("DELAY", delay.removeFromTop (14), juce::Justification::centred);
    }
}

void Display::paintUser (juce::Graphics& g, juce::Rectangle<int> r)
{
    // PATCH > USER: the user routing slots (the console's; Sidecar has none, so the page is drawn and inert).
    const int rowH = juce::jlimit (8, 20, (r.getHeight() - 56) / 12);
    auto slots = r.removeFromLeft (juce::jmin (120, r.getWidth() / 7));
    r.removeFromLeft (4);
    drawHead (g, slots.removeFromTop (34), "USER\nSLOT");
    const auto choice = [&] (juce::Rectangle<int> row, const juce::String& text, bool on)
    {
        row = row.reduced (0, 1);
        g.setColour (on ? kYellow : kRowDark);
        g.fillRect (row);
        g.setColour (on ? kNavy : juce::Colour (0xffaab4d4));
        g.setFont (type::font (11.5f, true));
        g.drawText (text, row, juce::Justification::centred);
    };
    choice (slots.removeFromTop (rowH + 4), "INPUT", ! userOutput);
    choice (slots.removeFromTop (rowH + 4), "OUTPUT", userOutput);
    slots.removeFromTop (12);
    for (int b = 0; b < 4; ++b)
        choice (slots.removeFromTop (rowH + 4), juce::String (b * 8 + 1) + "-" + juce::String (b * 8 + 8), b == userBank);
    auto list = r.removeFromLeft (juce::jmin (140, r.getWidth() / 5));
    r.removeFromLeft (4);
    const juce::String side = userOutput ? "Out " : "In ";
    drawHead (g, list.removeFromTop (34), userOutput ? "USER\nOUTPUT" : "USER\nINPUT");
    {
        juce::StringArray names;
        for (int i = 0; i < 12 && userBank * 8 + i < 32; ++i)
            names.add (side + juce::String (userBank * 8 + i + 1) + ": -");
        drawList (g, list, names, rowH, userSlot, userSlot);
    }
    drawBar (g, r.removeFromTop (22), "EDIT USER ROUTING SLOTS");
    r.removeFromTop (3);
    const int w = r.getWidth();
    auto category = r.removeFromLeft (w * 28 / 100).reduced (4, 0);
    auto signal = r.removeFromLeft (w * 28 / 100).reduced (4, 0);
    auto setting = r.removeFromLeft (w * 28 / 100).reduced (4, 0);
    drawHead (g, category.removeFromTop (18), "CATEGORY");
    const juce::String dir = userOutput ? " Out" : " In";
    juce::StringArray categories;
    for (auto text : { juce::String ("OFF"), "Local" + dir, "AES-50 A" + dir, "AES-50 B" + dir, "Card" + dir, juce::String (userOutput ? "Aux Out" : "Aux In"), juce::String (userOutput ? "Monitor" : "Talkback") })
        categories.add (text);
    drawList (g, category, categories, rowH, 0, 0);
    drawHead (g, signal.removeFromTop (18), userOutput ? "OUT SIGNAL" : "IN SIGNAL");
    juce::StringArray names { "OFF" };
    names.addArray (numbered ("Local" + dir + " ", 1, 11));
    drawList (g, signal, names, rowH, 0, 0);
    drawHead (g, setting.removeFromTop (18), "SETTING");
    {
        auto box = setting.removeFromTop (rowH + 2);
        g.setColour (juce::Colour (0xff7b88b0));
        g.drawRect (box, 1);
        g.setColour (juce::Colours::white);
        g.setFont (type::font (11.5f, true));
        g.drawText ("OFF", box, juce::Justification::centred);
    }
}

void Display::assignUserEncoders()
{
    // 1 the bank of eight (push: input or output), 2 the slot (push: next), 3 and 4 the
    // category and signal (Sidecar has no user routing: shown, inert).
    KnobBinding bank;
    bank.label = "Select";
    bank.get = [this] { return (float) userBank / 3.0f; };
    bank.set = [this] (float v) { userBank = juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 3.0f); userSlot = 0; repaint(); };
    bank.text = [this] { return juce::String (userBank * 8 + 1) + "-" + juce::String (userBank * 8 + 8); };
    bank.steps = 4.0f;
    encoders[0] = encoder (bank, "INPUT / OUTPUT", [this] { userOutput = ! userOutput; repaint(); }, [this] { return userOutput; });
    KnobBinding slot;
    slot.label = "Select";
    slot.get = [this] { return (float) userSlot / 11.0f; };
    slot.set = [this] (float v) { userSlot = juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 11.0f); repaint(); };
    slot.text = [this] { return juce::String (userOutput ? "Out " : "In ") + juce::String (userBank * 8 + userSlot + 1); };
    slot.steps = 12.0f;
    encoders[1] = encoder (slot, "NEXT", [this] { userSlot = (userSlot + 1) % 12; repaint(); });
    encoders[2] = dimmedEncoder ("Select", "OFF", " ");
    encoders[3] = dimmedEncoder ("Select", "OFF", "ASSIGN");
}

void Display::paintRouting (juce::Graphics& g, juce::Rectangle<int> r)
{
    // BLOCK > INPUT: what feeds inputs 1-8 .. 25-32 and the aux ins, and the console's side (record or play).
    const bool play = x32Value ("/config/routing/routswitch") == "PLAY";
    const juce::String group = play ? "PLAY" : "IN";
    auto right = r.removeFromRight (juce::jmin (160, r.getWidth() / 6));
    right.removeFromLeft (4);
    drawBar (g, r.removeFromTop (22), "CHANNEL PROCESSING BLOCK PATCH");
    drawBar (g, right.removeFromTop (22), "CONNECTED DEVICES");
    r.removeFromTop (3);
    right.removeFromTop (3);
    std::vector<BlockColumn> cols;
    for (int b = 0; b < 5; ++b)
        cols.push_back (blockColumn (b < 4 ? juce::String (kInBlocks[b]) : juce::String ("AUX IN"), x32Value ("/config/routing/" + group + "/" + kInBlocks[b]),
                                     pendingBlock[(size_t) b], b < 4 ? kRoutingIn : kRoutingAux, b == 4));
    paintBlockColumns (g, r, cols);
    const int half = (right.getHeight() - 4) / 2;
    deviceBox (g, right.removeFromTop (half), "A", "NO DEVICE");
    right.removeFromTop (4);
    deviceBox (g, right, "B", "NO DEVICE");
}

void Display::paintSetupGlobal (juce::Graphics& g, juce::Rectangle<int> r)
{
    // The console's GLOBAL page: firmware, sample rate and clock, reset, backup, time and date, brightness.
    auto* device = ctx.deviceManager.getCurrentAudioDevice();
    auto area = r.reduced (8, 6);
    if (globalStatus.isNotEmpty())
    {
        g.setColour (kDeep);
        g.setFont (type::font (10.0f, true));
        g.drawFittedText (globalStatus, area.removeFromBottom (14), juce::Justification::centredLeft, 1, 0.7f);
    }
    const int w = area.getWidth();
    auto c1 = area.removeFromLeft (w * 17 / 100).reduced (2, 0);
    auto c2 = area.removeFromLeft (w * 20 / 100).reduced (2, 0);
    auto c3 = area.removeFromLeft (w * 19 / 100).reduced (2, 0);
    auto c5 = area.removeFromRight (w * 17 / 100).reduced (2, 0);
    auto c4 = area.reduced (2, 0);
    const int rh = juce::jlimit (10, 20, area.getHeight() / 14);
    const auto item = [&] (juce::Rectangle<int>& c, const juce::String& text, bool hot, bool framed = false)
    {
        auto row = c.removeFromTop (rh).reduced (0, 1);
        g.setColour (hot ? kYellow : kBarDark);
        g.fillRect (row);
        g.setColour (hot ? kNavy : juce::Colour (0xffc8d0ea));
        g.setFont (type::font (juce::jmin (11.5f, (float) rh * 0.75f), true));
        g.drawFittedText (text, row, juce::Justification::centred, 1, 0.7f);
        if (framed)
        {
            g.setColour (kFrame);
            g.drawRect (row, 1);
        }
    };
    const auto sub = [&] (juce::Rectangle<int>& c, const juce::String& text)
    {
        auto row = c.removeFromTop (rh + 2).reduced (0, 1);
        g.setColour (juce::Colour (0xff7b88b0));
        g.fillRect (row);
        g.setColour (juce::Colours::white);
        g.setFont (type::font (11.0f, true));
        g.drawFittedText (text, row, juce::Justification::centred, 1, 0.7f);
    };
    const auto dot = [&] (juce::Rectangle<int> row, bool on)
    {
        auto d = row.removeFromRight (rh).withSizeKeepingCentre (rh - 5, rh - 5).toFloat();
        g.setColour (on ? kFrame : kBarDark.darker());
        g.fillEllipse (d);
        if (on)
        {
            g.setColour (juce::Colours::white);
            g.drawLine (d.getX() + d.getWidth() * 0.25f, d.getCentreY(), d.getCentreX() - 0.5f, d.getBottom() - 3.0f, 1.5f);
            g.drawLine (d.getCentreX() - 0.5f, d.getBottom() - 3.0f, d.getRight() - 2.5f, d.getY() + 3.0f, 1.5f);
        }
    };
    // FIRMWARE: Sidecar's own version; its surface (X32, X32 Compact) below, the one the first encoder switches.
    drawBar (g, c1.removeFromTop (rh + 6), "FIRMWARE");
    {
        auto box = c1.removeFromTop (rh * 5);
        g.setColour (juce::Colour (0xff23283a));
        g.fillRect (box);
        g.setFont (type::font (9.5f, true));
        g.setColour (juce::Colours::white);
        auto line = box.removeFromTop (rh + 2);
        g.drawText ("VERSION", line.removeFromLeft (line.getWidth() * 52 / 100).reduced (3, 0), juce::Justification::centredRight);
        g.setColour (kFrame);
        g.drawFittedText (juce::String (JUCE_APPLICATION_VERSION_STRING), line.reduced (4, 0), juce::Justification::centredLeft, 1, 0.6f);
        g.setColour (juce::Colour (0xffc8d0ea));
        for (auto text : { juce::String ("SIDECAR"), juce::String (getModel && getModel() == 1 ? "X32 COMPACT" : "X32") })
            g.drawText (text, box.removeFromTop (rh), juce::Justification::centred);
    }
    c1.removeFromTop (4);
    drawBar (g, c1.removeFromTop (rh + 6), "SYS CONTROL");
    item (c1, "SHUTDOWN", true);
    item (c1, "REBOOT", false);
    item (c1, "UPDATE FW", false);
    // AUDIO SETTINGS: the rate (the interface's rates) and the clock.
    drawBar (g, c2.removeFromTop (rh + 6), "AUDIO SETTINGS");
    sub (c2, "SAMPLE RATE");
    {
        const int n = device != nullptr ? device->getAvailableSampleRates().size() : 0;
        for (int k = 0; k < n; ++k)
        {
            const auto rate = device->getAvailableSampleRates()[k];
            const auto text = juce::String (rate / 1000.0, rate == 44100.0 ? 1 : 0) + " kHz";
            auto row = c2.removeFromTop (rh).reduced (0, 1);
            g.setColour (juce::Colour (0xff23283a));
            g.fillRect (row);
            g.setColour (juce::Colours::white);
            g.setFont (type::font (11.5f, true));
            g.drawText (text, row.reduced (6, 0), juce::Justification::centredLeft);
            const bool now = rate == device->getCurrentSampleRate();
            dot (row, now);
            if (k == globalRate)
            {
                g.setColour (kFrame);
                g.drawRect (row, 1);
            }
        }
        if (n == 0)
            item (c2, "No interface", false);
    }
    c2.removeFromTop (rh * 2);
    sub (c2, "CLOCK SOURCE");
    {
        int k = 0;
        for (auto text : { "Internal", "AES50 A", "AES50 B", "Exp. Card" })
        {
            auto row = c2.removeFromTop (rh).reduced (0, 1);
            g.setColour (juce::Colour (0xff23283a));
            g.fillRect (row);
            g.setColour (juce::Colours::white);
            g.setFont (type::font (11.0f, true));
            g.drawText (text, row.reduced (6, 0), juce::Justification::centredLeft);
            dot (row, k++ == 0);
        }
    }
    // RESET, BACKUP, CUSTOM BOOT.
    drawBar (g, c3.removeFromTop (rh + 6), "RESET");
    {
        int k = 0;
        for (auto text : { "CONSOLE", "SHOW DATA", "LIBRARIES", "FACTORY RST" })
        {
            item (c3, text, k == initItem, k == initArmedItem);
            ++k;
        }
    }
    c3.removeFromTop (6);
    drawBar (g, c3.removeFromTop (rh + 6), "BACKUP");
    item (c3, "EXPORT", false);
    item (c3, "RESTORE", false);
    c3.removeFromTop (6);
    drawBar (g, c3.removeFromTop (rh + 6), "CUSTOM BOOT");
    item (c3, "CREATE", false);
    // TIME | DATE: the computer's.
    drawBar (g, c4.removeFromTop (rh + 6), "TIME | DATE");
    {
        const auto now = juce::Time::getCurrentTime();
        const int h12 = now.getHours() % 12 == 0 ? 12 : now.getHours() % 12;
        const juce::String time[][2] = { { now.getHours() >= 12 ? "HOUR  PM" : "HOUR  AM", juce::String (h12).paddedLeft ('0', 2) },
                                         { "MIN", juce::String (now.getMinutes()).paddedLeft ('0', 2) },
                                         { "SEC", juce::String (now.getSeconds()).paddedLeft ('0', 2) } };
        const juce::String date[][2] = { { "DAY", juce::String (now.getDayOfMonth()).paddedLeft ('0', 2) },
                                         { "MONTH", juce::String (now.getMonth() + 1).paddedLeft ('0', 2) }, { "YEAR", juce::String (now.getYear()) } };
        auto icons = c4.removeFromTop (rh * 2);
        auto left = c4.removeFromLeft (c4.getWidth() / 2).reduced (3, 0);
        auto rightHalf = c4.reduced (3, 0);
        g.setColour (juce::Colour (0xff3b4a7a));
        g.drawEllipse (icons.removeFromLeft (icons.getWidth() / 2).withSizeKeepingCentre (rh + 4, rh + 4).toFloat(), 1.6f);
        g.drawRect (icons.withSizeKeepingCentre (rh + 4, rh + 2), 2);
        const int boxH = rh * 2;
        for (int k = 0; k < 3; ++k)
            for (int side = 0; side < 2; ++side)
            {
                auto& col = side == 0 ? left : rightHalf;
                auto box = col.removeFromTop (boxH).reduced (0, 1);
                const auto& cell = side == 0 ? time[k] : date[k];
                g.setColour (juce::Colour (0xff3a4258));
                g.fillRect (box);
                g.setColour (juce::Colour (0xffc8d0ea));
                g.setFont (type::font (8.5f, true));
                g.drawText (cell[0], box.reduced (4, 0), juce::Justification::topLeft);
                g.setColour (juce::Colours::white);
                g.setFont (type::font ((float) boxH * 0.55f, true));
                g.drawText (cell[1], box.reduced (5, 0), juce::Justification::centredRight);
            }
        g.setColour (juce::Colour (0xff3b4a7a));
        g.setFont (type::font (8.5f, true));
        g.drawFittedText ("PUSH TO EDIT", left.removeFromTop (rh), juce::Justification::centred, 1, 0.6f);
        g.drawFittedText ("PUSH TO EDIT", rightHalf.removeFromTop (rh), juce::Justification::centred, 1, 0.6f);
    }
    // The three knobs: the screen's and the LEDs' brightness, the channel LCDs' contrast (the strips have none here).
    const auto knob = [&] (juce::Rectangle<int> cell, const juce::String& name, float v, bool framed)
    {
        if (framed)
        {
            g.setColour (kFrame);
            g.drawRect (cell, 2);
        }
        auto k = cell.reduced (2).removeFromTop (cell.getHeight() * 55 / 100).withSizeKeepingCentre (30, 30).toFloat();
        g.setColour (juce::Colour (0xffe9e9ea));
        g.fillEllipse (k);
        g.setColour (kFrame);
        g.drawEllipse (k.reduced (1.0f), 2.5f);
        const float angle = juce::degreesToRadians (-135.0f + 270.0f * juce::jlimit (0.0f, 1.0f, v));
        g.setColour (juce::Colour (0xff22262e));
        g.drawLine (k.getCentreX(), k.getCentreY(), k.getCentreX() + std::sin (angle) * 10.0f, k.getCentreY() - std::cos (angle) * 10.0f, 2.0f);
        g.setColour (juce::Colour (0xff1c2b55));
        g.setFont (type::font (10.0f, true));
        g.drawFittedText (name, cell.reduced (2).withTrimmedTop (cell.getHeight() * 56 / 100), juce::Justification::centredTop, 2, 0.7f);
    };
    const int third = c5.getHeight() / 3;
    knob (c5.removeFromTop (third).reduced (0, 2), "MAIN LCD\nBRIGHTNESS", (x32Value ("/-prefs/bright").getFloatValue() - 10.0f) / 90.0f, ! brightnessLed);
    knob (c5.removeFromTop (third).reduced (0, 2), "LED\nBRIGHTNESS", (x32Value ("/-prefs/ledbright").getFloatValue() - 10.0f) / 90.0f, brightnessLed);
    knob (c5.reduced (0, 2), "CHANNEL LCD\nCONTRAST", 0.5f, false);
}


void Display::assignRoutingEncoders()
{
    if (tab == 8)
    {
        assignUserEncoders();
        return;
    }
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
        // Turned through the list, connected by the push (p.48).
        encoders[(size_t) b] = blockEncoder (b, "Select", path, choices, "CONNECT");
    }
    KnobBinding sw;
    sw.label = "Inputs";
    sw.get = [group] { return group() == "PLAY" ? 1.0f : 0.0f; };
    sw.set = [this] (float v)
    {
        int set = 0;
        ctx.engine.getWing().runX32Text (v >= 0.5f ? "/config/routing/routswitch PLAY" : "/config/routing/routswitch REC", set);
        repaint();
    };
    sw.text = [group] { return group() == "PLAY" ? juce::String ("Play") : juce::String ("Rec"); };
    sw.steps = 2.0f;
    encoders[5] = encoder (sw, "RECORD / PLAY", [this, group]
    {
        pendingBlock.fill (-1);
        int set = 0;
        ctx.engine.getWing().runX32Text (group() == "PLAY" ? "/config/routing/routswitch REC" : "/config/routing/routswitch PLAY", set);
        assignEncoders();
    }, [group] { return group() == "PLAY"; });
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
    publishRemotePrefs();
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
                publishRemotePrefs();
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
        publishRemotePrefs();
        repaint();
    };
    KnobBinding b;
    b.label = "OSC SYSEX";
    b.get = [sysexOn] { return sysexOn() ? 1.0f : 0.0f; };
    b.set = [setSysex] (float v) { setSysex (v >= 0.5f); };
    b.text = [sysexOn] { return sysexOn() ? juce::String ("ON") : juce::String ("OFF"); };
    b.steps = 2.0f;
    encoders[0] = encoder (b, "SYSEX", [sysexOn, setSysex] { setSysex (! sysexOn()); }, sysexOn);
    // 2 the DAW protocol Sidecar's DAW remote speaks (OFF, MCU or HUI), 5 the port it
    // uses (USB's three MIDI DAW ports, or the DIN socket's one).
    auto midi = [this] { return ctx.midi != nullptr ? ctx.midi->getSettings() : MidiRemote::Settings::load (ctx.settings); };
    auto change = [this, midi] (const std::function<void (MidiRemote::Settings&)>& edit)
    {
        auto s = midi();
        edit (s);
        midiui::applySettings (ctx, s);
        assignEncoders();
        repaint();
    };
    const auto protocol = [midi]
    {
        const auto s = midi();
        return ! s.daw.enabled ? 0 : s.daw.emulation == DawRemote::Emulation::Hui ? 2 : 1;
    };
    const auto setProtocol = [change] (int p)
    {
        change ([p] (MidiRemote::Settings& s)
        {
            s.daw.enabled = p != 0;
            if (p != 0)
                s.daw.emulation = p == 2 ? DawRemote::Emulation::Hui : DawRemote::Emulation::Mcu;
        });
    };
    KnobBinding proto;
    proto.label = "PROTOCOL";
    proto.get = [protocol] { return (float) protocol() / 2.0f; };
    proto.set = [setProtocol] (float v) { setProtocol (juce::roundToInt (v * 2.0f)); };
    proto.text = [protocol] { return juce::StringArray { "OFF", "MACKIE MCU", "HUI" }[protocol()]; };
    proto.steps = 3.0f;
    encoders[1] = encoder (proto, "REMOTE", [protocol, setProtocol] { setProtocol ((protocol() + 1) % 3); }, [protocol] { return protocol() != 0; });
    KnobBinding port;
    port.label = "PORT";
    port.get = [midi] { return midi().daw.usb ? 1.0f : 0.0f; };
    port.set = [change] (float v) { change ([v] (MidiRemote::Settings& s) { s.daw.usb = v >= 0.5f; }); };
    port.text = [midi] { return midi().daw.usb ? juce::String ("USB") : juce::String ("DIN"); };
    port.steps = 2.0f;
    encoders[4] = encoder (port, "SET", [midi, change] { change ([usb = midi().daw.usb] (MidiRemote::Settings& s) { s.daw.usb = ! usb; }); },
                           [midi] { return midi().daw.usb; });
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
    const auto daw = (ctx.midi != nullptr ? ctx.midi->getSettings() : MidiRemote::Settings::load (settings)).daw;
    auto mode = [bits] (int rx)
    {
        const bool in = (bits & rx) != 0, out = (bits & (rx << 1)) != 0;
        return in && out ? juce::String ("RX + TX") : in ? juce::String ("RX") : out ? juce::String ("TX") : juce::String ("OFF");
    };
    paintRows (g, r, { "MIDI IN", "MIDI OUT", "RECEIVED / SENT", "SCENES, SNIPPETS (PC, CH 1, 2)", "FADERS (CC, CH 1)",
                       "MUTES, MUTE GROUPS (CC, CH 2)", "PANS (CC, CH 3)", "OSC OVER SYSEX", "DAW PROTOCOL", "DAW PORT" },
               { device (true), device (false), juce::String (status.received) + " / " + juce::String (status.sent), mode (X32Midi::RxProgram),
                 mode (X32Midi::RxFader), mode (X32Midi::RxMute), mode (X32Midi::RxPan), (bits & X32Midi::SysEx) != 0 ? "ON" : "OFF",
                 ! daw.enabled ? "OFF" : daw.emulation == DawRemote::Emulation::Hui ? "HUI" : "MACKIE MCU", daw.usb ? "USB" : "DIN" });
}

//==============================================================================
// What the console's own preferences and status hold (`/-prefs`, `/-stat`: the
// names and ranges of the X32 OSC document, pp.50-59), so that the screen and
// a remote agree.

void Display::x32Run (const juce::String& text)
{
    int set = 0;
    ctx.engine.getWing().runX32Text (text, set);
    repaint();
}

int Display::x32Int (const juce::String& address, int fallback) const
{
    const auto value = x32Value (address);
    return value.isEmpty() ? fallback : value.getIntValue();
}

KnobBinding Display::intKnob (const juce::String& label, const juce::String& path, int first, int count, std::function<juce::String (int)> text)
{
    KnobBinding k;
    k.label = label;
    const int last = juce::jmax (1, count - 1);
    k.get = [this, path, first, last] { return (float) juce::jlimit (0, last, x32Int (path, first) - first) / (float) last; };
    k.set = [this, path, first, last] (float v) { x32Run (path + " " + juce::String (first + juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * (float) last))); };
    k.text = [this, path, first, text] { return text (x32Int (path, first)); };
    k.steps = (float) juce::jmax (2, count);
    return k;
}

int Display::x32Index (const Strip& s) const
{
    const auto address = x32Address (s);
    const int n = address.fromLastOccurrenceOf ("/", false, false).getIntValue();
    if (address.startsWith ("/ch/"))
        return n - 1;
    if (address.startsWith ("/auxin/"))
        return 31 + n;
    if (address.startsWith ("/fxrtn/"))
        return 39 + n;
    if (address.startsWith ("/bus/"))
        return 47 + n;
    if (address.startsWith ("/mtx/"))
        return 63 + n;
    if (address == "/main/st")
        return 70;
    if (address == "/main/m")
        return 71;
    return -1;
}

Strip* Display::stripAtX32Index (int index) const
{
    if (index < 0)
        return nullptr;
    for (auto* s : recallStrips())
        if (x32Index (*s) == index)
            return s;
    return nullptr;
}

void Display::publishScreen()
{
    // `/-stat/screen` (p.57): the screen, its page, MUTE GRP over it; Sidecar's own
    // UTILITY pages as `utils`.
    int set = 0;
    auto& wing = ctx.engine.getWing();
    const int muteGrp = screen == Screen::MuteGroups ? 1 : 0;
    if (muteGrp != publishedMuteGrp)
    {
        publishedMuteGrp = muteGrp;
        wing.runX32Text (juce::String ("/-stat/screen/mutegrp ") + (muteGrp != 0 ? "ON" : "OFF"), set);
    }
    const auto st = statScreen (screen == Screen::MuteGroups ? underScreen : screen);
    if (st.number < 0)
    {
        if (publishedScreen != -2)
            wing.runX32Text ("/-stat/screen/utils ON", set);
        publishedScreen = -2;
        return;
    }
    if (publishedScreen == -2)
        wing.runX32Text ("/-stat/screen/utils OFF", set);
    const int page = shownTab();
    if (st.number != publishedScreen)
        wing.runX32Text ("/-stat/screen/screen " + juce::String (st.number), set);
    wing.runX32Text ("/-stat/screen/" + juce::String (st.node) + "/page " + juce::String (page), set);
    publishedScreen = st.number;
    publishedPage = page;
}

void Display::followRemote()
{
    // What a remote set since the surface last wrote it: the screen and its page,
    // the selected strip, the EQ's band, the assign set. And for it: the recorder.
    if (x32Value ("/-stat/screen/screen").isEmpty())
        return; // the console isn't an X32
    int set = 0;
    auto& wing = ctx.engine.getWing();
    if (publishedScreen >= 0)
    {
        const int wantScreen = x32Int ("/-stat/screen/screen", publishedScreen);
        if (wantScreen != publishedScreen)
        {
            bool found = false;
            for (auto s : { Screen::Home, Screen::Meters, Screen::Routing, Screen::Setup, Screen::Library, Screen::Effects, Screen::Monitor, Screen::Usb,
                            Screen::Scenes, Screen::Assign })
                if (const auto st = statScreen (s); st.number == wantScreen)
                {
                    found = true;
                    showScreen (s, x32Int ("/-stat/screen/" + juce::String (st.node) + "/page", 0));
                    break;
                }
            if (! found) // the lock screen: none here
                wing.runX32Text ("/-stat/screen/screen " + juce::String (publishedScreen), set);
            return;
        }
        const auto st = statScreen (screen == Screen::MuteGroups ? underScreen : screen);
        if (st.number >= 0)
            if (const int wantPage = x32Int ("/-stat/screen/" + juce::String (st.node) + "/page", publishedPage); wantPage != publishedPage)
            {
                showTab (wantPage);
                return;
            }
    }
    if (const int sel = x32Int ("/-stat/selidx", publishedSel); sel != publishedSel)
    {
        publishedSel = sel;
        if (auto* strip = stripAtX32Index (sel); strip != nullptr && ctx.selectStrip)
            ctx.selectStrip (strip);
    }
    if (screen == Screen::Home && tab == 4)
        if (const int band = x32Int ("/-stat/eqband", publishedBand); band != publishedBand && publishedBand >= 0)
        {
            auto* s = selected();
            static constexpr int four[] = { 0, 0, 1, 2, 3, 3 };
            eqBand = s != nullptr && s->isOutput() ? juce::jlimit (0, 5, band) : four[juce::jlimit (0, 5, band)];
            assignEncoders(); // writes the band it took
        }
    if (getAssignSet && setAssignSet)
    {
        const int here = getAssignSet(), there = x32Int ("/-stat/userbank", here);
        if (here != publishedBank)
        {
            publishedBank = here;
            wing.runX32Text ("/-stat/userbank " + juce::String (juce::jlimit (0, 2, here)), set);
        }
        else if (there != publishedBank)
        {
            publishedBank = there;
            setAssignSet (there);
            if (screen == Screen::Assign)
                showTab (tab == 0 ? 0 : there + 1);
        }
    }
    // The recorder: 0 stop, 1 pause, 2 play, 3 record paused, 4 record; the seconds played and left.
    auto& usb = ctx.engine.getUsbRecorder();
    using P = UsbRecorder::PlayState;
    using R = UsbRecorder::RecState;
    const auto rec = usb.getRecState();
    const auto play = usb.getPlayState();
    const int tape = rec == R::Recording ? 4 : rec == R::Paused ? 3 : play == P::Playing ? 2 : play == P::Paused ? 1 : 0;
    if (tape != publishedTape)
    {
        publishedTape = tape;
        wing.runX32Text ("/-stat/tape/state " + juce::String (tape), set);
    }
    const double rate = juce::jmax (1.0, usb.getSampleRate());
    const int seconds = tape >= 3 ? (int) ((double) usb.getRecTime() / rate) : (int) ((double) usb.getPosition() / rate);
    if (tape != 0 && seconds != publishedTapeSeconds)
    {
        publishedTapeSeconds = seconds;
        const int left = tape >= 3 ? 0 : (int) ((double) juce::jmax ((int64_t) 0, usb.getLength() - usb.getPosition()) / rate);
        wing.runX32Text ("/-stat/tape/etime " + juce::String (juce::jlimit (0, 359999, seconds)), set);
        wing.runX32Text ("/-stat/tape/rtime " + juce::String (juce::jlimit (0, 359999, left)), set);
    }
}

void Display::publishRemotePrefs()
{
    // `/-prefs/remote/ioenable` (p.51): bit 0 MIDI In/Out, 3-10 Rx and Tx of program
    // changes, faders, mutes and pans, 11 X/OSC (OSC over SysEx): the app's X32 MIDI settings.
    const int bits = ctx.settings.getIntValue (X32Midi::enableKey, X32Midi::All);
    const int io = 1 | ((bits & 0xff) << 3) | ((bits & X32Midi::SysEx) != 0 ? 1 << 11 : 0);
    if (io != publishedMidi)
    {
        publishedMidi = io;
        int set = 0;
        ctx.engine.getWing().runX32Text ("/-prefs/remote/ioenable " + juce::String (io), set);
    }
}

void Display::followRemotePrefs()
{
    const auto text = x32Value ("/-prefs/remote/ioenable");
    if (text.isEmpty() || publishedMidi < 0)
        return;
    const int io = x32Bits (text);
    if (io == publishedMidi)
        return;
    publishedMidi = io;
    const int bits = ((io >> 3) & 0xff) | ((io & (1 << 11)) != 0 ? (int) X32Midi::SysEx : 0);
    ctx.settings.setValue (X32Midi::enableKey, bits);
    if (auto* m = x32Midi())
        m->setEnabled (bits);
    assignEncoders();
}

float Display::lcdBrightness() const
{
    const auto value = x32Value ("/-prefs/bright");
    return value.isEmpty() ? 1.0f : juce::jlimit (0.1f, 1.0f, value.getFloatValue() / 100.0f);
}

//==============================================================================
// GAIN: the head amp or the trim

bool Display::gainIsHeadAmp (const Strip& s) const
{
    const int input = s.params.inputLeft.load();
    if (input < 0 || input >= 128 || ctx.engine.getSource (input) == nullptr)
        return false;
    // HA gain split (`/-prefs/haflags`: bit 1 the local inputs', 2 and 3 AES50 A's and
    // B's): the strip's GAIN is its own trim, the head amp is SETUP > preamps'.
    const int split = x32Bits (x32Value ("/-prefs/haflags"));
    return (split & (input < 32 ? 2 : input < 80 ? 4 : 8)) == 0;
}

KnobBinding Display::gainBinding (Strip& s) const
{
    const auto address = x32Address (s);
    if (! (address.startsWith ("/ch/") || address.startsWith ("/auxin/")))
        return {};
    if (gainIsHeadAmp (s))
    {
        auto b = bindLinear ("GAIN", ctx.engine.getSource (s.params.inputLeft.load())->gainDb, -12.0f, 60.0f, " dB", 1, 0.0f);
        b.steps = 145.0f;
        return b;
    }
    return bindLinear ("GAIN", s.params.input.trimDb, -18.0f, 18.0f, " dB", 1, 0.0f);
}

//==============================================================================
// The RTA (manual 7.3, 7.2.5)

Strip* Display::rtaStrip() const
{
    const int source = x32Int ("/-prefs/rta/source", 0);
    if (source >= 2)
        if (auto* s = stripAtX32Index (source - 2))
            return s;
    return selected();
}

void Display::setRtaOption (int option, bool on)
{
    const int bits = x32Bits (x32Value ("/-prefs/rta/option"));
    x32Run ("/-prefs/rta/option " + juce::String (on ? bits | (1 << option) : bits & ~(1 << option)));
    static const char* const twins[] = { "/-stat/rtaeqpre", "/-stat/rtamodeeq", nullptr, "/-stat/rtageqpost", "/-stat/rtamodegeq", nullptr };
    if (juce::isPositiveAndBelow (option, 6) && twins[option] != nullptr)
        x32Run (juce::String (twins[option]) + (option == 1 || option == 4 ? (on ? " SPEC" : " BAR") : (on ? " ON" : " OFF")));
    updateRta();
}

void Display::updateRta()
{
    // METERS > rta analyses its source; HOME > eq the selected strip, while the overlay
    // is visible (`/-prefs/rta/visibility`). PRE: the strip's input; POST: after its EQ.
    auto analysis = ctx.engine.getAnalysis();
    const auto shown = screen == Screen::MuteGroups ? underScreen : screen;
    const int t = shownTab();
    const bool page = shown == Screen::Meters && t == 4;
    const auto visibility = x32Value ("/-prefs/rta/visibility");
    const bool overlay = shown == Screen::Home && t == 4 && visibility.isNotEmpty() && visibility != "OFF";
    if (page || overlay)
    {
        const bool pre = page ? x32Value ("/-prefs/rta/pos") == "PRE" : x32On ("/-stat/rtaeqpre");
        analysis.settings (ld::kRtaViewHome).tap = pre ? 0 : 1;
        analysis.showRta (page ? rtaStrip() : selected(), ld::kRtaViewHome);
        rtaShown = true;
    }
    else if (rtaShown)
    {
        analysis.hideRta();
        rtaShown = false;
        rtaHistory.clear();
    }
}

void Display::assignRtaEncoders()
{
    // METERS > rta (manual 7.3): 1 the six check boxes, pushed on and off; 2 bar graph or
    // spectrograph; 4 the gain by hand, its push AUTO GAIN; 5 the EQ overlay's visibility,
    // its push before or after the EQ; 6 the source, SET by its push. The third encoder is
    // Sidecar's placing: the decay (`/-prefs/rta/decay`, the document's), its push FREEZE.
    auto& engine = ctx.engine;
    auto& rta = engine.getAnalysis().settings (ld::kRtaViewHome);
    KnobBinding option;
    option.label = "SELECT";
    option.get = [this] { return (float) rtaOption / 5.0f; };
    option.set = [this] (float v) { rtaOption = juce::jlimit (0, 5, juce::roundToInt (v * 5.0f)); repaint(); };
    option.text = [this] { return rtaOptions()[rtaOption]; };
    option.steps = 6.0f;
    const auto checked = [this] { return (x32Bits (x32Value ("/-prefs/rta/option")) & (1 << rtaOption)) != 0; };
    encoders[0] = encoder (option, "SET", [this, checked] { setRtaOption (rtaOption, ! checked()); }, checked);

    const auto spec = [this] { return x32Value ("/-prefs/rta/mode") == "SPEC"; };
    KnobBinding mode;
    mode.label = "DISPLAY";
    mode.get = [spec] { return spec() ? 1.0f : 0.0f; };
    mode.set = [this] (float v) { x32Run (v >= 0.5f ? "/-prefs/rta/mode SPEC" : "/-prefs/rta/mode BAR"); };
    mode.text = [spec] { return spec() ? juce::String ("Spectro") : juce::String ("Bars"); };
    mode.steps = 2.0f;
    encoders[1] = encoder (mode, "SPECTROGRAPH", [this, spec] { x32Run (spec() ? "/-prefs/rta/mode BAR" : "/-prefs/rta/mode SPEC"); }, spec);

    KnobBinding decay;
    decay.label = "DECAY";
    decay.get = [this] { return juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (0.25f, x32Value ("/-prefs/rta/decay").getFloatValue()) / 0.25f) / std::log (64.0f)); };
    decay.set = [this, &rta] (float v)
    {
        const float time = 0.25f * std::pow (64.0f, juce::jlimit (0.0f, 1.0f, v));
        x32Run ("/-prefs/rta/decay " + juce::String (time, 2));
        rta.decay = time < 0.7f ? 2 : time < 3.0f ? 1 : 0; // the engine's FAST, MED and SLOW
    };
    decay.text = [this] { return x32Value ("/-prefs/rta/decay"); };
    decay.steps = 19.0f;
    encoders[2] = encoder (decay, "FREEZE", [&engine] { auto a = engine.getAnalysis(); a.setFrozen (! a.isFrozen()); },
                           [&engine] { return engine.getAnalysis().isFrozen(); });

    KnobBinding gain;
    gain.label = "GAIN";
    gain.get = [&rta] { return juce::jlimit (0.0f, 1.0f, rta.gainDb.load() / 60.0f); };
    gain.set = [this, &rta] (float v)
    {
        const float db = 6.0f * (float) juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 10.0f);
        rta.gainDb = db;
        x32Run ("/-prefs/rta/gain " + juce::String (juce::roundToInt (db)));
    };
    gain.text = [&rta] { return rta.autogain.load() ? juce::String ("Auto") : juce::String (juce::roundToInt (rta.gainDb.load())) + " dB"; };
    gain.steps = 11.0f;
    encoders[3] = encoder (gain, "AUTO GAIN", [this, &rta]
    {
        rta.autogain = ! rta.autogain.load();
        x32Run (rta.autogain.load() ? "/-prefs/rta/autogain ON" : "/-prefs/rta/autogain OFF");
    }, isOn (rta.autogain));

    const auto post = [this] { return x32Value ("/-prefs/rta/pos") == "POST"; };
    encoders[4] = encoder (enumKnob ("EQ OVERLAY", "/-prefs/rta/visibility", rtaVisibilities()), "POST EQ",
                           [this, post] { x32Run (post() ? "/-prefs/rta/pos PRE" : "/-prefs/rta/pos POST"); updateRta(); }, post);

    // The source: the selected channel (the document's 0, "none", and 1, the monitor,
    // whose path Sidecar's analyser doesn't take), or a fixed strip.
    const auto current = [this] { return juce::jlimit (0, 73, x32Int ("/-prefs/rta/source", 0)); };
    if (rtaSourcePick < 0)
        rtaSourcePick = current();
    KnobBinding source;
    source.label = "SOURCE";
    source.get = [this] { return (float) rtaSourcePick / 73.0f; };
    source.set = [this] (float v) { rtaSourcePick = juce::jlimit (0, 73, juce::roundToInt (v * 73.0f)); repaint(); };
    source.text = [this]
    {
        if (rtaSourcePick < 2)
            return juce::String (rtaSourcePick == 0 ? "Selected" : "Monitor");
        auto* strip = stripAtX32Index (rtaSourcePick - 2);
        return strip != nullptr ? strip->getLabel() : juce::String (rtaSourcePick);
    };
    source.steps = 74.0f;
    encoders[5] = encoder (source, "SET", [this]
    {
        x32Run ("/-prefs/rta/source " + juce::String (rtaSourcePick));
        updateRta();
    }, [this, current] { return current() == rtaSourcePick; });
}

namespace
{
    /** The RTA's gain as drawn: by hand, or (AUTO GAIN) what lifts the loudest band to 6 dB under the top. */
    float rtaGainFor (const ld::RtaViewSettings& rta, const float* levels, int count)
    {
        if (! rta.autogain.load())
            return rta.gainDb.load();
        float top = -144.0f;
        for (int k = 0; k < count; ++k)
            top = juce::jmax (top, levels[k]);
        return top > -120.0f ? juce::jlimit (0.0f, 60.0f, -6.0f - top) : 0.0f;
    }
}

void Display::paintRtaBars (juce::Graphics& g, juce::Rectangle<float> area, float alpha)
{
    // A bar a band (20 Hz to 20 kHz across), its peak a line over it.
    Analysis::Bands bands;
    ctx.engine.getAnalysis().read (bands);
    const float gain = rtaGainFor (ctx.engine.getAnalysis().settings (ld::kRtaViewHome), bands.peaks.data(), Analysis::numBands);
    const auto xOf = [&area] (float hz) { return area.getX() + area.getWidth() * juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (20.0f, hz) / 20.0f) / std::log (1000.0f)); };
    const auto yOf = [&area, gain] (float db) { return area.getBottom() - area.getHeight() * juce::jlimit (0.0f, 1.0f, (db + gain + 90.0f) / 90.0f); };
    for (int k = 0; k < Analysis::numBands; ++k)
    {
        const float x0 = xOf (Analysis::bandLowHz (k)), x1 = xOf (Analysis::bandHighHz (k));
        if (x1 <= x0)
            continue;
        const float w = juce::jmax (1.0f, x1 - x0 - 0.5f);
        const float y = yOf (bands.levels[(size_t) k]);
        g.setColour (juce::Colour (0xffff9a1f).withAlpha (alpha));
        g.fillRect (juce::Rectangle<float> (x0, y, w, area.getBottom() - y));
        g.setColour (kSky.withAlpha (alpha));
        g.fillRect (juce::Rectangle<float> (x0, yOf (bands.peaks[(size_t) k]) - 1.0f, w, 2.0f));
    }
}

void Display::paintSpectrograph (juce::Graphics& g, juce::Rectangle<float> area, float alpha)
{
    // The last ten seconds, the newest at the bottom: a row a timer tick, a column a
    // band, the level as a colour from blue (quiet) to red.
    constexpr int rows = 150;
    const int n = juce::jmin (rows, (int) rtaHistory.size());
    if (n == 0 || area.isEmpty())
        return;
    const auto& newest = rtaHistory.back();
    const float gain = rtaGainFor (ctx.engine.getAnalysis().settings (ld::kRtaViewHome), newest.data(), (int) newest.size());
    juce::Image image (juce::Image::RGB, Analysis::numBands, rows, true);
    {
        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
        for (int r = 0; r < n; ++r)
        {
            const auto& row = rtaHistory[rtaHistory.size() - (size_t) n + (size_t) r];
            const int y = rows - n + r;
            for (int k = 0; k < Analysis::numBands && k < (int) row.size(); ++k)
            {
                const float v = juce::jlimit (0.0f, 1.0f, (row[(size_t) k] + gain + 90.0f) / 90.0f);
                data.setPixelColour (k, y, juce::Colour::fromHSV (0.66f * (1.0f - v), 1.0f, juce::jmin (1.0f, 0.1f + 1.3f * v), 1.0f));
            }
        }
    }
    g.setOpacity (alpha);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (image, area);
    g.setOpacity (1.0f);
}

void Display::paintRta (juce::Graphics& g, juce::Rectangle<int> r)
{
    // The analyser on top, over its grid (20 Hz to 20 kHz, 6 dB steps); under it the
    // defaults' boxes: the channel EQ's, the graphic EQ's and the RTA source's.
    auto area = r.reduced (8, 4).toFloat();
    const int bits = x32Bits (x32Value ("/-prefs/rta/option"));
    auto defaults = area.removeFromBottom (juce::jmin (62.0f, area.getHeight() * 0.32f));
    area.removeFromBottom (3.0f);
    g.setColour (juce::Colour (0xff05070a));
    g.fillRect (area);
    {
        const auto plot = area.reduced (24.0f, 6.0f).withTrimmedBottom (8.0f);
        const auto xOf = [&plot] (float hz) { return plot.getX() + plot.getWidth() * std::log10 (hz / 20.0f) / 3.0f; };
        g.setColour (juce::Colour (0xff3f5f8a));
        g.setFont (type::font (7.5f, false));
        for (float decade : { 10.0f, 100.0f, 1000.0f })
            for (int m = 1; m <= 9; ++m)
            {
                const float hz = decade * (float) m;
                if (hz < 20.0f || hz > 20000.0f)
                    continue;
                g.drawVerticalLine (juce::roundToInt (xOf (hz)), plot.getY(), plot.getBottom());
                if (m == 1 || m == 2 || m == 4 || m == 6 || m == 8 || hz == 30.0f || hz == 50.0f)
                    g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                                juce::Rectangle<float> (xOf (hz) - 10.0f, plot.getBottom(), 20.0f, 8.0f), juce::Justification::centred);
            }
        g.drawVerticalLine (juce::roundToInt (xOf (20000.0f)), plot.getY(), plot.getBottom());
        for (int k = 1; k <= 5; ++k)
        {
            const float y = plot.getY() + plot.getHeight() * (float) k / 6.0f;
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.drawText ("-" + juce::String (6 * k), juce::Rectangle<float> (area.getX() + 1.0f, y - 5.0f, 22.0f, 10.0f), juce::Justification::centredRight);
        }
        if (x32Value ("/-prefs/rta/mode") == "SPEC")
            paintSpectrograph (g, plot, 1.0f);
        else
            paintRtaBars (g, plot, 1.0f);
        auto* strip = rtaStrip();
        g.setColour (juce::Colour (0xff6fb4ff));
        g.setFont (type::font (10.0f, true));
        g.drawText (strip != nullptr ? strip->getLabel() : juce::String ("Monitor"), plot.withHeight (12.0f), juce::Justification::topRight);
    }
    // The defaults: a dark title over each box's check boxes (the ringed one is the encoder's).
    const float third = defaults.getWidth() / 3.0f;
    const auto box = [&] (juce::Rectangle<float> c, const juce::String& title, std::vector<int> items)
    {
        drawBar (g, c.removeFromTop (14.0f).toNearestInt(), title, 9.0f);
        const float colW = c.getWidth() / 2.0f;
        int k = 0;
        for (int item : items)
        {
            auto cell = juce::Rectangle<float> (c.getX() + colW * (float) (k % 2), c.getY() + 15.0f * (float) (k / 2), colW, 15.0f);
            ++k;
            if (item == rtaOption)
            {
                g.setColour (kFrame);
                g.drawRect (cell, 1.0f);
            }
            g.setColour (juce::Colour (0xff1c2b55));
            checkBox (g, cell.reduced (1.0f), rtaOptions()[item].toUpperCase(), (bits & (1 << item)) != 0);
        }
    };
    box (defaults.removeFromLeft (third).reduced (3.0f, 0.0f), "CHANNEL EQ DEFAULTS", { 0, 1 });
    box (defaults.removeFromLeft (third).reduced (3.0f, 0.0f), "GEQ DEFAULTS", { 3, 2, 4 });
    {
        auto c = defaults.reduced (3.0f, 0.0f);
        box (c.removeFromLeft (c.getWidth() * 0.55f), "RTA SOURCE", { 5 });
        auto list = c.withTrimmedTop (16.0f).reduced (2.0f, 0.0f);
        auto* strip = rtaStrip();
        for (auto text : { juce::String ("Selected Ch"), strip != nullptr ? strip->getLabel() : juce::String ("Monitor") })
        {
            auto row = list.removeFromTop (14.0f).reduced (0.0f, 1.0f).toNearestInt();
            g.setColour (text == "Selected Ch" ? kRowDark : kPicked);
            g.fillRect (row);
            g.setColour (text == "Selected Ch" ? juce::Colours::white : kFrame);
            g.setFont (type::font (10.0f, true));
            g.drawText (text, row.reduced (3, 0), juce::Justification::centredLeft);
        }
    }
}

void Display::paintAutomix (juce::Graphics& g, juce::Rectangle<int> r)
{
    // METERS > automix: the information, and the eight strips it works on (the engine has no
    // automix: the page is drawn, its groups OFF and its controls inert).
    auto area = r.reduced (8, 4);
    auto strips = area.removeFromRight (juce::jmin (300, area.getWidth() * 46 / 100));
    area.removeFromRight (6);
    auto info = area.removeFromTop (area.getHeight() - 32);
    g.setColour (juce::Colours::white);
    g.setFont (type::font (10.5f, true));
    g.drawText ("AUTOMIX INFORMATION", info.removeFromTop (14).withTrimmedLeft (18), juce::Justification::centredLeft);
    g.setFont (type::font (9.0f, false));
    g.drawFittedText ("Automix is a feature that helps you control levels of several channels without having to move any fader.\n\n"
                      "The Automix algorithm detects the levels of the assigned channels, and adjusts their volume to keep a constant general level. "
                      "The Weight parameter can be used to adjust the priority of each channel in that group. The meter shows the applied level reduction.\n\n"
                      "This feature is available on channels 1 to 8",
                      info.withTrimmedLeft (18), juce::Justification::topLeft, 12, 0.8f);
    drawBar (g, area.removeFromTop (14).withWidth (juce::jmin (170, area.getWidth())), "AUTOMIX GROUP ENABLE", 8.5f);
    {
        auto keys = area.removeFromTop (14);
        g.setColour (juce::Colour (0xff1c3be0));
        g.fillRect (keys.removeFromLeft (30).translated (14, 0));
        g.setColour (juce::Colour (0xff22d9d0));
        g.fillRect (keys.withX (keys.getX() + 80).withWidth (30));
    }
    const int n = 8;
    const float w = (float) strips.getWidth() / (float) n;
    const auto& channels = ctx.engine.getChannels();
    const auto row = [&] (juce::Rectangle<int> rows, bool group)
    {
        for (int i = 0; i < n; ++i)
        {
            auto cell = juce::Rectangle<float> ((float) strips.getX() + w * (float) i, (float) rows.getY(), w, (float) rows.getHeight()).reduced (1.0f, 0.0f);
            g.setColour (kRowDark);
            g.fillRect (cell);
            auto* s = i < (int) channels.size() ? channels[(size_t) i].get() : nullptr;
            auto label = cell.removeFromBottom (12.0f);
            auto bar = cell.removeFromRight (cell.getWidth() * 0.45f).reduced (1.0f, 2.0f);
            const float level = group ? 0.7f : s != nullptr ? juce::jlimit (0.0f, 1.0f, fader::dbToPos (juce::Decibels::gainToDecibels (ctx.meters.get (s, MeterPoint::Output, 0), -144.0f))) : 0.0f;
            for (float yy = bar.getBottom(); yy > bar.getY() + 2.0f; yy -= 3.0f)
            {
                const float up = (bar.getBottom() - yy) / bar.getHeight();
                g.setColour (up > level ? juce::Colour (0xff2a2d36)
                                        : group ? juce::Colour (0xff1fb8d0)
                                                : up < fader::dbToPos (-18.0f) ? juce::Colour (0xff3ddc5a) : up < 0.95f ? juce::Colour (0xfff2d51f) : juce::Colour (0xffff3b30));
                g.fillRect (bar.getX(), yy - 2.0f, bar.getWidth(), 2.0f);
            }
            const float pos = s != nullptr ? juce::jlimit (0.0f, 1.0f, fader::dbToPos (s->params.faderDb.load())) : 0.5f;
            const float y = cell.getBottom() - cell.getHeight() * pos;
            g.setColour (juce::Colour (0xffdfe3ee));
            g.fillRect (cell.getX() + 2.0f, y - 3.0f, cell.getWidth() - 4.0f, 6.0f);
            g.setColour (juce::Colours::white);
            g.setFont (type::font (10.0f, true));
            g.drawText (group ? juce::String ("OFF") : juce::String (i + 1), label.toNearestInt(), juce::Justification::centred);
        }
    };
    drawBar (g, strips.removeFromTop (14), "INPUT CHANNELS", 9.0f);
    row (strips.removeFromTop (juce::jmax (30, strips.getHeight() * 38 / 100)), false);
    strips.removeFromTop (4);
    drawBar (g, strips.removeFromTop (14), "AUTOMIX GROUP/WEIGHT", 9.0f);
    row (strips, true);
    g.setColour (kFrame);
    g.drawRect (juce::Rectangle<float> ((float) strips.getX(), (float) strips.getY(), w * 4.0f, (float) strips.getHeight()), 1.5f);
}

void Display::assignAutomixEncoders()
{
    KnobBinding scroll;
    scroll.label = "Scroll";
    scroll.get = [this] { return overviewFirst >= 4 ? 1.0f : 0.0f; };
    scroll.set = [this] (float v) { overviewFirst = v >= 0.5f ? 4 : 0; repaint(); };
    scroll.text = [this] { return overviewFirst >= 4 ? juce::String ("5..8") : juce::String ("1..4"); };
    scroll.steps = 2.0f;
    encoders[0] = encoder (scroll, "ACTIVE");
    encoders[1] = dimmedEncoder (" ", " ", "ACTIVE");
    for (int k = 2; k < 6; ++k)
        encoders[(size_t) k] = dimmedEncoder ("Auto Weight", "+0.0 dB", "X / Y");
}

void Display::paintOverChildren (juce::Graphics& g)
{
    // The RTA over the channel EQ (`/-prefs/rta/visibility`, 25 to 80 %), as bars or as
    // the spectrograph (an X32 draws it behind the curve; here it lies over the graph).
    if (eqView.isVisible() && rtaShown)
    {
        const float alpha = juce::jlimit (0.0f, 0.8f, x32Value ("/-prefs/rta/visibility").getFloatValue() / 100.0f) * 0.6f;
        if (alpha > 0.0f)
        {
            const auto area = eqView.getBounds().toFloat();
            if (x32Value ("/-stat/rtamodeeq") == "SPEC")
                paintSpectrograph (g, area, alpha);
            else
                paintRtaBars (g, area, alpha);
        }
    }
    // The screen's brightness (`/-prefs/bright`, SETUP > global).
    const float dark = 1.0f - lcdBrightness();
    if (dark > 0.01f)
    {
        g.setColour (juce::Colours::black.withAlpha (dark * 0.75f));
        g.fillRect (screenArea);
    }
}

//==============================================================================
// SETUP's pages

void Display::assignSetupGlobalEncoders()
{
    // GLOBAL's right half (manual 7.5.1): 4 INITIALIZE, the item turned, pushed twice (the
    // first push arms it); 5 the time and date, the computer's; 6 the brightness of the
    // screen or of the LEDs (`/-prefs/bright`, `ledbright`), its push between the two.
    static const juce::StringArray initItems { "Console", "Show Data", "Libraries", "Factory Rst" };
    KnobBinding init;
    init.label = "Select";
    init.get = [this] { return (float) initItem / 3.0f; };
    init.set = [this] (float v)
    {
        initItem = juce::jlimit (0, 3, juce::roundToInt (v * 3.0f));
        initArmedItem = -1;
        globalStatus = {};
        repaint();
    };
    init.text = [this] { return initItems[initItem]; };
    init.steps = 4.0f;
    encoders[2] = encoder (init, "SELECT", [this] { initialize (initItem); }, [this] { return initArmedItem == initItem; });
    // The clock is the computer's: a readout, nothing to set.
    encoders[3] = encoder (readout ("Time", [] { return juce::Time::getCurrentTime().formatted ("%H:%M"); }));
    encoders[4] = encoder (readout ("Date", [] { return juce::Time::getCurrentTime().formatted ("%d.%m.%Y"); }));
    const auto path = [this] { return juce::String (brightnessLed ? "/-prefs/ledbright" : "/-prefs/bright"); };
    KnobBinding bright;
    bright.label = "Bright";
    bright.get = [this, path] { return juce::jlimit (0.0f, 1.0f, (x32Value (path()).getFloatValue() - 10.0f) / 90.0f); };
    bright.set = [this, path] (float v) { x32Run (path() + " " + juce::String (10 + 5 * juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 18.0f))); };
    bright.text = [this, path] { return x32Value (path()); };
    bright.steps = 19.0f;
    encoders[5] = encoder (bright, "SELECT", [this] { brightnessLed = ! brightnessLed; repaint(); }, [this] { return brightnessLed; });
}

void Display::initialize (int item)
{
    if (item == 3)
    {
        initArmedItem = -1;
        globalStatus = "Factory reset: not in Sidecar (initialize the console and its libraries instead)";
        repaint();
        return;
    }
    if (item == 2)
    {
        // Sidecar's library is a folder of the user's files: nothing is deleted from here.
        initArmedItem = -1;
        globalStatus = "Libraries: the library folder's files stay (delete them there)";
        repaint();
        return;
    }
    if (initArmedItem != item)
    {
        initArmedItem = item;
        globalStatus = juce::String ("Push INIT again to initialize ") + (item == 0 ? "every strip of the console" : "the show data");
        repaint();
        return;
    }
    initArmedItem = -1;
    if (item == 0)
    {
        // Every strip back to an X32's factory values (its source stays), as UTILITY's INIT does one.
        int done = 0;
        for (auto* strip : recallStrips())
            if (const auto address = x32Address (*strip); address.isNotEmpty() && ctx.engine.getWing().initX32Channel (address))
                ++done;
        globalStatus = done > 0 ? "Initialized " + juce::String (done) + " strips" : juce::String ("The console isn't an X32");
        refreshStrip();
    }
    else
    {
        globalStatus = "Show data: a new show";
        if (ctx.newShow)
            ctx.newShow(); // asks before it drops unsaved changes
    }
    repaint();
}

void Display::assignNetworkEncoders()
{
    // NETWORK (manual 7.5.4): 1 the field, 2-5 its four numbers, the sixth push applies
    // them. Kept as `/-prefs/ip` for a remote that reads them; the computer's own
    // address is set in its network settings and shown beside them.
    static const juce::StringArray fields { "IP Address", "Subnet Mask", "Gateway" };
    KnobBinding field;
    field.label = "SELECT";
    field.get = [this] { return (float) netField / 2.0f; };
    field.set = [this] (float v)
    {
        netField = juce::jlimit (0, 2, juce::roundToInt (v * 2.0f));
        netPending.fill (-1);
        assignEncoders();
    };
    field.text = [this] { return fields[netField]; };
    field.steps = 3.0f;
    encoders[0] = encoder (field);
    const juce::String node = juce::String ("/-prefs/ip/") + kIpNodes[juce::jlimit (0, 2, netField)] + "/";
    for (int k = 0; k < 4; ++k)
    {
        const auto value = [this, node, k] { return netPending[(size_t) k] >= 0 ? netPending[(size_t) k] : x32Int (node + juce::String (k), 0); };
        KnobBinding b;
        b.label = "FIELD " + juce::String (k + 1);
        b.get = [value] { return (float) value() / 255.0f; };
        b.set = [this, k] (float v) { netPending[(size_t) k] = juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 255.0f); repaint(); };
        b.text = [value] { return juce::String (value()); };
        b.steps = 256.0f;
        encoders[(size_t) k + 1] = encoder (b);
    }
    encoders[5] = encoder ({}, "APPLY", [this, node]
    {
        for (int k = 0; k < 4; ++k)
            if (netPending[(size_t) k] >= 0)
                x32Run (node + juce::String (k) + " " + juce::String (netPending[(size_t) k]));
        netPending.fill (-1);
    }, [this] { return std::any_of (netPending.begin(), netPending.end(), [] (int v) { return v >= 0; }); });
}

void Display::assignIqEncoders()
{
    // iQ SETUP (manual 7.4.4): 1 the P16 channel, 3 the speaker on it, 4 its EQ preset, 5
    // the sound profile it models, 6 back (2, the group, is the document's). Kept as
    // `/outputs/p16/NN/iQ`: no Turbosound iQ speaker listens on a computer's P16.
    selectedOutput = juce::jlimit (0, 15, selectedOutput);
    KnobBinding pick;
    pick.label = "P16";
    pick.get = [this] { return (float) selectedOutput / 15.0f; };
    pick.set = [this] (float v)
    {
        selectedOutput = juce::jlimit (0, 15, juce::roundToInt (v * 15.0f));
        assignEncoders();
    };
    pick.text = [this] { return "P16 " + juce::String (selectedOutput + 1); };
    pick.steps = 16.0f;
    encoders[0] = encoder (pick);
    const juce::String node = "/outputs/p16/" + two (selectedOutput + 1) + "/iQ/";
    static const juce::StringArray groups { "OFF", "A", "B" };
    static const juce::StringArray eqs { "Linear", "Live", "Speech", "Playback", "User" };
    encoders[1] = encoder (enumKnob ("GROUP", node + "group", groups));
    encoders[2] = encoder (enumKnob ("SPEAKER", node + "speaker", iqSpeakers()));
    encoders[3] = encoder (enumKnob ("EQ PRESET", node + "eq", eqs));
    encoders[4] = encoder (intKnob ("SOUND", node + "model", 0, 8, [] (int v) { return "Profile " + juce::String (v + 1); }));
    encoders[5] = encoder ({}, "IQ SETUP", [this] { iqSetup = false; assignEncoders(); }, [] { return true; });
}

void Display::paintIqSetup (juce::Graphics& g, juce::Rectangle<int> r)
{
    // The sixteen P16 channels in two columns: each one's speaker, EQ preset and profile.
    auto columns = r;
    for (int c = 0; c < 2; ++c)
    {
        auto area = columns.removeFromLeft (c == 0 ? r.getWidth() / 2 : columns.getWidth()).reduced (c == 0 ? 0 : 4, 0);
        juce::StringArray left, right;
        for (int i = 8 * c; i < 8 * c + 8; ++i)
        {
            const juce::String node = "/outputs/p16/" + two (i + 1) + "/iQ/";
            const auto speaker = x32Value (node + "speaker");
            left.add ((i == selectedOutput ? "> P16 " : "P16 ") + juce::String (i + 1));
            right.add (speaker == "none" || speaker.isEmpty() ? juce::String ("no iQ speaker")
                                                              : speaker + "  " + x32Value (node + "eq") + "  profile " + juce::String (x32Int (node + "model", 0) + 1));
        }
        paintRows (g, area, left, right);
    }
}

//==============================================================================
// USB > config: the recorder's inputs

void Display::assignUsbConfigEncoders()
{
    // Manual 7.11.2: 2 the recorder's side, left or right (the X32's input trim has no
    // counterpart in Sidecar's recorder); 3 the category and 4 the signal that side
    // records; 5 its tap point. `/outputs/rec/01` and `02` keep them, and the app's
    // recorder takes the signal (a monitor signal or the talkback: nothing).
    const juce::String p = "/outputs/rec/" + two (usbRecSide + 1);
    KnobBinding side;
    side.label = usbRecSide == 0 ? "INPUT LEFT" : "INPUT RIGHT";
    encoders[1] = encoder (side, "L / R", [this] { usbRecSide = 1 - usbRecSide; assignEncoders(); }, [this] { return usbRecSide == 1; });
    const int last = outputSources().size() - 1;
    const auto shown = [this, p, last] { return juce::jlimit (0, last, x32Int (p + "/src", 0)); };
    const auto assign = [this, p] (int source)
    {
        x32Run (p + "/src " + juce::String (source));
        UsbRecorder::Tap tap;
        const auto strip = [&tap] (StripKind kind, int number, int channel)
        {
            tap.kind = UsbRecorder::Tap::Kind::Strip;
            tap.stripKind = kind;
            tap.stripNumber = number;
            tap.channel = channel;
        };
        if (source == 1 || source == 2)
            strip (StripKind::Main, 1, source - 1);
        else if (source == 3)
            strip (StripKind::Main, 2, 0);
        else if (source >= 4 && source <= 19)
            strip (StripKind::Bus, source - 3, 0);
        else if (source >= 20 && source <= 25)
            strip (StripKind::Matrix, source - 19, 0);
        else if (source >= 26 && source <= 57)
            strip (StripKind::Channel, source - 25, 0);
        else if (source >= 58 && source <= 73)
            strip (StripKind::Aux, source - 57, 0); // aux in 1-8, then the FX returns
        ctx.engine.getUsbRecorder().setTap (usbRecSide, tap);
    };
    KnobBinding category;
    category.label = "CATEGORY";
    category.get = [shown] { return (float) categoryOf (shown()) / 5.0f; };
    category.set = [this, assign, shown] (float v)
    {
        const int to = juce::jlimit (0, 5, juce::roundToInt (v * 5.0f));
        if (to != categoryOf (shown()))
            assign (kSourceCategories[to].first);
        assignEncoders();
    };
    category.text = [shown] { return juce::String (kSourceCategories[categoryOf (shown())].name); };
    category.steps = 6.0f;
    encoders[2] = encoder (category);
    const auto c = kSourceCategories[categoryOf (shown())];
    const int span = c.last - c.first;
    KnobBinding signal;
    signal.label = "SOURCE";
    signal.get = [shown, c, span] { return span > 0 ? (float) (shown() - c.first) / (float) span : 0.0f; };
    signal.set = [assign, c, span] (float v) { assign (c.first + juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * (float) span)); };
    signal.text = [shown] { return outputSources()[shown()]; };
    signal.steps = (float) (span + 1);
    encoders[3] = encoder (signal);
    encoders[4] = encoder (enumKnob ("TAP", p + "/pos", kOutputTaps));
}

//==============================================================================
// SCENES > snippets (manual 7.13.3)

std::vector<Strip*> Display::snippetStrips() const
{
    std::vector<Strip*> out;
    for (auto* strip : recallStrips())
        if (std::find (snippetChecked.begin(), snippetChecked.end(), strip) != snippetChecked.end())
            out.push_back (strip);
    if (out.empty())
        if (auto* strip = selected())
            out.push_back (strip);
    return out;
}

int Display::saveSnippet (int slot, const juce::String& name)
{
    auto& show = ctx.show;
    const auto scene = show.getScene (selectedScene);
    if (! scene.has_value())
    {
        sceneStatus = "No scene to keep the snippet in";
        repaint();
        return -1;
    }
    if (juce::isPositiveAndBelow (slot, scene->numSnippets))
    {
        // An existing one: the console into it, as it was scoped.
        show.storeSnippet (selectedScene, slot);
        if (name.isNotEmpty())
            show.setSnippetName (selectedScene, slot, name);
        selectedSnippet = slot;
        sceneStatus = "Saved snippet " + two (slot + 1);
    }
    else
    {
        const auto strips = snippetStrips();
        if (strips.empty())
        {
            sceneStatus = "Select a strip, or check the channels to save";
            repaint();
            return -1;
        }
        std::vector<state::StripRef> refs;
        for (auto* strip : strips)
            refs.push_back (state::StripRef::of (*strip));
        const auto sections = snippetScope != 0 ? snippetScope : state::AllSections & ~(juce::uint32) state::Routing;
        selectedSnippet = show.addSnippet (selectedScene, name.isNotEmpty() ? name : strips.front()->getLabel(), sections, refs);
        snippetSlot = selectedSnippet;
        sceneStatus = "Saved snippet " + two (selectedSnippet + 1);
    }
    assignEncoders();
    repaint();
    return selectedSnippet;
}

void Display::assignSnippetEncoders()
{
    // The selected scene's snippets (an X32's belong to the show, Sidecar's to a
    // scene): 1 a snippet, LOAD; 2 a slot, SAVE (a new one asks its name); 3-5 what a
    // new one holds: the parameters, the inputs and the outputs, each checked by its
    // push (nothing checked: every parameter but the routing, of the selected strip).
    // Layer 2, Sidecar's own: FIRE (recalled with its scene) and DELETE.
    auto& show = ctx.show;
    const auto refresh = [this] { assignEncoders(); repaint(); };
    const auto scene = show.getScene (selectedScene);
    const int snippets = scene.has_value() ? scene->numSnippets : 0;
    const auto ok = [this, &show]
    {
        const auto sc = show.getScene (selectedScene);
        return sc.has_value() && juce::isPositiveAndBelow (selectedSnippet, sc->numSnippets);
    };
    const auto name = [this, &show, snippets]
    {
        const auto snip = snippets > 0 ? show.getSnippet (selectedScene, selectedSnippet) : std::optional<Show::Snippet>();
        return snip.has_value() ? two (selectedSnippet + 1) + " " + snip->name : juce::String ("-");
    };
    if (layer == 1)
    {
        encoders[0] = encoder (pickKnob ("SNIPPET", selectedSnippet, snippets, name, refresh), "FIRE", [this, &show, ok]
        {
            if (ok())
                show.setSnippetFireWithScene (selectedScene, selectedSnippet, ! show.getSnippet (selectedScene, selectedSnippet)->fireWithScene);
            repaint();
        }, [this, &show, ok] { return ok() && show.getSnippet (selectedScene, selectedSnippet)->fireWithScene; });
        encoders[1] = encoder ({}, "DELETE", [this, &show, ok]
        {
            if (ok())
                show.removeSnippet (selectedScene, selectedSnippet);
            assignEncoders();
        });
        return;
    }
    encoders[0] = encoder (pickKnob ("SNIPPET", selectedSnippet, snippets, name, refresh), "LOAD",
                           [this, &show, ok] { if (ok()) show.recallSnippet (selectedScene, selectedSnippet); repaint(); });
    encoders[1] = encoder (pickKnob ("SLOT", snippetSlot, snippets + 1, [this, snippets] { return two (snippetSlot + 1) + (snippetSlot >= snippets ? " (NEW)" : ""); }, refresh),
                           "SAVE", [this, snippets]
    {
        if (snippetSlot < snippets)
        {
            saveSnippet (snippetSlot, {});
            return;
        }
        const auto strips = snippetStrips();
        auto* w = new juce::AlertWindow ("SAVE SNIPPET", "Snippet " + two (snippetSlot + 1) + ":", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", strips.empty() ? juce::String ("Snippet") : strips.front()->getLabel(), "Name:");
        w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<Display> safe (this);
        const int slot = snippetSlot;
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, slot] (int result)
        {
            if (result != 0 && safe != nullptr)
                safe->saveSnippet (slot, w->getTextEditorContents ("name").trim());
        }), true);
    });
    encoders[2] = encoder (pickKnob ("PARAMETERS", snippetRows[0], state::kNumSections,
                                     [this] { return juce::String (state::sectionName (state::sectionAt (snippetRows[0]))).toUpperCase(); }, [this] { repaint(); }),
                           "CHECK", [this] { snippetScope ^= (juce::uint32) state::sectionAt (snippetRows[0]); repaint(); },
                           [this] { return (snippetScope & (juce::uint32) state::sectionAt (snippetRows[0])) != 0; });
    std::vector<Strip*> inputs, outputs;
    for (auto* strip : recallStrips())
        (strip->isInput() ? inputs : outputs).push_back (strip);
    const auto column = [this] (const char* label, int& row, std::vector<Strip*> list)
    {
        const auto checked = [this, &row, list]
        {
            return ! list.empty() && std::find (snippetChecked.begin(), snippetChecked.end(), list[(size_t) juce::jlimit (0, (int) list.size() - 1, row)]) != snippetChecked.end();
        };
        return encoder (pickKnob (label, row, (int) list.size(), [&row, list] { return list.empty() ? juce::String ("-") : list[(size_t) juce::jlimit (0, (int) list.size() - 1, row)]->getLabel(); },
                                  [this] { repaint(); }),
                        "CHECK", [this, &row, list]
        {
            if (list.empty())
                return;
            const Strip* strip = list[(size_t) juce::jlimit (0, (int) list.size() - 1, row)];
            const auto at = std::find (snippetChecked.begin(), snippetChecked.end(), strip);
            if (at != snippetChecked.end())
                snippetChecked.erase (at);
            else
                snippetChecked.push_back (strip);
            repaint();
        }, checked);
    };
    encoders[3] = column ("INPUTS", snippetRows[1], inputs);
    encoders[4] = column ("OUTPUTS", snippetRows[2], outputs);
}

void Display::paintSnippets (juce::Graphics& g, juce::Rectangle<int> r)
{
    // The selected scene's snippets at the left (FIRE marks those recalled with it),
    // the scope a new one takes beside them: a column of check boxes an encoder.
    auto& show = ctx.show;
    auto area = r.reduced (10);
    if (sceneStatus.isNotEmpty())
    {
        g.setColour (kDeep);
        g.setFont (type::font (10.0f));
        g.drawFittedText (sceneStatus, area.removeFromBottom (14), juce::Justification::centredLeft, 1, 0.8f);
    }
    auto listArea = area.removeFromLeft (area.getWidth() * 2 / 5);
    area.removeFromLeft (6);
    juce::StringArray left, right;
    const auto scene = show.getScene (selectedScene);
    left.add ("SCENE " + two (selectedScene + 1));
    right.add (scene.has_value() ? scene->name : juce::String ("-"));
    const int n = scene.has_value() ? scene->numSnippets : 0;
    for (int k = 0; k < n; ++k)
        if (const auto snip = show.getSnippet (selectedScene, k))
        {
            left.add ((k == selectedSnippet ? "> " : "  ") + two (k + 1) + (snip->fireWithScene ? "  FIRE" : ""));
            right.add (snip->name);
        }
    if (n == 0)
    {
        left.add ("No snippets");
        right.add ("SAVE makes one");
    }
    paintRows (g, listArea, left, right);

    std::vector<SafeColumn> columns { { "Parameters", {} }, { "Inputs", {} }, { "Outputs", {} } };
    for (int k = 0; k < state::kNumSections; ++k)
    {
        const auto bit = (juce::uint32) state::sectionAt (k);
        columns[0].items.push_back ({ juce::String (state::sectionName (state::sectionAt (k))), [this, bit] { return (snippetScope & bit) != 0; }, {} });
    }
    for (auto* strip : recallStrips())
        columns[strip->isInput() ? 1 : 2].items.push_back (
            { strip->getLabel(), [this, strip] { return std::find (snippetChecked.begin(), snippetChecked.end(), strip) != snippetChecked.end(); }, {} });
    paintSafeColumns (g, area.toFloat(), columns, snippetRows.data(), "Nothing checked: every parameter but the routing, of the selected strip.");
}

//==============================================================================
// EFFECTS: the home tab's slots, a device's face, EQ on faders

juce::String Display::geqBandName (int band)
{
    static const char* const names[] = { "20",  "25",  "31.5", "40",  "50",  "63",   "80",  "100", "125", "160", "200",
                                         "250", "315", "400",  "500", "630", "800",  "1k",  "1k25", "1k6", "2k",  "2k5",
                                         "3k15", "4k", "5k",   "6k3", "8k",  "10k",  "12k5", "16k", "20k" };
    return names[juce::jlimit (0, 30, band)];
}

bool Display::isGraphicEq (int slot) const
{
    auto& fx = ctx.engine.getFx();
    if (! juce::isPositiveAndBelow (slot, fx.getNumSlots()))
        return false;
    const int m = fx.getModel (slot);
    if (m <= ld::kFxExt)
        return false;
    const auto& token = models::model (models::Fx, m).token;
    return token == "X32GEQ2" || token == "X32GEQ" || token == "X32TEQ2" || token == "X32TEQ";
}

std::vector<int> Display::geqBands (int slot) const
{
    std::vector<int> bands;
    if (! isGraphicEq (slot))
        return bands;
    const auto& model = models::model (models::Fx, ctx.engine.getFx().getModel (slot));
    for (int k = 0; k < (int) model.params.size(); ++k)
        if (model.params[(size_t) k].id.startsWith ("band_"))
            bands.push_back (k);
    return bands;
}

void Display::showGeqOnFaders (int slot, int firstBand)
{
    geqSlot = slot >= 0 && isGraphicEq (slot) ? slot : -1;
    geqFirst = juce::jlimit (0, 27, firstBand);
    // The faders take eight bands (the last window starts at the 24th); a dual EQ's side
    // is the one its page shows.
    const int window = juce::jmin (23, geqFirst);
    int set = 0;
    auto& wing = ctx.engine.getWing();
    wing.runX32Text (juce::String ("/-stat/geqonfdr ") + (geqSlot >= 0 ? "ON" : "OFF"), set);
    if (geqSlot >= 0)
        wing.runX32Text ("/-stat/geqpos " + juce::String (((geqSlot + 1) << 8) | window), set);
    const bool sideB = geqSlot >= 0 && geqBands (geqSlot).size() > 31 && screen == Screen::Effects && tab == geqSlot + 1 && layer == 1;
    if (setGeqOnFaders)
        setGeqOnFaders (geqSlot, (sideB ? 31 : 0) + window);
    assignEncoders();
    repaint();
}

void Display::paintEffectsHome (juce::Graphics& g, juce::Rectangle<int> r)
{
    // The eight slots, four to a row: each one's sources (FX 1-4 take a mix bus or an
    // insert, FX 5-8 are inserts) and its effect; the slot the encoders set outlined.
    auto& fx = ctx.engine.getFx();
    auto area = r.reduced (8, 4).toFloat();
    const int slots = juce::jmin (8, fx.getNumSlots());
    const float w = area.getWidth() / 4.0f, h = area.getHeight() / 2.0f;
    for (int i = 0; i < slots; ++i)
    {
        const auto cell = juce::Rectangle<float> (area.getX() + w * (float) (i % 4), area.getY() + h * (float) (i / 4), w, h).reduced (3.0f);
        auto in = block (g, cell, "FX " + juce::String (i + 1));
        if (i == selectedFx)
        {
            g.setColour (kGr);
            g.drawRoundedRectangle (cell.reduced (0.75f), 4.0f, 2.0f);
        }
        const int m = fx.getModel (i);
        const bool pending = i == selectedFx && pendingFxModel >= 0;
        const auto name = pending ? models::model (models::Fx, pendingFxModel).name : m > ld::kFxExt ? models::model (models::Fx, m).name : juce::String ("-");
        // The effect on a navy plate (amber while it waits for ASSIGN).
        auto plate = in.removeFromBottom (juce::jmin (30.0f, in.getHeight() * 0.45f));
        g.setColour (kNavy);
        g.fillRoundedRectangle (plate, 3.0f);
        g.setColour (pending ? kAmber : juce::Colours::white);
        g.setFont (type::font (10.5f, true));
        g.drawFittedText (name, plate.reduced (3.0f, 1.0f).toNearestInt(), juce::Justification::centred, 2, 0.7f);
        in.removeFromBottom (3.0f);
        const float rowH = juce::jmin (14.0f, in.getHeight() / 2.0f);
        for (int side = 0; side < 2; ++side)
        {
            auto row = in.removeFromTop (rowH);
            const auto source = i < 4 ? x32Value ("/fx/" + juce::String (i + 1) + (side == 0 ? "/source/l" : "/source/r")) : juce::String ("INS");
            g.setColour (kNavy);
            g.setFont (type::font (juce::jmin (9.5f, rowH), true));
            g.drawText (side == 0 ? "L" : "R", row.removeFromLeft (12.0f), juce::Justification::centred);
            const bool send = source.isNotEmpty() && source != "INS";
            chip (g, row.reduced (2.0f, 1.0f), send ? source : juce::String ("Insert"), send);
        }
    }
}

void Display::paintFxPanel (juce::Graphics& g, juce::Rectangle<int> r, int slot)
{
    // As the EFFECTS photographs: a strip along the top (the slot, its sources, the
    // effect's name), then the device's face: a rack unit in navy, its name at the left,
    // every control on it, this layer's six lit. A graphic EQ: its 31 sliders and the
    // level, the four bands on the encoders boxed. (Sidecar's drawing of each device.)
    auto& fx = ctx.engine.getFx();
    const int m = fx.getModel (slot);
    auto area = r.reduced (6, 3).toFloat();
    auto head = block (g, area.removeFromTop (30.0f), {}).reduced (2.0f, 1.0f);
    area.removeFromTop (3.0f);
    g.setColour (kNavy);
    g.setFont (type::font (12.0f, true));
    g.drawText (juce::String (slot + 1), head.removeFromLeft (16.0f), juce::Justification::centred);
    {
        auto sources = head.removeFromLeft (juce::jmin (70.0f, head.getWidth() * 0.2f));
        const float rowH = sources.getHeight() / 2.0f;
        for (int side = 0; side < 2; ++side)
        {
            const auto source = slot < 4 ? x32Value ("/fx/" + juce::String (slot + 1) + (side == 0 ? "/source/l" : "/source/r")) : juce::String ("INS");
            const bool send = source.isNotEmpty() && source != "INS";
            chip (g, sources.removeFromTop (rowH).reduced (1.0f), send ? source : juce::String ("Insert"), send);
        }
    }
    g.setColour (kNavy);
    g.setFont (type::font (11.0f, true));
    g.drawFittedText (m > ld::kFxExt ? models::model (models::Fx, m).name : juce::String ("No effect"), head.reduced (8.0f, 0.0f).toNearestInt(),
                      juce::Justification::centredRight, 1, 0.7f);

    g.setColour (juce::Colour (0xff1b2a5c));
    g.fillRoundedRectangle (area, 5.0f);
    g.setColour (juce::Colour (0xff8fa3d8));
    g.drawRoundedRectangle (area.reduced (1.0f), 5.0f, 1.0f);
    for (const auto screw : { area.getTopLeft().translated (7.0f, 7.0f), area.getTopRight().translated (-7.0f, 7.0f), area.getBottomLeft().translated (7.0f, -7.0f),
                              area.getBottomRight().translated (-7.0f, -7.0f) })
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (screw));
    if (m <= ld::kFxExt)
        return;
    const auto& model = models::model (models::Fx, m);
    auto& values = fx.params (slot).values;
    auto face = area.reduced (14.0f, 6.0f);

    if (isGraphicEq (slot))
    {
        const auto bands = geqBands (slot);
        const bool dual = bands.size() > 31;
        const int side = dual ? juce::jlimit (0, 1, layer) : 0;
        const auto title = face.removeFromTop (16.0f);
        g.setColour (juce::Colours::white);
        g.setFont (type::font (13.0f, true).italicised());
        g.drawText (model.name.toUpperCase(), title, juce::Justification::centred);
        if (dual)
            g.drawText (side == 0 ? "A" : "B", title, juce::Justification::centredLeft);
        const auto names = face.removeFromBottom (juce::jmin (22.0f, face.getHeight() * 0.25f));
        const float cw = face.getWidth() / 32.0f;
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float> (face.getX(), face.getCentreY() - 0.5f, face.getWidth(), 1.0f));
        for (int k = 0; k < 32; ++k)
        {
            int index = -1;
            if (k == 31)
                index = model.find (dual ? (side == 0 ? "master_a" : "master_b") : "master");
            else if (const size_t at = (size_t) (side * 31 + k); at < bands.size())
                index = bands[at];
            const auto col = juce::Rectangle<float> (face.getX() + cw * (float) k, face.getY(), cw, face.getHeight());
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRect (col.withSizeKeepingCentre (2.0f, col.getHeight()));
            if (index >= 0)
            {
                const float v = juce::jlimit (-15.0f, 15.0f, values[index].load());
                const float y = col.getCentreY() - v / 15.0f * (col.getHeight() * 0.5f - 3.0f);
                g.setColour (juce::Colours::white);
                g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (9.0f, cw - 2.0f), 6.0f).withCentre ({ col.getCentreX(), y }), 1.5f);
            }
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (type::font (juce::jlimit (6.0f, 8.0f, cw * 0.6f), true));
            g.drawFittedText (k < 31 ? geqBandName (k) : juce::String ("lvl"), juce::Rectangle<float> (col.getX(), names.getY() + (k % 2 == 0 ? 0.0f : names.getHeight() * 0.5f), cw, names.getHeight() * 0.5f)
                                                                                    .expanded (cw * 0.45f, 0.0f)
                                                                                    .toNearestInt(),
                              juce::Justification::centred, 1, 0.6f);
        }
        // The four bands on the encoders, boxed; the eight on the faders under a bar.
        g.setColour (kAmber);
        g.drawRect (juce::Rectangle<float> (face.getX() + cw * (float) juce::jlimit (0, 27, geqFirst), face.getY() - 1.0f, cw * 4.0f, face.getHeight() + 2.0f), 1.5f);
        if (geqSlot == slot)
            g.fillRect (juce::Rectangle<float> (face.getX() + cw * (float) juce::jmin (23, geqFirst), face.getBottom() + 1.0f, cw * 8.0f, 2.0f));
        return;
    }

    auto plate = face.removeFromLeft (juce::jmin (92.0f, face.getWidth() * 0.18f));
    g.setColour (juce::Colours::white);
    g.setFont (type::font (13.0f, true).italicised());
    g.drawFittedText (model.name, plate.reduced (2.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 3, 0.7f);
    const int n = (int) model.params.size();
    if (n == 0)
        return;
    const int perRow = n > 24 ? 12 : 6; // a row a layer of six, while they fit
    const int rows = juce::jmax (1, (n + perRow - 1) / perRow);
    const float cw = face.getWidth() / (float) perRow, ch = face.getHeight() / (float) rows;
    for (int k = 0; k < n; ++k)
    {
        const auto& p = model.params[(size_t) k];
        auto cell = juce::Rectangle<float> (face.getX() + cw * (float) (k % perRow), face.getY() + ch * (float) (k / perRow), cw, ch).reduced (1.0f);
        const bool mine = k / 6 == layer;
        if (mine)
        {
            g.setColour (kYellow.withAlpha (0.16f));
            g.fillRoundedRectangle (cell, 3.0f);
        }
        const float v = values[k].load();
        g.setColour (juce::Colours::white.withAlpha (mine ? 1.0f : 0.75f));
        g.setFont (type::font (juce::jlimit (6.5f, 9.0f, ch * 0.22f), true));
        g.drawFittedText (titled (p.label), cell.removeFromBottom (juce::jmin (11.0f, ch * 0.26f)).toNearestInt(), juce::Justification::centred, 1, 0.6f);
        if (ch >= 40.0f)
        {
            g.setColour (kDeepText);
            g.setFont (type::font (juce::jlimit (6.5f, 9.0f, ch * 0.2f), false));
            g.drawFittedText (p.display (v), cell.removeFromTop (juce::jmin (11.0f, ch * 0.24f)).toNearestInt(), juce::Justification::centred, 1, 0.6f);
        }
        if (p.kind == models::ParamKind::Toggle)
        {
            chip (g, juce::Rectangle<float> (juce::jmin (34.0f, cell.getWidth() - 4.0f), juce::jmin (13.0f, cell.getHeight())).withCentre (cell.getCentre()),
                  v > 0.5f ? "On" : "Off", v > 0.5f);
            continue;
        }
        // A knob: a dark cap in a white ring, its pointer yellow.
        const float radius = juce::jmax (3.0f, juce::jmin (9.0f, cell.getHeight() * 0.4f, cell.getWidth() * 0.3f));
        const float angle = (juce::jlimit (0.0f, 1.0f, p.toNormalised (v)) - 0.5f) * juce::MathConstants<float>::pi * 5.0f / 3.0f;
        const auto c = cell.getCentre();
        const auto cap = juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (c);
        g.setColour (juce::Colour (0xff0d1634));
        g.fillEllipse (cap);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawEllipse (cap, 1.2f);
        g.setColour (kYellow);
        g.drawLine (c.x, c.y, c.x + radius * std::sin (angle), c.y - radius * std::cos (angle), 1.8f);
    }
}

void Display::paintMatrixSends (juce::Graphics& g, juce::Rectangle<int> area, Strip& s)
{
    // A mix bus's or a main's sends to the six matrices, the four on the encoders marked.
    const int matrices = juce::jmin (6, (int) ctx.engine.getMatrices().size());
    if (matrices == 0)
    {
        g.setColour (kDim);
        g.setFont (type::font (type::kLabel));
        g.drawText ("No matrix sends on this console", area, juce::Justification::centred);
        return;
    }
    auto r = block (g, area.toFloat().reduced (8.0f, 4.0f), "Matrix Sends").reduced (4.0f, 2.0f);
    const float w = r.getWidth() / (float) matrices;
    const int first = juce::jlimit (0, 1, sendBank) > 0 ? juce::jmax (0, matrices - 4) : 0;
    for (int i = 0; i < matrices; ++i)
    {
        auto col = r.removeFromLeft (w).reduced (juce::jmin (12.0f, w * 0.2f), 0.0f);
        auto& send = s.matrix[(size_t) i];
        const bool on = send.on.load();
        const float pos = fader::dbToPos (send.levelDb.load());
        const bool focused = i >= first && i < first + 4;
        auto label = col.removeFromBottom (16.0f);
        g.setColour (focused ? kYellow : kSkyAlt);
        g.fillRect (label.reduced (0.0f, 1.0f));
        g.setColour (kNavy);
        g.setFont (type::font (10.0f, true));
        g.drawText ("MTX " + juce::String (i + 1), label, juce::Justification::centred);
        g.setColour (kWell);
        g.fillRect (col);
        g.setColour (on ? kYellow : kDeepText.withAlpha (0.5f));
        g.fillRect (col.reduced (1.0f).withTrimmedTop ((col.getHeight() - 2.0f) * (1.0f - pos)));
    }
}

void Display::timerCallback()
{
    if (! isShowing())
        return;
    followRemote();
    if (screen == Screen::Setup && tab == 2)
    {
        followRemotePrefs();
        repaint(); // the messages counted
    }
    if (screen == Screen::Setup && (tab == 0 || tab == 3))
        repaint (encoderRow); // the clock; the fields being edited
    // The spectrograph's rows (and the EQ's overlay redrawn).
    if (rtaShown)
    {
        if (! ctx.engine.getAnalysis().isFrozen())
        {
            Analysis::Bands bands;
            ctx.engine.getAnalysis().read (bands);
            rtaHistory.emplace_back (bands.levels.begin(), bands.levels.end());
            if (rtaHistory.size() > 150)
                rtaHistory.erase (rtaHistory.begin());
        }
        if (eqView.isVisible())
            repaint (eqView.getBounds());
    }
    // EQ on faders ends when its slot no longer holds a graphic EQ.
    if (geqSlot >= 0 && ! isGraphicEq (geqSlot))
        showGeqOnFaders (-1, geqFirst);
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
    const auto page = screen == Screen::MuteGroups ? underScreen : screen;
    const int pageTab = shownTab();
    if (page == Screen::Meters || (page == Screen::Home && (pageTab == 0 || pageTab == 5)) || page == Screen::Monitor || page == Screen::Usb)
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
    // The page: pale, under the tabs (the screen's photographs).
    g.setColour (kPage);
    g.fillRect (contentArea);
    // The thin lines from the on-screen cells down to their encoders.
    g.setColour (juce::Colour (0xff4a4c50));
    for (int i = 0; i < 6; ++i)
    {
        const auto knob = knobs[(size_t) i].getBounds();
        g.drawVerticalLine (knob.getCentreX(), (float) screenArea.getBottom(), (float) knob.getY());
    }
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
    // The status bar as the screen's photographs show it: five tiles along the top.
    // The selected channel on a pale tile, its colour at its right edge; the scene, its
    // number large in amber; the recorder's clock; the digital links and rates; the clock.
    auto r = area.toFloat().reduced (4.0f, 3.0f);
    const float unit = (r.getWidth() - 12.0f) / 440.0f;
    const auto tileOf = [&r, &g] (float width, juce::Colour fill)
    {
        auto t = r.removeFromLeft (width);
        r.removeFromLeft (3.0f);
        g.setColour (fill);
        g.fillRoundedRectangle (t, 3.0f);
        g.setColour (kNavy.withAlpha (0.55f));
        g.drawRoundedRectangle (t.reduced (0.5f), 3.0f, 1.0f);
        return t.reduced (4.0f, 2.0f);
    };

    {
        auto tile = tileOf (78.0f * unit, kSky);
        if (auto* s = selected())
        {
            g.setColour (consoleColour (s->getColourIndex()));
            g.fillRect (tile.removeFromRight (5.0f));
            tile.removeFromRight (2.0f);
            auto label = s->getLabel();
            if (label.startsWith ("CH "))
                label = "Ch" + label.fromFirstOccurrenceOf (" ", false, false).paddedLeft ('0', 2);
            g.setColour (kNavy);
            g.setFont (type::font (10.0f, true));
            g.drawFittedText (label, tile.removeFromTop (tile.getHeight() * 0.42f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
            g.setFont (type::font (11.5f, true));
            g.drawFittedText (s->getName().isNotEmpty() ? s->getName() : s->getLabel(), tile.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        }
    }

    // The scene: its number, the name and the next one on the bars beside it.
    auto& show = ctx.show;
    const int current = show.getCurrentSceneIndex();
    {
        auto scene = tileOf (124.0f * unit, kSky);
        g.setColour (kAmber);
        g.setFont (type::font (23.0f, true));
        g.drawText (current >= 0 ? juce::String (current + 1).paddedLeft ('0', 2) : juce::String ("--"), scene.removeFromLeft (34.0f), juce::Justification::centredLeft);
        const auto nameOf = [&show] (int i)
        {
            const auto s = show.getScene (i);
            return s.has_value() ? s->name : juce::String();
        };
        auto upper = scene.removeFromTop (scene.getHeight() * 0.55f);
        g.setColour (kBox);
        g.fillRect (upper.reduced (0.0f, 1.0f));
        g.fillRect (scene.reduced (0.0f, 1.0f));
        g.setColour (kNavy);
        g.setFont (type::font (10.5f, true));
        g.drawFittedText (current >= 0 ? nameOf (current) : juce::String ("no scene"), upper.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        if (current + 1 < show.getNumScenes())
        {
            g.setFont (type::font (9.5f, true));
            g.drawFittedText (juce::String (current + 2).paddedLeft ('0', 2) + ":  " + nameOf (current + 1), scene.reduced (3.0f, 0.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.7f);
        }
    }

    // The recorder: its file, the time, a stop / play / record mark, the time left.
    {
        auto tape = tileOf (90.0f * unit, kBox);
        auto& usb = ctx.engine.getUsbRecorder();
        using P = UsbRecorder::PlayState;
        const auto clock = [] (int64_t samples, double rate)
        {
            const int secs = rate > 0.0 ? (int) ((double) samples / rate) : 0;
            return juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2);
        };
        const double rate = juce::jmax (1.0, usb.getSampleRate());
        const bool recording = usb.isRecording();
        const int64_t now = recording ? usb.getRecTime() : usb.getPosition();
        const int64_t left = recording ? 0 : juce::jmax ((int64_t) 0, usb.getLength() - usb.getPosition());
        const auto file = (recording ? usb.getRecFile() : usb.getCurrentFile()).getFileNameWithoutExtension();
        g.setColour (kNavy);
        if (file.isNotEmpty())
        {
            g.setFont (type::font (9.0f, false));
            g.drawFittedText (file, tape.removeFromTop (12.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        }
        g.setFont (type::font (11.0f, true));
        const auto w = tape.getWidth() * 0.5f;
        g.drawText (clock (now, rate), tape.removeFromLeft (w * 0.8f), juce::Justification::centred);
        auto mark = tape.removeFromLeft (w * 0.4f).withSizeKeepingCentre (7.0f, 7.0f);
        if (recording)
        {
            g.setColour (kGr);
            g.fillEllipse (mark);
        }
        else if (usb.getPlayState() == P::Playing)
        {
            juce::Path arrow;
            arrow.addTriangle (mark.getX(), mark.getY(), mark.getRight(), mark.getCentreY(), mark.getX(), mark.getBottom());
            g.fillPath (arrow);
        }
        else
            g.fillRect (mark.withSizeKeepingCentre (6.0f, 6.0f));
        g.setColour (kNavy);
        g.drawText ("-" + clock (left, rate), tape, juce::Justification::centred);
    }

    // The digital links and clock: AES A and B, the sample rate, the expansion card.
    {
        auto rates = tileOf (70.0f * unit, kBox);
        auto* device = ctx.deviceManager.getCurrentAudioDevice();
        const double sr = ctx.engine.getSampleRate();
        const auto rate = sr > 0 ? juce::String (juce::roundToInt (sr / 1000.0)) + "K" : juce::String ("--");
        // Each with its green LED; A and B are the AES50 ports (nothing plugged in here),
        // L the clock (the audio interface's), C the expansion card (the audio interface).
        const auto cell = [&g] (juce::Rectangle<float> area, const juce::String& text, bool led)
        {
            auto dot = area.removeFromLeft (8.0f).withSizeKeepingCentre (5.0f, 5.0f);
            g.setColour (led ? juce::Colour (0xff3ddc5a) : kNavy.withAlpha (0.25f));
            g.fillEllipse (dot);
            g.setColour (kNavy);
            g.drawText (text, area, juce::Justification::centredLeft);
        };
        g.setFont (type::font (9.5f, true));
        auto upper = rates.removeFromTop (rates.getHeight() * 0.5f);
        cell (upper.removeFromLeft (upper.getWidth() * 0.45f), "A: -", false);
        cell (upper, "L  " + rate, device != nullptr);
        cell (rates.removeFromLeft (rates.getWidth() * 0.45f), "B: -", false);
        cell (rates, device != nullptr ? "C: " + device->getName().substring (0, 5).toUpperCase() : juce::String ("C: -"), device != nullptr);
    }

    // The clock on its orange tile, the seconds small.
    {
        auto clock = tileOf (r.getWidth(), kClockTile);
        const auto now = juce::Time::getCurrentTime();
        g.setColour (juce::Colours::white);
        g.setFont (type::font (10.0f, true));
        g.drawText (now.formatted (":%S"), clock.removeFromRight (18.0f).withTrimmedTop (9.0f), juce::Justification::centredLeft);
        g.setFont (type::font (19.0f, true));
        // 12 h or 24 h (SETUP > config's 12h Clock Mode, `/-prefs/clockmode`).
        g.drawFittedText (now.formatted (x32Value ("/-prefs/clockmode") == "12h" ? "%I:%M" : "%H:%M"), clock.toNearestInt(), juce::Justification::centredRight, 1, 0.8f);
    }
}

void Display::paintBezelMeters (juce::Graphics& g, juce::Rectangle<int> area)
{
    // The X32's main meters beside the screen: M/C (and the solo bus), the
    // scale, L and R; a red CLIP, yellow above -18 dB, then green.
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
        const auto colour = k == 0 ? juce::Colour (0xffff3b30) : k < 10 ? juce::Colour (0xfff2d51f) : juce::Colour (0xff3ddc5a);
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
    if ((screen == Screen::MuteGroups ? underScreen : screen) == Screen::Routing)
    {
        for (auto& c : routingStrip (tabsArea, shownTab()))
            if (c.kind == 1 && c.tab == i)
                return c.bounds;
        return {};
    }
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
    if ((screen == Screen::MuteGroups ? underScreen : screen) == Screen::Routing)
    {
        // BLOCK and PATCH: the shown group's name, its tabs in a band, the shown one framed.
        const auto cells = routingStrip (tabsArea, shownTab());
        g.setColour (kTabOff);
        juce::Rectangle<int> band;
        for (auto& c : cells)
            if (c.kind != 2)
                band = band.isEmpty() ? c.bounds : band.getUnion (c.bounds);
        g.fillRect (band.withTrimmedTop (3));
        for (auto& c : cells)
        {
            auto b = c.bounds.toFloat().withTrimmedTop (3.0f);
            const bool on = c.kind == 1 && c.tab == shownTab();
            if (c.kind == 2)
            {
                g.setColour (kDeep);
                g.fillRoundedRectangle (b.reduced (0.0f, 1.0f), 3.0f);
            }
            if (on)
            {
                g.setColour (kYellow);
                g.drawRoundedRectangle (b.reduced (1.5f, 2.0f), 3.0f, 2.0f);
            }
            g.setColour (c.kind == 1 && ! on ? kDeepText : juce::Colours::white);
            g.setFont (type::font (c.kind == 1 ? 12.5f : 11.5f, true));
            g.drawFittedText (c.text, b.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
        g.setColour (kBox);
        g.fillRect (r.removeFromBottom (2));
        return;
    }
    g.setFont (type::font (12.0f, true).italicised());
    for (int i = 0; i < tabs.size(); ++i)
    {
        auto cell = getTabBounds (i).toFloat().reduced (1.0f, 0.0f).withTrimmedTop (2.0f);
        const bool on = i == shownTab();
        juce::Path folder;
        folder.addRoundedRectangle (cell.getX(), cell.getY(), cell.getWidth(), cell.getHeight(), 6.0f, 6.0f, true, true, false, false);
        g.setColour (on ? kBox : kTabOff);
        g.fillPath (folder);
        g.setColour (kSkyLine);
        g.strokePath (folder, juce::PathStrokeType (1.0f));
        g.setColour (kNavy);
        g.drawText (screen == Screen::Setup || screen == Screen::Meters || screen == Screen::Home ? tabs[i].toUpperCase() : tabs[i], cell, juce::Justification::centred);
    }
    g.setColour (kBox);
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
    // Ins, EQ, Dynamics, Out, Bus Sends), its DCAs and mute groups along the bottom.
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
        parts.push_back ({ "Bus Sends", 1.5f });
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
            // GAIN: the input's head amp, or the strip's trim (an FX return, a bus: neither).
            if (const auto gain = gainBinding (s); gain.isValid())
            {
                const bool headAmp = gainIsHeadAmp (s);
                valueBox (g, in.removeFromTop (13.0f).reduced (2.0f, 0.0f), gain.text().upToFirstOccurrenceOf (" ", false, false));
                knobWithName (in, headAmp ? "Gain" : "Trim", gain.get(), ! headAmp);
            }
        }
        else if (name == "Gate" || name == "Dynamics")
        {
            const bool gate = name == "Gate";
            auto& d = gate ? p.gate() : p.dynamics();
            transfer (in.removeFromTop (in.getHeight() * 0.44f), gate ? gateView : dynView, d.model.load() > 0,
                      gate ? s.meters.gateReductionDb.load() : s.meters.dynReductionDb.load());
            in.removeFromTop (3.0f);
            chip (g, in.removeFromTop (12.0f).reduced (6.0f, 0.0f), gate ? "Gate" : "Comp", d.on.load());
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
        else if (name == "Bus Sends")
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
    // SAFE, then the groups (the HOME photograph).
    g.setColour (kNavy.withAlpha (0.25f));
    g.setFont (type::font (13.0f, true));
    g.drawText ("SAFE", line.removeFromLeft (44.0f), juce::Justification::centredLeft);
    row ("DCA", 30.0f, 8, dcas.words[0]);
    row ("Mute Groups", 76.0f, 6, mutes.words[0]);
}

void Display::paintSends (juce::Graphics& g, juce::Rectangle<int> area, Strip& s)
{
    // The 16 bus sends as the console draws them: the bus number, the send's fader and meter,
    // MUTE and the tap point of each pair, and the signal-flow diagram under them.
    const int buses = juce::jmin (16, (int) ctx.engine.getBuses().size());
    if (! s.isInput() || buses == 0)
    {
        g.setColour (kDim);
        g.setFont (type::font (type::kLabel));
        g.drawText ("No bus sends on this strip", area, juce::Justification::centred);
        return;
    }
    auto r = area.reduced (8, 4);
    const float w = (float) r.getWidth() / (float) buses;
    const int diagramH = juce::jmin (40, r.getHeight() / 4);
    auto diagram = r.removeFromBottom (diagramH);
    const int boxH = 12, chipH = 11;
    auto numbers = r.removeFromTop (boxH);
    auto chips = r.removeFromBottom (chipH * 2 + 2);
    const auto taps = [] (const juce::String& t)
    {
        return t == "IN/LC" ? juce::String ("IN/LC") : t == "<-EQ" ? juce::String ("PRE EQ") : t == "EQ->" ? juce::String ("POST EQ")
               : t == "PRE" ? juce::String ("PRE FADER") : t == "POST" ? juce::String ("POST FADER") : juce::String ("SUB GROUP");
    };
    for (int i = 0; i < buses; ++i)
    {
        const float x = (float) r.getX() + w * (float) i;
        auto* send = s.sends.find (i);
        const bool on = send != nullptr && send->enabled.load();
        const float pos = fader::dbToPos (send != nullptr ? send->levelDb.load() : -144.0f);
        const bool focused = i >= 2 * sendBank && i < 2 * sendBank + 4;
        auto num = juce::Rectangle<float> (x + 1.0f, (float) numbers.getY(), w - 2.0f, (float) boxH);
        g.setColour (i < 8 ? juce::Colour (0xffaeb8d6) : juce::Colour (0xffe9ecf4));
        g.fillRect (num);
        g.setColour (juce::Colour (0xff5b668c));
        g.drawRect (num, 1.0f);
        g.setColour (kNavy);
        g.setFont (type::font (10.0f, true));
        g.drawText (juce::String (i + 1), num.toNearestInt(), juce::Justification::centred);
        auto well = juce::Rectangle<float> (x + 1.0f, (float) r.getY() + 1.0f, w - 2.0f, (float) r.getHeight() - 5.0f);
        g.setColour (juce::Colour (0xff22262e));
        g.fillRect (well);
        // The fader's slot and cap at the left, the meter at the right: green to -18, yellow above.
        auto meter = well.removeFromRight (well.getWidth() * 0.45f).reduced (1.0f, 2.0f);
        auto slot = well.reduced (well.getWidth() * 0.4f, 3.0f);
        g.setColour (juce::Colour (0xff10131a));
        g.fillRect (slot);
        g.setColour (juce::Colour (0xffdfe3ee));
        g.fillRect (slot.getCentreX() - w * 0.22f, slot.getBottom() - slot.getHeight() * pos - 3.0f, w * 0.44f, 6.0f);
        for (float yy = meter.getBottom(); yy > meter.getY() + 2.0f; yy -= 3.0f)
        {
            const float up = (meter.getBottom() - yy) / meter.getHeight();
            if (up > pos)
                break;
            g.setColour (! on ? juce::Colour (0xff4a4f5c) : up < fader::dbToPos (-18.0f) ? juce::Colour (0xff3ddc5a) : up < 0.95f ? juce::Colour (0xfff2d51f) : juce::Colour (0xffff3b30));
            g.fillRect (meter.getX(), yy - 2.0f, meter.getWidth(), 2.0f);
        }
        g.setColour (i < 12 ? juce::Colour (0xff22d9d0) : juce::Colour (0xffff4fd0));
        g.fillRect (x + 1.0f, (float) r.getBottom() - 3.0f, w - 2.0f, 2.0f);
        auto chip = juce::Rectangle<float> (x + 1.0f, (float) chips.getY(), w - 2.0f, (float) chipH);
        g.setColour (on ? juce::Colour (0xffbec6e0) : kYellow);
        g.fillRect (chip);
        g.setColour (kNavy);
        g.setFont (type::font (8.5f, true));
        g.drawText ("MUTE", chip.toNearestInt(), juce::Justification::centred);
        if (i % 2 == 0)
        {
            auto tap = juce::Rectangle<float> (x + 1.0f, (float) chips.getY() + (float) chipH + 2.0f, w * 2.0f - 2.0f, (float) chipH);
            g.setColour (i < 8 ? juce::Colour (0xffbec6e0) : juce::Colour (0xffe9ecf4));
            g.fillRect (tap);
            g.setColour (kNavy);
            g.drawText (taps (x32Value (x32Address (s) + "/mix/" + juce::String (i + 1).paddedLeft ('0', 2) + "/type")), tap.toNearestInt(), juce::Justification::centred);
        }
        if (focused && i % 4 == 0)
        {
            g.setColour (kFrame);
            g.drawRect (juce::Rectangle<float> (x, (float) numbers.getY() - 1.0f, w * 4.0f, (float) (chips.getBottom() - numbers.getY() + 2)), 1.5f);
        }
    }
    // The flow of a channel: the points the sends can tap.
    {
        const float y = (float) diagram.getCentreY() + 4.0f, x0 = (float) diagram.getX() + (float) diagram.getWidth() * 0.18f, span = (float) diagram.getWidth() * 0.6f;
        g.setColour (kNavy);
        g.drawHorizontalLine ((int) y, x0, x0 + span);
        g.setFont (type::font (8.5f, true));
        const char* const names[] = { "IN/LC", "Pre EQ", "Post EQ", "Pre Fdr", "Post Fdr" };
        const float at[] = { 0.12f, 0.3f, 0.52f, 0.68f, 0.88f };
        for (int k = 0; k < 5; ++k)
        {
            const float px = x0 + span * at[k];
            g.drawLine (px, y, px, y - 7.0f, 1.0f);
            g.drawText (names[k], juce::Rectangle<float> (px - 26.0f, y - 18.0f, 52.0f, 11.0f), juce::Justification::centred);
        }
        for (float f : { 0.0f, 0.1f, 0.22f, 0.4f, 0.6f, 0.8f })
        {
            g.setColour (juce::Colour (0xffd9deee));
            g.fillRect (x0 + span * f, y - 5.0f, 10.0f, 10.0f);
        }
        g.setColour (kNavy);
        g.drawText ("Sub Grp", juce::Rectangle<float> (x0 + span + 4.0f, y - 6.0f, 44.0f, 12.0f), juce::Justification::centredLeft);
    }
}

void Display::paintMeters (juce::Graphics& g, juce::Rectangle<int> r)
{
    // As the METERS photograph: the meters, the faders' positions, then rows of
    // gain reduction; each tab's groups as the manual lists them (p.47). A group
    // with no strips (outputs the engine has no meter for) is drawn as empty wells.
    struct Group
    {
        juce::String title;
        std::vector<Strip*> strips;
    };
    auto& e = ctx.engine;
    const auto take = [] (const MixerEngine::StripList& list, int from, int count)
    {
        std::vector<Strip*> out;
        for (int i = from; i < from + count && i < (int) list.size(); ++i)
            out.push_back (list[(size_t) i].get());
        return out;
    };
    const auto blanks = [] (int n) { return std::vector<Strip*> ((size_t) n, nullptr); };
    std::vector<Group> groups;
    switch (tab)
    {
        case 0:
            groups = { { "", take (e.getChannels(), 0, 16) }, { "", take (e.getChannels(), 16, 16) } };
            break;
        case 1:
            groups = { { "Mix Bus", take (e.getBuses(), 0, 16) }, { "Matrix", take (e.getMatrices(), 0, 6) }, { "Main", take (e.getMains(), 0, 2) } };
            break;
        case 2:
            groups = { { "Aux Sends", blanks (6) }, { "Aux Returns", take (e.getAux(), 0, 8) }, { "FX Returns", take (e.getAux(), 8, 8) } };
            break;
        default:
            groups = { { "Inputs", take (e.getChannels(), 0, 32) }, { "Aux In", take (e.getAux(), 0, 8) }, { "Aux Out", blanks (6) }, { "AES", blanks (2) },
                       { "Mon", blanks (2) }, { "Outputs", blanks (16) }, { "P16", blanks (16) } };
            break;
    }
    std::vector<Strip*> strips;
    std::vector<int> groupStart;
    for (auto& grp : groups)
    {
        groupStart.push_back ((int) strips.size());
        strips.insert (strips.end(), grp.strips.begin(), grp.strips.end());
    }
    if (strips.empty())
        return;
    r.reduce (6, 4);
    g.setColour (juce::Colour (0xff7f8aae));
    g.fillRoundedRectangle (r.toFloat().expanded (1.0f), 4.0f);
    g.setColour (juce::Colour (0xffaeb8d6));
    g.fillRoundedRectangle (r.toFloat().reduced (1.0f), 3.0f);
    // A dark title bar over each group, as the console's.
    const auto section = [&g] (juce::Rectangle<int> head, const juce::String& name)
    {
        g.setColour (kBarDark);
        g.fillRect (head);
        g.setColour (juce::Colours::white);
        g.setFont (type::font (9.0f, true));
        g.drawText (name.toUpperCase(), head, juce::Justification::centred);
    };
    auto body = r.reduced (4, 2);
    const int rows = tab == 0 ? 2 : tab == 1 ? 1 : 0;
    const int grRow = rows > 0 ? 16 : 0;
    const int rowsH = body.getHeight() - rows * (grRow + 14);
    const auto heads = body.removeFromTop (12);
    const int n = (int) strips.size();
    const int numGaps = (int) groups.size() - 1;
    const float gap = tab == 0 ? 22.0f : 8.0f; // channels: the scale stands between 16 and 17
    const float w = ((float) body.getWidth() - gap * (float) numGaps) / (float) n;
    const auto colX = [&, w, gap] (int i)
    {
        int gaps = 0;
        for (size_t k = 1; k < groupStart.size(); ++k)
            gaps += i >= groupStart[k] ? 1 : 0;
        return (float) body.getX() + w * (float) i + gap * (float) gaps;
    };
    if (tab == 0)
        section (heads, "Input Channels");
    else
        for (size_t k = 0; k < groups.size(); ++k)
        {
            const int first = groupStart[k], last = first + (int) groups[k].strips.size() - 1;
            const int x0 = juce::roundToInt (colX (first)), x1 = juce::roundToInt (colX (last) + w);
            section (juce::Rectangle<int> (x0, heads.getY(), x1 - x0, 12), groups[k].title);
        }
    auto meterRow = body.removeFromTop (rowsH / 2 - 10);
    auto faderRow = body.removeFromTop (rowsH / 2 - 10);
    const float faderH = (float) juce::jmax (0, faderRow.getHeight() - 12);
    for (int i = 0; i < n; ++i)
    {
        auto* s = strips[(size_t) i];
        const float x = colX (i);
        juce::Rectangle<float> cell (x + 0.5f, (float) meterRow.getY(), w - 1.0f, (float) meterRow.getHeight());
        g.setColour (kRowDark);
        g.fillRect (cell);
        if (s == nullptr)
            continue;
        const float level = juce::jlimit (0.0f, 1.0f, fader::dbToPos (juce::Decibels::gainToDecibels (ctx.meters.get (s, MeterPoint::Output, 0), -144.0f)));
        const float h = cell.getHeight() * level;
        // As the console's: green to -18 dB, yellow above it, red from the top (CLIP).
        const float yellowAt = cell.getHeight() * fader::dbToPos (-18.0f), redAt = cell.getHeight() * fader::dbToPos (-1.0f);
        for (auto [from, to, colour] : { std::tuple<float, float, juce::uint32> { 0.0f, yellowAt, 0xff3ddc5a }, { yellowAt, redAt, 0xfff2d51f },
                                          { redAt, cell.getHeight(), 0xffff3b30 } })
        {
            const float top = juce::jmin (h, to);
            if (top > from)
            {
                g.setColour (juce::Colour (colour));
                g.fillRect (juce::Rectangle<float> (cell.getX(), cell.getBottom() - top, cell.getWidth(), top - from));
            }
        }
        // The console's LED segments.
        g.setColour (kRowDark);
        for (float yy = cell.getBottom() - 2.0f; yy > cell.getY(); yy -= 3.0f)
            g.fillRect (cell.getX(), yy, cell.getWidth(), 1.0f);

        juce::Rectangle<float> fcell (x + 0.5f, (float) faderRow.getY(), w - 1.0f, faderH);
        g.setColour (kNavy);
        g.fillRect (fcell.withSizeKeepingCentre (2.0f, fcell.getHeight()));
        const float pos = juce::jlimit (0.0f, 1.0f, fader::dbToPos (s->params.faderDb.load()));
        const float y = fcell.getBottom() - fcell.getHeight() * pos;
        g.setColour (s == selected() ? kYellow : juce::Colours::white);
        g.fillRect (juce::Rectangle<float> (fcell.getCentreX() - w * 0.35f, y - 3.0f, w * 0.7f, 6.0f));
        g.setColour (kNavy);
        g.drawRect (juce::Rectangle<float> (fcell.getCentreX() - w * 0.35f, y - 3.0f, w * 0.7f, 6.0f), 0.8f);
        g.setFont (type::font (8.5f, true));
        if (n <= 40)
            g.drawFittedText (juce::String (s->getNumber()), juce::Rectangle<float> (x, fcell.getBottom() + 1.0f, w, 10.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
    }
    if (tab == 0)
    {
        const float sx = colX (16) - gap, sy = (float) meterRow.getY();
        g.setColour (juce::Colour (0xff4b5578));
        g.fillRect (sx, sy, gap, (float) meterRow.getHeight());
        g.setColour (juce::Colours::white);
        g.setFont (type::font (7.5f, true));
        const char* const marks[] = { "Clip", "-6", "-18", "-36", "-60" };
        const float ys[] = { 0.0f, 0.07f, 0.25f, 0.5f, 0.93f };
        for (int k = 0; k < 5; ++k)
            g.drawText (marks[k], juce::Rectangle<float> (sx - 2.0f, sy + ys[k] * ((float) meterRow.getHeight() - 8.0f), gap + 4.0f, 9.0f), juce::Justification::centred);
    }
    if (rows == 0)
        return;
    const auto reductionRow = [&] (juce::Rectangle<int> row, const juce::String& name, bool gate)
    {
        section (row.removeFromTop (12), name);
        for (int i = 0; i < n; ++i)
        {
            auto* s = strips[(size_t) i];
            juce::Rectangle<float> cell (colX (i) + 0.5f, (float) row.getY(), w - 1.0f, (float) row.getHeight());
            g.setColour (juce::Colour (0xff4d1416)); // unlit: dark red, as the LEDs
            g.fillRect (cell);
            if (s == nullptr)
                continue;
            const float gr = std::abs (gate ? s->meters.gateReductionDb.load() : s->meters.dynReductionDb.load());
            g.setColour (juce::Colour (0xffff3326));
            g.fillRect (cell.withHeight (cell.getHeight() * juce::jlimit (0.0f, 1.0f, gr / 30.0f)));
        }
    };
    body.removeFromTop (2);
    if (tab == 0)
    {
        {
            // Thr -18 (the gate's), Thr -6 -12 -18 (the dynamics') in the scale's column.
            g.setColour (juce::Colours::white);
            g.setFont (type::font (7.5f, true));
            const float sx = colX (16) - gap;
            g.drawText ("Thr", juce::Rectangle<float> (sx - 2.0f, (float) body.getY() + 11.0f, gap + 4.0f, 9.0f), juce::Justification::centred);
            g.drawText ("-18", juce::Rectangle<float> (sx - 2.0f, (float) body.getY() + 20.0f, gap + 4.0f, 9.0f), juce::Justification::centred);
        }
        reductionRow (body.removeFromTop (grRow + 12), "Gate Gain Reduction", true);
        body.removeFromTop (2);
        reductionRow (body.removeFromTop (grRow + 12), "Dyn Gain Reduction", false);
    }
    else
        reductionRow (body.removeFromTop (grRow + 12), "Gain Reduction", false);
}

void Display::paintContent (juce::Graphics& g, juce::Rectangle<int> r)
{
    // MUTE GRP covers only the encoder row (manual 7.8): the page under it stays.
    juce::ScopedValueSetter<Screen> screenBelow (screen, screen == Screen::MuteGroups ? underScreen : screen);
    juce::ScopedValueSetter<int> tabBelow (tab, shownTab());
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
                paintHomeConfig (g, r, *s);
            else if (tab == 5 && (s->getKind() == StripKind::Bus || s->getKind() == StripKind::Main))
                paintMatrixSends (g, r, *s);
            else if (tab == 5)
                paintSends (g, r, *s);
            else if (tab == 6)
                paintHomeMain (g, r, *s);
            // gate, dyn and eq: their graphs (child components) in a block.
            if (tab >= 2 && tab <= 4)
            {
                const auto inner = block (g, r.reduced (12, 6).toFloat(), tab == 2 ? "Gate" : tab == 3 ? "Dynamics" : "Equalizer");
                // A strip without the block (an X32's aux ins and FX returns have an EQ only; a gate is a channel's).
                const bool hasGate = s->getKind() == StripKind::Channel;
                if ((tab == 2 && ! hasGate) || (tab == 3 && ! hasGate && ! s->isOutput()))
                {
                    g.setColour (kDim);
                    g.setFont (type::font (type::kLabel));
                    g.drawText (tab == 2 ? "No gate on this strip" : "No dynamics on this strip", inner, juce::Justification::centred);
                }
            }
            break;
        case Screen::Meters:
            if (tab == 4)
            {
                paintRta (g, r);
                break;
            }
            if (tab == 5)
            {
                paintAutomix (g, r);
                break;
            }
            paintMeters (g, r);
            break;
        case Screen::Library:
            paintLibraryPresets (g, r);
            break;
        case Screen::Effects:
            if (tab == 0)
                paintEffectsHome (g, r);
            else
                paintFxPanel (g, r, tab - 1);
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
                paintSetupConfig (g, r);
                break;
            }
            if (tab == 2)
            {
                paintRemote (g, r.reduced (12));
                break;
            }
            if (tab == 3)
            {
                paintNetwork (g, r);
                break;
            }
            if (tab == 6)
            {
                paintCard (g, r);
                break;
            }
            if (tab == 4)
            {
                paintScribble (g, r.reduced (12).toFloat(), s);
                break;
            }
            if (tab == 5)
            {
                paintPreamps (g, r.reduced (12).toFloat());
                break;
            }
            paintSetupGlobal (g, r);
            break;
        case Screen::MuteGroups:
            break; // its keys
        case Screen::Monitor:
            if (tab == 0)
                paintMonitor (g, r);
            else if (tab < 3)
                paintTalkback (g, r);
            else
                paintOscillator (g, r);
            break;
        case Screen::Usb:
            paintUsb (g, r);
            break;
        case Screen::Scenes:
            paintScenes (g, r);
            break;
        case Screen::Assign:
            paintAssign (g, r);
            break;
        case Screen::Utility:
        {
            if (tab == 1)
            {
                auto* sel = selected();
                paintRows (g, r.reduced (12), { "INITIALISE (SELECTED)", "", "" },
                           { sel != nullptr ? sel->getLabel() + "  " + sel->getName() : juce::String ("-"),
                             "Back to an X32's factory settings; the source stays", utilityStatus });
                break;
            }
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
    // The six encoders' cells: what each turns (its icon, name and value), and along the
    // bottom what a push does, orange while on. Names as the screen prints them; the
    // HOME page's first layer as photographed (Gate/Duck, Comp/Exp, the two Thresholds).
    static const char* const homeTurn[] = { "Gain", "Threshold", "Insert", "Dynamics", "Threshold", "Pan" };
    static const char* const homePush[] = { "Link", "Gate/Duck", "Insert", "EQ", "Comp/Exp", "LR" };
    const bool homeFirst = screen == Screen::Home && tab == 0 && layer == 0;
    for (int i = 0; i < 6; ++i)
    {
        auto cell = getEncoderCell (i).toFloat().reduced (2.0f, 3.0f);
        const auto& e = encoders[(size_t) i];
        if (! e.turn.isValid() && ! e.push && ! e.dimmed)
        {
            g.setColour (kDeep.withAlpha (0.45f));
            g.fillRoundedRectangle (cell, 4.0f);
            continue;
        }
        // A real menu item with nothing behind it: drawn, greyed.
        g.setColour (e.dimmed ? kSky.withAlpha (0.4f) : kSky);
        g.fillRoundedRectangle (cell, 4.0f);
        g.setColour (e.dimmed ? kSkyLine.withAlpha (0.4f) : kSkyLine);
        g.drawRoundedRectangle (cell.reduced (0.75f), 4.0f, 1.5f);
        auto in = cell.reduced (3.0f, 3.0f);
        auto band = in.removeFromBottom (15.0f);
        if (e.dimmed)
        {
            g.setColour (kDim.withAlpha (0.35f));
            g.fillRoundedRectangle (band, 2.5f);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.setFont (type::font (10.0f, true));
            g.drawFittedText (titled (e.pushLabel), band.toNearestInt(), juce::Justification::centred, 1, 0.7f);
            in.removeFromBottom (2.0f);
            g.setColour (kDeepText.withAlpha (0.75f));
            g.setFont (type::font (10.0f, true));
            g.drawFittedText (titled (e.turn.label), in.removeFromTop (in.getHeight() * 0.5f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
            g.setFont (type::font (10.0f, false));
            g.drawFittedText (e.turn.text ? e.turn.text() : juce::String(), in.toNearestInt(), juce::Justification::centred, 1, 0.7f);
            continue;
        }
        if (! e.push && e.pushLabel.isNotEmpty())
        {
            // A real push Sidecar has nothing behind: its chip shown, dimmed.
            g.setColour (kDim.withAlpha (0.35f));
            g.fillRoundedRectangle (band, 2.5f);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.setFont (type::font (10.0f, true));
            g.drawFittedText (titled (e.pushLabel), band.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
        else
            chip (g, band, e.push ? (homeFirst && e.pushLabel.isNotEmpty() ? juce::String (homePush[i]) : titled (e.pushLabel)) : juce::String(), e.push && e.pushOn && e.pushOn());
        in.removeFromBottom (2.0f);
        if (! e.turn.isValid())
        {
            // A push with a name over it (USB Drive).
            if (e.turn.label.isNotEmpty())
            {
                g.setColour (kNavy);
                g.setFont (type::font (10.0f, true));
                g.drawFittedText (titled (e.turn.label), in.toNearestInt(), juce::Justification::centred, 1, 0.7f);
            }
            continue;
        }
        auto icon = in.removeFromLeft (juce::jmin (in.getHeight(), 30.0f));
        knobIcon (g, icon.getCentre(), juce::jmin (10.0f, icon.getWidth() * 0.32f), e.turn.get());
        g.setColour (kNavy);
        g.setFont (type::font (10.0f, true));
        g.drawFittedText (homeFirst ? juce::String (homeTurn[i]) : titled (e.turn.label), in.removeFromTop (in.getHeight() * 0.45f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        valueBox (g, in.reduced (2.0f, 1.0f), e.turn.text ? e.turn.text() : juce::String());
    }
}

//==============================================================================
void Display::mouseDown (const juce::MouseEvent& e)
{
    dragEncoder = -1;
    dragged = false;
    const auto p = e.getPosition();
    if ((screen == Screen::MuteGroups ? underScreen : screen) == Screen::Routing)
        for (auto& c : routingStrip (tabsArea, shownTab()))
            if (c.kind == 2 && c.bounds.contains (p))
            {
                showTab (c.tab);
                return;
            }
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

    // The six encoders under the screen, each with its push key.
    knobsRow.reduce (6, 0);
    const int w = knobsRow.getWidth() / 6;
    for (int i = 0; i < 6; ++i)
    {
        auto cell = knobsRow.removeFromLeft (w).reduced (8, 0);
        pushKeys[(size_t) i]->setBounds (cell.withTrimmedTop (cell.getHeight() - 26));
        knobs[(size_t) i].setBounds (cell.withSizeKeepingCentre (56, 56));
    }
}

} // namespace livedaw::ui::x32
