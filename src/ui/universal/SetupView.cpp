#include "ui/universal/SetupView.h"
#include "engine/AppPaths.h"
#include "engine/ConsoleState.h"
#include "engine/Show.h"
#include "ui/MixerChooser.h"
#include "ui/universal/Widgets.h"

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kRow = 52;

    const char* kindName (SetupView::Kind k)
    {
        static const char* names[] = { "CHANNELS", "AUX", "BUSES", "MAINS", "MATRICES", "DCAS", "MUTE GROUPS", "FX SLOTS" };
        return names[(size_t) k];
    }

    /** The range a count may take (the engine holds more channels; a console of 1024 is plenty here). */
    juce::Range<int> rangeOf (SetupView::Kind k)
    {
        switch (k)
        {
            case SetupView::Channels:   return { 0, 1024 };
            case SetupView::Aux:        return { 0, 512 };
            case SetupView::Buses:      return { 0, 256 };
            case SetupView::Mains:      return { 1, ld::kMaxMains };
            case SetupView::Matrices:   return { 0, ld::kMaxMatrices };
            case SetupView::Dcas:       return { 0, ld::kMaxDcas };
            case SetupView::MuteGroups: return { 0, ld::kMaxMuteGroups };
            case SetupView::FxSlots:    return { ld::kWingFx, ld::kMaxFx };
            case SetupView::NumKinds:   break;
        }
        return {};
    }

    /** What a removal of `kind` from `from` (1-based) to `to` takes, and what stays. */
    juce::String removal (SetupView::Kind k, int from, int to)
    {
        static const char* labels[] = { "CH", "AUX", "BUS", "MAIN", "MTX", "DCA", "MGRP", "FX" };
        const auto range = juce::String (labels[(size_t) k]) + " " + juce::String (from) + (to > from ? "-" + juce::String (to) : juce::String());
        switch (k)
        {
            case SetupView::Channels:
            case SetupView::Aux:        return range + ", with their settings";
            case SetupView::Buses:
            case SetupView::Mains:
            case SetupView::Matrices:   return range + " (one that something sends to stays, with those before it)";
            case SetupView::Dcas:
            case SetupView::MuteGroups: return range + " (one with members stays, with those before it)";
            case SetupView::FxSlots:    return range + " (a slot in use stays, with those before it)";
            case SetupView::NumKinds:   break;
        }
        return range;
    }
} // namespace

/** A count: its name, what the console has, what APPLY makes it, and keys to change it. */
class SetupView::Row final : public juce::Component
{
public:
    Row (SetupView& o, Kind k)
        : owner (o), kind (k), minus8 ("-8", o.theme.accent, o.theme), minus1 ("-1", o.theme.accent, o.theme), plus1 ("+1", o.theme.accent, o.theme),
          plus8 ("+8", o.theme.accent, o.theme)
    {
        const std::pair<ToggleKey*, int> keys[] = { { &minus8, -8 }, { &minus1, -1 }, { &plus1, 1 }, { &plus8, 8 } };
        for (auto [key, delta] : keys)
        {
            key->onClick = [this, step = delta] { owner.setPending (kind, owner.getPending (kind) + step); };
            addAndMakeVisible (key);
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.theme;
        g.setColour (t.raised.withAlpha (0.5f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), sizes::kRadius);
        auto area = getLocalBounds().reduced (sizes::kGapLarge, 0);
        g.setColour (t.text);
        g.setFont (type::font (type::kLabel, true));
        g.drawText (kindName (kind), area.removeFromLeft (180), juce::Justification::centredLeft);
        const int now = owner.getCurrent (kind), next = owner.getPending (kind);
        g.setFont (type::font (type::kTitle, true));
        g.setColour (next == now ? t.text : t.accent);
        g.drawText (next == now ? juce::String (now) : juce::String (now) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + juce::String (next),
                    area.removeFromLeft (200), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto keys = getLocalBounds().reduced (sizes::kGapSmall).removeFromRight (4 * 64 + 3 * sizes::kGapSmall);
        for (auto* k : { &minus8, &minus1, &plus1, &plus8 })
        {
            k->setBounds (keys.removeFromLeft (64));
            keys.removeFromLeft (sizes::kGapSmall);
        }
    }

private:
    SetupView& owner;
    Kind kind;
    ToggleKey minus8, minus1, plus1, plus8;
};

SetupView::SetupView (UiContext& c, const Theme& t)
    : consoleTab ("CONSOLE", t.accent, t), audioTab ("AUDIO", t.accent, t), remoteTab ("REMOTE", t.accent, t), prefsTab ("PREFERENCES", t.accent, t), benchmarkTab ("BENCHMARK", t.accent, t),
      applyKey ("APPLY", t.accent, t), revertKey ("REVERT", t.accent, t), interfaceChoice (t), compensationKey ("DELAY COMPENSATION", t.accent, t),
      protectionKey ("OUTPUT PROTECTION", t.accent, t), gainBypassKey ("GAIN BYPASS (INPUTS ALREADY PREAMPED)", t.accent, t),
      copyFromKey ("COPY FROM ANOTHER CONSOLE...", t.accent, t), remote (c, t), benchmark (c, t), mixerConfig (c, t), ctx (c), theme (t)
{
    for (auto* k : { &consoleTab, &audioTab, &remoteTab, &prefsTab, &benchmarkTab })
        k->tab = true;
    const std::pair<ToggleKey*, Page> tabs[] = { { &consoleTab, Page::Console }, { &audioTab, Page::Audio }, { &remoteTab, Page::Remote },
                                                 { &prefsTab, Page::Preferences }, { &benchmarkTab, Page::Benchmark } };
    for (auto [key, target] : tabs)
    {
        key->isOn = [this, p = target] { return page == p; };
        key->onClick = [this, p = target] { showPage (p); };
        addAndMakeVisible (key);
    }
    for (int k = 0; k < NumKinds; ++k)
    {
        rows.push_back (std::make_unique<Row> (*this, static_cast<Kind> (k)));
        addChildComponent (*rows.back());
    }
    applyKey.onClick = [this]
    {
        const auto removals = describeRemovals();
        if (removals.isEmpty())
        {
            apply();
            return;
        }
        juce::Component::SafePointer<SetupView> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withTitle ("APPLY")
                                          .withMessage ("This takes away:\n" + removals.joinIntoString ("\n"))
                                          .withButton ("APPLY")
                                          .withButton ("CANCEL")
                                          .withAssociatedComponent (this),
                                      [safe] (int result)
                                      {
                                          if (safe != nullptr && result == 1)
                                              safe->apply();
                                      });
    };
    revertKey.onClick = [this] { revert(); };
    // Lit while there is something to apply.
    applyKey.isOn = [this]
    {
        for (int k = 0; k < NumKinds; ++k)
            if (pending[(size_t) k] != getCurrent (static_cast<Kind> (k)))
                return true;
        return false;
    };
    addChildComponent (applyKey);
    addChildComponent (revertKey);

    interfaceChoice.caption = "INTERFACE";
    interfaceChoice.items = [] { juce::StringArray names; for (const auto& m : mixerChooser::mixers()) names.add (m.name); return names; };
    interfaceChoice.current = [this] { return (int) ctx.interfaceKind; };
    interfaceChoice.choose = [this] (int i)
    {
        // Switching rebuilds the window's skin: let the tap return first.
        juce::MessageManager::callAsync ([&context = ctx, i]
        {
            if (context.setInterface)
                context.setInterface (static_cast<InterfaceKind> (i));
        });
    };
    compensationKey.isOn = [this] { return ctx.engine.isLatencyCompensationOn(); };
    compensationKey.onClick = [this]
    {
        // An app setting (the window applies it at start-up), as on the WING's SETUP.
        const bool on = ! ctx.engine.isLatencyCompensationOn();
        ctx.engine.setLatencyCompensation (on);
        ctx.engine.updateLatencyCompensation();
        ctx.settings.setValue (uiprefs::latencyCompensation, on);
    };
    protectionKey.isOn = [this] { return ctx.engine.outputProtection.load(); };
    protectionKey.onClick = [this] { ctx.engine.outputProtection = ! ctx.engine.outputProtection.load(); };
    // An app setting (MainComponent applies it at start-up), as on the WING's SETUP.
    gainBypassKey.isOn = [this] { return ctx.engine.isGainBypassOn(); };
    gainBypassKey.onClick = [this]
    {
        const bool on = ! ctx.engine.isGainBypassOn();
        ctx.engine.setGainBypass (on);
        ctx.settings.setValue (uiprefs::gainBypass, on);
        ctx.log.info (on ? "Gain bypass on: the inputs' GAIN counts as 0 dB, TRIM still works"
                         : "Gain bypass off: the inputs' GAIN applies again");
    };
    gainBypassKey.setTooltip ("For inputs already through a real preamp (an interface, a console's recording): "
                              "the GAIN counts as 0 dB, TRIM still works. Applies to every console.");
    copyFromKey.onClick = [this] { chooseCopySource(); };
    copyFromKey.setTooltip ("Copy another console's names, preamp, EQ, dynamics, sends and faders into this one "
                            "(a console Sidecar ran before, or a show file)");
    for (auto* child : std::initializer_list<juce::Component*> { &interfaceChoice, &copyFromKey, &compensationKey, &protectionKey, &gainBypassKey })
        addChildComponent (child);

    addChildComponent (remote);
    addChildComponent (benchmark);
    addChildComponent (mixerConfig);
    revert();
    showPage (Page::Console);
    startTimerHz (5);
}

SetupView::~SetupView()
{
    stopTimer();
}

void SetupView::showPage (Page p)
{
    page = p;
    showConsolePage();
    if (p == Page::Audio && device == nullptr)
    {
        // The audio device's page lists every driver and device, which costs time a visit to another page never needs.
        device = std::make_unique<juce::AudioDeviceSelectorComponent> (ctx.deviceManager, 0, 256, 0, 256, false, false, false, false);
        addChildComponent (*device);
        resized();
    }
    if (device != nullptr)
        device->setVisible (p == Page::Audio);
    remote.setVisible (p == Page::Remote);
    benchmark.setVisible (p == Page::Benchmark);
    for (auto* c : std::initializer_list<juce::Component*> { &interfaceChoice, &copyFromKey, &compensationKey, &protectionKey, &gainBypassKey })
        c->setVisible (p == Page::Preferences);
    refresh();
    repaint();
}

void SetupView::showConsolePage()
{
    const bool console = page == Page::Console, dlive = ctx.engine.getProfile() == ld::kProfileDLive;
    for (auto& r : rows)
        r->setVisible (console && ! dlive);
    applyKey.setVisible (console && ! dlive);
    revertKey.setVisible (console && ! dlive);
    if (mixerConfig.isVisible() != (console && dlive))
    {
        mixerConfig.setVisible (console && dlive);
        mixerConfig.revert();
    }
}

int SetupView::getCurrent (Kind k) const
{
    const auto n = ctx.engine.getCounts();
    switch (k)
    {
        case Channels:   return n.channels;
        case Aux:        return n.aux;
        case Buses:      return n.buses;
        case Mains:      return n.mains;
        case Matrices:   return n.matrices;
        case Dcas:       return n.dcas;
        case MuteGroups: return n.muteGroups;
        case FxSlots:    return n.fx;
        case NumKinds:   break;
    }
    return 0;
}

void SetupView::setPending (Kind k, int count)
{
    pending[(size_t) k] = rangeOf (k).clipValue (count);
    rows[(size_t) k]->repaint();
}

juce::StringArray SetupView::describeRemovals() const
{
    juce::StringArray lines;
    for (int k = 0; k < NumKinds; ++k)
    {
        const auto kind = static_cast<Kind> (k);
        const int now = getCurrent (kind), next = pending[(size_t) k];
        if (next < now)
            lines.add (removal (kind, next + 1, now));
    }
    return lines;
}

void SetupView::apply()
{
    auto& e = ctx.engine;
    // The destinations first (a send to one that goes is off anyway), then the inputs, the groups, the FX slots.
    e.setNumStrips (StripKind::Matrix, pending[Matrices]);
    e.setNumStrips (StripKind::Main, pending[Mains]);
    e.setNumStrips (StripKind::Bus, pending[Buses]);
    e.setNumStrips (StripKind::Aux, pending[Aux]);
    e.setNumStrips (StripKind::Channel, pending[Channels]);
    e.setNumDcas (pending[Dcas]);
    e.setNumMuteGroups (pending[MuteGroups]);
    e.getFx().setNumSlots (pending[FxSlots]);
    if (ctx.structureChanged)
        ctx.structureChanged();

    // What stayed because it is in use.
    juce::StringArray kept;
    for (int k = 0; k < NumKinds; ++k)
        if (const int now = getCurrent (static_cast<Kind> (k)); now > pending[(size_t) k])
            kept.add (juce::String (kindName (static_cast<Kind> (k))) + " " + juce::String (now) + " (in use)");
    status = kept.isEmpty() ? juce::String ("Applied.") : "Applied; kept: " + kept.joinIntoString (", ") + ".";
    ctx.log.info ("Console configuration: " + status);
    revert();
}

void SetupView::revert()
{
    for (int k = 0; k < NumKinds; ++k)
        pending[(size_t) k] = getCurrent (static_cast<Kind> (k));
    for (auto& r : rows)
        r->repaint();
}

void SetupView::refresh()
{
    for (auto* k : { &consoleTab, &audioTab, &remoteTab, &prefsTab, &benchmarkTab, &compensationKey, &protectionKey, &gainBypassKey })
        k->refresh();
    interfaceChoice.refresh();
    showConsolePage(); // the console may have become a dLive, or stopped being one
    if (mixerConfig.isVisible())
        mixerConfig.refresh();
    bool changes = false;
    for (int k = 0; k < NumKinds; ++k)
        changes = changes || pending[(size_t) k] != getCurrent (static_cast<Kind> (k));
    applyKey.setEnabled (changes);
    revertKey.setEnabled (changes);
    applyKey.refresh();
    for (auto& r : rows)
        r->repaint();
}

void SetupView::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    if (page == Page::Console && status.isNotEmpty())
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel));
        g.drawText (status, applyKey.getBounds().withX (revertKey.getRight() + sizes::kGapLarge).withRight (getWidth() - sizes::kGap),
                    juce::Justification::centredLeft);
    }
}

void SetupView::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap, sizes::kGapSmall);
    auto tabs = area.removeFromTop (sizes::kTouch);
    for (auto* k : { &consoleTab, &audioTab, &remoteTab, &prefsTab, &benchmarkTab })
    {
        k->setBounds (tabs.removeFromLeft (170));
        tabs.removeFromLeft (sizes::kGapSmall);
    }
    area.removeFromTop (sizes::kGap);
    auto console = area.withWidth (juce::jmin (area.getWidth(), 900));
    mixerConfig.setBounds (console);
    for (auto& r : rows)
    {
        r->setBounds (console.removeFromTop (kRow));
        console.removeFromTop (sizes::kGapSmall);
    }
    console.removeFromTop (sizes::kGap);
    auto keys = console.removeFromTop (sizes::kTouch);
    applyKey.setBounds (keys.removeFromLeft (170));
    keys.removeFromLeft (sizes::kGapSmall);
    revertKey.setBounds (keys.removeFromLeft (170));

    if (device != nullptr)
        device->setBounds (area.withWidth (juce::jmin (area.getWidth(), 700)));
    remote.setBounds (area);
    benchmark.setBounds (area);
    auto prefs = area.withWidth (juce::jmin (area.getWidth(), 600));
    layoutRows (prefs, { { &interfaceChoice, 40 }, { &copyFromKey, sizes::kTouch }, { &compensationKey, sizes::kTouch }, { &protectionKey, sizes::kTouch }, { &gainBypassKey, sizes::kTouch } }, sizes::kGapSmall);
}

//==============================================================================
// COPY FROM (docs/COPY_BETWEEN_CONSOLES.md)

namespace
{
    /** A notice over `host` with an OK key. */
    void notice (juce::Component& host, const juce::String& title, const juce::String& text)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle (title).withMessage (text).withButton ("OK").withAssociatedComponent (&host), nullptr);
    }
}

void SetupView::chooseCopySource()
{
    if (ctx.engine.isRemoteControlled())
    {
        notice (*this, "COPY FROM", "Not while a real console is under remote control: its own settings are the ones that count.");
        return;
    }
    // The consoles Sidecar ran before, as kept when it turned into another (issue #25).
    auto kept = paths::appData().getChildFile ("consoles").findChildFiles (juce::File::findFiles, false, juce::String ("*") + Show::fileExtension);
    std::sort (kept.begin(), kept.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });

    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeelFor (theme));
    menu.addSectionHeader ("COPY FROM");
    for (int i = 0; i < kept.size(); ++i)
        menu.addItem (i + 1, "THE " + kept[i].getFileNameWithoutExtension().toUpperCase() + " (AS LEFT "
                                 + kept[i].getLastModificationTime().formatted ("%d %b %H:%M").toUpperCase() + ")");
    if (kept.isEmpty())
        menu.addItem (-1, "NO OTHER CONSOLE YET: ONE IS KEPT WHEN SIDECAR TURNS INTO ANOTHER", false, false);
    menu.addSeparator();
    constexpr int showFileItem = 10000;
    menu.addItem (showFileItem, "A SHOW FILE...");

    juce::Component::SafePointer<SetupView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&copyFromKey), [safe, kept] (int item)
    {
        if (safe == nullptr || item == 0)
            return;
        if (item != showFileItem)
        {
            if (juce::isPositiveAndBelow (item - 1, kept.size()))
                safe->chooseCopyWhat (kept[item - 1]);
            return;
        }
        safe->copyChooser = std::make_unique<juce::FileChooser> ("Copy from a show", paths::documents(), juce::String ("*") + Show::fileExtension);
        safe->copyChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe != nullptr && fc.getResult().existsAsFile())
                safe->chooseCopyWhat (fc.getResult());
        });
    });
}

void SetupView::chooseCopyWhat (const juce::File& source)
{
    using namespace state;
    struct Choice
    {
        const char* text;
        juce::uint32 sections;
    };
    static const Choice choices[] = {
        { "NAMES, PREAMP, GATE, EQ, DYNAMICS, SENDS AND FADERS", Name | Preamp | Gate | Eq | Dynamics | Sends | Fader },
        { "ALL OF THAT, WITH ROUTING AND GROUPS", AllSections & ~(juce::uint32) Inserts },
        { "NAMES ONLY", Name },
        { "GATE, EQ AND DYNAMICS ONLY", Gate | Eq | Dynamics },
        { "FADERS AND SENDS ONLY", Fader | Sends },
    };
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeelFor (theme));
    menu.addSectionHeader ("COPY WHAT FROM " + source.getFileNameWithoutExtension().toUpperCase());
    for (int i = 0; i < (int) std::size (choices); ++i)
        menu.addItem (i + 1, choices[i].text);

    juce::Component::SafePointer<SetupView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&copyFromKey), [safe, source] (int item)
    {
        if (safe == nullptr || ! juce::isPositiveAndBelow (item - 1, (int) std::size (choices)))
            return;
        const auto& c = choices[item - 1];
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withTitle ("COPY FROM")
                                          .withMessage ("Copy " + juce::String (c.text).toLowerCase() + " from " + source.getFileNameWithoutExtension()
                                                        + " into this console?\n\nEach channel, bus, main and matrix goes onto the one with the same number here. "
                                                          "Effects and scenes stay as they are. UNDO COPY takes it back.")
                                          .withButton ("COPY")
                                          .withButton ("CANCEL")
                                          .withAssociatedComponent (safe.getComponent()),
                                      [safe, source, sections = c.sections, what = juce::String (c.text).toLowerCase()] (int result)
                                      {
                                          if (safe != nullptr && result == 1)
                                              safe->copyFrom (source, sections, what);
                                      });
    });
}

void SetupView::copyFrom (const juce::File& source, juce::uint32 sections, const juce::String& what)
{
    const auto r = ctx.show.copyFrom (source, sections, 0x1fu);
    if (r.strips < 0)
    {
        notice (*this, "COPY FROM", "Nothing was copied: " + r.error);
        ctx.log.warning ("COPY FROM " + source.getFileName() + ": " + r.error);
        return;
    }
    if (ctx.structureChanged)
        ctx.structureChanged();
    ctx.log.info ("Copied " + what + " from " + r.source + " (" + source.getFileName() + ") onto " + juce::String (r.strips) + " strips");
    for (const auto& line : r.leftOut)
        ctx.log.info ("COPY FROM left out: " + line);

    juce::String text;
    text << "Copied " << what << " from the " << r.source << " onto " << r.strips << " strips.";
    if (! r.leftOut.isEmpty())
        text << "\n\nLeft out, with no place in this console:\n" << r.leftOut.joinIntoString ("\n");
    juce::Component::SafePointer<SetupView> safe (this);
    // UNDO COPY first: the last key (OK) is also what Escape gives (0).
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("COPY FROM").withMessage (text).withButton ("UNDO COPY").withButton ("OK").withAssociatedComponent (this),
                                  [safe] (int result)
                                  {
                                      if (safe == nullptr || result != 1)
                                          return;
                                      // UNDO COPY (the copy set the step back first).
                                      if (safe->ctx.show.canUndoRecall())
                                      {
                                          safe->ctx.show.undoRecall();
                                          if (safe->ctx.structureChanged)
                                              safe->ctx.structureChanged();
                                          safe->ctx.log.info ("COPY FROM undone");
                                      }
                                  });
}

} // namespace livedaw::ui::universal
