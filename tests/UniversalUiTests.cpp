// The Universal Mixer's interface (src/ui/universal; docs/UNIVERSAL_MIXER.md, Phase 2).

#include "TestHelpers.h"
#include "engine/EventLog.h"
#include "engine/PluginHost.h"
#include "engine/Show.h"
#include "ui/Knobs.h"
#include "ui/UniversalSkin.h"

using namespace livedaw;
using namespace livedaw::ui::universal;

namespace
{
/** A UI context over an engine (with a settings file of its own) that keeps the selection. */
struct UiRig
{
    explicit UiRig (MixerEngine& e) : engine (e), show (e, host), meters (e), ctx (e, host, show, log, devices, settings, meters)
    {
        ctx.selectStrip = [this] (Strip* s) { selected = s; };
        ctx.getSelectedStrip = [this] { return selected; };
    }
    ~UiRig() { settingsFile.deleteFile(); }

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

/** A mouse event on `c`, from `down` to `at` (a drag when they differ). */
juce::MouseEvent mouseEvent (juce::Component& c, juce::Point<float> at, juce::Point<float> down, juce::ModifierKeys mods = {})
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, down, now,
                             1, at != down);
}
} // namespace

class UniversalUiTests final : public juce::UnitTest
{
public:
    UniversalUiTests() : juce::UnitTest ("Universal Mixer UI", "LiveDAW") {}

    void runTest() override
    {
        beginTest ("The dark theme reads: text, keys and strip labels stand out from what they sit on");
        {
            const auto& t = Theme::dark();
            expectWithinAbsoluteError (contrastRatio (juce::Colours::black, juce::Colours::white), 21.0f, 0.01f);
            expectGreaterOrEqual (contrastRatio (t.text, t.background), 7.0f);
            expectGreaterOrEqual (contrastRatio (t.text, t.raised), 7.0f);
            expectGreaterOrEqual (contrastRatio (t.textDim, t.panel), 4.5f);
            for (auto role : { t.accent, t.mute, t.solo })
                expectGreaterOrEqual (contrastRatio (t.textOnAccent, role), 4.5f, "text on a lit key");
            for (int k = 0; k < kNumStripKinds; ++k)
                expectGreaterOrEqual (contrastRatio (t.kindColour (static_cast<StripKind> (k)), t.raised), 4.5f, "a strip kind's label");
        }

        beginTest ("The fader follows the WING law, writes its value as it is moved and follows changes made elsewhere");
        {
            std::atomic<float> db { -100.0f };
            FaderControl f (Theme::dark());
            f.setSize (60, 400);
            expect (! f.isEnabled(), "unbound: disabled");
            f.bind (&db);
            expect (f.isEnabled());
            expectEquals (f.getPosition(), 0.0);
            f.setPositionFromUser (0.75);
            expectWithinAbsoluteError (db.load(), 0.0f, 1.0e-4f);
            f.setPositionFromUser (1.5);
            expectWithinAbsoluteError (db.load(), 10.0f, 1.0e-4f);
            db = -10.0f;
            f.refresh();
            expectWithinAbsoluteError (f.getPosition(), (double) ui::fader::dbToPos (-10.0f), 1.0e-6);

            // A drag moves it by how far it travels (the track is 372 px): a tenth up, -10 dB to -6 dB.
            f.mouseDown (mouseEvent (f, { 30.0f, 300.0f }, { 30.0f, 300.0f }));
            f.mouseDrag (mouseEvent (f, { 30.0f, 300.0f - 37.2f }, { 30.0f, 300.0f }));
            expectWithinAbsoluteError (db.load(), -6.0f, 0.05f);
            // With Shift a quarter as far.
            f.mouseDown (mouseEvent (f, { 30.0f, 300.0f }, { 30.0f, 300.0f }));
            f.mouseDrag (mouseEvent (f, { 30.0f, 300.0f + 37.2f }, { 30.0f, 300.0f }, juce::ModifierKeys::shiftModifier));
            expectWithinAbsoluteError (f.getPosition(), 0.575, 1.0e-3);
            f.mouseDoubleClick (mouseEvent (f, { 30.0f, 100.0f }, { 30.0f, 100.0f }));
            expectWithinAbsoluteError (db.load(), 0.0f, 1.0e-4f);
            juce::MouseWheelDetails wheel {};
            wheel.deltaY = 0.5f;
            f.mouseWheelMove (mouseEvent (f, { 30.0f, 100.0f }, { 30.0f, 100.0f }), wheel);
            expectWithinAbsoluteError (f.getPosition(), 0.76, 1.0e-6);
            f.bind (nullptr);
            f.setPositionFromUser (0.2);
            expectWithinAbsoluteError (db.load(), ui::fader::posToDb (0.76f), 1.0e-4f, "unbound: nothing written");
        }

        beginTest ("The MIXER view has a column for every strip of any configuration, by kind, and follows a new one");
        {
            MixerEngine engine (0);
            const Counts counts { 48, 4, 24, 2, 6, 32, 12, 16 };
            engine.configure (&counts);
            UiRig rig (engine);
            MixerView view (rig.ctx, Theme::dark());
            view.setSize (1400, 800);
            expectEquals (view.getNumColumns(), 48 + 4 + 24 + 2 + 6);
            expect (&view.getColumn (0)->getStrip() == engine.getChannels()[0].get());
            expect (&view.getColumn (47)->getStrip() == engine.getChannels()[47].get());
            expect (view.getColumn (48)->getStrip().getKind() == StripKind::Aux);
            expect (&view.getColumn (83)->getStrip() == engine.getMatrices()[5].get());
            // Side by side, a gap between kinds.
            const auto* ch48 = view.getColumn (47);
            const auto* aux1 = view.getColumn (48);
            expectEquals (aux1->getX() - ch48->getRight(), sizes::kKindGap);
            expectEquals (ch48->getWidth(), sizes::kColumnWidth);

            view.setFilter ((int) StripKind::Bus);
            expectEquals (view.getNumColumns(), 24);
            expect (view.getColumn (23)->getStrip().getKind() == StripKind::Bus);
            view.setFilter (-1);
            expectEquals (view.getNumColumns(), 84);

            // Another configuration (the WING): the view notices by itself.
            engine.configure (nullptr);
            view.refresh();
            expectEquals (view.getNumColumns(), 40 + 8 + 16 + 4 + 8);
        }

        beginTest ("A strip column's keys, pan and fader act on its strip; its meter is the strip's output; a tap on its name selects it");
        {
            MixerEngine engine (0);
            test::setChannels (engine, 2);
            UiRig rig (engine);
            MixerView view (rig.ctx, Theme::dark());
            view.setSize (1400, 800);
            auto& column = *view.getColumn (1);
            auto& strip = column.getStrip();
            expect (&strip == engine.getChannels()[1].get());

            expect (! strip.params.mute.load());
            column.muteKey.onClick();
            expect (strip.params.mute.load() && column.muteKey.isOn());
            column.muteKey.onClick();
            expect (! strip.params.mute.load());
            column.soloKey.onClick();
            expect (strip.params.solo.load() && column.soloKey.isOn());
            column.soloKey.onClick();
            expect (! strip.params.solo.load());
            column.pan.setPanFromUser (-40.0f);
            expectEquals (strip.params.pan.load(), -40.0f);
            column.fader.setPositionFromUser (0.75);
            expectWithinAbsoluteError (strip.params.faderDb.load(), 0.0f, 1.0e-4f);

            // Changes made elsewhere (a scene, a remote) show.
            strip.params.faderDb = -20.0f;
            strip.params.pan = 30.0f;
            view.refresh();
            expectWithinAbsoluteError (column.fader.getPosition(), (double) ui::fader::dbToPos (-20.0f), 1.0e-6);
            expectEquals (column.pan.getPan(), 30.0f);

            // The meter: the strip's output as the meter bank moves it.
            strip.meters.outputPeak[0] = 0.5f;
            rig.meters.advance (0.01f);
            view.refresh();
            expectWithinAbsoluteError (column.meter.getPosition (0), ui::fader::dbToPos (dsp::gainToDb (0.5f)), 0.01f);

            // A tap on its name selects it; the view shows the selection.
            column.mouseUp (mouseEvent (column, { 40.0f, 30.0f }, { 40.0f, 30.0f }));
            expect (rig.selected == &strip);
            view.selectionChanged();
            expect (column.isSelected() && ! view.getColumn (0)->isSelected());
            // A tap lower down (its fader's area) doesn't.
            rig.selected = nullptr;
            column.mouseUp (mouseEvent (column, { 40.0f, 400.0f }, { 40.0f, 400.0f }));
            expect (rig.selected == nullptr);
        }

        beginTest ("The Universal skin draws its top bar over the MIXER view");
        {
            MixerEngine engine (0);
            const Counts counts { 24, 2, 8, 2, 4, 16, 8, 16 };
            engine.configure (&counts);
            UiRig rig (engine);
            ui::UniversalSkin skin (rig.ctx);
            skin.setSize (1600, 900);
            expectEquals (skin.getMixer().getNumColumns(), 24 + 2 + 8 + 2 + 4);
            // Some strips in use: names, a colour, a mute, a solo, a level.
            const auto& channels = engine.getChannels();
            for (int i = 0; i < 3; ++i)
                channels[(size_t) i]->params.custLink = false; // their own names and colours, not their sources'
            channels[0]->setName ("Kick");
            channels[0]->setColourIndex (5);
            channels[0]->params.faderDb = 0.0f;
            channels[0]->meters.outputPeak[0] = 0.5f;
            channels[1]->setName ("Snare");
            channels[1]->params.mute = true;
            channels[1]->params.faderDb = -5.0f;
            channels[2]->setName ("Lead vocal");
            channels[2]->params.solo = true;
            channels[2]->params.pan = -30.0f;
            channels[2]->params.faderDb = 3.0f;
            rig.meters.advance (0.01f);
            skin.getMixer().refresh();
            const auto image = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
            const auto& t = Theme::dark();
            const auto near = [] (juce::Colour a, juce::Colour b)
            {
                return std::abs (a.getRed() - b.getRed()) <= 2 && std::abs (a.getGreen() - b.getGreen()) <= 2 && std::abs (a.getBlue() - b.getBlue()) <= 2;
            };
            expect (near (image.getPixelAt (sizes::kGapLarge / 2, 4), t.panel), "the top bar");
            expect (near (image.getPixelAt (1500, sizes::kTopBar + sizes::kFilterBar / 2), t.background), "the filter row, right of its keys");
            // LIVEDAW_UI_SNAPSHOTS=<folder>: keeps the picture (to look at, or for the docs).
            const auto folder = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_UI_SNAPSHOTS", {});
            if (folder.isNotEmpty())
            {
                const auto file = juce::File (folder).getChildFile ("universal-mixer.png");
                file.deleteFile();
                juce::FileOutputStream out (file);
                expect (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out));
            }
        }
    }
};

static UniversalUiTests universalUiTests;
