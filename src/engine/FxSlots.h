#pragma once

#include "engine/Parameters.h"
#include "engine/PluginChain.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace livedaw
{

/** The FX slots of the EFFECTS screen: the WING's 16, or up to kMaxFx
    (getNumSlots()). Each slot runs one of the
    console's built-in devices (reverbs, delays, EQs, amps... implemented in
    Rust, model table 4) or, with the EXT model, a VST/AU/LV2 plugin; strips use
    a slot through their PRE or POST insert. Premium devices run in slots 1-8. The plugin instances live here (JUCE); the Rust engine calls
    them from the audio thread through a callback per slot, checks their output
    and auto-bypasses a plugin that throws or emits NaN/inf. A slot runs for one
    strip per block: two inserts pointing at the same slot are flagged as
    "double assigned" and the second one passes its signal through.

    Replaced plugins are destroyed on the message thread once the engine can no
    longer call them (the engine invokes the release callback from
    ld_engine_collect). Plugins run in stereo: a mono strip feeds both inputs and
    takes the left output back; a plugin with a mono main bus gets both sides
    mixed and feeds both (MonoBuses). A sandboxed plugin (SandboxedPlugin) that misses a
    block or loses its helper process is reported like one that throws; its latency
    includes the sandbox's block, so delay compensation covers it.

    Each plugin's latency (getLatencySamples) goes to the engine's plugin
    delay compensation: when the plugin is installed, whenever it reports a
    change (from any thread: only an atomic store) and on updateLatencies().

    An EXT slot may hold a serial chain of up to 8 plugins (a PluginChain, run as
    one plugin; a plugin on its own is a chain of one). addToChain(),
    removeFromChain() and moveInChain() build the new chain on the message thread
    and swap it in like setPlugin(); the plugins that stay keep their instance
    and state, and keep running. In a chain of several a failing plugin is
    faulted on its own (passed through) and the slot keeps running; see
    PluginChain.

    A plugin block of a strip's chain (ld::kBlockPlugin) is hosted by the same code: its slot
    number is kBlockSlotBase + its host id (blockSlotOf), and every call that takes a slot
    (setPlugin, getPlugin, the chain members, the show's plugin bridge, the plugin window)
    treats it like an FX slot's plugin. The engine counts its latency in the strip's
    delay compensation; a failing plugin bypasses only its block.

    Stateless view: any number of FxSlots may view one engine. */
class FxSlots
{
public:
    explicit FxSlots (ld::Engine* e) noexcept : engine (e) {}

    /** Slot numbers from here on are plugin blocks (ld::kPluginBlockSlotBase + the block's host id). */
    static constexpr int kBlockSlotBase = ld::kPluginBlockSlotBase;
    static bool isBlockSlot (int slot) noexcept { return slot >= kBlockSlotBase; }
    static uint32_t blockPid (int slot) noexcept { return (uint32_t) (slot - kBlockSlotBase); }
    static int blockSlot (uint32_t pid) noexcept { return kBlockSlotBase + (int) pid; }
    /** The slot number of plugin block `uid` of `strip` (-1: not a plugin block). */
    static int blockSlotOf (const ld::Strip* strip, int uid) noexcept
    {
        const auto pid = ld::ld_plugin_block_pid (strip, uid);
        return pid != 0 ? blockSlot (pid) : -1;
    }
    /** What a plugin block shows: the plugin's (or chain's) name and vendor, its latency; a plugin that
        could not be loaded keeps the name it was saved with (`missing`, the block bypassed). */
    struct BlockInfo
    {
        juce::String name, vendor;
        bool loaded = false, missing = false, faulted = false, bypassed = false;
        int latency = 0;
    };
    BlockInfo describeBlock (const ld::Strip* strip, int uid) const;
    /** The slot numbers of the plugin blocks that hold a plugin. */
    std::vector<int> blockSlots() const;
    /** A plugin block's bypass (the whole block; a plugin of its chain bypasses with setChainMemberBypassed). */
    bool isBlockBypassed (int slot) const noexcept { return ld::ld_plugin_block_state (engine, blockPid (slot), 2) == 1; }
    void setBlockBypassed (int slot, bool bypassed) { ld::ld_plugin_block_set_bypass (engine, blockPid (slot), bypassed); }

    /** The slots in use: the WING's 16 (kWingFx) to kMaxFx (SETUP > GENERAL > FX
        SLOTS). The screens list these; the engine holds kMaxFx. */
    int getNumSlots() const noexcept { return ld::ld_engine_num_fx (engine); }
    /** Message thread: within kWingFx..kMaxFx and never below a slot in use
        (isInUse), so no device, plugin or insert assignment is dropped.
        Returns the number set. */
    int setNumSlots (int count) { return ld::ld_engine_set_num_fx (engine, count); }
    /** Message thread: the slot holds something (a device, plugin or outboard
        insert) or a strip's insert is assigned to it. */
    bool isInUse (int slot) const noexcept { return ! isBlockSlot (slot) && ld::ld_engine_fx_in_use (engine, slot); }

    /** Message thread, device stopped: re-prepares the loaded plugins. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Message thread: installs `plugin` in `slot` (nullptr clears it). */
    void setPlugin (int slot, std::unique_ptr<juce::AudioPluginInstance> plugin);

    /** Message thread only. */
    juce::AudioPluginInstance* getPlugin (int slot) const noexcept;
    /** Message thread: the slot's plugin (or a plugin of its chain) runs in the plugin sandbox (a helper process). */
    bool isSandboxed (int slot) const noexcept;

    //==============================================================================
    // Plugin chains (message thread). Member indices are in processing order.

    /** The slot's PluginChain, or nullptr (no plugin, or a plugin on its own). */
    PluginChain* getChain (int slot) const noexcept;
    /** Plugins in the slot: 0, 1 for a plugin on its own, else the chain's size. */
    int getNumChainMembers (int slot) const noexcept;
    juce::AudioPluginInstance* getChainMember (int slot, int index) const noexcept;
    /** The same plugin, shared: whoever holds it keeps it alive (a scene's glide, see Show). */
    std::shared_ptr<juce::AudioPluginInstance> getChainMemberShared (int slot, int index) const noexcept;
    bool isChainMemberBypassed (int slot, int index) const noexcept;
    /** No latency while bypassed. Bypassing a plugin on its own makes it a chain
        of one. Bypassing on or off clears the plugin's fault. */
    void setChainMemberBypassed (int slot, int index, bool bypassed);
    /** The plugin failed in a chain of several and is passed through until cleared. */
    bool isChainMemberFaulted (int slot, int index) const noexcept;
    /** Un-faults the plugin: it runs again. */
    void clearChainMemberFault (int slot, int index);
    /** Plugins of the slot's chain that are faulted on their own (the slot isn't). */
    int getNumFaultedChainMembers (int slot) const noexcept;
    /** How the chain window shows a plugin (zoom, collapsed; saved with the show). */
    PluginChain::View getChainMemberView (int slot, int index) const noexcept;
    /** A view other than the default for a plugin on its own makes it a chain of one. */
    void setChainMemberView (int slot, int index, PluginChain::View);
    /** Whether the plugin can take a key (PLUGIN HOST): it is in a plugin block, runs in this process
        (a sandboxed plugin's buses are stereo only) and has a side-chain input bus. */
    bool chainMemberHasSidechain (int slot, int index) const noexcept;
    PluginChain::Key getChainMemberKey (int slot, int index) const noexcept;
    /** Keys the plugin's side chain from a channel's input or output (none: Key{}). A block has one key,
        so every plugin of its chain that has a side chain takes it. A plugin on its own becomes a chain
        of one. False when refused: the plugin has no side chain, or the channel would key itself after
        its own chain (the key then stays as it was). The engine delays the key of a channel that is
        processed after the keyed one by a block (see PluginChain). */
    bool setChainMemberKey (int slot, int index, PluginChain::Key);
    /** The plugin's hot knobs and MIDI maps (PLUGIN HOST; saved with it). */
    PluginChain::Control getChainMemberControl (int slot, int index) const noexcept;
    /** A plugin on its own that gets any becomes a chain of one. */
    void setChainMemberControl (int slot, int index, PluginChain::Control);
    /** Inserts `plugin` at `index` (-1: the end). False (and `plugin` dropped) if
        the slot has no plugin yet or its chain is full. */
    bool addToChain (int slot, std::unique_ptr<juce::AudioPluginInstance> plugin, int index = -1);
    /** Removes a member (the last one empties the slot). It is destroyed once
        the engine can no longer call it: close its editor first. */
    bool removeFromChain (int slot, int index);
    /** Moves a member to position `to`. */
    bool moveInChain (int slot, int from, int to);

    /** The slot's model (index into model table 4: kFxNone, kFxExt or a device). */
    int getModel (int slot) const noexcept { return isBlockSlot (slot) ? ld::kFxExt : params (slot).model.load(); }
    /** Selects a built-in device (removing a plugin first). False if not allowed in the slot. */
    bool setModel (int slot, int model);

    //==============================================================================
    // The EXT device's outboard insert (WING manual 11.11 p. 164, Fig. 169): with
    // SOURCE OUTBOARD and no plugin, the slot sends its input (TRIM, FX MODE) to
    // its FX SEND outputs (params().sendLeft / sendRight, patched on ROUTING >
    // OUTPUTS) and returns the RETURN inputs, delayed for LATENCY plus the
    // interface's round trip like a plugin's latency.

    /** EXT with SOURCE OUTBOARD (removing a plugin first; the EXT settings stay). */
    void setOutboard (int slot);
    bool isOutboard (int slot) const noexcept { return ! isBlockSlot (slot) && ld::ld_fx_state (engine, slot, 4); }
    /** An outboard insert with a RETURN (it runs; without one it passes through). */
    bool hasReturn (int slot) const noexcept { return ! isBlockSlot (slot) && ld::ld_fx_state (engine, slot, 5); }
    /** The RETURN as the console shows it: "LCL 3/4", "A 5", "OFF". */
    juce::String returnLabel (int slot) const;
    /** Whether `model` may run in `slot` (premium devices need slots 1-8). */
    static bool isAllowed (int model, int slot);

    /** The slot is in use (a plugin, an outboard insert or a built-in device). */
    bool isLoaded (int slot) const noexcept { return hasPlugin (slot) || isOutboard (slot) || getModel (slot) > ld::kFxExt; }
    bool hasPlugin (int slot) const noexcept { return isBlockSlot (slot) ? ld::ld_plugin_block_state (engine, blockPid (slot), 0) == 1 : ld::ld_fx_state (engine, slot, 0); }
    bool hasDevice (int slot) const noexcept { return ! isBlockSlot (slot) && ld::ld_fx_state (engine, slot, 3); }
    /** Gain reduction of the slot's device (dB, <= 0). */
    float getMeter (int slot) const noexcept { return ld::ld_fx_meter (engine, slot); }
    /** Live gain of a dynamic band of the slot's device (dB, signed). */
    float getBandGain (int slot, int band) const noexcept { return ld::ld_fx_band_gain (engine, slot, band); }
    /** The slot's live meters into `out` (up to `n`; returns how many): ld::kFxLevels levels
        (input L/R, output L/R before FX MIX, or the outboard insert's FX SEND and RETURN;
        linear, falling at 60 dB/s), then ld::kFxReadouts readouts of its device (laid out
        per device, FxAlgo::readouts; 0 = idle). Any thread. */
    int getReadouts (int slot, float* out, int n) const noexcept { return ld::ld_fx_readouts (engine, slot, out, n); }
    bool isFaulted (int slot) const noexcept { return isBlockSlot (slot) ? ld::ld_plugin_block_state (engine, blockPid (slot), 1) == 1 : ld::ld_fx_state (engine, slot, 1); }
    bool isDoubleAssigned (int slot) const noexcept { return ! isBlockSlot (slot) && ld::ld_fx_state (engine, slot, 2); }
    /** Clears the slot's fault and double assignment, and its chain's faulted plugins. */
    void clearFlags (int slot);
    /** Share of the block time the plugin took (smoothed). */
    float getLoad (int slot) const noexcept { return isBlockSlot (slot) ? 0.0f : ld::ld_fx_load (engine, slot); }
    /** Number of strip inserts using the slot. */
    int getNumUsers (int slot) const noexcept { return isBlockSlot (slot) ? 0 : ld::ld_fx_num_users (engine, slot); }
    /** The slot's latency in samples: its plugin's, its outboard insert's, or its built-in
        device's (the PRECISION LIMITER's look-ahead; 0 otherwise). */
    int getLatency (int slot) const noexcept { return isBlockSlot (slot) ? juce::jmax (0, ld::ld_plugin_block_state (engine, blockPid (slot), 3)) : ld::ld_fx_latency (engine, slot); }

    /** Message thread: passes every plugin's current latency to the engine (a
        safety net for plugins that change it without telling their host).
        Returns true if one changed. */
    bool updateLatencies();

    /** An FX slot's settings (never a plugin block's: they have none). */
    FxParams& params (int slot) const noexcept { return ld::ld_engine_controls (engine)->fx[juce::jlimit (0, kMaxFx - 1, slot)]; }

    /** "FX 3: Hall Reverb", "FX 3: TAL-Reverb" or "FX 3" for an empty slot. */
    juce::String describe (int slot) const;

private:
    struct Holder;
    Holder* holderAt (int slot) const noexcept;
    /** Tells the engine the key of the plugin block `slot` holds now (its chain's; none otherwise). */
    void syncKey (int slot);
    /** `rearranged`: `plugin` is a chain of the slot's plugins (added to, moved, removed from), so
        a faulted (auto-bypassed) slot stays faulted until the fault is cleared. */
    void install (int slot, std::shared_ptr<juce::AudioPluginInstance> plugin, bool rearranged = false);
    /** The slot's plugins as chain members (wrapping a plugin on its own). */
    std::vector<PluginChain::MemberPtr> members (int slot) const;

    ld::Engine* engine;
};

} // namespace livedaw
