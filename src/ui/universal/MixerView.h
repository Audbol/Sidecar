#pragma once

#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <memory>
#include <vector>

namespace livedaw::ui::universal
{

/** One strip of any kind as a column of the MIXER view: its colour, name and
    label (a tap selects it), pan, MUTE and SOLO, the fader with its output
    meter, and the fader's value. */
class StripColumn final : public juce::Component
{
public:
    StripColumn (UiContext&, Strip&, const Theme&);

    Strip& getStrip() const noexcept { return strip; }
    /** Takes the strip's values and meters (the view's timer calls it). */
    void refresh();
    void setSelected (bool shouldBeSelected);
    bool isSelected() const noexcept { return selected; }

    ToggleKey muteKey, soloKey;
    PanControl pan;
    FaderControl fader;
    MeterControl meter;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> header() const noexcept;

    UiContext& ctx;
    Strip& strip;
    const Theme& theme;
    juce::String name;
    int colour = 0;
    float shownDb = 0.0f;
    bool selected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripColumn)
};

/** The MIXER view: a column per strip, every kind and any number of each (the
    console's configuration), side by side and scrolled sideways, with keys
    that show one kind at a time. */
class MixerView final : public juce::Component, private juce::Timer
{
public:
    MixerView (UiContext&, const Theme&);
    ~MixerView() override;

    /** -1: every kind; else a StripKind. */
    void setFilter (int kind);
    int getFilter() const noexcept { return filter; }
    /** The strips changed (the view also notices by itself). */
    void rebuild();
    void selectionChanged();
    /** Takes every column's values and meters (the timer does this 30 times a second). */
    void refresh();

    int getNumColumns() const noexcept { return (int) columns.size(); }
    StripColumn* getColumn (int index) const noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { refresh(); }
    void layoutColumns();

    UiContext& ctx;
    const Theme& theme;
    int filter = -1;
    int structureVersion = -1;
    std::vector<std::unique_ptr<ToggleKey>> filterKeys;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<StripColumn>> columns;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};

} // namespace livedaw::ui::universal
