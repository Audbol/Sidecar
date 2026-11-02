#pragma once

#include "daw/Session.h"
#include "engine/MidiRemote.h"
#include "ui/LookAndFeel.h"
#include "ui/MeterBank.h"
#include "ui/PluginPictures.h"
#include "ui/Skin.h"
#include "ui/UiContext.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace livedaw::ui
{

class DawEditor;
class PluginChainWindow;
class PluginOverviewWindow;
class WebMirror;

/** The window: the shell around one of the interfaces (skins) over the
    console: the WING surface (WingSurface, the default) or the Universal Mixer
    (UniversalSkin, a preview while Phase 2 of docs/UNIVERSAL_MIXER.md is under
    way), switched live (SETUP > GENERAL > INTERFACE). The shell owns what both
    share: the engine, the show and its files, the selection, the solo system,
    the plugin windows, the recorder and the multitrack editor (which can take
    the whole window: Ctrl+E, EDIT on the recorder screen, the surface's DAW
    key), the MIDI and web remotes, full screen (F11) and the start-up notice. */
class MainComponent final : public juce::Component
{
public:
    MainComponent (juce::AudioDeviceManager&, MixerEngine&, PluginHost&, Show&, EventLog&, juce::PropertiesFile&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;

    /** File the show was last opened from / saved to. */
    juce::File getShowFile() const { return showFile; }
    void setShowFile (const juce::File& f) { showFile = f; updateTitle(); }

    /** Reloads `file` (used by safe-mode recovery). */
    juce::Result reloadShow (const juce::File& file, bool loadPlugins);

    /** The start-up notice (about the project and how it differs from a
        WING), unless it was turned off for this version. */
    void showStartupNotice();

    /** Full screen (F11): the window covers the screen without its title bar
        (kiosk mode). Works together with the screen-only mode (F10). */
    void setFullScreen (bool);
    bool isFullScreen() const noexcept { return fullScreen; }
    /** The window's state from before full screen (what to save and restore). */
    juce::String getWindowStateBeforeFullScreen() const { return windowStateBeforeFullScreen; }

private:
    class SoloWatcher;

    void selectStrip (Strip*);
    Strip* getSelectedStrip() const;
    void refreshAll();
    /** Shows the console model (ctx.consoleModel; saved in the app settings with
        `save`): the layers, the WING remote's identity, the surface. */
    void applyConsoleModel (ConsoleModel model, bool save);
    /** Shows `kind`'s skin (saved in the app settings with `save`). The WING
        surface on a console that isn't a WING offers to make it a WING first. */
    void setInterface (InterfaceKind kind, bool save);
    /** The console is a WING as far as the WING surface goes: the WING's aux,
        mains, matrices, DCAs and mute groups (channels, buses and FX slots grow). */
    bool isWingConsole() const;

    void showPluginScanner();
    /** The plugin overview (a window of its own): opens it, or brings it to the front. */
    void showPluginOverview();
    /** CAPTURE ALL (ctx.capturePluginPictures): the next slot of captureQueue. */
    void captureNextPluginPicture();
    /** Forgets the pictures of plugins no longer in an FX slot. */
    void prunePluginPictures();
    void openPluginEditor (int slot);
    void closePluginWindowsFor (juce::AudioProcessor*);
    void closeAllPluginWindows();

    void newShow();
    void openShow();
    void openShowFile (const juce::File&);
    void saveShowTo (juce::File);
    void saveShow (bool saveAs);
    void updateTitle();

    /** The multitrack editor in place of the console view, or back. */
    void showEditor (bool);
    /** Reopens the last recording project (offering its autosave). */
    void openLastProject();

    MixerEngine& engine;
    Show& show;
    EventLog& log;
    juce::PropertiesFile& settings;
    MeterBank meterBank;
    UiContext ctx;
    std::unique_ptr<daw::Session> recording;
    std::unique_ptr<cc::Assignments> customControlAssignments;
    std::unique_ptr<MidiRemote> midiRemote; // MIDI remote control and DAW remote control
    std::unique_ptr<WebMirror> webMirror;   // the web remote (screen mirror)
    std::unique_ptr<DawEditor> editor;
    bool editorShown = false;
    bool fullScreen = false;
    juce::String windowStateBeforeFullScreen;
    juce::uint32 lastFullScreenSwitch = 0;

    ConsoleLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this };

    state::StripRef selected;
    bool hasSelection = false;

    /** The interface over the console (WingSurface or UniversalSkin). */
    std::unique_ptr<Skin> skin;
    std::unique_ptr<SoloWatcher> soloWatcher;

    juce::OwnedArray<PluginChainWindow> pluginWindows;
    std::unique_ptr<juce::DocumentWindow> scannerWindow;
    std::unique_ptr<PluginOverviewWindow> pluginOverview;
    PluginPictures pluginPictures; // the plugins' editor pictures (the overview shows them)
    /** CAPTURE ALL: the slots still to picture, and how many there were. */
    std::vector<int> captureQueue;
    int captureTotal = 0;
    static constexpr int kCaptureWaitMs = 1200;
    std::function<void (int, int)> captureProgress;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File showFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace livedaw::ui
