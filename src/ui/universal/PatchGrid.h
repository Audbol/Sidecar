#pragma once

#include "ui/universal/Theme.h"

#include <functional>
#include <memory>
#include <vector>

namespace livedaw::ui::universal
{

/** A crosspoint matrix: destinations down the side, sources along the top; a
    lit point is a patch, and a tap patches it (or takes the patch away). The
    row and column labels stay in view as the points scroll.

    The point under the pointer (or the finger, while it is down) has a
    crosshair: its row and its column are tinted and their labels lit, so a
    point far from the labels can be read. A drag with the mouse from a point
    patches a range 1:1: the next row from the next column and so on, as far as
    the drag goes (rings show what a release will patch); a touch drag scrolls
    the points as before. A row or a column can show a level beside its label
    (Model::rowLevel, columnLevel). */
class PatchGrid final : public juce::Component
{
public:
    /** A drag moves around the grid, with the mouse too, instead of patching a range (a dLive's routing page). */
    void setDragPans (bool);
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
        /** A row's or a column's level, 0 to 1 on the fader's scale (not set: no meter). */
        std::function<float (int row)> rowLevel;
        std::function<float (int column)> columnLevel;
    };
    void setModel (Model);

    /** Takes the points again (and the rows and columns when their number changed). */
    void refresh();
    /** As refresh, with the row and column names read again (for rows that changed but not in number). */
    void relabel();
    /** As a tap on point (row, column) does. */
    void tap (int row, int column);
    State getState (int row, int column) const noexcept;
    int getNumRows() const noexcept { return rows; }
    int getNumColumns() const noexcept { return columns; }

    /** A point of the grid; row -1: none. */
    struct Point
    {
        int row = -1, column = -1;
        bool operator== (const Point& o) const noexcept { return row == o.row && column == o.column; }
        bool operator!= (const Point& o) const noexcept { return ! (*this == o); }
        bool isValid() const noexcept { return row >= 0 && column >= 0; }
    };
    /** The crosshair's point (as the pointer over the points sets it). */
    void setCrosshair (Point);
    Point getCrosshair() const noexcept { return crosshair; }
    /** The points a drag from `from` to `to` patches: 1:1 from `from`, a row and a column further each,
        as many as the drag is long (the longer of its two ways), towards `to`, inside the grid. */
    std::vector<Point> rangePoints (Point from, Point to) const;
    /** As a drag with the mouse from `from` to `to` does on release: patches rangePoints (those not patched yet). */
    void patchRange (Point from, Point to);
    /** Takes the rows' and columns' levels again (the owner's timer calls it). */
    void refreshLevels();
    float getRowLevel (int row) const noexcept { return juce::isPositiveAndBelow (row, (int) rowLevels.size()) ? rowLevels[(size_t) row] : 0.0f; }
    float getColumnLevel (int column) const noexcept { return juce::isPositiveAndBelow (column, (int) columnLevels.size()) ? columnLevels[(size_t) column] : 0.0f; }

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
    std::vector<float> rowLevels, columnLevels;
    Point crosshair, dragFrom, dragTo;
    bool dragPans = false;
    std::unique_ptr<Points> points;
    std::unique_ptr<ScrollView> viewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchGrid)
};

} // namespace livedaw::ui::universal
