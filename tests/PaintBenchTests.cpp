// A repeatable measure of how much the interface costs to draw, headless.
// A category of its own, not run with the others:
//
//   build/LiveDAWTests_artefacts/Release/LiveDAWTests PaintBench
//
// For each of six views at 1920x1080 it times a full repaint (the window's whole area into a
// software image, as the Raspberry Pi draws) and a meter-only update: the strips' peaks move
// every 30 ms while the views' own timers run, the areas JUCE is asked to repaint are
// collected and only those are painted. It prints the mean and 95th percentile in ms for
// both, the area a meter tick repaints (the share of the window) and the time the timers'
// handlers took per tick. Without a display nothing is measured on Linux.

#include "TestHelpers.h"
#include "engine/EventLog.h"
#include "engine/PluginHost.h"
#include "engine/Show.h"
#include "ui/DLiveC3500Surface.h"
#include "ui/Dm2000Surface.h"
#include "ui/LookAndFeel.h"
#include "ui/PluginHostSurface.h"
#include "ui/UniversalSkin.h"
#include "ui/WingSurface.h"
#include "daw/Session.h"
#include "ui/X32Surface.h"
#include "ui/universal/MixerView.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <cstdlib>
#include <typeinfo>
#include <vector>

using namespace livedaw;

namespace
{
/** A UI context over an engine (with a settings file of its own). */
struct BenchRig
{
    explicit BenchRig (MixerEngine& e) : engine (e), show (e, host), meters (e), ctx (e, host, show, log, devices, settings, meters)
    {
        ctx.selectStrip = [this] (Strip* s) { selected = s; };
        ctx.getSelectedStrip = [this] { return selected; };
        ctx.interfaceKind = ui::InterfaceKind::Universal;
    }
    ~BenchRig() { settingsFile.deleteFile(); }

    MixerEngine& engine;
    PluginHost host { nullptr };
    Show show;
    EventLog log;
    juce::AudioDeviceManager devices;
    juce::File settingsFile = juce::File::createTempFile (".settings");
    juce::PropertiesFile settings { settingsFile, {} };
    ui::MeterBank meters;
    ui::UiContext ctx;
    Strip* selected = nullptr;
};

/** Listens to the repaints of a window: JUCE reports every one of them (and of anything in
    it) to the cached image of the top component, as the web mirror uses. */
class Tracker final : public juce::CachedComponentImage
{
public:
    explicit Tracker (juce::Component& c) : owner (c) {}
    void paint (juce::Graphics&) override {}
    bool invalidateAll() override { return invalidate (owner.getLocalBounds()); }
    bool invalidate (const juce::Rectangle<int>& area) override
    {
        dirty.add (area);
        ++asked[area.toString()];
        return true;
    }
    void releaseResources() override {}

    juce::RectangleList<int> take() { return std::exchange (dirty, {}); }

    /** How often each rectangle was asked for, to see which components repaint. */
    std::map<juce::String, int> asked;

private:
    juce::Component& owner;
    juce::RectangleList<int> dirty;
};

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
    double p95() const
    {
        if (values.empty())
            return 0.0;
        auto sorted = values;
        std::sort (sorted.begin(), sorted.end());
        return sorted[(size_t) juce::jmin ((int) sorted.size() - 1, (int) (0.95 * (double) sorted.size()))];
    }
    std::vector<double> values;
};
} // namespace

class PaintBenchTests final : public juce::UnitTest
{
public:
    PaintBenchTests() : juce::UnitTest ("Paint benchmark", "PaintBench") {}

    void runTest() override
    {
        if (! test::hasDisplay())
            return;

        beginTest ("The WING, full surface");
        {
            MixerEngine engine (0);
            engine.setNumChannels (40);
            BenchRig rig (engine);
            setShownProfile (ld::kProfileWing);
            rig.ctx.interfaceKind = ui::InterfaceKind::Wing;
            rig.ctx.consoleModel = ConsoleModel::Full;
            rig.show.layers.setModel (ConsoleModel::Full);
            ui::ConsoleLookAndFeel look;
            juce::LookAndFeel::setDefaultLookAndFeel (&look);
            daw::Session recording (engine.getRecorder(), &rig.settings);
            rig.ctx.recording = &recording;
            ui::cc::Assignments assignments (rig.show);
            rig.ctx.customControls = &assignments;
            nameStrips (engine);
            rig.selected = engine.getChannels()[18].get();
            ui::WingSurface skin (rig.ctx);
            skin.setSize (1920, 1080);
            skin.selectionChanged();
            skin.refreshAll();
            measure ("WING full surface", engine, rig, skin);
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        }

        beginTest ("The Universal Mixer, 32 channels, Flagship Black");
        {
            MixerEngine engine (0);
            const Counts counts { 32, 4, 16, 4, 6, 16, 8, 16 };
            engine.configure (&counts);
            BenchRig rig (engine);
            nameStrips (engine);
            rig.selected = engine.getChannels()[18].get();
            ui::UniversalSkin skin (rig.ctx);
            skin.setSize (1920, 1080);
            skin.selectionChanged();
            skin.getMixer().refresh();
            skin.getMixer().getChannelPanel().refresh();
            measure ("Universal Mixer 32 ch", engine, rig, skin);
        }

        beginTest ("The dLive C3500");
        {
            MixerEngine engine (0);
            engine.configureProfile (ld::kProfileDLive);
            BenchRig rig (engine);
            setShownProfile (ld::kProfileDLive);
            ui::fader::setLaw (engine.getFaderLaw());
            nameStrips (engine);
            rig.selected = engine.getChannels()[5].get();
            ui::DLiveC3500Surface skin (rig.ctx);
            skin.setSize (1920, 1080);
            skin.refreshAll();
            measure ("dLive C3500", engine, rig, skin);
        }

        beginTest ("The DM2000 with its meter bridge");
        {
            MixerEngine engine (0);
            engine.configureProfile (ld::kProfileDm2000);
            BenchRig rig (engine);
            setShownProfile (ld::kProfileDm2000);
            ui::routing::setProfile (ld::kProfileDm2000);
            ui::fader::setLaw (engine.getFaderLaw());
            nameStrips (engine);
            rig.selected = engine.getChannels()[0].get();
            ui::Dm2000Surface skin (rig.ctx);
            skin.setSize (1920, 1080);
            skin.refreshAll();
            skin.selectionChanged();
            measure ("DM2000 + meter bridge", engine, rig, skin);
        }

        beginTest ("The X32");
        {
            MixerEngine engine (0);
            engine.configureProfile (ld::kProfileX32);
            BenchRig rig (engine);
            setShownProfile (ld::kProfileX32);
            nameStrips (engine);
            rig.selected = engine.getChannels()[12].get();
            ui::X32Surface skin (rig.ctx);
            skin.setSize (1920, 1080);
            skin.selectionChanged();
            skin.refreshAll();
            measure ("X32", engine, rig, skin);
        }

        beginTest ("The PLUGIN HOST, RACK page");
        {
            MixerEngine engine (0);
            engine.configurePluginHost (16);
            BenchRig rig (engine);
            setShownProfile (ld::kProfilePluginHost);
            ui::PluginHostSurface skin (rig.ctx);
            skin.setSize (1920, 1080);
            nameStrips (engine);
            auto* strip = engine.getChannels()[0].get();
            strip->insertBlock (BlockKind::Eq, -1);
            strip->insertBlock (BlockKind::Dynamics, -1);
            skin.showChanged();
            skin.refreshAll();
            skin.selectChannel (0);
            skin.getTab (ui::PluginHostSurface::Page::Rack).onClick();
            measure ("PLUGIN HOST RACK", engine, rig, skin);
        }
    }

private:
    static void nameStrips (MixerEngine& engine)
    {
        static const char* names[] = { "Kick", "Snare", "Hat", "Tom 1", "Tom 2", "OH L", "OH R", "Bass", "Gtr 1", "Gtr 2", "Keys L", "Keys R", "Vox 1", "Vox 2", "Vox 3", "Click" };
        auto& channels = engine.getChannels();
        for (size_t i = 0; i < channels.size(); ++i)
        {
            auto& s = *channels[i];
            s.params.custLink = false;
            s.setName (names[i % std::size (names)]);
            s.setColourIndex ((int) (i % 7) + 1);
            s.params.faderDb = i % 5 == 0 ? 0.0f : -6.0f - (float) (i % 4) * 3.0f;
        }
    }

    /** Moves every strip's peaks as a band plays: each at its own pace, never still. */
    static void movePeaks (MixerEngine& engine, int tick)
    {
        int n = 0;
        for (int k = 0; k < kNumStripKinds; ++k)
            for (auto& s : engine.getStrips (static_cast<StripKind> (k)))
            {
                const float level = 0.06f + 0.7f * (0.5f + 0.5f * std::sin (0.37f * (float) tick * (1.0f + 0.05f * (float) (n % 7)) + 1.7f * (float) n));
                ++n;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float v = level * (ch == 0 ? 1.0f : 0.9f);
                    s->meters.inputPeak[ch] = v;
                    s->meters.trimPeak[ch] = v;
                    s->meters.outputPeak[ch] = v * 0.8f;
                }
            }
        engine.monitorPeak[0] = 0.3f;
        engine.monitorPeak[1] = 0.28f;
    }

    /** Runs the message loop for `milliseconds` and returns how long it spent handling
        messages (the timers' callbacks), not waiting. */
    static double pump (int milliseconds)
    {
        double busy = 0;
#if JUCE_MAC
        test::pumpMessages (milliseconds);
#else
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) milliseconds;
        while (juce::Time::getMillisecondCounter() < end)
        {
            const auto t0 = juce::Time::getHighResolutionTicks();
            if (juce::detail::dispatchNextMessageOnSystemQueue (true))
                busy += ms (t0);
            else
                juce::Thread::sleep (1);
        }
#endif
        return busy;
    }

    void measure (const juce::String& view, MixerEngine& engine, BenchRig& rig, juce::Component& skin)
    {
        constexpr int width = 1920, height = 1080;
        skin.setVisible (true);
        juce::Image image (juce::Image::ARGB, width, height, true, juce::SoftwareImageType());
        const double window = (double) width * height;

        // A full repaint.
        Stats full;
        for (int i = 0; i < 33; ++i)
        {
            const auto t0 = juce::Time::getHighResolutionTicks();
            juce::Graphics g (image);
            skin.paintEntireComponent (g, false);
            if (i >= 3)
                full.add (ms (t0));
        }

        // A meter-only update: the peaks move, the timers run for a tick, the areas that
        // asked for a repaint are painted.
        auto* tracker = new Tracker (skin);
        skin.setCachedComponentImage (tracker);
        Stats paint, area, handlers;
        for (int tick = 0; tick < 90; ++tick)
        {
            movePeaks (engine, tick);
            const double busy = pump (30);
            auto dirty = tracker->take();
            double painted = 0;
            if (! dirty.isEmpty())
            {
                const auto t0 = juce::Time::getHighResolutionTicks();
                juce::Graphics g (image);
                g.reduceClipRegion (dirty);
                skin.paintEntireComponent (g, false);
                painted = ms (t0);
            }
            if (tick >= 10)
            {
                paint.add (painted);
                area.add (100.0 * regionArea (dirty) / window);
                handlers.add (busy);
            }
        }
        if (std::getenv ("LIVEDAW_PAINTBENCH_TREE") != nullptr)
        {
            std::vector<std::pair<double, juce::String>> top;
            for (auto& [rect, count] : tracker->asked)
            {
                const auto r = juce::Rectangle<int>::fromString (rect);
                juce::String owner;
                for (auto* c = skin.getComponentAt (r.getCentre()); c != nullptr && c != &skin; c = c->getParentComponent())
                {
                    owner = typeid (*c).name();
                    if (skin.getLocalArea (c, c->getLocalBounds()).contains (r))
                        break;
                }
                double each = 1.0e9;
                for (int i = 0; i < 3; ++i)
                {
                    const auto t0 = juce::Time::getHighResolutionTicks();
                    juce::Graphics g (image);
                    g.reduceClipRegion (r);
                    skin.paintEntireComponent (g, false);
                    each = juce::jmin (each, ms (t0));
                }
                top.push_back ({ each * count, rect + " x" + juce::String (count) + " " + owner + " " + juce::String (each, 2) + " ms each, " + juce::String (each * count / 80.0, 2) + " ms per tick" });
            }
            std::sort (top.begin(), top.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
            for (size_t i = 0; i < top.size() && i < 8; ++i)
                logMessage ("  DIRTY " + view + ": " + top[i].second);
        }
        skin.setCachedComponentImage (nullptr);

        logMessage ("PAINTBENCH " + view.paddedRight (' ', 24)
                    + " full mean " + juce::String (full.mean(), 2) + " p95 " + juce::String (full.p95(), 2)
                    + " ms | meter tick paint mean " + juce::String (paint.mean(), 2) + " p95 " + juce::String (paint.p95(), 2)
                    + " ms, timers " + juce::String (handlers.mean(), 2) + " ms, area mean " + juce::String (area.mean(), 1)
                    + " % p95 " + juce::String (area.p95(), 1) + " %");
        expect (full.mean() > 0.0, view);
        if (std::getenv ("LIVEDAW_PAINTBENCH_TREE") != nullptr)
            logTree (view, skin);
        juce::ignoreUnused (rig);
    }

    struct Cost
    {
        juce::String what;
        juce::Rectangle<int> area;
        double self = 0;
    };

    /** Paints `c` and what is inside it alone, a few times, and returns the fastest time. */
    static double inclusive (juce::Component& c)
    {
        juce::Image image (juce::Image::ARGB, juce::jmax (1, c.getWidth()), juce::jmax (1, c.getHeight()), true, juce::SoftwareImageType());
        double best = 1.0e9;
        for (int i = 0; i < 4; ++i)
        {
            const auto t0 = juce::Time::getHighResolutionTicks();
            juce::Graphics g (image);
            c.paintEntireComponent (g, false);
            best = juce::jmin (best, ms (t0));
        }
        return best;
    }

    /** What each component costs on its own (painted with its children hidden), for the hotspot hunt. */
    static void walk (juce::Component& c, juce::Component& root, std::vector<Cost>& out)
    {
        std::vector<juce::Component*> shown;
        for (auto* child : c.getChildren())
            if (child->isVisible())
                shown.push_back (child);
        for (auto* child : shown)
            child->setVisible (false);
        const double self = inclusive (c);
        for (auto* child : shown)
            child->setVisible (true);
        out.push_back ({ juce::String (typeid (c).name()) + " " + c.getName(), root.getLocalArea (&c, c.getLocalBounds()), self });
        for (auto* child : shown)
            walk (*child, root, out);
    }

    void logTree (const juce::String& view, juce::Component& skin)
    {
        double direct = 0;
        for (auto* child : skin.getChildren())
            if (child->isVisible())
                direct += inclusive (*child);
        logMessage ("  TREE " + view + ": root in all " + juce::String (inclusive (skin), 1) + " ms, its children alone " + juce::String (direct, 1) + " ms");
        std::vector<Cost> costs;
        walk (skin, skin, costs);
        std::sort (costs.begin(), costs.end(), [] (const Cost& a, const Cost& b) { return a.self > b.self; });
        double total = 0;
        for (auto& c : costs)
            total += c.self;
        logMessage ("  TREE " + view + ": " + juce::String ((int) costs.size()) + " components, " + juce::String (total, 1) + " ms on their own");
        for (size_t i = 0; i < costs.size() && i < 14; ++i)
            logMessage ("  TREE " + view + ": " + juce::String (costs[i].self, 2) + " ms  " + costs[i].what + " " + costs[i].area.toString());
    }

    /** The pixels a list of rectangles covers (a list from `add` has no overlaps). */
    static double regionArea (const juce::RectangleList<int>& list)
    {
        double sum = 0;
        for (auto r : list)
            sum += (double) r.getWidth() * r.getHeight();
        return sum;
    }
};

static PaintBenchTests paintBenchTests;
