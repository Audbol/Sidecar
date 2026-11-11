#pragma once

#include "ui/UiContext.h"
#include "ui/universal/ChannelPanel.h"
#include "ui/universal/Groups.h"
#include "ui/universal/LayerDrag.h"
#include "ui/universal/LayerEditor.h"
#include "ui/universal/Widgets.h"

#include "ui/universal/BlockEditor.h"
#include "ui/universal/BlockPicture.h"

#include <array>
#include <map>
#include <memory>
#include <vector>

namespace livedaw::ui::universal
{

/** How every strip is divided (the same for all, kept in the settings): a height for the
    meters and each kind of block, how wide a strip is. A section at its least
    collapses to a labelled bar. */
struct StripLayout
{
    enum Section
    {
        Meters,
        kNumSections
    };
    static constexpr int kCollapsed = 18;
    static constexpr const char* kSetting = "ui.universalStripSections";
    std::array<int, kNumSections> heights { 64 };
    std::map<int, int> blocks; // by blockKey
    int widthMode = 1;         // 0 narrow, 1 normal, 2 wide

    int columnWidth() const noexcept { return widthMode == 0 ? 68 : widthMode == 2 ? 124 : sizes::kColumnWidth; }
    static int blockKey (const ChainStep& s) noexcept { return (int) s.kind * 1000 + (s.isBlock() ? s.arg : 0); }
    static int defaultBlockHeight (const ChainStep&) noexcept;
    int blockHeight (const ChainStep&) const;
    /** Below half of the way to its minimum: collapsed. */
    void setSection (int section, int height) noexcept;
    void setBlock (int key, int height);
    void reset();
    juce::String toString() const;
    void fromString (const juce::String&);
};

/** A strip's body: the blocks of its chain (Strip::getChain), in order down the strip so that
    reading down it reads the signal path, each named with its model and tag and showing its curve
    or its gain reduction. It follows the chain as blocks are added, removed or moved; a tap shows
    the block in the inspector; a block's lower edge drags to resize it (on every strip), and when the
    blocks don't all fit the body scrolls. */
class StripBody final : public juce::Component
{
public:
    StripBody (UiContext&, Strip&, const Theme&);
    void setLayout (const StripLayout*);
    void refresh();
    int getNaturalHeight() const;
    int getNumBlocks() const noexcept { return (int) blocks.size(); }
    const ChainStep& getStep (int index) const { return blocks[(size_t) index].step; }
    juce::String getBlockName (int index) const { return blocks[(size_t) index].name; }
    std::function<void (const ChainStep&)> onOpen;
    std::function<void (int key, int height)> onResize;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Block
    {
        ChainStep step;
        BlockTarget target;
        juce::String name, model;
        int key = 0;
        BlockPicture picture;
        float reduction = 0.0f;
        bool on = false;
    };
    void rebuild (const std::vector<ChainStep>&);
    bool blockOn (const Block&) const;
    int heightOf (const Block&) const;
    int blockAt (int y) const;
    int dividerAt (int y) const;
    void setScroll (int);

    UiContext& ctx;
    Strip& strip;
    const Theme& theme;
    const StripLayout* layout = nullptr;
    std::vector<Block> blocks;
    std::vector<ChainStep> shownChain;
    std::vector<double> freqs;
    int scroll = 0, dragScroll = 0, dragDivider = -1, dragHeight = 0, tick = 0;
    TouchGesture gesture;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripBody)
};

/** A strip of the MIXER view, top to bottom along the signal: a meter bridge, its blocks
    (StripBody), its pan, the name tab (a tap selects the strip), then in the tray
    MUTE, SOLO, the fader and its value. Without the fader the blocks take its room. */
class StripColumn final : public juce::Component
{
public:
    ~StripColumn() override;
    StripColumn (UiContext&, Strip&, const Theme&);
    /** Shows or hides the fader (the blocks take its room). */
    void setFaderShown (bool);
    bool isFaderShown() const noexcept { return faderShown; }

    Strip& getStrip() const noexcept { return strip; }
    /** Takes the strip's values and meters (the view's timer calls it). */
    void refresh();
    /** SENDS ON FADER: the fader moves this strip's send to `destination` and sendKey is the send's ON
        (a strip with no such send: the fader is out of use); nullptr: its own fader again. */
    void setSendDestination (Strip* destination);
    Strip* getSendDestination() const noexcept { return sendDestination; }
    /** Opens a step in the inspector (the SENDS page). */
    std::function<void (Strip&, int stepKind, int uid)> onOpenStep;
    void setSelected (bool shouldBeSelected);
    bool isSelected() const noexcept { return selected; }

    ToggleKey muteKey, soloKey;
    PanControl pan;
    FaderControl fader;
    MeterControl meter;
    StripBody body;
    /** How strips are divided (the view's, shared by its columns). */
    void setLayout (const StripLayout*);
    /** A block tapped: the strip selected, the block in the inspector. */
    std::function<void (Strip&, const ChainStep&)> onOpenBlock;
    /** A divider dragged: the section's new height, for every strip. */
    std::function<void (int section, int height)> onResizeSection;
    std::function<void (int key, int height)> onResizeBlock;
    Strip& getStrip() noexcept { return strip; }
    /** SENDS ON FADER: the send's ON, named after its destination. */
    ToggleKey sendKey;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    class Divider;
    juce::Rectangle<int> header() const noexcept;
    std::array<juce::Rectangle<int>, StripLayout::kNumSections> sections;
    std::array<std::unique_ptr<Divider>, StripLayout::kNumSections> dividers;
    const StripLayout* layout = nullptr;
    juce::Rectangle<int> nameArea, readoutArea, trayArea, panArea;

    UiContext& ctx;
    Strip& strip;
    const Theme& theme;
    juce::String name;
    int colour = 0;
    float shownDb = 0.0f;
    bool selected = false, faderShown = true;
    Strip* sendDestination = nullptr;
    std::atomic<float>* sendLevel = nullptr;
    std::atomic<bool>* sendOn = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripColumn)
};

/** The MIXER view: a column per strip, every kind and any number of each (the
    console's configuration), then a column per DCA and the mute groups' keys,
    side by side and scrolled sideways, with keys that show one kind at a time
    (DCAS: the DCAs and the mute groups). The first main stays in view at the
    right whatever is scrolled. SOF puts a bus, main or matrix on the faders
    (the shell's Sends on Fader, so a custom control's key does it too). A DCA's
    Spill members shows only its strips.

    A console with more columns than the view holds (a DM2000's 96 channels, a
    dLive's 128) gets a row of bank keys under the filter keys: the columns in
    runs of 8, 16 or more by kind ("CH 1-16", "BUS 1-8", "DCA 1-8"), the one at
    the left of the view lit; a tap brings its first column there. JUMP opens a
    list of every strip by label and name with a search line: type a number
    ("12"), a label ("bus 3", "m2") or part of a name, Up / Down pick, Return
    goes there and selects the strip, Esc closes. The keyboard (handleKey):
    Ctrl+J or / opens the list, Page Up / Page Down step through the banks,
    Home / End go to the first and last, Left / Right select the strip before
    or after the selected one. */
class MixerView final : public juce::Component, private juce::Timer
{
public:
    MixerView (UiContext&, const Theme&);
    ~MixerView() override;

    /** The strips' controls (the filter, JUMP, BANK, SOF, CHANNEL, FADERS) as the window's header shows them:
        the owner makes them its children (the header is the window's), places them with layoutHeader. */
    std::vector<juce::Component*> getHeaderParts();
    /** Places the header's controls in `area` (the owner's coordinates), the filter as keys where they fit,
        else as one menu key; `visible` false hides them (another view is shown). */
    void layoutHeader (juce::Rectangle<int> area, bool visible);
    void setScrollX (int x) { viewport.setViewPosition (x, 0); }
    /** The window places the header again (the filter or the banks changed). */
    std::function<void()> onHeaderChanged;
    /** The MIXER's filter (-1 for all). */
    /** -1: every kind; a StripKind; or kDcas. */
    void setFilter (int kind);
    static constexpr int kDcas = kNumStripKinds;
    /** Shows only the strips of the show's user layer `id`, in its order (a spacer is a gap the width of a column);
        the filter keys and ALL work within it. Empty (or an id that is gone): every strip again. The layers are the
        WING surface's user layers (ctx.show.layers). */
    void setLayer (juce::String id); // by value: the caller may pass a reference into the layers snapshot
    const juce::String& getLayer() const noexcept { return layerId; }
    /** The layers' editor, over the columns. */
    void setLayerEditorShown (bool);
    bool isLayerEditorShown() const noexcept { return layerEditor != nullptr && layerEditor->isVisible(); }
    LayerEditor* getLayerEditor() const noexcept { return layerEditor.get(); }
    /** The header's keys for the layers: how many layer keys show, or true when they folded into one menu key. */
    int getNumLayerKeys() const noexcept { return (int) layerKeys.size(); }
    bool areLayersInMenu() const noexcept { return layersInMenu; }

    //==============================================================================
    // Drag editing of the layers, on the view itself. A long press (or, with a mouse, a drag) on a strip's or
    // DCA's name tab picks it up: dropped between two columns of the shown layer it moves there, on a layer's
    // key (or, with the keys folded into the menu, on LAYER) it is added to that layer, anywhere else (in a
    // layer) it is taken out. Everything goes through the show's layers, so the other surfaces follow.
    /** Moves the shown layer's entry `from` to between entries `to - 1` and `to` (slot numbers of the layer). */
    bool dragMoveEntry (int from, int to);
    /** Adds a strip or DCA to a layer (not when it already holds it). */
    bool dragAddToLayer (const state::FaderRef&, const juce::String& layerId);
    /** Takes the shown layer's entry `slot` out of it (the entries after it close up). */
    bool dragRemoveEntry (int slot);
    /** The pointer's drag as the gestures make it: pick up (a layer's entry `slot`, or -1 from ALL), move, drop;
        points are in the view's coordinates. endDrag returns whether anything was edited. */
    void beginDrag (const state::FaderRef&, int slot, const juce::String& label, juce::Point<int> at);
    void moveDrag (juce::Point<int> at);
    bool endDrag (juce::Point<int> at);
    bool isDragging() const noexcept { return drag.active; }
    /** Where the layer's entry `slot` is shown (view coordinates; empty when it isn't, or no layer is). */
    juce::Rectangle<int> getLayerEntryBounds (int slot) const;
    /** The key of the user layer `id` (nullptr: none shown, or folded into the menu). */
    ToggleKey* getLayerKey (const juce::String& id) const;
    /** Only DCA `dca`'s members and its fader (a filter key goes back). */
    void setDcaSpill (int dca);
    /** The DCA being spilled, or -1. */
    int getSpilledDca() const noexcept { return filter == kSpill ? spillDca : -1; }
    /** The strip kept in view at the right (null with the MAINS filter: they are all there). */
    StripColumn* getPinnedMain() const noexcept { return pinnedMain; }
    /** SENDS ON FADER, its destination (nullptr: off). */
    void setSendsOnFader (Strip* destination);
    /** The destination on the faders now: the one chosen, else the selected bus, main or matrix. */
    Strip* getSofDestination() const;
    /** The shell's Sends on Fader changed. */
    void sendsOnFaderChanged();
    /** A tap on a strip's overview or the channel panel: its strip, the chain step to open. */
    std::function<void (Strip&, int stepKind, int uid)> onOpenStep;
    /** The FADERS key: the columns show their faders, or only their overviews. */
    void setFadersShown (bool);
    bool areFadersShown() const noexcept { return fadersShown; }
    /** The CHANNEL key: the selected strip's panel (ChannelPanel) across the top. */
    void setChannelShown (bool);
    bool isChannelShown() const noexcept { return channelShown; }
    ChannelPanel& getChannelPanel() noexcept { return channel; }
    void visibilityChanged() override { channel.updateRta(); }
    int getFilter() const noexcept { return filter; }
    /** The strips changed (the view also notices by itself). */
    void rebuild();
    void selectionChanged();
    /** Takes every column's values and meters (the timer does this 30 times a second). */
    void refresh();

    int getNumColumns() const noexcept { return (int) columns.size(); }
    /** The strips' sections and width, the same for all; they are kept in the settings. */
    StripLayout& getStripLayout() noexcept { return layout; }
    void applyStripLayout();
    StripColumn* getColumn (int index) const noexcept;
    int getNumDcaColumns() const noexcept { return (int) dcaColumns.size(); }
    DcaColumn* getDcaColumn (int index) const noexcept;
    /** The mute groups' panel (null while the filter hides the DCAs). */
    MuteGroupsPanel* getMuteGroups() const noexcept { return muteGroups.get(); }

    /** A run of scrolled columns of one kind (a StripKind, kDcas, or kMuteGroupsBank for the mute groups' panel). */
    struct Bank
    {
        int kind = 0, first = 0, count = 0;
        juce::String label; // "CH 1-16"
        int x = 0;          // its first column, in the scrolled content
    };
    static constexpr int kMuteGroupsBank = kDcas + 2;
    int getNumBanks() const noexcept { return (int) banks.size(); }
    const Bank* getBank (int index) const noexcept { return juce::isPositiveAndBelow (index, (int) banks.size()) ? &banks[(size_t) index] : nullptr; }
    /** The bank keys show while the columns don't all fit and there is more than one bank. */
    bool isBankBarShown() const noexcept;
    /** Scrolls bank `index`'s first column to the left of the view. */
    void showBank (int index);
    /** The bank at the left of the view (-1: no banks). */
    int getCurrentBank() const noexcept;
    /** The bank before (-1) or after (1) the current one. */
    void stepBank (int delta);
    /** Where the columns are scrolled to (for the tests). */
    int getScrollX() const noexcept { return viewport.getViewPositionX(); }

    /** The strips a search finds, the best first: a number is a strip's number ("12"), letters and a number its
        label ("bus 3", "m2"), anything else part of a name or label. Empty: every strip, in the console's order. */
    std::vector<Strip*> findStrips (const juce::String& query) const;
    /** Selects `strip` and brings its column to the left of the view (every kind again if the filter left it out). */
    void jumpTo (Strip& strip);
    /** The JUMP list, over the columns. */
    void setJumpListShown (bool);
    bool isJumpListShown() const noexcept;
    /** The JUMP list's search line and what it lists, as typing and the keys do (for the tests too). */
    void setJumpQuery (const juce::String&);
    int getNumJumpRows() const noexcept;
    Strip* getJumpRow (int row) const noexcept;
    /** Up / Down in the list, and Return on its highlighted row. */
    void moveJumpHighlight (int delta);
    void chooseJumpRow();
    /** The strip before (-1) or after (1) the selected one, among the columns shown. */
    void selectNeighbour (int delta);
    /** The view's keys (see above); true if used. */
    bool handleKey (const juce::KeyPress&);
    ToggleKey& getJumpKey() noexcept { return jumpKey; }
    void paintOverChildren (juce::Graphics&) override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override
    {
        if (isVisible())
            refresh();
    }
    void layoutColumns();
    /** The strips on the faders follow the Sends on Fader state (only when it changed, unless `force`). */
    void applySendsOnFader (bool force);
    void showSofMenu();
    void connect (StripColumn&);
    /** Scrolls the selected strip into view when the selection moved. */
    void revealSelection();

    static constexpr int kSpill = kDcas + 1;

    UiContext& ctx;
    const Theme& theme;
    int filter = -1, spillDca = 0;
    int structureVersion = -1;
    std::vector<std::unique_ptr<ToggleKey>> filterKeys;
    ToggleKey fadersKey, channelKey, sofKey, jumpKey, filterMenuKey, bankKey;
    void showFilterMenu();
    void showBankMenu();
    void showLayerMenu();
    /** The layer keys again when the show's layers changed (compared by a signature). */
    void updateLayers (bool force);
    juce::String layerId, layersSignature;
    std::vector<juce::String> layerKeyIds;
    struct Drag
    {
        bool active = false;
        state::FaderRef ref;
        int slot = -1;
        juce::String label;
        juce::Point<int> at;
    } drag;
    struct Drop
    {
        enum Kind
        {
            None,
            Reorder,
            Add,
            Remove,
            Menu
        } kind = None;
        int to = -1;
        juce::String layerId;
    };
    Drop dropFor (juce::Point<int>) const;
    void showAddMenu (const state::FaderRef&);
    DragPickup pickup;
    void makePickable (juce::Component&);
    bool layersInMenu = false;
    ToggleKey layerMenuKey, layerEditKey;
    std::vector<std::unique_ptr<ToggleKey>> layerKeys;
    std::unique_ptr<LayerEditor> layerEditor;
    /** The columns in a layer's order (a spacer: both null). */
    struct Placed
    {
        StripColumn* strip = nullptr;
        DcaColumn* dca = nullptr;
        int slot = -1; // its place in the layer's faders
    };
    std::vector<Placed> layerOrder;
    void updateBankKey();
    StripColumn* pinnedMain = nullptr;
    /** The pinned main when the filter leaves it out of the columns. */
    std::unique_ptr<StripColumn> extraPinned;
    juce::Rectangle<int> pinnedArea;
    Strip* appliedSof = nullptr;
    Strip* revealed = nullptr;
    bool fadersShown = true, channelShown = true;
    ChannelPanel channel;
    int ticks = 0;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<StripColumn>> columns;
    std::vector<std::unique_ptr<DcaColumn>> dcaColumns;
    std::unique_ptr<MuteGroupsPanel> muteGroups;
    class BankBar;
    class JumpList;
    void buildBanks();
    std::vector<Bank> banks;
    std::unique_ptr<BankBar> bankBar;
    std::unique_ptr<JumpList> jumpList;
    StripLayout layout;
    int shownScrollX = -1, bankHint = -1, bankHintX = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};

/** Undo and redo of the mix: every strip's fader, pan, mute, solo and sends. It watches the
    values, so whatever moved them counts (a control, a scene, a remote): tick() takes a change
    once it has stopped moving, so a fader's whole travel is one step. */
class MixerHistory final
{
public:
    explicit MixerHistory (MixerEngine& e) : engine (e) {}

    void tick (juce::uint32 nowMs);
    bool undo();
    bool redo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }

    static constexpr juce::uint32 kSettleMs = 300;
    static constexpr size_t kDepth = 100;

private:
    template <typename Visit>
    void visit (Visit&&);
    std::vector<float> capture();
    void apply (const std::vector<float>&);

    MixerEngine& engine;
    std::vector<float> stable, latest;
    std::vector<std::vector<float>> undoStack, redoStack;
    juce::uint32 settledAt = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerHistory)
};

/** The main output's two meters and its level, for the top bar. */
class OutputBridge final : public juce::Component, private juce::Timer
{
public:
    OutputBridge (UiContext&, const Theme&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    UiContext& ctx;
    const Theme& theme;
    std::array<float, 2> levels {};
    float held = -120.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputBridge)
};

} // namespace livedaw::ui::universal
