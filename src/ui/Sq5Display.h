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
/** Bus Config (Setup > Mixer Config, p.87): the last n of the twelve mixes are Groups. */
int groupCount() noexcept;
void setGroupCount (int) noexcept;
/** Mix `index` (0-11) is a Group. */
bool isGroupMix (int index) noexcept;
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

/** What the SoftKeys do (Setup > Surface > Soft Controls, pp.88-89), kept in the app settings. */
namespace soft
{
    constexpr const char* settingsKey = "sq5.softkeys";
    /** p.89's control types, in its order. */
    enum Type
    {
        Unassigned,
        AmmChannel,
        AmmInOut,
        ChannelMute,
        ChannelPafl,
        DawControl,
        DcaMute,
        DcaPafl,
        DcaSpill,
        MidiNote,
        MidiProgram,
        MixSelect,
        Mmc,
        MuteGroup,
        PaflClear,
        RecallScene,
        SceneNext,
        ScenePrevious,
        SceneGo,
        SceneStore,
        Select,
        DriveMultitrack,
        DrivePlay,
        DriveRecord,
        Talk,
        TapTempo,
        GlobalTap,
        kNumTypes
    };
    /** A key's control type and its two options. */
    struct Assign
    {
        int type = Unassigned, option1 = 0, option2 = 0;
        bool operator== (const Assign& o) const noexcept { return type == o.type && option1 == o.option1 && option2 == o.option2; }
    };
    /** Every audio channel, as "All audio channels" lists them: inputs, FX returns, mixes, FX sends, LR, matrices. */
    constexpr int kChannels = 48 + 8 + kMixes + kFxSends + 1 + 3;
    Slot channelAt (int index);

    juce::StringArray typeNames();
    /** False for what Sidecar has no counterpart of (drawn dim in the list): DAW Control,
        which needs the app's DAW remote (the list lights it where there is one). */
    bool isModelled (int type);
    int option1Count (int type);
    int option2Count (int type, int numScenes);
    juce::String option1Text (int type, int value);
    juce::String option2Text (int type, int value, MixerEngine&);
    Assign get (juce::PropertiesFile&, int key);
    void set (juce::PropertiesFile&, int key, const Assign&);

    /** The footswitch's two switches (Setup > Surface > Footswitch, p.91): 0 Tip, 1 Ring.
        Sidecar has no footswitch socket: Sq5Surface::pressFootswitch is what one would press. */
    constexpr const char* footswitchKey = "sq5.footswitch";
    Assign getFootswitch (juce::PropertiesFile&, int which);
    void setFootswitch (juce::PropertiesFile&, int which, const Assign&);
} // namespace soft

/** Input ganging (Setup > Ganging, p.105): 8 gangs of up to 12 input channels; what is
    linked changes on every member when it changes on one. Kept in the app settings. The
    engine has no gangs: the surface's display follows the members (Follower) while it's up. */
namespace gangs
{
    constexpr int kGangs = 8, kMembers = 12;
    constexpr const char* settingsKey = "sq5.gangs";
    /** p.105's attributes, in the screen's order. */
    enum Attribute
    {
        Trim,
        Filter,
        Gate,
        Peq,
        Compressor,
        Delay,
        Fader,
        Mute,
        Pan,
        Routing,
        DirectOut,
        kNumAttributes
    };
    const char* attributeName (int attribute);
    /** False for Direct Out: Sidecar's SQ has no direct outs yet. */
    bool isModelled (int attribute);
    struct Gang
    {
        /** Input channels, 0-based, in the order they were added. */
        std::vector<int> members;
        /** Bit n: Attribute n is linked. */
        int attributes = 0;
        bool operator== (const Gang& o) const noexcept { return members == o.members && attributes == o.attributes; }
    };
    using Set = std::array<Gang, kGangs>;
    Set load (juce::PropertiesFile&);
    void save (juce::PropertiesFile&, const Set&);
    /** The gang a channel is in (-1: none). */
    int gangOf (const Set&, int channel);

    /** Copies what changed on one member of a gang to the others. Message thread. */
    class Follower
    {
    public:
        /** The gangs changed: what the members hold now is where they start from. */
        void reset() { seen.clear(); }
        void follow (MixerEngine&, const Set&);

    private:
        /** Each gang's members' linked values as last seen (member by member). */
        std::vector<std::vector<std::vector<float>>> seen;
    };
} // namespace gangs

/** On/Off settings of Setup's pages, kept in the app settings. */
namespace prefs
{
    constexpr const char* additive = "sq5.pafl.additive";
    constexpr const char* selFollow = "sq5.pafl.selFollow";
    constexpr const char* followMix = "sq5.pafl.followMix";
    constexpr const char* overrideMix = "sq5.pafl.overrideMix";
    constexpr const char* preampOnSurface = "sq5.prefs.preampOnSurface";
    constexpr const char* fxFollowSel = "sq5.prefs.fxFollowSel";
    /** Master Fader Mode: Listen Level, not Mix Master (p.103). */
    constexpr const char* listen = "sq5.masterListen";
    bool get (juce::PropertiesFile&, const char* key, bool fallback);
    void set (juce::PropertiesFile&, const char* key, bool value);
} // namespace prefs

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
        Area,
        /** Words only: its caption. */
        Label
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
    /** What's printed, if not its title (a field is found by its title). */
    juce::String caption;
    /** A button's text while not lit, if it differs (On / Off, p.50). */
    juce::String captionOff;
    /** A value without its title above the box (the PEQ's columns name the rows once). */
    bool hideTitle = false;
    /** A label centred, not at the left. */
    bool centred = false;
    /** A value's box outline, if not the usual one (the PEQ's bands' colours). */
    juce::Colour outline;
    /** A button's little picture in place of its text (the PEQ's mode keys, p.40):
        1 low shelf, 2 bell, 3 high pass, 4 high shelf, 5 low pass; the transport keys
        (pp.111-116): 6 stop, 7 play/pause, 8 record, 9 previous, 10 next; 0 none. */
    int glyph = 0;
    /** What's printed, asked for at every paint (a state that changes by itself): a
        label's or a button's words, or a value box's text where it has no binding. */
    std::function<juce::String()> live;
    /** A list's row (a Button): its words at the left, on the list's green while lit. */
    bool row = false;
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
    /** A Setup page changed what the surface shows. */
    std::function<void()> onSurfaceChanged;
    /** Utility > Data's "USB" (pp.80-82): the folder its shows and scenes are kept in.
        Sidecar's Library folder (Documents/Sidecar/Library) until set. */
    void setDataFolder (const juce::File&);
    juce::File getDataFolder() const;
    /** The gangs as applied (Setup > Ganging). */
    const gangs::Set& getGangs() const noexcept { return gangSet; }
    /** The SQ-Drive pages' transports (pp.111-114) and the SoftKeys' SQ Drive types (p.89):
        Sidecar's USB recorder is the Stereo page's, its multitrack recorder the Multitrack
        page's. `action`: 0 previous, 1 stop, 2 start / pause, 3 arm (here: record), 4 next,
        5 arm / record / stop in turn. */
    void driveStereoPlay (int action);
    void driveStereoRecord (int action);
    void driveMultitrack (int action);
    /** The Scene Manager's selection, by a SoftKey (pp.68, 89). */
    void stepSceneSelection (int delta);
    void recallSelectedScene (bool autoIncrementAfter);

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
    void buildAudioSetup();
    void buildSurfaceSetup();
    void buildSoftControls();
    void buildFootswitch();
    /** The control type's list of Soft Controls and Footswitch (p.89). */
    void buildTypePicker (juce::Rectangle<float>);
    /** Control Type, Option 1 and Option 2 of `softEdit`, at the three places. */
    void buildAssignValues (juce::Rectangle<float> type, juce::Rectangle<float> option1, juce::Rectangle<float> option2);
    void buildMixerConfig();
    void buildGanging();
    void buildNetwork();
    void buildUsers();
    void buildUtility();
    void buildData();
    void buildUsbUtility();
    void buildDrive();
    void buildCalibration();
    void buildAddons();
    void buildAmm();
    void buildIo();
    void buildLibrary (Strip&);
    /** A row's words and a key (On / Off). */
    void addFlag (juce::Rectangle<float> label, juce::Rectangle<float> key, const juce::String& text, bool enabled, std::function<bool()> get, std::function<void (bool)> set);
    /** The assign-to-mix keys of Talkback and SigGen (pp.96-97). */
    void addMixGrid (juce::Rectangle<float>, std::function<bool (int)> get, std::function<void (int, bool)> set);
    void applySigGen();
    void loadSigGen();
    void applyInputStereo();
    void applyBusConfig (int groups);
    void applyMixStereo();
    /** The shows (`.lshow`) or scenes (`.lsnap`) of the data folder, by name. */
    juce::Array<juce::File> dataFiles (const char* extension) const;
    /** A list of `count` rows in `area`, `first` its first row and `picked` the one chosen
        (both kept in range): the rows are fields named `prefix` + number (from 1). */
    void addList (juce::Rectangle<float> area, const juce::String& prefix, int count, int& first, int& picked, std::function<juce::String (int)> words);
    /** The transport keys in `area`, left to right: previous, stop, play / pause, record
        (if `withRecord`), next. `lit (action)`: the key's lamp. */
    void addTransport (juce::Rectangle<float> area, const juce::String& prefix, std::function<void (int action)> run, std::function<bool (int action)> lit, bool withRecord, bool withSkip);
    void resetRole (int role);
    ld::SourceParams* preampSource (Strip&) const;
    void addSideTabs (const juce::StringArray& names, int& tab);
    Field& addTab (juce::Rectangle<float>, const juce::String& title, int& tab, int value);
    void paintRoutingBank (juce::Graphics&);
    void paintRoutingColumn (juce::Graphics&, juce::Rectangle<float>, const Slot&, bool isSelected);
    void paintMeters (juce::Graphics&);
    void paintScenes (juce::Graphics&);
    void paintUtility (juce::Graphics&);
    void paintSetup (juce::Graphics&);
    void paintDrive (juce::Graphics&);
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
    int audioTab = 1, configTab = 0, ammTab = 0, ammPick = 0, softKeySelected = 0, addonSelected = 0, pendingGroups = -1, resetRoleChoice = 0, fxPage = 0;
    bool softPicking = false, stereoPendingValid = false, resetArmed = false;
    soft::Assign softEdit;
    std::array<bool, 24> stereoPending {};
    // The signal generator (p.97).
    std::atomic<int32_t> sigType { 0 };
    std::atomic<bool> sigMute { true };
    std::atomic<float> sigFreq { 1000.0f }, sigLevel { 0.0f };
    std::atomic<int> sigAssign { 0 };
    struct SigShown
    {
        int type = 0;
        float mute = 1.0f, freq = 1000.0f, level = 0.0f;
        int mask = 0;
    } sigShown;
    int meterFirst = 0, sendFirst = 0, sceneFirst = 0, sceneSelected = 0, ioFirst = 0, ioSocketFirst = 0;
    int assignKind = 0, assignFirst = 0, assignLayer = 0;
    Slot assignChoice;
    bool assignPicked = false, autoIncrement = true, libraryOpen = false;
    // Setup's Mix Stereo (p.86): 12 mixes, then 3 matrices.
    std::array<bool, kMixes + 3> mixStereoPending {};
    bool mixStereoPendingValid = false;
    // Ganging (p.105).
    gangs::Set gangSet, gangPending;
    gangs::Follower gangFollower;
    int gangSelected = 0, gangFirst = 0;
    bool gangPendingValid = false;
    // Footswitch (p.91), Users (p.107), Data (pp.80-83), USB Utility, SQ-Drive (pp.111-114).
    int footswitchSelected = 0, usersTab = 0, userSelected = 0, userFirst = 0, userPage = 0;
    int dataTab = 0, dataShowFirst = 0, dataShowPicked = 0, dataSceneFirst = 0, dataScenePicked = 0, dataFileFirst = 0, dataFilePicked = 0;
    /** What Data's keys wait to have confirmed: 1 Recall, 2 Overwrite, 3 Delete, 4 Overwrite SQ; 0 nothing. */
    int dataArmed = 0;
    int usbTab = 0, driveTab = 0;
    juce::File dataFolder;
    /** The page's lists (addList): a drag up or down, or the wheel, scrolls the one under it. */
    struct ListArea
    {
        juce::Rectangle<float> area;
        int* first = nullptr;
        /** Its rows run left to right (Ganging's channels): a drag left or right scrolls it. */
        bool across = false;
    };
    std::vector<ListArea> lists;
    int* dragList = nullptr;
    bool dragListAcross = false;
    const ListArea* listAt (juce::Point<float>) const;
    /** Whose assignment `softEdit` holds: a SoftKey (0-7), a footswitch (100 Tip, 101 Ring); -1 none yet. */
    int assignEditing = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Display)
};

} // namespace livedaw::ui::sq5
