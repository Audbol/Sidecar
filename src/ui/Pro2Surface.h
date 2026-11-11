#pragma once

#include "engine/FaderLaw.h"
#include "ui/Knobs.h"
#include "ui/Skin.h"
#include "ui/ScreenMaximiser.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace livedaw::ui
{

class Pro2Surface;

namespace pro2
{
    /** The fader bays (Owner's Manual p.18-19): the input bay (the PRO2's extra eight faders), the
        channel bay under the screen, the mix bay, the three masters (area B). */
    enum class Bay
    {
        Input,
        Channel,
        Mix,
        Master
    };

    /** What the mix bay's faders hold (pp.19, 44). */
    enum class MixMode
    {
        Vca,    // the eight VCA groups (the VCA key)
        Output, // outputs, scrolled by eight (the OUTPUT key)
        Extend, // the input channels after the channel bay's (EXTEND)
        Geq     // the selected output's GEQ, eight bands (GEQ)
    };

    /** The channel strip's detail areas (p.240, Figure 22). */
    enum class Area
    {
        Overview,
        Config,
        Comp,
        Gate,
        Eq,
        Insert,
        Aux,
        Matrix,
        Masters
    };

    /** The GUI's screens Sidecar has (the GUI menu, p.285). */
    enum class Page
    {
        Default,    // the eight fast strips and the detail area
        Overview,   // Console Overview: every meter
        Automation, // the scene cue list
        Effects,    // the rack of six
        Geq,        // the selected output's GEQ
        Patching,
        Groups,     // VCA and POPulation groups
        kCount
    };

    constexpr int kVcas = 8, kPops = 6, kAutoMutes = 6, kGroups = kVcas + kPops, kFxUnits = 6;
    /** The bays' banks, in the order SCROLL goes through them (p.19): inputs, returns, aux sends,
        matrices, masters. */
    constexpr int kInputBanks = 7, kBanks = kInputBanks + 5;

    /** A key: a lit cap (MUTE, SOLO, ON), an LCD select button, a round quick access button, a
        cream arrow. */
    class Key final : public juce::Button
    {
    public:
        enum class Shape
        {
            Cap,
            Lcd,
            Round,
            Left,
            Right,
            Up,
            Down
        };

        Key (const juce::String& name, juce::Colour face, Shape);

        std::function<bool()> isOn;
        /** An LCD button's legend and backlight (transparent: dark). */
        std::function<juce::String()> legend;
        std::function<juce::Colour()> backlight;
        /** True once after a press that lasted more than half a second (an LCD select button
            held: group assignment); its onClick asks. */
        bool takeHeld() noexcept
        {
            const bool h = held;
            held = false;
            return h;
        }
        /** For the tests: the next takeHeld() answers `h`. */
        void setHeld (bool h) noexcept { held = h; }
        void setGlyph (const juce::String& text) { glyph = text; }
        void refresh();
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::Colour face;
        Shape shape;
        juce::String glyph, shownLegend;
        juce::Colour shownLight;
        bool shownOn = false, held = false;
        juce::uint32 downAt = 0;
    };

    /** A knob: a black body with a coloured top and a white pointer. */
    class Knob final : public juce::Component
    {
    public:
        explicit Knob (juce::Colour top);

        void setBinding (KnobBinding);
        const KnobBinding& getBinding() const noexcept { return binding; }
        std::function<void()> onTouch;
        void refresh();
        /** The position drawn (-1: none yet). */
        float getShown() const noexcept { return shown; }

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        void moveTo (float position);

        juce::Colour top;
        KnobBinding binding;
        float shown = -1.0f, dragStart = 0.0f;
    };

    /** A motor fader on the printed scale (10, 5, 0, 5, 10, 20, 30, 40, 50, 70, oo: a press render). */
    class Fader final : public juce::Component
    {
    public:
        Fader (const FaderLaw& printed, juce::Colour band);

        void bind (std::atomic<float>* value, const FaderLaw&);
        /** A fader that works something other than a level in the law (a GEQ band). */
        void bind (KnobBinding);
        bool isBound() const noexcept { return value != nullptr || binding.isValid(); }
        double getPosition() const noexcept { return position; }
        void setPositionFromUser (double);
        void refresh();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        juce::Rectangle<float> track() const noexcept;

        FaderLaw printed, law;
        juce::Colour band;
        std::atomic<float>* value = nullptr;
        KnobBinding binding;
        double position = 0.0, dragStart = 0.0;
    };

    /** An 11-segment LED meter (p.241). */
    class Meter final : public juce::Component
    {
    public:
        /** The level, linear. */
        std::function<float()> level;
        void refresh();
        void paint (juce::Graphics&) override;
        int getLit() const noexcept { return lit; }

    private:
        int lit = 0;
    };

    /** The GUI screen (pp.38-39): the status bar, the main area and the detail area. Sidecar's own
        drawing of what the manual's pictures show, in vectors on a 1024 x 768 page. */
    /** The trackball and its two keys on the surface: a drag on the ball moves the screen's cursor; the keys click
        (a drag with a key held moves it as a drag on the screen does). */
    class Screen;
    class Trackball final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        enum class Part { Ball, LeftKey, RightKey };
        Trackball (Screen&, Part);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool hitTest (int x, int y) override;
        const Part part;

    private:
        Screen& screen;
        juce::Point<float> last;
    };

    class Screen final : public juce::Component
    {
    public:
        static constexpr int kWidth = 1024, kHeight = 768;

        Screen (UiContext&, Pro2Surface&);

        void showPage (Page);
        Page getPage() const noexcept { return page; }
        /** The screen's name in the status bar ("Channels 9 - 16", "Automation"). */
        juce::String getTitle() const;
        /** The structure, the selection or the page changed: the page's controls again. */
        void rebuild();
        /** Moving things (meters, values) drawn again. */
        void refresh();

        // The Effects screen's unit, the cue list's highlighted scene, the Patching screen's first row.
        void setFxUnit (int);
        int getFxUnit() const noexcept { return fxUnit; }
        /** Puts device `model` (the FX table's) in the selected unit. */
        void setFxModel (int model);
        /** The devices a unit can hold: none, then the console's own. */
        juce::Array<int> fxModels() const;
        void setCue (int);
        int getCue() const noexcept { return cue; }
        void setPatchScroll (int);

        /** The page's controls: a place, what a press does, what a drag turns. */
        struct Spot
        {
            juce::String name;
            juce::Rectangle<float> area;
            std::function<void()> press;
            KnobBinding binding;
            /** A fader: the pointer's height is its position (a dial turns by the drag's length). */
            bool absolute = false;
        };
        int getNumSpots() const noexcept { return (int) spots.size(); }
        const Spot& getSpot (int i) const { return spots[(size_t) i]; }
        /** The spot named `name` (-1: none). */
        int findSpot (const juce::String& name) const;
        /** Presses a spot by name; false if the page has none. */
        bool press (const juce::String& name);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        /** The trackball's cursor (p.12: the trackball moves a cursor on the GUI, its keys click): its place
            in the screen's own pixels, moved by a distance, a key down (a press, and a drag while it is held)
            and up. */
        juce::Point<float> getCursor() const noexcept { return cursor; }
        void setCursor (juce::Point<float>);
        void moveCursor (float dx, float dy);
        void cursorDown();
        void cursorUp();
        bool isCursorDown() const noexcept { return cursorHeld; }

    private:
        void pointerDown (juce::Point<float>);
        void pointerDrag (juce::Point<float>, float distanceY);
        juce::Point<float> cursor { 512.0f, 384.0f }, cursorPressedAt;
        bool cursorHeld = false;
        // Everything is laid out by one pass that draws when `g` is set and always notes the spots.
        void render();
        void statusBar();
        void pageDefault();
        void pageOverview();
        void pageAutomation();
        void pageEffects();
        void pageGeq();
        void pagePatching();
        void pageGroups();
        void fastStrip (Strip&, juce::Rectangle<float>, int index);
        void detailArea (juce::Rectangle<float>);
        void detailGroup (juce::Rectangle<float>);
        void dialGrid (juce::Rectangle<float>, std::vector<KnobBinding>, int columns, float rowHeight, juce::Colour cap);
        void select (Strip&, Area);

        void fill (juce::Rectangle<float>, juce::Colour, float radius = 0.0f);
        void frame (juce::Rectangle<float>, juce::Colour, float radius = 0.0f, float thickness = 1.0f);
        void text (juce::Rectangle<float>, const juce::String&, juce::Colour, float size, juce::Justification = juce::Justification::centred, bool bold = false);
        void button (juce::Rectangle<float>, const juce::String& name, const juce::String& label, bool on, juce::Colour lit, std::function<void()> press);
        void dial (juce::Rectangle<float>, const juce::String& name, KnobBinding, juce::Colour cap, bool legend = true);
        void bar (juce::Rectangle<float>, const juce::String& name, KnobBinding, juce::Colour);
        void slider (juce::Rectangle<float>, const juce::String& name, KnobBinding, juce::Colour);
        void meter (juce::Rectangle<float>, float linear);
        void eqGraph (Strip&, juce::Rectangle<float>, bool bands);
        void dynGraph (Strip&, bool gate, juce::Rectangle<float>);
        void filterGraph (Strip&, juce::Rectangle<float>);
        Spot& spot (const juce::String& name, juce::Rectangle<float>, std::function<void()> press, KnobBinding = {});

        UiContext& ctx;
        Pro2Surface& surface;
        Page page = Page::Default;
        std::vector<Spot> spots;
        juce::Graphics* g = nullptr;
        int fxUnit = 0, cue = 0, patchScroll = 0;
        KnobBinding dragged;
        bool draggedAbsolute = false;
        juce::Rectangle<float> draggedArea;
        float dragStart = 0.0f;
    };

    /** A strip's contribution to a mix: an aux send or a matrix send (p.259). */
    struct MixSend
    {
        std::atomic<bool>* on = nullptr;
        std::atomic<bool>* preFlag = nullptr;   // a matrix send's
        std::atomic<int32_t>* mode = nullptr;   // an aux send's (SendMode)
        std::atomic<float>* level = nullptr;

        bool isValid() const noexcept { return level != nullptr; }
        bool isPre() const noexcept;
        /** A group's send has no level and no PRE (p.260). */
        bool isGroup() const noexcept;
        void setPre (bool) const noexcept;
    };
    /** `from`'s send to `mix` (an aux send or a matrix); not valid when it has none. */
    MixSend sendTo (Strip& from, Strip& mix);

    // The PRO2's ranges, as its profile has them (Owner's Manual pp.243-262, 281).
    /** The high pass, 10 Hz to 400 Hz (p.246). */
    KnobBinding bindHighPass (Strip&);
    /** The low pass, 2 kHz to 20 kHz. */
    KnobBinding bindLowPass (Strip&);
    /** The high pass's SLOPE: 24 dB/oct on, 12 off; the low pass's: 12 on, 6 off (p.245). */
    bool steepSlope (const Strip&, bool lowPass);
    void setSteepSlope (Strip&, bool lowPass, bool steep);
    /** Digital trim: -40 to +20 dB on an input, -12 to +6 dB on an output's bus trim (pp.246, 280). */
    KnobBinding bindTrim (Strip&);
    /** The delay in ms: 0-50 on an input (p.243), 0-500 on an output (p.281). */
    KnobBinding bindDelayMs (Strip&);
    /** A send's level, off to +6 dB (p.259). The taper is Sidecar's: the console's fader law, 4 dB down. */
    KnobBinding bindSendLevel (const juce::String& label, std::atomic<float>& levelDb);
    /** The side chain filter's WIDTH (p.255): 0 narrow (1/3 oct), 1, 2 wide. */
    int sideChainWidth (const DynSlotParams&);
    void setSideChainWidth (DynSlotParams&, int width);
    bool sideChainIn (const DynSlotParams&);
    void setSideChainIn (DynSlotParams&, bool);
    /** The compressor's MODE list on this strip: four on an input, five on an output (pp.250, 273). */
    juce::StringArray compModes (const Strip&);
    /** A dynamics or EQ parameter of the strip's model by id (invalid: the model has none). */
    KnobBinding bindDyn (Strip&, bool gate, const juce::String& id);
    KnobBinding bindEqParam (Strip&, const juce::String& id);
    /** The EQ's bands on this strip, top band first: "t", "h", "l", "b" on an input, "6" to "1" on an output. */
    juce::StringArray eqBands (const Strip&);
    juce::String eqBandName (const Strip&, int band);
} // namespace pro2

/** The Midas PRO2's surface: the input and channel bays, the mix bay with the VCA and POPulation groups,
    the masters, the channel strip's detail areas, the outputs, comms and its screen. From the PRO2 Owner's
    Manual (DOC02-DL2) and the maker's press renders of the control centre, kept outside the repository;
    no maker's logo or artwork is used. */
class Pro2Surface final : public Skin,
                          private juce::Timer
{
public:
    explicit Pro2Surface (UiContext&);
    ~Pro2Surface() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshAll() override;
    bool skinKeyPressed (const juce::KeyPress& key) override { return maximiser.keyPressed (key); }
    void selectionChanged() override;
    void showChanged() override;

    static constexpr int kDesignWidth = 1920, kDesignHeight = 1080;

    // Navigation (pp.43-45).
    /** A bay's bank (Input and Channel: 0-6 inputs, 7 returns, 8-9 aux sends, 10 matrices, 11 masters). */
    void setBank (pro2::Bay, int bank);
    int getBank (pro2::Bay) const noexcept;
    void scroll (pro2::Bay, int delta);
    void setMixMode (pro2::MixMode);
    pro2::MixMode getMixMode() const noexcept { return mixMode; }
    /** The mix bay's outputs: 0 aux 1-8, 1 aux 9-16, 2 matrices, 3 masters. */
    void setOutputBank (int);
    int getOutputBank() const noexcept { return outputBank; }
    /** FLIP: the input faders are the sends to the selected mix (p.44). */
    void setFlip (bool);
    bool isFlipped() const noexcept { return flip; }
    /** The strips of bank `bank` (fewer than eight: the masters, a console with other counts). */
    std::vector<Strip*> bankStrips (int bank) const;
    juce::String bankName (int bank) const;

    /** The engine strip on fader `i` of a bay now (nullptr: none, or the mix bay holds VCAs or a GEQ). */
    Strip* stripAt (pro2::Bay, int i) const;
    /** The output the outputs' SELECT keys chose: the "selected mix" (p.18). */
    Strip* selectedMix() const;
    void selectMix (Strip*);
    Strip* selected() const { return ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr; }

    // VCA and POPulation groups (pp.143-146). Groups 0-7 are the VCAs, 8-13 the POPulation groups.
    /** Selects a group, unfolding its members to the faders; again, or -1: none. */
    void selectGroup (int group);
    int getGroup() const noexcept { return group; }
    std::vector<Strip*> groupMembers (int group) const;
    bool isMember (int group, const Strip&) const;
    void setMember (int group, Strip&, bool member);
    juce::String groupName (int group) const;
    /** The group whose members the LCD select buttons set (an LCD button held; -1: none);
        kGroups + n: auto-mute group n's SELECT (p.147). */
    void setAssigning (int group);
    int getAssigning() const noexcept { return assigning; }
    /** A group's area B button (p.145): selecting it puts its first three members on the masters. */
    void setAreaB (int group, bool);
    bool isAreaB (int group) const;

    // The channel strip.
    void setArea (pro2::Area);
    pro2::Area getArea() const noexcept { return area; }
    /** The D zone works the gate (inputs) or the compressor. */
    void setDynamicsShowsGate (bool);
    bool dynamicsShowsGate() const noexcept { return showGate; }
    /** The E zone's band, top band 0. */
    void setEqBand (int);
    int getEqBand() const noexcept { return eqBand; }
    /** The mix section's eight: 0 aux 1-8, 1 aux 9-16, 2 matrices. */
    void setMixBank (int);
    int getMixBank() const noexcept { return mixBank; }
    void setGainSwap (bool);
    bool isGainSwapped() const noexcept { return gainSwap; }
    /** The GEQ's first band on the mix bay's faders (0-23; p.44: it starts on 200 Hz). */
    void setGeqFirst (int);
    int getGeqFirst() const noexcept { return geqFirst; }
    /** The selected output's EQ is its GEQ (it replaces the PEQ, p.276). */
    static bool hasGeq (const Strip&);
    static void setGeq (Strip&, bool geq);

    /** A press of an LCD select button: selects the strip, or sets its membership while a group assigns. */
    void pressSelect (Strip*);
    void pressSolo (Strip&);
    /** The automation keys (p.162). */
    void storePressed();
    void okPressed();
    void cancelPressed();
    bool isStorePending() const noexcept { return storePending; }

    pro2::Screen& getScreen() noexcept { return screen; }
    pro2::Fader& getFader (pro2::Bay, int i);
    pro2::Fader& getMonitorFader() noexcept { return *monitorFader; }
    pro2::Key* findKey (const juce::String& name) const;
    pro2::Knob* findKnob (const juce::String& name) const;
    /** One tick of the timer that moves the faders, keys and the screen (the screenshots). */
    void timerCallbackForTest() { timerCallback(); }

private:
    void timerCallback() override;
    void bindFaders();
    void bindSelected();
    void bindAssignable();
    void changed();
    pro2::Key& key (const juce::String& name, juce::Colour face, pro2::Key::Shape, juce::Rectangle<int>, const juce::String& glyph = {});
    pro2::Knob& knob (const juce::String& name, juce::Colour top, juce::Rectangle<int>);
    pro2::Meter& meter (juce::Rectangle<int>, std::function<float()> level);
    void silk (float x, float y, float w, float h, const juce::String& text, float size, juce::Colour, juce::Justification = juce::Justification::centred, bool bold = false);
    void plate (juce::Rectangle<float>, juce::Colour);
    void outline (juce::Rectangle<float>);
    void buildStrip (pro2::Bay, int i, int cx, bool meters);
    void buildDetail();
    void buildOutputs();
    void buildComms();
    void buildNavigation();
    void buildMasters();
    juce::String settingsKey (int group, const char* what) const;
    Strip* outputAt (int row, int i) const;
    Strip* findRef (const juce::String& ref) const;
    static juce::String refOf (const Strip&);

    struct Silk
    {
        juce::Rectangle<float> area;
        juce::String text;
        float size;
        juce::Colour colour;
        juce::Justification justification;
        bool bold;
    };
    struct Plate
    {
        juce::Rectangle<float> area;
        juce::Colour colour;
        bool outlineOnly;
    };

    UiContext& ctx;
    const universal::Theme& theme;
    universal::ToggleKey universalKey;
    pro2::Screen screen;
    pro2::Trackball trackBall { screen, pro2::Trackball::Part::Ball }, trackLeft { screen, pro2::Trackball::Part::LeftKey },
                    trackRight { screen, pro2::Trackball::Part::RightKey };
    std::array<std::vector<std::unique_ptr<pro2::Fader>>, 4> faders;
    std::unique_ptr<pro2::Fader> monitorFader;
    std::vector<std::pair<juce::String, std::unique_ptr<pro2::Key>>> keys;
    std::vector<std::pair<juce::String, std::unique_ptr<pro2::Knob>>> knobs;
    std::vector<std::unique_ptr<pro2::Meter>> meters;
    std::vector<Silk> silks;
    std::vector<Plate> plates;

    std::array<int, 2> banks { 0, 1 };
    pro2::MixMode mixMode = pro2::MixMode::Vca, mixModeBeforeExtend = pro2::MixMode::Vca;
    pro2::Area area = pro2::Area::Overview;
    int outputBank = 0, group = -1, assigning = -1, eqBand = 0, mixBank = 0, geqFirst = 10, screenTick = 0;
    bool flip = false, showGate = false, gainSwap = false, storePending = false, altButtons = false, fxFollows = false, phonesOnFader = false;
    juce::String mixRef;
    Strip* boundStrip = nullptr;
    int boundVersion = -1;
    juce::AffineTransform design;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Pro2Surface)
private:
    /** Maximise display: F10 or a double-click on the display; Esc returns. */
    ScreenMaximiser maximiser { *this, { &screen } };
};

} // namespace livedaw::ui
