#pragma once

#include "engine/MidiProtocol.h"
#include "livedaw_engine.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <functional>

namespace livedaw
{

/** The Allen & Heath SQ-5's MIDI protocol (MidiProtocol::AhSq), on one port: what
    its messages mean is livedaw-sq's. NRPN mutes, levels, pans and assignments,
    scenes by bank and program change; console changes go out as NRPNs, received
    values don't echo, nothing is sent at start. The MIDI channel and NRPN fader
    law are in the app settings (`sqmidi.setup`: the channel 1-16 and 0 / 1 for
    Audio Taper), the devices in `sqmidi.input` / `.output`. Message thread only. */
class SqMidi final : public MidiProtocol,
                     private juce::Timer
{
public:
    /** `useDevices` false: no MIDI devices are opened (tests read onSend). */
    SqMidi (MixerEngine&, Show&, bool useDevices = true);
    ~SqMidi() override;

    int getId() const noexcept override { return MidiProtocol::AhSq; }
    int getNumPorts() const noexcept override { return 1; }
    juce::String getPortName (int) const override { return "MIDI"; }
    MidiPorts::Status getPortStatus (int port) const override { return ports.getStatus (port); }

    void loadSettings (juce::PropertiesFile&) override;
    void handleMessage (int port, const juce::MidiMessage&) override;
    void sendFeedback() override;
    void resendAll() override;

    /** The channel 1-16 and the fader law, as a line of numbers. */
    void setSetupText (const juce::String&);

    /** Every outgoing message (tests, monitors). */
    std::function<void (const juce::MidiMessage&)> onSend;

    static constexpr const char* inputKey = "sqmidi.input";
    static constexpr const char* outputKey = "sqmidi.output";
    static constexpr const char* setupKey = "sqmidi.setup";

private:
    void timerCallback() override { sendFeedback(); }
    void flush();

    Show& show;
    ld::LdSq* handle = nullptr;
    MidiPorts ports;
    int lastScene = -1; // the current scene as last seen (a change goes out)

    JUCE_DECLARE_NON_COPYABLE (SqMidi)
};

} // namespace livedaw
