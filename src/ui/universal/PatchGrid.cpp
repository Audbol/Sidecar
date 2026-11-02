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
        for (int r = r0; r < r1; ++r)
        {
            // Every other row a shade apart, to follow a row across.
            g.setColour ((r % 2) == 0 ? t.panel : t.background);
            g.fillRect (c0 * kCell, r * kCell, (c1 - c0) * kCell, kCell);
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
        // A line every 8 columns and rows, to count them.
        g.setColour (t.outline);
        for (int c = (c0 / 8 + 1) * 8; c < c1; c += 8)
            g.fillRect (c * kCell, r0 * kCell, 1, (r1 - r0) * kCell);
        for (int r = (r0 / 8 + 1) * 8; r < r1; r += 8)
            g.fillRect (c0 * kCell, r * kCell, (c1 - c0) * kCell, 1);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasDraggedSinceMouseDown())
            owner.tap (e.y / kCell, e.x / kCell);
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
            g.setColour (theme.textDim);
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
            g.setColour (theme.text);
            g.drawFittedText (rowNames[(size_t) r], juce::Rectangle<int> (sizes::kGap, y, kRowLabel - sizes::kGap * 2, kCell), juce::Justification::centredLeft, 1, 0.8f);
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
