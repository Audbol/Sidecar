#pragma once

#include "engine/FaderLaw.h"
#include "ui/Knobs.h"
#include "ui/Skin.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace livedaw::ui
{

class Y01VSurface;

namespace y01v
{
    /** What the faders and the master's keys work (Owner's Manual, pp.32-35). */
    enum class FaderMode
    {
        Home,
        Aux1,
        Aux2,
        Aux3,
        Aux4,
        Effect1,
        Effect2,
        OptionIO,
        Remote
    };

    /** The LCD's functions: what the function and fader mode keys call. */
    enum class Function
    {
        View,
        Dynamics,
        EqAtt,
        PhaseDelay,
        PanRouting,
        Utility,
        Midi,
        Setup,
        Send, // the fader mode's AUX or EFFECT send
        OptionIO,
        Remote,
        Memory
    };

    /** A key of the panel: a rounded block in a well, a moulded bar near its foot
        and, on most, a small LED window near its top. */
    class Key final : public juce::Button
    {
    public:
        Key (const juce::String& name, juce::Colour face, bool withLed);

        std::function<bool()> isOn;
        void refresh();
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        juce::Colour face;
        bool led, shownOn = false;
    };

    /** A fluted knob with a scalloped top, optionally a pointer line up its side. */
    class Knob final : public juce::Component
    {
    public:
        Knob (juce::Colour cap, juce::Colour pointer = {});

        void setBinding (KnobBinding);
        const KnobBinding& getBinding() const noexcept { return binding; }
        void refresh();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        void moveTo (float position);

        juce::Colour cap, pointer;
        KnobBinding binding;
        float shown = -1.0f, dragStart = 0.0f;
    };

    /** The PARAMETER wheel: matte black, a glossy finger dimple near its edge. */
    class Wheel final : public juce::Component
    {
    public:
        std::function<void (int)> onTurn;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        float angleAt (juce::Point<float>) const;
        void turnBy (float radians);

        float angle = 4.0f, lastAngle = 0.0f, pending = 0.0f;
    };

    /** A motor fader on its printed stripe: navy left of the slot with white
        numbers, silver right of it with dark ticks. The scale is the panel's,
        fixed; the cap follows what the fader mode gives it. */
    class Fader final : public juce::Component
    {
    public:
        Fader (juce::Colour cap, juce::Colour line, bool masterScale, const FaderLaw& printedLaw);

        /** The level it moves (nullptr: none: the cap rests at the bottom). */
        void bind (std::atomic<float>* db, const FaderLaw&);
        bool isBound() const noexcept { return value != nullptr; }
        double getPosition() const noexcept { return position; }
        /** Moves the fader as the user would: writes the value. */
        void setPositionFromUser (double newPosition);
        void refresh();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        juce::Rectangle<float> track() const noexcept;

        juce::Colour capColour, lineColour;
        bool master;
        FaderLaw printed, law;
        std::atomic<float>* value = nullptr;
        double position = 0.0, dragStart = 0.0;
    };

    /** The STEREO meter: 12 LEDs a side, CLIP red, -3 to -18 amber, the rest green. */
    class Meter final : public juce::Component
    {
    public:
        explicit Meter (UiContext&);
        void refresh();
        void paint (juce::Graphics&) override;

    private:
        UiContext& ctx;
        std::array<int, 2> shown { -1, -1 };
    };

    /** The 320 x 80 dot LCD (pp.28-30): the function, scene and effects on the left,
        the selected channel on the right, a page between. */
    class Display final : public juce::Component
    {
    public:
        static constexpr int kDotsW = 320, kDotsH = 80, kDot = 3;
        static constexpr int kWidth = kDotsW * kDot, kHeight = kDotsH * kDot;

        explicit Display (UiContext&);
        ~Display() override;

        void setFunction (Function);
        /** A function key: its function, or if that's showing, the function's next page. */
        void pressFunction (Function);
        void nextPage();
        int getPage() const noexcept { return page; }
        int getNumPages() const;
        /** UTILITY's preferences (pp.203-204), kept in the app's settings. */
        bool preference (const char* key, bool initial = true) const;
        void setPreference (const char* key, bool);
        /** [-1/DEC] and [+1/INC]: as the wheel; on MEMORY, with INC/DEC SCENE RECALL, the next scene. */
        void incDec (int delta);
        Function getFunction() const noexcept { return function; }
        void setFaderMode (FaderMode);
        /** What the right column shows: the PAN, F and G knobs' values. */
        void setSelectedBindings (KnobBinding pan, KnobBinding freq, KnobBinding gain);
        /** The EQ band F and G work: its frequency is the dotted line on the EQ page. */
        void setEqBand (int band) noexcept { eqBand = band; }
        /** The structure or the selection changed: the page's fields again. */
        void rebuild();
        void setContrast (float);

        void cursor (int dx, int dy);
        void turn (int delta);
        void enter();
        /** MEMORY's selected scene memory (0: the read-only InitData). */
        int getMemory() const noexcept { return memory; }
        void setMemory (int);
        int getNumFields() const noexcept { return (int) cells.size(); }
        int getFocus() const noexcept { return focus; }
        /** The focused field's value text. */
        juce::String getFocusText() const;
        /** The dots, for the tests. */
        bool isDotOn (int x, int y) const noexcept;

        void refresh();
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        struct Cell
        {
            enum Kind
            {
                Text,
                Switch,
                Dial,
                Bar,
                Button,   // ENTER or a click presses it
                List,     // the wheel moves it; drawn by its page
                DialLine  // a dial with its name and value beside it (DYNAMICS)
            };
            Kind kind = Text;
            juce::Rectangle<int> area;
            KnobBinding binding;
            juce::String caption;
            std::function<void()> press;
            bool dotted = false; // a Text field in a dotted frame
            bool raw = false;    // printed as it is, not as a value with units to shorten
        };

        // The dots.
        void clear();
        void dot (int x, int y, bool on = true);
        void hLine (int x0, int x1, int y, bool on = true);
        void vLine (int x, int y0, int y1, bool on = true);
        void box (juce::Rectangle<int>, bool on = true);
        void fill (juce::Rectangle<int>, bool on = true);
        void dottedH (int x0, int x1, int y);
        int textWidth (const juce::String&, bool tiny, int scale = 1) const;
        void text (int x, int y, const juce::String&, bool tiny = false, int scale = 1, bool on = true);
        /** Printed by flipping the dots under it (labels over the EQ curve). */
        void textFlipped (int x, int y, const juce::String&);
        void textCentred (juce::Rectangle<int>, const juce::String&, bool tiny = false, bool on = true);
        /** The 5x7 font drawn taller: `repeat` is how many rows each of its seven becomes. */
        void textTall (int x, int y, const juce::String&, const int* repeat, float pitch, bool bold, bool on = true);
        void dial (int cx, int cy, int r, float value, bool filled = false);
        void dottedV (int x, int y0, int y1);
        /** A small framed arrow: more of the page that way. */
        void arrowBox (int x, int y, bool right);
        void header (const juce::String&);
        /** The level meter at the top right of many pages, its marks and its two channels' names. */
        void channelMeter (Strip&, const juce::String& left, const juce::String& right);

        // The page.
        Cell& add (Cell::Kind, juce::Rectangle<int>, KnobBinding, const juce::String& caption = {});
        void buildEq (Strip&);
        void buildDynamics (Strip&);
        void drawDynamics (Strip&);
        void buildPhase();
        void drawPhase();
        void buildDelay();
        void drawDelay();
        void buildPan();
        void drawPan();
        void buildSend();
        void buildView (Strip&);
        void drawView (Strip&);
        void buildMemory();
        void drawMemory();
        void buildEffectEdit();
        void drawEffectEdit();
        void buildEffectLibrary();
        void drawEffectLibrary();
        void buildPrePost();
        void drawPrePost();
        void buildOscillator();
        void drawOscillator();
        void buildPreferences();
        void drawPreferences();
        void buildRouting();
        void drawRouting();
        void buildOption();
        void drawOption();
        /** OPTION's channel: the selected one if it's CH 17-24, else CH 17. */
        Strip* optionStrip() const;
        /** The slot of the EFFECT fader mode (-1: none). */
        int effectSlot() const noexcept;
        /** The strip of a send's column: 0-13 the channels, 14-15 the returns (none: nullptr). */
        Strip* sendColumn (int i, juce::String& name) const;
        void applyOscillator();
        void meterColumn (int x, int y, float db);
        void meterScale (int x, int y);
        juce::Array<int> libraryModels() const;
        void recallLibrary();
        /** MEMORY's title of scene memory `m` (0: InitData; empty: nothing stored there). */
        juce::String memoryTitle (int m) const;
        bool isProtected (int m) const;
        void setProtected (int m, bool);
        void storeMemory();
        void recallMemory();
        void editMemoryTitle();

        void drawLeft();
        void drawRight();
        void drawPage();
        void drawEq (Strip&);
        void drawCell (const Cell&, bool focused);
        void renderImage();
        void pick (juce::Point<int> pixel);
        Strip* selected() const;
        juce::String stripName (const Strip*) const;
        /** The AUX or EFFECT bus slot of the fader mode (-1: none). */
        int sendBus() const noexcept;
        juce::String functionName() const;

        UiContext& ctx;
        Function function = Function::EqAtt;
        FaderMode mode = FaderMode::Home;
        std::vector<Cell> cells;
        int focus = 0;
        float dragStart = 0.0f, contrast = 0.75f;
        int memory = 0;
        int page = 0, fxScroll = 0, library = -1, eqBand = 1;
        std::array<int, 12> lastPages {};
        // The oscillator (UTILITY page 1, p.204). Not saved: it starts off.
        bool oscOn = false;
        int oscWave = 1;
        float oscLevelDb = -20.0f;
        std::array<bool, 11> oscAssign {}; // AUX 1-4, BUS 1-4, EFF 1-2, STEREO
        std::array<KnobBinding, 3> selectedBindings;
        std::vector<uint8_t> dots, shownDots;
        juce::Image image;
    };
} // namespace y01v

/** The Yamaha 01V's surface, drawn from the Owner's Manual's panel drawing and
    photographs of a real console: 14 motor faders and the STEREO master, the fader
    modes, SELECTED CHANNEL, the L/R meter and the 320 x 80 dot LCD. Wider than
    the real panel, so its LCD can be read on a screen. No maker's artwork or logo is used. */
class Y01VSurface final : public Skin,
                          private juce::Timer
{
public:
    explicit Y01VSurface (UiContext&);
    ~Y01VSurface() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshAll() override;
    void selectionChanged() override;

    static constexpr int kStrips = 14;
    static constexpr int kDesignWidth = 1920, kDesignHeight = 1080;

    void setFaderMode (y01v::FaderMode);
    y01v::FaderMode getFaderMode() const noexcept { return mode; }
    /** The EQ band F and G work: 0 LOW, 1 LO-MID, 2 HI-MID, 3 HIGH. */
    void setEqBand (int);
    int getEqBand() const noexcept { return eqBand; }

    y01v::Display& getDisplay() noexcept { return display; }
    y01v::Fader& getFader (int i) { return *faders[(size_t) juce::jlimit (0, kStrips - 1, i)]; }
    y01v::Fader& getMasterFader() noexcept { return *masterFader; }
    y01v::Key* findKey (const juce::String& name) const;
    y01v::Knob& getKnob (const juce::String& name);
    /** The engine strip a physical strip works in this fader mode (nullptr: none). */
    Strip* stripAt (int i) const;
    /** The strip the master fader and its keys work in this fader mode. */
    Strip* masterStrip() const;

private:
    void timerCallback() override;
    void bindStrips();
    void bindKnobs();
    void bindInputs();
    void pressPad (int channel);
    y01v::Key& key (const juce::String& name, juce::Colour face, bool led, juce::Rectangle<int>);
    y01v::Knob& knob (const juce::String& name, juce::Colour cap, juce::Colour pointer, juce::Rectangle<int>);
    int sendBus() const noexcept;

    UiContext& ctx;
    const universal::Theme& theme;
    universal::ToggleKey universalKey;
    y01v::Display display;
    y01v::Meter meter;
    y01v::Wheel wheel;

    std::vector<std::unique_ptr<y01v::Fader>> faders;
    std::unique_ptr<y01v::Fader> masterFader;
    std::vector<std::pair<juce::String, std::unique_ptr<y01v::Key>>> keys;
    std::vector<std::pair<juce::String, std::unique_ptr<y01v::Knob>>> knobs;

    y01v::FaderMode mode = y01v::FaderMode::Home;
    int eqBand = 1;
    Strip* boundStrip = nullptr;
    std::atomic<float> contrastValue { 0.75f };
    bool soloLit = false;
    /** The 26 dB PAD switches of CH 1-12. */
    std::array<bool, 12> pad {};
    juce::AffineTransform design;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Y01VSurface)
};

} // namespace livedaw::ui
