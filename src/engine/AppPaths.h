#pragma once

#include <juce_core/juce_core.h>

#include <atomic>

namespace livedaw
{

/** The app's name as the user sees it. */
inline constexpr const char* kAppName = "Sidecar";

/** The app's folders. The app was called "Live DAW" before; an existing
    folder under the old name keeps being used (projects refer to their
    audio by path, so it isn't moved). */
namespace paths
{
    inline juce::File chooseFolder (const juce::File& parent, const char* name, const char* oldName)
    {
        const auto current = parent.getChildFile (name);
        const auto old = parent.getChildFile (oldName);
        return ! current.exists() && old.isDirectory() ? old : current;
    }

    /** SIDECAR_HOME (an absolute folder), if set: the app data and documents
        folders live in it instead of the user's (test runs, such as the monkey
        test, that mustn't touch the real ones). */
    inline juce::File home()
    {
        const auto h = juce::SystemStats::getEnvironmentVariable ("SIDECAR_HOME", {});
        return juce::File::isAbsolutePath (h) ? juce::File (h) : juce::File();
    }

    /** <app data>/Sidecar (settings, logs, safe-mode state). */
    inline juce::File appData()
    {
        if (const auto h = home(); h != juce::File())
            return h.getChildFile ("AppData").getChildFile (kAppName);
        return chooseFolder (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory), kAppName, "LiveDAW");
    }

    /** <documents>/Sidecar (recordings, USB, library, screenshots). */
    inline juce::File documents()
    {
        if (const auto h = home(); h != juce::File())
            return h.getChildFile ("Documents").getChildFile (kAppName);
        return chooseFolder (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), kAppName, "Live DAW");
    }

    /** No actions outside the app: no file manager windows, no network servers
        (the monkey test sets it; the app never does). */
    inline std::atomic<bool>& externalActionsBlocked()
    {
        static std::atomic<bool> blocked { false };
        return blocked;
    }

    /** Shows `f` in the file manager (nothing while external actions are blocked). */
    inline void reveal (const juce::File& f)
    {
        if (! externalActionsBlocked().load())
            f.revealToUser();
    }
} // namespace paths

} // namespace livedaw
