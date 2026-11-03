#pragma once

#include "ui/Knobs.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

namespace livedaw::ui::x32
{

/** The X32 surface's channel strip section (Phase 4 step 8c): the selected
    strip's most used controls, as the X32 groups them over its panel:

    - CONFIG / PREAMP: GAIN (trim), LOW CUT (frequency, on), polarity (the
      X32's key reads a slashed O);
    - GATE: THRESHOLD, on, the gain reduction;
    - DYNAMICS: THRESHOLD, RATIO, on, the gain reduction;
    - EQUALIZER: the band keys (LOW... HIGH: 4 on inputs, 6 on buses, mains
      and matrices), the band's TYPE, FREQ, GAIN and Q, on;
    - BUS SENDS: four sends at a time (the bank keys: 1-4... 13-16);
    - MAIN BUS: PAN, LR and M/C on, the M/C level.

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

    DisplayKnob gain, lowCut, gateThr, dynThr, dynRatio, eqType, eqFreq, eqGain, eqQ, pan, mcLevel;
    std::array<DisplayKnob, 4> sends;
    universal::ToggleKey lowCutOn, invert, gateOn, dynOn, eqOn, lrOn, mcOn;
    std::vector<std::unique_ptr<universal::ToggleKey>> bandKeys, bankKeys, viewKeys;
    std::array<Section, 6> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelSection)
};

} // namespace livedaw::ui::x32
