#include "engine/SqMidi.h"

#include "engine/MixerEngine.h"
#include "engine/Show.h"

#include <array>

namespace livedaw
{

SqMidi::SqMidi (MixerEngine& e, Show& s, bool useDevices)
    : show (s), handle (ld::ld_sq_create (e.getHandle())), ports (juce::StringArray ("SQ-5"), useDevices)
{
    lastScene = show.getCurrentSceneIndex(); // nothing is sent at start
    ports.onMessage = [this] (int port, const juce::MidiMessage& m) { handleMessage (port, m); };
    startTimer (50);
}

SqMidi::~SqMidi()
{
    stopTimer();
    ports.onMessage = nullptr;
    ports.closeAll();
    ld::ld_sq_destroy (handle);
}

void SqMidi::loadSettings (juce::PropertiesFile& p)
{
    setSetupText (p.getValue (setupKey));
    ports.open ({ { true, p.getValue (inputKey, MidiPorts::virtualDevice), p.getValue (outputKey, MidiPorts::virtualDevice) } });
}

void SqMidi::setSetupText (const juce::String& text)
{
    juce::StringArray v;
    v.addTokens (text, " ", "");
    v.removeEmptyStrings();
    if (v.size() < 2)
        return; // the defaults: channel 1, Linear Taper
    ld::ld_sq_set_setup (handle, juce::jlimit (0, 15, v[0].getIntValue() - 1), v[1].getIntValue() != 0);
}

void SqMidi::handleMessage (int port, const juce::MidiMessage& m)
{
    ports.noteReceived (port);
    ld::ld_sq_midi_in (handle, m.getRawData(), (size_t) m.getRawDataSize());
    flush();
}

void SqMidi::flush()
{
    // Scenes the MIDI asks for: the show's, numbered from 1.
    while (const int scene = ld::ld_sq_scene_request (handle))
    {
        if (scene <= show.getNumScenes())
        {
            show.recallScene (scene - 1);
            lastScene = show.getCurrentSceneIndex(); // not sent back
        }
    }
    std::array<uint8_t, 3> out {};
    while (const int n = ld::ld_sq_midi_out (handle, out.data(), out.size()))
    {
        const juce::MidiMessage m (out.data(), n);
        if (onSend)
            onSend (m);
        ports.send (0, m);
    }
}

void SqMidi::sendFeedback()
{
    ld::ld_sq_midi_poll (handle);
    // A scene recalled on the console: its bank and program change.
    const int scene = show.getCurrentSceneIndex();
    if (scene != lastScene)
    {
        if (scene >= 0 && scene < 300)
            ld::ld_sq_scene_recalled (handle, scene + 1);
        lastScene = scene;
    }
    flush();
}

void SqMidi::resendAll()
{
    ld::ld_sq_midi_resend (handle);
    sendFeedback();
}

} // namespace livedaw
