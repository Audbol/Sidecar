#pragma once

#include "ui/Skin.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace livedaw
{
class ProMix;
}

namespace livedaw::ui
{

namespace promix
{
    using namespace universal;

    /** The ProMix 01's LCD functions (User's Guide chapter 2, p.15): the keys that
        call them, and the MIDI and UTILITY menus. */
    enum class Function
    {
        Meter,
        Pan,
        Phase,
        Comp,
        Cue,
        Send1,
        Send2,
        Send3,
        Send4,
        EqLow,
        EqMid,
        EqHigh,
        Library,
        Group,
        Pair,
        Utility,
        Midi,
        MemoryStore,  // STORE's and RECALL's scene lists (p.63-64), and the
        MemoryRecall, // title a stored scene takes (TITLE EDIT)
        TitleEdit
    };

    /** A ProMix fader: 128 steps on the console's law (+6 dB to -66 dB, -oo),
        its scale marked as the panel's (6, 0, 5, 10, 20, 40, 60, oo). It moves a
        ProMix parameter (a strip's level, or its send level while a SEND
        function is shown: p.25), through the console's panel operations, so a
        pair or a group follows it. */
    class Fader final : public juce::Component
    {
    public:
        Fader (UiContext&, const Theme&);

        /** The parameter it moves (-1: none). */
        void setParam (int n);
        int getParam() const noexcept { return param; }
        void refresh();
        /** LOCAL OFF (p.86): while `isLocal` says so the fader moves only itself and
            `onLocal` gets its value (0-127); without `onLocal` it sits at the bottom
            (ST IN, RTN / SEND, ST OUT). */
        std::function<bool()> isLocal;
        std::function<void (int)> onLocal;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        juce::Rectangle<float> track() const noexcept;
        float yOf (int step) const noexcept;
        void moveTo (int step);

        UiContext& ctx;
        const Theme& theme;
        int param = -1, step = 0, dragStart = 0, localStep = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Fader)
    };

    /** The ProMix 01's LCD (pp.12-16), laid out as a console's: down its left
        the FUNCTION area (the function shown), the MEMORY area (the title of
        the scene last recalled, the memory number, EDIT once it's changed) and
        SEL CH; at the right the function's page: its parameters as
        fields, one with the cursor; the cursor keys move it, the PARAMETER
        wheel changes its value, ENTER switches, recalls or executes. Unlike
        the console's, the screen takes touch: a tap puts the cursor on a
        field, a drag turns the wheel. */
    class Display final : public juce::Component
    {
    public:
        Display (UiContext&, const Theme&);
        ~Display() override;

        void setFunction (Function);
        Function getFunction() const noexcept { return function; }
        /** The page within the function (PAN's two, a SEND's program list and
            EDIT, COMP's setup and EDIT, METER's two). */
        int getPage() const noexcept { return page; }
        void setPage (int);
        /** The processor COMP shows (0-2). */
        int getProcessor() const noexcept { return processor; }
        void setProcessor (int p);
        /** The strip EQ, PHASE and LIBRARY act on: 0-15 CH1-16, 16 ST IN,
            17-18 RTN1-2, 19 ST OUT. */
        int getEqStrip() const noexcept { return eqStrip; }
        void setEqStrip (int);

        void cursor (int delta);
        void turn (int delta);
        void enter();

        /** MIDI's LOCAL ON/OFF page with Local OFF (p.86): the faders and CH1-16
            [SEL] control nothing here; a fader's move goes out as its control
            change, a [SEL] as a program change. Only while the page shows. */
        bool isLocalOff() const noexcept { return localOff && function == Function::Midi && page == 4; }
        void localMoved (int fader, int value);
        void localSelected (int ch);

        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        /** What the MEMORY area shows besides the number (`chosenMemory`): the
            title of the scene last recalled, and whether the console has
            changed since (its EDIT flag). */
        std::function<juce::String()> memoryName;
        std::function<bool()> memoryEdited;
        /** The scene memories, for STORE's and RECALL's lists: a memory's title
            (00 the initial data; empty: none stored), the chosen number (INC,
            DEC, the wheel), and what ENTER does there (the surface's STORE or
            RECALL); a stored memory's title (TITLE EDIT). */
        std::function<juce::String (int memory)> memoryTitle;
        std::function<int()> chosenMemory;
        std::function<void (int)> chooseMemory;
        std::function<void()> storeChosen, recallChosen;
        std::function<void (int memory, const juce::String&)> setMemoryTitle;
        /** TITLE EDIT for memory `m` (its title now `title`). */
        void editTitle (int m, const juce::String& title);
        /** A program's NAME (STORE on a library page, p.22): user program `n` of
            `library`, from `target`; ENTER stores it. */
        void editProgramName (int library, int n, int target);
        /** A user program was stored (the surface keeps the library). */
        std::function<void()> libraryChanged;
        /** MEMORY PROTECT (UTILITY, p.65): memories 01-25 and 26-50 (the app's settings). */
        std::function<bool (int half)> isProtected;
        std::function<void (int half, bool)> setProtected;
        /** What the RTN / SEND strip is (0 RTN 1 ... 3 SEND 4): the LCD's arrow
            at its legend at the right. */
        std::function<int()> rtnSendShown;
        /** Where the legends beside the LCD sit, as fractions of its height:
            FUNCTION, MEMORY, SEL CH at the left, RTN 1 ... SEND 4 at the right. */
        static constexpr float kLeftRows[] = { 0.19f, 0.53f, 0.86f }, kRightRows[] = { 0.17f, 0.39f, 0.61f, 0.83f };

        /** A field of the page: a ProMix parameter, a program list, or a value of
            the app's (MIDI SETUP). */
        struct Field
        {
            juce::String label;
            std::function<juce::String()> text;
            /** A value's text before it is set (a choice waiting for ENTER). */
            std::function<juce::String (int)> textOf;
            std::function<int()> get;
            std::function<int()> steps;
            std::function<void (int)> set;
            /** ENTER sets the value the wheel chose (PATCH, KEY IN, an Execute,
                a program to recall); the wheel alone only chooses it. */
            bool onEnter = false;
            /** A list (programs): its items' names, shown around the chosen one. */
            std::function<juce::String (int)> item;
            juce::Rectangle<float> box; // in the page's area, 0..1
            int pending = -1;
            /** How it's drawn: a labelled box; its value alone (a cell of EQ's
                table); "LABEL:VALUE" on a line; a key on the screen (STORE,
                RECALL, EDIT); a pan knob (`span` 2: a pair's, over both boxes;
                0: none, its partner's drawn). */
            enum class Style { Box, Bare, Inline, Button, Knob } style = Style::Box;
            int span = 1;
            /** A list's rows (0: as many as fit). */
            int rows = 0;
        };

        const std::vector<Field>& getFields() const noexcept { return fields; }
        int getCursor() const noexcept { return focus; }

    private:
        void build();
        Field paramField (int n, juce::Rectangle<float> box, bool onEnter = false);
        Field listField (const juce::String& label, int library, int count, int param, juce::Rectangle<float> box);
        void row (std::initializer_list<int> params, float y, float h, float x0 = 0.0f, float x1 = 1.0f, bool onEnter = false);
        juce::Rectangle<float> pageArea() const;
        juce::Rectangle<float> boxOf (const Field&) const;
        juce::String title() const;
        /** The FUNCTION area's second line ("No.2", "EDIT", "CH1-16"). */
        juce::String subtitle() const;
        juce::Rectangle<float> leftColumn() const;
        void paintLeftColumn (juce::Graphics&) const;
        /** The link to METER's or PAN's other page (ST IN, CH): where it is. */
        juce::Rectangle<float> pageLink() const;
        void paintField (juce::Graphics&, const Field&, bool focused) const;
        void paintMeters (juce::Graphics&, juce::Rectangle<float>) const;
        void paintEqCurve (juce::Graphics&, juce::Rectangle<float>) const;
        void paintGroups (juce::Graphics&, juce::Rectangle<float>) const;

        UiContext& ctx;
        const Theme& theme;
        ProMix& pm;
        Function function = Function::Meter;
        int page = 0, processor = 0, eqStrip = 0, focus = 0;
        int numPages() const;
        /** TITLE EDIT: the memory and its eight characters (indexes into kTitleChars). */
        int titleMemory = 0, pcProgram = 0, ccParam = 0;
        /** LOCAL ON/OFF (ON at power on), the faders' last values, what was sent last. */
        bool localOff = false;
        std::array<int, 16> localValues {};
        juce::String localMessage;
        /** MEMORY CONTROL CHANGE OUT's data types (bits), and its message. */
        uint32_t ccOutMask = 0xff;
        juce::String ccOutMessage;
        /** METER's PEAK HOLD (p.19), and the peaks it holds (cleared when the page goes). */
        bool peakHold = false;
        mutable std::array<int, 16> peaks {};
        std::array<int, 15> titleChars {};
        /** What TITLE EDIT names: a memory (-1), or a program of library 0-2 (its number, its target), and where it came from. */
        int nameLibrary = -1, nameProgram = 0, nameTarget = 0;
        Function nameFrom = Function::Meter;
        /** A library page's STORE field: the user programs `first`.. `first + count - 1` of `library`, from `target`. */
        Field storeField (int library, int first, int count, int target, juce::Rectangle<float> box);
        std::vector<Field> fields;
        juce::String shown;
        float dragY = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Display)
    };
} // namespace promix

/** The ProMix 01 surface (docs/UNIVERSAL_MIXER.md, Phase 5 step 7): a ProMix 01
    console's skin, laid out as its top panel (User's Guide pp.2-4) and drawn in
    Sidecar's own style (no Yamaha artwork):

    - 16 channel faders, ST IN, RTN / SEND and ST OUT, each with SEL and ON (the
      RTN / SEND SEL picks RTN1, RTN2, SEND3 or SEND4 for its fader and ON); in a
      SEND function the faders are the inputs' send levels and RTN / SEND the
      return or the send master (p.25);
    - the LCD, its function keys (METER, PAN/PHASE, COMP, CUE, SEND1-4, EQ LOW,
      MID, HIGH, LIBRARY; GROUP, PAIR, UTILITY, MIDI), the cursor keys, ENTER
      and the PARAMETER wheel;
    - SCENE MEMORY: its number, STORE, RECALL, INC and DEC (the show's scenes
      1-50; 00 the initial data);
    - the stereo output's LED meter.

    SEL in the GROUP function adds a channel to the group shown or takes it
    out (p.56), in PAIR it picks the pair, in CUE it cues the channel. Shown
    while the console is a ProMix 01 (MainComponent); UNIVERSAL MIXER switches
    to the Universal Mixer. */
class ProMixSurface final : public Skin,
                            private juce::Timer
{
public:
    explicit ProMixSurface (UiContext&);
    ~ProMixSurface() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshAll() override;
    void selectionChanged() override;
    void showChanged() override { display.refresh(); }

    static constexpr int kNumFaders = 19; // CH1-16, ST IN, RTN / SEND, ST OUT

    /** For tests and the screenshots. */
    promix::Display& getDisplay() noexcept { return display; }
    promix::Fader& getFader (int i) { return *faders[(size_t) juce::jlimit (0, kNumFaders - 1, i)]; }
    void pressFunction (promix::Function);
    void pressSel (int fader);
    /** RTN / SEND's strip: 0 RTN1, 1 RTN2, 2 SEND3, 3 SEND4. */
    int getRtnSend() const noexcept { return rtnSend; }
    /** The scene memory number (0-50, 0 scene 00). */
    /** The app settings' MEMORY PROTECT ("lo hi", 0 or 1 each) and user programs (ProMix::getLibraryText). */
    static constexpr const char* protectKey = "promix.memoryProtect";
    static constexpr const char* libraryKey = "promix.library";
    int getMemory() const noexcept { return memory; }
    void setMemory (int m) { memory = juce::jlimit (0, 50, m); repaint(); }
    void store();
    void recall();

private:
    void timerCallback() override;
    void bindFaders();
    int levelParam (int fader) const;
    int onParam (int fader) const;
    int cueParam (int fader) const;
    Strip* stripOf (int fader) const;
    juce::Rectangle<int> meterArea() const;
    juce::Rectangle<int> memoryArea() const;
    /** The panel is laid out at 1920 x 1080 and scaled to the window, in
        proportion and centred: it looks the same on any screen. */
    static constexpr int kDesignWidth = 1920, kDesignHeight = 1080;
    juce::AffineTransform design;
    juce::Rectangle<int> designBounds() const noexcept { return { kDesignWidth, kDesignHeight }; }
    void repaintDesign (juce::Rectangle<int> r) { repaint (r.toFloat().transformedBy (design).getSmallestIntegerContainer().expanded (2)); }
    /** The panel's analog row (PAD and GAIN, MONITOR OUT, PHONES: drawn, not
        controls), and the stereo meter's place, set by resized(). */
    juce::Rectangle<int> analogArea, meterRect, contrastKnob, grilleArea;

    UiContext& ctx;
    const universal::Theme& theme;
    ProMix& pm;
    universal::ToggleKey universalKey;
    promix::Display display;
    std::array<std::unique_ptr<promix::Fader>, kNumFaders> faders;
    std::array<std::unique_ptr<universal::ToggleKey>, kNumFaders> selKeys, onKeys;
    std::vector<std::pair<promix::Function, std::unique_ptr<universal::ToggleKey>>> functionKeys;
    std::vector<std::unique_ptr<universal::ToggleKey>> panelKeys; // STORE, RECALL, INC, DEC, cursors, ENTER
    std::unique_ptr<juce::Slider> wheel;
    int rtnSend = 0, memory = 1, lastBound = -1;
    /** The Edit Buffer as the scene last recalled or stored left it (the LCD's EDIT). */
    std::vector<uint8_t> recalledBytes;
    int recalledIndex = -2;
    /** STORE and RECALL as the keys work them: the first press opens the list,
        the second (or ENTER) stores or recalls the chosen memory. */
    void pressStore();
    void pressRecall();
    /** MEMORY PROTECT of memories 01-25 (`half` 0) or 26-50 (1). */
    bool isProtected (int half) const;
    double wheelLast = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProMixSurface)
};

} // namespace livedaw::ui
