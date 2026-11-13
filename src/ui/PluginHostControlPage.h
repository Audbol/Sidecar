#pragma once

#include "ui/PluginHostControl.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <vector>

namespace livedaw::ui::ph
{

/** The CONTROL page of the PLUGIN HOST's screen (docs/PLUGIN_HOST.md, page 5): three panels.

    - MIDI DEVICES (left): the MIDI inputs of this computer, each a row; a tap switches one on or off (the
      choice is an app setting). A switched-on input that is not there says so.
    - MAPPINGS (middle): each mapped MIDI control and what it moves ("CC 21  CH 1  ->  CH 3  EQ: Cutoff"). LEARN
      waits for a tap or a move of a plug-in parameter (on the RACK page's dock, or from the picker here), a hot
      knob, a channel's SELECT or BYPASS or a scene key, and then for a MIDI control. MODE turns the selected
      controller to relative (an endless encoder) and back, DELETE removes it, CLEAR ALL removes every mapping and
      asks twice (the key turns to CONFIRM for a few seconds). A line under the list says what LEARN waits for, or
      what happened (a mapping that went with its plug-in).
    - HOT KNOBS (right): the selected channel's plug-ins, each with its two hot parameters; a tap on one opens the
      plug-in's parameter list (and NONE) to choose from.

    Rows are 48 px high, keys 52 px (touch sized in the 1920 x 1080 design space). Message thread only. */
class ControlPage final : public juce::Component, private juce::Timer
{
public:
    ControlPage (UiContext&, const universal::Theme&, Control&);
    ~ControlPage() override;

    /** The channel whose plug-ins the HOT KNOBS panel lists (0-based). */
    void setChannel (int index);
    /** Reads the devices, the mappings and the rack again (the timer does this too). */
    void refresh();

    // MIDI devices.
    int getNumDevices() const noexcept { return (int) devices.size(); }
    const juce::String& getDeviceName (int row) const { return devices[(size_t) row]; }
    bool isDeviceOn (int row) const;
    void toggleDevice (int row);

    // Mappings.
    int getNumMappings() const noexcept { return (int) mappings.size(); }
    const Control::Row& getMapping (int row) const { return mappings[(size_t) row]; }
    int getSelectedMapping() const noexcept { return selectedMapping; }
    void selectMapping (int row);
    void pressDelete();
    void pressMode();
    /** Asks twice: the first press arms CLEAR ALL, the second does it. */
    void pressClearAll();
    bool isClearArmed() const noexcept { return clearArmed; }
    universal::ToggleKey& getLearnKey() noexcept { return learnKey; }
    universal::ToggleKey& getModeKey() noexcept { return modeKey; }
    universal::ToggleKey& getDeleteKey() noexcept { return deleteKey; }
    universal::ToggleKey& getClearKey() noexcept { return clearKey; }

    // Hot knobs.
    struct HotRow
    {
        PluginRef ref;
        juce::String name;
        std::array<juce::String, 2> hot; // the parameters' names ("" for none)
        bool sandboxed = false;
        int numParameters = 0;
    };
    int getNumHotRows() const noexcept { return (int) hotRows.size(); }
    const HotRow& getHotRow (int row) const { return hotRows[(size_t) row]; }
    /** Opens the plug-in's parameter list for hot knob `knob` of the row. */
    void pickHot (int row, int knob);
    /** The list's choice: parameter `paramIndex` (-1: none) is the knob's. While LEARN waits it is learned instead. */
    void chooseHot (int row, int knob, int paramIndex);

    /** The rows' and keys' places in this component (tests, screenshots). */
    juce::Rectangle<int> getDeviceRowBounds (int row) const;
    juce::Rectangle<int> getMappingRowBounds (int row) const;
    juce::Rectangle<int> getHotKeyBounds (int row, int knob) const;
    void scrollMappings (int firstRow);
    int getFirstMappingRow() const noexcept { return mapTop; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int kRow = 48, kHead = 36, kKey = 52, kDevicesWidth = 420, kMapX = 440, kMapWidth = 700, kHotX = 1152, kGap = 12;

private:
    enum class Panel
    {
        None,
        Devices,
        Mappings,
        Hot
    };

    void timerCallback() override;
    void disarm();
    void clampScroll();
    juce::Rectangle<int> devicesRect() const;
    juce::Rectangle<int> mappingsRect() const;
    juce::Rectangle<int> hotRect() const;
    juce::Rectangle<int> listOf (Panel) const;
    int visibleRows (Panel) const;
    int& topOf (Panel);
    int numRows (Panel) const;
    juce::String messageText() const;

    UiContext& ctx;
    const universal::Theme& theme;
    Control& control;
    int channel = 0;
    universal::ToggleKey learnKey, modeKey, deleteKey, clearKey;
    juce::StringArray devices;
    std::vector<Control::Row> mappings;
    std::vector<HotRow> hotRows;
    juce::String signature;
    int selectedMapping = -1, deviceTop = 0, mapTop = 0, hotTop = 0;
    bool clearArmed = false;
    juce::uint32 armedAt = 0;

    Panel drag = Panel::None;
    int dragFrom = 0;
    bool dragged = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlPage)
};

} // namespace livedaw::ui::ph
