#include "ui/UniversalSkin.h"

namespace livedaw::ui
{

using namespace universal;

UniversalSkin::UniversalSkin (UiContext& c)
    : ctx (c), theme (Theme::dark()),
      wingKey (c.engine.getProfile() == ld::kProfileX32        ? "X32 SURFACE"
               : c.engine.getProfile() == ld::kProfileProMix01 ? "PROMIX SURFACE"
               : c.engine.getProfile() == ld::kProfileYamaha02R ? "02R SURFACE"
                                                               : "WING SURFACE",
               theme.accent, theme), mixer (c, theme), routingView (c, theme), fxRack (c, theme),
      monitoring (c, theme), scenes (c, theme), recorder (c, theme), setup (c, theme), inspector (c, theme)
{
    const std::pair<const char*, View> views[] = { { "MIXER", View::Mixer },           { "ROUTING", View::Routing }, { "FX RACK", View::Fx },
                                                   { "MONITORING", View::Monitoring }, { "SCENES", View::Scenes },   { "RECORDER", View::Recorder },
                                                   { "SETUP", View::Setup } };
    for (auto [name, target] : views)
    {
        auto key = std::make_unique<ToggleKey> (name, theme.accent, theme);
        key->isOn = [this, v = target] { return view == v; };
        key->onClick = [this, v = target] { showView (v); };
        addAndMakeVisible (*key);
        viewKeys.push_back (std::move (key));
    }
    // Switching rebuilds the window's skin: let the click return first.
    // (An X32 console's surface is the X32's, a ProMix 01's the ProMix's.)
    const auto back = ctx.engine.getProfile() == ld::kProfileX32        ? InterfaceKind::X32
                      : ctx.engine.getProfile() == ld::kProfileProMix01 ? InterfaceKind::ProMix
                      : ctx.engine.getProfile() == ld::kProfileYamaha02R ? InterfaceKind::Yamaha02R
                                                                        : InterfaceKind::Wing;
    wingKey.onClick = [&context = ctx, back]
    {
        juce::MessageManager::callAsync ([&context, back]
        {
            if (context.setInterface)
                context.setInterface (back);
        });
    };
    wingKey.setTooltip (back == InterfaceKind::X32 ? "Back to the X32 surface" : back == InterfaceKind::ProMix ? "Back to the ProMix 01 surface" : "Back to the WING surface");
    addAndMakeVisible (wingKey);
    addAndMakeVisible (mixer);
    addChildComponent (routingView);
    addChildComponent (fxRack);
    addChildComponent (monitoring);
    addChildComponent (scenes);
    addChildComponent (recorder);
    addChildComponent (setup);
    // Opened by a strip's selection (a tap on its name), closed by its CLOSE key.
    addChildComponent (inspector);
    inspector.onClose = [this]
    {
        inspectorOpen = false;
        resized();
    };
    refreshAll();
}

void UniversalSkin::showView (View v)
{
    view = v;
    mixer.setVisible (v == View::Mixer);
    routingView.setVisible (v == View::Routing);
    fxRack.setVisible (v == View::Fx);
    monitoring.setVisible (v == View::Monitoring);
    scenes.setVisible (v == View::Scenes);
    recorder.setVisible (v == View::Recorder);
    setup.setVisible (v == View::Setup);
    for (auto& key : viewKeys)
        key->refresh();
    // The view shown takes the console at once.
    switch (v)
    {
        case View::Mixer:      mixer.refresh(); break;
        case View::Routing:    routingView.refresh(); break;
        case View::Fx:         fxRack.refresh(); break;
        case View::Monitoring: monitoring.refresh(); break;
        case View::Scenes:     scenes.refresh(); break;
        case View::Recorder:   break;
        case View::Setup:      setup.refresh(); break;
    }
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
    auto keys = area.removeFromTop (sizes::kTabBar + sizes::kGap).reduced (sizes::kGap, sizes::kGapSmall);
    const int keyWidth = juce::jmin (150, (keys.getWidth() - ((int) viewKeys.size() - 1) * sizes::kGapSmall) / juce::jmax (1, (int) viewKeys.size()));
    for (auto& key : viewKeys)
    {
        key->setBounds (keys.removeFromLeft (keyWidth));
        keys.removeFromLeft (sizes::kGapSmall);
    }
    const bool showInspector = inspectorOpen && inspector.getStrip() != nullptr;
    inspector.setVisible (showInspector);
    if (showInspector)
        inspector.setBounds (area.removeFromRight (juce::jmin (460, area.getWidth() / 2)));
    for (auto* v : std::initializer_list<juce::Component*> { &mixer, &routingView, &fxRack, &monitoring, &scenes, &recorder, &setup })
        v->setBounds (area);
}

} // namespace livedaw::ui
