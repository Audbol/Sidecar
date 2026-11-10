#pragma once

#include "ui/Skin.h"
#include "ui/UiContext.h"
#include "ui/universal/ControlsView.h"
#include "ui/universal/FxRackView.h"
#include "ui/universal/Inspector.h"
#include "ui/universal/LibraryView.h"
#include "ui/universal/MixerView.h"
#include "ui/universal/MonitorView.h"
#include "ui/universal/RecorderView.h"
#include "ui/universal/RoutingView.h"
#include "ui/universal/ScenesView.h"
#include "ui/universal/SetupView.h"

namespace livedaw::ui
{

/** The Universal Mixer's interface (docs/UNIVERSAL_MIXER.md, Phase 2), a
    preview while it is being built: a top bar (the console it runs, DSP load
    and the time, the show and its SHOW menu: NEW, OPEN, SAVE, SAVE AS; the way
    back to the console's surface; MENU: the theme, dark or light, the views,
    the show's and the scenes' commands that a narrow window has no key for,
    JUMP) and a row of the view keys (MIXER, ROUTING, FX
    RACK, MONITORING, SCENES, LIBRARY, RECORDER, CONTROLS, SETUP) with what a show
    needs at any moment beside them (the current scene with PREV and GO NEXT, CLR
    SOLO, DIM, TALK A and B), over the view shown, with the inspector of the
    selected strip beside it (src/ui/universal).

    The keyboard (skinKeyPressed): Ctrl+J opens the MIXER view's JUMP list from
    any view; in the MIXER view / opens it too, Page Up / Page Down step
    through the banks, Home / End go to the first and last, Left / Right
    select the strip before or after the selected one. */
class UniversalSkin final : public Skin, private juce::Timer
{
public:
    explicit UniversalSkin (UiContext&);
    ~UniversalSkin() override;

    /** The header: two rows (title, connection and views; the strips' controls or the show's details,
        with what a show needs), the second one can be put away. */
    int headerHeight() const noexcept { return universal::sizes::kTopBar + (row2Shown ? universal::sizes::kFilterBar : 0); }
    bool isSecondRowShown() const noexcept { return row2Shown; }
    void setSecondRowShown (bool);
    /** The way back: the key's text, the console it names, and what it does (a surface now; a real
        console that the Universal Mixer controls later). */
    void setBackTarget (const juce::String& keyText, const juce::String& consoleName, std::function<void()> go);
    universal::ToggleKey& getBackKey() noexcept { return wingKey; }
    /** The console shown in the header's connection place: LOCAL, or a real console's name, CONNECTED or OFFLINE. */
    struct Connection
    {
        juce::String console;
        bool remote = false, connected = false;
        /** Its status word (OFFLINE, CONNECTING, CONNECTED, LOST). */
        juce::String status;
    };
    void setConnection (const Connection&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshAll() override;
    void selectionChanged() override;

    enum class View
    {
        Mixer,
        Routing,
        Fx,
        Monitoring,
        Scenes,
        Library,
        Recorder,
        Setup,
        Controls
    };
    void showView (View);
    View getView() const noexcept { return view; }

    universal::MixerView& getMixer() noexcept { return mixer; }
    universal::RoutingView& getRouting() noexcept { return routingView; }
    universal::FxRackView& getFxRack() noexcept { return fxRack; }
    universal::MonitorView& getMonitoring() noexcept { return monitoring; }
    universal::ScenesView& getScenes() noexcept { return scenes; }
    universal::LibraryView& getLibrary() noexcept { return library; }
    universal::RecorderView& getRecorder() noexcept { return recorder; }
    universal::SetupView& getSetup() noexcept { return setup; }
    universal::ControlsView& getControls() noexcept { return controls; }
    void sendsOnFaderChanged() override { mixer.sendsOnFaderChanged(); }
    void customControlsChanged() override { controls.refresh(); }
    void showChanged() override
    {
        scenes.refresh();
        repaint(); // the show's name
    }
    universal::Inspector& getInspector() noexcept { return inspector; }
    bool isInspectorOpen() const noexcept { return inspectorOpen; }
    /** The show's name in the top bar (its file's, or UNTITLED SHOW). */
    juce::String getShowTitle() const;
    /** The SHOW menu's items, as its key offers them: 1 NEW, 2 OPEN, 3 SAVE, 4 SAVE AS. */
    void runShowCommand (int item);
    universal::ToggleKey& getShowKey() noexcept { return showKey; }

    /** The theme its views are drawn in (the app setting's when it was made). */
    const universal::Theme& getTheme() const noexcept { return theme; }
    /** Keeps the theme in the app settings and has the window make the skin again in it
        (a view takes its theme when it is made). */
    void setLightTheme (bool light);
    /** MENU's items. 1-4 are the SHOW menu's. */
    enum MenuItem
    {
        kMenuThemeDark = 10,
        kMenuThemeLight = 11,
        kMenuView = 20, // + the view (View)
        kMenuPreviousScene = 40,
        kMenuNextScene = 41,
        kMenuClearSolo = 42,
        kMenuDim = 43,
        kMenuJump = 50,
        kMenuUndo = 51,
        kMenuRedo = 52,
        kMenuShortcuts = 53,
        kMenuStripWidth = 54, // + 0 narrow, 1 normal, 2 wide
        kMenuResetStrips = 57,
        kMenuHeader = 58,
        kMenuSurface = 60
    };
    juce::PopupMenu buildMenu();
    void runMenuCommand (int item);
    universal::ToggleKey& getMenuKey() noexcept { return menuKey; }
    bool skinKeyPressed (const juce::KeyPress&) override;
    /** The mix's undo and redo. */
    universal::MixerHistory& getHistory() noexcept { return history; }
    void showShortcuts();

    /** What a show needs at any moment, beside the view keys. */
    universal::ToggleKey prevSceneKey, nextSceneKey, clearSoloKey, dimKey, talkAKey, talkBKey;
    /** The current scene as shown beside the keys ("3  Song 1"; empty: none). */
    juce::String getSceneReadout() const;

private:
    void timerCallback() override;
    juce::String shownScene, shownStatus;
    juce::Rectangle<int> sceneArea, statusArea;
    bool talkHeld[2] { false, false };
    UiContext& ctx;
    const universal::Theme& theme;
    universal::ToggleKey wingKey, showKey, menuKey, headerKey, connectKey;
    RemoteSession::State shownRemote = RemoteSession::State::Offline;
    void layoutSecondRow();
    juce::String backName;
    Connection connection;
    bool row2Shown = true;
    juce::Rectangle<int> titleArea, infoArea, mixerKeysArea;
    std::vector<std::unique_ptr<universal::ToggleKey>> viewKeys;
    universal::MixerView mixer;
    universal::RoutingView routingView;
    universal::FxRackView fxRack;
    universal::MonitorView monitoring;
    universal::ScenesView scenes;
    universal::LibraryView library;
    universal::RecorderView recorder;
    universal::SetupView setup;
    universal::ControlsView controls;
    View view = View::Mixer;
    universal::Inspector inspector;
    universal::OutputBridge bridge;
    universal::MixerHistory history;
    bool inspectorOpen = false;
    juce::String summary, summaryShort;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UniversalSkin)
};

} // namespace livedaw::ui
