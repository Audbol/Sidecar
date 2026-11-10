#include "ui/MainComponent.h"
#include "ui/InterfacePage.h"
#include "engine/AppPaths.h"
#include "ui/MidiRemoteUi.h"
#include "ui/RoutingGroups.h"
#include "ui/ProMixSurface.h"
#include "ui/Sq5Surface.h"
#include "ui/Y02RSurface.h"
#include "ui/Y01VSurface.h"
#include "ui/DLiveSurface.h"
#include "ui/DLiveC3500Surface.h"
#include "ui/Dm2000Surface.h"
#include "ui/Pro2Surface.h"
#include "ui/Xenyx1202Surface.h"
#include "ui/X32Surface.h"
#include "ui/PluginChainWindow.h"
#include "ui/PluginOverviewWindow.h"
#include "ui/MixerChooser.h"
#include "ui/StartupNotice.h"
#include "ui/UniversalSkin.h"
#include "ui/universal/Groups.h"
#include "ui/WebMirror.h"
#include "ui/WingPopups.h"
#include "ui/WingSurface.h"
#include "ui/daw/DawEditor.h"

#include <set>

namespace livedaw::ui
{

//==============================================================================
/** SOLO SYSTEM (SETUP > AUDIO): EXCLUSIVE SOLO and SELECT FOLLOWS SOLO act on
    every new solo, wherever it was switched on (surface, screens, custom
    controls), so no control needs to know about them. */
class MainComponent::SoloWatcher final : private juce::Timer
{
public:
    explicit SoloWatcher (MainComponent& o) : owner (o)
    {
        snapshot();
        startTimerHz (30);
    }

    /** Takes the solos as they are now as known (after the owner changed them). */
    void snapshot() { known = current(); }

    /** Clears every solo but `keep` (a strip handle or DCA, nullptr for none). */
    void clearAllBut (const void* keep)
    {
        auto& mixer = owner.engine;
        for (int k = 0; k < kNumStripKinds; ++k)
            for (auto& s : mixer.getStrips (static_cast<StripKind> (k)))
                if (s->getHandle() != keep)
                    s->params.solo = false;
        for (int i = 0; i < mixer.getNumDcas(); ++i)
            if (&mixer.getDca (i) != keep)
                mixer.getDca (i).solo = false;
    }

private:
    std::set<const void*> current() const
    {
        std::set<const void*> soloed;
        auto& mixer = owner.engine;
        for (int k = 0; k < kNumStripKinds; ++k)
            for (auto& s : mixer.getStrips (static_cast<StripKind> (k)))
                if (s->params.solo.load())
                    soloed.insert (s->getHandle());
        for (int i = 0; i < mixer.getNumDcas(); ++i)
            if (mixer.getDca (i).solo.load())
                soloed.insert (&mixer.getDca (i));
        return soloed;
    }

    Strip* stripOf (const void* key) const
    {
        for (int k = 0; k < kNumStripKinds; ++k)
            for (auto& s : owner.engine.getStrips (static_cast<StripKind> (k)))
                if (s->getHandle() == key)
                    return s.get();
        return nullptr;
    }

    void timerCallback() override
    {
        const auto now = current();
        const void* added = nullptr;
        for (auto* k : now)
            if (known.count (k) == 0)
                added = k;
        known = now;
        if (added == nullptr)
            return;
        if (owner.settings.getBoolValue (uiprefs::exclusiveSolo, uiprefs::exclusiveSoloDefault))
        {
            clearAllBut (added);
            known = { added };
        }
        if (owner.settings.getBoolValue (uiprefs::selectFollowsSolo, uiprefs::selectFollowsSoloDefault))
            if (auto* s = stripOf (added); s != nullptr && s != owner.getSelectedStrip())
                owner.selectStrip (s);
    }

    MainComponent& owner;
    std::set<const void*> known;
};

//==============================================================================
MainComponent::MainComponent (juce::AudioDeviceManager& dm, MixerEngine& e, PluginHost& h, Show& s,
                              EventLog& l, juce::PropertiesFile& props)
    : engine (e),
      show (s),
      log (l),
      settings (props),
      meterBank (e),
      ctx { e, h, s, l, dm, props, meterBank }
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    setLookAndFeel (&lookAndFeel);
    // The surface's finish and the console model (SETUP > SIDECAR)
    // are app settings; the COMPACT is black.
    const auto look = ConsoleLook::load (settings);
    setSurfaceFinish (look.shownFinish());
    ctx.consoleModel = look.model;

    // Plugin delay compensation (SETUP > SIDECAR > PLUGINS) is an app setting, on by default.
    engine.setLatencyCompensation (settings.getBoolValue (uiprefs::latencyCompensation, true));
    engine.onPluginWarning = [this] (const juce::String& text) { log.warning (text); };

    ctx.selectStrip = [this] (Strip* strip) { selectStrip (strip); };
    ctx.getSelectedStrip = [this] { return getSelectedStrip(); };
    ctx.openPluginEditor = [this] (int slot) { openPluginEditor (slot); };
    // The audio interface's patch is the computer's: it comes back with its device.
    engine.onDeviceChanged = [this] { loadInterfacePatch (ctx); };
    loadInterfacePatch (ctx); // the device may be running already
    ctx.pluginRemoved = [this] (juce::AudioProcessor* p)
    {
        closePluginWindowsFor (p);
        pluginPictures.forget (p);
        // A chain's plugins go with it: once it is gone, forget theirs too.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe != nullptr) safe->prunePluginPictures(); });
    };
    ctx.pluginPictures = &pluginPictures;
    ctx.capturePluginPictures = [this] (std::function<void (int, int)> progress)
    {
        if (! captureQueue.empty())
            return; // already capturing
        for (int slot = 0; slot < kMaxFx; ++slot)
            if (engine.getFx().hasPlugin (slot))
                captureQueue.push_back (slot);
        captureTotal = (int) captureQueue.size();
        captureProgress = std::move (progress);
        captureNextPluginPicture();
    };
    ctx.showPluginScanner = [this] { showPluginScanner(); };
    ctx.showPluginOverview = [this] { showPluginOverview(); };
    // The plugin overview was open when Sidecar quit: again (after the main window).
    if (settings.getBoolValue (PluginOverviewWindow::openKey, false))
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe != nullptr) safe->showPluginOverview(); });
    ctx.structureChanged = [this] { refreshAll(); };
    ctx.layersChanged = [this] { skin->refreshLayers(); };
    ctx.sofChanged = [this]
    {
        // BUS SoF ACTIVATES SOLO: entering Sends on Fader solos the destination.
        if (auto* sel = getSelectedStrip(); sel != nullptr && ctx.sendsOnFader && sel->isOutput() && settings.getBoolValue (uiprefs::sofActivatesSolo, false))
            sel->params.solo = true;
        skin->sendsOnFaderChanged();
    };
    ctx.setConsoleModel = [this] (ConsoleModel m) { applyConsoleModel (m, true); };
    ctx.setInterface = [this] (InterfaceKind kind) { setInterface (kind, true); };
    ctx.remoteInfo = [this] { return engine.getRemoteSession().info(); };
    ctx.remoteConnect = [this] { connectRemote(); };
    ctx.remoteDisconnect = [this] { endRemote (true); };
    ctx.newShow = [this] { newShow(); };
    ctx.openShow = [this] { openShow(); };
    ctx.saveShow = [this] (bool saveAs) { saveShow (saveAs); };
    ctx.getShowFile = [this] { return showFile; };
    ctx.openShowFile = [this] (const juce::File& file) { openShowFile (file); };
    ctx.saveShowTo = [this] (const juce::File& file) { saveShowTo (file); };
    ctx.showFileRenamed = [this] (const juce::File& from, const juce::File& to)
    {
        // The open show's file: SAVE writes the new one, and the show takes its name.
        if (showFile == juce::File() || from != showFile)
            return;
        showFile = to;
        show.setName (to.getFileNameWithoutExtension());
        updateTitle();
        skin->showChanged();
    };

    // The recording session (the recorder screen, the editor and MULTI TRANSP share it).
    recording = std::make_unique<daw::Session> (engine.getRecorder(), &settings);
    recording->onMessage = [this] (const juce::String& text, bool error)
    {
        if (error)
            log.warning (text);
        else
            log.info (text);
    };
    // AUTO INPUT SELECT (manual 8.1 p. 85-86): the recorder's stop, play and
    // record status switch the channels in GLOBAL INPUT SELECT ENABLE that
    // are routed to or from the recorder (as VIRTUAL SOUNDCHECK sets them up)
    // to MAIN or ALT.
    recording->onTransportModeChanged = [this] (daw::Session::TransportMode mode)
    {
        if (const int switched = recording->applyAutoInput (engine, mode); switched > 0)
        {
            static const char* const names[] = { "STOP", "PLAY", "RECORD" };
            log.info (juce::String ("AUTO INPUT SELECT (") + names[(int) mode] + "): " + juce::String (switched) + " channels listen to their "
                      + (*daw::Session::autoInputFor (&settings, mode) ? "ALT" : "MAIN") + " inputs");
        }
    };
    ctx.recording = recording.get();
    ctx.showEditor = [this] (bool shown) { showEditor (shown); };
    ctx.isEditorShown = [this] { return editorShown; };
    // The custom control assignments live in the show (CC EDIT edits them).
    customControlAssignments = std::make_unique<cc::Assignments> (show);
    ctx.customControls = customControlAssignments.get();
    ctx.customControlsChanged = [this] { skin->customControlsChanged(); };

    show.onChanged = [this] { skin->showChanged(); };
    show.onFxCleared = [this] (int slot)
    {
        if (auto* p = engine.getFx().getPlugin (slot))
            closePluginWindowsFor (p);
    };

    // The console's MIDI protocol: the WING's MIDI remote control and DAW
    // remote control (SETUP > REMOTE and DAW).
    syncRemotes();

    // The web remote (SETUP > REMOTE): starts now if it is enabled.
    webMirror = std::make_unique<WebMirror> (*this, settings, log);
    ctx.web = webMirror.get();
    soloWatcher = std::make_unique<SoloWatcher> (*this);

    // WING remote clients recall scenes (/$ctl/lib) like MIDI does, and change
    // the layers and custom controls (/$ctl/layer, /$ctl/user) like the surface.
    engine.getWing().setSurfaceCallbacks ([this] { return customControlAssignments->currentLayer; },
                                          [this] (int changes, int bank)
                                          {
                                              if ((changes & WingRemote::surfaceControls) != 0)
                                                  customControlAssignments->reload();
                                              if (bank >= 0)
                                                  customControlAssignments->currentLayer = bank;
                                              skin->customControlsChanged();
                                              if ((changes & WingRemote::surfaceLayers) != 0)
                                                  skin->refreshLayers();
                                          });
    engine.getWing().attachShow (&show, [this] (const juce::String& what, const state::ApplyResult& r)
                                 { logRecall (ctx, what, r); });
    // ... the selection and the MIDI keys' and knobs' values, which a WING reports to
    // clients (issues #19, #21) ...
    engine.getWing().setConsoleCallbacks ([this] { return getSelectedStrip(); },
                                          [this] (Strip& strip) { selectStrip (&strip); },
                                          [this] (juce::uint8* cc, juce::uint8* notes)
                                          {
                                              if (ctx.midi != nullptr)
                                                  ctx.midi->getExternalValues (cc, notes);
                                          });
    // ... and SETUP's options that are WING parameters (metering, surface, solo, MIDI, DAW).
    engine.getWing().setHostSettings (&settings,
                                      [this] (const juce::StringArray& keys)
                                      {
                                          for (const auto& k : keys)
                                              if (k.startsWith ("midi.") || k.startsWith ("daw."))
                                              {
                                                  if (ctx.midi != nullptr)
                                                      ctx.midi->apply (MidiRemote::Settings::load (settings));
                                                  break;
                                              }
                                          // The recorder's MODE from a client (/cards/wlive/1/cfg/playmode).
                                          if (keys.contains (daw::Session::playModeKey) && recording != nullptr)
                                              recording->setPlayMode ((daw::Session::PlayMode) juce::jlimit (0, 2, settings.getIntValue (daw::Session::playModeKey)));
                                          if (keys.contains ("ui.selDblClick") || keys.contains ("setup.faderScreenLayerLink"))
                                              notifyLayersChangedAsync (ctx);
                                          // The COMPACT's fader 13 and 16 keys (/$ctl/cfg/mfdr, cscmode, mfdrkeep, dcacc...).
                                          for (const auto& k : keys)
                                              if (k.startsWith ("ui.compact"))
                                              {
                                                  skin->sendsOnFaderChanged();
                                                  break;
                                              }
                                          repaint();
                                      });

    if (! engine.getChannels().empty())
        selectStrip (engine.getChannels()[0].get());

    // The network remotes (WING apps; an X32's OSC on an X32) start with the app
    // whatever the interface: only the WING surface's SETUP page started them, so
    // a start on the Universal Mixer or a console's surface left them off.
    {
        const auto remote = WingRemote::Settings::load (settings);
        for (const auto& error : engine.getWing().apply (remote))
            log.error ("WING remote: " + error);
    }

    // The console model's state beyond the surface (the layers, the WING remote's
    // identity), then the interface (the WING surface unless the settings say
    // the Universal Mixer, or the console isn't a WING).
    applyConsoleModel (look.model, false);
    const auto savedKind = settings.getValue (uiprefs::interfaceKind);
    setInterface (savedKind == "universal" ? InterfaceKind::Universal
                  : savedKind == "x32"     ? InterfaceKind::X32
                  : savedKind == "promix"  ? InterfaceKind::ProMix
                  : savedKind == "02r"     ? InterfaceKind::Yamaha02R
                  : savedKind == "sq5"     ? InterfaceKind::Sq5
                  : savedKind == "01v"     ? InterfaceKind::Yamaha01V
                  : savedKind == "dlive"   ? InterfaceKind::DLive
                  : savedKind == "dm2000"  ? InterfaceKind::Dm2000
                  : savedKind == "pro2"    ? InterfaceKind::Pro2
                  : savedKind == "xenyx1202" ? InterfaceKind::Xenyx1202
                                           : InterfaceKind::Wing,
                  false);
    setWantsKeyboardFocus (true);
    setSize (1680, 1000);
    refreshAll();
    updateTitle();
    openLastProject();
}

MainComponent::~MainComponent()
{
    // The web remote stops watching the window and closes its browsers.
    ctx.web = nullptr;
    webMirror.reset();
    // MIDI and the WING remote's scene and surface control go first: their callbacks reach the views.
    if (isRemoteActive())
        endRemote (false); // the console is let go and the local show comes back before anything shuts down
    engine.getWing().attachShow (nullptr);
    engine.getWing().setSurfaceCallbacks ({}, {});
    engine.getWing().setConsoleCallbacks ({}, {}, {});
    engine.getWing().setHostSettings (nullptr);
    ctx.midi = nullptr;
    midi.reset();
    engine.onPluginWarning = nullptr;

    // Unsaved project changes survive the quit in the autosave (offered on the next open).
    recording->autosave();
    soloWatcher.reset();
    editor.reset();
    show.onChanged = nullptr;
    show.onFxCleared = nullptr;
    closeAllPluginWindows();
    scannerWindow.reset();
    pluginOverview.reset(); // (stays "open" in the settings: it reopens next time)
    skin.reset();
    ctx.recording = nullptr;
    recording.reset();
    setLookAndFeel (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

//==============================================================================
Strip* MainComponent::getSelectedStrip() const
{
    return hasSelection ? state::findStrip (engine, selected) : nullptr;
}

void MainComponent::selectStrip (Strip* strip)
{
    hasSelection = strip != nullptr;
    if (strip != nullptr)
        selected = state::StripRef::of (*strip);

    // SOLO FOLLOWS SELECT: while anything is soloed, the solo moves to the selection.
    if (strip != nullptr && soloWatcher != nullptr && settings.getBoolValue (uiprefs::soloFollowsSelect, false) && engine.anySoloActive.load()
        && ! strip->params.solo.load())
    {
        soloWatcher->clearAllBut (nullptr);
        strip->params.solo = true;
        soloWatcher->snapshot();
    }
    if (skin != nullptr)
        skin->selectionChanged();
}

void MainComponent::refreshAll()
{
    // The console's faders move along its profile's law; its routing pages
    // show its profile's port groups, its strips its profile's labels and colours.
    ui::fader::setLaw (engine.getFaderLaw());
    ui::routing::setProfile (engine.getProfile());
    setShownProfile (engine.getProfile());
    setShownSlots (engine.getHandle());

    // A loaded or new show brings its own custom controls.
    customControlAssignments->reload();

    // Keep a valid selection after structure changes and show loads.
    if (getSelectedStrip() == nullptr)
    {
        hasSelection = false;
        if (! engine.getChannels().empty())
        {
            selected = state::StripRef::of (*engine.getChannels()[0]);
            hasSelection = true;
        }
    }

    // A console that isn't a WING (a show of one was loaded): the X32 surface
    // shows an X32, the ProMix surface a ProMix 01, the Universal Mixer any
    // other; a console's surface without its console gives way to the Universal Mixer.
    const bool x32 = engine.getProfile() == ld::kProfileX32, promix = engine.getProfile() == ld::kProfileProMix01,
               y02r = engine.getProfile() == ld::kProfileYamaha02R, sq5 = engine.getProfile() == ld::kProfileSq5,
               y01v = engine.getProfile() == ld::kProfileYamaha01V, dlive = engine.getProfile() == ld::kProfileDLive,
               dm2000 = engine.getProfile() == ld::kProfileDm2000,
               pro2 = engine.getProfile() == ld::kProfileMidasPro2, xenyx = engine.getProfile() == ld::kProfileXenyx1202;
    if (ctx.interfaceKind == InterfaceKind::Wing && ! isWingConsole())
    {
        log.info (x32      ? "This console is an X32: the X32 surface shows it"
                  : promix ? "This console is a ProMix 01: the ProMix surface shows it"
                  : y02r   ? "This console is an 02R: the 02R surface shows it"
                  : sq5    ? "This console is an SQ-5: the SQ-5 surface shows it"
                  : y01v   ? "This console is a 01V: the Universal Mixer shows it"
                  : dlive  ? "This console is a dLive: the dLive surface shows it"
                  : dm2000 ? "This console is a DM2000: the DM2000 surface shows it"
                  : pro2   ? "This console is a PRO2: the PRO2 surface shows it"
                  : xenyx  ? "This console is a XENYX Q1202USB: its surface shows it"
                           : "This console isn't a WING: the Universal Mixer shows it (SETUP > GENERAL > INTERFACE)");
        setInterface (x32 ? InterfaceKind::X32 : promix ? InterfaceKind::ProMix : y02r ? InterfaceKind::Yamaha02R : sq5 ? InterfaceKind::Sq5 : y01v ? InterfaceKind::Yamaha01V : dlive ? InterfaceKind::DLive : dm2000 ? InterfaceKind::Dm2000 : pro2 ? InterfaceKind::Pro2 : xenyx ? InterfaceKind::Xenyx1202 : InterfaceKind::Universal, false);
    }
    else if ((ctx.interfaceKind == InterfaceKind::X32 && ! x32) || (ctx.interfaceKind == InterfaceKind::ProMix && ! promix)
             || (ctx.interfaceKind == InterfaceKind::Yamaha02R && ! y02r) || (ctx.interfaceKind == InterfaceKind::Sq5 && ! sq5)
             || (ctx.interfaceKind == InterfaceKind::Yamaha01V && ! y01v) || (ctx.interfaceKind == InterfaceKind::DLive && ! dlive)
             || (ctx.interfaceKind == InterfaceKind::Dm2000 && ! dm2000)
             || (ctx.interfaceKind == InterfaceKind::Pro2 && ! pro2) || (ctx.interfaceKind == InterfaceKind::Xenyx1202 && ! xenyx))
        setInterface (InterfaceKind::Universal, false);
    syncRemotes();
    skin->refreshAll();
}

void MainComponent::syncRemotes()
{
    const auto remotes = engine.getRemotes();
    if (midi == nullptr ? remotes.midi != MidiProtocol::None : midi->getId() != remotes.midi)
    {
        ctx.midi = nullptr;
        ctx.consoleMidi = nullptr;
        midi.reset(); // its devices close before the next one opens them
        midi = MidiProtocol::create (remotes.midi, engine, show);
        ctx.midi = midi != nullptr ? midi->asWing() : nullptr;
        ctx.consoleMidi = midi.get();
        midiui::connect (ctx);
        if (midi != nullptr && ctx.midi == nullptr)
            midi->loadSettings (settings); // another console's: its own devices
    }
    engine.getWing().refreshProtocols();
}

bool MainComponent::isWingConsole() const
{
    // The console's profile: the engine makes it the Universal once the counts
    // leave what the WING surface addresses, and a show records it.
    return engine.getProfile() == ld::kProfileWing;
}

void MainComponent::setInterface (InterfaceKind kind, bool save)
{
    // Swapping between the X32 surface and the Universal Mixer keeps the session (it lives in the engine);
    // a surface that would turn the console into another one can't be opened meanwhile.
    if (isRemoteActive() && kind != InterfaceKind::X32 && kind != InterfaceKind::Universal)
    {
        refuseWhileRemote ("SURFACE");
        return;
    }
    if (kind == InterfaceKind::Wing && skin != nullptr && ! isWingConsole())
    {
        // The WING surface addresses the WING's buses, mains and matrices: offer
        // to make this a fresh WING console (scenes stay; the console is cleared).
        popups::confirm (skin->popupHost(), "WING SURFACE",
                         "This console isn't a WING. The WING surface shows a WING: clear the console and make it a fresh 40 channel WING?", "MAKE IT A WING",
                         [this, save]
                         {
                             closeAllPluginWindows();
                             engine.configure (nullptr);
                             setInterface (InterfaceKind::Wing, save);
                             refreshAll();
                             log.info ("The console is a WING again");
                         }, true);
        return;
    }
    if (kind == InterfaceKind::X32 && skin != nullptr && engine.getProfile() != ld::kProfileX32)
    {
        // Likewise the X32 surface: an X32's 32 channels, 16 buses, 6 matrices.
        popups::confirm (skin->popupHost(), "X32 SURFACE",
                         "This console isn't an X32. The X32 surface shows an X32: clear the console and make it a fresh X32 (32 channels, 8 aux, "
                         "8 FX returns, 16 buses, 6 matrices)?",
                         "MAKE IT AN X32",
                         [this, save]
                         {
                             closeAllPluginWindows();
                             engine.configureProfile (ld::kProfileX32);
                             setInterface (InterfaceKind::X32, save);
                             refreshAll();
                             log.info ("The console is an X32");
                         }, true);
        return;
    }
    if (kind == InterfaceKind::ProMix && skin != nullptr && engine.getProfile() != ld::kProfileProMix01)
    {
        // Likewise the ProMix surface: a ProMix 01's 16 channels, 4 sends, 2 effects.
        popups::confirm (skin->popupHost(), "PROMIX SURFACE",
                         "This console isn't a ProMix 01. The ProMix surface shows a ProMix 01: clear the console and make it a fresh "
                         "ProMix 01 (16 channels, ST IN, two effects and their returns, SEND1-4, ST OUT)?",
                         "MAKE IT A PROMIX 01",
                         [this, save]
                         {
                             closeAllPluginWindows();
                             engine.configureProfile (ld::kProfileProMix01);
                             setInterface (InterfaceKind::ProMix, save);
                             refreshAll();
                             log.info ("The console is a ProMix 01");
                         }, true);
        return;
    }
    if (kind == InterfaceKind::Yamaha02R && skin != nullptr && engine.getProfile() != ld::kProfileYamaha02R)
    {
        // Likewise the 02R surface: an 02R's 24 inputs, 16 tape returns, 8 buses, 8 aux.
        popups::confirm (skin->popupHost(), "02R SURFACE",
                         "This console isn't an 02R. The 02R surface shows an 02R: clear the console and make it a fresh "
                         "02R (MIC/LINE 1-16, LINE 17-24, TAPE 1-16, two effects and their returns, BUS 1-8, AUX 1-8, STEREO)?",
                         "MAKE IT AN 02R",
                         [this, save]
                         {
                             closeAllPluginWindows();
                             engine.configureProfile (ld::kProfileYamaha02R);
                             setInterface (InterfaceKind::Yamaha02R, save);
                             refreshAll();
                             log.info ("The console is an 02R");
                         }, true);
        return;
    }
    if (kind == InterfaceKind::Sq5 && skin != nullptr && engine.getProfile() != ld::kProfileSq5)
    {
        // Likewise an SQ-5: its 48 inputs, 12 mixes and 4 FX sends.
        popups::confirm (skin->popupHost(), "SQ-5",
                         "This console isn't an SQ-5. Clear the console and make it a fresh SQ-5 (48 inputs, 12 mixes, "
                         "4 FX sends, 8 FX and their returns, LR, 3 matrices, 8 DCAs, 8 mute groups)?",
                         "MAKE IT AN SQ-5",
                         [this, save]
                         {
                             closeAllPluginWindows();
                             engine.configureProfile (ld::kProfileSq5);
                             setInterface (InterfaceKind::Sq5, save);
                             refreshAll();
                             log.info ("The console is an SQ-5");
                         }, true);
        return;
    }
    if (kind == InterfaceKind::Yamaha01V && skin != nullptr && engine.getProfile() != ld::kProfileYamaha01V)
    {
        // Likewise a 01V: its 24 inputs, 4 auxes, 4 buses and 2 effects.
        popups::confirm (skin->popupHost(), "01V",
                         "This console isn't a 01V. Clear the console and make it a fresh 01V (24 inputs on 22 channels, "
                         "4 auxes, 4 buses, 2 effect sends and returns, STEREO)?",
                         "MAKE IT A 01V",
                         [this, save]
                         {
                             closeAllPluginWindows();
                             engine.configureProfile (ld::kProfileYamaha01V);
                             setInterface (InterfaceKind::Yamaha01V, save);
                             refreshAll();
                             log.info ("The console is a 01V");
                         }, true);
        return;
    }
    if (kind == InterfaceKind::DLive && skin != nullptr && engine.getProfile() != ld::kProfileDLive)
    {
        // Likewise a dLive, as its FoH template lays out its mixes.
        popups::confirm (skin->popupHost(), "DLIVE",
                         "This console isn't a dLive. Clear the console and make it a fresh dLive (128 inputs on 124 channels, "
                         "8 groups, 16 auxes, 8 FX sends, 16 FX, LR, 8 matrices, 24 DCAs)?",
                         "MAKE IT A DLIVE",
                         [this, save]
                         {
                             closeAllPluginWindows();
                             engine.configureProfile (ld::kProfileDLive);
                             setInterface (InterfaceKind::DLive, save);
                             refreshAll();
                             log.info ("The console is a dLive");
                         }, true);
        return;
    }
    if (kind == InterfaceKind::Dm2000 && skin != nullptr && engine.getProfile() != ld::kProfileDm2000)
    {
        popups::confirm (skin->popupHost(), "DM2000",
                         "This console isn't a DM2000. Clear the console and make it a fresh DM2000 (96 channels, 8 buses, 12 auxes, "
                         "4 stereo matrices, stereo, 8 effects, 12 fader groups)?",
                         "MAKE IT A DM2000",
                         [this, save]
                         {
                             closeAllPluginWindows();
                             engine.configureProfile (ld::kProfileDm2000);
                             setInterface (InterfaceKind::Dm2000, save);
                             refreshAll();
                             log.info ("The console is a DM2000");
                         }, true);
        return;
    }
    if (kind == InterfaceKind::Xenyx1202 && skin != nullptr && engine.getProfile() != ld::kProfileXenyx1202)
    {
        popups::confirm (skin->popupHost(), "XENYX Q1202USB",
                         "This console isn't a XENYX Q1202USB. Clear the console and make it a fresh one (4 mono and 4 stereo channels, "
                         "the FX send, MAIN MIX)?",
                         "MAKE IT A Q1202USB",
                         [this, save]
                         {
                             closeAllPluginWindows();
                             engine.configureProfile (ld::kProfileXenyx1202);
                             setInterface (InterfaceKind::Xenyx1202, save);
                             refreshAll();
                             log.info ("The console is a XENYX Q1202USB");
                         }, true);
        return;
    }
    if (kind == InterfaceKind::Pro2 && skin != nullptr && engine.getProfile() != ld::kProfileMidasPro2)
    {
        popups::confirm (skin->popupHost(), "PRO2",
                         "This console isn't a PRO2. Clear the console and make it a fresh Midas PRO2 (56 channels, 8 aux returns, 16 aux sends, "
                         "8 matrices, stereo and mono masters, 8 VCAs, 6 effects)?",
                         "MAKE IT A PRO2",
                         [this, save]
                         {
                             closeAllPluginWindows();
                             engine.configureProfile (ld::kProfileMidasPro2);
                             setInterface (InterfaceKind::Pro2, save);
                             refreshAll();
                             log.info ("The console is a Midas PRO2");
                         }, true);
        return;
    }
    if (save)
        settings.setValue (uiprefs::interfaceKind, kind == InterfaceKind::Universal ? "universal"
                                                   : kind == InterfaceKind::X32     ? "x32"
                                                   : kind == InterfaceKind::ProMix  ? "promix"
                                                   : kind == InterfaceKind::Yamaha02R ? "02r"
                                                   : kind == InterfaceKind::Sq5     ? "sq5"
                                                   : kind == InterfaceKind::Yamaha01V ? "01v"
                                                   : kind == InterfaceKind::DLive   ? "dlive"
                                                   : kind == InterfaceKind::Dm2000  ? "dm2000"
                                                   : kind == InterfaceKind::Pro2    ? "pro2"
                                                   : kind == InterfaceKind::Xenyx1202 ? "xenyx1202"
                                                                                    : "wing");
    if (skin != nullptr && kind == ctx.interfaceKind)
        return;
    ctx.interfaceKind = kind;
    if (skin != nullptr)
        removeChildComponent (skin.get());
    skin.reset();
    if ((kind == InterfaceKind::X32 && engine.getProfile() != ld::kProfileX32)
        || (kind == InterfaceKind::ProMix && engine.getProfile() != ld::kProfileProMix01)
        || (kind == InterfaceKind::Yamaha02R && engine.getProfile() != ld::kProfileYamaha02R)
        || (kind == InterfaceKind::Sq5 && engine.getProfile() != ld::kProfileSq5)
        || (kind == InterfaceKind::Yamaha01V && engine.getProfile() != ld::kProfileYamaha01V)
        || (kind == InterfaceKind::DLive && engine.getProfile() != ld::kProfileDLive)
        || (kind == InterfaceKind::Dm2000 && engine.getProfile() != ld::kProfileDm2000)
        || (kind == InterfaceKind::Xenyx1202 && engine.getProfile() != ld::kProfileXenyx1202)
        || (kind == InterfaceKind::Pro2 && engine.getProfile() != ld::kProfileMidasPro2))
        kind = ctx.interfaceKind = InterfaceKind::Universal; // a console's surface shows that console only
    if (kind == InterfaceKind::Wing)
        skin = std::make_unique<WingSurface> (ctx);
    else if (kind == InterfaceKind::X32)
        skin = std::make_unique<X32Surface> (ctx);
    else if (kind == InterfaceKind::ProMix)
        skin = std::make_unique<ProMixSurface> (ctx);
    else if (kind == InterfaceKind::Yamaha02R)
        skin = std::make_unique<Y02RSurface> (ctx);
    else if (kind == InterfaceKind::Sq5)
        skin = std::make_unique<Sq5Surface> (ctx);
    else if (kind == InterfaceKind::Yamaha01V)
        skin = std::make_unique<Y01VSurface> (ctx);
    else if (kind == InterfaceKind::DLive)
    {
        if (settings.getValue (uiprefs::dliveSurface, "c3500") == "c1500")
            skin = std::make_unique<DLiveSurface> (ctx);
        else
            skin = std::make_unique<DLiveC3500Surface> (ctx);
    }
    else if (kind == InterfaceKind::Dm2000)
        skin = std::make_unique<Dm2000Surface> (ctx);
    else if (kind == InterfaceKind::Pro2)
        skin = std::make_unique<Pro2Surface> (ctx);
    else if (kind == InterfaceKind::Xenyx1202)
        skin = std::make_unique<Xenyx1202Surface> (ctx);
    else
        skin = std::make_unique<UniversalSkin> (ctx);
    addChildComponent (*skin, 0); // under the editor and anything over the window
    skin->setCovered (editorShown);
    skin->setVisible (true);
    skin->setBounds (getLocalBounds());
    if (save)
    {
        skin->refreshAll();
        log.info (kind == InterfaceKind::Universal ? "Interface: the Universal Mixer (preview)"
                  : kind == InterfaceKind::X32     ? "Interface: the X32 surface"
                  : kind == InterfaceKind::ProMix  ? "Interface: the ProMix 01 surface"
                  : kind == InterfaceKind::Yamaha02R ? "Interface: the 02R surface"
                  : kind == InterfaceKind::Sq5     ? "Interface: the SQ-5 surface"
                  : kind == InterfaceKind::Yamaha01V ? "Interface: the 01V surface"
                  : kind == InterfaceKind::DLive   ? "Interface: the dLive surface"
                  : kind == InterfaceKind::Dm2000  ? "Interface: the DM2000 surface"
                  : kind == InterfaceKind::Pro2    ? "Interface: the PRO2 surface"
                  : kind == InterfaceKind::Xenyx1202 ? "Interface: the XENYX Q1202USB surface"
                                                   : "Interface: the WING surface");
    }
}

void MainComponent::applyConsoleModel (ConsoleModel model, bool save)
{
    if (save)
    {
        auto look = ConsoleLook::load (settings);
        look.model = model;
        look.save (settings);
        log.info ("Console model: " + consoleModelName (model));
    }
    const bool changed = model != ctx.consoleModel;
    ctx.consoleModel = model;
    // The layers (the COMPACT's section and keys), the WING remote's identity,
    // the colours and the parts of the surface.
    show.layers.setModel (model);
    engine.getWing().setConsoleModel (model);
    engine.getWing().setBlackFinish (ConsoleLook::load (settings).finish == SurfaceFinish::Black);
    setSurfaceFinish (ConsoleLook::load (settings).shownFinish());
    if (auto* wing = dynamic_cast<WingSurface*> (skin.get()))
        wing->consoleModelChanged (changed);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void MainComponent::resized()
{
    if (editor != nullptr)
        editor->setBounds (getLocalBounds());
    if (skin != nullptr)
        skin->setBounds (getLocalBounds());
    if (mixerChooserWindow != nullptr)
        mixerChooserWindow->setBounds (getLocalBounds());
}

void MainComponent::focusGained (FocusChangeType)
{
    // Something over the whole console that takes the keyboard (a pop-up such
    // as the start-up notice, the editor) keeps it when the window gets the focus.
    for (int i = getNumChildComponents(); --i >= 0;)
        if (auto* c = getChildComponent (i); c->isVisible() && c->getWantsKeyboardFocus() && c->getBounds() == getLocalBounds())
        {
            c->grabKeyboardFocus();
            return;
        }
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (key.getKeyCode() == juce::KeyPress::F11Key && ! mods.isAnyModifierKeyDown())
    {
        // Switching recreates the window on Linux; a press that reaches the new
        // window as well mustn't switch back.
        if (const auto now = juce::Time::getMillisecondCounter(); now - lastFullScreenSwitch > 500)
        {
            lastFullScreenSwitch = now;
            setFullScreen (! fullScreen);
        }
        return true;
    }
    if (key.getModifiers().isCommandDown())
    {
        // With Ctrl held, X11 gives a control code as the text and the lowercase key as the code.
        const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
        const auto k = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getKeyCode());
        if (c == 'e' || k == 'e')
        {
            showEditor (! editorShown);
            return true;
        }
        if (c == 's' || k == 's')
        {
            saveShow (key.getModifiers().isShiftDown());
            return true;
        }
        if (c == 'o' || k == 'o')
        {
            openShow();
            return true;
        }
        if (c == 'g' || k == 'g')
        {
            logRecall (ctx, "next scene", show.recallNext());
            return true;
        }
        if ((c == 'p' || k == 'p') && key.getModifiers().isShiftDown())
        {
            showPluginOverview();
            return true;
        }
    }
    // The skin's keys (the WING surface: screen only, screenshot, surface lock), then
    // Esc leaves full screen.
    if (skin != nullptr && skin->skinKeyPressed (key))
        return true;
    if (key == juce::KeyPress::escapeKey && ! editorShown && fullScreen)
    {
        setFullScreen (false);
        return true;
    }
    return false;
}

//==============================================================================
void MainComponent::showPluginScanner()
{
    if (scannerWindow != nullptr)
    {
        scannerWindow->toFront (true);
        return;
    }

    class ScannerWindow final : public juce::DocumentWindow
    {
    public:
        ScannerWindow (PluginHost& h, MainComponent& o)
            : juce::DocumentWindow ("Plugins (VST3 / AU / LV2) - scanned in a separate process", Colours::panel, closeButton),
              owner (o)
        {
            setUsingNativeTitleBar (true);
            auto* list = new juce::PluginListComponent (h.getFormatManager(), h.getKnownPlugins(),
                                                        h.getDeadMansPedalFile(), h.getSettings(), true);
            list->setSize (720, 500);
            setContentOwned (list, true);
            setResizable (true, false);
            centreWithSize (720, 500);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::MessageManager::callAsync ([o = juce::Component::SafePointer<MainComponent> (&owner)]
            {
                if (o != nullptr)
                    o->scannerWindow.reset();
            });
        }

    private:
        MainComponent& owner;
    };

    scannerWindow = std::make_unique<ScannerWindow> (ctx.host, *this);
}

void MainComponent::showPluginOverview()
{
    if (pluginOverview != nullptr)
    {
        pluginOverview->setMinimised (false);
        pluginOverview->toFront (true);
        return;
    }
    pluginOverview = std::make_unique<PluginOverviewWindow> (ctx, [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr)
            safe->pluginOverview.reset();
    });
}

void MainComponent::captureNextPluginPicture()
{
    if (captureQueue.empty())
    {
        if (auto progress = std::exchange (captureProgress, nullptr))
            progress (captureTotal, captureTotal);
        return;
    }
    const int slot = captureQueue.front();
    captureQueue.erase (captureQueue.begin());
    auto windowFor = [this] (int s) -> PluginChainWindow*
    {
        for (auto* w : pluginWindows)
            if (w->getSlot() == s)
                return w;
        return nullptr;
    };
    const bool wasOpen = windowFor (slot) != nullptr;
    if (! engine.getFx().hasPlugin (slot))
    {
        captureNextPluginPicture();
        return;
    }
    openPluginEditor (slot);
    // Long enough for the editors to open and draw themselves.
    juce::Timer::callAfterDelay (kCaptureWaitMs, [safe = juce::Component::SafePointer<MainComponent> (this), slot, wasOpen, windowFor]
    {
        if (safe == nullptr)
            return;
        if (auto* w = windowFor (slot))
        {
            w->capturePictures();
            if (! wasOpen)
                safe->pluginWindows.removeObject (w);
        }
        if (safe->captureProgress)
            safe->captureProgress (safe->captureTotal - (int) safe->captureQueue.size(), safe->captureTotal);
        safe->captureNextPluginPicture();
    });
}

void MainComponent::prunePluginPictures()
{
    std::vector<const juce::AudioProcessor*> live;
    auto& fx = engine.getFx();
    for (int slot = 0; slot < kMaxFx; ++slot)
        for (int i = 0; i < fx.getNumChainMembers (slot); ++i)
            live.push_back (fx.getChainMember (slot, i));
    pluginPictures.retainOnly (live);
}

void MainComponent::openPluginEditor (int slot)
{
    if (! engine.getFx().hasPlugin (slot))
        return;

    for (auto* w : pluginWindows)
    {
        if (w->getSlot() == slot)
        {
            w->toFront (true);
            return;
        }
    }

    // One window per slot: its plugins' editors side by side.
    pluginWindows.add (new PluginChainWindow (ctx, slot, [this] (PluginChainWindow& w) { pluginWindows.removeObject (&w); }));
}

void MainComponent::closePluginWindowsFor (juce::AudioProcessor* p)
{
    // The slot's plugin (or chain) closes its window; a plugin of a chain only its editor.
    for (int i = pluginWindows.size(); --i >= 0;)
    {
        // The slot's current plugin counts too: the window notices a chain
        // swapped elsewhere only on its next refresh.
        if (pluginWindows[i]->getShownProcessor() == p || engine.getFx().getPlugin (pluginWindows[i]->getSlot()) == p)
            pluginWindows.remove (i);
        else
            pluginWindows[i]->dropEditorFor (p);
    }
}

void MainComponent::closeAllPluginWindows()
{
    pluginWindows.clear();
}

//==============================================================================
void MainComponent::showEditor (bool shown)
{
    if (shown && editor == nullptr)
    {
        editor = std::make_unique<DawEditor> (ctx, *recording);
        editor->onClose = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe != nullptr) safe->showEditor (false); }); };
        addChildComponent (*editor);
        editor->setBounds (getLocalBounds());
    }
    editorShown = shown;
    skin->setCovered (shown);
    if (editor != nullptr)
    {
        editor->setVisible (shown);
        if (shown)
        {
            editor->toFront (true);
            editor->grabKeyboardFocus();
        }
    }
    if (! shown)
        grabKeyboardFocus();
}

void MainComponent::setFullScreen (bool shouldBe)
{
    auto* window = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent());
    if (window == nullptr || window == static_cast<juce::Component*> (this) || shouldBe == fullScreen)
        return;
    fullScreen = shouldBe;
    settings.setValue (uiprefs::fullScreen, shouldBe);
    if (shouldBe)
    {
        windowStateBeforeFullScreen = window->getWindowStateAsString();
#if JUCE_LINUX || JUCE_BSD
        // X11: JUCE's kiosk mode only resizes the window, so drop the native
        // frame first (an undecorated window over the whole screen).
        window->setUsingNativeTitleBar (false);
        window->setTitleBarHeight (0);
        window->setResizable (false, false);
#endif
        juce::Desktop::getInstance().setKioskModeComponent (window, false);
    }
    else
    {
        juce::Desktop::getInstance().setKioskModeComponent (nullptr);
#if JUCE_LINUX || JUCE_BSD
        window->setUsingNativeTitleBar (true);
        window->setResizable (true, true);
#endif
        if (windowStateBeforeFullScreen.isNotEmpty())
            window->restoreWindowStateFromString (windowStateBeforeFullScreen);
        windowStateBeforeFullScreen.clear();
    }
    log.info (shouldBe ? "Full screen (F11 or Esc to leave)" : "Left full screen");
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr && safe->isShowing() && ! safe->editorShown)
            safe->grabKeyboardFocus();
    });
}

void MainComponent::showStartupNotice()
{
    startupNotice::show (settings, *this, true);
}

void MainComponent::showMixerChooser (bool canContinue, std::function<void()> beforeFresh, std::function<void()> done)
{
    // Closing destroys the window whose key is calling: after that key's callback.
    const auto close = [this, done]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), done]
        {
            if (safe == nullptr)
                return;
            safe->mixerChooserWindow.reset();
            if (done)
                done();
        });
    };
    mixerChooser::Options options;
    if (canContinue)
    {
        options.continueText = mixerChooser::label (ctx.interfaceKind);
        if (showFile != juce::File())
            options.continueText << ", " << showFile.getFileNameWithoutExtension();
    }
    options.onContinue = close;
    options.onMixer = [this, beforeFresh, close] (InterfaceKind kind)
    {
        if (beforeFresh)
            beforeFresh();
        startFresh (kind);
        close();
    };
    mixerChooserWindow = mixerChooser::create (std::move (options));
    addChildComponent (*mixerChooserWindow);
    mixerChooserWindow->setBounds (getLocalBounds());
    mixerChooserWindow->setVisible (true);
}

void MainComponent::startFresh (InterfaceKind kind)
{
    if (refuseWhileRemote ("NEW CONSOLE"))
        return;
    closeAllPluginWindows();
    for (int i = 0; i < kMaxFx; ++i)
        engine.getFx().setPlugin (i, nullptr);
    // The profile first: a console's surface shows only that console.
    if (kind == InterfaceKind::Wing)
        engine.configure (nullptr);
    else
        engine.configureProfile (kind == InterfaceKind::X32         ? ld::kProfileX32
                                 : kind == InterfaceKind::ProMix    ? ld::kProfileProMix01
                                 : kind == InterfaceKind::Yamaha02R ? ld::kProfileYamaha02R
                                 : kind == InterfaceKind::Sq5       ? ld::kProfileSq5
                                 : kind == InterfaceKind::Yamaha01V ? ld::kProfileYamaha01V
                                 : kind == InterfaceKind::DLive     ? ld::kProfileDLive
                                 : kind == InterfaceKind::Dm2000    ? ld::kProfileDm2000
                                 : kind == InterfaceKind::Pro2      ? ld::kProfileMidasPro2
                                 : kind == InterfaceKind::Xenyx1202 ? ld::kProfileXenyx1202
                                                                    : ld::kProfileUniversal);
    show.clear (engine.getConfiguredCounts().channels);
    showFile = juce::File();
    hasSelection = false;
    setInterface (kind, true);
    refreshAll();
    updateTitle();
    log.info ("Started a fresh console: " + mixerChooser::label (kind));
}

void MainComponent::openLastProject()
{
    const auto recent = recording->getRecentProjects();
    const juce::File last = recent.isEmpty() ? juce::File() : juce::File (recent[0]);
    if (! last.existsAsFile())
        return;
    if (! daw::Session::hasNewerAutosave (last))
    {
        if (auto r = recording->openProject (last); r.failed())
            log.warning ("Recording project: " + r.getErrorMessage());
        return;
    }
    // The saved version opens now; RESTORE reopens it with the autosave.
    if (auto r = recording->openProject (last); r.failed())
        log.warning ("Recording project: " + r.getErrorMessage());
    popups::confirm (skin->popupHost(), "RECORDING PROJECT",
                     "\"" + last.getFileNameWithoutExtension() + "\" has unsaved changes from the last session. Restore them?", "RESTORE",
                     [this, last]
                     {
                         if (auto result = recording->openProject (last, true); result.failed())
                             log.warning ("Recording project: " + result.getErrorMessage());
                     });
}

//==============================================================================
void MainComponent::updateTitle()
{
    juce::String title = juce::String (kAppName) + " - " + show.getName();
    if (showFile != juce::File())
        title << "  [" << showFile.getFileName() << "]";
    if (auto* top = getTopLevelComponent(); top != nullptr && top != this)
        top->setName (title);
    else
        setName (title);
}

bool MainComponent::refuseWhileRemote (const juce::String& title)
{
    if (! isRemoteActive() || skin == nullptr)
        return false;
    popups::warn (skin->popupHost(), title, "An X32 is controlled from here. Disconnect first: this would change the whole console, "
                                              "and the console's state must not be overwritten from this app.");
    return true;
}

void MainComponent::connectRemote()
{
    if (skin == nullptr || isRemoteActive())
        return;
    const auto last = ctx.settings.getValue ("remoteX32Ip", "192.168.1.");
    universal::askForText (skin->popupHost(), "CONNECT TO AN X32", "The X32's IP address (on its SETUP > NETWORK page):", last,
                           [this] (const juce::String& ip)
    {
        if (! ip.matchesWildcard ("*.*.*.*", true) || juce::IPAddress (ip).isNull())
        {
            popups::warn (skin->popupHost(), "CONNECT TO AN X32", "\"" + ip + "\" isn't an IP address.");
            return;
        }
        const bool switches = engine.getProfile() != ld::kProfileX32;
        popups::confirm (skin->popupHost(), "CONTROL THE X32 AT " + ip,
                         "The console is read first and wins: its state replaces what is on screen, and this app never sends its own "
                         "state to the console. Only what you change here afterwards goes to it. The console does the audio: this "
                         "computer passes none while connected. Recalling scenes here is off (use the console's; they follow here). "
                         "Your local show is saved first and brought back when you disconnect"
                             + juce::String (switches ? ", and the mixer here becomes an X32 meanwhile." : "."),
                         "CONNECT", [this, ip] { startRemote (ip); }, false);
    });
}

void MainComponent::startRemote (const juce::String& ip)
{
    if (isRemoteActive())
        return;
    // The local show first (its scenes, the console, plugins): it comes back on disconnect.
    const auto file = paths::appData().getChildFile ("remote-recovery").withFileExtension (Show::fileExtension);
    file.getParentDirectory().createDirectory();
    const auto saved = show.saveToFile (file);
    if (saved.failed())
    {
        popups::warn (skin->popupHost(), "CONNECT", "The local show couldn't be kept (" + saved.getErrorMessage() + "), so nothing was changed.");
        return;
    }
    remoteRestoreFile = file;
    remoteRestoreShowFile = showFile;
    if (engine.getProfile() != ld::kProfileX32)
    {
        closeAllPluginWindows();
        for (int i = 0; i < kMaxFx; ++i)
            engine.getFx().setPlugin (i, nullptr);
        engine.configureProfile (ld::kProfileX32);
        show.clear (engine.getConfiguredCounts().channels);
    }
    engine.setRemoteControlled (true);
    show.setRecallLocked (true);
    if (! engine.getRemoteSession().connect (ip))
    {
        endRemote (false);
        popups::warn (skin->popupHost(), "CONNECT", "Couldn't start connecting to " + ip + ".");
        return;
    }
    ctx.settings.setValue ("remoteX32Ip", ip);
    ctx.settings.saveIfNeeded();
    refreshAll();
    log.info ("Controlling the X32 at " + ip + ": its state is read first; audio runs on the console");
}

void MainComponent::endRemote (bool announce)
{
    engine.getRemoteSession().disconnect(); // joins the network thread: nothing is sent after this line
    show.setRecallLocked (false);
    engine.setRemoteControlled (false);
    if (remoteRestoreFile.existsAsFile())
    {
        const auto result = reloadShow (remoteRestoreFile, true);
        showFile = remoteRestoreShowFile;
        if (result.failed())
            log.warning ("The local show couldn't be restored: " + result.getErrorMessage() + " (kept at " + remoteRestoreFile.getFullPathName() + ")");
        else
            remoteRestoreFile.deleteFile();
    }
    remoteRestoreFile = remoteRestoreShowFile = juce::File();
    if (skin != nullptr)
    {
        refreshAll();
        updateTitle();
    }
    if (announce)
        log.info ("Disconnected from the X32: the local show is back, this computer's audio runs again");
}

void MainComponent::newShow()
{
    if (refuseWhileRemote ("NEW SHOW"))
        return;
    // The console stays what it is (its profile and configuration), with its
    // configured channels.
    const int channels = engine.getConfiguredCounts().channels;
    const auto console = isWingConsole() ? juce::String (channels) + " channel WING" : "console of " + juce::String (channels) + " channels";
    popups::confirm (skin->popupHost(), "NEW SHOW", "Clear the console and all scenes and start a new show (a " + console + ")?", "NEW", [this, channels]
    {
        closeAllPluginWindows();
        for (int i = 0; i < kMaxFx; ++i)
            engine.getFx().setPlugin (i, nullptr);
        show.clear (channels);
        showFile = juce::File();
        hasSelection = false;
        refreshAll();
        updateTitle();
        log.info ("New show");
    }, true);
}

juce::Result MainComponent::reloadShow (const juce::File& file, bool loadPlugins)
{
    closeAllPluginWindows();
    const auto result = show.loadFromFile (file, loadPlugins);
    refreshAll();
    updateTitle();
    return result;
}

void MainComponent::openShow()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Open show", showFile, "*.lshow;*.livedaw");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
    {
        if (const auto file = fc.getResult(); file != juce::File())
            openShowFile (file);
    });
}

void MainComponent::openShowFile (const juce::File& file)
{
    if (refuseWhileRemote ("OPEN SHOW"))
        return;
    const auto result = reloadShow (file, true);
    showFile = file.hasFileExtension (Show::fileExtension) ? file : juce::File();
    updateTitle();
    log.info ("Opened show " + file.getFileName());

    if (result.failed())
    {
        log.warning (result.getErrorMessage());
        popups::warn (skin->popupHost(), "OPEN SHOW", result.getErrorMessage());
    }
}

void MainComponent::saveShowTo (juce::File file)
{
    file = file.withFileExtension (Show::fileExtension);
    // As on the WING, the show is called what its file is called (also after
    // SAVE AS under a new name).
    const auto previousName = show.getName();
    show.setName (file.getFileNameWithoutExtension());

    const auto result = show.saveToFile (file);

    if (result.failed())
    {
        show.setName (previousName);
        log.error ("Save failed: " + result.getErrorMessage());
        popups::warn (skin->popupHost(), "SAVE SHOW", result.getErrorMessage());
        return;
    }

    showFile = file;
    log.info ("Saved show " + file.getFileName());
    updateTitle();
    skin->showChanged();
}

void MainComponent::saveShow (bool saveAs)
{
    auto writeTo = [this] (const juce::File& file) { saveShowTo (file); };

    if (! saveAs && showFile != juce::File())
    {
        writeTo (showFile);
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser> ("Save show", showFile, "*.lshow");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [writeTo] (const juce::FileChooser& fc)
    {
        if (fc.getResult() != juce::File())
            writeTo (fc.getResult());
    });
}


} // namespace livedaw::ui
