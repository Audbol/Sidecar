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
            expect (wait (panel, 600.0, [&] { return panel.getState() == BenchmarkPanel::State::Done; }));
            logMessage ("Quick run: " + juce::String ((juce::Time::getMillisecondCounterHiRes() - began) / 1000.0, 1) + " s");
            const auto& r = panel.getResult();
            expect (! r.cancelled);
            expect (r.measures.size() > 20);
            expect (! r.summary.isEmpty() && ! r.recommendations.isEmpty());
            const auto* wing = r.find ("Console", "WING", 0);
            // How many channels it holds depends on the machine (a sanitiser build is many times slower and may hold none): only that it was measured.
            expect (wing != nullptr && wing->value >= 0, "WING measured");
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
            // A sanitiser build is so slow that recording or the interface may score nothing; the Mark still stands.
            expect (r.score.valid() && r.score.estimate && r.score.dsp > 0 && r.score.effects >= 0 && r.score.recording >= 0 && r.score.interface >= 0, "Mark parts");
            logMessage ("Mark " + juce::String (r.score.mark, 0) + " (DSP " + juce::String (r.score.dsp, 0) + ", effects " + juce::String (r.score.effects, 0) + ", recording "
                        + juce::String (r.score.recording, 0) + ", interface " + juce::String (r.score.interface, 0) + ") raw " + juce::String (r.score.raw[0], 1) + " " + juce::String (r.score.raw[1], 1)
                        + " " + juce::String (r.score.raw[2], 1) + " " + juce::String (r.score.raw[3], 1));
            expectEquals (juce::roundToInt (panel.getSaved()[0].score.mark), juce::roundToInt (r.score.mark));
            expect (r.toText().contains ("SIDECAR MARK") && r.toCsv().contains ("Sidecar Mark"));
            const auto shots = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_UI_SNAPSHOTS", {});
            if (shots.isNotEmpty())
                snapshot (panel, "benchmark-score.png");
        }

        beginTest ("The Sidecar Mark rises with a faster result");
        {
            auto fake = [] (double channels, double effectMs, double tracks, double fps)
            {
                bench::Result f;
                f.settings.sampleRate = 48000.0;
                f.settings.blockSize = 128;
                bench::Measure c, e, rec, ui;
                c.group = "Console"; c.name = "WING"; c.blockSize = 128; c.value = channels;
                e.group = "Effect"; e.name = "Reverb"; e.blockSize = 128; e.meanMs = effectMs;
                rec.group = "Recording"; rec.name = "8 tracks"; rec.value = tracks;
                ui.group = "Interface"; ui.name = "Universal Mixer 1280x720"; ui.value = fps;
                for (auto& m : { c, e, rec, ui })
                    f.add (m);
                f.finish();
                return f;
            };
            const auto slow = fake (30, 0.2, 16, 30), fast = fake (120, 0.05, 64, 120);
            expect (slow.score.valid() && fast.score.valid());
            expect (fast.score.mark > slow.score.mark && fast.score.dsp > slow.score.dsp && fast.score.effects > slow.score.effects);
            expect (fast.score.recording > slow.score.recording && fast.score.interface > slow.score.interface);
            expect (! slow.score.estimate && slow.score.rating().isNotEmpty());
            const auto back = bench::Result::fromVar (juce::JSON::parse (juce::JSON::toString (fast.toVar())));
            expectEquals (juce::roundToInt (back.score.mark), juce::roundToInt (fast.score.mark));
        }

        beginTest ("A run can be cancelled; what finished is kept, and it is not saved");
        {
            panel.tests = { false, false, true, false, false, false };
            panel.quick = false; // long enough to still be going
            panel.start();
            wait (panel, 5.0, [&] { return panel.getLog().size() >= 2; });
            panel.cancel();
            expect (wait (panel, 300.0, [&] { return panel.getState() == BenchmarkPanel::State::Done; }));
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
