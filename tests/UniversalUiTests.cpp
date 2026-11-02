// The Universal Mixer's interface (src/ui/universal; docs/UNIVERSAL_MIXER.md, Phase 2).

#include "TestHelpers.h"
#include "engine/EventLog.h"
#include "engine/PluginHost.h"
#include "engine/Show.h"
#include "ui/Knobs.h"
#include "ui/UniversalSkin.h"

#include <optional>

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

/** LIVEDAW_UI_SNAPSHOTS=<folder> keeps a test's picture there (to look at, or for the docs). */
bool keepPicture (const juce::Image& image, const juce::String& name)
{
    const auto folder = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_UI_SNAPSHOTS", {});
    if (folder.isEmpty())
        return true;
    const auto file = juce::File (folder).getChildFile (name);
    file.deleteFile();
    juce::FileOutputStream out (file);
    return out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out);
}

/** The step of `strip`'s chain that `match` picks (an empty step if none). */
template <typename Match>
ChainStep stepOf (const Strip& strip, Match match)
{
    for (const auto& s : strip.getChain())
        if (match (s))
            return s;
    return {};
}

/** Where a step is in `strip`'s chain (-1: not there). */
template <typename Match>
int indexOf (const Strip& strip, Match match)
{
    const auto chain = strip.getChain();
    for (size_t i = 0; i < chain.size(); ++i)
        if (match (chain[i]))
            return (int) i;
    return -1;
}

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
            expect (keepPicture (image, "universal-mixer.png"));
        }

        beginTest ("Value bars and choice lists write their values");
        {
            std::atomic<float> trim { 0.0f };
            ValueBar bar (Theme::dark());
            bar.setSize (300, 40);
            expect (! bar.isEnabled(), "unbound: disabled");
            bar.bind (ValueBar::linear ("TRIM", trim, -18.0f, 18.0f, 0.0f, " dB", 1));
            bar.setPositionFromUser (0.75f);
            expectWithinAbsoluteError (trim.load(), 9.0f, 1.0e-4f);
            expectEquals (bar.getText(), juce::String ("9.0 dB"));
            // A drag a third of its width to the left: a third of its range down.
            bar.mouseDown (mouseEvent (bar, { 150.0f, 20.0f }, { 150.0f, 20.0f }));
            bar.mouseDrag (mouseEvent (bar, { 50.0f, 20.0f }, { 150.0f, 20.0f }));
            expectWithinAbsoluteError (trim.load(), -3.0f, 0.01f);
            bar.mouseDoubleClick (mouseEvent (bar, { 50.0f, 20.0f }, { 50.0f, 20.0f }));
            expectEquals (trim.load(), 0.0f, "a double tap: its default");
            std::atomic<float> hz { 100.0f };
            bar.bind (ValueBar::frequency ("FREQ", hz, 20.0f, 2000.0f, 100.0f));
            bar.setPositionFromUser (0.5f);
            expectWithinAbsoluteError (hz.load(), 200.0f, 0.5f, "a log scale: the middle is the geometric mean");

            ChoiceBar choice (Theme::dark());
            int chosen = 1;
            choice.items = [] { return juce::StringArray { "A", "B", "C" }; };
            choice.current = [&chosen] { return chosen; };
            choice.choose = [&chosen] (int i) { chosen = i; };
            choice.refresh();
            expectEquals (choice.getText(), juce::String ("B"));
            choice.chooseItem (2);
            expectEquals (chosen, 2);
            expectEquals (choice.getText(), juce::String ("C"));
            choice.chooseItem (7);
            expectEquals (chosen, 2, "not an item: nothing");
        }

        beginTest ("The chain list shows a strip's processing in order, and adds, moves and removes blocks and taps");
        {
            MixerEngine engine (0);
            test::setChannels (engine, 1);
            UiRig rig (engine);
            auto& ch = *engine.getChannels()[0];
            ChainList list (rig.ctx, Theme::dark());
            list.setSize (420, 600);
            list.setStrip (&ch);
            const auto names = [&list]
            {
                juce::StringArray n;
                for (const auto& r : list.getRows())
                    n.add (r.name);
                return n;
            };
            // The WING template: INPUT, FILTER, the process order's four blocks,
            // the fader between its markers, INSERT 2, DELAY.
            auto n = names();
            expectEquals (n.size(), 11, n.joinIntoString (", "));
            expectEquals (n[0], juce::String ("INPUT"));
            expectEquals (n[1], juce::String ("FILTER"));
            for (int i = 2; i < 6; ++i)
                expectEquals (list.getRows()[(size_t) i].step.slot, i - 2, "a process order slot");
            expectEquals (n.indexOf ("PRE-FADER"), 6);
            expectEquals (n.indexOf ("FADER"), 7);
            expectEquals (n.indexOf ("POST"), 10);
            expect (std::none_of (list.getRows().begin(), list.getRows().end(), [] (const ChainList::Row& r) { return r.movable; }));

            // ADD an EQ block with FILTER selected: right after FILTER, and selected.
            list.selectRow (1);
            expect (list.add (BlockKind::Eq));
            expectEquals (names()[2], juce::String ("EQ BLOCK"));
            expectEquals (list.getSelectedRow(), 2);
            const int uid = list.getRows()[2].step.uid;
            const auto isBlock = [uid] (const ChainStep& s) { return s.isBlock() && s.uid == uid; };
            expectEquals (indexOf (ch, isBlock), indexOf (ch, [] (const ChainStep& s) { return s.kind == ld::kStepFilter; }) + 1);

            // A user tap with nothing selected: before PRE-FADER.
            list.selectRow (-1);
            expect (list.add (std::nullopt));
            n = names();
            expectEquals (n[n.indexOf ("TAP 1") + 1], juce::String ("PRE-FADER"));
            const int tap = list.getRows()[(size_t) n.indexOf ("TAP 1")].step.arg;
            expect (tap >= ld::kFirstUserTap);

            // Moved as a drag moves it: after FADER, in the strip's chain too.
            expect (list.moveRow (2, names().indexOf ("FADER") + 1));
            n = names();
            expectEquals (n[n.indexOf ("FADER") + 1], juce::String ("EQ BLOCK"));
            expect (indexOf (ch, isBlock) > indexOf (ch, [] (const ChainStep& s) { return s.kind == ld::kStepFader; }));
            expectEquals (list.getSelectedRow(), n.indexOf ("EQ BLOCK"), "the moved row stays selected");

            // A change made elsewhere shows.
            expect (ch.removeTap (tap));
            list.refresh();
            expectEquals (names().indexOf ("TAP 1"), -1);

            // REMOVE takes an extra block away; the WING's own blocks stay.
            expect (list.removeRow (list.getSelectedRow()));
            expect (! ch.getBlock (uid).isValid());
            expect (! list.removeRow (0), "INPUT");
            expectEquals (names().size(), 11);
        }

        beginTest ("The block editor edits a strip's own blocks and extra blocks through the model tables");
        {
            MixerEngine engine (0);
            test::setChannels (engine, 1);
            UiRig rig (engine);
            auto& ch = *engine.getChannels()[0];
            BlockEditor editor (rig.ctx, Theme::dark());
            editor.setSize (420, 700);
            const auto paramsOf = [] (models::Table table, int model, int numValues)
            {
                const auto& ps = models::model (table, model).params;
                return (int) std::count_if (ps.begin(), ps.end(), [numValues] (const models::Param& p) { return p.index < numValues; });
            };

            // The strip's own EQ: ON, MODEL, its curve, MIX, its model's parameters.
            auto target = BlockTarget::of (rig.ctx, ch, stepOf (ch, [] (const ChainStep& s) { return s.kind == ld::kStepEq; }));
            expect (target.kind == BlockTarget::Kind::Eq && target.eq == &ch.params.eq);
            editor.setTarget (target);
            auto* on = dynamic_cast<ToggleKey*> (editor.getControl (0));
            auto* model = dynamic_cast<ChoiceBar*> (editor.getControl (1));
            auto* panel = dynamic_cast<ParamPanel*> (editor.getControl (4));
            expect (on != nullptr && model != nullptr && panel != nullptr && editor.getResponse() != nullptr);
            if (on == nullptr || model == nullptr || panel == nullptr)
                return;
            const bool wasOn = ch.params.eq.on.load();
            on->onClick();
            expect (ch.params.eq.on.load() != wasOn);
            const auto eqTable = (models::Table) ch.getEqTable();
            expectEquals (panel->getNumControls(), paramsOf (eqTable, ch.params.eq.model.load(), ld::kMaxEqParams));
            // Another model: its parameters.
            model->chooseItem (1);
            expectEquals (ch.params.eq.model.load(), 1);
            editor.refresh();
            expectEquals (panel->getNumControls(), paramsOf (eqTable, 1, ld::kMaxEqParams));
            // A parameter's bar writes the block's value.
            for (int i = 0; i < panel->getNumControls(); ++i)
                if (auto* bar = dynamic_cast<ValueBar*> (panel->getControl (i)))
                {
                    bar->setPositionFromUser (1.0f);
                    const auto& p = models::model (eqTable, 1).params[(size_t) i];
                    expectWithinAbsoluteError (ch.params.eq.values[p.index].load(), p.fromNormalised (1.0f), 1.0e-4f, p.label);
                    break;
                }

            // An extra EQ block with a low shelf boost: its own values and its curve.
            const int eqUid = ch.insertBlock (BlockKind::Eq, -1);
            auto* eq = ch.getBlock (eqUid).eq();
            expect (eq != nullptr);
            eq->on = true;
            eq->values[models::model (eqTable, eq->model.load()).find ("lg")] = 6.0f;
            target = BlockTarget::of (rig.ctx, ch, stepOf (ch, [eqUid] (const ChainStep& s) { return s.uid == eqUid; }));
            expect (target.eq == eq && target.uid == eqUid);
            editor.setTarget (target);
            const auto& curve = editor.getResponse()->getCurve();
            expect (! curve.empty() && curve.front() > 5.0 && std::abs (curve.back()) < 0.5);

            // An extra dynamics block: its model through the engine (with the model's defaults).
            const int dynUid = ch.insertBlock (BlockKind::Dynamics, -1);
            editor.setTarget (BlockTarget::of (rig.ctx, ch, stepOf (ch, [dynUid] (const ChainStep& s) { return s.uid == dynUid; })));
            model = dynamic_cast<ChoiceBar*> (editor.getControl (1));
            expect (model != nullptr);
            if (model != nullptr)
                model->chooseItem (3);
            expectEquals (ch.getBlock (dynUid).dynamics()->model.load(), 3);

            // A delay block: another unit keeps its time (3 m is 10 ms).
            const int delayUid = ch.insertBlock (BlockKind::Delay, -1);
            auto* delay = ch.getBlock (delayUid).delay();
            delay->delay = 3.0f;
            editor.setTarget (BlockTarget::of (rig.ctx, ch, stepOf (ch, [delayUid] (const ChainStep& s) { return s.uid == delayUid; })));
            auto* unit = dynamic_cast<ChoiceBar*> (editor.getControl (1));
            expect (unit != nullptr);
            if (unit != nullptr)
                unit->chooseItem (2);
            expectEquals (delay->mode.load(), 2);
            expectWithinAbsoluteError (delay->delay.load(), 10.0f, 1.0e-3f);

            // The fader has no editor here.
            expect (BlockTarget::of (rig.ctx, ch, stepOf (ch, [] (const ChainStep& s) { return s.kind == ld::kStepFader; })).kind
                    == BlockTarget::Kind::None);
            editor.setTarget ({});
            ch.clearChain();
            engine.getGarbage().collectAllNow();
        }

        beginTest ("The sends panel has a row per destination a strip can send to, and sets a send's ON, level and source");
        {
            MixerEngine engine (0);
            const Counts counts { 2, 1, 4, 2, 3, 8, 4, 16 };
            engine.configure (&counts);
            UiRig rig (engine);
            SendsPanel panel (rig.ctx, Theme::dark());
            panel.setSize (420, 600);
            auto& ch = *engine.getChannels()[0];
            panel.setStrip (&ch);
            expectEquals (panel.getNumRows(), 4 + 2 + 3);
            panel.setStrip (engine.getStrips (StripKind::Bus)[1].get());
            expectEquals (panel.getNumRows(), 3 + 2 + 3, "a bus: the other buses, the mains, the matrices");
            panel.setStrip (engine.getMains()[0].get());
            expectEquals (panel.getNumRows(), 3, "a main: the matrices");
            panel.setStrip (engine.getMatrices()[0].get());
            expectEquals (panel.getNumRows(), 0);

            // CH 1 to BUS 2: ON, its level, its source (POST, then a user tap).
            panel.setStrip (&ch);
            auto* row = panel.getRow (1);
            row->on.onClick();
            expect (ch.sends[1].enabled.load());
            row->level.setPositionFromUser (0.75f);
            expectWithinAbsoluteError (ch.sends[1].levelDb.load(), 0.0f, 1.0e-4f);
            row->source.chooseItem (1);
            expectEquals (ch.sends[1].mode.load(), (int32_t) SendMode::Post);
            expectEquals (ch.sends[1].tap.load(), -1);
            const int tap = ch.insertTap (-1);
            panel.refresh();
            expectEquals (row->source.items().size(), models::list (models::SendModes).size() + 1);
            row->source.chooseItem (models::list (models::SendModes).size());
            expectEquals (ch.sends[1].tap.load(), tap);
            panel.refresh();
            expectEquals (row->source.getText(), juce::String ("TAP 1"));
            row->source.chooseItem (0);
            expectEquals (ch.sends[1].tap.load(), -1, "a mode again: no tap");

            // MAIN 1: PRE; MTX 1: the tap.
            panel.getRow (4)->source.chooseItem (1);
            expect (ch.mains[0].pre.load());
            panel.getRow (6)->source.chooseItem (2);
            expectEquals (ch.matrix[0].tap.load(), tap);
        }

        beginTest ("The inspector opens on the selected strip: its chain with the selected block's editor, its sends");
        {
            MixerEngine engine (0);
            const Counts counts { 24, 2, 8, 2, 4, 16, 8, 16 };
            engine.configure (&counts);
            UiRig rig (engine);
            ui::UniversalSkin skin (rig.ctx);
            skin.setSize (1600, 900);
            expect (! skin.isInspectorOpen());
            auto& ch = *engine.getChannels()[0];
            ch.params.custLink = false;
            ch.setName ("Kick");
            // An EQ block with a low shelf boost after the strip's EQ.
            const int uid = ch.insertBlock (BlockKind::Eq, indexOf (ch, [] (const ChainStep& s) { return s.kind == ld::kStepEq; }) + 1);
            auto* eq = ch.getBlock (uid).eq();
            eq->on = true;
            eq->values[models::model ((models::Table) ch.getEqTable(), eq->model.load()).find ("lg")] = 6.0f;

            rig.selected = &ch; // as the window selects it (a tap on its name)
            skin.selectionChanged();
            auto& inspector = skin.getInspector();
            expect (skin.isInspectorOpen() && inspector.isVisible() && inspector.getStrip() == &ch);
            expectEquals (inspector.getWidth(), 460);
            expectEquals (skin.getMixer().getWidth(), 1600 - 460);

            // Its EQ block selected: its editor.
            const auto& rows = inspector.chain.getRows();
            for (int i = 0; i < (int) rows.size(); ++i)
                if (rows[(size_t) i].step.uid == uid)
                    inspector.chain.selectRow (i);
            expect (inspector.editor.getTarget().eq == eq);
            inspector.refresh();
            const auto image = skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f);
            expect (keepPicture (image, "universal-inspector.png"));

            inspector.showPage (Inspector::Page::Sends);
            expect (inspector.sends.isVisible() && ! inspector.chain.isVisible());
            expectEquals (inspector.sends.getNumRows(), 8 + 2 + 4);
            inspector.refresh();
            expect (keepPicture (skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f), "universal-sends.png"));

            // Another console: the selection again (none here), the inspector lets go.
            rig.selected = nullptr;
            engine.configure (nullptr);
            inspector.refresh();
            expect (inspector.getStrip() == nullptr);
            skin.refreshAll();
            skin.selectionChanged();
            expect (! skin.isInspectorOpen() && ! inspector.isVisible());
        }
    }
};

static UniversalUiTests universalUiTests;
