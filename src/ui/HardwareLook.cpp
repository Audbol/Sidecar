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
        // As the console photographs: a neutral dark grey top, its sections and
        // the faders' strips on black, white silkscreen, mid-grey rubber keys.
        l.panelTop = c (0xff4a4b4c);
        l.panelBottom = c (0xff3a3b3c);
        l.frame = c (0xff1e1f20);
        l.section = c (0xff161718);
        l.sectionLine = c (0xff9a9c9e);
        l.silk = c (0xfff2f2f2);
        l.silkDim = c (0xffa8aaac);
        l.keyTop = c (0xff9c9e9f);
        l.keyBottom = c (0xff7a7c7d);
        l.keyText = c (0xff1c1e1e);
        l.blackSections = true;
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
        l.cap = Cap::Metal;
        l.knobRing = c (0xffff5a2a); // red-orange LEDs
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
        // Its LCD's yellow-green and dark green pixels (sampled from a console's).
        l.lcd = c (0xffa2cc1e);
        l.lcdPixel = c (0xff284d02);
        l.lcdDim = c (0xff58801a);
        l.lcdBezel = c (0xff232527);
        l.keyRadius = 2.5f;
        l.ledWidth = l.ledHeight = 5.0f; // round, above the bar
        return l;
    }();
    return look;
}

const Look& Look::y02r()
{
    static const Look look = [] {
        Look l;
        l.panelTop = c (0xff404245);
        l.panelBottom = c (0xff2f3134);
        l.frame = c (0xff6a4428); // the wooden cheeks
        l.section = c (0xff36383b);
        l.sectionLine = c (0xffc4c7ca);
        l.silk = c (0xffeceeef);
        l.silkDim = c (0xffa9adb1);
        l.keyTop = c (0xffe4ddc8);
        l.keyBottom = c (0xffc6bea6);
        l.keyText = c (0xff2a2723);
        l.backlit = false;
        l.slot = c (0xff151617);
        l.capTop = c (0xffe2e2dd);
        l.capBottom = c (0xffa9a9a3);
        l.capLine = c (0xff2a2723);
        l.knobLine = c (0xffe6e6e6);
        l.knobStyle = Knob::Dome;
        l.knob = c (0xffc9cbcc);
        l.lcd = c (0xff7fa8e0);
        l.lcdPixel = c (0xff0d1a3a);
        l.lcdDim = c (0xff36507e);
        l.lcdBezel = c (0xff2a2c2e);
        l.keyRadius = 2.5f;
        l.ledWidth = 8.0f; // a small oval at the top
        l.ledHeight = 4.0f;
        return l;
    }();
    return look;
}

void panel (juce::Graphics& g, juce::Rectangle<float> area, const Look& l)
{
    g.setGradientFill (juce::ColourGradient (l.panelTop, area.getX(), area.getY(), l.panelBottom, area.getX(), area.getBottom(), false));
    g.fillRect (area);
    // A fine brushed texture: faint horizontal lines, each its own shade (the
    // same at every repaint), only those in the area being painted.
    const auto clip = g.getClipBounds().toFloat().getIntersection (area);
    const float first = area.getY() + 2.0f * std::floor ((clip.getY() - area.getY()) / 2.0f);
    for (float y = juce::jmax (area.getY(), first); y < clip.getBottom(); y += 2.0f)
    {
        const auto row = (juce::uint32) ((y - area.getY()) / 2.0f);
        const float shade = (float) ((row * 2654435761u) >> 24) / 255.0f;
        g.setColour (juce::Colours::white.withAlpha (0.012f + 0.02f * shade));
        g.drawHorizontalLine ((int) y, clip.getX(), clip.getRight());
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
    if (! g.clipRegionIntersects (area.toNearestInt().expanded (2)))
        return;
    if (l.blackSections)
    {
        // A black panel, the title centred along its top.
        g.setColour (l.section);
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);
        if (title.isNotEmpty())
        {
            g.setColour (l.silk);
            g.setFont (universal::type::font (11.0f, true));
            g.drawText (title, area.withHeight (20.0f).translated (0.0f, 2.0f), juce::Justification::centred);
        }
        return;
    }
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
    if (text.isEmpty() || ! g.clipRegionIntersects (area.toNearestInt().expanded (2)))
        return;
    g.setColour (dim ? l.silkDim : l.silk);
    g.setFont (universal::type::font (size, true));
    g.drawText (text, area, j);
}

void key (juce::Graphics& g, juce::Rectangle<float> a, const juce::String& text, bool lit, bool down, juce::Colour colour, const Look& l,
          juce::Colour face, bool withLed)
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
    if (! l.backlit && ! withLed && text.isEmpty())
    {
        // A blank key without an LED: the moulded bar across its face.
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (juce::Rectangle<float> (body.getWidth() * 0.5f, 3.0f).withCentre (body.getCentre().translated (0.0f, body.getHeight() * 0.18f)), 1.5f, 1.0f);
    }
    if (! l.backlit && withLed)
    {
        // A small LED above the text, lit or dark.
        const float ledW = juce::jmin (l.ledWidth, body.getWidth() * 0.4f);
        auto led = juce::Rectangle<float> (ledW, l.ledHeight).withCentre ({ body.getCentreX(), body.getY() + 3.0f + l.ledHeight * 0.5f });
        if (body.getHeight() < 22.0f)
            led = juce::Rectangle<float> (3.5f, 7.0f).withCentre ({ body.getX() + 6.0f, body.getCentreY() });
        if (lit)
        {
            g.setColour (colour.withAlpha (0.45f));
            g.fillRoundedRectangle (led.expanded (2.0f), 2.0f);
        }
        g.setColour (lit ? colour.brighter (0.3f) : juce::Colour (0xff3a2a26));
        g.fillRoundedRectangle (led, juce::jmin (led.getWidth(), led.getHeight()) * 0.5f);
        if (body.getHeight() >= 22.0f)
            textArea = body.withTrimmedTop (l.ledHeight + 2.0f);
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
    if (l.cap == Look::Cap::Metal)
    {
        // The X32's: a tall brushed-aluminium cap (tinted if coloured), its face
        // scooped: lit above the hollow, shaded in it, a lit lip at the bottom.
        const float h = width * 1.3f;
        auto cap = juce::Rectangle<float> (width, h).withCentre ({ x, y });
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (cap.translated (0.0f, 3.0f), 4.0f);
        const auto tint = [&colour] (juce::Colour m) { return colour.isTransparent() ? m : m.interpolatedWith (colour, 0.6f); };
        juce::ColourGradient metal (tint (c (0xffeef0f1)), cap.getX(), cap.getY(), tint (c (0xffa3a8ac)), cap.getX(), cap.getBottom(), false);
        metal.addColour (0.42, tint (c (0xffd0d4d7)));
        metal.addColour (0.5, tint (c (0xff8b9094)));
        metal.addColour (0.82, tint (c (0xffb9bec2)));
        g.setGradientFill (metal);
        g.fillRoundedRectangle (cap, 4.0f);
        // The brushing: faint vertical lines.
        juce::Random rng (3);
        for (float lx = cap.getX() + 2.0f; lx < cap.getRight() - 2.0f; lx += 1.5f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.04f + 0.06f * rng.nextFloat()));
            g.drawVerticalLine ((int) lx, cap.getY() + 2.0f, cap.getBottom() - 2.0f);
        }
        // The line across at the fader's position, the edges' highlights.
        g.setColour (l.capLine);
        g.fillRect (juce::Rectangle<float> (cap.getX() + 2.0f, y - 1.0f, cap.getWidth() - 4.0f, 2.0f));
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawHorizontalLine ((int) cap.getY() + 1, cap.getX() + 3.0f, cap.getRight() - 3.0f);
        g.drawHorizontalLine ((int) cap.getBottom() - 2, cap.getX() + 3.0f, cap.getRight() - 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawRoundedRectangle (cap.reduced (0.5f), 4.0f, 1.0f);
        return;
    }
    // A moulded block: lit from above, bevelled sides, grip ridges, a groove
    // across the middle with the line in it.
    const float h = width * 0.8f;
    auto cap = juce::Rectangle<float> (width, h).withCentre ({ x, y });
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (cap.translated (0.0f, 3.0f), 2.5f);
    const auto top = colour.isTransparent() ? l.capTop : colour.brighter (0.25f), bottom = colour.isTransparent() ? l.capBottom : colour.darker (0.3f);
    juce::ColourGradient body (top, cap.getX(), cap.getY(), bottom, cap.getX(), cap.getBottom(), false);
    body.addColour (0.5, top.interpolatedWith (bottom, 0.35f));
    g.setGradientFill (body);
    g.fillRoundedRectangle (cap, 2.5f);
    g.setColour (juce::Colours::black.withAlpha (0.12f));
    g.fillRect (cap.withWidth (2.5f));
    g.fillRect (cap.withLeft (cap.getRight() - 2.5f));
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    for (float k : { 0.2f, 0.32f, 0.68f, 0.8f })
        g.drawHorizontalLine ((int) (cap.getY() + cap.getHeight() * k), cap.getX() + 3.0f, cap.getRight() - 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRect (juce::Rectangle<float> (cap.getX() + 1.5f, y - 2.5f, cap.getWidth() - 3.0f, 5.0f));
    g.setColour (colour.isTransparent() ? l.capLine : (colour.getPerceivedBrightness() > 0.5f ? l.capLine : juce::Colours::white));
    g.fillRect (juce::Rectangle<float> (cap.getX() + 2.0f, y - 1.0f, cap.getWidth() - 4.0f, 2.0f));
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawHorizontalLine ((int) cap.getY() + 1, cap.getX() + 3.0f, cap.getRight() - 3.0f);
}

void knob (juce::Graphics& g, juce::Rectangle<float> area, float fraction, const Look& l, juce::Colour ring, bool centred)
{
    knob (g, area, fraction, l, l.knobStyle, {}, ring, centred);
}

void knob (juce::Graphics& g, juce::Rectangle<float> area, float fraction, const Look& l, Look::Knob style, juce::Colour colour,
           juce::Colour ring, bool centred)
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
    const auto base = colour.isTransparent() ? l.knob : colour;
    const float angle = start + span * fraction - juce::MathConstants<float>::halfPi;
    const auto cc = outer.getCentre();
    const float radius = outer.getWidth() * 0.5f;
    const auto pointer = [&g, cc, angle] (float r0, float r1, float thickness, juce::Colour col)
    {
        g.setColour (col);
        g.drawLine (cc.x + r0 * std::cos (angle), cc.y + r0 * std::sin (angle), cc.x + r1 * std::cos (angle), cc.y + r1 * std::sin (angle), thickness);
    };
    const auto knurl = [&g, cc, radius] (int count, float inner, float alpha, float thickness)
    {
        g.setColour (juce::Colours::white.withAlpha (alpha));
        for (int k = 0; k < count; ++k)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) k / (float) count;
            g.drawLine (cc.x + radius * inner * std::cos (a), cc.y + radius * inner * std::sin (a), cc.x + radius * std::cos (a),
                        cc.y + radius * std::sin (a), thickness);
        }
    };
    // Its shadow on the panel.
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (outer.translated (0.0f, 2.5f));
    switch (style)
    {
        case Look::Knob::Dome:
        {
            // The 02R's: a smooth dome, lit from the top left, a dark pointer.
            g.setGradientFill (juce::ColourGradient (base.brighter (0.4f), outer.getX() + d * 0.3f, outer.getY() + d * 0.25f, base.darker (0.5f),
                                                     outer.getRight(), outer.getBottom(), true));
            g.fillEllipse (outer);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawEllipse (outer.reduced (0.5f), 1.0f);
            pointer (radius * 0.3f, radius * 0.92f, 2.2f, base.getPerceivedBrightness() > 0.5f ? c (0xff202020) : juce::Colours::white);
            break;
        }
        case Look::Knob::Wheel:
        {
            // A data wheel: a knurled rim, a dished top, a finger dimple.
            g.setColour (base);
            g.fillEllipse (outer);
            knurl (64, 0.88f, 0.08f, 1.2f);
            auto dish = outer.reduced (outer.getWidth() * 0.08f);
            g.setGradientFill (juce::ColourGradient (base.darker (0.3f), dish.getX(), dish.getY(), base.brighter (0.35f), dish.getX(), dish.getBottom(), false));
            g.fillEllipse (dish);
            const float rr = dish.getWidth() * 0.3f, dr = dish.getWidth() * 0.1f;
            auto dimple = juce::Rectangle<float> (2.0f * dr, 2.0f * dr).withCentre ({ cc.x + rr * std::cos (angle), cc.y + rr * std::sin (angle) });
            g.setGradientFill (juce::ColourGradient (base.darker (0.6f), dimple.getX(), dimple.getY(), base.brighter (0.45f), dimple.getX(), dimple.getBottom(), false));
            g.fillEllipse (dimple);
            break;
        }
        case Look::Knob::Plain:
        default:
        {
            // A plain cap: a knurled skirt, a lighter top, a pointer line.
            g.setColour (base);
            g.fillEllipse (outer);
            knurl (24, 0.8f, 0.07f, 1.0f);
            auto top = outer.reduced (outer.getWidth() * 0.14f);
            g.setGradientFill (juce::ColourGradient (base.brighter (0.5f), top.getX(), top.getY(), base, top.getX(), top.getBottom(), false));
            g.fillEllipse (top);
            // An endless encoder's value is its ring: only a pot has a pointer.
            if (ring.isTransparent())
                pointer (top.getWidth() * 0.15f, top.getWidth() * 0.47f, 2.2f, l.knobLine);
            break;
        }
    }
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
        KnobLookAndFeel (const Look& l, bool r) : look (l), ring (r) {}
        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& s) override
        {
            const bool centred = s.getMinimum() < 0.0 && std::abs (s.getMinimum() + s.getMaximum()) < 1.0e-6;
            knob (g, juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f), pos, look, ring ? look.knobRing : juce::Colour(), centred);
        }

    private:
        const Look& look;
        const bool ring;
    };
} // namespace

juce::LookAndFeel& knobLookAndFeel (const Look& l, bool ring)
{
    static std::map<std::pair<const Look*, bool>, std::unique_ptr<KnobLookAndFeel>> lafs;
    auto& laf = lafs[{ &l, ring }];
    if (laf == nullptr)
        laf = std::make_unique<KnobLookAndFeel> (l, ring);
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
