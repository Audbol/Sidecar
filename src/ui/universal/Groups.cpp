#include "ui/universal/Groups.h"

#include "ui/Knobs.h"

namespace livedaw::ui::universal
{

juce::String customGroupName (const juce::String& name, int index)
{
    const auto n = name.trim();
    const auto number = juce::String (index + 1);
    for (const auto* prefix : { "MGRP.", "MGRP ", "DCA.", "DCA ", "DCA" })
        if (n.equalsIgnoreCase (prefix + number))
            return {};
    return n;
}

void askForText (juce::Component& host, const juce::String& title, const juce::String& prompt, const juce::String& initial,
                 std::function<void (const juce::String&)> done)
{
    auto* w = new juce::AlertWindow (title, prompt, juce::MessageBoxIconType::NoIcon, &host);
    w->addTextEditor ("text", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::Component> safe (&host);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, done = std::move (done)] (int result)
    {
        if (result != 0 && safe != nullptr && done)
            done (w->getTextEditorContents ("text").trim());
    }), true);
}

//==============================================================================
GroupGrid::GroupGrid (const Theme& t, juce::Colour onColour) : theme (t), colour (onColour)
{
    startTimerHz (10);
}

int GroupGrid::columns (int width) const
{
    return juce::jmax (1, width / juce::jmax (1, minKey));
}

juce::Rectangle<int> GroupGrid::keyArea (int i) const
{
    const int n = count ? count() : 0;
    if (! juce::isPositiveAndBelow (i, n))
        return {};
    const int cols = columns (getWidth());
    const int w = getWidth() / cols;
    const int h = minKey;
    return { (i % cols) * w, (i / cols) * h, w, h };
}

int GroupGrid::heightFor (int width) const
{
    const int n = count ? count() : 0;
    const int cols = columns (width);
    return ((n + cols - 1) / cols) * minKey;
}

void GroupGrid::refresh()
{
    const int n = count ? count() : 0;
    std::vector<bool> now ((size_t) n);
    for (int i = 0; i < n; ++i)
        now[(size_t) i] = isOn && isOn (i);
    if (now != shown)
    {
        shown = std::move (now);
        repaint();
    }
}

void GroupGrid::paint (juce::Graphics& g)
{
    const int n = count ? count() : 0;
    for (int i = 0; i < n; ++i)
    {
        const auto r = keyArea (i).toFloat().reduced (2.0f);
        const bool on = juce::isPositiveAndBelow (i, (int) shown.size()) && shown[(size_t) i];
        g.setColour (on ? colour : theme.raised);
        g.fillRoundedRectangle (r, sizes::kRadius);
        g.setColour (on ? theme.textOnAccent : theme.text);
        const auto label = name ? name (i) : juce::String();
        auto text = r.reduced (3.0f, 2.0f);
        g.setFont (type::font (type::kLabel, true));
        const auto number = keyText ? keyText (i) : juce::String (i + 1);
        if (label.isEmpty())
        {
            g.drawFittedText (number, text.toNearestInt(), juce::Justification::centred, 1, 0.7f);
            continue;
        }
        g.drawText (number, text.removeFromTop (text.getHeight() * 0.5f), juce::Justification::centredBottom);
        g.setFont (type::font (type::kCaption - 2.0f));
        g.drawFittedText (label, text.toNearestInt(), juce::Justification::centredTop, 1, 0.6f);
    }
}

void GroupGrid::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown())
        return;
    const int n = count ? count() : 0;
    for (int i = 0; i < n; ++i)
        if (keyArea (i).contains (e.getPosition()))
        {
            if (toggle)
                toggle (i);
            refresh();
            return;
        }
}

//==============================================================================
DcaColumn::DcaColumn (UiContext& c, int index, const Theme& t)
    : muteKey ("MUTE", t.mute, t), soloKey ("SOLO", t.solo, t), fader (t), ctx (c), dca (index), theme (t)
{
    auto& d = ctx.engine.getDca (dca);
    muteKey.onClick = [&d] { d.mute = ! d.mute.load(); };
    muteKey.isOn = [&d] { return d.mute.load(); };
    soloKey.onClick = [&d] { d.solo = ! d.solo.load(); };
    soloKey.isOn = [&d] { return d.solo.load(); };
    fader.bind (&d.faderDb);
    for (auto* child : std::initializer_list<juce::Component*> { &muteKey, &soloKey, &fader })
        addAndMakeVisible (child);
    setTitle ("DCA " + juce::String (dca + 1));
    refresh();
}

void DcaColumn::refresh()
{
    muteKey.refresh();
    soloKey.refresh();
    fader.refresh();
    auto& d = ctx.engine.getDca (dca);
    const auto newName = ctx.engine.getDcaName (dca);
    const int newColour = d.colour.load();
    const float db = d.faderDb.load();
    if (newName != name || newColour != colour || std::abs (db - shownDb) > 0.005f)
    {
        name = newName;
        colour = newColour;
        shownDb = db;
        repaint();
    }
}

juce::Rectangle<int> DcaColumn::header() const noexcept
{
    return getLocalBounds().reduced (sizes::kGapSmall).removeFromTop (58);
}

void DcaColumn::paint (juce::Graphics& g)
{
    g.setColour (theme.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), sizes::kRadius);
    auto h = header().toFloat();
    g.setColour (consoleColour (colour));
    g.fillRoundedRectangle (h.removeFromTop (6.0f), 3.0f);
    h.removeFromTop (4.0f);
    g.setColour (theme.accent);
    g.setFont (type::font (type::kCaption, true));
    g.drawText ("DCA " + juce::String (dca + 1), h.removeFromTop (16.0f), juce::Justification::centred);
    g.setColour (theme.text);
    g.setFont (type::font (type::kLabel, true));
    g.drawFittedText (name, h.toNearestInt(), juce::Justification::centred, 2, 0.8f);

    auto bottom = getLocalBounds().reduced (sizes::kGapSmall).removeFromBottom (24);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kValue));
    g.drawText (fader::text (shownDb) + " dB", bottom, juce::Justification::centred);
}

void DcaColumn::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGapSmall);
    area.removeFromTop (header().getHeight() + sizes::kGapSmall);
    area.removeFromBottom (24);
    // Lined up with the strips' MUTE and SOLO (under their pan).
    area.removeFromTop (34 + sizes::kGapSmall);
    muteKey.setBounds (area.removeFromTop (sizes::kTouch));
    area.removeFromTop (sizes::kGapSmall);
    soloKey.setBounds (area.removeFromTop (sizes::kTouch));
    area.removeFromTop (sizes::kGap);
    // The fader as tall as a strip's (from the bottom), leaving the rest empty.
    fader.setBounds (area.removeFromBottom (juce::jmin (area.getHeight(), 340)).withTrimmedRight (12 + sizes::kGapSmall));
}

void DcaColumn::mouseUp (const juce::MouseEvent& e)
{
    if (! header().contains (e.getPosition()) || e.mouseWasDraggedSinceMouseDown())
        return;
    // A tap on the header: rename it, or pick its colour.
    juce::PopupMenu menu, colours;
    menu.addSectionHeader ("DCA " + juce::String (dca + 1));
    menu.addItem (1, "Rename...");
    for (int i = 1; i <= palette::count(); ++i)
        colours.addItem (100 + i, consoleColourName (i), true, ctx.engine.getDca (dca).colour.load() == i);
    menu.addSubMenu ("Colour", colours);
    juce::Component::SafePointer<DcaColumn> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto& self = *safe;
        if (r == 1)
            askForText (self, "RENAME", "DCA " + juce::String (self.dca + 1) + ":", self.ctx.engine.getDcaName (self.dca),
                        [safe] (const juce::String& text)
                        {
                            if (safe != nullptr)
                            {
                                safe->ctx.engine.setDcaName (safe->dca, text);
                                safe->refresh();
                            }
                        });
        else if (r > 100)
        {
            self.ctx.engine.getDca (self.dca).colour = palette::clamp (r - 100);
            self.refresh();
        }
    });
}

//==============================================================================
MuteGroupsPanel::MuteGroupsPanel (UiContext& c, const Theme& t) : groups (t, t.mute), allOffKey ("ALL OFF", t.accent, t), ctx (c), theme (t)
{
    groups.minKey = 52;
    groups.count = [this] { return ctx.engine.getNumMuteGroups(); };
    groups.isOn = [this] (int i) { return ctx.engine.muteGroups.contains (i); };
    groups.toggle = [this] (int i) { ctx.engine.muteGroups.toggle (i); };
    groups.name = [this] (int i) { return customGroupName (ctx.engine.getMuteGroupName (i), i); };
    allOffKey.onClick = [this]
    {
        ctx.engine.muteGroups.clear();
        groups.refresh();
    };
    addAndMakeVisible (groups);
    addAndMakeVisible (allOffKey);
    setTitle ("MUTE GROUPS");
}

void MuteGroupsPanel::refresh()
{
    groups.refresh();
}

void MuteGroupsPanel::paint (juce::Graphics& g)
{
    g.setColour (theme.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), sizes::kRadius);
    g.setColour (theme.mute);
    g.setFont (type::font (type::kCaption, true));
    g.drawText ("MUTE GROUPS", getLocalBounds().reduced (sizes::kGap).removeFromTop (24), juce::Justification::centred);
}

void MuteGroupsPanel::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap);
    area.removeFromTop (24 + sizes::kGapSmall);
    allOffKey.setBounds (area.removeFromBottom (sizes::kTouch));
    area.removeFromBottom (sizes::kGap);
    groups.setBounds (area);
}

} // namespace livedaw::ui::universal
