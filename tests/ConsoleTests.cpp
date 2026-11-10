#include "TestHelpers.h"
#include "engine/EventLog.h"
#include "engine/PluginHost.h"
#include "engine/Show.h"
#include "ui/ConsoleLook.h"
#include "ui/CustomControlsPage.h"
#include "ui/MainDisplay.h"
#include "ui/MeterBank.h"
#include "ui/RackSection.h"
#include "ui/SurfaceLayout.h"
#include "ui/UiContext.h"

using namespace livedaw;
using livedaw::test::OfflineRig;

namespace
{
/** A UI context over an engine, with a settings file of its own (for the WING RACK's section). */
struct RackRig
{
    explicit RackRig (MixerEngine& e) : engine (e), show (e, host), meters (e), ctx (e, host, show, log, devices, settings, meters)
    {
        ctx.selectStrip = [this] (Strip* s) { selected = s; };
        ctx.getSelectedStrip = [this] { return selected; };
        ctx.showScreen = [this] (ui::Screen s) { screen = s; };
        ctx.getScreen = [this] { return screen; };
        ctx.showCustomControls = [this] (int layer)
        {
            customControlsShown = layer;
            screen = ui::Screen::Setup;
        };
    }
    ~RackRig() { settingsFile.deleteFile(); }

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
    ui::Screen screen = ui::Screen::Home;
    int customControlsShown = -1; // the layer CUSTOM CONTROLS was opened on
};
} // namespace

/** The console-level features through the C++ facade: SOLO MODE, MUTEGROUP/SIP
    OVERRIDE, the talk keys, the PHONES path, MON DCAs and sources; and the
    console look (the surface finish) kept in the app settings. */
class ConsoleTests final : public juce::UnitTest
{
public:
    ConsoleTests() : juce::UnitTest ("Console level", "LiveDAW") {}

    void runTest() override
    {
        beginTest ("SIP mutes the unsoloed inputs; MUTE flashes and OVERRIDE unmutes");
        {
            MixerEngine engine (0);
            OfflineRig rig (engine, 2, 2);
            test::setChannels (engine, 2);
            auto& a = *engine.getChannels()[0];
            auto& b = *engine.getChannels()[1];
            b.params.inputLeft = 1; // both centred: unity on the left (the WING pan law)
            rig.setInput (0, 0.5f);
            rig.setInput (1, 0.25f);
            expectEquals ((int) engine.soloMode.load(), (int) ld::kSoloStudio); // the test console's (unityConsole)

            engine.soloMode = ld::kSoloSip;
            a.params.solo = true;
            rig.run (4);
            expect (engine.sipActive.load());
            expectEquals (b.getMuteState(), (int) ld::kMuteGroup);
            expectWithinAbsoluteError (rig.out (0), 0.5f, 1.0e-5f);

            // Without OVERRIDE, MUTE mutes the strip itself; with it, MUTE unmutes.
            engine.pressMute (b);
            expect (b.params.mute.load());
            engine.pressMute (b);
            engine.muteOverride = true;
            rig.run (2);
            engine.pressMute (b);
            rig.run (2);
            expectEquals (b.getMuteState(), (int) ld::kMuteOverridden);
            expectWithinAbsoluteError (rig.out (0), 0.75f, 1.0e-5f);

            a.params.solo = false;
            rig.run (2);
            expect (! engine.sipActive.load());
            expectEquals (b.getMuteState(), (int) ld::kMuteOff);
        }

        beginTest ("Talk keys: PUSH, LATCH and AUTO; talk reaches its buses");
        {
            MixerEngine engine (0);
            OfflineRig rig (engine, 2, 4);
            test::setChannels (engine, 40);
            auto* talkCh = engine.getChannels()[39].get();
            talkCh->params.inputLeft = 1;
            talkCh->params.mute = true;
            rig.setInput (1, 0.5f);
            expect (engine.getTalkStrip() == nullptr);
            engine.setTalkStrip (talkCh);
            expect (engine.getTalkStrip() == talkCh);
            expectEquals ((int) engine.talk.assignIndex.load(), 39);

            auto& pathA = engine.talk.paths[0];
            pathA.buses.store (ld::GroupBits::single (2)); // BUS 3
            auto& bus3 = *engine.getBusInSlot (2);
            bus3.params.outputLeft = 2;
            rig.run (2);
            expectWithinAbsoluteError (rig.out (2), 0.0f, 1.0e-7f);

            pathA.mode = ld::kTalkPush;
            engine.talkKey (0, true);
            expect (engine.isTalking());
            rig.run (2);
            expectWithinAbsoluteError (rig.out (2), 0.5f, 1.0e-5f);
            engine.talkKey (0, false);
            expect (! engine.isTalkOn (0));

            pathA.mode = ld::kTalkLatch;
            engine.talkKey (0, true);
            engine.talkKey (0, false);
            expect (engine.isTalkOn (0));
            engine.talkKey (0, true);
            expect (! engine.isTalkOn (0));
            engine.talkKey (0, false);

            // AUTO: a short press latches, the next press ends it; held, it's a push.
            pathA.mode = ld::kTalkAuto;
            engine.talkKey (0, true);
            engine.talkKey (0, false);
            expect (engine.isTalkOn (0));
            engine.talkKey (0, true);
            engine.talkKey (0, false);
            expect (! engine.isTalkOn (0));
            engine.talkKey (0, true);
            juce::Thread::sleep ((int) MixerEngine::kTalkHoldMs + 50);
            engine.talkKey (0, false);
            expect (! engine.isTalkOn (0));

            engine.setTalkStrip (nullptr);
            expectEquals ((int) engine.talk.assignKind.load(), (int) ld::kTalkOff);
        }

        beginTest ("The PHONES path, the solo bus and MON DCAs");
        {
            MixerEngine engine (0);
            OfflineRig rig (engine, 1, 6);
            test::setChannels (engine, 1);
            auto& ch = *engine.getChannels()[0];
            rig.setInput (0, 0.5f);
            engine.monitorOutputLeft = 2;
            engine.phones.outputLeft = 4;
            rig.run (4);
            expectWithinAbsoluteError (rig.out (4), 0.5f, 1.0e-5f);

            engine.setSoloBus (1); // B: SPEAKERS
            ch.params.solo = true;
            ch.params.faderDb = -100.0f;
            rig.run (4);
            expectWithinAbsoluteError (rig.out (2), 0.5f, 1.0e-5f); // PFL
            expectWithinAbsoluteError (rig.out (4), 0.0f, 1.0e-6f); // Main 1, fader down
            ch.params.solo = false;
            ch.params.faderDb = 0.0f;

            engine.getDca (0).faderDb = -6.0206f;
            engine.setDcaName (0, "MON A"); // the phones
            expectEquals ((int) engine.getDcaMonitorPaths (0), 2);
            rig.run (4);
            expectWithinAbsoluteError (rig.out (2), 0.5f, 1.0e-5f);
            expectWithinAbsoluteError (rig.out (4), 0.25f, 1.0e-4f);

            expectEquals (MixerEngine::monitorSourceName (0), juce::String ("MAIN.1"));
            expectEquals (MixerEngine::monitorSourceName (4 + 8 + 15), juce::String ("BUS.16"));
            expectEquals (MixerEngine::monitorSourceName (ld::kNumMonitorSources - 1), juce::String ("AUX.8"));
        }

        beginTest ("Sources process the ports, pair, and survive a show");
        {
            MixerEngine engine (0);
            PluginHost host (nullptr);
            Show show (engine, host);
            OfflineRig rig (engine, 2, 2);
            test::setChannels (engine, 1);
            auto& ch = *engine.getChannels()[0];
            rig.setInput (0, 0.25f);
            expectEquals (engine.getNumSources(), ld::kNumSources);
            const int index = MixerEngine::sourceIndex (0);
            auto* src = engine.getSource (index);
            expect (src != nullptr);
            if (src == nullptr)
                return;
            // LOCAL IN's 2.5 dB steps: a gain off them (a show from before) plays
            // on the nearest one, +5 dB, and loads as that.
            src->gainDb = 6.0206f;
            src->invert = true;
            rig.run (4);
            const float plus5 = 0.25f * juce::Decibels::decibelsToGain (5.0f);
            expectWithinAbsoluteError (rig.out (0), -plus5, 1.0e-4f);
            engine.setSourceName (index, "Kick In");
            engine.setSourceMode (index, ld::kSourceStereo);
            expectEquals ((int) engine.getSource (1)->mode.load(), (int) ld::kSourceStereo);
            expectEquals (engine.getSource (1)->gainDb.load(), 6.0206f);
            ch.setTalkTags (ld::kTalkAOn | ld::kTalkBSolo);

            auto xml = show.toXml();
            engine.initializeSource (index);
            expect (engine.getSourceName (index).isEmpty());
            expectEquals (engine.getSource (0)->gainDb.load(), 0.0f);

            MixerEngine restoredEngine (0);
            Show restored (restoredEngine, host);
            const auto result = restored.loadFromXml (*xml);
            expect (result.wasOk(), result.getErrorMessage());
            expectEquals (restoredEngine.getSourceName (index), juce::String ("Kick In"));
            expectEquals (restoredEngine.getSource (1)->gainDb.load(), 5.0f);
            expect (restoredEngine.getSource (0)->invert.load());
            expectEquals ((int) restoredEngine.getChannels()[0]->getTalkTags(), (int) (ld::kTalkAOn | ld::kTalkBSolo));
            expectEquals (MixerEngine::sourcePort (MixerEngine::sourceIndex (ld::kPlaybackSourceBase + 3)), ld::kPlaybackSourceBase + 3);
        }

        beginTest ("Oscillator: the OSC sources play on a channel and are saved with the show");
        {
            MixerEngine engine (0);
            PluginHost host (nullptr);
            Show show (engine, host);
            OfflineRig rig (engine, 2, 2);
            test::setChannels (engine, 1);
            auto& ch = *engine.getChannels()[0];
            ch.params.inputLeft = MixerEngine::oscSource (1);
            expectEquals (MixerEngine::sourceIndex (MixerEngine::oscSource (1)), ld::kNumSources - ld::kNumUsrSources - 1);
            auto* osc = engine.getOscillator (1);
            expect (osc != nullptr && engine.getOscillator (2) == nullptr);
            if (osc == nullptr)
                return;
            osc->levelDb = -12.0f;
            osc->freqHz = 250.0f;
            rig.run (40);
            float peak = 0.0f;
            for (int b = 0; b < 40; ++b)
            {
                rig.run (1);
                for (float v : rig.outputs[0])
                    peak = juce::jmax (peak, std::abs (v));
            }
            expectWithinAbsoluteError (peak, juce::Decibels::decibelsToGain (-12.0f), 0.005f);

            auto xml = show.toXml();
            MixerEngine restoredEngine (0);
            Show restored (restoredEngine, host);
            const auto result = restored.loadFromXml (*xml);
            expect (result.wasOk(), result.getErrorMessage());
            expectEquals (restoredEngine.getOscillator (1)->freqHz.load(), 250.0f);
            expectEquals (restoredEngine.getOscillator (1)->levelDb.load(), -12.0f);
            expectEquals (restoredEngine.getChannels()[0]->getInputLeft(), MixerEngine::oscSource (1));
        }

        beginTest ("Monitor paths: DIRECT IN, EQ curve, INVERT, LIMITER; SOURCE SOLO");
        {
            MixerEngine engine (0);
            PluginHost host (nullptr);
            Show show (engine, host);
            OfflineRig rig (engine, 4, 6);
            test::setChannels (engine, 40);
            engine.phones.outputLeft = 4;
            for (auto& ch : engine.getChannels())
                ch->params.inputLeft = -1;
            auto& ch1 = *engine.getChannels()[0];
            auto& ch2 = *engine.getChannels()[1];
            ch1.params.inputLeft = 0;
            ch2.params.inputLeft = 1;
            ch2.mains[0].on = false;
            rig.setInput (0, 0.25f);
            rig.setInput (1, 0.125f);
            rig.setInput (2, 0.5f);
            rig.run (4);
            expectWithinAbsoluteError (rig.out (4), 0.25f, 1.0e-5f);

            // DIRECT IN CH.2 on the PHONES, then INVERT.
            auto& phones = engine.monitorPath (true);
            expect (&phones == &engine.monitor.paths[1]);
            phones.directSource = 1;
            expectEquals (MixerEngine::directSourceName (1), juce::String ("CH.2"));
            expectEquals (MixerEngine::directSourceName (-1), juce::String ("OFF"));
            rig.run (4);
            expectWithinAbsoluteError (rig.out (4), 0.375f, 1.0e-5f);
            phones.invert = true;
            rig.run (4);
            expectWithinAbsoluteError (rig.out (4), -0.375f, 1.0e-5f);
            phones.invert = false;
            phones.limiterDb = -12.0f;
            rig.run (8);
            expect (rig.out (4) <= juce::Decibels::decibelsToGain (-12.0f) + 1.0e-5f);
            expect (phones.limiterGrDb.exchange (0.0f) > 1.0f);
            phones.limiterDb = 0.0f;
            rig.run (400); // its release

            // The EQ curve for the graph.
            phones.eqOn = true;
            phones.eq[3].freq = 1000.0f;
            phones.eq[3].gainDb = 6.0f;
            const float freqs[] = { 1000.0f };
            float db = 0.0f;
            engine.getMonitorEqResponse (true, -1, freqs, &db, 1);
            expectWithinAbsoluteError (db, 6.0f, 0.1f);
            expectWithinAbsoluteError (MixerEngine::monitorDelayMs (3.43f), 10.0f, 1.0e-3f);

            // SOURCE SOLO: nothing while OFF; CH39 hosts it (its SOLO), the solo follows the source.
            engine.setSoloBus (0); // PHONES
            engine.setSourceSolo (2, true);
            expect (! engine.isSourceSoloed (2));
            expect (engine.getSourceSoloHost() == nullptr);
            engine.monitor.sourceSolo = ld::kSourceSoloCh39;
            expect (engine.getSourceSoloHost() == engine.getChannels()[38].get());
            expect (engine.isSourceSoloed (2) && engine.isSourceSoloActive());
            engine.monitor.sourceSoloOn = false;
            engine.setSourceSolo (2, true);
            expect (engine.getChannels()[38]->params.solo.load(), "through the host's SOLO");
            expect (engine.isSourceSoloed (2));
            phones.eqOn = false;
            phones.directSource = -1;
            rig.run (8);
            expectWithinAbsoluteError (rig.out (4), 0.5f, 1.0e-4f);
            engine.setSourceSolo (1, true);
            expect (engine.isSourceSoloed (1) && ! engine.isSourceSoloed (2));
            rig.run (8);
            expectWithinAbsoluteError (rig.out (4), 0.125f, 1.0e-4f);
            engine.setSourceSolo (2, false); // not the soloed one: nothing changes
            expect (engine.isSourceSoloed (1));
            engine.setSourceSoloPort (2);
            expect (engine.isSourceSoloed (2));
            engine.setSourceSoloPort (1);
            engine.clearSourceSolo();
            expect (! engine.getChannels()[38]->params.solo.load());
            rig.run (8);
            expectWithinAbsoluteError (rig.out (4), 0.25f, 1.0e-4f);

            // Source tags: a mute group mutes the source.
            engine.getSource (0)->muteGroups.store (GroupBits::mask (1u << 1));
            engine.muteGroups.store (GroupBits::mask (1u << 1));
            rig.run (4);
            expectWithinAbsoluteError (rig.out (4), 0.0f, 1.0e-6f);
            engine.muteGroups.clear();

            // All of it survives a show.
            phones.delayOn = true;
            phones.delayM = 20.0f;
            auto xml = show.toXml();
            MixerEngine restoredEngine (0);
            Show restored (restoredEngine, host);
            expect (restored.loadFromXml (*xml).wasOk());
            expect (restoredEngine.monitorPath (true).delayOn.load());
            expectEquals (restoredEngine.monitorPath (true).delayM.load(), 20.0f);
            expectEquals ((int) restoredEngine.monitor.sourceSolo.load(), (int) ld::kSourceSoloCh39);
            expectEquals ((int) restoredEngine.getSource (0)->muteGroups.load().low(), 2);
        }

        beginTest ("The surface finish persists in the app settings");
        {
            juce::TemporaryFile file (".settings");
            juce::PropertiesFile::Options o;
            o.applicationName = "looktest";
            {
                juce::PropertiesFile props (file.getFile(), o);
                expect (ui::ConsoleLook::load (props).finish == ui::SurfaceFinish::Silver); // the default
                ui::ConsoleLook look;
                look.finish = ui::SurfaceFinish::Black;
                look.save (props);
                expectEquals (props.getValue (ui::ConsoleLook::finishKey), juce::String ("BLACK"));
            }
            juce::PropertiesFile reopened (file.getFile(), o);
            expect (ui::ConsoleLook::load (reopened).finish == ui::SurfaceFinish::Black);
            reopened.setValue (ui::ConsoleLook::finishKey, "GOLD"); // unknown: silver
            expect (ui::ConsoleLook::load (reopened).finish == ui::SurfaceFinish::Silver);
        }

        beginTest ("The console model persists in the app settings; the COMPACT is black");
        {
            juce::TemporaryFile file (".settings");
            juce::PropertiesFile::Options o;
            o.applicationName = "modeltest";
            {
                juce::PropertiesFile props (file.getFile(), o);
                auto look = ui::ConsoleLook::load (props);
                expect (look.model == ConsoleModel::Full); // the default
                expect (look.shownFinish() == ui::SurfaceFinish::Silver);
                look.model = ConsoleModel::Compact;
                look.save (props);
                expectEquals (props.getValue (ui::ConsoleLook::modelKey), juce::String ("COMPACT"));
            }
            juce::PropertiesFile reopened (file.getFile(), o);
            const auto look = ui::ConsoleLook::load (reopened);
            expect (look.model == ConsoleModel::Compact);
            expect (look.finish == ui::SurfaceFinish::Silver);       // the setting stays ...
            expect (look.shownFinish() == ui::SurfaceFinish::Black); // ... the COMPACT shows black
            // The RACK, black too.
            reopened.setValue (ui::ConsoleLook::modelKey, "RACK");
            const auto rack = ui::ConsoleLook::load (reopened);
            expect (rack.model == ConsoleModel::Rack);
            expect (rack.finish == ui::SurfaceFinish::Silver && rack.shownFinish() == ui::SurfaceFinish::Black);
            reopened.setValue (ui::ConsoleLook::modelKey, "EXTENDER"); // unknown: the full-size console
            expect (ui::ConsoleLook::load (reopened).model == ConsoleModel::Full);
            expect (consoleModelFromName ("compact") == ConsoleModel::Compact);
            expect (consoleModelFromName (" rack ") == ConsoleModel::Rack);
            expectEquals (consoleModelName (ConsoleModel::Compact), juce::String ("COMPACT"));
            expectEquals (consoleModelName (ConsoleModel::Rack), juce::String ("RACK"));
            // The values that cross the C ABI.
            expect (consoleModelFromInt (2) == ConsoleModel::Rack && consoleModelFromInt (1) == ConsoleModel::Compact);
            expect (consoleModelFromInt (0) == ConsoleModel::Full && consoleModelFromInt (7) == ConsoleModel::Full);
            expect (isSmallModel (ConsoleModel::Rack) && isSmallModel (ConsoleModel::Compact) && ! isSmallModel (ConsoleModel::Full));
        }

        beginTest ("The console models' local I/O: 24 preamps and 1x2 GPIO on the COMPACT");
        {
            const auto full = consoleModelInfo (ConsoleModel::Full), compact = consoleModelInfo (ConsoleModel::Compact);
            expectEquals ((int) full.localPreamps, 8);
            expectEquals ((int) full.gpios, 4);
            expectEquals ((int) full.auxInputs, 8);
            expectEquals ((int) full.auxOutputs, 8);
            expectEquals ((int) full.faders, 24);
            expectEquals ((int) full.layerKeys, 7);
            expectEquals ((int) compact.localPreamps, 24);
            expectEquals ((int) compact.gpios, 2);
            expectEquals ((int) compact.auxInputs, 0);
            expectEquals ((int) compact.auxOutputs, 0);
            expectEquals ((int) compact.localOutputs, 8);
            expectEquals ((int) compact.faders, 13);
            expectEquals ((int) compact.layerKeys, 9);
            // The COMPACT's 24 local preamps take LOCAL IN's range, as the full size's 8 do
            // (LCL 1-48 are the interface's first 48 ports on either model).
            for (int lcl = 0; lcl < (int) compact.localPreamps; ++lcl)
            {
                const auto r = MixerEngine::sourceGainRange (lcl);
                expect (r.min == -2.5f && r.max == 45.0f && r.step == 2.5f, "LCL " + juce::String (lcl + 1));
            }
            // The RACK (WING manual chapter 12): 24 preamps, 8 XLR outputs, no local aux
            // I/O, 2x2 GPIO, no faders; five front panel layers (5.6 p. 56).
            const auto rack = consoleModelInfo (ConsoleModel::Rack);
            expectEquals ((int) rack.localPreamps, 24);
            expectEquals ((int) rack.localOutputs, 8);
            expectEquals ((int) rack.auxInputs, 0);
            expectEquals ((int) rack.auxOutputs, 0);
            expectEquals ((int) rack.gpios, 4);
            expectEquals ((int) rack.faders, 0);
            expectEquals ((int) rack.layerKeys, 5);
        }

        beginTest ("The WING RACK's layers: four channels at a time on CHANNELS, AUX, BUSES, MAIN/MTX and DCA");
        {
            using S = LayerSet::Section;
            MixerEngine engine (0);
            PluginHost host (nullptr);
            Show show (engine, host);
            engine.setNumChannels (40);
            auto& layers = show.layers;
            layers.setModel (ConsoleModel::Rack);
            expect (layers.getModel() == ConsoleModel::Rack);

            juce::StringArray ids;
            for (auto& layer : layers.getDefaultLayers (engine, S::Left))
                ids.add (layer.id);
            expectEquals (ids.joinIntoString ("|"), juce::String ("inputs|aux|bus-1|out-1|dca-1"));
            expectEquals (ids.size(), (int) consoleModelInfo (ConsoleModel::Rack).layerKeys);
            expectEquals ((int) layers.findLayer (engine, S::Left, "inputs").faders.size(), 40);
            expectEquals (layers.findLayer (engine, S::Left, "dca-1").name, juce::String ("DCA 1-16"));
            // Four wide, whatever the width setting; a layer of another model falls on CHANNELS.
            layers.setWidth (S::Left, 12);
            expectEquals (layers.getEffectiveWidth (S::Left), 4);
            expectEquals (layers.findLayer (engine, S::Left, "user-1").id, juce::String ("inputs"));
            // Its VIRTUAL section: eight layers, eight wide (manual 5.7 p. 61).
            expectEquals ((int) layers.getDefaultLayers (engine, LayerSet::kVirtualSection).size(), LayerSet::kNumVirtualLayers);
            expectEquals (layers.getEffectiveWidth (LayerSet::kVirtualSection), 8);

            // An app setting: a new show keeps it.
            show.clear (40);
            expect (layers.getModel() == ConsoleModel::Rack);
            layers.setModel (ConsoleModel::Full);
            expectEquals ((int) layers.getDefaultLayers (engine, S::Left).size(), 7);
            expectEquals (layers.getEffectiveWidth (S::Left), 12);
        }

        beginTest ("The WING RACK's front panel fits the window: the display and the four-channel section");
        {
            namespace rack = ui::layout::rack;
            using Rect = juce::Rectangle<int>;
            for (const auto [w, h] : { std::pair (1920, 1080), std::pair (1366, 768), std::pair (1280, 720), std::pair (2560, 1440), std::pair (3440, 1440), std::pair (1100, 700) })
            {
                const auto where = juce::String (w) + "x" + juce::String (h);
                const Rect window (w, h);
                const auto s = rack::plan (window, [] (int height) { return ui::MainDisplay::preferredWidth (height); });
                expect (window.contains (s.panel) && ! s.display.isEmpty() && ! s.section.isEmpty(), where);
                expect (! s.display.intersects (s.section) && s.display.getUnion (s.section) == s.panel, where);
                expectEquals (s.display.getRight(), s.section.getX(), where);
                // A 19 in, 4U unit: a band at least twice as wide as tall.
                expect (s.panel.getWidth() >= 2 * s.panel.getHeight(), where + ": " + s.panel.toString());
                expect (s.section.getWidth() >= 250 && s.display.getWidth() >= s.section.getWidth(), where);

                // The section's parts: inside it, none on another.
                const auto area = s.section.withZeroOrigin();
                const auto p = rack::planSection (area);
                std::vector<std::pair<juce::String, Rect>> parts { { "channels", p.channels }, { "layer display", p.layerDisplay }, { "<4", p.shiftLeft },
                                                                   { "4>", p.shiftRight },     { "VIEW", p.view },                  { "USB", p.usb },
                                                                   { "LEVEL", p.level },       { "phones", p.phones } };
                const char* keyNames[] = { "INPUT AUX", "BUSES MAIN", "DCA MUTE", "CUST TRANS" };
                for (int i = 0; i < rack::kChannels; ++i)
                    parts.push_back ({ keyNames[i], p.layerKeys[(size_t) i] });
                for (size_t i = 0; i < parts.size(); ++i)
                {
                    const auto& [name, r] = parts[i];
                    expect (! r.isEmpty() && area.contains (r), where + ": " + name + " " + r.toString());
                    for (size_t j = i + 1; j < parts.size(); ++j)
                        expect (! r.intersects (parts[j].second), where + ": " + name + " overlaps " + parts[j].first);
                }
                expectEquals (p.channels.getWidth(), rack::kChannels * p.columnW, where);
                expect (p.columnW >= 30, where);

                // A channel's column, top to bottom: the LCD window, the name band, the
                // encoder, MUTE, SOLO (manual Fig. 12: MUTE is the upper row); the
                // shift keys beside the MUTE and SOLO rows, the layer keys under the columns.
                for (int i = 0; i < rack::kChannels; ++i)
                {
                    const Rect column (p.channels.getX() + i * p.columnW, p.channels.getY(), p.columnW, p.channels.getHeight());
                    const auto c = rack::planColumn (column);
                    for (const auto& r : { c.window, c.knob, c.mute, c.solo })
                        expect (! r.isEmpty() && column.contains (r), where + ": column " + juce::String (i + 1) + " " + r.toString());
                    expect (c.window.getBottom() <= c.name.getY() && c.name.getBottom() <= c.knob.getY(), where);
                    expect (c.knob.getBottom() <= c.mute.getY() && c.mute.getBottom() <= c.solo.getY(), where);
                    expect (std::abs (c.knob.getCentreX() - column.getCentreX()) <= 1, where);
                    expectEquals (p.layerKeys[(size_t) i].getCentreX(), column.getCentreX(), where);
                    if (i == 0)
                    {
                        expectEquals (p.shiftLeft.getCentreY(), c.mute.getCentreY(), where);
                        expectEquals (p.shiftRight.getCentreY(), c.solo.getCentreY(), where);
                        expectEquals (p.layerDisplay.getCentreY(), c.knob.getCentreY(), where);
                    }
                }
                expect (p.layerKeys[0].getY() >= p.channels.getBottom() && p.usb.getY() >= p.layerKeys[0].getBottom(), where);
            }
        }

        beginTest ("The WING RACK's four-channel section: its layer keys, the shift keys, the layer display and VIEW");
        {
            using Key = ui::RackSection::Key;
            using Mode = ui::RackSection::Mode;
            using RackMode = ui::CustomControlSection::RackMode;
            MixerEngine engine (0);
            engine.setNumChannels (40);
            RackRig rig (engine);
            rig.ctx.consoleModel = ConsoleModel::Rack;
            rig.show.layers.setModel (ConsoleModel::Rack);
            ui::cc::Assignments assignments (rig.show);
            rig.ctx.customControls = &assignments;

            ui::FaderSection channels (rig.ctx, LayerSet::Left);
            ui::CustomControlSection controls (rig.ctx);
            ui::RackSection rack (rig.ctx, channels, controls);
            int modeChanges = 0;
            rack.onModeChanged = [&modeChanges] { ++modeChanges; };
            rack.setActive (true);
            channels.refresh();
            rack.setSize (620, 720);
            channels.setBounds (rack.getChannelsArea());
            controls.setBounds (rack.getChannelsArea());
            expect (channels.isRackStyle() && rack.showsChannels());
            expectEquals (channels.getNumStrips(), 4);
            expect (! rack.getChannelsArea().isEmpty() && rack.getLocalBounds().contains (rack.getChannelsArea()));

            // INPUT AUX: it starts on input channels 1-40, four at a time; the key toggles to the aux channels.
            expectEquals (channels.getLayerId(), juce::String (ui::RackSection::kInputs));
            expect (channels.getStrip (0) == engine.getChannels()[0].get() && channels.getStrip (3) == engine.getChannels()[3].get());
            expect (rack.isKeyLit (Key::InputAux) && ! rack.isKeyLit (Key::BusesMain) && ! rack.isKeyLit (Key::DcaMute) && ! rack.isKeyLit (Key::CustTrans));
            expectEquals (rack.layerDisplayText(), juce::String ("1"));
            expect (channels.canShift (1) && ! channels.canShift (-1));
            rack.shift (1, false); // 4>
            expect (channels.getStrip (0) == engine.getChannels()[4].get());
            expectEquals (rack.layerDisplayText(), juce::String ("2"));
            rack.shift (1, true); // held: the layer's last four
            expect (channels.getStrip (3) == engine.getChannels()[39].get());
            expectEquals (rack.layerDisplayText(), juce::String ("10"));
            expect (! channels.canShift (1));
            rack.shift (-1, true); // held: its first four
            expect (channels.getStrip (0) == engine.getChannels()[0].get());
            rack.pressKey (Key::InputAux);
            expectEquals (channels.getLayerId(), juce::String (ui::RackSection::kAux));
            expect (rack.isKeyLit (Key::InputAux));
            rack.pressKey (Key::InputAux);
            expectEquals (channels.getLayerId(), juce::String (ui::RackSection::kInputs));

            // BUSES MAIN: buses 1-16; again: main 1-4 and matrix 1-8.
            rack.pressKey (Key::BusesMain);
            expectEquals (channels.getLayerId(), juce::String (ui::RackSection::kBuses));
            expect (channels.getStrip (0) == engine.getBuses()[0].get());
            rack.pressKey (Key::BusesMain);
            expectEquals (channels.getLayerId(), juce::String (ui::RackSection::kMainMatrix));
            expect (channels.getStrip (0) == engine.getMains()[0].get());
            expect (rack.isKeyLit (Key::BusesMain) && ! rack.isKeyLit (Key::InputAux));

            // DCA MUTE: DCA 1-16; again: the mute groups on the eight keys (the custom
            // controls section plays them, in the channels' place).
            rack.pressKey (Key::DcaMute);
            expectEquals (channels.getLayerId(), juce::String (ui::RackSection::kDca));
            expect (channels.getStrip (0) == nullptr && rack.showsChannels()); // a DCA, not a strip
            const int before = modeChanges;
            rack.pressKey (Key::DcaMute);
            expect (rack.getMode() == Mode::MuteGroups && ! rack.showsChannels() && rack.isKeyLit (Key::DcaMute));
            expect (controls.getRackMode() == RackMode::MuteGroups);
            expectEquals (modeChanges, before + 1);
            expect (rack.layerDisplayText().isEmpty());
            rack.shift (1, false); // nothing to step through
            expect (rack.getMode() == Mode::MuteGroups);
            rack.pressKey (Key::DcaMute);
            expect (rack.showsChannels());
            expectEquals (channels.getLayerId(), juce::String (ui::RackSection::kDca));

            // CUST TRANS: the custom controls' 16 layers on the shift keys, then the transports.
            rack.pressKey (Key::CustTrans);
            expect (rack.getMode() == Mode::Custom && controls.getRackMode() == RackMode::Custom && rack.isKeyLit (Key::CustTrans));
            expectEquals (rack.layerDisplayText(), juce::String ("1"));
            rack.shift (1, false);
            expectEquals (assignments.currentLayer, 1);
            expectEquals (rack.layerDisplayText(), juce::String ("2"));
            rack.shift (1, true);
            expectEquals (assignments.currentLayer, ui::cc::kNumLayers - 1);
            expectEquals (rack.layerDisplayText(), juce::String ("16"));
            rack.shift (-1, true);
            expectEquals (assignments.currentLayer, 0);
            rack.pressView (false); // VIEW on CUST: CUSTOM CONTROLS on the layer shown
            expectEquals (rig.customControlsShown, 0);
            rack.pressKey (Key::CustTrans);
            expect (rack.getMode() == Mode::UsbTransport && controls.getRackMode() == RackMode::TwoTrack);
            rack.pressView (false);
            expect (rig.screen == ui::Screen::UsbRecorder);
            rack.pressKey (Key::CustTrans);
            expect (rack.getMode() == Mode::MultiTransport && controls.getRackMode() == RackMode::MultiTrack);
            rack.pressKey (Key::CustTrans);
            expect (rack.getMode() == Mode::Custom);

            // VIEW on a channel layer (manual Table 4.6): OVERVIEW, again FADERS; held: CONFIG.
            rack.pressKey (Key::InputAux);
            expect (rack.showsChannels());
            rig.screen = ui::Screen::Home;
            rig.ctx.viewSection = LayerSet::Right;
            rack.pressView (false);
            expect (rig.screen == ui::Screen::Overview && rig.ctx.viewSection == LayerSet::Left);
            rack.pressView (false);
            expect (rig.screen == ui::Screen::Faders);
            rack.pressView (false);
            expect (rig.screen == ui::Screen::Overview);
            rack.pressView (true);
            expect (rig.screen == ui::Screen::Config);

            // A fader to be shown brings the channels back.
            rack.pressKey (Key::CustTrans);
            rack.showChannels();
            expect (rack.showsChannels() && controls.getRackMode() == RackMode::Custom); // (hidden, in its RACK layout)

            // Another model: the strips and the custom controls are the console's again.
            rack.setActive (false);
            expect (! channels.isRackStyle() && controls.getRackMode() == RackMode::Off);
        }
    }
};

static ConsoleTests consoleTests;
