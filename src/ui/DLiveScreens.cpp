#include "ui/DLiveScreens.h"

#include "engine/Models.h"
#include "engine/Show.h"
#include "ui/MeterBank.h"
#include "ui/WingPopups.h"

#include <algorithm>
#include <cmath>

namespace livedaw::ui::dl
{

using namespace universal;

namespace
{
    // The screens' colours: the owner's photographs of a C3500, which lit blue around them.
    const juce::Colour kBack (0xff0b1228), kFrameLight (0xff9fc6da), kFrame (0xff7aa6c2), kSteelTop (0xff7a98ab), kSteel (0xff587a90), kPanel (0xff6ca2c8), kPanelDeep (0xff4f83a6), kNavy (0xff161a44), kCyan (0xff17c6ea),
        kTeal (0xff1fd6c8), kTabIdle (0xff5f7fa8), kTabOn (0xff0da4d2), kTabOnText (0xff0a5f86), kGraph (0xff1a1434), kYellow (0xffe1df1c), kOrange (0xfff0a030), kRed (0xffef0c42),
        kGreen (0xff1e9920), kKnobCyan (0xff09ece6), kKnobYellow (0xfff2c10a), kKnobGreen (0xff68db0e), kKnobBlue (0xff2f7bff), kKnobViolet (0xffb070f0);
    const juce::Colour kBands[] = { juce::Colour (0xff9ba7d4), juce::Colour (0xff23a2bf), juce::Colour (0xff3e81dc), juce::Colour (0xffd36470) };
    const char* const kBandPrefix[] = { "l", "1", "2", "h" };

    constexpr float kEqRange = 20.0f;
    constexpr int kSafeRowsShown = 7;

    void text (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, float size, juce::Colour c, juce::Justification j = juce::Justification::centredLeft, bool bold = false)
    {
        g.setColour (c);
        g.setFont (type::font (size, bold));
        g.drawText (s, r, j);
    }

    void panel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour top, juce::Colour bottom, float radius = 6.0f)
    {
        g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawRoundedRectangle (r, radius, 1.2f);
    }

    /** A value with its unit small beside it: 40 dB, 313 Hz. */
    void drawValueText (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, float size, juce::Colour c)
    {
        const auto number = s.upToFirstOccurrenceOf (" ", false, false), unit = s.fromFirstOccurrenceOf (" ", false, false);
        const auto big = type::font (size), small = type::font (size * 0.68f);
        juce::GlyphArrangement a, b;
        a.addLineOfText (big, number, 0.0f, 0.0f);
        b.addLineOfText (small, unit, 0.0f, 0.0f);
        const float w0 = a.getBoundingBox (0, -1, true).getWidth(), w1 = unit.isNotEmpty() ? b.getBoundingBox (0, -1, true).getWidth() + 3.0f : 0.0f;
        const float x = r.getCentreX() - (w0 + w1) * 0.5f;
        text (g, { x, r.getY(), w0 + 2.0f, r.getHeight() }, number, size, c);
        if (unit.isNotEmpty())
            text (g, { x + w0 + 3.0f, r.getY() + 2.0f, w1 + 2.0f, r.getHeight() }, unit, size * 0.68f, c);
    }

    void side (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, bool on, float size = 15.0f)
    {
        g.setColour (on ? kTabOn : kTabIdle);
        g.fillRoundedRectangle (r, 5.0f);
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, r.getCentreX(), r.getCentreY()));
        text (g, juce::Rectangle<float> (r.getHeight(), r.getWidth()).withCentre (r.getCentre()), s, size, on ? kTabOnText : juce::Colours::white, juce::Justification::centred);
    }

    /** 38.2 Hz, 385 Hz, 3.79 kHz, 10.00 kHz (photographed). */
    juce::String freqText (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, 2) + " kHz";
        return hz < 100.0f ? juce::String (hz, 1) + " Hz" : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    /** A band's width: 3/4 as a fraction of an octave where it is one, else 1.2 (photographed). */
    juce::String widthText (float oct)
    {
        const std::pair<float, const char*> fractions[] = { { 0.75f, "3/4" }, { 0.5f, "1/2" }, { 1.0f / 3.0f, "1/3" }, { 0.25f, "1/4" }, { 1.0f / 6.0f, "1/6" }, { 0.125f, "1/8" }, { 1.0f / 9.0f, "1/9" } };
        for (auto [value, name] : fractions)
            if (std::abs (oct - value) < 0.012f)
                return name;
        return juce::String (oct, oct < 1.0f ? 2 : 1);
    }

    KnobBinding asFreq (KnobBinding b, std::atomic<float>& value)
    {
        b.text = [&value] { return freqText (value.load()); };
        return b;
    }

    /** The strip's name, or its label if it hasn't one. */
    juce::String nameOf (const Strip& s)
    {
        const auto n = s.getName();
        return n.isNotEmpty() ? n : s.getLabel();
    }

    /** A dLive's PAFL bus as an input, its left side (livedaw_engine's routing::PAFL_SOURCE). */
    constexpr int kPaflSource = ld::kStripSourceBase + 0x1000;
    /** The MixRack, DX and Surface sockets Ext In and ABCD offer (as the Universal strip panel's EXT IN). */
    constexpr int kSockets = 140;

    /** A titled field of a page (MixRack's areas, the Ext In tab's). */
    void titledArea (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
    {
        panel (g, r, kPanel, kPanelDeep);
        if (title.isEmpty())
            return;
        const auto head = r.withHeight (26.0f);
        text (g, head, title, 17.0f, juce::Colours::white, juce::Justification::centred);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawHorizontalLine ((int) head.getBottom(), r.getX(), r.getRight());
    }

    /** The sockets, sixteen to a submenu; ids from `base` (socket 0). */
    void addSocketItems (juce::PopupMenu& menu, int base, int current)
    {
        for (int first = 0; first < kSockets; first += 16)
        {
            juce::PopupMenu block;
            const int last = juce::jmin (kSockets, first + 16);
            for (int i = first; i < last; ++i)
                block.addItem (base + i, "Socket " + juce::String (i + 1), true, i == current);
            menu.addSubMenu (juce::String (first + 1) + " - " + juce::String (last), block, true, nullptr, current >= first && current < last);
        }
    }

    /** A strip as a Source Selector's source names it: a mix by its slot, the others by their place, from 1. */
    int selectorNumber (const Strip& s)
    {
        return s.getKind() == StripKind::Bus ? s.getId() + 1 : s.getNumber();
    }

    /** The mix a Source Selector plays into (nullptr: unassigned). */
    Strip* selectorOutputOf (MixerEngine& engine, int selector)
    {
        int32_t kind = -1, index = 0;
        if (! ld::ld_selector_output (engine.getHandle(), selector, &kind, &index) || (kind != (int) StripKind::Bus && kind != (int) StripKind::Matrix))
            return nullptr;
        for (const auto& s : engine.getStrips ((StripKind) kind))
            if (s->getId() == index)
                return s.get();
        return nullptr;
    }

    /** What the Source Selector tab's keys show of the engine: it builds again when this moves. */
    juce::String selectorSignature (MixerEngine& engine, int selector)
    {
        auto* out = selectorOutputOf (engine, selector);
        return juce::String (selector) + ":" + (out != nullptr ? out->getLabel() + out->getName() : juce::String ("-")) + ":" + juce::String ((int) engine.getBuses().size());
    }

    /** The top right box: the active Mix, or Main (photographed). */
    void mainBox (juce::Graphics& g, juce::Rectangle<float> box, MixerEngine& engine, const Desk& desk)
    {
        const int bus = desk.getMixOnFaders();
        auto* mix = bus >= 0 ? stripOf (engine, { Slot::Bus, bus }) : nullptr;
        const auto fill = mix != nullptr ? mix->getColour() : juce::Colour (0xffc9cc4a);
        g.setColour (fill);
        g.fillRoundedRectangle (box, 4.0f);
        const auto pill = box.reduced (18.0f, 13.0f);
        g.setColour (fill.darker (0.35f));
        g.fillRoundedRectangle (pill, 5.0f);
        text (g, pill, mix != nullptr ? nameOf (*mix) : juce::String ("Main"), 15.0f, juce::Colours::white, juce::Justification::centred, true);
    }
} // namespace

static void valueTextDrawn (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, float size, juce::Colour c) { drawValueText (g, r, s, size, c); }

//==============================================================================
void Touchable::setBinding (KnobBinding b)
{
    binding = std::move (b);
    shown = -2.0f;
    repaint();
}

void Touchable::setTouched (bool t)
{
    if (t != touched)
    {
        touched = t;
        repaint();
    }
}

void Touchable::refresh()
{
    const float v = binding.isValid() ? binding.get() : -1.0f;
    const auto s = valueText();
    if (std::abs (v - shown) > 0.0005f || s != shownText)
    {
        shown = v;
        shownText = s;
        repaint();
    }
}

juce::String Touchable::valueText() const
{
    return binding.isValid() && binding.text ? binding.text() : juce::String();
}

void Touchable::mouseDown (const juce::MouseEvent&)
{
    dragStart = binding.isValid() ? binding.get() : 0.0f;
    if (onTouched && binding.isValid())
        onTouched (*this);
}

void Touchable::mouseDrag (const juce::MouseEvent& e)
{
    if (binding.isValid() && binding.set)
        binding.set (juce::jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() / 220.0f));
}

void Touchable::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasDraggedSinceMouseDown() && onTapped)
        onTapped();
}

void Touchable::mouseDoubleClick (const juce::MouseEvent&)
{
    if (binding.reset)
        binding.reset();
}

void Touchable::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (binding.isValid() && binding.set)
    {
        const float step = binding.steps > 1.0f ? 1.0f / (binding.steps - 1.0f) : 0.01f;
        binding.set (juce::jlimit (0.0f, 1.0f, binding.get() + (w.deltaY > 0 ? step : -step)));
    }
}

//==============================================================================
ParamBox::ParamBox (juce::Colour e, const juce::String& c) : edge (e), caption (c) {}

void ParamBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.5f);
    if (caption.isNotEmpty())
        text (g, r.removeFromTop (19.0f), caption, 14.0f, juce::Colours::white);
    g.setColour (kNavy);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (touched ? juce::Colours::white : edge);
    g.drawRoundedRectangle (r, 6.0f, touched ? 2.8f : 2.0f);
    if (! showBar)
    {
        text (g, r, valueText(), 16.5f, juce::Colours::white, juce::Justification::centred);
        return;
    }
    auto bar = r.removeFromBottom (10.0f).reduced (5.0f, 2.5f);
    text (g, r, valueText(), 16.5f, juce::Colours::white, juce::Justification::centred);
    g.setColour (juce::Colour (0xff070914));
    g.fillRect (bar);
    if (binding.isValid())
    {
        g.setColour (juce::Colour (0xffd8e6ee));
        g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, binding.get())));
    }
}

//==============================================================================
TouchKnob::TouchKnob (juce::Colour c) : colour (c) {}

void TouchKnob::setColour (juce::Colour c)
{
    colour = c;
    repaint();
}

void TouchKnob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (title.isNotEmpty())
        text (g, r.removeFromTop (28.0f), title, 21.0f, juce::Colours::white, juce::Justification::centred);
    if (showValue)
        valueTextDrawn (g, r.removeFromBottom (28.0f), Touchable::valueText(), 19.0f, valueColour);
    const float d = juce::jmin (r.getWidth(), r.getHeight()) - 3.0f;
    const auto k = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
    g.setColour (touched ? juce::Colours::white : juce::Colour (0xff20242c));
    g.drawEllipse (k.expanded (1.0f), touched ? 2.2f : 1.4f);
    g.setGradientFill (juce::ColourGradient (colour.brighter (0.25f), k.getX(), k.getY(), colour.darker (0.25f), k.getRight(), k.getBottom(), false));
    g.fillEllipse (k);
    if (binding.isValid())
    {
        const float a = juce::MathConstants<float>::pi * (-0.75f + 1.5f * juce::jlimit (0.0f, 1.0f, binding.get()));
        const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
        g.setColour (juce::Colour (0xff15171c));
        g.drawLine (juce::Line<float> (k.getCentre() + dir * (d * 0.12f), k.getCentre() + dir * (d * 0.48f)), 2.0f);
    }
}

//==============================================================================
FilterThumb::FilterThumb (const juce::String& t, bool h) : title (t), high (h) {}

void FilterThumb::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    text (g, r.removeFromTop (28.0f), title, 21.0f, juce::Colours::white, juce::Justification::centred);
    valueTextDrawn (g, r.removeFromBottom (28.0f), Touchable::valueText(), 19.0f, juce::Colours::white);
    auto pic = r.reduced (4.0f, 2.0f);
    g.setColour (juce::Colour (0xff232634));
    g.fillRect (pic);
    g.setColour (touched ? juce::Colours::white : juce::Colour (0xffc9d2da));
    g.drawRect (pic, touched ? 2.0f : 1.0f);
    const bool on = isOn && isOn();
    juce::Path p;
    const auto in = pic.reduced (1.0f);
    if (high)
    {
        p.startNewSubPath (in.getX(), in.getY() + 4.0f);
        p.lineTo (in.getRight() - 16.0f, in.getY() + 4.0f);
        p.quadraticTo (in.getRight() - 6.0f, in.getY() + 5.0f, in.getRight(), in.getBottom());
        p.lineTo (in.getX(), in.getBottom());
    }
    else
    {
        p.startNewSubPath (in.getRight(), in.getY() + 4.0f);
        p.lineTo (in.getX() + 26.0f, in.getY() + 4.0f);
        p.quadraticTo (in.getX() + 16.0f, in.getY() + 5.0f, in.getX() + 10.0f, in.getBottom());
        p.lineTo (in.getRight(), in.getBottom());
    }
    p.closeSubPath();
    g.setColour (on ? kGreen.brighter (0.2f) : juce::Colour (0xff5c6470));
    g.fillPath (p);
    if (on)
    {
        g.setColour (kYellow);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }
}

//==============================================================================
ScreenButton::ScreenButton (const juce::String& t, juce::Colour e) : juce::Button (t), edge (e) {}

void ScreenButton::refresh()
{
    const bool on = isOn && isOn();
    if (on != shownOn)
    {
        shownOn = on;
        repaint();
    }
}

void ScreenButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (1.5f);
    const float alpha = isEnabled() ? 1.0f : 0.5f;
    const auto base = pale ? juce::Colour (0xffb9cbd6) : kNavy;
    g.setColour ((shownOn && ! litFill.isTransparent() ? litFill : base).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (edge.withMultipliedAlpha (isEnabled() ? 1.0f : 0.35f));
    g.drawRoundedRectangle (r, 6.0f, shownOn ? 3.0f : 2.0f);
    if (down || highlighted)
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.2f : 0.08f));
        g.fillRoundedRectangle (r, 6.0f);
    }
    auto area = getLocalBounds().reduced (6, 2);
    if (dropdown)
    {
        const auto box = r.withTrimmedLeft (r.getWidth() - r.getHeight());
        g.setColour (kCyan.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (box, 6.0f);
        juce::Path arrow;
        const auto c = box.getCentre();
        arrow.addTriangle (c.x - 8.0f, c.y - 5.0f, c.x + 8.0f, c.y - 5.0f, c.x, c.y + 7.0f);
        g.setColour (kNavy.withMultipliedAlpha (alpha));
        g.fillPath (arrow);
        area.setRight (juce::roundToInt (box.getX()));
    }
    g.setColour ((pale && ! shownOn ? juce::Colour (0xff4a6478) : juce::Colours::white).withAlpha (isEnabled() ? 1.0f : 0.55f));
    g.setFont (type::font (fontSize));
    g.drawFittedText (getButtonText(), area, juce::Justification::centred, 2);
}

//==============================================================================
ScreenTab::ScreenTab (const juce::String& t, bool isTitle) : juce::Button (t), title (isTitle)
{
    if (title)
        setInterceptsMouseClicks (false, false);
}

void ScreenTab::refresh()
{
    const bool on = isOn && isOn();
    if (on != shownOn)
    {
        shownOn = on;
        repaint();
    }
}

void ScreenTab::paintButton (juce::Graphics& g, bool, bool)
{
    auto r = getLocalBounds().toFloat();
    juce::Path p;
    p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight() + 6.0f, 6.0f);
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (getLocalBounds());
    const auto fill = title ? juce::Colour (0xffd8e6ee) : shownOn ? kTabOn : kTabIdle.withMultipliedAlpha (isEnabled() ? 1.0f : 0.6f);
    g.setColour (fill);
    g.fillPath (p);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.strokePath (p, juce::PathStrokeType (1.2f));
    const auto ink = title ? juce::Colour (0xff5f7f96) : shownOn ? kTabOnText : juce::Colours::white.withAlpha (isEnabled() ? 1.0f : 0.5f);
    g.setColour (ink);
    g.setFont (type::font (title ? 26.0f : 15.0f, false));
    g.drawFittedText (getButtonText(), getLocalBounds().reduced (3, 1), juce::Justification::centred, 2);
}

//==============================================================================
SoftWidget::SoftWidget()
{
    for (auto& k : knobs)
    {
        k = std::make_unique<TouchKnob> (kKnobCyan);
        k->onTouched = [this] (Touchable& t)
        {
            if (onTouched)
                onTouched (t);
        };
        addChildComponent (*k);
    }
}

void SoftWidget::set (const Block& a, const Block& b, int l, int n, bool z)
{
    blocks[0] = a;
    blocks[1] = b;
    layer = l;
    numLayers = n;
    zigzag = z;
    for (int bi = 0; bi < 2; ++bi)
        for (int i = 0; i < 3; ++i)
        {
            auto& k = *knobs[(size_t) (bi * 3 + i)];
            const auto& c = blocks[(size_t) bi].controls[(size_t) i];
            k.setBinding (c.binding);
            k.setColour (c.colour);
            k.setVisible (c.binding.isValid());
        }
    resized();
    repaint();
}

juce::Rectangle<int> SoftWidget::knobArea (int b, int control) const
{
    const auto& block = blocks[(size_t) b];
    const int blockH = getHeight() / 2, y0 = b * blockH;
    const int top = y0 + (block.thumb != 0 ? 124 : 52), rowH = (blockH - (top - y0) - 4) / 3;
    const int d = juce::jmin (38, rowH - 6);
    const bool right = zigzag && control == 1;
    return { right ? getWidth() - d - 8 : 8, top + control * rowH + 3, d, d };
}

void SoftWidget::resized()
{
    for (int bi = 0; bi < 2; ++bi)
        for (int i = 0; i < 3; ++i)
            knobs[(size_t) (bi * 3 + i)]->setBounds (knobArea (bi, i));
}

void SoftWidget::refresh()
{
    bool values = false;
    for (auto& k : knobs)
    {
        k->refresh();
        values = values || k->isVisible();
    }
    if (values)
        repaint();
}

void SoftWidget::paint (juce::Graphics& g)
{
    const int blockH = getHeight() / 2;
    for (int bi = 0; bi < 2; ++bi)
    {
        const auto area = juce::Rectangle<int> (0, bi * blockH, getWidth(), blockH).toFloat().reduced (1.5f);
        panel (g, area, kSteelTop, kSteel);
        const auto& b = blocks[(size_t) bi];
        auto r = area.reduced (7.0f, 4.0f);
        auto head = r.removeFromTop (24.0f);
        text (g, head, b.title, 20.0f, juce::Colours::white);
        if (bi == 0)
            for (int d = 0; d < numLayers; ++d)
            {
                const auto dot = juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ area.getRight() - 12.0f - (float) (numLayers - 1 - d) * 16.0f, head.getCentreY() });
                g.setColour (juce::Colours::white);
                if (d == layer)
                    g.fillEllipse (dot);
                else
                    g.drawEllipse (dot, 1.4f);
            }
        auto names = r.removeFromTop (17.0f);
        text (g, names, b.left, 12.5f, juce::Colours::white);
        text (g, names, b.right, 12.5f, juce::Colours::white, juce::Justification::centredRight);

        if (b.thumb != 0)
        {
            auto thumb = r.removeFromTop (70.0f).reduced (14.0f, 4.0f);
            g.setColour (juce::Colour (0xff2a3140));
            g.fillRect (thumb);
            g.setColour (juce::Colour (0xffd0dbe2));
            g.drawRect (thumb, 1.4f);
            if (b.dyn != nullptr && (b.thumb == 2 || b.thumbText.isEmpty()))
            {
                TransferGraph curve;
                curve.setDyn (b.dyn);
                curve.setBounds (thumb.toNearestInt());
                juce::Graphics::ScopedSaveState state (g);
                g.addTransform (juce::AffineTransform::translation (thumb.getX(), thumb.getY()));
                curve.paint (g);
                text (g, thumb.withPosition (3.0f, 1.0f), b.thumbText, 15.0f, juce::Colours::white, juce::Justification::topLeft, true);
            }
            else
            {
                g.setColour (juce::Colours::white);
                g.setFont (type::font (19.0f, true));
                g.drawFittedText (b.thumbText, thumb.toNearestInt(), juce::Justification::centred, 2);
            }
            // Its meters: the level going in at its left, the gain reduction, down from the top, at its right.
            const float on = b.in != nullptr && b.in->load() ? 1.0f : 0.35f;
            const juce::Rectangle<float> levelBar (thumb.getX() - 8.0f, thumb.getY() + 6.0f, 5.0f, thumb.getHeight() - 12.0f);
            const juce::Rectangle<float> reductionBar (thumb.getRight() + 3.0f, thumb.getY() + 6.0f, 5.0f, thumb.getHeight() - 12.0f);
            g.setColour (juce::Colour (0xff121522));
            g.fillRect (levelBar);
            g.fillRect (reductionBar);
            if (b.levelDb)
            {
                const float f = juce::jlimit (0.0f, 1.0f, (b.levelDb() + 60.0f) / 60.0f);
                g.setColour (kGreen.withAlpha (on));
                g.fillRect (levelBar.withTop (levelBar.getBottom() - levelBar.getHeight() * f));
            }
            if (b.reductionDb)
            {
                const float f = juce::jlimit (0.0f, 1.0f, -b.reductionDb() / 24.0f);
                g.setColour (juce::Colour (0xffe02030).withAlpha (on));
                g.fillRect (reductionBar.withHeight (reductionBar.getHeight() * f));
            }
        }

        for (int i = 0; i < 3; ++i)
        {
            const auto& c = b.controls[(size_t) i];
            if (! c.binding.isValid())
                continue;
            const auto k = knobArea (bi, i).toFloat();
            const bool knobRight = zigzag && i == 1;
            const auto label = juce::Rectangle<float> (knobRight ? 8.0f : k.getRight() + 3.0f, k.getY() - 1.0f, area.getWidth() - k.getWidth() - 20.0f, 17.0f);
            const auto value = label.translated (0.0f, 18.0f);
            text (g, label, c.label, 14.0f, juce::Colours::white, knobRight ? juce::Justification::centredLeft : juce::Justification::centredRight);
            text (g, value, c.binding.text ? c.binding.text() : juce::String(), 14.5f, c.colour.brighter (0.1f), knobRight ? juce::Justification::centredLeft : juce::Justification::centredRight);
            if (c.tag.isNotEmpty())
                text (g, { k.getRight() + 1.0f, k.getY() - 3.0f, 14.0f, 12.0f }, c.tag, 11.0f, c.colour);
        }
    }
}

void SoftWidget::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown())
        return;
    if (e.y < 28 && e.x > getWidth() - 16 * numLayers - 8)
    {
        const int d = juce::jlimit (0, numLayers - 1, numLayers - 1 - (getWidth() - 4 - e.x) / 16);
        if (onLayerTapped)
            onLayerTapped (d);
        return;
    }
    if (onBlockTapped)
        onBlockTapped (e.y < getHeight() / 2 ? 0 : 1);
}

void sendBlocks (MixerEngine& engine, Strip* s, int first, SoftWidget::Block& a, SoftWidget::Block& b)
{
    static const juce::Colour colours[] = { kKnobCyan, kKnobYellow, kRed, kKnobBlue, kKnobViolet, kKnobGreen };
    a = {};
    b = {};
    if (s == nullptr || ! s->isInput())
        return;
    a.title = b.title = "Send";
    a.left = b.left = nameOf (*s);
    a.right = b.right = s->getLabel();
    const auto& buses = engine.getBuses();
    for (int i = 0; i < 6; ++i)
    {
        const int bus = first + i;
        auto& block = i < 3 ? a : b;
        auto& c = block.controls[(size_t) (i % 3)];
        if (bus >= (int) buses.size())
            continue;
        const auto name = buses[(size_t) bus]->getName();
        c.label = name.isNotEmpty() ? name : "Aux " + juce::String (bus + 1);
        c.tag = juce::String (bus + 1);
        c.binding = bindFader (c.label, s->sends[(size_t) bus].levelDb);
        c.colour = colours[i];
    }
}

//==============================================================================
EqGraph::EqGraph (UiContext& c) : ctx (c)
{
    for (int i = 0; i < kPoints; ++i)
        freqs[(size_t) i] = 20.0f * std::pow (1000.0f, (float) i / (float) (kPoints - 1));
}

void EqGraph::setStrip (Strip* s)
{
    strip = s;
    lastValues.clear();
    update();
    repaint();
}

float EqGraph::xOf (float hz) const noexcept { return (float) getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); }
float EqGraph::yOf (float db) const noexcept { return (float) getHeight() * 0.5f * (1.0f - db / kEqRange); }

int EqGraph::find (const juce::String& id) const
{
    if (strip == nullptr)
        return -1;
    return models::model ((models::Table) strip->getEqTable(), strip->params.eq().model.load()).find (id);
}

void EqGraph::compute()
{
    auto& eq = strip->params.eq();
    const int model = eq.model.load();
    const auto table = (models::Table) strip->getEqTable();
    const auto& m = models::model (table, model);
    const int n = (int) m.params.size();
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = eq.values[i].load();
    sum.fill (0.0f);
    models::modelResponse (table, model, v.data(), n, freqs.data(), sum.data(), kPoints);
    hasBands = m.find ("1g") >= 0 && m.find ("lg") >= 0;
    for (auto& b : bands)
        b.fill (0.0f);
    if (hasBands)
    {
        for (int b = 0; b < 4; ++b)
        {
            auto only = v;
            for (int o = 0; o < 4; ++o)
            {
                if (o == b)
                    continue;
                only[(size_t) m.find (juce::String (kBandPrefix[o]) + "g")] = 0.0f;
                const int mode = m.find (juce::String (kBandPrefix[o]) + "m");
                if (mode >= 0)
                    only[(size_t) mode] = 1.0f; // a bell, flat
            }
            models::modelResponse (table, model, only.data(), n, freqs.data(), bands[(size_t) b].data(), kPoints);
        }
    }
    filter.fill (0.0f);
    if (strip->isInput())
    {
        std::array<double, kPoints> hz {}, db {};
        for (int i = 0; i < kPoints; ++i)
            hz[(size_t) i] = (double) freqs[(size_t) i];
        ld::ld_strip_response (strip->getHandle(), 3, hz.data(), db.data(), kPoints, 48000.0);
        for (int i = 0; i < kPoints; ++i)
            filter[(size_t) i] = (float) db[(size_t) i];
    }
}

void EqGraph::update()
{
    if (strip == nullptr)
        return;
    auto& eq = strip->params.eq();
    std::vector<float> now;
    now.reserve (32);
    now.push_back ((float) eq.model.load());
    now.push_back (eq.on.load() ? 1.0f : 0.0f);
    for (int i = 0; i < juce::jmin ((int) ld::kMaxEqParams, 16); ++i)
        now.push_back (eq.values[i].load());
    if (strip->isInput())
    {
        auto& f = strip->params.filter();
        now.push_back (f.lowCut.load() ? f.lowCutFreq.load() : -1.0f);
        now.push_back (f.highCut.load() ? f.highCutFreq.load() : -1.0f);
        now.push_back ((float) f.lowCutSlope.load());
    }
    if (now != lastValues)
    {
        lastValues = std::move (now);
        compute();
        repaint();
    }
}

float EqGraph::bandFreq (int b) const
{
    const int i = find (juce::String (kBandPrefix[b]) + "f");
    return i >= 0 ? strip->params.eq().values[i].load() : 1000.0f;
}

float EqGraph::bandGain (int b) const
{
    const int i = find (juce::String (kBandPrefix[b]) + "g");
    return i >= 0 && ! bandIsCut (b) ? strip->params.eq().values[i].load() : 0.0f;
}

bool EqGraph::bandIsCut (int b) const
{
    const int i = find (juce::String (kBandPrefix[b]) + "m");
    return i >= 0 && strip->params.eq().values[i].load() > 1.5f;
}

void EqGraph::setRta (const Analysis::Bands* b)
{
    rta = b;
    repaint();
}

void EqGraph::selectBand (int b)
{
    if (b != selected)
    {
        selected = b;
        repaint();
        if (onBandChanged)
            onBandChanged();
    }
}

void EqGraph::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    if (onlyBand >= 0)
    {
        g.fillAll (juce::Colour (0xff0b0c14));
        if (strip == nullptr || ! hasBands)
            return;
        const auto& band = bands[(size_t) onlyBand];
        const float zero = yOf (0.0f);
        juce::Path fill, line;
        fill.startNewSubPath (0.0f, zero);
        for (int i = 0; i < kPoints; ++i)
        {
            const juce::Point<float> pt (xOf (freqs[(size_t) i]), yOf (juce::jlimit (-kEqRange, kEqRange, band[(size_t) i])));
            fill.lineTo (pt);
            if (i == 0)
                line.startNewSubPath (pt);
            else
                line.lineTo (pt);
        }
        fill.lineTo (r.getRight(), zero);
        fill.closeSubPath();
        g.setColour (kBands[onlyBand].darker (0.15f));
        g.fillPath (fill);
        g.setColour (juce::Colour (0xffd8e8d8));
        g.strokePath (line, juce::PathStrokeType (2.0f));
        return;
    }
    g.fillAll (kGraph);
    const auto grid = juce::Colour (0xffc8d4f0);
    for (float decade : { 10.0f, 100.0f, 1000.0f, 10000.0f })
        for (int m = decade < 11.0f ? 2 : 1; m < 10; ++m)
        {
            const float hz = decade * (float) m;
            if (hz < 20.0f || hz > 20000.0f)
                continue;
            g.setColour (grid.withAlpha (m == 1 || m == 2 || m == 5 ? 0.5f : 0.26f));
            g.drawVerticalLine ((int) xOf (hz), 0.0f, r.getBottom());
        }
    for (float db : { -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f })
    {
        g.setColour (grid.withAlpha (db == 0.0f ? 0.55f : 0.32f));
        g.drawHorizontalLine ((int) yOf (db), 0.0f, r.getRight());
    }

    const bool on = strip != nullptr && strip->params.eq().on.load();
    const float y0 = yOf (0.0f);
    if (strip != nullptr)
    {
        // Each band, from the 0 dB line to its own curve.
        if (hasBands)
            for (int b = 0; b < 4; ++b)
            {
                juce::Path p;
                p.startNewSubPath (0.0f, y0);
                for (int i = 0; i < kPoints; ++i)
                    p.lineTo (xOf (freqs[(size_t) i]), yOf (juce::jlimit (-kEqRange, kEqRange, bands[(size_t) b][(size_t) i])));
                p.lineTo (r.getRight(), y0);
                p.closeSubPath();
                g.setColour (kBands[b].withAlpha (on ? 0.82f : 0.3f));
                g.fillPath (p);
            }
        // The sum, with the filters in it.
        std::array<float, kPoints> total {};
        for (int i = 0; i < kPoints; ++i)
            total[(size_t) i] = juce::jlimit (-kEqRange - 1.0f, kEqRange, on ? sum[(size_t) i] : 0.0f);

        // The filters: the area each cuts, from the curve to the filter's own skirt (photographed),
        // in green for the high pass when it's in, dim when it's out.
        if (strip->isInput())
        {
            auto& f = strip->params.filter();
            const auto skirt = [] (float hz, float fc, int order, bool high)
            {
                const float ratio = high ? hz / fc : fc / hz;
                return -10.0f * std::log10 (1.0f + std::pow (ratio, 2.0f * (float) order));
            };
            const auto cut = [&] (float fc, int order, bool high, juce::Colour colour)
            {
                juce::Path p;
                p.startNewSubPath (high ? r.getRight() : 0.0f, r.getBottom());
                std::vector<int> along;
                for (int i = 0; i < kPoints; ++i)
                {
                    const int at = high ? kPoints - 1 - i : i;
                    if (skirt (freqs[(size_t) at], fc, order, high) > -0.1f)
                        break;
                    along.push_back (at);
                    p.lineTo (xOf (freqs[(size_t) at]), yOf (total[(size_t) at]));
                }
                for (auto it = along.rbegin(); it != along.rend(); ++it)
                    p.lineTo (xOf (freqs[(size_t) *it]), yOf (juce::jmax (-kEqRange - 1.0f, total[(size_t) *it] + skirt (freqs[(size_t) *it], fc, order, high))));
                p.closeSubPath();
                g.setColour (colour);
                g.fillPath (p);
            };
            cut (f.lowCutFreq.load(), juce::jlimit (1, 4, f.lowCutSlope.load() + 1), false, f.lowCut.load() ? kGreen : juce::Colour (0xff262c44));
            cut (f.highCutFreq.load(), juce::jlimit (1, 4, f.highCutSlope.load() + 1), true, f.highCut.load() ? kGreen.withMultipliedSaturation (0.4f) : juce::Colour (0xff262c44));
        }

        // The RTA: purple, its peaks in a thin line over it.
        if (rta != nullptr)
        {
            const auto yOfLevel = [&r] (float db) { return r.getBottom() - r.getHeight() * 0.72f * juce::jlimit (0.0f, 1.0f, (db + 72.0f) / 72.0f); };
            juce::Path fill, peaks;
            fill.startNewSubPath (0.0f, r.getBottom());
            bool started = false;
            for (int b = 0; b < Analysis::numBands; ++b)
            {
                const float hz = Analysis::bandHz (b);
                if (hz < 20.0f || hz > 20000.0f)
                    continue;
                fill.lineTo (xOf (hz), yOfLevel (rta->levels[(size_t) b]));
                if (! started)
                    peaks.startNewSubPath (xOf (hz), yOfLevel (rta->peaks[(size_t) b]));
                else
                    peaks.lineTo (xOf (hz), yOfLevel (rta->peaks[(size_t) b]));
                started = true;
            }
            fill.lineTo (r.getRight(), r.getBottom());
            fill.closeSubPath();
            g.setColour (juce::Colour (0xff6d2bb0));
            g.fillPath (fill);
            g.setColour (juce::Colour (0xffe8604a));
            g.strokePath (peaks, juce::PathStrokeType (1.0f));
        }

        juce::Path line;
        for (int i = 0; i < kPoints; ++i)
        {
            const juce::Point<float> pt (xOf (freqs[(size_t) i]), yOf (total[(size_t) i]));
            if (i == 0)
                line.startNewSubPath (pt);
            else
                line.lineTo (pt);
        }
        g.setColour (kYellow.withAlpha (on ? 1.0f : 0.5f));
        g.strokePath (line, juce::PathStrokeType (1.8f));

        if (handles && hasBands)
            for (int b = 0; b < 4; ++b)
            {
                const auto ring = juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ xOf (bandFreq (b)), yOf (bandGain (b)) });
                if (b == selected)
                {
                    g.setColour (kBands[b]);
                    g.fillEllipse (ring);
                }
                g.setColour (kBands[b].brighter (0.3f));
                g.drawEllipse (ring, 2.0f);
            }
    }

    if (handles)
    {
        for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f })
            text (g, { 5.0f, yOf (db) - 8.0f, 28.0f, 16.0f }, juce::String ((int) std::abs (db)), 12.0f, juce::Colours::white);
        for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
            text (g, { xOf (hz) - 22.0f, r.getBottom() - 18.0f, 44.0f, 16.0f }, hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz), 12.0f, juce::Colours::white, juce::Justification::centred);
    }
    if (title.isNotEmpty())
        text (g, { 6.0f, 3.0f, r.getWidth() - 12.0f, 16.0f }, title, 12.5f, juce::Colours::white, juce::Justification::centredLeft, true);
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    dragging = -1;
    if (! handles && onTap)
    {
        onTap();
        return;
    }
    if (! handles || strip == nullptr || ! hasBands)
        return;
    float best = 900.0f;
    for (int b = 0; b < 4; ++b)
    {
        const float dx = xOf (bandFreq (b)) - (float) e.x, dy = yOf (bandGain (b)) - (float) e.y;
        if (dx * dx + dy * dy < best)
        {
            best = dx * dx + dy * dy;
            dragging = b;
        }
    }
    if (dragging >= 0)
        selectBand (dragging);
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0 || strip == nullptr)
        return;
    auto& eq = strip->params.eq();
    const auto& m = models::model ((models::Table) strip->getEqTable(), eq.model.load());
    const juce::String pre (kBandPrefix[dragging]);
    if (const int f = m.find (pre + "f"); f >= 0)
    {
        const float hz = 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (float) e.x / (float) getWidth()));
        eq.values[f] = juce::jlimit (m.params[(size_t) f].min, m.params[(size_t) f].max, hz);
    }
    if (const int gi = m.find (pre + "g"); gi >= 0 && ! bandIsCut (dragging))
    {
        const float db = kEqRange * (1.0f - 2.0f * (float) e.y / (float) getHeight());
        eq.values[gi] = juce::jlimit (m.params[(size_t) gi].min, m.params[(size_t) gi].max, db);
    }
}

void EqGraph::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (strip == nullptr || ! hasBands)
        return;
    auto& eq = strip->params.eq();
    const auto& m = models::model ((models::Table) strip->getEqTable(), eq.model.load());
    if (const int q = m.find (juce::String (kBandPrefix[selected]) + "q"); q >= 0)
    {
        const auto& p = m.params[(size_t) q];
        eq.values[q] = p.fromNormalised (juce::jlimit (0.0f, 1.0f, p.toNormalised (eq.values[q].load()) + (w.deltaY > 0 ? 0.02f : -0.02f)));
    }
}

//==============================================================================
BandPanels::BandPanels (UiContext& c)
{
    for (int b = 0; b < 4; ++b)
    {
        graphs[(size_t) b] = std::make_unique<EqGraph> (c);
        graphs[(size_t) b]->handles = false;
        graphs[(size_t) b]->onlyBand = b;
        graphs[(size_t) b]->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*graphs[(size_t) b]);
    }
}

void BandPanels::setStrip (Strip* s)
{
    strip = s;
    for (auto& g : graphs)
        g->setStrip (s);
    repaint();
}

void BandPanels::update()
{
    for (auto& g : graphs)
        g->update();
    repaint();
}

void BandPanels::resized()
{
    const int w = getWidth() / 4;
    for (int b = 0; b < 4; ++b)
        graphs[(size_t) b]->setBounds (b * w + 5, 30, w - 10, getHeight() - 34);
}

void BandPanels::paint (juce::Graphics& g)
{
    g.fillAll (kBack);
    static const char* const names[] = { "LF", "LM", "HM", "HF" };
    const int w = getWidth() / 4;
    for (int b = 0; b < 4; ++b)
    {
        const auto r = juce::Rectangle<int> (b * w + 1, 0, w - 2, getHeight()).toFloat();
        const auto colour = b == 0 ? juce::Colour (0xff7a4fe0) : kBands[b];
        g.setColour (colour);
        g.fillRect (r);
        auto head = r.withHeight (30.0f);
        text (g, head, names[b], 20.0f, juce::Colours::white, juce::Justification::centred);

        juce::Path diamond;
        const auto c = juce::Point<float> (r.getX() + 20.0f, head.getCentreY());
        diamond.addTriangle (c.x - 8.0f, c.y, c.x, c.y - 7.0f, c.x + 8.0f, c.y);
        diamond.addTriangle (c.x - 8.0f, c.y, c.x, c.y + 7.0f, c.x + 8.0f, c.y);
        g.setColour (juce::Colours::white);
        g.strokePath (diamond, juce::PathStrokeType (1.8f));
        g.drawLine (c.x - 14.0f, c.y, c.x - 8.0f, c.y, 1.8f);
        g.drawLine (c.x + 8.0f, c.y, c.x + 14.0f, c.y, 1.8f);

        if (strip != nullptr)
        {
            auto width = bindEq (*strip, juce::String (kBandPrefix[b]) + "q", "Width");
            auto& eq = strip->params.eq();
            const auto& m = models::model ((models::Table) strip->getEqTable(), eq.model.load());
            const int q = m.find (juce::String (kBandPrefix[b]) + "q");
            if (width.isValid() && q >= 0)
            {
                text (g, head.reduced (6.0f, 0.0f), widthText (eq.values[q].load()), 15.0f, juce::Colours::white, juce::Justification::centredRight);
                text (g, head.withTrimmedRight (38.0f), "Width:", 11.5f, juce::Colours::white, juce::Justification::centredRight);
            }
        }
    }
}

//==============================================================================
void TransferGraph::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff232634));
    g.fillRect (r);
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    for (int i = 1; i < 4; ++i)
    {
        g.drawVerticalLine ((int) (r.getWidth() * (float) i / 4.0f), 0.0f, r.getBottom());
        g.drawHorizontalLine ((int) (r.getHeight() * (float) i / 4.0f), 0.0f, r.getRight());
    }
    if (dyn == nullptr)
        return;
    constexpr int n = 48;
    constexpr float lo = -72.0f, hi = 18.0f;
    std::array<float, n> in {}, out {};
    for (int i = 0; i < n; ++i)
        in[(size_t) i] = lo + (hi - lo) * (float) i / (n - 1);
    std::array<float, ld::kMaxDynParams> values {};
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = dyn->values[i].load();
    if (! ld::ld_dyn_static_curve (dyn->model.load(), values.data(), (int32_t) values.size(), in.data(), out.data(), n))
        return;
    juce::Path area, line;
    area.startNewSubPath (0.0f, r.getBottom());
    for (int i = 0; i < n; ++i)
    {
        const float x = r.getWidth() * (in[(size_t) i] - lo) / (hi - lo);
        const float y = r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (out[(size_t) i] - lo) / (hi - lo));
        area.lineTo (x, y);
        if (i == 0)
            line.startNewSubPath (x, y);
        else
            line.lineTo (x, y);
    }
    area.lineTo (r.getRight(), r.getBottom());
    area.closeSubPath();
    g.setColour (juce::Colour (0xff6f8496).withAlpha (dyn->on.load() ? 0.9f : 0.4f));
    g.fillPath (area);
    g.setColour (dyn->on.load() ? juce::Colour (0xffe8eef2) : juce::Colour (0xff9aa6b0));
    g.strokePath (line, juce::PathStrokeType (1.6f));
}

//==============================================================================
MiniFaders::MiniFaders (UiContext& c, Desk& d) : ctx (c), desk (d)
{
    startTimerHz (8);
}

Strip* MiniFaders::stripAt (int column) const
{
    const auto& list = ctx.engine.getChannels();
    const int i = first + column;
    return juce::isPositiveAndBelow (i, (int) list.size()) ? list[(size_t) i].get() : nullptr;
}

int MiniFaders::columnAt (int x) const
{
    const float w = ((float) getWidth() - 56.0f) / (float) kColumns;
    return juce::jlimit (0, kColumns - 1, (int) (((float) x - 28.0f) / w));
}

float MiniFaders::levelDb (const Strip& st) const
{
    const int bus = desk.getMixOnFaders();
    if (bus >= 0 && st.isInput() && desk.getRotaryFunction() != RotaryFunction::Sends)
        return st.sends[(size_t) bus].levelDb.load();
    return st.params.faderDb.load();
}

void MiniFaders::timerCallback()
{
    if (! isVisible())
        return;
    juce::String s;
    for (int c = 0; c < kColumns; ++c)
        if (auto* st = stripAt (c))
            s << juce::roundToInt (levelDb (*st) * 2.0f) << st->getMuteState() << st->getColourIndex() << st->getName();
    if (s != shown)
    {
        shown = s;
        repaint();
    }
}

void MiniFaders::paint (juce::Graphics& g)
{
    g.fillAll (kBack);
    const float w = ((float) getWidth() - 56.0f) / (float) kColumns;
    const float top = 5.0f, faderH = 88.0f;
    for (float side : { 2.0f, (float) getWidth() - 25.0f })
    {
        // At the dB they say: where the console's fader law puts them.
        const float slotTop = top + 6.0f, slotH = faderH - 8.0f;
        const std::pair<const char*, float> marks[] = { { "+10", 10.0f }, { "0", 0.0f }, { "-10", -10.0f }, { "-20", -20.0f } };
        for (auto [t, db] : marks)
            text (g, { side, slotTop + slotH * (1.0f - fader::dbToPos (db)) - 6.0f, 24.0f, 13.0f }, t, 10.0f, juce::Colours::white, juce::Justification::centred);
    }
    for (int c = 0; c < kColumns; ++c)
    {
        auto* st = stripAt (c);
        const float x = 28.0f + (float) c * w;
        if (st == nullptr)
            continue;
        const auto colour = st->getColour();
        const auto column = juce::Rectangle<float> (x + 1.5f, top, w - 3.0f, faderH + 4.0f);
        g.setColour (colour.getPerceivedBrightness() < 0.12f ? juce::Colour (0xff38404e) : colour);
        g.fillRoundedRectangle (column, 5.0f);
        const auto slot = juce::Rectangle<float> (column.getX() + 3.5f, top + 6.0f, column.getWidth() * 0.46f, faderH - 8.0f);
        g.setColour (juce::Colour (0xff0b0c12));
        g.fillRoundedRectangle (slot, 2.0f);
        const float pos = fader::dbToPos (levelDb (*st));
        const float capY = slot.getBottom() - slot.getHeight() * pos;
        g.setColour (juce::Colour (0xffcbd4dc));
        g.fillRect (slot.getRight() + 2.0f, capY - 7.0f, 7.0f, 14.0f);
        text (g, { x - 2.0f, top + faderH + 6.0f, w + 4.0f, 15.0f }, st->getLabel().removeCharacters (" ").replace ("Ch", "Ip"), 11.0f, juce::Colours::white, juce::Justification::centred);

        // The strip's mute key, its name turned on its side.
        const auto key = juce::Rectangle<float> (x + 2.0f, top + faderH + 23.0f, w - 4.0f, (float) getHeight() - (top + faderH + 28.0f));
        const bool muted = st->getMuteState() != 0;
        g.setColour (muted ? juce::Colour (0xfff0606a) : juce::Colour (0xff12141f));
        g.fillRoundedRectangle (key, 4.0f);
        g.setColour (muted ? juce::Colour (0xffffb0b0) : colour.getPerceivedBrightness() < 0.12f ? juce::Colour (0xff70808f) : colour);
        g.drawRoundedRectangle (key, 4.0f, 1.4f);
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, key.getCentreX(), key.getCentreY()));
        const auto turned = juce::Rectangle<float> (key.getHeight(), key.getWidth()).withCentre (key.getCentre());
        text (g, turned.reduced (2.0f, 0.0f), nameOf (*st).substring (0, 8), 12.0f, juce::Colours::white, juce::Justification::centred, true);
    }
}

void MiniFaders::mouseDown (const juce::MouseEvent&)
{
    dragFirst = first;
}

void MiniFaders::mouseDrag (const juce::MouseEvent& e)
{
    const float w = ((float) getWidth() - 56.0f) / (float) kColumns;
    const int count = (int) ctx.engine.getChannels().size();
    const int next = juce::jlimit (0, juce::jmax (0, count - kColumns), dragFirst - juce::roundToInt ((float) e.getDistanceFromDragStartX() / w));
    if (next != first)
    {
        first = next;
        shown.clear();
        repaint();
    }
}

void MiniFaders::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown())
        return;
    auto* st = stripAt (columnAt (e.x));
    if (st == nullptr)
        return;
    if (e.y > 118)
        ctx.engine.pressMute (*st);
    else if (ctx.selectStrip)
        ctx.selectStrip (st);
    shown.clear();
    repaint();
}

//==============================================================================
ProcessingScreen::ProcessingScreen (UiContext& c, Desk& d)
    : ctx (c), desk (d), gainKnob (kRed), trimKnob (kKnobYellow), hpf ("HPF", false), lpf ("LPF", true), graph (c), wideCurve (c), bandPanels (c), faders (c, d)
{
    gainKnob.title = "Gain";
    gainKnob.showValue = true;
    gainKnob.valueColour = juce::Colour (0xffffb040);
    gainKnob.onTapped = [this] { showTab (Tab::Preamp); };
    trimKnob.title = "Trim";
    trimKnob.showValue = true;
    trimKnob.onTapped = [this] { showTab (Tab::Preamp); };
    hpf.onTapped = [this] { showTab (Tab::Filter); };
    lpf.onTapped = [this] { showTab (Tab::Filter); };
    hpf.isOn = [this] { return strip != nullptr && strip->isInput() && strip->params.filter().lowCut.load(); };
    lpf.isOn = [this] { return strip != nullptr && strip->isInput() && strip->params.filter().highCut.load(); };
    for (auto* t : std::initializer_list<Touchable*> { &gainKnob, &trimKnob, &hpf, &lpf })
    {
        t->onTouched = [this] (Touchable& x) { touch (x); };
        addAndMakeVisible (t);
    }
    addAndMakeVisible (graph);
    wideCurve.handles = false;
    wideCurve.onTap = [this] { showTab (Tab::Peq); }; // the curve opens the EQ page
    addAndMakeVisible (wideCurve);
    addChildComponent (bandPanels);
    addChildComponent (faders);
    addChildComponent (transfer);
    addAndMakeVisible (soft);
    addChildComponent (pageArea);
    soft.onTouched = [this] (Touchable& t) { touch (t); };
    soft.onLayerTapped = [this] (int l) { setLayer (l); };
    soft.onBlockTapped = [this] (int block)
    {
        if (layer == 0)
            showTab (block == 0 ? Tab::Gate : Tab::Comp);
    };
    startTimerHz (20);
}

ProcessingScreen::~ProcessingScreen()
{
    if (rtaShowing)
        Analysis (ctx.engine.getHandle()).hideRta();
}

bool ProcessingScreen::hasTab (Tab t) const
{
    for (const auto& [k, b] : tabs)
        if (k == t)
            return true;
    return false;
}

void ProcessingScreen::touch (Touchable& t)
{
    if (touchedNow != nullptr && touchedNow.getComponent() != &t)
        touchedNow->setTouched (false);
    touchedNow = &t;
    t.setTouched (true);
    if (onTouchTurn)
        onTouchTurn (t.getBinding());
}

void ProcessingScreen::rebuildTabs()
{
    tabs.clear();
    if (strip == nullptr)
        return;
    using T = Tab;
    std::vector<std::pair<T, const char*>> list;
    switch (strip->getKind())
    {
        case StripKind::Channel:
            list = { { T::Bank, "Bank" }, { T::Overview, "Overview" }, { T::Preamp, "Preamp" }, { T::Filter, "Filter" }, { T::Gate, "Gate" },
                     { T::InsA, "Ins A" }, { T::Peq, "PEQ" }, { T::Comp, "Comp" }, { T::InsB, "Ins B" }, { T::Delay, "Delay" }, { T::Routing, "Routing" } };
            break;
        case StripKind::Aux: // photographed on a C1500
            list = { { T::Bank, "Bank" }, { T::FxUnit, "FX Unit" }, { T::Peq, "PEQ" }, { T::Ufx, "UFX" } };
            break;
        default: // a mix's tabs as the guide's picture of Ext In (1.6) has them, and Ins B
            list = { { T::Bank, "Bank" }, { T::Overview, "Overview" }, { T::ExtIn, "Ext In" }, { T::InsA, "Ins A" }, { T::Peq, "PEQ" }, { T::Geq, "GEQ" }, { T::Comp, "Comp" }, { T::InsB, "Ins B" },
                     { T::Delay, "Delay" }, { T::Routing, "Routing" } };
            break;
    }
    if (! hasTab (tab) && tab != T::Ufx) // the UFX page is the console's, reached from Overview too
    {
        bool found = false;
        for (auto& [t, n] : list)
            found = found || t == tab;
        if (! found)
            tab = T::Peq;
    }
    for (auto& [t, name] : list)
    {
        auto b = std::make_unique<ScreenTab> (name);
        b->isOn = [this, target = t] { return tab == target; };
        b->onClick = [this, target = t] { showTab (target); };
        b->refresh();
        addChildComponent (*b);
        b->setVisible (hostedPage == nullptr);
        tabs.emplace_back (t, std::move (b));
    }
    resized();
}

void ProcessingScreen::bindFixed()
{
    for (auto* t : std::initializer_list<Touchable*> { &gainKnob, &trimKnob, &hpf, &lpf })
    {
        t->setBinding ({});
        t->setVisible (false);
    }
    if (strip == nullptr || ! strip->isInput())
    {
        if (strip != nullptr)
        {
            trimKnob.setBinding (bindLinear ("Trim", strip->params.input.trimDb, -24.0f, 24.0f, " dB", 1, 0.0f));
            trimKnob.setVisible (true);
        }
        return;
    }
    auto& p = strip->params;
    const int in = p.inputLeft.load();
    if (auto* src = in >= 0 && in < 128 ? ctx.engine.getSource (in) : nullptr)
    {
        auto b = bindLinear ("Gain", src->gainDb, 0.0f, 60.0f, " dB", 0, 0.0f);
        b.steps = 61.0f;
        gainKnob.setBinding (std::move (b));
        gainKnob.setVisible (true);
    }
    trimKnob.setBinding (bindLinear ("Trim", p.input.trimDb, -24.0f, 24.0f, " dB", 1, 0.0f));
    trimKnob.setVisible (true);
    hpf.setBinding (asFreq (bindFreq ("HPF", p.filter().lowCutFreq, 20.0f, 2000.0f, 20.0f), p.filter().lowCutFreq));
    hpf.setVisible (true);
    lpf.setBinding (asFreq (bindFreq ("LPF", p.filter().highCutFreq, 500.0f, 20000.0f, 20000.0f), p.filter().highCutFreq));
    lpf.setVisible (true);
}

void ProcessingScreen::buildBlocks()
{
    SoftWidget::Block a, b;
    if (strip != nullptr && layer == 0)
    {
        auto& p = strip->params;
        a.left = b.left = nameOf (*strip);
        a.right = b.right = strip->getLabel();
        if (strip->getKind() == StripKind::Channel)
        {
            auto& gate = p.gate();
            a.title = "Gate";
            a.thumb = 1;
            a.dyn = &gate;
            a.in = &gate.on;
            a.levelDb = [this] { return strip != nullptr ? juce::Decibels::gainToDecibels (ctx.meters.get (strip, MeterPoint::Trim, 0), -100.0f) : -100.0f; };
            a.reductionDb = [this] { return strip != nullptr ? strip->meters.gateReductionDb.load() : 0.0f; };
            const auto gateName = models::model (models::Dynamics, gate.model.load()).name;
            a.thumbText = gateName == "Source Expander" ? "SRC\nEXP" : gateName == "Ducker" ? "DUCK" : gateName == "Dual Threshold Expander" ? "DUAL\nEXP" : "";
            a.controls[1] = { "Thres", {}, bindDyn (gate, "thr", "Thres"), kKnobCyan };
            a.controls[2] = { "Depth", {}, bindDyn (gate, "range", "Depth"), kRed };
        }
        if (strip->getKind() != StripKind::Aux)
        {
            auto& dyn = p.dynamics();
            b.title = "Comp";
            b.thumb = 2;
            b.dyn = &dyn;
            b.in = &dyn.on;
            b.levelDb = [this] { return strip != nullptr ? juce::Decibels::gainToDecibels (ctx.meters.get (strip, MeterPoint::PreFader, 0), -100.0f) : -100.0f; };
            b.reductionDb = [this] { return strip != nullptr ? strip->meters.dynReductionDb.load() : 0.0f; };
            b.thumbText = models::model (models::Dynamics, dyn.model.load()).name.fromLastOccurrenceOf (" ", false, false); // its detector: Peak, RMS
            b.controls[0] = { "Ratio", {}, bindDyn (dyn, "ratio", "Ratio"), kKnobCyan };
            b.controls[1] = { "Thres", {}, bindDyn (dyn, "thr", "Thres"), kKnobGreen };
            b.controls[2] = { "Gain", {}, bindDyn (dyn, "gain", "Gain"), kRed };
        }
    }
    else if (strip != nullptr)
        sendBlocks (ctx.engine, strip, (layer - 1) * 6, a, b);
    soft.set (a, b, layer, 3, layer == 0);
    if (onSoftChanged)
        onSoftChanged();
}

void ProcessingScreen::setLayer (int l)
{
    layer = juce::jlimit (0, 2, l);
    buildBlocks();
}

void ProcessingScreen::setStrip (Strip* s)
{
    strip = s;
    gateModel = compModel = -1;
    graph.setStrip (s);
    wideCurve.setStrip (s);
    bandPanels.setStrip (s);
    rebuildTabs();
    bindFixed();
    buildBlocks();
    loadExtIn();
    buildTab();
    repaint();
}

void ProcessingScreen::refreshAll()
{
    setStrip (strip);
}

void ProcessingScreen::showTab (Tab t)
{
    tab = t;
    if (t == Tab::ExtIn)
        loadExtIn();
    if (hostedPage != nullptr && onWantsProcessing)
        onWantsProcessing();
    for (auto& [k, b] : tabs)
        b->refresh();
    buildTab();
    repaint();
}

void ProcessingScreen::toggleView()
{
    setView ((view + 1) % 3);
}

void ProcessingScreen::setView (int v)
{
    view = juce::jlimit (0, 2, v);
    wideCurve.setVisible (view == 0);
    bandPanels.setVisible (view == 1);
    faders.setVisible (view == 2);
}

void ProcessingScreen::hostPage (juce::Component* page, juce::Rectangle<int> source)
{
    if (hostedPage != nullptr && hostedPage != page)
    {
        hostedPage->setVisible (false);
        pageArea.removeChildComponent (hostedPage);
    }
    hostedPage = page;
    pageArea.setVisible (page != nullptr);
    if (page != nullptr)
    {
        pageArea.addAndMakeVisible (page);
        page->setBounds (0, 0, kTouchW, kTouchH);
        page->setVisible (true);
        // Only narrowed: the C1500's page is as tall as the C3500's but not as wide.
        const float sx = (float) pageArea.getWidth() / (float) source.getWidth();
        page->setTransform (juce::AffineTransform::translation ((float) -source.getX(), (float) -source.getY()).scaled (sx, 1.0f));
    }
    for (auto& [t, b] : tabs)
        b->setVisible (page == nullptr);
    buildTab();
    resized();
    repaint();
}

juce::String ProcessingScreen::helpKey() const
{
    const char* name = "Peq";
    switch (tab)
    {
        case Tab::Bank:     name = "Bank"; break;
        case Tab::Overview: name = "Overview"; break;
        case Tab::FxUnit:   name = "FxUnit"; break;
        case Tab::Preamp:   name = "Preamp"; break;
        case Tab::Filter:   name = "Filter"; break;
        case Tab::Gate:     name = "Gate"; break;
        case Tab::InsA:
        case Tab::InsB:     name = "Insert"; break;
        case Tab::Peq:      name = "Peq"; break;
        case Tab::Geq:      name = "Geq"; break;
        case Tab::Comp:     name = "Comp"; break;
        case Tab::Delay:    name = "Delay"; break;
        case Tab::ExtIn:    name = "ExtIn"; break;
        case Tab::Routing:  name = "Routing"; break;
        case Tab::Ufx:      name = "Ufx"; break;
    }
    return juce::String ("Processing/") + name;
}

ParamBox& ProcessingScreen::addBox (juce::Rectangle<int> r, KnobBinding b, juce::Colour c, const juce::String& caption)
{
    auto box = std::make_unique<ParamBox> (c, caption);
    box->setBinding (std::move (b));
    box->setBounds (r);
    box->onTouched = [this] (Touchable& x) { touch (x); };
    addAndMakeVisible (*box);
    auto& ref = *box;
    items.push_back (std::move (box));
    return ref;
}

ScreenButton& ProcessingScreen::addButton (juce::Rectangle<int> r, const juce::String& name, juce::Colour c, std::function<void()> click)
{
    auto b = std::make_unique<ScreenButton> (name, c);
    b->setBounds (r);
    b->onClick = std::move (click);
    addAndMakeVisible (*b);
    auto& ref = *b;
    items.push_back (std::move (b));
    return ref;
}

ScreenButton& ProcessingScreen::addToggle (juce::Rectangle<int> r, const juce::String& name, juce::Colour c, std::atomic<bool>& state)
{
    auto& b = addButton (r, name, c, [&state] { state = ! state.load(); });
    b.isOn = [&state] { return state.load(); };
    b.litFill = c.darker (0.3f);
    return b;
}

void ProcessingScreen::buildTab()
{
    if (touchedNow != nullptr)
        touchedNow->setTouched (false);
    touchedNow = nullptr;
    for (auto& i : items)
    {
        i->setVisible (false);
        retired.push_back (std::move (i));
    }
    items.clear();
    const bool withGraph = hostedPage == nullptr && strip != nullptr && (tab == Tab::Peq || tab == Tab::Filter);
    graph.setVisible (withGraph);
    transfer.setVisible (false);
    if (strip != nullptr)
    {
        const auto source = "Source: " + (strip->getKind() == StripKind::Channel ? "Input " + juce::String (strip->getLabel().retainCharacters ("0123456789")) : strip->getLabel());
        graph.title = "PEQ+RTA1  " + source;
        wideCurve.title = "PEQ+RTA1  " + source;
    }
    if (strip != nullptr && hostedPage == nullptr)
        switch (tab)
        {
            case Tab::Peq:      buildPeq (*strip); break;
            case Tab::Filter:   buildFilter (*strip); break;
            case Tab::Preamp:   buildPreamp (*strip); break;
            case Tab::Gate:     buildDyn (*strip, strip->params.gate(), ModelBlock::Gate, 0); break;
            case Tab::Comp:     buildDyn (*strip, strip->params.dynamics(), ModelBlock::Dynamics, 1); break;
            case Tab::InsA:     buildInsert (strip->params.preInsert()); break;
            case Tab::InsB:     buildInsert (strip->params.postInsert()); break;
            case Tab::Delay:    buildDelay (*strip); break;
            case Tab::Overview: buildOverview (*strip); break;
            case Tab::Bank:     buildBank(); break;
            case Tab::Geq:      buildGeq (*strip); break;
            case Tab::ExtIn:    buildExtIn (*strip); break;
            case Tab::Routing:  buildRouting (*strip); break;
            case Tab::Ufx:      buildUfx(); break;
            case Tab::FxUnit:
                addButton ({ 380, 220, 240, 64 }, "Open this FX unit", kCyan, [this] { if (onFxUnit) onFxUnit(); });
                break;
        }
}

void ProcessingScreen::buildPeq (Strip& s)
{
    auto& eq = s.params.eq();
    const auto& m = models::model ((models::Table) s.getEqTable(), eq.model.load());
    const auto modeOf = [&] (const char* id) { const int i = m.find (id); return i >= 0 ? juce::roundToInt (eq.values[i].load()) : 1; };
    const auto setMode = [this, &eq, &m] (const char* id, int mode)
    {
        const int i = m.find (id);
        if (i >= 0)
            eq.values[i] = (float) mode;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ProcessingScreen> (this)] { if (safe != nullptr) safe->showTab (Tab::Peq); });
    };

    addToggle ({ 133, 464, 29, 53 }, "In", kGreen, eq.on).fontSize = 12.0f;
    if (m.find ("1g") < 0)
    {
        // Not the four-band PEQ (NEQ-12 on a mix): its controls, in rows.
        int n = 0;
        for (int i = 0; i < (int) m.params.size() && n < 36; ++i, ++n)
            addBox ({ 170 + (n % 9) * 75, 348 + (n / 9) * 42, 72, 40 }, bindModelParam (eq.values[i], m.params[(size_t) i]), kCyan);
        return;
    }

    const int rows[] = { 365, 414, 466 };
    const int columns[] = { 276, 386, 497, 608 };
    const char* lowShapes[] = { "Shelf", "Bell", "HPF" };
    const char* highShapes[] = { "Shelf", "Bell", "LPF" };
    for (int b = 0; b < 4; ++b)
    {
        const juce::String pre (kBandPrefix[b]);
        const bool bell = b == 1 || b == 2 || modeOf (b == 0 ? "lm" : "hm") == 1;
        const bool cut = (b == 0 || b == 3) && modeOf (b == 0 ? "lm" : "hm") == 2;
        addBox ({ columns[b], rows[0], 77, 36 }, asFreq (bindEq (s, pre + "f", "Freq"), eq.values[m.find (pre + "f")]), kBands[b].brighter (0.2f));
        if (bell)
        {
            auto width = bindEq (s, pre + "q", "Width");
            width.text = [&value = eq.values[m.find (pre + "q")]] { return widthText (value.load()); };
            addBox ({ columns[b], rows[1], 77, 36 }, std::move (width), kBands[b].brighter (0.2f));
        }
        if (! cut)
            addBox ({ columns[b], rows[2], 77, 36 }, bindEq (s, pre + "g", "Gain"), kBands[b].brighter (0.2f));
    }
    for (int side = 0; side < 2; ++side)
    {
        const char* id = side == 0 ? "lm" : "hm";
        const int x = side == 0 ? 215 : 705;
        for (int mode = 0; mode < 3; ++mode)
        {
            auto& k = addButton ({ x, rows[mode], 42, 36 }, side == 0 ? lowShapes[mode] : highShapes[mode], kBands[side == 0 ? 0 : 3], [setMode, id, mode] { setMode (id, mode); });
            k.fontSize = 10.5f;
            k.isOn = [&eq, i = m.find (id), mode] { return i >= 0 && juce::roundToInt (eq.values[i].load()) == mode; };
            k.litFill = kBands[side == 0 ? 0 : 3].darker (0.45f);
        }
    }
    addButton ({ 777, 365, 70, 50 }, "PEQ\nLibrary", kTeal, [this] { if (onLibrary) onLibrary(); }).fontSize = 13.0f;
}

void ProcessingScreen::buildDyn (Strip& s, ld::DynSlotParams& d, ModelBlock block, int slot)
{
    juce::ignoreUnused (s);
    const int model = d.model.load();
    const auto& m = models::model (models::Dynamics, model);
    auto& pick = addButton ({ 140, 44, 240, 34 }, m.name, kCyan, {});
    pick.onClick = [this, &d, block, slot, &pick]
    {
        juce::PopupMenu menu;
        const auto& list = models::table (models::Dynamics);
        for (int i = 0; i < (int) list.size(); ++i)
            if (ld::ld_dyn_model_allowed (i, slot))
                menu.addItem (i + 1, list[(size_t) i].name, true, i == d.model.load());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pick),
                            [safe = juce::Component::SafePointer<ProcessingScreen> (this), block] (int r)
                            {
                                if (safe == nullptr || r <= 0 || safe->strip == nullptr)
                                    return;
                                safe->strip->setModel (block, r - 1);
                                safe->buildBlocks();
                                safe->buildTab();
                            });
    };
    addToggle ({ 390, 44, 70, 34 }, "In", kGreen, d.on);
    transfer.setDyn (&d);
    transfer.setBounds (140, 90, 280, 280);
    transfer.setVisible (true);
    int n = 0;
    for (int i = 0; i < (int) m.params.size() && n < 12; ++i, ++n)
        addBox ({ 440 + (n % 3) * 140, 90 + (n / 3) * 76, 132, 64 }, bindModelParam (d.values[i], m.params[(size_t) i]), kCyan, m.params[(size_t) i].label);
}

void ProcessingScreen::buildPreamp (Strip& s)
{
    auto& p = s.params;
    const int in = p.inputLeft.load();
    addButton ({ 150, 60, 200, 40 }, in >= 0 ? "Socket " + juce::String (in + 1) : juce::String ("Unassigned"), kCyan, {}).setEnabled (false);
    if (auto* src = in >= 0 && in < 128 ? ctx.engine.getSource (in) : nullptr)
    {
        auto gain = bindLinear ("Gain", src->gainDb, 0.0f, 60.0f, " dB", 0, 0.0f);
        gain.steps = 61.0f;
        addBox ({ 150, 130, 150, 66 }, std::move (gain), kRed, "Gain");
        addToggle ({ 320, 149, 90, 47 }, "48V", kRed, src->phantom);
    }
    addBox ({ 150, 220, 150, 66 }, bindLinear ("Trim", p.input.trimDb, -24.0f, 24.0f, " dB", 1, 0.0f), kKnobYellow, "Trim");
    addToggle ({ 320, 239, 90, 47 }, "Polarity", kCyan, p.input.invert);
    auto* d = ld::ld_strip_dlive (s.getHandle());
    if (p.stereo.load() && d != nullptr)
    {
        // Stereo Image (the guide's Preamp page): the mode, and the width from mono to stereo.
        addBox ({ 150, 310, 150, 66 }, bindChoice ("Mode", d->stereoImage, { "L/R", "R/L", "L-Pol/R", "R-Pol/L", "Mono", "L/L", "R/R", "M/S" }, 0), kCyan, "Stereo Image");
        addBox ({ 320, 310, 130, 66 }, bindLinear ("Width", d->imageWidth, 0.0f, 100.0f, " %", 0, 100.0f), kCyan, "Width");
    }
    if (d == nullptr || s.getKind() != StripKind::Channel)
        return;

    // ABCD inputs (the guide's Preamp page): Enable ABCD, the input the channel plays, and
    // each one's socket. The console opens ABCD Source Setup for the sockets; Sidecar keeps
    // the four on the page, under their keys.
    auto* e = ctx.engine.getHandle();
    auto* h = s.getHandle();
    const bool on = d->abcdOn.load();
    auto& enable = addButton ({ 480, 60, 174, 40 }, on ? "Disable ABCD" : "Enable ABCD", kCyan, [this, e, h, d]
    {
        ld::ld_engine_abcd_enable (e, h, ! d->abcdOn.load());
        bindFixed();
        buildTab();
    });
    enable.isOn = [d] { return d->abcdOn.load(); };
    enable.litFill = kCyan.darker (0.45f);
    for (int i = 0; i < 4; ++i)
    {
        auto& k = addButton ({ 480 + i * 60, 130, 54, 47 }, juce::String::charToString ((juce::juce_wchar) ('A' + i)), kKnobGreen, [this, e, h, i]
        {
            ld::ld_engine_abcd_select (e, h, i);
            bindFixed();
            buildTab();
        });
        k.fontSize = 20.0f;
        k.isOn = [d, i] { return d->abcdOn.load() && d->abcdActive.load() == i; };
        k.litFill = kGreen;
        k.setEnabled (on);
        const int socket = d->abcdLeft[i].load();
        auto& patch = addButton ({ 480 + i * 60, 184, 54, 32 }, socket >= 0 ? juce::String (socket + 1) : juce::String ("-"), kCyan, {});
        patch.fontSize = 13.0f;
        patch.onClick = [this, i, socket, &patch]
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Unassigned", true, socket < 0);
            addSocketItems (menu, 2, socket);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patch),
                                [safe = juce::Component::SafePointer<ProcessingScreen> (this), i] (int r)
                                {
                                    if (safe != nullptr && r > 0)
                                        safe->setAbcdPatch (i, r - 2);
                                });
        };
    }
}

void ProcessingScreen::setAbcdPatch (int which, int socket)
{
    if (strip == nullptr || strip->getKind() != StripKind::Channel || ! juce::isPositiveAndBelow (which, 4))
        return;
    auto* d = ld::ld_strip_dlive (strip->getHandle());
    if (d == nullptr)
        return;
    socket = juce::jlimit (-1, kSockets - 1, socket);
    d->abcdLeft[which] = socket;
    d->abcdRight[which] = socket >= 0 && strip->params.stereo.load() ? socket + 1 : -1;
    if (d->abcdOn.load() && d->abcdActive.load() == which)
        ld::ld_engine_abcd_select (ctx.engine.getHandle(), strip->getHandle(), which); // the channel takes it now
    if (tab == Tab::Preamp)
    {
        bindFixed();
        buildTab();
    }
}

Touchable* ProcessingScreen::findBox (const juce::String& label) const
{
    for (auto& i : items)
        if (auto* box = dynamic_cast<Touchable*> (i.get()); box != nullptr && box->getBinding().label == label)
            return box;
    return nullptr;
}

ScreenButton* ProcessingScreen::findButton (const juce::String& name) const
{
    for (auto& i : items)
        if (auto* b = dynamic_cast<ScreenButton*> (i.get()); b != nullptr && b->getButtonText() == name)
            return b;
    return nullptr;
}

//==============================================================================
int ProcessingScreen::extCount (ExtType t) const
{
    switch (t)
    {
        case ExtType::None:   return 1;
        case ExtType::Socket: return kSockets;
        case ExtType::Mix:    return (int) ctx.engine.getBuses().size();
        case ExtType::Main:   return (int) ctx.engine.getMains().size();
        case ExtType::Matrix: return juce::jmin ((int) ctx.engine.getMatrices().size(), ld::kWingMatrices); // the engine's strip sources stop there
        case ExtType::Pafl:
        {
            int32_t pafl = 0, mainMode = 0;
            return ld::ld_engine_dlive_structure (ctx.engine.getHandle(), &pafl, &mainMode) && pafl > 0 ? 1 : 0;
        }
    }
    return 0;
}

int ProcessingScreen::extInSource (ExtType t, int number) const
{
    // A mix's signal goes by its slot, its left side first (the right is the next input).
    const auto of = [this, number] (const MixerEngine::StripList& list, StripKind kind)
    {
        if (! juce::isPositiveAndBelow (number, (int) list.size()) || list[(size_t) number].get() == strip)
            return -1; // a mix isn't its own Ext In (Sidecar's reading)
        return MixerEngine::stripSource (kind, list[(size_t) number]->getId(), 0);
    };
    switch (t)
    {
        case ExtType::None:   break;
        case ExtType::Socket: return juce::isPositiveAndBelow (number, kSockets) ? number : -1;
        case ExtType::Mix:    return of (ctx.engine.getBuses(), StripKind::Bus);
        case ExtType::Main:   return of (ctx.engine.getMains(), StripKind::Main);
        case ExtType::Matrix: return of (ctx.engine.getMatrices(), StripKind::Matrix);
        case ExtType::Pafl:   return extCount (t) > 0 ? kPaflSource : -1;
    }
    return -1;
}

juce::String ProcessingScreen::extName (ExtType t, int number) const
{
    const auto of = [number] (const MixerEngine::StripList& list)
    {
        return juce::isPositiveAndBelow (number, (int) list.size()) ? nameOf (*list[(size_t) number]) : juce::String();
    };
    switch (t)
    {
        case ExtType::None:   break;
        case ExtType::Socket: return juce::String (number + 1);
        case ExtType::Mix:    return of (ctx.engine.getBuses());
        case ExtType::Main:   return of (ctx.engine.getMains());
        case ExtType::Matrix: return of (ctx.engine.getMatrices());
        case ExtType::Pafl:   return "PAFL";
    }
    return {};
}

void ProcessingScreen::loadExtIn()
{
    extType = ExtType::None;
    extNumber = 0;
    if (strip == nullptr || strip->isInput())
        return;
    const int source = strip->mixInput().extSource.load();
    if (source < 0)
        return;
    StripKind kind = StripKind::Bus;
    int number = 0, side = 0;
    if (source == kPaflSource || source == kPaflSource + 1)
        extType = ExtType::Pafl;
    else if (routing::stripOfSource (source, kind, number, side))
    {
        extType = kind == StripKind::Bus ? ExtType::Mix : kind == StripKind::Main ? ExtType::Main : ExtType::Matrix;
        const auto& list = ctx.engine.getStrips (kind);
        for (int i = 0; i < (int) list.size(); ++i)
            if (list[(size_t) i]->getId() == number)
                extNumber = i;
    }
    else
    {
        extType = ExtType::Socket;
        extNumber = juce::jlimit (0, kSockets - 1, source);
    }
}

void ProcessingScreen::chooseExtIn (ExtType t, int number)
{
    extType = extCount (t) > 0 ? t : ExtType::None;
    extNumber = juce::jlimit (0, juce::jmax (0, extCount (extType) - 1), number);
    if (tab == Tab::ExtIn)
        buildTab();
}

void ProcessingScreen::applyExtIn()
{
    if (strip == nullptr || strip->isInput())
        return;
    const int source = extInSource (extType, extNumber);
    if (source >= 0 || extType == ExtType::None)
        strip->mixInput().extSource = source;
    else
        loadExtIn(); // nothing it can take: as it was
    if (tab == Tab::ExtIn)
        buildTab();
}

void ProcessingScreen::cancelExtIn()
{
    loadExtIn();
    if (tab == Tab::ExtIn)
        buildTab();
}

void ProcessingScreen::buildExtIn (Strip& s)
{
    // As the guide's picture of the tab (1.6): Source Select with its Socket box, Apply and
    // Cancel; the socket's preamp under it once one is patched; Trim and Polarity. SigGen and
    // Talkback to the mix are drawn and dimmed: Sidecar sets those on its own pages.
    if (s.isInput())
        return;
    static const char* const types[] = { "Unassigned", "Socket", "Mix", "Main", "Matrix", "PAFL" };
    auto& mi = s.mixInput();
    extShown = mi.extSource.load();
    auto& pick = addButton ({ 144, 104, 190, 40 }, types[(int) extType], kCyan, {});
    pick.dropdown = true;
    pick.onClick = [this, &pick]
    {
        juce::PopupMenu menu;
        for (int i = 0; i < 6; ++i)
            menu.addItem (i + 1, types[i], extCount ((ExtType) i) > 0, i == (int) extType);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pick),
                            [safe = juce::Component::SafePointer<ProcessingScreen> (this)] (int r)
                            {
                                if (safe != nullptr && r > 0)
                                    safe->chooseExtIn ((ExtType) (r - 1), 0);
                            });
    };
    if (const int count = extCount (extType); count > 1)
    {
        KnobBinding b;
        b.label = extType == ExtType::Socket ? "Socket" : "Mix";
        b.steps = (float) count;
        b.get = [this, count] { return (float) extNumber / (float) (count - 1); };
        b.set = [this, count] (float v) { extNumber = juce::jlimit (0, count - 1, juce::roundToInt (v * (float) (count - 1))); };
        b.text = [this] { return extName (extType, extNumber); };
        addBox ({ 344, 100, 112, 48 }, std::move (b), kCyan).showBar = false;
    }
    addButton ({ 468, 92, 100, 31 }, "Apply", juce::Colours::white, [this] { applyExtIn(); }).fontSize = 14.0f;
    addButton ({ 468, 128, 100, 31 }, "Cancel", juce::Colours::white, [this] { cancelExtIn(); }).fontSize = 14.0f;
    for (int x : { 610, 750 })
    {
        auto& k = addButton ({ x, 94, 88, 58 }, nameOf (s), kTeal, {});
        k.fontSize = 13.0f;
        k.setEnabled (false);
    }

    const int source = mi.extSource.load();
    if (auto* src = source >= 0 && source < 128 ? ctx.engine.getSource (source) : nullptr)
    {
        auto gain = bindLinear ("Gain", src->gainDb, 0.0f, 60.0f, " dB", 0, 0.0f);
        gain.steps = 61.0f;
        addBox ({ 150, 222, 140, 66 }, std::move (gain), kRed, "Gain");
        addToggle ({ 308, 241, 90, 47 }, "48V", kRed, src->phantom);
    }
    addToggle ({ 473, 222, 92, 40 }, "Polarity", kCyan, mi.extInvert);
    addBox ({ 467, 296, 104, 66 }, bindLinear ("Trim", mi.extTrimDb, -24.0f, 24.0f, " dB", 1, 0.0f), kKnobYellow, "Trim");
}

void ProcessingScreen::buildRouting (Strip& s)
{
    // What the console's Routing screen has for the strip and Sidecar's routing page hasn't
    // (the guide's chapter 6). A tab of its own is Sidecar's placing of them.
    auto* d = ld::ld_strip_dlive (s.getHandle());
    auto* e = ctx.engine.getHandle();
    if (d == nullptr || s.getKind() == StripKind::Aux)
        return;
    if (s.getKind() == StripKind::Channel)
    {
        // Direct Out: the channel's level; the source point and what it follows are the console's.
        static const char* const points[] = { "Post Preamp", "Post LPF", "Post Gate", "Post Insert A Return", "Post PEQ", "Post Compressor", "Post Insert B Return", "Post Delay" };
        addBox ({ 150, 100, 150, 66 }, bindFader ("Direct Out", d->directDb), kCyan, "Level");
        auto& source = addButton ({ 330, 119, 300, 44 }, points[juce::jlimit (0, 7, (int) ld::ld_engine_dlive_direct_source (e))], kCyan, {});
        source.dropdown = true;
        source.onClick = [this, e, &source]
        {
            juce::PopupMenu menu;
            const int now = ld::ld_engine_dlive_direct_source (e);
            for (int i = 0; i < 8; ++i)
                menu.addItem (i + 1, points[i], true, i == now);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&source),
                                [safe = juce::Component::SafePointer<ProcessingScreen> (this)] (int r)
                                {
                                    if (safe == nullptr || r <= 0)
                                        return;
                                    ld::ld_engine_dlive_set_direct_source (safe->ctx.engine.getHandle(), r - 1);
                                    safe->buildTab();
                                });
        };
        static const char* const follows[] = { "Follow Fader", "Follow Mute", "Follow All" };
        for (int i = 0; i < 3; ++i)
        {
            auto& k = addButton ({ 150 + i * 162, 196, 150, 44 }, follows[i], kCyan, [e, i] { ld::ld_engine_dlive_set_direct_follow (e, ld::ld_engine_dlive_direct_follow (e) ^ (1 << i)); });
            k.isOn = [e, i] { return (ld::ld_engine_dlive_direct_follow (e) & (1 << i)) != 0; };
            k.litFill = kCyan.darker (0.45f);
        }
        return;
    }

    // A mix: where the channels' signal leaves them for it; a stereo mix can take the channels' own pans.
    static const char* const mixPoints[] = { "Post Preamp", "Post Insert A Return", "Post PEQ", "Post Delay" };
    auto& source = addButton ({ 150, 100, 320, 44 }, mixPoints[juce::jlimit (0, 3, (int) s.mixInput().mixSource.load())], kCyan, {});
    source.dropdown = true;
    source.onClick = [this, &source]
    {
        juce::PopupMenu menu;
        const int now = strip != nullptr ? (int) strip->mixInput().mixSource.load() : -1;
        for (int i = 0; i < 4; ++i)
            menu.addItem (i + 1, mixPoints[i], true, i == now);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&source),
                            [safe = juce::Component::SafePointer<ProcessingScreen> (this)] (int r)
                            {
                                if (safe == nullptr || r <= 0 || safe->strip == nullptr || safe->strip->isInput())
                                    return;
                                safe->strip->mixInput().mixSource = r - 1;
                                safe->buildTab();
                            });
    };
    if (s.params.stereo.load() && (s.getKind() == StripKind::Bus || s.getKind() == StripKind::Matrix))
    {
        auto* h = s.getHandle();
        auto& follow = addButton ({ 518, 100, 166, 44 }, "Follow Main Pan", kCyan, [e, h, d] { ld::ld_engine_set_follow_main_pan (e, h, ! d->followMainPan.load(), d->followInvert.load()); });
        follow.isOn = [d] { return d->followMainPan.load(); };
        follow.litFill = kCyan.darker (0.45f);
        auto& invert = addButton ({ 696, 100, 146, 44 }, "Invert", kCyan, [e, h, d] { ld::ld_engine_set_follow_main_pan (e, h, d->followMainPan.load(), ! d->followInvert.load()); });
        invert.isOn = [d] { return d->followInvert.load(); };
        invert.litFill = kCyan.darker (0.45f);
    }
}

void ProcessingScreen::buildFilter (Strip& s)
{
    if (! s.isInput())
        return;
    auto& f = s.params.filter();
    addToggle ({ 170, 370, 60, 40 }, "In", kGreen, f.lowCut);
    addBox ({ 240, 352, 130, 62 }, asFreq (bindFreq ("HPF", f.lowCutFreq, 20.0f, 2000.0f, 20.0f), f.lowCutFreq), kGreen.brighter (0.4f), "HPF");
    addBox ({ 380, 352, 120, 62 }, bindChoice ("Slope", f.lowCutSlope, { "6 dB", "12 dB", "18 dB", "24 dB" }, 3), kGreen.brighter (0.4f), "Slope");
    addToggle ({ 530, 370, 60, 40 }, "In", kGreen, f.highCut);
    addBox ({ 600, 352, 130, 62 }, asFreq (bindFreq ("LPF", f.highCutFreq, 500.0f, 20000.0f, 20000.0f), f.highCutFreq), juce::Colour (0xffc9d2da), "LPF");
}

void ProcessingScreen::buildInsert (ld::InsertParams& ins)
{
    auto* e = ctx.engine.getHandle();
    const bool post = strip != nullptr && &ins == &strip->params.postInsert();
    addToggle ({ 170, 70, 90, 44 }, "In", kGreen, ins.on);
    const int fx = ins.fx.load();
    shownInsertFx = fx;
    const int dyn = fx >= ld::kDyn8FxBase ? fx - ld::kDyn8FxBase : -1;
    // A Dyn8 engine's list: 64 engines in four menus of 16; one another insert has is dimmed.
    const auto pickDyn8 = [this, post, e] (juce::Component& anchor)
    {
        const int now = strip != nullptr ? (post ? strip->params.postInsert() : strip->params.preInsert()).fx.load() - ld::kDyn8FxBase : -1;
        const auto used = ld::ld_dyn8_in_use (e);
        juce::PopupMenu menu;
        menu.addItem (1, "Unassign", now >= 0);
        for (int group = 0; group < ld::kNumDyn8 / 16; ++group)
        {
            juce::PopupMenu sub;
            for (int i = 0; i < 16; ++i)
            {
                const int n = group * 16 + i;
                const bool taken = ((used >> n) & 1) != 0 && n != now;
                sub.addItem (n + 2, "Dyn8 " + juce::String (n + 1) + (taken ? " (in use)" : ""), ! taken, n == now);
            }
            menu.addSubMenu ("Dyn8 " + juce::String (group * 16 + 1) + "-" + juce::String (group * 16 + 16), sub);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
                            [safe = juce::Component::SafePointer<ProcessingScreen> (this), post] (int r)
                            {
                                if (safe != nullptr && r > 0)
                                    safe->chooseDyn8 (post, r - 2);
                            });
    };
    if (dyn >= 0)
    {
        auto& target = addButton ({ 280, 70, 240, 44 }, "Dyn8 " + juce::String (dyn + 1), kCyan, {});
        target.dropdown = true;
        target.onClick = [pickDyn8, &target] { pickDyn8 (target); };
    }
    else
        addButton ({ 280, 70, 240, 44 }, fx >= 0 ? "FX Unit " + juce::String (fx + 1) : juce::String ("Unassigned"), kCyan, [this] { if (onFxUnit) onFxUnit(); });
    if (! ld::ld_engine_has_ufx (e))
        return;
    if (dyn < 0)
    {
        auto& pick = addButton ({ 540, 70, 190, 44 }, "Pick Dyn8", kCyan, {});
        pick.dropdown = true;
        pick.onClick = [pickDyn8, &pick] { pickDyn8 (pick); };
        return;
    }
    buildDyn8Panel (dyn);
}

bool ProcessingScreen::chooseDyn8 (bool post, int engine)
{
    auto* e = ctx.engine.getHandle();
    if (strip == nullptr || engine >= ld::kNumDyn8 || ! ld::ld_engine_has_ufx (e))
        return false;
    auto& ins = post ? strip->params.postInsert() : strip->params.preInsert();
    if (engine >= 0)
    {
        const int now = ins.fx.load() - ld::kDyn8FxBase;
        if (((ld::ld_dyn8_in_use (e) >> engine) & 1) != 0 && engine != now)
            return false; // another insert has it
    }
    ins.fx = engine >= 0 ? ld::kDyn8FxBase + engine : -1;
    if (tab == Tab::InsA || tab == Tab::InsB)
        buildTab();
    return true;
}

void ProcessingScreen::setDyn8Page (int page)
{
    dyn8Page = juce::jlimit (0, 1, page);
    if (tab == Tab::InsA || tab == Tab::InsB)
        buildTab();
}

void ProcessingScreen::buildDyn8Panel (int engine)
{
    // Sidecar's layout (the console's own Dyn8 pictures aren't public here): the key source on top, a
    // page for the multiband (the common controls, then a row per band) and one for the dynamic EQ.
    auto* e = ctx.engine.getHandle();
    const int slot = ld::kDyn8FxBase + engine;
    auto* params = const_cast<ld::FxParams*> (ld::ld_fx_params (e, slot));
    const int m = models::findModel (models::Fx, "DLVDYN8");
    if (params == nullptr || m < 0)
        return;
    const auto& model = models::model (models::Fx, m);
    addKeyControls (slot, { 140, 124, 190, 40 }, { 340, 124, 130, 40 });
    for (int page = 0; page < 2; ++page)
    {
        auto& k = addButton ({ 500 + page * 180, 124, 170, 40 }, page == 0 ? "Multiband" : "Dynamic EQ", kCyan, [this, page] { setDyn8Page (page); });
        k.isOn = [this, page] { return dyn8Page == page; };
        k.litFill = kCyan.darker (0.45f);
    }
    // The values: 7 common and 4 x 7 multiband, then 4 x 7 dynamic EQ.
    if (dyn8Page == 0)
        addModelBoxes (model, params->values, 0, 35, 176, 68, 62);
    else
        addModelBoxes (model, params->values, 35, 28, 176, 68, 62);
}

void ProcessingScreen::addModelBoxes (const models::Model& model, std::atomic<float>* values, int first, int n, int y, int pitch, int height)
{
    for (int i = 0; i < n && first + i < (int) model.params.size(); ++i)
    {
        const auto& p = model.params[(size_t) (first + i)];
        addBox ({ 140 + (i % 7) * 103, y + (i / 7) * pitch, 99, height }, bindModelParam (values[first + i], p), p.isContinuous() ? kCyan : kTeal, p.label);
    }
}

void ProcessingScreen::addKeyControls (int fxSlot, juce::Rectangle<int> at, juce::Rectangle<int> tapAt)
{
    // Sidecar's reading of the key source list: an input, a mix, a main or a matrix, as a gate's KEY SOURCE.
    auto* e = ctx.engine.getHandle();
    const int source = ld::ld_fx_key_source (e, fxSlot);
    const int tap = ld::ld_fx_key_tap (e, fxSlot);
    static const char* const kinds[] = { "Input", "Aux", "Mix", "Main", "Matrix" };
    const auto nameOfKey = [] (int code) -> juce::String
    {
        if (code < 0)
            return "Self";
        if (code < ld::kKeyStripBase)
            return "Input " + juce::String (code + 1);
        const int kind = code / ld::kKeyStripBase;
        return (kind > 0 && kind < 5 ? juce::String (kinds[kind]) : juce::String ("Strip")) + " " + juce::String (code % ld::kKeyStripBase + 1);
    };
    auto& src = addButton (at, "Key: " + nameOfKey (source), kCyan, {});
    src.dropdown = true;
    src.onClick = [this, fxSlot, &src]
    {
        auto* eng = ctx.engine.getHandle();
        const int now = ld::ld_fx_key_source (eng, fxSlot);
        juce::PopupMenu menu;
        menu.addItem (1, "Self (none)", true, now < 0);
        struct Group
        {
            const char* name;
            StripKind kind;
            int base;
        };
        for (const auto& g : { Group { "Inputs", StripKind::Channel, 100 }, Group { "Mixes", StripKind::Bus, 1000 }, Group { "Mains", StripKind::Main, 2000 }, Group { "Matrices", StripKind::Matrix, 3000 } })
        {
            const auto& list = ctx.engine.getStrips (g.kind);
            const int count = (int) list.size();
            if (count == 0)
                continue;
            juce::PopupMenu sub;
            for (int first = 0; first < count; first += 16)
            {
                juce::PopupMenu part;
                for (int i = first; i < juce::jmin (count, first + 16); ++i)
                {
                    const int code = g.kind == StripKind::Channel ? i : ld::kKeyStripBase * (int) g.kind + i;
                    part.addItem (g.base + i, nameOf (*list[(size_t) i]), true, code == now);
                }
                if (count > 16)
                    sub.addSubMenu (juce::String (first + 1) + "-" + juce::String (juce::jmin (count, first + 16)), part);
                else
                    sub = part;
            }
            menu.addSubMenu (g.name, sub);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&src),
                            [safe = juce::Component::SafePointer<ProcessingScreen> (this), fxSlot] (int r)
                            {
                                if (safe == nullptr || r <= 0)
                                    return;
                                int code = -1;
                                if (r >= 3000)
                                    code = ld::kKeyStripBase * (int) StripKind::Matrix + r - 3000;
                                else if (r >= 2000)
                                    code = ld::kKeyStripBase * (int) StripKind::Main + r - 2000;
                                else if (r >= 1000)
                                    code = ld::kKeyStripBase * (int) StripKind::Bus + r - 1000;
                                else if (r >= 100)
                                    code = r - 100;
                                const int taps = code >= 0 && code < ld::kKeyStripBase ? 8 : ld::kNumOutputKeyTaps;
                                safe->setKeySource (fxSlot, code, juce::jlimit (0, taps - 1, (int) ld::ld_fx_key_tap (safe->ctx.engine.getHandle(), fxSlot)));
                            });
    };
    static const char* const channelTaps[] = { "Input", "Filter", "Tap 3", "Tap 4", "Tap 5", "Pre Fader", "Post Fader", "Post Proc" };
    static const char* const outputTaps[] = { "Bus", "Dyn", "Pre Fdr", "Post Fdr", "EQ", "Ins 2" };
    const bool fromChannel = source >= 0 && source < ld::kKeyStripBase;
    const int taps = fromChannel ? 8 : ld::kNumOutputKeyTaps;
    const int shownTap = juce::jlimit (0, taps - 1, tap);
    auto& tapKey = addButton (tapAt, source < 0 ? juce::String ("Tap") : juce::String (fromChannel ? channelTaps[shownTap] : outputTaps[shownTap]), kCyan, {});
    tapKey.dropdown = true;
    tapKey.setEnabled (source >= 0);
    tapKey.onClick = [this, fxSlot, source, taps, fromChannel, shownTap, &tapKey]
    {
        juce::PopupMenu menu;
        for (int i = 0; i < taps; ++i)
            menu.addItem (i + 1, fromChannel ? channelTaps[i] : outputTaps[i], true, i == shownTap);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tapKey),
                            [safe = juce::Component::SafePointer<ProcessingScreen> (this), fxSlot, source] (int r)
                            {
                                if (safe != nullptr && r > 0)
                                    safe->setKeySource (fxSlot, source, r - 1);
                            });
    };
}

void ProcessingScreen::setKeySource (int fxSlot, int source, int tap)
{
    ld::ld_fx_set_key (ctx.engine.getHandle(), fxSlot, source, tap);
    if (tab == Tab::InsA || tab == Tab::InsB || tab == Tab::Ufx)
        buildTab();
}

void ProcessingScreen::chooseUfxSlot (int fxSlot)
{
    ufxSlot = juce::jlimit ((int) ld::kUfxFirst, ld::kUfxFirst + ld::kNumUfx - 1, fxSlot);
    if (tab == Tab::Ufx)
        buildTab();
}

void ProcessingScreen::setUfxPage (int section)
{
    ufxPage = juce::jlimit (0, 4, section);
    if (tab == Tab::Ufx)
        buildTab();
}

bool ProcessingScreen::chooseUfxModel (int model)
{
    const bool ok = ld::ld_fx_set_model (ctx.engine.getHandle(), ufxSlot, model, true);
    if (ok && tab == Tab::Ufx)
        buildTab();
    return ok;
}

void ProcessingScreen::setGlobalKey (int key, int scale)
{
    ld::ld_engine_set_global_key (ctx.engine.getHandle(), juce::jlimit (0, 11, key), juce::jlimit (0, 2, scale));
    if (tab == Tab::Ufx)
        buildTab();
}

void ProcessingScreen::playUfxNote (int note, int velocity)
{
    ld::ld_fx_midi_note (ctx.engine.getHandle(), note, velocity);
}

void ProcessingScreen::buildUfx()
{
    // Sidecar's layout: the console's own RackUltra pages aren't public here. The engines along the top, the
    // device, its key source and the Global Key under them, then the strip's five sections and note keys.
    auto* e = ctx.engine.getHandle();
    if (! ld::ld_engine_has_ufx (e))
        return;
    ufxSlot = juce::jlimit ((int) ld::kUfxFirst, ld::kUfxFirst + ld::kNumUfx - 1, ufxSlot);
    auto* params = const_cast<ld::FxParams*> (ld::ld_fx_params (e, ufxSlot));
    if (params == nullptr)
        return;
    for (int i = 0; i < ld::kNumUfx; ++i)
    {
        const int slot = ld::kUfxFirst + i;
        auto& k = addButton ({ 140 + i * 90, 62, 86, 40 }, "UFX " + juce::String (i + 1), kCyan, [this, slot] { chooseUfxSlot (slot); });
        k.isOn = [this, slot] { return ufxSlot == slot; };
        k.litFill = kCyan.darker (0.45f);
    }
    shownUfxModel = params->model.load();
    auto& device = addButton ({ 140, 110, 280, 40 }, models::model (models::Fx, shownUfxModel).name, kCyan, {});
    device.dropdown = true;
    device.onClick = [this, &device]
    {
        juce::PopupMenu menu;
        const auto& list = models::table (models::Fx);
        for (int i = 0; i < (int) list.size(); ++i)
            if (ld::ld_model_fx_allowed (i, ufxSlot))
                menu.addItem (i + 1, list[(size_t) i].name, true, i == shownUfxModel);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&device),
                            [safe = juce::Component::SafePointer<ProcessingScreen> (this)] (int r)
                            {
                                if (safe != nullptr && r > 0)
                                    safe->chooseUfxModel (r - 1);
                            });
    };
    addToggle ({ 430, 110, 100, 40 }, "Bypass", kRed, params->bypass);
    addButton ({ 540, 110, 150, 40 }, "Reset Strip", juce::Colours::white, [e, slot = ufxSlot] { ld::ld_ufx_strip_reset (e, slot); });
    addButton ({ 700, 110, 150, 40 }, "Open FX Unit", kCyan, [this] { if (onFxUnit) onFxUnit(); });

    addKeyControls (ufxSlot, { 140, 158, 190, 40 }, { 340, 158, 130, 40 });
    static const char* const notes[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* const scales[] = { "Major", "Minor", "Chromatic" };
    const int key = ld::ld_engine_global_key (e, 0), scale = ld::ld_engine_global_key (e, 1);
    auto& keyKey = addButton ({ 510, 158, 150, 40 }, juce::String ("Global Key ") + notes[juce::jlimit (0, 11, key)], kYellow, {});
    keyKey.dropdown = true;
    keyKey.onClick = [this, &keyKey, key, scale]
    {
        juce::PopupMenu menu;
        for (int i = 0; i < 12; ++i)
            menu.addItem (i + 1, notes[i], true, i == key);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyKey),
                            [safe = juce::Component::SafePointer<ProcessingScreen> (this), scale] (int r)
                            {
                                if (safe != nullptr && r > 0)
                                    safe->setGlobalKey (r - 1, scale);
                            });
    };
    auto& scaleKey = addButton ({ 670, 158, 180, 40 }, scales[juce::jlimit (0, 2, scale)], kYellow, {});
    scaleKey.dropdown = true;
    scaleKey.onClick = [this, &scaleKey, key, scale]
    {
        juce::PopupMenu menu;
        for (int i = 0; i < 3; ++i)
            menu.addItem (i + 1, scales[i], true, i == scale);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scaleKey),
                            [safe = juce::Component::SafePointer<ProcessingScreen> (this), key] (int r)
                            {
                                if (safe != nullptr && r > 0)
                                    safe->setGlobalKey (key, r - 1);
                            });
    };

    static const char* const sections[] = { "Pre PEQ", "Pre Dyn", "Post Dyn", "Post PEQ", "Width" };
    for (int i = 0; i < 5; ++i)
    {
        auto& k = addButton ({ 140 + i * 108, 208, 104, 36 }, sections[i], kCyan, [this, i] { setUfxPage (i); });
        k.isOn = [this, i] { return ufxPage == i; };
        k.litFill = kCyan.darker (0.45f);
    }
    auto* values = const_cast<std::atomic<float>*> (ld::ld_ufx_strip_values (e, ufxSlot));
    if (values != nullptr)
    {
        const auto& model = models::model (models::UfxStrip, 0);
        const int first = ld::ld_ufx_strip_section (ufxPage);
        const int next = ufxPage < 4 ? ld::ld_ufx_strip_section (ufxPage + 1) : (int) model.params.size();
        if (first >= 0 && next > first)
            addModelBoxes (model, values, first, next - first, 252, 76, 70);
    }
    for (int i = 0; i < 12; ++i)
    {
        const int note = 60 + i;
        const bool black = std::strlen (notes[i]) > 1;
        auto& k = addButton ({ 140 + i * 60, 486, 56, 34 }, juce::String (notes[i]) + "4", black ? kTeal : juce::Colours::white, {});
        k.pale = ! black;
        k.fontSize = 13.0f;
        k.onClick = [this, note]
        {
            playUfxNote (note, 100);
            juce::Timer::callAfterDelay (300, [safe = juce::Component::SafePointer<ProcessingScreen> (this), note] { if (safe != nullptr) safe->playUfxNote (note, 0); });
        };
    }
}

void ProcessingScreen::buildDelay (Strip& s)
{
    auto* in = &s.params.input;
    auto delay = bindLinear ("Delay", in->delay, 0.0f, s.isInput() ? 341.0f : 682.0f, " ms", 2, 0.0f);
    delay.set = [in, inner = delay.set] (float v) { in->delayMode.store (2); inner (v); }; // the engine's milliseconds
    addToggle ({ 170, 88, 90, 44 }, "In", kGreen, in->delayOn);
    addBox ({ 280, 70, 170, 66 }, std::move (delay), kCyan, "Delay");
}

void ProcessingScreen::buildOverview (Strip& s)
{
    struct Tile
    {
        const char* name;
        Tab target;
        std::atomic<bool>* on;
    };
    auto& p = s.params;
    std::vector<Tile> tiles;
    if (s.getKind() == StripKind::Channel)
    {
        tiles.push_back ({ "Preamp", Tab::Preamp, nullptr });
        tiles.push_back ({ "Filter", Tab::Filter, &p.filter().lowCut });
        tiles.push_back ({ "Gate", Tab::Gate, &p.gate().on });
    }
    else
        tiles.push_back ({ "Ext In", Tab::ExtIn, nullptr });
    tiles.push_back ({ "Ins A", Tab::InsA, &p.preInsert().on });
    tiles.push_back ({ "PEQ", Tab::Peq, &p.eq().on });
    if (s.getKind() != StripKind::Channel)
        tiles.push_back ({ "GEQ", Tab::Geq, nullptr });
    tiles.push_back ({ "Comp", Tab::Comp, &p.dynamics().on });
    tiles.push_back ({ "Ins B", Tab::InsB, &p.postInsert().on });
    tiles.push_back ({ "Delay", Tab::Delay, &p.input.delayOn });
    tiles.push_back ({ "Routing", Tab::Routing, nullptr });
    if (ld::ld_engine_has_ufx (ctx.engine.getHandle()))
        tiles.push_back ({ "UFX", Tab::Ufx, nullptr });
    int n = 0;
    for (const auto& t : tiles)
    {
        auto& b = addButton ({ 146 + (n % 4) * 178, 60 + (n / 4) * 150, 168, 130 }, t.name, kCyan, [this, target = t.target] { showTab (target); });
        b.fontSize = 22.0f;
        if (t.on != nullptr)
        {
            b.isOn = [on = t.on] { return on->load(); };
            b.litFill = kGreen.darker (0.25f);
        }
        ++n;
    }
}

void ProcessingScreen::buildBank()
{
    // Sidecar's reading: the inputs, to pick one from (no photograph of this tab).
    const auto& list = ctx.engine.getChannels();
    for (int i = 0; i < juce::jmin (48, (int) list.size()); ++i)
    {
        auto* st = list[(size_t) i].get();
        auto& b = addButton ({ 140 + (i % 8) * 90, 52 + (i / 8) * 78, 86, 72 }, nameOf (*st), st->getColour(), [this, st] { if (ctx.selectStrip) ctx.selectStrip (st); });
        b.fontSize = 13.0f;
        b.isOn = [this, st] { return strip == st; };
        b.litFill = st->getColour().darker (0.2f);
    }
}

void ProcessingScreen::buildGeq (Strip& s)
{
    for (const auto& step : s.getChain())
    {
        if (! step.isBlock() || step.getBlockKind() != BlockKind::Eq)
            continue;
        auto block = s.getBlock (step.uid);
        auto* eq = block.eq();
        if (eq == nullptr)
            continue;
        const auto& m = models::model (models::BusEq, eq->model.load());
        addToggle ({ 146, 44, 70, 34 }, "In", kGreen, eq->on);
        addButton ({ 226, 44, 220, 34 }, m.name, kCyan, {}).setEnabled (false);
        for (int i = 0; i < (int) m.params.size() && i < 35; ++i)
            addBox ({ 140 + (i % 7) * 103, 90 + (i / 7) * 84, 99, 76 }, bindModelParam (eq->values[i], m.params[(size_t) i]), kCyan, m.params[(size_t) i].label);
        return;
    }
}

void ProcessingScreen::timerCallback()
{
    retired.clear();
    if (! isShowing())
        return;
    for (auto& [t, b] : tabs)
        b->refresh();
    for (auto* t : std::initializer_list<Touchable*> { &gainKnob, &trimKnob, &hpf, &lpf })
        t->refresh();
    for (auto& i : items)
    {
        if (auto* box = dynamic_cast<Touchable*> (i.get()))
            box->refresh();
        else if (auto* key = dynamic_cast<ScreenButton*> (i.get()))
            key->refresh();
    }
    soft.refresh();
    graph.update();
    wideCurve.update();
    if (view == 1)
        bandPanels.update();
    // Nothing here draws the RTA while another page is hosted, unless the wide curve is up.
    const bool wantsRta = hostedPage == nullptr || view == 0;
    if (strip != nullptr && ! wantsRta && rtaShowing)
    {
        Analysis (ctx.engine.getHandle()).hideRta();
        rtaShowing = false;
    }
    if (strip != nullptr && wantsRta)
    {
        // The RTA follows the selected strip while this screen shows.
        Analysis analysis (*strip);
        if (! rtaShowing || ! analysis.isAnalysed (*strip) || ! analysis.controls.rtaOn.load())
        {
            analysis.showRta (strip, ld::kRtaViewEq);
            rtaShowing = true;
        }
        analysis.read (rtaBands);
        if ((int) rtaBands.frame != rtaFrame)
        {
            rtaFrame = (int) rtaBands.frame;
            graph.setRta (&rtaBands);
            wideCurve.setRta (&rtaBands);
        }
    }
    if (strip != nullptr && tab == Tab::Comp && strip->params.dynamics().model.load() != compModel)
    {
        compModel = strip->params.dynamics().model.load();
        buildBlocks();
    }
    if (strip != nullptr && hostedPage == nullptr && tab == Tab::ExtIn && strip->isOutput() && strip->mixInput().extSource.load() != extShown)
    {
        // Patched from somewhere else (a scene, the strip panel): the page takes it.
        loadExtIn();
        buildTab();
    }
    if (strip != nullptr && hostedPage == nullptr && (tab == Tab::InsA || tab == Tab::InsB))
    {
        // Another insert took the engine, or a scene recall moved it.
        const int fx = (tab == Tab::InsA ? strip->params.preInsert() : strip->params.postInsert()).fx.load();
        if (fx != shownInsertFx)
            buildTab();
    }
    if (hostedPage == nullptr && tab == Tab::Ufx)
    {
        if (auto* params = ld::ld_fx_params (ctx.engine.getHandle(), ufxSlot); params != nullptr && params->model.load() != shownUfxModel)
            buildTab();
        repaint (124, 200, 742, 50);
    }
    if (transfer.isVisible())
        transfer.repaint();
}

void ProcessingScreen::resized()
{
    // The tabs sit 7 apart (photographed); a channel's eleven, with Routing, close up a little to
    // stay left of the Mix box.
    const auto widthOf = [] (const ScreenTab& b) { return juce::jmax (52, b.getButtonText().length() * 9 + 12); };
    int total = 0;
    for (auto& [t, b] : tabs)
        total += widthOf (*b);
    const int gap = tabs.size() > 1 ? juce::jlimit (3, 7, (866 - 141 - total) / ((int) tabs.size() - 1)) : 7;
    int x = 141;
    for (auto& [t, b] : tabs)
    {
        const int w = widthOf (*b);
        b->setBounds (x, 8, w, 38);
        x += w + gap;
    }
    gainKnob.setBounds (18, 56, 92, 112);
    trimKnob.setBounds (18, 176, 76, 112);
    hpf.setBounds (16, 294, 96, 118);
    lpf.setBounds (16, 416, 96, 112);
    graph.setBounds (129, 48, 713, 288);
    wideCurve.setBounds (6, 537, kTouchW - 12, 150);
    bandPanels.setBounds (4, 535, kTouchW - 8, kTouchH - 537);
    faders.setBounds (0, 535, kTouchW, kTouchH - 535);
    pageArea.setBounds (sceneSource());
    if (hostedPage != nullptr)
        soft.setBounds (878, 62, 120, 468);
    else
        soft.setBounds (872, 40, 126, 490);
}

void ProcessingScreen::paint (juce::Graphics& g)
{
    g.fillAll (kBack);

    // The strip's name at the top left.
    {
        const auto tint = strip != nullptr ? strip->getColour().interpolatedWith (juce::Colours::white, 0.55f) : kFrame;
        const juce::Rectangle<float> box (14.0f, 2.0f, 101.0f, 46.0f);
        g.setColour (tint);
        g.fillRoundedRectangle (box, 5.0f);
        if (strip != nullptr)
        {
            for (int i = 0; i < 2; ++i)
            {
                const juce::Rectangle<float> pill (20.0f, 5.0f + (float) i * 21.0f, 89.0f, 19.0f);
                g.setColour (tint.darker (0.45f));
                g.fillRoundedRectangle (pill, 5.0f);
                text (g, pill, i == 0 ? nameOf (*strip) : strip->getLabel(), 13.5f, juce::Colours::white, juce::Justification::centred);
            }
        }
    }

    // The active Mix at the top right.
    if (oneScreen)
        mainBox (g, hostedPage != nullptr ? juce::Rectangle<float> (878.0f, 4.0f, 120.0f, 50.0f) : juce::Rectangle<float> (872.0f, 2.0f, 126.0f, 46.0f), ctx.engine, desk);
    else
    {
        const juce::Rectangle<float> box (872.0f, 2.0f, 126.0f, 46.0f);
        const int bus = desk.getMixOnFaders();
        auto* mix = bus >= 0 ? stripOf (ctx.engine, { Slot::Bus, bus }) : nullptr;
        g.setColour (mix != nullptr ? mix->getColour().interpolatedWith (juce::Colours::white, 0.3f) : kBack.brighter (0.15f));
        g.fillRoundedRectangle (box, 5.0f);
        if (mix != nullptr)
        {
            auto r = box.reduced (4.0f, 2.0f);
            text (g, r.removeFromTop (21.0f), nameOf (*mix), 14.0f, juce::Colours::white, juce::Justification::centred);
            text (g, r, mix->getLabel(), 14.0f, juce::Colours::white, juce::Justification::centred);
        }
    }

    // The preamp and the filters; the main area's frame.
    panel (g, { 14.0f, 52.0f, 101.0f, 478.0f }, kSteelTop, kSteel);
    g.setColour (juce::Colours::white.withAlpha (0.4f));
    for (float y : { 172.0f, 292.0f, 414.0f })
        g.drawHorizontalLine ((int) y, 16.0f, 113.0f);
    panel (g, { 124.0f, 28.0f, 742.0f, 502.0f }, kFrameLight, kFrame, 7.0f);

    // Trim's meter.
    if (strip != nullptr && trimKnob.isVisible())
    {
        const juce::Rectangle<float> m (96.0f, 196.0f, 11.0f, 74.0f);
        g.setColour (juce::Colour (0xff121522));
        g.fillRect (m);
        const float db = juce::Decibels::gainToDecibels (ctx.meters.get (strip, MeterPoint::Trim, 0), -60.0f);
        g.setColour (juce::Colour (0xff30c060));
        const float h = m.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        g.fillRect (m.getX() + 1.0f, m.getBottom() - h, m.getWidth() - 2.0f, h);
    }

    // The PEQ's panel: its side tabs and the blue field.
    if (strip != nullptr && (tab == Tab::Peq || tab == Tab::Filter))
    {
        const juce::Rectangle<float> field (162.0f, 345.0f, 701.0f, 172.0f);
        g.setColour (kPanelDeep);
        g.fillRoundedRectangle (field, 6.0f);
        g.setColour (kCyan);
        g.drawRoundedRectangle (field, 6.0f, 2.0f);
        if (tab == Tab::Peq)
        {
            const juce::Rectangle<float> side (133.0f, 358.0f, 30.0f, 100.0f);
            g.setColour (kTabOn);
            g.fillRoundedRectangle (side, 5.0f);
            juce::Graphics::ScopedSaveState state (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, side.getCentreX(), side.getCentreY()));
            text (g, juce::Rectangle<float> (100.0f, 30.0f).withCentre (side.getCentre()), "PEQ", 15.0f, kTabOnText, juce::Justification::centred);
        }
    }
    if (strip != nullptr && hostedPage == nullptr && tab == Tab::ExtIn && strip->isOutput())
    {
        titledArea (g, { 132.0f, 56.0f, 448.0f, 112.0f }, "Source Select");
        titledArea (g, { 588.0f, 56.0f, 132.0f, 112.0f }, "SigGen");
        titledArea (g, { 728.0f, 56.0f, 132.0f, 112.0f }, "Talkback");
        const int source = strip->mixInput().extSource.load();
        titledArea (g, { 132.0f, 176.0f, 318.0f, 150.0f }, source >= 0 && source < 128 && ctx.engine.getSource (source) != nullptr ? "Socket Preamp" : "");
        titledArea (g, { 458.0f, 176.0f, 122.0f, 346.0f }, "Trim");
    }
    else if (strip != nullptr && hostedPage == nullptr && tab == Tab::Routing && strip->getKind() != StripKind::Aux)
    {
        if (strip->getKind() == StripKind::Channel)
        {
            titledArea (g, { 132.0f, 56.0f, 728.0f, 200.0f }, "Direct Out");
            text (g, { 330.0f, 92.0f, 300.0f, 24.0f }, "Global Direct Out Source", 14.0f, juce::Colours::white, juce::Justification::centred);
        }
        else
        {
            titledArea (g, { 132.0f, 56.0f, 360.0f, 106.0f }, "Mix Source");
            if (strip->params.stereo.load() && strip->getKind() != StripKind::Main)
                titledArea (g, { 500.0f, 56.0f, 360.0f, 106.0f }, "Follow Main Pan");
        }
    }
    else if (hostedPage == nullptr && tab == Tab::Ufx)
    {
        auto* e = ctx.engine.getHandle();
        if (! ld::ld_engine_has_ufx (e))
            text (g, { 124.0f, 200.0f, 742.0f, 40.0f }, "This console has no RackUltra engines", 22.0f, juce::Colours::white, juce::Justification::centred);
        else
        {
            // The two dynamics sections' gain reduction.
            text (g, { 700.0f, 204.0f, 160.0f, 22.0f }, "GR pre " + juce::String (ld::ld_ufx_strip_meter (e, ufxSlot, 0), 1) + " dB", 12.5f, juce::Colours::white);
            text (g, { 700.0f, 224.0f, 160.0f, 22.0f }, "GR post " + juce::String (ld::ld_ufx_strip_meter (e, ufxSlot, 1), 1) + " dB", 12.5f, juce::Colours::white);
            text (g, { 140.0f, 466.0f, 400.0f, 18.0f }, "MIDI notes for the Harmoniser and the Tuner", 11.5f, juce::Colours::white.withAlpha (0.8f));
        }
    }
    else if (strip != nullptr && hostedPage == nullptr && tab == Tab::Preamp && strip->getKind() == StripKind::Channel)
        text (g, { 724.0f, 184.0f, 138.0f, 32.0f }, "ABCD Source Setup", 12.5f, juce::Colours::white);
    if (strip == nullptr)
        text (g, { 124.0f, 200.0f, 742.0f, 40.0f }, "Press a strip's Sel key", 22.0f, juce::Colours::white, juce::Justification::centred);
}

//==============================================================================
SceneScreen::SceneScreen (UiContext& c, Desk& d)
    : goKey ("Go", kTeal), undoKey ("Undo Go", kTeal), storeKey ("Store", kYellow), deleteKey ("Delete", kRed), filterKey ("Recall Filter", kCyan), lockKey ("Lock", juce::Colours::white),
      copyFilterKey ("Copy Filter To...", kCyan), embeddedKey ("Embedded Recall", kTeal), midiKey ("Custom MIDI", kTeal), actionsKey ("Actions", kTeal), copyKey ("Copy", juce::Colours::white),
      pasteKey ("Paste", juce::Colours::white), ctx (c), desk (d), titleTab ("Scenes", true), managerTab ("Scene Manager"), updateTab ("Update"), cueTab ("Cue List\nEditor"), importTab ("Import"),
      safesTab ("Global Safes"), fade (kCyan), faders (c, d)
{
    int subIndex = 0;
    for (auto* t : { &managerTab, &updateTab, &cueTab, &importTab, &safesTab })
    {
        t->isOn = [this, subIndex] { return sub == subIndex; };
        t->onClick = [this, subIndex] { showSub (subIndex); };
        t->refresh();
        ++subIndex;
    }
    for (auto* t : { &titleTab, &managerTab, &updateTab, &cueTab, &importTab, &safesTab })
        addAndMakeVisible (t);

    for (auto* k : { &goKey, &undoKey, &storeKey, &deleteKey, &filterKey, &lockKey, &copyFilterKey, &embeddedKey, &midiKey, &actionsKey, &copyKey, &pasteKey })
        addAndMakeVisible (k);
    // What Sidecar's shows can't do yet.
    for (auto* k : { &copyFilterKey, &embeddedKey, &midiKey, &actionsKey })
        k->setEnabled (false);
    undoKey.setEnabled (ctx.show.canUndoRecall());
    undoKey.onClick = [this] { undoGo(); };
    lockKey.isOn = [this] { return isLocked (selected); };
    lockKey.litFill = juce::Colour (0xff9a6a10);
    lockKey.onClick = [this] { toggleLock(); };
    copyKey.onClick = [this] { copyScene(); };
    pasteKey.onClick = [this] { pasteScene(); };
    goKey.onClick = [this] { go(); };
    storeKey.onClick = [this]
    {
        if (isLocked (selected))
            return;
        if (selected >= ctx.show.getNumScenes())
        {
            store();
            return;
        }
        popups::confirm (*this, "STORE", "Overwrite scene " + juce::String (selected + 1) + " with the console as it is?", "STORE", [this] { store(); }, false);
    };
    deleteKey.onClick = [this]
    {
        if (selected < ctx.show.getNumScenes() && ! isLocked (selected))
            popups::confirm (*this, "DELETE", "Delete scene " + juce::String (selected + 1) + "?", "DELETE", [this] { remove(); }, true);
    };
    filterKey.onClick = [this] { showRecallFilter(); };

    for (auto* e : { &nameEditor, &notesEditor })
    {
        e->setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        e->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        e->setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::white.withAlpha (0.5f));
        e->setColour (juce::TextEditor::textColourId, juce::Colours::white);
        e->setFont (type::font (18.0f));
        addAndMakeVisible (e);
    }
    notesEditor.setMultiLine (true);
    notesEditor.setReturnKeyStartsNewLine (true);
    nameEditor.onTextChange = [this]
    {
        if (selected < ctx.show.getNumScenes())
            ctx.show.setSceneName (selected, nameEditor.getText());
    };
    notesEditor.onTextChange = [this]
    {
        if (selected < ctx.show.getNumScenes())
            ctx.show.setSceneNotes (selected, notesEditor.getText());
    };

    KnobBinding crossfade;
    crossfade.label = "Crossfade";
    crossfade.get = [this]
    {
        const auto s = ctx.show.getScene (selected);
        return s.has_value() ? (float) juce::jlimit (0.0, 1.0, s->fadeSeconds / 30.0) : 0.0f;
    };
    crossfade.set = [this] (float v)
    {
        if (selected < ctx.show.getNumScenes())
            ctx.show.setSceneFadeSeconds (selected, std::round (v * 300.0f) / 10.0f);
    };
    crossfade.text = [this]
    {
        const auto s = ctx.show.getScene (selected);
        return juce::String (s.has_value() ? s->fadeSeconds : 0.0, 1) + " s";
    };
    crossfade.reset = [this]
    {
        if (selected < ctx.show.getNumScenes())
            ctx.show.setSceneFadeSeconds (selected, 0.0);
    };
    fade.setBinding (std::move (crossfade));
    addAndMakeVisible (fade);
    const auto touch = [this] (Touchable& t)
    {
        if (touchedNow != nullptr && touchedNow.getComponent() != &t)
            touchedNow->setTouched (false);
        touchedNow = &t;
        t.setTouched (true);
        if (onTouchTurn)
            onTouchTurn (t.getBinding());
    };
    fade.onTouched = touch;
    addAndMakeVisible (sends);
    sends.onTouched = touch;
    sends.onLayerTapped = [this] (int l) { setSoftLayer (l); };
    addAndMakeVisible (faders);
    loadEditors();
    buildSends();
    startTimerHz (5);
}

SceneScreen::~SceneScreen()
{
    copied.deleteFile();
}

bool SceneScreen::isLocked (int row) const
{
    return std::find (lockedScenes.begin(), lockedScenes.end(), row) != lockedScenes.end();
}

void SceneScreen::toggleLock()
{
    if (selected >= ctx.show.getNumScenes())
        return;
    const auto it = std::find (lockedScenes.begin(), lockedScenes.end(), selected);
    if (it != lockedScenes.end())
        lockedScenes.erase (it);
    else
        lockedScenes.push_back (selected);
    loadEditors();
    lockKey.refresh();
    repaint();
}

void SceneScreen::copyScene()
{
    if (selected >= ctx.show.getNumScenes())
        return;
    copied.deleteFile();
    copied = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("sidecar-dlive-scene", Show::sceneFileExtension, false);
    if (ctx.show.exportScene (selected, copied).failed())
    {
        copied.deleteFile();
        copied = juce::File();
    }
    refresh();
}

void SceneScreen::pasteScene()
{
    if (! hasCopiedScene() || isLocked (selected))
        return;
    const int count = ctx.show.getNumScenes();
    const auto old = selected < count ? ctx.show.getScene (selected) : std::nullopt;
    juce::String error;
    const int added = ctx.show.importScene (copied, juce::jmin (selected, count), error);
    if (added < 0)
        return;
    if (old.has_value())
    {
        // Pasted over a scene: it keeps its place, name and description.
        ctx.show.removeScene (added + 1);
        ctx.show.setSceneName (added, old->name);
        ctx.show.setSceneNotes (added, old->notes);
    }
    else
    {
        ctx.show.setSceneName (added, "Scene " + juce::String (added + 1));
        ctx.show.setSceneNotes (added, {});
    }
    selected = added;
    next = selected;
    loadEditors();
    refresh();
    repaint();
}

juce::String SceneScreen::rowName (int row) const
{
    const auto s = row < ctx.show.getNumScenes() ? ctx.show.getScene (row) : std::nullopt;
    return juce::String (row + 1) + ") " + (s.has_value() ? s->name : juce::String());
}

void SceneScreen::loadEditors()
{
    const auto s = ctx.show.getScene (selected);
    nameEditor.setText (s.has_value() ? s->name : juce::String(), false);
    notesEditor.setText (s.has_value() ? s->notes : juce::String(), false);
    nameEditor.setEnabled (s.has_value());
    notesEditor.setEnabled (s.has_value());
    nameEditor.setReadOnly (isLocked (selected));
    notesEditor.setReadOnly (isLocked (selected));
}

void SceneScreen::select (int row)
{
    selected = juce::jlimit (0, kSlots - 1, row);
    next = selected;
    firstMain = juce::jlimit (0, kSlots - kMainRows, selected - kMainRows / 2);
    firstLeft = juce::jlimit (0, kSlots - kLeftRows, selected - kLeftRows / 2);
    loadEditors();
    repaint();
}

void SceneScreen::stepNext (int delta)
{
    const int count = ctx.show.getNumScenes();
    if (count > 0)
        next = juce::jlimit (0, count - 1, next + delta);
    repaint();
}

void SceneScreen::go()
{
    if (selected < ctx.show.getNumScenes())
    {
        ctx.show.recallScene (selected);
        next = juce::jmin (selected + 1, juce::jmax (0, ctx.show.getNumScenes() - 1));
        repaint();
    }
}

void SceneScreen::undoGo()
{
    if (ctx.show.canUndoRecall())
    {
        ctx.show.undoRecall();
        repaint();
    }
}

void SceneScreen::goNext()
{
    if (next < ctx.show.getNumScenes())
    {
        ctx.show.recallScene (next);
        selected = next;
        loadEditors();
        next = juce::jmin (next + 1, juce::jmax (0, ctx.show.getNumScenes() - 1));
        firstMain = juce::jlimit (0, kSlots - kMainRows, selected - kMainRows / 2);
        firstLeft = juce::jlimit (0, kSlots - kLeftRows, selected - kLeftRows / 2);
        repaint();
    }
}

void SceneScreen::store()
{
    if (isLocked (selected))
        return;
    if (selected >= ctx.show.getNumScenes())
    {
        selected = ctx.show.addScene ("Scene " + juce::String (ctx.show.getNumScenes() + 1));
        next = selected;
        loadEditors();
    }
    else
        ctx.show.storeScene (selected);
    repaint();
}

void SceneScreen::remove()
{
    if (selected < ctx.show.getNumScenes() && ! isLocked (selected))
    {
        ctx.show.removeScene (selected);
        for (auto it = lockedScenes.begin(); it != lockedScenes.end();)
        {
            if (*it > selected)
                --*it;
            ++it;
        }
        selected = juce::jmin (selected, juce::jmax (0, ctx.show.getNumScenes() - 1));
        next = selected;
        loadEditors();
        repaint();
    }
}

void SceneScreen::setStrip (Strip* s)
{
    strip = s;
    buildSends();
}

void SceneScreen::setSoftLayer (int l)
{
    softLayer = juce::jlimit (0, 2, l);
    buildSends();
}

void SceneScreen::buildSends()
{
    SoftWidget::Block a, b;
    sendBlocks (ctx.engine, strip, softLayer * 6, a, b);
    sends.set (a, b, softLayer, 3, false);
}

void SceneScreen::showRecallFilter()
{
    const auto s = ctx.show.getScene (selected);
    if (! s.has_value())
        return;
    juce::PopupMenu menu;
    for (int i = 0; i < state::kNumSections; ++i)
    {
        const auto section = state::sectionAt (i);
        menu.addItem (i + 1, state::sectionName (section), true, (s->scope & section) != 0);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&filterKey),
                        [safe = juce::Component::SafePointer<SceneScreen> (this)] (int r)
                        {
                            if (safe == nullptr || r <= 0)
                                return;
                            const auto now = safe->ctx.show.getScene (safe->selected);
                            if (now.has_value())
                                safe->ctx.show.setSceneScope (safe->selected, now->scope ^ (juce::uint32) state::sectionAt (r - 1));
                        });
}

void SceneScreen::refresh()
{
    undoKey.setEnabled (ctx.show.canUndoRecall());
    // Last Scene and its time: whatever recalled it (a remote, the scene keys).
    const int current = ctx.show.getCurrentSceneIndex();
    if (current != lastRecalled)
    {
        lastRecalled = current;
        if (current >= 0)
            lastTime = juce::Time::getCurrentTime().formatted ("%H:%M:%S");
        repaint();
    }
    // Locks are this show's: a load of another drops them, deleted scenes drop theirs.
    const auto showName = ctx.show.getName();
    if (showName != shownShow)
    {
        shownShow = showName;
        lockedScenes.clear();
    }
    const int sceneCount = ctx.show.getNumScenes();
    lockedScenes.erase (std::remove_if (lockedScenes.begin(), lockedScenes.end(), [sceneCount] (int r) { return r >= sceneCount; }), lockedScenes.end());
    lockKey.setEnabled (selected < sceneCount);
    copyKey.setEnabled (selected < sceneCount);
    pasteKey.setEnabled (hasCopiedScene() && ! isLocked (selected));

    juce::String signature;
    signature << ctx.show.getNumScenes() << "|" << selected << "|" << next << "|" << firstMain << "|" << (int) lockedScenes.size() << "|" << lastRecalled;
    for (int i = firstLeft; i < firstLeft + kLeftRows; ++i)
        signature << rowName (i);
    for (int i = firstMain; i < firstMain + kMainRows; ++i)
        signature << rowName (i);
    if (signature != shownSignature)
    {
        shownSignature = signature;
        if (! nameEditor.hasKeyboardFocus (true) && ! notesEditor.hasKeyboardFocus (true))
            loadEditors();
        repaint();
    }
    fade.refresh();
    sends.refresh();
    for (auto* k : { &goKey, &undoKey, &storeKey, &deleteKey, &lockKey })
        k->refresh();
}

void SceneScreen::resized()
{
    titleTab.setBounds (120, 12, 107, 34);
    managerTab.setBounds (244, 8, 132, 38);
    updateTab.setBounds (384, 8, 78, 38);
    cueTab.setBounds (470, 4, 118, 42);
    importTab.setBounds (596, 8, 78, 38);
    safesTab.setBounds (682, 8, 110, 38);

    nameEditor.setBounds (472, 82, 270, 28);
    fade.setBounds (760, 80, 76, 34);
    notesEditor.setBounds (472, 136, 380, 80);

    goKey.setBounds (523, 256, 143, 30);
    undoKey.setBounds (692, 256, 143, 30);
    storeKey.setBounds (526, 327, 145, 27);
    deleteKey.setBounds (697, 327, 146, 27);
    filterKey.setBounds (526, 362, 145, 27);
    lockKey.setBounds (697, 362, 146, 27);
    copyFilterKey.setBounds (526, 397, 145, 27);
    embeddedKey.setBounds (697, 397, 146, 27);
    midiKey.setBounds (526, 432, 145, 27);
    actionsKey.setBounds (697, 432, 146, 27);
    copyKey.setBounds (563, 482, 96, 25);
    pasteKey.setBounds (720, 482, 97, 25);

    sends.setBounds (878, 62, 120, 468);
    faders.setBounds (0, 535, kTouchW, kTouchH - 535);
}

void SceneScreen::paint (juce::Graphics& g)
{
    g.fillAll (kBack);
    if (sub != 0)
    {
        paintSub (g);
        return;
    }
    const int count = ctx.show.getNumScenes();

    // Selected and Next.
    const auto named = [&] (juce::Rectangle<float> r, const char* what, int row)
    {
        const auto s = row < count ? ctx.show.getScene (row) : std::nullopt;
        text (g, r.removeFromTop (r.getHeight() * 0.5f), what, 14.0f, juce::Colours::white, juce::Justification::centred, true);
        text (g, r, s.has_value() ? s->name : juce::String(), 15.0f, juce::Colour (0xfff0e0a0), juce::Justification::centred, true);
    };
    named ({ 11.0f, 14.0f, 103.0f, 42.0f }, "Selected", selected);
    named ({ 872.0f, 8.0f, 120.0f, 46.0f }, "Next", next);

    panel (g, { 117.0f, 40.0f, 758.0f, 490.0f }, kFrameLight, kFrame, 7.0f);
    panel (g, { 128.0f, 56.0f, 330.0f, 464.0f }, kPanel, kPanelDeep);
    panel (g, { 462.0f, 56.0f, 408.0f, 464.0f }, kPanel, kPanelDeep);

    // The left column of scenes.
    for (int i = 0; i < kLeftRows; ++i)
    {
        const int row = firstLeft + i;
        const juce::Rectangle<float> r (15.0f, 70.0f + (float) i * 30.7f, 91.0f, 27.5f);
        g.setColour (row == selected ? kYellow : kNavy);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.65f));
        g.drawRoundedRectangle (r, 3.0f, 1.2f);
        text (g, r.reduced (5.0f, 0.0f), rowName (row), 13.0f, row == selected ? juce::Colour (0xff8a8a10) : row == next ? kKnobGreen : juce::Colours::white, juce::Justification::centredLeft);
    }

    // The filters, drawn: every scene's shown.
    for (auto [r, s] : { std::pair<juce::Rectangle<float>, const char*> { { 135.0f, 64.0f, 136.0f, 26.0f }, "Scene Status" }, { { 277.0f, 64.0f, 166.0f, 26.0f }, "All Scenes" } })
    {
        g.setColour (kTabOnText.darker (0.2f));
        g.fillRoundedRectangle (r, 5.0f);
        text (g, r, s, 13.5f, juce::Colours::white, juce::Justification::centred);
    }
    text (g, { 148.0f, 95.0f, 140.0f, 16.0f }, "Scene Name", 12.5f, juce::Colours::white);
    text (g, { 332.0f, 95.0f, 110.0f, 16.0f }, "Scene Status", 12.5f, juce::Colours::white);

    // The main list.
    g.setColour (kOrange);
    g.drawRoundedRectangle ({ 138.0f, 111.0f, 298.0f, 369.0f }, 4.0f, 2.2f);
    for (int i = 0; i < kMainRows; ++i)
    {
        const int row = firstMain + i;
        const juce::Rectangle<float> r (141.0f, 113.0f + (float) i * 33.0f, 291.0f, 31.0f);
        const bool sel = row == selected;
        g.setColour (sel ? kYellow : kNavy);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (kTeal.withAlpha (0.9f));
        g.drawRoundedRectangle (r, 3.0f, 1.3f);
        text (g, r.reduced (8.0f, 0.0f), rowName (row), 16.5f, sel ? juce::Colour (0xff8a8a10) : row == next ? kKnobGreen : juce::Colours::white);
        if (row < count && isLocked (row))
            text (g, r.reduced (8.0f, 0.0f), "Locked", 13.0f, sel ? juce::Colour (0xff6e7a10) : kOrange, juce::Justification::centredRight);
        else if (row < count)
        {
            juce::Path tick;
            const float x = r.getRight() - 26.0f, y = r.getCentreY();
            tick.startNewSubPath (x, y);
            tick.lineTo (x + 5.0f, y + 6.0f);
            tick.lineTo (x + 16.0f, y - 8.0f);
            g.setColour (sel ? juce::Colour (0xff6e7a10) : juce::Colour (0xffb6e23c));
            g.strokePath (tick, juce::PathStrokeType (2.6f));
        }
    }
    const auto last = lastRecalled >= 0 ? rowName (lastRecalled) : juce::String();
    text (g, { 135.0f, 484.0f, 300.0f, 16.0f }, "Last Scene: " + last, 13.0f, juce::Colours::white);
    text (g, { 135.0f, 502.0f, 300.0f, 16.0f }, "Time:         " + lastTime, 13.0f, juce::Colours::white);

    // The scene's name, fade and description.
    text (g, { 468.0f, 62.0f, 200.0f, 17.0f }, "Scene Name", 14.0f, juce::Colours::white);
    text (g, { 748.0f, 62.0f, 110.0f, 17.0f }, "Crossfade Time", 13.0f, juce::Colours::white);
    text (g, { 468.0f, 116.0f, 200.0f, 17.0f }, "Description", 14.0f, juce::Colours::white);
    for (auto r : { juce::Rectangle<float> { 466.0f, 80.0f, 284.0f, 32.0f }, juce::Rectangle<float> { 466.0f, 132.0f, 392.0f, 88.0f } })
    {
        g.setColour (kPanelDeep.brighter (0.05f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawRoundedRectangle (r, 4.0f, 1.4f);
    }

    // The Recall and Modify blocks, their side labels.
    panel (g, { 470.0f, 233.0f, 390.0f, 73.0f }, kPanelDeep.brighter (0.1f), kPanelDeep);
    panel (g, { 502.0f, 315.0f, 358.0f, 153.0f }, kPanelDeep.brighter (0.1f), kPanelDeep);
    const auto side = [&g] (juce::Rectangle<float> r, const char* s, bool on)
    {
        g.setColour (on ? kTabOn : kTabIdle);
        g.fillRoundedRectangle (r, 5.0f);
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, r.getCentreX(), r.getCentreY()));
        text (g, juce::Rectangle<float> (r.getHeight(), r.getWidth()).withCentre (r.getCentre()), s, 15.0f, on ? kTabOnText : juce::Colours::white, juce::Justification::centred);
    };
    side ({ 470.0f, 233.0f, 30.0f, 73.0f }, "Recall", false);
    side ({ 470.0f, 328.0f, 32.0f, 69.0f }, "Modify", true);
    side ({ 470.0f, 398.0f, 32.0f, 70.0f }, "Cues", false);
}

void SceneScreen::mouseDown (const juce::MouseEvent& e)
{
    if (sub == 1)
    {
        const juce::Rectangle<int> list (30, 92, 372, 33 * 13);
        if (list.contains (e.getPosition()))
            toggleUpdateScene (firstSubRow + (e.y - 92) / 33);
        return;
    }
    if (sub == 4)
    {
        const auto p = e.getPosition().toFloat();
        for (int i = 0; i < 13; ++i)
            if (juce::Rectangle<float> (26.0f, 90.0f + (float) i * 33.5f, 150.0f, 30.0f).contains (p))
            {
                if (safeList (i) != nullptr)
                {
                    safeCategory = i;
                    firstSubRow = 0;
                    repaint();
                }
                return;
            }
        const float x0 = 368.0f, pitchX = 45.0f, top = 268.0f, pitchY = 34.0f;
        for (int c = 0; c < 10; ++c)
            if (p.getDistanceFrom ({ x0 + (float) (c + 1) * pitchX, top }) < 14.0f)
            {
                toggleSafeColumn (c);
                return;
            }
        for (int i = 0; i < kSafeRowsShown; ++i)
            if (p.getDistanceFrom ({ x0, top + (float) (i + 1) * pitchY }) < 14.0f)
            {
                toggleSafeRow (safeCategory, firstSubRow + i);
                return;
            }
        return;
    }
    if (sub != 0)
        return;
    for (int i = 0; i < kMainRows; ++i)
        if (juce::Rectangle<int> (141, 113 + i * 33, 291, 32).contains (e.getPosition()))
        {
            selected = firstMain + i;
            next = selected;
            loadEditors();
            repaint();
            return;
        }
    for (int i = 0; i < kLeftRows; ++i)
        if (juce::Rectangle<int> (15, 70 + (int) ((float) i * 30.7f), 91, 28).contains (e.getPosition()))
        {
            select (firstLeft + i);
            return;
        }
}

void SceneScreen::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int step = w.deltaY > 0 ? -1 : 1;
    if (sub != 0)
    {
        firstSubRow = juce::jmax (0, firstSubRow + step);
        repaint();
        return;
    }
    if (e.x < 116)
        firstLeft = juce::jlimit (0, kSlots - kLeftRows, firstLeft + step);
    else
        firstMain = juce::jlimit (0, kSlots - kMainRows, firstMain + step);
    repaint();
}

//==============================================================================
MixRackScreen::MixRackScreen (UiContext& c, Desk& d)
    : ctx (c), desk (d), config (c, Theme::dark()), titleTab ("MixRack", true), configTab ("Config"), audioTab ("Audio"), talkbackTab ("Talkback"), muteTab ("Mute Groups"), sigGenTab ("SigGen"),
      controllersTab ("Controllers"), faders (c, d)
{
    config.revert();
    pendingMidi = getMidiChannel();
    configTab.isOn = [this] { return isConfigSub(); };
    audioTab.isOn = [this] { return ! isConfigSub(); };
    configTab.onClick = [this] { showSub (Sub::MixerConfig); };
    audioTab.onClick = [this] { showSub (Sub::IoPort); };
    for (auto* t : { &titleTab, &configTab, &audioTab, &talkbackTab, &muteTab, &sigGenTab, &controllersTab })
        addAndMakeVisible (t);
    // No photographs of these yet.
    for (auto* t : { &talkbackTab, &muteTab, &sigGenTab, &controllersTab })
        t->setEnabled (false);
    addAndMakeVisible (sends);
    sends.onTouched = [this] (Touchable& t) { touch (t); };
    sends.onLayerTapped = [this] (int l) { setSoftLayer (l); };
    addAndMakeVisible (faders);
    buildSends();
    rebuild();
    startTimerHz (5);
}

MixRackScreen::~MixRackScreen() = default;

juce::Rectangle<int> MixRackScreen::at (float x0, float y0, float x1, float y1) const
{
    // Positions as read from a photograph of the page, in its pixels.
    const float sx = 856.0f / 1220.0f, sy = 0.579f;
    const auto x = [sx] (float v) { return 14.0f + (v - 370.0f) * sx; };
    const auto y = [sy] (float v) { return 6.0f + (v - 205.0f) * sy; };
    return juce::Rectangle<float> (x (x0), y (y0), x (x1) - x (x0), y (y1) - y (y0)).toNearestInt();
}

void MixRackScreen::touch (Touchable& t)
{
    if (touchedNow != nullptr && touchedNow.getComponent() != &t)
        touchedNow->setTouched (false);
    touchedNow = &t;
    t.setTouched (true);
    if (onTouchTurn)
        onTouchTurn (t.getBinding());
}

void MixRackScreen::setStrip (Strip* s)
{
    strip = s;
    buildSends();
}

void MixRackScreen::setSoftLayer (int l)
{
    softLayer = juce::jlimit (0, 2, l);
    buildSends();
}

void MixRackScreen::buildSends()
{
    SoftWidget::Block a, b;
    sendBlocks (ctx.engine, strip, softLayer * 6, a, b);
    sends.set (a, b, softLayer, 3, false);
}

void MixRackScreen::addLabel (juce::Rectangle<int> r, const juce::String& text_, float size, juce::Justification j)
{
    labels.push_back ({ r.toFloat(), text_, size, j });
}

ScreenButton& MixRackScreen::addKey (juce::Rectangle<int> r, const juce::String& name, juce::Colour edge, std::function<void()> click)
{
    auto b = std::make_unique<ScreenButton> (name, edge);
    b->setBounds (r);
    b->onClick = std::move (click);
    addAndMakeVisible (*b);
    auto& ref = *b;
    items.push_back (std::move (b));
    return ref;
}

ScreenButton& MixRackScreen::addDim (juce::Rectangle<int> r, const juce::String& name, juce::Colour edge)
{
    auto& b = addKey (r, name, edge, {});
    b.setEnabled (false);
    return b;
}

void MixRackScreen::addCount (juce::Rectangle<int> r, universal::MixerConfigPanel::Kind kind)
{
    using K = universal::MixerConfigPanel;
    const bool stereo = ((int) kind & 1) != 0;
    const int most = kind == K::FxSends || kind == K::StereoFxSends ? K::kMaxFxSends : stereo ? K::kBusses / 2 : K::kBusses;
    const int step = stereo ? 1 : 2; // mono mixes come and go in pairs
    KnobBinding b;
    b.label = "Mixes";
    b.steps = (float) (most / step + 1);
    b.get = [this, kind, most] { return (float) config.getPending (kind) / (float) most; };
    b.set = [this, kind, most, step] (float v) { config.setPending (kind, juce::roundToInt (v * (float) most / (float) step) * step); };
    b.text = [this, kind] { return juce::String (config.getPending (kind)); };
    auto box = std::make_unique<ParamBox> (kCyan);
    box->showBar = false;
    box->setBinding (std::move (b));
    box->setBounds (r);
    box->onTouched = [this] (Touchable& x) { touch (x); };
    addAndMakeVisible (*box);
    items.push_back (std::move (box));
}

void MixRackScreen::addApplyCancel (float x0, float x1, float gap, float y0, float y1)
{
    const float w = (x1 - x0 - gap) * 0.5f;
    auto& apply = addKey (at (x0, y0, x0 + w, y1), "Apply", juce::Colours::white, {});
    apply.onClick = [this]
    {
        popups::confirm (*this, "APPLY", "Lay the mixes out like this? The mixes reset; everything else stays.", "APPLY", [this] { config.apply(); rebuild(); }, true);
    };
    addKey (at (x0 + w + gap, y0, x1, y1), "Cancel", juce::Colours::white, [this] { config.revert(); rebuild(); });
    applyKey = &apply;
}

void MixRackScreen::showSub (Sub s)
{
    sub = s;
    if (s == Sub::Network)
        pendingMidi = getMidiChannel();
    rebuild();
}

void MixRackScreen::rebuild()
{
    if (touchedNow != nullptr)
        touchedNow->setTouched (false);
    touchedNow = nullptr;
    items.clear();
    areas.clear();
    labels.clear();
    subTabs.clear();
    ammBoxes.clear();
    colourDots.clear();
    applyKey = nullptr;
    midiApply = nullptr;
    nameField = nullptr;

    struct Def
    {
        Sub s;
        const char* name;
        float x0, x1;
    };
    static const Def configTabs[] = { { Sub::MixerConfig, "Mixer Config", 405, 590 }, { Sub::InputStereos, "Input Stereos", 598, 780 }, { Sub::NameColour, "Name & Colour", 788, 970 },
                                      { Sub::Network, "Network", 980, 1160 }, { Sub::Users, "User Profiles", 1170, 1350 }, { Sub::RfDevices, "RF Devices", 1358, 1535 } };
    static const Def audioTabs[] = { { Sub::IoPort, "I/O Port", 405, 590 }, { Sub::AudioSync, "Audio Sync", 598, 780 }, { Sub::SourceSelector, "Source Selector", 788, 985 }, { Sub::Amm, "AMM", 995, 1180 } };
    const auto addTabs = [this] (const Def* defs, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const auto& d = defs[i];
            auto t = std::make_unique<ScreenTab> (d.name);
            t->setBounds (at (d.x0, 272.0f, d.x1, 332.0f));
            t->isOn = [this, s = d.s] { return sub == s; };
            t->onClick = [this, s = d.s] { showSub (s); };
            t->refresh();
            addAndMakeVisible (*t);
            subTabs.push_back ({ d.s, std::move (t) });
        }
    };
    if (isConfigSub())
        addTabs (configTabs, (int) std::size (configTabs));
    else
        addTabs (audioTabs, (int) std::size (audioTabs));
    configTab.refresh();
    audioTab.refresh();

    switch (sub)
    {
        case Sub::MixerConfig:    buildMixerConfig(); break;
        case Sub::InputStereos:   buildInputStereos(); break;
        case Sub::NameColour:     buildNameColour(); break;
        case Sub::Network:        buildNetwork(); break;
        case Sub::Users:          buildUsers(); break;
        case Sub::RfDevices:      areas.push_back ({ at (380, 340, 1580, 1085).toFloat(), {} }); addLabel (at (380, 640, 1580, 700), "No RF devices", 16.0f); break;
        case Sub::IoPort:
            areas.push_back ({ at (380, 340, 1580, 1085).toFloat(), {} });
            addLabel (at (395, 385, 1000, 440), "No I/O Port Card Fitted", 15.0f, juce::Justification::centredLeft);
            break;
        case Sub::AudioSync:      buildSync(); break;
        case Sub::SourceSelector: buildSourceSelector(); break;
        case Sub::Amm:            buildAmm(); break;
    }
    refresh();
    repaint();
}

void MixRackScreen::buildMixerConfig()
{
    using K = universal::MixerConfigPanel;
    areas.push_back ({ at (380, 340, 1270, 1085).toFloat(), {} });
    areas.push_back ({ at (1285, 340, 1580, 1085).toFloat(), {} });
    addLabel (at (560, 405, 680, 440), "Mono", 15.0f);
    addLabel (at (685, 405, 800, 440), "Stereo", 15.0f);
    struct Row
    {
        const char* name;
        K::Kind mono, stereo;
        float y;
    };
    const Row rows[] = { { "Group", K::Groups, K::StereoGroups, 460 }, { "FX", K::FxSends, K::StereoFxSends, 543 }, { "Aux", K::Aux, K::StereoAux, 627 }, { "Matrix", K::Matrices, K::StereoMatrices, 710 } };
    for (const auto& r : rows)
    {
        addLabel (at (420, r.y, 545, r.y + 58), r.name, 15.0f, juce::Justification::centredRight);
        addCount (at (567, r.y, 672, r.y + 58), r.mono);
        addCount (at (690, r.y, 795, r.y + 58), r.stereo);
    }
    addLabel (at (420, 797, 545, 855), "PAFL", 15.0f, juce::Justification::centredRight);
    {
        KnobBinding b;
        b.label = "PAFL";
        b.steps = 5.0f;
        b.get = [this] { return (float) config.getPendingPafl() / 4.0f; };
        b.set = [this] (float v) { config.setPendingPafl (juce::roundToInt (v * 4.0f)); };
        b.text = [this] { return juce::String (config.getPendingPafl()); };
        auto box = std::make_unique<ParamBox> (kCyan);
        box->showBar = false;
        box->setBinding (std::move (b));
        box->setBounds (at (690, 797, 795, 855));
        box->onTouched = [this] (Touchable& x) { touch (x); };
        addAndMakeVisible (*box);
        items.push_back (std::move (box));
    }
    addLabel (at (420, 882, 545, 937), "Main", 15.0f, juce::Justification::centredRight);
    {
        // livedaw_engine's MainMode, in its order.
        static const char* const modes[] = { "None", "LR", "LR+Msum", "LR+M", "LCR", "LCR+", "5.1" };
        const int shown = juce::jlimit (0, 6, config.getPendingMainMode());
        auto& main = addKey (at (562, 882, 797, 937), modes[shown], kCyan, {});
        main.dropdown = true;
        main.onClick = [this, &main]
        {
            juce::PopupMenu menu;
            for (int i = 0; i < 7; ++i)
                menu.addItem (i + 1, modes[i], true, i == config.getPendingMainMode());
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&main),
                                [safe = juce::Component::SafePointer<MixRackScreen> (this)] (int r)
                                {
                                    if (safe != nullptr && r > 0)
                                    {
                                        safe->config.setPendingMainMode (r - 1);
                                        safe->rebuild();
                                    }
                                });
        };
    }
    addLabel (at (853, 400, 1203, 440), "Main Channel Strips", 15.0f);
    addApplyCancel (655, 997, 30, 993, 1040);
}

void MixRackScreen::buildInputStereos()
{
    areas.push_back ({ at (385, 340, 1510, 1085).toFloat(), {} });
    const int pairs = 64;
    for (int i = 0; i < 32 && pairFirst + i < pairs; ++i)
    {
        const int pair = pairFirst + i;
        const float x = 470.0f + (float) (i % 8) * 126.0f, y = 395.0f + (float) (i / 8) * 145.0f;
        addLabel (at (x - 60, y - 16, x + 60, y + 16), juce::String (pair * 2 + 1) + " / " + juce::String (pair * 2 + 2), 14.0f);
        auto& k = addKey (at (x - 40, y + 22, x + 40, y + 62), "Mono", kCyan, {});
        k.pale = true;
        k.isOn = [this, pair] { return config.isStereoPending (pair); };
        k.litFill = kTabOn;
        k.fontSize = 14.0f;
        k.onClick = [this, pair, &k]
        {
            config.setStereoPending (pair, ! config.isStereoPending (pair));
            k.setButtonText (config.isStereoPending (pair) ? "Stereo" : "Mono");
        };
        k.setButtonText (config.isStereoPending (pair) ? "Stereo" : "Mono");
    }
    addApplyCancel (775, 1110, 35, 982, 1027);
}

const MixerEngine::StripList* MixRackScreen::typeList() const
{
    switch (nameType)
    {
        case 1:  return &ctx.engine.getAux();
        case 2:  return &ctx.engine.getBuses();
        case 3:  return &ctx.engine.getMatrices();
        case 4:  return &ctx.engine.getMains();
        default: return &ctx.engine.getChannels();
    }
}

void MixRackScreen::setRange (int type, int first, int last)
{
    nameType = juce::jlimit (0, 4, type);
    const int count = juce::jmax (1, (int) typeList()->size());
    rangeFirst = juce::jlimit (0, count - 1, first);
    rangeLast = juce::jlimit (rangeFirst, count - 1, last);
    if (sub == Sub::NameColour)
        rebuild();
}

void MixRackScreen::applyName (const juce::String& name)
{
    const auto* list = typeList();
    for (int i = rangeFirst; i <= rangeLast && i < (int) list->size(); ++i)
        (*list)[(size_t) i]->setName (name.isEmpty() || rangeFirst == rangeLast ? name : name + " " + juce::String (i - rangeFirst + 1));
}

void MixRackScreen::applyColour (int colourIndex)
{
    const auto* list = typeList();
    for (int i = rangeFirst; i <= rangeLast && i < (int) list->size(); ++i)
        (*list)[(size_t) i]->params.colour = colourIndex;
    repaint();
}

void MixRackScreen::buildNameColour()
{
    static const char* const types[] = { "Input", "FX Return", "Mix", "Matrix", "Main" };
    areas.push_back ({ at (385, 345, 1508, 690).toFloat(), "Channel Selection" });
    areas.push_back ({ at (385, 697, 825, 1060).toFloat(), "Name" });
    areas.push_back ({ at (835, 697, 1505, 1060).toFloat(), "Colour" });
    addLabel (at (590, 445, 840, 478), "Type", 15.0f);
    auto& type = addKey (at (590, 482, 840, 530), types[nameType], kCyan, {});
    type.dropdown = true;
    type.onClick = [this, &type]
    {
        juce::PopupMenu menu;
        for (int i = 0; i < 5; ++i)
            menu.addItem (i + 1, types[i], true, i == nameType);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&type), [safe = juce::Component::SafePointer<MixRackScreen> (this)] (int r)
        {
            if (safe != nullptr && r > 0)
                safe->setRange (r - 1, 0, (int) 10000);
        });
    };
    const auto rangeBox = [this] (juce::Rectangle<int> r, bool last)
    {
        KnobBinding b;
        b.label = last ? "End" : "Start";
        const int count = juce::jmax (1, (int) typeList()->size());
        b.steps = (float) count;
        b.get = [this, last, count] { return count > 1 ? (float) (last ? rangeLast : rangeFirst) / (float) (count - 1) : 0.0f; };
        b.set = [this, last, count] (float v)
        {
            const int n = juce::roundToInt (v * (float) (count - 1));
            if (last)
                rangeLast = juce::jmax (rangeFirst, n);
            else
            {
                rangeFirst = n;
                rangeLast = juce::jmax (rangeFirst, rangeLast);
            }
        };
        b.text = [this, last] { return juce::String ((last ? rangeLast : rangeFirst) + 1); };
        auto box = std::make_unique<ParamBox> (kCyan);
        box->showBar = false;
        box->setBinding (std::move (b));
        box->setBounds (r);
        box->onTouched = [this] (Touchable& x) { touch (x); };
        addAndMakeVisible (*box);
        items.push_back (std::move (box));
    };
    addLabel (at (1042, 448, 1158, 480), "Start", 15.0f);
    rangeBox (at (1042, 482, 1158, 535), false);
    addLabel (at (1192, 448, 1308, 480), "End", 15.0f);
    rangeBox (at (1192, 482, 1308, 535), true);
    addKey (at (1088, 582, 1262, 621), "Select All", juce::Colours::white, [this] { setRange (nameType, 0, 10000); });

    auto field = std::make_unique<juce::TextEditor>();
    field->setBounds (at (412, 836, 785, 880));
    field->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff4a6478));
    field->setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.5f));
    field->setColour (juce::TextEditor::textColourId, juce::Colours::white);
    field->setFont (type::font (16.0f));
    field->setJustification (juce::Justification::centred);
    field->onReturnKey = [this, f = field.get()] { applyName (f->getText()); };
    addAndMakeVisible (*field);
    nameField = field.get();
    items.push_back (std::move (field));
    addKey (at (522, 942, 670, 983), "Reset", juce::Colours::white, [this] { applyName ({}); if (nameField != nullptr) nameField->clear(); });

    for (int i = 0; i < 8; ++i)
        colourDots.push_back (juce::Rectangle<float> (46.0f, 42.0f).withCentre (at (885.0f + (float) i * 83.0f - 1.0f, 859.0f, 885.0f + (float) i * 83.0f + 1.0f, 861.0f).toFloat().getCentre()));
    addKey (at (1100, 942, 1250, 983), "Reset", juce::Colours::white, [this] { applyColour (8); });
}

void MixRackScreen::buildNetwork()
{
    areas.push_back ({ at (385, 340, 995, 1060).toFloat(), "IP Settings" });
    areas.push_back ({ at (1010, 345, 1495, 660).toFloat(), "Unit Name" });
    areas.push_back ({ at (1010, 673, 1510, 1060).toFloat(), "Connections" });
    addDim (at (635, 425, 780, 465), "Reset", kRed);
    const juce::StringArray names { "DHCP Enabled", "IP Address", "Subnet Mask", "Gateway" };
    const auto ip = juce::IPAddress::getLocalAddress().toString();
    const float ys[] = { 525, 612, 700, 788 };
    for (int i = 0; i < 4; ++i)
        addLabel (at (420, ys[i], 690, ys[i] + 42), names[i], 15.0f, juce::Justification::centredRight);
    addDim (at (713, 525, 778, 565), "OFF", juce::Colours::white).pale = true;
    addDim (at (710, 612, 930, 652), ip, kFrameLight);
    addDim (at (710, 700, 930, 740), juce::String(), kFrameLight);
    addDim (at (710, 788, 930, 830), juce::String(), kFrameLight);
    addDim (at (525, 905, 672, 948), "Apply", juce::Colours::white);
    addDim (at (700, 905, 848, 948), "Cancel", juce::Colours::white);
    addDim (at (1125, 490, 1372, 532), "dLive MixRack", kFrameLight);

    // MIDI over TCP. The console has its MIDI Channel box, with Apply and Cancel, on Utility >
    // Control > MIDI (the guide's 11.8); Sidecar has no Utility screen yet, so it sits with the
    // connections here, over what the server is doing (painted: the clients, the ports).
    addLabel (at (1030, 730, 1235, 780), "MIDI Channel", 15.0f, juce::Justification::centredRight);
    {
        KnobBinding b;
        b.label = "MIDI Channel";
        b.steps = 12.0f;
        b.get = [this] { return (float) (pendingMidi - 1) / 11.0f; };
        b.set = [this] (float v) { pendingMidi = juce::jlimit (1, 12, 1 + juce::roundToInt (v * 11.0f)); };
        b.text = [this] { return juce::String (pendingMidi) + " to " + juce::String (pendingMidi + 4); };
        auto box = std::make_unique<ParamBox> (kCyan);
        box->showBar = false;
        box->setBinding (std::move (b));
        box->setBounds (at (1250, 728, 1398, 782));
        box->onTouched = [this] (Touchable& x) { touch (x); };
        addAndMakeVisible (*box);
        items.push_back (std::move (box));
    }
    midiApply = &addKey (at (1128, 795, 1262, 838), "Apply", juce::Colours::white, [this] { applyMidiChannel(); });
    addKey (at (1276, 795, 1410, 838), "Cancel", juce::Colours::white, [this] { pendingMidi = getMidiChannel(); });
}

int MixRackScreen::getMidiChannel() const
{
    return juce::jlimit (1, 12, ctx.settings.getIntValue ("dlive.midiChannel", 1));
}

void MixRackScreen::setPendingMidiChannel (int channel)
{
    pendingMidi = juce::jlimit (1, 12, channel);
}

void MixRackScreen::applyMidiChannel()
{
    // The remote's own setting (WingRemote::Settings::load reads it): the servers take it at once.
    ctx.settings.setValue ("dlive.midiChannel", pendingMidi);
    for (const auto& error : ctx.engine.getWing().apply (WingRemote::Settings::load (ctx.settings)))
        ctx.log.error ("MIDI over TCP: " + error);
}

ScreenButton* MixRackScreen::findButton (const juce::String& name) const
{
    for (auto& i : items)
        if (auto* b = dynamic_cast<ScreenButton*> (i.get()); b != nullptr && b->getButtonText() == name)
            return b;
    return nullptr;
}

void MixRackScreen::buildUsers()
{
    areas.push_back ({ at (395, 340, 890, 1050).toFloat(), "Users" });
    areas.push_back ({ at (905, 340, 1500, 1050).toFloat(), "Settings" });
    for (int i = 0; i < 10; ++i)
    {
        const float y = 405.0f + (float) i * 55.5f;
        auto& k = addDim (at (410, y, 860, y + 47), i == 0 ? juce::String ("Admin") : "User " + juce::String (i + 1), kTeal);
        k.fontSize = 14.0f;
    }
    addDim (at (530, 1000, 738, 1040), "Active", juce::Colours::white);
    const auto row = [this] (float y0, float y1, const char* name) { addLabel (at (925, y0, 1062, y1), name, 14.0f, juce::Justification::centredRight); };
    row (405, 445, "User Name");
    addDim (at (1075, 405, 1430, 445), "Admin", kFrameLight);
    row (455, 500, "Enable\nPassword");
    addDim (at (1077, 458, 1115, 497), juce::String(), juce::Colours::white).pale = true;
    row (510, 552, "Password");
    addDim (at (1075, 510, 1433, 552), "Set Password", kFrameLight);
    row (562, 600, "Comment");
    addDim (at (1077, 562, 1440, 735), juce::String(), kFrameLight);
    row (748, 795, "User Scene");
    addDim (at (1080, 748, 1442, 795), "No Scene").dropdown = true;
    addDim (at (1115, 995, 1318, 1037), "Set Permissions", juce::Colours::white);
}

void MixRackScreen::buildSync()
{
    areas.push_back ({ at (385, 340, 1570, 1085).toFloat(), {} });
    const juce::String rate = juce::String (juce::roundToInt (ctx.engine.getSampleRate() / 1000.0)) + "kHz";
    addLabel (at (650, 360, 1170, 396), "Audio Clock Source", 15.0f);
    addDim (at (650, 400, 1170, 450), "Internal (" + rate + ")").dropdown = true;
    addLabel (at (1190, 355, 1500, 392), "Audio Sync Lock", 15.0f);
    addLabel (at (650, 505, 1400, 540), "Audio Sync Jitter Tolerance", 15.0f);
    auto& low = addDim (at (915, 560, 995, 600), "Low", kTeal);
    low.isOn = [] { return true; };
    low.litFill = kTabOnText;
    addDim (at (1040, 560, 1120, 600), "High", juce::Colours::white).pale = true;
    addLabel (at (800, 700, 1250, 770), "MixRack BNC Wordclock\nSocket", 15.0f);
    auto& in = addDim (at (980, 780, 1060, 820), "In", kTeal);
    in.isOn = [] { return true; };
    in.litFill = kTabOnText;
    addLabel (at (800, 850, 1250, 920), "BNC Wordclock Out\n(MixRack & Surface)", 15.0f);
    addDim (at (895, 935, 1145, 985), rate).dropdown = true;
}

std::vector<Strip*> MixRackScreen::selectorSources() const
{
    // The top pane: the inputs, or the mixes a selector can take (Groups, Aux and Mains;
    // Sidecar lists every mix of the 64 with the mains after them).
    std::vector<Strip*> list;
    if (! selectorMixes)
        for (const auto& s : ctx.engine.getChannels())
            list.push_back (s.get());
    else
        for (const auto* strips : { &ctx.engine.getBuses(), &ctx.engine.getMains() })
            for (const auto& s : *strips)
                list.push_back (s.get());
    return list;
}

Strip* MixRackScreen::selectorStrip (int member, float* onDb) const
{
    int32_t kind = -1, number = 0;
    float db = 0.0f;
    if (! ld::ld_selector_source (ctx.engine.getHandle(), selector, member, &kind, &number, &db) || ! juce::isPositiveAndBelow (kind, kNumStripKinds))
        return nullptr;
    for (const auto& s : ctx.engine.getStrips ((StripKind) kind))
        if (selectorNumber (*s) == number)
        {
            if (onDb != nullptr)
                *onDb = db;
            return s.get();
        }
    return nullptr;
}

int MixRackScreen::selectorMember (const Strip* s) const
{
    if (s != nullptr)
        for (int m = 0; m < kSelectorSources; ++m)
            if (selectorStrip (m) == s)
                return m;
    return -1;
}

juce::Rectangle<float> MixRackScreen::selectorCell (int i) const
{
    const float x = 500.0f + (float) (i % 16) * 57.0f, y = 405.0f + (float) (i / 16) * 150.0f;
    return at (x, y, x + 52, y + 136).toFloat();
}

juce::Rectangle<float> MixRackScreen::selectorSlot (int member) const
{
    const float x = 442.0f + (float) (member % 5) * 154.5f, y = 742.0f + (float) (member / 5) * 68.5f;
    return at (x, y, x + 143, y + 58).toFloat();
}

void MixRackScreen::chooseSelector (int s)
{
    selector = juce::jlimit (0, kSelectors - 1, s);
    selectorTouched = -1;
    if (sub == Sub::SourceSelector)
        rebuild();
}

void MixRackScreen::showSelectorMixes (bool mixes)
{
    selectorMixes = mixes;
    selectorFirst = 0;
    if (sub == Sub::SourceSelector)
        rebuild();
}

bool MixRackScreen::toggleSelectorSource (Strip* s)
{
    if (s == nullptr)
        return false;
    auto* e = ctx.engine.getHandle();
    if (const int m = selectorMember (s); m >= 0)
    {
        if (ld::ld_selector_active (e, selector) == m)
            ld::ld_selector_select (e, selector, -1);
        ld::ld_selector_set_source (e, selector, m, -1, 0);
        if (selectorTouched == m)
            selectorTouched = -1;
        repaint();
        return true;
    }
    const auto kind = s->getKind();
    if (s == getSelectorOutput() || (kind != StripKind::Channel && kind != StripKind::Bus && kind != StripKind::Main))
        return false; // a mix isn't its own source
    for (int m = 0; m < kSelectorSources; ++m)
        if (! ld::ld_selector_source (e, selector, m, nullptr, nullptr, nullptr))
        {
            ld::ld_selector_set_source (e, selector, m, (int) kind, selectorNumber (*s));
            ld::ld_selector_set_on_level (e, selector, m, 0.0f);
            repaint();
            return true;
        }
    return false;
}

KnobBinding MixRackScreen::selectorLevel (int member)
{
    auto* e = ctx.engine.getHandle();
    const int sel = selector;
    const auto level = [e, sel, member]
    {
        float db = 0.0f;
        ld::ld_selector_source (e, sel, member, nullptr, nullptr, &db);
        return db;
    };
    KnobBinding b;
    b.label = "On Level";
    b.get = [level] { return fader::dbToPos (level()); };
    b.set = [e, sel, member] (float p) { ld::ld_selector_set_on_level (e, sel, member, fader::posToDb (p)); };
    b.text = [level] { return fader::text (level()); };
    b.reset = [e, sel, member] { ld::ld_selector_set_on_level (e, sel, member, 0.0f); };
    return b;
}

KnobBinding MixRackScreen::selectorFade (bool out)
{
    auto* e = ctx.engine.getHandle();
    const int sel = selector;
    const auto read = [e, sel, out]
    {
        float in = 0.0f, o = 0.0f;
        ld::ld_selector_fades (e, sel, &in, &o);
        return out ? o : in;
    };
    KnobBinding b;
    b.label = out ? "Fade Out" : "Fade In";
    b.steps = 101.0f; // up to 10 s, in tenths
    b.get = [read] { return read() / 10.0f; };
    b.set = [e, sel, out] (float v)
    {
        float in = 0.0f, o = 0.0f;
        ld::ld_selector_fades (e, sel, &in, &o);
        (out ? o : in) = (float) juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 100.0f) / 10.0f;
        ld::ld_selector_set_fades (e, sel, in, o);
    };
    b.text = [read] { return juce::String (read(), 1) + " s"; };
    return b;
}

void MixRackScreen::playSelectorMember (int member)
{
    member = juce::jmax (-1, member);
    if (member >= 0 && selectorStrip (member) == nullptr)
        return;
    ld::ld_selector_select (ctx.engine.getHandle(), selector, member);
    selectorTouched = member;
    if (touchedNow != nullptr)
        touchedNow->setTouched (false);
    touchedNow = nullptr;
    if (member >= 0 && onTouchTurn)
        onTouchTurn (selectorLevel (member));
    repaint();
}

Strip* MixRackScreen::getSelectorOutput() const
{
    return selectorOutputOf (ctx.engine, selector);
}

void MixRackScreen::setSelectorOutput (Strip* s)
{
    auto* e = ctx.engine.getHandle();
    if (s == nullptr)
        ld::ld_selector_set_output (e, selector, -1, 0);
    else if (s->getKind() == StripKind::Bus || s->getKind() == StripKind::Matrix)
    {
        if (const int m = selectorMember (s); m >= 0) // a mix isn't its own source
            ld::ld_selector_set_source (e, selector, m, -1, 0);
        ld::ld_selector_set_output (e, selector, (int) s->getKind(), s->getId());
    }
    if (sub == Sub::SourceSelector)
        rebuild();
}

void MixRackScreen::buildSourceSelector()
{
    // The guide's 9.9, on the photographed page. Where a mouse and Sidecar differ from the
    // console, this is Sidecar's reading: a touch on a source of the top pane puts it in the
    // selector or takes it out (the console drags them down); a touch on a member plays it and
    // puts its on-level on the data rotary (the console's IP controllers choose the source; its
    // members' own names and colours aren't kept).
    auto* e = ctx.engine.getHandle();
    areas.push_back ({ at (480, 345, 1510, 715).toFloat(), {} });
    areas.push_back ({ at (430, 730, 1215, 1010).toFloat(), {} });
    areas.push_back ({ at (1228, 730, 1512, 1010).toFloat(), {} });
    static const char* const ranges[] = { "1-32", "33-64", "65-96", "97-128" };
    const int count = (int) selectorSources().size();
    for (int i = 0; i < 4; ++i)
    {
        const float x = 512.0f + (float) i * 103.0f;
        auto& k = addKey (at (x, 348, x + 92, 396), ranges[i], kTeal, [this, i] { selectorFirst = i * 32; rebuild(); });
        k.fontSize = 14.0f;
        k.isOn = [this, i] { return selectorFirst == i * 32; };
        k.litFill = kTabOn;
        k.setEnabled (i == 0 || i * 32 < count);
    }
    auto& off = addKey (at (1436, 478, 1486, 615), "Off", juce::Colours::white, [this] { playSelectorMember (-1); });
    off.pale = true;
    off.isOn = [this, e] { return ld::ld_selector_active (e, selector) < 0; };
    off.litFill = kTabOn;

    const auto title = [] (int sel, Strip* out) { return juce::String (sel + 1) + ") " + (out != nullptr ? nameOf (*out) : juce::String ("Unassigned")); };
    auto* output = getSelectorOutput();
    selectorShown = selectorSignature (ctx.engine, selector);
    addLabel (at (1235, 738, 1505, 770), "Choose Source Selector", 15.0f);
    auto& choose = addKey (at (1240, 772, 1505, 815), title (selector, output), kCyan, {});
    choose.dropdown = true;
    choose.onClick = [this, title, &choose]
    {
        juce::PopupMenu menu;
        for (int i = 0; i < kSelectors; ++i)
            menu.addItem (i + 1, title (i, selectorOutputOf (ctx.engine, i)), true, i == selector);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&choose),
                            [safe = juce::Component::SafePointer<MixRackScreen> (this)] (int r)
                            {
                                if (safe != nullptr && r > 0)
                                    safe->chooseSelector (r - 1);
                            });
    };
    addLabel (at (1235, 828, 1505, 860), "Set Output", 15.0f);
    // An Aux or a Matrix, mono or stereo (the guide); Sidecar lists every mix and matrix.
    const auto outputs = [this]
    {
        std::vector<Strip*> list;
        for (const auto* strips : { &ctx.engine.getBuses(), &ctx.engine.getMatrices() })
            for (const auto& s : *strips)
                list.push_back (s.get());
        return list;
    };
    auto& set = addKey (at (1243, 862, 1502, 900), output != nullptr ? nameOf (*output) : juce::String ("Unassigned"), juce::Colours::white, {});
    set.onClick = [this, outputs, output, &set]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Unassigned", true, output == nullptr);
        const auto list = outputs();
        for (int i = 0; i < (int) list.size(); ++i)
            menu.addItem (i + 2, nameOf (*list[(size_t) i]), true, list[(size_t) i] == output);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&set),
                            [safe = juce::Component::SafePointer<MixRackScreen> (this), outputs] (int r)
                            {
                                if (safe == nullptr || r <= 0)
                                    return;
                                const auto now = outputs();
                                safe->setSelectorOutput (r >= 2 && r - 2 < (int) now.size() ? now[(size_t) (r - 2)] : nullptr);
                            });
    };
    const auto fade = [this] (juce::Rectangle<int> r, bool out)
    {
        auto box = std::make_unique<ParamBox> (kCyan);
        box->setBinding (selectorFade (out));
        box->setBounds (r);
        box->onTouched = [this] (Touchable& x) { selectorTouched = -1; touch (x); };
        addAndMakeVisible (*box);
        items.push_back (std::move (box));
    };
    addLabel (at (1240, 918, 1360, 950), "Fade In", 15.0f);
    fade (at (1243, 950, 1358, 1005), false);
    addLabel (at (1385, 918, 1505, 950), "Fade Out", 15.0f);
    fade (at (1390, 950, 1505, 1005), true);
}

std::vector<Strip*> MixRackScreen::ammMembers (int amm) const
{
    std::vector<Strip*> members;
    for (const auto& s : ctx.engine.getChannels())
    {
        const auto* p = ld::ld_strip_amm (s->getHandle());
        if (p != nullptr && p->amm.load() == amm)
            members.push_back (s.get());
    }
    return members;
}

void MixRackScreen::buildAmm()
{
    auto* amm = ld::ld_engine_amm (ctx.engine.getHandle());
    addLabel (at (395, 325, 560, 385), "AMM", 22.0f);
    addKey (at (1218, 315, 1365, 355), "Input Assign", kTeal, {}).setEnabled (false);
    addKey (at (1380, 315, 1525, 355), "Setup", kTeal, [this] { if (onOpenSetup) onOpenSetup(); });

    const int count = amm->count.load();
    const float left = 432.0f, right = 1540.0f, gap = 10.0f;
    const float boxW = (right - left - gap * (float) (count - 1)) / (float) count;
    for (int a = 0; a < count; ++a)
    {
        const float x = left + (float) a * (boxW + gap);
        ammBoxes.push_back (at (x, 388, x + boxW, 568).toFloat());
        const float cx = x + boxW * 0.42f;
        auto& in = addDim (at (cx - 44, 548, cx + 44, 584), "IN", kKnobGreen);
        in.isOn = [] { return true; };
        in.litFill = kGreen;
        in.fontSize = 12.0f;
        addLabel (at (cx + 58, 548, cx + 108, 584), "Best\nMic", 11.0f);
        auto& best = addKey (at (cx + 110, 550, cx + 150, 584), "ON", kKnobGreen, [amm, a] { amm->bestMic[a] = ! amm->bestMic[a].load(); });
        best.isOn = [amm, a] { return amm->bestMic[a].load(); };
        best.litFill = kGreen;
        best.fontSize = 10.5f;
    }

    // The eight channels of the chosen block.
    std::vector<Strip*> all;
    for (int a = 0; a < count; ++a)
    {
        auto members = ammMembers (a);
        all.insert (all.end(), members.begin(), members.end());
    }
    for (int i = 0; i < 8; ++i)
    {
        const int at_ = ammBlock * 8 + i;
        if (! juce::isPositiveAndBelow (at_, (int) all.size()))
            continue;
        auto* member = all[(size_t) at_];
        auto* p = ld::ld_strip_amm (member->getHandle());
        const float x = 425.0f + (float) i * 140.5f;
        addLabel (at (x + 6, 610, x + 134, 650), nameOf (*member), 14.0f);
        auto& on = addKey (at (x + 25, 940, x + 115, 985), "On", kKnobGreen, [p] { p->on = ! p->on.load(); });
        on.isOn = [p] { return p->on.load(); };
        on.litFill = kGreen;
    }
}

void MixRackScreen::refresh()
{
    if (applyKey != nullptr)
        applyKey->setEnabled (config.hasChanges() && config.fits());
    if (midiApply != nullptr)
        midiApply->setEnabled (pendingMidi != getMidiChannel());
    if (sub == Sub::SourceSelector && selectorShown != selectorSignature (ctx.engine, selector))
    {
        rebuild(); // a scene or a remote moved what the keys show (this comes back here, settled)
        return;
    }
    for (auto& t : subTabs)
        t.tab->refresh();
    for (auto& i : items)
    {
        if (auto* box = dynamic_cast<Touchable*> (i.get()))
            box->refresh();
        else if (auto* key = dynamic_cast<ScreenButton*> (i.get()))
            key->refresh();
    }
    sends.refresh();
    if (sub == Sub::MixerConfig || sub == Sub::Amm || sub == Sub::NameColour || sub == Sub::SourceSelector || sub == Sub::Network)
        repaint();
}

void MixRackScreen::resized()
{
    titleTab.setBounds (at (378, 205, 550, 262));
    configTab.setBounds (at (575, 205, 682, 262));
    audioTab.setBounds (at (692, 205, 790, 262));
    talkbackTab.setBounds (at (802, 205, 932, 262));
    muteTab.setBounds (at (945, 205, 1118, 262));
    sigGenTab.setBounds (at (1130, 205, 1245, 262));
    controllersTab.setBounds (at (1257, 205, 1408, 262));
    sends.setBounds (878, 62, 120, 468);
    faders.setBounds (0, 535, kTouchW, kTouchH - 535);
}

void MixRackScreen::paint (juce::Graphics& g)
{
    g.fillAll (kBack);
    panel (g, at (372, 262, 1590, 1110).toFloat(), kFrameLight, kFrame, 7.0f);

    // The active Mix, or Main.
    {
        const juce::Rectangle<float> box (878.0f, 4.0f, 120.0f, 50.0f);
        const int bus = desk.getMixOnFaders();
        auto* mix = bus >= 0 ? stripOf (ctx.engine, { Slot::Bus, bus }) : nullptr;
        const auto fill = mix != nullptr ? mix->getColour() : juce::Colour (0xffc9cc4a);
        g.setColour (fill);
        g.fillRoundedRectangle (box, 4.0f);
        const auto pill = box.reduced (18.0f, 13.0f);
        g.setColour (fill.darker (0.35f));
        g.fillRoundedRectangle (pill, 5.0f);
        text (g, pill, mix != nullptr ? nameOf (*mix) : juce::String ("Main"), 15.0f, juce::Colours::white, juce::Justification::centred, true);
    }

    g.setColour (kCyan);
    g.drawRoundedRectangle (at (378, 332, 1584, 1098).toFloat(), 4.0f, 1.6f);
    for (const auto& a : areas)
    {
        panel (g, a.r, kPanel, kPanelDeep);
        if (a.title.isNotEmpty())
        {
            const auto head = a.r.withHeight (26.0f);
            text (g, head, a.title, 17.0f, juce::Colours::white, juce::Justification::centred);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawHorizontalLine ((int) head.getBottom(), a.r.getX(), a.r.getRight());
        }
    }
    for (const auto& l : labels)
    {
        g.setColour (juce::Colours::white);
        g.setFont (type::font (l.size));
        g.drawFittedText (l.text, l.r.toNearestInt(), l.j, 2);
    }

    if (sub == Sub::MixerConfig)
    {
        const auto card = [&] (juce::Rectangle<float> r, const char* name, bool on, int faderCount)
        {
            g.setColour (on ? kCyan : kPanelDeep);
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawRoundedRectangle (r, 5.0f, 1.5f);
            text (g, r.withTrimmedTop (r.getHeight() - 30.0f), name, 15.0f, juce::Colours::white, juce::Justification::centred);
            for (int f = 0; f < faderCount; ++f)
            {
                const float x = r.getX() + r.getWidth() * (float) (f + 1) / (float) (faderCount + 1);
                text (g, { x - 12.0f, r.getY() + 7.0f, 24.0f, 18.0f }, faderCount == 1 ? "LR" : f == 0 ? "L" : "R", 15.0f, juce::Colours::white, juce::Justification::centred);
                g.setColour (juce::Colour (0xff0b0c12));
                g.fillRect (x - 1.5f, r.getY() + 34.0f, 3.0f, r.getHeight() - 74.0f);
                g.setColour (juce::Colour (0xffd8dde2));
                g.fillRoundedRectangle (x - 14.0f, r.getCentreY() - 16.0f, 28.0f, 36.0f, 2.0f);
                g.setColour (juce::Colour (0xff5a6068));
                g.fillRect (x - 14.0f, r.getCentreY() + 1.0f, 28.0f, 2.0f);
            }
        };
        card (at (853, 450, 1003, 757).toFloat(), "Combined", true, 1);
        card (at (1048, 450, 1203, 757).toFloat(), "Individual", false, 2);
        const auto remaining = [&] (float y, const char* name, int value)
        {
            text (g, at (1290, y, 1575, y + 40).toFloat(), name, 15.0f, juce::Colours::white, juce::Justification::centred);
            text (g, at (1290, y + 48, 1575, y + 88).toFloat(), juce::String (value), 16.0f, juce::Colours::white, juce::Justification::centred);
        };
        remaining (515, "Mixes Remaining", universal::MixerConfigPanel::kBusses - config.getBussesUsed());
        remaining (790, "FX Remaining", universal::MixerConfigPanel::kMaxFxSends - config.getFxSendsUsed());
    }
    else if (sub == Sub::NameColour)
    {
        const auto* list = typeList();
        const int shown = rangeFirst < (int) list->size() ? (*list)[(size_t) rangeFirst]->getColourIndex() : 0;
        for (int i = 0; i < (int) colourDots.size(); ++i)
        {
            const auto dot = colourDots[(size_t) i];
            if (shown == i + 1)
            {
                g.setColour (juce::Colours::white.withAlpha (0.6f));
                g.fillEllipse (dot.expanded (4.0f));
            }
            g.setColour (i == 0 ? juce::Colour (0xff3e4450) : consoleColour (i + 1));
            g.fillEllipse (dot);
        }
    }
    else if (sub == Sub::IoPort)
    {
        const auto head = at (395, 345, 1565, 385).toFloat();
        g.setColour (kCyan);
        g.fillRoundedRectangle (head, 4.0f);
        text (g, head, "I/O Port 1", 15.0f, kTabOnText, juce::Justification::centred);
    }
    else if (sub == Sub::AudioSync)
    {
        juce::Path tick;
        const auto c = at (1260, 395, 1305, 435).toFloat();
        tick.startNewSubPath (c.getX(), c.getCentreY());
        tick.lineTo (c.getCentreX() - 4.0f, c.getBottom());
        tick.lineTo (c.getRight(), c.getY());
        g.setColour (juce::Colour (0xffb6e23c));
        g.strokePath (tick, juce::PathStrokeType (3.2f));
    }
    else if (sub == Sub::Network)
    {
        auto& remote = ctx.engine.getWing();
        const bool running = remote.isAhMidiRunning();
        text (g, at (1040, 852, 1500, 895).toFloat(), "Number of connections: " + juce::String (running ? remote.getAhMidiClients() : 0), 15.0f, juce::Colours::white);
        text (g, at (1040, 900, 1500, 940).toFloat(),
              running ? "MIDI over TCP: ports " + juce::String (WingRemote::ahMixRackPort) + " and " + juce::String (WingRemote::ahSurfacePort) : juce::String ("MIDI over TCP: off (SETUP > REMOTE)"), 14.0f,
              juce::Colours::white.withAlpha (0.85f));
    }
    else if (sub == Sub::SourceSelector)
    {
        side (g, at (435, 340, 482, 522).toFloat(), "Ip", ! selectorMixes);
        side (g, at (435, 530, 482, 712).toFloat(), "Mix", selectorMixes);
        const auto sources = selectorSources();
        for (int i = 0; i < 32; ++i)
        {
            const int n = selectorFirst + i;
            const auto cell = selectorCell (i);
            auto* st = juce::isPositiveAndBelow (n, (int) sources.size()) ? sources[(size_t) n] : nullptr;
            if (st == nullptr && selectorMixes)
                continue; // the inputs' page keeps its empty cells (photographed), the mixes' ends
            const auto colour = st != nullptr ? st->getColour().interpolatedWith (kFrameLight, 0.35f) : kPanelDeep;
            g.setColour (colour);
            g.fillRoundedRectangle (cell.withHeight (cell.getHeight() * 0.42f + 6.0f), 3.0f);
            const auto lower = cell.withTrimmedTop (cell.getHeight() * 0.42f);
            g.setColour (juce::Colour (0xff6f8ea2));
            g.fillRoundedRectangle (lower, 3.0f);
            g.setColour (colour.brighter (0.3f));
            g.drawRoundedRectangle (cell, 3.0f, 1.2f);
            const auto kind = st == nullptr || st->getKind() == StripKind::Channel ? juce::String ("Ip") : st->getKind() == StripKind::Main ? juce::String ("Main") : juce::String ("Mix");
            text (g, cell.withHeight (cell.getHeight() * 0.42f), kind + "\n" + juce::String (st != nullptr ? st->getNumber() : n + 1), 9.5f, juce::Colours::white, juce::Justification::centred);
            if (st != nullptr)
            {
                juce::Graphics::ScopedSaveState state (g);
                g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, lower.getCentreX(), lower.getCentreY()));
                text (g, juce::Rectangle<float> (lower.getHeight(), lower.getWidth()).withCentre (lower.getCentre()), nameOf (*st).substring (0, 7), 10.5f, juce::Colours::white, juce::Justification::centred);
            }
            if (selectorMember (st) >= 0)
            {
                // In the selector: struck through in yellow, as the guide's picture has its members.
                g.setColour (kYellow);
                g.drawLine (cell.getX() + 4.0f, cell.getBottom() - 4.0f, cell.getRight() - 4.0f, cell.getY() + 4.0f, 3.0f);
            }
        }
        const int active = ld::ld_selector_active (ctx.engine.getHandle(), selector);
        for (int m = 0; m < kSelectorSources; ++m)
        {
            const auto slot = selectorSlot (m);
            float db = 0.0f;
            auto* st = selectorStrip (m, &db);
            g.setColour (juce::Colour (0xff7d97a9));
            g.fillRect (slot);
            const auto head = slot.withHeight (slot.getHeight() * 0.36f);
            g.setColour (st != nullptr && m == active ? kGreen : juce::Colour (0xff35485a));
            g.fillRect (head);
            if (st == nullptr)
                continue;
            text (g, head, st->getKind() == StripKind::Channel ? "Ip " + juce::String (st->getNumber()) : st->getLabel(), 10.5f, juce::Colours::white, juce::Justification::centred);
            auto body = slot.withTrimmedTop (head.getHeight());
            const auto bar = body.removeFromBottom (5.0f).reduced (4.0f, 1.0f);
            text (g, body, nameOf (*st).substring (0, 12), 11.5f, juce::Colours::white, juce::Justification::centred, true);
            g.setColour (juce::Colour (0xff070914));
            g.fillRect (bar);
            g.setColour (juce::Colour (0xffd8e6ee));
            g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, fader::dbToPos (db))));
            if (m == selectorTouched)
            {
                g.setColour (juce::Colours::white);
                g.drawRect (slot, 2.0f);
            }
        }
    }
    else if (sub == Sub::Amm)
    {
        auto* amm = ld::ld_engine_amm (ctx.engine.getHandle());
        const auto mode = at (590, 322, 800, 372).toFloat();
        text (g, mode.withWidth (50.0f), "Mode:", 14.0f, juce::Colours::white, juce::Justification::centredLeft, true);
        text (g, mode.withTrimmedLeft (50.0f), amm->mode.load() == 0 ? "NOM" : "D-Classic", 24.0f, juce::Colours::white);
        const juce::Colour edges[] = { juce::Colour (0xfff06a3a), juce::Colour (0xffa8e870), juce::Colour (0xff4ad2f0), juce::Colour (0xffe8d84a) };
        int block = 0;
        for (int a = 0; a < (int) ammBoxes.size(); ++a)
        {
            const auto box = ammBoxes[(size_t) a];
            g.setColour (edges[a % 4]);
            g.drawRoundedRectangle (box, 4.0f, 2.2f);
            const auto members = ammMembers (a);
            const int blocks = 8 / (int) ammBoxes.size();
            const float w = box.getWidth() / (float) blocks;
            for (int b = 0; b < blocks; ++b, ++block)
            {
                const auto cell = juce::Rectangle<float> (box.getX() + (float) b * w, box.getY(), w, box.getHeight() - 24.0f).reduced (6.0f, 6.0f);
                if (block == ammBlock)
                {
                    g.setColour (kCyan.withAlpha (0.55f));
                    g.fillRect (cell);
                }
                for (int m = 0; m < 8; ++m)
                {
                    const float x = cell.getX() + (float) m * cell.getWidth() / 8.0f + 1.5f, barW = cell.getWidth() / 8.0f - 3.0f;
                    g.setColour (kPanelDeep.darker (0.6f));
                    g.fillRect (x, cell.getY(), barW, cell.getHeight());
                    const int at_ = b * 8 + m;
                    if (juce::isPositiveAndBelow (at_, (int) members.size()))
                    {
                        const float gain = ld::ld_strip_amm (members[(size_t) at_]->getHandle())->gainDb.load();
                        const float f = juce::jlimit (0.0f, 1.0f, 1.0f + gain / 60.0f);
                        g.setColour (kKnobGreen);
                        g.fillRect (x, cell.getBottom() - cell.getHeight() * f, barW, cell.getHeight() * f);
                    }
                }
            }
        }
        const auto strips_ = at (425, 595, 1550, 1095).toFloat();
        g.setColour (kPanelDeep.darker (0.15f));
        g.fillRect (strips_);
        g.setColour (juce::Colour (0xffa8e870).withAlpha (0.8f));
        for (int c = 0; c <= 8; ++c)
            g.drawVerticalLine ((int) (strips_.getX() + strips_.getWidth() * (float) c / 8.0f), strips_.getY(), strips_.getBottom());
        g.drawHorizontalLine ((int) strips_.getY(), strips_.getX(), strips_.getRight());
    }
}

void MixRackScreen::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition().toFloat();
    if (sub == Sub::NameColour)
    {
        for (int i = 0; i < (int) colourDots.size(); ++i)
            if (colourDots[(size_t) i].contains (p))
                applyColour (i + 1);
    }
    else if (sub == Sub::Amm)
    {
        int block = 0;
        for (const auto& box : ammBoxes)
        {
            const int blocks = 8 / (int) ammBoxes.size();
            for (int b = 0; b < blocks; ++b, ++block)
                if (juce::Rectangle<float> (box.getX() + (float) b * box.getWidth() / (float) blocks, box.getY(), box.getWidth() / (float) blocks, box.getHeight() - 24.0f).contains (p))
                {
                    ammBlock = block;
                    rebuild();
                    return;
                }
        }
    }
    else if (sub == Sub::SourceSelector)
    {
        if (at (435, 340, 482, 522).toFloat().contains (p) || at (435, 530, 482, 712).toFloat().contains (p))
        {
            showSelectorMixes (p.y >= at (435, 530, 482, 712).toFloat().getY());
            return;
        }
        const auto sources = selectorSources();
        for (int i = 0; i < 32; ++i)
            if (selectorCell (i).contains (p))
            {
                if (juce::isPositiveAndBelow (selectorFirst + i, (int) sources.size()))
                    toggleSelectorSource (sources[(size_t) (selectorFirst + i)]);
                return;
            }
        for (int m = 0; m < kSelectorSources; ++m)
            if (selectorSlot (m).contains (p))
            {
                playSelectorMember (m);
                return;
            }
    }
}

void MixRackScreen::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (sub == Sub::InputStereos)
    {
        pairFirst = juce::jlimit (0, 32, pairFirst + (w.deltaY > 0 ? -8 : 8));
        rebuild();
    }
    else if (sub == Sub::SourceSelector)
    {
        // Over a member: its on-level, a dB a notch.
        for (int m = 0; m < kSelectorSources; ++m)
        {
            float db = 0.0f;
            if (selectorSlot (m).contains (e.position) && selectorStrip (m, &db) != nullptr)
            {
                ld::ld_selector_set_on_level (ctx.engine.getHandle(), selector, m, juce::jlimit (-90.0f, 10.0f, db + (w.deltaY > 0 ? 1.0f : -1.0f)));
                repaint();
                return;
            }
        }
    }
}

//==============================================================================
namespace
{
    /** A strip's factory name, Aux1 or StGrp12, in two: Aux, 1 (false: it isn't one). */
    bool splitName (const juce::String& name, juce::String& kind, juce::String& number)
    {
        const int digits = name.indexOfAnyOf ("0123456789");
        if (digits <= 0 || ! name.substring (digits).containsOnly ("0123456789"))
            return false;
        kind = name.substring (0, digits);
        number = name.substring (digits);
        return true;
    }

    const juce::Colour kMeterGreen (0xff35c060), kMeterAmber (0xffe8b030), kMeterRed (0xffe03a30), kCellMuted (0xfff0606a);
    constexpr float kMeterTop = 18.0f, kMeterBottom = -28.0f;
}

MetersScreen::MetersScreen (UiContext& c, Desk& d) : ctx (c), desk (d), titleTab ("Meters", true), faders (c, d)
{
    addAndMakeVisible (titleTab);
    static const char* const names[] = { "Inputs", "FX", "UFX", "Mix", "User 1", "User 2", "User 3", "User 4", "RTA 1", "RTA 2" };
    for (int i = 0; i < NumTabs; ++i)
    {
        auto t = std::make_unique<ScreenTab> (names[i]);
        t->isOn = [this, i] { return tab == i; };
        t->onClick = [this, i] { showTab (i); };
        addAndMakeVisible (*t);
        tabs.push_back (std::move (t));
    }
    addAndMakeVisible (sends);
    sends.onTouched = [this] (Touchable& t)
    {
        if (touchedNow != nullptr && touchedNow.getComponent() != &t)
            touchedNow->setTouched (false);
        touchedNow = &t;
        t.setTouched (true);
        if (onTouchTurn)
            onTouchTurn (t.getBinding());
    };
    sends.onLayerTapped = [this] (int l) { setSoftLayer (l); };
    addAndMakeVisible (faders);
    bands.levels.fill (-120.0f);
    bands.peaks.fill (-120.0f);
    maxBands = bands;
    setStrip (nullptr);
    rebuild();
    startTimerHz (15);
}

MetersScreen::~MetersScreen() = default;

void MetersScreen::setStrip (Strip* s)
{
    strip = s;
    SoftWidget::Block a, b;
    sendBlocks (ctx.engine, strip, softLayer * 6, a, b);
    sends.set (a, b, softLayer, 3, false);
    if (tab == Rta1 || tab == Rta2)
        repaint();
}

void MetersScreen::setSoftLayer (int l)
{
    softLayer = juce::jlimit (0, 2, l);
    setStrip (strip);
}

void MetersScreen::showTab (int t)
{
    tab = juce::jlimit (0, (int) NumTabs - 1, t);
    first = 0;
    rebuild();
}

void MetersScreen::addRow (const std::vector<Cell>& row, float y, float h, float pitch, float x0)
{
    float x = x0;
    for (const auto& c : row)
    {
        auto cell = c;
        cell.r = { x, y, pitch - 2.5f, h };
        cells.push_back (cell);
        x += pitch;
    }
}

void MetersScreen::rebuild()
{
    cells.clear();
    scales.clear();
    rtaKeys.clear();
    for (auto& t : tabs)
        t->refresh();

    auto& e = ctx.engine;
    const auto cellOf = [] (Strip& s, const juce::String& top, const juce::String& bottom, const juce::String& key, juce::Colour body)
    {
        Cell c;
        c.strip = &s;
        c.top = top;
        c.bottom = bottom;
        c.key = key;
        c.body = body;
        c.bars = s.params.stereo.load() ? 2 : 1;
        return c;
    };
    const juce::Colour teal (0xff1f8f7c), blue (0xff3f7fc4), cyan (0xff3aa6c8), violet (0xff7a68b8), olive (0xff7c8a48), green (0xff1f8f6a);

    if (tab == Inputs)
    {
        // Two rows of 24, a gap after every 12 (the wheel scrolls on).
        const auto& list = e.getChannels();
        for (int row = 0; row < 2; ++row)
        {
            const float y = 69.0f + (float) row * 228.0f, h = 222.0f;
            scales.push_back ({ 16.0f, y, h });
            scales.push_back ({ 836.0f, y, h });
            for (int i = 0; i < 24; ++i)
            {
                const int n = first + row * 24 + i;
                if (! juce::isPositiveAndBelow (n, (int) list.size()))
                    continue;
                auto& s = *list[(size_t) n];
                auto c = cellOf (s, {}, juce::String (n + 1), s.getName().isNotEmpty() ? s.getName() : juce::String (n + 1), s.getColour().getPerceivedBrightness() < 0.15f ? teal : s.getColour().interpolatedWith (teal, 0.25f));
                c.r = { 46.0f + (float) i * 32.0f + (float) (i / 12) * 8.0f, y, 29.5f, h };
                cells.push_back (c);
            }
        }
    }
    else if (tab == Fx || tab == Ufx)
    {
        std::vector<Cell> sendsRow, returnsRow;
        if (tab == Fx)
        {
            for (const auto& b : e.getBuses())
            {
                juce::String kind, number;
                if (splitName (b->getName(), kind, number) && (kind == "FX" || kind == "StFX"))
                    sendsRow.push_back (cellOf (*b, kind, number, number, cyan));
            }
            int n = 0;
            for (const auto& a : e.getAux())
            {
                ++n;
                returnsRow.push_back (cellOf (*a, "FXRet", juce::String (n), juce::String (n), green));
            }
        }
        else
        {
            // RackUltra isn't emulated yet: its meters, idle.
            for (int i = 0; i < 16; ++i)
            {
                Cell c;
                c.top = i < 8 ? "UFX" : "UFXR";
                c.bottom = juce::String (i % 8 + 1);
                c.key = c.bottom;
                c.body = i < 8 ? cyan : green;
                c.bars = 2;
                (i < 8 ? sendsRow : returnsRow).push_back (c);
            }
        }
        const float pitch = 42.0f, w = 800.0f;
        const auto limited = [&pitch, w] (std::vector<Cell>& row) { if ((float) row.size() * pitch > w) row.resize ((size_t) (w / pitch)); };
        limited (sendsRow);
        limited (returnsRow);
        const float top = 60.0f, h = 224.0f;
        const auto centre = [&] (const std::vector<Cell>& row, float y)
        {
            const float x0 = 442.0f - (float) row.size() * pitch * 0.5f;
            addRow (row, y, h, pitch, x0);
            if (! row.empty())
            {
                scales.push_back ({ x0 - 30.0f, y, h });
                scales.push_back ({ x0 + (float) row.size() * pitch + 2.0f, y, h });
            }
        };
        centre (sendsRow, top);
        centre (returnsRow, top + h + 8.0f);
    }
    else if (tab == Mix)
    {
        // By kind, flowing over two rows, as the photograph has them.
        std::vector<std::vector<Cell>> groups;
        const char* const order[] = { "Grp", "StGrp", "Aux", "StAux" };
        for (const char* wanted : order)
        {
            std::vector<Cell> g;
            for (const auto& b : e.getBuses())
            {
                juce::String kind, number;
                if (splitName (b->getName(), kind, number) && kind == wanted)
                    g.push_back (cellOf (*b, kind, number, number, kind.contains ("Grp") ? blue : cyan));
            }
            if (! g.empty())
                groups.push_back (std::move (g));
        }
        {
            std::vector<Cell> g;
            for (const auto& m : e.getMains())
            {
                const auto signal = m->getName().isNotEmpty() ? m->getName() : juce::String ("LR"); // the Main mode's: LR, M, C, LFE, Ls/Rs
                auto c = cellOf (*m, "Main", "(" + signal + ")", signal, olive);
                c.bars = 1;
                g.push_back (c);
            }
            if (! g.empty())
                groups.push_back (std::move (g));
        }
        for (const char* wanted : { "Mtx", "StMtx" })
        {
            std::vector<Cell> g;
            int n = 0;
            for (const auto& m : e.getMatrices())
            {
                juce::String kind, number;
                const bool named = splitName (m->getName(), kind, number);
                ++n;
                const bool stereo = named ? kind == "StMtx" : m->params.stereo.load();
                if (stereo == (juce::String (wanted) == "StMtx"))
                    g.push_back (cellOf (*m, wanted, named ? number : juce::String (n), named ? number : juce::String (n), violet));
            }
            if (! g.empty())
                groups.push_back (std::move (g));
        }
        const float pitch = 34.5f, h = 222.0f, gap = 16.0f, right = 862.0f;
        float x = 50.0f, y = 69.0f;
        scales.push_back ({ 20.0f, y, h });
        for (auto& g : groups)
        {
            for (size_t i = 0; i < g.size(); ++i)
            {
                if (x + pitch > right)
                {
                    x = 50.0f;
                    y += h + 6.0f;
                    scales.push_back ({ 20.0f, y, h });
                }
                g[i].r = { x, y, pitch - 2.5f, h };
                cells.push_back (g[i]);
                x += pitch;
            }
            x += gap;
        }
    }
    else if (tab == Rta1 || tab == Rta2)
        buildRta();
    repaint();
}

void MetersScreen::buildRta()
{
    const auto add = [this] (juce::Rectangle<int> r, const char* name, juce::Colour edge, std::function<void()> click)
    {
        auto b = std::make_unique<ScreenButton> (name, edge);
        b->setBounds (r);
        b->pale = true;
        b->fontSize = 14.0f;
        b->onClick = std::move (click);
        addAndMakeVisible (*b);
        rtaKeys.push_back (std::move (b));
        return rtaKeys.back().get();
    };
    const juce::Colour lit (0xff17b63a);
    const char* const modes[] = { "Line", "Bar", "Sono" };
    for (int m = 0; m < 3; ++m)
    {
        auto* k = add ({ 620 + m * 80, 60, 74, 28 }, modes[m], juce::Colours::white, [this, m] { rtaMode = m; });
        k->isOn = [this, m] { return rtaMode == m; };
        k->litFill = lit;
        k->setEnabled (m != 2); // the spectrogram isn't built yet
    }
    add ({ 44, 408, 96, 32 }, "Library", kKnobGreen, {})->setEnabled (false);
    add ({ 620, 406, 74, 32 }, "Dual View", juce::Colours::white, {})->setEnabled (false);
    {
        auto* freeze = add ({ 700, 406, 74, 32 }, "Freeze", juce::Colours::white, {});
        freeze->isOn = [this] { return Analysis (ctx.engine.getHandle()).isFrozen(); };
        freeze->litFill = lit;
        freeze->onClick = [this]
        {
            Analysis a (ctx.engine.getHandle());
            a.setFrozen (! a.isFrozen());
        };
    }
    add ({ 780, 406, 74, 32 }, "Clear", juce::Colours::white, [this] { maxBands.levels.fill (-120.0f); });
    {
        auto* maximum = add ({ 110, 458, 90, 26 }, "Maximum", juce::Colours::white, {});
        maximum->isOn = [this] { return holdMax; };
        maximum->litFill = lit;
        maximum->onClick = [this] { holdMax = ! holdMax; maxBands.levels.fill (-120.0f); };
        auto* damp = add ({ 110, 492, 90, 26 }, "LF Damp", juce::Colours::white, {});
        damp->setEnabled (false);
    }
    const std::pair<juce::Rectangle<int>, bool MetersScreen::*> onKeys[] = { { { 332, 462, 150, 20 }, &MetersScreen::avgOn }, { { 500, 462, 150, 20 }, &MetersScreen::pkOn } };
    int n = 0;
    for (auto [r, flag] : onKeys)
    {
        auto* k = add (r, n == 0 ? "Avg" : "Pk", n == 0 ? juce::Colour (0xffe040c8) : kOrange, {});
        k->isOn = [this, flag = flag] { return this->*flag; };
        k->litFill = (n == 0 ? juce::Colour (0xffc030a8) : juce::Colour (0xffd89020));
        k->onClick = [this, flag = flag] { this->*flag = ! (this->*flag); };
        ++n;
    }
}

void MetersScreen::refresh()
{
    for (auto& t : tabs)
        t->refresh();
    for (auto& k : rtaKeys)
        k->refresh();
    sends.refresh();
    repaint();
}

void MetersScreen::timerCallback()
{
    if (! isShowing())
        return;
    if (tab == Rta1 || tab == Rta2)
    {
        Analysis a (ctx.engine.getHandle());
        if (strip != nullptr && ! a.controls.rtaOn.load())
            a.showRta (strip, ld::kRtaViewEq);
        a.read (bands);
        if (holdMax)
            for (int b = 0; b < Analysis::numBands; ++b)
                maxBands.levels[(size_t) b] = juce::jmax (maxBands.levels[(size_t) b], bands.levels[(size_t) b]);
    }
    refresh();
}

void MetersScreen::resized()
{
    titleTab.setBounds (14, 10, 112, 36);
    int x = 134;
    for (auto& t : tabs)
    {
        const int w = t->getButtonText().length() <= 3 ? 54 : 72; // these fit a C1500's page too
        t->setBounds (x, 8, w, 38);
        x += w + 5;
    }
    sends.setBounds (878, 62, 120, 468);
    faders.setBounds (0, 535, kTouchW, kTouchH - 535);
}

void MetersScreen::paintCell (juce::Graphics& g, const Cell& c) const
{
    const auto r = c.r;
    g.setColour (c.body);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    const float wellH = r.getHeight() * 0.56f;
    const auto well = juce::Rectangle<float> (r.getX() + 3.0f, r.getY() + 5.0f, r.getWidth() - 6.0f, wellH);
    const auto bar = [&] (juce::Rectangle<float> b, float linear)
    {
        const float db = juce::Decibels::gainToDecibels (linear, -100.0f) + 18.0f; // 0 on the meter is -18 dBFS
        const float level = juce::jlimit (0.0f, 1.0f, (db - kMeterBottom) / (kMeterTop - kMeterBottom));
        const float y0 = (kMeterTop - 0.0f) / (kMeterTop - kMeterBottom), y1 = (kMeterTop - 12.0f) / (kMeterTop - kMeterBottom);
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (b.toNearestInt());
        const float red = b.getY() + b.getHeight() * y1, amber = b.getY() + b.getHeight() * y0;
        g.setColour (kMeterRed.withAlpha (0.38f));
        g.fillRect (b.withBottom (red));
        g.setColour (kMeterAmber.withAlpha (0.38f));
        g.fillRect (b.withTop (red).withBottom (amber));
        g.setColour (kMeterGreen.withAlpha (0.38f));
        g.fillRect (b.withTop (amber));
        g.reduceClipRegion (b.withTop (b.getBottom() - b.getHeight() * level).toNearestInt());
        g.setColour (kMeterRed);
        g.fillRect (b.withBottom (red));
        g.setColour (kMeterAmber);
        g.fillRect (b.withTop (red).withBottom (amber));
        g.setColour (kMeterGreen);
        g.fillRect (b.withTop (amber));
    };
    const float gr = c.strip != nullptr ? c.strip->meters.dynReductionDb.load() : 0.0f;
    const bool input = c.strip != nullptr && c.strip->isInput();
    const auto point = input ? MeterPoint::PreFader : MeterPoint::Output;
    if (c.bars == 2)
    {
        const float w = (well.getWidth() - 2.0f) * 0.5f;
        bar (well.withWidth (w), c.strip != nullptr ? ctx.meters.get (c.strip, point, 0) : 0.0f);
        bar (well.withTrimmedLeft (w + 2.0f), c.strip != nullptr ? ctx.meters.get (c.strip, point, 1) : 0.0f);
    }
    else
    {
        const float w = well.getWidth() * 0.62f;
        bar (well.withWidth (w), c.strip != nullptr ? ctx.meters.get (c.strip, point, 0) : 0.0f);
        // The gain reduction beside it, down from the top.
        const auto side = juce::Rectangle<float> (well.getRight() - 4.0f, well.getY() + well.getHeight() * 0.32f, 3.5f, well.getHeight() * 0.62f);
        g.setColour (juce::Colour (0xff0b0c12));
        g.fillRect (side);
        g.setColour (kMeterAmber);
        g.fillRect (side.withHeight (side.getHeight() * juce::jlimit (0.0f, 1.0f, -gr / 24.0f)));
    }

    auto label = juce::Rectangle<float> (r.getX() - 3.0f, well.getBottom() + 2.0f, r.getWidth() + 6.0f, r.getHeight() * 0.14f);
    if (c.top.isNotEmpty())
    {
        text (g, label.removeFromTop (label.getHeight() * 0.5f), c.top, c.top.length() > 4 ? 9.0f : 11.0f, juce::Colours::white, juce::Justification::centred);
        text (g, label, c.bottom, 11.0f, juce::Colours::white, juce::Justification::centred);
    }
    else
        text (g, label, c.bottom, 11.5f, juce::Colours::white, juce::Justification::centred);

    const auto key = juce::Rectangle<float> (r.getX() + 2.0f, r.getBottom() - r.getHeight() * 0.255f, r.getWidth() - 4.0f, r.getHeight() * 0.255f - 3.0f);
    const bool muted = c.strip != nullptr && c.strip->getMuteState() != 0;
    const bool main = c.key == "LR";
    g.setColour (c.strip == nullptr ? juce::Colour (0xff5d7488) : muted ? kCellMuted : main ? juce::Colour (0xff1b2038) : c.body.darker (0.55f));
    g.fillRoundedRectangle (key, 3.0f);
    g.setColour (main ? kYellow : juce::Colours::white.withAlpha (0.55f));
    g.drawRoundedRectangle (key, 3.0f, 1.0f);
    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, key.getCentreX(), key.getCentreY()));
    text (g, juce::Rectangle<float> (key.getHeight(), key.getWidth()).withCentre (key.getCentre()), c.key.substring (0, 7), 11.5f, juce::Colours::white, juce::Justification::centred, true);
}

void MetersScreen::paintRta (juce::Graphics& g)
{
    const juce::Rectangle<float> back (22.0f, 54.0f, 840.0f, 472.0f);
    g.setColour (juce::Colour (0xff10131c));
    g.fillRect (back);
    text (g, { 40.0f, 58.0f, 200.0f, 32.0f }, tab == Rta1 ? "RTA1" : "RTA2", 27.0f, kTeal, juce::Justification::centredLeft, true);

    const juce::Rectangle<float> plot (70.0f, 94.0f, 760.0f, 290.0f);
    const auto xOf = [&plot] (float hz) { return plot.getX() + plot.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
    const auto yOf = [&plot] (float db) { return plot.getY() + plot.getHeight() * (20.0f - db) / 80.0f; };
    const auto grid = juce::Colour (0xffc8d8e0);
    for (float decade : { 10.0f, 100.0f, 1000.0f, 10000.0f })
        for (int m = 1; m < 10; ++m)
        {
            const float hz = decade * (float) m;
            if (hz >= 20.0f && hz <= 20000.0f)
            {
                g.setColour (grid.withAlpha (m == 1 || m == 2 || m == 5 ? 0.7f : 0.42f));
                g.drawVerticalLine ((int) xOf (hz), plot.getY(), plot.getBottom());
            }
        }
    for (int db = 20; db >= -60; db -= 10)
    {
        g.setColour (grid.withAlpha (0.55f));
        g.drawHorizontalLine ((int) yOf ((float) db), plot.getX(), plot.getRight());
        text (g, { plot.getX() - 30.0f, yOf ((float) db) - 7.0f, 26.0f, 14.0f }, juce::String (db), 10.5f, juce::Colours::white, juce::Justification::centredRight);
        text (g, { plot.getRight() + 4.0f, yOf ((float) db) - 7.0f, 26.0f, 14.0f }, juce::String (db), 10.5f, juce::Colours::white);
    }
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        text (g, { xOf (hz) - 20.0f, plot.getBottom() + 3.0f, 40.0f, 14.0f }, hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz), 10.5f, juce::Colours::white, juce::Justification::centred);

    const auto levelY = [&] (float dbfs) { return juce::jlimit (plot.getY(), plot.getBottom(), yOf (dbfs + 18.0f)); };
    const auto traced = [&] (const std::array<float, ld::kRtaBands>& v, juce::Colour c)
    {
        juce::Path p;
        bool started = false;
        for (int b = 0; b < Analysis::numBands; ++b)
        {
            const float hz = Analysis::bandHz (b);
            if (hz < 20.0f || hz > 20000.0f)
                continue;
            if (started)
                p.lineTo (xOf (hz), levelY (v[(size_t) b]));
            else
                p.startNewSubPath (xOf (hz), levelY (v[(size_t) b]));
            started = true;
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    };
    const juce::Colour avg (0xffe040c8), pk = kOrange;
    if (rtaMode == 1)
    {
        for (int b = 0; b < Analysis::numBands; ++b)
        {
            const float hz = Analysis::bandHz (b);
            if (hz < 20.0f || hz > 20000.0f)
                continue;
            const float x0 = xOf (Analysis::bandLowHz (b)), x1 = xOf (Analysis::bandHighHz (b));
            if (avgOn)
            {
                g.setColour (avg.withAlpha (0.8f));
                g.fillRect (x0, levelY (bands.levels[(size_t) b]), juce::jmax (1.0f, x1 - x0 - 1.0f), plot.getBottom() - levelY (bands.levels[(size_t) b]));
            }
            if (pkOn)
            {
                g.setColour (pk);
                g.fillRect (x0, levelY (bands.peaks[(size_t) b]), juce::jmax (1.0f, x1 - x0 - 1.0f), 1.5f);
            }
        }
    }
    else
    {
        if (avgOn)
            traced (bands.levels, avg);
        if (pkOn)
            traced (bands.peaks, pk);
    }
    if (holdMax)
        traced (maxBands.levels, juce::Colours::white.withAlpha (0.7f));

    // The level at the left of the plot.
    {
        const juce::Rectangle<float> m (31.0f, 94.0f, 8.0f, 290.0f);
        g.setColour (juce::Colour (0xff0b0c12));
        g.fillRect (m);
        if (strip != nullptr)
        {
            const float y = levelY (juce::Decibels::gainToDecibels (ctx.meters.get (strip, MeterPoint::PreFader, 0), -100.0f));
            g.setColour (kMeterGreen);
            g.fillRect (m.withTop (y));
        }
    }

    const auto source = strip == nullptr ? juce::String() : strip->getKind() == StripKind::Channel ? "Input " + strip->getLabel().retainCharacters ("0123456789") : strip->getLabel();
    text (g, { 150.0f, 412.0f, 300.0f, 22.0f }, "Source: " + source, 14.0f, juce::Colours::white);

    // Under the plot: its Control tab; the two ballistics' boxes (their knobs aren't wired).
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawHorizontalLine (446, back.getX(), back.getRight());
    for (auto [x0, name, on] : { std::tuple<float, const char*, bool> { 300.0f, "Control", true }, { 372.0f, "Source", false }, { 444.0f, "SigGen", false } })
    {
        juce::Rectangle<float> t (x0, 448.0f, 70.0f, 14.0f);
        text (g, t, name, 12.5f, on ? juce::Colours::white : juce::Colours::white.withAlpha (0.45f), juce::Justification::centred);
    }
    for (auto [x, c] : { std::pair<float, juce::Colour> { 332.0f, avg }, { 500.0f, pk } })
    {
        const juce::Rectangle<float> box (x, 482.0f, 150.0f, 40.0f);
        g.setColour (c.withAlpha (0.9f));
        g.drawRect (box, 1.8f);
        text (g, box.reduced (8.0f, 2.0f), "Slow", 11.0f, juce::Colours::white.withAlpha (0.7f));
        text (g, box.reduced (8.0f, 2.0f), "Fast", 11.0f, juce::Colours::white.withAlpha (0.7f), juce::Justification::centredRight);
        const auto dial = juce::Rectangle<float> (30.0f, 30.0f).withCentre (box.getCentre());
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawEllipse (dial, 1.8f);
        g.drawVerticalLine ((int) dial.getCentreX(), dial.getY() - 1.0f, dial.getCentreY());
    }
}

void MetersScreen::paint (juce::Graphics& g)
{
    g.fillAll (kBack);
    {
        const juce::Rectangle<float> box (878.0f, 4.0f, 120.0f, 50.0f);
        const int bus = desk.getMixOnFaders();
        auto* mix = bus >= 0 ? stripOf (ctx.engine, { Slot::Bus, bus }) : nullptr;
        const auto fill = mix != nullptr ? mix->getColour() : juce::Colour (0xffc9cc4a);
        g.setColour (fill);
        g.fillRoundedRectangle (box, 4.0f);
        const auto pill = box.reduced (18.0f, 13.0f);
        g.setColour (fill.darker (0.35f));
        g.fillRoundedRectangle (pill, 5.0f);
        text (g, pill, mix != nullptr ? nameOf (*mix) : juce::String ("Main"), 15.0f, juce::Colours::white, juce::Justification::centred, true);
    }
    panel (g, { 14.0f, 46.0f, 856.0f, 484.0f }, kFrameLight, kFrame, 7.0f);
    panel (g, { 20.0f, 52.0f, 844.0f, 472.0f }, kPanel, kPanelDeep);

    if (tab == Rta1 || tab == Rta2)
    {
        paintRta (g);
        return;
    }
    if (tab >= User1 && tab <= User4)
    {
        text (g, { 20.0f, 250.0f, 844.0f, 30.0f }, "Enter Setup to add meters", 20.0f, juce::Colours::white, juce::Justification::centred);
        juce::Path gear;
        gear.addStar ({ 442.0f, 306.0f }, 8, 15.0f, 22.0f);
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.fillPath (gear);
        g.setColour (kPanelDeep);
        g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre ({ 442.0f, 306.0f }));
        return;
    }
    if (tab == Inputs || tab == Mix)
        text (g, { 34.0f, 53.0f, 500.0f, 15.0f }, juce::String ("Current Meter Source Point:  ") + (tab == Inputs ? "Pre Fader" : "Post Fader"), 12.5f, juce::Colours::white);
    for (const auto& s : scales)
    {
        const float wellH = s.h * 0.56f;
        const auto yAt = [&] (float db) { return s.y + 5.0f + wellH * (kMeterTop - db) / (kMeterTop - kMeterBottom); };
        for (int db : { 10, 0, -10, -20 })
            text (g, { s.x - 2.0f, yAt ((float) db) - 7.0f, 30.0f, 14.0f }, (db > 0 ? "+" : "") + juce::String (db), 10.0f, juce::Colours::white, juce::Justification::centred);
    }
    for (const auto& c : cells)
        paintCell (g, c);
}

void MetersScreen::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition().toFloat();
    for (const auto& c : cells)
        if (c.r.contains (p) && c.strip != nullptr)
        {
            if (p.y > c.r.getBottom() - c.r.getHeight() * 0.255f)
                ctx.engine.pressMute (*c.strip);
            else if (ctx.selectStrip)
                ctx.selectStrip (c.strip);
            return;
        }
}

void MetersScreen::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (tab != Inputs)
        return;
    const int count = (int) ctx.engine.getChannels().size();
    first = juce::jlimit (0, juce::jmax (0, count - 48), first + (w.deltaY > 0 ? -12 : 12));
    rebuild();
}


void SceneScreen::showSub (int s)
{
    sub = juce::jlimit (0, 4, s);
    firstSubRow = 0;
    rebuildSub();
    if (onSubChanged)
        onSubChanged();
}

ScreenButton& SceneScreen::addSubKey (juce::Rectangle<int> r, const juce::String& name, juce::Colour edge, std::function<void()> click, bool enabled)
{
    auto b = std::make_unique<ScreenButton> (name, edge);
    b->setBounds (r);
    b->onClick = std::move (click);
    b->setEnabled (enabled);
    b->fontSize = 14.0f;
    addAndMakeVisible (*b);
    subKeys.push_back (std::move (b));
    return *subKeys.back();
}

void SceneScreen::rebuildSub()
{
    subKeys.clear();
    for (auto* t : { &managerTab, &updateTab, &cueTab, &importTab, &safesTab })
        t->refresh();
    const bool manager = sub == 0;
    for (auto* c : std::initializer_list<juce::Component*> { &goKey, &undoKey, &storeKey, &deleteKey, &filterKey, &lockKey, &copyFilterKey, &embeddedKey, &midiKey, &actionsKey, &copyKey, &pasteKey, &nameEditor, &notesEditor, &fade })
        c->setVisible (manager);

    const juce::Colour white = juce::Colours::white;
    if (sub == 1)
    {
        addSubKey ({ 478, 72, 160, 30 }, "All", kYellow, [this]
        {
            updateRows.clear();
            for (int i = 0; i < ctx.show.getNumScenes(); ++i)
                updateRows.push_back (i);
            repaint();
        });
        addSubKey ({ 680, 72, 160, 30 }, "None", kYellow, [this] { updateRows.clear(); repaint(); });
        addSubKey ({ 578, 150, 160, 30 }, "Range", kYellow, {}, false);
        auto& manualKey = addSubKey ({ 478, 280, 160, 34 }, "Manual", white, {});
        manualKey.pale = true;
        manualKey.isOn = [this] { return updateManual; };
        manualKey.litFill = kTabOn;
        manualKey.onClick = [this, &manualKey]
        {
            updateManual = true;
            juce::PopupMenu menu;
            for (int i = 0; i < state::kNumSections; ++i)
            {
                const auto section = state::sectionAt (i);
                menu.addItem (i + 1, state::sectionName (section), true, (updateSections & section) != 0);
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&manualKey), [safe = juce::Component::SafePointer<SceneScreen> (this)] (int r)
            {
                if (safe != nullptr && r > 0)
                    safe->updateSections ^= (juce::uint32) state::sectionAt (r - 1);
            });
        };
        auto& tracking = addSubKey ({ 680, 280, 160, 34 }, "Auto Tracking", white, {}, false);
        tracking.pale = true;
        addSubKey ({ 478, 366, 160, 32 }, "Absolute", white, {}, false);
        addSubKey ({ 680, 366, 160, 32 }, "Relative", white, {}, false);
        addSubKey ({ 478, 452, 160, 32 }, "Apply", white, [this]
        {
            if (! updateRows.empty())
                popups::confirm (*this, "UPDATE", "Store the console into " + juce::String ((int) updateRows.size()) + " scenes?", "UPDATE", [this] { applyUpdate(); }, true);
        });
        addSubKey ({ 680, 452, 160, 32 }, "Cancel", white, [this] { updateRows.clear(); repaint(); });
    }
    else if (sub == 2)
    {
        auto& add = addSubKey ({ 100, 56, 210, 28 }, "Add New Cue List", kTeal, {}, false);
        add.dropdown = true;
        auto& all = addSubKey ({ 640, 56, 200, 28 }, "All Scenes", kTeal, {}, false);
        all.dropdown = true;
        const char* const top[] = { "Rename", "Duplicate", "Delete" };
        for (int i = 0; i < 3; ++i)
            addSubKey ({ 40 + i * 118, 90, 108, 28 }, top[i], white, {}, false);
        const char* const bottom[] = { "Indent", "Spacer", "Remove" };
        for (int i = 0; i < 3; ++i)
            addSubKey ({ 40 + i * 118, 454, 108, 28 }, bottom[i], white, {}, false);
        addSubKey ({ 40, 488, 165, 28 }, "Renumber", white, {}, false);
        addSubKey ({ 215, 488, 165, 28 }, "Surface Recall ID", white, {}, false);
        const char* const middle[] = { "Select All", "Deselect All", "Select Range" };
        for (int i = 0; i < 3; ++i)
            addSubKey ({ 412, 224 + i * 40, 100, 30 }, middle[i], kYellow, {}, false);
        for (int i = 0; i < 2; ++i)
            addSubKey ({ 412, 400 + i * 40, 100, 30 }, i == 0 ? "Auto Number" : "Auto Indent", white, {}, false).pale = true;
    }
    else if (sub == 3)
    {
        addSubKey ({ 452, 252, 118, 32 }, "Select Show", kYellow, {}, false);
        const char* const keys[] = { "Select All", "Deselect All", "Select Range" };
        for (int i = 0; i < 3; ++i)
            addSubKey ({ 452, 342 + i * 40, 118, 32 }, keys[i], kYellow, {}, false);
    }
    else if (sub == 4)
    {
        addSubKey ({ 26, 56, 100, 28 }, "Safe All", white, [this] { setAllSafe (true); });
        addSubKey ({ 134, 56, 104, 28 }, "Safe None", white, [this] { setAllSafe (false); });
        addSubKey ({ 680, 56, 84, 28 }, "Apply", white, {}, false); // a change takes effect at once here
        addSubKey ({ 772, 56, 84, 28 }, "Cancel", white, {}, false);
    }
    repaint();
}

void SceneScreen::toggleUpdateScene (int row)
{
    if (row >= ctx.show.getNumScenes())
        return;
    const auto it = std::find (updateRows.begin(), updateRows.end(), row);
    if (it != updateRows.end())
        updateRows.erase (it);
    else
        updateRows.push_back (row);
    repaint();
}

void SceneScreen::applyUpdate()
{
    state::Scope scope;
    updateRows.erase (std::remove_if (updateRows.begin(), updateRows.end(), [this] (int r) { return isLocked (r); }), updateRows.end());
    scope.sections = updateManual ? updateSections : (juce::uint32) state::AllSections;
    for (int row : updateRows)
        ctx.show.storeScenePartial (row, scope);
    updateRows.clear();
    repaint();
}

namespace
{
    /** Global Safes' columns: the sections each stands for (0: nothing behind it yet). Mute,
        Level and Pan are one section in Sidecar's scenes, and move together. */
    struct SafeColumnDef
    {
        const char* name;
        juce::uint32 sections;
    };
    const SafeColumnDef kSafeColumns[] = {
        { "Processing", state::Preamp | state::Gate | state::Eq | state::Dynamics | state::Inserts },
        { "Mute", state::Fader }, { "Level", state::Fader }, { "Main On", state::Sends }, { "Pan", state::Fader },
        { "Routing", state::Routing }, { "Direct Out", 0 }, { "IEM / Wedge", 0 }, { "DCA & Mute Grp", state::Groups }, { "Name & Colour", state::Name },
    };
    const char* const kSafeCategories[] = { "Inputs", "Mixes", "FX Units", "UFX Units", "FX Sends", "FX Returns", "DCAs", "Mute Groups", "SoftKeys", "MIDI", "Controllers", "Dyn8", "Other" };
    constexpr int kSafeRowStrips = 12;
}

const MixerEngine::StripList* SceneScreen::safeList (int category) const
{
    return category == 0 ? &ctx.engine.getChannels() : category == 1 ? &ctx.engine.getBuses() : category == 5 ? &ctx.engine.getAux() : nullptr;
}

bool SceneScreen::isSafeColumn (int column) const
{
    const auto bits = kSafeColumns[column].sections;
    return bits != 0 && (ctx.show.getGlobalSafe().sections & bits) == bits;
}

void SceneScreen::toggleSafeColumn (int column)
{
    auto scope = ctx.show.getGlobalSafe();
    const auto bits = kSafeColumns[column].sections;
    if (bits == 0)
        return;
    scope.sections = isSafeColumn (column) ? scope.sections & ~bits : scope.sections | bits;
    ctx.show.setGlobalSafe (scope);
    repaint();
}

bool SceneScreen::isSafeRow (int category, int row) const
{
    const auto* list = safeList (category);
    if (list == nullptr || row * kSafeRowStrips >= (int) list->size())
        return false;
    for (int i = row * kSafeRowStrips; i < (row + 1) * kSafeRowStrips && i < (int) list->size(); ++i)
        if (! (*list)[(size_t) i]->params.recallSafe.load())
            return false;
    return true;
}

void SceneScreen::toggleSafeRow (int category, int row)
{
    const auto* list = safeList (category);
    if (list == nullptr)
        return;
    const bool on = ! isSafeRow (category, row);
    for (int i = row * kSafeRowStrips; i < (row + 1) * kSafeRowStrips && i < (int) list->size(); ++i)
        (*list)[(size_t) i]->params.recallSafe = on;
    repaint();
}

void SceneScreen::setAllSafe (bool on)
{
    auto scope = ctx.show.getGlobalSafe();
    scope.sections = on ? (juce::uint32) state::AllSections : 0u;
    ctx.show.setGlobalSafe (scope);
    for (int category : { 0, 1, 5 })
        for (const auto& s : *safeList (category))
            s->params.recallSafe = on;
    repaint();
}

void SceneScreen::paintRows (juce::Graphics& g, juce::Rectangle<float> first, float pitch, int count, int firstRow, bool marks) const
{
    const int scenes = ctx.show.getNumScenes();
    for (int i = 0; i < count; ++i)
    {
        const int row = firstRow + i;
        const auto r = first.translated (0.0f, (float) i * pitch);
        const bool chosen = sub == 1 && std::find (updateRows.begin(), updateRows.end(), row) != updateRows.end();
        g.setColour (chosen ? kTabOnText : kNavy);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (kTeal.withAlpha (0.9f));
        g.drawRoundedRectangle (r, 3.0f, 1.3f);
        text (g, r.reduced (8.0f, 0.0f), rowName (row), 15.5f, juce::Colours::white);
        if (row < scenes && marks)
        {
            float x = r.getRight() - 54.0f;
            const float y = r.getCentreY();
            juce::Path funnel;
            funnel.addTriangle (x, y - 7.0f, x + 13.0f, y - 7.0f, x + 6.5f, y + 2.0f);
            funnel.addRectangle (x + 5.0f, y, 3.0f, 8.0f);
            g.setColour (kCyan);
            g.fillPath (funnel);
            x += 18.0f;
            g.setColour (juce::Colours::white);
            g.fillRoundedRectangle (x, y - 2.0f, 10.0f, 9.0f, 1.5f);
            g.drawEllipse (x + 2.0f, y - 8.0f, 6.0f, 9.0f, 1.3f);
            juce::Path tick;
            x += 14.0f;
            tick.startNewSubPath (x, y);
            tick.lineTo (x + 4.0f, y + 5.0f);
            tick.lineTo (x + 13.0f, y - 7.0f);
            g.setColour (juce::Colour (0xffb6e23c));
            g.strokePath (tick, juce::PathStrokeType (2.4f));
        }
    }
}

void SceneScreen::paintSub (juce::Graphics& g)
{
    mainBox (g, { 878.0f, 4.0f, 120.0f, 50.0f }, ctx.engine, desk);
    panel (g, { 14.0f, 46.0f, 856.0f, 484.0f }, kFrameLight, kFrame, 7.0f);
    const juce::Rectangle<float> field (20.0f, 52.0f, 844.0f, 472.0f);
    if (sub == 1)
    {
        panel (g, { 22.0f, 54.0f, 390.0f, 468.0f }, kPanel, kPanelDeep);
        panel (g, { 418.0f, 54.0f, 444.0f, 468.0f }, kPanel, kPanelDeep);
        {
            const juce::Rectangle<float> r (245.0f, 58.0f, 160.0f, 26.0f);
            g.setColour (kTabOnText);
            g.fillRoundedRectangle (r, 6.0f);
            text (g, r, "All Scenes", 14.0f, juce::Colours::white.withAlpha (0.7f), juce::Justification::centred);
        }
        paintRows (g, { 30.0f, 92.0f, 372.0f, 30.0f }, 33.0f, 13, firstSubRow, false);
        const std::tuple<float, float, const char*> blocks[] = { { 58.0f, 192.0f, "Select Scenes" }, { 258.0f, 80.0f, "Scope" }, { 346.0f, 72.0f, "Mode" }, { 426.0f, 90.0f, "Update" } };
        for (auto [y, h, name] : blocks)
        {
            const juce::Rectangle<float> b (426.0f, y, 428.0f, h);
            g.setColour (kPanelDeep.brighter (0.1f));
            g.fillRoundedRectangle (b, 5.0f);
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawRoundedRectangle (b, 5.0f, 1.2f);
            side (g, b.withWidth (30.0f), name, false);
        }
        text (g, { 456.0f, 196.0f, 398.0f, 30.0f }, "Select Scenes to Update", 19.0f, juce::Colours::white, juce::Justification::centred);
    }
    else if (sub == 2)
    {
        panel (g, field, kPanel, kPanelDeep);
        const juce::Rectangle<float> drop (36.0f, 124.0f, 356.0f, 322.0f);
        g.setColour (juce::Colour (0xff121622));
        g.fillRoundedRectangle (drop, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawRoundedRectangle (drop, 4.0f, 1.4f);
        juce::Path box;
        const auto c = drop.getCentre().translated (0.0f, -40.0f);
        box.addRoundedRectangle (c.x - 42.0f, c.y - 42.0f, 84.0f, 84.0f, 8.0f);
        juce::PathStrokeType stroke (5.0f);
        const float dashes[] = { 11.0f, 7.0f };
        juce::Path dashed;
        stroke.createDashedStroke (dashed, box, dashes, 2);
        g.setColour (juce::Colour (0xffd0d4c0));
        g.fillPath (dashed);
        g.fillRect (c.x - 3.0f, c.y - 20.0f, 6.0f, 40.0f);
        g.fillRect (c.x - 20.0f, c.y - 3.0f, 40.0f, 6.0f);
        text (g, drop.withTrimmedTop (220.0f).withHeight (40.0f), "Drop Scenes Here", 20.0f, juce::Colours::white.withAlpha (0.85f), juce::Justification::centred);
        paintRows (g, { 520.0f, 92.0f, 336.0f, 30.0f }, 33.0f, 13, firstSubRow, true);
    }
    else if (sub == 3)
    {
        panel (g, field, kPanel, kPanelDeep);
        text (g, { 30.0f, 56.0f, 410.0f, 28.0f }, "Local Scenes", 19.0f, juce::Colours::white, juce::Justification::centred);
        text (g, { 580.0f, 56.0f, 280.0f, 28.0f }, "No Show Selected", 19.0f, juce::Colours::white, juce::Justification::centred);
        paintRows (g, { 30.0f, 90.0f, 412.0f, 30.0f }, 33.0f, 13, firstSubRow, true);
        const juce::Rectangle<float> other (580.0f, 90.0f, 280.0f, 428.0f);
        g.setColour (juce::Colour (0xff121622));
        g.fillRoundedRectangle (other, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawRoundedRectangle (other, 4.0f, 1.4f);
        text (g, other, "No Show Selected", 19.0f, juce::Colours::white.withAlpha (0.8f), juce::Justification::centred);
    }
    else if (sub == 4)
    {
        panel (g, field, juce::Colour (0xff6f97ae), kPanelDeep);
        text (g, { 248.0f, 56.0f, 430.0f, 28.0f }, "Global Scene Safes", 19.0f, juce::Colours::white, juce::Justification::centred);
        const juce::Colour lit (0xff2f7bff), off (0xffa9bcc8);
        for (int i = 0; i < 13; ++i)
        {
            const juce::Rectangle<float> r (26.0f, 90.0f + (float) i * 33.5f, 150.0f, 30.0f);
            g.setColour (i == safeCategory ? kTabOn : juce::Colour (0xff3a4a5a));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawRoundedRectangle (r, 5.0f, 1.2f);
            const bool available = safeList (i) != nullptr;
            text (g, r.withTrimmedRight (30.0f), kSafeCategories[i], 14.0f, juce::Colours::white.withAlpha (available ? 1.0f : 0.5f), juce::Justification::centredRight);
            bool any = false;
            if (auto* list = safeList (i))
                for (const auto& s : *list)
                    any = any || s->params.recallSafe.load();
            g.setColour (any ? lit : off);
            g.fillEllipse (r.getRight() - 25.0f, r.getY() + 5.0f, 20.0f, 20.0f);
        }

        const float x0 = 368.0f, pitchX = 45.0f, top = 268.0f, pitchY = 34.0f;
        const auto dotAt = [&] (float cx, float cy, bool on, float alpha)
        {
            g.setColour ((on ? lit : off).withMultipliedAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (26.0f, 26.0f).withCentre ({ cx, cy }));
        };
        for (int c = 0; c < 10; ++c)
        {
            const float cx = x0 + (float) (c + 1) * pitchX;
            const juce::Rectangle<float> band (cx - 22.0f, 90.0f, 44.0f, 432.0f);
            g.setColour (juce::Colour (c % 2 == 0 ? 0xff4a5a68 : 0xff232c38).withAlpha (0.8f));
            g.fillRect (band);
            juce::Graphics::ScopedSaveState state (g);
            const juce::Rectangle<float> head (cx - 12.0f, 96.0f, 24.0f, 152.0f);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, head.getCentreX(), head.getCentreY()));
            text (g, juce::Rectangle<float> (head.getHeight(), head.getWidth()).withCentre (head.getCentre()), kSafeColumns[c].name, 14.0f,
                  juce::Colours::white.withAlpha (kSafeColumns[c].sections != 0 ? 1.0f : 0.5f), juce::Justification::centredLeft);
        }
        text (g, { x0 - 22.0f, top - 42.0f, 44.0f, 22.0f }, "ALL", 13.0f, juce::Colours::white, juce::Justification::centred, true);
        dotAt (x0, top, false, 0.5f);
        for (int c = 0; c < 10; ++c)
            dotAt (x0 + (float) (c + 1) * pitchX, top, isSafeColumn (c), kSafeColumns[c].sections != 0 ? 1.0f : 0.5f);
        const auto* list = safeList (safeCategory);
        const int count = list != nullptr ? ((int) list->size() + kSafeRowStrips - 1) / kSafeRowStrips : 0;
        const juce::String kindName = kSafeCategories[safeCategory];
        for (int i = 0; i < kSafeRowsShown && firstSubRow + i < count; ++i)
        {
            const int row = firstSubRow + i;
            const float y = top + (float) (i + 1) * pitchY;
            const juce::Rectangle<float> label (192.0f, y - 15.0f, x0 - 192.0f, 30.0f);
            g.setColour (juce::Colour (i % 2 == 0 ? 0xff121622 : 0xff46525e));
            g.fillRoundedRectangle (label, 8.0f);
            const int a = row * kSafeRowStrips + 1, b = juce::jmin ((row + 1) * kSafeRowStrips, (int) list->size());
            text (g, label.withTrimmedRight (22.0f), kindName + " " + juce::String (a) + "-" + juce::String (b), 14.0f, juce::Colours::white, juce::Justification::centredRight);
            const bool rowOn = isSafeRow (safeCategory, row);
            dotAt (x0, y, rowOn, 1.0f);
            for (int c = 0; c < 10; ++c)
                dotAt (x0 + (float) (c + 1) * pitchX, y, rowOn || isSafeColumn (c), kSafeColumns[c].sections != 0 ? 1.0f : 0.5f);
        }
    }
}
//==============================================================================
IoScreen::IoScreen (UiContext& c, Desk& d) : ctx (c), desk (d), titleTab ("I/O", true), faders (c, d)
{
    addAndMakeVisible (titleTab);
    static const char* const names[] = { "Outputs", "Inputs", "Tie Lines", "Virtual SoundCheck" };
    for (int i = 0; i < NumTabs; ++i)
    {
        auto t = std::make_unique<ScreenTab> (names[i]);
        t->isOn = [this, i] { return tab == i; };
        t->onClick = [this, i] { showTab (i); };
        addAndMakeVisible (*t);
        tabs.push_back (std::move (t));
    }
    addAndMakeVisible (sends);
    sends.onTouched = [this] (Touchable& t)
    {
        if (touchedNow != nullptr && touchedNow.getComponent() != &t)
            touchedNow->setTouched (false);
        touchedNow = &t;
        t.setTouched (true);
        if (onTouchTurn)
            onTouchTurn (t.getBinding());
    };
    sends.onLayerTapped = [this] (int l)
    {
        softLayer = juce::jlimit (0, 2, l);
        setStrip (strip);
    };
    addAndMakeVisible (faders);
    setStrip (nullptr);
    rebuild();
    startTimerHz (10);
}

IoScreen::~IoScreen() = default;

void IoScreen::setStrip (Strip* s)
{
    strip = s;
    SoftWidget::Block a, b;
    sendBlocks (ctx.engine, strip, softLayer * 6, a, b);
    sends.set (a, b, softLayer, 3, false);
}

void IoScreen::showTab (int t)
{
    tab = juce::jlimit (0, (int) NumTabs - 1, t);
    groupIndex = 0;
    firstRow = firstColumn = 0;
    rebuild();
}

void IoScreen::setSide (int s)
{
    side = juce::jlimit (0, 1, s);
    groupIndex = 0;
    firstRow = firstColumn = 0;
    rebuild();
}

void IoScreen::setGroup (int g)
{
    groupIndex = g;
    firstColumn = 0;
    rebuild();
}

void IoScreen::setPatchMode (bool on)
{
    patchMode = on;
    repaint();
}

std::vector<routing::Group> IoScreen::sideGroups() const
{
    using G = routing::Group;
    // The profile's port groups, in the WING's places (livedaw_engine::ports::DLIVE_PORT_GROUPS).
    if (tab == Outputs)
        return side == 0 ? std::vector<G> { G::LocalOut, G::Aes50A, G::Aes50B, G::Aes50C, G::StConnect, G::Dante } : std::vector<G> { G::AuxOut };
    return side == 0 ? std::vector<G> { G::LocalIn, G::Aes50A, G::Aes50B, G::Aes50C, G::StConnect, G::Dante } : std::vector<G> { G::AuxIn, G::UsbPlayer };
}

ScreenButton& IoScreen::addKey (juce::Rectangle<int> r, const juce::String& name, juce::Colour edge, std::function<void()> click, bool pale)
{
    auto b = std::make_unique<ScreenButton> (name, edge);
    b->setBounds (r);
    b->onClick = std::move (click);
    b->pale = pale;
    b->fontSize = 13.0f;
    addAndMakeVisible (*b);
    items.push_back (std::move (b));
    return *items.back();
}

void IoScreen::rebuild()
{
    items.clear();
    for (auto& t : tabs)
        t->refresh();

    const auto lit = kTabOn;
    if (tab == Outputs || tab == Inputs)
    {
        const char* const socketNames[] = { "MixRack", "Surface" };
        for (int s = 0; s < 2; ++s)
        {
            auto& k = addKey ({ 26 + s * 88, 54, 84, 34 }, juce::String (socketNames[s]) + (tab == Outputs ? "\nOutput Sockets" : "\nInput Sockets"), juce::Colours::white, [this, s] { setSide (s); });
            k.fontSize = 11.5f;
            k.isOn = [this, s] { return side == s; };
            k.litFill = lit;
            k.refresh();
        }
        const auto groups = sideGroups();
        for (int g = 0; g < (int) groups.size(); ++g)
        {
            const juce::String name = g == 0 ? "Local" : g == 1 && groups[(size_t) g] == routing::Group::UsbPlayer ? "USB" : juce::String (routing::info (groups[(size_t) g]).name).replace ("I/O PORT", "I/O Port");
            auto& k = addKey ({ 210 + g * 80, 54, 76, 34 }, name, juce::Colours::white, [this, g] { setGroup (g); });
            k.isOn = [this, g] { return groupIndex == g; };
            k.litFill = lit;
            k.fontSize = 12.5f;
            k.refresh();
        }
        // The rows' kinds down the left: only the first has a page here so far.
        const juce::StringArray kinds = tab == Outputs ? juce::StringArray { "Mix Out", "Ip Direct Out", "Rack FX", "UltraFX", "PAFL" } : juce::StringArray { "Input", "Mix Ext In" };
        juce::ignoreUnused (kinds);
    }
    if (tab != SoundCheck)
    {
        addKey ({ 780, 56, 34, 32 }, "+", juce::Colours::white, [this] { zoom = juce::jmin (1.5f, zoom + 0.15f); repaint(); }).fontSize = 20.0f;
        addKey ({ 820, 56, 34, 32 }, "-", juce::Colours::white, [this] { zoom = juce::jmax (0.7f, zoom - 0.15f); repaint(); }).fontSize = 20.0f;
    }
    if (tab == SoundCheck)
    {
        auto& inactive = addKey ({ 150, 466, 190, 30 }, "Inactive", kTeal, {});
        inactive.isOn = [] { return true; };
        inactive.litFill = kTabOnText;
        inactive.refresh();
        inactive.setEnabled (false);
        addKey ({ 350, 466, 190, 30 }, "Record Send", juce::Colours::white, {}, true).setEnabled (false);
        addKey ({ 550, 466, 190, 30 }, "Virtual SoundCheck", juce::Colours::white, {}, true).setEnabled (false);
        auto& port = addKey ({ 110, 56, 200, 30 }, "Surface I/O Port 4", kCyan, {});
        port.dropdown = true;
        port.setEnabled (false);
    }
    for (int s = 0; s < 2; ++s)
    {
        auto& p = addKey ({ s == 0 ? 26 : 780, 500, 76, 26 }, "Patch", kTeal, [this] { setPatchMode (! patchMode); });
        p.isOn = [this] { return patchMode; };
        p.litFill = kTabOnText;
        p.refresh();
        p.setEnabled (tab == Outputs || tab == Inputs);
    }
    rebuildGrid();
    repaint();
}

void IoScreen::rebuildGrid()
{
    rows.clear();
    columns.clear();
    heading.clear();
    if (tab == Outputs || tab == Inputs)
    {
        const auto groups = sideGroups();
        const auto group = groups[(size_t) juce::jlimit (0, (int) groups.size() - 1, groupIndex)];
        const auto info = routing::info (group);
        for (int i = 0; i < info.size; ++i)
        {
            const int at = tab == Outputs ? routing::outputAt (group, i) : routing::sourceAt (group, i);
            if (at >= 0)
                columns.push_back (at);
        }
        const juce::String where (side == 0 ? "MixRack " : "Surface ");
        const juce::String what = groupIndex == 0 ? "Local" : juce::String (info.name).replace ("I/O PORT", "I/O Port");
        heading = where + what + (tab == Outputs ? " Outputs" : " Inputs");
    }
    else if (tab == TieLines)
    {
        for (int i = 0; i < 32; ++i)
            columns.push_back (i);
        heading = "MixRack Local Outputs";
    }
    else
    {
        for (int i = 0; i < 32; ++i)
            columns.push_back (i);
        heading = "Virtual SoundCheck";
    }

    if (tab == Outputs)
    {
        for (auto kind : { StripKind::Bus, StripKind::Main, StripKind::Matrix })
            for (auto& s : ctx.engine.getStrips (kind))
            {
                const bool stereo = s->params.stereo.load() || kind == StripKind::Main;
                const auto colour = kind == StripKind::Main ? kYellow : kind == StripKind::Matrix ? kKnobViolet : kCyan;
                for (int sideOf = 0; sideOf < (stereo ? 2 : 1); ++sideOf)
                    rows.push_back ({ s.get(), sideOf, nameOf (*s), stereo ? (sideOf == 0 ? "L" : "R") : juce::String(), stereo, colour });
            }
    }
    else if (tab == Inputs || tab == SoundCheck)
    {
        int n = 0;
        for (auto& s : ctx.engine.getChannels())
        {
            ++n;
            const auto name = s->getName();
            rows.push_back ({ s.get(), 0, juce::String (n) + (name.isNotEmpty() ? " " + name : juce::String()), juce::String(), s->params.stereo.load(), s->getColour() });
        }
    }
    else
    {
        for (int i = 0; i < 64; ++i)
            rows.push_back ({ nullptr, 0, juce::String (i + 1), juce::String(), false, kFrame });
    }
}

bool IoScreen::isOn (int r, int c) const
{
    if (! juce::isPositiveAndBelow (r, (int) rows.size()) || ! juce::isPositiveAndBelow (c, (int) columns.size()) || rows[(size_t) r].strip == nullptr)
        return false;
    const auto& row = rows[(size_t) r];
    auto& p = row.strip->params;
    const int point = columns[(size_t) c];
    if (tab == Outputs)
        return (row.side == 0 ? p.outputLeft : p.outputRight).load() == point;
    if (tab == Inputs)
    {
        const int left = p.inputLeft.load(), right = p.inputRight.load();
        return left == point || (p.stereo.load() && left >= 0 && (right == point || (right < 0 && point == left + 1)));
    }
    return tab == SoundCheck && c == r;
}

void IoScreen::tapPoint (int r, int c)
{
    if (! patchMode || ! juce::isPositiveAndBelow (r, (int) rows.size()) || ! juce::isPositiveAndBelow (c, (int) columns.size()) || rows[(size_t) r].strip == nullptr)
        return;
    const auto& row = rows[(size_t) r];
    auto& p = row.strip->params;
    const int point = columns[(size_t) c];
    if (tab == Outputs)
    {
        auto& out = row.side == 0 ? p.outputLeft : p.outputRight;
        if (out.load() == point)
            out = -1;
        else
        {
            ctx.engine.clearOutput (point); // one signal per socket
            out = point;
        }
    }
    else if (tab == Inputs)
    {
        if (p.inputLeft.load() == point)
        {
            p.inputLeft = -1;
            p.inputRight = -1;
        }
        else
        {
            p.inputLeft = point;
            p.inputRight = -1; // a stereo channel takes the next socket
        }
    }
    repaint();
}

void IoScreen::refresh()
{
    for (auto& k : items)
        k->refresh();
    sends.refresh();
    repaint();
}

void IoScreen::resized()
{
    titleTab.setBounds (14, 10, 112, 36);
    int x = 136;
    for (auto& t : tabs)
    {
        const int w = t->getButtonText().length() > 12 ? 170 : 86;
        t->setBounds (x, 8, w, 38);
        x += w + 6;
    }
    sends.setBounds (878, 62, 120, 468);
    faders.setBounds (0, 535, kTouchW, kTouchH - 535);
}

juce::Rectangle<float> IoScreen::gridArea() const noexcept
{
    const float left = tab == TieLines ? 200.0f : 70.0f;
    return { left + (float) rowLabelWidth(), tab == SoundCheck ? 132.0f : 128.0f, 862.0f - (left + (float) rowLabelWidth()), tab == SoundCheck ? 330.0f : 368.0f };
}

void IoScreen::paint (juce::Graphics& g)
{
    g.fillAll (kBack);
    {
        const juce::Rectangle<float> box (878.0f, 4.0f, 120.0f, 50.0f);
        g.setColour (juce::Colour (0xffc9cc4a));
        g.fillRoundedRectangle (box, 4.0f);
        const auto pill = box.reduced (18.0f, 13.0f);
        g.setColour (juce::Colour (0xffc9cc4a).darker (0.35f));
        g.fillRoundedRectangle (pill, 5.0f);
        const int bus = desk.getMixOnFaders();
        auto* mix = bus >= 0 ? stripOf (ctx.engine, { Slot::Bus, bus }) : nullptr;
        text (g, pill, mix != nullptr ? nameOf (*mix) : juce::String ("Main"), 15.0f, juce::Colours::white, juce::Justification::centred, true);
    }
    panel (g, { 14.0f, 46.0f, 856.0f, 484.0f }, kFrameLight, kFrame, 7.0f);
    panel (g, { 20.0f, 52.0f, 844.0f, 472.0f }, juce::Colour (0xff6f8798), juce::Colour (0xff5a7385));

    if (tab == Outputs || tab == Inputs)
    {
        const juce::StringArray kinds = tab == Outputs ? juce::StringArray { "Mix Out", "Ip Direct Out", "Rack FX", "UltraFX", "PAFL" } : juce::StringArray { "Input", "Mix Ext In" };
        const float h = tab == Outputs ? 70.0f : 84.0f;
        for (int k = 0; k < kinds.size(); ++k)
            dl::side (g, { 26.0f, 112.0f + (float) k * (h + 4.0f), 30.0f, h }, kinds[k], k == 0, kinds[k].length() > 8 ? 12.0f : 15.0f);
    }
    else if (tab == TieLines)
    {
        const char* const groups[] = { "MixRack\nInput Sockets", "Local", "DX 1/2", "DX 3/4", "I/O Port 1", "I/O Port 2", "I/O Port 3" };
        for (int i = 0; i < 7; ++i)
        {
            const juce::Rectangle<float> r (40.0f, 100.0f + (float) i * 50.0f, 140.0f, 44.0f);
            g.setColour (i <= 1 ? kTabOn : kNavy);
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawRoundedRectangle (r, 5.0f, 1.2f);
            g.setColour (juce::Colours::white.withAlpha (i <= 1 ? 1.0f : 0.5f));
            g.setFont (type::font (13.0f));
            g.drawFittedText (groups[i], r.toNearestInt(), juce::Justification::centred, 2);
        }
    }

    const auto area = gridArea();
    const float cell = cellSize();
    text (g, area.withHeight (16.0f).translated (0.0f, -36.0f), heading, 14.5f, juce::Colours::white, juce::Justification::centred);

    const int visibleRows = juce::jmin ((int) rows.size() - firstRow, (int) (area.getHeight() / cell));
    const int visibleColumns = juce::jmin ((int) columns.size() - firstColumn, (int) (area.getWidth() / cell));
    {
        juce::Graphics::ScopedSaveState state (g);
        for (int c = 0; c < visibleColumns; ++c)
        {
            const juce::Rectangle<float> head (area.getX() + (float) c * cell, area.getY() - 18.0f, cell - 1.0f, 17.0f);
            g.setColour (juce::Colour (0xff46596a));
            g.fillRect (head);
            text (g, head, juce::String (firstColumn + c + 1), cell < 16.0f ? 8.5f : 10.0f, juce::Colours::white.withAlpha (0.7f), juce::Justification::centred);
        }
        const float labelX = area.getX() - (float) rowLabelWidth();
        for (int r = 0; r < visibleRows; ++r)
        {
            const auto& row = rows[(size_t) (firstRow + r)];
            const float y = area.getY() + (float) r * cell;
            const juce::Rectangle<float> label (labelX, y, (float) rowLabelWidth() - 2.0f, cell - 1.0f);
            g.setColour (juce::Colour (0xff2d3a48));
            g.fillRect (label);
            g.setColour (row.edge);
            g.drawRect (label, 1.3f);
            auto textArea = label.reduced (4.0f, 0.0f);
            if (row.stereo)
                text (g, textArea.removeFromLeft (14.0f).withTrimmedBottom (5.0f), "St", 9.0f, juce::Colours::white);
            text (g, textArea, row.name, cell < 16.0f ? 10.5f : 12.5f, juce::Colours::white);
            if (row.right.isNotEmpty())
                text (g, textArea, row.right, 12.0f, juce::Colours::white, juce::Justification::centredRight);
            else if (tab == Inputs || tab == SoundCheck)
            {
                juce::Path tick;
                const float tx = label.getRight() - 15.0f, ty = label.getCentreY();
                tick.startNewSubPath (tx, ty);
                tick.lineTo (tx + 3.5f, ty + 4.0f);
                tick.lineTo (tx + 11.0f, ty - 5.0f);
                g.setColour (juce::Colour (0xffb6e23c));
                g.strokePath (tick, juce::PathStrokeType (2.0f));
            }
            for (int c = 0; c < visibleColumns; ++c)
            {
                const juce::Rectangle<float> dot (area.getX() + (float) c * cell, y, cell - 1.0f, cell - 1.0f);
                const bool on = isOn (firstRow + r, firstColumn + c);
                g.setColour (on ? juce::Colour (0xffc8dce6) : juce::Colour (0xff232c38));
                g.fillRect (dot);
                g.setColour (juce::Colour (0xffb4c8d4).withAlpha (0.55f));
                g.drawRect (dot.expanded (0.5f), 1.0f);
            }
        }
    }

    // The footer: Navigate mode, or Patch mode with a tap patching.
    if (tab == Outputs || tab == Inputs || tab == TieLines || tab == SoundCheck)
    {
        const juce::Rectangle<float> bar (22.0f, 498.0f, 840.0f, 28.0f);
        g.setColour (juce::Colour (0xff121622));
        g.fillRect (bar);
        text (g, bar, patchMode ? "Patch mode" : "Navigate mode", 16.0f, juce::Colours::white, juce::Justification::centred);
    }
    if (tab == SoundCheck)
    {
        text (g, { 30.0f, 58.0f, 80.0f, 26.0f }, "Select Port:", 13.0f, juce::Colours::white);
        text (g, { 330.0f, 58.0f, 50.0f, 26.0f }, "Start", 13.0f, juce::Colours::white);
        text (g, { 480.0f, 58.0f, 50.0f, 26.0f }, "End", 13.0f, juce::Colours::white);
        for (auto [x, s] : { std::pair<float, const char*> { 375.0f, "1" }, { 520.0f, "128" } })
        {
            const juce::Rectangle<float> r (x, 58.0f, 80.0f, 26.0f);
            g.setColour (kNavy);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (kCyan.withAlpha (0.5f));
            g.drawRoundedRectangle (r, 4.0f, 1.4f);
            text (g, r, s, 15.0f, juce::Colours::white.withAlpha (0.6f), juce::Justification::centred);
        }
    }
}

void IoScreen::mouseDown (const juce::MouseEvent&)
{
    dragRow = firstRow;
    dragColumn = firstColumn;
}

void IoScreen::mouseDrag (const juce::MouseEvent& e)
{
    if (patchMode)
        return;
    const auto area = gridArea();
    const int rowsFit = (int) (area.getHeight() / cellSize()), columnsFit = (int) (area.getWidth() / cellSize());
    firstRow = juce::jlimit (0, juce::jmax (0, (int) rows.size() - rowsFit), dragRow - juce::roundToInt ((float) e.getDistanceFromDragStartY() / cellSize()));
    firstColumn = juce::jlimit (0, juce::jmax (0, (int) columns.size() - columnsFit), dragColumn - juce::roundToInt ((float) e.getDistanceFromDragStartX() / cellSize()));
    repaint();
}

void IoScreen::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown())
        return;
    const auto area = gridArea();
    if (area.contains (e.getPosition().toFloat()))
        tapPoint (firstRow + (int) (((float) e.y - area.getY()) / cellSize()), firstColumn + (int) (((float) e.x - area.getX()) / cellSize()));
}

//==============================================================================
SurfaceScreen::SurfaceScreen (UiContext& c, Desk& d) : ctx (c), desk (d), titleTab ("Surface", true), faders (c, d)
{
    addAndMakeVisible (titleTab);
    static const char* const names[] = { "Strip Assign", "SoftKeys", "Rotaries" };
    for (int i = 0; i < NumTabs; ++i)
    {
        auto t = std::make_unique<ScreenTab> (names[i]);
        t->isOn = [this, i] { return tab == i; };
        t->onClick = [this, i] { showTab (i); };
        addAndMakeVisible (*t);
        tabs.push_back (std::move (t));
    }
    addAndMakeVisible (sends);
    sends.onTouched = [this] (Touchable& t)
    {
        if (touchedNow != nullptr && touchedNow.getComponent() != &t)
            touchedNow->setTouched (false);
        touchedNow = &t;
        t.setTouched (true);
        if (onTouchTurn)
            onTouchTurn (t.getBinding());
    };
    sends.onLayerTapped = [this] (int l)
    {
        softLayer = juce::jlimit (0, 2, l);
        setStrip (strip);
    };
    addAndMakeVisible (faders);
    assignCell = [this] (int b, int l, int s) { showStripMenu (ctx, desk.getSetup(), b, l, s, this, [this] { repaint(); }); };
    setStrip (nullptr);
    rebuild();
    startTimerHz (5);
}

SurfaceScreen::~SurfaceScreen() = default;

void SurfaceScreen::setStrip (Strip* s)
{
    strip = s;
    SoftWidget::Block a, b;
    sendBlocks (ctx.engine, strip, softLayer * 6, a, b);
    sends.set (a, b, softLayer, 3, false);
}

void SurfaceScreen::showTab (int t)
{
    tab = juce::jlimit (0, (int) NumTabs - 1, t);
    rebuild();
}

void SurfaceScreen::setBank (int b)
{
    bank = juce::jlimit (0, desk.getSetup().getNumBanks() - 1, b);
    rebuild();
}

ScreenButton& SurfaceScreen::addKey (juce::Rectangle<int> r, const juce::String& name, juce::Colour edge, std::function<void()> click)
{
    auto b = std::make_unique<ScreenButton> (name, edge);
    b->setBounds (r);
    b->onClick = std::move (click);
    b->fontSize = 14.0f;
    addAndMakeVisible (*b);
    items.push_back (std::move (b));
    return *items.back();
}

void SurfaceScreen::rebuild()
{
    items.clear();
    for (auto& t : tabs)
        t->refresh();
    auto& setup = desk.getSetup();
    if (tab == StripAssign)
    {
        if (setup.getNumBanks() > 1)
            for (int b = 0; b < setup.getNumBanks(); ++b)
            {
                auto& k = addKey ({ 26 + b * 86, 56, 80, 30 }, "Bank " + juce::String (b + 1), juce::Colours::white, [this, b] { setBank (b); });
                k.isOn = [this, b] { return bank == b; };
                k.litFill = kTabOn;
                k.refresh();
            }
        addKey ({ 762, 56, 102, 30 }, "Reset All", kRed, [this]
        {
            popups::confirm (*this, "RESET", "Put the strips, SoftKeys and rotaries back as they came?", "RESET", [this] { desk.getSetup().reset(); rebuild(); }, true);
        });
    }
    else if (tab == SoftKeys)
    {
        for (int i = 0; i < SurfaceSetup::kSoftKeys; ++i)
        {
            const juce::String label = "Key " + juce::String (i + 1) + ":  " + softKeyName (ctx, setup.getSoftKey (i));
            auto& k = addKey ({ 26 + (i / 10) * 424, 60 + (i % 10) * 45, 414, 38 }, label, kCyan, {});
            k.onClick = [this, i, target = &k] { showSoftKeyMenu (ctx, desk.getSetup(), i, target, [this] { rebuild(); }); };
        }
    }
    else
    {
        for (int i = 0; i < SurfaceSetup::kCustomRotaries; ++i)
        {
            const juce::String label = "Custom " + juce::String (i + 1) + ":  " + customRotaryName (setup.getCustom (i));
            auto& k = addKey ({ 26, 60 + i * 60, 414, 48 }, label, kKnobGreen, {});
            k.onClick = [this, i, target = &k] { showCustomRotaryMenu (desk.getSetup(), i, target, [this] { rebuild(); }); };
        }
    }
    repaint();
}

juce::Rectangle<int> SurfaceScreen::cellBounds (int layer, int stripIndex) const noexcept
{
    return { 58 + stripIndex * 67, 96 + layer * 71, 64, 68 };
}

juce::String SurfaceScreen::getCellText (int layer, int stripIndex) const
{
    return slotName (ctx, desk.getSetup().getShownSlot (bank, layer, stripIndex, (int) ctx.engine.getMains().size()));
}

void SurfaceScreen::refresh()
{
    for (auto& t : tabs)
        t->refresh();
    for (auto& k : items)
        k->refresh();
    sends.refresh();
    if (tab == StripAssign)
        repaint();
}

void SurfaceScreen::resized()
{
    titleTab.setBounds (14, 10, 112, 36);
    int x = 136;
    for (auto& t : tabs)
    {
        t->setBounds (x, 8, 110, 38);
        x += 116;
    }
    sends.setBounds (878, 62, 120, 468);
    faders.setBounds (0, 535, kTouchW, kTouchH - 535);
}

void SurfaceScreen::paint (juce::Graphics& g)
{
    g.fillAll (kBack);
    mainBox (g, { 878.0f, 4.0f, 120.0f, 50.0f }, ctx.engine, desk);
    panel (g, { 14.0f, 46.0f, 856.0f, 484.0f }, kFrameLight, kFrame, 7.0f);
    panel (g, { 20.0f, 52.0f, 844.0f, 472.0f }, kPanel, kPanelDeep);
    if (tab == StripAssign)
    {
        auto& setup = desk.getSetup();
        if (setup.getNumBanks() == 1)
            text (g, { 30.0f, 58.0f, 700.0f, 26.0f }, "Touch a strip to change what sits on it (kept in Sidecar's settings for this surface).", 14.0f, juce::Colours::white);
        for (int l = 0; l < setup.getNumLayers(); ++l)
        {
            text (g, cellBounds (l, 0).toFloat().withX (28.0f).withWidth (26.0f), juce::String::charToString ((juce::juce_wchar) ('A' + l)), 19.0f, juce::Colours::white, juce::Justification::centred, true);
            for (int i = 0; i < setup.getNumStrips(); ++i)
            {
                const auto r = cellBounds (l, i).toFloat();
                const auto slot = setup.getShownSlot (bank, l, i, (int) ctx.engine.getMains().size());
                auto* st = stripOf (ctx.engine, slot);
                juce::Colour fill (0xff2d3a48);
                if (st != nullptr)
                    fill = st->getColour().getPerceivedBrightness() < 0.12f ? juce::Colour (0xff38506a) : st->getColour();
                else if (slot.kind == Slot::Dca && juce::isPositiveAndBelow (slot.index, ctx.engine.getNumDcas()))
                    fill = consoleColour (ctx.engine.getDca (slot.index).colour.load());
                g.setColour (fill);
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.drawRoundedRectangle (r, 4.0f, 1.2f);
                const auto ink = fill.getPerceivedBrightness() > 0.6f ? juce::Colour (0xff101418) : juce::Colours::white;
                auto top = r.reduced (2.0f, 4.0f);
                text (g, top.removeFromTop (top.getHeight() * 0.48f), slotName (ctx, slot), 12.5f, ink, juce::Justification::centred, true);
                text (g, top, st != nullptr ? nameOf (*st).substring (0, 8) : juce::String(), 11.5f, ink.withAlpha (0.8f), juce::Justification::centred);
            }
        }
    }
    else if (tab == SoftKeys)
        text (g, { 30.0f, 508.0f, 800.0f, 18.0f }, "Touch a key's line to choose its function, or hold Setup and press the key itself.", 13.0f, juce::Colours::white, juce::Justification::centred);
    else
        text (g, { 30.0f, 508.0f, 800.0f, 18.0f }, "Touch a line to choose what the rotary key sets, or hold Setup and press the key.", 13.0f, juce::Colours::white, juce::Justification::centred);
}

void SurfaceScreen::mouseDown (const juce::MouseEvent& e)
{
    if (tab != StripAssign)
        return;
    auto& setup = desk.getSetup();
    for (int l = 0; l < setup.getNumLayers(); ++l)
        for (int i = 0; i < setup.getNumStrips(); ++i)
            if (cellBounds (l, i).contains (e.getPosition()))
            {
                if (assignCell)
                    assignCell (bank, l, i);
                return;
            }
}

//==============================================================================
StatusScreen::StatusScreen (UiContext& c) : ctx (c)
{
    startTimerHz (4);
}

StatusScreen::~StatusScreen() = default;

juce::StringArray StatusScreen::getLines() const
{
    auto& engine = ctx.engine;
    auto& show = ctx.show;
    juce::StringArray l;
    l.add ("Show: " + (show.getName().isNotEmpty() ? show.getName() : juce::String ("Unnamed")));
    const int current = show.getCurrentSceneIndex();
    const auto scene = current >= 0 && current < show.getNumScenes() ? show.getScene (current) : std::nullopt;
    l.add ("Last Scene: " + (scene.has_value() ? juce::String (current + 1) + ") " + scene->name : juce::String ("None")));
    l.add ("Scenes: " + juce::String (show.getNumScenes()));
    auto* selected = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    l.add ("Selected: " + (selected != nullptr ? nameOf (*selected) + " (" + selected->getLabel() + ")" : juce::String ("None")));
    int groups = 0;
    for (int i = 0; i < engine.getNumMuteGroups(); ++i)
        groups += engine.muteGroups.contains (i) ? 1 : 0;
    l.add ("Mute Groups engaged: " + juce::String (groups));
    l.add ("PAFL: " + juce::String (engine.anySoloActive.load() ? "Active" : "Off"));

    auto* device = ctx.deviceManager.getCurrentAudioDevice();
    l.add ("Audio: " + (device != nullptr ? device->getName() : juce::String ("No device")));
    l.add ("Sample Rate: " + juce::String (juce::roundToInt (engine.getSampleRate())) + " Hz");
    if (device != nullptr)
        l.add ("Buffer: " + juce::String (device->getCurrentBufferSizeSamples()) + " samples");
    l.add ("Audio CPU: " + juce::String (juce::roundToInt (ctx.deviceManager.getCpuUsage() * 100.0)) + " %");
    l.add ("Inputs: " + juce::String ((int) engine.getChannels().size()));
    l.add ("Mixes: " + juce::String ((int) engine.getBuses().size()));
    l.add ("FX Returns: " + juce::String ((int) engine.getAux().size()));
    l.add ("DCAs: " + juce::String (engine.getNumDcas()));
    l.add ("Clock: " + juce::Time::getCurrentTime().formatted ("%H:%M:%S"));
    return l;
}

void StatusScreen::timerCallback()
{
    if (! isShowing())
        return;
    const auto now = getLines().joinIntoString ("\n");
    if (now != shown)
    {
        shown = now;
        repaint();
    }
}

void StatusScreen::paint (juce::Graphics& g)
{
    g.fillAll (kBack);
    panel (g, { 14.0f, 46.0f, 856.0f, 484.0f }, kFrameLight, kFrame, 7.0f);
    panel (g, { 20.0f, 52.0f, 844.0f, 472.0f }, kPanel, kPanelDeep);
    text (g, { 14.0f, 8.0f, 400.0f, 36.0f }, "System Status", 26.0f, juce::Colours::white);
    const auto lines = getLines();
    for (int i = 0; i < lines.size(); ++i)
    {
        const float x = i < 8 ? 40.0f : 450.0f;
        const float y = 70.0f + (float) (i < 8 ? i : i - 8) * 44.0f;
        const auto name = lines[i].upToFirstOccurrenceOf (":", false, false);
        const auto value = lines[i].fromFirstOccurrenceOf (":", false, false).trim();
        text (g, { x, y, 160.0f, 22.0f }, name, 15.0f, juce::Colours::white.withAlpha (0.8f));
        text (g, { x, y + 20.0f, 370.0f, 22.0f }, value, 17.0f, juce::Colours::white, juce::Justification::centredLeft, true);
    }
}

//==============================================================================
BottomBand::BottomBand (UiContext& c) : curve (c), bands (c)
{
    curve.handles = false;
    addAndMakeVisible (curve);
    addAndMakeVisible (bands);
    setVisible (false);
    startTimerHz (15);
}

BottomBand::~BottomBand() = default;

void BottomBand::setStrip (Strip* s)
{
    curve.setStrip (s);
    bands.setStrip (s);
}

void BottomBand::setView (int v)
{
    view = juce::jlimit (0, 2, v);
    setVisible (view != 0);
    bands.setVisible (view == 1);
    curve.setVisible (view == 2);
}

void BottomBand::resized()
{
    curve.setBounds (getLocalBounds().reduced (6, 2));
    bands.setBounds (getLocalBounds().reduced (4, 0));
}

void BottomBand::timerCallback()
{
    if (! isShowing())
        return;
    if (view == 1)
        bands.update();
    else if (view == 2)
        curve.update();
}

//==============================================================================
void HelpPanel::show (const juce::String& key)
{
    title = "Help: " + key.upToFirstOccurrenceOf ("/", false, false);
    body = screenHelp (key);
    setVisible (true);
    toFront (false);
    repaint();
}

void HelpPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colour (0xf20e1a30));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (kCyan);
    g.drawRoundedRectangle (r, 8.0f, 2.0f);
    auto inner = r.reduced (20.0f, 14.0f);
    text (g, inner.removeFromTop (34.0f), title, 24.0f, juce::Colours::white, juce::Justification::centredLeft, true);
    g.setColour (juce::Colours::white);
    g.setFont (type::font (17.0f));
    g.drawFittedText (body, inner.removeFromTop (inner.getHeight() - 26.0f).toNearestInt(), juce::Justification::topLeft, 14, 1.0f);
    text (g, inner, "Touch to close", 13.0f, juce::Colours::white.withAlpha (0.6f), juce::Justification::centredRight);
}

void HelpPanel::mouseDown (const juce::MouseEvent&)
{
    setVisible (false);
}

juce::String screenHelp (const juce::String& key)
{
    static const std::pair<const char*, const char*> notes[] = {
        { "Status", "No screen key is lit, so this page shows how the console is: the show and scene, the audio device and what the engine has laid out. Press a screen key to work." },
        { "Processing", "The selected strip's processing. The tabs open each section; touch a value and turn the data rotary, or drag it. The panel's PEQ rotaries follow the selected strip." },
        { "Processing/Bank", "Pick a strip here without going to its fader." },
        { "Processing/Overview", "Each section of the strip at a glance; touch one to open it. A green section is in." },
        { "Processing/Preamp", "The socket the strip listens to, its gain and 48V, the digital trim and polarity. Gain and 48V belong to the socket, so they follow it, not the strip. With ABCD on, the channel plays one of four sockets: A to D choose, the keys under them patch." },
        { "Processing/Filter", "The high-pass and low-pass filters. Each has its own In." },
        { "Processing/Gate", "The gate or expander. The curve shows input to output; the soft widget's meters show the signal and how much is being cut. Change the type at the top." },
        { "Processing/Insert", "Sends the strip through an FX unit or a Dyn8 engine at this point. Assign an FX unit on the FX page first; pick a Dyn8 here (an engine another insert uses is dimmed) to see its multiband and dynamic EQ pages and its key source." },
        { "Processing/Ufx", "The RackUltra engines: pick one, its device, its channel strip (pre and post PEQ and dynamics, Width), its key source and the Global Key every engine shares. The note keys play the devices that follow MIDI notes." },
        { "Processing/Peq", "Four bands with their own response; the outer bands can be shelf, bell or filter. Width on the panel makes the Freq rotaries set width. View changes what's along the bottom." },
        { "Processing/Geq", "The graphic EQ on this mix. The GEQ key puts it on the faders instead, the mix's own master on the last strip." },
        { "Processing/Comp", "The compressor. Choose its type at the top. The curve follows the settings; Gain makes up what it takes away." },
        { "Processing/Delay", "Delay in milliseconds, set with the In key." },
        { "Processing/ExtIn", "An external input for this mix: a socket, another mix or PAFL. Choose the kind, touch the box and turn the data rotary, then Apply. It joins the mix before its inserts. SigGen and Talkback are set on Sidecar's own pages." },
        { "Processing/Routing", "A channel's Direct Out level; the source point and the Follow keys are for every channel. On a mix: where the channels' signal leaves them for it, and Follow Main Pan for a stereo mix. Sends and assignments are on the Routing page." },
        { "Processing/FxUnit", "This return's FX unit. Open it on the FX page." },
        { "Meters", "The levels of every input, FX, mix and an RTA for the selected strip. A strip's coloured key mutes it, its meter selects it. The mouse wheel moves on through the inputs." },
        { "Fx", "The FX units and what's patched to them." },
        { "Io", "Patch sockets. Patch mode must be on before a touch changes anything; in Navigate mode dragging moves the grid." },
        { "Scenes", "Store and recall scenes. Go recalls the highlighted scene, the green one is next. Lock stops a scene being changed (until the show is closed). Copy and Paste take a scene's content and recall filter, not its name." },
        { "Scenes/Update", "Store the console into several scenes at once: pick them, then Apply." },
        { "Scenes/Cue", "Drawn from the console; Sidecar has no cue lists yet." },
        { "Scenes/Import", "Drawn from the console; use Sidecar's show import instead for now." },
        { "Scenes/Safes", "Make kinds of settings, or whole strips, safe from every scene recall. The ALL column's rows make a whole strip safe." },
        { "Ganging", "Groups of strips that move together. For now this opens Sidecar's own setup page." },
        { "MixRack", "How the console is laid out: mixes and stereos (Apply resets the mixes only), names and colours, AMM. Network has the MIDI channel of MIDI over TCP and who is connected. Audio's Source Selector: touch a source to put it in the selector or take it out, touch a member to play it (the others fade out) and turn its on-level; Off plays none. Dimmed parts have nothing behind them yet." },
        { "Surface", "What this surface does: which strips sit on each layer, the SoftKeys and the four custom rotaries. All of it stays in Sidecar's settings, one set for each surface." },
        { "Util", "Shows and libraries." },
        { "Library", "Sidecar's preset and scene libraries." },
        { "Routing", "Where the selected strip and the outputs are patched." },
        { "Setup", "Sidecar's setup. With this key lit, a SoftKey or a rotary key 1-4 opens its assignment instead of doing its job." },
        { "Listen", "Headphone and PAFL listening." },
    };
    const auto find = [] (const juce::String& k) -> juce::String
    {
        for (auto [name, note] : notes)
            if (k == name)
                return note;
        return {};
    };
    auto note = find (key);
    if (note.isEmpty())
        note = find (key.upToFirstOccurrenceOf ("/", false, false));
    if (note.isEmpty())
        note = "Touch a value on the screen and turn the data rotary to change it. The keys beside the screen choose what it shows.";
    return note;
}

} // namespace livedaw::ui::dl
