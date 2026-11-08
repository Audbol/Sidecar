#include "ui/universal/StripPanel.h"

#include "ui/StripCustomize.h"

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kCaption = 24, kSwatch = 40, kNameRow = 56;
} // namespace

StripPanel::StripPanel (UiContext& c, const Theme& t)
    : renameKey ("RENAME", t.accent, t), dcas (t, t.accent), muteGroups (t, t.mute), ctx (c), theme (t)
{
    renameKey.onClick = [this]
    {
        if (strip == nullptr)
            return;
        const auto own = strip->getName();
        askForText (*this, "RENAME", (linked ? "The source of " : juce::String()) + strip->getLabel() + ":", own,
                    [safe = juce::Component::SafePointer<StripPanel> (this)] (const juce::String& text)
                    {
                        if (safe != nullptr)
                            safe->rename (text);
                    });
    };
    dcas.count = [this] { return strip != nullptr ? ctx.engine.getNumDcas() : 0; };
    dcas.isOn = [this] (int i) { return strip != nullptr && strip->params.dcaMask.contains (i); };
    dcas.toggle = [this] (int i)
    {
        if (strip != nullptr)
            strip->params.dcaMask.toggle (i);
    };
    dcas.name = [this] (int i) { return customGroupName (ctx.engine.getDcaName (i), i); };
    muteGroups.count = [this] { return strip != nullptr ? ctx.engine.getNumMuteGroups() : 0; };
    muteGroups.isOn = [this] (int i) { return strip != nullptr && strip->params.muteGroupMask.contains (i); };
    muteGroups.toggle = [this] (int i)
    {
        if (strip != nullptr)
            strip->params.muteGroupMask.toggle (i);
    };
    muteGroups.name = [this] (int i) { return customGroupName (ctx.engine.getMuteGroupName (i), i); };
    for (auto* child : std::initializer_list<juce::Component*> { &renameKey, &dcas, &muteGroups })
        addAndMakeVisible (child);
    startTimerHz (6);
}

void StripPanel::setStrip (Strip* s)
{
    strip = s;
    name = {};
    colour = 0;
    refresh();
    resized();
    repaint();
}

void StripPanel::rename (const juce::String& text)
{
    if (strip == nullptr)
        return;
    customize::rename (ctx.engine, *strip, text);
    refresh();
}

void StripPanel::chooseColour (int c)
{
    if (strip == nullptr)
        return;
    customize::setColour (*strip, c);
    refresh();
}

void StripPanel::refresh()
{
    dcas.refresh();
    muteGroups.refresh();
    if (strip == nullptr)
        return;
    const auto newName = strip->getName();
    const int newColour = strip->getColourIndex();
    const bool newLinked = customize::goesToSource (*strip);
    if (newName != name || newColour != colour || newLinked != linked)
    {
        name = newName;
        colour = newColour;
        linked = newLinked;
        repaint();
    }
}

juce::Rectangle<int> StripPanel::swatchArea (int c) const
{
    if (! juce::isPositiveAndBelow (c - 1, palette::count()) || coloursArea.isEmpty())
        return {};
    const int cols = juce::jmax (1, coloursArea.getWidth() / kSwatch);
    const int w = coloursArea.getWidth() / cols;
    return { coloursArea.getX() + ((c - 1) % cols) * w, coloursArea.getY() + ((c - 1) / cols) * kSwatch, w, kSwatch };
}

void StripPanel::paint (juce::Graphics& g)
{
    if (strip == nullptr)
        return;
    auto caption = [&] (juce::Rectangle<int> r, const juce::String& text)
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kCaption, true));
        g.drawText (text, r, juce::Justification::bottomLeft);
    };

    // NAME: the name shown, and whose it is while linked.
    auto n = nameArea;
    caption (n.removeFromTop (kCaption / 2 + 4), linked ? "NAME (THE SOURCE'S: LINK CUSTOMIZATION TO SOURCE)" : "NAME");
    g.setColour (theme.text);
    g.setFont (type::font (type::kValue + 3.0f, true));
    g.drawFittedText (name.isNotEmpty() ? name : strip->getLabel(), n, juce::Justification::centredLeft, 1, 0.7f);

    caption (colourCaption, "COLOUR");
    for (int c = 1; c <= palette::count(); ++c)
    {
        const auto r = swatchArea (c).toFloat().reduced (3.0f);
        g.setColour (consoleColour (c));
        g.fillRoundedRectangle (r, sizes::kRadius);
        if (c == colour)
        {
            g.setColour (theme.text);
            g.drawRoundedRectangle (r.reduced (1.0f), sizes::kRadius, 3.0f);
        }
    }
    caption (dcaCaption, "DCAS (A TAP ASSIGNS OR UNASSIGNS)");
    caption (muteCaption, "MUTE GROUPS");
}

int StripPanel::heightFor (int width) const
{
    const int w = width - 2 * sizes::kGap;
    const int cols = juce::jmax (1, w / kSwatch);
    const int rows = (palette::count() + cols - 1) / cols;
    return sizes::kGap + kNameRow + sizes::kGap + kCaption + rows * kSwatch + sizes::kGap + kCaption + dcas.heightFor (w) + sizes::kGap + kCaption
           + muteGroups.heightFor (w) + sizes::kGap;
}

void StripPanel::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap, 0);
    area.removeFromTop (sizes::kGap);
    nameArea = area.removeFromTop (kNameRow);
    renameKey.setBounds (nameArea.removeFromRight (112).withSizeKeepingCentre (112, sizes::kTouch));
    area.removeFromTop (sizes::kGap);
    colourCaption = area.removeFromTop (kCaption);
    const int cols = juce::jmax (1, area.getWidth() / kSwatch);
    const int rows = (palette::count() + cols - 1) / cols;
    coloursArea = area.removeFromTop (rows * kSwatch);
    area.removeFromTop (sizes::kGap);
    dcaCaption = area.removeFromTop (kCaption);
    dcas.setBounds (area.removeFromTop (dcas.heightFor (area.getWidth())));
    area.removeFromTop (sizes::kGap);
    muteCaption = area.removeFromTop (kCaption);
    muteGroups.setBounds (area.removeFromTop (muteGroups.heightFor (area.getWidth())));
}

void StripPanel::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown())
        return;
    for (int c = 1; c <= palette::count(); ++c)
        if (swatchArea (c).contains (e.getPosition()))
        {
            chooseColour (c);
            return;
        }
}

} // namespace livedaw::ui::universal
