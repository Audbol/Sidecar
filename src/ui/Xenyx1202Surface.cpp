#include "ui/Xenyx1202Surface.h"

#include "engine/FaderLaw.h"
#include "ui/RoutingGroups.h"
#include "ui/universal/PatchGrid.h"
#include "ui/universal/RoutingView.h"

#include <cmath>

namespace livedaw::ui
{
namespace
{
    // Colours read off the owner's photographs of the panel.
    const juce::Colour kBody (0xff2b2c2e), kSilver (0xffb9bab8), kSilverDark (0xff8b8a87), kJackPanel (0xff4c4d50), kBand (0xff1d1e20);
    const juce::Colour kCell (0xffa9abab), kMainCell (0xffc1c3c0), kInk (0xff222427), kEqBand (0xff333436), kCompBand (0xff222325);
    const juce::Colour kBlue (0xff2a7bb0), kNavy (0xff3d4f82), kRed (0xffc9272f), kPan (0xff343536), kWhite (0xffd8d6cc), kBlack (0xff1b1b1c);
    const juce::Colour kPhantom (0xffc31944);

    // Photo coordinates: the panel's own space is the photograph's, moved by this much.
    constexpr int kOy = 335, kTop = 1475; // upright X = photo y - kOy, upright Y = kTop - photo x
    constexpr int kMonoTop = 415, kMonoPitch = 152, kStereoTop = 1023, kStereoPitch = 148;

    int rowTop (int row) { return row < 4 ? kMonoTop + kMonoPitch * row : kStereoTop + kStereoPitch * (row - 4); }
    int rowCy (int row) { return rowTop (row) + (row < 4 ? 70 : 66); }

    /** A gain read off an atomic the strip owns, found again at every call (a strip may go). */
    using FloatRef = std::function<std::atomic<float>*()>;

    KnobBinding makeBinding (const juce::String& label, FloatRef ref, std::function<float (float)> toPos, std::function<float (float)> toValue,
                             std::function<juce::String (float)> text, float defaultValue, bool bipolar = false)
    {
        KnobBinding b;
        b.label = label;
        b.bipolar = bipolar;
        b.get = [ref, toPos] { auto* a = ref(); return a != nullptr ? juce::jlimit (0.0f, 1.0f, toPos (a->load())) : 0.0f; };
        b.set = [ref, toValue] (float p) { if (auto* a = ref()) a->store (toValue (juce::jlimit (0.0f, 1.0f, p))); };
        b.text = [ref, text] { auto* a = ref(); return a != nullptr ? text (a->load()) : juce::String(); };
        b.reset = [ref, defaultValue] { if (auto* a = ref()) a->store (defaultValue); };
        return b;
    }

    KnobBinding linearBinding (const juce::String& label, FloatRef ref, float lo, float hi, const juce::String& suffix, int decimals, float def)
    {
        return makeBinding (label, ref, [lo, hi] (float v) { return (v - lo) / (hi - lo); }, [lo, hi] (float p) { return lo + p * (hi - lo); },
                            [suffix, decimals] (float v) { return juce::String (v, decimals) + suffix; }, def, lo < 0.0f && hi > 0.0f);
    }

    /** An FX knob or a master: -oo at its start, then dB from -60 to `maxDb` (the taper is Sidecar's). */
    KnobBinding levelBinding (const juce::String& label, FloatRef ref, float maxDb, float def)
    {
        constexpr float kFloorPos = 0.02f;
        return makeBinding (label, ref,
                            [maxDb] (float db) { return db <= -100.0f ? 0.0f : kFloorPos + (1.0f - kFloorPos) * (juce::jmax (db, -60.0f) + 60.0f) / (maxDb + 60.0f); },
                            [maxDb] (float p) { return p < kFloorPos ? -144.0f : -60.0f + (p - kFloorPos) / (1.0f - kFloorPos) * (maxDb + 60.0f); },
                            [] (float db) { return db <= -100.0f ? juce::String ("-oo") : juce::String (db, 1) + " dB"; }, def);
    }

    /** A LEVEL knob: the engine's fader law (this console's: -oo to +15 dB). */
    KnobBinding levelKnobBinding (const juce::String& label, FloatRef ref)
    {
        return makeBinding (label, ref, [] (float db) { return fader::dbToPos (db); }, [] (float p) { return fader::posToDb (p); },
                            [] (float db) { return fader::text (db) + " dB"; }, 0.0f);
    }

    /** The MAIN MIX fader's law (`livedaw_dsp::fader::FaderLaw::XENYX1202`: the panel's printed scale). */
    const FaderLaw& mainLaw()
    {
        static const FaderLaw l { { { 0.0f, -90.0f }, { 0.05f, -60.0f }, { 0.103f, -40.0f }, { 0.182f, -30.0f }, { 0.361f, -20.0f }, { 0.572f, -10.0f },
                                    { 0.786f, 0.0f }, { 1.0f, 10.0f } },
                                  0 };
        return l;
    }

    KnobBinding mainBinding (const juce::String& label, FloatRef ref)
    {
        return makeBinding (label, ref, [] (float db) { return mainLaw().toPosition (db); }, [] (float p) { return mainLaw().toDb (p); },
                            [] (float db) { return fader::text (db) + " dB"; }, 0.0f);
    }

    juce::Rectangle<int> centred (int cx, int cy, int w, int h)
    {
        return juce::Rectangle<int> (w, h).withCentre ({ cy - kOy, kTop - cx });
    }

    /** The Q1202USB's gain knob scales (`livedaw_dsp::xenyx1202::preamp_gain_db`): mic +10 to +60 dB, line -10 to +40. */
    float gainOf (float position, bool line) { return (line ? -10.0f : 10.0f) + 50.0f * juce::jlimit (0.0f, 1.0f, position); }
    float gainPosition (float db, bool line) { return juce::jlimit (0.0f, 1.0f, (db - (line ? -10.0f : 10.0f)) / 50.0f); }
    /** The stereo channels' level switch: -10 dBV reads as +4 dBu with about 11.8 dB more gain. */
    constexpr float kMinus10Trim = 11.8f;

    /** The strip's gain knob is on the line scale (its LED flag is off; it starts on: a mic). */
    bool lineOf (const Strip& s) { return ! s.params.led.load(); }

    // The meter's four steps (CLIP, 6, 0, -20), left to right, and where each lights (dBFS; +4 dBu at -18 dBFS: Sidecar's reading).
    constexpr float kMeterDb[4] = { -38.0f, -18.0f, -12.0f, -1.0f };
    const char* kMeterText[4] = { "-20", "0", "6", "CLIP" };
    const juce::Colour kMeterColour[4] = { juce::Colour (0xff53bf3a), juce::Colour (0xff53bf3a), juce::Colour (0xffe2b21a), juce::Colour (0xffe5352c) };
    constexpr float kMeterX0 = 660.0f, kMeterDx = 27.0f, kMeterY0 = 1508.0f, kMeterDy = 54.0f;
} // namespace

namespace xy
{
    const hw::Look& look()
    {
        static const hw::Look l = []
        {
            hw::Look k;
            k.panelTop = kBody;
            k.panelBottom = kBody.darker (0.15f);
            k.frame = juce::Colour (0xff141415);
            k.section = kBand;
            k.sectionLine = juce::Colour (0xff4d4f52);
            k.silk = juce::Colour (0xffe9e9e9);
            k.silkDim = juce::Colour (0xffb0b2b4);
            k.keyTop = juce::Colour (0xff9a9c9b);
            k.keyBottom = juce::Colour (0xff6c6e6d);
            k.keyText = juce::Colour (0xff202020);
            k.backlit = false;
            k.slot = juce::Colour (0xff090909);
            k.capTop = juce::Colour (0xffa4a8a5);
            k.capBottom = juce::Colour (0xff7a7e7b);
            k.capLine = juce::Colour (0xfff4f4f4);
            k.knob = juce::Colour (0xff8d908c);
            k.knobLine = juce::Colour (0xfff4f4f4);
            k.keyRadius = 3.0f;
            k.ledWidth = 8.0f;
            k.ledHeight = 3.0f;
            k.keyBar = false;
            k.cap = hw::Look::Cap::Block;
            k.knobStyle = hw::Look::Knob::Plain;
            return k;
        }();
        return l;
    }

    // ---- Knob -----------------------------------------------------------------------------

    Knob::Knob (juce::Colour c, KnobBinding b) : cap (c), binding (std::move (b)) {}
    void Knob::setBinding (KnobBinding b) { binding = std::move (b); shown = -1.0f; repaint(); }

    void Knob::refresh()
    {
        const float now = binding.isValid() ? binding.get() : 0.0f;
        if (now != shown)
        {
            shown = now;
            repaint();
        }
    }

    void Knob::paint (juce::Graphics& g)
    {
        hw::knob (g, getLocalBounds().toFloat().reduced (1.0f), binding.isValid() ? binding.get() : 0.0f, look(), hw::Look::Knob::Plain, cap);
    }

    void Knob::mouseDown (const juce::MouseEvent&) { dragStart = binding.isValid() ? binding.get() : 0.0f; }
    void Knob::mouseDrag (const juce::MouseEvent& e)
    {
        if (binding.isValid())
            binding.set (juce::jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() / 160.0f));
    }
    void Knob::mouseDoubleClick (const juce::MouseEvent&) { if (binding.reset) binding.reset(); }
    void Knob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (binding.isValid())
            binding.set (juce::jlimit (0.0f, 1.0f, binding.get() + w.deltaY * 0.15f));
    }

    // ---- Key ------------------------------------------------------------------------------

    Key::Key (juce::Colour f, juce::Colour l) : face (f), led (l) {}

    void Key::refresh()
    {
        const bool now = isOn && isOn();
        if (now != shownOn)
        {
            shownOn = now;
            repaint();
        }
    }

    void Key::paint (juce::Graphics& g)
    {
        hw::key (g, getLocalBounds().toFloat().reduced (1.0f), {}, shownOn, down, led, look(), face, true);
    }

    void Key::mouseDown (const juce::MouseEvent&) { down = true; repaint(); }
    void Key::mouseUp (const juce::MouseEvent& e)
    {
        down = false;
        repaint();
        if (getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }

    // ---- Fader (the MAIN MIX's, horizontal) -----------------------------------------------

    Fader::Fader (juce::Colour c, KnobBinding b) : cap (c), binding (std::move (b)) {}
    void Fader::setBinding (KnobBinding b) { binding = std::move (b); shown = -1.0f; repaint(); }

    float Fader::capX (float position) const noexcept
    {
        constexpr float half = 44.0f;
        return half + juce::jlimit (0.0f, 1.0f, position) * ((float) juce::jmax (getWidth(), getHeight()) - 2.0f * half);
    }

    void Fader::refresh()
    {
        const float now = binding.isValid() ? binding.get() : 0.0f;
        if (now != shown)
        {
            shown = now;
            repaint();
        }
    }

    void Fader::paint (juce::Graphics& g)
    {
        // Drawn lying down in a space L long and T wide; stood up (the panel's fader runs front to back), -oo at the bottom.
        const float L = (float) juce::jmax (getWidth(), getHeight()), T = (float) juce::jmin (getWidth(), getHeight());
        if (getHeight() > getWidth())
            g.addTransform (juce::AffineTransform (0.0f, 1.0f, 0.0f, -1.0f, 0.0f, L));
        const float y = T * 0.5f;
        g.setColour (juce::Colour (0xff0b0b0b));
        g.fillRoundedRectangle (4.0f, y - 5.0f, L - 8.0f, 10.0f, 5.0f);
        g.setColour (juce::Colour (0xff3a3a3a));
        g.drawRoundedRectangle (4.0f, y - 5.0f, L - 8.0f, 10.0f, 5.0f, 1.0f);
        const float x = capX (binding.isValid() ? binding.get() : 0.0f);
        const juce::Rectangle<float> r (x - 44.0f, y - 25.0f, 88.0f, 50.0f);
        g.setGradientFill (juce::ColourGradient (cap.brighter (0.25f), r.getX(), r.getY(), cap.darker (0.3f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (int i = 1; i < 10; ++i)
            g.drawVerticalLine ((int) (r.getX() + 8.0f * (float) i + 4.0f), r.getY() + 4.0f, r.getBottom() - 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawVerticalLine ((int) x, r.getY() + 2.0f, r.getBottom() - 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    void Fader::mouseDown (const juce::MouseEvent& e)
    {
        const float along = getHeight() > getWidth() ? (float) getHeight() - e.position.y : e.position.x;
        if (binding.isValid())
            binding.set ((along - capX (0.0f)) / (capX (1.0f) - capX (0.0f)));
    }
    void Fader::mouseDrag (const juce::MouseEvent& e) { mouseDown (e); }
    void Fader::mouseDoubleClick (const juce::MouseEvent&) { if (binding.reset) binding.reset(); }
    void Fader::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (binding.isValid())
            binding.set (juce::jlimit (0.0f, 1.0f, binding.get() + w.deltaY * 0.1f));
    }
} // namespace xy

class Xenyx1202Surface::Panel final : public juce::Component
{
public:
    explicit Panel (Xenyx1202Surface& s) : owner (s) {}
    void paint (juce::Graphics& g) override { owner.paintPanel (g); }

private:
    Xenyx1202Surface& owner;
};

Xenyx1202Surface::Xenyx1202Surface (UiContext& c)
    : ctx (c), theme (universal::Theme::dark()), universalKey ("UNIVERSAL MIXER", theme.accent, theme), routingKey ("ROUTING", theme.accent, theme)
{
    setOpaque (true);
    universalKey.onClick = [&context = ctx]
    {
        juce::MessageManager::callAsync ([&context]
        {
            if (context.setInterface)
                context.setInterface (InterfaceKind::Universal);
        });
    };
    universalKey.setTooltip ("The Universal Mixer");
    addAndMakeVisible (universalKey);
    routingKey.isOn = [this] { return isRoutingShown(); };
    routingKey.onClick = [this] { showRouting (! isRoutingShown()); };
    routingKey.setTooltip ("The routing: the interface's inputs to the channels, the outputs to the interface");
    addAndMakeVisible (routingKey);
    panel = std::make_unique<Panel> (*this);
    addAndMakeVisible (*panel);
    build();
    for (auto* b : { &prevKey, &nextKey, &storeKey, &newKey })
        addAndMakeVisible (*b);
    addAndMakeVisible (sceneBox);
    addAndMakeVisible (sceneTitle);
    sceneTitle.setColour (juce::Label::textColourId, juce::Colour (0xffb8bcc2));
    sceneTitle.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    sceneBox.setTextWhenNothingSelected ("no scene");
    sceneBox.setTooltip ("A scene of the show: choosing one recalls it");
    prevKey.setTooltip ("Recall the previous scene");
    nextKey.setTooltip ("Recall the next scene");
    storeKey.setTooltip ("Store the console in the current scene (a new one if there is none)");
    newKey.setTooltip ("Add the console to the show as a new scene");
    prevKey.onClick = [this] { ctx.show.recallPrevious(); fillScenes(); };
    nextKey.onClick = [this] { ctx.show.recallNext(); fillScenes(); };
    storeKey.onClick = [this] { storeScene(); };
    newKey.onClick = [this] { newScene(); };
    sceneBox.onChange = [this] { recallSelected(); };
    refreshAll();
    startTimerHz (30);
}

Xenyx1202Surface::~Xenyx1202Surface() { stopTimer(); }

/** The routing page: two patch grids in the panel's colours. INPUTS: the interface's inputs (across) to the
    channels, the stereo pairs and 2-TRACK (down); OUTPUTS: the interface's outputs (down) fed by MAIN OUT,
    CTRL ROOM, PHONES and FX SEND (across). */
class Xenyx1202Surface::RoutingOverlay final : public juce::Component
{
public:
    RoutingOverlay (Xenyx1202Surface& o, const universal::Theme& t)
        : owner (o), theme (t), inputsKey ("INPUTS", t.accent, t), outputsKey ("OUTPUTS", t.accent, t), inGrid (t), outGrid (t)
    {
        inputsKey.isOn = [this] { return ! outputs; };
        outputsKey.isOn = [this] { return outputs; };
        inputsKey.onClick = [this] { showOutputs (false); };
        outputsKey.onClick = [this] { showOutputs (true); };
        for (auto* k : { &inputsKey, &outputsKey })
            addAndMakeVisible (*k);
        addAndMakeVisible (inGrid);
        addChildComponent (outGrid);
        build();
    }

    void showOutputs (bool shown)
    {
        outputs = shown;
        inGrid.setVisible (! shown);
        outGrid.setVisible (shown);
        inputsKey.refresh();
        outputsKey.refresh();
        repaint();
    }

    bool showsOutputs() const noexcept { return outputs; }
    universal::PatchGrid& inputsGrid() noexcept { return inGrid; }
    universal::PatchGrid& outputsGrid() noexcept { return outGrid; }

    void build()
    {
        auto& e = owner.ctx.engine;
        rows.clear();
        for (int i = 0; i < kChannels; ++i)
            if (auto* s = owner.channel (i))
                rows.push_back (s);
        if (auto* s = owner.aux (0))
            rows.push_back (s); // 2-TRACK
        sources.clear();
        for (int i = 0; i < routing::tileCount (routing::Group::LocalIn, e.getHardwareInputNames().size()); ++i)
            if (const int src = routing::sourceAt (routing::Group::LocalIn, i); src >= 0)
                sources.push_back (src);
        ports.clear();
        for (int i = 0; i < routing::tileCount (routing::Group::LocalOut, e.getHardwareOutputNames().size()); ++i)
            if (const int port = routing::outputAt (routing::Group::LocalOut, i); port >= 0)
                ports.push_back (port);
        signals.clear();
        if (auto* m = owner.main())
        {
            signals.push_back ({ "MAIN L", &m->params.outputLeft });
            signals.push_back ({ "MAIN R", &m->params.outputRight });
        }
        signals.push_back ({ "CTRL L", &e.monitorOutputLeft });
        signals.push_back ({ "CTRL R", &e.monitorOutputRight });
        signals.push_back ({ "PHONES L", &e.phones.outputLeft });
        signals.push_back ({ "PHONES R", &e.phones.outputRight });
        if (auto* b = owner.bus (0))
            signals.push_back ({ "FX SEND", &b->params.outputLeft });

        universal::PatchGrid::Model in;
        in.numRows = [this] { return (int) rows.size(); };
        in.numColumns = [this] { return (int) sources.size(); };
        in.rowName = [this] (int r) { return isTwoTrack (r) ? juce::String ("2-TRACK") : rows[(size_t) r]->getLabel(); };
        in.columnName = [this] (int c) { return routing::sourceLabel (sources[(size_t) c]); };
        in.state = [this] (int r, int c)
        {
            const auto& p = rows[(size_t) r]->params;
            const int src = sources[(size_t) c], left = p.inputLeft.load(), right = p.inputRight.load();
            if (left == src)
                return universal::PatchGrid::On;
            return p.stereo.load() && left >= 0 && (right == src || (right < 0 && src == left + 1)) ? universal::PatchGrid::Pair
                                                                                                    : universal::PatchGrid::Off;
        };
        in.toggle = [this] (int r, int c)
        {
            auto& p = rows[(size_t) r]->params;
            if (p.inputLeft.load() == sources[(size_t) c])
            {
                p.inputLeft = -1;
                p.inputRight = -1;
                return;
            }
            p.inputLeft = sources[(size_t) c];
            p.inputRight = isTwoTrack (r) ? sources[(size_t) c] + 1 : -1; // 2-TRACK is a stereo return
            if (isTwoTrack (r))
                p.stereo = true;
        };
        inGrid.setModel (std::move (in));

        universal::PatchGrid::Model out;
        out.numRows = [this] { return (int) ports.size(); };
        out.numColumns = [this] { return (int) signals.size(); };
        out.rowName = [this] (int r) { return universal::outputPortName (ports[(size_t) r]); };
        out.columnName = [this] (int c) { return signals[(size_t) c].name; };
        out.state = [this] (int r, int c)
        {
            return signals[(size_t) c].out->load() == ports[(size_t) r] ? universal::PatchGrid::On : universal::PatchGrid::Off;
        };
        out.toggle = [this] (int r, int c)
        {
            auto* o = signals[(size_t) c].out;
            if (o->load() == ports[(size_t) r])
            {
                o->store (-1);
                return;
            }
            owner.ctx.engine.clearOutput (ports[(size_t) r]); // one signal per port
            o->store (ports[(size_t) r]);
        };
        outGrid.setModel (std::move (out));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff151618));
        g.setColour (juce::Colour (0xff3b3e42));
        g.drawRect (getLocalBounds());
        const auto head = getLocalBounds().removeFromTop (46).withTrimmedLeft (16);
        g.setColour (theme.text);
        g.setFont (universal::type::font (universal::type::kTitle, true));
        g.drawText ("ROUTING", head, juce::Justification::centredLeft);
        g.setColour (theme.textDim);
        g.setFont (universal::type::font (universal::type::kCaption));
        g.drawText (outputs ? "The interface's outputs (down) fed by the mixer's (across)"
                            : "The interface's inputs (across) to the channels, the stereo pairs and 2-TRACK (down)",
                    head.withTrimmedLeft (120).withTrimmedRight (270), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto head = r.removeFromTop (34);
        outputsKey.setBounds (head.removeFromRight (120).reduced (2));
        inputsKey.setBounds (head.removeFromRight (120).reduced (2));
        r.removeFromTop (8);
        inGrid.setBounds (r);
        outGrid.setBounds (r);
    }

private:
    struct Sig
    {
        juce::String name;
        std::atomic<int32_t>* out;
    };
    bool isTwoTrack (int row) const { return rows[(size_t) row] == owner.aux (0); }

    Xenyx1202Surface& owner;
    const universal::Theme& theme;
    universal::ToggleKey inputsKey, outputsKey;
    universal::PatchGrid inGrid, outGrid;
    std::vector<Strip*> rows;
    std::vector<int> sources, ports;
    std::vector<Sig> signals;
    bool outputs = false;
};

void Xenyx1202Surface::showRouting (bool shown)
{
    if (shown && routingOverlay == nullptr)
    {
        routingOverlay = std::make_unique<RoutingOverlay> (*this, theme);
        addChildComponent (*routingOverlay);
    }
    if (routingOverlay != nullptr)
    {
        if (shown)
            routingOverlay->build();
        routingOverlay->setVisible (shown);
        if (shown)
            routingOverlay->toFront (false);
    }
    routingKey.refresh();
    resized();
}

bool Xenyx1202Surface::isRoutingShown() const noexcept { return routingOverlay != nullptr && routingOverlay->isVisible(); }
juce::Component* Xenyx1202Surface::getRoutingPage() noexcept { return routingOverlay.get(); }

universal::PatchGrid& Xenyx1202Surface::getRoutingGrid (bool outputs)
{
    if (routingOverlay == nullptr)
        showRouting (true);
    return outputs ? routingOverlay->outputsGrid() : routingOverlay->inputsGrid();
}

int Xenyx1202Surface::rowOf (int ch) noexcept { return ch < 4 ? ch : 4 + (ch - 4) / 2; }
bool Xenyx1202Surface::rightOf (int ch) noexcept { return ch >= 4 && ch % 2 == 0; }

Strip* Xenyx1202Surface::channel (int i) const
{
    const auto& l = ctx.engine.getChannels();
    return i >= 0 && i < (int) l.size() ? l[(size_t) i].get() : nullptr;
}
Strip* Xenyx1202Surface::aux (int i) const
{
    const auto& l = ctx.engine.getAux();
    return i >= 0 && i < (int) l.size() ? l[(size_t) i].get() : nullptr;
}
Strip* Xenyx1202Surface::bus (int i) const
{
    const auto& l = ctx.engine.getBuses();
    return i >= 0 && i < (int) l.size() ? l[(size_t) i].get() : nullptr;
}
Strip* Xenyx1202Surface::main() const
{
    const auto& l = ctx.engine.getMains();
    return l.empty() ? nullptr : l[0].get();
}

xy::Knob& Xenyx1202Surface::knob (const juce::String& id, juce::Colour cap, juce::Rectangle<int> r)
{
    auto* k = new xy::Knob (cap, {});
    k->setComponentID (id);
    k->setBounds (r);
    panel->addAndMakeVisible (k);
    controls.push_back (k);
    return *k;
}
xy::Key& Xenyx1202Surface::key (const juce::String& id, juce::Colour face, juce::Colour led, juce::Rectangle<int> r)
{
    auto* k = new xy::Key (face, led);
    k->setComponentID (id);
    k->setBounds (r);
    panel->addAndMakeVisible (k);
    controls.push_back (k);
    return *k;
}
xy::Fader& Xenyx1202Surface::fader (const juce::String& id, juce::Colour cap, juce::Rectangle<int> r)
{
    auto* f = new xy::Fader (cap, {});
    f->setComponentID (id);
    f->setBounds (r);
    panel->addAndMakeVisible (f);
    controls.push_back (f);
    return *f;
}

juce::Component& Xenyx1202Surface::getPanel() noexcept { return *panel; }

xy::Control* Xenyx1202Surface::findControl (const juce::String& id)
{
    for (auto* c : controls)
        if (c->getComponentID() == id)
            return c;
    return nullptr;
}

/** Makes every control, in the panel's pixels (the photograph's, turned upright by kOy, kTop). What they do is `bindControls`. */
void Xenyx1202Surface::build()
{
    const juce::Colour lamp (0xff3ec44a);
    for (int i = 0; i < kChannels; ++i)
    {
        const juce::String ch = "CH" + juce::String (i + 1) + " ";
        const int row = rowOf (i), cy = rowCy (row);
        // x of the LEVEL knob, then the others, from it: a mono row's, a stereo row's left channel and its right.
        const int lx = i < 4 ? 248 : rightOf (i) ? 592 : 244;
        knob (ch + "LEVEL", kWhite, centred (lx, cy - (i < 4 ? 8 : 6), 86, 86));
        if (i < 4)
        {
            knob (ch + "PAN", kPan, centred (338, cy - 6, 76, 76));
            knob (ch + "FX", kRed, centred (440, cy, 72, 72));
            knob (ch + "LOW", kNavy, centred (575, cy - 4, 72, 72));
            knob (ch + "MID", kNavy, centred (695, cy - 4, 72, 72));
            knob (ch + "HIGH", kNavy, centred (810, cy - 4, 72, 72));
            knob (ch + "COMP", kBlue, centred (893, cy - 4, 72, 72));
            key (ch + "LOWCUT", juce::Colour (0xffb4b5b3), {}, centred (545, cy + 48, 32, 32));
            // The jack panel: GAIN, the line jack and the XLR.
            knob (ch + "GAIN", kBlack, centred (1105, cy, 76, 76));
            micJacks[(size_t) i].setComponentID (ch + "MIC");
            micJacks[(size_t) i].setBounds (centred (1335, cy, 124, 124));
            lineJacks[(size_t) i].setComponentID (ch + "LINE");
            lineJacks[(size_t) i].setBounds (centred (1205, cy, 68, 68));
            panel->addAndMakeVisible (micJacks[(size_t) i]);
            panel->addAndMakeVisible (lineJacks[(size_t) i]);
        }
        else
        {
            const int bx = rightOf (i) ? 680 : 335, fx = rightOf (i) ? 780 : 440, kx = rightOf (i) ? 745 : 410;
            knob (ch + "BAL", kPan, centred (bx, cy - 6, 70, 70));
            knob (ch + "FX", kRed, centred (fx, cy - 4, 66, 66));
            key (ch + "PAD", juce::Colour (0xffb4b5b3), {}, centred (kx, cy + 46, 32, 32));
        }
        tapes[(size_t) i] = std::make_unique<ConsoleTape> (i + 1);
        tapes[(size_t) i]->setComponentID (ch + "TAPE");
        tapes[(size_t) i]->setBounds (centred (lx - 58, cy - 14, 84, 30));
        panel->addAndMakeVisible (*tapes[(size_t) i]);
    }
    juce::ignoreUnused (lamp);

    // The main section.
    knob ("PHONES LEVEL", kWhite, centred (230, 1388, 90, 90));
    key ("FX TO CTRL", juce::Colour (0xffb4b5b3), {}, centred (365, 1392, 56, 44));
    key ("USB TO MAIN", juce::Colour (0xffb4b5b3), {}, centred (470, 1392, 56, 44));
    key ("USB TO PHONES", juce::Colour (0xffb4b5b3), {}, centred (572, 1392, 56, 44));
    knob ("FX SEND", kRed, centred (680, 1390, 90, 90));
    key ("PHANTOM", kPhantom, juce::Colour (0xffff5a4a), centred (797, 1384, 50, 58));
    fader ("MAIN FADER", juce::Colour (0xff9fa3a5), centred (418, 1540, 64, 420));

    for (int i = 0; i < 4; ++i)
    {
        micJacks[(size_t) i].onClick = [this, i] { if (auto* s = channel (i)) { const float pos = gainPosition (s->params.input.trimDb.load(), lineOf (*s)); s->params.led = true; s->params.input.trimDb = gainOf (pos, false); } };
        lineJacks[(size_t) i].onClick = [this, i] { if (auto* s = channel (i)) { const float pos = gainPosition (s->params.input.trimDb.load(), lineOf (*s)); s->params.led = false; s->params.input.trimDb = gainOf (pos, true); } };
    }
}

/** The controls' bindings: what each knob, key and fader of the panel changes in the engine. The Q1202USB's
    controls are fixed, so a strip is found again at every call (a console that changes under the surface
    leaves the control idle). */
void Xenyx1202Surface::bindControls()
{
    const auto knobOf = [this] (const juce::String& id) { return static_cast<xy::Knob*> (findControl (id)); };
    const auto keyOf = [this] (const juce::String& id) { return static_cast<xy::Key*> (findControl (id)); };
    for (int i = 0; i < kChannels; ++i)
    {
        const juce::String ch = "CH" + juce::String (i + 1) + " ";
        const auto param = [this, i] (std::function<std::atomic<float>* (Strip&)> pick) -> FloatRef
        {
            return [this, i, pick] { auto* s = channel (i); return s != nullptr ? pick (*s) : nullptr; };
        };
        if (i < 4)
        {
            // The GAIN knob: the mic scale or the line scale, by the jack the cable is in (kept in the strip's `led`: on is MIC, the default; off is LINE).
            KnobBinding gain;
            gain.label = "GAIN";
            gain.get = [this, i] { auto* s = channel (i); return s != nullptr ? gainPosition (s->params.input.trimDb.load(), lineOf (*s)) : 0.0f; };
            gain.set = [this, i] (float p) { if (auto* s = channel (i)) s->params.input.trimDb = gainOf (p, lineOf (*s)); };
            gain.text = [this, i] { auto* s = channel (i); return s != nullptr ? juce::String (s->params.input.trimDb.load(), 0) + " dB" + (lineOf (*s) ? " LINE" : " MIC") : juce::String(); };
            gain.reset = [this, i] { if (auto* s = channel (i)) s->params.input.trimDb = gainOf (0.0f, lineOf (*s)); };
            knobOf (ch + "GAIN")->setBinding (std::move (gain));
            knobOf (ch + "COMP")->setBinding (linearBinding ("COMP", param ([] (Strip& s) { return &s.params.dynamics().values[0]; }), 0.0f, 10.0f, "", 1, 0.0f));
            auto* low = keyOf (ch + "LOWCUT");
            low->isOn = [this, i] { auto* s = channel (i); return s != nullptr && s->params.filter().lowCut.load(); };
            low->onClick = [this, i] { if (auto* s = channel (i)) s->params.filter().lowCut = ! s->params.filter().lowCut.load(); };
            const char* eq[] = { "HIGH", "MID", "LOW" };
            for (int b = 0; b < 3; ++b)
                knobOf (ch + eq[b])->setBinding (linearBinding (eq[b], param ([b] (Strip& s) { return &s.params.eq().values[b]; }), -15.0f, 15.0f, " dB", 1, 0.0f));
        }
        else
        {
            auto* pad = keyOf (ch + "PAD");
            pad->isOn = [this, i] { auto* s = channel (i); return s != nullptr && s->params.input.trimDb.load() > 6.0f; };
            pad->onClick = [this, i] { if (auto* s = channel (i)) s->params.input.trimDb = s->params.input.trimDb.load() > 6.0f ? 0.0f : kMinus10Trim; };
        }
        knobOf (ch + "FX")->setBinding (levelBinding ("FX", [this, i] { auto* s = channel (i); return s != nullptr ? &s->sends[0].levelDb : nullptr; }, 15.0f, -144.0f));
        knobOf (ch + (i < 4 ? "PAN" : "BAL"))->setBinding (linearBinding (i < 4 ? "PAN" : "BAL", param ([] (Strip& s) { return &s.params.pan; }), -100.0f, 100.0f, "", 0, 0.0f));
        knobOf (ch + "LEVEL")->setBinding (levelKnobBinding ("CH " + juce::String (i + 1), param ([] (Strip& s) { return &s.params.faderDb; })));
    }

    knobOf ("FX SEND")->setBinding (levelBinding ("FX SEND", [this] { auto* s = bus (0); return s != nullptr ? &s->params.faderDb : nullptr; }, 15.0f, 0.0f));

    // What PHONES / CTRL ROOM listen to: the main mix, or (Sidecar's reading of the keys) USB/2-TRACK or the FX send, one at a time.
    const int idxUsb = MixerEngine::monitorSourceIndex (StripKind::Aux, 0), idxFx = MixerEngine::monitorSourceIndex (StripKind::Bus, 0),
              idxMain = MixerEngine::monitorSourceIndex (StripKind::Main, 0);
    const auto source = [this, keyOf, idxMain] (const juce::String& id, int index)
    {
        auto* k = keyOf (id);
        k->isOn = [this, index] { return ctx.engine.phones.source.source.load() == index; };
        k->onClick = [this, index, idxMain]
        {
            const int to = ctx.engine.phones.source.source.load() == index ? idxMain : index;
            ctx.engine.phones.source.source = to;
            ctx.engine.speakers.source = to;
        };
    };
    source ("USB TO PHONES", idxUsb);
    source ("FX TO CTRL", idxFx);
    keyOf ("USB TO MAIN")->isOn = [this] { auto* s = aux (0); return s != nullptr && s->mains[0].on.load(); };
    keyOf ("USB TO MAIN")->onClick = [this] { if (auto* s = aux (0)) s->mains[0].on = ! s->mains[0].on.load(); };
    // +48 V is stored only: nothing is switched; the show keeps it with the mic sources.
    keyOf ("PHANTOM")->isOn = [this] { return phantom; };
    keyOf ("PHANTOM")->onClick = [this]
    {
        phantom = ! phantom;
        for (int i = 0; i < 4; ++i)
            if (auto* src = ctx.engine.getSource (i))
                src->phantom = phantom;
    };
    KnobBinding phones = levelBinding ("PHONES / CTRL ROOM", [this] { return &ctx.engine.phones.levelDb; }, 6.0f, -20.0f);
    phones.set = [this, inner = phones.set] (float p) { inner (p); ctx.engine.monitorLevelDb = ctx.engine.phones.levelDb.load(); };
    knobOf ("PHONES LEVEL")->setBinding (std::move (phones));

    static_cast<xy::Fader*> (findControl ("MAIN FADER"))->setBinding (mainBinding ("MAIN MIX", [this] { auto* s = main(); return s != nullptr ? &s->params.faderDb : nullptr; }));

    for (int i = 0; i < kChannels; ++i)
    {
        tapes[(size_t) i]->getText = [this, i] { auto* s = channel (i); return s != nullptr ? s->getOwnName() : juce::String(); };
        tapes[(size_t) i]->setText = [this, i] (const juce::String& t) { if (auto* s = channel (i)) s->setName (t); };
    }
}

/** What the panel assumes of the routing, set once on a new console (the engine's strips know nothing of
    it: sends live apart from the strip parameters): the FX knobs are post-LEVEL sends to the one FX bus,
    every channel is on the main mix, and USB/2-TRACK reaches it only through its key. The flag that says
    it was done is the main strip's `led` (a light this console has no use for; it starts on, off once
    done), which a scene keeps. */
void Xenyx1202Surface::ensureRouting()
{
    auto* m = main();
    if (m == nullptr)
        return;
    const bool fresh = m->params.led.load();
    for (int i = 0; i < kChannels + 1; ++i)
    {
        auto* s = i < kChannels ? channel (i) : aux (0);
        if (s == nullptr || ctx.engine.getBuses().empty())
            continue;
        s->sends[0].mode = (int) SendMode::Post;
        if (! fresh)
            continue;
        s->sends[0].enabled = i < kChannels;
        s->sends[0].levelDb = -144.0f;
        s->params.faderDb = 0.0f;
        s->mains[0].on = i < kChannels; // USB/2-TRACK to MAIN is a key
        s->mains[0].levelDb = 0.0f;
    }
    if (fresh)
    {
        m->params.faderDb = 0.0f;
        if (auto* b = bus (0))
            b->params.faderDb = 0.0f;
        ctx.engine.phones.source.source = MixerEngine::monitorSourceIndex (StripKind::Main, 0);
        ctx.engine.speakers.source = ctx.engine.phones.source.source.load();
        m->params.led = false;
    }
}

void Xenyx1202Surface::refreshAll()
{
    ensureRouting();
    bindControls();
    phantom = false;
    for (int i = 0; i < 4; ++i)
        if (auto* src = ctx.engine.getSource (i); src != nullptr && src->phantom.load())
            phantom = true;
    fillScenes();
    for (auto* c : controls)
        c->refresh();
    for (auto& t : tapes)
        t->refresh();
    repaint();
}

void Xenyx1202Surface::showChanged() { fillScenes(); }

void Xenyx1202Surface::fillScenes()
{
    const int n = ctx.show.getNumScenes();
    shownScenes = n;
    sceneBox.clear (juce::dontSendNotification);
    for (int i = 0; i < n; ++i)
        if (const auto s = ctx.show.getScene (i))
            sceneBox.addItem (juce::String (i + 1) + "  " + s->name, i + 1);
    const int current = ctx.show.getCurrentSceneIndex();
    sceneBox.setSelectedId (current >= 0 && current < n ? current + 1 : 0, juce::dontSendNotification);
}

void Xenyx1202Surface::recallSelected()
{
    const int index = sceneBox.getSelectedId() - 1;
    if (index < 0)
        return;
    ctx.show.recallScene (index);
    ctx.log.info ("Recalled scene " + juce::String (index + 1));
    refreshAll();
}

void Xenyx1202Surface::newScene()
{
    const int index = ctx.show.addScene ("Scene " + juce::String (ctx.show.getNumScenes() + 1));
    ctx.log.info ("Stored the console as scene " + juce::String (index + 1));
    fillScenes();
}

void Xenyx1202Surface::storeScene()
{
    const int current = ctx.show.getCurrentSceneIndex();
    if (current < 0 || current >= ctx.show.getNumScenes())
        return newScene();
    ctx.show.storeScene (current);
    ctx.log.info ("Stored the console in scene " + juce::String (current + 1));
    fillScenes();
}

void Xenyx1202Surface::timerCallback()
{
    if (ctx.show.getNumScenes() != shownScenes)
        fillScenes();
    for (auto* c : controls)
        c->refresh();
    for (auto& t : tapes)
        t->refresh();
    // The LEDs and meters move on their own.
    const auto area = [] (int px, int py, int w, int hh) { return juce::Rectangle<int> (py - kOy, kTop - (px + w), hh, w); };
    panel->repaint (area (640, 1480, 170, 110));
    for (int i = 0; i < 4; ++i)
    {
        const int cy = rowCy (i);
        panel->repaint (area (320, cy + 70, 40, 24));
        panel->repaint (area (873, cy + 70, 40, 24));
    }
}

void Xenyx1202Surface::resized()
{
    auto bar = getLocalBounds().removeFromTop (kBarHeight).reduced (10, 9);
    sceneTitle.setBounds (bar.removeFromLeft (80));
    prevKey.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (6);
    sceneBox.setBounds (bar.removeFromLeft (320));
    bar.removeFromLeft (6);
    nextKey.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (18);
    storeKey.setBounds (bar.removeFromLeft (80));
    bar.removeFromLeft (6);
    newKey.setBounds (bar.removeFromLeft (70));
    universalKey.setBounds (getLocalBounds().removeFromTop (kBarHeight).removeFromRight (190).reduced (universal::sizes::kGap, 7));
    routingKey.setBounds (getLocalBounds().removeFromTop (kBarHeight).withTrimmedRight (190).removeFromRight (110).reduced (universal::sizes::kGap, 7));
    if (routingOverlay != nullptr)
        routingOverlay->setBounds (getLocalBounds().withTrimmedTop (kBarHeight).reduced (8));

    const auto area = getLocalBounds().withTrimmedTop (kBarHeight).reduced (8).toFloat();
    const float s = juce::jmin (area.getWidth() / (float) kPanelWidth, area.getHeight() / (float) kPanelHeight);
    panel->setBounds (0, 0, kPanelWidth, kPanelHeight);
    panel->setTransform (juce::AffineTransform::scale (s).translated (area.getCentreX() - (float) kPanelWidth * s * 0.5f,
                                                                      area.getCentreY() - (float) kPanelHeight * s * 0.5f));
}

void Xenyx1202Surface::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1c1e), 0.0f, 0.0f, juce::Colour (0xff101112), 0.0f, (float) getHeight(), false));
    g.fillAll();
    g.setColour (juce::Colour (0xff2a2c2f));
    g.fillRect (0, 0, getWidth(), kBarHeight);
    g.setColour (juce::Colour (0xff3b3e42));
    g.drawHorizontalLine (kBarHeight - 1, 0.0f, (float) getWidth());
}

// ---- the panel's drawing ----------------------------------------------------------------------------

namespace
{
    void jack (juce::Graphics& g, float cx, float cy, float r, bool xlr, bool plugged)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8dadb), cx, cy - r, juce::Colour (0xff6f7274), cx, cy + r, false));
        g.fillEllipse (cx - r, cy - r, 2 * r, 2 * r);
        g.setColour (juce::Colour (0xff101010));
        g.fillEllipse (cx - r * 0.82f, cy - r * 0.82f, r * 1.64f, r * 1.64f);
        if (xlr)
        {
            g.setColour (juce::Colour (0xff3d3f41));
            g.fillEllipse (cx - r * 0.66f, cy - r * 0.66f, r * 1.32f, r * 1.32f);
            g.setColour (juce::Colour (0xff050505));
            for (const auto d : { juce::Point<float> (-0.3f, -0.18f), juce::Point<float> (0.3f, -0.18f), juce::Point<float> (0.0f, 0.34f) })
                g.fillEllipse (cx + d.x * r - r * 0.13f, cy + d.y * r - r * 0.13f, r * 0.26f, r * 0.26f);
        }
        else
        {
            g.setColour (juce::Colour (0xff2b2c2e));
            g.fillEllipse (cx - r * 0.5f, cy - r * 0.5f, r, r);
            g.setColour (juce::Colour (0xff000000));
            g.fillEllipse (cx - r * 0.25f, cy - r * 0.25f, r * 0.5f, r * 0.5f);
        }
        if (plugged)
        {
            g.setColour (juce::Colour (0xff7bd88f));
            g.drawEllipse (cx - r - 3.0f, cy - r - 3.0f, 2 * r + 6.0f, 2 * r + 6.0f, 2.0f);
        }
    }

    void rca (juce::Graphics& g, float cx, float cy, juce::Colour ring)
    {
        g.setColour (juce::Colour (0xff1a1a1a));
        g.fillEllipse (cx - 24.0f, cy - 24.0f, 48.0f, 48.0f);
        g.setColour (ring);
        g.fillEllipse (cx - 20.0f, cy - 20.0f, 40.0f, 40.0f);
        g.setColour (juce::Colour (0xffd9dadc));
        g.fillEllipse (cx - 13.0f, cy - 13.0f, 26.0f, 26.0f);
        g.setColour (juce::Colour (0xff111111));
        g.fillEllipse (cx - 5.0f, cy - 5.0f, 10.0f, 10.0f);
    }

    void led (juce::Graphics& g, float cx, float cy, float r, juce::Colour c, bool on)
    {
        g.setColour (on ? c : c.darker (0.85f));
        g.fillEllipse (cx - r, cy - r, 2 * r, 2 * r);
        if (on)
        {
            g.setColour (c.withAlpha (0.35f));
            g.fillEllipse (cx - r * 1.9f, cy - r * 1.9f, r * 3.8f, r * 3.8f);
        }
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (cx - r, cy - r, 2 * r, 2 * r, 1.0f);
    }
} // namespace

void Xenyx1202Surface::paintPanel (juce::Graphics& g)
{
    g.addTransform (juce::AffineTransform (0.0f, 1.0f, (float) -kOy, -1.0f, 0.0f, (float) kTop)); // the photograph turned upright
    const auto& lk = xy::look();
    auto darkLook = lk;
    darkLook.silk = kInk;
    darkLook.silkDim = juce::Colour (0xff55585b);
    // Text in the photograph's coordinates, `onGrey` in ink for the light cells, `turn` degrees clockwise about its centre.
    const auto text = [&] (const juce::String& s, float cx, float cy, float w, float size, bool onGrey = false, float turn = 0.0f)
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::ignoreUnused (turn); // upright: in the photograph's space the text is turned back a quarter
        g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (90.0f), cx, cy));
        const auto lines = juce::StringArray::fromLines (s);
        const float line = size * 1.25f, top = cy - line * (float) lines.size() * 0.5f;
        for (int n = 0; n < lines.size(); ++n)
            hw::silk (g, { cx - w * 0.5f, top + line * (float) n, w, line }, lines[n], onGrey ? darkLook : lk, size, juce::Justification::centred, false);
    };
    const auto box = [&] (juce::Rectangle<float> r, juce::Colour c, float radius = 5.0f)
    {
        g.setColour (c);
        g.fillRoundedRectangle (r, radius);
    };

    // The silver frame, the dark body and the raised jack panel.
    g.setGradientFill (juce::ColourGradient (kSilver, 0.0f, 350.0f, kSilverDark, 0.0f, 1695.0f, false));
    g.fillRoundedRectangle (90.0f, 352.0f, 1370.0f, 1343.0f, 34.0f);
    box ({ 108.0f, 398.0f, 856.0f, 1252.0f }, kBody, 14.0f);
    box ({ 972.0f, 386.0f, 478.0f, 1268.0f }, kJackPanel, 10.0f);
    g.setColour (juce::Colour (0xff131415));
    g.drawRoundedRectangle (108.0f, 398.0f, 856.0f, 1252.0f, 14.0f, 1.5f);

    // ---- the jack panel -------------------------------------------------------------------------
    for (int i = 0; i < 4; ++i)
    {
        const float cy = (float) rowCy (i);
        auto* s = channel (i);
        const bool line = s != nullptr && lineOf (*s);
        jack (g, 1335.0f, cy, 60.0f, true, ! line);
        jack (g, 1205.0f, cy, 34.0f, false, line);
        box ({ 1240.0f, cy - 60.0f, 34.0f, 30.0f }, juce::Colour (0xffdcdcd8), 6.0f);
        text ("MIC", 1257.0f, cy - 45.0f, 34.0f, 12.0f, true);
        text ("LINE\nIN", 1257.0f, cy + 22.0f, 34.0f, 11.0f);
        text ("BAL\nOR\nUNBAL", 1160.0f, cy - 2.0f, 40.0f, 9.0f, false, 0.0f);
        text ("GAIN", 1140.0f, cy + 46.0f, 60.0f, 13.0f, false, -90.0f);
        text ("+10  -10", 1078.0f, cy - 52.0f, 60.0f, 9.0f);
        text ("+40  +60", 1130.0f, cy - 52.0f, 60.0f, 9.0f);
        text (juce::String (i + 1), 1436.0f, cy + 20.0f, 30.0f, 22.0f, false, 90.0f);
    }
    static const char* pairs[] = { "LINE IN 5/6", "LINE IN 7/8", "LINE IN 9/10", "LINE IN 11/12" };
    for (int k = 0; k < 4; ++k)
    {
        const float cy = 1062.0f + 110.0f * (float) k;
        jack (g, 1100.0f, cy, 36.0f, false, false);
        jack (g, 1210.0f, cy, 36.0f, false, false);
        jack (g, 1325.0f, cy, 36.0f, false, false);
        box ({ 1260.0f, cy - 48.0f, 24.0f, 96.0f }, juce::Colour (0xffd2d4d0), 4.0f);
        text (pairs[k], 1272.0f, cy, 90.0f, 10.0f, true, -90.0f);
        text ("R", 1095.0f, cy + 54.0f, 14.0f, 10.0f);
        text ("L/MONO", 1195.0f, cy + 54.0f, 50.0f, 10.0f);
        text ("BAL OR\nUNBAL", 1150.0f, cy - 34.0f, 42.0f, 8.0f);
    }
    static const char* outs[] = { "FX SEND", "PHONES", "MAIN OUT\nL", "MAIN OUT\nR" };
    for (int k = 0; k < 4; ++k)
    {
        const float cy = 1062.0f + 110.0f * (float) k;
        box ({ 1366.0f, cy - 44.0f, 24.0f, 88.0f }, juce::Colour (0xffd2d4d0), 4.0f);
        text (outs[k], 1378.0f, cy, 80.0f, 9.0f, true, 90.0f);
    }
    // The rear row: 2-TRACK IN and OUT (RCA pairs) and CTRL ROOM OUT.
    rca (g, 1105.0f, 1500.0f, juce::Colour (0xffe8e8e8));
    rca (g, 1105.0f, 1575.0f, juce::Colour (0xffd23a35));
    rca (g, 1195.0f, 1500.0f, juce::Colour (0xffe8e8e8));
    rca (g, 1195.0f, 1575.0f, juce::Colour (0xffd23a35));
    text ("2-TRACK", 1150.0f, 1618.0f, 110.0f, 11.0f);
    text ("IN", 1105.0f, 1626.0f, 30.0f, 10.0f);
    text ("OUT", 1195.0f, 1626.0f, 40.0f, 10.0f);
    jack (g, 1290.0f, 1500.0f, 36.0f, false, false);
    jack (g, 1290.0f, 1575.0f, 36.0f, false, false);
    box ({ 1336.0f, 1470.0f, 24.0f, 130.0f }, juce::Colour (0xffd2d4d0), 4.0f);
    text ("CTRL ROOM OUT", 1348.0f, 1535.0f, 120.0f, 9.0f, true, 90.0f);
    text ("XENYX Q1202USB", 1412.0f, 1012.0f, 260.0f, 26.0f, false, 90.0f);
    g.setColour (juce::Colour (0xff131415));
    g.drawRoundedRectangle (972.0f, 386.0f, 478.0f, 1268.0f, 10.0f, 1.5f);

    // ---- the mono channels ------------------------------------------------------------------------
    for (int row = 0; row < 6; ++row)
    {
        const float top = (float) rowTop (row), cy = (float) rowCy (row), h = row < 4 ? 140.0f : 138.0f;
        box ({ 160.0f, top, 140.0f, h }, kCell, 3.0f);
        if (row < 4)
        {
            box ({ 400.0f, top, 100.0f, h }, kBand, 6.0f);
            box ({ 506.0f, top, 330.0f, h }, kEqBand, 4.0f);
            box ({ 838.0f, top, 100.0f, h }, kCompBand, 6.0f);
            const int i = row;
            text ("LEVEL", 287.0f, cy, 40.0f, 9.0f, true);
            text ("+15", 287.0f, cy + 36.0f, 24.0f, 8.0f, true);
            text ("0", 287.0f, cy - 38.0f, 12.0f, 8.0f, true);
            g.setColour (kInk);
            g.drawRect (166.0f, top + 6.0f, 26.0f, h - 12.0f, 1.2f);
            text (juce::String (i + 1), 179.0f, cy + 44.0f, 22.0f, 17.0f, true);
            text ("PAN", 342.0f, cy + 48.0f, 36.0f, 11.0f);
            auto* s = channel (i);
            const bool clip = s != nullptr && ctx.meters.get (s, MeterPoint::Trim, 0) >= 0.89f;
            led (g, 340.0f, cy + 82.0f, 5.0f, juce::Colour (0xffe5352c), clip);
            text ("FX", 450.0f, cy + 54.0f, 30.0f, 12.0f);
            text ("LOW", 581.0f, cy + 50.0f, 40.0f, 10.0f);
            text ("MID", 701.0f, cy + 50.0f, 40.0f, 10.0f);
            text ("HIGH", 816.0f, cy + 50.0f, 40.0f, 10.0f);
            text ("80 Hz", 569.0f, cy + 50.0f, 40.0f, 8.0f);
            text ("2.5 kHz", 689.0f, cy + 50.0f, 44.0f, 8.0f);
            text ("12 kHz", 804.0f, cy + 50.0f, 44.0f, 8.0f);
            text ("LOW CUT\n75 Hz", 515.0f, cy + 50.0f, 44.0f, 8.0f);
            text ("EQ", 524.0f, cy - 52.0f, 20.0f, 12.0f);
            text ("COMP", 893.0f, cy + 48.0f, 40.0f, 11.0f);
            const bool reducing = s != nullptr && s->meters.dynReductionDb.load() > 0.5f;
            led (g, 893.0f, cy + 82.0f, 5.0f, juce::Colour (0xffe5352c), reducing);
        }
        else
        {
            // 7/8 and 11/12 at the left, 5/6 and 9/10 at the right (the panel's order).
            for (int side = 0; side < 2; ++side)
            {
                const int i = side == 0 ? 5 + 2 * (row - 4) : 4 + 2 * (row - 4);
                const float ox = side == 0 ? 0.0f : 345.0f;
                if (side == 1)
                    box ({ 505.0f, top, 130.0f, h }, kCell, 3.0f);
                box ({ 400.0f + ox, top, 100.0f, h }, kBand, 6.0f);
                const juce::String name = juce::String (2 * (i - 4) + 5) + "/" + juce::String (2 * (i - 4) + 6);
                text (name, 179.0f + ox, cy + 44.0f, 36.0f, 12.0f, true);
                text ("LEVEL", 287.0f + ox, cy, 40.0f, 9.0f, true);
                text ("-oo", 290.0f + ox, cy - 36.0f, 20.0f, 8.0f, true);
                text ("+15", 290.0f + ox, cy + 36.0f, 20.0f, 8.0f, true);
                text ("BAL", 335.0f + ox, cy + 52.0f, 30.0f, 11.0f);
                text ("FX", 472.0f + ox, cy + 54.0f, 30.0f, 12.0f);
                text ("+4\n-10", (side == 0 ? 442.0f : 787.0f), cy + 56.0f, 26.0f, 9.0f);
            }
        }
    }
    box ({ 838.0f, 1020.0f, 122.0f, 280.0f }, kBand, 4.0f);

    // ---- the main section -------------------------------------------------------------------------
    box ({ 152.0f, 1318.0f, 690.0f, 300.0f }, kMainCell, 6.0f);
    box ({ 404.0f, 1324.0f, 204.0f, 140.0f }, kBand, 4.0f);
    box ({ 614.0f, 1324.0f, 126.0f, 140.0f }, kBand, 4.0f);
    g.setColour (kInk);
    g.drawRect (326.0f, 1334.0f, 70.0f, 118.0f, 1.2f);
    g.drawRect (160.0f, 1326.0f, 28.0f, 284.0f, 1.2f);
    text ("MAIN SECTION", 174.0f, 1470.0f, 200.0f, 12.0f, true, -90.0f);
    text ("PHONES/\nCTRL ROOM", 290.0f, 1388.0f, 70.0f, 10.0f, true);
    text ("-oo", 200.0f, 1448.0f, 24.0f, 9.0f, true);
    text ("FX to\nCTRL RM", 410.0f, 1392.0f, 50.0f, 9.0f, true);
    text ("USB/2-TRACK", 421.0f, 1394.0f, 110.0f, 11.0f, false, -90.0f);
    text ("TO MAIN\nMIX", 512.0f, 1392.0f, 50.0f, 9.0f);
    text ("TO PHONES/\nCTRL RM", 614.0f, 1392.0f, 56.0f, 9.0f);
    text ("FX SEND", 628.0f, 1384.0f, 60.0f, 11.0f, false, -90.0f);
    text ("-oo", 650.0f, 1450.0f, 24.0f, 9.0f);
    text ("+15", 712.0f, 1450.0f, 24.0f, 9.0f);
    text ("PHANTOM", 841.0f, 1384.0f, 56.0f, 10.0f, true);
    text ("MAIN MIX", 196.0f, 1580.0f, 60.0f, 11.0f, true, -90.0f);
    text ("+48 V", 774.0f, 1562.0f, 40.0f, 9.0f, true);
    text ("POWER", 774.0f, 1508.0f, 40.0f, 9.0f, true);
    led (g, 818.0f, 1508.0f, 7.0f, juce::Colour (0xfff08a2e), true);
    led (g, 818.0f, 1562.0f, 7.0f, juce::Colour (0xffe5352c), phantom);
    box ({ 855.0f, 1380.0f, 100.0f, 250.0f }, kBand, 4.0f);
    text ("USB INTERFACE", 905.0f, 1520.0f, 160.0f, 13.0f, false, -90.0f);

    // The MAIN MIX scale (printed above the fader's slot) on its law.
    static const std::pair<const char*, float> scale[] = { { "-oo", -144.0f }, { "60", -60.0f }, { "40", -40.0f }, { "30", -30.0f }, { "25", -25.0f },
                                                            { "20", -20.0f }, { "15", -15.0f }, { "10", -10.0f }, { "5", -5.0f }, { "0", 0.0f }, { "+5", 5.0f }, { "+10", 10.0f } };
    {
        constexpr float x0 = 418.0f - 210.0f + 44.0f, x1 = 418.0f + 210.0f - 44.0f;
        for (const auto& [label, db] : scale)
        {
            const float x = x0 + mainLaw().toPosition (db) * (x1 - x0);
            g.setColour (kInk);
            g.drawVerticalLine ((int) x, 1500.0f, 1512.0f);
            text (label, x, 1494.0f, 26.0f, 8.5f, true);
        }
    }

    // The meter: L above R, each four steps (-20, 0, 6, CLIP), the two halves of the main mix.
    for (int side = 0; side < 2; ++side)
    {
        const float y = kMeterY0 + kMeterDy * (float) side;
        const float level = main() != nullptr ? ctx.meters.get (main(), MeterPoint::Output, side) : 0.0f;
        const float db = level > 1.0e-6f ? 20.0f * std::log10 (level) : -200.0f;
        text (side == 0 ? "L" : "R", 634.0f, y, 14.0f, 13.0f, true);
        for (int step = 0; step < 4; ++step)
        {
            const float x = kMeterX0 + kMeterDx * (float) step;
            led (g, x, y, 7.0f, kMeterColour[step], db >= kMeterDb[step]);
            if (side == 0)
                text (kMeterText[step], x, y + 27.0f, 34.0f, 8.0f, true);
        }
    }
}

} // namespace livedaw::ui
