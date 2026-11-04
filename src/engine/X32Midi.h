#pragma once

#include "engine/FaderLaw.h"
#include "engine/MidiProtocol.h"
#include "engine/Parameters.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <functional>

namespace livedaw
{

class Strip;

/** The X32's MIDI protocol (MidiProtocol::X32): its fixed map, both ways
    (the X32's MIDI implementation, Sept 2017, as P.-G. Maillot's protocol
    document quotes it; Phase 4 step 6):

    - MIDI channel 1, CC 0-79: faders (95 = 0 dB, on the X32's fader law);
      channel 2, CC 0-79: mutes (127 muted), CC 80-85: mute groups 1-6;
      channel 3, CC 0-79: pans (64 centre). The CCs address Ch 1-32, Aux 1-8,
      FX returns 1-8, buses 1-16, matrices 1-6, main LR, M/C and DCA 1-8.
    - Channel 1 program change n: recall scene n + 1; a scene recalled on the
      console goes out as one. Channel 2 program change n: recall snippet
      n + 1 of the current scene (an X32's snippets are the show's; Sidecar's
      belong to a scene).
    - OSC over SysEx (step 6b): `F0 00 20 32 32 <text> F7`, the text an OSC
      command as an X32 takes it (`/ch/01/mix/on OFF`); an address alone is
      a query, answered by SysEx with its values.

    Each kind can be received and sent or not (SETUP > remote, as an X32's
    `/-prefs/remote/ioenable` bits): `enableKey`, the Enable bits. One MIDI
    port ("Sidecar MIDI X32" where virtual ports exist), its devices in the
    app settings (`x32midi.input`, `x32midi.output`). Console changes go back
    out (feedback); received values don't echo. Message thread only. */
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

    /** Sends a message of the assign section's controls (their MIDI codes),
        whatever the fixed map's Tx settings. */
    void sendUser (const juce::MidiMessage& m) { send (m); }

    /** Every outgoing message (tests, monitors). */
    std::function<void (const juce::MidiMessage&)> onSend;

    static constexpr int kNumStrips = 80; // CC 0-79
    static constexpr int kNumMuteGroups = 6;
    static constexpr const char* inputKey = "x32midi.input";
    static constexpr const char* outputKey = "x32midi.output";
    static constexpr const char* enableKey = "x32midi.enable";

    /** What is received (Rx) and sent (Tx): program changes (scenes,
        snippets), the fader, mute (and mute group) and pan CCs, OSC SysEx. */
    enum Enable
    {
        RxProgram = 1 << 0,
        TxProgram = 1 << 1,
        RxFader = 1 << 2,
        TxFader = 1 << 3,
        RxMute = 1 << 4,
        TxMute = 1 << 5,
        RxPan = 1 << 6,
        TxPan = 1 << 7,
        SysEx = 1 << 8,
        All = (1 << 9) - 1
    };
    int getEnabled() const noexcept { return enabled; }
    /** Sets the bits (the app settings keep them: loadSettings reads them). */
    void setEnabled (int bits) noexcept { enabled = bits & All; }

    /** A fader position (0..1) as a CC value, and back: 95 is 0 dB. */
    static int faderToCc (float position) noexcept;
    static float ccToFader (int value) noexcept;
    static int panToCc (float pan) noexcept;
    static float ccToPan (int value) noexcept;

private:
    void timerCallback() override { sendFeedback(); }
    void send (const juce::MidiMessage&);
    void handleSysEx (const juce::MidiMessage&);
    /** The Rx (or Tx) bit of MIDI channel `channel`'s CCs (1-3). */
    static int ccBit (int channel, bool tx) noexcept;

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
    int enabled = All;
    int lastScene = -1; // the current scene as last seen (a change goes out)
    MidiPorts ports;

    JUCE_DECLARE_NON_COPYABLE (X32Midi)
};

} // namespace livedaw
