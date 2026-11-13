// The PLUGIN HOST's surface and its RACK page (src/ui/PluginHostSurface.*, PluginHostRack.*; docs/PLUGIN_HOST.md, step 2).

#include "TestHelpers.h"
#include "engine/DirectOut.h"
#include "engine/EventLog.h"
#include "engine/PluginHost.h"
#include "engine/Show.h"
#include "ui/PluginHostSurface.h"

using namespace livedaw;

namespace
{
/** A plug-in format for the tests' fakes (the description's file or identifier names the plug-in), so a recall can make a plug-in again. */
class FakeFormat final : public juce::AudioPluginFormat
{
public:
    juce::String getName() const override { return "Fake"; }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String&) override {}
    bool fileMightContainThisPluginType (const juce::String&) override { return true; }
    juce::String getNameOfPluginFromIdentifier (const juce::String& id) override { return id; }
    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist (const juce::PluginDescription&) override { return true; }
    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

private:
    void createPluginInstance (const juce::PluginDescription& d, double, int, PluginCreationCallback callback) override
    {
        if (d.fileOrIdentifier == "fake-params")
            return callback (std::make_unique<test::ParameterPlugin>(), {});
        if (d.fileOrIdentifier == "fake-keyed")
            return callback (std::make_unique<test::SidechainPlugin>(), {});
        callback (std::make_unique<test::FakePlugin> (1.0f, test::FakePlugin::Mode::Gain, d.fileOrIdentifier), {});
    }
};

struct HostRig
{
    explicit HostRig (MixerEngine& e) : engine (e), show (e, host), meters (e), ctx (e, host, show, log, devices, settings, meters)
    {
        host.getFormatManager().addFormat (std::make_unique<FakeFormat>());
        ctx.selectStrip = [this] (Strip* s) { selected = s; };
        ctx.getSelectedStrip = [this] { return selected; };
        ctx.interfaceKind = ui::InterfaceKind::PluginHost;
    }
    ~HostRig() { settingsFile.deleteFile(); }

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

juce::MouseEvent mouseEvent (juce::Component& c, juce::Point<float> at, juce::Point<float> down)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, down, now, 1, at != down);
}

/** The uids of the blocks of `strip`'s chain, in order. */
std::vector<int> blockUids (const Strip& strip)
{
    std::vector<int> uids;
    for (const auto& step : strip.getChain())
        if (step.isBlock())
            uids.push_back (step.uid);
    return uids;
}

/** A plug-in block of `strip` holding a fake plug-in named `name` (latency 128). */
int addFakePlugin (MixerEngine& engine, Strip& strip, const juce::String& name, juce::AudioPluginInstance** out = nullptr)
{
    const int uid = strip.insertBlock (BlockKind::Plugin, -1);
    auto* fake = new test::FakePlugin (1.0f, test::FakePlugin::Mode::Gain, name);
    fake->setLatencySamples (128);
    engine.getFx().setPlugin (FxSlots::blockSlotOf (strip.getHandle(), uid), std::unique_ptr<juce::AudioPluginInstance> (fake));
    if (out != nullptr)
        *out = fake;
    return uid;
}
/** The chain step where the RACK page puts a new block (before the level: after the input's tap). */
int rackInsertionPoint (const Strip& strip)
{
    const auto chain = strip.getChain();
    for (int i = 0; i < (int) chain.size(); ++i)
        if (chain[(size_t) i].kind == ld::kStepPfl)
            return i;
    return -1;
}

/** A plug-in block on `strip` where the RACK page puts it, holding `plugin`; returns its block slot. */
int addPluginBlock (MixerEngine& engine, Strip& strip, std::unique_ptr<juce::AudioPluginInstance> plugin)
{
    const int uid = strip.insertBlock (BlockKind::Plugin, rackInsertionPoint (strip));
    const int slot = FxSlots::blockSlotOf (strip.getHandle(), uid);
    engine.getFx().setPlugin (slot, std::move (plugin));
    return slot;
}

/** A plug-in block of `strip` where the RACK page puts it, holding `plugin`; returns the block's uid. */
int addRackPlugin (MixerEngine& engine, Strip& strip, std::unique_ptr<juce::AudioPluginInstance> plugin)
{
    const int uid = strip.insertBlock (BlockKind::Plugin, rackInsertionPoint (strip));
    engine.getFx().setPlugin (FxSlots::blockSlotOf (strip.getHandle(), uid), std::move (plugin));
    return uid;
}

/** The uids of the plug-in blocks of `strip`, in order. */
std::vector<int> pluginUids (const Strip& strip)
{
    std::vector<int> uids;
    for (const auto& step : strip.getChain())
        if (step.isBlock() && step.getBlockKind() == BlockKind::Plugin)
            uids.push_back (step.uid);
    return uids;
}

/** Moves the show's glides on, as its timer does, until `done` or `ms` have passed. */
template <typename Done>
bool waitFor (Show& show, Done&& done, int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (! done() && juce::Time::getMillisecondCounter() < end)
    {
        juce::Thread::sleep (10);
        show.advanceGlides();
    }
    return done();
}
/** A PLUGIN HOST of 16 channels with its surface, for the CONTROL page's tests. */
struct Bench
{
    Bench() : engine (0)
    {
        engine.configurePluginHost (16);
        setShownProfile (ld::kProfilePluginHost);
        rig = std::make_unique<HostRig> (engine);
        skin = std::make_unique<ui::PluginHostSurface> (rig->ctx);
        skin->setSize (1920, 1080);
    }
    ~Bench()
    {
        skin.reset();
        rig.reset();
        engine.getGarbage().collectAllNow();
        setShownProfile (ld::kProfileUniversal);
    }

    /** A plug-in with parameters (LEVEL, MODE) in a block of channel `ch`. */
    test::ParameterPlugin* add (int ch)
    {
        auto* plugin = new test::ParameterPlugin();
        addRackPlugin (engine, *engine.getChannels()[(size_t) ch], std::unique_ptr<juce::AudioPluginInstance> (plugin));
        skin->refreshAll();
        return plugin;
    }
    ui::ph::Control& control() { return skin->getControl(); }
    ui::ph::PluginRef ref (int ch, int index = 0) { return control().pluginsOf (ch)[(size_t) index]; }
    ui::ph::ParamRef param (const ui::ph::PluginRef& r, int index) { return ui::ph::ParamRef::of (*control().pluginOf (r), index); }
    /** LEARN, the target, then a MIDI message. */
    void learn (const ui::ph::Target& target, const juce::MidiMessage& message)
    {
        control().setLearning (true);
        control().captureTarget (target);
        control().handleMidi (message);
    }

    MixerEngine engine;
    std::unique_ptr<HostRig> rig;
    std::unique_ptr<ui::PluginHostSurface> skin;
};

juce::MidiMessage cc (int channel, int number, int value)
{
    return juce::MidiMessage::controllerEvent (channel, number, value);
}
juce::MidiMessage note (int channel, int number)
{
    return juce::MidiMessage::noteOn (channel, number, (juce::uint8) 100);
}
} // namespace

class PluginHostTests final : public juce::UnitTest
{
public:
    PluginHostTests() : juce::UnitTest ("PLUGIN HOST surface", "LiveDAW") {}

    void runTest() override
    {
        beginTest ("The strips show channels 1-8, BANK moves them to 9-16, and 32 and 64 channels have 4 and 8 banks");
        {
            const std::pair<int, int> sizes[] = { { 16, 2 }, { 32, 4 }, { 64, 8 } };
            for (const auto& [channels, banks] : sizes)
            {
                MixerEngine engine (0);
                engine.configurePluginHost (channels);
                setShownProfile (ld::kProfilePluginHost);
                HostRig rig (engine);
                ui::PluginHostSurface skin (rig.ctx);
                skin.setSize (1920, 1080);
                const auto label = juce::String (channels) + " channels";
                expectEquals (skin.getNumBanks(), banks, label);
                expectEquals (skin.getBank(), 0, label);
                for (int i = 0; i < 8; ++i)
                    expectEquals (skin.getStrip (i).getChannel(), i, label + " strip " + juce::String (i + 1));
                expectEquals (skin.getStrip (0).getLcd().shown()[0], juce::String ("01"), label);
                expectEquals (skin.getBankLcd().shown()[0], juce::String ("BANK 1 / ") + juce::String (banks), label);

                skin.getBankKey (true).click();
                expectEquals (skin.getBank(), 1, label);
                for (int i = 0; i < 8; ++i)
                    expectEquals (skin.getStrip (i).getChannel(), 8 + i, label + " second bank");
                expectEquals (skin.getStrip (0).getLcd().shown()[0], juce::String ("09"), label);
                for (int b = 2; b < banks; ++b)
                    skin.getBankKey (true).click();
                expectEquals (skin.getBank(), banks - 1, label + ": the last bank");
                expectEquals (skin.getStrip (7).getChannel(), channels - 1, label + ": the last strip is the last channel");
                skin.getBankKey (true).click();
                expectEquals (skin.getBank(), 0, label + ": the banks go round");
                skin.getBankKey (false).click();
                expectEquals (skin.getBank(), banks - 1, label + ": and back");
                setShownProfile (ld::kProfileUniversal);
            }
        }

        beginTest ("SELECT changes the RACK page's channel, the data wheel steps through the channels and the bank follows it");
        {
            MixerEngine engine (0);
            engine.configurePluginHost (16);
            setShownProfile (ld::kProfilePluginHost);
            HostRig rig (engine);
            ui::PluginHostSurface skin (rig.ctx);
            skin.setSize (1920, 1080);
            expectEquals (skin.getRack().getChannel(), 0);
            expect (rig.selected == engine.getChannels()[0].get(), "channel 1 is selected at the start");
            skin.getStrip (3).getSelectKey().click();
            expect (rig.selected == engine.getChannels()[3].get());
            expectEquals (skin.getRack().getChannel(), 3);
            expectEquals (skin.getSelectedChannel(), 3);
            expect (skin.getStrip (3).getSelectKey().isOn(), "the selected strip's key is lit");
            skin.getWheel().turn (2);
            expectEquals (skin.getRack().getChannel(), 5);
            skin.getWheel().turn (-10);
            expectEquals (skin.getSelectedChannel(), 0);
            skin.getWheel().turn (9);
            expectEquals (skin.getSelectedChannel(), 9);
            expectEquals (skin.getBank(), 1, "the wheel moved the selection into the second bank");
            expectEquals (skin.getRack().getChannel(), 9);
            setShownProfile (ld::kProfileUniversal);
        }

        beginTest ("BYPASS sets the channel's bypass parameter");
        {
            MixerEngine engine (0);
            engine.configurePluginHost (16);
            setShownProfile (ld::kProfilePluginHost);
            HostRig rig (engine);
            ui::PluginHostSurface skin (rig.ctx);
            skin.setSize (1920, 1080);
            auto& key = skin.getStrip (1).getBypassKey();
            expect (! engine.getChannels()[1]->params.input.bypass.load() && ! key.isOn());
            key.click();
            expect (engine.getChannels()[1]->params.input.bypass.load() && key.isOn());
            expect (! engine.getChannels()[0]->params.input.bypass.load(), "only that channel");
            key.click();
            expect (! engine.getChannels()[1]->params.input.bypass.load());
            // A change made elsewhere shows on the key.
            engine.getChannels()[1]->params.input.bypass = true;
            skin.getStrip (1).refresh();
            expect (key.isOn());
            skin.getBankKey (true).click();
            skin.getStrip (1).getBypassKey().click(); // channel 10
            expect (engine.getChannels()[9]->params.input.bypass.load(), "the second bank's strips are channels 9-16");
            setShownProfile (ld::kProfileUniversal);
        }

        beginTest ("The RACK page's tiles match the chain in order, a drag reorders it, and a tile is bypassed and removed");
        {
            MixerEngine engine (0);
            engine.configurePluginHost (16);
            setShownProfile (ld::kProfilePluginHost);
            HostRig rig (engine);
            ui::PluginHostSurface skin (rig.ctx);
            skin.setSize (1920, 1080);
            auto& rack = skin.getRack();
            auto& strip = *engine.getChannels()[0];
            expectEquals (rack.getNumTiles(), 0);

            expect (rack.addBlock (BlockKind::Eq));
            expect (rack.addBlock (BlockKind::Dynamics));
            const int plugUid = addFakePlugin (engine, strip, "Hall Reverb");
            rack.refresh();
            expectEquals (rack.getNumTiles(), 3);
            auto uids = blockUids (strip);
            expectEquals ((int) uids.size(), 3);
            for (int i = 0; i < 3 && i < (int) uids.size(); ++i)
                expectEquals (rack.getTile (i).uid, uids[(size_t) i], "tile " + juce::String (i + 1) + " is the chain's block " + juce::String (i + 1));
            expectEquals (rack.getTile (0).name, juce::String ("EQ BLOCK"));
            expectEquals (rack.getTile (1).name, juce::String ("DYNAMICS BLOCK"));
            expectEquals (rack.getTile (2).name, juce::String ("Fake Hall Reverb"));
            expectEquals (rack.getTile (2).maker, juce::String ("Tests"));
            expectEquals (rack.getTile (2).latency, 128);
            expectEquals (rack.getTile (2).uid, plugUid);

            // A drag of the last tile to before the first.
            auto& canvas = rack.getCanvas();
            const auto centre = [&] (int i) { return rack.getTileBounds (i).getCentre().toFloat(); };
            const auto from = centre (2);
            const auto to = rack.getTileBounds (0).getPosition().toFloat() + juce::Point<float> (20.0f, 60.0f);
            canvas.mouseDown (mouseEvent (canvas, from, from));
            canvas.mouseDrag (mouseEvent (canvas, to, from));
            canvas.mouseUp (mouseEvent (canvas, to, from));
            const auto moved = blockUids (strip);
            expectEquals ((int) moved.size(), 3);
            if (moved.size() == 3)
                expect (moved[0] == uids[2] && moved[1] == uids[0] && moved[2] == uids[1], "the plug-in is now first in the chain");
            for (int i = 0; i < 3 && i < (int) moved.size(); ++i)
                expectEquals (rack.getTile (i).uid, moved[(size_t) i], "the tiles follow the new order");
            expect (rack.moveTile (0, 3), "to the end");
            expect (blockUids (strip) == std::vector<int> { uids[0], uids[1], uids[2] });
            expect (! rack.moveTile (1, 1) && ! rack.moveTile (1, 2), "dropping a tile where it is does nothing");

            // BYPASS on a tile: the plug-in block's, and a built-in block's ON.
            expect (! rack.getTile (2).bypassed);
            expect (rack.setTileBypassed (2, true) && rack.getTile (2).bypassed);
            expect (engine.getFx().describeBlock (strip.getHandle(), plugUid).bypassed);
            expect (rack.setTileBypassed (2, false) && ! rack.getTile (2).bypassed);
            expect (rack.getTile (0).bypassed, "a new EQ block is off");
            expect (rack.setTileBypassed (0, false) && ! rack.getTile (0).bypassed, "an EQ is on when it is not bypassed");

            // Copy and paste of a built-in block; remove.
            rack.copyTile (1);
            expect (rack.hasCopy());
            rack.selectTile (2);
            expect (rack.pasteTile());
            expectEquals (rack.getNumTiles(), 4);
            expectEquals (rack.getTile (3).name, juce::String ("DYNAMICS BLOCK"), "pasted after the selected tile");
            expect (rack.removeTile (3));
            expectEquals (rack.getNumTiles(), 3);
            expect (rack.removeTile (2));
            expectEquals ((int) blockUids (strip).size(), 2);
            engine.getGarbage().collectAllNow();

            // The "+" tile is the one after the last.
            expectEquals (rack.getTileBounds (rack.getNumTiles()).getWidth(), ui::ph::RackPage::kTileWidth);
            setShownProfile (ld::kProfileUniversal);
        }

        beginTest ("The dock shows the selected tile's plug-in; PIN keeps it while the channel changes and a removed block unpins");
        {
            MixerEngine engine (0);
            engine.configurePluginHost (16);
            setShownProfile (ld::kProfilePluginHost);
            HostRig rig (engine);
            ui::PluginHostSurface skin (rig.ctx);
            skin.setSize (1920, 1080);
            auto& rack = skin.getRack();
            auto& dock = rack.getDock();
            auto& ch1 = *engine.getChannels()[0];
            auto& ch2 = *engine.getChannels()[1];
            juce::AudioPluginInstance* first = nullptr;
            juce::AudioPluginInstance* second = nullptr;
            const int uid1 = addFakePlugin (engine, ch1, "One", &first);
            const int uid2 = addFakePlugin (engine, ch2, "Two", &second);
            skin.refreshAll();
            expect (dock.getChannel() == 0 && dock.getUid() == uid1, "the dock follows the first plug-in of channel 1");
            expect (dock.getContent() == ui::ph::RackDock::Content::Generic && dock.getShownPlugin() == first, "a plug-in without an editor shows generic controls");
            expectEquals (dock.getWidth(), 920, "the dock is the right half of the screen");

            skin.getStrip (1).getSelectKey().click();
            expect (dock.getChannel() == 1 && dock.getUid() == uid2 && dock.getShownPlugin() == second, "unpinned, the dock follows the channel");

            // Pinned to channel 2's plug-in, the dock stays when the channel changes.
            expect (! dock.isPinned());
            dock.getPinKey().onClick();
            expect (dock.isPinned() && dock.getPinKey().isOn());
            skin.getStrip (0).getSelectKey().click();
            expectEquals (rack.getChannel(), 0);
            expect (dock.getChannel() == 1 && dock.getUid() == uid2 && dock.getShownPlugin() == second, "pinned: the dock keeps the plug-in");
            skin.getStrip (5).getSelectKey().click();
            expect (dock.getShownPlugin() == second && dock.isPinned(), "still pinned on a channel without blocks");
            dock.getPinKey().onClick();
            expect (! dock.isPinned());
            skin.getStrip (0).getSelectKey().click();
            expect (dock.getChannel() == 0 && dock.getShownPlugin() == first, "unpinned: it follows again");

            // Removing the pinned block unpins and empties the dock.
            skin.getStrip (1).getSelectKey().click();
            dock.getPinKey().onClick();
            skin.getStrip (0).getSelectKey().click();
            expect (dock.isPinned() && dock.getShownPlugin() == second);
            dock.dropEditorFor (second);
            expect (ctxRemove (rig, engine, ch2, uid2));
            dock.refresh();
            expect (! dock.isPinned() && dock.getShownPlugin() == nullptr, "the block is gone: no longer pinned");
            engine.getGarbage().collectAllNow();
            setShownProfile (ld::kProfileUniversal);
        }

        beginTest ("The screen's pages: RACK, ROUTING, SCENES and CONTROL are built, the others say so; F10 maximises the screen; the scene keys cue and recall; the readout shows latency and CPU");
        {
            MixerEngine engine (0);
            engine.configurePluginHost (16);
            setShownProfile (ld::kProfilePluginHost);
            HostRig rig (engine);
            ui::PluginHostSurface skin (rig.ctx);
            skin.setSize (1920, 1080);
            using Page = ui::PluginHostSurface::Page;
            expect (skin.getPage() == Page::Rack && skin.getRack().isVisible() && skin.getComingSoon() != nullptr && ! skin.getComingSoon()->isVisible());
            expectEquals (juce::String (ui::PluginHostSurface::nameOf (Page::System)), juce::String ("SYSTEM"));
            for (int p = 1; p < 7; ++p)
            {
                skin.getTab ((Page) p).onClick();
                const bool built = (Page) p == Page::Routing || (Page) p == Page::Scenes || (Page) p == Page::Control;
                expect (skin.getPage() == (Page) p && ! skin.getRack().isVisible()
                            && (skin.getRouting().isVisible() || skin.getScenes().isVisible() || skin.getControlPage().isVisible()) == built && skin.getComingSoon()->isVisible() == ! built,
                        juce::String (ui::PluginHostSurface::nameOf ((Page) p)) + (built ? " shows its page" : " shows the coming-soon panel"));
            }
            skin.getTab (Page::Rack).onClick();
            expect (skin.getRack().isVisible() && ! skin.getComingSoon()->isVisible() && ! skin.getRouting().isVisible());

            // The unit's parts are in the design space, at least 44 px high and inside it.
            const auto in = [&] (juce::Component& c) { return juce::Rectangle<int> (0, 0, 1920, 1080).contains (c.getBounds()); };
            expect (in (skin.getUniversalKey()) && skin.getUniversalKey().getHeight() >= 44);
            for (int i = 0; i < 8; ++i)
            {
                auto& s = skin.getStrip (i);
                expect (in (s), "strip " + juce::String (i + 1) + " is on the panel");
                expect (s.getSelectKey().getHeight() >= 44 && s.getBypassKey().getHeight() >= 44, "touch-sized keys");
                expect (s.getKnob (0).getHeight() >= 44 && s.getKnob (1).getHeight() >= 44);
            }
            expect (skin.getBankKey (true).getHeight() >= 44 && skin.getSceneKey (0).getHeight() >= 44);
            expect (skin.getScreen().getHeight() >= 1080 * 55 / 100 && skin.getScreen().getHeight() <= 1080 * 2 / 3, "the screen is about two-thirds of the height");

            // Maximise (F10), restore (F10).
            expect (skin.skinKeyPressed (juce::KeyPress (juce::KeyPress::F10Key)));
            expect (skin.getScreen().isVisible() && ! skin.getUniversalKey().isVisible() && ! skin.getStrip (0).isVisible(), "only the screen shows");
            expect (skin.skinKeyPressed (juce::KeyPress (juce::KeyPress::F10Key)));
            expect (skin.getUniversalKey().isVisible() && skin.getStrip (0).isVisible());

            // Scenes: PREV and NEXT cue, GO recalls the cue.
            rig.show.addScene ("Verse", -1);
            rig.show.addScene ("Chorus", -1);
            rig.show.addScene ("Bridge", -1);
            skin.showChanged();
            skin.refreshAll();
            skin.getSceneLcd().refresh();
            const int start = skin.getCue();
            expect (start >= 0);
            skin.getSceneKey (-1).click();
            skin.getSceneKey (-1).click();
            expectEquals (skin.getCue(), 0);
            skin.getSceneKey (1).click();
            expectEquals (skin.getCue(), 1);
            engine.getChannels()[4]->params.input.bypass = true;
            skin.getSceneKey (0).click();
            expectEquals (rig.show.getCurrentSceneIndex(), 1);
            expectEquals (skin.getSceneLcd().shown()[1], juce::String ("Chorus"));

            // The readout: the selected channel's latency and the engine's CPU.
            skin.getReadout().refresh();
            const auto lines = skin.getReadout().shown();
            expectEquals (lines.size(), 3);
            expect (lines.size() == 3 && lines[0].contains ("CH 1") && lines[1].contains ("smp") && lines[1].contains ("ms") && lines[2].contains ("CPU"));
            setShownProfile (ld::kProfileUniversal);
        }

        beginTest ("ROUTING: the INPUTS and OUTPUTS grids patch channels to the interface and the engine follows");
        {
            for (const int channels : { 16, 32, 64 })
            {
                MixerEngine engine (0);
                engine.configurePluginHost (channels);
                setShownProfile (ld::kProfilePluginHost);
                HostRig rig (engine);
                ui::PluginHostSurface skin (rig.ctx);
                skin.setSize (1920, 1080);
                auto& routing = skin.getRouting();
                const auto label = juce::String (channels) + " channels";
                for (auto* grid : { &routing.getInputsGrid(), &routing.getOutputsGrid() })
                    expect (grid->getNumRows() == channels && grid->getNumColumns() == channels, label + ": a square grid that scrolls");
                expectEquals (routing.getKeysGrid().getNumColumns(), 1 + 2 * channels, label + ": NONE and each channel's IN and OUT");
                // The factory patch is 1:1.
                for (int i : { 0, channels - 1 })
                {
                    expect (routing.getInputsGrid().getState (i, i) == ui::universal::PatchGrid::On, label);
                    expect (routing.getOutputsGrid().getState (i, i) == ui::universal::PatchGrid::On, label);
                }
                setShownProfile (ld::kProfileUniversal);
            }

            MixerEngine engine (0);
            engine.configurePluginHost (16);
            setShownProfile (ld::kProfilePluginHost);
            HostRig rig (engine);
            ui::PluginHostSurface skin (rig.ctx);
            skin.setSize (1920, 1080);
            using Page = ui::PluginHostSurface::Page;
            skin.getTab (Page::Routing).onClick();
            auto& routing = skin.getRouting();
            expect (routing.isVisible() && ! skin.getComingSoon()->isVisible() && ! skin.getRack().isVisible());
            expect (routing.getView() == ui::ph::RoutingPage::View::Inputs && routing.getInputsGrid().isVisible() && ! routing.getOutputsGrid().isVisible());
            test::OfflineRig audio (engine, 16, 16, false);
            for (int i = 0; i < 16; ++i)
                audio.setInput (i, 0.02f * (float) (i + 1));
            const auto level = [] (int input) { return 0.02f * (float) (input + 1); };
            audio.run (30);
            expectWithinAbsoluteError (audio.out (0), level (0), 0.002f);
            expectWithinAbsoluteError (audio.out (7), level (7), 0.002f);

            // Channel 1 takes interface input 6; one input per channel, so input 1 is let go.
            auto& in = routing.getInputsGrid();
            in.tap (0, 5);
            expect (in.getState (0, 5) == ui::universal::PatchGrid::On && in.getState (0, 0) == ui::universal::PatchGrid::Off);
            expectEquals (engine.getChannels()[0]->params.inputLeft.load(), 5);
            expectEquals (engine.getChannels()[0]->params.inputRight.load(), -1);
            audio.run (30);
            expectWithinAbsoluteError (audio.out (0), level (5), 0.002f);
            expectWithinAbsoluteError (audio.out (5), level (5), 0.002f);
            in.tap (0, 5); // the lit point again: unpatched
            expectEquals (engine.getChannels()[0]->params.inputLeft.load(), -1);
            audio.run (30);
            expectWithinAbsoluteError (audio.out (0), 0.0f, 0.002f);
            in.tap (0, 0);

            // OUTPUTS: output 3 plays channel 9.
            routing.getViewKey (ui::ph::RoutingPage::View::Outputs).onClick();
            expect (routing.getOutputsGrid().isVisible() && ! routing.getInputsGrid().isVisible());
            auto& out = routing.getOutputsGrid();
            out.tap (2, 8);
            expect (out.getState (2, 8) == ui::universal::PatchGrid::On && out.getState (2, 2) == ui::universal::PatchGrid::Off);
            const auto code = engine.getOutputSource (2);
            expect (DirectOut::fromCode (code).has_value() && DirectOut::fromCode (code)->number == 9 && DirectOut::fromCode (code)->post);
            audio.run (30);
            expectWithinAbsoluteError (audio.out (2), level (8), 0.002f);
            expectWithinAbsoluteError (audio.out (8), level (8), 0.002f);
            out.tap (2, 8);
            expectEquals (engine.getOutputSource (2), -1);
            audio.run (30);
            expectWithinAbsoluteError (audio.out (2), 0.0f, 0.002f);

            // RESET 1:1 asks twice.
            in.tap (4, 11);
            out.tap (2, 11);
            auto& reset = routing.getResetKey();
            reset.onClick();
            expect (routing.isResetArmed() && engine.getChannels()[4]->params.inputLeft.load() == 11, "the first press only arms it");
            reset.onClick();
            expect (! routing.isResetArmed());
            for (int i = 0; i < 16; ++i)
            {
                expectEquals (engine.getChannels()[(size_t) i]->params.inputLeft.load(), i);
                expect (in.getState (i, i) == ui::universal::PatchGrid::On && out.getState (i, i) == ui::universal::PatchGrid::On);
            }
            audio.run (30);
            expectWithinAbsoluteError (audio.out (2), level (2), 0.002f);
            expectWithinAbsoluteError (audio.out (4), level (4), 0.002f);
            setShownProfile (ld::kProfileUniversal);
        }

        beginTest ("ROUTING > KEYS: a plug-in with a sidechain gets a row; a sidechain choice reaches the plug-in, before or after the chain");
        {
            MixerEngine engine (0);
            engine.configurePluginHost (16);
            setShownProfile (ld::kProfilePluginHost);
            HostRig rig (engine);
            ui::PluginHostSurface skin (rig.ctx);
            skin.setSize (1920, 1080);
            using Page = ui::PluginHostSurface::Page;
            using Key = PluginChain::Key;
            auto& ch3 = *engine.getChannels()[2];
            auto* keyed = new test::SidechainPlugin();
            const int plain = addPluginBlock (engine, ch3, std::make_unique<test::FakePlugin> (1.0f, test::FakePlugin::Mode::Gain, "Plain"));
            const int slot = addPluginBlock (engine, ch3, std::unique_ptr<juce::AudioPluginInstance> (keyed));
            auto& fx = engine.getFx();
            expect (! fx.chainMemberHasSidechain (plain, 0) && fx.chainMemberHasSidechain (slot, 0), "only the plug-in with a second input bus can be keyed");
            expect (! fx.chainMemberHasSidechain (0, 0), "an FX slot's plug-in is not keyed");
            expect (! fx.setChainMemberKey (plain, 0, Key { 5, false }), "a plug-in without a sidechain refuses a key");

            skin.selectChannel (2);
            skin.getTab (Page::Routing).onClick();
            auto& routing = skin.getRouting();
            routing.getViewKey (ui::ph::RoutingPage::View::Keys).onClick();
            expectEquals (routing.getNumKeyRows(), 1, "one row: the plug-in that has a sidechain");
            expectEquals (routing.getKeyRow (0).name, juce::String ("Fake Keyed"));
            expect (routing.getKeyRow (0).key.isNone() && routing.getKeysGrid().isVisible());
            expectEquals (ui::ph::RoutingPage::describeKey ({}), juce::String ("NONE"));
            expectEquals (ui::ph::RoutingPage::describeKey ({ 5, true }), juce::String ("CH 6 OUT"));
            expect (ui::ph::RoutingPage::keyAt (ui::ph::RoutingPage::keyColumn ({ 7, true })) == Key { 7, true });

            test::OfflineRig audio (engine, 16, 16, false);
            for (int i = 0; i < 16; ++i)
                audio.setInput (i, 0.02f * (float) (i + 1));
            audio.run (30);
            expectWithinAbsoluteError (keyed->keyLevel.load(), 0.0f, 1.0e-6f, "no key: silence on the sidechain");

            // The key is channel 6's input (0.12), before its chain.
            auto& grid = routing.getKeysGrid();
            grid.tap (0, ui::ph::RoutingPage::keyColumn ({ 5, false }));
            expect (grid.getState (0, ui::ph::RoutingPage::keyColumn ({ 5, false })) == ui::universal::PatchGrid::On);
            expect (fx.getChainMemberKey (slot, 0) == Key { 5, false } && fx.getChain (slot) != nullptr, "a keyed plug-in is a chain of one");
            expect (routing.getKeyRow (0).key == Key { 5, false });
            audio.run (30);
            expectWithinAbsoluteError (keyed->keyLevel.load(), 0.12f, 0.003f, "the key reaches the plug-in's sidechain");

            // After its chain: channel 6's level is lowered by 20 dB.
            engine.getChannels()[5]->params.faderDb = -20.0f;
            audio.run (30);
            expectWithinAbsoluteError (keyed->keyLevel.load(), 0.12f, 0.003f, "pre-chain: the fader does not matter");
            grid.tap (0, ui::ph::RoutingPage::keyColumn ({ 5, true }));
            expect (fx.getChainMemberKey (slot, 0) == Key { 5, true });
            audio.run (30);
            expectWithinAbsoluteError (keyed->keyLevel.load(), 0.012f, 0.002f, "post-chain: after the fader");

            // A channel cannot key itself after its chain; before it is fine.
            grid.tap (0, ui::ph::RoutingPage::keyColumn ({ 2, true }));
            expect (fx.getChainMemberKey (slot, 0) == Key { 5, true }, "refused: the key stays");
            expect (! routing.setKey (0, { 2, true }) && ! fx.setChainMemberKey (slot, 0, Key { 2, true }));
            expect (fx.getChainMemberKey (slot, 0) == Key { 5, true });
            grid.tap (0, ui::ph::RoutingPage::keyColumn ({ 2, false }));
            expect (fx.getChainMemberKey (slot, 0) == Key { 2, false }, "its own input is allowed");
            audio.run (30);
            expectWithinAbsoluteError (keyed->keyLevel.load(), 0.06f, 0.003f, "channel 3's own input");

            // Another channel's KEYS page has no rows; the lit point again takes the key away.
            skin.selectChannel (6);
            expectEquals (routing.getNumKeyRows(), 0);
            expect (routing.getKeysNote().isNotEmpty() && ! routing.getKeysGrid().isVisible());
            skin.selectChannel (2);
            grid.tap (0, ui::ph::RoutingPage::keyColumn ({ 2, false }));
            expect (fx.getChainMemberKey (slot, 0).isNone());
            audio.run (30);
            expectWithinAbsoluteError (keyed->keyLevel.load(), 0.0f, 1.0e-6f);
            expect (routing.getKeyRow (0).key.isNone());

            // A block's plug-in is gone: the key goes with it.
            expect (fx.setChainMemberKey (slot, 0, Key { 4, false }));
            fx.setPlugin (slot, nullptr);
            expectEquals (ld::ld_plugin_block_key (engine.getHandle(), FxSlots::blockPid (slot), nullptr), -1);
            engine.getGarbage().collectAllNow();
            setShownProfile (ld::kProfileUniversal);
        }

        beginTest ("A sidechain choice is saved with the show and the scenes, and a recall puts it back");
        {
            MixerEngine engine (0);
            engine.configurePluginHost (16);
            setShownProfile (ld::kProfilePluginHost);
            HostRig rig (engine);
            using Key = PluginChain::Key;
            auto& ch = *engine.getChannels()[3];
            auto* keyed = new test::SidechainPlugin();
            const int slot = addPluginBlock (engine, ch, std::unique_ptr<juce::AudioPluginInstance> (keyed));
            auto& fx = engine.getFx();
            expect (fx.setChainMemberKey (slot, 0, Key { 9, true }));

            // The chain's saved form carries it (and reads back).
            auto xml = fx.getChain (slot)->createXml();
            expect (xml != nullptr && xml->getBoolAttribute ("keys"));
            std::vector<PluginChain::Saved> saved;
            expect (PluginChain::parseXml (*xml, saved) && saved.size() == 1);
            expect (saved.size() == 1 && saved[0].hasKey && saved[0].key == Key { 9, true });

            rig.show.addScene ("Keyed", -1);
            const auto doc = rig.show.toXml();
            expect (doc != nullptr && doc->toString().contains ("keyChannel"), "the show's file holds the key");

            expect (fx.setChainMemberKey (slot, 0, Key {}));
            expect (fx.getChainMemberKey (slot, 0).isNone());
            rig.show.recallScene (0);
            expect (fx.getChainMemberKey (slot, 0) == Key { 9, true }, "the scene brings the key back");
            // The engine has it too (a block's key is the chain's).
            bool post = false;
            expectEquals (ld::ld_plugin_block_key (engine.getHandle(), FxSlots::blockPid (slot), &post), 9);
            expect (post);

            // A scene saved without a key puts the plug-in back to none.
            expect (fx.setChainMemberKey (slot, 0, Key {}));
            rig.show.addScene ("Plain", -1);
            expect (fx.setChainMemberKey (slot, 0, Key { 1, false }));
            rig.show.recallScene (1);
            expect (fx.getChainMemberKey (slot, 0).isNone());
            expectEquals (ld::ld_plugin_block_key (engine.getHandle(), FxSlots::blockPid (slot), &post), -1);
            engine.getGarbage().collectAllNow();
            setShownProfile (ld::kProfileUniversal);
        }

        beginTest ("A scene recalls a channel's whole rack: states, bypass, order, which plug-ins are there, keys and the patch");
        {
            MixerEngine engine (0);
            engine.configurePluginHost (16);
            setShownProfile (ld::kProfilePluginHost);
            HostRig rig (engine);
            auto& ch = *engine.getChannels()[0];
            auto& fx = engine.getFx();
            auto* one = new test::FakePlugin (0.5f, test::FakePlugin::Mode::Gain, "One");
            auto* three = new test::FakePlugin (0.25f, test::FakePlugin::Mode::Gain, "Three");
            const int uid1 = addRackPlugin (engine, ch, std::unique_ptr<juce::AudioPluginInstance> (one));
            const int uid2 = addRackPlugin (engine, ch, std::make_unique<test::SidechainPlugin>());
            const int uid3 = addRackPlugin (engine, ch, std::unique_ptr<juce::AudioPluginInstance> (three));
            const auto slotOf = [&] (int uid) { return FxSlots::blockSlotOf (ch.getHandle(), uid); };
            expect (fx.setChainMemberKey (slotOf (uid2), 0, PluginChain::Key { 5, false }));
            fx.setChainMemberBypassed (slotOf (uid3), 0, true);
            ch.params.input.bypass = true;
            ch.params.inputLeft = 7;
            const auto outCode = DirectOut { StripKind::Channel, 1, 0, true }.code();
            engine.routeOutputSource (2, outCode);
            expect (pluginUids (ch) == std::vector<int> { uid1, uid2, uid3 });
            const int scene = rig.show.addScene ("Verse", -1);
            expect (rig.show.getScene (scene)->scope == state::AllSections, "a PLUGIN HOST scene holds the patch too");

            // The rack is changed in every way: a state, a block taken out, another put in, the order, the bypass, the key, the patch.
            one->gain = 0.9f;
            expect (ctxRemove (rig, engine, ch, uid2));
            const int uid4 = addRackPlugin (engine, ch, std::make_unique<test::FakePlugin> (1.0f, test::FakePlugin::Mode::Gain, "Four"));
            expect (ch.moveBlock (uid3, rackInsertionPoint (ch) - 2));
            fx.setChainMemberBypassed (slotOf (uid3), 0, false);
            ch.params.input.bypass = false;
            ch.params.inputLeft = 3;
            engine.routeOutputSource (2, -1);
            expect (pluginUids (ch) != std::vector<int> { uid1, uid2, uid3 });

            const auto result = rig.show.recallScene (scene);
            expect (result.warnings.isEmpty(), result.warnings.joinIntoString (" | "));
            expect (pluginUids (ch) == std::vector<int> { uid1, uid2, uid3 }, "the scene's order; the block that was taken out is back, the one put in is gone");
            expect (! ch.getBlock (uid4).isValid());
            expect (fx.getPlugin (slotOf (uid1)) == one, "a plug-in that is there keeps its instance");
            expectWithinAbsoluteError (one->gain.load(), 0.5f, 1.0e-6f, "its state");
            auto* again = fx.getPlugin (slotOf (uid2));
            expect (again != nullptr && again->getName() == "Fake Keyed", "the plug-in that was gone is made again from its description");
            expect (fx.getChainMemberKey (slotOf (uid2), 0) == PluginChain::Key { 5, false }, "its key");
            expect (fx.isChainMemberBypassed (slotOf (uid3), 0), "bypass");
            expectWithinAbsoluteError (three->gain.load(), 0.25f, 1.0e-6f);
            expect (ch.params.input.bypass.load(), "the channel's BYPASS");
            expectEquals (ch.params.inputLeft.load(), 7);
            expectEquals (engine.getOutputSource (2), outCode);

            // The audio runs on through it (the channel's BYPASS passes the input straight).
            test::OfflineRig audio (engine, 16, 16, false);
            audio.setInput (7, 0.16f);
            audio.run (30);
            expectWithinAbsoluteError (audio.out (0), 0.16f, 0.003f);
            engine.getGarbage().collectAllNow();
            setShownProfile (ld::kProfileUniversal);
        }

        beginTest ("A safe plug-in, and a safe channel, are left alone by a recall");
        {
            MixerEngine engine (0);
            engine.configurePluginHost (16);
            setShownProfile (ld::kProfilePluginHost);
            HostRig rig (engine);
            ui::PluginHostSurface skin (rig.ctx);
            skin.setSize (1920, 1080);
            auto& ch = *engine.getChannels()[0];
            auto& other = *engine.getChannels()[1];
            auto& fx = engine.getFx();
            auto* a = new test::FakePlugin (0.5f, test::FakePlugin::Mode::Gain, "A");
            auto* b = new test::FakePlugin (0.25f, test::FakePlugin::Mode::Gain, "B");
            auto* c = new test::FakePlugin (0.75f, test::FakePlugin::Mode::Gain, "C");
            const int uidA = addRackPlugin (engine, ch, std::unique_ptr<juce::AudioPluginInstance> (a));
            const int uidB = addRackPlugin (engine, ch, std::unique_ptr<juce::AudioPluginInstance> (b));
            const int uidC = addRackPlugin (engine, other, std::unique_ptr<juce::AudioPluginInstance> (c));
            const int scene = rig.show.addScene ("Verse", -1);
            skin.refreshAll();

            // The SAFES panel lists every channel, and under each the plug-ins of its rack.
            auto& page = skin.getScenes();
            expectEquals (page.getNumSafeRows(), 16 + 3);
            expect (page.getSafeRow (0).isChannel() && page.getSafeRow (0).channel == 0);
            expect (! page.getSafeRow (1).isChannel() && page.getSafeRow (1).uid == uidA && page.getSafeRow (1).name == "Fake A");
            expect (page.getSafeRow (2).uid == uidB && page.getSafeRow (3).isChannel() && page.getSafeRow (3).channel == 1);
            expect (page.getSafeRow (4).uid == uidC && page.getSafeRow (5).channel == 2);
            expect (! page.isSafe (1));
            page.toggleSafe (1); // A
            page.toggleSafe (3); // channel 2
            expect (page.isSafe (1) && ch.isBlockSafe (uidA) && ! ch.isBlockSafe (uidB) && other.params.recallSafe.load());
            expect (page.isSafe (3) && ! page.isSafe (0) && ! page.isSafe (2));

            // Everything is changed: A (safe) and B (not), a safe plug-in and another put into channel 1, the patch of both.
            a->gain = 0.8f;
            b->gain = 0.7f;
            c->gain = 0.1f;
            const int uidD = addRackPlugin (engine, ch, std::make_unique<test::FakePlugin> (1.0f, test::FakePlugin::Mode::Gain, "D"));
            const int uidE = addRackPlugin (engine, ch, std::make_unique<test::FakePlugin> (1.0f, test::FakePlugin::Mode::Gain, "E"));
            ch.setBlockSafe (uidD, true);
            ch.params.inputLeft = 9;
            other.params.inputLeft = 9;
            expect (pluginUids (ch).size() == 4);

            const auto result = rig.show.recallScene (scene);
            expect (result.warnings.isEmpty(), result.warnings.joinIntoString (" | "));
            expectWithinAbsoluteError (a->gain.load(), 0.8f, 1.0e-6f, "a safe plug-in keeps its state");
            expectWithinAbsoluteError (b->gain.load(), 0.25f, 1.0e-6f, "the others take the scene's");
            expect (ch.getBlock (uidD).isValid() && fx.getPlugin (FxSlots::blockSlotOf (ch.getHandle(), uidD)) != nullptr, "a safe plug-in stays though the scene lacks it");
            expect (! ch.getBlock (uidE).isValid(), "one that is not safe goes");
            expectEquals (ch.params.inputLeft.load(), 0);
            expectWithinAbsoluteError (c->gain.load(), 0.1f, 1.0e-6f, "a safe channel is left alone altogether");
            expectEquals (other.params.inputLeft.load(), 9);

            // Not safe any more: the next recall brings them in (rows: CH 1, A, B, D, CH 2, C ...).
            page.refresh();
            expect (page.getSafeRow (3).uid == uidD && page.getSafeRow (4).isChannel() && page.getSafeRow (4).channel == 1);
            page.toggleSafe (1);
            page.toggleSafe (4);
            rig.show.recallScene (scene);
            expectWithinAbsoluteError (a->gain.load(), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (c->gain.load(), 0.75f, 1.0e-6f);
            expect (ch.getBlock (uidD).isValid(), "D is still safe, so it stays");
            engine.getGarbage().collectAllNow();
            setShownProfile (ld::kProfileUniversal);
        }

        beginTest ("The safes are saved with the show");
        {
            MixerEngine engine (0);
            engine.configurePluginHost (16);
            setShownProfile (ld::kProfilePluginHost);
            HostRig rig (engine);
            auto& ch = *engine.getChannels()[2];
            const int keep = addRackPlugin (engine, ch, std::make_unique<test::FakePlugin> (0.5f, test::FakePlugin::Mode::Gain, "Keep"));
            const int plain = addRackPlugin (engine, ch, std::make_unique<test::FakePlugin> (0.5f, test::FakePlugin::Mode::Gain, "Plain"));
            ch.setBlockSafe (keep, true);
            engine.getChannels()[4]->params.recallSafe = true;
            const auto xml = rig.show.toXml();
            expect (xml != nullptr);

            MixerEngine engine2 (0);
            HostRig rig2 (engine2);
            expect (rig2.show.loadFromXml (*xml, true).wasOk());
            expectEquals ((int) engine2.getChannels().size(), 16);
            auto& back = *engine2.getChannels()[2];
            expect (pluginUids (back) == std::vector<int> { keep, plain });
            expect (back.isBlockSafe (keep) && ! back.isBlockSafe (plain), "the plug-in's safe comes back with it");
            expect (engine2.getChannels()[4]->params.recallSafe.load() && ! engine2.getChannels()[3]->params.recallSafe.load(), "and the channel's");
            engine.getGarbage().collectAllNow();
            engine2.getGarbage().collectAllNow();
            setShownProfile (ld::kProfileUniversal);
        }

        beginTest ("GLIDE moves the continuous parameters to the scene's values over the fade time, then applies the whole state; SNAP applies it at once");
        {
            MixerEngine engine (0);
            engine.configurePluginHost (16);
            setShownProfile (ld::kProfilePluginHost);
            HostRig rig (engine);
            auto& ch = *engine.getChannels()[0];
            auto* plugin = new test::ParameterPlugin();
            addRackPlugin (engine, ch, std::unique_ptr<juce::AudioPluginInstance> (plugin));
            *plugin->level = 0.8f;
            *plugin->mode = 2;
            plugin->tag = 7;
            const int target = rig.show.addScene ("Target", -1);
            const int snap = rig.show.addScene ("Snap", -1);
            expect (! rig.show.getScene (target)->glide, "SNAP is the default");
            rig.show.setSceneFadeSeconds (target, 0.6);
            rig.show.setSceneGlide (target, true);
            expect (rig.show.getScene (target)->glide && rig.show.getScene (snap)->fadeSeconds == 0.0);

            const auto reset = [&]
            {
                *plugin->level = 0.1f;
                *plugin->mode = 0;
                plugin->tag = 1;
            };
            reset();
            rig.show.recallScene (target);
            expect (rig.show.isGliding());
            expectEquals (plugin->mode->getIndex(), 2, "a stepped parameter takes the scene's value when the recall starts");
            expectEquals (plugin->tag.load(), 1, "the whole state comes at the end");
            expect (plugin->level->get() < 0.35f, "the continuous parameter starts where it was");
            expect (waitFor (rig.show, [&] { return plugin->level->get() > 0.3f; }, 1000));
            const float middle = plugin->level->get();
            expect (middle > 0.3f && middle < 0.8f && plugin->tag.load() == 1, "on its way");
            expect (waitFor (rig.show, [&] { return ! rig.show.isGliding(); }, 3000));
            expectWithinAbsoluteError (plugin->level->get(), 0.8f, 1.0e-3f, "it arrives");
            expectEquals (plugin->tag.load(), 7);
            expectEquals (plugin->mode->getIndex(), 2);

            // A fade time of 0 snaps, and so does a SNAP scene with a fade time.
            reset();
            rig.show.setSceneFadeSeconds (target, 0.0);
            rig.show.recallScene (target);
            expect (! rig.show.isGliding() && plugin->tag.load() == 7);
            reset();
            rig.show.setSceneFadeSeconds (snap, 2.0);
            rig.show.recallScene (snap);
            expect (! rig.show.isGliding());
            expectWithinAbsoluteError (plugin->level->get(), 0.8f, 1.0e-3f);
            expectEquals (plugin->tag.load(), 7);

            // A new recall ends a glide that is running: the scene's state is the one that counts.
            reset();
            rig.show.setSceneFadeSeconds (target, 5.0);
            rig.show.recallScene (target);
            expect (rig.show.isGliding());
            reset();
            rig.show.recallScene (snap);
            expect (! rig.show.isGliding() && plugin->tag.load() == 7);
            engine.getGarbage().collectAllNow();
            setShownProfile (ld::kProfileUniversal);
        }

        beginTest ("SCENES page: store, recall, rename, delete and reorder; the cue stays with the PREV, GO and NEXT keys");
        {
            MixerEngine engine (0);
            engine.configurePluginHost (16);
            setShownProfile (ld::kProfilePluginHost);
            HostRig rig (engine);
            ui::PluginHostSurface skin (rig.ctx);
            skin.setSize (1920, 1080);
            using Page = ui::PluginHostSurface::Page;
            auto& page = skin.getScenes();
            auto& show = rig.show;
            skin.getTab (Page::Scenes).onClick();
            expect (page.isVisible() && ! skin.getComingSoon()->isVisible() && ! skin.getRack().isVisible());
            expectEquals (page.getNumScenes(), 0);
            expect (! page.getRecallKey().isEnabled() && page.getStoreKey().isEnabled());

            // The page's keys and rows are touch sized in the design space (the surface scales the whole screen).
            for (auto* key : { &page.getRecallKey(), &page.getStoreKey(), &page.getOverwriteKey(), &page.getRenameKey(), &page.getDeleteKey(),
                               &page.getUpKey(), &page.getDownKey(), &page.getSnapKey(), &page.getGlideKey(), &page.getTimeDownKey(), &page.getTimeUpKey() })
                expect (key->getHeight() >= 44 && key->getWidth() >= 44, key->getButtonText());
            expect (ui::ph::ScenesPage::kRow >= 44);

            auto& fader = engine.getChannels()[0]->params.faderDb;
            fader = -3.0f;
            page.getStoreKey().onClick();
            fader = -6.0f;
            page.getStoreKey().onClick();
            fader = -9.0f;
            page.getStoreKey().onClick();
            expectEquals (page.getNumScenes(), 3);
            expectEquals (show.getScene (2)->name, juce::String ("Scene 3"));
            expectEquals (skin.getCue(), 2, "a stored scene is cued");
            expect (page.getSceneRow (2).cued && ! page.getSceneRow (0).cued);
            expect (skin.getSceneLcd().shown()[0].startsWith ("CUE 3 / 3"));

            // PREV and NEXT of the surface move the cue the page shows; a tap on a row moves the surface's.
            skin.getSceneKey (-1).click();
            expectEquals (skin.getCue(), 1);
            expect (page.getSceneRow (1).cued && ! page.getSceneRow (2).cued);
            page.selectScene (0);
            expectEquals (skin.getCue(), 0);
            expectEquals (skin.getSceneLcd().shown()[0], juce::String ("CUE 1 / 3"));
            skin.getSceneKey (1).click();
            skin.getSceneKey (1).click();
            skin.getSceneKey (1).click();
            expectEquals (skin.getCue(), 2, "NEXT stops at the last scene");
            expect (page.getSceneRow (2).cued);

            // RECALL is GO: the cued scene is recalled and is the active one.
            page.selectScene (0);
            page.getRecallKey().onClick();
            expectEquals (show.getCurrentSceneIndex(), 0);
            expectWithinAbsoluteError (fader.load(), -3.0f, 0.01f);
            expect (page.getSceneRow (0).active && page.getSceneRow (0).cued && ! page.getSceneRow (1).active);
            skin.getSceneKey (1).click();
            skin.getSceneKey (0).click();
            expectEquals (show.getCurrentSceneIndex(), 1);
            expectWithinAbsoluteError (fader.load(), -6.0f, 0.01f);
            expect (page.getSceneRow (1).active);

            // RENAME: the keyboard hands the name to rename().
            page.rename (1, "  Chorus ");
            expectEquals (show.getScene (1)->name, juce::String ("Chorus"));
            page.rename (1, "   ");
            expectEquals (show.getScene (1)->name, juce::String ("Chorus"), "an empty name is ignored");
            expectEquals (page.getSceneRow (1).name, juce::String ("Chorus"));

            // The fade mode and time of the cued scene.
            expect (page.getSnapKey().isOn() && ! page.getGlideKey().isOn());
            page.getGlideKey().onClick();
            expect (show.getScene (1)->glide && page.getGlideKey().isOn() && ! page.getSnapKey().isOn());
            expect (page.getSceneRow (1).glide);
            page.getTimeUpKey().onClick();
            expectEquals (show.getScene (1)->fadeSeconds, 0.5);
            page.getTimeUpKey().onClick();
            page.getTimeUpKey().onClick();
            expectEquals (show.getScene (1)->fadeSeconds, 2.0);
            page.getTimeDownKey().onClick();
            expectEquals (show.getScene (1)->fadeSeconds, 1.0);
            expectEquals (ui::ph::ScenesPage::describeTime (0.5), juce::String ("0.5 s"));
            expectEquals (ui::ph::ScenesPage::describeTime (0.0), juce::String ("NO FADE"));
            page.getSnapKey().onClick();
            expect (! show.getScene (1)->glide);

            // REORDER: the cue goes with the scene.
            page.selectScene (0);
            page.getUpKey().onClick();
            expectEquals (show.getScene (0)->name, juce::String ("Scene 1"), "the first scene can't go up");
            page.selectScene (1);
            page.getUpKey().onClick();
            expectEquals (show.getScene (0)->name, juce::String ("Chorus"));
            expectEquals (show.getScene (1)->name, juce::String ("Scene 1"));
            expectEquals (skin.getCue(), 0);
            expect (page.getSceneRow (0).cued);
            page.getDownKey().onClick();
            expectEquals (skin.getCue(), 1);
            expectEquals (show.getScene (0)->name, juce::String ("Scene 1"));

            // OVERWRITE and DELETE ask twice.
            show.setSceneFadeSeconds (1, 0.0);
            fader = -12.0f;
            page.getOverwriteKey().onClick();
            expect (page.isArmed() && page.getOverwriteKey().getButtonText() == "CONFIRM");
            page.selectScene (1); // another press elsewhere lets go
            expect (! page.isArmed() && page.getOverwriteKey().getButtonText() == "OVERWRITE");
            page.getOverwriteKey().onClick();
            page.getOverwriteKey().onClick();
            expect (! page.isArmed());
            fader = 0.0f;
            show.recallScene (1);
            expectWithinAbsoluteError (fader.load(), -12.0f, 0.01f, "the cued scene now holds the console as it was when it was stored");

            page.selectScene (2);
            page.getDeleteKey().onClick();
            expect (page.isArmed() && page.getNumScenes() == 3 && page.getDeleteKey().getButtonText() == "CONFIRM");
            page.getDeleteKey().onClick();
            expectEquals (page.getNumScenes(), 2);
            expectEquals (skin.getCue(), 1, "the cue stays on the scenes that are left");
            for (int i = 0; i < 4; ++i)
                page.getDeleteKey().onClick();
            expectEquals (page.getNumScenes(), 0);
            expectEquals (skin.getCue(), -1);
            expect (! page.getRecallKey().isEnabled() && ! page.getDeleteKey().isEnabled());
            engine.getGarbage().collectAllNow();
            setShownProfile (ld::kProfileUniversal);
        }

        beginTest ("Hot knobs: ASSIGN HOT takes the parameter that is touched, a knob turns it, the ring follows and the LCD names it");
        {
            Bench b;
            auto* plugin = b.add (0);
            auto& control = b.control();
            auto& rack = b.skin->getRack();
            auto& dock = rack.getDock();
            auto& strip = b.skin->getStrip (0);
            rack.selectTile (0);
            expect (dock.getContent() == ui::ph::RackDock::Content::Generic);
            expect (dock.getHotKey (0).isVisible() && dock.getHotKey (1).isVisible() && dock.getHotKey (0).getHeight() >= 44);
            strip.refresh();
            expect (! strip.getKnob (0).isAssigned() && ! strip.getKnob (1).isAssigned(), "unassigned knobs are dim");
            const auto ref = b.ref (0);

            dock.getHotKey (0).onClick();
            expectEquals (dock.getAssigning(), 0);
            expectEquals (dock.getHotKey (0).getButtonText(), juce::String ("TOUCH A CONTROL"));
            plugin->level->beginChangeGesture(); // what the plug-in's editor, or a tap on a generic slider, does
            plugin->level->endChangeGesture();
            expectEquals (dock.getAssigning(), -1);
            expectEquals (control.controlOf (ref).hot[0].name, juce::String ("Level"));
            dock.getHotKey (1).onClick();
            plugin->mode->beginChangeGesture();
            plugin->mode->endChangeGesture();
            expectEquals (control.controlOf (ref).hot[1].name, juce::String ("Mode"));
            expectEquals (dock.getHotKey (0).getButtonText(), juce::String ("HOT 1: LEVEL"));
            plugin->level->beginChangeGesture(); // not assigning: a touch does nothing
            plugin->level->endChangeGesture();
            expect (control.controlOf (ref).hot[0].name == "Level" && ! control.isLearning());

            strip.refresh();
            expect (strip.getKnob (0).isAssigned() && strip.getKnob (1).isAssigned());
            expectWithinAbsoluteError (strip.getKnob (0).getValue(), 0.5f, 0.01f);
            strip.getKnob (0).onTurn (0.2f);
            expectWithinAbsoluteError (plugin->level->get(), 0.7f, 0.01f, "a turn sets the parameter");
            expectWithinAbsoluteError (strip.getKnob (0).getValue(), 0.7f, 0.01f, "and the ring follows");
            strip.getKnob (0).onTurn (-1.0f);
            expectWithinAbsoluteError (plugin->level->get(), 0.0f, 1.0e-6f, "it stops at the end");
            strip.getKnob (1).onTurn (0.1f);
            expectEquals (plugin->mode->getIndex(), 0, "a stepped parameter waits until the turns add up to a step");
            strip.getKnob (1).onTurn (0.2f);
            expectEquals (plugin->mode->getIndex(), 1);
            expectWithinAbsoluteError (strip.getKnob (1).getValue(), 0.5f, 0.01f);

            strip.getKnob (0).onTouch (false);
            strip.getLcd().refresh();
            expectEquals (strip.getLcd().shown()[0], juce::String ("LEVEL"), "the LCD names the parameter that is touched");
            expect (strip.getKnob (0).getHeight() >= 44);

            // A parameter is on one knob only.
            expect (control.assignHot (ref, 1, b.param (ref, 0)));
            expect (! control.controlOf (ref).hot[0].isSet() && control.controlOf (ref).hot[1].name == "Level");
            control.clearHot (ref, 1);
            strip.refresh();
            expect (! control.controlOf (ref).hasHot() && ! strip.getKnob (1).isAssigned());
        }

        beginTest ("The focused plug-in: the one selected on RACK for the shown channel, else the first that has hot parameters");
        {
            Bench b;
            auto* a = b.add (0);
            auto* bb = b.add (0);
            addRackPlugin (b.engine, *b.engine.getChannels()[0], std::make_unique<test::FakePlugin> (1.0f, test::FakePlugin::Mode::Gain, "Plain"));
            b.add (1);
            auto& control = b.control();
            auto& rack = b.skin->getRack();
            juce::ignoreUnused (a, bb);
            expectEquals ((int) control.pluginsOf (0).size(), 3);
            const auto refA = b.ref (0, 0), refB = b.ref (0, 1), refD = b.ref (1, 0);
            expect (control.assignHot (refA, 0, b.param (refA, 0)));
            expect (control.assignHot (refB, 1, b.param (refB, 1)));
            expect (control.assignHot (refD, 0, b.param (refD, 0)));

            rack.selectTile (1);
            expect (control.knobPlugin (0) == refB, "the plug-in selected on RACK");
            b.skin->getStrip (0).refresh();
            expect (! b.skin->getStrip (0).getKnob (0).isAssigned() && b.skin->getStrip (0).getKnob (1).isAssigned(), "its knobs: only the one it has");
            rack.selectTile (0);
            expect (control.knobPlugin (0) == refA);
            rack.selectTile (2);
            expect (control.knobPlugin (0) == refA, "a selected plug-in without hot parameters gives way to the first that has");
            expect (control.knobPlugin (1) == refD, "another channel: its first plug-in with hot parameters");
            rack.selectTile (1);
            b.skin->selectChannel (1);
            expect (control.knobPlugin (0) == refA, "a channel that is not the one RACK shows: the first, not the old selection");
            expect (control.knobPlugin (1) == refD);
            expect (! control.knobPlugin (5).isValid(), "a channel without any");
            expect (! control.getHots (5)[0].assigned);
        }

        beginTest ("Hot knob assignments are saved with the show and stay through a scene recall");
        {
            Bench b;
            auto* plugin = b.add (0);
            auto& control = b.control();
            const auto ref = b.ref (0);
            const int uid = ref.uid;
            auto& fx = b.engine.getFx();
            expect (control.assignHot (ref, 0, b.param (ref, 0)));
            expect (control.assignHot (ref, 1, b.param (ref, 1)));
            *plugin->level = 0.9f;
            b.learn (ui::ph::Target::ofParam (ref, b.param (ref, 0)), cc (1, 21, 10));
            const auto xml = b.rig->show.toXml();
            expect (xml != nullptr);

            MixerEngine engine2 (0);
            HostRig rig2 (engine2);
            expect (rig2.show.loadFromXml (*xml, true).wasOk());
            auto& ch2 = *engine2.getChannels()[0];
            const int slot2 = FxSlots::blockSlotOf (ch2.getHandle(), uid);
            const auto back = engine2.getFx().getChainMemberControl (slot2, 0);
            expect (engine2.getFx().getChain (slot2) != nullptr, "a plug-in with hot knobs is stored as a chain of one");
            expectEquals (back.hot[0].name, juce::String ("Level"));
            expectEquals (back.hot[1].name, juce::String ("Mode"));
            expectEquals (back.hot[1].index, 1);
            engine2.getGarbage().collectAllNow();

            // A scene recall of the same plug-in leaves the assignments as they are.
            const int scene = b.rig->show.addScene ("A", -1);
            control.clearHot (ref, 1);
            b.rig->show.recallScene (scene);
            expectEquals (control.controlOf (ref).hot[0].name, juce::String ("Level"));
            expect (! control.controlOf (ref).hot[1].isSet(), "the scene does not give hot knob 2 back");
            expectEquals ((int) control.controlOf (ref).maps.size(), 1);

            // A plug-in a scene makes again comes back with the assignments the scene was stored with.
            expect (control.assignHot (ref, 1, b.param (ref, 1)));
            const int scene2 = b.rig->show.addScene ("B", -1);
            expect (ctxRemove (*b.rig, b.engine, *b.engine.getChannels()[0], uid));
            expect (pluginUids (*b.engine.getChannels()[0]).empty());
            const auto result = b.rig->show.recallScene (scene2);
            expect (result.warnings.isEmpty(), result.warnings.joinIntoString (" | "));
            const int slot = FxSlots::blockSlotOf (b.engine.getChannels()[0]->getHandle(), uid);
            const auto again = fx.getChainMemberControl (slot, 0);
            expect (again.hot[0].name == "Level" && again.hot[1].name == "Mode" && again.maps.size() == 1, "hot knobs and the map came back with the plug-in");
        }

        beginTest ("MIDI learn: tap a parameter, move a control: a mapping is made; a CC sets the parameter, a relative one steps it, a note toggles");
        {
            Bench b;
            auto* plugin = b.add (0);
            auto& control = b.control();
            auto& rack = b.skin->getRack();
            const auto ref = b.ref (0);
            expect (! control.isLearning());
            control.handleMidi (cc (1, 21, 127));
            expectWithinAbsoluteError (plugin->level->get(), 0.5f, 1.0e-6f, "nothing is mapped yet");

            control.setLearning (true);
            expect (control.getLearn() == ui::ph::Control::Learn::WaitTarget && control.describeLearn().isNotEmpty());
            rack.selectTile (0);
            plugin->level->beginChangeGesture(); // a touch of the parameter in the dock
            plugin->level->endChangeGesture();
            expect (control.getLearn() == ui::ph::Control::Learn::WaitMidi);
            expect (control.getLearnTarget().kind == ui::ph::Target::Kind::Param && control.getLearnTarget().param.name == "Level");
            control.handleMidi (cc (1, 21, 100));
            expect (! control.isLearning());
            expectEquals (control.getNumMappings(), 1);
            expectEquals (control.getMappings()[0].sourceText, juce::String ("CC 21  CH 1"));
            expect (control.getMappings()[0].targetText.contains ("Level") && control.getMappings()[0].targetText.contains ("Fake Params"));

            control.handleMidi (cc (1, 21, 127));
            expectWithinAbsoluteError (plugin->level->get(), 1.0f, 1.0e-6f, "a CC sets the parameter");
            control.handleMidi (cc (1, 21, 0));
            expectWithinAbsoluteError (plugin->level->get(), 0.0f, 1.0e-6f);
            control.handleMidi (cc (1, 21, 64));
            expectWithinAbsoluteError (plugin->level->get(), 0.5f, 0.01f);
            control.handleMidi (cc (2, 21, 127));
            expectWithinAbsoluteError (plugin->level->get(), 0.5f, 0.01f, "another MIDI channel is another control");

            // An endless encoder: 1..63 up, 65..127 down.
            expect (control.setMappingMode (0, ui::ph::MidiSource::Mode::Relative));
            expectEquals (control.getMappings()[0].sourceText, juce::String ("CC 21  CH 1  REL"));
            const float before = plugin->level->get();
            control.handleMidi (cc (1, 21, 5));
            expectWithinAbsoluteError (plugin->level->get(), before + 0.05f, 1.0e-3f);
            control.handleMidi (cc (1, 21, 125));
            expectWithinAbsoluteError (plugin->level->get(), before + 0.02f, 1.0e-3f);

            // A note toggles (a stepped parameter goes between its ends).
            b.learn (ui::ph::Target::ofParam (ref, b.param (ref, 1)), note (1, 60));
            expectEquals (control.getNumMappings(), 2);
            control.handleMidi (note (1, 60));
            expectEquals (plugin->mode->getIndex(), 2);
            control.handleMidi (note (1, 60));
            expectEquals (plugin->mode->getIndex(), 0);
            control.handleMidi (juce::MidiMessage::noteOff (1, 60));
            expectEquals (plugin->mode->getIndex(), 0, "a note off does nothing");

            // One control, one target: learning the same control for another parameter moves it.
            b.learn (ui::ph::Target::ofParam (ref, b.param (ref, 1)), cc (1, 21, 0));
            expectEquals (control.getNumMappings(), 1, "the note's mapping and the CC's went; one mapping is left");
            expect (control.getMappings()[0].source.kind == ui::ph::MidiSource::Kind::Cc && control.getMappings()[0].targetText.contains ("Mode"));
        }

        beginTest ("MIDI learn also takes a hot knob, a channel's SELECT and BYPASS and the scene keys (which then do not act)");
        {
            Bench b;
            auto* plugin = b.add (0);
            auto& control = b.control();
            auto& skin = *b.skin;
            const auto ref = b.ref (0);
            expect (control.assignHot (ref, 0, b.param (ref, 0)));
            b.rig->show.addScene ("A", -1);
            b.rig->show.addScene ("B", -1);
            skin.showChanged();
            skin.refreshAll();

            control.setLearning (true);
            skin.getStrip (0).getKnob (0).onClick(); // a tap on the knob
            expect (control.getLearnTarget().kind == ui::ph::Target::Kind::Hot && control.getLearnTarget().channel == 0);
            control.handleMidi (cc (1, 22, 5));
            control.handleMidi (cc (1, 22, 127));
            expectWithinAbsoluteError (plugin->level->get(), 1.0f, 1.0e-6f, "the hot knob's parameter follows the CC");
            control.handleMidi (cc (1, 22, 0));
            expectWithinAbsoluteError (plugin->level->get(), 0.0f, 1.0e-6f);

            control.setLearning (true);
            skin.getStrip (0).getBypassKey().onClick();
            expect (! b.engine.getChannels()[0]->params.input.bypass.load(), "BYPASS did not act: LEARN took the tap");
            control.handleMidi (note (1, 40)); // the control being learned
            expect (! b.engine.getChannels()[0]->params.input.bypass.load());
            control.handleMidi (note (1, 40));
            expect (b.engine.getChannels()[0]->params.input.bypass.load(), "a note toggles the channel's BYPASS");
            control.handleMidi (note (1, 40));
            expect (! b.engine.getChannels()[0]->params.input.bypass.load());
            control.handleMidi (cc (1, 22, 64)); // another control: no effect on BYPASS

            control.setLearning (true);
            skin.getStrip (3).getSelectKey().onClick();
            control.handleMidi (cc (1, 30, 127));
            expectEquals (skin.getSelectedChannel(), 0, "SELECT did not act while learning");
            control.handleMidi (cc (1, 30, 0));
            control.handleMidi (cc (1, 30, 127));
            expectEquals (skin.getSelectedChannel(), 3, "a CC (on its rising edge) selects the channel");

            skin.setCue (0);
            control.setLearning (true);
            skin.getSceneKey (0).click();
            expect (control.getLearnTarget().kind == ui::ph::Target::Kind::SceneGo);
            control.handleMidi (note (1, 36));
            control.setLearning (true);
            skin.getSceneKey (1).click();
            control.handleMidi (note (1, 37));
            control.setLearning (true);
            skin.getSceneKey (-1).click();
            control.handleMidi (note (1, 35));
            expectEquals (control.getNumMappings(), 6);
            skin.setCue (0);
            control.handleMidi (note (1, 37));
            expectEquals (skin.getCue(), 1, "NEXT");
            control.handleMidi (note (1, 35));
            expectEquals (skin.getCue(), 0, "PREV");
            control.handleMidi (note (1, 37));
            control.handleMidi (note (1, 36));
            expectEquals (b.rig->show.getCurrentSceneIndex(), 1, "GO recalls the cued scene");
        }

        beginTest ("A mapping follows its plug-in when the rack is reordered and goes, with a note, when the plug-in is removed");
        {
            Bench b;
            auto* one = b.add (0);
            auto* two = b.add (0);
            auto& control = b.control();
            auto& rack = b.skin->getRack();
            auto& fx = b.engine.getFx();
            auto& ch = *b.engine.getChannels()[0];
            b.learn (ui::ph::Target::ofParam (b.ref (0, 0), b.param (b.ref (0, 0), 0)), cc (1, 21, 1));
            b.learn (ui::ph::Target::ofParam (b.ref (0, 1), b.param (b.ref (0, 1), 0)), cc (1, 22, 1));
            expectEquals (control.getNumMappings(), 2);

            expect (rack.moveTile (0, 2)); // plug-in one is now second in the rack
            expect (control.pluginOf (b.ref (0, 1)) == one);
            control.handleMidi (cc (1, 21, 127));
            expectWithinAbsoluteError (one->level->get(), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (two->level->get(), 0.5f, 1.0e-6f, "the other one is left alone");
            expect (control.getMappings()[0].targetText.contains ("Level") && control.getMappings()[1].targetText.contains ("Level"));

            // Inside a block of several plug-ins: the chain is rebuilt on a move, the plug-ins keep their maps.
            const int slot = FxSlots::blockSlotOf (ch.getHandle(), b.ref (0, 0).uid);
            auto* three = new test::ParameterPlugin();
            expect (fx.addToChain (slot, std::unique_ptr<juce::AudioPluginInstance> (three)));
            b.skin->refreshAll();
            const auto refThree = b.ref (0, 1);
            expect (control.pluginOf (refThree) == three);
            b.learn (ui::ph::Target::ofParam (refThree, b.param (refThree, 0)), cc (1, 23, 1));
            expect (fx.moveInChain (slot, 1, 0));
            control.handleMidi (cc (1, 23, 127));
            expectWithinAbsoluteError (three->level->get(), 1.0f, 1.0e-6f, "the map went with its plug-in to the front of the chain");
            expectWithinAbsoluteError (two->level->get(), 0.5f, 1.0e-6f);
            expectEquals (control.getNumMappings(), 3);

            // Removing a plug-in removes its mappings, and says so.
            control.clearNote(); // what the last LEARN said
            control.poll();
            expect (control.getNote().isEmpty(), "a plug-in that is still there takes nothing away");
            expect (ctxRemove (*b.rig, b.engine, ch, b.ref (0, 2).uid));
            b.skin->refreshAll();
            control.poll();
            expectEquals (control.getNumMappings(), 2);
            expect (control.getNote().contains ("went with") && control.getNote().contains ("Fake Params"), control.getNote());
            control.handleMidi (cc (1, 22, 127)); // the removed plug-in's control is harmless
        }

        beginTest ("Mappings are saved with the show: a plug-in's, and the keys' and hot knobs'");
        {
            Bench b;
            auto* plugin = b.add (0);
            auto& control = b.control();
            const auto ref = b.ref (0);
            expect (control.assignHot (ref, 0, b.param (ref, 0)));
            b.learn (ui::ph::Target::ofParam (ref, b.param (ref, 0)), cc (1, 21, 1));
            b.learn (ui::ph::Target::ofHot (0, 0), cc (1, 22, 1));
            expect (control.setMappingMode (0, ui::ph::MidiSource::Mode::Relative));
            b.learn (ui::ph::Target::of (ui::ph::Target::Kind::Bypass, 2), note (3, 40));
            b.learn (ui::ph::Target::of (ui::ph::Target::Kind::SceneGo), note (1, 36));
            expectEquals (control.getNumMappings(), 4);
            expect (b.rig->show.getHostControlXml().contains ("PluginHostControl"));
            const auto xml = b.rig->show.toXml();
            expect (xml != nullptr && xml->getChildByName ("PluginHostControl") != nullptr);
            const auto before = control.getMappings();

            MixerEngine engine2 (0);
            engine2.configurePluginHost (16);
            HostRig rig2 (engine2);
            expect (rig2.show.loadFromXml (*xml, true).wasOk());
            {
                ui::PluginHostSurface skin2 (rig2.ctx);
                skin2.setSize (1920, 1080);
                auto& control2 = skin2.getControl();
                const auto after = control2.getMappings();
                expectEquals ((int) after.size(), 4);
                for (size_t i = 0; i < before.size() && i < after.size(); ++i)
                    expect (after[i].sourceText == before[i].sourceText && after[i].targetText == before[i].targetText && after[i].source == before[i].source,
                            "mapping " + juce::String ((int) i + 1) + " is the same: " + after[i].sourceText + " -> " + after[i].targetText);
                expect (after[0].source.mode == ui::ph::MidiSource::Mode::Relative);
                control2.handleMidi (note (3, 40));
                expect (engine2.getChannels()[2]->params.input.bypass.load(), "the restored mapping works");
                const auto refBack = control2.pluginsOf (0)[0];
                auto* back = dynamic_cast<test::ParameterPlugin*> (control2.pluginOf (refBack));
                expect (back != nullptr);
                if (back != nullptr)
                {
                    control2.handleMidi (cc (1, 21, 127));
                    expectWithinAbsoluteError (back->level->get(), 1.0f, 1.0e-6f, "and so does the plug-in's");
                }

                // A show without mappings takes them away when the skin is told it was loaded.
                rig2.show.setHostControlXml ({});
                skin2.showChanged();
                expectEquals (control2.getNumMappings(), 1, "the plug-in's stay with the plug-in, the show's are gone");
            }
            engine2.getGarbage().collectAllNow();
            juce::ignoreUnused (plugin);
        }

        beginTest ("CONTROL page: the mappings as a list, DELETE, CLEAR ALL (asks twice), LEARN, the hot knobs' picker and the MIDI devices");
        {
            Bench b;
            auto* plugin = b.add (0);
            auto& control = b.control();
            auto& skin = *b.skin;
            auto& page = skin.getControlPage();
            using Page = ui::PluginHostSurface::Page;
            const auto ref = b.ref (0);
            skin.getTab (Page::Control).onClick();
            expect (page.isVisible() && ! skin.getComingSoon()->isVisible() && ! skin.getRack().isVisible());
            expectEquals (page.getNumMappings(), 0);

            b.learn (ui::ph::Target::of (ui::ph::Target::Kind::Bypass, 0), cc (1, 30, 1));
            b.learn (ui::ph::Target::ofParam (ref, b.param (ref, 0)), cc (1, 21, 1));
            b.learn (ui::ph::Target::ofParam (ref, b.param (ref, 1)), note (1, 60));
            page.refresh();
            expectEquals (page.getNumMappings(), 3);
            expectEquals (page.getMapping (0).sourceText, juce::String ("CC 30  CH 1"));
            expectEquals (page.getMapping (0).targetText, juce::String ("CH 1  BYPASS"));
            expect (page.getMapping (1).targetText.contains ("Level") && page.getMapping (2).sourceText.startsWith ("NOTE"));

            // Touch-sized: the rows, the keys and the hot keys.
            for (auto* key : { &page.getLearnKey(), &page.getModeKey(), &page.getDeleteKey(), &page.getClearKey() })
                expect (key->getHeight() >= 44 && key->getWidth() >= 44);
            expect (page.getMappingRowBounds (0).getHeight() >= 44 && page.getMappingRowBounds (1).getY() >= page.getMappingRowBounds (0).getBottom());

            page.selectMapping (1);
            expectEquals (page.getSelectedMapping(), 1);
            page.getModeKey().onClick();
            expect (page.getMapping (1).source.mode == ui::ph::MidiSource::Mode::Relative, "MODE turns the controller relative");
            page.getModeKey().onClick();
            expect (page.getMapping (1).source.mode == ui::ph::MidiSource::Mode::Absolute);
            page.getDeleteKey().onClick();
            expectEquals (page.getNumMappings(), 2);
            expectEquals ((int) control.controlOf (ref).maps.size(), 1, "the plug-in's map is gone from the plug-in");
            expect (page.getMapping (0).targetText.contains ("BYPASS") && page.getMapping (1).sourceText.startsWith ("NOTE"));

            page.getClearKey().onClick();
            expect (page.isClearArmed() && page.getNumMappings() == 2, "the first press arms it");
            expectEquals (page.getClearKey().getButtonText(), juce::String ("CONFIRM"));
            page.selectMapping (0);
            expect (! page.isClearArmed(), "touching something else lets go");
            page.getClearKey().onClick();
            page.getClearKey().onClick();
            expectEquals (page.getNumMappings(), 0);
            expect (! page.isClearArmed() && control.controlOf (ref).maps.empty() && b.rig->show.getHostControlXml().isEmpty());

            page.getLearnKey().onClick();
            expect (control.isLearning() && page.getLearnKey().isOn());
            page.getLearnKey().onClick();
            expect (! control.isLearning());

            // HOT KNOBS: the selected channel's plug-ins, and the picker's choice.
            expectEquals (page.getNumHotRows(), 1);
            expectEquals (page.getHotRow (0).name, juce::String ("Fake Params"));
            expectEquals (page.getHotRow (0).numParameters, 2);
            expect (page.getHotRow (0).hot[0].isEmpty() && page.getHotKeyBounds (0, 0).getHeight() >= 44 && page.getHotKeyBounds (0, 1).getX() > page.getHotKeyBounds (0, 0).getRight() - 1);
            page.chooseHot (0, 0, 1);
            page.chooseHot (0, 1, 0);
            expectEquals (page.getHotRow (0).hot[0], juce::String ("Mode"));
            expectEquals (page.getHotRow (0).hot[1], juce::String ("Level"));
            b.skin->getStrip (0).refresh();
            expect (b.skin->getStrip (0).getKnob (0).isAssigned() && b.skin->getStrip (0).getKnob (1).isAssigned());
            page.chooseHot (0, 1, -1);
            expect (page.getHotRow (0).hot[1].isEmpty() && page.getHotRow (0).hot[0] == "Mode");
            control.setLearning (true); // while LEARN waits, the picker offers the parameter to it
            page.chooseHot (0, 0, 0);
            expect (control.getLearn() == ui::ph::Control::Learn::WaitMidi && control.getLearnTarget().param.name == "Level");
            expectEquals (page.getHotRow (0).hot[0], juce::String ("Mode"), "and does not assign it");
            control.setLearning (false);
            skin.selectChannel (4);
            expectEquals (page.getNumHotRows(), 0, "the panel follows the selected channel");
            skin.selectChannel (0);

            // The MIDI devices: an input that is switched on stays listed, even when it is not there.
            control.setInputEnabled ("Test MIDI input", true);
            page.refresh();
            int row = -1;
            for (int i = 0; i < page.getNumDevices(); ++i)
                if (page.getDeviceName (i) == "Test MIDI input")
                    row = i;
            expect (row >= 0 && page.isDeviceOn (row) && ! control.isInputOpen ("Test MIDI input"));
            expectEquals (b.rig->settings.getValue (ui::ph::Control::inputsKey), juce::String ("Test MIDI input"));
            expect (page.getDeviceRowBounds (juce::jmax (0, row)).getHeight() >= 44);
            page.toggleDevice (row);
            expect (! control.isInputEnabled ("Test MIDI input") && b.rig->settings.getValue (ui::ph::Control::inputsKey).isEmpty());
            juce::ignoreUnused (plugin);
        }

        beginTest ("The PLUGIN HOST has its own console theme");
        {
            expect (ui::universal::Theme::consoleLook (ld::kProfilePluginHost) == ui::universal::Theme::Look::ConsolePluginHost);
            expect (&ui::universal::Theme::named ("c-pluginhost") == &ui::universal::Theme::of (ui::universal::Theme::Look::ConsolePluginHost));
            expectEquals (juce::String (ui::universal::Theme::titleOf (ui::universal::Theme::Look::ConsolePluginHost)), juce::String ("PLUGIN HOST"));
        }
    }

private:
    /** Removes block `uid` of `strip` the way the RACK page's menu does (the page finds the tile by its block). */
    static bool ctxRemove (HostRig& rig, MixerEngine& engine, Strip& strip, int uid)
    {
        if (auto* plugin = engine.getFx().getPlugin (FxSlots::blockSlotOf (strip.getHandle(), uid)))
            if (rig.ctx.pluginRemoved)
                rig.ctx.pluginRemoved (plugin);
        return strip.removeBlock (uid);
    }
};

static PluginHostTests pluginHostTests;
