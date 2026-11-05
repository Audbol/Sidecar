#include "ui/HardwareLook.h"

#include "ui/Knobs.h"
#include "ui/universal/Theme.h"
#include "ui/universal/Widgets.h"

#include <map>

#include <cmath>

namespace livedaw::ui::hw
{

namespace
{
    juce::Colour c (juce::uint32 argb) { return juce::Colour (argb); }
} // namespace

const Look& Look::x32()
{
    static const Look look = [] {
        Look l;
        l.panelTop = c (0xff6b6760);
        l.panelBottom = c (0xff4f4c47);
        l.frame = c (0xff2c2b29);
        l.section = c (0xff28312f);
        l.sectionLine = c (0xffc9d0cc);
        l.silk = c (0xffe6e9e6);
        l.silkDim = c (0xffa3aaa6);
        l.keyTop = c (0xffdcdedd);
        l.keyBottom = c (0xffbfc2c1);
        l.keyText = c (0xff1c1e1e);
        l.backlit = true;
        l.slot = c (0xff0d0e0e);
        l.capTop = c (0xffd7d9d8);
        l.capBottom = c (0xff9fa3a2);
        l.capLine = c (0xff202222);
        l.knob = c (0xff1d1f1f);
        l.knobLine = c (0xffeeeeee);
        l.lcd = c (0xff0b0d10);
        l.lcdPixel = c (0xffe8eef2);
        l.lcdDim = c (0xff8b959c);
        l.lcdBezel = c (0xff222423);
        l.keyRadius = 3.0f;
        return l;
    }();
    return look;
}

const Look& Look::promix()
{
    static const Look look = [] {
        Look l;
        l.panelTop = c (0xff5b5e62);
        l.panelBottom = c (0xff45484c);
        l.frame = c (0xffb9bcbe); // the silver rack ears
        l.section = c (0xff4a4d51);
        l.sectionLine = c (0xffd7d9db);
        l.silk = c (0xfff0f0ee);
        l.silkDim = c (0xffb3b6b8);
        l.keyTop = c (0xffe6dfc6);
        l.keyBottom = c (0xffcbc3a6);
        l.keyText = c (0xff2a2722);
        l.backlit = false;
        l.slot = c (0xff17181a);
        l.capTop = c (0xfff0ead6);
        l.capBottom = c (0xffc9c1a6);
        l.capLine = c (0xff2a2722);
        l.knob = c (0xff1b1c1e);
        l.knobLine = c (0xffe8e8e8);
        l.lcd = c (0xffb5cf63);
        l.lcdPixel = c (0xff1f2b0c);
        l.lcdDim = c (0xff56682a);
        l.lcdBezel = c (0xff232527);
        l.keyRadius = 2.5f;
        return l;
    }();
    return look;
}

const Look& Look::y02r()
{
    static const Look look = [] {
        Look l;
        l.panelTop = c (0xff4e5053);
        l.panelBottom = c (0xff3c3e41);
        l.frame = c (0xff6a4428); // the wooden cheeks
        l.section = c (0xff434548);
        l.sectionLine = c (0xffc4c7ca);
        l.silk = c (0xffeceeef);
        l.silkDim = c (0xffa9adb1);
        l.keyTop = c (0xffe4ddc8);
        l.keyBottom = c (0xffc6bea6);
        l.keyText = c (0xff2a2723);
        l.backlit = false;
        l.slot = c (0xff151617);
        l.capTop = c (0xfff1ebd8);
        l.capBottom = c (0xffc8c0a8);
        l.capLine = c (0xff2a2723);
        l.knob = c (0xff1c1d1f);
        l.knobLine = c (0xffe6e6e6);
        l.lcd = c (0xff7fa8e0);
        l.lcdPixel = c (0xff0d1a3a);
        l.lcdDim = c (0xff36507e);
        l.lcdBezel = c (0xff2a2c2e);
        l.keyRadius = 2.5f;
        return l;
    }();
    return look;
}

void panel (juce::Graphics& g, juce::Rectangle<float> area, const Look& l)
{
    g.setGradientFill (juce::ColourGradient (l.panelTop, area.getX(), area.getY(), l.panelBottom, area.getX(), area.getBottom(), false));
    g.fillRect (area);
    // A fine brushed texture: faint horizontal lines.
    juce::Random rng (7);
    for (float y = area.getY(); y < area.getBottom(); y += 2.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.012f + 0.02f * rng.nextFloat()));
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
    }
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawHorizontalLine ((int) area.getY(), area.getX(), area.getRight());
}

void cheeks (juce::Graphics& g, juce::Rectangle<float> left, juce::Rectangle<float> right)
{
    for (auto r : { left, right })
    {
        if (r.isEmpty())
            continue;
        g.setGradientFill (juce::ColourGradient (c (0xff7a4f2e), r.getX(), r.getY(), c (0xff4a2d18), r.getRight(), r.getY(), false));
        g.fillRect (r);
        // The grain: thin dark streaks along it.
        juce::Random rng (11);
        for (float x = r.getX() + 2.0f; x < r.getRight(); x += 3.0f + 3.0f * rng.nextFloat())
        {
            g.setColour (juce::Colours::black.withAlpha (0.08f + 0.1f * rng.nextFloat()));
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        }
    }
}

void section (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title, const Look& l)
{
    g.setColour (l.section);
    g.fillRoundedRectangle (area, 5.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (area.translated (0.0f, 1.0f), 5.0f, 1.0f);
    // The silkscreen outline, the title breaking its top line.
    auto box = area.reduced (5.0f);
    g.setColour (l.sectionLine.withAlpha (0.75f));
    g.drawRoundedRectangle (box, 3.0f, 1.0f);
    if (title.isNotEmpty())
    {
        const auto font = universal::type::font (10.5f, true);
        const float w = juce::GlyphArrangement::getStringWidth (font, title) + 10.0f;
        auto label = juce::Rectangle<float> (box.getX() + 8.0f, box.getY() - 7.0f, w, 14.0f);
        g.setColour (l.section);
        g.fillRect (label);
        g.setColour (l.silk);
        g.setFont (font);
        g.drawText (title, label, juce::Justification::centred);
    }
}

void silk (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, const Look& l, float size, juce::Justification j, bool dim)
{
    g.setColour (dim ? l.silkDim : l.silk);
    g.setFont (universal::type::font (size, true));
    g.drawText (text, area, j);
}

void key (juce::Graphics& g, juce::Rectangle<float> a, const juce::String& text, bool lit, bool down, juce::Colour colour, const Look& l,
          juce::Colour face)
{
    const float r = l.keyRadius;
    // Its shadow on the panel, then the rubber.
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (a.translated (0.0f, down ? 0.5f : 1.5f), r);
    auto body = down ? a.translated (0.0f, 1.0f) : a;
    const auto top = face.isTransparent() ? l.keyTop : face.brighter (0.12f), bottom = face.isTransparent() ? l.keyBottom : face.darker (0.15f);
    if (lit && l.backlit)
    {
        // Backlit: the whole key glows.
        g.setColour (colour.withAlpha (0.35f));
        g.fillRoundedRectangle (body.expanded (1.5f), r + 1.5f);
        g.setGradientFill (juce::ColourGradient (colour.brighter (0.25f), body.getX(), body.getY(), colour.darker (0.2f), body.getX(), body.getBottom(), false));
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (top, body.getX(), body.getY(), bottom, body.getX(), body.getBottom(), false));
    }
    g.fillRoundedRectangle (body, r);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawHorizontalLine ((int) body.getY() + 1, body.getX() + r, body.getRight() - r);
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.drawRoundedRectangle (body.reduced (0.5f), r, 1.0f);
    auto textArea = body;
    if (! l.backlit)
    {
        // A small LED above the text, lit or dark.
        const float ledW = juce::jmin (14.0f, body.getWidth() * 0.4f);
        auto led = juce::Rectangle<float> (ledW, 3.5f).withCentre ({ body.getCentreX(), body.getY() + 5.0f });
        if (body.getHeight() < 22.0f)
            led = juce::Rectangle<float> (3.5f, 7.0f).withCentre ({ body.getX() + 6.0f, body.getCentreY() });
        if (lit)
        {
            g.setColour (colour.withAlpha (0.45f));
            g.fillRoundedRectangle (led.expanded (2.0f), 2.0f);
        }
        g.setColour (lit ? colour.brighter (0.3f) : juce::Colour (0xff3a2a26));
        g.fillRoundedRectangle (led, 1.5f);
        if (body.getHeight() >= 22.0f)
            textArea = body.withTrimmedTop (5.0f);
    }
    const bool dark = lit && l.backlit ? colour.getPerceivedBrightness() > 0.55f : (face.isTransparent() ? true : face.getPerceivedBrightness() > 0.5f);
    g.setColour (lit && l.backlit ? (dark ? juce::Colour (0xff101010) : juce::Colours::white) : (dark ? l.keyText : juce::Colours::white));
    g.setFont (universal::type::font (juce::jlimit (9.0f, 13.0f, body.getHeight() * 0.42f), true));
    g.drawFittedText (text, textArea.reduced (3.0f, 1.0f).toNearestInt(), juce::Justification::centred, 2, 0.75f);
}

void faderSlot (juce::Graphics& g, float x, float top, float bottom, const Look& l)
{
    auto slot = juce::Rectangle<float> (x - 2.5f, top, 5.0f, bottom - top);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (slot.expanded (1.5f), 3.0f);
    g.setColour (l.slot);
    g.fillRoundedRectangle (slot, 2.5f);
}

void faderCap (juce::Graphics& g, float x, float y, float width, const Look& l, juce::Colour colour)
{
    const float h = width * 0.78f;
    auto cap = juce::Rectangle<float> (width, h).withCentre ({ x, y });
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (cap.translated (0.0f, 3.0f), 3.0f);
    const auto top = colour.isTransparent() ? l.capTop : colour.brighter (0.25f), bottom = colour.isTransparent() ? l.capBottom : colour.darker (0.3f);
    // A moulded cap: lit from above, ridged across.
    g.setGradientFill (juce::ColourGradient (top, cap.getX(), cap.getY(), bottom, cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    for (float k = 0.22f; k < 0.95f; k += 0.14f)
        g.drawHorizontalLine ((int) (cap.getY() + cap.getHeight() * k), cap.getX() + 3.0f, cap.getRight() - 3.0f);
    g.setColour (colour.isTransparent() ? l.capLine : (colour.getPerceivedBrightness() > 0.5f ? l.capLine : juce::Colours::white));
    g.fillRect (juce::Rectangle<float> (cap.getX() + 2.0f, y - 1.0f, cap.getWidth() - 4.0f, 2.0f));
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawHorizontalLine ((int) cap.getY() + 1, cap.getX() + 3.0f, cap.getRight() - 3.0f);
}

void knob (juce::Graphics& g, juce::Rectangle<float> area, float fraction, const Look& l, juce::Colour ring, bool centred)
{
    const float d = juce::jmin (area.getWidth(), area.getHeight());
    auto outer = juce::Rectangle<float> (d, d).withCentre (area.getCentre());
    constexpr float span = juce::MathConstants<float>::pi * 5.0f / 3.0f; // 300 degrees
    const float start = -span * 0.5f;
    fraction = juce::jlimit (0.0f, 1.0f, fraction);
    if (! ring.isTransparent())
    {
        // The LED ring: dots, lit up to the value (from the middle if centred).
        constexpr int leds = 15;
        const float rr = d * 0.5f - 2.5f;
        const int at = juce::roundToInt (fraction * (leds - 1)), mid = leds / 2;
        for (int k = 0; k < leds; ++k)
        {
            const float a = start + span * (float) k / (float) (leds - 1) - juce::MathConstants<float>::halfPi;
            const bool on = centred ? ((k >= juce::jmin (mid, at) && k <= juce::jmax (mid, at))) : k <= at;
            g.setColour (on ? ring : ring.withAlpha (0.14f));
            g.fillEllipse (juce::Rectangle<float> (3.6f, 3.6f).withCentre ({ outer.getCentreX() + rr * std::cos (a), outer.getCentreY() + rr * std::sin (a) }));
        }
        outer = outer.reduced (8.0f);
    }
    // The cap: a shadow, a knurled skirt, a lighter top.
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (outer.translated (0.0f, 2.0f));
    g.setColour (l.knob);
    g.fillEllipse (outer);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    for (int k = 0; k < 24; ++k)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) k / 24.0f;
        const auto cc = outer.getCentre();
        const float r0 = outer.getWidth() * 0.4f, r1 = outer.getWidth() * 0.5f;
        g.drawLine (cc.x + r0 * std::cos (a), cc.y + r0 * std::sin (a), cc.x + r1 * std::cos (a), cc.y + r1 * std::sin (a), 1.0f);
    }
    auto top = outer.reduced (outer.getWidth() * 0.14f);
    g.setGradientFill (juce::ColourGradient (l.knob.brighter (0.5f), top.getX(), top.getY(), l.knob, top.getX(), top.getBottom(), false));
    g.fillEllipse (top);
    const float a = start + span * fraction - juce::MathConstants<float>::halfPi;
    const auto cc = top.getCentre();
    const float rr = top.getWidth() * 0.5f;
    g.setColour (l.knobLine);
    g.drawLine (cc.x + rr * 0.3f * std::cos (a), cc.y + rr * 0.3f * std::sin (a), cc.x + rr * 0.95f * std::cos (a), cc.y + rr * 0.95f * std::sin (a), 2.2f);
}

juce::Rectangle<float> lcd (juce::Graphics& g, juce::Rectangle<float> area, const Look& l)
{
    g.setColour (l.lcdBezel);
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);
    auto screen = area.reduced (10.0f);
    g.setGradientFill (juce::ColourGradient (l.lcd.brighter (0.08f), screen.getX(), screen.getY(), l.lcd.darker (0.08f), screen.getX(), screen.getBottom(), false));
    g.fillRect (screen);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRect (screen, 1.5f);
    return screen.reduced (4.0f);
}

namespace
{
    class KnobLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        explicit KnobLookAndFeel (const Look& l) : look (l) {}
        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider&) override
        {
            knob (g, juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f), pos, look);
        }

    private:
        const Look& look;
    };
} // namespace

juce::LookAndFeel& knobLookAndFeel (const Look& l)
{
    static std::map<const Look*, std::unique_ptr<KnobLookAndFeel>> lafs;
    auto& laf = lafs[&l];
    if (laf == nullptr)
        laf = std::make_unique<KnobLookAndFeel> (l);
    return *laf;
}

void applyTo (juce::Component& root, const Look& l, const juce::Component* except)
{
    for (auto* child : root.getChildren())
    {
        if (child == except)
            continue;
        if (auto* k = dynamic_cast<universal::ToggleKey*> (child))
            k->look = &l;
        else if (auto* f = dynamic_cast<universal::FaderControl*> (child))
            f->look = &l;
        else if (auto* d = dynamic_cast<DisplayKnob*> (child))
            d->getSlider().setLookAndFeel (&knobLookAndFeel (l));
        applyTo (*child, l, except);
    }
}

} // namespace livedaw::ui::hw
