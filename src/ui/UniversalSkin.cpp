#include "ui/UniversalSkin.h"

namespace livedaw::ui
{

using namespace universal;

UniversalSkin::UniversalSkin (UiContext& c)
    : ctx (c), theme (Theme::dark()), wingKey ("WING SURFACE", theme.accent, theme), mixer (c, theme), inspector (c, theme)
{
    // Switching rebuilds the window's skin: let the click return first.
    wingKey.onClick = [&c = ctx]
    {
        juce::MessageManager::callAsync ([&c]
        {
            if (c.setInterface)
                c.setInterface (InterfaceKind::Wing);
        });
    };
    wingKey.setTooltip ("Back to the WING surface");
    addAndMakeVisible (wingKey);
    addAndMakeVisible (mixer);
    // Opened by a strip's selection (a tap on its name), closed by its CLOSE key.
    addChildComponent (inspector);
    inspector.onClose = [this]
    {
        inspectorOpen = false;
        resized();
    };
    refreshAll();
}

void UniversalSkin::selectionChanged()
{
    mixer.selectionChanged();
    auto* selected = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    if (selected != inspector.getStrip())
        inspector.setStrip (selected);
    inspectorOpen = selected != nullptr;
    resized();
}

void UniversalSkin::refreshAll()
{
    const auto n = ctx.engine.getCounts();
    summary = juce::String (n.channels) + " CH  " + juce::String (n.aux) + " AUX  " + juce::String (n.buses) + " BUS  "
              + juce::String (n.mains) + " MAIN  " + juce::String (n.matrices) + " MTX  " + juce::String (n.dcas) + " DCA  "
              + juce::String (n.muteGroups) + " MGRP  " + juce::String (n.fx) + " FX";
    mixer.rebuild();
    // The strips may be new ones: the selected strip again.
    inspector.setStrip (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr);
    repaint();
}

void UniversalSkin::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    auto bar = getLocalBounds().removeFromTop (sizes::kTopBar);
    g.setColour (theme.panel);
    g.fillRect (bar);
    g.setColour (theme.outline);
    g.fillRect (bar.removeFromBottom (1));

    bar.reduce (sizes::kGapLarge, 0);
    g.setColour (theme.text);
    g.setFont (type::font (type::kTitle, true));
    const int titleWidth = 200;
    g.drawText ("Universal Mixer", bar.removeFromLeft (titleWidth), juce::Justification::centredLeft);
    // PREVIEW until Phase 2 is complete.
    auto badge = bar.removeFromLeft (84).withSizeKeepingCentre (76, 24).toFloat();
    g.setColour (theme.accent);
    g.drawRoundedRectangle (badge, 4.0f, 1.5f);
    g.setFont (type::font (type::kCaption, true));
    g.drawText ("PREVIEW", badge, juce::Justification::centred);
    bar.removeFromLeft (sizes::kGapLarge);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kLabel));
    g.drawFittedText (summary, bar.withTrimmedRight (wingKey.getWidth() + sizes::kGapLarge), juce::Justification::centredLeft, 1, 0.8f);
}

void UniversalSkin::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop (sizes::kTopBar).reduced (sizes::kGapLarge, (sizes::kTopBar - sizes::kTouch) / 2);
    wingKey.setBounds (bar.removeFromRight (160));
    const bool showInspector = inspectorOpen && inspector.getStrip() != nullptr;
    inspector.setVisible (showInspector);
    if (showInspector)
        inspector.setBounds (area.removeFromRight (juce::jmin (460, area.getWidth() / 2)));
    mixer.setBounds (area);
}

} // namespace livedaw::ui
