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

/** The selected scene's snippets, drawn; a tap selects one. */
class ScenesView::SnippetList final : public juce::Component
{
public:
    explicit SnippetList (ScenesView& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.theme;
        const int scene = owner.selected;
        const int n = owner.getNumSnippets();
        if (n == 0)
        {
            g.setColour (t.textDim);
            g.setFont (type::font (type::kLabel));
            g.drawFittedText (scene < 0 ? "Select a scene" : "No snippets: NEW makes one of the selected strip", getLocalBounds().reduced (8),
                              juce::Justification::centredTop, 2, 0.8f);
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            const auto s = owner.ctx.show.getSnippet (scene, i);
            const auto r = juce::Rectangle<int> (0, i * (kRow + sizes::kGapSmall), getWidth(), kRow).toFloat();
            const bool sel = i == owner.selectedSnippet;
            g.setColour (sel ? t.raised.brighter (0.1f) : t.raised);
            g.fillRoundedRectangle (r, sizes::kRadius);
            g.setColour (sel ? t.accent : t.outline);
            g.drawRoundedRectangle (r.reduced (0.5f), sizes::kRadius, sel ? 2.0f : 1.0f);
            auto inner = r.reduced (12.0f, 0.0f);
            if (! s.has_value())
                continue;
            g.setColour (t.textDim);
            g.setFont (type::font (type::kCaption, true));
            const auto strips = juce::String ((int) s->strips.size()) + ((int) s->strips.size() == 1 ? " STRIP" : " STRIPS");
            g.drawText (strips + (s->fireWithScene ? "  FIRES" : ""), inner, juce::Justification::centredRight);
            g.setColour (t.text);
            g.setFont (type::font (type::kLabel, true));
            g.drawText (s->name, inner, juce::Justification::centredLeft);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasDraggedSinceMouseDown())
            owner.selectSnippet (e.y / (kRow + sizes::kGapSmall));
    }

private:
    ScenesView& owner;
};

ScenesView::ScenesView (UiContext& c, const Theme& t)
    : goKey ("GO", t.accent, t), prevKey ("PREV", t.accent, t), nextKey ("NEXT", t.accent, t), addKey ("ADD", t.accent, t),
      storeKey ("STORE", t.accent, t), renameKey ("RENAME", t.accent, t), upKey ("UP", t.accent, t), downKey ("DOWN", t.accent, t),
      removeKey ("REMOVE", t.mute, t), importKey ("IMPORT .scn", t.accent, t), exportKey ("EXPORT .scn", t.accent, t),
      snippetNew ("NEW", t.accent, t), snippetGo ("GO", t.accent, t), snippetStore ("STORE", t.accent, t), snippetRename ("RENAME", t.accent, t),
      snippetFire ("FIRE WITH SCENE", t.accent, t), snippetRemove ("REMOVE", t.mute, t), snippetAddStrip ("+ STRIP", t.accent, t),
      snippetRemoveStrip ("- STRIP", t.accent, t), ctx (c), theme (t), list (std::make_unique<List> (*this)),
      snippetList (std::make_unique<SnippetList> (*this))
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

    auto said = [safe = juce::Component::SafePointer<ScenesView> (this)] (const juce::String& status)
    {
        if (safe == nullptr)
            return;
        safe->fileStatus = status;
        safe->refresh();
        safe->selectScene (safe->ctx.show.getNumScenes() - 1);
        safe->repaint();
    };
    importKey.onClick = [this, said] { sceneFiles.importScene (said); };
    exportKey.onClick = [this, said] { sceneFiles.exportScene (said); };
    importKey.setTooltip ("Import an X32 scene file as a new scene");
    exportKey.setTooltip ("Export the console as an X32 scene file");
    addChildComponent (importKey);
    addChildComponent (exportKey);

    // The selected scene's snippets.
    snippetNew.onClick = [this] { newSnippet(); };
    snippetGo.onClick = [this] { recallSnippet(); };
    snippetStore.onClick = [this]
    {
        if (selectedSnippet >= 0)
            confirm ("STORE", "Store the console into this snippet? What it holds now is replaced.", [this] { storeSnippet(); });
    };
    snippetRename.onClick = [this]
    {
        const auto s = selected >= 0 && selectedSnippet >= 0 ? ctx.show.getSnippet (selected, selectedSnippet) : std::nullopt;
        if (! s.has_value())
            return;
        auto* w = new juce::AlertWindow ("RENAME", "Snippet:", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", s->name);
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ScenesView> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
        {
            if (r != 0 && safe != nullptr)
                safe->renameSnippet (w->getTextEditorContents ("name").trim());
        }), true);
    };
    snippetFire.isOn = [this]
    {
        const auto s = selected >= 0 && selectedSnippet >= 0 ? ctx.show.getSnippet (selected, selectedSnippet) : std::nullopt;
        return s.has_value() && s->fireWithScene;
    };
    snippetFire.onClick = [this] { toggleFireWithScene(); };
    snippetRemove.onClick = [this]
    {
        if (selectedSnippet >= 0)
            confirm ("REMOVE", "Remove this snippet from its scene?", [this] { removeSnippet(); });
    };
    // Its scope: the selected strip in or out, and each section.
    snippetAddStrip.onClick = [this] { addSelectedStrip(); };
    snippetRemoveStrip.onClick = [this] { removeSelectedStrip(); };
    snippetAddStrip.setTooltip ("Capture the selected strip into the snippet, in its sections");
    snippetRemoveStrip.setTooltip ("Drop the selected strip from the snippet");
    const std::pair<juce::uint32, const char*> sections[] = { { state::Name, "NAME" },       { state::Routing, "ROUTING" }, { state::Preamp, "INPUT" },
                                                              { state::Gate, "GATE" },       { state::Eq, "EQ" },           { state::Dynamics, "DYNAMICS" },
                                                              { state::Inserts, "INSERTS" }, { state::Sends, "SENDS" },     { state::Fader, "FADER" },
                                                              { state::Groups, "GROUPS" } };
    for (auto [bit, name] : sections)
    {
        auto key = std::make_unique<ToggleKey> (name, t.accent, t);
        key->isOn = [this, b = bit] { return (getSnippetSections() & b) != 0; };
        key->onClick = [this, b = bit] { toggleSection (b); };
        addAndMakeVisible (*key);
        sectionKeys.push_back (std::move (key));
    }
    for (auto* k : { &snippetNew, &snippetGo, &snippetStore, &snippetRename, &snippetFire, &snippetRemove, &snippetAddStrip, &snippetRemoveStrip })
        addAndMakeVisible (k);
    addAndMakeVisible (*snippetList);

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
    const bool another = s != selected;
    if (another)
    {
        selected = s;
        list->repaint();
    }
    for (auto* k : { &goKey, &storeKey, &renameKey, &upKey, &downKey, &removeKey })
        k->setEnabled (selected >= 0);
    if (another)
    {
        selectSnippet (-1); // another scene's snippets
        snippetsShown = {};
        repaint (snippetPanel);
    }
}

int ScenesView::getNumSnippets() const
{
    const auto scene = selected >= 0 ? ctx.show.getScene (selected) : std::nullopt;
    return scene.has_value() ? scene->numSnippets : 0;
}

void ScenesView::selectSnippet (int index)
{
    selectedSnippet = juce::isPositiveAndBelow (index, getNumSnippets()) ? index : -1;
    for (auto* k : { &snippetGo, &snippetStore, &snippetRename, &snippetFire, &snippetRemove })
        k->setEnabled (selectedSnippet >= 0);
    snippetNew.setEnabled (selected >= 0);
    snippetFire.refresh();
    const bool strip = ctx.getSelectedStrip && ctx.getSelectedStrip() != nullptr;
    snippetAddStrip.setEnabled (selectedSnippet >= 0 && strip);
    snippetRemoveStrip.setEnabled (selectedSnippet >= 0 && strip);
    for (auto& k : sectionKeys)
    {
        k->setEnabled (selectedSnippet >= 0);
        k->refresh();
    }
    snippetList->repaint();
}

int ScenesView::newSnippet()
{
    auto* strip = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    if (selected < 0 || strip == nullptr)
        return -1;
    const auto name = strip->getName().isNotEmpty() ? strip->getName() : strip->getLabel();
    const int index = ctx.show.addSnippet (selected, name, state::AllSections, { state::StripRef::of (*strip) });
    refresh();
    selectSnippet (index);
    return index;
}

void ScenesView::recallSnippet()
{
    if (selected < 0 || selectedSnippet < 0)
        return;
    logRecall (ctx, "snippet " + juce::String (selectedSnippet + 1) + " of scene " + juce::String (selected + 1),
               ctx.show.recallSnippet (selected, selectedSnippet));
}

void ScenesView::storeSnippet()
{
    if (selected >= 0 && selectedSnippet >= 0)
        ctx.show.storeSnippet (selected, selectedSnippet);
    snippetList->repaint();
}

void ScenesView::removeSnippet()
{
    if (selected < 0 || selectedSnippet < 0)
        return;
    ctx.show.removeSnippet (selected, selectedSnippet);
    refresh();
    selectSnippet (juce::jmin (selectedSnippet, getNumSnippets() - 1));
}

void ScenesView::renameSnippet (const juce::String& name)
{
    if (selected >= 0 && selectedSnippet >= 0 && name.isNotEmpty())
        ctx.show.setSnippetName (selected, selectedSnippet, name);
    snippetList->repaint();
}

void ScenesView::toggleFireWithScene()
{
    const auto s = selected >= 0 && selectedSnippet >= 0 ? ctx.show.getSnippet (selected, selectedSnippet) : std::nullopt;
    if (! s.has_value())
        return;
    ctx.show.setSnippetFireWithScene (selected, selectedSnippet, ! s->fireWithScene);
    snippetFire.refresh();
    snippetList->repaint();
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

juce::uint32 ScenesView::getSnippetSections() const
{
    const auto s = selected >= 0 && selectedSnippet >= 0 ? ctx.show.getSnippet (selected, selectedSnippet) : std::nullopt;
    return s.has_value() ? s->sections : 0u;
}

void ScenesView::addSelectedStrip()
{
    auto* strip = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    if (strip == nullptr || selected < 0 || selectedSnippet < 0)
        return;
    const auto sections = getSnippetSections();
    ctx.show.addSnippetItems (selected, selectedSnippet, state::Scope::only (sections != 0 ? sections : (juce::uint32) state::AllSections,
                                                                              { state::StripRef::of (*strip) }));
    refresh();
}

void ScenesView::removeSelectedStrip()
{
    auto* strip = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    if (strip == nullptr || selected < 0 || selectedSnippet < 0)
        return;
    ctx.show.removeSnippetItems (selected, selectedSnippet, state::Scope::only (state::AllSections, { state::StripRef::of (*strip) }));
    refresh();
}

void ScenesView::toggleSection (juce::uint32 section)
{
    if (selected < 0 || selectedSnippet < 0)
        return;
    // Without strips the items are every strip the snippet has.
    if ((getSnippetSections() & section) != 0)
        ctx.show.removeSnippetItems (selected, selectedSnippet, state::Scope::only (section, {}));
    else
        ctx.show.addSnippetItems (selected, selectedSnippet, state::Scope::only (section, {}));
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
    // The snippet list follows changes made elsewhere (the WING surface, a remote).
    juce::String snippets;
    for (int i = 0; i < getNumSnippets(); ++i)
        if (const auto s = ctx.show.getSnippet (selected, i))
            snippets << s->name << (s->fireWithScene ? "*" : "") << (int) s->strips.size() << "/" << (int) s->sections << "|";
    if (snippets != snippetsShown)
    {
        snippetsShown = snippets;
        snippetList->setSize (snippetList->getWidth(), juce::jmax (kRow, getNumSnippets() * (kRow + sizes::kGapSmall)));
        if (selectedSnippet >= getNumSnippets())
            selectSnippet (getNumSnippets() - 1);
        snippetList->repaint();
    }
    snippetNew.setEnabled (selected >= 0);
    snippetFire.refresh();
    const bool snippet = selected >= 0 && selectedSnippet >= 0;
    const bool strip = ctx.getSelectedStrip && ctx.getSelectedStrip() != nullptr;
    snippetAddStrip.setEnabled (snippet && strip);
    snippetRemoveStrip.setEnabled (snippet && strip);
    for (auto& k : sectionKeys)
    {
        k->setEnabled (snippet);
        k->refresh();
    }
    const bool x32 = ctx.engine.getProfile() == ld::kProfileX32;
    if (importKey.isVisible() != x32)
    {
        importKey.setVisible (x32);
        exportKey.setVisible (x32);
        resized();
    }
}

void ScenesView::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    if (! snippetPanel.isEmpty())
    {
        g.setColour (theme.panel);
        g.fillRoundedRectangle (snippetPanel.toFloat(), sizes::kRadius);
        g.setColour (theme.text);
        g.setFont (type::font (type::kLabel + 2.0f, true));
        g.drawText (selected >= 0 ? "SNIPPETS OF SCENE " + juce::String (selected + 1) : juce::String ("SNIPPETS"),
                    snippetPanel.withHeight (36).reduced (sizes::kGap + 4, 0), juce::Justification::centredLeft);
    }
    if (fileStatus.isNotEmpty() && importKey.isVisible())
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kCaption));
        g.drawFittedText (fileStatus, statusArea, juce::Justification::centredLeft, 2, 0.8f);
    }
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
    if (importKey.isVisible())
    {
        // The X32's scene files at the right of the keys, what they did under them.
        exportKey.setBounds (keys.removeFromRight (w + 30));
        keys.removeFromRight (sizes::kGapSmall);
        importKey.setBounds (keys.removeFromRight (w + 30));
        statusArea = area.removeFromTop (22).withTrimmedLeft (keys.getX() - area.getX());
    }
    area.removeFromTop (sizes::kGap);
    // The selected scene's snippets at the right (a third of the view, at least 360 px).
    snippetPanel = area.removeFromRight (juce::jmax (360, area.getWidth() / 3));
    area.removeFromRight (sizes::kGap);
    auto sp = snippetPanel.reduced (sizes::kGap);
    sp.removeFromTop (36 - sizes::kGap);
    auto row1 = sp.removeFromTop (sizes::kTouch);
    const int kw = (row1.getWidth() - 3 * sizes::kGapSmall) / 4;
    for (auto* k : { &snippetNew, &snippetGo, &snippetStore, &snippetRename })
    {
        k->setBounds (row1.removeFromLeft (kw));
        row1.removeFromLeft (sizes::kGapSmall);
    }
    sp.removeFromTop (sizes::kGapSmall);
    auto row2 = sp.removeFromTop (sizes::kTouch);
    snippetRemove.setBounds (row2.removeFromRight (kw));
    row2.removeFromRight (sizes::kGapSmall);
    snippetFire.setBounds (row2);
    // Its scope: the selected strip in or out, then the sections in two rows of five.
    sp.removeFromTop (sizes::kGapSmall);
    auto row3 = sp.removeFromTop (sizes::kTouch);
    const int half = (row3.getWidth() - sizes::kGapSmall) / 2;
    snippetAddStrip.setBounds (row3.removeFromLeft (half));
    snippetRemoveStrip.setBounds (row3.withTrimmedLeft (sizes::kGapSmall));
    const int sw = (sp.getWidth() - 4 * sizes::kGapSmall) / 5;
    for (int i = 0; i < (int) sectionKeys.size(); ++i)
    {
        if (i % 5 == 0)
            sp.removeFromTop (sizes::kGapSmall);
        auto row = sp.withHeight (sizes::kTouch);
        sectionKeys[(size_t) i]->setBounds (row.withTrimmedLeft ((i % 5) * (sw + sizes::kGapSmall)).withWidth (sw));
        if (i % 5 == 4)
            sp.removeFromTop (sizes::kTouch);
    }
    sp.removeFromTop (sizes::kGap);
    snippetList->setBounds (sp.withHeight (juce::jmax (kRow, juce::jmin (sp.getHeight(), getNumSnippets() * (kRow + sizes::kGapSmall)))));
    viewport.setBounds (area);
    list->setSize (viewport.getWidth() - viewport.getScrollBarThickness(), (int) rows.size() * (kRow + sizes::kGapSmall));
}

} // namespace livedaw::ui::universal
