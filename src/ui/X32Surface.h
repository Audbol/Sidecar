#pragma once

#include "ui/Skin.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"
#include "ui/MeterBank.h"
#include "ui/X32ChannelSection.h"
#include "ui/X32Display.h"
#include "ui/X32AssignSection.h"
#include "ui/X32MonitorSection.h"

#include <array>

namespace livedaw::ui
{

namespace x32
{
    constexpr int kScribble = 42;  // the scribble strip's LCD
    constexpr int kKeyHeight = 34; // a key and its name printed under it
    // The X32's lit keys: SELECT mint, SOLO yellow, MUTE red.
    const juce::Colour kSelectLit (0xff62f0a6), kSoloLit (0xffffd23c), kMuteLit (0xffff5a52);

    /** The X32's scribble colours (`Palette::X32`, indexes 1-16): OFF and seven
        colours lit behind dark text, then the same eight inverted (coloured
        text on black). */
    struct ScribbleLook
    {
        juce::Colour background, text, frame;
    };

    inline ScribbleLook scribbleLook (int index)
    {
        const auto c = consoleColour (index);
        if (index <= 1)
            return { juce::Colour (0xff2a2c30), juce::Colour (0xffd8d8d8), juce::Colour (0xff3a3c40) };
        if (index <= 8)
            return { c, c.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff101010) : juce::Colours::white, c.brighter (0.3f) };
        if (index == 9)
            return { juce::Colour (0xff0c0c0c), juce::Colour (0xff9a9a9a), juce::Colour (0xff3a3c40) };
        return { juce::Colour (0xff0c0c0c), c, c };
    }

    using namespace universal;

    /** A channel's meter as on the X32's strips (MAN pp.15-16): one column of
        LEDs in a dark window, top to bottom COMP, CLIP, -6, -12, -18, -30, -60
        and GATE, the marks at their right; COMP and GATE light while the
        compressor or the gate works. A bus's meter has PRE in GATE's place, lit
        while an input sends to the bus pre-fader. A stereo strip shows its
        louder side. */
    class LedMeter final : public juce::Component
    {
    public:
        /** The level of side `channel` (linear). */
        std::function<float (int channel)> level;
        void setChannels (int n) { channels = juce::jlimit (1, 2, n); repaint(); }
        /** The gate's and the compressor's gain reduction now (dB, 0 for none);
            no `gate`: the strip has none. */
        std::function<float()> gate, comp;
        /** A bus: whether any input sends to it pre-fader (the bottom LED is PRE). */
        std::function<bool()> pre;
        /** The bottom LED's mark, printed whatever the strip: GATE on the input
            bank, PRE on the group bank (null: none). */
        const char* bottomMark = "GATE";
        void setBottomMark (const char* mark) { bottomMark = mark; repaint(); }

        void refresh()
        {
            float peak = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                peak = juce::jmax (peak, level ? level (ch) : 0.0f);
            const int lit = litFor (juce::Decibels::gainToDecibels (peak, -144.0f));
            // The LEDs light from 0.5 dB of reduction.
            const bool g = pre ? pre() : gate && std::abs (gate()) >= 0.5f, c = comp && std::abs (comp()) >= 0.5f;
            if (lit != shown || g != gateOn || c != compOn)
            {
                shown = lit;
                gateOn = g;
                compOn = c;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            // The window, the marks and the unlit LEDs don't change: drawn once
            // into an image (again for a new size, or PRE / GATE / none at the
            // bottom); a meter change draws only the lit LEDs over it.
            const int bottom = bottomMark == nullptr ? 0 : isPre() ? 2 : 1;
            const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
            if (! face.isValid() || faceKind != bottom || faceScale != scale || face.getWidth() != juce::roundToInt ((float) getWidth() * scale)
                || face.getHeight() != juce::roundToInt ((float) getHeight() * scale))
            {
                face = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * scale)),
                                    juce::jmax (1, juce::roundToInt ((float) getHeight() * scale)), true);
                juce::Graphics fg (face);
                fg.addTransform (juce::AffineTransform::scale (scale));
                paintLeds (fg, false);
                faceKind = bottom;
                faceScale = scale;
            }
            g.drawImage (face, getLocalBounds().toFloat());
            paintLeds (g, true);
        }

    private:
        /** The meter's face (`lit` false: the window, marks and unlit LEDs), or its lit LEDs. */
        void paintLeds (juce::Graphics& g, bool lit) const
        {
            static const char* const marks[] = { "COMP", "CLIP", "-6", "-12", "-18", "-30", "-60", "GATE" };
            auto area = getLocalBounds().toFloat();
            // The window at the left of centre, the marks to its right.
            auto window = juce::Rectangle<float> (16.0f, area.getHeight()).withX (area.getCentreX() - 18.0f);
            if (! lit)
            {
                g.setColour (juce::Colour (0xff0b0c0c));
                g.fillRoundedRectangle (window, 4.0f);
                g.setColour (juce::Colours::black.withAlpha (0.5f));
                g.drawRoundedRectangle (window.reduced (0.5f), 4.0f, 1.0f);
                g.setFont (type::font (8.5f, true));
            }
            const float rowH = (window.getHeight() - 6.0f) / (float) kLeds;
            for (int k = 0; k < kLeds; ++k)
            {
                if (k == kLeds - 1 && bottomMark == nullptr)
                    continue;
                const float y = window.getY() + 3.0f + rowH * (float) k;
                const auto colour = k == 0 ? juce::Colour (0xffff9a3c) : k == 1 ? juce::Colour (0xffff3b30)
                                    : k == kLeds - 1 && isPre() ? juce::Colour (0xffffd02a) : juce::Colour (0xff59f08a);
                const auto led = juce::Rectangle<float> (9.0f, juce::jmin (4.0f, rowH - 2.0f)).withCentre ({ window.getCentreX(), y + rowH * 0.5f });
                if (! lit)
                {
                    g.setColour (colour.withAlpha (0.12f));
                    g.fillRoundedRectangle (led, 2.0f);
                    g.setColour (juce::Colour (0xffe6e9e6));
                    g.drawText (k == kLeds - 1 ? juce::String (bottomMark) : juce::String (marks[k]),
                                juce::Rectangle<float> (window.getRight() + 3.0f, y, area.getRight() - window.getRight() - 3.0f, rowH),
                                juce::Justification::centredLeft);
                    continue;
                }
                const bool on = k == 0 ? compOn : k == kLeds - 1 ? gateOn : (kLeds - 2 - k) < shown;
                if (! on)
                    continue;
                g.setColour (colour.withAlpha (0.3f));
                g.fillRoundedRectangle (led.expanded (2.0f), 3.0f);
                g.setColour (colour.brighter (0.4f));
                g.fillRoundedRectangle (led, 2.0f);
            }
        }

        bool isPre() const noexcept { return bottomMark != nullptr && juce::String (bottomMark) == "PRE"; }

        juce::Image face;
        int faceKind = -1;
        float faceScale = 0.0f;

        static constexpr int kLeds = 8;
        /** How many level LEDs light from the bottom (-60) for a level. */
        static int litFor (float db)
        {
            static constexpr float thresholds[] = { -60.0f, -30.0f, -18.0f, -12.0f, -6.0f, 0.0f };
            int n = 0;
            for (float th : thresholds)
                n += db >= th ? 1 : 0;
            return n;
        }

        int channels = 1, shown = 0;
        bool gateOn = false, compOn = false;
    };

    /** One fader of the X32 surface, top to bottom as on the console: SELECT,
        the meter, SOLO, the scribble strip, MUTE, and the fader; on a strip, or
        on a DCA (its meter unlit, no SELECT: an X32's DCAs can't be selected), or empty. */
    class FaderStrip final : public juce::Component
    {
    public:
        FaderStrip (UiContext& c, const Theme& t)
            : ctx (c), theme (t), fader (t), solo ("SOLO", kSoloLit, t), mute ("MUTE", kMuteLit, t),
              select ("SELECT", kSelectLit, t)
        {
            solo.onClick = [this]
            {
                if (strip != nullptr)
                    strip->params.solo = ! strip->params.solo.load();
                else if (dca >= 0)
                    ctx.engine.getDca (dca).solo = ! ctx.engine.getDca (dca).solo.load();
            };
            solo.isOn = [this] { return strip != nullptr ? strip->params.solo.load() : dca >= 0 && ctx.engine.getDca (dca).solo.load(); };
            mute.onClick = [this]
            {
                if (strip != nullptr)
                    strip->params.mute = ! strip->params.mute.load();
                else if (dca >= 0)
                    ctx.engine.getDca (dca).mute = ! ctx.engine.getDca (dca).mute.load();
            };
            mute.isOn = [this] { return strip != nullptr ? strip->params.mute.load() : dca >= 0 && ctx.engine.getDca (dca).mute.load(); };
            select.onClick = [this]
            {
                if (strip != nullptr && ctx.selectStrip)
                    ctx.selectStrip (strip);
            };
            select.isOn = [this] { return strip != nullptr && ctx.getSelectedStrip && ctx.getSelectedStrip() == strip; };
            meter.level = [this] (int ch) { return strip != nullptr ? ctx.meters.get (strip, MeterPoint::Output, ch) : 0.0f; };
            for (auto* child : std::initializer_list<juce::Component*> { &fader, &meter, &solo, &mute, &select })
                addAndMakeVisible (child);
            clear();
        }

        void setStrip (Strip* s)
        {
            strip = s;
            dca = -1;
            sendTo = {};
            fader.bind (s != nullptr ? &s->params.faderDb : nullptr);
            meter.setChannels (s != nullptr && s->params.stereo.load() ? 2 : 1);
            meter.gate = s != nullptr && isInputKind (s->getKind()) ? std::function<float()> ([s] { return s->meters.gateReductionDb.load(); }) : nullptr;
            meter.comp = [this] { return strip != nullptr ? strip->meters.dynReductionDb.load() : 0.0f; };
            meter.pre = s != nullptr && s->isBus() ? std::function<bool()> ([this, s] { return sendsPre (*s); }) : nullptr;
            meter.setVisible (meterShown); // its legend is printed on the panel
            select.setEnabled (s != nullptr);
            solo.setEnabled (s != nullptr);
            mute.setEnabled (s != nullptr);
            setTitle (s != nullptr ? s->getLabel() : juce::String());
            repaint();
        }

        void setDca (int index)
        {
            strip = nullptr;
            dca = index;
            sendTo = {};
            fader.bind (&ctx.engine.getDca (index).faderDb);
            // A DCA has no meter of its own: the meter's legend, unlit.
            meter.gate = nullptr;
            meter.pre = nullptr;
            meter.setVisible (meterShown);
            select.setEnabled (false);
            solo.setEnabled (true);
            mute.setEnabled (true);
            setTitle ("DCA " + juce::String (index + 1));
            repaint();
        }

        void clear() { setStrip (nullptr); }

        /** Sends on fader: the fader moves a send of the strip (`level`, null for
            the strip's own fader again), the scribble strip marks it with `to`
            ("BUS 3"). SOLO, MUTE and SELECT stay the strip's. */
        void setSend (std::atomic<float>* level, const juce::String& to)
        {
            sendTo = level != nullptr ? to : juce::String();
            fader.bind (level != nullptr ? level : strip != nullptr ? &strip->params.faderDb : dca >= 0 ? &ctx.engine.getDca (dca).faderDb : nullptr);
            repaint();
        }
        /** What the fader sends to ("BUS 3"), or empty for its own fader. */
        const juce::String& getSendTo() const noexcept { return sendTo; }

        Strip* getStrip() const noexcept { return strip; }
        int getDca() const noexcept { return dca; }
        bool isEmpty() const noexcept { return strip == nullptr && dca < 0; }

        /** What the scribble strip shows: its label ("CH 1") and name. */
        juce::String getLabel() const
        {
            // The X32's mains are the LR and the M/C.
            if (strip != nullptr && strip->getKind() == StripKind::Main)
                return strip->getNumber() == 1 ? "LR" : "M/C";
            if (strip != nullptr)
                return strip->getLabel();
            return dca >= 0 ? "DCA " + juce::String (dca + 1) : juce::String();
        }
        juce::String getName() const
        {
            if (strip != nullptr)
                return strip->getName();
            return dca >= 0 ? ctx.engine.getDcaName (dca) : juce::String();
        }
        int getColourIndex() const
        {
            if (strip != nullptr)
                return strip->getColourIndex();
            return dca >= 0 ? (int) ctx.engine.getDca (dca).colour.load() : 1;
        }

        void refresh()
        {
            fader.refresh();
            if (meter.isVisible())
                meter.refresh();
            for (auto* key : { &solo, &mute, &select })
                key->refresh();
            // The scribble strip follows renames and recolours.
            const auto now = getLabel() + "|" + getName() + "|" + juce::String (getColourIndex()) + "|" + sendTo;
            if (now != shown)
            {
                shown = now;
                repaint (scribbleArea());
            }
        }

        void paint (juce::Graphics& g) override
        {
            // The fader's bay: a darker recess down the strip.
            const auto bay = fader.getBounds().toFloat().expanded (1.0f, 6.0f);
            g.setColour (juce::Colour (0xff1d2021));
            g.fillRoundedRectangle (bay, 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.drawRoundedRectangle (bay.reduced (0.5f), 4.0f, 1.0f);
            auto area = scribbleArea().toFloat();
            if (isEmpty())
            {
                g.setColour (juce::Colour (0xff15171a));
                g.fillRoundedRectangle (area, 3.0f);
                return;
            }
            const auto look = scribbleLook (getColourIndex());
            // The LCD in its black bezel, lit from behind in the strip's colour.
            g.setColour (juce::Colour (0xff070808));
            g.fillRoundedRectangle (area.expanded (2.0f), 3.0f);
            g.setGradientFill (juce::ColourGradient (look.background.brighter (0.18f), area.getX(), area.getY(), look.background.darker (0.3f), area.getX(),
                                                     area.getBottom(), false));
            g.fillRect (area);
            g.setColour (juce::Colours::white.withAlpha (0.1f));
            g.drawHorizontalLine ((int) area.getY() + 1, area.getX() + 1.0f, area.getRight() - 1.0f);
            if (sendTo.isNotEmpty())
            {
                // Sends on fader: the send's destination along the bottom, in amber.
                auto band = area.removeFromBottom (15.0f);
                g.setColour (juce::Colour (0xfff0a020));
                g.fillRoundedRectangle (band, 3.0f);
                g.setColour (juce::Colour (0xff101010));
                g.setFont (type::font (type::kCaption, true));
                g.drawText ("> " + sendTo, band, juce::Justification::centred);
            }
            auto text = area.reduced (3.0f, 2.0f);
            g.setColour (look.text);
            const auto name = getName(), label = getLabel();
            if (name.isEmpty() || name == label)
            {
                // No name of its own: the label, large.
                g.setFont (type::font (13.0f, true));
                g.drawFittedText (label, text.toNearestInt(), juce::Justification::centred, 1, 0.7f);
                return;
            }
            g.setFont (type::font (10.0f, true));
            g.drawText (label, text.removeFromTop (13.0f), juce::Justification::centredLeft);
            g.setFont (type::font (12.0f, true));
            g.drawFittedText (name, text.toNearestInt(), juce::Justification::centredLeft, 1, 0.65f);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            // As on the panel, the keys blank with their names under them.
            select.setBounds (area.removeFromTop (kKeyHeight).reduced (8, 0));
            area.removeFromTop (4);
            meterArea = area.removeFromTop (kMeterHeight);
            meter.setBounds (meterArea.reduced (2, 0));
            area.removeFromTop (4);
            solo.setBounds (area.removeFromTop (kKeyHeight).reduced (8, 0));
            area.removeFromTop (6);
            scribble = area.removeFromTop (kScribble).reduced (5, 0);
            area.removeFromTop (6);
            mute.setBounds (area.removeFromTop (kKeyHeight).reduced (8, 0));
            area.removeFromTop (12);
            fader.setBounds (area.reduced (2, 6));
        }

    private:
        static constexpr int kMeterHeight = 70;
        juce::Rectangle<int> scribbleArea() const { return scribble; }

    public:
        /** Where the meter is (the main strip has CLEAR SOLO there instead), and showing it. */
        juce::Rectangle<int> getMeterArea() const noexcept { return meterArea; }
        void setMeterShown (bool on)
        {
            meterShown = on;
            meter.setVisible (on);
        }
        /** The group bank's: PRE at the bottom of the meter's legend, not GATE. */
        void setGroupBank() { meter.setBottomMark ("PRE"); }
        /** Where the MUTE key's row starts (the layer keys line up with it). */
        int getMuteTop() const noexcept { return mute.getY(); }

    private:
        bool meterShown = true;
        juce::Rectangle<int> meterArea;

        UiContext& ctx;
        const Theme& theme;
        Strip* strip = nullptr;
        int dca = -1;
        FaderControl fader;
        LedMeter meter;
        ToggleKey solo, mute, select;
        juce::Rectangle<int> scribble;

        /** Whether an input channel sends to `bus` pre-fader (an enabled PRE send). */
        bool sendsPre (const Strip& bus) const
        {
            const int index = bus.getNumber() - 1;
            for (auto* list : { &ctx.engine.getChannels(), &ctx.engine.getAux() })
                for (auto& each : *list)
                    if (auto* send = each->sends.find (index); send != nullptr && send->enabled.load() && send->mode.load() == 0)
                        return true;
            return false;
        }
        juce::String shown, sendTo;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaderStrip)
    };
} // namespace x32

/** The X32 surface (docs/UNIVERSAL_MIXER.md, Phase 4 step 8): an X32 console's
    skin, laid out as its top panel and drawn in Sidecar's own style (no
    Behringer artwork):

    - the input fader bank: 16 faders in four layers (CH 1-16, CH 17-32,
      AUX IN / FX RETURNS, BUS MASTERS);
    - the group fader bank: 8 faders in four layers (DCA, BUS 1-8, BUS 9-16,
      MATRIX / M/C);
    - the main LR fader, always there;
    - on every fader its scribble strip (number, name, the X32's colours, the
      inverted ones as coloured text on black), a meter, SOLO, MUTE and SELECT;
    - the channel strip section for the selected strip (step 8c);
    - monitoring and talkback (step 12b);
    - the assign section, right of the main fader (step 12c);
    - the X32's display, its screen keys and its six push-encoders, the screen
      taking touch (step 8d).

    Shown while the console is an X32 (MainComponent); UNIVERSAL MIXER switches
    to the Universal Mixer. */
class X32Surface final : public Skin,
                         private juce::Timer
{
public:
    explicit X32Surface (UiContext&);
    ~X32Surface() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshAll() override;
    void selectionChanged() override;
    void showChanged() override { display.refreshStrip(); }

    /** The model: the full-size X32 (16 input faders) or the X32 COMPACT (8,
        in twice the layers), kept in the app settings. */
    enum class Model
    {
        Full,
        Compact
    };
    static constexpr const char* modelKey = "ui.x32Model"; // "X32" (default) or "COMPACT"
    void setModel (Model);
    Model getModel() const noexcept { return model; }
    int getNumInputFaders() const noexcept { return model == Model::Compact ? 8 : 16; }

    /** The input bank's layers, the model's: CH 1-16, CH 17-32, AUX / FX RTN,
        BUS MASTER on an X32; CH 1-8 .. CH 25-32, AUX IN / USB, FX RTN, BUS 1-8,
        BUS 9-16 on an X32 COMPACT. */
    juce::StringArray getInputLayerNames() const;
    void setInputLayer (int);
    int getInputLayer() const noexcept { return inputLayer; }

    enum class GroupLayer
    {
        Dca,
        Bus1to8,
        Bus9to16,
        MatrixMain
    };
    void setGroupLayer (GroupLayer);
    GroupLayer getGroupLayer() const noexcept { return groupLayer; }

    /** Fader `i` of the input bank (0-15; 0-7 on an X32 COMPACT) and of the group bank (0-7); the main LR. */
    x32::FaderStrip& getInputStrip (int i);
    x32::FaderStrip& getGroupStrip (int i);
    x32::FaderStrip& getMainStrip();
    x32::ChannelSection& getChannelSection() noexcept { return channel; }
    x32::MonitorSection& getMonitorSection() noexcept { return monitor; }
    x32::AssignSection& getAssignSection() noexcept { return assign; }

    /** SENDS ON FADER (the X32's key): with a bus selected, the input faders
        move each strip's send to it; with a channel or aux selected, the bus
        layers of the group faders move its sends to buses 1-16 (and a bus's to
        the matrices on the MTX layer). */
    void setSendsOnFader (bool);
    bool isSendsOnFader() const noexcept { return sendsOnFader; }
    x32::Display& getDisplay() noexcept { return display; }

private:
    void timerCallback() override;
    void assignBanks();
    void buildInputLayerKeys();
    juce::Rectangle<int> channelSectionArea() const;

    UiContext& ctx;
    const universal::Theme& theme;
    universal::ToggleKey universalKey, sendsOnFaderKey, clearSolo;
    /** REMOTE (DAW remote), at the top of the input layer keys: Sidecar has none, so it's dark. */
    universal::ToggleKey remoteKey;
    std::vector<std::unique_ptr<universal::ToggleKey>> inputLayerKeys, groupLayerKeys;
    /** SCENES (PREV, NEXT, UNDO, GO, VIEW) and MUTE GROUPS 1-6, right of the main fader. */
    std::vector<std::unique_ptr<universal::ToggleKey>> sceneKeys, muteGroupKeys;
    /** Clears every solo (CLEAR SOLO, on the main strip). */
    void clearSolos();
    std::array<std::unique_ptr<x32::FaderStrip>, 16> inputs;
    std::array<std::unique_ptr<x32::FaderStrip>, 8> groups;
    std::unique_ptr<x32::FaderStrip> main;
    x32::ChannelSection channel;
    x32::MonitorSection monitor;
    x32::AssignSection assign;
    x32::Display display;
    Model model = Model::Full;
    int inputLayer = 0;
    GroupLayer groupLayer = GroupLayer::Dca;
    bool sendsOnFader = false;
    void applySendsOnFader();
    Strip* sofShown = nullptr; // the selected strip the sends were laid out for


    juce::Rectangle<int> displayArea, channelArea, fadersArea, scenesArea, muteGroupsArea, palmRest;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (X32Surface)
};

} // namespace livedaw::ui
