#include "engine/MidiPorts.h"

namespace livedaw
{

//==============================================================================
/** A port's open devices. The input's callback only queues the messages. */
struct MidiPorts::Device final : private juce::MidiInputCallback
{
    Device (MidiPorts& o, int p) : owner (o), port (p) {}
    ~Device() override { closeInput(); }

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override { owner.enqueue (port, m); }

    bool openInput (const juce::String& name)
    {
        closeInput();
        if (name == virtualDevice)
            input = juce::MidiInput::createNewDevice (owner.virtualPortName (port), this);
        else
            for (auto& d : juce::MidiInput::getAvailableDevices())
                if (d.name == name)
                {
                    input = juce::MidiInput::openDevice (d.identifier, this);
                    break;
                }
        if (input == nullptr)
            return false;
        input->start();
        inputName = name;
        return true;
    }

    bool openOutput (const juce::String& name)
    {
        output.reset();
        if (name == virtualDevice)
            output = juce::MidiOutput::createNewDevice (owner.virtualPortName (port));
        else
            for (auto& d : juce::MidiOutput::getAvailableDevices())
                if (d.name == name)
                {
                    output = juce::MidiOutput::openDevice (d.identifier);
                    break;
                }
        if (output != nullptr)
            outputName = name;
        return output != nullptr;
    }

    void closeInput()
    {
        if (input != nullptr)
            input->stop(); // no callback runs after this
        input.reset();
        inputName = {};
    }

    void closeOutput()
    {
        output.reset();
        outputName = {};
    }

    MidiPorts& owner;
    const int port;
    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;
    juce::String inputName, outputName;
};

//==============================================================================
MidiPorts::MidiPorts (juce::StringArray virtualNames, bool devicesOn)
    : names (std::move (virtualNames)), useDevices (devicesOn), wants ((size_t) names.size()), status ((size_t) names.size())
{
    for (int p = 0; p < names.size(); ++p)
        ports.push_back (std::make_unique<Device> (*this, p));
    // Devices that went away: try again every two seconds (hot-plug).
    startTimer (2000);
}

MidiPorts::~MidiPorts()
{
    stopTimer();
    deviceListConnection.reset();
    closeAll();
    cancelPendingUpdate();
}

juce::String MidiPorts::virtualPortName (int port) const
{
    return juce::String (virtualPrefix) + " " + names[juce::jlimit (0, names.size() - 1, port)];
}

juce::StringArray MidiPorts::getInputDevices()
{
    juce::StringArray result;
    for (auto& d : juce::MidiInput::getAvailableDevices())
        if (! d.name.startsWith (virtualPrefix))
            result.addIfNotAlreadyThere (d.name);
    return result;
}

juce::StringArray MidiPorts::getOutputDevices()
{
    juce::StringArray result;
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        if (! d.name.startsWith (virtualPrefix))
            result.addIfNotAlreadyThere (d.name);
    return result;
}

bool MidiPorts::canCreateVirtualPorts()
{
   #if JUCE_LINUX || JUCE_MAC || JUCE_BSD
    return true;
   #else
    return false;
   #endif
}

MidiPorts::Status MidiPorts::getStatus (int port) const
{
    return juce::isPositiveAndBelow (port, size()) ? status[(size_t) port] : Status {};
}

void MidiPorts::open (std::vector<Want> w)
{
    w.resize (ports.size());
    wants = std::move (w);
    reopen();
}

void MidiPorts::reopen()
{
    if (! useDevices)
        return;
    if (! listening)
    {
        // Hot-plug: reopen the configured devices when the device list changes.
        deviceListConnection = juce::MidiDeviceListConnection::make ([this] { reopen(); });
        listening = true;
    }

    for (size_t p = 0; p < ports.size(); ++p)
    {
        auto& d = *ports[p];
        auto& st = status[p];
        const auto& want = wants[p];
        if (! want.needed || want.input.isEmpty() || d.inputName != want.input)
            d.closeInput();
        if (! want.needed || want.output.isEmpty() || d.outputName != want.output)
            d.closeOutput();
        if (want.needed && want.input.isNotEmpty() && d.input == nullptr)
            d.openInput (want.input);
        if (want.needed && want.output.isNotEmpty() && d.output == nullptr)
            d.openOutput (want.output);
        st.inputOpen = d.input != nullptr;
        st.outputOpen = d.output != nullptr;
        st.inputMissing = want.needed && want.input.isNotEmpty() && ! st.inputOpen;
        st.outputMissing = want.needed && want.output.isNotEmpty() && ! st.outputOpen;
    }
}

void MidiPorts::closeAll()
{
    for (auto& d : ports)
    {
        d->closeInput();
        d->closeOutput();
    }
}

void MidiPorts::send (int port, const juce::MidiMessage& m)
{
    if (! juce::isPositiveAndBelow (port, size()))
        return;
    ++status[(size_t) port].sent;
    if (auto* out = ports[(size_t) port]->output.get())
        out->sendMessageNow (m);
}

void MidiPorts::noteReceived (int port) noexcept
{
    if (juce::isPositiveAndBelow (port, size()))
        ++status[(size_t) port].received;
}

void MidiPorts::enqueue (int port, const juce::MidiMessage& m)
{
    {
        const juce::ScopedLock sl (queueLock);
        queue.emplace_back (port, m);
    }
    triggerAsyncUpdate();
}

void MidiPorts::handleAsyncUpdate()
{
    {
        const juce::ScopedLock sl (queueLock);
        draining.swap (queue);
    }
    for (auto& [port, message] : draining)
        if (onMessage)
            onMessage (port, message);
    draining.clear();
}

void MidiPorts::timerCallback()
{
    for (auto& st : status)
        if (st.inputMissing || st.outputMissing)
        {
            reopen();
            break;
        }
}

} // namespace livedaw
