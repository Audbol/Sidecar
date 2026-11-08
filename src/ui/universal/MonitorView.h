#pragma once

#include "ui/UiContext.h"
#include "ui/universal/Groups.h"
#include "ui/universal/ValueControls.h"

namespace livedaw::ui::universal
{

/** The MONITORING view: the two monitor paths, SPEAKERS and PHONES, each with
    its source (the WING's monitor source list), levels, DIM, MONO and MUTE,
    outputs and meter, and its processing (DIRECT IN, the 8-band EQ with its
    curve, PAN and WIDTH, INVERT, DELAY by distance, the LIMITER with its gain
    reduction); then the solo settings (the WING's solo bus and solo modes, PFL
    or AFL by kind of strip, CLEAR SOLO) and talkback (the talkback channel,
    TALK LEVEL, the TALK A and TALK B keys, and each talk path's mode, dims
    and destinations). */
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
        // Processing (engine monitor.rs): DIRECT IN -> EQ -> PAN / WIDTH -> INVERT -> DELAY -> LIMITER.
        ChoiceBar directIn;
        ValueBar directLevel;
        ToggleKey eqOn, invert, delayOn;
        ResponseView eq;
        ValueBar pan, width, distance, limiter;

        /** The height the path needs with an EQ graph worth having (its scroller
            gives it at least this; a small window scrolls). */
        int preferredHeight() const;
        /** The limiter's largest gain reduction shown (dB, 0 or below), held a moment. */
        float getLimiterReduction() const noexcept { return limiterGr; }
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        UiContext& ctx;
        const Theme& theme;
        bool phones;
        float limiterGr = 0.0f;
        int heldTicks = 0;
        std::vector<std::pair<juce::Component*, int>> rows;
        juce::Rectangle<int> processingCaption, reductionArea;
    };

    /** Talkback: the talkback channel, TALK LEVEL, TALK A / TALK B (pressed and
        released as on the console: the path's mode decides), and the settings
        of the talk path chosen by its tab. */
    class Talkback final : public juce::Component
    {
    public:
        Talkback (UiContext&, const Theme&);

        ChoiceBar channel;
        ValueBar talkLevel;
        ToggleKey talkA, talkB, tabA, tabB;
        ChoiceBar mode;
        ValueBar monDim, busDim;
        ToggleKey useSendLevels;
        GroupGrid destinations;

        /** The height its controls and every destination key need at a width. */
        int preferredHeight (int width) const;
        /** The talk path whose settings show (0 = A, 1 = B). */
        void showPath (int path);
        int getShownPath() const noexcept { return shown; }
        /** The destinations' keys: the console's buses, then mains, then matrices
            (the first 256 of each). */
        int numDestinations() const;
        /** Destination key `i`: (kind, its number in the talk sets: a bus's slot). */
        std::pair<StripKind, int> destination (int i) const;
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void bindPath();

        UiContext& ctx;
        const Theme& theme;
        int shown = 0;
        bool downA = false, downB = false;
        juce::Rectangle<int> toCaption;
    };

    Path speakers, phones;
    /** The paths' scrollers (a small window scrolls them rather than squash the EQ). */
    juce::Viewport speakersView, phonesView;
    ChoiceBar soloBus, soloMode;
    ToggleKey aflChannels, aflBuses, aflMains, aflMatrices, clearSolo;
    Talkback talkback;
    juce::Viewport talkbackView;

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
    juce::Rectangle<int> soloPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonitorView)
};

} // namespace livedaw::ui::universal
