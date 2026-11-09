#pragma once

#include "ui/Knobs.h"
#include "ui/Skin.h"
#include "ui/Sq5Display.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <array>
#include <memory>
#include <vector>

namespace livedaw::ui
{

class Sq5Surface;

namespace sq5
{
    /** A fader strip of the surface: its LCD (backlit in the channel's colour), Mute,
        Sel and PAFL keys, signal and peak LEDs, its fader. The Master strip has no LCD. */
    class FaderStrip final : public juce::Component
    {
    public:
        FaderStrip (Sq5Surface&, UiContext&, const universal::Theme&, bool isMaster);

        void setSlot (const Slot&);
        /** Which of the 16 strips it is (the GEQ on the faders takes a band each). */
        void setIndex (int i) { index = i; }
        const Slot& getSlot() const noexcept { return slot; }
        /** Takes the selected mix and the keys' modes: what the fader and the Sel key do now. */
        void rebind();
        void refresh();

        universal::FaderControl& getFader() noexcept { return fader; }
        universal::ToggleKey& getMute() noexcept { return mute; }
        universal::ToggleKey& getSel() noexcept { return sel; }
        universal::ToggleKey& getPafl() noexcept { return pafl; }
        juce::String getLcdText() const;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void pressSel();
        bool muteState (bool byGroup) const;

        Sq5Surface& surface;
        UiContext& ctx;
        Slot slot;
        const bool master;
        int index = 0;
        juce::String flipLabel;
        universal::FaderControl fader;
        universal::ToggleKey mute, sel, pafl;
        juce::String shownName;
        juce::Colour shownColour;
        bool shownSignal = false, shownPeak = false;
        juce::String shownValue;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaderStrip)
    };
} // namespace sq5

/** The Allen & Heath SQ-5's surface, drawn from the Technical Datasheet's dimensioned
    panel (p.2) and the Reference Guide's pictures: 16+1 motor faders on 6 layers, the
    processing and EQ rotaries around a 7" touchscreen, 8 screen keys, 8 SoftKeys, the
    Mix and FX keys. No maker's artwork or logo is used. */
class Sq5Surface final : public Skin,
                         private juce::Timer
{
public:
    explicit Sq5Surface (UiContext&);
    ~Sq5Surface() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshAll() override;
    void selectionChanged() override;
    void showChanged() override { display.repaint(); }

    static constexpr int kStrips = 16, kLayers = 6;
    static constexpr int kDesignWidth = 1920, kDesignHeight = 1080;

    /** Layers A-F (0-5). */
    void setLayer (int);
    int getLayer() const noexcept { return layer; }
    /** 0 LR, 1-12 Mix n, 13-16 FX n. */
    void setMix (int);
    int getMix() const noexcept { return mix; }
    bool isAssignMode() const noexcept { return assignMode; }
    bool isPreFadeMode() const noexcept { return preFadeMode; }
    bool isChToAll() const noexcept { return chToAll; }
    bool isViewAlt() const noexcept { return viewAlt; }
    /** Fader Flip (p.38): 0 off, 1 the GEQ's first 16 bands on the faders, 2 the next 12. */
    int getFaderFlip() const noexcept { return faderFlip; }
    void pressFaderFlip();
    /** The GEQ of the selected mix (nullptr: none). */
    ld::EqBlockParams* selectedGeq() const;
    /** Hold Copy, Paste or Reset and press Sel (p.13): a latch here, as a mouse has one
        button. 0 none, 1 Copy, 2 Paste, 3 Reset. */
    int getClipboardMode() const noexcept { return clipboardMode; }
    void setClipboardMode (int);
    /** The SoftKey's function (Setup's Soft Controls). */
    void pressSoftKey (int key);
    void setAssignMode (bool);
    void setPreFadeMode (bool);
    void setChToAll (bool);
    /** The EQ band the Width, Freq and Gain rotaries work (0 LF, 1 LM, 2 HM, 3 HF). */
    void setEqBand (int);
    int getEqBand() const noexcept { return eqBand; }

    sq5::Display& getDisplay() noexcept { return display; }
    sq5::FaderStrip& getStrip (int i) { return *strips[(size_t) juce::jlimit (0, kStrips - 1, i)]; }
    sq5::FaderStrip& getMaster() noexcept { return *master; }
    /** A panel key by its printed name (nullptr: none). */
    universal::ToggleKey* findKey (const juce::String& name) const;
    DisplayKnob& getKnob (const juce::String& name);

    /** The strips' layers, as they are (Setup's Strip Assign changes them). */
    const std::vector<sq5::Slot>& getLayerSlots (int l) const { return layers[(size_t) juce::jlimit (0, kLayers - 1, l)]; }

private:
    friend class sq5::FaderStrip;

    void timerCallback() override;
    void bindKnobs();
    void bindStrips();
    /** A panel key: `colour` lit, `face` idle (their backlights never go out). */
    universal::ToggleKey& panelKey (const juce::String& name, juce::Colour, juce::Rectangle<int>, bool labelAbove = true, juce::Colour face = {});
    void lightJewels();
    DisplayKnob& panelKnob (const juce::String& name, juce::Rectangle<int>);
    sq5::Slot selectedSlot() const;
    /** With Copy, Paste or Reset latched: does it to `strip`; false: not in that mode. */
    bool applyClipboard (Strip& strip);
    void saveLayers();
    void loadLayers();

    struct Silk
    {
        juce::Rectangle<float> area;
        juce::String text;
        float size;
        juce::Justification justification;
    };
    /** The rounded outline around the screen and its rotaries (the datasheet's p.2). */
    juce::Path podOutline() const;

    juce::AffineTransform design;

    UiContext& ctx;
    const universal::Theme& theme;
    universal::ToggleKey universalKey;
    sq5::Display display;
    std::array<std::unique_ptr<sq5::FaderStrip>, kStrips> strips;
    std::unique_ptr<sq5::FaderStrip> master;
    std::vector<std::unique_ptr<universal::ToggleKey>> keys;
    std::vector<std::pair<juce::String, std::unique_ptr<DisplayKnob>>> knobs;
    DisplayKnob rotary;
    std::vector<Silk> silks;
    std::vector<std::vector<sq5::Slot>> layers;
    int layer = 0, mix = 0, eqBand = 1;
    bool assignMode = false, preFadeMode = false, chToAll = false, viewAlt = false;
    int faderFlip = 0, clipboardMode = 0;
    sq5::Slot clipboard;
    Strip* boundStrip = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sq5Surface)
};

} // namespace livedaw::ui
