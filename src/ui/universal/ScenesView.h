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
    .scn and EXPORT .scn read and write X32 scene files (`x32::SceneFiles`). */
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

    ToggleKey goKey, prevKey, nextKey, addKey, storeKey, renameKey, upKey, downKey, removeKey;
    ToggleKey importKey, exportKey;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class List;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScenesView)
};

} // namespace livedaw::ui::universal
