#include "engine/EventLog.h"
#include "engine/AppPaths.h"
#include "engine/AudioWatch.h"
#include "engine/MixerEngine.h"
#include "engine/PluginHost.h"
#include "engine/PluginScanner.h"
#include "engine/Show.h"
#include "engine/VirtualAudioDevice.h"
#include "ui/MainComponent.h"
#include "ui/ShowPanel.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include "BinaryData.h"

#include <csignal>

namespace livedaw
{

/** Enough to open every channel of large ASIO/CoreAudio/ALSA/JACK interfaces. */
constexpr int kMaxDeviceChannels = 256;
//==============================================================================
/** Keeps audio running: reopens the device if it disappears, stops calling back
    or reports an error. Retries with a back-off so a missing interface doesn't
    hammer the driver. */
class DeviceWatchdog final : private juce::Timer, private juce::ChangeListener
{
public:
    /** `driverMarker` names the device while a restart waits on its driver
        (left behind, that driver hung or crashed: the next start says so). */
    DeviceWatchdog (juce::AudioDeviceManager& dm, MixerEngine& e, EventLog& l, juce::File driverMarker)
        : deviceManager (dm), engine (e), log (l), marker (std::move (driverMarker))
    {
        deviceManager.addChangeListener (this);
        engine.onDeviceError = [this] (const juce::String& message)
        {
            log.error ("Audio device error: " + message);
            restartPending = true;
        };
        changeListenerCallback (nullptr);
        startTimer (1000);
    }

    ~DeviceWatchdog() override
    {
        engine.onDeviceError = nullptr;
        deviceManager.removeChangeListener (this);
    }

    /** A device that opened but hasn't sent any audio (its name), once per start. */
    std::function<void (const juce::String&)> onSilentDevice;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (auto* device = deviceManager.getCurrentAudioDevice())
        {
            lastGoodSetup = deviceManager.getAudioDeviceSetup();
            haveGoodSetup = true;

            const auto description = device->getTypeName() + " / " + device->getName() + " @ "
                                     + juce::String (device->getCurrentSampleRate(), 0) + " Hz, "
                                     + juce::String (device->getCurrentBufferSizeSamples()) + " samples";
            if (description != lastDescription)
                log.info ("Audio device: " + description);
            lastDescription = description;
        }
        else if (lastDescription.isNotEmpty())
        {
            log.error ("Audio device closed or lost: " + lastDescription);
            lastDescription = {};
        }
    }

    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounter();
        auto* device = deviceManager.getCurrentAudioDevice();

        const auto callbacks = audiowatch::check (device != nullptr && device->isPlaying() && engine.isDeviceRunning(),
                                                  engine.hasCalledBackSinceStart(), now,
                                                  engine.getDeviceStartTime(), engine.getLastCallbackTime());

        if (callbacks == audiowatch::Callbacks::stalled && ! restartPending)
        {
            log.error ("Audio callbacks stalled for more than 2 s");
            restartPending = true;
        }

        // A driver that opened but never sent a block isn't restarted: closing
        // one like that can hang (issue #29), and opening it again rarely helps.
        // It stays open, silent, until another device is chosen.
        if (callbacks == audiowatch::Callbacks::silent && ! silentReported)
        {
            silentReported = true;
            log.error ("The audio device opened but sends no audio: " + lastDescription
                       + ". Choose another device or driver in the audio settings.");
            if (onSilentDevice != nullptr)
                onSilentDevice (device->getName());
        }
        if (callbacks != audiowatch::Callbacks::silent)
            silentReported = false;

        const bool lost = device == nullptr && haveGoodSetup;

        if ((! restartPending && ! lost) || now < nextAttemptMs)
            return;

        ++attempts;
        log.warning ("Restarting audio device (attempt " + juce::String (attempts) + ")");

        juce::String error;
        marker.replaceWithText (lastDescription.isNotEmpty() ? lastDescription : lastGoodSetup.outputDeviceName);
        if (device != nullptr)
        {
            deviceManager.closeAudioDevice();
            deviceManager.restartLastAudioDevice();
        }
        else
        {
            error = deviceManager.setAudioDeviceSetup (lastGoodSetup, true);
        }
        marker.deleteFile();

        if (deviceManager.getCurrentAudioDevice() != nullptr && error.isEmpty())
        {
            log.info ("Audio device restarted");
            restartPending = false;
            attempts = 0;
            nextAttemptMs = 0;
        }
        else
        {
            // Back off: 1 s, 2 s, 4 s ... capped at 10 s.
            nextAttemptMs = now + static_cast<juce::uint32> (juce::jmin (10000, 1000 << juce::jmin (attempts, 4)));
        }
    }

    juce::AudioDeviceManager& deviceManager;
    MixerEngine& engine;
    EventLog& log;
    juce::File marker;
    juce::AudioDeviceManager::AudioDeviceSetup lastGoodSetup;
    bool haveGoodSetup = false, restartPending = false, silentReported = false;
    int attempts = 0;
    juce::uint32 nextAttemptMs = 0;
    juce::String lastDescription;
};

//==============================================================================
/** SIGTERM/SIGINT (OS shutdown, service managers, Ctrl+C) quit cleanly, so they
    save state and aren't mistaken for a crash on the next start. The handler only
    sets a flag; a timer on the message thread does the actual quit. */
class SignalQuitter final : private juce::Timer
{
public:
    SignalQuitter()
    {
        std::signal (SIGTERM, handler);
        std::signal (SIGINT, handler);
        startTimer (200);
    }

private:
    static void handler (int) { requested = 1; }

    void timerCallback() override
    {
        if (requested != 0)
        {
            stopTimer();
            juce::JUCEApplicationBase::quit();
        }
    }

    static inline volatile std::sig_atomic_t requested = 0;
};

//==============================================================================
class LiveDawApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    // The plugin scanner runs as a second instance of this executable.
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        if (auto worker = PluginScannerWorker::tryToStart (commandLine))
        {
            scannerWorker = std::move (worker);
            return;
        }

        juce::PropertiesFile::Options options;
        options.applicationName = livedaw::kAppName;
        options.filenameSuffix = "settings";
        options.folderName = livedaw::kAppName;
        options.osxLibrarySubFolder = "Application Support";
        {
            // The app was called "Live DAW": carry its settings over once.
            auto legacy = options;
            legacy.applicationName = legacy.folderName = "LiveDAW";
            const auto from = legacy.getDefaultFile(), to = options.getDefaultFile();
            if (! to.existsAsFile() && from.existsAsFile() && to.getParentDirectory().createDirectory())
                from.copyFileTo (to);
        }
        properties.setStorageParameters (options);
        auto& settings = *properties.getUserSettings();

        if (const char* mismatch = ld::verifyLayout())
        {
            // A mismatched engine library would corrupt the shared parameter blocks.
            const auto message = juce::String ("The audio engine library does not match this build (")
                                 + mismatch + "). Please reinstall Sidecar.";
            juce::NativeMessageBox::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                                       "Sidecar", message),
                                               [] (int) { quit(); });
            return;
        }

        getDataDirectory().createDirectory();
        log.setLogFile (getLogFile(), getPreviousLogFile());

        // Did the previous run end without a clean shutdown?
        const bool crashedLastTime = getRunningMarker().existsAsFile();
        // Did it hang or crash inside the audio driver? The marker names the device.
        if (const auto marker = getDriverMarker(); marker.existsAsFile())
        {
            stuckDriver = marker.loadFileAsString().trim();
            marker.deleteFile();
            log.error ("The audio driver did not respond last time: " + stuckDriver);
        }
        getRunningMarker().replaceWithText (juce::String (juce::Time::getCurrentTime().toMilliseconds()));

        pluginHost = std::make_unique<PluginHost> (&settings);
        {
            // Sandboxed plugins report their crashes, hangs and restarts here.
            auto sandbox = pluginHost->getSandboxOptions();
            sandbox.onEvent = [this] (const juce::String& message, bool isError)
            {
                if (isError)
                    log.error (message);
                else
                    log.warning (message);
            };
            pluginHost->setSandboxOptions (sandbox);
        }
        engine = std::make_unique<MixerEngine>();
        // SETUP > GENERAL > FX SLOTS, before the console is restored (a show using more raises it).
        engine->getFx().setNumSlots (settings.getIntValue (ui::uiprefs::fxSlots, kWingFx));
        show = std::make_unique<Show> (*engine, *pluginHost);

        log.info ("Sidecar " + getApplicationVersion() + " starting, "
                  + juce::String (engine->getNumWorkerThreads() + 1) + " DSP threads");
        if (pluginHost->isSandboxEnabled())
            log.info ("Plugins run in the plugin sandbox (experimental)");

        const auto crashedPlugin = pluginHost->checkForCrashDuringPluginLoad();
        if (crashedPlugin.isNotEmpty())
            log.error ("\"" + crashedPlugin + "\" crashed while loading last time and has been blacklisted");

        // Restore the console from the last run. After a crash, restore it without
        // plugins (safe mode) so a bad plugin can't crash the app again.
        safeMode = crashedLastTime;
        bool restored = false;
        if (getAutosaveFile().existsAsFile())
        {
            const auto result = show->loadFromFile (getAutosaveFile(), ! safeMode);
            restored = result.wasOk();
            if (result.failed())
                log.warning (result.getErrorMessage());
        }

        // STARTUP (manual 5.6 p. 52, chapter 10): STARTUP* files in the
        // library's STARTUP folder load now. The WING skips them while LIBRARY
        // is held at power-up; here SHIFT held while Sidecar starts, or
        // --no-startup on the command line.
        juce::File startupShow;
        const auto startupFolder = Show::startupFolder (ui::ShowPanel::libraryFolder());
        if (startupFolder.isDirectory())
        {
            if (commandLine.contains ("--no-startup") || juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown())
                log.info ("STARTUP files skipped");
            else
            {
                const auto startup = show->loadStartupFolder (startupFolder, ! safeMode);
                for (const auto& f : startup.loaded)
                    log.info ("STARTUP: loaded " + f);
                for (const auto& e : startup.errors)
                    log.warning ("STARTUP: " + e);
                startupShow = startup.showFile;
            }
        }

        if (engine->getChannels().empty())
            engine->setNumChannels (engine->getConfiguredCounts().channels); // a fresh console: the WING's 40

        // STARTUP MAIN MUTE: the mains start muted (before any audio runs) when
        // the show asks for it.
        if (engine->applyStartupMainMute())
            log.info ("Mains muted at start-up (STARTUP MAIN MUTE)");

        // A device with no hardware behind it, for machines without a sound card.
        // Added after the platform's types (created by getAvailableDeviceTypes),
        // so it is never picked by default over real hardware.
        deviceManager.getAvailableDeviceTypes();
        deviceManager.addAudioDeviceType (std::make_unique<VirtualAudioIODeviceType>());

        // Safe mode also starts without the audio interface (on the device with
        // no hardware behind it): a driver that hangs or crashes Sidecar would
        // otherwise do it again on every start. The recovery question turns it
        // back on.
        audioHeldBack = safeMode;
        if (audioHeldBack)
        {
            log.warning ("Safe mode: the audio interface is off until you turn it back on");
            openVirtualDevice();
        }
        else
        {
            openSavedDevice();
        }

        deviceManager.addAudioCallback (engine.get());
        watchdog = std::make_unique<DeviceWatchdog> (deviceManager, *engine, log, getDriverMarker());
        watchdog->onSilentDevice = [] (const juce::String& name)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "No audio",
                                                    "The audio device \"" + name + "\" opened but isn't sending any audio, "
                                                    "so the console is silent.\n\nChoose another device or driver in the "
                                                    "audio settings.");
        };

        auto* main = new ui::MainComponent (deviceManager, *engine, *pluginHost, *show, log, settings);
        mainComponent = main;
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), main, settings.getValue ("ui.windowState"));
        if (startupShow != juce::File())
            main->setShowFile (startupShow);
        else
            main->setShowFile (juce::File (settings.getValue ("lastShowFile")).existsAsFile()
                                   ? juce::File (settings.getValue ("lastShowFile")) : juce::File());

        if (settings.getBoolValue (ui::uiprefs::fullScreen, false))
            main->setFullScreen (true);

        // The start-up notice (unless turned off for this version); in safe
        // mode after the recovery question, which comes first. Otherwise the
        // choice of mixer comes first, unless STARTUP files chose the show:
        // those start a console unattended.
        if (safeMode)
            offerSafeModeRecovery (crashedPlugin);
        else if (startupShow != juce::File())
            showStartupNotice();
        else
            main->showMixerChooser (restored,
                                    [this]
                                    {
                                        // A fresh console's autosave replaces this one: keep it.
                                        if (getAutosaveFile().existsAsFile()
                                            && ! getAutosaveFile().copyFileTo (getDataDirectory().getChildFile ("autosave-previous.lshow")))
                                            log.warning ("Could not keep the previous autosave");
                                    },
                                    [this] { showStartupNotice(); });

        startAutosave();
        signalQuitter = std::make_unique<SignalQuitter>();
    }

    void shutdown() override
    {
        if (scannerWorker != nullptr)
        {
            scannerWorker.reset();
            return;
        }

        if (engine == nullptr) // refused to start
            return;

        signalQuitter.reset();
        autosaveTimer.reset();
        saveState();

        watchdog.reset();
        mainWindow.reset();
        deviceManager.removeAudioCallback (engine.get());
        deviceManager.closeAudioDevice();
        show.reset();
        engine.reset();
        pluginHost.reset();

        // Clean exit: next start is not a crash recovery.
        getRunningMarker().deleteFile();
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override {}

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, juce::Component* content, const juce::String& savedState)
            : DocumentWindow (name, ui::Colours::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
           #if JUCE_LINUX || JUCE_BSD
            // Windows and macOS take the icon from the executable / app bundle.
            setIcon (juce::ImageCache::getFromMemory (BinaryData::sidecar256_png, BinaryData::sidecar256_pngSize));
           #endif
            setContentOwned (content, true);
            setResizable (true, true);
            // The WING's whole surface: a 1280 x 720 screen less a taskbar and
            // the title bar. Below that the 24 faders of the default layers
            // and the custom controls no longer fit side by side with usable
            // strips, or the main display (legible) and the fader sections no
            // longer fit on top of each other.
            setResizeLimits (1280, 640, 32768, 32768);

            // Reopen where it was (size, position, maximised); fall back to
            // centred in the main display's work area (a 1366 x 768 laptop
            // can't take the default size).
            if (savedState.isEmpty() || ! restoreWindowStateFromString (savedState))
            {
                const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
                const auto area = display != nullptr ? contentArea (*display) : juce::Rectangle<int> (getWidth(), getHeight());
                setBounds (juce::Rectangle<int> (juce::jmin (getWidth(), area.getWidth()), juce::jmin (getHeight(), area.getHeight()))
                               .withCentre (area.getCentre()));
            }
            else if (! isFullScreen())
            {
                keepOnScreen();
            }

            setVisible (true);
        }

        /** A display's work area less this window's frame (the title bar and
            borders, guessed until the window manager tells): where the
            content fits with the title bar on screen. */
        juce::Rectangle<int> contentArea (const juce::Displays::Display& display) const
        {
            juce::BorderSize<int> frame (32, 0, 0, 0);
            if (auto* peer = getPeer())
                if (const auto size = peer->getFrameSizeIfPresent())
                    frame = *size;
            const auto area = display.userBounds.toNearestInt();
            return frame.subtractedFrom (area).getIntersection (area);
        }

        /** A size saved on a bigger screen (an external monitor since unplugged,
            a lower resolution) would leave part of the surface off screen, which
            JUCE only corrects when almost nothing shows: fit the window into its
            display's work area. */
        void keepOnScreen()
        {
            const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds());
            if (display == nullptr)
                return;
            const auto area = contentArea (*display);
            if (area.contains (getScreenBounds()))
                return;
            auto bounds = getBounds();
            bounds.setSize (juce::jmin (bounds.getWidth(), area.getWidth()), juce::jmin (bounds.getHeight(), area.getHeight()));
            setBounds (bounds.constrainedWithin (area));
        }

        // The window passes the keyboard focus on to the console, so its
        // shortcuts (F10, F11, Ctrl+S...) work from the start.
        void focusGained (FocusChangeType) override
        {
            if (auto* content = getContentComponent(); content != nullptr && content->isShowing())
                content->grabKeyboardFocus();
        }

        void closeButtonPressed() override
        {
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Quit Sidecar",
                                                "Quit and stop all audio?", "Quit", "Cancel", this,
                                                juce::ModalCallbackFunction::create ([] (int result)
            {
                if (result != 0)
                    JUCEApplication::getInstance()->systemRequestedQuit();
            }));
        }
    };

    static juce::File getDataDirectory()
    {
        return livedaw::paths::appData();
    }

    static juce::File getRunningMarker() { return getDataDirectory().getChildFile ("running.lock"); }

    /** Exists while Sidecar waits on the audio driver to open or restart a
        device, and names it: left behind, that driver hung or crashed. */
    static juce::File getDriverMarker() { return getDataDirectory().getChildFile ("audio-driver.lock"); }

    /** This run's event log, and the one before (for a bug report after a freeze or crash). */
    static juce::File getLogFile() { return getDataDirectory().getChildFile ("event-log.txt"); }
    static juce::File getPreviousLogFile() { return getDataDirectory().getChildFile ("event-log-previous.txt"); }

    /** The autosave restored at startup (always the full one, with plugins). */
    static juce::File getAutosaveFile() { return getDataDirectory().getChildFile ("autosave.lshow"); }

    /** Where autosaves go: in safe mode, elsewhere, so the plugin-less console
        never overwrites the real autosave before the user decides. */
    juce::File getAutosaveTarget() const
    {
        return safeMode ? getDataDirectory().getChildFile ("autosave-safemode.lshow") : getAutosaveFile();
    }

    /** Device state selecting the first ASIO (or JACK) device, or nullptr to use
        the OS default. Drivers that come with the computer's own sound rather than
        with an audio interface are passed over (audiowatch::isStandInDriver): with
        only those, the OS default it is. Uses the same format as
        AudioDeviceManager::createStateXml(). */
    std::unique_ptr<juce::XmlElement> makePreferredDeviceState()
    {
        for (const auto* typeName : { "ASIO", "JACK" })
        {
            for (auto* type : deviceManager.getAvailableDeviceTypes())
            {
                if (type->getTypeName() != typeName)
                    continue;

                type->scanForDevices();
                const auto devices = type->getDeviceNames (false);
                const int index = audiowatch::firstStartDevice (devices);
                for (int i = 0; i < (index < 0 ? devices.size() : index); ++i)
                    log.info (juce::String (typeName) + " driver passed over on a first start (it comes with the "
                              "computer's own sound or other software, not an audio interface): " + devices[i]);
                if (index < 0)
                    continue;

                log.info (juce::String (typeName) + " driver found: " + devices[index]);
                auto xml = std::make_unique<juce::XmlElement> ("DEVICESETUP");
                xml->setAttribute ("deviceType", typeName);
                xml->setAttribute ("audioOutputDeviceName", devices[index]);
                xml->setAttribute ("audioInputDeviceName",
                                   type->hasSeparateInputsAndOutputs() ? type->getDeviceNames (true)[0] : devices[index]);
                return xml;
            }
        }

        return nullptr;
    }

    /** Calls into the audio driver with the driver marker in place, so a
        driver that never returns (or crashes) is named on the next start. */
    template <typename Fn>
    void callDriver (const juce::String& what, Fn&& fn)
    {
        getDriverMarker().replaceWithText (what);
        fn();
        getDriverMarker().deleteFile();
    }

    static juce::String describeSetup (const juce::XmlElement* xml)
    {
        if (xml == nullptr)
            return "the default audio device";
        return xml->getStringAttribute ("deviceType") + " / " + xml->getStringAttribute ("audioOutputDeviceName");
    }

    /** The audio device of the last run (the first ASIO or JACK device on a first start). */
    void openSavedDevice()
    {
        // First launch: open a low-latency driver (ASIO on Windows, JACK on Linux)
        // directly, instead of trying the OS default device first.
        auto savedDevice = properties.getUserSettings()->getXmlValue ("audioDevice");
        if (savedDevice == nullptr)
            savedDevice = makePreferredDeviceState();

        juce::String error;
        callDriver (describeSetup (savedDevice.get()),
                    [&] { error = deviceManager.initialise (kMaxDeviceChannels, kMaxDeviceChannels, savedDevice.get(), true); });
        if (error.isNotEmpty())
            log.error ("Audio device: " + error);
    }

    void openVirtualDevice()
    {
        juce::XmlElement xml ("DEVICESETUP");
        xml.setAttribute ("deviceType", VirtualAudioIODeviceType::kTypeName);
        xml.setAttribute ("audioOutputDeviceName", VirtualAudioIODeviceType::kDeviceName);
        xml.setAttribute ("audioInputDeviceName", VirtualAudioIODeviceType::kDeviceName);
        const auto error = deviceManager.initialise (kMaxDeviceChannels, kMaxDeviceChannels, &xml, false);
        if (error.isNotEmpty())
            log.error ("Audio device: " + error);
    }

    void restoreAudio()
    {
        if (! audioHeldBack)
            return;
        audioHeldBack = false;
        openSavedDevice();
        log.info ("Left safe mode: audio interface turned back on");
    }

    void offerSafeModeRecovery (const juce::String& crashedPlugin)
    {
        juce::String message = "Sidecar did not shut down cleanly last time.\n\n"
                               "The console has been restored in SAFE MODE, without any plugins and "
                               "with the audio interface off.";
        if (stuckDriver.isNotEmpty())
            message << "\n\nThe audio driver stopped responding while opening \"" << stuckDriver
                    << "\". Keep the audio off and choose another device or driver in the audio settings.";
        if (crashedPlugin.isNotEmpty())
            message << "\n\n\"" << crashedPlugin << "\" crashed while loading and has been blacklisted.";
        message << "\n\nThe last run's event log, for a bug report:\n" << getPreviousLogFile().getFullPathName();

        log.warning ("Started in safe mode (previous run did not exit cleanly)");

        juce::AlertWindow::showYesNoCancelBox (juce::MessageBoxIconType::WarningIcon, "Safe mode", message,
                                               "Turn everything back on", "Audio only", "Stay in safe mode", nullptr,
                                               juce::ModalCallbackFunction::create ([this] (int result)
        {
            showStartupNotice();
            if (result == 0 || mainComponent == nullptr)
                return;

            restoreAudio();
            if (result != 1)
                return;

            safeMode = false;
            if (! getAutosaveFile().existsAsFile())
                return; // nothing saved yet: no plugins to bring back
            const auto loadResult = mainComponent->reloadShow (getAutosaveFile(), true);
            log.info ("Left safe mode: plugins reloaded");
            if (loadResult.failed())
                log.warning (loadResult.getErrorMessage());
        }));
    }

    void showStartupNotice()
    {
        juce::MessageManager::callAsync ([main = mainComponent]
        {
            if (main != nullptr)
                main->showStartupNotice();
        });
    }

    void saveState()
    {
        if (engine == nullptr)
            return;

        auto& settings = *properties.getUserSettings();
        // While safe mode holds the interface back (still on the device with no
        // hardware), keep the last run's choice.
        const auto* current = deviceManager.getCurrentAudioDevice();
        if (! (audioHeldBack && current != nullptr && current->getTypeName() == VirtualAudioIODeviceType::kTypeName))
            if (auto xml = deviceManager.createStateXml())
                settings.setValue ("audioDevice", xml.get());
        if (mainComponent != nullptr)
            settings.setValue ("lastShowFile", mainComponent->getShowFile().getFullPathName());
        if (mainWindow != nullptr)
        {
            // In full screen, the window as it was before (full screen itself is its own setting).
            const bool fullScreen = mainComponent != nullptr && mainComponent->isFullScreen();
            settings.setValue ("ui.windowState", fullScreen ? mainComponent->getWindowStateBeforeFullScreen()
                                                            : mainWindow->getWindowStateAsString());
        }
        settings.saveIfNeeded();

        const auto result = show->saveToFile (getAutosaveTarget());
        if (result.failed())
            log.error ("Autosave failed: " + result.getErrorMessage());
    }

    void startAutosave()
    {
        struct AutosaveTimer final : juce::Timer
        {
            explicit AutosaveTimer (LiveDawApplication& a) : app (a) { startTimer (30000); }
            void timerCallback() override { app.saveState(); }
            LiveDawApplication& app;
        };

        autosaveTimer = std::make_unique<AutosaveTimer> (*this);
    }

    std::unique_ptr<PluginScannerWorker> scannerWorker;

    juce::ApplicationProperties properties;
    juce::AudioDeviceManager deviceManager;
    EventLog log;
    std::unique_ptr<PluginHost> pluginHost;
    std::unique_ptr<MixerEngine> engine;
    std::unique_ptr<Show> show;
    std::unique_ptr<DeviceWatchdog> watchdog;
    std::unique_ptr<MainWindow> mainWindow;
    juce::Component::SafePointer<ui::MainComponent> mainComponent;
    std::unique_ptr<juce::Timer> autosaveTimer;
    std::unique_ptr<SignalQuitter> signalQuitter;
    bool safeMode = false;
    bool audioHeldBack = false; // safe mode: on the device with no hardware until turned back on
    juce::String stuckDriver;   // the device whose driver hung or crashed last run
};

} // namespace livedaw

START_JUCE_APPLICATION (livedaw::LiveDawApplication)
