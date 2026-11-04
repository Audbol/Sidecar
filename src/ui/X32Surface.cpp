#include "ui/X32Surface.h"

#include "ui/MeterBank.h"

namespace livedaw::ui
{

using namespace universal;

namespace
{
    constexpr int kTopBar = 48;
    constexpr int kLayerColumn = 112;

    /** An input layer: its key, the strips (0 channels, 1 aux, 2 buses) from the first. */
    struct InputLayerSpec
    {
        const char* name;
        int list, first;
    };
    constexpr InputLayerSpec kFullLayers[] = { { "CH 1-16", 0, 0 }, { "CH 17-32", 0, 16 }, { "AUX / FX RTN", 1, 0 }, { "BUS MASTER", 2, 0 } };
    constexpr InputLayerSpec kCompactLayers[] = { { "CH 1-8", 0, 0 },        { "CH 9-16", 0, 8 }, { "CH 17-24", 0, 16 }, { "CH 25-32", 0, 24 },
                                                  { "AUX IN / USB", 1, 0 }, { "FX RTN", 1, 8 },  { "BUS 1-8", 2, 0 },   { "BUS 9-16", 2, 8 } };

    std::vector<InputLayerSpec> layersOf (X32Surface::Model m)
    {
        if (m == X32Surface::Model::Compact)
            return { std::begin (kCompactLayers), std::end (kCompactLayers) };
        return { std::begin (kFullLayers), std::end (kFullLayers) };
    }
} // namespace

using x32::kKeyHeight;
using x32::kScribble;


//==============================================================================
X32Surface::X32Surface (UiContext& c)
    : ctx (c), theme (Theme::dark()), universalKey ("UNIVERSAL MIXER", theme.accent, theme),
      sendsOnFaderKey ("SENDS ON FADER", juce::Colour (0xfff0a020), theme), channel (c, theme),
      display (c, theme)
{
    for (auto& s : inputs)
    {
        s = std::make_unique<x32::FaderStrip> (ctx, theme);
        addAndMakeVisible (*s);
    }
    for (auto& s : groups)
    {
        s = std::make_unique<x32::FaderStrip> (ctx, theme);
        addAndMakeVisible (*s);
    }
    main = std::make_unique<x32::FaderStrip> (ctx, theme);
    addAndMakeVisible (*main);

    model = ctx.settings.getValue (modelKey) == "COMPACT" ? Model::Compact : Model::Full;
    buildInputLayerKeys();
    for (int i = 0; i < 16; ++i)
        inputs[(size_t) i]->setVisible (i < getNumInputFaders());
    const std::pair<const char*, GroupLayer> groupLayers[] = { { "DCA 1-8", GroupLayer::Dca },
                                                               { "BUS 1-8", GroupLayer::Bus1to8 },
                                                               { "BUS 9-16", GroupLayer::Bus9to16 },
                                                               { "MTX 1-6 / M/C", GroupLayer::MatrixMain } };
    for (auto [name, layer] : groupLayers)
    {
        auto key = std::make_unique<ToggleKey> (name, theme.accent, theme);
        key->isOn = [this, l = layer] { return groupLayer == l; };
        key->onClick = [this, l = layer] { setGroupLayer (l); };
        addAndMakeVisible (*key);
        groupLayerKeys.push_back (std::move (key));
    }

    // Switching rebuilds the window's skin: let the click return first.
    universalKey.onClick = [&context = ctx]
    {
        juce::MessageManager::callAsync ([&context]
        {
            if (context.setInterface)
                context.setInterface (InterfaceKind::Universal);
        });
    };
    universalKey.setTooltip ("The Universal Mixer");
    addAndMakeVisible (universalKey);
    sendsOnFaderKey.isOn = [this] { return sendsOnFader; };
    sendsOnFaderKey.onClick = [this] { setSendsOnFader (! sendsOnFader); };
    sendsOnFaderKey.setTooltip ("Sends on fader: select a bus for the inputs' sends to it, or a channel for its sends to the buses");
    addAndMakeVisible (sendsOnFaderKey);

    addAndMakeVisible (channel);
    // A VIEW key opens its section's tab of the display's HOME screen.
    channel.onView = [this] (int section) { display.showScreen (x32::Display::Screen::Home, section + 1); };
    display.getModel = [this] { return (int) model; };
    display.setModel = [this] (int m) { setModel (m == 1 ? Model::Compact : Model::Full); };
    addAndMakeVisible (display);
    refreshAll();
    startTimerHz (30);
}

X32Surface::~X32Surface()
{
    stopTimer();
}

x32::FaderStrip& X32Surface::getInputStrip (int i)
{
    return *inputs[(size_t) juce::jlimit (0, getNumInputFaders() - 1, i)];
}

x32::FaderStrip& X32Surface::getGroupStrip (int i)
{
    return *groups[(size_t) juce::jlimit (0, 7, i)];
}

x32::FaderStrip& X32Surface::getMainStrip()
{
    return *main;
}

juce::StringArray X32Surface::getInputLayerNames() const
{
    juce::StringArray names;
    for (auto& l : layersOf (model))
        names.add (l.name);
    return names;
}

void X32Surface::buildInputLayerKeys()
{
    inputLayerKeys.clear();
    const auto layers = layersOf (model);
    for (int i = 0; i < (int) layers.size(); ++i)
    {
        auto key = std::make_unique<ToggleKey> (layers[(size_t) i].name, theme.accent, theme);
        key->isOn = [this, i] { return inputLayer == i; };
        key->onClick = [this, i] { setInputLayer (i); };
        addAndMakeVisible (*key);
        inputLayerKeys.push_back (std::move (key));
    }
}

void X32Surface::setModel (Model m)
{
    if (m == model)
        return;
    model = m;
    ctx.settings.setValue (modelKey, m == Model::Compact ? "COMPACT" : "X32");
    // The same strips stay up where the new model has them (CH 17-32: CH 17-24).
    const auto from = layersOf (m == Model::Compact ? Model::Full : Model::Compact)[(size_t) inputLayer];
    const auto layers = layersOf (m);
    inputLayer = 0;
    for (int i = 0; i < (int) layers.size(); ++i)
        if (layers[(size_t) i].list == from.list && layers[(size_t) i].first == from.first)
            inputLayer = i;
    buildInputLayerKeys();
    for (int i = 0; i < 16; ++i)
        inputs[(size_t) i]->setVisible (i < getNumInputFaders());
    assignBanks();
    resized();
    repaint();
}

void X32Surface::setInputLayer (int l)
{
    inputLayer = juce::jlimit (0, (int) layersOf (model).size() - 1, l);
    assignBanks();
}

void X32Surface::setGroupLayer (GroupLayer l)
{
    groupLayer = l;
    assignBanks();
}

void X32Surface::assignBanks()
{
    auto& e = ctx.engine;
    auto at = [] (const MixerEngine::StripList& list, int i) -> Strip*
    { return juce::isPositiveAndBelow (i, (int) list.size()) ? list[(size_t) i].get() : nullptr; };

    const auto layer = layersOf (model)[(size_t) inputLayer];
    const auto& list = layer.list == 0 ? e.getChannels() : layer.list == 1 ? e.getAux() : e.getBuses();
    for (int i = 0; i < 16; ++i)
        inputs[(size_t) i]->setStrip (i < getNumInputFaders() ? at (list, layer.first + i) : nullptr);
    for (int i = 0; i < 8; ++i)
    {
        auto& g = *groups[(size_t) i];
        switch (groupLayer)
        {
            case GroupLayer::Dca:
                if (i < e.getNumDcas())
                    g.setDca (i);
                else
                    g.clear();
                break;
            case GroupLayer::Bus1to8:  g.setStrip (at (e.getBuses(), i)); break;
            case GroupLayer::Bus9to16: g.setStrip (at (e.getBuses(), 8 + i)); break;
            case GroupLayer::MatrixMain:
                // Matrices 1-6, then the M/C (main 2) on the last fader.
                g.setStrip (i < 6 ? at (e.getMatrices(), i) : i == 7 ? at (e.getMains(), 1) : nullptr);
                break;
        }
    }
    main->setStrip (at (e.getMains(), 0));
    applySendsOnFader();
    for (auto* keys : { &inputLayerKeys, &groupLayerKeys })
        for (auto& k : *keys)
            k->refresh();
    timerCallback(); // the faders, meters and keys at once
}

void X32Surface::setSendsOnFader (bool on)
{
    sendsOnFader = on;
    assignBanks();
    sendsOnFaderKey.refresh();
}

void X32Surface::applySendsOnFader()
{
    auto* sel = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    sofShown = sel;
    for (int i = 0; i < getNumInputFaders(); ++i)
        inputs[(size_t) i]->setSend (nullptr, {});
    for (auto& g : groups)
        g->setSend (nullptr, {});
    if (! sendsOnFader || sel == nullptr)
        return;
    if (sel->getKind() == StripKind::Bus)
    {
        // The inputs' sends to the selected bus.
        const int slot = sel->getId();
        const auto to = "BUS " + juce::String (slot + 1);
        for (int i = 0; i < getNumInputFaders(); ++i)
            if (auto* s = inputs[(size_t) i]->getStrip(); s != nullptr && s->isInput())
                inputs[(size_t) i]->setSend (&s->sends[(size_t) slot].levelDb, to);
        // A bus's sends to the matrices, on the MTX layer.
        if (groupLayer == GroupLayer::MatrixMain)
            for (int i = 0; i < juce::jmin (6, (int) ctx.engine.getMatrices().size()); ++i)
                groups[(size_t) i]->setSend (&sel->matrix[(size_t) i].levelDb, "FROM BUS " + juce::String (slot + 1));
        return;
    }
    if (sel->isInput() && (groupLayer == GroupLayer::Bus1to8 || groupLayer == GroupLayer::Bus9to16))
    {
        // The selected channel's sends to buses 1-8 or 9-16.
        const int first = groupLayer == GroupLayer::Bus1to8 ? 0 : 8;
        for (int i = 0; i < 8; ++i)
            if (groups[(size_t) i]->getStrip() != nullptr)
                groups[(size_t) i]->setSend (&sel->sends[(size_t) (first + i)].levelDb, "FROM " + sel->getLabel());
    }
}

void X32Surface::refreshAll()
{
    assignBanks();
    channel.setStrip (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr);
    display.refreshStrip();
    repaint();
}

void X32Surface::selectionChanged()
{
    channel.setStrip (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr);
    if (sendsOnFader && sofShown != (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr))
        applySendsOnFader(); // the sends follow the selection
    timerCallback(); // the SELECT keys
    display.refreshStrip();
    repaint (channelArea);
}

void X32Surface::timerCallback()
{
    if (isCovered())
        return;
    for (int i = 0; i < getNumInputFaders(); ++i)
        inputs[(size_t) i]->refresh();
    for (auto& s : groups)
        s->refresh();
    main->refresh();
    sendsOnFaderKey.refresh();
}

//==============================================================================
juce::Rectangle<int> X32Surface::channelSectionArea() const
{
    return channelArea;
}

void X32Surface::paint (juce::Graphics& g)
{
    // The console: a dark body, its sections as raised panels.
    g.fillAll (juce::Colour (0xff17191c));
    auto panel = [&g] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (juce::Colour (0xff22252a));
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (juce::Colour (0xff30343a));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
        if (title.isNotEmpty())
        {
            g.setColour (juce::Colour (0xff8b9099));
            g.setFont (type::font (type::kCaption, true));
            g.drawText (title, r.reduced (12, 6).removeFromTop (16), juce::Justification::centredLeft);
        }
    };

    // The top bar: what this is.
    auto bar = getLocalBounds().removeFromTop (kTopBar).reduced (sizes::kGapLarge, 0);
    g.setColour (theme.text);
    g.setFont (type::font (type::kTitle, true));
    g.drawText ("X32", bar.removeFromLeft (60), juce::Justification::centredLeft);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kLabel));
    g.drawText ("Sidecar's X32 surface  -  not affiliated with Behringer", bar.removeFromLeft (420), juce::Justification::centredLeft);

    panel (channelArea, "CHANNEL STRIP");
    panel (displayArea.expanded (10), {});
    panel (fadersArea, {});

    // The channel strip section (step 8c): its sections, for the selected strip.
    auto* selected = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    auto inner = channelArea.reduced (12).withTrimmedTop (22);
    g.setColour (theme.text);
    g.setFont (type::font (type::kValue, true));
    g.drawText (selected != nullptr ? selected->getLabel() + "   " + selected->getName() : "No strip selected", inner.removeFromTop (24),
                juce::Justification::centredLeft);

    // The bank labels.
    g.setColour (juce::Colour (0xff8b9099));
    g.setFont (type::font (type::kCaption, true));
    auto label = [&g] (juce::Component& first, juce::Component& last, const juce::String& text)
    {
        const auto r = first.getBounds().getUnion (last.getBounds());
        g.drawText (text, r.withY (first.getY() - 18).withHeight (16), juce::Justification::centredLeft);
    };
    label (*inputs.front(), *inputs[(size_t) getNumInputFaders() - 1], "INPUT CHANNELS");
    label (*groups.front(), *groups.back(), "GROUPS / BUSES / DCA");
    label (*main, *main, "MAIN");
}

void X32Surface::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop (kTopBar).reduced (sizes::kGapLarge, 8);
    universalKey.setBounds (bar.removeFromRight (170));
    bar.removeFromRight (sizes::kGap);
    sendsOnFaderKey.setBounds (bar.removeFromRight (170));
    area.reduce (sizes::kGapLarge, 0);
    area.removeFromBottom (sizes::kGapLarge);

    // The upper half: the channel strip section and the display.
    auto upper = area.removeFromTop (juce::roundToInt (area.getHeight() * 0.44f));
    area.removeFromTop (sizes::kGapLarge);
    displayArea = upper.removeFromRight (juce::roundToInt (upper.getWidth() * 0.42f)).reduced (10);
    upper.removeFromRight (sizes::kGapLarge);
    channelArea = upper;
    channel.setBounds (channelArea.reduced (12).withTrimmedTop (22 + 24 + sizes::kGap));

    display.setBounds (displayArea);

    // The faders: input bank, its layer keys, group bank, its layer keys, main.
    fadersArea = area;
    auto faders = area.reduced (12).withTrimmedTop (22);
    auto layerColumn = [&] (std::vector<std::unique_ptr<ToggleKey>>& keyList)
    {
        auto column = faders.removeFromLeft (kLayerColumn);
        faders.removeFromLeft (sizes::kGap);
        column.removeFromTop (kScribble + sizes::kGapSmall);
        const int keyHeight = juce::jmin (40, column.getHeight() / juce::jmax (1, (int) keyList.size()) - sizes::kGapSmall);
        for (auto& k : keyList)
        {
            k->setBounds (column.removeFromTop (keyHeight));
            column.removeFromTop (sizes::kGapSmall);
        }
    };
    const int stripWidth = (faders.getWidth() - 2 * (kLayerColumn + sizes::kGap) - 3 * sizes::kGapLarge) / (getNumInputFaders() + 9);
    layerColumn (inputLayerKeys);
    for (int i = 0; i < getNumInputFaders(); ++i)
        inputs[(size_t) i]->setBounds (faders.removeFromLeft (stripWidth));
    faders.removeFromLeft (sizes::kGapLarge);
    layerColumn (groupLayerKeys);
    for (auto& s : groups)
        s->setBounds (faders.removeFromLeft (stripWidth));
    faders.removeFromLeft (2 * sizes::kGapLarge);
    main->setBounds (faders.removeFromLeft (stripWidth));
}

} // namespace livedaw::ui
