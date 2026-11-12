#pragma once

#include "engine/PluginChain.h"
#include "ui/UiContext.h"
#include "ui/universal/PatchGrid.h"
#include "ui/universal/Widgets.h"

#include <array>
#include <memory>
#include <vector>

namespace livedaw::ui::ph
{

/** The ROUTING page of the PLUGIN HOST's screen (docs/PLUGIN_HOST.md): patch grids in the style of the
    Q1202USB's routing page, which scroll when there are more channels than fit.

    - INPUTS: the interface's inputs across the top, the channels down the side; a tap patches a channel
      to an input (one input per channel; a tap on the lit point takes the patch away).
    - OUTPUTS: the channels across the top, the interface's outputs down the side; a tap makes an output
      play the channel after its plug-ins (an output has one source).
    - KEYS: for the selected channel, a row for each of its plug-ins that has a sidechain input, and across
      the top NONE and each channel's input (IN) and output (OUT); a tap keys the plug-in from there. A
      channel cannot key its own plug-ins from its output (it would feed itself), so that point is dim.
      Plug-ins that run in the sandbox have no sidechain input (their buses are stereo only) and so no row.
    RESET puts the inputs and outputs back to 1:1; it asks to be pressed twice. Message thread only. */
class RoutingPage final : public juce::Component, private juce::Timer
{
public:
    RoutingPage (UiContext&, const universal::Theme&);
    ~RoutingPage() override;

    enum class View
    {
        Inputs,
        Outputs,
        Keys
    };
    void showView (View);
    View getView() const noexcept { return view; }

    /** The channel the KEYS view shows (0-based). */
    void setChannel (int index);
    int getChannel() const noexcept { return channel; }
    /** Reads the engine again (the timer does this too). */
    void refresh();

    universal::PatchGrid& getInputsGrid() noexcept { return inGrid; }
    universal::PatchGrid& getOutputsGrid() noexcept { return outGrid; }
    universal::PatchGrid& getKeysGrid() noexcept { return keyGrid; }

    /** A KEYS row: one plug-in of the selected channel's chain that has a sidechain input. */
    struct KeyRow
    {
        int slot = -1;   // the block's FxSlots number
        int member = 0;  // the plug-in's place in the block's chain
        juce::String name;
        PluginChain::Key key;
    };
    int getNumKeyRows() const noexcept { return (int) keyRows.size(); }
    const KeyRow& getKeyRow (int row) const { return keyRows[(size_t) row]; }
    /** The KEYS grid's column for a key (0: none; then each channel's IN and OUT), and back. */
    static int keyColumn (PluginChain::Key) noexcept;
    static PluginChain::Key keyAt (int column) noexcept;
    /** Keys a row's plug-in (as a tap on the grid does); false if refused. */
    bool setKey (int row, PluginChain::Key);
    /** "NONE", "CH 6 IN", "CH 6 OUT". */
    static juce::String describeKey (PluginChain::Key);

    /** The 1:1 patch: channel N on interface input N, output N playing channel N. */
    void resetToDefaults();
    /** RESET: the first press arms it ("PRESS AGAIN"), the second resets; it disarms after a few seconds. */
    universal::ToggleKey& getResetKey() noexcept { return resetKey; }
    bool isResetArmed() const noexcept { return armed; }
    universal::ToggleKey& getViewKey (View v) noexcept { return *viewKeys[(size_t) v]; }

    /** What the KEYS view says when there is nothing to key (empty: it shows rows). */
    juce::String getKeysNote() const;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kHead = 64;

private:
    void timerCallback() override;
    void build();
    void rebuildKeyRows();
    Strip* strip (int index) const;
    int numChannels() const;
    void pressReset();
    void disarm();

    UiContext& ctx;
    const universal::Theme& theme;
    View view = View::Inputs;
    int channel = 0;
    bool armed = false;
    juce::uint32 armedAt = 0;
    std::array<std::unique_ptr<universal::ToggleKey>, 3> viewKeys;
    universal::ToggleKey resetKey;
    universal::PatchGrid inGrid, outGrid, keyGrid;
    std::vector<KeyRow> keyRows;
    juce::String keySignature;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoutingPage)
};

} // namespace livedaw::ui::ph
