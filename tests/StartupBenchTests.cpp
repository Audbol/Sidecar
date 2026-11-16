// A repeatable measure of how long loading takes, headless. A category of its own, not run
// with the others:
//
//   build/LiveDAWTests_artefacts/Release/LiveDAWTests StartupBench
//
// It times, in milliseconds:
//   - the start-up up to the main component built (the engine, the show, the main component)
//     and the first full paint of it;
//   - switching the console through every one of the twelve consoles (the start-up tile picker's
//     "new console": engine, show and surface), three rounds, so the first (cold) round is told
//     apart from the warm ones;
//   - loading a large WING show (40 channels processed, an effect in each of the 16 slots,
//     100 scenes) from its file, and saving it;
//   - recalling a scene on the WING (100 scenes) and on the PLUGIN HOST (fake plug-ins).
//
// Each prints the mean and the worst case. Without a display nothing is measured on Linux.

#include "TestHelpers.h"
#include "engine/EventLog.h"
#include "engine/Models.h"
#include "engine/PluginHost.h"
#include "engine/Show.h"
#include "SampleProfiler.h"
#include "sandbox/TestPlugins.h"
#include "ui/LookAndFeel.h"
#include "ui/MainComponent.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

using namespace livedaw;

namespace
{
double ms (juce::int64 from) { return juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - from) * 1000.0; }

struct Stats
{
    void add (double v) { values.push_back (v); }
    double mean() const
    {
        double sum = 0;
        for (auto v : values)
            sum += v;
        return values.empty() ? 0.0 : sum / (double) values.size();
    }
    double worst() const { return values.empty() ? 0.0 : *std::max_element (values.begin(), values.end()); }
    std::vector<double> values;
};

/** The device, plug-in host and settings file a main component needs. */
struct StartRig
{
    StartRig() { host.getFormatManager().addFormat (std::make_unique<sandbox::TestPluginFormat>()); }
    ~StartRig() { settingsFile.deleteFile(); }

    PluginHost host { nullptr };
    EventLog log;
    juce::AudioDeviceManager devices;
    juce::File settingsFile = juce::File::createTempFile (".settings");
    juce::PropertiesFile settings { settingsFile, {} };
};

int fxModelFor (int slot, int pick)
{
    std::vector<int> allowed;
    for (int m = 0, n = ld::ld_model_count (models::Fx); m < n; ++m)
    {
        const auto token = takeString (ld::ld_model_text (models::Fx, m, 0));
        if (token != "NONE" && token != "EXT" && ld::ld_model_fx_allowed (m, slot))
            allowed.push_back (m);
    }
    return allowed.empty() ? -1 : allowed[(size_t) pick % allowed.size()];
}
} // namespace

class StartupBenchTests final : public juce::UnitTest
{
public:
    StartupBenchTests() : juce::UnitTest ("Start-up benchmark", "StartupBench") {}

    void runTest() override
    {
        if (! test::hasDisplay())
            return;

        // The program's window holds the graphics device (Direct2D on Windows) for as long as it runs; the
        // tests have no window, so without this one it would be made again each time the last picture goes.
        juce::Image keepGraphicsDevice (juce::Image::ARGB, 16, 16, true, juce::NativeImageType());

        beginTest ("Start-up to the main component");
        profiled ("startup", [this] { startUp(); });

        beginTest ("Switching the console");
        profiled ("switch", [this] { switching(); });

        beginTest ("Loading a large WING show");
        profiled ("load", [this] { largeShow(); });

        beginTest ("Recalling scenes");
        profiled ("recall", [this] { recalls(); });
    }

private:
    /** With STARTBENCH_PROFILE=<phase> the phase runs under the sampling profiler, which names where the time went. */
    void profiled (const char* phase, const std::function<void()>& run)
    {
        if (juce::SystemStats::getEnvironmentVariable ("STARTBENCH_PROFILE", {}) != phase)
        {
            run();
            return;
        }
        test::SampleProfiler profiler;
        profilerInUse = &profiler;
        run();
        profilerInUse = nullptr;
        profiler.report ([this] (const juce::String& line) { logMessage (line); }, 60);
    }

    test::SampleProfiler* profilerInUse = nullptr;

    void report (const juce::String& what, const Stats& s, const juce::String& unit = "ms")
    {
        logMessage ("STARTBENCH " + what.paddedRight (' ', 34) + " mean " + juce::String (s.mean(), 2) + " worst "
                    + juce::String (s.worst(), 2) + " " + unit + "  (n " + juce::String ((int) s.values.size()) + ")");
    }

    void startUp()
    {
        // A typical autosave: the WING as it starts, saved once and read at each start.
        const auto autosave = juce::File::createTempFile (".lshow");
        {
            StartRig rig;
            MixerEngine engine;
            Show show (engine, rig.host);
            show.saveToFile (autosave);
        }
        Stats hostTime, engineTime, showTime, autosaveTime, devicesTime, mainTime, paintTime, chooserTime, total;
        for (int round = 0; round < 6; ++round)
        {
            const auto tHost = juce::Time::getHighResolutionTicks();
            StartRig rig;
            const double tHostDone = ms (tHost);
            const auto t0 = juce::Time::getHighResolutionTicks();
            auto engine = std::make_unique<MixerEngine>();
            const double tEngine = ms (t0);
            const auto t1 = juce::Time::getHighResolutionTicks();
            auto show = std::make_unique<Show> (*engine, rig.host);
            const double tShow = ms (t1);
            const auto tLoad = juce::Time::getHighResolutionTicks();
            show->loadFromFile (autosave, true);
            const double tLoadDone = ms (tLoad);
            if (engine->getChannels().empty())
                engine->setNumChannels (engine->getConfiguredCounts().channels);
            const auto tDevices = juce::Time::getHighResolutionTicks();
            rig.devices.getAvailableDeviceTypes();
            const double tDevicesDone = ms (tDevices);
            const auto t2 = juce::Time::getHighResolutionTicks();
            auto main = std::make_unique<ui::MainComponent> (rig.devices, *engine, rig.host, *show, rig.log, rig.settings);
            const double tMain = ms (t2);
            const auto t3 = juce::Time::getHighResolutionTicks();
            juce::Image image (juce::Image::ARGB, 1680, 1000, true, juce::SoftwareImageType());
            {
                juce::Graphics g (image);
                main->paintEntireComponent (g, false);
            }
            const double tPaint = ms (t3);
            // The window the program opens with, as the program shows it: the choice of mixer over the console.
            const auto t4 = juce::Time::getHighResolutionTicks();
            main->setSize (1680, 1000);
            main->showMixerChooser (true, nullptr, nullptr);
            {
                juce::Graphics g (image);
                main->paintEntireComponent (g, false);
            }
            const double tChooser = ms (t4);
            // The first round is the cold one: fonts, pictures and tables are still to be made.
            if (round == 0)
                logMessage ("STARTBENCH cold start: plug-in host " + juce::String (tHostDone, 1) + " engine " + juce::String (tEngine, 1) + " show " + juce::String (tShow, 1)
                            + " autosave " + juce::String (tLoadDone, 1) + " device types " + juce::String (tDevicesDone, 1) + " main component "
                            + juce::String (tMain, 1) + " first paint " + juce::String (tPaint, 1) + " chooser " + juce::String (tChooser, 1) + " ms");
            else
            {
                hostTime.add (tHostDone);
                engineTime.add (tEngine);
                showTime.add (tShow);
                autosaveTime.add (tLoadDone);
                devicesTime.add (tDevicesDone);
                mainTime.add (tMain);
                paintTime.add (tPaint);
                chooserTime.add (tChooser);
            }
            total.add (tHostDone + tEngine + tShow + tLoadDone + tDevicesDone + tMain + tPaint + tChooser);
            main.reset();
            show.reset();
            engine.reset();
        }
        report ("warm plug-in host", hostTime);
        report ("warm engine", engineTime);
        report ("warm show", showTime);
        report ("warm autosave read", autosaveTime);
        report ("warm device types", devicesTime);
        report ("warm main component", mainTime);
        report ("warm first paint", paintTime);
        report ("warm chooser, built and painted", chooserTime);
        report ("start-up in all (cold + warm)", total);
        autosave.deleteFile();
    }

    void switching()
    {
        using ui::InterfaceKind;
        const std::vector<std::pair<InterfaceKind, const char*>> kinds {
            { InterfaceKind::Wing, "WING" },       { InterfaceKind::X32, "X32" },         { InterfaceKind::DLive, "dLive" },
            { InterfaceKind::Dm2000, "DM2000" },   { InterfaceKind::Universal, "Universal" }, { InterfaceKind::PluginHost, "PLUGIN HOST" },
            { InterfaceKind::ProMix, "ProMix 01" }, { InterfaceKind::Yamaha02R, "02R" },  { InterfaceKind::Sq5, "SQ-5" },
            { InterfaceKind::Yamaha01V, "01V" },   { InterfaceKind::Pro2, "PRO2" },       { InterfaceKind::Xenyx1202, "XENYX" } };
        StartRig rig;
        auto engine = std::make_unique<MixerEngine>();
        auto show = std::make_unique<Show> (*engine, rig.host);
        if (engine->getChannels().empty())
            engine->setNumChannels (engine->getConfiguredCounts().channels);
        auto main = std::make_unique<ui::MainComponent> (rig.devices, *engine, rig.host, *show, rig.log, rig.settings);
        main->setSize (1680, 1000);

        std::vector<Stats> perKind (kinds.size());
        Stats cold, warm, all;
        for (int round = 0; round < 3; ++round)
            for (size_t i = 0; i < kinds.size(); ++i)
            {
                // STARTBENCH_KIND=<name> samples that console's switches only.
                if (profilerInUse != nullptr)
                    profilerInUse->setPaused (juce::SystemStats::getEnvironmentVariable ("STARTBENCH_KIND", kinds[i].second) != kinds[i].second);
                const auto t0 = juce::Time::getHighResolutionTicks();
                main->startFresh (kinds[i].first);
                const double t = ms (t0);
                perKind[i].add (t);
                (round == 0 ? cold : warm).add (t);
                all.add (t);
            }
        for (size_t i = 0; i < kinds.size(); ++i)
            logMessage ("STARTBENCH switch to " + juce::String (kinds[i].second).paddedRight (' ', 22) + " cold " + juce::String (perKind[i].values[0], 1)
                        + " warm mean " + juce::String ((perKind[i].values[1] + perKind[i].values[2]) / 2.0, 1) + " ms");
        report ("switch, cold round", cold);
        report ("switch, warm rounds", warm);
        report ("switch, all", all);

        // The sequence of the brief: WING, X32, dLive, DM2000, Universal, PLUGIN HOST.
        if (profilerInUse != nullptr)
            profilerInUse->setPaused (true);
        Stats sequence;
        for (int round = 0; round < 3; ++round)
            for (size_t i = 0; i < 6; ++i)
            {
                const auto t0 = juce::Time::getHighResolutionTicks();
                main->startFresh (kinds[i].first);
                sequence.add (ms (t0));
            }
        report ("switch, six-console sequence (warm)", sequence);
        main.reset();
    }

    /** A WING with the channels processed, an effect in each slot and 100 scenes. */
    static void fill (MixerEngine& engine, Show& show)
    {
        engine.setNumChannels (40);
        auto& chs = engine.getChannels();
        for (size_t i = 0; i < chs.size(); ++i)
        {
            auto& s = *chs[i];
            s.setName ("Channel " + juce::String ((int) i + 1));
            s.setColourIndex ((int) (i % 11) + 1);
            s.params.inputLeft = (int) i;
            s.params.filter().lowCut = true;
            s.setModel (ModelBlock::Gate, (int) i % 8);
            s.params.gate().on = true;
            s.params.eq().on = true;
            s.setModel (ModelBlock::Dynamics, (int) i % 8);
            s.params.dynamics().on = true;
            s.params.faderDb = -3.0f - (float) (i % 7);
        }
        for (int slot = 0; slot < engine.getFx().getNumSlots(); ++slot)
            if (const int m = fxModelFor (slot, slot * 3 + 1); m >= 0)
                engine.getFx().setModel (slot, m);
        for (int n = 0; n < 100; ++n)
        {
            for (size_t i = 0; i < chs.size(); ++i)
                chs[i]->params.faderDb = -60.0f + (float) ((n * 7 + (int) i * 3) % 60);
            chs[(size_t) (n % 40)]->params.mute = (n % 2) == 0;
            show.addScene ("Scene " + juce::String (n + 1), -1);
        }
    }

    void largeShow()
    {
        StartRig rig;
        const auto file = juce::File::createTempFile (".lshow");
        {
            MixerEngine engine (0);
            Show show (engine, rig.host);
            fill (engine, show);
            Stats save;
            for (int i = 0; i < 5; ++i)
            {
                const auto t0 = juce::Time::getHighResolutionTicks();
                expect (show.saveToFile (file).wasOk());
                save.add (ms (t0));
            }
            report ("save large show", save);
            logMessage ("STARTBENCH large show file: " + juce::String (file.getSize() / 1024) + " KB, " + juce::String (show.getNumScenes()) + " scenes");
        }
        Stats load;
        for (int i = 0; i < 6; ++i)
        {
            MixerEngine engine (0);
            Show show (engine, rig.host);
            const auto t0 = juce::Time::getHighResolutionTicks();
            const auto result = show.loadFromFile (file, true);
            const double t = ms (t0);
            expect (result.wasOk(), result.getErrorMessage());
            expectEquals (show.getNumScenes(), 100);
            if (i == 0)
                logMessage ("STARTBENCH load large show, first (cold) " + juce::String (t, 1) + " ms");
            else
                load.add (t);
        }
        report ("load large show (warm)", load);
        file.deleteFile();
    }

    void recalls()
    {
        StartRig rig;
        {
            MixerEngine engine (0);
            Show show (engine, rig.host);
            fill (engine, show);
            Stats recall;
            for (int pass = 0; pass < 2; ++pass)
                for (int n = 0; n < show.getNumScenes(); ++n)
                {
                    const auto t0 = juce::Time::getHighResolutionTicks();
                    show.recallScene (n);
                    recall.add (ms (t0));
                }
            report ("recall scene, WING (100 scenes)", recall);
        }
        {
            MixerEngine engine (0);
            engine.configurePluginHost (16);
            Show show (engine, rig.host);
            for (int slot = 0; slot < 16; ++slot)
                engine.getFx().setPlugin (slot, sandbox::TestPluginFormat::create (sandbox::TestPluginFormat::Kind::gain));
            auto& chs = engine.getChannels();
            for (int n = 0; n < 20; ++n)
            {
                for (size_t i = 0; i < chs.size(); ++i)
                    chs[i]->params.faderDb = -40.0f + (float) ((n * 5 + (int) i * 3) % 40);
                for (int slot = 0; slot < 16; ++slot)
                    if (auto* p = engine.getFx().getPlugin (slot); p != nullptr && ! p->getParameters().isEmpty())
                        p->getParameters()[0]->setValue ((float) ((n + slot) % 10) / 10.0f);
                show.addScene ("Scene " + juce::String (n + 1), -1);
            }
            Stats recall;
            for (int pass = 0; pass < 3; ++pass)
                for (int n = 0; n < show.getNumScenes(); ++n)
                {
                    const auto t0 = juce::Time::getHighResolutionTicks();
                    show.recallScene (n);
                    recall.add (ms (t0));
                }
            report ("recall scene, PLUGIN HOST (16 plug-ins)", recall);
        }

        // The same with the program's window over the console, which redraws its screens on each recall.
        {
            auto engine = std::make_unique<MixerEngine>();
            auto show = std::make_unique<Show> (*engine, rig.host);
            auto main = std::make_unique<ui::MainComponent> (rig.devices, *engine, rig.host, *show, rig.log, rig.settings);
            main->setSize (1680, 1000);
            fill (*engine, *show);
            Stats recall;
            for (int pass = 0; pass < 2; ++pass)
                for (int n = 0; n < show->getNumScenes(); ++n)
                {
                    const auto t0 = juce::Time::getHighResolutionTicks();
                    show->recallScene (n);
                    recall.add (ms (t0));
                }
            report ("recall scene, WING with its surface", recall);
            main.reset();
        }
        {
            auto engine = std::make_unique<MixerEngine>();
            auto show = std::make_unique<Show> (*engine, rig.host);
            auto main = std::make_unique<ui::MainComponent> (rig.devices, *engine, rig.host, *show, rig.log, rig.settings);
            main->setSize (1680, 1000);
            main->startFresh (ui::InterfaceKind::PluginHost, 16);
            for (int slot = 0; slot < 16; ++slot)
                engine->getFx().setPlugin (slot, sandbox::TestPluginFormat::create (sandbox::TestPluginFormat::Kind::gain));
            auto& chs = engine->getChannels();
            for (int n = 0; n < 20; ++n)
            {
                for (size_t i = 0; i < chs.size(); ++i)
                    chs[i]->params.faderDb = -40.0f + (float) ((n * 5 + (int) i * 3) % 40);
                show->addScene ("Scene " + juce::String (n + 1), -1);
            }
            Stats recall;
            for (int pass = 0; pass < 3; ++pass)
                for (int n = 0; n < show->getNumScenes(); ++n)
                {
                    const auto t0 = juce::Time::getHighResolutionTicks();
                    show->recallScene (n);
                    recall.add (ms (t0));
                }
            report ("recall scene, PLUGIN HOST with its surface", recall);
            main.reset();
        }
    }
};

static StartupBenchTests startupBenchTests;
