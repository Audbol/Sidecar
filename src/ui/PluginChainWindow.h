#pragma once

#include "ui/PluginChainView.h"
#include "ui/UiContext.h"

namespace livedaw::ui
{

/** The plugin window of an FX slot (opened with ctx.openPluginEditor): one
    window with the slot's chain bar and the editors of all its plugins, in
    chain order (PluginChainView). + adds a plugin (the plugin list); the chips
    and title rows bypass, clear, move, zoom, collapse and remove plugins. The
    title names the chain; the window grows to fit (staying on its screen)
    and scrolls beyond the screen. It is resizable; its size and its layout
    (ROW / FOCUS) are kept per slot in the settings. It closes itself when the
    slot empties. It pictures its plugins' editors for the plugin overview
    (PluginPictures) shortly after opening and every few seconds while it shows. */
class PluginChainWindow final : public juce::DocumentWindow, private juce::Timer
{
public:
    /** `onClose` deletes the window (called asynchronously). */
    PluginChainWindow (UiContext&, int slot, std::function<void (PluginChainWindow&)> onClose);
    ~PluginChainWindow() override;

    int getSlot() const noexcept { return view.getSlot(); }
    /** The slot's plugin or chain this window shows (it closes when that goes). */
    juce::AudioProcessor* getShownProcessor() const noexcept { return view.getShownProcessor(); }
    /** `p` is about to be destroyed: a plugin of the chain loses its editor. */
    void dropEditorFor (const juce::AudioProcessor* p) { view.dropEditorFor (p); }

    /** Pictures the plugins it shows now (ctx.pluginPictures). */
    void capturePictures();

    void closeButtonPressed() override;
    void resized() override;

private:
    void chainChanged();
    void requestClose();
    void showAddMenu (juce::Component& target);
    void removeMember (int index);
    juce::String settingsKey (const char* what) const;
    /** Moves (or shrinks) the window back onto its screen: it grows when a plugin's
        editor takes its real size, after the window was placed. */
    void keepOnScreen();
    void timerCallback() override;

    UiContext& ctx;
    PluginChainView view;
    std::function<void (PluginChainWindow&)> onClose;
    bool remembering = false; // the size is saved once the window is set up
    bool placing = false;     // keepOnScreen() moving it
    static constexpr int kFirstPictureMs = 700, kPictureEveryMs = 3000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginChainWindow)
};

} // namespace livedaw::ui
