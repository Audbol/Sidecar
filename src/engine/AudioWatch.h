#pragma once

#include <juce_core/juce_core.h>

namespace livedaw::audiowatch
{

/** Callbacks that stop for longer than this, once they have run, count as stalled. */
constexpr juce::uint32 kStallMs = 2000;
/** How long a device that has just started may take to send its first block. */
constexpr juce::uint32 kFirstBlockMs = 6000;

/** What the watchdog makes of the audio callbacks. */
enum class Callbacks
{
    idle,     // no device playing
    starting, // playing, no block yet, still within kFirstBlockMs
    running,
    stalled,  // they ran, then stopped
    silent    // playing but never sent a block: a driver that opened without starting
};

/** `startedMs`, `lastMs` and `now` are juce::Time::getMillisecondCounter() values
    (their differences hold across its wrap). */
inline Callbacks check (bool playing, bool calledBack, juce::uint32 now, juce::uint32 startedMs, juce::uint32 lastMs) noexcept
{
    if (! playing)
        return Callbacks::idle;
    if (! calledBack)
        return now - startedMs > kFirstBlockMs ? Callbacks::silent : Callbacks::starting;
    return now - lastMs > kStallMs ? Callbacks::stalled : Callbacks::running;
}

/** A low-latency driver that comes with a computer's own sound chip or with other
    software rather than with an audio interface. Some of these open and then never
    send audio, or hang when closed, so a first start doesn't pick one by itself
    (it can still be chosen in the audio settings). */
inline bool isStandInDriver (const juce::String& name)
{
    for (const auto* part : { "realtek", "generic low latency" })
        if (name.containsIgnoreCase (part))
            return true;
    return false;
}

/** The device a first start opens from a driver type's devices: the first that
    isn't a stand-in driver, or -1 for none. */
inline int firstStartDevice (const juce::StringArray& names)
{
    for (int i = 0; i < names.size(); ++i)
        if (! isStandInDriver (names[i]))
            return i;
    return -1;
}

} // namespace livedaw::audiowatch
