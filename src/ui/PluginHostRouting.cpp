#include "ui/PluginHostRouting.h"

#include "engine/DirectOut.h"
#include "engine/PluginHost.h"

namespace livedaw::ui::ph
{
using namespace universal;

namespace
{
    constexpr int kArmMs = 4000;
} // namespace

RoutingPage::RoutingPage (UiContext& c, const universal::Theme& t)
    : ctx (c), theme (t), resetKey ("RESET 1:1", t.accent, t), inGrid (t), outGrid (t), keyGrid (t)
{
    const char* names[] = { "INPUTS", "OUTPUTS", "KEYS" };
    for (int i = 0; i < 3; ++i)
    {
        auto key = std::make_unique<ToggleKey> (names[i], theme.accent, theme);
        key->isOn = [this, i] { return view == static_cast<View> (i); };
        key->onClick = [this, i] { showView (static_cast<View> (i)); };
        addAndMakeVisible (*key);
        viewKeys[(size_t) i] = std::move (key);
    }
    resetKey.onClick = [this] { pressReset(); };
    resetKey.isOn = [this] { return armed; };
    resetKey.setTooltip ("Puts every channel back on its own interface input and output. Press twice.");
    addAndMakeVisible (resetKey);
    addAndMakeVisible (inGrid);
    addChildComponent (outGrid);
    addChildComponent (keyGrid);
    for (auto* grid : { &inGrid, &outGrid, &keyGrid })
        grid->setDragPans (true); // a drag moves around a grid this size; a tap patches
    build();
    rebuildKeyRows();
    startTimerHz (4);
}

RoutingPage::~RoutingPage()
{
    stopTimer();
}

Strip* RoutingPage::strip (int index) const
{
    const auto& list = ctx.engine.getChannels();
    return juce::isPositiveAndBelow (index, (int) list.size()) ? list[(size_t) index].get() : nullptr;
}

int RoutingPage::numChannels() const
{
    return (int) ctx.engine.getChannels().size();
}

//==============================================================================
juce::String RoutingPage::describeKey (PluginChain::Key key)
{
    return key.isNone() ? juce::String ("NONE") : "CH " + juce::String (key.channel + 1) + (key.post ? " OUT" : " IN");
}

int RoutingPage::keyColumn (PluginChain::Key key) noexcept
{
    return key.isNone() ? 0 : 1 + 2 * key.channel + (key.post ? 1 : 0);
}

PluginChain::Key RoutingPage::keyAt (int column) noexcept
{
    if (column <= 0)
        return {};
    return { (column - 1) / 2, ((column - 1) % 2) == 1 };
}

void RoutingPage::build()
{
    // INPUTS: a channel takes one interface input (the inputs are numbered as the channels are).
    PatchGrid::Model in;
    in.numRows = [this] { return numChannels(); };
    in.numColumns = [this] { return numChannels(); };
    in.rowName = [] (int r) { return "CH " + juce::String (r + 1); };
    in.columnName = [] (int c) { return "IN " + juce::String (c + 1); };
    in.state = [this] (int r, int c)
    {
        auto* s = strip (r);
        return s != nullptr && s->params.inputLeft.load() == c ? PatchGrid::On : PatchGrid::Off;
    };
    in.toggle = [this] (int r, int c)
    {
        auto* s = strip (r);
        if (s == nullptr)
            return;
        const bool on = s->params.inputLeft.load() == c;
        s->params.inputLeft = on ? -1 : c;
        s->params.inputRight = -1; // the channels are mono
    };
    inGrid.setModel (std::move (in));

    // OUTPUTS: an interface output plays one channel after its plug-ins (the engine's direct out, post fader).
    const auto source = [] (int channelIndex) { return DirectOut { StripKind::Channel, channelIndex + 1, 0, true }.code(); };
    const auto playing = [this] (int port)
    {
        const auto out = DirectOut::fromCode (ctx.engine.getOutputSource (port));
        return out.has_value() && out->kind == StripKind::Channel ? out->number - 1 : -1;
    };
    PatchGrid::Model out;
    out.numRows = [this] { return numChannels(); };
    out.numColumns = [this] { return numChannels(); };
    out.rowName = [] (int r) { return "OUT " + juce::String (r + 1); };
    out.columnName = [] (int c) { return "CH " + juce::String (c + 1); };
    out.state = [playing] (int r, int c) { return playing (r) == c ? PatchGrid::On : PatchGrid::Off; };
    out.toggle = [this, source, playing] (int r, int c)
    {
        if (playing (r) == c)
            ctx.engine.routeOutputSource (r, -1);
        else
            ctx.engine.routeOutputSource (r, source (c));
    };
    outGrid.setModel (std::move (out));

    // KEYS: the selected channel's plug-ins with a sidechain input, against NONE and every channel's IN and OUT.
    PatchGrid::Model keys;
    keys.numRows = [this] { return (int) keyRows.size(); };
    keys.numColumns = [this] { return 1 + 2 * numChannels(); };
    keys.rowName = [this] (int r) { return keyRows[(size_t) r].name; };
    keys.columnName = [] (int c) { return c == 0 ? juce::String ("NONE") : juce::String ((c - 1) / 2 + 1) + (((c - 1) % 2) == 1 ? " OUT" : " IN"); };
    keys.state = [this] (int r, int c)
    {
        return keyColumn (keyRows[(size_t) r].key) == c ? PatchGrid::On : PatchGrid::Off;
    };
    keys.toggle = [this] (int r, int c)
    {
        // A tap on the point that is lit takes the key away.
        setKey (r, keyColumn (keyRows[(size_t) r].key) == c ? PluginChain::Key {} : keyAt (c));
    };
    keyGrid.setModel (std::move (keys));
}

void RoutingPage::rebuildKeyRows()
{
    keyRows.clear();
    auto& fx = ctx.engine.getFx();
    if (auto* s = strip (channel))
        for (const auto& step : s->getChain())
        {
            if (! step.isBlock() || step.getBlockKind() != BlockKind::Plugin)
                continue;
            const int slot = FxSlots::blockSlotOf (s->getHandle(), step.uid);
            for (int m = 0; slot >= 0 && m < fx.getNumChainMembers (slot); ++m)
                if (fx.chainMemberHasSidechain (slot, m))
                    keyRows.push_back ({ slot, m, PluginHost::describe (*fx.getChainMember (slot, m)).name, fx.getChainMemberKey (slot, m) });
        }
    juce::String sig (channel);
    for (const auto& row : keyRows)
        sig << "|" << row.slot << "," << row.member << "," << row.name << "," << keyColumn (row.key);
    sig << "|" << numChannels();
    if (sig != keySignature)
    {
        keySignature = sig;
        keyGrid.relabel();
    }
    else
        keyGrid.refresh();
    keyGrid.setVisible (view == View::Keys && ! keyRows.empty());
}

bool RoutingPage::setKey (int row, PluginChain::Key key)
{
    if (! juce::isPositiveAndBelow (row, (int) keyRows.size()))
        return false;
    const auto r = keyRows[(size_t) row];
    // The engine refuses a channel keying itself from its own output; so do we, before asking.
    if (key.post && key.channel == channel)
        return false;
    const bool ok = ctx.engine.getFx().setChainMemberKey (r.slot, r.member, key);
    rebuildKeyRows();
    repaint();
    return ok;
}

juce::String RoutingPage::getKeysNote() const
{
    if (! keyRows.empty())
        return {};
    return "CH " + juce::String (channel + 1) + " has no plug-in with a sidechain input. A plug-in in the sandbox has none.";
}

//==============================================================================
void RoutingPage::showView (View v)
{
    view = v;
    inGrid.setVisible (v == View::Inputs);
    outGrid.setVisible (v == View::Outputs);
    keyGrid.setVisible (v == View::Keys && ! keyRows.empty());
    for (auto& k : viewKeys)
        k->refresh();
    refresh();
    repaint();
}

void RoutingPage::setChannel (int index)
{
    index = juce::jlimit (0, juce::jmax (0, numChannels() - 1), index);
    if (index == channel)
        return;
    channel = index;
    rebuildKeyRows();
    repaint();
}

void RoutingPage::refresh()
{
    inGrid.refresh();
    outGrid.refresh();
    // The KEYS rows follow the chain; the other points follow the engine.
    const auto before = keySignature;
    rebuildKeyRows();
    if (before != keySignature)
        repaint();
}

void RoutingPage::timerCallback()
{
    if (armed && (int) (juce::Time::getMillisecondCounter() - armedAt) > kArmMs)
        disarm();
    if (isVisible())
        refresh();
}

void RoutingPage::resetToDefaults()
{
    const int n = numChannels();
    for (int i = 0; i < n; ++i)
        if (auto* s = strip (i))
        {
            s->params.inputLeft = i;
            s->params.inputRight = -1;
            ctx.engine.routeOutputSource (i, DirectOut { StripKind::Channel, i + 1, 0, true }.code());
        }
    refresh();
}

void RoutingPage::pressReset()
{
    if (! armed)
    {
        armed = true;
        armedAt = juce::Time::getMillisecondCounter();
        resetKey.setButtonText ("PRESS AGAIN");
        resetKey.refresh();
        return;
    }
    disarm();
    resetToDefaults();
}

void RoutingPage::disarm()
{
    armed = false;
    resetKey.setButtonText ("RESET 1:1");
    resetKey.refresh();
}

//==============================================================================
void RoutingPage::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    auto head = getLocalBounds().removeFromTop (kHead).withTrimmedLeft (16 + 3 * 160);
    head.removeFromRight (240);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption));
    juce::String text;
    switch (view)
    {
        case View::Inputs:  text = "Each channel takes one interface input: the inputs across, the channels down. Tap to patch."; break;
        case View::Outputs: text = "Each interface output plays one channel after its plug-ins: the channels across, the outputs down."; break;
        case View::Keys:    text = "CH " + juce::String (channel + 1) + ": the key a plug-in's sidechain hears: a channel's input (IN) or output (OUT)."; break;
    }
    g.drawFittedText (text, head, juce::Justification::centredLeft, 2);

    if (view == View::Keys && keyRows.empty())
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kValue));
        g.drawFittedText (getKeysNote(), getLocalBounds().withTrimmedTop (kHead).reduced (60), juce::Justification::centred, 3);
    }
}

void RoutingPage::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (kHead).reduced (10, 10);
    for (auto& k : viewKeys)
        k->setBounds (head.removeFromLeft (152).withTrimmedRight (8));
    resetKey.setBounds (head.removeFromRight (232));
    const auto grid = r.reduced (8, 0).withTrimmedBottom (6);
    inGrid.setBounds (grid);
    outGrid.setBounds (grid);
    keyGrid.setBounds (grid);
}

} // namespace livedaw::ui::ph
