#pragma once

#include "ui/Knobs.h"
#include "ui/UiContext.h"
#include "ui/X32SceneFiles.h"
#include "ui/universal/ValueControls.h"
#include "ui/universal/Widgets.h"

#include <array>

namespace livedaw
{
class X32Midi;
}

namespace livedaw::ui::x32
{

/** The X32 surface's display (Phase 4 step 8d): the X32's screen, drawn the
    way the X32 draws it (light blue blocks on black glass, yellow for what is
    on), and the main / solo meters in its bezel.

    - Its global elements along the top: the selected channel (number, name,
      colour), the scene, the sample rate, the clock.
    - Its screens, by the eight keys beside it (HOME, METERS, ROUTING, SETUP,
      LIBRARY, EFFECTS, MUTE GRP, UTILITY), each with its tabs (PAGE < >).
    - Six push-encoders under the screen, as the X32's manual maps them for
      each tab (some tabs have a second layer of them: LAYER ^ v); above them
      on the screen, what each turns and what a push does.

    Unlike the X32's, the screen takes touch: tap a tab, drag a value on the
    encoder row (a tap is the encoder's push), drag the EQ's bands and the
    dynamics' threshold on their graphs. Message thread only. */
class Display final : public juce::Component,
                      private juce::Timer
{
public:
    Display (UiContext&, const universal::Theme&);
    ~Display() override;

    enum class Screen
    {
        Home,
        Meters,
        Routing,
        Setup,
        Library,
        Effects,
        MuteGroups,
        Utility,
        Monitor, // the MONITOR and TALKBACK sections' VIEW
        Assign,  // ASSIGN's VIEW
        Scenes,  // SCENES' VIEW
        Usb      // USB RECORDER's VIEW
    };
    /** The ASSIGN page's lines and set (the surface's assign section). */
    std::function<juce::StringArray()> assignLines;
    /** The shown set's control codes (0-3 encoders, 4-11 keys), and setting one. */
    std::function<juce::String (int)> assignCode;
    std::function<void (int, const juce::String&)> setAssignCode;
    std::function<int()> getAssignSet;
    std::function<void (int)> setAssignSet;
    /** Any set's control codes, and the assign section's encoders 1-4 (ASSIGN > home). */
    std::function<juce::String (int, int)> assignCodeOf;
    std::function<KnobBinding (int)> assignBinding;

    void showScreen (Screen, int tab = 0);
    void showTab (int);
    void setLayer (int);
    Screen getScreen() const noexcept { return screen; }
    int getTab() const noexcept { return tab; }
    int getLayer() const noexcept { return layer; }
    juce::StringArray getTabs() const;

    /** The surface's model (SETUP, encoder 1): 0 the X32, 1 the X32 COMPACT. */
    std::function<int()> getModel;
    std::function<void (int)> setModel;

    /** EQ on faders (EFFECTS, a graphic EQ's first push; the manual's tip, p.58):
        the surface puts eight bands of FX slot `slot`, from `firstBand`, on its
        group faders; a slot below 0 gives the faders back. `/-stat/geqonfdr`
        and `/-stat/geqpos` hold it for a remote. */
    std::function<void (int slot, int firstBand)> setGeqOnFaders;
    int getGeqSlot() const noexcept { return geqSlot; }
    int getGeqFirstBand() const noexcept { return geqFirst; }
    /** Puts a graphic EQ's bands on the faders (a window of eight from `firstBand`), or takes them off (`slot` < 0). */
    void showGeqOnFaders (int slot, int firstBand);
    /** Whether FX slot `slot` holds a graphic EQ, and its band parameters
        (indexes into its model's parameters, the left side's first). */
    bool isGraphicEq (int slot) const;
    std::vector<int> geqBands (int slot) const;
    /** A graphic EQ's band 0-30 as the faders' scribble strips name it ("20", "1k25", "20k": the ISO thirds). */
    static juce::String geqBandName (int band);
    /** A strip's index as the X32 counts them (`/-stat/selidx`: 0-31 the
        channels, 32-39 aux in, 40-47 FX returns, 48-63 buses, 64-69 matrices,
        70 LR, 71 M/C; -1 for none), and the strip at one. */
    int x32Index (const Strip&) const;
    Strip* stripAtX32Index (int) const;
    /** An RTA check box (`/-prefs/rta/option` bit 0-5), and its `/-stat` twin. */
    void setRtaOption (int option, bool on);

    /** The selected strip changed (or the console did). */
    void refreshStrip();

    /** The strip METERS > rta and the EQ's overlay analyse: the selected one, or
        the fixed one `/-prefs/rta/source` names (2-73; 0 and 1, the document's
        "none" and the monitor, follow the selection: Sidecar's reading). */
    Strip* rtaStrip() const;
    /** A SNIPPETS save's scope: the sections and strips its check boxes hold
        (no strip checked: the selected one). */
    juce::uint32 snippetSections() const noexcept { return snippetScope; }
    std::vector<Strip*> snippetStrips() const;
    /** SCENES > snippets' SAVE: the console into snippet `slot` of the selected
        scene (a new one past the end, of the checked scope, named). Returns its index, -1 for none. */
    int saveSnippet (int slot, const juce::String& name);
    /** SETUP > global's INITIALIZE: 0 the console's strips, 1 the show data (asks), 2 the libraries (not done). */
    void initialize (int item);
    /** The screen's brightness (`/-prefs/bright`, 10-100 %), 0.1 to 1. */
    float lcdBrightness() const;
    void paintOverChildren (juce::Graphics&) override;

    /** What encoder `i` (0-5) turns and what its push does, as the screen shows. */
    struct Encoder
    {
        KnobBinding turn;
        juce::String pushLabel;
        std::function<void()> push;
        std::function<bool()> pushOn;
        /** The real console has it, Sidecar has nothing behind it: drawn dimmed. */
        bool dimmed = false;
    };
    const Encoder& getEncoder (int i) const { return encoders[(size_t) juce::jlimit (0, 5, i)]; }
    /** A push of encoder `i` (its key under the screen, or a tap on the screen). */
    void pushEncoder (int i);

    /** Where tab `i` and encoder `i`'s cell on the screen are (for touch, and tests). */
    juce::Rectangle<int> getTabBounds (int i) const;
    juce::Rectangle<int> getEncoderCell (int i) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** The strips CHAN SAFE lists. */
    std::vector<Strip*> recallStrips() const;

    /** A SCENES safe tab's check box: its name, whether it's on, and flipping it. */
    struct SafeItem
    {
        juce::String label;
        std::function<bool()> on;
        std::function<void()> flip;
    };
    struct SafeColumn
    {
        juce::String title;
        std::vector<SafeItem> items;
    };
    /** PARAM SAFE's four columns (p.66) and CHAN SAFE's six, an encoder each. */
    std::vector<SafeColumn> paramSafeColumns();
    std::vector<SafeColumn> chanSafeColumns();
    /** LIBRARY's channel, effects and routing tabs (0-2): the elements a load
        recalls, as names (channel and routing), and SAVE under `name`. */
    static const juce::StringArray& channelElements();
    static const juce::StringArray& routingElements();
    juce::Result savePreset (int libraryTab, const juce::String& name);
    /** A routing preset's lines ("<element> <address> <value>") of the console now. */
    juce::StringArray routingPresetLines() const;
    /** LIBRARY's elements a load recalls (bit n: element n). */
    juce::uint32 channelRecall = 0x3f, routingRecall = 0x7f;
    static constexpr const char* routingPresetExtension = ".lroute";

    /** SCENES > scenes' SAVE: the console into scene `slot` (a new one past the
        end), without the save safes' sections, named. Returns its index. */
    int saveScene (int slot, const juce::String& name, const juce::String& notes);
    /** A scene's SEND MIDI as SCENES > MIDI sets it: kind (0 none, 1 program
        change, 2 control change, 3 note, -1 something else), channel 1-16,
        number and value 0-127. */
    struct SceneMidi
    {
        int kind = 0, channel = 1, number = 0, value = 127;
    };
    static SceneMidi parseSceneMidi (const juce::String&);
    static juce::String sceneMidiText (const SceneMidi&);

    /** SETUP > scribble strip's Snippets: names to give a strip. */
    static const juce::StringArray& scribbleNames();
    /** Asks for the strip's name (Name's EDIT). */
    void editStripName (Strip&);

    /** A SETUP > config entry: its name, its X32 address and its on and off values. */
    struct ConfigItem
    {
        const char* name;
        const char* path;
        const char* on;
        const char* off;
    };
    static const std::vector<ConfigItem>& configPrefs();
    static const std::vector<ConfigItem>& configLinks();

private:
    void timerCallback() override;
    void assignEncoders();
    int numLayers() const;
    Strip* selected() const;

    void paintStatus (juce::Graphics&, juce::Rectangle<int>);
    /** The main and solo meters in the bezel beside the screen. */
    void paintBezelMeters (juce::Graphics&, juce::Rectangle<int>);
    void paintTabs (juce::Graphics&, juce::Rectangle<int>);
    void paintContent (juce::Graphics&, juce::Rectangle<int>);
    void paintEncoderRow (juce::Graphics&, juce::Rectangle<int>);
    void paintHomeFlow (juce::Graphics&, juce::Rectangle<int>, Strip&);
    void paintRows (juce::Graphics&, juce::Rectangle<int>, const juce::StringArray& left, const juce::StringArray& right);
    void paintMeters (juce::Graphics&, juce::Rectangle<int>);
    void paintSends (juce::Graphics&, juce::Rectangle<int>, Strip&);
    void paintRemote (juce::Graphics&, juce::Rectangle<int>);
    /** MUTE GRP covers only the encoder row: the page it covers, and pressing it again. */
    Screen underScreen = Screen::Home;
    int underTab = 0;
    int shownTab() const noexcept { return screen == Screen::MuteGroups ? underTab : tab; }
    /** UTILITY: the page belonging to the screen in view (manual 7.9). */
    void pressUtility();
    void paintMonitor (juce::Graphics&, juce::Rectangle<int>);
    void paintTalkback (juce::Graphics&, juce::Rectangle<int>);
    void paintOscillator (juce::Graphics&, juce::Rectangle<int>);
    void paintAssign (juce::Graphics&, juce::Rectangle<int>);
    void paintHomeConfig (juce::Graphics&, juce::Rectangle<int>, Strip&);
    void paintHomeMain (juce::Graphics&, juce::Rectangle<int>, Strip&);
    void paintSetupGlobal (juce::Graphics&, juce::Rectangle<int>);
    void paintSetupConfig (juce::Graphics&, juce::Rectangle<int>);
    void paintNetwork (juce::Graphics&, juce::Rectangle<int>);
    void paintCard (juce::Graphics&, juce::Rectangle<int>);
    void paintUsb (juce::Graphics&, juce::Rectangle<int>);
    /** EFFECTS > home: the model the fifth encoder points at, not yet assigned (-1 none). */
    int pendingFxModel = -1;
    /** USB > config: playback folder mode (a file's end goes on to the next). */
    bool usbFolderMode = true;
    std::unique_ptr<juce::FileChooser> usbFolderChooser;
    /** MONITOR's source: the list's pointer (-1: at the source in use). */
    int monitorSourcePick = -1;
    /** LIBRARY: the slot SAVE points at (past the end: a new preset). */
    int presetSaveSlot = 0;
    /** ASSIGN's set tab, as drawn: the lists the encoders point into. */
    juce::StringArray assignKinds;
    int assignKind = 0, assignTargetAt = 0, assignTargetCount = 0;
    std::function<juce::String (int)> assignTargetText;
    void paintScribble (juce::Graphics&, juce::Rectangle<float>, Strip*);
    void paintPreamps (juce::Graphics&, juce::Rectangle<float>);
    void paintSafeColumns (juce::Graphics&, juce::Rectangle<float>, const std::vector<SafeColumn>&, const int* pointed, const juce::String& footer);
    void paintScenes (juce::Graphics&, juce::Rectangle<int>);
    void assignScenesEncoders();
    void assignLibraryPresetEncoders();
    void paintLibraryPresets (juce::Graphics&, juce::Rectangle<int>);
    int libraryElement = 0;
    /** SETUP > global: the sample rate the second encoder points at (-1: the current one). */
    int globalRate = -1;
    void paintRouting (juce::Graphics&, juce::Rectangle<int>);
    /** ROUTING > home (step 10c): the input blocks, through the X32 map's
        text commands (`/config/routing/...`). */
    void assignRoutingEncoders();
    /** ROUTING's output tabs (out 1-16, aux out, P16 out, card out, AES50-A
        and -B): the output signals and the output blocks. */
    void assignOutputEncoders();
    /** EFFECTS (step 11): home picks a slot, its model and (FX 1-4) its
        source; fx1-fx8 turn the slot's parameters, six per layer. */
    void assignEffectsEncoders();
    /** The X32 models slot `slot` (0-7) takes: FX 1-4 any, FX 5-8 the insert ones. */
    std::vector<int> fxModelsFor (int slot) const;
    void paintOutputs (juce::Graphics&, juce::Rectangle<int>);
    void paintPatch (juce::Graphics&, juce::Rectangle<int>);
    void paintAutomix (juce::Graphics&, juce::Rectangle<int>);
    void assignAutomixEncoders();
    void paintUser (juce::Graphics&, juce::Rectangle<int>);
    void assignUserEncoders();
    /** An encoder that turns `path`'s value through `choices` (by name). */
    KnobBinding enumKnob (const juce::String& label, const juce::String& path, const juce::StringArray& choices);
    /** An X32 value's text as a level (dB, "-oo" the bottom) and as hertz ("1k97"). */
    static float x32Level (const juce::String& text);
    /** A `%0101...` bit field's value. */
    static int x32Bits (const juce::String& text);
    /** MONITOR's solo options, whether one is on, and switching it. */
    static const juce::StringArray& soloOptions();
    bool soloOptionOn (int option) const;
    void toggleSoloOption (int option);
    static float x32Hz (const juce::String& text);
    /** The oscillator's destination 0-25 by name. */
    static juce::String x32OscDestination (int dest);
    /** An X32 address's values as the console answers them ("AN1-8"), or empty. */
    juce::String x32Value (const juce::String& address) const;
    /** A strip's X32 address ("/ch/05", "/auxin/02", "/fxrtn/01", "/bus/03",
        "/mtx/01", "/main/st", "/main/m"), and its pair's link ("/config/chlink/5-6";
        empty for a strip that has none). */
    juce::String x32Address (const Strip&) const;
    juce::String linkAddress (const Strip&) const;
    /** A `config/source` value as the X32 names it (0 OFF, In 01-32, Aux 1-6,
        USB L / R, Fx 1L-4R, Bus 01-16). */
    static juce::String sourceName (int);
    /** SETUP > remote: the X32's MIDI (step 6b). */
    void assignRemoteEncoders();
    X32Midi* x32Midi() const;
    /** Runs an X32 text command ("/-prefs/lamp 50") and repaints. */
    void x32Run (const juce::String& text);
    /** An address's value as a number (`fallback` while the console isn't an X32). */
    int x32Int (const juce::String& address, int fallback = 0) const;
    bool x32On (const juce::String& address) const { return x32Value (address) == "ON"; }
    /** A list encoder on an int address: `count` values from `first`, named by `text`. */
    KnobBinding intKnob (const juce::String& label, const juce::String& path, int first, int count, std::function<juce::String (int)> text);

    // What the surface shows, for a remote (`/-stat`): written when it changes
    // here, followed when a remote changes it (the timer).
    void publishScreen();
    void followRemote();
    int publishedScreen = -1, publishedPage = -1, publishedMuteGrp = -1, publishedSel = -1, publishedBand = -1, publishedBank = -1,
        publishedTape = -1, publishedTapeSeconds = -1, publishedMidi = -1;
    /** `/-prefs/remote/ioenable` from the app's X32 MIDI settings, and back. */
    void publishRemotePrefs();
    void followRemotePrefs();

    // The RTA: which strip it analyses and where; its page and the EQ's overlay.
    void updateRta();
    void paintRta (juce::Graphics&, juce::Rectangle<int>);
    void paintRtaBars (juce::Graphics&, juce::Rectangle<float>, float alpha);
    void paintSpectrograph (juce::Graphics&, juce::Rectangle<float>, float alpha);
    /** The spectrograph's last ten seconds: a row of band levels (dB) per timer tick. */
    std::vector<std::vector<float>> rtaHistory;
    int rtaOption = 0;      // METERS > rta: the check box the first encoder points at
    int rtaSourcePick = -1; // and the source the sixth points at (-1: the one in use)
    void assignRtaEncoders();
    void assignSetupGlobalEncoders();
    void assignNetworkEncoders();
    void assignUsbConfigEncoders();
    void assignSnippetEncoders();
    void assignIqEncoders();
    void paintSnippets (juce::Graphics&, juce::Rectangle<int>);
    void paintEffectsHome (juce::Graphics&, juce::Rectangle<int>);
    void paintFxPanel (juce::Graphics&, juce::Rectangle<int>, int slot);
    void paintMatrixSends (juce::Graphics&, juce::Rectangle<int>, Strip&);
    void paintIqSetup (juce::Graphics&, juce::Rectangle<int>);
    /** The strip's GAIN as the X32 has it: the head amp of its input, or the
        trim (no head amp on its input, or HA gain split on for it). Not valid
        for a strip without a preamp (FX returns, buses, matrices, mains). */
    KnobBinding gainBinding (Strip&) const;
    bool gainIsHeadAmp (const Strip&) const;
    // SETUP > global: the item INITIALIZE points at, the one its first push
    // armed, what it did; the brightness encoder on the LEDs.
    int initItem = 0, initArmedItem = -1;
    juce::String globalStatus;
    bool brightnessLed = false;
    // SETUP > network: the field (address, mask, gateway) and its four numbers being edited (-1: as kept).
    int netField = 0;
    std::array<int, 4> netPending { -1, -1, -1, -1 };
    // SETUP > preamps: the option the sixth encoder points at; card: the mode pointed at (-1: the one in use).
    int haOption = 0, cardMode = -1;
    // ROUTING > p16 out: iQ SETUP shown.
    bool iqSetup = false;
    // USB > config: the recorder's side being set (0 left, 1 right).
    int usbRecSide = 0;
    // SCENES > snippets: the slot to save into; the rows the scope's encoders
    // point at (parameters, inputs, outputs) and what is checked.
    int snippetSlot = 0;
    std::array<int, 3> snippetRows {};
    juce::uint32 snippetScope = 0;
    std::vector<const Strip*> snippetChecked;
    // ROUTING's blocks: the choice turned on encoder i, not yet connected (-1 none).
    std::array<int, 6> pendingBlock { -1, -1, -1, -1, -1, -1 };
    /** A routing block's encoder: turned through `choices`, its push connects it. */
    Encoder blockEncoder (int i, const juce::String& label, std::function<juce::String()> path, const juce::StringArray& choices, const juce::String& pushLabel);
    // SETUP > scribble strip: the DCA shown (-1: the selected strip).
    int scribbleDca = -1;
    juce::String dcaName (int dca) const { return x32Value ("/dca/" + juce::String (dca + 1) + "/config/name").unquoted(); }
    // EQ on faders: the FX slot (-1 off) and the first band on the faders.
    int geqSlot = -1, geqFirst = 0;

    UiContext& ctx;
    const universal::Theme& theme;
    /** The graphs' look on the screen: black wells, orange curves, red gain reduction. */
    const universal::Theme screenTheme;
    Screen screen = Screen::Home;
    int tab = 0, layer = 0, sendBank = 0, eqBand = 0, selectedScene = 0;
    /** A talkback tab's bus; HOME's DCA and mute group to assign. */
    int talkBus = 0, homeDca = 0, homeMuteGroup = 0, soloOption = 0;
    /** SCENES' SNIPPETS: the snippet of the selected scene. */
    int selectedSnippet = 0;
    /** SCENES: home's scene to skip and snippet; scenes' slot to save, scene to
        edit, library file to import and save safe; each safe column's check box. */
    int skipScene = 0, homeSnippet = 0, sceneSlot = 0, editScene = 0, importFile = 0, saveSafeRow = 0;
    juce::uint32 saveSafes = 0;
    std::array<int, 10> safeRows {}; // PARAM SAFE's columns, then CHAN SAFE's
    juce::String sceneStatus;
    /** SETUP > config: the preference, the link preference and the bus scheme the encoders point at. */
    int configPref = 0, configLink = 0, configBus = 0;
    /** HOME config's SOURCE turned and not yet selected (-1 none), and for which strip. */
    int pendingSource = -1, pendingInsert = -1; // and the insert's slot, CONNECT not yet pushed
    const Strip* pendingSourceStrip = nullptr;
    /** SETUP's preamps tab: the block of eight inputs shown (its layers the top and bottom four). */
    int preampBlock = 0;
    /** SETUP's scribble strip tab: the name the Snippets column points at. */
    int scribbleName = 0;
    /** ASSIGN's control being edited (0-3 encoders 1-4, 4-11 keys 5-12). */
    int assignControl = 0;
    /** LIBRARY's channel and effects tabs: the preset picked, the FX slot. */
    int presetChoice = 0, presetSlot = 0;
    /** The library's presets of a kind (".lchn", ".lfx"), by name. */
    static juce::Array<juce::File> presets (const char* extension);
    /** METERS' RTA tab showed the selected strip's RTA (hidden again when it goes). */
    bool rtaShown = false;
    int selectedOutput = 0; // ROUTING's signal tabs: the output the encoders set
    int lastKeyFilter = 3;  // a key filter switched off: its type, for switching it on again
    int usbTrack = 0;       // USB > home: the track the sixth encoder shows
    int overviewFirst = 0; // ROUTING's OUTPUT PATCH OVERVIEW: the first row shown
    int userBank = 0, userSlot = 0; // ROUTING > USER: the bank of eight, the slot (inert: Sidecar has no user routing)
    bool userOutput = false;
    int pendingOutputSource = -1; // ROUTING outputs: the source chosen, not yet assigned (-1 none)
    int selectedFx = 0;     // EFFECTS > home: the slot the encoders set
    // UTILITY: the strip COPY took and the sections PASTE copies (CopySection bits).
    Strip* copySource = nullptr;
    juce::uint32 copySections = 0x3f;
    juce::String utilityStatus;
    /** UTILITY's INIT: the strip its first push armed (a second push initialises it). */
    const Strip* initArmed = nullptr;
    bool isStrip (const Strip*) const;

    std::array<Encoder, 6> encoders;
    /** An encoder's turn or push that asks for the encoders again gets them when it
        has returned: assigning them replaces the very function being called. */
    int encoderCallbacks = 0;
    bool encodersWanted = false;
    std::array<DisplayKnob, 6> knobs;
    SceneFiles sceneFiles { ctx };
    juce::String libraryStatus; // what the last IMPORT / EXPORT .scn did
    // SETUP > remote: the devices an encoder offers (shown, setting values)
    // and the one it is on (a push uses it), for MIDI IN [0] and OUT [1].
    std::array<juce::StringArray, 2> midiChoices, midiValues;
    std::array<int, 2> midiPick { 0, 0 };
    std::vector<std::unique_ptr<universal::ToggleKey>> screenKeys, pushKeys;
    universal::ToggleKey pagePrev, pageNext, layerUp, layerDown;
    /** Where PAGE SELECT and LAYER are printed (set by resized()). */
    juce::Rectangle<int> pageLabel, layerLabel;
    universal::ResponseView eqView;
    universal::TransferView gateView, dynView;

    juce::Rectangle<int> screenArea, statusArea, tabsArea, contentArea, encoderRow, bezelMeters;
    int dragEncoder = -1;
    float dragStartY = 0.0f, dragStartValue = 0.0f;
    bool dragged = false;
    juce::String shownStatus;
    // What the status bar, the encoder row and the bezel meters last showed (repainted on a change).
    juce::String shownStatusBar, shownEncoderRow, shownBezel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Display)
};

} // namespace livedaw::ui::x32
