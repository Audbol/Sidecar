#include "engine/Dm2000Midi.h"

#include "engine/MixerEngine.h"
#include "engine/Show.h"

#include <array>
#include <vector>

namespace livedaw
{

namespace
{
constexpr int setupBytes = 28; // livedaw_dm2000::SETUP_BYTES
}

Dm2000Midi::Dm2000Midi (MixerEngine& e, Show& s, bool useDevices)
    : show (s), handle (ld::ld_dm2000_create (e.getHandle())), ports (juce::StringArray ("DM2000"), useDevices)
{
    lastScene = show.getCurrentSceneIndex(); // nothing is sent at start
    ports.onMessage = [this] (int port, const juce::MidiMessage& m) { handleMessage (port, m); };
    startTimer (50);
}

Dm2000Midi::~Dm2000Midi()
{
    stopTimer();
    ports.onMessage = nullptr;
    ports.closeAll();
    ld::ld_dm2000_destroy (handle);
}

void Dm2000Midi::loadSettings (juce::PropertiesFile& p)
{
    setSetupText (p.getValue (setupKey));
    setPcText (p.getValue (pcKey));
    ports.open ({ { true, p.getValue (inputKey, MidiPorts::virtualDevice), p.getValue (outputKey, MidiPorts::virtualDevice) } });
}

juce::String Dm2000Midi::getSetupText() const
{
    std::array<uint8_t, setupBytes> b {};
    ld::ld_dm2000_get_setup (handle, b.data());
    juce::StringArray v { juce::String ((int) b[0] + 1), juce::String ((int) b[1] + 1) };
    for (size_t i = 2; i < b.size(); ++i)
        v.add (juce::String ((int) b[i]));
    return v.joinIntoString (" ");
}

void Dm2000Midi::setSetupText (const juce::String& text)
{
    juce::StringArray v;
    v.addTokens (text, " ", "");
    v.removeEmptyStrings();
    if (v.size() < setupBytes)
        return; // the defaults: Yamaha's initial setup
    std::array<uint8_t, setupBytes> b {};
    b[0] = (uint8_t) juce::jlimit (0, 15, v[0].getIntValue() - 1);
    b[1] = (uint8_t) juce::jlimit (0, 15, v[1].getIntValue() - 1);
    for (int i = 2; i < setupBytes; ++i)
        b[(size_t) i] = (uint8_t) (v[i].getIntValue() != 0);
    ld::ld_dm2000_set_setup (handle, b.data());
}

juce::String Dm2000Midi::getPcText() const
{
    std::array<uint8_t, 128> t {};
    ld::ld_dm2000_get_pc_table (handle, t.data());
    juce::StringArray v;
    for (auto x : t)
        v.add (juce::String ((int) x));
    return v.joinIntoString (" ");
}

void Dm2000Midi::setPcText (const juce::String& text)
{
    juce::StringArray v;
    v.addTokens (text, " ", "");
    v.removeEmptyStrings();
    if (v.size() != 128)
        return; // the initial table
    std::array<uint8_t, 128> t {};
    for (int i = 0; i < 128; ++i)
        t[(size_t) i] = (uint8_t) juce::jlimit (0, 100, v[i].getIntValue());
    ld::ld_dm2000_set_pc_table (handle, t.data());
}

juce::String Dm2000Midi::initialSetupText()
{
    // livedaw_dm2000::Setup::default (Owner's Manual p.217's picture): channel 1 both ways, nothing
    // transmitted, program changes, parameter changes and bulk received.
    return "1 1  0 0 0 0 0 0  1 0 1 1 0 0  0 0 0 0 0 0  0 0 0 0 0 0  0 0";
}

juce::String Dm2000Midi::initialPcText()
{
    // Program changes 1-99 recall scenes 1-99, 100 scene 0, the rest nothing (p.352).
    juce::StringArray v;
    for (int i = 0; i < 128; ++i)
        v.add (juce::String (i < 99 ? i + 1 : i == 99 ? 0 : 100));
    return v.joinIntoString (" ");
}

void Dm2000Midi::mmc (int command)
{
    ld::ld_dm2000_mmc (handle, (uint8_t) juce::jlimit (0, 127, command));
    flush();
}

void Dm2000Midi::handleMessage (int port, const juce::MidiMessage& m)
{
    ports.noteReceived (port);
    ld::ld_dm2000_midi_in (handle, m.getRawData(), (size_t) m.getRawDataSize());
    flush();
}

void Dm2000Midi::flush()
{
    // What the MIDI asks of the app: the show's scenes 1-99 (scene 0 is the
    // console's read-only initial data; a Key Remote key belongs to the surface).
    int kind = 0, value = 0;
    while (ld::ld_dm2000_event (handle, &kind, &value))
    {
        if (kind == 0 && value >= 1 && value <= show.getNumScenes())
        {
            show.recallScene (value - 1);
            lastScene = show.getCurrentSceneIndex(); // not sent back
        }
        else if (kind == 1 && value >= 1)
        {
            while (show.getNumScenes() < value)
                show.addScene ("SCENE " + juce::String (show.getNumScenes() + 1));
            show.storeScene (value - 1);
        }
    }
    std::vector<uint8_t> out (2048);
    for (;;)
    {
        int n = ld::ld_dm2000_midi_out (handle, out.data(), out.size());
        if (n < 0)
        {
            out.resize ((size_t) -n);
            n = ld::ld_dm2000_midi_out (handle, out.data(), out.size());
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

void Dm2000Midi::sendFeedback()
{
    ld::ld_dm2000_midi_poll (handle);
    // A scene recalled on the console: its program change (through the table).
    const int scene = show.getCurrentSceneIndex();
    if (scene != lastScene)
    {
        if (scene >= 0 && scene < 99)
            ld::ld_dm2000_scene_recalled (handle, scene + 1);
        lastScene = scene;
    }
    flush();
}

void Dm2000Midi::resendAll()
{
    ld::ld_dm2000_midi_resend (handle);
    sendFeedback();
}

} // namespace livedaw
