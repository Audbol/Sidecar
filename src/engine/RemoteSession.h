#pragma once

#include "engine/Models.h"
#include "engine/WingRemote.h"

#include <juce_core/juce_core.h>

namespace livedaw
{

/** Remote control of a real console over the network: Sidecar's engine, its surface and the Universal
    Mixer drive the console's own mixer. One small interface so that another console (the 01V's MIDI,
    say) can follow the X32's; only the X32's exists so far. Message thread only; the network work runs
    on threads of its own (livedaw-x32's client), never on the audio thread. */
class RemoteSession
{
public:
    enum class State
    {
        Offline,
        Connecting,
        Connected,
        Lost
    };

    struct Info
    {
        State state = State::Offline;
        /** The console's name ("TEST-X32"), model and firmware as it reports them (empty before it answers). */
        juce::String name, model, firmware;
        /** Why it isn't connected (empty while it is, or before anything went wrong). */
        juce::String error;
        /** The read on connect: addresses answered of those asked. */
        int readDone = 0, readTotal = 0;
    };

    virtual ~RemoteSession() = default;

    /** "X32": the kind of console this session controls. */
    virtual juce::String consoleName() const = 0;
    /** The engine's console profile that has to be loaded for the session (ld::kProfile...). */
    virtual int requiredProfile() const = 0;
    /** Starts connecting to the console at `address` (an IP). The console is read first; its state wins
        and nothing is sent to it until that read is done. False for an address that can't be used. */
    virtual bool connect (const juce::String& address) = 0;
    /** Stops at once; nothing is sent afterwards. */
    virtual void disconnect() = 0;
    virtual Info info() const = 0;

    /** A session in progress (connecting, connected or lost): the console does the audio, not Sidecar. */
    bool isActive() const { return info().state != State::Offline; }
};

/** A real Behringer X32 (or M32) over OSC (livedaw-x32's client, WingRemote's handle). */
class X32RemoteSession final : public RemoteSession
{
public:
    explicit X32RemoteSession (WingRemote& w) : wing (w) {}

    juce::String consoleName() const override { return "X32"; }
    int requiredProfile() const override { return 2; } // ld::kProfileX32
    bool connect (const juce::String& address) override
    {
        const bool started = wing.connectX32Remote (address);
        models::setRemoteHeld (started); // the hardware runs its own models only
        return started;
    }

    void disconnect() override
    {
        wing.disconnectX32Remote();
        models::setRemoteHeld (false);
    }

    Info info() const override
    {
        Info i;
        i.state = (State) juce::jlimit (0, 3, wing.getX32RemoteState());
        models::setRemoteHeld (i.state != State::Offline);
        if (i.state == State::Offline)
            return i;
        const auto r = wing.getX32RemoteInfo();
        i.name = r.name;
        i.model = r.model;
        i.firmware = r.firmware;
        i.error = r.error;
        i.readDone = r.readDone;
        i.readTotal = r.readTotal;
        return i;
    }

private:
    WingRemote& wing;
};

} // namespace livedaw
