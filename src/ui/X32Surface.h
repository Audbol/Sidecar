#pragma once

#include "ui/Skin.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"
#include "ui/MeterBank.h"
#include "ui/X32ChannelSection.h"
#include "ui/X32Display.h"

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

    /** One fader of the X32 surface: its scribble strip, a meter, SOLO, MUTE and
        SELECT, and the fader; on a strip, or on a DCA (no meter, no SELECT: an
        X32's DCAs can't be selected), or empty. */
    class FaderStrip final : public juce::Component
    {
    public:
        FaderStrip (UiContext& c, const Theme& t)
            : ctx (c), theme (t), fader (t), meter (t), solo ("SOLO", t.solo, t), mute ("MUTE", t.mute, t),
              select ("SELECT", t.accent, t)
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
            area.removeFromTop (kScribble + sizes::kGapSmall);
            for (auto* key : { &select, &solo, &mute })
            {
                key->setBounds (area.removeFromTop (kKeyHeight).reduced (2, 0));
                area.removeFromTop (3);
            }
            area.removeFromTop (sizes::kGapSmall);
            const int meterWidth = 12;
            meter.setBounds (area.removeFromRight (meterWidth));
            fader.setBounds (area.reduced (2, 0));
        }

    private:
        juce::Rectangle<int> scribbleArea() const { return getLocalBounds().removeFromTop (kScribble).reduced (2, 0); }

        UiContext& ctx;
        const Theme& theme;
        Strip* strip = nullptr;
        int dca = -1;
        FaderControl fader;
        MeterControl meter;
        ToggleKey solo, mute, select;
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
