#pragma once

#include "ui/HardwareLook.h"
#include "ui/PluginHostRack.h"
#include "ui/PluginHostRouting.h"
#include "ui/PluginHostScenes.h"
#include "ui/ScreenMaximiser.h"
#include "ui/Skin.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <array>
#include <functional>
#include <memory>

namespace livedaw::ui
{

class PluginHostSurface;

/** The PLUGIN HOST's parts: Sidecar's own design, a dark modern rack unit (no metal, no maker's
    look). Touch first: every key is at least about 44 px high in the 1920 x 1080 design space. */
namespace ph
{
    /** The unit's look: charcoal panels, soft keys that glow, a dark LCD. */
    const hw::Look& look();

    /** A key of the unit: a soft key that glows while on. */
    class Key final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        enum class Glyph
        {
            None,
            Left,
            Right
        };
        Key (const juce::String& text, juce::Colour lit, Glyph = Glyph::None);

        std::function<bool()> isOn;
        std::function<void()> onClick;
        void setText (const juce::String&);
        const juce::String& getText() const noexcept { return text; }
        /** Presses it (the tests, the wheel). */
        void click();
        void refresh();
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::String text;
        juce::Colour litColour;
        Glyph glyph;
        bool shownOn = false, down = false;
    };

    /** A small dark LCD with up to two lines of text. */
    class Lcd final : public juce::Component
    {
    public:
        /** The lines (the first in the accent). */
        std::function<juce::StringArray()> getLines;
        juce::Colour accent;
        float firstSize = 18.0f, otherSize = 14.0f;
        void refresh();
        void paint (juce::Graphics&) override;
        const juce::StringArray& shown() const noexcept { return lines; }

    private:
        juce::StringArray lines;
    };

    /** An endless knob with an LED ring. Not assigned yet (step 5 of docs/PLUGIN_HOST.md): drawn with a dim ring. */
    class HotKnob final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        HotKnob();
        std::function<void()> onClick;
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
    };

    /** The data wheel: a drag or the mouse wheel turns it, one step per notch. */
    class DataWheel final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        std::function<void (int steps)> onTurn;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        /** Turns by `steps` notches (the tests). */
        void turn (int steps);

    private:
        float angle = 0.0f, lastY = 0.0f, lastX = 0.0f, carried = 0.0f;
    };

    /** One of the eight channel strips under the screen. */
    class StripUnit final : public juce::Component
    {
    public:
        StripUnit (PluginHostSurface&, UiContext&, const universal::Theme&, int position);

        /** Binds the strip to channel `index` (0-based; -1: none: the position is past the console's channels). */
        void setChannel (int index);
        int getChannel() const noexcept { return channel; }
        void refresh();

        Key& getSelectKey() noexcept { return selectKey; }
        Key& getBypassKey() noexcept { return bypassKey; }
        Lcd& getLcd() noexcept { return lcd; }
        HotKnob& getKnob (int i) noexcept { return knobs[(size_t) i]; }
        /** The meters' levels now, 0..1 (the tests). */
        float getInputLevel() const noexcept { return inLevel; }
        float getReduction() const noexcept { return grDb; }
        /** Whether the gain-reduction meter has a gain reduction to show (a dynamics block on the channel) or is the dim placeholder. */
        bool hasReduction() const noexcept { return grAvailable; }

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int kWidth = 164, kHeight = 352;

    private:
        Strip* strip() const;

        PluginHostSurface& owner;
        UiContext& ctx;
        const universal::Theme& theme;
        int channel = -1;
        Key selectKey, bypassKey;
        Lcd lcd;
        std::array<HotKnob, 2> knobs;
        float inLevel = 0.0f, grDb = 0.0f;
        bool grAvailable = false, selectedNow = false, bypassedNow = false;
    };
} // namespace ph

/** The PLUGIN HOST's surface (docs/PLUGIN_HOST.md): a large touchscreen across the top, eight channel strips under
    it, and at the right the bank, scene, data wheel and latency / CPU readout. Drawn in a 1920 x 1080 design space
    scaled to fit, like the dLive's. The screen has seven pages; RACK, ROUTING and SCENES are built (the others say so). */
class PluginHostSurface final : public Skin,
                                private juce::Timer
{
public:
    explicit PluginHostSurface (UiContext&);
    ~PluginHostSurface() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshAll() override;
    void selectionChanged() override;
    void showChanged() override;
    bool skinKeyPressed (const juce::KeyPress& key) override { return maximiser.keyPressed (key); }
    void dropPluginEditors (juce::AudioProcessor*) override;

    static constexpr int kDesignWidth = 1920, kDesignHeight = 1080, kStrips = 8;
    static constexpr int kTopBar = 56;
    /** The touchscreen's place in the design space. */
    static constexpr int kScreenX = 40, kScreenY = 64, kScreenW = 1840, kScreenH = 640, kTabHeight = 48;

    enum class Page
    {
        Rack,
        Overview,
        Routing,
        Scenes,
        Control,
        Browser,
        System
    };
    void showPage (Page);
    Page getPage() const noexcept { return page; }
    static const char* nameOf (Page);

    // The banks of eight channels.
    int getNumBanks() const noexcept;
    int getBank() const noexcept { return bank; }
    void setBank (int);
    void stepBank (int delta);

    /** The selected channel (0-based). */
    int getSelectedChannel() const;
    void selectChannel (int index);

    /** The scene cue: PREV and NEXT move it, GO recalls it. */
    int getCue() const noexcept { return cue; }
    void sceneStep (int delta);
    void sceneGo();
    /** Puts the cue on scene `index` (-1: none), as the SCENES page does. */
    void setCue (int index);

    ph::StripUnit& getStrip (int i) { return *strips[(size_t) juce::jlimit (0, kStrips - 1, i)]; }
    ph::RackPage& getRack() noexcept { return *rack; }
    ph::RoutingPage& getRouting() noexcept { return *routing; }
    ph::ScenesPage& getScenes() noexcept { return *scenes; }
    ph::DataWheel& getWheel() noexcept { return wheel; }
    juce::Component& getScreen() noexcept { return screenHost; }
    ph::Key& getBankKey (bool next) noexcept { return next ? bankNext : bankPrev; }
    ph::Key& getSceneKey (int which) noexcept { return which < 0 ? scenePrev : which > 0 ? sceneNext : sceneGoKey; } // -1 PREV, 0 GO, 1 NEXT
    ph::Lcd& getBankLcd() noexcept { return bankLcd; }
    ph::Lcd& getSceneLcd() noexcept { return sceneLcd; }
    ph::Lcd& getReadout() noexcept { return readout; }
    juce::Component* getComingSoon() noexcept;
    /** The page tab keys, in order (RACK ... SYSTEM). */
    universal::ToggleKey& getTab (Page p) { return *tabs[(size_t) p]; }
    universal::ToggleKey& getUniversalKey() noexcept { return universalKey; }

private:
    class ComingSoon;

    void timerCallback() override;
    void bindStrips();
    void updateReadouts();
    Strip* channelStrip (int index) const;

    juce::AffineTransform design;
    UiContext& ctx;
    const universal::Theme& theme;
    universal::ToggleKey universalKey;
    juce::Component screenHost;
    std::array<std::unique_ptr<universal::ToggleKey>, 7> tabs;
    std::unique_ptr<ph::RackPage> rack;
    std::unique_ptr<ph::RoutingPage> routing;
    std::unique_ptr<ph::ScenesPage> scenes;
    std::unique_ptr<ComingSoon> comingSoon;
    std::array<std::unique_ptr<ph::StripUnit>, kStrips> strips;
    ph::Key bankPrev, bankNext, scenePrev, sceneGoKey, sceneNext;
    ph::Lcd bankLcd, sceneLcd, readout;
    ph::DataWheel wheel;
    Page page = Page::Rack;
    int bank = 0, cue = -1, shownScenes = -1;
    bool selecting = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginHostSurface)
private:
    /** Maximise display: F10 or a double-click on the screen; Esc returns. */
    ScreenMaximiser maximiser { *this, { &screenHost } };
};

} // namespace livedaw::ui
