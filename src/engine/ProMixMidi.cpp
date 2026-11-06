#include "engine/ProMixMidi.h"

#include "engine/MixerEngine.h"
#include "engine/ProMix.h"
#include "engine/Show.h"

#include <vector>

namespace livedaw
{

ProMixMidi::ProMixMidi (MixerEngine& e, Show& s, bool useDevices)
    : show (s), promix (e.getProMix()), ports (juce::StringArray ("ProMix"), useDevices)
{
    lastScene = show.getCurrentSceneIndex(); // nothing is sent at start
    ports.onMessage = [this] (int port, const juce::MidiMessage& m) { handleMessage (port, m); };
    startTimer (50);
}

ProMixMidi::~ProMixMidi()
{
    stopTimer();
    ports.onMessage = nullptr;
    ports.closeAll();
}

void ProMixMidi::loadSettings (juce::PropertiesFile& p)
{
    setSetupText (p.getValue (setupKey));
    promix.setPcText (p.getValue (pcKey));
    promix.setCcText (p.getValue (ccKey));
    ports.open ({ { true, p.getValue (inputKey, MidiPorts::virtualDevice), p.getValue (outputKey, MidiPorts::virtualDevice) } });
}

juce::String ProMixMidi::getSetupText() const
{
    return promix.getSetupText();
}

void ProMixMidi::setSetupText (const juce::String& text)
{
    promix.setSetupText (text);
}

void ProMixMidi::handleMessage (int port, const juce::MidiMessage& m)
{
    ports.noteReceived (port);
    ld::ld_promix_midi_in (promix.getHandle(), m.getRawData(), (size_t) m.getRawDataSize());
    flush();
}

void ProMixMidi::flush()
{
    // What the protocol asks of the app: the show's scenes 1-50 recalled, read
    // (a bulk request) and written (a bulk dump), neither recalling them.
    ld::LdProMixEvent ev {};
    std::vector<uint8_t> bytes (512);
    while (ld::ld_promix_event (promix.getHandle(), &ev, bytes.data(), bytes.size()))
    {
        if (ev.kind == 0 && ev.value >= 1 && ev.value <= show.getNumScenes())
        {
            show.recallScene (ev.value - 1);
            lastScene = show.getCurrentSceneIndex(); // not sent back
        }
        else if (ev.kind == 1)
        {
            if (const auto scene = show.getProMixSceneBytes (promix.getHandle(), ev.value); ! scene.empty())
                ld::ld_promix_send_scene_dump (promix.getHandle(), ev.value, scene.data(), scene.size());
        }
        else if (ev.kind == 2)
        {
            show.storeProMixSceneBytes (promix.getHandle(), ev.value, { bytes.begin(), bytes.begin() + ev.size });
        }
    }
    std::vector<uint8_t> out (4096);
    for (;;)
    {
        int n = ld::ld_promix_midi_out (promix.getHandle(), out.data(), out.size());
        if (n < 0)
        {
            out.resize ((size_t) -n);
            n = ld::ld_promix_midi_out (promix.getHandle(), out.data(), out.size());
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

void ProMixMidi::sendFeedback()
{
    ld::ld_promix_midi_poll (promix.getHandle());
    // A scene recalled on the console: its program change (through the table).
    const int scene = show.getCurrentSceneIndex();
    if (scene != lastScene)
    {
        if (scene >= 0 && scene < 50)
            ld::ld_promix_scene_recalled (promix.getHandle(), scene + 1);
        lastScene = scene;
    }
    flush();
}

void ProMixMidi::resendAll()
{
    ld::ld_promix_midi_resend (promix.getHandle());
    sendFeedback();
}

} // namespace livedaw
