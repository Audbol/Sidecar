#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace livedaw::ui::universal
{

/** Picks something up under a mouse or a finger, for the layers' drag editing: a long press (a touch can't
    be told from a scroll or a tap any other way), or moving a few pixels at once with a mouse (a touch
    does that only where `movePicks` says: a handle made for it, the rest of the row scrolls). Add it as a
    mouse listener (not nested) to what can be picked up; the callbacks get the press's events.
    `onMenu` is what a right-click opens, and what a long press that is lifted without having moved opens (a
    finger's right-click); without it the lift is a drop. */
class DragPickup final : public juce::MouseListener,
                         private juce::Timer
{
public:
    static constexpr int kHoldMs = 500, kMovePx = 10;

    /** May a drag start at this press? (Unset: always.) */
    std::function<bool (const juce::MouseEvent&)> canPick;
    /** Does moving a finger that went down here pick it up? (Unset: no, it scrolls.) */
    std::function<bool (const juce::MouseEvent&)> movePicks;
    std::function<void (const juce::MouseEvent&)> onPick, onMove, onDrop, onMenu;

    bool isDragging() const noexcept { return dragging; }

    void cancel()
    {
        stopTimer();
        pending = dragging = byHold = false;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        cancel();
        if (canPick && ! canPick (e))
            return;
        if (e.mods.isPopupMenu())
        {
            if (onMenu)
                onMenu (e);
            return;
        }
        pending = true;
        moveStarts = ! e.source.isTouch() || (movePicks && movePicks (e));
        last.emplace (e);
        source = e.eventComponent;
        startTimer (kHoldMs);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging)
        {
            if (onMove)
                onMove (e);
            return;
        }
        if (! pending)
            return;
        last.emplace (e);
        if (e.getDistanceFromDragStart() > kMovePx)
        {
            if (moveStarts)
                begin (e);
            else
                cancel(); // it is scrolling
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const bool was = dragging, held = byHold;
        cancel();
        if (! was)
            return;
        if (held && onMenu && e.getDistanceFromDragStart() <= kMovePx)
            onMenu (e);
        else if (onDrop)
            onDrop (e);
    }

private:
    void begin (const juce::MouseEvent& e)
    {
        stopTimer();
        pending = false;
        dragging = true;
        if (onPick)
            onPick (e);
    }

    void timerCallback() override
    {
        stopTimer();
        if (pending && source != nullptr && last.has_value())
        {
            byHold = true;
            begin (*last);
        }
    }

    std::optional<juce::MouseEvent> last;
    juce::Component::SafePointer<juce::Component> source;
    bool pending = false, dragging = false, byHold = false, moveStarts = true;
};

/** A long press (or a right-click) on something that isn't picked up, for its menu: the keys of the layers.
    Add it as a mouse listener; `onPress` gets the event. A key's own click must not follow a press that
    opened the menu: it asks `wasFired()` first (the flag lasts until the button is up). */
class LongPress final : public juce::MouseListener,
                        private juce::Timer
{
public:
    static constexpr int kHoldMs = 500, kMovePx = 10;

    std::function<void (const juce::MouseEvent&)> onPress;

    bool wasFired() const noexcept { return fired; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        stopTimer();
        fired = false;
        if (e.mods.isPopupMenu())
        {
            fire (e);
            return;
        }
        last.emplace (e);
        startTimer (kHoldMs);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() > kMovePx)
            stopTimer();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        stopTimer();
        fired = false; // after the button's own mouseUp has asked
    }

private:
    void fire (const juce::MouseEvent& e)
    {
        fired = true;
        if (onPress)
            onPress (e);
    }

    void timerCallback() override
    {
        stopTimer();
        if (last.has_value())
            fire (*last);
    }

    std::optional<juce::MouseEvent> last;
    bool fired = false;
};

/** Scrolls while a dragged finger is held near an edge: `update` with where the pointer is along the axis
    and the two edges; `scrollBy` gets a signed step every tick while it is within `kZone` of one of them
    (faster the nearer it is to the edge). */
class EdgeScroll final : private juce::Timer
{
public:
    static constexpr int kZone = 56, kMaxStep = 18;

    std::function<void (int)> scrollBy;

    void update (int pos, int low, int high)
    {
        int step = 0;
        if (pos >= low && pos < low + kZone)
            step = -juce::jmax (1, (low + kZone - pos) * kMaxStep / kZone);
        else if (pos <= high && pos > high - kZone)
            step = juce::jmax (1, (pos - (high - kZone)) * kMaxStep / kZone);
        speed = step;
        if (step != 0 && ! isTimerRunning())
            startTimer (16);
        else if (step == 0)
            stopTimer();
    }

    void stop()
    {
        speed = 0;
        stopTimer();
    }

    bool isScrolling() const noexcept { return speed != 0; }

    /** One tick, as the timer makes it. */
    void tick()
    {
        if (speed != 0 && scrollBy)
            scrollBy (speed);
    }

private:
    void timerCallback() override { tick(); }

    int speed = 0;
};

} // namespace livedaw::ui::universal
