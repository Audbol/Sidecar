#pragma once

#include "ui/Knobs.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

namespace livedaw::ui::x32
{

/** The X32 surface's monitoring and talkback section (Phase 4 step 12b), as
    the X32 groups them beside its display:

    - MONITOR (the speakers' level) and PHONES (the phones' level);
    - DIM, MONO and MUTE of the monitor (the solo bus's path);
    - TALK (the talkback level) and the TALK A and TALK B keys, latching.

    Each control is the engine's own (EngineControls: the monitor paths and
    the talk paths); where the talkback goes and which input it takes are set
    on the Universal Mixer for now. Message thread only. */
class MonitorSection final : public juce::Component,
                             private juce::Timer
{
public:
    MonitorSection (UiContext&, const universal::Theme&);
    ~MonitorSection() override;

    /** For tests: a knob by its label ("MONITOR", "PHONES", "TALK"), or nullptr. */
    DisplayKnob* knob (const juce::String& label);
    /** For tests: a key by its text ("DIM", "MONO", "MUTE", "TALK A", "TALK B"), or nullptr. */
    universal::ToggleKey* key (const juce::String& text);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    UiContext& ctx;
    const universal::Theme& theme;
    DisplayKnob monitor, phones, talk;
    /** MONITOR's and TALKBACK's sections, the EXT MIC socket (set by resized()). */
    juce::Rectangle<int> monitorArea, talkArea, socket;
    universal::ToggleKey dim, mono, mute, talkA, talkB;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonitorSection)
};

} // namespace livedaw::ui::x32
