#include "engine/Y02RMidi.h"

#include "engine/MixerEngine.h"
#include "engine/Show.h"
#include "engine/Yamaha02R.h"

#include <vector>

namespace livedaw
{

Y02RMidi::Y02RMidi (MixerEngine& e, Show& s, bool useDevices)
    : show (s), console (e.getYamaha02R()), ports (juce::StringArray ("02R"), useDevices)
{
    lastScene = show.getCurrentSceneIndex(); // nothing is sent at start
    ports.onMessage = [this] (int port, const juce::MidiMessage& m) { handleMessage (port, m); };
    startTimer (50);
}

Y02RMidi::~Y02RMidi()
{
    stopTimer();
    ports.onMessage = nullptr;
    ports.closeAll();
}

void Y02RMidi::loadSettings (juce::PropertiesFile& p)
{
    setSetupText (p.getValue (setupKey));
    console.setPcText (p.getValue (pcKey));
    ports.open ({ { true, p.getValue (inputKey, MidiPorts::virtualDevice), p.getValue (outputKey, MidiPorts::virtualDevice) } });
}

juce::String Y02RMidi::getSetupText() const
{
    ld::LdY02RSetup s {};
    ld::ld_y02r_get_setup (console.getHandle(), &s);
    juce::StringArray v { juce::String (s.channel + 1) };
    for (auto x : s.switches)
        v.add (juce::String ((int) x));
    return v.joinIntoString (" ");
}

void Y02RMidi::setSetupText (const juce::String& text)
{
    juce::StringArray v;
    v.addTokens (text, " ", "");
    v.removeEmptyStrings();
    if (v.size() < 8)
        return; // the defaults: channel 1, everything on but OMNI and ECHO
    ld::LdY02RSetup s {};
    s.channel = juce::jlimit (0, 15, v[0].getIntValue() - 1);
    for (int k = 0; k < 7; ++k)
        s.switches[k] = (uint8_t) (v[1 + k].getIntValue() != 0);
    ld::ld_y02r_set_setup (console.getHandle(), &s);
}

void Y02RMidi::handleMessage (int port, const juce::MidiMessage& m)
{
    ports.noteReceived (port);
    ld::ld_y02r_midi_in (console.getHandle(), m.getRawData(), (size_t) m.getRawDataSize());
    flush();
}

void Y02RMidi::flush()
{
    // What the protocol asks of the app: the show's scenes 1-96 (scene 0 is
    // the protocol's own).
    ld::LdY02REvent ev {};
    std::vector<uint8_t> bytes ((size_t) livedaw::Yamaha02R::size);
    while (ld::ld_y02r_event (console.getHandle(), &ev, bytes.data(), bytes.size()))
    {
        if (ev.kind == 0 && ev.value >= 1 && ev.value <= show.getNumScenes())
        {
            show.recallScene (ev.value - 1);
            lastScene = show.getCurrentSceneIndex(); // not sent back
        }
        else if (ev.kind == 1 && ev.value >= 1)
        {
            // Store into scene n: the show grows to it if it must.
            while (show.getNumScenes() < ev.value)
                show.addScene ("SCENE " + juce::String (show.getNumScenes() + 1));
            show.storeScene (ev.value - 1);
        }
        else if (ev.kind == 3 && ev.value >= 1)
        {
            // A scene's dump: stored into scene n, the console as it was after.
            const auto now = console.getSceneBytes();
            console.loadSceneBytes (std::vector<uint8_t> (bytes.begin(), bytes.begin() + ev.size));
            while (show.getNumScenes() < ev.value)
                show.addScene ("SCENE " + juce::String (show.getNumScenes() + 1));
            show.storeScene (ev.value - 1);
            if (juce::String (ev.title).isNotEmpty())
                show.setSceneName (ev.value - 1, juce::String (ev.title));
            console.loadSceneBytes (now);
        }
        // 2 (a scene's dump requested) needs the scene's mix without recalling
        // it; 4 and 5 (key remote, encoders) belong to the surface.
    }
    std::vector<uint8_t> out (4096);
    for (;;)
    {
        int n = ld::ld_y02r_midi_out (console.getHandle(), out.data(), out.size());
        if (n < 0)
        {
            out.resize ((size_t) -n);
            n = ld::ld_y02r_midi_out (console.getHandle(), out.data(), out.size());
        }
        if (n <= 0)
            break;
        const auto m = out[0] == 0xf0 ? juce::MidiMessage::createSysExMessage (out.data() + 1, n - 2)
                                      : juce::MidiMessage (out.data(), n);
        if (onSend)
            onSend (m);
        ports.send (0, m);
    }
}

void Y02RMidi::sendFeedback()
{
    ld::ld_y02r_midi_poll (console.getHandle());
    // A scene recalled on the console: its program change (through the table).
    const int scene = show.getCurrentSceneIndex();
    if (scene != lastScene)
    {
        if (scene >= 0 && scene < 96)
            ld::ld_y02r_scene_recalled (console.getHandle(), scene + 1);
        lastScene = scene;
    }
    flush();
}

void Y02RMidi::resendAll()
{
    sendFeedback();
}

} // namespace livedaw
