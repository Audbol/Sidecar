#pragma once

#include "engine/ConsoleState.h"
#include "engine/Layers.h"
#include "engine/PluginChain.h"
#include "engine/PluginHost.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace livedaw
{

/** A show: the console setup plus an ordered list of scenes, each of which can
    hold snippets. Implemented in Rust (crate livedaw-session); this class is the
    C++ interface and hosts the plugins the session refers to.

      Show     - the whole file: console structure (channels, buses, FX slot
                 plugins, patching), DCAs, mute groups, fader banks and every scene.
      Scene    - a snapshot of the console. Recalling it applies the sections in
                 its recall scope to every strip that isn't recall-safe. It can
                 glide faders, pans and send levels over a fade time.
      Snippet  - a partial snapshot that belongs to a scene: chosen strips x chosen
                 sections. Fire it on its own, or mark it to fire automatically
                 straight after its scene is recalled.
      Item     - as on the WING (manual 5.6 p. 52, fig. 45), a scene can also be
                 a file added to the show: a snippet, an audio clip (played on
                 the USB player), an FX preset (into an FX slot) or a channel
                 preset (onto a strip). Recalling it loads the file.

    Scene and snippet recall never create or destroy channels or plugins, so
    recalling during a show can't cause dropouts.

    Message thread only.
*/
class Show final : private juce::Timer
{
    MixerEngine& engine;
    PluginHost& host;
    ld::Show* handle; // declared before `layers`, which views the show's layer set

public:
    /** What a scene is: a console snapshot, or a file added to the show. */
    enum class ItemKind
    {
        Snapshot,
        Snippet,
        Clip,
        Fx,
        Channel
    };

    /** A copy of a scene's settings (change them with the setters below). */
    struct Scene
    {
        ItemKind item = ItemKind::Snapshot;
        juce::String origin; // an item's file name as added (shown with its type once renamed)
        juce::String target; // where an item loads: the clip's path, "FX 3", "ch:4"
        juce::String name, notes;
        juce::uint32 scope = 0;
        double fadeSeconds = 0.0;
        bool glide = false;    // GLIDE (PLUGIN HOST): plug-ins reach the scene's parameter values over the fade time; else SNAP
        int numSnippets = 0;
        int tag = 0;           // EDIT TAG #1..#128 (0 = none)
        bool skip = false;     // SKIP: GO PREV/NEXT and recalls pass over it
        bool linkNext = false; // LINK: recalling it also recalls the scene below
        juce::String midi;     // SEND MIDI: MIDI data sent when it's recalled (see MidiRemote::parseMidiText)
    };

    static constexpr int maxSceneTag = 128;

    /** A copy of a snippet's settings. */
    struct Snippet
    {
        juce::String name;
        juce::uint32 sections = 0;
        std::vector<state::StripRef> strips;
        bool fireWithScene = false;
        /** Its whole scope (strips, sections, DCAs, mute groups, FX, console). */
        state::Scope scope = state::Scope::none();
        /** The parameters REC recorded, else the values it holds. */
        int parameterCount = 0;
    };

    Show (MixerEngine&, PluginHost&);
    ~Show() override;

    juce::String getName() const { return takeString (ld::ld_show_name (handle)); }
    void setName (const juce::String&);

    /** Fader banks of the three sections (default + user banks), saved with the show. */
    LayerSet layers;

    //==============================================================================
    // Scenes
    int getNumScenes() const noexcept { return ld::ld_show_num_scenes (handle); }
    /** A copy (const, so that writing to it by mistake doesn't compile: use the setters). */
    const std::optional<Scene> getScene (int index) const;
    int getCurrentSceneIndex() const noexcept { return ld::ld_show_current_scene (handle); }

    // Editing a scene or snippet's settings doesn't call onChanged (the editor
    // that made the change updates itself).
    void setSceneName (int index, const juce::String&);
    void setSceneNotes (int index, const juce::String&);
    void setSceneScope (int index, juce::uint32 scope);
    void setSceneFadeSeconds (int index, double seconds);
    /** PLUGIN HOST: how a recall brings a plug-in's state in. SNAP (the default) applies it at once. GLIDE
        moves the plug-ins' continuous parameters (those that are not stepped; plug-ins in the sandbox have
        none that can) from where they are to the scene's values over the scene's fade time, and applies the
        whole state at the end; stepped parameters take the scene's value when the recall starts. Needs a fade
        time above 0 and plug-ins that run in this process. Stays on the message thread (see advanceGlides). */
    void setSceneGlide (int index, bool glide);
    /** 1..128; anything else clears the tag. */
    void setSceneTag (int index, int tag);
    void setSceneSkip (int index, bool skip);
    /** The link follows the scene: after a move it links to its new neighbour below. */
    void setSceneLink (int index, bool linkNext);
    /** SEND MIDI (the EDIT MIDI TEXT STRING window). */
    void setSceneMidi (int index, const juce::String&);
    /** The first scene tagged `tag` that isn't skipped, or -1. */
    int findSceneByTag (int tag) const noexcept { return ld::ld_show_find_scene_by_tag (handle, tag); }

    /** Captures the current console as a new scene (appended, or inserted at `index`). */
    int addScene (const juce::String& sceneName, int index = -1);
    void storeScene (int index);
    /** A ProMix 01's scene memory `memory` (1-50, scene `memory - 1`) as scene bytes,
        without recalling it (empty: no such scene, or no mix in it). */
    std::vector<uint8_t> getProMixSceneBytes (ld::LdProMix*, int memory) const;
    /** Stores scene bytes (a bulk dump) into scene memory `memory` without recalling it. */
    bool storeProMixSceneBytes (ld::LdProMix*, int memory, const std::vector<uint8_t>& bytes);
    /** A 01V's scene memory `memory` (1-99) as scene bytes, without recalling it. */
    std::vector<uint8_t> getY01VSceneBytes (ld::LdY01V*, int memory) const;
    bool storeY01VSceneBytes (ld::LdY01V*, int memory, const std::vector<uint8_t>& bytes);
    void removeScene (int index);
    void moveScene (int from, int to);

    /** Recalls a scene (and its auto-fire snippets), then the scenes it links to.
        A skipped scene is not recalled. The current scene becomes the last one recalled. */
    state::ApplyResult recallScene (int index);
    /** GO NEXT / GO PREV: the next / previous scene that isn't skipped. */
    state::ApplyResult recallNext();
    state::ApplyResult recallPrevious();
    /** Recalls the first non-skipped scene tagged `tag` (nothing if none). */
    state::ApplyResult recallSceneByTag (int tag);
    /** RECALL UNDO (the 01V's, the dLive's Undo Go): the console as it was before
        the last scene recall, what that recall changed and no more (its recall
        filter and safes kept it from the rest). Called again it redoes the recall.
        Not one of the show's scenes and never saved. */
    state::ApplyResult undoRecall();
    /** Whether a recall can be undone (none yet, or the show or console changed: false). */
    bool canUndoRecall() const noexcept;
    /** Takes that step back for a recall done outside the show's scenes (the 01V's scene 00), just before it. */
    void markRecallUndo();

    //==============================================================================
    // Show items (manual 5.6 p. 52): ADD ITEM TO SHOW and SAVE + ADD

    /** The file types ADD ITEM TO SHOW takes: scene and snippet files, clips, FX and channel presets. */
    static bool canAddToShow (const juce::File&);
    /** ADD ITEM TO SHOW: a scene file is imported; a snippet file, clip, FX preset (loading into FX
        slot `fxSlot`, 0-based) or channel preset (loading onto `strip`) becomes a scene of its own,
        named after the file, at `index` (-1: appended). Its index, or -1 with `error`. */
    int addItem (const juce::File&, int index, int fxSlot, const state::StripRef& strip, juce::String& error);
    /** SAVE + ADD on SNIP: a snippet as a scene of its own at `index` (recorded: the parameters REC /
        STOP recorded within `scope`, nullptr = all; else what `scope` covers). -1 if nothing to save. */
    int addSnippetScene (const juce::String& snippetName, const state::Scope* scope, bool recorded, int index);

    //==============================================================================
    // STARTUP (manual 5.6 p. 52 and chapter 10): files named STARTUP* in the STARTUP folder

    /** Loads a scene or snippet file onto the console without adding it to the show. */
    state::ApplyResult applyLibraryFile (const juce::File&, juce::String& error);

    struct StartupResult
    {
        juce::File showFile;           // the STARTUP show opened (Sidecar's), if any
        juce::StringArray loaded, errors;
    };
    /** The STARTUP folder: <library>/STARTUP. */
    static juce::File startupFolder (const juce::File& libraryFolder) { return libraryFolder.getChildFile ("STARTUP"); }
    /** Loads the folder's STARTUP* files: a show first (Sidecar's .lshow, else a WING .show whose
        snapshots become scenes), then a scene file (.lsnap, or a WING .snap) and a snippet file
        (.lsnip) onto the console; of each type the first by name. */
    StartupResult loadStartupFolder (const juce::File& folder, bool loadPlugins);

    //==============================================================================
    // Scopes beyond sections, GLOBAL safes, PARTIAL LOAD / UPDATE, RESTORE

    /** A scene's whole recall scope (its sections are Scene::scope). */
    state::Scope getSceneRecallScope (int index) const;
    void setSceneRecallScope (int index, const state::Scope&);
    /** PARTIAL UPDATE: stores only `scope` into the scene. */
    void storeScenePartial (int index, const state::Scope&);
    /** PARTIAL LOAD: recalls only `scope` of the scene (noGlobal: NO GLOB, the
        safes are ignored). No links, auto-fire snippets or fade. */
    state::ApplyResult recallScenePartial (int index, const state::Scope&, bool noGlobal);
    /** RESTORE: the whole scene regardless of its scope, safes included. */
    state::ApplyResult restoreScene (int index);
    /** The scope PARTIAL LOAD / UPDATE last used (they keep it). */
    state::Scope getPartialScope() const;
    /** GLOBAL safes beyond the strips' recall safe (strips are not used). */
    state::Scope getGlobalSafe() const;
    void setGlobalSafe (const state::Scope&);
    bool hasGlobalSafes() const;

    //==============================================================================
    // Snippets
    const std::optional<Snippet> getSnippet (int sceneIndex, int snippetIndex) const;
    int addSnippet (int sceneIndex, const juce::String& snippetName, juce::uint32 sections,
                    const std::vector<state::StripRef>& strips);
    void storeSnippet (int sceneIndex, int snippetIndex);
    void removeSnippet (int sceneIndex, int snippetIndex);
    state::ApplyResult recallSnippet (int sceneIndex, int snippetIndex);

    void setSnippetName (int sceneIndex, int snippetIndex, const juce::String&);
    void setSnippetSections (int sceneIndex, int snippetIndex, juce::uint32 sections);
    void setSnippetStrips (int sceneIndex, int snippetIndex, const std::vector<state::StripRef>&);
    void setSnippetFireWithScene (int sceneIndex, int snippetIndex, bool fire);
    /** A snippet of what `scope` covers (strips, sections, DCAs, mute groups, FX, console). */
    int addSnippet (int sceneIndex, const juce::String& snippetName, const state::Scope&);
    /** ADD ITEMS / REMOVE ITEMS: no strips in `items` means every strip of the snippet. */
    void addSnippetItems (int sceneIndex, int snippetIndex, const state::Scope& items);
    void removeSnippetItems (int sceneIndex, int snippetIndex, const state::Scope& items);
    /** PARTIAL LOAD of a snippet: only what is in both its scope and `limit`. */
    state::ApplyResult recallSnippetPartial (int sceneIndex, int snippetIndex, const state::Scope& limit);

    // Snippet REC / STOP: the parameters changed between REC and STOP.
    void startSnippetRecording();
    /** Returns the number of changed parameters. */
    int stopSnippetRecording();
    void clearSnippetRecording();
    bool isRecordingSnippet() const;
    /** STOPped with changes that SAVE hasn't stored yet. */
    bool hasRecordedSnippet() const;
    /** The parameters changed so far (recording) or by the last STOP. */
    int getRecordedParameterCount() const;
    /** SAVE after STOP: a snippet of the recorded parameters within `focus` (REC
        FOCUS; nullptr: all). -1 if nothing was recorded. */
    int addRecordedSnippet (int sceneIndex, const juce::String& snippetName, const state::Scope* focus);

    //==============================================================================
    // Custom controls (CC EDIT): the assignments as a <CustomControls> element,
    // saved with the show (the UI interprets the functions).
    juce::String getCustomControlsXml() const;
    bool setCustomControlsXml (const juce::String&);
    /** The PLUGIN HOST's MIDI mappings of its keys and hot knobs (a <PluginHostControl> element; empty: none),
        saved with the show. Empty text clears them. */
    juce::String getHostControlXml() const;
    bool setHostControlXml (const juce::String&);

    //==============================================================================
    // LIBRARY presets: an FX slot's device (FX tab) or a strip's settings (CHAN tab) as files.
    struct PresetInfo
    {
        enum class Kind
        {
            None,
            Fx,
            Channel
        };
        Kind kind = Kind::None;
        juce::String name, detail; // detail: the FX device, or the strip kind
        juce::uint32 scope = 0;    // a channel preset's sections
        std::optional<StripKind> stripKind;
    };

    static constexpr const char* fxPresetExtension = ".lfx";
    static constexpr const char* channelPresetExtension = ".lchn";

    static PresetInfo readPreset (const juce::File&);
    juce::Result saveFxPreset (int slot, const juce::File&, const juce::String& presetName);
    /** Fails with the warnings when part of it didn't load (a plugin, a premium device in 9-16). */
    juce::Result loadFxPreset (int slot, const juce::File&);
    juce::Result saveChannelPreset (const state::StripRef&, juce::uint32 scope, const juce::File&, const juce::String& presetName);
    /** With its own scope, or `scope`. */
    juce::Result loadChannelPreset (const state::StripRef&, const juce::File&, std::optional<juce::uint32> scope = std::nullopt);
    /** UPDATE SCOPE. */
    static bool setPresetScope (const juce::File&, juce::uint32 scope);

    //==============================================================================
    // LIBRARY scene and snippet files (SNAP and SNIP tabs): Sidecar's own
    // format, not the WING's .snap (imported on the SHOW tab).
    struct LibraryFileInfo
    {
        enum class Kind
        {
            None,
            Scene,
            Snippet
        };
        Kind kind = Kind::None;
        juce::String name;
        int count = 0; // a scene's snippets, a snippet's parameters
    };

    static constexpr const char* sceneFileExtension = ".lsnap";
    static constexpr const char* snippetFileExtension = ".lsnip";

    static LibraryFileInfo readLibraryFile (const juce::File&);
    /** A scene with its snippets. */
    juce::Result exportScene (int index, const juce::File&) const;
    juce::Result exportSnippet (int sceneIndex, int snippetIndex, const juce::File&) const;
    /** Inserts the file's scene at `index` (-1: appended); its new index, or -1 with `error`. */
    int importScene (const juce::File&, int index, juce::String& error);
    /** Adds the file's snippet to scene `sceneIndex`; its index, or -1 with `error`. */
    int importSnippet (const juce::File&, int sceneIndex, juce::String& error);

    //==============================================================================
    bool isFading() const noexcept { return ld::ld_show_is_fading (handle); }
    /** Plug-ins still gliding to a scene's values (PLUGIN HOST). */
    bool isGliding() const noexcept { return ! glides.empty(); }
    /** Moves every glide on and ends those that are due; true while some are left. The show's timer calls it at 60 Hz
        while any runs. */
    bool advanceGlides();
    void cancelFade();

    //==============================================================================
    // Files
    std::unique_ptr<juce::XmlElement> toXml() const;
    juce::String toXmlString() const;

    /** Loads a show (or a v1 session file). With loadPlugins false (safe mode) the
        console is restored without instantiating any plugins. */
    /** A dLive's Mixer Config (eight numbers: ld_engine_dlive_layout): its mixes laid out anew and
        reset, the rest of the console kept. */
    juce::Result applyDLiveLayout (const int32_t* layout, uint64_t stereoInputs);
    /** As applyDLiveLayout, with the PAFL busses (0-4) and the Main mode (livedaw_engine MainMode's order). */
    juce::Result applyDLiveStructure (const int32_t* layout, uint64_t stereoInputs, int pafl, int mainMode);
    juce::Result loadFromXml (const juce::XmlElement&, bool loadPlugins = true);

    /** Writes atomically (temp file + rename) and keeps the previous version as .bak,
        so a crash or power cut during a save can never leave a corrupt show. */
    juce::Result saveToFile (const juce::File&) const;
    juce::Result loadFromFile (const juce::File&, bool loadPlugins = true);

    /** Adds the snapshot scenes of a WING .show file (see WingRemote::importShow);
        `report` gets what was skipped. The console only grows to 40 channels. */
    juce::Result importWingShow (ld::LdWing*, const juce::File&, juce::String& report);
    /** WING remote surface control (see WingRemote::setSurfaceCallbacks): applies the layer and
        custom control changes clients made, then publishes the layers and custom controls with
        the upper custom control layer `ccBank`. Returns ld::kWingSurface* flags; `requestedBank`
        gets the layer a client chose (-1 none). */
    juce::uint32 syncWingSurface (ld::LdWing*, int ccBank, int& requestedBank);

    /** Clears scenes and resets the console to `numChannels` channels (the WING has 40). */
    void clear (int numChannels);

    /** Called after any change to the scene list or current scene. */
    std::function<void()> onChanged;

    /** Called before a show load empties FX slot `slot` (close its plugin editor here). */
    std::function<void (int slot)> onFxCleared;

    /** SEND MIDI: after a recall, the MIDI data of the scenes it recalled
        (linked ones too), in order. Set by the MIDI service. */
    std::function<void (const juce::StringArray&)> onSceneMidi;

    static constexpr const char* fileExtension = ".lshow";

    /** While a real console is under remote control its own scenes are the ones that count: every
        recall of this app's scenes (scene, next, previous, tag, partial, snippet, undo) is refused with a
        warning, so that the console's state isn't overwritten from a file. Storing a scene still works. */
    void setRecallLocked (bool locked) noexcept { recallLocked.store (locked); }
    bool isRecallLocked() const noexcept { return recallLocked.load(); }

private:
    std::atomic<bool> recallLocked { false };
    static state::ApplyResult lockedResult();
    void timerCallback() override;
    void changed();
    state::ApplyResult afterRecall (ld::ApplyResultC&);
    /** ld_show_recall_scene (`which`: 0 a scene, 1 next, 2 previous, 3 by tag), with
        an 02R's fader recall safe and fade times around it. */
    ld::ApplyResultC runRecall (int which, int index);
    juce::Result afterLoad (bool ok, ld::LdString* message);

    static bool captureFx (void* ctx, int32_t slot, ld::LdSink* description, ld::LdSink* state);
    static int32_t restoreFx (void* ctx, int32_t slot, int32_t mode, const char* description, size_t descriptionLen,
                              const char* state, size_t stateLen, ld::LdSink* error);
    static void clearFx (void* ctx, int32_t slot);
    static void beginRecall (void* ctx, double glideSeconds);

    /** The PLUGIN HOST's recall of a block: its plug-ins made again from the scene's descriptions (message thread). */
    int32_t recreateBlock (int slot, const std::vector<PluginChain::Saved>& saved, juce::String& error);
    /** A plug-in's state brought in by a recall: at once, or (glide seconds above 0) as a glide. */
    void applyState (int slot, int index, const juce::MemoryBlock& state);

    /** One plug-in on its way to a scene's state: the continuous parameters move from `from` to `to`, then
        the whole state is applied. */
    struct Glide
    {
        int slot = 0;
        int index = 0;
        std::shared_ptr<juce::AudioPluginInstance> plugin; // keeps it alive until the glide ends
        juce::MemoryBlock state;
        struct Param
        {
            juce::AudioProcessorParameter* parameter;
            float from, to;
        };
        std::vector<Param> params;
        double start = 0.0, seconds = 0.0;
    };
    std::vector<Glide> glides;
    double glideSeconds = 0.0; // the recall in progress: 0 snaps
    JUCE_DECLARE_NON_COPYABLE (Show)
};

} // namespace livedaw
