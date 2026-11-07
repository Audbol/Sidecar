#include "ui/MainComponent.h"
#include "ui/InterfacePage.h"
#include "engine/AppPaths.h"
#include "ui/MidiRemoteUi.h"
#include "ui/RoutingGroups.h"
#include "ui/ProMixSurface.h"
#include "ui/Y02RSurface.h"
#include "ui/X32Surface.h"
#include "ui/PluginChainWindow.h"
#include "ui/PluginOverviewWindow.h"
#include "ui/MixerChooser.h"
#include "ui/StartupNotice.h"
#include "ui/UniversalSkin.h"
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

    // The console model's state beyond the surface (the layers, the WING remote's
    // identity), then the interface (the WING surface unless the settings say
    // the Universal Mixer, or the console isn't a WING).
    applyConsoleModel (look.model, false);
    const auto savedKind = settings.getValue (uiprefs::interfaceKind);
    setInterface (savedKind == "universal" ? InterfaceKind::Universal
                  : savedKind == "x32"     ? InterfaceKind::X32
                  : savedKind == "promix"  ? InterfaceKind::ProMix
                  : savedKind == "02r"     ? InterfaceKind::Yamaha02R
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
               y02r = engine.getProfile() == ld::kProfileYamaha02R;
    if (ctx.interfaceKind == InterfaceKind::Wing && ! isWingConsole())
    {
        log.info (x32      ? "This console is an X32: the X32 surface shows it"
                  : promix ? "This console is a ProMix 01: the ProMix surface shows it"
                  : y02r   ? "This console is an 02R: the 02R surface shows it"
                           : "This console isn't a WING: the Universal Mixer shows it (SETUP > GENERAL > INTERFACE)");
        setInterface (x32 ? InterfaceKind::X32 : promix ? InterfaceKind::ProMix : y02r ? InterfaceKind::Yamaha02R : InterfaceKind::Universal, false);
    }
    else if ((ctx.interfaceKind == InterfaceKind::X32 && ! x32) || (ctx.interfaceKind == InterfaceKind::ProMix && ! promix)
             || (ctx.interfaceKind == InterfaceKind::Yamaha02R && ! y02r))
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
    if (save)
        settings.setValue (uiprefs::interfaceKind, kind == InterfaceKind::Universal ? "universal"
                                                   : kind == InterfaceKind::X32     ? "x32"
                                                   : kind == InterfaceKind::ProMix  ? "promix"
                                                   : kind == InterfaceKind::Yamaha02R ? "02r"
                                                                                    : "wing");
    if (skin != nullptr && kind == ctx.interfaceKind)
        return;
    ctx.interfaceKind = kind;
    if (skin != nullptr)
        removeChildComponent (skin.get());
    skin.reset();
    if ((kind == InterfaceKind::X32 && engine.getProfile() != ld::kProfileX32)
        || (kind == InterfaceKind::ProMix && engine.getProfile() != ld::kProfileProMix01)
        || (kind == InterfaceKind::Yamaha02R && engine.getProfile() != ld::kProfileYamaha02R))
        kind = ctx.interfaceKind = InterfaceKind::Universal; // a console's surface shows that console only
    if (kind == InterfaceKind::Wing)
        skin = std::make_unique<WingSurface> (ctx);
    else if (kind == InterfaceKind::X32)
        skin = std::make_unique<X32Surface> (ctx);
    else if (kind == InterfaceKind::ProMix)
        skin = std::make_unique<ProMixSurface> (ctx);
    else if (kind == InterfaceKind::Yamaha02R)
        skin = std::make_unique<Y02RSurface> (ctx);
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

void MainComponent::newShow()
{
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
