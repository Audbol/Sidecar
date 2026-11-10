#pragma once

#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <functional>

/** DCAs and mute groups in the Universal Mixer: a DCA as a MIXER column, the
    mute groups' keys, and a grid of numbered keys for the groups a strip is a
    member of (the STRIP page). Any number of each, as the console is
    configured (up to 256). */
namespace livedaw::ui::universal
{

/** A group's name under its number, empty while it is the default one
    ("MGRP.2", "DCA 3": the number says it already). */
juce::String customGroupName (const juce::String& name, int index);

/** Asks for a line of text (a name), then calls `done` with it, trimmed, unless
    cancelled. */
void askForText (juce::Component& host, const juce::String& title, const juce::String& prompt, const juce::String& initial,
                 std::function<void (const juce::String&)> done);

/** A grid of numbered keys, one per group: lit while `isOn` (a strip's
    membership, an engaged mute group), a tap calls `toggle`. Painted as one
    component, so 256 keys cost nothing; the keys are at least `minKey` px. */
class GroupGrid final : public juce::Component, private juce::Timer
{
public:
    GroupGrid (const Theme&, juce::Colour onColour);

    std::function<int()> count;
    std::function<bool (int)> isOn;
    std::function<void (int)> toggle;
    /** A key's text under its number (a group's name), empty for none. */
    std::function<juce::String (int)> name;
    /** A key's own text in place of its number ("B12"), if set. */
    std::function<juce::String (int)> keyText;
    int minKey = 44;

    /** The key of group `i` (for the tests and the layout). */
    juce::Rectangle<int> keyArea (int i) const;
    /** The height the grid needs for its keys at a width. */
    int heightFor (int width) const;
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override { refresh(); }
    int columns (int width) const;

    const Theme& theme;
    juce::Colour colour;
    std::vector<bool> shown;
};

/** A DCA as a column of the MIXER view: its colour, label and name (a tap
    renames it or picks its colour), MUTE and SOLO, its fader and the fader's
    value. A DCA has no meter, as on a console. */
class DcaColumn final : public juce::Component
{
public:
    DcaColumn (UiContext&, int dca, const Theme&);

    int getDca() const noexcept { return dca; }
    void refresh();
    /** Its header's Spill members (the MIXER view shows this DCA's strips). */
    std::function<void (int dca)> onSpill;

    ToggleKey muteKey, soloKey;
    FaderControl fader;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> header() const noexcept;

    UiContext& ctx;
    const int dca;
    const Theme& theme;
    juce::String name;
    int colour = 0;
    float shownDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DcaColumn)
};

/** The mute groups beside the DCAs: a key per group, lit while it is engaged
    (a tap engages or releases it), named; ALL OFF releases them all. */
class MuteGroupsPanel final : public juce::Component
{
public:
    MuteGroupsPanel (UiContext&, const Theme&);

    void refresh();
    GroupGrid groups;
    ToggleKey allOffKey;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    UiContext& ctx;
    const Theme& theme;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MuteGroupsPanel)
};

} // namespace livedaw::ui::universal
