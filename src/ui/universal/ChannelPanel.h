#pragma once

#include "ui/UiContext.h"
#include "ui/universal/SendsPanel.h"
#include "ui/universal/ValueControls.h"
#include "ui/universal/Widgets.h"

#include <array>

namespace livedaw::ui::universal
{

/** The selected strip, all of it at once, across the top of the MIXER view: its
    INPUT (trim, balance, polarity, delay), FILTER, GATE, EQ and DYNAMICS with their
    curves and handles (an EQ band's frequency and gain by dragging, its Q with the
    wheel; a dynamics block's threshold and ratio or range), its CHAIN in order (the
    blocks and taps the Universal core added among the strip's own) and its SENDS. */
class ChannelPanel final : public juce::Component
{
public:
    ChannelPanel (UiContext&, const Theme&);
    ~ChannelPanel() override;

    /** The strip it shows (nullptr: none, a hint instead). */
    void setStrip (Strip*);
    Strip* getStrip() const noexcept { return strip; }
    /** Takes the strip's values, curves and meters again (the view's timer). */
    void refresh();

    ResponseView& getFilterView() noexcept { return filterView; }
    ResponseView& getEqView() noexcept { return eqView; }
    TransferView& getGateView() noexcept { return gateView; }
    TransferView& getDynamicsView() noexcept { return dynView; }
    /** A tap on a section's title or a CHAIN row: that chain step (its kind, an extra block's id or -1). */
    std::function<void (int stepKind, int uid)> onOpenStep;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum Section
    {
        InputSection,
        FilterSection,
        GateSection,
        EqSection,
        DynamicsSection,
        ChainSection,
        SendsSection,
        NumSections
    };
    static const char* sectionName (int);
    double sampleRate() const;
    void bindStrip();

    UiContext& ctx;
    const Theme& theme;
    Strip* strip = nullptr;

    ValueBar trim, balance, delay;
    ToggleKey invert, delayOn, lowCut, highCut, gateOn, eqOn, dynOn;
    ResponseView filterView, eqView;
    TransferView gateView, dynView;
    SendsPanel sends;
    std::array<juce::Rectangle<int>, NumSections> areas;
    juce::Rectangle<int> chainList;
    std::vector<ChainStep> shownSteps() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelPanel)
};

} // namespace livedaw::ui::universal
