#pragma once

#include "ui/ConsoleTape.h"
#include "ui/HardwareLook.h"
#include "ui/Knobs.h"
#include "ui/Skin.h"
#include "ui/UiContext.h"
#include "ui/universal/PatchGrid.h"
#include "ui/universal/Widgets.h"

#include <functional>
#include <memory>
#include <vector>

namespace livedaw::ui
{

/** The parts of the panel that follow the engine, shared by every analog mixer's surface. */
namespace analog
{
    /** The look a control is drawn in when its surface gives none. */
    const hw::Look& defaultLook();

    /** Anything on the panel that follows the engine. */
    class Control : public juce::Component
    {
    public:
        virtual void refresh() = 0;
        void setLook (const hw::Look& l) noexcept { look = &l; }
        const hw::Look& getLook() const noexcept { return look != nullptr ? *look : defaultLook(); }

    private:
        const hw::Look* look = nullptr;
    };

    /** A knob with a coloured cap. */
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

    /** A push switch; `face` colours its cap, `led` is its lamp. */
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

    /** A fader, lying down or standing up by the shape of its bounds; -oo at the left or the bottom. */
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
        float capX (float position) const noexcept;

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
} // namespace analog

/** What a surface says of its routing page: the rows of the inputs grid (the strips the interface's inputs
    feed) and the columns of the outputs grid (the mix's signals that the interface's outputs carry). */
struct AnalogRoutingSpec
{
    struct Row
    {
        Strip* strip = nullptr;
        /** What the row is called; empty: the strip's own label. */
        juce::String name;
        /** A stereo return (2-TRACK): a tap patches a pair of inputs and makes the strip stereo. */
        bool stereoReturn = false;
    };
    struct Signal
    {
        juce::String name;
        std::atomic<int32_t>* out = nullptr;
    };
    std::vector<Row> rows;
    std::vector<Signal> signals;
    /** The line under the page's title, for the inputs and for the outputs. */
    juce::String inputsHint, outputsHint;
};

/** The base of every analog mixer's surface. Analog mixers have no scene memory and no names, so Sidecar adds
    a scenes bar over the panel and a strip of console tape under each channel; neither is on the mixer. The
    panel is drawn in its own pixel space and scaled to fit under the bar. This class holds what they all share:
    the bar (scenes, the ROUTING key, the UNIVERSAL MIXER key), the panel host, the controls and tapes with
    their refresh, findControl by component ID, and the routing page. A mixer's surface supplies its panel
    drawing (`paintPanel`), its control layout (`build`, in the constructor), the controls' bindings
    (`bindControls`) and its routing grid's labels (`routingSpec`); see docs/ANALOG_MIXERS.md. */
class AnalogSurface : public Skin,
                      private juce::Timer
{
public:
    /** The scenes bar's height, in this component's pixels. */
    static constexpr int kBarHeight = 52;

    ~AnalogSurface() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshAll() override;
    void showChanged() override;

    /** The panel's drawing (the panel component calls it), in the panel's own pixels. */
    virtual void paintPanel (juce::Graphics&) = 0;

    /** The panel holding every control (tests find controls by component ID). */
    juce::Component& getPanel() noexcept;
    analog::Control* findControl (const juce::String& id);
    ConsoleTape& getTape (int channel) { return *tapes[(size_t) channel]; }
    juce::ComboBox& getSceneBox() noexcept { return sceneBox; }
    /** The routing page (the interface's inputs to the channels, the outputs to the interface): shown over the panel. */
    void showRouting (bool shown);
    bool isRoutingShown() const noexcept;
    juce::Component* getRoutingPage() noexcept;
    /** The routing page's patch grid: the inputs' or the outputs' (shows the page if it is not up yet). */
    universal::PatchGrid& getRoutingGrid (bool outputs);

    /** The scenes bar: STORE writes the current scene (or a new one), NEW adds one. */
    void storeScene();
    void newScene();
    void recallSelected();

protected:
    /** `panelWidth` x `panelHeight` is the panel's own space. */
    AnalogSurface (UiContext&, int panelWidth, int panelHeight);

    /** Called once, at the end of the subclass's constructor, after its controls are built: binds, fills the bar and starts the refresh. */
    void start();
    /** Stops the refresh (a subclass's destructor, before its own members go). */
    void stopRefresh() { stopTimer(); }

    /** The look the controls are drawn in. */
    virtual const hw::Look& panelLook() const { return analog::defaultLook(); }
    /** Sets what each control does in the engine; called at every `refreshAll`, before the controls read it. */
    virtual void bindControls() {}
    /** The routing page's rows and columns; read each time the page is shown. */
    virtual AnalogRoutingSpec routingSpec() = 0;
    /** Called on every refresh of the panel (30 times a second): repaint what moves on its own, the LEDs and meters. */
    virtual void repaintLive() {}

    /** Makes a control, bounds in the panel's pixels, and parents it to the panel. The surface owns it. */
    analog::Knob& addKnob (const juce::String& id, juce::Colour cap, juce::Rectangle<int>);
    analog::Key& addKey (const juce::String& id, juce::Colour face, juce::Colour led, juce::Rectangle<int>);
    analog::Fader& addFader (const juce::String& id, juce::Colour cap, juce::Rectangle<int>);
    /** Console tape for a channel (its name is the strip's own): `seed` makes its tilt. */
    ConsoleTape& addTape (int channel, int seed, const juce::String& id, juce::Rectangle<int>);

    Strip* channel (int i) const;
    Strip* aux (int i) const;
    Strip* bus (int i) const;
    Strip* main() const;

    UiContext& ctx;
    universal::Theme theme;

private:
    class Panel;
    class RoutingOverlay;

    void timerCallback() override;
    void fillScenes();

    int panelWidth, panelHeight;
    universal::ToggleKey universalKey, routingKey;
    std::unique_ptr<Panel> panel;
    std::unique_ptr<RoutingOverlay> routingOverlay;
    std::vector<std::unique_ptr<analog::Control>> controls;
    std::vector<std::unique_ptr<ConsoleTape>> tapes;
    juce::TextButton prevKey { "PREV" }, nextKey { "NEXT" }, storeKey { "STORE" }, newKey { "NEW" };
    juce::ComboBox sceneBox;
    juce::Label sceneTitle { {}, "SCENES" };
    int shownScenes = -1;
};

} // namespace livedaw::ui
