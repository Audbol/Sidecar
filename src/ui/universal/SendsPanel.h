#pragma once

#include "ui/UiContext.h"
#include "ui/universal/ValueControls.h"

#include <memory>
#include <vector>

namespace livedaw::ui::universal
{

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
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Row>> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SendsPanel)
};

} // namespace livedaw::ui::universal
