#pragma once

#include "ui/UiContext.h"
#include "ui/universal/BlockEditor.h"
#include "ui/universal/BlockPicture.h"
#include "ui/universal/SendsPanel.h"
#include "ui/universal/ValueControls.h"
#include "ui/universal/Widgets.h"

#include "engine/ConsoleState.h"

#include <array>
#include <optional>

namespace livedaw::ui::universal
{

/** The selected strip, all of it at once, across the top of the MIXER view: its
    INPUT (trim, balance, polarity, delay), FILTER, GATE, EQ and DYNAMICS with their
    curves and handles (an EQ band's frequency and gain by dragging, its Q with the
    wheel; a dynamics block's threshold and ratio or range), its CHAIN in order (the
    blocks and taps the Universal core added among the strip's own) and its SENDS. */
class ChannelPanel final : public juce::Component, private juce::Timer
{
public:
    ChannelPanel (UiContext&, const Theme&);
    ~ChannelPanel() override;

    /** The header SOF key (sends on fader): the view menu. */
    std::function<void()> onSendsOnFader;
    /** The card shown large (a Section: input, filter, gate, EQ or dynamics; -1: none). Tapping a
        card title opens it in place, tapping it again closes it; `animate` false jumps. */
    void setExpanded (int section, bool animate = true);
    int getExpanded() const noexcept { return expanded; }
    /** How wide card `section` is now (for the tests). */
    int cardWidth (int section) const noexcept { return areas[(size_t) section].getWidth(); }
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
    /** The header height, over the cards. */
    static constexpr int kHeader = 44;
    ToggleKey& getMuteKey() noexcept { return muteKey; }
    ToggleKey& getSoloKey() noexcept { return soloKey; }
    ToggleKey& getCopyKey() noexcept { return copyKey; }
    ToggleKey& getPasteKey() noexcept { return pasteKey; }

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

    /** The RTA key on the EQ card (on by default, kept in the app settings under kRtaSetting). */
    static constexpr const char* kRtaSetting = "ui.universalEqRta";
    ToggleKey& getRtaKey() noexcept { return rtaKey; }
    /** Whether the analyser is running for this panel (only while it is shown, a strip is picked and the key is on). */
    bool isRtaRunning() const noexcept { return rtaRunning; }
    void visibilityChanged() override;
    /** Starts or stops the analyser for what is shown now (also called when the view holding the panel is shown or hidden). */
    void updateRta();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Colour cardColour (int section) const;
    int expanded = -1, shownExpanded = -1;
    float expandAmount = 0.0f;
    ToggleKey muteKey, soloKey, sofKey, copyKey, pasteKey, editKey, routeKey;
    std::optional<state::StripRef> copied;
    static const char* sectionName (int);
    double sampleRate() const;
    void bindStrip();
    bool rtaRunning = false;
    Analysis::Bands rtaBands;
    ToggleKey rtaKey;

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
    /** CHAIN's rows: each block with its picture, as the strip body of the MIXER view draws it. */
    struct Thumb
    {
        ChainStep step;
        BlockTarget target;
        BlockPicture picture;
    };
    std::vector<Thumb> thumbs;
    std::vector<double> thumbFreqs;
    int thumbTick = 0;
    void updateThumbs();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelPanel)
};

} // namespace livedaw::ui::universal
