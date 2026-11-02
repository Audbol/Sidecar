#pragma once

#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <optional>
#include <vector>

namespace livedaw::ui::universal
{

/** A strip's chain as a list, in processing order: its blocks (the WING
    template's and extra ones), its user taps, and PRE-FADER, FADER and POST
    as markers (the RTA points and the WING's own taps aren't shown). A tap
    selects a row; extra blocks and user taps can be dragged to another place
    and removed; ADD puts a block or a tap after the selected row. */
class ChainList final : public juce::Component
{
public:
    ChainList (UiContext&, const Theme&);
    ~ChainList() override;

    void setStrip (Strip*);
    /** Follows the chain (another strip, a change made elsewhere) and the blocks' ON. */
    void refresh();

    struct Row
    {
        ChainStep step;
        /** Its place in the strip's chain (Strip::getChain). */
        int stepIndex = 0;
        juce::String name;
        /** An extra block or a user tap: it can be moved and removed. */
        bool movable = false;
        /** PRE-FADER, FADER, POST: a marker, not a block. */
        bool marker = false;
    };
    const std::vector<Row>& getRows() const noexcept { return rows; }
    int getSelectedRow() const noexcept { return selected; }
    void selectRow (int row);
    /** Called when the selection moves to another row (or none). */
    std::function<void()> onSelectionChanged;

    /** Adds a block of `kind`, or a user tap without one, after the selected
        row (before PRE-FADER with none) and selects it; false if the strip has
        no room. */
    bool add (std::optional<BlockKind> kind);
    /** Moves movable row `row` to before row `before` (the end: getRows().size()). */
    bool moveRow (int row, int before);
    bool removeRow (int row);

    ToggleKey addKey, removeKey;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Rows;
    void rebuild (const std::vector<ChainStep>& chain);
    static bool sameChain (const std::vector<ChainStep>&, const std::vector<ChainStep>&);
    bool isOn (const ChainStep&) const;
    void selectStep (std::function<bool (const ChainStep&)> match);

    UiContext& ctx;
    const Theme& theme;
    Strip* strip = nullptr;
    std::vector<ChainStep> shownChain;
    std::vector<Row> rows;
    std::vector<bool> lit;
    int selected = -1;
    std::unique_ptr<Rows> rowsView;
    juce::Viewport viewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainList)
};

/** A step's name in the chain list ("EQ", "INSERT 1", "TAP 3", "EQ BLOCK"). */
juce::String stepName (const ChainStep&);

} // namespace livedaw::ui::universal
