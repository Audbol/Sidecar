#pragma once

#include "ui/UiContext.h"

namespace livedaw::ui
{

/** The plugin overview (SETUP > PLUGINS > PLUGIN OVERVIEW, Ctrl+Shift+P): every
    plugin of every FX slot in a window of its own, to keep in view during a
    show, on a second screen for instance.

    CARDS (the default) shows a card per slot holding plugins: the slot and its
    state (RUNNING green, BYPASS / NOT USED grey, PLUGIN FAULT amber when a
    plugin of its chain faulted on its own, FAULT / DOUBLE red), the strips
    whose inserts use it, a picture of each plugin's editor in chain order (dim
    and marked when bypassed, framed red when faulted, with the picture's age),
    the slot's input and output meters, its share of the block time and its
    latency. LIST shows the same as one compact row per slot with a small
    picture. The title row sums it up (plugins, faults, DSP load, delay
    compensation); the status line counts the pictures.

    A plugin draws its editor only while it is open: the pictures are the last
    ones its plugin window took (PluginPictures). CAPTURE ALL opens each slot's
    window in turn to take them. Touching a slot opens its plugin window.

    The window remembers where it was (screen included), its size, the layout,
    ON TOP and its text size (- / +), and opens again at start-up if it was
    open when Sidecar quit. It only reads the slots: nothing here bypasses or
    changes a plugin. */
class PluginOverviewWindow final : public juce::DocumentWindow
{
public:
    /** `onClose` deletes the window (called asynchronously). */
    PluginOverviewWindow (UiContext&, std::function<void()> onClose);
    ~PluginOverviewWindow() override;

    void closeButtonPressed() override;
    void moved() override;
    void resized() override;

    /** The settings: the window was open when Sidecar quit (MainComponent reopens it). */
    static constexpr const char* openKey = "pluginOverview.open";

    enum class Layout
    {
        cards,
        list
    };
    Layout getLayout() const noexcept;
    void setLayout (Layout);
    /** For tests: the slots shown, in order. */
    std::vector<int> getShownSlots() const;

private:
    class Content;

    void remember();

    UiContext& ctx;
    std::unique_ptr<Content> content;
    std::function<void()> onClose;
    bool remembering = false; // the place is saved once the window is set up

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginOverviewWindow)
};

} // namespace livedaw::ui
