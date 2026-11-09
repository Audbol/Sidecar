#include "ui/DLiveC3500Surface.h"

#include "engine/MixerEngine.h"
#include "engine/Show.h"
#include "ui/HardwareLook.h"
#include "ui/MeterBank.h"

#include <cmath>

namespace livedaw::ui
{

using namespace universal;
using dl::look;

namespace
{
    constexpr int kTopBar = 40, kStripW = 66, kStripsY = 534, kStripH = 546;
    constexpr int kLeftScreenX = 140, kRightScreenX = 1026, kScreenY = 50, kScreenW = 580, kScreenH = 400;
    constexpr float kScreenScale = (float) kScreenW / (float) dl::kTouchW;

    int stripX (int i)
    {
        const int inBank = i % DLiveC3500Surface::kBankStrips;
        return (i < DLiveC3500Surface::kBankStrips ? 98 : 1002) + inBank * kStripW + (inBank / 4) * 4;
    }

    // The keys' colours, idle and lit (photographs of a C3500 and a C1500).
    const juce::Colour kPaflIdle (0xffdad0c7), kPale (0xffc9d9d2), kPaleLit (0xff17d57a), kSoft (0xffb9c6d4), kSoftLit (0xff8a35f0), kBlueIdle (0xff3a8fe0), kBlueLit (0xff0c4fe6),
        kMuteLit (0xfff2526c), kSelIdle (0xff2fa897), kSelLit (0xff36e0b2), kTeal (0xff5bb394), kPink (0xffeb8596), kRed (0xffdc5060), kAmberKey (0xffe0a040), kAmberLit (0xfff5b83a),
        kPaflLit (0xfff2a432), kLayerIdle (0xff7fb7c9), kOutline (0xff9fa5ad);

    dl::StripLayout stripLayout()
    {
        dl::StripLayout l;
        const int w = kStripW, mid = w / 2;
        l.level = { 19.0f, 4.0f, 12.0f, 46.0f };
        l.compMeter = { 35.0f, 12.0f, 10.0f, 26.0f };
        l.gateMeter = { 35.0f, 41.0f, 10.0f, 9.0f };
        l.lcd = { 1.0f, 55.0f, (float) w - 2.0f, 48.0f };
        l.rotary = { mid - 21, 107, 42, 42 };
        l.mute = { mid - 23, 156, 46, 26 };
        l.mutedY = 183.0f;
        l.sel = { mid - 22, 196, 44, 26 };
        l.mix = { mid - 18, 225, 36, 21 };
        l.pafl = { mid - 17, 249, 34, 26 };
        l.fader = { 0, 288, w, 246 };
        l.kindDot = 1.0f;
        l.nameDot = 1.45f;
        l.valueDot = 1.0f;
        l.kindY = 3.0f;
        l.nameY = 12.0f;
        l.barY = 25.0f;
        l.valueY = 31.0f;
        l.assignY = 40.0f;
        l.scale = true;
        l.screws = true;
        l.faderInset = (float) w * 0.5f;
        return l;
    }

    float peakDb (UiContext& ctx, const dl::Slot& slot)
    {
        auto* s = dl::stripOf (ctx.engine, slot);
        return s != nullptr ? juce::Decibels::gainToDecibels (ctx.meters.get (s, s->isInput() ? MeterPoint::Trim : MeterPoint::Output, 0), -100.0f) : -100.0f;
    }
} // namespace

/** The status page: what shows while no page key is lit. */
class DLiveC3500Surface::Status final : public juce::Component
{
public:
    explicit Status (UiContext& c) : ctx (c) {}
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0b1228));
        g.setColour (juce::Colours::white);
        g.setFont (type::font (30.0f));
        g.drawText ("System Status", 40, 30, 600, 40, juce::Justification::centredLeft);
        g.setFont (type::font (20.0f));
        const juce::StringArray lines {
            "Scenes: " + juce::String (ctx.show.getNumScenes()),
            "Inputs: " + juce::String ((int) ctx.engine.getChannels().size()),
            "Mixes: " + juce::String ((int) ctx.engine.getBuses().size()),
            "FX returns: " + juce::String ((int) ctx.engine.getAux().size()),
            "DCAs: " + juce::String (ctx.engine.getNumDcas()),
        };
        for (int i = 0; i < lines.size(); ++i)
            g.drawText (lines[i], 40, 96 + i * 38, 700, 32, juce::Justification::centredLeft);
    }

private:
    UiContext& ctx;
};

//==============================================================================
std::vector<dl::Slot> DLiveC3500Surface::defaultLayer (int b, int l)
{
    using S = dl::Slot;
    std::vector<S> slots ((size_t) kBankStrips);
    for (int i = 0; i < kBankStrips; ++i)
    {
        auto& s = slots[(size_t) i];
        if (b == 0)
            s = { S::Channel, l * kBankStrips + i };
        else
            switch (l)
            {
                case 0:  s = { S::Bus, i }; break;
                case 1:  s = { S::Bus, kBankStrips + i }; break;
                case 2:  s = { S::FxReturn, i }; break;
                case 3:  s = { S::Dca, i }; break;
                case 4:  s = { S::Dca, kBankStrips + i }; break;
                default: s = i < 8 ? S { S::Matrix, i } : S { S::Main, i - 8 }; break;
            }
    }
    return slots;
}

dl::Key& DLiveC3500Surface::key (const juce::String& name, juce::Colour idle, juce::Colour lit, juce::Rectangle<int> bounds, dl::Key::Shape shape, bool led)
{
    auto k = std::make_unique<dl::Key> (name, idle, lit, shape, led);
    k->setBounds (bounds);
    addAndMakeVisible (*k);
    keys.emplace_back (name, std::move (k));
    return *keys.back().second;
}

dl::Key* DLiveC3500Surface::findKey (const juce::String& name) const
{
    for (auto& [n, k] : keys)
        if (n == name)
            return k.get();
    return nullptr;
}

void DLiveC3500Surface::silk (float x, float y, float w, float h, const juce::String& text, float size, juce::Justification j, bool dim)
{
    silks.push_back ({ { x, y, w, h }, text, size, j, dim });
}

DLiveC3500Surface::DLiveC3500Surface (UiContext& c)
    : ctx (c), theme (Theme::dark()), universalKey ("UNIVERSAL MIXER", theme.accent, theme), c1500Key ("C1500 SURFACE", theme.accent, theme), processing (c, *this), scenes (c), mixRack (c, *this), mixer (c, theme),
      routingView (c, theme), fxRack (c, theme), monitoring (c, theme), library (c, theme), setup (c, theme), status (std::make_unique<Status> (c))
{
    using Shape = dl::Key::Shape;
    for (int b = 0; b < kBanks; ++b)
        for (int l = 0; l < kLayers; ++l)
            layouts[(size_t) b][(size_t) l] = defaultLayer (b, l);

    universalKey.onClick = [&context = ctx]
    {
        juce::MessageManager::callAsync ([&context]
        {
            if (context.setInterface)
                context.setInterface (InterfaceKind::Universal);
        });
    };
    addAndMakeVisible (universalKey);
    c1500Key.onClick = [&context = ctx] { dl::switchSurface (context, "c1500"); };
    addAndMakeVisible (c1500Key);

    // The two touchscreens.
    addAndMakeVisible (leftHost);
    addAndMakeVisible (rightHost);
    leftHost.addAndMakeVisible (processing);
    processing.setBounds (0, 0, dl::kTouchW, dl::kTouchH);
    for (auto* view : std::initializer_list<juce::Component*> { &scenes, &mixRack, &mixer, &routingView, &fxRack, &monitoring, &library, &setup, status.get() })
    {
        rightHost.addChildComponent (view);
        view->setBounds (0, 0, dl::kTouchW, dl::kTouchH);
    }
    scenes.setVisible (true);
    processing.onTouchTurn = [this] (const KnobBinding& b) { dataLeft->setBinding (b); };
    scenes.onTouchTurn = [this] (const KnobBinding& b) { dataRight->setBinding (b); };
    mixRack.onTouchTurn = [this] (const KnobBinding& b) { dataRight->setBinding (b); };
    processing.onSoftChanged = [this] { bindSoft(); };
    const auto selectPage = [this] (const juce::String& name, Page p)
    {
        litPageKey = name;
        showPage (p);
    };
    mixRack.onOpenSetup = [this, selectPage] { selectPage ("Setup", Page::Setup); };
    processing.onLibrary = [this, selectPage] { selectPage ("Lib", Page::Library); };
    processing.onFxUnit = [this, selectPage] { selectPage ("Page FX", Page::Fx); };

    // Left of the left screen.
    key ("Home", kPaflIdle, kPaleLit, { 66, 220, 54, 26 }, Shape::Oval, false).onClick = [this] { pressHome(); };
    silk (58.0f, 266.0f, 70.0f, 17.0f, "Insert", 15.0f);
    silk (58.0f, 294.0f, 70.0f, 11.0f, "Assigned", 9.0f, juce::Justification::centred, true);
    {
        auto& in = key ("In", kBlueIdle, kBlueLit, { 68, 308, 50, 26 }, Shape::Oval);
        const auto selected = [this] { return ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr; };
        in.isOn = [selected]
        {
            auto* s = selected();
            return s != nullptr && s->params.preInsert().on.load();
        };
        in.onClick = [selected]
        {
            if (auto* s = selected())
                s->params.preInsert().on = ! s->params.preInsert().on.load();
        };
    }
    const auto pageKey = [this, selectPage] (const juce::String& name, Page p, juce::Rectangle<int> r, juce::Colour idle)
    {
        auto& k = key (name, idle, kPaleLit, r);
        k.isOn = [this, name] { return litPageKey == name; };
        k.onClick = [this, name, p, selectPage]
        {
            if (litPageKey == name && p != Page::Status)
                selectPage ({}, Page::Status); // a lit key: none is, the status page
            else
                selectPage (name, p);
        };
        return &k;
    };
    pageKey ("Lib", Page::Library, { 66, 356, 54, 26 }, kBlueIdle);
    pageKey ("Setup", Page::Setup, { 66, 387, 54, 26 }, kBlueIdle);
    pageKey ("Listen", Page::Listen, { 66, 418, 54, 26 }, kBlueIdle);

    // The soft rotaries' three layer keys and two blocks.
    for (int i = 0; i < 3; ++i)
    {
        auto& k = key ("Layer " + juce::String (i + 1), kSoft, kPaleLit, { 748 + i * 54, 56, 46, 24 });
        k.setButtonText (juce::String (i + 1));
        k.isOn = [this, i] { return softLayer == i; };
        k.onClick = [this, i]
        {
            softLayer = i;
            processing.setLayer (i);
        };
    }
    for (int block = 0; block < 2; ++block)
    {
        const int y = 84 + block * 148;
        const std::array<juce::Point<int>, 3> at { juce::Point<int> { 772, y + 28 }, juce::Point<int> { 860, y + 68 }, juce::Point<int> { 772, y + 112 } };
        for (int i = 0; i < 3; ++i)
        {
            auto r = std::make_unique<dl::Rotary> (juce::Colour (0xff202226));
            r->setBounds (juce::Rectangle<int> (44, 44).withCentre (at[(size_t) i]));
            addAndMakeVisible (*r);
            soft[(size_t) (block * 3 + i)] = std::move (r);
        }
        silk (838.0f, (float) y + 10.0f, 44.0f, 12.0f, "Pk!", 9.5f, juce::Justification::centred, true);
        auto& in = key ("Soft In " + juce::String (block + 1), kSelIdle, kPaleLit, { 842, y + 106, 50, 26 }, Shape::Oval);
        in.setButtonText ({});
        in.isOn = [this, block]
        {
            const auto* s = processing.getBlock (block).in;
            return s != nullptr && s->load();
        };
        in.onClick = [this, block]
        {
            if (auto* s = processing.getBlock (block).in)
                *s = ! s->load();
        };
    }
    key ("View L", kPale, kPaleLit, { 758, 388, 56, 26 }).setButtonText ("View");
    findKey ("View L")->isOn = [this] { return processing.isViewAlt(); };
    findKey ("View L")->onClick = [this] { processing.toggleView(); };
    key ("Help L", kPale, kPaleLit, { 836, 388, 56, 26 }).setButtonText ("Help");
    dataLeft = std::make_unique<dl::Rotary> (juce::Colour (0xfff5b83a));
    dataLeft->alwaysLit = true;
    dataLeft->setBounds (juce::Rectangle<int> (66, 66).withCentre ({ 815, 462 }));
    addAndMakeVisible (*dataLeft);

    // Under the left screen: the PEQ.
    silk (58.0f, 462.0f, 64.0f, 18.0f, "PEQ", 15.0f);
    silk (58.0f, 481.0f, 64.0f, 11.0f, "Pk!", 9.0f, juce::Justification::centred, true);
    {
        auto& in = key ("PEQ In", kSelIdle, kPaleLit, { 66, 498, 48, 26 }, Shape::Oval);
        in.setButtonText ("In");
        const auto selected = [this] { return ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr; };
        in.isOn = [selected]
        {
            auto* s = selected();
            return s != nullptr && s->params.eq().on.load();
        };
        in.onClick = [selected]
        {
            if (auto* s = selected())
                s->params.eq().on = ! s->params.eq().on.load();
        };
        auto& width = key ("Width", kAmberKey, kAmberLit, { 134, 476, 38, 38 }, Shape::Oval, false);
        width.isOn = [this] { return widthMode; };
        width.onClick = [this]
        {
            widthMode = ! widthMode;
            bindPeq();
        };
    }
    for (int band = 0; band < 4; ++band)
    {
        const int x = 182 + band * 136;
        silk ((float) x + 4.0f, 490.0f, 34.0f, 12.0f, "Gain", 9.5f);
        silk ((float) x + 74.0f, 460.0f, 40.0f, 12.0f, "Freq", 9.5f);
        auto gain = std::make_unique<dl::Rotary> (juce::Colour (0xffff3030));
        auto freq = std::make_unique<dl::Rotary> (juce::Colour (0xff30e060));
        gain->setBounds (x + 38, 486, 40, 40);
        freq->setBounds (x + 76, 472, 40, 40);
        addAndMakeVisible (*gain);
        addAndMakeVisible (*freq);
        peq[(size_t) band * 2] = std::move (gain);
        peq[(size_t) band * 2 + 1] = std::move (freq);
    }

    // Right of the right screen: its pages.
    const std::tuple<const char*, Page, const char*> pages[] = { { "Meters", Page::Meters, "Meters" }, { "FX", Page::Fx, "FX" }, { "I/O", Page::Io, "I/O" }, { "Scenes", Page::Scenes, "Scenes" },
                                                                 { "Ganging", Page::Ganging, "Ganging" }, { "MixRack", Page::MixRack, "MixRack" }, { "Surface", Page::Surface, "Surface" },
                                                                 { "Util/Shows", Page::Util, "Util/Shows" }, { "Routing", Page::Routing, "Routing" } };
    int row = 0;
    for (const auto& [name, p, label] : pages)
    {
        const int y = 50 + row * 38;
        silk (1636.0f, (float) y, 70.0f, 12.0f, label, 10.0f);
        auto* k = pageKey (juce::String ("Page ") + name, p, { 1644, y + 12, 54, 24 }, row == 8 ? juce::Colour (0xff1ea898) : kPale);
        k->setButtonText ({});
        ++row;
    }

    // PAFL's meters and Clear; Assign and Talk.
    silk (1716.0f, 54.0f, 68.0f, 11.0f, "L     R     M", 9.5f);
    silk (1716.0f, 224.0f, 68.0f, 11.0f, "PAFL", 9.5f);
    {
        auto& clear = key ("Clear", kPaflIdle, kPaflLit, { 1726, 236, 48, 24 }, Shape::Oval);
        clear.onClick = [this] { clearSolos(); };
        clear.isOn = [this]
        {
            for (const auto* list : { &ctx.engine.getChannels(), &ctx.engine.getAux(), &ctx.engine.getBuses(), &ctx.engine.getMains(), &ctx.engine.getMatrices() })
                for (const auto& s : *list)
                    if (s->params.solo.load())
                        return true;
            return false;
        };
    }
    {
        auto& assign = key ("Assign Talk", kBlueIdle, kBlueLit, { 1726, 278, 48, 26 }, Shape::Oval);
        assign.setButtonText ("Assign");
        silk (1716.0f, 305.0f, 68.0f, 11.0f, "+Mix", 9.5f, juce::Justification::centred, true);
        silk (1716.0f, 330.0f, 68.0f, 11.0f, "Talk Active", 9.5f);
        auto& talk = key ("Talk", kPale, kPaleLit, { 1722, 348, 56, 32 });
        talk.isOn = [this] { return ctx.engine.isTalkOn (0); };
        talk.onClick = [this] { ctx.engine.talk.paths[0].on = ! ctx.engine.talk.paths[0].on.load(); };
    }

    // The right screen's View, Help and data rotary; the scene keys.
    key ("View R", kPale, kPaleLit, { 1656, 404, 54, 26 }).setButtonText ("View");
    key ("Help R", kPale, kPaleLit, { 1738, 404, 54, 26 }).setButtonText ("Help");
    dataRight = std::make_unique<dl::Rotary> (juce::Colour (0xfff5b83a));
    dataRight->alwaysLit = true;
    dataRight->setBounds (juce::Rectangle<int> (62, 62).withCentre ({ 1724, 480 }));
    addAndMakeVisible (*dataRight);

    silk (1560.0f, 460.0f, 74.0f, 16.0f, "Scene", 13.0f);
    silk (1544.0f, 512.0f, 106.0f, 12.0f, "Reset for Undo", 8.5f, juce::Justification::centred, true);
    key ("Prev", kBlueIdle, kBlueLit, { 1486, 462, 48, 26 }, Shape::Oval, false).onClick = [this] { scenes.stepNext (-1); };
    key ("Next", kBlueIdle, kBlueLit, { 1486, 498, 48, 26 }, Shape::Oval, false).onClick = [this] { scenes.stepNext (1); };
    key ("GO", kSelIdle, kSelLit, { 1566, 478, 62, 32 }, Shape::Oval, false).onClick = [this] { scenes.goNext(); };

    // The phones and the SoftKeys (not assignable yet).
    phones = std::make_unique<dl::Rotary> (juce::Colour (0xffe0e0e0));
    phones->setBinding (bindFader ("Phones", ctx.engine.phones.levelDb));
    phones->setBounds (1830, 134, 42, 42);
    addAndMakeVisible (*phones);
    silk (1810.0f, 178.0f, 82.0f, 12.0f, "Phones", 10.0f);
    const auto softKey = [this] (int n, juce::Rectangle<int> r) { key ("Soft " + juce::String (n), kSoft, kSoftLit, r).setButtonText (juce::String (n)); };
    for (int i = 0; i < 10; ++i)
        softKey (i + 1, { 1826, 198 + i * 33, 58, 27 });
    for (int i = 0; i < 6; ++i)
        softKey (11 + i, { 1830, 542 + i * 30, 58, 26 });
    for (int i = 0; i < 3; ++i)
        softKey (17 + i, { 1830, 966 + i * 36, 58, 30 });

    // The strips.
    const auto layout = stripLayout();
    for (int i = 0; i < kStrips; ++i)
    {
        strips[(size_t) i] = std::make_unique<dl::StripUnit> (*this, ctx, theme, layout, i, kStrips);
        strips[(size_t) i]->setBounds (stripX (i), kStripsY, kStripW, kStripH);
        addAndMakeVisible (*strips[(size_t) i]);
    }

    // Left of the strips: the LCDs' function; Pre, Assign; Copy, Paste, Reset.
    {
        const std::pair<const char*, dl::DisplayFunction> displayKeys[] = { { "Name", dl::DisplayFunction::Name }, { "dB", dl::DisplayFunction::Db }, { "I/O", dl::DisplayFunction::Io }, { "Chan", dl::DisplayFunction::Chan } };
        int r = 0;
        for (auto [name, f] : displayKeys)
        {
            auto& k = key (name, kPale, kPaleLit, { 22, 544 + r * 31, 58, 26 });
            k.isOn = [this, target = f] { return displayFunction == target; };
            k.onClick = [this, target = f] { setDisplayFunction (target); };
            ++r;
        }
        silks.push_back ({ { 14.0f, 668.0f, 74.0f, 26.0f }, "Display\nFunction", 9.5f, juce::Justification::centred, false });
    }
    const auto latchKey = [this] (const juce::String& name, Latch l, juce::Colour idle, juce::Colour lit, juce::Rectangle<int> r)
    {
        auto& k = key (name, idle, lit, r, Shape::Oval);
        k.isOn = [this, l] { return getLatch() == l; };
        k.onClick = [this, l] { toggleLatch (l); };
        return &k;
    };
    latchKey ("Pre L", Latch::Pre, kSelIdle, kSelLit, { 22, 722, 46, 24 })->setButtonText ("Pre");
    silk (66.0f, 728.0f, 30.0f, 11.0f, "+Sel", 8.5f, juce::Justification::centredLeft, true);
    latchKey ("Assign L", Latch::Assign, kBlueIdle, kBlueLit, { 22, 750, 46, 24 })->setButtonText ("Assign");
    silk (66.0f, 756.0f, 30.0f, 11.0f, "+Mix", 8.5f, juce::Justification::centredLeft, true);
    latchKey ("Copy", Latch::Copy, kTeal, kPaleLit, { 24, 860, 50, 26 });
    latchKey ("Paste", Latch::Paste, kPink, kMuteLit, { 24, 900, 50, 26 });
    latchKey ("Reset", Latch::Reset, kRed, kMuteLit, { 24, 940, 50, 26 });

    // Between the banks: the rotaries' function; Pre, Assign; Bank 1; Safes, Freeze, GEQ.
    {
        const std::pair<const char*, dl::RotaryFunction> rotaryKeys[] = { { "Gain", dl::RotaryFunction::Gain }, { "Pan", dl::RotaryFunction::Pan }, { "1", dl::RotaryFunction::Custom1 }, { "2", dl::RotaryFunction::Custom2 },
                                                                          { "3", dl::RotaryFunction::Custom3 }, { "4", dl::RotaryFunction::Custom4 }, { "Sends", dl::RotaryFunction::Sends } };
        int r = 0;
        for (auto [name, f] : rotaryKeys)
        {
            const bool custom = r >= 2;
            auto& k = key (juce::String ("Rotary ") + name, custom ? kBlueIdle : kPale, f == dl::RotaryFunction::Sends ? kBlueLit : kMuteLit, { 924, 540 + r * 25, 52, 22 });
            k.setButtonText (name);
            k.isOn = [this, target = f] { return rotaryFunction == target; };
            k.onClick = [this, target = f] { setRotaryFunction (target); };
            ++r;
        }
    }
    latchKey ("Pre", Latch::Pre, kSelIdle, kSelLit, { 918, 722, 46, 24 });
    silk (964.0f, 728.0f, 30.0f, 11.0f, "+Sel", 8.5f, juce::Justification::centredLeft, true);
    latchKey ("Assign", Latch::Assign, kBlueIdle, kBlueLit, { 918, 750, 46, 24 });
    silk (964.0f, 756.0f, 30.0f, 11.0f, "+Mix", 8.5f, juce::Justification::centredLeft, true);
    latchKey ("Safes", Latch::Safes, kBlueIdle, kBlueLit, { 922, 968, 48, 24 });
    silk (970.0f, 974.0f, 28.0f, 11.0f, "+Mix", 8.5f, juce::Justification::centredLeft, true);
    latchKey ("Freeze", Latch::Freeze, kBlueIdle, kBlueLit, { 922, 998, 48, 24 });
    silk (970.0f, 1004.0f, 28.0f, 11.0f, "+Mix", 8.5f, juce::Justification::centredLeft, true);
    {
        auto& geq = key ("GEQ", kTeal, kPaleLit, { 922, 1028, 48, 24 }, Shape::Oval);
        geq.isOn = [this] { return getGeq() != nullptr; };
        geq.onClick = [this]
        {
            pressGeq (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr, kStrips);
            bindStrips();
        };
    }

    // The two banks' layer keys.
    for (int b = 0; b < kBanks; ++b)
    {
        const int x = b == 0 ? 922 : 1830;
        silk ((float) x - 8.0f, 780.0f, 70.0f, 13.0f, "Bank " + juce::String (b + 1), 11.0f);
        for (int l = 0; l < kLayers; ++l)
        {
            const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + l));
            auto& k = key ("Bank" + juce::String (b + 1) + " " + letter, kLayerIdle, kPaleLit, { x, 796 + l * 28, 46, 24 });
            k.setButtonText (letter);
            k.isOn = [this, b, l] { return layers[(size_t) b] == l; };
            k.onClick = [this, b, l] { setLayer (b, l); };
            silk ((float) x + 50.0f, 802.0f + (float) l * 28.0f, 24.0f, 11.0f, "Pk!", 8.5f, juce::Justification::centredLeft, true);
        }
    }

    bindStrips();
    bindPeq();
    bindSoft();
    startTimerHz (30);
}

DLiveC3500Surface::~DLiveC3500Surface() = default;

void DLiveC3500Surface::bindStrips()
{
    for (int i = 0; i < kStrips; ++i)
    {
        const int bank = i / kBankStrips;
        auto& unit = *strips[(size_t) i];
        if (unit.getSlot().kind == dl::Slot::None || ! isFrozen (unit.getSlot()))
            unit.setSlot (layouts[(size_t) bank][(size_t) layers[(size_t) bank]][(size_t) (i % kBankStrips)]);
        else
            unit.rebind();
    }
}

void DLiveC3500Surface::bindPeq()
{
    auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    boundStrip = s;
    const char* bands[] = { "l", "1", "2", "h" };
    for (int b = 0; b < 4; ++b)
    {
        const juce::String pre (bands[b]);
        peq[(size_t) b * 2]->setBinding (s != nullptr ? dl::bindEq (*s, pre + "g", "Gain") : KnobBinding {});
        // Width lit: the Freq rotaries set the bands' widths.
        peq[(size_t) b * 2 + 1]->setBinding (s != nullptr ? dl::bindEq (*s, pre + (widthMode ? "q" : "f"), widthMode ? "Width" : "Freq") : KnobBinding {});
    }
}

void DLiveC3500Surface::bindSoft()
{
    for (int b = 0; b < 2; ++b)
    {
        const auto& block = processing.getBlock (b);
        for (int i = 0; i < 3; ++i)
        {
            const auto& c = block.controls[(size_t) i];
            soft[(size_t) (b * 3 + i)]->setBinding (c.binding);
            soft[(size_t) (b * 3 + i)]->setLight (c.colour);
        }
    }
}

void DLiveC3500Surface::clearSolos()
{
    for (const auto* list : { &ctx.engine.getChannels(), &ctx.engine.getAux(), &ctx.engine.getBuses(), &ctx.engine.getMains(), &ctx.engine.getMatrices() })
        for (const auto& s : *list)
            s->params.solo = false;
}

void DLiveC3500Surface::setLayer (int bank, int layer)
{
    layers[(size_t) bank] = juce::jlimit (0, kLayers - 1, layer);
    bindStrips();
}

void DLiveC3500Surface::setRotaryFunction (dl::RotaryFunction f)
{
    rotaryFunction = f;
    bindStrips();
}

void DLiveC3500Surface::setMixOnFaders (int bus)
{
    mixBus = bus;
    bindStrips();
    processing.mixChanged();
}

void DLiveC3500Surface::pressHome()
{
    layers = { 0, 0 };
    mixBus = -1;
    rotaryFunction = dl::RotaryFunction::Gain;
    displayFunction = dl::DisplayFunction::Name;
    setLatch (Latch::None);
    endGeq();
    bindStrips();
    processing.mixChanged();
    processing.showTab (dl::ProcessingScreen::Tab::Peq);
    litPageKey = "Page Scenes";
    showPage (Page::Scenes);
}

void DLiveC3500Surface::showPage (Page p)
{
    page = p;
    scenes.setVisible (p == Page::Scenes);
    mixer.setVisible (p == Page::Meters);
    fxRack.setVisible (p == Page::Fx);
    routingView.setVisible (p == Page::Io || p == Page::Routing);
    library.setVisible (p == Page::Util || p == Page::Library);
    mixRack.setVisible (p == Page::MixRack);
    setup.setVisible (p == Page::Ganging || p == Page::Surface || p == Page::Setup);
    monitoring.setVisible (p == Page::Listen);
    status->setVisible (p == Page::Status);
    switch (p)
    {
        case Page::Meters:  mixer.refresh(); break;
        case Page::Fx:      fxRack.refresh(); break;
        case Page::Io:
        case Page::Routing: routingView.refresh(); break;
        case Page::Scenes:  scenes.refresh(); break;
        case Page::Util:
        case Page::Library: library.refresh(); break;
        case Page::MixRack: mixRack.refresh(); break;
        case Page::Ganging:
        case Page::Surface:
        case Page::Setup:   setup.refresh(); break;
        case Page::Listen:  monitoring.refresh(); break;
        case Page::Status:  status->repaint(); break;
    }
}

void DLiveC3500Surface::refreshAll()
{
    mixer.rebuild();
    selectionChanged();
    processing.refreshAll();
    bindStrips();
    bindPeq();
    repaint();
}

void DLiveC3500Surface::selectionChanged()
{
    mixer.selectionChanged();
    auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    if (s != processing.getStrip())
    {
        processing.setStrip (s);
        scenes.setStrip (s);
        mixRack.setStrip (s);
    }
    bindPeq();
}

void DLiveC3500Surface::showChanged()
{
    scenes.refresh();
}

void DLiveC3500Surface::timerCallback()
{
    for (auto& s : strips)
        s->refresh();
    for (auto& [name, k] : keys)
        k->refresh();
    for (auto& r : peq)
        r->refresh();
    for (auto& r : soft)
        r->refresh();
    dataLeft->refresh();
    dataRight->refresh();
    phones->refresh();
    universalKey.refresh();
    c1500Key.refresh();
    if ((ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr) != boundStrip)
        selectionChanged();
    // PAFL's meters, the Assigned light, the banks' peak lights.
    repaint (juce::Rectangle<float> (1712.0f, 60.0f, 76.0f, 170.0f).transformedBy (design).getSmallestIntegerContainer());
    repaint (juce::Rectangle<float> (60.0f, 280.0f, 70.0f, 14.0f).transformedBy (design).getSmallestIntegerContainer());
    repaint (juce::Rectangle<float> (970.0f, 796.0f, 12.0f, 170.0f).transformedBy (design).getSmallestIntegerContainer());
    repaint (juce::Rectangle<float> (1878.0f, 796.0f, 12.0f, 170.0f).transformedBy (design).getSmallestIntegerContainer());
}

void DLiveC3500Surface::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0b0b0c));
    g.addTransform (design);
    const auto& l = look();

    auto top = juce::Rectangle<int> (kDesignWidth, kTopBar);
    g.setColour (theme.panel);
    g.fillRect (top);
    g.setColour (theme.text);
    g.setFont (type::font (type::kTitle, true));
    g.drawText ("dLive C3500", top.reduced (sizes::kGapLarge, 0), juce::Justification::centredLeft);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption));
    g.drawText ("Not affiliated with or endorsed by Allen & Heath; dLive is their trademark, used only to say what is emulated.",
                top.reduced (sizes::kGapLarge, 0).withTrimmedLeft (170).withTrimmedRight (390), juce::Justification::centredLeft);

    // The upper panel, nearly black; the surface under it.
    const juce::Rectangle<float> upper (0.0f, (float) kTopBar, (float) kDesignWidth, (float) (kStripsY - kTopBar));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff191a1e), 0.0f, upper.getY(), juce::Colour (0xff24262b), 0.0f, upper.getBottom(), false));
    g.fillRect (upper);
    hw::panel (g, juce::Rectangle<float> (0.0f, upper.getBottom(), (float) kDesignWidth, (float) kDesignHeight - upper.getBottom()), l);
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.fillRect (0.0f, upper.getBottom(), (float) kDesignWidth, 1.5f);

    // The screens' silver bezels.
    for (int x : { kLeftScreenX, kRightScreenX })
    {
        const juce::Rectangle<float> bezel ((float) x - 5.0f, (float) kScreenY - 5.0f, (float) kScreenW + 10.0f, (float) kScreenH + 10.0f);
        g.setColour (juce::Colour (0xffb9bdc3));
        g.fillRoundedRectangle (bezel, 4.0f);
        g.setColour (juce::Colours::black);
        g.fillRect (bezel.reduced (3.0f));
    }

    const auto outline = [&g] (juce::Rectangle<float> r, juce::Colour c = kOutline, float alpha = 0.55f)
    {
        g.setColour (c.withAlpha (alpha));
        g.drawRoundedRectangle (r, 8.0f, 1.2f);
    };
    outline ({ 58.0f, 212.0f, 70.0f, 42.0f });
    outline ({ 58.0f, 260.0f, 70.0f, 80.0f });
    outline ({ 58.0f, 350.0f, 70.0f, 100.0f });
    outline ({ 742.0f, 50.0f, 164.0f, 34.0f });
    outline ({ 738.0f, 84.0f, 172.0f, 144.0f });
    outline ({ 738.0f, 232.0f, 172.0f, 144.0f });
    outline ({ 1636.0f, 48.0f, 70.0f, 348.0f });
    outline ({ 1712.0f, 50.0f, 76.0f, 216.0f });
    outline ({ 1712.0f, 272.0f, 76.0f, 118.0f });
    outline ({ 58.0f, 456.0f, 66.0f, 74.0f });
    outline ({ 130.0f, 472.0f, 46.0f, 46.0f });
    for (int band = 0; band < 4; ++band)
        outline ({ 180.0f + (float) band * 136.0f, 456.0f, 130.0f, 74.0f });
    outline ({ 1478.0f, 456.0f, 158.0f, 74.0f });
    outline ({ 14.0f, 540.0f, 74.0f, 156.0f });
    outline ({ 916.0f, 536.0f, 66.0f, 182.0f });
    outline ({ 912.0f, 776.0f, 90.0f, 186.0f });
    outline ({ 1820.0f, 776.0f, 90.0f, 186.0f });

    // SoftKeys' brackets: 1-4 and 9-10 in red, 5-8 white (and likewise below).
    {
        const auto bracket = [&g] (float x, float y0, float y1, juce::Colour c)
        {
            g.setColour (c.withAlpha (0.7f));
            g.drawVerticalLine ((int) x, y0, y1);
            g.drawHorizontalLine ((int) y1, x, x + 68.0f);
        };
        const juce::Colour red (0xffc8404a), white (0xffc0c6ce);
        for (int i = 0; i < 10; ++i)
            bracket (1818.0f, 196.0f + (float) i * 33.0f, 226.0f + (float) i * 33.0f, i < 4 || i >= 8 ? red : white);
        for (int i = 0; i < 6; ++i)
            bracket (1822.0f, 540.0f + (float) i * 30.0f, 568.0f + (float) i * 30.0f, white);
        for (int i = 0; i < 3; ++i)
            bracket (1822.0f, 964.0f + (float) i * 36.0f, 996.0f + (float) i * 36.0f, red);
    }

    // PAFL's three meters.
    {
        const float x[] = { 1720.0f, 1742.0f, 1764.0f };
        const float marks[] = { 12.0f, 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -9.0f, -12.0f, -16.0f, -20.0f, -40.0f };
        for (int m = 0; m < 3; ++m)
        {
            const juce::Rectangle<float> r (x[m], 68.0f, 14.0f, 154.0f);
            g.setColour (juce::Colour (0xffb9bdc3));
            g.fillRoundedRectangle (r.expanded (1.6f), 3.0f);
            g.setColour (juce::Colour (0xff111214));
            g.fillRoundedRectangle (r, 2.0f);
            const auto levelAt = [this] (int side) { return juce::Decibels::gainToDecibels (ctx.engine.phones.peak[side].load(), -100.0f); };
            const float db = (m == 2 ? juce::jmax (levelAt (0), levelAt (1)) : levelAt (m)) + 18.0f;
            const float seg = r.getHeight() / (float) std::size (marks);
            for (int i = 0; i < (int) std::size (marks); ++i)
            {
                const auto colour = i == 0 ? juce::Colour (0xffff3030) : i < 3 ? juce::Colour (0xfff0c030) : juce::Colour (0xff40d070);
                g.setColour (db >= marks[i] ? colour : colour.withAlpha (0.14f));
                g.fillRect (r.getX() + 2.0f, r.getY() + (float) i * seg + 1.5f, r.getWidth() - 4.0f, seg - 3.0f);
            }
        }
    }

    // The Assigned light.
    {
        auto* s = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
        const bool assigned = s != nullptr && (s->params.preInsert().fx.load() >= 0 || (s->getKind() != StripKind::Aux && s->params.postInsert().fx.load() >= 0));
        g.setColour (assigned ? juce::Colour (0xffff4040) : juce::Colour (0xff3a2020));
        g.fillEllipse (89.0f, 284.0f, 8.0f, 8.0f);
    }

    // The banks' peak lights.
    for (int b = 0; b < kBanks; ++b)
        for (int layer = 0; layer < kLayers; ++layer)
        {
            bool peak = false;
            for (const auto& slot : layouts[(size_t) b][(size_t) layer])
                peak = peak || peakDb (ctx, slot) > -5.0f;
            g.setColour (peak ? juce::Colour (0xffff3030) : juce::Colour (0xff3a2020));
            g.fillEllipse ((b == 0 ? 971.0f : 1879.0f), 804.0f + (float) layer * 28.0f, 7.0f, 7.0f);
        }

    // The two USB sockets and the Talk Active light.
    for (int i = 0; i < 2; ++i)
    {
        const juce::Rectangle<float> r (1800.0f, 48.0f + (float) i * 42.0f, 104.0f, 38.0f);
        g.setColour (juce::Colour (0xff3c3f46));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colour (0xff0d0e10));
        g.fillRoundedRectangle (juce::Rectangle<float> (42.0f, 20.0f).withCentre (r.getCentre()), 2.0f);
        g.setColour (juce::Colour (0xff4c74c0));
        g.fillRect (juce::Rectangle<float> (36.0f, 7.0f).withCentre (r.getCentre()));
    }
    g.setColour (ctx.engine.isTalkOn (0) ? juce::Colour (0xffff4040) : juce::Colour (0xff3a2020));
    g.fillEllipse (1746.0f, 317.0f, 8.0f, 8.0f);

    // The strips' groups: the LCD windows, the lines between them, the GEQ's marks.
    for (int group = 0; group < 6; ++group)
    {
        const int first = group * 4;
        const float x0 = (float) stripX (first), x1 = (float) stripX (first + 3) + (float) kStripW;
        g.setColour (juce::Colour (0xffb9bdc3));
        g.drawRect (juce::Rectangle<float> (x0 - 1.0f, (float) kStripsY + 53.0f, x1 - x0 + 2.0f, 52.0f), 2.0f);
        if (group % 3 != 2)
        {
            g.setColour (kOutline.withAlpha (0.5f));
            g.drawVerticalLine ((int) x1 + 2, (float) kStripsY, (float) kDesignHeight - 8.0f);
            hw::silk (g, { x1 - 18.0f, (float) kStripsY + 400.0f, 40.0f, 12.0f }, "GEQ", l, 8.5f, juce::Justification::centred, true);
        }
    }

    for (const auto& s : silks)
    {
        if (s.text.containsChar ('\n'))
        {
            g.setColour (l.silk);
            g.setFont (type::font (s.size, true));
            g.drawFittedText (s.text, s.area.toNearestInt(), s.justification, 2);
        }
        else
            hw::silk (g, s.area, s.text, l, s.size, s.justification, s.dim);
    }
}

void DLiveC3500Surface::resized()
{
    const float s = juce::jmin ((float) getWidth() / (float) kDesignWidth, (float) getHeight() / (float) kDesignHeight);
    design = s > 0.0f ? juce::AffineTransform::scale (s).translated (((float) getWidth() - (float) kDesignWidth * s) * 0.5f, ((float) getHeight() - (float) kDesignHeight * s) * 0.5f)
                      : juce::AffineTransform();
    for (auto* c : getChildren())
        c->setTransform (design);
    auto bar = juce::Rectangle<int> (kDesignWidth, kTopBar);
    universalKey.setBounds (bar.removeFromRight (190).reduced (sizes::kGap, 5));
    c1500Key.setBounds (bar.removeFromRight (190).reduced (sizes::kGap, 5));
    for (auto [host, x] : { std::pair<juce::Component*, int> { &leftHost, kLeftScreenX }, { &rightHost, kRightScreenX } })
    {
        host->setBounds (0, 0, dl::kTouchW, dl::kTouchH);
        host->setTransform (juce::AffineTransform::scale (kScreenScale).translated ((float) x, (float) kScreenY).followedBy (design));
    }
}

} // namespace livedaw::ui
