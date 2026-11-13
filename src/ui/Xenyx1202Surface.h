#pragma once

#include "ui/AnalogSurface.h"

#include <array>

namespace livedaw::ui
{

/** The Q1202USB's panel: the owner's console photographed straight on, drawn from its shapes and
    colours (no maker's name, artwork or logo). Its processing is `livedaw_dsp::xenyx1202`; its routing is the profile's.
    Sidecar's own additions are the console tape under each channel and the scenes bar over the
    panel; neither is on the mixer. */
namespace xy
{
    /** The panel's look: charcoal, grey plastic knobs with coloured caps and white pointers. */
    const hw::Look& look();

    /** The panel's controls are the analog mixers' shared ones. */
    using Control = analog::Control;
    using Knob = analog::Knob;
    using Key = analog::Key;
    using Fader = analog::Fader;
    using Hotspot = analog::Hotspot;
} // namespace xy

class Xenyx1202Surface final : public AnalogSurface
{
public:
    explicit Xenyx1202Surface (UiContext&);
    ~Xenyx1202Surface() override;

    static constexpr int kDesignWidth = 1920, kDesignHeight = 1080;
    /** The panel's own space (a render's pixels); the bar over it is `kBarHeight`, in this component's pixels. */
    static constexpr int kPanelWidth = 1380, kPanelHeight = 1410;
    static constexpr int kChannels = 8;

    /** The panel's drawing. The controls are found by component ID: "CH1 GAIN", "CH3 FX", "CH5 LEVEL", "MAIN FADER", "USB TO MAIN"... */
    void paintPanel (juce::Graphics&) override;

private:
    const hw::Look& panelLook() const override;
    void bindControls() override;
    AnalogRoutingSpec routingSpec() override;
    void repaintLive() override;
    void build();
    void ensureRouting();
    /** Where a channel sits on the panel: its row, and whether it is the right-hand one of a stereo pair. */
    static int rowOf (int channel) noexcept;
    static bool rightOf (int channel) noexcept;

    std::array<xy::Hotspot, 4> micJacks, lineJacks;
    bool phantom = false;
};

} // namespace livedaw::ui
