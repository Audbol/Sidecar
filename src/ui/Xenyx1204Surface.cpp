#include "ui/Xenyx1204Surface.h"

#include "engine/FaderLaw.h"

#include <cmath>

namespace livedaw::ui
{
namespace
{
    // Colours read off the straight-on render of the panel.
    const juce::Colour kBody (0xff2d2e30), kBodyTop (0xff38393b), kBand (0xff1d1e20), kGrey (0xffc6c9ca), kGreyDark (0xffa9adae);
    const juce::Colour kBlue (0xff3b86c9), kNavy (0xff1f3b8c), kRed (0xffd6262b), kOrange (0xfff08a2e), kPanGrey (0xff3b3b3d);
    const juce::Colour kSilkOnGrey (0xff25272a), kPlate (0xffc9cccd), kKeyRed (0xffd7262c);

    constexpr int kStripPitch = 152;
    constexpr float kFaderTop = 1351.0f, kFaderBottom = 1692.0f; // the cap's centre at +10 dB and at -oo
    constexpr float kFaderBox = 1325.0f;                          // top of a fader component

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

    /** A level knob: -oo at its start, then dB from -60 to `maxDb` (the taper is Sidecar's). */
    KnobBinding levelBinding (const juce::String& label, FloatRef ref, float maxDb, float def)
    {
        constexpr float kFloorPos = 0.02f;
        return makeBinding (label, ref,
                            [maxDb] (float db) { return db <= -100.0f ? 0.0f : kFloorPos + (1.0f - kFloorPos) * (juce::jmax (db, -60.0f) + 60.0f) / (maxDb + 60.0f); },
                            [maxDb] (float p) { return p < kFloorPos ? -144.0f : -60.0f + (p - kFloorPos) / (1.0f - kFloorPos) * (maxDb + 60.0f); },
                            [] (float db) { return db <= -100.0f ? juce::String ("-oo") : juce::String (db, 1) + " dB"; }, def);
    }

    KnobBinding faderBinding (const juce::String& label, FloatRef ref)
    {
        return makeBinding (label, ref, [] (float db) { return fader::dbToPos (db); }, [] (float p) { return fader::posToDb (p); },
                            [] (float db) { return fader::text (db) + " dB"; }, 0.0f);
    }

    juce::Rectangle<int> centred (int cx, int cy, int w, int h)
    {
        return juce::Rectangle<int> (w, h).withCentre ({ cx, cy });
    }

    /** The 1204USB's gain knob scales (`livedaw_dsp::xenyx1204::preamp_gain_db`): mic +10 to +60 dB, line -10 to +40. */
    float gainOf (float position, bool line) { return (line ? -10.0f : 10.0f) + 50.0f * juce::jlimit (0.0f, 1.0f, position); }
    float gainPosition (float db, bool line) { return juce::jlimit (0.0f, 1.0f, (db - (line ? -10.0f : 10.0f)) / 50.0f); }
    /** The stereo channels' level switch: -10 dBV reads as +4 dBu with about 11.8 dB more gain. */
    constexpr float kMinus10Trim = 11.8f;

    /** The strip's gain knob is on the line scale (its LED flag is off; it starts on: a mic). */
    bool lineOf (const Strip& s) { return ! s.params.led.load(); }

    juce::Colour meterColour (int row, bool lit)
    {
        const juce::Colour c = row == 0 ? juce::Colour (0xffe5352c) : row < 5 ? juce::Colour (0xffe2b21a) : juce::Colour (0xff53bf3a);
        return lit ? c : c.darker (0.8f);
    }
    // Meter rows, top to bottom: CLIP, +10, +7, +4, +2, 0, -2, -4, -7, -10, -20, -30, and where each lights (dBFS; +4 dBu at -18 dBFS: Sidecar's reading).
    constexpr float kMeterDb[12] = { -1.0f, -8.0f, -11.0f, -14.0f, -16.0f, -18.0f, -20.0f, -22.0f, -25.0f, -28.0f, -38.0f, -48.0f };
    const char* kMeterText[12] = { "CLIP", "10", "7", "4", "2", "0", "2", "4", "7", "10", "20", "30" };
    constexpr float kMeterY0 = 952.0f, kMeterStep = 28.8f;
} // namespace

namespace xy
{
    const hw::Look& look()
    {
        static const hw::Look l = []
        {
            hw::Look k;
            k.panelTop = kBodyTop;
            k.panelBottom = kBody;
            k.frame = juce::Colour (0xff141415);
            k.section = kBand;
            k.sectionLine = juce::Colour (0xff4d4f52);
            k.silk = juce::Colour (0xffe9e9e9);
            k.silkDim = juce::Colour (0xffb0b2b4);
            k.keyTop = juce::Colour (0xff414244);
            k.keyBottom = juce::Colour (0xff1c1d1e);
            k.keyText = juce::Colour (0xffe0e0e0);
            k.backlit = false;
            k.slot = juce::Colour (0xff090909);
            k.capTop = juce::Colour (0xff4b4c4e);
            k.capBottom = juce::Colour (0xff1b1c1d);
            k.capLine = juce::Colours::white;
            k.knob = juce::Colour (0xff303133);
            k.knobLine = juce::Colour (0xfff4f4f4);
            k.keyRadius = 3.0f;
            k.ledWidth = 10.0f;
            k.ledHeight = 4.0f;
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
        // The key's moulded triangle.
        const auto r = getLocalBounds().toFloat();
        juce::Path tri;
        tri.addTriangle (r.getCentreX(), r.getCentreY() - 5.0f, r.getCentreX() - 6.0f, r.getCentreY() + 4.0f, r.getCentreX() + 6.0f, r.getCentreY() + 4.0f);
        g.setColour (juce::Colours::black.withAlpha (face.isTransparent() ? 0.55f : 0.35f));
        g.fillPath (tri);
    }

    void Key::mouseDown (const juce::MouseEvent&) { down = true; repaint(); }
    void Key::mouseUp (const juce::MouseEvent& e)
    {
        down = false;
        repaint();
        if (getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }

    // ---- Fader ----------------------------------------------------------------------------

    Fader::Fader (juce::Colour c, KnobBinding b) : cap (c), binding (std::move (b)) {}
    void Fader::setBinding (KnobBinding b) { binding = std::move (b); shown = -1.0f; repaint(); }

    float Fader::capY (float position) const noexcept
    {
        return (kFaderTop - kFaderBox) + (1.0f - juce::jlimit (0.0f, 1.0f, position)) * (kFaderBottom - kFaderTop);
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
        const float x = (float) getWidth() * 0.5f;
        hw::faderSlot (g, x, capY (1.0f) - 4.0f, capY (0.0f) + 4.0f, look());
        hw::faderCap (g, x, capY (binding.isValid() ? binding.get() : 0.0f), 56.0f, look(), cap);
    }

    void Fader::mouseDown (const juce::MouseEvent& e)
    {
        if (binding.isValid())
            binding.set (1.0f - (e.position.y - capY (1.0f)) / (capY (0.0f) - capY (1.0f)));
    }
    void Fader::mouseDrag (const juce::MouseEvent& e) { mouseDown (e); }
    void Fader::mouseDoubleClick (const juce::MouseEvent&) { if (binding.reset) binding.reset(); }
    void Fader::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (binding.isValid())
            binding.set (juce::jlimit (0.0f, 1.0f, binding.get() + w.deltaY * 0.1f));
    }
} // namespace xy

class Xenyx1204Surface::Panel final : public juce::Component
{
public:
    explicit Panel (Xenyx1204Surface& s) : owner (s) {}
    void paint (juce::Graphics& g) override { owner.paintPanel (g); }

private:
    Xenyx1204Surface& owner;
};

Xenyx1204Surface::Xenyx1204Surface (UiContext& c) : ctx (c)
{
    setOpaque (true);
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

Xenyx1204Surface::~Xenyx1204Surface() { stopTimer(); }

Strip* Xenyx1204Surface::channel (int i) const
{
    const auto& l = ctx.engine.getChannels();
    return i >= 0 && i < (int) l.size() ? l[(size_t) i].get() : nullptr;
}
Strip* Xenyx1204Surface::aux (int i) const
{
    const auto& l = ctx.engine.getAux();
    return i >= 0 && i < (int) l.size() ? l[(size_t) i].get() : nullptr;
}
Strip* Xenyx1204Surface::bus (int i) const
{
    const auto& l = ctx.engine.getBuses();
    return i >= 0 && i < (int) l.size() ? l[(size_t) i].get() : nullptr;
}
Strip* Xenyx1204Surface::main() const
{
    const auto& l = ctx.engine.getMains();
    return l.empty() ? nullptr : l[0].get();
}

xy::Knob& Xenyx1204Surface::knob (const juce::String& id, juce::Colour cap, juce::Rectangle<int> r)
{
    auto* k = new xy::Knob (cap, {});
    k->setComponentID (id);
    k->setBounds (r);
    panel->addAndMakeVisible (k);
    controls.push_back (k);
    return *k;
}
xy::Key& Xenyx1204Surface::key (const juce::String& id, juce::Colour face, juce::Colour led, juce::Rectangle<int> r)
{
    auto* k = new xy::Key (face, led);
    k->setComponentID (id);
    k->setBounds (r);
    panel->addAndMakeVisible (k);
    controls.push_back (k);
    return *k;
}
xy::Fader& Xenyx1204Surface::fader (const juce::String& id, juce::Colour cap, juce::Rectangle<int> r)
{
    auto* f = new xy::Fader (cap, {});
    f->setComponentID (id);
    f->setBounds (r);
    panel->addAndMakeVisible (f);
    controls.push_back (f);
    return *f;
}

juce::Component& Xenyx1204Surface::getPanel() noexcept { return *panel; }

xy::Control* Xenyx1204Surface::findControl (const juce::String& id)
{
    for (auto* c : controls)
        if (c->getComponentID() == id)
            return c;
    return nullptr;
}

/** Makes every control, in the render's pixels. What they do is `bindControls`. */
void Xenyx1204Surface::build()
{
    for (int i = 0; i < kChannels; ++i)
    {
        const int kx = kStripPitch * i;
        const juce::String ch = "CH" + juce::String (i + 1) + " ";
        if (i < 4)
        {
            knob (ch + "GAIN", juce::Colour (0xff2a2b2d), centred (172 + kx, 440, 76, 76));
            knob (ch + "COMP", kBlue, centred (143 + kx, 588, 64, 64));
            key (ch + "LOWCUT", {}, juce::Colour (0xff3ec44a), centred (150 + kx, 362, 52, 26));
            micJacks[(size_t) i].setComponentID (ch + "MIC");
            micJacks[(size_t) i].setBounds (centred (160 + kx, 132, 100, 100));
            lineJacks[(size_t) i].setComponentID (ch + "LINE");
            lineJacks[(size_t) i].setBounds (centred (150 + kx, 275, 64, 64));
            panel->addAndMakeVisible (micJacks[(size_t) i]);
            panel->addAndMakeVisible (lineJacks[(size_t) i]);
        }
        else
        {
            key (ch + "LEVEL", {}, juce::Colour (0xff3ec44a), centred (150 + kx, 462, 52, 26));
        }
        knob (ch + "HIGH", kNavy, centred (150 + kx, 690, 66, 66));
        knob (ch + "MID", kNavy, centred (150 + kx, 783, 66, 66));
        knob (ch + "LOW", kNavy, centred (150 + kx, 877, 66, 66));
        knob (ch + "AUX1", kRed, centred (143 + kx, 980, 66, 66));
        knob (ch + "AUX2", kOrange, centred (147 + kx, 1073, 66, 66));
        knob (ch + (i < 4 ? "PAN" : "BAL"), kPanGrey, centred (143 + kx, 1172, 62, 62));
        key (ch + "ALT", {}, juce::Colour (0xffe2b21a), centred (215 + kx, 1260, 52, 48));
        soloKeys[(size_t) i].setComponentID (ch + "SOLO");
        soloKeys[(size_t) i].setBounds (centred (143 + kx, 1260, 82, 48));
        panel->addAndMakeVisible (soloKeys[(size_t) i]);
        fader (ch + "FADER", juce::Colour (0xff3a3b3d), { 146 + kx - 30, (int) kFaderBox, 60, 390 });

        tapes[(size_t) i] = std::make_unique<ConsoleTape> (i + 1);
        tapes[(size_t) i]->setComponentID (ch + "TAPE");
        tapes[(size_t) i]->setBounds (centred (146 + kx, 1824, 140, 56));
        panel->addAndMakeVisible (*tapes[(size_t) i]);
    }
    knob ("AUXSEND1", kRed, centred (1060, 782, 66, 66));
    knob ("AUXSEND2", kOrange, centred (1060, 878, 66, 66));
    knob ("RETURN1", kRed, centred (1210, 782, 66, 66));
    knob ("RETURN1 TOSEND1", kRed, centred (1362, 782, 66, 66));
    knob ("RETURN2", kOrange, centred (1210, 878, 66, 66));
    key ("RETURN2 ASSIGN", {}, juce::Colour (0xffe2b21a), centred (1363, 880, 56, 28));
    key ("SOURCE 2TR", kKeyRed, juce::Colour (0xffff5a4a), centred (1070, 980, 56, 28));
    key ("SOURCE ALT", {}, juce::Colour (0xffff5a4a), centred (1070, 1073, 56, 28));
    key ("SOURCE MAIN", {}, juce::Colour (0xffff5a4a), centred (1070, 1167, 56, 28));
    key ("2TR TO MAIN", kKeyRed, juce::Colour (0xffff5a4a), centred (1211, 1166, 56, 28));
    key ("PHANTOM", {}, juce::Colour (0xffe5352c), centred (1212, 980, 30, 30));
    knob ("PHONES LEVEL", juce::Colour (0xffd9dadb), centred (1212, 1073, 84, 84));
    fader ("ALT FADER", juce::Colour (0xff4b5660), { 1098 - 30, (int) kFaderBox, 60, 390 });
    fader ("MAIN FADER L", juce::Colour (0xffd9dadb), { 1305 - 30, (int) kFaderBox, 60, 390 });
    fader ("MAIN FADER R", juce::Colour (0xffd9dadb), { 1385 - 30, (int) kFaderBox, 60, 390 });

    for (int i = 0; i < 4; ++i)
    {
        micJacks[(size_t) i].onClick = [this, i] { if (auto* s = channel (i)) { const float pos = gainPosition (s->params.input.trimDb.load(), lineOf (*s)); s->params.led = true; s->params.input.trimDb = gainOf (pos, false); } };
        lineJacks[(size_t) i].onClick = [this, i] { if (auto* s = channel (i)) { const float pos = gainPosition (s->params.input.trimDb.load(), lineOf (*s)); s->params.led = false; s->params.input.trimDb = gainOf (pos, true); } };
    }
    for (int i = 0; i < kChannels; ++i)
        soloKeys[(size_t) i].onClick = [this, i] { if (auto* s = channel (i)) s->params.solo = ! s->params.solo.load(); };
}

/** The controls' bindings: what each knob, key and fader of the panel changes in the engine. The 1204USB's
    controls are fixed, so a strip is found again at every call (a console that changes under the surface
    leaves the control idle). */
void Xenyx1204Surface::bindControls()
{
    for (int i = 0; i < kChannels; ++i)
    {
        const juce::String ch = "CH" + juce::String (i + 1) + " ";
        const auto param = [this, i] (std::function<std::atomic<float>* (Strip&)> pick) -> FloatRef
        {
            return [this, i, pick] { auto* s = channel (i); return s != nullptr ? pick (*s) : nullptr; };
        };
        const auto k = [this] (const juce::String& id) { return static_cast<xy::Knob*> (findControl (id)); };
        if (i < 4)
        {
            // The GAIN knob: the mic scale or the line scale, by the jack the cable is in (kept in the strip's `led`: on is MIC, the default; off is LINE).
            KnobBinding gain;
            gain.label = "GAIN";
            gain.get = [this, i] { auto* s = channel (i); return s != nullptr ? gainPosition (s->params.input.trimDb.load(), lineOf (*s)) : 0.0f; };
            gain.set = [this, i] (float p) { if (auto* s = channel (i)) s->params.input.trimDb = gainOf (p, lineOf (*s)); };
            gain.text = [this, i] { auto* s = channel (i); return s != nullptr ? juce::String (s->params.input.trimDb.load(), 0) + " dB" + (lineOf (*s) ? " LINE" : " MIC") : juce::String(); };
            gain.reset = [this, i] { if (auto* s = channel (i)) s->params.input.trimDb = gainOf (0.0f, lineOf (*s)); };
            k (ch + "GAIN")->setBinding (std::move (gain));
            k (ch + "COMP")->setBinding (linearBinding ("COMP", param ([] (Strip& s) { return &s.params.dynamics().values[0]; }), 0.0f, 10.0f, "", 1, 0.0f));
            auto* low = static_cast<xy::Key*> (findControl (ch + "LOWCUT"));
            low->isOn = [this, i] { auto* s = channel (i); return s != nullptr && s->params.filter().lowCut.load(); };
            low->onClick = [this, i] { if (auto* s = channel (i)) s->params.filter().lowCut = ! s->params.filter().lowCut.load(); };
        }
        else
        {
            auto* level = static_cast<xy::Key*> (findControl (ch + "LEVEL"));
            level->isOn = [this, i] { auto* s = channel (i); return s != nullptr && s->params.input.trimDb.load() > 6.0f; };
            level->onClick = [this, i] { if (auto* s = channel (i)) s->params.input.trimDb = s->params.input.trimDb.load() > 6.0f ? 0.0f : kMinus10Trim; };
        }
        const char* eq[] = { "HIGH", "MID", "LOW" };
        for (int b = 0; b < 3; ++b)
            k (ch + eq[b])->setBinding (linearBinding (eq[b], param ([b] (Strip& s) { return &s.params.eq().values[b]; }), -15.0f, 15.0f, " dB", 1, 0.0f));
        k (ch + "AUX1")->setBinding (levelBinding ("AUX 1", [this, i] { auto* s = channel (i); return s != nullptr ? &s->sends[0].levelDb : nullptr; }, 15.0f, -144.0f));
        k (ch + "AUX2")->setBinding (levelBinding ("AUX 2", [this, i] { auto* s = channel (i); return s != nullptr ? &s->sends[1].levelDb : nullptr; }, 15.0f, -144.0f));
        k (ch + (i < 4 ? "PAN" : "BAL"))->setBinding (linearBinding (i < 4 ? "PAN" : "BAL", param ([] (Strip& s) { return &s.params.pan; }), -100.0f, 100.0f, "", 0, 0.0f));
        auto* alt = static_cast<xy::Key*> (findControl (ch + "ALT"));
        alt->isOn = [this, i] { auto* s = channel (i); return s != nullptr && s->sends[2].enabled.load(); };
        alt->onClick = [this, i]
        {
            // MUTE / ALT 3-4: the channel leaves the main mix for the ALT 3-4 bus.
            if (auto* s = channel (i))
            {
                const bool to = ! s->sends[2].enabled.load();
                s->sends[2].enabled = to;
                s->mains[0].on = ! to;
            }
        };
        static_cast<xy::Fader*> (findControl (ch + "FADER"))->setBinding (faderBinding ("CH " + juce::String (i + 1), param ([] (Strip& s) { return &s.params.faderDb; })));
    }

    const auto knobOf = [this] (const juce::String& id) { return static_cast<xy::Knob*> (findControl (id)); };
    const auto keyOf = [this] (const juce::String& id) { return static_cast<xy::Key*> (findControl (id)); };
    knobOf ("AUXSEND1")->setBinding (levelBinding ("AUX SEND 1", [this] { auto* s = bus (0); return s != nullptr ? &s->params.faderDb : nullptr; }, 15.0f, 0.0f));
    knobOf ("AUXSEND2")->setBinding (levelBinding ("AUX SEND 2", [this] { auto* s = bus (1); return s != nullptr ? &s->params.faderDb : nullptr; }, 15.0f, 0.0f));
    knobOf ("RETURN1")->setBinding (levelBinding ("RETURN 1", [this] { auto* s = aux (0); return s != nullptr ? &s->params.faderDb : nullptr; }, 20.0f, 0.0f));
    knobOf ("RETURN1 TOSEND1")->setBinding (levelBinding ("TO AUX SEND 1", [this] { auto* s = aux (0); return s != nullptr ? &s->sends[0].levelDb : nullptr; }, 15.0f, -144.0f));
    knobOf ("RETURN2")->setBinding (levelBinding ("RETURN 2", [this] { auto* s = aux (1); return s != nullptr ? &s->params.faderDb : nullptr; }, 20.0f, 0.0f));
    keyOf ("RETURN2 ASSIGN")->isOn = [this] { auto* s = aux (1); return s != nullptr && s->sends[2].enabled.load(); };
    keyOf ("RETURN2 ASSIGN")->onClick = [this]
    {
        if (auto* s = aux (1))
        {
            const bool alt = ! s->sends[2].enabled.load();
            s->sends[2].enabled = alt;
            s->mains[0].on = ! alt;
        }
    };

    // SOURCE: what PHONES / CTRL ROOM listen to (2-TR/USB, ALT 3-4 or MAIN MIX; the last when none is down).
    const int idx2tr = MixerEngine::monitorSourceIndex (StripKind::Aux, 2), idxAlt = MixerEngine::monitorSourceIndex (StripKind::Bus, 2),
              idxMain = MixerEngine::monitorSourceIndex (StripKind::Main, 0);
    const auto source = [this, keyOf] (const juce::String& id, int index)
    {
        auto* k = keyOf (id);
        k->isOn = [this, index] { return ctx.engine.phones.source.source.load() == index; };
        k->onClick = [this, index] { ctx.engine.phones.source.source = index; ctx.engine.speakers.source = index; };
    };
    source ("SOURCE 2TR", idx2tr);
    source ("SOURCE ALT", idxAlt);
    source ("SOURCE MAIN", idxMain);
    keyOf ("2TR TO MAIN")->isOn = [this] { auto* s = aux (2); return s != nullptr && s->mains[0].on.load(); };
    keyOf ("2TR TO MAIN")->onClick = [this] { if (auto* s = aux (2)) s->mains[0].on = ! s->mains[0].on.load(); };
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

    const auto mainFader = [this] { auto* s = main(); return s != nullptr ? &s->params.faderDb : nullptr; };
    static_cast<xy::Fader*> (findControl ("MAIN FADER L"))->setBinding (faderBinding ("MAIN MIX", mainFader));
    static_cast<xy::Fader*> (findControl ("MAIN FADER R"))->setBinding (faderBinding ("MAIN MIX", mainFader));
    static_cast<xy::Fader*> (findControl ("ALT FADER"))->setBinding (faderBinding ("ALT 3-4", [this] { auto* s = bus (2); return s != nullptr ? &s->params.faderDb : nullptr; }));

    for (int i = 0; i < kChannels; ++i)
    {
        tapes[(size_t) i]->getText = [this, i] { auto* s = channel (i); return s != nullptr ? s->getOwnName() : juce::String(); };
        tapes[(size_t) i]->setText = [this, i] (const juce::String& t) { if (auto* s = channel (i)) s->setName (t); };
    }
}

/** What the panel assumes of the routing, set once on a new console (the engine's strips know nothing of
    it: sends live apart from the strip parameters): AUX 1 is a pre-fader send and AUX 2 a post-fader one,
    ALT 3-4 takes a channel post-fader at 0 dB while its MUTE / ALT 3-4 key is down, and 2-TR/USB reaches
    the main mix only through its key. The flag that says it was done is the main strip's `led` (a light
    this console has no use for; it starts on, off once done), which a scene keeps. */
void Xenyx1204Surface::ensureRouting()
{
    auto* m = main();
    if (m == nullptr)
        return;
    for (int i = 0; i < kChannels + 3; ++i)
    {
        auto* s = i < kChannels ? channel (i) : aux (i - kChannels);
        if (s == nullptr)
            continue;
        s->sends[0].mode = (int) (i < kChannels ? SendMode::Pre : SendMode::Post);
        s->sends[1].mode = (int) SendMode::Post;
        s->sends[2].mode = (int) SendMode::Post;
        if (! m->params.led.load())
            continue;
        s->sends[0].enabled = i < kChannels || i == kChannels;
        s->sends[1].enabled = i < kChannels;
        s->sends[2].enabled = false;
        s->sends[2].levelDb = 0.0f;
        if (i >= kChannels)
        {
            s->params.faderDb = 0.0f;
            s->mains[0].on = i != kChannels + 2; // 2-TR/USB to MAIN is a key
        }
        else
        {
            s->mains[0].on = true;
            s->mains[0].levelDb = 0.0f;
            s->params.faderDb = -144.0f;
        }
    }
    if (m->params.led.load())
    {
        m->params.faderDb = 0.0f;
        for (int i = 0; i < 3; ++i)
            if (auto* b = bus (i))
                b->params.faderDb = 0.0f;
        ctx.engine.phones.source.source = MixerEngine::monitorSourceIndex (StripKind::Main, 0);
        ctx.engine.speakers.source = ctx.engine.phones.source.source.load();
        m->params.led = false;
    }
}

void Xenyx1204Surface::refreshAll()
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

void Xenyx1204Surface::showChanged() { fillScenes(); }

void Xenyx1204Surface::fillScenes()
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

void Xenyx1204Surface::recallSelected()
{
    const int index = sceneBox.getSelectedId() - 1;
    if (index < 0)
        return;
    ctx.show.recallScene (index);
    ctx.log.info ("Recalled scene " + juce::String (index + 1));
    refreshAll();
}

void Xenyx1204Surface::newScene()
{
    const int index = ctx.show.addScene ("Scene " + juce::String (ctx.show.getNumScenes() + 1));
    ctx.log.info ("Stored the console as scene " + juce::String (index + 1));
    fillScenes();
}

void Xenyx1204Surface::storeScene()
{
    const int current = ctx.show.getCurrentSceneIndex();
    if (current < 0 || current >= ctx.show.getNumScenes())
        return newScene();
    ctx.show.storeScene (current);
    ctx.log.info ("Stored the console in scene " + juce::String (current + 1));
    fillScenes();
}

void Xenyx1204Surface::timerCallback()
{
    if (ctx.show.getNumScenes() != shownScenes)
        fillScenes();
    for (auto* c : controls)
        c->refresh();
    for (auto& t : tapes)
        t->refresh();
    // The LEDs and meters move on their own.
    panel->repaint (1180, 940, 262, 370);
    for (int i = 0; i < kChannels; ++i)
        panel->repaint (kStripPitch * i + 190, 1320, 50, 90);
    for (int i = 0; i < 4; ++i)
        panel->repaint (kStripPitch * i + 215, 570, 36, 40);
}

void Xenyx1204Surface::resized()
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

    const auto area = getLocalBounds().withTrimmedTop (kBarHeight).reduced (8).toFloat();
    const float s = juce::jmin (area.getWidth() / (float) kPanelWidth, area.getHeight() / (float) kPanelHeight);
    panel->setBounds (0, 0, kPanelWidth, kPanelHeight);
    panel->setTransform (juce::AffineTransform::scale (s).translated (area.getCentreX() - (float) kPanelWidth * s * 0.5f,
                                                                      area.getCentreY() - (float) kPanelHeight * s * 0.5f));
}

void Xenyx1204Surface::paint (juce::Graphics& g)
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

void Xenyx1204Surface::paintPanel (juce::Graphics& g)
{
    const auto& lk = xy::look();
    auto darkLook = lk;
    darkLook.silk = kSilkOnGrey;
    darkLook.silkDim = juce::Colour (0xff55585b);
    const auto text = [&] (const juce::String& s, float cx, float cy, float w, float size, bool onGrey = false, bool bold = false,
                           juce::Justification j = juce::Justification::centred)
    {
        juce::ignoreUnused (bold);
        const auto lines = juce::StringArray::fromLines (s);
        const float line = size * 1.25f, top = cy - line * (float) lines.size() * 0.5f;
        for (int n = 0; n < lines.size(); ++n)
            hw::silk (g, { cx - w * 0.5f, top + line * (float) n, w, line }, lines[n], onGrey ? darkLook : lk, size, j, false);
    };
    const auto box = [&] (juce::Rectangle<float> r, juce::Colour c, float radius = 5.0f)
    {
        g.setColour (c);
        g.fillRoundedRectangle (r, radius);
    };

    // The frame: silver cheeks either side of the dark body.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc9ccce), 0.0f, 0.0f, juce::Colour (0xff9da1a4), 60.0f, 0.0f, false));
    g.fillRect (0, 25, 70, kPanelHeight - 50);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9da1a4), 1470.0f, 0.0f, juce::Colour (0xffc9ccce), 1536.0f, 0.0f, false));
    g.fillRect (1470, 25, 66, kPanelHeight - 50);
    hw::panel (g, { 66.0f, 28.0f, 1408.0f, 1838.0f }, lk);
    box ({ 66.0f, 28.0f, 1408.0f, 482.0f }, kBodyTop, 0.0f);

    // ---- the jack panel -----------------------------------------------------------------------
    for (int i = 0; i < 4; ++i)
    {
        const float kx = (float) (kStripPitch * i);
        auto* s = channel (i);
        const bool line = s != nullptr && lineOf (*s);
        jack (g, 160.0f + kx, 132.0f, 48.0f, true, ! line);
        box ({ 142.0f + kx, 204.0f, 56.0f, 24.0f }, juce::Colour (0xffe8e8e8), 6.0f);
        text ("MIC", 170.0f + kx, 216.0f, 56.0f, 16.0f, true);
        jack (g, 150.0f + kx, 275.0f, 30.0f, false, line);
        text ("BAL\nOR\nUNBAL", 218.0f + kx, 276.0f, 50.0f, 10.0f);
        text ("LINE IN", 150.0f + kx, 326.0f, 90.0f, 15.0f);
        text ("LOW CUT\n75 Hz\n18 dB/Oct", 216.0f + kx, 364.0f, 62.0f, 10.0f);
        text (juce::String (i + 1), 240.0f + kx, 490.0f, 20.0f, 16.0f);
        text ("GAIN", 172.0f + kx, 495.0f, 60.0f, 14.0f);
        text ("+10  -10", 158.0f + kx - 22.0f, 484.0f, 56.0f, 9.0f, false, false, juce::Justification::centredRight);
        text ("+40  +60", 190.0f + kx + 30.0f, 484.0f, 56.0f, 9.0f, false, false, juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff1a1a1a));
        g.fillEllipse (232.0f + kx - 10.0f, 440.0f - 10.0f, 20.0f, 20.0f);
    }
    for (int i = 4; i < 6; ++i)
    {
        const float kx = (float) (kStripPitch * i);
        jack (g, 150.0f + kx, 296.0f, 30.0f, false, false);
        jack (g, 150.0f + kx, 388.0f, 30.0f, false, false);
        text ("MONO\nL", 214.0f + kx, 290.0f, 40.0f, 10.0f);
        text ("BAL\nOR\nUNBAL", 214.0f + kx, 346.0f, 44.0f, 10.0f);
        text ("R", 214.0f + kx, 388.0f, 20.0f, 12.0f);
        text ("LEVEL\n+4\n-10", 214.0f + kx, 462.0f, 50.0f, 10.0f);
        text (i == 4 ? "LINE IN 5/6" : "LINE IN 7/8", 150.0f + kx + 20.0f, 493.0f, 130.0f, 16.0f);
    }
    // Stereo aux returns and aux sends.
    for (const auto& p : { juce::Point<float> (758, 98), { 850, 98 }, { 951, 98 }, { 758, 188 }, { 850, 188 }, { 951, 188 } })
        jack (g, p.x, p.y, 30.0f, false, false);
    text ("LEFT/MONO", 758.0f, 48.0f, 110.0f, 12.0f);
    text ("RIGHT", 862.0f, 48.0f, 60.0f, 12.0f);
    text ("1", 800.0f, 62.0f, 14.0f, 11.0f);
    text ("2", 800.0f, 152.0f, 14.0f, 11.0f);
    text ("FX", 901.0f, 158.0f, 30.0f, 12.0f);
    text ("1", 996.0f, 62.0f, 14.0f, 11.0f);
    text ("2", 996.0f, 152.0f, 14.0f, 11.0f);
    text ("STEREO AUX RETURNS", 800.0f, 240.0f, 200.0f, 14.0f);
    text ("AUX SENDS", 950.0f, 240.0f, 100.0f, 14.0f);
    // 2-TRACK / USB.
    text ("2-TRACK/USB", 1096.0f, 42.0f, 160.0f, 15.0f);
    text ("INPUT", 1056.0f, 69.0f, 70.0f, 14.0f);
    text ("OUTPUT", 1137.0f, 69.0f, 70.0f, 14.0f);
    for (const float x : { 1056.0f, 1137.0f })
    {
        rca (g, x, 125.0f, juce::Colour (0xfff4f4f4));
        rca (g, x, 203.0f, juce::Colour (0xffd02a2a));
    }
    text ("L", 1090.0f, 125.0f, 14.0f, 12.0f);
    text ("R", 1090.0f, 203.0f, 14.0f, 12.0f);
    jack (g, 1096.0f, 388.0f, 30.0f, false, false);
    text ("PHONES", 1096.0f, 443.0f, 100.0f, 15.0f);
    // The two stereo line inputs' printed frames.
    g.setColour (lk.sectionLine);
    g.drawRect (710.0f, 252.0f, 296.0f, 246.0f, 1.0f);
    g.drawVerticalLine (857, 252.0f, 498.0f);
    g.drawRect (1008.0f, 30.0f, 172.0f, 218.0f, 1.0f);
    g.drawRect (1187.0f, 40.0f, 258.0f, 460.0f, 1.0f);
    // The title block (no logo): the model name and what the panel prints under it.
    text ("XENYX 1204USB", 1316.0f, 66.0f, 250.0f, 28.0f);
    text ("PREMIUM 12-INPUT 2/2-BUS MIXER", 1316.0f, 106.0f, 250.0f, 11.0f);
    text ("MIC PREAMPS & COMPRESSORS", 1316.0f, 124.0f, 250.0f, 11.0f);
    text ("BRITISH EQS | USB AUDIO INTERFACE", 1316.0f, 142.0f, 250.0f, 11.0f);

    // ---- the mixer: comp, EQ, aux, pan ----------------------------------------------------------
    box ({ 96.0f, 546.0f, 910.0f, 98.0f }, kBand, 6.0f);
    box ({ 98.0f, 650.0f, 908.0f, 288.0f }, juce::Colour (0xff25262a), 4.0f);
    box ({ 98.0f, 930.0f, 908.0f, 200.0f }, kBand, 4.0f);
    box ({ 96.0f, 1132.0f, 910.0f, 176.0f }, juce::Colour (0xff2a2b2e), 4.0f);
    for (int i = 0; i < kChannels; ++i)
    {
        const float kx = (float) (kStripPitch * i);
        auto* s = channel (i);
        if (i < 4)
        {
            const bool working = s != nullptr && std::abs (s->meters.dynReductionDb.load()) >= 0.5f;
            led (g, 232.0f + kx, 590.0f, 6.0f, juce::Colour (0xffe5352c), working);
            text ("COMP", 218.0f + kx, 611.0f, 50.0f, 12.0f);
            text ("0", 128.0f + kx, 634.0f, 14.0f, 10.0f);
            text ("10", 174.0f + kx, 634.0f, 20.0f, 10.0f);
            g.setColour (lk.silk);
            juce::Path slope;
            slope.startNewSubPath (200.0f + kx, 574.0f);
            slope.lineTo (214.0f + kx, 562.0f);
            slope.lineTo (238.0f + kx, 558.0f);
            g.strokePath (slope, juce::PathStrokeType (1.5f));
        }
        text ("EQ", 220.0f + kx, 657.0f, 40.0f, 15.0f);
        const char* band[] = { "HIGH", "MID", "LOW" };
        const char* hz[] = { "12 kHz", "2.5 kHz", "80 Hz" };
        for (int b = 0; b < 3; ++b)
        {
            const float y = 690.0f + 93.5f * (float) b;
            text (band[b], 220.0f + kx, y - 4.0f, 54.0f, 13.0f);
            text (hz[b], 220.0f + kx, y + 10.0f, 54.0f, 9.0f);
            text ("-15", 118.0f + kx, y + 44.0f, 24.0f, 9.0f);
            text ("+15", 184.0f + kx, y + 44.0f, 24.0f, 9.0f);
        }
        text ("AUX", 220.0f + kx, 943.0f, 50.0f, 17.0f);
        text ("MON\nPRE", 219.0f + kx, 987.0f, 40.0f, 11.0f);
        text ("FX\nPOST", 219.0f + kx, 1080.0f, 40.0f, 11.0f);
        text ("1", 110.0f + kx, 940.0f, 12.0f, 11.0f);
        text ("2", 110.0f + kx, 1036.0f, 12.0f, 11.0f);
        text (i < 4 ? "PAN" : "BAL", 222.0f + kx, 1175.0f, 40.0f, 13.0f);
        text ("L", 118.0f + kx, 1220.0f, 12.0f, 9.0f);
        text ("R", 170.0f + kx, 1220.0f, 12.0f, 9.0f);
        // The printed switch label, the channel's SOLO key (Sidecar's: a press lights it), the MUTE / ALT 3-4 key.
        const bool solo = s != nullptr && s->params.solo.load();
        box ({ 104.0f + kx, 1236.0f, 80.0f, 50.0f }, solo ? juce::Colour (0xff8f6d1a) : juce::Colour (0xff3a3b3e), 4.0f);
        g.setColour (lk.sectionLine);
        g.drawRoundedRectangle (104.0f + kx, 1236.0f, 80.0f, 50.0f, 4.0f, 1.0f);
        text (juce::String (i == 4 ? "5/6" : i == 5 ? "7/8" : juce::String (i + 1)), 144.0f + kx, 1248.0f, 70.0f, 17.0f);
        text ("MUTE\nALT 3-4", 144.0f + kx, 1272.0f, 70.0f, 10.5f);
    }

    // ---- USB, aux sends and returns, SOURCE, meters ------------------------------------------------
    box ({ 1008.0f, 546.0f, 430.0f, 160.0f }, kBand, 6.0f);
    box ({ 1136.0f, 610.0f, 176.0f, 42.0f }, juce::Colour (0xff2a2b2e), 4.0f);
    text ("USB", 1196.0f, 630.0f, 70.0f, 17.0f);
    text ("INTERFACE", 1266.0f, 636.0f, 90.0f, 10.0f);
    box ({ 1008.0f, 712.0f, 148.0f, 222.0f }, kBand, 4.0f);
    box ({ 1158.0f, 712.0f, 280.0f, 222.0f }, kBand, 4.0f);
    box ({ 1014.0f, 714.0f, 136.0f, 24.0f }, kPlate, 3.0f);
    box ({ 1164.0f, 714.0f, 268.0f, 24.0f }, kPlate, 3.0f);
    text ("AUX SENDS", 1082.0f, 726.0f, 130.0f, 14.0f, true);
    text ("STEREO AUX RETURNS", 1298.0f, 726.0f, 250.0f, 14.0f, true);
    text ("1", 1016.0f, 746.0f, 10.0f, 11.0f);
    text ("2", 1016.0f, 842.0f, 10.0f, 11.0f);
    text ("FX", 1112.0f, 880.0f, 30.0f, 12.0f);
    text ("-oo", 1040.0f, 828.0f, 24.0f, 9.0f);
    text ("+15", 1084.0f, 828.0f, 24.0f, 9.0f);
    text ("1", 1168.0f, 746.0f, 10.0f, 11.0f);
    text ("1", 1318.0f, 746.0f, 10.0f, 11.0f);
    text ("2", 1168.0f, 842.0f, 10.0f, 11.0f);
    text ("TO AUX\nSEND 1", 1288.0f, 780.0f, 60.0f, 9.0f);
    g.setColour (lk.silk);
    g.drawArrow ({ 1250.0f, 764.0f, 1318.0f, 764.0f }, 1.5f, 6.0f, 6.0f);
    g.drawArrow ({ 1250.0f, 880.0f, 1330.0f, 880.0f }, 1.5f, 6.0f, 6.0f);
    text ("MAIN MIX\nALT 3-4", 1398.0f, 912.0f, 70.0f, 9.5f);
    text ("-oo", 1190.0f, 828.0f, 24.0f, 9.0f);
    text ("+20", 1234.0f, 828.0f, 24.0f, 9.0f);
    text ("-oo", 1342.0f, 828.0f, 24.0f, 9.0f);
    text ("+15", 1386.0f, 828.0f, 24.0f, 9.0f);

    g.setColour (kGrey);
    g.fillRoundedRectangle (1008.0f, 936.0f, 430.0f, 374.0f, 8.0f);
    g.setColour (juce::Colour (0xff22242a));
    g.drawRoundedRectangle (1018.0f, 948.0f, 106.0f, 280.0f, 6.0f, 1.8f);
    g.drawLine (1124.0f, 1073.0f, 1170.0f, 1073.0f, 1.4f);
    box ({ 1018.0f, 1200.0f, 106.0f, 28.0f }, juce::Colour (0xff22242a), 4.0f);
    text ("SOURCE", 1070.0f, 1214.0f, 90.0f, 14.0f);
    text ("2-TR/USB", 1070.0f, 1006.0f, 90.0f, 12.0f, true);
    text ("ALT 3-4", 1070.0f, 1100.0f, 90.0f, 12.0f, true);
    text ("MAIN MIX", 1070.0f, 1194.0f, 90.0f, 12.0f, true);
    text ("POWER", 1158.0f, 960.0f, 50.0f, 11.0f, true);
    text ("+48 V", 1250.0f, 960.0f, 50.0f, 11.0f, true);
    text ("-oo", 1170.0f, 1126.0f, 26.0f, 10.0f, true);
    text ("MAX", 1250.0f, 1118.0f, 36.0f, 10.0f, true);
    text ("PHONES/CTRL ROOM", 1212.0f, 1136.0f, 160.0f, 11.0f, true);
    text ("2-TR/USB TO MAIN", 1211.0f, 1195.0f, 160.0f, 11.0f, true);
    led (g, 1176.0f, 954.0f, 6.0f, juce::Colour (0xfff08a2e), true);
    led (g, 1249.0f, 982.0f, 6.0f, juce::Colour (0xffe5352c), phantom);

    // The meters: two columns of LEDs, the two halves of the main mix.
    for (int side = 0; side < 2; ++side)
    {
        const float x = 1304.0f + 80.0f * (float) side;
        const float level = main() != nullptr ? ctx.meters.get (main(), MeterPoint::Output, side) : 0.0f;
        const float db = level > 1.0e-6f ? 20.0f * std::log10 (level) : -200.0f;
        for (int row = 0; row < 12; ++row)
        {
            const float y = kMeterY0 + kMeterStep * (float) row;
            led (g, x, y, 7.0f, meterColour (row, true), db >= kMeterDb[row]);
            if (side == 0)
                text (kMeterText[row], 1344.0f, y, 36.0f, 11.0f, true);
        }
    }
    text ("L", 1304.0f, 1296.0f, 14.0f, 14.0f, true);
    text ("R", 1384.0f, 1296.0f, 14.0f, 14.0f, true);
    g.setColour (juce::Colour (0xff22242a));
    g.fillEllipse (1337.0f, 1288.0f, 14.0f, 14.0f);

    // ---- the faders ------------------------------------------------------------------------------
    box ({ 98.0f, 1310.0f, 908.0f, 480.0f }, kGrey, 4.0f);
    box ({ 1008.0f, 1300.0f, 430.0f, 440.0f }, kGrey, 4.0f);
    for (int i = 0; i < kChannels; ++i)
        g.setColour (kGreyDark), g.drawVerticalLine (98 + kStripPitch * i, 1312.0f, 1788.0f);
    static const std::pair<const char*, float> scale[] = { { "10", 1351.0f }, { "", 1387.0f }, { "0", 1424.0f }, { "", 1460.0f }, { "10", 1497.0f }, { "15", 1534.0f },
                                                            { "20", 1569.0f }, { "25", 1604.0f }, { "30", 1630.0f }, { "40", 1657.0f }, { "60", 1675.0f }, { "oo", 1692.0f } };
    const auto legend = [&] (float slotX)
    {
        for (const auto& [label, y] : scale)
        {
            g.setColour (kSilkOnGrey);
            g.drawHorizontalLine ((int) y, slotX - 34.0f, slotX - 24.0f);
            g.drawHorizontalLine ((int) y, slotX + 24.0f, slotX + 34.0f);
            if (*label != 0)
                hw::silk (g, { slotX - 62.0f, y - 8.0f, 26.0f, 16.0f }, label, darkLook, 11.0f, juce::Justification::centredRight);
        }
    };
    for (int i = 0; i < kChannels; ++i)
    {
        const float kx = (float) (kStripPitch * i), slot = 146.0f + kx;
        legend (slot);
        auto* s = channel (i);
        const bool muted = s != nullptr && s->sends[2].enabled.load();
        // CLIP lights a little under full scale on the signal after the gain.
        const bool clip = s != nullptr && ctx.meters.get (s, MeterPoint::Trim, 0) >= 0.89f;
        text ("MUTE", 216.0f + kx, 1320.0f, 40.0f, 10.0f, true);
        led (g, 212.0f + kx, 1342.0f, 6.0f, juce::Colour (0xffe2b21a), muted);
        led (g, 212.0f + kx, 1370.0f, 6.0f, juce::Colour (0xffe5352c), clip);
        text ("CLIP", 216.0f + kx, 1390.0f, 40.0f, 10.0f, true);
        box ({ 104.0f + kx, 1743.0f, 136.0f, 40.0f }, juce::Colour (0xffd3d6d7), 3.0f);
        g.setColour (kSilkOnGrey);
        g.drawRoundedRectangle (104.0f + kx, 1743.0f, 136.0f, 40.0f, 3.0f, 1.4f);
        text (i == 4 ? "5/6" : i == 5 ? "7/8" : juce::String (i + 1), 190.0f + kx, 1764.0f, 60.0f, 20.0f, true);
        g.setColour (juce::Colour (0xff1b1c1d));
        g.fillEllipse (212.0f + kx, 1690.0f, 18.0f, 18.0f);
    }
    for (const float slot : { 1098.0f, 1305.0f, 1385.0f })
        legend (slot);
    g.setColour (juce::Colour (0xff1d1e20));
    g.drawVerticalLine (1195, 1325.0f, 1730.0f);
    text ("3-4", 1098.0f, 1294.0f, 60.0f, 20.0f, true);
    box ({ 1010.0f, 1740.0f, 428.0f, 46.0f }, juce::Colour (0xff17181a), 5.0f);
    text ("ALT 3-4", 1098.0f, 1764.0f, 160.0f, 24.0f);
    text ("MAIN MIX", 1345.0f, 1764.0f, 160.0f, 24.0f);
}

} // namespace livedaw::ui
