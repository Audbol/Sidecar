#pragma once

#include "ui/RoutingGroups.h"
#include "ui/UiContext.h"
#include "ui/universal/PatchGrid.h"
#include "ui/universal/ValueControls.h"

#include <vector>

namespace livedaw::ui::universal
{

/** The output ports ROUTING offers: LOCAL OUT (at least the WING's 8), and
    AES50 B / C when the interface has that many outputs. */
std::vector<int> outputPorts (MixerEngine&);
/** An output port as ROUTING names it ("LCL 3", "B 1"). */
juce::String outputPortName (int port);

/** The ROUTING view: patch matrices for any configuration. INPUTS puts the
    console's channels and aux down the side against one group of sources
    (the WING's port groups, ui::routing: the interface's inputs as LOCAL IN
    and AES50 B / C, the oscillators, playback, the USB player, the strips'
    signals, the user signals); a point is a channel's MAIN input (a stereo
    source or a strip's signal takes the pair). OUTPUTS puts one group of the
    interface's outputs down the side against every bus, main and matrix side
    and the two monitor paths; a point feeds the port (replacing whatever
    fed it). */
class RoutingView final : public juce::Component, private juce::Timer
{
public:
    RoutingView (UiContext&, const Theme&);
    ~RoutingView() override;

    enum class Page
    {
        Inputs,
        Outputs
    };
    void showPage (Page);
    Page getPage() const noexcept { return page; }
    /** The group shown on the page (a source group on INPUTS, an output group on OUTPUTS). */
    void setGroup (routing::Group);
    routing::Group getGroup() const noexcept { return page == Page::Inputs ? inputGroup : outputGroup; }
    /** The groups the page offers here (by the interface's ports). */
    std::vector<routing::Group> groups() const;
    /** Takes the patch again (the timer does this 10 times a second). */
    void refresh();

    ToggleKey inputsTab, outputsTab;
    ChoiceBar group;
    PatchGrid grid;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override
    {
        if (isVisible()) // a hidden view waits
            refresh();
    }
    void rebuild();
    void patchInput (Strip&, int source);

    /** An output signal: a strip's side, or a monitor path's (no strip: 0/1 SPEAKERS, 2/3 PHONES). */
    struct Signal
    {
        Strip* strip = nullptr;
        int side = 0;
    };
    std::atomic<int32_t>* outputOf (const Signal&) const;

    UiContext& ctx;
    const Theme& theme;
    Page page = Page::Inputs;
    routing::Group inputGroup = routing::Group::LocalIn, outputGroup = routing::Group::LocalOut;
    int structureVersion = -1;
    std::vector<Strip*> inputStrips;
    std::vector<int> sources, ports;
    std::vector<Signal> signals;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoutingView)
};

} // namespace livedaw::ui::universal
