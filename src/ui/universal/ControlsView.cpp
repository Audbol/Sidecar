#include "ui/universal/ControlsView.h"

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kTitle = 40, kEncoder = 64, kKey = 72, kMaxColumn = 300;
    /** The layers offered: the upper part's 16, then USER. */
    constexpr int kNumShownLayers = cc::kNumLayers + 1;

    int layerOfKey (int key) noexcept
    {
        return key < cc::kNumLayers ? key : cc::kUserLayer;
    }
} // namespace

//==============================================================================
ControlsView::Encoder::Encoder (const Theme& t) : theme (t)
{
    setViewportIgnoreDragFlag (true);
}

void ControlsView::Encoder::setBinding (KnobBinding b)
{
    binding = std::move (b);
    setEnabled (binding.isValid());
    refresh();
    repaint();
}

void ControlsView::Encoder::setPositionFromUser (float newPosition)
{
    if (! binding.isValid() || ! binding.set)
        return;
    binding.set (juce::jlimit (0.0f, 1.0f, newPosition));
    refresh();
}

void ControlsView::Encoder::refresh()
{
    const float p = binding.isValid() ? juce::jlimit (0.0f, 1.0f, binding.get()) : 0.0f;
    const auto t = binding.isValid() && binding.text ? binding.text() : juce::String();
    if (std::abs (p - position) > 1.0e-3f || t != shownText)
    {
        position = p;
        shownText = t;
        repaint();
    }
}

void ControlsView::Encoder::paint (juce::Graphics& g)
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
    g.setColour (isEnabled() ? theme.text : theme.textDim);
    g.setFont (type::font (type::kHeading, true));
    g.drawFittedText (isEnabled() ? shownText : juce::String::fromUTF8 ("\xe2\x80\x94"), text.toNearestInt(), juce::Justification::centred, 1, 0.8f);
}

void ControlsView::Encoder::mouseDown (const juce::MouseEvent&)
{
    dragStart = position;
}

void ControlsView::Encoder::mouseDrag (const juce::MouseEvent& e)
{
    // As far as the touch travels, sideways or up (a quarter of that with Shift).
    const auto travel = (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / juce::jmax (1.0f, (float) getWidth());
    setPositionFromUser (dragStart + travel * (e.mods.isShiftDown() ? 0.25f : 1.0f));
}

void ControlsView::Encoder::mouseDoubleClick (const juce::MouseEvent&)
{
    if (binding.reset)
    {
        binding.reset();
        refresh();
    }
}

void ControlsView::Encoder::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (binding.isValid() && wheel.deltaY != 0.0f)
        setPositionFromUser (position + (wheel.deltaY > 0.0f ? 1.0f : -1.0f) / (binding.steps > 0.0f ? binding.steps : 100.0f));
}

//==============================================================================
ControlsView::Column::Column (UiContext& c, const Theme& t, int i)
    : encoder (t), upper ("--", t.accent, t), lower ("--", t.accent, t), ctx (c), theme (t), index (i)
{
    bindKey (upper, cc::Slot::Upper);
    bindKey (lower, cc::Slot::Lower);
    for (auto* child : std::initializer_list<juce::Component*> { &encoder, &upper, &lower })
        addAndMakeVisible (child);
}

void ControlsView::Column::bindKey (ToggleKey& key, cc::Slot slot)
{
    const size_t side = slot == cc::Slot::Upper ? 0 : 1;
    key.isOn = [this, slot] { return control.slot (slot).isAssigned() && cc::isLit (ctx, control.slot (slot)); };
    // Pressed and released as on the console: some functions act while held.
    key.onStateChange = [this, &key, slot, side]
    {
        const bool down = key.isDown();
        if (down == held[side])
            return;
        held[side] = down;
        const auto f = control.slot (slot);
        if (! f.isAssigned())
            return;
        if (down)
            cc::press (ctx, f);
        else
            cc::release (ctx, f);
    };
}

void ControlsView::Column::bind (int layer)
{
    control = ctx.customControls != nullptr ? ctx.customControls->get (layer, index) : cc::Control {};
    hasEncoder = ! cc::isLowerLayer (layer);
    hasLower = cc::hasLowerKeys (layer);
    encoder.setBinding (hasEncoder ? cc::bindEncoder (ctx, control.encoder) : KnobBinding {});
    encoder.setVisible (hasEncoder);
    lower.setVisible (hasLower);
    const auto title = [this] (cc::Slot slot)
    {
        const auto& f = control.slot (slot);
        if (! f.isAssigned())
            return juce::String::fromUTF8 ("\xe2\x80\x94");
        const auto& name = control.nameOf (slot, hasEncoder);
        return name.isNotEmpty() ? name : cc::describe (ctx, slot, f);
    };
    upper.setButtonText (title (cc::Slot::Upper));
    lower.setButtonText (title (cc::Slot::Lower));
    upper.setEnabled (control.upper.isAssigned());
    lower.setEnabled (control.lower.isAssigned());
    resized();
    repaint();
}

void ControlsView::Column::refresh()
{
    encoder.refresh();
    upper.refresh();
    lower.refresh();
}

void ControlsView::Column::paint (juce::Graphics& g)
{
    g.setColour (theme.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), sizes::kRadius);
    auto title = getLocalBounds().removeFromTop (kTitle).reduced (sizes::kGapLarge, 0);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption, true));
    g.drawText (juce::String (index + 1), title.removeFromLeft (24), juce::Justification::centredLeft);
    if (hasEncoder)
    {
        const auto name = control.name.isNotEmpty() ? control.name : (control.encoder.isAssigned() ? cc::describe (ctx, cc::Slot::Encoder, control.encoder) : juce::String());
        g.setColour (theme.text);
        g.setFont (type::font (type::kLabel, true));
        g.drawFittedText (name, title, juce::Justification::centredLeft, 1, 0.75f);
    }
}

void ControlsView::Column::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap, 0);
    area.removeFromTop (kTitle);
    if (hasEncoder)
    {
        encoder.setBounds (area.removeFromTop (kEncoder));
        area.removeFromTop (sizes::kGap);
    }
    upper.setBounds (area.removeFromTop (kKey));
    area.removeFromTop (sizes::kGap);
    if (hasLower)
        lower.setBounds (area.removeFromTop (kKey));
}

//==============================================================================
ControlsView::ControlsView (UiContext& c, const Theme& t) : editKey ("EDIT", t.accent, t), ctx (c), theme (t)
{
    for (int k = 0; k < kNumShownLayers; ++k)
    {
        const int target = layerOfKey (k);
        auto key = std::make_unique<ToggleKey> (cc::layerName (target), t.accent, t);
        key->isOn = [this, target] { return layer == target; };
        key->onClick = [this, target] { setLayer (target); };
        addAndMakeVisible (*key);
        layerKeys.push_back (std::move (key));
    }
    for (int i = 0; i < cc::kNumControls; ++i)
    {
        columns.push_back (std::make_unique<Column> (c, t, i));
        addAndMakeVisible (*columns.back());
    }
    editKey.device = devices::wing; // CC EDIT is the WING display's page
    editKey.isOn = [this] { return isEditorShown(); };
    editKey.onClick = [this] { showEditor (! isEditorShown()); };
    editKey.setEnabled (ctx.customControls != nullptr);
    addAndMakeVisible (editKey);
    if (ctx.customControls != nullptr)
        layer = juce::jlimit (0, cc::kNumLayers - 1, ctx.customControls->currentLayer);
    bindLayer();
    startTimerHz (15);
}

ControlsView::~ControlsView()
{
    stopTimer();
}

void ControlsView::bindLayer()
{
    shownRevision = ctx.customControls != nullptr ? ctx.customControls->getRevision() : -1;
    for (auto& column : columns)
        column->bind (layer);
    for (auto& key : layerKeys)
        key->refresh();
    repaint();
}

void ControlsView::setLayer (int newLayer)
{
    if (newLayer == layer || (newLayer != cc::kUserLayer && ! juce::isPositiveAndBelow (newLayer, cc::kNumLayers)))
        return;
    layer = newLayer;
    if (ctx.customControls != nullptr && layer < cc::kNumLayers)
    {
        ctx.customControls->currentLayer = layer;
        if (ctx.customControlsChanged)
            ctx.customControlsChanged();
    }
    bindLayer();
}

void ControlsView::showEditor (bool show)
{
    if (show == isEditorShown() || (show && ctx.customControls == nullptr))
        return;
    if (show)
    {
        editor = std::make_unique<CustomControlsPage> (ctx, *ctx.customControls, layer);
        editor->setLookAndFeel (&juce::LookAndFeel::getDefaultLookAndFeel()); // drawn in its own colours
        addAndMakeVisible (*editor);
    }
    else
    {
        editor.reset();
    }
    for (auto& column : columns)
        column->setVisible (! show);
    editKey.refresh();
    resized();
}

void ControlsView::refresh()
{
    if (ctx.customControls != nullptr)
    {
        // Moved elsewhere (the WING's section, a custom control's CC BANK key, a remote).
        if (const int current = ctx.customControls->currentLayer; layer < cc::kNumLayers && current != layer && juce::isPositiveAndBelow (current, cc::kNumLayers))
        {
            layer = current;
            bindLayer();
        }
        else if (ctx.customControls->getRevision() != shownRevision)
        {
            bindLayer();
        }
    }
    for (auto& column : columns)
        column->refresh();
    for (auto& key : layerKeys)
        key->refresh();
}

void ControlsView::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    if (ctx.customControls == nullptr)
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel));
        g.drawText ("Custom controls need a show", getLocalBounds(), juce::Justification::centred);
    }
}

void ControlsView::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap, sizes::kGapSmall);
    auto bar = area.removeFromTop (sizes::kTouch);
    editKey.setBounds (bar.removeFromRight (120));
    bar.removeFromRight (sizes::kGapLarge);
    const int n = (int) layerKeys.size();
    const int keyW = juce::jmin (84, (bar.getWidth() - (n - 1) * sizes::kGapSmall) / juce::jmax (1, n));
    for (auto& key : layerKeys)
    {
        key->setBounds (bar.removeFromLeft (keyW));
        bar.removeFromLeft (sizes::kGapSmall);
    }
    area.removeFromTop (sizes::kGap);
    if (editor != nullptr)
        editor->setBounds (area);
    const int w = juce::jmin (kMaxColumn, (area.getWidth() - (cc::kNumControls - 1) * sizes::kGap) / cc::kNumControls);
    const int h = kTitle + kEncoder + 2 * kKey + 2 * sizes::kGap + sizes::kGap;
    for (auto& column : columns)
    {
        column->setBounds (area.removeFromLeft (w).withHeight (h));
        area.removeFromLeft (sizes::kGap);
    }
}

} // namespace livedaw::ui::universal
