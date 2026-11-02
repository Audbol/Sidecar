#pragma once

#include "ui/UiContext.h"
#include "ui/universal/ValueControls.h"

namespace livedaw::ui::universal
{

/** The MONITORING view: the two monitor paths, SPEAKERS and PHONES (their
    source from the WING's monitor source list, levels, DIM, MONO and MUTE,
    outputs and meters), and the solo settings (the WING's solo bus and solo
    modes, PFL or AFL by kind of strip, CLEAR SOLO). */
class MonitorView final : public juce::Component, private juce::Timer
{
public:
    MonitorView (UiContext&, const Theme&);
    ~MonitorView() override;

    /** One monitor path. */
    class Path final : public juce::Component
    {
    public:
        Path (UiContext&, const Theme&, bool phones);

        ChoiceBar source;
        ValueBar sourceLevel, level, dimAmount;
        ToggleKey dim, mono, mute;
        ChoiceBar outputLeft, outputRight;
        MeterControl meter;

        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        const Theme& theme;
        bool phones;
        std::vector<std::pair<juce::Component*, int>> rows;
    };

    Path speakers, phones;
    ChoiceBar soloBus, soloMode;
    ToggleKey aflChannels, aflBuses, aflMains, aflMatrices, clearSolo;

    /** Takes the values again (the timer does this 20 times a second). */
    void refresh();
    /** CLEAR SOLO: every strip's and DCA's solo off, and LISTEN ended. */
    void clearAllSolos();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override
    {
        if (isVisible()) // a hidden view waits
            refresh();
    }

    UiContext& ctx;
    const Theme& theme;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonitorView)
};

} // namespace livedaw::ui::universal
