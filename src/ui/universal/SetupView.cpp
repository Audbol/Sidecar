#include "ui/universal/SetupView.h"

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
    : consoleTab ("CONSOLE", t.accent, t), audioTab ("AUDIO", t.accent, t), prefsTab ("PREFERENCES", t.accent, t), applyKey ("APPLY", t.accent, t),
      revertKey ("REVERT", t.accent, t), interfaceChoice (t), compensationKey ("DELAY COMPENSATION", t.accent, t),
      protectionKey ("OUTPUT PROTECTION", t.accent, t), ctx (c), theme (t)
{
    const std::pair<ToggleKey*, Page> tabs[] = { { &consoleTab, Page::Console }, { &audioTab, Page::Audio }, { &prefsTab, Page::Preferences } };
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
    addChildComponent (applyKey);
    addChildComponent (revertKey);

    interfaceChoice.caption = "INTERFACE";
    interfaceChoice.items = [] { return juce::StringArray { "WING", "UNIVERSAL" }; };
    interfaceChoice.current = [this] { return ctx.interfaceKind == InterfaceKind::Universal ? 1 : 0; };
    interfaceChoice.choose = [this] (int i)
    {
        // Switching rebuilds the window's skin: let the tap return first.
        juce::MessageManager::callAsync ([&context = ctx, i]
        {
            if (context.setInterface)
                context.setInterface (i == 1 ? InterfaceKind::Universal : InterfaceKind::Wing);
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
    for (auto* child : std::initializer_list<juce::Component*> { &interfaceChoice, &compensationKey, &protectionKey })
        addChildComponent (child);

    device = std::make_unique<juce::AudioDeviceSelectorComponent> (ctx.deviceManager, 0, 256, 0, 256, false, false, false, false);
    addChildComponent (*device);
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
    for (auto& r : rows)
        r->setVisible (p == Page::Console);
    applyKey.setVisible (p == Page::Console);
    revertKey.setVisible (p == Page::Console);
    device->setVisible (p == Page::Audio);
    for (auto* c : std::initializer_list<juce::Component*> { &interfaceChoice, &compensationKey, &protectionKey })
        c->setVisible (p == Page::Preferences);
    refresh();
    repaint();
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
    for (auto* k : { &consoleTab, &audioTab, &prefsTab, &compensationKey, &protectionKey })
        k->refresh();
    interfaceChoice.refresh();
    bool changes = false;
    for (int k = 0; k < NumKinds; ++k)
        changes = changes || pending[(size_t) k] != getCurrent (static_cast<Kind> (k));
    applyKey.setEnabled (changes);
    revertKey.setEnabled (changes);
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
    for (auto* k : { &consoleTab, &audioTab, &prefsTab })
    {
        k->setBounds (tabs.removeFromLeft (170));
        tabs.removeFromLeft (sizes::kGapSmall);
    }
    area.removeFromTop (sizes::kGap);
    auto console = area.withWidth (juce::jmin (area.getWidth(), 900));
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

    device->setBounds (area.withWidth (juce::jmin (area.getWidth(), 700)));
    auto prefs = area.withWidth (juce::jmin (area.getWidth(), 600));
    layoutRows (prefs, { { &interfaceChoice, 40 }, { &compensationKey, sizes::kTouch }, { &protectionKey, sizes::kTouch } }, sizes::kGapSmall);
}

} // namespace livedaw::ui::universal
