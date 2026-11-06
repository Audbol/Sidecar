#pragma once

#include "engine/MidiProtocol.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <functional>

namespace livedaw
{

class ProMix;

/** The Yamaha ProMix 01's MIDI protocol (MidiProtocol::ProMix01; Phase 5
    step 5): the console's only remote, on one port (its MIDI IN and OUT).
    What its messages mean is livedaw-promix's (the MixerEngine's ProMix):

    - Control changes carry its 531 parameters on 16 banks of 96, in Channel
      mode (a bank per MIDI channel from the Tx / Rx channel) or Register mode
      (NRPN LSB, CC 98, names the bank); the effects' parameters in 14 bits.
    - Program changes recall scenes through its table (1-50 the scenes, 128
      scene 00); a scene recalled on the console sends its program change.
    - Yamaha bulk dumps and requests (the Edit Buffer, the scene memories 1-50
      as the show's scenes, read and written without a recall, the tables, the
      setup, the user programs; their bytes Sidecar's layout), Key Remote.

    MIDI SETUP (Tx / Rx channels, TX, RX, OMNI, ECHO per kind, the mode) is
    kept in the app settings (`promixmidi.setup`), the devices in
    `promixmidi.input` / `.output`. Console changes go out (feedback),
    received values don't echo, nothing is sent at start. Message thread only. */
class ProMixMidi final : public MidiProtocol,
                         private juce::Timer
{
public:
    /** `useDevices` false: no MIDI devices are opened (tests read onSend). */
    ProMixMidi (MixerEngine&, Show&, bool useDevices = true);
    ~ProMixMidi() override;

    int getId() const noexcept override { return MidiProtocol::ProMix01; }
    int getNumPorts() const noexcept override { return 1; }
    juce::String getPortName (int) const override { return "MIDI"; }
    MidiPorts::Status getPortStatus (int port) const override { return ports.getStatus (port); }

    void loadSettings (juce::PropertiesFile&) override;
    void handleMessage (int port, const juce::MidiMessage&) override;
    void sendFeedback() override;
    void resendAll() override;

    /** MIDI SETUP as a line of numbers (the settings' `promixmidi.setup`). */
    juce::String getSetupText() const;
    void setSetupText (const juce::String&);

    /** Every outgoing message (tests, monitors). */
    std::function<void (const juce::MidiMessage&)> onSend;

    static constexpr const char* inputKey = "promixmidi.input";
    static constexpr const char* outputKey = "promixmidi.output";
    /** PROGRAM CHANGE ASSIGN's table (the console's `getPcText`). */
    static constexpr const char* ccKey = "promixmidi.cc"; // CONTROL CHANGE ASSIGN's table
    static constexpr const char* pcKey = "promixmidi.pc";
    static constexpr const char* setupKey = "promixmidi.setup";

private:
    void timerCallback() override { sendFeedback(); }
    /** Sends what livedaw-promix queued, and does what it asks (scenes). */
    void flush();

    Show& show;
    ProMix& promix;
    int lastScene = -1; // the current scene as last seen (a change goes out)
    MidiPorts ports;

    JUCE_DECLARE_NON_COPYABLE (ProMixMidi)
};

} // namespace livedaw
