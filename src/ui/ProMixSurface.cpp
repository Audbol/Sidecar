#include "ui/ProMixSurface.h"

#include "engine/MixerEngine.h"
#include "engine/ProMix.h"
#include "engine/ProMixMidi.h"
#include "engine/Show.h"
#include "ui/HardwareLook.h"
#include "ui/MeterBank.h"

#include <cmath>
#include <complex>
#include <optional>

namespace livedaw::ui
{

using namespace universal;

namespace
{
    constexpr int kTopBar = 48;
    constexpr int kKey = 34;
    constexpr int kStripWidth = 92;

    // The LCD's colours: a backlit panel, light pixels, the cursor inverted.
    // The ProMix 01's hardware look: its yellow-green LCD with dark pixels.
    const hw::Look& look() { return hw::Look::promix(); }
    const juce::Colour kLcd = hw::Look::promix().lcd, kLcdFrame = hw::Look::promix().lcd.darker (0.2f),
                       kPixel = hw::Look::promix().lcdPixel, kPixelDim = hw::Look::promix().lcdDim;
    // Its LEDs: SEL and ON red; its function keys' colours.
    const juce::Colour kSelLed (0xffff3030), kOnLed (0xffff3030), kOrangeKey (0xffd9762c), kBlueKey (0xff7f96bf),
                       kGreenKey (0xff6aa676);

    // Parameter numbers (User's Guide, Add-5 to Add-15).
    constexpr int kChOn = 22, kChPan = 44, kSendLevel = 68, kSendPre = 136, kSend34Pan = 140, kCue = 157, kPhase = 179;
    constexpr int kEqOn = 196, kEq = 216, kPairFlag = 396, kFxType = 404, kFxParam = 406, kCompType = 446, kCompPatch = 449;
    constexpr int kCompKey = 452, kCompIn = 455, kCompOn = 458, kCompParam = 461, kSend34Flag = 476, kOsc = 477, kCueMode = 483;
    constexpr int kGroup = 484, kPanMode = 501, kGroupSelect = 502, kPairExecute = 503, kPatchPointSt = 511, kSend34Exec = 514;

    juce::Font lcdFont (float size, bool bold = false) { return type::font (size, bold); }

    // TITLE EDIT's characters (the console's set, as far as Sidecar's fonts draw them).
    const juce::String kTitleChars = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!\"#$%&'()*+,-./:;<=>?@[]_";

    /** The RBJ biquad magnitude (dB) at `f` of a bell or shelf (the ProMix EQ's
        bands as livedaw_dsp designs them), for the EQ function's curve. */
    double bandDb (double f, double fc, double gainDb, double q, int shape) // 0 bell, 1 low shelf, 2 high shelf
    {
        constexpr double sr = 48000.0, pi = 3.14159265358979323846;
        const double A = std::pow (10.0, gainDb / 40.0), w0 = 2.0 * pi * fc / sr, cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * (shape == 0 ? q : 0.7071067811865476));
        double b0, b1, b2, a0, a1, a2;
        if (shape == 0)
        {
            b0 = 1 + alpha * A, b1 = -2 * cw, b2 = 1 - alpha * A, a0 = 1 + alpha / A, a1 = -2 * cw, a2 = 1 - alpha / A;
        }
        else
        {
            const double s = shape == 1 ? -1.0 : 1.0, r = 2.0 * std::sqrt (A) * alpha;
            b0 = A * ((A + 1) + s * (A - 1) * cw + r);
            b1 = -2 * s * A * ((A - 1) + s * (A + 1) * cw);
            b2 = A * ((A + 1) + s * (A - 1) * cw - r);
            a0 = (A + 1) - s * (A - 1) * cw + r;
            a1 = 2 * s * ((A - 1) - s * (A + 1) * cw);
            a2 = (A + 1) - s * (A - 1) * cw - r;
        }
        const double w = 2.0 * pi * f / sr;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        const auto h = (b0 + b1 * z1 + b2 * z2) / (a0 + a1 * z1 + a2 * z2);
        return 20.0 * std::log10 (std::max (1e-9, std::abs (h)));
    }

    // The EQ tables (livedaw_dsp::promix: LOW_FREQS, MID_FREQS, HIGH_FREQS).
    constexpr float kMid[] = { 32, 36, 40, 45, 50, 56, 60, 70, 80, 90, 100, 110, 125, 140, 160, 180, 200, 220, 250, 280, 315,
                               360, 400, 450, 500, 560, 630, 700, 800, 900, 1000, 1100, 1250, 1400, 1600, 1800, 2000, 2200,
                               2500, 2800, 3200, 3600, 4000, 4500, 5000, 5600, 6300, 7000, 8000, 9000, 10000, 11000, 12000,
                               14000, 16000, 18000 };
    constexpr double kQ[] = { 8.65, 5.76, 4.32, 2.87, 1.90, 1.41, 0.92, 0.67, 0.40 };
} // namespace

namespace promix
{
    //==========================================================================
    Fader::Fader (UiContext& c, const Theme& t) : ctx (c), theme (t) {}

    void Fader::setParam (int n)
    {
        param = n;
        refresh();
        repaint();
    }

    void Fader::refresh()
    {
        const bool local = isLocal && isLocal();
        const int now = local ? (onLocal ? localStep : 0) : param >= 0 ? juce::jmax (0, ctx.engine.getProMix().get (param)) : 0;
        if (now != step)
        {
            step = now;
            repaint();
        }
    }

    juce::Rectangle<float> Fader::track() const noexcept
    {
        return getLocalBounds().toFloat().reduced (0.0f, 14.0f).withTrimmedLeft (26.0f);
    }

    float Fader::yOf (int s) const noexcept
    {
        const auto t = track();
        return t.getBottom() - t.getHeight() * (float) s / 127.0f;
    }

    void Fader::moveTo (int s)
    {
        s = juce::jlimit (0, 127, s);
        if (isLocal && isLocal())
        {
            if (onLocal && s != localStep)
            {
                localStep = s;
                onLocal (s);
                refresh();
            }
            return;
        }
        if (param < 0 || s == step)
            return;
        ctx.engine.getProMix().set (param, s);
        refresh();
    }

    void Fader::paint (juce::Graphics& g)
    {
        const auto t = track();
        const float cx = t.getCentreX();
        // The panel's scale: 6, 0, 5, 10, 20, 40, 60 and oo beside the slot.
        const auto& law = ctx.engine.getFaderLaw();
        g.setFont (type::font (type::kCaption));
        const std::pair<float, const char*> scale[] = { { 6.0f, "6" },    { 0.0f, "0" },    { -5.0f, "5" }, { -10.0f, "10" },
                                                        { -20.0f, "20" }, { -40.0f, "40" }, { -60.0f, "60" } };
        for (auto [db, text] : scale)
        {
            const float y = t.getBottom() - t.getHeight() * law.toPosition (db);
            hw::silk (g, juce::Rectangle<float> (0.0f, y - 7.0f, 22.0f, 14.0f), text, look(), 10.0f, juce::Justification::centredRight, true);
            g.setColour (look().silkDim.withAlpha (0.6f));
            g.drawHorizontalLine ((int) y, cx - 14.0f, cx - 6.0f);
            g.drawHorizontalLine ((int) y, cx + 6.0f, cx + 14.0f);
        }
        // The unlabelled ticks between (3, 2.5, 7.5, 15 and 30 down).
        g.setColour (look().silkDim.withAlpha (0.45f));
        for (float db : { 3.0f, -2.5f, -7.5f, -15.0f, -30.0f })
        {
            const float y = t.getBottom() - t.getHeight() * law.toPosition (db);
            g.drawHorizontalLine ((int) y, cx - 12.0f, cx - 6.0f);
            g.drawHorizontalLine ((int) y, cx + 6.0f, cx + 12.0f);
        }
        hw::silk (g, juce::Rectangle<float> (0.0f, t.getBottom() - 7.0f, 22.0f, 14.0f), juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")), look(),
                  10.0f, juce::Justification::centredRight, true);
        hw::faderSlot (g, cx, t.getY(), t.getBottom(), look());
        if (param < 0)
            return;
        // ST IN's cap light blue, RTN / SEND's salmon, ST OUT's dark grey (the console's).
        const auto capColour = param == 16 ? juce::Colour (0xff9fb3d6)
                             : param >= 17 && param <= 20 ? juce::Colour (0xffe08a80)
                             : param == 21 ? juce::Colour (0xff55585c) : juce::Colour();
        hw::faderCap (g, cx, yOf (step), 32.0f, look(), capColour);
    }

    void Fader::mouseDown (const juce::MouseEvent&)
    {
        dragStart = step;
    }

    void Fader::mouseDrag (const juce::MouseEvent& e)
    {
        const float perStep = track().getHeight() / 127.0f;
        moveTo (dragStart - juce::roundToInt ((float) e.getDistanceFromDragStartY() / perStep));
    }

    void Fader::mouseDoubleClick (const juce::MouseEvent&)
    {
        moveTo (juce::roundToInt (ctx.engine.getFaderLaw().toPosition (0.0f) * 127.0f)); // 0 dB
    }

    void Fader::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        moveTo (step + (w.deltaY > 0 ? 1 : -1));
    }

    //==========================================================================
    Display::Display (UiContext& c, const Theme& t) : ctx (c), theme (t), pm (c.engine.getProMix())
    {
        build();
    }

    Display::~Display() = default;

    /** The UTILITY and MIDI menus, as p.96's map lists them. */
    const char* const kUtilityItems[] = { "OSCILLATOR", "SEND3,4 CONFIGURATION", "OUTPUT COMP PATCH POINT", "MEMORY PROTECT", "BATTERY CHECK" };
    const char* const kMidiItems[] = { "MIDI SETUP", "PROGRAM CHANGE ASSIGN", "CONTROL CHANGE ASSIGN", "BULK DUMP/REQUEST", "LOCAL ON/OFF",
                                       "MEMORY CONTROL CHANGE OUT" };

    void Display::setFunction (Function f)
    {
        if (f == function)
        {
            // Pressed again: the function's next page (PAN, then PHASE: p.20);
            // GROUP the next group (p.56); an EQ band's key its next value.
            if (f == Function::Pan)
                function = Function::Phase;
            else if (f == Function::Phase)
                function = Function::Pan;
            else if (f == Function::Group)
                pm.set (kGroupSelect, (juce::jmax (0, pm.get (kGroupSelect)) + 1) % 4);
            else if (f == Function::Utility || f == Function::Midi)
                page = 0; // its key again: the menu (pp.65, 81)
            else if (f == Function::EqLow || f == Function::EqMid || f == Function::EqHigh)
            {
                const int band = f == Function::EqLow ? 0 : f == Function::EqMid ? 1 : 2;
                focus = 3 * band + ((focus / 3 == band ? focus % 3 : -1) + 1) % 3;
                repaint();
                return;
            }
            else
                page = (page + 1) % numPages();
        }
        else
        {
            if (function == Function::Group)
                pm.setBalAdj (false); // BAL ADJ is GROUP's
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
            // The menu (page 0), then its items in the map's order (p.96).
            case Function::Utility: return 1 + (int) std::size (kUtilityItems);
            case Function::Midi:    return 1 + (int) std::size (kMidiItems);
            case Function::Meter:
            case Function::Send1:
            case Function::Send2:
            case Function::Comp:    return 2;
            case Function::Pan:
            case Function::Phase:
            case Function::Cue:
            case Function::Send3:
            case Function::Send4:
            case Function::EqLow:
            case Function::EqMid:
            case Function::EqHigh:
            case Function::Library:
            case Function::Group:
            case Function::Pair:
            case Function::MemoryStore:
            case Function::MemoryRecall:
            case Function::TitleEdit: return 1;
        }
        return 1;
    }

    void Display::editProgramName (int library, int n, int target)
    {
        nameFrom = function;
        nameLibrary = library;
        nameProgram = n;
        nameTarget = target;
        const auto name = pm.programName (library, n);
        for (int k = 0; k < 15; ++k)
            titleChars[(size_t) k] = juce::jmax (0, kTitleChars.indexOfChar (k < name.length() ? name[k] : ' '));
        function = Function::TitleEdit;
        page = 0;
        focus = 0;
        build();
    }

    Display::Field Display::exitField (juce::Rectangle<float> box)
    {
        // EXIT: back to the function's first page (an EDIT page's, pp.35, 76).
        Field f;
        f.label = "EXIT";
        f.style = Field::Style::Button;
        f.get = [] { return -1; };
        f.steps = [] { return 1; };
        f.text = [] { return juce::String(); };
        f.set = [this] (int) { setPage (0); };
        f.onEnter = true;
        f.pending = 0;
        f.box = box;
        return f;
    }

    Display::Field Display::storeField (int library, int first, int count, int target, juce::Rectangle<float> box)
    {
        Field f;
        f.label = "STORE";
        f.get = [] { return -1; };
        f.steps = [count] { return count; };
        f.text = [] { return juce::String ("ENTER"); };
        f.textOf = [this, library, first] (int v) { return juce::String (first + v + 1).paddedLeft ('0', 2) + " " + pm.programName (library, first + v); };
        f.set = [this, library, first, target] (int v) { editProgramName (library, first + v, target); };
        f.onEnter = true;
        f.pending = 0;
        f.box = box;
        return f;
    }

    void Display::editTitle (int m, const juce::String& title)
    {
        nameLibrary = -1;
        titleMemory = m;
        for (int k = 0; k < 8; ++k)
            titleChars[(size_t) k] = juce::jmax (0, kTitleChars.indexOfChar (k < title.length() ? title[k] : ' '));
        function = Function::TitleEdit;
        page = 0;
        focus = 0;
        build();
    }

    void Display::setPage (int p)
    {
        page = juce::jlimit (0, numPages() - 1, p);
        focus = 0;
        build();
    }

    void Display::setProcessor (int p)
    {
        processor = (p % 3 + 3) % 3;
        build();
    }

    void Display::setEqStrip (int s)
    {
        if (s == eqStrip)
            return;
        eqStrip = juce::jlimit (0, 19, s);
        build();
    }

    namespace
    {
        /** A parameter's name as an EDIT page lists it (pp.35, 37-44, 76): the
            manual's own spellings ("Rev.time" as the LCD prints it, "Ini.Dly",
            "FB.Gain"), the rest in words with capitals ("Threshold", "Dly L"). */
        juce::String editName (const juce::String& label)
        {
            static const std::pair<const char*, const char*> names[] = {
                { "REV TIME", "Rev.Time" }, { "INI DLY", "Ini.Dly" }, { "MOD FREQ", "Mod.Freq" }, { "MOD DLY", "Mod.Dly" },
                { "MOD DEPTH", "Mod.Depth" }, { "REV DEPTH", "Rev.Depth" }, { "FB GAIN", "FB.Gain" }, { "FB DLY", "FB.Dly" },
                { "AM DEPTH", "AM Depth" }, { "PM DEPTH", "PM Depth" }, { "LPF", "LPF" }, { "HPF", "HPF" }, { "L/R DEPTH", "L/R Depth" },
                { "F/R DEPTH", "F/R Depth" } };
            for (auto [from, to] : names)
                if (label == from)
                    return to;
            juce::StringArray words;
            words.addTokens (label, " ", "");
            for (auto& w : words)
                w = w.length() <= 1 ? w : w.substring (0, 1) + w.substring (1).toLowerCase();
            return words.joinIntoString (" ");
        }

        /** A value as an EDIT page shows it: no space before the unit ("2.8s",
            "10ms", "0dB"), a ratio as "2:1". */
        juce::String editValue (const juce::String& label, juce::String text)
        {
            text = text.removeCharacters (" ");
            if (label == "RATIO" && text != "INF")
            {
                if (text.containsChar ('.'))
                    text = text.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
                return text + ":1";
            }
            if (text.startsWith ("+0") && text.removeCharacters ("+0.dB").isEmpty())
                return "0dB";
            return text;
        }
    }

    Display::Field Display::paramField (int n, juce::Rectangle<float> box, bool enter)
    {
        Field f;
        auto& p = pm;
        f.label = p.label (n);
        f.text = [&p, n] { return p.text (n); };
        f.get = [&p, n] { return p.get (n); };
        f.steps = [&p, n] { return p.steps (n); };
        f.set = [&p, n] (int v) { p.set (n, v); };
        f.textOf = [&p, n] (int v) { return p.text (n, v); };
        f.onEnter = enter;
        f.box = box;
        return f;
    }

    Display::Field Display::listField (const juce::String& label, int library, int count, int param, juce::Rectangle<float> box)
    {
        Field f = paramField (param, box, true);
        f.label = label;
        f.steps = [count] { return count; };
        auto& p = pm;
        f.item = [&p, library] (int i) { return juce::String (i + 1).paddedLeft ('0', 2) + " " + p.programName (library, i); };
        return f;
    }

    void Display::row (std::initializer_list<int> params, float y, float h, float x0, float x1, bool enter)
    {
        const float w = (x1 - x0) / (float) params.size();
        float x = x0;
        for (int n : params)
        {
            fields.push_back (paramField (n, { x, y, w, h }, enter));
            x += w;
        }
    }

    void Display::build()
    {
        fields.clear();
        scrollFirst = 0;
        const int e = eqStrip;
        switch (function)
        {
            case Function::Meter:
            {
                // PEAK HOLD: ENTER switches it.
                Field f;
                f.label = "PEAK HOLD";
                f.get = [this] { return peakHold ? 1 : 0; };
                f.steps = [] { return 2; };
                f.set = [this] (int v) { peakHold = v != 0; peaks.fill (0); };
                f.text = [this] { return juce::String (peakHold ? "ON" : "OFF"); };
                f.box = { 0.3f, 0.82f, 0.4f, 0.17f };
                f.style = Field::Style::Inline;
                fields.push_back (std::move (f));
                break;
            }
            case Function::Pan:
                if (page == 0)
                {
                    for (int ch = 0; ch < 16; ++ch)
                    {
                        auto f = paramField (kChPan + ch, { 0.1f * (float) (ch % 8), ch < 8 ? 0.0f : 0.37f, 0.1f, 0.37f });
                        f.style = Field::Style::Knob;
                        f.label = juce::String (ch + 1);
                        if (pm.get (kPairFlag + ch / 2) == 1)
                            f.span = ch % 2 == 0 ? 2 : 0;
                        fields.push_back (std::move (f));
                    }
                    auto mode = paramField (kPanMode, { 0.6f, 0.8f, 0.4f, 0.2f });
                    mode.label = "CONTROL";
                    mode.style = Field::Style::Inline;
                    fields.push_back (std::move (mode));
                }
                else
                {
                    row ({ 60, 61, 62, 63, 64, 65 }, 0.10f, 0.34f);
                    row ({ 67, 66 }, 0.55f, 0.34f, 0.0f, 0.34f);
                }
                break;
            case Function::Phase:
                // As p.20's picture: 1-8 beside the PAN link, 9-16 and ST IN under them;
                // the legend under the rule.
                for (int i = 0; i < 17; ++i)
                {
                    auto f = paramField (kPhase + i, { 0.12f + 0.095f * (float) (i < 8 ? i : i - 8), i < 8 ? 0.0f : 0.38f, 0.095f, 0.38f });
                    f.style = Field::Style::Circle;
                    f.label = i < 16 ? juce::String (i + 1) : juce::String ("STIN");
                    fields.push_back (std::move (f));
                }
                break;
            case Function::EqLow:
            case Function::EqMid:
            case Function::EqHigh:
            {
                const int focusBand = function == Function::EqLow ? 0 : function == Function::EqMid ? 1 : 2;
                for (int band = 0; band < 3; ++band)
                    for (int row = 0; row < 3; ++row)
                    {
                        const int what = row == 0 ? 1 : row == 1 ? 0 : 2; // G, F, Q
                        auto f = paramField (kEq + 20 * (3 * band + what) + e, { 0.25f + 0.25f * (float) band, 0.6f + 0.133f * (float) row, 0.25f, 0.133f });
                        f.style = Field::Style::Bare;
                        fields.push_back (std::move (f));
                    }
                {
                    auto on = paramField (kEqOn + e, { 0.0f, 0.62f, 0.15f, 0.38f });
                    on.label = "ON/OFF:";
                    on.style = Field::Style::Value;
                    fields.push_back (std::move (on));
                }
                focus = juce::jlimit (0, (int) fields.size() - 1, focus == 0 ? 3 * focusBand : focus);
                break;
            }
            case Function::Library:
            {
                // As p.22's picture: four programs ("1.RESET"), the curve at the
                // right; STORE and RECALL along the bottom.
                Field f;
                auto& p = pm;
                f.text = [] { return juce::String(); };
                f.get = [] { return -1; };
                f.steps = [] { return 50; };
                f.set = [&p, this] (int n) { p.recallEq (eqStrip, n); };
                f.onEnter = true;
                f.item = [&p] (int i) { return juce::String (i + 1) + "." + p.programName (ProMix::Eq, i); };
                f.box = { 0.0f, 0.0f, 0.64f, 0.76f };
                f.rows = 4;
                f.pending = 0;
                const int list = (int) fields.size();
                fields.push_back (std::move (f));
                fields.push_back (storeField (ProMix::Eq, 30, 20, eqStrip, { 0.0f, 0.8f, 0.18f, 0.2f }));
                fields.back().style = Field::Style::Button;
                Field recall;
                recall.label = "RECALL";
                recall.style = Field::Style::Button;
                recall.get = [] { return -1; };
                recall.steps = [] { return 1; };
                recall.text = [] { return juce::String(); };
                recall.set = [this, list] (int)
                {
                    auto& l = fields[(size_t) list];
                    if (l.pending >= 0)
                        l.set (l.pending);
                };
                recall.onEnter = true;
                recall.pending = 0;
                recall.box = { 0.2f, 0.8f, 0.2f, 0.2f };
                fields.push_back (recall);
                break;
            }
            case Function::Send1:
            case Function::Send2:
            {
                const int fx = function == Function::Send1 ? 0 : 1;
                if (page == 0)
                {
                    // As p.33's picture: three programs, "(FADER = SEND1)" under them;
                    // STORE, RECALL, EDIT and SEND: PRE / POST along the bottom.
                    const int list = (int) fields.size();
                    {
                        auto f = listField ({}, ProMix::Effects, 40, kFxType + fx, { 0.0f, 0.0f, 0.74f, 0.6f });
                        auto& p = pm;
                        f.item = [&p] (int i) { return juce::String (i + 1) + "." + p.programName (ProMix::Effects, i); };
                        f.rows = 3;
                        fields.push_back (std::move (f));
                    }
                    fields.push_back (storeField (ProMix::Effects, 30, 10, fx, { 0.0f, 0.8f, 0.18f, 0.2f }));
                    fields.back().style = Field::Style::Button;
                    Field recall;
                    recall.label = "RECALL";
                    recall.style = Field::Style::Button;
                    recall.get = [] { return -1; };
                    recall.steps = [] { return 1; };
                    recall.text = [] { return juce::String(); };
                    recall.set = [this, list] (int)
                    {
                        auto& l = fields[(size_t) list];
                        if (l.pending >= 0)
                            l.set (l.pending);
                    };
                    recall.onEnter = true;
                    recall.pending = 0;
                    recall.box = { 0.2f, 0.8f, 0.2f, 0.2f };
                    fields.push_back (recall);
                    Field edit = recall;
                    edit.label = "EDIT";
                    edit.set = [this] (int) { setPage (1); };
                    edit.box = { 0.42f, 0.8f, 0.15f, 0.2f };
                    fields.push_back (edit);
                    auto send = paramField (kSendPre + fx, { 0.6f, 0.8f, 0.38f, 0.2f });
                    send.label = "SEND";
                    send.style = Field::Style::Inline;
                    fields.push_back (std::move (send));
                }
                else
                {
                    // EFFECT EDIT (p.35): the parameters as a list, "[1]Rev.Time= 2.8s",
                    // three at a time under the program's name; EXIT at the bottom right.
                    scrollArea = { 0.02f, 0.2f, 0.72f, 0.48f };
                    scrollRows = 3;
                    for (int k = 0; k < 10; ++k)
                    {
                        const int n = kFxParam + 20 * fx + 2 * k;
                        const auto name = pm.label (n);
                        if (name.isEmpty())
                            break;
                        auto f = paramField (n, {});
                        f.label = "[" + juce::String (k + 1) + "]" + editName (name) + "=";
                        auto& p = pm;
                        f.text = [&p, n, name] { return editValue (name, p.text (n)); };
                        f.style = Field::Style::Row;
                        f.scroll = k;
                        fields.push_back (std::move (f));
                    }
                    fields.push_back (exitField ({ 0.84f, 0.8f, 0.14f, 0.2f }));
                }
                break;
            }
            case Function::Send3:
            case Function::Send4:
            {
                // As p.45: "(FADER = SEND3)", the send's meter at the right, SEND: PRE /
                // POST at the bottom right; as a stereo pair (p.47) the channels' and
                // ST IN's SEND3-4 pans above, the pair's balance.
                const int send = function == Function::Send3 ? 2 : 3;
                auto pre = paramField (kSendPre + send, { 0.56f, 0.8f, 0.42f, 0.2f });
                pre.label = "SEND";
                pre.style = Field::Style::Inline;
                fields.push_back (std::move (pre));
                if (pm.get (kSend34Flag) == 1)
                {
                    for (int i = 0; i < 17; ++i)
                    {
                        auto f = paramField (kSend34Pan + i, { 0.08f * (float) (i % 9), i < 9 ? 0.0f : 0.3f, 0.08f, 0.3f });
                        f.style = Field::Style::Knob;
                        f.label = i < 16 ? juce::String (i + 1) : juce::String ("STIN");
                        fields.push_back (std::move (f));
                    }
                    auto bal = paramField (66, { 0.0f, 0.8f, 0.4f, 0.2f });
                    bal.label = "BAL";
                    bal.style = Field::Style::Inline;
                    fields.push_back (std::move (bal));
                }
                break;
            }
            case Function::Comp:
            {
                const int k = processor;
                if (page == 0)
                {
                    auto line = [this] (Field f, const juce::String& label)
                    {
                        f.style = Field::Style::Inline;
                        f.label = label;
                        fields.push_back (std::move (f));
                    };
                    line (paramField (kCompOn + k, { 0.2f, 0.0f, 0.42f, 0.11f }), "ON/OFF");
                    line (paramField (kCompPatch + k, { 0.2f, 0.11f, 0.42f, 0.11f }, true), "PATCH");
                    line (paramField (kCompKey + k, { 0.2f, 0.22f, 0.42f, 0.11f }, true), "KEYIN");
                    line (paramField (kCompIn + k, { 0.62f, 0.11f, 0.16f, 0.11f }), "IN");
                    const int list = (int) fields.size();
                    {
                        auto f = listField ({}, ProMix::Dynamics, 20, kCompType + k, { 0.0f, 0.37f, 0.76f, 0.4f });
                        auto& p = pm;
                        f.item = [&p] (int i) { return juce::String (i + 1) + "." + p.programName (ProMix::Dynamics, i); };
                        f.rows = 3;
                        fields.push_back (std::move (f));
                    }
                    fields.push_back (storeField (ProMix::Dynamics, 10, 10, k, { 0.0f, 0.8f, 0.22f, 0.2f }));
                    fields.back().style = Field::Style::Button;
                    Field recall;
                    recall.label = "RECALL";
                    recall.style = Field::Style::Button;
                    recall.get = [] { return -1; };
                    recall.steps = [] { return 1; };
                    recall.text = [] { return juce::String(); };
                    recall.set = [this, list] (int)
                    {
                        // RECALL: the program chosen in the list.
                        auto& l = fields[(size_t) list];
                        if (l.pending >= 0)
                            l.set (l.pending);
                    };
                    recall.onEnter = true;
                    recall.pending = 0;
                    recall.box = { 0.24f, 0.8f, 0.24f, 0.2f };
                    fields.push_back (recall);
                    Field edit = recall;
                    edit.label = "EDIT";
                    edit.set = [this] (int) { setPage (1); };
                    edit.box = { 0.5f, 0.8f, 0.18f, 0.2f };
                    fields.push_back (edit);
                }
                else
                {
                    // COMP EDIT (p.76): the program's name, its parameters as a list
                    // ("Threshold= 0dB", four at a time) beside the gain curve; ON/OFF
                    // and PATCH along the bottom, EXIT at the right.
                    scrollArea = { 0.02f, 0.2f, 0.46f, 0.58f };
                    scrollRows = 4;
                    for (int i = 0; i < 5; ++i)
                    {
                        const int n = kCompParam + 5 * k + i;
                        const auto name = pm.label (n);
                        if (name.isEmpty())
                            break;
                        auto f = paramField (n, {});
                        f.label = editName (name) + "=";
                        auto& p = pm;
                        f.text = [&p, n, name] { return editValue (name, p.text (n)); };
                        f.style = Field::Style::Row;
                        f.scroll = i;
                        fields.push_back (std::move (f));
                    }
                    auto on = paramField (kCompOn + k, { 0.0f, 0.8f, 0.3f, 0.2f });
                    on.label = "ON/OFF";
                    on.style = Field::Style::Inline;
                    fields.push_back (std::move (on));
                    fields.push_back (exitField ({ 0.84f, 0.8f, 0.14f, 0.2f }));
                }
                break;
            }
            case Function::Cue:
            {
                // The selected strip at a glance (pp.52-54): CUE: along the top; an
                // input's group, PHASE and PAN (ST IN's dual pan), a return's or ST
                // OUT's balance; its fader with ON over it (an input's sends beside
                // it, PRE / POST over them), the EQ curve and EQ:.
                using S = Field::Style;
                auto cue = paramField (kCueMode, { 0.36f, 0.0f, 0.36f, 0.16f }, true);
                cue.style = S::Inline;
                cue.label = "CUE";
                fields.push_back (std::move (cue));
                const bool input = e <= 16, stOut = e == 19;
                const int level = stOut ? 21 : e; // CH1-16, ST IN, RTN1-2 are their own levels' numbers
                auto on = paramField (kChOn + level, { 0.0f, 0.3f, 0.08f, 0.12f });
                on.style = S::Value;
                on.label = {};
                fields.push_back (std::move (on));
                auto fader = paramField (level, { 0.0f, 0.42f, 0.08f, 0.58f });
                fader.style = S::Fader;
                fader.label = e < 16 ? "CH" : e == 16 ? "STIN" : stOut ? "STOUT" : "RTN";
                fields.push_back (std::move (fader));
                if (input)
                {
                    for (int k = 0; k < 4; ++k)
                    {
                        auto s = paramField (kSendLevel + 17 * k + e, { 0.08f + 0.07f * (float) k, 0.42f, 0.07f, 0.58f });
                        s.style = S::Fader;
                        s.label = "S" + juce::String (k + 1);
                        fields.push_back (std::move (s));
                    }
                    auto group = paramField (kGroup + e, { 0.0f, 0.0f, 0.1f, 0.29f });
                    group.style = S::Groups;
                    group.label = {};
                    fields.push_back (std::move (group));
                    auto phase = paramField (kPhase + e, { 0.11f, 0.07f, 0.1f, 0.22f }); // under its heading
                    phase.style = S::Circle;
                    phase.label = {};
                    fields.push_back (std::move (phase));
                }
                // PAN: a channel's knob; ST IN's and a return's dual pan (L, R); ST OUT's balance.
                const int pan = e < 16 ? kChPan + e : e == 16 ? 60 : e <= 18 ? 62 + 2 * (e - 17) : 67;
                auto knob = paramField (pan, { 0.22f, 0.07f, 0.1f, 0.22f });
                knob.style = S::Knob;
                knob.label = {};
                fields.push_back (std::move (knob));
                if (e < 20)
                {
                    auto eq = paramField (kEqOn + e, { 0.36f, 0.84f, 0.3f, 0.16f });
                    eq.style = S::Inline;
                    eq.label = "EQ";
                    fields.push_back (std::move (eq));
                }
                break;
            }
            case Function::Group:
            {
                // As p.56's picture: the channels' numbers over the dots, the group
                // shown inverted; BAL ADJ at the right; SEL = GROUP under them.
                fields.push_back (paramField (kGroupSelect, { 0.2f, 0.84f, 0.5f, 0.16f }));
                fields.back().label = "SEL = GROUP";
                fields.back().style = Field::Style::Inline;
                // BAL ADJ (p.56): ENTER lets the group go while one fader is set.
                Field bal;
                bal.label = "BAL ADJ";
                bal.get = [this] { return pm.balAdj() ? 1 : 0; };
                bal.steps = [] { return 2; };
                bal.set = [this] (int v) { pm.setBalAdj (v != 0); };
                bal.text = [this] { return juce::String (pm.balAdj() ? "ON" : "OFF"); };
                bal.box = { 0.8f, 0.3f, 0.2f, 0.2f };
                bal.style = Field::Style::Inline;
                fields.push_back (std::move (bal));
                for (int m = 0; m < 17; ++m)
                {
                    fields.push_back (paramField (kGroup + m, { 0.08f + 0.7f * (float) m / 17.0f, 0.0f, 0.7f / 17.0f, 0.16f }));
                    fields.back().label = m < 16 ? juce::String (m + 1) : juce::String ("St");
                    fields.back().style = Field::Style::Number;
                }
                break;
            }
            case Function::Pair:
                // As p.58's picture: the eight pairs' boxes in two rows; the one
                // under the cursor's setting under them.
                for (int p = 0; p < 8; ++p)
                {
                    fields.push_back (paramField (kPairExecute + p, { 0.03f + 0.24f * (float) (p % 4), p < 4 ? 0.04f : 0.34f, 0.22f, 0.26f }, true));
                    fields.back().style = Field::Style::PairBox;
                    fields.back().index = p;
                }
                break;
            case Function::Utility:
            {
                if (page == 0)
                {
                    menu (kUtilityItems, (int) std::size (kUtilityItems));
                    break;
                }
                const int item = page - 1;
                if (item == 1)
                {
                    // SEND3,4 CONFIGURATION (p.46): the flag shown, the Execute set by ENTER.
                    auto mode = paramField (kSend34Exec, { 0.1f, 0.4f, 0.7f, 0.16f }, true);
                    auto& p = pm;
                    mode.text = [&p] { return p.text (kSend34Flag); };
                    mode.get = [&p] { return p.get (kSend34Flag); };
                    mode.label = "MODE";
                    mode.style = Field::Style::Inline;
                    fields.push_back (std::move (mode));
                }
                else if (item == 2)
                {
                    // OUTPUT COMP PATCH POINT (p.74): SEND3, SEND4 and STEREO, PRE or POST FADER.
                    const std::pair<int, const char*> points[] = { { 512, "SEND3" }, { 513, "SEND4" }, { kPatchPointSt, "STEREO" } };
                    float y = 0.34f;
                    for (auto [n, name] : points)
                    {
                        auto f = paramField (n, { 0.1f, y, 0.7f, 0.15f });
                        auto& p = pm;
                        f.text = [&p, n] { return p.text (n) + " FADER"; };
                        f.label = name;
                        f.style = Field::Style::Inline;
                        fields.push_back (std::move (f));
                        y += 0.15f;
                    }
                }
                else if (item == 0)
                {
                    // OSCILLATOR (p.90): ON, LEVEL, FREQ, and where it goes.
                    // As p.90's picture: ASSIGN's three, OSC: and FREQ: down the left;
                    // the LEVEL fader; the meters of SEND3, SEND4 and ST.
                    row ({ kOsc, kOsc + 1, kOsc + 2 }, 0.1f, 0.34f, 0.0f, 0.75f);
                    row ({ 480, 481, 482 }, 0.55f, 0.34f, 0.0f, 0.75f);
                    const juce::Rectangle<float> boxes[] = { { 0.0f, 0.68f, 0.34f, 0.15f }, { 0.38f, 0.18f, 0.1f, 0.82f }, { 0.0f, 0.84f, 0.34f, 0.15f },
                                                             { 0.04f, 0.29f, 0.32f, 0.13f }, { 0.04f, 0.42f, 0.32f, 0.13f }, { 0.04f, 0.55f, 0.32f, 0.13f } };
                    const char* names[] = { "OSC", "LEVEL", "FREQ", "SEND3", "SEND4", "ST OUT" };
                    for (size_t k = 0; k < 6 && k < fields.size(); ++k)
                    {
                        fields[k].box = boxes[k];
                        fields[k].label = names[k];
                        fields[k].style = k == 1 ? Field::Style::Fader : Field::Style::Inline;
                    }
                }
                else if (item == 3)
                {
                    // MEMORY PROTECT (p.65): STORE refuses a protected memory.
                    for (int half = 0; half < 2; ++half)
                    {
                        Field f;
                        f.get = [this, half] { return isProtected && isProtected (half) ? 1 : 0; };
                        f.set = [this, half] (int v) { if (setProtected) setProtected (half, v != 0); };
                        f.steps = [] { return 2; };
                        f.text = [this, half] { return juce::String (isProtected && isProtected (half) ? "ON" : "OFF"); };
                        f.label = half == 0 ? "Scene memory No.01-No.25" : "Scene memory No.26-No.50";
                        f.style = Field::Style::Inline;
                        f.box = { 0.04f, 0.36f + 0.16f * (float) half, 0.9f, 0.16f };
                        fields.push_back (std::move (f));
                    }
                }
                else
                {
                    // BATTERY CHECK (p.91): the memory's backup battery.
                    Field f;
                    f.get = [] { return 0; };
                    f.set = [] (int) {};
                    f.steps = [] { return 1; };
                    f.text = [] { return juce::String ("Battery is OK!!"); };
                    f.style = Field::Style::Text;
                    f.box = { 0.04f, 0.42f, 0.9f, 0.18f };
                    fields.push_back (std::move (f));
                }
                break;
            }
            case Function::Midi:
            {
                if (page == 0)
                {
                    menu (kMidiItems, (int) std::size (kMidiItems));
                    break;
                }
                const int mp = midiPage();
                if (mp == 5)
                {
                    // MEMORY CONTROL CHANGE OUT (p.87): the Edit Buffer's data types
                    // chosen, out as control changes; ENTER on OUTPUT executes it.
                    // (The console's other memories go out by recalling them first.)
                    Field memory;
                    memory.label = "MEMORY";
                    memory.get = [] { return 0; };
                    memory.steps = [] { return 1; };
                    memory.set = [] (int) {};
                    memory.text = [] { return juce::String ("EDIT BUFFER"); };
                    memory.style = Field::Style::Inline;
                    memory.box = { 0.0f, 0.31f, 0.6f, 0.13f };
                    fields.push_back (std::move (memory));
                    const char* const types[] = { "ON/OFF", "LEVEL", "EQ", "COMP", "PAN", "SEND", "EFFECT", "OTHERS" };
                    for (int k = 0; k < 8; ++k)
                    {
                        Field f;
                        f.label = types[k];
                        f.get = [this, k] { return (ccOutMask >> k & 1) != 0 ? 1 : 0; };
                        f.steps = [] { return 2; };
                        f.set = [this, k] (int v) { ccOutMask = v != 0 ? ccOutMask | 1u << k : ccOutMask & ~(1u << k); };
                        f.text = [this, k] { return juce::String ((ccOutMask >> k & 1) != 0 ? "ON" : "OFF"); };
                        f.style = Field::Style::Inline;
                        f.box = { k < 4 ? 0.0f : 0.4f, 0.44f + 0.14f * (float) (k % 4), 0.38f, 0.14f };
                        fields.push_back (std::move (f));
                    }
                    Field out;
                    out.label = "OUTPUT";
                    out.get = [] { return -1; };
                    out.steps = [] { return 1; };
                    out.text = [this] { return ccOutMessage.isEmpty() ? juce::String ("ENTER: EXECUTE") : ccOutMessage; };
                    out.textOf = [] (int) { return juce::String ("EXECUTE"); };
                    out.set = [this] (int)
                    {
                        if (ctx.consoleMidi == nullptr)
                        {
                            ccOutMessage = "MIDI IS OFF";
                            return;
                        }
                        pm.memoryCcOut (ccOutMask);
                        ccOutMessage = "....DONE";
                    };
                    out.onEnter = true;
                    out.pending = 0;
                    out.style = Field::Style::Inline;
                    out.box = { 0.0f, 0.17f, 0.6f, 0.14f };
                    fields.push_back (std::move (out));
                    break;
                }
                if (mp == 4)
                {
                    // LOCAL ON/OFF (p.86): with Local OFF the faders and CH1-16 [SEL]
                    // control another device (control and program changes), only
                    // while this page shows; the faders' values in hex.
                    Field local;
                    local.label = "LOCAL ON/OFF";
                    local.get = [this] { return localOff ? 0 : 1; };
                    local.steps = [] { return 2; };
                    local.set = [this] (int v) { localOff = v == 0; localMessage.clear(); };
                    local.text = [this] { return juce::String (localOff ? "OFF" : "ON"); };
                    local.style = Field::Style::Inline;
                    local.box = { 0.0f, 0.2f, 0.5f, 0.16f };
                    fields.push_back (std::move (local));
                    Field message;
                    message.get = [] { return 0; };
                    message.steps = [] { return 1; };
                    message.set = [] (int) {};
                    message.text = [this] { return localMessage; };
                    message.style = Field::Style::Text;
                    message.box = { 0.5f, 0.2f, 0.5f, 0.16f };
                    fields.push_back (std::move (message));
                    for (int k = 0; k < 16; ++k)
                    {
                        Field f;
                        f.label = juce::String (k + 1);
                        f.get = [] { return 0; };
                        f.steps = [] { return 1; };
                        f.set = [] (int) {};
                        f.text = [this, k] { return juce::String::toHexString (localValues[(size_t) k]).paddedLeft ('0', 2).toUpperCase(); };
                        f.style = Field::Style::Grid;
                        f.box = { 0.02f + 0.1f * (float) (k % 8), k < 8 ? 0.44f : 0.72f, 0.1f, 0.28f };
                        fields.push_back (std::move (f));
                    }
                    break;
                }
                if (mp == 3)
                {
                    // CONTROL CHANGE ASSIGN (p.83): a parameter picked, its bank and
                    // controller (bank "--": none); the eight after it.
                    auto& p = pm;
                    const auto save = [this, &p] { ctx.settings.setValue (ProMixMidi::ccKey, p.getCcText()); };
                    const auto ccText = [&p] (int n) { const int c = p.ccCode (n); return c < 0 ? juce::String ("--") : "B" + juce::String (c >> 8) + " CC" + juce::String (c & 0xff); };
                    Field param;
                    param.label = "PARAMETER";
                    param.get = [this] { return ccParam; };
                    param.steps = [] { return 531; };
                    param.set = [this] (int v) { ccParam = v; build(); };
                    param.text = [this] { return juce::String (ccParam).paddedLeft ('0', 3) + " " + ProMix::name (ccParam); };
                    param.style = Field::Style::Text;
                    param.label = {}; // the number and name alone, under NO. and NAME
                    param.box = { 0.01f, 0.3f, 0.64f, 0.15f };
                    fields.push_back (std::move (param));
                    Field bank;
                    bank.label = "BANK";
                    bank.get = [this, &p] { const int c = p.ccCode (ccParam); return c < 0 ? 16 : c >> 8; };
                    bank.steps = [] { return 17; };
                    bank.set = [this, &p, save] (int v)
                    {
                        const int c = p.ccCode (ccParam);
                        p.setCcCode (ccParam, v >= 16 ? -1 : v << 8 | (c < 0 ? 0 : c & 0xff));
                        save();
                    };
                    bank.text = [this, &p] { const int c = p.ccCode (ccParam); return c < 0 ? juce::String ("--") : juce::String (c >> 8); };
                    bank.style = Field::Style::Bare;
                    bank.box = { 0.66f, 0.3f, 0.15f, 0.15f };
                    fields.push_back (std::move (bank));
                    Field cc;
                    cc.label = "CC";
                    cc.get = [this, &p] { const int c = p.ccCode (ccParam); return c < 0 ? 0 : c & 0xff; };
                    cc.steps = [] { return 96; };
                    cc.set = [this, &p, save] (int v)
                    {
                        const int c = p.ccCode (ccParam);
                        p.setCcCode (ccParam, (c < 0 ? 0 : c >> 8) << 8 | v);
                        save();
                    };
                    cc.text = [this, &p] { const int c = p.ccCode (ccParam); return c < 0 ? juce::String ("--") : juce::String (c & 0xff); };
                    cc.style = Field::Style::Bare;
                    cc.box = { 0.81f, 0.3f, 0.15f, 0.15f };
                    fields.push_back (std::move (cc));
                    for (int k = 0; k < 3; ++k)
                    {
                        const int n = (ccParam + 1 + k) % 531;
                        Field f;
                        f.label = juce::String (n).paddedLeft ('0', 3) + " " + ProMix::name (n);
                        f.get = [] { return 0; };
                        f.steps = [] { return 1; };
                        f.set = [] (int) {};
                        f.text = [ccText, n] { return ccText (n); };
                        f.style = Field::Style::Text;
                        f.box = { 0.0f, 0.45f + 0.15f * (float) k, 0.96f, 0.15f };
                        fields.push_back (std::move (f));
                    }
                    break;
                }
                if (mp == 2)
                {
                    // PROGRAM CHANGE ASSIGN (p.82): a program picked, the memory it recalls; the twelve from it.
                    auto& p = pm;
                    Field pgm;
                    pgm.label = "PGM";
                    pgm.get = [this] { return pcProgram; };
                    pgm.steps = [] { return 128; };
                    pgm.set = [this] (int v) { pcProgram = v; build(); };
                    pgm.text = [this] { return juce::String (pcProgram + 1); };
                    pgm.style = Field::Style::Inline;
                    pgm.box = { 0.04f, 0.2f, 0.26f, 0.15f };
                    fields.push_back (std::move (pgm));
                    Field mem;
                    mem.label = "MEMORY";
                    mem.get = [this, &p] { return p.pcScene (pcProgram) + 1; };
                    mem.steps = [] { return 52; };
                    mem.set = [this, &p] (int v)
                    {
                        p.setPcScene (pcProgram, v - 1);
                        ctx.settings.setValue (ProMixMidi::pcKey, p.getPcText());
                    };
                    mem.text = [this, &p] { const int s = p.pcScene (pcProgram); return s < 0 ? juce::String ("--") : juce::String (s).paddedLeft ('0', 2); };
                    mem.label = "MEM";
                    mem.style = Field::Style::Inline;
                    mem.box = { 0.34f, 0.2f, 0.26f, 0.15f };
                    fields.push_back (std::move (mem));
                    for (int k = 0; k < 9; ++k)
                    {
                        const int pc = (pcProgram + 1 + k) % 128;
                        Field f;
                        f.label = juce::String (pc + 1) + "= ";
                        f.style = Field::Style::Text;
                        f.get = [] { return 0; };
                        f.steps = [] { return 1; };
                        f.set = [] (int) {};
                        f.text = [&p, pc] { const int s = p.pcScene (pc); return s < 0 ? juce::String ("--") : juce::String (s).paddedLeft ('0', 2); };
                        f.box = { 0.08f + 0.27f * (float) (k % 3), 0.5f + 0.16f * (float) (k / 3), 0.25f, 0.16f };
                        fields.push_back (std::move (f));
                    }
                    break;
                }
                // MIDI SETUP (p.81): the protocol's own values (the console's, with
                // or without MIDI running), kept in the app settings.
                auto& p = pm;
                auto value = [&p] (int i) { juce::StringArray v; v.addTokens (p.getSetupText(), " ", ""); return v[i].getIntValue(); };
                auto setValue = [this, &p] (int i, int x)
                {
                    juce::StringArray v;
                    v.addTokens (p.getSetupText(), " ", "");
                    v.set (i, juce::String (x));
                    p.setSetupText (v.joinIntoString (" "));
                    ctx.settings.setValue (ProMixMidi::setupKey, p.getSetupText());
                };
                auto field = [&] (const juce::String& label, int i, int steps, int offset, juce::Rectangle<float> box, std::function<juce::String (int)> text)
                {
                    Field f;
                    f.label = label;
                    f.get = [value, i, offset] { return value (i) - offset; };
                    f.steps = [steps] { return steps; };
                    f.set = [setValue, i, offset] (int x) { setValue (i, x + offset); };
                    f.text = [value, i, text] { return text (value (i)); };
                    f.box = box;
                    fields.push_back (std::move (f));
                };
                auto number = [] (int x) { return juce::String (x); };
                auto onOff = [] (int x) { return juce::String (x != 0 ? "ON" : "OFF"); };
                if (mp == 1)
                {
                    // BULK (p.85): the interval between two dumps' messages.
                    field ("INTERVAL TIME", 15, 11, 0, { 0.04f, 0.76f, 0.7f, 0.16f }, [] (int x) { return juce::String (x * 100) + "ms"; });
                    fields.back().style = Field::Style::Inline;
                    break;
                }
                // As p.81's picture: Tx Ch: and Rx Ch: over the table of PROGRAM,
                // CONTROL and BULK against TX, RX, OMNI and ECHO; MODE: under it.
                field ("Tx Ch", 0, 16, 1, { 0.14f, 0.16f, 0.3f, 0.14f }, number);
                field ("Rx Ch", 1, 16, 1, { 0.5f, 0.16f, 0.3f, 0.14f }, number);
                field ("MODE", 14, 2, 0, { 0.0f, 0.86f, 0.6f, 0.14f }, [] (int x) { return juce::String (x != 0 ? "REGISTER" : "CHANNEL"); });
                for (auto& f : fields)
                    f.style = Field::Style::Inline;
                const char* kinds[] = { "PROGRAM", "CONTROL", "BULK" };
                const char* switches[] = { "Tx", "Rx", "OMNI", "ECHO" };
                for (int k = 0; k < 3; ++k)
                    for (int s = 0; s < 4; ++s)
                    {
                        field (juce::String (kinds[k]) + " " + switches[s], 2 + 4 * k + s, 2, 0,
                               { 0.22f + 0.15f * (float) s, 0.42f + 0.14f * (float) k, 0.15f, 0.14f }, onOff);
                        fields.back().style = Field::Style::Bare;
                    }
                break;
            }
            case Function::MemoryStore:
            case Function::MemoryRecall:
            {
                // The memories 00-50, the current one "=", the chosen one in the
                // middle (INC, DEC, the wheel); ENTER (or the key again) stores
                // or recalls it.
                // As pp.63-65: five memories, the list going round ("50.MEMORY50",
                // "00.InitData", "01=InitData"), an empty one named MEMORYnn.
                const bool store = function == Function::MemoryStore;
                Field f;
                f.get = [this] { return ctx.show.getCurrentSceneIndex() + 1; };
                f.steps = [] { return 51; };
                f.text = [] { return juce::String(); };
                f.item = [this] (int m)
                {
                    const auto t = memoryTitle ? memoryTitle (m) : juce::String();
                    const auto nn = juce::String (m).paddedLeft ('0', 2);
                    return nn + "." + (t.isEmpty() ? "MEMORY" + nn : t);
                };
                f.rows = 5;
                f.wrap = true;
                f.set = [this, store] (int m)
                {
                    if (chooseMemory)
                        chooseMemory (m);
                    if (store ? (bool) storeChosen : (bool) recallChosen)
                        (store ? storeChosen : recallChosen)();
                };
                f.onEnter = true;
                f.pending = chosenMemory ? chosenMemory() : 1;
                f.box = { 0.02f, 0.22f, 0.44f, 0.78f };
                fields.push_back (std::move (f));
                break;
            }
            case Function::TitleEdit:
            {
                // As pp.22 and 63: the name's characters on a line after its number
                // ("31. USER LIBRARY 1", "01.InitData"), the cursor's inverted; the
                // characters around it scroll in the box above; ENTER keeps the
                // name, CANCEL leaves it.
                const int n = nameLibrary < 0 ? 8 : 15;
                const float w = 0.026f;
                for (int k = 0; k < n; ++k)
                {
                    Field f;
                    f.get = [this, k] { return titleChars[(size_t) k]; };
                    f.set = [this, k] (int v) { titleChars[(size_t) k] = v; };
                    f.steps = [] { return kTitleChars.length(); };
                    f.text = [this, k] { return juce::String::charToString (kTitleChars[titleChars[(size_t) k]]); };
                    f.box = { 0.3f + w * (float) k, 0.5f, w, 0.2f };
                    f.style = Field::Style::Bare;
                    fields.push_back (std::move (f));
                }
                Field cancel;
                cancel.label = "CANCEL";
                cancel.style = Field::Style::Button;
                cancel.get = [] { return -1; };
                cancel.steps = [] { return 1; };
                cancel.text = [] { return juce::String(); };
                cancel.set = [] (int) {};
                cancel.box = { 0.78f, 0.8f, 0.2f, 0.2f };
                fields.push_back (cancel);
                break;
            }
        }
        if ((function == Function::Utility || function == Function::Midi) && page > 0)
            fields.push_back (exitField ({ 0.84f, 0.8f, 0.14f, 0.2f })); // back to the menu (pp.65, 81)
        focus = juce::jlimit (0, juce::jmax (0, (int) fields.size() - 1), focus);
        repaint();
    }

    void Display::menu (const char* const* items, int count)
    {
        // The UTILITY or MIDI menu (p.96's map; Sidecar's drawing): its items
        // listed, ENTER opens the one chosen.
        Field f;
        f.get = [] { return -1; };
        f.steps = [count] { return count; };
        f.text = [] { return juce::String(); };
        f.item = [items] (int i) { return juce::String (i + 1) + "." + items[i]; };
        f.set = [this] (int i) { menuChoice = i; setPage (i + 1); };
        f.onEnter = true;
        f.pending = juce::jlimit (0, count - 1, menuChoice);
        f.rows = 6;
        f.box = { 0.02f, 0.18f, 0.8f, 0.82f };
        fields.push_back (std::move (f));
    }

    int Display::midiPage() const
    {
        // MIDI's items in the menu's order, as the pages they were built as:
        // 0 SETUP, 1 BULK, 2 PROGRAM CHANGE, 3 CONTROL CHANGE, 4 LOCAL, 5 MEMORY CC OUT.
        static constexpr int pages[] = { 0, 2, 3, 1, 4, 5 };
        return function == Function::Midi && page > 0 ? pages[juce::jlimit (0, 5, page - 1)] : -1;
    }

    void Display::cursor (int delta)
    {
        // Past a page's first or last value: the function's other page (METER's,
        // PAN's, a SEND's or COMP's EDIT), else round again.
        const bool paged = function == Function::Meter || function == Function::Pan || function == Function::Comp
                           || function == Function::Send1 || function == Function::Send2;
        const int n = (int) fields.size(), next = focus + delta;
        if (paged && (n == 0 || next < 0 || next >= n))
        {
            setPage (page == 0 ? 1 : 0);
            return;
        }
        if (n == 0)
            return;
        focus = (next + n) % n;
        repaint();
    }

    void Display::turn (int delta)
    {
        if (fields.empty())
            return;
        auto& f = fields[(size_t) focus];
        const int n = juce::jmax (1, f.steps());
        if (f.onEnter)
        {
            const int from = f.pending >= 0 ? f.pending : juce::jmax (0, f.get());
            f.pending = juce::jlimit (0, n - 1, from + delta);
            if ((function == Function::MemoryStore || function == Function::MemoryRecall) && chooseMemory)
                chooseMemory (f.pending);
        }
        else
        {
            f.set (juce::jlimit (0, n - 1, juce::jmax (0, f.get()) + delta));
        }
        repaint();
    }

    void Display::enter()
    {
        if (fields.empty())
            return;
        if (function == Function::TitleEdit && fields[(size_t) focus].label == "CANCEL")
        {
            // CANCEL: back without the name (a memory already stored keeps its old one).
            if (nameLibrary >= 0)
            {
                nameLibrary = -1;
                function = nameFrom;
                focus = 0;
                build();
            }
            else
                setFunction (Function::MemoryRecall);
            return;
        }
        if (function == Function::TitleEdit)
        {
            juce::String title;
            for (int k = 0; k < (nameLibrary < 0 ? 8 : 15); ++k)
                title << kTitleChars[titleChars[(size_t) k]];
            if (nameLibrary >= 0)
            {
                // The program stored under its name; back to its page.
                pm.storeProgram (nameLibrary, nameProgram, nameTarget, title.trimEnd());
                if (libraryChanged)
                    libraryChanged();
                nameLibrary = -1;
                function = nameFrom;
                focus = 0;
                build();
                return;
            }
            if (setMemoryTitle)
                setMemoryTitle (titleMemory, title.trimEnd());
            setFunction (Function::MemoryRecall);
            return;
        }
        if (function == Function::Group)
        {
            // ENTER turns BAL ADJ on and off (p.56), wherever the cursor is.
            pm.setBalAdj (! pm.balAdj());
            build();
            return;
        }
        auto& f = fields[(size_t) focus];
        if (f.onEnter)
        {
            // What ENTER does may rebuild the page (TITLE EDIT): the field isn't touched after it.
            const auto set = f.set;
            const int pending = f.pending;
            if (! f.item)
                f.pending = -1;
            if (pending >= 0)
                set (pending);
        }
        else if (f.steps() == 2)
        {
            f.set (f.get() == 0 ? 1 : 0);
        }
        build(); // a type or a mode changes the page's fields
    }

    void Display::localMoved (int fader, int value)
    {
        localValues[(size_t) juce::jlimit (0, 15, fader)] = value;
        if (ctx.consoleMidi != nullptr)
            pm.localFader (fader, value);
        repaint();
    }

    void Display::localSelected (int ch)
    {
        if (ctx.consoleMidi != nullptr)
            pm.localSelect (ch);
        localMessage = ctx.consoleMidi != nullptr ? "Program Change Transmitted" : "MIDI IS OFF";
        repaint();
    }

    void Display::refresh()
    {
        juce::String now = title() + "|" + subtitle() + "|" + (memoryName ? memoryName() : juce::String()) + "|"
                           + juce::String (chosenMemory ? chosenMemory() : 0) + (memoryEdited && memoryEdited() ? "E" : "");
        for (auto& f : fields)
            now << "|" << f.text() << f.pending;
        if (function == Function::Meter || function == Function::Send1 || function == Function::Send2 || function == Function::Send3
            || function == Function::Send4 || function == Function::Comp)
            now << juce::String (juce::Time::getMillisecondCounter() / 100); // meters move
        if (now != shown)
        {
            shown = now;
            repaint();
        }
    }

    juce::String Display::title() const
    {
        // The FUNCTION area's name, as the console's LCD prints it.
        const char* names[] = { "METER", "PAN/\xc3\xb8", "PAN/\xc3\xb8", "COMP", "CUE", "SEND1", "SEND2", "SEND3", "SEND4", "EQ", "EQ", "EQ",
                                "LIBRARY", "GROUP", "PAIR", "UTILITY", "MIDI", "STORE", "RECALL", "TITLE" };
        if (function == Function::TitleEdit && nameLibrary >= 0)
        {
            // A program's name page keeps its function's name (pp.22, 36, 77).
            const int from = (int) nameFrom;
            return juce::String::fromUTF8 (names[juce::jlimit (0, (int) std::size (names) - 1, from)]);
        }
        if (function == Function::MemoryStore || function == Function::MemoryRecall || function == Function::TitleEdit)
            return "SCENE MEMORY";
        if ((function == Function::Send3 || function == Function::Send4) && pm.get (kSend34Flag) == 1)
            return "SEND 3-4"; // a stereo pair (p.47)
        return juce::String::fromUTF8 (names[(int) function]);
    }

    juce::String Display::subtitle() const
    {
        switch (function)
        {
            case Function::Phase: return "PHASE";
            case Function::Group: return "FADER";
            case Function::Pair: return "STEREO";
            case Function::Cue: return "CH VIEW";
            case Function::Comp: return "NO." + juce::String (processor + 1); // as the photo and p.76: no EDIT
            case Function::Send1:
            case Function::Send2: return {}; // EFFECT EDIT too (p.35)
            case Function::Library: return "EQ";
            case Function::Pan:
            case Function::Meter: return page == 0 ? "" : "ST IN / RTN";
            case Function::Utility:
            case Function::Midi:
                return {}; // the page's name is its banner
            case Function::TitleEdit:
                return nameLibrary == ProMix::Eq ? "EQ" : nameLibrary == ProMix::Dynamics ? "NO." + juce::String (nameTarget + 1) : juce::String();
            case Function::Send3:
            case Function::Send4:
            case Function::EqLow:
            case Function::EqMid:
            case Function::EqHigh:
            case Function::MemoryStore:
            case Function::MemoryRecall: return {};
        }
        return {};
    }

    juce::Rectangle<float> Display::leftColumn() const
    {
        const auto b = getLocalBounds().toFloat().reduced (14.0f, 10.0f);
        return b.withWidth (b.getWidth() * 0.2f);
    }

    juce::Rectangle<float> Display::pageArea() const
    {
        auto a = getLocalBounds().toFloat().reduced (14.0f, 12.0f);
        a.removeFromLeft (leftColumn().getWidth() + 16.0f); // the column and its rule
        return a.withTrimmedRight (18.0f);                  // the RTN / SEND arrow
    }

    juce::Rectangle<float> Display::pageLink() const
    {
        const auto a = pageArea();
        if (function == Function::Phase)
            return { a.getX(), a.getY(), a.getWidth() * 0.1f, a.getHeight() * 0.38f };
        return function == Function::Pan ? juce::Rectangle<float> (a.getX() + a.getWidth() * 0.84f, a.getY() + a.getHeight() * 0.4f, a.getWidth() * 0.13f, a.getHeight() * 0.32f)
                                         : juce::Rectangle<float> (a.getX() + a.getWidth() * 0.88f, a.getY() + a.getHeight() * 0.42f, a.getWidth() * 0.11f, a.getHeight() * 0.32f);
    }

    juce::Rectangle<float> Display::boxOf (const Field& f) const
    {
        const auto a = pageArea();
        auto box = f.box;
        if (f.scroll >= 0)
        {
            // A row of the scrolling list, if it's among the rows shown (the
            // cursor's row kept in view); else nowhere.
            if (juce::isPositiveAndBelow (focus, (int) fields.size()) && fields[(size_t) focus].scroll >= 0)
            {
                const int at = fields[(size_t) focus].scroll;
                scrollFirst = juce::jlimit (at - scrollRows + 1, at, scrollFirst);
            }
            const int row = f.scroll - scrollFirst;
            if (row < 0 || row >= scrollRows)
                return {};
            const float h = scrollArea.getHeight() / (float) scrollRows;
            box = { scrollArea.getX(), scrollArea.getY() + h * (float) row, scrollArea.getWidth(), h };
        }
        return { a.getX() + box.getX() * a.getWidth(), a.getY() + box.getY() * a.getHeight(), box.getWidth() * a.getWidth(), box.getHeight() * a.getHeight() };
    }

    void Display::resized() {}

    void Display::paintField (juce::Graphics& g, const Field& f, bool focused) const
    {
        auto box = boxOf (f).reduced (3.0f);
        using Style = Field::Style;
        if (f.item)
        {
            // A program list: the chosen one in the middle, the current one marked "=" (p.34).
            const int count = f.steps(), current = f.get(), chosen = f.pending >= 0 ? f.pending : juce::jmax (0, current);
            if (f.label.isNotEmpty())
            {
                g.setColour (kPixelDim);
                g.setFont (lcdFont (type::kCaption, true));
                g.drawText (f.label, box.removeFromTop (18.0f), juce::Justification::centredLeft);
            }
            const float rowH = f.rows > 0 ? box.getHeight() / (float) f.rows : 22.0f;
            const int rows = f.rows > 0 ? f.rows : juce::jmax (1, (int) (box.getHeight() / rowH));
            const int first = f.wrap ? chosen - rows / 2 : juce::jlimit (0, juce::jmax (0, count - rows), chosen - rows / 2);
            for (int j = first; j < (f.wrap ? first + rows : juce::jmin (count, first + rows)); ++j)
            {
                const int i = count > 0 ? (j % count + count) % count : j;
                auto r = box.removeFromTop (rowH);
                const bool sel = i == chosen;
                // The chosen one inverted under the cursor (pp.22, 32); a short
                // list's framed when the cursor is elsewhere (the COMP photo).
                g.setColour (kPixel);
                if (sel && focused)
                    g.fillRect (r);
                else if (sel && f.rows > 0)
                    g.drawRect (r.reduced (0.0f, 1.0f), 2.0f);
                g.setColour (sel && focused ? kLcd : i == current || f.rows > 0 ? kPixel : kPixelDim);
                g.setFont (lcdFont (f.rows > 0 ? type::kValue + 2.0f : type::kLabel, i == current));
                auto text = f.item (i);
                if (i == current)
                {
                    const int mark = text.indexOfAnyOf (" .");
                    if (mark >= 0)
                        text = text.substring (0, mark) + "=" + text.substring (mark + 1);
                }
                g.drawText (text, r.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
            }
            return;
        }
        if (f.style == Style::Bare)
        {
            // A cell of a table: the value alone, inverted under the cursor.
            if (focused)
            {
                g.setColour (kPixel);
                g.fillRect (box);
            }
            g.setColour (focused ? kLcd : kPixel);
            g.setFont (lcdFont (type::kValue + 1.0f, true));
            g.drawFittedText (f.text(), box.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centredRight, 1, 0.6f);
            return;
        }
        if (f.style == Style::Inline)
        {
            // "LABEL :VALUE", the value inverted under the cursor.
            juce::String value = f.text();
            if (f.onEnter && f.pending >= 0 && f.pending != f.get() && f.textOf)
                value = f.textOf (f.pending) + "?"; // waiting for ENTER
            g.setFont (lcdFont (type::kValue + 1.0f, true));
            const float labelW = juce::jmin (box.getWidth() * 0.55f, juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), f.label + " :") + 6.0f);
            g.setColour (kPixel);
            g.drawText (f.label + (f.label.length() < 6 ? " :" : ":"), box.removeFromLeft (labelW), juce::Justification::centredLeft);
            auto v = box.withWidth (juce::jmin (box.getWidth(), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), value) + 8.0f));
            if (focused)
            {
                g.fillRect (v);
                g.setColour (kLcd);
            }
            g.drawFittedText (value, v.toNearestInt(), juce::Justification::centred, 1, 0.6f);
            return;
        }
        if (f.style == Style::Row)
        {
            // "Name=" then the value at the right, the value inverted under the cursor (pp.35, 76).
            g.setFont (lcdFont (type::kValue + 1.0f, true));
            g.setColour (kPixel);
            g.drawText (f.label, box, juce::Justification::centredLeft);
            const auto value = f.text();
            auto v = box.removeFromRight (juce::jmin (box.getWidth() * 0.45f, juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), value) + 10.0f));
            if (focused)
            {
                g.fillRect (v);
                g.setColour (kLcd);
            }
            g.drawFittedText (value, v.toNearestInt(), juce::Justification::centredRight, 1, 0.6f);
            return;
        }
        if (f.style == Style::Button)
        {
            // A key on the screen: framed twice, inverted under the cursor.
            g.setColour (kPixel);
            if (focused)
                g.fillRect (box);
            else
            {
                g.drawRect (box, 2.0f);
                g.drawRect (box.reduced (3.0f), 1.0f);
            }
            g.setColour (focused ? kLcd : kPixel);
            g.setFont (lcdFont (type::kValue + 1.0f, true));
            g.drawFittedText (f.label, box.toNearestInt(), juce::Justification::centred, 1, 0.6f);
            return;
        }
        if (f.style == Style::Knob)
        {
            // A pan knob: its pointer from L16 (down left) to R16 (down right),
            // the channel's number under it (inverted under the cursor).
            auto num = box.removeFromBottom (juce::jmin (20.0f, box.getHeight() * 0.32f));
            g.setFont (lcdFont (type::kValue, true));
            auto numBox = num.withSizeKeepingCentre (juce::jmin (num.getWidth(), juce::jmax (26.0f, juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), f.label) + 8.0f)), num.getHeight());
            if (focused)
            {
                g.setColour (kPixel);
                g.fillRect (numBox);
            }
            g.setColour (focused ? kLcd : kPixel);
            g.setFont (lcdFont (type::kValue, true));
            g.drawText (f.label, numBox, juce::Justification::centred);
            if (f.span == 0)
                return; // a pair's: drawn over its partner's box and this one
            const float span = (float) f.span;
            const auto c = juce::Point<float> (box.getX() + box.getWidth() * span / 2.0f, box.getCentreY());
            const float r = juce::jmin (box.getWidth() * 0.4f, box.getHeight() * 0.46f) * (span > 1.0f ? 1.15f : 1.0f);
            g.setColour (kPixel);
            g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.6f);
            if (span > 1.0f)
                g.drawEllipse (juce::Rectangle<float> (r * 1.6f, r * 1.6f).withCentre (c), 1.2f);
            const int steps = juce::jmax (2, f.steps());
            const float t = (float) juce::jlimit (0, steps - 1, f.get()) / (float) (steps - 1);
            const float a = juce::MathConstants<float>::pi * (-0.75f + 1.5f * t); // 0 = up
            const juce::Point<float> dir (std::sin (a), -std::cos (a));
            g.drawLine (juce::Line<float> (c + dir * (r * 0.2f), c + dir * (r * 0.95f)), 2.5f);
            return;
        }
        // A word, inverted under the cursor.
        const auto word = [&g, focused] (juce::Rectangle<float> r, const juce::String& text, float size)
        {
            g.setFont (lcdFont (size, true));
            auto w = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 8.0f), r.getHeight());
            g.setColour (kPixel);
            if (focused)
            {
                g.fillRect (w);
                g.setColour (kLcd);
            }
            g.drawFittedText (text, w.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        };
        if (f.style == Style::Circle)
        {
            // N in a ring, R in a filled one; the number under it.
            word (box.removeFromBottom (juce::jmin (20.0f, box.getHeight() * 0.34f)), f.label, type::kValue + 1.0f);
            const float r = juce::jmin (box.getWidth(), box.getHeight()) * 0.42f;
            const auto ring = juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (box.getCentre());
            const auto letter = f.text().substring (0, 1);
            const bool rev = letter == "R";
            g.setColour (kPixel);
            if (rev)
                g.fillEllipse (ring);
            else
                g.drawEllipse (ring, 2.0f);
            g.setColour (rev ? kLcd : kPixel);
            g.setFont (lcdFont (r * 1.3f, true));
            g.drawText (letter, ring, juce::Justification::centred);
            return;
        }
        if (f.style == Style::PairBox)
        {
            // The two channels at its ends; between them the stereo mark when paired.
            const bool paired = pm.get (kPairFlag + f.index) == 1;
            const auto r = box.reduced (2.0f, box.getHeight() * 0.12f);
            g.setColour (kPixel);
            if (focused)
                g.fillRect (r);
            else
            {
                g.drawRect (r, 2.0f);
                g.drawRect (r.reduced (3.0f), 1.0f);
            }
            g.setColour (focused ? kLcd : kPixel);
            g.setFont (lcdFont (r.getHeight() * 0.62f, false));
            auto in = r.reduced (8.0f, 0.0f);
            g.drawText (juce::String (2 * f.index + 1), in, juce::Justification::centredLeft);
            g.drawText (juce::String (2 * f.index + 2), in, juce::Justification::centredRight);
            if (paired)
            {
                const auto c = r.getCentre();
                const float s = r.getHeight() * 0.2f;
                juce::Path mark;
                mark.addTriangle (c.x - 1.5f * s, c.y, c.x - 0.3f * s, c.y - s, c.x - 0.3f * s, c.y + s);
                mark.addTriangle (c.x + 1.5f * s, c.y, c.x + 0.3f * s, c.y - s, c.x + 0.3f * s, c.y + s);
                mark.addRectangle (c.x - 2.1f * s, c.y - s, 1.5f, 2.0f * s);
                mark.addRectangle (c.x + 2.1f * s - 1.5f, c.y - s, 1.5f, 2.0f * s);
                g.fillPath (mark);
            }
            return;
        }
        if (f.style == Style::Fader)
        {
            // Its slot and knob at the level; its name under it.
            word (box.removeFromBottom (18.0f), f.label, type::kValue + 1.0f);
            const auto slot = box.reduced (0.0f, 6.0f);
            g.setColour (kPixel);
            g.fillRect (juce::Rectangle<float> (3.0f, slot.getHeight()).withCentre (slot.getCentre()));
            const int steps = juce::jmax (2, f.steps());
            const float t = (float) juce::jlimit (0, steps - 1, f.get()) / (float) (steps - 1);
            const auto knobAt = juce::Rectangle<float> (juce::jmin (16.0f, box.getWidth() * 0.6f), 6.0f).withCentre ({ slot.getCentreX(), slot.getBottom() - t * slot.getHeight() });
            g.fillRect (knobAt);
            return;
        }
        if (f.style == Style::Number)
        {
            word (box, f.label, type::kValue + 1.0f);
            return;
        }
        if (f.style == Style::Text)
        {
            // Its name and value on a line, the value inverted under the cursor.
            g.setFont (lcdFont (type::kValue + 2.0f, false));
            g.setColour (kPixel);
            const float nameW = f.label.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), f.label);
            g.drawText (f.label, box.removeFromLeft (nameW + 2.0f), juce::Justification::centredLeft);
            auto v = box.withWidth (juce::jmin (box.getWidth(), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), f.text()) + 8.0f));
            if (focused && f.steps() > 1)
            {
                g.fillRect (v);
                g.setColour (kLcd);
            }
            g.drawFittedText (f.text(), v.toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
            return;
        }
        if (f.style == Style::Groups)
        {
            // A B over C D (p.52): the strip's group filled with its letter, the
            // others dotted; framed under the cursor.
            const int group = f.get(); // 0 none, 1-4 A-D
            const float s = juce::jmin (box.getWidth(), box.getHeight()) * 0.46f;
            g.setFont (lcdFont (s * 0.85f, true));
            for (int k = 0; k < 4; ++k)
            {
                auto sq = juce::Rectangle<float> (box.getCentreX() - s + s * (float) (k % 2), box.getCentreY() - s + s * (float) (k / 2), s, s).reduced (2.0f);
                g.setColour (kPixel);
                if (k + 1 == group)
                {
                    g.fillRect (sq);
                    g.setColour (kLcd);
                    g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + k)), sq, juce::Justification::centred);
                }
                else
                    for (float x = sq.getX(); x < sq.getRight(); x += 3.0f)
                    {
                        g.fillRect (x, sq.getY(), 1.0f, 1.0f);
                        g.fillRect (x, sq.getBottom() - 1.0f, 1.0f, 1.0f);
                    }
            }
            if (focused)
            {
                g.setColour (kPixel);
                g.drawRect (box, 1.5f);
            }
            return;
        }
        if (f.style == Style::Grid)
        {
            // A ruled cell: its name over its value.
            g.setColour (kPixel);
            g.drawRect (box.expanded (3.0f, 3.0f), 1.5f);
            g.setFont (lcdFont (type::kValue + 2.0f, true));
            g.drawText (f.label, box.removeFromTop (box.getHeight() * 0.5f), juce::Justification::centred);
            if (focused)
            {
                g.fillRect (box);
                g.setColour (kLcd);
            }
            g.drawText (f.text(), box, juce::Justification::centred);
            return;
        }
        if (f.style == Style::Value)
        {
            // Its name, and the value boxed under it.
            if (f.label.isNotEmpty())
            {
                g.setColour (kPixel);
                g.setFont (lcdFont (type::kValue, true));
                g.drawFittedText (f.label, box.removeFromTop (box.getHeight() * 0.4f).toNearestInt(), juce::Justification::centredLeft, 2, 0.6f);
            }
            const auto v = box.reduced (2.0f, 2.0f);
            g.setColour (kPixel);
            if (focused)
                g.fillRect (v);
            else
                g.drawRect (v, 2.0f);
            g.setColour (focused ? kLcd : kPixel);
            g.setFont (lcdFont (juce::jmin (v.getHeight() * 0.7f, 26.0f), true));
            g.drawFittedText (f.text(), v.toNearestInt(), juce::Justification::centred, 1, 0.6f);
            return;
        }
        if (focused)
        {
            g.setColour (kPixel);
            g.fillRoundedRectangle (box, 3.0f);
        }
        else
        {
            g.setColour (kLcdFrame);
            g.drawRoundedRectangle (box, 3.0f, 1.0f);
        }
        const auto ink = focused ? kLcd : kPixel;
        auto inner = box.reduced (4.0f, 2.0f);
        g.setColour (focused ? kLcd : kPixelDim);
        g.setFont (lcdFont (type::kCaption, true));
        g.drawFittedText (f.label, inner.removeFromTop (juce::jmin (18.0f, inner.getHeight() / 2.0f)).toNearestInt(), juce::Justification::centred, 1, 0.6f);
        juce::String value = f.text();
        if (f.onEnter && f.pending >= 0 && f.pending != f.get() && f.textOf)
            value = "> " + f.textOf (f.pending); // a choice waiting for ENTER
        g.setColour (ink);
        g.setFont (lcdFont (type::kValue, true));
        g.drawFittedText (value, inner.toNearestInt(), juce::Justification::centred, 1, 0.6f);
    }

    void Display::paintMeters (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        // METER (p.19): the inputs post GAIN, pre phase and EQ; RTN post effect;
        // SEND3-4 post fader.
        std::vector<std::pair<juce::String, float>> bars;
        auto level = [this] (Strip* s, MeterPoint p, int ch) { return s != nullptr ? ctx.meters.get (s, p, ch) : 0.0f; };
        const auto& chs = ctx.engine.getChannels();
        const auto& aux = ctx.engine.getAux();
        const auto& buses = ctx.engine.getBuses();
        if (page == 0)
        {
            for (int i = 0; i < 16 && i < (int) chs.size(); ++i)
                bars.push_back ({ juce::String (i + 1), level (chs[(size_t) i].get(), MeterPoint::Input, 0) });
        }
        else
        {
            const char* names[] = { "ST L", "ST R", "R1 L", "R1 R", "R2 L", "R2 R" };
            for (int i = 0; i < 6 && i / 2 < (int) aux.size(); ++i)
                bars.push_back ({ names[i], level (aux[(size_t) (i / 2)].get(), MeterPoint::Input, i % 2) });
            for (int b = 2; b < 4 && b < (int) buses.size(); ++b)
                bars.push_back ({ "S" + juce::String (b + 1), level (buses[(size_t) b].get(), MeterPoint::Output, 0) });
        }
        const float w = area.getWidth() / (float) juce::jmax (1, (int) bars.size());
        // Segments: CLIP, 15, 12, 9, 6, 3, 0, -6, -12, -18, -24, -40 (0 = -20 dBFS... the
        // LCD's scale is the console's, its CLIP the converter's).
        const float marks[] = { 0.0f, -5.0f, -8.0f, -11.0f, -14.0f, -17.0f, -20.0f, -26.0f, -32.0f, -38.0f, -44.0f, -60.0f };
        for (size_t i = 0; i < bars.size(); ++i)
        {
            auto col = juce::Rectangle<float> (area.getX() + w * (float) i, area.getY(), w, area.getHeight()).reduced (w * 0.18f, 0.0f);
            // The segments lit now, and the highest held.
            int litNow = 0;
            {
                const float db = juce::Decibels::gainToDecibels (bars[i].second, -100.0f);
                for (int s = 0; s < 12; ++s)
                    litNow += db >= marks[11 - s] ? 1 : 0;
                if (i < peaks.size())
                    peaks[i] = peakHold ? juce::jmax (peaks[i], litNow) : 0;
            }
            const int held = i < peaks.size() ? peaks[i] : 0;
            g.setColour (kPixelDim);
            g.setFont (lcdFont (type::kCaption, true));
            g.drawText (bars[i].first, col.removeFromBottom (16.0f), juce::Justification::centred);
            const float db = juce::Decibels::gainToDecibels (bars[i].second, -100.0f);
            const float segH = col.getHeight() / 12.0f;
            for (int s = 0; s < 12; ++s)
            {
                const bool lit = db >= marks[11 - s] || (peakHold && s == held - 1);
                auto seg = juce::Rectangle<float> (col.getX(), col.getBottom() - segH * (float) (s + 1), col.getWidth(), segH).reduced (0.0f, 1.5f);
                g.setColour (lit ? (s == 11 ? juce::Colour (0xffff6060) : kPixel) : kLcdFrame);
                g.fillRect (seg);
            }
        }
    }

    void Display::paintEqCurve (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        // The axes, as the console draws them: dB ticks up the left, the 0 dB
        // line with 100, 1.0K and 10K marked under it.
        const auto xOf = [area] (double f) { return area.getX() + area.getWidth() * (float) (std::log10 (f / 20.0) / 3.0); };
        g.setColour (kPixel);
        g.fillRect (area.getX(), area.getY(), 2.0f, area.getHeight());
        for (int k = -6; k <= 6; ++k)
            g.fillRect (area.getX(), area.getCentreY() - (float) k * 3.0f / 18.0f * area.getHeight() / 2.0f, k % 2 == 0 ? 9.0f : 5.0f, 1.0f);
        g.fillRect (area.getX(), area.getCentreY(), area.getWidth(), 1.0f);
        g.setFont (lcdFont (type::kCaption, true));
        for (auto [f, text] : { std::pair<double, const char*> { 100.0, "100" }, { 1000.0, "1.0K" }, { 10000.0, "10K" } })
        {
            const float x = xOf (f);
            g.fillRect (x, area.getCentreY() - 5.0f, 1.0f, 10.0f);
            g.drawText (text, juce::Rectangle<float> (x - 24.0f, area.getCentreY() + 3.0f, 48.0f, 14.0f), juce::Justification::centred);
        }
        const int e = eqStrip;
        auto v = [this] (int n) { return juce::jmax (0, pm.get (n)); };
        const bool on = v (kEqOn + e) != 0;
        struct Band
        {
            double f, g, q;
            int shape;
        } bands[3];
        for (int b = 0; b < 3; ++b)
        {
            const int fi = v (kEq + 20 * (3 * b) + e), gi = v (kEq + 20 * (3 * b + 1) + e), qi = v (kEq + 20 * (3 * b + 2) + e);
            const float f = b == 2 ? kMid[30 + juce::jmin (fi, 25)] : kMid[juce::jmin (fi, b == 0 ? 30 : 55)];
            const bool shelf = b != 1 && qi == 9;
            bands[b] = { f, (double) (gi - 15), kQ[juce::jmin (qi, 8)], shelf ? (b == 0 ? 1 : 2) : 0 };
        }
        juce::Path p;
        for (int i = 0; i <= 200; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / 200.0);
            double db = 0.0;
            if (on)
                for (auto& b : bands)
                    if (b.g != 0.0)
                        db += bandDb (f, b.f, b.g, b.q, b.shape);
            const float x = area.getX() + area.getWidth() * (float) i / 200.0f;
            const float y = area.getCentreY() - (float) juce::jlimit (-18.0, 18.0, db) / 18.0f * area.getHeight() / 2.0f;
            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
        g.setColour (on ? kPixel : kPixelDim);
        g.strokePath (p, juce::PathStrokeType (2.0f));
        // The band under the cursor: a dotted line at its frequency.
        if (function != Function::Library && focus < 9)
        {
            const float x = area.getX() + area.getWidth() * (float) (std::log10 (bands[focus / 3].f / 20.0) / 3.0);
            g.setColour (kPixel);
            for (float y = area.getY(); y < area.getBottom(); y += 5.0f)
                g.fillRect (x, y, 1.5f, 2.0f);
        }
    }

    void Display::paintGroups (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        // The FADER GROUP display (p.56): groups A-D down the side, the channels
        // across (their numbers over them, the fields), the shown group's row
        // inverted; a big dot where a channel is in the group, a small one where not.
        const int shownGroup = juce::jmax (0, pm.get (kGroupSelect));
        const float rowH = area.getHeight() / 4.0f, labelW = area.getWidth() * 0.1f, colW = (area.getWidth() - labelW) / 17.0f;
        for (int grp = 0; grp < 4; ++grp)
        {
            auto r = area.withY (area.getY() + rowH * (float) grp).withHeight (rowH);
            const bool inverted = grp == shownGroup;
            if (inverted)
            {
                g.setColour (kPixel);
                g.fillRect (r.reduced (0.0f, 1.0f));
            }
            const auto ink = inverted ? kLcd : kPixel;
            g.setColour (ink);
            g.setFont (lcdFont (type::kValue + 2.0f, true));
            const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + grp));
            g.drawText (inverted ? "[" + letter + "]" : letter, r.removeFromLeft (labelW), juce::Justification::centredLeft);
            for (int m = 0; m < 17; ++m)
            {
                const auto c = juce::Rectangle<float> (r.getX() + colW * (float) m, r.getY(), colW, rowH).getCentre();
                const float d = pm.get (kGroup + m) == grp + 1 ? juce::jmin (colW, rowH) * 0.6f : 3.0f;
                g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (c));
            }
        }
    }

    void Display::paintLeftColumn (juce::Graphics& g) const
    {
        // Down the LCD's left (as a console's): FUNCTION, MEMORY and SEL CH,
        // ruled off from each other and, twice, from the page.
        const auto col = leftColumn();
        const float y1 = col.getY() + col.getHeight() * 0.36f, y2 = col.getY() + col.getHeight() * 0.72f;
        g.setColour (kPixel);
        g.fillRect (col.getX(), y1, col.getWidth(), 2.0f);
        g.fillRect (col.getX(), y2, col.getWidth(), 2.0f);
        g.fillRect (col.getRight() + 5.0f, col.getY(), 2.5f, col.getHeight());
        g.fillRect (col.getRight() + 9.0f, col.getY(), 1.0f, col.getHeight());

        // FUNCTION: its name, and a second line ("No.2").
        auto fn = juce::Rectangle<float> (col.getX(), col.getY(), col.getWidth(), y1 - col.getY()).reduced (4.0f, 2.0f);
        const auto sub = subtitle();
        g.setFont (lcdFont (30.0f, true));
        g.drawFittedText (title(), fn.removeFromTop (fn.getHeight() * (sub.isEmpty() ? 1.0f : 0.6f)).toNearestInt(), juce::Justification::centred, 2, 0.7f); // SCENE over MEMORY
        if (sub.isNotEmpty())
        {
            g.setFont (lcdFont (type::kLabel, true));
            g.drawFittedText (sub, fn.toNearestInt(), juce::Justification::centredRight, 2, 0.6f);
        }

        // MEMORY: the scene's title, the number (two digits, big), EDIT.
        auto mem = juce::Rectangle<float> (col.getX(), y1 + 2.0f, col.getWidth(), y2 - y1 - 2.0f).reduced (4.0f, 2.0f);
        g.setFont (lcdFont (type::kValue + 1.0f));
        g.drawFittedText (memoryName ? memoryName() : juce::String(), mem.removeFromTop (mem.getHeight() * 0.34f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.6f);
        const int number = chosenMemory ? chosenMemory() : 0;
        g.setFont (lcdFont (mem.getHeight() * 1.05f, true));
        g.drawText (juce::String (number).paddedLeft ('0', 2), mem.removeFromLeft (mem.getWidth() * 0.5f), juce::Justification::centredLeft);
        if (memoryEdited && memoryEdited())
        {
            auto flag = mem.removeFromBottom (juce::jmin (20.0f, mem.getHeight() * 0.45f)).reduced (2.0f, 1.0f);
            g.fillRect (flag);
            g.setColour (kLcd);
            g.setFont (lcdFont (type::kLabel, true));
            g.drawText ("EDIT", flag, juce::Justification::centred);
            g.setColour (kPixel);
        }

        // SEL CH: the selected channel.
        auto sel = juce::Rectangle<float> (col.getX(), y2 + 2.0f, col.getWidth(), col.getBottom() - y2 - 2.0f).reduced (6.0f, 2.0f);
        const char* stripNames[] = { "ST IN", "RTN 1", "RTN 2", "ST OUT" };
        g.setFont (lcdFont (type::kTitle, false));
        g.drawFittedText (eqStrip < 16 ? "CH " + juce::String (eqStrip + 1) : juce::String (stripNames[eqStrip - 16]), sel.toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.6f);
    }

    void Display::paintEditHeader (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& name, float width) const
    {
        // An EDIT page's top line: the program's number and name, a rule under it.
        g.setColour (kPixel);
        g.setFont (lcdFont (type::kValue + 2.0f, true));
        g.drawText (name, juce::Rectangle<float> (area.getX() + 8.0f, area.getY(), area.getWidth() * width - 8.0f, area.getHeight() * 0.18f),
                    juce::Justification::centredLeft);
        g.fillRect (area.getX(), area.getY() + area.getHeight() * 0.18f, area.getWidth() * width, 1.5f);
    }

    void Display::paintGainCurve (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        // The processor's output level against its input (p.76), from -60 to
        // +18 dB both ways: 1:1 below the threshold, the ratio above it; a
        // gate's or a ducker's the line alone (Sidecar's drawing).
        const int base = kCompParam + 5 * processor;
        const bool comp = pm.label (base) == "THRESHOLD" && pm.label (base + 1) == "RATIO";
        g.setColour (kPixel);
        g.drawLine (r.getX(), r.getBottom(), r.getX(), r.getY(), 1.0f);
        g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getBottom(), 1.0f);
        float thr = 18.0f, ratio = 1.0f;
        if (comp)
        {
            thr = pm.text (base).removeCharacters ("dB +").getFloatValue();
            const auto rt = pm.text (base + 1);
            ratio = rt == "INF" ? 1000.0f : juce::jmax (1.0f, rt.getFloatValue());
        }
        const auto at = [r] (float in, float out)
        {
            return juce::Point<float> (r.getX() + r.getWidth() * (in + 60.0f) / 78.0f, r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (out + 60.0f) / 78.0f));
        };
        juce::Path p;
        p.startNewSubPath (at (-60.0f, -60.0f));
        p.lineTo (at (thr, thr));
        p.lineTo (at (18.0f, thr + (18.0f - thr) / ratio));
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    void Display::paint (juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        hw::lcd (g, b, look());
        paintLeftColumn (g);

        const auto area = pageArea();
        // The link to METER's or PAN's other page: a page with an arrow, its name under it.
        auto paintLink = [&g, this] (const juce::String& name)
        {
            auto r = pageLink();
            auto icon = r.removeFromTop (r.getHeight() * 0.62f).withSizeKeepingCentre (r.getHeight() * 0.8f, r.getHeight() * 0.8f);
            icon = icon.withSizeKeepingCentre (juce::jmin (icon.getWidth(), 34.0f), juce::jmin (icon.getHeight(), 34.0f));
            g.setColour (kPixel);
            g.drawRect (icon, 1.5f);
            juce::Path arrow;
            const auto c = icon.getCentre();
            const float s = icon.getWidth() * 0.28f;
            arrow.addArrow ({ c.x - s, c.y, c.x + s, c.y }, s * 0.7f, s * 1.4f, s * 0.9f);
            g.fillPath (arrow);
            g.setFont (lcdFont (type::kValue, true));
            g.drawText (name, r, juce::Justification::centred);
        };
        // METER's scale, as the LCD prints it: CLIP, +12, +6, 0, -40 at their segments.
        auto paintScale = [&g] (juce::Rectangle<float> col, juce::Rectangle<float> lines)
        {
            const std::pair<int, const char*> marks[] = { { 11, "CLIP" }, { 9, "+12" }, { 7, "+6" }, { 5, "0" }, { 0, "-40" } };
            const float segH = col.getHeight() / 12.0f;
            g.setFont (lcdFont (type::kCaption + 1.0f, true));
            for (auto [s, text] : marks)
            {
                const float y = col.getBottom() - segH * ((float) s + 0.5f);
                g.setColour (kPixel);
                g.drawText (text, col.withY (y - 8.0f).withHeight (16.0f), juce::Justification::centredRight);
                g.setColour (kPixelDim);
                for (float x = lines.getX(); x < lines.getRight(); x += 6.0f)
                    g.fillRect (x, y, 2.0f, 1.0f);
            }
        };
        switch (function)
        {
            case Function::Comp:
            {
                if (page != 0)
                {
                    // EDIT (p.76): the program's name over a rule; the gain curve
                    // beside the list; a rule over ON/OFF and PATCH.
                    const int prog = juce::jmax (0, pm.get (kCompType + processor));
                    paintEditHeader (g, area, juce::String (prog + 1) + "." + pm.programName (ProMix::Dynamics, prog), 0.78f);
                    g.fillRect (area.getX(), area.getY() + area.getHeight() * 0.79f, area.getWidth() * 0.8f, 1.5f);
                    g.setFont (lcdFont (type::kValue + 1.0f, true));
                    g.drawText ("PATCH:" + pm.text (kCompPatch + processor),
                                juce::Rectangle<float> (area.getX() + area.getWidth() * 0.34f, area.getY() + area.getHeight() * 0.8f, area.getWidth() * 0.4f,
                                                        area.getHeight() * 0.2f),
                                juce::Justification::centredLeft);
                    paintGainCurve (g, juce::Rectangle<float> (area.getX() + area.getWidth() * 0.5f, area.getY() + area.getHeight() * 0.2f,
                                                               area.getWidth() * 0.26f, area.getHeight() * 0.56f));
                }
                else
                {
                    // COMP and the processor's number, big and inverted; the rules under the setup and the list.
                    auto num = juce::Rectangle<float> (area.getX(), area.getY(), area.getWidth() * 0.18f, area.getHeight() * 0.34f).reduced (4.0f, 2.0f);
                    g.setColour (kPixel);
                    g.setFont (lcdFont (type::kValue + 3.0f));
                    g.drawText ("COMP", num.removeFromTop (num.getHeight() * 0.36f), juce::Justification::centred);
                    const auto box = num.withSizeKeepingCentre (juce::jmin (num.getWidth(), num.getHeight() * 0.95f), num.getHeight() * 0.95f);
                    g.fillRect (box);
                    g.setColour (kLcd);
                    g.setFont (lcdFont (box.getHeight() * 0.9f, true));
                    g.drawText (juce::String (processor + 1), box, juce::Justification::centred);
                    g.setColour (kPixel);
                    g.fillRect (area.getX(), area.getY() + area.getHeight() * 0.35f, area.getWidth() * 0.78f, 1.5f);
                    g.fillRect (area.getX(), area.getY() + area.getHeight() * 0.78f, area.getWidth() * 0.78f, 1.5f);
                }
                // At the right, the processor's gain reduction (top down, p.72) and
                // its output level, on what it's patched to.
                const int patch = pm.get (kCompPatch + processor);
                auto& e = ctx.engine;
                auto pick = [] (const MixerEngine::StripList& l, int k) -> Strip* { return juce::isPositiveAndBelow (k, (int) l.size()) ? l[(size_t) k].get() : nullptr; };
                Strip* st = patch >= 1 && patch <= 16 ? pick (e.getChannels(), patch - 1) : patch == 17 ? pick (e.getBuses(), 2)
                          : patch == 18 ? pick (e.getBuses(), 3) : patch == 19 ? pick (e.getMains(), 0) : nullptr;
                const float gr = st != nullptr ? std::abs (st->meters.dynReductionDb.load()) : 0.0f;
                const float out = juce::Decibels::gainToDecibels (st != nullptr ? ctx.meters.get (st, MeterPoint::Output, 0) : 0.0f, -100.0f);
                auto m = juce::Rectangle<float> (area.getX() + area.getWidth() * 0.8f, area.getY(), area.getWidth() * 0.2f, area.getHeight() * 0.76f);
                g.setColour (kPixel);
                g.fillRect (m.getX() - 4.0f, m.getY(), 1.5f, m.getHeight());
                auto labels = m.removeFromBottom (18.0f);
                auto bars = m.withTrimmedLeft (m.getWidth() * 0.42f).reduced (2.0f, 4.0f);
                paintScale (m.withWidth (m.getWidth() * 0.4f).withTrimmedTop (4.0f).withTrimmedBottom (4.0f), bars);
                g.setColour (kPixel);
                g.setFont (lcdFont (type::kLabel, true));
                g.drawText ("GR OUT", labels.withTrimmedLeft (m.getWidth() * 0.36f), juce::Justification::centred);
                auto grBar = bars.removeFromLeft (bars.getWidth() / 2.0f).reduced (bars.getWidth() * 0.15f, 0.0f);
                auto outBar = bars.reduced (bars.getWidth() * 0.15f, 0.0f);
                g.fillRect (grBar.withHeight (grBar.getHeight() * juce::jlimit (0.0f, 1.0f, gr / 30.0f)));
                // OUT on the scale's segments (0 at -20 dBFS: METER's).
                const float marks[] = { -60.0f, -44.0f, -38.0f, -32.0f, -26.0f, -20.0f, -17.0f, -14.0f, -11.0f, -8.0f, -5.0f, 0.0f };
                int lit = 0;
                for (float mark : marks)
                    lit += out >= mark ? 1 : 0;
                g.fillRect (outBar.withTop (outBar.getBottom() - outBar.getHeight() * (float) lit / 12.0f));
                if (focus >= 0 && focus < (int) fields.size())
                {
                    // STORE's choice, waiting for ENTER.
                    const auto& f = fields[(size_t) focus];
                    if (f.style == Field::Style::Button && f.textOf && f.pending >= 0)
                    {
                        g.setFont (lcdFont (type::kLabel, true));
                        g.drawFittedText ("> " + f.textOf (f.pending),
                                          juce::Rectangle<float> (area.getX() + area.getWidth() * 0.7f, area.getY() + area.getHeight() * 0.8f, area.getWidth() * 0.3f,
                                                                  area.getHeight() * 0.2f).toNearestInt(),
                                          juce::Justification::centredRight, 2, 0.6f);
                    }
                }
                break;
            }
            case Function::Pan:
                if (page == 0)
                {
                    paintLink ("ST IN");
                    // The rule; under it the selected channel's pan as a slider.
                    g.setColour (kPixel);
                    g.fillRect (area.getX(), area.getY() + area.getHeight() * 0.76f, area.getWidth(), 1.5f);
                    g.fillRect (area.getX() + area.getWidth() * 0.58f, area.getY() + area.getHeight() * 0.78f, 1.5f, area.getHeight() * 0.22f);
                    if (eqStrip < 16)
                    {
                        auto row = juce::Rectangle<float> (area.getX(), area.getY() + area.getHeight() * 0.8f, area.getWidth() * 0.56f, area.getHeight() * 0.2f);
                        g.setFont (lcdFont (type::kValue + 2.0f, true));
                        g.drawText (juce::String (eqStrip + 1), row.removeFromLeft (30.0f), juce::Justification::centredLeft);
                        auto value = row.removeFromRight (56.0f).reduced (2.0f, 3.0f);
                        g.fillRect (value);
                        g.setColour (kLcd);
                        g.drawText (pm.text (kChPan + eqStrip), value, juce::Justification::centred);
                        g.setColour (kPixel);
                        auto bar = row.reduced (6.0f, row.getHeight() * 0.3f);
                        g.drawRect (bar, 1.5f);
                        const int steps = juce::jmax (2, pm.steps (kChPan + eqStrip));
                        const float x = bar.getX() + bar.getWidth() * (float) juce::jmax (0, pm.get (kChPan + eqStrip)) / (float) (steps - 1);
                        g.fillRect (x - 2.0f, bar.getY() - 4.0f, 4.0f, bar.getHeight() + 8.0f);
                    }
                }
                break;
            case Function::Send1:
            case Function::Send2:
                if (page != 0)
                {
                    // EFFECT EDIT (p.35): the program's name over a rule, "(FADER = SEND1)"
                    // under the list.
                    const int prog = juce::jmax (0, pm.get (kFxType + (function == Function::Send1 ? 0 : 1)));
                    paintEditHeader (g, area, juce::String (prog + 1) + "." + pm.programName (ProMix::Effects, prog), 0.76f);
                    g.setFont (lcdFont (type::kValue + 1.0f, true));
                    g.drawText (function == Function::Send1 ? "(FADER = SEND1)" : "(FADER = SEND2)",
                                juce::Rectangle<float> (area.getX(), area.getY() + area.getHeight() * 0.78f, area.getWidth() * 0.76f, area.getHeight() * 0.2f),
                                juce::Justification::centred);
                }
                if (page == 0)
                {
                    // "(FADER = SEND1)" under the list, the rules; STORE's choice waiting for ENTER.
                    g.setColour (kPixel);
                    g.fillRect (area.getX(), area.getY() + area.getHeight() * 0.62f, area.getWidth() * 0.76f, 1.5f);
                    g.fillRect (area.getX(), area.getY() + area.getHeight() * 0.77f, area.getWidth(), 1.5f);
                    g.setFont (lcdFont (type::kValue + 1.0f, true));
                    g.drawText (function == Function::Send1 ? "(FADER = SEND1)" : "(FADER = SEND2)",
                                juce::Rectangle<float> (area.getX(), area.getY() + area.getHeight() * 0.62f, area.getWidth() * 0.76f, area.getHeight() * 0.15f),
                                juce::Justification::centred);
                    if (juce::isPositiveAndBelow (focus, (int) fields.size()))
                    {
                        const auto& f = fields[(size_t) focus];
                        if (f.style == Field::Style::Button && f.textOf && f.pending >= 0)
                            g.drawFittedText ("> " + f.textOf (f.pending),
                                              juce::Rectangle<float> (area.getX(), area.getY() + area.getHeight() * 0.62f, area.getWidth() * 0.76f, area.getHeight() * 0.15f).toNearestInt(),
                                              juce::Justification::centredRight, 1, 0.6f);
                    }
                }
                {
                    // The effect's return at the right (pp.32, 35): the CLIP ... -40
                    // scale, the L and R meters on its segments, "RTN1 L R" under them.
                    const auto& aux = ctx.engine.getAux();
                    const int fx = function == Function::Send1 ? 1 : 2;
                    auto* rtn = fx < (int) aux.size() ? aux[(size_t) fx].get() : nullptr;
                    auto m = juce::Rectangle<float> (area.getX() + area.getWidth() * 0.8f, area.getY(), area.getWidth() * 0.2f, area.getHeight() * 0.76f);
                    g.setColour (kPixel);
                    g.fillRect (m.getX() - 4.0f, m.getY(), 1.5f, m.getHeight());
                    auto labels = m.removeFromBottom (18.0f);
                    auto bars = m.withTrimmedLeft (m.getWidth() * 0.42f).reduced (2.0f, 4.0f);
                    paintScale (m.withWidth (m.getWidth() * 0.4f).withTrimmedTop (4.0f).withTrimmedBottom (4.0f), bars);
                    g.setColour (kPixel);
                    g.setFont (lcdFont (type::kLabel, true));
                    g.drawText (fx == 1 ? "RTN1 L R" : "RTN2 L R", labels, juce::Justification::centredRight);
                    // On the scale's segments (0 at -20 dBFS: METER's).
                    const float marks[] = { -60.0f, -44.0f, -38.0f, -32.0f, -26.0f, -20.0f, -17.0f, -14.0f, -11.0f, -8.0f, -5.0f, 0.0f };
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        auto col = bars.withWidth (bars.getWidth() / 2.0f).withX (bars.getX() + bars.getWidth() / 2.0f * (float) ch).reduced (bars.getWidth() * 0.08f, 0.0f);
                        const float db = juce::Decibels::gainToDecibels (rtn != nullptr ? ctx.meters.get (rtn, MeterPoint::Output, ch) : 0.0f, -100.0f);
                        int lit = 0;
                        for (float mark : marks)
                            lit += db >= mark ? 1 : 0;
                        g.fillRect (col.withTop (col.getBottom() - col.getHeight() * (float) lit / 12.0f));
                    }
                }
                break;
            case Function::Send3:
            case Function::Send4:
            {
                // "(FADER = SEND3)" over a rule (p.45); the send's level at the right
                // on the CLIP ... -40 scale, its name under it (both, as a stereo pair).
                const bool stereo = pm.get (kSend34Flag) == 1;
                const int send = function == Function::Send3 ? 2 : 3;
                g.setColour (kPixel);
                g.setFont (lcdFont (type::kValue + 1.0f, true));
                g.drawText (stereo ? "(FADER = SEND3-4)" : function == Function::Send3 ? "(FADER = SEND3)" : "(FADER = SEND4)",
                            juce::Rectangle<float> (area.getX(), area.getY() + area.getHeight() * 0.62f, area.getWidth() * 0.76f, area.getHeight() * 0.15f),
                            juce::Justification::centred);
                g.fillRect (area.getX(), area.getY() + area.getHeight() * 0.77f, area.getWidth(), 1.5f);
                auto m = juce::Rectangle<float> (area.getX() + area.getWidth() * 0.8f, area.getY(), area.getWidth() * 0.2f, area.getHeight() * 0.76f);
                g.fillRect (m.getX() - 4.0f, m.getY(), 1.5f, m.getHeight());
                auto labels = m.removeFromBottom (18.0f);
                auto bars = m.withTrimmedLeft (m.getWidth() * 0.42f).reduced (2.0f, 4.0f);
                paintScale (m.withWidth (m.getWidth() * 0.4f).withTrimmedTop (4.0f).withTrimmedBottom (4.0f), bars);
                g.setColour (kPixel);
                g.setFont (lcdFont (type::kLabel, true));
                g.drawText (stereo ? "SEND3-4" : send == 2 ? "SEND3" : "SEND4", labels, juce::Justification::centredRight);
                const auto& buses = ctx.engine.getBuses();
                auto* bus = juce::isPositiveAndBelow (stereo ? 2 : send, (int) buses.size()) ? buses[(size_t) (stereo ? 2 : send)].get() : nullptr;
                const float marks[] = { -60.0f, -44.0f, -38.0f, -32.0f, -26.0f, -20.0f, -17.0f, -14.0f, -11.0f, -8.0f, -5.0f, 0.0f };
                const int sides = stereo ? 2 : 1;
                for (int ch = 0; ch < sides; ++ch)
                {
                    auto col = bars.withWidth (bars.getWidth() / (float) sides).withX (bars.getX() + bars.getWidth() / (float) sides * (float) ch)
                                   .reduced (bars.getWidth() * 0.08f, 0.0f);
                    const float db = juce::Decibels::gainToDecibels (bus != nullptr ? ctx.meters.get (bus, MeterPoint::Output, ch) : 0.0f, -100.0f);
                    int lit = 0;
                    for (float mark : marks)
                        lit += db >= mark ? 1 : 0;
                    g.fillRect (col.withTop (col.getBottom() - col.getHeight() * (float) lit / 12.0f));
                }
                break;
            }
            case Function::Meter:
            {
                // The scale at the left, the meters, the other page's link; PEAK HOLD under them.
                auto m = area.withHeight (area.getHeight() * 0.78f).withWidth (area.getWidth() * 0.86f);
                auto scale = m.removeFromLeft (m.getWidth() * 0.1f);
                paintScale (scale.withTrimmedBottom (16.0f), m.withTrimmedBottom (16.0f));
                paintMeters (g, m);
                paintLink (page == 0 ? "ST IN" : "CH");
                g.setColour (kPixel);
                g.fillRect (area.getX(), area.getY() + area.getHeight() * 0.8f, area.getWidth(), 1.5f);
                break;
            }
            case Function::EqLow:
            case Function::EqMid:
            case Function::EqHigh:
            {
                paintEqCurve (g, area.withHeight (area.getHeight() * 0.44f));
                // The table: LOW, MID, HIGH across, G, F, Q down, framed.
                const auto table = juce::Rectangle<float> (area.getX() + area.getWidth() * 0.18f, area.getY() + area.getHeight() * 0.48f,
                                                           area.getWidth() * 0.82f, area.getHeight() * 0.52f);
                const float headH = area.getHeight() * 0.12f, rowH = area.getHeight() * 0.133f, colW = area.getWidth() * 0.25f;
                g.setColour (kPixel);
                g.drawRect (table, 1.5f);
                g.fillRect (table.getX(), table.getY() + headH, table.getWidth(), 1.0f);
                g.setFont (lcdFont (type::kValue + 1.0f, true));
                const char* bands[] = { "LOW", "MID", "HIGH" };
                for (int band = 0; band < 3; ++band)
                {
                    const float x = area.getX() + area.getWidth() * 0.25f + colW * (float) band;
                    g.drawText (bands[band], juce::Rectangle<float> (x, table.getY(), colW, headH), juce::Justification::centred);
                    g.fillRect (x, table.getY(), 1.0f, table.getHeight());
                }
                const char* rows[] = { "G", "F", "Q" };
                for (int r = 0; r < 3; ++r)
                {
                    auto cell = juce::Rectangle<float> (table.getX(), area.getY() + area.getHeight() * (0.6f + 0.133f * (float) r), area.getWidth() * 0.07f, rowH)
                                    .reduced (5.0f, 3.0f);
                    g.drawRect (cell, 1.0f);
                    g.drawText (rows[r], cell, juce::Justification::centred);
                }
                break;
            }
            case Function::Library:
                // The curve at the right, framed; the rule over STORE and RECALL.
                paintEqCurve (g, juce::Rectangle<float> (area.getX() + area.getWidth() * 0.66f, area.getY(), area.getWidth() * 0.34f, area.getHeight() * 0.74f));
                g.setColour (kPixel);
                g.drawRect (juce::Rectangle<float> (area.getX() + area.getWidth() * 0.66f, area.getY(), area.getWidth() * 0.34f, area.getHeight() * 0.74f), 1.5f);
                g.fillRect (area.getX(), area.getY() + area.getHeight() * 0.77f, area.getWidth(), 1.5f);
                break;
            case Function::Utility:
            case Function::Midi:
            {
                // The page's name in a banner, hatched both sides (pp.81-91).


                const juce::String name = page == 0 ? juce::String (function == Function::Utility ? "UTILITY MENU" : "MIDI MENU") : juce::String (function == Function::Utility ? kUtilityItems[juce::jlimit (0, 4, page - 1)] : kMidiItems[juce::jlimit (0, 5, page - 1)]);
                auto banner = area.withHeight (area.getHeight() * 0.15f);
                g.setFont (lcdFont (banner.getHeight() * 0.85f, true));
                const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + 16.0f;
                g.setColour (kPixel);
                g.drawText (name, banner.withSizeKeepingCentre (w, banner.getHeight()), juce::Justification::centred);
                for (const auto& side : { banner.withWidth ((banner.getWidth() - w) * 0.5f), banner.withTrimmedLeft ((banner.getWidth() + w) * 0.5f) })
                {
                    const auto hatch = side.reduced (0.0f, banner.getHeight() * 0.22f);
                    for (float x = hatch.getX(); x < hatch.getRight(); x += 3.0f)
                        for (float yy = hatch.getY() + std::fmod (x, 6.0f) * 0.5f; yy < hatch.getBottom(); yy += 3.0f)
                            g.fillRect (x, yy, 1.5f, 1.5f);
                }
                g.setFont (lcdFont (type::kValue + 2.0f, false));
                if (function == Function::Utility && page == 1)
                {
                    // ASSIGN over its three; the meters of SEND3, SEND4 and ST OUT.
                    g.drawText ("ASSIGN", juce::Rectangle<float> (area.getX(), area.getY() + area.getHeight() * 0.17f, area.getWidth() * 0.3f, area.getHeight() * 0.12f),
                                juce::Justification::centredLeft);
                    auto m = juce::Rectangle<float> (area.getX() + area.getWidth() * 0.52f, area.getY() + area.getHeight() * 0.2f, area.getWidth() * 0.36f, area.getHeight() * 0.8f);
                    auto labels = m.removeFromBottom (16.0f);
                    auto scale = m.removeFromLeft (m.getWidth() * 0.3f);
                    paintScale (scale, m);
                    auto& e = ctx.engine;
                    const auto& buses = e.getBuses();
                    const auto& mains = e.getMains();
                    Strip* strips[] = { buses.size() > 2 ? buses[2].get() : nullptr, buses.size() > 3 ? buses[3].get() : nullptr,
                                        mains.empty() ? nullptr : mains[0].get(), mains.empty() ? nullptr : mains[0].get() };
                    const float marks[] = { -60.0f, -44.0f, -38.0f, -32.0f, -26.0f, -20.0f, -17.0f, -14.0f, -11.0f, -8.0f, -5.0f, 0.0f };
                    const float bw = m.getWidth() / 4.0f;
                    for (int k = 0; k < 4; ++k)
                    {
                        const float db = juce::Decibels::gainToDecibels (strips[k] != nullptr ? ctx.meters.get (strips[k], MeterPoint::Output, k == 3 ? 1 : 0) : 0.0f, -100.0f);
                        int lit = 0;
                        for (float mark : marks)
                            lit += db >= mark ? 1 : 0;
                        auto bar = juce::Rectangle<float> (m.getX() + bw * (float) k, m.getY(), bw, m.getHeight()).reduced (bw * 0.2f, 0.0f);
                        g.setColour (kPixel);
                        g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * (float) lit / 12.0f));
                    }
                    g.setFont (lcdFont (type::kValue + 1.0f, true));
                    g.drawText ("34", labels.withWidth (bw * 2.0f).withX (m.getX()), juce::Justification::centred);
                    g.drawText ("ST", labels.withWidth (bw * 2.0f).withX (m.getX() + bw * 2.0f), juce::Justification::centred);
                }
                if (midiPage() == 0 && fields.size() > 3)
                {
                    // The table's heads and its kinds, ruled.
                    const char* heads[] = { "TX", "RX", "OMNI", "ECHO" };
                    const char* kinds[] = { "PROGRAM", "CONTROL", "BULK" };
                    g.setFont (lcdFont (type::kValue + 1.0f, true));
                    for (int s = 0; s < 4; ++s)
                    {
                        const auto r = juce::Rectangle<float> (area.getX() + area.getWidth() * (0.22f + 0.15f * (float) s), area.getY() + area.getHeight() * 0.31f,
                                                               area.getWidth() * 0.15f, area.getHeight() * 0.11f);
                        g.drawRect (r, 1.0f);
                        g.drawText (heads[s], r, juce::Justification::centred);
                    }
                    for (int k = 0; k < 3; ++k)
                    {
                        const auto r = juce::Rectangle<float> (area.getX() + area.getWidth() * 0.02f, area.getY() + area.getHeight() * (0.43f + 0.14f * (float) k),
                                                               area.getWidth() * 0.19f, area.getHeight() * 0.12f);
                        g.drawRect (r, 1.5f);
                        g.drawText (kinds[k], r, juce::Justification::centred);
                    }
                    g.drawRect (juce::Rectangle<float> (area.getX() + area.getWidth() * 0.22f, area.getY() + area.getHeight() * 0.42f, area.getWidth() * 0.6f, area.getHeight() * 0.42f), 1.5f);
                }
                if (midiPage() == 2)
                {
                    // PGM | MEM heads over the table's frame.
                    g.setFont (lcdFont (type::kValue + 1.0f, true));
                    for (int c = 0; c < 3; ++c)
                        g.drawText ("PGM | MEM", juce::Rectangle<float> (area.getX() + area.getWidth() * (0.08f + 0.27f * (float) c), area.getY() + area.getHeight() * 0.38f,
                                                                         area.getWidth() * 0.25f, area.getHeight() * 0.11f), juce::Justification::centredLeft);
                    g.drawRect (juce::Rectangle<float> (area.getX() + area.getWidth() * 0.05f, area.getY() + area.getHeight() * 0.49f, area.getWidth() * 0.82f, area.getHeight() * 0.5f), 1.5f);
                }
                if (midiPage() == 3)
                {
                    // NO. NAME BANK NO. over the table's frame.
                    g.setFont (lcdFont (type::kValue + 1.0f, true));
                    const std::pair<float, const char*> heads[] = { { 0.0f, "NO." }, { 0.2f, "NAME" }, { 0.66f, "BANK" }, { 0.81f, "NO." } };
                    for (auto [x, text] : heads)
                        g.drawText (text, juce::Rectangle<float> (area.getX() + area.getWidth() * x, area.getY() + area.getHeight() * 0.18f, area.getWidth() * 0.15f,
                                                                  area.getHeight() * 0.11f), juce::Justification::centred);
                    g.drawRect (juce::Rectangle<float> (area.getX(), area.getY() + area.getHeight() * 0.29f, area.getWidth() * 0.97f, area.getHeight() * 0.7f), 1.5f);
                }
                break;
            }
            case Function::Group:
            {
                g.setColour (kPixel);
                g.setFont (lcdFont (type::kValue + 1.0f, true));
                g.drawText ("GRP", juce::Rectangle<float> (area.getX(), area.getY(), area.getWidth() * 0.08f, area.getHeight() * 0.16f), juce::Justification::centredLeft);
                paintGroups (g, juce::Rectangle<float> (area.getX(), area.getY() + area.getHeight() * 0.17f, area.getWidth() * 0.78f, area.getHeight() * 0.62f));
                g.fillRect (area.getX(), area.getY() + area.getHeight() * 0.81f, area.getWidth(), 1.5f);
                g.fillRect (area.getX() + area.getWidth() * 0.79f, area.getY(), 1.5f, area.getHeight() * 0.81f);
                break;
            }
            case Function::Pair:
                if (juce::isPositiveAndBelow (focus, (int) fields.size()))
                {
                    // "CH1-2 :MONO x 2", the setting inverted.
                    const auto& f = fields[(size_t) focus];
                    const auto value = f.pending >= 0 && f.textOf ? f.textOf (f.pending) : f.text();
                    auto line = juce::Rectangle<float> (area.getX() + area.getWidth() * 0.2f, area.getY() + area.getHeight() * 0.7f, area.getWidth() * 0.7f, area.getHeight() * 0.24f);
                    g.setColour (kPixel);
                    g.setFont (lcdFont (type::kValue + 4.0f, false));
                    const auto name = "CH" + juce::String (2 * f.index + 1) + "-" + juce::String (2 * f.index + 2) + "  :";
                    g.drawText (name, line.removeFromLeft (line.getWidth() * 0.4f), juce::Justification::centredRight);
                    auto v = line.withWidth (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), value) + 14.0f);
                    g.fillRect (v);
                    g.setColour (kLcd);
                    g.drawText (value, v, juce::Justification::centred);
                }
                break;
            case Function::Phase:
            {
                paintLink ("PAN");
                g.setColour (kPixel);
                g.fillRect (area.getX(), area.getY() + area.getHeight() * 0.78f, area.getWidth(), 1.5f);
                g.setFont (lcdFont (type::kValue + 2.0f, false));
                auto legend = juce::Rectangle<float> (area.getX(), area.getY() + area.getHeight() * 0.79f, area.getWidth(), area.getHeight() * 0.21f);
                g.drawText ("N=Normal Phase", legend.removeFromTop (legend.getHeight() * 0.5f), juce::Justification::centred);
                g.drawText ("R=Reverse Phase", legend, juce::Justification::centred);
                break;
            }
            case Function::Cue:
            {
                const int e = eqStrip;
                const bool input = e <= 16, stOut = e == 19;
                g.setColour (kPixel);
                g.setFont (lcdFont (type::kValue, true));
                // PHASE and PAN (BAL on ST OUT) as headings over their icons; an
                // input's PRE / POST over its sends.
                if (input)
                    g.drawText ("PHASE", juce::Rectangle<float> (area.getX() + area.getWidth() * 0.11f, area.getY(), area.getWidth() * 0.1f, 14.0f), juce::Justification::centred);
                g.drawText (stOut ? "BAL" : "PAN", juce::Rectangle<float> (area.getX() + area.getWidth() * 0.22f, area.getY(), area.getWidth() * 0.1f, 14.0f),
                            juce::Justification::centred);
                if (input)
                    for (int k = 0; k < 4; ++k)
                        g.drawText (pm.text (kSendPre + k), juce::Rectangle<float> (area.getX() + area.getWidth() * (0.08f + 0.07f * (float) k), area.getY() + area.getHeight() * 0.3f,
                                                                                   area.getWidth() * 0.07f, area.getHeight() * 0.12f), juce::Justification::centred);
                // COMPn:ON under CUE: when a processor is patched to the strip.
                const int patchOf = e < 16 ? e + 1 : stOut ? 19 : -1;
                int patched = -1;
                for (int k = 0; k < 3; ++k)
                    if (patchOf > 0 && pm.get (kCompPatch + k) == patchOf)
                        patched = k;
                if (patched >= 0)
                    g.drawText ("COMP" + juce::String (patched + 1) + ":" + pm.text (kCompOn + patched),
                                juce::Rectangle<float> (area.getX() + area.getWidth() * 0.36f, area.getY() + area.getHeight() * 0.15f, area.getWidth() * 0.4f, area.getHeight() * 0.14f),
                                juce::Justification::centredLeft);
                g.fillRect (area.getX(), area.getY() + area.getHeight() * 0.29f, area.getWidth() * 0.82f, 1.5f);
                g.fillRect (area.getX() + area.getWidth() * 0.345f, area.getY() + area.getHeight() * 0.3f, 1.5f, area.getHeight() * 0.7f);
                g.fillRect (area.getX() + area.getWidth() * 0.82f, area.getY(), 1.5f, area.getHeight());
                if (e < 20)
                    paintEqCurve (g, juce::Rectangle<float> (area.getX() + area.getWidth() * 0.36f, area.getY() + area.getHeight() * 0.32f, area.getWidth() * 0.44f, area.getHeight() * 0.5f));
                // The meters at the right: the strip's (ST IN's and ST OUT's two), a
                // patched processor's GR beside them; the strip's name under them.
                auto& eng = ctx.engine;
                auto pick = [] (const MixerEngine::StripList& l, int k) -> Strip* { return juce::isPositiveAndBelow (k, (int) l.size()) ? l[(size_t) k].get() : nullptr; };
                Strip* st = e < 16 ? pick (eng.getChannels(), e) : e <= 18 ? pick (eng.getAux(), e - 16) : pick (eng.getMains(), 0);
                const int sides = e == 16 || stOut ? 2 : 1;
                auto m = juce::Rectangle<float> (area.getX() + area.getWidth() * 0.84f, area.getY(), area.getWidth() * 0.16f, area.getHeight() * 0.8f);
                auto caption = m.removeFromBottom (16.0f);
                auto bars = m.withTrimmedLeft (m.getWidth() * 0.5f).reduced (2.0f, 4.0f);
                paintScale (m.withWidth (m.getWidth() * 0.48f).withTrimmedTop (4.0f).withTrimmedBottom (4.0f), bars);
                const float marks[] = { -60.0f, -44.0f, -38.0f, -32.0f, -26.0f, -20.0f, -17.0f, -14.0f, -11.0f, -8.0f, -5.0f, 0.0f };
                const int cols = sides + (patched >= 0 ? 1 : 0);
                g.setColour (kPixel);
                for (int c = 0; c < cols; ++c)
                {
                    auto col = bars.withWidth (bars.getWidth() / (float) cols).withX (bars.getX() + bars.getWidth() / (float) cols * (float) c).reduced (1.5f, 0.0f);
                    if (patched >= 0 && c == 0)
                    {
                        // GR, from the top (p.72).
                        const float gr = st != nullptr ? std::abs (st->meters.dynReductionDb.load()) : 0.0f;
                        g.fillRect (col.withHeight (col.getHeight() * juce::jlimit (0.0f, 1.0f, gr / 30.0f)));
                        continue;
                    }
                    const int ch = c - (patched >= 0 ? 1 : 0);
                    const float db = juce::Decibels::gainToDecibels (st != nullptr ? ctx.meters.get (st, input ? MeterPoint::Input : MeterPoint::Output, ch) : 0.0f, -100.0f);
                    int lit = 0;
                    for (float mark : marks)
                        lit += db >= mark ? 1 : 0;
                    g.fillRect (col.withTop (col.getBottom() - col.getHeight() * (float) lit / 12.0f));
                }
                const juce::String name = e < 16 ? juce::String (e + 1) : e == 16 ? juce::String ("STIN") : e <= 18 ? "RTN" + juce::String (e - 16) : juce::String ("L R");
                g.setFont (lcdFont (type::kLabel, true));
                g.drawText ((patched >= 0 ? "GR" + juce::String (patched + 1) + " " : juce::String()) + name, caption, juce::Justification::centredRight);
                break;
            }
            case Function::MemoryStore:
            case Function::MemoryRecall:
            case Function::TitleEdit:
            {
                // The "Press RECALL to recall." line (pp.63-65, 22), its three bars both sides.
                auto press = [&g] (juce::Rectangle<float> r, const juce::String& text)
                {
                    g.setColour (kPixel);
                    g.setFont (lcdFont (type::kValue + 2.0f, true));
                    const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 12.0f;
                    g.drawText (text, r.withSizeKeepingCentre (w, r.getHeight()), juce::Justification::centred);
                    for (const float x : { r.getCentreX() - w * 0.5f - 14.0f, r.getCentreX() + w * 0.5f + 2.0f })
                        for (int k = 0; k < 3; ++k)
                            g.fillRect (x, r.getCentreY() - 5.0f + 4.0f * (float) k, 12.0f, 1.5f);
                };
                const bool title = function == Function::TitleEdit;
                const auto top = area.withHeight (area.getHeight() * 0.18f);
                if (! title)
                {
                    const bool store = function == Function::MemoryStore;
                    press (top, store ? "Press \"STORE\" to store." : "Press \"RECALL\" to recall.");
                    // The list's rule at its right; PROTECT over it when the chosen memory's group is protected.
                    g.fillRect (area.getX() + area.getWidth() * 0.48f, area.getY() + area.getHeight() * 0.22f, 1.5f, area.getHeight() * 0.78f);
                    const int m = chosenMemory ? chosenMemory() : 0;
                    if (m > 0 && isProtected && isProtected (m <= 25 ? 0 : 1))
                    {
                        auto badge = juce::Rectangle<float> (area.getX() + area.getWidth() * 0.06f, area.getY() + area.getHeight() * 0.19f, area.getWidth() * 0.22f, 15.0f);
                        g.fillRect (badge);
                        g.setColour (kLcd);
                        g.setFont (lcdFont (type::kCaption, true));
                        g.drawText ("PROTECT", badge, juce::Justification::centred);
                    }
                    break;
                }
                press (top, "Press \"ENTER\" to store.");
                // The characters around the cursor's, scrolling in a box (its own inverted).
                const int at = juce::isPositiveAndBelow (focus, (int) fields.size()) && fields[(size_t) focus].label.isEmpty() ? fields[(size_t) focus].get() : -1;
                auto strip = juce::Rectangle<float> (area.getX() + area.getWidth() * 0.3f, area.getY() + area.getHeight() * 0.24f, area.getWidth() * 0.46f, area.getHeight() * 0.2f);
                g.setColour (kPixel);
                g.drawRect (strip, 1.5f);
                const int span = 15;
                const float cw = strip.getWidth() / (float) span;
                g.setFont (lcdFont (type::kValue + 2.0f, true));
                for (int k = 0; k < span; ++k)
                {
                    const int c = at < 0 ? k : at - span / 2 + k;
                    if (c < 0 || c >= kTitleChars.length())
                        continue;
                    auto cell = juce::Rectangle<float> (strip.getX() + cw * (float) k, strip.getY(), cw, strip.getHeight()).reduced (0.0f, 3.0f);
                    g.setColour (kPixel);
                    if (c == at)
                        g.fillRect (cell);
                    g.setColour (c == at ? kLcd : kPixel);
                    g.drawText (juce::String::charToString (kTitleChars[c]), cell, juce::Justification::centred);
                }
                // The number before the name: "31. " (a program), "01." (a memory).
                g.setColour (kPixel);
                const auto number = nameLibrary >= 0 ? juce::String (nameProgram + 1) + ". " : juce::String (titleMemory).paddedLeft ('0', 2) + ".";
                g.drawText (number, juce::Rectangle<float> (area.getX(), area.getY() + area.getHeight() * 0.5f, area.getWidth() * 0.3f - 2.0f, area.getHeight() * 0.2f),
                            juce::Justification::centredRight);
                break;
            }
            default:
                break;
        }
        for (size_t i = 0; i < fields.size(); ++i)
            if (! boxOf (fields[i]).isEmpty())
                paintField (g, fields[i], (int) i == focus);
        // The LCD's dot matrix: 240 x 64 dots, the gaps between them in the backlight.
        {
            const auto screen = b.reduced (6.0f);
            const float pitchX = screen.getWidth() / 240.0f, pitchY = screen.getHeight() / 64.0f;
            g.setColour (kLcd.withAlpha (0.42f));
            for (int x = 1; x < 240; ++x)
                g.fillRect (screen.getX() + pitchX * (float) x - 0.35f, screen.getY(), 0.7f, screen.getHeight());
            for (int y = 1; y < 64; ++y)
                g.fillRect (screen.getX(), screen.getY() + pitchY * (float) y - 0.35f, screen.getWidth(), 0.7f);
        }
        // At the right edge, the arrow at what the RTN / SEND strip is.
        if (rtnSendShown)
        {
            const int k = juce::jlimit (0, 3, rtnSendShown());
            const float y = b.getY() + b.getHeight() * kRightRows[k], x = b.getRight() - 12.0f;
            juce::Path arrow;
            arrow.addTriangle (x - 7.0f, y - 5.0f, x - 7.0f, y + 5.0f, x + 1.0f, y);
            g.setColour (kPixel);
            g.fillPath (arrow);
        }
    }

    void Display::mouseDown (const juce::MouseEvent& e)
    {
        dragY = (float) e.y;
        for (size_t i = 0; i < fields.size(); ++i)
            if (boxOf (fields[i]).contains (e.position))
            {
                focus = (int) i;
                repaint();
                return;
            }
        if (function == Function::Meter || (function == Function::Pan && (page == 1 || pageLink().contains (e.position))))
            setPage (page == 0 ? 1 : 0);
    }

    void Display::mouseDrag (const juce::MouseEvent& e)
    {
        const float per = 12.0f;
        const int d = juce::roundToInt ((dragY - (float) e.y) / per);
        if (d != 0)
        {
            turn (d);
            dragY -= (float) d * per;
        }
    }

    void Display::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        turn (w.deltaY > 0 ? 1 : -1);
    }
} // namespace promix

//==============================================================================
ProMixSurface::ProMixSurface (UiContext& c)
    : ctx (c), theme (Theme::dark()), pm (c.engine.getProMix()), universalKey ("UNIVERSAL MIXER", theme.accent, theme), display (c, theme)
{
    using promix::Function;
    for (int i = 0; i < kNumFaders; ++i)
    {
        faders[(size_t) i] = std::make_unique<promix::Fader> (ctx, theme);
        faders[(size_t) i]->isLocal = [this] { return display.isLocalOff(); };
        if (i < 16)
            faders[(size_t) i]->onLocal = [this, i] (int v) { display.localMoved (i, v); };
        addAndMakeVisible (*faders[(size_t) i]);
        auto& sel = selKeys[(size_t) i];
        sel = std::make_unique<ToggleKey> ("SEL", kSelLed, theme);
        sel->onClick = [this, i] { pressSel (i); };
        sel->isOn = [this, i]
        {
            if (display.getFunction() == Function::Group && i < 17)
            {
                // BAL ADJ: the group's [SEL] keys flash.
                const bool in = pm.get (kGroup + i) == pm.get (kGroupSelect) + 1;
                return in && (! pm.balAdj() || (juce::Time::getMillisecondCounter() / 300) % 2 == 0);
            }
            if (display.getFunction() == Function::Cue)
                return pm.get (cueParam (i)) == 1;
            if (i < 16)
                return display.getEqStrip() == i;
            return i == 16 ? display.getEqStrip() == 16 : i == 18 ? display.getEqStrip() == 19 : rtnSend < 2 && display.getEqStrip() == 17 + rtnSend;
        };
        addAndMakeVisible (*sel);
        auto& on = onKeys[(size_t) i];
        on = std::make_unique<ToggleKey> ("ON", kOnLed, theme);
        on->onClick = [this, i] { pm.set (onParam (i), pm.get (onParam (i)) == 1 ? 0 : 1); };
        on->isOn = [this, i] { return pm.get (onParam (i)) == 1; };
        addAndMakeVisible (*on);
    }
    const std::pair<const char*, Function> keys[] = { { "METER", Function::Meter },     { "PAN / PHASE", Function::Pan },
                                                      { "COMP", Function::Comp },       { "CUE", Function::Cue },
                                                      { "SEND1", Function::Send1 },     { "SEND2", Function::Send2 },
                                                      { "SEND3", Function::Send3 },     { "SEND4", Function::Send4 },
                                                      { "EQ LOW", Function::EqLow },    { "EQ MID", Function::EqMid },
                                                      { "EQ HIGH", Function::EqHigh },  { "LIBRARY", Function::Library },
                                                      { "GROUP", Function::Group },     { "PAIR", Function::Pair },
                                                      { "UTILITY", Function::Utility }, { "MIDI", Function::Midi } };
    for (auto [name, f] : keys)
    {
        auto key = std::make_unique<ToggleKey> (name, kSelLed, theme);
        // The panel's groups: METER ... CUE orange, SEND1-4 blue, EQ and LIBRARY green, the rest cream.
        const int n = (int) functionKeys.size();
        // Blank keys without LEDs, their names printed over them (SEND 1, 2, 3, 4...).
        static const char* const printed[] = { "METER", "PAN/\xc3\xb8", "COMP", "CUE", "SEND 1", "2", "3", "4",
                                               "EQ LOW", "MID", "HIGH", "LIBRARY", "GROUP", "PAIR", "UTILITY", "MIDI" };
        key->labelAbove = true;
        key->showLed = false;
        key->belowText = juce::String::fromUTF8 (printed[n]);
        key->face = n < 4 ? kOrangeKey : n < 8 ? kBlueKey : n < 12 ? kGreenKey : juce::Colour();
        key->onClick = [this, fn = f] { pressFunction (fn); };
        key->isOn = [this, fn = f]
        {
            const auto now = display.getFunction();
            return now == fn || (fn == Function::Pan && now == Function::Phase);
        };
        addAndMakeVisible (*key);
        functionKeys.emplace_back (f, std::move (key));
    }
    auto utf8 = [] (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); };
    const std::pair<juce::String, std::function<void()>> panel[] = {
        { "STORE", [this] { pressStore(); } },
        { "RECALL", [this] { pressRecall(); } },
        { "INC +", [this] { setMemory (memory + 1); if (display.getFunction() >= promix::Function::MemoryStore) display.setPage (0); } },
        { "DEC -", [this] { setMemory (memory - 1); if (display.getFunction() >= promix::Function::MemoryStore) display.setPage (0); } },
        { utf8 ("\xe2\x97\x80"), [this] { display.cursor (-1); } },
        { utf8 ("\xe2\x96\xb2"), [this] { display.cursor (-1); } },
        { utf8 ("\xe2\x96\xbc"), [this] { display.cursor (1); } },
        { utf8 ("\xe2\x96\xb6"), [this] { display.cursor (1); } },
        { "ENTER", [this] { display.enter(); } },
    };
    for (auto& [name, action] : panel)
    {
        auto key = std::make_unique<ToggleKey> (name, kSelLed, theme);
        key->onClick = action;
        key->showLed = false;
        // SCENE MEMORY's keys: blank, named over them; ENTER an oval, the cursor keys triangles.
        const int n = (int) panelKeys.size();
        using Shape = ToggleKey::Shape;
        key->labelAbove = n < 4;
        key->shape = n == 4 ? Shape::Left : n == 5 ? Shape::Up : n == 6 ? Shape::Down : n == 7 ? Shape::Right : n == 8 ? Shape::Oval : Shape::Rect;
        key->setTitle (name);
        addAndMakeVisible (*key);
        panelKeys.push_back (std::move (key));
    }
    // The PARAMETER wheel: an endless rotary; each detent a step.
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
    wheel->setTitle ("PARAMETER");
    addAndMakeVisible (*wheel);

    display.memoryName = [this]
    {
        // The MEMORY area: the title of the scene last recalled (scene 00's InitData).
        const int current = ctx.show.getCurrentSceneIndex();
        auto scene = current >= 0 ? ctx.show.getScene (current) : std::nullopt;
        return current >= 0 && scene.has_value() ? scene->name : juce::String ("InitData");
    };
    display.memoryEdited = [this]
    {
        // EDIT: the Edit Buffer no longer what the last recall or store left.
        const int current = ctx.show.getCurrentSceneIndex();
        auto now = pm.getSceneBytes();
        if (current != recalledIndex)
        {
            recalledIndex = current;
            recalledBytes = std::move (now);
            return false;
        }
        return now != recalledBytes;
    };
    // STORE's and RECALL's lists, TITLE EDIT, MEMORY PROTECT.
    display.memoryTitle = [this] (int m) -> juce::String
    {
        if (m == 0)
            return "InitData";
        auto scene = m - 1 < ctx.show.getNumScenes() ? ctx.show.getScene (m - 1) : std::nullopt;
        return scene.has_value() ? scene->name : juce::String();
    };
    display.chosenMemory = [this] { return memory; };
    display.chooseMemory = [this] (int m) { setMemory (m); };
    display.storeChosen = [this]
    {
        if (memory < 1 || isProtected (memory <= 25 ? 0 : 1))
            return; // 00 is read-only; a protected memory refuses STORE (p.65)
        const int index = juce::jmin (memory - 1, ctx.show.getNumScenes());
        store();
        auto scene = ctx.show.getScene (index);
        display.editTitle (index + 1, scene.has_value() ? scene->name : juce::String());
    };
    display.recallChosen = [this] { recall(); };
    display.setMemoryTitle = [this] (int m, const juce::String& title)
    {
        if (juce::isPositiveAndBelow (m - 1, ctx.show.getNumScenes()))
            ctx.show.setSceneName (m - 1, title);
    };
    display.isProtected = [this] (int half) { return isProtected (half); };
    display.setProtected = [this] (int half, bool on)
    {
        juce::StringArray v;
        v.addTokens (ctx.settings.getValue (protectKey, "0 0"), " ", "");
        while (v.size() < 2)
            v.add ("0");
        v.set (half, on ? "1" : "0");
        ctx.settings.setValue (protectKey, v.joinIntoString (" "));
    };
    display.rtnSendShown = [this] { return rtnSend; };
    // The user programs: the console's memory, kept in the app settings.
    pm.setLibraryText (ctx.settings.getValue (libraryKey));
    display.libraryChanged = [this] { ctx.settings.setValue (libraryKey, pm.getLibraryText()); };
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
    // The panel's keys are the ProMix's: rubber with an LED.
    for (auto* child : getChildren())
        if (auto* k = dynamic_cast<ToggleKey*> (child); k != nullptr && k != &universalKey)
            k->look = &look();
    refreshAll();
    startTimerHz (30);
}

ProMixSurface::~ProMixSurface()
{
    stopTimer();
}

Strip* ProMixSurface::stripOf (int i) const
{
    auto pick = [] (const MixerEngine::StripList& l, int k) -> Strip* { return juce::isPositiveAndBelow (k, (int) l.size()) ? l[(size_t) k].get() : nullptr; };
    if (i < 16)
        return pick (ctx.engine.getChannels(), i);
    if (i == 16)
        return pick (ctx.engine.getAux(), 0);
    if (i == 17)
        return rtnSend < 2 ? pick (ctx.engine.getAux(), 1 + rtnSend) : pick (ctx.engine.getBuses(), rtnSend);
    return pick (ctx.engine.getMains(), 0);
}

int ProMixSurface::levelParam (int i) const
{
    using promix::Function;
    const auto f = display.getFunction();
    const int send = f == Function::Send1 ? 0 : f == Function::Send2 ? 1 : f == Function::Send3 ? 2 : f == Function::Send4 ? 3 : -1;
    if (send >= 0 && i <= 16)
    {
        // The faders are the inputs' sends (p.25); in SEND3-4 stereo SEND4's are SEND3's.
        const int bus = send == 3 && pm.get (kSend34Flag) == 1 ? 2 : send;
        return kSendLevel + 17 * bus + i;
    }
    if (i < 17)
        return i;
    if (i == 17)
        return 17 + rtnSend; // RTN1, RTN2, SEND3, SEND4 (17-20)
    return 21;
}

int ProMixSurface::onParam (int i) const
{
    return i < 16 ? kChOn + i : i == 16 ? 38 : i == 17 ? 39 + rtnSend : 43;
}

int ProMixSurface::cueParam (int i) const
{
    return kCue + (i < 17 ? i : i == 17 ? 17 + rtnSend : 21);
}

void ProMixSurface::bindFaders()
{
    for (int i = 0; i < kNumFaders; ++i)
        faders[(size_t) i]->setParam (levelParam (i));
}

void ProMixSurface::pressFunction (promix::Function f)
{
    using promix::Function;
    if (f == Function::Comp && display.getFunction() == Function::Comp)
    {
        // COMP pressed again: the next processor (p.72); its EDIT page by the cursor.
        display.setProcessor (display.getProcessor() + 1);
    }
    else
    {
        display.setFunction (f);
    }
    // SEND1-2 put RTN1-2 on the RTN / SEND fader, SEND3-4 their masters (p.33, p.45).
    if (f == Function::Send1 || f == Function::Send2 || f == Function::Send3 || f == Function::Send4)
        rtnSend = f == Function::Send1 ? 0 : f == Function::Send2 ? 1 : f == Function::Send3 ? 2 : 3;
    bindFaders();
}

void ProMixSurface::pressSel (int i)
{
    using promix::Function;
    const auto f = display.getFunction();
    if (display.isLocalOff())
    {
        // LOCAL OFF: CH1-16 [SEL] send program changes (p.86).
        if (i < 16)
            display.localSelected (i);
        return;
    }
    if (f == Function::Group && i < 17)
    {
        // A channel in or out of the group shown (p.56).
        const int g = pm.get (kGroupSelect) + 1;
        pm.set (kGroup + i, pm.get (kGroup + i) == g ? 0 : g);
        return;
    }
    if (f == Function::Cue)
    {
        pm.set (cueParam (i), pm.get (cueParam (i)) == 1 ? 0 : 1);
        return;
    }
    if (i == 17)
    {
        // RTN / SEND SEL: RTN1, RTN2, SEND3, SEND4 in turn (p.25).
        rtnSend = (rtnSend + 1) % 4;
        bindFaders();
        if (rtnSend < 2)
            display.setEqStrip (17 + rtnSend);
    }
    else
    {
        display.setEqStrip (i < 16 ? i : i == 16 ? 16 : 19);
    }
    if (auto* s = stripOf (i); s != nullptr && ctx.selectStrip)
        ctx.selectStrip (s);
}

bool ProMixSurface::isProtected (int half) const
{
    juce::StringArray v;
    v.addTokens (ctx.settings.getValue (protectKey, "0 0"), " ", "");
    return v[juce::jlimit (0, 1, half)] == "1";
}

void ProMixSurface::pressStore()
{
    if (display.getFunction() == promix::Function::MemoryStore)
        display.enter();
    else
        display.setFunction (promix::Function::MemoryStore);
}

void ProMixSurface::pressRecall()
{
    if (display.getFunction() == promix::Function::MemoryRecall)
        display.enter();
    else
        display.setFunction (promix::Function::MemoryRecall);
}

void ProMixSurface::store()
{
    // The show's scene `memory` (scene 00 is read-only: p.62); a new one appended
    // when the show has fewer.
    if (memory < 1)
        return;
    const int index = memory - 1;
    if (index < ctx.show.getNumScenes())
        ctx.show.storeScene (index);
    else
        ctx.show.addScene ("SCENE " + juce::String (memory).paddedLeft ('0', 2));
    recalledIndex = -2; // EDIT off
    display.refresh();
}

void ProMixSurface::recall()
{
    if (memory == 0)
        pm.recallInitial();
    else if (memory - 1 < ctx.show.getNumScenes())
        ctx.show.recallScene (memory - 1);
    recalledIndex = -2; // EDIT off
    display.refresh();
}

void ProMixSurface::refreshAll()
{
    bindFaders();
    selectionChanged();
    timerCallback(); // the keys lit as they are now, not a tick later
}

void ProMixSurface::selectionChanged()
{
    if (! ctx.getSelectedStrip)
        return;
    auto* s = ctx.getSelectedStrip();
    if (s == nullptr)
        return;
    const int n = s->getNumber() - 1;
    switch (s->getKind())
    {
        case StripKind::Channel: display.setEqStrip (juce::jlimit (0, 15, n)); break;
        case StripKind::Aux: display.setEqStrip (n == 0 ? 16 : juce::jlimit (17, 18, 16 + n)); break;
        case StripKind::Main: display.setEqStrip (19); break;
        case StripKind::Bus:
        case StripKind::Matrix: break;
    }
}

void ProMixSurface::timerCallback()
{
    for (int i = 0; i < kNumFaders; ++i)
    {
        faders[(size_t) i]->refresh();
        selKeys[(size_t) i]->refresh();
        onKeys[(size_t) i]->refresh();
    }
    for (auto& [f, key] : functionKeys)
        key->refresh();
    display.refresh();
    repaintDesign (meterArea());
    repaintDesign (memoryArea());
}

juce::Rectangle<int> ProMixSurface::meterArea() const
{
    return meterRect;
}

juce::Rectangle<int> ProMixSurface::memoryArea() const
{
    // The scene number is on the LCD's MEMORY area.
    return display.getBounds();
}

void ProMixSurface::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    g.addTransform (design);
    {
        auto body = designBounds().withTrimmedTop (kTopBar).toFloat();
        // The silver rack ears, then the panel.
        for (auto ear : { body.removeFromLeft (14.0f), body.removeFromRight (14.0f) })
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd2d5d8), ear.getX(), ear.getY(), juce::Colour (0xff8f9396), ear.getRight(),
                                                     ear.getY(), false));
            g.fillRect (ear);
        }
        hw::panel (g, body, look());
        // The vent above the faders: a grille of fine slots across the strips.
        if (! grilleArea.isEmpty())
        {
            const auto gr = grilleArea.toFloat();
            g.setColour (juce::Colour (0xff2a2c2f));
            g.fillRect (gr);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            for (float y = gr.getY() + 3.0f; y < gr.getBottom() - 2.0f; y += 4.0f)
                g.fillRect (gr.getX() + 4.0f, y, gr.getWidth() - 8.0f, 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawRect (gr, 1.0f);
        }
        // The raised outline the PARAMETER wheel and the cursor keys sit on,
        // its lower left edge slanting down to the right.
        {
            const auto w = wheel->getBounds().toFloat().expanded (14.0f);
            const auto k = panelKeys[4]->getBounds().getUnion (panelKeys[7]->getBounds()).toFloat();
            juce::Path raised;
            raised.startNewSubPath (k.getX() - 14.0f, w.getY());
            raised.lineTo (w.getRight(), w.getY());
            raised.lineTo (w.getRight(), w.getBottom() - 30.0f);
            raised.lineTo (w.getCentreX(), (float) panelKeys[6]->getBottom() + 22.0f);
            raised.lineTo (k.getX() - 14.0f, (float) panelKeys[6]->getBottom() + 22.0f);
            raised.closeSubPath();
            raised = raised.createPathWithRoundedCorners (16.0f);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillPath (raised);
            g.setColour (look().silkDim.withAlpha (0.45f));
            g.strokePath (raised, juce::PathStrokeType (1.2f));
        }
        // The PARAMETER wheel: a big knurled knob.
        hw::knob (g, wheel->getBounds().toFloat(), (float) std::fmod (wheel->getValue() / 48.0 + 1.0e6, 1.0), look(), hw::Look::Knob::Wheel, {});
    }
    auto top = designBounds().removeFromTop (kTopBar);
    g.setColour (theme.panel);
    g.fillRect (top);
    g.setColour (theme.text);
    g.setFont (type::font (type::kTitle, true));
    g.drawText ("ProMix 01", top.reduced (sizes::kGapLarge, 0), juce::Justification::centredLeft);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption));
    g.drawText ("Not affiliated with or endorsed by Yamaha; ProMix is Yamaha's trademark, used only to say what is emulated.",
                top.reduced (sizes::kGapLarge, 0).withTrimmedLeft (150).withTrimmedRight (200), juce::Justification::centredLeft);

    // The dashed outlines: SCENE MEMORY's four keys, and the function keys
    // with them; the PARAMETER wheel's name.
    if (panelKeys.size() > 3)
    {
        const auto dashed = [&g] (juce::Rectangle<float> r)
        {
            juce::Path p;
            p.addRectangle (r);
            juce::Path d;
            const float dashes[] = { 6.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (d, p, dashes, 2);
            g.setColour (look().silkDim.withAlpha (0.8f));
            g.fillPath (d);
        };
        const auto scene = panelKeys[0]->getBounds().getUnion (panelKeys[3]->getBounds()).toFloat().expanded (4.0f, 4.0f).withTrimmedTop (-14.0f);
        dashed (scene);
        hw::silk (g, scene.withHeight (16.0f).withTrimmedTop (1.0f), "SCENE MEMORY", look(), 10.5f);
        auto all = scene;
        for (auto& [f, key] : functionKeys)
            all = all.getUnion (key->getBounds().toFloat().expanded (4.0f, 4.0f));
        juce::Path rest;
        const float x0 = all.getX(), x1 = all.getRight(), y0 = all.getY(), y1 = all.getBottom(), sx = scene.getRight() + 3.0f, sy = scene.getBottom() + 3.0f;
        rest.startNewSubPath (sx, y0);
        rest.lineTo (x1, y0);
        rest.lineTo (x1, y1);
        rest.lineTo (x0, y1);
        rest.lineTo (x0, sy);
        rest.lineTo (sx, sy);
        rest.closeSubPath();
        juce::Path d;
        const float dashes[] = { 6.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (d, rest, dashes, 2);
        g.setColour (look().silkDim.withAlpha (0.8f));
        g.fillPath (d);
        const auto w = wheel->getBounds().toFloat();
        hw::silk (g, w.withY (w.getY() - 30.0f).withHeight (14.0f), "PARAMETER", look(), 11.0f);
        const auto enter = panelKeys[8]->getBounds().toFloat();
        hw::silk (g, enter.withY (enter.getY() - 16.0f).withHeight (14.0f), "ENTER", look(), 11.0f);
    }

    // The legends beside the LCD: FUNCTION, MEMORY, SEL CH; RTN 1 ... SEND 4.
    {
        const auto lcd = display.getBounds().toFloat();
        const char* left[] = { "FUNCTION", "MEMORY", "SEL CH" };
        for (int k = 0; k < 3; ++k)
            hw::silk (g, juce::Rectangle<float> (lcd.getX() - 76.0f, lcd.getY() + lcd.getHeight() * promix::Display::kLeftRows[k] - 7.0f, 70.0f, 14.0f), left[k],
                      look(), 10.0f, juce::Justification::centredRight);
        const char* right[] = { "RTN   1", "RTN   2", "SEND 3", "SEND 4" };
        for (int k = 0; k < 4; ++k)
            hw::silk (g, juce::Rectangle<float> (lcd.getRight() + 6.0f, lcd.getY() + lcd.getHeight() * promix::Display::kRightRows[k] - 7.0f, 60.0f, 14.0f), right[k],
                      look(), 10.0f, juce::Justification::centredLeft);
    }

    // The analog row along the top, drawn as on the panel (they aren't Sidecar's
    // controls): per channel its 20dB PAD switch, its GAIN knob in its ring of
    // ticks from -16 to -60, the number; CUE/2TR IN, MONITOR OUT and PHONES.
    if (! analogArea.isEmpty())
    {
        const auto row = analogArea.toFloat();
        const auto ticked = [&g] (juce::Point<float> c, float radius, int ticks)
        {
            g.setColour (look().silkDim.withAlpha (0.8f));
            for (int k = 0; k < ticks; ++k)
            {
                const float a = juce::MathConstants<float>::pi * (1.25f - 1.5f * (float) k / (float) (ticks - 1));
                const juce::Point<float> dir (std::cos (a), -std::sin (a));
                g.drawLine (juce::Line<float> (c + dir * (radius + 2.0f), c + dir * (radius + 6.0f)), 1.0f);
            }
        };
        const auto pad = [&g] (juce::Rectangle<float> r)
        {
            g.setColour (juce::Colour (0xff2a2c2f));
            g.fillRect (r);
            g.setColour (juce::Colour (0xffb9bcbe));
            g.drawRect (r, 1.0f);
            g.setColour (juce::Colour (0xffd8dadc));
            g.fillRect (r.reduced (3.0f, 2.5f));
        };
        for (int i = 0; i < 16; ++i)
        {
            const float cx = (float) faders[(size_t) i]->getBounds().getCentreX() + 12.0f;
            const auto sw = juce::Rectangle<float> (24.0f, 13.0f).withCentre ({ cx, row.getY() + 12.0f });
            pad (sw);
            if (i == 0)
                hw::silk (g, sw.withX (sw.getX() - 34.0f).withWidth (30.0f), "PAD", look(), 10.0f, juce::Justification::centredRight);
            hw::silk (g, juce::Rectangle<float> (40.0f, 10.0f).withCentre ({ cx, row.getY() + 25.0f }), "20dB", look(), 8.0f);
            const juce::Point<float> c (cx, row.getY() + 45.0f);
            ticked (c, 10.0f, 11);
            hw::knob (g, juce::Rectangle<float> (20.0f, 20.0f).withCentre (c), 0.5f, look());
            hw::silk (g, juce::Rectangle<float> (20.0f, 9.0f).withCentre ({ cx - 9.0f, row.getY() + 63.0f }), "-16", look(), 7.5f, juce::Justification::centred, true);
            hw::silk (g, juce::Rectangle<float> (20.0f, 9.0f).withCentre ({ cx + 9.0f, row.getY() + 63.0f }), "-60", look(), 7.5f, juce::Justification::centred, true);
            hw::silk (g, juce::Rectangle<float> (40.0f, 11.0f).withCentre ({ cx, row.getY() + 74.0f }), "GAIN", look(), 9.0f);
            hw::silk (g, juce::Rectangle<float> (40.0f, 12.0f).withCentre ({ cx, row.getY() + 88.0f }), juce::String (i + 1), look(), 10.5f);
            g.setColour (look().silkDim.withAlpha (0.35f));
            g.drawVerticalLine ((int) (cx + (float) faders[0]->getWidth() * 0.5f), row.getY() + 2.0f, row.getBottom() - 4.0f);
        }
        // CUE/2TR IN over the two output knobs, at the right end of the PAD row.
        const float lx = (float) faders[17]->getBounds().getCentreX() + 12.0f, rx = (float) faders[18]->getBounds().getCentreX() + 12.0f;
        const auto cue = juce::Rectangle<float> (26.0f, 13.0f).withCentre ({ rx, row.getY() + 12.0f });
        pad (cue);
        hw::silk (g, juce::Rectangle<float> (90.0f, 10.0f).withCentre ({ rx, row.getY() + 25.0f }), "CUE/ 2TR IN", look(), 8.0f);
        const char* outs[] = { "MONITOR\nOUT", "PHONES" };
        for (int k = 0; k < 2; ++k)
        {
            const juce::Point<float> c (k == 0 ? lx : rx, row.getY() + 47.0f);
            ticked (c, 13.0f, 11);
            hw::knob (g, juce::Rectangle<float> (26.0f, 26.0f).withCentre (c), 0.6f, look());
            hw::silk (g, juce::Rectangle<float> (12.0f, 9.0f).withCentre (c + juce::Point<float> (-12.0f, 18.0f)), "0", look(), 7.5f, juce::Justification::centred, true);
            hw::silk (g, juce::Rectangle<float> (14.0f, 9.0f).withCentre (c + juce::Point<float> (13.0f, 18.0f)), "10", look(), 7.5f, juce::Justification::centred, true);
            hw::silk (g, juce::Rectangle<float> (40.0f, 9.0f).withCentre (c + juce::Point<float> (0.0f, 27.0f)), "LEVEL", look(), 7.5f);
            g.setColour (look().silk);
            g.setFont (type::font (9.5f, true));
            g.drawFittedText (outs[k], juce::Rectangle<float> (80.0f, 22.0f).withCentre (c + juce::Point<float> (0.0f, 43.0f)).toNearestInt(),
                              juce::Justification::centredTop, 2);
        }
    }

    // The LCD's contrast knob, its half-moon mark beside it.
    if (! contrastKnob.isEmpty())
    {
        const auto k = contrastKnob.toFloat();
        g.setColour (juce::Colour (0xff1b1c1e));
        g.fillEllipse (k);
        g.setColour (look().silkDim.withAlpha (0.6f));
        g.drawEllipse (k.reduced (0.5f), 1.0f);
        g.drawEllipse (k.reduced (k.getWidth() * 0.32f), 1.0f);
        auto mark = juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ k.getRight() + 10.0f, k.getY() + 8.0f });
        g.drawEllipse (mark, 1.0f);
        juce::Path half;
        half.addPieSegment (mark, 0.0f, juce::MathConstants<float>::pi, 0.0f);
        g.fillPath (half);
    }

    // The stereo output's LED meter (pp.2, 4): two columns of 12 round LEDs,
    // CLIP, 15 ... 0 ... -40 printed between them, L and R under them.
    auto m = meterArea().toFloat();
    {
        auto lr = m.removeFromBottom (18.0f);
        const float lx = m.getX() + 7.0f, rx = m.getRight() - 7.0f;
        g.setColour (look().silkDim);
        g.drawLine (lx - 4.0f, lr.getY() + 3.0f, rx + 4.0f, lr.getY() + 3.0f, 1.0f);
        g.drawLine (lx - 4.0f, lr.getY(), lx - 4.0f, lr.getY() + 3.0f, 1.0f);
        g.drawLine (rx + 4.0f, lr.getY(), rx + 4.0f, lr.getY() + 3.0f, 1.0f);
        hw::silk (g, juce::Rectangle<float> (16.0f, 12.0f).withCentre ({ lx, lr.getY() + 11.0f }), "L", look(), 10.0f);
        hw::silk (g, juce::Rectangle<float> (16.0f, 12.0f).withCentre ({ rx, lr.getY() + 11.0f }), "R", look(), 10.0f);
    }
    const char* labels[] = { "CLIP", "15", "12", "9", "6", "3", "0", "-6", "-12", "-18", "-24", "-40" };
    const float dbfs[] = { -0.1f, -5.0f, -8.0f, -11.0f, -14.0f, -17.0f, -20.0f, -26.0f, -32.0f, -38.0f, -44.0f, -60.0f };
    auto* st = ctx.engine.getMains().empty() ? nullptr : ctx.engine.getMains()[0].get();
    const float segH = m.getHeight() / 12.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        // Each column in its rounded window.
        const float x = ch == 0 ? m.getX() + 7.0f : m.getRight() - 7.0f;
        g.setColour (juce::Colour (0xff1a1b1d));
        g.fillRoundedRectangle (juce::Rectangle<float> (13.0f, m.getHeight() + 4.0f).withCentre ({ x, m.getCentreY() }), 6.5f);
    }
    for (int s = 0; s < 12; ++s)
    {
        auto row = juce::Rectangle<float> (m.getX(), m.getY() + segH * (float) s, m.getWidth(), segH);
        hw::silk (g, row.reduced (14.0f, 0.0f), labels[s], look(), 8.5f, juce::Justification::centred, true);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float level = st != nullptr ? juce::Decibels::gainToDecibels (ctx.meters.get (st, MeterPoint::Output, ch), -100.0f) : -100.0f;
            const float x = ch == 0 ? m.getX() + 7.0f : m.getRight() - 7.0f;
            const auto led = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x, row.getCentreY() });
            const auto colour = s == 0 ? juce::Colour (0xffff4040) : s < 6 ? juce::Colour (0xfff0c030) : juce::Colour (0xff40d070);
            g.setColour (level >= dbfs[s] ? colour : colour.withAlpha (0.15f));
            g.fillEllipse (led);
        }
    }

    // The strips' names over the SEL keys, as the panel prints them.
    for (int i = 0; i < kNumFaders; ++i)
    {
        const auto s = selKeys[(size_t) i]->getBounds().toFloat();
        const juce::String name = i < 16 ? juce::String (i + 1) : i == 16 ? juce::String ("ST IN") : i == 17 ? juce::String ("RTN/\nSEND") : juce::String ("ST OUT");
        g.setColour (look().silk);
        g.setFont (type::font (i < 16 ? 13.0f : 10.5f, true));
        g.drawFittedText (name, s.withY (s.getY() - 26.0f).withHeight (24.0f).toNearestInt(), juce::Justification::centredBottom, 2);
    }

    // The faders' names under them; dividers between the strips.
    for (int i = 0; i < kNumFaders; ++i)
    {
        const auto f = faders[(size_t) i]->getBounds();
        const juce::String name = i < 16 ? juce::String (i + 1) : i == 16 ? juce::String ("ST IN") : i == 17 ? juce::String ("RTN/\nSEND") : juce::String ("ST OUT");
        g.setColour (look().silk);
        g.setFont (type::font (i < 16 ? 12.0f : 9.5f, true));
        g.drawFittedText (name, f.withY (f.getBottom()).withHeight (24), juce::Justification::centredTop, 2);
        if (i > 0)
        {
            g.setColour (look().silkDim.withAlpha (0.35f));
            const int x = f.getX() - (i == 16 || i == 17 ? 4 : 0);
            g.drawVerticalLine (x, (float) selKeys[(size_t) i]->getY() - 26.0f, (float) f.getBottom() + 20.0f);
        }
    }
}

void ProMixSurface::resized()
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
    auto top = area.removeFromTop (kTopBar);
    universalKey.setBounds (top.removeFromRight (190).reduced (sizes::kGap, 7));

    // The panel as the console has it (User's Guide pp.2-4): the analog row
    // along the top; then the function keys in their block left of the LCD
    // (SCENE MEMORY's STORE, INC, RECALL, DEC with UTILITY, MIDI, GROUP, PAIR;
    // the orange, blue and green rows), the LCD with its legends, its contrast
    // knob over the stereo meter, ENTER, the cursor keys and the PARAMETER wheel
    // at the right; the strips below, the vent over their faders.
    area.removeFromLeft (14);
    area.removeFromRight (14); // the rack ears
    analogArea = area.removeFromTop (100);
    auto upper = area.removeFromTop (292).reduced (28, 8);
    auto block = upper.removeFromLeft (420);
    block.removeFromTop (12);
    const int kw = block.getWidth() / 4, kh = juce::jmin (kKey + 18, block.getHeight() / 5);
    auto cell = [&] (int row, int col) { return juce::Rectangle<int> (block.getX() + col * kw, block.getY() + row * kh, kw, kh).reduced (6, 3); };
    // Rows 1-2: STORE, INC, UTILITY, MIDI / RECALL, DEC, GROUP, PAIR.
    panelKeys[0]->setBounds (cell (0, 0));
    panelKeys[2]->setBounds (cell (0, 1));
    panelKeys[1]->setBounds (cell (1, 0));
    panelKeys[3]->setBounds (cell (1, 1));
    functionKeys[14].second->setBounds (cell (0, 2)); // UTILITY
    functionKeys[15].second->setBounds (cell (0, 3)); // MIDI
    functionKeys[12].second->setBounds (cell (1, 2)); // GROUP
    functionKeys[13].second->setBounds (cell (1, 3)); // PAIR
    for (int k = 0; k < 12; ++k) // METER ... CUE; SEND 1-4; EQ LOW ... LIBRARY
        functionKeys[(size_t) k].second->setBounds (cell (2 + k / 4, k % 4));
    upper.removeFromLeft (96); // FUNCTION, MEMORY, SEL CH
    auto right = upper.removeFromRight (300);
    auto meterColumn = upper.removeFromRight (64);
    contrastKnob = meterColumn.removeFromTop (54).withSizeKeepingCentre (38, 38).translated (-6, 0);
    meterRect = meterColumn.reduced (8, 4);
    upper.removeFromRight (74); // RTN 1 ... SEND 4
    display.setBounds (upper.withTrimmedTop (14).withTrimmedBottom (14));
    // ENTER, an oval, beside the PARAMETER wheel; the cursor keys in a diamond under it.
    wheel->setBounds (right.removeFromRight (170).withSizeKeepingCentre (156, 156).withY (right.getY() + 36));
    right.removeFromRight (8);
    panelKeys[8]->setBounds (right.getX() + 20, right.getY() + 34, 92, 30); // ENTER
    const int cx = right.getX() + 8, cy = right.getY() + 104;
    panelKeys[5]->setBounds (cx + 42, cy, 44, 38);                // up
    panelKeys[4]->setBounds (cx, cy + 40, 40, 44);                // left
    panelKeys[7]->setBounds (cx + 88, cy + 40, 40, 44);           // right
    panelKeys[6]->setBounds (cx + 42, cy + 86, 44, 38);           // down

    // The faders: 19 strips, the name and SEL and ON above each, the vent, the
    // fader, its name below.
    auto lower = area.reduced (sizes::kGapLarge, sizes::kGap);
    const int w = juce::jmin (kStripWidth, lower.getWidth() / kNumFaders);
    int x = lower.getX() + (lower.getWidth() - w * kNumFaders - 16) / 2;
    grilleArea = {};
    for (int i = 0; i < kNumFaders; ++i)
    {
        if (i == 16 || i == 17)
            x += 8;
        auto col = juce::Rectangle<int> (x, lower.getY() + 26, w, lower.getHeight() - 50); // the name above
        selKeys[(size_t) i]->setBounds (col.removeFromTop (kKey).reduced (6, 0));
        col.removeFromTop (6);
        onKeys[(size_t) i]->setBounds (col.removeFromTop (kKey).reduced (6, 0));
        col.removeFromTop (8);
        grilleArea = grilleArea.isEmpty() ? col.removeFromTop (46).reduced (2, 0) : grilleArea.getUnion (col.removeFromTop (46).reduced (2, 0));
        col.removeFromTop (4);
        faders[(size_t) i]->setBounds (col);
        x += w;
    }
}

} // namespace livedaw::ui
