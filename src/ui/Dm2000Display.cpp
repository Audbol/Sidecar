// The DM2000's LCD: 320 x 240 dots, drawn on a bitmap, with pages after the V2 Owner's Manual's
// pictures (pp.52-54, 67-81, 84-157, 165-172, 183-188, 217-219, 254, 270-279) and photographs of real
// consoles. The layouts are Sidecar's own drawing of those pages; nothing is copied from the console.

#include "daw/Session.h"
#include "engine/AppPaths.h"
#include "engine/ConsoleState.h"
#include "engine/DirectOut.h"
#include "engine/Dm2000Midi.h"
#include "engine/MixerEngine.h"
#include "engine/Models.h"
#include "engine/Show.h"
#include "ui/Dm2000Surface.h"
#include "ui/DotFont.h"
#include "ui/HomeScreens.h"
#include "ui/MeterBank.h"
#include "ui/RoutingGroups.h"
#include "ui/universal/Groups.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <tuple>

namespace livedaw::ui::dm2k
{

using namespace universal;
using dots::kFont5x7;
using dots::tinyGlyph;

namespace
{
    // The LCD, from photographs: a pale yellow field, blue dots, the title bar's a shade more violet.
    const juce::Colour kField (0xffe4d566), kFieldLow (0xffd8c95c), kInk (0xff2438b4), kInkTitle (0xff5b2fa6);

    constexpr int kTitleH = 14, kTabsY = 229;
    constexpr int kChannelsPerPage = 24;

    /** The first AUX in the bus slots (livedaw_engine::profile::dm2000::AUX). */
    constexpr int kAuxSlot = 8;

    KnobBinding bindBool (const juce::String& label, std::atomic<bool>& v, const juce::String& on = "ON", const juce::String& off = "OFF")
    {
        KnobBinding b;
        b.label = label;
        b.get = [&v] { return v.load() ? 1.0f : 0.0f; };
        b.set = [&v] (float x) { v = x >= 0.5f; };
        b.text = [&v, on, off] { return v.load() ? on : off; };
        b.steps = 2.0f;
        return b;
    }

    KnobBinding bindId (models::Table table, int model, std::atomic<float>* values, const juce::String& id, const juce::String& label)
    {
        const auto& m = models::model (table, model);
        const int i = m.find (id);
        if (i < 0)
            return {};
        auto b = bindModelParam (values[i], m.params[(size_t) i]);
        b.label = label;
        return b;
    }

    KnobBinding bindEq (Strip& s, const juce::String& id, const juce::String& label)
    {
        auto& eq = s.params.eq();
        return bindId (s.isOutput() ? models::BusEq : models::ChannelEq, eq.model.load(), eq.values, id, label);
    }

    KnobBinding bindInt (const juce::String& label, std::function<int()> get, std::function<void (int)> set, int lo, int hi, std::function<juce::String (int)> text)
    {
        KnobBinding b;
        b.label = label;
        b.get = [get, lo, hi] { return hi > lo ? (float) (get() - lo) / (float) (hi - lo) : 0.0f; };
        b.set = [set, lo, hi] (float v) { set (lo + juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * (float) (hi - lo))); };
        b.text = [get, text] { return text (get()); };
        b.steps = (float) (hi - lo + 1);
        return b;
    }

    KnobBinding bindMask (ld::GroupMask& mask, int group)
    {
        KnobBinding b;
        b.get = [&mask, group] { return mask.contains (group) ? 1.0f : 0.0f; };
        b.set = [&mask, group] (float v)
        {
            if ((v >= 0.5f) != mask.contains (group))
                mask.toggle (group);
        };
        b.text = [&mask, group] { return juce::String (mask.contains (group) ? "*" : "-"); };
        b.steps = 2.0f;
        return b;
    }

    float toDb (float linear) { return juce::Decibels::gainToDecibels (linear, -100.0f); }

    /** The LCD's font has no infinity sign. */
    juce::String ascii (const juce::String& s) { return s.replace (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")), "oo").replace ("inf", "oo"); }

    KnobBinding bindRoute (ld::SendParams& send)
    {
        KnobBinding b;
        b.get = [&send] { return send.enabled.load() && send.levelDb.load() > -89.5f ? 1.0f : 0.0f; };
        b.set = [&send] (float v)
        {
            send.levelDb = 0.0f;
            send.enabled = v >= 0.5f;
        };
        b.steps = 2.0f;
        return b;
    }

    const char* const kBandIds[] = { "l", "lm", "hm", "h" };
    const char* const kBandNames[] = { "LOW", "L-MID", "H-MID", "HIGH" };
    const char* const kGroupLetters = "ABCDEFGHIJKLMNOPQRSTUVWX";

    /** The first strip index a layer tab of `arg` shows (0-3: CH 1-24 ...). */
    int firstOf (int arg) { return arg * kChannelsPerPage; }
    juce::String rangeName (int arg) { return juce::String (firstOf (arg) + 1) + "-" + juce::String (firstOf (arg) + kChannelsPerPage); }

    /** Dm2000Midi's setup text: 28 numbers (livedaw_dm2000::SETUP_BYTES). */
    constexpr int kSetupValues = 28;

    // The surface's own settings (the app settings: they are the console's, not a show's).
    constexpr const char* kPairsKey = "dm2000.pairs";       // the input pairs and the output pairs, in hex
    constexpr const char* kUserKeysKey = "dm2000.userkeys"; // sixteen of userKeyNames()
    constexpr const char* kCopyKey = "dm2000.copy";         // Channel Copy Parameter's bits

    /** PREFER1's preferences, in Display::Pref's order, by the manual's names (p.274). The Auto ...
        Display ones of the SELECTED CHANNEL's knobs start on, as the surface was before it had this page. */
    struct PrefSpec
    {
        const char* key;
        const char* name;
        bool initial;
    };
    const PrefSpec kPrefs[] = {
        { "dm2000.pref.autoRouting", "Auto ROUTING Display", false },
        { "dm2000.pref.autoPhase", "Auto PHASE/INSERT Display", false },
        { "dm2000.pref.autoDelay", "Auto DELAY Display", true },
        { "dm2000.pref.autoAux", "Auto AUX/MATRIX Display", true },
        { "dm2000.pref.autoDynamics", "Auto DYNAMICS Display", true },
        { "dm2000.pref.autoPan", "Auto PAN/SURROUND Display", true },
        { "dm2000.pref.autoEq", "Auto EQUALIZER Display", true },
        { "dm2000.pref.autoSelect", "Auto Channel Select", false },
        { "dm2000.pref.storeConfirm", "Store Confirmation", true },
        { "dm2000.pref.recallConfirm", "Recall Confirmation", false },
    };

    /** The libraries, in Display::Library's order: the folder of its memories, its name on the page,
        its files, and its memories' numbers as the manual has them (pp.166-172, 346): the first, the
        first a user may store into, the last. */
    struct LibrarySpec
    {
        const char* folder;
        const char* name;
        const char* extension;
        int first, firstUser, last;
    };
    const LibrarySpec kLibraries[] = {
        { "CHANNEL", "CHANNEL", Show::channelPresetExtension, 0, 2, 128 },
        { "EQ", "EQUALIZER", Show::channelPresetExtension, 1, 41, 200 },
        { "GATE", "GATE", Show::channelPresetExtension, 1, 5, 128 },
        { "COMP", "COMP", Show::channelPresetExtension, 1, 37, 128 },
        { "EFFECT", "EFFECT", Show::fxPresetExtension, 1, 62, 128 },
        { "INPATCH", "INPUT PATCH", ".txt", 0, 1, 32 },
        { "OUTPATCH", "OUTPUT PATCH", ".txt", 0, 1, 32 },
    };

    // userKeyNames()' ranges.
    constexpr int kKeyScenePlus = 1, kKeySceneMinus = 2, kKeyScene = 3, kKeyFxBypass = 19, kKeyGeq = 27, kKeyOsc = 33, kKeyMuteMaster = 34, kKeyLibrary = 46, kKeyCount = 54;
} // namespace

//==============================================================================
int processOrder (int comp, int insert, bool insertFirst) noexcept
{
    comp = juce::jlimit (0, 2, comp);
    insert = juce::jlimit (0, 2, insert);
    if (comp == insert && insertFirst)
        return 9 + comp;
    return comp * 3 + insert;
}

void decodeOrder (int order, int& comp, int& insert, bool& insertFirst) noexcept
{
    order = juce::jlimit (0, 11, order);
    insertFirst = order >= 9;
    if (insertFirst)
    {
        comp = insert = order - 9;
        return;
    }
    comp = order / 3;
    insert = order % 3;
}

KnobBinding bindAttenuator (Strip& s)
{
    auto b = bindLinear ("ATT", s.params.input.trimDb, -96.0f, 12.0f, " dB", 1, 0.0f);
    b.steps = 1081.0f;
    b.text = [&s] { return juce::String (s.params.input.trimDb.load(), 1) + " dB"; };
    return b;
}

KnobBinding bindDelay (Strip& s)
{
    KnobBinding b;
    b.label = "DELAY";
    auto& delay = s.params.input.delay;
    b.get = [&delay] { return juce::jlimit (0.0f, 1.0f, delay.load() / 43400.0f); };
    b.set = [&delay] (float v) { delay = (float) juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 43400.0f); };
    b.text = [&delay] { return juce::String (juce::roundToInt (delay.load())); };
    b.reset = [&delay] { delay = 0.0f; };
    b.steps = 43401.0f;
    return b;
}

KnobBinding bindPanYamaha (Strip& s)
{
    KnobBinding b;
    b.label = "PAN";
    auto& pan = s.params.pan;
    b.get = [&pan] { return juce::jlimit (0.0f, 1.0f, (pan.load() + 100.0f) / 200.0f); };
    b.set = [&pan] (float v) { pan = (float) juce::roundToInt ((juce::jlimit (0.0f, 1.0f, v) * 2.0f - 1.0f) * 63.0f) * 100.0f / 63.0f; };
    b.text = [&pan]
    {
        const int v = juce::roundToInt (pan.load() * 0.63f);
        return v == 0 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (std::abs (v));
    };
    b.reset = [&pan] { pan = 0.0f; };
    b.bipolar = true;
    b.steps = 127.0f;
    return b;
}

juce::String lcdValue (const KnobBinding& b)
{
    if (! b.isValid() || ! b.text)
        return {};
    return ascii (b.text()).replace (" kHz", "k").replace ("kHz", "k").replace (" Hz", "").replace ("Hz", "").replace (" ms", "ms").replace (" dB", "dB").replace ("-inf", "-oo");
}

void stepBy (const KnobBinding& b, int delta)
{
    if (! b.isValid() || ! b.set)
        return;
    const float step = b.steps > 1.0f ? 1.0f / (b.steps - 1.0f) : 0.01f;
    b.set (juce::jlimit (0.0f, 1.0f, b.get() + (float) delta * step));
}

const juce::StringArray& assignNames()
{
    static const juce::StringArray names { "No Assign", "Attenuator", "Delay Time", "EQ On", "Comp On", "Gate On", "Insert On", "Phase", "EQ Type", "Pan", "Comp Threshold", "Gate Threshold" };
    return names;
}

KnobBinding bindAssign (UiContext&, Strip& s, int item)
{
    auto& p = s.params;
    switch (item)
    {
        case 1:  return bindAttenuator (s);
        case 2:  return bindDelay (s);
        case 3:  return bindBool ("EQ", p.eq().on);
        case 4:  return bindBool ("COMP", p.dynamics().on);
        case 5:  return s.isInput() ? bindBool ("GATE", p.gate().on) : KnobBinding {};
        case 6:  return bindBool ("INSERT", p.preInsert().on);
        case 7:  return s.isInput() ? bindBool ("PHASE", p.input.invert) : KnobBinding {};
        case 8:  return bindEq (s, "type", "EQ TYPE");
        case 9:  return s.isInput() ? bindPanYamaha (s) : KnobBinding {};
        case 10: return bindId (models::Dynamics, p.dynamics().model.load(), p.dynamics().values, "thr", "COMP TH.");
        case 11: return s.isInput() ? bindId (models::Dynamics, p.gate().model.load(), p.gate().values, "thr", "GATE TH.") : KnobBinding {};
        default: return {};
    }
}

//==============================================================================
const std::vector<Display::Tab>& Display::tabsOf (Group g)
{
    using D = Display;
    static const std::array<std::vector<Tab>, (size_t) Group::kCount> all = []
    {
        std::array<std::vector<Tab>, (size_t) Group::kCount> a;
        const auto put = [&a] (Group g2, std::vector<Tab> t) { a[(size_t) g2] = std::move (t); };
        put (Group::MatrixSend, { { "MTRX SEND", &D::buildMatrix, &D::drawMatrix, 0 } });
        put (Group::AuxSend, { { "AUX1-24", &D::buildAux, &D::drawAux, 0 }, { "AUX25-48", &D::buildAux, &D::drawAux, 1 }, { "AUX49-72", &D::buildAux, &D::drawAux, 2 }, { "AUX73-96", &D::buildAux, &D::drawAux, 3 } });
        put (Group::EncoderMode, { { "ASSIGN", &D::buildAssign, &D::drawAssign, 0 } });
        put (Group::Effects, { { "FX EDIT", &D::buildFxEdit, &D::drawFxEdit, 0 }, { "FX LIBRARY", &D::buildLibrary, &D::drawLibrary, (int) Library::Effects }, { "GEQ EDIT", &D::buildGeq, &D::drawGeq, 0 } });
        put (Group::Routing, { { "RT 1-24", &D::buildRouting, &D::drawRouting, 0 }, { "RT 25-48", &D::buildRouting, &D::drawRouting, 1 }, { "RT 49-72", &D::buildRouting, &D::drawRouting, 2 }, { "RT 73-96", &D::buildRouting, &D::drawRouting, 3 }, { "BUS TO ST", &D::buildBusToStereo, &D::drawBusToStereo, 0 } });
        put (Group::PhaseInsert, { { "PHASE1-48", &D::buildPhase, &D::drawPhase, 0 }, { "PHASE49-96", &D::buildPhase, &D::drawPhase, 2 }, { "INSERT", &D::buildInsert, &D::drawInsert, 0 } });
        put (Group::Delay, { { "DLY 1-24", &D::buildDelay, &D::drawDelay, 0 }, { "DLY 25-48", &D::buildDelay, &D::drawDelay, 1 }, { "DLY 49-72", &D::buildDelay, &D::drawDelay, 2 }, { "DLY 73-96", &D::buildDelay, &D::drawDelay, 3 }, { "OUT DELAY", &D::buildDelay, &D::drawDelay, 4 } });
        put (Group::Dynamics, { { "GATE EDIT", &D::buildGate, &D::drawGate, 0 }, { "GATE LIB", &D::buildLibrary, &D::drawLibrary, (int) Library::Gate }, { "COMP EDIT", &D::buildComp, &D::drawComp, 0 }, { "COMP LIB", &D::buildLibrary, &D::drawLibrary, (int) Library::Comp } });
        put (Group::Pan, { { "PAN 1-24", &D::buildPan, &D::drawPan, 0 }, { "PAN25-48", &D::buildPan, &D::drawPan, 1 }, { "PAN49-72", &D::buildPan, &D::drawPan, 2 }, { "PAN73-96", &D::buildPan, &D::drawPan, 3 } });
        put (Group::Equalizer, { { "EQ EDIT", &D::buildEq, &D::drawEq, 0 }, { "EQ LIBRARY", &D::buildLibrary, &D::drawLibrary, (int) Library::Eq }, { "ATT 1-24", &D::buildAtt, &D::drawAtt, 0 }, { "ATT 25-48", &D::buildAtt, &D::drawAtt, 1 }, { "ATT 49-72", &D::buildAtt, &D::drawAtt, 2 }, { "ATT 73-96", &D::buildAtt, &D::drawAtt, 3 }, { "OUT ATT", &D::buildAtt, &D::drawAtt, 4 } });
        put (Group::SceneMemory, { { "SCENE", &D::buildScene, &D::drawScene, 0 } });
        put (Group::Monitor, { { "MONITOR", &D::buildMonitor, &D::drawMonitor, 0 } });
        put (Group::View, { { "PARAMETER", &D::buildView, &D::drawView, 0 }, { "FADER1-24", &D::buildFaderView, &D::drawFaderView, 0 }, { "FADER25-48", &D::buildFaderView, &D::drawFaderView, 1 }, { "FADER49-72", &D::buildFaderView, &D::drawFaderView, 2 }, { "FADER73-96", &D::buildFaderView, &D::drawFaderView, 3 }, { "MASTER", &D::buildFaderView, &D::drawFaderView, 4 }, { "LIBRARY", &D::buildLibrary, &D::drawLibrary, (int) Library::Channel } });
        put (Group::Meter, { { "CH 1-24", &D::buildMeter, &D::drawMeter, 0 }, { "CH 25-48", &D::buildMeter, &D::drawMeter, 1 }, { "CH 49-72", &D::buildMeter, &D::drawMeter, 2 }, { "CH 73-96", &D::buildMeter, &D::drawMeter, 3 }, { "MASTER", &D::buildMeter, &D::drawMeter, 4 } });
        put (Group::FaderGroup, { { "IN FADER", &D::buildGroupGrid, &D::drawGroupGrid, 0 }, { "IN MUTE", &D::buildGroupGrid, &D::drawGroupGrid, 1 }, { "OUT FADER", &D::buildGroupGrid, &D::drawGroupGrid, 2 }, { "OUT MUTE", &D::buildGroupGrid, &D::drawGroupGrid, 3 }, { "IN MASTER", &D::buildGroupMaster, &D::drawGroupMaster, 0 }, { "OUT MASTER", &D::buildGroupMaster, &D::drawGroupMaster, 1 } });
        put (Group::InputPatch, { { "PATCH1-24", &D::buildPatch, &D::drawPatch, 0 }, { "PATCH25-48", &D::buildPatch, &D::drawPatch, 1 }, { "PATCH49-72", &D::buildPatch, &D::drawPatch, 2 }, { "PATCH73-96", &D::buildPatch, &D::drawPatch, 3 }, { "IN LIB", &D::buildLibrary, &D::drawLibrary, (int) Library::InputPatch } });
        // The Output Patch pages the engine can back (p.79): the Output Channels' Insert In patch isn't one.
        put (Group::OutputPatch, { { "SLOT1-2", &D::buildOutPatch, &D::drawOutPatch, 0 }, { "SLOT3-4", &D::buildOutPatch, &D::drawOutPatch, 1 }, { "SLOT5-6", &D::buildOutPatch, &D::drawOutPatch, 2 }, { "OMNI OUT", &D::buildOutPatch, &D::drawOutPatch, 3 }, { "D.OUT1-48", &D::buildDirectOut, &D::drawDirectOut, 0 }, { "D.OUT49-96", &D::buildDirectOut, &D::drawDirectOut, 1 }, { "2TR OUT", &D::buildOutPatch, &D::drawOutPatch, 4 }, { "INS IN", &D::buildOutInsert, &D::drawOutInsert, 0 }, { "OUT LIB", &D::buildLibrary, &D::drawLibrary, (int) Library::OutputPatch } });
        put (Group::Pair, { { "CH1-48", &D::buildPair, &D::drawPair, 0 }, { "CH49-96", &D::buildPair, &D::drawPair, 1 }, { "OUTPUT", &D::buildPair, &D::drawPair, 2 } });
        put (Group::Midi, { { "SETUP", &D::buildMidiSetup, &D::drawMidiSetup, 0 }, { "PGM ASGN", &D::buildMidiProgram, &D::drawMidiProgram, 0 }, { "CTL ASGN", &D::buildMidiControl, &D::drawMidiControl, 0 }, { "BULK", &D::buildNone, &D::drawNone, 0 } });
        put (Group::Setup, { { "PREFER1", &D::buildPrefs, &D::drawPrefs, 0 }, { "PREFER2", &D::buildPrefs, &D::drawPrefs, 1 } });
        put (Group::Utility, { { "OSCILLATOR", &D::buildOscillator, &D::drawOscillator, 0 }, { "CH STATUS", &D::buildChannelStatus, &D::drawChannelStatus, 0 } });
        put (Group::Dio, { { "WORD CLOCK", &D::buildWordClock, &D::drawWordClock, 0 } });
        put (Group::UserKeys, { { "USER DEF", &D::buildUserKeys, &D::drawUserKeys, 0 } });
        put (Group::Remote, { { "REMOTE1", &D::buildRemote, &D::drawRemote, 0 }, { "REMOTE2", &D::buildRemote, &D::drawRemote, 1 }, { "REMOTE3", &D::buildRemote, &D::drawRemote, 2 }, { "REMOTE4", &D::buildRemote, &D::drawRemote, 3 } });
        put (Group::Automix, { { "MAIN", &D::buildAutomix, &D::drawAutomix, 0 } });
        put (Group::TrackArming, { { "ARM 1-24", &D::buildArming, &D::drawArming, 0 }, { "ARM25-48", &D::buildArming, &D::drawArming, 1 }, { "ARM49-64", &D::buildArming, &D::drawArming, 2 } });
        put (Group::Locator, { { "LOCATE", &D::buildLocator, &D::drawLocator, 0 } });
        put (Group::Data, { { "SAVE", &D::buildData, &D::drawData, 0 } });
        return a;
    }();
    return all[(size_t) g];
}

const std::vector<Display::Tab>& Display::tabs() const { return tabsOf (group); }
int Display::getNumPages() const { return (int) tabs().size(); }
juce::String Display::getTabName (int p) const { return juce::isPositiveAndBelow (p, getNumPages()) ? tabs()[(size_t) p].name : ""; }

Display::Display (UiContext& c) : ctx (c), dots ((size_t) (kDotsW * kDotsH), 0), image (juce::Image::RGB, kWidth, kHeight, true)
{
    setOpaque (true);
    libraryRoot = paths::documents().getChildFile ("Library").getChildFile ("DM2000");
    for (size_t l = 0; l < libraryChoice.size(); ++l)
        libraryChoice[l] = kLibraries[l].firstUser;

    // The pairs and the User Defined Keys, as they were left. The keys start on the manual's Bank B,
    // Scene 1-16 Recall (p.285): its Bank A is the surround monitor's, which Sidecar hasn't.
    juce::StringArray pairs, keys;
    pairs.addTokens (ctx.settings.getValue (kPairsKey), " ", "");
    pairs.removeEmptyStrings();
    if (pairs.size() == 2)
    {
        inputPairs = (uint64_t) pairs[0].getHexValue64() & ((uint64_t (1) << 48) - 1);
        outputPairs = (uint32_t) pairs[1].getHexValue32() & 0x3ffu;
    }
    keys.addTokens (ctx.settings.getValue (kUserKeysKey), " ", "");
    keys.removeEmptyStrings();
    for (int k = 0; k < 16; ++k)
        userKeys[(size_t) k] = keys.size() == 16 ? juce::jlimit (0, kKeyCount - 1, keys[k].getIntValue()) : kKeyScene + k;
    rebuild();
}

Strip* Display::selected() const { return ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr; }

Strip* Display::layerStrip (int layer, int i) const
{
    if (layer <= 3)
    {
        const auto& c = ctx.engine.getChannels();
        const int n = firstOf (layer) + i;
        return juce::isPositiveAndBelow (n, (int) c.size()) ? c[(size_t) n].get() : nullptr;
    }
    const auto& b = ctx.engine.getBuses();
    if (juce::isPositiveAndBelow (i, (int) b.size()))
        return b[(size_t) i].get();
    const auto& m = ctx.engine.getMatrices();
    const int n = i - (int) b.size();
    return juce::isPositiveAndBelow (n, (int) m.size()) ? m[(size_t) n].get() : nullptr;
}

juce::String Display::idOf (const Strip* s) const
{
    if (s == nullptr)
        return "----";
    const int n = s->getNumber();
    switch (s->getKind())
    {
        case StripKind::Channel: return "CH" + juce::String (n);
        case StripKind::Bus:     return n <= kAuxSlot ? "BUS" + juce::String (n) : (n - kAuxSlot < 10 ? "AUX" + juce::String (n - kAuxSlot) : "AX" + juce::String (n - kAuxSlot));
        case StripKind::Matrix:  return "MT" + juce::String (n);
        case StripKind::Main:    return "ST";
        case StripKind::Aux:     break;
    }
    return "AUX" + juce::String (n);
}

juce::String Display::shortName (const Strip* s) const { return s != nullptr ? s->getName().removeCharacters (" ").substring (0, 8) : juce::String ("----"); }

//==============================================================================
void Display::clear() { std::fill (dots.begin(), dots.end(), (uint8_t) 0); }

void Display::dot (int x, int y, bool on)
{
    if (x >= 0 && x < kDotsW && y >= 0 && y < kDotsH)
        dots[(size_t) (y * kDotsW + x)] = on ? 1 : 0;
}

bool Display::isDotOn (int x, int y) const noexcept { return x >= 0 && x < kDotsW && y >= 0 && y < kDotsH && dots[(size_t) (y * kDotsW + x)] != 0; }

void Display::hLine (int x0, int x1, int y, bool on)
{
    for (int x = x0; x <= x1; ++x)
        dot (x, y, on);
}

void Display::vLine (int x, int y0, int y1, bool on)
{
    for (int y = y0; y <= y1; ++y)
        dot (x, y, on);
}

void Display::box (juce::Rectangle<int> r, bool on)
{
    hLine (r.getX(), r.getRight() - 1, r.getY(), on);
    hLine (r.getX(), r.getRight() - 1, r.getBottom() - 1, on);
    vLine (r.getX(), r.getY(), r.getBottom() - 1, on);
    vLine (r.getRight() - 1, r.getY(), r.getBottom() - 1, on);
}

void Display::fill (juce::Rectangle<int> r, bool on)
{
    for (int y = r.getY(); y < r.getBottom(); ++y)
        hLine (r.getX(), r.getRight() - 1, y, on);
}

void Display::dottedH (int x0, int x1, int y)
{
    for (int x = x0; x <= x1; x += 2)
        dot (x, y);
}

void Display::dottedV (int x, int y0, int y1)
{
    for (int y = y0; y <= y1; y += 2)
        dot (x, y);
}

void Display::dottedBox (juce::Rectangle<int> r)
{
    dottedH (r.getX(), r.getRight() - 1, r.getY());
    dottedH (r.getX(), r.getRight() - 1, r.getBottom() - 1);
    dottedV (r.getX(), r.getY(), r.getBottom() - 1);
    dottedV (r.getRight() - 1, r.getY(), r.getBottom() - 1);
}

int Display::textWidth (const juce::String& s, bool tiny) const { return s.length() * (tiny ? 4 : 6) - 1; }

void Display::text (int x, int y, const juce::String& s, bool tiny, bool on)
{
    for (auto p = s.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        if (tiny)
        {
            if (const auto* glyph = tinyGlyph (c))
                for (int col = 0; col < 3; ++col)
                    for (int row = 0; row < 5; ++row)
                        if ((glyph[col] >> row) & 1)
                            dot (x + col, y + row, on);
            x += 4;
            continue;
        }
        const int index = c >= 32 && c <= 126 ? (int) c - 32 : 0;
        const auto* glyph = kFont5x7[index];
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if ((glyph[col] >> row) & 1)
                    dot (x + col, y + row, on);
        x += 6;
    }
}

void Display::textCentred (juce::Rectangle<int> r, const juce::String& s, bool tiny, bool on)
{
    text (r.getCentreX() - textWidth (s, tiny) / 2, r.getCentreY() - (tiny ? 2 : 3), s, tiny, on);
}

void Display::textTall (int x, int y, const juce::String& s, bool on)
{
    // The title bar's letters: the 5x7 font, its rows repeated to ten dots, struck twice.
    static const int repeat[7] = { 2, 1, 2, 1, 2, 1, 1 };
    for (auto p = s.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        const int index = c >= 32 && c <= 126 ? (int) c - 32 : 0;
        const auto* glyph = kFont5x7[index];
        for (int col = 0; col < 5; ++col)
        {
            int yy = y;
            for (int row = 0; row < 7; ++row)
            {
                for (int n = 0; n < repeat[row]; ++n, ++yy)
                    if ((glyph[col] >> row) & 1)
                    {
                        dot (x + col, yy, on);
                        dot (x + col + 1, yy, on);
                    }
            }
        }
        x += 7;
    }
}

void Display::inverseText (juce::Rectangle<int> r, const juce::String& s, bool tiny)
{
    fill (r);
    textCentred (r, s, tiny, false);
}

void Display::circle (int cx, int cy, int r)
{
    // A midpoint circle, four-way symmetric.
    int x = r, y = 0, err = 1 - r;
    while (x >= y)
    {
        for (auto [dx, dy] : { std::pair { x, y }, std::pair { y, x } })
        {
            dot (cx + dx, cy + dy);
            dot (cx - dx, cy + dy);
            dot (cx + dx, cy - dy);
            dot (cx - dx, cy - dy);
        }
        ++y;
        if (err < 0)
            err += 2 * y + 1;
        else
        {
            --x;
            err += 2 * (y - x) + 1;
        }
    }
}

void Display::disc (int cx, int cy, int r, bool on)
{
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x)
            if (x * x + y * y <= r * r)
                dot (cx + x, cy + y, on);
}

void Display::dial (int cx, int cy, int r, float value, bool filled)
{
    // The pointer sweeps 300 degrees, from lower left over the top to lower right.
    const float angle = juce::degreesToRadians (-150.0f + 300.0f * juce::jlimit (0.0f, 1.0f, value));
    const float sx = std::sin (angle), sy = -std::cos (angle);
    if (filled)
    {
        disc (cx, cy, r);
        for (int i = 0; i <= r; ++i)
            dot (cx + juce::roundToInt ((float) i * sx), cy + juce::roundToInt ((float) i * sy), false);
    }
    else
    {
        circle (cx, cy, r);
        for (int i = 0; i <= r; ++i)
            dot (cx + juce::roundToInt ((float) i * sx), cy + juce::roundToInt ((float) i * sy));
    }
}

void Display::frame (juce::Rectangle<int> r, const juce::String& title)
{
    box (r);
    if (title.isNotEmpty())
    {
        const int w = textWidth (title) + 4, x = r.getCentreX() - w / 2;
        fill ({ x, r.getY() - 1, w, 3 }, false);
        text (x + 2, r.getY() - 3, title);
        vLine (x, r.getY() - 2, r.getY() + 4);
        vLine (x + w - 1, r.getY() - 2, r.getY() + 4);
    }
}

void Display::pageTitle (const juce::String& title, const juce::String& channel)
{
    const int w = textWidth (title) + 6;
    box ({ 1, 15, w, 11 });
    text (4, 17, title);
    if (channel.isNotEmpty())
    {
        box ({ 200, 15, 119, 11 });
        text (203, 17, channel.substring (0, 18));
    }
}

void Display::heart (int cx, int cy, bool whole)
{
    static const char* const rows[] = { " ## ## ", "#######", "#######", " ##### ", "  ###  ", "   #   " };
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 7; ++x)
            if (rows[y][x] == '#' && (whole || x != 3 + y % 2))
                dot (cx - 3 + x, cy - 3 + y);
}

//==============================================================================
Display::Cell& Display::add (Cell::Kind kind, juce::Rectangle<int> area, KnobBinding binding, const juce::String& caption)
{
    cells.emplace_back();
    auto& c = cells.back();
    c.kind = kind;
    c.area = area;
    c.binding = std::move (binding);
    c.caption = caption;
    return c;
}

Display::Cell& Display::button (juce::Rectangle<int> r, const juce::String& caption, std::function<void()> press)
{
    auto& c = add (Cell::Press, r, {}, caption);
    c.press = std::move (press);
    c.boxed = true;
    return c;
}

void Display::rebuild()
{
    const auto oldFocus = focus;
    cells.clear();
    if (confirmGo != nullptr)
    {
        // A confirmation takes the page's place until it's answered.
        button ({ 96, 126, 44, 14 }, "NO", [this] { answer (false); });
        button ({ 180, 126, 44, 14 }, "YES", [this] { answer (true); });
    }
    else
    {
        const auto& t = tabs()[(size_t) juce::jlimit (0, getNumPages() - 1, pages[(size_t) group])];
        (this->*t.build) (t.arg);
    }
    focus = cells.empty() ? 0 : juce::jlimit (0, (int) cells.size() - 1, oldFocus);
    refresh();
    if (changed)
        changed();
}

void Display::pressGroup (Group g)
{
    if (group == g)
        showPage (g, (getPage() + 1) % getNumPages());
    else
        showPage (g, pages[(size_t) g]);
}

void Display::previousInGroup (Group g)
{
    showPage (g, (pages[(size_t) g] + (int) tabsOf (g).size() - 1) % (int) tabsOf (g).size());
}

void Display::showGroup (Group g) { showPage (g, pages[(size_t) g]); }

void Display::showPage (Group g, int page)
{
    group = g;
    pages[(size_t) g] = juce::jlimit (0, getNumPages() - 1, page);
    // Keep the shown tab among the four visible.
    auto& scroll = tabScroll[(size_t) g];
    scroll = juce::jlimit (0, juce::jmax (0, getNumPages() - 4), juce::jmin (scroll, pages[(size_t) g]));
    if (pages[(size_t) g] >= scroll + 4)
        scroll = pages[(size_t) g] - 3;
    if (trail.empty() || trail.back() != std::make_pair (g, pages[(size_t) g]))
    {
        trail.resize ((size_t) juce::jmax (0, trailAt + 1));
        trail.emplace_back (g, pages[(size_t) g]);
        if (trail.size() > 8)
            trail.erase (trail.begin());
        trailAt = (int) trail.size() - 1;
    }
    focus = 0;
    rebuild();
}

void Display::pressF (int key)
{
    const int p = tabScroll[(size_t) group] + key;
    if (juce::isPositiveAndBelow (p, getNumPages()))
        showPage (group, p);
}

void Display::scrollTabs (int delta)
{
    auto& scroll = tabScroll[(size_t) group];
    scroll = juce::jlimit (0, juce::jmax (0, getNumPages() - 4), scroll + delta);
    refresh();
}

void Display::history (int delta)
{
    if (trail.empty())
        return;
    trailAt = juce::jlimit (0, (int) trail.size() - 1, trailAt + delta);
    group = trail[(size_t) trailAt].first;
    pages[(size_t) group] = trail[(size_t) trailAt].second;
    focus = 0;
    rebuild();
}

void Display::selectionChanged()
{
    auto* s = selected();
    if (s != nullptr)
    {
        // The pages that are by layer follow the channel to its page (p.53).
        const auto& ts = tabs();
        const int shown = juce::jlimit (0, getNumPages() - 1, getPage());
        const auto build = ts[(size_t) shown].build;
        const bool layered = build == &Display::buildAtt || build == &Display::buildDelay || build == &Display::buildPan || build == &Display::buildRouting || build == &Display::buildAux || build == &Display::buildFaderView || build == &Display::buildMeter || build == &Display::buildPatch;
        if (layered)
        {
            const int layer = s->getKind() == StripKind::Channel ? (s->getNumber() - 1) / kChannelsPerPage : 4;
            for (int i = 0; i < (int) ts.size(); ++i)
                if (ts[(size_t) i].build == build && ts[(size_t) i].arg == layer && i != shown)
                {
                    pages[(size_t) group] = i;
                    tabScroll[(size_t) group] = juce::jlimit (0, juce::jmax (0, getNumPages() - 4), i - i % 4);
                    break;
                }
        }
    }
    rebuild();
    if (s != nullptr && s->getKind() == StripKind::Channel)
    {
        const int f = findField (juce::String (s->getNumber()));
        if (f >= 0)
            setFocus (f);
    }
}

KnobBinding Display::getSoftBinding (int knob) const
{
    int n = 0;
    for (const auto& c : cells)
        if ((c.kind == Cell::Dial || c.kind == Cell::Line) && c.binding.isValid() && n++ == knob)
            return c.binding;
    return {};
}

void Display::setFocus (int i)
{
    focus = cells.empty() ? 0 : juce::jlimit (0, (int) cells.size() - 1, i);
    refresh();
}

juce::String Display::getFocusText() const
{
    if (! juce::isPositiveAndBelow (focus, (int) cells.size()))
        return {};
    const auto& c = cells[(size_t) focus];
    return c.binding.isValid() ? lcdValue (c.binding) : c.caption;
}

juce::String Display::getFieldCaption (int i) const { return juce::isPositiveAndBelow (i, (int) cells.size()) ? cells[(size_t) i].caption : juce::String(); }

int Display::findField (const juce::String& caption) const
{
    for (int i = 0; i < (int) cells.size(); ++i)
        if (cells[(size_t) i].caption == caption)
            return i;
    return -1;
}

void Display::cursor (int dx, int dy)
{
    if (cells.empty())
        return;
    const auto from = cells[(size_t) focus].area.getCentre();
    int best = -1, bestScore = std::numeric_limits<int>::max();
    for (int i = 0; i < (int) cells.size(); ++i)
    {
        if (i == focus)
            continue;
        const auto to = cells[(size_t) i].area.getCentre();
        const int along = dx != 0 ? (to.x - from.x) * dx : (to.y - from.y) * dy;
        const int across = dx != 0 ? std::abs (to.y - from.y) : std::abs (to.x - from.x);
        if (along < 3)
            continue;
        const int score = along + across * 3;
        if (score < bestScore)
        {
            bestScore = score;
            best = i;
        }
    }
    if (best >= 0)
        focus = best;
    refresh();
}

void Display::turn (int delta)
{
    if (! juce::isPositiveAndBelow (focus, (int) cells.size()))
        return;
    const auto& c = cells[(size_t) focus];
    if (c.binding.isValid())
        stepBy (c.binding, delta);
    refresh();
}

void Display::enter()
{
    if (! juce::isPositiveAndBelow (focus, (int) cells.size()))
        return;
    const auto c = cells[(size_t) focus]; // a press may rebuild the page
    if (c.press)
        c.press();
    else if ((c.kind == Cell::Toggle || c.kind == Cell::Mark || c.kind == Cell::Check) && c.binding.isValid())
        c.binding.set (c.binding.get() > 0.5f ? 0.0f : 1.0f);
    refresh();
}

void Display::pick (juce::Point<int> p)
{
    int best = -1, bestDist = 12;
    for (int i = 0; i < (int) cells.size(); ++i)
    {
        const auto& r = cells[(size_t) i].area;
        const int dist = r.contains (p) ? 0 : (int) r.getCentre().getDistanceFrom (p);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = i;
        }
    }
    if (best >= 0)
    {
        const bool again = focus == best;
        focus = best;
        if (again || cells[(size_t) best].kind == Cell::Press)
            enter();
    }
    refresh();
}

void Display::mouseDown (const juce::MouseEvent& e)
{
    dragStart = (float) e.y;
    pick ({ e.x / kDot, e.y / kDot });
}

void Display::mouseDrag (const juce::MouseEvent& e)
{
    const int steps = juce::roundToInt ((dragStart - (float) e.y) / 6.0f);
    if (steps != 0)
    {
        turn (steps);
        dragStart = (float) e.y;
    }
}

void Display::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    juce::ignoreUnused (e);
    turn (w.deltaY > 0 ? 1 : -1);
}

//==============================================================================
void Display::drawCell (const Cell& c, bool focused)
{
    const auto r = c.area;
    switch (c.kind)
    {
        case Cell::Text:
        {
            const auto s = lcdValue (c.binding);
            if (c.boxed)
                box (r);
            else if (c.dotted)
                dottedBox (r);
            textCentred (r, s.substring (0, juce::jmax (1, (r.getWidth() + 1) / 6)));
            break;
        }
        case Cell::Toggle:
        {
            const bool on = c.binding.isValid() && c.binding.get() > 0.5f;
            const auto s = c.caption.isNotEmpty() ? c.caption : lcdValue (c.binding);
            if (on)
                inverseText (r, s, r.getHeight() < 11);
            else
            {
                box (r);
                textCentred (r, s, r.getHeight() < 11);
            }
            break;
        }
        case Cell::Dial:
        {
            if (c.binding.isValid())
                dial (r.getCentreX(), r.getY() + c.radius, c.radius, c.binding.get());
            else
                circle (r.getCentreX(), r.getY() + c.radius, c.radius);
            const auto s = c.binding.isValid() ? lcdValue (c.binding) : juce::String ("-");
            text (r.getCentreX() - juce::jmin (r.getWidth(), s.length() * 4) / 2, r.getY() + 2 * c.radius + 2, s.substring (0, r.getWidth() / 4), true);
            if (c.big)
                text (r.getCentreX() - textWidth (c.caption, true) / 2, r.getY() - 6, c.caption, true);
            break;
        }
        case Cell::Line:
        {
            const bool lit = c.big;
            dial (r.getX() + c.radius, r.getY() + c.radius, c.radius, c.binding.isValid() ? c.binding.get() : 0.0f, lit);
            const int x = r.getX() + c.radius * 2 + 3;
            text (x, r.getY() - 1, c.caption, true);
            const auto s = lcdValue (c.binding);
            text (r.getRight() - juce::jmin (r.getWidth() - c.radius * 2 - 3, textWidth (s)), r.getY() + 6, s);
            break;
        }
        case Cell::Press:
            if (c.boxed)
            {
                box (r);
                hLine (r.getX() + 1, r.getRight() - 1, r.getBottom());
                vLine (r.getRight(), r.getY() + 1, r.getBottom());
            }
            if (c.caption.containsChar (' ') && textWidth (c.caption) > r.getWidth() - 2)
            {
                textCentred (r.withTrimmedBottom (r.getHeight() / 2), c.caption.upToFirstOccurrenceOf (" ", false, false), true);
                textCentred (r.withTrimmedTop (r.getHeight() / 2), c.caption.fromFirstOccurrenceOf (" ", false, false), true);
            }
            else
                textCentred (r, c.caption);
            break;
        case Cell::Mark:
            if (c.binding.isValid() && c.binding.get() > 0.5f)
                disc (r.getCentreX(), r.getCentreY(), 1);
            else
                hLine (r.getCentreX() - 1, r.getCentreX() + 1, r.getCentreY());
            break;
        case Cell::Slide:
        {
            vLine (r.getCentreX(), r.getY(), r.getBottom() - 1);
            const float v = c.binding.isValid() ? c.binding.get() : 0.0f;
            const int y = r.getBottom() - 2 - juce::roundToInt (v * (float) (r.getHeight() - 5));
            fill ({ r.getCentreX() - 2, y - 2, 5, 4 });
            break;
        }
        case Cell::Check:
            box (r);
            if (c.binding.isValid() && c.binding.get() > 0.5f)
                for (int i = 2; i < r.getWidth() - 2; ++i)
                {
                    dot (r.getX() + i, r.getY() + i);
                    dot (r.getRight() - 1 - i, r.getY() + i);
                }
            break;
    }
    if (focused)
    {
        const auto cur = r.expanded (2);
        box (cur);
        box (cur.expanded (1));
    }
}

void Display::drawChrome()
{
    // The scene, as the title bar has it (p.52).
    fill ({ 0, kTitleH - 1, kDotsW, 1 });
    const int number = memory;
    text (4, 3, juce::String (number));
    text (17, 3, sceneTitle (number).substring (0, 16));
    if (ctx.show.getCurrentSceneIndex() >= 0 && number > 0)
        inverseText ({ 118, 1, 21, 9 }, "EDIT", true);
    // The group, after a diamond.
    static const char* const names[] = { "MATRIX", "AUX", "ENCODER", "EFFECT", "ROUTING", "PHASE", "DELAY", "DYNAMICS", "PAN", "EQ", "SCENE", "AUTOMIX", "ARMING", "USER DEF", "LOCATOR", "MONITOR", "DATA", "DIO", "SETUP", "UTILITY", "MIDI", "REMOTE", "METER", "VIEW", "PAIR", "GROUP", "IN PATCH", "OUT PATCH" };
    const juce::String groupName = group == Group::PhaseInsert ? "PHASE/INS" : names[(size_t) group];
    const int x = 142;
    for (int i = 0; i < 6; ++i)
    {
        dot (x + i, 6 - i);
        dot (x + i, 6 + i);
        dot (x + 10 - i, 6 - i);
        dot (x + 10 - i, 6 + i);
    }
    textTall (x + 14, 2, groupName.substring (0, 10));
    // The sampling rate and surround mode, then the channel: its ID and short name.
    const int rate = juce::roundToInt (ctx.engine.getSampleRate() / 1000.0);
    inverseText ({ 228, 1, 15, 11 }, "", false);
    text (229, 2, juce::String (rate == 0 ? 48 : rate), true, false);
    text (230, 7, "ST", true, false);
    auto* s = selected();
    textTall (245, 2, (idOf (s) + "-" + shortName (s)).substring (0, 10));
    drawTabs();
}

void Display::drawTabs()
{
    const auto& ts = tabs();
    const int shownPage = juce::jlimit (0, getNumPages() - 1, getPage()), scroll = tabScroll[(size_t) group];
    hLine (0, kDotsW - 1, kTabsY);
    const auto hatch = [this] (int x0, int x1)
    {
        for (int y = kTabsY + 1; y < kDotsH; ++y)
            for (int x = x0; x <= x1; ++x)
                if ((x + y) % 2 == 0)
                    dot (x, y);
    };
    hatch (0, 19);
    hatch (300, kDotsW - 1);
    if (scroll > 0)
        text (7, kTabsY + 3, "<", false, false);
    if (scroll + 4 < (int) ts.size())
        text (307, kTabsY + 3, ">", false, false);
    for (int i = 0; i < 4 && scroll + i < (int) ts.size(); ++i)
    {
        const int x = 20 + i * 70;
        const juce::Rectangle<int> r (x, kTabsY, 70, kDotsH - kTabsY);
        const bool on = scroll + i == shownPage;
        // The shown tab has no top line and flares at its edges.
        if (on)
        {
            hLine (x + 3, x + 66, kTabsY, false);
            for (int k = 0; k < 4; ++k)
            {
                dot (x + 3 - k, kTabsY + k);
                dot (x + 66 + k, kTabsY + k);
            }
        }
        else
        {
            hatch (x, x + 2);
            hatch (x + 67, x + 69);
        }
        textCentred (r.withTrimmedTop (1), juce::String (ts[(size_t) (scroll + i)].name).substring (0, 10));
    }
}

void Display::levelMeter (int x, int y, int h, float db, int width)
{
    const float norm = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
    const int lit = juce::roundToInt (norm * (float) h);
    for (int i = 0; i < lit; ++i)
        if (i % 2 == 0)
            hLine (x, x + width - 1, y + h - 1 - i);
}

void Display::meterScale (int x, int y, int h, bool labels)
{
    static const std::pair<const char*, float> marks[] = { { "0", 0.0f }, { "-6", -6.0f }, { "-12", -12.0f }, { "-18", -18.0f }, { "-30", -30.0f }, { "-48", -48.0f } };
    for (const auto& [label, db] : marks)
    {
        const int my = y + h - juce::roundToInt ((db + 48.0f) / 51.0f * (float) (h - 1)) - 1;
        dottedH (x, x + 24, my);
        if (labels)
            text (x + 26, my - 3, label, true);
    }
    if (labels)
        text (x + 26, y - 6, "OVER", true);
}

void Display::stripMeters (Strip& s, juce::Rectangle<int> r)
{
    meterScale (r.getX(), r.getY(), r.getHeight());
    for (int ch = 0; ch < 2; ++ch)
        levelMeter (r.getX() + 8 + ch * 7, r.getY() + 3, r.getHeight() - 3, toDb (ctx.meters.get (&s, MeterPoint::Output, ch)), 4);
}

void Display::eqCurve (Strip& s, juce::Rectangle<int> r)
{
    auto& eq = s.params.eq();
    const auto table = s.isOutput() ? models::BusEq : models::ChannelEq;
    const auto& m = models::model (table, eq.model.load());
    std::vector<float> values (m.params.size());
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = eq.values[i].load();
    const int n = r.getWidth() - 2, x0 = r.getX() + 1;
    const float decade = (float) n / std::log10 (30000.0f / 12.0f);
    std::vector<float> freqs ((size_t) n), db ((size_t) n);
    for (int i = 0; i < n; ++i)
        freqs[(size_t) i] = 12.0f * std::pow (10.0f, (float) i / decade);
    const int mid = r.getCentreY(), half = r.getHeight() / 2 - 2;
    box (r);
    for (float hz : { 100.0f, 1000.0f, 10000.0f })
        vLine (x0 + juce::roundToInt (std::log10 (hz / 12.0f) * decade), r.getY(), r.getBottom() - 1);
    hLine (r.getX(), r.getRight() - 1, mid);
    if (models::modelResponse (table, eq.model.load(), values.data(), (int) values.size(), freqs.data(), db.data(), n))
        for (int i = 0; i < n; ++i)
        {
            const int y = mid - juce::roundToInt (juce::jlimit (-18.0f, 18.0f, db[(size_t) i]) / 18.0f * (float) half);
            vLine (x0 + i, juce::jmin (y, mid), juce::jmax (y, mid));
        }
    // The band the FREQUENCY/Q keys and F, G last worked: a dotted line.
    const int f = m.find (juce::String (kBandIds[juce::jlimit (0, 3, band)]) + "f");
    if (f >= 0)
    {
        const auto label = m.params[(size_t) f].display (eq.values[f].load());
        const float hz = label.getFloatValue() * (label.containsChar ('k') ? 1000.0f : 1.0f);
        dottedV (x0 + juce::roundToInt (std::log10 (juce::jmax (12.0f, hz) / 12.0f) * decade), r.getY(), r.getBottom() - 1);
    }
}

void Display::dynCurve (const ld::DynSlotParams& d, juce::Rectangle<int> r, bool gateScale)
{
    juce::ignoreUnused (gateScale);
    const auto& m = models::model (models::Dynamics, d.model.load());
    std::vector<float> values (m.params.size());
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = d.values[i].load();
    box (r);
    const int n = r.getWidth() - 2;
    std::vector<float> in ((size_t) n), out ((size_t) n);
    for (int i = 0; i < n; ++i)
        in[(size_t) i] = -66.0f + 66.0f * (float) i / (float) (n - 1);
    if (models::dynStaticCurve (d.model.load(), values.data(), (int) values.size(), in.data(), out.data(), n))
    {
        int last = -1;
        for (int i = 0; i < n; ++i)
        {
            const int y = r.getBottom() - 2 - juce::roundToInt (juce::jlimit (0.0f, 1.0f, (out[(size_t) i] + 66.0f) / 66.0f) * (float) (r.getHeight() - 3));
            if (last >= 0)
                vLine (r.getX() + 1 + i, juce::jmin (y, last), juce::jmax (y, last));
            else
                dot (r.getX() + 1 + i, y);
            last = y;
        }
    }
}

juce::String Display::sceneTitle (int m) const
{
    if (m <= 0)
        return "Initial Data";
    if (const auto scene = ctx.show.getScene (m - 1))
        return scene->name;
    return "No Data!";
}

void Display::setMemory (int m)
{
    memory = juce::jlimit (0, 99, m);
    refresh();
}

void Display::storeScene()
{
    if (memory <= 0)
        return; // scene #0 is the read-only initial data
    const int index = memory - 1;
    auto& show = ctx.show;
    const auto done = [safe = juce::Component::SafePointer<Display> (this), index] (const juce::String& title)
    {
        if (safe == nullptr)
            return;
        auto& sh = safe->ctx.show;
        if (index < sh.getNumScenes())
            sh.storeScene (index);
        else
            sh.addScene (title);
        if (index < sh.getNumScenes())
            sh.setSceneName (index, title);
        safe->refresh();
    };
    const auto current = index < show.getNumScenes() ? sceneTitle (memory) : "SCENE " + juce::String (memory).paddedLeft ('0', 2);
    // Store Confirmation (p.275): off, the Title Edit window doesn't come up.
    if (getParentComponent() != nullptr && preference (Pref::StoreConfirmation))
        askForText (*getParentComponent(), "TITLE EDIT", "Scene " + juce::String (memory).paddedLeft ('0', 2) + ":", current, [done] (const juce::String& t) { done (t.substring (0, 16)); });
    else
        done (current);
}

void Display::recallScene()
{
    if (memory < 1 || memory - 1 >= ctx.show.getNumScenes())
        return;
    // Recall Confirmation (p.275).
    if (preference (Pref::RecallConfirmation))
        confirm ("Recall scene " + juce::String (memory).paddedLeft ('0', 2) + "?", [this] { doRecallScene(); });
    else
        doRecallScene();
}

void Display::doRecallScene()
{
    if (memory >= 1 && memory - 1 < ctx.show.getNumScenes())
    {
        ctx.show.recallScene (memory - 1);
        if (amSceneClock && amRun)
        {
            amStart = juce::Time::getMillisecondCounterHiRes() * 0.001;
            amNext = 0;
        }
        refresh();
    }
}

void Display::refresh()
{
    clear();
    drawChrome();
    if (confirmGo != nullptr)
    {
        // The confirmation's window, its NO and YES the only fields.
        drawWindow ({ 40, 84, 240, 66 });
        textCentred ({ 44, 96, 232, 10 }, confirmText.substring (0, 38));
    }
    else
    {
        const auto& t = tabs()[(size_t) juce::jlimit (0, getNumPages() - 1, getPage())];
        (this->*t.draw) (t.arg);
    }
    for (int i = 0; i < (int) cells.size(); ++i)
        drawCell (cells[(size_t) i], i == focus);
    if (message.isNotEmpty() && confirmGo == nullptr)
    {
        if ((juce::int32) (messageUntil - juce::Time::getMillisecondCounter()) <= 0)
            message.clear();
        else
        {
            drawWindow ({ 20, 98, 280, 28 });
            textCentred ({ 20, 98, 280, 28 }, message.substring (0, 44));
        }
    }
    if (dots != shownDots)
    {
        shownDots = dots;
        renderImage();
        repaint();
    }
}

void Display::renderImage()
{
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < kDotsH; ++y)
    {
        const auto field = kField.interpolatedWith (kFieldLow, (float) y / (float) kDotsH);
        const auto ink = y < kTitleH ? kInkTitle : kInk;
        for (int x = 0; x < kDotsW; ++x)
        {
            const auto colour = dots[(size_t) (y * kDotsW + x)] != 0 ? ink : field;
            for (int py = 0; py < kDot; ++py)
                for (int px = 0; px < kDot; ++px)
                    data.setPixelColour (x * kDot + px, y * kDot + py, colour);
        }
    }
}

void Display::paint (juce::Graphics& g)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImageAt (image, 0, 0);
}

//==============================================================================
// The pages.

void Display::buildNone (int) {}

// BULK: its bodies aren't published, so nothing can be built.
void Display::drawNone (int)
{
    const juce::Rectangle<int> r (16, 70, 288, 80);
    box (r);
    textCentred (r.withTrimmedBottom (24), getTabName (getPage()));
    textCentred (r.withTrimmedTop (24).withTrimmedBottom (10), "Bulk dumps aren't in Sidecar.");
    textCentred (r.withTrimmedTop (44), "What they hold isn't published.", true);
}

//==============================================================================
// AUTOMIX: fader and ON moves of the channels, recorded against a clock that runs from RUN (or
// restarts with every scene recall) and played back with it.

namespace
{
    KnobBinding flagBinding (std::function<bool()> get, std::function<void()> flip)
    {
        KnobBinding b;
        b.get = [get] { return get() ? 1.0f : 0.0f; };
        b.set = [get, flip] (float v)
        {
            if ((v >= 0.5f) != get())
                flip();
        };
        b.steps = 2.0f;
        return b;
    }

    juce::String clockText (double seconds)
    {
        const int ms = (int) (seconds * 1000.0 + 0.5);
        return juce::String (ms / 3600000) + ":" + juce::String (ms / 60000 % 60).paddedLeft ('0', 2) + ":" + juce::String (ms / 1000 % 60).paddedLeft ('0', 2) + "." + juce::String (ms % 1000).paddedLeft ('0', 3);
    }
}

bool Display::automixIs (AutomixKey k) const
{
    switch (k)
    {
        case AutomixKey::Enable: return amEnable;
        case AutomixKey::Rec: return amRec;
        case AutomixKey::Run: return amRun;
        case AutomixKey::Clock: return amSceneClock;
        case AutomixKey::Fader: return amFaders;
        case AutomixKey::On: return amOns;
        default: return false;
    }
}

void Display::automixPress (AutomixKey k)
{
    const auto& channels = ctx.engine.getChannels();
    switch (k)
    {
        case AutomixKey::Enable: amEnable = ! amEnable; break;
        case AutomixKey::Fader: amFaders = ! amFaders; break;
        case AutomixKey::On: amOns = ! amOns; break;
        case AutomixKey::Clock: amSceneClock = ! amSceneClock; break;
        case AutomixKey::Clear: amEvents.clear(); amNext = 0; break;
        case AutomixKey::Undo:
            amEvents = amBackup;
            amNext = amEvents.size();
            break;
        case AutomixKey::Rec:
            amRec = ! amRec;
            if (amRec)
            {
                // The take replaces what was there from here on, in the kinds being recorded; UNDO restores it.
                amBackup = amEvents;
                amEvents.erase (std::remove_if (amEvents.begin(), amEvents.end(), [this] (const AutomixEvent& e) { return e.t >= amNow && (e.on ? amOns : amFaders); }), amEvents.end());
                if (! amRun)
                    automixPress (AutomixKey::Run);
            }
            break;
        case AutomixKey::Run:
            amRun = ! amRun;
            if (amRun)
            {
                amStart = juce::Time::getMillisecondCounterHiRes() * 0.001;
                amNow = 0.0;
                amNext = 0;
                amFader.assign (channels.size(), 0.0f);
                amOn.assign (channels.size(), 0);
                for (size_t i = 0; i < channels.size(); ++i)
                {
                    amFader[i] = channels[i]->params.faderDb.load();
                    amOn[i] = ! channels[i]->params.mute.load();
                }
            }
            else
                amRec = false;
            break;
    }
    refresh();
}

void Display::automixTick()
{
    if (! amRun)
        return;
    const auto& channels = ctx.engine.getChannels();
    amNow = juce::Time::getMillisecondCounterHiRes() * 0.001 - amStart;
    if (amFader.size() != channels.size())
        return;
    if (amRec)
    {
        for (size_t i = 0; i < channels.size(); ++i)
        {
            auto& p = channels[i]->params;
            const float f = p.faderDb.load();
            const char on = ! p.mute.load();
            if (amFaders && std::abs (f - amFader[i]) > 0.05f)
                amEvents.push_back ({ amNow, (int) i, false, f });
            if (amOns && on != amOn[i])
                amEvents.push_back ({ amNow, (int) i, true, on ? 1.0f : 0.0f });
            amFader[i] = f;
            amOn[i] = on;
        }
        amNext = amEvents.size();
    }
    else if (amEnable)
    {
        for (; amNext < amEvents.size() && amEvents[amNext].t <= amNow; ++amNext)
        {
            const auto& e = amEvents[amNext];
            if (! juce::isPositiveAndBelow (e.strip, (int) channels.size()))
                continue;
            auto& p = channels[(size_t) e.strip]->params;
            if (e.on)
            {
                p.mute = e.value < 0.5f;
                amOn[(size_t) e.strip] = e.value >= 0.5f;
            }
            else
            {
                p.faderDb = e.value;
                amFader[(size_t) e.strip] = e.value;
            }
        }
    }
}

void Display::buildAutomix (int)
{
    const auto flag = [this] (AutomixKey k, juce::Rectangle<int> r, const juce::String& caption)
    {
        add (Cell::Toggle, r, flagBinding ([this, k] { return automixIs (k); }, [this, k] { automixPress (k); }), caption);
    };
    flag (AutomixKey::Enable, { 8, 44, 56, 14 }, "ENABLE");
    flag (AutomixKey::Rec, { 70, 44, 56, 14 }, "REC");
    flag (AutomixKey::Run, { 132, 44, 56, 14 }, "RUN");
    flag (AutomixKey::Clock, { 194, 44, 118, 14 }, "SCENE CLOCK");
    flag (AutomixKey::Fader, { 8, 92, 56, 14 }, "FADER");
    flag (AutomixKey::On, { 70, 92, 56, 14 }, "ON");
    button ({ 8, 130, 56, 14 }, "UNDO", [this] { automixPress (AutomixKey::Undo); });
    button ({ 70, 130, 56, 14 }, "CLEAR", [this] { automixPress (AutomixKey::Clear); });
}

void Display::drawAutomix (int)
{
    pageTitle ("AUTOMIX", amRec ? "RECORDING" : amRun ? "RUNNING" : "STOPPED");
    text (8, 68, "TIME " + clockText (amNow));
    text (8, 80, "EVENTS " + juce::String ((int) amEvents.size()) + "  CLOCK " + juce::String (amSceneClock ? "SCENE" : "INTERNAL"), true);
    frame ({ 4, 84, 130, 26 }, "OVERWRITE");
    text (8, 154, "Channel faders and ON keys; a take replaces what", true);
    text (8, 164, "follows it. UNDO restores the last take.", true);
}

//==============================================================================
// TRACK ARMING and LOCATOR: the multitrack recorder's tracks and markers.

bool Display::isTrackArmed (int track) const
{
    if (ctx.recording == nullptr)
        return false;
    const auto d = ctx.recording->getProject().getTracks();
    return juce::isPositiveAndBelow (track, (int) d.size()) && d[(size_t) track].arm;
}

void Display::toggleTrackArm (int track)
{
    if (ctx.recording == nullptr)
        return;
    const auto d = ctx.recording->getProject().getTracks();
    if (juce::isPositiveAndBelow (track, (int) d.size()))
        ctx.recording->getProject().setTrackProperty (d[(size_t) track].id, daw::ids::arm, ! d[(size_t) track].arm, false);
    else
        showMessage ("No track " + juce::String (track + 1) + " in the recorder.");
    refresh();
}

void Display::buildArming (int arg)
{
    armPage = arg;
    for (int i = 0; i < 24; ++i)
    {
        const int t = arg * 24 + i;
        add (Cell::Check, { 8 + (i / 8) * 104, 41 + (i % 8) * 20, 10, 10 }, flagBinding ([this, t] { return isTrackArmed (t); }, [this, t] { toggleTrackArm (t); }));
    }
}

void Display::drawArming (int arg)
{
    pageTitle ("TRACK ARMING", "TRACKS " + juce::String (arg * 24 + 1) + "-" + juce::String (arg * 24 + 24));
    if (ctx.recording == nullptr)
        return;
    const auto d = ctx.recording->getProject().getTracks();
    for (int i = 0; i < 24; ++i)
    {
        const int t = arg * 24 + i;
        const auto name = t < (int) d.size() && d[(size_t) t].name.isNotEmpty() ? d[(size_t) t].name : "TRACK";
        text (22 + (i / 8) * 104, 41 + (i % 8) * 20, juce::String (t + 1).paddedLeft ('0', 2) + " " + name.substring (0, 10).toUpperCase(), t >= (int) d.size());
    }
}

void Display::locateMemory (int n)
{
    if (ctx.recording == nullptr)
        return;
    const auto m = ctx.recording->getProject().getMarkers();
    if (n < (int) m.size())
        ctx.recording->locate (m[(size_t) n].position);
    else
    {
        // An empty memory stores the current position.
        ctx.recording->addMarkerAtPosition();
        showMessage ("Stored in LOCATE MEMORY.");
    }
    refresh();
}

void Display::transport (int command)
{
    if (ctx.recording == nullptr)
        return;
    auto& r = *ctx.recording;
    switch (command)
    {
        case 0x01: r.stop(); break;
        case 0x02: if (! r.isRolling()) r.play(); break;
        case 0x04: r.jumpToMarker (true); break;
        case 0x05: r.jumpToMarker (false); break;
        case 0x06:
            if (r.isRecording())
                r.stop();
            else if (auto res = r.record(); res.failed())
                showMessage (res.getErrorMessage().substring (0, 38));
            break;
        default: break;
    }
    refresh();
}

void Display::buildLocator (int)
{
    auto* rec = ctx.recording;
    if (rec == nullptr)
        return;
    for (int i = 0; i < 8; ++i)
        button ({ 8 + (i / 4) * 150, 66 + (i % 4) * 16, 40, 13 }, "LOC " + juce::String (i + 1), [this, i] { locateMemory (i); });
    button ({ 8, 150, 40, 14 }, "ZERO", [rec] { rec->locate (0); });
    button ({ 54, 150, 40, 14 }, "END", [rec] { rec->locate (rec->getProject().getLength()); });
    button ({ 100, 150, 40, 14 }, "STOP", [this] { transport (0x01); });
    button ({ 146, 150, 40, 14 }, "PLAY", [this] { transport (0x02); });
    button ({ 192, 150, 40, 14 }, "REC", [this] { transport (0x06); });
    add (Cell::Toggle, { 238, 150, 40, 14 }, flagBinding ([rec] { return rec->isLoopEnabled(); }, [rec] { rec->setLoopEnabled (! rec->isLoopEnabled()); }), "LOOP");
}

void Display::drawLocator (int)
{
    pageTitle ("LOCATOR");
    if (ctx.recording == nullptr)
    {
        textCentred ({ 16, 90, 288, 12 }, "No multitrack recorder.");
        return;
    }
    auto& r = *ctx.recording;
    const double rate = juce::jmax (1.0, r.getRecorder().getSampleRate());
    text (8, 38, "TIME " + clockText ((double) r.getPosition() / rate));
    const char* state = r.isRecording() ? "REC" : r.isRolling() ? "PLAY" : "STOP";
    text (200, 38, state);
    text (8, 52, "LOCATE MEMORY (markers; an empty one stores here)", true);
    const auto m = r.getProject().getMarkers();
    for (int i = 0; i < 8; ++i)
    {
        const int x = 52 + (i / 4) * 150, y = 68 + (i % 4) * 16;
        text (x, y, i < (int) m.size() ? clockText ((double) m[(size_t) i].position / rate).dropLastCharacters (4) : "--");
    }
}

//==============================================================================
// DATA: the show (Sidecar keeps the scenes, libraries and settings in one file).

void Display::buildData (int)
{
    const auto files = [this]
    {
        const auto current = ctx.getShowFile ? ctx.getShowFile() : juce::File();
        const auto folder = current != juce::File() ? current.getParentDirectory() : paths::documents().getChildFile ("Shows");
        auto f = folder.findChildFiles (juce::File::findFiles, false, "*.lshow");
        f.sort();
        return f;
    };
    auto list = bindInt ("SHOW", [this] { return dataRow; }, [this] (int v) { dataRow = v; }, 0, 63, [] (int) { return juce::String(); });
    add (Cell::Text, { 8, 125, 150, 9 }, std::move (list), "FILE").dotted = true;
    button ({ 168, 44, 56, 14 }, "LOAD", [this, files]
    {
        const auto f = files();
        if (ctx.openShowFile && juce::isPositiveAndBelow (dataRow, f.size()))
            confirm ("Load " + f[dataRow].getFileNameWithoutExtension().substring (0, 20) + "?", [this, file = f[dataRow]] { ctx.openShowFile (file); });
        else if (ctx.openShow)
            ctx.openShow();
    });
    button ({ 168, 64, 56, 14 }, "SAVE", [this]
    {
        if (ctx.saveShow)
            ctx.saveShow (false);
    });
    button ({ 168, 84, 56, 14 }, "SAVE AS", [this]
    {
        if (ctx.saveShow)
            ctx.saveShow (true);
    });
    button ({ 168, 104, 56, 14 }, "NEW", [this] { confirm ("Start a new show?", [this] { if (ctx.newShow) ctx.newShow(); }); });
}

void Display::drawData (int)
{
    const auto current = ctx.getShowFile ? ctx.getShowFile() : juce::File();
    pageTitle ("DATA", current != juce::File() ? current.getFileNameWithoutExtension() : "UNSAVED SHOW");
    const auto folder = current != juce::File() ? current.getParentDirectory() : paths::documents().getChildFile ("Shows");
    auto f = folder.findChildFiles (juce::File::findFiles, false, "*.lshow");
    f.sort();
    text (8, 32, "SHOWS IN " + folder.getFileName().toUpperCase().substring (0, 20), true);
    box ({ 6, 40, 156, 80 });
    for (int row = 0; row < 7; ++row)
    {
        const int n = dataRow - 3 + row, y = 43 + row * 11;
        if (n < 0 || n >= f.size())
            continue;
        text (12, y, f[n].getFileNameWithoutExtension().toUpperCase().substring (0, 22));
        if (n == dataRow)
            for (int i = 0; i < 4; ++i)
                vLine (6 + i, y + 3 - i, y + 3 + i);
    }
    text (8, 150, "The show holds scenes, libraries and settings.", true);
}

//==============================================================================
// The Output Channels' Insert In patch (p.79): the FX unit inserted in a bus.

void Display::buildOutInsert (int arg)
{
    const auto& buses = ctx.engine.getBuses();
    for (int i = 0; i < kChannelsPerPage; ++i)
    {
        const int n = arg * kChannelsPerPage + i;
        if (n >= (int) buses.size())
            break;
        auto* s = buses[(size_t) n].get();
        auto b = bindInt (
            "INSERT", [s] { return s->params.preInsert().on.load() ? s->params.preInsert().fx.load() : -1; },
            [s] (int v) { s->params.preInsert().fx = v; s->params.preInsert().on = v >= 0; }, -1, 7,
            [] (int v) { return v < 0 ? juce::String ("NONE") : "FX" + juce::String (v + 1); });
        add (Cell::Text, { 8 + (i % 6) * 52, 44 + (i / 6) * 42, 46, 12 }, std::move (b), juce::String (s->getNumber())).dotted = true;
    }
}

void Display::drawOutInsert (int arg)
{
    pageTitle ("INSERT IN BUS " + juce::String (arg * kChannelsPerPage + 1) + "-" + juce::String (arg * kChannelsPerPage + kChannelsPerPage));
    for (const auto& c : cells)
        textCentred ({ c.area.getX(), c.area.getY() - 9, c.area.getWidth(), 8 }, c.caption, true);
}

// EQ EDIT (p.134, and a photograph of the page).
void Display::buildEq (int)
{
    auto* s = selected();
    if (s == nullptr)
        return;
    auto& eq = s->params.eq();
    add (Cell::Toggle, { 19, 72, 26, 14 }, bindBool ("EQ ON", eq.on));
    const auto typeButton = [this, s] (int type, int y)
    {
        auto typeParam = bindEq (*s, "type", {});
        KnobBinding b;
        const auto get = typeParam.get;
        b.get = [get, type] { return juce::roundToInt (get()) == type ? 1.0f : 0.0f; };
        b.set = [set = typeParam.set, type] (float) { set ((float) type); };
        b.steps = 2.0f;
        add (Cell::Toggle, { 55, y, 30, 10 }, std::move (b), type == 0 ? "TYPE I" : "TYPE II");
    };
    typeButton (0, 63);
    typeButton (1, 80);
    auto& att = add (Cell::Dial, { 97, 60, 26, 32 }, bindAttenuator (*s), "ATT");
    att.radius = 8;

    for (int b = 0; b < 4; ++b)
    {
        const int x = 12 + b * 74;
        const juce::String pre (kBandIds[b]);
        const char* rows[] = { "q", "f", "g" };
        const char* caps[] = { "[Q]", "[F]", "[G]" };
        for (int row = 0; row < 3; ++row)
        {
            auto& c = add (Cell::Line, { x, 143 + row * 25, 62, 17 }, bindEq (*s, pre + rows[row], {}), caps[row]);
            c.radius = 8;
            c.big = (row == 1 && freqNotQ[(size_t) b]) || (row == 0 && ! freqNotQ[(size_t) b]);
        }
    }
    focus = juce::jlimit (0, juce::jmax (0, (int) cells.size() - 1), focus);
}

void Display::drawEq (int)
{
    auto* s = selected();
    if (s == nullptr)
        return;
    pageTitle (idOf (s) + " EQUALIZER EDIT", s->getName());
    frame ({ 12, 44, 38, 87 }, "EQ ON");
    frame ({ 52, 44, 37, 87 }, "TYPE");
    frame ({ 92, 44, 36, 87 }, "ATT");
    textCentred ({ 132, 37, 127, 8 }, "CURVE", true);
    eqCurve (*s, { 132, 46, 127, 85 });
    text (166, 92, "100", true);
    text (200, 92, "1.0K", true);
    text (238, 92, "10K", true);
    stripMeters (*s, { 264, 54, 52, 70 });
    text (272, 124, juce::String (s->getNumber()), true);

    for (int b = 0; b < 4; ++b)
        frame ({ 8 + b * 74, 137, 70, 87 }, kBandNames[b]);
}

// The attenuators: 24 to a page, three rows of eight (p.130).
void Display::buildAtt (int arg)
{
    const auto& lists = ctx.engine.getBuses();
    if (arg < 4)
    {
        for (int i = 0; i < kChannelsPerPage; ++i)
            if (auto* s = layerStrip (arg, i))
            {
                auto& c = add (Cell::Dial, { 8 + (i % 8) * 38, 48 + (i / 8) * 58, 34, 34 }, bindAttenuator (*s), juce::String (s->getNumber()));
                c.radius = 9;
            }
        return;
    }
    juce::ignoreUnused (lists);
    std::vector<Strip*> outs;
    for (const auto* l : { &ctx.engine.getBuses(), &ctx.engine.getMatrices(), &ctx.engine.getMains() })
        for (const auto& s : *l)
            outs.push_back (s.get());
    int i = 0;
    for (auto* s : outs)
    {
        auto& c = add (Cell::Dial, { 8 + (i % 8) * 38, 48 + (i / 8) * 58, 34, 34 }, bindAttenuator (*s), idOf (s));
        c.radius = 9;
        ++i;
    }
}

void Display::drawAtt (int arg)
{
    pageTitle ((arg < 4 ? "CH" + rangeName (arg) : juce::String ("OUTPUT")) + " ATTENUATOR", selected() != nullptr ? selected()->getName() : juce::String());
    text (108, 34, "ATT (-96.0dB to +12.0dB)", true);
    for (const auto& c : cells)
        textCentred ({ c.area.getX(), c.area.getY() - 9, c.area.getWidth(), 8 }, c.caption, true);
}

void Display::buildGate (int)
{
    auto* s = selected();
    if (s == nullptr || ! s->isInput())
        return;
    auto& gate = s->params.gate();
    add (Cell::Toggle, { 24, 110, 34, 12 }, bindBool ("GATE", gate.on));

    const int gat = models::findModel (models::Dynamics, "DMGAT"), duk = models::findModel (models::Dynamics, "DMDUK");
    auto type = bindInt (
        "TYPE", [&gate, duk] { return gate.model.load() == duk ? 1 : 0; },
        [this, s, gat, duk] (int v)
        {
            s->setModel (ModelBlock::Gate, v == 1 ? duk : gat);
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Display> (this)] { if (safe != nullptr) safe->rebuild(); });
        },
        0, 1, [] (int v) { return juce::String (v == 1 ? "DUCKING" : "GATE"); });
    auto& t = add (Cell::Text, { 226, 40, 64, 13 }, std::move (type), "TYPE");
    t.boxed = true;

    const std::pair<const char*, const char*> params[] = { { "thr", "THRESHOLD" }, { "range", "RANGE" }, { "att", "ATTACK" }, { "dcy", "DECAY" }, { "hld", "HOLD" } };
    int n = 0;
    for (auto [id, name] : params)
    {
        auto& c = add (Cell::Line, { 86 + (n % 3) * 76, 150 + (n / 3) * 34, 72, 17 }, bindId (models::Dynamics, gate.model.load(), gate.values, id, {}), name);
        c.radius = 7;
        ++n;
    }
}

void Display::drawGate (int)
{
    auto* s = selected();
    if (s == nullptr)
        return;
    pageTitle (idOf (s) + " GATE EDIT", s->getName());
    if (! s->isInput())
    {
        textCentred ({ 0, 100, kDotsW, 12 }, "The Output Channels have no gate.");
        return;
    }
    frame ({ 12, 34, 62, 44 }, "KEYIN SOURCE");
    textCentred ({ 12, 44, 62, 12 }, "SELF", false);
    frame ({ 12, 92, 62, 38 }, "ON/OFF");
    dynCurve (s->params.gate(), { 128, 40, 90, 90 }, true);
    textCentred ({ 128, 31, 90, 8 }, "CURVE", true);
    text (226, 31, "TYPE", true);
    meterScale (250, 70, 62);
    frame ({ 76, 143, 236, 78 }, "PARAMETER");
}

void Display::buildComp (int)
{
    auto* s = selected();
    if (s == nullptr)
        return;
    auto& comp = s->params.dynamics();
    add (Cell::Toggle, { 24, 110, 34, 12 }, bindBool ("COMP", comp.on));

    static const char* const tokens[] = { "DMCMP", "DMEXP", "DMCPH", "DMCPS" };
    static const char* const names[] = { "COMP", "EXPAND", "COMPAND-H", "COMPAND-S" };
    auto type = bindInt (
        "TYPE", [&comp] { const auto token = models::model (models::Dynamics, comp.model.load()).token; for (int i = 0; i < 4; ++i) if (token == tokens[i]) return i; return 0; },
        [this, s] (int v)
        {
            s->setModel (ModelBlock::Dynamics, models::findModel (models::Dynamics, tokens[juce::jlimit (0, 3, v)]));
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Display> (this)] { if (safe != nullptr) safe->rebuild(); });
        },
        0, 3, [] (int v) { return juce::String (names[juce::jlimit (0, 3, v)]); });
    auto& t = add (Cell::Text, { 226, 40, 64, 13 }, std::move (type), "TYPE");
    t.boxed = true;

    const auto& m = models::model (models::Dynamics, comp.model.load());
    const std::pair<const char*, const char*> params[] = { { "thr", "THRESHOLD" }, { "ratio", "RATIO" }, { "att", "ATTACK" }, { "rel", "RELEASE" }, { "gain", "OUT GAIN" }, { "knee", "KNEE" }, { "width", "WIDTH" } };
    int n = 0;
    for (auto [id, name] : params)
    {
        if (m.find (id) < 0)
            continue;
        auto& c = add (Cell::Line, { 86 + (n % 3) * 76, 150 + (n / 3) * 34, 72, 17 }, bindId (models::Dynamics, comp.model.load(), comp.values, id, {}), name);
        c.radius = 7;
        ++n;
    }
}

void Display::drawComp (int)
{
    auto* s = selected();
    if (s == nullptr)
        return;
    pageTitle (idOf (s) + " COMP EDIT", s->getName());
    frame ({ 12, 34, 62, 44 }, "POSITION");
    int comp = 0, insert = 0;
    bool insFirst = false;
    decodeOrder (s->params.processOrder.load(), comp, insert, insFirst);
    static const char* const places[] = { "PRE EQ", "PRE FADER", "POST FADER" };
    textCentred ({ 12, 44, 62, 12 }, juce::String (places[comp]).substring (0, 10), true);
    frame ({ 12, 92, 62, 38 }, "ON/OFF");
    dynCurve (s->params.dynamics(), { 128, 40, 90, 90 }, false);
    textCentred ({ 128, 31, 90, 8 }, "CURVE", true);
    text (226, 31, "TYPE", true);
    meterScale (250, 70, 62);
    frame ({ 76, 143, 236, 78 }, "PARAMETER");
}

void Display::buildPhase (int arg)
{
    for (int layer = arg; layer < arg + 2; ++layer)
        for (int i = 0; i < kChannelsPerPage; ++i)
            if (auto* s = layerStrip (layer, i))
                add (Cell::Toggle, { 8 + (i % 12) * 26, 42 + (layer - arg) * 92 + (i / 12) * 44, 24, 14 }, bindBool ("PHASE", s->params.input.invert), juce::String (s->getNumber()));
}

void Display::drawPhase (int arg)
{
    pageTitle ("CH" + juce::String (firstOf (arg) + 1) + "-" + juce::String (firstOf (arg + 2)) + " PHASE", selected() != nullptr ? selected()->getName() : juce::String());
    text (116, 30, "Lit: reversed", true);
}

// The Insert page: POSITION, the block diagram of p.136.
void Display::buildInsert (int)
{
    auto* s = selected();
    if (s == nullptr)
        return;
    auto& p = s->params;
    const auto place = [&p] (bool comp, int spot)
    {
        KnobBinding b;
        b.get = [&p, comp, spot]
        {
            int c = 0, i = 0;
            bool f = false;
            decodeOrder (p.processOrder.load(), c, i, f);
            return (comp ? c : i) == spot ? 1.0f : 0.0f;
        };
        b.set = [&p, comp, spot] (float)
        {
            int c = 0, i = 0;
            bool f = false;
            decodeOrder (p.processOrder.load(), c, i, f);
            p.processOrder = processOrder (comp ? spot : c, comp ? i : spot, f);
        };
        b.steps = 2.0f;
        return b;
    };
    static const int xs[] = { 20, 112, 216 };
    for (int spot = 0; spot < 3; ++spot)
    {
        add (Cell::Toggle, { xs[spot], 86, 44, 12 }, place (true, spot), "COMP");
        add (Cell::Toggle, { xs[spot], 101, 44, 12 }, place (false, spot), "INSERT");
    }
    add (Cell::Toggle, { 66, 146, 28, 12 }, bindBool ("INSERT", p.preInsert().on));
    auto& cin = add (Cell::Text, { 40, 162, 70, 11 }, bindInt (
        "INSERT", [&p] { return p.preInsert().fx.load(); }, [&p] (int v) { p.preInsert().fx = v; },
        -1, 7, [] (int v) { return v < 0 ? juce::String ("NONE") : "FX" + juce::String (v + 1); }), "OUT");
    cin.dotted = true;
    add (Cell::Toggle, { 166, 146, 28, 12 }, bindBool ("COMP", p.dynamics().on));

    KnobBinding order;
    order.get = [&p]
    {
        int c = 0, i = 0;
        bool f = false;
        decodeOrder (p.processOrder.load(), c, i, f);
        return f ? 1.0f : 0.0f;
    };
    order.set = [&p] (float v)
    {
        int c = 0, i = 0;
        bool f = false;
        decodeOrder (p.processOrder.load(), c, i, f);
        p.processOrder = processOrder (c, i, v >= 0.5f);
    };
    order.steps = 2.0f;
    order.text = [&p]
    {
        int c = 0, i = 0;
        bool f = false;
        decodeOrder (p.processOrder.load(), c, i, f);
        return juce::String (f ? "INS->COMP" : "COMP->INS");
    };
    auto& oc = add (Cell::Text, { 146, 163, 72, 11 }, std::move (order), "ORDER");
    oc.dotted = true;
}

void Display::drawInsert (int)
{
    auto* s = selected();
    if (s == nullptr)
        return;
    pageTitle (idOf (s) + " INSERT", s->getName());
    frame ({ 6, 36, 308, 90 }, "POSITION");
    // The signal path: PRE EQ, EQ, PRE FADER, ON, FADER, POST FADER, OUT.
    const int y = 62;
    hLine (10, 304, y);
    box ({ 72, y - 6, 18, 13 });
    fill ({ 73, y - 5, 16, 11 }, false);
    textCentred ({ 72, y - 6, 18, 13 }, "EQ", true);
    box ({ 190, y - 6, 30, 13 });
    fill ({ 191, y - 5, 28, 11 }, false);
    textCentred ({ 190, y - 6, 30, 13 }, "FADER", true);
    text (130, y - 8, "ON", true);
    for (int x : { 20, 112, 216 })
    {
        textCentred ({ x - 8, 46, 62, 8 }, x == 20 ? "PRE EQ" : x == 112 ? "PRE FADER" : "POST FADER", true);
        disc (x + 22, y, 2);
        vLine (x + 22, y, 82);
    }
    frame ({ 8, 137, 124, 44 }, "INSERT");
    text (14, 149, "ON/OFF", true);
    text (14, 165, "OUT", true);
    frame ({ 138, 137, 90, 44 }, "COMP");
    text (144, 149, "ON/OFF", true);
    frame ({ 234, 137, 76, 44 }, "GEQ");
    textCentred ({ 234, 148, 76, 10 }, "-");
    int c = 0, i = 0;
    bool f = false;
    decodeOrder (s->params.processOrder.load(), c, i, f);
    text (8, 192, "Process order " + juce::String (s->params.processOrder.load() + 1) + " of 12", true);
    (void) c;
    (void) i;
}

void Display::buildDelay (int arg)
{
    std::vector<Strip*> strips;
    if (arg < 4)
    {
        for (int i = 0; i < kChannelsPerPage; ++i)
            strips.push_back (layerStrip (arg, i));
    }
    else
    {
        for (const auto* l : { &ctx.engine.getBuses(), &ctx.engine.getMatrices(), &ctx.engine.getMains() })
            for (const auto& s : *l)
                strips.push_back (s.get());
    }
    int i = 0;
    for (auto* s : strips)
    {
        if (s == nullptr)
            continue;
        const int col = i % 3, row = i / 3;
        add (Cell::Toggle, { 42 + col * 96, 36 + row * 22, 16, 9 }, bindBool ("ON", s->params.input.delayOn), "ON");
        auto& c = add (Cell::Text, { 62 + col * 96, 35 + row * 22, 40, 11 }, bindDelay (*s), s->isInput() ? juce::String (s->getNumber()) : idOf (s));
        c.dotted = true;
        ++i;
    }
}

void Display::drawDelay (int arg)
{
    pageTitle ((arg < 4 ? "CH" + rangeName (arg) : juce::String ("OUTPUT")) + " DELAY", selected() != nullptr ? selected()->getName() : juce::String());
    int i = 0;
    for (size_t c = 1; c < cells.size(); c += 2)
    {
        const int col = i % 3, row = i / 3;
        text (6 + col * 96, 37 + row * 22, cells[c].caption);
        ++i;
    }
    text (8, 220, "smp (0-43400)", true);
}

void Display::buildPan (int arg)
{
    for (int i = 0; i < kChannelsPerPage; ++i)
        if (auto* s = layerStrip (arg, i))
        {
            auto& c = add (Cell::Dial, { 8 + (i % 8) * 38, 48 + (i / 8) * 58, 34, 34 }, bindPanYamaha (*s), juce::String (s->getNumber()));
            c.radius = 9;
        }
}

void Display::drawPan (int arg)
{
    pageTitle ("CH" + rangeName (arg) + " PAN", selected() != nullptr ? selected()->getName() : juce::String());
    for (const auto& c : cells)
        textCentred ({ c.area.getX(), c.area.getY() - 9, c.area.getWidth(), 8 }, c.caption, true);
}

void Display::buildRouting (int arg)
{
    for (int i = 0; i < kChannelsPerPage; ++i)
    {
        auto* s = layerStrip (arg, i);
        if (s == nullptr)
            continue;
        const int x = 38 + i * 11;
        for (int b = 0; b < 8; ++b)
        {
            add (Cell::Toggle, { x, 44 + b * 14, 9, 11 }, bindRoute (s->sends[(size_t) b]), juce::String (b + 1));
        }
        auto& main = s->mains[0];
        KnobBinding st;
        st.get = [&main] { return main.on.load() ? 1.0f : 0.0f; };
        st.set = [&main] (float v)
        {
            main.levelDb = 0.0f;
            main.on = v >= 0.5f;
        };
        st.steps = 2.0f;
        add (Cell::Toggle, { x, 44 + 8 * 14 + 4, 9, 11 }, std::move (st), "S");
    }
}

void Display::drawRouting (int arg)
{
    pageTitle ("CH" + rangeName (arg) + " ROUTING", selected() != nullptr ? selected()->getName() : juce::String());
    for (int i = 0; i < kChannelsPerPage; ++i)
        text (38 + i * 11 + 1, 35, juce::String (firstOf (arg) + i + 1 > 9 ? (firstOf (arg) + i + 1) % 100 : firstOf (arg) + i + 1), true);
    for (int b = 0; b < 8; ++b)
        text (6, 46 + b * 14, "BUS" + juce::String (b + 1), true);
    text (6, 46 + 8 * 14 + 4, "STEREO", true);
    text (6, 190, "Direct Out: each channels output patch.", true);
}

// Bus to Stereo (p.109): BUS 1-8 into the Stereo Out, ON, level and pan.
void Display::buildBusToStereo (int)
{
    const auto& buses = ctx.engine.getBuses();
    for (int b = 0; b < juce::jmin (8, (int) buses.size()); ++b)
    {
        auto& s = *buses[(size_t) b];
        auto& main = s.mains[0];
        KnobBinding on;
        on.get = [&main] { return main.on.load() ? 1.0f : 0.0f; };
        on.set = [&main] (float v) { main.on = v >= 0.5f; };
        on.steps = 2.0f;
        add (Cell::Toggle, { 12 + b * 38, 70, 30, 12 }, std::move (on), "ON");
        auto& level = add (Cell::Dial, { 12 + b * 38, 100, 30, 34 }, bindFader ("LEVEL", main.levelDb), "BUS" + juce::String (b + 1));
        level.radius = 10;
        auto& pan = add (Cell::Dial, { 12 + b * 38, 160, 30, 34 }, bindPanYamaha (s), "PAN");
        pan.radius = 8;
    }
}

void Display::drawBusToStereo (int)
{
    pageTitle ("BUS TO STEREO", ctx.engine.getMains().empty() ? juce::String() : ctx.engine.getMains()[0]->getName());
    for (int b = 0; b < 8; ++b)
    {
        textCentred ({ 12 + b * 38, 55, 30, 8 }, "BUS" + juce::String (b + 1), true);
    }
    text (6, 145, "PAN", true);
    text (6, 90, "LEVEL", true);
}

// The Aux Send pages (p.112): 24 channels, each level with PRE/POST.
void Display::buildAux (int arg)
{
    const int bus = kAuxSlot + aux;
    for (int i = 0; i < kChannelsPerPage; ++i)
        if (auto* s = layerStrip (arg, i))
        {
            auto& send = s->sends[(size_t) bus];
            auto& d = add (Cell::Dial, { 8 + (i % 8) * 38, 44 + (i / 8) * 59, 34, 30 }, bindFader ("SEND", send.levelDb), juce::String (s->getNumber()));
            d.radius = 8;
            KnobBinding mode;
            mode.get = [&send] { return send.mode.load() == 0 ? 1.0f : 0.0f; };
            mode.set = [&send] (float v) { send.mode = v >= 0.5f ? 0 : 1; };
            mode.steps = 2.0f;
            mode.text = [&send] { return juce::String (send.mode.load() == 0 ? "PRE" : "POST"); };
            add (Cell::Toggle, { 14 + (i % 8) * 38, 77 + (i / 8) * 59, 22, 8 }, std::move (mode));
            KnobBinding on;
            on.get = [&send] { return send.enabled.load() ? 1.0f : 0.0f; };
            on.set = [&send] (float v) { send.enabled = v >= 0.5f; };
            on.steps = 2.0f;
            add (Cell::Toggle, { 36 + (i % 8) * 38, 44 + (i / 8) * 59, 6, 6 }, std::move (on), " ");
        }
    button ({ 136, 28, 62, 10 }, "ALL NOMINAL", [this, arg, bus]
    {
        for (int i = 0; i < kChannelsPerPage; ++i)
            if (auto* s = layerStrip (arg, i))
            {
                s->sends[(size_t) bus].levelDb = 0.0f;
                s->sends[(size_t) bus].enabled = true;
            }
    });
}

void Display::drawAux (int arg)
{
    pageTitle ("AUX" + juce::String (aux + 1) + " SEND CH" + rangeName (arg), selected() != nullptr ? selected()->getName() : juce::String());
    text (6, 30, "VARIABLE", true);
    for (int i = 0; i < kChannelsPerPage; ++i)
    {
        const auto n = juce::String (firstOf (arg) + i + 1);
        text (8 + (i % 8) * 38 + 2, 44 + (i / 8) * 59 - 7, n, true);
    }
}

// Matrix Send (p.122): BUS 1-8, AUX 1-12, STEREO into the matrix chosen on MATRIX SELECT.
void Display::buildMatrix (int)
{
    const auto put = [this] (Strip& s, int i)
    {
        auto& m = s.matrix[(size_t) matrix];
        auto& d = add (Cell::Dial, { 8 + (i % 8) * 38, 44 + (i / 8) * 59, 34, 30 }, bindFader ("SEND", m.levelDb), idOf (&s));
        d.radius = 8;
        KnobBinding pre;
        pre.get = [&m] { return m.pre.load() ? 1.0f : 0.0f; };
        pre.set = [&m] (float v) { m.pre = v >= 0.5f; };
        pre.steps = 2.0f;
        pre.text = [&m] { return juce::String (m.pre.load() ? "PRE" : "POST"); };
        add (Cell::Toggle, { 14 + (i % 8) * 38, 77 + (i / 8) * 59, 22, 8 }, std::move (pre));
        KnobBinding on;
        on.get = [&m] { return m.on.load() ? 1.0f : 0.0f; };
        on.set = [&m] (float v) { m.on = v >= 0.5f; };
        on.steps = 2.0f;
        add (Cell::Toggle, { 36 + (i % 8) * 38, 44 + (i / 8) * 59, 6, 6 }, std::move (on), " ");
    };
    int i = 0;
    for (const auto& s : ctx.engine.getBuses())
        put (*s, i++);
    if (! ctx.engine.getMains().empty())
        put (*ctx.engine.getMains()[0], i++);
}

void Display::drawMatrix (int)
{
    pageTitle ("MATRIX" + juce::String (matrix + 1) + " SEND", selected() != nullptr ? selected()->getName() : juce::String());
    for (size_t c = 0; c < cells.size(); c += 3)
    {
        const auto& d = cells[c];
        text (d.area.getX() + 1, d.area.getY() - 7, d.caption, true);
    }
}

// ENCODER MODE ASSIGN (p.62).
void Display::buildAssign (int)
{
    for (int k = 0; k < 4; ++k)
    {
        auto b = bindInt (
            "ASSIGN", [this, k] { return assign[(size_t) k]; },
            [this, k] (int v) { setAssign (k, v); }, 0, assignNames().size() - 1, [] (int v) { return assignNames()[v]; });
        auto& c = add (Cell::Text, { 90, 44 + k * 24, 180, 16 }, std::move (b), "ASSIGN " + juce::String (k + 1));
        c.boxed = true;
    }
}

void Display::setAssign (int key, int item)
{
    assign[(size_t) juce::jlimit (0, 3, key)] = juce::jlimit (0, assignNames().size() - 1, item);
    refresh();
    if (changed)
        changed();
}

void Display::drawAssign (int)
{
    pageTitle ("ENCODER MODE ASSIGN");
    for (int k = 0; k < 4; ++k)
        text (34, 49 + k * 24, "ASSIGN " + juce::String (k + 1));
    text (34, 160, "ENTER is not needed here: the wheel assigns at once.", true);
}

// PARAMETER VIEW (p.150), a photograph of it on a real console.
void Display::buildView (int)
{
    auto* s = selected();
    if (s == nullptr)
        return;
    auto& p = s->params;
    const auto small = [this] (juce::Rectangle<int> r, KnobBinding b, const juce::String& cap)
    {
        auto& c = add (Cell::Dial, r, std::move (b), cap);
        c.radius = 6;
        c.big = cap.isNotEmpty();
    };
    if (s->isInput())
    {
        auto& g = p.gate();
        add (Cell::Toggle, { 12, 98, 26, 9 }, bindBool ("GATE", g.on));
        small ({ 46, 38, 30, 20 }, bindId (models::Dynamics, g.model.load(), g.values, "thr", {}), "THRES");
        small ({ 80, 38, 30, 20 }, bindId (models::Dynamics, g.model.load(), g.values, "range", {}), "RANGE");
        small ({ 46, 66, 30, 20 }, bindId (models::Dynamics, g.model.load(), g.values, "att", {}), "ATTACK");
        small ({ 80, 66, 30, 20 }, bindId (models::Dynamics, g.model.load(), g.values, "dcy", {}), "DECAY");
        small ({ 66, 96, 30, 20 }, bindId (models::Dynamics, g.model.load(), g.values, "hld", {}), "HOLD");
    }
    auto& d = p.dynamics();
    add (Cell::Toggle, { 12, 198, 26, 9 }, bindBool ("COMP", d.on));
    small ({ 46, 138, 30, 20 }, bindId (models::Dynamics, d.model.load(), d.values, "thr", {}), "THRES");
    small ({ 80, 138, 30, 20 }, bindId (models::Dynamics, d.model.load(), d.values, "ratio", {}), "RATIO");
    small ({ 46, 166, 30, 20 }, bindId (models::Dynamics, d.model.load(), d.values, "att", {}), "ATTACK");
    small ({ 80, 166, 30, 20 }, bindId (models::Dynamics, d.model.load(), d.values, "rel", {}), "RELEASE");
    small ({ 46, 194, 30, 20 }, bindId (models::Dynamics, d.model.load(), d.values, "gain", {}), "OUTGAIN");
    small ({ 80, 194, 30, 20 }, bindId (models::Dynamics, d.model.load(), d.values, "knee", {}), "KNEE");

    add (Cell::Toggle, { 158, 28, 24, 9 }, bindBool ("INSERT", p.preInsert().on));
    for (int b = 0; b < 4; ++b)
    {
        const juce::String pre (kBandIds[b]);
        const char* rows[] = { "q", "f", "g" };
        for (int row = 0; row < 3; ++row)
            small ({ 128 + b * 24, 96 + row * 30, 24, 22 }, bindEq (*s, pre + rows[row], {}), {});
    }
    add (Cell::Toggle, { 128, 192, 22, 9 }, bindBool ("EQ", p.eq().on));
    auto& type = add (Cell::Text, { 154, 192, 34, 9 }, bindEq (*s, "type", {}), "TYPE");
    type.boxed = true;
    small ({ 200, 188, 24, 20 }, bindAttenuator (*s), "ATT");

    if (s->isInput())
        add (Cell::Toggle, { 286, 78, 28, 9 }, bindBool ("PHASE", p.input.invert, "REV", "NOR"));
    add (Cell::Toggle, { 286, 96, 28, 9 }, bindBool ("DELAY", p.input.delayOn));
    auto& delay = add (Cell::Text, { 258, 110, 58, 9 }, bindDelay (*s), "DELAY");
    delay.dotted = true;
}

void Display::drawView (int)
{
    auto* s = selected();
    if (s == nullptr)
        return;
    pageTitle (idOf (s) + " PARAMETER VIEW", s->getName());
    hLine (0, 319, 27);
    vLine (124, 27, 227);
    vLine (228, 27, 227);
    hLine (0, 123, 124);
    hLine (124, 319, 70);
    text (3, 29, "GATE", true);
    text (3, 126, "COMP", true);
    if (s->isInput())
        dynCurve (s->params.gate(), { 4, 38, 38, 56 }, true);
    dynCurve (s->params.dynamics(), { 4, 135, 38, 56 }, false);
    text (128, 30, "INSERT", true);
    text (128, 40, "OUT", true);
    dottedBox ({ 148, 39, 70, 8 });
    text (128, 50, "IN", true);
    dottedBox ({ 148, 49, 70, 8 });
    text (128, 62, "EQ", true);
    eqCurve (*s, { 144, 59, 80, 26 });
    for (int b = 0; b < 4; ++b)
        textCentred ({ 128 + b * 24, 89, 24, 7 }, juce::String (kBandNames[b]).substring (0, 5), true);
    meterScale (232, 34, 34);
    for (int ch = 0; ch < 2; ++ch)
        levelMeter (240 + ch * 8, 37, 31, toDb (ctx.meters.get (s, MeterPoint::Output, ch)), 4);
    text (232, 73, "PHASE", true);
    text (232, 92, "DELAY", true);
    text (232, 120, "smp", true);
    text (232, 190, "PAIR", true);
    heart (262, 206, s->params.stereo.load());
}

// FADER VIEW (p.151): each layer's faders with their ON keys.
void Display::buildFaderView (int arg)
{
    int i = 0;
    const auto put = [this, &i] (Strip& s)
    {
        add (Cell::Slide, { 8 + i * 13, 54, 12, 122 }, bindFader ("FADER", s.params.faderDb), idOf (&s));
        ++i;
    };
    if (arg < 4)
    {
        for (int k = 0; k < kChannelsPerPage; ++k)
            if (auto* s = layerStrip (arg, k))
                put (*s);
    }
    else
    {
        for (const auto* l : { &ctx.engine.getBuses(), &ctx.engine.getMatrices() })
            for (const auto& s : *l)
                put (*s);
    }
}

void Display::drawFaderView (int arg)
{
    pageTitle ((arg < 4 ? "CH" + rangeName (arg) : juce::String ("MASTER")) + " FADER VIEW", selected() != nullptr ? selected()->getName() : juce::String());
    for (size_t i = 0; i < cells.size(); ++i)
    {
        const auto& c = cells[i];
        text (c.area.getX() + 1, 44, juce::String ((int) (arg < 4 ? firstOf (arg) + (int) i + 1 : (int) i + 1)), true);
        const auto v = ascii (fader::text (c.binding.isValid() ? fader::posToDb (c.binding.get()) : -144.0f));
        text (c.area.getX() + 1, 180, v.substring (0, 3), true);
    }
}

void Display::buildMeter (int) {}

void Display::drawMeter (int arg)
{
    pageTitle ((arg < 4 ? "CH" + rangeName (arg) : juce::String ("MASTER")) + " METER");
    std::vector<Strip*> strips;
    if (arg < 4)
    {
        for (int k = 0; k < kChannelsPerPage; ++k)
            strips.push_back (layerStrip (arg, k));
    }
    else
    {
        for (const auto* l : { &ctx.engine.getBuses(), &ctx.engine.getMatrices(), &ctx.engine.getMains() })
            for (const auto& s : *l)
                strips.push_back (s.get());
    }
    const int y0 = 52, h = 136, pitch = arg < 4 ? 12 : 11;
    static const std::pair<const char*, float> marks[] = { { "0", 0.0f }, { "-6", -6.0f }, { "-12", -12.0f }, { "-18", -18.0f }, { "-30", -30.0f }, { "-48", -48.0f } };
    for (const auto& [label, db] : marks)
    {
        const int y = y0 + h - 1 - juce::roundToInt ((db + 48.0f) / 48.0f * (float) (h - 1));
        dottedH (22, 22 + (int) strips.size() * pitch, y);
        text (2, y - 2, label, true);
    }
    for (int i = 0; i < (int) strips.size(); ++i)
        if (auto* s = strips[(size_t) i])
        {
            levelMeter (24 + i * pitch, y0, h, toDb (ctx.meters.get (s, MeterPoint::Output, 0)), 5);
            text (24 + i * pitch, y0 + h + 3, arg < 4 ? juce::String ((firstOf (arg) + i + 1) % 100) : juce::String (i + 1), true);
        }
}

// The fader groups' and mute groups' grids (pp.91, 149): groups A-H and Q-T the DCAs (0-7, 8-11);
// I-P and U-X the mute groups (0-7, 8-11).
void Display::buildGroupGrid (int arg)
{
    const bool input = arg == 0 || arg == 1, mute = arg == 1 || arg == 3;
    const int count = input ? 8 : 4, first = input ? 0 : 8;
    std::vector<Strip*> strips;
    if (input)
        for (int i = 0; i < 48; ++i)
            strips.push_back (layerStrip (i / 24, i % 24));
    else
        for (const auto* l : { &ctx.engine.getBuses(), &ctx.engine.getMatrices(), &ctx.engine.getMains() })
            for (const auto& s : *l)
                strips.push_back (s.get());
    const int columns = input ? 24 : 25;
    for (int g = 0; g < count; ++g)
        for (int i = 0; i < (int) strips.size(); ++i)
            if (auto* s = strips[(size_t) i])
            {
                auto& mask = mute ? s->params.muteGroupMask : s->params.dcaMask;
                const int col = i % columns, row = i / columns;
                add (Cell::Mark, { 22 + col * 11, 44 + row * 96 + g * 10, 9, 8 }, bindMask (mask, first + g), juce::String (kGroupLetters[(mute ? (input ? 8 : 20) : (input ? 0 : 16)) + g]));
            }
}

void Display::drawGroupGrid (int arg)
{
    const bool input = arg == 0 || arg == 1, mute = arg == 1 || arg == 3;
    pageTitle (juce::String (input ? "INPUT" : "OUTPUT") + (mute ? " MUTE GROUP" : " FADER GROUP"));
    const int count = input ? 8 : 4, base = mute ? (input ? 8 : 20) : (input ? 0 : 16);
    const int rows = input ? 2 : 1;
    for (int r = 0; r < rows; ++r)
    {
        for (int g = 0; g < count; ++g)
            text (6, 44 + r * 96 + g * 10, juce::String::charToString (kGroupLetters[base + g]));
        for (int i = 0; i < 24; ++i)
            text (22 + i * 11 + 1, 36 + r * 96, juce::String ((r * 24 + i + 1) % 100), true);
    }
}

void Display::buildGroupMaster (int arg)
{
    const int first = arg == 0 ? 0 : 8, count = arg == 0 ? 8 : 4;
    for (int i = 0; i < count; ++i)
    {
        auto& d = ctx.engine.getDca (first + i);
        KnobBinding on;
        on.get = [&d] { return d.mute.load() ? 0.0f : 1.0f; };
        on.set = [&d] (float v) { d.mute = v < 0.5f; };
        on.text = [&d] { return juce::String (d.mute.load() ? "OFF" : "ON"); };
        on.steps = 2.0f;
        add (Cell::Toggle, { 10 + i * 38, 42, 26, 11 }, std::move (on));
        add (Cell::Slide, { 12 + i * 38, 60, 22, 130 }, bindFader ("MASTER", d.faderDb), juce::String::charToString (kGroupLetters[(arg == 0 ? 0 : 16) + i]));
    }
}

void Display::drawGroupMaster (int arg)
{
    pageTitle (juce::String (arg == 0 ? "INPUT" : "OUTPUT") + " FADER GROUP MASTER");
    const int count = arg == 0 ? 8 : 4, base = arg == 0 ? 0 : 16;
    for (int i = 0; i < count; ++i)
    {
        textCentred ({ 10 + i * 38, 32, 26, 8 }, juce::String::charToString (kGroupLetters[base + i]));
        const auto& c = cells[(size_t) (i * 2 + 1)];
        const auto v = ascii (fader::text (fader::posToDb (c.binding.get())));
        textCentred ({ 10 + i * 38, 195, 26, 8 }, v, true);
    }
}

juce::Array<int> Display::fxTypes() const
{
    juce::Array<int> list;
    const auto& t = models::table (models::Fx);
    for (int i = 0; i < (int) t.size(); ++i)
        if (models::consoleOf (t[(size_t) i].token) == "DM2000")
            list.add (i);
    return list;
}

// FX EDIT (p.177): unit, type, and its parameters in four columns.
void Display::buildFxEdit (int)
{
    auto& fx = ctx.engine.getFx();
    const int model = fx.getModel (fxUnit);
    add (Cell::Toggle, { 110, 42, 44, 12 }, bindBool ("BYPASS", fx.params (fxUnit).bypass, "BYPASS", "BYPASS"), "BYPASS");
    auto& mix = add (Cell::Dial, { 26, 76, 36, 26 }, bindLinear ("MIX", fx.params (fxUnit).mix, 0.0f, 100.0f, "%", 0, 100.0f), "MIX");
    mix.radius = 8;
    if (model <= ld::kFxExt)
        return;
    const auto& spec = models::model (models::Fx, model);
    int n = 0;
    for (int i = 0; i < juce::jmin ((int) spec.params.size(), 20); ++i)
    {
        auto& c = add (Cell::Line, { 6 + (n % 4) * 78, 122 + (n / 4) * 21, 74, 17 }, bindModelParam (fx.params (fxUnit).values[i], spec.params[(size_t) i]), spec.params[(size_t) i].label.toUpperCase().substring (0, 9));
        c.radius = 7;
        ++n;
    }
}

void Display::drawFxEdit (int)
{
    auto& fx = ctx.engine.getFx();
    const int model = fx.getModel (fxUnit);
    pageTitle ("EFFECT " + juce::String (fxUnit + 1) + " EDIT", model > ld::kFxExt ? models::model (models::Fx, model).name : juce::String ("No Effect"));
    text (6, 32, "EFFECT", true);
    box ({ 6, 40, 92, 15 });
    textCentred ({ 6, 40, 92, 15 }, model > ld::kFxExt ? models::model (models::Fx, model).name.substring (0, 14) : juce::String ("---"));
    hLine (2, 317, 112);
    auto& channels = ctx.engine.getChannels();
    if (fxUnit * 2 + 72 < (int) channels.size())
    {
        auto& s = *channels[(size_t) (72 + fxUnit * 2)];
        stripMeters (s, { 250, 36, 56, 66 });
    }
}

// GEQ EDIT (pp.183-184): six units, each an EQ block on an Output Channel.
std::vector<Strip*> Display::geqStrips() const
{
    std::vector<Strip*> out;
    const int geq = models::findModel (models::BusEq, "DMGEQ");
    for (const auto* l : { &ctx.engine.getBuses(), &ctx.engine.getMatrices(), &ctx.engine.getMains() })
        for (const auto& s : *l)
            for (const auto& step : s->getChain())
                if (step.isBlock() && step.getBlockKind() == BlockKind::Eq)
                    if (auto* eq = s->getBlock (step.uid).eq(); eq != nullptr && eq->model.load() == geq)
                    {
                        out.push_back (s.get());
                        break;
                    }
    return out;
}

bool Display::insertGeq (Strip& s)
{
    const auto have = geqStrips();
    if ((int) have.size() >= kMaxGeqs || std::find (have.begin(), have.end(), &s) != have.end() || ! s.isOutput())
        return false;
    const auto chain = s.getChain();
    int at = -1;
    for (int i = 0; i < (int) chain.size(); ++i)
        if (chain[(size_t) i].kind == ld::kStepEq)
            at = i + 1;
    const int uid = s.insertBlock (BlockKind::Eq, at);
    if (uid < 0 || ! s.setBlockModel (uid, models::findModel (models::BusEq, "DMGEQ")))
        return false;
    if (auto* eq = s.getBlock (uid).eq())
        eq->on = true;
    return true;
}

void Display::removeGeq (Strip& s)
{
    const int geq = models::findModel (models::BusEq, "DMGEQ");
    for (const auto& step : s.getChain())
        if (step.isBlock() && step.getBlockKind() == BlockKind::Eq)
            if (auto* eq = s.getBlock (step.uid).eq(); eq != nullptr && eq->model.load() == geq)
            {
                s.removeBlock (step.uid);
                return;
            }
}

void Display::buildGeq (int)
{
    const int geq = models::findModel (models::BusEq, "DMGEQ");
    const auto strips = geqStrips();
    for (int u = 0; u < kMaxGeqs; ++u)
    {
        KnobBinding b;
        b.get = [this, u] { return geqUnit == u ? 1.0f : 0.0f; };
        b.set = [this, u] (float) { setGeqUnit (u); };
        b.steps = 2.0f;
        add (Cell::Toggle, { 6 + u * 13, 44, 12, 11 }, std::move (b), juce::String (u + 1));
    }
    button ({ 96, 44, 56, 12 }, "INSERT", [this]
    {
        if (auto* s = selected(); s != nullptr && ! insertGeq (*s) && s->isOutput())
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("GEQ").withMessage ("All six GEQs are in use, or this output already has one.").withButton ("OK").withAssociatedComponent (this), nullptr);
        rebuild();
    });
    button ({ 158, 44, 52, 12 }, "REMOVE", [this]
    {
        const auto list = geqStrips();
        if (geqUnit < (int) list.size())
            removeGeq (*list[(size_t) geqUnit]);
        rebuild();
    });
    if (geqUnit >= (int) strips.size())
        return;
    auto& s = *strips[(size_t) geqUnit];
    ld::EqBlockParams* eq = nullptr;
    for (const auto& step : s.getChain())
        if (step.isBlock() && step.getBlockKind() == BlockKind::Eq)
            if (auto* block = s.getBlock (step.uid).eq(); block != nullptr && block->model.load() == geq)
                eq = block;
    if (eq == nullptr)
        return;
    const auto& m = models::model (models::BusEq, geq);
    add (Cell::Toggle, { 216, 44, 26, 12 }, bindBool ("GEQ", eq->on), "ON");
    auto lim = bindModelParam (eq->values[0], m.params[0]);
    auto& limit = add (Cell::Text, { 262, 44, 52, 12 }, std::move (lim), "LIMIT");
    limit.boxed = true;
    for (int band = 1; band <= 31; ++band)
        add (Cell::Slide, { 22 + (band - 1) * 9, 118, 8, 88 }, bindModelParam (eq->values[band], m.params[(size_t) band]), m.params[(size_t) band].label);
    button ({ 6, 62, 40, 12 }, "FLAT", [eq, count = (int) m.params.size()]
    {
        for (int i = 1; i < count; ++i)
            eq->values[i] = 0.0f;
    });
}

void Display::drawGeq (int)
{
    const auto strips = geqStrips();
    pageTitle ("GEQ " + juce::String (geqUnit + 1) + " EDIT", geqUnit < (int) strips.size() ? idOf (strips[(size_t) geqUnit]) : juce::String ("No output"));
    text (6, 30, "UNIT (" + juce::String ((int) strips.size()) + " of 6 in use)", true);
    text (96, 33, "SEL CHANNEL", true);
    text (262, 33, "LIMIT", true);
    if (geqUnit >= (int) strips.size())
    {
        textCentred ({ 0, 110, kDotsW, 12 }, "Select an Output Channel and press INSERT.");
        return;
    }
    // The curve from the bands' positions, and the band under the cursor.
    const juce::Rectangle<int> r (22, 76, 279, 34);
    box (r);
    hLine (r.getX(), r.getRight() - 1, r.getCentreY());
    int shown = -1;
    for (int i = 0; i < (int) cells.size(); ++i)
        if (cells[(size_t) i].kind == Cell::Slide)
        {
            if (shown < 0)
                shown = i;
            const int band = i - shown, x = r.getX() + 4 + band * 9;
            const float db = juce::jmap (cells[(size_t) i].binding.get(), 0.0f, 1.0f, -24.0f, 15.0f);
            const int y = r.getCentreY() - juce::roundToInt (juce::jlimit (-15.0f, 15.0f, db) / 15.0f * 15.0f);
            vLine (x, juce::jmin (y, r.getCentreY()), juce::jmax (y, r.getCentreY()));
        }
    for (int mark : { 0, 15 })
        dottedH (22, 300, 118 + 88 - 1 - juce::roundToInt ((float) mark * 2.0f));
    text (22, 212, "20", true);
    text (280, 212, "20k", true);
    if (juce::isPositiveAndBelow (focus, (int) cells.size()) && cells[(size_t) focus].kind == Cell::Slide)
        text (92, 212, "FREQUENCY=" + cells[(size_t) focus].caption + "  GAIN=" + lcdValue (cells[(size_t) focus].binding), true);
}

// The Scene Memory page (p.188).
void Display::buildScene (int)
{
    auto number = bindInt (
        "SCENE", [this] { return memory; }, [this] (int v) { setMemory (v); }, 0, 99, [] (int) { return juce::String(); });
    auto& list = add (Cell::Text, { 66, 125, 180, 9 }, std::move (number), "SCENE");
    list.dotted = true;
    button ({ 8, 118, 48, 15 }, "RECALL", [this] { recallScene(); });
    button ({ 8, 142, 48, 15 }, "STORE", [this] { storeScene(); });
}

void Display::drawScene (int)
{
    pageTitle ("SCENE MEMORY", sceneTitle (memory));
    text (66, 32, "No.   TITLE", true);
    box ({ 64, 40, 184, 170 });
    for (int row = 0; row < 14; ++row)
    {
        const int m = memory - 7 + row, y = 42 + row * 12;
        if (! juce::isPositiveAndBelow (m, 100))
            continue;
        text (68, y, juce::String (m).paddedLeft ('0', 2));
        text (90, y, sceneTitle (m).substring (0, 22));
        if (m == memory)
            for (int i = 0; i < 4; ++i)
                vLine (58 + i, y + 3 - i, y + 3 + i);
    }
}

void Display::buildPatch (int arg)
{
    for (int i = 0; i < kChannelsPerPage; ++i)
        if (auto* s = layerStrip (arg, i))
        {
            auto b = bindInt (
                "PATCH", [s] { return s->params.inputLeft.load(); },
                [s] (int v) { s->params.inputLeft = v; }, -1, ctx.engine.getNumSources() - 1,
                [] (int v) { return v < 0 ? juce::String ("NONE") : routing::sourceLabel (v); });
            auto& c = add (Cell::Text, { 8 + (i % 6) * 52, 44 + (i / 6) * 42, 46, 12 }, std::move (b), juce::String (s->getNumber()));
            c.dotted = true;
        }
}

void Display::drawPatch (int arg)
{
    pageTitle ("INPUT PATCH CH" + rangeName (arg), selected() != nullptr ? selected()->getName() : juce::String());
    for (const auto& c : cells)
        textCentred ({ c.area.getX(), c.area.getY() - 9, c.area.getWidth(), 8 }, c.caption, true);
}

void Display::buildMonitor (int)
{
    auto& e = ctx.engine;
    auto& level = add (Cell::Dial, { 26, 54, 50, 34 }, bindFader ("CR", e.monitorLevelDb), "LEVEL");
    level.radius = 14;
    add (Cell::Toggle, { 100, 56, 50, 13 }, bindBool ("DIMMER", e.monitorDim), "DIMMER");
    add (Cell::Toggle, { 160, 56, 50, 13 }, bindBool ("MONO", e.monitorMono), "MONO");
    add (Cell::Toggle, { 220, 56, 50, 13 }, bindBool ("MUTE", e.monitorMute), "MUTE");
    auto& ph = add (Cell::Dial, { 26, 126, 50, 34 }, bindFader ("PHONES", e.phones.levelDb), "PHONES");
    ph.radius = 14;
}

void Display::drawMonitor (int)
{
    pageTitle ("CONTROL ROOM");
    frame ({ 10, 44, 300, 62 }, "CONTROL ROOM");
    text (28, 96, "LEVEL", true);
    text (28, 168, "PHONES", true);
    for (int ch = 0; ch < 2; ++ch)
        levelMeter (280 + ch * 8, 60, 44, toDb (ctx.engine.monitorPeak[ch].load()), 5);
}

//==============================================================================
// Messages and confirmations (p.54).

void Display::drawWindow (juce::Rectangle<int> r)
{
    fill (r.expanded (2), false);
    box (r);
    box (r.reduced (2));
}

void Display::showMessage (const juce::String& text)
{
    message = text;
    messageUntil = juce::Time::getMillisecondCounter() + 2000;
    refresh();
}

juce::String Display::getMessage() const { return message; }

void Display::confirm (const juce::String& question, std::function<void()> yes)
{
    if (confirmGo == nullptr)
        focusBeforeConfirm = focus;
    confirmText = question;
    confirmGo = std::move (yes);
    focus = 0;
    rebuild();
}

void Display::answer (bool yes)
{
    const auto go = std::move (confirmGo);
    confirmGo = nullptr;
    focus = focusBeforeConfirm;
    rebuild();
    if (yes && go)
        go();
    refresh();
}

//==============================================================================
// The preferences (PREFER1, p.274): the ones that do something on this surface.

bool Display::preference (Pref p) const
{
    const auto& spec = kPrefs[(size_t) juce::jlimit (0, (int) Pref::kCount - 1, (int) p)];
    return ctx.settings.getBoolValue (spec.key, spec.initial);
}

void Display::setPreference (Pref p, bool on)
{
    ctx.settings.setValue (kPrefs[(size_t) juce::jlimit (0, (int) Pref::kCount - 1, (int) p)].key, on);
}

bool Display::autoDisplay (Group g) const
{
    if (g == Group::Routing)
        return preference (Pref::AutoRouting);
    if (g == Group::PhaseInsert)
        return preference (Pref::AutoPhase);
    if (g == Group::Delay)
        return preference (Pref::AutoDelay);
    if (g == Group::AuxSend || g == Group::MatrixSend)
        return preference (Pref::AutoAux);
    if (g == Group::Dynamics)
        return preference (Pref::AutoDynamics);
    if (g == Group::Pan)
        return preference (Pref::AutoPan);
    if (g == Group::Equalizer)
        return preference (Pref::AutoEq);
    return true;
}

int Display::getCopyParameters() const { return juce::jlimit (1, 127, ctx.settings.getIntValue (kCopyKey, 1)); }

void Display::setCopyParameters (int bits) { ctx.settings.setValue (kCopyKey, juce::jlimit (1, 127, bits)); }

void Display::buildPrefs (int arg)
{
    if (arg == 0)
    {
        for (int i = 0; i < (int) Pref::kCount; ++i)
        {
            KnobBinding b;
            const auto pref = (Pref) i;
            b.get = [this, pref] { return preference (pref) ? 1.0f : 0.0f; };
            b.set = [this, pref] (float v) { setPreference (pref, v >= 0.5f); };
            b.text = [this, pref] { return juce::String (preference (pref) ? "ON" : "OFF"); };
            b.steps = 2.0f;
            add (Cell::Check, { 10 + (i / 5) * 156, 46 + (i % 5) * 26, 9, 9 }, std::move (b), kPrefs[i].name);
        }
        return;
    }
    // Channel Copy Parameter (p.276): ALL, or any of the others.
    static const char* const names[] = { "ALL", "FADER", "ON", "PAN", "AUX", "AUX ON", "EQ" };
    for (int i = 0; i < 7; ++i)
    {
        KnobBinding b;
        b.get = [this, i] { return ((getCopyParameters() >> i) & 1) != 0 ? 1.0f : 0.0f; };
        b.set = [this, i] (float v)
        {
            int bits = getCopyParameters();
            if (i == 0)
                bits = 1;
            else
                bits = (v >= 0.5f ? bits | (1 << i) : bits & ~(1 << i)) & ~1;
            setCopyParameters (bits == 0 ? 1 : bits);
        };
        b.steps = 2.0f;
        add (Cell::Toggle, { 34 + (i % 4) * 64, 62 + (i / 4) * 18, 54, 11 }, std::move (b), names[i]);
    }
}

void Display::drawPrefs (int arg)
{
    pageTitle (arg == 0 ? "PREFERENCES1" : "PREFERENCES2");
    if (arg == 0)
    {
        for (const auto& c : cells)
            text (c.area.getRight() + 5, c.area.getY() + 2, c.caption, true);
        text (10, 186, "The consoles other preferences have nothing to work on here.", true);
        return;
    }
    text (10, 46, "Channel Copy Parameter");
    text (10, 104, "What CHANNEL [PASTE] copies. SURR is not here: no surround pan.", true);
    text (10, 126, "Short names touch sense brightness and the date:", true);
    text (10, 134, "not on this surface.", true);
}

//==============================================================================
// MIDI SETUP, the Program Change Assign Table and the Control Change Assign Table (pp.217-219).

juce::StringArray Display::midiTokens (bool pcTable) const
{
    auto* midi = dynamic_cast<Dm2000Midi*> (ctx.consoleMidi);
    juce::StringArray v;
    v.addTokens (midi != nullptr ? (pcTable ? midi->getPcText() : midi->getSetupText()) : ctx.settings.getValue (pcTable ? Dm2000Midi::pcKey : Dm2000Midi::setupKey), " ", "");
    v.removeEmptyStrings();
    if (v.size() != (pcTable ? 128 : kSetupValues))
    {
        v.clear();
        v.addTokens (pcTable ? Dm2000Midi::initialPcText() : Dm2000Midi::initialSetupText(), " ", "");
        v.removeEmptyStrings();
    }
    return v;
}

void Display::setMidiTokens (bool pcTable, const juce::StringArray& v)
{
    // Kept as Dm2000Midi keeps them, so the console's MIDI finds them at its next start too.
    const auto line = v.joinIntoString (" ");
    ctx.settings.setValue (pcTable ? Dm2000Midi::pcKey : Dm2000Midi::setupKey, line);
    if (auto* midi = dynamic_cast<Dm2000Midi*> (ctx.consoleMidi))
    {
        if (pcTable)
            midi->setPcText (line);
        else
            midi->setSetupText (line);
    }
}

int Display::getMidiSetup (int index) const
{
    return juce::isPositiveAndBelow (index, kSetupValues) ? midiTokens (false)[index].getIntValue() : 0;
}

void Display::setMidiSetup (int index, int value)
{
    if (! juce::isPositiveAndBelow (index, kSetupValues))
        return;
    auto v = midiTokens (false);
    v.set (index, juce::String (index < 2 ? juce::jlimit (1, 16, value) : (value != 0 ? 1 : 0)));
    setMidiTokens (false, v);
}

int Display::getPcScene (int program) const
{
    return juce::isPositiveAndBelow (program, 128) ? juce::jlimit (0, 100, midiTokens (true)[program].getIntValue()) : 100;
}

void Display::setPcScene (int program, int scene)
{
    if (! juce::isPositiveAndBelow (program, 128))
        return;
    auto v = midiTokens (true);
    v.set (program, juce::String (juce::jlimit (0, 100, scene)));
    setMidiTokens (true, v);
}

void Display::initializePcTable()
{
    juce::StringArray v;
    v.addTokens (Dm2000Midi::initialPcText(), " ", "");
    v.removeEmptyStrings();
    setMidiTokens (true, v);
}

namespace
{
    // MIDI SETUP's columns and rows, off the manual's picture (p.217).
    constexpr int kMidiColumns[] = { 129, 172, 217, 261 };
    constexpr int kMidiRowTop = 60, kMidiRowHeight = 22;
    struct MidiRow
    {
        const char* first;
        const char* second;
        int kind; // Dm2000Midi's six kinds; -1 the channels
        bool tx, rx, omni, echo;
    };
    const MidiRow kMidiRows[] = {
        { "CHANNEL", "", -1, true, true, false, false },
        { "PROGRAM", "CHANGE", 0, true, true, true, true },
        { "CONTROL", "CHANGE", 1, true, true, false, true },
        { "PARAMETER", "CHANGE", 2, true, true, false, true },
        { "BULK", "", 3, false, true, false, false },
        { "OTHER", "COMMANDS", 5, false, false, false, true },
    };
} // namespace

void Display::buildMidiSetup (int)
{
    const auto flag = [this] (int index)
    {
        KnobBinding b;
        b.get = [this, index] { return getMidiSetup (index) != 0 ? 1.0f : 0.0f; };
        b.set = [this, index] (float v) { setMidiSetup (index, v >= 0.5f ? 1 : 0); };
        b.text = [this, index] { return juce::String (getMidiSetup (index) != 0 ? "ON" : "OFF"); };
        b.steps = 2.0f;
        return b;
    };
    for (int r = 0; r < 6; ++r)
    {
        const auto& row = kMidiRows[r];
        const int y = kMidiRowTop + r * kMidiRowHeight + 5;
        if (row.kind < 0)
        {
            // The setup's first number is the Rx channel, its second the Tx channel.
            for (int column = 0; column < 2; ++column)
            {
                const int index = column == 0 ? 1 : 0;
                auto b = bindInt (
                    column == 0 ? "Tx CH" : "Rx CH", [this, index] { return getMidiSetup (index); },
                    [this, index] (int v) { setMidiSetup (index, v); }, 1, 16, [] (int v) { return juce::String (v); });
                auto& c = add (Cell::Text, { kMidiColumns[column] - 11, y, 22, 11 }, std::move (b), column == 0 ? "Tx CH" : "Rx CH");
                c.dotted = true;
            }
            continue;
        }
        const bool has[] = { row.tx, row.rx, row.omni, row.echo };
        for (int column = 0; column < 4; ++column)
            if (has[column])
                add (Cell::Toggle, { kMidiColumns[column] - 11, y, 22, 11 }, flag (2 + column * 6 + row.kind));
    }
    // Fader Resolution: HIGH is the 10-bit table, LOW 256 steps.
    for (int low = 0; low < 2; ++low)
    {
        KnobBinding b;
        b.get = [this, low] { return (getMidiSetup (27) != 0) == (low != 0) ? 1.0f : 0.0f; };
        b.set = [this, low] (float) { setMidiSetup (27, low); };
        b.steps = 2.0f;
        add (Cell::Toggle, { 170 + low * 36, 202, 30, 11 }, std::move (b), low != 0 ? "LOW" : "HIGH");
    }
}

void Display::drawMidiSetup (int)
{
    pageTitle ("MIDI SETUP");
    static const char* const heads[] = { "Tx", "Rx", "OMNI", "ECHO" };
    for (int column = 0; column < 4; ++column)
        textCentred ({ kMidiColumns[column] - 16, 46, 32, 8 }, heads[column]);
    vLine (100, 42, kMidiRowTop + 6 * kMidiRowHeight - 2);
    hLine (28, 292, kMidiRowTop - 3);
    hLine (28, 292, kMidiRowTop - 2);
    for (int r = 0; r < 6; ++r)
    {
        const auto& row = kMidiRows[r];
        const int top = kMidiRowTop + r * kMidiRowHeight;
        const juce::String second (row.second);
        if (second.isEmpty())
            textCentred ({ 28, top + 7, 72, 6 }, row.first, true);
        else
        {
            textCentred ({ 28, top + 4, 72, 6 }, row.first, true);
            textCentred ({ 28, top + 10, 72, 6 }, second, true);
        }
        const bool has[] = { row.tx, row.rx, row.omni, row.echo };
        for (int column = 0; column < 4; ++column)
            if (! has[column])
                hLine (kMidiColumns[column] - 2, kMidiColumns[column] + 2, top + 10);
        if (r < 5)
            dottedH (28, 292, top + kMidiRowHeight - 2);
        else
            hLine (28, 292, top + kMidiRowHeight - 2);
    }
    text (62, 204, "Fader Resolution:");
    if (dynamic_cast<Dm2000Midi*> (ctx.consoleMidi) == nullptr)
        text (8, 219, "The consoles MIDI is not running: kept for when it is.", true);
}

void Display::buildMidiProgram (int)
{
    auto no = bindInt (
        "PGM CHG.", [this] { return midiProgram + 1; }, [this] (int v) { midiProgram = v - 1; }, 1, 128, [] (int) { return juce::String(); });
    auto& n = add (Cell::Text, { 56, 117, 26, 11 }, std::move (no), "PGM CHG.");
    n.dotted = true;
    auto scene = bindInt (
        "SCENE", [this] { return getPcScene (midiProgram); }, [this] (int v) { setPcScene (midiProgram, v); }, 0, 100, [] (int) { return juce::String(); });
    auto& s = add (Cell::Text, { 100, 117, 150, 11 }, std::move (scene), "SCENE");
    s.dotted = true;
    button ({ 228, 200, 66, 13 }, "INITIALIZE", [this] { initializePcTable(); });
}

void Display::drawMidiProgram (int)
{
    pageTitle ("PROGRAM CHANGE ASSIGN TABLE");
    text (44, 42, "PGM CHG.", true);
    text (104, 42, "SCENE No./TITLE", true);
    hLine (40, 94, 50);
    hLine (100, 292, 50);
    hLine (40, 94, 190);
    hLine (100, 292, 190);
    text (34, 119, "No.", true);
    // Ten rows, the higher numbers above, the chosen one in the dotted boxes.
    for (int r = 0; r < 10; ++r)
    {
        const int program = midiProgram + 6 - r, y = 53 + r * 11;
        if (! juce::isPositiveAndBelow (program, 128))
            continue;
        const auto number = juce::String (program + 1);
        text (79 - textWidth (number), y, number);
        text (88, y, "=");
        const int scene = getPcScene (program);
        if (scene >= 100)
            text (104, y, "NO ASSIGN");
        else
        {
            const bool held = scene == 0 || scene - 1 < ctx.show.getNumScenes();
            text (104, y, juce::String (scene).paddedLeft (' ', 2) + "." + (held ? sceneTitle (scene).substring (0, 20) : juce::String ("[   No Data!   ]")));
        }
    }
}

void Display::buildMidiControl (int)
{
    // MODE: TABLE the manual's Control Changes, NRPN the unpublished numbers (the setting alone is kept).
    for (int nrpn = 0; nrpn < 2; ++nrpn)
    {
        KnobBinding b;
        b.get = [this, nrpn] { return (getMidiSetup (26) != 0) == (nrpn != 0) ? 1.0f : 0.0f; };
        b.set = [this, nrpn] (float) { setMidiSetup (26, nrpn); };
        b.steps = 2.0f;
        add (Cell::Toggle, { 224 + nrpn * 46, 16, 42, 11 }, std::move (b), nrpn != 0 ? "NRPN" : "TABLE");
    }
    auto no = bindInt (
        "No.", [this] { return midiControl; }, [this] (int v) { midiControl = v; }, 0, 119, [] (int) { return juce::String(); });
    auto& n = add (Cell::Text, { 8, 117, 22, 11 }, std::move (no), "No.");
    n.dotted = true;
    auto table = bindInt (
        "CH", [this] { return midiTable + 1; }, [this] (int v) { midiTable = v - 1; }, 1, 16, [] (int) { return juce::String(); });
    auto& t = add (Cell::Text, { 36, 117, 20, 11 }, std::move (table), "CH");
    t.dotted = true;
}

void Display::drawMidiControl (int)
{
    pageTitle ("CONTROL CHANGE ASSIGN TABLE");
    text (196, 19, "MODE", true);
    text (10, 42, "No.(CH)", true);
    text (72, 42, "PARAMETER", true);
    hLine (6, 60, 50);
    hLine (66, 312, 50);
    hLine (6, 312, 190);
    static const char* const kinds[] = { "NO ASSIGN", "FADER H", "FADER L", "ON", "PAN", "EQ ATT H", "EQ ATT L", "EQ ON" };
    for (int r = 0; r < 10; ++r)
    {
        const int cc = midiControl + 6 - r, y = 53 + r * 11;
        if (! juce::isPositiveAndBelow (cc, 120))
            continue;
        const auto number = juce::String (cc);
        text (27 - textWidth (number), y, number);
        text (32, y, "(" + juce::String (midiTable + 1).paddedLeft (' ', 2) + ")=");
        const int entry = ld::ld_dm2000_cc_initial (midiTable, cc), kind = juce::jlimit (0, 7, entry / 256), target = entry % 256;
        text (72, y, kinds[kind]);
        if (kind == 0)
            continue;
        text (136, y, target < 96 ? "CHANNEL" : "MASTER");
        text (196, y, target < 96 ? "INPUT" + juce::String (target + 1) : target < 104 ? "BUS" + juce::String (target - 95) : target < 116 ? "AUX" + juce::String (target - 103) : target < 120 ? "MATRIX" + juce::String (target - 115) : juce::String ("STEREO"));
    }
    text (8, 196, "Yamahas initial table (pp.353-368) as the consoles MIDI uses it:", true);
    text (8, 204, "assignments cannot be changed here.", true);
    if (getMidiSetup (26) != 0)
        text (8, 216, "NRPN: the numbers are not published so no Control Changes pass.", true);
}

//==============================================================================
// The libraries (pp.165-172).

void Display::setLibraryFolder (const juce::File& folder)
{
    libraryRoot = folder;
    for (auto& l : libraryFiles)
        l.scanned = false;
    refresh();
}

int Display::firstLibraryMemory (Library l) const { return kLibraries[(size_t) l].first; }
int Display::firstUserMemory (Library l) const { return kLibraries[(size_t) l].firstUser; }
int Display::lastLibraryMemory (Library l) const { return kLibraries[(size_t) l].last; }

juce::File Display::libraryFolderOf (Library l) const { return libraryRoot.getChildFile (kLibraries[(size_t) l].folder); }

juce::File Display::libraryFile (Library l, int m) const
{
    auto& cache = libraryFiles[(size_t) l];
    if (! cache.scanned)
    {
        // A memory's file is "041 Title.ext": its number, then its title.
        cache.scanned = true;
        cache.files.clear();
        for (const auto& f : libraryFolderOf (l).findChildFiles (juce::File::findFiles, false, juce::String ("*") + kLibraries[(size_t) l].extension))
        {
            const auto name = f.getFileName();
            if (name.length() > 4 && name[3] == ' ' && name.substring (0, 3).containsOnly ("0123456789"))
                cache.files[name.substring (0, 3).getIntValue()] = f;
        }
    }
    const auto found = cache.files.find (m);
    return found != cache.files.end() ? found->second : juce::File();
}

juce::String Display::presetTitle (Library l, int m) const
{
    // The dynamics presets Sidecar has are each type's own (pp.333-334: 1 and 2 of the gates, 1-4 of
    // the compressors); the effects' are its types.
    static const char* const gates[] = { "Gate", "Ducking" };
    static const char* const comps[] = { "Comp", "Expand", "Compander (H)", "Compander (S)" };
    if (l == Library::Gate && m >= 1 && m <= 2)
        return gates[m - 1];
    if (l == Library::Comp && m >= 1 && m <= 4)
        return comps[m - 1];
    if (l == Library::Effects)
        if (const auto list = fxTypes(); m >= 1 && m <= list.size())
            return models::model (models::Fx, list[m - 1]).name;
    return {};
}

juce::String Display::libraryTitle (Library l, int m) const
{
    if (m < kLibraries[(size_t) l].first || m > kLibraries[(size_t) l].last)
        return {};
    if (isLibraryPreset (l, m))
        return presetTitle (l, m);
    const auto file = libraryFile (l, m);
    return file != juce::File() ? file.getFileNameWithoutExtension().substring (4) : juce::String();
}

void Display::setLibraryChoice (Library l, int m)
{
    libraryChoice[(size_t) l] = juce::jlimit (kLibraries[(size_t) l].first, kLibraries[(size_t) l].last, m);
    refresh();
}

juce::String Display::patchText (Library l) const
{
    juce::StringArray v;
    if (l == Library::InputPatch)
    {
        for (const auto& s : ctx.engine.getChannels())
            v.add (juce::String (s->params.inputLeft.load()));
    }
    else
    {
        for (int port = 0; port < kNumPorts; ++port)
            v.add (juce::String (getOutputPatch (port)));
    }
    return v.joinIntoString (" ");
}

bool Display::applyPatchText (Library l, const juce::String& patch)
{
    juce::StringArray v;
    v.addTokens (patch, " \r\n", "");
    v.removeEmptyStrings();
    if (l == Library::InputPatch)
    {
        const auto& channels = ctx.engine.getChannels();
        if (v.isEmpty() || v.size() > (int) channels.size())
            return false;
        for (int i = 0; i < v.size(); ++i)
            channels[(size_t) i]->params.inputLeft = juce::jlimit (-1, ctx.engine.getNumSources() - 1, v[i].getIntValue());
        return true;
    }
    if (v.size() != kNumPorts)
        return false;
    // A port that keeps its signal is left alone (a Direct Out on it stays).
    for (int port = kPortOmni; port < kNumPorts; ++port)
        if (const int choice = v[port].getIntValue(); choice != getOutputPatch (port))
            setOutputPatch (port, choice);
    return true;
}

bool Display::storeLibrary (Library l, int m, const juce::String& title)
{
    const auto& spec = kLibraries[(size_t) l];
    if (m < spec.firstUser || m > spec.last)
        return false;
    auto name = juce::File::createLegalFileName (title.substring (0, 16).trim());
    if (name.isEmpty())
        name = "New Data";
    const auto folder = libraryFolderOf (l);
    if (! folder.createDirectory().wasOk())
        return false;
    const auto old = libraryFile (l, m);
    const auto file = folder.getChildFile (juce::String (m).paddedLeft ('0', 3) + " " + name + spec.extension);
    auto* s = selected();
    bool ok = false;
    if (l == Library::Effects)
        ok = ctx.show.saveFxPreset (fxUnit, file, name).wasOk();
    else if (l == Library::InputPatch || l == Library::OutputPatch)
        ok = file.replaceWithText (patchText (l));
    else if (s != nullptr && (l != Library::Gate || s->isInput()))
    {
        // A channel memory holds the channel but its name and patch; the others their own block.
        const juce::uint32 all = (juce::uint32) state::AllSections & ~((juce::uint32) state::Name | (juce::uint32) state::Routing);
        const juce::uint32 scope = l == Library::Channel ? all : l == Library::Eq ? (juce::uint32) state::Eq : l == Library::Gate ? (juce::uint32) state::Gate : (juce::uint32) state::Dynamics;
        ok = ctx.show.saveChannelPreset (state::StripRef::of (*s), scope, file, name).wasOk();
    }
    if (ok && old != juce::File() && old != file)
        old.deleteFile();
    libraryFiles[(size_t) l].scanned = false;
    refresh();
    return ok;
}

bool Display::recallLibrary (Library l, int m)
{
    const auto& spec = kLibraries[(size_t) l];
    if (m < spec.first || m > spec.last)
        return false;
    auto* s = selected();
    bool ok = false;
    if (m < spec.firstUser)
    {
        if (presetTitle (l, m).isEmpty())
            return false;
        if (l == Library::Effects)
            ok = ctx.engine.getFx().setModel (fxUnit, fxTypes()[m - 1]);
        else if (s != nullptr && (l == Library::Comp || s->isInput()))
        {
            // A dynamics preset: its type, on the type's own values.
            static const char* const gates[] = { "DMGAT", "DMDUK" };
            static const char* const comps[] = { "DMCMP", "DMEXP", "DMCPH", "DMCPS" };
            const bool gate = l == Library::Gate;
            const int model = models::findModel (models::Dynamics, gate ? gates[m - 1] : comps[m - 1]);
            if (model >= 0)
            {
                s->setModel (gate ? ModelBlock::Gate : ModelBlock::Dynamics, model);
                auto& slot = gate ? s->params.gate() : s->params.dynamics();
                const auto& type = models::model (models::Dynamics, model);
                for (size_t i = 0; i < type.params.size(); ++i)
                    slot.values[i] = type.params[i].defaultValue;
                ok = true;
            }
        }
    }
    else
    {
        const auto file = libraryFile (l, m);
        if (file == juce::File())
            return false;
        if (l == Library::Effects)
            ok = ctx.show.loadFxPreset (fxUnit, file).wasOk();
        else if (l == Library::InputPatch || l == Library::OutputPatch)
            ok = applyPatchText (l, file.loadFileAsString());
        else if (s != nullptr && (l != Library::Gate || s->isInput()))
            ok = ctx.show.loadChannelPreset (state::StripRef::of (*s), file).wasOk();
    }
    if (ok)
        rebuild();
    return ok;
}

bool Display::clearLibrary (Library l, int m)
{
    const auto file = libraryFile (l, m);
    if (isLibraryPreset (l, m) || file == juce::File())
        return false;
    const bool ok = file.deleteFile();
    libraryFiles[(size_t) l].scanned = false;
    refresh();
    return ok;
}

void Display::buildLibrary (int arg)
{
    const auto l = (Library) arg;
    const auto& spec = kLibraries[(size_t) l];
    auto& choice = libraryChoice[(size_t) l];
    choice = juce::jlimit (spec.first, spec.last, choice);

    // The list: the wheel chooses, the memory in the dotted box is the one the buttons work on.
    auto memoryBinding = bindInt (
        "MEMORY", [this, l] { return libraryChoice[(size_t) l]; }, [this, l] (int v) { libraryChoice[(size_t) l] = v; }, spec.first, spec.last, [] (int) { return juce::String(); });
    auto& row = add (Cell::Text, { 54, 193, 162, 11 }, std::move (memoryBinding), "MEMORY");
    row.dotted = true;

    const auto safe = juce::Component::SafePointer<Display> (this);
    button ({ 4, 124, 42, 19 }, {}, [this, l, safe]
    {
        const int m = libraryChoice[(size_t) l];
        if (isLibraryPreset (l, m) || libraryFile (l, m) == juce::File())
        {
            showMessage (isLibraryPreset (l, m) ? "This memory is read-only." : "No data.");
            return;
        }
        if (getParentComponent() == nullptr)
            return;
        askForText (*getParentComponent(), "TITLE EDIT", "Memory " + juce::String (m) + ":", libraryTitle (l, m), [safe, l, m] (const juce::String& title)
        {
            if (safe == nullptr)
                return;
            const auto file = safe->libraryFile (l, m);
            auto name = juce::File::createLegalFileName (title.substring (0, 16).trim());
            if (file == juce::File() || name.isEmpty())
                return;
            file.moveFileTo (file.getSiblingFile (juce::String (m).paddedLeft ('0', 3) + " " + name + file.getFileExtension()));
            safe->libraryFiles[(size_t) l].scanned = false;
            safe->refresh();
        });
    }).caption = "TITLE EDIT";
    button ({ 4, 150, 42, 13 }, "RECALL", [this, l]
    {
        const int m = libraryChoice[(size_t) l];
        const auto go = [this, l, m]
        {
            if (! recallLibrary (l, m))
                showMessage ("It can't be recalled to this channel.");
        };
        if (libraryTitle (l, m).isEmpty())
            showMessage ("No data.");
        else if (preference (Pref::RecallConfirmation))
            confirm ("Recall memory " + juce::String (m) + "?", go);
        else
            go();
    });
    button ({ 4, 174, 42, 13 }, "STORE", [this, l, safe]
    {
        const int m = libraryChoice[(size_t) l];
        if (isLibraryPreset (l, m))
        {
            showMessage ("This memory is read-only.");
            return;
        }
        const auto store = [safe, l, m] (const juce::String& title)
        {
            if (safe != nullptr && ! safe->storeLibrary (l, m, title))
                safe->showMessage ("Nothing to store from this channel.");
        };
        const auto current = libraryTitle (l, m).isNotEmpty() ? libraryTitle (l, m) : juce::String ("New Data");
        if (getParentComponent() != nullptr && preference (Pref::StoreConfirmation))
            askForText (*getParentComponent(), "TITLE EDIT", "Memory " + juce::String (m) + ":", current, [store] (const juce::String& title) { store (title); });
        else
            store (current);
    });
    button ({ 4, 198, 42, 13 }, "CLEAR", [this, l]
    {
        const int m = libraryChoice[(size_t) l];
        if (isLibraryPreset (l, m))
            showMessage ("This memory is read-only.");
        else if (libraryFile (l, m) == juce::File())
            showMessage ("No data.");
        else
            confirm ("Clear memory " + juce::String (m) + "?", [this, l, m] { clearLibrary (l, m); });
    });
}

void Display::drawLibrary (int arg)
{
    const auto l = (Library) arg;
    const auto& spec = kLibraries[(size_t) l];
    auto* s = selected();
    const bool ofChannel = l == Library::Channel || l == Library::Eq || l == Library::Gate || l == Library::Comp;
    const auto title = l == Library::Effects ? "EFFECT " + juce::String (fxUnit + 1) + " LIBRARY" : (ofChannel && l != Library::Channel ? idOf (s) + " " : juce::String()) + spec.name + " LIBRARY";
    pageTitle (title, ofChannel && s != nullptr ? s->getName() : juce::String());

    // What is there now.
    if (l == Library::Eq && s != nullptr)
    {
        text (8, 36, "CURRENT TYPE", true);
        const auto type = bindEq (*s, "type", {});
        box ({ 8, 46, 56, 13 });
        textCentred ({ 8, 46, 56, 13 }, lcdValue (type).substring (0, 9));
        textCentred ({ 96, 29, 128, 6 }, "CURRENT CURVE", true);
        eqCurve (*s, { 96, 37, 128, 74 });
        stripMeters (*s, { 264, 44, 52, 62 });
    }
    else if ((l == Library::Gate || l == Library::Comp) && s != nullptr)
    {
        if (l == Library::Gate && ! s->isInput())
            textCentred ({ 0, 60, kDotsW, 12 }, "The Output Channels have no gate.");
        else
        {
            auto& slot = l == Library::Gate ? s->params.gate() : s->params.dynamics();
            text (8, 36, "CURRENT TYPE", true);
            box ({ 8, 46, 84, 13 });
            textCentred ({ 8, 46, 84, 13 }, models::model (models::Dynamics, slot.model.load()).name.substring (0, 13));
            textCentred ({ 128, 29, 76, 6 }, "CURRENT CURVE", true);
            dynCurve (slot, { 128, 37, 76, 74 }, l == Library::Gate);
            stripMeters (*s, { 264, 44, 52, 62 });
        }
    }
    else if (l == Library::Channel && s != nullptr)
    {
        text (8, 36, "SEL CH", true);
        box ({ 8, 46, 60, 17 });
        textCentred ({ 8, 46, 60, 17 }, idOf (s));
        text (8, 72, "A memory holds the channels settings", true);
        text (8, 80, "but its name and its patches.", true);
        stripMeters (*s, { 264, 44, 52, 62 });
    }
    else if (l == Library::Effects)
    {
        const int model = ctx.engine.getFx().getModel (fxUnit);
        text (8, 36, "CURRENT TYPE", true);
        box ({ 8, 46, 100, 13 });
        textCentred ({ 8, 46, 100, 13 }, model > ld::kFxExt ? models::model (models::Fx, model).name.substring (0, 16) : juce::String ("No Effect"));
        text (8, 72, "The presets are the types Sidecar has; the 5.1 types", true);
        text (8, 80, "and the Add-On Effects are not among them.", true);
    }
    else if (l == Library::InputPatch)
        text (8, 40, "A memory holds the input patch of every Input Channel.", true);
    else if (l == Library::OutputPatch)
        text (8, 40, "A memory holds the slots' OMNI and 2TR outputs' patches.", true);

    // The list, the higher numbers above (as the console has it).
    text (56, 118, "No.  LIBRARY TITLE", true);
    box ({ 52, 126, 166, 94 });
    const int choice = libraryChoice[(size_t) l];
    for (int r = 0; r < 8; ++r)
    {
        const int m = choice + 6 - r, y = 129 + r * 11;
        if (m < spec.first || m > spec.last)
            continue;
        const auto name = libraryTitle (l, m);
        const bool preset = isLibraryPreset (l, m);
        text (56, y, juce::String (m).paddedLeft (' ', 3) + "." + (name.isNotEmpty() ? name.substring (0, 18) : preset ? juce::String ("---") : juce::String ("[   No Data!   ]")));
        if (preset)
            inverseText ({ 206, y - 1, 9, 9 }, "R", true);
    }
    for (int i = 0; i < 4; ++i)
    {
        vLine (48 + i, 198 - (3 - i), 198 + (3 - i));
        vLine (224 - i, 198 - (3 - i), 198 + (3 - i));
    }
    const bool preset = isLibraryPreset (l, choice);
    text (232, 130, preset ? "PRESET" : "USER MEMORY", true);
    text (232, 138, preset ? "READ ONLY" : libraryTitle (l, choice).isEmpty() ? juce::String ("EMPTY") : juce::String ("STORED"), true);
    text (232, 154, "USER MEMORIES", true);
    text (232, 162, juce::String (spec.firstUser) + " TO " + juce::String (spec.last), true);
    if (spec.firstUser > spec.first)
    {
        text (232, 178, "---: A PRESET", true);
        text (232, 186, "SIDECAR HASN'T", true);
    }
}

//==============================================================================
// OUTPUT PATCH (pp.79-81).

juce::String Display::outputChoiceName (int c)
{
    if (c >= 1 && c <= 8)
        return "BUS" + juce::String (c);
    if (c >= 9 && c <= 20)
        return "AUX" + juce::String (c - 8);
    if (c >= 21 && c <= 28)
        return "MTX" + juce::String ((c - 21) / 2 + 1) + ((c - 21) % 2 == 0 ? "L" : "R");
    if (c == 29 || c == 30)
        return c == 29 ? "ST L" : "ST R";
    return "-";
}

namespace
{
    /** The engine's source of an output choice (livedaw_dm2000's output_source): -1 none. */
    int choiceSource (int c)
    {
        if (c >= 1 && c <= 20)
            return MixerEngine::stripSource (StripKind::Bus, c - 1, 0);
        if (c >= 21 && c <= 28)
            return MixerEngine::stripSource (StripKind::Matrix, (c - 21) / 2, (c - 21) % 2);
        if (c == 29 || c == 30)
            return MixerEngine::stripSource (StripKind::Main, 0, c - 29);
        return -1;
    }
} // namespace

int Display::getOutputPatch (int port) const
{
    const int source = ctx.engine.getOutputSource (port);
    if (source >= 0)
        for (int c = 1; c < kNumOutputChoices; ++c)
            if (choiceSource (c) == source)
                return c;
    return 0;
}

void Display::setOutputPatch (int port, int choice)
{
    if (! juce::isPositiveAndBelow (port, kNumPorts))
        return;
    // Straight into the patch table, as the factory patch and the MIDI do: a signal can feed many ports.
    ctx.engine.clearOutput (port);
    int32_t count = 0;
    if (auto* table = ld::ld_engine_output_patch (ctx.engine.getHandle(), &count); table != nullptr && port < count)
        table[port] = choiceSource (juce::jlimit (0, kNumOutputChoices - 1, choice));
}

namespace
{
    int destinationPort (int destination)
    {
        if (destination >= 1 && destination <= 96)
            return Display::kPortSlot + destination - 1;
        if (destination >= 97 && destination <= 104)
            return Display::kPortOmni + destination - 97;
        if (destination >= 105 && destination <= 110)
            return Display::kPort2tr + destination - 105;
        return -1;
    }

    int portDestination (int port)
    {
        if (port >= Display::kPortSlot && port < Display::kPortSlot + 96)
            return 1 + port - Display::kPortSlot;
        if (port >= Display::kPortOmni && port < Display::kPortOmni + 8)
            return 97 + port - Display::kPortOmni;
        if (port >= Display::kPort2tr && port < Display::kPort2tr + 6)
            return 105 + port - Display::kPort2tr;
        return 0;
    }
} // namespace

juce::String Display::directDestinationName (int d)
{
    if (d >= 1 && d <= 96)
        return "S" + juce::String ((d - 1) / 16 + 1) + "-" + juce::String ((d - 1) % 16 + 1);
    if (d >= 97 && d <= 104)
        return "OMNI" + juce::String (d - 96);
    if (d >= 105 && d <= 110)
        return "2TD" + juce::String ((d - 105) / 2 + 1) + ((d - 105) % 2 == 0 ? "L" : "R");
    return "-";
}

int Display::directPort (int channel) const
{
    for (int port = kPortOmni; port < kNumPorts; ++port)
        if (const auto direct = DirectOut::fromCode (ctx.engine.getOutputSource (port)); direct.has_value() && direct->kind == StripKind::Channel && direct->number == channel + 1)
            return port;
    return -1;
}

int Display::getDirectOut (int channel) const
{
    if (! juce::isPositiveAndBelow (channel, 96))
        return 0;
    const int port = directPort (channel);
    return port >= 0 ? portDestination (port) : directDestination[(size_t) channel];
}

void Display::setDirectOut (int channel, int destination)
{
    if (! juce::isPositiveAndBelow (channel, 96))
        return;
    destination = juce::jlimit (0, kNumDirectDestinations - 1, destination);
    if (const int old = directPort (channel); old >= 0)
        ctx.engine.clearOutput (old);
    directDestination[(size_t) channel] = destination;
    // Auto Direct Out On (p.276): a destination turns the Direct Out on, none turns it off.
    if (const int port = destinationPort (destination); port >= 0)
        ctx.engine.routeOutputSource (port, DirectOut { StripKind::Channel, channel + 1, 0, directPost }.code());
}

bool Display::isDirectOn (int channel) const { return juce::isPositiveAndBelow (channel, 96) && directPort (channel) >= 0; }

void Display::setDirectOn (int channel, bool on)
{
    if (! juce::isPositiveAndBelow (channel, 96))
        return;
    const int port = directPort (channel);
    if (on && port < 0)
    {
        if (const int to = destinationPort (directDestination[(size_t) channel]); to >= 0)
            ctx.engine.routeOutputSource (to, DirectOut { StripKind::Channel, channel + 1, 0, directPost }.code());
    }
    else if (! on && port >= 0)
    {
        directDestination[(size_t) channel] = portDestination (port);
        ctx.engine.clearOutput (port);
    }
}

void Display::setDirectPost (bool post)
{
    directPost = post;
    for (int channel = 0; channel < 96; ++channel)
        if (const int port = directPort (channel); port >= 0)
            ctx.engine.routeOutputSource (port, DirectOut { StripKind::Channel, channel + 1, 0, post }.code());
}

// The slots' outputs (arg 0-2: two slots a page), OMNI OUT (3) and the 2TR digital outputs (4).
void Display::buildOutPatch (int arg)
{
    const auto field = [this] (int port, juce::Rectangle<int> r, const juce::String& caption)
    {
        auto b = bindInt (
            "PATCH", [this, port] { return getOutputPatch (port); },
            [this, port] (int v)
            {
                // A port a Direct Out is on can't be changed here (p.79).
                if (! DirectOut::isDirectOut (ctx.engine.getOutputSource (port)))
                    setOutputPatch (port, v);
            },
            0, kNumOutputChoices - 1, [] (int v) { return outputChoiceName (v); });
        b.text = [this, port]
        {
            if (const auto direct = DirectOut::fromCode (ctx.engine.getOutputSource (port)))
                return "CH" + juce::String (direct->number);
            return outputChoiceName (getOutputPatch (port));
        };
        auto& c = add (Cell::Text, r, std::move (b), caption);
        c.dotted = true;
    };
    if (arg <= 2)
    {
        for (int half = 0; half < 2; ++half)
            for (int i = 0; i < 16; ++i)
                field (kPortSlot + (arg * 2 + half) * 16 + i, { 16 + (i % 4) * 76, 48 + half * 96 + (i / 4) * 19, 44, 11 }, juce::String (i + 1));
    }
    else if (arg == 3)
    {
        for (int i = 0; i < 8; ++i)
            field (kPortOmni + i, { 32 + (i % 4) * 68, 84 + (i / 4) * 60, 48, 11 }, "OMNI" + juce::String (i + 1));
    }
    else
    {
        for (int i = 0; i < 6; ++i)
            field (kPort2tr + i, { 112 + (i % 2) * 56, 66 + (i / 2) * 44, 44, 11 }, i % 2 == 0 ? "L" : "R");
    }
}

void Display::drawOutPatch (int arg)
{
    const auto shown = juce::isPositiveAndBelow (focus, (int) cells.size()) ? lcdValue (cells[(size_t) focus].binding) : juce::String();
    pageTitle (arg <= 2 ? "SLOT" + juce::String (arg * 2 + 1) + "-" + juce::String (arg * 2 + 2) + " OUTPUT PATCH" : arg == 3 ? juce::String ("OMNI OUT PATCH") : juce::String ("2TR OUT DIGITAL PATCH"), shown);
    if (arg <= 2)
    {
        for (int half = 0; half < 2; ++half)
            frame ({ 6, 34 + half * 96, 308, 90 }, "SLOT" + juce::String (arg * 2 + half + 1));
        for (const auto& c : cells)
            text (c.area.getX() + 18, c.area.getY() - 7, c.caption, true);
    }
    else if (arg == 3)
    {
        for (const auto& c : cells)
            textCentred ({ c.area.getX(), c.area.getY() - 10, c.area.getWidth(), 8 }, c.caption, true);
    }
    else
    {
        for (int i = 0; i < 3; ++i)
            textCentred ({ 80, 48 + i * 44, 160, 6 }, "2TR OUT DIGITAL" + juce::String (i + 1), true);
        for (const auto& c : cells)
            textCentred ({ c.area.getX(), c.area.getY() - 8, c.area.getWidth(), 6 }, c.caption, true);
    }
}

// The Direct Out destinations of Input Channels 1-48 (arg 0) and 49-96 (p.81).
void Display::buildDirectOut (int arg)
{
    for (int i = 0; i < 48; ++i)
    {
        const int channel = arg * 48 + i;
        auto b = bindInt (
            "DIRECT", [this, channel] { return getDirectOut (channel); }, [this, channel] (int v) { setDirectOut (channel, v); }, 0, kNumDirectDestinations - 1,
            [] (int v) { return directDestinationName (v); });
        auto& c = add (Cell::Text, { 5 + (i % 8) * 39, 44 + (i / 8) * 26, 35, 11 }, std::move (b), juce::String (channel + 1));
        c.dotted = true;
    }
    for (int post = 0; post < 2; ++post)
    {
        KnobBinding b;
        b.get = [this, post] { return directPost == (post != 0) ? 1.0f : 0.0f; };
        b.set = [this, post] (float) { setDirectPost (post != 0); };
        b.steps = 2.0f;
        add (Cell::Toggle, { post == 0 ? 170 : 236, 204, post == 0 ? 60 : 66, 11 }, std::move (b), post == 0 ? "PRE FADER" : "POST FADER");
    }
}

void Display::drawDirectOut (int arg)
{
    const auto shown = juce::isPositiveAndBelow (focus, 48) ? "CH" + cells[(size_t) focus].caption + (isDirectOn (arg * 48 + focus) ? " ON" : " OFF") : juce::String();
    pageTitle ("CH" + juce::String (arg * 48 + 1) + "-" + juce::String (arg * 48 + 48) + " DIRECT OUT DESTINATION", shown);
    for (int i = 0; i < juce::jmin (48, (int) cells.size()); ++i)
    {
        const auto& c = cells[(size_t) i];
        text (c.area.getX() + 12, c.area.getY() - 7, c.caption, true);
        // A destination kept while the Direct Out is off: struck through.
        if (! isDirectOn (arg * 48 + i) && getDirectOut (arg * 48 + i) > 0)
            hLine (c.area.getX() + 2, c.area.getRight() - 3, c.area.getCentreY());
    }
    text (62, 206, "DIRECT OUT", true);
    // PRE EQ is on the console; Sidecar's direct outs are pre or post fader.
    dottedBox ({ 118, 204, 46, 11 });
    textCentred ({ 118, 204, 46, 11 }, "PRE EQ", true);
    text (6, 219, "Struck through: kept the Direct Out off ([DIRECT]). PRE EQ is not in Sidecar.", true);
}

//==============================================================================
// PAIR (pp.144-145).

std::pair<Strip*, Strip*> Display::pairStrips (bool output, int pair) const
{
    const auto& list = output ? ctx.engine.getBuses() : ctx.engine.getChannels();
    const int a = pair * 2;
    if (pair < 0 || pair >= (output ? 10 : 48) || a + 1 >= (int) list.size())
        return { nullptr, nullptr };
    return { list[(size_t) a].get(), list[(size_t) a + 1].get() };
}

bool Display::isPaired (bool output, int pair) const
{
    if (output)
        return juce::isPositiveAndBelow (pair, 10) && ((outputPairs >> pair) & 1u) != 0;
    return juce::isPositiveAndBelow (pair, 48) && ((inputPairs >> pair) & 1u) != 0;
}

void Display::savePairs() const
{
    ctx.settings.setValue (kPairsKey, juce::String::toHexString ((juce::int64) inputPairs) + " " + juce::String::toHexString ((int) outputPairs));
}

void Display::setPaired (bool output, int pair, bool on)
{
    const auto [a, b] = pairStrips (output, pair);
    if (a == nullptr || b == nullptr)
        return;
    if (output)
        outputPairs = on ? outputPairs | (1u << pair) : outputPairs & ~(1u << pair);
    else
        inputPairs = on ? inputPairs | (uint64_t (1) << pair) : inputPairs & ~(uint64_t (1) << pair);
    if (on)
    {
        // The first channel's settings go to the second (p.144).
        pairCopy (*a, *b);
        const size_t at = (size_t) ((output ? 48 : 0) + pair) * 2;
        pairSeen[at] = pairHash (*a);
        pairSeen[at + 1] = pairHash (*b);
    }
    savePairs();
    refresh();
}

// What a pair shares (p.144): fader, ON, solo, insert on, the AUX sends' level, on and pre/post, the
// gate, compressor and EQ, the comp's position, the fader and mute groups; a BUS pair's Matrix Sends and
// Bus to Stereo on and level. Not the patches, phase, delay, routing or pans; nor the attenuator, which
// the console copies once and then keeps apart.
uint64_t Display::pairHash (Strip& s) const
{
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&h] (uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    const auto level = [&mix] (float v)
    {
        uint32_t bits = 0;
        std::memcpy (&bits, &v, sizeof (bits));
        mix (bits);
    };
    const auto block = [&] (bool on, int model, const std::atomic<float>* values, models::Table table)
    {
        mix (on ? 1u : 0u);
        mix ((uint64_t) (uint32_t) model);
        const int n = (int) models::model (table, model).params.size();
        for (int i = 0; i < n; ++i)
            level (values[i].load());
    };
    auto& p = s.params;
    level (p.faderDb.load());
    mix (p.mute.load() ? 1u : 0u);
    mix (p.solo.load() ? 1u : 0u);
    mix (p.preInsert().on.load() ? 1u : 0u);
    mix ((uint64_t) (uint32_t) p.processOrder.load());
    block (p.eq().on.load(), p.eq().model.load(), p.eq().values, s.isOutput() ? models::BusEq : models::ChannelEq);
    block (p.dynamics().on.load(), p.dynamics().model.load(), p.dynamics().values, models::Dynamics);
    if (s.isInput())
    {
        block (p.gate().on.load(), p.gate().model.load(), p.gate().values, models::Dynamics);
        for (int a = 0; a < 12; ++a)
        {
            auto& send = s.sends[(size_t) (kAuxSlot + a)];
            level (send.levelDb.load());
            mix (send.enabled.load() ? 1u : 0u);
            mix ((uint64_t) (uint32_t) send.mode.load());
        }
    }
    else
    {
        for (int m = 0; m < 4; ++m)
        {
            auto& send = s.matrix[(size_t) m];
            level (send.levelDb.load());
            mix (send.on.load() ? 1u : 0u);
            mix (send.pre.load() ? 1u : 0u);
        }
        mix (s.mains[0].on.load() ? 1u : 0u);
        level (s.mains[0].levelDb.load());
    }
    for (int w = 0; w < ld::kGroupWords; ++w)
    {
        mix (p.dcaMask.words[w].load());
        mix (p.muteGroupMask.words[w].load());
    }
    return h;
}

void Display::pairCopy (Strip& from, Strip& to) const
{
    auto& s = from.params;
    auto& d = to.params;
    copyStripSections (ctx.engine, from, to, kCopyEq | kCopyDynamics | (from.isInput() ? (juce::uint32) kCopyGate : 0u));
    d.faderDb = s.faderDb.load();
    d.mute = s.mute.load();
    d.solo = s.solo.load();
    d.preInsert().on = s.preInsert().on.load();
    d.processOrder = s.processOrder.load();
    d.eq().on = s.eq().on.load();
    d.dynamics().on = s.dynamics().on.load();
    if (from.isInput())
    {
        d.gate().on = s.gate().on.load();
        for (int a = 0; a < 12; ++a)
        {
            auto& src = from.sends[(size_t) (kAuxSlot + a)];
            auto& dst = to.sends[(size_t) (kAuxSlot + a)];
            dst.levelDb = src.levelDb.load();
            dst.enabled = src.enabled.load();
            dst.mode = src.mode.load();
        }
    }
    else
    {
        for (int m = 0; m < 4; ++m)
        {
            auto& src = from.matrix[(size_t) m];
            auto& dst = to.matrix[(size_t) m];
            dst.levelDb = src.levelDb.load();
            dst.on = src.on.load();
            dst.pre = src.pre.load();
        }
        to.mains[0].on = from.mains[0].on.load();
        to.mains[0].levelDb = from.mains[0].levelDb.load();
    }
    d.dcaMask.store (s.dcaMask.load());
    d.muteGroupMask.store (s.muteGroupMask.load());
}

void Display::followPairs()
{
    if (inputPairs == 0 && outputPairs == 0)
        return;
    for (int i = 0; i < 58; ++i)
    {
        const bool output = i >= 48;
        if (! isPaired (output, output ? i - 48 : i))
            continue;
        const auto [a, b] = pairStrips (output, output ? i - 48 : i);
        if (a == nullptr || b == nullptr)
            continue;
        // The side that changed since the last look gives; the first one if both did.
        const size_t at = (size_t) i * 2;
        const auto ha = pairHash (*a), hb = pairHash (*b);
        if (ha != pairSeen[at])
            pairCopy (*a, *b);
        else if (hb != pairSeen[at + 1])
            pairCopy (*b, *a);
        else
            continue;
        pairSeen[at] = pairHash (*a);
        pairSeen[at + 1] = pairHash (*b);
    }
}

void Display::buildPair (int arg)
{
    const auto toggle = [this] (bool output, int pair, juce::Rectangle<int> r)
    {
        KnobBinding b;
        b.get = [this, output, pair] { return isPaired (output, pair) ? 1.0f : 0.0f; };
        b.set = [this, output, pair] (float v) { setPaired (output, pair, v >= 0.5f); };
        b.text = [this, output, pair] { return juce::String (isPaired (output, pair) ? "STEREO" : "MONOx2"); };
        b.steps = 2.0f;
        add (Cell::Toggle, r, std::move (b));
    };
    if (arg < 2)
    {
        for (int i = 0; i < 24; ++i)
            toggle (false, arg * 24 + i, { 12 + (i % 4) * 78, 45 + (i / 4) * 31, 42, 11 });
        return;
    }
    for (int i = 0; i < 4; ++i)
        toggle (true, i, { 24 + i * 70, 66, 42, 11 });
    for (int i = 0; i < 6; ++i)
        toggle (true, 4 + i, { 10 + i * 51, 138, 42, 11 });
}

void Display::drawPair (int arg)
{
    pageTitle (arg < 2 ? "INPUT CH" + juce::String (arg * 48 + 1) + "-" + juce::String (arg * 48 + 48) + " PAIR" : juce::String ("OUTPUT PAIR"));
    const auto numbers = [this] (const Cell& c, int first, bool on)
    {
        const int cx = c.area.getCentreX(), y = c.area.getY() - 9;
        const auto a = juce::String (first), b = juce::String (first + 1);
        text (cx - 8 - textWidth (a), y, a);
        heart (cx, y + 3, on);
        text (cx + 8, y, b);
    };
    if (arg < 2)
    {
        text (150, 19, "PAIR MODE", true);
        inverseText ({ 192, 16, 62, 10 }, "HORIZONTAL", true);
        dottedBox ({ 258, 16, 54, 10 });
        textCentred ({ 258, 16, 54, 10 }, "VERTICAL", true);
        for (int i = 0; i < juce::jmin (24, (int) cells.size()); ++i)
            numbers (cells[(size_t) i], arg * 48 + i * 2 + 1, isPaired (false, arg * 24 + i));
        text (6, 221, "Kept alike while this surface shows. No VERTICAL or MS here.", true);
        return;
    }
    frame ({ 6, 44, 308, 44 }, "BUS");
    frame ({ 6, 116, 308, 44 }, "AUX");
    for (int i = 0; i < juce::jmin (10, (int) cells.size()); ++i)
        numbers (cells[(size_t) i], i < 4 ? i * 2 + 1 : (i - 4) * 2 + 1, isPaired (true, i));
    text (6, 176, "A pair shares its fader ON EQ compressor groups and sends;", true);
    text (6, 184, "it is kept alike while this surface shows. No surround here.", true);
}

void Display::copyChannel()
{
    if (auto* s = selected())
    {
        copied = true;
        copiedKind = s->getKind();
        copiedNumber = s->getNumber();
        showMessage (idOf (s) + " copied.");
    }
}

bool Display::pasteChannel()
{
    auto* dst = selected();
    Strip* src = nullptr;
    if (copied)
        for (const auto* list : { &ctx.engine.getChannels(), &ctx.engine.getBuses(), &ctx.engine.getMatrices(), &ctx.engine.getMains() })
            for (const auto& s : *list)
                if (s->getKind() == copiedKind && s->getNumber() == copiedNumber)
                    src = s.get();
    // Among channels of the same type only (p.155).
    if (src == nullptr || dst == nullptr || src == dst || src->getKind() != dst->getKind())
    {
        showMessage (copied && src != dst ? "It can't be pasted to this channel." : "Nothing to paste.");
        return false;
    }
    const int bits = getCopyParameters();
    if ((bits & 1) != 0)
        copyStripSections (ctx.engine, *src, *dst, kCopyAll);
    else
    {
        auto& s = src->params;
        auto& d = dst->params;
        if ((bits & 2) != 0)
            d.faderDb = s.faderDb.load();
        if ((bits & 4) != 0)
            d.mute = s.mute.load();
        if ((bits & 8) != 0)
            d.pan = s.pan.load();
        if (src->isInput())
            for (int a = 0; a < 12; ++a)
            {
                if ((bits & 16) != 0)
                    dst->sends[(size_t) (kAuxSlot + a)].levelDb = src->sends[(size_t) (kAuxSlot + a)].levelDb.load();
                if ((bits & 32) != 0)
                    dst->sends[(size_t) (kAuxSlot + a)].enabled = src->sends[(size_t) (kAuxSlot + a)].enabled.load();
            }
        if ((bits & 64) != 0)
            copyStripSections (ctx.engine, *src, *dst, kCopyEq);
    }
    rebuild();
    showMessage (idOf (src) + " pasted to " + idOf (dst) + ".");
    return true;
}

//==============================================================================
// UTILITY: the oscillator (p.279) and the Channel Status Monitor (p.73); DIO's Word Clock Select (p.67).

void Display::applyOscillator()
{
    const auto* e = ctx.engine.getHandle();
    const auto gain = [this] (int i) { return oscAssign[(size_t) i] ? 1.0f : 0.0f; };
    for (int b = 0; b < 20; ++b)
        ld::ld_engine_console_osc_assign (e, (int) StripKind::Bus, b, gain (b), gain (b));
    for (int m = 0; m < 4; ++m)
        ld::ld_engine_console_osc_assign (e, (int) StripKind::Matrix, m, gain (20 + m * 2), gain (21 + m * 2));
    ld::ld_engine_console_osc_assign (e, (int) StripKind::Main, 0, gain (28), gain (29));
    static const float freqs[] = { 100.0f, 1000.0f, 10000.0f, 1000.0f, 1000.0f };
    ld::ld_engine_console_osc_set (e, oscOn, oscWave >= 3 ? ld::kOscPink : ld::kOscSine, freqs[oscWave], oscLevelDb, oscWave == 4, false);
}

void Display::setOscillator (bool on, int waveform, float levelDb)
{
    oscOn = on;
    oscWave = juce::jlimit (0, 4, waveform);
    oscLevelDb = juce::jlimit (-96.0f, 0.0f, levelDb);
    applyOscillator();
    refresh();
}

void Display::setOscillatorAssign (int index, bool on)
{
    if (juce::isPositiveAndBelow (index, (int) oscAssign.size()))
    {
        oscAssign[(size_t) index] = on;
        applyOscillator();
    }
}

namespace
{
    // The WAVEFORM buttons in the page's order; -1: 1kHz/400Hz, which Sidecar's oscillator hasn't.
    constexpr int kWaveRows[] = { 0, 1, 2, -1, 3, 4 };
    const char* const kWaveNames[] = { "SINE 100Hz", "SINE 1kHz", "SINE 10kHz", "1kHz/400Hz", "PINK NOISE", "BURST NOISE" };
} // namespace

void Display::buildOscillator (int)
{
    KnobBinding on;
    on.get = [this] { return oscOn ? 1.0f : 0.0f; };
    on.set = [this] (float v) { setOscillator (v >= 0.5f, oscWave, oscLevelDb); };
    on.text = [this] { return juce::String (oscOn ? "ON" : "OFF"); };
    on.steps = 2.0f;
    add (Cell::Toggle, { 26, 50, 44, 13 }, std::move (on));

    for (int row = 0; row < 6; ++row)
    {
        const int wave = kWaveRows[row];
        if (wave < 0)
            continue;
        KnobBinding b;
        b.get = [this, wave] { return oscWave == wave ? 1.0f : 0.0f; };
        b.set = [this, wave] (float) { setOscillator (oscOn, wave, oscLevelDb); };
        b.steps = 2.0f;
        add (Cell::Toggle, { 12, 86 + row * 15, 72, 11 }, std::move (b), kWaveNames[row]);
    }

    KnobBinding level;
    level.label = "LEVEL";
    level.get = [this] { return 1.0f + oscLevelDb / 96.0f; };
    level.set = [this] (float v) { setOscillator (oscOn, oscWave, (float) juce::roundToInt (96.0f * (juce::jlimit (0.0f, 1.0f, v) - 1.0f))); };
    level.text = [this] { return juce::String (juce::roundToInt (oscLevelDb)) + "dB"; };
    level.steps = 97.0f;
    auto& dialCell = add (Cell::Dial, { 132, 50, 40, 30 }, std::move (level), "LEVEL");
    dialCell.radius = 9;

    const auto assignTo = [this] (int index, juce::Rectangle<int> r, const juce::String& caption)
    {
        KnobBinding b;
        b.get = [this, index] { return oscAssign[(size_t) index] ? 1.0f : 0.0f; };
        b.set = [this, index] (float v) { setOscillatorAssign (index, v >= 0.5f); };
        b.steps = 2.0f;
        add (Cell::Toggle, r, std::move (b), caption);
    };
    for (int i = 0; i < 8; ++i)
        assignTo (i, { 142 + i * 20, 114, 16, 11 }, juce::String (i + 1));
    for (int i = 0; i < 12; ++i)
        assignTo (8 + i, { 142 + (i % 8) * 20, 130 + (i / 8) * 14, 16, 11 }, juce::String (i + 1));
    for (int i = 0; i < 8; ++i)
        assignTo (20 + i, { 142 + i * 20, 162, 16, 11 }, juce::String (i / 2 + 1) + (i % 2 == 0 ? "L" : "R"));
    assignTo (28, { 142, 178, 16, 11 }, "L");
    assignTo (29, { 162, 178, 16, 11 }, "R");
}

void Display::drawOscillator (int)
{
    pageTitle ("OSCILLATOR");
    frame ({ 6, 40, 84, 28 }, "OSCILLATOR ON");
    frame ({ 6, 78, 84, 104 }, "WAVEFORM");
    dottedBox ({ 12, 86 + 3 * 15, 72, 11 });
    textCentred ({ 12, 86 + 3 * 15, 72, 11 }, kWaveNames[3]);
    frame ({ 96, 40, 218, 54 }, "LEVEL");
    if (const auto& mains = ctx.engine.getMains(); ! mains.empty())
        stripMeters (*mains[0], { 250, 46, 52, 44 });
    frame ({ 96, 104, 218, 92 }, "ASSIGN");
    text (104, 116, "BUS", true);
    text (104, 132, "AUX", true);
    text (104, 164, "MATRIX", true);
    text (104, 180, "STEREO", true);
    text (6, 204, "1kHz/400Hz is not in Sidecars oscillator. An assigned bus carries its own", true);
    text (6, 212, "signal with the oscillators.", true);
}

void Display::buildChannelStatus (int)
{
    static const char* const names[] = { "SLOT1", "SLOT2", "SLOT3", "SLOT4", "SLOT5", "SLOT6", "2TR IN" };
    for (int i = 0; i < 7; ++i)
    {
        KnobBinding b;
        b.get = [this, i] { return statusSlot == i ? 1.0f : 0.0f; };
        b.set = [this, i] (float) { statusSlot = i; };
        b.steps = 2.0f;
        add (Cell::Toggle, { 10 + (i % 4) * 44, 40 + (i / 4) * 15, 40, 11 }, std::move (b), names[i]);
    }
}

void Display::drawChannelStatus (int)
{
    pageTitle ("CHANNEL STATUS MONITOR");
    const int rate = juce::roundToInt (ctx.engine.getSampleRate() / 1000.0);
    const auto fs = (rate == 0 ? juce::String ("48") : rate == 44 ? juce::String ("44.1") : rate == 88 ? juce::String ("88.2") : juce::String (rate)) + "kHz";
    static const char* const rows[] = { "FS", "EMPHASIS", "CATEGORY", "COPY" };
    hLine (10, 310, 96);
    for (int column = 0; column < 4; ++column)
    {
        const int x = 86 + column * 56;
        vLine (x - 4, 80, 168);
        textCentred ({ x, 80, 48, 6 }, statusSlot < 6 ? "SLOT" + juce::String (statusSlot + 1) : juce::String ("2TR IN"), true);
        textCentred ({ x, 87, 48, 6 }, statusSlot < 6 ? juce::String (column * 2 + 1) + "/" + juce::String (column * 2 + 2) : column < 3 ? "D" + juce::String (column + 1) : juce::String ("-"), true);
        for (int r = 0; r < 4; ++r)
            text (x + 46 - textWidth (r == 0 ? fs : juce::String ("---")), 104 + r * 17, r == 0 ? fs : juce::String ("---"));
    }
    for (int r = 0; r < 4; ++r)
    {
        text (76 - textWidth (rows[r], true), 105 + r * 17, rows[r], true);
        hLine (10, 310, 116 + r * 17);
    }
    text (6, 184, "Sidecars ports carry no channel status: the rate is the audio devices", true);
    text (6, 192, "the rest is not known.", true);
}

void Display::buildWordClock (int) {}

void Display::drawWordClock (int)
{
    pageTitle ("WORD CLOCK SELECT");
    const int rate = juce::roundToInt (ctx.engine.getSampleRate() / 1000.0);
    // The slots: sixteen in and out each on Sidecar, none of them a clock source.
    text (8, 34, "SLOT", true);
    text (50, 34, "IN", true);
    text (68, 34, "OUT", true);
    for (int pair = 0; pair < 8; ++pair)
        textCentred ({ 88 + pair * 28, 34, 24, 6 }, juce::String (pair * 2 + 1) + "/" + juce::String (pair * 2 + 2), true);
    hLine (4, 314, 42);
    for (int slot = 0; slot < 6; ++slot)
    {
        const int y = 46 + slot * 21;
        text (8, y + 4, "SLOT" + juce::String (slot + 1));
        text (50, y + 4, "16");
        text (68, y + 4, "16");
        for (int pair = 0; pair < 8; ++pair)
            dottedBox ({ 95 + pair * 28, y + 2, 11, 11 });
        dottedH (4, 314, y + 18);
    }
    // The sources along the bottom: the internal generator at the engine's rate is the one in use.
    box ({ 6, 178, 46, 30 });
    textCentred ({ 6, 181, 46, 8 }, "FS", true);
    textCentred ({ 6, 192, 46, 10 }, (rate == 0 ? juce::String ("48") : rate == 44 ? juce::String ("44.1") : rate == 88 ? juce::String ("88.2") : juce::String (rate)) + "kHz");
    static const char* const outer[] = { "WC IN", "CAS.IN", "2TRD1", "2TRD2", "2TRD3" };
    for (int i = 0; i < 5; ++i)
    {
        const int x = 60 + i * 51;
        dottedBox ({ x, 176, 11, 11 });
        text (x + 14, 179, outer[i], true);
    }
    static const std::pair<const char*, int> inner[] = { { "INT44.1k", 44 }, { "INT48k", 48 }, { "INT88.2k", 88 }, { "INT96k", 96 } };
    for (int i = 0; i < 4; ++i)
    {
        const int x = 60 + i * 64;
        const bool current = inner[i].second == (rate == 0 ? 48 : rate);
        box ({ x, 196, 11, 11 });
        if (current)
            fill ({ x + 2, 198, 7, 7 });
        text (x + 14, 199, inner[i].first, true);
    }
    text (6, 213, "For looking at: the clock is the audio devices. Dotted: cannot be chosen.", true);
}

//==============================================================================
// USER DEFINED KEYS (p.270) and the Remote pages (p.254).

const juce::StringArray& Display::userKeyNames()
{
    // The functions of the manual's list (pp.283-284) that Sidecar has something for, by their
    // display names.
    static const juce::StringArray names = []
    {
        juce::StringArray n { "No Assign", "Scene +1 Recall", "Scene -1 Recall" };
        for (int i = 1; i <= 16; ++i)
            n.add ("Scene " + juce::String (i) + " Recall");
        for (int i = 1; i <= 8; ++i)
            n.add ("Fx" + juce::String (i) + " Bypass");
        for (int i = 1; i <= 6; ++i)
            n.add ("GEQ" + juce::String (i) + " ON/OFF");
        n.add ("OSC. ON/OFF");
        for (int i = 0; i < 8; ++i)
            n.add ("In Mute Master " + juce::String::charToString ((juce::juce_wchar) ('I' + i)));
        for (int i = 0; i < 4; ++i)
            n.add ("Out Mute Master " + juce::String::charToString ((juce::juce_wchar) ('U' + i)));
        n.addArray ({ "CH Lib+1 Recall", "CH Lib-1 Recall", "Gate Lib+1 RCL.", "Gate Lib-1 RCL.", "Comp Lib+1 RCL.", "Comp Lib-1 RCL.", "EQ Lib+1 Recall", "EQ Lib-1 Recall" });
        return n;
    }();
    return names;
}

namespace
{
    /** The GEQ of an Output Channel (its EQ block on the DM2000's GEQ model), or nullptr. */
    ld::EqBlockParams* geqOf (Strip& s)
    {
        const int geq = models::findModel (models::BusEq, "DMGEQ");
        for (const auto& step : s.getChain())
            if (step.isBlock() && step.getBlockKind() == BlockKind::Eq)
                if (auto* eq = s.getBlock (step.uid).eq(); eq != nullptr && eq->model.load() == geq)
                    return eq;
        return nullptr;
    }
} // namespace

void Display::setUserKey (int key, int function)
{
    userKeys[(size_t) juce::jlimit (0, 15, key)] = juce::jlimit (0, kKeyCount - 1, function);
    juce::StringArray v;
    for (int f : userKeys)
        v.add (juce::String (f));
    ctx.settings.setValue (kUserKeysKey, v.joinIntoString (" "));
    refresh();
}

void Display::pressUserKey (int key)
{
    const int f = getUserKey (key);
    if (f == kKeyScenePlus || f == kKeySceneMinus)
    {
        setMemory (memory + (f == kKeyScenePlus ? 1 : -1));
        recallScene();
    }
    else if (f >= kKeyScene && f < kKeyFxBypass)
    {
        setMemory (f - kKeyScene + 1);
        recallScene();
    }
    else if (f >= kKeyFxBypass && f < kKeyGeq)
    {
        auto& bypass = ctx.engine.getFx().params (f - kKeyFxBypass).bypass;
        bypass = ! bypass.load();
    }
    else if (f >= kKeyGeq && f < kKeyOsc)
    {
        const auto strips = geqStrips();
        if (f - kKeyGeq < (int) strips.size())
            if (auto* eq = geqOf (*strips[(size_t) (f - kKeyGeq)]))
                eq->on = ! eq->on.load();
    }
    else if (f == kKeyOsc)
        setOscillator (! oscOn, oscWave, oscLevelDb);
    else if (f >= kKeyMuteMaster && f < kKeyLibrary)
    {
        // I-P are the Input Channels' mute groups (0-7), U-X the Output Channels' (8-11).
        auto& groups = ctx.engine.muteGroups;
        groups.set (f - kKeyMuteMaster, ! groups.contains (f - kKeyMuteMaster));
    }
    else if (f >= kKeyLibrary && f < kKeyCount)
    {
        static const Library libraries[] = { Library::Channel, Library::Gate, Library::Comp, Library::Eq };
        const auto l = libraries[(f - kKeyLibrary) / 2];
        setLibraryChoice (l, libraryChoice[(size_t) l] + ((f - kKeyLibrary) % 2 == 0 ? 1 : -1));
        if (! recallLibrary (l, libraryChoice[(size_t) l]))
            showMessage (juce::String (kLibraries[(size_t) l].name) + " library " + juce::String (libraryChoice[(size_t) l]) + ": no data.");
    }
    refresh();
}

bool Display::isUserKeyOn (int key) const
{
    const int f = getUserKey (key);
    if (f >= kKeyFxBypass && f < kKeyGeq)
        return ctx.engine.getFx().params (f - kKeyFxBypass).bypass.load();
    if (f >= kKeyGeq && f < kKeyOsc)
    {
        const auto strips = geqStrips();
        if (f - kKeyGeq < (int) strips.size())
            if (auto* eq = geqOf (*strips[(size_t) (f - kKeyGeq)]))
                return eq->on.load();
        return false;
    }
    if (f == kKeyOsc)
        return oscOn;
    if (f >= kKeyMuteMaster && f < kKeyLibrary)
        return ctx.engine.muteGroups.contains (f - kKeyMuteMaster);
    return false;
}

void Display::buildUserKeys (int)
{
    for (int k = 0; k < 16; ++k)
    {
        auto b = bindInt (
            "KEY", [this, k] { return getUserKey (k); }, [this, k] (int v) { setUserKey (k, v); }, 0, kKeyCount - 1, [] (int v) { return userKeyNames()[v]; });
        auto& c = add (Cell::Text, { 24 + (k / 8) * 152, 60 + (k % 8) * 19, 122, 11 }, std::move (b), juce::String (k + 1));
        c.dotted = true;
    }
    button ({ 228, 34, 66, 13 }, "INITIALIZE", [this]
    {
        for (int k = 0; k < 16; ++k)
            setUserKey (k, kKeyScene + k);
    });
}

void Display::drawUserKeys (int)
{
    pageTitle ("USER DEFINED KEY ASSIGN");
    text (8, 38, "One bank here; the console has A to H.", true);
    for (int k = 0; k < juce::jmin (16, (int) cells.size()); ++k)
    {
        const auto& c = cells[(size_t) k];
        text (c.area.getX() - 5 - textWidth (c.caption, true), c.area.getY() + 3, c.caption, true);
    }
    text (8, 216, "The functions Sidecar has: the wheel assigns at once.", true);
}

void Display::buildRemote (int) {}

void Display::drawRemote (int arg)
{
    pageTitle ("REMOTE" + juce::String (arg + 1));
    text (96, 38, "TARGET", true);
    dottedBox ({ 126, 34, 110, 13 });
    textCentred ({ 126, 34, 110, 13 }, "NO ASSIGN");
    frame ({ 6, 60, 308, 44 }, "ON");
    frame ({ 6, 112, 308, 36 }, "ENCODER");
    frame ({ 6, 156, 308, 36 }, "FADER");
    for (int y : { 78, 126, 170 })
    {
        text (12, y, "DATA", true);
        for (int i = 0; i < 8; ++i)
            dottedBox ({ 40 + i * 33, y - 3, 26, 11 });
    }
    text (6, 198, "Remote Layers (a DAW user defined MIDI Pro Tools) are not in Sidecar: nothing", true);
    text (6, 206, "here can be set and a REMOTE layers strips are empty. The transport keys", true);
    text (6, 214, "send MMC (STOP PLAY FF REW REC) on the consoles MIDI.", true);
}

} // namespace livedaw::ui::dm2k
