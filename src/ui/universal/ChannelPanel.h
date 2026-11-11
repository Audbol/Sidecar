#pragma once

#include "ui/UiContext.h"
#include "ui/universal/BlockEditor.h"
#include "ui/universal/BlockPicture.h"
#include "ui/universal/PluginBlockCard.h"
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

    /** The panel's height: dragged on the bar along its bottom edge (kept in the settings under
        kHeightSetting), or the MAXIMISE key / a double tap on the bar: most of the view, one compact
        row of faders left under it. `onSizeChanged` asks the view to lay out again. */
    static constexpr const char* kHeightSetting = "ui.universalChannelHeight";
    static constexpr int kDefaultHeight = 300, kMinHeight = 220, kBarHeight = 32, kCompactFaders = 230;
    std::function<void()> onSizeChanged;
    /** The view's height (the panel takes what it may of it). */
    void setViewHeight (int h) noexcept { viewHeight = h; }
    int getPanelHeight() const;
    bool isMaximised() const noexcept { return maximised; }
    void setMaximised (bool);
    void setPanelHeight (int);
    juce::Rectangle<int> getDragBar() const { return getLocalBounds().removeFromBottom (kBarHeight); }
    ToggleKey& getMaximiseKey() noexcept { return maxKey; }

    /** The extra EQ and dynamics blocks of the strip's chain as cards of their own, in signal order
        ("EQ 2", "DYNAMICS 2"...), rebuilt as the chain changes. */
    int numExtraCards() const noexcept { return (int) extras.size(); }
    juce::String extraCardName (int i) const { return extras[(size_t) i]->name; }
    /** Extra card `i` as a plug-in block's (nullptr: an EQ or dynamics card). */
    PluginBlockCard* getExtraPluginCard (int i) const noexcept { return extras[(size_t) i]->plugin.get(); }
    ResponseView* getExtraEqView (int i) { return extras[(size_t) i]->eqView.get(); }
    TransferView* getExtraDynamicsView (int i) { return extras[(size_t) i]->dynView.get(); }
    ToggleKey& getMuteKey() noexcept { return muteKey; }
    ToggleKey& getSoloKey() noexcept { return soloKey; }
    ToggleKey& getCopyKey() noexcept { return copyKey; }
    ToggleKey& getPasteKey() noexcept { return pasteKey; }

    /** The strip it shows (nullptr: none, a hint instead). */
    void setStrip (Strip*);
    Strip* getStrip() const noexcept { return strip; }
    /** Lets go of the strip if the engine has removed it since (a show loaded, a channel deleted). */
    void dropStaleStrip();
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
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    struct ExtraCard
    {
        ChainStep step;
        BlockTarget target;
        juce::String name;
        std::unique_ptr<ToggleKey> onKey;
        std::unique_ptr<ResponseView> eqView;
        std::unique_ptr<TransferView> dynView;
        std::unique_ptr<PluginBlockCard> plugin; // a plug-in block's card (no ON key or curve)
        juce::Rectangle<int> area;
    };
    std::vector<std::unique_ptr<ExtraCard>> extras;
    void rebuildExtras();
    int viewHeight = 1000, height = kDefaultHeight, dragStartHeight = 0, dragStartY = 0;
    bool maximised = false, draggingBar = false;
    ToggleKey maxKey;
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
    int seenStructureVersion = -1;

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
