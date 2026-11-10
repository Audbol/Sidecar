#include "engine/SqMidi.h"

#include "engine/MixerEngine.h"
#include "engine/Show.h"

#include <array>
#include <cstdlib>

namespace livedaw
{

namespace
{
    SqMidi* currentMidi = nullptr; // message thread
} // namespace

std::function<void (int, bool)> SqMidi::onSoftKey;
SqMidi* SqMidi::current() noexcept { return currentMidi; }

SqMidi::SqMidi (MixerEngine& e, Show& s, bool useDevices)
    : show (s), handle (ld::ld_sq_create (e.getHandle())), ports (juce::StringArray ("SQ-5"), useDevices)
{
    lastScene = show.getCurrentSceneIndex(); // nothing is sent at start
    currentMidi = this;
    ports.onMessage = [this] (int port, const juce::MidiMessage& m) { handleMessage (port, m); };
    startTimer (50);
}

SqMidi::~SqMidi()
{
    stopTimer();
    if (currentMidi == this)
        currentMidi = nullptr;
    ports.onMessage = nullptr;
    ports.closeAll();
    ld::ld_sq_destroy (handle);
}

void SqMidi::loadSettings (juce::PropertiesFile& p)
{
    setSetupText (p.getValue (setupKey));
    setGroups (p.getIntValue ("sq5.groups", 0));
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

void SqMidi::setGroups (int n)
{
    ld::ld_sq_set_groups (handle, n);
}

void SqMidi::sendMessage (const juce::MidiMessage& m)
{
    if (onSend)
        onSend (m);
    ports.send (0, m);
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
    // A SoftKey by its note (the protocol's 3.2): held and released as a press would.
    while (const int key = ld::ld_sq_soft_key (handle))
        if (onSoftKey)
            onSoftKey (std::abs (key) - 1, key > 0);
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
