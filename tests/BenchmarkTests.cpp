#include "TestHelpers.h"
#include "engine/Benchmark.h"
#include "engine/EventLog.h"
#include "engine/PluginHost.h"
#include "engine/Show.h"
#include "ui/universal/BenchmarkPanel.h"

using namespace livedaw;
using namespace livedaw::ui::universal;

/** The built-in benchmark (engine/Benchmark.h, SETUP > BENCHMARK): a tiny run finishes with a
    summary and numbers, can be cancelled, and its runs save, load and export. The runs are
    a few blocks long so that the suite stays fast. */
class BenchmarkTests final : public juce::UnitTest
{
public:
    BenchmarkTests() : juce::UnitTest ("Benchmark", "LiveDAW") {}

    static void snapshot (juce::Component& c, const juce::String& name)
    {
        const auto folder = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_UI_SNAPSHOTS", {});
        if (folder.isEmpty() || ! juce::File (folder).createDirectory())
            return;
        const auto file = juce::File (folder).getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (out.openedOk())
            juce::PNGImageFormat().writeImageToStream (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f), out);
    }

    /** Polls the panel until `until` is true or the time is up. */
    template <typename Fn>
    bool wait (BenchmarkPanel& panel, double seconds, Fn until)
    {
        const auto end = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
        while (juce::Time::getMillisecondCounterHiRes() < end)
        {
            panel.tick();
            if (until())
                return true;
            juce::Thread::sleep (15);
        }
        return false;
    }

    void runTest() override
    {
        MixerEngine engine (0);
        PluginHost host { nullptr };
        Show show (engine, host);
        EventLog log;
        juce::AudioDeviceManager devices;
        const auto settingsFile = juce::File::createTempFile (".settings");
        juce::PropertiesFile settings { settingsFile, {} };
        ui::MeterBank meters (engine);
        ui::UiContext ctx (engine, host, show, log, devices, settings, meters);
        const auto folder = juce::File::createTempFile ("benchruns");

        auto panelOwner = std::make_unique<BenchmarkPanel> (ctx, Theme::charcoal());
        auto& panel = *panelOwner;
        panel.setSize (1200, 760);
        panel.runsFolder = folder;
        panel.quick = true;
        panel.tests[4] = false;
        panel.onlyProfiles = { ld::kProfileWing, ld::kProfileX32 };

        beginTest ("A short run finishes with a summary, numbers and a saved result");
        {
            const auto began = juce::Time::getMillisecondCounterHiRes();
            panel.start();
            expect (panel.getState() == BenchmarkPanel::State::Running);
            wait (panel, 10.0, [&] { return panel.getLog().size() >= 6 || panel.getState() == BenchmarkPanel::State::Done; });
            snapshot (panel, "benchmark-running.png");
            expect (wait (panel, 40.0, [&] { return panel.getState() == BenchmarkPanel::State::Done; }));
            logMessage ("Quick run: " + juce::String ((juce::Time::getMillisecondCounterHiRes() - began) / 1000.0, 1) + " s");
            const auto& r = panel.getResult();
            expect (! r.cancelled);
            expect (r.measures.size() > 20);
            expect (! r.summary.isEmpty() && ! r.recommendations.isEmpty());
            const auto* wing = r.find ("Console", "WING", 0);
            expect (wing != nullptr && wing->value >= 8);
            expect (r.find ("Interface", "Universal Mixer 1280x720", 0) != nullptr);
            expect (r.find ("Recording", "8 tracks", 0) != nullptr);
            expect (r.summary.joinIntoString ("\n").contains ("channels of WING processing"));
            for (const auto& m : r.measures)
                expect (m.meanPct >= 0 && std::isfinite (m.p99Pct) && std::isfinite (m.worstPct), m.key());
            expectEquals ((int) panel.getSaved().size(), 1);
            expectEquals ((int) panel.getSaved()[0].measures.size(), (int) r.measures.size());
            expectEquals (panel.getSaved()[0].summary.joinIntoString ("\n"), r.summary.joinIntoString ("\n"));
            expect (r.toText().contains ("RECOMMENDATIONS") && r.toCsv().startsWith ("group,name"));
            const auto out = juce::File::createTempFile (".csv");
            expect (panel.exportTo (out, true) && out.loadFileAsString().contains ("WING"));
            out.deleteFile();
            snapshot (panel, "benchmark-results.png");
        }

        beginTest ("A run can be cancelled; what finished is kept, and it is not saved");
        {
            panel.tests = { false, false, true, false, false, false };
            panel.quick = false; // long enough to still be going
            panel.start();
            wait (panel, 5.0, [&] { return panel.getLog().size() >= 2; });
            panel.cancel();
            expect (wait (panel, 20.0, [&] { return panel.getState() == BenchmarkPanel::State::Done; }));
            expect (panel.getResult().cancelled);
            expect (panel.getResult().summary[0].contains ("cancelled"));
            expectEquals ((int) panel.getSaved().size(), 1);
        }

        if (juce::SystemStats::getEnvironmentVariable ("LIVEDAW_BENCH_FULL", {}).isNotEmpty())
        {
            beginTest ("A full run (only when asked for)");
            panel.tests = { true, true, true, true, false, true };
            panel.quick = false;
            panel.onlyProfiles.clear();
            const auto began = juce::Time::getMillisecondCounterHiRes();
            panel.start();
            wait (panel, 1200.0, [&] { return panel.getState() == BenchmarkPanel::State::Done; });
            logMessage ("Full run: " + juce::String ((juce::Time::getMillisecondCounterHiRes() - began) / 1000.0, 0) + " s, " + juce::String ((int) panel.getResult().measures.size()) + " measures");
            juce::File (juce::SystemStats::getEnvironmentVariable ("LIVEDAW_BENCH_FULL", {})).replaceWithText (panel.getResult().toText());
        }

        panelOwner.reset();
        folder.deleteRecursively();
        settingsFile.deleteFile();
    }
};

static BenchmarkTests benchmarkTests;
