#pragma once

#include "ui/PluginHostControl.h"
#include "ui/UiContext.h"
#include "ui/universal/BlockEditor.h"
#include "ui/universal/Widgets.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <memory>
#include <vector>

namespace livedaw::ui::ph
{

/** The docked plug-in window of the PLUGIN HOST's RACK page: the editor of one block of one channel's
    chain, inside the screen instead of in a window of its own.

    - A plug-in that runs in this process shows its own editor (a native view: it gets the size it asks
      for in real pixels, whatever the surface's scale, and a resizable one is made to fit; one that
      still does not fit says so and offers POP OUT). A plug-in without an editor shows generic controls.
    - A sandboxed plug-in runs in another process and its real editor is a window of that process, which
      cannot be embedded: the dock shows its generic controls and a clear "opens in its own window" panel
      with an OPEN key.
    - A built-in block (EQ, dynamics...) shows the Universal Mixer's block editor.
    - A block with several plug-ins (a chain) shows a key per plug-in.
    PIN keeps the dock on its block while the channel and the tile selection change. POP OUT opens the
    block's floating plug-in window (the Universal Mixer's EDIT).

    ASSIGN HOT 1 and ASSIGN HOT 2 (PLUGIN HOST hot knobs): tap one, then touch or move a control of the plug-in
    shown (its own editor, or the generic controls: a tap on a slider is enough); the parameter that was touched
    becomes the hot knob's. It listens to the plug-in's parameter gestures (the callbacks a plug-in makes when
    its editor starts a change), so it works for a plug-in's own editor, the generic controls and the sandbox's.
    While MIDI LEARN waits for a target the touched parameter is offered to it instead. Message thread only. */
class RackDock final : public juce::Component, private juce::Timer
{
public:
    RackDock (UiContext&, const universal::Theme&);
    ~RackDock() override;

    /** The hot knobs and MIDI learn (set by the surface). */
    void setControl (Control* c) { control = c; }
    /** Parameter `paramIndex` of the shown plug-in was touched (the plug-in's gesture callback; the tests). */
    void parameterTouched (int paramIndex);
    /** The hot knob being assigned (0, 1; -1: none), and its keys. */
    int getAssigning() const noexcept { return assigning; }
    universal::ToggleKey& getHotKey (int knob) noexcept { return knob == 0 ? hot1Key : hot2Key; }

    /** What the dock shows now. */
    enum class Content
    {
        Empty,    // no block selected
        NoPlugin, // a plug-in block with nothing loaded
        Editor,   // a plug-in's own editor, in this process
        Generic,  // a plug-in's generic controls (it has no editor)
        Sandbox,  // a sandboxed plug-in: its generic controls and the OPEN panel
        TooLarge, // a native editor larger than the dock that cannot be resized
        BuiltIn   // a built-in block's editor
    };

    /** Shows block `uid` of channel `channel` (0-based; uid < 0: nothing), unless the dock is pinned. */
    void follow (int channel, int uid);
    int getChannel() const noexcept { return channel; }
    int getUid() const noexcept { return uid; }
    Content getContent() const noexcept { return content; }
    /** The plug-in of the block's chain the dock shows (its editor), or nullptr. */
    juce::AudioProcessor* getShownPlugin() const noexcept { return shownPlugin; }

    bool isPinned() const noexcept { return pinned; }
    void setPinned (bool);

    /** `p` (a plug-in, or the chain that holds it; nullptr: every plug-in) is about to go: its editor is dropped. */
    void dropEditorFor (const juce::AudioProcessor* p);
    /** Looks again at what the block holds (the timer does this). */
    void refresh();

    /** The sandbox panel's OPEN key and the keys, for the tests. */
    universal::ToggleKey& getPinKey() noexcept { return pinKey; }
    universal::ToggleKey& getOpenKey() noexcept { return openKey; }
    universal::ToggleKey& getPopOutKey() noexcept { return popKey; }
    universal::ToggleKey& getChooseKey() noexcept { return chooseKey; }
    int getNumMemberKeys() const noexcept { return (int) memberKeys.size(); }
    void showMember (int index);
    int getMember() const noexcept { return member; }

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kHeader = 48, kHotKeyWidth = 148;

private:
    void timerCallback() override;
    void rebuild();
    void dropEditor();
    void place();
    void openSandboxWindow();
    void refreshHotKeys();
    void watch (juce::AudioProcessor*);
    void unwatch();
    Strip* strip() const;
    int slot() const;
    juce::String title() const;

    UiContext& ctx;
    const universal::Theme& theme;
    int channel = -1, uid = -1, member = 0;
    bool pinned = false;
    Content content = Content::Empty;
    juce::String message, detail, signature;
    juce::AudioPluginInstance* shownPlugin = nullptr;
    int shownSlot = -1;

    universal::ToggleKey pinKey, popKey, openKey, chooseKey, hot1Key, hot2Key;
    Control* control = nullptr;
    int assigning = -1;
    class Watcher;
    std::unique_ptr<Watcher> watcher;
    juce::AudioProcessor* watched = nullptr;
    std::vector<std::unique_ptr<universal::ToggleKey>> memberKeys;
    juce::Component holder;
    juce::Viewport viewport;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    universal::BlockEditor blockEditor;
    juce::Rectangle<int> body;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackDock)
};

/** The RACK page: the selected channel's chain as large tiles in signal order, the docked plug-in window
    (RackDock) at its right. A tile shows the block's name, maker, latency and a BYPASS key; a tap selects
    it (the dock follows), a drag moves it in the chain, a long press opens its menu (bypass, remove,
    copy, paste); the "+" tile adds a plug-in (the plug-in browser) or a built-in block. Message thread only. */
class RackPage final : public juce::Component, private juce::Timer
{
public:
    RackPage (UiContext&, const universal::Theme&);
    ~RackPage() override;

    /** The channel shown (0-based). */
    void setChannel (int index);
    int getChannel() const noexcept { return channel; }
    /** The tile selected (its block's uid; -1 none): what the hot knobs follow while this channel is the selected one. */
    int getSelectedUid() const noexcept { return selectedUid; }
    /** The selected plug-in as the hot knobs see it: the shown channel, the selected block, the dock's plug-in in it. */
    PluginRef getFocus() const;
    void setControl (Control* c) { dock.setControl (c); }
    /** Reads the chain again (the timer does this too). */
    void refresh();

    /** One tile: a block of the chain. */
    struct TileInfo
    {
        int uid = -1;
        BlockKind kind = BlockKind::Plugin;
        juce::String name, maker;
        int latency = 0;
        bool loaded = false, missing = false, faulted = false, bypassed = false, canBypass = false;
    };
    int getNumTiles() const noexcept { return (int) tiles.size(); }
    TileInfo getTile (int index) const;
    /** The tile's place on the canvas (the "+" tile is index getNumTiles()). */
    juce::Rectangle<int> getTileBounds (int index) const;
    int getSelectedTile() const noexcept { return selected; }
    void selectTile (int index);

    /** Moves tile `from` to before tile `before` (getNumTiles(): the end) in the channel's chain. */
    bool moveTile (int from, int before);
    bool removeTile (int index);
    bool setTileBypassed (int index, bool bypassed);
    bool addBlock (BlockKind kind);
    /** A new plug-in block holding `desc` (after the selected tile, else before the level); false if it could not be loaded. */
    bool addPlugin (const juce::PluginDescription& desc);
    void copyTile (int index);
    bool hasCopy() const noexcept { return clip.valid; }
    bool pasteTile();
    /** The "+" tile's menu (the plug-in browser and the block kinds), and a tile's menu. */
    void showAddMenu();
    void showTileMenu (int index);

    RackDock& getDock() noexcept { return dock; }
    /** What takes the touches (for the tests). */
    juce::Component& getCanvas() noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kLeftWidth = 920, kTileWidth = 272, kTileHeight = 168, kTileGap = 24, kColumns = 3, kHead = 44;

private:
    class Canvas;
    friend class Canvas;

    void timerCallback() override;
    Strip* strip() const;
    std::vector<ChainStep> blocks() const;
    void rebuildTiles();
    TileInfo describe (const ChainStep&) const;
    /** The chain step index a new block goes in (after the selected tile, else before PRE-FADER). */
    int insertionPoint() const;
    void selectUid (int uid);
    void followSelection();
    void reselect (int uid);
    std::atomic<bool>* switchOf (int uid) const;

    UiContext& ctx;
    const universal::Theme& theme;
    int channel = 0, selected = -1, selectedUid = -1;
    std::map<int, int> lastUid; // the block last selected on each channel
    std::vector<ChainStep> tiles;
    std::vector<TileInfo> infos;
    juce::String signature;

    /** A copied block: its kind, and for a plug-in block each plug-in with its state. */
    struct Clip
    {
        bool valid = false;
        BlockKind kind = BlockKind::Plugin;
        struct Member
        {
            juce::PluginDescription description;
            juce::MemoryBlock state;
        };
        std::vector<Member> members;
    } clip;

    juce::Viewport viewport;
    std::unique_ptr<Canvas> canvas;
    RackDock dock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackPage)
};

} // namespace livedaw::ui::ph
