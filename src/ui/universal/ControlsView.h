#pragma once

#include "ui/CustomControlsPage.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <array>
#include <memory>
#include <vector>

namespace livedaw::ui::universal
{

/** The CONTROLS view: the show's custom controls, as the WING's CUSTOM CONTROLS
    section holds them (a layer of four encoders and eight keys; the same
    assignments, so a change here is one there too). The layer keys pick the layer;
    each control is played right here, its encoder as a bar and its two keys as keys.
    EDIT opens the assignments (the WING's CC EDIT page, embedded and tagged WING). */
class ControlsView final : public juce::Component, private juce::Timer
{
public:
    ControlsView (UiContext&, const Theme&);
    ~ControlsView() override;

    /** The encoder of a control: a bar over its function's value, moved like a ValueBar. */
    class Encoder final : public juce::Component
    {
    public:
        explicit Encoder (const Theme&);

        void setBinding (KnobBinding);
        bool isBound() const noexcept { return binding.isValid(); }
        float getPosition() const noexcept { return position; }
        void setPositionFromUser (float newPosition);
        void refresh();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        const Theme& theme;
        KnobBinding binding;
        float position = 0.0f, dragStart = 0.0f;
        juce::String shownText;
    };

    /** One of a layer's four controls. */
    class Column final : public juce::Component
    {
    public:
        Column (UiContext&, const Theme&, int index);

        /** Takes control `index` of `layer`, as the assignments have it now. */
        void bind (int layer);
        void refresh();

        Encoder encoder;
        ToggleKey upper, lower;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void bindKey (ToggleKey&, cc::Slot);

        UiContext& ctx;
        const Theme& theme;
        const int index;
        cc::Control control;
        std::array<bool, 2> held {};
        bool hasLower = true, hasEncoder = true;
    };

    int getLayer() const noexcept { return layer; }
    /** The layer shown (the upper part's 16, then USER); the WING's current one follows the first 16. */
    void setLayer (int);
    /** The assignments (CC EDIT) in place of the controls. */
    void showEditor (bool);
    bool isEditorShown() const noexcept { return editor != nullptr; }
    Column* getColumn (int index) const noexcept { return juce::isPositiveAndBelow (index, (int) columns.size()) ? columns[(size_t) index].get() : nullptr; }
    /** Takes the assignments and values again (the timer does this 15 times a second). */
    void refresh();

    std::vector<std::unique_ptr<ToggleKey>> layerKeys;
    ToggleKey editKey;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override
    {
        if (isVisible())
            refresh();
    }
    void bindLayer();

    UiContext& ctx;
    const Theme& theme;
    int layer = 0, shownRevision = -1;
    std::vector<std::unique_ptr<Column>> columns;
    std::unique_ptr<juce::Component> editor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlsView)
};

} // namespace livedaw::ui::universal
