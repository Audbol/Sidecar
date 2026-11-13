#pragma once

#include "engine/Benchmark.h"
#include "ui/UiContext.h"
#include "ui/universal/BenchmarkPanel.h"
#include "ui/universal/Widgets.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace livedaw::ui::ph
{

/** The PLUGIN HOST's latency compensation (docs/PLUGIN_HOST.md, "Latency"). Two modes, an app setting:

    - AS IS (the default): each channel only has its own plug-ins' latency.
    - ALIGNED: every channel is delayed to the longest channel's plug-in latency, so that phase-related sources
      (drum mics) stay in time. A channel can opt out (an app setting too: a vocal someone hears live) and is then
      left as it is; it still counts when the longest is worked out.

    A channel's own latency is the sum of its loaded plug-ins' reported latencies, leaving out a plug-in that is
    bypassed or faulted, and nothing at all while the channel's BYPASS is on (the input passes straight through). The
    delay is the engine's strip delay stage (the host's channels have one after the level, in samples from 0): the
    channel's delay is set to `longest - own`, up to the engine's compensation limit, and off when it is 0. update()
    reads the racks again and sets the delays; the surface's timer calls it, so a plug-in that changes its latency, a
    bypass, a plug-in added or removed all follow within a fraction of a second. AS IS switches every delay off.
    Message thread only. */
class Latency
{
public:
    enum class Mode
    {
        AsIs,
        Aligned
    };
    static constexpr const char* modeKey = "pluginhost.latency.mode";       // "asis" (default) or "aligned"
    static constexpr const char* optOutKey = "pluginhost.latency.optout";    // the channels left alone, 1-based, comma separated

    explicit Latency (UiContext&);

    Mode getMode() const;
    /** Sets the mode, keeps it and applies it at once. */
    void setMode (Mode);
    bool isOptedOut (int channel) const;
    void setOptedOut (int channel, bool optedOut);

    /** Channel `channel`'s own plug-in latency now (samples). */
    int getRackLatency (int channel) const;
    /** The longest of them: what ALIGNED delays the channels to. */
    int getLongest() const;
    /** The delay the channel has now (samples; 0: none). */
    int getDelay (int channel) const;
    /** Its round trip in the host, the rack's latency and the delay (samples). */
    int getTotal (int channel) const { return getRackLatency (channel) + getDelay (channel); }
    /** The longest rack is beyond what the engine can delay (the channels stay late by the rest). */
    bool isLimited() const { return getLongest() > MixerEngine::kMaxCompensation; }
    /** Sets every channel's delay for the mode and the racks as they are now; true when any changed. */
    bool update();

    /** The samples as "128 smp  (2.7 ms)". */
    static juce::String describe (int samples, double sampleRate);

private:
    Strip* stripAt (int channel) const;

    UiContext& ctx;
};

/** The CPU guard: before a plug-in is added, a warning when the engine's CPU is above a threshold (an app setting,
    70 % at first). It is only a warning: the plug-in is added anyway. The benchmark's newest saved run is read for the
    page's line about this computer (and a plug-in it measured is named in the warning); with none saved the guard
    suggests running one. */
class Guard
{
public:
    static constexpr const char* thresholdKey = "pluginhost.guard.threshold"; // percent
    static constexpr int kDefaultThreshold = 70, kMinThreshold = 10, kMaxThreshold = 100;

    explicit Guard (UiContext&);

    int getThreshold() const;
    void setThreshold (int percent);
    /** The engine's CPU now, 0..1 (the audio device's; tests replace it). */
    std::function<double()> getCpu;
    int getCpuPercent() const { return juce::roundToInt (juce::jlimit (0.0, 1.0, getCpu()) * 100.0); }

    /** Where the benchmark keeps its runs (tests: a folder of their own). */
    juce::File runsFolder = bench::runsFolder();
    /** Reads the saved runs again; the newest is the one the guard knows. */
    void reloadBenchmark();
    const bench::Result* getBenchmark() const noexcept { return latest.has_value() ? &*latest : nullptr; }
    /** "2026-11-12 10:30   MARK 1020   FINE FOR SMALL TO MEDIUM SHOWS", or the suggestion to run one. */
    juce::String describeBenchmark() const;

    struct Advice
    {
        bool overThreshold = false, noBenchmark = false;
        juce::String text; // empty: nothing to say
    };
    /** What to say before `description` is added. */
    Advice check (const juce::PluginDescription& description) const;
    /** check() as the add does it: the text becomes the notice (and goes to the event log). */
    Advice noteAdd (const juce::PluginDescription& description);
    const juce::String& getNotice() const noexcept { return notice; }
    void clearNotice() { notice.clear(); }

private:
    UiContext& ctx;
    std::optional<bench::Result> latest;
    juce::String notice;
};

/** The SYSTEM page of the PLUGIN HOST's screen (docs/PLUGIN_HOST.md, page 7). Three columns:

    - AUDIO DEVICE and CHANNELS (left): the device, its rate and buffer, and AUDIO SETTINGS, which opens the audio
      settings of the Universal Mixer's SETUP over the page (CLOSE returns). CHANNELS: 16, 32 or 64; a tap on another
      size arms it (the key reads CONFIRM for a few seconds) and a second tap does it. The engine resizes the host in
      place: the channels that remain keep their racks, plug-ins, settings and patch; channels past the new count go
      with their racks and mappings; new ones come on the factory settings. The page says so. BENCHMARK opens the
      benchmark panel over the page.
    - LATENCY COMPENSATION and CPU GUARD (middle): AS IS or ALIGNED, the longest rack; the engine's CPU, the
      guard's threshold (- and +), what the benchmark knew, and the last warning.
    - CHANNELS' LATENCY (right): each channel's rack latency, its delay and the total, with an OUT key per channel that
      opts it out of ALIGNED. Rows are 48 px, keys 52 px (touch sized in the 1920 x 1080 design space).

    Message thread only. */
class SystemPage final : public juce::Component, private juce::Timer
{
public:
    SystemPage (UiContext&, const universal::Theme&, Latency&, Guard&);
    ~SystemPage() override;

    /** Reads everything again (the timer does this too). */
    void refresh();

    // Audio device and benchmark: panels over the page.
    universal::ToggleKey& getAudioKey() noexcept { return audioKey; }
    universal::ToggleKey& getBenchmarkKey() noexcept { return benchmarkKey; }
    void openAudio();
    void openBenchmark();
    void closePanel();
    bool isAudioOpen() const noexcept { return panel == Panel::Audio; }
    bool isBenchmarkOpen() const noexcept { return panel == Panel::Benchmark; }
    juce::Component* getPanel() noexcept;
    /** The benchmark panel once it has been opened (it keeps running while closed). */
    universal::BenchmarkPanel* getBenchmarkPanel() noexcept;
    /** The audio device as the page says it ("NO AUDIO DEVICE", "Speakers  48000 Hz  256 smp  5.3 ms"). */
    juce::String describeDevice() const;

    // The channel count.
    /** The surface resizes the host (false: it did not); set by the surface. */
    std::function<bool (int channels)> resize;
    universal::ToggleKey& getSizeKey (int index) noexcept { return *sizeKeys[(size_t) juce::jlimit (0, 2, index)]; }
    static int sizeOf (int index) noexcept { return 16 << juce::jlimit (0, 2, index); }
    /** Asks twice: the first press of another size arms it (CONFIRM), the second does it. */
    void pressSize (int channels);
    int getArmedSize() const noexcept { return armedSize; }
    /** What a change to `channels` does, as the page says it. */
    juce::String describeResize (int channels) const;

    // Latency.
    universal::ToggleKey& getModeKey (bool aligned) noexcept { return aligned ? alignedKey : asIsKey; }
    struct Row
    {
        int channel = 0; // 0-based
        juce::String name;
        int rack = 0, delay = 0, total = 0;
        bool optedOut = false, bypassed = false;
    };
    int getNumRows() const noexcept { return (int) rows.size(); }
    const Row& getRow (int row) const { return rows[(size_t) row]; }
    void toggleOptOut (int row);
    juce::Rectangle<int> getRowBounds (int row) const;
    juce::Rectangle<int> getOptOutBounds (int row) const;
    void scrollRows (int firstRow);
    int getFirstRow() const noexcept { return rowTop; }

    // The CPU guard.
    universal::ToggleKey& getThresholdKey (bool up) noexcept { return up ? thresholdUp : thresholdDown; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int kRow = 48, kHead = 36, kKey = 52, kLeftWidth = 560, kMiddleX = 580, kMiddleWidth = 620, kRightX = 1212, kGap = 12;

private:
    class Overlay;
    enum class Panel
    {
        None,
        Audio,
        Benchmark
    };

    void timerCallback() override { refresh(); }
    void disarm();
    void clampScroll();
    void showPanel (Panel);
    juce::Rectangle<int> leftRect() const;
    juce::Rectangle<int> middleRect() const;
    juce::Rectangle<int> rightRect() const;
    juce::Rectangle<int> listRect() const;
    int visibleRows() const;

    UiContext& ctx;
    const universal::Theme& theme;
    Latency& latency;
    Guard& guard;
    universal::ToggleKey audioKey, benchmarkKey, asIsKey, alignedKey, thresholdDown, thresholdUp;
    std::array<std::unique_ptr<universal::ToggleKey>, 3> sizeKeys;
    std::vector<Row> rows;
    juce::String signature, message;
    int rowTop = 0, armedSize = 0;
    juce::uint32 armedAt = 0;
    Panel panel = Panel::None;
    std::unique_ptr<Overlay> audioOverlay, benchmarkOverlay;
    int dragFrom = 0;
    bool dragging = false, dragStarted = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SystemPage)
};

} // namespace livedaw::ui::ph
