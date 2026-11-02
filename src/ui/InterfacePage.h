#pragma once

#include "ui/UiContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace livedaw::ui
{

/** The audio interface's name on ROUTING: its driver ("ASIO", "CORE AUDIO", "ALSA",
    "JACK", "WASAPI" ...), or INTERFACE without a device. */
juce::String interfaceLabel (const MixerEngine&);

/** The interface patch belongs to the computer's interface, not to a show: it is
    kept in the app settings per driver and device, and comes back with them. */
void loadInterfacePatch (UiContext&);
void saveInterfacePatch (UiContext&);

/** ROUTING's page for the audio interface (Sidecar's own, a device beside the
    console's port groups): each of its inputs and the console input port (LOCAL IN,
    AES50 B or C) it feeds, and each of its outputs and the console output port it
    plays. Straight through by default; STRAIGHT puts it back. Its own channel names
    from the driver. */
class InterfacePage final : public juce::Component, private juce::Timer
{
public:
    explicit InterfacePage (UiContext&);
    ~InterfacePage() override;

    /** Patches interface input `input` to console input port `port` alone (-1: to none). */
    void patchInput (int input, int port);
    /** Makes interface output `output` play console output port `port` (-1: none). */
    void patchOutput (int output, int port);
    /** What a row shows: "LCL 3", "B 1, C 2", "-". */
    juce::String inputText (int input) const;
    juce::String outputText (int output) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    class Column;
    void timerCallback() override;
    void changed();

    UiContext& ctx;
    std::unique_ptr<Column> inputs, outputs;
    juce::Viewport inputView, outputView;
    juce::Rectangle<int> header, straightKey, inputCaption, outputCaption;
    juce::String shown;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InterfacePage)
};

} // namespace livedaw::ui
