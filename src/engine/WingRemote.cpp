#include "engine/WingRemote.h"
#include "engine/AppPaths.h"
#include "engine/ConsoleState.h"

#include "engine/MixerEngine.h"
#include "engine/RustStrings.h"
#include "engine/Show.h"
#include "engine/SqMidi.h"

#include <array>

namespace livedaw
{

WingRemote::WingRemote (MixerEngine& e)
    : engine (e), handle (ld::ld_wing_create (e.getHandle())), x32 (ld::ld_x32_create (e.getHandle())),
      ahMidi (ld::ld_ahmidi_create (e.getHandle()))
{
}

WingRemote::~WingRemote()
{
    stopTimer();
    ld::ld_ahmidi_destroy (ahMidi);
    ld::ld_x32_destroy (x32);
    ld::ld_wing_destroy (handle);
}

//==============================================================================
WingRemote::Settings WingRemote::Settings::load (juce::PropertiesFile& p)
{
    Settings s;
    s.enabled = p.getBoolValue ("wing.enabled", s.enabled);
    s.name = p.getValue (nameKey, s.name).trim();
    if (s.name.isEmpty())
        s.name = "SIDECAR";
    auto port = [&p] (const char* key, int fallback)
    {
        const int v = p.getIntValue (key, fallback);
        return v > 0 && v < 65536 ? v : fallback;
    };
    s.discoveryPort = port ("wing.discoveryPort", s.discoveryPort);
    s.oscPort = port ("wing.oscPort", s.oscPort);
    s.nativePort = port ("wing.nativePort", s.nativePort);
    s.discovery = p.getBoolValue ("wing.discovery", s.discovery);
    s.osc = p.getBoolValue ("wing.osc", s.osc);
    s.native = p.getBoolValue ("wing.native", s.native);
    s.sqMidiSetup = p.getValue (SqMidi::setupKey);
    s.dliveMidiChannel = juce::jlimit (1, 12, p.getIntValue ("dlive.midiChannel", 1));
    return s;
}

void WingRemote::Settings::save (juce::PropertiesFile& p) const
{
    p.setValue ("wing.enabled", enabled);
    p.setValue (nameKey, name);
    p.setValue ("wing.discoveryPort", discoveryPort);
    p.setValue ("wing.oscPort", oscPort);
    p.setValue ("wing.nativePort", nativePort);
    p.setValue ("wing.discovery", discovery);
    p.setValue ("wing.osc", osc);
    p.setValue ("wing.native", native);
}

bool WingRemote::Settings::operator== (const Settings& o) const
{
    return enabled == o.enabled && name == o.name && discoveryPort == o.discoveryPort && oscPort == o.oscPort
           && nativePort == o.nativePort && discovery == o.discovery && osc == o.osc && native == o.native;
}

//==============================================================================
juce::StringArray WingRemote::apply (const Settings& s)
{
    auto errors = applyWing (s);
    if (! applyX32 (s))
        errors.add ("X32 OSC: UDP port " + juce::String (x32Port) + " is in use");
    if (! applyAhMidi (s))
        errors.add ("MIDI over TCP: a TCP port (" + juce::String (ahMixRackPort) + ", " + juce::String (ahSurfacePort) + ") is in use");
    return errors;
}

bool WingRemote::applyAhMidi (const Settings& s)
{
    // Allen & Heath MIDI over TCP while the console is a dLive or SQ-5 and the
    // network remote is on.
    const auto profile = engine.getProfile();
    const bool wanted = s.enabled && ! paths::externalActionsBlocked().load()
                        && (engine.getRemotes().network & ahMidiBit) != 0;
    if (! wanted)
    {
        ld::ld_ahmidi_stop (ahMidi);
        ahMidiProfile = -1;
        return true;
    }
    const bool dlive = profile == ld::kProfileDLive;
    if (dlive)
    {
        ld::ld_ahmidi_set_setup (ahMidi, s.dliveMidiChannel - 1, false);
    }
    else
    {
        // The SQ's: its MIDI's own setting (SqMidi): the channel 1-16 and the fader law.
        juce::StringArray v;
        v.addTokens (s.sqMidiSetup, " ", "");
        v.removeEmptyStrings();
        ld::ld_ahmidi_set_setup (ahMidi, v.size() >= 2 ? juce::jlimit (0, 15, v[0].getIntValue() - 1) : 0, v.size() >= 2 && v[1].getIntValue() != 0);
    }
    if (isAhMidiRunning() && ahMidiProfile == (int) profile)
        return true;
    ahMidiProfile = (int) profile;
    ahLastScene = -2; // the current scene as it is at the next pump
    return ld::ld_ahmidi_start (ahMidi, ahMixRackPort, dlive ? ahSurfacePort : -1);
}

bool WingRemote::isAhMidiRunning() const
{
    return ld::ld_ahmidi_port (ahMidi, 0) >= 0;
}

int WingRemote::getAhMidiClients() const
{
    return ld::ld_ahmidi_clients (ahMidi);
}

bool WingRemote::applyX32 (const Settings& s)
{
    // The X32's OSC while the console is one and the network remote is on.
    const bool wanted = s.enabled && ! paths::externalActionsBlocked().load()
                        && (engine.getRemotes().network & x32OscBit) != 0;
    if (! wanted)
    {
        ld::ld_x32_stop (x32);
        x32Name.clear();
        return true;
    }
    if (isX32Running() && x32Name == s.name)
        return true;
    const Utf8 name (s.name);
    x32Name = s.name;
    return ld::ld_x32_start (x32, name.data, name.size, x32Port);
}

bool WingRemote::isX32Running() const
{
    return ld::ld_x32_port (x32) >= 0;
}

juce::String WingRemote::writeX32Scene (const juce::String& name, const juce::String& note) const
{
    const Utf8 n (name), t (note);
    return takeString (ld::ld_x32_scene_write (x32, n.data, n.size, t.data, t.size));
}

juce::String WingRemote::runX32Text (const juce::String& text, int& set)
{
    const Utf8 t (text);
    juce::int32 n = -1;
    auto answer = takeString (ld::ld_x32_sysex (x32, t.data, t.size, &n));
    set = (int) n;
    return answer;
}

WingRemote::X32SceneRead WingRemote::readX32Scene (const juce::String& text)
{
    const Utf8 t (text);
    ld::LdX32SceneRead out {};
    X32SceneRead r;
    r.ok = ld::ld_x32_scene_read (x32, t.data, t.size, &out);
    if (r.ok)
    {
        r.applied = out.applied;
        r.skipped = out.skipped;
        r.name = takeString (out.name);
        r.note = takeString (out.note);
    }
    return r;
}

juce::String WingRemote::writeX32Channel (const juce::String& strip, const juce::String& name) const
{
    const Utf8 s (strip), n (name);
    return takeString (ld::ld_x32_channel_write (x32, s.data, s.size, n.data, n.size));
}

WingRemote::X32SceneRead WingRemote::readX32Channel (const juce::String& strip, const juce::String& text)
{
    const Utf8 s (strip), t (text);
    ld::LdX32SceneRead out {};
    X32SceneRead r;
    r.ok = ld::ld_x32_channel_read (x32, s.data, s.size, t.data, t.size, &out);
    if (r.ok)
    {
        r.applied = out.applied;
        r.skipped = out.skipped;
        r.name = takeString (out.name);
    }
    return r;
}

bool WingRemote::initX32Channel (const juce::String& strip)
{
    const Utf8 s (strip);
    return ld::ld_x32_channel_init (x32, s.data, s.size) >= 0;
}

juce::StringArray WingRemote::applyWing (const Settings& s)
{
    if (! s.enabled)
    {
        stop();
        return {};
    }
    if (paths::externalActionsBlocked().load())
    {
        stop();
        return { "No network servers in this test run" };
    }

    // The servers run only the network protocols the console's profile names
    // (livedaw-wing limits them); a change of that set restarts them.
    const auto protocols = engine.getRemotes().network;
    auto sameServers = s;
    sameServers.name = running.name;
    if (isRunning() && sameServers == running && protocols == runningProtocols)
    {
        if (s.name != running.name)
            setName (s.name);
        running = s;
        return getStatus().errors;
    }

    const Utf8 name (s.name);
    const auto enable = (juce::uint32) ((s.discovery ? 1 : 0) | (s.osc ? 2 : 0) | (s.native ? 4 : 0));
    ld::ld_wing_start (handle, name.data, name.size, s.discoveryPort, s.oscPort, s.nativePort, enable);
    running = s;
    runningProtocols = protocols;
    return getStatus().errors;
}

void WingRemote::refreshProtocols()
{
    const auto network = engine.getRemotes().network;
    if (running.enabled && (network != runningProtocols || ((network & ahMidiBit) != 0 && (int) engine.getProfile() != ahMidiProfile)))
        apply (running);
}

void WingRemote::stop()
{
    ld::ld_wing_stop (handle);
    ld::ld_x32_stop (x32);
    ld::ld_ahmidi_stop (ahMidi);
    ahMidiProfile = -1;
    x32Name.clear();
    running.enabled = false;
}

bool WingRemote::isRunning() const
{
    return ld::ld_wing_running (handle);
}

void WingRemote::setName (const juce::String& newName)
{
    const Utf8 u (newName);
    ld::ld_wing_set_name (handle, u.data, u.size);
    running.name = newName;
}

void WingRemote::setConsoleModel (ConsoleModel model)
{
    ld::ld_wing_set_model (handle, (int32_t) model);
}

void WingRemote::setBlackFinish (bool black)
{
    ld::ld_wing_set_black_finish (handle, black);
}

WingRemote::Status WingRemote::getStatus() const
{
    Status st;
    st.running = isRunning();
    st.discoveryPort = ld::ld_wing_port (handle, 0);
    st.oscPort = ld::ld_wing_port (handle, 1);
    st.nativePort = ld::ld_wing_port (handle, 2);

    juce::StringArray lines;
    lines.addLines (takeString (ld::ld_wing_status (handle)));
    for (auto& line : lines)
    {
        juce::StringArray f;
        f.addTokens (line, "\t", {});
        if (f[0] == "error" && f.size() >= 2)
            st.errors.add (f[1]);
        else if (f[0] == "client" && f.size() >= 5)
            st.clients.push_back ({ f[1] == "native", f[2], f[3] == "1", f[4].getIntValue() });
        else if (f[0] == "traffic" && f.size() >= 3)
        {
            st.oscMessages = f[1].getLargeIntValue();
            st.nativeBytes = f[2].getLargeIntValue();
        }
    }
    return st;
}

//==============================================================================
juce::Result WingRemote::importSnapshot (const juce::File& file, juce::String& report)
{
    const Utf8 path (file.getFullPathName());
    ld::LdString* message = nullptr;
    const bool ok = ld::ld_wing_import_snap (handle, path.data, path.size, &message);
    report = takeString (message);
    engine.syncStrips(); // channels may have been added
    return ok ? juce::Result::ok() : juce::Result::fail (report);
}

juce::Result WingRemote::exportSnapshot (const juce::File& file)
{
    // The default folder (Documents) may not exist yet on a fresh system.
    if (auto dir = file.getParentDirectory().createDirectory(); dir.failed())
        return dir;
    const Utf8 path (file.getFullPathName());
    ld::LdString* message = nullptr;
    if (ld::ld_wing_export_snap (handle, path.data, path.size, &message))
        return juce::Result::ok();
    return juce::Result::fail (takeString (message));
}

juce::Result WingRemote::importShow (Show& show, const juce::File& file, juce::String& report)
{
    return show.importWingShow (handle, file, report);
}

//==============================================================================
void WingRemote::attachShow (Show* s, std::function<void (const juce::String&, const state::ApplyResult&)> recalled)
{
    libraryShow = s;
    onRecalled = std::move (recalled);
    if (libraryShow == nullptr)
    {
        stopTimer();
        ld::ld_wing_clear_library (handle);
        ld::ld_wing_clear_safes (handle);
        ld::ld_wing_clear_usb (handle);
        ld::ld_wing_clear_surface (handle);
        return;
    }
    publishLibrary();
    pumpSafes();
    pumpUsb();
    startTimerHz (10);
}

void WingRemote::timerCallback()
{
    if (isRunning())
        pump();
    pumpAhMidi();
}

void WingRemote::pumpAhMidi()
{
    if (libraryShow == nullptr || ! isAhMidiRunning())
        return;
    while (const int scene = ld::ld_ahmidi_take_scene (ahMidi))
    {
        if (scene <= libraryShow->getNumScenes())
        {
            auto result = libraryShow->recallScene (scene - 1);
            if (onRecalled)
                onRecalled ("scene " + juce::String (scene), result);
        }
    }
    // A scene recalled here, or by a client: its clients are told (not the one that asked).
    const int current = libraryShow->getCurrentSceneIndex();
    if (ahLastScene != current)
    {
        if (ahLastScene != -2 && current >= 0)
            ld::ld_ahmidi_scene_recalled (ahMidi, current + 1);
        ahLastScene = current;
    }
}

void WingRemote::pump()
{
    pumpHostSettings();
    pumpConsole();
    if (libraryShow == nullptr)
        return;
    pumpSafes();
    pumpUsb();
    int32_t action = 0, index = 0;
    while (ld::ld_wing_take_library_action (handle, &action, &index))
    {
        juce::String what;
        state::ApplyResult result;
        switch (action)
        {
            case 1:
                if (! juce::isPositiveAndBelow (index, libraryShow->getNumScenes()))
                    continue;
                what = "scene " + juce::String (index + 1);
                result = libraryShow->recallScene (index);
                break;
            case 2:
                what = "next scene";
                result = libraryShow->recallNext();
                break;
            case 3:
                what = "previous scene";
                result = libraryShow->recallPrevious();
                break;
            case 4:
                if (libraryShow->findSceneByTag (index) < 0)
                    continue;
                what = "scene #" + juce::String (index);
                result = libraryShow->recallSceneByTag (index);
                break;
            default: continue;
        }
        if (onRecalled)
            onRecalled (what + " (WING remote)", result);
    }
    publishLibrary();
    syncSurface();
}

void WingRemote::setSurfaceCallbacks (std::function<int()> ccBank, std::function<void (int, int)> changed)
{
    surfaceBank = std::move (ccBank);
    onSurfaceChanged = std::move (changed);
}

static_assert (WingRemote::surfaceLayers == (int) ld::kWingSurfaceLayers && WingRemote::surfaceControls == (int) ld::kWingSurfaceControls);

void WingRemote::syncSurface()
{
    if (libraryShow == nullptr)
        return;
    int bank = -1;
    const auto changes = libraryShow->syncWingSurface (handle, surfaceBank ? surfaceBank() : 0, bank);
    if ((changes != 0 || bank >= 0) && onSurfaceChanged)
        onSurfaceChanged ((int) changes, bank);
}

void WingRemote::setConsoleCallbacks (std::function<Strip*()> selected, std::function<void (Strip&)> select,
                                      std::function<void (juce::uint8*, juce::uint8*)> values)
{
    selectedStrip = std::move (selected);
    onSelect = std::move (select);
    midiValues = std::move (values);
    if (! selectedStrip)
        ld::ld_wing_set_selection (handle, { -1, 0 });
    if (! midiValues)
        ld::ld_wing_set_midi_values (handle, nullptr, nullptr);
    pumpConsole();
}

void WingRemote::pumpConsole()
{
    // A client's choice first, so that the selection published is the new one.
    ld::StripRefC chosen {};
    if (ld::ld_wing_take_selection (handle, &chosen) && onSelect)
        if (auto* strip = state::findStrip (engine, state::StripRef::fromC (chosen)))
            onSelect (*strip);
    if (selectedStrip)
    {
        auto* strip = selectedStrip();
        ld::ld_wing_set_selection (handle, strip != nullptr ? state::StripRef::of (*strip).toC() : ld::StripRefC { -1, 0 });
    }
    if (midiValues)
    {
        std::array<juce::uint8, 16 * 128> cc {}, notes {};
        midiValues (cc.data(), notes.data());
        ld::ld_wing_set_midi_values (handle, cc.data(), notes.data());
    }
}

void WingRemote::setHostSettings (juce::PropertiesFile* settings, std::function<void (const juce::StringArray&)> changed)
{
    hostSettings = settings;
    onHostSettingsChanged = std::move (changed);
    if (hostSettings == nullptr)
    {
        ld::ld_wing_clear_host_settings (handle);
        return;
    }
    hostSettingKeys = juce::StringArray::fromLines (takeString (ld::ld_wing_host_setting_keys()));
    hostSettingKeys.removeEmptyStrings();
    pumpHostSettings();
}

void WingRemote::pumpHostSettings()
{
    if (hostSettings == nullptr)
        return;
    juce::StringArray changed;
    ld::LdString* key = nullptr;
    ld::LdString* value = nullptr;
    while (ld::ld_wing_take_host_setting (handle, &key, &value))
    {
        const auto k = takeString (key);
        hostSettings->setValue (k, takeString (value));
        changed.addIfNotAlreadyThere (k);
    }
    if (! changed.isEmpty() && onHostSettingsChanged)
        onHostSettingsChanged (changed);
    // Keys the settings don't hold read Sidecar's defaults.
    for (const auto& k : hostSettingKeys)
        if (hostSettings->containsKey (k))
        {
            const Utf8 keyUtf8 (k), valueUtf8 (hostSettings->getValue (k));
            ld::ld_wing_set_host_setting (handle, keyUtf8.data, keyUtf8.size, valueUtf8.data, valueUtf8.size);
        }
}

juce::StringArray WingRemote::resetHostSettings()
{
    juce::StringArray removed;
    if (hostSettings == nullptr)
        return removed;
    for (const auto& k : hostSettingKeys)
        if (! k.startsWith ("midi.") && ! k.startsWith ("daw.") && hostSettings->containsKey (k))
        {
            hostSettings->removeValue (k);
            removed.add (k);
        }
    // Forget what was published (and changes not taken yet), then publish the rest again.
    ld::ld_wing_clear_host_settings (handle);
    pumpHostSettings();
    return removed;
}

void WingRemote::publishLibrary()
{
    const int current = libraryShow->getCurrentSceneIndex();
    const auto scene = libraryShow->getScene (current);
    const auto showName = libraryShow->getName();
    const auto sceneName = scene ? scene->name : juce::String();
    const Utf8 name (showName), sceneUtf8 (sceneName);
    ld::ld_wing_set_library (handle, name.data, name.size, libraryShow->getNumScenes(), current, sceneUtf8.data, sceneUtf8.size,
                             scene ? scene->tag : 0);
}

void WingRemote::pumpSafes()
{
    juce::uint32 dcas = 0, muteGroups = 0, fx = 0;
    auto safe = libraryShow->getGlobalSafe();
    if (ld::ld_wing_take_safes (handle, &dcas, &muteGroups, &fx))
    {
        // The WING knows DCAs 1-16 and mute groups 1-8: the safes beyond stay.
        const auto wing = [] (GroupBits set, int n, juce::uint32 mask) { return (set & ~GroupBits::first (n)) | (GroupBits::mask (mask) & GroupBits::first (n)); };
        safe.dcas = wing (safe.dcas, kWingDcas, dcas);
        safe.muteGroups = wing (safe.muteGroups, kWingMuteGroups, muteGroups);
        // The WING knows FX 1-16: the safes of the slots beyond stay.
        safe.fx = (safe.fx & ~state::Scope::allFx (kWingFx)) | (fx & state::Scope::allFx (kWingFx));
        libraryShow->setGlobalSafe (safe);
    }
    ld::ld_wing_set_safes (handle, (juce::uint32) (safe.dcas.low() & 0xffffu), (juce::uint32) (safe.muteGroups.low() & 0xffu),
                           (juce::uint32) (safe.fx & state::Scope::allFx (kWingFx)));
}

void WingRemote::pumpUsb()
{
    auto& usb = engine.getUsbRecorder();
    const auto storage = usb.getStorageFolder();
    // Clients name files relative to the USB folder ("/" separated); nothing outside it.
    auto inStorage = [&] (const juce::String& relative)
    {
        const auto f = storage.getChildFile (relative.trimCharactersAtStart ("/\\"));
        return usb.isInsideStorage (f) ? f : juce::File();
    };

    int32_t channels = 0, bits = 0;
    ld::LdString* recPath = nullptr;
    if (ld::ld_wing_take_usb_settings (handle, &channels, &bits, &recPath))
    {
        usb.setRecChannels (channels);
        usb.setRecBits (bits);
        if (const auto folder = inStorage (takeString (recPath)); folder != juce::File())
            usb.setRecPath (folder);
    }

    int32_t action = 0;
    ld::LdString* file = nullptr;
    while (ld::ld_wing_take_usb_action (handle, &action, &file))
    {
        const auto name = takeString (file);
        switch (action)
        {
            case 1: usb.record(); break;
            case 2: usb.newFile(); break;
            case 3:
                if (const auto f = inStorage (name); f.existsAsFile())
                    usb.playFile (f);
                break;
            default: break;
        }
    }

    const auto rec = usb.getRecPath();
    const auto relative = rec == storage || ! usb.isInsideStorage (rec) ? juce::String() : rec.getRelativePathFrom (storage).replaceCharacter ('\\', '/');
    const Utf8 storageUtf8 (storage.getFullPathName()), recUtf8 (relative);
    ld::ld_wing_set_usb (handle, storageUtf8.data, storageUtf8.size, recUtf8.data, recUtf8.size, usb.getRecChannels(), usb.getRecBits());
}

juce::String WingRemote::coverageReport()
{
    return takeString (ld::ld_wing_coverage());
}

} // namespace livedaw
