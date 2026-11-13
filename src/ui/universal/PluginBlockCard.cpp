#include "ui/universal/PluginBlockCard.h"

namespace livedaw::ui::universal
{

PluginBlockCard::PluginBlockCard (UiContext& c, const Theme& t)
    : ctx (c), theme (t), onKey ("ON", t.accent, t), editKey ("EDIT", t.accent, t), menuKey ("PLUG-IN", t.accent, t)
{
    onKey.isOn = [this] { return onFlag != nullptr && onFlag->load(); };
    onKey.onClick = [this]
    {
        const int s = slot();
        if (s < 0 || onFlag == nullptr)
            return;
        // Bypassing on or off also clears a fault, as an FX slot's.
        ctx.engine.getFx().setBlockBypassed (s, onFlag->load());
        update();
    };
    editKey.isOn = [] { return false; };
    editKey.onClick = [this]
    {
        if (rack)
            openRack();
        else
            openEditor();
    };
    menuKey.isOn = [] { return false; };
    menuKey.onClick = [this] { showMenu(); };
    for (auto* key : { &onKey, &editKey, &menuKey })
        addAndMakeVisible (*key);
}

PluginBlockCard::~PluginBlockCard()
{
    stopTimer();
}

void PluginBlockCard::setBlock (Strip* s, int blockUid, std::atomic<bool>* on)
{
    strip = s;
    uid = blockUid;
    onFlag = on;
    update();
    if (strip != nullptr)
        startTimerHz (2);
    else
        stopTimer();
}

int PluginBlockCard::slot() const
{
    return strip != nullptr ? FxSlots::blockSlotOf (strip->getHandle(), uid) : -1;
}

void PluginBlockCard::update()
{
    FxSlots::BlockInfo now;
    if (strip != nullptr)
        now = ctx.engine.getFx().describeBlock (strip->getHandle(), uid);
    const bool wasRack = rack;
    rack = strip != nullptr && strip->isBlockHostRack (uid);

    // The rack: each plug-in's name and bypass, and the hot knobs of the first plug-in that has any.
    juce::StringArray names;
    std::vector<bool> bypassed;
    std::array<Hot, 2> knobs;
    if (rack)
    {
        auto& fx = ctx.engine.getFx();
        const int s = slot();
        const int n = s >= 0 ? fx.getNumChainMembers (s) : 0;
        bool focused = false;
        for (int i = 0; i < n; ++i)
        {
            auto* plugin = fx.getChainMember (s, i);
            if (plugin == nullptr)
                continue;
            names.add (PluginHost::describe (*plugin).name);
            bypassed.push_back (fx.isChainMemberBypassed (s, i));
            const auto control = fx.getChainMemberControl (s, i);
            if (focused || ! control.hasHot())
                continue;
            focused = true;
            for (size_t k = 0; k < 2; ++k)
                if (auto* p = control.hot[k].find (*plugin))
                    knobs[k] = { true, p->getValue(), p->getName (8).toUpperCase(), p->getCurrentValueAsText() };
        }
    }
    const auto sameKnob = [] (const Hot& a, const Hot& b) { return a.assigned == b.assigned && a.name == b.name && a.text == b.text && std::abs (a.value - b.value) < 0.002f; };
    const bool same = now.name == shown.name && now.vendor == shown.vendor && now.loaded == shown.loaded && now.missing == shown.missing
                      && now.faulted == shown.faulted && now.bypassed == shown.bypassed && now.latency == shown.latency && wasRack == rack
                      && names == rackNames && bypassed == rackBypassed && sameKnob (knobs[0], hots[0]) && sameKnob (knobs[1], hots[1]);
    shown = now;
    rackNames = names;
    rackBypassed = bypassed;
    hots = knobs;
    editKey.setButtonText (rack ? "OPEN" : "EDIT");
    editKey.setEnabled (rack ? ctx.openHostRack != nullptr : shown.loaded);
    onKey.refresh();
    if (! same)
        repaint();
}

void PluginBlockCard::timerCallback()
{
    update();
}

void PluginBlockCard::openEditor()
{
    const int s = slot();
    if (s >= 0 && shown.loaded && ctx.openPluginEditor)
        ctx.openPluginEditor (s);
}

void PluginBlockCard::openRack()
{
    if (strip != nullptr && rack && ctx.openHostRack)
        ctx.openHostRack (*strip, uid);
}

//==============================================================================
juce::AudioProcessorParameter* PluginBlockCard::hotParam (int knob) const
{
    auto& fx = ctx.engine.getFx();
    const int s = slot();
    for (int i = 0, n = s >= 0 ? fx.getNumChainMembers (s) : 0; i < n; ++i)
    {
        const auto control = fx.getChainMemberControl (s, i);
        if (! control.hasHot())
            continue;
        auto* plugin = fx.getChainMember (s, i);
        return plugin != nullptr ? control.hot[(size_t) juce::jlimit (0, 1, knob)].find (*plugin) : nullptr;
    }
    return nullptr;
}

bool PluginBlockCard::turnHot (int knob, float delta)
{
    auto* p = rack ? hotParam (knob) : nullptr;
    if (p == nullptr)
        return false;
    if (p->isDiscrete() && p->getNumSteps() > 1)
    {
        const float step = 1.0f / (float) (p->getNumSteps() - 1);
        carried += delta;
        if (std::abs (carried) < step * 0.5f)
            return true;
        const int steps = juce::roundToInt (carried / step);
        carried -= (float) steps * step;
        delta = (float) steps * step;
    }
    p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + delta));
    update();
    return true;
}

juce::Rectangle<int> PluginBlockCard::getHotBounds (int knob) const
{
    // At the right of the card, above the keys: one column per knob.
    auto area = getLocalBounds().withTrimmedBottom (kKeysHeight).reduced (sizes::kGap, sizes::kGapSmall);
    area.removeFromTop (18);
    const int w = juce::jmin (68, area.getWidth() / 3);
    auto right = area.removeFromRight (2 * w);
    if (knob == 0)
        return right.removeFromLeft (w);
    return right.withTrimmedLeft (w);
}

void PluginBlockCard::mouseDown (const juce::MouseEvent& e)
{
    dragKnob = -1;
    if (rack)
        for (int k = 0; k < 2; ++k)
            if (hots[(size_t) k].assigned && getHotBounds (k).contains (e.getPosition()))
            {
                dragKnob = k;
                dragFrom = 0.0f;
                carried = 0.0f;
            }
}

void PluginBlockCard::mouseDrag (const juce::MouseEvent& e)
{
    if (dragKnob < 0)
        return;
    const float now = (float) -e.getDistanceFromDragStartY() / kKnobTravel;
    if (turnHot (dragKnob, now - dragFrom))
        dragFrom = now;
}

void PluginBlockCard::mouseUp (const juce::MouseEvent&)
{
    dragKnob = -1;
}

//==============================================================================
void PluginBlockCard::showMenu()
{
    const int s = slot();
    if (s < 0)
        return;
    auto& fx = ctx.engine.getFx();
    juce::Component::SafePointer<PluginBlockCard> safe (this);
    const auto chosen = [this, s, safe] (const juce::PluginDescription& desc)
    {
        if (loadPluginIntoFx (ctx, s, desc) && ctx.openPluginEditor)
            ctx.openPluginEditor (s);
        if (safe != nullptr)
            safe->update();
    };

    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeelFor (theme));
    const auto types = ctx.host.getSortedTypes();
    menu.addSubMenu (shown.loaded ? "Replace with" : "Choose plug-in", pluginTypesMenu (ctx, chosen), ! types.isEmpty());
    if (shown.loaded)
    {
        menu.addSubMenu ("Add to chain", pluginTypesMenu (ctx, [this, s, safe] (const juce::PluginDescription& desc)
                         {
                             if (addPluginToChain (ctx, s, desc) && ctx.openPluginEditor)
                                 ctx.openPluginEditor (s);
                             if (safe != nullptr)
                                 safe->update();
                         }),
                         ! types.isEmpty() && fx.getNumChainMembers (s) < PluginChain::kMaxMembers);
        if (shown.faulted)
            menu.addItem ("Clear fault", [this, s, safe]
            {
                ctx.engine.getFx().clearFlags (s);
                if (safe != nullptr)
                    safe->update();
            });
        menu.addItem ("Remove plug-in", [this, s, safe]
        {
            clearFx (ctx, s); // closes its editor first; the block stays, empty
            if (safe != nullptr)
                safe->update();
        });
    }
    menu.addSeparator();
    menu.addItem ("Scan for plugins...", [this] { if (ctx.showPluginScanner) ctx.showPluginScanner(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuKey).withStandardItemHeight (sizes::kTouch));
}

void PluginBlockCard::resized()
{
    auto keys = getLocalBounds().removeFromBottom (kKeysHeight).reduced (sizes::kGapSmall, 2);
    const int w = keys.getWidth() / 3;
    onKey.setBounds (keys.removeFromLeft (w).reduced (2, 0));
    editKey.setBounds (keys.removeFromLeft (w).reduced (2, 0));
    menuKey.setBounds (keys.reduced (2, 0));
}

void PluginBlockCard::paintRack (juce::Graphics& g, juce::Rectangle<float> area)
{
    const bool dim = shown.bypassed || shown.missing || ! shown.loaded;

    // The head: what it is, and the rack's latency.
    auto head = area.removeFromTop (18.0f);
    g.setFont (type::font (type::kMicro, true));
    g.setColour (theme.accent);
    g.drawText ("PLUGIN HOST RACK", head.removeFromLeft (head.getWidth() * 0.6f), juce::Justification::centredLeft, false);
    g.setColour (theme.textDim);
    const double sr = ctx.engine.getSampleRate();
    juce::String latency = juce::String (shown.latency) + " smp";
    if (sr > 0.0)
        latency << "  " << juce::String (1000.0 * shown.latency / sr, 1) << " ms";
    g.drawText (latency, head, juce::Justification::centredRight, false);

    // The plug-ins, one to a line.
    area.removeFromRight ((float) 2 * juce::jmin (68, juce::roundToInt (area.getWidth()) / 3)); // the hot knobs' room
    const float line = 18.0f;
    const int lines = juce::jmax (1, (int) (area.getHeight() / line));
    g.setFont (type::font (type::kCaption));
    if (rackNames.isEmpty())
    {
        g.setColour (theme.textDim);
        const auto text = shown.missing ? "NOT FOUND: " + shown.name : juce::String ("EMPTY - OPEN TO ADD PLUG-INS");
        g.drawFittedText (text, area.removeFromTop (line * 2.0f).toNearestInt(), juce::Justification::topLeft, 2, 0.8f);
    }
    else
    {
        const int shownLines = rackNames.size() > lines ? lines - 1 : rackNames.size();
        for (int i = 0; i < shownLines; ++i)
        {
            const bool off = shown.bypassed || (i < (int) rackBypassed.size() && rackBypassed[(size_t) i]);
            g.setColour (off ? theme.textDim : theme.text);
            auto row = area.removeFromTop (line);
            g.drawFittedText (juce::String (i + 1) + "  " + rackNames[i] + (off ? "  BYP" : ""), row.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        }
        if (shownLines < rackNames.size())
        {
            g.setColour (theme.textDim);
            g.drawText ("+" + juce::String (rackNames.size() - shownLines) + " MORE", area.removeFromTop (line), juce::Justification::centredLeft, false);
        }
    }
    if (shown.faulted || shown.missing || shown.bypassed)
    {
        g.setColour (shown.faulted || shown.missing ? theme.mute : theme.textDim);
        g.setFont (type::font (type::kMicro, true));
        g.drawText (shown.missing ? "NOT FOUND - BYPASSED" : shown.faulted ? "FAULT - BYPASSED" : "BYPASSED", area.removeFromBottom (14.0f), juce::Justification::centredLeft, false);
    }

    // The hot knobs: a small knob with its name and value.
    for (int k = 0; k < 2; ++k)
    {
        const auto cell = getHotBounds (k).toFloat();
        const auto& hot = hots[(size_t) k];
        auto c = cell;
        const auto textArea = c.removeFromBottom (26.0f);
        const float d = juce::jmin (c.getWidth() - 8.0f, c.getHeight());
        drawKnob (g, c.withSizeKeepingCentre (d, d), hot.value, false, theme, hot.assigned && ! dim);
        g.setColour (hot.assigned ? theme.text : theme.textDim);
        g.setFont (type::font (type::kMicro, true));
        auto text = textArea;
        g.drawFittedText (hot.assigned ? hot.name : "HOT " + juce::String (k + 1), text.removeFromTop (13.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kMicro));
        g.drawFittedText (hot.assigned ? hot.text : "-", text.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }
}

void PluginBlockCard::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced ((float) sizes::kGap, (float) sizes::kGapSmall);
    area.removeFromBottom ((float) kKeysHeight);
    if (rack)
    {
        paintRack (g, area);
        return;
    }
    const bool dim = shown.bypassed || shown.missing || ! shown.loaded;

    // The glyph: a plug (body, two pins, a cable).
    auto glyph = area.removeFromLeft (44.0f).removeFromTop (44.0f).reduced (6.0f);
    const auto colour = dim ? theme.textDim : theme.accent;
    g.setColour (colour);
    g.fillRoundedRectangle (glyph.withTrimmedTop (glyph.getHeight() * 0.3f).withTrimmedBottom (glyph.getHeight() * 0.25f), 4.0f);
    for (const float x : { 0.3f, 0.7f })
        g.fillRect (juce::Rectangle<float> (3.0f, glyph.getHeight() * 0.32f).withCentre ({ glyph.getX() + glyph.getWidth() * x, glyph.getY() + glyph.getHeight() * 0.15f }));
    g.fillRect (juce::Rectangle<float> (3.0f, glyph.getHeight() * 0.25f).withCentre ({ glyph.getCentreX(), glyph.getBottom() - glyph.getHeight() * 0.12f }));
    area.removeFromLeft ((float) sizes::kGap);

    const auto name = shown.name.isNotEmpty() ? shown.name : juce::String (shown.missing ? "plug-in" : "no plug-in");
    g.setColour (dim ? theme.textDim : theme.text);
    g.setFont (type::font (type::kValue, true));
    g.drawFittedText (name, area.removeFromTop (22.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption));
    g.drawFittedText (shown.vendor, area.removeFromTop (18.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

    juce::String status;
    if (shown.missing)
        status = "NOT FOUND - BYPASSED";
    else if (! shown.loaded)
        status = "TAP PLUG-IN TO CHOOSE";
    else if (shown.faulted)
        status = "FAULT - BYPASSED";
    else if (shown.bypassed)
        status = "BYPASSED";
    const double sr = ctx.engine.getSampleRate();
    if (shown.loaded && status.isEmpty())
        status = "LATENCY " + juce::String (shown.latency) + " smp" + (sr > 0.0 ? " (" + juce::String (1000.0 * shown.latency / sr, 1) + " ms)" : juce::String());
    else if (shown.loaded && shown.latency > 0)
        status << "  " << shown.latency << " smp";
    g.setColour (shown.faulted || shown.missing ? theme.mute : theme.textDim);
    g.drawFittedText (status, area.removeFromTop (18.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
}

} // namespace livedaw::ui::universal
