#include "ui/UniversalSkin.h"

namespace livedaw::ui
{

using namespace universal;

namespace
{
    /** The theme the app settings name: the skin's, from its first member on. */
    const Theme& themeOf (UiContext& c)
    {
        return Theme::fromSettings (c.settings);
    }
} // namespace

UniversalSkin::UniversalSkin (UiContext& c)
    : prevSceneKey ("PREV", themeOf (c).accent, themeOf (c)), nextSceneKey ("GO NEXT", themeOf (c).accent, themeOf (c)), clearSoloKey ("CLR SOLO", themeOf (c).solo, themeOf (c)),
      dimKey ("DIM", themeOf (c).solo, themeOf (c)), talkAKey ("TALK A", themeOf (c).solo, themeOf (c)), talkBKey ("TALK B", themeOf (c).solo, themeOf (c)),
      ctx (c), theme (themeOf (c)),
      wingKey (c.engine.getProfile() == ld::kProfileXenyx1204 ? "1204USB SURFACE" : c.engine.getProfile() == ld::kProfileMidasPro2 ? "PRO2 SURFACE" : c.engine.getProfile() == ld::kProfileDm2000 ? "DM2000 SURFACE" : c.engine.getProfile() == ld::kProfileX32        ? "X32 SURFACE"
               : c.engine.getProfile() == ld::kProfileProMix01 ? "PROMIX SURFACE"
               : c.engine.getProfile() == ld::kProfileYamaha02R ? "02R SURFACE"
               : c.engine.getProfile() == ld::kProfileSq5       ? "SQ-5 SURFACE"
               : c.engine.getProfile() == ld::kProfileYamaha01V ? "01V SURFACE"
               : c.engine.getProfile() == ld::kProfileDLive     ? "DLIVE SURFACE"
                                                               : "WING SURFACE",
               theme.accent, theme), showKey ("SHOW", theme.accent, theme), menuKey ("MENU", theme.accent, theme), mixer (c, theme), routingView (c, theme), fxRack (c, theme),
      monitoring (c, theme), scenes (c, theme), library (c, theme), recorder (c, theme), setup (c, theme), controls (c, theme), inspector (c, theme), bridge (c, theme), history (c.engine)
{
    setLookAndFeel (&universal::lookAndFeelFor (theme));
    addChildComponent (bridge);
    const std::pair<const char*, View> views[] = { { "MIXER", View::Mixer },           { "ROUTING", View::Routing }, { "FX RACK", View::Fx },
                                                   { "MONITORING", View::Monitoring }, { "SCENES", View::Scenes },   { "LIBRARY", View::Library },
                                                   { "RECORDER", View::Recorder },     { "CONTROLS", View::Controls },
                                                   { "SETUP", View::Setup } };
    for (auto [name, target] : views)
    {
        auto key = std::make_unique<ToggleKey> (name, theme.accent, theme);
        key->tab = true;
        key->isOn = [this, v = target] { return view == v; };
        key->onClick = [this, v = target] { showView (v); };
        addAndMakeVisible (*key);
        viewKeys.push_back (std::move (key));
    }
    // Switching rebuilds the window's skin: let the click return first.
    // (An X32 console's surface is the X32's, a ProMix 01's the ProMix's.)
    const auto back = ctx.engine.getProfile() == ld::kProfileMidasPro2 ? InterfaceKind::Pro2
                      : ctx.engine.getProfile() == ld::kProfileX32        ? InterfaceKind::X32
                      : ctx.engine.getProfile() == ld::kProfileProMix01 ? InterfaceKind::ProMix
                      : ctx.engine.getProfile() == ld::kProfileYamaha02R ? InterfaceKind::Yamaha02R
                      : ctx.engine.getProfile() == ld::kProfileSq5      ? InterfaceKind::Sq5
                      : ctx.engine.getProfile() == ld::kProfileYamaha01V ? InterfaceKind::Yamaha01V
                      : ctx.engine.getProfile() == ld::kProfileDLive    ? InterfaceKind::DLive
                      : ctx.engine.getProfile() == ld::kProfileDm2000   ? InterfaceKind::Dm2000
                      : ctx.engine.getProfile() == ld::kProfileXenyx1204 ? InterfaceKind::Xenyx1204
                                                                        : InterfaceKind::Wing;
    wingKey.onClick = [&context = ctx, back]
    {
        juce::MessageManager::callAsync ([&context, back]
        {
            if (context.setInterface)
                context.setInterface (back);
        });
    };
    wingKey.setTooltip (back == InterfaceKind::Xenyx1204 ? "Back to the XENYX 1204USB surface" : back == InterfaceKind::Pro2 ? "Back to the PRO2 surface" : back == InterfaceKind::Dm2000 ? "Back to the DM2000 surface" : back == InterfaceKind::X32 ? "Back to the X32 surface" : back == InterfaceKind::ProMix ? "Back to the ProMix 01 surface" : back == InterfaceKind::Yamaha02R ? "Back to the 02R surface" : back == InterfaceKind::Sq5 ? "Back to the SQ-5 surface" : back == InterfaceKind::Yamaha01V ? "Back to the 01V surface" : back == InterfaceKind::DLive ? "Back to the dLive surface" : "Back to the WING surface");
    addChildComponent (wingKey);
    wingKey.setVisible (true);
    // The show: NEW, OPEN, SAVE and SAVE AS (the shell's, as the WING's LIBRARY uses them).
    showKey.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&universal::lookAndFeelFor (theme));
        menu.addSectionHeader (getShowTitle());
        menu.addItem (1, "New show");
        menu.addItem (2, "Open...");
        menu.addItem (3, "Save");
        menu.addItem (4, "Save as...");
        juce::Component::SafePointer<UniversalSkin> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&showKey), [safe] (int item)
        {
            if (safe != nullptr && item != 0)
                safe->runShowCommand (item);
        });
    };
    showKey.setTooltip ("The show: new, open, save");
    // MENU: the theme, the views, and what a narrow window has no key for.
    menuKey.onClick = [this]
    {
        juce::Component::SafePointer<UniversalSkin> safe (this);
        buildMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuKey).withStandardItemHeight (sizes::kTouch - 8), [safe] (int item)
        {
            if (safe != nullptr && item != 0)
                safe->runMenuCommand (item);
        });
    };
    menuKey.setTooltip ("Theme, views, show and scene commands");
    addAndMakeVisible (menuKey);
    startTimerHz (5);
    addAndMakeVisible (showKey);
    addAndMakeVisible (mixer);
    addChildComponent (routingView);
    addChildComponent (fxRack);
    addChildComponent (monitoring);
    addChildComponent (scenes);
    addChildComponent (library);
    addChildComponent (recorder);
    addChildComponent (setup);
    addChildComponent (controls);
    // What a show needs at any moment.
    prevSceneKey.onClick = [this] { logRecall (ctx, "the previous scene", ctx.show.recallPrevious()); };
    nextSceneKey.onClick = [this] { logRecall (ctx, "the next scene", ctx.show.recallNext()); };
    prevSceneKey.setTooltip ("Recall the previous scene");
    nextSceneKey.setTooltip ("Recall the next scene");
    clearSoloKey.isOn = [&engine = ctx.engine] { return engine.anySoloActive.load(); };
    clearSoloKey.onClick = [this] { monitoring.clearAllSolos(); };
    dimKey.isOn = [&engine = ctx.engine] { return engine.monitorDim.load(); };
    dimKey.onClick = [&engine = ctx.engine] { engine.monitorDim = ! engine.monitorDim.load(); };
    for (int path = 0; path < 2; ++path)
    {
        auto& key = path == 0 ? talkAKey : talkBKey;
        key.isOn = [&engine = ctx.engine, path] { return engine.isTalkOn (path); };
        // Pressed and released as on the console (the path's mode decides, MONITORING sets it).
        key.onStateChange = [this, &key, path]
        {
            const bool down = key.isDown();
            if (down != talkHeld[path])
            {
                talkHeld[path] = down;
                ctx.engine.talkKey (path, down);
            }
        };
    }
    for (auto* key : { &prevSceneKey, &nextSceneKey, &clearSoloKey, &dimKey, &talkAKey, &talkBKey })
        addAndMakeVisible (key);
    // A tap on a strip's overview or the channel panel: that block in the inspector.
    mixer.onOpenStep = [this] (Strip& strip, int stepKind, int uid)
    {
        inspector.setStrip (&strip);
        inspector.openStep (stepKind, uid);
        inspectorOpen = true;
        resized();
    };
    // Opened by a strip's selection (a tap on its name), closed by its CLOSE key.
    addChildComponent (inspector);
    inspector.onClose = [this]
    {
        inspectorOpen = false;
        resized();
    };
    refreshAll();
}

void UniversalSkin::setLightTheme (bool light)
{
    ctx.settings.setValue (Theme::kSetting, light ? "light" : "dark");
    if (light == theme.isLight || ! ctx.setInterface)
        return;
    // The views took their theme when they were made: the window makes the skin again. It only does
    // so for another interface than the one it shows, so it is told it shows none of ours first.
    // (After the click has returned: this skin goes.)
    juce::MessageManager::callAsync ([&context = ctx]
    {
        if (context.setInterface && context.interfaceKind == InterfaceKind::Universal)
        {
            context.interfaceKind = InterfaceKind::Wing;
            context.setInterface (InterfaceKind::Universal);
        }
    });
}

juce::PopupMenu UniversalSkin::buildMenu()
{
    juce::PopupMenu menu, themes, views, showMenu;
    menu.setLookAndFeel (&universal::lookAndFeelFor (theme));
    menu.addItem (kMenuUndo, "Undo mix change   Ctrl+Z", history.canUndo());
    menu.addItem (kMenuRedo, "Redo mix change   Ctrl+Y", history.canRedo());
    menu.addSeparator();
    themes.addItem (kMenuThemeDark, "Dark", true, ! theme.isLight);
    themes.addItem (kMenuThemeLight, "Light", true, theme.isLight);
    menu.addSubMenu ("Theme", themes);
    const std::pair<const char*, View> names[] = { { "Mixer", View::Mixer },           { "Routing", View::Routing }, { "FX rack", View::Fx },
                                                   { "Monitoring", View::Monitoring }, { "Scenes", View::Scenes },   { "Library", View::Library },
                                                   { "Recorder", View::Recorder },     { "Controls", View::Controls }, { "Setup", View::Setup } };
    for (const auto& [name, v] : names)
        views.addItem (kMenuView + (int) v, name, true, view == v);
    menu.addSubMenu ("View", views);
    menu.addItem (kMenuJump, "Jump to a strip...   Ctrl+J");
    menu.addItem (kMenuShortcuts, "Keyboard shortcuts...");
    juce::PopupMenu width;
    width.addItem (kMenuStripWidth, "Narrow", true, mixer.getStripLayout().widthMode == 0);
    width.addItem (kMenuStripWidth + 1, "Normal", true, mixer.getStripLayout().widthMode == 1);
    width.addItem (kMenuStripWidth + 2, "Wide", true, mixer.getStripLayout().widthMode == 2);
    menu.addSubMenu ("Strip width", width);
    menu.addItem (kMenuResetStrips, "Reset the strips' sections");
    menu.addSeparator();
    showMenu.addItem (1, "New show");
    showMenu.addItem (2, "Open...");
    showMenu.addItem (3, "Save");
    showMenu.addItem (4, "Save as...");
    menu.addSubMenu ("Show: " + getShowTitle(), showMenu);
    menu.addSeparator();
    // What the view row has keys for only on a wide window.
    const bool anyScene = ctx.show.getNumScenes() > 0;
    menu.addItem (kMenuPreviousScene, "Previous scene", anyScene);
    menu.addItem (kMenuNextScene, "Next scene", anyScene);
    menu.addItem (kMenuClearSolo, "Clear solo", ctx.engine.anySoloActive.load());
    menu.addItem (kMenuDim, "Dim the monitors", true, ctx.engine.monitorDim.load());
    if (wingKey.isVisible())
    {
        menu.addSeparator();
        menu.addItem (kMenuSurface, wingKey.getTooltip());
    }
    return menu;
}

void UniversalSkin::runMenuCommand (int item)
{
    if (item >= 1 && item <= 4)
        runShowCommand (item);
    else if (item == kMenuThemeDark || item == kMenuThemeLight)
        setLightTheme (item == kMenuThemeLight);
    else if (item >= kMenuView && item <= kMenuView + (int) View::Controls)
        showView (static_cast<View> (item - kMenuView));
    else if (item == kMenuPreviousScene)
        logRecall (ctx, "the previous scene", ctx.show.recallPrevious());
    else if (item == kMenuNextScene)
        logRecall (ctx, "the next scene", ctx.show.recallNext());
    else if (item == kMenuClearSolo)
        monitoring.clearAllSolos();
    else if (item == kMenuDim)
        ctx.engine.monitorDim = ! ctx.engine.monitorDim.load();
    else if (item == kMenuJump)
    {
        showView (View::Mixer);
        mixer.setJumpListShown (true);
    }
    else if (item == kMenuUndo)
        history.undo();
    else if (item == kMenuRedo)
        history.redo();
    else if (item >= kMenuStripWidth && item < kMenuStripWidth + 3)
    {
        mixer.getStripLayout().widthMode = item - kMenuStripWidth;
        mixer.applyStripLayout();
    }
    else if (item == kMenuResetStrips)
    {
        mixer.getStripLayout().reset();
        mixer.applyStripLayout();
    }
    else if (item == kMenuShortcuts)
        showShortcuts();
    else if (item == kMenuSurface && wingKey.isVisible())
        wingKey.triggerClick();
}

bool UniversalSkin::skinKeyPressed (const juce::KeyPress& key)
{
    // Ctrl+J from any view: the MIXER view's JUMP list. (With Ctrl held, X11 gives a control code
    // as the text and the lowercase key as the code.)
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    const auto k = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getKeyCode());
    if (key.getModifiers().isCommandDown() && (c == 'j' || k == 'j'))
    {
        showView (View::Mixer);
        mixer.setJumpListShown (true);
        return true;
    }
    if (key.getModifiers().isCommandDown() && (c == 'z' || k == 'z' || c == 'y' || k == 'y'))
    {
        const bool redo = c == 'y' || k == 'y' || key.getModifiers().isShiftDown();
        return redo ? history.redo() : history.undo();
    }
    // F1 to F9: the views, in their order on the row.
    if (const int code = key.getKeyCode(); code >= juce::KeyPress::F1Key && code <= juce::KeyPress::F9Key && ! key.getModifiers().isAnyModifierKeyDown())
    {
        static constexpr View order[] = { View::Mixer, View::Routing, View::Fx, View::Monitoring, View::Scenes, View::Library, View::Recorder, View::Controls, View::Setup };
        showView (order[code - juce::KeyPress::F1Key]);
        return true;
    }
    return view == View::Mixer && mixer.handleKey (key);
}

void UniversalSkin::showShortcuts()
{
    const juce::StringArray lines { "Ctrl+Z  /  Ctrl+Y (or Ctrl+Shift+Z)      Undo / redo a mix change",
                                    "F1 to F9      The views, in their order",
                                    "Ctrl+J  or  /      Jump to a strip",
                                    "Left  /  Right      Select the previous / next strip",
                                    "Page Up  /  Page Down      Previous / next bank",
                                    "Home  /  End      First / last bank",
                                    "",
                                    "Double-tap a fader or pan      Back to 0 dB / centre",
                                    "Shift while dragging      Fine adjustment" };
    auto* window = new juce::AlertWindow ("KEYBOARD SHORTCUTS", lines.joinIntoString ("\n"), juce::MessageBoxIconType::NoIcon, this);
    window->setLookAndFeel (&universal::lookAndFeelFor (theme));
    window->addButton ("CLOSE", 1, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, nullptr, true);
}

void UniversalSkin::showView (View v)
{
    view = v;
    if (v != View::Mixer)
        mixer.setJumpListShown (false);
    mixer.setVisible (v == View::Mixer);
    routingView.setVisible (v == View::Routing);
    fxRack.setVisible (v == View::Fx);
    monitoring.setVisible (v == View::Monitoring);
    scenes.setVisible (v == View::Scenes);
    library.setVisible (v == View::Library);
    recorder.setVisible (v == View::Recorder);
    setup.setVisible (v == View::Setup);
    controls.setVisible (v == View::Controls);
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
        case View::Library:    library.refresh(); break;
        case View::Recorder:   break;
        case View::Setup:      setup.refresh(); break;
        case View::Controls:   controls.refresh(); break;
    }
}

juce::String UniversalSkin::getSceneReadout() const
{
    const int current = ctx.show.getCurrentSceneIndex();
    const auto scene = current >= 0 ? ctx.show.getScene (current) : std::nullopt;
    return scene.has_value() ? juce::String (current + 1) + "   " + scene->name : juce::String();
}

void UniversalSkin::timerCallback()
{
    if (! isVisible() || isCovered())
        return;
    history.tick (juce::Time::getMillisecondCounter());
    for (auto* key : { &clearSoloKey, &dimKey, &talkAKey, &talkBKey })
        key->refresh();
    const auto scene = getSceneReadout();
    if (scene != shownScene)
    {
        shownScene = scene;
        repaint (sceneArea);
    }
    const auto status = "DSP " + juce::String (juce::roundToInt (ctx.engine.getDspLoad() * 100.0f)) + " %    " + juce::Time::getCurrentTime().toString (false, true, false, true);
    if (status != shownStatus)
    {
        shownStatus = status;
        repaint (statusArea);
    }
}

juce::String UniversalSkin::getShowTitle() const
{
    const auto file = ctx.getShowFile ? ctx.getShowFile() : juce::File();
    return file != juce::File() ? file.getFileNameWithoutExtension() : juce::String ("UNTITLED SHOW");
}

void UniversalSkin::runShowCommand (int item)
{
    switch (item)
    {
        case 1: if (ctx.newShow) ctx.newShow(); break;
        case 2: if (ctx.openShow) ctx.openShow(); break;
        case 3: if (ctx.saveShow) ctx.saveShow (false); break;
        case 4: if (ctx.saveShow) ctx.saveShow (true); break;
        default: break;
    }
    repaint();
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
    summaryShort = juce::String (n.channels) + " CH  " + juce::String (n.aux) + " AUX  " + juce::String (n.buses) + " BUS  "
                   + juce::String (n.mains) + " MAIN  " + juce::String (n.matrices) + " MTX";
    summary = summaryShort + "  " + juce::String (n.dcas) + " DCA  " + juce::String (n.muteGroups) + " MGRP  " + juce::String (n.fx) + " FX";
    mixer.rebuild();
    // The strips may be new ones: the selected strip again.
    inspector.setStrip (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr);
    repaint();
}

void UniversalSkin::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    auto bar = getLocalBounds().removeFromTop (sizes::kTopBar);
    universal::drawPlate (g, bar.toFloat(), theme.hw.plateTop, theme.hw.plateBottom, theme);
    g.setColour (theme.outline);
    g.fillRect (bar.removeFromBottom (1));

    bar.reduce (sizes::kGapLarge, 0);
    g.setColour (theme.text);
    g.setFont (type::font (type::kTitle, true));
    const int titleWidth = 200;
    g.drawText ("Universal Mixer", bar.removeFromLeft (titleWidth), juce::Justification::centredLeft);
    // The current scene, beside the keys that step through the show.
    if (! sceneArea.isEmpty())
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kCaption, true));
        auto readout = sceneArea;
        g.drawText ("SCENE", readout.removeFromLeft (52), juce::Justification::centredLeft);
        g.setColour (shownScene.isNotEmpty() ? theme.text : theme.textDim);
        g.setFont (type::font (type::kLabel, true));
        g.drawFittedText (shownScene.isNotEmpty() ? shownScene : juce::String::fromUTF8 ("\xe2\x80\x94"), readout, juce::Justification::centredLeft, 1, 0.75f);
    }
    // PREVIEW until Phase 2 is complete.
    auto badge = bar.removeFromLeft (84).withSizeKeepingCentre (76, 24).toFloat();
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption, true));
    g.drawText ("PREVIEW", badge, juce::Justification::centredLeft);
    bar.removeFromLeft (sizes::kGapLarge);
    if (bridge.isVisible())
        bar.removeFromLeft (bridge.getWidth() + sizes::kGapLarge);
    // The show's name left of its SHOW key; the console's counts between.
    bar.removeFromRight ((wingKey.isVisible() ? wingKey.getWidth() + sizes::kGapLarge : 0) + showKey.getWidth() + sizes::kGapLarge + menuKey.getWidth() + sizes::kGapSmall);
    auto showName = bar.removeFromRight (juce::jmin (320, bar.getWidth() / 3));
    // DSP load and the time, left of the show's name, where there's room.
    if (statusArea = bar.getWidth() > 760 ? bar.removeFromRight (170) : juce::Rectangle<int>(); ! statusArea.isEmpty())
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel));
        g.drawText (shownStatus, statusArea, juce::Justification::centredRight);
    }
    g.setColour (theme.text);
    g.setFont (type::font (type::kValue, true));
    g.drawFittedText (getShowTitle(), showName, juce::Justification::centredRight, 1, 0.7f);
    g.setColour (theme.textDim);
    const auto countsFont = type::font (type::kLabel);
    g.setFont (countsFont);
    // The counts in full where they fit (squeezed a little at most), else the strips' alone.
    const auto countsArea = bar.withTrimmedRight (sizes::kGapLarge);
    const bool full = juce::GlyphArrangement::getStringWidth (countsFont, summary) * 0.8f <= (float) countsArea.getWidth();
    g.drawFittedText (full ? summary : summaryShort, countsArea, juce::Justification::centredLeft, 1, 0.8f);
}

void UniversalSkin::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop (sizes::kTopBar).reduced (sizes::kGapLarge, (sizes::kTopBar - sizes::kTouch) / 2);
    menuKey.setBounds (bar.removeFromRight (84));
    bar.removeFromRight (sizes::kGapSmall);
    if (wingKey.isVisible())
    {
        wingKey.setBounds (bar.removeFromRight (160));
        bar.removeFromRight (sizes::kGapLarge);
    }
    showKey.setBounds (bar.removeFromRight (112));
    // The main output, where the bar has room for it beside the counts.
    bridge.setVisible (getWidth() >= 1500);
    bridge.setBounds (sizes::kGapLarge + 200 + 84 + sizes::kGapLarge, (sizes::kTopBar - 28) / 2, 220, 28);
    auto keys = area.removeFromTop (sizes::kTabBar + sizes::kGap).reduced (sizes::kGap, sizes::kGapSmall);
    // The show's keys at the right: all of them on a wide window, fewer on a narrower one, none
    // at the smallest, where the view keys need the row.
    const int width = keys.getWidth();
    const bool talk = width >= 1760, quick = width >= 1400;
    for (auto* key : { &dimKey, &talkAKey, &talkBKey })
        key->setVisible (talk);
    for (auto* key : { &prevSceneKey, &nextSceneKey, &clearSoloKey })
        key->setVisible (quick);
    sceneArea = {};
    if (quick)
    {
        const auto fromRight = [&keys] (juce::Component& key, int w)
        {
            key.setBounds (keys.removeFromRight (w));
            keys.removeFromRight (sizes::kGapSmall);
        };
        if (talk)
        {
            fromRight (talkBKey, 72);
            fromRight (talkAKey, 72);
            fromRight (dimKey, 64);
        }
        fromRight (clearSoloKey, 104);
        keys.removeFromRight (sizes::kGap);
        fromRight (nextSceneKey, 96);
        fromRight (prevSceneKey, 64);
        sceneArea = keys.removeFromRight (width >= 1760 ? 210 : 190).withTrimmedRight (sizes::kGap);
        keys.removeFromRight (sizes::kGapLarge);
    }
    const int keyWidth = juce::jmin (quick ? 132 : 150, (keys.getWidth() - ((int) viewKeys.size() - 1) * sizes::kGapSmall) / juce::jmax (1, (int) viewKeys.size()));
    for (auto& key : viewKeys)
    {
        key->setBounds (keys.removeFromLeft (keyWidth));
        keys.removeFromLeft (sizes::kGapSmall);
    }
    const bool showInspector = inspectorOpen && inspector.getStrip() != nullptr;
    inspector.setVisible (showInspector);
    if (showInspector)
        inspector.setBounds (area.removeFromRight (juce::jmin (460, area.getWidth() / 2)));
    for (auto* v : std::initializer_list<juce::Component*> { &mixer, &routingView, &fxRack, &monitoring, &scenes, &library, &recorder, &setup, &controls })
        v->setBounds (area);
}

} // namespace livedaw::ui
