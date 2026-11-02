// The Universal Mixer's screenshots for the docs (docs/images/universal-*.png).
// A category of its own, not run with the others:
//
//   LIVEDAW_SCREENSHOTS=docs/images build/LiveDAWTests_artefacts/Release/LiveDAWTests Screenshots
//
// It sets up a demo console (a band on a 32-channel console of the Universal
// Mixer, not a WING) and renders each view. Without LIVEDAW_SCREENSHOTS it
// renders them and only checks the pictures aren't empty.

#include "TestHelpers.h"
#include "engine/EventLog.h"
#include "engine/PluginHost.h"
#include "engine/Show.h"
#include "ui/UniversalSkin.h"

using namespace livedaw;
using namespace livedaw::ui::universal;

namespace
{
/** A UI context over an engine (with a settings file of its own) that keeps the selection. */
struct ShotRig
{
    explicit ShotRig (MixerEngine& e) : engine (e), show (e, host), meters (e), ctx (e, host, show, log, devices, settings, meters)
    {
        ctx.selectStrip = [this] (Strip* s) { selected = s; };
        ctx.getSelectedStrip = [this] { return selected; };
        ctx.interfaceKind = ui::InterfaceKind::Universal;
    }
    ~ShotRig() { settingsFile.deleteFile(); }

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

/** A parameter of a strip's EQ model by id (nothing if the model has none). */
void setEq (EqBlockParams& eq, models::Table table, const char* id, float value)
{
    if (const int i = models::model (table, eq.model.load()).find (id); i >= 0)
        eq.values[i] = value;
}
} // namespace

class ScreenshotTests final : public juce::UnitTest
{
public:
    ScreenshotTests() : juce::UnitTest ("Universal Mixer screenshots", "Screenshots") {}

    void runTest() override
    {
        beginTest ("The Universal Mixer's views, on a demo console");

        MixerEngine engine (0);
        const Counts counts { 32, 4, 16, 4, 6, 16, 8, 16 };
        engine.configure (&counts);
        ShotRig rig (engine);
        setUpBand (engine, rig.show);

        // Levels as a band plays.
        for (size_t i = 0; i < engine.getChannels().size(); ++i)
        {
            auto& m = engine.getChannels()[i]->meters;
            const float level = 0.12f + 0.55f * (float) ((i * 37) % 11) / 10.0f;
            m.trimPeak[0] = level;
            m.trimPeak[1] = level * 0.9f;
            m.outputPeak[0] = level * 0.8f;
            m.outputPeak[1] = level * 0.75f;
        }
        for (auto kind : { StripKind::Bus, StripKind::Main, StripKind::Matrix })
            for (auto& s : engine.getStrips (kind))
            {
                s->meters.outputPeak[0] = 0.45f;
                s->meters.outputPeak[1] = 0.4f;
            }
        engine.monitorPeak[0] = 0.3f;
        engine.monitorPeak[1] = 0.28f;
        rig.meters.advance (0.01f);

        ui::UniversalSkin skin (rig.ctx);
        skin.setSize (1920, 1080); // a 1080p screen, the size Sidecar is laid out for
        const auto shot = [this, &skin] (const juce::String& fileName)
        {
            const auto image = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
            expect (image.isValid() && image.getWidth() == 1920, fileName);
            const auto folder = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_SCREENSHOTS", {});
            if (folder.isEmpty())
                return;
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile (fileName);
            file.deleteFile();
            juce::FileOutputStream out (file);
            expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out), file.getFullPathName());
            logMessage ("wrote " + file.getFullPathName());
        };

        // MIXER, with and without its faders, the lead vocal selected (its panel across the top).
        auto& vox = *engine.getChannels()[18];
        rig.selected = &vox;
        skin.selectionChanged();
        skin.getMixer().refresh();
        skin.getMixer().getChannelPanel().refresh();
        shot ("universal-mixer.png");
        skin.getMixer().setFadersShown (false);
        skin.getMixer().refresh();
        shot ("universal-overview.png");
        skin.getMixer().setFadersShown (true);

        // The inspector on the lead vocal: an EQ block after its DYNAMICS, a user tap
        // after that for the reverb send.
        const auto chain = vox.getChain();
        int afterDyn = -1;
        for (size_t i = 0; i < chain.size(); ++i)
            if (chain[i].kind == ld::kStepDynamics)
                afterDyn = (int) i + 1;
        const int uid = vox.insertBlock (BlockKind::Eq, afterDyn);
        if (auto* eq = vox.getBlock (uid).eq())
        {
            eq->on = true;
            setEq (*eq, (models::Table) vox.getEqTable(), "hg", 3.0f);
            setEq (*eq, (models::Table) vox.getEqTable(), "lg", -4.0f);
        }
        const int tap = vox.insertTap (afterDyn + 1);
        vox.sends[5].enabled = true; // REVERB
        vox.sends[5].levelDb = -8.0f;
        vox.sends[5].tap = tap;
        vox.sends[4].enabled = true; // VOCALS
        vox.sends[4].levelDb = 0.0f;
        rig.selected = &vox;
        skin.selectionChanged();
        auto& inspector = skin.getInspector();
        for (int i = 0; i < (int) inspector.chain.getRows().size(); ++i)
            if (inspector.chain.getRows()[(size_t) i].step.uid == uid)
                inspector.chain.selectRow (i);
        inspector.refresh();
        skin.getMixer().refresh();
        shot ("universal-inspector.png");
        inspector.showPage (Inspector::Page::Sends);
        inspector.refresh();
        shot ("universal-sends.png");
        inspector.showPage (Inspector::Page::Chain);
        inspector.closeKey.onClick();

        // ROUTING, FX RACK (its parameter list), MONITORING, SCENES, SETUP.
        skin.showView (ui::UniversalSkin::View::Routing);
        shot ("universal-routing.png");
        skin.showView (ui::UniversalSkin::View::Fx);
        auto& fx = skin.getFxRack();
        fx.selectSlot (0);
        if (fx.isShowingPanel())
            fx.panelKey.onClick();
        fx.refresh();
        shot ("universal-fx.png");
        skin.showView (ui::UniversalSkin::View::Monitoring);
        shot ("universal-monitoring.png");
        skin.showView (ui::UniversalSkin::View::Scenes);
        skin.getScenes().selectScene (2);
        shot ("universal-scenes.png");
        skin.showView (ui::UniversalSkin::View::Setup);
        skin.getSetup().setPending (SetupView::Buses, 24);
        skin.getSetup().refresh();
        shot ("universal-setup.png");
    }

private:
    /** A band on the demo console: names, colours, faders, pans, dynamics and EQs,
        buses, FX devices on inserts, scenes. */
    void setUpBand (MixerEngine& engine, Show& show)
    {
        struct Ch
        {
            const char* name;
            int colour;
            float fader, pan;
        };
        static const Ch band[] = {
            { "Kick In", 2, -2, 0 },       { "Kick Out", 2, -6, 0 },    { "Snare Top", 2, -3, 0 },   { "Snare Bot", 2, -9, 0 },
            { "Hi-Hat", 2, -12, 30 },      { "Rack Tom", 2, -6, -20 },  { "Floor Tom", 2, -6, 25 },  { "OH L", 2, -8, -70 },
            { "OH R", 2, -8, 70 },         { "Room", 2, -14, 0 },       { "Bass DI", 5, -4, 0 },     { "Bass Amp", 5, -7, 0 },
            { "Gtr L", 8, -6, -45 },       { "Gtr R", 8, -6, 45 },      { "Acoustic", 8, -5, 10 },   { "Keys L", 11, -7, -60 },
            { "Keys R", 11, -7, 60 },      { "Synth", 11, -10, 0 },     { "Lead Vox", 14, 0, 0 },    { "BV 1", 14, -6, -25 },
            { "BV 2", 14, -6, 25 },        { "BV 3", 14, -8, 0 },       { "Sax", 16, -6, -15 },      { "Trumpet", 16, -6, 15 },
            { "Talkback", 18, -100, 0 },   { "Click", 18, -100, 0 },    { "Track L", 9, -10, -100 }, { "Track R", 9, -10, 100 },
        };
        const auto& channels = engine.getChannels();
        for (size_t i = 0; i < std::size (band) && i < channels.size(); ++i)
        {
            auto& s = *channels[i];
            s.params.custLink = false;
            s.setName (band[i].name);
            s.setColourIndex (band[i].colour);
            s.params.faderDb = band[i].fader;
            s.params.pan = band[i].pan;
        }
        // Gates on the drums, compressors on the bass and vocals, EQs with some shape.
        for (int i : { 0, 2, 5, 6 })
        {
            channels[(size_t) i]->params.gate.on = true;
            channels[(size_t) i]->meters.gateReductionDb = 4.0f + (float) i;
        }
        for (int i : { 0, 2, 10, 18, 19, 20 })
        {
            channels[(size_t) i]->params.dynamics.on = true;
            channels[(size_t) i]->meters.dynReductionDb = 3.0f + (float) (i % 5);
        }
        for (int i : { 0, 1, 2, 10, 12, 13, 18 })
        {
            auto& eq = channels[(size_t) i]->params.eq;
            const auto table = (models::Table) channels[(size_t) i]->getEqTable();
            eq.on = true;
            setEq (eq, table, "lg", i < 2 ? 5.0f : -3.0f);
            setEq (eq, table, "hg", i == 18 ? 2.5f : -2.0f);
        }

        static const char* buses[] = { "Drums", "Bass", "Guitars", "Keys", "Vocals", "Reverb", "Delay", "Drum verb",
                                       "Mon 1",  "Mon 2", "Mon 3",   "Mon 4", "IEM L", "IEM R",   "Sub",   "Spare" };
        const auto& b = engine.getStrips (StripKind::Bus);
        for (size_t i = 0; i < std::size (buses) && i < b.size(); ++i)
        {
            b[i]->params.custLink = false;
            b[i]->setName (buses[i]);
            b[i]->params.faderDb = i < 8 ? -2.0f : -6.0f;
        }
        static const char* mains[] = { "Main LR", "Subs", "Front fill", "Broadcast" };
        const auto& m = engine.getMains();
        for (size_t i = 0; i < std::size (mains) && i < m.size(); ++i)
            m[i]->setName (mains[i]);
        static const char* matrices[] = { "Delay 1", "Delay 2", "Lobby", "Stage L", "Stage R", "Record" };
        const auto& x = engine.getMatrices();
        for (size_t i = 0; i < std::size (matrices) && i < x.size(); ++i)
            x[i]->setName (matrices[i]);

        // FX devices, two of them inserted.
        auto& fx = engine.getFx();
        const std::pair<int, const char*> devices[] = { { 0, "HALL" }, { 1, "PLATE" }, { 2, "ST-DL" }, { 3, "CHORUS" }, { 8, "LIMITER" }, { 9, "DE-S2" } };
        for (auto [slot, token] : devices)
            fx.setModel (slot, models::findModel (models::Fx, token));
        for (auto [strip, slot] : { std::pair<int, int> { 18, 9 }, { 19, 9 } })
        {
            channels[(size_t) strip]->params.preInsert.fx = slot;
            channels[(size_t) strip]->params.preInsert.on = true;
        }

        // The show's scenes.
        for (auto* scene : { "Doors", "Walk-in music", "Song 1: Opener", "Song 2", "Ballad", "Encore" })
            show.addScene (scene);
        show.setSceneSkip (1, true);
        show.setSceneLink (4, true);
        show.setSceneTag (2, 1);
    }
};

static ScreenshotTests screenshotTests;
