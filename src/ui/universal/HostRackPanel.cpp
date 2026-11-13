#include "ui/universal/HostRackPanel.h"

namespace livedaw::ui::universal
{

HostRackPanel::HostRackPanel (UiContext& c, const Theme& t)
    : ctx (c), theme (t), control (c, false), page (c, t), closeKey ("CLOSE", t.accent, t), keyKey ("KEY", t.accent, t)
{
    control.getFocus = [this] { return page.getFocus(); };
    page.setControl (&control);
    closeKey.isOn = [] { return false; };
    closeKey.onClick = [this]
    {
        if (onClose)
            onClose();
    };
    keyKey.isOn = [this] { return ! getKey().isNone(); };
    keyKey.onClick = [this] { showKeyMenu(); };
    keyKey.setTooltip ("The sidechain key of the selected plug-in: a channel's input or output");
    addAndMakeVisible (closeKey);
    addChildComponent (keyKey);
    addAndMakeVisible (page);
    startTimerHz (4);
}

HostRackPanel::~HostRackPanel()
{
    stopTimer();
}

void HostRackPanel::show (Strip& s, int uid)
{
    strip = &s;
    seenStructureVersion = ctx.engine.getStructureVersion();
    page.setStrip (&s, uid);
    resized();
    refreshKey();
    repaint();
}

int HostRackPanel::selectedSlot() const
{
    return strip != nullptr && page.getSelectedUid() >= 0 ? FxSlots::blockSlotOf (strip->getHandle(), page.getSelectedUid()) : -1;
}

int HostRackPanel::channelIndexOfStrip() const
{
    const auto& list = ctx.engine.getChannels();
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].get() == strip)
            return i;
    return -1;
}

//==============================================================================
bool HostRackPanel::isKeyAvailable() const
{
    const int slot = selectedSlot();
    // The keys are the channels' taps: a console with none has nothing to offer.
    return slot >= 0 && ! ctx.engine.getChannels().empty() && ctx.engine.getFx().chainMemberHasSidechain (slot, page.getDock().getMember());
}

PluginChain::Key HostRackPanel::getKey() const
{
    const int slot = selectedSlot();
    return slot >= 0 ? ctx.engine.getFx().getChainMemberKey (slot, page.getDock().getMember()) : PluginChain::Key {};
}

bool HostRackPanel::setKey (PluginChain::Key key)
{
    if (! isKeyAvailable())
        return false;
    // The engine refuses a channel keying its own plug-ins from its output; so do we, before asking.
    if (key.post && key.channel == channelIndexOfStrip())
        return false;
    const bool ok = ctx.engine.getFx().setChainMemberKey (selectedSlot(), page.getDock().getMember(), key);
    refreshKey();
    return ok;
}

std::vector<HostRackPanel::KeyChoice> HostRackPanel::getKeyChoices() const
{
    std::vector<KeyChoice> out;
    out.push_back ({ {}, "NONE", true });
    const auto& list = ctx.engine.getChannels();
    const int own = channelIndexOfStrip();
    for (int i = 0; i < (int) list.size(); ++i)
    {
        const auto label = list[(size_t) i]->getLabel().toUpperCase();
        out.push_back ({ { i, false }, label + " IN", true });
        out.push_back ({ { i, true }, label + " OUT", i != own });
    }
    return out;
}

void HostRackPanel::showKeyMenu()
{
    if (! isKeyAvailable())
        return;
    juce::Component::SafePointer<HostRackPanel> safe (this);
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeelFor (theme));
    const auto now = getKey();
    const auto choices = getKeyChoices();
    menu.addItem ("NONE", true, now.isNone(), [safe] { if (safe != nullptr) safe->setKey ({}); });
    const auto& list = ctx.engine.getChannels();
    for (int i = 0; i < (int) list.size(); ++i)
    {
        juce::PopupMenu sub;
        for (const bool post : { false, true })
        {
            const auto& choice = choices[(size_t) (1 + 2 * i + (post ? 1 : 0))];
            sub.addItem (post ? "OUTPUT (after its blocks)" : "INPUT (before its blocks)", choice.allowed, now == choice.key,
                         [safe, key = choice.key] { if (safe != nullptr) safe->setKey (key); });
        }
        const auto name = list[(size_t) i]->getName();
        menu.addSubMenu (list[(size_t) i]->getLabel().toUpperCase() + (name.isNotEmpty() ? "  " + name : juce::String()), sub, true, juce::Image(), ! now.isNone() && now.channel == i);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyKey).withStandardItemHeight (sizes::kTouch).withMinimumWidth (260));
}

void HostRackPanel::refreshKey()
{
    const bool available = isKeyAvailable();
    const auto key = getKey();
    keyKey.setButtonText (key.isNone() ? juce::String ("KEY: NONE") : "KEY: CH " + juce::String (key.channel + 1) + (key.post ? " OUT" : " IN"));
    if (keyKey.isVisible() != available)
    {
        keyKey.setVisible (available);
        resized();
    }
    keyKey.refresh();
}

void HostRackPanel::timerCallback()
{
    if (strip == nullptr)
        return;
    // The strip went (a show loaded, a channel deleted): the panel closes. Addresses are only compared here.
    const int version = ctx.engine.getStructureVersion();
    if (version != seenStructureVersion)
    {
        seenStructureVersion = version;
        bool live = false;
        for (int k = 0; k < kNumStripKinds && ! live; ++k)
            for (auto& s : ctx.engine.getStrips (static_cast<StripKind> (k)))
                live = live || s.get() == strip;
        if (! live)
        {
            strip = nullptr;
            page.setStrip (nullptr);
            if (onClose)
                onClose();
            return;
        }
    }
    refreshKey();
}

//==============================================================================
void HostRackPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    g.setColour (theme.outline);
    g.drawRect (getLocalBounds(), 2);
    auto head = getLocalBounds().removeFromTop (kHeader).reduced (sizes::kGapLarge, 0);
    g.setColour (theme.accent);
    g.setFont (type::font (type::kHeading, true));
    g.drawText ("PLUGIN HOST RACK", head.removeFromLeft (200), juce::Justification::centredLeft);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption));
    if (strip != nullptr)
        g.drawText (strip->getLabel().toUpperCase() + (strip->getName().isNotEmpty() ? "   " + strip->getName() : juce::String()),
                    head.withTrimmedRight (260), juce::Justification::centredLeft);
    g.setColour (theme.outline);
    g.drawHorizontalLine (kHeader - 1, 0.0f, (float) getWidth());
}

void HostRackPanel::resized()
{
    auto area = getLocalBounds().reduced (2);
    auto head = area.removeFromTop (kHeader - 2).reduced (sizes::kGap, 6);
    closeKey.setBounds (head.removeFromRight (120));
    head.removeFromRight (sizes::kGapSmall);
    keyKey.setBounds (head.removeFromRight (200));
    // The rack is drawn in its own design space; a narrower window scales it down.
    const float s = juce::jmin (1.0f, (float) area.getWidth() / (float) kDesignWidth);
    page.setTransform (juce::AffineTransform::scale (s).translated ((float) area.getX(), (float) area.getY()));
    page.setBounds (0, 0, juce::roundToInt ((float) area.getWidth() / s), juce::roundToInt ((float) area.getHeight() / s));
}

} // namespace livedaw::ui::universal
