#pragma once

#include "engine/MidiPorts.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <memory>

namespace livedaw
{

class MidiRemote;
class MixerEngine;
class Show;

/** A console's MIDI protocol: what its MIDI messages mean, over the
    console-neutral MidiPorts (Phase 3 step 6). The console's profile names it
    (ld_engine_remotes); the app makes it with create() and remakes it when the
    console's protocol changes. The WING's is MidiRemote, the X32's X32Midi,
    the ProMix 01's ProMixMidi.

    This is what the app drives whatever the console; a protocol's own
    settings and pages are its own (the WING's: asWing()). Message thread only. */
class MidiProtocol
{
public:
    /** The protocols, as ld_engine_remotes numbers them (RemotesC::midi). */
    enum Id
    {
        None = -1,
        Wing = 0,
        X32 = 1,
        ProMix01 = 2
    };

    virtual ~MidiProtocol() = default;

    /** The console's protocol `id` (nullptr for None or one this build lacks).
        `useDevices` false: no MIDI devices are opened (tests). */
    static std::unique_ptr<MidiProtocol> create (int id, MixerEngine&, Show&, bool useDevices = true);

    virtual int getId() const noexcept = 0;

    /** Opens its devices from the app settings (the WING's: midiui::connect). */
    virtual void loadSettings (juce::PropertiesFile&) {}

    /** The console's MIDI ports ("DIN", "USB", "DAW 1"...). */
    virtual int getNumPorts() const noexcept = 0;
    virtual juce::String getPortName (int port) const = 0;
    virtual MidiPorts::Status getPortStatus (int port) const = 0;

    /** Handles a message that arrived on `port` (the ports queue theirs here;
        tests call it directly). */
    virtual void handleMessage (int port, const juce::MidiMessage&) = 0;
    /** Sends the console changes since the last call (called by a timer). */
    virtual void sendFeedback() = 0;
    /** Forgets what was sent, so the next sendFeedback() sends every value. */
    virtual void resendAll() = 0;

    /** The WING's protocol (its SETUP > REMOTE and DAW pages, CC EDIT and the
        LIBRARY's SEND MIDI use it), or nullptr. */
    virtual MidiRemote* asWing() noexcept { return nullptr; }
};

} // namespace livedaw
