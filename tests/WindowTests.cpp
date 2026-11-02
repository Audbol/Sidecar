// What JUCE's windows do that the monkey test (tests/MonkeyMain.cpp) has to allow for.

#include "TestHelpers.h"

#include <typeinfo>

using namespace livedaw;

class WindowTests final : public juce::UnitTest
{
public:
    WindowTests() : juce::UnitTest ("Windows", "LiveDAW") {}

    void runTest() override
    {
        beginTest ("A press on a pop-up menu's drop shadow goes to the window under it and can delete the shadow's window");
        {
            // The monkey once picked a drop shadow's window for its clicks. Blocked by the
            // menu, the press under it dismissed the menu, the shadow's window was deleted
            // before the press returned, and the click's mouse-up then went to the deleted
            // window: a crash of the test (0xC0000409). The OS sends those windows no mouse
            // input at all, so the monkey now leaves them alone.
            juce::DocumentWindow window ("Menu test", juce::Colours::black, 0);
            window.setUsingNativeTitleBar (true);
            juce::Component content, target;
            content.setSize (600, 400);
            target.setBounds (250, 40, 100, 24);
            content.addAndMakeVisible (target);
            window.setContentNonOwned (&content, true);
            window.setTopLeftPosition (80, 80);
            window.setVisible (true);
            test::pumpMessages (100);

            juce::PopupMenu menu;
            for (int i = 1; i <= 4; ++i)
                menu.addItem (i, "Item " + juce::String (i));
            bool dismissed = false;
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [&dismissed] (int) { dismissed = true; });
            test::pumpMessages (300);

            // The menu's shadow windows (only where JUCE draws them itself: Windows).
            juce::Component* shadow = nullptr;
            auto& desktop = juce::Desktop::getInstance();
            for (int i = 0; i < desktop.getNumComponents(); ++i)
            {
                auto* c = desktop.getComponent (i);
                if (c == nullptr || c->getPeer() == nullptr || ! juce::String (typeid (*c).name()).contains ("ShadowWindow"))
                    continue;
                expect ((c->getPeer()->getStyleFlags() & juce::ComponentPeer::windowIgnoresMouseClicks) != 0,
                        "the OS sends a drop shadow's window no clicks");
                const auto centre = c->getScreenBounds().getCentre();
                if (shadow == nullptr && content.getScreenBounds().contains (centre) && ! target.getScreenBounds().contains (centre))
                    shadow = c;
            }

            if (auto* mainPeer = window.getPeer(); shadow != nullptr && mainPeer != nullptr)
            {
                auto* shadowPeer = shadow->getPeer();
                const auto mouse = juce::MouseInputSource::InputSourceType::mouse;
                const auto at = shadow->getScreenBounds().getCentre().toFloat();
                const auto t = juce::Time::currentTimeMillis();
                // The last event went to the test window, as the monkey's last click had...
                mainPeer->handleMouseEvent (mouse, mainPeer->globalToLocal (at), {}, 0.0f, 0.0f, t);
                // ...so a press on the shadow goes to the test window under it.
                shadowPeer->handleMouseEvent (mouse, shadowPeer->globalToLocal (at), {}, 0.0f, 0.0f, t);
                if (juce::ComponentPeer::isValidPeer (shadowPeer))
                    shadowPeer->handleMouseEvent (mouse, shadowPeer->globalToLocal (at), juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, t);
                expect (! juce::ComponentPeer::isValidPeer (shadowPeer), "the press deleted the window it was sent to");
                mainPeer->handleMouseEvent (mouse, mainPeer->globalToLocal (at), {}, 0.0f, 0.0f, t + 40);
                test::pumpMessages (200);
                expect (dismissed, "the press dismissed the menu");
            }
            else
            {
                logMessage ("(no drop shadow windows over the test window here: nothing more to check)");
            }

            juce::PopupMenu::dismissAllActiveMenus();
            test::pumpMessages (100);
        }
    }
};

static WindowTests windowTests;
