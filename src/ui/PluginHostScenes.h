#pragma once

#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace livedaw::ui::ph
{

/** The SCENES page of the PLUGIN HOST's screen (docs/PLUGIN_HOST.md, page 4).

    At the left the show's scenes as a list (number, name, fade mode and time, and a mark on the one that is
    active and the one that is cued), under it the keys: RECALL, STORE NEW, OVERWRITE, RENAME, DELETE, MOVE UP
    and DOWN, the fade mode (SNAP or GLIDE) and the fade time. At the right the SAFES: every channel, and under
    it every plug-in of its rack, each with a SAFE key. A safe channel is left alone by a recall altogether; a
    safe plug-in keeps its state, its bypass and its place in the rack, and stays even when the scene lacks it.

    The cue is the surface's (its PREV, GO and NEXT keys): a tap on a row cues the scene, RECALL is GO, and a
    store, a delete or a move leaves the cue on the scene it concerns. OVERWRITE and DELETE ask twice: the first
    press turns the key to CONFIRM, the second does it (it lets go after a few seconds). Message thread only. */
class ScenesPage final : public juce::Component, private juce::Timer
{
public:
    ScenesPage (UiContext&, const universal::Theme&);
    ~ScenesPage() override;

    /** The surface's cue (-1 none), moving it, and GO: set by the surface that owns the cue. */
    std::function<int()> getCue;
    std::function<void (int)> setCue;
    std::function<void()> go;

    /** Reads the show and the racks again (the timer does this too). */
    void refresh();

    // The scenes.
    int getNumScenes() const;
    /** A tap on a row: the scene is cued. */
    void selectScene (int index);
    /** A new scene holding the console as it is, after the last; it is cued. */
    int storeNew();
    /** Stores the console into the cued scene (asks twice: the first press arms it). */
    void pressOverwrite();
    /** Names the cued scene (the on-screen keyboard asks for the name). */
    void askRename();
    void rename (int index, const juce::String& name);
    /** Deletes the cued scene (asks twice). */
    void pressDelete();
    /** Moves the cued scene up (-1) or down (1) the list; the cue goes with it. */
    void moveSelected (int delta);
    /** The cued scene's fade mode and time. */
    void setGlide (bool glide);
    void stepTime (int delta);
    /** "SNAP" or "GLIDE" and "0.5 s" ("NO FADE" for 0). */
    static juce::String describeTime (double seconds);
    static const std::vector<double>& timeSteps();
    bool isArmed() const noexcept { return armed != Armed::None; }

    universal::ToggleKey& getRecallKey() noexcept { return recallKey; }
    universal::ToggleKey& getStoreKey() noexcept { return storeKey; }
    universal::ToggleKey& getOverwriteKey() noexcept { return overwriteKey; }
    universal::ToggleKey& getRenameKey() noexcept { return renameKey; }
    universal::ToggleKey& getDeleteKey() noexcept { return deleteKey; }
    universal::ToggleKey& getUpKey() noexcept { return upKey; }
    universal::ToggleKey& getDownKey() noexcept { return downKey; }
    universal::ToggleKey& getSnapKey() noexcept { return snapKey; }
    universal::ToggleKey& getGlideKey() noexcept { return glideKey; }
    universal::ToggleKey& getTimeDownKey() noexcept { return timeDownKey; }
    universal::ToggleKey& getTimeUpKey() noexcept { return timeUpKey; }

    /** What a list row shows (the tests read it). */
    struct SceneRow
    {
        int number = 0;
        juce::String name;
        bool glide = false;
        double seconds = 0.0;
        bool active = false, cued = false;
    };
    SceneRow getSceneRow (int index) const;

    // The SAFES panel: a row for each channel and, under it, one for each plug-in of its rack.
    struct SafeRow
    {
        int channel = 0; // 0-based
        int uid = -1;    // the plug-in's block; -1 for the channel's own row
        juce::String name;
        bool isChannel() const noexcept { return uid < 0; }
    };
    int getNumSafeRows() const noexcept { return (int) safeRows.size(); }
    const SafeRow& getSafeRow (int row) const { return safeRows[(size_t) row]; }
    bool isSafe (int row) const;
    /** A tap on a row's SAFE key. */
    void toggleSafe (int row);
    /** Scrolls the lists (a row at the top). */
    void scrollScenes (int firstRow);
    void scrollSafes (int firstRow);
    int getFirstSceneRow() const noexcept { return sceneTop; }
    int getFirstSafeRow() const noexcept { return safeTop; }
    /** The list rows' places in this component, to tap them (the tests, the screenshot). */
    juce::Rectangle<int> getSceneRowBounds (int index) const;
    juce::Rectangle<int> getSafeKeyBounds (int row) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int kRow = 48, kHead = 36, kKey = 52, kLeft = 900, kGap = 12;

private:
    enum class Armed
    {
        None,
        Overwrite,
        Delete
    };

    void timerCallback() override;
    void rebuildSafeRows();
    void arm (Armed);
    void disarm();
    void showCue();
    int cue() const { return getCue ? getCue() : -1; }
    int visibleSceneRows() const;
    int visibleSafeRows() const;
    juce::Rectangle<int> sceneList() const;
    juce::Rectangle<int> safeList() const;
    juce::Rectangle<int> safeKey (juce::Rectangle<int> row) const;
    void clampScroll();

    UiContext& ctx;
    const universal::Theme& theme;
    universal::ToggleKey recallKey, storeKey, overwriteKey, renameKey, deleteKey, upKey, downKey, snapKey, glideKey, timeDownKey, timeUpKey;
    std::vector<SafeRow> safeRows;
    juce::String safeSignature;
    Armed armed = Armed::None;
    juce::uint32 armedAt = 0;
    int sceneTop = 0, safeTop = 0, shownCue = -2, shownCurrent = -2, shownScenes = -1;
    juce::String message;

    // A drag in a list scrolls it; a tap without a drag is a press.
    enum class Drag
    {
        None,
        Scenes,
        Safes
    };
    Drag drag = Drag::None;
    int dragFrom = 0;
    bool dragged = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScenesPage)
};

} // namespace livedaw::ui::ph
