#pragma once

#include "ui/universal/BlockEditor.h"
#include "ui/universal/ChainList.h"
#include "ui/universal/SendsPanel.h"

namespace livedaw::ui::universal
{

/** The inspector: the selected strip in depth, beside the MIXER view. CHAIN
    lists its processing (blocks, taps, the fader) with the selected block's
    editor under it; SENDS has its sends. */
class Inspector final : public juce::Component, private juce::Timer
{
public:
    Inspector (UiContext&, const Theme&);
    ~Inspector() override;

    void setStrip (Strip*);
    Strip* getStrip() const noexcept { return strip; }

    enum class Page
    {
        Chain,
        Sends
    };
    void showPage (Page);
    Page getPage() const noexcept { return page; }
    /** Takes the strip's values (the timer does this 30 times a second). */
    void refresh();

    /** The CLOSE key. */
    std::function<void()> onClose;

    ToggleKey chainTab, sendsTab, closeKey;
    ChainList chain;
    BlockEditor editor;
    SendsPanel sends;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { refresh(); }
    void editSelectedStep();

    UiContext& ctx;
    const Theme& theme;
    Strip* strip = nullptr;
    int structureVersion = -1;
    Page page = Page::Chain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Inspector)
};

} // namespace livedaw::ui::universal
