#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace livedaw::ui::universal
{

/** Picks something up under a mouse or a finger, for the layers' drag editing: a long press (a touch can't
    be told from a scroll or a tap any other way), or, with a mouse, moving a few pixels at once. Add it as a
    mouse listener (not nested) to what can be picked up; the callbacks get the press's events. A touch that
    moves before the hold is up is left to scroll. */
class DragPickup final : public juce::MouseListener,
                         private juce::Timer
{
public:
    static constexpr int kHoldMs = 450, kMovePx = 10;

    /** May a drag start at this press? (Unset: always.) */
    std::function<bool (const juce::MouseEvent&)> canPick;
    std::function<void (const juce::MouseEvent&)> onPick, onMove, onDrop;

    bool isDragging() const noexcept { return dragging; }

    void cancel()
    {
        stopTimer();
        pending = dragging = false;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        cancel();
        if (canPick && ! canPick (e))
            return;
        pending = true;
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
            if (e.source.isTouch())
                cancel(); // it is scrolling
            else
                begin (e);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const bool was = dragging;
        cancel();
        if (was && onDrop)
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
            begin (*last);
    }

    std::optional<juce::MouseEvent> last;
    juce::Component::SafePointer<juce::Component> source;
    bool pending = false, dragging = false;
};

} // namespace livedaw::ui::universal
