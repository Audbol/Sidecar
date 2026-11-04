#pragma once

#include "ui/Knobs.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <array>

namespace livedaw::ui::x32
{

/** The X32 surface's assign section (Phase 4 step 12c): the user controls'
    three sets (A, B, C), each four encoders and eight buttons (the X32's 5-12),
    acting as their function codes say (`/config/userctrl/<set>/enc|btn/<n>`,
    kept by the X32 map; OSC document pp.116-121):

    - encoders: F fader, P pan, S send, X FX parameter, D the selected
      strip's fader or dynamics, M MIDI (CC, note, program change);
    - buttons: O mute (strips, DCAs, mute groups), I insert, X an FX
      parameter at its lowest or highest, M MIDI push / toggle, S0 a scene,
      S9 PREV / NEXT / GO, P a page of the display.

    Codes for what Sidecar doesn't have (remote, X-Live!, the USB recorder,
    cues) do nothing. Message thread only. */
class AssignSection final : public juce::Component,
                            private juce::Timer
{
public:
    AssignSection (UiContext&, const universal::Theme&);
    ~AssignSection() override;

    void setSet (int);
    int getSet() const noexcept { return set; }

    /** A page jump (button code P): the display's screen and tab, and the
        strip to select (nullptr: none). Set by the surface. */
    std::function<void (int screen, int tab, Strip*)> onJump;

    /** For tests: encoder `i` (0-3) and button `i` (0-7) of the shown set. */
    DisplayKnob& getEncoder (int i) { return encoders[(size_t) juce::jlimit (0, 3, i)]; }
    universal::ToggleKey& getButton (int i) { return *buttons[(size_t) juce::jlimit (0, 7, i)]; }
    /** Reads the set's codes again and rebinds (a code changed). */
    void refreshCodes();

    /** A code's function as a short label ("FDR CH 6", "MUTE GRP 1"). */
    static juce::String describe (const juce::String& code, bool button);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::String code (bool button, int n) const;
    KnobBinding bindEncoder (const juce::String& code, int index);
    void pressButton (const juce::String& code, int index);
    bool buttonLit (const juce::String& code, int index) const;
    /** Strip `xx` of a code (0-71 as the X32 numbers them), nullptr for none or a DCA. */
    Strip* stripAt (int xx) const;

    UiContext& ctx;
    const universal::Theme& theme;
    int set = 0;
    std::array<DisplayKnob, 4> encoders;
    std::vector<std::unique_ptr<universal::ToggleKey>> buttons, setKeys;
    std::array<juce::String, 12> shownCodes;
    Strip* shownSelected = nullptr;
    std::array<float, 4> midiEncoder {};  // an encoder sending MIDI: its value (0..1)
    std::array<bool, 8> midiToggle {};    // a MIDI toggle button: on

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AssignSection)
};

} // namespace livedaw::ui::x32
