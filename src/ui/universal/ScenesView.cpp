#include "ui/universal/ScenesView.h"

#include "ui/universal/Groups.h"

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

    /** What a snippet holds, at the right of its row: "2 STRIPS  3 DCA  1 FX  FIRES". */
    juce::String snippetMarks (const Show::Snippet& s, MixerEngine& engine)
    {
        const auto counts = engine.getCounts();
        juce::StringArray marks;
        marks.add (juce::String ((int) s.strips.size()) + ((int) s.strips.size() == 1 ? " STRIP" : " STRIPS"));
        if (const int n = (s.scope.dcas & GroupBits::first (counts.dcas)).count(); n > 0)
            marks.add (juce::String (n) + " DCA");
        if (const int n = (s.scope.muteGroups & GroupBits::first (counts.muteGroups)).count(); n > 0)
            marks.add (juce::String (n) + " MGRP");
        int fx = 0;
        for (auto bits = s.scope.fx & state::Scope::allFx (counts.fx); bits != 0; bits &= bits - 1)
            ++fx;
        if (fx > 0)
            marks.add (juce::String (fx) + " FX");
        if (s.scope.console)
            marks.add ("CONSOLE");
        if (s.fireWithScene)
            marks.add ("FIRES");
        return marks.joinIntoString ("  ");
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
            g.setColour (sel ? t.lifted (t.raised, 0.1f) : t.raised);
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
            const auto flagsFont = type::font (type::kCaption, true);
            g.setFont (flagsFont);
            g.drawText (row.flags, inner, juce::Justification::centredRight);
            g.setColour (t.text);
            const auto nameFont = type::font (type::kLabel, true);
            g.setFont (nameFont);
            g.drawText (row.text, inner, juce::Justification::centredLeft);
            // Its notes after its name, where the name and the marks leave room.
            if (row.notes.isNotEmpty())
            {
                auto notes = inner.withTrimmedLeft (juce::GlyphArrangement::getStringWidth (nameFont, row.text) + 24.0f)
                                 .withTrimmedRight (juce::GlyphArrangement::getStringWidth (flagsFont, row.flags) + 16.0f);
                if (notes.getWidth() > 60.0f)
                {
                    g.setColour (t.textDim);
                    g.setFont (type::font (type::kCaption));
                    g.drawText (row.notes, notes, juce::Justification::centredLeft);
                }
            }
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
            g.setColour (sel ? t.lifted (t.raised, 0.1f) : t.raised);
            g.fillRoundedRectangle (r, sizes::kRadius);
            g.setColour (sel ? t.accent : t.outline);
            g.drawRoundedRectangle (r.reduced (0.5f), sizes::kRadius, sel ? 2.0f : 1.0f);
            auto inner = r.reduced (12.0f, 0.0f);
            if (! s.has_value())
                continue;
            g.setColour (t.textDim);
            g.setFont (type::font (type::kCaption, true));
            const auto marks = snippetMarks (*s, owner.ctx.engine);
            g.drawText (marks, inner, juce::Justification::centredRight);
            g.setColour (t.text);
            g.setFont (type::font (type::kLabel, true));
            g.drawFittedText (s->name, inner.withTrimmedRight (juce::GlyphArrangement::getStringWidth (type::font (type::kCaption, true), marks) + 10.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.8f);
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
    : snippetsTab ("SNIPPETS", t.accent, t), scopeTab ("SCOPE", t.accent, t), safesTab ("SAFES", t.solo, t), snippetItemsKey ("ITEMS", t.accent, t),
      goKey ("GO", t.accent, t), prevKey ("PREV", t.accent, t), nextKey ("NEXT", t.accent, t), addKey ("ADD", t.accent, t),
      storeKey ("STORE", t.accent, t), renameKey ("RENAME", t.accent, t), upKey ("UP", t.accent, t), downKey ("DOWN", t.accent, t),
      removeKey ("REMOVE", t.mute, t), importKey ("IMPORT .scn", t.accent, t), exportKey ("EXPORT .scn", t.accent, t),
      skipKey ("SKIP", t.accent, t), linkKey ("LINK", t.accent, t), tagKey ("TAG", t.accent, t), fadeKey ("FADE", t.accent, t), notesKey ("NOTES", t.accent, t),
      snippetNew ("NEW", t.accent, t), snippetGo ("GO", t.accent, t), snippetStore ("STORE", t.accent, t), snippetRename ("RENAME", t.accent, t),
      snippetFire ("FIRE WITH SCENE", t.accent, t), snippetRemove ("REMOVE", t.mute, t), snippetAddStrip ("+ STRIP", t.accent, t),
      snippetRemoveStrip ("- STRIP", t.accent, t), ctx (c), theme (t), list (std::make_unique<List> (*this)),
      snippetList (std::make_unique<SnippetList> (*this)), scopeEditor (c, t, ScopeRole::Recalls), safesEditor (c, t, ScopeRole::Safe),
      snippetItems (c, t, ScopeRole::Holds)
{
    // GO is the view's main key: lit, and the widest.
    goKey.isOn = [this] { return selected >= 0; };
    goKey.onClick = [this] { go(); };
    skipKey.isOn = [this]
    {
        const auto s = selected >= 0 ? ctx.show.getScene (selected) : std::nullopt;
        return s.has_value() && s->skip;
    };
    skipKey.onClick = [this] { toggleSkip(); };
    skipKey.setTooltip ("GO PREV / NEXT and recalls pass over this scene");
    linkKey.isOn = [this]
    {
        const auto s = selected >= 0 ? ctx.show.getScene (selected) : std::nullopt;
        return s.has_value() && s->linkNext;
    };
    linkKey.onClick = [this] { toggleLink(); };
    linkKey.setTooltip ("Recalling this scene also recalls the one below it");
    tagKey.onClick = [this]
    {
        const auto s = selected >= 0 ? ctx.show.getScene (selected) : std::nullopt;
        if (! s.has_value())
            return;
        juce::Component::SafePointer<ScenesView> safe (this);
        askForText (*this, "TAG", "Tag of scene " + juce::String (selected + 1) + " (1-" + juce::String (Show::maxSceneTag) + ", empty for none):", s->tag > 0 ? juce::String (s->tag) : juce::String(),
                    [safe] (const juce::String& text)
                    {
                        if (safe != nullptr)
                            safe->setTag (text.getIntValue());
                    });
    };
    fadeKey.setTooltip ("Glide the faders, pans and send levels over this time when recalled");
    fadeKey.onClick = [this]
    {
        const auto s = selected >= 0 ? ctx.show.getScene (selected) : std::nullopt;
        if (! s.has_value())
            return;
        juce::PopupMenu menu;
        menu.setLookAndFeel (&lookAndFeelFor (theme));
        menu.addSectionHeader ("Fade time");
        const std::array<double, 10> times { 0.0, 0.5, 1.0, 2.0, 3.0, 5.0, 8.0, 10.0, 20.0, 30.0 };
        for (size_t i = 0; i < times.size(); ++i)
            menu.addItem ((int) i + 1, times[i] <= 0.0 ? juce::String ("Off") : juce::String (times[i], times[i] < 1.0 ? 1 : 0) + " s", true, std::abs (s->fadeSeconds - times[i]) < 0.01);
        juce::Component::SafePointer<ScenesView> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fadeKey).withStandardItemHeight (sizes::kTouch - 8), [safe, times] (int r)
        {
            if (safe != nullptr && juce::isPositiveAndBelow (r - 1, (int) times.size()))
                safe->setFadeSeconds (times[(size_t) (r - 1)]);
        });
    };
    notesKey.onClick = [this]
    {
        const auto s = selected >= 0 ? ctx.show.getScene (selected) : std::nullopt;
        if (! s.has_value())
            return;
        juce::Component::SafePointer<ScenesView> safe (this);
        askForText (*this, "NOTES", "Notes of scene " + juce::String (selected + 1) + ":", s->notes,
                    [safe] (const juce::String& text)
                    {
                        if (safe != nullptr)
                            safe->setNotes (text);
                    });
    };
    for (auto* k : { &skipKey, &linkKey, &tagKey, &fadeKey, &notesKey })
        addAndMakeVisible (k);
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

    // The panel's pages: the selected scene's snippets, its recall scope, the recall safes.
    for (auto* k : { &snippetsTab, &scopeTab, &safesTab })
        k->tab = true;
    snippetsTab.isOn = [this] { return page == Page::Snippets; };
    snippetsTab.onClick = [this] { showPage (Page::Snippets); };
    scopeTab.isOn = [this] { return page == Page::Scope; };
    scopeTab.onClick = [this] { showPage (Page::Scope); };
    scopeTab.setTooltip ("What GO recalls of the selected scene");
    safesTab.isOn = [this] { return page == Page::Safes; };
    safesTab.isHalfOn = [this] { return anySafe(); }; // its outline lit while anything is safe
    safesTab.onClick = [this] { showPage (Page::Safes); };
    safesTab.setTooltip ("What every recall leaves alone");
    for (auto* k : { &snippetsTab, &scopeTab, &safesTab })
        addAndMakeVisible (k);

    scopeEditor.get = [this] { return selected >= 0 ? ctx.show.getSceneRecallScope (selected) : state::Scope::none(); };
    scopeEditor.change = [this] (const state::Scope& items, bool in)
    {
        if (selected < 0)
            return;
        auto scope = ctx.show.getSceneRecallScope (selected);
        applyScopeChange (scope, items, in, ctx.engine);
        // Its sections alone are the scene's scope as the show has always kept it.
        if (items.strips.empty() && ! items.dcas.any() && ! items.muteGroups.any() && items.fx == 0 && ! items.console)
            ctx.show.setSceneScope (selected, scope.sections);
        else
            ctx.show.setSceneRecallScope (selected, scope);
        refresh();
    };
    // The safes: a strip's is its own recall safe, the rest are the show's global safes.
    safesEditor.stripOn = [] (const Strip& s) { return s.params.recallSafe.load(); };
    safesEditor.get = [this] { return ctx.show.getGlobalSafe(); };
    safesEditor.change = [this] (const state::Scope& items, bool in)
    {
        for (const auto& r : items.strips)
            if (auto* s = state::findStrip (ctx.engine, r))
                s->params.recallSafe = in;
        auto rest = items;
        rest.strips.clear();
        auto safe = ctx.show.getGlobalSafe();
        applyScopeChange (safe, rest, in, ctx.engine);
        safe.allStrips = false;
        safe.strips.clear();
        ctx.show.setGlobalSafe (safe);
        refresh();
    };
    // A snippet's items: captured into it as they come in.
    snippetItems.setSectionsShown (false);
    snippetItems.get = [this]
    {
        const auto s = selected >= 0 && selectedSnippet >= 0 ? ctx.show.getSnippet (selected, selectedSnippet) : std::nullopt;
        return s.has_value() ? s->scope : state::Scope::none();
    };
    snippetItems.change = [this] (const state::Scope& items, bool in)
    {
        if (selected < 0 || selectedSnippet < 0)
            return;
        auto chosen = items;
        if (! chosen.strips.empty())
        {
            // A strip comes in with the snippet's sections and goes with all it holds.
            const auto sections = getSnippetSections();
            chosen.sections = in && sections != 0 ? sections : (juce::uint32) state::AllSections;
        }
        if (in)
            ctx.show.addSnippetItems (selected, selectedSnippet, chosen);
        else
            ctx.show.removeSnippetItems (selected, selectedSnippet, chosen);
        refresh();
    };
    snippetItemsKey.isOn = [this] { return snippetItemsShown; };
    snippetItemsKey.onClick = [this] { setSnippetItemsShown (! snippetItemsShown); };
    snippetItemsKey.setTooltip ("The snippet's strips, DCAs, mute groups and FX slots as keys");
    addAndMakeVisible (snippetItemsKey);
    for (auto* e : { &scopeEditor, &safesEditor, &snippetItems })
        addChildComponent (e);

    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme.outline);
    addAndMakeVisible (viewport);
    for (auto* k : { &goKey, &prevKey, &nextKey, &addKey, &storeKey, &renameKey, &upKey, &downKey, &removeKey })
        addAndMakeVisible (k);
    selectSnippet (-1); // the snippet keys start disabled, as with no snippet selected
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
    for (auto* k : { &goKey, &storeKey, &renameKey, &upKey, &downKey, &removeKey, &skipKey, &linkKey, &tagKey, &fadeKey, &notesKey })
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
    snippetItemsKey.setEnabled (selectedSnippet >= 0);
    if (selectedSnippet < 0 && snippetItemsShown)
        setSnippetItemsShown (false); // no snippet: the list again
    snippetItems.refresh();
    snippetList->repaint();
}

void ScenesView::setSnippetItemsShown (bool shouldShow)
{
    snippetItemsShown = shouldShow && selectedSnippet >= 0;
    snippetItemsKey.refresh();
    showPage (page);
}

void ScenesView::showPage (Page p)
{
    page = p;
    const bool snippets = p == Page::Snippets;
    for (auto* k : { &snippetNew, &snippetGo, &snippetStore, &snippetRename, &snippetFire, &snippetRemove, &snippetAddStrip, &snippetRemoveStrip, &snippetItemsKey })
        k->setVisible (snippets);
    for (auto& k : sectionKeys)
        k->setVisible (snippets);
    snippetList->setVisible (snippets && ! snippetItemsShown);
    snippetItems.setVisible (snippets && snippetItemsShown);
    scopeEditor.setVisible (p == Page::Scope);
    safesEditor.setVisible (p == Page::Safes);
    for (auto* k : { &snippetsTab, &scopeTab, &safesTab })
        k->refresh();
    resized();
    refresh();
    repaint();
}

bool ScenesView::anySafe() const
{
    if (ctx.show.hasGlobalSafes())
        return true;
    for (int k = 0; k < kNumStripKinds; ++k)
        for (auto& s : ctx.engine.getStrips (static_cast<StripKind> (k)))
            if (s->params.recallSafe.load())
                return true;
    return false;
}

juce::String ScenesView::getSafesLine() const
{
    return anySafe() ? "SAFE FROM EVERY RECALL:  " + safesEditor.getSummary() : juce::String();
}

juce::String ScenesView::getRowMarks (int index) const
{
    return juce::isPositiveAndBelow (index, (int) rows.size()) ? rows[(size_t) index].flags : juce::String();
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

void ScenesView::toggleSkip()
{
    if (const auto s = selected >= 0 ? ctx.show.getScene (selected) : std::nullopt; s.has_value())
        ctx.show.setSceneSkip (selected, ! s->skip);
    refresh();
}

void ScenesView::toggleLink()
{
    if (const auto s = selected >= 0 ? ctx.show.getScene (selected) : std::nullopt; s.has_value())
        ctx.show.setSceneLink (selected, ! s->linkNext);
    refresh();
}

void ScenesView::setTag (int tag)
{
    if (selected >= 0)
        ctx.show.setSceneTag (selected, tag);
    refresh();
}

void ScenesView::setFadeSeconds (double seconds)
{
    if (selected >= 0)
        ctx.show.setSceneFadeSeconds (selected, seconds);
    refresh();
}

void ScenesView::setNotes (const juce::String& notes)
{
    if (selected >= 0)
        ctx.show.setSceneNotes (selected, notes);
    refresh();
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
            // A scene that recalls less than a new one does says so.
            if (scene->item == Show::ItemKind::Snapshot && isPartialScope (ctx.show.getSceneRecallScope (i), ctx.engine))
                marks.add ("PARTIAL");
            row.flags = marks.joinIntoString ("  ");
            row.notes = scene->notes;
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
    for (auto* k : { &goKey, &storeKey, &renameKey, &upKey, &downKey, &removeKey, &skipKey, &linkKey, &tagKey, &fadeKey, &notesKey })
        k->setEnabled (selected >= 0);
    {
        // The selected scene's settings, on their keys.
        const auto s = selected >= 0 ? ctx.show.getScene (selected) : std::nullopt;
        tagKey.setButtonText (s.has_value() && s->tag > 0 ? "TAG #" + juce::String (s->tag) : juce::String ("TAG"));
        fadeKey.setButtonText (s.has_value() && s->fadeSeconds > 0.0 ? "FADE " + juce::String (s->fadeSeconds, s->fadeSeconds < 1.0 ? 1 : 0) + " s" : juce::String ("FADE"));
        skipKey.refresh();
        linkKey.refresh();
        goKey.refresh();
    }
    // The snippet list follows changes made elsewhere (the WING surface, a remote).
    juce::String snippets;
    for (int i = 0; i < getNumSnippets(); ++i)
        if (const auto s = ctx.show.getSnippet (selected, i))
            snippets << s->name << "/" << snippetMarks (*s, ctx.engine) << "/" << (int) s->sections << "|";
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
    snippetItemsKey.setEnabled (snippet);
    // The page shown: its keys, and what it says in a line.
    const auto scene = selected >= 0 ? ctx.show.getScene (selected) : std::nullopt;
    const bool scoped = scene.has_value() && scene->item == Show::ItemKind::Snapshot;
    scopeEditor.setEnabled (scoped);
    scopeEditor.setAlpha (scoped ? 1.0f : 0.4f);
    juce::String summary;
    if (page == Page::Scope)
    {
        scopeEditor.refresh();
        summary = scoped ? scopeEditor.getSummary() : juce::String (scene.has_value() ? "A file added to the show has no scope" : "Select a scene");
    }
    else if (page == Page::Safes)
    {
        safesEditor.refresh();
        summary = safesEditor.getSummary();
    }
    else if (snippetItemsShown)
    {
        snippetItems.refresh();
    }
    if (summary != summaryShown)
    {
        summaryShown = summary;
        repaint (snippetPanel);
    }
    safesTab.refresh();
    // The line over the list comes and goes with the safes.
    if (const auto line = getSafesLine(); line != safesShown)
    {
        const bool moved = line.isEmpty() != safesShown.isEmpty();
        safesShown = line;
        if (moved)
            resized();
        repaint();
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
        g.setFont (type::font (type::kHeading, true));
        const auto scene = selected >= 0 ? " " + juce::String (selected + 1) : juce::String();
        const auto title = page == Page::Snippets ? (selected >= 0 ? "SNIPPETS OF SCENE" + scene : juce::String ("SNIPPETS"))
                           : page == Page::Scope  ? (selected >= 0 ? "SCENE" + scene + " RECALLS" : juce::String ("RECALL SCOPE"))
                                                  : juce::String ("EVERY RECALL LEAVES ALONE");
        g.drawFittedText (title, titleArea, juce::Justification::centredLeft, 1, 0.8f);
        if (! summaryArea.isEmpty())
        {
            // The scope in a line (two where it needs them), in the colour of its keys.
            g.setColour (page == Page::Safes ? (anySafe() ? theme.solo : theme.textDim) : theme.text);
            g.setFont (type::font (type::kLabel));
            g.drawFittedText (summaryShown, summaryArea, juce::Justification::centredLeft, 2, 0.8f);
        }
    }
    if (! safesArea.isEmpty() && safesShown.isNotEmpty())
    {
        g.setColour (theme.solo);
        g.setFont (type::font (type::kCaption, true));
        g.drawFittedText (safesShown, safesArea, juce::Justification::centredLeft, 1, 0.8f);
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
    // In three groups: run the show (GO the widest), make scenes, tidy them.
    constexpr int kGo = 140;
    const int reserve = importKey.isVisible() ? 2 * 130 + 2 * sizes::kGapSmall : 0;
    const int w = juce::jmax (44, juce::jmin (104, (keys.getWidth() - kGo - reserve - 2 * sizes::kGapLarge - 7 * sizes::kGapSmall) / 8));
    goKey.setBounds (keys.removeFromLeft (kGo));
    keys.removeFromLeft (sizes::kGapSmall);
    const auto group = [&keys, w] (std::initializer_list<ToggleKey*> members)
    {
        for (auto* k : members)
        {
            k->setBounds (keys.removeFromLeft (w));
            keys.removeFromLeft (sizes::kGapSmall);
        }
        keys.removeFromLeft (sizes::kGapLarge - sizes::kGapSmall);
    };
    group ({ &prevKey, &nextKey });
    group ({ &addKey, &storeKey, &renameKey });
    group ({ &upKey, &downKey, &removeKey });
    if (importKey.isVisible())
    {
        // The X32's scene files at the right of the keys, what they did under them.
        exportKey.setBounds (keys.removeFromRight (130));
        keys.removeFromRight (sizes::kGapSmall);
        importKey.setBounds (keys.removeFromRight (130));
        statusArea = area.removeFromTop (22).withTrimmedLeft (keys.getX() - area.getX());
    }
    area.removeFromTop (sizes::kGap);
    // The selected scene's snippets at the right (a third of the view, at least 360 px).
    snippetPanel = area.removeFromRight (juce::jmax (360, area.getWidth() / 3));
    area.removeFromRight (sizes::kGap);
    // What is safe from every recall, over the list while anything is.
    safesArea = {};
    if (safesShown.isNotEmpty())
    {
        safesArea = area.removeFromTop (22).reduced (4, 0);
        area.removeFromTop (sizes::kGapSmall);
    }
    auto sp = snippetPanel.reduced (sizes::kGap);
    // The panel's pages, then what the page shown is about.
    auto tabs = sp.removeFromTop (sizes::kTouch);
    const int tw = (tabs.getWidth() - 2 * sizes::kGapSmall) / 3;
    for (auto* k : { &snippetsTab, &scopeTab, &safesTab })
    {
        k->setBounds (tabs.removeFromLeft (tw));
        tabs.removeFromLeft (sizes::kGapSmall);
    }
    titleArea = sp.removeFromTop (32).reduced (4, 0);
    summaryArea = {};
    if (page != Page::Snippets)
    {
        summaryArea = sp.removeFromTop (36).reduced (4, 0);
        sp.removeFromTop (sizes::kGapSmall);
        scopeEditor.setBounds (sp);
        safesEditor.setBounds (sp);
    }
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
    const int third = (row3.getWidth() - 2 * sizes::kGapSmall) / 3;
    snippetAddStrip.setBounds (row3.removeFromLeft (third));
    row3.removeFromLeft (sizes::kGapSmall);
    snippetRemoveStrip.setBounds (row3.removeFromLeft (third));
    snippetItemsKey.setBounds (row3.withTrimmedLeft (sizes::kGapSmall));
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
    snippetItems.setBounds (sp);
    // The selected scene's settings under the list.
    auto details = area.removeFromBottom (sizes::kTouch);
    area.removeFromBottom (sizes::kGap);
    for (auto* k : { &skipKey, &linkKey, &tagKey, &fadeKey })
    {
        k->setBounds (details.removeFromLeft (juce::jmin (120, (details.getWidth() - 4 * sizes::kGapSmall) / 5)));
        details.removeFromLeft (sizes::kGapSmall);
    }
    notesKey.setBounds (details.withWidth (juce::jmin (details.getWidth(), 200)));
    viewport.setBounds (area);
    list->setSize (viewport.getWidth() - viewport.getScrollBarThickness(), (int) rows.size() * (kRow + sizes::kGapSmall));
}

} // namespace livedaw::ui::universal

