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
#include "ui/Y01VSurface.h"
#include "ui/DLiveSurface.h"
#include "ui/DLiveC3500Surface.h"
#include "ui/Y02RSurface.h"
#include "engine/ProMixMidi.h"
#include "engine/Y02RMidi.h"
#include "engine/Yamaha02R.h"
#include "ui/X32SceneFiles.h"
#include "ui/ProMixSurface.h"
#include "ui/Sq5Surface.h"
#include "ui/Scribble.h"

#include <set>
#include "ui/X32Surface.h"

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
        x32Surface();
        proMixSurface();
        universalOnEveryConsole();
        y02rSurface();
        sq5Surface();
        y01vSurface();
        dliveSurface();
        dliveC3500Surface();
        customControlScribbles();
    }

    /** Issue 22's custom control LCD: the names wxfade sets on layer 2. */
    void customControlScribbles()
    {
        beginTest ("A custom control's LCD names its encoder and both keys");
        const char* const rows[4][3] = { { "Scene: 1", "Save", "Restore" }, { "Go_To: 1", "|GO!", "Next/+1" }, { "Fade", "scroll_up", "scroll_dn" }, { "D_v: -144", "scroll", "|Fade" } };
        juce::Image image (juce::Image::ARGB, 4 * 134, 112, true);
        {
            juce::Graphics g (image); // ended before the pixels are read: it may draw deferred
            for (int i = 0; i < 4; ++i)
            {
                ui::ScribbleInfo info;
                info.controlRows = true;
                info.controlTop = rows[i][0];
                info.controlUpper = rows[i][1];
                info.controlLower = rows[i][2];
                expect (! info.isEmpty());
                ui::drawScribble (g, juce::Rectangle<float> ((float) i * 134.0f, 0.0f, 133.0f, 112.0f), info);
            }
        }
        // The backlight and text of all three rows are there.
        for (const auto [from, to] : { std::pair (0, 40), std::pair (42, 76), std::pair (77, 112) })
        {
            std::set<juce::uint32> shades;
            for (int y = from; y < to; ++y)
                for (int x = 0; x < 133; ++x)
                    shades.insert (image.getPixelAt (x, y).getARGB());
            expect (shades.size() > 1, "text in the row from " + juce::String (from));
        }
        const auto folder = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_SCREENSHOTS", {});
        if (folder.isNotEmpty())
        {
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile ("cc-scribbles.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out));
        }
    }

    /** The 01V's surface and LCD, at 1920x1080. */
    void y01vSurface()
    {
        beginTest ("The 01V surface, on a 01V");
        MixerEngine engine (0);
        engine.configureProfile (ld::kProfileYamaha01V);
        ShotRig rig (engine);
        setShownProfile (ld::kProfileYamaha01V);
        ui::fader::setLaw (engine.getFaderLaw());
        auto& channels = engine.getChannels();
        auto& buses = engine.getBuses();
        expectEquals ((int) channels.size(), 22);
        for (size_t i = 0; i < 14; ++i)
        {
            auto& m = channels[i]->meters;
            const float level = 0.08f + 0.6f * (float) ((i * 37) % 11) / 10.0f;
            m.trimPeak[0] = m.outputPeak[0] = level;
            m.trimPeak[1] = m.outputPeak[1] = level * 0.9f;
            channels[i]->params.faderDb = i % 5 == 0 ? 0.0f : -12.0f - (float) (i % 7) * 3.0f;
        }
        auto& lr = engine.getMain (0);
        lr.meters.outputPeak[0] = 0.32f;
        lr.meters.outputPeak[1] = 0.25f;
        lr.params.faderDb = 0.0f;
        channels[3]->params.solo = true;
        channels[4]->params.eq().on = true;
        setEq (channels[4]->params.eq(), models::ChannelEq, "lmg", 10.5f);
        setEq (channels[4]->params.eq(), models::ChannelEq, "hmg", 5.0f);
        rig.meters.advance (0.01f);
        rig.selected = channels[4].get();

        ui::Y01VSurface skin (rig.ctx);
        skin.setSize (1920, 1080);
        skin.refreshAll();
        const auto shot = [this, &skin] (const juce::String& fileName)
        {
            skin.getDisplay().refresh();
            const auto image = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
            expect (image.isValid(), fileName);
            const auto folder = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_SCREENSHOTS", {});
            if (folder.isEmpty())
                return;
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile (fileName);
            file.deleteFile();
            juce::FileOutputStream out (file);
            expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out), file.getFullPathName());
        };
        using namespace ui::y01v;
        auto& display = skin.getDisplay();
        shot ("y01v-eq.png");

        // HOME: each fader its channel, the master the STEREO.
        expect (skin.stripAt (0) == channels[0].get());
        expect (skin.stripAt (13) == channels[13].get());
        expect (skin.masterStrip() == &lr);
        expectWithinAbsoluteError (skin.getFader (0).getPosition(), (double) engine.getFaderLaw ((int) StripKind::Channel).toPosition (0.0f), 0.001);

        // [ON] and [SOLO] (pp.33-34): the channel's.
        channels[2]->params.mute = false;
        skin.findKey ("ON 2")->onClick();
        expect (channels[2]->params.mute.load());
        skin.findKey ("SOLO 5")->onClick();
        expect (channels[5]->params.solo.load());
        channels[5]->params.solo = false;

        // AUX 1: the faders are the sends to it (bus slot 4), the master its master.
        channels[2]->sends[4].levelDb = -7.0f;
        skin.findKey ("AUX 1")->onClick();
        expect (skin.getFaderMode() == FaderMode::Aux1);
        expect (display.getFunction() == Function::Send);
        expect (skin.masterStrip() == buses[4].get());
        expectWithinAbsoluteError (skin.getFader (2).getPosition(), (double) engine.getFaderLaw ((int) StripKind::Channel).toPosition (-7.0f), 0.001);
        expectEquals (display.getNumFields(), 16);
        shot ("y01v-aux1.png");

        // EFFECT 1: a return can't feed its own effect (p.35).
        skin.findKey ("EFFECT 1")->onClick();
        expect (skin.masterStrip() == buses[8].get());
        expect (! skin.getKnob ("RETURN 1").getBinding().isValid());
        expect (skin.getKnob ("RETURN 2").getBinding().isValid());

        // Its pages, a press each (pp.125, 134, 136): EDIT, LIBRARY, PRE/POST.
        expectEquals (display.getNumPages(), 3);
        expectEquals (display.getPage(), 0);
        expect (display.getNumFields() > 0 && display.getNumFields() <= 9);
        shot ("y01v-effect-edit.png");
        skin.findKey ("EFFECT 1")->onClick();
        expectEquals (display.getPage(), 1);
        expectEquals (display.getNumFields(), 2);
        shot ("y01v-effect-library.png");
        const int effectBefore = engine.getFx().getModel (0);
        display.cursor (0, 1);              // the list
        display.turn (1);
        display.cursor (0, -1);             // RECALL
        display.enter();
        expect (engine.getFx().getModel (0) != effectBefore);
        expect (models::consoleOf (models::model (models::Fx, engine.getFx().getModel (0)).token) == "01V");
        skin.findKey ("EFFECT 1")->onClick();
        expectEquals (display.getPage(), 2);
        expectEquals (display.getNumFields(), 15); // a return can't feed its own effect
        shot ("y01v-effect-prepost.png");
        const int sendBefore = channels[0]->sends[8].mode.load();
        display.enter();
        expect (channels[0]->sends[8].mode.load() != sendBefore);
        channels[0]->sends[8].mode = sendBefore;
        skin.findKey ("EFFECT 1")->onClick();
        expectEquals (display.getPage(), 0);
        skin.findKey ("AUX 1")->onClick();
        expectEquals (display.getPage(), 0);
        expectEquals (display.getNumPages(), 2);
        skin.findKey ("AUX 1")->onClick();
        expectEquals (display.getNumFields(), 16); // AUX PRE/POST: both returns can

        // OPTION I/O: 1-8 are CH 17-24, the rest do nothing.
        skin.findKey ("OPTION I/O")->onClick();
        expect (skin.stripAt (0) == channels[14].get());
        expect (skin.stripAt (8) == nullptr);
        expect (! skin.getFader (8).isBound());
        expect (skin.masterStrip() == &lr);
        shot ("y01v-option.png");
        // OPTION page 2: what CH 17-24 have, only AUX 1-2 and EFFECT 1-2 among the sends.
        rig.selected = channels[14].get();
        skin.selectionChanged();
        skin.findKey ("OPTION I/O")->onClick();
        expectEquals (display.getPage(), 1);
        expectEquals (display.getNumFields(), 5 + 1 + 2 + 8 + 2 + 6); // ROUTE, PAN, FADER and ON, sends and PRE/POST, EQ ON and ATT, F/G/Q
        shot ("y01v-option-control.png");
        rig.selected = channels[4].get();
        skin.selectionChanged();

        // Back HOME: the LCD's fields, the cursor, the wheel.
        skin.findKey ("HOME")->onClick();
        skin.findKey ("EQ/ATT")->onClick();
        expect (display.getFunction() == Function::EqAtt);
        expectEquals (display.getNumFields(), 14); // EQ ON, ATT, F, G and Q of four bands
        display.enter();
        expect (! channels[4]->params.eq().on.load());
        display.enter();
        expect (channels[4]->params.eq().on.load());
        const int before = display.getFocus();
        display.cursor (1, 0);
        expect (display.getFocus() != before);
        const auto text = display.getFocusText();
        display.turn (-4);
        expect (display.getFocusText() != text);
        display.turn (4);
        expectEquals (display.getFocusText(), text);

        // SELECTED CHANNEL: F and G work the band picked.
        const auto& eqModel = models::model (models::ChannelEq, channels[4]->params.eq().model.load());
        skin.setEqBand (1);
        expect (skin.getKnob ("G").getBinding().isValid());
        const float atOne = skin.getKnob ("G").getBinding().get();
        skin.getKnob ("G").getBinding().set (0.5f);
        expectWithinAbsoluteError (channels[4]->params.eq().values[eqModel.find ("lmg")].load(), 0.0f, 0.3f);
        skin.getKnob ("G").getBinding().set (atOne);

        // CH 17-24 have only LOW and HIGH (p.264): their page has fewer fields.
        rig.selected = channels[14].get();
        skin.selectionChanged();
        expectEquals (display.getNumFields(), 8); // EQ ON, ATT, and F, G, Q of two bands

        rig.selected = channels[4].get();
        skin.selectionChanged();
        skin.findKey ("DYNAMICS")->onClick();
        expect (display.getNumFields() > 4);
        shot ("y01v-dynamics.png");
        skin.findKey ("PAN/ROUTING")->onClick();
        expectEquals (display.getNumFields(), 16);
        shot ("y01v-pan.png");
        // PAN/ROUT page 2 (p.55): four buses and ST for each.
        skin.findKey ("PAN/ROUTING")->onClick();
        expectEquals (display.getPage(), 1);
        expectEquals (display.getNumFields(), 16 * 5);
        channels[0]->sends[0].enabled = false;
        display.enter();                    // bus 1, the first field: routed at 0 dB
        expect (channels[0]->sends[0].enabled.load());
        expectWithinAbsoluteError (channels[0]->sends[0].levelDb.load(), 0.0f, 0.01f);
        display.enter();
        expect (! channels[0]->sends[0].enabled.load());
        shot ("y01v-routing.png");
        skin.findKey ("PAN/ROUTING")->onClick();
        skin.findKey ("VIEW")->onClick();
        shot ("y01v-view.png");
        // MEMORY (p.186): store a scene, name comes from the show; 00 is read-only.
        skin.findKey ("MEMORY")->onClick();
        expect (display.getFunction() == Function::Memory);
        expectEquals (display.getMemory(), 0);
        display.turn (1);                  // TITLE EDIT has the focus: nothing moves
        display.setMemory (1);
        channels[0]->params.faderDb = -5.0f;
        display.setPreference ("y01vStoreConfirm", false);
        for (int i = 0; i < 2; ++i)
            display.cursor (0, 1);          // TITLE EDIT, RECALL, STORE
        display.enter();
        expectEquals (rig.show.getNumScenes(), 1);
        channels[0]->params.faderDb = -30.0f;
        display.cursor (0, -1);             // RECALL
        display.enter();
        expectWithinAbsoluteError (channels[0]->params.faderDb.load(), -5.0f, 0.01f);
        display.setMemory (0);
        for (int i = 0; i < 1; ++i)
            display.cursor (0, 1);          // STORE: 00 refuses it
        display.enter();
        expectEquals (rig.show.getNumScenes(), 1);
        shot ("y01v-memory.png");
        // UTILITY: the oscillator (p.204), then the two pages of preferences (p.203).
        skin.findKey ("UTILITY")->onClick();
        expectEquals (display.getPage(), 0);
        expectEquals (display.getNumFields(), 14); // ON, WAVEFORM, LEVEL, 11 assigns
        shot ("y01v-oscillator.png");
        skin.findKey ("UTILITY")->onClick();
        expectEquals (display.getPage(), 1);
        expectEquals (display.getNumFields(), 4);
        expect (display.preference ("y01vEqAutoScreen"));
        display.enter();
        expect (! display.preference ("y01vEqAutoScreen"));
        display.enter();
        shot ("y01v-preferences.png");
        skin.findKey ("UTILITY")->onClick();
        expectEquals (display.getPage(), 2);
        expectEquals (display.getNumFields(), 2);
        skin.findKey ("MIDI")->onClick();
        expectEquals (display.getNumFields(), 0);
        skin.findKey ("UTILITY")->onClick();
        expectEquals (display.getPage(), 2);        // where it was left
        skin.findKey ("EQ/ATT")->onClick();
        shot ("y01v-surface.png");
        setShownProfile (ld::kProfileWing);
    }

    /** The dLive's surface (a C1500's layout), at 1920x1080. */
    void dliveSurface()
    {
        beginTest ("The dLive surface, on a dLive");
        MixerEngine engine (0);
        engine.configureProfile (ld::kProfileDLive);
        ShotRig rig (engine);
        setShownProfile (ld::kProfileDLive);
        ui::fader::setLaw (engine.getFaderLaw());
        auto& channels = engine.getChannels();
        for (size_t i = 0; i < 12; ++i)
        {
            auto& m = channels[i]->meters;
            m.trimPeak[0] = m.outputPeak[0] = 0.08f + 0.05f * (float) i;
            channels[i]->params.faderDb = -12.0f + (float) (i % 5) * 3.0f;
            channels[i]->params.colour = 1 + (int) i % 8;
        }
        channels[1]->params.mute = true;
        channels[2]->params.solo = true;
        rig.meters.advance (0.01f);
        rig.selected = channels[4].get();
        channels[4]->params.eq().on = true;

        ui::DLiveSurface skin (rig.ctx);
        skin.setSize (1920, 1080);
        skin.refreshAll();
        for (int i = 0; i < 12; ++i)
            skin.getStrip (i).refresh();
        const auto shot = [this, &skin] (const juce::String& fileName)
        {
            const auto image = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
            expect (image.isValid(), fileName);
            const auto folder = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_SCREENSHOTS", {});
            if (folder.isEmpty())
                return;
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile (fileName);
            file.deleteFile();
            juce::FileOutputStream out (file);
            expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out), file.getFullPathName());
        };
        shot ("dlive-surface.png");

        using Page = ui::DLiveSurface::Page;
        // Bank A: the first 12 channels on the faders.
        expectEquals (skin.getBank(), 0);
        expect (skin.getStrip (0).getSlot().kind == ui::dl::Slot::Channel && skin.getStrip (11).getSlot().index == 11);
        expectWithinAbsoluteError (skin.getStrip (3).getFader().getPosition(), (double) ui::fader::dbToPos (channels[3]->params.faderDb.load()), 0.002);
        // Mute, Sel and PAFL are the strip's.
        channels[7]->params.mute = false;
        skin.getStrip (7).getMute().onClick();
        expect (channels[7]->params.mute.load());
        skin.getStrip (5).getPafl().onClick();
        expect (channels[5]->params.solo.load());
        skin.findKey ("Clear")->onClick();
        expect (! channels[5]->params.solo.load() && ! channels[2]->params.solo.load());
        skin.getStrip (9).getSel().onClick();
        expect (rig.selected == channels[9].get());
        skin.selectionChanged();
        // Banks: B is the next 12, F the mixes and LR.
        skin.findKey ("B")->onClick();
        expectEquals (skin.getBank(), 1);
        expectEquals (skin.getStrip (0).getSlot().index, 12);
        skin.findKey ("F")->onClick();
        expect (skin.getStrip (0).getSlot().kind == ui::dl::Slot::Bus && skin.getStrip (11).getSlot().kind == ui::dl::Slot::Main);
        // A mix's Mix key: the sends on the faders, on bank A.
        skin.getStrip (2).getMix().onClick();
        expectEquals (skin.getMixOnFaders(), 2);
        skin.findKey ("A")->onClick();
        channels[0]->sends[2].levelDb = -7.0f;
        skin.getStrip (0).rebind();
        skin.getStrip (0).getFader().refresh();
        expectWithinAbsoluteError (skin.getStrip (0).getFader().getPosition(), (double) ui::fader::dbToPos (-7.0f), 0.002);
        skin.setMixOnFaders (-1);
        // The rotaries' function.
        skin.findKey ("Rotary Pan")->onClick();
        expect (skin.getRotaryFunction() == ui::DLiveSurface::RotaryFunction::Pan);
        expect (skin.getStrip (0).getRotary().getBinding().isValid());
        // The PEQ's rotaries follow the selection.
        rig.selected = channels[4].get();
        skin.selectionChanged();
        expect (skin.getPeqRotary (1, true).getBinding().isValid());
        // The screen keys pick the page.
        skin.findKey ("Page Processing")->onClick();
        expect (skin.getPage() == Page::Processing);
        expect (skin.getInspector().getStrip() == channels[4].get());
        shot ("dlive-surface-processing.png");
        skin.findKey ("Page Scenes")->onClick();
        expect (skin.getPage() == Page::Scenes);
        skin.findKey ("Page FX")->onClick();
        shot ("dlive-surface-fx.png");
        setShownProfile (ld::kProfileWing);
    }

    /** The dLive's C3500 surface, at 1920x1080: its strips and banks, its two screens. */
    void dliveC3500Surface()
    {
        beginTest ("The dLive C3500 surface, on a dLive");
        MixerEngine engine (0);
        engine.configureProfile (ld::kProfileDLive);
        ShotRig rig (engine);
        setShownProfile (ld::kProfileDLive);
        ui::fader::setLaw (engine.getFaderLaw());
        auto& channels = engine.getChannels();
        for (size_t i = 0; i < 24; ++i)
        {
            auto& m = channels[i]->meters;
            m.trimPeak[0] = m.outputPeak[0] = 0.08f + 0.03f * (float) i;
            channels[i]->params.faderDb = -12.0f + (float) (i % 5) * 3.0f;
            channels[i]->params.colour = 1 + (int) (i / 4) % 8;
            channels[i]->setName ("lav" + juce::String ((int) i + 1));
        }
        for (size_t i = 4; i < 8; ++i)
            channels[i]->params.mute = true;
        rig.meters.advance (0.01f);
        rig.selected = channels[5].get();
        auto& eq = channels[5]->params.eq();
        eq.on = true;
        const auto& peqModel = models::model ((models::Table) channels[5]->getEqTable(), eq.model.load());
        const auto setEq = [&] (const char* id, float v)
        {
            const int at = peqModel.find (id);
            expect (at >= 0, id);
            eq.values[at] = v;
        };
        setEq ("lm", 1.0f);
        setEq ("lf", 60.0f);
        setEq ("lg", 3.0f);
        setEq ("1f", 385.0f);
        setEq ("1g", -2.1f);
        setEq ("2f", 3790.0f);
        setEq ("2g", -2.9f);
        setEq ("hm", 2.0f);
        setEq ("hf", 10000.0f);
        channels[5]->params.filter().lowCut = true;
        channels[5]->params.filter().lowCutFreq = 180.0f;
        rig.show.addScene ("Peaches");
        rig.show.addScene ("oakenfold");
        rig.show.addScene ("Rental");

        ui::DLiveC3500Surface skin (rig.ctx);
        skin.setSize (1920, 1080);
        skin.refreshAll();
        for (int i = 0; i < 24; ++i)
            skin.getStrip (i).refresh();
        const auto shot = [this, &skin] (const juce::String& fileName)
        {
            const auto image = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
            expect (image.isValid(), fileName);
            const auto folder = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_SCREENSHOTS", {});
            if (folder.isEmpty())
                return;
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile (fileName);
            file.deleteFile();
            juce::FileOutputStream out (file);
            expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out), file.getFullPathName());
        };
        using Page = ui::DLiveC3500Surface::Page;
        using Tab = ui::dl::ProcessingScreen::Tab;
        skin.getScenes().select (2);
        shot ("dlive-c3500.png");

        // Two banks of 12: inputs on the first, the mixes on the second.
        expect (skin.getStrip (0).getSlot().kind == ui::dl::Slot::Channel && skin.getStrip (11).getSlot().index == 11);
        expect (skin.getStrip (12).getSlot().kind == ui::dl::Slot::Bus && skin.getStrip (23).getSlot().index == 11);
        expectWithinAbsoluteError (skin.getStrip (3).getFader().getPosition(), (double) ui::fader::dbToPos (channels[3]->params.faderDb.load()), 0.002);
        // A layer key moves its own bank only.
        skin.findKey ("Bank1 B")->onClick();
        expectEquals (skin.getStrip (0).getSlot().index, 12);
        expect (skin.getStrip (12).getSlot().kind == ui::dl::Slot::Bus);
        skin.findKey ("Bank2 D")->onClick();
        expect (skin.getStrip (12).getSlot().kind == ui::dl::Slot::Dca);
        skin.findKey ("Bank1 A")->onClick();
        skin.findKey ("Bank2 A")->onClick();

        // Mute and Sel; PAFL and Clear.
        channels[7]->params.mute = false;
        skin.getStrip (7).getMute().onClick();
        expect (channels[7]->params.mute.load());
        channels[7]->params.mute = false;
        skin.getStrip (2).getPafl().onClick();
        expect (channels[2]->params.solo.load());
        skin.findKey ("Clear")->onClick();
        expect (! channels[2]->params.solo.load());
        skin.getStrip (5).getSel().onClick();
        expect (rig.selected == channels[5].get());
        skin.selectionChanged();

        // The left screen follows the selection; its tabs; the PEQ rotaries.
        expect (skin.getProcessing().getStrip() == channels[5].get());
        expect (skin.getProcessing().getTab() == Tab::Peq);
        expect (skin.getProcessing().hasTab (Tab::Gate) && ! skin.getProcessing().hasTab (Tab::Geq));
        expect (skin.getPeqRotary (1, true).getBinding().isValid());
        expectWithinAbsoluteError (eq.values[peqModel.find ("1f")].load(), 385.0f, 0.01f);
        skin.getProcessing().showTab (Tab::Gate);
        shot ("dlive-c3500-gate.png");
        skin.getProcessing().showTab (Tab::Comp);
        shot ("dlive-c3500-comp.png");
        skin.getProcessing().showTab (Tab::Preamp);
        shot ("dlive-c3500-preamp.png");
        skin.getProcessing().showTab (Tab::Filter);
        skin.getProcessing().showTab (Tab::Overview);
        shot ("dlive-c3500-overview.png");
        skin.getProcessing().showTab (Tab::Peq);

        // The soft rotaries: Gate and Comp on layer 1, sends on layer 2.
        expect (skin.getSoftRotary (1).getBinding().isValid() && skin.getSoftRotary (3).getBinding().isValid());
        expect (! skin.getSoftRotary (0).getBinding().isValid());
        auto& gate = channels[5]->params.gate();
        const int thr = models::model (models::Dynamics, gate.model.load()).find ("thr");
        skin.getSoftRotary (1).getBinding().set (0.5f);
        expectWithinAbsoluteError (skin.getSoftRotary (1).getBinding().get(), 0.5f, 0.02f);
        expect (thr >= 0);
        skin.findKey ("Layer 2")->onClick();
        channels[5]->sends[1].levelDb = -7.0f;
        expectWithinAbsoluteError (skin.getSoftRotary (1).getBinding().get(), ui::fader::dbToPos (-7.0f), 0.02f);
        skin.findKey ("Layer 1")->onClick();

        // A mix's Mix key: the sends on the faders; Assign with a strip's Mix key.
        skin.getStrip (14).getMix().onClick();
        expectEquals (skin.getMixOnFaders(), 2);
        channels[0]->sends[2].levelDb = -7.0f;
        skin.getStrip (0).rebind();
        skin.getStrip (0).getFader().refresh();
        expectWithinAbsoluteError (skin.getStrip (0).getFader().getPosition(), (double) ui::fader::dbToPos (-7.0f), 0.002);
        const bool wasOn = channels[1]->sends[2].enabled.load();
        skin.findKey ("Assign")->onClick();
        skin.getStrip (1).getMix().onClick();
        expect (channels[1]->sends[2].enabled.load() != wasOn);
        skin.findKey ("Assign")->onClick();
        expect (skin.getLatch() == ui::dl::Desk::Latch::None);

        // Safes on a strip's Mix key.
        skin.setMixOnFaders (-1);
        expect (! channels[3]->params.recallSafe.load());
        skin.findKey ("Safes")->onClick();
        skin.getStrip (3).getMix().onClick();
        expect (channels[3]->params.recallSafe.load());
        skin.findKey ("Safes")->onClick();

        // MixRack: Mixer Config's counts are pending until Apply, Name & Colour reaches the strips.
        using Sub = ui::dl::MixRackScreen::Sub;
        skin.findKey ("Page MixRack")->onClick();
        expect (skin.getPage() == Page::MixRack);
        shot ("dlive-c3500-mixrack.png");
        auto& mixRack = skin.getMixRack();
        const auto before = mixRack.getConfig().getCurrent (ui::universal::MixerConfigPanel::Aux);
        mixRack.getConfig().setPending (ui::universal::MixerConfigPanel::Aux, before + 2);
        expect (mixRack.getConfig().hasChanges());
        mixRack.getConfig().revert();
        for (auto [sub, file] : { std::pair<Sub, const char*> { Sub::InputStereos, "dlive-c3500-stereos.png" }, { Sub::NameColour, "dlive-c3500-name.png" }, { Sub::Network, "dlive-c3500-network.png" },
                                  { Sub::Users, "dlive-c3500-users.png" }, { Sub::IoPort, "dlive-c3500-ioport.png" }, { Sub::AudioSync, "dlive-c3500-sync.png" },
                                  { Sub::SourceSelector, "dlive-c3500-selector.png" }, { Sub::Amm, "dlive-c3500-amm.png" } })
        {
            mixRack.showSub (sub);
            expect (mixRack.getSub() == sub);
            shot (file);
        }
        mixRack.setRange (0, 30, 31);
        mixRack.applyColour (5);
        mixRack.applyName ("Pad");
        expectEquals (channels[30]->params.colour.load(), 5);
        expect (channels[31]->getName() == "Pad 2");
        mixRack.applyName ({});

        // Home puts things back.
        skin.findKey ("Rotary Pan")->onClick();
        expect (skin.getRotaryFunction() == ui::dl::RotaryFunction::Pan);
        expect (skin.getStrip (0).getRotary().getBinding().isValid());
        skin.findKey ("Page FX")->onClick();
        expect (skin.getPage() == Page::Fx);
        shot ("dlive-c3500-fx.png");
        skin.findKey ("Home")->onClick();
        expect (skin.getPage() == Page::Scenes);
        expect (skin.getRotaryFunction() == ui::dl::RotaryFunction::Gain);

        // Scene keys: Next and GO.
        skin.getScenes().select (0);
        skin.findKey ("Next")->onClick();
        expectEquals (skin.getScenes().getNext(), 1);
        skin.findKey ("GO")->onClick();
        expectEquals (skin.getScenes().getSelected(), 1);
        expectEquals (skin.getScenes().getNext(), 2);
        setShownProfile (ld::kProfileWing);
    }

    /** The SQ-5's surface and touchscreen, at 1920x1080. */
    void sq5Surface()
    {
        beginTest ("The SQ-5 surface, on an SQ-5");
        MixerEngine engine (0);
        engine.configureProfile (ld::kProfileSq5);
        ShotRig rig (engine);
        setShownProfile (ld::kProfileSq5);
        ui::fader::setLaw (engine.getFaderLaw());
        auto& channels = engine.getChannels();
        expectEquals ((int) channels.size(), 48);
        for (size_t i = 0; i < channels.size(); ++i)
        {
            auto& m = channels[i]->meters;
            const float level = 0.08f + 0.6f * (float) ((i * 37) % 11) / 10.0f;
            m.trimPeak[0] = m.outputPeak[0] = level;
            m.trimPeak[1] = m.outputPeak[1] = level * 0.9f;
            channels[i]->params.colour = 2 + (int) (i / 4) % 7;
            channels[i]->params.faderDb = i % 5 == 0 ? 0.0f : -12.0f - (float) (i % 7) * 3.0f;
        }
        channels[2]->params.mute = true;
        channels[4]->params.solo = true;
        channels[0]->params.gate().on = true;
        channels[0]->params.dynamics().on = true;
        channels[0]->params.eq().on = true;
        channels[0]->params.filter().lowCut = true;
        channels[0]->params.filter().lowCutFreq = 100.0f;
        setEq (channels[0]->params.eq(), models::ChannelEq, "1g", 8.0f);
        setEq (channels[0]->params.eq(), models::ChannelEq, "2g", -6.0f);
        rig.meters.advance (0.01f);
        rig.selected = channels[0].get();

        ui::Sq5Surface skin (rig.ctx);
        skin.setSize (1920, 1080);
        skin.refreshAll();
        const auto shot = [this, &skin] (const juce::String& fileName)
        {
            skin.getDisplay().repaint();
            const auto image = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
            expect (image.isValid(), fileName);
            const auto folder = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_SCREENSHOTS", {});
            if (folder.isEmpty())
                return;
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile (fileName);
            file.deleteFile();
            juce::FileOutputStream out (file);
            expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out), file.getFullPathName());
        };
        using namespace ui::sq5;
        auto& display = skin.getDisplay();
        shot ("sq5-home.png");

        // The layers: A Ip 1-16, the fader on the LR; a mix: the send to it, the master its fader.
        expect (skin.getStrip (0).getSlot() == Slot { Slot::Input, 0 });
        skin.setLayer (1);
        expect (skin.getStrip (0).getSlot() == Slot { Slot::Input, 16 });
        skin.setLayer (0);
        channels[0]->sends[2].levelDb = -7.0f;
        skin.setMix (3);
        expect (skin.getMaster().getSlot() == Slot { Slot::Mix, 2 });
        expectWithinAbsoluteError ((float) ui::fader::posToDb ((float) skin.getStrip (0).getFader().getPosition()), -7.0f, 0.3f, "Ip1's send to Mix 3");
        // Assign: Sel assigns the channel to the selected mix.
        const bool before = channels[1]->sends[2].enabled.load();
        skin.setAssignMode (true);
        skin.getStrip (1).getSel().onClick();
        expect (channels[1]->sends[2].enabled.load() != before, "Assign + Sel");
        skin.setAssignMode (false);
        skin.setMix (0);

        // Processing: the bank view, then each block's detail.
        skin.findKey ("Processing")->onClick();
        expect (display.getScreen() == Screen::Processing && display.getBlock() == Block::Bank);
        shot ("sq5-processing.png");
        for (auto [block, name] : { std::pair (Block::Preamp, "sq5-preamp.png"), std::pair (Block::Hpf, "sq5-hpf.png"), std::pair (Block::Gate, "sq5-gate.png"),
                                    std::pair (Block::Insert, "sq5-insert.png"), std::pair (Block::Peq, "sq5-peq.png"), std::pair (Block::Comp, "sq5-comp.png") })
        {
            display.showBlock (block);
            shot (name);
        }
        // The other screens, as the guide draws them.
        channels[1]->sends[0].enabled = true;
        channels[1]->sends[0].levelDb = -3.0f;
        channels[1]->params.dcaMask.set (0, true);
        rig.show.addScene ("Verse");
        rig.show.addScene ("Chorus");
        for (auto [key, name] : { std::pair ("Routing", "sq5-routing.png"), std::pair ("Meters", "sq5-meters.png"), std::pair ("FX", "sq5-fx.png"),
                                  std::pair ("Scenes", "sq5-scenes.png"), std::pair ("Setup", "sq5-setup.png"), std::pair ("Utility", "sq5-utility.png"),
                                  std::pair ("I/O", "sq5-io.png") })
        {
            skin.findKey (key)->onClick();
            shot (name);
        }
        // I/O's Inputs: socket 3 to Ip1, the first crosspoint under the side tabs.
        skin.findKey ("I/O")->onClick();
        const auto crosspoint = [&display] (int row, int col)
        {
            int area = 0;
            for (int i = 0; i < (int) display.getFields().size(); ++i)
                if (display.getFields()[(size_t) i].kind == Field::Area && area++ == row * 14 + col)
                    return i;
            return -1;
        };
        display.touchField (crosspoint (0, 2));
        expectEquals (channels[0]->params.inputLeft.load(), 2, "patched");
        display.touchField (crosspoint (0, 0));
        expectEquals (channels[0]->params.inputLeft.load(), 0, "patched back");
        // Routing's details: the Sends and DCA assignment work.
        display.showBlock (Block::Sends);
        expect (display.getScreen() == Screen::Routing);
        shot ("sq5-sends.png");
        display.showBlock (Block::DcaMute);
        shot ("sq5-dca.png");
        const bool wasInDca = channels[0]->params.dcaMask.contains (2);
        display.touchField (display.findField ("DCA3 Assign"));
        expect (channels[0]->params.dcaMask.contains (2) != wasInDca, "DCA assign");
        // The FX Library loads a unit.
        skin.findKey ("FX")->onClick();
        expect (display.hasLibrary());
        skin.findKey ("Library")->onClick();
        expect (display.isLibraryOpen());
        shot ("sq5-fxlib.png");
        display.touchField (display.findField ("Gated Verb"));
        expect (! display.isLibraryOpen(), "Recalled");
        shot ("sq5-fxunit.png");
        // Strip Assign: a channel to a fader strip, kept in the settings.
        skin.findKey ("Setup")->onClick();
        display.touchField (display.findField ("Ip3"));
        // The strips' row: Ip1 is the first strip of layer A.
        int strip1 = -1;
        for (int i = (int) display.getFields().size() - 1; i >= 0; --i)
            if (display.getFields()[(size_t) i].title == "Ip1")
                strip1 = i;
        // The channel row comes first: the strip's is the second.
        int count = 0;
        for (int i = 0; i < (int) display.getFields().size(); ++i)
            if (display.getFields()[(size_t) i].title == "Ip1" && ++count == 2)
                strip1 = i;
        display.touchField (strip1);
        expect (skin.getStrip (0).getSlot() == Slot { Slot::Input, 2 }, "Strip Assign");
        expect (rig.settings.getValue ("sq5.layers").isNotEmpty());
        // Back as it was, for what follows.
        rig.settings.removeValue ("sq5.layers");
        // Soft Controls: a SoftKey as a mute group.
        soft::set (rig.settings, 0, 4);
        engine.muteGroups.set (0, false);
        skin.findKey ("Soft 1")->onClick();
        expect (engine.muteGroups.contains (0), "SoftKey = Mute Group 1");
        engine.muteGroups.set (0, false);
        // Copy and Paste by the Sel keys.
        rig.selected = channels[0].get();
        skin.setLayer (0);
        channels[0]->params.gate().on = true;
        skin.setClipboardMode (1);
        skin.getStrip (2).getSel().onClick();
        skin.setClipboardMode (2);
        skin.getStrip (3).getSel().onClick();
        expect (channels[3]->params.filter().lowCut.load() == channels[2]->params.filter().lowCut.load());
        // Fader Flip: the selected mix's GEQ on the faders, 2 sets, then back.
        skin.setMix (1);
        auto* geq = skin.selectedGeq();
        expect (geq != nullptr, "Mix 1's GEQ");
        skin.pressFaderFlip();
        expectEquals (skin.getFaderFlip(), 1);
        expectEquals (skin.getStrip (0).getLcdText(), juce::String ("31"));
        skin.getStrip (0).getFader().setPositionFromUser (1.0);
        expectWithinAbsoluteError (geq->values[0].load(), 12.0f, 0.01f);
        skin.pressFaderFlip();
        expectEquals (skin.getStrip (0).getLcdText(), juce::String ("1.25k"));
        skin.pressFaderFlip();
        expectEquals (skin.getFaderFlip(), 0);
        skin.setMix (0);
        rig.selected = channels[0].get();

        // A value touched is yellow and the rotary adjusts it.
        display.showBlock (Block::Hpf);
        const int freq = display.findField ("Freq");
        expect (freq >= 0);
        display.touchField (freq);
        expectEquals (display.getSelectedField(), freq);
        auto rotary = display.getRotaryBinding();
        expect (rotary.isValid());
        rotary.set (0.0f);
        expectWithinAbsoluteError (channels[0]->params.filter().lowCutFreq.load(), 20.0f, 0.5f);
        // The Processing key: from the detail to the bank, from the bank home, deselected.
        display.pressScreenKey (Screen::Processing);
        expect (display.getBlock() == Block::Bank);
        display.pressScreenKey (Screen::Processing);
        expect (display.getScreen() == Screen::Home);
        expect (rig.selected == nullptr);
        setShownProfile (ld::kProfileWing);
        ui::fader::setLaw (FaderLaw::wing());
    }

    /** The 02R surface (Phase 6 step 7) on an 02R, at 1920x1080. */
    void y02rSurface()
    {
        beginTest ("The 02R surface, on an 02R");
        MixerEngine engine (0);
        engine.configureProfile (ld::kProfileYamaha02R);
        ShotRig rig (engine);
        setShownProfile (ld::kProfileYamaha02R);
        auto& y = engine.getYamaha02R();
        auto& channels = engine.getChannels();
        for (int i = 0; i < 36; ++i)
        {
            y.set (i * Yamaha02R::perStrip, i % 5 == 0 ? 101 : 88 - 3 * (i % 4)); // levels on the console's steps
            channels[(size_t) i]->meters.outputPeak[0] = 0.08f + 0.5f * (float) ((i * 37) % 11) / 10.0f;
        }
        y.set (54 * Yamaha02R::perStrip, 127); // STEREO at 0 dB
        y.set (12 * Yamaha02R::perStrip + 1, 0); // CH 13 OFF
        engine.getMains()[0]->meters.outputPeak[0] = 0.3f;
        engine.getMains()[0]->meters.outputPeak[1] = 0.28f;
        rig.meters.advance (0.01f);
        rig.selected = channels[4].get();

        ui::Y02RSurface skin (rig.ctx);
        skin.setSize (1920, 1080);
        skin.refreshAll();
        using F = ui::y02r::Function;
        // The faders: MIC 1-16, LINE, STEREO; FLIP puts TAPE 1-16 on them.
        expectEquals (skin.getFader (0).getAddress(), 0);
        expectEquals (skin.getFader (20).getAddress(), 54 * Yamaha02R::perStrip, "STEREO");
        skin.setFlip (true);
        expectEquals (skin.getFader (0).getAddress(), 20 * Yamaha02R::perStrip, "TAPE 1");
        skin.setFlip (false);
        // AUX 3: the faders are the inputs' sends to it, STEREO's the aux master.
        skin.pressFunction (F::Aux3);
        expectEquals (skin.getFader (2).getAddress(), ui::y02r::sendAddress (2, 2), "CH 3 to AUX 3");
        expectEquals (skin.getFader (20).getAddress(), ui::y02r::address (ui::y02r::kAux + 2, 0), "AUX 3's master");
        // A pair moves together from the surface.
        y.setState (Yamaha02R::Pair, 3, 1); // MIC 7/8
        skin.pressFunction (F::View);
        y.set (6 * Yamaha02R::perStrip, 100);
        expectEquals (y.get (7 * Yamaha02R::perStrip), 100, "CH 8 follows CH 7");
        // MS decoding (PAIR 1/2's MS): the faders apart, CH 8 the S with its right side reversed.
        y.setState (Yamaha02R::Ms, 3, 1);
        expectEquals (y.getState (Yamaha02R::Ms, 3), 1);
        y.set (6 * Yamaha02R::perStrip, 90);
        expectEquals (y.get (7 * Yamaha02R::perStrip), 100, "CH 8's fader its own");
        expect (engine.getChannels()[7]->params.msSide.load() && ! engine.getChannels()[6]->params.msSide.load());
        y.setState (Yamaha02R::Ms, 3, 0);
        expect (! engine.getChannels()[7]->params.msSide.load());
        y.set (6 * Yamaha02R::perStrip, 100);
        // The display on the selected channel (CH 5): the EQ's fields, LIBRARY by ENTER.
        skin.selectionChanged();
        auto& display = skin.getDisplay();
        expectEquals (display.getStrip(), 4);
        skin.pressFunction (F::Eq);
        expectEquals (display.getFields()[0].label, juce::String ("EQ"));
        y.set (ui::y02r::address (4, 5), y.get (ui::y02r::address (4, 5)) | (1 << 2)); // EQ ON
        y.set (ui::y02r::address (4, 8 + 4), 60); // L-MID G +12 dB
        const int library = (int) display.getFields().size() - 2;
        expectEquals (display.getFields()[(size_t) library].label, juce::String ("LIBRARY"));
        // STORE (the last field): a user program titled, kept in the settings.
        display.cursor (-1);
        expectEquals (display.getFields()[(size_t) display.getCursor()].label, juce::String ("STORE"));
        const auto was = y.programName (Yamaha02R::Eq, 32); // 33, the first a user may store into
        display.enter();
        expect (display.getFunction() == F::TitleEdit);
        display.turn (1); // its title's first character, the next one
        display.enter();
        expect (display.getFunction() == F::Eq);
        expect (y.programName (Yamaha02R::Eq, 32) != was && y.programName (Yamaha02R::Eq, 32).substring (1) == was.substring (1).trimEnd());
        expect (rig.settings.getValue (ui::Y02RSurface::libraryKey).contains ("eq.33="));
        // MIDI's second page, Program Change Assign; AUX 7's fourth, its effect's EDIT.
        skin.pressFunction (F::Midi);
        skin.pressFunction (F::Midi);
        expectEquals (display.getPage(), 1);
        display.getFields()[1].set (11);
        expectEquals (y.pcScene (0), 10);
        expect (rig.settings.getValue (Y02RMidi::pcKey).startsWith ("10 "));
        display.getFields()[1].set (2);
        // Control Change Assign (MIDI's third page): CC 1 moves MIC 1's fader, then its ON.
        skin.pressFunction (F::Midi);
        expectEquals (display.getPage(), 2);
        expectEquals (y.ccCode (1) & 7, 1, "the preset map: a fader");
        display.getFields()[1].set (2);
        expectEquals (y.ccCode (1) & 7, 2, "ON");
        display.getFields()[1].set (1);
        expect (rig.settings.getValue (Y02RMidi::ccKey).isNotEmpty());
        // VIEW's LIB opens the CHANNEL LIBRARY (VIEW 2/2): MIC 5 stored as program 1, recalled, cleared.
        skin.pressFunction (F::View);
        display.cursor (-1);
        expectEquals (display.getFields()[(size_t) display.getCursor()].label, juce::String ("LIB"));
        display.enter();
        expectEquals (display.getPage(), 1);
        expectEquals (display.getFields()[0].label, juce::String ("RECALL"));
        const int mic5 = ui::y02r::address (4, 0);
        y.set (mic5, 77);
        display.cursor (1);
        display.enter(); // STORE: its TITLE EDIT
        expect (display.getFunction() == F::TitleEdit);
        display.turn (1);
        display.enter();
        expect (display.getFunction() == F::View && display.getPage() == 1, "back in the library");
        expectEquals (y.programName (Yamaha02R::Channel, 0), juce::String ("A"));
        expect (rig.settings.getValue (ui::Y02RSurface::libraryKey).contains ("ch.1=A"));
        y.set (mic5, 20);
        display.getFields()[0].set (0); // RECALL program 1
        expectEquals (y.get (mic5), 77);
        display.getFields()[0].set (64); // the UNDO
        expectEquals (y.get (mic5), 20);
        display.getFields()[2].set (0); // CLEAR
        expect (y.programName (Yamaha02R::Channel, 0).isEmpty() && y.programName (Yamaha02R::Channel, 64) == "U:A");
        y.set (mic5, 101);
        // SETUP 3/4, PREFERENCES 1: STORE CONFIRMATION asks; CANCEL (ENTER at once) doesn't store.
        for (int k = 0; k < 3; ++k)
            skin.pressFunction (F::Setup);
        expectEquals (display.getPage(), 2);
        expect (ui::y02r::pref (rig.settings, ui::y02r::AutoEq) && ! ui::y02r::pref (rig.settings, ui::y02r::StoreConfirm), "the factory settings");
        display.getFields()[4].set (1); // STORE CONFIRM
        expect (ui::y02r::pref (rig.settings, ui::y02r::StoreConfirm));
        const int scenesBefore = rig.show.getNumScenes();
        skin.setMemory (scenesBefore + 1);
        skin.store();
        expect (display.isConfirming() && display.getConfirmChoice() == 0);
        display.enter();
        expect (! display.isConfirming());
        expectEquals (rig.show.getNumScenes(), scenesBefore, "cancelled");
        skin.store();
        display.cursor (1); // EXECUTE
        if (const auto shots = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_SCREENSHOTS", {}); shots.isNotEmpty())
        {
            const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (shots).getChildFile ("y02r-page-confirm.png");
            out.deleteFile();
            juce::FileOutputStream stream (out);
            juce::PNGImageFormat().writeImageToStream (display.createComponentSnapshot (display.getLocalBounds(), true, 1.0f), stream);
        }
        display.enter();
        expectEquals (rig.show.getNumScenes(), scenesBefore + 1, "stored");
        ui::y02r::setPref (rig.settings, ui::y02r::StoreConfirm, false);
        // INITIAL DATA NOMINAL: scene 0 puts the channel faders at 0 dB.
        ui::y02r::setPref (rig.settings, ui::y02r::InitialNominal, true);
        const int memoryWas = skin.getMemory();
        skin.setMemory (0);
        skin.recall();
        expectEquals (y.get (ui::y02r::address (2, 0)), 101);
        ui::y02r::setPref (rig.settings, ui::y02r::InitialNominal, false);
        skin.recall();
        expectEquals (y.get (ui::y02r::address (2, 0)), 0, "-oo");
        skin.setMemory (scenesBefore + 1);
        skin.recall(); // the mix back
        skin.setMemory (memoryWas);
        // TOUCH SENSE SELECT: touching fader 4 selects MIC 4.
        ui::y02r::setPref (rig.settings, ui::y02r::TouchSelect, true);
        skin.getFader (3).onTouch();
        expectEquals (display.getStrip(), 3);
        ui::y02r::setPref (rig.settings, ui::y02r::TouchSelect, false);
        skin.getFader (5).onTouch();
        expectEquals (display.getStrip(), 3, "off");
        skin.pressSel (4);
        skin.pressFunction (F::Meter);
        expectEquals (display.getFields()[0].label, juce::String ("METER POINT"));
        for (int k = 0; k < 4; ++k)
            skin.pressFunction (F::Aux7);
        expectEquals (display.getPage(), 3);
        expect (display.getFields().size() > 2, "the effect's type and parameters");
        skin.pressFunction (F::Eq);
        display.cursor (1);
        // SCENE MEMORY: STORE into scene 1, RECALL it.
        skin.setMemory (1);
        skin.store();
        expectEquals (rig.show.getNumScenes(), 1);
        y.set (0, 20);
        skin.recall();
        expectEquals (y.get (0), 101, "scene 1's CH 1");
        skin.pressFunction (F::Dynamics);
        expect (display.getFields().size() >= 8, "ON, TYPE, six parameters...");
        // The pages: a function's key again steps them; the all-channel pages.
        skin.pressFunction (F::Att);
        expectEquals (display.getPage(), 0);
        expectEquals ((int) display.getFields().size(), 20, "MIC 1-16, LINE 17/18-23/24");
        skin.pressFunction (F::Att);
        expectEquals (display.getPage(), 1, "TAPE, EFF and STEREO");
        expectEquals ((int) display.getFields().size(), 19);
        skin.pressFunction (F::Att);
        expectEquals (display.getPage(), 0, "and round");
        // ENTER double-clicked: every channel's attenuation as MIC 1's, STEREO aside.
        display.getFields()[0].set (90); // MIC 1 at -6 dB
        display.enterAll();
        expectEquals (y.get (ui::y02r::address (7, 4)), 90);
        expectEquals (y.get (ui::y02r::address (19, 4)), 90, "LINE 23/24");
        expectEquals (y.get (ui::y02r::address (ui::y02r::kTape + 3, 4)), 90, "TAPE 4, on the other page");
        expectEquals (y.get (ui::y02r::address (ui::y02r::kStereo, 4)), 96, "not STEREO");
        for (int st = 0; st < ui::y02r::kBus; ++st)
            y.set (ui::y02r::address (st, 4), 96);
        // SURROUND (V2 MAN pp.64-73): ROUTING 2/2 asks, then 3/4's pan graph and 4/4's trajectories.
        {
            skin.pressSel (4);
            skin.pressFunction (F::Routing);
            skin.pressFunction (F::Routing);
            expectEquals (display.getPage(), 1);
            expectEquals (display.getFields()[0].label, juce::String ("SURROUND"));
            display.getFields()[1].set (3); // 3+2+1, before it's on
            display.enter();
            expect (display.isConfirming(), "always asked");
            display.cursor (1);
            display.enter();
            expectEquals (y.getState (Yamaha02R::Surround, 0), 3);
            expectEquals (display.numPages(), 4);
            skin.pressFunction (F::Routing);
            expectEquals (display.getPage(), 2);
            expectEquals (display.getFields()[1].label, juce::String ("L / R"));
            const int mic5Pan = 4 * Yamaha02R::kSurFields;
            display.getFields()[1].set (10); // to the left
            display.getFields()[2].set (16); // towards the rear
            expectEquals (y.getState (Yamaha02R::SurroundPan, mic5Pan + Yamaha02R::SurX), 10);
            const auto shot = [&display] (const char* page)
            {
                if (const auto shots = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_SCREENSHOTS", {}); shots.isNotEmpty())
                {
                    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (shots).getChildFile (juce::String ("y02r-page-") + page + ".png");
                    out.deleteFile();
                    juce::FileOutputStream stream (out);
                    juce::PNGImageFormat().writeImageToStream (display.createComponentSnapshot (display.getLocalBounds(), true, 1.0f), stream);
                }
            };
            shot ("surround-pan");
            skin.pressFunction (F::Routing);
            expectEquals (display.getPage(), 3);
            display.getFields()[0].set (6); // CIRCLE
            expectEquals (display.getFields()[3].label, juce::String ("WIDTH"));
            display.getFields()[3].set (24);
            display.getFields()[4].set (20);
            display.getFields()[1].set (16); // a quarter round: the right
            expectEquals (y.getState (Yamaha02R::SurroundPan, mic5Pan + Yamaha02R::SurX), 32 + 24);
            shot ("surround-trajectory");
            // FAST: MOVE four steps a click.
            expectEquals (display.getFields()[2].label, juce::String ("FAST"));
            display.getFields()[2].set (1);
            display.getFields()[1].set (17);
            expectEquals (y.getState (Yamaha02R::SurroundPan, mic5Pan + Yamaha02R::SurAlong), 20);
            display.getFields()[2].set (0);
            // ST LINK on a made pair: MIC 6 mirrors MIC 5 (MIRROR L-R).
            const auto has = [&display] (const char* label)
            {
                for (const auto& f : display.getFields())
                    if (f.label == label)
                        return true;
                return false;
            };
            expect (! has ("ST LINK"), "MIC 5 unpaired");
            y.setState (Yamaha02R::Pair, 2, 1);
            skin.pressSel (4);
            expectEquals (display.getPage(), 3);
            expect (has ("ST LINK") && has ("PATTERN"));
            display.getFields()[1].set (8); // an eighth round: the front right
            const int mic6Pan = 5 * Yamaha02R::kSurFields;
            expectEquals (y.getState (Yamaha02R::SurroundPan, mic6Pan + Yamaha02R::SurX),
                          64 - y.getState (Yamaha02R::SurroundPan, mic5Pan + Yamaha02R::SurX));
            expectEquals (y.getState (Yamaha02R::SurroundPan, mic6Pan + Yamaha02R::SurY),
                          y.getState (Yamaha02R::SurroundPan, mic5Pan + Yamaha02R::SurY));
            shot ("surround-stlink");
            y.setState (Yamaha02R::Pair, 2, 0);
            y.setState (Yamaha02R::Surround, 0, 0);
            expectEquals (y.getState (Yamaha02R::Surround, 0), 0);
            skin.pressFunction (F::Eq);
        }
        // ROUTING 1/2 (V2 MAN pp.39-40): the selected channel's side, every channel
        // against BUS 1-8 and S; D under MIC 1-16.
        skin.pressFunction (F::Routing);
        expectEquals (display.getPage(), 0);
        expectEquals ((int) display.getFields().size(), 20 * 9 + 16);
        {
            const auto& cell = display.getFields()[2 * 20 + 4];
            expectEquals (cell.label, juce::String ("3"), "BUS 3");
            expectEquals (cell.strip, 4, "MIC 5");
            const int routed = cell.get();
            cell.set (1 - routed);
            expectEquals (cell.get(), 1 - routed);
            cell.set (routed);
            display.cursor (4); // down: BUS 2 of MIC 1
            expectEquals (display.getFields()[(size_t) display.getCursor()].label, juce::String ("2"));
            expectEquals (display.getFields()[(size_t) display.getCursor()].strip, 0);
        }
        skin.pressFunction (F::Eq);
        // DELAY digit by digit (MAN p.35): MIC 1's samples' hundreds, then in ms the tenths.
        skin.pressFunction (F::Delay);
        expectEquals (display.getFields()[0].label, juce::String ("UNIT"));
        display.cursor (1); // MIC 1, at its thousands
        expectEquals (display.getDigit(), 0);
        display.cursor (1); // its hundreds
        display.turn (2);
        const int mic1Delay = ui::y02r::address (0, 6);
        expectEquals ((y.get (mic1Delay) << 7) | y.get (mic1Delay + 1), 200);
        display.cursor (1);
        display.cursor (1);
        display.turn (-1); // ones
        expectEquals ((y.get (mic1Delay) << 7) | y.get (mic1Delay + 1), 199);
        display.cursor (1); // the next channel, at its thousands
        expectEquals (display.getCursor(), 2);
        expectEquals (display.getDigit(), 0);
        display.cursor (-1); // back: MIC 1's ones
        expectEquals (display.getDigit(), 3);
        display.getFields()[0].set (1); // mSEC
        expectEquals (display.getFields()[1].text (display.getFields()[1].get()).substring (0, 4), juce::String ("04.1"), "199 samples at 48 kHz");
        display.turn (1); // the tenths: 4.2 ms, 202 samples
        expectEquals ((y.get (mic1Delay) << 7) | y.get (mic1Delay + 1), 202);
        if (const auto shots = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_SCREENSHOTS", {}); shots.isNotEmpty())
        {
            const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (shots).getChildFile ("y02r-page-delay-digits.png");
            out.deleteFile();
            juce::FileOutputStream stream (out);
            juce::PNGImageFormat().writeImageToStream (display.createComponentSnapshot (display.getLocalBounds(), true, 1.0f), stream);
        }
        display.getFields()[1].set (0);
        display.getFields()[0].set (0);
        skin.pressFunction (F::Scene);
        expectEquals (display.getFields()[0].text (0), rig.show.getScene (0)->name, "the titles");
        skin.pressFunction (F::DigitalIo);
        expectEquals (display.getFields()[0].label, juce::String ("WORD CLOCK"));
        skin.pressFunction (F::Eq);
        skin.refreshAll();
        // Each page's look, for review (not the docs).
        if (const auto shots = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_SCREENSHOTS", {}); shots.isNotEmpty())
        {
            const std::tuple<F, int, const char*> pages[] = { { F::Scene, 0, "scene-titles" }, { F::Scene, 1, "scene-safe" }, { F::Scene, 2, "scene-fade" },
                                                              { F::Group, 0, "group-fader" }, { F::Group, 1, "group-mute" }, { F::Pair, 0, "pair" },
                                                              { F::Att, 0, "att" }, { F::Delay, 1, "delay-tape" }, { F::Pan, 0, "pan" }, { F::Routing, 0, "routing" },
                                                              { F::Meter, 2, "meter-outputs" }, { F::Meter, 0, "meter-inputs" }, { F::Eq, 0, "eq" }, { F::Dynamics, 0, "dynamics" }, { F::Pair, 1, "pair-aux" }, { F::Aux1, 0, "aux1" }, { F::Aux7, 2, "aux7" },
                                                              { F::Utility, 0, "utility" }, { F::DigitalIo, 0, "digital-io" }, { F::Setup, 0, "setup" },
                                                              { F::Automix, 0, "automix" }, { F::View, 1, "channel-library" },
                                                              { F::Setup, 2, "setup-prefs" }, { F::Setup, 3, "setup-prefs2" }, { F::View, 0, "view" } };
            for (auto [f, page, shot] : pages)
            {
                skin.pressFunction (F::Eq);
                for (int k = 0; k <= page; ++k)
                    skin.pressFunction (f);
                const auto lcd = display.createComponentSnapshot (display.getLocalBounds(), true, 1.0f);
                const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (shots).getChildFile (juce::String ("y02r-page-") + shot + ".png");
                out.deleteFile();
                juce::FileOutputStream stream (out);
                juce::PNGImageFormat().writeImageToStream (lcd, stream);
            }
            skin.pressFunction (F::Eq);
        }

        const auto image = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
        expect (image.isValid() && image.getWidth() == 1920);
        const auto folder = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_SCREENSHOTS", {});
        if (folder.isNotEmpty())
        {
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile ("y02r-surface.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out), file.getFullPathName());
            {
                // On a bigger screen: the same panel, scaled.
                skin.setSize (2560, 1440);
                const auto big = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
                const auto bigFile = file.getSiblingFile ("y02r-surface-1440.png");
                bigFile.deleteFile();
                juce::FileOutputStream bigOut (bigFile);
                juce::PNGImageFormat().writeImageToStream (big, bigOut);
                skin.setSize (1920, 1080);
            }
            skin.pressFunction (F::Dynamics);
            const auto lcd = display.createComponentSnapshot (display.getLocalBounds(), true, 1.0f);
            const auto lcdFile = file.getSiblingFile ("y02r-display-dynamics.png");
            lcdFile.deleteFile();
            juce::FileOutputStream lcdOut (lcdFile);
            expect (lcdOut.openedOk() && juce::PNGImageFormat().writeImageToStream (lcd, lcdOut), lcdFile.getFullPathName());
        }
        setShownProfile (ld::kProfileWing);
    }

    /** The Universal Mixer on each console's profile, every view drawn: the X32
        surface's UNIVERSAL MIXER key opens it on an X32. */
    void universalOnEveryConsole()
    {
        const auto folder = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_SCREENSHOTS", {});
        for (const auto profile : { ld::kProfileX32, ld::kProfileProMix01, ld::kProfileYamaha02R, ld::kProfileSq5, ld::kProfileYamaha01V, ld::kProfileDLive })
        {
            beginTest ("The Universal Mixer on profile " + juce::String ((int) profile));
            MixerEngine engine (0);
            engine.configureProfile (profile);
            ShotRig rig (engine);
            setShownProfile (profile);
            ui::routing::setProfile (profile);
            rig.selected = engine.getChannels()[0].get();
            ui::UniversalSkin skin (rig.ctx);
            skin.setSize (1920, 1080);
            skin.refreshAll();
            skin.selectionChanged();
            for (auto view : { ui::UniversalSkin::View::Mixer, ui::UniversalSkin::View::Routing, ui::UniversalSkin::View::Fx,
                               ui::UniversalSkin::View::Monitoring, ui::UniversalSkin::View::Scenes, ui::UniversalSkin::View::Library, ui::UniversalSkin::View::Recorder,
                               ui::UniversalSkin::View::Setup })
            {
                skin.showView (view);
                skin.refreshAll();
                const auto image = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
                expect (image.isValid());
                if (folder.isNotEmpty())
                {
                    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder)
                                          .getChildFile ("universal-on-" + juce::String ((int) profile) + "-" + juce::String ((int) view) + ".png");
                    file.deleteFile();
                    juce::FileOutputStream out (file);
                    expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out), file.getFullPathName());
                }
            }
            setShownProfile (ld::kProfileWing);
            ui::routing::setProfile (ld::kProfileWing);
        }
    }

private:
    /** The ProMix 01 surface (Phase 5 step 7) on a ProMix 01 with a mix on it. */
    void proMixSurface()
    {
        beginTest ("The ProMix 01 surface, on a ProMix 01");
        MixerEngine engine (0);
        engine.configureProfile (ld::kProfileProMix01);
        ShotRig rig (engine);
        setShownProfile (ld::kProfileProMix01);
        auto& pm = engine.getProMix();
        auto& channels = engine.getChannels();
        for (int i = 0; i < 16; ++i)
        {
            pm.set (i, i % 5 == 0 ? 100 : 95 - 4 * (i % 4)); // levels on the console's steps
            channels[(size_t) i]->meters.inputPeak[0] = 0.08f + 0.5f * (float) ((i * 37) % 11) / 10.0f;
            channels[(size_t) i]->meters.outputPeak[0] = channels[(size_t) i]->meters.inputPeak[0].load();
        }
        pm.set (16, 107); // ST IN
        pm.set (21, 100); // ST OUT
        pm.set (22 + 12, 0); // CH13 OFF
        engine.getMains()[0]->meters.outputPeak[0] = 0.2f;
        engine.getMains()[0]->meters.outputPeak[1] = 0.18f;
        rig.meters.advance (0.01f);
        rig.selected = channels[4].get();

        ui::ProMixSurface skin (rig.ctx);
        skin.setSize (1920, 1080);
        skin.refreshAll();
        using F = ui::promix::Function;
        // The faders: the channels' levels, then in SEND1 their sends to it, RTN1 on RTN / SEND.
        expectEquals (skin.getFader (0).getParam(), 0);
        expectEquals (skin.getFader (18).getParam(), 21, "ST OUT");
        skin.pressFunction (F::Send1);
        expectEquals (skin.getFader (2).getParam(), 68 + 2, "CH3 to SEND1");
        expectEquals (skin.getFader (17).getParam(), 17, "RTN1");
        skin.pressFunction (F::Send4);
        expectEquals (skin.getFader (17).getParam(), 20, "SEND4's master");
        expectEquals (skin.getRtnSend(), 3);
        // A pair moves together from the surface (the panel's operations).
        pm.set (503 + 3, 3); // CH7/8 ST (RESET)
        skin.pressFunction (F::Meter);
        pm.set (6, 100);
        skin.getFader (7).refresh();
        expectEquals (pm.get (7), 100, "CH8 follows CH7");
        // GROUP: SEL puts a channel in the group shown.
        skin.pressFunction (F::Group);
        skin.pressSel (9);
        expectEquals (pm.get (484 + 9), 1, "CH10 in A");
        skin.pressSel (9);
        expectEquals (pm.get (484 + 9), 0, "and out");
        // COMP: its fields, PATCH set by ENTER; COMP again: processor 2.
        skin.pressFunction (F::Comp);
        auto& display = skin.getDisplay();
        expectEquals (display.getFields()[1].label, juce::String ("PATCH"));
        display.cursor (1);
        display.turn (5); // CH5, chosen
        expectEquals (pm.get (449), 0, "not until ENTER");
        display.enter();
        expectEquals (pm.get (449), 5, "COMP1 on CH5");
        skin.pressFunction (F::Comp);
        expectEquals (display.getProcessor(), 1);
        // SCENE MEMORY: STORE into scene 1, RECALL it.
        skin.setMemory (1);
        skin.store();
        expectEquals (rig.show.getNumScenes(), 1);
        pm.set (0, 20);
        skin.recall();
        expectEquals (pm.get (0), 100, "scene 1's CH1");
        // STORE's list (the chosen memory), then TITLE EDIT of what it stored.
        skin.setMemory (2);
        display.setFunction (F::MemoryStore);
        expectEquals (display.getFields()[0].pending, 2);
        display.enter();
        expectEquals (rig.show.getNumScenes(), 2);
        expect (display.getFunction() == F::TitleEdit);
        display.turn (1); // "SCENE 02": S to T
        display.enter();
        expect (display.getFunction() == F::MemoryRecall);
        expectEquals (rig.show.getScene (1)->name, juce::String ("TCENE 02"));
        // MEMORY PROTECT, the UTILITY menu's fourth item (p.96): STORE refuses a
        // protected memory. EXIT and the key again: the menu.
        skin.pressFunction (F::Utility);
        expectEquals (display.getPage(), 0, "the menu");
        display.turn (3);
        display.enter();
        expectEquals (display.getPage(), 4);
        display.turn (1);
        skin.setMemory (3);
        display.setFunction (F::MemoryStore);
        display.enter();
        expectEquals (rig.show.getNumScenes(), 2, "protected");
        skin.pressFunction (F::Utility);
        display.setPage (4);
        display.turn (-1);
        display.cursor (-1);
        expectEquals (display.getFields()[(size_t) display.getCursor()].label, juce::String ("EXIT"));
        display.enter();
        expectEquals (display.getPage(), 0, "EXIT: the menu");
        // EQ LIBRARY's STORE: user program 31, named, kept in the settings.
        skin.pressFunction (F::Library);
        display.cursor (1);
        expectEquals (display.getFields()[(size_t) display.getCursor()].label, juce::String ("STORE"));
        display.enter();
        expect (display.getFunction() == F::TitleEdit);
        display.turn (1); // "USER LIBRARY 1": U to V
        display.enter();
        expect (display.getFunction() == F::Library);
        expectEquals (pm.programName (ProMix::Eq, 30), juce::String ("VSER LIBRARY 1"));
        expect (rig.settings.getValue (ui::ProMixSurface::libraryKey).contains ("VSER LIBRARY 1"));
        // PROGRAM CHANGE ASSIGN, the MIDI menu's second item: PGM 1 to memory 04, kept in the settings.
        skin.pressFunction (F::Midi);
        display.setPage (2);
        display.getFields()[1].set (5);
        expectEquals (pm.pcScene (0), 4);
        expect (rig.settings.getValue (ProMixMidi::pcKey).startsWith ("4 "));
        display.getFields()[1].set (2);
        // CONTROL CHANGE ASSIGN (the third item): parameter 0's bank.
        display.setPage (3);
        const int cc0 = pm.ccCode (0);
        display.getFields()[1].set (3);
        expectEquals (pm.ccCode (0) >> 8, 3);
        expect (rig.settings.getValue (ProMixMidi::ccKey).isNotEmpty());
        pm.setCcCode (0, cc0);
        // LOCAL ON/OFF (the fifth item): Local OFF lets the faders and [SEL] go, only
        // while the page shows; MEMORY CC OUT (the sixth): its data types.
        display.setPage (5);
        expect (! display.isLocalOff(), "ON at first");
        display.getFields()[0].set (0);
        expect (display.isLocalOff());
        const int level3 = pm.get (3);
        display.localMoved (3, 0x40);
        expectEquals (pm.get (3), level3, "a fader moves nothing here");
        expectEquals (display.getFields()[2 + 3].text(), juce::String ("40"));
        skin.pressSel (3);
        expect (display.getFields()[1].text().isNotEmpty(), "a [SEL]: its program change");
        display.setPage (6);
        expect (! display.isLocalOff(), "only while LOCAL ON/OFF shows");
        expectEquals (display.getFields()[2].label, juce::String ("LEVEL"));
        display.getFields()[2].set (0);
        expectEquals (display.getFields()[2].text(), juce::String ("OFF"));
        expectEquals (display.getFields()[9].textOf (0), juce::String ("EXECUTE"));
        // GROUP's BAL ADJ: ENTER, off again when GROUP goes.
        skin.pressFunction (F::Group);
        display.enter();
        expect (pm.balAdj());
        skin.pressFunction (F::Meter);
        expect (! pm.balAdj());
        // METER's PEAK HOLD.
        skin.pressFunction (F::Meter);
        expectEquals (display.getFields()[0].label, juce::String ("PEAK HOLD"));
        // Each page's look, for review (not the docs).
        if (const auto shots = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_SCREENSHOTS", {}); shots.isNotEmpty())
        {
            const auto save = [&] (const char* shot)
            {
                const auto lcd = display.createComponentSnapshot (display.getLocalBounds(), true, 1.0f);
                const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (shots).getChildFile (juce::String ("promix-page-") + shot + ".png");
                out.deleteFile();
                juce::FileOutputStream stream (out);
                juce::PNGImageFormat().writeImageToStream (lcd, stream);
            };
            display.setFunction (F::Midi);
            display.setPage (5);
            save ("local");
            display.setPage (6);
            save ("cc-out");
            display.setFunction (F::Group);
            display.enter();
            save ("group-bal-adj");
            display.enter();
            display.setFunction (F::MemoryRecall);
            save ("recall");
            display.editTitle (2, "TCENE 02");
            save ("title");
            for (int k = 0; k < 4; ++k)
            {
                skin.pressFunction (F::Utility);
                display.setPage (k + 1);
                save ((juce::String ("utility") + juce::String (k)).toRawUTF8());
            }
            skin.pressFunction (F::Cue);
            save ("cue");
            // CUE on ST IN and ST OUT (pp.53-54).
            display.setEqStrip (16);
            save ("cue-stin");
            display.setEqStrip (19);
            save ("cue-stout");
            display.setEqStrip (4);
            skin.pressFunction (F::Midi);
            display.setPage (4);
            save ("bulk");
            // Every function's every page, to set beside the manual's pictures.
            const std::pair<F, const char*> all[] = { { F::Meter, "meter" }, { F::Pan, "pan" }, { F::Phase, "phase" }, { F::Comp, "comp" },
                                                      { F::Cue, "cue" }, { F::Send1, "send1" }, { F::Send3, "send3" }, { F::EqMid, "eq" },
                                                      { F::Library, "library" }, { F::Group, "group" }, { F::Pair, "pair" }, { F::Utility, "utility" },
                                                      { F::Midi, "midi" }, { F::MemoryStore, "store" } };
            for (auto [f, fn] : all)
            {
                display.setFunction (f == F::Meter ? F::Pan : F::Meter);
                display.setFunction (f);
                for (int p = 0; p < display.numPages(); ++p)
                {
                    display.setPage (p);
                    save ((juce::String ("all-") + fn + "-" + juce::String (p)).toRawUTF8());
                }
            }
            display.setFunction (F::Meter);
        }
        // The EQ function on the selected channel (CH5), a bell on MID.
        skin.selectionChanged();
        expectEquals (display.getEqStrip(), 4);
        pm.set (296 + 4, 24); // MID G +9 dB
        pm.set (196 + 4, 1);  // EQ ON
        skin.pressFunction (F::EqMid);
        expectEquals (display.getFields()[(size_t) display.getCursor()].label, juce::String ("MID G"));
        skin.refreshAll();

        const auto image = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
        expect (image.isValid() && image.getWidth() == 1920);
        const auto folder = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_SCREENSHOTS", {});
        if (folder.isNotEmpty())
        {
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile ("promix-surface.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out), file.getFullPathName());
            {
                // On a bigger screen: the same panel, scaled.
                skin.setSize (2560, 1440);
                const auto big = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
                const auto bigFile = file.getSiblingFile ("promix-surface-1440.png");
                bigFile.deleteFile();
                juce::FileOutputStream bigOut (bigFile);
                juce::PNGImageFormat().writeImageToStream (big, bigOut);
                skin.setSize (1920, 1080);
            }
            // The LCD's COMP function.
            skin.pressFunction (F::Comp);
            skin.pressFunction (F::Comp);
            skin.pressFunction (F::Comp); // back to COMP 1
            const auto lcd = display.createComponentSnapshot (display.getLocalBounds(), true, 1.0f);
            const auto lcdFile = file.getSiblingFile ("promix-display-comp.png");
            lcdFile.deleteFile();
            juce::FileOutputStream lcdOut (lcdFile);
            expect (lcdOut.openedOk() && juce::PNGImageFormat().writeImageToStream (lcd, lcdOut), lcdFile.getFullPathName());
        }
        setShownProfile (ld::kProfileWing);
    }

    /** The X32 surface (Phase 4 step 8) on an X32 with a few channels named. */
    void x32Surface()
    {
        beginTest ("The X32 surface, on an X32");
        MixerEngine engine (0);
        engine.configureProfile (ld::kProfileX32);
        ShotRig rig (engine);
        setShownProfile (ld::kProfileX32);
        const char* names[] = { "Kick", "Snare", "Hat", "Tom 1", "Tom 2", "OH L", "OH R", "Bass", "Gtr 1", "Gtr 2", "Keys L", "Keys R", "Vox 1", "Vox 2", "Vox 3", "Click" };
        const int colours[] = { 2, 2, 2, 2, 2, 3, 3, 5, 4, 4, 6, 6, 7, 7, 7, 9 };
        auto& channels = engine.getChannels();
        for (int i = 0; i < 16; ++i)
        {
            auto& s = *channels[(size_t) i];
            s.setName (names[i]);
            s.setColourIndex (colours[i]);
            s.params.faderDb = i % 5 == 0 ? 0.0f : -6.0f - (float) (i % 4) * 3.0f;
            s.meters.outputPeak[0] = 0.15f + 0.6f * (float) ((i * 37) % 11) / 10.0f;
        }
        channels[12]->params.mute = true;
        channels[1]->params.solo = true;
        engine.getMains()[0]->params.faderDb = 0.0f;
        engine.getMains()[0]->meters.outputPeak[0] = 0.5f;
        engine.getMains()[0]->meters.outputPeak[1] = 0.48f;
        rig.meters.advance (0.01f);
        rig.selected = channels[12].get();

        ui::X32Surface skin (rig.ctx);
        skin.setSize (1920, 1080);
        skin.selectionChanged();
        skin.refreshAll();
        // The layers: AUX IN / FX RTN shows aux in 1-8, then FX returns 1-8;
        // MATRIX / M/C the matrices and, last, the M/C.
        expect (skin.getInputStrip (0).getStrip() == channels[0].get());
        skin.setInputLayer (2); // AUX / FX RTN
        expectEquals (skin.getInputStrip (8).getStrip()->getLabel(), juce::String ("FX 1"));
        expectEquals (skin.getInputStrip (6).getName(), juce::String ("USB L"));
        skin.setGroupLayer (ui::X32Surface::GroupLayer::MatrixMain);
        expect (skin.getGroupStrip (7).getStrip() == engine.getMains()[1].get(), "the M/C on the last fader");
        expect (skin.getGroupStrip (6).isEmpty());
        skin.setGroupLayer (ui::X32Surface::GroupLayer::Dca);
        expectEquals (skin.getGroupStrip (0).getName(), juce::String ("DCA 1"));
        skin.setInputLayer (0);
        // The channel strip section turns the selected strip's X32 models.
        auto& section = skin.getChannelSection();
        expect (section.getStrip() == channels[12].get());
        auto* thr = section.knob ("GATE", "THRESHOLD");
        expect (thr != nullptr);
        if (thr != nullptr)
        {
            thr->getBinding().set (0.5f);
            expectWithinAbsoluteError (channels[12]->params.gate().values[0].load(), -40.0f, 0.5f, "X32GATE thr -80..0");
        }
        expect (section.knob ("EQUALIZER", "FREQ") != nullptr);
        channels[12]->params.gate().on = true;
        channels[12]->params.eq().on = true;
        channels[12]->params.eq().values[2] = 4.5f; // band 1 gain
        channels[12]->meters.dynReductionDb = 6.0f;
        channels[12]->params.dynamics().on = true;
        skin.selectionChanged(); // the knobs and keys show it at once

        // The display: HOME's encoders as the X32's manual maps them, and touch.
        auto& display = skin.getDisplay();
        auto& gate = channels[12]->params.gate();
        expect (display.getScreen() == ui::x32::Display::Screen::Home && display.getTab() == 0);
        expectEquals (display.getEncoder (1).pushLabel, juce::String ("GATE"));
        display.pushEncoder (1);
        expect (! gate.on.load(), "encoder 2's push: the gate off");
        display.pushEncoder (1);
        expect (gate.on.load());
        auto touch = [&display] (juce::Point<int> at, juce::Point<int> to)
        {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            auto event = [&] (juce::Point<int> p, bool dragged)
            {
                return juce::MouseEvent (source, p.toFloat(), juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &display, &display, now,
                                         at.toFloat(), now, 1, dragged);
            };
            display.mouseDown (event (at, false));
            if (to != at)
                display.mouseDrag (event (to, true));
            display.mouseUp (event (to, to != at));
        };
        touch (display.getTabBounds (2).getCentre(), display.getTabBounds (2).getCentre());
        expectEquals (display.getTab(), 2, "a tap on the gate tab");
        expectEquals (display.getEncoder (1).turn.label, juce::String ("MODE"));
        const float before = gate.values[0].load();
        const auto cell = display.getEncoderCell (0).getCentre();
        touch (cell, cell.translated (0, -40));
        expect (gate.values[0].load() > before, "a drag up on the encoder row turns the threshold up");
        expect (gate.on.load(), "a drag isn't a push");
        touch (cell, cell);
        expect (! gate.on.load(), "a tap on the encoder row is its push");
        gate.on = true;

        // HOME config (p.45): LINK on encoder 1's push, the pair then kept alike;
        // SOURCE turned through the X32's list, patched by SELECT.
        display.showScreen (ui::x32::Display::Screen::Home, 1);
        expectEquals (display.getEncoder (0).pushLabel, juce::String ("LINK"));
        expect (! display.getEncoder (0).pushOn());
        channels[12]->params.faderDb = -12.0f;
        display.pushEncoder (0);
        expect (display.getEncoder (0).pushOn(), "13-14 linked");
        expectWithinAbsoluteError (channels[13]->params.faderDb.load(), -12.0f, 0.2f, "linking: 14 takes 13's fader");
        channels[13]->params.faderDb = -6.0f;
        engine.getWing().followX32Links();
        expectWithinAbsoluteError (channels[12]->params.faderDb.load(), -6.0f, 0.2f, "a move on 14 reaches 13");
        expectEquals (display.getEncoder (2).turn.label, juce::String ("SOURCE"));
        const int inputBefore = channels[12]->params.inputLeft.load();
        display.getEncoder (2).turn.set (40.0f / 64.0f);
        expectEquals (display.getEncoder (2).turn.text(), juce::String ("USB R"));
        expectEquals (channels[12]->params.inputLeft.load(), inputBefore, "turning alone patches nothing");
        expect (display.getEncoder (2).pushOn());
        display.pushEncoder (2);
        expect (! display.getEncoder (2).pushOn());
        expect (channels[12]->params.inputLeft.load() != inputBefore, "SELECT patches the USB player's R");
        display.getEncoder (2).turn.set (13.0f / 64.0f);
        display.pushEncoder (2); // back to In 13
        display.pushEncoder (0);
        expect (! display.getEncoder (0).pushOn(), "unlinked again");
        expectEquals (display.getEncoder (5).pushLabel, juce::String ("CONNECT"));
        display.getEncoder (5).turn.set (9.0f / 22.0f);
        expectEquals (display.getEncoder (5).turn.text(), juce::String ("FX5L"));
        display.pushEncoder (5);
        expectEquals (channels[12]->params.preInsert().fx.load(), 4, "CONNECT: FX 5 in the insert");
        display.getEncoder (5).turn.set (0.0f);
        display.pushEncoder (5);
        display.showScreen (ui::x32::Display::Screen::Home, 4);
        expectEquals (display.getEncoder (2).turn.label, juce::String ("GAIN"));

        const auto image = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
        expect (image.isValid() && image.getWidth() == 1920);
        const auto folder = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_SCREENSHOTS", {});
        if (folder.isNotEmpty())
        {
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile ("x32-surface.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out), file.getFullPathName());
            {
                skin.setSize (2560, 1440);
                const auto big = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
                const auto bigFile = file.getSiblingFile ("x32-surface-1440.png");
                bigFile.deleteFile();
                juce::FileOutputStream bigOut (bigFile);
                juce::PNGImageFormat().writeImageToStream (big, bigOut);
                skin.setSize (1920, 1080);
            }

            // The display's HOME tab: the signal path.
            display.showScreen (ui::x32::Display::Screen::Home, 0);
            const auto home = display.createComponentSnapshot (display.getLocalBounds(), true, 1.0f);
            const auto homeFile = file.getSiblingFile ("x32-display-home.png");
            homeFile.deleteFile();
            juce::FileOutputStream homeOut (homeFile);
            expect (homeOut.openedOk() && juce::PNGImageFormat().writeImageToStream (home, homeOut), homeFile.getFullPathName());
        }

        // SETUP > remote (step 6b): the X32's MIDI devices and what goes in and out.
        display.showScreen (ui::x32::Display::Screen::Setup, 2);
        expectEquals (display.getEncoder (0).turn.label, juce::String ("MIDI IN"));
        expectEquals (display.getEncoder (2).turn.label, juce::String ("PROGRAM"));
        display.setLayer (1);
        expectEquals (display.getEncoder (0).turn.label, juce::String ("OSC SYSEX"));
        if (folder.isNotEmpty())
        {
            display.setLayer (0);
            const auto remote = display.createComponentSnapshot (display.getLocalBounds(), true, 1.0f);
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile ("x32-display-remote.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (remote, out), file.getFullPathName());
        }

        // ROUTING (step 10c): the input blocks, as the X32 map answers them.
        display.showScreen (ui::x32::Display::Screen::Routing);
        expectEquals (display.getEncoder (0).turn.label, juce::String ("IN 1-8"));
        expectEquals (display.getEncoder (2).turn.text(), juce::String ("AN17-24"), "inputs 17-24 local, as on a full-size X32");
        display.getEncoder (0).turn.set (10.0f / 23.0f); // B1-8
        expectEquals (channels[2]->params.inputLeft.load(), 82, "channel 3 from AES50 B 3 (interface input 83)");
        display.getEncoder (0).turn.set (0.0f); // AN1-8
        expectEquals (channels[2]->params.inputLeft.load(), 2);
        if (folder.isNotEmpty())
        {
            const auto routing = display.createComponentSnapshot (display.getLocalBounds(), true, 1.0f);
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile ("x32-display-routing.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (routing, out), file.getFullPathName());
        }

        // ROUTING's output tabs: out 1-16's signals (Main LR on 15-16), then its blocks.
        display.showScreen (ui::x32::Display::Screen::Routing, 1);
        // The manual's encoders (p.52): 3 the category, 4 the signal, pushed to assign.
        expectEquals (display.getEncoder (3).turn.label, juce::String ("SIGNAL"));
        display.getEncoder (0).turn.set (14.0f / 15.0f); // OUT 15
        expectEquals (display.getEncoder (3).turn.text(), juce::String ("MAIN L"));
        expectEquals (display.getEncoder (2).turn.text(), juce::String ("MAIN"));
        const int mainL = engine.getOutputSource (14);
        display.getEncoder (2).turn.set (2.0f / 5.0f); // MIX BUS
        expectEquals (display.getEncoder (3).turn.text(), juce::String ("MIXBUS 01"));
        expectEquals (engine.getOutputSource (14), mainL, "not until ASSIGN");
        display.pushEncoder (3);
        expect (engine.getOutputSource (14) == engine.getOutputSource (0) && engine.getOutputSource (14) != mainL, "output 15 plays bus 1");
        display.getEncoder (2).turn.set (1.0f / 5.0f); // MAIN: MAIN L
        display.pushEncoder (3);
        expectEquals (engine.getOutputSource (14), mainL);
        if (folder.isNotEmpty())
        {
            const auto outs = display.createComponentSnapshot (display.getLocalBounds(), true, 1.0f);
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile ("x32-display-outputs.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (outs, out), file.getFullPathName());
        }
        display.showScreen (ui::x32::Display::Screen::Routing, 7); // xlr out
        expectEquals (display.getEncoder (3).turn.text(), juce::String ("OUT13-16"));
        display.showScreen (ui::x32::Display::Screen::Routing, 4); // card out
        expectEquals (display.getEncoder (0).turn.text(), juce::String ("AN1-8"));
        display.getEncoder (0).turn.set (10.0f / 35.0f); // B1-8
        expectEquals (engine.getOutputSource (128), 80, "card 1 records AES50 B 1");
        display.getEncoder (0).turn.set (0.0f);
        expectEquals (engine.getOutputSource (128), 0);

        // EFFECTS (step 11): home picks a slot and its model; fx1-fx8 its parameters.
        {
            auto& fx = engine.getFx();
            display.showScreen (ui::x32::Display::Screen::Effects);
            expectEquals (display.getEncoder (1).turn.text(), juce::String ("Vintage Room"), "FX 1's factory model");
            display.getEncoder (0).turn.set (4.0f / 7.0f); // FX 5
            display.getEncoder (1).turn.set (0.0f);
            const auto first = models::model (models::Fx, fx.getModel (4));
            expect (first.category != models::Category::FxPremium, "FX 5 takes the insert models only: " + first.name);
            display.getEncoder (0).turn.set (0.0f); // FX 1
            display.pushEncoder (1);                // EDIT
            expectEquals (display.getTab(), 1);
            expect (display.getEncoder (0).turn.label.isNotEmpty(), "FX 1's first parameter");
            display.getEncoder (0).turn.set (0.0f);
            const float low = fx.params (0).values[0].load();
            display.getEncoder (0).turn.set (1.0f);
            expect (fx.params (0).values[0].load() > low, "the encoder turns FX 1's parameter: " + juce::String (low));
            display.getEncoder (0).turn.set (0.5f);
            if (folder.isNotEmpty())
            {
                const auto shot = display.createComponentSnapshot (display.getLocalBounds(), true, 1.0f);
                const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile ("x32-display-effects.png");
                file.deleteFile();
                juce::FileOutputStream out (file);
                expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (shot, out), file.getFullPathName());
            }
        }

        // SENDS ON FADER (step 12): a bus selected, the input faders are the sends to it;
        // a channel selected, the bus layers are its sends.
        {
            auto& buses = engine.getBuses();
            skin.setInputLayer (0);
            rig.selected = buses[2].get(); // BUS 3
            skin.setSendsOnFader (true);
            expectEquals (skin.getInputStrip (0).getSendTo(), juce::String ("BUS 3"));
            auto& send = channels[0]->sends[2].levelDb;
            send.store (-20.0f);
            expect (skin.getInputStrip (0).getStrip() == channels[0].get(), "the strip stays");
            if (folder.isNotEmpty())
            {
                const auto shot = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
                const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile ("x32-sends-on-fader.png");
                file.deleteFile();
                juce::FileOutputStream out (file);
                expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (shot, out), file.getFullPathName());
            }
            rig.selected = channels[4].get(); // CH 5: its sends to buses 9-16
            skin.setGroupLayer (ui::X32Surface::GroupLayer::Bus9to16);
            skin.selectionChanged();
            expect (skin.getGroupStrip (0).getSendTo().startsWith ("FROM"), skin.getGroupStrip (0).getSendTo());
            expectEquals (skin.getInputStrip (0).getSendTo(), juce::String(), "the inputs are their own faders again");
            skin.setSendsOnFader (false);
            expectEquals (skin.getGroupStrip (0).getSendTo(), juce::String());
            skin.setGroupLayer (ui::X32Surface::GroupLayer::Dca);
            rig.selected = channels[12].get();
            skin.selectionChanged();
        }

        // MONITOR / TALKBACK (step 12b): the engine's monitor and talk controls.
        {
            auto& mon = skin.getMonitorSection();
            expect (mon.knob ("MONITOR") != nullptr && mon.knob ("PHONES") != nullptr && mon.knob ("TALK") != nullptr);
            mon.knob ("PHONES")->getBinding().set (0.0f);
            expect (engine.phones.levelDb.load() < -60.0f, "PHONES down");
            mon.key ("DIM")->onClick();
            expect (engine.monitorDim.load());
            mon.key ("DIM")->onClick();
            mon.key ("TALK B")->onClick();
            expect (engine.talk.paths[1].on.load(), "TALK B latches");
            mon.key ("TALK B")->onClick();
            expect (! engine.talk.paths[1].on.load());
        }

        // ASSIGN (step 12c): codes kept by the X32 map, acted on by the surface.
        {
            auto& assign = skin.getAssignSection();
            int set = 0;
            auto& wing = engine.getWing();
            wing.runX32Text ("/config/userctrl/A/enc/1 F05", set);  // CH 6's fader
            wing.runX32Text ("/config/userctrl/A/btn/5 O80", set);  // mute group 1
            wing.runX32Text ("/config/userctrl/A/btn/6 P0004", set); // CH 1's EQ page
            assign.refreshCodes();
            expectEquals (assign.getEncoder (0).getBinding().label, juce::String ("FDR CH 6"));
            assign.getEncoder (0).getBinding().set (0.0f);
            expect (channels[5]->params.faderDb.load() < -60.0f, "encoder 1 pulls CH 6's fader down: " + juce::String (channels[5]->params.faderDb.load()));
            expectEquals (assign.getButton (0).getButtonText(), juce::String ("MUTE G1"));
            assign.getButton (0).onClick();
            expect (engine.muteGroups.contains (0));
            assign.getButton (0).onClick();
            expect (! engine.muteGroups.contains (0));
            assign.getButton (1).onClick();
            expect (display.getScreen() == ui::x32::Display::Screen::Home && display.getTab() == 4, "the page jump");
            if (folder.isNotEmpty())
            {
                const auto shot = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
                const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile ("x32-assign.png");
                file.deleteFile();
                juce::FileOutputStream out (file);
                expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (shot, out), file.getFullPathName());
            }
            channels[5]->params.faderDb.store (0.0f);
            rig.selected = channels[12].get();
            skin.selectionChanged();
        }

        // LIBRARY > show and UTILITY (step 12d): the show file keys; COPY / PASTE.
        {
            display.showScreen (ui::x32::Display::Screen::Library, 1);
            expectEquals (display.getEncoder (0).pushLabel, juce::String ("NEW SHOW"));
            expectEquals (display.getEncoder (3).pushLabel, juce::String ("SAVE AS"));
            display.showScreen (ui::x32::Display::Screen::Utility);
            auto& eqFrom = channels[0]->params.eq();
            const float was = channels[3]->params.eq().values[2].load();
            eqFrom.values[2] = was + 3.0f; // band 1's gain
            rig.selected = channels[0].get();
            skin.selectionChanged();
            display.pushEncoder (0); // COPY CH 1
            rig.selected = channels[3].get();
            skin.selectionChanged();
            display.pushEncoder (1); // PASTE onto CH 4
            expectWithinAbsoluteError (channels[3]->params.eq().values[2].load(), was + 3.0f, 1e-4f, "CH 1's EQ on CH 4");
            display.pushEncoder (4); // EQ off the list
            channels[3]->params.eq().values[2] = was;
            display.pushEncoder (1);
            expectWithinAbsoluteError (channels[3]->params.eq().values[2].load(), was, 1e-4f, "EQ not pasted");
            eqFrom.values[2] = was;
            rig.selected = channels[12].get();
            skin.selectionChanged();
        }
        // SCENES: NEW, then GO; the param safes.
        {
            using S = ui::x32::Display::Screen;
            display.showScreen (S::Scenes);
            const int scenesBefore = rig.show.getNumScenes();
            display.pushEncoder (2); // NEW
            expectEquals (rig.show.getNumScenes(), scenesBefore + 1);
            display.pushEncoder (0); // GO
            expectEquals (rig.show.getCurrentSceneIndex(), scenesBefore);
            // SNIPPETS: NEW of the selected strip, FIRE with its scene, GO, DELETE.
            // SCENES: SAVE into a new slot without the EQ (a save safe); EXPORT, IMPORT.
            display.showScreen (S::Scenes, 1);
            display.getEncoder (5).turn.set ((float) [] { for (int k = 0; k < state::kNumSections; ++k) if (state::sectionAt (k) == state::Eq) return k; return 0; }()
                                             / (float) (state::kNumSections - 1));
            display.pushEncoder (5); // EQ safe
            const int saved = display.saveScene (rig.show.getNumScenes(), "Saved", "a note");
            expectEquals (rig.show.getScene (saved)->name, juce::String ("Saved"));
            expectEquals (rig.show.getScene (saved)->notes, juce::String ("a note"));
            expect ((rig.show.getScene (saved)->scope & (juce::uint32) state::Eq) == 0, "EQ left out");
            display.pushEncoder (5);
            rig.show.removeScene (saved);
            // MIDI: a program change on channel 3.
            display.showScreen (S::Scenes, 5);
            display.getEncoder (1).turn.set (1.0f / 3.0f);
            expectEquals (display.getEncoder (1).turn.text(), juce::String ("PRG CHANGE"));
            display.getEncoder (2).turn.set (2.0f / 15.0f);
            display.getEncoder (3).turn.set (5.0f / 127.0f);
            expectEquals (rig.show.getScene (scenesBefore)->midi, juce::String ("C2 05"));
            {
                const auto m = ui::x32::Display::parseSceneMidi ("B0,07,64");
                expect (m.kind == 2 && m.channel == 1 && m.number == 7 && m.value == 100);
                expectEquals (ui::x32::Display::parseSceneMidi ("U:C005").kind, -1);
            }
            display.getEncoder (1).turn.set (0.0f);
            expect (rig.show.getScene (scenesBefore)->midi.isEmpty());
            display.showScreen (S::Scenes, 2);
            display.pushEncoder (2); // NEW
            expectEquals (rig.show.getScene (scenesBefore)->numSnippets, 1);
            display.pushEncoder (3); // FIRE
            expect (rig.show.getSnippet (scenesBefore, 0)->fireWithScene);
            display.pushEncoder (0); // GO
            display.pushEncoder (2);
            expectEquals (rig.show.getScene (scenesBefore)->numSnippets, 2);
            display.pushEncoder (5); // DELETE the second
            expectEquals (rig.show.getScene (scenesBefore)->numSnippets, 1);
            // PARAM SAFE: HA (the preamp) safe; the console's column.
            display.showScreen (S::Scenes, 3);
            display.pushEncoder (0);
            expect (rig.show.getGlobalSafe().sections == (juce::uint32) state::Preamp);
            display.pushEncoder (0);
            expect (rig.show.getGlobalSafe().sections == 0);
            display.pushEncoder (3);
            expect (rig.show.getGlobalSafe().console);
            display.pushEncoder (3);
            expect (! rig.show.getGlobalSafe().console);
            // CHAN SAFE: CH 2 in column 1, FX 3 in column 6.
            display.showScreen (S::Scenes, 4);
            display.getEncoder (0).turn.set (1.0f / 15.0f);
            expectEquals (display.getEncoder (0).turn.text(), channels[1]->getLabel());
            display.pushEncoder (0);
            expect (channels[1]->params.recallSafe.load());
            display.pushEncoder (0);
            expect (! channels[1]->params.recallSafe.load());
            display.getEncoder (5).turn.set (2.0f / 7.0f);
            display.pushEncoder (5);
            expect (rig.show.getGlobalSafe().fx == 4u);
            display.pushEncoder (5);
            expect (rig.show.getGlobalSafe().fx == 0u);
            // HOME gate's second layer: the key's source; METERS' RTA; ASSIGN's codes.
            display.showScreen (S::Home, 2);
            display.setLayer (1);
            expectEquals (display.getEncoder (5).turn.label, juce::String ("KEY SRC"));
            expectEquals (display.getEncoder (4).pushLabel, juce::String ("KEY SOLO"));
            display.setLayer (0);
            display.showScreen (S::Meters, 4);
            expectEquals (display.getEncoder (0).pushLabel, juce::String ("FREEZE"));
            display.showScreen (S::Library, 2);
            expectEquals (display.getEncoder (0).pushLabel, juce::String ("ON / OFF"));
            expectEquals (display.getEncoder (1).pushLabel, juce::String ("LOAD"));
            expectEquals (display.getEncoder (2).pushLabel, juce::String ("SAVE"));
            expectEquals (display.getEncoder (3).pushLabel, juce::String ("DELETE"));
            // LIBRARY > routing: SAVE the routing, change XLR 1-4, LOAD it back;
            // with XLR Out off the recall list, LOAD leaves it. (In a library
            // folder of the test's own.)
            {
                const auto home = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sidecar-x32-library-test");
                home.deleteRecursively();
                home.createDirectory();
               #if JUCE_WINDOWS
                _putenv_s ("SIDECAR_HOME", home.getFullPathName().toRawUTF8());
               #else
                setenv ("SIDECAR_HOME", home.getFullPathName().toRawUTF8(), 1);
               #endif
                int set = 0;
                auto& wing = rig.engine.getWing();
                const auto xlr = [&wing, &set] { return wing.runX32Text ("/config/routing/OUT/1-4", set); };
                const auto xlrBefore = xlr();
                const juce::String other = xlrBefore.endsWith ("AN9-12") ? "AN1-4" : "AN9-12";
                display.showScreen (S::Library, 4);
                expect (display.savePreset (4, "Test routing").wasOk());
                expectEquals (display.getEncoder (1).turn.text(), juce::String ("Test routing"));
                wing.runX32Text ("/config/routing/OUT/1-4 " + other, set);
                expect (xlr() != xlrBefore);
                display.pushEncoder (1); // LOAD
                expectEquals (xlr(), xlrBefore);
                display.getEncoder (0).turn.set (1.0f); // XLR Out
                display.pushEncoder (0);                 // off the recall list
                wing.runX32Text ("/config/routing/OUT/1-4 " + other, set);
                display.pushEncoder (1);
                expect (xlr().endsWith (other));
                display.pushEncoder (0);
                wing.runX32Text ("/config/routing/OUT/1-4 " + xlrBefore.fromLastOccurrenceOf (" ", false, false), set);
               #if JUCE_WINDOWS
                _putenv_s ("SIDECAR_HOME", "");
               #else
                unsetenv ("SIDECAR_HOME");
               #endif
                home.deleteRecursively();
            }
            display.showScreen (S::Assign, 0);
            display.getEncoder (1).turn.set (1.0f / 5.0f); // ENC 1: FADER
            expect (skin.getAssignSection().getCode (0).startsWith ("F"), "a fader");
            display.getEncoder (2).turn.set (3.0f / 79.0f); // CH 4
            expectEquals (skin.getAssignSection().getCode (0), juce::String ("F03"));
            display.getEncoder (1).turn.set (0.0f);
            expectEquals (skin.getAssignSection().getCode (0), juce::String ("-"));
            // MONITOR: AFL on the fifth push; TALK A's bus.
            display.showScreen (S::Monitor);
            const bool afl = rig.engine.soloAfl.load();
            display.getEncoder (1).turn.set (3.0f / 6.0f); // the solo options: CHANNEL AFL
            expectEquals (display.getEncoder (1).turn.text(), juce::String ("CHANNEL AFL"));
            display.pushEncoder (1);
            expect (rig.engine.soloAfl.load() != afl);
            display.pushEncoder (1);
            expect (rig.engine.soloAfl.load() == afl);
            // TALK A: the destination list's LR, assigned with the fourth push.
            {
                int set = 0;
                auto& wing = rig.engine.getWing();
                display.showScreen (S::Monitor, 1);
                display.getEncoder (3).turn.set (16.0f / 17.0f);
                expectEquals (display.getEncoder (3).turn.text(), juce::String ("LR"));
                display.pushEncoder (3);
                expect (rig.engine.talk.paths[0].mains.contains (0), "LR");
                expectEquals (wing.runX32Text ("/config/talk/A/destmap", set), juce::String ("/config/talk/A/destmap %010000000000000000"));
                display.pushEncoder (3);
                expect (! rig.engine.talk.paths[0].mains.contains (0));
            }
            // OSCILLATOR: the first push switches it on, the sixth encoder its destination.
            {
                int set = 0;
                auto& wing = rig.engine.getWing();
                display.showScreen (S::Monitor, 3);
                display.pushEncoder (0);
                expectEquals (wing.runX32Text ("/-stat/osc/on", set), juce::String ("/-stat/osc/on ON"));
                display.getEncoder (5).turn.set (18.0f / 25.0f); // MAIN L+R
                expectEquals (wing.runX32Text ("/config/osc/dest", set), juce::String ("/config/osc/dest 18"));
                expectEquals (display.getEncoder (5).turn.text(), juce::String ("MAIN L+R"));
                display.pushEncoder (0);
                expectEquals (wing.runX32Text ("/-stat/osc/on", set), juce::String ("/-stat/osc/on OFF"));
            }
            if (folder.isNotEmpty())
            {
                const std::tuple<S, int, const char*> pages[] = { { S::Scenes, 0, "scenes" }, { S::Scenes, 1, "scenes-tab" }, { S::Scenes, 2, "snippets" }, { S::Scenes, 3, "param-safe" },
                                                                  { S::Scenes, 4, "chan-safe" }, { S::Scenes, 5, "scenes-midi" }, { S::Library, 2, "library-channel" }, { S::Library, 4, "library-routing" }, { S::Setup, 0, "setup-global" }, { S::Monitor, 0, "monitor" },
                                                                  { S::Monitor, 1, "talk-a" }, { S::Monitor, 3, "osc" }, { S::Assign, 0, "assign" },
                                                                  { S::Usb, 0, "usb" }, { S::Setup, 4, "scribble" }, { S::Setup, 5, "preamps" }, { S::Setup, 1, "setup-config" },
                                                                  { S::Setup, 3, "network" }, { S::Setup, 6, "card" },
                                                                  { S::Meters, 4, "rta" }, { S::Home, 1, "config" } };
                for (auto [screen, page, shot] : pages)
                {
                    display.showScreen (screen, page);
                    const auto lcd = display.createComponentSnapshot (display.getLocalBounds(), true, 1.0f);
                    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile (juce::String ("x32-page-") + shot + ".png");
                    out.deleteFile();
                    juce::FileOutputStream stream (out);
                    juce::PNGImageFormat().writeImageToStream (lcd, stream);
                }
                display.showScreen (S::Home, 0);
                display.setLayer (1);
                const auto lcd = display.createComponentSnapshot (display.getLocalBounds(), true, 1.0f);
                const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile ("x32-page-home2.png");
                out.deleteFile();
                juce::FileOutputStream stream (out);
                juce::PNGImageFormat().writeImageToStream (lcd, stream);
                display.setLayer (0);
            }
        }

        // SETUP > config: what a link shares, as /config/linkcfg.
        display.showScreen (ui::x32::Display::Screen::Setup, 1);
        expectEquals (display.getEncoder (3).turn.label, juce::String ("LINK"));
        display.getEncoder (3).turn.set (1.0f / 3.0f); // LINK EQ
        expectEquals (display.getEncoder (3).turn.text(), juce::String ("LINK EQ"));
        expect (display.getEncoder (3).pushOn(), "on by default");
        display.pushEncoder (3);
        expect (! display.getEncoder (3).pushOn());
        display.pushEncoder (3);
        expect (display.getEncoder (3).pushOn());
        // The bus scheme: every input's sends pre fader, then post again.
        {
            int set = 0;
            display.getEncoder (4).turn.set (1.0f / 3.0f); // ALL PRE
            display.pushEncoder (4);
            expect (engine.getWing().runX32Text ("/ch/05/mix/03/type", set).endsWith ("PRE"));
            display.getEncoder (4).turn.set (0.0f); // ALL POST
            display.pushEncoder (4);
            expect (engine.getWing().runX32Text ("/ch/05/mix/03/type", set).endsWith ("POST"));
        }
        // SCRIBBLE STRIP: a name from the Snippets list, given by its push.
        display.showScreen (ui::x32::Display::Screen::Setup, 4);
        if (display.getEncoder (3).push)
        {
            auto* strip = rig.selected;
            const auto was = strip != nullptr ? strip->getName() : juce::String();
            display.getEncoder (3).turn.set (0.0f);
            display.pushEncoder (3);
            if (strip != nullptr)
            {
                expectEquals (strip->getName(), ui::x32::Display::scribbleNames()[0]);
                strip->setName (was);
            }
        }
        // PREAMPS: layer 2 holds the block's bottom four.
        display.showScreen (ui::x32::Display::Screen::Setup, 5);
        expectEquals (display.getEncoder (1).turn.label, juce::String ("GAIN 1"));
        display.setLayer (1);
        expectEquals (display.getEncoder (1).turn.label, juce::String ("GAIN 5"));
        display.getEncoder (0).turn.set (1.0f);
        expectEquals (display.getEncoder (0).turn.text(), juce::String ("25-32"));
        expectEquals (display.getEncoder (1).turn.label, juce::String ("GAIN 29"));
        display.setLayer (0);
        // The X32 COMPACT: 8 input faders in eight layers, chosen on SETUP.
        display.showScreen (ui::x32::Display::Screen::Setup);
        expectEquals (display.getEncoder (0).turn.label, juce::String ("MODEL"));
        skin.setInputLayer (1); // CH 17-32
        display.pushEncoder (0);
        expect (skin.getModel() == ui::X32Surface::Model::Compact);
        expectEquals (skin.getNumInputFaders(), 8);
        expectEquals (skin.getInputLayerNames().size(), 8);
        expectEquals (skin.getInputLayer(), 2, "CH 17-32 stays up as CH 17-24");
        expect (skin.getInputStrip (0).getStrip() == channels[16].get());
        skin.setInputLayer (5); // FX RTN
        expectEquals (skin.getInputStrip (0).getStrip()->getLabel(), juce::String ("FX 1"));
        skin.setInputLayer (0);
        if (folder.isNotEmpty())
        {
            const auto compact = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (folder).getChildFile ("x32-compact.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (compact, out), file.getFullPathName());
        }
        display.pushEncoder (0);
        expect (skin.getModel() == ui::X32Surface::Model::Full);

        // X32 scene files: EXPORT writes the console, IMPORT keeps one as a new scene.
        {
            ui::x32::SceneFiles files (rig.ctx);
            const auto file = juce::File::createTempFile (".scn");
            expect (files.exportFile (file).startsWith ("Exported"));
            auto text = file.loadFileAsString();
            expect (text.startsWith ("#2.7# "), text.substring (0, 40));
            expect (text.contains ("\n/ch/13/config \"Vox 1\" "), "the channel's name");
            text = text.replace ("/ch/13/config \"Vox 1\"", "/ch/13/config \"Lead\"").replace ("#2.7# \"", "#2.7# \"Encore");
            text << "/-prefs/remote/enable OFF\n";
            file.replaceWithText (text);
            const int scenes = rig.show.getNumScenes();
            const auto status = files.importFile (file);
            expect (status.startsWith ("Imported"), status);
            expect (status.contains ("1 line(s)"), "preferences aren't part of a scene: " + status);
            expectEquals (rig.show.getNumScenes(), scenes + 1);
            expect (rig.show.getScene (scenes)->name.startsWith ("Encore"));
            expectEquals (channels[12]->getName(), juce::String ("Lead"));
            file.deleteFile();
        }

        // ASSIGN's editor writes each kind of code; another kind shows as itself.
        {
            display.showScreen (ui::x32::Display::Screen::Assign, 0);
            const auto turn = [&display] (int encoder, int step, int steps) { display.getEncoder (encoder).turn.set ((float) step / (float) (steps - 1)); };
            turn (0, 0, 12); // ENC 1
            turn (1, 4, 6);  // FX PARAM
            turn (2, 2, 8);  // FX3
            turn (3, 5, 64); // P6
            expectEquals (display.assignCode (0), juce::String ("X205"));
            turn (1, 5, 6); // SELECTED
            turn (2, 7, 13);
            expectEquals (display.assignCode (0), juce::String ("DG"));
            expectEquals (display.getEncoder (2).turn.text(), juce::String ("SEL RATIO"));
            turn (0, 4, 12); // KEY 5
            turn (1, 5, 8);  // SCENE
            turn (2, 3, 100);
            expectEquals (display.assignCode (4), juce::String ("S003"));
            turn (1, 7, 8); // NEXT
            expectEquals (display.assignCode (4), juce::String ("S91"));
            display.setAssignCode (4, "M100");
            display.showScreen (ui::x32::Display::Screen::Assign, 0);
            expect (display.getEncoder (1).turn.text().startsWith ("MIDI"), display.getEncoder (1).turn.text());
            display.setAssignCode (0, "-");
            display.setAssignCode (4, "-");
        }

        // UTILITY's init tab: two pushes put the selected strip back to the factory.
        {
            auto* selectedBefore = rig.selected;
            auto& ch = *channels[14];
            rig.selected = &ch;
            ch.setName ("Pad");
            ch.params.faderDb = -12.0f;
            display.showScreen (ui::x32::Display::Screen::Utility, 1);
            display.pushEncoder (0);
            expectEquals (ch.getName(), juce::String ("Pad"), "the first push only arms it");
            display.pushEncoder (0);
            expectEquals (ch.getName(), juce::String());
            expect (ch.params.faderDb.load() < -100.0f, "a factory fader is down");
            rig.selected = selectedBefore;
        }

        // MONITOR's talk tabs: the sixth encoder picks the talkback channel.
        {
            display.showScreen (ui::x32::Display::Screen::Monitor, 1);
            const auto& talk = display.getEncoder (5).turn;
            expectEquals (talk.label, juce::String ("TALK CH"));
            talk.set (0.0f);
            expect (engine.getTalkStrip() == nullptr);
            expectEquals (talk.text(), juce::String ("OFF"));
            const float count = (float) (channels.size() + engine.getAux().size());
            talk.set (3.0f / count); // CH 3
            expect (engine.getTalkStrip() == channels[2].get());
            expectEquals (talk.text(), channels[2]->getLabel());
            talk.set (0.0f);
        }

        // X32 channel presets: channel 13 exported, loaded onto channel 20; the main too.
        {
            ui::x32::SceneFiles files (rig.ctx);
            const auto file = juce::File::createTempFile (".chn");
            expect (files.exportChannelFile (file, "/ch/13", "Lead").startsWith ("Exported"));
            const auto text = file.loadFileAsString();
            expect (text.startsWith ("#2.1# \"Lead\" "), text.substring (0, 40));
            expect (text.contains ("\n/config \"Lead\" ") && ! text.contains ("/ch/13"), "paths relative to the strip");
            const auto status = files.importChannelFile (file, "/ch/20");
            expect (status.startsWith ("Loaded"), status);
            expectEquals (channels[19]->getName(), juce::String ("Lead"));
            expect (files.importChannelFile (file, {}) == "Select a strip");
            expect (files.exportChannelFile (file, "/main/st", "Main").startsWith ("Exported"));
            expect (file.loadFileAsString().contains ("\n/mix "));
            file.deleteFile();
        }
        setShownProfile (ld::kProfileWing);
    }

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
            channels[(size_t) i]->params.gate().on = true;
            channels[(size_t) i]->meters.gateReductionDb = 4.0f + (float) i;
        }
        for (int i : { 0, 2, 10, 18, 19, 20 })
        {
            channels[(size_t) i]->params.dynamics().on = true;
            channels[(size_t) i]->meters.dynReductionDb = 3.0f + (float) (i % 5);
        }
        for (int i : { 0, 1, 2, 10, 12, 13, 18 })
        {
            auto& eq = channels[(size_t) i]->params.eq();
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
            channels[(size_t) strip]->params.preInsert().fx = slot;
            channels[(size_t) strip]->params.preInsert().on = true;
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
