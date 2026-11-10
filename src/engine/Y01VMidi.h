#pragma once

#include "engine/MidiProtocol.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <deque>
#include <functional>

namespace livedaw
{

class Yamaha01V;

/** The Yamaha 01V's MIDI protocol (MidiProtocol::Yamaha01V): the console's only
    remote, on one port (its MIDI IN and OUT). What its messages mean is
    livedaw-01v's (the MixerEngine's Yamaha01V):

    - Program changes recall scenes through its table (MIDI page 2); a scene
      recalled on the console sends its program change.
    - Control changes through its assign table (MIDI page 3, with its three
      arrangements), both ways.
    - SysEx parameter changes (bytes, 7-bit, bits; the setup; scene recall and
      store, titles, effect recall; the key remote), parameter requests.
    - Yamaha bulk dumps and requests: scene memories (the show's scenes, read and
      written without a recall; protected ones aren't written), the setup, both
      tables, a channel's current EQ and dynamics, an effect's current settings.

    MIDI SETUP is kept in the app settings (`y01vmidi.setup`), the devices in
    `y01vmidi.input` / `.output`, the tables in `y01vmidi.pc` / `.cc`, the
    settings' MEMORY protection in `y01vProtectedMemories`. Console changes go out
    as control changes or parameter changes (see Setup's Tx switches), received
    values don't echo, nothing is sent at start. Message thread only. */
class Y01VMidi final : public MidiProtocol,
                       private juce::Timer
{
public:
    /** `useDevices` false: no MIDI devices are opened (tests read onSend). */
    Y01VMidi (MixerEngine&, Show&, bool useDevices = true);
    ~Y01VMidi() override;

    int getId() const noexcept override { return MidiProtocol::Yamaha01V; }
    int getNumPorts() const noexcept override { return 1; }
    juce::String getPortName (int) const override { return "MIDI"; }
    MidiPorts::Status getPortStatus (int port) const override { return ports.getStatus (port); }

    void loadSettings (juce::PropertiesFile&) override;
    void handleMessage (int port, const juce::MidiMessage&) override;
    void sendFeedback() override;
    void resendAll() override;

    /** MIDI SETUP, as a line of numbers. */
    juce::String getSetupText() const;
    void setSetupText (const juce::String&);

    /** MIDI page 4: TRANSMIT `kind` ('M', 'S', 'P', 'C'), `number` a scene memory (127 the edit buffer). */
    void transmit (char kind, int number = 0);
    /** MIDI page 4: REQUEST the same from the other device. */
    void request (char kind, int number = 0);
    /** Scene 00 was recalled on the surface: its program change goes out. */
    void initialSceneRecalled();

    /** Every outgoing message (tests, monitors). */
    std::function<void (const juce::MidiMessage&)> onSend;

    static constexpr const char* inputKey = "y01vmidi.input";
    static constexpr const char* outputKey = "y01vmidi.output";
    static constexpr const char* pcKey = "y01vmidi.pc";
    static constexpr const char* ccKey = "y01vmidi.cc";
    static constexpr const char* setupKey = "y01vmidi.setup";
    /** The scene memories MEMORY page 1 protects (bulk dumps can't write them, p.189). */
    static constexpr const char* protectKey = "y01vProtectedMemories";

private:
    void timerCallback() override { sendFeedback(); }
    /** Sends what livedaw-01v queued, and does what it asks (scenes). */
    void flush();
    void send (const juce::MidiMessage&);
    void sendPaced();
    bool isProtected (int memory) const;

    Show& show;
    livedaw::Yamaha01V& console;
    int lastScene = -1; // the current scene as last seen (a change goes out)
    MidiPorts ports;
    juce::PropertiesFile* settings = nullptr;
    /** Bulk blocks waiting for TRANSMIT INTERVAL (p.233). */
    std::deque<juce::MidiMessage> paced;
    double nextBulkTime = 0.0;

    JUCE_DECLARE_NON_COPYABLE (Y01VMidi)
};

} // namespace livedaw
