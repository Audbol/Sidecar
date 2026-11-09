#pragma once

#include "ui/DLiveSurface.h"
#include "engine/Analysis.h"
#include "ui/universal/MixerConfigPanel.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace livedaw::ui::dl
{

/** The size the C3500's touchscreens are laid out at (scaled to their place on the panel). */
constexpr int kTouchW = 1000, kTouchH = 690;

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
    explicit MiniFaders (UiContext&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    Strip* stripAt (int column) const;
    int columnAt (int x) const;
    static constexpr int kColumns = 31;
    UiContext& ctx;
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
        Delay
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
    bool isViewAlt() const noexcept { return viewAlt; }
    /** The active Mix's name moved. */
    void mixChanged() { repaint(); }
    /** Takes a model or a structure change (a scene, a remote). */
    void refreshAll();

    EqGraph& getGraph() noexcept { return graph; }

    /** Touch & Turn: what the data rotary turns now. */
    std::function<void (const KnobBinding&)> onTouchTurn;
    std::function<void()> onLibrary, onFxUnit, onSoftChanged;

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
    void touch (Touchable&);
    ParamBox& addBox (juce::Rectangle<int>, KnobBinding, juce::Colour, const juce::String& caption = {});
    ScreenButton& addButton (juce::Rectangle<int>, const juce::String&, juce::Colour, std::function<void()>);
    ScreenButton& addToggle (juce::Rectangle<int>, const juce::String&, juce::Colour, std::atomic<bool>&);

    UiContext& ctx;
    Desk& desk;
    Strip* strip = nullptr;
    Tab tab = Tab::Peq;
    int layer = 0;
    bool viewAlt = false;
    juce::uint32 structureSeen = 0;
    std::vector<std::pair<Tab, std::unique_ptr<ScreenTab>>> tabs;
    TouchKnob gainKnob, trimKnob;
    FilterThumb hpf, lpf;
    EqGraph graph, wideCurve;
    MiniFaders faders;
    TransferGraph transfer;
    SoftWidget soft;
    std::vector<std::unique_ptr<juce::Component>> items;
    juce::Component::SafePointer<Touchable> touchedNow;
    Analysis::Bands rtaBands;
    int rtaFrame = -1;
    bool rtaShowing = false;
    int gateModel = -1, compModel = -1;

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
    juce::TextEditor* nameField = nullptr;
    std::vector<juce::Rectangle<float>> ammBoxes, colourDots;
    int softLayer = 0, pairFirst = 0, nameType = 0, rangeFirst = 0, rangeLast = 127, selectorFirst = 0, ammBlock = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixRackScreen)
};

/** The right screen's Scenes page: Scene Manager. */
class SceneScreen final : public juce::Component, private juce::Timer
{
public:
    explicit SceneScreen (UiContext&);
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
    UiContext& ctx;
    Strip* strip = nullptr;
    ScreenTab titleTab, managerTab, updateTab, cueTab, importTab, safesTab;
    ParamBox fade;
    SoftWidget sends;
    MiniFaders faders;
    int selected = 0, next = 0, firstMain = 0, firstLeft = 0, softLayer = 0, lastRecalled = -1;
    juce::String lastTime, shownSignature;
    juce::Component::SafePointer<Touchable> touchedNow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SceneScreen)
};

} // namespace livedaw::ui::dl
