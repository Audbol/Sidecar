#pragma once

#include "engine/FaderLaw.h"
#include "ui/Knobs.h"
#include "ui/Skin.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace livedaw::ui
{

class Dm2000Surface;

namespace dm2k
{
    /** The LCD's page groups: what each [DISPLAY] key calls (V2 Owner's Manual p.53). */
    enum class Group
    {
        MatrixSend,
        AuxSend,
        EncoderMode,
        Effects,
        Routing,
        PhaseInsert,
        Delay,
        Dynamics,
        Pan,
        Equalizer,
        SceneMemory,
        Automix,
        TrackArming,
        UserKeys,
        Locator,
        Monitor,
        Data,
        Dio,
        Setup,
        Utility,
        Midi,
        Remote,
        Meter,
        View,
        Pair,
        FaderGroup,
        InputPatch,
        OutputPatch,
        kCount
    };

    /** A key of the panel: a rubber block in the look of the DM2000's, with a slit
        LED across its top. */
    class Key final : public juce::Button
    {
    public:
        enum class Shape
        {
            Block,   // a strip's SEL, SOLO, ON: wide, flat-faced
            Square,  // the small square ones on the black sections
            Diamond, // a section's [DISPLAY]: a square standing on its corner
            Big      // the transport's, the cursor keys, ENTER, Scene STORE and RECALL
        };

        Key (const juce::String& name, juce::Colour face, juce::Colour led, Shape);

        std::function<bool()> isOn;
        void setGlyph (const juce::String& text) { glyph = text; }
        void refresh();
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        juce::Colour face, led;
        Shape shape;
        juce::String glyph;
        bool shownOn = false;
    };

    /** A knob: a knurled skirt, a coloured top, a pointer line. A click without a
        drag presses it (FREQUENCY/Q's F and Q, an encoder's push). */
    class Knob final : public juce::Component
    {
    public:
        Knob (juce::Colour skirt, juce::Colour top, juce::Colour ring = {});

        void setBinding (KnobBinding);
        const KnobBinding& getBinding() const noexcept { return binding; }
        std::function<void()> onPress, onTouch;
        void refresh();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        void moveTo (float position);

        juce::Colour skirt, top, ring;
        KnobBinding binding;
        float shown = -1.0f, dragStart = 0.0f;
        bool dragged = false;
    };

    /** The data wheel: brushed metal with a finger dimple. */
    class Wheel final : public juce::Component
    {
    public:
        std::function<void (int)> onTurn;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        float angle = 3.8f, last = 0.0f, pending = 0.0f;
    };

    /** A fader's printed legend: where the panel prints each level on the travel (0 the bottom, 1 the
        top), as (position, dB) pairs from the top down, measured on a straight-on photograph of a real
        console. `master`: the 0 to 70 legend (the STEREO fader's, and the dim one right of each
        channel fader's slot), else the +10 to 50 one. The legend is not the fader law: Yamaha's 10-bit
        table has -20 dB where the panel prints 15. */
    const std::vector<std::pair<float, float>>& printedLegend (bool master);

    /** A motor fader with its printed scales: a channel strip has the +10 to 50 legend left of the slot
        and the Output Channels' 0 to 70 legend, dimmer, right of it; the STEREO fader the 0 to 70 one.
        The cap sits where the fader law puts the level; the legends are where the panel prints them. */
    class Fader final : public juce::Component
    {
    public:
        explicit Fader (bool masterScale, const FaderLaw& law);

        void bind (std::atomic<float>* value, const FaderLaw&);
        bool isBound() const noexcept { return value != nullptr; }
        double getPosition() const noexcept { return position; }
        void setPositionFromUser (double);
        std::function<void()> onTouch;
        void refresh();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        juce::Rectangle<float> track() const noexcept;

        bool master;
        FaderLaw law;
        std::atomic<float>* value = nullptr;
        double position = 0.0, dragStart = 0.0;
    };

    /** A channel strip's vacuum fluorescent display (p.55): its encoder's arc in
        orange, the routing and EQ, INS, DLY, COMP, GATE indicators, the name in
        cyan dots; the selected channel's border lights. */
    class StripDisplay final : public juce::Component
    {
    public:
        struct State
        {
            juce::String name;
            /** What's printed over the name for a moment (a fader's level, a held [SEL]). */
            juce::String value;
            bool arc = false, bipolar = false, selected = false, eq = false, ins = false, dly = false, comp = false, gate = false, muted = false;
            float position = 0.0f;
            /** Bits: buses 1-8, then S, then D. */
            int routes = 0;
            bool operator== (const State&) const;
        };
        void set (const State&);
        const State& get() const noexcept { return state; }
        void paint (juce::Graphics&) override;

    private:
        State state;
    };

    /** A red seven-segment readout: EQ band values, the scene number. */
    class Readout final : public juce::Component
    {
    public:
        explicit Readout (int digits);
        std::function<juce::String()> text;
        void refresh();
        void paint (juce::Graphics&) override;
        const juce::String& getText() const noexcept { return shown; }

    private:
        int digits;
        juce::String shown;
    };

    /** The 320 x 240 dot LCD (pp.52-54): the title bar, the page, four tabs and their
        arrows. Everything is drawn as dots on a bitmap. The cursor keys move a cursor over
        the page's fields, the wheel or INC/DEC change the one under it, ENTER presses it. */
    class Display final : public juce::Component
    {
    public:
        static constexpr int kDotsW = 320, kDotsH = 240, kDot = 2;
        static constexpr int kWidth = kDotsW * kDot, kHeight = kDotsH * kDot;

        explicit Display (UiContext&);

        /** A [DISPLAY] key: the group, or if that's showing, its next page (p.53). */
        void pressGroup (Group);
        /** A [DISPLAY] key held: the group's previous page. */
        void previousInGroup (Group);
        void showGroup (Group);
        void showPage (Group, int page);
        Group getGroup() const noexcept { return group; }
        int getPage() const noexcept { return pages[(size_t) group]; }
        int getNumPages() const;
        juce::String getTabName (int page) const;
        /** F1-F4: the visible tabs; the arrows beside them scroll more into view. */
        void pressF (int key);
        void scrollTabs (int delta);
        int getTabScroll() const noexcept { return tabScroll[(size_t) group]; }
        /** DISPLAY HISTORY: up to eight pages. */
        void history (int delta);

        void cursor (int dx, int dy);
        void turn (int delta);
        void enter();
        void pick (juce::Point<int> dot);

        /** The structure or the selection changed: the page's fields again. */
        void rebuild();
        /** The selection changed: the paged-by-layer pages follow it (p.53). */
        void selectionChanged();
        void refresh();

        // What the panel's select keys set and the pages read.
        void setAux (int a) { aux = juce::jlimit (0, 11, a); rebuild(); }
        int getAux() const noexcept { return aux; }
        void setMatrix (int m) { matrix = juce::jlimit (0, 3, m); rebuild(); }
        int getMatrix() const noexcept { return matrix; }
        void setFxUnit (int u) { fxUnit = juce::jlimit (0, 7, u); rebuild(); }
        int getFxUnit() const noexcept { return fxUnit; }
        void setGeqUnit (int u) { geqUnit = juce::jlimit (0, 5, u); rebuild(); }
        int getGeqUnit() const noexcept { return geqUnit; }
        int getMemory() const noexcept { return memory; }
        void setMemory (int m);
        void storeScene();
        void recallScene();
        /** The page's first four knobs: what the four rotaries under the tabs work. */
        KnobBinding getSoftBinding (int knob) const;
        void setBand (int b) noexcept { band = b; }
        void setFreqNotQ (int b, bool f) noexcept { freqNotQ[(size_t) juce::jlimit (0, 3, b)] = f; }
        bool isFreqNotQ (int b) const noexcept { return freqNotQ[(size_t) juce::jlimit (0, 3, b)]; }

        /** ENCODER MODE ASSIGN 1-4's parameters (p.62), as AssignItem. */
        int getAssign (int key) const noexcept { return assign[(size_t) juce::jlimit (0, 3, key)]; }
        void setAssign (int key, int item);
        int getAssignKey() const noexcept { return assignKey; }
        void setAssignKey (int k) { assignKey = juce::jlimit (0, 3, k); rebuild(); }

        //======================================================================
        // MIDI (pp.217-219). The setup is Dm2000Midi's 28 numbers (0 the Rx channel and 1 the Tx
        // channel, 1-16; then Tx, Rx, OMNI and ECHO of program change, control change, parameter
        // change, bulk, MTC and the others; 26 NRPN, 27 FADER LOW), kept in the app settings and
        // given to the console's MIDI when there is one.
        int getMidiSetup (int index) const;
        void setMidiSetup (int index, int value);
        /** The scene program change `program` (0-127) recalls: 0-99, 100 none. */
        int getPcScene (int program) const;
        void setPcScene (int program, int scene);
        void initializePcTable();

        //======================================================================
        // The libraries (pp.165-172). A user memory is a file in the library folder (a channel or
        // FX preset of Sidecar's, a patch as text); the presets are the ones Sidecar can make.
        enum class Library
        {
            Channel,
            Eq,
            Gate,
            Comp,
            Effects,
            InputPatch,
            OutputPatch,
            kCount
        };
        /** Where the memories are kept (<documents>/Library/DM2000 unless set: the tests). */
        void setLibraryFolder (const juce::File&);
        int firstLibraryMemory (Library) const;
        int firstUserMemory (Library) const;
        int lastLibraryMemory (Library) const;
        /** A memory's title: empty for an empty user memory or a preset Sidecar doesn't have. */
        juce::String libraryTitle (Library, int memory) const;
        bool isLibraryPreset (Library l, int memory) const { return memory < firstUserMemory (l); }
        /** STORE the selected channel's (the effects processor's, the patch's) settings; false for a preset. */
        bool storeLibrary (Library, int memory, const juce::String& title);
        bool recallLibrary (Library, int memory);
        bool clearLibrary (Library, int memory);
        int getLibraryChoice (Library l) const noexcept { return libraryChoice[(size_t) l]; }
        void setLibraryChoice (Library, int memory);

        //======================================================================
        // OUTPUT PATCH (pp.79-81). A port takes one of kNumOutputChoices signals: 0 none, 1-8 BUS,
        // 9-20 AUX, 21-28 MATRIX 1L-4R, 29-30 STEREO L and R. On Sidecar a signal feeds one port.
        static constexpr int kNumOutputChoices = 31;
        /** The engine's ports (livedaw_engine::ports::dm2000_ports). */
        static constexpr int kPortOmni = 2, kPortSlot = 24, kPort2tr = 120, kNumPorts = 126;
        static juce::String outputChoiceName (int choice);
        int getOutputPatch (int port) const;
        void setOutputPatch (int port, int choice);
        /** The Direct Out destinations: 0 none, 1-96 the slots' outputs, 97-104 OMNI OUT, 105-110 2TR. */
        static constexpr int kNumDirectDestinations = 111;
        static juce::String directDestinationName (int destination);
        /** Input Channel `channel`'s (0-95) destination, kept while its Direct Out is off. */
        int getDirectOut (int channel) const;
        void setDirectOut (int channel, int destination);
        bool isDirectOn (int channel) const;
        void setDirectOn (int channel, bool);
        bool isDirectPost() const noexcept { return directPost; }
        void setDirectPost (bool);

        //======================================================================
        // PAIR (pp.144-145), horizontal pairs: Input Channels 2n+1 and 2n+2 (48 pairs), BUS 1-8
        // (output pairs 0-3) and AUX 1-12 (4-9). The surface keeps a pair alike while it is shown.
        bool isPaired (bool output, int pair) const;
        void setPaired (bool output, int pair, bool);
        /** What changed on one of a pair goes to the other (the surface's timer). */
        void followPairs();

        /** CHANNEL COPY and PASTE (p.155), by PREFER2's Channel Copy Parameter. */
        void copyChannel();
        bool pasteChannel();
        /** Channel Copy Parameter's bits: 0 ALL, 1 FADER, 2 ON, 3 PAN, 4 AUX, 5 AUX ON, 6 EQ. */
        int getCopyParameters() const;
        void setCopyParameters (int bits);

        //======================================================================
        // USER DEFINED KEYS (p.270): one bank of 16, each one of userKeyNames().
        static const juce::StringArray& userKeyNames();
        int getUserKey (int key) const noexcept { return userKeys[(size_t) juce::jlimit (0, 15, key)]; }
        void setUserKey (int key, int function);
        void pressUserKey (int key);
        bool isUserKeyOn (int key) const;

        /** AUTOMIX: fader and ON moves recorded against a running clock (or the scene recall), played back. */
        enum class AutomixKey { Enable, Rec, Run, Clear, Undo, Clock, Fader, On };
        void automixPress (AutomixKey);
        bool automixIs (AutomixKey) const;
        void automixTick();
        /** TRACK ARMING / LOCATOR: the multitrack recorder's tracks and markers (LOCATE MEMORY 1-8). */
        int getArmPage() const noexcept { return armPage; }
        bool isTrackArmed (int track) const;
        void toggleTrackArm (int track);
        void locateMemory (int n);
        void transport (int command);

        /** PREFER1's preferences that do something here (p.274). */
        enum class Pref
        {
            AutoRouting,
            AutoPhase,
            AutoDelay,
            AutoAux,
            AutoDynamics,
            AutoPan,
            AutoEq,
            AutoChannelSelect,
            StoreConfirmation,
            RecallConfirmation,
            kCount
        };
        bool preference (Pref) const;
        void setPreference (Pref, bool);
        /** Whether a control of `group` brings its page up (the Auto ... Display preferences). */
        bool autoDisplay (Group) const;

        /** The oscillator (p.279): waveform 0 SINE 100Hz, 1 SINE 1kHz, 2 SINE 10kHz, 3 PINK NOISE,
            4 BURST NOISE; assignment 0-7 BUS, 8-19 AUX, 20-27 MATRIX 1L-4R, 28-29 STEREO L, R. */
        void setOscillator (bool on, int waveform, float levelDb);
        bool isOscillatorOn() const noexcept { return oscOn; }
        void setOscillatorAssign (int index, bool);

        /** A message over the page for two seconds; a confirmation the cursor answers (p.54). */
        void showMessage (const juce::String&);
        juce::String getMessage() const;
        void confirm (const juce::String& question, std::function<void()> yes);
        bool isConfirming() const noexcept { return confirmGo != nullptr; }
        void answer (bool yes);

        /** The Output Channels holding GEQs (six at most, p.183), in unit order. */
        std::vector<Strip*> geqStrips() const;
        /** Puts a GEQ on `strip`; false when all six are in use or it can't hold one. */
        bool insertGeq (Strip&);
        void removeGeq (Strip&);
        static constexpr int kMaxGeqs = 6;

        std::function<void()> changed;

        int getNumFields() const noexcept { return (int) cells.size(); }
        int getFocus() const noexcept { return focus; }
        void setFocus (int i);
        juce::String getFocusText() const;
        juce::String getFieldCaption (int i) const;
        /** The field captioned `caption` (-1: none), for the tests. */
        int findField (const juce::String& caption) const;
        /** The dots, for the tests. */
        bool isDotOn (int x, int y) const noexcept;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        struct Cell
        {
            enum Kind
            {
                Text,   // a value, in a box if `boxed`, in a dotted box if `dotted`
                Toggle, // a switch: its caption or value, reversed when on
                Dial,   // a knob: value under it, name over it
                Line,   // a knob with its name and value beside it (EQ, dynamics)
                Press,  // a button: ENTER presses it
                Mark,   // a grid's dot: filled when on, a dash when off
                Slide,  // a vertical fader
                Check   // a check box: crossed when on
            };
            Kind kind = Text;
            juce::Rectangle<int> area;
            KnobBinding binding;
            juce::String caption;
            std::function<void()> press;
            bool boxed = false, dotted = false, big = false;
            int radius = 6;
        };

    private:
        struct Tab
        {
            const char* name;
            void (Display::*build) (int);
            void (Display::*draw) (int);
            int arg;
        };
        const std::vector<Tab>& tabs() const;
        static const std::vector<Tab>& tabsOf (Group);
        Strip* selected() const;
        Strip* layerStrip (int layer, int i) const;
        juce::String idOf (const Strip*) const;
        juce::String shortName (const Strip*) const;

        // The dots.
        void clear();
        void dot (int x, int y, bool on = true);
        void hLine (int x0, int x1, int y, bool on = true);
        void vLine (int x, int y0, int y1, bool on = true);
        void box (juce::Rectangle<int>, bool on = true);
        void fill (juce::Rectangle<int>, bool on = true);
        void dottedH (int x0, int x1, int y);
        void dottedV (int x, int y0, int y1);
        void dottedBox (juce::Rectangle<int>);
        int textWidth (const juce::String&, bool tiny = false) const;
        void text (int x, int y, const juce::String&, bool tiny = false, bool on = true);
        void textCentred (juce::Rectangle<int>, const juce::String&, bool tiny = false, bool on = true);
        void textTall (int x, int y, const juce::String&, bool on = true);
        void inverseText (juce::Rectangle<int>, const juce::String&, bool tiny = false);
        void circle (int cx, int cy, int r);
        void disc (int cx, int cy, int r, bool on = true);
        void dial (int cx, int cy, int r, float value, bool filled = false);
        void frame (juce::Rectangle<int>, const juce::String& title);
        void pageTitle (const juce::String& title, const juce::String& channel = {});
        void heart (int cx, int cy, bool whole);

        // The page's cells.
        Cell& add (Cell::Kind, juce::Rectangle<int>, KnobBinding, const juce::String& caption = {});
        Cell& button (juce::Rectangle<int>, const juce::String& caption, std::function<void()> press);
        void drawCell (const Cell&, bool focused);
        void drawChrome();
        void drawTabs();
        void renderImage();

        // The pages. `arg` is a layer (0-3) or a source set.
        void buildNone (int);
        void drawNone (int);
        void buildEq (int);
        void drawEq (int);
        void buildAtt (int);
        void drawAtt (int);
        void buildGate (int);
        void drawGate (int);
        void buildComp (int);
        void drawComp (int);
        void buildPhase (int);
        void drawPhase (int);
        void buildInsert (int);
        void drawInsert (int);
        void buildDelay (int);
        void drawDelay (int);
        void buildPan (int);
        void drawPan (int);
        void buildRouting (int);
        void drawRouting (int);
        void buildBusToStereo (int);
        void drawBusToStereo (int);
        void buildAux (int);
        void drawAux (int);
        void buildMatrix (int);
        void drawMatrix (int);
        void buildAssign (int);
        void drawAssign (int);
        void buildView (int);
        void drawView (int);
        void buildFaderView (int);
        void drawFaderView (int);
        void buildMeter (int);
        void drawMeter (int);
        void buildGroupGrid (int);
        void drawGroupGrid (int);
        void buildGroupMaster (int);
        void drawGroupMaster (int);
        void buildFxEdit (int);
        void drawFxEdit (int);
        void buildLibrary (int);
        void drawLibrary (int);
        void buildMidiSetup (int);
        void drawMidiSetup (int);
        void buildMidiProgram (int);
        void drawMidiProgram (int);
        void buildMidiControl (int);
        void drawMidiControl (int);
        void buildOutPatch (int);
        void drawOutPatch (int);
        void buildDirectOut (int);
        void drawDirectOut (int);
        void buildPair (int);
        void drawPair (int);
        void buildPrefs (int);
        void drawPrefs (int);
        void buildOscillator (int);
        void drawOscillator (int);
        void buildChannelStatus (int);
        void drawChannelStatus (int);
        void buildWordClock (int);
        void drawWordClock (int);
        void buildUserKeys (int);
        void drawUserKeys (int);
        void buildRemote (int);
        void drawRemote (int);
        void buildGeq (int);
        void drawGeq (int);
        void buildScene (int);
        void drawScene (int);
        void buildPatch (int);
        void drawPatch (int);
        void buildMonitor (int);
        void drawMonitor (int);
        void buildArming (int);
        void drawArming (int);
        void buildLocator (int);
        void drawLocator (int);
        void buildData (int);
        void drawData (int);
        void buildAutomix (int);
        void drawAutomix (int);
        void buildOutInsert (int);
        void drawOutInsert (int);

        void levelMeter (int x, int y, int h, float db, int width = 3);
        void meterScale (int x, int y, int h, bool labels = true);
        void stripMeters (Strip&, juce::Rectangle<int>);
        void eqCurve (Strip&, juce::Rectangle<int>);
        void dynCurve (const ld::DynSlotParams&, juce::Rectangle<int>, bool gateScale);
        void paramLine (const std::atomic<float>* values, const models::Model&, const char* id, const juce::String& name, juce::Rectangle<int>);
        juce::Array<int> fxTypes() const;
        juce::String sceneTitle (int m) const;
        void doRecallScene();
        struct AutomixEvent { double t; int strip; bool on; float value; };
        std::vector<AutomixEvent> amEvents, amBackup;
        std::vector<float> amFader;
        std::vector<char> amOn;
        bool amEnable = true, amRec = false, amRun = false, amSceneClock = false, amFaders = true, amOns = true;
        double amStart = 0.0, amNow = 0.0;
        size_t amNext = 0;
        int armPage = 0, dataRow = 0;
        juce::StringArray midiTokens (bool pcTable) const;
        void setMidiTokens (bool pcTable, const juce::StringArray&);
        juce::File libraryFile (Library, int memory) const;
        juce::File libraryFolderOf (Library) const;
        juce::String presetTitle (Library, int memory) const;
        juce::String patchText (Library) const;
        bool applyPatchText (Library, const juce::String&);
        int directPort (int channel) const;
        std::pair<Strip*, Strip*> pairStrips (bool output, int pair) const;
        uint64_t pairHash (Strip&) const;
        void pairCopy (Strip& from, Strip& to) const;
        void savePairs() const;
        void applyOscillator();
        void drawWindow (juce::Rectangle<int>);

        UiContext& ctx;
        Group group = Group::Equalizer;
        std::array<int, (size_t) Group::kCount> pages {}, tabScroll {};
        std::vector<Cell> cells;
        int focus = 0, band = 1, aux = 0, matrix = 0, fxUnit = 0, geqUnit = 0, memory = 0, assignKey = 0, sceneScroll = 0;
        int midiProgram = 0, midiControl = 1, midiTable = 0, statusSlot = 0;
        std::array<int, (size_t) Library::kCount> libraryChoice {};
        juce::File libraryRoot;
        struct LibraryFiles
        {
            bool scanned = false;
            std::map<int, juce::File> files;
        };
        mutable std::array<LibraryFiles, (size_t) Library::kCount> libraryFiles;
        std::array<int, 96> directDestination {};
        bool directPost = true;
        uint64_t inputPairs = 0;
        uint32_t outputPairs = 0;
        std::array<uint64_t, 116> pairSeen {}; // each pair's two sides, as last seen alike
        bool copied = false;
        StripKind copiedKind = StripKind::Channel;
        int copiedNumber = 0;
        std::array<int, 16> userKeys {};
        bool oscOn = false;
        int oscWave = 1;
        float oscLevelDb = -20.0f;
        std::array<bool, 30> oscAssign {};
        juce::String message, confirmText;
        juce::uint32 messageUntil = 0;
        int focusBeforeConfirm = 0;
        std::function<void()> confirmGo;
        std::array<bool, 4> freqNotQ { true, true, true, true };
        std::array<int, 4> assign { 1, 2, 3, 4 };
        std::vector<std::pair<Group, int>> trail;
        int trailAt = -1;
        std::vector<uint8_t> dots, shownDots;
        juce::Image image;
        float dragStart = 0.0f;
    };

    /** ENCODER MODE ASSIGN's parameters Sidecar has (p.63). */
    const juce::StringArray& assignNames();
    /** Encoder binding of assign item `item` on `strip` (invalid: the encoder is inactive on it). */
    KnobBinding bindAssign (UiContext&, Strip&, int item);
    /** Comp and insert positions (0 PRE EQ, 1 PRE FADER, 2 POST FADER) <-> process order (p.136). */
    int processOrder (int comp, int insert, bool insertFirst) noexcept;
    void decodeOrder (int order, int& comp, int& insert, bool& insertFirst) noexcept;
    /** An attenuator's binding, -96 to +12 dB in 0.1 dB (p.343). */
    KnobBinding bindAttenuator (Strip&);
    /** A strip's delay in samples, 0-43400 (p.342). */
    KnobBinding bindDelay (Strip&);
    /** PAN, L63 - C - R63 (p.96). */
    KnobBinding bindPanYamaha (Strip&);
    /** A value as the LCD prints it: "125", "1.00k", "-1.9". */
    juce::String lcdValue (const KnobBinding&);
    void stepBy (const KnobBinding&, int delta);
} // namespace dm2k

/** The Yamaha DM2000 Version 2's surface: 24 strips and the master, channel name displays, the
    Selected Channel, Display Access, the 320 x 240 dot LCD. From the V2 Owner's Manual's control
    surface chapter (pp.20-44) and photographs of real consoles, kept outside the repository; no
    maker's logo or artwork is used. */
class Dm2000Surface final : public Skin,
                            private juce::Timer
{
public:
    explicit Dm2000Surface (UiContext&);
    ~Dm2000Surface() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshAll() override;
    void selectionChanged() override;
    void showChanged() override;

    static constexpr int kStrips = 24;
    static constexpr int kDesignWidth = 1920, kDesignHeight = 1080;

    enum Layer
    {
        Layer1,
        Layer25,
        Layer49,
        Layer73,
        LayerMaster,
        LayerRemote1,
        LayerRemote2,
        LayerRemote3,
        LayerRemote4
    };
    enum class FaderMode
    {
        Fader,
        AuxMtrx
    };
    enum class EncoderMode
    {
        Pan,
        AuxMtrx,
        Assign1,
        Assign2,
        Assign3,
        Assign4
    };

    void setLayer (int);
    int getLayer() const noexcept { return layer; }
    void setFaderMode (FaderMode);
    FaderMode getFaderMode() const noexcept { return faderMode; }
    void setEncoderMode (EncoderMode);
    EncoderMode getEncoderMode() const noexcept { return encoderMode; }
    void setAuxBank (int bank);
    int getAuxBank() const noexcept { return auxBank; }
    void setDynamicsShowsComp (bool);
    bool dynamicsShowsComp() const noexcept { return showComp; }
    /** A transport key: its MMC command goes out on the console's MIDI (false: there is none). */
    bool sendMmc (int command);

    /** The engine strip of a physical strip on this layer (nullptr: none: a remote layer,
        the Master Layer's strips past Matrix 4). */
    Strip* stripAt (int i) const;
    Strip* stereoStrip() const;

    dm2k::Display& getDisplay() noexcept { return display; }
    dm2k::Fader& getFader (int i) { return *faders[(size_t) juce::jlimit (0, kStrips - 1, i)]; }
    dm2k::Fader& getStereoFader() noexcept { return *stereoFader; }
    dm2k::Knob& getEncoder (int i) { return *encoders[(size_t) juce::jlimit (0, kStrips - 1, i)]; }
    dm2k::StripDisplay& getStripDisplay (int i) { return *names[(size_t) juce::jlimit (0, kStrips - 1, i)]; }
    dm2k::Key* findKey (const juce::String& name) const;
    dm2k::Knob* findKnob (const juce::String& name) const;
    dm2k::Readout* findReadout (const juce::String& name) const;
    /** One tick of the timer that moves the faders, keys and displays (the screenshots). */
    void timerCallbackForTest() { timerCallback(); }

private:
    void timerCallback() override;
    void bindStrips();
    void bindSelected();
    void bindSoft();
    void refreshNames();
    void autoShow (dm2k::Group);
    void autoSelect (int strip);
    Strip* selected() const { return ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr; }
    dm2k::Key& key (const juce::String& name, juce::Colour face, juce::Colour led, dm2k::Key::Shape, juce::Rectangle<int>);
    dm2k::Knob& knob (const juce::String& name, juce::Colour skirt, juce::Colour top, juce::Rectangle<int>, juce::Colour ring = {});
    dm2k::Key& displayKey (const juce::String& name, dm2k::Group, juce::Rectangle<int>);
    void silk (float x, float y, float w, float h, const juce::String& text, float size, juce::Colour, juce::Justification = juce::Justification::centred, bool bold = false);
    void drawnKeys (juce::Rectangle<int> first, int cols, int rows, int dx, int dy);

    struct Silk
    {
        juce::Rectangle<float> area;
        juce::String text;
        float size;
        juce::Colour colour;
        juce::Justification justification;
        bool bold;
    };

    UiContext& ctx;
    const universal::Theme& theme;
    universal::ToggleKey universalKey;
    dm2k::Display display;
    dm2k::Wheel wheel;
    std::vector<std::unique_ptr<dm2k::Fader>> faders;
    std::unique_ptr<dm2k::Fader> stereoFader;
    std::vector<std::unique_ptr<dm2k::Knob>> encoders;
    std::vector<std::unique_ptr<dm2k::StripDisplay>> names;
    std::vector<std::pair<juce::String, std::unique_ptr<dm2k::Key>>> keys;
    std::vector<std::pair<juce::String, std::unique_ptr<dm2k::Knob>>> knobs;
    std::vector<std::pair<juce::String, std::unique_ptr<dm2k::Readout>>> readouts;
    std::vector<Silk> silks;
    std::vector<juce::Rectangle<float>> dummyKeys;

    int layer = Layer1, auxBank = 0;
    FaderMode faderMode = FaderMode::Fader;
    EncoderMode encoderMode = EncoderMode::Pan;
    bool showComp = false;
    Strip* boundStrip = nullptr;
    std::array<juce::uint32, 4> eqGainMoved {};
    std::array<juce::uint32, kStrips> movedAt {};
    std::array<float, kStrips> lastFaderDb {};
    juce::AffineTransform design;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Dm2000Surface)
};

} // namespace livedaw::ui
