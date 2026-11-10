#pragma once

#include "ui/DLiveSurface.h"
#include "engine/Analysis.h"
#include "engine/ConsoleState.h"
#include "ui/RoutingGroups.h"
#include "ui/universal/MixerConfigPanel.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace livedaw::ui::dl
{

/** The size the C3500's touchscreens are laid out at (scaled to their place on the panel). */
constexpr int kTouchW = 1000, kTouchH = 690;

/** What of a right-screen page the C1500's one screen shows of it (`ProcessingScreen::hostPage`):
    Scenes keeps its own proportions, the others are drawn across the screen's whole width on
    a C3500 and are narrowed to fit, as a C1500's page is. */
inline juce::Rectangle<int> sceneSource() { return { 117, 0, 758, 530 }; }
inline juce::Rectangle<int> wideSource() { return { 10, 0, 866, 530 }; }

/** Something on a screen a value drives: dragged where it is, or touched so that the data
    rotary beside the screen turns it (the manual's Touch & Turn). */
class Touchable : public juce::Component
{
public:
    void setBinding (KnobBinding);
    const KnobBinding& getBinding() const noexcept { return binding; }
    void setTouched (bool);
    void refresh();

    std::function<void (Touchable&)> onTouched;
    /** Tapped without moving. */
    std::function<void()> onTapped;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

protected:
    juce::String valueText() const;

    KnobBinding binding;
    bool touched = false;

private:
    float shown = -2.0f, dragStart = 0.0f;
    juce::String shownText;
};

/** A value in a dark box with a coloured edge and a bar under it (the PEQ's boxes). */
class ParamBox final : public Touchable
{
public:
    explicit ParamBox (juce::Colour edge, const juce::String& caption = {});
    /** Off: only the value (the count boxes). */
    bool showBar = true;
    void paint (juce::Graphics&) override;

private:
    juce::Colour edge;
    juce::String caption;
};

/** A coloured knob on the screen, its title above it and its value under it if it has them. */
class TouchKnob final : public Touchable
{
public:
    explicit TouchKnob (juce::Colour);
    void setColour (juce::Colour);
    juce::String title;
    bool showValue = false;
    juce::Colour valueColour = juce::Colours::white;
    void paint (juce::Graphics&) override;

private:
    juce::Colour colour;
};

/** A filter's picture and its frequency (the screen's HPF and LPF). */
class FilterThumb final : public Touchable
{
public:
    FilterThumb (const juce::String& title, bool high);
    std::function<bool()> isOn;
    void paint (juce::Graphics&) override;

private:
    juce::String title;
    bool high;
};

/** A dark key with a coloured edge. */
class ScreenButton final : public juce::Button
{
public:
    ScreenButton (const juce::String& text, juce::Colour edge);
    std::function<bool()> isOn;
    juce::Colour litFill;
    float fontSize = 15.0f;
    /** A pale key (Mono, Off), or a dark one with a list's arrow. */
    bool pale = false, dropdown = false;
    void refresh();
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Colour edge;
    bool shownOn = false;
};

/** A tab of a screen; `title`: the page's name, in white, not a tab to press. */
class ScreenTab final : public juce::Button
{
public:
    ScreenTab (const juce::String& text, bool title = false);
    std::function<bool()> isOn;
    void refresh();
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool title, shownOn = false;
};

/** The six soft controls' widget at the right of a screen: two blocks of three over three
    layers, the rotaries' colours and values. */
class SoftWidget final : public juce::Component
{
public:
    struct Control
    {
        juce::String label, tag;
        KnobBinding binding;
        juce::Colour colour;
    };
    struct Block
    {
        juce::String title, left, right, thumbText;
        int thumb = 0; // 0 none, 1 a gate's, 2 a compressor's
        ld::DynSlotParams* dyn = nullptr;
        std::atomic<bool>* in = nullptr;
        /** The thumbnail's meters: its level (dBFS) and its gain reduction (dB, negative). */
        std::function<float()> levelDb, reductionDb;
        std::array<Control, 3> controls;
    };

    SoftWidget();
    /** `zigzag`: the rotaries as the panel has them, the middle one to the right. */
    void set (const Block& a, const Block& b, int layer, int numLayers, bool zigzag);
    const Block& getBlock (int i) const noexcept { return blocks[(size_t) i]; }
    void refresh();
    std::function<void (Touchable&)> onTouched;
    std::function<void (int block)> onBlockTapped;
    std::function<void (int layer)> onLayerTapped;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    std::array<Block, 2> blocks;
    std::array<std::unique_ptr<TouchKnob>, 6> knobs;
    juce::Rectangle<int> knobArea (int block, int control) const;
    int layer = 0, numLayers = 3;
    bool zigzag = true;
};

/** The six soft controls of the selected strip, as sends (Aux first + 1...). */
void sendBlocks (MixerEngine&, Strip*, int first, SoftWidget::Block& a, SoftWidget::Block& b);

/** The PEQ's response: a band each in its colour, the filter in green, the sum in yellow. */
class EqGraph final : public juce::Component
{
public:
    explicit EqGraph (UiContext&);
    void setStrip (Strip*);
    /** Takes the strip's values (repaints if they moved). */
    void update();
    /** With handles: the rings to drag, the mouse wheel for a band's width. */
    bool handles = true;
    /** One band alone, on black (the four panels' View); -1: all of them. */
    int onlyBand = -1;
    int getSelectedBand() const noexcept { return selected; }
    void selectBand (int);
    std::function<void()> onBandChanged;
    /** The RTA's latest, drawn behind the curves. */
    void setRta (const Analysis::Bands*);
    juce::String title;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int kPoints = 180;

private:
    float xOf (float hz) const noexcept;
    float yOf (float db) const noexcept;
    float bandGain (int band) const;
    float bandFreq (int band) const;
    bool bandIsCut (int band) const;
    int find (const juce::String& id) const;
    void compute();

    UiContext& ctx;
    Strip* strip = nullptr;
    std::array<float, kPoints> freqs {}, sum {}, filter {};
    std::array<std::array<float, kPoints>, 4> bands {};
    std::vector<float> lastValues;
    const Analysis::Bands* rta = nullptr;
    bool hasBands = false;
    int selected = 1, dragging = -1;
};

/** The PEQ band by band: LF, LM, HM, HF, each with its colour, shape and width (the View key). */
class BandPanels final : public juce::Component
{
public:
    explicit BandPanels (UiContext&);
    void setStrip (Strip*);
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Strip* strip = nullptr;
    std::array<std::unique_ptr<EqGraph>, 4> graphs;
};

/** A dynamics section's input-to-output curve. */
class TransferGraph final : public juce::Component
{
public:
    void setDyn (ld::DynSlotParams* d) { dyn = d; }
    void paint (juce::Graphics&) override;

private:
    ld::DynSlotParams* dyn = nullptr;
};

/** The screens' row of small faders: each strip's colour, fader and name, its mute under it. */
class MiniFaders final : public juce::Component, private juce::Timer
{
public:
    MiniFaders (UiContext&, Desk&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    Strip* stripAt (int column) const;
    int columnAt (int x) const;
    static constexpr int kColumns = 31;
    /** What a strip's small fader stands at (with a mix on the faders, as its fader does). */
    float levelDb (const Strip&) const;
    UiContext& ctx;
    Desk& desk;
    int first = 0, dragFirst = 0;
    juce::String shown;
};

/** The left screen: the selected strip's processing. A strip's name, the preamp and the
    filters at the left, its tabs along the top, the soft controls at the right, a wide
    curve (or the small faders, with View) along the bottom. */
class ProcessingScreen final : public juce::Component, private juce::Timer
{
public:
    enum class Tab
    {
        Bank,
        Overview,
        FxUnit,
        Preamp,
        Filter,
        Gate,
        InsA,
        Peq,
        Geq,
        Comp,
        InsB,
        Delay,
        /** A mix's external input (the guide's 1.6). */
        ExtIn,
        /** What the console's Routing screen has and Sidecar's routing page hasn't: a channel's
            Direct Out, a mix's Mix Source and Follow Main Pan. */
        Routing
    };

    /** Ext In's Source Select: what kind of source, then which of them. */
    enum class ExtType
    {
        None,
        Socket,
        Mix,
        Main,
        Matrix,
        Pafl
    };

    ProcessingScreen (UiContext&, Desk&);
    ~ProcessingScreen() override;

    void setStrip (Strip*);
    Strip* getStrip() const noexcept { return strip; }
    void showTab (Tab);
    Tab getTab() const noexcept { return tab; }
    bool hasTab (Tab) const;
    void setLayer (int);
    int getLayer() const noexcept { return layer; }
    const SoftWidget::Block& getBlock (int i) const noexcept { return soft.getBlock (i); }
    void toggleView();
    bool isViewAlt() const noexcept { return view != 0; }
    /** 0 the wide curve, 1 the four bands, 2 the small faders. */
    int getView() const noexcept { return view; }
    void setView (int);
    /** The C1500's one screen: the Main box as its photographs have it. */
    void setOneScreen (bool on) { oneScreen = on; repaint(); }
    /** The C1500's one screen: `page` (a right-screen page, laid out at kTouchW by kTouchH)
        takes the main area, the part of it `source` says and no more: its own side columns and
        small faders are clipped away, this screen's strip box, preamp, soft widget and bottom
        band stay. nullptr: the processing page again. */
    void hostPage (juce::Component* page, juce::Rectangle<int> source = wideSource());
    juce::Component* getHostedPage() const noexcept { return hostedPage; }
    /** Help's key for the page shown now. */
    juce::String helpKey() const;
    /** The active Mix's name moved. */
    void mixChanged() { repaint(); }
    /** Takes a model or a structure change (a scene, a remote). */
    void refreshAll();

    EqGraph& getGraph() noexcept { return graph; }

    /** Ext In: Source Select and the Socket box choose, Apply patches the mix's Ext In,
        Cancel takes the mix's own again. */
    void chooseExtIn (ExtType, int number);
    ExtType getExtType() const noexcept { return extType; }
    int getExtNumber() const noexcept { return extNumber; }
    void applyExtIn();
    void cancelExtIn();
    /** How many sources a kind has on this console, and the engine's input for one (-1: none). */
    int extCount (ExtType) const;
    int extInSource (ExtType, int number) const;
    /** ABCD Source Setup: the socket (an input number, -1 none) of input A to D of the strip. */
    void setAbcdPatch (int which, int socket);
    /** A key of the tab showing, by its text (nullptr: none). */
    ScreenButton* findButton (const juce::String& text) const;

    /** Touch & Turn: what the data rotary turns now. */
    std::function<void (const KnobBinding&)> onTouchTurn;
    std::function<void()> onLibrary, onFxUnit, onSoftChanged;
    /** A tap that wants the processing page, while another is hosted (the C1500). */
    std::function<void()> onWantsProcessing;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildTabs();
    void bindFixed();
    void buildBlocks();
    void buildTab();
    void buildPeq (Strip&);
    void buildDyn (Strip&, ld::DynSlotParams&, ModelBlock, int slot);
    void buildPreamp (Strip&);
    void buildFilter (Strip&);
    void buildInsert (ld::InsertParams&);
    void buildDelay (Strip&);
    void buildOverview (Strip&);
    void buildBank();
    void buildGeq (Strip&);
    void buildExtIn (Strip&);
    void buildRouting (Strip&);
    void loadExtIn();
    juce::String extName (ExtType, int number) const;
    void touch (Touchable&);
    ParamBox& addBox (juce::Rectangle<int>, KnobBinding, juce::Colour, const juce::String& caption = {});
    ScreenButton& addButton (juce::Rectangle<int>, const juce::String&, juce::Colour, std::function<void()>);
    ScreenButton& addToggle (juce::Rectangle<int>, const juce::String&, juce::Colour, std::atomic<bool>&);

    UiContext& ctx;
    Desk& desk;
    Strip* strip = nullptr;
    Tab tab = Tab::Peq;
    int layer = 0;
    int view = 0;
    juce::uint32 structureSeen = 0;
    std::vector<std::pair<Tab, std::unique_ptr<ScreenTab>>> tabs;
    TouchKnob gainKnob, trimKnob;
    FilterThumb hpf, lpf;
    EqGraph graph, wideCurve;
    BandPanels bandPanels;
    MiniFaders faders;
    TransferGraph transfer;
    SoftWidget soft;
    juce::Component pageArea;
    juce::Component* hostedPage = nullptr;
    bool oneScreen = false;
    std::vector<std::unique_ptr<juce::Component>> items;
    /** A tab's old items, hidden until the next timer tick: one of them may be the key whose
        click is building the tab again. */
    std::vector<std::unique_ptr<juce::Component>> retired;
    juce::Component::SafePointer<Touchable> touchedNow;
    Analysis::Bands rtaBands;
    int rtaFrame = -1;
    bool rtaShowing = false;
    int gateModel = -1, compModel = -1;
    ExtType extType = ExtType::None;
    int extNumber = 0, extShown = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessingScreen)
};

/** The right screen's MixRack page, as photographs of a real console's have it: Config
    (Mixer Config, Input Stereos, Name & Colour, Network, User Profiles, RF Devices) and Audio
    (I/O Port, Audio Sync, Source Selector, AMM). What Sidecar has no counterpart for is drawn
    and dimmed. */
class MixRackScreen final : public juce::Component, private juce::Timer
{
public:
    enum class Sub
    {
        MixerConfig,
        InputStereos,
        NameColour,
        Network,
        Users,
        RfDevices,
        IoPort,
        AudioSync,
        SourceSelector,
        Amm
    };

    MixRackScreen (UiContext&, Desk&);
    ~MixRackScreen() override;

    void showSub (Sub);
    Sub getSub() const noexcept { return sub; }
    void setStrip (Strip*);
    void setSoftLayer (int);
    void refresh();
    /** The layout being edited (what Apply lays out). */
    universal::MixerConfigPanel& getConfig() noexcept { return config; }
    /** Name & Colour: names and colours the selected range of strips. */
    void applyName (const juce::String&);
    void applyColour (int colourIndex);
    void setRange (int type, int first, int last);

    /** Source Selector (Audio): the selector shown, 0 to 19. */
    static constexpr int kSelectors = 20, kSelectorSources = 20;
    void chooseSelector (int);
    int getSelector() const noexcept { return selector; }
    /** Its top pane lists the inputs (false) or the mixes: Groups, Aux and Mains. */
    void showSelectorMixes (bool);
    /** A source of the top pane touched: it joins the selector at its first free place, or
        leaves it (the console drags them down; Sidecar's reading). False: no room, or no such. */
    bool toggleSelectorSource (Strip*);
    /** The place a strip has in the selector, -1: none. */
    int selectorMember (const Strip*) const;
    /** A member touched: it plays (the others fade out) and its on-level goes to the rotary.
        -1: Off. */
    void playSelectorMember (int member);
    /** Set Output: a mono or stereo Aux or Matrix, nullptr: unassigned. */
    void setSelectorOutput (Strip*);
    Strip* getSelectorOutput() const;
    /** Network: the MIDI channel dLive's messages start at (1 to 12; it uses five), pending
        until Apply. */
    void setPendingMidiChannel (int);
    int getPendingMidiChannel() const noexcept { return pendingMidi; }
    int getMidiChannel() const;
    void applyMidiChannel();
    /** A key of the tab showing, by its text (nullptr: none). */
    ScreenButton* findButton (const juce::String& text) const;

    std::function<void (const KnobBinding&)> onTouchTurn;
    std::function<void()> onOpenSetup;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Tab
    {
        Sub sub;
        std::unique_ptr<ScreenTab> tab;
    };
    struct Area
    {
        juce::Rectangle<float> r;
        juce::String title;
    };
    struct Label
    {
        juce::Rectangle<float> r;
        juce::String text;
        float size;
        juce::Justification j;
    };

    void timerCallback() override { if (isVisible()) refresh(); }
    juce::Rectangle<int> at (float x0, float y0, float x1, float y1) const;
    bool isConfigSub() const noexcept { return (int) sub <= (int) Sub::RfDevices; }
    void rebuild();
    void buildSends();
    void touch (Touchable&);
    void addLabel (juce::Rectangle<int>, const juce::String&, float size, juce::Justification = juce::Justification::centred);
    ScreenButton& addKey (juce::Rectangle<int>, const juce::String&, juce::Colour, std::function<void()>);
    ScreenButton& addDim (juce::Rectangle<int>, const juce::String&, juce::Colour edge = juce::Colour (0xff17c6ea));
    void addCount (juce::Rectangle<int>, universal::MixerConfigPanel::Kind);
    void addApplyCancel (float x0, float x1, float gap, float y0, float y1);
    const MixerEngine::StripList* typeList() const;
    std::vector<Strip*> ammMembers (int amm) const;
    std::vector<Strip*> selectorSources() const;
    Strip* selectorStrip (int member, float* onDb = nullptr) const;
    juce::Rectangle<float> selectorCell (int index) const;
    juce::Rectangle<float> selectorSlot (int member) const;
    KnobBinding selectorFade (bool out);
    KnobBinding selectorLevel (int member);

    void buildMixerConfig();
    void buildInputStereos();
    void buildNameColour();
    void buildNetwork();
    void buildUsers();
    void buildSync();
    void buildSourceSelector();
    void buildAmm();

    UiContext& ctx;
    Desk& desk;
    universal::MixerConfigPanel config;
    Strip* strip = nullptr;
    Sub sub = Sub::MixerConfig;
    ScreenTab titleTab, configTab, audioTab, talkbackTab, muteTab, sigGenTab, controllersTab;
    std::vector<Tab> subTabs;
    std::vector<std::unique_ptr<juce::Component>> items;
    std::vector<Area> areas;
    std::vector<Label> labels;
    SoftWidget sends;
    MiniFaders faders;
    juce::Component::SafePointer<Touchable> touchedNow;
    ScreenButton* applyKey = nullptr;
    ScreenButton* midiApply = nullptr;
    juce::TextEditor* nameField = nullptr;
    std::vector<juce::Rectangle<float>> ammBoxes, colourDots;
    int softLayer = 0, pairFirst = 0, nameType = 0, rangeFirst = 0, rangeLast = 127, selectorFirst = 0, ammBlock = 0;
    int selector = 0, selectorTouched = -1, pendingMidi = 0;
    bool selectorMixes = false;
    juce::String selectorShown;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixRackScreen)
};

/** The right screen's Meters page, as photographs of a real console's have it: Inputs, FX,
    UFX, Mix, four User pages and two RTAs. */
class MetersScreen final : public juce::Component, private juce::Timer
{
public:
    enum Tab
    {
        Inputs,
        Fx,
        Ufx,
        Mix,
        User1,
        User2,
        User3,
        User4,
        Rta1,
        Rta2,
        NumTabs
    };

    MetersScreen (UiContext&, Desk&);
    ~MetersScreen() override;

    void showTab (int);
    int getTab() const noexcept { return tab; }
    int getNumCells() const noexcept { return (int) cells.size(); }
    /** Which strip a cell shows (nullptr: an empty one), and what its key is. */
    Strip* getCellStrip (int i) const noexcept { return cells[(size_t) i].strip; }
    void setStrip (Strip*);
    void setSoftLayer (int);
    void refresh();

    std::function<void (const KnobBinding&)> onTouchTurn;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Cell
    {
        Strip* strip = nullptr;
        juce::String top, bottom, key;
        juce::Rectangle<float> r;
        juce::Colour body;
        int bars = 1;
        bool withKey = true;
    };
    struct Scale
    {
        float x, y, h;
    };

    void timerCallback() override;
    void rebuild();
    void buildRta();
    void addRow (const std::vector<Cell>&, float y, float h, float pitch, float x0);
    void paintCell (juce::Graphics&, const Cell&) const;
    void paintRta (juce::Graphics&);

    UiContext& ctx;
    Desk& desk;
    Strip* strip = nullptr;
    int tab = Inputs, first = 0, softLayer = 0, rtaMode = 0;
    bool avgOn = true, pkOn = true, holdMax = false;
    ScreenTab titleTab;
    std::vector<std::unique_ptr<ScreenTab>> tabs;
    std::vector<std::unique_ptr<ScreenButton>> rtaKeys;
    std::vector<Cell> cells;
    std::vector<Scale> scales;
    SoftWidget sends;
    MiniFaders faders;
    juce::Component::SafePointer<Touchable> touchedNow;
    Analysis::Bands bands, maxBands;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetersScreen)
};

/** The right screen's I/O page, as photographs of a real console's have it: Outputs and
    Inputs as patch grids, group by group, a tap patching only in Patch mode; Tie Lines and
    Virtual SoundCheck are drawn (Sidecar has nothing behind them yet). */
class IoScreen final : public juce::Component, private juce::Timer
{
public:
    enum Tab
    {
        Outputs,
        Inputs,
        TieLines,
        SoundCheck,
        NumTabs
    };

    IoScreen (UiContext&, Desk&);
    ~IoScreen() override;

    void showTab (int);
    int getTab() const noexcept { return tab; }
    /** MixRack (0) or Surface (1) sockets, and which group of them. */
    void setSide (int);
    void setGroup (int);
    void setPatchMode (bool);
    bool isPatchMode() const noexcept { return patchMode; }
    int getNumRows() const noexcept { return (int) rows.size(); }
    int getNumColumns() const noexcept { return (int) columns.size(); }
    bool isOn (int row, int column) const;
    /** A tap on the point (only in Patch mode). */
    void tapPoint (int row, int column);
    void setStrip (Strip*);
    void refresh();

    std::function<void (const KnobBinding&)> onTouchTurn;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Row
    {
        Strip* strip = nullptr;
        int side = 0;
        juce::String name, right;
        bool stereo = false;
        juce::Colour edge;
    };

    void timerCallback() override { if (isVisible()) refresh(); }
    void rebuild();
    void rebuildGrid();
    ScreenButton& addKey (juce::Rectangle<int>, const juce::String&, juce::Colour, std::function<void()>, bool pale = false);
    juce::Rectangle<float> gridArea() const noexcept;
    float cellSize() const noexcept { return 18.0f * zoom; }
    int rowLabelWidth() const noexcept { return tab == Inputs || tab == SoundCheck ? 118 : 138; }
    std::vector<routing::Group> sideGroups() const;

    UiContext& ctx;
    Desk& desk;
    Strip* strip = nullptr;
    int tab = Inputs, side = 0, groupIndex = 0, firstRow = 0, firstColumn = 0, softLayer = 0, dragRow = 0, dragColumn = 0;
    float zoom = 1.0f;
    bool patchMode = false;
    ScreenTab titleTab;
    std::vector<std::unique_ptr<ScreenTab>> tabs;
    std::vector<std::unique_ptr<ScreenButton>> items;
    std::vector<Row> rows;
    std::vector<int> columns; // sources (Inputs) or output ports (Outputs)
    SoftWidget sends;
    MiniFaders faders;
    juce::Component::SafePointer<Touchable> touchedNow;
    juce::String heading;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IoScreen)
};

/** The right screen's Surface page: Strip Assign, the SoftKeys, the custom rotaries. */
class SurfaceScreen final : public juce::Component, private juce::Timer
{
public:
    enum Tab
    {
        StripAssign,
        SoftKeys,
        Rotaries,
        NumTabs
    };

    SurfaceScreen (UiContext&, Desk&);
    ~SurfaceScreen() override;

    void showTab (int);
    int getTab() const noexcept { return tab; }
    /** Which bank Strip Assign shows (bank 2 is a C3500's). */
    void setBank (int);
    int getBank() const noexcept { return bank; }
    void setStrip (Strip*);
    void refresh();
    juce::String getCellText (int layer, int stripIndex) const;
    juce::Rectangle<int> cellBounds (int layer, int stripIndex) const noexcept;
    /** A strip cell touched (tests replace it). */
    std::function<void (int bank, int layer, int stripIndex)> assignCell;

    std::function<void (const KnobBinding&)> onTouchTurn;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override { if (isVisible()) refresh(); }
    void rebuild();
    ScreenButton& addKey (juce::Rectangle<int>, const juce::String&, juce::Colour, std::function<void()>);

    UiContext& ctx;
    Desk& desk;
    Strip* strip = nullptr;
    int tab = StripAssign, bank = 0, softLayer = 0;
    ScreenTab titleTab;
    std::vector<std::unique_ptr<ScreenTab>> tabs;
    std::vector<std::unique_ptr<ScreenButton>> items;
    SoftWidget sends;
    MiniFaders faders;
    juce::Component::SafePointer<Touchable> touchedNow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SurfaceScreen)
};

/** What shows while no screen key is lit: the console's state, from the engine and the show. */
class StatusScreen final : public juce::Component, private juce::Timer
{
public:
    explicit StatusScreen (UiContext&);
    ~StatusScreen() override;

    /** The panels' lines, "Name: value". */
    juce::StringArray getLines() const;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    UiContext& ctx;
    juce::String shown;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusScreen)
};

/** The right screen's bottom band on View: the four bands or the wide curve of the selected
    strip, over the page's small faders. */
class BottomBand final : public juce::Component, private juce::Timer
{
public:
    explicit BottomBand (UiContext&);
    ~BottomBand() override;
    void setStrip (Strip*);
    /** 0: the small faders (this hides), 1 the four bands, 2 the wide curve. */
    void setView (int);
    int getView() const noexcept { return view; }
    void cycleView() { setView ((view + 1) % 3); }
    void resized() override;

private:
    void timerCallback() override;
    EqGraph curve;
    BandPanels bands;
    int view = 0;
};

/** A short note on the screen showing (the Help key), in Sidecar's own words. */
class HelpPanel final : public juce::Component
{
public:
    /** Shows the note for a screen (see `screenHelp`). */
    void show (const juce::String& key);
    const juce::String& getText() const noexcept { return body; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String title, body;
};

/** Help's note: "Scenes", "Meters", "Processing/Gate"... (a key it doesn't know gets a general one). */
juce::String screenHelp (const juce::String& key);

/** The right screen's Scenes page: Scene Manager. */
class SceneScreen final : public juce::Component, private juce::Timer
{
public:
    SceneScreen (UiContext&, Desk&);
    ~SceneScreen() override;

    int getSelected() const noexcept { return selected; }
    int getNext() const noexcept { return next; }
    void select (int row);
    /** Prev and Next: the scene GO will recall. */
    void stepNext (int delta);
    /** GO on the panel: recalls Next, the following scene becomes Next. */
    void goNext();
    void setStrip (Strip*);
    void refresh();
    /** What the screen's keys do (Store and Delete ask first; these don't). */
    void go();
    void store();
    void remove();
    void setSoftLayer (int);

    /** The tabs: 0 Scene Manager, 1 Update, 2 Cue List Editor, 3 Import, 4 Global Safes. */
    void showSub (int);
    int getSub() const noexcept { return sub; }
    std::function<void()> onSubChanged;
    /** What of this page a hosting screen should show: Scene Manager's has a side list a
        C1500 shows instead of the page's own, the other tabs are wide. */
    juce::Rectangle<int> hostSource() const noexcept { return sub == 0 ? sceneSource() : wideSource(); }
    /** Lock: a locked scene can't be stored over, deleted, pasted on or renamed. The show
        file has no field for this, so it lasts while the show is open. */
    bool isLocked (int row) const;
    void toggleLock();
    /** Copy: the selected scene's content and Recall Filter. Paste: over the selected scene,
        or into a free slot; names and descriptions stay where they are. */
    void copyScene();
    void pasteScene();
    bool hasCopiedScene() const { return copied.existsAsFile(); }
    /** Global Safes: a whole column (a kind of setting) on every strip, a whole row of
        strips, everything. Its other points show what those make safe. */
    void toggleSafeColumn (int column);
    void toggleSafeRow (int category, int row);
    bool isSafeColumn (int column) const;
    bool isSafeRow (int category, int row) const;
    void setAllSafe (bool);
    void toggleUpdateScene (int row);
    int getNumUpdateScenes() const noexcept { return (int) updateRows.size(); }
    void applyUpdate();

    void undoGo();
    ScreenButton goKey, undoKey, storeKey, deleteKey, filterKey, lockKey, copyFilterKey, embeddedKey, midiKey, actionsKey, copyKey, pasteKey;
    juce::TextEditor nameEditor, notesEditor;

    std::function<void (const KnobBinding&)> onTouchTurn;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override { if (isVisible()) refresh(); }
    void buildSends();
    void showRecallFilter();
    void loadEditors();
    juce::String rowName (int row) const;

    static constexpr int kMainRows = 11, kLeftRows = 15, kSlots = 500;
    void rebuildSub();
    void paintSub (juce::Graphics&);
    void paintRows (juce::Graphics&, juce::Rectangle<float> first, float pitch, int count, int firstRow, bool marks) const;
    const MixerEngine::StripList* safeList (int category) const;
    ScreenButton& addSubKey (juce::Rectangle<int>, const juce::String&, juce::Colour, std::function<void()>, bool enabled = true);
    int sub = 0, safeCategory = 0, firstSubRow = 0;
    juce::uint32 updateSections = state::AllSections;
    bool updateManual = false;
    std::vector<int> updateRows;
    std::vector<std::unique_ptr<ScreenButton>> subKeys;
    UiContext& ctx;
    Desk& desk;
    Strip* strip = nullptr;
    ScreenTab titleTab, managerTab, updateTab, cueTab, importTab, safesTab;
    ParamBox fade;
    SoftWidget sends;
    MiniFaders faders;
    int selected = 0, next = 0, firstMain = 0, firstLeft = 0, softLayer = 0, lastRecalled = -1;
    juce::String lastTime, shownSignature, shownShow;
    std::vector<int> lockedScenes;
    juce::File copied;
    juce::Component::SafePointer<Touchable> touchedNow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SceneScreen)
};

} // namespace livedaw::ui::dl
