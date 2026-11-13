#pragma once

#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <array>
#include <vector>

namespace livedaw::ui::universal
{

/** A plug-in block of a strip's chain as the Universal Mixer shows it (the CHANNEL panel's card and
    the block editor): the plugin's (or chain's) name and vendor with a small plug glyph, its latency,
    its state (bypassed, faulted, or missing: the plugin it was saved with could not be loaded, the
    block stays bypassed with that name), and the keys ON (bypass), EDIT (opens the plugin window)
    and PLUG-IN (choose, replace, add to the chain, remove). Hosting is FxSlots': the block's slot
    number is FxSlots::blockSlotOf, so the sandbox setting, delay compensation and crash handling
    are the FX slots'.

    A block marked as a PLUGIN HOST rack (Strip::isBlockHostRack) shows its rack compactly instead: the
    names of its plug-ins (a bypassed one dim), the rack's total latency and the two hot knobs of its
    focused plug-in (the first that has any) as small knobs that turn the parameters they follow. EDIT
    becomes OPEN: it asks the Universal Mixer for the rack's panel (UiContext::openHostRack). */
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

    //==============================================================================
    // A PLUGIN HOST rack

    /** The block is a PLUGIN HOST rack: the card shows the rack. */
    bool isRack() const noexcept { return rack; }
    /** The names of the rack's plug-ins, in signal order. */
    const juce::StringArray& getRackNames() const noexcept { return rackNames; }
    /** OPEN (the card's EDIT key in a rack): the rack's panel over the Universal Mixer. */
    void openRack();
    ToggleKey& getOpenKey() noexcept { return editKey; }

    /** One of the two hot knobs of the rack's focused plug-in. */
    struct Hot
    {
        bool assigned = false;
        float value = 0.0f;
        juce::String name; // short: "CUTOFF"
        juce::String text; // the value as the plug-in shows it
    };
    const Hot& getHot (int knob) const noexcept { return hots[(size_t) juce::jlimit (0, 1, knob)]; }
    /** A turn of the knob: `delta` in 0..1 of the parameter's range (a stepped parameter moves a step once the
        turns add up to half of one). False when the knob follows nothing. */
    bool turnHot (int knob, float delta);
    juce::Rectangle<int> getHotBounds (int knob) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Space the keys take at the bottom, and the least height the card reads at. */
    static constexpr int kKeysHeight = 40;
    /** A hot knob turns through its whole range over this many pixels of drag. */
    static constexpr float kKnobTravel = 200.0f;

private:
    void timerCallback() override;
    void update();
    int slot() const;
    juce::AudioProcessorParameter* hotParam (int knob) const;
    void paintRack (juce::Graphics&, juce::Rectangle<float> area);

    UiContext& ctx;
    const Theme& theme;
    Strip* strip = nullptr;
    int uid = -1;
    ToggleKey onKey, editKey, menuKey;
    FxSlots::BlockInfo shown;
    std::atomic<bool>* onFlag = nullptr;

    bool rack = false;
    juce::StringArray rackNames;
    std::vector<bool> rackBypassed;
    std::array<Hot, 2> hots;
    float carried = 0.0f; // a stepped parameter: what the turns have added up to
    int dragKnob = -1;
    float dragFrom = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginBlockCard)
};

} // namespace livedaw::ui::universal
