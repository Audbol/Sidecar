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
    // Its LEDs: SEL amber, ON green; its function keys' colours.
    const juce::Colour kSelLed (0xffffb030), kOnLed (0xff4cd964), kOrangeKey (0xffd9762c), kBlueKey (0xff7f96bf),
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
        const int now = param >= 0 ? juce::jmax (0, ctx.engine.getProMix().get (param)) : 0;
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
        // ST IN's cap black, RTN / SEND's blue-grey, ST OUT's red (the console's).
        const auto capColour = param == 16 ? juce::Colour (0xff222325)
                             : param >= 17 && param <= 20 ? juce::Colour (0xff8ea0b6)
                             : param == 21 ? juce::Colour (0xffc8302a) : juce::Colour();
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
            case Function::Utility: return 4; // SEND3,4 / OUTPUT COMP, OSCILLATOR, MEMORY PROTECT, BATTERY CHECK
            case Function::Midi:    return 2; // MIDI SETUP, BULK
            case Function::Meter:
            case Function::Send1:
            case Function::Send2:
            case Function::Comp:    return 2;
            default:                return 1;
        }
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
        const int e = eqStrip;
        switch (function)
        {
            case Function::Meter:
                break;
            case Function::Pan:
                if (page == 0)
                {
                    for (int r = 0; r < 2; ++r)
                    {
                        const float y = 0.08f + 0.42f * (float) r;
                        for (int i = 0; i < 8; ++i)
                            fields.push_back (paramField (kChPan + 8 * r + i, { 0.125f * (float) i, y, 0.125f, 0.34f }));
                    }
                    fields.push_back (paramField (kPanMode, { 0.70f, 0.86f, 0.30f, 0.14f }));
                }
                else
                {
                    row ({ 60, 61, 62, 63, 64, 65 }, 0.10f, 0.34f);
                    row ({ 67, 66 }, 0.55f, 0.34f, 0.0f, 0.34f);
                }
                break;
            case Function::Phase:
                for (int i = 0; i < 17; ++i)
                    fields.push_back (paramField (kPhase + i, { (float) (i % 9) / 9.0f, i < 9 ? 0.15f : 0.55f, 1.0f / 9.0f, 0.3f }));
                break;
            case Function::EqLow:
            case Function::EqMid:
            case Function::EqHigh:
            {
                const int focusBand = function == Function::EqLow ? 0 : function == Function::EqMid ? 1 : 2;
                for (int band = 0; band < 3; ++band)
                    for (int what = 0; what < 3; ++what)
                        fields.push_back (paramField (kEq + 20 * (3 * band + what) + e,
                                                      { 0.6f + 0.13f * (float) band, 0.06f + 0.24f * (float) what, 0.13f, 0.22f }));
                fields.push_back (paramField (kEqOn + e, { 0.6f, 0.80f, 0.39f, 0.17f }));
                focus = juce::jlimit (0, (int) fields.size() - 1, focus == 0 ? 3 * focusBand : focus);
                break;
            }
            case Function::Library:
            {
                Field f;
                f.label = "EQ LIBRARY";
                auto& p = pm;
                f.text = [] { return juce::String(); };
                f.get = [] { return -1; };
                f.steps = [] { return 50; };
                f.set = [&p, this] (int n) { p.recallEq (eqStrip, n); };
                f.onEnter = true;
                f.item = [&p] (int i) { return juce::String (i + 1).paddedLeft ('0', 2) + " " + p.programName (ProMix::Eq, i); };
                f.box = { 0.0f, 0.0f, 0.62f, 1.0f };
                f.pending = 0;
                fields.push_back (std::move (f));
                fields.push_back (storeField (ProMix::Eq, 30, 20, eqStrip, { 0.0f, 0.0f, 0.0f, 0.0f }));
                fields.back().box = { 0.64f, 0.0f, 0.36f, 0.2f };
                break;
            }
            case Function::Send1:
            case Function::Send2:
            {
                const int fx = function == Function::Send1 ? 0 : 1;
                if (page == 0)
                {
                    fields.push_back (listField ("EFFECT " + juce::String (fx + 1), ProMix::Effects, 40, kFxType + fx, { 0.0f, 0.0f, 0.62f, 1.0f }));
                    fields.push_back (paramField (kSendPre + fx, { 0.66f, 0.80f, 0.34f, 0.18f }));
                    fields.push_back (storeField (ProMix::Effects, 30, 10, fx, { 0.66f, 0.56f, 0.34f, 0.18f }));
                }
                else
                {
                    for (int k = 0; k < 10; ++k)
                    {
                        const int n = kFxParam + 20 * fx + 2 * k;
                        if (pm.label (n).isEmpty())
                            break;
                        fields.push_back (paramField (n, { 0.2f * (float) (k % 5), k < 5 ? 0.12f : 0.56f, 0.2f, 0.34f }));
                    }
                }
                break;
            }
            case Function::Send3:
            case Function::Send4:
            {
                const int send = function == Function::Send3 ? 2 : 3;
                fields.push_back (paramField (kSendPre + send, { 0.0f, 0.02f, 0.25f, 0.18f }));
                if (pm.get (kSend34Flag) == 1)
                {
                    for (int i = 0; i < 17; ++i)
                        fields.push_back (paramField (kSend34Pan + i, { (float) (i % 9) / 9.0f, i < 9 ? 0.26f : 0.6f, 1.0f / 9.0f, 0.3f }));
                    fields.push_back (paramField (66, { 0.8f, 0.02f, 0.2f, 0.18f }));
                }
                break;
            }
            case Function::Comp:
            {
                const int k = processor;
                if (page == 0)
                {
                    fields.push_back (paramField (kCompOn + k, { 0.0f, 0.0f, 0.18f, 0.2f }));
                    fields.push_back (paramField (kCompPatch + k, { 0.18f, 0.0f, 0.18f, 0.2f }, true));
                    fields.push_back (paramField (kCompKey + k, { 0.36f, 0.0f, 0.18f, 0.2f }, true));
                    fields.push_back (paramField (kCompIn + k, { 0.54f, 0.0f, 0.14f, 0.2f }));
                    fields.push_back (listField ("COMP " + juce::String (k + 1), ProMix::Dynamics, 20, kCompType + k, { 0.0f, 0.24f, 0.68f, 0.76f }));
                    fields.push_back (storeField (ProMix::Dynamics, 10, 10, k, { 0.70f, 0.24f, 0.30f, 0.2f }));
                }
                else
                {
                    for (int i = 0; i < 5; ++i)
                        fields.push_back (paramField (kCompParam + 5 * k + i, { 0.2f * (float) i, 0.2f, 0.2f, 0.4f }));
                    fields.push_back (paramField (kCompOn + k, { 0.0f, 0.75f, 0.2f, 0.22f }));
                }
                break;
            }
            case Function::Cue:
                fields.push_back (paramField (kCueMode, { 0.0f, 0.0f, 0.3f, 0.24f }, true));
                if (e < 16)
                {
                    // The selected channel at a glance (p.52): ON, group, phase,
                    // pan, EQ, its fader and its sends.
                    row ({ kChOn + e, kGroup + e, kPhase + e, kChPan + e, kEqOn + e }, 0.3f, 0.3f, 0.0f, 1.0f);
                    row ({ e, kSendLevel + e, kSendLevel + 17 + e, kSendLevel + 34 + e, kSendLevel + 51 + e }, 0.64f, 0.3f, 0.0f, 1.0f);
                    const char* names[] = { "ON", "GROUP", "PHASE", "PAN", "EQ", "FADER", "SEND1", "SEND2", "SEND3", "SEND4" };
                    for (size_t k = 0; k < 10 && k + 1 < fields.size(); ++k)
                        fields[k + 1].label = names[k];
                }
                break;
            case Function::Group:
                fields.push_back (paramField (kGroupSelect, { 0.0f, 0.0f, 0.2f, 0.18f }));
                for (int m = 0; m < 17; ++m)
                    fields.push_back (paramField (kGroup + m, { 0.06f + 0.94f * (float) m / 17.0f, 0.82f, 0.94f / 17.0f, 0.16f }));
                break;
            case Function::Pair:
                for (int p = 0; p < 8; ++p)
                    fields.push_back (paramField (kPairExecute + p, { 0.125f * (float) p, 0.45f, 0.125f, 0.3f }, true));
                break;
            case Function::Utility:
            {
                if (page == 0)
                {
                    // SEND3,4 CONFIGURATION: the flag shown, the Execute set by ENTER
                    // (p.46); OUTPUT COMP PATCH POINT (p.75); PAN's GANG.
                    auto mode = paramField (kSend34Exec, { 0.0f, 0.1f, 0.3f, 0.3f }, true);
                    auto& p = pm;
                    mode.text = [&p] { return p.text (kSend34Flag); };
                    mode.get = [&p] { return p.get (kSend34Flag); };
                    fields.push_back (std::move (mode));
                    row ({ 512, 513, kPatchPointSt }, 0.1f, 0.3f, 0.34f, 1.0f);
                    fields.push_back (paramField (kPanMode, { 0.0f, 0.55f, 0.3f, 0.3f }));
                }
                else if (page == 1)
                {
                    // OSCILLATOR (p.90): ON, LEVEL, FREQ, and where it goes.
                    row ({ kOsc, kOsc + 1, kOsc + 2 }, 0.1f, 0.34f, 0.0f, 0.75f);
                    row ({ 480, 481, 482 }, 0.55f, 0.34f, 0.0f, 0.75f);
                }
                else if (page == 2)
                {
                    // MEMORY PROTECT (p.65): STORE refuses a protected memory.
                    for (int half = 0; half < 2; ++half)
                    {
                        Field f;
                        f.label = half == 0 ? "MEMORY 01-25" : "MEMORY 26-50";
                        f.get = [this, half] { return isProtected && isProtected (half) ? 1 : 0; };
                        f.set = [this, half] (int v) { if (setProtected) setProtected (half, v != 0); };
                        f.steps = [] { return 2; };
                        f.text = [this, half] { return juce::String (isProtected && isProtected (half) ? "PROTECT ON" : "PROTECT OFF"); };
                        f.box = { 0.1f + 0.42f * (float) half, 0.3f, 0.38f, 0.34f };
                        fields.push_back (std::move (f));
                    }
                }
                else
                {
                    // BATTERY CHECK (p.91): the memory's backup battery.
                    Field f;
                    f.label = "BATTERY CHECK";
                    f.get = [] { return 0; };
                    f.set = [] (int) {};
                    f.steps = [] { return 1; };
                    f.text = [] { return juce::String ("Battery is OK!!"); };
                    f.box = { 0.25f, 0.3f, 0.5f, 0.34f };
                    fields.push_back (std::move (f));
                }
                break;
            }
            case Function::Midi:
            {
                // MIDI SETUP (p.81): the protocol's own values, kept in the app settings.
                auto* midi = dynamic_cast<ProMixMidi*> (ctx.consoleMidi);
                if (midi == nullptr)
                    break;
                auto value = [midi] (int i) { juce::StringArray v; v.addTokens (midi->getSetupText(), " ", ""); return v[i].getIntValue(); };
                auto setValue = [this, midi] (int i, int x)
                {
                    juce::StringArray v;
                    v.addTokens (midi->getSetupText(), " ", "");
                    v.set (i, juce::String (x));
                    midi->setSetupText (v.joinIntoString (" "));
                    ctx.settings.setValue (ProMixMidi::setupKey, midi->getSetupText());
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
                if (page == 1)
                {
                    // BULK (p.85): the interval between two dumps' messages.
                    field ("INTERVAL", 15, 11, 0, { 0.3f, 0.3f, 0.4f, 0.3f }, [] (int x) { return juce::String (x * 100) + " ms"; });
                    break;
                }
                field ("Tx CH", 0, 16, 1, { 0.0f, 0.02f, 0.2f, 0.2f }, number);
                field ("Rx CH", 1, 16, 1, { 0.2f, 0.02f, 0.2f, 0.2f }, number);
                field ("MODE", 14, 2, 0, { 0.45f, 0.02f, 0.3f, 0.2f }, [] (int x) { return juce::String (x != 0 ? "REGISTER" : "CHANNEL"); });
                const char* kinds[] = { "PROGRAM", "CONTROL", "BULK" };
                const char* switches[] = { "Tx", "Rx", "OMNI", "ECHO" };
                for (int k = 0; k < 3; ++k)
                    for (int s = 0; s < 4; ++s)
                        field (juce::String (kinds[k]) + " " + switches[s], 2 + 4 * k + s, 2, 0,
                               { 0.25f * (float) s, 0.3f + 0.23f * (float) k, 0.25f, 0.21f }, onOff);
                break;
            }
            case Function::MemoryStore:
            case Function::MemoryRecall:
            {
                // The memories 00-50, the current one "=", the chosen one in the
                // middle (INC, DEC, the wheel); ENTER (or the key again) stores
                // or recalls it.
                const bool store = function == Function::MemoryStore;
                Field f;
                f.label = store ? "MEMORY STORE" : "MEMORY RECALL";
                f.get = [this] { return ctx.show.getCurrentSceneIndex() + 1; };
                f.steps = [] { return 51; };
                f.text = [] { return juce::String(); };
                f.item = [this] (int m)
                {
                    const auto t = memoryTitle ? memoryTitle (m) : juce::String();
                    return juce::String (m).paddedLeft ('0', 2) + " " + (t.isEmpty() ? juce::String ("--------") : t)
                         + (m > 0 && isProtected && isProtected (m <= 25 ? 0 : 1) ? "  PROTECT" : "");
                };
                f.set = [this, store] (int m)
                {
                    if (chooseMemory)
                        chooseMemory (m);
                    if (store ? (bool) storeChosen : (bool) recallChosen)
                        (store ? storeChosen : recallChosen)();
                };
                f.onEnter = true;
                f.pending = chosenMemory ? chosenMemory() : 1;
                f.box = { 0.0f, 0.0f, 0.62f, 1.0f };
                fields.push_back (std::move (f));
                break;
            }
            case Function::TitleEdit:
            {
                // A memory's eight characters, or a program's fifteen: the cursor keys
                // choose one, the wheel changes it, ENTER keeps the title.
                const int n = nameLibrary < 0 ? 8 : 15;
                const float w = nameLibrary < 0 ? 0.1f : 0.06f;
                for (int k = 0; k < n; ++k)
                {
                    Field f;
                    f.label = k == 0 ? (nameLibrary < 0 ? "TITLE " + juce::String (titleMemory).paddedLeft ('0', 2) : juce::String ("NAME ") + juce::String (nameProgram + 1))
                                     : juce::String();
                    f.get = [this, k] { return titleChars[(size_t) k]; };
                    f.set = [this, k] (int v) { titleChars[(size_t) k] = v; };
                    f.steps = [] { return kTitleChars.length(); };
                    f.text = [this, k] { return juce::String::charToString (kTitleChars[titleChars[(size_t) k]]); };
                    f.box = { 0.05f + w * (float) k, 0.3f, w, 0.34f };
                    fields.push_back (std::move (f));
                }
                break;
            }
        }
        focus = juce::jlimit (0, juce::jmax (0, (int) fields.size() - 1), focus);
        repaint();
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
        auto& f = fields[(size_t) focus];
        if (f.onEnter)
        {
            if (f.pending >= 0)
                f.set (f.pending);
            if (! f.item)
                f.pending = -1;
        }
        else if (f.steps() == 2)
        {
            f.set (f.get() == 0 ? 1 : 0);
        }
        build(); // a type or a mode changes the page's fields
    }

    void Display::refresh()
    {
        juce::String now = title() + "|" + (memoryText ? memoryText() : juce::String());
        for (auto& f : fields)
            now << "|" << f.text() << f.pending;
        if (function == Function::Meter || function == Function::Send1 || function == Function::Send2 || function == Function::Comp)
            now << juce::String (juce::Time::getMillisecondCounter() / 100); // meters move
        if (now != shown)
        {
            shown = now;
            repaint();
        }
    }

    juce::String Display::title() const
    {
        const char* names[] = { "METER", "PAN", "PHASE", "COMP", "CUE", "SEND1", "SEND2", "SEND3", "SEND4", "EQ LOW", "EQ MID", "EQ HIGH",
                                "EQ LIBRARY", "GROUP", "PAIR", "UTILITY", "MIDI SETUP", "MEMORY STORE", "MEMORY RECALL", "TITLE EDIT" };
        juce::String t = names[(int) function];
        if (function == Function::TitleEdit && nameLibrary >= 0)
            t = juce::String (nameLibrary == ProMix::Eq ? "EQ" : nameLibrary == ProMix::Dynamics ? "COMP" : "EFFECT") + " PROGRAM NAME";
        if (function == Function::Utility)
        {
            const char* pages[] = { "  SEND3,4 / OUTPUT COMP", "  OSCILLATOR", "  MEMORY PROTECT", "  BATTERY CHECK" };
            t << pages[juce::jlimit (0, 3, page)];
        }
        if (function == Function::Midi && page == 1)
            t = "BULK DUMP";
        if (function == Function::Comp)
            t << " " << (processor + 1) << (page == 1 ? "  EDIT" : "");
        if ((function == Function::Send1 || function == Function::Send2) && page == 1)
            t << "  EFFECT EDIT";
        if (function == Function::Pan || function == Function::Meter)
            t << (page == 0 ? "  CH1-16" : "  ST IN / RTN / ST OUT");
        return t;
    }

    juce::Rectangle<float> Display::pageArea() const
    {
        return getLocalBounds().toFloat().reduced (14.0f).withTrimmedTop (40.0f);
    }

    juce::Rectangle<float> Display::boxOf (const Field& f) const
    {
        const auto a = pageArea();
        return { a.getX() + f.box.getX() * a.getWidth(), a.getY() + f.box.getY() * a.getHeight(), f.box.getWidth() * a.getWidth(),
                 f.box.getHeight() * a.getHeight() };
    }

    void Display::resized() {}

    void Display::paintField (juce::Graphics& g, const Field& f, bool focused) const
    {
        auto box = boxOf (f).reduced (3.0f);
        if (f.item)
        {
            // A program list: the chosen one in the middle, the current one marked "=" (p.34).
            const int count = f.steps(), current = f.get(), chosen = f.pending >= 0 ? f.pending : juce::jmax (0, current);
            g.setColour (kPixelDim);
            g.setFont (lcdFont (type::kCaption, true));
            g.drawText (f.label, box.removeFromTop (18.0f), juce::Justification::centredLeft);
            const int rows = juce::jmax (1, (int) (box.getHeight() / 22.0f));
            const int first = juce::jlimit (0, juce::jmax (0, count - rows), chosen - rows / 2);
            for (int i = first; i < juce::jmin (count, first + rows); ++i)
            {
                auto r = box.removeFromTop (22.0f);
                const bool sel = i == chosen;
                if (sel && focused)
                {
                    g.setColour (kPixel);
                    g.fillRect (r);
                }
                g.setColour (sel && focused ? kLcd : i == current ? kPixel : kPixelDim);
                g.setFont (lcdFont (type::kLabel, i == current));
                auto text = f.item (i);
                if (i == current)
                    text = text.replaceFirstOccurrenceOf (" ", "=");
                g.drawText (text, r.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
            }
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
            g.setColour (kPixelDim);
            g.setFont (lcdFont (type::kCaption, true));
            g.drawText (bars[i].first, col.removeFromBottom (16.0f), juce::Justification::centred);
            const float db = juce::Decibels::gainToDecibels (bars[i].second, -100.0f);
            const float segH = col.getHeight() / 12.0f;
            for (int s = 0; s < 12; ++s)
            {
                const bool lit = db >= marks[11 - s];
                auto seg = juce::Rectangle<float> (col.getX(), col.getBottom() - segH * (float) (s + 1), col.getWidth(), segH).reduced (0.0f, 1.5f);
                g.setColour (lit ? (s == 11 ? juce::Colour (0xffff6060) : kPixel) : kLcdFrame);
                g.fillRect (seg);
            }
        }
    }

    void Display::paintEqCurve (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        g.setColour (kLcdFrame);
        g.drawRect (area);
        for (float db : { -15.0f, 0.0f, 15.0f })
            g.drawHorizontalLine ((int) area.getCentreY() - juce::roundToInt (db / 18.0f * area.getHeight() / 2.0f), area.getX(), area.getRight());
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
    }

    void Display::paintGroups (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        // The FADER GROUP display (p.56): groups A-D down the side, the channels
        // across, a dot where a channel is in the group.
        const int shownGroup = juce::jmax (0, pm.get (kGroupSelect));
        const float rowH = area.getHeight() / 4.0f, colW = (area.getWidth() - area.getWidth() * 0.06f) / 17.0f;
        for (int grp = 0; grp < 4; ++grp)
        {
            auto r = area.withY (area.getY() + rowH * (float) grp).withHeight (rowH);
            g.setColour (grp == shownGroup ? kPixel : kPixelDim);
            g.setFont (lcdFont (type::kLabel, grp == shownGroup));
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + grp)), r.removeFromLeft (area.getWidth() * 0.06f), juce::Justification::centred);
            for (int m = 0; m < 17; ++m)
            {
                const auto cell = juce::Rectangle<float> (r.getX() + colW * (float) m, r.getY(), colW, rowH).reduced (colW * 0.3f, rowH * 0.3f);
                if (pm.get (kGroup + m) == grp + 1)
                    g.fillEllipse (cell);
                else
                {
                    g.setColour (kLcdFrame);
                    g.drawEllipse (cell, 1.0f);
                    g.setColour (grp == shownGroup ? kPixel : kPixelDim);
                }
            }
        }
    }

    void Display::paint (juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        hw::lcd (g, b, look());
        // The FUNCTION and MEMORY areas along the top.
        auto top = b.reduced (16.0f, 12.0f).removeFromTop (26.0f);
        g.setColour (kPixel);
        g.setFont (lcdFont (type::kValue, true));
        g.drawText (title(), top, juce::Justification::centredLeft);
        const char* stripNames[] = { "ST IN", "RTN1", "RTN2", "ST OUT" };
        const juce::String sel = eqStrip < 16 ? "CH" + juce::String (eqStrip + 1) : juce::String (stripNames[eqStrip - 16]);
        g.setColour (kPixelDim);
        g.drawText ("SEL " + sel, top, juce::Justification::centred);
        g.drawText (memoryText ? memoryText() : juce::String(), top, juce::Justification::centredRight);

        const auto area = pageArea();
        switch (function)
        {
            case Function::Meter:
                paintMeters (g, area);
                break;
            case Function::EqLow:
            case Function::EqMid:
            case Function::EqHigh:
            case Function::Library:
                paintEqCurve (g, area.withWidth (area.getWidth() * (function == Function::Library ? 0.34f : 0.56f))
                                     .withX (function == Function::Library ? area.getRight() - area.getWidth() * 0.34f : area.getX()));
                break;
            case Function::Group:
                paintGroups (g, area.withTrimmedTop (area.getHeight() * 0.2f).withTrimmedBottom (area.getHeight() * 0.2f));
                break;
            case Function::Pair:
                for (int p = 0; p < 8; ++p)
                {
                    auto box = juce::Rectangle<float> (area.getX() + area.getWidth() * 0.125f * (float) p, area.getY() + area.getHeight() * 0.1f,
                                                       area.getWidth() * 0.125f, area.getHeight() * 0.3f).reduced (6.0f);
                    g.setColour (kLcdFrame);
                    g.drawRoundedRectangle (box, 4.0f, 1.5f);
                    g.setColour (pm.get (kPairFlag + p) == 1 ? kPixel : kPixelDim);
                    g.setFont (lcdFont (type::kValue, true));
                    g.drawText (pm.get (kPairFlag + p) == 1 ? "STEREO" : "MONO x2", box, juce::Justification::centred);
                }
                break;
            case Function::Send1:
            case Function::Send2:
            case Function::Comp:
                if (page == 0)
                {
                    // The selected item's details beside the list.
                    auto info = area.withTrimmedLeft (area.getWidth() * 0.72f).withTrimmedTop (area.getHeight() * 0.3f).withTrimmedBottom (area.getHeight() * 0.25f);
                    g.setColour (kPixelDim);
                    g.setFont (lcdFont (type::kLabel));
                    const juce::String what = function == Function::Comp ? "A program, PATCH and KEY IN: choose, then ENTER. EDIT: the cursor past the last value"
                                                                         : "Recall: choose, then ENTER. EDIT: the key again";
                    g.drawFittedText (what, info.reduced (6.0f).toNearestInt(), juce::Justification::topLeft, 4);
                }
                break;
            default:
                break;
        }
        for (size_t i = 0; i < fields.size(); ++i)
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
        if (function == Function::Meter)
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
        addAndMakeVisible (*faders[(size_t) i]);
        auto& sel = selKeys[(size_t) i];
        sel = std::make_unique<ToggleKey> ("SEL", kSelLed, theme);
        sel->onClick = [this, i] { pressSel (i); };
        sel->isOn = [this, i]
        {
            if (display.getFunction() == Function::Group && i < 17)
                return pm.get (kGroup + i) == pm.get (kGroupSelect) + 1;
            if (display.getFunction() == Function::Cue)
                return pm.get (cueParam (i)) == 1;
            if (i < 16)
                return display.getEqStrip() == i;
            return i == 16 ? display.getEqStrip() == 16 : i == 18 ? display.getEqStrip() == 19 : rtnSend < 2 && display.getEqStrip() == 17 + rtnSend;
        };
        addAndMakeVisible (*sel);
        auto& on = onKeys[(size_t) i];
        on = std::make_unique<ToggleKey> ("ON", juce::Colour (0xff40c060), theme);
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
        repaint (wheel->getBounds());
    };
    wheel->setTitle ("PARAMETER");
    addAndMakeVisible (*wheel);

    display.memoryText = [this]
    {
        // The MEMORY area: the number STORE and RECALL act on, the scene last recalled.
        const int current = ctx.show.getCurrentSceneIndex();
        auto scene = current >= 0 ? ctx.show.getScene (current) : std::nullopt;
        const auto now = current >= 0 && scene.has_value() ? juce::String (current + 1).paddedLeft ('0', 2) + " " + scene->name : juce::String ("00 Initial Data");
        return "MEMORY " + juce::String (memory).paddedLeft ('0', 2) + (memory > 0 && isProtected (memory <= 25 ? 0 : 1) ? " PROTECT" : "") + "   (" + now + ")";
    };
    // STORE's and RECALL's lists, TITLE EDIT, MEMORY PROTECT.
    display.memoryTitle = [this] (int m) -> juce::String
    {
        if (m == 0)
            return "Initial Data";
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
    display.refresh();
}

void ProMixSurface::recall()
{
    if (memory == 0)
        pm.recallInitial();
    else if (memory - 1 < ctx.show.getNumScenes())
        ctx.show.recallScene (memory - 1);
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
        default: break;
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
    repaint (meterArea());
    repaint (memoryArea());
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
    {
        auto body = getLocalBounds().withTrimmedTop (kTopBar).toFloat();
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
    auto top = getLocalBounds().removeFromTop (kTopBar);
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
    auto area = getLocalBounds();
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
