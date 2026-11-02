#pragma once

#include "ui/Faceplate.h"
#include "ui/UiContext.h"
#include "ui/universal/ValueControls.h"

#include <memory>

namespace livedaw::ui::universal
{

/** The FX RACK view: a tile per FX slot in use (its device or plugin, how many
    inserts use it, its level), and under them the selected slot: its device
    (the WING's FX devices), its plugins (the FX slot menu: load one, which
    makes the slot EXT, open its window, bypass, remove), and the device's
    controls: the WING's front panel where it has one (ui::Faceplate) or the
    list of its parameters. */
class FxRackView final : public juce::Component, private juce::Timer
{
public:
    FxRackView (UiContext&, const Theme&);
    ~FxRackView() override;

    int getNumSlots() const noexcept { return numSlots; }
    void selectSlot (int slot);
    int getSelectedSlot() const noexcept { return selected; }
    /** The front panel (true) or the parameter list shown for the selected slot. */
    bool isShowingPanel() const noexcept;
    /** Takes the slots again (the timer does this 15 times a second). */
    void refresh();

    ChoiceBar device;
    ToggleKey pluginKey, panelKey;
    ParamPanel params;
    Faceplate faceplate;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Tiles;
    void timerCallback() override
    {
        if (isVisible()) // a hidden view waits
            refresh();
    }
    void bindSlot();

    UiContext& ctx;
    const Theme& theme;
    std::unique_ptr<Tiles> tiles;
    juce::Viewport tilesView, listView;
    int numSlots = 0, selected = 0, boundModel = -2;
    bool panelWanted = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxRackView)
};

} // namespace livedaw::ui::universal
