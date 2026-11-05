#include "ui/Y02RSurface.h"

#include "engine/MixerEngine.h"
#include "engine/Models.h"
#include "engine/Show.h"
#include "engine/Y02RMidi.h"
#include "engine/Yamaha02R.h"
#include "ui/HardwareLook.h"
#include "ui/MeterBank.h"

#include <cmath>

namespace livedaw::ui
{

using namespace universal;

namespace
{
    constexpr int kTopBar = 48;
    constexpr int kKey = 32;

    // The 02R's hardware look (its panel, keys, faders; its blue LCD with dark
    // pixels, the cursor inverted).
    const hw::Look& look() { return hw::Look::y02r(); }
    const juce::Colour kLcd = hw::Look::y02r().lcd, kLcdFrame = hw::Look::y02r().lcd.darker (0.18f),
                       kPixel = hw::Look::y02r().lcdPixel, kPixelDim = hw::Look::y02r().lcdDim;
    // Its LEDs: SEL orange, ON green, the function keys orange.
    const juce::Colour kSelLed (0xffffa030), kOnLed (0xff4cd964);

    // The edit buffer's items (livedaw_02r::params).
    constexpr int kLevel = 0, kOn = 1, kPanL = 2, kPanR = 3, kAtt = 4, kSwitches = 5, kDelayHi = 6, kDelayLo = 7, kEq = 8;
    constexpr int kDynType = 20, kDynParam = 21, kRouting = 27;
    constexpr int kNominal = 101; // a channel fader's 0 dB step
    // The switches' bits.
    constexpr int kPhase = 0, kDelayOn = 1, kEqOn = 2, kHpf = 3, kLpf = 4, kDynOn = 5, kSt = 6;
    constexpr int kSendsBase = 55 * 28;

    const char* const kDynTokens[] = { "Y2CMP", "Y2EXP", "Y2CPH", "Y2CPS", "Y2GAT", "Y2DUK" };
    const char* const kDynNames[] = { "COMP", "EXPAND", "COMPANDER-H", "COMPANDER-S", "GATE", "DUCKING" };

    juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

    // TITLE EDIT's characters.
    const juce::String kTitleChars = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!\"#$%&'()*+,-./:;<=>?@[]_";
    /** The first program of each library (EQ, dynamics, effects) a user may store into (MAN p.211). */
    constexpr int kFirstUser[] = { 32, 40, 40, 0 };

    /** A model parameter's step as its value (livedaw_02r's step_param). */
    float stepValue (const models::Param& p, int step)
    {
        if (p.isContinuous() && p.steps > 1)
            return p.min + (p.max - p.min) * (float) step / (float) (p.steps - 1);
        return (float) step;
    }

    /** EQ frequency `n` (0-119): 1/12 octave steps, 1 kHz at 67 (livedaw_dsp::y02r::FREQS). */
    juce::String freqText (int n)
    {
        const double hz = 1000.0 * std::pow (2.0, (n - 67) / 12.0);
        return hz < 1000.0 ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0, hz < 10000.0 ? 2 : 1) + " kHz";
    }

    juce::String panText (int v)
    {
        return v == 16 ? juce::String ("C") : v < 16 ? "L" + juce::String (16 - v) : "R" + juce::String (v - 16);
    }

    /** The member (the strips that take a group) of a strip, or -1. */
    int memberOf (int strip) { return strip < y02r::kBus ? strip : -1; }
    /** The fader recall safe and fade times cover. */
    int faderIndexOf (int strip) { return strip < y02r::kBus ? strip : strip == y02r::kStereo ? 38 : -1; }
    /** The channel pair a strip is in (MIC 1/2 ... then TAPE 1/2 ...), or -1. */
    int pairOf (int strip)
    {
        return strip < 16 ? strip / 2 : strip >= y02r::kTape && strip < y02r::kEff ? 8 + (strip - y02r::kTape) / 2 : -1;
    }
    bool isStereoStrip (int strip) { return (strip >= 16 && strip < y02r::kTape) || strip == y02r::kEff || strip == y02r::kEff + 1; }
    bool hasDynamics (int strip) { return strip < y02r::kEff || (strip >= y02r::kBus && strip < y02r::kAux) || strip == y02r::kStereo; }
    bool hasEq (int strip) { return strip < y02r::kBus || strip == y02r::kStereo; }
} // namespace

namespace y02r
{
    int address (int strip, int item) { return strip * 28 + item; }
    int sendAddress (int member, int aux) { return kSendsBase + member * 10 + aux; }

    juce::String stripName (int s)
    {
        if (s < 16)
            return "CH " + juce::String (s + 1);
        if (s < kTape)
            return "LINE " + juce::String (17 + 2 * (s - 16)) + "/" + juce::String (18 + 2 * (s - 16));
        if (s < kEff)
            return "TAPE " + juce::String (s - kTape + 1);
        if (s < kBus)
            return "EFF" + juce::String (s - kEff + 1);
        if (s < kAux)
            return "BUS " + juce::String (s - kBus + 1);
        if (s < kStereo)
            return "AUX " + juce::String (s - kAux + 1);
        return "STEREO";
    }

    //==========================================================================
    Fader::Fader (UiContext& c, const Theme& t) : ctx (c), theme (t) {}

    void Fader::bind (int a, bool stereo)
    {
        addr = a;
        stereoLaw = stereo;
        refresh();
        repaint();
    }

    void Fader::refresh()
    {
        const int now = addr >= 0 ? juce::jmax (0, ctx.engine.getYamaha02R().get (addr)) : 0;
        if (now != step)
        {
            step = now;
            repaint();
        }
    }

    juce::Rectangle<float> Fader::track() const noexcept
    {
        return getLocalBounds().toFloat().reduced (0.0f, 14.0f).withTrimmedLeft (24.0f);
    }

    void Fader::moveTo (int s)
    {
        s = juce::jlimit (0, 127, s);
        if (addr < 0 || s == step)
            return;
        ctx.engine.getYamaha02R().set (addr, s);
        refresh();
    }

    void Fader::paint (juce::Graphics& g)
    {
        const auto t = track();
        const float cx = t.getCentreX();
        // The panel's scale beside the slot (MAN p.28): the channels' 10 ... 50,
        // STEREO's 0 ... 70; oo at the bottom.
        const auto law = ctx.engine.getFaderLaw (stereoLaw ? (int) ld::kMain : (int) ld::kChannel);
        const float channel[] = { 10, 5, 0, -5, -10, -15, -20, -30, -40, -50 };
        const float stereo[] = { 0, -5, -10, -15, -20, -30, -40, -50, -60, -70 };
        for (float db : stereoLaw ? stereo : channel)
        {
            const float y = t.getBottom() - t.getHeight() * law.toPosition (db);
            hw::silk (g, juce::Rectangle<float> (0.0f, y - 7.0f, 20.0f, 14.0f), juce::String (std::abs ((int) db)), look(), 10.0f,
                      juce::Justification::centredRight, true);
            // The 0 tick bold (STEREO's at the top).
            g.setColour (look().silkDim.withAlpha (db == 0.0f ? 0.95f : 0.6f));
            const float h = db == 0.0f ? 2.0f : 1.0f;
            g.fillRect (cx - 12.0f, y - h * 0.5f, 6.0f, h);
            g.fillRect (cx + 6.0f, y - h * 0.5f, 6.0f, h);
        }
        hw::silk (g, juce::Rectangle<float> (0.0f, t.getBottom() - 7.0f, 20.0f, 14.0f), utf8 ("\xe2\x88\x9e"), look(), 10.0f,
                  juce::Justification::centredRight, true);
        hw::faderSlot (g, cx, t.getY(), t.getBottom(), look());
        if (addr < 0)
            return;
        const float y = t.getBottom() - t.getHeight() * (float) step / 127.0f;
        hw::faderCap (g, cx, y, 30.0f, look()); // STEREO's the same cap as the others
    }

    void Fader::mouseDown (const juce::MouseEvent&)
    {
        dragStart = step;
        if (onTouch)
            onTouch();
    }

    void Fader::mouseDrag (const juce::MouseEvent& e)
    {
        const float perStep = track().getHeight() / 127.0f;
        moveTo (dragStart - juce::roundToInt ((float) e.getDistanceFromDragStartY() / perStep));
    }

    void Fader::mouseDoubleClick (const juce::MouseEvent&)
    {
        // 0 dB: the channels' step 101, STEREO's top.
        const auto law = ctx.engine.getFaderLaw (stereoLaw ? (int) ld::kMain : (int) ld::kChannel);
        moveTo (juce::roundToInt (law.toPosition (0.0f) * 127.0f));
    }

    void Fader::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (onTouch)
            onTouch();
        moveTo (step + (w.deltaY > 0 ? 1 : -1));
    }

    //==========================================================================
    Encoder::Encoder (const juce::String& l, const Theme& t) : theme (t), label (l) {}

    void Encoder::refresh()
    {
        const int now = get ? get() : 0;
        if (now != value)
        {
            value = now;
            repaint();
        }
    }

    void Encoder::moveTo (int v)
    {
        v = juce::jlimit (lo, hi, v);
        if (v != value && set)
        {
            set (v);
            refresh();
        }
    }

    void Encoder::paint (juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        if (endless)
        {
            // An endless encoder: a knurled black ring round a grey dome, no pointer.
            if (nameBelow)
            {
                auto name = b.removeFromBottom (24.0f);
                g.setColour (look().silk);
                g.setFont (type::font (9.5f, true));
                g.drawFittedText (label, name.toNearestInt(), juce::Justification::centred, 2);
            }
            else
                hw::silk (g, b.removeFromTop (14.0f), label, look(), 10.5f);
            const float size = juce::jmin (b.getWidth(), b.getHeight()) - 4.0f;
            auto ring = juce::Rectangle<float> (size, size).withCentre (b.getCentre());
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillEllipse (ring.translated (0.0f, 2.0f));
            g.setColour (juce::Colour (0xff1c1d1f));
            g.fillEllipse (ring);
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            const auto c = ring.getCentre();
            const float r = size * 0.5f, turn = (float) value * 0.15f;
            for (int k = 0; k < 36; ++k)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) k / 36.0f + turn;
                g.drawLine (c.x + r * 0.8f * std::cos (a), c.y + r * 0.8f * std::sin (a), c.x + r * 0.97f * std::cos (a), c.y + r * 0.97f * std::sin (a), 1.0f);
            }
            auto dome = ring.reduced (size * 0.2f);
            const auto base = cap.isTransparent() ? look().knob : cap;
            g.setGradientFill (juce::ColourGradient (base.brighter (0.35f), dome.getX() + dome.getWidth() * 0.3f, dome.getY() + dome.getHeight() * 0.25f,
                                                     base.darker (0.45f), dome.getRight(), dome.getBottom(), true));
            g.fillEllipse (dome);
            return;
        }
        hw::silk (g, b.removeFromTop (14.0f), label, look(), 10.5f);
        auto textArea = b.removeFromBottom (14.0f);
        const float frac = hi > lo ? (float) (value - lo) / (float) (hi - lo) : 0.0f;
        // The 02R's encoders have no LED rings: the dome's pointer and the value below.
        hw::knob (g, b.reduced (3.0f), frac, look(), hw::Look::Knob::Dome, cap);
        hw::silk (g, textArea, text ? text (value) : juce::String (value), look(), 10.5f, juce::Justification::centred, true);
    }

    void Encoder::mouseDown (const juce::MouseEvent&)
    {
        dragStart = value;
        if (onTouch)
            onTouch();
    }

    void Encoder::mouseDrag (const juce::MouseEvent& e)
    {
        const int range = juce::jmax (1, hi - lo);
        const float perStep = juce::jlimit (1.0f, 6.0f, 200.0f / (float) range);
        moveTo (dragStart - juce::roundToInt ((float) e.getDistanceFromDragStartY() / perStep));
    }

    void Encoder::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (onTouch)
            onTouch();
        moveTo (value + (w.deltaY > 0 ? 1 : -1));
    }

    bool pref (juce::PropertiesFile& settings, Pref p) { return ((settings.getIntValue (prefsKey, kDefaultPrefs) >> (int) p) & 1) != 0; }

    void setPref (juce::PropertiesFile& settings, Pref p, bool on)
    {
        const int was = settings.getIntValue (prefsKey, kDefaultPrefs);
        settings.setValue (prefsKey, on ? (was | (1 << (int) p)) : (was & ~(1 << (int) p)));
    }

    //==========================================================================
    Display::Display (UiContext& c, const Theme& t) : ctx (c), theme (t), y (c.engine.getYamaha02R()) { build(); }

    void Display::editTitle (int library, int n, int target)
    {
        nameFrom = function;
        nameLibrary = library;
        nameProgram = n;
        nameTarget = target;
        const auto name = y.programName (library, n);
        for (int k = 0; k < 16; ++k)
            nameChars[(size_t) k] = juce::jmax (0, kTitleChars.indexOfChar (k < name.length() ? name[k] : ' '));
        function = Function::TitleEdit;
        page = 0;
        focus = 0;
        build();
    }

    void Display::show (Function f, int p)
    {
        if (function == f && page == p)
            return;
        storeChoice = -1;
        function = f;
        page = juce::jlimit (0, numPages() - 1, p);
        focus = 0;
        build();
    }

    void Display::confirm (bool store, const juce::String& what, std::function<void()> go)
    {
        if (! pref (ctx.settings, store ? StoreConfirm : RecallConfirm))
        {
            go();
            return;
        }
        confirmText = what;
        confirmGo = std::move (go);
        confirmChoice = 0;
        confirmSince = juce::Time::getMillisecondCounter();
        repaint();
    }

    void Display::setFunction (Function f)
    {
        confirmGo = nullptr;
        storeChoice = -1;
        // The function's key again: its next page.
        if (f == function)
            page = (page + 1) % numPages();
        else
        {
            function = f;
            page = 0;
        }
        focus = 0;
        build();
    }

    int Display::numPages() const
    {
        switch (function)
        {
            case Function::Scene:
            case Function::Meter:
                return 3;
            case Function::Setup:
                return 4; // SOLO, AUX OUTPUT SELECT, PREFERENCES 1 and 2
            case Function::Midi:
                return 3; // MIDI SETUP, Program Change Assign, Control Change Assign
            case Function::Group:
            case Function::Pair:
            case Function::View: // VIEW, the CHANNEL LIBRARY
            case Function::Att:
            case Function::Delay:
            case Function::Pan:
                return 2;
            default:
                // AUX 1-8: MIC/LINE, TAPE/EFF, the selected strip's; AUX 7-8 their effect's EDIT.
                return function >= Function::Aux7 && function <= Function::Aux8 ? 4 : function >= Function::Aux1 && function <= Function::Aux8 ? 3 : 1;
        }
    }

    void Display::setStrip (int s)
    {
        strip = juce::jlimit (0, kStrips - 1, s);
        build();
    }

    void Display::build()
    {
        fields.clear();
        const int s = strip;
        auto byteField = [this] (const juce::String& label, int a, int lo, int hi, std::function<juce::String (int)> text)
        {
            fields.push_back ({ label, [this, a] { return y.get (a); }, [this, a] (int v) { y.set (a, v); }, std::move (text), lo, hi });
        };
        auto bitField = [this] (const juce::String& label, int a, int bit)
        {
            fields.push_back ({ label,
                                [this, a, bit] { return (y.get (a) >> bit) & 1; },
                                [this, a, bit] (int v)
                                {
                                    const int byte = juce::jmax (0, y.get (a));
                                    y.set (a, v != 0 ? (byte | (1 << bit)) : (byte & ~(1 << bit)));
                                },
                                [] (int v) { return juce::String (v != 0 ? "ON" : "OFF"); },
                                0,
                                1 });
        };
        auto stateField = [this] (const juce::String& label, Yamaha02R::State what, int i, int lo, int hi, std::function<juce::String (int)> text)
        {
            if (y.getState (what, i) < 0)
                return;
            fields.push_back ({ label, [this, what, i] { return y.getState (what, i); }, [this, what, i] (int v) { y.setState (what, i, v); },
                                std::move (text), lo, hi });
        };
        auto onOff = [] (int v) { return juce::String (v != 0 ? "ON" : "OFF"); };
        auto levelText = [this, s] (int step)
        {
            const auto law = ctx.engine.getFaderLaw (s == kStereo ? (int) ld::kMain : (int) ld::kChannel);
            const float db = law.toDb ((float) step / 127.0f);
            return db <= law.minDb() - 0.5f ? utf8 ("-\xe2\x88\x9e") : juce::String (db, 1) + " dB";
        };
        // STORE: the wheel picks a user program, ENTER opens its TITLE EDIT.
        auto storeField = [this] (int library, int target)
        {
            if (storeChoice < kFirstUser[library])
                storeChoice = kFirstUser[library];
            Field f;
            f.label = "STORE";
            f.lo = kFirstUser[library];
            f.hi = library == Yamaha02R::Channel ? 63 : 127;
            f.get = [this] { return storeChoice; };
            f.set = [this] (int v) { storeChoice = v; };
            f.text = [this, library] (int n)
            {
                const auto name = y.programName (library, n);
                return juce::String (n + 1).paddedLeft ('0', library == Yamaha02R::Channel ? 2 : 3) + " " + (name.isEmpty() ? juce::String ("(empty)") : name);
            };
            f.action = [this, library, target] { editTitle (library, storeChoice, target); };
            fields.push_back (f);
        };
        auto libraryField = [this, storeField] (const juce::String& label, int library, int target)
        {
            Field f;
            f.label = label;
            f.lo = 0;
            f.hi = 127;
            f.onEnter = true;
            f.get = [] { return 0; };
            f.text = [this, library] (int n) { return juce::String (n + 1).paddedLeft ('0', 3) + " " + y.programName (library, n); };
            f.set = [this, library, target] (int n)
            {
                confirm (false, "RECALL " + y.programName (library, n) + "?", [this, library, n, target] { y.recall (library, n, target); });
            };
            fields.push_back (f);
            storeField (library, target);
        };
        const auto eqModel = models::findModel (models::ChannelEq, "Y2EQ");
        // A strip's short name on an all-channel page; the strips of page 1
        // (MIC/LINE) and page 2 (TAPE, EFF and STEREO).
        const auto shortName = [] (int st)
        {
            return st < 16 ? juce::String (st + 1)
                 : st < kTape ? juce::String (17 + 2 * (st - 16)) + "/" + juce::String (18 + 2 * (st - 16))
                 : st < kEff ? "T" + juce::String (st - kTape + 1)
                 : st < kBus ? "EFF" + juce::String (st - kEff + 1)
                 : st < kAux ? "BUS" + juce::String (st - kBus + 1)
                 : st < kStereo ? "AUX" + juce::String (st - kAux + 1) : juce::String ("ST");
        };
        std::vector<int> pageStrips;
        for (int st = page == 0 ? 0 : kTape; st < (page == 0 ? kTape : kBus); ++st)
            pageStrips.push_back (st);
        if (page == 1)
            pageStrips.push_back (kStereo);
        const auto fixed = [this] (const juce::String& label, const juce::String& value)
        {
            fields.push_back ({ label, [] { return 0; }, [] (int) {}, [value] (int) { return value; }, 0, 0 });
        };
        const bool allChannels = (function == Function::Att || function == Function::Delay || function == Function::Pan)
                                 || (function >= Function::Aux1 && function <= Function::Aux8 && page < 2);
        if (allChannels)
        {
            const int aux = (int) function - (int) Function::Aux1;
            for (int st : pageStrips)
            {
                Field f;
                f.label = shortName (st);
                if (function == Function::Att)
                {
                    // The attenuator; ENTER flips the phase.
                    const int a = address (st, kAtt), sw = address (st, kSwitches);
                    f.get = [this, a] { return y.get (a); };
                    f.set = [this, a] (int v) { y.set (a, v); };
                    f.hi = st == kStereo ? 108 : 96;
                    f.text = [this, sw, st] (int v) { return juce::String (v - 96) + "dB" + (st != kStereo && ((y.get (sw) >> kPhase) & 1) != 0 ? utf8 (" \xc3\x98") : juce::String()); };
                    if (st != kStereo)
                        f.action = [this, sw] { y.set (sw, juce::jmax (0, y.get (sw)) ^ (1 << kPhase)); };
                }
                else if (function == Function::Delay)
                {
                    if (memberOf (st) < 0)
                        continue;
                    // The time in samples; ENTER switches the delay on or off.
                    const int hi = address (st, kDelayHi), lo = address (st, kDelayLo), sw = address (st, kSwitches);
                    f.get = [this, hi, lo] { return (juce::jmax (0, y.get (hi)) << 7) | juce::jmax (0, y.get (lo)); };
                    f.set = [this, hi, lo] (int v) { y.set (hi, v >> 7); y.set (lo, v & 0x7f); };
                    f.hi = 2600;
                    f.text = [this, sw] (int v) { return juce::String (v) + (((y.get (sw) >> kDelayOn) & 1) != 0 ? "" : " off"); };
                    f.action = [this, sw] { y.set (sw, juce::jmax (0, y.get (sw)) ^ (1 << kDelayOn)); };
                }
                else if (function == Function::Pan)
                {
                    const int a = address (st, kPanL);
                    f.get = [this, a] { return y.get (a); };
                    f.set = [this, a] (int v) { y.set (a, v); };
                    f.hi = 32;
                    f.text = panText;
                }
                else
                {
                    if (memberOf (st) < 0)
                        continue;
                    // The send's level; ENTER switches PRE and POST.
                    const int a = sendAddress (memberOf (st), aux), on = sendAddress (memberOf (st), 8), pre = sendAddress (memberOf (st), 9);
                    f.get = [this, a] { return y.get (a); };
                    f.set = [this, a] (int v) { y.set (a, v); };
                    f.hi = 127;
                    f.text = [this, on, pre, aux, levelText] (int v)
                    {
                        return levelText (v) + (((y.get (pre) >> aux) & 1) != 0 ? " PRE" : "") + (((y.get (on) >> aux) & 1) != 0 ? "" : " OFF");
                    };
                    f.action = [this, pre, aux] { y.set (pre, juce::jmax (0, y.get (pre)) ^ (1 << aux)); };
                }
                fields.push_back (f);
            }
            focus = juce::jlimit (0, juce::jmax (0, (int) fields.size() - 1), focus);
            repaint();
            return;
        }
        if (function == Function::View && page == 1)
        {
            // VIEW 2/2, the CHANNEL LIBRARY (V2 MAN pp.48-50): 64 programs of an
            // input strip's settings and the UNDO ("U"); RECALL, STORE (its
            // TITLE EDIT), CLEAR (into the UNDO), EXIT back to VIEW 1/2.
            if (memberOf (s) < 0)
                fixed ("LIBRARY", "INPUT CHANNELS ONLY");
            else
            {
                const auto programText = [this] (int n)
                {
                    if (n == 64)
                    {
                        const auto undo = y.programName (Yamaha02R::Channel, 64);
                        return undo.isEmpty() ? juce::String ("U: (empty)") : undo;
                    }
                    const auto name = y.programName (Yamaha02R::Channel, n);
                    return juce::String (n + 1).paddedLeft ('0', 2) + " " + (name.isEmpty() ? juce::String ("(empty)") : name);
                };
                Field recall;
                recall.label = "RECALL";
                recall.lo = 0;
                recall.hi = 64;
                recall.onEnter = true;
                recall.get = [] { return 0; };
                recall.text = programText;
                recall.set = [this, s, programText] (int n)
                {
                    confirm (false, "RECALL " + programText (n) + "?", [this, n, s] { y.recall (Yamaha02R::Channel, n, s); });
                };
                fields.push_back (recall);
                storeField (Yamaha02R::Channel, s);
                Field clear;
                clear.label = "CLEAR";
                clear.lo = 0;
                clear.hi = 63;
                clear.onEnter = true;
                clear.get = [] { return 0; };
                clear.text = programText;
                clear.set = [this, programText] (int n)
                {
                    confirm (true, "CLEAR " + programText (n) + "?", [this, n]
                    {
                        if (y.clearChannel (n) && libraryChanged)
                            libraryChanged();
                    });
                };
                fields.push_back (clear);
                Field exit;
                exit.label = "EXIT";
                exit.text = [] (int) { return juce::String ("VIEW 1/2"); };
                exit.get = [] { return 0; };
                exit.lo = exit.hi = 0;
                exit.action = [this]
                {
                    page = 0;
                    focus = 0;
                    build();
                };
                fields.push_back (exit);
            }
            focus = juce::jlimit (0, juce::jmax (0, (int) fields.size() - 1), focus);
            repaint();
            return;
        }
        switch (function)
        {
            case Function::View:
            case Function::Att:
                if (function == Function::View)
                    byteField ("FADER", address (s, kLevel), 0, 127, levelText), bitField ("ON", address (s, kOn), 0);
                byteField ("ATT", address (s, kAtt), 0, s == kStereo ? 108 : 96, [] (int v) { return juce::String (v - 96) + " dB"; });
                if (s != kStereo)
                    bitField (utf8 ("\xc3\x98 PHASE"), address (s, kSwitches), kPhase);
                if (function == Function::Att)
                    break;
                [[fallthrough]];
            case Function::Pan:
                if (isStereoStrip (s))
                {
                    byteField ("PAN L", address (s, kPanL), 0, 32, panText);
                    byteField ("PAN R", address (s, kPanR), 0, 32, panText);
                }
                else if (s < kBus || s == kStereo)
                {
                    byteField (s == kStereo ? "BALANCE" : "PAN", address (s, kPanL), 0, 32, panText);
                }
                if (pairOf (s) >= 0)
                    stateField ("GANG", Yamaha02R::Gang, pairOf (s), 0, 1, onOff);
                if (function == Function::Pan)
                    break;
                if (memberOf (s) >= 0)
                {
                    stateField ("FADER GRP", Yamaha02R::FaderGroup, memberOf (s), 0, 4, [] (int v) { return v == 0 ? juce::String ("-") : juce::String::charToString ('A' + v - 1); });
                    stateField ("MUTE GRP", Yamaha02R::MuteGroup, memberOf (s), 0, 4, [] (int v) { return v == 0 ? juce::String ("-") : juce::String::charToString ('E' + v - 1); });
                    // LIB: ENTER opens the CHANNEL LIBRARY (VIEW 2/2), as VIEW again does.
                    Field lib;
                    lib.label = "LIB";
                    lib.text = [] (int) { return juce::String ("CHANNEL LIBRARY"); };
                    lib.get = [] { return 0; };
                    lib.lo = lib.hi = 0;
                    lib.action = [this]
                    {
                        page = 1;
                        focus = 0;
                        build();
                    };
                    fields.push_back (lib);
                }
                break;
            case Function::Delay:
                if (memberOf (s) < 0)
                    break;
                bitField ("DELAY", address (s, kSwitches), kDelayOn);
                fields.push_back ({ "TIME",
                                    [this, s] { return (juce::jmax (0, y.get (address (s, kDelayHi))) << 7) | juce::jmax (0, y.get (address (s, kDelayLo))); },
                                    [this, s] (int v)
                                    {
                                        y.set (address (s, kDelayHi), v >> 7);
                                        y.set (address (s, kDelayLo), v & 0x7f);
                                    },
                                    [this] (int v)
                                    {
                                        const double sr = juce::jmax (1.0, ctx.engine.getSampleRate());
                                        return juce::String (v) + " smpl  " + juce::String (1000.0 * v / sr, 1) + " ms";
                                    },
                                    0,
                                    2600 });
                break;
            case Function::Routing:
                if (memberOf (s) < 0)
                    break;
                for (int b = 0; b < 8; ++b)
                    bitField ("BUS " + juce::String (b + 1), address (s, kRouting), b);
                bitField ("ST", address (s, kSwitches), kSt);
                if (s < 16)
                    stateField ("DIRECT", Yamaha02R::Direct, s, 0, 1, onOff);
                break;
            case Function::Eq:
            {
                if (! hasEq (s) || eqModel < 0)
                    break;
                bitField ("EQ", address (s, kSwitches), kEqOn);
                const auto& m = models::model (models::ChannelEq, eqModel);
                const char* bands[] = { "LOW", "L-MID", "H-MID", "HIGH" };
                const char* what[] = { "F", "G", "Q" };
                for (int k = 0; k < 12 && k < (int) m.params.size(); ++k)
                {
                    const auto p = m.params[(size_t) k];
                    byteField (juce::String (bands[k / 3]) + " " + what[k % 3], address (s, kEq + k), 0, juce::jmax (1, p.steps) - 1,
                               [p, k] (int v) { return k % 3 == 0 ? freqText (v) : p.display (stepValue (p, v)); });
                }
                bitField ("HPF", address (s, kSwitches), kHpf);
                bitField ("LPF", address (s, kSwitches), kLpf);
                libraryField ("LIBRARY", Yamaha02R::Eq, s);
                break;
            }
            case Function::Dynamics:
            {
                if (! hasDynamics (s))
                    break;
                bitField ("DYNAMICS", address (s, kSwitches), kDynOn);
                byteField ("TYPE", address (s, kDynType), 0, 5, [] (int v) { return juce::String (kDynNames[juce::jlimit (0, 5, v)]); });
                const int type = juce::jlimit (0, 5, y.get (address (s, kDynType)));
                const auto& m = models::model (models::Dynamics, juce::jmax (0, models::findModel (models::Dynamics, kDynTokens[type])));
                for (int k = 0; k < 6 && k < (int) m.params.size(); ++k)
                {
                    const auto p = m.params[(size_t) k];
                    byteField (p.label, address (s, kDynParam + k), 0, juce::jmax (1, p.steps) - 1, [p] (int v) { return p.display (stepValue (p, v)); });
                }
                const char* keys[] = { "SELF-POST EQ", "SELF-PRE EQ", "LEFT-POST EQ", "LEFT-PRE EQ", "AUX 1", "AUX 2", "BUS 1", "BUS 2", "BUS 3", "BUS 4", "BUS 5", "BUS 6", "BUS 7", "BUS 8", "STEREO" };
                stateField ("KEY IN", Yamaha02R::KeyIn, s, 0, s >= kBus ? 14 : 5, [keys] (int v) { return juce::String (keys[juce::jlimit (0, 14, v)]); });
                if (pairOf (s) >= 0 || isStereoStrip (s))
                {
                    stateField ("ST.LINK", Yamaha02R::StLink, s, 0, 1, onOff);
                    stateField ("IN", Yamaha02R::InMode, s, 0, 2, [] (int v) { return juce::String (v == 0 ? "BOTH" : v == 1 ? "L" : "R"); });
                }
                libraryField ("LIBRARY", Yamaha02R::Dynamics, s);
                break;
            }
            case Function::Group:
            {
                // 1/2 the fader groups A-D, 2/2 the mute groups E-H: each group on,
                // and every channel's group.
                const bool mute = page == 1;
                for (int g = 0; g < 4; ++g)
                    stateField ("GRP " + juce::String::charToString ((mute ? 'E' : 'A') + g), Yamaha02R::GroupOn, (mute ? 4 : 0) + g, 0, 1, onOff);
                for (int st = 0; st < kBus; ++st)
                    stateField (shortName (st), mute ? Yamaha02R::MuteGroup : Yamaha02R::FaderGroup, memberOf (st), 0, 4, [mute] (int v)
                                { return v == 0 ? juce::String ("-") : juce::String::charToString ((mute ? 'E' : 'A') + v - 1); });
                break;
            }
            case Function::Pair:
                if (page == 0)
                {
                    // 1/2 the channel pairs: MIC 1/2 ... 15/16, TAPE 1/2 ... 15/16.
                    for (int k = 0; k < 16; ++k)
                        stateField ((k < 8 ? "" : "T") + juce::String (2 * (k % 8) + 1) + "/" + juce::String (2 * (k % 8) + 2), Yamaha02R::Pair, k, 0, 1, onOff);
                    break;
                }
                for (int k = 0; k < 4; ++k)
                    stateField ("AUX " + juce::String (2 * k + 1) + "/" + juce::String (2 * k + 2), Yamaha02R::AuxPair, k, 0, 1, onOff);
                for (int k = 0; k < 4; ++k)
                    stateField ("BUS " + juce::String (2 * k + 1) + "/" + juce::String (2 * k + 2), Yamaha02R::BusPair, k, 0, 1, onOff);
                break;
            case Function::Scene:
                if (page == 0)
                {
                    // The scenes' titles: ENTER recalls one.
                    auto& show = ctx.show;
                    for (int n = 0; n < juce::jmin (24, show.getNumScenes()); ++n)
                    {
                        auto scene = show.getScene (n);
                        Field f;
                        f.label = juce::String (n + 1).paddedLeft ('0', 2);
                        const auto name = scene.has_value() ? scene->name : juce::String();
                        f.get = [] { return 0; };
                        f.set = [] (int) {};
                        f.text = [name] (int) { return name; };
                        f.action = [this, n] { confirm (false, "RECALL SCENE " + juce::String (n + 1) + "?", [this, n] { ctx.show.recallScene (n); }); };
                        fields.push_back (f);
                    }
                    if (fields.empty())
                        fixed ("SCENES", "NONE STORED");
                    break;
                }
                // Recall safe (with its ENABLE), or the fade times, of every fader.
                if (page == 1)
                    stateField ("ENABLE", Yamaha02R::RecallSafeOn, 0, 0, 1, onOff);
                for (int st = 0; st <= kBus; ++st)
                {
                    const int strip_ = st == kBus ? kStereo : st;
                    if (page == 1)
                        stateField (shortName (strip_), Yamaha02R::RecallSafe, faderIndexOf (strip_), 0, 1, onOff);
                    else
                        stateField (shortName (strip_), Yamaha02R::FadeTime, faderIndexOf (strip_), 0, 100, [] (int v) { return juce::String (v / 10.0, 1) + "s"; });
                }
                break;
            case Function::Utility:
            {
                stateField ("OSC", Yamaha02R::OscOn, 0, 0, 1, onOff);
                stateField ("LEVEL", Yamaha02R::OscLevel, 0, 0, 96, [] (int v) { return "-" + juce::String (v) + " dB"; });
                stateField ("WAVE", Yamaha02R::OscWave, 0, 0, 3, [] (int v) {
                    const char* w[] = { "100 Hz", "1 kHz", "10 kHz", "NOISE" };
                    return juce::String (w[juce::jlimit (0, 3, v)]);
                });
                // Where it plays: BUS 1-8 (bits 0-7), STEREO (8), AUX 1-8 (9-16).
                for (int bit = 0; bit < 17; ++bit)
                    fields.push_back ({ bit < 8 ? "BUS " + juce::String (bit + 1) : bit == 8 ? juce::String ("STEREO") : "AUX " + juce::String (bit - 8),
                                        [this, bit] { return (y.getState (Yamaha02R::OscAssign, 0) >> bit) & 1; },
                                        [this, bit] (int v)
                                        {
                                            const int a = juce::jmax (0, y.getState (Yamaha02R::OscAssign, 0));
                                            y.setState (Yamaha02R::OscAssign, 0, v != 0 ? (a | (1 << bit)) : (a & ~(1 << bit)));
                                        },
                                        onOff,
                                        0,
                                        1 });
                break;
            case Function::DigitalIo:
            {
                // DIGITAL I/O (MAN pp.154-159): Sidecar's clock and inputs are
                // its audio interface's, so the page shows them as fixed.
                fixed ("WORD CLOCK", "INT " + juce::String (ctx.engine.getSampleRate() / 1000.0, 1) + "k");
                fixed ("MIC 1-8", "ANALOG");
                fixed ("MIC 9-16", "ANALOG");
                fixed ("LINE 17/18", "ANALOG");
                fixed ("LINE 19/20", "ANALOG");
                fixed ("CASCADE", "NONE");
                fixed ("SLOT 1-4", "NONE");
                fixed ("DITHER", "OFF");
                break;
            }
            case Function::Setup:
            {
                if (page > 0)
                {
                    auto prefField = [this] (const juce::String& label, Pref p)
                    {
                        fields.push_back ({ label, [this, p] { return pref (ctx.settings, p) ? 1 : 0; }, [this, p] (int v) { setPref (ctx.settings, p, v != 0); },
                                            [] (int v) { return juce::String (v != 0 ? "ON" : "OFF"); }, 0, 1 });
                    };
                    if (page == 1)
                    {
                        // AUX OUTPUT SELECT (V2 MAN p.203): the analog aux outs carry AUX 1-6.
                        fixed ("AUX OUT 1-6", "AUX 1-6");
                        fixed ("BUS 1-6", "NOT EMULATED");
                    }
                    else if (page == 2)
                    {
                        // PREFERENCES 1 (V2 MAN pp.198-200): what Sidecar acts on; the
                        // rest are the hardware's (word clock, timecode, MB02, cards).
                        prefField ("AUTO EQ DISP", AutoEq);
                        prefField ("AUTO PAN DISP", AutoPan);
                        prefField ("AUTO ROUTING", AutoRouting);
                        prefField ("AUTO SOLO DISP", AutoSolo);
                        prefField ("STORE CONFIRM", StoreConfirm);
                        prefField ("RECALL CONFIRM", RecallConfirm);
                        prefField ("INIT NOMINAL", InitialNominal);
                        fixed ("OTHERS", "NOT EMULATED");
                    }
                    else
                    {
                        // PREFERENCES 2 (V2 MAN pp.201-202): the automix's aren't Sidecar's.
                        prefField ("TOUCH SELECT", TouchSelect);
                        fixed ("FLIP RCL SAFE", "ON");
                        fixed ("AUTOMIX", "NOT EMULATED");
                    }
                    break;
                }
                // SETUP's solo (MAN p.160): where the channels and the outputs are heard.
                auto& e = ctx.engine;
                const auto listen = [] (int v) { return juce::String (v != 0 ? "AFTER PAN" : "PRE FADER"); };
                fields.push_back ({ "SOLO CH", [&e] { return e.soloAfl.load() ? 1 : 0; }, [&e] (int v) { e.soloAfl = v != 0; }, listen, 0, 1 });
                fields.push_back ({ "SOLO OUT", [&e] { return e.busSoloAfl.load() ? 1 : 0; }, [&e] (int v) { e.busSoloAfl = v != 0; }, listen, 0, 1 });
                fixed ("SOLO MODE", "MIXDOWN");
                fixed ("SOLO SAFE", "NONE");
                break;
            }
            case Function::TitleEdit:
                // Sixteen characters: the cursor keys choose one, the wheel changes it, ENTER stores the program.
                for (int k = 0; k < 16; ++k)
                {
                    Field f;
                    f.label = k == 0 ? "PROGRAM " + juce::String (nameProgram + 1) : juce::String();
                    f.lo = 0;
                    f.hi = kTitleChars.length() - 1;
                    f.get = [this, k] { return nameChars[(size_t) k]; };
                    f.set = [this, k] (int v) { nameChars[(size_t) k] = v; };
                    f.text = [] (int v) { return juce::String::charToString (kTitleChars[v]); };
                    f.action = [this]
                    {
                        juce::String title;
                        for (int c : nameChars)
                            title << kTitleChars[c];
                        const auto stored = title.trimEnd();
                        confirm (true, "STORE TO " + juce::String (nameProgram + 1) + "?", [this, stored, library = nameLibrary, n = nameProgram, target = nameTarget]
                        {
                            y.store (library, n, target, stored);
                            if (libraryChanged)
                                libraryChanged();
                        });
                        function = nameFrom;
                        page = nameLibrary == Yamaha02R::Channel ? 1 : 0; // back to the CHANNEL LIBRARY
                        focus = 0;
                        build();
                    };
                    fields.push_back (f);
                }
                break;
            case Function::Automix:
                // AUTOMIX (MAN pp.123-140): Sidecar has no timecode automation.
                fixed ("TIME CODE", "INT");
                fixed ("POSITION", "00:00:00.00");
                fixed ("AUTOMIX", "NOT EMULATED");
                break;
            }
            case Function::Midi:
            {
                if (page == 2)
                {
                    // Control Change Assign (V2 p.164): a controller picked (0-95,
                    // 102-119), what it moves: FADER, ON, PAN or a SEND, of a strip.
                    const auto save = [this] { ctx.settings.setValue (Y02RMidi::ccKey, y.getCcText()); };
                    const auto describe = [] (int code)
                    {
                        const int kind = code & 7, extra = (code >> 3) & 7, st = code >> 8;
                        const char* kinds[] = { "--", "FADER", "ON", "PAN", "SEND" };
                        if (kind == 0 || kind > 4)
                            return juce::String ("--");
                        return juce::String (kinds[kind]) + " " + stripName (st) + (kind == 4 ? " AUX" + juce::String (extra + 1) : kind == 3 && extra > 0 ? " R" : "");
                    };
                    fields.push_back ({ "CC", [this] { return ccNumber; },
                                        [this] (int v) { ccNumber = v >= 96 && v < 102 ? (v > ccNumber ? 102 : 95) : v; build(); },
                                        [] (int v) { return juce::String (v); }, 0, 119 });
                    const int code = y.ccCode (ccNumber);
                    const auto setPart = [this, save] (int kind, int extra, int st)
                    {
                        y.setCcCode (ccNumber, kind == 0 ? 0 : kind | (extra << 3) | (st << 8));
                        save();
                        build();
                    };
                    const int kind = code & 7, extra = (code >> 3) & 7, st = code >> 8;
                    fields.push_back ({ "MOVES", [kind] { return kind; }, [setPart, extra, st] (int v) { setPart (v, v == 4 || v == 3 ? extra : 0, st); },
                                        [] (int v) { const char* k[] = { "--", "FADER", "ON", "PAN", "SEND" }; return juce::String (k[juce::jlimit (0, 4, v)]); }, 0, 4 });
                    if (kind > 0)
                    {
                        fields.push_back ({ "STRIP", [st] { return st; }, [setPart, kind, extra] (int v) { setPart (kind, extra, v); },
                                            [] (int v) { return stripName (v); }, 0, kStrips - 1 });
                        if (kind == 3 || kind == 4)
                            fields.push_back ({ kind == 4 ? "AUX" : "SIDE", [extra] { return extra; }, [setPart, kind, st] (int v) { setPart (kind, v, st); },
                                                [kind] (int v) { return kind == 4 ? juce::String (v + 1) : juce::String (v == 0 ? "L" : "R"); }, 0, kind == 4 ? 7 : 1 });
                    }
                    for (int k = 1; k <= 12; ++k)
                    {
                        int cc = ccNumber + k;
                        if (cc >= 96 && cc < 102)
                            cc += 6;
                        if (cc > 119)
                            break;
                        fixed ("CC " + juce::String (cc), describe (y.ccCode (cc)));
                    }
                    break;
                }
                if (page == 1)
                {
                    // Program Change Assign (MAN p.144): a program picked, the scene it recalls; the next ones.
                    fields.push_back ({ "PGM", [this] { return pcProgram; }, [this] (int v) { pcProgram = v; build(); },
                                        [] (int v) { return juce::String (v + 1); }, 0, 127 });
                    fields.push_back ({ "SCENE", [this] { return y.pcScene (pcProgram) + 1; },
                                        [this] (int v)
                                        {
                                            y.setPcScene (pcProgram, v - 1);
                                            ctx.settings.setValue (Y02RMidi::pcKey, y.getPcText());
                                        },
                                        [] (int v) { return v <= 0 ? juce::String ("--") : juce::String (v - 1).paddedLeft ('0', 2); }, 0, 97 });
                    for (int k = 0; k < 14; ++k)
                    {
                        const int pc = (pcProgram + 1 + k) % 128;
                        fixed ("PGM " + juce::String (pc + 1), y.pcScene (pc) < 0 ? juce::String ("--") : juce::String (y.pcScene (pc)).paddedLeft ('0', 2));
                    }
                    break;
                }
                auto* midi = dynamic_cast<Y02RMidi*> (ctx.consoleMidi);
                if (midi == nullptr)
                    break;
                const char* names[] = { "CHANNEL", "PGM RX", "PGM TX", "PGM OMNI", "PGM ECHO", "PARAM RX", "PARAM TX", "CTRL RX" };
                for (int k = 0; k < 8; ++k)
                {
                    auto value = [midi, k]
                    {
                        juce::StringArray v;
                        v.addTokens (midi->getSetupText(), " ", "");
                        return v[k].getIntValue();
                    };
                    fields.push_back ({ names[k], value,
                                        [midi, k] (int x)
                                        {
                                            juce::StringArray v;
                                            v.addTokens (midi->getSetupText(), " ", "");
                                            v.set (k, juce::String (x));
                                            midi->setSetupText (v.joinIntoString (" "));
                                        },
                                        [k] (int x) { return k == 0 ? juce::String (x) : juce::String (x != 0 ? "ON" : "OFF"); },
                                        k == 0 ? 1 : 0,
                                        k == 0 ? 16 : 1 });
                }
                break;
            }
            case Function::Meter:
                // Where the meters read (MAN p.41): inputs PRE EQ, POST EQ or POST
                // FADER; outputs PRE or POST FADER.
                fields.push_back ({ "METER POINT", [this] { return meterPoint; }, [this] (int v) { meterPoint = v; },
                                    [this] (int v)
                                    {
                                        const char* in[] = { "PRE EQ", "POST EQ", "POST FADER" };
                                        const char* out[] = { "PRE FADER", "PRE FADER", "POST FADER" };
                                        return juce::String (page == 2 ? out[v] : in[v]);
                                    },
                                    0, 2 });
                break;
            default:
            {
                // AUX 1-8: the strip's send, the aux master; AUX 7-8 their effect's library.
                const int aux = (int) function - (int) Function::Aux1;
                if (aux >= 6 && page == 3)
                {
                    // EFFECT EDIT (MAN pp.90-111): the effect's type and parameters.
                    auto& fx = ctx.engine.getFx();
                    const int slot = aux - 6, m = fx.getModel (slot);
                    if (m <= ld::kFxExt)
                        break;
                    const auto& model = models::model (models::Fx, m);
                    auto& values = fx.params (slot).values;
                    fixed ("EFFECT " + juce::String (slot + 1), model.name);
                    for (int k = 0; k < juce::jmin (23, (int) model.params.size()); ++k)
                    {
                        auto b = bindModelParam (values[k], model.params[(size_t) k]);
                        const int hi = b.steps > 1.0f ? (int) b.steps - 1 : 100;
                        fields.push_back ({ model.params[(size_t) k].label, [b, hi] { return juce::roundToInt (b.get() * (float) hi); },
                                            [b, hi] (int v) { b.set ((float) v / (float) hi); }, [b] (int) { return b.text(); }, 0, hi });
                    }
                    break;
                }
                if (memberOf (s) >= 0)
                {
                    const int a = sendAddress (memberOf (s), aux);
                    byteField ("SEND", a, 0, 127, levelText);
                    bitField ("SEND ON", sendAddress (memberOf (s), 8), aux);
                    fields.push_back ({ "PRE / POST",
                                        [this, a = sendAddress (memberOf (s), 9), aux] { return (y.get (a) >> aux) & 1; },
                                        [this, a = sendAddress (memberOf (s), 9), aux] (int v)
                                        {
                                            const int byte = juce::jmax (0, y.get (a));
                                            y.set (a, v != 0 ? (byte | (1 << aux)) : (byte & ~(1 << aux)));
                                        },
                                        [] (int v) { return juce::String (v != 0 ? "PRE" : "POST"); },
                                        0,
                                        1 });
                }
                byteField ("AUX " + juce::String (aux + 1) + " MASTER", address (kAux + aux, kLevel), 0, 127, levelText);
                if (aux >= 6)
                    libraryField ("EFFECT " + juce::String (aux - 5), Yamaha02R::Effects, aux - 6);
                break;
            }
        }
        focus = juce::jlimit (0, juce::jmax (0, (int) fields.size() - 1), focus);
        repaint();
    }

    juce::String Display::title() const
    {
        const char* names[] = { "SCENE MEMORY", "UTILITY", "MIDI", "GROUP", "PAIR", "PHASE / ATT", "DELAY", "PAN", "ROUTING", "METER", "VIEW",
                                "EQ", "DYNAMICS", "AUX 1", "AUX 2", "AUX 3", "AUX 4", "AUX 5", "AUX 6", "AUX 7 (EFF1)", "AUX 8 (EFF2)" };
        const char* more[] = { "DIGITAL I/O", "SETUP", "AUTOMIX", "TITLE EDIT" };
        const int f = (int) function;
        if (function == Function::View && page == 1)
            return "CHANNEL LIBRARY  2/2";
        if (function == Function::Setup)
        {
            const char* setup[] = { "SOLO", "AUX OUTPUT SELECT", "PREFERENCES 1", "PREFERENCES 2" };
            return juce::String ("SETUP ") + juce::String (page + 1) + "/4  " + setup[juce::jlimit (0, 3, page)];
        }
        const juce::String name = f < (int) std::size (names) ? juce::String (names[f]) : juce::String (more[f - (int) std::size (names)]);
        return numPages() > 1 ? name + "  " + juce::String (page + 1) + "/" + juce::String (numPages()) : name;
    }

    void Display::cursor (int delta)
    {
        if (confirmGo != nullptr)
        {
            confirmChoice = delta > 0 ? 1 : 0; // CANCEL, EXECUTE
            repaint();
            return;
        }
        if (fields.empty())
            return;
        focus = (focus + delta + (int) fields.size()) % (int) fields.size();
        repaint();
    }

    void Display::turn (int delta)
    {
        if (confirmGo != nullptr)
            return;
        if (! juce::isPositiveAndBelow (focus, (int) fields.size()))
            return;
        auto& f = fields[(size_t) focus];
        if (f.onEnter)
        {
            f.pending = juce::jlimit (f.lo, f.hi, (f.pending < 0 ? f.get() : f.pending) + delta);
            repaint();
            return;
        }
        if (f.get && f.set)
            f.set (juce::jlimit (f.lo, f.hi, f.get() + delta));
        // The dynamics' parameters follow a new type.
        if (function == Function::Dynamics && f.label == "TYPE")
            build();
        repaint();
    }

    void Display::enter()
    {
        if (confirmGo != nullptr)
        {
            auto go = std::move (confirmGo);
            confirmGo = nullptr;
            if (confirmChoice == 1)
                go();
            build();
            return;
        }
        if (! juce::isPositiveAndBelow (focus, (int) fields.size()))
            return;
        auto& f = fields[(size_t) focus];
        if (f.action)
        {
            f.action();
            repaint();
            return;
        }
        if (f.onEnter)
        {
            if (f.pending >= 0 && f.set)
                f.set (f.pending);
            f.pending = -1;
            build();
        }
        else if (f.lo == 0 && f.hi == 1 && f.get && f.set)
        {
            f.set (f.get() != 0 ? 0 : 1);
        }
        repaint();
    }

    void Display::refresh()
    {
        // A dialog left alone cancels after about ten seconds (V2 MAN p.48).
        if (confirmGo != nullptr && juce::Time::getMillisecondCounter() - confirmSince > 10000)
            confirmGo = nullptr;
        repaint();
    }

    juce::Rectangle<float> Display::fieldBox (int i) const
    {
        auto area = getLocalBounds().toFloat().reduced (14.0f).withTrimmedTop (40.0f).withTrimmedBottom (28.0f);
        if (function == Function::Meter || function == Function::Eq)
            area = area.withTrimmedRight (area.getWidth() * (function == Function::Eq ? 0.42f : 0.0f));
        const int n = (int) fields.size();
        const int cols = function == Function::Eq || n <= 16 ? 4 : n <= 24 ? 6 : 8; // EQ beside its curve: four
        const int rows = juce::jmax (4, (n + cols - 1) / cols);
        const float w = area.getWidth() / cols, h = juce::jmin (52.0f, area.getHeight() / (float) rows);
        return { area.getX() + w * (float) (i % cols), area.getY() + h * (float) (i / cols), w - 6.0f, h - 6.0f };
    }

    void Display::paint (juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        hw::lcd (g, b, look());
        auto top = b.reduced (16.0f, 12.0f).removeFromTop (26.0f);
        g.setFont (type::font (type::kLabel, true));
        g.setColour (kPixel);
        g.drawText (title(), top, juce::Justification::centredLeft);
        g.drawText (stripName (strip), top, juce::Justification::centred);
        g.setColour (kPixelDim);
        g.drawText (memoryText ? memoryText() : juce::String(), top, juce::Justification::centredRight);
        g.drawHorizontalLine ((int) top.getBottom() + 2, b.getX() + 14.0f, b.getRight() - 14.0f);

        if (function == Function::Meter)
            paintMeters (g, b.reduced (14.0f).withTrimmedTop (96.0f).withTrimmedBottom (24.0f)); // METER POINT over them
        if (function == Function::Eq)
            paintEq (g, b.reduced (14.0f).withTrimmedTop (40.0f).withTrimmedBottom (28.0f).withTrimmedLeft (b.getWidth() * 0.6f));
        if (fields.empty())
        {
            g.setColour (kPixelDim);
            g.drawText (stripName (strip) + " has no " + title(), b, juce::Justification::centred);
        }
        if (function == Function::View && page == 1 && fields.size() > 1)
            paintPrograms (g, b.reduced (14.0f).withTrimmedBottom (14.0f).withTop (fieldBox (0).getBottom() + 10.0f));
        for (int i = 0; i < (int) fields.size(); ++i)
        {
            const auto& f = fields[(size_t) i];
            const auto box = fieldBox (i);
            const bool focused = i == focus;
            g.setColour (focused ? kPixel : kLcdFrame);
            g.fillRoundedRectangle (box, 3.0f);
            const auto ink = focused ? kLcd : kPixel;
            g.setColour (focused ? kLcd : kPixelDim);
            g.setFont (type::font (type::kCaption, true));
            g.drawText (f.label, box.withHeight (box.getHeight() * 0.42f), juce::Justification::centred);
            const int v = f.pending >= 0 ? f.pending : f.get ? f.get() : 0;
            g.setColour (ink);
            g.setFont (type::font (type::kLabel, true));
            g.drawText (f.text ? f.text (v) : juce::String (v), box.withTrimmedTop (box.getHeight() * 0.4f), juce::Justification::centred);
        }
        if (confirmGo != nullptr)
        {
            // The confirmation dialog over the page: its question, CANCEL and EXECUTE.
            auto box = b.withSizeKeepingCentre (b.getWidth() * 0.6f, 120.0f);
            g.setColour (kLcd.withAlpha (1.0f).brighter (0.08f)); // opaque: the page under it hidden
            g.fillRect (box);
            g.setColour (kPixel);
            g.drawRect (box, 2.0f);
            g.setFont (type::font (type::kLabel, true));
            g.drawText (confirmText, box.removeFromTop (60.0f).reduced (10.0f, 0.0f), juce::Justification::centred);
            auto row = box.reduced (20.0f, 14.0f);
            for (int k = 0; k < 2; ++k)
            {
                const auto key = (k == 0 ? row.removeFromLeft (row.getWidth() * 0.5f) : row).reduced (10.0f, 0.0f);
                g.setColour (k == confirmChoice ? kPixel : kLcdFrame);
                g.fillRoundedRectangle (key, 3.0f);
                g.setColour (k == confirmChoice ? kLcd : kPixel);
                g.drawText (k == 0 ? "CANCEL" : "EXECUTE", key, juce::Justification::centred);
            }
        }
    }

    void Display::paintPrograms (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        // The CHANNEL LIBRARY's scrolling list (V2 MAN p.48): the programs around
        // the one RECALL, STORE or CLEAR has chosen (RECALL's past 64 the UNDO).
        const auto& f = fields[(size_t) juce::jlimit (0, 2, focus)];
        const int chosen = f.pending >= 0 ? f.pending : f.get ? f.get() : 0;
        constexpr int kLines = 7;
        const float h = area.getHeight() / (float) kLines;
        const int first = juce::jlimit (0, juce::jmax (0, f.hi - kLines + 1), chosen - kLines / 2);
        g.setFont (type::font (type::kLabel, true));
        for (int k = 0; k < kLines && first + k <= f.hi; ++k)
        {
            const int n = first + k;
            const auto line = juce::Rectangle<float> (area.getX(), area.getY() + h * (float) k, area.getWidth(), h - 2.0f);
            if (n == chosen)
            {
                g.setColour (kPixel);
                g.fillRect (line);
            }
            g.setColour (n == chosen ? kLcd : kPixel);
            g.drawText (f.text ? f.text (n) : juce::String (n + 1), line.reduced (10.0f, 0.0f), juce::Justification::centredLeft);
        }
    }

    void Display::paintMeters (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        // METER (MAN pp.41-43): 1/3 the inputs (MIC/LINE), 2/3 the returns
        // (TAPE, EFF), 3/3 the outputs (BUS, AUX, STEREO).
        const int first = page == 0 ? 0 : page == 1 ? kTape : kBus, last = page == 0 ? kTape : page == 1 ? kBus : kStrips;
        const int n = last - first;
        const float w = area.getWidth() / (float) n;
        for (int s = first; s < last; ++s)
        {
            Strip* st = nullptr;
            auto& e = ctx.engine;
            auto pick = [] (const MixerEngine::StripList& l, int k) -> Strip* { return juce::isPositiveAndBelow (k, (int) l.size()) ? l[(size_t) k].get() : nullptr; };
            if (s < kEff)
                st = pick (e.getChannels(), s);
            else if (s < kBus)
                st = pick (e.getAux(), s - kEff);
            else if (s < kStereo)
                st = pick (e.getBuses(), s - kBus);
            else
                st = pick (e.getMains(), 0);
            const auto point = meterPoint == 0 ? (s < kBus ? MeterPoint::Trim : MeterPoint::PreFader) : meterPoint == 1 ? MeterPoint::PreFader : MeterPoint::Output;
            const float level = st != nullptr ? juce::Decibels::gainToDecibels (ctx.meters.get (st, point, 0), -60.0f) : -60.0f;
            auto bar = juce::Rectangle<float> (area.getX() + w * (float) (s - first) + 1.0f, area.getY(), juce::jmax (1.0f, w - 6.0f), area.getHeight() - 14.0f);
            g.setColour (kLcdFrame);
            g.fillRect (bar);
            g.setColour (kPixel);
            g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * (level + 60.0f) / 60.0f));
            // Each bar's number under it.
            g.setFont (type::font (9.0f));
            const juce::String name = s < 16 ? juce::String (s + 1) : s < kTape ? juce::String (17 + 2 * (s - 16)) : s < kEff ? juce::String (s - kTape + 1)
                                    : s < kBus ? "E" + juce::String (s - kEff + 1) : s < kAux ? "B" + juce::String (s - kBus + 1)
                                    : s < kStereo ? "A" + juce::String (s - kAux + 1) : juce::String ("ST");
            g.drawText (name, juce::Rectangle<float> (bar.getX() - 4.0f, bar.getBottom() + 1.0f, bar.getWidth() + 8.0f, 12.0f), juce::Justification::centred);
        }
    }

    void Display::paintEq (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        // The curve of the strip's four bands (bells and shelves as the DSP's;
        // a filter as a 12 dB/oct slope).
        g.setColour (kLcdFrame);
        g.drawRect (area);
        const int eqModel = models::findModel (models::ChannelEq, "Y2EQ");
        if (eqModel < 0)
            return;
        const auto& m = models::model (models::ChannelEq, eqModel);
        std::vector<float> values;
        for (int k = 0; k < (int) m.params.size(); ++k)
        {
            const auto& p = m.params[(size_t) k];
            const int a = k < 12 ? address (strip, kEq + k) : address (strip, kSwitches);
            const int raw = y.get (a);
            values.push_back (k < 12 ? stepValue (p, juce::jmax (0, raw)) : (float) ((raw >> (k == 12 ? kHpf : kLpf)) & 1));
        }
        constexpr int n = 96;
        float freqs[n], db[n];
        for (int i = 0; i < n; ++i)
            freqs[i] = 20.0f * std::pow (1000.0f, (float) i / (float) (n - 1));
        if (! models::modelResponse (models::ChannelEq, eqModel, values.data(), (int) values.size(), freqs, db, n))
            return;
        juce::Path p;
        for (int i = 0; i < n; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (n - 1);
            const float yy = area.getCentreY() - juce::jlimit (-18.0f, 18.0f, db[i]) / 18.0f * area.getHeight() * 0.45f;
            i == 0 ? p.startNewSubPath (x, yy) : p.lineTo (x, yy);
        }
        g.setColour (kPixelDim);
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
        g.setColour (kPixel);
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }

    void Display::mouseDown (const juce::MouseEvent& e)
    {
        for (int i = 0; i < (int) fields.size(); ++i)
            if (fieldBox (i).contains (e.position))
            {
                focus = i;
                if (e.getNumberOfClicks() > 1)
                    enter();
                repaint();
                break;
            }
        dragY = e.position.y;
    }

    void Display::mouseDrag (const juce::MouseEvent& e)
    {
        const float d = dragY - e.position.y;
        if (std::abs (d) >= 6.0f)
        {
            turn ((int) (d / 6.0f));
            dragY = e.position.y;
        }
    }

    void Display::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) { turn (w.deltaY > 0 ? 1 : -1); }
} // namespace y02r

//==============================================================================
Y02RSurface::Y02RSurface (UiContext& c)
    : ctx (c), theme (Theme::dark()), y (c.engine.getYamaha02R()), universalKey ("UNIVERSAL MIXER", theme.accent, theme),
      flipKey ("FLIP", kSelLed, theme), display (c, theme)
{
    using y02r::Function;
    const auto onColour = juce::Colour (0xff40c060);
    for (int i = 0; i < kNumFaders; ++i)
    {
        faders[(size_t) i] = std::make_unique<y02r::Fader> (ctx, theme);
        addAndMakeVisible (*faders[(size_t) i]);
        auto& sel = selKeys[(size_t) i];
        sel = std::make_unique<ToggleKey> ("SEL", kSelLed, theme);
        sel->onClick = [this, i] { pressSel (faderStrip (i)); };
        sel->isOn = [this, i] { return display.getStrip() == faderStrip (i); };
        faders[(size_t) i]->onTouch = [this, i]
        {
            // TOUCH SENSE SELECT; in an AUX function fader 21 is the aux master, not STEREO.
            if (y02r::pref (ctx.settings, y02r::TouchSelect) && (auxShown() < 0 || i < kNumFaders - 1))
                pressSel (faderStrip (i));
        };
        addAndMakeVisible (*sel);
        auto& on = onKeys[(size_t) i];
        on = std::make_unique<ToggleKey> ("ON", onColour, theme);
        on->onClick = [this, i] { y.set (y02r::address (faderStrip (i), kOn), y.get (y02r::address (faderStrip (i), kOn)) == 1 ? 0 : 1); };
        on->isOn = [this, i] { return y.get (y02r::address (faderStrip (i), kOn)) == 1; };
        addAndMakeVisible (*on);
    }
    for (int i = 0; i < kNumEncoders; ++i)
    {
        auto& enc = encoders[(size_t) i];
        enc = std::make_unique<y02r::Encoder> ("", theme);
        enc->get = [this, i]
        {
            const int s = encoderStrip (i), aux = auxShown();
            return juce::jmax (0, aux >= 0 && memberOf (s) >= 0 ? y.get (y02r::sendAddress (memberOf (s), aux)) : y.get (y02r::address (s, kLevel)));
        };
        enc->set = [this, i] (int v)
        {
            const int s = encoderStrip (i), aux = auxShown();
            y.set (aux >= 0 && memberOf (s) >= 0 ? y02r::sendAddress (memberOf (s), aux) : y02r::address (s, kLevel), v);
        };
        enc->text = [] (int v) { return juce::String (v); };
        enc->endless = enc->nameBelow = true;
        enc->onTouch = [this, i]
        {
            if (y02r::pref (ctx.settings, y02r::TouchSelect))
                pressSel (encoderStrip (i));
        };
        addAndMakeVisible (*enc);
        auto& sel = encSelKeys[(size_t) i];
        sel = std::make_unique<ToggleKey> ("SEL", kSelLed, theme);
        sel->onClick = [this, i] { pressSel (encoderStrip (i)); };
        sel->isOn = [this, i] { return display.getStrip() == encoderStrip (i); };
        addAndMakeVisible (*sel);
        auto& on = encOnKeys[(size_t) i];
        on = std::make_unique<ToggleKey> ("ON", onColour, theme);
        on->onClick = [this, i] { y.set (y02r::address (encoderStrip (i), kOn), y.get (y02r::address (encoderStrip (i), kOn)) == 1 ? 0 : 1); };
        on->isOn = [this, i] { return y.get (y02r::address (encoderStrip (i), kOn)) == 1; };
        addAndMakeVisible (*on);
    }
    flipKey.onClick = [this] { setFlip (! flip); };
    flipKey.isOn = [this] { return flip; };
    addAndMakeVisible (flipKey);

    const std::pair<const char*, Function> keys[] = {
        { "SCENE MEMORY", Function::Scene }, { "UTILITY", Function::Utility }, { "MIDI", Function::Midi },     { "GROUP", Function::Group },
        { "PAIR", Function::Pair },          { "METER", Function::Meter },     { "VIEW", Function::View },     { "ROUTING", Function::Routing },
        { "\xc3\x98 / ATT", Function::Att }, { "DELAY", Function::Delay },     { "PAN", Function::Pan },       { "EQ", Function::Eq },
        { "DYNAMICS", Function::Dynamics },  { "AUX 1", Function::Aux1 },      { "AUX 2", Function::Aux2 },    { "AUX 3", Function::Aux3 },
        { "AUX 4", Function::Aux4 },         { "AUX 5", Function::Aux5 },      { "AUX 6", Function::Aux6 },    { "AUX 7", Function::Aux7 },
        { "AUX 8", Function::Aux8 },
    };
    for (auto [name, f] : keys)
    {
        auto key = std::make_unique<ToggleKey> (utf8 (name), kSelLed, theme);
        key->onClick = [this, fn = f] { pressFunction (fn); };
        key->isOn = [this, fn = f] { return display.getFunction() == fn; };
        addAndMakeVisible (*key);
        functionKeys.emplace_back (f, std::move (key));
    }
    const std::pair<juce::String, std::function<void()>> panel[] = {
        { "STORE", [this] { store(); } },
        { "RECALL", [this] { recall(); } },
        { utf8 ("\xe2\x96\xb2"), [this] { setMemory (memory + 1); } },
        { utf8 ("\xe2\x96\xbc"), [this] { setMemory (memory - 1); } },
        { utf8 ("\xe2\x97\x80"), [this] { display.cursor (-1); } },
        { utf8 ("\xe2\x96\xb2"), [this] { display.cursor (-4); } },
        { utf8 ("\xe2\x96\xbc"), [this] { display.cursor (4); } },
        { utf8 ("\xe2\x96\xb6"), [this] { display.cursor (1); } },
        { "ENTER", [this] { display.enter(); } },
    };
    for (auto& [name, action] : panel)
    {
        auto key = std::make_unique<ToggleKey> (name, kSelLed, theme);
        key->onClick = action;
        addAndMakeVisible (*key);
        panelKeys.push_back (std::move (key));
    }

    // SELECTED CHANNEL: ROUTING 1-8, ST, DIRECT.
    for (int k = 0; k < 10; ++k)
    {
        auto key = std::make_unique<ToggleKey> (k < 8 ? juce::String (k + 1) : k == 8 ? juce::String ("ST") : juce::String ("DIRECT"), kSelLed, theme);
        key->onClick = [this, k]
        {
            const int s = display.getStrip();
            if (memberOf (s) < 0)
                return;
            autoDisplay (y02r::AutoRouting, y02r::Function::Routing);
            if (k == 9)
            {
                y.setState (Yamaha02R::Direct, s, y.getState (Yamaha02R::Direct, s) == 1 ? 0 : 1);
                return;
            }
            const int a = y02r::address (s, k < 8 ? kRouting : kSwitches), bit = k < 8 ? k : kSt;
            y.set (a, juce::jmax (0, y.get (a)) ^ (1 << bit));
        };
        key->isOn = [this, k]
        {
            const int s = display.getStrip();
            if (memberOf (s) < 0)
                return false;
            if (k == 9)
                return y.getState (Yamaha02R::Direct, s) == 1;
            return ((y.get (y02r::address (s, k < 8 ? kRouting : kSwitches)) >> (k < 8 ? k : kSt)) & 1) != 0;
        };
        addAndMakeVisible (*key);
        routingKeys.push_back (std::move (key));
    }
    // AUX 1-8 (the send the SEND LEVEL encoder works) and its ON.
    for (int k = 0; k < 9; ++k)
    {
        auto key = std::make_unique<ToggleKey> (k < 8 ? "AUX " + juce::String (k + 1) : juce::String ("ON"), k < 8 ? kSelLed : onColour, theme);
        if (k < 8)
        {
            key->onClick = [this, k] { selAux = k; };
            key->isOn = [this, k] { return selAux == k; };
        }
        else
        {
            key->onClick = [this]
            {
                const int m = memberOf (display.getStrip());
                if (m >= 0)
                    y.set (y02r::sendAddress (m, 8), juce::jmax (0, y.get (y02r::sendAddress (m, 8))) ^ (1 << selAux));
            };
            key->isOn = [this]
            {
                const int m = memberOf (display.getStrip());
                return m >= 0 && ((y.get (y02r::sendAddress (m, 8)) >> selAux) & 1) != 0;
            };
        }
        addAndMakeVisible (*key);
        auxKeys.push_back (std::move (key));
    }
    sendLevel = std::make_unique<y02r::Encoder> ("SEND LEVEL", theme);
    sendLevel->get = [this]
    {
        const int m = memberOf (display.getStrip());
        return m >= 0 ? juce::jmax (0, y.get (y02r::sendAddress (m, selAux))) : 0;
    };
    sendLevel->set = [this] (int v)
    {
        if (const int m = memberOf (display.getStrip()); m >= 0)
            y.set (y02r::sendAddress (m, selAux), v);
    };
    addAndMakeVisible (*sendLevel);
    pan = std::make_unique<y02r::Encoder> ("PAN", theme);
    pan->hi = 32;
    pan->centred = true;
    pan->get = [this] { return juce::jmax (0, y.get (y02r::address (display.getStrip(), panSide == 0 ? kPanL : kPanR))); };
    pan->set = [this] (int v) { y.set (y02r::address (display.getStrip(), panSide == 0 ? kPanL : kPanR), v); };
    pan->onTouch = [this] { autoDisplay (y02r::AutoPan, y02r::Function::Pan); };
    for (int k = 0; k < 2; ++k)
    {
        auto key = std::make_unique<ToggleKey> (k == 0 ? "L/ODD" : "R/EVEN", kSelLed, theme);
        key->onClick = [this, k]
        {
            panSide = k;
            autoDisplay (y02r::AutoPan, y02r::Function::Pan);
        };
        key->isOn = [this, k] { return panSide == k; };
        addAndMakeVisible (*key);
        panKeys.push_back (std::move (key));
    }
    pan->text = panText;
    addAndMakeVisible (*pan);
    // EQUALIZER: the band keys, Q, F and G of the band, EQ ON.
    const char* bands[] = { "LOW/\nHPF", "L-MID", "H-MID", "HIGH/\nLPF", "EQ ON" };
    for (int k = 0; k < 5; ++k)
    {
        auto key = std::make_unique<ToggleKey> (bands[k], k < 4 ? kSelLed : onColour, theme);
        if (k < 4)
        {
            key->onClick = [this, k]
            {
                display.band = k;
                autoDisplay (y02r::AutoEq, y02r::Function::Eq);
            };
            key->isOn = [this, k] { return display.band == k; };
        }
        else
        {
            key->onClick = [this]
            {
                const int a = y02r::address (display.getStrip(), kSwitches);
                y.set (a, juce::jmax (0, y.get (a)) ^ (1 << kEqOn));
                autoDisplay (y02r::AutoEq, y02r::Function::Eq);
            };
            key->isOn = [this] { return ((y.get (y02r::address (display.getStrip(), kSwitches)) >> kEqOn) & 1) != 0; };
        }
        addAndMakeVisible (*key);
        eqKeys.push_back (std::move (key));
    }
    auto eqEncoder = [this] (const char* label, int what)
    {
        auto e = std::make_unique<y02r::Encoder> (label, theme);
        e->get = [this, what] { return juce::jmax (0, y.get (y02r::address (display.getStrip(), kEq + 3 * display.band + what))); };
        e->set = [this, what] (int v) { y.set (y02r::address (display.getStrip(), kEq + 3 * display.band + what), v); };
        e->hi = what == 0 ? 119 : what == 1 ? 72 : (display.band == 1 || display.band == 2 ? 40 : 42);
        e->text = [this, what] (int v)
        {
            const int m = models::findModel (models::ChannelEq, "Y2EQ");
            if (what == 0)
                return freqText (v);
            if (m < 0)
                return juce::String (v);
            const auto& p = models::model (models::ChannelEq, m).params[(size_t) (3 * display.band + what)];
            return p.display (stepValue (p, v));
        };
        e->cap = juce::Colour (0xff5f9a62);
        e->onTouch = [this] { autoDisplay (y02r::AutoEq, y02r::Function::Eq); };
        addAndMakeVisible (*e);
        return e;
    };
    eqQ = eqEncoder ("Q", 2);
    eqF = eqEncoder ("F", 0);
    eqG = eqEncoder ("G", 1);

    // The wheel: an endless rotary; each detent a step.
    wheel = std::make_unique<juce::Slider> (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox);
    wheel->setRange (-1.0e6, 1.0e6, 1.0);
    wheel->setRotaryParameters (0.0f, juce::MathConstants<float>::twoPi * 40.0f, false);
    wheel->setMouseDragSensitivity (240);
    // Drawn by the surface (paint): the slider itself is only its touch.
    for (auto id : { juce::Slider::rotarySliderFillColourId, juce::Slider::rotarySliderOutlineColourId, juce::Slider::thumbColourId })
        wheel->setColour (id, juce::Colours::transparentBlack);
    wheel->onValueChange = [this]
    {
        const double v = wheel->getValue();
        const int d = (int) (v - wheelLast);
        if (d != 0)
        {
            display.turn (d);
            wheelLast = v;
        }
        repaintDesign (wheel->getBounds());
    };
    wheel->setTitle ("WHEEL");
    addAndMakeVisible (*wheel);

    // DISPLAY ACCESS's keys Sidecar has no page for.
    // DIGITAL I/O, SETUP and AUTOMIX: their pages.
    const std::pair<const char*, Function> config[] = { { "DIGITAL I/O", Function::DigitalIo }, { "SETUP", Function::Setup }, { "AUTOMIX", Function::Automix } };
    for (auto [name, f] : config)
    {
        auto key = std::make_unique<ToggleKey> (name, kSelLed, theme);
        key->onClick = [this, fn = f] { pressFunction (fn); };
        key->isOn = [this, fn = f] { return display.getFunction() == fn; };
        addAndMakeVisible (*key);
        darkKeys.push_back (std::move (key));
    }
    // The monitor column (MAN p.8): the sources dark (Sidecar's monitor follows
    // the solo bus and the main), MONO and DIM the monitor's, SLATE and T/B the
    // talkback paths, SOLO lit while anything is soloed (a press clears them),
    // C-R LEVEL the monitor level.
    {
        auto& e = ctx.engine;
        const char* names[] = { "C-R", "ST / AUX 5", "AUX 6", "2TR-D1", "2TR-A1", "2TR-D2", "2TR-A2", "2TR-D3", "ST / AUX 5", "AUX 6",
                                "MONO", "DIM", "SLATE", "T/B", "SOLO" };
        for (int k = 0; k < 15; ++k)
        {
            auto key = std::make_unique<ToggleKey> (names[k], kSelLed, theme);
            if (k < 10)
            {
                key->setEnabled (false);
                key->setTooltip ("Sidecar's monitor follows the solo bus and the main");
            }
            else if (k < 14)
            {
                auto& flag = k == 10 ? e.monitorMono : k == 11 ? e.monitorDim : e.talk.paths[(size_t) (k == 12 ? 1 : 0)].on;
                key->onClick = [&flag] { flag = ! flag.load(); };
                key->isOn = [&flag] { return flag.load(); };
            }
            else
            {
                key->isOn = [&e] { return e.anySoloActive.load(); };
                key->onClick = [this, &e]
                {
                    for (auto* list : { &e.getChannels(), &e.getAux(), &e.getBuses(), &e.getMains(), &e.getMatrices() })
                        for (auto& s : *list)
                            s->params.solo = false;
                    if (y02r::pref (ctx.settings, y02r::AutoSolo))
                        showFunction (y02r::Function::Setup, 0);
                };
            }
            addAndMakeVisible (*key);
            monitorKeys.push_back (std::move (key));
        }
        crLevel.setBinding (bindFader ("C-R LEVEL", e.monitorLevelDb));
        crLevel.setHardware (true);
        crLevel.getSlider().setLookAndFeel (&hw::knobLookAndFeel (look(), false));
        addAndMakeVisible (crLevel);
    }

    // The user programs: the console's memory, kept in the app settings.
    y.setLibraryText (ctx.settings.getValue (libraryKey));
    display.libraryChanged = [this] { ctx.settings.setValue (libraryKey, y.getLibraryText()); };
    display.memoryText = [this]
    {
        const int current = ctx.show.getCurrentSceneIndex();
        auto scene = current >= 0 ? ctx.show.getScene (current) : std::nullopt;
        return current >= 0 && scene.has_value() ? juce::String (current + 1).paddedLeft ('0', 2) + " " + scene->name : juce::String ("00 Initial Data");
    };
    addAndMakeVisible (display);

    universalKey.onClick = [&context = ctx]
    {
        // Switching rebuilds the window's skin: let the click return first.
        juce::MessageManager::callAsync ([&context]
        {
            if (context.setInterface)
                context.setInterface (InterfaceKind::Universal);
        });
    };
    universalKey.setTooltip ("The Universal Mixer");
    addAndMakeVisible (universalKey);

    // The panel's keys are the 02R's: cream rubber with an LED (SEL orange, ON
    // green); the function keys orange (MIXING, AUX) and light grey (CONFIGURATION).
    for (auto* child : getChildren())
        if (auto* k = dynamic_cast<ToggleKey*> (child); k != nullptr && k != &universalKey)
            k->look = &look();
    for (auto& [f, key] : functionKeys)
    {
        key->face = f <= y02r::Function::Pair ? juce::Colour (0xffc4c6c7) : juce::Colour (0xffd9762c);
        // DISPLAY ACCESS: blank keys without LEDs, their names under them.
        key->labelBelow = true;
        key->showLed = false;
        key->belowText = f == y02r::Function::Aux7 ? "AUX 7 EFF1" : f == y02r::Function::Aux8 ? "AUX 8 EFF2" : key->getButtonText();
    }
    for (auto& key : darkKeys)
    {
        key->face = juce::Colour (0xffc4c6c7);
        key->labelBelow = true;
        key->showLed = false;
    }
    // FLIP and SOLO: their names reversed out of a dark box, under them.
    for (auto* key : { &flipKey, monitorKeys.back().get() })
    {
        key->labelBelow = true;
        key->belowText = key->getButtonText();
    }
    for (auto& k : panelKeys)
        k->showLed = false;
    auxKeys[6]->setButtonText ("AUX 7 EFF1");
    auxKeys[7]->setButtonText ("AUX 8 EFF2");
    // SELECTED CHANNEL's encoders: endless rings, their values on the readouts and LEDs.
    for (auto* e : { sendLevel.get(), pan.get(), eqQ.get(), eqF.get(), eqG.get() })
    {
        e->endless = true;
        e->showValue = false;
        if (e != sendLevel.get())
            e->setLabel ({});
    }
    using Shape = ToggleKey::Shape;
    panelKeys[4]->shape = Shape::Left;
    panelKeys[5]->shape = Shape::Up;
    panelKeys[6]->shape = Shape::Down;
    panelKeys[7]->shape = Shape::Right;
    for (int k : { 0, 1, 8 })
        panelKeys[(size_t) k]->labelBelow = true;
    refreshAll();
    startTimerHz (30);
}

Y02RSurface::~Y02RSurface()
{
    stopTimer();
}

int Y02RSurface::auxShown() const
{
    const int f = (int) display.getFunction(), a = (int) y02r::Function::Aux1;
    return f >= a && f <= (int) y02r::Function::Aux8 ? f - a : -1;
}

int Y02RSurface::faderStrip (int i) const
{
    if (i < 16)
        return flip ? y02r::kTape + i : i;
    if (i < 20)
        return i; // LINE 17/18-23/24
    return auxShown() >= 0 ? y02r::kAux + auxShown() : y02r::kStereo;
}

int Y02RSurface::encoderStrip (int i) const
{
    if (i < 16)
        return flip ? i : y02r::kTape + i;
    return y02r::kEff + (i - 16);
}

Strip* Y02RSurface::engineStrip (int s) const
{
    auto& e = ctx.engine;
    auto pick = [] (const MixerEngine::StripList& l, int k) -> Strip* { return juce::isPositiveAndBelow (k, (int) l.size()) ? l[(size_t) k].get() : nullptr; };
    if (s < y02r::kEff)
        return pick (e.getChannels(), s);
    if (s < y02r::kBus)
        return pick (e.getAux(), s - y02r::kEff);
    if (s < y02r::kStereo)
        return pick (e.getBuses(), s - y02r::kBus);
    return pick (e.getMains(), 0);
}

void Y02RSurface::bind()
{
    const int aux = auxShown();
    for (int i = 0; i < kNumFaders; ++i)
    {
        const int s = faderStrip (i);
        // In an AUX function the faders are the inputs' sends to it (MAN p.85).
        const int a = aux >= 0 && i < 20 ? y02r::sendAddress (memberOf (s), aux) : y02r::address (s, kLevel);
        faders[(size_t) i]->bind (a, s == y02r::kStereo);
    }
    for (int i = 0; i < kNumEncoders; ++i)
        encoders[(size_t) i]->setLabel (i < 16 ? "TAPE\n" + juce::String (i + 1) : "EFF" + juce::String (i - 15) + "\nRTN"); // as printed
    repaint();
}

void Y02RSurface::pressFunction (y02r::Function f)
{
    display.setFunction (f);
    bind();
}

void Y02RSurface::pressSel (int s)
{
    display.setStrip (s);
    if (auto* st = engineStrip (s); st != nullptr && ctx.selectStrip)
        ctx.selectStrip (st);
}

void Y02RSurface::setFlip (bool f)
{
    flip = f;
    bind();
}

void Y02RSurface::store()
{
    // The show's scene `memory` (scene 0 is read-only); a new one appended when
    // the show has fewer.
    if (memory < 1)
        return;
    display.confirm (true, "STORE SCENE " + juce::String (memory).paddedLeft ('0', 2) + "?", [this, index = memory - 1]
    {
        if (index < ctx.show.getNumScenes())
            ctx.show.storeScene (index);
        else
            ctx.show.addScene ("SCENE " + juce::String (index + 1).paddedLeft ('0', 2));
        display.refresh();
    });
}

void Y02RSurface::recall()
{
    if (memory > 0 && memory - 1 >= ctx.show.getNumScenes())
        return;
    display.confirm (false, "RECALL SCENE " + juce::String (memory).paddedLeft ('0', 2) + "?", [this, m = memory]
    {
        if (m == 0)
        {
            y.recallInitial();
            // INITIAL DATA NOMINAL: the channel faders at 0 dB, not -oo (V2 MAN p.200).
            if (y02r::pref (ctx.settings, y02r::InitialNominal))
                for (int s = 0; s < y02r::kBus; ++s)
                    y.set (y02r::address (s, kLevel), kNominal);
        }
        else
            ctx.show.recallScene (m - 1);
        display.refresh();
    });
}

void Y02RSurface::autoDisplay (y02r::Pref p, y02r::Function f)
{
    if (y02r::pref (ctx.settings, p) && display.getFunction() != f && display.getFunction() != y02r::Function::TitleEdit)
        showFunction (f, 0);
}

void Y02RSurface::showFunction (y02r::Function f, int page)
{
    display.show (f, page);
    bind();
}

void Y02RSurface::refreshAll()
{
    bind();
    selectionChanged();
    timerCallback();
}

void Y02RSurface::selectionChanged()
{
    if (! ctx.getSelectedStrip)
        return;
    auto* s = ctx.getSelectedStrip();
    if (s == nullptr)
        return;
    const int n = s->getNumber() - 1;
    switch (s->getKind())
    {
        case StripKind::Channel: display.setStrip (juce::jlimit (0, y02r::kEff - 1, n)); break;
        case StripKind::Aux: display.setStrip (y02r::kEff + juce::jlimit (0, 1, n)); break;
        case StripKind::Bus: display.setStrip (y02r::kBus + juce::jlimit (0, 15, n)); break;
        case StripKind::Main: display.setStrip (y02r::kStereo); break;
        default: break;
    }
}

void Y02RSurface::timerCallback()
{
    for (int i = 0; i < kNumFaders; ++i)
    {
        faders[(size_t) i]->refresh();
        selKeys[(size_t) i]->refresh();
        onKeys[(size_t) i]->refresh();
    }
    for (int i = 0; i < kNumEncoders; ++i)
    {
        encoders[(size_t) i]->refresh();
        encSelKeys[(size_t) i]->refresh();
        encOnKeys[(size_t) i]->refresh();
    }
    for (auto& [f, key] : functionKeys)
        key->refresh();
    for (auto* list : { &routingKeys, &auxKeys, &eqKeys, &panKeys, &monitorKeys })
        for (auto& k : *list)
            k->refresh();
    flipKey.refresh();
    for (auto* e : { sendLevel.get(), pan.get(), eqQ.get(), eqF.get(), eqG.get() })
        e->refresh();
    eqQ->hi = display.band == 1 || display.band == 2 ? 40 : 42;
    display.refresh();
    repaintDesign (meterArea());
    repaintDesign (memoryArea());
}

juce::Rectangle<int> Y02RSurface::meterArea() const
{
    return meterRect;
}

juce::Rectangle<int> Y02RSurface::memoryArea() const
{
    return readoutArea;
}

void Y02RSurface::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    g.addTransform (design);
    // The 02R's panel between its wooden cheeks, its sections outlined in silkscreen.
    auto body = designBounds().withTrimmedTop (kTopBar).toFloat();
    hw::cheeks (g, body.removeFromLeft (14.0f), body.removeFromRight (14.0f));
    hw::panel (g, body, look());
    auto top = designBounds().removeFromTop (kTopBar);
    g.setColour (theme.panel);
    g.fillRect (top);
    g.setColour (theme.text);
    g.setFont (type::font (type::kTitle, true));
    g.drawText ("02R", top.reduced (sizes::kGapLarge, 0), juce::Justification::centredLeft);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption));
    g.drawText ("Not affiliated with or endorsed by Yamaha; 02R is Yamaha's trademark, used only to say what is emulated.",
                top.reduced (sizes::kGapLarge, 0).withTrimmedLeft (90).withTrimmedRight (200), juce::Justification::centredLeft);

    const auto red = juce::Colour (0xffff4a36), green = juce::Colour (0xff40d070), amber = juce::Colour (0xfff0c030);
    // A row of LEDs (lit: `on` of each), round or square.
    const auto led = [&g] (juce::Rectangle<float> r, juce::Colour c, bool on)
    {
        if (on)
        {
            g.setColour (c.withAlpha (0.35f));
            g.fillEllipse (r.expanded (2.0f));
        }
        g.setColour (on ? c.brighter (0.2f) : c.withAlpha (0.16f));
        g.fillEllipse (r);
    };
    // A red seven-segment readout in its dark window.
    const auto readout = [&g, red] (juce::Rectangle<float> r, const juce::String& text, float size)
    {
        g.setColour (juce::Colour (0xff1a0c0c));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (red.withAlpha (0.1f));
        g.setFont (type::font (size, true));
        g.drawText ("888", r.reduced (4.0f, 0.0f), juce::Justification::centredRight, false);
        g.setColour (red);
        g.drawText (text, r.reduced (4.0f, 0.0f), juce::Justification::centredRight, false);
    };
    const auto knobAt = [&g] (juce::Point<float> c, float size)
    {
        hw::knob (g, juce::Rectangle<float> (size, size).withCentre (c), 0.5f, look(), hw::Look::Knob::Plain, juce::Colour (0xff1c1d1f));
    };

    // The analog input row along the top (MAN p.3): on MIC/LINE 1-8 the +48V
    // and A/B switches, on 1-16 the 20dB pad, then GAIN, PEAK and SIGNAL, the
    // number; LINE 17/18-23/24 one GAIN each. Drawn, not Sidecar's controls.
    if (! analogArea.isEmpty())
    {
        const auto row = analogArea.toFloat();
        const auto slide = [&g] (juce::Rectangle<float> r, bool right)
        {
            g.setColour (juce::Colour (0xff101112));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (juce::Colour (0xffc9cbcc));
            g.fillRoundedRectangle (r.withWidth (r.getWidth() * 0.45f).withX (right ? r.getCentreX() : r.getX()).reduced (1.0f), 1.5f);
        };
        auto legend = juce::Rectangle<float> (row.getX() - 46.0f, row.getY(), 40.0f, row.getHeight());
        hw::silk (g, legend.withY (row.getY() + 4.0f).withHeight (10.0f), "+48V  A/B", look(), 7.0f, juce::Justification::centredRight, true);
        hw::silk (g, legend.withY (row.getY() + 17.0f).withHeight (10.0f), "20dB", look(), 7.5f, juce::Justification::centredRight, true);
        hw::silk (g, legend.withY (row.getY() + 36.0f).withHeight (10.0f), "GAIN", look(), 8.0f, juce::Justification::centredRight, true);
        for (int i = 0; i < 20; ++i)
        {
            const float cx = (float) faders[(size_t) i]->getBounds().getCentreX() + 12.0f;
            if (i < 8)
            {
                slide (juce::Rectangle<float> (14.0f, 7.0f).withCentre ({ cx - 9.0f, row.getY() + 9.0f }), false);
                slide (juce::Rectangle<float> (14.0f, 7.0f).withCentre ({ cx + 9.0f, row.getY() + 9.0f }), false);
            }
            if (i < 16)
            {
                const auto pad = juce::Rectangle<float> (18.0f, 8.0f).withCentre ({ cx, row.getY() + 22.0f });
                g.setColour (juce::Colour (0xffd0c9b2));
                g.fillRoundedRectangle (pad, 1.5f);
            }
            knobAt ({ cx, row.getY() + 42.0f }, 22.0f);
            led (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ cx + 19.0f, row.getY() + 36.0f }), red, false);
            led (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ cx + 19.0f, row.getY() + 47.0f }), green, false);
            const juce::String n = i < 16 ? juce::String (i + 1) : juce::String (17 + 2 * (i - 16)) + "/" + juce::String (18 + 2 * (i - 16));
            hw::silk (g, juce::Rectangle<float> (50.0f, 11.0f).withCentre ({ cx, row.getY() + 64.0f }), n, look(), 9.5f);
        }
        hw::silk (g, juce::Rectangle<float> (40.0f, 9.0f).withCentre ({ (float) faders[0]->getBounds().getCentreX() + 31.0f, row.getY() + 30.0f }), "PEAK",
                  look(), 6.5f, juce::Justification::centred, true);
        // The analog outputs at the right (MAN p.4): T/B LEVEL over the talkback
        // mic, STUDIO LEVEL, PHONES LEVEL and the phones jack.
        const auto outs = juce::Rectangle<float> ((float) monitorArea.getX() - 250.0f, row.getY(), 250.0f + (float) monitorArea.getWidth(), row.getHeight());
        const char* names[] = { "T/B LEVEL", "STUDIO LEVEL", "PHONES LEVEL" };
        for (int k = 0; k < 3; ++k)
        {
            const juce::Point<float> c (outs.getX() + 40.0f + 82.0f * (float) k, row.getY() + 30.0f);
            knobAt (c, 30.0f);
            hw::silk (g, juce::Rectangle<float> (80.0f, 10.0f).withCentre (c.translated (0.0f, 26.0f)), names[k], look(), 8.0f);
            hw::silk (g, juce::Rectangle<float> (14.0f, 9.0f).withCentre (c.translated (-16.0f, 14.0f)), "0", look(), 7.0f, juce::Justification::centred, true);
            hw::silk (g, juce::Rectangle<float> (14.0f, 9.0f).withCentre (c.translated (17.0f, 14.0f)), "10", look(), 7.0f, juce::Justification::centred, true);
        }
        {
            // The talkback mic's grille, beside T/B LEVEL; the phones jack.
            const juce::Point<float> mic (outs.getX() + 8.0f, row.getY() + 66.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            for (int k = 0; k < 9; ++k)
                g.fillEllipse (juce::Rectangle<float> (2.5f, 2.5f).withCentre (mic.translated ((float) (k % 3) * 4.0f, (float) (k / 3) * 4.0f)));
            const juce::Point<float> jack (outs.getX() + 40.0f + 82.0f * 3.0f, row.getY() + 30.0f);
            g.setColour (juce::Colour (0xffb9bcbe));
            g.fillEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (jack));
            g.setColour (juce::Colour (0xff0e0f10));
            g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (jack));
            hw::silk (g, juce::Rectangle<float> (60.0f, 10.0f).withCentre (jack.translated (0.0f, 26.0f)), "PHONES", look(), 8.0f);
        }
    }

    // DISPLAY ACCESS: its banner, three boxes, each named in a dark bar under it.
    {
        const char* names[] = { "CONFIGURATION", "MIXING", "AUX" };
        hw::silk (g, accessBoxes[0].toFloat().withY ((float) accessBoxes[0].getY() - 16.0f).withHeight (14.0f), "DISPLAY ACCESS", look(), 10.5f);
        for (int b = 0; b < 3; ++b)
        {
            const auto box = accessBoxes[(size_t) b].toFloat();
            g.setColour (look().sectionLine.withAlpha (0.55f));
            g.drawRoundedRectangle (box, 3.0f, 1.0f);
            auto bar = box.withTrimmedTop (box.getHeight() - 15.0f).reduced (1.0f, 1.0f);
            g.setColour (juce::Colour (0xff1c1d1f));
            g.fillRect (bar);
            hw::silk (g, bar, names[b], look(), 9.0f);
        }
    }

    // The display's bezel: the readouts at its left, the screen, the stereo meter.
    {
        const auto bz = bezelArea.toFloat();
        g.setColour (juce::Colour (0xff1e1f21));
        g.fillRoundedRectangle (bz, 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (bz.reduced (0.5f), 8.0f, 1.0f);
    }
    // SCENE MEMORY (its decimal point the edit indicator), FADER STATUS, SELECTED
    // CHANNEL with what kind of strip it is (MAN p.6).
    {
        auto r = readoutArea.toFloat();
        hw::silk (g, r.removeFromTop (12.0f), "SCENE MEMORY", look(), 8.0f, juce::Justification::centredLeft, true);
        auto scene = r.removeFromTop (40.0f).withWidth (70.0f);
        readout (scene, juce::String (memory).paddedLeft ('0', 2), 32.0f);
        led (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ scene.getRight() - 4.0f, scene.getBottom() - 6.0f }), red, false);
        r.removeFromTop (14.0f);
        hw::silk (g, r.removeFromTop (12.0f), "FADER STATUS", look(), 8.0f, juce::Justification::centredLeft, true);
        const int aux = auxShown();
        {
            auto st = r.removeFromTop (44.0f);
            auto kinds = st.removeFromLeft (40.0f);
            led (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ kinds.getX() + 6.0f, kinds.getY() + 10.0f }), amber, aux < 0);
            hw::silk (g, juce::Rectangle<float> (kinds.getX() + 12.0f, kinds.getY() + 4.0f, 30.0f, 12.0f), "INPUT", look(), 7.0f, juce::Justification::centredLeft, true);
            juce::Path tri;
            const juce::Point<float> c (kinds.getX() + 6.0f, kinds.getY() + 30.0f);
            tri.addTriangle (c.x - 4.0f, c.y - 3.0f, c.x + 4.0f, c.y - 3.0f, c.x, c.y + 4.0f);
            g.setColour (aux >= 0 ? amber : amber.withAlpha (0.16f));
            g.fillPath (tri);
            hw::silk (g, juce::Rectangle<float> (kinds.getX() + 12.0f, kinds.getY() + 24.0f, 30.0f, 12.0f), "AUX", look(), 7.0f, juce::Justification::centredLeft, true);
            for (int n = 0; n < 8; ++n)
            {
                const auto cell = juce::Rectangle<float> (st.getX() + (float) (n % 4) * 15.0f, st.getY() + (float) (n / 4) * 20.0f, 15.0f, 18.0f);
                g.setColour (aux == n ? amber : amber.withAlpha (0.18f));
                g.setFont (type::font (12.0f, true));
                g.drawText (juce::String (n + 1), cell, juce::Justification::centred);
            }
        }
        r.removeFromTop (10.0f);
        hw::silk (g, r.removeFromTop (12.0f), "SELECTED CHANNEL", look(), 8.0f, juce::Justification::centredLeft, true);
        const int s = display.getStrip();
        const int n = s < 16 ? s + 1 : s < y02r::kTape ? 17 + 2 * (s - 16) : s < y02r::kEff ? s - y02r::kTape + 1 : s < y02r::kBus ? s - y02r::kEff + 1
                    : s < y02r::kAux ? s - y02r::kBus + 1 : s < y02r::kStereo ? s - y02r::kAux + 1 : -1;
        readout (r.removeFromTop (40.0f).withWidth (70.0f), n < 0 ? juce::String ("--") : juce::String (n).paddedLeft ('0', 2), 32.0f);
        r.removeFromTop (4.0f);
        const char* kinds[] = { "MIC/LINE", "TAPE/RTN", "OUTPUT" };
        const int kind = s < y02r::kTape ? 0 : s < y02r::kBus ? 1 : 2;
        for (int k = 0; k < 3; ++k)
        {
            auto row = r.removeFromTop (14.0f);
            juce::Path tri;
            const juce::Point<float> c (row.getX() + 6.0f, row.getCentreY());
            tri.addTriangle (c.x, c.y - 4.0f, c.x + 4.0f, c.y + 3.0f, c.x - 4.0f, c.y + 3.0f);
            g.setColour (kind == k ? amber : amber.withAlpha (0.16f));
            g.fillPath (tri);
            hw::silk (g, row.withTrimmedLeft (14.0f), kinds[k], look(), 7.5f, juce::Justification::centredLeft, true);
        }
    }

    // The stereo meter (MAN p.6): 21 LEDs a side, the scale between them, L STEREO R under.
    {
        auto m = meterArea().toFloat();
        auto names = m.removeFromBottom (14.0f);
        hw::silk (g, names, "L  STEREO  R", look(), 8.0f);
        static constexpr float steps[] = { -0.1f, -1.0f, -2.0f, -3.0f, -4.0f, -5.0f, -6.0f, -8.0f, -10.0f, -12.0f, -15.0f,
                                           -18.0f, -21.0f, -24.0f, -30.0f, -35.0f, -42.0f, -48.0f, -54.0f, -60.0f, -72.0f };
        auto* st = ctx.engine.getMains().empty() ? nullptr : ctx.engine.getMains()[0].get();
        const float segH = m.getHeight() / 21.0f;
        for (int k = 0; k < 21; ++k)
        {
            auto row = juce::Rectangle<float> (m.getX(), m.getY() + segH * (float) k, m.getWidth(), segH);
            const float db = steps[k];
            const bool labelled = k == 0 || db == -2.0f || db == -4.0f || db == -6.0f || db == -12.0f || db == -15.0f || db == -24.0f || db == -35.0f
                                  || db == -48.0f || db == -60.0f || db == -72.0f;
            if (labelled)
                hw::silk (g, row.reduced (12.0f, -2.0f), k == 0 ? juce::String ("CLIP") : juce::String ((int) db), look(), 7.0f, juce::Justification::centred, true);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float level = st != nullptr ? juce::Decibels::gainToDecibels (ctx.meters.get (st, MeterPoint::Output, ch), -100.0f) : -100.0f;
                const auto seg = juce::Rectangle<float> (9.0f, juce::jmax (2.0f, segH - 3.0f)).withCentre ({ ch == 0 ? m.getX() + 5.0f : m.getRight() - 5.0f, row.getCentreY() });
                const auto colour = k == 0 ? juce::Colour (0xffff4040) : k < 7 ? amber : green;
                g.setColour (level >= db ? colour : colour.withAlpha (0.15f));
                g.fillRect (seg);
            }
        }
        // CONTRAST, under the meter.
        const auto kn = contrastKnob.toFloat();
        knobAt (kn.getCentre(), kn.getWidth());
        hw::silk (g, kn.withY (kn.getBottom()).withHeight (10.0f).expanded (20.0f, 0.0f), "CONTRAST", look(), 7.0f, juce::Justification::centred, true);
    }

    // SELECTED CHANNEL: its four boxes.
    {
        const auto all = routingBox.getUnion (eqBox).getUnion (auxBox).getUnion (panBox).toFloat().expanded (8.0f, 8.0f).withTrimmedTop (-10.0f);
        hw::section (g, all, "SELECTED CHANNEL", look());
        hw::section (g, routingBox.toFloat(), "ROUTING", look());
        hw::section (g, auxBox.toFloat(), "AUX", look());
        hw::section (g, panBox.toFloat(), "PAN", look());
        hw::section (g, eqBox.toFloat(), "EQUALIZER", look());
        // AUX: the send level on a column of ten LEDs.
        const int m = memberOf (display.getStrip());
        const int level = m >= 0 ? juce::jmax (0, y.get (y02r::sendAddress (m, selAux))) : 0;
        const int lit = juce::roundToInt ((float) level / 127.0f * 10.0f);
        const auto bar = auxBar.toFloat();
        for (int k = 0; k < 10; ++k)
            led (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ bar.getCentreX(), bar.getBottom() - bar.getHeight() * ((float) k + 0.5f) / 10.0f }),
                 k >= 8 ? amber : green, k < lit);
        // PAN: a row of ten LEDs, lit where it points (the middle two at C).
        const int p = juce::jmax (0, y.get (y02r::address (display.getStrip(), panSide == 0 ? kPanL : kPanR)));
        const float at = (float) p / 32.0f * 9.0f;
        const auto pb = panBar.toFloat();
        for (int k = 0; k < 10; ++k)
            led (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ pb.getX() + pb.getWidth() * ((float) k + 0.5f) / 10.0f, pb.getY() + 4.0f }), green,
                 std::abs ((float) k - at) < 0.55f || (p == 16 && (k == 4 || k == 5)));
        hw::silk (g, pb.withTrimmedTop (10.0f), juce::String::fromUTF8 ("L ODD  \xe2\x96\xb2  R EVEN"), look(), 7.5f, juce::Justification::centred, true);
        // EQUALIZER: the band keys joined by a bar; Q, F, G on their readouts.
        const auto first = eqKeys[0]->getBounds().toFloat(), last = eqKeys[3]->getBounds().toFloat();
        g.setColour (look().silkDim.withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (first.getX() + 6.0f, first.getY() - 5.0f, last.getRight() - first.getX() - 12.0f, 2.0f));
        const char* tags[] = { "Q", "F", "G" };
        for (int k = 0; k < 3; ++k)
        {
            const auto r = eqReadouts[(size_t) k].toFloat();
            auto* e = k == 0 ? eqQ.get() : k == 1 ? eqF.get() : eqG.get();
            const int v = e->get ? e->get() : 0;
            auto text = e->text ? e->text (v) : juce::String (v);
            const bool khz = text.endsWith ("kHz"), hz = ! khz && text.endsWith ("Hz");
            text = text.upToFirstOccurrenceOf (" ", false, false);
            hw::silk (g, r.withWidth (14.0f).translated (-16.0f, 0.0f), tags[k], look(), 10.0f);
            readout (r, text, 13.0f);
            if (k == 1)
            {
                led (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ r.getRight() + 6.0f, r.getY() + 5.0f }), red, hz);
                hw::silk (g, juce::Rectangle<float> (r.getRight() + 10.0f, r.getY(), 22.0f, 10.0f), "Hz", look(), 7.0f, juce::Justification::centredLeft, true);
                led (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ r.getRight() + 6.0f, r.getBottom() - 5.0f }), red, khz);
                hw::silk (g, juce::Rectangle<float> (r.getRight() + 10.0f, r.getBottom() - 10.0f, 22.0f, 10.0f), "kHz", look(), 7.0f, juce::Justification::centredLeft, true);
            }
            if (k == 2)
                hw::silk (g, juce::Rectangle<float> (r.getRight() + 4.0f, r.getY(), 22.0f, r.getHeight()), "dB", look(), 7.5f, juce::Justification::centredLeft, true);
        }
    }

    // The monitor column (MAN p.8).
    if (! monitorArea.isEmpty() && monitorKeys.size() == 15)
    {
        const auto over = [&] (int from, int to, const char* title)
        {
            auto r = monitorKeys[(size_t) from]->getBounds().getUnion (monitorKeys[(size_t) to]->getBounds()).toFloat().expanded (4.0f, 4.0f).withTrimmedTop (-12.0f);
            g.setColour (look().sectionLine.withAlpha (0.4f));
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
            hw::silk (g, r.withHeight (13.0f), title, look(), 8.0f);
        };
        over (0, 2, "STUDIO");
        over (3, 11, "CONTROL ROOM");
        over (12, 13, "TALKBACK");
        const auto k = crLevel.getBounds().toFloat();
        hw::silk (g, k.withY (k.getBottom()).withHeight (11.0f).expanded (20.0f, 0.0f), "C-R LEVEL", look(), 8.0f);
        hw::silk (g, juce::Rectangle<float> (14.0f, 9.0f).withCentre ({ k.getX() + 2.0f, k.getBottom() - 4.0f }), "0", look(), 7.0f, juce::Justification::centred, true);
        hw::silk (g, juce::Rectangle<float> (14.0f, 9.0f).withCentre ({ k.getRight() - 2.0f, k.getBottom() - 4.0f }), "10", look(), 7.0f, juce::Justification::centred, true);
    }

    // The tape row's and the faders' scribble strips: blank grey strips to write on.
    g.setColour (juce::Colour (0xffb7b9b4));
    for (int i = 0; i < kNumEncoders; ++i)
        g.fillRect (encOnKeys[(size_t) i]->getBounds().toFloat().withY ((float) encOnKeys[(size_t) i]->getBottom() + 3.0f).withHeight (9.0f).reduced (2.0f, 0.0f));
    for (int i = 0; i < kNumFaders; ++i)
        g.fillRect (onKeys[(size_t) i]->getBounds().toFloat().withY ((float) onKeys[(size_t) i]->getBottom() + 3.0f).withHeight (11.0f).reduced (2.0f, 0.0f));

    // SCENE MEMORY and CURSOR printed under their keys; the cursor keys' oval.
    {
        const auto k = panelKeys[0]->getBounds().getUnion (panelKeys[1]->getBounds()).toFloat();
        hw::silk (g, k.withY (k.getBottom() + 2.0f).withHeight (13.0f), "SCENE MEMORY", look(), 9.5f);
        const auto c = panelKeys[4]->getBounds().getUnion (panelKeys[7]->getBounds()).getUnion (panelKeys[5]->getBounds()).getUnion (panelKeys[6]->getBounds())
                           .toFloat().expanded (10.0f, 8.0f);
        g.setColour (juce::Colour (0xff8d9093));
        g.fillEllipse (c);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawEllipse (c.reduced (0.5f), 1.0f);
        hw::silk (g, c.withY (c.getBottom() + 2.0f).withHeight (12.0f), "CURSOR", look(), 9.0f);
        const auto w = wheel->getBounds().toFloat();
        hw::knob (g, w, (float) std::fmod (wheel->getValue() / 48.0 + 1.0e6, 1.0), look(), hw::Look::Knob::Wheel, juce::Colour (0xff1c1d1f));
    }

    // The faders' names below them.
    g.setFont (type::font (type::kLabel, true));
    for (int i = 0; i < kNumFaders; ++i)
    {
        const auto f = faders[(size_t) i]->getBounds();
        const int s = faderStrip (i);
        juce::String name = s < 16 ? juce::String (s + 1)
                          : s < y02r::kTape ? juce::String (17 + 2 * (s - 16)) + "/" + juce::String (18 + 2 * (s - 16))
                          : s < y02r::kEff  ? "T" + juce::String (s - y02r::kTape + 1)
                                            : y02r::stripName (s);
        hw::silk (g, f.withY (f.getBottom()).withHeight (20).toFloat(), name, look(), 12.0f);
    }
}

void Y02RSurface::resized()
{
    // The panel as the console has it (MAN pp.2-10, Getting Started p.4): the
    // analog inputs along the top and the analog outputs at their right; the
    // middle band with DISPLAY ACCESS, the display in its bezel (the scene and
    // channel readouts at its left, the stereo meter at its right), SELECTED
    // CHANNEL, and the monitor column at the far right; the tape returns'
    // encoders above the fader columns; the faders; SCENE MEMORY, the wheel, the
    // cursor keys and ENTER at the bottom right.
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
    auto top = area.removeFromTop (kTopBar);
    universalKey.setBounds (top.removeFromRight (190).reduced (sizes::kGap, 7));
    area.removeFromLeft (14);
    area.removeFromRight (14); // the wooden cheeks

    // The fader columns first: the rows above line up with them.
    // STEREO's strip is twice as wide as a channel's.
    const int colW = 66, x0 = area.getX() + 50;
    auto colX = [x0, colW] (int i) { return x0 + i * colW + (i >= 16 ? 8 : 0) + (i >= 20 ? 8 : 0); };
    analogArea = area.removeFromTop (78).withX (x0).withWidth (colX (20) - x0);

    // The monitor column at the far right, down the middle band and the encoder row.
    auto monitorColumn = area.withTrimmedLeft (area.getWidth() - 132).withHeight (470).reduced (6, 10);
    monitorArea = monitorColumn;
    {
        auto r = monitorColumn.withTrimmedTop (12);
        const int kh = 30, half = r.getWidth() / 2;
        auto rowOf = [&] (std::initializer_list<int> keys)
        {
            auto row = r.removeFromTop (kh);
            int k = 0;
            for (int i : keys)
                monitorKeys[(size_t) i]->setBounds (juce::Rectangle<int> (row.getX() + half * k++, row.getY(), half, kh).reduced (2, 1));
            r.removeFromTop (3);
        };
        rowOf ({ 0 });
        rowOf ({ 1, 2 });
        r.removeFromTop (18);
        rowOf ({ 3, 4 });
        rowOf ({ 5, 6 });
        rowOf ({ 7 });
        rowOf ({ 8, 9 });
        rowOf ({ 10, 11 });
        r.removeFromTop (18);
        rowOf ({ 12, 13 });
        r.removeFromTop (8);
        monitorKeys[14]->setBounds (r.removeFromTop (kh + 12).withSizeKeepingCentre (56, kh + 12)); // SOLO
        r.removeFromTop (8);
        crLevel.setBounds (r.removeFromTop (60).withSizeKeepingCentre (54, 54));
    }
    area.removeFromRight (132);

    // The middle band.
    auto middle = area.removeFromTop (340).reduced (14, 6);
    {
        // DISPLAY ACCESS: CONFIGURATION, MIXING, AUX, eight keys each.
        auto left = middle.removeFromLeft (300).withTrimmedTop (14);
        const int boxH = (left.getHeight() - 12) / 3;
        auto config = [this] (int k) -> ToggleKey& {
            // SCENE MEMORY, DIGITAL I/O, SETUP, UTILITY / AUTOMIX, MIDI, GROUP, PAIR
            switch (k)
            {
                case 0: return *functionKeys[0].second;
                case 1: return *darkKeys[0];
                case 2: return *darkKeys[1];
                case 3: return *functionKeys[1].second;
                case 4: return *darkKeys[2];
                case 5: return *functionKeys[2].second;
                case 6: return *functionKeys[3].second;
                default: return *functionKeys[4].second;
            }
        };
        // MIXING: phase / ATT, DELAY, PAN, ROUTING / METER, VIEW, EQ, DYNAMICS.
        const int mixing[] = { 8, 9, 10, 7, 5, 6, 11, 12 };
        for (int b = 0; b < 3; ++b)
        {
            auto box = left.removeFromTop (boxH);
            left.removeFromTop (6);
            accessBoxes[(size_t) b] = box;
            auto keys = box.reduced (4, 4).withTrimmedBottom (14);
            const int kw = keys.getWidth() / 4, kh = keys.getHeight() / 2;
            for (int k = 0; k < 8; ++k)
            {
                auto& key = b == 0 ? config (k) : b == 1 ? *functionKeys[(size_t) mixing[k]].second : *functionKeys[(size_t) (13 + k)].second;
                key.setBounds (juce::Rectangle<int> (keys.getX() + (k % 4) * kw, keys.getY() + (k / 4) * kh, kw, kh).reduced (3, 1));
            }
        }
    }
    middle.removeFromLeft (14);
    // The bezel: the readouts, the display, the meter and CONTRAST.
    bezelArea = middle.removeFromLeft (800);
    {
        auto bz = bezelArea.reduced (10, 10);
        readoutArea = bz.removeFromLeft (100);
        bz.removeFromLeft (8);
        auto meterColumn = bz.removeFromRight (62);
        contrastKnob = meterColumn.removeFromBottom (50).withSizeKeepingCentre (26, 26).translated (0, -6);
        meterRect = meterColumn.reduced (2, 2);
        bz.removeFromRight (8);
        display.setBounds (bz);
    }
    middle.removeFromLeft (16);

    // SELECTED CHANNEL: ROUTING down the left; AUX and PAN over EQUALIZER.
    {
        auto sel = middle.reduced (8, 4).withTrimmedTop (14);
        routingBox = sel.removeFromLeft (112);
        sel.removeFromLeft (8);
        {
            auto r = routingBox.reduced (8, 6).withTrimmedTop (14);
            const int kh = r.getHeight() / 5, kw = r.getWidth() / 2;
            for (int k = 0; k < 10; ++k)
                routingKeys[(size_t) k]->setBounds (juce::Rectangle<int> (r.getX() + (k % 2) * kw, r.getY() + (k / 2) * kh, kw, kh).withSizeKeepingCentre (kw - 4, 28));
        }
        auto upperRow = sel.removeFromTop (sel.getHeight() / 2 + 6);
        sel.removeFromTop (8);
        eqBox = sel;
        auxBox = upperRow.removeFromLeft (upperRow.getWidth() * 3 / 5);
        upperRow.removeFromLeft (8);
        panBox = upperRow;
        {
            // AUX 1-4 and 5-8 in two columns, the LED column, SEND LEVEL and ON.
            auto r = auxBox.reduced (8, 6).withTrimmedTop (14);
            auto keys = r.removeFromLeft (r.getWidth() / 2 + 20);
            const int kh = keys.getHeight() / 4, kw = keys.getWidth() / 2;
            for (int k = 0; k < 8; ++k)
                auxKeys[(size_t) k]->setBounds (juce::Rectangle<int> (keys.getX() + (k / 4) * kw, keys.getY() + (k % 4) * kh, kw, kh).reduced (2, 2));
            auxBar = r.removeFromLeft (16).reduced (0, 4);
            r.removeFromLeft (4);
            auxKeys[8]->setBounds (r.removeFromBottom (30).reduced (8, 0));
            sendLevel->setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 76), juce::jmin (r.getHeight(), 76)));
        }
        {
            // L/ODD and R/EVEN, the LED row, the encoder at the right.
            auto r = panBox.reduced (8, 6).withTrimmedTop (14);
            pan->setBounds (r.removeFromRight (56).withSizeKeepingCentre (56, 56));
            auto keys = r.removeFromTop (32);
            panKeys[0]->setBounds (keys.removeFromLeft (keys.getWidth() / 2).reduced (2, 0));
            panKeys[1]->setBounds (keys.reduced (2, 0));
            r.removeFromTop (12);
            panBar = r.removeFromTop (24);
        }
        {
            // The band keys in a row, EQ ON under L-MID; Q, F and G's readouts over their encoders.
            auto r = eqBox.reduced (8, 6).withTrimmedTop (18);
            auto keys = r.removeFromLeft (r.getWidth() / 2);
            const int kw = keys.getWidth() / 4;
            for (int k = 0; k < 4; ++k)
                eqKeys[(size_t) k]->setBounds (juce::Rectangle<int> (keys.getX() + kw * k, keys.getY() + 6, kw, 34).reduced (1, 0));
            eqKeys[4]->setBounds (juce::Rectangle<int> (keys.getX() + kw, keys.getY() + 52, kw, 30).reduced (2, 0));
            r.removeFromLeft (10);
            const int w = r.getWidth() / 3;
            for (int k = 0; k < 3; ++k)
            {
                auto col = juce::Rectangle<int> (r.getX() + w * k, r.getY(), w, r.getHeight()).reduced (6, 0);
                eqReadouts[(size_t) k] = col.removeFromTop (26).withTrimmedLeft (10).withTrimmedRight (k == 0 ? 0 : 14);
                auto* e = k == 0 ? eqQ.get() : k == 1 ? eqF.get() : eqG.get();
                e->setBounds (col.withSizeKeepingCentre (juce::jmin (col.getWidth(), 62), juce::jmin (col.getHeight(), 62)));
            }
        }
    }

    // The encoder row: TAPE 1-16 above faders 1-16, FLIP in the gap after them,
    // then EFF1 and EFF2 RTN.
    auto encRow = area.removeFromTop (132).withTrimmedTop (4);
    auto encoderColumn = [&] (int col) { return juce::Rectangle<int> (colX (col), encRow.getY(), colW, encRow.getHeight()).reduced (3, 0); };
    for (int i = 0; i < kNumEncoders; ++i)
    {
        auto col = encoderColumn (i < 16 ? i : i + 1);
        col.removeFromBottom (12); // the scribble strip
        encOnKeys[(size_t) i]->setBounds (col.removeFromBottom (26).reduced (4, 0));
        encSelKeys[(size_t) i]->setBounds (col.removeFromBottom (27).reduced (4, 0).withTrimmedBottom (1));
        encoders[(size_t) i]->setBounds (col.reduced (0, 1));
    }
    flipKey.setBounds (encoderColumn (16).removeFromBottom (12 + 48).removeFromTop (44).reduced (4, 0));

    // The faders: SEL and ON above each, the scribble strip, the name below.
    auto faderArea = area.withTrimmedTop (4).withTrimmedBottom (4);
    for (int i = 0; i < kNumFaders; ++i)
    {
        auto col = juce::Rectangle<int> (colX (i), faderArea.getY(), i == 20 ? 2 * colW : colW, faderArea.getHeight() - 22);
        selKeys[(size_t) i]->setBounds (col.removeFromTop (28).reduced (6 + (i == 20 ? colW / 2 : 0), 1));
        col.removeFromTop (2);
        onKeys[(size_t) i]->setBounds (col.removeFromTop (28).reduced (6 + (i == 20 ? colW / 2 : 0), 1));
        col.removeFromTop (18); // the scribble strip
        faders[(size_t) i]->setBounds (col);
    }

    // The bottom right: SCENE MEMORY (STORE, up over down, RECALL), the wheel,
    // the cursor keys right of it, ENTER below it.
    auto right = faderArea.withLeft (colX (20) + 2 * colW + 26).withTrimmedRight (14);
    auto memRow = right.removeFromTop (70).withTrimmedTop (10);
    const int mw = memRow.getWidth() / 4;
    panelKeys[0]->setBounds (juce::Rectangle<int> (memRow.getX(), memRow.getY(), mw, 44).reduced (4, 0));          // STORE
    panelKeys[2]->setBounds (juce::Rectangle<int> (memRow.getX() + mw + mw / 4, memRow.getY(), mw, 21).reduced (6, 0)); // up
    panelKeys[3]->setBounds (juce::Rectangle<int> (memRow.getX() + mw + mw / 4, memRow.getY() + 23, mw, 21).reduced (6, 0)); // down
    panelKeys[1]->setBounds (juce::Rectangle<int> (memRow.getX() + 2 * mw + mw / 2, memRow.getY(), mw, 44).reduced (4, 0)); // RECALL
    right.removeFromTop (24);
    auto wheelRow = right.removeFromTop (172);
    wheel->setBounds (wheelRow.removeFromLeft (172).reduced (6));
    const int cx = wheelRow.getX() + 18, cy = wheelRow.getCentreY() - 52;
    panelKeys[5]->setBounds (cx + 36, cy, 40, 32);       // up
    panelKeys[4]->setBounds (cx, cy + 36, 34, 32);       // left
    panelKeys[7]->setBounds (cx + 78, cy + 36, 34, 32);  // right
    panelKeys[6]->setBounds (cx + 36, cy + 72, 40, 32);  // down
    right.removeFromTop (4);
    panelKeys[8]->setBounds (right.removeFromTop (46).withWidth (70)); // ENTER
}

} // namespace livedaw::ui
