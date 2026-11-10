#pragma once

#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

namespace livedaw::ui
{

/** The CONNECT key and the status words a surface's top bar and the Universal Mixer's header share
    for remote control of a real console (engine/RemoteSession.h): OFFLINE, CONNECTING, CONNECTED, LOST. */
namespace remotebar
{
    using State = RemoteSession::State;

    inline RemoteSession::Info infoOf (const UiContext& c)
    {
        return c.remoteInfo ? c.remoteInfo() : RemoteSession::Info();
    }

    inline juce::String word (State s)
    {
        return s == State::Connecting ? "CONNECTING" : s == State::Connected ? "CONNECTED" : s == State::Lost ? "LOST" : "OFFLINE";
    }

    /** "X32 TEST-X32  CONNECTED": what the status line says (the console's own name when it gave one). */
    inline juce::String caption (const RemoteSession::Info& i, const juce::String& consoleName)
    {
        auto text = consoleName;
        if (i.name.isNotEmpty() && i.state != State::Offline)
            text << " " << i.name;
        text << "  " << word (i.state);
        if (i.state == State::Connecting && i.readTotal > 0)
            text << " " << juce::String (i.readDone * 100 / i.readTotal) << " %";
        return text;
    }

    /** Lit while a session runs; a click asks for the address and connects, or disconnects. */
    inline void setup (universal::ToggleKey& key, UiContext& c)
    {
        key.isOn = [&c] { return infoOf (c).state != State::Offline; };
        key.setTooltip ("Control a real X32 over the network: the console is read first and wins, "
                        "this computer's audio is off while connected");
        key.onClick = [&c]
        {
            const bool active = infoOf (c).state != State::Offline;
            juce::MessageManager::callAsync ([&c, active]
            {
                if (auto& go = active ? c.remoteDisconnect : c.remoteConnect)
                    go();
            });
        };
    }

    /** Keeps the key's text and light right; true when the session's state changed since `shown`. */
    inline bool refresh (universal::ToggleKey& key, const UiContext& c, State& shown)
    {
        const auto s = infoOf (c).state;
        key.setButtonText (s == State::Offline ? "CONNECT" : "DISCONNECT");
        key.repaint();
        const bool changed = s != shown;
        shown = s;
        return changed;
    }
}

} // namespace livedaw::ui
