#include "engine/Strip.h"
#include "ui/universal/Widgets.h"
#include "ui/HardwareLook.h"

#include "dsp/DspUtils.h"
#include "ui/Knobs.h"

#include <algorithm>
#include <map>

namespace livedaw::ui::universal
{

//==============================================================================
float deviceTagWidth (const juce::String& device)
{
    return device.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (type::font (type::kMicro, true), device) + 10.0f;
}

juce::String devices::shown()
{
    switch (shownProfile())
    {
        case ld::kProfileWing:      return "WING";
        case ld::kProfileX32:       return "X32";
        case ld::kProfileProMix01:  return "PROMIX";
        case ld::kProfileYamaha02R: return "02R";
        case ld::kProfileSq5:       return "SQ-5";
        case ld::kProfileYamaha01V: return "01V";
        case ld::kProfileDLive:     return "DLIVE";
        case ld::kProfileDm2000:    return "DM2000";
        case ld::kProfileMidasPro2: return "PRO2";
        case ld::kProfileXenyx1202: return "Q1202";
        default:                    return {};
    }
}

juce::String devices::of (const juce::String& token, bool foreign, const juce::String& fallback)
{
    const auto console = models::consoleOf (token);
    if (! foreign && (console.isEmpty() || shownProfile() != ld::kProfileUniversal))
        return fallback;
    // The tags are short: PROMIX for PROMIX 01, DLIVE, Q1202; the WING's models name no console.
    if (console.isEmpty())
        return "WING";
    return console == "PROMIX 01" ? "PROMIX" : console == "dLive" ? "DLIVE" : console == "Q1202USB" ? "Q1202" : console;
}

float drawDeviceTag (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& device, const Theme& theme, bool atLeft)
{
    if (device.isEmpty())
        return 0.0f;
    // The console's name, set small: a word, not one more box.
    const auto font = type::font (type::kMicro, true);
    const float w = juce::GlyphArrangement::getStringWidth (font, device) + 4.0f;
    const float h = juce::jmin (area.getHeight(), 16.0f);
    const auto tag = (atLeft ? area.removeFromLeft (w) : area.removeFromRight (w)).withSizeKeepingCentre (w, h);
    g.setColour (theme.textDim);
    g.setFont (font);
    g.drawText (device, tag, juce::Justification::centred);
    return w + 6.0f;
}

//==============================================================================
namespace
{
    class ThemedLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        explicit ThemedLookAndFeel (const Theme& t) : theme (t)
        {
            setColourScheme ({ t.background, t.panel, t.raised, t.outline, t.text, t.accent, t.textOnAccent, t.accent, t.text });
            using PM = juce::PopupMenu;
            setColour (PM::backgroundColourId, t.raised);
            setColour (PM::textColourId, t.text);
            setColour (PM::headerTextColourId, t.textDim);
            setColour (PM::highlightedBackgroundColourId, t.accent);
            setColour (PM::highlightedTextColourId, t.textOnAccent);
            setColour (juce::AlertWindow::backgroundColourId, t.panel);
            setColour (juce::AlertWindow::textColourId, t.text);
            setColour (juce::AlertWindow::outlineColourId, t.outline);
            setColour (juce::ScrollBar::thumbColourId, t.lifted (t.outline, 0.3f));
            setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
            setColour (juce::TextEditor::backgroundColourId, t.well);
            setColour (juce::TextEditor::textColourId, t.text);
            setColour (juce::TextEditor::outlineColourId, t.outline);
            setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
            setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.35f));
            setColour (juce::TextButton::buttonColourId, t.raised);
            setColour (juce::TextButton::buttonOnColourId, t.accent);
            setColour (juce::TextButton::textColourOffId, t.text);
            setColour (juce::TextButton::textColourOnId, t.textOnAccent);
            setColour (juce::ComboBox::backgroundColourId, t.raised);
            setColour (juce::ComboBox::textColourId, t.text);
            setColour (juce::ComboBox::outlineColourId, t.outline);
            setColour (juce::ComboBox::arrowColourId, t.textDim);
            setColour (juce::ListBox::backgroundColourId, t.well);
            setColour (juce::ListBox::textColourId, t.text);
            setColour (juce::TooltipWindow::backgroundColourId, t.raised);
            setColour (juce::TooltipWindow::textColourId, t.text);
            setColour (juce::TooltipWindow::outlineColourId, t.outline);
        }

        juce::Font getPopupMenuFont() override { return type::font (type::kLabel); }

        void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) override
        {
            g.setColour (theme.panel);
            g.fillPath (path);
            g.setColour (theme.outline);
            g.strokePath (path, juce::PathStrokeType (1.0f));
        }

    private:
        const Theme& theme;
    };

    /** A finger's first few pixels decide what it is doing: going along the strips scrolls them
        and leaves the control alone. */
    constexpr int kTouchSlop = 8;
} // namespace

namespace
{
    /** The hairlines of the brushing, tiled over a plate. */
    const juce::Image& grain (const Theme& theme)
    {
        static std::map<const Theme*, juce::Image> images;
        auto& image = images[&theme];
        if (image.isNull())
        {
            // Fine: every pixel its own, with only a faint run along the strip.
            image = juce::Image (juce::Image::ARGB, 256, 256, true);
            juce::Image::BitmapData pixels (image, juce::Image::BitmapData::writeOnly);
            juce::Random random (0x51dec);
            std::array<float, 256> column;
            for (auto& c : column)
                c = random.nextFloat() - 0.5f;
            for (int y = 0; y < image.getHeight(); ++y)
                for (int x = 0; x < image.getWidth(); ++x)
                {
                    const float v = column[(size_t) x] * 0.35f + (random.nextFloat() - 0.5f) * 0.65f;
                    pixels.setPixelColour (x, y, v >= 0.0f ? juce::Colours::white.withAlpha (juce::jmin (1.0f, theme.hw.grainLight * v * 2.0f))
                                                           : juce::Colours::black.withAlpha (juce::jmin (1.0f, theme.hw.grainDark * -v * 2.0f)));
                }
        }
        return image;
    }
} // namespace

void drawPlate (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour top, juce::Colour bottom, const Theme& theme, bool screws)
{
    g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
    g.fillRect (r);
    g.setColour (theme.edgeLight());
    g.fillRect (r.getX(), r.getY(), r.getWidth(), 1.0f);
    g.setColour (theme.edgeDark());
    g.fillRect (r.getRight() - 1.0f, r.getY(), 1.0f, r.getHeight());
    juce::ignoreUnused (screws);
}

void drawEngraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification just, const Theme& theme, bool bold)
{
    g.setFont (type::font (type::kMicro, bold));
    g.setColour (theme.hw.inkShadow);
    g.drawText (text, r.translated (0.0f, theme.isLight ? 1.0f : -1.0f), just);
    g.setColour (theme.hw.ink);
    g.drawText (text, r, just);
}

void drawGlowText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification just, juce::Colour colour, const Theme& theme)
{
    if (! theme.isLight)
    {
        g.setColour (colour.withAlpha (0.28f));
        for (auto d : { juce::Point<float> (-1.0f, 0.0f), { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } })
            g.drawText (text, r + d, just);
    }
    g.setColour (colour);
    g.drawText (text, r, just);
}

bool isTouchInput()
{
    return juce::Desktop::getInstance().getMainMouseSource().isTouch();
}

juce::LookAndFeel& lookAndFeelFor (const Theme& theme)
{
    static ThemedLookAndFeel classic (Theme::dark());
    static std::map<const Theme*, std::unique_ptr<ThemedLookAndFeel>> looks;
    if (&theme == &Theme::dark() || (! theme.isCharcoal && ! theme.isLight && theme.look == Theme::Look::FlagshipBlack))
        return classic;
    auto& made = looks[&Theme::of (theme.look)];
    if (made == nullptr)
        made = std::make_unique<ThemedLookAndFeel> (Theme::of (theme.look));
    return *made;
}

void drawKnob (juce::Graphics& g, juce::Rectangle<float> area, float position, bool fromCentre, const Theme& theme, bool enabled)
{
    const auto& hw = theme.hw;
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float ring = size * 0.5f - 1.0f, body = ring - 6.0f;
    constexpr float sweep = 2.4f; // radians either side of the top
    const float value = juce::jlimit (0.0f, 1.0f, position);
    const float angle = -sweep + 2.0f * sweep * value;

    // The ring of ticks, those the knob has passed lit.
    constexpr int ticks = 11;
    const float from = fromCentre ? 0.5f : 0.0f;
    for (int i = 0; i < ticks; ++i)
    {
        const float t = (float) i / (float) (ticks - 1), a = -sweep + 2.0f * sweep * t;
        const bool lit = enabled && t >= juce::jmin (from, value) - 0.001f && t <= juce::jmax (from, value) + 0.001f;
        g.setColour (lit ? hw.pointer : hw.tick);
        g.drawLine (c.x + std::sin (a) * (ring - 3.5f), c.y - std::cos (a) * (ring - 3.5f), c.x + std::sin (a) * ring, c.y - std::cos (a) * ring, 1.2f);
    }

    const auto cap = juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (c);
    g.setColour (theme.capShadow());
    g.fillEllipse (cap.translated (0.0f, 2.0f).expanded (0.5f));
    g.setGradientFill (juce::ColourGradient (hw.knobLight, c.x - body * 0.35f, c.y - body * 0.5f, hw.knobDark, c.x + body, c.y + body, true));
    g.fillEllipse (cap);
    // The knurling round its edge.
    g.setColour (hw.capEdge);
    for (int i = 0; i < 32; ++i)
    {
        const float a = (float) i * juce::MathConstants<float>::twoPi / 32.0f;
        g.drawLine (c.x + std::sin (a) * (body - 2.5f), c.y - std::cos (a) * (body - 2.5f), c.x + std::sin (a) * body, c.y - std::cos (a) * body, 0.8f);
    }
    g.drawEllipse (cap, 1.0f);

    const float dx = std::sin (angle), dy = -std::cos (angle);
    g.setColour (enabled ? hw.pointer : hw.tick);
    g.drawLine (c.x + dx * body * 0.25f, c.y + dy * body * 0.25f, c.x + dx * (body - 2.0f), c.y + dy * (body - 2.0f), 2.0f);
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
    linearMin = linearMax = 0.0f;
    setEnabled (db != nullptr);
    refresh();
}

void FaderControl::bindLinear (std::atomic<float>* v, float min, float max)
{
    value = v;
    linearMin = min;
    linearMax = max;
    setEnabled (v != nullptr);
    refresh();
}

double FaderControl::toPosition (float v) const noexcept
{
    if (linearMax > linearMin)
        return (double) juce::jlimit (0.0f, 1.0f, (v - linearMin) / (linearMax - linearMin));
    return (double) fader::dbToPos (v);
}

float FaderControl::fromPosition (double p) const noexcept
{
    if (linearMax > linearMin)
        return linearMin + (linearMax - linearMin) * (float) p;
    return fader::posToDb ((float) p);
}

void FaderControl::setPositionFromUser (double newPosition)
{
    position = juce::jlimit (0.0, 1.0, newPosition);
    if (value != nullptr)
        value->store (fromPosition (position));
    repaint();
}

void FaderControl::refresh()
{
    const double p = value != nullptr ? toPosition (value->load()) : 0.0;
    if (std::abs (p - position) > 1.0e-4)
    {
        position = p;
        repaint();
    }
}

juce::Rectangle<float> FaderControl::track() const noexcept
{
    // The cap's centre travels between these (the cap stays inside the control).
    return getLocalBounds().toFloat().reduced (0.0f, look != nullptr && look->cap == hw::Look::Cap::Ribbed ? 33.0f : look != nullptr && look->cap == hw::Look::Cap::Waisted ? 30.0f : look != nullptr ? kTrackInset : kCapInset);
}

void FaderControl::setKnobMode (bool knob)
{
    if (knob != knobMode)
    {
        knobMode = knob;
        repaint();
    }
}

void FaderControl::paint (juce::Graphics& g)
{
    if (knobMode)
    {
        const float size = juce::jlimit (56.0f, 76.0f, (float) juce::jmin (getWidth() - 8, getHeight()));
        const auto area = juce::Rectangle<float> (size, size).withCentre (getLocalBounds().toFloat().getCentre());
        drawKnob (g, area, (float) position, false, theme, isEnabled());
        if (emphasised)
        {
            g.setColour (theme.accent);
            g.drawEllipse (area.expanded (2.0f), 1.5f);
        }
        return;
    }
    const auto t = track();
    const float groove = t.getRight() - grooveFromRight; // the groove's centre
    const auto yAt = [&t] (float pos) { return t.getBottom() - t.getHeight() * pos; };

    static constexpr float marks[] = { 10.0f, 5.0f, 0.0f, -5.0f, -10.0f, -20.0f, -30.0f, -40.0f, -60.0f, fader::kMinDb };
    if (look != nullptr)
    {
        // A console's fader: its silkscreen scale (the X32's: 10 to -60, signed,
        // with -50), the slot, the moulded cap.
        static constexpr float consoleMarks[] = { 10.0f, 5.0f, 0.0f, -5.0f, -10.0f, -20.0f, -30.0f, -40.0f, -50.0f, -60.0f, fader::kMinDb };
        // The cap never covers the printed numbers: the legend ends where the cap's edge begins (a wide cap
        // moves it out), and a strip too narrow for both gets a narrower cap.
        const float capRatio = look->cap == hw::Look::Cap::Ribbed ? 0.85f : 0.5f; // the cap's half width to its `width`
        const float capWidth = juce::jlimit (14.0f, look->cap == hw::Look::Cap::Metal ? 26.0f : 28.0f, (groove - 20.0f) / capRatio);
        const float legendRight = juce::jmin (groove - 12.0f, groove - capWidth * capRatio - 3.0f);
        for (float db : consoleMarks)
        {
            if (db > fader::maxDb() + 0.01f || (look->shortScale && (db == -50.0f || db == -60.0f)))
                continue;
            const float y = yAt (fader::dbToPos (db));
            const auto label = db <= fader::kMinDb ? juce::String::fromUTF8 (look->shortScale ? "\xe2\x88\x9e" : "-\xe2\x88\x9e")
                                                   : juce::String ((int) (look->shortScale ? std::abs (db) : db));
            hw::silk (g, juce::Rectangle<float> (-2.0f, y - 7.0f, legendRight + 2.0f, 14.0f), label, *look, 9.0f, juce::Justification::centredRight,
                      db != 0.0f);
            g.setColour (look->silkDim.withAlpha (0.6f));
            g.fillRect (groove - 13.0f, y - 0.5f, 6.0f, 1.0f);
        }
        if (look->cap == hw::Look::Cap::Ribbed)
        {
            // The dLive's: ticks both sides of the slot, a stipple round 0 dB.
            for (float db : consoleMarks)
                if (db <= fader::maxDb() + 0.01f && ! (look->shortScale && (db == -50.0f || db == -60.0f)))
                {
                    g.setColour (look->silkDim.withAlpha (0.6f));
                    g.fillRect (groove + 7.0f, yAt (fader::dbToPos (db)) - 0.5f, 6.0f, 1.0f);
                }
            const float top = yAt (fader::dbToPos (5.0f)), bottom = yAt (fader::dbToPos (-5.0f));
            g.setColour (look->silkDim.withAlpha (0.42f));
            for (float y = top; y < bottom; y += 3.0f)
                for (float dx = 6.0f; dx < 13.0f; dx += 3.0f)
                {
                    g.fillRect (groove - dx - 1.0f, y, 1.2f, 1.2f);
                    g.fillRect (groove + dx, y, 1.2f, 1.2f);
                }
        }
        hw::faderSlot (g, groove, t.getY(), t.getBottom(), *look);
        if (isEnabled())
            hw::faderCap (g, groove, yAt ((float) position), capWidth, *look); // the X32's: narrow and tall
        return;
    }
    g.setFont (type::font (type::kMicro));
    // 0 dB and the ends first, then the marks that clear those and each other: on a
    // short fader the low marks crowd together and would print over one another.
    std::array<float, std::size (marks)> placed {};
    size_t numPlaced = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (float db : marks)
        {
            if (db > fader::maxDb() + 0.01f || (db == 0.0f || db == fader::kMinDb) != (pass == 0))
                continue;
            const float y = yAt (fader::dbToPos (db));
            if (std::any_of (placed.begin(), placed.begin() + (std::ptrdiff_t) numPlaced, [y] (float other) { return std::abs (other - y) < 13.0f; }))
                continue;
            placed[numPlaced++] = y;
            g.setColour (db == 0.0f ? theme.text : theme.textDim);
            const auto label = db <= fader::kMinDb ? juce::String::fromUTF8 ("-\xe2\x88\x9e") : (db > 0.0f ? "+" : "") + juce::String ((int) db);
            g.drawText (label, juce::Rectangle<float> (0.0f, y - 7.0f, groove - 18.0f, 14.0f), juce::Justification::centredRight);
            // Printed either side of the slot, longer at 0 dB.
            g.setColour (db == 0.0f ? theme.text : theme.textDim.withAlpha (0.7f));
            g.fillRect (groove - (db == 0.0f ? 16.0f : 13.0f), y - 0.5f, db == 0.0f ? 11.0f : 8.0f, 1.0f);
            g.fillRect (groove + 5.0f, y - 0.5f, db == 0.0f ? 11.0f : 8.0f, 1.0f);
        }

    // The slot, cut into the plate: dark inside, its far edge catching the light.
    const auto& hwTheme = theme.hw;
    const juce::Rectangle<float> slot (groove - 5.0f, t.getY() - 16.0f, 10.0f, t.getHeight() + 32.0f);
    g.setColour (theme.edgeDark());
    g.fillRect (slot.expanded (2.0f));
    g.setColour (theme.edgeLight());
    g.fillRect (slot.getX() - 2.0f, slot.getBottom() + 2.0f, slot.getWidth() + 4.0f, 1.0f);
    g.setColour (theme.well);
    g.fillRoundedRectangle (slot, sizes::kRadius);
    g.setColour (juce::Colours::black.withAlpha (theme.isLight ? 0.2f : 0.75f));
    g.fillRect (slot.getX() + 1.0f, slot.getY() + 2.0f, 3.0f, slot.getHeight() - 4.0f);
    g.setColour (theme.edgeLight());
    g.fillRect (slot.getRight(), slot.getY() + 2.0f, 1.0f, slot.getHeight() - 4.0f);
    if (isEnabled() && ! tint.isTransparent())
    {
        // A send on the fader shows in the destination's colour.
        g.setColour (tint);
        g.fillRoundedRectangle (groove - 3.0f, yAt ((float) position), 6.0f, t.getBottom() - yAt ((float) position), sizes::kRadius);
    }

    // The cap: machined, its face falling away to a waist, ribbed, casting a shadow into the slot.
    const float y = yAt ((float) position);
    const float half = juce::jmin (22.0f, grooveFromRight - 2.0f);
    const juce::Rectangle<float> cap (groove - half, y - 29.0f, half * 2.0f, 58.0f);
    // Its shadow, falling down the slot and onto the plate.
    g.setGradientFill (juce::ColourGradient (theme.capShadow(), 0.0f, cap.getBottom(), juce::Colours::transparentBlack, 0.0f, cap.getBottom() + 16.0f, false));
    g.fillRect (slot.getX(), cap.getBottom(), slot.getWidth(), 16.0f);
    g.setColour (theme.capShadow().withMultipliedAlpha (0.5f));
    g.fillRoundedRectangle (cap.translated (2.0f, 5.0f).expanded (1.0f), sizes::kRadius);
    g.setColour (theme.capShadow());
    g.fillRoundedRectangle (cap.translated (0.0f, 2.0f).expanded (1.0f), sizes::kRadius);
    juce::ColourGradient body (hwTheme.capTop, cap.getX(), cap.getY(), hwTheme.capBottom, cap.getX(), cap.getBottom(), false);
    body.addColour (0.46, hwTheme.capUpper);
    body.addColour (0.54, hwTheme.capLower);
    g.setGradientFill (body);
    g.fillRoundedRectangle (cap, sizes::kRadius);
    g.setColour (hwTheme.capEdge);
    g.drawRoundedRectangle (cap, sizes::kRadius, 1.0f);
    g.setColour (hwTheme.capShine);
    g.fillRect (cap.getX() + 1.0f, cap.getY() + 1.0f, cap.getWidth() - 2.0f, 1.0f);
    for (float row = 5.0f; row < cap.getHeight() - 4.0f; row += 3.0f)
    {
        if (std::abs (row - cap.getHeight() * 0.5f) < 5.0f)
            continue;
        g.setColour (juce::Colours::black.withAlpha (theme.isLight ? 0.2f : 0.45f));
        g.fillRect (cap.getX() + 3.0f, cap.getY() + row, cap.getWidth() - 6.0f, 1.0f);
        g.setColour (hwTheme.capShine.withMultipliedAlpha (0.55f));
        g.fillRect (cap.getX() + 3.0f, cap.getY() + row + 1.0f, cap.getWidth() - 6.0f, 1.0f);
    }
    g.setColour (! isEnabled() ? theme.textDim : hwTheme.capLine);
    g.fillRect (cap.getX() + 1.0f, y - 1.5f, cap.getWidth() - 2.0f, 3.0f);
    if (emphasised)
    {
        // The selected strip's cap: its line carries a glow.
        g.setColour (hwTheme.capLine.withAlpha (0.35f));
        g.fillRect (cap.getX() + 1.0f, y - 3.5f, cap.getWidth() - 2.0f, 7.0f);
    }
}

bool TouchGesture::update (juce::Component& control, const juce::MouseEvent& e)
{
    const int dx = e.getDistanceFromDragStartX(), dy = e.getDistanceFromDragStartY();
    if (mode == Mode::Undecided)
    {
        if (juce::jmax (std::abs (dx), std::abs (dy)) < kTouchSlop)
            return false;
        view = std::abs (dx) > std::abs (dy) ? control.findParentComponentOfClass<juce::Viewport>() : nullptr;
        mode = view != nullptr ? Mode::Scroll : Mode::Value;
        if (view != nullptr)
            viewStart = view->getViewPosition();
        return false; // the event that decided moves nothing
    }
    if (mode == Mode::Scroll && view != nullptr)
    {
        view->setViewPosition (viewStart.x - dx, viewStart.y);
        return false;
    }
    return mode == Mode::Value;
}

void FaderControl::mouseDown (const juce::MouseEvent&)
{
    lastDragY = 0;
    gesture = {};
}

void FaderControl::mouseDrag (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;
    const int dragged = e.getDistanceFromDragStartY();
    if (look == nullptr && e.source.isTouch() && ! gesture.update (*this, e))
    {
        lastDragY = dragged;
        return;
    }
    // As far as the touch travels; finer with Shift, and finer the further the finger goes off the
    // fader to the side (the Universal Mixer's: a console's surface moves as its fader does).
    const double off = look != nullptr ? 0.0 : (double) juce::jmax (0, std::abs (e.x - getWidth() / 2) - getWidth() / 2);
    const double scale = (e.mods.isShiftDown() ? 0.25 : 1.0) * juce::jmax (0.1, 1.0 / (1.0 + off / 40.0));
    const double travel = (double) -(dragged - lastDragY) / (knobMode ? 150.0 : juce::jmax (1.0, (double) track().getHeight()));
    lastDragY = dragged;
    setPositionFromUser (position + travel * scale);
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
    channels = juce::jlimit (1, kMaxChannels, numChannels);
    for (size_t ch = (size_t) channels; ch < positions.size(); ++ch)
        positions[ch] = peaks[ch] = 0.0f;
    repaint();
}

void MeterControl::refresh()
{
    bool moved = false;
    const auto now = juce::Time::getMillisecondCounter();
    static const float fullScale = fader::dbToPos (0.0f);
    for (int ch = 0; ch < channels; ++ch)
    {
        const float p = level ? fader::dbToPos (dsp::gainToDb (level (ch))) : 0.0f;
        auto& peak = peaks[(size_t) ch];
        if (p >= peak)
        {
            peak = p;
            peakAt[(size_t) ch] = now;
        }
        else if (now - peakAt[(size_t) ch] > 1500u)
        {
            peak = juce::jmax (p, peak - 0.012f); // held, then falling
            moved = true;
        }
        auto& clip = clipUntil[(size_t) ch];
        if (p >= fullScale)
        {
            moved = moved || clip == 0;
            clip = juce::jmax (1u, now + 2000u);
        }
        else if (clip != 0 && now >= clip)
        {
            clip = 0;
            moved = true;
        }
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
    const float clipH = 4.0f, gap = 2.0f;
    const float barW = (area.getWidth() - gap * (float) (channels - 1)) / (float) channels;
    // The bars span a fader's travel; the clip light sits above them.
    const auto bars = area.reduced (0.0f, inset);
    const float lowTop = fader::dbToPos (-18.0f), midTop = fader::dbToPos (-6.0f), highTop = fader::dbToPos (0.0f);
    const auto zone = [&] (float p) { return p >= highTop ? theme.hw.ledHot : p >= midTop ? theme.meterHigh : p >= lowTop ? theme.hw.ledWarn : theme.hw.ledOk; };

    for (int ch = 0; ch < channels; ++ch)
    {
        const float x = area.getX() + (barW + gap) * (float) ch;
        const auto bar = juce::Rectangle<float> (x, bars.getY(), barW, bars.getHeight());
        g.setColour (theme.well);
        g.fillRect (bar);

        // LEDs: unlit ones show faintly, as a dark diode does.
        const float p = positions[(size_t) ch], peak = peaks[(size_t) ch];
        for (float y = bar.getBottom() - 2.0f; y >= bar.getY(); y -= 3.0f)
        {
            const float at = (bar.getBottom() - y - 1.0f) / bar.getHeight();
            const auto c = zone (at);
            g.setColour (at <= p ? c : c.withAlpha (0.1f));
            g.fillRect (x + 1.0f, y, barW - 2.0f, 2.0f);
        }
        if (peak > 0.002f)
        {
            g.setColour (zone (peak));
            g.fillRect (x + 1.0f, bar.getBottom() - bar.getHeight() * peak - 1.0f, barW - 2.0f, 2.0f);
        }
        g.setColour (clipUntil[(size_t) ch] != 0 ? theme.hw.ledHot : theme.hw.ledHot.withAlpha (0.12f));
        g.fillRect (x + 1.0f, bars.getY() - clipH - gap, barW - 2.0f, clipH);
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
    auto area = getLocalBounds().toFloat();
    const auto readout = area.removeFromRight (area.getWidth() * 0.44f);
    drawKnob (g, area, (shown + 100.0f) / 200.0f, true, theme, isEnabled());
    const int rounded = juce::roundToInt (shown);
    const auto text = rounded == 0 ? juce::String ("C") : (rounded < 0 ? "L" : "R") + juce::String (std::abs (rounded));
    g.setColour (isEnabled() ? theme.text : theme.textDim);
    g.setFont (type::font (type::kCaption, true));
    g.drawText (text, readout, juce::Justification::centredLeft);
}

void PanControl::mouseDown (const juce::MouseEvent&)
{
    dragStart = shown;
    gesture = {};
    dragBase = 0;
}

void PanControl::mouseDrag (const juce::MouseEvent& e)
{
    const int dragged = e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY();
    if (e.source.isTouch() && ! gesture.update (*this, e))
    {
        dragBase = dragged;
        return;
    }
    if (isEnabled())
        setPanFromUser (dragStart + 200.0f * (float) (dragged - dragBase) / juce::jmax (1.0f, (float) getWidth()) * (e.mods.isShiftDown() ? 0.25f : 1.0f));
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
    // What it shows now, whoever asked for the repaint (a view shown for the first time, a panel
    // that only repaints its keys): refresh() alone would leave a key dark until something changed.
    lit = isOn && isOn();
    halfLit = ! lit && isHalfOn && isHalfOn();
    if (look != nullptr)
    {
        auto area = getLocalBounds().toFloat();
        if (shape != Shape::Rect)
        {
            // An oval or a triangle of rubber, lit by its colour's edge.
            auto a = area.reduced (2.0f).translated (0.0f, down ? 1.0f : 0.0f);
            juce::Path p;
            if (shape == Shape::Oval)
                p.addRoundedRectangle (a, cornerRadius >= 0.0f ? cornerRadius : a.getHeight() * 0.5f);
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
            if (windowLit)
            {
                // The SQ's keys, from the console's photographs: the cap keeps its own colour
                // and a short slot in it, a little above the middle, is the light. Dark when
                // off; lit, it glows and spills a little colour into a pale cap around it.
                const bool on = lit || halfLit;
                const auto top = face.isTransparent() ? look->keyTop : face.brighter (0.12f), bottom = face.isTransparent() ? look->keyBottom : face.darker (0.12f);
                const float spill = on ? (top.getPerceivedBrightness() > 0.7f ? 0.3f : 0.1f) : 0.0f;
                g.setGradientFill (juce::ColourGradient (top.interpolatedWith (colour, spill), a.getX(), a.getY(), bottom.interpolatedWith (colour, spill), a.getX(), a.getBottom(), false));
                g.fillPath (p);
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.strokePath (p, juce::PathStrokeType (1.2f));
                if (shape == Shape::Oval)
                {
                    const auto window = juce::Rectangle<float> (juce::jlimit (7.0f, 18.0f, a.getWidth() * 0.3f), juce::jlimit (3.5f, 6.0f, a.getHeight() * 0.2f))
                                            .withCentre ({ a.getCentreX(), a.getY() + a.getHeight() * 0.44f });
                    if (on)
                    {
                        g.setColour (colour.withAlpha (lit ? 0.4f : 0.2f));
                        g.fillRoundedRectangle (window.expanded (2.5f), window.getHeight() * 0.5f + 2.5f);
                    }
                    g.setColour (lit ? colour.brighter (0.35f) : halfLit ? colour.darker (0.25f) : juce::Colours::black.withAlpha (0.45f));
                    g.fillRoundedRectangle (window, window.getHeight() * 0.5f);
                }
                return;
            }
            if (lit && look->backlit)
            {
                // A backlit console's key: the whole key glows in its colour.
                g.setGradientFill (juce::ColourGradient (colour.brighter (0.2f), a.getX(), a.getY(), colour.darker (0.2f), a.getX(), a.getBottom(), false));
                g.fillPath (p);
                return;
            }
            g.setGradientFill (juce::ColourGradient (face.isTransparent() ? look->keyTop : face.brighter (0.12f), a.getX(), a.getY(),
                                                     face.isTransparent() ? look->keyBottom : face.darker (0.12f), a.getX(), a.getBottom(), false));
            g.fillPath (p);
            g.setColour (lit || (halfLit && look->backlit) ? colour : juce::Colours::black.withAlpha (0.35f));
            g.strokePath (p, juce::PathStrokeType (lit ? 1.0f : 1.6f));
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
            g.setColour (on ? colour.brighter (0.25f) : juce::Colour (0xffe8eaec));
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
            // The name over the key, as the panel prints it.
            const auto printed = belowText.isNotEmpty() ? belowText : getButtonText();
            auto label = area.removeFromTop (13.0f);
            g.setColour (labelColour.isTransparent() ? look->silk : labelColour);
            const auto font = type::font (11.0f);
            g.setFont (font);
            g.drawFittedText (printed, label.toNearestInt(), juce::Justification::centred, 1, 0.7f);
            const float w = juce::jmin (label.getWidth() - 4.0f, juce::GlyphArrangement::getStringWidth (font, printed) + 2.0f);
            if (look->backlit)
                // The X32's: the name in a small outlined box.
                g.drawRoundedRectangle (juce::Rectangle<float> (w + 4.0f, label.getHeight() - 1.0f).withCentre (label.getCentre()), 2.0f, 1.0f);
            hw::key (g, area.reduced (3.0f, 2.0f), {}, lit || halfLit, down, colour, *look, face, showLed);
            return;
        }
        hw::key (g, area.reduced (2.0f), getButtonText(), lit || halfLit, down, colour, *look, face, showLed);
        return;
    }
    const auto area = getLocalBounds().toFloat().reduced (1.5f);
    if (tab)
    {
        if (highlighted || down)
        {
            g.setColour (theme.raised);
            g.fillRoundedRectangle (area, sizes::kRadius);
        }
        if (lit)
        {
            g.setColour (colour);
            g.fillRect (area.getX(), area.getBottom() - 3.0f, area.getWidth(), 3.0f);
        }
        g.setColour (lit ? theme.text : theme.textDim);
    }
    else
    {
        // A backlit square key: its face a hard block, a window at its top lit in its colour.
        const auto top = lit ? colour.brighter (0.15f) : (down ? theme.hw.keyBottom : theme.hw.keyTop);
        const auto bottom = lit ? colour.darker (0.2f) : (down ? theme.hw.keyTop : theme.hw.keyBottom);
        // The well it sits in, its side showing below the face until it is pressed.
        g.setColour (theme.isLight ? theme.outline : theme.well);
        g.fillRoundedRectangle (area, sizes::kRadius);
        const auto faceArea = area.reduced (1.0f).withTrimmedBottom (down ? 1.0f : 3.0f).translated (0.0f, down ? 1.5f : 0.0f);
        g.setColour (lit ? colour.darker (0.55f) : bottom.darker (0.45f));
        g.fillRoundedRectangle (area.reduced (1.0f), sizes::kRadius);
        g.setGradientFill (juce::ColourGradient (highlighted && ! lit ? top.brighter (0.08f) : top, 0.0f, faceArea.getY(), bottom, 0.0f, faceArea.getBottom(), false));
        g.fillRoundedRectangle (faceArea, sizes::kRadius);
        g.setColour (halfLit ? colour : theme.hw.capEdge);
        g.drawRoundedRectangle (faceArea, sizes::kRadius, halfLit ? 2.0f : 1.0f);
        g.setColour (theme.hw.capShine);
        g.fillRect (faceArea.getX() + 2.0f, faceArea.getY() + 1.0f, faceArea.getWidth() - 4.0f, 1.0f);
        if (isOn && area.getHeight() >= 30.0f)
        {
            const auto window = juce::Rectangle<float> (juce::jmin (18.0f, area.getWidth() * 0.4f), 3.0f).withCentre ({ area.getCentreX(), area.getY() + 6.5f });
            if (lit || halfLit)
            {
                g.setColour (colour.withAlpha (0.45f));
                g.fillRect (window.expanded (1.5f));
            }
            g.setColour (lit ? colour.brighter (0.7f) : (halfLit ? colour : theme.well));
            g.fillRect (window);
        }
        g.setColour (lit ? theme.textOnAccent : (isEnabled() ? theme.hw.keyText : theme.textDim));
    }
    // A narrow key (the channel panel's INVERT, LOW CUT in a slim column) makes its
    // label smaller rather than cut it ("IN...").
    auto font = type::font (type::kLabel, true);
    const float room = area.getWidth() - 8.0f;
    if (const float w = juce::GlyphArrangement::getStringWidth (font, getButtonText()); w * 0.7f > room && w > 0.0f)
        font = font.withHeight (juce::jmax (9.0f, font.getHeight() * room / (w * 0.7f)));
    g.setFont (font);
    // A key with a device tag: a label that would reach the tag keeps to its left.
    auto textArea = area.toNearestInt().reduced (4, 0);
    if (const float tag = deviceTagWidth (device); tag > 0.0f)
    {
        const float half = juce::GlyphArrangement::getStringWidth (font, getButtonText()) / 2.0f;
        if (area.getCentreX() + half > area.getRight() - 5.0f - tag)
            textArea.removeFromRight ((int) tag);
    }
    g.drawFittedText (getButtonText(), textArea, juce::Justification::centred, 1, 0.7f);
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
