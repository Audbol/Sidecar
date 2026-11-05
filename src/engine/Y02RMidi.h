#pragma once

#include "engine/MidiProtocol.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <functional>

namespace livedaw
{

class Yamaha02R;

/** The Yamaha 02R's MIDI protocol (MidiProtocol::Yamaha02R; Phase 6 step 5):
    the console's only remote, on one port (its MIDI IN and OUT). What its
    messages mean is livedaw-02r's (the MixerEngine's Yamaha02R):

    - SysEx parameter changes (type 0, Sidecar's addresses), recall and store
      (type 24: scenes and the libraries), key remote (25), fader and encoder
      remote (26); parameter requests.
    - Control changes through its CC assign table (the 02R preset map).
    - Program changes recall scenes through its table (1-96 the scenes, 97
      scene 0); a scene recalled on the console sends its program change.
    - Yamaha bulk dumps and requests (scenes, the tables, the setup, the
      library programs; their bytes Sidecar's layout).

    MIDI SETUP (the channel; program change RX, TX, OMNI, ECHO; parameter
    change RX, TX; control change RX) is kept in the app settings
    (`y02rmidi.setup`), the devices in `y02rmidi.input` / `.output`. Console
    changes go out as parameter changes, received values don't echo, nothing
    is sent at start. Message thread only. */
class Y02RMidi final : public MidiProtocol,
                       private juce::Timer
{
public:
    /** `useDevices` false: no MIDI devices are opened (tests read onSend). */
    Y02RMidi (MixerEngine&, Show&, bool useDevices = true);
    ~Y02RMidi() override;

    int getId() const noexcept override { return MidiProtocol::Yamaha02R; }
    int getNumPorts() const noexcept override { return 1; }
    juce::String getPortName (int) const override { return "MIDI"; }
    MidiPorts::Status getPortStatus (int port) const override { return ports.getStatus (port); }

    void loadSettings (juce::PropertiesFile&) override;
    void handleMessage (int port, const juce::MidiMessage&) override;
    void sendFeedback() override;
    void resendAll() override;

    /** MIDI SETUP as a line of numbers (the settings' `y02rmidi.setup`). */
    juce::String getSetupText() const;
    void setSetupText (const juce::String&);

    /** Every outgoing message (tests, monitors). */
    std::function<void (const juce::MidiMessage&)> onSend;

    static constexpr const char* inputKey = "y02rmidi.input";
    static constexpr const char* outputKey = "y02rmidi.output";
    /** PROGRAM CHANGE ASSIGN's table (the console's `getPcText`). */
    static constexpr const char* ccKey = "y02rmidi.cc"; // CONTROL CHANGE ASSIGN's table
    static constexpr const char* pcKey = "y02rmidi.pc";
    static constexpr const char* setupKey = "y02rmidi.setup";

private:
    void timerCallback() override { sendFeedback(); }
    /** Sends what livedaw-02r queued, and does what it asks (scenes). */
    void flush();

    Show& show;
    livedaw::Yamaha02R& console;
    int lastScene = -1; // the current scene as last seen (a change goes out)
    MidiPorts ports;

    JUCE_DECLARE_NON_COPYABLE (Y02RMidi)
};

} // namespace livedaw
