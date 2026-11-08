#pragma once

#include "ui/UiContext.h"
#include "ui/universal/ChannelPanel.h"
#include "ui/universal/Groups.h"
#include "ui/universal/Widgets.h"

#include <memory>
#include <vector>

namespace livedaw::ui::universal
{

/** A strip's processing at a glance: its input level, its GATE's and
    DYNAMICS' gain reduction (their names lit while on), its EQ's curve (as the
    engine runs it, taken again only when the EQ changes) and its output
    level. Taller, it gives the EQ the room. */
class StripOverview final : public juce::Component
{
public:
    StripOverview (UiContext&, Strip&, const Theme&);

    void refresh();
    /** What it shows (for the tests): levels 0..1 on the fader scale, reductions in dB. */
    float getInput() const noexcept { return input; }
    float getOutput() const noexcept { return output; }
    float getGateReduction() const noexcept { return gateGr; }
    float getDynamicsReduction() const noexcept { return dynGr; }
    const std::vector<double>& getEqCurve() const noexcept { return eqCurve; }

    void paint (juce::Graphics&) override;

private:
    UiContext& ctx;
    Strip& strip;
    const Theme& theme;
    float input = 0.0f, output = 0.0f, gateGr = 0.0f, dynGr = 0.0f;
    bool gateOn = false, dynOn = false, eqOn = false;
    std::vector<float> eqState;
    std::vector<double> eqFreqs, eqCurve;
};

/** One strip of any kind as a column of the MIXER view: its colour, name and
    label (a tap selects it), pan, MUTE and SOLO, its overview (StripOverview),
    the fader with its output meter (hidden with the view's FADERS key, the
    overview then taking the column), and the fader's value. */
class StripColumn final : public juce::Component
{
public:
    StripColumn (UiContext&, Strip&, const Theme&);
    /** Shows or hides the fader (the overview takes its room). */
    void setFaderShown (bool);
    bool isFaderShown() const noexcept { return faderShown; }

    Strip& getStrip() const noexcept { return strip; }
    /** Takes the strip's values and meters (the view's timer calls it). */
    void refresh();
    void setSelected (bool shouldBeSelected);
    bool isSelected() const noexcept { return selected; }

    ToggleKey muteKey, soloKey;
    PanControl pan;
    StripOverview overview;
    FaderControl fader;
    MeterControl meter;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> header() const noexcept;

    UiContext& ctx;
    Strip& strip;
    const Theme& theme;
    juce::String name;
    int colour = 0;
    float shownDb = 0.0f;
    bool selected = false, faderShown = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripColumn)
};

/** The MIXER view: a column per strip, every kind and any number of each (the
    console's configuration), then a column per DCA and the mute groups' keys,
    side by side and scrolled sideways, with keys that show one kind at a time
    (DCAS: the DCAs and the mute groups). */
class MixerView final : public juce::Component, private juce::Timer
{
public:
    MixerView (UiContext&, const Theme&);
    ~MixerView() override;

    /** -1: every kind; a StripKind; or kDcas. */
    void setFilter (int kind);
    static constexpr int kDcas = kNumStripKinds;
    /** The FADERS key: the columns show their faders, or only their overviews. */
    void setFadersShown (bool);
    bool areFadersShown() const noexcept { return fadersShown; }
    /** The CHANNEL key: the selected strip's panel (ChannelPanel) across the top. */
    void setChannelShown (bool);
    bool isChannelShown() const noexcept { return channelShown; }
    ChannelPanel& getChannelPanel() noexcept { return channel; }
    int getFilter() const noexcept { return filter; }
    /** The strips changed (the view also notices by itself). */
    void rebuild();
    void selectionChanged();
    /** Takes every column's values and meters (the timer does this 30 times a second). */
    void refresh();

    int getNumColumns() const noexcept { return (int) columns.size(); }
    StripColumn* getColumn (int index) const noexcept;
    int getNumDcaColumns() const noexcept { return (int) dcaColumns.size(); }
    DcaColumn* getDcaColumn (int index) const noexcept;
    /** The mute groups' panel (null while the filter hides the DCAs). */
    MuteGroupsPanel* getMuteGroups() const noexcept { return muteGroups.get(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override
    {
        if (isVisible())
            refresh();
    }
    void layoutColumns();

    UiContext& ctx;
    const Theme& theme;
    int filter = -1;
    int structureVersion = -1;
    std::vector<std::unique_ptr<ToggleKey>> filterKeys;
    ToggleKey fadersKey, channelKey;
    bool fadersShown = true, channelShown = true;
    ChannelPanel channel;
    int ticks = 0;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<StripColumn>> columns;
    std::vector<std::unique_ptr<DcaColumn>> dcaColumns;
    std::unique_ptr<MuteGroupsPanel> muteGroups;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};

} // namespace livedaw::ui::universal
