#pragma once

#include "ui/universal/Theme.h"

#include <functional>
#include <memory>
#include <vector>

namespace livedaw::ui::universal
{

/** A crosspoint matrix: destinations down the side, sources along the top; a
    lit point is a patch, and a tap patches it (or takes the patch away). The
    row and column labels stay in view as the points scroll. */
class PatchGrid final : public juce::Component
{
public:
    explicit PatchGrid (const Theme&);
    ~PatchGrid() override;

    /** A point's state: not patched, patched, or patched as the right side of a
        stereo pair (drawn dimmer). */
    enum State : std::uint8_t
    {
        Off = 0,
        On = 1,
        Pair = 2
    };

    /** What the grid shows and does. */
    struct Model
    {
        std::function<int()> numRows, numColumns;
        std::function<juce::String (int row)> rowName;
        std::function<juce::String (int column)> columnName;
        std::function<State (int row, int column)> state;
        std::function<void (int row, int column)> toggle;
    };
    void setModel (Model);

    /** Takes the points again (and the rows and columns when their number changed). */
    void refresh();
    /** As a tap on point (row, column) does. */
    void tap (int row, int column);
    State getState (int row, int column) const noexcept;
    int getNumRows() const noexcept { return rows; }
    int getNumColumns() const noexcept { return columns; }

    static constexpr int kCell = 40, kRowLabel = 150, kColumnLabel = 72;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Points;
    class ScrollView;

    const Theme& theme;
    Model model;
    int rows = 0, columns = 0;
    std::vector<std::uint8_t> states;
    std::vector<juce::String> rowNames, columnNames;
    std::unique_ptr<Points> points;
    std::unique_ptr<ScrollView> viewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchGrid)
};

} // namespace livedaw::ui::universal
