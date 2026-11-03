#pragma once

#include "engine/FaderLaw.h"
#include "engine/MidiProtocol.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <functional>

namespace livedaw
{

class Strip;
struct DcaParams;

/** The X32's MIDI protocol (MidiProtocol::X32): its fixed map, both ways
    (the X32's MIDI implementation, Sept 2017, as P.-G. Maillot's protocol
    document quotes it; Phase 4 step 6):

    - MIDI channel 1, CC 0-79: faders (95 = 0 dB, on the X32's fader law);
      channel 2, CC 0-79: mutes (127 muted), CC 80-85: mute groups 1-6;
      channel 3, CC 0-79: pans (64 centre). The CCs address Ch 1-32, Aux 1-8,
      FX returns 1-8, buses 1-16, matrices 1-6, main LR, M/C and DCA 1-8.
    - Channel 1 program change n: recall scene n + 1.

    One MIDI port ("Sidecar MIDI X32" where virtual ports exist), its devices
    in the app settings (`x32midi.input`, `x32midi.output`). Console changes
    go back out (feedback); received values don't echo. Message thread only. */
class X32Midi final : public MidiProtocol,
                      private juce::Timer
{
public:
    /** `useDevices` false: no MIDI devices are opened (tests read onSend). */
    X32Midi (MixerEngine&, Show&, bool useDevices = true);
    ~X32Midi() override;

    int getId() const noexcept override { return MidiProtocol::X32; }
    int getNumPorts() const noexcept override { return 1; }
    juce::String getPortName (int) const override { return "MIDI"; }
    MidiPorts::Status getPortStatus (int port) const override { return ports.getStatus (port); }

    void loadSettings (juce::PropertiesFile&) override;
    void handleMessage (int port, const juce::MidiMessage&) override;
    void sendFeedback() override;
    void resendAll() override;

    /** Every outgoing message (tests, monitors). */
    std::function<void (const juce::MidiMessage&)> onSend;

    static constexpr int kNumStrips = 80; // CC 0-79
    static constexpr int kNumMuteGroups = 6;
    static constexpr const char* inputKey = "x32midi.input";
    static constexpr const char* outputKey = "x32midi.output";

    /** A fader position (0..1) as a CC value, and back: 95 is 0 dB. */
    static int faderToCc (float position) noexcept;
    static float ccToFader (int value) noexcept;
    static int panToCc (float pan) noexcept;
    static float ccToPan (int value) noexcept;

private:
    void timerCallback() override { sendFeedback(); }
    void send (const juce::MidiMessage&);

    /** CC `cc` (0-79): the strip, or the DCA (72-79). */
    Strip* stripAt (int cc) const;
    DcaParams* dcaAt (int cc) const;

    /** The current value of `controller` on MIDI channel `channel` (1-3), -1 for none. */
    int currentValue (int channel, int controller) const;
    void applyValue (int channel, int controller, int value);

    MixerEngine& engine;
    Show& show;
    // What was last sent or received per MIDI channel (1-3) and CC (-1: unknown).
    std::array<std::array<int, 128>, 3> last {};
    // The first pass only takes the console's values (nothing is sent at start).
    bool primed = false;
    MidiPorts ports;

    JUCE_DECLARE_NON_COPYABLE (X32Midi)
};

} // namespace livedaw
