#include "ui/universal/ValueControls.h"

#include "ui/Knobs.h"

#include <cmath>

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kRowHeight = 40;
} // namespace

//==============================================================================
ValueBar::ValueBar (const Theme& t) : theme (t)
{
    setViewportIgnoreDragFlag (true);
    setEnabled (false);
}

ValueBar::Binding ValueBar::linear (const juce::String& caption, std::atomic<float>& v, float min, float max, float defaultValue,
                                    const juce::String& unit, int decimals)
{
    Binding b;
    b.caption = caption;
    b.value = &v;
    b.toPosition = [min, max] (float x) { return (x - min) / (max - min); };
    b.fromPosition = [min, max] (float p) { return min + p * (max - min); };
    b.text = [unit, decimals] (float x) { return juce::String (x, decimals) + unit; };
    b.defaultValue = defaultValue;
    return b;
}

ValueBar::Binding ValueBar::frequency (const juce::String& caption, std::atomic<float>& v, float min, float max, float defaultValue)
{
    Binding b;
    b.caption = caption;
    b.value = &v;
    const float ratio = std::log (max / min);
    b.toPosition = [min, ratio] (float hz) { return std::log (juce::jmax (hz, 1.0f) / min) / ratio; };
    b.fromPosition = [min, ratio] (float p) { return min * std::exp (p * ratio); };
    b.text = [] (float hz) { return formatHz (hz) + " Hz"; };
    b.defaultValue = defaultValue;
    return b;
}

ValueBar::Binding ValueBar::level (const juce::String& caption, std::atomic<float>& v)
{
    Binding b;
    b.caption = caption;
    b.value = &v;
    b.toPosition = [] (float db) { return fader::dbToPos (db); };
    b.fromPosition = [] (float p) { return fader::posToDb (p); };
    b.text = [] (float db) { return fader::text (db) + " dB"; };
    b.defaultValue = 0.0f;
    return b;
}

ValueBar::Binding ValueBar::param (std::atomic<float>& v, const models::Param& p)
{
    Binding b;
    b.caption = p.label.toUpperCase();
    b.value = &v;
    b.toPosition = [p] (float x) { return p.toNormalised (x); };
    b.fromPosition = [p] (float pos) { return p.fromNormalised (pos); };
    b.text = [p] (float x) { return p.display (x); };
    b.defaultValue = p.defaultValue;
    return b;
}

void ValueBar::bind (Binding b)
{
    binding = std::move (b);
    setEnabled (binding.value != nullptr);
    setTitle (binding.caption);
    shownText = {};
    refresh();
    repaint();
}

void ValueBar::unbind()
{
    bind ({});
}

void ValueBar::setPositionFromUser (float newPosition)
{
    if (binding.value == nullptr)
        return;
    binding.value->store (binding.fromPosition (juce::jlimit (0.0f, 1.0f, newPosition)));
    refresh();
}

void ValueBar::refresh()
{
    if (binding.value == nullptr)
        return;
    const float v = binding.value->load();
    const float p = juce::jlimit (0.0f, 1.0f, binding.toPosition (v));
    auto text = binding.text ? binding.text (v) : juce::String (v);
    if (std::abs (p - position) > 1.0e-4f || text != shownText)
    {
        position = p;
        shownText = std::move (text);
        repaint();
    }
}

void ValueBar::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme.well);
    g.fillRoundedRectangle (area, sizes::kRadius);
    if (isEnabled())
    {
        g.setColour (theme.accent.withAlpha (0.35f));
        g.fillRoundedRectangle (area.withWidth (juce::jmax (sizes::kRadius * 2.0f, area.getWidth() * position)), sizes::kRadius);
    }
    auto text = area.reduced (10.0f, 0.0f);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption, true));
    g.drawText (binding.caption, text, juce::Justification::centredLeft);
    g.setColour (isEnabled() ? theme.text : theme.textDim);
    g.setFont (type::font (type::kValue));
    g.drawText (shownText, text, juce::Justification::centredRight);
}

void ValueBar::mouseDown (const juce::MouseEvent&)
{
    dragStart = position;
}

void ValueBar::mouseDrag (const juce::MouseEvent& e)
{
    if (isEnabled())
        setPositionFromUser (dragStart + (float) e.getDistanceFromDragStartX() / juce::jmax (1.0f, (float) getWidth()) * (e.mods.isShiftDown() ? 0.25f : 1.0f));
}

void ValueBar::mouseDoubleClick (const juce::MouseEvent&)
{
    if (binding.value != nullptr)
    {
        binding.value->store (binding.defaultValue);
        refresh();
    }
}

void ValueBar::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (isEnabled() && wheel.deltaY != 0.0f)
        setPositionFromUser (position + (wheel.deltaY > 0.0f ? 0.01f : -0.01f));
}

//==============================================================================
ChoiceBar::ChoiceBar (const Theme& t) : theme (t) {}

void ChoiceBar::chooseItem (int index)
{
    if (choose && items && juce::isPositiveAndBelow (index, items().size()))
        choose (index);
    refresh();
}

void ChoiceBar::refresh()
{
    const auto list = items ? items() : juce::StringArray();
    const int i = current ? current() : -1;
    auto text = juce::isPositiveAndBelow (i, list.size()) ? list[i] : juce::String ("-");
    if (text != shownText)
    {
        shownText = std::move (text);
        repaint();
    }
}

void ChoiceBar::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme.raised);
    g.fillRoundedRectangle (area, sizes::kRadius);
    g.setColour (theme.outline);
    g.drawRoundedRectangle (area, sizes::kRadius, 1.0f);
    auto text = area.reduced (10.0f, 0.0f);
    g.setColour (theme.textDim);
    const auto captionFont = type::font (type::kCaption, true);
    g.setFont (captionFont);
    g.drawText (caption, text, juce::Justification::centredLeft);
    // The value, with a mark that says it opens a list; the tag between, where there's room.
    auto valueFont = type::font (type::kValue, true);
    auto value = shownText + juce::String::fromUTF8 ("  \xe2\x96\xbe");
    // A narrow bar (a send's FROM on one line) gives up the list mark, then makes
    // the value smaller, rather than cutting it ("PRE...").
    const float room = text.getWidth() - (caption.isNotEmpty() ? juce::GlyphArrangement::getStringWidth (captionFont, caption) + 8.0f : 0.0f);
    if (juce::GlyphArrangement::getStringWidth (valueFont, value) > room)
        value = shownText;
    if (const float w = juce::GlyphArrangement::getStringWidth (valueFont, value); w > room && w > 0.0f)
        valueFont = valueFont.withHeight (juce::jmax (9.0f, valueFont.getHeight() * juce::jmax (0.0f, room) / w));
    const auto between = text.withTrimmedLeft (juce::GlyphArrangement::getStringWidth (captionFont, caption) + 8.0f)
                             .withTrimmedRight (juce::GlyphArrangement::getStringWidth (valueFont, value) + 6.0f);
    if (between.getWidth() >= deviceTagWidth (device))
        drawDeviceTag (g, between, device, theme, true);
    g.setColour (theme.text);
    g.setFont (valueFont);
    g.drawFittedText (value, text.toNearestInt(), juce::Justification::centredRight, 1, 0.8f);
}

void ChoiceBar::mouseUp (const juce::MouseEvent& e)
{
    if (! items || e.mouseWasDraggedSinceMouseDown())
        return;
    if (paintItem)
    {
        // The pictures, in a call-out under the bar (scrolled when there are many).
        juce::Component::SafePointer<ChoiceBar> safe (this);
        auto grid = std::make_unique<PictureGrid> (theme, items(), current ? current() : -1, paintItem, [safe] (int index)
        {
            if (safe != nullptr)
                safe->chooseItem (index);
        });
        auto holder = std::make_unique<juce::Viewport>();
        holder->setScrollBarsShown (true, false);
        const int width = grid->getWidth() + holder->getScrollBarThickness();
        const int height = juce::jmin (grid->getHeight(), 3 * PictureGrid::kTileHeight + PictureGrid::kTileHeight / 2);
        holder->setViewedComponent (grid.release(), true);
        holder->setSize (width, height);
        juce::CallOutBox::launchAsynchronously (std::move (holder), getScreenBounds(), nullptr);
        return;
    }
    juce::PopupMenu menu;
    const auto list = items();
    const int i = current ? current() : -1;
    for (int k = 0; k < list.size(); ++k)
        menu.addItem (k + 1, list[k], true, k == i);
    juce::Component::SafePointer<ChoiceBar> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()).withStandardItemHeight (sizes::kTouch),
                        [safe] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->chooseItem (result - 1);
                        });
}

//==============================================================================
ResponseView::ResponseView (const Theme& t) : theme (t)
{
    setInterceptsMouseClicks (false, false);
    constexpr int n = 96;
    for (int i = 0; i < n; ++i)
        freqs.push_back (20.0 * std::pow (1000.0, (double) i / (double) (n - 1)));
}

std::vector<ResponseView::Handle> ResponseView::eqHandles (const models::Model& m, std::atomic<float>* values)
{
    std::vector<Handle> out;
    if (values == nullptr)
        return out;
    for (int i = 0; i < (int) m.params.size(); ++i)
    {
        const auto& p = m.params[(size_t) i];
        if (p.id.length() < 2 || ! p.id.endsWithChar ('f') || p.kind != models::ParamKind::Log)
            continue;
        const auto band = p.id.dropLastCharacters (1);
        const int gi = m.find (band + "g");
        if (gi < 0)
            continue; // a band without gain (a cut) has no point on the curve to hold
        const auto& gp = m.params[(size_t) gi];
        Handle h;
        h.label = band.toUpperCase();
        h.freq = &values[i];
        h.minFreq = p.min;
        h.maxFreq = p.max;
        h.defaultFreq = p.defaultValue;
        h.gain = &values[gi];
        h.minGain = gp.min;
        h.maxGain = gp.max;
        h.defaultGain = gp.defaultValue;
        if (const int qi = m.find (band + "q"); qi >= 0 && m.params[(size_t) qi].isContinuous())
        {
            const auto& qp = m.params[(size_t) qi];
            h.q = &values[qi];
            h.minQ = qp.min;
            h.maxQ = qp.max;
            h.defaultQ = qp.defaultValue;
        }
        out.push_back (h);
    }
    return out;
}

void ResponseView::refresh()
{
    const auto before = handles.size();
    if (handleSource && dragging < 0)
        handles = handleSource();
    setInterceptsMouseClicks (! handles.empty(), false);

    std::vector<double> db (freqs.size(), 0.0);
    if (! response || ! response (freqs.data(), db.data(), (int) freqs.size()))
        db.clear();
    // A handle that moves moves the curve; a cut switched on or off shows or hides one.
    juce::String active;
    for (const auto& h : handles)
        active << (int) isActive (h);
    if (db != curve || handles.size() != before || active != shownActive || dragging >= 0)
    {
        curve = std::move (db);
        shownActive = active;
        repaint();
    }
}

juce::Rectangle<float> ResponseView::plotArea() const { return getLocalBounds().toFloat().reduced (1.0f); }

float ResponseView::xAt (double hz) const
{
    const auto area = plotArea();
    return area.getX() + area.getWidth() * (float) (std::log (juce::jmax (1.0, hz) / 20.0) / std::log (1000.0));
}

float ResponseView::yAt (double db) const
{
    const auto area = plotArea();
    return area.getCentreY() - area.getHeight() * 0.5f * (float) juce::jlimit (-1.0, 1.0, db / rangeDb);
}

double ResponseView::hzAt (float x) const
{
    const auto area = plotArea();
    return 20.0 * std::pow (1000.0, (double) ((x - area.getX()) / juce::jmax (1.0f, area.getWidth())));
}

double ResponseView::dbAt (float y) const
{
    const auto area = plotArea();
    return (double) ((area.getCentreY() - y) / juce::jmax (1.0f, area.getHeight() * 0.5f)) * rangeDb;
}

bool ResponseView::isActive (const Handle& h) const { return h.freq != nullptr && (! h.active || h.active()); }

juce::Point<float> ResponseView::handlePosition (int i) const
{
    if (! juce::isPositiveAndBelow (i, (int) handles.size()))
        return {};
    const auto& h = handles[(size_t) i];
    return { xAt (h.freq->load()), h.gain != nullptr ? yAt (h.gain->load()) : yAt (0.0) };
}

int ResponseView::handleAt (juce::Point<float> pos) const
{
    int best = -1;
    float bestDistance = 18.0f; // a fingertip
    for (int i = 0; i < (int) handles.size(); ++i)
        if (isActive (handles[(size_t) i]))
            if (const float d = handlePosition (i).getDistanceFrom (pos); d < bestDistance)
            {
                best = i;
                bestDistance = d;
            }
    return best;
}

void ResponseView::mouseDown (const juce::MouseEvent& e) { dragging = handleAt (e.position); }

void ResponseView::mouseDrag (const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (dragging, (int) handles.size()))
        return;
    auto& h = handles[(size_t) dragging];
    h.freq->store ((float) juce::jlimit ((double) h.minFreq, (double) h.maxFreq, hzAt (e.position.x)));
    if (h.gain != nullptr)
        h.gain->store ((float) juce::jlimit ((double) h.minGain, (double) h.maxGain, dbAt (e.position.y)));
    refresh();
    if (onEdited)
        onEdited();
}

void ResponseView::mouseUp (const juce::MouseEvent&) { dragging = -1; }

void ResponseView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = handleAt (e.position);
    if (i < 0)
        return;
    auto& h = handles[(size_t) i];
    h.freq->store (h.defaultFreq);
    if (h.gain != nullptr)
        h.gain->store (h.defaultGain);
    if (h.q != nullptr)
        h.q->store (h.defaultQ);
    refresh();
    if (onEdited)
        onEdited();
}

void ResponseView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int i = handleAt (e.position);
    if (i < 0 || handles[(size_t) i].q == nullptr)
    {
        juce::Component::mouseWheelMove (e, wheel);
        return;
    }
    auto& h = handles[(size_t) i];
    const float factor = wheel.deltaY > 0 ? 1.12f : (wheel.deltaY < 0 ? 1.0f / 1.12f : 1.0f);
    h.q->store (juce::jlimit (h.minQ, h.maxQ, h.q->load() * factor));
    refresh();
    if (onEdited)
        onEdited();
}

void ResponseView::paint (juce::Graphics& g)
{
    const auto area = plotArea();
    g.setColour (theme.well);
    g.fillRoundedRectangle (area, sizes::kRadius);

    g.setColour (theme.outline);
    for (double hz : { 100.0, 1000.0, 10000.0 })
        g.fillRect (xAt (hz), area.getY(), 1.0f, area.getHeight());
    g.fillRect (area.getX(), yAt (0.0), area.getWidth(), 1.0f);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption));
    for (auto [hz, label] : { std::pair<double, const char*> { 100.0, "100" }, { 1000.0, "1k" }, { 10000.0, "10k" } })
    {
        // Right of its line, or left of it where the graph ends first (a narrow FILTER: "10" for "10k").
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 4.0f;
        const float x = xAt (hz);
        if (x + 3.0f + w <= area.getRight())
            g.drawText (label, juce::Rectangle<float> (x + 3.0f, area.getBottom() - 16.0f, w, 14.0f), juce::Justification::centredLeft);
        else
            g.drawText (label, juce::Rectangle<float> (x - 3.0f - w, area.getBottom() - 16.0f, w, 14.0f), juce::Justification::centredRight);
    }

    if (! curve.empty())
    {
        juce::Path p;
        for (size_t i = 0; i < curve.size(); ++i)
        {
            const juce::Point<float> pt (xAt (freqs[i]), yAt (curve[i]));
            if (i == 0)
                p.startNewSubPath (pt);
            else
                p.lineTo (pt);
        }
        g.setColour (theme.accent);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
    }

    // The handles: a ring with the band's name; the one held, filled.
    const float r = juce::jlimit (6.0f, 11.0f, area.getHeight() * 0.06f);
    for (int i = 0; i < (int) handles.size(); ++i)
    {
        if (! isActive (handles[(size_t) i]))
            continue;
        const auto c = handlePosition (i);
        const auto ring = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
        g.setColour (theme.accent.withAlpha (i == dragging ? 0.9f : 0.25f));
        g.fillEllipse (ring);
        g.setColour (theme.accent);
        g.drawEllipse (ring, 1.5f);
        g.setColour (i == dragging ? theme.background : theme.text);
        g.setFont (type::font (juce::jmin (type::kCaption, r * 1.2f), true));
        g.drawText (handles[(size_t) i].label, ring, juce::Justification::centred, false);
    }
}

//==============================================================================
TransferView::TransferView (const Theme& t) : theme (t)
{
    for (int i = 0; i <= 60; ++i)
        inDb.push_back (-60.0f + (float) i);
}

void TransferView::bind (DynSlotParams* p)
{
    params = p;
    shownValues.clear();
    refresh();
}

juce::Rectangle<float> TransferView::plotArea() const
{
    auto area = getLocalBounds().toFloat().reduced (1.0f);
    area.removeFromRight (12.0f); // the gain reduction bar
    return area;
}

float TransferView::xAt (float db) const
{
    const auto area = plotArea();
    return area.getX() + area.getWidth() * (db + 60.0f) / 60.0f;
}

float TransferView::yAt (float db) const
{
    const auto area = plotArea();
    return area.getBottom() - area.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

float TransferView::dbAtX (float x) const
{
    const auto area = plotArea();
    return (x - area.getX()) / juce::jmax (1.0f, area.getWidth()) * 60.0f - 60.0f;
}

float TransferView::outAt (float in) const
{
    if (outDb.size() != inDb.size())
        return in;
    const float pos = juce::jlimit (0.0f, 60.0f, in + 60.0f);
    const int i = juce::jmin (59, (int) pos);
    return outDb[(size_t) i] + (outDb[(size_t) i + 1] - outDb[(size_t) i]) * (pos - (float) i);
}

float TransferView::getCurveAt (float in) const { return outAt (in); }

void TransferView::computeCurve()
{
    outDb.clear();
    thrIndex = slopeIndex = -1;
    shownValues.clear();
    if (params == nullptr)
        return;
    const int model = params->model.load();
    curveModel = model;
    const auto& spec = models::model (models::Dynamics, model);
    thrIndex = spec.find ("thr");
    slopeIndex = spec.find ("ratio");
    if (slopeIndex < 0)
        slopeIndex = spec.find ("range");
    std::vector<float> values ((size_t) ld::kMaxDynParams);
    for (int i = 0; i < ld::kMaxDynParams; ++i)
        values[(size_t) i] = params->values[i].load();
    outDb.resize (inDb.size());
    if (! models::dynStaticCurve (model, values.data(), (int) values.size(), inDb.data(), outDb.data(), (int) inDb.size()))
        outDb.clear();
    shownValues = values;
    shownValues.push_back ((float) model);
}

void TransferView::refresh()
{
    std::vector<float> now;
    if (params != nullptr)
    {
        for (int i = 0; i < ld::kMaxDynParams; ++i)
            now.push_back (params->values[i].load());
        now.push_back ((float) params->model.load());
    }
    bool changed = false;
    if (params == nullptr || now != shownValues)
    {
        computeCurve();
        changed = true;
    }
    setInterceptsMouseClicks (thrIndex >= 0 || slopeIndex >= 0, false);
    const float gr = reductionDb ? reductionDb() : 0.0f;
    if (std::abs (gr - reduction) > 0.1f)
    {
        reduction = gr;
        changed = true;
    }
    if (changed || dragging >= 0)
        repaint();
}

const models::Param* TransferView::slopeParam() const
{
    // The model the curve and its indexes were made for: the strip's model can
    // change (a recall, a console's own processing) before the next refresh.
    const auto& spec = models::model (models::Dynamics, curveModel);
    return juce::isPositiveAndBelow (slopeIndex, (int) spec.params.size()) ? &spec.params[(size_t) slopeIndex] : nullptr;
}

juce::Point<float> TransferView::handlePosition (int which) const
{
    if (params == nullptr || outDb.empty())
        return {};
    if (which == 0 && thrIndex >= 0)
    {
        const float thr = params->values[thrIndex].load();
        return { xAt (thr), yAt (outAt (thr)) };
    }
    if (which == 1 && slopeIndex >= 0)
    {
        // A compressor's ratio at the top of the curve, a gate's range at its foot.
        const auto* p = slopeParam();
        if (p == nullptr)
            return {};
        const bool range = p->id == "range";
        const float at = range ? -54.0f : 0.0f;
        return { xAt (at), yAt (outAt (at)) };
    }
    return {};
}

void TransferView::mouseDown (const juce::MouseEvent& e)
{
    dragging = -1;
    float best = 18.0f;
    for (int which : { 0, 1 })
        if (const auto c = handlePosition (which); ! c.isOrigin())
            if (const float d = c.getDistanceFrom (e.position); d < best)
            {
                best = d;
                dragging = which;
            }
    if (dragging == 1 && slopeParam() == nullptr)
        dragging = -1;
    if (dragging == 1)
    {
        const auto& p = *slopeParam();
        const float v = params->values[slopeIndex].load();
        const int n = p.choices.size() > 0 ? p.choices.size() : p.steps;
        dragStartPos = p.isStepped() && n > 1 ? v / (float) (n - 1) : p.toNormalised (v);
        dragStartY = e.position.y;
    }
}

void TransferView::mouseDrag (const juce::MouseEvent& e)
{
    if (params == nullptr || dragging < 0)
        return;
    const auto& spec = models::model (models::Dynamics, curveModel);
    const int numParams = (int) spec.params.size();
    if (dragging == 0 && juce::isPositiveAndBelow (thrIndex, numParams))
    {
        const auto& p = spec.params[(size_t) thrIndex];
        params->values[thrIndex] = juce::jlimit (p.min, p.max, dbAtX (e.position.x));
    }
    else if (dragging == 1 && juce::isPositiveAndBelow (slopeIndex, numParams))
    {
        // Up: less (a lower ratio, a smaller range).
        const auto& p = spec.params[(size_t) slopeIndex];
        const float pos = juce::jlimit (0.0f, 1.0f, dragStartPos + (e.position.y - dragStartY) / 160.0f);
        const int n = p.choices.size() > 0 ? p.choices.size() : p.steps;
        params->values[slopeIndex] = p.isStepped() && n > 1 ? (float) juce::roundToInt (pos * (float) (n - 1)) : p.fromNormalised (pos);
    }
    refresh();
    if (onEdited)
        onEdited();
}

void TransferView::mouseUp (const juce::MouseEvent&) { dragging = -1; }

void TransferView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme.well);
    g.fillRoundedRectangle (bounds, sizes::kRadius);
    const auto area = plotArea();

    g.setColour (theme.outline);
    for (float db : { -40.0f, -20.0f })
    {
        g.fillRect (xAt (db), area.getY(), 1.0f, area.getHeight());
        g.fillRect (area.getX(), yAt (db), area.getWidth(), 1.0f);
    }
    // 1:1, dashed.
    juce::Path unity;
    unity.startNewSubPath (xAt (-60.0f), yAt (-60.0f));
    unity.lineTo (xAt (0.0f), yAt (0.0f));
    juce::Path dashed;
    const float dashes[] = { 3.0f, 4.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, unity, dashes, 2);
    g.fillPath (dashed);

    if (! outDb.empty())
    {
        juce::Path p;
        for (size_t i = 0; i < inDb.size(); ++i)
        {
            const juce::Point<float> pt (xAt (inDb[i]), yAt (outDb[i]));
            if (i == 0)
                p.startNewSubPath (pt);
            else
                p.lineTo (pt);
        }
        g.setColour (theme.accent);
        g.strokePath (p, juce::PathStrokeType (2.0f));
    }

    for (int which : { 0, 1 })
        if (const auto c = handlePosition (which); ! c.isOrigin())
        {
            const auto ring = juce::Rectangle<float> (16.0f, 16.0f).withCentre (c);
            g.setColour (theme.accent.withAlpha (which == dragging ? 0.9f : 0.25f));
            g.fillEllipse (ring);
            g.setColour (theme.accent);
            g.drawEllipse (ring, 1.5f);
        }

    // The gain reduction, down from the top.
    auto bar = bounds.withLeft (area.getRight() + 3.0f).reduced (0.0f, 2.0f);
    g.setColour (theme.outline);
    g.fillRect (bar);
    g.setColour (theme.meterMid);
    g.fillRect (bar.withHeight (bar.getHeight() * juce::jlimit (0.0f, 1.0f, -reduction / 30.0f)));
}

//==============================================================================
PictureGrid::PictureGrid (const Theme& t, juce::StringArray n, int c, std::function<void (juce::Graphics&, juce::Rectangle<float>, int)> p,
                          std::function<void (int)> done)
    : theme (t), names (std::move (n)), current (c), paintItem (std::move (p)), chosen (std::move (done))
{
    const int rows = (names.size() + kColumns - 1) / kColumns;
    setSize (kColumns * kTileWidth, juce::jmax (1, rows) * kTileHeight);
}

juce::Rectangle<int> PictureGrid::tileBounds (int index) const
{
    return { (index % kColumns) * kTileWidth, (index / kColumns) * kTileHeight, kTileWidth, kTileHeight };
}

void PictureGrid::paint (juce::Graphics& g)
{
    g.fillAll (theme.panel);
    for (int i = 0; i < names.size(); ++i)
    {
        auto tile = tileBounds (i).toFloat().reduced (4.0f);
        if (! g.clipRegionIntersects (tile.toNearestInt()))
            continue;
        g.setColour (theme.well);
        g.fillRoundedRectangle (tile, sizes::kRadius);
        auto caption = tile.removeFromBottom (22.0f);
        if (paintItem)
            paintItem (g, tile.reduced (4.0f), i);
        g.setColour (i == current ? theme.accent : theme.text);
        g.setFont (type::font (type::kCaption, true));
        g.drawFittedText (names[i].toUpperCase(), caption.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.7f);
        if (i == current)
        {
            g.setColour (theme.accent);
            g.drawRoundedRectangle (tileBounds (i).toFloat().reduced (3.0f), sizes::kRadius, 2.0f);
        }
    }
}

void PictureGrid::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown())
        return;
    for (int i = 0; i < names.size(); ++i)
        if (tileBounds (i).contains (e.getPosition()))
        {
            auto done = chosen;
            if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                box->dismiss();
            if (done)
                done (i);
            return;
        }
}

//==============================================================================
ParamPanel::ParamPanel (const Theme& t) : theme (t) {}

void ParamPanel::bind (models::Table t, std::atomic<int32_t>* m, std::atomic<float>* v, int n)
{
    table = t;
    model = m;
    values = v;
    numValues = n;
    builtModel = -2;
    refresh();
}

juce::Component* ParamPanel::getControl (int index) const noexcept
{
    return juce::isPositiveAndBelow (index, (int) controls.size()) ? controls[(size_t) index].get() : nullptr;
}

int ParamPanel::getHeightFor (int) const noexcept
{
    return (int) controls.size() * (kRowHeight + sizes::kGapSmall);
}

void ParamPanel::refresh()
{
    const int m = model != nullptr && values != nullptr ? model->load() : -1;
    if (m != builtModel)
    {
        builtModel = m;
        rebuild();
    }
    for (auto& r : refreshers)
        r();
}

void ParamPanel::rebuild()
{
    controls.clear();
    refreshers.clear();
    if (builtModel >= 0)
    {
        const auto& spec = models::model (table, builtModel);
        for (const auto& p : spec.params)
        {
            if (p.index >= numValues)
                break;
            auto& v = values[p.index];
            if (p.kind == models::ParamKind::Toggle)
            {
                auto key = std::make_unique<ToggleKey> (p.label.toUpperCase(), theme.accent, theme);
                key->isOn = [&v] { return v.load() > 0.5f; };
                key->onClick = [&v] { v.store (v.load() > 0.5f ? 0.0f : 1.0f); };
                refreshers.push_back ([k = key.get()] { k->refresh(); });
                controls.push_back (std::move (key));
            }
            else if (p.kind == models::ParamKind::Choice)
            {
                auto choice = std::make_unique<ChoiceBar> (theme);
                choice->caption = p.label.toUpperCase();
                choice->items = [choices = p.choices] { return choices; };
                choice->current = [&v] { return juce::roundToInt (v.load()); };
                choice->choose = [&v] (int i) { v.store ((float) i); };
                refreshers.push_back ([c = choice.get()] { c->refresh(); });
                controls.push_back (std::move (choice));
            }
            else
            {
                auto bar = std::make_unique<ValueBar> (theme);
                bar->bind (ValueBar::param (v, p));
                refreshers.push_back ([b = bar.get()] { b->refresh(); });
                controls.push_back (std::move (bar));
            }
            addAndMakeVisible (*controls.back());
        }
    }
    resized();
}

void ParamPanel::resized()
{
    std::vector<std::pair<juce::Component*, int>> rows;
    for (auto& c : controls)
        rows.push_back ({ c.get(), kRowHeight });
    layoutRows (getLocalBounds(), rows, sizes::kGapSmall);
}

int layoutRows (juce::Rectangle<int> area, const std::vector<std::pair<juce::Component*, int>>& rows, int gap)
{
    int y = area.getY();
    for (auto [c, h] : rows)
    {
        if (c != nullptr)
            c->setBounds (area.getX(), y, area.getWidth(), h);
        y += h + gap;
    }
    return y - area.getY();
}

} // namespace livedaw::ui::universal
