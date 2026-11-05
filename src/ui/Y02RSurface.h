#pragma once

#include "ui/Skin.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace livedaw
{
class Yamaha02R;
}

namespace livedaw::ui
{

namespace y02r
{
    using namespace universal;

    /** The 02R's edit buffer (livedaw_02r::params): strip `s` item `item`, and
        member `m`'s send to AUX `aux` (0-7). */
    int address (int strip, int item);
    int sendAddress (int member, int aux);
    /** The edit buffer's strips: 0-15 MIC, 16-19 LINE, 20-35 TAPE, 36-37 EFF,
        38-45 BUS, 46-53 AUX, 54 STEREO. */
    constexpr int kTape = 20, kEff = 36, kBus = 38, kAux = 46, kStereo = 54, kStrips = 55;
    juce::String stripName (int strip);

    /** The display's functions (MAN pp.18-19): CONFIGURATION, MIXING, AUX. */
    enum class Function
    {
        Scene,
        Utility,
        Midi,
        Group,
        Pair,
        Att,
        Delay,
        Pan,
        Routing,
        Meter,
        View,
        Eq,
        Dynamics,
        Aux1,
        Aux2,
        Aux3,
        Aux4,
        Aux5,
        Aux6,
        Aux7,
        Aux8
    };

    /** A 100 mm motor fader of 128 steps on an edit buffer address (a strip's
        level, or an input's send in an AUX function), its panel's scale beside it
        (the channels' 10 ... 50, STEREO's 0 ... 70). */
    class Fader final : public juce::Component
    {
    public:
        Fader (UiContext&, const Theme&);

        /** The address it moves (-1: none) and whether it is STEREO's (its own law). */
        void bind (int address, bool stereo);
        int getAddress() const noexcept { return addr; }
        void refresh();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        juce::Rectangle<float> track() const noexcept;
        void moveTo (int step);

        UiContext& ctx;
        const Theme& theme;
        int addr = -1, step = 0, dragStart = 0;
        bool stereoLaw = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Fader)
    };

    /** A rotary encoder with its LED ring (the tape returns', the SELECTED
        CHANNEL section's): a value from `get` within `lo`..`hi`, moved by drag or
        wheel through `set`, its text below. */
    class Encoder final : public juce::Component
    {
    public:
        Encoder (const juce::String& label, const Theme&);

        std::function<int()> get;
        std::function<void (int)> set;
        std::function<juce::String (int)> text;
        int lo = 0, hi = 127;
        /** A pan's ring lights from the centre. */
        bool centred = false;
        void setLabel (const juce::String& l) { label = l; repaint(); }
        void refresh();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        void moveTo (int v);
        const Theme& theme;
        juce::String label;
        int value = 0, dragStart = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Encoder)
    };

    /** The 02R's 320x240 display (MAN pp.20-21), in Sidecar's style: the
        scene, the function and the selected channel along the top, and the
        function's page: its values as fields, one with the cursor; the cursor
        keys move it, the wheel changes its value, ENTER recalls (a library
        program) or switches. A tap puts the cursor on a field, a drag turns
        the wheel. */
    class Display final : public juce::Component
    {
    public:
        Display (UiContext&, const Theme&);

        void setFunction (Function);
        Function getFunction() const noexcept { return function; }
        /** The strip the page is for (the edit buffer's numbering). */
        void setStrip (int);
        int getStrip() const noexcept { return strip; }
        /** The EQ band the SELECTED CHANNEL section works (0 LOW ... 3 HIGH). */
        int band = 1;

        void cursor (int delta);
        void turn (int delta);
        void enter();
        void refresh();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        struct Field
        {
            juce::String label;
            std::function<int()> get;
            std::function<void (int)> set;
            std::function<juce::String (int)> text;
            int lo = 0, hi = 127;
            /** ENTER does it (a library program recalled); the wheel only chooses. */
            bool onEnter = false;
            int pending = -1;
        };
        const std::vector<Field>& getFields() const noexcept { return fields; }
        int getCursor() const noexcept { return focus; }
        std::function<juce::String()> memoryText;

    private:
        void build();
        juce::Rectangle<float> fieldBox (int i) const;
        juce::String title() const;
        void paintMeters (juce::Graphics&, juce::Rectangle<float>) const;
        void paintEq (juce::Graphics&, juce::Rectangle<float>) const;

        UiContext& ctx;
        const Theme& theme;
        Yamaha02R& y;
        Function function = Function::View;
        int strip = 0, focus = 0;
        std::vector<Field> fields;
        float dragY = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Display)
    };
} // namespace y02r

/** The Yamaha 02R surface (docs/UNIVERSAL_MIXER.md, Phase 6 step 7): an 02R
    console's skin, laid out as its top panel (MAN pp.2-10) and drawn in
    Sidecar's own style (no Yamaha artwork):

    - 21 faders (MIC/LINE 1-16, LINE 17/18-23/24, STEREO) with SEL and ON;
      FLIP puts TAPE 1-16 on faders 1-16 (and MIC on the encoders); in an AUX
      function the faders are the inputs' sends to it and STEREO's the aux master;
    - the encoders of TAPE 1-16 and EFF1-2, with SEL and ON;
    - the display and its function keys (CONFIGURATION: SCENE MEMORY, UTILITY,
      MIDI, GROUP, PAIR; MIXING: phase / ATT, DELAY, PAN, ROUTING, METER, VIEW,
      EQ, DYNAMICS; AUX 1-8), the cursor keys, ENTER and the wheel;
    - the SELECTED CHANNEL section: ROUTING 1-8, ST, DIRECT; AUX 1-8 with its
      SEND LEVEL and ON; PAN; EQ (the bands, Q, F, G, EQ ON);
    - SCENE MEMORY: its number, STORE, RECALL, INC, DEC (the show's scenes
      1-96, 0 the initial data); the stereo meter.

    Shown while the console is an 02R (MainComponent); UNIVERSAL MIXER switches
    to the Universal Mixer. */
class Y02RSurface final : public Skin,
                          private juce::Timer
{
public:
    explicit Y02RSurface (UiContext&);
    ~Y02RSurface() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshAll() override;
    void selectionChanged() override;
    void showChanged() override { display.refresh(); }

    static constexpr int kNumFaders = 21, kNumEncoders = 18;

    /** For tests and the screenshots. */
    y02r::Display& getDisplay() noexcept { return display; }
    y02r::Fader& getFader (int i) { return *faders[(size_t) juce::jlimit (0, kNumFaders - 1, i)]; }
    void pressFunction (y02r::Function);
    void pressSel (int strip);
    void setFlip (bool);
    bool isFlipped() const noexcept { return flip; }
    int getMemory() const noexcept { return memory; }
    void setMemory (int m) { memory = juce::jlimit (0, 96, m); repaint(); }
    void store();
    void recall();

private:
    void timerCallback() override;
    void bind();
    /** The strip fader `i` (encoder `i`) works now. */
    int faderStrip (int i) const;
    int encoderStrip (int i) const;
    /** The aux an AUX function shows (0-7), or -1. */
    int auxShown() const;
    Strip* engineStrip (int strip) const;
    juce::Rectangle<int> meterArea() const;
    juce::Rectangle<int> memoryArea() const;

    UiContext& ctx;
    const universal::Theme& theme;
    Yamaha02R& y;
    universal::ToggleKey universalKey, flipKey;
    y02r::Display display;
    std::array<std::unique_ptr<y02r::Fader>, kNumFaders> faders;
    std::array<std::unique_ptr<universal::ToggleKey>, kNumFaders> selKeys, onKeys;
    std::array<std::unique_ptr<y02r::Encoder>, kNumEncoders> encoders;
    std::array<std::unique_ptr<universal::ToggleKey>, kNumEncoders> encSelKeys, encOnKeys;
    std::vector<std::pair<y02r::Function, std::unique_ptr<universal::ToggleKey>>> functionKeys;
    std::vector<std::unique_ptr<universal::ToggleKey>> panelKeys;   // STORE, RECALL, INC, DEC, cursors, ENTER
    std::vector<std::unique_ptr<universal::ToggleKey>> routingKeys; // 1-8, ST, DIRECT
    std::vector<std::unique_ptr<universal::ToggleKey>> auxKeys;     // AUX 1-8, then ON
    std::vector<std::unique_ptr<universal::ToggleKey>> eqKeys;      // LOW, L-MID, H-MID, HIGH, EQ ON
    std::unique_ptr<y02r::Encoder> sendLevel, pan, eqQ, eqF, eqG;
    std::unique_ptr<juce::Slider> wheel;
    /** The analog row (drawn), the LED readouts and the stereo meter's places, set by resized(). */
    juce::Rectangle<int> analogArea, readoutArea, meterRect;
    int memory = 1, selAux = 0;
    bool flip = false;
    double wheelLast = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Y02RSurface)
};

} // namespace livedaw::ui
