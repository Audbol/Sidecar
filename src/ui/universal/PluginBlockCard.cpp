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
    editKey.onClick = [this] { openEditor(); };
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
    const bool same = now.name == shown.name && now.vendor == shown.vendor && now.loaded == shown.loaded && now.missing == shown.missing
                      && now.faulted == shown.faulted && now.bypassed == shown.bypassed && now.latency == shown.latency;
    shown = now;
    editKey.setEnabled (shown.loaded);
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

void PluginBlockCard::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced ((float) sizes::kGap, (float) sizes::kGapSmall);
    area.removeFromBottom ((float) kKeysHeight);
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
