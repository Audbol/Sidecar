#pragma once

#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

namespace livedaw::ui::universal
{

/** A plug-in block of a strip's chain as the Universal Mixer shows it (the CHANNEL panel's card and
    the block editor): the plugin's (or chain's) name and vendor with a small plug glyph, its latency,
    its state (bypassed, faulted, or missing: the plugin it was saved with could not be loaded, the
    block stays bypassed with that name), and the keys ON (bypass), EDIT (opens the plugin window)
    and PLUG-IN (choose, replace, add to the chain, remove). Hosting is FxSlots': the block's slot
    number is FxSlots::blockSlotOf, so the sandbox setting, delay compensation and crash handling
    are the FX slots'. */
class PluginBlockCard final : public juce::Component, private juce::Timer
{
public:
    PluginBlockCard (UiContext&, const Theme&);
    ~PluginBlockCard() override;

    /** Shows block `uid` of `strip` (nullptr: nothing). `on` is the block's ON flag. */
    void setBlock (Strip* strip, int uid, std::atomic<bool>* on);

    /** What the card shows now (for the tests). */
    const FxSlots::BlockInfo& info() const noexcept { return shown; }
    /** The plugin menu's entries (the keys call it). */
    void showMenu();
    void openEditor();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Space the keys take at the bottom, and the least height the card reads at. */
    static constexpr int kKeysHeight = 40;

private:
    void timerCallback() override;
    void update();
    int slot() const;

    UiContext& ctx;
    const Theme& theme;
    Strip* strip = nullptr;
    int uid = -1;
    ToggleKey onKey, editKey, menuKey;
    FxSlots::BlockInfo shown;
    std::atomic<bool>* onFlag = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginBlockCard)
};

} // namespace livedaw::ui::universal
