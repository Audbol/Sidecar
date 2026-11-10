#pragma once

#include "engine/MidiProtocol.h"
#include "livedaw_engine.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <functional>

namespace livedaw
{

/** The Yamaha DM2000's MIDI protocol (MidiProtocol::Dm2000), on one port: what its
    messages mean is livedaw-dm2000's, from Yamaha's V2 MIDI specification. Parameter
    changes and requests, the control change tables, program changes to scenes, Key
    Remote, remote meter data and MMC. Console changes go out when MIDI SETUP's Tx is
    on (Yamaha's initial setup has it off), received values don't echo, nothing is
    sent at start.

    MIDI SETUP (`dm2000midi.setup`: the Rx and Tx channels 1-16, then Tx of the six
    kinds (program change, control change, parameter change, bulk, MTC, others) as
    0 / 1, then Rx, OMNI and ECHO the same, NRPN and FADER LOW) and the program change table
    (`dm2000midi.pc`) are kept in the app settings, the devices in
    `dm2000midi.input` / `.output`. Message thread only. */
class Dm2000Midi final : public MidiProtocol,
                         private juce::Timer
{
public:
    /** `useDevices` false: no MIDI devices are opened (tests read onSend). */
    Dm2000Midi (MixerEngine&, Show&, bool useDevices = true);
    ~Dm2000Midi() override;

    int getId() const noexcept override { return MidiProtocol::Dm2000; }
    int getNumPorts() const noexcept override { return 1; }
    juce::String getPortName (int) const override { return "MIDI"; }
    MidiPorts::Status getPortStatus (int port) const override { return ports.getStatus (port); }

    void loadSettings (juce::PropertiesFile&) override;
    void handleMessage (int port, const juce::MidiMessage&) override;
    void sendFeedback() override;
    void resendAll() override;

    /** MIDI SETUP as a line of numbers, and the program change table. */
    juce::String getSetupText() const;
    void setSetupText (const juce::String&);
    juce::String getPcText() const;
    void setPcText (const juce::String&);
    /** Yamaha's initial setup and table, as those texts (what an empty setting means). */
    static juce::String initialSetupText();
    static juce::String initialPcText();
    /** An MMC command goes out (01 STOP, 02 PLAY, 04 FAST FORWARD, 05 REWIND, 06 RECORD STROBE). */
    void mmc (int command);

    /** Every outgoing message (tests, monitors). */
    std::function<void (const juce::MidiMessage&)> onSend;

    static constexpr const char* inputKey = "dm2000midi.input";
    static constexpr const char* outputKey = "dm2000midi.output";
    static constexpr const char* setupKey = "dm2000midi.setup";
    static constexpr const char* pcKey = "dm2000midi.pc";

private:
    void timerCallback() override { sendFeedback(); }
    void flush();

    Show& show;
    ld::LdDm2000* handle = nullptr;
    MidiPorts ports;
    int lastScene = -1; // the current scene as last seen (a change goes out)

    JUCE_DECLARE_NON_COPYABLE (Dm2000Midi)
};

} // namespace livedaw
