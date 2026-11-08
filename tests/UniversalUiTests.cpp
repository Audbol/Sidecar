// The Universal Mixer's interface (src/ui/universal; docs/UNIVERSAL_MIXER.md, Phase 2).

#include "TestHelpers.h"
#include "engine/EventLog.h"
#include "engine/PluginHost.h"
#include "engine/Show.h"
#include "engine/Recorder.h"
#include "ui/Knobs.h"
#include "ui/RoutingGroups.h"
#include "engine/Models.h"
#include "ui/MixerChooser.h"
#include "ui/UniversalSkin.h"
#include "ui/StripCustomize.h"
#include "ui/universal/StripPanel.h"
#include "ui/universal/ScenesView.h"
#include "ui/universal/FxRackView.h"
#include "engine/MidiRemote.h"
#include "engine/WebRemote.h"
#include "ui/universal/ValueControls.h"
#include "ui/universal/Widgets.h"

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
    // A folder that isn't there yet is made (rather than failing every picture).
    if (! juce::File (folder).createDirectory())
        return false;
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
            auto viewOwner = std::make_unique<MixerView> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& view = *viewOwner;
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
            auto viewOwner = std::make_unique<MixerView> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& view = *viewOwner;
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
            auto skinOwner = std::make_unique<ui::UniversalSkin> (rig.ctx); // on the heap: the test function's frame is big
            auto& skin = *skinOwner;
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

        beginTest ("The start-up window offers to continue, and says how finished each mixer is");
        {
            expectEquals (ui::mixerChooser::label (ui::InterfaceKind::Wing), juce::String ("WING (experimental)"));
            expectEquals (ui::mixerChooser::label (ui::InterfaceKind::X32), juce::String ("X32 (very experimental)"));
            expectEquals (ui::mixerChooser::label (ui::InterfaceKind::ProMix), juce::String ("PROMIX 01 (very experimental)"));
            expectEquals (ui::mixerChooser::label (ui::InterfaceKind::Yamaha02R), juce::String ("02R (very experimental)"));
            expectEquals (ui::mixerChooser::label (ui::InterfaceKind::Universal), juce::String ("UNIVERSAL"));
            // The SETUP keys are in ui::InterfaceKind's order: a key's index is its kind.
            const auto keys = ui::mixerChooser::keyLabels();
            for (int i = 0; i < (int) ui::mixerChooser::mixers().size(); ++i)
            {
                expect (ui::mixerChooser::mixers()[(size_t) i].kind == static_cast<ui::InterfaceKind> (i));
                expect (keys[i].startsWith (ui::mixerChooser::mixers()[(size_t) i].name));
            }

            int kept = 0;
            std::optional<ui::InterfaceKind> chosen;
            const auto open = [&] (const juce::String& continueText)
            {
                ui::mixerChooser::Options options;
                options.continueText = continueText;
                options.onContinue = [&] { ++kept; };
                options.onMixer = [&] (ui::InterfaceKind k) { chosen = k; };
                auto window = ui::mixerChooser::create (std::move (options));
                window->setSize (1600, 900);
                return window;
            };
            const auto keyNamed = [] (juce::Component& window, const juce::String& text) -> juce::Button*
            {
                for (auto* child : window.getChildren())
                    if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->getButtonText().startsWith (text))
                        return b;
                return nullptr;
            };

            auto first = open ({}); // nothing to continue
            expect (keyNamed (*first, "CONTINUE") == nullptr);
            expectEquals (first->getNumChildComponents(), 5);

            auto later = open ("02R (very experimental)");
            expectEquals (later->getNumChildComponents(), 6);
            keyNamed (*later, "CONTINUE")->onClick();
            expectEquals (kept, 1);
            later->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            expectEquals (kept, 2, "Esc keeps the console");
            expect (! chosen.has_value());
            keyNamed (*later, "PROMIX 01 (very experimental)")->onClick();
            expect (chosen == ui::InterfaceKind::ProMix);
        }

        beginTest ("A dynamics graph whose strip changes model before it refreshes keeps to the model it drew");
        {
            // An 02R console opened on the Universal Mixer crashed here: the handle of
            // a model's ratio was looked up in the strip's new model, which has fewer
            // parameters.
            const auto& table = models::table (models::Dynamics);
            int wide = -1, wideAt = -1, narrow = -1;
            for (int m = 0; m < (int) table.size(); ++m)
            {
                int at = table[(size_t) m].find ("ratio");
                if (at < 0)
                    at = table[(size_t) m].find ("range");
                if (at > wideAt)
                {
                    wideAt = at;
                    wide = m;
                }
            }
            for (int m = 0; m < (int) table.size() && narrow < 0; ++m)
                if ((int) table[(size_t) m].params.size() <= wideAt)
                    narrow = m;
            expect (wide >= 0 && narrow >= 0, "two models to switch between");
            if (wide >= 0 && narrow >= 0)
            {
                auto slot = std::make_unique<DynSlotParams>();
                slot->model = wide;
                TransferView view (Theme::dark());
                view.setSize (300, 200);
                view.bind (slot.get());
                slot->model = narrow; // changed, not yet refreshed
                expect (! view.handlePosition (1).isOrigin(), "the drawn model's handle");
                const auto image = view.createComponentSnapshot (view.getLocalBounds());
                expect (image.isValid());
                view.refresh();
                expect (view.handlePosition (1).isOrigin() == (table[(size_t) narrow].find ("ratio") < 0 && table[(size_t) narrow].find ("range") < 0));
            }
        }

        beginTest ("A data wheel turns a step per few pixels of a drag and per click of the mouse wheel");
        {
            DataWheel wheel;
            wheel.setBounds (0, 0, 156, 156);
            int turned = 0;
            double last = wheel.getValue();
            wheel.onValueChange = [&]
            {
                turned += (int) (wheel.getValue() - last);
                last = wheel.getValue();
            };
            const juce::Point<float> from (78.0f, 100.0f);
            const auto left = juce::ModifierKeys::leftButtonModifier;
            const auto dragBy = [&] (float pixels)
            {
                turned = 0;
                wheel.mouseDown (mouseEvent (wheel, from, from, left));
                wheel.mouseDrag (mouseEvent (wheel, from.translated (0.0f, -pixels), from, left));
                wheel.mouseUp (mouseEvent (wheel, from.translated (0.0f, -pixels), from));
                return turned;
            };
            expectEquals (dragBy ((float) DataWheel::kPixelsPerStep), 1, "one step's drag");
            expectEquals (dragBy (40.0f * DataWheel::kPixelsPerStep), 40, "up");
            expectEquals (dragBy (-40.0f * DataWheel::kPixelsPerStep), -40, "down");
            // Far past what used to wrap it round to the other end of its range.
            expectEquals (dragBy (500.0f * DataWheel::kPixelsPerStep), 500, "a long drag");
            {
                DataWheel slow (6); // the ProMix 01's
                slow.setBounds (0, 0, 156, 156);
                const double before = slow.getValue();
                slow.mouseDown (mouseEvent (slow, from, from, left));
                slow.mouseDrag (mouseEvent (slow, from.translated (0.0f, -60.0f), from, left));
                slow.mouseUp (mouseEvent (slow, from.translated (0.0f, -60.0f), from));
                expectEquals ((int) (slow.getValue() - before), 10, "6 pixels a step");
            }

            const auto scroll = [&] (float deltaY, bool reversed)
            {
                turned = 0;
                juce::MouseWheelDetails details {};
                details.deltaY = deltaY;
                details.isReversed = reversed;
                wheel.mouseWheelMove (mouseEvent (wheel, from, from), details);
                return turned;
            };
            expectEquals (scroll (0.25f, false), 1, "a click up");
            expectEquals (scroll (-0.25f, false), -1, "a click down");
            expectEquals (scroll (0.25f, true), -1, "reversed");
            expectEquals (scroll (0.0f, false), 0, "no movement");
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
            expect (target.kind == BlockTarget::Kind::Eq && target.eq == &ch.params.eq());
            editor.setTarget (target);
            auto* on = dynamic_cast<ToggleKey*> (editor.getControl (0));
            auto* model = dynamic_cast<ChoiceBar*> (editor.getControl (1));
            auto* panel = dynamic_cast<ParamPanel*> (editor.getControl (4));
            expect (on != nullptr && model != nullptr && panel != nullptr && editor.getResponse() != nullptr);
            if (on == nullptr || model == nullptr || panel == nullptr)
                return;
            const bool wasOn = ch.params.eq().on.load();
            on->onClick();
            expect (ch.params.eq().on.load() != wasOn);
            const auto eqTable = (models::Table) ch.getEqTable();
            expectEquals (panel->getNumControls(), paramsOf (eqTable, ch.params.eq().model.load(), ld::kMaxEqParams));
            // Another model: its parameters.
            model->chooseItem (1);
            expectEquals (ch.params.eq().model.load(), 1);
            editor.refresh();
            expectEquals (panel->getNumControls(), paramsOf (eqTable, 1, ld::kMaxEqParams));
            // A parameter's bar writes the block's value.
            for (int i = 0; i < panel->getNumControls(); ++i)
                if (auto* bar = dynamic_cast<ValueBar*> (panel->getControl (i)))
                {
                    bar->setPositionFromUser (1.0f);
                    const auto& p = models::model (eqTable, 1).params[(size_t) i];
                    expectWithinAbsoluteError (ch.params.eq().values[p.index].load(), p.fromNormalised (1.0f), 1.0e-4f, p.label);
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
            auto skinOwner = std::make_unique<ui::UniversalSkin> (rig.ctx); // on the heap: the test function's frame is big
            auto& skin = *skinOwner;
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

        beginTest ("The parts that come from the WING are tagged WING; the Universal core's own are not");
        {
            MixerEngine engine (0);
            test::setChannels (engine, 1);
            UiRig rig (engine);
            auto& ch = *engine.getChannels()[0];
            const int uid = ch.insertBlock (BlockKind::Eq, -1);
            const int tap = ch.insertTap (-1);
            ChainList list (rig.ctx, Theme::dark());
            list.setSize (420, 600);
            list.setStrip (&ch);
            for (const auto& row : list.getRows())
            {
                if (row.marker)
                    expect (row.device.isEmpty(), row.name);
                else if (row.step.isBlock() || row.step.isUserTap())
                    expect (row.device.isEmpty(), row.name + ": the core's");
                else
                    expectEquals (row.device, devices::wing, row.name + ": the WING template's");
            }
            const auto own = BlockTarget::of (rig.ctx, ch, stepOf (ch, [] (const ChainStep& s) { return s.kind == ld::kStepEq; }));
            const auto extra = BlockTarget::of (rig.ctx, ch, stepOf (ch, [uid] (const ChainStep& s) { return s.uid == uid; }));
            expectEquals (own.device, devices::wing);
            expect (extra.device.isEmpty());
            // An extra block's models are the WING's model tables all the same.
            BlockEditor editor (rig.ctx, Theme::dark());
            editor.setSize (420, 700);
            editor.setTarget (extra);
            auto* model = dynamic_cast<ChoiceBar*> (editor.getControl (1));
            expect (model != nullptr && model->device == devices::wing);
            SendsPanel sends (rig.ctx, Theme::dark());
            sends.setSize (420, 600);
            sends.setStrip (&ch);
            expect (sends.getNumRows() > 0 && sends.getRow (0)->source.device == devices::wing, "the WING's send modes");
            expect (ch.removeTap (tap));
            editor.setTarget ({});
            ch.clearChain();
            engine.getGarbage().collectAllNow();
        }

        beginTest ("The patch matrix shows its model's points and patches with a tap");
        {
            PatchGrid grid (Theme::dark());
            grid.setSize (600, 400);
            std::vector<int> patched { -1, 2, -1 }; // a column per row, -1 none
            PatchGrid::Model m;
            m.numRows = [] { return 3; };
            m.numColumns = [] { return 4; };
            m.rowName = [] (int r) { return "ROW " + juce::String (r + 1); };
            m.columnName = [] (int c) { return "COL " + juce::String (c + 1); };
            m.state = [&patched] (int r, int c)
            {
                return patched[(size_t) r] == c ? PatchGrid::On : (patched[(size_t) r] >= 0 && patched[(size_t) r] + 1 == c ? PatchGrid::Pair : PatchGrid::Off);
            };
            m.toggle = [&patched] (int r, int c) { patched[(size_t) r] = patched[(size_t) r] == c ? -1 : c; };
            grid.setModel (m);
            expectEquals (grid.getNumRows(), 3);
            expectEquals (grid.getNumColumns(), 4);
            expect (grid.getState (1, 2) == PatchGrid::On && grid.getState (1, 3) == PatchGrid::Pair && grid.getState (0, 0) == PatchGrid::Off);
            grid.tap (0, 1);
            expectEquals (patched[0], 1);
            expect (grid.getState (0, 1) == PatchGrid::On);
            grid.tap (0, 1);
            expectEquals (patched[0], -1, "a tap on a patch takes it away");
            grid.tap (7, 1); // outside: nothing
            expect (grid.getState (0, 1) == PatchGrid::Off);
        }

        beginTest ("ROUTING patches a channel's input and feeds an output port, for any configuration");
        {
            MixerEngine engine (0);
            const Counts counts { 4, 1, 6, 2, 3, 8, 4, 16 };
            engine.configure (&counts);
            UiRig rig (engine);
            auto viewOwner = std::make_unique<RoutingView> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& view = *viewOwner;
            view.setSize (1200, 700);

            // INPUTS: the channels and aux against LOCAL IN.
            const int ins = engine.getHardwareInputNames().size();
            expect (view.getGroup() == ui::routing::Group::LocalIn);
            expectEquals (view.grid.getNumRows(), 4 + 1);
            expectEquals (view.grid.getNumColumns(), ui::routing::tileCount (ui::routing::Group::LocalIn, ins));
            auto& ch1 = *engine.getChannels()[0];
            auto& ch2 = *engine.getChannels()[1];
            view.grid.tap (0, 2);
            expectEquals (ch1.params.inputLeft.load(), 2);
            expect (view.grid.getState (0, 2) == PatchGrid::On);
            ch2.params.stereo = true;
            view.grid.tap (1, 4);
            view.refresh();
            expect (view.grid.getState (1, 4) == PatchGrid::On && view.grid.getState (1, 5) == PatchGrid::Pair, "a stereo channel's pair");
            view.grid.tap (0, 2);
            expectEquals (ch1.params.inputLeft.load(), -1, "a tap on the patch takes it away");
            // Another group: the oscillators.
            view.setGroup (ui::routing::Group::Oscillator);
            expectEquals (view.grid.getNumColumns(), ld::kNumOscillators);
            view.grid.tap (0, 1);
            expectEquals (ch1.params.inputLeft.load(), MixerEngine::oscSource (1));

            // OUTPUTS: the ports against every bus, main and matrix side and the monitor paths.
            view.showPage (RoutingView::Page::Outputs);
            expect (view.getGroup() == ui::routing::Group::LocalOut);
            expectEquals (view.grid.getNumColumns(), (6 + 2 + 3) * 2 + 4);
            const int main1L = 2 * 6; // after the buses' sides
            auto& main1 = *engine.getMains()[0];
            // MAIN 1 L moves to LCL 6 (a side feeds one port).
            view.grid.tap (5, main1L);
            expectEquals (main1.params.outputLeft.load(), 5);
            expect (view.grid.getState (5, main1L) == PatchGrid::On);
            // Another signal on the port replaces it.
            auto& mtx3 = *engine.getMatrices()[2];
            view.grid.tap (5, 2 * (6 + 2 + 2) + 1);
            expectEquals (mtx3.params.outputRight.load(), 5);
            expectEquals (main1.params.outputLeft.load(), -1, "one signal per port");
            view.grid.tap (5, 2 * (6 + 2 + 2) + 1);
            expectEquals (mtx3.params.outputRight.load(), -1, "a tap on the patch takes it away");
            engine.monitorOutputLeft = -1;
            view.grid.tap (6, 2 * (6 + 2 + 3)); // SPEAKERS L
            expectEquals (engine.monitorOutputLeft.load(), 6);
        }

        beginTest ("FX RACK shows the slots in use and sets the selected slot's device, panel and parameters");
        {
            MixerEngine engine (0);
            UiRig rig (engine);
            auto viewOwner = std::make_unique<FxRackView> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& view = *viewOwner;
            view.setSize (1400, 800);
            auto& fx = engine.getFx();
            expectEquals (view.getNumSlots(), fx.getNumSlots());

            // A device with a front panel in FX 3.
            view.selectSlot (2);
            int withPanel = -1;
            for (const auto& m : models::table (models::Fx))
                if (m.index > ld::kFxExt && withPanel < 0 && FxSlots::isAllowed (m.index, 2))
                {
                    ui::Faceplate probe;
                    std::vector<std::atomic<float>> values (ld::kMaxFxParams);
                    probe.bind (models::Fx, m.index, values.data());
                    if (probe.hasDesign())
                        withPanel = m.index;
                    probe.unbind();
                }
            expect (withPanel > ld::kFxExt, "a device with a front panel");
            view.device.chooseItem (withPanel);
            expectEquals (fx.getModel (2), withPanel);
            view.refresh();
            expect (view.isShowingPanel() && view.faceplate.isVisible());
            expectEquals (view.device.device, devices::wing);
            expect (keepPicture (view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f), "universal-fx.png"));
            // The list of its parameters instead.
            view.panelKey.onClick();
            expect (! view.isShowingPanel());
            const auto& params = models::model (models::Fx, withPanel).params;
            expectEquals (view.params.getNumControls(),
                          (int) std::count_if (params.begin(), params.end(), [] (const models::Param& p) { return p.index < ld::kMaxFxParams; }));

            // A premium device needs FX 1-8.
            int premium = -1;
            for (const auto& m : models::table (models::Fx))
                if (m.category == models::Category::FxPremium && premium < 0)
                    premium = m.index;
            view.selectSlot (10);
            const int before = fx.getModel (10);
            view.device.chooseItem (premium);
            expectEquals (fx.getModel (10), before, "not in FX 11");
            // The slot's plugins are a menu away, whatever the slot holds.
            expect (view.pluginKey.isEnabled());
        }

        beginTest ("MONITORING sets the monitor paths and the solo settings");
        {
            MixerEngine engine (0);
            const Counts counts { 2, 0, 4, 1, 2, 32, 8, 16 };
            engine.configure (&counts);
            UiRig rig (engine);
            auto viewOwner = std::make_unique<MonitorView> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& view = *viewOwner;
            view.setSize (1400, 700);
            view.speakers.level.setPositionFromUser (0.75f);
            expectWithinAbsoluteError (engine.monitorLevelDb.load(), 0.0f, 1.0e-4f);
            const bool dimmed = engine.monitorDim.load();
            view.speakers.dim.onClick();
            expect (engine.monitorDim.load() != dimmed);
            // The console's own: MAIN.1, MTX.1-2, BUS.1-4 (no aux here).
            expectEquals (view.phones.source.items().size(), 1 + 2 + 4);
            view.phones.source.chooseItem (2);
            expectEquals (engine.phones.source.source.load(), MixerEngine::monitorSourceIndex (StripKind::Matrix, 1), "MTX.2");
            view.speakers.outputLeft.chooseItem (2);
            expectEquals (engine.monitorOutputLeft.load(), 1);
            view.speakers.outputLeft.chooseItem (0);
            expectEquals (engine.monitorOutputLeft.load(), -1, "OFF");
            view.soloMode.chooseItem (0);
            expectEquals (engine.soloMode.load(), (int32_t) ld::kSoloLive);
            const bool afl = engine.isSoloAfl (StripKind::Bus);
            view.aflBuses.onClick();
            expect (engine.isSoloAfl (StripKind::Bus) != afl);
            // CLEAR SOLO: every strip and every DCA in use (DCA 30 of 32 here).
            engine.getChannels()[1]->params.solo = true;
            engine.getDca (29).solo = true;
            view.clearSolo.onClick();
            expect (! engine.getChannels()[1]->params.solo.load() && ! engine.getDca (29).solo.load());
            expect (view.phones.source.device.isEmpty() && view.soloMode.device == devices::wing, "the console's sources, the WING's solo modes");
            view.refresh();
            expect (keepPicture (view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f), "universal-monitoring.png"));

            // 24 buses: BUS.17-24 are sources and direct inputs too (past the WING's 16).
            engine.setNumStrips (StripKind::Bus, 24);
            view.refresh();
            const auto names = view.speakers.source.items();
            expect (names.contains ("BUS.24"), names.joinIntoString (", "));
            view.speakers.source.chooseItem (names.indexOf ("BUS.24"));
            expectEquals (engine.speakers.source.load(), MixerEngine::monitorSourceIndex (StripKind::Bus, 23));
            expect (engine.speakers.source.load() >= 0x10000, "an extended choice");
            expectEquals (view.speakers.source.getText(), juce::String ("BUS.24"));
            const auto direct = view.phones.directIn.items();
            view.phones.directIn.chooseItem (direct.indexOf ("BUS.17"));
            expectEquals (engine.monitorPath (true).directSource.load(), MixerEngine::directSourceIndex (StripKind::Bus, 16));
        }

        beginTest ("MONITORING: each path's processing (DIRECT IN, EQ, PAN / WIDTH, INVERT, DELAY, LIMITER) and talkback");
        {
            MixerEngine engine (0);
            engine.configure (nullptr);
            UiRig rig (engine);
            auto viewOwner = std::make_unique<MonitorView> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& view = *viewOwner;
            view.setSize (1600, 1000);
            view.refresh();
            auto& sp = engine.monitorPath (false);
            auto& ph = engine.monitorPath (true);

            // The EQ: 8 handles (L, 1-6, H; the shelves without a Q) on the path's bands.
            auto& eq = view.speakers.eq;
            expectEquals ((int) eq.getHandles().size(), ld::kMonitorEqBands);
            expect (eq.getHandles()[0].q == nullptr && eq.getHandles()[3].q == &sp.eq[3].q);
            expect (eq.getHandles()[3].freq == &sp.eq[3].freq && eq.getHandles()[7].label == "H");
            const bool eqWasOn = sp.eqOn.load();
            view.speakers.eqOn.onClick();
            expect (sp.eqOn.load() != eqWasOn);
            if (! sp.eqOn.load())
                view.speakers.eqOn.onClick();
            sp.eq[3].gainDb = 9.0f;
            eq.refresh();
            const auto& curve = eq.getCurve();
            expect (! curve.empty() && *std::max_element (curve.begin(), curve.end()) > 6.0, "the engine's response: a 9 dB band");
            // A drag on a band's handle moves its frequency and gain.
            const float freqBefore = sp.eq[3].freq.load();
            const auto at = eq.handlePosition (3);
            eq.mouseDown (mouseEvent (eq, at, at));
            eq.mouseDrag (mouseEvent (eq, at + juce::Point<float> (40.0f, 20.0f), at));
            eq.mouseUp (mouseEvent (eq, at + juce::Point<float> (40.0f, 20.0f), at));
            expect (sp.eq[3].freq.load() > freqBefore && sp.eq[3].gainDb.load() < 9.0f);

            // DIRECT IN, PAN, WIDTH, INVERT, DELAY (by distance), LIMITER.
            view.phones.directIn.chooseItem (2);
            expectEquals (ph.directSource.load(), 1, "CH.2");
            expect (view.phones.directIn.device.isEmpty(), "the console's strips");
            view.phones.pan.setPositionFromUser (1.0f);
            expectWithinAbsoluteError (ph.pan.load(), 100.0f, 0.01f);
            view.phones.width.setPositionFromUser (0.5f);
            expectWithinAbsoluteError (ph.width.load(), 0.0f, 0.5f);
            const bool inverted = ph.invert.load();
            view.phones.invert.onClick();
            expect (ph.invert.load() != inverted);
            view.phones.delayOn.onClick();
            view.phones.distance.setPositionFromUser (1.0f / 3.0f);
            expectWithinAbsoluteError (ph.delayM.load(), 1.0f, 0.05f, "a third of the log scale: 1 m");
            expect (view.phones.distance.getText().contains (" ms"));
            view.phones.limiter.setPositionFromUser (0.5f);
            expectWithinAbsoluteError (ph.limiterDb.load(), -20.0f, 0.1f);
            ph.limiterGrDb = 6.0f;
            view.phones.refresh();
            expectWithinAbsoluteError (view.phones.getLimiterReduction(), -6.0f, 0.01f, "its gain reduction shows");

            // Talkback: the channel, TALK A by its key (a short press latches in AUTO), the destinations.
            auto& tb = view.talkback;
            tb.channel.chooseItem (40); // CH 40
            expect (engine.getTalkStrip() == engine.getChannels()[39].get());
            tb.channel.chooseItem (0);
            expect (engine.getTalkStrip() == nullptr, "OFF");
            tb.channel.chooseItem (41); // AUX 1
            expect (engine.getTalkStrip() == engine.getAux()[0].get());
            engine.talk.paths[0].mode = ld::kTalkAuto;
            tb.talkA.setState (juce::Button::buttonDown);
            tb.talkA.setState (juce::Button::buttonNormal);
            expect (engine.isTalkOn (0), "a tap latches TALK A");
            tb.talkA.setState (juce::Button::buttonDown);
            tb.talkA.setState (juce::Button::buttonNormal);
            expect (! engine.isTalkOn (0), "another tap releases it");
            // TALK B's settings by its tab: mode, dims, the destinations (B1-16, M1-4, X1-8).
            tb.showPath (1);
            tb.mode.chooseItem (1);
            expectEquals (engine.talk.paths[1].mode.load(), (int32_t) ld::kTalkPush);
            expectEquals (tb.destinations.count(), 16 + 4 + 8);
            tb.destinations.toggle (2);  // BUS 3
            tb.destinations.toggle (17); // MAIN 2
            tb.destinations.toggle (27); // MTX 8
            expect (engine.talk.paths[1].buses.load().low() == (1u << 2));
            expect (engine.talk.paths[1].mains.load().low() == (1u << 1));
            expect (engine.talk.paths[1].matrices.load().low() == (1u << 7));
            expect (! engine.talk.paths[0].buses.contains (2), "TALK A's untouched");
            tb.useSendLevels.onClick();
            expect (engine.talk.paths[1].individual.load());
            // A bigger console: its buses past the WING's 16 are destinations too (B17-B24).
            engine.setNumStrips (StripKind::Bus, 24);
            expectEquals (tb.destinations.count(), 24 + 4 + 8);
            expectEquals (tb.destinations.keyText (23), juce::String ("B24"));
            expectEquals (tb.destinations.keyText (24), juce::String ("M1"));
            tb.destinations.toggle (23);
            expect (engine.talk.paths[1].buses.contains (23), "BUS 24");
            view.refresh();
            expect (keepPicture (view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f), "universal-monitoring.png"));
        }

        beginTest ("The Universal skin's view keys show one view at a time");
        {
            MixerEngine engine (0);
            const Counts counts { 16, 2, 8, 2, 4, 16, 8, 16 };
            engine.configure (&counts);
            UiRig rig (engine);
            auto skinOwner = std::make_unique<ui::UniversalSkin> (rig.ctx); // on the heap: the test function's frame is big
            auto& skin = *skinOwner;
            skin.setSize (1600, 900);
            expect (skin.getView() == ui::UniversalSkin::View::Mixer && skin.getMixer().isVisible() && ! skin.getRouting().isVisible());
            skin.showView (ui::UniversalSkin::View::Routing);
            expect (skin.getRouting().isVisible() && ! skin.getMixer().isVisible());
            // A few channels patched, for the picture.
            for (int i = 0; i < 6; ++i)
                engine.getChannels()[(size_t) i]->params.inputLeft = i;
            skin.getRouting().refresh();
            expect (keepPicture (skin.createComponentSnapshot (skin.getLocalBounds(), true, 1.0f), "universal-routing.png"));
            skin.showView (ui::UniversalSkin::View::Fx);
            expect (skin.getFxRack().isVisible() && ! skin.getRouting().isVisible());
            skin.showView (ui::UniversalSkin::View::Monitoring);
            expect (skin.getMonitoring().isVisible() && ! skin.getFxRack().isVisible());
            skin.showView (ui::UniversalSkin::View::Mixer);
            expect (skin.getMixer().isVisible() && ! skin.getMonitoring().isVisible());
            for (auto v : { ui::UniversalSkin::View::Scenes, ui::UniversalSkin::View::Recorder, ui::UniversalSkin::View::Setup })
            {
                skin.showView (v);
                expect (skin.getView() == v && ! skin.getMixer().isVisible());
            }
            expect (skin.getScenes().isVisible() == false && skin.getSetup().isVisible());
            // The recorder pages are the WING display's.
            expectEquals (skin.getRecorder().usbTab.device, devices::wing);
            skin.getRecorder().showPage (RecorderView::Page::Usb);
            expect (skin.getRecorder().getPageComponent() != nullptr && skin.getRecorder().getPageComponent()->isVisible());
        }

        beginTest ("A strip's overview shows its input, gate, compression, EQ and output; FADERS hides or shows the faders");
        {
            MixerEngine engine (0);
            test::setChannels (engine, 2);
            UiRig rig (engine);
            auto viewOwner = std::make_unique<MixerView> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& view = *viewOwner;
            view.setChannelShown (false); // the columns alone (the channel panel has a test of its own)
            view.setSize (1400, 900);
            auto& column = *view.getColumn (0);
            auto& strip = column.getStrip();
            auto& p = strip.params;
            p.gate().on = true;
            p.dynamics().on = true;
            strip.meters.gateReductionDb = 6.0f;
            strip.meters.dynReductionDb = 3.0f;
            strip.meters.trimPeak[0] = 0.5f;
            strip.meters.outputPeak[0] = 0.25f;
            p.eq().on = true;
            p.eq().values[models::model ((models::Table) strip.getEqTable(), p.eq().model.load()).find ("lg")] = 6.0f;
            rig.meters.advance (0.01f);
            view.refresh();
            const auto& o = column.overview;
            expectWithinAbsoluteError (o.getGateReduction(), 6.0f, 0.01f);
            expectWithinAbsoluteError (o.getDynamicsReduction(), 3.0f, 0.01f);
            expectWithinAbsoluteError (o.getInput(), ui::fader::dbToPos (dsp::gainToDb (0.5f)), 0.01f);
            expectWithinAbsoluteError (o.getOutput(), ui::fader::dbToPos (dsp::gainToDb (0.25f)), 0.01f);
            expect (! o.getEqCurve().empty() && o.getEqCurve().front() > 5.0, "the EQ's low shelf");

            // With faders: the fader no taller than it needs, the overview above it.
            expect (view.areFadersShown() && column.fader.isVisible());
            expect (column.fader.getHeight() <= 340 && column.overview.getBottom() < column.fader.getY());
            const int withFaders = column.overview.getHeight();
            expect (keepPicture (view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f), "universal-overview-faders.png"));
            // FADERS off: no fader, the overview takes the column.
            view.setFadersShown (false);
            expect (! column.fader.isVisible() && ! column.meter.isVisible());
            expect (column.overview.getHeight() > withFaders + 200);
            expect (keepPicture (view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f), "universal-overview.png"));
            // A rebuild keeps the choice.
            view.setFilter ((int) StripKind::Channel);
            expect (! view.getColumn (0)->fader.isVisible());
            view.setFadersShown (true);
            expect (view.getColumn (0)->fader.isVisible());
        }

        beginTest ("SCENES runs and edits the show's scenes");
        {
            MixerEngine engine (0);
            test::setChannels (engine, 2);
            UiRig rig (engine);
            auto viewOwner = std::make_unique<ScenesView> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& view = *viewOwner;
            view.setSize (1200, 700);
            expectEquals (view.getNumRows(), 0);
            auto& fader = engine.getChannels()[0]->params.faderDb;
            fader = -10.0f;
            expectEquals (view.add(), 0);
            fader = -20.0f;
            expectEquals (view.add(), 1);
            expectEquals (view.getNumRows(), 2);
            expectEquals (view.getSelectedScene(), 1);
            view.rename ("Verse");
            expectEquals (rig.show.getScene (1)->name, juce::String ("Verse"));
            // GO: the selected scene's console.
            view.selectScene (0);
            view.go();
            expectEquals (fader.load(), -10.0f);
            expectEquals (rig.show.getCurrentSceneIndex(), 0);
            // STORE: the console into the selected scene.
            fader = -5.0f;
            view.store();
            fader = 0.0f;
            view.go();
            expectEquals (fader.load(), -5.0f);
            // DOWN, then REMOVE.
            view.move (1);
            expectEquals (view.getSelectedScene(), 1);
            expectEquals (rig.show.getScene (0)->name, juce::String ("Verse"));
            view.remove();
            expectEquals (view.getNumRows(), 1);
            expect (keepPicture (view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f), "universal-scenes.png"));
        }

        beginTest ("SETUP changes the console's configuration without a reset, keeping what is in use");
        {
            MixerEngine engine (0);
            const Counts counts { 8, 2, 8, 2, 4, 8, 4, 16 };
            engine.configure (&counts);
            UiRig rig (engine);
            auto viewOwner = std::make_unique<SetupView> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& view = *viewOwner;
            view.setSize (1200, 800);
            auto& ch1 = *engine.getChannels()[0];
            ch1.setName ("Kick");
            ch1.params.faderDb = -3.0f;

            // More mains and matrices: nothing goes, everything stays.
            view.setPending (SetupView::Mains, 4);
            view.setPending (SetupView::Matrices, 6);
            expect (view.describeRemovals().isEmpty());
            view.apply();
            const auto c = engine.getCounts();
            expect (c.mains == 4 && c.matrices == 6, "the new counts");
            auto& kick = *engine.getChannels()[0];
            expectEquals (kick.getOwnName(), juce::String ("Kick"));
            expectEquals (kick.params.faderDb.load(), -3.0f, "no reset");
            expect (kick.mains[3].on.load() == false, "its send to MAIN 4: the factory's");

            // Fewer buses: one that something feeds stays (with those before it).
            kick.sends[5].enabled = true;
            view.setPending (SetupView::Buses, 4);
            expect (view.describeRemovals().joinIntoString (" ").contains ("BUS 5-8"));
            view.apply();
            expectEquals (engine.getCounts().buses, 6);
            expect (view.getStatus().contains ("kept"), view.getStatus());
            // Fewer channels: they go, with their settings.
            view.setPending (SetupView::Channels, 4);
            expect (view.describeRemovals().joinIntoString (" ").contains ("CH 5-8, with their settings"));
            view.apply();
            expectEquals (engine.getCounts().channels, 4);
            // The ranges hold: FX slots from the WING's 16, a main at least.
            view.setPending (SetupView::FxSlots, 4);
            expectEquals (view.getPending (SetupView::FxSlots), ld::kWingFx);
            view.setPending (SetupView::Mains, 0);
            expectEquals (view.getPending (SetupView::Mains), 1);
            view.revert();
            expectEquals (view.getPending (SetupView::Mains), 4);
            view.refresh();
            expect (keepPicture (view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f), "universal-setup.png"));
        }

        beginTest ("The EQ curve's handles: a band's frequency and gain by dragging, its Q with the wheel, its default with a double tap");
        {
            MixerEngine engine (0);
            test::setChannels (engine, 2);
            auto& strip = *engine.getChannels()[0];
            auto& eq = strip.params.eq();
            const auto& model = models::model ((models::Table) strip.getEqTable(), eq.model.load());
            ResponseView view (Theme::dark());
            view.setSize (600, 300);
            view.response = [&strip] (const double* f, double* db, int n)
            {
                ld::ld_strip_response (strip.getHandle(), 1, f, db, n, 48000.0);
                return true;
            };
            view.handleSource = [&eq, &model] { return ResponseView::eqHandles (model, eq.values); };
            view.refresh();
            const auto& handles = view.getHandles();
            juce::StringArray labels;
            for (const auto& h : handles)
                labels.add (h.label);
            expect (labels.joinIntoString (" ") == "L 1 2 3 4 H", labels.joinIntoString (" "));

            // Band 1 up and to the right.
            const int band = labels.indexOf ("1");
            // The band's values (the handles are taken again at every refresh: keep what they point at).
            auto* freq = handles[(size_t) band].freq;
            auto* gain = handles[(size_t) band].gain;
            auto* q = handles[(size_t) band].q;
            const float freqBefore = freq->load(), gainBefore = gain->load(), qBefore = q->load();
            const auto at = view.handlePosition (band);
            view.mouseDown (mouseEvent (view, at, at));
            view.mouseDrag (mouseEvent (view, at + juce::Point<float> (60.0f, -50.0f), at));
            view.mouseUp (mouseEvent (view, at + juce::Point<float> (60.0f, -50.0f), at));
            expect (freq->load() > freqBefore * 1.3f, "higher");
            expect (gain->load() > gainBefore + 3.0f, "louder");
            expect (! view.getCurve().empty(), "the curve follows");
            // The wheel over it: Q.
            const auto moved = view.handlePosition (band);
            view.mouseWheelMove (mouseEvent (view, moved, moved), juce::MouseWheelDetails { 0.0f, 1.0f, false, false, false });
            expect (q->load() > qBefore, "a narrower band");
            // A double tap: back to its defaults.
            view.mouseDoubleClick (mouseEvent (view, moved, moved));
            expectWithinAbsoluteError (gain->load(), model.params[(size_t) model.find ("1g")].defaultValue, 0.001f);
        }

        beginTest ("The dynamics curve's handles: the threshold along the curve, the ratio up or down");
        {
            MixerEngine engine (0);
            test::setChannels (engine, 2);
            auto& strip = *engine.getChannels()[0];
            auto& dyn = strip.params.dynamics();
            const auto& model = models::model (models::Dynamics, dyn.model.load());
            const int thr = model.find ("thr"), ratio = model.find ("ratio");
            expect (thr >= 0 && ratio >= 0, "a compressor");
            TransferView view (Theme::dark());
            view.setSize (300, 300);
            view.bind (&dyn);
            view.refresh();
            expect (view.getCurveAt (0.0f) < 0.0f || dyn.values[thr].load() >= 0.0f, "it compresses above the threshold");

            const float thrBefore = dyn.values[thr].load();
            const auto at = view.handlePosition (0);
            expect (! at.isOrigin(), "a threshold handle");
            view.mouseDown (mouseEvent (view, at, at));
            view.mouseDrag (mouseEvent (view, at - juce::Point<float> (60.0f, 0.0f), at));
            view.mouseUp (mouseEvent (view, at - juce::Point<float> (60.0f, 0.0f), at));
            expect (dyn.values[thr].load() < thrBefore - 5.0f, "a lower threshold");

            const float ratioBefore = dyn.values[ratio].load();
            const auto top = view.handlePosition (1);
            view.mouseDown (mouseEvent (view, top, top));
            view.mouseDrag (mouseEvent (view, top + juce::Point<float> (0.0f, 80.0f), top));
            view.mouseUp (mouseEvent (view, top + juce::Point<float> (0.0f, 80.0f), top));
            expect (dyn.values[ratio].load() > ratioBefore, "down: more ratio");
        }

        beginTest ("MIXER's channel panel shows the selected strip whole; CHANNEL hides it and the columns take the room");
        {
            MixerEngine engine (0);
            test::setChannels (engine, 4);
            UiRig rig (engine);
            auto viewOwner = std::make_unique<MixerView> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& view = *viewOwner;
            view.setSize (1920, 1000);
            view.setChannelShown (true);
            auto& panel = view.getChannelPanel();
            expect (panel.isVisible() && panel.getStrip() == nullptr, "nothing selected: a hint");
            rig.selected = engine.getChannels()[2].get();
            view.selectionChanged();
            view.refresh();
            expect (panel.getStrip() == rig.selected);
            expect (panel.getEqView().getHandles().size() == 6, "the EQ's bands as handles");
            expect (panel.getEqView().getWidth() > 300 && panel.getDynamicsView().getWidth() > 150);
            const int columnHeight = view.getColumn (0)->getHeight();
            expect (keepPicture (view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f), "universal-channel-panel.png"));
            view.setChannelShown (false);
            expect (! panel.isVisible() && view.getColumn (0)->getHeight() > columnHeight + 150);
        }

        beginTest ("A model or device is chosen by its picture: a grid of the front panels, a tap chooses");
        {
            juce::StringArray names { "One", "Two", "Three", "Four", "Five", "Six", "Seven" };
            int painted = 0, picked = -1;
            PictureGrid grid (Theme::dark(), names, 2, [&painted] (juce::Graphics&, juce::Rectangle<float>, int) { ++painted; },
                              [&picked] (int i) { picked = i; });
            expectEquals (grid.getNumItems(), 7);
            expectEquals (grid.getHeight(), 2 * PictureGrid::kTileHeight);
            expect (grid.createComponentSnapshot (grid.getLocalBounds()).isValid() && painted == 7);
            const auto tile = grid.tileBounds (5).getCentre().toFloat();
            grid.mouseUp (mouseEvent (grid, tile, tile));
            expectEquals (picked, 5);
        }

        beginTest ("DCAs and mute groups in the MIXER view: a column per DCA and the mute groups' keys, after the strips or alone (DCAS)");
        {
            MixerEngine engine (0);
            const Counts counts { 8, 2, 4, 1, 2, 6, 3, 8 };
            engine.configure (&counts);
            UiRig rig (engine);
            auto viewOwner = std::make_unique<MixerView> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& view = *viewOwner;
            view.setSize (1400, 800);
            expectEquals (view.getNumColumns(), 8 + 2 + 4 + 1 + 2);
            expectEquals (view.getNumDcaColumns(), 6);
            expect (view.getMuteGroups() != nullptr);
            // After the strips, a gap between them.
            const auto* lastStrip = view.getColumn (view.getNumColumns() - 1);
            expectEquals (view.getDcaColumn (0)->getX() - lastStrip->getRight(), sizes::kKindGap);
            expect (view.getMuteGroups()->getX() > view.getDcaColumn (5)->getRight());

            view.setFilter (MixerView::kDcas);
            expectEquals (view.getNumColumns(), 0);
            expectEquals (view.getNumDcaColumns(), 6);
            view.setFilter ((int) StripKind::Bus);
            expectEquals (view.getNumDcaColumns(), 0);
            expect (view.getMuteGroups() == nullptr, "the strips of one kind only");
            view.setFilter (-1);

            // A DCA's keys and fader act on it.
            auto* dca3 = view.getDcaColumn (2);
            expectEquals (dca3->getDca(), 2);
            dca3->muteKey.onClick();
            dca3->soloKey.onClick();
            expect (engine.getDca (2).mute.load() && engine.getDca (2).solo.load());
            dca3->fader.setPositionFromUser (0.25);
            expect (engine.getDca (2).faderDb.load() < -10.0f, juce::String (engine.getDca (2).faderDb.load()));
            engine.setDcaName (2, "Drums");
            engine.setMuteGroupName (0, "Band");
            expectEquals (customGroupName (engine.getMuteGroupName (1), 1), juce::String(), "a default name isn't repeated under its number");
            expectEquals (customGroupName (engine.getMuteGroupName (0), 0), juce::String ("Band"));
            view.setFilter (MixerView::kDcas);
            view.refresh();
            expect (keepPicture (view.createComponentSnapshot (view.getLocalBounds()), "universal-dcas.png"));
            view.setFilter (-1);

            // The mute groups: a key engages a group, ALL OFF releases them.
            auto& groups = view.getMuteGroups()->groups;
            expectEquals (groups.count(), 3);
            const auto key = groups.keyArea (1).getCentre().toFloat();
            groups.mouseUp (mouseEvent (groups, key, key));
            expect (engine.muteGroups.contains (1) && ! engine.muteGroups.contains (0));
            view.getMuteGroups()->allOffKey.onClick();
            expect (! engine.muteGroups.any());
        }

        beginTest ("The inspector's STRIP page names and colours a strip (its source while linked) and assigns its DCAs and mute groups");
        {
            MixerEngine engine (0);
            engine.configure (nullptr); // a factory WING: channels linked to their sources
            UiRig rig (engine);
            auto panelOwner = std::make_unique<StripPanel> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& panel = *panelOwner;
            panel.setSize (440, 1200);

            // A channel with LINK CUSTOMIZATION TO SOURCE: the source is renamed and recoloured.
            auto& ch1 = *engine.getChannels()[0];
            panel.setStrip (&ch1);
            const auto ownBefore = ch1.getOwnName();
            expect (ui::customize::goesToSource (ch1), "a factory channel is linked to its source");
            if (ui::customize::goesToSource (ch1))
            {
                panel.rename ("Kick");
                expectEquals (ch1.getName(), juce::String ("Kick"));
                expectEquals (engine.getSourceName (ch1.linkedSourceIndex()), juce::String ("Kick"));
                expectEquals (ch1.getOwnName(), ownBefore, "the strip's own name stays");
                panel.chooseColour (7);
                expectEquals (ch1.getColourIndex(), 7);
            }

            // A bus: its own name and colour.
            auto& bus2 = *engine.getBuses()[1];
            panel.setStrip (&bus2);
            panel.rename ("Monitors");
            expectEquals (bus2.getName(), juce::String ("Monitors"));
            panel.chooseColour (3);
            expectEquals (bus2.getColourIndex(), 3);
            // A colour by its swatch.
            const auto swatch = panel.swatchArea (5).getCentre().toFloat();
            panel.mouseUp (mouseEvent (panel, swatch, swatch));
            expectEquals (bus2.getColourIndex(), 5);

            // DCAs and mute groups by their keys: a tap assigns, another unassigns.
            expectEquals (panel.dcas.count(), engine.getNumDcas());
            const auto dca4 = panel.dcas.keyArea (3).getCentre().toFloat();
            panel.dcas.mouseUp (mouseEvent (panel.dcas, dca4, dca4));
            expect (bus2.params.dcaMask.contains (3));
            panel.dcas.mouseUp (mouseEvent (panel.dcas, dca4, dca4));
            expect (! bus2.params.dcaMask.contains (3));
            const auto mg2 = panel.muteGroups.keyArea (1).getCentre().toFloat();
            panel.muteGroups.mouseUp (mouseEvent (panel.muteGroups, mg2, mg2));
            expect (bus2.params.muteGroupMask.contains (1));
            expect (panel.heightFor (440) >= panel.muteGroups.getBottom(), "the scroller shows every key");

            // In the inspector, its third tab.
            auto inspectorOwner = std::make_unique<Inspector> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& inspector = *inspectorOwner;
            inspector.setSize (460, 900);
            inspector.setStrip (&bus2);
            inspector.showPage (Inspector::Page::Strip);
            expect (inspector.getPage() == Inspector::Page::Strip && inspector.stripPage.getStrip() == &bus2);
            expect (! inspector.chain.isVisible() && ! inspector.sends.isVisible());
            expect (keepPicture (inspector.createComponentSnapshot (inspector.getLocalBounds()), "universal-strip-page.png"));
        }

        beginTest ("SETUP > REMOTE: the WING, web and MIDI remotes' settings, in the app settings and applied at once");
        {
            MixerEngine engine (0);
            engine.configure (nullptr);
            UiRig rig (engine);
            auto setupOwner = std::make_unique<SetupView> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& setup = *setupOwner;
            setup.setSize (1600, 900);
            setup.showPage (SetupView::Page::Remote);
            auto& r = setup.remote;
            expect (r.isVisible());

            // WING REMOTE: enable, a server off, a port, the console name (upper case).
            expect (! WingRemote::Settings::load (rig.settings).enabled);
            r.wingOn.onClick();
            expect (WingRemote::Settings::load (rig.settings).enabled);
            r.oscOn.onClick();
            expect (! WingRemote::Settings::load (rig.settings).osc);
            expect (! r.nativePort.change ("not a port") && ! r.nativePort.change ("70000"), "a port is 1-65535");
            expect (r.nativePort.change ("3000"));
            expectEquals (WingRemote::Settings::load (rig.settings).nativePort, 3000);
            expect (r.consoleName.change ("foh desk"));
            expectEquals (WingRemote::Settings::load (rig.settings).name, juce::String ("FOH DESK"));
            r.refresh();
            expect (! r.getWingStatus().isEmpty() && ! r.getWingStatus().contains ("WING REMOTE IS OFF"));

            // WEB REMOTE: enable, port, PIN (4-8 digits or none), view only.
            r.webOn.onClick();
            expect (WebRemote::Settings::load (rig.settings).enabled);
            expect (r.webPort.change ("8090"));
            expectEquals (WebRemote::Settings::load (rig.settings).port, 8090);
            expect (! r.webPin.change ("12"), "too short");
            expect (r.webPin.change ("4321"));
            expectEquals (WebRemote::Settings::load (rig.settings).pin, juce::String ("4321"));
            r.webPin.refresh();
            expect (r.webPin.change (""), "no PIN");
            expect (WebRemote::Settings::load (rig.settings).pin.isEmpty());
            r.viewOnly.onClick();
            expect (WebRemote::Settings::load (rig.settings).viewOnly);

            // MIDI: a function's route, DAW control and its emulation.
            r.routes[(size_t) MidiRemote::SceneChange]->chooseItem (2);
            expectEquals (MidiRemote::Settings::load (rig.settings).routes[(size_t) MidiRemote::SceneChange], (int) MidiRemote::RouteUsb);
            expect (r.routes[0]->device == devices::wing);
            r.dawOn.onClick();
            expect (MidiRemote::Settings::load (rig.settings).daw.enabled);
            r.dawEmulation.chooseItem (1);
            expect (MidiRemote::Settings::load (rig.settings).daw.emulation == DawRemote::Emulation::Hui);
            r.devices[0]->chooseItem (0);
            expect (MidiRemote::Settings::load (rig.settings).inputs[(size_t) MidiRemote::Din].isEmpty(), "DIN IN: NONE");
            // A device that is set but not there reads NOT FOUND, never another device.
            {
                auto s = MidiRemote::Settings::load (rig.settings);
                s.outputs[(size_t) MidiRemote::Usb] = "No Such MIDI Device";
                s.save (rig.settings);
                r.devices[3]->refresh();
                expectEquals (r.devices[3]->getText(), juce::String ("No Such MIDI Device (NOT FOUND)"));
                s.outputs[(size_t) MidiRemote::Usb] = MidiRemote::virtualDevice;
                s.save (rig.settings);
                r.devices[3]->refresh();
                expect (MidiRemote::canCreateVirtualPorts() ? r.devices[3]->getText() == MidiRemote::virtualPortName (MidiRemote::Usb)
                                                            : r.devices[3]->getText() == "VIRTUAL PORT (NOT FOUND)",
                        r.devices[3]->getText());
            }
            r.refresh();
            expect (keepPicture (setup.createComponentSnapshot (setup.getLocalBounds()), "universal-remote.png"));
            r.wingOn.onClick(); // off again (no servers left behind)
            expect (! WingRemote::Settings::load (rig.settings).enabled);
        }

        beginTest ("SCENES: the selected scene's snippets, made of the selected strip, recalled, stored, renamed and removed");
        {
            MixerEngine engine (0);
            engine.configure (nullptr);
            UiRig rig (engine);
            auto viewOwner = std::make_unique<ScenesView> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& view = *viewOwner;
            view.setSize (1400, 700);
            expectEquals (view.add(), 0);
            expectEquals (view.getNumSnippets(), 0);

            // NEW: a snippet of the selected strip (none selected: nothing).
            expectEquals (view.newSnippet(), -1, "no strip selected");
            auto& ch4 = *engine.getChannels()[3];
            rig.selected = &ch4;
            ch4.setName ("Snare");
            ch4.params.faderDb = -4.0f;
            const int sn = view.newSnippet();
            expectEquals (sn, 0);
            expectEquals (view.getNumSnippets(), 1);
            expectEquals (view.getSelectedSnippet(), 0);
            const auto s = rig.show.getSnippet (0, 0);
            expect (s.has_value() && s->strips.size() == 1);

            // GO brings the strip back as the snippet holds it; another strip is left alone.
            ch4.params.faderDb = -20.0f;
            engine.getChannels()[0]->params.faderDb = -9.0f;
            view.recallSnippet();
            expectWithinAbsoluteError (ch4.params.faderDb.load(), -4.0f, 0.01f);
            expectWithinAbsoluteError (engine.getChannels()[0]->params.faderDb.load(), -9.0f, 0.01f, "not in the snippet");

            // STORE, RENAME, FIRE WITH SCENE.
            ch4.params.faderDb = -12.0f;
            view.storeSnippet();
            ch4.params.faderDb = 0.0f;
            view.recallSnippet();
            expectWithinAbsoluteError (ch4.params.faderDb.load(), -12.0f, 0.01f);
            view.renameSnippet ("Snare down");
            expectEquals (rig.show.getSnippet (0, 0)->name, juce::String ("Snare down"));
            view.toggleFireWithScene();
            expect (rig.show.getSnippet (0, 0)->fireWithScene && view.snippetFire.isEnabled());
            view.refresh();
            expect (keepPicture (view.createComponentSnapshot (view.getLocalBounds()), "universal-snippets.png"));

            // Another scene: its own (none); back again: the snippet is there.
            view.selectScene (-1);
            expectEquals (view.getNumSnippets(), 0);
            view.selectScene (0);
            expectEquals (view.getNumSnippets(), 1);
            view.selectSnippet (0);
            view.removeSnippet();
            expectEquals (view.getNumSnippets(), 0);
            expectEquals (view.getSelectedSnippet(), -1);
        }

        beginTest ("Channel and FX presets from the Universal Mixer: saved to a file and loaded back");
        {
            MixerEngine engine (0);
            engine.configure (nullptr);
            UiRig rig (engine);
            const auto folder = juce::File::createTempFile ("presets");
            folder.createDirectory();

            // A channel preset: the strip's processing and levels, not its routing.
            auto panelOwner = std::make_unique<StripPanel> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& panel = *panelOwner;
            panel.setSize (440, 1400);
            auto& ch3 = *engine.getChannels()[2];
            panel.setStrip (&ch3);
            ch3.params.faderDb = -7.5f;
            ch3.params.filter().lowCutFreq = 140.0f;
            const auto chanFile = folder.getChildFile ("Lead vocal" + juce::String (Show::channelPresetExtension));
            expect (panel.savePreset (chanFile).wasOk() && chanFile.existsAsFile());
            ch3.params.faderDb = 0.0f;
            ch3.params.filter().lowCutFreq = 40.0f;
            expect (panel.loadPreset (chanFile).wasOk());
            expectWithinAbsoluteError (ch3.params.filter().lowCutFreq.load(), 140.0f, 0.5f);
            expectEquals (Show::readPreset (chanFile).name, juce::String ("Lead vocal"));
            expect (panel.savePresetKey.isVisible() && panel.loadPresetKey.getBottom() <= panel.heightFor (440));
            expect (panel.loadPreset (folder.getChildFile ("missing.lchn")).failed(), "a file that isn't there");

            // An FX preset: the slot's device and its settings.
            auto rackOwner = std::make_unique<FxRackView> (rig.ctx, Theme::dark()); // on the heap: the test function's frame is big
            auto& rack = *rackOwner;
            rack.setSize (1400, 800);
            rack.selectSlot (0);
            const int hall = models::findModel (models::Fx, "HALL");
            expect (engine.getFx().setModel (0, hall));
            const int decay = models::model (models::Fx, hall).find ("dcy");
            engine.getFx().params (0).values[decay] = 3.5f;
            const auto fxFile = folder.getChildFile ("Long hall" + juce::String (Show::fxPresetExtension));
            expect (rack.savePreset (fxFile).wasOk() && fxFile.existsAsFile());
            expect (engine.getFx().setModel (0, models::findModel (models::Fx, "ST-DL")));
            expect (rack.loadPreset (fxFile).wasOk());
            expectEquals (engine.getFx().getModel (0), hall);
            expectWithinAbsoluteError (engine.getFx().params (0).values[decay].load(), 3.5f, 0.01f);
            expect (rack.savePresetKey.isVisible() && rack.loadPresetKey.isVisible());
            folder.deleteRecursively();
        }

        beginTest ("Every view of the Universal skin draws at 1280 x 720 (LIVEDAW_UI_SNAPSHOTS keeps them, to look at)");
        {
            MixerEngine engine (0);
            engine.configure (nullptr);
            UiRig rig (engine);
            rig.selected = engine.getChannels()[0].get();
            auto skinOwner = std::make_unique<ui::UniversalSkin> (rig.ctx); // on the heap: the test function's frame is big
            auto& skin = *skinOwner;
            skin.setSize (1280, 720);
            skin.selectionChanged();
            using V = ui::UniversalSkin::View;
            const std::pair<V, const char*> views[] = { { V::Mixer, "mixer" },       { V::Routing, "routing" }, { V::Fx, "fx" },
                                                        { V::Monitoring, "monitoring" }, { V::Scenes, "scenes" },   { V::Recorder, "recorder" },
                                                        { V::Setup, "setup" } };
            for (const auto& [v, viewName] : views)
            {
                skin.showView (v);
                const auto picture = skin.createComponentSnapshot (skin.getLocalBounds());
                expect (picture.isValid(), viewName);
                expect (keepPicture (picture, juce::String ("small-") + viewName + ".png"));
            }
            skin.getInspector().showPage (Inspector::Page::Strip);
            expect (keepPicture (skin.createComponentSnapshot (skin.getLocalBounds()), "small-strip.png"));
            skin.getSetup().showPage (SetupView::Page::Remote);
            skin.showView (V::Setup);
            expect (keepPicture (skin.createComponentSnapshot (skin.getLocalBounds()), "small-remote.png"));
        }

        beginTest ("The top bar names the show; its SHOW menu runs NEW, OPEN, SAVE and SAVE AS");
        {
            MixerEngine engine (0);
            engine.configure (nullptr);
            UiRig rig (engine);
            juce::StringArray calls;
            juce::File file;
            rig.ctx.getShowFile = [&file] { return file; };
            rig.ctx.newShow = [&calls] { calls.add ("new"); };
            rig.ctx.openShow = [&calls] { calls.add ("open"); };
            rig.ctx.saveShow = [&calls] (bool as) { calls.add (as ? "save as" : "save"); };
            auto skinOwner = std::make_unique<ui::UniversalSkin> (rig.ctx); // on the heap: the test function's frame is big
            auto& skin = *skinOwner;
            skin.setSize (1600, 900);
            expectEquals (skin.getShowTitle(), juce::String ("UNTITLED SHOW"));
            file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Friday Gig.lshow");
            expectEquals (skin.getShowTitle(), juce::String ("Friday Gig"));
            for (int item = 1; item <= 4; ++item)
                skin.runShowCommand (item);
            expectEquals (calls.joinIntoString (","), juce::String ("new,open,save,save as"));
            expect (skin.getShowKey().isVisible() && skin.getShowKey().getRight() <= skin.getWidth());
            expect (keepPicture (skin.createComponentSnapshot (skin.getLocalBounds()), "universal-topbar.png"));
        }
    }
};

static UniversalUiTests universalUiTests;
