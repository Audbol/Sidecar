#pragma once

#include "ui/UiContext.h"
#include "ui/universal/LayerDrag.h"
#include "ui/universal/Theme.h"
#include "ui/universal/Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace livedaw::ui::universal
{

/** The colours of a layer as a grid of swatches big enough for a finger (the editor's COLOUR key shows it in
    a call-out; the first swatch clears the colour). */
class ColourGrid final : public juce::Component
{
public:
    static constexpr int kSwatch = 56, kColumns = 3;

    explicit ColourGrid (const Theme&);

    /** Called with the colour touched (0xAARRGGBB; 0 clears it). */
    std::function<void (juce::uint32)> onPick;

    static int getNumColours();
    static juce::uint32 colourAt (int index);
    static juce::String nameAt (int index);
    juce::Rectangle<int> getSwatchBounds (int index) const;
    /** The size the grid wants. */
    juce::Point<int> getIdealSize() const;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    const Theme& theme;
};

/** The editor of the show's user layers (the same layers as the WING surface's
    USER layers): the layers on the left (NEW, RENAME, COLOUR, DELETE, DUPLICATE,
    EXPORT, IMPORT, BANK: which console bank it occupies), the selected layer's entries on the right (ADD
    SELECTED, ADD DCA, SPACER, REMOVE, UP, DOWN, and drag: a finger on an entry's handle, a mouse drag, or
    a long press picks an entry up; dropped between entries it moves there, on a layer in the left list it is
    added to that layer, outside the list it is taken out). Everything is for a finger as well as a mouse:
    keys and rows are at least a touch target tall, a right-click or a long press on a layer or an entry
    opens its menu, a name is typed on the on-screen keyboard, the colours are a grid of swatches, and a
    drag held near the list's edge scrolls it. A spacer is a blank slot: a gap in the view. An
    entry is any strip, a DCA, or a spacer. Every edit is made on the show's
    LayerSet, so the WING surface sees it at once. */
class LayerEditor final : public juce::Component, private juce::ListBoxModel
{
public:
    LayerEditor (UiContext&, const Theme&);

    /** Called after every edit (the view rebuilds its keys and columns). */
    std::function<void()> onChanged;

    // The edits (the keys call these; so do the tests).
    juce::String newLayer();
    void renameSelected (const juce::String& name);
    void setSelectedColour (juce::uint32 argb);
    void deleteSelected();
    juce::String duplicateSelected();
    /** The strip selected on the console goes to the end of the layer; false when none is selected or the layer is full. */
    bool addSelectedStrip();
    /** DCA `index` goes to the end of the layer; false when the layer is full or the DCA is out of range. */
    bool addDca (int index);
    /** A gap before the selected entry (nothing without one: a gap at the end would be dropped). */
    void addSpacer();
    /** A gap at `index` (before the entry there; nothing at the end, where it would be dropped). */
    void addSpacerAt (int index);
    void removeEntry();
    /** Drag editing: entry `from` moved to between entries `to - 1` and `to`; copied to another layer; taken out. */
    bool moveEntryTo (int from, int to);
    bool addEntryToLayer (int row, const juce::String& otherLayerId);
    bool removeEntryAt (int row);
    /** The selected layer occupies `bank` of `console` (layerlink's keys: "sq5", "c1500", "c3500"; empty: none). */
    void setSelectedBank (const juce::String& console, const juce::String& bank);
    /** The drag as the pointer makes it (editor coordinates); drop returns whether anything was edited. */
    void beginDrag (int row, juce::Point<int> at);
    void moveDrag (juce::Point<int> at);
    bool endDrag (juce::Point<int> at);
    bool isDragging() const noexcept { return dragRow >= 0; }
    void moveEntry (int delta);
    bool exportTo (const juce::File&) const;
    /** How many layers were added; 0 when the file isn't a layer file. */
    int importFrom (const juce::File&);

    /** The name asked for on the on-screen keyboard (RENAME). */
    void beginRename();
    /** The menu of a layer (a right-click or a long press on its key or row): it is selected, then RENAME,
        COLOUR, DUPLICATE, DELETE and BANK. `screenArea` is what the menu opens beside. */
    void showLayerMenu (juce::String id, juce::Rectangle<int> screenArea); // the id by value: callers pass one from a layer list that a lookup may rebuild
    /** The menu of an entry: SPACER before and after, UP, DOWN, REMOVE. */
    void showEntryMenu (int row, juce::Rectangle<int> screenArea);
    /** The colours as a grid of swatches in a call-out beside `screenArea`. */
    void showColourMenu (juce::Rectangle<int> screenArea);
    /** The gestures' listeners (a press on an entry, on a layer), for the tests to feed events to. */
    DragPickup& getPickup() noexcept { return pickup; }
    LongPress& getLayerPress() noexcept { return layerPress; }
    /** How many of its menus (a layer's, an entry's) the editor has opened. */
    int getMenusShown() const noexcept { return menusShown; }
    /** The hint a key shows (always, in the title bar) while it is touched. */
    const juce::String& getHint() const noexcept { return hint; }
    /** Where the entry list's rows and keys are (editor coordinates), for the tests and the drag. */
    juce::Rectangle<int> getEntryRowBounds (int row) const;
    juce::Rectangle<int> getLayerRowBounds (int row) const;
    juce::Rectangle<int> getEntryHandleBounds (int row) const;
    std::vector<juce::Rectangle<int>> getKeyBounds() const;
    /** The list's rows, and the ticks of the scroll a drag held at an edge makes (the tests call it). */
    void scrollTick() { edgeScroll.tick(); }
    int getEntriesScroll() const;

    void selectLayer (juce::String id);
    const juce::String& getSelectedLayer() const noexcept { return layerId; }
    void selectEntry (int row) { entries.selectRow (row); }
    int getNumEntries() const;
    /** What an entry row says: "CH 3  Kick", "DCA 2", "(spacer)". */
    juce::String entryText (int row) const;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int) override {}

    struct Model : juce::ListBoxModel
    {
        LayerEditor& owner;
        explicit Model (LayerEditor& o) : owner (o) {}
        int getNumRows() override { return owner.getNumEntries(); }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    };

    void changed();
    void askName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done);
    void showMenu (juce::PopupMenu, juce::Rectangle<int> screenArea, std::function<void (int)> chosen);
    void showDcaMenu();
    void showBankMenu (juce::Rectangle<int> screenArea);
    void updateEdgeScroll();
    struct Drop
    {
        enum Kind { None, Move, Add, Remove } kind = None;
        int to = -1;
        juce::String layerId;
    };
    Drop dropFor (juce::Point<int>) const;
    int dragRow = -1;
    juce::Point<int> dragAt;
    DragPickup pickup;
    LongPress layerPress;
    EdgeScroll edgeScroll;
    juce::String hint;
    int menusShown = 0;

    UiContext& ctx;
    const Theme& theme;
    juce::String layerId;
    Model entryModel { *this };
    juce::ListBox layers { {}, this }, entries { {}, &entryModel };
    std::vector<std::unique_ptr<ToggleKey>> keys;
    ToggleKey* colourKey = nullptr;
    ToggleKey* dcaKey = nullptr;
    ToggleKey* bankKey = nullptr;
    std::shared_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayerEditor)
};

} // namespace livedaw::ui::universal
