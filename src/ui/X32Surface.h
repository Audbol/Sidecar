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
    constexpr int kScribble = 66;
    constexpr int kKeyHeight = 30;

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
            static const char* const marks[] = { "COMP", "CLIP", "-6", "-12", "-18", "-30", "-60", "GATE" };
            const auto mark = [this] (int k) { return k == kLeds - 1 && pre ? juce::String ("PRE") : juce::String (marks[k]); };
            auto area = getLocalBounds().toFloat();
            // The window at the left of centre, the marks to its right.
            auto window = juce::Rectangle<float> (16.0f, area.getHeight()).withX (area.getCentreX() - 18.0f);
            g.setColour (juce::Colour (0xff0b0c0c));
            g.fillRoundedRectangle (window, 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (window.reduced (0.5f), 4.0f, 1.0f);
            const float rowH = (window.getHeight() - 6.0f) / (float) kLeds;
            g.setFont (type::font (8.5f, true));
            for (int k = 0; k < kLeds; ++k)
            {
                if (k == kLeds - 1 && ! gate && ! pre)
                    continue;
                const float y = window.getY() + 3.0f + rowH * (float) k;
                const auto colour = k == 0 ? juce::Colour (0xffff9a3c) : k == 1 ? juce::Colour (0xffff3b30)
                                    : k == kLeds - 1 && pre ? juce::Colour (0xffffd02a) : juce::Colour (0xff59f08a);
                const bool on = k == 0 ? compOn : k == kLeds - 1 ? gateOn : (kLeds - 2 - k) < shown;
                const auto led = juce::Rectangle<float> (9.0f, juce::jmin (4.0f, rowH - 2.0f)).withCentre ({ window.getCentreX(), y + rowH * 0.5f });
                if (on)
                {
                    g.setColour (colour.withAlpha (0.3f));
                    g.fillRoundedRectangle (led.expanded (2.0f), 3.0f);
                }
                g.setColour (on ? colour.brighter (0.4f) : colour.withAlpha (0.12f));
                g.fillRoundedRectangle (led, 2.0f);
                g.setColour (juce::Colour (0xffe6e9e6));
                g.drawText (mark (k), juce::Rectangle<float> (window.getRight() + 3.0f, y, area.getRight() - window.getRight() - 3.0f, rowH),
                            juce::Justification::centredLeft);
            }
        }

    private:
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
        on a DCA (no meter, no SELECT: an X32's DCAs can't be selected), or empty. */
    class FaderStrip final : public juce::Component
    {
    public:
        FaderStrip (UiContext& c, const Theme& t)
            : ctx (c), theme (t), fader (t), solo ("SOLO", t.solo, t), mute ("MUTE", t.mute, t),
              select ({}, t.accent, t)
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
            meter.setVisible (s != nullptr);
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
            meter.setVisible (false);
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
            if (! selectLabel.isEmpty() && strip != nullptr)
            {
                g.setColour (juce::Colour (0xffe6e9e6));
                g.setFont (type::font (10.0f, true));
                g.drawText ("SELECT", selectLabel, juce::Justification::centred);
            }
            auto area = scribbleArea().toFloat();
            if (isEmpty())
            {
                g.setColour (juce::Colour (0xff15171a));
                g.fillRoundedRectangle (area, 3.0f);
                return;
            }
            const auto look = scribbleLook (getColourIndex());
            g.setColour (look.background);
            g.fillRoundedRectangle (area, 3.0f);
            g.setColour (look.frame);
            g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 1.0f);
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
            auto text = area.reduced (4.0f, 3.0f);
            g.setColour (look.text);
            const auto name = getName(), label = getLabel();
            if (name.isEmpty() || name == label)
            {
                // No name of its own: the label, large.
                g.setFont (type::font (type::kValue, true));
                g.drawFittedText (label, text.toNearestInt(), juce::Justification::centred, 1, 0.7f);
                return;
            }
            g.setFont (type::font (type::kCaption, true));
            g.drawText (label, text.removeFromTop (16.0f), juce::Justification::centred);
            g.setFont (type::font (type::kLabel, true));
            g.drawFittedText (name, text.toNearestInt(), juce::Justification::centred, 2, 0.7f);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            // SELECT: a blank key, its name printed under it.
            select.setBounds (area.removeFromTop (kKeyHeight - 6).reduced (8, 0));
            selectLabel = area.removeFromTop (13);
            meter.setBounds (area.removeFromTop (kMeterHeight).reduced (2, 0));
            area.removeFromTop (3);
            solo.setBounds (area.removeFromTop (kKeyHeight).reduced (2, 0));
            area.removeFromTop (3);
            scribble = area.removeFromTop (kScribble).reduced (2, 0);
            area.removeFromTop (3);
            mute.setBounds (area.removeFromTop (kKeyHeight).reduced (2, 0));
            area.removeFromTop (sizes::kGapSmall);
            fader.setBounds (area.reduced (2, 0));
        }

    private:
        static constexpr int kMeterHeight = 76;
        juce::Rectangle<int> scribbleArea() const { return scribble; }

        UiContext& ctx;
        const Theme& theme;
        Strip* strip = nullptr;
        int dca = -1;
        FaderControl fader;
        LedMeter meter;
        ToggleKey solo, mute, select;
        juce::Rectangle<int> scribble, selectLabel;

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
    universal::ToggleKey universalKey, sendsOnFaderKey;
    std::vector<std::unique_ptr<universal::ToggleKey>> inputLayerKeys, groupLayerKeys;
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


    juce::Rectangle<int> displayArea, channelArea, fadersArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (X32Surface)
};

} // namespace livedaw::ui
