#pragma once

#include "ui/ConsoleTape.h"
#include "ui/HardwareLook.h"
#include "ui/Knobs.h"
#include "ui/Skin.h"
#include "ui/UiContext.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace livedaw::ui
{

class Xenyx1204Surface;

/** The 1204USB's panel: a straight-on render's, drawn from its shapes and colours (no maker's
    artwork or logo). Its processing is `livedaw_dsp::xenyx1204`; its routing is the profile's.
    Sidecar's own additions are the console tape under each channel and the scenes bar over the
    panel; neither is on the mixer. */
namespace xy
{
    /** The panel's look: dark charcoal, black rubber keys, white-lined fader caps. */
    const hw::Look& look();

    /** Anything on the panel that follows the engine. */
    class Control : public juce::Component
    {
    public:
        virtual void refresh() = 0;
    };

    /** A knob with a coloured cap (blue COMP, navy EQ, red AUX 1, orange AUX 2, grey PAN). */
    class Knob final : public Control
    {
    public:
        Knob (juce::Colour cap, KnobBinding);
        void setBinding (KnobBinding);
        const KnobBinding& getBinding() const noexcept { return binding; }
        void refresh() override;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        juce::Colour cap;
        KnobBinding binding;
        float shown = -1.0f, dragStart = 0.0f;
    };

    /** A rubber key; `face` colours its cap (the red source and 2-TR keys), `lit` is its LED. */
    class Key final : public Control
    {
    public:
        Key (juce::Colour face, juce::Colour led);
        std::function<bool()> isOn;
        std::function<void()> onClick;
        void refresh() override;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::Colour face, led;
        bool shownOn = false, down = false;
    };

    /** A fader on the 1204USB's 60 mm travel, its cap on the engine's fader law. */
    class Fader final : public Control
    {
    public:
        Fader (juce::Colour cap, KnobBinding);
        void setBinding (KnobBinding);
        const KnobBinding& getBinding() const noexcept { return binding; }
        void refresh() override;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        /** Where the cap's centre sits for a position 0..1, in this component. */
        float capY (float position) const noexcept;

    private:
        juce::Colour cap;
        KnobBinding binding;
        float shown = -1.0f;
    };

    /** A part of the panel that is only pressed (a jack: the plug it takes). */
    class Hotspot final : public juce::Component
    {
    public:
        std::function<void()> onClick;
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasClicked() && onClick)
                onClick();
        }
    };
} // namespace xy

class Xenyx1204Surface final : public Skin,
                               private juce::Timer
{
public:
    explicit Xenyx1204Surface (UiContext&);
    ~Xenyx1204Surface() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshAll() override;
    void showChanged() override;

    static constexpr int kDesignWidth = 1920, kDesignHeight = 1080;
    /** The panel's own space (a render's pixels) and the bar over it, in this component's pixels. */
    static constexpr int kPanelWidth = 1536, kPanelHeight = 1870, kBarHeight = 52;
    static constexpr int kChannels = 6;

    /** The panel's drawing (the panel component calls it). */
    void paintPanel (juce::Graphics&);

    /** The panel holding every control, in the render's pixels (tests find controls by component ID:
        "CH1 GAIN", "CH3 ALT", "ALT FADER", "SOURCE ALT"...). */
    juce::Component& getPanel() noexcept;
    xy::Control* findControl (const juce::String& id);
    ConsoleTape& getTape (int channel) { return *tapes[(size_t) channel]; }
    juce::ComboBox& getSceneBox() noexcept { return sceneBox; }

    /** The scenes bar: STORE writes the current scene (or a new one), NEW adds one. */
    void storeScene();
    void newScene();
    void recallSelected();

private:
    class Panel;

    void timerCallback() override;
    void build();
    void bindControls();
    void ensureRouting();
    void fillScenes();
    Strip* channel (int i) const;
    Strip* aux (int i) const;
    Strip* bus (int i) const;
    Strip* main() const;
    xy::Knob& knob (const juce::String& id, juce::Colour cap, juce::Rectangle<int>);
    xy::Key& key (const juce::String& id, juce::Colour face, juce::Colour led, juce::Rectangle<int>);
    xy::Fader& fader (const juce::String& id, juce::Colour cap, juce::Rectangle<int>);

    UiContext& ctx;
    std::unique_ptr<Panel> panel;
    std::vector<xy::Control*> controls;
    std::array<std::unique_ptr<ConsoleTape>, kChannels> tapes;
    std::array<xy::Hotspot, 4> micJacks, lineJacks;
    std::array<xy::Hotspot, kChannels> soloKeys;
    juce::TextButton prevKey { "PREV" }, nextKey { "NEXT" }, storeKey { "STORE" }, newKey { "NEW" };
    juce::ComboBox sceneBox;
    juce::Label sceneTitle { {}, "SCENES" };
    bool phantom = false;
    int shownScenes = -1;
};

} // namespace livedaw::ui
