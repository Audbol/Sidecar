#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace juce
{
class AudioProcessor;
}

namespace livedaw::ui
{

/** One of the window's interfaces over the console (docs/UNIVERSAL_MIXER.md,
    Phase 2): the WING surface (WingSurface) or the Universal Mixer
    (UniversalSkin). The window (MainComponent) owns what both share: the
    engine, the show, the selection, the plugin windows, the recorder and its
    editor, the MIDI and web remotes. It tells the skin what changed and gives it
    the keys it doesn't use itself. */
class Skin : public juce::Component
{
public:
    /** The console's structure changed, or a show was loaded. */
    virtual void refreshAll() = 0;
    /** The fader banks (layers) changed. */
    virtual void refreshLayers() {}
    /** The selected strip changed. */
    virtual void selectionChanged() {}
    /** Sends on Fader turned on or off, or follows another strip. */
    virtual void sendsOnFaderChanged() {}
    /** The show changed (its scenes, its file, the library). */
    virtual void showChanged() {}
    /** The custom control assignments changed. */
    virtual void customControlsChanged() {}
    /** The component pop-ups (confirmations, warnings) open over. */
    virtual juce::Component& popupHost() { return *this; }
    /** The skin's own keys, offered after the window's; true if used. */
    virtual bool skinKeyPressed (const juce::KeyPress&) { return false; }
    /** A plug-in (nullptr: every plug-in) is about to go: a skin that docks plug-in editors drops them. */
    virtual void dropPluginEditors (juce::AudioProcessor*) {}
    /** The multitrack editor covers the window (the skin keeps its state underneath). */
    virtual void setCovered (bool shouldBeCovered) { covered = shouldBeCovered; }
    bool isCovered() const noexcept { return covered; }

protected:
    bool covered = false;
};

} // namespace livedaw::ui
