#pragma once

#include <juce_events/juce_events.h>

#include <memory>

namespace livedaw
{

/** Timestamped log of system events (device restarts, plugin faults, recalls...).
    Message thread only. Listeners are notified via ChangeBroadcaster. With a
    log file set, each entry is also written to it at once, so a run that
    freezes or crashes leaves its log behind for a bug report. */
class EventLog final : public juce::ChangeBroadcaster
{
public:
    enum class Level
    {
        Info,
        Warning,
        Error
    };

    struct Entry
    {
        juce::Time time;
        Level level;
        juce::String message;
    };

    void add (Level level, const juce::String& message)
    {
        entries.add ({ juce::Time::getCurrentTime(), level, message });
        if (entries.size() > kMaxEntries)
            entries.removeRange (0, entries.size() - kMaxEntries);

        juce::Logger::writeToLog ("[Sidecar] " + message);
        if (file != nullptr)
        {
            const char* tag = level == Level::Error ? "ERROR " : level == Level::Warning ? "WARN  " : "INFO  ";
            file->writeText (entries.getLast().time.formatted ("%Y-%m-%d %H:%M:%S ") + tag + message + "\n", false, false, nullptr);
            file->flush(); // on disk even if the app is killed next
        }
        sendChangeMessage();
    }

    /** Writes the log to `logFile` from now on. The previous run's log, if any,
        is first moved to `previousFile`. */
    void setLogFile (const juce::File& logFile, const juce::File& previousFile)
    {
        file.reset();
        if (logFile.existsAsFile())
        {
            previousFile.deleteFile();
            logFile.moveFileTo (previousFile);
        }
        auto stream = std::make_unique<juce::FileOutputStream> (logFile);
        if (stream->openedOk())
        {
            stream->setPosition (0);
            stream->truncate();
            file = std::move (stream);
        }
    }

    void info (const juce::String& m) { add (Level::Info, m); }
    void warning (const juce::String& m) { add (Level::Warning, m); }
    void error (const juce::String& m) { add (Level::Error, m); }

    const juce::Array<Entry>& getEntries() const noexcept { return entries; }

private:
    static constexpr int kMaxEntries = 500;
    juce::Array<Entry> entries;
    std::unique_ptr<juce::FileOutputStream> file;
};

} // namespace livedaw
