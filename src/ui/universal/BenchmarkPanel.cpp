#include "ui/universal/BenchmarkPanel.h"

#include "daw/Session.h"
#include "engine/AppPaths.h"
#include "engine/Models.h"
#include "ui/universal/MixerView.h"

#include <algorithm>

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kLeft = 250, kKeyH = 32, kRowH = 20;
    juce::String pct (double v) { return juce::String (v, v < 10 ? 2 : 1) + "%"; }
}

/** The results: the summary, the recommendations and the tables, drawn row by row. */
class BenchmarkPanel::Body final : public juce::Component
{
public:
    struct Row
    {
        int kind = 0; // 0 heading, 1 paragraph, 2 table head, 3 table row
        juce::StringArray cols;
    };
    explicit Body (const Theme& t) : theme (t) {}

    void set (const bench::Result& r, const bench::Result* other)
    {
        rows.clear();
        auto head = [&] (const juce::String& s) { rows.push_back ({ 0, { s } }); };
        head ("SUMMARY");
        for (const auto& s : r.summary)
            rows.push_back ({ 1, { s } });
        head ("RECOMMENDATIONS");
        for (const auto& s : r.recommendations)
            rows.push_back ({ 1, { s } });
        juce::String group;
        for (const auto& m : r.measures)
        {
            if (m.group != group)
            {
                head (group = m.group);
                rows.push_back ({ 2, { "TEST", "BUFFER", "MEAN", "99TH", "WORST", "VALUE", other != nullptr ? "99TH, " + other->when.formatted ("%d %b %H:%M") : juce::String(), "NOTE" } });
            }
            juce::String o;
            if (other != nullptr)
                if (auto* om = other->find (m.group, m.name, m.blockSize))
                    o = pct (om->p99Pct);
            rows.push_back ({ 3, { m.name, m.blockSize > 0 ? juce::String (m.blockSize) : juce::String(), pct (m.meanPct), pct (m.p99Pct), pct (m.worstPct),
                                   m.value > 0 ? juce::String (juce::roundToInt (m.value)) : juce::String(), o, m.note } });
        }
        int h = 8;
        for (auto& row : rows)
            h += heightOf (row);
        setSize (juce::jmax (600, getParentWidth() - 16), h);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        int y = 4;
        const int w = getWidth();
        const int x[] = { 8, 300, 370, 440, 510, 580, 660, 790 };
        for (auto& row : rows)
        {
            const int h = heightOf (row);
            if (row.kind == 0)
            {
                g.setColour (theme.accent);
                g.setFont (type::font (type::kHeading, true));
                g.drawText (row.cols[0], 8, y + 6, w - 16, 20, juce::Justification::centredLeft);
            }
            else if (row.kind == 1)
            {
                g.setColour (theme.text);
                g.setFont (type::font (type::kLabel));
                g.drawFittedText (row.cols[0], 8, y, w - 16, h, juce::Justification::centredLeft, 2);
            }
            else
            {
                g.setColour (row.kind == 2 ? theme.textDim : theme.text);
                g.setFont (type::font (type::kCaption, row.kind == 2));
                for (int c = 0; c < 8; ++c)
                {
                    const int right = c < 7 ? x[c + 1] - 6 : w - 8;
                    g.drawText (row.cols[(int) c], x[c], y, right - x[c], h, c >= 1 && c <= 6 ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
                }
                if (row.kind == 3 && row.cols[3].getDoubleValue() > 70.0)
                {
                    g.setColour (theme.mute.withAlpha (0.25f));
                    g.fillRect (0, y, w, h);
                }
            }
            y += h;
        }
    }

    static int heightOf (const Row& r) { return r.kind == 0 ? 30 : r.kind == 1 ? 38 : kRowH; }
    std::vector<Row> rows;

private:
    const Theme& theme;
};

BenchmarkPanel::BenchmarkPanel (UiContext& c, const Theme& t)
    : ctx (c), theme (t), startKey ("START", t.accent, t), cancelKey ("CANCEL", t.mute, t), quickKey ("QUICK TEST", t.accent, t), textKey ("EXPORT TEXT", t.accent, t),
      csvKey ("EXPORT CSV", t.accent, t), compareKey ("COMPARE", t.accent, t), logKey ("LOG", t.accent, t)
{
    startKey.onClick = [this] { start(); };
    cancelKey.onClick = [this] { cancel(); };
    quickKey.isOn = [this] { return quick; };
    quickKey.onClick = [this] { quick = ! quick; };
    textKey.onClick = [this] { chooseExport (false); };
    csvKey.onClick = [this] { chooseExport (true); };
    compareKey.onClick = [this]
    {
        // Cycles through the saved runs before the shown one, then back to none.
        const int last = (int) saved.size() - (state == State::Done ? 2 : 1);
        setCompare (compareIndex >= last ? -1 : compareIndex + 1);
    };
    logKey.isOn = [this] { return showLog; };
    logKey.onClick = [this] { showLog = ! showLog; showBody(); repaint(); };
    const juce::String testNames[] = { "BLOCKS", "EFFECTS", "CONSOLES", "RECORDING", "PLUG-INS (RISKY)", "INTERFACE" };
    for (size_t i = 0; i < 6; ++i)
    {
        sizeKeys[i] = std::make_unique<ToggleKey> (juce::String (kSizes[i]), t.accent, t);
        sizeKeys[i]->isOn = [this, i] { return bufferSizes[i]; };
        sizeKeys[i]->onClick = [this, i] { bufferSizes[i] = ! bufferSizes[i]; };
        testKeys[i] = std::make_unique<ToggleKey> (testNames[i], t.accent, t);
        testKeys[i]->isOn = [this, i] { return tests[i]; };
        testKeys[i]->onClick = [this, i] { tests[i] = ! tests[i]; };
        addAndMakeVisible (*sizeKeys[i]);
        addAndMakeVisible (*testKeys[i]);
    }
    for (auto* k : { &startKey, &cancelKey, &quickKey, &textKey, &csvKey, &compareKey, &logKey })
        addAndMakeVisible (k);
    body = std::make_unique<Body> (t);
    viewport.setViewedComponent (body.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    reloadSaved();
    refreshKeys();
    startTimerHz (15);
}

BenchmarkPanel::~BenchmarkPanel()
{
    stopTimer();
    runner.cancel();
}

void BenchmarkPanel::reloadSaved()
{
    saved = bench::loadAll (runsFolder);
}

void BenchmarkPanel::setCompare (int index)
{
    compareIndex = index >= 0 && index < (int) saved.size() ? index : -1;
    showBody();
    refreshKeys();
}

bool BenchmarkPanel::exportTo (const juce::File& f, bool csv) const
{
    return f.replaceWithText (csv ? result.toCsv() : result.toText());
}

void BenchmarkPanel::chooseExport (bool csv)
{
    if (result.measures.empty())
        return;
    chooser = std::make_unique<juce::FileChooser> ("Export the benchmark", paths::documents().getChildFile (csv ? "Sidecar-benchmark.csv" : "Sidecar-benchmark.txt"), csv ? "*.csv" : "*.txt");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, csv] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult() != juce::File())
                                  note = exportTo (fc.getResult(), csv) ? "Saved " + fc.getResult().getFileName() : juce::String ("Could not save that file");
                              repaint();
                          });
}

bench::Settings BenchmarkPanel::buildSettings() const
{
    bench::Settings s;
    s.sampleRate = 48000.0;
    s.blockSize = 128;
    if (auto* device = ctx.deviceManager.getCurrentAudioDevice())
    {
        s.sampleRate = device->getCurrentSampleRate();
        s.blockSize = device->getCurrentBufferSizeSamples();
        s.deviceInfo = device->getTypeName() + ": " + device->getName() + ", " + juce::String (juce::roundToInt (s.sampleRate)) + " Hz, " + juce::String (s.blockSize) + " samples, "
                       + juce::String (device->getActiveInputChannels().countNumberOfSetBits()) + " in / " + juce::String (device->getActiveOutputChannels().countNumberOfSetBits()) + " out";
    }
    for (size_t i = 0; i < 6; ++i)
        if (bufferSizes[i] && kSizes[i] != s.blockSize)
            s.extraSizes.push_back (kSizes[i]);
    s.currentProfile = ctx.engine.getProfile();
    s.restoreShownProfile = s.currentProfile;
    s.profiles = onlyProfiles;
    s.doBlocks = tests[0];
    s.doEffects = tests[1];
    s.doConsoles = tests[2];
    s.doRecording = tests[3];
    s.doPlugins = tests[4];
    s.doInterface = tests[5];
    s.recordFolder = daw::Session::getDefaultFolder();
    if (quick)
    {
        s.blocks = 30;
        s.warmup = 4;
        s.modelSamples = 1;
        s.maxChannels = 32;
        s.recordSeconds = 0.3;
        s.recordMaxTracks = 16;
        s.extraSizes.resize (juce::jmin<size_t> (s.extraSizes.size(), 1));
    }
    return s;
}

void BenchmarkPanel::start()
{
    if (state == State::Running || state == State::Interface)
        return;
    auto s = buildSettings();
    // The model tables of the console shown are built now, so that nothing builds them while the
    // benchmark thread has the other consoles' models selected.
    for (int t = 0; t < 5; ++t)
    {
        models::table ((models::Table) t);
        models::tableAll ((models::Table) t);
    }
    pluginInstances.clear();
    if (s.doPlugins)
        for (int slot = 0, n = ctx.engine.getFx().getNumSlots(); slot < n && pluginInstances.size() < 8; ++slot)
            if (auto* p = ctx.engine.getFx().getPlugin (slot))
            {
                juce::String error;
                if (auto copy = ctx.host.createInstance (p->getPluginDescription(), s.sampleRate, s.blockSize, error))
                {
                    s.plugins.push_back ({ copy->getName(), copy.get() });
                    pluginInstances.push_back (std::move (copy));
                }
            }
    log.clear();
    graph.clear();
    result = {};
    note = {};
    compareIndex = -1;
    showLog = true;
    state = State::Running;
    runner.start (s);
    showBody();
    refreshKeys();
}

void BenchmarkPanel::cancel()
{
    if (state == State::Running)
    {
        runner.cancel();
        test = "Cancelling";
    }
    else if (state == State::Interface)
        uiPhase = 99;
}

void BenchmarkPanel::tick()
{
    if (state == State::Running)
    {
        auto live = runner.poll();
        test = live.test;
        progress = live.progress;
        budgetMs = live.budgetMs;
        graph = std::move (live.blockMs);
        for (auto& l : live.newLog)
            log.add (l);
        while (log.size() > 2000)
            log.remove (0);
        if (runner.isDone() && ! runner.isRunning())
        {
            result = runner.getResult();
            if (result.settings.doInterface && ! result.cancelled)
            {
                state = State::Interface;
                uiPhase = 0;
                uiFrame = 0;
                uiTimes.clear();
                uiTarget.reset();
            }
            else
                finishRun();
        }
        repaint();
    }
    else if (state == State::Interface)
    {
        runInterfaceStep();
        repaint();
    }
}

void BenchmarkPanel::runInterfaceStep()
{
    const int frames = quick ? 6 : 60;
    if (uiPhase >= 2)
    {
        finishRun();
        return;
    }
    if (uiTarget == nullptr)
    {
        if (uiPhase == 0)
        {
            auto view = std::make_unique<MixerView> (ctx, theme);
            view->setSize (1280, 720);
            uiTarget = std::move (view);
        }
        test = uiPhase == 0 ? "Interface: the Universal Mixer" : "Interface: the current surface";
        log.add (test);
    }
    juce::Component* target = uiPhase == 0 ? uiTarget.get() : getTopLevelComponent();
    if (target == nullptr || target->getWidth() < 1 || target->getHeight() < 1)
        uiFrame = frames;
    // A few frames a tick keeps the page answering; each is a full repaint, as when every meter moves.
    for (int i = 0; i < 4 && uiFrame < frames; ++i, ++uiFrame)
    {
        juce::Image image (juce::Image::ARGB, target->getWidth(), target->getHeight(), true, juce::SoftwareImageType());
        const auto t0 = juce::Time::getHighResolutionTicks();
        if (uiPhase == 0)
            static_cast<MixerView*> (target)->refresh();
        juce::Graphics g (image);
        target->paintEntireComponent (g, false);
        uiTimes.push_back (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1000.0);
    }
    progress = 0.9 + 0.1 * (uiPhase * frames + uiFrame) / (2.0 * frames);
    if (uiFrame >= frames)
    {
        if (! uiTimes.empty())
        {
            std::sort (uiTimes.begin(), uiTimes.end());
            double sum = 0;
            for (auto v : uiTimes)
                sum += v;
            const double mean = sum / (double) uiTimes.size(), p99 = uiTimes[(size_t) juce::jmin ((int) uiTimes.size() - 1, (int) (0.99 * (double) uiTimes.size()))];
            bench::Measure m;
            m.group = "Interface";
            m.name = juce::String (uiPhase == 0 ? "Universal Mixer " : "Current surface ") + juce::String (target->getWidth()) + "x" + juce::String (target->getHeight());
            m.meanMs = mean;
            m.meanPct = mean / (1000.0 / 60.0) * 100.0; // of a 60 fps frame
            m.p99Pct = p99 / (1000.0 / 60.0) * 100.0;
            m.worstPct = uiTimes.back() / (1000.0 / 60.0) * 100.0;
            m.value = 1000.0 / juce::jmax (0.01, mean);
            m.note = "frames per second; figures are % of a 60 fps frame";
            result.add (m);
            log.add (m.name + ": " + juce::String (m.value, 0) + " frames per second");
        }
        uiTimes.clear();
        uiFrame = 0;
        ++uiPhase;
        uiTarget.reset();
    }
}

void BenchmarkPanel::finishRun()
{
    pluginInstances.clear();
    result.finish();
    if (! result.measures.empty() && ! result.cancelled)
        if (bench::save (result, runsFolder) == juce::File())
            note = "The run could not be saved";
    reloadSaved();
    state = State::Done;
    progress = 1.0;
    test = result.cancelled ? "Cancelled" : "Finished";
    showLog = false;
    showBody();
    refreshKeys();
    repaint();
}

void BenchmarkPanel::showBody()
{
    viewport.setVisible (! showLog && state == State::Done);
    if (state == State::Done)
    {
        // The run just saved is the last one in `saved`; comparing is with another.
        const bench::Result* other = compareIndex >= 0 && compareIndex < (int) saved.size() ? &saved[(size_t) compareIndex] : nullptr;
        body->set (result, other);
    }
}

void BenchmarkPanel::refreshKeys()
{
    const bool busy = state == State::Running || state == State::Interface;
    startKey.setEnabled (! busy);
    cancelKey.setEnabled (busy);
    for (size_t i = 0; i < 6; ++i)
    {
        sizeKeys[i]->setEnabled (! busy);
        testKeys[i]->setEnabled (! busy);
    }
    quickKey.setEnabled (! busy);
    textKey.setEnabled (state == State::Done);
    csvKey.setEnabled (state == State::Done);
    compareKey.setEnabled (state == State::Done && saved.size() > 1);
    compareKey.setButtonText (compareIndex >= 0 && compareIndex < (int) saved.size() ? "VS " + saved[(size_t) compareIndex].when.formatted ("%d %b %H:%M") : juce::String ("COMPARE"));
    for (auto* k : { &startKey, &cancelKey, &quickKey, &textKey, &csvKey, &compareKey, &logKey })
        k->refresh();
    for (size_t i = 0; i < 6; ++i)
    {
        sizeKeys[i]->refresh();
        testKeys[i]->refresh();
    }
}

void BenchmarkPanel::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap);
    auto left = area.removeFromLeft (kLeft);
    area.removeFromLeft (sizes::kGap);
    auto key = [&] (juce::Component& c) { c.setBounds (left.removeFromTop (kKeyH)); left.removeFromTop (sizes::kGapSmall); };
    key (startKey);
    key (cancelKey);
    key (quickKey);
    left.removeFromTop (20); // BUFFER SIZES
    for (size_t row = 0; row < 3; ++row)
    {
        auto r = left.removeFromTop (kKeyH);
        sizeKeys[row * 2]->setBounds (r.removeFromLeft (r.getWidth() / 2 - 2));
        sizeKeys[row * 2 + 1]->setBounds (r.withTrimmedLeft (4));
        left.removeFromTop (sizes::kGapSmall);
    }
    left.removeFromTop (20); // TESTS
    for (auto& k : testKeys)
        key (*k);
    left.removeFromTop (sizes::kGap);
    key (textKey);
    key (csvKey);
    key (compareKey);
    key (logKey);
    // The header (machine, test, progress), the graph, then the log or the results.
    area.removeFromTop (kRowH * 4 + 44 + 8);
    area.removeFromTop (150 + sizes::kGap);
    viewport.setBounds (area);
    if (body != nullptr)
        body->setSize (juce::jmax (600, area.getWidth() - 16), body->getHeight());
}

void BenchmarkPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    auto area = getLocalBounds().reduced (sizes::kGap);
    auto left = area.removeFromLeft (kLeft);
    area.removeFromLeft (sizes::kGap);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption, true));
    g.drawText ("ALSO TEST THESE BUFFER SIZES", left.getX(), sizeKeys[0]->getY() - 18, kLeft, 16, juce::Justification::centredLeft);
    g.drawText ("TESTS (NO SOUND IS MADE)", left.getX(), testKeys[0]->getY() - 18, kLeft, 16, juce::Justification::centredLeft, true);

    // The machine, from the result if there is one.
    const auto machine = result.machine.size() > 0 ? result.machine : bench::machineDetails (buildSettings().deviceInfo);
    g.setFont (type::font (type::kCaption));
    auto head = area.removeFromTop (kRowH * 4);
    for (int i = 0; i < machine.size() && i < 6; ++i)
    {
        const bool second = i >= 3;
        auto cell = head.withTrimmedLeft (second ? head.getWidth() / 2 : 0).withWidth (head.getWidth() / 2).withY (head.getY() + (i % 3) * (kRowH * 4 / 3)).withHeight (kRowH);
        g.setColour (theme.textDim);
        g.drawText (machine.getAllKeys()[i], cell.removeFromLeft (92), juce::Justification::centredLeft);
        g.setColour (theme.text);
        g.drawText (machine.getAllValues()[i], cell, juce::Justification::centredLeft, true);
    }
    area.removeFromTop (4);

    // The test, and the progress.
    auto status = area.removeFromTop (44);
    g.setColour (theme.text);
    g.setFont (type::font (type::kValue, true));
    const auto title = state == State::Idle ? juce::String ("Ready. START runs the tests below; nothing makes a sound.") : test;
    g.drawText (title + (note.isNotEmpty() ? "   (" + note + ")" : juce::String()), status.removeFromTop (22), juce::Justification::centredLeft, true);
    auto bar = status.removeFromTop (16).toFloat();
    g.setColour (theme.well);
    g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (theme.accent);
    g.fillRoundedRectangle (bar.withWidth ((float) (bar.getWidth() * progress)), 3.0f);
    g.setColour (theme.text);
    g.setFont (type::font (type::kMicro));
    g.drawText (juce::String (juce::roundToInt (progress * 100)) + "%", bar, juce::Justification::centred);
    area.removeFromTop (sizes::kGap);

    // The graph: each block's time against the real-time budget.
    auto plot = area.removeFromTop (150).toFloat();
    area.removeFromTop (sizes::kGap);
    g.setColour (theme.well);
    g.fillRect (plot);
    const double budget = budgetMs > 0 ? budgetMs : 1.0;
    float top = (float) budget * 1.25f;
    for (auto v : graph)
        top = juce::jmax (top, v * 1.05f);
    auto yOf = [&] (double ms) { return plot.getBottom() - (float) (ms / top) * plot.getHeight(); };
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kMicro));
    if (budgetMs > 0)
    {
        g.setColour (theme.mute);
        g.drawHorizontalLine ((int) yOf (budget), plot.getX(), plot.getRight());
        g.drawText ("budget " + juce::String (budget, 2) + " ms (100%)", plot.getX() + 4, (int) yOf (budget) - 14, 220, 12, juce::Justification::centredLeft);
        g.setColour (theme.meterMid);
        g.drawHorizontalLine ((int) yOf (budget * 0.7), plot.getX(), plot.getRight());
        g.drawText ("70%", plot.getX() + 4, (int) yOf (budget * 0.7) - 14, 60, 12, juce::Justification::centredLeft);
    }
    if (graph.size() > 1)
    {
        juce::Path p;
        for (size_t i = 0; i < graph.size(); ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) (bench::Runner::kGraphLength - 1);
            i == 0 ? p.startNewSubPath (x, yOf (graph[i])) : p.lineTo (x, yOf (graph[i]));
        }
        g.setColour (theme.accent);
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }
    else
    {
        g.setColour (theme.textDim);
        g.drawText ("Block times appear here while a test runs", plot, juce::Justification::centred);
    }
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kMicro));
    g.drawText ("block time (ms)  max " + juce::String (top, 2), plot.toNearestInt().withTrimmedLeft (plot.getWidth() > 400 ? (int) plot.getWidth() - 200 : 0).withHeight (14), juce::Justification::centredRight);

    // The log, while it runs or on LOG.
    if (viewport.isVisible())
        return;
    g.setColour (theme.text);
    g.setFont (type::font (type::kCaption));
    const int lines = juce::jmax (1, area.getHeight() / 15);
    for (int i = 0; i < lines; ++i)
    {
        const int idx = juce::jmax (0, log.size() - lines) + i;
        if (idx < log.size())
            g.drawText (log[idx], area.getX(), area.getY() + i * 15, area.getWidth(), 15, juce::Justification::centredLeft, true);
    }
    if (log.isEmpty())
    {
        g.setColour (theme.textDim);
        g.drawText ("Every measurement is listed here as it is made.", area, juce::Justification::centred);
    }
}

} // namespace livedaw::ui::universal
