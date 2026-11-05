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
    const juce::Colour kSelLed (0xffffa030), kOnLed (0xff4cd964), kRing (0xffff7a2a);

    // The edit buffer's items (livedaw_02r::params).
    constexpr int kLevel = 0, kOn = 1, kPanL = 2, kPanR = 3, kAtt = 4, kSwitches = 5, kDelayHi = 6, kDelayLo = 7, kEq = 8;
    constexpr int kDynType = 20, kDynParam = 21, kRouting = 27;
    // The switches' bits.
    constexpr int kPhase = 0, kDelayOn = 1, kEqOn = 2, kHpf = 3, kLpf = 4, kDynOn = 5, kSt = 6;
    constexpr int kSendsBase = 55 * 28;

    const char* const kDynTokens[] = { "Y2CMP", "Y2EXP", "Y2CPH", "Y2CPS", "Y2GAT", "Y2DUK" };
    const char* const kDynNames[] = { "COMP", "EXPAND", "COMPANDER-H", "COMPANDER-S", "GATE", "DUCKING" };

    juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

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
            g.setColour (look().silkDim.withAlpha (0.6f));
            g.drawHorizontalLine ((int) y, cx - 12.0f, cx - 6.0f);
            g.drawHorizontalLine ((int) y, cx + 6.0f, cx + 12.0f);
        }
        hw::silk (g, juce::Rectangle<float> (0.0f, t.getBottom() - 7.0f, 20.0f, 14.0f), utf8 ("\xe2\x88\x9e"), look(), 10.0f,
                  juce::Justification::centredRight, true);
        hw::faderSlot (g, cx, t.getY(), t.getBottom(), look());
        if (addr < 0)
            return;
        const float y = t.getBottom() - t.getHeight() * (float) step / 127.0f;
        hw::faderCap (g, cx, y, 30.0f, look(), stereoLaw ? juce::Colour (0xffc8302a) : juce::Colour());
    }

    void Fader::mouseDown (const juce::MouseEvent&) { dragStart = step; }

    void Fader::mouseDrag (const juce::MouseEvent& e)
    {
        const float perStep = track().getHeight() / 127.0f;
        moveTo (dragStart - juce::roundToInt ((float) e.getDistanceFromDragStartY() / perStep));
    }

    void Fader::mouseDoubleClick (const juce::MouseEvent&)
    {
        // 0 dB: the channels' step 103, STEREO's top.
        const auto law = ctx.engine.getFaderLaw (stereoLaw ? (int) ld::kMain : (int) ld::kChannel);
        moveTo (juce::roundToInt (law.toPosition (0.0f) * 127.0f));
    }

    void Fader::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) { moveTo (step + (w.deltaY > 0 ? 1 : -1)); }

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
        hw::silk (g, b.removeFromTop (14.0f), label, look(), 10.5f);
        auto textArea = b.removeFromBottom (14.0f);
        const float frac = hi > lo ? (float) (value - lo) / (float) (hi - lo) : 0.0f;
        hw::knob (g, b, frac, look(), kRing, centred);
        hw::silk (g, textArea, text ? text (value) : juce::String (value), look(), 10.5f, juce::Justification::centred, true);
    }

    void Encoder::mouseDown (const juce::MouseEvent&) { dragStart = value; }

    void Encoder::mouseDrag (const juce::MouseEvent& e)
    {
        const int range = juce::jmax (1, hi - lo);
        const float perStep = juce::jlimit (1.0f, 6.0f, 200.0f / (float) range);
        moveTo (dragStart - juce::roundToInt ((float) e.getDistanceFromDragStartY() / perStep));
    }

    void Encoder::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) { moveTo (value + (w.deltaY > 0 ? 1 : -1)); }

    //==========================================================================
    Display::Display (UiContext& c, const Theme& t) : ctx (c), theme (t), y (c.engine.getYamaha02R()) { build(); }

    void Display::setFunction (Function f)
    {
        function = f;
        focus = 0;
        build();
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
        auto libraryField = [this] (const juce::String& label, int library, int target)
        {
            Field f;
            f.label = label;
            f.lo = 0;
            f.hi = 127;
            f.onEnter = true;
            f.get = [] { return 0; };
            f.text = [this, library] (int n) { return juce::String (n + 1).paddedLeft ('0', 3) + " " + y.programName (library, n); };
            f.set = [this, library, target] (int n) { y.recall (library, n, target); };
            fields.push_back (f);
        };
        const auto eqModel = models::findModel (models::ChannelEq, "Y2EQ");
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
                for (int g = 0; g < 8; ++g)
                    stateField ("GROUP " + juce::String::charToString ('A' + g), Yamaha02R::GroupOn, g, 0, 1, onOff);
                if (memberOf (s) >= 0)
                {
                    stateField ("FADER GRP", Yamaha02R::FaderGroup, memberOf (s), 0, 4, [] (int v) { return v == 0 ? juce::String ("-") : juce::String::charToString ('A' + v - 1); });
                    stateField ("MUTE GRP", Yamaha02R::MuteGroup, memberOf (s), 0, 4, [] (int v) { return v == 0 ? juce::String ("-") : juce::String::charToString ('E' + v - 1); });
                }
                break;
            case Function::Pair:
                if (pairOf (s) >= 0)
                    stateField ("PAIR", Yamaha02R::Pair, pairOf (s), 0, 1, onOff);
                for (int k = 0; k < 4; ++k)
                    stateField ("AUX " + juce::String (2 * k + 1) + "/" + juce::String (2 * k + 2), Yamaha02R::AuxPair, k, 0, 1, onOff);
                for (int k = 0; k < 4; ++k)
                    stateField ("BUS " + juce::String (2 * k + 1) + "/" + juce::String (2 * k + 2), Yamaha02R::BusPair, k, 0, 1, onOff);
                break;
            case Function::Scene:
                if (faderIndexOf (s) >= 0)
                {
                    stateField ("FADE TIME", Yamaha02R::FadeTime, faderIndexOf (s), 0, 100, [] (int v) { return juce::String (v / 10.0, 1) + " s"; });
                    stateField ("RECALL SAFE", Yamaha02R::RecallSafe, faderIndexOf (s), 0, 1, onOff);
                }
                stateField ("SAFE ENABLE", Yamaha02R::RecallSafeOn, 0, 0, 1, onOff);
                break;
            case Function::Utility:
            {
                stateField ("OSC", Yamaha02R::OscOn, 0, 0, 1, onOff);
                stateField ("LEVEL", Yamaha02R::OscLevel, 0, 0, 96, [] (int v) { return "-" + juce::String (v) + " dB"; });
                stateField ("WAVE", Yamaha02R::OscWave, 0, 0, 3, [] (int v) {
                    const char* w[] = { "100 Hz", "1 kHz", "10 kHz", "NOISE" };
                    return juce::String (w[juce::jlimit (0, 3, v)]);
                });
                fields.push_back ({ "TO ST",
                                    [this] { return (y.getState (Yamaha02R::OscAssign, 0) >> 8) & 1; },
                                    [this] (int v)
                                    {
                                        const int a = juce::jmax (0, y.getState (Yamaha02R::OscAssign, 0));
                                        y.setState (Yamaha02R::OscAssign, 0, v != 0 ? (a | 0x100) : (a & ~0x100));
                                    },
                                    onOff,
                                    0,
                                    1 });
                break;
            }
            case Function::Midi:
            {
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
                break;
            default:
            {
                // AUX 1-8: the strip's send, the aux master; AUX 7-8 their effect's library.
                const int aux = (int) function - (int) Function::Aux1;
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
        return names[(int) function];
    }

    void Display::cursor (int delta)
    {
        if (fields.empty())
            return;
        focus = (focus + delta + (int) fields.size()) % (int) fields.size();
        repaint();
    }

    void Display::turn (int delta)
    {
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
        if (! juce::isPositiveAndBelow (focus, (int) fields.size()))
            return;
        auto& f = fields[(size_t) focus];
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

    void Display::refresh() { repaint(); }

    juce::Rectangle<float> Display::fieldBox (int i) const
    {
        auto page = getLocalBounds().toFloat().reduced (14.0f).withTrimmedTop (40.0f).withTrimmedBottom (28.0f);
        if (function == Function::Meter || function == Function::Eq)
            page = page.withTrimmedRight (page.getWidth() * (function == Function::Eq ? 0.42f : 0.0f));
        constexpr int cols = 4;
        const int rows = juce::jmax (4, ((int) fields.size() + cols - 1) / cols);
        const float w = page.getWidth() / cols, h = juce::jmin (52.0f, page.getHeight() / (float) rows);
        return { page.getX() + w * (float) (i % cols), page.getY() + h * (float) (i / cols), w - 6.0f, h - 6.0f };
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
        {
            paintMeters (g, b.reduced (14.0f).withTrimmedTop (40.0f).withTrimmedBottom (24.0f));
            return;
        }
        if (function == Function::Eq)
            paintEq (g, b.reduced (14.0f).withTrimmedTop (40.0f).withTrimmedBottom (28.0f).withTrimmedLeft (b.getWidth() * 0.6f));
        if (fields.empty())
        {
            g.setColour (kPixelDim);
            g.drawText (stripName (strip) + " has no " + title(), b, juce::Justification::centred);
        }
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
        g.setColour (kPixelDim);
        g.setFont (type::font (type::kCaption));
        g.drawText ("CURSOR / WHEEL / ENTER", b.reduced (14.0f, 6.0f).removeFromBottom (18.0f), juce::Justification::centredRight);
    }

    void Display::paintMeters (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        // METER: the inputs and returns, then the outputs (MAN pp.41-42).
        const int n = kStrips;
        const float w = area.getWidth() / (float) n;
        for (int s = 0; s < n; ++s)
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
            const float level = st != nullptr ? juce::Decibels::gainToDecibels (ctx.meters.get (st, MeterPoint::Output, 0), -60.0f) : -60.0f;
            auto bar = juce::Rectangle<float> (area.getX() + w * (float) s + 1.0f, area.getY(), juce::jmax (1.0f, w - 2.0f), area.getHeight() - 14.0f);
            g.setColour (kLcdFrame);
            g.fillRect (bar);
            g.setColour (kPixel);
            g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * (level + 60.0f) / 60.0f));
            if (s == 0 || s == 16 || s == kTape || s == kEff || s == kBus || s == kAux || s == kStereo)
            {
                g.setFont (type::font (9.0f));
                g.drawText (stripName (s).upToFirstOccurrenceOf (" ", false, false), juce::Rectangle<float> (bar.getX(), bar.getBottom() + 1.0f, 40.0f, 12.0f),
                            juce::Justification::centredLeft);
            }
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
    pan->get = [this] { return juce::jmax (0, y.get (y02r::address (display.getStrip(), kPanL))); };
    pan->set = [this] (int v) { y.set (y02r::address (display.getStrip(), kPanL), v); };
    pan->text = panText;
    addAndMakeVisible (*pan);
    // EQUALIZER: the band keys, Q, F and G of the band, EQ ON.
    const char* bands[] = { "LOW", "L-MID", "H-MID", "HIGH", "EQ ON" };
    for (int k = 0; k < 5; ++k)
    {
        auto key = std::make_unique<ToggleKey> (bands[k], k < 4 ? kSelLed : onColour, theme);
        if (k < 4)
        {
            key->onClick = [this, k] { display.band = k; };
            key->isOn = [this, k] { return display.band == k; };
        }
        else
        {
            key->onClick = [this]
            {
                const int a = y02r::address (display.getStrip(), kSwitches);
                y.set (a, juce::jmax (0, y.get (a)) ^ (1 << kEqOn));
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
        repaint (wheel->getBounds());
    };
    wheel->setTitle ("WHEEL");
    addAndMakeVisible (*wheel);

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
    // green), the function keys grey.
    for (auto* child : getChildren())
        if (auto* k = dynamic_cast<ToggleKey*> (child); k != nullptr && k != &universalKey)
            k->look = &look();
    for (auto& [f, key] : functionKeys)
        key->face = juce::Colour (0xff8f9295);
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
    return f >= a ? f - a : -1;
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
        encoders[(size_t) i]->setLabel (y02r::stripName (encoderStrip (i)).replace ("TAPE ", "TAPE").replace ("CH ", "CH"));
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
    const int index = memory - 1;
    if (index < ctx.show.getNumScenes())
        ctx.show.storeScene (index);
    else
        ctx.show.addScene ("SCENE " + juce::String (memory).paddedLeft ('0', 2));
    display.refresh();
}

void Y02RSurface::recall()
{
    if (memory == 0)
        y.recallInitial();
    else if (memory - 1 < ctx.show.getNumScenes())
        ctx.show.recallScene (memory - 1);
    display.refresh();
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
    for (auto* list : { &routingKeys, &auxKeys, &eqKeys })
        for (auto& k : *list)
            k->refresh();
    flipKey.refresh();
    for (auto* e : { sendLevel.get(), pan.get(), eqQ.get(), eqF.get(), eqG.get() })
        e->refresh();
    eqQ->hi = display.band == 1 || display.band == 2 ? 40 : 42;
    display.refresh();
    repaint (meterArea());
    repaint (memoryArea());
}

juce::Rectangle<int> Y02RSurface::meterArea() const
{
    return { getWidth() - 74, kTopBar + 14, 60, 380 };
}

juce::Rectangle<int> Y02RSurface::memoryArea() const
{
    return { getWidth() - 74 - 250, kTopBar + 14, 236, 80 };
}

void Y02RSurface::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    // The 02R's panel between its wooden cheeks, its sections outlined in silkscreen.
    auto body = getLocalBounds().withTrimmedTop (kTopBar).toFloat();
    hw::cheeks (g, body.removeFromLeft (14.0f), body.removeFromRight (14.0f));
    hw::panel (g, body, look());
    const auto bounds = [] (const juce::Component& c) { return c.getBounds().toFloat(); };
    if (! routingKeys.empty())
        hw::section (g, bounds (*routingKeys[0]).getUnion (bounds (*eqKeys.back())).getUnion (bounds (*eqG)).expanded (12.0f, 22.0f).withTrimmedTop (4.0f),
                     "SELECTED CHANNEL   " + y02r::stripName (display.getStrip()), look());
    hw::section (g, bounds (flipKey).getUnion (bounds (*encOnKeys.back())).getUnion (bounds (*encoders[0])).expanded (10.0f, 12.0f), "", look());
    auto top = getLocalBounds().removeFromTop (kTopBar);
    g.setColour (theme.panel);
    g.fillRect (top);
    g.setColour (theme.text);
    g.setFont (type::font (type::kTitle, true));
    g.drawText ("02R", top.reduced (sizes::kGapLarge, 0), juce::Justification::centredLeft);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption));
    g.drawText ("Not affiliated with or endorsed by Yamaha; 02R is Yamaha's trademark, used only to say what is emulated.",
                top.reduced (sizes::kGapLarge, 0).withTrimmedLeft (90).withTrimmedRight (200), juce::Justification::centredLeft);

    // SCENE MEMORY: the number to store or recall, as the panel's two digits.
    auto mem = memoryArea().toFloat();
    hw::section (g, mem.expanded (8.0f, 10.0f).withHeight (mem.getHeight() + 2.0f * kKey + 40.0f), "SCENE MEMORY", look());
    mem.removeFromTop (16.0f);
    auto digits = mem.removeFromLeft (100.0f);
    g.setColour (juce::Colour (0xff1a0c0c));
    g.fillRoundedRectangle (digits, 4.0f);
    g.setColour (juce::Colour (0xffff5a40));
    g.setFont (type::font (44.0f, true));
    g.drawText (juce::String (memory).paddedLeft ('0', 2), digits, juce::Justification::centred);

    // The stereo meter (21 segments, MAN p.6).
    auto m = meterArea().toFloat();
    hw::silk (g, m.removeFromBottom (16.0f), "STEREO", look(), 10.5f);
    const char* labels[] = { "CLIP", "-2", "-4", "-6", "-12", "-15", "-24", "-35", "-48", "-60", "-72" };
    const float dbfs[] = { -0.1f, -2.0f, -4.0f, -6.0f, -12.0f, -15.0f, -24.0f, -35.0f, -48.0f, -60.0f, -72.0f };
    auto* st = ctx.engine.getMains().empty() ? nullptr : ctx.engine.getMains()[0].get();
    const float segH = m.getHeight() / 11.0f;
    for (int s = 0; s < 11; ++s)
    {
        auto row = juce::Rectangle<float> (m.getX(), m.getY() + segH * (float) s, m.getWidth(), segH);
        hw::silk (g, row.removeFromLeft (30.0f), labels[s], look(), 9.5f, juce::Justification::centredRight, true);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float level = st != nullptr ? juce::Decibels::gainToDecibels (ctx.meters.get (st, MeterPoint::Output, ch), -100.0f) : -100.0f;
            auto led = row.removeFromLeft (row.getWidth() / (float) (2 - ch)).reduced (3.0f, 2.0f);
            const auto colour = s == 0 ? juce::Colour (0xffff4040) : s < 4 ? juce::Colour (0xfff0c030) : juce::Colour (0xff40d070);
            g.setColour (level >= dbfs[s] ? colour : colour.withAlpha (0.15f));
            g.fillRect (led);
        }
    }

    // The SELECTED CHANNEL section's blocks, and the faders' names below them.
    if (! routingKeys.empty())
    {
        // The PARAMETER wheel: a big knurled knob.
        const auto w = wheel->getBounds().toFloat();
        hw::knob (g, w, (float) std::fmod (wheel->getValue() / 48.0 + 1.0e6, 1.0), look());
    }
    g.setFont (type::font (type::kLabel, true));
    const int aux = auxShown();
    for (int i = 0; i < kNumFaders; ++i)
    {
        const auto f = faders[(size_t) i]->getBounds();
        const int s = faderStrip (i);
        juce::String name = s < 16 ? juce::String (s + 1)
                          : s < y02r::kTape ? juce::String (17 + 2 * (s - 16)) + "/" + juce::String (18 + 2 * (s - 16))
                          : s < y02r::kEff  ? "T" + juce::String (s - y02r::kTape + 1)
                                            : y02r::stripName (s);
        hw::silk (g, f.withY (f.getBottom()).withHeight (20).toFloat(), name, look(), 12.0f);
        if (aux >= 0)
        {
            g.setColour (kSelLed.withAlpha (0.8f));
            g.fillRect (f.withY (f.getBottom() + 18).withHeight (2).reduced (16, 0));
        }
        if (i == 16 || i == 20)
        {
            g.setColour (look().silkDim.withAlpha (0.5f));
            g.drawVerticalLine (f.getX() - 4, (float) f.getY(), (float) f.getBottom() + 20.0f);
        }
    }
    if (aux >= 0)
    {
        const auto f = faders[0]->getBounds().toFloat();
        hw::silk (g, f.withY (f.getY() - 16.0f).withHeight (14.0f).withWidth (300.0f), "FADERS: SENDS TO AUX " + juce::String (aux + 1), look(), 11.0f,
                  juce::Justification::centredLeft);
    }
}

void Y02RSurface::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (kTopBar);
    universalKey.setBounds (top.removeFromRight (190).reduced (sizes::kGap, 7));

    // The upper panel: the display and its function keys, the SELECTED CHANNEL
    // section, the scene memory, the cursor keys and wheel, the stereo meter.
    auto upper = area.removeFromTop (420).reduced (sizes::kGapLarge, sizes::kGap);
    auto lcdColumn = upper.removeFromLeft (860);
    display.setBounds (lcdColumn.removeFromTop (270));
    lcdColumn.removeFromTop (sizes::kGap);
    const int perRow = 7;
    for (int r = 0; r < 3; ++r)
    {
        auto keyRow = lcdColumn.removeFromTop (kKey + 6);
        const int w = keyRow.getWidth() / perRow;
        for (int k = 0; k < perRow; ++k)
            if (const int n = r * perRow + k; n < (int) functionKeys.size())
                functionKeys[(size_t) n].second->setBounds (keyRow.removeFromLeft (w).reduced (3, 3));
    }
    upper.removeFromLeft (sizes::kGapLarge);
    upper.removeFromRight (330); // the scene memory and the meter

    // SELECTED CHANNEL: ROUTING, AUX, PAN, EQUALIZER.
    auto sel = upper;
    sel.removeFromTop (20);
    auto routing = sel.removeFromTop (kKey);
    const int rw = routing.getWidth() / 10;
    for (auto& k : routingKeys)
        k->setBounds (routing.removeFromLeft (rw).reduced (2, 0));
    sel.removeFromTop (sizes::kGap);
    auto auxRow = sel.removeFromTop (2 * kKey + 6);
    auto auxKeysArea = auxRow.removeFromLeft (auxRow.getWidth() - 200);
    for (int k = 0; k < 8; ++k)
        auxKeys[(size_t) k]->setBounds (auxKeysArea.getX() + (k % 4) * (auxKeysArea.getWidth() / 4), auxKeysArea.getY() + (k / 4) * (kKey + 6),
                                        auxKeysArea.getWidth() / 4 - 4, kKey);
    sendLevel->setBounds (auxRow.removeFromLeft (110));
    auxKeys[8]->setBounds (auxRow.removeFromTop (kKey).reduced (4, 0));
    sel.removeFromTop (sizes::kGap);
    auto eqRow = sel.removeFromTop (110);
    pan->setBounds (eqRow.removeFromLeft (100));
    eqRow.removeFromLeft (sizes::kGap);
    for (auto* e : { eqQ.get(), eqF.get(), eqG.get() })
        e->setBounds (eqRow.removeFromLeft (100));
    auto eqKeysArea = eqRow.reduced (4, 0);
    for (int k = 0; k < 5; ++k)
        eqKeys[(size_t) k]->setBounds (eqKeysArea.getX() + (k % 2) * (eqKeysArea.getWidth() / 2), eqKeysArea.getY() + (k / 2) * (kKey + 4),
                                       eqKeysArea.getWidth() / 2 - 4, kKey);
    sel.removeFromTop (sizes::kGap);
    // The cursor keys in a cross, ENTER beside, the wheel.
    auto controls = sel;
    auto cross = controls.removeFromLeft (3 * 60 + 8);
    const int cx = cross.getX(), cy = cross.getY();
    panelKeys[4]->setBounds (cx, cy + kKey + 4, 56, kKey);
    panelKeys[5]->setBounds (cx + 60, cy, 56, kKey);
    panelKeys[6]->setBounds (cx + 60, cy + 2 * (kKey + 4), 56, kKey);
    panelKeys[7]->setBounds (cx + 120, cy + kKey + 4, 56, kKey);
    panelKeys[8]->setBounds (controls.removeFromLeft (110).withHeight (kKey).withY (cy + kKey + 4).reduced (4, 0));
    wheel->setBounds (controls.removeFromLeft (120).withHeight (120));

    // The scene memory's keys under its digits.
    const auto mem = memoryArea();
    for (int k = 0; k < 4; ++k)
        panelKeys[(size_t) k]->setBounds (mem.getX() + (k % 2) * 118, mem.getBottom() + 8 + (k / 2) * (kKey + 6), 110, kKey);

    // The encoder row: TAPE 1-16 and EFF1-2, SEL and ON under each, FLIP at the left.
    auto lower = area.reduced (sizes::kGapLarge, 0);
    auto encRow = lower.removeFromTop (142);
    flipKey.setBounds (encRow.removeFromLeft (70).withSizeKeepingCentre (64, kKey));
    const int ew = encRow.getWidth() / kNumEncoders;
    for (int i = 0; i < kNumEncoders; ++i)
    {
        auto col = encRow.removeFromLeft (ew).reduced (2, 0);
        encOnKeys[(size_t) i]->setBounds (col.removeFromBottom (24).reduced (4, 0));
        encSelKeys[(size_t) i]->setBounds (col.removeFromBottom (24).reduced (4, 0).translated (0, -2));
        encoders[(size_t) i]->setBounds (col.reduced (0, 2));
    }

    // The faders: 21 strips, SEL and ON above each, its name below.
    lower.removeFromTop (sizes::kGap);
    const int w = juce::jmin (88, lower.getWidth() / kNumFaders);
    int x = lower.getX() + (lower.getWidth() - w * kNumFaders - 16) / 2;
    for (int i = 0; i < kNumFaders; ++i)
    {
        if (i == 16 || i == 20)
            x += 8;
        auto col = juce::Rectangle<int> (x, lower.getY() + 18, w, lower.getHeight() - 42);
        selKeys[(size_t) i]->setBounds (col.removeFromTop (28).reduced (6, 0));
        col.removeFromTop (4);
        onKeys[(size_t) i]->setBounds (col.removeFromTop (28).reduced (6, 0));
        col.removeFromTop (4);
        faders[(size_t) i]->setBounds (col);
        x += w;
    }
}

} // namespace livedaw::ui
