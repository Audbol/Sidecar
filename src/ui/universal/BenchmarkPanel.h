#pragma once

#include "engine/Benchmark.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <array>
#include <memory>

namespace livedaw::ui::universal
{

/** SETUP > BENCHMARK: runs the built-in benchmark (engine/Benchmark.h) and shows all of it
    live: the test running and its progress, a graph of block times against the budget, a
    log of every measurement and a cancel key. Afterwards the plain summary, the
    recommendations and every number in tables, kept as a saved run to compare later. */
class BenchmarkPanel final : public juce::Component, private juce::Timer
{
public:
    BenchmarkPanel (UiContext&, const Theme&);
    ~BenchmarkPanel() override;

    enum class State
    {
        Idle,
        Running,
        Interface, // the frame-rate tests, on this (message) thread
        Done
    };
    State getState() const noexcept { return state; }

    /** The settings the next run uses: this device's rate and buffer, the keys' choices. */
    bench::Settings buildSettings() const;
    void start();
    void cancel();
    /** One step of the poll (the timer calls it; tests call it to wait). */
    void tick();
    const bench::Result& getResult() const noexcept { return result; }
    const juce::StringArray& getLog() const noexcept { return log; }
    /** Saved runs, oldest first; where they are kept (tests: a folder of their own). */
    const std::vector<bench::Result>& getSaved() const noexcept { return saved; }
    juce::File runsFolder = bench::runsFolder();
    void reloadSaved();
    /** Compare with the saved run `index` (-1: none); the tables then show its figures beside this run's. */
    void setCompare (int index);
    bool exportTo (const juce::File&, bool csv) const;

    // The choices.
    std::array<bool, 6> bufferSizes { false, false, true, true, false, false }; // 32 64 128 256 512 1024: the current one is always tested
    std::array<bool, 6> tests { true, true, true, true, false, true };    // blocks, effects, consoles, recording, plug-ins, interface
    bool quick = false;
    std::vector<ld::ProfileId> onlyProfiles; // empty: every console
    static constexpr std::array<int, 6> kSizes { 32, 64, 128, 256, 512, 1024 };

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Body;
    void timerCallback() override { tick(); }
    void finishRun();
    void runInterfaceStep();
    void refreshKeys();
    void showBody();
    void chooseExport (bool csv);

    UiContext& ctx;
    const Theme& theme;
    bench::Runner runner;
    bench::Result result;
    std::vector<bench::Result> saved;
    int compareIndex = -1;
    State state = State::Idle;
    juce::StringArray log;
    juce::String test;
    double progress = 0, budgetMs = 0;
    std::vector<float> graph;
    bool showLog = false;
    juce::String note;
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> pluginInstances;
    int uiPhase = 0, uiFrame = 0;
    std::vector<double> uiTimes;
    std::unique_ptr<juce::Component> uiTarget;
    std::unique_ptr<juce::FileChooser> chooser;

    ToggleKey startKey, cancelKey, quickKey, textKey, csvKey, compareKey, logKey;
    std::array<std::unique_ptr<ToggleKey>, 6> sizeKeys, testKeys;
    juce::Viewport viewport;
    std::unique_ptr<Body> body;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BenchmarkPanel)
};

} // namespace livedaw::ui::universal
