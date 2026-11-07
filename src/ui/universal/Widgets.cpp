#include "ui/universal/Widgets.h"
#include "ui/HardwareLook.h"

#include "dsp/DspUtils.h"
#include "ui/Knobs.h"

namespace livedaw::ui::universal
{

//==============================================================================
float deviceTagWidth (const juce::String& device)
{
    return device.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (type::font (type::kCaption - 2.0f, true), device) + 16.0f;
}

float drawDeviceTag (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& device, const Theme& theme, bool atLeft)
{
    if (device.isEmpty())
        return 0.0f;
    const auto font = type::font (type::kCaption - 2.0f, true);
    const float w = juce::GlyphArrangement::getStringWidth (font, device) + 10.0f;
    const float h = juce::jmin (area.getHeight(), 16.0f);
    const auto tag = (atLeft ? area.removeFromLeft (w) : area.removeFromRight (w)).withSizeKeepingCentre (w, h);
    g.setColour (theme.textDim);
    g.drawRoundedRectangle (tag.reduced (0.5f), 3.0f, 1.0f);
    g.setFont (font);
    g.drawText (device, tag, juce::Justification::centred);
    return w + 6.0f;
}

//==============================================================================
FaderControl::FaderControl (const Theme& t) : theme (t)
{
    setViewportIgnoreDragFlag (true);
    setTitle ("Fader");
    setEnabled (false);
}

void FaderControl::bind (std::atomic<float>* db)
{
    value = db;
    setEnabled (db != nullptr);
    refresh();
}

void FaderControl::setPositionFromUser (double newPosition)
{
    position = juce::jlimit (0.0, 1.0, newPosition);
    if (value != nullptr)
        value->store (fader::posToDb ((float) position));
    repaint();
}

void FaderControl::refresh()
{
    const double p = value != nullptr ? (double) fader::dbToPos (value->load()) : 0.0;
    if (std::abs (p - position) > 1.0e-4)
    {
        position = p;
        repaint();
    }
}

juce::Rectangle<float> FaderControl::track() const noexcept
{
    // The cap's centre travels between these (the cap stays inside the control).
    return getLocalBounds().toFloat().reduced (0.0f, kTrackInset);
}

void FaderControl::paint (juce::Graphics& g)
{
    const auto t = track();
    const float groove = t.getRight() - 18.0f; // the groove's centre
    const auto yAt = [&t] (float pos) { return t.getBottom() - t.getHeight() * pos; };

    static constexpr float marks[] = { 10.0f, 5.0f, 0.0f, -5.0f, -10.0f, -20.0f, -30.0f, -40.0f, -60.0f, fader::kMinDb };
    if (look != nullptr)
    {
        // A console's fader: its silkscreen scale (the X32's: 10 to -60, signed,
        // with -50), the slot, the moulded cap.
        static constexpr float consoleMarks[] = { 10.0f, 5.0f, 0.0f, -5.0f, -10.0f, -20.0f, -30.0f, -40.0f, -50.0f, -60.0f, fader::kMinDb };
        for (float db : consoleMarks)
        {
            if (db > fader::maxDb() + 0.01f)
                continue;
            const float y = yAt (fader::dbToPos (db));
            const auto label = db <= fader::kMinDb ? juce::String::fromUTF8 ("-\xe2\x88\x9e") : juce::String ((int) db);
            hw::silk (g, juce::Rectangle<float> (-2.0f, y - 7.0f, groove - 12.0f, 14.0f), label, *look, 9.0f, juce::Justification::centredRight,
                      db != 0.0f);
            g.setColour (look->silkDim.withAlpha (0.6f));
            g.fillRect (groove - 13.0f, y - 0.5f, 6.0f, 1.0f);
        }
        hw::faderSlot (g, groove, t.getY(), t.getBottom(), *look);
        if (isEnabled())
            hw::faderCap (g, groove, yAt ((float) position), look->cap == hw::Look::Cap::Metal ? 26.0f : 28.0f, *look); // the X32's: narrow and tall
        return;
    }
    g.setFont (type::font (type::kCaption));
    for (float db : marks)
    {
        if (db > fader::maxDb() + 0.01f)
            continue;
        const float y = yAt (fader::dbToPos (db));
        g.setColour (db == 0.0f ? theme.text : theme.textDim);
        const auto label = db <= fader::kMinDb ? juce::String::fromUTF8 ("-\xe2\x88\x9e") : (db > 0.0f ? "+" : "") + juce::String ((int) db);
        g.drawText (label, juce::Rectangle<float> (0.0f, y - 7.0f, groove - 20.0f, 14.0f), juce::Justification::centredRight);
        g.setColour (theme.outline);
        g.fillRect (groove - 12.0f, y - 0.5f, 5.0f, 1.0f);
    }

    g.setColour (theme.well);
    g.fillRoundedRectangle (groove - 3.0f, t.getY(), 6.0f, t.getHeight(), 3.0f);
    if (isEnabled())
    {
        // The level from the bottom, so a glance tells where a fader is.
        g.setColour (theme.accent.withAlpha (0.55f));
        g.fillRoundedRectangle (groove - 3.0f, yAt ((float) position), 6.0f, t.getBottom() - yAt ((float) position), 3.0f);
    }

    const float y = yAt ((float) position);
    const juce::Rectangle<float> cap (groove - 14.0f, y - 12.0f, 28.0f, 24.0f);
    g.setColour (isEnabled() ? theme.raised.brighter (0.25f) : theme.raised);
    g.fillRoundedRectangle (cap, 4.0f);
    g.setColour (theme.outline.brighter (0.4f));
    g.drawRoundedRectangle (cap, 4.0f, 1.0f);
    g.setColour (isEnabled() ? theme.text : theme.textDim);
    g.fillRect (cap.getX() + 5.0f, y - 1.0f, cap.getWidth() - 10.0f, 2.0f);
}

void FaderControl::mouseDown (const juce::MouseEvent&)
{
    dragStart = position;
}

void FaderControl::mouseDrag (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;
    // As far as the touch travels (a quarter of that with Shift, for fine moves).
    const double travel = (double) -e.getDistanceFromDragStartY() / juce::jmax (1.0, (double) track().getHeight());
    setPositionFromUser (dragStart + travel * (e.mods.isShiftDown() ? 0.25 : 1.0));
}

void FaderControl::mouseDoubleClick (const juce::MouseEvent&)
{
    if (isEnabled())
        setPositionFromUser (fader::dbToPos (0.0f));
}

void FaderControl::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (isEnabled() && wheel.deltaY != 0.0f)
        setPositionFromUser (position + (wheel.deltaY > 0.0f ? 0.01 : -0.01));
}

//==============================================================================
MeterControl::MeterControl (const Theme& t) : theme (t)
{
    setInterceptsMouseClicks (false, false);
}

void MeterControl::setChannels (int numChannels)
{
    channels = juce::jlimit (1, 2, numChannels);
    positions[1] = 0.0f;
    repaint();
}

void MeterControl::refresh()
{
    bool moved = false;
    for (int ch = 0; ch < channels; ++ch)
    {
        const float p = level ? fader::dbToPos (dsp::gainToDb (level (ch))) : 0.0f;
        // Repaint for a move of a pixel or more.
        if (std::abs (p - positions[(size_t) ch]) * (float) getHeight() >= 1.0f || (p == 0.0f) != (positions[(size_t) ch] == 0.0f))
        {
            positions[(size_t) ch] = p;
            moved = true;
        }
    }
    if (moved)
        repaint();
}

void MeterControl::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float clipH = 5.0f;
    const float gap = 2.0f;
    const float barW = (area.getWidth() - gap * (float) (channels - 1)) / (float) channels;
    // The bars span a fader's travel; the clip light sits above them.
    const auto bars = area.reduced (0.0f, kTrackInset);
    const float lowTop = fader::dbToPos (-18.0f), midTop = fader::dbToPos (-6.0f), highTop = fader::dbToPos (0.0f);

    for (int ch = 0; ch < channels; ++ch)
    {
        const float x = area.getX() + (barW + gap) * (float) ch;
        const auto bar = juce::Rectangle<float> (x, bars.getY(), barW, bars.getHeight());
        g.setColour (theme.well);
        g.fillRoundedRectangle (bar, 2.0f);

        const float p = positions[(size_t) ch];
        const auto fill = [&] (float from, float to, juce::Colour c)
        {
            if (p <= from)
                return;
            const float top = bar.getBottom() - bar.getHeight() * juce::jmin (p, to);
            const float bottom = bar.getBottom() - bar.getHeight() * from;
            g.setColour (c);
            g.fillRect (bar.getX(), top, bar.getWidth(), bottom - top);
        };
        fill (0.0f, lowTop, theme.meterLow);
        fill (lowTop, midTop, theme.meterMid);
        fill (midTop, highTop, theme.meterHigh);
        fill (highTop, 1.0f, theme.meterClip);

        g.setColour (p >= highTop ? theme.meterClip : theme.well);
        g.fillRoundedRectangle (x, bars.getY() - clipH - gap * 2.0f, barW, clipH, 1.5f);
    }
}

//==============================================================================
PanControl::PanControl (const Theme& t) : theme (t)
{
    setViewportIgnoreDragFlag (true);
    setTitle ("Pan");
    setEnabled (false);
}

void PanControl::bind (std::atomic<float>* pan)
{
    value = pan;
    setEnabled (pan != nullptr);
    refresh();
}

void PanControl::setPanFromUser (float newPan)
{
    shown = juce::jlimit (-100.0f, 100.0f, newPan);
    if (value != nullptr)
        value->store (shown);
    repaint();
}

void PanControl::refresh()
{
    const float p = value != nullptr ? value->load() : 0.0f;
    if (std::abs (p - shown) > 0.05f)
    {
        shown = p;
        repaint();
    }
}

void PanControl::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (2.0f);
    const auto bar = area.removeFromBottom (10.0f);
    g.setColour (theme.well);
    g.fillRoundedRectangle (bar, 3.0f);
    const float centre = bar.getCentreX();
    const float x = centre + bar.getWidth() * 0.5f * shown / 100.0f;
    if (isEnabled())
    {
        g.setColour (theme.accent);
        g.fillRect (juce::jmin (centre, x), bar.getY(), juce::jmax (2.0f, std::abs (x - centre)), bar.getHeight());
    }
    g.setColour (theme.textDim);
    g.fillRect (centre - 0.5f, bar.getY() - 2.0f, 1.0f, bar.getHeight() + 4.0f);

    const int rounded = juce::roundToInt (shown);
    const auto text = rounded == 0 ? juce::String ("C") : (rounded < 0 ? "L" : "R") + juce::String (std::abs (rounded));
    g.setColour (isEnabled() ? theme.text : theme.textDim);
    g.setFont (type::font (type::kCaption, true));
    g.drawText (text, area, juce::Justification::centred);
}

void PanControl::mouseDown (const juce::MouseEvent&)
{
    dragStart = shown;
}

void PanControl::mouseDrag (const juce::MouseEvent& e)
{
    if (isEnabled())
        setPanFromUser (dragStart + 200.0f * (float) e.getDistanceFromDragStartX() / juce::jmax (1.0f, (float) getWidth()) * (e.mods.isShiftDown() ? 0.25f : 1.0f));
}

void PanControl::mouseDoubleClick (const juce::MouseEvent&)
{
    if (isEnabled())
        setPanFromUser (0.0f);
}

//==============================================================================
ToggleKey::ToggleKey (const juce::String& label, juce::Colour onColour, const Theme& t) : juce::Button (label), theme (t), colour (onColour)
{
    setTitle (label);
}

void ToggleKey::refresh()
{
    const bool on = isOn && isOn(), half = ! on && isHalfOn && isHalfOn();
    if (on != lit || half != halfLit)
    {
        lit = on;
        halfLit = half;
        repaint();
    }
}

void ToggleKey::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    if (look != nullptr)
    {
        auto area = getLocalBounds().toFloat();
        if (shape != Shape::Rect)
        {
            // An oval or a triangle of rubber, lit by its colour's edge.
            auto a = area.reduced (2.0f).translated (0.0f, down ? 1.0f : 0.0f);
            juce::Path p;
            if (shape == Shape::Oval)
                p.addRoundedRectangle (a, a.getHeight() * 0.5f);
            else
            {
                const auto c = a.getCentre();
                const float x0 = a.getX(), x1 = a.getRight(), y0 = a.getY(), y1 = a.getBottom();
                if (shape == Shape::Up)
                    p.addTriangle (c.x, y0, x1, y1, x0, y1);
                else if (shape == Shape::Down)
                    p.addTriangle (x0, y0, x1, y0, c.x, y1);
                else if (shape == Shape::Left)
                    p.addTriangle (x0, c.y, x1, y0, x1, y1);
                else
                    p.addTriangle (x0, y0, x1, c.y, x0, y1);
                p = p.createPathWithRoundedCorners (3.0f);
            }
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillPath (p, juce::AffineTransform::translation (0.0f, down ? 0.5f : 1.5f));
            g.setGradientFill (juce::ColourGradient (look->keyTop, a.getX(), a.getY(), look->keyBottom, a.getX(), a.getBottom(), false));
            g.fillPath (p);
            g.setColour (lit ? colour : juce::Colours::black.withAlpha (0.35f));
            g.strokePath (p, juce::PathStrokeType (1.0f));
            return;
        }
        if (textLit)
        {
            auto a = area.reduced (2.0f).translated (0.0f, down ? 1.0f : 0.0f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (a.translated (0.0f, 1.5f), 4.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2b2c), a.getX(), a.getY(), juce::Colour (0xff141515), a.getX(), a.getBottom(), false));
            g.fillRoundedRectangle (a, 4.0f);
            g.setColour (juce::Colour (0xff5a5c5e));
            g.drawRoundedRectangle (a.reduced (0.5f), 4.0f, 1.0f);
            const bool on = lit || halfLit;
            g.setFont (type::font (juce::jlimit (10.0f, 13.5f, a.getHeight() * 0.48f), true));
            if (on)
            {
                g.setColour (colour.withAlpha (0.35f));
                g.drawText (getButtonText(), a.translated (0.0f, 0.5f).expanded (0.5f), juce::Justification::centred);
            }
            g.setColour (on ? colour.brighter (0.25f) : colour.withMultipliedBrightness (0.8f));
            g.drawText (getButtonText(), a, juce::Justification::centred);
            return;
        }
        if (labelBelow)
        {
            // The name under the key, on two lines where it has a line break.
            const auto printed = belowText.isNotEmpty() ? belowText : getButtonText();
            const int lines = printed.containsChar ('\n') ? 2 : 1;
            const float h = juce::jlimit (9.0f, 12.0f, area.getHeight() * 0.3f / (float) lines);
            auto label = area.removeFromBottom ((h - 1.0f) * (float) lines + 2.0f);
            g.setColour (look->silk);
            g.setFont (type::font (h - 1.0f, true));
            g.drawFittedText (printed, label.toNearestInt(), juce::Justification::centred, lines, 0.6f);
            hw::key (g, area.reduced (2.0f, 1.0f), {}, lit || halfLit, down, colour, *look, face);
            return;
        }
        if (labelAbove)
        {
            // The name over the key, underlined, as the panel prints it.
            const auto printed = belowText.isNotEmpty() ? belowText : getButtonText();
            auto label = area.removeFromTop (13.0f);
            g.setColour (look->silk);
            const auto font = type::font (11.0f);
            g.setFont (font);
            g.drawFittedText (printed, label.toNearestInt(), juce::Justification::centred, 1, 0.7f);
            const float w = juce::jmin (label.getWidth() - 4.0f, juce::GlyphArrangement::getStringWidth (font, printed) + 2.0f);
            if (look->backlit)
                // The X32's: the name in a small outlined box.
                g.drawRoundedRectangle (juce::Rectangle<float> (w + 4.0f, label.getHeight() - 1.0f).withCentre (label.getCentre()), 2.0f, 1.0f);
            else
                g.fillRect (juce::Rectangle<float> (w, 1.0f).withCentre ({ label.getCentreX(), label.getBottom() - 0.5f }));
            hw::key (g, area.reduced (3.0f, 2.0f), {}, lit || halfLit, down, colour, *look, face, showLed);
            return;
        }
        hw::key (g, area.reduced (2.0f), getButtonText(), lit || halfLit, down, colour, *look, face, showLed);
        return;
    }
    const auto area = getLocalBounds().toFloat().reduced (1.5f);
    g.setColour (lit ? colour : (down ? theme.raised.brighter (0.2f) : theme.raised));
    g.fillRoundedRectangle (area, sizes::kRadius);
    g.setColour (halfLit ? colour : (highlighted && ! lit ? theme.outline.brighter (0.3f) : theme.outline));
    g.drawRoundedRectangle (area, sizes::kRadius, halfLit ? 2.0f : 1.0f);
    g.setColour (lit ? theme.textOnAccent : (isEnabled() ? theme.text : theme.textDim));
    g.setFont (type::font (type::kLabel, true));
    g.drawText (getButtonText(), area, juce::Justification::centred);
    drawDeviceTag (g, area.reduced (5.0f, 3.0f).removeFromTop (14.0f), device, theme);
}

//==============================================================================
DataWheel::DataWheel (int pixelsPerStep) : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox)
{
    // The range is only so it never ends; setMouseDragSensitivity is the pixels
    // for all of it, so the pixels per step are that many times the range.
    constexpr int range = 2000000;
    setRange (-(double) range / 2.0, (double) range / 2.0, 1.0);
    setRotaryParameters (0.0f, juce::MathConstants<float>::twoPi * 40.0f, false);
    setMouseDragSensitivity (range * juce::jlimit (1, 1000, pixelsPerStep));
    for (auto id : { rotarySliderFillColourId, rotarySliderOutlineColourId, thumbColourId })
        setColour (id, juce::Colours::transparentBlack);
}

void DataWheel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    // Not the slider's own: that moves a share of the range.
    const float amount = (std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : w.deltaY) * (w.isReversed ? -1.0f : 1.0f);
    const int step = amount > 0.0f ? 1 : (amount < 0.0f ? -1 : 0);
    if (step != 0)
        setValue (getValue() + step, juce::sendNotificationSync);
}

} // namespace livedaw::ui::universal
