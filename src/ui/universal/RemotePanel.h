#pragma once

#include "engine/MidiRemote.h"
#include "engine/WebRemote.h"
#include "ui/universal/Groups.h"
#include "ui/universal/ValueControls.h"

#include <array>

namespace livedaw::ui::universal
{

/** A value shown as a field (its caption at the left, the value at the
    right); a tap asks for a new one (a port, a PIN, a name). */
class TextField final : public juce::Component
{
public:
    explicit TextField (const Theme&);

    juce::String caption, prompt;
    std::function<juce::String()> value;
    /** The text typed; false: not taken (the field says why with `rejectReason`). */
    std::function<bool (const juce::String&)> change;
    /** Red while true (a port that couldn't be opened). */
    std::function<bool()> failed;
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    const Theme& theme;
    juce::String shown;
    bool shownFailed = false;
};

/** SETUP > REMOTE: the remotes' settings, kept in the app settings as on the
    WING surface's SETUP and applied at once. WING REMOTE (WING apps over the
    network: discovery, OSC, the native protocol, the console name), WEB REMOTE
    (the screen in a browser: port, PIN, view only, the addresses to open) and
    MIDI (each MIDI REMOTE CONTROL function's route, the DIN and USB ports'
    devices, DAW control). */
class RemotePanel final : public juce::Component, private juce::Timer
{
public:
    RemotePanel (UiContext&, const Theme&);

    // WING REMOTE
    ToggleKey wingOn, discoveryOn, oscOn, nativeOn;
    TextField consoleName, discoveryPort, oscPort, nativePort;
    // WEB REMOTE
    ToggleKey webOn, viewOnly;
    TextField webPort, webPin;
    // MIDI
    std::array<std::unique_ptr<ChoiceBar>, 8> routes;
    std::array<std::unique_ptr<ChoiceBar>, 4> devices; // DIN IN, DIN OUT, USB IN, USB OUT
    ToggleKey dawOn;
    ChoiceBar dawEmulation;
    std::array<std::unique_ptr<ChoiceBar>, 2> dawDevices; // MIDI DAW 1 IN, OUT

    void refresh();
    /** The status lines under WING REMOTE and WEB REMOTE (for the tests). */
    juce::StringArray getWingStatus() const;
    juce::StringArray getWebStatus() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override
    {
        if (isShowing())
            refresh();
    }
    void changeWing (const std::function<void (WingRemote::Settings&)>&);
    void changeWeb (const std::function<void (WebRemote::Settings&)>&);
    void changeMidi (const std::function<void (MidiRemote::Settings&)>&);
    MidiRemote::Settings midiSettings() const;
    void bindDevice (ChoiceBar&, int port, bool input, const juce::String& caption);

    UiContext& ctx;
    const Theme& theme;
    juce::Rectangle<int> wingPanel, webPanel, midiPanel, wingStatusArea, webStatusArea;
    juce::StringArray wingStatus, webStatus;
};

} // namespace livedaw::ui::universal
