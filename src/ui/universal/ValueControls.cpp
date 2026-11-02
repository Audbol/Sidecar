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
    g.setFont (type::font (type::kCaption, true));
    g.drawText (caption, text, juce::Justification::centredLeft);
    // The value, with a mark that says it opens a list.
    g.setColour (theme.text);
    g.setFont (type::font (type::kValue, true));
    g.drawText (shownText + juce::String::fromUTF8 ("  \xe2\x96\xbe"), text, juce::Justification::centredRight);
}

void ChoiceBar::mouseUp (const juce::MouseEvent& e)
{
    if (! items || e.mouseWasDraggedSinceMouseDown())
        return;
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

void ResponseView::refresh()
{
    std::vector<double> db (freqs.size(), 0.0);
    if (! response || ! response (freqs.data(), db.data(), (int) freqs.size()))
        db.clear();
    if (db != curve)
    {
        curve = std::move (db);
        repaint();
    }
}

void ResponseView::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme.well);
    g.fillRoundedRectangle (area, sizes::kRadius);
    const auto xAt = [&area] (double hz) { return area.getX() + area.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
    const auto yAt = [this, &area] (double db) { return area.getCentreY() - area.getHeight() * 0.5f * (float) juce::jlimit (-1.0, 1.0, db / rangeDb); };

    g.setColour (theme.outline);
    for (double hz : { 100.0, 1000.0, 10000.0 })
        g.fillRect (xAt (hz), area.getY(), 1.0f, area.getHeight());
    g.fillRect (area.getX(), yAt (0.0), area.getWidth(), 1.0f);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption));
    for (auto [hz, label] : { std::pair<double, const char*> { 100.0, "100" }, { 1000.0, "1k" }, { 10000.0, "10k" } })
        g.drawText (label, juce::Rectangle<float> (xAt (hz) + 3.0f, area.getBottom() - 16.0f, 40.0f, 14.0f), juce::Justification::centredLeft);

    if (curve.empty())
        return;
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
