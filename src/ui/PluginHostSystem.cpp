#include "ui/PluginHostSystem.h"

#include "engine/FxSlots.h"

#include <juce_audio_utils/juce_audio_utils.h> // AudioDeviceSelectorComponent

#include <set>

namespace livedaw::ui::ph
{
using namespace universal;

namespace
{
    constexpr int kArmMs = 4000;
    constexpr int kDragPixels = 8;

    std::set<int> parseChannels (const juce::String& text)
    {
        std::set<int> channels;
        for (const auto& part : juce::StringArray::fromTokens (text, ",", {}))
            if (const int n = part.trim().getIntValue(); n > 0)
                channels.insert (n);
        return channels;
    }
} // namespace

//==============================================================================
// Latency

Latency::Latency (UiContext& c) : ctx (c) {}

Strip* Latency::stripAt (int channel) const
{
    const auto& list = ctx.engine.getChannels();
    return juce::isPositiveAndBelow (channel, (int) list.size()) ? list[(size_t) channel].get() : nullptr;
}

Latency::Mode Latency::getMode() const
{
    return ctx.settings.getValue (modeKey, "asis") == "aligned" ? Mode::Aligned : Mode::AsIs;
}

void Latency::setMode (Mode mode)
{
    ctx.settings.setValue (modeKey, mode == Mode::Aligned ? "aligned" : "asis");
    ctx.settings.saveIfNeeded();
    update();
}

bool Latency::isOptedOut (int channel) const
{
    return parseChannels (ctx.settings.getValue (optOutKey)).count (channel + 1) > 0;
}

void Latency::setOptedOut (int channel, bool optedOut)
{
    auto channels = parseChannels (ctx.settings.getValue (optOutKey));
    if (optedOut)
        channels.insert (channel + 1);
    else
        channels.erase (channel + 1);
    juce::StringArray text;
    for (const int n : channels)
        text.add (juce::String (n));
    ctx.settings.setValue (optOutKey, text.joinIntoString (","));
    ctx.settings.saveIfNeeded();
    update();
}

int Latency::getRackLatency (int channel) const
{
    auto* s = stripAt (channel);
    if (s == nullptr || s->params.input.bypass.load())
        return 0; // the channel's BYPASS passes the input straight through: no plug-in runs
    int total = 0;
    for (const auto& step : s->getChain())
        if (step.isBlock() && step.getBlockKind() == BlockKind::Plugin)
        {
            const auto info = ctx.engine.getFx().describeBlock (s->getHandle(), step.uid);
            if (info.loaded && ! info.bypassed && ! info.faulted)
                total += info.latency;
        }
    return total;
}

int Latency::getLongest() const
{
    int longest = 0;
    for (int i = 0; i < (int) ctx.engine.getChannels().size(); ++i)
        longest = juce::jmax (longest, getRackLatency (i));
    return longest;
}

int Latency::getDelay (int channel) const
{
    auto* s = stripAt (channel);
    return s != nullptr && s->params.input.delayOn.load() ? juce::jmax (0, juce::roundToInt (s->params.input.delay.load())) : 0;
}

bool Latency::update()
{
    if (ctx.engine.getProfile() != ld::kProfilePluginHost)
        return false;
    const bool aligned = getMode() == Mode::Aligned;
    const auto opted = parseChannels (ctx.settings.getValue (optOutKey));
    const int longest = getLongest();
    bool changed = false;
    for (int i = 0; i < (int) ctx.engine.getChannels().size(); ++i)
    {
        auto* s = stripAt (i);
        auto& in = s->params.input;
        const int want = aligned && opted.count (i + 1) == 0 ? juce::jmin (MixerEngine::kMaxCompensation, longest - getRackLatency (i)) : 0;
        if (want > 0)
        {
            if (! in.delayOn.load() || juce::roundToInt (in.delay.load()) != want || in.delayMode.load() != 3)
            {
                in.delayMode = 3; // samples
                in.delay = (float) want;
                in.delayOn = true;
                changed = true;
            }
        }
        else if (in.delayOn.load())
        {
            in.delayOn = false;
            changed = true;
        }
    }
    return changed;
}

juce::String Latency::describe (int samples, double sampleRate)
{
    auto text = juce::String (samples) + " smp";
    if (sampleRate > 0.0)
        text << "  (" << juce::String (1000.0 * samples / sampleRate, 1) << " ms)";
    return text;
}

//==============================================================================
// Guard

Guard::Guard (UiContext& c) : ctx (c)
{
    getCpu = [this] { return ctx.deviceManager.getCpuUsage(); };
}

int Guard::getThreshold() const
{
    return juce::jlimit (kMinThreshold, kMaxThreshold, ctx.settings.getIntValue (thresholdKey, kDefaultThreshold));
}

void Guard::setThreshold (int percent)
{
    ctx.settings.setValue (thresholdKey, juce::jlimit (kMinThreshold, kMaxThreshold, percent));
    ctx.settings.saveIfNeeded();
}

void Guard::reloadBenchmark()
{
    auto runs = bench::loadAll (runsFolder);
    latest.reset();
    if (! runs.empty())
        latest = std::move (runs.back());
}

juce::String Guard::describeBenchmark() const
{
    if (! latest.has_value())
        return "NO BENCHMARK IS SAVED. RUN ONE (BENCHMARK) SO THE GUARD KNOWS THIS COMPUTER.";
    const auto& r = *latest;
    auto text = r.when.formatted ("%Y-%m-%d %H:%M") + "   ";
    if (r.score.valid())
        text << "MARK " << juce::roundToInt (r.score.mark) << (r.score.estimate ? " (ESTIMATE)" : "") << "   " << r.score.rating().toUpperCase();
    else
        text << "NO MARK (THE RUN WAS PARTIAL)";
    text << "   AT " << r.settings.blockSize << " smp";
    return text;
}

Guard::Advice Guard::check (const juce::PluginDescription& description) const
{
    Advice a;
    const int cpu = getCpuPercent(), threshold = getThreshold();
    a.overThreshold = cpu > threshold;
    a.noBenchmark = ! latest.has_value();
    juce::StringArray parts;
    if (a.overThreshold)
    {
        auto text = "ENGINE CPU IS " + juce::String (cpu) + " %, OVER THE GUARD'S " + juce::String (threshold) + " %. ADDING " + description.name.toUpperCase()
                    + " MAY MAKE THE AUDIO BREAK UP.";
        if (latest.has_value())
            if (const auto* m = latest->find ("Plug-in", description.name, 0))
                text << " THE BENCHMARK MEASURED IT AT ABOUT " << juce::roundToInt (m->meanPct) << " % OF A BLOCK.";
        parts.add (text);
    }
    if (a.noBenchmark)
        parts.add ("NO BENCHMARK IS SAVED: RUN ONE (SYSTEM > BENCHMARK) SO THE GUARD KNOWS THIS COMPUTER.");
    a.text = parts.joinIntoString ("  ");
    return a;
}

Guard::Advice Guard::noteAdd (const juce::PluginDescription& description)
{
    auto a = check (description);
    notice = a.text;
    if (a.overThreshold)
        ctx.log.warning ("PLUGIN HOST: " + a.text);
    return a;
}

//==============================================================================
// SystemPage

/** A panel over the page (the audio settings, the benchmark) with a CLOSE key. */
class SystemPage::Overlay final : public juce::Component
{
public:
    Overlay (const Theme& t, const juce::String& heading, std::unique_ptr<juce::Component> c, int maxWidth, std::function<void()> onClose)
        : theme (t), title (heading), content (std::move (c)), closeKey ("CLOSE", t.accent, t), widest (maxWidth)
    {
        closeKey.onClick = std::move (onClose);
        addAndMakeVisible (*content);
        addAndMakeVisible (closeKey);
    }

    juce::Component& getContent() noexcept { return *content; }
    ToggleKey& getCloseKey() noexcept { return closeKey; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme.background);
        g.setColour (theme.text);
        g.setFont (type::font (type::kHeading, true));
        g.drawText (title, getLocalBounds().withHeight (kKey + 8).withTrimmedLeft (16), juce::Justification::centredLeft);
        g.setColour (theme.outline);
        g.drawHorizontalLine (kKey + 8, 0.0f, (float) getWidth());
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto head = area.removeFromTop (kKey + 8);
        closeKey.setBounds (head.removeFromRight (180).reduced (4));
        area.removeFromTop (4);
        content->setBounds (widest > 0 ? area.withWidth (juce::jmin (area.getWidth(), widest)) : area);
    }

private:
    const Theme& theme;
    juce::String title;
    std::unique_ptr<juce::Component> content;
    ToggleKey closeKey;
    int widest;
};

SystemPage::SystemPage (UiContext& c, const Theme& t, Latency& l, Guard& g)
    : ctx (c), theme (t), latency (l), guard (g), audioKey ("AUDIO SETTINGS", t.accent, t), benchmarkKey ("BENCHMARK", t.accent, t),
      asIsKey ("AS IS", t.accent, t), alignedKey ("ALIGNED", t.accent, t), thresholdDown ("-", t.accent, t), thresholdUp ("+", t.accent, t)
{
    audioKey.onClick = [this] { openAudio(); };
    benchmarkKey.onClick = [this] { openBenchmark(); };
    asIsKey.isOn = [this] { return latency.getMode() == Latency::Mode::AsIs; };
    alignedKey.isOn = [this] { return latency.getMode() == Latency::Mode::Aligned; };
    asIsKey.onClick = [this]
    {
        latency.setMode (Latency::Mode::AsIs);
        refresh();
    };
    alignedKey.onClick = [this]
    {
        latency.setMode (Latency::Mode::Aligned);
        refresh();
    };
    thresholdDown.onClick = [this]
    {
        guard.setThreshold (guard.getThreshold() - 5);
        refresh();
    };
    thresholdUp.onClick = [this]
    {
        guard.setThreshold (guard.getThreshold() + 5);
        refresh();
    };
    audioKey.setTooltip ("The audio device, its sample rate and buffer size");
    benchmarkKey.setTooltip ("Measures this computer: the saved result is what the CPU guard knows");
    asIsKey.setTooltip ("Each channel has only its own plug-ins' latency");
    alignedKey.setTooltip ("Every channel is delayed to the longest channel's plug-in latency, except the ones that opted out");
    thresholdDown.setTooltip ("A lower CPU level for the guard's warning");
    thresholdUp.setTooltip ("A higher CPU level for the guard's warning");
    for (auto* k : { &audioKey, &benchmarkKey, &asIsKey, &alignedKey, &thresholdDown, &thresholdUp })
        addAndMakeVisible (*k);
    for (int i = 0; i < 3; ++i)
    {
        auto key = std::make_unique<ToggleKey> (juce::String (sizeOf (i)), t.accent, t);
        key->isOn = [this, i] { return (int) ctx.engine.getChannels().size() == sizeOf (i); };
        key->onClick = [this, i] { pressSize (sizeOf (i)); };
        key->setTooltip ("The host's channel count: " + juce::String (sizeOf (i)) + ". Press twice to change it.");
        addAndMakeVisible (*key);
        sizeKeys[(size_t) i] = std::move (key);
    }
    guard.reloadBenchmark();
    refresh();
    startTimerHz (4);
}

SystemPage::~SystemPage()
{
    stopTimer();
}

//==============================================================================
juce::String SystemPage::describeDevice() const
{
    auto* device = ctx.deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return "NO AUDIO DEVICE";
    const double rate = device->getCurrentSampleRate();
    const int buffer = device->getCurrentBufferSizeSamples();
    return device->getName() + "   " + juce::String (juce::roundToInt (rate)) + " Hz   " + juce::String (buffer) + " smp   "
           + juce::String (rate > 0.0 ? 1000.0 * buffer / rate : 0.0, 1) + " ms";
}

void SystemPage::showPanel (Panel p)
{
    panel = p;
    if (audioOverlay != nullptr)
        audioOverlay->setVisible (p == Panel::Audio);
    if (benchmarkOverlay != nullptr)
        benchmarkOverlay->setVisible (p == Panel::Benchmark);
    resized();
    repaint();
}

void SystemPage::openAudio()
{
    if (audioOverlay == nullptr)
    {
        audioOverlay = std::make_unique<Overlay> (theme, "AUDIO SETTINGS",
                                                  std::make_unique<juce::AudioDeviceSelectorComponent> (ctx.deviceManager, 0, 256, 0, 256, false, false, false, false),
                                                  760, [this] { closePanel(); });
        addChildComponent (*audioOverlay);
    }
    showPanel (Panel::Audio);
}

void SystemPage::openBenchmark()
{
    if (benchmarkOverlay == nullptr)
    {
        benchmarkOverlay = std::make_unique<Overlay> (theme, "BENCHMARK", std::make_unique<BenchmarkPanel> (ctx, theme), 0, [this] { closePanel(); });
        addChildComponent (*benchmarkOverlay);
    }
    showPanel (Panel::Benchmark);
}

void SystemPage::closePanel()
{
    const bool wasBenchmark = panel == Panel::Benchmark;
    showPanel (Panel::None);
    if (wasBenchmark)
        guard.reloadBenchmark(); // a run that finished is saved: the guard knows it now
    refresh();
}

juce::Component* SystemPage::getPanel() noexcept
{
    return panel == Panel::Audio ? static_cast<juce::Component*> (audioOverlay.get()) : panel == Panel::Benchmark ? static_cast<juce::Component*> (benchmarkOverlay.get()) : nullptr;
}

BenchmarkPanel* SystemPage::getBenchmarkPanel() noexcept
{
    return benchmarkOverlay != nullptr ? dynamic_cast<BenchmarkPanel*> (&benchmarkOverlay->getContent()) : nullptr;
}

//==============================================================================
juce::String SystemPage::describeResize (int channels) const
{
    const int now = (int) ctx.engine.getChannels().size();
    if (channels == now)
        return {};
    if (channels > now)
        return juce::String (now) + " > " + juce::String (channels) + ": CH " + juce::String (now + 1) + "-" + juce::String (channels) + " ARE ADDED, EMPTY. CH 1-"
               + juce::String (now) + " KEEP THEIR RACKS, PLUG-INS, SETTINGS AND PATCH. PRESS AGAIN TO CHANGE.";
    int plugins = 0;
    for (int i = channels; i < now; ++i)
        if (auto* s = ctx.engine.getChannels()[(size_t) i].get())
            for (const auto& step : s->getChain())
                if (step.isBlock() && step.getBlockKind() == BlockKind::Plugin && ctx.engine.getFx().describeBlock (s->getHandle(), step.uid).loaded)
                    ++plugins;
    return juce::String (now) + " > " + juce::String (channels) + ": CH " + juce::String (channels + 1) + "-" + juce::String (now) + " GO WITH THEIR RACKS ("
           + juce::String (plugins) + (plugins == 1 ? " PLUG-IN" : " PLUG-INS") + "), THEIR MIDI MAPPINGS AND THE OUTPUTS THAT PLAYED THEM. CH 1-" + juce::String (channels)
           + " KEEP EVERYTHING. PRESS AGAIN TO CHANGE.";
}

void SystemPage::pressSize (int channels)
{
    const int now = (int) ctx.engine.getChannels().size();
    if (channels == now || (channels != 16 && channels != 32 && channels != 64))
    {
        disarm();
        message.clear();
        refresh();
        return;
    }
    if (armedSize != channels)
    {
        disarm();
        armedSize = channels;
        armedAt = juce::Time::getMillisecondCounter();
        message = describeResize (channels);
        for (int i = 0; i < 3; ++i)
            sizeKeys[(size_t) i]->setButtonText (sizeOf (i) == channels ? "CONFIRM" : juce::String (sizeOf (i)));
        refresh();
        repaint();
        return;
    }
    disarm();
    const bool done = resize != nullptr && resize (channels);
    message = done ? "THE HOST HAS " + juce::String (channels) + " CHANNELS NOW." : juce::String ("THE SIZE COULD NOT BE CHANGED.");
    refresh();
}

void SystemPage::disarm()
{
    if (armedSize == 0)
        return;
    armedSize = 0;
    message.clear();
    for (int i = 0; i < 3; ++i)
        sizeKeys[(size_t) i]->setButtonText (juce::String (sizeOf (i)));
    repaint();
}

//==============================================================================
void SystemPage::toggleOptOut (int row)
{
    if (! juce::isPositiveAndBelow (row, getNumRows()))
        return;
    const int channel = rows[(size_t) row].channel;
    latency.setOptedOut (channel, ! latency.isOptedOut (channel));
    refresh();
}

juce::Rectangle<int> SystemPage::leftRect() const
{
    return { 8, 0, kLeftWidth, getHeight() };
}

juce::Rectangle<int> SystemPage::middleRect() const
{
    return { kMiddleX, 0, kMiddleWidth, getHeight() };
}

juce::Rectangle<int> SystemPage::rightRect() const
{
    return { kRightX, 0, getWidth() - kRightX - 8, getHeight() };
}

juce::Rectangle<int> SystemPage::listRect() const
{
    return rightRect().withTrimmedTop (kHead).withTrimmedBottom (8);
}

int SystemPage::visibleRows() const
{
    return juce::jmax (1, listRect().getHeight() / kRow);
}

void SystemPage::clampScroll()
{
    rowTop = juce::jlimit (0, juce::jmax (0, getNumRows() - visibleRows()), rowTop);
}

void SystemPage::scrollRows (int firstRow)
{
    rowTop = firstRow;
    clampScroll();
    repaint();
}

juce::Rectangle<int> SystemPage::getRowBounds (int row) const
{
    const auto list = listRect();
    return { list.getX(), list.getY() + (row - rowTop) * kRow, list.getWidth(), kRow };
}

juce::Rectangle<int> SystemPage::getOptOutBounds (int row) const
{
    return getRowBounds (row).removeFromRight (104).reduced (4, 2);
}

void SystemPage::resized()
{
    const auto l = leftRect(), m = middleRect();
    audioKey.setBounds (l.getX(), 100, l.getWidth(), kKey);
    const int w = (l.getWidth() - 2 * 8) / 3;
    for (int i = 0; i < 3; ++i)
        sizeKeys[(size_t) i]->setBounds (l.getX() + i * (w + 8), 212, w, kKey);
    benchmarkKey.setBounds (l.getX(), 396, l.getWidth(), kKey);
    const int half = (m.getWidth() - 8) / 2;
    asIsKey.setBounds (m.getX(), 40, half, kKey);
    alignedKey.setBounds (m.getX() + half + 8, 40, half, kKey);
    thresholdDown.setBounds (m.getRight() - 2 * 96 - 8, 348, 96, kKey);
    thresholdUp.setBounds (m.getRight() - 96, 348, 96, kKey);
    if (audioOverlay != nullptr)
        audioOverlay->setBounds (getLocalBounds());
    if (benchmarkOverlay != nullptr)
        benchmarkOverlay->setBounds (getLocalBounds());
    clampScroll();
}

//==============================================================================
void SystemPage::refresh()
{
    const auto& list = ctx.engine.getChannels();
    rows.clear();
    for (int i = 0; i < (int) list.size(); ++i)
    {
        Row r;
        r.channel = i;
        const auto own = list[(size_t) i]->getOwnName();
        r.name = own == juce::String (i + 1) ? juce::String() : own;
        r.rack = latency.getRackLatency (i);
        r.delay = latency.getDelay (i);
        r.total = r.rack + r.delay;
        r.optedOut = latency.isOptedOut (i);
        r.bypassed = list[(size_t) i]->params.input.bypass.load();
        rows.push_back (std::move (r));
    }
    clampScroll();
    if (armedSize != 0 && (juce::Time::getMillisecondCounter() - armedAt > (juce::uint32) kArmMs || armedSize == (int) list.size()))
        disarm();
    for (auto* k : { &audioKey, &benchmarkKey, &asIsKey, &alignedKey, &thresholdDown, &thresholdUp })
        k->refresh();
    for (auto& k : sizeKeys)
        k->refresh();

    auto now = juce::String (latency.getLongest()) + "|" + juce::String (guard.getCpuPercent()) + "|" + juce::String (guard.getThreshold()) + "|" + message + "|" + guard.getNotice()
               + "|" + describeDevice() + "|" + guard.describeBenchmark() + "|" + juce::String (rowTop) + "|" + juce::String (list.size());
    for (const auto& r : rows)
        now << "|" << r.name << r.rack << "," << r.delay << (r.optedOut ? "o" : "") << (r.bypassed ? "b" : "");
    if (now != signature)
    {
        signature = now;
        repaint();
    }
}

//==============================================================================
void SystemPage::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    const double rate = ctx.engine.getSampleRate();

    const auto head = [&] (juce::Rectangle<int> panelArea, int y, const juce::String& title, const juce::String& hint = {})
    {
        const auto r = juce::Rectangle<int> (panelArea.getX(), y, panelArea.getWidth(), kHead);
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel, true));
        g.drawText (title, r.withTrimmedLeft (4), juce::Justification::centredLeft);
        g.setFont (type::font (type::kCaption));
        g.drawFittedText (hint, r.withTrimmedLeft (200), juce::Justification::centredRight, 1);
    };
    const auto text = [&] (juce::Rectangle<int> r, const juce::String& s, juce::Colour colour, float size, bool bold, int lines)
    {
        g.setColour (colour);
        g.setFont (type::font (size, bold));
        g.drawFittedText (s, r, juce::Justification::topLeft, lines, 0.9f);
    };

    // AUDIO DEVICE, CHANNELS, BENCHMARK.
    {
        const auto l = leftRect();
        head (l, 0, "AUDIO DEVICE");
        text ({ l.getX() + 4, 40, l.getWidth() - 8, 56 }, describeDevice(), theme.text, type::kValue, true, 2);
        head (l, 172, "CHANNELS", "the host's size");
        text ({ l.getX() + 4, 270, l.getWidth() - 8, 80 }, message.isNotEmpty() ? message : "16, 32 OR 64. CHANGING THE SIZE KEEPS THE RACKS OF THE CHANNELS THAT REMAIN.",
              armedSize != 0 ? theme.accent : theme.textDim, type::kCaption, armedSize != 0, 5);
        head (l, 356, "BENCHMARK");
        text ({ l.getX() + 4, 454, l.getWidth() - 8, 66 }, guard.describeBenchmark(), guard.getBenchmark() == nullptr ? theme.accent : theme.text, type::kCaption, false, 3);
    }

    // LATENCY COMPENSATION and CPU GUARD.
    {
        const auto m = middleRect();
        head (m, 0, "LATENCY COMPENSATION");
        const bool aligned = latency.getMode() == Latency::Mode::Aligned;
        text ({ m.getX() + 4, 100, m.getWidth() - 8, 80 },
              aligned ? "ALIGNED: EVERY CHANNEL IS DELAYED TO THE LONGEST CHANNEL'S PLUG-IN LATENCY, SO PHASE-RELATED SOURCES STAY IN TIME. A CHANNEL THAT OPTS OUT (OUT, AT THE RIGHT) IS LEFT AS IT IS."
                      : "AS IS: EACH CHANNEL HAS ONLY ITS OWN PLUG-INS' LATENCY. SWITCH TO ALIGNED FOR DRUM MICS AND OTHER SOURCES THAT MUST STAY IN TIME.",
              theme.textDim, type::kCaption, false, 5);
        const int longest = latency.getLongest();
        text ({ m.getX() + 4, 190, m.getWidth() - 8, 24 }, "LONGEST RACK  " + Latency::describe (longest, rate), theme.text, type::kValue, true, 1);
        if (latency.isLimited())
            text ({ m.getX() + 4, 214, m.getWidth() - 8, 40 }, "THE ENGINE CAN DELAY A CHANNEL BY " + juce::String (MixerEngine::kMaxCompensation) + " smp AT MOST: THE REST STAYS LATE.", theme.mute, type::kCaption, true, 2);

        head (m, 262, "CPU GUARD", "warns before a plug-in is added; never stops it");
        const int cpu = guard.getCpuPercent(), threshold = guard.getThreshold();
        const juce::Rectangle<float> bar ((float) m.getX() + 4.0f, 308.0f, (float) m.getWidth() - 8.0f, 28.0f);
        g.setColour (theme.well);
        g.fillRoundedRectangle (bar, 5.0f);
        g.setColour (cpu > threshold ? theme.mute : theme.meterLow);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0, 100, cpu) / 100.0f), 5.0f);
        const float markX = bar.getX() + bar.getWidth() * (float) threshold / 100.0f;
        g.setColour (theme.accent);
        g.fillRect (markX - 1.5f, bar.getY() - 4.0f, 3.0f, bar.getHeight() + 8.0f);
        g.setColour (theme.text);
        g.setFont (type::font (type::kLabel, true));
        g.drawText ("ENGINE CPU  " + juce::String (cpu) + " %", bar.toNearestInt().reduced (10, 0), juce::Justification::centredLeft);
        text ({ m.getX() + 4, 348, 260, kKey }, "WARN ABOVE", theme.textDim, type::kLabel, true, 1);
        g.setColour (theme.text);
        g.setFont (type::font (type::kTitle, true));
        g.drawText (juce::String (threshold) + " %", juce::Rectangle<int> (m.getX() + 150, 348, 140, kKey), juce::Justification::centredLeft);
        text ({ m.getX() + 4, 408, m.getWidth() - 8, 44 }, guard.describeBenchmark(), guard.getBenchmark() == nullptr ? theme.accent : theme.textDim, type::kCaption, false, 2);
        if (guard.getNotice().isNotEmpty())
            text ({ m.getX() + 4, 460, m.getWidth() - 8, 80 }, "LAST ADD: " + guard.getNotice(), theme.mute, type::kCaption, true, 5);
    }

    // CHANNEL LATENCY.
    {
        const auto r = rightRect();
        head (r, 0, "CHANNEL LATENCY", "OUT: left alone by ALIGNED");
        const auto list = listRect();
        g.saveState();
        g.reduceClipRegion (list);
        for (int i = rowTop; i < getNumRows() && i < rowTop + visibleRows() + 1; ++i)
        {
            const auto& row = rows[(size_t) i];
            const auto b = getRowBounds (i).reduced (0, 2).toFloat();
            g.setColour (theme.panel);
            g.fillRoundedRectangle (b, 6.0f);
            auto area = b.toNearestInt().withTrimmedRight (108);
            g.setColour (row.bypassed ? theme.textDim : theme.text);
            g.setFont (type::font (type::kLabel, true));
            g.drawFittedText (juce::String (row.channel + 1).paddedLeft ('0', 2) + (row.name.isNotEmpty() ? "  " + row.name : juce::String()), area.removeFromLeft (170).withTrimmedLeft (10),
                              juce::Justification::centredLeft, 1, 0.8f);
            g.setFont (type::font (type::kCaption));
            g.setColour (theme.textDim);
            g.drawText ("RACK " + juce::String (row.rack), area.removeFromLeft (92), juce::Justification::centredLeft);
            g.drawText ("DELAY " + juce::String (row.delay), area.removeFromLeft (100), juce::Justification::centredLeft);
            g.setColour (theme.text);
            g.drawText ("= " + juce::String (row.total) + " smp   " + juce::String (rate > 0.0 ? 1000.0 * row.total / rate : 0.0, 1) + " ms", area, juce::Justification::centredLeft);
            const auto key = getOptOutBounds (i).toFloat();
            g.setColour (row.optedOut ? theme.mute : theme.well);
            g.fillRoundedRectangle (key, 6.0f);
            g.setColour (row.optedOut ? theme.mute : theme.outline);
            g.drawRoundedRectangle (key.reduced (0.5f), 6.0f, 1.5f);
            g.setColour (row.optedOut ? theme.textOnAccent : theme.textDim);
            g.setFont (type::font (type::kLabel, true));
            g.drawText (row.optedOut ? "OUT" : "IN", key, juce::Justification::centred);
        }
        g.restoreState();
    }
}

//==============================================================================
void SystemPage::mouseDown (const juce::MouseEvent& e)
{
    dragStarted = listRect().contains (e.getPosition());
    dragging = false;
    dragFrom = e.y;
}

void SystemPage::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragStarted || (! dragging && std::abs (e.y - dragFrom) < kDragPixels))
        return;
    dragging = true;
    const int rowsMoved = (dragFrom - e.y) / kRow;
    if (rowsMoved == 0)
        return;
    dragFrom = e.y;
    scrollRows (rowTop + rowsMoved);
}

void SystemPage::mouseUp (const juce::MouseEvent& e)
{
    const bool wasDrag = dragging || e.mouseWasDraggedSinceMouseDown();
    const bool started = dragStarted;
    dragging = dragStarted = false;
    if (wasDrag || ! started)
        return;
    const int row = rowTop + (e.y - listRect().getY()) / kRow;
    if (juce::isPositiveAndBelow (row, getNumRows()) && getOptOutBounds (row).expanded (6, 2).contains (e.getPosition()))
        toggleOptOut (row);
}

void SystemPage::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! listRect().contains (e.getPosition()))
        return;
    scrollRows (rowTop + (wheel.deltaY > 0.0f ? -1 : wheel.deltaY < 0.0f ? 1 : 0));
}

} // namespace livedaw::ui::ph
