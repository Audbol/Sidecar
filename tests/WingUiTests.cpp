// The WING surface's screens and pages, driven as a user would: what a press reaches,
// what a pointer passing over does, what asks first.

#include "TestHelpers.h"
#include "engine/EventLog.h"
#include "engine/MixerEngine.h"
#include "engine/PluginHost.h"
#include "engine/Show.h"
#include "ui/ChainSummary.h"
#include "ui/HomePage.h"
#include "ui/HomeScreens.h"
#include "ui/HomeWidgets.h"
#include "ui/Knobs.h"
#include "ui/LookAndFeel.h"
#include "ui/MeterBank.h"
#include "ui/Pages.h"
#include "ui/SurfaceSections.h"
#include "ui/UiContext.h"

#include <functional>
#include <typeinfo>
#include <vector>

using namespace livedaw;
using livedaw::test::FakePlugin;

namespace
{
struct WingRig
{
    explicit WingRig (MixerEngine& e) : engine (e), show (e, host), meters (e), ctx (e, host, show, log, devices, settings, meters)
    {
        ctx.selectStrip = [this] (Strip* s) { selected = s; };
        ctx.getSelectedStrip = [this] { return selected; };
        ctx.showHomeTab = [this] (ui::HomeTab t) { tabs.push_back (t); };
        ctx.openPluginEditor = [this] (int slot) { opened.push_back (slot); };
    }
    ~WingRig() { settingsFile.deleteFile(); }

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
    std::vector<ui::HomeTab> tabs; // the HOME tabs the display was asked to show
    std::vector<int> opened;       // the FX slots whose plugin window was asked for
};

template <typename T>
std::vector<T*> findAll (juce::Component& root)
{
    std::vector<T*> found;
    std::function<void (juce::Component&)> walk = [&found, &walk] (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* t = dynamic_cast<T*> (child))
                found.push_back (t);
            walk (*child);
        }
    };
    walk (root);
    return found;
}

juce::Button* findButton (juce::Component& root, const juce::String& text)
{
    for (auto* b : findAll<juce::Button> (root))
        if (b->getButtonText() == text)
            return b;
    return nullptr;
}

juce::MouseEvent mouseAt (juce::Component& c, juce::Point<int> p)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    return { source, p.toFloat(), {}, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, p.toFloat(), now, 1, false };
}
} // namespace

class WingUiTests final : public juce::UnitTest
{
public:
    WingUiTests() : juce::UnitTest ("WING surface UI", "LiveDAW") {}

    void runTest() override
    {
        beginTest ("A channel's INS screen: OPEN EDITOR takes the press when the slot holds a plugin");
        {
            MixerEngine engine (0);
            test::setChannels (engine, 2);
            WingRig rig (engine);
            auto& fx = engine.getFx();
            fx.setPlugin (0, std::make_unique<FakePlugin> (1.0f, FakePlugin::Mode::Gain, "editor"));
            auto& ch = *engine.getChannels()[0];
            ch.params.preInsert.on = true;
            ch.params.preInsert.fx = 0;

            auto screen = ui::makeInsertScreen (rig.ctx, ch, false);
            screen->setSize (1160, 600);
            screen->setVisible (true); // shown, as on the display (a hidden component takes no press)
            test::pumpMessages (400); // its look at the slot (4 a second) finds the plugin
            auto* open = findButton (*screen, "OPEN EDITOR");
            expect (open != nullptr && open->isVisible());
            if (open != nullptr)
            {
                // The front panel (a device's) used to lie over it, unbound, and took the press.
                auto* hit = screen->getComponentAt (screen->getLocalPoint (open, open->getLocalBounds().getCentre()));
                expect (hit == open || (hit != nullptr && open->isParentOf (hit)),
                        "a press on OPEN EDITOR reaches it, not " + (hit != nullptr ? juce::String (typeid (*hit).name()) : juce::String ("nothing")));
                open->triggerClick();
                test::pumpMessages (50);
                expect (rig.opened == std::vector<int> { 0 });
            }
            screen.reset();
            fx.setPlugin (0, nullptr);
            engine.getGarbage().collectAllNow();
        }

        beginTest ("The channel strip's encoders: pointing at one shows its slot there; a grab or a wheel turn moves the display");
        {
            MixerEngine engine (0);
            test::setChannels (engine, 2);
            WingRig rig (engine);
            rig.selected = engine.getChannels()[0].get();
            ui::ChannelStripSection section (rig.ctx);
            section.setSize (600, 300);
            section.refresh();
            const auto knobs = findAll<ui::DisplayKnob> (section);
            expectEquals ((int) knobs.size(), 11, "six slots, four EQ bands, the EQ's mix");
            if (knobs.size() == 11)
            {
                // The pointer on its way across the panel (to a plugin window, say).
                for (auto* knob : knobs)
                    knob->mouseEnter (mouseAt (*knob, knob->getLocalBounds().getCentre()));
                expect (rig.tabs.empty(), "the display stays where it is");

                // A grab of the EQ's first band: the EQ (CH STRIP TOUCH SELECT is on by default).
                auto* band = knobs[6];
                band->getSlider().onDragStart();
                expect (rig.tabs.size() == 1 && rig.tabs.back() == ui::HomeTab::Eq);
                // A turn of the wheel on it.
                auto& dial = band->getSlider();
                band->mouseWheelMove (mouseAt (dial, dial.getLocalBounds().getCentre()), juce::MouseWheelDetails { 0.0f, 0.1f, false, false, false });
                expectEquals ((int) rig.tabs.size(), 2);
                // A slot's encoder: its block.
                knobs[2]->getSlider().onDragStart();
                expect (rig.tabs.size() == 3 && rig.tabs.back() == ui::HomeTab::Gate);

                // CH STRIP TOUCH SELECT off: a grab leaves the display alone too.
                rig.settings.setValue (ui::uiprefs::stripTouchSelect, false);
                band->getSlider().onDragStart();
                expectEquals ((int) rig.tabs.size(), 3);
            }
        }

        beginTest ("HOME > CHAIN: the tab counts the blocks and taps the Universal Mixer added; the screen shows the chain with them");
        {
            MixerEngine engine (0);
            test::setChannels (engine, 2);
            WingRig rig (engine);
            auto& ch = *engine.getChannels()[0];
            rig.selected = &ch;
            ui::HomePage page (rig.ctx);
            ui::setWingScreenHeight (800.0f); // the display's screen, as MainDisplay sets it
            page.setSize (1280, 800);
            page.setVisible (true);
            page.refresh();
            test::pumpMessages (300);
            expect (findButton (page, "CHAIN") != nullptr, "a strip of the console's own blocks: CHAIN");

            // An EQ block after the dynamics and a user tap at the end (the Universal core's).
            const auto chain = ch.getChain();
            int afterDyn = -1;
            for (int i = 0; i < (int) chain.size(); ++i)
                if (chain[(size_t) i].kind == ld::kStepDynamics)
                    afterDyn = i + 1;
            const int uid = ch.insertBlock (BlockKind::Eq, afterDyn);
            expect (uid >= 0 && ch.insertTap (-1) >= 0);
            expectEquals (ui::countExtras (ch), 2);
            test::pumpMessages (300); // the block column's look (8 a second) updates the tab
            expect (findButton (page, "CHAIN +2") != nullptr, "CHAIN +2");

            // In the chain's order, named, the block with its model.
            juce::StringArray names;
            for (const auto& step : ch.getChain())
                if (ui::isShown (step))
                    names.add (ui::stepName (step));
            const auto order = names.joinIntoString (" ");
            expect (order.contains ("DYNAMICS EQ BLOCK") && order.endsWith ("TAP 1"), order);
            for (const auto& step : ch.getChain())
                if (step.isBlock())
                    expect (ui::blockDetail (ch, step).isNotEmpty(), "the block's model");

            page.showTab (ui::HomeTab::Chain);
            expect (page.getTab() == ui::HomeTab::Chain);
            const auto picture = page.createComponentSnapshot (page.getLocalBounds());
            expect (picture.isValid());
            // LIVEDAW_UI_SNAPSHOTS=<folder> keeps the picture there, to look at.
            if (const auto folder = juce::SystemStats::getEnvironmentVariable ("LIVEDAW_UI_SNAPSHOTS", {}); folder.isNotEmpty())
            {
                juce::FileOutputStream out (juce::File (folder).getChildFile ("wing-home-chain.png"));
                out.setPosition (0);
                out.truncate();
                juce::PNGImageFormat().writeImageToStream (picture, out);
            }
        }

        beginTest ("EFFECTS, a plugin's slot: its name and keys sit under the SOURCE keys; OUTBOARD asks before it unloads the plugin");
        {
            MixerEngine engine (0);
            WingRig rig (engine);
            auto& fx = engine.getFx();
            fx.setPlugin (0, std::make_unique<FakePlugin> (1.0f, FakePlugin::Mode::Gain, "kept"));
            {
                ui::EffectsPage page (rig.ctx);
                page.setSize (1160, 600);
                page.editSlot (0);
                test::pumpMessages (100);

                auto* open = findButton (page, "OPEN EDITOR");
                auto* load = findButton (page, "LOAD / REPLACE");
                ui::home::Segmented* source = nullptr;
                for (auto* s : findAll<ui::home::Segmented> (page))
                    if (s->isVisible())
                        source = s;
                expect (open != nullptr && open->isVisible() && load != nullptr && source != nullptr);
                if (open != nullptr && load != nullptr && source != nullptr)
                {
                    const auto area = [&page] (juce::Component& c) { return page.getLocalArea (c.getParentComponent(), c.getBounds()); };
                    // The plugin's name and its maker / format / LOAD line go between them.
                    expect (area (*open).getY() >= area (*source).getBottom() + 60, "OPEN EDITOR under the SOURCE keys and the plugin's name");
                    expect (! area (*open).intersects (area (*source)) && ! area (*load).intersects (area (*source)));

                    // OUTBOARD (the right half of SOURCE): a question first, the plugin stays meanwhile.
                    const int before = page.getNumChildComponents();
                    source->mouseUp (mouseAt (*source, { source->getWidth() * 3 / 4, source->getHeight() / 2 }));
                    expect (fx.hasPlugin (0) && ! fx.isOutboard (0), "nothing is unloaded before the answer");
                    expectEquals (page.getNumChildComponents(), before + 1, "the question over the page");
                    if (page.getNumChildComponents() > before)
                        delete page.getChildComponent (page.getNumChildComponents() - 1); // not answered
                }
            }
            fx.setPlugin (0, nullptr);
            engine.getGarbage().collectAllNow();
        }
    }
};

static WingUiTests wingUiTests;
