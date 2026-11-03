#include "ui/X32Surface.h"

#include "ui/MeterBank.h"

namespace livedaw::ui
{

using namespace universal;

namespace
{
    constexpr int kTopBar = 48;
    constexpr int kLayerColumn = 112;
} // namespace

using x32::kKeyHeight;
using x32::kScribble;


//==============================================================================
X32Surface::X32Surface (UiContext& c)
    : ctx (c), theme (Theme::dark()), universalKey ("UNIVERSAL MIXER", theme.accent, theme), channel (c, theme), home (c, theme), routing (c, theme),
      setup (c, theme), library (c, theme), effects (c, theme), monitor (c, theme)
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

    const std::pair<const char*, InputLayer> inputLayers[] = { { "CH 1-16", InputLayer::Ch1to16 },
                                                               { "CH 17-32", InputLayer::Ch17to32 },
                                                               { "AUX / FX RTN", InputLayer::AuxFx },
                                                               { "BUS MASTER", InputLayer::BusMasters } };
    for (auto [name, layer] : inputLayers)
    {
        auto key = std::make_unique<ToggleKey> (name, theme.accent, theme);
        key->isOn = [this, l = layer] { return inputLayer == l; };
        key->onClick = [this, l = layer] { setInputLayer (l); };
        addAndMakeVisible (*key);
        inputLayerKeys.push_back (std::move (key));
    }
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
    const std::pair<const char*, Page> pages[] = { { "HOME", Page::Home },       { "ROUTING", Page::Routing }, { "SETUP", Page::Setup },
                                                   { "LIBRARY", Page::Library }, { "EFFECTS", Page::Effects }, { "MONITOR", Page::Monitor } };
    for (auto [name, target] : pages)
    {
        auto key = std::make_unique<ToggleKey> (name, theme.accent, theme);
        key->isOn = [this, p = target] { return page == p; };
        key->onClick = [this, p = target] { showPage (p); };
        addAndMakeVisible (*key);
        pageKeys.push_back (std::move (key));
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

    addAndMakeVisible (channel);
    channel.onView = [this] { showPage (Page::Home); };
    addAndMakeVisible (home);
    for (auto* v : std::initializer_list<juce::Component*> { &routing, &setup, &library, &effects, &monitor })
        addChildComponent (v);
    refreshAll();
    startTimerHz (30);
}

X32Surface::~X32Surface()
{
    stopTimer();
}

x32::FaderStrip& X32Surface::getInputStrip (int i)
{
    return *inputs[(size_t) juce::jlimit (0, 15, i)];
}

x32::FaderStrip& X32Surface::getGroupStrip (int i)
{
    return *groups[(size_t) juce::jlimit (0, 7, i)];
}

x32::FaderStrip& X32Surface::getMainStrip()
{
    return *main;
}

void X32Surface::setInputLayer (InputLayer l)
{
    inputLayer = l;
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

    for (int i = 0; i < 16; ++i)
    {
        Strip* s = nullptr;
        switch (inputLayer)
        {
            case InputLayer::Ch1to16:    s = at (e.getChannels(), i); break;
            case InputLayer::Ch17to32:   s = at (e.getChannels(), 16 + i); break;
            case InputLayer::AuxFx:      s = at (e.getAux(), i); break; // aux in 1-8, FX returns 1-8
            case InputLayer::BusMasters: s = at (e.getBuses(), i); break;
        }
        inputs[(size_t) i]->setStrip (s);
    }
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
    for (auto* keys : { &inputLayerKeys, &groupLayerKeys })
        for (auto& k : *keys)
            k->refresh();
    timerCallback(); // the faders, meters and keys at once
}

void X32Surface::showPage (Page p)
{
    page = p;
    home.setVisible (p == Page::Home);
    routing.setVisible (p == Page::Routing);
    setup.setVisible (p == Page::Setup);
    library.setVisible (p == Page::Library);
    effects.setVisible (p == Page::Effects);
    monitor.setVisible (p == Page::Monitor);
    switch (p)
    {
        case Page::Home:    break;
        case Page::Routing: routing.refresh(); break;
        case Page::Setup:   setup.refresh(); break;
        case Page::Library: library.refresh(); break;
        case Page::Effects: effects.refresh(); break;
        case Page::Monitor: monitor.refresh(); break;
    }
    for (auto& k : pageKeys)
        k->refresh();
}

void X32Surface::refreshAll()
{
    assignBanks();
    home.setStrip (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr);
    channel.setStrip (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr);
    showPage (page);
    repaint();
}

void X32Surface::selectionChanged()
{
    home.setStrip (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr);
    channel.setStrip (ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr);
    timerCallback(); // the SELECT keys
    repaint (channelArea);
}

void X32Surface::timerCallback()
{
    if (isCovered())
        return;
    for (auto& s : inputs)
        s->refresh();
    for (auto& s : groups)
        s->refresh();
    main->refresh();
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
    label (*inputs.front(), *inputs.back(), "INPUT CHANNELS");
    label (*groups.front(), *groups.back(), "GROUPS / BUSES / DCA");
    label (*main, *main, "MAIN");
}

void X32Surface::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop (kTopBar).reduced (sizes::kGapLarge, 8);
    universalKey.setBounds (bar.removeFromRight (170));
    area.reduce (sizes::kGapLarge, 0);
    area.removeFromBottom (sizes::kGapLarge);

    // The upper half: the channel strip section and the display.
    auto upper = area.removeFromTop (juce::roundToInt (area.getHeight() * 0.44f));
    area.removeFromTop (sizes::kGapLarge);
    displayArea = upper.removeFromRight (juce::roundToInt (upper.getWidth() * 0.42f)).reduced (10);
    upper.removeFromRight (sizes::kGapLarge);
    channelArea = upper;
    channel.setBounds (channelArea.reduced (12).withTrimmedTop (22 + 24 + sizes::kGap));

    auto display = displayArea;
    auto keys = display.removeFromBottom (36);
    display.removeFromBottom (sizes::kGapSmall);
    const int keyWidth = (keys.getWidth() - ((int) pageKeys.size() - 1) * sizes::kGapSmall) / (int) pageKeys.size();
    for (auto& k : pageKeys)
    {
        k->setBounds (keys.removeFromLeft (keyWidth));
        keys.removeFromLeft (sizes::kGapSmall);
    }
    for (auto* v : std::initializer_list<juce::Component*> { &home, &routing, &setup, &library, &effects, &monitor })
        v->setBounds (display);

    // The faders: input bank, its layer keys, group bank, its layer keys, main.
    fadersArea = area;
    auto faders = area.reduced (12).withTrimmedTop (22);
    auto layerColumn = [&] (std::vector<std::unique_ptr<ToggleKey>>& keyList)
    {
        auto column = faders.removeFromLeft (kLayerColumn);
        faders.removeFromLeft (sizes::kGap);
        column.removeFromTop (kScribble + sizes::kGapSmall);
        for (auto& k : keyList)
        {
            k->setBounds (column.removeFromTop (40));
            column.removeFromTop (sizes::kGapSmall);
        }
    };
    const int stripWidth = (faders.getWidth() - 2 * (kLayerColumn + sizes::kGap) - 3 * sizes::kGapLarge) / 25;
    layerColumn (inputLayerKeys);
    for (auto& s : inputs)
        s->setBounds (faders.removeFromLeft (stripWidth));
    faders.removeFromLeft (sizes::kGapLarge);
    layerColumn (groupLayerKeys);
    for (auto& s : groups)
        s->setBounds (faders.removeFromLeft (stripWidth));
    faders.removeFromLeft (2 * sizes::kGapLarge);
    main->setBounds (faders.removeFromLeft (stripWidth));
}

} // namespace livedaw::ui
