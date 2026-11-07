#include "ui/Y02RSurface.h"

#include "engine/MixerEngine.h"
#include "engine/Models.h"
#include "engine/Show.h"
#include "engine/Y02RMidi.h"
#include "engine/Yamaha02R.h"
#include "ui/DynamicsGraph.h"
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
    /** A table row's columns (DELAY's), as fractions of its width: the number,
        Smpl, mSEC, ON. */
    constexpr float kRowSamples = 0.2f, kRowMs = 0.5f, kRowOn = 0.8f;
    /** A row of a number and one value: where the value starts. */
    constexpr float kRowCompact = 0.4f;
    constexpr float kSmall = 10.0f;
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
    /** A value as the display prints it (V2 MAN pp.50, 81): no space before
        the unit and no ".0" ("0dB", "5ms", "-23dB", "3.5dB"), a ratio as
        "1:1", the knee "hard", the shelves "L.SHLF" and "H.SHLF". */
    juce::String displayText (const models::Param& p, float value)
    {
        auto t = p.display (value);
        if (t == "L.SHELF" || t == "H.SHELF")
            return t.substring (0, 2) + "SHLF";
        if (t == "HARD")
            return "hard";
        if (p.label == "RATIO")
            return t == "INF" ? t : t.trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + ":1";
        t = t.removeCharacters (" ");
        for (const char* unit : { "dB", "ms", "Hz", "s" })
            if (t.endsWith (unit) && t.dropLastCharacters ((int) strlen (unit)).endsWith (".0"))
                return t.dropLastCharacters ((int) strlen (unit) + 2) + unit;
        return t;
    }

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
    namespace
    {
        /** The pictures' marks (drawn further on): a word boxed, lit or outlined; a
            box as wide as its word; a knob with its pointer at `frac` of its travel. */
        void chip (juce::Graphics&, juce::Rectangle<float>, const juce::String&, bool lit);
        juce::Rectangle<float> fitted (juce::Rectangle<float>, const juce::String&, float margin = 8.0f);
        void knob (juce::Graphics&, juce::Rectangle<float>, float frac, float thickness = 1.4f);
    } // namespace

    int address (int strip, int item) { return strip * 28 + item; }
    int sendAddress (int member, int aux) { return kSendsBase + member * 10 + aux; }

    juce::String stripName (int s)
    {
        // As the display's channel box names them (V2 MAN p.19): MIC 1, LINE 17/18, TAPE 1, EFF1, BUS 1, AUX 1, STEREO.
        if (s < 16)
            return "MIC " + juce::String (s + 1);
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
        ask (what, std::move (go));
    }

    void Display::ask (const juce::String& what, std::function<void()> go)
    {
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
            case Function::Routing:
                // ROUTING, SURROUND on / off; with it on the pan graphs and the trajectories.
                return y.getState (Yamaha02R::Surround, 0) > 0 ? 4 : 2;
            case Function::Group:
            case Function::Pair:
            case Function::View: // VIEW, the CHANNEL LIBRARY
            case Function::Att:
            case Function::Delay:
            case Function::Pan:
                return 2;
            case Function::Aux1:
            case Function::Aux2:
            case Function::Aux3:
            case Function::Aux4:
            case Function::Aux5:
            case Function::Aux6:
            case Function::Aux7:
            case Function::Aux8:
                // AUX 1-8: MIC/LINE, TAPE/EFF, the selected strip's; AUX 7-8 their effect's EDIT.
                return function >= Function::Aux7 ? 4 : 3;
            case Function::Eq:
            case Function::Dynamics:
            case Function::DigitalIo:
            case Function::Automix:
            case Function::Utility:
            case Function::TitleEdit:
                return 1;
        }
        return 1;
    }

    void Display::setStrip (int s)
    {
        strip = juce::jlimit (0, kStrips - 1, s);
        build();
    }

    void Display::build()
    {
        fields.clear();
        spots.clear();
        marks.clear();
        arrowAt = { 1.0f, 1.0f };
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
                confirm (false, "Recall " + y.programName (library, n) + "?", [this, library, n, target] { y.recall (library, n, target); });
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
            const double rate = ctx.engine.getSampleRate() > 0.0 ? ctx.engine.getSampleRate() : 48000.0; // 48 kHz before the device runs
            if (function == Function::Delay)
            {
                // [Smpl] or [mSEC] (MAN p.34): the unit the channels show and the wheel moves.
                fields.push_back ({ "UNIT", [this] { return delayMs ? 1 : 0; }, [this] (int v) { delayMs = v != 0; build(); },
                                    [] (int v) { return juce::String (v != 0 ? "mSEC" : "Smpl"); }, 0, 1 });
            }
            for (int st : pageStrips)
            {
                Field f;
                f.label = shortName (st);
                f.strip = st;
                if (function == Function::Att)
                {
                    // The attenuator; ENTER flips the phase.
                    const int a = address (st, kAtt), sw = address (st, kSwitches);
                    f.get = [this, a] { return y.get (a); };
                    f.set = [this, a] (int v) { y.set (a, v); };
                    f.hi = st == kStereo ? 108 : 96;
                    f.text = [] (int v) { return juce::String (v - 96) + "dB"; };
                    if (st != kStereo)
                    {
                        // The phase over the knob: NOR, or REV lit.
                        f.action = [this, sw] { y.set (sw, juce::jmax (0, y.get (sw)) ^ (1 << kPhase)); };
                        f.tagOn = [this, sw] { return ((y.get (sw) >> kPhase) & 1) != 0; };
                        f.tag = [on = f.tagOn] { return juce::String (on() ? "REV" : "NOR"); };
                        f.tagAbove = true;
                    }
                }
                else if (function == Function::Delay)
                {
                    if (memberOf (st) < 0)
                        continue;
                    // The time in samples; ENTER switches the delay on or off.
                    // Digit by digit (MAN p.35): samples' thousands to ones, or ms' tens, ones and tenths.
                    const int hi = address (st, kDelayHi), lo = address (st, kDelayLo), sw = address (st, kSwitches);
                    const auto samples = [this, hi, lo] { return (juce::jmax (0, y.get (hi)) << 7) | juce::jmax (0, y.get (lo)); };
                    const auto setSamples = [this, hi, lo] (int v)
                    {
                        v = juce::jlimit (0, 2600, v);
                        y.set (hi, v >> 7);
                        y.set (lo, v & 0x7f);
                    };
                    // ON at the row's end; the unit the wheel doesn't move beside it.
                    f.tagOn = [this, sw] { return ((y.get (sw) >> kDelayOn) & 1) != 0; };
                    f.tag = [on = f.tagOn] { return juce::String (on() ? "ON" : "OFF"); };
                    const auto msText = [] (int v) { return juce::String (v / 10).paddedLeft ('0', 2) + "." + juce::String (v % 10); };
                    if (delayMs)
                    {
                        // Tenths of a ms; a sample's step is finer, so the nearest sample.
                        f.get = [samples, rate] { return juce::roundToInt (samples() * 10000.0 / rate); };
                        f.set = [setSamples, rate] (int v) { setSamples (juce::roundToInt (v * rate / 10000.0)); };
                        f.hi = juce::roundToInt (2600 * 10000.0 / rate);
                        f.digits = 3;
                        f.text = msText;
                        f.note = [samples] { return juce::String (samples()); };
                        f.noteFirst = true;
                    }
                    else
                    {
                        f.get = samples;
                        f.set = setSamples;
                        f.hi = 2600;
                        f.digits = 4;
                        f.text = [] (int v) { return juce::String (v).paddedLeft ('0', 4); };
                        f.note = [samples, rate, msText] { return msText (juce::roundToInt (samples() * 10000.0 / rate)); };
                    }
                    f.action = [this, sw] { y.set (sw, juce::jmax (0, y.get (sw)) ^ (1 << kDelayOn)); };
                }
                else if (function == Function::Pan)
                {
                    const int a = address (st, kPanL);
                    f.get = [this, a] { return y.get (a); };
                    f.set = [this, a] (int v) { y.set (a, v); };
                    f.hi = 32;
                    f.text = panText;
                    if (isStereoStrip (st))
                    {
                        // Its two pans (V2 MAN p.36): L, and R after it.
                        f.noNumber = true;
                        fields.push_back (f);
                        const int r = address (st, kPanR);
                        f.get = [this, r] { return y.get (r); };
                        f.set = [this, r] (int v) { y.set (r, v); };
                    }
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
                    f.text = [this, on, aux, levelText] (int v) { return ((y.get (on) >> aux) & 1) != 0 ? levelText (v) : juce::String ("OFF"); }; // OFF in the level's place (p.98)
                    f.action = [this, pre, aux] { y.set (pre, juce::jmax (0, y.get (pre)) ^ (1 << aux)); };
                    // PRE or POST under the value (V2 MAN p.107), PRE lit.
                    f.tagOn = [this, pre, aux] { return ((y.get (pre) >> aux) & 1) != 0; };
                    f.tag = [on = f.tagOn] { return juce::String (on() ? "PRE" : "POST"); };
                }
                fields.push_back (f);
            }
            if (function == Function::Delay)
                layoutDelay();
            else
                layoutChannels();
            focus = juce::jlimit (0, juce::jmax (0, (int) fields.size() - 1), focus);
            digit = juce::jlimit (0, juce::jmax (0, fields.empty() ? 0 : fields[(size_t) focus].digits - 1), digit);
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
                    confirm (false, "Recall " + programText (n) + "?", [this, n, s] { y.recall (Yamaha02R::Channel, n, s); });
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
                    confirm (true, "Clear " + programText (n) + "?", [this, n]
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
                if (pairOf (s) >= 0)
                    stateField ("MS", Yamaha02R::Ms, pairOf (s), 0, 1, onOff);
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
                if (function == Function::View)
                    layoutView();
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
                if (page > 0)
                {
                    const int mode = y.getState (Yamaha02R::Surround, 0);
                    if (page == 1)
                    {
                        // ROUTING 2/2 (V2 MAN p.65): SURROUND, asked; its mode.
                        Field on;
                        on.label = "SURROUND";
                        on.get = [mode] { return mode > 0 ? 1 : 0; };
                        on.text = onOff;
                        on.lo = on.hi = 0;
                        on.action = [this, mode]
                        {
                            ask (mode > 0 ? "Surround Off?" : "Surround On?", [this, mode]
                            {
                                y.setState (Yamaha02R::Surround, 0, mode > 0 ? 0 : surroundMode);
                                build();
                            });
                        };
                        fields.push_back (on);
                        fields.push_back ({ "MODE", [this, mode] { return mode > 0 ? mode : surroundMode; },
                                            [this, mode] (int v)
                                            {
                                                surroundMode = v;
                                                if (mode > 0)
                                                    y.setState (Yamaha02R::Surround, 0, v);
                                            },
                                            [] (int v) { const char* m[] = { "OFF", "2+2", "3+1", "3+2+1" }; return juce::String (m[juce::jlimit (0, 3, v)]); }, 1, 3 });
                        fixed ("BUS 1-4", "SURROUND");
                        fixed ("BUS 5-6", mode == 3 ? "SURROUND" : "ROUTING");
                        break;
                    }
                    const int m = memberOf (s);
                    if (m < 0)
                    {
                        fixed ("SURROUND", "INPUT CHANNELS ONLY");
                        break;
                    }
                    auto sur = [this, m] (const juce::String& label, int field, int lo, int hi, std::function<juce::String (int)> text)
                    {
                        const int i = m * Yamaha02R::kSurFields + field;
                        fields.push_back ({ label, [this, i] { return y.getState (Yamaha02R::SurroundPan, i); },
                                            [this, i] (int v) { y.setState (Yamaha02R::SurroundPan, i, v); }, std::move (text), lo, hi });
                    };
                    const auto lr = [] (int v) { return v == 32 ? juce::String ("C") : (v < 32 ? "L" : "R") + juce::String (std::abs (v - 32)); };
                    const auto fr = [] (int v) { return v == 32 ? juce::String ("0") : (v > 32 ? "FRONT " : "REAR ") + juce::String (std::abs (v - 32)); };
                    const auto amount = [] (int v) { return juce::String (v); };
                    if (page == 2)
                    {
                        // ROUTING 3/4 (V2 MAN pp.67-68): the place on the pan graph.
                        sur ("SURR", Yamaha02R::SurOn, 0, 1, onOff);
                        sur ("L / R", Yamaha02R::SurX, 0, 64, lr);
                        sur ("F / R", Yamaha02R::SurY, 0, 64, fr);
                        if (mode >= 2)
                            sur ("CSR", Yamaha02R::SurCsr, 0, 10, [] (int v) { return juce::String (v / 10.0, 1); });
                        if (mode == 3)
                            sur ("SW", Yamaha02R::SurSub, 0, 60, [] (int v) { return v == 0 ? utf8 ("-\xe2\x88\x9e") : juce::String (v - 60) + " dB"; });
                        Field traj;
                        traj.label = "TRAJECTORY";
                        traj.text = [] (int) { return juce::String ("4/4"); };
                        traj.get = [] { return 0; };
                        traj.lo = traj.hi = 0;
                        traj.action = [this] { show (Function::Routing, 3); };
                        fields.push_back (traj);
                    }
                    else
                    {
                        // ROUTING 4/4 (V2 MAN pp.69-73): a trajectory, its size and offset,
                        // the place along it (FAST four steps a click); ST LINK and its
                        // pattern; a speaker's place by JUMP.
                        sur ("TRAJECTORY", Yamaha02R::SurTraj, 0, 6,
                             [] (int v) { const char* t[] = { "L <> R", "F <> R", "RL <> FR", "FL <> RR", "L<>R SEMI", "F<>R SEMI", "CIRCLE" }; return juce::String (t[juce::jlimit (0, 6, v)]); });
                        const int along = m * Yamaha02R::kSurFields + Yamaha02R::SurAlong;
                        fields.push_back ({ "MOVE", [this, along] { return y.getState (Yamaha02R::SurroundPan, along); },
                                            [this, along] (int v)
                                            {
                                                const int was = y.getState (Yamaha02R::SurroundPan, along);
                                                y.setState (Yamaha02R::SurroundPan, along, surroundFast ? juce::jlimit (0, 64, was + (v - was) * 4) : v);
                                            },
                                            amount, 0, 64 });
                        fields.push_back ({ "FAST", [this] { return surroundFast ? 1 : 0; }, [this] (int v) { surroundFast = v != 0; }, onOff, 0, 1 });
                        sur ("WIDTH", Yamaha02R::SurWidth, 0, 32, amount);
                        sur ("DEPTH", Yamaha02R::SurDepth, 0, 32, amount);
                        sur ("OFFSET L/R", Yamaha02R::SurOffsetX, 0, 64, lr);
                        sur ("OFFSET F/R", Yamaha02R::SurOffsetY, 0, 64, fr);
                        if (mode >= 2)
                            sur ("CSR", Yamaha02R::SurCsr, 0, 10, [] (int v) { return juce::String (v / 10.0, 1); });
                        if (mode == 3)
                            sur ("SW", Yamaha02R::SurSub, 0, 60, [] (int v) { return v == 0 ? utf8 ("-\xe2\x88\x9e") : juce::String (v - 60) + " dB"; });
                        // ST LINK (p.72): a stereo channel's sides, or a made pair's channels, together.
                        if (isStereoStrip (s) || (pairOf (s) >= 0 && y.getState (Yamaha02R::Pair, pairOf (s)) != 0))
                        {
                            sur ("ST LINK", Yamaha02R::SurLink, 0, 1, onOff);
                            sur ("PATTERN", Yamaha02R::SurPattern, 0, 7, [] (int v)
                                 {
                                     const char* p[] = { "SAME", "MIRROR F-R", "MIRROR L-R", "MIRROR /", "MIRROR \\", "OPPOSITE", "TURN CW", "TURN CCW" };
                                     return juce::String (p[juce::jlimit (0, 7, v)]);
                                 });
                        }
                        // JUMP: the wheel picks a speaker, ENTER puts the sound there.
                        static const juce::StringArray speakers[] = { { "FL", "FR", "RL", "RR" }, { "L", "R", "C", "S" }, { "FL", "FR", "RL", "RR", "FC" } };
                        static const int places[3][5][2] = { { { 0, 64 }, { 64, 64 }, { 0, 0 }, { 64, 0 } },
                                                             { { 0, 64 }, { 64, 64 }, { 32, 64 }, { 32, 0 } },
                                                             { { 0, 64 }, { 64, 64 }, { 0, 0 }, { 64, 0 }, { 32, 64 } } };
                        const int k = juce::jlimit (1, 3, mode) - 1;
                        Field jump;
                        jump.label = "JUMP";
                        jump.lo = 0;
                        jump.hi = speakers[k].size() - 1;
                        jump.onEnter = true;
                        jump.get = [] { return 0; };
                        jump.text = [k] (int v) { return speakers[k][v]; };
                        jump.set = [this, m, k] (int v)
                        {
                            y.setState (Yamaha02R::SurroundPan, m * Yamaha02R::kSurFields + Yamaha02R::SurX, places[k][v][0]);
                            y.setState (Yamaha02R::SurroundPan, m * Yamaha02R::kSurFields + Yamaha02R::SurY, places[k][v][1]);
                        };
                        fields.push_back (jump);
                        Field exit;
                        exit.label = "EXIT";
                        exit.text = [] (int) { return juce::String ("3/4"); };
                        exit.get = [] { return 0; };
                        exit.lo = exit.hi = 0;
                        exit.action = [this] { show (Function::Routing, 2); };
                        fields.push_back (exit);
                    }
                    break;
                }
            {
                // ROUTING 1/2 (V2 MAN pp.39-40): every channel of the selected one's
                // side, MIC 1-16 and LINE 17/18-23/24 or TAPE 1-16 and EFF 1-2, a
                // column each; a row for each bus, S (STEREO), D (the direct out,
                // MIC 1-16); their numbers over and under them.
                const bool tape = s >= kTape && s < kBus;
                std::vector<int> columns;
                for (int k = 0; k < 16; ++k)
                    columns.push_back ((tape ? kTape : 0) + k);
                for (int k = 0; k < (tape ? 2 : 4); ++k)
                    columns.push_back ((tape ? kEff : 16) + k);
                const float units = (float) columns.size() + 0.6f, rows = 12.4f;
                const auto x = [units] (int c) { return ((float) c + (c >= 16 ? 0.6f : 0.0f)) / units; };
                const auto rowY = [rows] (int r) { return ((float) r + 1.0f + (r >= 8 ? 0.4f : 0.0f)) / rows; };
                for (int r = 0; r < 10; ++r)
                    for (int c = 0; c < (int) columns.size(); ++c)
                    {
                        const int st = columns[(size_t) c];
                        if (r == 9 && (tape || st >= 16))
                            continue;
                        const juce::String label = r < 8 ? juce::String (r + 1) : r == 8 ? juce::String ("S") : juce::String ("D");
                        const auto before = fields.size();
                        if (r < 8)
                            bitField (label, address (st, kRouting), r);
                        else if (r == 8)
                            bitField (label, address (st, kSwitches), kSt);
                        else
                            stateField (label, Yamaha02R::Direct, st, 0, 1, onOff);
                        if (fields.size() == before)
                            continue;
                        fields.back().look = Field::Look::Cell;
                        fields.back().strip = st;
                        spots.push_back ({ x (c), rowY (r), 1.0f / units, 1.0f / rows });
                    }
                for (int c = 0; c < (int) columns.size(); ++c)
                {
                    const int st = columns[(size_t) c];
                    const juce::String over = st >= kEff ? "E" + juce::String (st - kEff + 1) : st >= kTape ? juce::String (st - kTape + 1) : st >= 16 ? juce::String (17 + 2 * (st - 16)) : juce::String (st + 1);
                    const juce::String under = st >= 16 && st < kTape ? juce::String (18 + 2 * (st - 16)) : over;
                    marks.push_back ({ Mark::Box, { x (c), 0.0f, 1.0f / units, 1.0f / rows }, over, st == s });
                    marks.push_back ({ Mark::Box, { x (c), (rows - 1.0f) / rows, 1.0f / units, 1.0f / rows }, under, st == s });
                }
                arrowAt = { 1.0f, rowY (9) + 1.0f / rows };
                break;
            }
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
                               [p, k] (int v) { return k % 3 == 0 ? freqText (v) : displayText (p, stepValue (p, v)); });
                }
                bitField ("HPF", address (s, kSwitches), kHpf);
                bitField ("LPF", address (s, kSwitches), kLpf);
                // ATT at the left of the knobs, as the picture has it.
                byteField ("ATT", address (s, kAtt), 0, s == kStereo ? 108 : 96, [] (int v) { return juce::String (v - 96) + "dB"; });
                libraryField ("LIBRARY", Yamaha02R::Eq, s);
                layoutEq();
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
                    byteField (p.label, address (s, kDynParam + k), 0, juce::jmax (1, p.steps) - 1, [p] (int v) { return displayText (p, stepValue (p, v)); });
                }
                const char* keys[] = { "SELF-POST EQ", "SELF-PRE EQ", "LEFT-POST EQ", "LEFT-PRE EQ", "AUX 1", "AUX 2", "BUS 1", "BUS 2", "BUS 3", "BUS 4", "BUS 5", "BUS 6", "BUS 7", "BUS 8", "STEREO" };
                stateField ("KEY IN", Yamaha02R::KeyIn, s, 0, s >= kBus ? 14 : 5, [keys] (int v) { return juce::String (keys[juce::jlimit (0, 14, v)]); });
                if (pairOf (s) >= 0 || isStereoStrip (s))
                {
                    stateField ("ST.LINK", Yamaha02R::StLink, s, 0, 1, onOff);
                    stateField ("IN", Yamaha02R::InMode, s, 0, 2, [] (int v) { return juce::String (v == 0 ? "BOTH" : v == 1 ? "L" : "R"); });
                }
                libraryField ("LIBRARY", Yamaha02R::Dynamics, s);
                layoutDynamics();
                break;
            }
            case Function::Group:
            {
                // 1/2 the fader groups A-D, 2/2 the mute groups E-H: each group on,
                // and every channel's group.
                // As the pictures (V2 MAN pp.182-183): TAPE 1-16 and EFF 1-2, then
                // MIC 1-16 and LINE 17/18-23/24, a column each; a row per group,
                // a channel's group lit; each group's ENABLE at the right.
                const bool mute = page == 1;
                const auto groupWhat = mute ? Yamaha02R::MuteGroup : Yamaha02R::FaderGroup;
                const float units = 22.6f, rows = 12.0f;
                for (int g = 0; g < 4; ++g)
                {
                    const auto letter = juce::String::charToString ((mute ? 'E' : 'A') + g);
                    const auto before = fields.size();
                    stateField ("GRP " + letter, Yamaha02R::GroupOn, (mute ? 4 : 0) + g, 0, 1, onOff);
                    if (fields.size() == before)
                        continue;
                    fields.back().look = Field::Look::Button;
                    fields.back().text = [letter] (int) { return letter; };
                    spots.push_back ({ 20.9f / units, (float) (2 + g) / rows, 1.2f / units, 1.0f / rows });
                }
                marks.push_back ({ Mark::Caption, { 20.4f / units, 1.0f / rows, 2.2f / units, 1.0f / rows }, "Enable" });
                marks.push_back ({ Mark::Frame, { 20.6f / units, 1.9f / rows, 1.8f / units, 4.2f / rows }, {} });
                marks.push_back ({ Mark::Caption, { 0.0f, 0.0f, 1.0f, 1.0f / rows }, mute ? "Mute  Group" : "Fader  Group" });
                for (int side = 0; side < 2; ++side)
                {
                    // TAPE (and EFF) over MIC (and LINE).
                    std::vector<int> columns;
                    for (int k = 0; k < 16; ++k)
                        columns.push_back ((side == 0 ? kTape : 0) + k);
                    for (int k = 0; k < (side == 0 ? 2 : 4); ++k)
                        columns.push_back ((side == 0 ? kEff : 16) + k);
                    const float top = side == 0 ? 1.0f : 7.0f;
                    marks.push_back ({ Mark::Caption, { 0.0f, (top - 1.0f) / rows, 2.0f / units, 1.0f / rows }, side == 0 ? "Tape" : "Mic" });
                    for (int g = 0; g < 4; ++g)
                        for (int c = 0; c < (int) columns.size(); ++c)
                        {
                            const int st = columns[(size_t) c], m = memberOf (st);
                            if (y.getState (groupWhat, m) < 0)
                                continue;
                            Field f;
                            f.label = juce::String::charToString ((mute ? 'E' : 'A') + g);
                            f.strip = st;
                            f.look = Field::Look::Cell;
                            f.lo = 0;
                            f.hi = 1;
                            f.get = [this, groupWhat, m, g] { return y.getState (groupWhat, m) == g + 1 ? 1 : 0; };
                            f.set = [this, groupWhat, m, g] (int v)
                            {
                                if (v != 0)
                                    y.setState (groupWhat, m, g + 1);
                                else if (y.getState (groupWhat, m) == g + 1)
                                    y.setState (groupWhat, m, 0);
                            };
                            f.text = onOff;
                            fields.push_back (f);
                            spots.push_back ({ (float) c / units, (top + 1.0f + (float) g) / rows, 1.0f / units, 1.0f / rows });
                        }
                    for (int c = 0; c < (int) columns.size(); ++c)
                    {
                        const int st = columns[(size_t) c];
                        const juce::String number = st >= kEff ? "E" + juce::String (st - kEff + 1) : st >= kTape ? juce::String (st - kTape + 1)
                                                  : st >= 16 ? juce::String (17 + 2 * (st - 16)) : juce::String (st + 1);
                        marks.push_back ({ Mark::Box, { (float) c / units, top / rows, 1.0f / units, 1.0f / rows }, number, st == s });
                    }
                }
                break;
            }
            case Function::Pair:
            {
                // As the pictures (V2 MAN pp.184-186): each pair's numbers, its MS
                // (M the odd channel, S the even) and its heart; TAPE over MIC, or
                // AUX over BUS.
                const float rows = 12.0f;
                const auto pairRow = [&] (const juce::String& side, const juce::String& prefix, Yamaha02R::State what, int first, int n, float top, bool ms)
                {
                    const float left = 0.22f, w = (1.0f - left - 0.06f) / (float) (2 * n);
                    marks.push_back ({ Mark::Caption, { 0.0f, (top + 1.5f) / rows, left - 0.02f, 1.0f / rows }, side });
                    marks.push_back ({ Mark::Frame, { left, top / rows, w * (float) (2 * n), (ms ? 4.0f : 3.0f) / rows }, {} });
                    for (int k = 0; k < n; ++k)
                    {
                        const float x = left + w * (float) (2 * k);
                        marks.push_back ({ Mark::Caption, { x, top / rows, w, 1.0f / rows }, juce::String (2 * k + 1) });
                        marks.push_back ({ Mark::Caption, { x + w, top / rows, w, 1.0f / rows }, juce::String (2 * k + 2) });
                        const auto name = prefix + juce::String (2 * k + 1) + "/" + juce::String (2 * k + 2);
                        auto before = fields.size();
                        stateField (name, what, first + k, 0, 1, onOff);
                        if (fields.size() > before)
                        {
                            fields.back().look = Field::Look::Heart;
                            spots.push_back ({ x, (top + (ms ? 2.0f : 1.0f)) / rows, 2.0f * w, 2.0f / rows });
                        }
                        if (! ms)
                            continue;
                        before = fields.size();
                        stateField (name + " MS", Yamaha02R::Ms, first + k, 0, 1, onOff);
                        if (fields.size() > before)
                        {
                            fields.back().look = Field::Look::Button;
                            fields.back().text = [] (int) { return juce::String ("M  S"); };
                            spots.push_back ({ x, (top + 1.0f) / rows, 2.0f * w, 1.0f / rows });
                        }
                    }
                };
                if (page == 0)
                {
                    // MIC first in the fields (pairs 0-7), drawn under TAPE.
                    marks.push_back ({ Mark::Caption, { 0.0f, 0.0f, 1.0f, 1.0f / rows }, "Pair / MS decoding" });
                    pairRow ("Mic", "", Yamaha02R::Pair, 0, 8, 7.0f, true);
                    pairRow ("Tape", "T", Yamaha02R::Pair, 8, 8, 1.5f, true);
                    break;
                }
                marks.push_back ({ Mark::Caption, { 0.0f, 0.0f, 1.0f, 1.0f / rows }, "Aux Pair" });
                pairRow ("Aux", "AUX ", Yamaha02R::AuxPair, 0, 4, 1.5f, false);
                marks.push_back ({ Mark::Caption, { 0.0f, 6.0f / rows, 1.0f, 1.0f / rows }, "Bus Pair" });
                pairRow ("Bus", "BUS ", Yamaha02R::BusPair, 0, 4, 7.5f, false);
                break;
            }
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
                        f.action = [this, n] { confirm (false, "Recall Scene " + juce::String (n + 1) + "?", [this, n] { ctx.show.recallScene (n); }); };
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
                    const auto before = fields.size();
                    if (page == 1)
                        stateField (shortName (strip_), Yamaha02R::RecallSafe, faderIndexOf (strip_), 0, 1, onOff);
                    else
                        stateField (shortName (strip_), Yamaha02R::FadeTime, faderIndexOf (strip_), 0, 100, [] (int v) { return juce::String (v / 10.0, 1); });
                    if (fields.size() > before)
                        fields.back().strip = strip_;
                }
                if (page == 1)
                    layoutRecallSafe();
                else
                    layoutFadeTimes();
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
                {
                    Field f;
                    f.label = bit < 8 ? "BUS " + juce::String (bit + 1) : bit == 8 ? juce::String ("STEREO") : "AUX " + juce::String (bit - 8);
                    f.get = [this, bit] { return (y.getState (Yamaha02R::OscAssign, 0) >> bit) & 1; };
                    f.set = [this, bit] (int v)
                    {
                        const int a = juce::jmax (0, y.getState (Yamaha02R::OscAssign, 0));
                        y.setState (Yamaha02R::OscAssign, 0, v != 0 ? (a | (1 << bit)) : (a & ~(1 << bit)));
                    };
                    f.text = onOff;
                    f.lo = 0;
                    f.hi = 1;
                    fields.push_back (std::move (f));
                }
                layoutUtility();
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
                layoutDigitalIo();
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
                    layoutSetup();
                    break;
                }
                // SETUP's solo (MAN p.160): where the channels and the outputs are heard.
                auto& e = ctx.engine;
                const auto listen = [] (int v) { return juce::String (v != 0 ? "AFTER PAN" : "PRE FADER"); };
                fields.push_back ({ "SOLO CH", [&e] { return e.soloAfl.load() ? 1 : 0; }, [&e] (int v) { e.soloAfl = v != 0; }, listen, 0, 1 });
                fields.push_back ({ "SOLO OUT", [&e] { return e.busSoloAfl.load() ? 1 : 0; }, [&e] (int v) { e.busSoloAfl = v != 0; }, listen, 0, 1 });
                fixed ("SOLO MODE", "MIXDOWN");
                fixed ("SOLO SAFE", "NONE");
                layoutSetup();
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
                        confirm (true, "Store to " + juce::String (nameProgram + 1) + "?", [this, stored, library = nameLibrary, n = nameProgram, target = nameTarget]
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
                                        const char* in[] = { "PRE EQ", "POST EQ", "POST FAD" }; // as p.40 abbreviates them
                                        const char* out[] = { "PRE FAD", "PRE FAD", "POST FAD" };
                                        return juce::String (page == 2 ? out[v] : in[v]);
                                    },
                                    0, 2 });
                // Its choices at the right of the lower meters (V2 MAN pp.41-43), or under the outputs'.
                fields.back().look = Field::Look::Radio;
                spots.push_back (page == 2 ? juce::Rectangle<float> (0.5f, 0.88f, 0.34f, 0.1f) : juce::Rectangle<float> (0.6f, 0.62f, 0.18f, 0.33f));
                break;
            case Function::Aux1:
            case Function::Aux2:
            case Function::Aux3:
            case Function::Aux4:
            case Function::Aux5:
            case Function::Aux6:
            case Function::Aux7:
            case Function::Aux8:
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
        // The function's name and its page as the manual's title bar has them
        // (V2 MAN pp.19, 32-37, 97, 104): a strip's MIC and TAPE halves aren't
        // pages (ATT 1/1, AUX 1 1/1), nor is the selected strip's AUX page;
        // AUX 7/8's effect EDIT is 2/3.
        const char* names[] = { "SCENE", "UTILITY", "MIDI", "GROUP", "PAIR", "\xce\xa6/ATT", "DELAY", "PAN", "ROUTING", "METER", "VIEW",
                                "EQ", "DYNAMICS", "AUX 1", "AUX 2", "AUX 3", "AUX 4", "AUX 5", "AUX 6", "AUX 7", "AUX 8" };
        const char* more[] = { "DIGITAL I/O", "SETUP", "AUTOMIX", "TITLE EDIT" };
        const int f = (int) function;
        const juce::String name = juce::String::fromUTF8 (f < (int) std::size (names) ? names[f] : more[f - (int) std::size (names)]);
        auto counted = [&name] (int at, int of) { return name + "  " + juce::String (at) + "/" + juce::String (of); };
        const bool aux = function >= Function::Aux1 && function <= Function::Aux8;
        const bool effectAux = function >= Function::Aux7 && function <= Function::Aux8;
        if (function == Function::Att || function == Function::Delay || function == Function::Pan)
            return counted (1, 1);
        if (aux)
            return effectAux ? counted (page == 3 ? 2 : 1, 3) : counted (1, 1);
        return numPages() > 1 ? counted (page + 1, numPages()) : name;
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
        // Left and right step through a field's digits first.
        const int n = (int) fields.size();
        const auto& f = fields[(size_t) juce::jlimit (0, n - 1, focus)];
        if ((delta == 1 || delta == -1) && f.digits > 0 && juce::isPositiveAndBelow (digit + delta, f.digits))
        {
            digit += delta;
            repaint();
            return;
        }
        if (! spots.empty() && std::abs (delta) > 1)
        {
            const auto from = fieldBox (focus).getCentre();
            int best = -1;
            float bestCost = 0.0f;
            for (int j = 0; j < n; ++j)
            {
                const auto to = fieldBox (j).getCentre();
                const float dy = (to.y - from.y) * (delta > 0 ? 1.0f : -1.0f);
                if (j == focus || dy < 2.0f)
                    continue;
                const float cost = dy + 3.0f * std::abs (to.x - from.x);
                if (best < 0 || cost < bestCost)
                    best = j, bestCost = cost;
            }
            if (best >= 0)
                focusField (best, delta > 0);
            repaint();
            return;
        }
        focusField ((focus + delta + n) % n, delta > 0);
        repaint();
    }

    void Display::layoutChannels()
    {
        // Eight columns, three rows (V2 MAN pp.33, 36, 107): MIC 1-8 and 9-16 (or
        // TAPE), then LINE 17/18-23/24 two columns each (or EFF 1, EFF 2, STEREO);
        // ruled in pairs.
        for (auto& f : fields)
        {
            f.look = Field::Look::Knob;
            const int st = f.strip;
            const int base = st >= kTape && st < kEff ? st - kTape : st;
            const bool second = ! spots.empty() && fields[spots.size() - 1].strip == st;
            int col = 0, row = 2, span = 2;
            if (base < 16)
                col = base % 8, row = base / 8, span = 1;
            else if (st < kTape)
                col = 2 * (st - 16);
            else if (st < kBus)
                col = 2 * (st - kEff);
            else
                col = 6;
            // A stereo channel's two pans: L, then R in the right column, one number under both.
            if (f.noNumber)
            {
                span = 1;
                if (second)
                    ++col;
                else
                    marks.push_back ({ Mark::Box, { (float) col / 8.0f, (float) (row + 1) / 3.0f - 0.062f, 2.0f / 8.0f, 0.055f }, f.label });
            }
            spots.push_back ({ (float) col / 8.0f, (float) row / 3.0f, (float) span / 8.0f, 1.0f / 3.0f });
        }
        for (int k = 1; k < 4; ++k)
            marks.push_back ({ Mark::Rule, { (float) k / 4.0f, 0.0f, 0.0f, 1.0f }, {} });
        for (int k = 1; k < 3; ++k)
            marks.push_back ({ Mark::Rule, { 0.0f, (float) k / 3.0f, 1.0f, 0.0f }, {} });
        marks.push_back ({ Mark::Frame, { 0.0f, 0.0f, 1.0f, 1.0f }, {} });
    }

    void Display::layoutDelay()
    {
        // Three columns of rows (V2 MAN p.34): 1-8, 9-16 (or TAPE), then the
        // stereo channels two rows each (or EFF); "Smpl mSEC" over each; the
        // unit the wheel moves first.
        constexpr float rows = 9.0f, w = 1.0f / 3.0f;
        for (auto& f : fields)
        {
            if (f.strip < 0)
            {
                spots.push_back ({ 0.0f, 0.0f, w * kRowSamples, 1.0f / rows });
                continue;
            }
            f.look = Field::Look::Row;
            const int st = f.strip;
            const int base = st >= kTape && st < kEff ? st - kTape : st;
            if (base < 16)
                spots.push_back ({ (float) (base / 8) * w, (float) (1 + base % 8) / rows, w, 1.0f / rows });
            else
            {
                const int k = st < kTape ? st - 16 : st - kEff;
                spots.push_back ({ 2.0f * w, (float) (1 + 2 * k) / rows, w, 2.0f / rows });
            }
        }
        if (! fields.empty())
            fields[0].look = Field::Look::Button;
        for (int c = 0; c < 3; ++c)
        {
            marks.push_back ({ Mark::Caption, { (c + kRowSamples) * w, 0.0f, (kRowMs - kRowSamples) * w, 1.0f / rows }, "Smpl" });
            marks.push_back ({ Mark::Caption, { (c + kRowMs) * w, 0.0f, (kRowOn - kRowMs) * w, 1.0f / rows }, "mSEC" });
        }
        for (int k = 1; k < 3; ++k)
            marks.push_back ({ Mark::Rule, { (float) k * w, 0.0f, 0.0f, 1.0f }, {} });
        for (int k = 1; k < 5; ++k)
            marks.push_back ({ Mark::Rule, { 0.0f, (1.0f + 2.0f * (float) k) / rows, 1.0f, 0.0f }, {} });
        marks.push_back ({ Mark::Rule, { 0.0f, 1.0f / rows, 1.0f, 0.0f }, {} });
        marks.push_back ({ Mark::Frame, { 0.0f, 0.0f, 1.0f, 1.0f }, {} });
    }

    void Display::layoutRecallSafe()
    {
        // ENABLE and STEREO side by side; TAPE 1-16, MIC 1-16, LINE 17/18-23/24
        // and EFF 1-2, each framed; a channel's number lit when it's safe.
        constexpr float rows = 12.0f;
        const auto frame = [this] (const juce::String& name, float x, float top, float w)
        {
            marks.push_back ({ Mark::Frame, { x, top / rows, w, 1.6f / rows }, name });
        };
        marks.push_back ({ Mark::Caption, { 0.0f, 0.0f, 1.0f, 1.0f / rows }, "Fader Recall Safe" });
        frame ("Enable/Disable", 0.02f, 1.5f, 0.36f);
        frame ("Stereo Mas. Recall Safe", 0.44f, 1.5f, 0.54f);
        frame ("Tape Recall Safe", 0.02f, 4.0f, 0.96f);
        frame ("Mic Recall Safe", 0.02f, 6.5f, 0.96f);
        frame ("Line Recall Safe", 0.02f, 9.0f, 0.5f);
        frame ("Eff. Recall Safe", 0.58f, 9.0f, 0.4f);
        const float cell = 0.94f / 16.0f, h = 1.0f / rows;
        for (auto& f : fields)
        {
            f.look = Field::Look::Button;
            const int st = f.strip;
            juce::String name;
            juce::Rectangle<float> at;
            if (st < 0)
                name = "ENABLE", at = { 0.12f, 1.8f / rows, 0.16f, h };
            else if (st == kStereo)
                name = "STEREO", at = { 0.63f, 1.8f / rows, 0.16f, h };
            else if (st < 16 || (st >= kTape && st < kEff))
            {
                const int k = st < 16 ? st : st - kTape;
                name = juce::String (k + 1);
                at = { 0.03f + cell * (float) k, (st < 16 ? 6.8f : 4.3f) / rows, cell, h };
            }
            else if (st < kTape)
                name = juce::String (17 + 2 * (st - 16)) + "/" + juce::String (18 + 2 * (st - 16)), at = { 0.04f + 0.115f * (float) (st - 16), 9.3f / rows, 0.11f, h };
            else
                name = juce::String (st - kEff + 1), at = { 0.66f + 0.2f * (float) (st - kEff), 9.3f / rows, 0.06f, h };
            f.text = [name] (int) { return name; };
            spots.push_back (at);
        }
    }

    void Display::layoutFadeTimes()
    {
        // Three columns (V2 MAN p.136): MIC 1-8 beside 9-16, TAPE likewise, then
        // LINE 17/18-23/24 two rows each beside EFF 1, EFF 2 and ST; seconds.
        constexpr float rows = 10.0f, third = 1.0f / 3.0f, half = third / 2.0f;
        marks.push_back ({ Mark::Caption, { 0.0f, 0.0f, 1.0f, 1.0f / rows }, "Fade Time" });
        marks.push_back ({ Mark::Caption, { 2.0f * third, 0.0f, third, 1.0f / rows }, "[SEC]" });
        const char* heads[] = { "Mic", "Tape", "Line/Rtn/ST" };
        for (int c = 0; c < 3; ++c)
            marks.push_back ({ Mark::Caption, { c * third, 1.0f / rows, third, 1.0f / rows }, heads[c] });
        for (auto& f : fields)
        {
            f.look = Field::Look::Row;
            const int st = f.strip;
            if (st < 16 || (st >= kTape && st < kEff))
            {
                const int k = st < 16 ? st : st - kTape;
                f.label = juce::String (k + 1);
                spots.push_back ({ (st < 16 ? 0.0f : third) + (float) (k / 8) * half, (float) (2 + k % 8) / rows, half, 1.0f / rows });
            }
            else if (st < kTape)
            {
                f.label = juce::String (17 + 2 * (st - 16)) + "/" + juce::String (18 + 2 * (st - 16));
                spots.push_back ({ 2.0f * third, (float) (2 + 2 * (st - 16)) / rows, half, 2.0f / rows });
            }
            else
            {
                const int k = st == kStereo ? 3 : st - kEff;
                f.label = st == kStereo ? juce::String ("ST") : "E" + juce::String (k + 1);
                spots.push_back ({ 2.0f * third + half, (float) (2 + 2 * k) / rows, half, 2.0f / rows });
            }
        }
        for (int k = 1; k < 3; ++k)
            marks.push_back ({ Mark::Rule, { (float) k * third, 1.0f / rows, 0.0f, 1.0f - 1.0f / rows }, {} });
        for (int k = 0; k < 4; ++k)
            marks.push_back ({ Mark::Rule, { 0.0f, (2.0f + 2.0f * (float) k) / rows, 1.0f, 0.0f }, {} });
        marks.push_back ({ Mark::Frame, { 0.0f, 1.0f / rows, 1.0f, 1.0f - 1.0f / rows }, {} });
    }

    void Display::layoutEq()
    {
        // The LIBRARY and STORE at the top left; ATT and EQ ON at the bottom left;
        // a frame per band, its Q, F and G knobs down it (Q F G at the left), HPF
        // in LOW's and LPF in HIGH's top corner.
        const char* bands[] = { "LOW", "L-MID", "H-MID", "HIGH" };
        constexpr float top = 0.44f, rowH = 0.165f, left = 0.2f, bandW = 0.2f;
        for (auto& f : fields)
        {
            juce::Rectangle<float> at;
            if (f.label == "LIBRARY" || f.label == "STORE")
                at = { 0.0f, f.label == "LIBRARY" ? 0.02f : 0.21f, 0.19f, 0.17f };
            else if (f.label == "EQ")
            {
                f.look = Field::Look::Button;
                at = { 0.04f, 0.83f, 0.1f, 0.08f };
            }
            else if (f.label == "ATT")
            {
                f.look = Field::Look::Knob;
                at = { 0.02f, top + 0.02f, 0.14f, 0.34f };
            }
            else if (f.label == "HPF" || f.label == "LPF")
            {
                f.look = Field::Look::Button;
                f.text = [name = f.label] (int) { return name; };
                at = { left + bandW * (f.label == "HPF" ? 0.0f : 3.0f) + bandW * 0.7f, top + 3.0f * rowH - 0.075f, bandW * 0.27f, 0.065f };
            }
            else
            {
                int which = 0;
                for (int k = 0; k < 4; ++k)
                    if (f.label.startsWith (juce::String (bands[k]) + " "))
                        which = k;
                const int row = f.label.endsWith ("Q") ? 0 : f.label.endsWith ("F") ? 1 : 2;
                f.look = Field::Look::Knob;
                f.noNumber = true;
                at = { left + bandW * (float) which, top + rowH * (float) row, bandW, rowH };
            }
            spots.push_back (at);
        }
        for (int k = 0; k < 4; ++k)
            marks.push_back ({ Mark::Frame, { left + bandW * (float) k + 0.004f, top, bandW - 0.008f, 1.0f - top - 0.01f }, bands[k] });
        const char* rowNames[] = { "Q", "F", "G" };
        for (int r = 0; r < 3; ++r)
            marks.push_back ({ Mark::Caption, { left - 0.03f, top + rowH * (float) r, 0.03f, rowH * 0.6f }, rowNames[r] });
        marks.push_back ({ Mark::Caption, { 0.02f, 0.91f, 0.14f, 0.06f }, "EQ ON" });
        marks.push_back ({ Mark::Frame, { 0.0f, top, left - 0.035f, 1.0f - top - 0.01f }, {} });
    }

    void Display::layoutDynamics()
    {
        // The LIBRARY and STORE at the top left; ON, TYPE, KEY IN, ST.LINK and IN
        // in a frame beside them; the processor's knobs along the bottom, framed
        // with its type's name.
        const int type = juce::jlimit (0, 5, y.get (address (strip, kDynType)));
        int row = 0, knob = 0;
        int knobs = 0;
        for (const auto& f : fields)
            if (f.label != "LIBRARY" && f.label != "STORE" && f.label != "DYNAMICS" && f.label != "TYPE" && f.label != "KEY IN" && f.label != "ST.LINK" && f.label != "IN")
                ++knobs;
        for (auto& f : fields)
        {
            juce::Rectangle<float> at;
            if (f.label == "LIBRARY" || f.label == "STORE")
                at = { 0.0f, f.label == "LIBRARY" ? 0.02f : 0.21f, 0.19f, 0.17f };
            else if (f.label == "DYNAMICS" || f.label == "TYPE" || f.label == "KEY IN" || f.label == "ST.LINK" || f.label == "IN")
            {
                f.look = Field::Look::Row;
                at = { 0.21f, 0.03f + 0.07f * (float) row++, 0.27f, 0.07f };
            }
            else
            {
                f.look = Field::Look::Knob;
                at = { (float) knob++ / (float) juce::jmax (1, knobs), 0.46f, 1.0f / (float) juce::jmax (1, knobs), 0.52f };
            }
            spots.push_back (at);
        }
        marks.push_back ({ Mark::Frame, { 0.205f, 0.02f, 0.28f, 0.36f }, {} });
        marks.push_back ({ Mark::Frame, { 0.0f, 0.44f, 1.0f, 0.55f }, kDynNames[type] });
    }

    void Display::layoutSetup()
    {
        if (page == 0)
        {
            // Solo (V2 MAN p.196): Status (Sidecar mixes down), Listen, the outputs'
            // listen; the solo safes aren't Sidecar's.
            marks.push_back ({ Mark::Caption, { 0.0f, 0.0f, 1.0f, 0.08f }, "Solo" });
            const juce::Rectangle<float> frames[] = { { 0.02f, 0.14f, 0.46f, 0.12f }, { 0.52f, 0.14f, 0.46f, 0.12f }, { 0.02f, 0.36f, 0.46f, 0.12f } };
            const char* names[] = { "Listen", "Out Listen", "Status" };
            for (int k = 0; k < 3; ++k)
                marks.push_back ({ Mark::Frame, frames[k], names[k] });
            marks.push_back ({ Mark::Frame, { 0.02f, 0.58f, 0.96f, 0.36f }, "Solo Safe" });
            marks.push_back ({ Mark::Caption, { 0.02f, 0.68f, 0.96f, 0.16f }, "Not emulated: every channel's solo is heard" });
            for (size_t k = 0; k < fields.size(); ++k)
            {
                auto& f = fields[k];
                if (k < 2)
                {
                    f.look = Field::Look::Choices;
                    f.choices = { "Pre Fader", "After Pan" };
                    spots.push_back (frames[k].reduced (0.01f, 0.02f));
                }
                else if (k == 2)
                {
                    f.look = Field::Look::Choices;
                    f.choices = { "Recording", "Mixdown" };
                    f.get = [] { return 1; };
                    f.lo = f.hi = 0;
                    spots.push_back (frames[2].reduced (0.01f, 0.02f));
                }
                else
                    spots.push_back ({ 0.0f, 0.0f, 0.0f, 0.0f }); // SOLO SAFE: the caption says it
            }
            return;
        }
        // PREFERENCES and AUX OUTPUT SELECT: a check box a line, two columns.
        const char* titles[] = { "", "Aux Output Select", "Preferences 1", "Preferences 2" };
        marks.push_back ({ Mark::Caption, { 0.0f, 0.0f, 1.0f, 0.08f }, titles[juce::jlimit (0, 3, page)] });
        int row = 0;
        for (auto& f : fields)
        {
            const bool fixedOne = f.lo == f.hi;
            const int col = row / 6, line = row % 6;
            if (fixedOne)
            {
                // A fixed value: its line as words, not a box to tick.
                f.look = Field::Look::Row;
                spots.push_back ({ 0.02f + 0.5f * (float) col, 0.12f + 0.13f * (float) line, 0.46f, 0.12f });
            }
            else
            {
                f.look = Field::Look::Check;
                spots.push_back ({ 0.02f + 0.5f * (float) col, 0.12f + 0.13f * (float) line, 0.46f, 0.12f });
            }
            ++row;
        }
        marks.push_back ({ Mark::Frame, { 0.0f, 0.1f, 1.0f, 0.88f }, {} });
    }

    void Display::layoutUtility()
    {
        // Oscillator (V2 MAN p.209): On/Off, Level, Wave Form, BUS, Stereo and Aux Assign, framed.
        marks.push_back ({ Mark::Caption, { 0.0f, 0.0f, 1.0f, 0.08f }, "Oscillator" });
        marks.push_back ({ Mark::Frame, { 0.04f, 0.14f, 0.2f, 0.16f }, "On/Off" });
        marks.push_back ({ Mark::Frame, { 0.34f, 0.14f, 0.24f, 0.16f }, "Level" });
        marks.push_back ({ Mark::Frame, { 0.04f, 0.38f, 0.8f, 0.12f }, "Wave Form" });
        marks.push_back ({ Mark::Frame, { 0.04f, 0.58f, 0.46f, 0.12f }, "BUS Assign" });
        marks.push_back ({ Mark::Frame, { 0.54f, 0.58f, 0.3f, 0.12f }, "Stereo Assign" });
        marks.push_back ({ Mark::Frame, { 0.04f, 0.8f, 0.46f, 0.12f }, "Aux Assign" });
        for (size_t k = 0; k < fields.size(); ++k)
        {
            auto& f = fields[k];
            if (k == 0)
            {
                f.look = Field::Look::Button;
                spots.push_back ({ 0.08f, 0.17f, 0.12f, 0.1f });
            }
            else if (k == 1)
            {
                f.look = Field::Look::Row;
                spots.push_back ({ 0.35f, 0.16f, 0.22f, 0.12f });
            }
            else if (k == 2)
            {
                f.look = Field::Look::Radio;
                spots.push_back ({ 0.05f, 0.39f, 0.78f, 0.1f });
            }
            else
            {
                const int bit = (int) k - 3;
                f.look = Field::Look::Cell;
                if (bit < 8)
                    f.label = juce::String (bit + 1), spots.push_back ({ 0.06f + 0.055f * (float) bit, 0.6f, 0.05f, 0.08f });
                else if (bit == 8)
                    spots.push_back ({ 0.62f, 0.6f, 0.14f, 0.08f });
                else
                    f.label = juce::String (bit - 8), spots.push_back ({ 0.06f + 0.055f * (float) (bit - 9), 0.82f, 0.05f, 0.08f });
            }
        }
    }

    void Display::layoutView()
    {
        for (auto& f : fields)
        {
            juce::Rectangle<float> at;
            const auto& l = f.label;
            if (l == "ATT")
                f.look = Field::Look::Knob, f.noNumber = false, at = { 0.0f, 0.1f, 0.14f, 0.34f };
            else if (l.endsWith ("PHASE"))
            {
                f.look = Field::Look::Button;
                f.text = [] (int v) { return juce::String (v != 0 ? "REV" : "NOR"); };
                at = { 0.03f, 0.0f, 0.08f, 0.09f };
            }
            else if (l == "PAN" || l == "BALANCE" || l == "PAN L")
                f.look = Field::Look::Knob, at = { 0.0f, 0.46f, l == "PAN L" ? 0.07f : 0.14f, 0.3f };
            else if (l == "PAN R")
                f.look = Field::Look::Knob, at = { 0.07f, 0.46f, 0.07f, 0.3f };
            else if (l == "GANG" || l == "MS")
                f.look = Field::Look::Check, at = { 0.0f, l == "GANG" ? 0.78f : 0.88f, 0.15f, 0.1f };
            else if (l == "FADER")
                f.look = Field::Look::Row, at = { 0.16f, 0.55f, 0.18f, 0.1f };
            else if (l == "ON")
                f.look = Field::Look::Button, at = { 0.2f, 0.66f, 0.1f, 0.09f };
            else if (l == "FADER GRP" || l == "MUTE GRP")
            {
                at = { 0.16f, l == "FADER GRP" ? 0.77f : 0.88f, 0.18f, 0.1f };
                f.look = Field::Look::Row;
                f.label = l == "FADER GRP" ? "F GRP" : "M GRP"; // the picture's F and M
            }
            else if (l == "LIB")
            {
                f.look = Field::Look::Button;
                f.text = [] (int) { return juce::String ("LIB"); };
                at = { 0.86f, 0.84f, 0.12f, 0.12f };
            }
            spots.push_back (at);
        }
        marks.push_back ({ Mark::Caption, { 0.67f, 0.0f, 0.32f, 0.06f }, "EQ" });
        marks.push_back ({ Mark::Caption, { 0.67f, 0.41f, 0.32f, 0.06f }, "DYNAMICS" });
        for (float x : { 0.15f, 0.35f, 0.655f })
            marks.push_back ({ Mark::Rule, { x, 0.0f, 0.0f, 1.0f }, {} });
        marks.push_back ({ Mark::Frame, { 0.0f, 0.0f, 1.0f, 1.0f }, {} });
    }

    void Display::paintViewSends (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        // AUX 1-8: each send's knob, PRE or POST, its name (two columns of four).
        const int m = memberOf (strip);
        if (m < 0)
            return;
        for (int aux = 0; aux < 8; ++aux)
        {
            const auto cell = juce::Rectangle<float> (r.getX() + r.getWidth() * 0.5f * (float) (aux % 2), r.getY() + r.getHeight() * 0.25f * (float) (aux / 2),
                                                      r.getWidth() * 0.5f, r.getHeight() * 0.25f).reduced (2.0f);
            auto c = cell;
            g.setColour (kPixel);
            g.setFont (type::font (kSmall, true));
            g.drawText ("AUX" + juce::String (aux + 1), c.removeFromBottom (12.0f), juce::Justification::centred);
            const bool pre = ((y.get (sendAddress (m, 9)) >> aux) & 1) != 0, on = ((y.get (sendAddress (m, 8)) >> aux) & 1) != 0;
            chip (g, fitted (c.removeFromBottom (12.0f), "POST"), pre ? "PRE" : "POST", pre);
            knob (g, c.reduced (2.0f), (float) juce::jmax (0, y.get (sendAddress (m, aux))) / 127.0f);
            if (! on)
                g.drawText ("OFF", c.removeFromRight (c.getWidth() * 0.3f), juce::Justification::centred);
        }
    }

    void Display::layoutDigitalIo()
    {
        // Word clock and inputs (V2 MAN pp.188-190): Sidecar's are its audio
        // interface's, so each is a line of words, framed.
        marks.push_back ({ Mark::Caption, { 0.0f, 0.0f, 1.0f, 0.08f }, "Word Clock Select" });
        int row = 0;
        for (auto& f : fields)
        {
            f.look = Field::Look::Row;
            spots.push_back ({ 0.04f + 0.48f * (float) (row / 4), 0.14f + 0.18f * (float) (row % 4), 0.44f, 0.15f });
            ++row;
        }
        marks.push_back ({ Mark::Frame, { 0.0f, 0.1f, 1.0f, 0.86f }, {} });
    }

    void Display::paintChannelMeter (juce::Graphics& g, juce::Rectangle<float> r, bool reduction) const
    {
        auto& e = ctx.engine;
        auto pick = [] (const MixerEngine::StripList& l, int k) -> Strip* { return juce::isPositiveAndBelow (k, (int) l.size()) ? l[(size_t) k].get() : nullptr; };
        const int s = strip;
        Strip* st = s < kEff ? pick (e.getChannels(), s) : s < kBus ? pick (e.getAux(), s - kEff) : s < kStereo ? pick (e.getBuses(), s - kBus) : pick (e.getMains(), 0);
        const bool stereo = isStereoStrip (s) || s == kStereo;
        g.setFont (type::font (8.5f, true));
        const float marksDb[] = { 0.0f, -6.0f, -12.0f, -18.0f, -30.0f, -48.0f };
        const char* names[] = { "CLIP", "-6", "-12", "-18", "-30", "-48" };
        const auto scale = r.reduced (0.0f, 4.0f).withTrimmedBottom (10.0f);
        const auto yOf = [&scale] (float db) { return scale.getY() + scale.getHeight() * (-db / 48.0f); };
        const auto bars = scale.withTrimmedRight (30.0f).withTrimmedLeft (reduction ? 30.0f : 4.0f);
        for (int k = 0; k < 6; ++k)
        {
            const float yy = yOf (marksDb[k]);
            g.setColour (kPixel);
            g.drawText (names[k], juce::Rectangle<float> (scale.getRight() - 28.0f, yy - 5.0f, 28.0f, 10.0f), juce::Justification::centredRight);
            g.setColour (kPixelDim);
            for (float x = bars.getX(); x < bars.getRight(); x += 4.0f)
                g.fillRect (x, yy, 1.0f, 1.0f);
        }
        const int n = stereo ? 2 : 1;
        const float w = bars.getWidth() / (float) n;
        for (int c = 0; c < n; ++c)
        {
            const float db = st != nullptr ? juce::Decibels::gainToDecibels (ctx.meters.get (st, MeterPoint::PreFader, c), -48.0f) : -48.0f;
            const auto bar = juce::Rectangle<float> (bars.getX() + w * (float) c, scale.getY(), w, scale.getHeight()).withSizeKeepingCentre (juce::jmin (10.0f, w * 0.5f), scale.getHeight());
            g.setColour (kPixel);
            g.drawRect (bar, 0.5f);
            g.fillRect (bar.withTop (yOf (juce::jmax (-48.0f, db))));
            g.drawText (juce::String (c + 1), bar.withY (scale.getBottom()).withHeight (10.0f).expanded (6.0f, 0.0f), juce::Justification::centred);
        }
        if (reduction)
        {
            // GR: down from the top, 0 to -18 dB.
            const float gr = st != nullptr ? std::abs (st->meters.dynReductionDb.load()) : 0.0f;
            const auto bar = juce::Rectangle<float> (scale.getX() + 8.0f, scale.getY(), 10.0f, scale.getHeight());
            g.setColour (kPixel);
            g.drawRect (bar, 0.5f);
            g.fillRect (bar.withHeight (bar.getHeight() * juce::jlimit (0.0f, 1.0f, gr / 18.0f)));
            g.drawText ("GR", bar.withY (scale.getBottom()).withHeight (10.0f).expanded (6.0f, 0.0f), juce::Justification::centred);
        }
    }

    void Display::paintTransfer (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto box = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight()));
        g.setColour (kPixel);
        g.drawRect (box, 1.0f);
        const int type = juce::jlimit (0, 5, y.get (address (strip, kDynType)));
        const int model = models::findModel (models::Dynamics, kDynTokens[type]);
        if (model < 0)
            return;
        const auto& m = models::model (models::Dynamics, model);
        std::vector<float> values;
        for (int k = 0; k < (int) m.params.size(); ++k)
            values.push_back (stepValue (m.params[(size_t) k], juce::jmax (0, k < 6 ? y.get (address (strip, kDynParam + k)) : 0)));
        constexpr int n = 48;
        const auto curve = transferCurve (model, values.data(), n);
        if ((int) curve.size() < n)
            return;
        const float range = transferRangeDb (model);
        juce::Path p;
        for (int k = 0; k < n; ++k)
        {
            const float x = box.getX() + box.getWidth() * (float) k / (float) (n - 1);
            const float yy = box.getBottom() - box.getHeight() * (juce::jlimit (-range, 0.0f, curve[(size_t) k]) + range) / range;
            k == 0 ? p.startNewSubPath (x, yy) : p.lineTo (x, yy);
        }
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    void Display::focusField (int i, bool firstDigit)
    {
        focus = i;
        const int d = juce::isPositiveAndBelow (i, (int) fields.size()) ? fields[(size_t) i].digits : 0;
        digit = d > 0 ? (firstDigit ? 0 : d - 1) : 0;
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
        if (f.digits > 0)
            for (int k = digit; k < f.digits - 1; ++k)
                delta *= 10;
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

    void Display::enterAll()
    {
        if ((function != Function::Att && function != Function::Delay) || ! juce::isPositiveAndBelow (focus, (int) fields.size()))
            return;
        const auto& from = fields[(size_t) focus];
        if (from.strip < 0 || from.strip == kStereo || ! from.get)
            return;
        const int v = from.get();
        const int bit = function == Function::Att ? kPhase : kDelayOn;
        const bool on = ((y.get (address (from.strip, kSwitches)) >> bit) & 1) != 0;
        // Both pages' channels: MIC/LINE, TAPE and the effect returns.
        for (int st = 0; st < kBus; ++st)
        {
            if (function == Function::Att)
                y.set (address (st, kAtt), v);
            else
            {
                y.set (address (st, kDelayHi), v >> 7);
                y.set (address (st, kDelayLo), v & 0x7f);
            }
            const int sw = address (st, kSwitches), byte = juce::jmax (0, y.get (sw));
            y.set (sw, on ? (byte | (1 << bit)) : (byte & ~(1 << bit)));
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

    juce::Rectangle<float> Display::pageArea() const
    {
        return getLocalBounds().toFloat().reduced (14.0f).withTrimmedTop (30.0f).withTrimmedBottom (26.0f);
    }

    juce::Rectangle<float> Display::toPage (juce::Rectangle<float> unit) const
    {
        const auto a = pageArea();
        return { a.getX() + unit.getX() * a.getWidth(), a.getY() + unit.getY() * a.getHeight(), unit.getWidth() * a.getWidth(), unit.getHeight() * a.getHeight() };
    }

    juce::Rectangle<float> Display::fieldBox (int i) const
    {
        if (juce::isPositiveAndBelow (i, (int) spots.size()))
            return toPage (spots[(size_t) i]).reduced (1.0f);
        auto area = pageArea();
        const bool graph = surroundGraph();
        if (graph)
            area = area.withTrimmedRight (area.getHeight() + 20.0f); // the square pan graph at the right
        const int n = (int) fields.size();
        const int cols = graph ? 3 : function == Function::Eq || n <= 16 ? 4 : n <= 24 ? 6 : 8; // EQ beside its curve: four
        const int rows = juce::jmax (4, (n + cols - 1) / cols);
        const float w = area.getWidth() / cols, h = juce::jmin (52.0f, area.getHeight() / (float) rows);
        return { area.getX() + w * (float) (i % cols), area.getY() + h * (float) (i / cols), w - 6.0f, h - 6.0f };
    }

    namespace
    {
        constexpr float kTagH = 12.0f, kNumberH = 13.0f, kValueH = 12.0f;

        /** A word in a box: lit (inverted) or outlined. */
        void chip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool lit)
        {
            if (lit)
            {
                g.setColour (kPixel);
                g.fillRect (r);
            }
            else
            {
                g.setColour (kPixel);
                g.drawRect (r, 1.0f);
            }
            g.setColour (lit ? kLcd : kPixel);
            g.setFont (type::font (kSmall, true));
            g.drawFittedText (text, r.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }

        /** A box as wide as its word (and a margin), centred in `r`. */
        juce::Rectangle<float> fitted (juce::Rectangle<float> r, const juce::String& text, float margin)
        {
            const float w = juce::GlyphArrangement::getStringWidth (type::font (kSmall, true), text) + margin;
            return r.withSizeKeepingCentre (juce::jmin (r.getWidth(), juce::jmax (w, r.getHeight() + 2.0f)), r.getHeight());
        }

        /** A rotary knob: its circle and the pointer at `frac` of its travel (7 o'clock to 5). */
        void knob (juce::Graphics& g, juce::Rectangle<float> r, float frac, float thickness)
        {
            const float d = juce::jmin (r.getWidth(), r.getHeight(), 30.0f);
            const auto c = r.getCentre();
            g.setColour (kPixel);
            g.drawEllipse (juce::Rectangle<float> (d, d).withCentre (c), thickness);
            const float a = (-0.75f + 1.5f * juce::jlimit (0.0f, 1.0f, frac)) * juce::MathConstants<float>::pi;
            g.drawLine (c.x, c.y, c.x + std::sin (a) * d * 0.5f, c.y - std::cos (a) * d * 0.5f, thickness + 0.4f);
        }
    } // namespace

    juce::Rectangle<float> Display::valueBox (int i) const
    {
        const auto& f = fields[(size_t) i];
        auto r = fieldBox (i);
        switch (f.look)
        {
            case Field::Look::Knob:
            {
                if (! f.noNumber || f.strip >= 0) // a stereo channel's pans keep the row for their shared number
                    r.removeFromBottom (kNumberH);
                if (f.tag && ! f.tagAbove)
                    r.removeFromBottom (kTagH);
                return r.removeFromBottom (kValueH);
            }
            case Field::Look::Row:
            {
                const float w = r.getWidth();
                r = r.withHeight (juce::jmin (r.getHeight(), 22.0f));
                const auto at = [&r, w] (float from, float to) { return r.withX (r.getX() + from * w).withWidth ((to - from) * w); };
                if (! f.note && ! f.tag)
                    return at (kRowCompact, 1.0f); // a number and its value (FADE TIME's)
                return f.noteFirst ? at (kRowMs, kRowOn) : at (kRowSamples, kRowMs);
            }
            case Field::Look::Tile:
                return r.withTrimmedTop (r.getHeight() * 0.4f);
            case Field::Look::Cell:
            case Field::Look::Button:
            case Field::Look::Heart:
            case Field::Look::Radio:
            case Field::Look::Check:
            case Field::Look::Choices:
                break;
        }
        return r;
    }

    void Display::paintField (juce::Graphics& g, int i) const
    {
        const auto& f = fields[(size_t) i];
        const auto box = fieldBox (i);
        const bool focused = i == focus;
        const int v = f.pending >= 0 ? f.pending : f.get ? f.get() : 0;
        const auto text = f.text ? f.text (v) : juce::String (v);
        const auto value = valueBox (i);
        bool invertValue = false;
        switch (f.look)
        {
            case Field::Look::Tile:
                g.setColour (focused ? kPixel : kLcdFrame);
                g.fillRoundedRectangle (box, 3.0f);
                g.setColour (focused ? kLcd : kPixelDim);
                g.setFont (type::font (type::kCaption, true));
                g.drawText (f.label, box.withHeight (box.getHeight() * 0.42f), juce::Justification::centred);
                invertValue = focused;
                break;
            case Field::Look::Knob:
            {
                // Its switch, knob, value and number (the number lit under the cursor).
                auto r = box;
                if (f.tag && f.tagAbove)
                    chip (g, fitted (r.removeFromTop (kTagH), "REV"), f.tag(), f.tagOn && f.tagOn());
                if (! f.noNumber)
                    chip (g, fitted (r.removeFromBottom (kNumberH).reduced (0.0f, 1.0f), f.label), f.label, focused);
                else if (f.strip >= 0)
                    r.removeFromBottom (kNumberH);
                if (f.tag && ! f.tagAbove)
                    chip (g, fitted (r.removeFromBottom (kTagH).reduced (0.0f, 1.0f), "POST"), f.tag(), f.tagOn && f.tagOn());
                r.removeFromBottom (kValueH);
                const float span = (float) juce::jmax (1, f.hi - f.lo);
                knob (g, r.reduced (0.0f, 2.0f), (float) (v - f.lo) / span);
                invertValue = focused && f.noNumber;
                break;
            }
            case Field::Look::Row:
            {
                // Its number (a stereo pair's as tall as its two rows), the two readings, ON.
                const float w = box.getWidth();
                const auto line = box.withHeight (juce::jmin (box.getHeight(), 22.0f));
                const auto at = [&line, w] (float from, float to) { return line.withX (line.getX() + from * w).withWidth ((to - from) * w); };
                auto number = box.withWidth (w * (! f.note && ! f.tag ? kRowCompact : kRowSamples)).reduced (3.0f, 3.0f);
                number = number.withSizeKeepingCentre (juce::jmin (number.getWidth(), juce::GlyphArrangement::getStringWidth (type::font (kSmall, true), f.label) + 8.0f),
                                                        juce::jmin (number.getHeight(), box.getHeight() > 30.0f ? 34.0f : 15.0f))
                             .withX (number.getX());
                chip (g, number, f.label, focused);
                if (f.tag)
                    chip (g, at (kRowOn, 1.0f).reduced (3.0f, 4.0f), f.tag(), f.tagOn && f.tagOn());
                if (f.note)
                {
                    g.setColour (kPixelDim);
                    g.setFont (type::font (kSmall + 1.0f, true));
                    g.drawText (f.note(), f.noteFirst ? at (kRowSamples, kRowMs) : at (kRowMs, kRowOn), juce::Justification::centred);
                }
                break;
            }
            case Field::Look::Heart:
            {
                // Whole (filled) when paired, broken when not.
                const float d = juce::jmin (box.getWidth() * 0.6f, box.getHeight() * 0.9f);
                const auto h = juce::Rectangle<float> (d, d * 0.9f).withCentre (box.getCentre());
                juce::Path p;
                p.startNewSubPath (h.getCentreX(), h.getBottom());
                p.cubicTo (h.getX() - d * 0.1f, h.getY() + d * 0.5f, h.getX() + d * 0.05f, h.getY() - d * 0.1f, h.getCentreX(), h.getY() + d * 0.22f);
                p.cubicTo (h.getRight() - d * 0.05f, h.getY() - d * 0.1f, h.getRight() + d * 0.1f, h.getY() + d * 0.5f, h.getCentreX(), h.getBottom());
                p.closeSubPath();
                g.setColour (kPixel);
                g.fillPath (p);
                if (v == 0)
                {
                    // The crack down its middle.
                    juce::Path crack;
                    const float cx = h.getCentreX();
                    crack.startNewSubPath (cx, h.getY() + d * 0.2f);
                    crack.lineTo (cx - d * 0.12f, h.getY() + d * 0.4f);
                    crack.lineTo (cx + d * 0.1f, h.getY() + d * 0.58f);
                    crack.lineTo (cx - d * 0.06f, h.getY() + d * 0.75f);
                    crack.lineTo (cx, h.getBottom());
                    g.setColour (kLcd);
                    g.strokePath (crack, juce::PathStrokeType (2.5f));
                }
                if (focused)
                {
                    g.setColour (kPixel);
                    g.drawRect (box.reduced (1.0f), 1.5f);
                }
                return;
            }
            case Field::Look::Check:
            case Field::Look::Choices:
            {
                // A box crossed when on (or the chosen one of a row of them), its name after it.
                const auto mark = [&g] (juce::Rectangle<float> r, const juce::String& name, bool on, bool lit)
                {
                    const auto b = juce::Rectangle<float> (r.getX() + 2.0f, r.getCentreY() - 5.0f, 10.0f, 10.0f);
                    g.setColour (kPixel);
                    g.drawRect (b, 1.0f);
                    if (on)
                    {
                        g.drawLine (b.getX() + 2.0f, b.getY() + 2.0f, b.getRight() - 2.0f, b.getBottom() - 2.0f, 1.4f);
                        g.drawLine (b.getRight() - 2.0f, b.getY() + 2.0f, b.getX() + 2.0f, b.getBottom() - 2.0f, 1.4f);
                    }
                    g.setFont (type::font (kSmall + 1.0f, true));
                    const auto t = r.withTrimmedLeft (16.0f).withWidth (juce::jmin (r.getWidth() - 16.0f, juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + 6.0f));
                    if (lit)
                    {
                        g.fillRect (t);
                        g.setColour (kLcd);
                    }
                    g.drawText (name, t, juce::Justification::centred);
                };
                if (f.look == Field::Look::Check)
                    mark (box, f.label, v != 0, focused);
                else
                {
                    const int n = juce::jmax (1, f.choices.size());
                    for (int k = 0; k < n; ++k)
                        mark (box.withWidth (box.getWidth() / (float) n).translated (box.getWidth() / (float) n * (float) k, 0.0f), f.choices[k], v - f.lo == k, focused && v - f.lo == k);
                }
                return;
            }
            case Field::Look::Radio:
            {
                // Each different choice a box, in a column (or a row, when wide); the chosen lit.
                juce::StringArray choices;
                for (int k = f.lo; k <= f.hi; ++k)
                    choices.addIfNotAlreadyThere (f.text (k));
                const bool across = box.getWidth() > box.getHeight() * 3.0f;
                const int n = choices.size();
                for (int k = 0; k < n; ++k)
                {
                    const auto r = across ? box.withWidth (box.getWidth() / (float) n).translated (box.getWidth() / (float) n * (float) k, 0.0f)
                                          : box.withHeight (box.getHeight() / (float) n).translated (0.0f, box.getHeight() / (float) n * (float) k);
                    chip (g, r.reduced (3.0f, 2.0f), choices[k], choices[k] == text);
                }
                if (focused)
                {
                    g.setColour (kPixel);
                    g.drawRect (box.expanded (1.0f), 1.5f);
                }
                return;
            }
            case Field::Look::Cell:
            case Field::Look::Button:
            {
                // A box lit when on; the cursor's a double line round it.
                const bool on = v != 0;
                chip (g, box.reduced (1.0f), f.look == Field::Look::Cell ? f.label : text, on);
                if (focused)
                {
                    g.setColour (kPixel);
                    g.drawRect (box.expanded (1.0f), 1.5f);
                }
                return;
            }
        }
        if (invertValue)
        {
            g.setColour (kPixel);
            g.fillRect (f.look == Field::Look::Tile ? value : fitted (value, text, 6.0f));
        }
        g.setColour (invertValue ? kLcd : kPixel);
        const auto font = f.look == Field::Look::Tile ? type::font (type::kLabel, true) : type::font (kSmall + 1.0f, true);
        g.setFont (font);
        g.drawText (text, value, juce::Justification::centred);
        if (focused && f.digits > 0)
        {
            // The digit the wheel moves, underlined: the `digit`th of the text's first digits.
            int at = -1;
            for (int c = 0, seen = 0; c < text.length(); ++c)
                if (juce::CharacterFunctions::isDigit (text[c]) && seen++ == digit)
                {
                    at = c;
                    break;
                }
            if (at >= 0)
            {
                const float x0 = value.getCentreX() - juce::GlyphArrangement::getStringWidth (font, text) * 0.5f;
                const float x = x0 + juce::GlyphArrangement::getStringWidth (font, text.substring (0, at));
                const float w = juce::GlyphArrangement::getStringWidth (font, text.substring (at, at + 1));
                g.setColour (invertValue ? kLcd : kPixel);
                g.fillRect (juce::Rectangle<float> (x, value.getCentreY() + font.getHeight() * 0.5f, w, 2.0f));
            }
        }
    }

    void Display::paintEncoderRow (juce::Graphics& g, juce::Rectangle<float> row) const
    {
        // Sixteen knobs numbered under them, a gap, E1 and E2: the encoders over the faders.
        g.setColour (kPixel);
        g.drawRect (row, 1.0f);
        auto r = row.reduced (6.0f, 2.0f);
        const float cell = r.getWidth() / 19.0f;
        g.setFont (type::font (8.5f, true));
        for (int k = 0; k < 18; ++k)
        {
            const float x = r.getX() + cell * (float) (k < 16 ? k : k + 1);
            const auto c = juce::Rectangle<float> (x, r.getY(), cell, r.getHeight());
            const float d = juce::jmin (cell - 6.0f, r.getHeight() - 9.0f);
            knob (g, juce::Rectangle<float> (d, d).withCentre ({ c.getCentreX(), c.getY() + d * 0.5f + 1.0f }), 0.5f, 1.0f);
            g.drawText (k < 16 ? juce::String (k + 1) : "E" + juce::String (k - 15), c.withTrimmedTop (d + 1.0f), juce::Justification::centred);
        }
    }

    void Display::paint (juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        hw::lcd (g, b, look());
        // The title bar (V2 MAN p.20): the scene memory at the left, the page's
        // name and number boxed, the channel boxed at the right.
        auto top = b.reduced (14.0f, 0.0f).withTrimmedTop (12.0f).removeFromTop (22.0f);
        g.setColour (kPixel);
        g.drawRect (top, 1.0f);
        g.setFont (type::font (type::kLabel, true));
        const auto channel = top.removeFromRight (juce::jmax (90.0f, top.getWidth() * 0.16f));
        g.drawRect (channel, 1.5f);
        g.drawText (strip >= kEff && strip < kBus ? stripName (strip) + " RTN" : stripName (strip), channel, juce::Justification::centred); // "EFF1 RTN" (p.206)
        const auto name = title();
        const auto titleBox = top.removeFromRight (juce::jmax (150.0f, juce::GlyphArrangement::getStringWidth (type::font (type::kLabel, true), name) + 24.0f));
        g.drawRect (titleBox, 1.5f);
        g.drawText (name, titleBox, juce::Justification::centred);
        g.drawText (memoryText ? memoryText() : juce::String(), top.reduced (10.0f, 0.0f), juce::Justification::centredLeft);
        paintEncoderRow (g, b.reduced (14.0f, 0.0f).withTrimmedBottom (6.0f).removeFromBottom (28.0f));

        // A picture's lines, frames and words.
        for (const auto& m : marks)
        {
            const auto r = toPage (m.at);
            g.setColour (kPixel);
            if (m.kind == Mark::Rule)
                g.drawLine (r.getX(), r.getY(), r.getRight(), r.getBottom(), 1.0f);
            else if (m.kind == Mark::Frame)
            {
                g.drawRect (r, 1.0f);
                if (m.text.isNotEmpty())
                {
                    // Its title on its top line.
                    g.setFont (type::font (kSmall, true));
                    const auto cap = fitted (juce::Rectangle<float> (r.getX(), r.getY() - 6.0f, r.getWidth(), 12.0f), m.text, 10.0f);
                    g.setColour (kLcd);
                    g.fillRect (cap);
                    g.setColour (kPixel);
                    g.drawText (m.text, cap, juce::Justification::centred);
                }
            }
            else if (m.kind == Mark::Box)
                chip (g, fitted (r.reduced (1.0f), m.text, 4.0f), m.text, m.lit);
            else
            {
                g.setFont (type::font (kSmall, true));
                g.drawText (m.text, r, juce::Justification::centred);
            }
        }
        if (! spots.empty() && numPages() > 1)
        {
            // The page arrows: the next at the bottom right, the one before at the left.
            const auto a = toPage ({ arrowAt.x, arrowAt.y, 0.0f, 0.0f }).getPosition();
            if (page < numPages() - 1)
                chip (g, juce::Rectangle<float> (a.x - 16.0f, a.y - 13.0f, 16.0f, 13.0f), utf8 ("\xe2\x96\xb6"), false);
            if (page > 0)
                chip (g, juce::Rectangle<float> (a.x - 36.0f, a.y - 13.0f, 16.0f, 13.0f), utf8 ("\xe2\x97\x80"), false);
        }

        if (function == Function::Meter)
            paintMeters (g, pageArea());
        if (function == Function::Eq && ! spots.empty())
        {
            paintEq (g, toPage ({ 0.21f, 0.02f, 0.53f, 0.36f }));
            paintChannelMeter (g, toPage ({ 0.77f, 0.02f, 0.23f, 0.36f }), false);
        }
        if (function == Function::View && page == 0 && ! spots.empty())
        {
            paintChannelMeter (g, toPage ({ 0.16f, 0.0f, 0.18f, 0.52f }), false);
            paintViewSends (g, toPage ({ 0.36f, 0.0f, 0.28f, 1.0f }));
            if (hasEq (strip))
                paintEq (g, toPage ({ 0.67f, 0.06f, 0.32f, 0.34f }));
            if (hasDynamics (strip))
                paintTransfer (g, toPage ({ 0.67f, 0.48f, 0.16f, 0.32f }));
        }
        if (function == Function::Dynamics && ! spots.empty())
        {
            paintTransfer (g, toPage ({ 0.5f, 0.02f, 0.2f, 0.36f }));
            paintChannelMeter (g, toPage ({ 0.74f, 0.02f, 0.26f, 0.36f }), true);
        }
        if (fields.empty())
        {
            g.setColour (kPixelDim);
            g.drawText (stripName (strip) + " has no " + title(), b, juce::Justification::centred);
        }
        if (surroundGraph())
        {
            const auto area = pageArea();
            paintSurround (g, area.withLeft (area.getRight() - area.getHeight()));
        }
        if (function == Function::View && page == 1 && fields.size() > 1)
            paintPrograms (g, pageArea().withTop (fieldBox (0).getBottom() + 10.0f));
        for (int i = 0; i < (int) fields.size(); ++i)
            paintField (g, i);
        if (confirmGo != nullptr)
        {
            // The confirmation dialog over the page (V2 MAN p.126): "Please Confirm."
            // over its question ("Store to Scene 1?"), CANCEL and EXECUTE.
            auto box = b.withSizeKeepingCentre (b.getWidth() * 0.6f, 140.0f);
            g.setColour (kLcd.withAlpha (1.0f).brighter (0.08f)); // opaque: the page under it hidden
            g.fillRect (box);
            g.setColour (kPixel);
            g.drawRect (box, 2.0f);
            g.setFont (type::font (type::kLabel + 2.0f, true));
            g.drawText ("Please Confirm.", box.removeFromTop (40.0f).reduced (10.0f, 0.0f), juce::Justification::centred);
            g.setFont (type::font (type::kLabel, true));
            g.drawText (confirmText, box.removeFromTop (40.0f).reduced (10.0f, 0.0f), juce::Justification::centred);
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

    bool Display::surroundGraph() const { return function == Function::Routing && page >= 2 && memberOf (strip) >= 0; }

    void Display::paintSurround (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        // The pan graph (V2 MAN p.67): front at the top; the mode's speakers
        // around it; the places of the selected channel's group (MIC, LINE and
        // the effect returns, or TAPE), the selected one filled; on 4/4 its
        // trajectory.
        const int mode = juce::jlimit (1, 3, y.getState (Yamaha02R::Surround, 0));
        const auto graph = area.reduced (14.0f);
        const auto at = [graph] (float x, float yy) { return juce::Point<float> (graph.getX() + (x + 1.0f) * 0.5f * graph.getWidth(), graph.getY() + (1.0f - yy) * 0.5f * graph.getHeight()); };
        g.setColour (kLcdFrame);
        g.fillRect (graph);
        g.setColour (kPixelDim);
        g.drawRect (graph, 1.0f);
        g.drawLine (graph.getCentreX(), graph.getY(), graph.getCentreX(), graph.getBottom(), 0.5f);
        g.drawLine (graph.getX(), graph.getCentreY(), graph.getRight(), graph.getCentreY(), 0.5f);
        static const float places[3][5][2] = { { { -1, 1 }, { 1, 1 }, { -1, -1 }, { 1, -1 } },
                                               { { -1, 1 }, { 1, 1 }, { 0, 1 }, { 0, -1 } },
                                               { { -1, 1 }, { 1, 1 }, { -1, -1 }, { 1, -1 }, { 0, 1 } } };
        static const char* names[3][5] = { { "FL", "FR", "RL", "RR" }, { "L", "R", "C", "S" }, { "FL", "FR", "RL", "RR", "FC" } };
        g.setFont (type::font (9.0f, true));
        for (int k = 0; k < (mode == 3 ? 5 : 4); ++k)
        {
            const auto p = at (places[mode - 1][k][0], places[mode - 1][k][1]);
            const auto box = juce::Rectangle<float> (16.0f, 10.0f).withCentre ({ p.x, p.y + (places[mode - 1][k][1] > 0 ? -9.0f : 9.0f) });
            g.setColour (kPixel);
            g.fillRect (box);
            g.setColour (kLcd);
            g.drawText (names[mode - 1][k], box, juce::Justification::centred);
            // 4/4: each speaker's bus out level beside it (V2 MAN p.69), inwards.
            if (page == 3)
                speakerMeter (g, places[mode - 1][k][0] > 0 ? box.translated (-30.0f, 0.0f).withWidth (26.0f) : box.withX (box.getRight() + 4.0f).withWidth (26.0f), k);
        }
        if (mode == 3)
        {
            g.setColour (kPixel);
            const auto sw = juce::Rectangle<float> (24.0f, 10.0f).withCentre ({ graph.getCentreX(), area.getBottom() - 5.0f });
            g.drawText ("SW", sw, juce::Justification::centred);
            if (page == 3)
                speakerMeter (g, sw.withX (sw.getRight() + 2.0f).withWidth (26.0f), 5);
        }
        const auto value = [this] (int member, int field) { return (float) (y.getState (Yamaha02R::SurroundPan, member * Yamaha02R::kSurFields + field) - 32) / 32.0f; };
        const int sel = memberOf (strip);
        if (page == 3)
        {
            // The trajectory, as `livedaw_dsp::surround::Trajectory::at`.
            const int traj = y.getState (Yamaha02R::SurroundPan, sel * Yamaha02R::kSurFields + Yamaha02R::SurTraj);
            const float w = (float) y.getState (Yamaha02R::SurroundPan, sel * Yamaha02R::kSurFields + Yamaha02R::SurWidth) / 32.0f;
            const float d = (float) y.getState (Yamaha02R::SurroundPan, sel * Yamaha02R::kSurFields + Yamaha02R::SurDepth) / 32.0f;
            const float ox = value (sel, Yamaha02R::SurOffsetX), oy = value (sel, Yamaha02R::SurOffsetY);
            const float pi = juce::MathConstants<float>::pi;
            juce::Path path;
            for (int k = 0; k <= 64; ++k)
            {
                const float t = (float) k / 64.0f, s = 2.0f * t - 1.0f;
                float x = 0.0f, yy = 0.0f;
                switch (traj)
                {
                    case 0: x = s * w; break;
                    case 1: yy = -s * d; break;
                    case 2: x = s * w; yy = s * d; break;
                    case 3: x = s * w; yy = -s * d; break;
                    case 4: x = -std::cos (pi * t) * w; yy = std::sin (pi * t) * d; break;
                    case 5: x = -std::sin (pi * t) * w; yy = std::cos (pi * t) * d; break;
                    default: x = std::sin (2.0f * pi * t) * w; yy = std::cos (2.0f * pi * t) * d; break;
                }
                const auto p = at (juce::jlimit (-1.0f, 1.0f, x + ox), juce::jlimit (-1.0f, 1.0f, yy + oy));
                k == 0 ? path.startNewSubPath (p) : path.lineTo (p);
            }
            g.setColour (kPixel);
            g.strokePath (path, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved), juce::AffineTransform());
        }
        // The group's places.
        const int first = sel < 16 ? 0 : sel < kTape ? 16 : sel < kEff ? kTape : 16;
        std::vector<int> group;
        for (int m = first; m < (first == 16 ? kTape : first + 16); ++m)
            group.push_back (m);
        if (first == 16)
            group.insert (group.end(), { kEff, kEff + 1 });
        for (int m : group)
        {
            const auto p = at (value (m, Yamaha02R::SurX), value (m, Yamaha02R::SurY));
            const bool selected = m == sel;
            const float r = selected ? 7.0f : 5.0f;
            g.setColour (kPixel);
            if (selected)
                g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r);
            else
                g.drawEllipse (p.x - r, p.y - r, 2 * r, 2 * r, 1.0f);
            if (! selected)
                continue;
            g.setFont (type::font (9.0f, true));
            g.drawText (stripName (m), juce::Rectangle<float> (60.0f, 10.0f).withCentre ({ p.x, p.y + 13.0f }), juce::Justification::centred);
        }
    }

    void Display::speakerMeter (juce::Graphics& g, juce::Rectangle<float> box, int bus) const
    {
        // A short bar, -60 dB to 0, of the bus's output.
        const auto& buses = ctx.engine.getBuses();
        Strip* st = juce::isPositiveAndBelow (bus, (int) buses.size()) ? buses[(size_t) bus].get() : nullptr;
        const float level = st != nullptr ? juce::Decibels::gainToDecibels (ctx.meters.get (st, MeterPoint::Output, 0), -60.0f) : -60.0f;
        const auto bar = box.withSizeKeepingCentre (box.getWidth(), 4.0f);
        g.setColour (kPixelDim);
        g.drawRect (bar, 0.5f);
        g.setColour (kPixel);
        g.fillRect (bar.withWidth (bar.getWidth() * (level + 60.0f) / 60.0f));
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
        // METER (V2 MAN pp.41-43): 1/3 MIC 1-16 over LINE 17/18-23/24, 2/3 TAPE
        // 1-16 over EFF 1-2, 3/3 BUS, AUX and STEREO; each group on a scale from
        // CLIP to -48 dB, its numbers under it.
        const auto levelOf = [this] (int s)
        {
            auto& e = ctx.engine;
            auto pick = [] (const MixerEngine::StripList& l, int k) -> Strip* { return juce::isPositiveAndBelow (k, (int) l.size()) ? l[(size_t) k].get() : nullptr; };
            Strip* st = s < kEff ? pick (e.getChannels(), s) : s < kBus ? pick (e.getAux(), s - kEff) : s < kStereo ? pick (e.getBuses(), s - kBus) : pick (e.getMains(), 0);
            const auto point = meterPoint == 0 ? (s < kBus ? MeterPoint::Trim : MeterPoint::PreFader) : meterPoint == 1 ? MeterPoint::PreFader : MeterPoint::Output;
            return st != nullptr ? juce::Decibels::gainToDecibels (ctx.meters.get (st, point, 0), -48.0f) : -48.0f;
        };
        const auto numberOf = [] (int s)
        {
            return s < 16 ? juce::String (s + 1) : s < kTape ? juce::String (17 + 2 * (s - 16)) + "/" + juce::String (18 + 2 * (s - 16))
                 : s < kEff ? juce::String (s - kTape + 1) : s < kBus ? "EFF" + juce::String (s - kEff + 1)
                 : s < kAux ? juce::String (s - kBus + 1) : s < kStereo ? juce::String (s - kAux + 1) : juce::String ("ST");
        };
        const auto group = [&] (juce::Rectangle<float> r, const std::vector<int>& strips, const juce::StringArray& captions, const std::vector<int>& splits)
        {
            // The scale both sides, a dotted line at each mark; the bars; their numbers; the captions under brackets.
            g.setFont (type::font (8.5f, true));
            const auto scale = r.withTrimmedBottom (30.0f);
            const auto bars = scale.reduced (30.0f, 0.0f);
            const float marksDb[] = { 0.0f, -6.0f, -12.0f, -18.0f, -30.0f, -48.0f };
            const char* names[] = { "CLIP", "-6", "-12", "-18", "-30", "-48" };
            const auto yOf = [&scale] (float db) { return scale.getY() + 4.0f + (scale.getHeight() - 8.0f) * (-db / 48.0f); };
            for (int k = 0; k < 6; ++k)
            {
                const float yy = yOf (marksDb[k]);
                g.setColour (kPixel);
                g.drawText (names[k], juce::Rectangle<float> (scale.getX(), yy - 5.0f, 28.0f, 10.0f), juce::Justification::centredLeft);
                g.drawText (names[k], juce::Rectangle<float> (scale.getRight() - 28.0f, yy - 5.0f, 28.0f, 10.0f), juce::Justification::centredRight);
                g.setColour (kPixelDim);
                for (float x = bars.getX(); x < bars.getRight(); x += 4.0f)
                    g.fillRect (x, yy, 1.0f, 1.0f);
            }
            const int n = (int) strips.size();
            const float w = bars.getWidth() / (float) n;
            for (int k = 0; k < n; ++k)
            {
                const float level = levelOf (strips[(size_t) k]);
                const auto cell = juce::Rectangle<float> (bars.getX() + w * (float) k, yOf (0.0f), w, yOf (-48.0f) - yOf (0.0f));
                const auto bar = cell.withSizeKeepingCentre (juce::jmax (2.0f, w * 0.45f), cell.getHeight());
                g.setColour (kPixel);
                g.fillRect (bar.withTop (yOf (juce::jmax (-48.0f, level))));
                chip (g, fitted (juce::Rectangle<float> (cell.getX(), scale.getBottom() + 2.0f, w, 12.0f).reduced (1.0f, 0.0f), numberOf (strips[(size_t) k]), 4.0f),
                      numberOf (strips[(size_t) k]), strips[(size_t) k] == strip);
            }
            for (int c = 0; c < captions.size(); ++c)
            {
                const float x0 = bars.getX() + w * (float) splits[(size_t) c] + 2.0f, x1 = bars.getX() + w * (float) splits[(size_t) c + 1] - 2.0f;
                const float yy = scale.getBottom() + 18.0f;
                g.setColour (kPixel);
                g.drawLine (x0, yy, x1, yy, 1.0f);
                g.drawLine (x0, yy - 3.0f, x0, yy, 1.0f);
                g.drawLine (x1, yy - 3.0f, x1, yy, 1.0f);
                const auto cap = fitted (juce::Rectangle<float> (x0, yy - 1.0f, x1 - x0, 11.0f), captions[c], 10.0f);
                g.setColour (kLcd);
                g.fillRect (cap);
                g.setColour (kPixel);
                g.drawText (captions[c], cap, juce::Justification::centred);
            }
        };
        std::vector<int> top, bottom;
        if (page == 2)
        {
            for (int s = kBus; s <= kStereo; ++s)
                top.push_back (s);
            group (area.withTrimmedBottom (area.getHeight() * 0.14f), top, { "BUS", "AUX", "ST" }, { 0, 8, 16, 17 });
            return;
        }
        for (int k = 0; k < 16; ++k)
            top.push_back ((page == 0 ? 0 : kTape) + k);
        for (int k = 0; k < (page == 0 ? 4 : 2); ++k)
            bottom.push_back ((page == 0 ? 16 : kEff) + k);
        group (area.withHeight (area.getHeight() * 0.56f), top, { page == 0 ? "MIC" : "TAPE" }, { 0, 16 });
        group (area.withTrimmedTop (area.getHeight() * 0.6f).withWidth (area.getWidth() * (page == 0 ? 0.48f : 0.3f)), bottom, {}, {});
    }

    void Display::paintEq (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        // The curve of the strip's four bands (bells and shelves as the DSP's;
        // a filter as a 12 dB/oct slope).
        g.setColour (kPixel);
        g.drawRect (area, 1.0f);
        g.setFont (type::font (8.5f, true));
        for (const auto& [hz, name] : { std::pair<float, const char*> { 100.0f, "100" }, { 1000.0f, "1.0K" }, { 10000.0f, "10K" } })
        {
            const float x = area.getX() + area.getWidth() * std::log10 (hz / 20.0f) / 3.0f;
            g.setColour (kPixelDim);
            for (float yy = area.getY() + 2.0f; yy < area.getBottom() - 10.0f; yy += 3.0f)
                g.fillRect (x, yy, 1.0f, 1.0f);
            g.setColour (kPixel);
            g.drawText (name, juce::Rectangle<float> (x - 15.0f, area.getBottom() - 10.0f, 30.0f, 9.0f), juce::Justification::centred);
        }
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
                focusField (i, false);
                // A tap on a digit puts the cursor on it.
                if (const auto& f = fields[(size_t) i]; f.digits > 0 && f.get)
                {
                    const auto text = f.text ? f.text (f.get()) : juce::String (f.get());
                    const auto font = f.look == Field::Look::Tile ? type::font (type::kLabel, true) : type::font (kSmall + 1.0f, true);
                    float x = valueBox (i).getCentreX() - juce::GlyphArrangement::getStringWidth (font, text) * 0.5f;
                    for (int c = 0, seen = 0; c < text.length() && seen < f.digits; ++c)
                    {
                        const float w = juce::GlyphArrangement::getStringWidth (font, text.substring (c, c + 1));
                        if (juce::CharacterFunctions::isDigit (text[c]))
                        {
                            if (e.position.x >= x - 1.0f)
                                digit = seen;
                            ++seen;
                        }
                        x += w;
                    }
                }
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
        { "ENTER", [this]
          {
              // A double click: PHASE / ATT's and DELAY's all channels alike.
              const auto now = juce::Time::getMillisecondCounter();
              display.enter();
              if (now - lastEnter < 400)
              {
                  display.enterAll();
                  lastEnter = 0;
              }
              else
                  lastEnter = now;
          } },
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
            return displayText (p, stepValue (p, v));
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
    wheel = std::make_unique<universal::DataWheel>();
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
        return current >= 0 && scene.has_value() ? juce::String (current + 1) + " " + scene->name : juce::String ("0 Initial Data"); // unpadded, as p.19
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
    display.confirm (true, "Store to Scene " + juce::String (memory) + "?", [this, index = memory - 1]
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
    display.confirm (false, "Recall Scene " + juce::String (memory) + "?", [this, m = memory]
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
        case StripKind::Matrix: break;
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
