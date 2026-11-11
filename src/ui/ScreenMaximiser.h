#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <initializer_list>
#include <utility>
#include <vector>

namespace livedaw::ui
{

/** Maximise display, as the WING's: a console surface's display (its touchscreen, its LCD) enlarged to fill
    the window, everything else out of the way on a black ground; F10 or a double-click on the display does it,
    and again (or Esc) restores. The surface registers each display (a child of its own), calls layout() at the
    end of its resized() and passes its key presses to keyPressed(). The displays keep their size and place in
    the surface's own layout (restoring is a resized()); maximised, each is scaled to fit the window. */
class ScreenMaximiser final : private juce::MouseListener
{
public:
    /** `screens`: the displays (children of the surface, constructed before this). */
    ScreenMaximiser (juce::Component& surfaceToUse, std::initializer_list<juce::Component*> displays) : surface (surfaceToUse)
    {
        surface.addChildComponent (backdrop);
        for (auto* d : displays)
            attach (*d);
    }

    ~ScreenMaximiser() override
    {
        for (auto& s : screens)
            if (s != nullptr)
                s->removeMouseListener (this);
    }

    /** A display (a direct child of the surface); a double-click on it, or on anything in it, maximises it. */
    void attach (juce::Component& screen)
    {
        screens.push_back (&screen);
        screen.addMouseListener (this, true);
    }

    bool isOn() const noexcept { return active != nullptr; }
    juce::Component* getActive() const noexcept { return active.getComponent(); }

    /** Maximises `screen` (nullptr: back to the whole surface). */
    void set (juce::Component* screen)
    {
        if (screen == active.getComponent())
            return;
        if (screen != nullptr && active == nullptr)
        {
            hidden.clear();
            for (auto* c : surface.getChildren())
                if (c != &backdrop && c != screen)
                    hidden.push_back ({ c, c->isVisible() });
        }
        active = screen;
        if (screen == nullptr)
        {
            backdrop.setVisible (false);
            for (auto& h : hidden)
                if (h.first != nullptr)
                    h.first->setVisible (h.second);
            hidden.clear();
        }
        else
        {
            // (a second display while one is up: the first one hidden again by layout())
            for (auto& h : hidden)
                if (h.first == screen && h.first != nullptr)
                    h.first->setVisible (true);
        }
        surface.resized(); // places everything again; layout() then takes over when on
        surface.repaint();
    }

    void toggle (juce::Component* screen = nullptr)
    {
        if (screen == nullptr)
            screen = screens.empty() ? nullptr : screens.front().getComponent();
        set (isOn() ? nullptr : screen);
    }

    /** The end of the surface's resized(): the active display over the window, the rest hidden. */
    void layout()
    {
        auto* screen = active.getComponent();
        if (screen == nullptr)
            return;
        const auto all = surface.getLocalBounds();
        for (auto* c : surface.getChildren())
            if (c != &backdrop && c != screen)
                c->setVisible (false);
        backdrop.setTransform ({});
        backdrop.setBounds (all);
        backdrop.setVisible (true);
        backdrop.toFront (false);
        screen->setVisible (true);
        screen->toFront (false);
        const int nw = juce::jmax (1, screen->getWidth()), nh = juce::jmax (1, screen->getHeight());
        screen->setBounds (0, 0, nw, nh);
        const float s = juce::jmin ((float) all.getWidth() / (float) nw, (float) all.getHeight() / (float) nh);
        screen->setTransform (juce::AffineTransform::scale (s).translated (((float) all.getWidth() - (float) nw * s) * 0.5f,
                                                                           ((float) all.getHeight() - (float) nh * s) * 0.5f));
    }

    /** F10 toggles, Esc returns; true if used. */
    bool keyPressed (const juce::KeyPress& key)
    {
        if (screens.empty())
            return false;
        if (key.getKeyCode() == juce::KeyPress::F10Key && ! key.getModifiers().isAnyModifierKeyDown())
        {
            toggle();
            return true;
        }
        if (key == juce::KeyPress::escapeKey && isOn())
        {
            set (nullptr);
            return true;
        }
        return false;
    }

private:
    struct Backdrop final : juce::Component
    {
        Backdrop() { setOpaque (true); }
        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
    };

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        for (auto& s : screens)
            if (s != nullptr && (e.originalComponent == s.getComponent() || s->isParentOf (e.originalComponent)))
            {
                toggle (s.getComponent());
                return;
            }
    }

    juce::Component& surface;
    Backdrop backdrop;
    std::vector<juce::Component::SafePointer<juce::Component>> screens;
    juce::Component::SafePointer<juce::Component> active;
    std::vector<std::pair<juce::Component*, bool>> hidden;
};

} // namespace livedaw::ui
