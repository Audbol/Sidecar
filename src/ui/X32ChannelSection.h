#pragma once

#include "ui/Knobs.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

namespace livedaw::ui::x32
{

/** The X32 surface's channel strip section (Phase 4 step 8c): the selected
    strip's most used controls, laid out as on the X32's panel (encoders in
    LED rings, keys with their names printed under them):

    - CONFIG / PREAMP: GAIN, the LEVEL meter, FREQUENCY (low cut), 48V, the
      polarity key (a slashed O), LOW CUT;
    - GATE and DYNAMICS side by side under it: THRESHOLD, GATE / DUCKER and
      COMP / EXP (on), the GR meter between them;
    - EQUALIZER: Q, FREQ and GAIN, MODE (the band's type, its LED lit: HCUT
      ... LCUT), the band keys HIGH, HIGH MID, LOW MID, LOW (a second press of
      HIGH or LOW: HIGH 2, LOW 2 on a bus's six bands), EQUALIZER (on);
    - BUS SENDS: four sends at a time, the bank keys 1-4 ... 13-16 beside them;
    - MAIN BUS: LEVEL (the M/C send), MONO BUS, PAN / BAL, STEREO BUS.

    Each section's VIEW key asks for the display's page of the strip. Knobs
    bind to the strip's own blocks through the console's models, so they turn
    an X32's gate, compressor and EQ. Message thread only. */
class ChannelSection final : public juce::Component,
                             private juce::Timer
{
public:
    ChannelSection (UiContext&, const universal::Theme&);
    ~ChannelSection() override;

    /** The strip to show (nullptr: none: the knobs empty). */
    void setStrip (Strip*);
    Strip* getStrip() const noexcept { return strip; }

    /** The band the EQ knobs turn (0-based), and the send bank (0: 1-4). */
    void setBand (int);
    void setSendBank (int);
    int getBand() const noexcept { return band; }
    int getSendBank() const noexcept { return sendBank; }

    /** A VIEW key: show the section (0: CONFIG / PREAMP... 5: MAIN BUS) on the display. */
    std::function<void (int section)> onView;

    /** For tests: the knob of a control ("GAIN", "THRESHOLD"...) of a section. */
    DisplayKnob* knob (const juce::String& section, const juce::String& label);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Section
    {
        juce::String title;
        juce::Rectangle<int> area;
    };

    void timerCallback() override;
    void bind();
    int numBands() const noexcept;

    UiContext& ctx;
    const universal::Theme& theme;
    Strip* strip = nullptr;
    int band = 0, sendBank = 0;
    int boundInput = -2; // the input GAIN was bound for (a head amp's, or the trim)

    /** The selected strip's source's settings (48V), or nullptr. */
    ld::SourceParams* source() const;
    /** The band's type: its index, and how many there are (MODE steps through them). */
    int typeIndex() const;

    DisplayKnob gain, lowCut, gateThr, dynThr, dynRatio, eqType, eqFreq, eqGain, eqQ, pan, mcLevel;
    std::array<DisplayKnob, 4> sends;
    universal::ToggleKey lowCutOn, invert, phantom, mode, gateOn, dynOn, eqOn, lrOn, mcOn;
    /** Where the drawn meters and the EQ's type LEDs are (set by resized()). */
    juce::Rectangle<int> levelMeter, grMeter, typeLeds;
    std::vector<std::unique_ptr<universal::ToggleKey>> bandKeys, bankKeys, viewKeys;
    std::array<Section, 6> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelSection)
};

} // namespace livedaw::ui::x32
