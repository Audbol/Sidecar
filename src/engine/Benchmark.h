#pragma once

#include "engine/MixerEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace livedaw::bench
{

/** The built-in benchmark (SETUP > BENCHMARK): it measures this computer offline, on a
    thread of its own, with engines of its own that never open the audio interface (no
    sound, and no lock shared with the live engine's audio thread). Every figure is a
    block's processing time as a percentage of the block's real-time budget
    (block size / sample rate). */

struct Measure
{
    juce::String group;  // "Block", "Effect", "Console", "Console + effects", "Plug-in", "Recording", "Interface"
    juce::String name;   // the block model, the console, the plug-in...
    int blockSize = 0;
    int count = 1;       // strips the time was spread over (the figures are per strip)
    double meanPct = 0, p99Pct = 0, worstPct = 0; // of the block's budget
    double meanMs = 0;
    double value = 0;    // channels (Console), tracks (Recording), frames per second (Interface)
    juce::String note;
    juce::String key() const { return group + "|" + name + "|" + juce::String (blockSize); }
};

struct PluginUnderTest
{
    juce::String name;
    juce::AudioPluginInstance* instance = nullptr; // borrowed: made, and deleted, by the message thread
};

struct Settings
{
    double sampleRate = 48000.0;
    int blockSize = 128;
    std::vector<int> extraSizes;       // other buffer sizes, tried on the current console's channel ramp
    ld::ProfileId currentProfile = ld::kProfileUniversal;
    std::vector<ld::ProfileId> profiles; // the consoles to test (empty: all)
    bool doBlocks = true, doEffects = true, doConsoles = true, doRecording = true, doPlugins = true, doInterface = true;
    int blocks = 300, warmup = 30;     // timed blocks and warm-up blocks per test
    int modelSamples = 5;              // models of each table tried per console (0: all)
    int maxChannels = 256;
    double limitPct = 70.0;            // the 99th percentile a console may reach
    int firstChannels = 8;
    int recordMaxTracks = 64;
    bool quick = false;                // the QUICK TEST: the Mark is only an estimate
    double recordSeconds = 2.0;
    juce::File recordFolder;
    std::vector<PluginUnderTest> plugins;
    juce::String deviceInfo;           // the audio device's settings, for the report
    ld::ProfileId restoreShownProfile = ld::kProfileUniversal;
};

/** The Sidecar Mark: one number, higher is better, about 1000 for a typical mid-range PC.
    A weighted geometric mean of each part's measured figure against a fixed reference. The
    DSP part is always taken at 128 samples and 48 kHz, whatever the user's settings. */
struct Score
{
    double mark = 0, dsp = 0, effects = 0, recording = 0, interface = 0; // each 0 when not measured
    double raw[4] = { 0, 0, 0, 0 }; // the measured figures: channels, effects per second of audio, tracks, frames per second
    bool estimate = false;          // from the QUICK TEST
    bool valid() const noexcept { return mark > 0; }
    juce::String rating() const;
};

struct Result;
Score computeScore (const Result&);

struct Result
{
    juce::Time when;
    Settings settings;
    juce::StringPairArray machine;
    std::vector<Measure> measures;
    juce::StringArray summary, recommendations;
    bool cancelled = false;
    double seconds = 0;
    Score score;

    void add (Measure m) { measures.push_back (std::move (m)); }
    const Measure* find (const juce::String& group, const juce::String& name, int blockSize) const;
    /** The summary and the recommendations, from the measures. */
    void finish();
    juce::String toText() const;
    juce::String toCsv() const;
    juce::var toVar() const;
    static Result fromVar (const juce::var&);
    juce::String title() const;
};

juce::StringPairArray machineDetails (const juce::String& deviceInfo);
const char* profileName (ld::ProfileId);
/** The folder runs are kept in (<app data>/Benchmarks), newest name last. */
juce::File runsFolder();
juce::File save (const Result&, const juce::File& folder = runsFolder());
std::vector<Result> loadAll (const juce::File& folder = runsFolder());

/** One run, on a thread of its own. The message thread polls it (nothing here locks
    against the audio thread). */
class Runner final : private juce::Thread
{
public:
    Runner();
    ~Runner() override;

    void start (const Settings&);
    void cancel() { signalThreadShouldExit(); }
    bool isRunning() const { return isThreadRunning(); }
    /** Finished (or cancelled); the result is complete but for the interface test and finish(). */
    bool isDone() const noexcept { return done.load(); }
    Result& getResult() noexcept { return result; }

    struct Live
    {
        juce::String test;
        double progress = 0;
        double budgetMs = 0;
        juce::StringArray newLog; // lines since the last call
        std::vector<float> blockMs; // the last block times, oldest first
    };
    Live poll();

    static constexpr int kGraphLength = 400;

private:
    void run() override;
    struct Stats
    {
        double mean = 0, p99 = 0, worst = 0;
    };
    void setTest (const juce::String&);
    void logLine (const juce::String&);
    Stats timeBlocks (MixerEngine&, int numIn, int blocks, int warmup);
    Measure toMeasure (const juce::String& group, const juce::String& name, int blockSize, const Stats& s, const Stats& base, int count) const;
    void blockTests (ld::ProfileId);
    void effectTests (ld::ProfileId);
    void consoleTests (ld::ProfileId, int blockSize, bool withEffects);
    void pluginTests();
    void recordingTests();
    double rampStep (ld::ProfileId, int channels, int blockSize, bool effects, Stats& out, int& actual);
    double budgetMs (int bs) const { return bs / settings.sampleRate * 1000.0; }

    Settings settings;
    Result result;
    std::atomic<bool> done { false };
    std::atomic<double> progress { 0 }, sub { 0 };
    std::atomic<double> graphBudget { 0 };
    std::array<std::atomic<float>, kGraphLength> graph {};
    std::atomic<int> graphHead { 0 };
    juce::CriticalSection lock; // the log and the test name only; never held while processing
    juce::String currentTest;
    juce::StringArray pendingLog;
    int graphTaken = 0;
    int blockSize = 128;
};

} // namespace livedaw::bench
