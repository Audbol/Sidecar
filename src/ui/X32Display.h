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

    /** The selected strip changed (or the console did). */
    void refreshStrip();

    /** What encoder `i` (0-5) turns and what its push does, as the screen shows. */
    struct Encoder
    {
        KnobBinding turn;
        juce::String pushLabel;
        std::function<void()> push;
        std::function<bool()> pushOn;
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
    void paintScribble (juce::Graphics&, juce::Rectangle<float>, Strip*);
    void paintPreamps (juce::Graphics&, juce::Rectangle<float>);
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
    void paintEffects (juce::Graphics&, juce::Rectangle<int>);
    /** The X32 models slot `slot` (0-7) takes: FX 1-4 any, FX 5-8 the insert ones. */
    std::vector<int> fxModelsFor (int slot) const;
    void paintOutputs (juce::Graphics&, juce::Rectangle<int>);
    /** An encoder that turns `path`'s value through `choices` (by name). */
    KnobBinding enumKnob (const juce::String& label, const juce::String& path, const juce::StringArray& choices);
    /** An X32 address's values as the console answers them ("AN1-8"), or empty. */
    juce::String x32Value (const juce::String& address) const;
    /** A strip's X32 address ("/ch/05", "/auxin/02", "/fxrtn/01", "/bus/03",
        "/mtx/01"; empty for the mains), and its pair's link ("/config/chlink/5-6";
        empty for a strip that has none). */
    juce::String x32Address (const Strip&) const;
    juce::String linkAddress (const Strip&) const;
    /** A `config/source` value as the X32 names it (0 OFF, In 01-32, Aux 1-6,
        USB L / R, Fx 1L-4R, Bus 01-16). */
    static juce::String sourceName (int);
    /** SETUP > remote: the X32's MIDI (step 6b). */
    void assignRemoteEncoders();
    X32Midi* x32Midi() const;

    UiContext& ctx;
    const universal::Theme& theme;
    /** The graphs' look on the screen: black wells, orange curves, red gain reduction. */
    const universal::Theme screenTheme;
    Screen screen = Screen::Home;
    int tab = 0, layer = 0, sendBank = 0, eqBand = 0, selectedScene = 0;
    /** SCENES' safes tab: the section the first encoder points at; a talkback
        tab's bus; HOME's DCA and mute group to assign. */
    int safeSection = 0, talkBus = 0, homeDca = 0, homeMuteGroup = 0;
    /** SCENES' SNIPPETS: the snippet of the selected scene; CHAN SAFE: the strip. */
    int selectedSnippet = 0, safeStrip = 0;
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
    int selectedFx = 0;     // EFFECTS > home: the slot the encoders set
    // UTILITY: the strip COPY took and the sections PASTE copies (CopySection bits).
    Strip* copySource = nullptr;
    juce::uint32 copySections = 0x3f;
    juce::String utilityStatus;
    bool isStrip (const Strip*) const;

    std::array<Encoder, 6> encoders;
    std::array<DisplayKnob, 6> knobs;
    SceneFiles sceneFiles { ctx };
    juce::String libraryStatus; // what the last IMPORT / EXPORT .scn did
    // SETUP > remote: the devices an encoder offers (shown, setting values)
    // and the one it is on (a push uses it), for MIDI IN [0] and OUT [1].
    std::array<juce::StringArray, 2> midiChoices, midiValues;
    std::array<int, 2> midiPick { 0, 0 };
    std::vector<std::unique_ptr<universal::ToggleKey>> screenKeys, pushKeys, muteGroupKeys;
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
