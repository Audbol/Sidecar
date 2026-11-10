#include "ui/ConsoleTape.h"

#include "BinaryData.h"

namespace livedaw::ui
{

juce::Font ConsoleTape::hand (float height)
{
    static const juce::Typeface::Ptr typeface
        = juce::Typeface::createSystemTypefaceFor (BinaryData::PatrickHandRegular_ttf, BinaryData::PatrickHandRegular_ttfSize);
    return typeface != nullptr ? juce::Font (juce::FontOptions (typeface).withHeight (height)) : juce::Font (juce::FontOptions (height));
}

ConsoleTape::ConsoleTape (int seed)
{
    juce::Random random (seed * 7919 + 13);
    tilt = (0.7f + 1.1f * random.nextFloat()) * ((seed + random.nextInt (2)) % 2 == 0 ? 1.0f : -1.0f);
    textTilt = tilt * 0.6f + (random.nextFloat() - 0.5f) * 0.8f;
    for (auto& j : jitter)
        j = random.nextFloat();

    editor.setMultiLine (false);
    editor.setJustification (juce::Justification::centred);
    editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xfffbf8ee));
    editor.setColour (juce::TextEditor::textColourId, juce::Colour (0xff1a1a1a));
    editor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xff6b7a99));
    editor.setIndents (2, 0);
    editor.setSelectAllWhenFocused (true);
    editor.onReturnKey = [this] { endEdit (true); };
    editor.onEscapeKey = [this] { endEdit (false); };
    editor.onFocusLost = [this] { endEdit (true); };
    addChildComponent (editor);
    setMouseCursor (juce::MouseCursor::IBeamCursor);
}

void ConsoleTape::refresh()
{
    if (isEditing() || ! getText)
        return;
    if (const auto now = getText(); now != shown)
    {
        shown = now;
        repaint();
    }
}

void ConsoleTape::beginEdit()
{
    if (isEditing())
        return;
    editor.setFont (hand ((float) getHeight() * 0.62f));
    editor.setText (getText ? getText() : shown, false);
    editor.setVisible (true);
    editor.grabKeyboardFocus();
    editor.selectAll();
}

void ConsoleTape::endEdit (bool keep)
{
    if (! isEditing())
        return;
    const auto text = editor.getText().trim();
    editor.setVisible (false);
    if (keep && setText && text != shown)
        setText (text);
    refresh();
    repaint();
}

void ConsoleTape::resized()
{
    editor.setBounds (getLocalBounds().reduced (6, 5));
}

void ConsoleTape::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && ! isEditing())
        beginEdit();
}

/** The tape's shape: a long strip tilted about its centre, its two ends not square
    (each corner is a little off, and a notch or two is torn along the end). */
juce::Path ConsoleTape::outline() const
{
    const auto b = getLocalBounds().toFloat().reduced (4.0f, 5.0f);
    const float tear = b.getHeight() * 0.09f, skew = b.getHeight() * 0.16f;
    juce::Path p;
    const float x0 = b.getX(), x1 = b.getRight(), y0 = b.getY(), y1 = b.getBottom(), h = b.getHeight();
    p.startNewSubPath (x0 + skew * jitter[0], y0);
    p.lineTo (x1 - skew * jitter[1], y0 + tear * (jitter[2] - 0.5f));
    p.lineTo (x1 - skew * 0.3f * jitter[3] + tear, y0 + h * 0.34f);
    p.lineTo (x1 - skew * jitter[3], y0 + h * 0.6f);
    p.lineTo (x1 - skew * jitter[4] * 0.8f, y1);
    p.lineTo (x0 + skew * jitter[5], y1 - tear * (jitter[6] - 0.5f));
    p.lineTo (x0 + skew * 0.2f * jitter[7], y0 + h * 0.56f);
    p.lineTo (x0 + skew * (0.4f + 0.6f * jitter[6]), y0 + h * 0.3f);
    p.closeSubPath();
    return p.createPathWithRoundedCorners (0.8f);
}

void ConsoleTape::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const auto turn = juce::AffineTransform::rotation (juce::degreesToRadians (tilt), c.x, c.y);
    auto tape = outline();
    tape.applyTransform (turn);

    juce::Path shadow (tape);
    shadow.applyTransform (juce::AffineTransform::translation (1.2f, 1.8f));
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillPath (shadow);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffaf8f0), c.x, c.y - 20.0f, juce::Colour (0xffe9e5d6), c.x, c.y + 24.0f, false));
    g.fillPath (tape);
    g.setColour (juce::Colour (0x30000000));
    g.strokePath (tape, juce::PathStrokeType (0.8f));
    // The creases masking tape has.
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    auto a = juce::Point<float> (c.x - (float) getWidth() * 0.45f, c.y - 9.0f), b = juce::Point<float> (c.x + (float) getWidth() * 0.4f, c.y - 10.0f);
    a.applyTransform (turn);
    b.applyTransform (turn);
    g.drawLine (a.x, a.y, b.x, b.y, 0.7f);

    if (isEditing())
        return;
    const auto name = shown.isNotEmpty() ? shown : juce::String();
    auto font = hand ((float) getHeight() * 0.64f);
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, name, 0.0f, 0.0f);
    const float room = (float) getWidth() - 34.0f, wide = glyphs.getBoundingBox (0, -1, true).getWidth();
    if (wide > room && wide > 0.0f)
        font = hand ((float) getHeight() * 0.64f * room / wide);
    g.saveState();
    g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (textTilt), c.x, c.y));
    g.setFont (font);
    g.setColour (juce::Colour (0xff1c1d24));
    g.drawText (name, getLocalBounds().toFloat().translated (0.0f, -1.0f), juce::Justification::centred, false);
    g.restoreState();
}

} // namespace livedaw::ui
