// The PRO2's panel. Laid out from the maker's press render of the control centre (its perspective taken
// out by hand) and the Owner's Manual's control surface pages (DOC02-DL2, pp.18-19, 237-282); the tray
// beside the screen is narrower here than on the console, to give the channel strip room. Sidecar's own
// drawing: no maker's logo or artwork.

#include "ui/Pro2Surface.h"

#include "engine/MixerEngine.h"
#include "engine/Models.h"
#include "engine/Show.h"
#include "ui/HardwareLook.h"
#include "ui/MeterBank.h"

#include <cmath>

namespace livedaw::ui
{

using namespace universal;
using namespace pro2;

namespace
{
    // The panel's colours, from the render.
    const juce::Colour kPanel (0xff3f6183), kPanelLow (0xff35536f), kTray (0xff15171a), kLine (0xffb4c4d4), kPlate (0xffe6eaee);
    const juce::Colour kSilk (0xffe4ebf2), kSilkDark (0xff1c2530);
    const juce::Colour kRed (0xffd9342c), kYellow (0xfff2ca1c), kGreen (0xff23933f), kBlue (0xff2f77cc), kWhite (0xffdcdfe0), kCream (0xffd9d6ca), kOrange (0xfff09a26), kGrey (0xff8f969c), kPink (0xffe7b4a6);
    const juce::Colour kLcdBody (0xff1b1d21);

    constexpr int kTopBar = 40;
    constexpr int kLcdY = 622, kMuteY = 668, kSoloY = 694, kFaderY = 728, kFaderH = 344;
    constexpr int kScreenX = 444, kScreenY = 56, kScreenW = 652, kScreenH = 489;
    constexpr int kAx = 1124, kMx = 1344, kBx = 1456, kCx = 1814;

    int bayCx (Bay bay, int i)
    {
        switch (bay)
        {
            case Bay::Input:   return 84 + i * 54;
            case Bay::Channel: return 578 + i * 54;
            case Bay::Mix:     return 1206 + i * 54;
            case Bay::Master:  break;
        }
        return 1672 + i * 52;
    }
    constexpr int kMonitorCx = 1852;

    juce::Rectangle<int> centred (int cx, int y, int w, int h) { return { cx - w / 2, y, w, h }; }
    juce::String ch (int unicode) { return juce::String::charToString ((juce::juce_wchar) unicode); }
    bool blink() { return (juce::Time::getMillisecondCounter() / 300) % 2 == 0; }

    juce::Colour textOn (juce::Colour c) { return c.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff101214) : juce::Colours::white; }

    /** The LED meters' thresholds in dBu (p.249: +18 to -36), 0 dBFS at +21 dBu as the engine has it. */
    constexpr float kMeterDbu[11] = { -36.0f, -30.0f, -24.0f, -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 15.0f, 18.0f };
    constexpr float kDbuAtFullScale = 21.0f;
} // namespace

namespace pro2
{
    //==========================================================================
    Key::Key (const juce::String& name, juce::Colour f, Shape s) : juce::Button (name), face (f), shape (s) {}

    void Key::refresh()
    {
        const bool on = isOn && isOn();
        const auto text = legend ? legend() : juce::String();
        const auto light = backlight ? backlight() : juce::Colour();
        if (on != shownOn || text != shownLegend || light != shownLight)
        {
            shownOn = on;
            shownLegend = text;
            shownLight = light;
            repaint();
        }
    }

    void Key::mouseDown (const juce::MouseEvent& e)
    {
        downAt = juce::Time::getMillisecondCounter();
        held = false;
        juce::Button::mouseDown (e);
    }

    void Key::mouseUp (const juce::MouseEvent& e)
    {
        held = juce::Time::getMillisecondCounter() - downAt > 500;
        juce::Button::mouseUp (e);
    }

    void Key::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        const auto area = getLocalBounds().toFloat();
        if (shape == Shape::Lcd)
        {
            // A black switch with a backlit LCD in its cap.
            g.setColour (juce::Colour (0xff08090a));
            g.fillRoundedRectangle (area, 3.5f);
            auto body = area.reduced (1.5f);
            if (down)
                body = body.translated (0.0f, 1.0f);
            g.setGradientFill (juce::ColourGradient (kLcdBody.brighter (0.25f), 0.0f, body.getY(), kLcdBody.darker (0.3f), 0.0f, body.getBottom(), false));
            g.fillRoundedRectangle (body, 3.0f);
            const auto lcd = body.reduced (3.5f, 3.5f);
            const bool lit = ! shownLight.isTransparent();
            const auto light = lit ? shownLight : juce::Colour (0xff10151d);
            g.setGradientFill (juce::ColourGradient (light.brighter (0.2f), 0.0f, lcd.getY(), light.darker (0.12f), 0.0f, lcd.getBottom(), false));
            g.fillRoundedRectangle (lcd, 1.5f);
            if (shownLegend.isNotEmpty())
            {
                g.setColour (lit ? textOn (light) : juce::Colour (0xff6d7f95));
                g.setFont (type::font (juce::jmin (13.0f, lcd.getHeight() * 0.42f), true));
                g.drawFittedText (shownLegend, lcd.reduced (1.5f, 0.0f).toNearestInt(), juce::Justification::centred, 2, 0.75f);
            }
            if (shownOn)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (lcd.reduced (0.5f), 1.5f, 2.0f);
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.drawRoundedRectangle (lcd.reduced (2.2f), 1.0f, 1.0f);
            }
            if (highlighted)
            {
                g.setColour (juce::Colours::white.withAlpha (0.07f));
                g.fillRoundedRectangle (body, 3.0f);
            }
            return;
        }
        if (shape == Shape::Round)
        {
            const float d = juce::jmin (area.getWidth(), area.getHeight()) - 1.0f;
            const auto dot = juce::Rectangle<float> (d, d).withCentre (area.getCentre());
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillEllipse (dot.translated (0.8f, 1.2f));
            const auto c = shownOn ? juce::Colour (0xfff8f8f2) : down ? kGrey.darker (0.2f) : juce::Colour (0xffc4c8cc);
            g.setGradientFill (juce::ColourGradient (c.brighter (0.2f), dot.getX(), dot.getY(), c.darker (0.3f), dot.getRight(), dot.getBottom(), false));
            g.fillEllipse (dot);
            if (shownOn)
            {
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.drawEllipse (dot.expanded (1.5f), 1.5f);
            }
            return;
        }
        if (shape != Shape::Cap)
        {
            // A cream arrow key.
            juce::Path p;
            const auto r = area.reduced (1.5f);
            if (shape == Shape::Left)
                p.addTriangle (r.getX(), r.getCentreY(), r.getRight(), r.getY(), r.getRight(), r.getBottom());
            else if (shape == Shape::Right)
                p.addTriangle (r.getRight(), r.getCentreY(), r.getX(), r.getY(), r.getX(), r.getBottom());
            else if (shape == Shape::Up)
                p.addTriangle (r.getCentreX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getBottom());
            else
                p.addTriangle (r.getCentreX(), r.getBottom(), r.getX(), r.getY(), r.getRight(), r.getY());
            p = p.createPathWithRoundedCorners (2.5f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillPath (p, juce::AffineTransform::translation (0.8f, 1.5f));
            const auto c = shownOn ? juce::Colour (0xfffdf6c8) : down ? face.darker (0.25f) : face;
            g.setGradientFill (juce::ColourGradient (c.brighter (0.15f), 0.0f, r.getY(), c.darker (0.2f), 0.0f, r.getBottom(), false));
            g.fillPath (p);
            if (highlighted)
            {
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.fillPath (p);
            }
            return;
        }
        // A cap that lights in its colour.
        const float radius = 2.5f;
        g.setColour (juce::Colour (0xff0c0d0f));
        g.fillRoundedRectangle (area, radius);
        auto body = area.reduced (1.2f);
        if (down)
            body = body.translated (0.0f, 0.8f);
        const auto c = shownOn ? face.brighter (0.2f) : face.darker (0.5f).withMultipliedSaturation (0.7f);
        if (shownOn)
        {
            g.setColour (face.withAlpha (0.35f));
            g.fillRoundedRectangle (area.expanded (2.0f), radius + 2.0f);
        }
        g.setGradientFill (juce::ColourGradient (c.brighter (0.25f), 0.0f, body.getY(), c.darker (0.25f), 0.0f, body.getBottom(), false));
        g.fillRoundedRectangle (body, radius);
        if (glyph.isNotEmpty())
        {
            g.setColour (textOn (c).withAlpha (shownOn ? 1.0f : 0.8f));
            g.setFont (type::font (juce::jlimit (6.5f, 11.0f, body.getHeight() * 0.62f), true));
            g.drawText (glyph, body, juce::Justification::centred, false);
        }
        if (highlighted)
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (body, radius);
        }
    }

    //==========================================================================
    Knob::Knob (juce::Colour t) : top (t) {}

    void Knob::setBinding (KnobBinding b)
    {
        binding = std::move (b);
        shown = -1.0f;
        repaint();
    }

    void Knob::refresh()
    {
        const float v = binding.isValid() ? binding.get() : 0.5f;
        if (std::abs (v - shown) > 0.001f)
        {
            shown = v;
            repaint();
        }
    }

    void Knob::paint (juce::Graphics& g)
    {
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const auto c = area.getCentre();
        const float position = shown < 0.0f ? 0.5f : shown;
        const float angle = (position - 0.5f) * juce::MathConstants<float>::pi * 5.0f / 3.0f;

        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c.translated (1.5f, 2.5f)));
        const juce::Colour skirt (0xff1a1b1e);
        g.setGradientFill (juce::ColourGradient (skirt.brighter (0.35f), c.x - r * 0.6f, c.y - r * 0.8f, skirt.darker (0.4f), c.x + r * 0.6f, c.y + r * 0.8f, false));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        for (int i = 0; i < 24; ++i)
        {
            const float a = (float) i * juce::MathConstants<float>::twoPi / 24.0f;
            g.drawLine (juce::Line<float> (c.getPointOnCircumference (r * 0.8f, a), c.getPointOnCircumference (r, a)), 1.0f);
        }
        const float topR = r * 0.66f;
        const auto cap = binding.isValid() ? top : top.withMultipliedSaturation (0.5f).darker (0.3f);
        g.setGradientFill (juce::ColourGradient (cap.brighter (0.3f), c.x - topR * 0.5f, c.y - topR * 0.8f, cap.darker (0.3f), c.x + topR * 0.5f, c.y + topR * 0.8f, false));
        g.fillEllipse (juce::Rectangle<float> (topR * 2.0f, topR * 2.0f).withCentre (c));
        g.setColour (juce::Colour (0xfff4f4f0));
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (topR * 0.2f, angle), c.getPointOnCircumference (r * 0.98f, angle)), juce::jmax (1.5f, r * 0.14f));
    }

    void Knob::moveTo (float p)
    {
        if (binding.isValid() && binding.set)
        {
            binding.set (juce::jlimit (0.0f, 1.0f, p));
            if (onTouch)
                onTouch();
        }
    }

    void Knob::mouseDown (const juce::MouseEvent&) { dragStart = binding.isValid() ? binding.get() : 0.0f; }
    void Knob::mouseDrag (const juce::MouseEvent& e) { moveTo (dragStart - (float) e.getDistanceFromDragStartY() / 220.0f); }

    void Knob::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (binding.isValid() && binding.reset)
        {
            binding.reset();
            if (onTouch)
                onTouch();
        }
    }

    void Knob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (binding.isValid())
        {
            const float step = binding.steps > 1.0f ? 1.0f / (binding.steps - 1.0f) : 0.01f;
            moveTo (binding.get() + (w.deltaY > 0 ? step : -step));
        }
    }

    //==========================================================================
    Fader::Fader (const FaderLaw& p, juce::Colour b) : printed (p), law (p), band (b) {}

    void Fader::bind (std::atomic<float>* v, const FaderLaw& l)
    {
        value = v;
        law = l;
        binding = {};
        position = -1.0;
        refresh();
    }

    void Fader::bind (KnobBinding b)
    {
        value = nullptr;
        binding = std::move (b);
        position = -1.0;
        refresh();
    }

    void Fader::refresh()
    {
        const double p = value != nullptr ? (double) law.toPosition (value->load()) : binding.isValid() ? (double) binding.get() : 0.0;
        if (std::abs (p - position) > 0.0005)
        {
            position = p;
            repaint();
        }
    }

    void Fader::setPositionFromUser (double p)
    {
        if (! isBound())
            return;
        position = juce::jlimit (0.0, 1.0, p);
        if (value != nullptr)
            *value = law.toDb ((float) position);
        else if (binding.set)
            binding.set ((float) position);
        repaint();
    }

    juce::Rectangle<float> Fader::track() const noexcept
    {
        return getLocalBounds().toFloat().withTrimmedTop (22.0f).withTrimmedBottom (26.0f);
    }

    void Fader::paint (juce::Graphics& g)
    {
        const auto t = track();
        const float w = (float) getWidth(), cx = w * 0.42f;
        // The scale: lines across the strip, the figures at their right.
        static const std::pair<float, const char*> marks[] = { { 10.0f, "10" }, { 5.0f, "5" }, { 0.0f, "0" }, { -5.0f, "5" }, { -10.0f, "10" }, { -20.0f, "20" }, { -30.0f, "30" }, { -40.0f, "40" }, { -50.0f, "50" }, { -70.0f, "70" } };
        g.setFont (type::font (8.5f));
        for (const auto& [db, label] : marks)
        {
            const float y = t.getBottom() - t.getHeight() * printed.toPosition (db);
            const bool unity = std::abs (db) < 0.01f;
            g.setColour (kSilk.withAlpha (unity ? 0.95f : 0.6f));
            g.fillRect (3.0f, y - (unity ? 0.8f : 0.5f), w - 20.0f, unity ? 1.6f : 1.0f);
            g.setColour (kSilk.withAlpha (0.9f));
            g.drawText (label, juce::Rectangle<float> (w - 17.0f, y - 6.0f, 16.0f, 12.0f), juce::Justification::centredLeft, false);
        }
        g.setColour (kSilk.withAlpha (0.6f));
        g.fillRect (3.0f, t.getBottom() - 0.5f, w - 20.0f, 1.0f);
        g.setColour (kSilk.withAlpha (0.9f));
        g.drawText (ch (0x221e), juce::Rectangle<float> (w - 17.0f, t.getBottom() - 6.0f, 16.0f, 12.0f), juce::Justification::centredLeft, false);

        // The slot with its screws, then the cap: black, a band of its colour across it.
        g.setColour (juce::Colour (0xff070809));
        g.fillRoundedRectangle (juce::Rectangle<float> (cx - 2.5f, t.getY() - 10.0f, 5.0f, t.getHeight() + 20.0f), 2.0f);
        for (float y : { t.getY() - 16.0f, t.getBottom() + 16.0f })
        {
            g.setColour (juce::Colour (0xffb9bcbe));
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ cx, y }));
            g.setColour (juce::Colour (0xff3a3d40));
            g.fillEllipse (juce::Rectangle<float> (2.4f, 2.4f).withCentre ({ cx, y }));
        }
        const float y = t.getBottom() - t.getHeight() * (float) juce::jmax (0.0, position);
        const juce::Rectangle<float> cap (cx - 11.0f, y - 19.0f, 22.0f, 38.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (cap.translated (2.0f, 3.0f), 4.0f);
        const juce::Colour body = isBound() ? juce::Colour (0xff222427) : juce::Colour (0xff34373b);
        g.setGradientFill (juce::ColourGradient (body.brighter (0.35f), cap.getX(), 0.0f, body.darker (0.3f), cap.getRight(), 0.0f, false));
        g.fillRoundedRectangle (cap, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        for (float dy : { -14.0f, -10.5f, 10.5f, 14.0f })
            g.fillRect (cap.getX() + 2.0f, y + dy - 0.5f, cap.getWidth() - 4.0f, 1.0f);
        g.setColour (band);
        g.fillRoundedRectangle (juce::Rectangle<float> (cap.getX() + 2.0f, y - 5.0f, cap.getWidth() - 4.0f, 10.0f), 4.5f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (cap.getX() + 3.0f, y - 0.6f, cap.getWidth() - 6.0f, 1.2f);
    }

    void Fader::mouseDown (const juce::MouseEvent&) { dragStart = juce::jmax (0.0, position); }

    void Fader::mouseDrag (const juce::MouseEvent& e)
    {
        setPositionFromUser (dragStart - (double) e.getDistanceFromDragStartY() / (double) track().getHeight());
    }

    void Fader::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (value != nullptr)
            setPositionFromUser ((double) law.toPosition (0.0f));
        else if (binding.isValid() && binding.reset)
        {
            binding.reset();
            refresh();
        }
    }

    void Fader::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        setPositionFromUser (juce::jmax (0.0, position) + (w.deltaY > 0 ? 0.0125 : -0.0125));
    }

    //==========================================================================
    void Meter::refresh()
    {
        const float db = juce::Decibels::gainToDecibels (level ? level() : 0.0f, -120.0f) + kDbuAtFullScale;
        int n = 0;
        for (float threshold : kMeterDbu)
            n += db >= threshold ? 1 : 0;
        if (n != lit)
        {
            lit = n;
            repaint();
        }
    }

    void Meter::paint (juce::Graphics& g)
    {
        const auto area = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0d0f11));
        g.fillRect (area);
        const float h = (area.getHeight() - 2.0f) / 11.0f;
        for (int i = 0; i < 11; ++i)
        {
            const auto c = i == 10 ? kRed : i >= 8 ? kYellow : juce::Colour (0xff4fd06a);
            g.setColour (i < lit ? c.brighter (0.2f) : c.darker (0.72f).withMultipliedSaturation (0.6f));
            g.fillRect (area.getX() + 1.0f, area.getBottom() - 1.0f - (float) (i + 1) * h + 0.7f, area.getWidth() - 2.0f, h - 1.2f);
        }
    }

    //==========================================================================
    bool MixSend::isPre() const noexcept
    {
        return preFlag != nullptr ? preFlag->load() : mode != nullptr && mode->load() == (int32_t) SendMode::Pre;
    }

    bool MixSend::isGroup() const noexcept { return mode != nullptr && mode->load() == (int32_t) SendMode::Group; }

    void MixSend::setPre (bool pre) const noexcept
    {
        if (preFlag != nullptr)
            *preFlag = pre;
        else if (mode != nullptr && ! isGroup())
            *mode = (int32_t) (pre ? SendMode::Pre : SendMode::Post);
    }

    MixSend sendTo (Strip& from, Strip& mix)
    {
        MixSend s;
        if (&from == &mix || from.getKind() == StripKind::Matrix)
            return s;
        if (mix.getKind() == StripKind::Bus && from.isInput())
        {
            auto& send = from.sends[(size_t) mix.getId()];
            s.on = &send.enabled;
            s.mode = &send.mode;
            s.level = &send.levelDb;
        }
        else if (mix.getKind() == StripKind::Matrix)
        {
            auto& send = from.matrix[(size_t) mix.getId()];
            s.on = &send.on;
            s.preFlag = &send.pre;
            s.level = &send.levelDb;
        }
        return s;
    }

    KnobBinding bindHighPass (Strip& s) { return bindFreq ("HIGH PASS", s.params.filter().lowCutFreq, 10.0f, 400.0f, 80.0f); }
    KnobBinding bindLowPass (Strip& s) { return bindFreq ("LOW PASS", s.params.filter().highCutFreq, 2000.0f, 20000.0f, 20000.0f); }

    // The slopes are the engine's lists: low cut 6, 12, 18, 24; high cut 6, 12.
    bool steepSlope (const Strip& s, bool lowPass)
    {
        const auto& f = s.params.filter();
        return lowPass ? f.highCutSlope.load() >= 1 : f.lowCutSlope.load() >= 3;
    }

    void setSteepSlope (Strip& s, bool lowPass, bool steep)
    {
        auto& f = s.params.filter();
        if (lowPass)
            f.highCutSlope = steep ? 1 : 0;
        else
            f.lowCutSlope = steep ? 3 : 1;
    }

    KnobBinding bindTrim (Strip& s)
    {
        return s.isInput() ? bindLinear ("TRIM", s.params.input.trimDb, -40.0f, 20.0f, " dB", 1, 0.0f)
                           : bindLinear ("BUS TRIM", s.params.input.trimDb, -12.0f, 6.0f, " dB", 1, 0.0f);
    }

    KnobBinding bindDelayMs (Strip& s)
    {
        auto b = bindLinear ("DELAY", s.params.input.delay, 0.0f, s.isInput() ? 50.0f : 500.0f, " ms", 1, 0.0f);
        b.set = [&p = s.params.input, set = b.set] (float v)
        {
            p.delayMode = 2; // ms
            set (v);
        };
        return b;
    }

    KnobBinding bindSendLevel (const juce::String& label, std::atomic<float>& levelDb)
    {
        KnobBinding b;
        b.label = label;
        b.get = [&levelDb] { return fader::dbToPos (juce::jmin (6.0f, levelDb.load()) + 4.0f); };
        b.set = [&levelDb] (float v)
        {
            const float db = fader::posToDb (juce::jlimit (0.0f, 1.0f, v)) - 4.0f;
            levelDb = v <= 0.0f || db <= fader::kMinDb ? FaderLaw::silenceDb : juce::jmin (6.0f, db);
        };
        b.text = [&levelDb] { return fader::text (levelDb.load()); };
        b.reset = [&levelDb] { levelDb = 0.0f; };
        return b;
    }

    // WIDTH as the band pass's Q (livedaw_dsp::midas_pro::SC_WIDTH_Q).
    constexpr float kWidthQ[3] = { 4.318f, 1.414f, 0.667f };

    int sideChainWidth (const DynSlotParams& d)
    {
        const float q = d.scQ.load();
        int best = 0;
        for (int i = 1; i < 3; ++i)
            if (std::abs (kWidthQ[i] - q) < std::abs (kWidthQ[best] - q))
                best = i;
        return best;
    }

    void setSideChainWidth (DynSlotParams& d, int width) { d.scQ = kWidthQ[juce::jlimit (0, 2, width)]; }
    bool sideChainIn (const DynSlotParams& d) { return d.scFilter.load() != 0; }

    void setSideChainIn (DynSlotParams& d, bool in)
    {
        if (in && d.scFreq.load() < 50.0f)
            d.scFreq = 50.0f;
        d.scFilter = in ? 3 : 0; // the list's BP
    }

    juce::StringArray compModes (const Strip& s)
    {
        const auto& m = models::model (models::Dynamics, s.params.dynamics().model.load());
        const int i = m.find ("mode");
        return i >= 0 ? m.params[(size_t) i].choices : juce::StringArray();
    }

    KnobBinding bindDyn (Strip& s, bool gate, const juce::String& id)
    {
        auto& d = gate ? s.params.gate() : s.params.dynamics();
        const auto& m = models::model (models::Dynamics, d.model.load());
        const int i = m.find (id);
        return i >= 0 ? bindModelParam (d.values[i], m.params[(size_t) i]) : KnobBinding {};
    }

    KnobBinding bindEqParam (Strip& s, const juce::String& id)
    {
        auto& eq = s.params.eq();
        const auto& m = models::model (s.isOutput() ? models::BusEq : models::ChannelEq, eq.model.load());
        const int i = m.find (id);
        return i >= 0 ? bindModelParam (eq.values[i], m.params[(size_t) i]) : KnobBinding {};
    }

    juce::StringArray eqBands (const Strip& s)
    {
        if (s.isInput())
            return { "t", "h", "l", "b" };
        if (Pro2Surface::hasGeq (s))
            return {};
        return { "6", "5", "4", "3", "2", "1" };
    }

    juce::String eqBandName (const Strip& s, int band)
    {
        static const char* const input[] = { "treble", "hi mid", "lo mid", "bass" };
        if (s.isInput())
            return input[juce::jlimit (0, 3, band)];
        return "band " + juce::String (6 - juce::jlimit (0, 5, band));
    }
} // namespace pro2

//==============================================================================
Key& Pro2Surface::key (const juce::String& name, juce::Colour face, Key::Shape shape, juce::Rectangle<int> r, const juce::String& glyph)
{
    auto k = std::make_unique<Key> (name, face, shape);
    k->setBounds (r);
    k->setGlyph (glyph);
    addAndMakeVisible (*k);
    keys.emplace_back (name, std::move (k));
    return *keys.back().second;
}

Key* Pro2Surface::findKey (const juce::String& name) const
{
    for (const auto& [n, k] : keys)
        if (n == name)
            return k.get();
    return nullptr;
}

Knob& Pro2Surface::knob (const juce::String& name, juce::Colour top, juce::Rectangle<int> r)
{
    auto k = std::make_unique<Knob> (top);
    k->setBounds (r);
    addAndMakeVisible (*k);
    knobs.emplace_back (name, std::move (k));
    return *knobs.back().second;
}

Knob* Pro2Surface::findKnob (const juce::String& name) const
{
    for (const auto& [n, k] : knobs)
        if (n == name)
            return k.get();
    return nullptr;
}

Meter& Pro2Surface::meter (juce::Rectangle<int> r, std::function<float()> level)
{
    auto m = std::make_unique<Meter>();
    m->setBounds (r);
    m->level = std::move (level);
    m->setInterceptsMouseClicks (false, false);
    addAndMakeVisible (*m);
    meters.push_back (std::move (m));
    return *meters.back();
}

void Pro2Surface::silk (float x, float y, float w, float h, const juce::String& text, float size, juce::Colour c, juce::Justification j, bool bold)
{
    silks.push_back ({ { x, y, w, h }, text, size, c, j, bold });
}

void Pro2Surface::plate (juce::Rectangle<float> r, juce::Colour c) { plates.push_back ({ r, c, false }); }
void Pro2Surface::outline (juce::Rectangle<float> r) { plates.push_back ({ r, kLine, true }); }

Fader& Pro2Surface::getFader (Bay bay, int i)
{
    auto& list = faders[(size_t) bay];
    return *list[(size_t) juce::jlimit (0, (int) list.size() - 1, i)];
}

Pro2Surface::Pro2Surface (UiContext& c)
    : ctx (c), theme (Theme::dark()), universalKey ("UNIVERSAL MIXER", theme.accent, theme), screen (c, *this)
{
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
    addAndMakeVisible (screen);

    // The bays: LCD select, MUTE, SOLO, the meter and the fader of each strip.
    for (int i = 0; i < 8; ++i)
    {
        buildStrip (Bay::Input, i, bayCx (Bay::Input, i), true);
        buildStrip (Bay::Channel, i, bayCx (Bay::Channel, i), true);
        buildStrip (Bay::Mix, i, bayCx (Bay::Mix, i), false);
    }
    for (int i = 0; i < 3; ++i)
        buildStrip (Bay::Master, i, bayCx (Bay::Master, i), true);

    // The bays' scroll keys, beside their LCD buttons (p.43).
    const auto scrollKeys = [this] (Bay bay, const char* tag, int left, int right)
    {
        auto& down = key (juce::String ("SCROLL ") + tag + "-", kCream, Key::Shape::Left, { left, kLcdY + 6, 24, 26 });
        down.onClick = [this, bay] { scroll (bay, -1); };
        auto& up = key (juce::String ("SCROLL ") + tag + "+", kCream, Key::Shape::Right, { right, kLcdY + 6, 24, 26 });
        up.onClick = [this, bay] { scroll (bay, 1); };
        silk ((float) left - 4.0f, (float) kLcdY + 33.0f, 32.0f, 8.0f, "scroll", 7.0f, kSilk);
        silk ((float) right - 4.0f, (float) kLcdY + 33.0f, 32.0f, 8.0f, "scroll", 7.0f, kSilk);
    };
    scrollKeys (Bay::Input, "L", 28, 492);
    scrollKeys (Bay::Channel, "C", 522, 986);
    scrollKeys (Bay::Mix, "M", 1154, 1612);

    buildDetail();
    buildOutputs();
    buildComms();
    buildNavigation();
    buildMasters();

    changed();
    startTimerHz (30);
}

Pro2Surface::~Pro2Surface() = default;

//==============================================================================
void Pro2Surface::buildStrip (Bay bay, int i, int cx, bool withMeter)
{
    const auto tag = juce::String (bay == Bay::Input ? "L" : bay == Bay::Channel ? "C" : bay == Bay::Mix ? "M" : "B") + juce::String (i);
    const bool master = bay == Bay::Master;
    auto& lcd = key ("LCD " + tag, kLcdBody, Key::Shape::Lcd, centred (cx, kLcdY, master ? 48 : 50, 38));
    auto* lcdKey = &lcd;
    lcd.legend = [this, bay, i]() -> juce::String
    {
        if (bay == Bay::Mix && mixMode == MixMode::Vca)
            return groupName (i);
        if (bay == Bay::Mix && mixMode == MixMode::Geq)
        {
            auto* mix = selectedMix();
            if (mix == nullptr || ! hasGeq (*mix))
                return {};
            const auto& m = models::model (models::BusEq, mix->params.eq().model.load());
            const int band = 11 + geqFirst + i;
            return band < (int) m.params.size() ? m.params[(size_t) band].label : juce::String();
        }
        auto* s = stripAt (bay, i);
        return s != nullptr ? s->getName() : juce::String();
    };
    lcd.backlight = [this, bay, i]() -> juce::Colour
    {
        if (bay == Bay::Mix && mixMode == MixMode::Vca)
            return consoleColour (ctx.engine.getDca (i).colour.load());
        if (bay == Bay::Mix && mixMode == MixMode::Geq)
            return selectedMix() != nullptr && hasGeq (*selectedMix()) ? juce::Colour (0xff9fd2ff) : juce::Colour();
        auto* s = stripAt (bay, i);
        return s != nullptr ? stripDisplayColour (*s) : juce::Colour();
    };
    lcd.isOn = [this, bay, i]
    {
        if (bay == Bay::Mix && mixMode == MixMode::Vca)
            return assigning == i ? blink() : group == i;
        auto* s = stripAt (bay, i);
        if (s == nullptr)
            return false;
        if (assigning >= 0)
            return isMember (assigning, *s);
        return s == selected() || (bay == Bay::Master && s == selectedMix());
    };
    lcd.onClick = [this, bay, i, lcdKey]
    {
        const bool held = lcdKey->takeHeld();
        if (bay == Bay::Mix && mixMode == MixMode::Vca)
        {
            // Held: the group's members are set with the channels' LCD buttons, until it is pressed again (p.74).
            if (held || assigning == i)
                setAssigning (assigning == i ? -1 : i);
            else
                selectGroup (i);
            return;
        }
        if (bay == Bay::Mix && mixMode == MixMode::Geq)
            return;
        pressSelect (stripAt (bay, i));
    };

    const auto dcaShown = [this, bay] { return bay == Bay::Mix && mixMode == MixMode::Vca; };
    auto& mute = key ("MUTE " + tag, kRed, Key::Shape::Cap, { cx - (withMeter ? 20 : 12), kMuteY, 24, 16 }, "MUTE");
    mute.isOn = [this, bay, i, dcaShown]
    {
        if (dcaShown())
            return ctx.engine.getDca (i).mute.load();
        auto* s = stripAt (bay, i);
        return s != nullptr && s->params.mute.load();
    };
    mute.onClick = [this, bay, i, dcaShown]
    {
        if (dcaShown())
            ctx.engine.getDca (i).mute = ! ctx.engine.getDca (i).mute.load();
        else if (auto* s = stripAt (bay, i))
            s->params.mute = ! s->params.mute.load();
    };
    auto& solo = key ("SOLO " + tag, kYellow, Key::Shape::Cap, { cx - (withMeter ? 20 : 12), kSoloY, 24, 16 }, "SOLO");
    solo.isOn = [this, bay, i, dcaShown]
    {
        if (dcaShown())
            return ctx.engine.getDca (i).solo.load();
        auto* s = stripAt (bay, i);
        return s != nullptr && s->params.solo.load();
    };
    solo.onClick = [this, bay, i, dcaShown]
    {
        if (dcaShown())
            ctx.engine.getDca (i).solo = ! ctx.engine.getDca (i).solo.load();
        else if (auto* s = stripAt (bay, i))
            pressSolo (*s);
    };
    if (withMeter)
        meter ({ cx + 9, kMuteY, 9, 44 }, [this, bay, i]
        {
            auto* s = stripAt (bay, i);
            return s != nullptr ? juce::jmax (ctx.meters.get (s, MeterPoint::Output, 0), ctx.meters.get (s, MeterPoint::Output, 1)) : 0.0f;
        });

    const auto band = ! master ? juce::Colour (0xffe9e9e4) : i < 2 ? kRed : kYellow;
    auto f = std::make_unique<Fader> (ctx.engine.getFaderLaw ((int) StripKind::Channel), band);
    f->setBounds (centred (cx, kFaderY, 50, kFaderH));
    addAndMakeVisible (*f);
    faders[(size_t) bay].push_back (std::move (f));
    static const char* const masters[] = { "master left", "master right", "master mono" };
    silk ((float) cx - 27.0f, (float) kFaderY - 9.0f, 54.0f, 9.0f, master ? masters[i] : bay == Bay::Mix ? "mix" : "channel", 7.0f, kSilk);
}

void Pro2Surface::buildDetail()
{
    using Shape = Key::Shape;
    const int ax = kAx;
    const auto fx = [ax] (int dx) { return (float) (ax + dx); };
    const auto onSelected = [this] (std::function<void (Strip&)> f)
    {
        return [this, f]
        {
            if (auto* s = selected())
                f (*s);
        };
    };
    const auto ifSelected = [this] (std::function<bool (Strip&)> f)
    {
        return [this, f]
        {
            auto* s = selected();
            return s != nullptr && f (*s);
        };
    };
    const auto quick = [this] (const juce::String& name, juce::Rectangle<int> r, Area a)
    {
        auto& k = key (name, kGrey, Shape::Round, r);
        k.isOn = [this, a] { return area == a; };
        k.onClick = [this, a] { setArea (area == a ? Area::Overview : a); };
    };

    // direct, channel safes (pp.244, 248). The direct output's level and the safes but AUTO have no engine yet.
    outline ({ fx (0), 58.0f, 38.0f, 38.0f });
    key ("DIRECT MUTE", kRed, Shape::Cap, { ax + 5, 63, 28, 14 }, "MUTE");
    silk (fx (0), 80.0f, 38.0f, 9.0f, "direct", 7.0f, kSilk);
    outline ({ fx (42), 58.0f, 172.0f, 38.0f });
    silk (fx (42), 59.0f, 172.0f, 8.0f, "channel safes", 7.0f, kSilk, juce::Justification::centred, true);
    {
        const char* names[] = { "EQ", "DYN", "MIC", "AUTO", "MUTE", "FADER" };
        const juce::Colour faces[] = { kRed, kBlue, juce::Colour (0xff50555a), kYellow, kRed, kGreen };
        for (int i = 0; i < 6; ++i)
        {
            auto& k = key (juce::String ("SAFE ") + names[i], faces[i], Shape::Cap, { ax + 50 + (i % 3) * 54, 68 + (i / 3) * 14, 32, 11 }, names[i]);
            if (i == 3)
            {
                k.isOn = ifSelected ([] (Strip& s) { return s.params.recallSafe.load(); });
                k.onClick = onSelected ([] (Strip& s) { s.params.recallSafe = ! s.params.recallSafe.load(); });
            }
        }
    }

    // TALK, 48V, phase, the filters and the gain (pp.245-246).
    outline ({ fx (0), 100.0f, 214.0f, 78.0f });
    key ("STRIP TALK", kGreen, Shape::Cap, { ax + 5, 104, 28, 13 }, "TALK");
    auto& phantom = key ("48V", kRed, Shape::Cap, { ax + 5, 121, 28, 13 }, "48V");
    const auto sourceOf = [this] (Strip& s) -> ld::SourceParams*
    {
        return s.isInput() && s.getInputLeft() >= 0 ? ctx.engine.getSource (MixerEngine::sourceIndex (s.getInputLeft())) : nullptr;
    };
    phantom.isOn = ifSelected ([sourceOf] (Strip& s) { auto* src = sourceOf (s); return src != nullptr && src->phantom.load(); });
    phantom.onClick = onSelected ([sourceOf] (Strip& s) { if (auto* src = sourceOf (s)) src->phantom = ! src->phantom.load(); });
    auto& phase = key ("PHASE", kBlue, Shape::Cap, { ax + 5, 138, 28, 13 }, ch (0xd8));
    phase.isOn = ifSelected ([] (Strip& s) { return s.params.input.invert.load(); });
    phase.onClick = onSelected ([] (Strip& s) { s.params.input.invert = ! s.params.input.invert.load(); });
    key ("30HZ", kGrey, Shape::Round, { ax + 12, 157, 14, 14 });
    silk (fx (26), 158.0f, 14.0f, 10.0f, "30", 6.5f, kSilk, juce::Justification::centredLeft);

    knob ("HIGH PASS", kGrey, { ax + 46, 106, 36, 36 }).onTouch = [this] { setArea (Area::Config); };
    silk (fx (38), 143.0f, 52.0f, 8.0f, "high pass", 7.0f, kSilk);
    knob ("LOW PASS", kGrey, { ax + 98, 106, 36, 36 }).onTouch = [this] { setArea (Area::Config); };
    silk (fx (90), 143.0f, 52.0f, 8.0f, "low pass", 7.0f, kSilk);
    for (int lp = 0; lp < 2; ++lp)
    {
        const bool low = lp == 1;
        const int x = ax + (low ? 92 : 40);
        auto& slope = key (low ? "LP SLOPE" : "HP SLOPE", kCream, Shape::Cap, { x, 153, 23, 11 }, "SLP");
        slope.isOn = ifSelected ([low] (Strip& s) { return s.getKind() == StripKind::Channel && steepSlope (s, low); });
        slope.onClick = onSelected ([low] (Strip& s) { if (s.getKind() == StripKind::Channel) setSteepSlope (s, low, ! steepSlope (s, low)); });
        auto& in = key (low ? "LP IN" : "HP IN", kCream, Shape::Cap, { x + 25, 153, 23, 11 }, "IN");
        in.isOn = ifSelected ([low] (Strip& s) { auto& f = s.params.filter(); return s.getKind() == StripKind::Channel && (low ? f.highCut : f.lowCut).load(); });
        in.onClick = onSelected ([low] (Strip& s)
        {
            auto& flag = low ? s.params.filter().highCut : s.params.filter().lowCut;
            if (s.getKind() == StripKind::Channel)
                flag = ! flag.load();
        });
        silk ((float) x, 165.0f, 48.0f, 8.0f, low ? "6/12  slope  in" : "12/24  slope  in", 5.8f, kSilk);
    }
    knob ("GAIN", kGrey, { ax + 148, 104, 44, 44 }).onTouch = [this] { setArea (Area::Config); };
    silk (fx (140), 149.0f, 60.0f, 8.0f, "gain", 7.0f, kSilk);
    auto& swap = key ("GAIN SWAP", kCream, Shape::Cap, { ax + 180, 158, 28, 12 }, "SWAP");
    swap.isOn = [this] { return gainSwap; };
    swap.onClick = [this] { setGainSwap (! gainSwap); };
    silk (fx (146), 160.0f, 32.0f, 8.0f, "gain", 6.5f, kSilk, juce::Justification::centredRight);

    // processing order: EQ, insert, dynamics, or the other way round (p.247).
    outline ({ fx (0), 182.0f, 214.0f, 15.0f });
    silk (fx (6), 183.0f, 150.0f, 13.0f, "processing order", 7.5f, kSilk, juce::Justification::centredLeft, true);
    auto& order = key ("C/O", kBlue, Shape::Cap, { ax + 182, 184, 28, 11 }, "C/O");
    order.isOn = ifSelected ([] (Strip& s) { return s.params.processOrder.load() == 1; });
    order.onClick = onSelected ([] (Strip& s) { s.params.processOrder = s.params.processOrder.load() == 1 ? 0 : 1; });

    // The D zone (pp.249-255): the compressor's and the gate's switches on the white column, the shared knobs.
    plate ({ fx (0), 201.0f, 38.0f, 131.0f }, kPlate);
    plate ({ fx (42), 201.0f, 172.0f, 131.0f }, kPlate);
    auto& compOn = key ("COMP ON", kBlue, Shape::Cap, { ax + 5, 206, 28, 13 }, "ON");
    compOn.isOn = ifSelected ([] (Strip& s) { return s.getKind() != StripKind::Aux && s.params.dynamics().on.load(); });
    compOn.onClick = onSelected ([this] (Strip& s)
    {
        if (s.getKind() != StripKind::Aux)
            s.params.dynamics().on = ! s.params.dynamics().on.load();
        setDynamicsShowsGate (false);
    });
    silk (fx (0), 220.0f, 38.0f, 8.0f, "comp", 7.0f, kSilkDark);
    quick ("QA COMP", { ax + 12, 242, 14, 14 }, Area::Comp);
    auto& gateOn = key ("GATE ON", kBlue, Shape::Cap, { ax + 5, 272, 28, 13 }, "ON");
    gateOn.isOn = ifSelected ([] (Strip& s) { return s.getKind() == StripKind::Channel && s.params.gate().on.load(); });
    gateOn.onClick = onSelected ([this] (Strip& s)
    {
        if (s.getKind() != StripKind::Channel)
            return;
        s.params.gate().on = ! s.params.gate().on.load();
        setDynamicsShowsGate (true);
    });
    silk (fx (0), 286.0f, 38.0f, 8.0f, "gate", 7.0f, kSilkDark);
    quick ("QA GATE", { ax + 12, 308, 14, 14 }, Area::Gate);

    const auto dynTouch = [this] { setArea (showGate ? Area::Gate : Area::Comp); };
    knob ("ATTACK", kGreen, { ax + 50, 214, 32, 32 }).onTouch = dynTouch;
    silk (fx (44), 247.0f, 44.0f, 8.0f, "attack", 7.0f, kSilkDark);
    knob ("RELEASE", kGreen, { ax + 98, 205, 32, 32 }).onTouch = dynTouch;
    silk (fx (92), 238.0f, 44.0f, 8.0f, "release", 7.0f, kSilkDark);
    knob ("HOLD", kGreen, { ax + 146, 216, 30, 30 }).onTouch = dynTouch;
    silk (fx (140), 247.0f, 42.0f, 8.0f, "hold", 7.0f, kSilkDark);
    knob ("THRESHOLD", juce::Colour (0xff2c3fa8), { ax + 96, 250, 36, 36 }).onTouch = dynTouch;
    silk (fx (88), 287.0f, 52.0f, 8.0f, "threshold", 7.0f, kSilkDark);
    knob ("RATIO", juce::Colour (0xff2c3fa8), { ax + 50, 262, 32, 32 }).onTouch = dynTouch;
    silk (fx (42), 295.0f, 48.0f, 8.0f, "ratio/range", 6.5f, kSilkDark);
    knob ("MAKE UP", kRed, { ax + 142, 268, 30, 30 }).onTouch = dynTouch;
    silk (fx (134), 299.0f, 46.0f, 8.0f, "make up", 7.0f, kSilkDark);
    silk (fx (140), 202.0f, 44.0f, 8.0f, "dynamics", 7.0f, kSilkDark, juce::Justification::centred, true);
    auto& mode = key ("MODE", kCream, Shape::Cap, { ax + 48, 314, 30, 13 }, "MODE");
    mode.onClick = onSelected ([this] (Strip& s)
    {
        if (s.getKind() == StripKind::Aux)
            return;
        auto b = bindDyn (s, false, "mode");
        const int n = compModes (s).size();
        if (b.isValid() && n > 1)
            b.set ((float) ((juce::roundToInt (b.get() * (float) (n - 1)) + 1) % n) / (float) (n - 1));
        setDynamicsShowsGate (false);
    });
    auto& knee = key ("KNEE", kCream, Shape::Cap, { ax + 82, 314, 30, 13 }, "KNEE");
    knee.onClick = onSelected ([this] (Strip& s)
    {
        if (s.getKind() == StripKind::Aux)
            return;
        auto b = bindDyn (s, false, "knee");
        if (b.isValid())
            b.set ((float) ((juce::roundToInt (b.get() * 2.0f) + 1) % 3) / 2.0f);
        setDynamicsShowsGate (false);
    });
    auto& dynUp = key ("DYN UP", kCream, Shape::Up, { ax + 186, 208, 22, 18 });
    dynUp.isOn = [this] { return ! showGate; };
    dynUp.onClick = [this] { setDynamicsShowsGate (false); };
    silk (fx (180), 228.0f, 34.0f, 8.0f, "comp", 7.0f, kSilkDark);
    silk (fx (180), 298.0f, 34.0f, 8.0f, "gate", 7.0f, kSilkDark);
    auto& dynDown = key ("DYN DOWN", kCream, Shape::Down, { ax + 186, 308, 22, 18 });
    dynDown.isOn = [this] { return showGate; };
    dynDown.onClick = [this] { setDynamicsShowsGate (true); };

    // The insert's switch and the side chain filter (pp.255-256).
    outline ({ fx (0), 336.0f, 38.0f, 32.0f });
    auto& ins = key ("INS", kBlue, Shape::Cap, { ax + 5, 339, 28, 12 }, "INS");
    ins.isOn = ifSelected ([] (Strip& s) { return s.params.preInsert().on.load(); });
    ins.onClick = onSelected ([] (Strip& s) { s.params.preInsert().on = ! s.params.preInsert().on.load(); });
    quick ("QA INS", { ax + 13, 354, 12, 12 }, Area::Insert);
    outline ({ fx (42), 336.0f, 172.0f, 32.0f });
    silk (fx (46), 337.0f, 62.0f, 9.0f, "side chain", 7.5f, kSilk, juce::Justification::centredLeft, true);
    const auto slotOf = [this] (Strip& s) -> DynSlotParams& { return showGate && s.getKind() == StripKind::Channel ? s.params.gate() : s.params.dynamics(); };
    auto& scIn = key ("SC IN", kBlue, Shape::Cap, { ax + 112, 338, 24, 11 }, "IN");
    scIn.isOn = ifSelected ([slotOf] (Strip& s) { return sideChainIn (slotOf (s)); });
    scIn.onClick = onSelected ([slotOf] (Strip& s) { setSideChainIn (slotOf (s), ! sideChainIn (slotOf (s))); });
    key ("SC LISTEN", kYellow, Shape::Cap, { ax + 48, 353, 28, 12 }, "LSTN");
    auto& width = key ("SC WIDTH", kCream, Shape::Cap, { ax + 80, 353, 28, 12 }, "WDTH");
    width.onClick = onSelected ([slotOf] (Strip& s)
    {
        auto& d = slotOf (s);
        if (sideChainIn (d)) // only enabled with the filter in (p.255)
            setSideChainWidth (d, (sideChainWidth (d) + 1) % 3);
    });
    key ("SC MENU", kCream, Shape::Cap, { ax + 112, 353, 28, 12 }, "MENU");
    knob ("SC FREQ", kGrey, { ax + 174, 338, 28, 28 });
    silk (fx (144), 356.0f, 30.0f, 8.0f, "freq", 7.0f, kSilk);

    // The E zone (pp.257-258, 275).
    plate ({ fx (0), 372.0f, 38.0f, 104.0f }, kPlate);
    plate ({ fx (42), 372.0f, 172.0f, 104.0f }, kPlate);
    auto& eqOn = key ("EQ ON", kRed, Shape::Cap, { ax + 5, 392, 28, 13 }, "EQ");
    eqOn.isOn = ifSelected ([] (Strip& s) { return s.params.eq().on.load(); });
    eqOn.onClick = onSelected ([] (Strip& s) { s.params.eq().on = ! s.params.eq().on.load(); });
    quick ("QA EQ", { ax + 12, 452, 14, 14 }, Area::Eq);
    silk (fx (96), 373.0f, 84.0f, 9.0f, "parametric EQ", 7.5f, kSilkDark, juce::Justification::centred, true);
    const auto eqTouch = [this] { setArea (Area::Eq); };
    knob ("EQ FREQ", kBlue, { ax + 54, 382, 40, 40 }).onTouch = eqTouch;
    silk (fx (54), 423.0f, 40.0f, 8.0f, "freq", 7.0f, kSilkDark);
    auto& shape = key ("SHAPE", kCream, Shape::Cap, { ax + 108, 390, 32, 13 }, "SHAPE");
    shape.onClick = onSelected ([this] (Strip& s)
    {
        const auto bands = eqBands (s);
        if (! juce::isPositiveAndBelow (eqBand, bands.size()))
            return;
        auto b = bindEqParam (s, bands[eqBand] + "s");
        if (b.isValid() && b.steps > 1.0f)
        {
            const int n = juce::roundToInt (b.steps);
            b.set ((float) ((juce::roundToInt (b.get() * (float) (n - 1)) + 1) % n) / (float) (n - 1));
        }
        setArea (Area::Eq);
    });
    knob ("EQ GAIN", kBlue, { ax + 108, 414, 40, 40 }).onTouch = eqTouch;
    silk (fx (108), 455.0f, 40.0f, 8.0f, "gain", 7.0f, kSilkDark);
    knob ("EQ WIDTH", kBlue, { ax + 54, 434, 32, 32 }).onTouch = eqTouch;
    silk (fx (50), 466.0f, 40.0f, 8.0f, "width", 7.0f, kSilkDark);
    auto& eqUp = key ("EQ UP", kCream, Shape::Up, { ax + 184, 380, 22, 18 });
    eqUp.isOn = ifSelected ([this] (Strip& s) { return s.isOutput() && eqBand == 1; }); // band 5 (p.275)
    eqUp.onClick = [this] { setEqBand (eqBand - 1); };
    auto& eqDown = key ("EQ DOWN", kCream, Shape::Down, { ax + 184, 456, 22, 18 });
    eqDown.isOn = ifSelected ([this] (Strip& s) { return s.isOutput() && eqBand == 4; }); // band 2
    eqDown.onClick = [this] { setEqBand (eqBand + 1); };

    // masters (pp.261-262). SIS has no engine (the pan is two-way).
    outline ({ fx (0), 480.0f, 38.0f, 74.0f });
    quick ("QA MASTERS", { ax + 12, 510, 14, 14 }, Area::Masters);
    outline ({ fx (42), 480.0f, 172.0f, 74.0f });
    silk (fx (84), 481.0f, 60.0f, 9.0f, "masters", 7.5f, kSilk, juce::Justification::centred, true);
    key ("SIS", kOrange, Shape::Cap, { ax + 50, 492, 28, 13 }, "SIS");
    auto& st = key ("ST", kRed, Shape::Cap, { ax + 104, 492, 28, 13 }, "ST");
    st.isOn = ifSelected ([] (Strip& s) { return s.getKind() != StripKind::Main && s.getKind() != StripKind::Matrix && s.mains[0].on.load(); });
    st.onClick = onSelected ([] (Strip& s)
    {
        if (s.getKind() != StripKind::Main && s.getKind() != StripKind::Matrix)
            s.mains[0].on = ! s.mains[0].on.load();
    });
    auto& mon = key ("MON", kYellow, Shape::Cap, { ax + 104, 512, 28, 13 }, "MON");
    mon.isOn = ifSelected ([this] (Strip& s) { return s.getKind() != StripKind::Main && s.getKind() != StripKind::Matrix && ctx.engine.getMains().size() > 1 && s.mains[1].on.load(); });
    mon.onClick = onSelected ([this] (Strip& s)
    {
        if (s.getKind() != StripKind::Main && s.getKind() != StripKind::Matrix && ctx.engine.getMains().size() > 1)
            s.mains[1].on = ! s.mains[1].on.load();
    });
    knob ("MONO LEVEL", kOrange, { ax + 48, 510, 34, 34 }).onTouch = [this] { setArea (Area::Masters); };
    silk (fx (40), 544.0f, 50.0f, 8.0f, "mono level", 6.5f, kSilk);
    knob ("PAN", kRed, { ax + 156, 494, 42, 42 }).onTouch = [this] { setArea (Area::Masters); };
    silk (fx (156), 538.0f, 42.0f, 8.0f, "pan", 7.0f, kSilk);

    // mix: eight contributions at a time, level / pan on the odd ones (p.259).
    const int mx = kMx;
    silk ((float) mx, 57.0f, 104.0f, 9.0f, "mix", 7.5f, kSilk, juce::Justification::centred, true);
    const juce::Colour tops[] = { kRed, kRed, kGreen, kGreen, kYellow, kYellow, juce::Colour (0xff2a86c8), juce::Colour (0xff2a86c8) };
    for (int k = 0; k < 8; ++k)
    {
        const int y = 68 + k * 46;
        if (k % 2 == 0)
            outline ({ (float) mx, (float) y - 2.0f, 104.0f, 92.0f });
        const auto sendOf = [this, k] (Strip& s) -> MixSend
        {
            auto* mix = mixBank < 2 ? ctx.engine.getBusInSlot (mixBank * 8 + k)
                                    : k < (int) ctx.engine.getMatrices().size() ? ctx.engine.getMatrices()[(size_t) k].get() : nullptr;
            return mix != nullptr ? sendTo (s, *mix) : MixSend {};
        };
        auto& on = key ("MIX ON " + juce::String (k + 1), kBlue, Shape::Cap, { mx + 6, y + 2, 28, 13 }, "ON");
        on.isOn = ifSelected ([sendOf] (Strip& s) { const auto send = sendOf (s); return send.isValid() && send.on->load(); });
        on.onClick = onSelected ([sendOf] (Strip& s) { if (const auto send = sendOf (s); send.isValid()) *send.on = ! send.on->load(); });
        auto& pre = key ("MIX PRE " + juce::String (k + 1), kCream, Shape::Cap, { mx + 6, y + 22, 28, 12 }, "PRE");
        pre.isOn = ifSelected ([sendOf] (Strip& s) { const auto send = sendOf (s); return send.isValid() && ! send.isGroup() && send.isPre(); });
        pre.onClick = onSelected ([sendOf] (Strip& s) { if (const auto send = sendOf (s); send.isValid()) send.setPre (! send.isPre()); });
        knob ("MIX " + juce::String (k + 1), tops[k], { mx + 56, y, 36, 36 }).onTouch = [this] { setArea (mixBank == 2 ? Area::Matrix : Area::Aux); };
        silk ((float) mx + 40.0f, (float) y + 35.0f, 64.0f, 8.0f, k % 2 == 0 ? "level / pan" : "level", 6.2f, kSilk);
        silk ((float) mx + 38.0f, (float) y + 2.0f, 16.0f, 10.0f, juce::String (k + 1), 8.0f, kSilk, juce::Justification::centred, true);
    }
    auto& mixUp = key ("MIX UP", kCream, Shape::Up, { mx + 60, 446, 24, 20 });
    mixUp.onClick = [this] { setMixBank (mixBank - 1); };
    silk ((float) mx + 44.0f, 468.0f, 56.0f, 20.0f, "scroll\nby 8", 7.0f, kSilk);
    auto& mixDown = key ("MIX DOWN", kCream, Shape::Down, { mx + 60, 490, 24, 20 });
    mixDown.onClick = [this] { setMixBank (mixBank + 1); };
    {
        auto& k = key ("QA MIX", kGrey, Shape::Round, { mx + 14, 494, 14, 14 });
        k.isOn = [this] { return area == Area::Aux || area == Area::Matrix; };
        k.onClick = [this] { setArea (area == Area::Aux || area == Area::Matrix ? Area::Overview : mixBank == 2 ? Area::Matrix : Area::Aux); };
    }
    outline ({ (float) mx, 440.0f, 104.0f, 114.0f });
}

Strip* Pro2Surface::outputAt (int row, int i) const
{
    if (row < 2)
        return ctx.engine.getBusInSlot (row * 8 + i);
    const auto& m = ctx.engine.getMatrices();
    return i < (int) m.size() ? m[(size_t) i].get() : nullptr;
}

void Pro2Surface::buildOutputs()
{
    using Shape = Key::Shape;
    // The 24 outputs: TALK, MUTE, the meter, SOLO and SELECT of each (pp.18, 272-278).
    for (int row = 0; row < 3; ++row)
    {
        const int y0 = 58 + row * 146;
        for (int i = 0; i < 8; ++i)
        {
            const int cx = kBx + 22 + i * 44;
            const auto name = juce::String (row == 2 ? "MTX " : "AUX ") + juce::String (row == 1 ? i + 9 : i + 1);
            plate ({ (float) cx - 20.0f, (float) y0, 40.0f, 16.0f }, kPlate);
            auto& talk = key ("TALK " + name, kGreen, Shape::Cap, { cx - 14, y0 + 2, 28, 12 }, "TALK");
            // TALK routes the internal talk bus to the mix (p.272): the engine's TALK A destinations.
            talk.isOn = [this, row, i]
            {
                auto& path = ctx.engine.talk.paths[0];
                return row < 2 ? path.buses.contains (row * 8 + i) : path.matrices.contains (i);
            };
            talk.onClick = [this, row, i]
            {
                auto& path = ctx.engine.talk.paths[0];
                if (row < 2)
                    path.buses.toggle (row * 8 + i);
                else
                    path.matrices.toggle (i);
            };
            auto& mute = key ("MUTE " + name, kRed, Shape::Cap, { cx - 19, y0 + 22, 24, 16 }, "MUTE");
            mute.isOn = [this, row, i] { auto* s = outputAt (row, i); return s != nullptr && s->params.mute.load(); };
            mute.onClick = [this, row, i] { if (auto* s = outputAt (row, i)) s->params.mute = ! s->params.mute.load(); };
            auto& solo = key ("SOLO " + name, kYellow, Shape::Cap, { cx - 19, y0 + 56, 24, 16 }, "SOLO");
            solo.isOn = [this, row, i] { auto* s = outputAt (row, i); return s != nullptr && s->params.solo.load(); };
            solo.onClick = [this, row, i] { if (auto* s = outputAt (row, i)) pressSolo (*s); };
            meter ({ cx + 8, y0 + 22, 9, 78 }, [this, row, i]
            {
                auto* s = outputAt (row, i);
                return s != nullptr ? juce::jmax (ctx.meters.get (s, MeterPoint::Output, 0), ctx.meters.get (s, MeterPoint::Output, 1)) : 0.0f;
            });
            auto& select = key ("SELECT " + name, kWhite, Shape::Cap, { cx - 14, y0 + 104, 28, 12 }, "SEL");
            select.isOn = [this, row, i]
            {
                auto* s = outputAt (row, i);
                return s != nullptr && (assigning >= 0 ? isMember (assigning, *s) : s == selectedMix());
            };
            select.onClick = [this, row, i] { pressSelect (outputAt (row, i)); };
            plate ({ (float) cx - 20.0f, (float) y0 + 121.0f, 40.0f, 14.0f }, kPlate);
            silk ((float) cx - 20.0f, (float) y0 + 121.0f, 40.0f, 14.0f, name.toLowerCase(), 8.5f, kSilkDark);
            outline ({ (float) cx - 21.0f, (float) y0 + 17.0f, 42.0f, 103.0f });
        }
    }

    // Advanced navigation (p.44). MCA groups have no engine.
    outline ({ (float) kBx, 500.0f, 174.0f, 54.0f });
    silk ((float) kBx, 501.0f, 174.0f, 9.0f, "advanced channel navigation", 7.0f, kSilk, juce::Justification::centred, true);
    auto& flipKey = key ("FLIP", kCream, Shape::Cap, { kBx + 8, 522, 34, 18 }, "FLIP");
    flipKey.isOn = [this] { return flip; };
    flipKey.onClick = [this] { setFlip (! flip); };
    silk ((float) kBx + 44.0f, 520.0f, 44.0f, 22.0f, "to faders with\noutput select", 6.0f, kSilk);
    auto& fxKey = key ("FX", kCream, Shape::Cap, { kBx + 94, 522, 30, 18 }, "FX");
    fxKey.isOn = [this] { return fxFollows; };
    fxKey.onClick = [this] { fxFollows = ! fxFollows; };
    silk ((float) kBx + 126.0f, 520.0f, 46.0f, 22.0f, "access with\noutput select", 6.0f, kSilk);
    outline ({ (float) kBx + 178.0f, 500.0f, 174.0f, 54.0f });
    silk ((float) kBx + 178.0f, 501.0f, 174.0f, 9.0f, "advanced mix bay navigation", 7.0f, kSilk, juce::Justification::centred, true);
    auto& geqKey = key ("GEQ", kCream, Shape::Cap, { kBx + 186, 522, 34, 18 }, "GEQ");
    geqKey.isOn = [this] { return mixMode == MixMode::Geq; };
    geqKey.onClick = [this] { setMixMode (mixMode == MixMode::Geq ? MixMode::Vca : MixMode::Geq); };
    silk ((float) kBx + 222.0f, 520.0f, 46.0f, 22.0f, "access with\noutput select", 6.0f, kSilk);
    key ("MCA", kCream, Shape::Cap, { kBx + 272, 522, 34, 18 }, "MCA");
    silk ((float) kBx + 308.0f, 520.0f, 44.0f, 22.0f, "access with\noutput select", 6.0f, kSilk);
}

void Pro2Surface::buildComms()
{
    using Shape = Key::Shape;
    const int x = kCx;
    // The talk mic: its gain, limiter and level (p.118). Only the level has an engine.
    silk ((float) x, 96.0f, 86.0f, 8.0f, "talk", 7.5f, kSilk, juce::Justification::centred, true);
    knob ("MIC GAIN", juce::Colour (0xff3a3d42), { x + 8, 106, 30, 30 });
    silk ((float) x + 40.0f, 114.0f, 44.0f, 10.0f, "mic gain", 6.5f, kSilk, juce::Justification::centredLeft);
    knob ("LIMITER", juce::Colour (0xff2c3fa8), { x + 8, 142, 30, 30 });
    silk ((float) x + 40.0f, 150.0f, 44.0f, 10.0f, "limiter", 6.5f, kSilk, juce::Justification::centredLeft);
    knob ("TALK LEVEL", kGreen, { x + 8, 178, 30, 30 }).setBinding (bindFader ("TALK", ctx.engine.talk.levelDb));
    silk ((float) x + 40.0f, 186.0f, 44.0f, 10.0f, "level", 6.5f, kSilk, juce::Justification::centredLeft);
    for (int p = 0; p < 2; ++p)
    {
        auto& talk = key (p == 0 ? "TALK INT" : "TALK EXT", kGreen, Shape::Cap, { x + 10 + p * 36, 216, 30, 14 }, "TALK");
        talk.isOn = [this, p] { return ctx.engine.isTalkOn (p); };
        talk.onClick = [this, p] { ctx.engine.talk.paths[p].on = ! ctx.engine.talk.paths[p].on.load(); };
        key (p == 0 ? "OSC INT" : "OSC EXT", kYellow, Shape::Cap, { x + 10 + p * 36, 236, 30, 14 }, "OSC");
        silk ((float) x + 6.0f + (float) p * 36.0f, 251.0f, 38.0f, 8.0f, p == 0 ? "internal" : "external", 6.2f, kSilk);
    }
    outline ({ (float) x + 4.0f, 92.0f, 78.0f, 170.0f });

    // solo in place, under its cover: not Sidecar's to switch from here (it mutes the console).
    key ("SIP", kPink, Shape::Cap, { x + 24, 270, 38, 32 });
    silk ((float) x, 303.0f, 86.0f, 9.0f, "solo in place", 7.0f, kSilk);

    outline ({ (float) x + 4.0f, 316.0f, 78.0f, 54.0f });
    silk ((float) x + 4.0f, 317.0f, 78.0f, 9.0f, "solo", 7.5f, kSilk, juce::Justification::centred, true);
    auto& clear = key ("SOLO CLEAR", kYellow, Shape::Cap, { x + 26, 329, 34, 14 }, "CLEAR");
    clear.isOn = [this] { return ctx.engine.anySoloActive.load(); };
    clear.onClick = [this]
    {
        for (int k = 0; k < kNumStripKinds; ++k)
            for (auto& s : ctx.engine.getStrips (static_cast<StripKind> (k)))
                s->params.solo = false;
        for (int i = 0; i < ctx.engine.getNumDcas(); ++i)
            ctx.engine.getDca (i).solo = false;
    };
    auto& pfl = key ("PFL", kCream, Shape::Cap, { x + 10, 350, 30, 13 }, "PFL");
    pfl.isOn = [this] { return ! ctx.engine.soloAfl.load(); };
    pfl.onClick = [this] { ctx.engine.soloAfl = ! ctx.engine.soloAfl.load(); };
    auto& add = key ("ADD", kCream, Shape::Cap, { x + 46, 350, 30, 13 }, "ADD");
    add.isOn = [this] { return ! ctx.settings.getBoolValue (uiprefs::exclusiveSolo, uiprefs::exclusiveSoloDefault); };
    add.onClick = [this] { ctx.settings.setValue (uiprefs::exclusiveSolo, ! ctx.settings.getBoolValue (uiprefs::exclusiveSolo, uiprefs::exclusiveSoloDefault)); };

    outline ({ (float) x + 4.0f, 376.0f, 78.0f, 112.0f });
    silk ((float) x + 4.0f, 377.0f, 78.0f, 9.0f, "phones", 7.5f, kSilk, juce::Justification::centred, true);
    knob ("PHONES", kRed, { x + 22, 392, 42, 42 }).setBinding (bindFader ("PHONES", ctx.engine.phones.levelDb));
    auto& toFader = key ("PHONES C/O", kCream, Shape::Cap, { x + 28, 446, 30, 14 }, "C/O");
    toFader.isOn = [this] { return phonesOnFader; };
    toFader.onClick = [this]
    {
        phonesOnFader = ! phonesOnFader;
        bindFaders();
    };
    silk ((float) x + 4.0f, 462.0f, 78.0f, 9.0f, "to fader", 6.5f, kSilk);

    outline ({ (float) x + 4.0f, 500.0f, 78.0f, 54.0f });
    silk ((float) x + 4.0f, 501.0f, 78.0f, 9.0f, "global", 7.0f, kSilk, juce::Justification::centred, true);
    key ("TAP", kCream, Shape::Cap, { x + 28, 522, 30, 18 }, "TAP");
}

void Pro2Surface::buildNavigation()
{
    using Shape = Key::Shape;
    // The assignable controls over the channel bay (pp.153-154): a knob and a button per fader.
    silk (551.0f, 566.0f, 432.0f, 8.0f, "assignable controls", 7.0f, kSilk, juce::Justification::centred, true);
    for (int i = 0; i < 8; ++i)
    {
        const int cx = bayCx (Bay::Channel, i);
        knob ("ASSIGN " + juce::String (i + 1), juce::Colour (0xff3a3d42), centred (cx, 575, 28, 28));
        auto& b = key ("ASSIGN KEY " + juce::String (i + 1), kCream, Shape::Cap, centred (cx, 605, 22, 10));
        b.isOn = [this, i]
        {
            auto* s = stripAt (Bay::Channel, i);
            if (s == nullptr)
                return false;
            switch (area)
            {
                case Area::Comp:    return s->getKind() != StripKind::Aux && s->params.dynamics().on.load();
                case Area::Gate:    return s->getKind() == StripKind::Channel && s->params.gate().on.load();
                case Area::Eq:      return s->params.eq().on.load();
                case Area::Insert:  return s->params.preInsert().on.load();
                case Area::Masters: return s->isInput() && s->mains[altButtons ? 1 : 0].on.load();
                case Area::Aux:
                case Area::Matrix:
                {
                    auto* mix = selectedMix();
                    const auto send = mix != nullptr ? sendTo (*s, *mix) : MixSend {};
                    return send.isValid() && (altButtons ? send.isPre() : send.on->load());
                }
                case Area::Overview:
                case Area::Config:  break;
            }
            return altButtons ? s->params.mute.load() : s->params.input.invert.load();
        };
        b.onClick = [this, i]
        {
            auto* s = stripAt (Bay::Channel, i);
            if (s == nullptr)
                return;
            const auto toggle = [] (std::atomic<bool>& v) { v = ! v.load(); };
            switch (area)
            {
                case Area::Comp:
                    if (s->getKind() != StripKind::Aux)
                        toggle (s->params.dynamics().on);
                    return;
                case Area::Gate:
                    if (s->getKind() == StripKind::Channel)
                        toggle (s->params.gate().on);
                    return;
                case Area::Eq:      toggle (s->params.eq().on); return;
                case Area::Insert:  toggle (s->params.preInsert().on); return;
                case Area::Masters:
                    if (s->isInput())
                        toggle (s->mains[altButtons ? 1 : 0].on);
                    return;
                case Area::Aux:
                case Area::Matrix:
                    if (auto* mix = selectedMix())
                        if (const auto send = sendTo (*s, *mix); send.isValid())
                        {
                            if (altButtons)
                                send.setPre (! send.isPre());
                            else
                                toggle (*send.on);
                        }
                    return;
                case Area::Overview:
                case Area::Config:  break;
            }
            toggle (altButtons ? s->params.mute : s->params.input.invert);
        };
    }
    silk (508.0f, 566.0f, 40.0f, 8.0f, "console", 6.5f, kSilk);
    auto& home = key ("HOME", kCream, Shape::Cap, { 514, 576, 28, 13 }, "HOME");
    home.onClick = [this] { screen.showPage (Page::Default); };
    auto& alt = key ("ALT", kCream, Shape::Cap, { 514, 594, 28, 13 }, "ALT");
    alt.isOn = [this] { return altButtons; };
    alt.onClick = [this] { altButtons = ! altButtons; };
    key ("ENTER", kCream, Shape::Cap, { 986, 576, 28, 13 }, "ENT");
    {
        auto& k = key ("QA ASSIGN", kGrey, Shape::Round, { 993, 595, 14, 14 });
        k.onClick = [this] { setArea (Area::Overview); };
    }

    // The arrows between the bays: the cross-hair (p.39). Left and right step the selected channel along the
    // channel bay, up and down the detail area (Sidecar's reading: the manual leaves them to the Quick Start Guide).
    const auto step = [this] (int d)
    {
        int at = -1;
        for (int i = 0; i < 8; ++i)
            if (stripAt (Bay::Channel, i) != nullptr && stripAt (Bay::Channel, i) == selected())
                at = i;
        for (int n = 1; n <= 8; ++n)
            if (auto* s = stripAt (Bay::Channel, ((at < 0 ? (d > 0 ? -1 : 0) : at) + d * n + 16) % 8))
            {
                pressSelect (s);
                return;
            }
    };
    key ("UP", kCream, Shape::Up, { 1072, 572, 26, 22 }).onClick = [this] { setArea ((Area) (((int) area + 8) % 9)); };
    key ("LEFT", kCream, Shape::Left, { 1042, 597, 26, 24 }).onClick = [step] { step (-1); };
    key ("RIGHT", kCream, Shape::Right, { 1102, 597, 26, 24 }).onClick = [step] { step (1); };
    key ("DOWN", kCream, Shape::Down, { 1072, 624, 26, 22 }).onClick = [this] { setArea ((Area) (((int) area + 1) % 9)); };

    // channel faders: INPUT, OUTPUT, EXTEND (p.19).
    outline ({ 1020.0f, 662.0f, 132.0f, 58.0f });
    silk (1020.0f, 663.0f, 132.0f, 9.0f, "channel faders", 7.0f, kSilk, juce::Justification::centred, true);
    auto& input = key ("INPUT", kCream, Shape::Cap, { 1026, 676, 36, 15 }, "INPUT");
    input.isOn = [this] { return banks[1] < kInputBanks + 1; };
    input.onClick = [this] { setBank (Bay::Channel, 0); };
    auto& output = key ("OUTPUT", kCream, Shape::Cap, { 1026, 696, 36, 15 }, "OUTPUT");
    output.isOn = [this] { return banks[1] > kInputBanks; };
    output.onClick = [this] { setBank (Bay::Channel, kInputBanks + 1); };
    key ("FADERS TO STRIP", kCream, Shape::Cap, { 1070, 682, 28, 14 });
    silk (1062.0f, 698.0f, 44.0f, 18.0f, "faders to\nchannel strip", 5.6f, kSilk);
    auto& extend = key ("EXTEND", juce::Colour (0xff3246d8), Shape::Cap, { 1108, 682, 40, 14 }, "EXTEND");
    extend.isOn = [this] { return mixMode == MixMode::Extend; };
    extend.onClick = [this] { setMixMode (mixMode == MixMode::Extend ? mixModeBeforeExtend : MixMode::Extend); };
    silk (1106.0f, 698.0f, 44.0f, 9.0f, "to mix bay", 5.8f, kSilk);

    // screen access (p.18): each key's screens in turn.
    outline ({ 1020.0f, 728.0f, 132.0f, 344.0f });
    silk (1020.0f, 730.0f, 132.0f, 9.0f, "screen access", 7.0f, kSilk, juce::Justification::centred, true);
    static const char* const access[] = { "automation / filing", "effects / graphics", "patching / metering", "inputs / outputs", "monitors / preferences", "vcas" };
    for (int i = 0; i < 6; ++i)
    {
        const int y = 746 + i * 22;
        auto& k = key ("SCREEN " + juce::String (i + 1), kGrey, Shape::Round, { 1028, y, 15, 15 });
        silk (1047.0f, (float) y, 104.0f, 15.0f, access[i], 6.6f, kSilk, juce::Justification::centredLeft);
        k.isOn = [this, i]
        {
            const auto p = screen.getPage();
            switch (i)
            {
                case 0:  return p == Page::Automation;
                case 1:  return p == Page::Effects || p == Page::Geq;
                case 2:  return p == Page::Patching || p == Page::Overview;
                case 3:  return p == Page::Default;
                case 5:  return p == Page::Groups;
                default: break;
            }
            return false;
        };
        k.onClick = [this, i]
        {
            const auto p = screen.getPage();
            switch (i)
            {
                case 0: screen.showPage (Page::Automation); break;
                case 1: screen.showPage (p == Page::Effects ? Page::Geq : Page::Effects); break;
                case 2: screen.showPage (p == Page::Patching ? Page::Overview : Page::Patching); break;
                case 3:
                    if (p == Page::Default)
                        setBank (Bay::Channel, banks[1] > kInputBanks ? 0 : kInputBanks + 1);
                    screen.showPage (Page::Default);
                    break;
                case 5: screen.showPage (Page::Groups); break;
                default: break; // monitors / preferences: Sidecar's own SETUP has them
            }
        };
    }

    // The POPulation groups and the auto-mutes over the mix bay (pp.146-147).
    silk (1180.0f, 566.0f, 296.0f, 8.0f, "population groups", 7.0f, kSilk, juce::Justification::centred, true);
    for (int k = 0; k < kPops; ++k)
    {
        auto& pop = key ("POP " + juce::String (k + 1), kLcdBody, Shape::Lcd, { 1182 + k * 50, 576, 46, 34 });
        auto* popKey = &pop;
        pop.legend = [this, k] { return groupName (kVcas + k); };
        pop.backlight = [this, k] { return ctx.settings.getValue (settingsKey (kVcas + k, "pop")).isEmpty() ? juce::Colour (0xff3d49d8) : juce::Colour (0xffd8dcff); };
        pop.isOn = [this, k] { return assigning == kVcas + k ? blink() : group == kVcas + k; };
        pop.onClick = [this, k, popKey]
        {
            const int g = kVcas + k;
            if (popKey->takeHeld() || assigning == g)
                setAssigning (assigning == g ? -1 : g);
            else
                selectGroup (g);
        };
    }
    silk (1486.0f, 566.0f, 104.0f, 8.0f, "auto mutes", 7.0f, kSilk, juce::Justification::centred, true);
    for (int k = 0; k < kAutoMutes; ++k)
    {
        auto& m = key ("AUTO MUTE " + juce::String (k + 1), kRed, Shape::Cap, { 1490 + (k % 3) * 34, 576 + (k / 3) * 20, 28, 14 }, "MUTE");
        auto* muteKey = &m;
        // Held: the LCD buttons set its channels (the GUI's SELECT, p.147), until it is pressed again.
        m.isOn = [this, k] { return assigning == kGroups + k ? blink() : ctx.engine.muteGroups.contains (k); };
        m.onClick = [this, k, muteKey]
        {
            if (muteKey->takeHeld() || assigning == kGroups + k)
                setAssigning (assigning == kGroups + k ? -1 : kGroups + k);
            else
                ctx.engine.muteGroups.toggle (k);
        };
    }
    for (int k = 0; k < 3; ++k)
        silk (1490.0f + (float) k * 34.0f, 610.0f, 28.0f, 7.0f, juce::String (k + 1) + " / " + juce::String (k + 4), 5.8f, kSilk);

    // mix bay: what its faders hold.
    silk (1606.0f, (float) kMuteY - 10.0f, 44.0f, 8.0f, "mix bay", 6.5f, kSilk);
    auto& vca = key ("VCA", kCream, Shape::Cap, { 1612, kMuteY, 32, 16 }, "VCA");
    vca.isOn = [this] { return mixMode == MixMode::Vca; };
    vca.onClick = [this] { setMixMode (MixMode::Vca); };
    auto& mixOut = key ("MIX OUTPUT", kCream, Shape::Cap, { 1612, kSoloY, 32, 16 }, "OUT");
    mixOut.isOn = [this] { return mixMode == MixMode::Output; };
    mixOut.onClick = [this] { setMixMode (MixMode::Output); };
}

void Pro2Surface::buildMasters()
{
    using Shape = Key::Shape;
    // storage (drawn), automation (p.162).
    silk (1596.0f, 566.0f, 50.0f, 8.0f, "storage", 7.0f, kSilk, juce::Justification::centred, true);
    silk (1654.0f, 566.0f, 140.0f, 8.0f, "automation", 7.0f, kSilk, juce::Justification::centred, true);
    auto& last = key ("LAST", kRed, Shape::Cap, { 1660, 577, 36, 16 }, "LAST");
    last.onClick = [this]
    {
        logRecall (ctx, "LAST", ctx.show.recallPrevious());
        screen.setCue (ctx.show.getCurrentSceneIndex());
    };
    auto& now = key ("NOW", kYellow, Shape::Cap, { 1702, 577, 36, 16 }, "NOW");
    now.onClick = [this]
    {
        if (juce::isPositiveAndBelow (screen.getCue(), ctx.show.getNumScenes()))
            logRecall (ctx, "NOW", ctx.show.recallScene (screen.getCue()));
        screen.rebuild();
    };
    auto& next = key ("NEXT", kGreen, Shape::Cap, { 1744, 575, 44, 20 }, "NEXT");
    next.onClick = [this]
    {
        logRecall (ctx, "NEXT", ctx.show.recallNext());
        screen.setCue (ctx.show.getCurrentSceneIndex());
    };
    auto& store = key ("STORE", kYellow, Shape::Cap, { 1664, 598, 28, 12 });
    store.isOn = [this] { return storePending; };
    store.onClick = [this] { storePressed(); };
    silk (1660.0f, 610.0f, 36.0f, 8.0f, "store", 6.5f, kSilk);
    auto& ok = key ("OK", kGreen, Shape::Cap, { 1706, 598, 28, 12 });
    ok.isOn = [this] { return storePending && blink(); };
    ok.onClick = [this] { okPressed(); };
    silk (1702.0f, 610.0f, 36.0f, 8.0f, "ok", 6.5f, kSilk);
    auto& cancel = key ("CANCEL", kRed, Shape::Cap, { 1752, 598, 28, 12 });
    cancel.isOn = [this] { return storePending; };
    cancel.onClick = [this] { cancelPressed(); };
    silk (1748.0f, 610.0f, 36.0f, 8.0f, "cancel", 6.5f, kSilk);

    // monitor: its meters, the mutes, DIM, the fader (pp.109-110). One MUTE flag serves both sides here.
    silk (1822.0f, 566.0f, 60.0f, 8.0f, "monitor", 7.0f, kSilk, juce::Justification::centred, true);
    for (int side = 0; side < 2; ++side)
    {
        meter ({ 1834 + side * 26, 577, 10, 80 }, [this, side] { return phonesOnFader ? ctx.engine.phones.peak[side].load() : ctx.engine.monitorPeak[side].load(); });
        silk (1826.0f + (float) side * 26.0f, 657.0f, 26.0f, 8.0f, side == 0 ? "left" : "right", 6.2f, kSilk);
        auto& mute = key (side == 0 ? "MON MUTE L" : "MON MUTE R", kRed, Shape::Cap, { 1828 + side * 26, kMuteY, 22, 16 }, "MUTE");
        mute.isOn = [this] { return ctx.engine.monitorMute.load(); };
        mute.onClick = [this] { ctx.engine.monitorMute = ! ctx.engine.monitorMute.load(); };
    }
    auto& dim = key ("DIM", kGreen, Shape::Cap, { 1828, kSoloY, 22, 15 }, "DIM");
    dim.isOn = [this] { return ctx.engine.monitorDim.load(); };
    dim.onClick = [this] { ctx.engine.monitorDim = ! ctx.engine.monitorDim.load(); };
    key ("MON B", kCream, Shape::Cap, { 1854, kSoloY, 22, 15 }, "C/O");
    silk (1848.0f, (float) kSoloY + 16.0f, 36.0f, 8.0f, "monitor b", 5.8f, kSilk);

    monitorFader = std::make_unique<Fader> (ctx.engine.getFaderLaw(), kOrange);
    monitorFader->setBounds (centred (kMonitorCx, kFaderY, 50, kFaderH));
    addAndMakeVisible (*monitorFader);
    silk ((float) kMonitorCx - 27.0f, (float) kFaderY - 9.0f, 54.0f, 9.0f, "monitor", 7.0f, kSilk);
    plate ({ (float) bayCx (Bay::Master, 2) - 24.0f, 1062.0f, 48.0f, 12.0f }, kPlate);
    silk ((float) bayCx (Bay::Master, 2) - 24.0f, 1062.0f, 48.0f, 12.0f, "selected mix", 6.5f, kSilkDark);
}

//==============================================================================
std::vector<Strip*> Pro2Surface::bankStrips (int bank) const
{
    std::vector<Strip*> out;
    const auto take = [&out] (const MixerEngine::StripList& list, int first)
    {
        for (int i = first; i < first + 8 && i < (int) list.size(); ++i)
            out.push_back (list[(size_t) i].get());
    };
    if (bank < 0)
        return out;
    if (bank < kInputBanks)
        take (ctx.engine.getChannels(), bank * 8);
    else if (bank == kInputBanks)
        take (ctx.engine.getAux(), 0);
    else if (bank <= kInputBanks + 2)
        take (ctx.engine.getBuses(), (bank - kInputBanks - 1) * 8);
    else if (bank == kInputBanks + 3)
        take (ctx.engine.getMatrices(), 0);
    else if (bank == kInputBanks + 4)
        take (ctx.engine.getMains(), 0);
    return out;
}

juce::String Pro2Surface::bankName (int bank) const
{
    if (bank < kInputBanks)
        return "Channels " + juce::String (bank * 8 + 1) + " - " + juce::String (bank * 8 + 8);
    switch (bank - kInputBanks)
    {
        case 0:  return "Aux Returns 1 - 8";
        case 1:  return "Aux Sends 1 - 8";
        case 2:  return "Aux Sends 9 - 16";
        case 3:  return "Matrix 1 - 8";
        default: break;
    }
    return "Masters";
}

int Pro2Surface::getBank (Bay bay) const noexcept { return bay == Bay::Input ? banks[0] : bay == Bay::Channel ? banks[1] : outputBank; }

void Pro2Surface::setBank (Bay bay, int bank)
{
    if (bay == Bay::Mix)
    {
        setOutputBank (bank);
        return;
    }
    if (bay == Bay::Master)
        return;
    banks[bay == Bay::Input ? 0 : 1] = juce::jlimit (0, kBanks - 1, bank);
    changed();
}

void Pro2Surface::scroll (Bay bay, int delta)
{
    if (bay == Bay::Mix)
    {
        if (mixMode == MixMode::Geq)
            setGeqFirst (geqFirst + delta * 4); // by four (p.44)
        else if (mixMode == MixMode::Output)
            setOutputBank (outputBank + delta);
        return;
    }
    // Scrolling goes round (p.43: "continuously").
    setBank (bay, (getBank (bay) + delta + kBanks) % kBanks);
}

void Pro2Surface::setMixMode (MixMode m)
{
    if (m == MixMode::Extend && mixMode != MixMode::Extend)
        mixModeBeforeExtend = mixMode;
    mixMode = m;
    changed();
}

void Pro2Surface::setOutputBank (int b)
{
    outputBank = juce::jlimit (0, 3, b);
    changed();
}

void Pro2Surface::setFlip (bool f)
{
    flip = f;
    changed();
}

Strip* Pro2Surface::stripAt (Bay bay, int i) const
{
    if (! juce::isPositiveAndBelow (i, 8))
        return nullptr;
    const auto at = [i] (const std::vector<Strip*>& list, int first) { return first + i < (int) list.size() ? list[(size_t) (first + i)] : nullptr; };
    const bool unfolded = group >= 0 && ! isAreaB (group);
    if (bay == Bay::Master)
    {
        if (i > 2)
            return nullptr;
        if (group >= 0 && isAreaB (group))
            return at (groupMembers (group), 0);
        const auto& mains = ctx.engine.getMains();
        if (i == 2)
        {
            if (auto* mix = selectedMix())
                return mix;
            return mains.size() > 1 ? mains[1].get() : nullptr;
        }
        // The stereo master is one strip here: both of its faders are it.
        return mains.empty() ? nullptr : mains[0].get();
    }
    if (bay == Bay::Mix)
    {
        if (mixMode == MixMode::Output)
            return at (bankStrips (kInputBanks + 1 + outputBank), 0);
        if (mixMode == MixMode::Extend)
            return unfolded ? at (groupMembers (group), 16) : banks[1] + 1 < kBanks ? at (bankStrips (banks[1] + 1), 0) : nullptr;
        return nullptr;
    }
    // A selected group's members, in order across the input bay, the channel bay and (EXTEND) the mix bay.
    if (unfolded)
        return at (groupMembers (group), bay == Bay::Input ? 0 : 8);
    return at (bankStrips (banks[bay == Bay::Input ? 0 : 1]), 0);
}

juce::String Pro2Surface::refOf (const Strip& s) { return juce::String (toString (s.getKind())) + ":" + juce::String (s.getId()); }

Strip* Pro2Surface::findRef (const juce::String& ref) const
{
    if (ref.isEmpty())
        return nullptr;
    for (int k = 0; k < kNumStripKinds; ++k)
        for (const auto& s : ctx.engine.getStrips (static_cast<StripKind> (k)))
            if (refOf (*s) == ref)
                return s.get();
    return nullptr;
}

Strip* Pro2Surface::selectedMix() const { return findRef (mixRef); }

void Pro2Surface::selectMix (Strip* s)
{
    mixRef = s != nullptr && s->isOutput() ? refOf (*s) : juce::String();
    if (s != nullptr && fxFollows)
    {
        // FX: the effect the mix feeds comes up (p.44); a new PRO2's matrices 1-4 feed FX 1-4 (p.193).
        if (s->getKind() == StripKind::Matrix && s->getId() < kFxUnits)
            screen.setFxUnit (s->getId());
        screen.showPage (Page::Effects);
    }
    changed();
}

void Pro2Surface::pressSelect (Strip* s)
{
    if (s == nullptr)
        return;
    if (assigning >= 0)
    {
        setMember (assigning, *s, ! isMember (assigning, *s));
        return;
    }
    if (s->isOutput())
        selectMix (s);
    if (ctx.selectStrip)
        ctx.selectStrip (s);
}

void Pro2Surface::pressSolo (Strip& s) { s.params.solo = ! s.params.solo.load(); }

//==============================================================================
juce::String Pro2Surface::settingsKey (int g, const char* what) const { return "pro2." + juce::String (what) + "." + juce::String (g); }

juce::String Pro2Surface::groupName (int g) const
{
    if (g < kVcas)
    {
        const auto name = ctx.engine.getDcaName (g);
        return name.isNotEmpty() ? name : "VCA " + juce::String (g + 1);
    }
    return ctx.settings.getValue (settingsKey (g, "popName"), "POP " + juce::String (g - kVcas + 1));
}

bool Pro2Surface::isMember (int g, const Strip& s) const
{
    if (g < 0)
        return false;
    if (g < kVcas)
        return s.params.dcaMask.contains (g);
    if (g >= kGroups)
        return s.params.muteGroupMask.contains (g - kGroups);
    return juce::StringArray::fromTokens (ctx.settings.getValue (settingsKey (g, "pop")), ",", "").contains (refOf (s));
}

void Pro2Surface::setMember (int g, Strip& s, bool member)
{
    if (g < 0)
        return;
    if (g < kVcas)
        s.params.dcaMask.set (g, member);
    else if (g >= kGroups)
        s.params.muteGroupMask.set (g - kGroups, member);
    else
    {
        // The engine has no POPulation groups: the surface keeps them, in the app's settings.
        auto list = juce::StringArray::fromTokens (ctx.settings.getValue (settingsKey (g, "pop")), ",", "");
        list.removeString (refOf (s));
        if (member)
            list.add (refOf (s));
        ctx.settings.setValue (settingsKey (g, "pop"), list.joinIntoString (","));
    }
    if (g == group)
        changed();
}

std::vector<Strip*> Pro2Surface::groupMembers (int g) const
{
    std::vector<Strip*> out;
    if (g < 0)
        return out;
    const auto list = g >= kVcas && g < kGroups ? juce::StringArray::fromTokens (ctx.settings.getValue (settingsKey (g, "pop")), ",", "") : juce::StringArray();
    for (auto kind : { StripKind::Channel, StripKind::Aux, StripKind::Bus, StripKind::Matrix, StripKind::Main })
        for (const auto& s : ctx.engine.getStrips (kind))
            if (g >= kVcas && g < kGroups ? list.contains (refOf (*s)) : isMember (g, *s))
                out.push_back (s.get());
    return out;
}

void Pro2Surface::selectGroup (int g)
{
    group = g == group || ! juce::isPositiveAndBelow (g, kGroups) ? -1 : g;
    changed();
}

void Pro2Surface::setAssigning (int g)
{
    assigning = juce::isPositiveAndBelow (g, kGroups + kAutoMutes) ? g : -1;
    repaint();
}

void Pro2Surface::setAreaB (int g, bool b)
{
    if (juce::isPositiveAndBelow (g, kGroups))
    {
        ctx.settings.setValue (settingsKey (g, "areaB"), b);
        changed();
    }
}

bool Pro2Surface::isAreaB (int g) const { return juce::isPositiveAndBelow (g, kGroups) && ctx.settings.getBoolValue (settingsKey (g, "areaB"), false); }

//==============================================================================
void Pro2Surface::setArea (Area a)
{
    if (a == Area::Gate)
        showGate = true;
    else if (a == Area::Comp)
        showGate = false;
    if (a == Area::Aux && mixBank == 2)
        mixBank = 0;
    else if (a == Area::Matrix)
        mixBank = 2;
    if (area == a && screen.getPage() == Page::Default)
        return;
    area = a;
    if (a != Area::Overview && screen.getPage() != Page::Default)
        screen.showPage (Page::Default);
    bindSelected();
    bindAssignable();
    screen.rebuild();
    repaint();
}

void Pro2Surface::setDynamicsShowsGate (bool gate)
{
    showGate = gate;
    if (area == Area::Comp || area == Area::Gate)
        area = gate ? Area::Gate : Area::Comp;
    bindSelected();
    bindAssignable();
    screen.rebuild();
    repaint();
}

void Pro2Surface::setEqBand (int b)
{
    auto* s = selected();
    const int n = s != nullptr ? juce::jmax (1, eqBands (*s).size()) : 4;
    eqBand = juce::jlimit (0, n - 1, b);
    bindSelected();
    bindAssignable();
    screen.rebuild();
    repaint();
}

void Pro2Surface::setMixBank (int b)
{
    mixBank = juce::jlimit (0, 2, b);
    if (area == Area::Aux || area == Area::Matrix)
        area = mixBank == 2 ? Area::Matrix : Area::Aux;
    bindSelected();
    screen.rebuild();
    repaint();
}

void Pro2Surface::setGainSwap (bool swap)
{
    gainSwap = swap;
    bindSelected();
    screen.rebuild();
}

void Pro2Surface::setGeqFirst (int first)
{
    geqFirst = juce::jlimit (0, 23, first);
    changed();
}

bool Pro2Surface::hasGeq (const Strip& s)
{
    return s.isOutput() && models::model (models::BusEq, s.params.eq().model.load()).token == "MPGEQ";
}

void Pro2Surface::setGeq (Strip& s, bool geq)
{
    if (! s.isOutput() || hasGeq (s) == geq)
        return;
    const int model = models::findModel (models::BusEq, geq ? "MPGEQ" : "MPEQ6");
    if (model >= 0)
        s.setModel (ModelBlock::Eq, model);
}

void Pro2Surface::storePressed()
{
    storePending = ! storePending;
    screen.rebuild();
}

void Pro2Surface::okPressed()
{
    if (! storePending)
        return;
    storePending = false;
    // The highlighted scene takes the current settings; past the list's end, a new one (p.162).
    auto& show = ctx.show;
    if (juce::isPositiveAndBelow (screen.getCue(), show.getNumScenes()))
        show.storeScene (screen.getCue());
    else
        screen.setCue (show.addScene ("Scene " + juce::String (show.getNumScenes() + 1)));
    screen.rebuild();
}

void Pro2Surface::cancelPressed()
{
    storePending = false;
    screen.rebuild();
}

//==============================================================================
void Pro2Surface::bindFaders()
{
    auto& e = ctx.engine;
    auto* mix = selectedMix();
    for (auto bay : { Bay::Input, Bay::Channel, Bay::Mix, Bay::Master })
        for (int i = 0; i < (int) faders[(size_t) bay].size(); ++i)
        {
            auto& f = *faders[(size_t) bay][(size_t) i];
            if (bay == Bay::Mix && mixMode == MixMode::Vca)
            {
                if (i < e.getNumDcas())
                    f.bind (&e.getDca (i).faderDb, e.getFaderLaw());
                else
                    f.bind (nullptr, e.getFaderLaw());
                continue;
            }
            if (bay == Bay::Mix && mixMode == MixMode::Geq)
            {
                KnobBinding b;
                if (mix != nullptr && hasGeq (*mix))
                {
                    auto& eq = mix->params.eq();
                    const auto& m = models::model (models::BusEq, eq.model.load());
                    const int band = 11 + geqFirst + i;
                    if (band < (int) m.params.size())
                        b = bindModelParam (eq.values[band], m.params[(size_t) band]);
                }
                f.bind (std::move (b));
                continue;
            }
            auto* s = stripAt (bay, i);
            std::atomic<float>* value = s != nullptr ? &s->params.faderDb : nullptr;
            // FLIP: the input faders are the contributions to the selected mix (p.44).
            if (flip && s != nullptr && bay != Bay::Master)
            {
                const auto send = mix != nullptr ? sendTo (*s, *mix) : MixSend {};
                value = send.isValid() && ! send.isGroup() ? send.level : s->isInput() ? nullptr : value;
            }
            f.bind (value, e.getFaderLaw ((int) (s != nullptr ? s->getKind() : StripKind::Channel)));
        }
    monitorFader->bind (phonesOnFader ? &e.phones.levelDb : &e.monitorLevelDb, e.getFaderLaw());
}

void Pro2Surface::bindSelected()
{
    auto* s = selected();
    boundStrip = s;
    const auto set = [this] (const juce::String& name, KnobBinding b)
    {
        if (auto* k = findKnob (name))
            k->setBinding (std::move (b));
    };
    if (s == nullptr)
    {
        for (const auto* name : { "HIGH PASS", "LOW PASS", "GAIN", "ATTACK", "RELEASE", "HOLD", "THRESHOLD", "RATIO", "MAKE UP", "SC FREQ", "EQ FREQ", "EQ GAIN", "EQ WIDTH", "MONO LEVEL", "PAN" })
            set (name, {});
        for (int k = 0; k < 8; ++k)
            set ("MIX " + juce::String (k + 1), {});
        return;
    }
    auto& p = s->params;
    const bool channel = s->getKind() == StripKind::Channel;
    set ("HIGH PASS", channel ? bindHighPass (*s) : KnobBinding {});
    set ("LOW PASS", channel ? bindLowPass (*s) : KnobBinding {});

    // gain: the stage box's gain, or (SWAP, and on anything without a mic amp) the digital trim (p.247).
    {
        auto* src = s->isInput() && s->getInputLeft() >= 0 ? ctx.engine.getSource (MixerEngine::sourceIndex (s->getInputLeft())) : nullptr;
        if (src != nullptr && ! gainSwap)
        {
            auto b = bindLinear ("GAIN", src->gainDb, -5.0f, 40.0f, " dB", 0, 0.0f);
            b.steps = 10.0f; // 5 dB steps (p.246)
            set ("GAIN", std::move (b));
        }
        else
            set ("GAIN", bindTrim (*s));
    }

    // The D zone's six, on the compressor or the gate; hold is the gate's, make up the compressor's.
    const bool gate = showGate && channel;
    const bool dyn = s->getKind() != StripKind::Aux;
    set ("ATTACK", dyn ? bindDyn (*s, gate, "att") : KnobBinding {});
    set ("RELEASE", dyn ? bindDyn (*s, gate, "rel") : KnobBinding {});
    set ("HOLD", gate ? bindDyn (*s, true, "hld") : KnobBinding {});
    set ("THRESHOLD", dyn ? bindDyn (*s, gate, "thr") : KnobBinding {});
    set ("RATIO", dyn ? bindDyn (*s, gate, gate ? "range" : "ratio") : KnobBinding {});
    set ("MAKE UP", dyn && ! gate ? bindDyn (*s, false, "gain") : KnobBinding {});
    set ("SC FREQ", dyn ? bindFreq ("FREQ", (gate ? p.gate() : p.dynamics()).scFreq, 50.0f, 15000.0f, 1000.0f) : KnobBinding {});

    // The E zone's band.
    {
        const auto bands = eqBands (*s);
        eqBand = juce::jlimit (0, juce::jmax (0, bands.size() - 1), eqBand);
        const auto id = bands.isEmpty() ? juce::String() : bands[eqBand];
        set ("EQ FREQ", id.isNotEmpty() ? bindEqParam (*s, id + "f") : KnobBinding {});
        set ("EQ GAIN", id.isNotEmpty() ? bindEqParam (*s, id + "g") : KnobBinding {});
        set ("EQ WIDTH", id.isNotEmpty() ? bindEqParam (*s, id + "w") : KnobBinding {});
    }

    // masters: pan, and the level to the mono master.
    const bool toMasters = s->getKind() != StripKind::Main && s->getKind() != StripKind::Matrix;
    set ("PAN", toMasters ? bindPan ("PAN", p.pan) : KnobBinding {});
    set ("MONO LEVEL", toMasters && ctx.engine.getMains().size() > 1 ? bindSendLevel ("MONO", s->mains[1].levelDb) : KnobBinding {});

    // mix: the eight of the bank.
    for (int k = 0; k < 8; ++k)
    {
        auto* mix = mixBank < 2 ? ctx.engine.getBusInSlot (mixBank * 8 + k)
                                : k < (int) ctx.engine.getMatrices().size() ? ctx.engine.getMatrices()[(size_t) k].get() : nullptr;
        const auto send = mix != nullptr ? sendTo (*s, *mix) : MixSend {};
        set ("MIX " + juce::String (k + 1), send.isValid() && ! send.isGroup() ? bindSendLevel (mix->getName(), *send.level) : KnobBinding {});
    }
}

void Pro2Surface::bindAssignable()
{
    // Each knob works one control of the selected detail area on its fader's channel (p.154). Which
    // control is "predetermined" in the manual; these are Sidecar's choices.
    auto* mix = selectedMix();
    for (int i = 0; i < 8; ++i)
    {
        auto* k = findKnob ("ASSIGN " + juce::String (i + 1));
        auto* s = stripAt (Bay::Channel, i);
        KnobBinding b;
        if (s != nullptr)
            switch (area)
            {
                case Area::Overview:
                case Area::Config:  b = bindTrim (*s); break;
                case Area::Comp:    b = s->getKind() != StripKind::Aux ? bindDyn (*s, false, "thr") : KnobBinding {}; break;
                case Area::Gate:    b = s->getKind() == StripKind::Channel ? bindDyn (*s, true, "thr") : KnobBinding {}; break;
                case Area::Eq:
                {
                    const auto bands = eqBands (*s);
                    b = juce::isPositiveAndBelow (eqBand, bands.size()) ? bindEqParam (*s, bands[eqBand] + "g") : KnobBinding {};
                    break;
                }
                case Area::Aux:
                case Area::Matrix:
                    if (const auto send = mix != nullptr ? sendTo (*s, *mix) : MixSend {}; send.isValid() && ! send.isGroup())
                        b = bindSendLevel (mix->getName(), *send.level);
                    break;
                case Area::Masters: b = s->isInput() ? bindPan ("PAN", s->params.pan) : KnobBinding {}; break;
                case Area::Insert:  break;
            }
        if (k != nullptr)
            k->setBinding (std::move (b));
    }
}

void Pro2Surface::changed()
{
    boundVersion = ctx.engine.getStructureVersion();
    bindFaders();
    bindSelected();
    bindAssignable();
    screen.rebuild();
    repaint();
}

void Pro2Surface::refreshAll()
{
    if (selectedMix() == nullptr)
        mixRef = {};
    changed();
}

void Pro2Surface::selectionChanged()
{
    bindSelected();
    screen.rebuild();
    repaint();
}

void Pro2Surface::showChanged() { screen.rebuild(); }

void Pro2Surface::timerCallback()
{
    if (isCovered())
        return;
    if (boundVersion != ctx.engine.getStructureVersion())
        refreshAll();
    else if (boundStrip != selected())
        selectionChanged();
    for (auto& bay : faders)
        for (auto& f : bay)
            f->refresh();
    monitorFader->refresh();
    for (auto& [n, k] : keys)
        k->refresh();
    for (auto& [n, k] : knobs)
        k->refresh();
    for (auto& m : meters)
        m->refresh();
    if (++screenTick % 3 == 0)
        screen.refresh();
    universalKey.refresh();
}

//==============================================================================
void Pro2Surface::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d0e12));
    g.addTransform (design);

    const auto top = juce::Rectangle<float> (0.0f, 0.0f, (float) kDesignWidth, (float) kTopBar);
    g.setColour (theme.panel);
    g.fillRect (top);
    g.setColour (theme.text);
    g.setFont (type::font (type::kTitle, true));
    g.drawText ("PRO2", top.reduced ((float) sizes::kGapLarge, 0.0f), juce::Justification::centredLeft);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption));
    g.drawText ("Not affiliated with or endorsed by any console maker; product names are their owners' trademarks.",
                top.reduced ((float) sizes::kGapLarge, 0.0f).withTrimmedLeft (100.0f).withTrimmedRight (260.0f), juce::Justification::centredLeft);

    // The panel: blue-grey, the fader tray a shade lower.
    const juce::Rectangle<float> body (0.0f, (float) kTopBar, (float) kDesignWidth, (float) (kDesignHeight - kTopBar));
    g.setGradientFill (juce::ColourGradient (kPanel, 0.0f, body.getY(), kPanelLow, 0.0f, body.getBottom(), false));
    g.fillRect (body);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRect (0.0f, 559.0f, (float) kDesignWidth, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRect (0.0f, 561.0f, (float) kDesignWidth, 1.0f);

    // The vents over the channel strip.
    g.setColour (juce::Colour (0xff10151b));
    for (int x = kAx; x < kCx + 84; x += 6)
        g.fillRect ((float) x, 45.0f, 3.0f, 9.0f);

    // The tray beside the screen, and the screen's housing.
    {
        const juce::Rectangle<float> tray (44.0f, 56.0f, 372.0f, 488.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (tray.expanded (4.0f), 16.0f);
        g.setGradientFill (juce::ColourGradient (kTray.brighter (0.08f), 0.0f, tray.getY(), kTray, 0.0f, tray.getBottom(), false));
        g.fillRoundedRectangle (tray, 14.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (tray.reduced (16.0f), 8.0f, 1.5f);
        for (float y : { tray.getY() + 168.0f, tray.getY() + 320.0f })
            g.fillRect (tray.getX() + 16.0f, y, tray.getWidth() - 32.0f, 1.5f);

        const juce::Rectangle<float> housing ((float) kScreenX - 14.0f, (float) kScreenY - 10.0f, (float) kScreenW + 28.0f, (float) kScreenH + 22.0f);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (housing.expanded (3.0f), 6.0f);
        g.setGradientFill (juce::ColourGradient (kPanel.brighter (0.12f), 0.0f, housing.getY(), kPanelLow.darker (0.1f), 0.0f, housing.getBottom(), false));
        g.fillRoundedRectangle (housing, 5.0f);
        g.setColour (juce::Colour (0xff07080a));
        g.fillRect (juce::Rectangle<float> ((float) kScreenX - 3.0f, (float) kScreenY - 3.0f, (float) kScreenW + 6.0f, (float) kScreenH + 6.0f));
    }

    // The white plates and the sections' outlines.
    for (const auto& p : plates)
    {
        if (p.outlineOnly)
        {
            g.setColour (p.colour.withAlpha (0.7f));
            g.drawRoundedRectangle (p.area, 3.0f, 1.0f);
        }
        else
        {
            g.setColour (p.colour);
            g.fillRoundedRectangle (p.area, 2.5f);
        }
    }

    // The strips' boxes around MUTE, SOLO and the meter, and their fader wells' outlines.
    g.setColour (kLine.withAlpha (0.55f));
    for (auto bay : { Bay::Input, Bay::Channel, Bay::Mix, Bay::Master })
        for (int i = 0; i < (bay == Bay::Master ? 3 : 8); ++i)
        {
            const float cx = (float) bayCx (bay, i), half = bay == Bay::Master ? 26.0f : 27.0f;
            g.drawRoundedRectangle (juce::Rectangle<float> (cx - half + 2.0f, (float) kMuteY - 4.0f, half * 2.0f - 4.0f, 52.0f), 2.5f, 1.0f);
            g.drawRect (juce::Rectangle<float> (cx - half, (float) kFaderY, half * 2.0f, (float) kFaderH + 4.0f), 1.0f);
        }
    g.drawRect (juce::Rectangle<float> ((float) kMonitorCx - 27.0f, (float) kFaderY, 54.0f, (float) kFaderH + 4.0f), 1.0f);

    // The E zone's band lamps (pp.257, 275): four blue LEDs; on an output bands 5 and 2 light the arrows.
    {
        auto* s = selected();
        int lit = -1;
        if (s != nullptr && s->isInput())
            lit = eqBand;
        else if (s != nullptr)
            lit = eqBand == 0 ? 0 : eqBand == 2 ? 1 : eqBand == 3 ? 2 : eqBand == 5 ? 3 : -1;
        for (int i = 0; i < 4; ++i)
        {
            const juce::Rectangle<float> led ((float) kAx + 191.0f, 404.0f + (float) i * 13.0f, 8.0f, 8.0f);
            g.setColour (i == lit ? juce::Colour (0xff3d6bff) : juce::Colour (0xff25305a));
            g.fillEllipse (led);
            if (i == lit)
            {
                g.setColour (juce::Colour (0x553d6bff));
                g.fillEllipse (led.expanded (3.0f));
            }
        }
        // The side chain listen lamps (no listen in the engine yet: dark).
        g.setColour (juce::Colour (0xff6b5a1c));
        g.fillEllipse ((float) kAx + 15.0f, 231.0f, 8.0f, 8.0f);
        g.fillEllipse ((float) kAx + 15.0f, 297.0f, 8.0f, 8.0f);
        // The safes' lamps: AUTO's follows the strip's recall safe.
        for (int i = 0; i < 6; ++i)
        {
            const bool on = i == 3 && s != nullptr && s->params.recallSafe.load();
            g.setColour (on ? juce::Colour (0xffffd84a) : juce::Colour (0xff27384a));
            g.fillEllipse ((float) kAx + 85.0f + (float) (i % 3) * 54.0f, 70.0f + (float) (i / 3) * 14.0f, 6.0f, 6.0f);
        }
    }

    // The talk mic's socket, the storage socket, solo in place's cover, the trackball and its keys.
    {
        const juce::Point<float> xlr ((float) kCx + 43.0f, 72.0f);
        g.setColour (juce::Colour (0xff101114));
        g.fillEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre (xlr));
        g.setColour (juce::Colour (0xff8d9296));
        g.drawEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre (xlr), 2.0f);
        for (float a : { -1.0f, 1.0f, 3.14f })
            g.fillEllipse (juce::Rectangle<float> (3.5f, 3.5f).withCentre (xlr.getPointOnCircumference (7.0f, a)));

        const juce::Rectangle<float> usb (1606.0f, 584.0f, 30.0f, 12.0f);
        g.setColour (juce::Colour (0xff0c0d0f));
        g.fillRoundedRectangle (usb, 2.0f);
        g.setColour (juce::Colour (0xff9aa0a6));
        g.drawRoundedRectangle (usb, 2.0f, 1.0f);
        g.setColour (kSilk);
        g.setFont (type::font (6.0f));
        g.drawText ("USB key", usb.translated (0.0f, 13.0f), juce::Justification::centred, false);

        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) kCx + 20.0f, 266.0f, 46.0f, 38.0f), 3.0f);

        const juce::Point<float> ball (1086.0f, 996.0f);
        g.setColour (juce::Colour (0xffc9cdd1));
        for (float x : { 1036.0f, 1094.0f })
            g.fillRoundedRectangle (juce::Rectangle<float> (x, 896.0f, 42.0f, 26.0f), 3.0f);
        g.setColour (juce::Colour (0xff0a0b0d));
        g.fillEllipse (juce::Rectangle<float> (104.0f, 104.0f).withCentre (ball));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4e55), ball.x - 18.0f, ball.y - 26.0f, juce::Colour (0xff0b0c0e), ball.x + 20.0f, ball.y + 30.0f, true));
        g.fillEllipse (juce::Rectangle<float> (84.0f, 84.0f).withCentre (ball));
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.fillEllipse (juce::Rectangle<float> (9.0f, 6.0f).withCentre (ball.translated (-16.0f, -22.0f)));
    }

    // What the mix bay holds, over its LCD buttons, and the selected group.
    {
        g.setColour (kSilk);
        g.setFont (type::font (7.0f, true));
        const auto what = mixMode == MixMode::Vca ? juce::String ("vca groups") : mixMode == MixMode::Geq ? juce::String ("graphic eq") : mixMode == MixMode::Extend ? juce::String ("channels") : bankName (kInputBanks + 1 + outputBank).toLowerCase();
        g.drawText (what, juce::Rectangle<float> (1180.0f, (float) kLcdY - 10.0f, 430.0f, 9.0f), juce::Justification::centred, false);
        g.drawText (group >= 0 && ! isAreaB (group) ? groupName (group) : bankName (banks[0]).toLowerCase(), juce::Rectangle<float> (57.0f, (float) kLcdY - 10.0f, 432.0f, 9.0f), juce::Justification::centred, false);
    }

    for (const auto& s : silks)
    {
        if (s.text.isEmpty())
            continue;
        g.setColour (s.colour);
        g.setFont (type::font (s.size, s.bold));
        if (s.text.containsChar ('\n'))
            g.drawFittedText (s.text, s.area.toNearestInt(), s.justification, 3);
        else
            g.drawText (s.text, s.area, s.justification, false);
    }
}

void Pro2Surface::resized()
{
    const float s = juce::jmin ((float) getWidth() / (float) kDesignWidth, (float) getHeight() / (float) kDesignHeight);
    design = s > 0.0f ? juce::AffineTransform::scale (s).translated (((float) getWidth() - (float) kDesignWidth * s) * 0.5f, ((float) getHeight() - (float) kDesignHeight * s) * 0.5f)
                      : juce::AffineTransform();
    for (auto* c : getChildren())
        c->setTransform (design);
    universalKey.setBounds (juce::Rectangle<int> (kDesignWidth - 190, 0, 190, kTopBar).reduced (sizes::kGap, 5));
    screen.setBounds (0, 0, pro2::Screen::kWidth, pro2::Screen::kHeight);
    screen.setTransform (juce::AffineTransform::scale ((float) kScreenW / (float) pro2::Screen::kWidth).translated ((float) kScreenX, (float) kScreenY).followedBy (design));
}

} // namespace livedaw::ui
