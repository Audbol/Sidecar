#include "ui/DLiveScreens.h"

#include "engine/Models.h"
#include "engine/Show.h"
#include "ui/MeterBank.h"
#include "ui/WingPopups.h"

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

    void side (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, bool on)
    {
        g.setColour (on ? kTabOn : kTabIdle);
        g.fillRoundedRectangle (r, 5.0f);
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, r.getCentreX(), r.getCentreY()));
        text (g, juce::Rectangle<float> (r.getHeight(), r.getWidth()).withCentre (r.getCentre()), s, 15.0f, on ? kTabOnText : juce::Colours::white, juce::Justification::centred);
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
            const float on = b.in != nullptr && b.in->load() ? 1.0f : 0.25f;
            g.setColour (juce::Colour (0xffe02030).withAlpha (on));
            g.fillRect (thumb.getRight() + 3.0f, thumb.getY() + 6.0f, 5.0f, thumb.getHeight() - 12.0f);
            g.setColour (kGreen.withAlpha (on));
            g.fillRect (thumb.getX() - 8.0f, thumb.getY() + 6.0f, 5.0f, thumb.getHeight() - 12.0f);
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
MiniFaders::MiniFaders (UiContext& c) : ctx (c)
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

void MiniFaders::timerCallback()
{
    if (! isVisible())
        return;
    juce::String s;
    for (int c = 0; c < kColumns; ++c)
        if (auto* st = stripAt (c))
            s << juce::roundToInt (st->params.faderDb.load()) << st->getMuteState() << st->getColourIndex() << st->getName();
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
        const std::pair<const char*, float> marks[] = { { "+10", 0.0f }, { "0", 0.25f }, { "-10", 0.5f }, { "-20", 0.75f } };
        for (auto [t, at] : marks)
            text (g, { side, top + at * faderH - 2.0f, 24.0f, 13.0f }, t, 10.0f, juce::Colours::white, juce::Justification::centred);
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
        const float pos = fader::dbToPos (st->params.faderDb.load());
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
    : ctx (c), desk (d), gainKnob (kRed), trimKnob (kKnobYellow), hpf ("HPF", false), lpf ("LPF", true), graph (c), wideCurve (c), faders (c)
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
    addAndMakeVisible (wideCurve);
    addChildComponent (faders);
    addChildComponent (transfer);
    addAndMakeVisible (soft);
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
                     { T::InsA, "Ins A" }, { T::Peq, "PEQ" }, { T::Comp, "Comp" }, { T::InsB, "Ins B" }, { T::Delay, "Delay" } };
            break;
        case StripKind::Aux: // photographed on a C1500
            list = { { T::Bank, "Bank" }, { T::FxUnit, "FX Unit" }, { T::Peq, "PEQ" } };
            break;
        default: // Sidecar's reading of a mix's tabs
            list = { { T::Bank, "Bank" }, { T::Overview, "Overview" }, { T::InsA, "Ins A" }, { T::Peq, "PEQ" }, { T::Geq, "GEQ" }, { T::Comp, "Comp" }, { T::InsB, "Ins B" }, { T::Delay, "Delay" } };
            break;
    }
    if (! hasTab (tab))
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
        addAndMakeVisible (*b);
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
    rebuildTabs();
    bindFixed();
    buildBlocks();
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
    for (auto& [k, b] : tabs)
        b->refresh();
    buildTab();
    repaint();
}

void ProcessingScreen::toggleView()
{
    viewAlt = ! viewAlt;
    wideCurve.setVisible (! viewAlt);
    faders.setVisible (viewAlt);
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
    items.clear();
    const bool withGraph = strip != nullptr && (tab == Tab::Peq || tab == Tab::Filter);
    graph.setVisible (withGraph);
    transfer.setVisible (false);
    if (strip != nullptr)
    {
        const auto source = "Source: " + (strip->getKind() == StripKind::Channel ? "Input " + juce::String (strip->getLabel().retainCharacters ("0123456789")) : strip->getLabel());
        graph.title = "PEQ+RTA1  " + source;
        wideCurve.title = "PEQ+RTA1  " + source;
    }
    if (strip != nullptr)
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
    if (p.stereo.load())
        addBox ({ 150, 310, 150, 66 }, bindLinear ("Width", p.width, 0.0f, 100.0f, " %", 0, 100.0f), kCyan, "Stereo Width");
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
    addToggle ({ 170, 70, 90, 44 }, "In", kGreen, ins.on);
    const int fx = ins.fx.load();
    addButton ({ 280, 70, 240, 44 }, fx >= 0 ? "FX Unit " + juce::String (fx + 1) : juce::String ("Unassigned"), kCyan, [this] { if (onFxUnit) onFxUnit(); });
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
    tiles.push_back ({ "Ins A", Tab::InsA, &p.preInsert().on });
    tiles.push_back ({ "PEQ", Tab::Peq, &p.eq().on });
    if (s.getKind() != StripKind::Channel)
        tiles.push_back ({ "GEQ", Tab::Geq, nullptr });
    tiles.push_back ({ "Comp", Tab::Comp, &p.dynamics().on });
    tiles.push_back ({ "Ins B", Tab::InsB, &p.postInsert().on });
    tiles.push_back ({ "Delay", Tab::Delay, &p.input.delayOn });
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
    if (strip != nullptr)
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
    if (transfer.isVisible())
        transfer.repaint();
}

void ProcessingScreen::resized()
{
    int x = 141;
    for (auto& [t, b] : tabs)
    {
        const int w = juce::jmax (52, b->getButtonText().length() * 9 + 12);
        b->setBounds (x, 8, w, 38);
        x += w + 7;
    }
    gainKnob.setBounds (18, 56, 92, 112);
    trimKnob.setBounds (18, 176, 76, 112);
    hpf.setBounds (16, 294, 96, 118);
    lpf.setBounds (16, 416, 96, 112);
    graph.setBounds (129, 48, 713, 288);
    wideCurve.setBounds (6, 537, kTouchW - 12, 150);
    faders.setBounds (0, 535, kTouchW, kTouchH - 535);
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
    if (strip == nullptr)
        text (g, { 124.0f, 200.0f, 742.0f, 40.0f }, "Press a strip's Sel key", 22.0f, juce::Colours::white, juce::Justification::centred);
}

//==============================================================================
SceneScreen::SceneScreen (UiContext& c)
    : goKey ("Go", kTeal), undoKey ("Undo Go", kTeal), storeKey ("Store", kYellow), deleteKey ("Delete", kRed), filterKey ("Recall Filter", kCyan), lockKey ("Lock", juce::Colours::white),
      copyFilterKey ("Copy Filter To...", kCyan), embeddedKey ("Embedded Recall", kTeal), midiKey ("Custom MIDI", kTeal), actionsKey ("Actions", kTeal), copyKey ("Copy", juce::Colours::white),
      pasteKey ("Paste", juce::Colours::white), ctx (c), titleTab ("Scenes", true), managerTab ("Scene Manager"), updateTab ("Update"), cueTab ("Cue List\nEditor"), importTab ("Import"),
      safesTab ("Global Safes"), fade (kCyan), faders (c)
{
    managerTab.isOn = [] { return true; };
    managerTab.refresh();
    for (auto* t : { &titleTab, &managerTab, &updateTab, &cueTab, &importTab, &safesTab })
        addAndMakeVisible (t);
    // Only Scene Manager is here so far.
    for (auto* t : { &updateTab, &cueTab, &importTab, &safesTab })
        t->setEnabled (false);

    for (auto* k : { &goKey, &undoKey, &storeKey, &deleteKey, &filterKey, &lockKey, &copyFilterKey, &embeddedKey, &midiKey, &actionsKey, &copyKey, &pasteKey })
        addAndMakeVisible (k);
    // What Sidecar's shows can't do yet.
    for (auto* k : { &undoKey, &lockKey, &copyFilterKey, &embeddedKey, &midiKey, &actionsKey, &copyKey, &pasteKey })
        k->setEnabled (false);
    goKey.onClick = [this] { go(); };
    storeKey.onClick = [this]
    {
        if (selected >= ctx.show.getNumScenes())
        {
            store();
            return;
        }
        popups::confirm (*this, "STORE", "Overwrite scene " + juce::String (selected + 1) + " with the console as it is?", "STORE", [this] { store(); }, false);
    };
    deleteKey.onClick = [this]
    {
        if (selected < ctx.show.getNumScenes())
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

SceneScreen::~SceneScreen() = default;

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
        lastRecalled = selected;
        lastTime = juce::Time::getCurrentTime().formatted ("%H:%M:%S");
        next = juce::jmin (selected + 1, juce::jmax (0, ctx.show.getNumScenes() - 1));
        repaint();
    }
}

void SceneScreen::goNext()
{
    if (next < ctx.show.getNumScenes())
    {
        ctx.show.recallScene (next);
        lastRecalled = next;
        lastTime = juce::Time::getCurrentTime().formatted ("%H:%M:%S");
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
    if (selected < ctx.show.getNumScenes())
    {
        ctx.show.removeScene (selected);
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
    juce::String signature;
    signature << ctx.show.getNumScenes() << "|" << selected << "|" << next << "|" << firstMain;
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
    for (auto* k : { &goKey, &undoKey, &storeKey, &deleteKey })
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
        if (row < count)
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
    if (e.x < 116)
        firstLeft = juce::jlimit (0, kSlots - kLeftRows, firstLeft + step);
    else
        firstMain = juce::jlimit (0, kSlots - kMainRows, firstMain + step);
    repaint();
}

//==============================================================================
MixRackScreen::MixRackScreen (UiContext& c, Desk& d)
    : ctx (c), desk (d), config (c, Theme::dark()), titleTab ("MixRack", true), configTab ("Config"), audioTab ("Audio"), talkbackTab ("Talkback"), muteTab ("Mute Groups"), sigGenTab ("SigGen"),
      controllersTab ("Controllers"), faders (c)
{
    config.revert();
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
    addDim (at (690, 797, 795, 855), "1");
    addLabel (at (420, 882, 545, 937), "Main", 15.0f, juce::Justification::centredRight);
    addDim (at (562, 882, 797, 937), "LR");
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
    addLabel (at (1040, 852, 1400, 895), "Number of connections:", 15.0f, juce::Justification::centredLeft);
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

void MixRackScreen::buildSourceSelector()
{
    areas.push_back ({ at (480, 345, 1510, 715).toFloat(), {} });
    areas.push_back ({ at (430, 730, 1215, 1010).toFloat(), {} });
    areas.push_back ({ at (1228, 730, 1512, 1010).toFloat(), {} });
    static const char* const ranges[] = { "1-32", "33-64", "65-96", "97-128" };
    for (int i = 0; i < 4; ++i)
    {
        const float x = 512.0f + (float) i * 103.0f;
        auto& k = addKey (at (x, 348, x + 92, 396), ranges[i], kTeal, [this, i] { selectorFirst = i * 32; rebuild(); });
        k.fontSize = 14.0f;
        k.isOn = [this, i] { return selectorFirst == i * 32; };
        k.litFill = kTabOn;
    }
    addDim (at (1436, 478, 1486, 615), "Off", juce::Colours::white).pale = true;
    addLabel (at (1235, 738, 1505, 770), "Choose Source Selector", 15.0f);
    addDim (at (1240, 772, 1505, 815), "1) Unassigned").dropdown = true;
    addLabel (at (1235, 828, 1505, 860), "Set Output", 15.0f);
    addDim (at (1243, 862, 1502, 900), "Unassigned", juce::Colours::white);
    addLabel (at (1240, 918, 1360, 950), "Fade In", 15.0f);
    addDim (at (1243, 950, 1358, 1005), "0.0 s");
    addLabel (at (1385, 918, 1505, 950), "Fade Out", 15.0f);
    addDim (at (1390, 950, 1505, 1005), "0.0 s");
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
    if (sub == Sub::MixerConfig || sub == Sub::Amm || sub == Sub::NameColour || sub == Sub::SourceSelector)
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
    else if (sub == Sub::SourceSelector)
    {
        side (g, at (435, 340, 482, 522).toFloat(), "Ip", true);
        side (g, at (435, 530, 482, 712).toFloat(), "Mix", false);
        const auto& channels = ctx.engine.getChannels();
        for (int i = 0; i < 32; ++i)
        {
            const int n = selectorFirst + i;
            const float x = 500.0f + (float) (i % 16) * 57.0f, y = 405.0f + (float) (i / 16) * 150.0f;
            const auto cell = at (x, y, x + 52, y + 136).toFloat();
            auto* st = juce::isPositiveAndBelow (n, (int) channels.size()) ? channels[(size_t) n].get() : nullptr;
            const auto colour = st != nullptr ? st->getColour().interpolatedWith (kFrameLight, 0.35f) : kPanelDeep;
            g.setColour (colour);
            g.fillRoundedRectangle (cell.withHeight (cell.getHeight() * 0.42f + 6.0f), 3.0f);
            const auto lower = cell.withTrimmedTop (cell.getHeight() * 0.42f);
            g.setColour (juce::Colour (0xff6f8ea2));
            g.fillRoundedRectangle (lower, 3.0f);
            g.setColour (colour.brighter (0.3f));
            g.drawRoundedRectangle (cell, 3.0f, 1.2f);
            text (g, cell.withHeight (cell.getHeight() * 0.42f), "Ip\n" + juce::String (n + 1), 9.5f, juce::Colours::white, juce::Justification::centred);
            if (st != nullptr)
            {
                juce::Graphics::ScopedSaveState state (g);
                g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, lower.getCentreX(), lower.getCentreY()));
                text (g, juce::Rectangle<float> (lower.getHeight(), lower.getWidth()).withCentre (lower.getCentre()), nameOf (*st).substring (0, 7), 10.5f, juce::Colours::white, juce::Justification::centred);
            }
        }
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 5; ++c)
            {
                const float x = 442.0f + (float) c * 154.5f, y = 742.0f + (float) r * 68.5f;
                const auto slot = at (x, y, x + 143, y + 58).toFloat();
                g.setColour (juce::Colour (0xff7d97a9));
                g.fillRect (slot);
                g.setColour (juce::Colour (0xff35485a));
                g.fillRect (slot.withHeight (slot.getHeight() * 0.36f));
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
}

void MixRackScreen::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (sub == Sub::InputStereos)
    {
        pairFirst = juce::jlimit (0, 32, pairFirst + (w.deltaY > 0 ? -8 : 8));
        rebuild();
    }
}

} // namespace livedaw::ui::dl
