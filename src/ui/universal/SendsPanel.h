#pragma once

#include "ui/UiContext.h"
#include "ui/universal/ValueControls.h"

#include <memory>
#include <vector>

namespace livedaw::ui::universal
{

/** What a PRO2 ignores of `from`'s send to mix `mix` (an aux send or a matrix) in `slot` (its
    pro2.rs): 0 nothing, 1 the level and FROM (an input's send to an aux send in group or mix
    minus mode: ON alone counts), 2 all of it (a linked pair's even mix takes the odd one's
    sends). Always 0 on another console. */
int pro2SendIgnored (MixerEngine&, const Strip& from, const Strip& mix, int slot);

/** A strip's sends, a row per destination it can send to on this console:
    the buses (from channels, aux and other buses), the mains (from channels,
    aux and buses) and the matrices (from all but matrices). A row has the
    send's ON, its level and where it takes the signal: PRE or POST (a bus
    send's GRP too), or one of the strip's user taps. */
class SendsPanel final : public juce::Component
{
public:
    SendsPanel (UiContext&, const Theme&);

    void setStrip (Strip*);
    /** A line per send (the channel panel): its name, ON, level and FROM side by side. */
    void setCompact (bool);
    /** Follows the console's buses, mains and matrices and the sends' values. */
    void refresh();

    /** One destination's send. */
    class Row final : public juce::Component
    {
    public:
        Row (const Theme&, const juce::String& destination);

        ToggleKey on;
        ValueBar level;
        ChoiceBar source;
        juce::String destination;
        bool compact = false;
        /** What of the send the console ignores (a PRO2's group or linked pair): 0 nothing,
            1 the level and FROM (ON alone counts), 2 all of it. Its controls are dimmed, and
            `why` says so beside the name. Unset: nothing. */
        std::function<int()> ignored;
        std::function<juce::String()> why;
        /** Dims what `ignored` says the console ignores. */
        void refreshIgnored();

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        const Theme& theme;
    };

    int getNumRows() const noexcept { return (int) rows.size(); }
    Row* getRow (int index) const noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void rebuild();
    /** The strip's user taps, by id. */
    std::vector<int> userTaps() const;

    UiContext& ctx;
    const Theme& theme;
    Strip* strip = nullptr;
    /** The strip's user taps (ids), taken each refresh. */
    std::vector<int> taps;
    int structureVersion = -1;
    bool compact = false;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Row>> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SendsPanel)
};

} // namespace livedaw::ui::universal
