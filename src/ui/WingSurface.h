#pragma once

#include "ui/CompactSection.h"
#include "ui/Controls.h"
#include "ui/DawSurface.h"
#include "ui/FaderSection.h"
#include "ui/FourChannelSection.h"
#include "ui/MainDisplay.h"
#include "ui/Skin.h"
#include "ui/SurfaceLock.h"
#include "ui/SurfaceSections.h"
#include "ui/UiContext.h"

namespace livedaw::ui
{

/** The WING skin: a WING surface. Top: the channel strip section, the main
    display and the monitoring panel. Bottom: the left (12) and center (8)
    fader sections, the custom controls section and the right (4) fader
    section, each fader section with its layer keys. The main display can take
    the whole window (F10, its maximize key): the rest of the surface stays
    alive underneath. As a WING COMPACT (SETUP > GENERAL > CONSOLE MODEL): the
    display and the monitoring panel on top; one fader section of 12 with the
    COMPACT's nine layer keys, and its key section with fader 13, below
    (layout::compact). */
class WingSurface final : public Skin
{
public:
    explicit WingSurface (UiContext&);
    ~WingSurface() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refreshAll() override;
    void refreshLayers() override { refreshSections(); }
    void selectionChanged() override;
    void sendsOnFaderChanged() override;
    void showChanged() override;
    void customControlsChanged() override;
    juce::Component& popupHost() override { return *display; }
    bool skinKeyPressed (const juce::KeyPress&) override;
    void setCovered (bool) override;

    /** The console model changed (ctx.consoleModel; `changed`: from another one):
        lays the surface out for it. */
    void consoleModelChanged (bool changed);

private:
    class LockOverlay;

    void saveScreenshot();
    /** Hold HOME (with the keys of `held`): locks or unlocks the surface (SurfaceLock). */
    void holdHomeLock (const juce::String& held);
    void refreshSections();
    void layoutTopRow();
    void layoutSections();
    /** The WING COMPACT's surface (both rows). */
    void layoutCompact();
    /** The display in `bounds`, or over the whole window in screen-only mode. */
    void placeDisplay (juce::Rectangle<int> bounds);
    bool isCompact() const noexcept { return ctx.consoleModel == ConsoleModel::Compact; }
    /** The top row's height: the splitter's share, a little more on wide
        windows (the screen gains most from it). */
    int topRowHeight() const;
    /** The fader strips' width and whether the custom controls fit beside
        them, for a bottom row `total` wide and `height` tall. */
    struct BottomRowPlan
    {
        int stripW = 40;
        bool withCc = true;
    };
    BottomRowPlan planBottomRow (int total, int height) const;
    /** The widths of the top row's sections beside the display unit. */
    struct TopRowSides
    {
        int monitor, strip, fourChannel;
        int total() const noexcept { return monitor + strip + fourChannel; }
    };
    static TopRowSides topRowSideWidths (int width);
    /** The console's columns: the whole window, or on very wide windows the
        width the sections use at their natural size, centred. */
    juce::Rectangle<int> consoleArea() const;
    /** Only the main display, as large as the window allows (F10), or back. */
    void setScreenMaximized (bool);
    /** Shows the parts that the editor and the screen-only mode leave. */
    void updateVisibility();
    /** The window takes the keyboard back (it handles the shortcuts). */
    void focusWindow();

    UiContext& ctx;
    bool screenMaximized = false;
    /** Screen only: the display's layout height; taller windows scale it up. */
    static constexpr float kMaxScreenOnlyLayoutHeight = 1100.0f;
    /** The narrowest the channel strip and the right-hand column (monitoring
        over the 4-Channel section) get when a narrow window's top row makes
        room for a legible screen. */
    static constexpr int kMinCompactStripWidth = 300, kMinCompactRightWidth = 220;

    std::unique_ptr<ChannelStripSection> channelStrip;
    std::unique_ptr<MainDisplay> display;
    std::unique_ptr<MonitorSection> monitor;
    std::unique_ptr<FourChannelSection> fourChannel;
    std::unique_ptr<FaderSection> sections[LayerSet::kNumSections];
    std::unique_ptr<CustomControlSection> customControls;
    std::unique_ptr<CompactSection> compactKeys; // the WING COMPACT's key section and fader 13
    std::unique_ptr<DawFaderSection> dawSections[LayerSet::kNumSections]; // the fader sections in DAW mode
    bool ccInBottomRow = true; // else docked under the monitoring panel
    std::unique_ptr<LockOverlay> lockOverlay;
    SurfaceLock surfaceLock;
    ResizerBar splitter { false };
    float topFraction = 0.52f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WingSurface)
};

} // namespace livedaw::ui
