#pragma once

#include "ui/UiContext.h"
#include "ui/X32SceneFiles.h"
#include "ui/universal/Widgets.h"

#include <memory>

namespace livedaw::ui::universal
{

/** The SCENES view: the show's scenes in order (the current one marked, SKIP,
    LINK and its tag shown), and keys to run and edit them: GO recalls the
    selected scene, PREV / NEXT the show's previous / next one (as the WING's
    GO PREV / GO NEXT), ADD captures the console as a new scene after the
    selected one, STORE captures it into the selected one, RENAME, UP / DOWN
    move it, REMOVE deletes it. STORE and REMOVE ask first. On an X32, IMPORT
    .scn and EXPORT .scn read and write X32 scene files (`x32::SceneFiles`).
    SNIPPETS, beside them, are the selected scene's: NEW makes one of the
    selected strip (every section), GO recalls one, STORE captures the console
    into it, RENAME, FIRE WITH SCENE (it fires when its scene is recalled),
    REMOVE (asking first). Its scope: + STRIP captures the selected strip into
    it, - STRIP drops that strip, and a section's key (NAME ... GROUPS) captures
    that section of its strips, or drops it. */
class ScenesView final : public juce::Component, private juce::Timer
{
public:
    ScenesView (UiContext&, const Theme&);
    ~ScenesView() override;

    int getNumRows() const noexcept { return (int) rows.size(); }
    int getSelectedScene() const noexcept { return selected; }
    void selectScene (int index);
    /** Takes the show's scenes again (the timer does this 5 times a second). */
    void refresh();

    // What the keys do (STORE and REMOVE ask first; these don't).
    void go();
    int add();
    void store();
    void remove();
    void move (int delta);
    void rename (const juce::String&);

    // The selected scene's snippets (these don't ask either).
    int getNumSnippets() const;
    int getSelectedSnippet() const noexcept { return selectedSnippet; }
    void selectSnippet (int index);
    /** A snippet of the selected strip, every section; -1 if no scene or strip is selected. */
    int newSnippet();
    void recallSnippet();
    void storeSnippet();
    void removeSnippet();
    void renameSnippet (const juce::String&);
    void toggleFireWithScene();
    /** The selected snippet's scope: the selected strip in (its sections now) or out;
        a section in (captured from its strips) or out. */
    void addSelectedStrip();
    void removeSelectedStrip();
    void toggleSection (juce::uint32 section);
    /** The selected snippet's sections (0: none selected). */
    juce::uint32 getSnippetSections() const;

    ToggleKey goKey, prevKey, nextKey, addKey, storeKey, renameKey, upKey, downKey, removeKey;
    ToggleKey importKey, exportKey;
    ToggleKey snippetNew, snippetGo, snippetStore, snippetRename, snippetFire, snippetRemove;
    ToggleKey snippetAddStrip, snippetRemoveStrip;
    /** NAME, ROUTING, INPUT, GATE, EQ, DYNAMICS, INSERTS, SENDS, FADER, GROUPS. */
    std::vector<std::unique_ptr<ToggleKey>> sectionKeys;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class List;
    class SnippetList;
    struct Row
    {
        juce::String text, flags;
        bool current = false;
        bool operator!= (const Row& o) const { return text != o.text || flags != o.flags || current != o.current; }
    };
    void timerCallback() override
    {
        if (isVisible())
            refresh();
    }
    void confirm (const juce::String& title, const juce::String& message, std::function<void()> then);

    UiContext& ctx;
    const Theme& theme;
    x32::SceneFiles sceneFiles { ctx };
    juce::String fileStatus; // what the last IMPORT / EXPORT .scn did
    juce::Rectangle<int> statusArea;
    std::vector<Row> rows;
    int selected = -1;
    std::unique_ptr<List> list;
    juce::Viewport viewport;
    int selectedSnippet = -1;
    std::unique_ptr<SnippetList> snippetList;
    juce::Rectangle<int> snippetPanel;
    juce::String snippetsShown; // what the snippet list drew (its names), to repaint on a change

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScenesView)
};

} // namespace livedaw::ui::universal
