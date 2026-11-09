#pragma once

#include "ui/Knobs.h"
#include "ui/UiContext.h"
#include "ui/universal/Theme.h"

#include <array>
#include <functional>
#include <vector>

namespace livedaw::ui::sq5
{

/** A fader strip's channel (Setup's Strip Assign): what a layer's slot holds. */
struct Slot
{
    enum Kind
    {
        None,
        Input,
        FxReturn,
        Mix,
        FxSend,
        Main,
        Matrix,
        Dca
    };
    Kind kind = None;
    int index = 0;

    bool isSendSource() const noexcept { return kind == Input || kind == FxReturn; }
    bool operator== (const Slot& o) const noexcept { return kind == o.kind && index == o.index; }
};

/** The engine strip of a slot (nullptr: none, or a DCA, which has none). */
Strip* stripOf (MixerEngine&, const Slot&);
/** The channel type's short name, as the title bar prints it: Ip, FX, Aux, Main, Mtx, DCA. */
juce::String typeOf (const Slot&);
/** The slot of a strip of the SQ's. */
Slot slotOf (MixerEngine&, const Strip*);

/** The mix the Mix keys choose (the "selected mix"): 0 LR, 1-12 Mix n, 13-16 FX n. */
constexpr int kMixes = 12, kFxSends = 4;
/** The bus slot of a selected mix, or -1 for LR. */
constexpr int busOfMix (int mix) noexcept { return mix - 1; }
juce::String mixType (int mix);
juce::String mixName (MixerEngine&, int mix);
/** The master strip of a selected mix. */
Slot slotOfMix (int mix);

/** The eight keys under the touchscreen, and the Home screen. */
enum class Screen
{
    Home,
    Processing,
    Routing,
    Meters,
    Fx,
    Scenes,
    Setup,
    Utility,
    Io
};

/** Processing's blocks: Bank is the bank view (Reference Guide p.29), the others
    its detail screens (pp.30-44). */
enum class Block
{
    Bank,
    Preamp,
    Hpf,
    Gate,
    Insert,
    Geq,
    Peq,
    Comp,
    // Routing's detail screens (pp.46-50).
    DirOut,
    MixSettings,
    DcaMute,
    Sends
};

/** What the SoftKeys do (Setup > Surface > Soft Controls, p.88), kept in the app settings. */
namespace soft
{
    constexpr const char* settingsKey = "sq5.softkeys";
    juce::StringArray functions();
    int get (juce::PropertiesFile&, int key);
    void set (juce::PropertiesFile&, int key, int function);
} // namespace soft

/** Something on the screen a touch reaches. */
struct Field
{
    enum Kind
    {
        /** A value: its title, white on black, a bar under it; yellow while selected. */
        Value,
        /** A button, lit in `colour` while `lit`. */
        Button,
        /** A round In key. */
        InKey,
        /** A black title bar. */
        Header,
        /** A tab: white while chosen. */
        Tab,
        /** A touch area only: what's under it paints itself (the bank view's blocks). */
        Area
    };
    Kind kind = Value;
    juce::Rectangle<float> area;
    juce::String title;
    KnobBinding binding;
    std::function<void()> press;
    std::function<bool()> lit;
    juce::Colour colour { 0xff3fd06a };
    /** What Sidecar doesn't model: drawn dim, doesn't respond. */
    bool enabled = true;
};

/** The SQ-5's 7" touchscreen, drawn after the Reference Guide's pictures: the title
    bar (p.11), Home (p.12), Processing's bank view (p.29) and its detail screens
    (pp.30-44). A value touched turns yellow and the touchscreen rotary adjusts it. */
class Display final : public juce::Component,
                      private juce::Timer
{
public:
    static constexpr int kWidth = 680, kHeight = 400, kTitleBar = 34, kColumns = 8;

    Display (UiContext&, const universal::Theme&);
    ~Display() override;

    /** A screen key: on Processing or Routing in a detail, back to the bank; in the
        bank, the channel is deselected and Home shows (p.12). */
    void pressScreenKey (Screen);
    void showScreen (Screen);
    void showBlock (Block);
    Screen getScreen() const noexcept { return screen; }
    Block getBlock() const noexcept { return block; }

    /** The selected fader layer's slots: the bank view shows them. */
    void setLayer (const std::vector<Slot>&);
    void setMix (int mix);
    int getMix() const noexcept { return selectedMix; }
    /** The selection or the console changed. */
    void refreshStrip();

    int getBankFirst() const noexcept { return bankFirst; }
    void setBankFirst (int);

    const std::vector<Field>& getFields() const noexcept { return fields; }
    /** The field called `title` (-1: none). */
    int findField (const juce::String& title) const;
    /** Presses a field as a touch would. */
    void touchField (int index);
    int getSelectedField() const noexcept { return selected; }
    /** What the touchscreen rotary adjusts (invalid: nothing selected). */
    KnobBinding getRotaryBinding() const;
    /** The selected value's binding changed. */
    std::function<void()> onRotaryChanged;
    /** Selects a channel (a touch on a bank column). */
    std::function<void (const Slot&)> onSelectSlot;
    /** The selected channel, as a slot. */
    std::function<Slot()> getSelectedSlot;
    /** Setup's Strip Assign: a layer's slots, and a slot given a channel. */
    std::function<const std::vector<Slot>& (int layer)> getLayerSlots;
    std::function<void (int layer, int index, const Slot&)> assignSlot;

    /** The Library key (lit where the screen has a library). */
    void pressLibrary();
    bool hasLibrary() const noexcept;
    bool isLibraryOpen() const noexcept { return libraryOpen; }

    bool isLocked() const noexcept { return locked; }
    void setLocked (bool);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    void rebuild();
    void select (int index);
    Strip* selectedStrip() const;

    // The screens' fields.
    void buildHome();
    void buildBank();
    void buildDetail();
    void buildOverview();
    void buildPreamp (Strip&);
    void buildHpf (Strip&);
    void buildGate (Strip&);
    void buildInsert (Strip&);
    void buildGeq (Strip&);
    void buildPeq (Strip&);
    void buildComp (Strip&);
    void buildRoutingBank();
    void buildDirOut (Strip&);
    void buildMixSettings (Strip&);
    void buildDcaMute (Strip&);
    void buildSends (Strip&);
    void buildMeters();
    void buildFx();
    void buildScenes();
    void buildSafes();
    void buildSetup();
    void buildUtility();
    void buildIo();
    void addSideTabs (const juce::StringArray& names, int& tab);
    Field& addTab (juce::Rectangle<float>, const juce::String& title, int& tab, int value);
    void paintRoutingBank (juce::Graphics&);
    void paintMeters (juce::Graphics&);
    void paintScenes (juce::Graphics&);
    void paintUtility (juce::Graphics&);
    void paintIo (juce::Graphics&);
    juce::Rectangle<float> matrixArea() const;

    // Their pictures.
    void paintTitleBar (juce::Graphics&);
    void paintHome (juce::Graphics&);
    void paintBank (juce::Graphics&);
    void paintColumn (juce::Graphics&, juce::Rectangle<float>, const Slot&, bool isSelected, bool overview);
    void paintDetail (juce::Graphics&);
    void paintField (juce::Graphics&, const Field&, bool isSelected);
    void paintGraph (juce::Graphics&);
    void paintResponse (juce::Graphics&, juce::Rectangle<float>, Strip&, int block);
    void paintTransfer (juce::Graphics&, juce::Rectangle<float>, ld::DynSlotParams&);
    void paintHistory (juce::Graphics&, juce::Rectangle<float>, bool gate);
    void paintLevel (juce::Graphics&, juce::Rectangle<float>, Strip&);
    Field& add (Field::Kind, juce::Rectangle<float>, const juce::String& title);
    Field& addValue (juce::Rectangle<float>, const juce::String& title, KnobBinding);
    Field& addToggle (Field::Kind, juce::Rectangle<float>, const juce::String& title, std::atomic<bool>&);

    UiContext& ctx;
    const universal::Theme& theme;
    Screen screen = Screen::Home;
    Block block = Block::Bank;
    std::vector<Slot> layer;
    int selectedMix = 0, bankFirst = 0;
    std::vector<Field> fields;
    int selected = -1, pressed = -1;
    float dragStartValue = 0.0f;
    int dragStartBank = 0, dragStartOther = 0;
    bool dragged = false, locked = false;
    /** The level and gain reduction drawn in the history (newest last). */
    static constexpr int kHistory = 120;
    std::array<float, kHistory> historyDb {}, historyGr {};
    int historyAt = 0;
    int timerTick = 0;
    // Where each screen was left.
    static constexpr int kIoRows = 12, kIoCols = 14;
    int meterTab = 0, sceneTab = 0, safesTab = 0, setupTab = 1, surfaceTab = 0, utilityTab = 0, generalTab = 0, ioTab = 0, fxSlot = 0;
    int sendFirst = 0, sceneFirst = 0, sceneSelected = 0, ioFirst = 0, ioSocketFirst = 0;
    int assignKind = 0, assignFirst = 0, assignLayer = 0;
    Slot assignChoice;
    bool assignPicked = false, autoIncrement = true, libraryOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Display)
};

} // namespace livedaw::ui::sq5
