#pragma once

#include "engine/FaderLaw.h"
#include "ui/ConsoleTape.h"
#include "ui/Knobs.h"
#include "ui/Skin.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <array>
#include <functional>
#include <map>
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
        /** REMOTE: any parameter, over the whole of its travel (invalid: none). */
        void bind (KnobBinding);
        bool isBound() const noexcept { return value != nullptr || knob.isValid(); }
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
        KnobBinding knob;
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
        /** A function's page 1, as the AUTO SCREEN preferences bring it up (p.203). */
        void showFirstPage (Function);
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

        // What the console keeps and the engine doesn't: the surface's own, in the app's settings
        // (not in a scene, not on MIDI).
        /** SETUP page 3 (pp.55-56). `fader`: 0-13, the panel's (13/14 and 15/16 one each).
            The group it is in: 0 none, 1-3 (A-C of the faders, D-F of the [ON] keys). */
        int getGroup (int fader, bool mute) const noexcept;
        void setGroup (int fader, bool mute, int group);
        /** Group 0-5 (A-F) works: its ENABLE switch. */
        bool isGroupOn (int group) const noexcept;
        void setGroupOn (int group, bool);
        /** SETUP page 4 (pp.52, 101, 110): pairs 0-5 are CH 1/2 to 11/12, 6-7 AUX 1/2 and 3/4,
            8-9 BUS 1/2 and 3/4. Making one copies the first's settings to the second
            (`fromSecond`: the other way). */
        bool isPaired (int pair) const noexcept;
        void setPair (int pair, bool on, bool fromSecond = false);
        /** SETUP page 2's SOLO (ENABLE) and SEL MODE (LAST SOLO: one at a time). */
        bool isSoloEnabled() const noexcept { return panel.soloOn; }
        bool isLastSolo() const noexcept { return panel.lastSolo; }
        /** The groups and pairs at work: the surface's timer calls it. */
        void tickLinks();
        /** The pairs' linked values again (the structure changed). */
        void relink();
        /** A [SEL] key (0-13, 14 the master's): true when the page took it (SETUP page 3 puts
            the fader in the cursor's group, REMOTE page 1 shows its assignments). */
        bool pressSel (int fader);

        /** REMOTE page 1 (p.194) is showing: the faders and [ON] keys work what it assigns. */
        bool isRemoteActive() const noexcept { return function == Function::Remote && page == 0; }
        /** What control 0-13 or 14 (the master) works in the bank (invalid: NO ASSIGN). */
        KnobBinding remoteFader (int control);
        KnobBinding remoteOn (int control);
        int getRemoteBank() const noexcept { return panel.bank; }
        void setRemoteBank (int);
        /** The three columns of a control's assignment, as the page prints them. */
        juce::String remoteLabel (bool on, int control, int column) const;
        /** Called when what REMOTE's controls work may have changed. */
        std::function<void()> onRemoteChanged;

        /** A delay as the pages print it: in DELAY UNITS (p.203). */
        juce::String delayText (float ms) const;
        /** The library pages (EQ and DYNAMICS page 2, EFFECT page 2): the program the list is on (1-based). */
        int getLibraryNumber() const;
        /** A library's (0 EQ, 1 dynamics, 2 effects) program's title (empty: nothing stored there). */
        juce::String getLibraryTitle (int kind, int number) const;

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
            bool tiny = false;   // a Button's or Switch's caption in the small font
            bool centred = false; // a Text field's value in its middle
            bool custom = false;  // drawn by its page: only the cursor here
            bool plain = false;   // a Switch that names its state: never filled
            std::function<bool()> enabled; // unset: always; a Button drawn gray while false (RECALL UNDO)
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
        void buildMidiSetup();
        void drawMidiSetup();
        void buildMidiProgram();
        void drawMidiProgram();
        void buildMidiControl();
        void drawMidiControl();
        void buildMidiBulk();
        void drawMidiBulk();
        void drawMidiLocal();
        /** A MIDI SETUP switch of the Yamaha01V (`what`: its Setup value). */
        KnobBinding midiSwitch (int what, const juce::String& label);
        /** The channel field of MIDI SETUP. */
        KnobBinding midiChannel (int what, const juce::String& label);
        /** The choices of one of MIDI page 3's three columns, for the current assignment. */
        std::vector<int> ccChoices (int column, int code) const;
        void saveMidiSettings();
        void initializeTable (bool program);
        /** MIDI page 4's REQUEST (false) or TRANSMIT (true) of the selected category. */
        void bulk (bool transmit);
        void buildEffectEdit();
        void drawEffectEdit();
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
        // The pages of the second pass (SETUP, the outputs, REMOTE, the libraries).
        void header (const juce::String&, int y);
        juce::String delayUnitLabel() const;
        /** A pair's two strips share what the manual lists (p.52); `a`'s values go onto `b`. */
        void linkStrips (Strip& a, Strip& b, bool input);
        void drawWordClock();
        void buildMonitorSetup();
        void drawMonitorSetup();
        void buildGroups();
        void drawGroups();
        void buildPairs();
        void drawPairs();
        void drawDither();
        void buildBusMaster();
        void drawBusMaster();
        void buildOmni();
        void drawOmni();
        void buildOutputDelay();
        void drawOutputDelay();
        void drawOptionMeter (bool outputs);
        void buildOptionOut();
        void drawOptionOut();
        void buildSwap();
        void drawSwap();
        void buildRemote();
        void drawRemote();
        void buildLibrary (int kind);
        void drawLibrary (int kind);
        /** The library the page shows: 0 EQ, 1 dynamics, 2 effects; -1 none. */
        int libraryKind() const noexcept;
        struct Program
        {
            juce::String title, token;
            std::vector<float> values;
        };
        /** A library's programs: its presets (effects: the 01V's types), then the user's up to its last. */
        int libraryPresets (int kind) const;
        int libraryLast (int kind) const;
        const Program* userProgram (int kind, int number) const;
        void storeLibrary (int kind);
        void recallUserProgram (int kind, int number);
        void editLibraryTitle (int kind);
        void loadLibrary();
        void saveLibrary();
        void loadPanel();
        void savePanel();
        void initRemoteBank (int bank);
        KnobBinding remoteColumn (bool on, int column);
        KnobBinding remoteBinding (bool on, int target);
        Strip* remoteStrip (int code) const;
        /** The strip and side feeding output `port` (nullptr: none of the 01V's). */
        Strip* outputStrip (int port, int& side) const;
        /** An output's source as OMNI OUT SELECT counts them: AUX 1-4, BUS 1-4, ST L, ST R, CH 1-16 (-1: another). */
        int outputChoice (int port) const;
        void setOutputChoice (int port, int choice);
        void swapInputs (int channel);
        bool isSwapped (int channel) const;
        void confirmPair (int pair);
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
        void undoRecall();

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
        int midiProgram = 0, midiControl = 1, bulkCategory = 0, bulkFrom = 0, bulkTo = 1;
        int page = 0, fxScroll = 0, library = -1, eqBand = 1;
        struct Panel
        {
            std::array<int, 14> faderGroup {}, muteGroup {};
            std::array<bool, 6> groupOn { { true, true, true, true, true, true } };
            std::array<bool, 10> pair {};
            bool soloOn = true, lastSolo = false;
            int bank = 0;
            /** Each bank: the 15 faders' targets, then the 15 [ON] keys'. */
            std::array<std::array<int, 30>, 4> remote {};
        };
        Panel panel;
        int remoteChannel = 0;
        // The groups' and pairs' last values, to see what moved.
        std::array<float, 14> lastFader {};
        std::array<bool, 14> lastMute {};
        bool linksPrimed = false;
        // The groups and pairs go with the show and its scenes: the engine's console text.
        juce::String groupsText() const;
        bool adoptGroupsText (const juce::String&);
        void adoptEngineGroups();
        uint64_t seenConsole = 0;
        template <typename T>
        struct Twin
        {
            std::atomic<T>* a;
            std::atomic<T>* b;
            T last;
        };
        std::vector<Twin<float>> twinFloats;
        std::vector<Twin<bool>> twinBools;
        std::vector<Twin<int32_t>> twinInts;
        struct TwinModel
        {
            Strip* a;
            Strip* b;
            int last;
        };
        std::vector<TwinModel> twinModels;
        /** The user programs: [kind] by number. */
        std::array<std::map<int, Program>, 3> programs;
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
    ConsoleTape& getTape (int i) { return *tapes[(size_t) juce::jlimit (0, kStrips, i)]; }
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
    void pressSolo (Strip*);
    /** The 15/16 - 2TR IN switch (p.77): CH 15/16 take the 2TR IN jacks. */
    void pressTwoTrack();
    y01v::Key& key (const juce::String& name, juce::Colour face, bool led, juce::Rectangle<int>);
    y01v::Knob& knob (const juce::String& name, juce::Colour cap, juce::Colour pointer, juce::Rectangle<int>);
    int sendBus() const noexcept;
    /** REMOTE's fader mode with its page 1 showing: the faders and [ON] keys work its assignments. */
    bool remoteWorks() const noexcept;

    UiContext& ctx;
    const universal::Theme& theme;
    universal::ToggleKey universalKey;
    y01v::Display display;
    y01v::Meter meter;
    y01v::Wheel wheel;

    std::vector<std::unique_ptr<y01v::Fader>> faders;
    std::vector<std::unique_ptr<ConsoleTape>> tapes; // Sidecar's console tape under each fader; the last is the master's
    std::unique_ptr<y01v::Fader> masterFader;
    std::vector<std::pair<juce::String, std::unique_ptr<y01v::Key>>> keys;
    std::vector<std::pair<juce::String, std::unique_ptr<y01v::Knob>>> knobs;

    y01v::FaderMode mode = y01v::FaderMode::Home;
    int eqBand = 1;
    Strip* boundStrip = nullptr;
    /** REMOTE: what the 14 [ON] keys and the master's work. */
    std::array<KnobBinding, 15> remoteOn;
    std::atomic<float> contrastValue { 0.75f };
    bool soloLit = false;
    /** The 26 dB PAD switches of CH 1-12. */
    std::array<bool, 12> pad {};
    juce::AffineTransform design;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Y01VSurface)
};

} // namespace livedaw::ui
