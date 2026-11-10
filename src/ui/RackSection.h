#pragma once

#include "ui/FaderSection.h"
#include "ui/SurfaceLayout.h"
#include "ui/SurfaceSections.h"
#include "ui/UiContext.h"

namespace livedaw::ui
{

/** The WING RACK's four-channel section, right of its display (WING manual
    4.2 p. 18-19, Fig. 12; 4.3 Table 4.6; 4.8 p. 26; layout::rack):

    - The LCD in four parts, a rotary encoder for each channel's level, a
      MUTE and a SOLO key under each. These are the left fader section's
      strips in their RACK style (FaderSection::setRackStyle), which the
      surface puts over getChannelsArea(): the section has no faders.
    - INPUT AUX, BUSES MAIN, DCA MUTE and CUST TRANS under them. Each key
      toggles between two layers: input channels 1-40 and aux channels 1-8;
      buses 1-16 and main 1-4 with matrix 1-8; DCA 1-16 and the mute groups
      (the encoders do nothing, the eight keys are the eight mute groups);
      the custom controls (16 layers of four encoders and eight keys, as the
      upper part of the full-size console's section) and the transport.
    - "<4" and "4>" step through the layer four channels at a time; held,
      they jump to its first and last channels. On the custom controls they
      step through the 16 layers (Sidecar's reading: the manual doesn't say
      how the layers are chosen; the protocol document lists a "Layer level
      indicator ... and >4 <4 buttons" in this section).
    - The two-digit display: the layer of the custom controls, else the page
      of four the channels are on (Sidecar's reading of the "layer level
      indicator").
    - VIEW (Table 4.6): OVERVIEW of the layer shown; pressed again, FADERS;
      held, CONFIG. On CUST it opens CUSTOM CONTROLS (4.8 p. 25); on the mute
      groups and the transports their screens (Sidecar's reading, as the
      full-size console's lower VIEW key).
    - The USB socket, the PHONES LEVEL pot and the headphone socket (4.7
      p. 24: "On WING RACK, only a dedicated PHONES LEVEL knob is available").

    TRANS: the manual gives one transport layer ("when a PLAYLIST is open or
    when SD cards are inserted"). Sidecar has both recorders at once, so CUST
    TRANS steps CUST, the USB player's transport, the multitrack recorder's,
    and back (Sidecar's reading).

    The custom controls, the mute groups and the transports are played by the
    custom controls section in its RACK mode (CustomControlSection::
    setRackMode), which the surface puts over the same area. */
class RackSection final : public juce::Component, private juce::Timer
{
public:
    /** What the LCD, the encoders and the eight keys show. */
    enum class Mode
    {
        Channels,
        MuteGroups,
        Custom,
        UsbTransport,
        MultiTransport
    };

    /** The four layer keys. */
    enum Key
    {
        InputAux,
        BusesMain,
        DcaMute,
        CustTrans,
        kNumKeys
    };

    RackSection (UiContext&, FaderSection& channels, CustomControlSection& controls);

    /** The surface is a RACK (true) or not any more: the channels take their
        RACK style and the custom controls their RACK mode, or give them up. */
    void setActive (bool);
    /** Structure or layers changed. */
    void refresh();

    Mode getMode() const noexcept { return mode; }
    /** The channels show (else the custom controls section does, in their place). */
    bool showsChannels() const noexcept { return mode == Mode::Channels; }
    /** Back to the channels (a fader is to be shown). */
    void showChannels();
    /** A layer key's press. */
    void pressKey (int key);
    /** "<4" (-1) or "4>" (1): a press, or held. */
    void shift (int direction, bool held);
    /** VIEW pressed (or held). */
    void pressView (bool held);
    /** What the two-digit display reads ("" when dark). */
    juce::String layerDisplayText() const;
    /** Whether a layer key's light is on. */
    bool isKeyLit (int key) const;

    /** Where the four channels go, in this section's coordinates. */
    juce::Rectangle<int> getChannelsArea() const noexcept { return plan.channels; }

    /** Called when the mode changed (the surface shows the channels or the custom controls). */
    std::function<void()> onModeChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The layers the first three keys toggle between (livedaw-session's ids). */
    static constexpr const char* kInputs = "inputs";
    static constexpr const char* kAux = "aux";
    static constexpr const char* kBuses = "bus-1";
    static constexpr const char* kMainMatrix = "out-1";
    static constexpr const char* kDca = "dca-1";

private:
    void timerCallback() override;
    void setMode (Mode);
    void applyMode();
    /** Shows `first`, or `second` when `first` is showing. */
    void toggleLayers (const char* first, const char* second);

    UiContext& ctx;
    FaderSection& channels;
    CustomControlSection& controls;
    Mode mode = Mode::Channels;
    bool active = false, viewOpened = false, spillShown = false;

    juce::OwnedArray<SurfaceKey> layerKeys;
    SurfaceKey shiftLeft { "4" }, shiftRight { "4" };
    ViewKey viewKey;
    DisplayKnob phonesLevel;
    layout::rack::Section plan;
    juce::String shownDisplay;
};

} // namespace livedaw::ui
