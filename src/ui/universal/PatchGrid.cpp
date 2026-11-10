#include "ui/universal/PatchGrid.h"

namespace livedaw::ui::universal
{

/** The points, scrolled. */
class PatchGrid::Points final : public juce::Component
{
public:
    explicit Points (PatchGrid& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.theme;
        const auto clip = g.getClipBounds();
        const int r0 = juce::jmax (0, clip.getY() / kCell), r1 = juce::jmin (owner.rows, clip.getBottom() / kCell + 1);
        const int c0 = juce::jmax (0, clip.getX() / kCell), c1 = juce::jmin (owner.columns, clip.getRight() / kCell + 1);
        const auto cross = owner.crosshair;
        const auto range = owner.rangePoints (owner.dragFrom, owner.dragTo);
        for (int r = r0; r < r1; ++r)
        {
            // Every other row a shade apart, to follow a row across.
            g.setColour ((r % 2) == 0 ? t.panel : t.background);
            g.fillRect (c0 * kCell, r * kCell, (c1 - c0) * kCell, kCell);
        }
        // The crosshair: the pointer's row and column, tinted.
        if (cross.isValid())
        {
            g.setColour (t.accent.withAlpha (0.14f));
            g.fillRect (c0 * kCell, cross.row * kCell, (c1 - c0) * kCell, kCell);
            g.fillRect (cross.column * kCell, r0 * kCell, kCell, (r1 - r0) * kCell);
        }
        for (int r = r0; r < r1; ++r)
        {
            for (int c = c0; c < c1; ++c)
            {
                const auto cell = juce::Rectangle<int> (c * kCell, r * kCell, kCell, kCell).toFloat().reduced (8.0f);
                const auto s = owner.getState (r, c);
                if (s == On || s == Pair)
                {
                    g.setColour (s == On ? t.accent : t.accent.withAlpha (0.45f));
                    g.fillEllipse (cell);
                }
                else
                {
                    g.setColour (t.outline);
                    g.drawEllipse (cell.reduced (6.0f), 1.0f);
                }
            }
        }
        // A drag's range: what a release will patch.
        g.setColour (t.text);
        for (const auto& p : range)
            g.drawEllipse (juce::Rectangle<int> (p.column * kCell, p.row * kCell, kCell, kCell).toFloat().reduced (5.0f), 2.0f);
        // A line every 8 columns and rows, to count them.
        g.setColour (t.outline);
        for (int c = (c0 / 8 + 1) * 8; c < c1; c += 8)
            g.fillRect (c * kCell, r0 * kCell, 1, (r1 - r0) * kCell);
        for (int r = (r0 / 8 + 1) * 8; r < r1; r += 8)
            g.fillRect (c0 * kCell, r * kCell, (c1 - c0) * kCell, 1);
    }

    Point pointAt (juce::Point<int> p) const
    {
        const Point at { p.y / kCell, p.x / kCell };
        return p.x >= 0 && p.y >= 0 && at.row < owner.rows && at.column < owner.columns ? at : Point();
    }

    void mouseMove (const juce::MouseEvent& e) override { owner.setCrosshair (pointAt (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override { owner.setCrosshair ({}); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.setCrosshair (pointAt (e.getPosition()));
        // A mouse drag patches a range; a touch drag scrolls the points (the viewport's).
        owner.dragFrom = e.source.canHover() && ! owner.dragPans ? pointAt (e.getPosition()) : Point();
        owner.dragTo = {};
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! owner.dragFrom.isValid())
            return;
        // Past the grid's edge the range ends at the edge.
        const Point at { juce::jlimit (0, juce::jmax (0, owner.rows - 1), e.y / kCell), juce::jlimit (0, juce::jmax (0, owner.columns - 1), e.x / kCell) };
        owner.setCrosshair (at);
        if (at != owner.dragTo)
        {
            owner.dragTo = at;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto from = owner.dragFrom, to = owner.dragTo;
        owner.dragFrom = {};
        owner.dragTo = {};
        if (from.isValid() && to.isValid() && to != from)
            owner.patchRange (from, to);
        else if (! e.mouseWasDraggedSinceMouseDown())
            owner.tap (e.y / kCell, e.x / kCell);
        if (! e.source.canHover())
            owner.setCrosshair ({}); // a finger lifted
        repaint();
    }

private:
    PatchGrid& owner;
};

/** The viewport, which moves the labels with the points. */
class PatchGrid::ScrollView final : public juce::Viewport
{
public:
    explicit ScrollView (PatchGrid& o) : owner (o) {}
    void visibleAreaChanged (const juce::Rectangle<int>&) override { owner.repaint(); }

private:
    PatchGrid& owner;
};

void PatchGrid::setDragPans (bool pans)
{
    dragPans = pans;
    viewport->setScrollOnDragMode (pans ? juce::Viewport::ScrollOnDragMode::all : juce::Viewport::ScrollOnDragMode::nonHover);
}

PatchGrid::PatchGrid (const Theme& t) : theme (t), points (std::make_unique<Points> (*this)), viewport (std::make_unique<ScrollView> (*this))
{
    viewport->setViewedComponent (points.get(), false);
    viewport->setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    viewport->getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme.outline);
    viewport->getHorizontalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme.outline);
    addAndMakeVisible (*viewport);
}

PatchGrid::~PatchGrid() = default;

void PatchGrid::setModel (Model m)
{
    model = std::move (m);
    rows = columns = -1;
    refresh();
}

PatchGrid::State PatchGrid::getState (int row, int column) const noexcept
{
    if (! juce::isPositiveAndBelow (row, rows) || ! juce::isPositiveAndBelow (column, columns))
        return Off;
    return static_cast<State> (states[(size_t) (row * columns + column)]);
}

void PatchGrid::refresh()
{
    const int r = model.numRows ? juce::jmax (0, model.numRows()) : 0;
    const int c = model.numColumns ? juce::jmax (0, model.numColumns()) : 0;
    if (r != rows || c != columns)
    {
        rows = r;
        columns = c;
        rowNames.clear();
        columnNames.clear();
        for (int i = 0; i < rows; ++i)
            rowNames.push_back (model.rowName ? model.rowName (i) : juce::String (i + 1));
        for (int i = 0; i < columns; ++i)
            columnNames.push_back (model.columnName ? model.columnName (i) : juce::String (i + 1));
        states.assign ((size_t) (rows * columns), Off);
        rowLevels.assign ((size_t) rows, 0.0f);
        columnLevels.assign ((size_t) columns, 0.0f);
        crosshair = dragFrom = dragTo = {};
        points->setSize (columns * kCell, rows * kCell);
        repaint();
    }
    bool changed = false;
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < columns; ++j)
        {
            const auto s = model.state ? model.state (i, j) : Off;
            auto& old = states[(size_t) (i * columns + j)];
            if (old != s)
            {
                old = s;
                changed = true;
            }
        }
    if (changed)
        points->repaint();
}

void PatchGrid::tap (int row, int column)
{
    if (juce::isPositiveAndBelow (row, rows) && juce::isPositiveAndBelow (column, columns) && model.toggle)
    {
        model.toggle (row, column);
        refresh();
    }
}

void PatchGrid::setCrosshair (Point p)
{
    if (! juce::isPositiveAndBelow (p.row, rows) || ! juce::isPositiveAndBelow (p.column, columns))
        p = {};
    if (p == crosshair)
        return;
    crosshair = p;
    points->repaint();
    repaint(); // its row's and column's labels
}

std::vector<PatchGrid::Point> PatchGrid::rangePoints (Point from, Point to) const
{
    std::vector<Point> range;
    if (! from.isValid() || ! to.isValid() || from == to)
        return range;
    const int dr = to.row - from.row, dc = to.column - from.column;
    const int n = juce::jmax (std::abs (dr), std::abs (dc)) + 1;
    for (int i = 0; i < n; ++i)
    {
        const Point p { from.row + (dr < 0 ? -i : i), from.column + (dc < 0 ? -i : i) };
        if (! juce::isPositiveAndBelow (p.row, rows) || ! juce::isPositiveAndBelow (p.column, columns))
            break;
        range.push_back (p);
    }
    return range;
}

void PatchGrid::patchRange (Point from, Point to)
{
    if (! model.toggle)
        return;
    for (const auto& p : rangePoints (from, to))
        if (model.state == nullptr || model.state (p.row, p.column) != On)
            model.toggle (p.row, p.column);
    refresh();
}

void PatchGrid::refreshLevels()
{
    bool changed = false;
    const auto take = [&changed] (std::vector<float>& levels, const std::function<float (int)>& level)
    {
        for (size_t i = 0; i < levels.size(); ++i)
        {
            const float now = level ? juce::jlimit (0.0f, 1.0f, level ((int) i)) : 0.0f;
            if (std::abs (now - levels[i]) > 0.01f)
            {
                levels[i] = now;
                changed = true;
            }
        }
    };
    take (rowLevels, model.rowLevel);
    take (columnLevels, model.columnLevel);
    if (changed)
        repaint(); // the labels' meters (the points are another component)
}

void PatchGrid::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    const auto view = viewport->getViewPosition();
    const auto area = viewport->getBounds();

    // The column labels, turned, over their columns.
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (area.getX(), 0, area.getWidth(), kColumnLabel);
        g.setFont (type::font (type::kCaption, true));
        for (int c = view.x / kCell; c < juce::jmin (columns, (view.x + area.getWidth()) / kCell + 1); ++c)
        {
            const float x = (float) (area.getX() + c * kCell - view.x);
            if (model.columnLevel)
            {
                // The column's level, up its left edge.
                const auto well = juce::Rectangle<float> (x + 3.0f, 6.0f, 4.0f, (float) kColumnLabel - 10.0f);
                g.setColour (theme.well);
                g.fillRect (well);
                g.setColour (theme.meterLow);
                g.fillRect (well.withTop (well.getBottom() - well.getHeight() * getColumnLevel (c)));
            }
            g.setColour (c == crosshair.column ? theme.accent : theme.textDim);
            juce::Graphics::ScopedSaveState turned (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, x + kCell * 0.5f, (float) kColumnLabel));
            g.drawText (columnNames[(size_t) c], juce::Rectangle<float> (x + kCell * 0.5f + 4.0f, (float) kColumnLabel - kCell * 0.5f, (float) kColumnLabel - 8.0f, (float) kCell),
                        juce::Justification::centredLeft);
        }
    }
    // The row labels, beside their rows.
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (0, area.getY(), kRowLabel, area.getHeight());
        g.setFont (type::font (type::kLabel, true));
        for (int r = view.y / kCell; r < juce::jmin (rows, (view.y + area.getHeight()) / kCell + 1); ++r)
        {
            const int y = area.getY() + r * kCell - view.y;
            g.setColour ((r % 2) == 0 ? theme.panel : theme.background);
            g.fillRect (0, y, kRowLabel, kCell);
            if (r == crosshair.row)
            {
                g.setColour (theme.accent.withAlpha (0.14f));
                g.fillRect (0, y, kRowLabel, kCell);
            }
            if (model.rowLevel)
            {
                // The row's level, along the bottom of its label.
                const auto well = juce::Rectangle<float> ((float) sizes::kGap, (float) (y + kCell - 7), (float) (kRowLabel - sizes::kGap * 2), 4.0f);
                g.setColour (theme.well);
                g.fillRect (well);
                g.setColour (theme.meterLow);
                g.fillRect (well.withWidth (well.getWidth() * getRowLevel (r)));
            }
            g.setColour (r == crosshair.row ? theme.accent : theme.text);
            g.drawFittedText (rowNames[(size_t) r], juce::Rectangle<int> (sizes::kGap, y, kRowLabel - sizes::kGap * 2, model.rowLevel ? kCell - 6 : kCell),
                              juce::Justification::centredLeft, 1, 0.8f);
        }
    }
    if (rows == 0 || columns == 0)
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel));
        g.drawText ("Nothing to patch here", area, juce::Justification::centred);
    }
}

void PatchGrid::resized()
{
    viewport->setBounds (getLocalBounds().withTrimmedLeft (kRowLabel).withTrimmedTop (kColumnLabel));
}

} // namespace livedaw::ui::universal
