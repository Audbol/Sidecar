#pragma once

#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <functional>
#include <memory>
#include <vector>

namespace livedaw::ui::universal
{

/** What a scope's lit keys mean: what a scene recalls, what every recall leaves
    alone (the recall safes), or what a snippet holds. */
enum class ScopeRole
{
    Recalls,
    Safe,
    Holds
};

/** `items` in or out of `scope`, as sets: its sections, strips, DCAs, mute
    groups, FX slots and the console's settings. A scope of every strip that
    loses one becomes a list of the others; a list that comes to hold every
    strip of `engine` is every strip again (strips added later included). */
void applyScopeChange (state::Scope& scope, const state::Scope& items, bool in, MixerEngine& engine);

/** A scope in a line, in numbers of this console ("9 of 10 sections, 30 of 48
    strips, 8 of 8 DCAs ..."): "Everything" when nothing is left out, and for
    the safes only what is safe ("Nothing" when nothing is). `stripOn`, if set,
    says which strips count instead of the scope's (the strips' recall safe). */
juce::String describeScope (const state::Scope&, MixerEngine&, ScopeRole, const std::function<bool (const Strip&)>& stripOn = {});

/** Whether a scene's recall scope is narrower than a new scene's (everything
    but ROUTING on every strip, DCA, mute group and FX slot of `engine`). */
bool isPartialScope (const state::Scope&, MixerEngine&);

/** A scope, to read and to edit: the sections as keys (NAME ... GROUPS), then,
    scrolled, a key per strip under its kind's heading, per DCA, per mute group
    and per FX slot, and the console's settings. A lit key is in the scope; a
    tap puts it in or takes it out, a tap on a heading does that for all of its
    keys (in, unless they all are). ALL and NONE do it for everything.

    The editor holds no scope: `get` reads the one it shows and `change` is told
    what was tapped, so the same editor does a scene's recall scope, the recall
    safes and a snippet's items. */
class ScopeEditor final : public juce::Component, private juce::Timer
{
public:
    ScopeEditor (UiContext&, const Theme&, ScopeRole);
    ~ScopeEditor() override;

    /** The scope shown. */
    std::function<state::Scope()> get;
    /** A tap: `items` (only what was tapped, its strips as a list) goes in or out. */
    std::function<void (const state::Scope& items, bool in)> change;
    /** The strips' state when it isn't the scope's (the recall safes: each strip's own flag). */
    std::function<bool (const Strip&)> stripOn;
    /** The section keys and ALL / NONE, for a scope whose sections are edited here. */
    void setSectionsShown (bool);
    bool areSectionsShown() const noexcept { return sectionsShown; }

    // As the keys do.
    void toggleSection (juce::uint32 section);
    void toggleStrip (const Strip&);
    /** Every strip of a kind in, unless they all are: then out. */
    void toggleKind (StripKind);
    void toggleDca (int index);
    void toggleMuteGroup (int index);
    void toggleFx (int slot);
    void toggleConsole();
    void setAll (bool in);

    bool hasStrip (const Strip&) const;
    /** The scope in a line (describeScope). */
    juce::String getSummary() const;
    /** Takes the scope and the console's strips again (its timer does this 5 times a second while shown). */
    void refresh();
    /** The height its keys need at `width`, unscrolled. */
    int getBodyHeight (int width) const;
    /** The key of strip `s` in the scrolled list (empty: not there), for the tests. */
    juce::Rectangle<int> getStripKeyArea (const Strip& s) const;

    ToggleKey allKey, noneKey;
    /** NAME, ROUTING, INPUT, GATE, EQ, DYNAMICS, INSERTS, SENDS, FADER, GROUPS. */
    std::vector<std::unique_ptr<ToggleKey>> sectionKeys;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Body;
    void timerCallback() override
    {
        if (isShowing())
            refresh();
    }
    juce::Colour onColour() const noexcept;
    void send (const state::Scope& items, bool in);
    /** A tap on key `index` of a group of the list (a strip kind, the DCAs ...), or on its heading. */
    void toggleItem (int group, int index);
    void toggleGroup (int group);

    UiContext& ctx;
    const Theme& theme;
    const ScopeRole role;
    bool sectionsShown = true;
    state::Scope shown = state::Scope::none();
    std::unique_ptr<Body> body;
    juce::Viewport viewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeEditor)
};

} // namespace livedaw::ui::universal
