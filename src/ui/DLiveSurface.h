#pragma once

#include "engine/MixerEngine.h"
#include "ui/HardwareLook.h"
#include "ui/Knobs.h"
#include "ui/ScreenMaximiser.h"
#include "ui/Skin.h"
#include "ui/UiContext.h"
#include "ui/universal/FxRackView.h"
#include "ui/universal/LibraryView.h"
#include "ui/universal/MonitorView.h"
#include "ui/universal/RoutingView.h"
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
    class ProcessingScreen;
    class SceneScreen;
    class MixRackScreen;
    class MetersScreen;
    class IoScreen;
    class SurfaceScreen;
    class StatusScreen;
    class HelpPanel;

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

    /** What a SoftKey does (a choice from the console's list of functions, the ones Sidecar
        has something behind). */
    struct SoftKey
    {
        enum Kind
        {
            Unused,
            Mute,      // the strip `slot`'s mute
            MuteGroup, // mute group `index`
            DcaMute,   // DCA `index`'s mute
            Mix,       // the mix `slot` on the faders
            SceneGo,
            SceneNext,
            ScenePrev,
            Talk,
            ClearPafl
        };
        Kind kind = Unused;
        int index = 0;
        Slot slot;
    };

    /** What a custom rotary key (1 to 4) sets, on every strip it can. */
    enum class CustomRotary
    {
        Trim,
        Hpf,
        Lpf,
        GateThres,
        GateDepth,
        CompThres,
        CompRatio,
        CompGain,
        Pan,
        Width,
        Delay,
        NumFunctions
    };
    /** Its name in a menu. */
    juce::String customRotaryName (CustomRotary);

    /** A surface's own assignments: which strips sit on each layer, what the SoftKeys and
        custom rotaries do. Kept in Sidecar's settings, one set per surface: the console stores
        them with a show's scenes, but the engine's show file has nowhere for them yet. */
    class SurfaceSetup
    {
    public:
        static constexpr int kSoftKeys = 19, kCustomRotaries = 4;
        using Factory = std::function<Slot (int bank, int layer, int strip)>;

        SurfaceSetup (juce::PropertiesFile&, const juce::String& model, int numBanks, int numLayers, int numStrips, Factory);

        int getNumBanks() const noexcept { return banks; }
        int getNumLayers() const noexcept { return layers; }
        int getNumStrips() const noexcept { return strips; }

        Slot getSlot (int bank, int layer, int strip) const;
        /** What a strip of a layer shows on a console with `numMains` main strips (its Main
            mode's: LR, then M or C, LFE, Ls/Rs). A layer that ends on LR's master makes room
            for the others beside it, in their order, where the strips there are still the
            factory's; else the strip's own slot. */
        Slot getShownSlot (int bank, int layer, int strip, int numMains) const;
        void setSlot (int bank, int layer, int strip, const Slot&);
        const SoftKey& getSoftKey (int i) const { return softKeys[(size_t) juce::jlimit (0, kSoftKeys - 1, i)]; }
        void setSoftKey (int i, const SoftKey&);
        CustomRotary getCustom (int i) const noexcept { return custom[(size_t) juce::jlimit (0, kCustomRotaries - 1, i)]; }
        void setCustom (int i, CustomRotary);
        /** Back to the factory strips, nothing on the SoftKeys, the default rotaries. */
        void reset();

        /** Something moved (the surface rebinds its strips). */
        std::function<void()> onChanged;

        /** The show's user layers that occupy a layer here (bank "c1500=<layer>" or "c3500=<bank>.<layer>") are the
            same layers: a slot edited here goes back to the layer (changed()), and pullShowLayers() takes a layer's
            faders into the slots (true: something changed; onChanged follows). */
        void linkTo (MixerEngine&, LayerSet&, const juce::String& console);
        bool pullShowLayers (bool notify = true);

    private:
        void load();
        void save() const;
        void changed();
        int cell (int bank, int layer, int strip) const noexcept;

        juce::String bankKey (int bank, int layer) const { return banks == 1 ? juce::String (layer) : juce::String (bank) + "." + juce::String (layer); }
        void pushShowLayers();

        MixerEngine* linkedEngine = nullptr;
        LayerSet* linkedLayers = nullptr;
        juce::String linkedConsole;
        bool pulling = false;

        juce::PropertiesFile& settings;
        juce::String key;
        int banks, layers, strips;
        Factory factory;
        std::vector<Slot> slots;
        std::array<SoftKey, kSoftKeys> softKeys;
        std::array<CustomRotary, kCustomRotaries> custom;
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

        /** The surface's assignments. */
        virtual SurfaceSetup& getSetup() noexcept = 0;
        /** GO on the panel; Prev (-1) and Next (+1): the scene GO will recall. */
        virtual void sceneGo() = 0;
        virtual void sceneStep (int delta) = 0;
        /** A SoftKey does what it's assigned; its light. */
        void pressSoftKey (UiContext&, int index);
        bool isSoftKeyOn (UiContext&, int index);
        /** Every PAFL off. */
        static void clearPafl (UiContext&);

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

    /** The assignment menus (they open beside `target`; a pick takes effect at once and
        `then` runs after it). */
    void showSoftKeyMenu (UiContext&, SurfaceSetup&, int index, juce::Component* target, std::function<void()> then = {});
    void showCustomRotaryMenu (SurfaceSetup&, int index, juce::Component* target, std::function<void()> then = {});
    void showStripMenu (UiContext&, SurfaceSetup&, int bank, int layer, int strip, juce::Component* target, std::function<void()> then = {});
    /** True if a press of a SoftKey or custom rotary key is to assign it: Setup is held
        (latched, as a mouse has one pointer), or it's a right-click. */
    bool wantsAssign (bool setupHeld);
    /** What a slot is called (Ip 5, Aux 3, DCA 2; "-": nothing). */
    juce::String slotName (UiContext&, const Slot&);
    /** What a SoftKey is set to, in words. */
    juce::String softKeyName (UiContext&, const SoftKey&);

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
    Gain and Freq by the screen. Wider than the real surface so that it fits a screen. Its one
    touchscreen is what photographs of a C1500's show: the strip box and preamp at the left,
    the soft widget at the right, the four bands or a curve along the bottom, the page in the
    middle (the screens in `DLiveScreens`, hosted by `ProcessingScreen`). The Universal
    Mixer's pages fill it for what has no screen of its own yet. No maker's artwork or logo
    is used. */
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
    bool skinKeyPressed (const juce::KeyPress& key) override { return maximiser.keyPressed (key); }
    void selectionChanged() override;
    void showChanged() override;

    static constexpr int kStrips = 12, kBanks = 6;
    static constexpr int kDesignWidth = 1920, kDesignHeight = 1080;

    /** What the screen shows (the keys beside it; none lit: the status page). */
    enum class Page
    {
        Status,
        Processing,
        Meters,
        Fx,
        Io,
        Scenes,
        Ganging,
        MixRack,
        Surface,
        Util,
        Routing,
        Library,
        Setup,
        Listen
    };
    void showPage (Page);
    Page getPage() const noexcept { return page; }

    /** Home: the default bank, screen, active Mix, rotary function and LCD view. */
    void pressHome();

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

    dl::SurfaceSetup& getSetup() noexcept override { return assignments; }
    void sceneGo() override;
    void sceneStep (int delta) override;

    dl::StripUnit& getStrip (int i) { return *strips[(size_t) juce::jlimit (0, kStrips - 1, i)]; }
    /** A panel key by its name (nullptr: none). */
    dl::Key* findKey (const juce::String& name) const;
    dl::Rotary& getPeqRotary (int band, bool freq) { return *peq[(size_t) (band * 2 + (freq ? 1 : 0))]; }
    dl::Rotary& getDataRotary() noexcept { return *dataRotary; }
    dl::ProcessingScreen& getProcessing() noexcept { return *processing; }
    dl::SceneScreen& getScenes() noexcept { return *scenes; }
    dl::MixRackScreen& getMixRack() noexcept { return *mixRack; }
    dl::MetersScreen& getMeters() noexcept { return *metersScreen; }
    dl::IoScreen& getIo() noexcept { return *ioScreen; }
    dl::SurfaceScreen& getSurfaceScreen() noexcept { return *surfaceScreen; }
    dl::StatusScreen& getStatus() noexcept { return *status; }
    dl::HelpPanel& getHelp() noexcept { return *help; }

    /** What opens the assignment menus (tests replace them). */
    std::function<void (int softKey, juce::Component*)> assignSoftKey;
    std::function<void (int customRotary, juce::Component*)> assignRotary;

    /** The bank's strips as a new console has them (Sidecar's reading: strips are freely
        assignable on a dLive, and we have no file of a template's banks). */
    static std::vector<dl::Slot> defaultBank (int bank);

private:
    void timerCallback() override;
    void bindStrips();
    void bindPeq();
    void softKeyPressed (int index);
    juce::String helpKey() const;
    void selectPage (const juce::String& keyName, Page);
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
    dl::SurfaceSetup assignments;
    int linkTick = 0;
    juce::Component screenHost;
    std::unique_ptr<dl::ProcessingScreen> processing;
    std::unique_ptr<dl::SceneScreen> scenes;
    std::unique_ptr<dl::MixRackScreen> mixRack;
    std::unique_ptr<dl::MetersScreen> metersScreen;
    std::unique_ptr<dl::IoScreen> ioScreen;
    std::unique_ptr<dl::SurfaceScreen> surfaceScreen;
    std::unique_ptr<dl::StatusScreen> status;
    std::unique_ptr<dl::HelpPanel> help;
    universal::RoutingView routingView;
    universal::FxRackView fxRack;
    universal::MonitorView monitoring;
    universal::LibraryView library;
    universal::SetupView setupView;
    std::array<std::unique_ptr<dl::StripUnit>, kStrips> strips;
    std::vector<std::pair<juce::String, std::unique_ptr<dl::Key>>> keys;
    std::array<std::unique_ptr<dl::Rotary>, 8> peq; // Gain, Freq of each band
    std::unique_ptr<dl::Rotary> dataRotary, phones;
    std::vector<Silk> silks;
    Page page = Page::Processing;
    juce::String litPageKey { "Page Processing" };
    RotaryFunction rotaryFunction = RotaryFunction::Gain;
    DisplayFunction displayFunction = DisplayFunction::Name;
    int bank = 0, mixBus = -1;
    bool widthMode = false;
    Strip* boundStrip = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DLiveSurface)
private:
    /** Maximise display: F10 or a double-click on the display; Esc returns. */
    ScreenMaximiser maximiser { *this, { &screenHost } };
};

} // namespace livedaw::ui
