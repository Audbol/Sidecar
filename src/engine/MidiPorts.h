#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <functional>
#include <memory>
#include <vector>

namespace livedaw
{

/** MIDI ports, as any console's MIDI remote uses them (the WING's DIN and USB
    ports today; a Yamaha console's MIDI IN / OUT next): each port has an input
    and an output, a device of this computer chosen by name or a virtual port
    the app creates ("Sidecar MIDI DAW 1") where the platform allows it.
    Devices that disappear are reopened when they come back (hot-plug).

    The protocol (what the messages mean) is the console's: MidiRemote is the
    WING's. The input callbacks only queue the messages; onMessage gets them on
    the message thread. Message thread only (apart from the device callbacks). */
class MidiPorts final : private juce::AsyncUpdater, private juce::Timer
{
public:
    /** One port per name: the app's virtual port for it is "Sidecar MIDI <name>".
        `useDevices` false: no device is opened (tests). */
    MidiPorts (juce::StringArray virtualNames, bool useDevices = true);
    ~MidiPorts() override;

    int size() const noexcept { return (int) ports.size(); }

    /** The value of a port's device setting that means the app's virtual port. */
    static constexpr const char* virtualDevice = "<virtual>";
    /** The app's virtual ports' names start with this. */
    static constexpr const char* virtualPrefix = "Sidecar MIDI";

    /** The devices of this computer (names), without the app's own virtual ports. */
    static juce::StringArray getInputDevices();
    static juce::StringArray getOutputDevices();
    /** Whether this platform lets the app create virtual MIDI ports. */
    static bool canCreateVirtualPorts();
    /** The name of the app's virtual port for `port` ("Sidecar MIDI DAW 1"). */
    juce::String virtualPortName (int port) const;

    /** What a port should have open: the devices (names, virtualDevice, or empty
        for none) when the console's settings use the port. */
    struct Want
    {
        bool needed = false;
        juce::String input, output;
    };
    /** Opens and closes devices to match `wants` (one per port), and keeps them
        so until the next call (reopening devices that come back). */
    void open (std::vector<Want> wants);
    void closeAll();

    struct Status
    {
        bool inputOpen = false, outputOpen = false;
        bool inputMissing = false, outputMissing = false; // configured but not there
        juce::int64 received = 0, sent = 0;
    };
    Status getStatus (int port) const;

    /** Sends to the port's output device (if open) and counts it. */
    void send (int port, const juce::MidiMessage&);
    /** Counts a message received on `port` (the remote's handler does, so a
        message fed by a test counts too). */
    void noteReceived (int port) noexcept;

    /** A message that arrived on a port, on the message thread. */
    std::function<void (int port, const juce::MidiMessage&)> onMessage;

private:
    struct Device;

    /** Called by the device callbacks (MIDI threads): queues the message. */
    void enqueue (int port, const juce::MidiMessage&);
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void reopen();

    const juce::StringArray names;
    const bool useDevices;
    std::vector<std::unique_ptr<Device>> ports;
    std::vector<Want> wants;
    std::vector<Status> status;
    juce::MidiDeviceListConnection deviceListConnection;
    bool listening = false;

    // Device callbacks -> the message thread.
    juce::CriticalSection queueLock;
    std::vector<std::pair<int, juce::MidiMessage>> queue, draining;

    JUCE_DECLARE_NON_COPYABLE (MidiPorts)
};

} // namespace livedaw
