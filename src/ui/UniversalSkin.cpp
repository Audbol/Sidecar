#include "ui/UniversalSkin.h"
#include "ui/RemoteBar.h"

namespace livedaw::ui
{

using namespace universal;

namespace
{
    /** The theme the app settings name: the skin's, from its first member on. */
    const Theme& themeOf (UiContext& c)
    {
        return Theme::forConsole (c.settings, (int) c.engine.getProfile());
    }

    /** The surface a console's profile returns to: one table for the back key, its tooltip and the swap. */
    struct Back
    {
        InterfaceKind kind;
        const char* key;
        const char* name;
    };
    Back backTo (ld::ProfileId profile)
    {
        if (profile == ld::kProfileXenyx1202) return { InterfaceKind::Xenyx1202, "Q1202USB SURFACE", "XENYX Q1202USB" };
        if (profile == ld::kProfileMidasPro2) return { InterfaceKind::Pro2, "PRO2 SURFACE", "PRO2" };
        if (profile == ld::kProfileDm2000)    return { InterfaceKind::Dm2000, "DM2000 SURFACE", "DM2000" };
        if (profile == ld::kProfileX32)       return { InterfaceKind::X32, "X32 SURFACE", "X32" };
        if (profile == ld::kProfileProMix01)  return { InterfaceKind::ProMix, "PROMIX SURFACE", "ProMix 01" };
        if (profile == ld::kProfileYamaha02R) return { InterfaceKind::Yamaha02R, "02R SURFACE", "02R" };
        if (profile == ld::kProfileSq5)       return { InterfaceKind::Sq5, "SQ-5 SURFACE", "SQ-5" };
        if (profile == ld::kProfileYamaha01V) return { InterfaceKind::Yamaha01V, "01V SURFACE", "01V" };
        if (profile == ld::kProfileDLive)     return { InterfaceKind::DLive, "DLIVE SURFACE", "dLive" };
        if (profile == ld::kProfilePluginHost) return { InterfaceKind::PluginHost, "PLUGIN HOST SURFACE", "PLUGIN HOST" };
        return { InterfaceKind::Wing, "WING SURFACE", "WING" };
    }
} // namespace

UniversalSkin::UniversalSkin (UiContext& c)
    : prevSceneKey ("PREV", themeOf (c).accent, themeOf (c)), nextSceneKey ("GO NEXT", themeOf (c).accent, themeOf (c)), clearSoloKey ("CLR SOLO", themeOf (c).solo, themeOf (c)),
      dimKey ("DIM", themeOf (c).solo, themeOf (c)), talkAKey ("TALK A", themeOf (c).solo, themeOf (c)), talkBKey ("TALK B", themeOf (c).solo, themeOf (c)),
      ctx (c), theme (themeOf (c)),
      wingKey (backTo (c.engine.getProfile()).key, themeOf (c).accent, themeOf (c)), showKey ("SHOW", theme.accent, theme), menuKey ("MENU", theme.accent, theme), headerKey ("LESS", theme.accent, theme), connectKey ("CONNECT", theme.accent, theme), mixer (c, theme), routingView (c, theme), fxRack (c, theme),
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
    // Switching rebuilds the window's skin: let the click return first. A real console that the
    // Universal Mixer controls (a later mode) names itself the same way, with its own `go`.
    const auto back = backTo (ctx.engine.getProfile());
    setBackTarget (back.key, back.name, [&context = ctx, kind = back.kind]
    {
        if (context.setInterface)
            context.setInterface (kind);
    });
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
    // The header's second row (the strips' controls and the show's keys) can be put away for strip height.
    row2Shown = ctx.settings.getBoolValue (uiprefs::universalHeader, true);
    headerKey.onClick = [this] { setSecondRowShown (! row2Shown); };
    addAndMakeVisible (headerKey);
    remotebar::setup (connectKey, ctx);
    addAndMakeVisible (connectKey);
    for (auto* part : mixer.getHeaderParts())
        addChildComponent (*part); // the window's header holds the strips' controls
    mixer.onHeaderChanged = [this] { layoutSecondRow(); };
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
    // Back from a console's surface: the view and the strips' filter and scroll where they were
    // (the selected strip is the window's, so it stays across the swap).
    if (const auto m = ctx.universalMemory; m.view >= 0 && m.view <= (int) View::Controls)
    {
        if (m.filter != mixer.getFilter())
            mixer.setFilter (m.filter);
        showView (static_cast<View> (m.view));
        mixer.setScrollX (m.scrollX);
    }
    selectionChanged();
}

void UniversalSkin::setLightTheme (bool light)
{
    setLook (light ? Theme::Look::BrushedSilver : Theme::Look::FlagshipBlack);
}

void UniversalSkin::setLook (Theme::Look look)
{
    // Kept for the console that is loaded (and, as before, as the app's own choice).
    ctx.settings.setValue (Theme::consoleSetting ((int) ctx.engine.getProfile()), Theme::idOf (look));
    if (look < Theme::Look::ConsoleWing)
        ctx.settings.setValue (Theme::kSetting, Theme::idOf (look));
    if (look == theme.look || ! ctx.setInterface)
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
    for (int i = 0; i < Theme::kNumLooks; ++i)
        themes.addItem (kMenuTheme + i, Theme::titleOf ((Theme::Look) i), true, theme.look == (Theme::Look) i);
    themes.addSeparator();
    themes.addSectionHeader ("Console themes");
    for (int i = 0; i < Theme::kNumConsoleLooks; ++i)
    {
        const auto look = (Theme::Look) ((int) Theme::Look::ConsoleWing + i);
        themes.addItem (kMenuConsoleTheme + i, Theme::titleOf (look), true, theme.look == look);
    }
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
    menu.addItem (kMenuHeader, "Second header row", true, row2Shown);
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
    else if (item >= kMenuConsoleTheme && item < kMenuConsoleTheme + Theme::kNumConsoleLooks)
        setLook ((Theme::Look) ((int) Theme::Look::ConsoleWing + item - kMenuConsoleTheme));
    else if (item >= kMenuTheme && item < kMenuTheme + Theme::kNumLooks)
        setLook ((Theme::Look) (item - kMenuTheme));
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
    else if (item == kMenuHeader)
        setSecondRowShown (! row2Shown);
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
    if (getWidth() > 0)
        resized(); // the second row follows the view
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

UniversalSkin::~UniversalSkin()
{
    const int filter = mixer.getFilter();
    ctx.universalMemory = { (int) view, filter > universal::MixerView::kDcas ? -1 : filter, mixer.getScrollX() };
}

void UniversalSkin::setBackTarget (const juce::String& keyText, const juce::String& consoleName, std::function<void()> go)
{
    backName = consoleName;
    wingKey.setButtonText (keyText);
    wingKey.setTooltip ("Back to the " + consoleName + " surface");
    wingKey.onClick = [go = std::move (go)]
    {
        juce::MessageManager::callAsync (go);
    };
}

void UniversalSkin::setConnection (const Connection& c)
{
    connection = c;
    repaint();
}

void UniversalSkin::setSecondRowShown (bool shown)
{
    row2Shown = shown;
    ctx.settings.setValue (uiprefs::universalHeader, shown);
    resized();
    repaint();
}

void UniversalSkin::timerCallback()
{
    if (! isVisible() || isCovered())
        return;
    history.tick (juce::Time::getMillisecondCounter());
    for (auto* key : { &clearSoloKey, &dimKey, &talkAKey, &talkBKey })
        key->refresh();
    {
        // A real console under remote control: its name and status in the header's connection place.
        const auto info = remotebar::infoOf (ctx);
        remotebar::refresh (connectKey, ctx, shownRemote);
        Connection c { ctx.engine.getRemoteSession().consoleName() + (info.name.isNotEmpty() ? " " + info.name : juce::String()),
                       info.state != RemoteSession::State::Offline, info.state == RemoteSession::State::Connected,
                       remotebar::word (info.state) };
        if (c.remote != connection.remote || c.connected != connection.connected || c.status != connection.status || c.console != connection.console)
            setConnection (c);
    }
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
        repaint (infoArea);
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

void UniversalSkin::layoutSecondRow()
{
    // The strips' controls, the left of the second row in the MIXER view.
    mixer.layoutHeader (mixerKeysArea, view == View::Mixer && row2Shown && ! mixerKeysArea.isEmpty());
}

void UniversalSkin::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    auto head = getLocalBounds().removeFromTop (headerHeight());
    g.setColour (theme.panel); // the header is flat: the metal is for the strips
    g.fillRect (head);
    g.setColour (theme.outline);
    g.fillRect (head.removeFromBottom (1));
    if (row2Shown)
        g.fillRect (0, sizes::kTopBar - 1, getWidth(), 1);

    // The title, and under it the connection: LOCAL (this app's own mixer), later a real console
    // the Universal Mixer controls (its name, CONNECTED or OFFLINE; its CONNECT key goes beside it).
    auto title = titleArea;
    g.setColour (theme.text);
    g.setFont (type::font (type::kTitle, true));
    g.drawFittedText ("Universal Mixer", title.withTrimmedBottom (20), juce::Justification::centredLeft, 1, 0.8f);
    auto caption = title.removeFromBottom (22).withTrimmedBottom (4);
    g.setColour (connection.remote && connection.connected ? theme.accent : theme.textDim);
    g.fillEllipse (caption.removeFromLeft (10).withSizeKeepingCentre (7, 7).toFloat());
    caption.removeFromLeft (4);
    g.setFont (type::font (type::kCaption, true));
    g.drawFittedText (connection.remote ? connection.console + "  " + connection.status : juce::String ("LOCAL  PREVIEW"),
                      caption, juce::Justification::centredLeft, 1, 0.8f);

    // The second row where it has no controls of its own: the show, the console's counts, DSP and time.
    if (! infoArea.isEmpty())
    {
        auto info = infoArea;
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel));
        if (info.getWidth() > 760)
            g.drawText (shownStatus, info.removeFromRight (190), juce::Justification::centredRight);
        g.setColour (theme.text);
        g.setFont (type::font (type::kValue, true));
        g.drawFittedText (getShowTitle(), info.removeFromLeft (juce::jmin (260, info.getWidth() / 2)), juce::Justification::centredLeft, 1, 0.7f);
        g.setColour (theme.textDim);
        const auto font = type::font (type::kLabel);
        g.setFont (font);
        g.drawFittedText (juce::GlyphArrangement::getStringWidth (font, summary) * 0.8f <= (float) info.getWidth() ? summary : summaryShort,
                          info, juce::Justification::centredLeft, 1, 0.8f);
    }
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
}

void UniversalSkin::resized()
{
    const int w = getWidth();
    auto area = getLocalBounds();
    auto head = area.removeFromTop (headerHeight());
    auto bar = head.removeFromTop (sizes::kTopBar).reduced (sizes::kGapLarge, (sizes::kTopBar - sizes::kTouch) / 2);
    const auto fromRight = [] (juce::Rectangle<int>& r, juce::Component& key, int kw)
    {
        key.setBounds (r.removeFromRight (kw));
        r.removeFromRight (sizes::kGapSmall);
    };

    // Row 1: the title and connection, the views, the console's way back, SHOW, MENU and the header's key.
    headerKey.setButtonText (row2Shown ? "LESS" : "MORE");
    headerKey.setTooltip (row2Shown ? "Hide the second header row for more strip height" : "Show the second header row");
    fromRight (bar, headerKey, 64);
    fromRight (bar, menuKey, 84);
    if (wingKey.isVisible())
        fromRight (bar, wingKey, 160);
    fromRight (bar, connectKey, 110);
    const bool showShown = w >= 1500;
    showKey.setVisible (showShown);
    if (showShown)
        fromRight (bar, showKey, 96);
    // With the second row put away, what a show needs stays on the first one where it fits.
    const bool oneRow = ! row2Shown && w >= 1500;
    for (auto* key : { &prevSceneKey, &talkAKey, &talkBKey })
        key->setVisible (false);
    for (auto* key : { &nextSceneKey, &clearSoloKey, &dimKey })
        key->setVisible (oneRow);
    if (oneRow)
    {
        fromRight (bar, dimKey, 64);
        fromRight (bar, clearSoloKey, 104);
        fromRight (bar, nextSceneKey, 96);
    }
    titleArea = bar.removeFromLeft (w >= 1500 ? 250 : 180);
    bar.removeFromLeft (sizes::kGap);
    const int n = (int) viewKeys.size();
    const bool bridgeShown = bar.getWidth() - (n * 104 + (n - 1) * sizes::kGapSmall) >= 236;
    bridge.setVisible (bridgeShown);
    if (bridgeShown)
        bridge.setBounds (bar.removeFromRight (220).withSizeKeepingCentre (220, 28));
    const int keyWidth = juce::jmin (124, (bar.getWidth() - (n - 1) * sizes::kGapSmall) / juce::jmax (1, n));
    for (auto& key : viewKeys)
    {
        key->setBounds (bar.removeFromLeft (keyWidth));
        bar.removeFromLeft (sizes::kGapSmall);
    }

    // Row 2: the strips' controls (MIXER) or the show's details, and at the right what a show needs.
    sceneArea = {};
    infoArea = {};
    mixerKeysArea = {};
    if (row2Shown)
    {
        auto keys = head.reduced (sizes::kGap, sizes::kGapSmall);
        const bool talk = w >= 1500, dim = w >= 1300, readout = w >= 1180;
        for (auto* key : { &prevSceneKey, &nextSceneKey, &clearSoloKey })
            key->setVisible (true);
        dimKey.setVisible (dim);
        for (auto* key : { &talkAKey, &talkBKey })
            key->setVisible (talk);
        if (talk)
        {
            fromRight (keys, talkBKey, 72);
            fromRight (keys, talkAKey, 72);
        }
        if (dim)
            fromRight (keys, dimKey, 64);
        fromRight (keys, clearSoloKey, 104);
        keys.removeFromRight (sizes::kGap);
        fromRight (keys, nextSceneKey, 96);
        fromRight (keys, prevSceneKey, 64);
        if (readout)
            sceneArea = keys.removeFromRight (w >= 1500 ? 210 : 170).withTrimmedRight (sizes::kGap);
        keys.removeFromRight (sizes::kGapLarge);
        (view == View::Mixer ? mixerKeysArea : infoArea) = keys;
    }
    layoutSecondRow();

    const bool showInspector = inspectorOpen && inspector.getStrip() != nullptr;
    inspector.setVisible (showInspector);
    if (showInspector)
        inspector.setBounds (area.removeFromRight (juce::jmin (460, area.getWidth() / 2)));
    for (auto* v : std::initializer_list<juce::Component*> { &mixer, &routingView, &fxRack, &monitoring, &scenes, &library, &recorder, &setup, &controls })
        v->setBounds (area);
    repaint (getLocalBounds().removeFromTop (headerHeight()));
}

} // namespace livedaw::ui
