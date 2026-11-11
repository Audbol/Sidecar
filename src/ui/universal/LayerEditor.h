#pragma once

#include "ui/UiContext.h"
#include "ui/universal/LayerDrag.h"
#include "ui/universal/Theme.h"
#include "ui/universal/Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace livedaw::ui::universal
{

/** The editor of the show's user layers (the same layers as the WING surface's
    USER layers): the layers on the left (NEW, RENAME, COLOUR, DELETE, DUPLICATE,
    EXPORT, IMPORT, BANK: which console bank it occupies), the selected layer's entries on the right (ADD
    SELECTED, ADD DCA, SPACER, REMOVE, UP, DOWN, and drag: a long press, or a mouse drag, picks an entry up;
    dropped between entries it moves there, on a layer in the left list it is added to that layer, outside
    the list it is taken out). A spacer is a blank slot: a gap in the view. An
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
    void showColourMenu();
    void showDcaMenu();
    void showBankMenu();
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
