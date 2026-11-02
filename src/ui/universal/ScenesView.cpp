#include "ui/universal/ScenesView.h"

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kRow = 44;

    juce::String kindName (Show::ItemKind k)
    {
        switch (k)
        {
            case Show::ItemKind::Snippet: return "SNIPPET";
            case Show::ItemKind::Clip:    return "CLIP";
            case Show::ItemKind::Fx:      return "FX PRESET";
            case Show::ItemKind::Channel: return "CHANNEL PRESET";
            case Show::ItemKind::Snapshot: break;
        }
        return {};
    }
} // namespace

/** The scenes, drawn; a tap selects one. */
class ScenesView::List final : public juce::Component
{
public:
    explicit List (ScenesView& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.theme;
        for (int i = 0; i < (int) owner.rows.size(); ++i)
        {
            const auto& row = owner.rows[(size_t) i];
            const auto r = juce::Rectangle<int> (0, i * (kRow + sizes::kGapSmall), getWidth(), kRow).toFloat();
            const bool sel = i == owner.selected;
            g.setColour (sel ? t.raised.brighter (0.1f) : t.raised);
            g.fillRoundedRectangle (r, sizes::kRadius);
            g.setColour (sel ? t.accent : t.outline);
            g.drawRoundedRectangle (r.reduced (0.5f), sizes::kRadius, sel ? 2.0f : 1.0f);
            auto inner = r.reduced (12.0f, 0.0f);
            // The current scene (the last recalled) is marked.
            if (row.current)
            {
                g.setColour (t.meterLow);
                g.fillEllipse (inner.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 10.0f));
            }
            else
            {
                inner.removeFromLeft (10.0f);
            }
            inner.removeFromLeft (10.0f);
            g.setColour (t.textDim);
            g.setFont (type::font (type::kCaption, true));
            g.drawText (row.flags, inner, juce::Justification::centredRight);
            g.setColour (t.text);
            g.setFont (type::font (type::kLabel, true));
            g.drawText (row.text, inner, juce::Justification::centredLeft);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasDraggedSinceMouseDown())
            owner.selectScene (e.y / (kRow + sizes::kGapSmall));
    }

private:
    ScenesView& owner;
};

ScenesView::ScenesView (UiContext& c, const Theme& t)
    : goKey ("GO", t.accent, t), prevKey ("PREV", t.accent, t), nextKey ("NEXT", t.accent, t), addKey ("ADD", t.accent, t),
      storeKey ("STORE", t.accent, t), renameKey ("RENAME", t.accent, t), upKey ("UP", t.accent, t), downKey ("DOWN", t.accent, t),
      removeKey ("REMOVE", t.mute, t), ctx (c), theme (t), list (std::make_unique<List> (*this))
{
    goKey.onClick = [this] { go(); };
    prevKey.onClick = [this]
    {
        logRecall (ctx, "the previous scene", ctx.show.recallPrevious());
        selectScene (ctx.show.getCurrentSceneIndex());
    };
    nextKey.onClick = [this]
    {
        logRecall (ctx, "the next scene", ctx.show.recallNext());
        selectScene (ctx.show.getCurrentSceneIndex());
    };
    addKey.onClick = [this] { add(); };
    storeKey.onClick = [this]
    {
        if (selected >= 0)
            confirm ("STORE", "Store the console into scene " + juce::String (selected + 1) + "? What it holds now is replaced.", [this] { store(); });
    };
    renameKey.onClick = [this]
    {
        if (selected < 0)
            return;
        auto* w = new juce::AlertWindow ("RENAME", "Scene " + juce::String (selected + 1) + ":", juce::MessageBoxIconType::NoIcon, this);
        const auto scene = ctx.show.getScene (selected);
        w->addTextEditor ("name", scene ? scene->name : juce::String());
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ScenesView> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
        {
            if (r != 0 && safe != nullptr)
                safe->rename (w->getTextEditorContents ("name").trim());
        }), true);
    };
    upKey.onClick = [this] { move (-1); };
    downKey.onClick = [this] { move (1); };
    removeKey.onClick = [this]
    {
        if (selected >= 0)
            confirm ("REMOVE", "Remove scene " + juce::String (selected + 1) + " from the show?", [this] { remove(); });
    };

    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme.outline);
    addAndMakeVisible (viewport);
    for (auto* k : { &goKey, &prevKey, &nextKey, &addKey, &storeKey, &renameKey, &upKey, &downKey, &removeKey })
        addAndMakeVisible (k);
    refresh();
    startTimerHz (5);
}

ScenesView::~ScenesView()
{
    stopTimer();
}

void ScenesView::confirm (const juce::String& title, const juce::String& message, std::function<void()> then)
{
    juce::Component::SafePointer<ScenesView> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withTitle (title)
                                      .withMessage (message)
                                      .withButton ("OK")
                                      .withButton ("CANCEL")
                                      .withAssociatedComponent (this),
                                  [safe, then] (int result)
                                  {
                                      if (safe != nullptr && result == 1)
                                          then();
                                  });
}

void ScenesView::selectScene (int index)
{
    const int s = juce::isPositiveAndBelow (index, (int) rows.size()) ? index : -1;
    if (s != selected)
    {
        selected = s;
        list->repaint();
    }
    for (auto* k : { &goKey, &storeKey, &renameKey, &upKey, &downKey, &removeKey })
        k->setEnabled (selected >= 0);
}

void ScenesView::go()
{
    if (selected < 0)
        return;
    logRecall (ctx, "scene " + juce::String (selected + 1), ctx.show.recallScene (selected));
    list->repaint();
}

int ScenesView::add()
{
    const int at = selected >= 0 ? selected + 1 : -1;
    const int index = ctx.show.addScene ("Scene " + juce::String (ctx.show.getNumScenes() + 1), at);
    refresh();
    selectScene (index);
    return index;
}

void ScenesView::store()
{
    if (selected >= 0)
        ctx.show.storeScene (selected);
    refresh();
}

void ScenesView::remove()
{
    if (selected < 0)
        return;
    ctx.show.removeScene (selected);
    refresh();
    selectScene (juce::jmin (selected, (int) rows.size() - 1));
}

void ScenesView::move (int delta)
{
    const int to = selected + delta;
    if (selected < 0 || ! juce::isPositiveAndBelow (to, (int) rows.size()))
        return;
    ctx.show.moveScene (selected, to);
    refresh();
    selectScene (to);
}

void ScenesView::rename (const juce::String& name)
{
    if (selected >= 0 && name.isNotEmpty())
        ctx.show.setSceneName (selected, name);
    refresh();
}

void ScenesView::refresh()
{
    std::vector<Row> now;
    const int current = ctx.show.getCurrentSceneIndex();
    for (int i = 0; i < ctx.show.getNumScenes(); ++i)
    {
        Row row;
        if (const auto scene = ctx.show.getScene (i))
        {
            row.text = juce::String (i + 1) + "   " + scene->name;
            juce::StringArray marks;
            if (const auto kind = kindName (scene->item); kind.isNotEmpty())
                marks.add (kind);
            if (scene->tag > 0)
                marks.add ("#" + juce::String (scene->tag));
            if (scene->linkNext)
                marks.add ("LINK");
            if (scene->skip)
                marks.add ("SKIP");
            row.flags = marks.joinIntoString ("  ");
        }
        row.current = i == current;
        now.push_back (row);
    }
    bool changed = now.size() != rows.size();
    for (size_t i = 0; ! changed && i < now.size(); ++i)
        changed = now[i] != rows[i];
    if (changed)
    {
        rows = std::move (now);
        list->setSize (viewport.getWidth() - viewport.getScrollBarThickness(), (int) rows.size() * (kRow + sizes::kGapSmall));
        list->repaint();
        if (selected >= (int) rows.size())
            selectScene ((int) rows.size() - 1);
    }
    for (auto* k : { &goKey, &storeKey, &renameKey, &upKey, &downKey, &removeKey })
        k->setEnabled (selected >= 0);
}

void ScenesView::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    if (rows.empty())
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel));
        g.drawText ("No scenes yet: ADD captures the console as one", viewport.getBounds(), juce::Justification::centred);
    }
}

void ScenesView::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap, sizes::kGapSmall);
    auto keys = area.removeFromTop (sizes::kTouch);
    const int w = juce::jmin (120, (keys.getWidth() - 8 * sizes::kGapSmall) / 9);
    for (auto* k : { &goKey, &prevKey, &nextKey, &addKey, &storeKey, &renameKey, &upKey, &downKey, &removeKey })
    {
        k->setBounds (keys.removeFromLeft (w));
        keys.removeFromLeft (sizes::kGapSmall);
    }
    area.removeFromTop (sizes::kGap);
    viewport.setBounds (area);
    list->setSize (viewport.getWidth() - viewport.getScrollBarThickness(), (int) rows.size() * (kRow + sizes::kGapSmall));
}

} // namespace livedaw::ui::universal
