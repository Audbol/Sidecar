#pragma once

#include "ui/PluginHostControl.h"
#include "ui/PluginHostRack.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <vector>

namespace livedaw::ui::universal
{

/** The PLUGIN HOST rack of one strip of any console, as a large panel over the Universal Mixer (docs/PLUGIN_HOST.md,
    "The Universal Mixer offers it as a device too"). It is the PLUGIN HOST's own RACK page (ph::RackPage, with its
    docked plug-in window, ph::RackDock) shown for one strip instead of a channel of the PLUGIN HOST: the strip's
    blocks as tiles (drag to reorder, hold for the menu, "+" to add a plug-in), the dock with its hot knob assignment
    (ASSIGN HOT 1 and 2), and CLOSE. Plug-ins added here go into blocks marked as PLUGIN HOST racks.

    KEY chooses the sidechain key of the selected plug-in when it has a sidechain input: NONE, or a channel of the
    console, its input (IN, before its blocks) or its output (OUT, after the strip's whole chain), the same taps the
    PLUGIN HOST's KEYS page uses (FxSlots::setChainMemberKey). The key is hidden for a plug-in that has no
    sidechain bus (or runs in the sandbox), and where the engine has no channels to take a key from. A channel
    cannot key a plug-in of its own strip after the chain (the engine refuses it, and so does the menu).

    MIDI learn is not part of it: the hot knobs are assigned here and turned from the card in the CHANNEL panel. */
class HostRackPanel final : public juce::Component, private juce::Timer
{
public:
    HostRackPanel (UiContext&, const Theme&);
    ~HostRackPanel() override;

    /** Shows `strip`'s rack with block `uid` selected. */
    void show (Strip& strip, int uid);
    Strip* getStrip() const noexcept { return strip; }
    ph::RackPage& getRack() noexcept { return page; }
    ToggleKey& getCloseKey() noexcept { return closeKey; }
    ToggleKey& getKeyKey() noexcept { return keyKey; }
    /** Called by CLOSE (and when the strip goes): the owner hides the panel. */
    std::function<void()> onClose;

    //==============================================================================
    // The sidechain key of the selected plug-in

    /** The selected plug-in has a sidechain input and the console has channels to key it from. */
    bool isKeyAvailable() const;
    PluginChain::Key getKey() const;
    /** False when refused (no sidechain, or the strip's own output after its chain). */
    bool setKey (PluginChain::Key);
    struct KeyChoice
    {
        PluginChain::Key key;
        juce::String text; // "NONE", "CH 5 IN", "CH 5 OUT"
        bool allowed = true;
    };
    /** NONE, then each channel's IN and OUT. */
    std::vector<KeyChoice> getKeyChoices() const;
    void showKeyMenu();
    /** The KEY key follows the selected plug-in (the timer does this too). */
    void refresh() { refreshKey(); }

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kHeader = 52, kDesignWidth = 1500;

private:
    void timerCallback() override;
    int selectedSlot() const;
    int channelIndexOfStrip() const;
    void refreshKey();

    UiContext& ctx;
    const Theme& theme;
    Strip* strip = nullptr;
    int seenStructureVersion = -1;
    ph::Control control;
    ph::RackPage page;
    ToggleKey closeKey, keyKey;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostRackPanel)
};

} // namespace livedaw::ui::universal
