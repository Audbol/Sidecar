#pragma once

#include "engine/MixerEngine.h"
#include "ui/HardwareLook.h"
#include "ui/Knobs.h"
#include "ui/Skin.h"
#include "ui/UiContext.h"
#include "ui/universal/FxRackView.h"
#include "ui/universal/Inspector.h"
#include "ui/universal/LibraryView.h"
#include "ui/universal/MixerView.h"
#include "ui/universal/MonitorView.h"
#include "ui/universal/RoutingView.h"
#include "ui/universal/ScenesView.h"
#include "ui/universal/SetupView.h"
#include "ui/universal/Widgets.h"

#include <array>
#include <memory>
#include <vector>

namespace livedaw::ui
{

class DLiveSurface;

namespace dl
{
    /** What a fader strip shows. */
    struct Slot
    {
        enum Kind
        {
            None,
            Channel,
            FxReturn,
            Bus,
            Main,
            Matrix,
            Dca
        };
        Kind kind = None;
        int index = 0;
    };

    /** A panel key: a pale block or oval with its name on it and, on most, a small LED
        near its top; lit, the whole key takes its bright colour (photographs of a C1500). */
    class Key final : public juce::Button
    {
    public:
        enum class Shape
        {
            Block,
            Oval
        };
        Key (const juce::String& text, juce::Colour idle, juce::Colour lit, Shape, bool withLed = true);

        std::function<bool()> isOn;
        void refresh();
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        juce::Colour idle, lit;
        Shape shape;
        bool led, shownOn = false;
    };

    /** A rotary: a dark fluted body, a scalloped top and a light at its centre. */
    class Rotary final : public juce::Component
    {
    public:
        explicit Rotary (juce::Colour light);

        void setBinding (KnobBinding);
        const KnobBinding& getBinding() const noexcept { return binding; }
        void setLight (juce::Colour c);
        /** Its light on even with nothing to turn (the data rotaries). */
        bool alwaysLit = false;
        void refresh();
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        KnobBinding binding;
        juce::Colour light;
        float shown = -1.0f, dragStart = 0.0f;
    };

    /** The strips' rotaries' functions (the keys Gain, Pan, 1-4, Sends). */
    enum class RotaryFunction
    {
        Gain,
        Pan,
        Custom1,
        Custom2,
        Custom3,
        Custom4,
        Sends
    };

    /** What the LCDs' main line shows (Name, dB, I/O, Chan). */
    enum class DisplayFunction
    {
        Name,
        Db,
        Io,
        Chan
    };

    /** What a strip needs from the surface it's on, and what the surfaces share: the keys
        that are held (latched here, as a mouse has one pointer) while a strip's Sel or Mix
        key is pressed, the channel clipboard, Freeze in Layers and the GEQ on the faders. */
    class Desk
    {
    public:
        virtual ~Desk() = default;

        virtual int getMixOnFaders() const noexcept = 0;
        virtual void setMixOnFaders (int bus) = 0;
        virtual RotaryFunction getRotaryFunction() const noexcept = 0;
        virtual DisplayFunction getDisplayFunction() const noexcept = 0;

        enum class Latch
        {
            None,
            Copy,
            Paste,
            Reset,
            Safes,
            Freeze,
            Assign,
            Pre
        };
        Latch getLatch() const noexcept { return latch; }
        void setLatch (Latch l) noexcept { latch = l; }
        void toggleLatch (Latch l) noexcept { latch = latch == l ? Latch::None : l; }
        bool isAssignMode() const noexcept { return latch == Latch::Assign; }
        bool isPreMode() const noexcept { return latch == Latch::Pre; }

        /** A strip's Sel key (or its Mix key) under Copy, Paste, Reset, Safes or Freeze:
            true if the press was for them. */
        bool applyLatch (UiContext&, const Slot&, Strip&, bool mixKey);
        bool isFrozen (const Slot&) const;

        /** The GEQ key: the GEQ of `mix` on the faders, then its next bands, then off. */
        void pressGeq (Strip* mix, int numStrips);
        void endGeq() noexcept { geqStrip = nullptr; }
        Strip* getGeqStrip() const noexcept { return geqStrip; }
        ld::EqBlockParams* getGeq() const;
        /** The band strip `position` moves, -1: none (or the last strip, the mix's master). */
        int getGeqBand (int position, int numStrips) const;

    private:
        Latch latch = Latch::None;
        Slot clipboard;
        bool clipboardMix = false;
        std::vector<Slot> frozen;
        Strip* geqStrip = nullptr;
        int geqPage = 0;
    };

    /** Where a strip's parts go, in its own space: the two surfaces differ. */
    struct StripLayout
    {
        juce::Rectangle<float> level, compMeter, gateMeter, lcd;
        juce::Rectangle<int> rotary, mute, sel, mix, pafl, fader;
        float mutedY = 0.0f, kindDot = 1.0f, nameDot = 2.0f, valueDot = 1.3f;
        float kindY = 4.0f, nameY = 15.0f, barY = 38.0f, valueY = 44.0f, assignY = -1.0f;
        /** The meters' scale printed beside the first strip of each four, and the two
            screws by the fader. */
        bool scale = false, screws = false;
        float faderInset = 18.0f;

        static StripLayout c1500 (int width, int height);
    };

    /** One strip: its meters, its part of the colour LCD, its rotary, Mute, Sel, Mix and
        PAFL, its fader. */
    class StripUnit final : public juce::Component
    {
    public:
        StripUnit (Desk&, UiContext&, const universal::Theme&, const StripLayout&, int position, int numStrips);

        void setSlot (const Slot&);
        const Slot& getSlot() const noexcept { return slot; }
        /** Takes the bank, the mix on the faders and the rotaries' function. */
        void rebind();
        void refresh();

        universal::FaderControl& getFader() noexcept { return fader; }
        Rotary& getRotary() noexcept { return rotary; }
        Key& getMute() noexcept { return mute; }
        Key& getSel() noexcept { return sel; }
        Key& getMix() noexcept { return mix; }
        Key& getPafl() noexcept { return pafl; }
        /** What the LCD's lines say now. */
        juce::String getLcdKind() const;
        juce::String getLcdName() const;
        juce::String getLcdValue() const;
        juce::String getLcdAssign() const;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        Strip* strip() const;
        ld::DcaParams* dca() const;
        void pressSel();
        void pressMix();
        float meterDb() const;
        juce::Colour backlight() const;

        Desk& surface;
        UiContext& ctx;
        StripLayout layout;
        int position, numStrips;
        Slot slot;
        universal::FaderControl fader;
        Rotary rotary;
        Key mute, sel, mix, pafl;
        juce::String shownSignature;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripUnit)
    };

    /** Shows a dLive on its other surface ("c1500" or "c3500"), and remembers that. */
    void switchSurface (UiContext&, const char* model);
    /** The strip a slot shows (nullptr: none, or a DCA). */
    Strip* stripOf (MixerEngine&, const Slot&);
    /** A control of a strip's PEQ, by the model's parameter id ("lf", "1g"...). */
    KnobBinding bindEq (Strip&, const juce::String& id, const juce::String& label);
    /** A control of a strip's gate or compressor. */
    KnobBinding bindDyn (ld::DynSlotParams&, const juce::String& id, const juce::String& label);
    const hw::Look& look();
    /** The screen's colour for a strip's LCD light. */
    void dotPrint (juce::Graphics&, float x, float y, const juce::String&, float dot);
} // namespace dl

/** The Allen & Heath dLive's surface, as the C1500 has it (the owner's photographs of a
    real one, kept outside the repository): 12 motor faders in three groups of four on six
    banks, a colour LCD per group, rotaries with a function chosen by their keys, the PEQ's
    Gain and Freq under the screen. Wider than the real surface so that it fits a screen,
    its touchscreen hosting the Universal Mixer's pages. No maker's artwork or logo is used. */
class DLiveSurface final : public Skin,
                           public dl::Desk,
                           private juce::Timer
{
public:
    explicit DLiveSurface (UiContext&);
    ~DLiveSurface() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshAll() override;
    void selectionChanged() override;
    void showChanged() override { scenes.refresh(); }

    static constexpr int kStrips = 12, kBanks = 6;
    static constexpr int kDesignWidth = 1920, kDesignHeight = 1080;

    /** What the screen shows (the keys beside it). */
    enum class Page
    {
        Home,
        Fx,
        Routing,
        Scenes,
        Library,
        Setup,
        Listen,
        Processing
    };
    void showPage (Page);
    Page getPage() const noexcept { return page; }

    /** Bank A-F (0-5). */
    void setBank (int);
    int getBank() const noexcept { return bank; }

    using RotaryFunction = dl::RotaryFunction;
    using DisplayFunction = dl::DisplayFunction;
    void setRotaryFunction (RotaryFunction);
    RotaryFunction getRotaryFunction() const noexcept override { return rotaryFunction; }
    void setDisplayFunction (DisplayFunction f) { displayFunction = f; }
    DisplayFunction getDisplayFunction() const noexcept override { return displayFunction; }

    /** The mix on the faders (a bus slot, -1: none): each input's fader is its send to it. */
    void setMixOnFaders (int bus) override;
    int getMixOnFaders() const noexcept override { return mixBus; }

    dl::StripUnit& getStrip (int i) { return *strips[(size_t) juce::jlimit (0, kStrips - 1, i)]; }
    /** A panel key by its name (nullptr: none). */
    dl::Key* findKey (const juce::String& name) const;
    dl::Rotary& getPeqRotary (int band, bool freq) { return *peq[(size_t) (band * 2 + (freq ? 1 : 0))]; }
    universal::Inspector& getInspector() noexcept { return inspector; }

    /** The bank's strips as a new console has them (Sidecar's reading: strips are freely
        assignable on a dLive, and we have no file of a template's banks). */
    static std::vector<dl::Slot> defaultBank (int bank);

private:
    void timerCallback() override;
    void bindStrips();
    void bindPeq();
    void clearSolos();
    dl::Key& key (const juce::String& name, juce::Colour idle, juce::Colour lit, juce::Rectangle<int>, dl::Key::Shape shape = dl::Key::Shape::Block, bool led = true);

    struct Silk
    {
        juce::Rectangle<float> area;
        juce::String text;
        float size;
        juce::Justification justification;
        bool dim;
    };

    juce::AffineTransform design;
    UiContext& ctx;
    const universal::Theme& theme;
    universal::ToggleKey universalKey, c3500Key;
    juce::Component screenHost;
    universal::MixerView mixer;
    universal::RoutingView routingView;
    universal::FxRackView fxRack;
    universal::MonitorView monitoring;
    universal::ScenesView scenes;
    universal::LibraryView library;
    universal::SetupView setup;
    universal::Inspector inspector;
    std::array<std::unique_ptr<dl::StripUnit>, kStrips> strips;
    std::vector<std::pair<juce::String, std::unique_ptr<dl::Key>>> keys;
    std::array<std::unique_ptr<dl::Rotary>, 8> peq; // Gain, Freq of each band
    std::unique_ptr<dl::Rotary> dataRotary, phones;
    std::vector<Silk> silks;
    std::vector<std::vector<dl::Slot>> banks;
    Page page = Page::Home;
    juce::String litPageKey { "Home" };
    RotaryFunction rotaryFunction = RotaryFunction::Gain;
    DisplayFunction displayFunction = DisplayFunction::Name;
    int bank = 0, mixBus = -1;
    bool widthMode = false;
    Strip* boundStrip = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DLiveSurface)
};

} // namespace livedaw::ui
