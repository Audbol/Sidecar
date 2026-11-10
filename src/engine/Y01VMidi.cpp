#include "engine/Y01VMidi.h"

#include "engine/MixerEngine.h"
#include "engine/Show.h"
#include "engine/Yamaha01V.h"


#include <vector>

namespace livedaw
{

Y01VMidi::Y01VMidi (MixerEngine& e, Show& s, bool useDevices)
    : show (s), console (e.getYamaha01V()), ports (juce::StringArray ("01V"), useDevices)
{
    lastScene = show.getCurrentSceneIndex(); // nothing is sent at start
    ports.onMessage = [this] (int port, const juce::MidiMessage& m) { handleMessage (port, m); };
    startTimer (50);
}

Y01VMidi::~Y01VMidi()
{
    stopTimer();
    ports.onMessage = nullptr;
    ports.closeAll();
}

void Y01VMidi::loadSettings (juce::PropertiesFile& p)
{
    settings = &p;
    console.setSetupText (p.getValue (setupKey));
    console.setPcText (p.getValue (pcKey));
    console.setCcText (p.getValue (ccKey));
    ports.open ({ { true, p.getValue (inputKey, MidiPorts::virtualDevice), p.getValue (outputKey, MidiPorts::virtualDevice) } });
}

juce::String Y01VMidi::getSetupText() const
{
    return console.getSetupText();
}

void Y01VMidi::setSetupText (const juce::String& text)
{
    console.setSetupText (text);
}

void Y01VMidi::handleMessage (int port, const juce::MidiMessage& m)
{
    ports.noteReceived (port);
    ld::ld_y01v_midi_in (console.getHandle(), m.getRawData(), (size_t) m.getRawDataSize());
    flush();
}

bool Y01VMidi::isProtected (int memory) const
{
    return settings != nullptr && juce::StringArray::fromTokens (settings->getValue (protectKey), " ", "").contains (juce::String (memory));
}

void Y01VMidi::flush()
{
    // What the protocol asks of the app: the show's scenes 1-99.
    auto* handle = console.getHandle();
    ld::LdY01VEvent ev {};
    std::vector<uint8_t> bytes ((size_t) livedaw::Yamaha01V::sceneBytes);
    while (ld::ld_y01v_event (handle, &ev, bytes.data(), bytes.size()))
    {
        const juce::String title (ev.title);
        if (ev.kind == 0 && ev.value >= 1 && ev.value <= show.getNumScenes())
        {
            show.recallScene (ev.value - 1);
            lastScene = show.getCurrentSceneIndex(); // not sent back
            ld::ld_y01v_take_values (handle);
        }
        else if (ev.kind == 1 && ev.value >= 1 && ! isProtected (ev.value))
        {
            // Store into scene n: the show grows to it if it must.
            while (show.getNumScenes() < ev.value)
                show.addScene ("SCENE " + juce::String (show.getNumScenes() + 1));
            show.storeScene (ev.value - 1);
        }
        else if (ev.kind == 2)
        {
            if (const auto scene = show.getY01VSceneBytes (handle, ev.value); ! scene.empty())
            {
                const auto name = show.getScene (ev.value - 1)->name;
                ld::ld_y01v_send_scene_dump (handle, ev.value, name.toRawUTF8(), name.getNumBytesAsUTF8(), scene.data(), scene.size());
            }
        }
        else if (ev.kind == 3 && ev.value >= 1 && ! isProtected (ev.value))
        {
            if (show.storeY01VSceneBytes (handle, ev.value, { bytes.begin(), bytes.begin() + ev.size }) && title.isNotEmpty())
                show.setSceneName (ev.value - 1, title);
        }
        else if (ev.kind == 4 && ev.value >= 1 && ev.value <= show.getNumScenes())
        {
            show.setSceneName (ev.value - 1, title);
        }
        else if (ev.kind == 5 && ev.value >= 1 && ev.value <= show.getNumScenes())
        {
            const auto name = show.getScene (ev.value - 1)->name;
            ld::ld_y01v_send_title (handle, ev.value, name.toRawUTF8(), name.getNumBytesAsUTF8());
        }
        else if (ev.kind == 6)
        {
            // Scene 00: the initial data, undoable like any other recall.
            show.markRecallUndo();
            ld::ld_y01v_recall_initial (handle);
        }
        // 7 (an effects program) and 8 (SEL and the function keys) would need
        // the surface: the effect library has no MIDI list yet, and the keys
        // select on its panel.
    }

    std::vector<uint8_t> out (4096);
    for (;;)
    {
        int n = ld::ld_y01v_midi_out (handle, out.data(), out.size());
        if (n < 0)
        {
            out.resize ((size_t) -n);
            n = ld::ld_y01v_midi_out (handle, out.data(), out.size());
        }
        if (n <= 0)
            break;
        const auto m = out[0] == 0xf0 ? juce::MidiMessage::createSysExMessage (out.data() + 1, n - 2)
                                      : juce::MidiMessage (out.data(), n);
        // TRANSMIT INTERVAL spaces the bulk blocks (p.233), and only them.
        if (console.getSetup (livedaw::Yamaha01V::Interval) > 0 && m.isSysEx() && (out[2] & 0xf0) == 0 && out[3] == 0x7e)
            paced.push_back (m);
        else
            send (m);
    }
    sendPaced();
}

void Y01VMidi::send (const juce::MidiMessage& m)
{
    if (onSend)
        onSend (m);
    ports.send (0, m);
}

void Y01VMidi::sendPaced()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (paced.empty() || now < nextBulkTime)
        return;
    send (paced.front());
    paced.pop_front();
    nextBulkTime = now + console.getSetup (livedaw::Yamaha01V::Interval) * 10;
}

void Y01VMidi::sendFeedback()
{
    // A recall changed the scene: its program change, and it isn't sent back
    // as a stream of parameter changes (scene_recalled takes the new values).
    const int scene = show.getCurrentSceneIndex();
    if (scene != lastScene)
    {
        if (scene >= 0 && scene < 99)
            ld::ld_y01v_scene_recalled (console.getHandle(), scene + 1);
        lastScene = scene;
    }
    ld::ld_y01v_midi_poll (console.getHandle());
    flush();
}

void Y01VMidi::resendAll()
{
    sendFeedback();
}

void Y01VMidi::transmit (char kind, int number)
{
    ld::ld_y01v_transmit (console.getHandle(), (int) kind, number);
    flush();
}

void Y01VMidi::request (char kind, int number)
{
    ld::ld_y01v_request (console.getHandle(), (int) kind, number);
    flush();
}

void Y01VMidi::initialSceneRecalled()
{
    ld::ld_y01v_scene_recalled (console.getHandle(), 0);
    flush();
}

} // namespace livedaw
