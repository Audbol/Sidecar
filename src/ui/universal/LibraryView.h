#pragma once

#include "ui/UiContext.h"
#include "ui/universal/ValueControls.h"
#include "ui/universal/Widgets.h"

#include <memory>

namespace livedaw::ui::universal
{

/** The LIBRARY view: the files in the Library folder (the WING surface's
    LIBRARY keeps them there too), as a list a filter narrows (ALL, SCENES,
    SNIPPETS, CHANNEL, FX), and keys for the one selected: LOAD puts a scene or
    snippet file onto the console, a channel preset onto the selected strip, an
    FX preset into the FX SLOT chosen; ADD TO SHOW makes it a scene of the show
    (a scene file is imported, the others become item scenes); RENAME; DELETE
    (to the bin, asking first). SAVE SCENE writes the show's current scene, with
    its snippets, as a scene file. What a key did shows under them. */
class LibraryView final : public juce::Component, private juce::Timer
{
public:
    LibraryView (UiContext&, const Theme&);
    ~LibraryView() override;

    enum class Filter
    {
        All,
        Scenes,
        Snippets,
        Channel,
        Fx
    };
    void setFilter (Filter);
    Filter getFilter() const noexcept { return filter; }

    /** Reads the folder again (the timer does this every 2 s while shown). */
    void refresh();
    int getNumFiles() const noexcept { return (int) files.size(); }
    juce::File getFile (int index) const;
    int getSelected() const noexcept { return selected; }
    void select (int index);
    void selectFile (const juce::File&);

    // What the keys do (DELETE asks first; this doesn't). Each returns what it did.
    juce::String load();
    juce::String addToShow();
    juce::String rename (const juce::String& newName);
    juce::String remove();
    juce::String saveScene();
    juce::String getStatus() const { return status; }

    /** The FX slot an FX preset loads into (0-based). */
    int fxSlot = 0;

    ToggleKey allKey, scenesKey, snippetsKey, channelKey, fxKey;
    ToggleKey loadKey, addKey, renameKey, deleteKey, saveSceneKey;
    ChoiceBar slotChoice;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    class List;
    struct Entry
    {
        juce::File file;
        juce::String kind, detail;
    };
    void timerCallback() override { refresh(); }
    void said (const juce::String&);

    UiContext& ctx;
    const Theme& theme;
    Filter filter = Filter::All;
    std::vector<Entry> files;
    int selected = -1;
    juce::String status, shown;
    juce::Rectangle<int> statusArea, folderArea;
    std::unique_ptr<List> list;
    juce::Viewport viewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryView)
};

} // namespace livedaw::ui::universal
