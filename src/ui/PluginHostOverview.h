#pragma once

#include "ui/PluginHostRack.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <memory>
#include <vector>

namespace livedaw::ui::ph
{

/** The OVERVIEW page of the PLUGIN HOST's screen (docs/PLUGIN_HOST.md, page 2): every channel (16, 32 or 64) as a
    column of small tiles, a glance at all the racks.

    A column has the channel's number and name at the top with its BYPASS state, then a tile for each block of its
    chain in signal order: the block's name, its latency, and BYP (bypassed), FAULT or NOT FOUND where it applies.
    A bypassed block and a bypassed channel are dim. A column holds up to 7 tiles: a longer chain shows 6 and a
    "+N MORE" tile. A tap on a tile selects its channel and that plug-in and jumps to the RACK page; a tap on the
    column's head, or on "+N MORE", selects the channel. 16 channels fit the screen; 32 and 64 scroll sideways.

    Tiles are 60 px high and columns 110 px wide (touch sized in the 1920 x 1080 design space). Message thread only. */
class OverviewPage final : public juce::Component, private juce::Timer
{
public:
    OverviewPage (UiContext&, const universal::Theme&);
    ~OverviewPage() override;

    /** A tile or a head was tapped: the surface selects the channel (0-based) and block `uid` (-1: the channel only) and shows RACK. */
    std::function<void (int channel, int uid)> onJump;
    /** The block selected on the RACK page for the selected channel (the tile is outlined), or -1. */
    std::function<int()> getSelectedUid;

    struct Column
    {
        int channel = 0; // 0-based
        juce::String name;
        bool bypassed = false;
        int latency = 0; // the plug-ins' latency, samples
        std::vector<RackPage::TileInfo> tiles; // every block, in order
    };
    /** Reads the racks again (the timer does this too). */
    void refresh();
    int getNumColumns() const noexcept { return (int) columns.size(); }
    const Column& getColumn (int index) const { return columns[(size_t) index]; }
    /** How many tiles the column draws (a long chain: 6 and the "+N MORE" tile). */
    static int shownTiles (int blocks) noexcept { return blocks <= kMaxTiles ? blocks : kMaxTiles - 1; }
    static bool hasMore (int blocks) noexcept { return blocks > kMaxTiles; }

    /** Places on the canvas (tests, screenshots); a tile index of shownTiles() is the "+N MORE" tile. */
    juce::Rectangle<int> getHeadBounds (int column) const;
    juce::Rectangle<int> getTileBounds (int column, int tile) const;
    /** The taps. */
    void tapHead (int column);
    void tapTile (int column, int tile);

    juce::Component& getCanvas() noexcept;
    juce::Viewport& getViewport() noexcept { return viewport; }

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kColumnWidth = 110, kColumnGap = 4, kMargin = 8, kHeadHeight = 76, kTileHeight = 60, kTileGap = 4, kMaxTiles = 7;

private:
    class Canvas;
    friend class Canvas;
    void timerCallback() override { refresh(); }
    int columnX (int column) const noexcept { return kMargin + column * (kColumnWidth + kColumnGap); }
    int selectedChannel() const;

    UiContext& ctx;
    const universal::Theme& theme;
    std::vector<Column> columns;
    juce::String signature;
    juce::Viewport viewport;
    std::unique_ptr<Canvas> canvas;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OverviewPage)
};

} // namespace livedaw::ui::ph
