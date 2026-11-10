#include "ui/universal/RoutingView.h"

#include "dsp/DspUtils.h"
#include "ui/InterfacePage.h"
#include "ui/Knobs.h"

#include <algorithm>

namespace livedaw::ui::universal
{

namespace
{
    juce::String stripName (const Strip& s)
    {
        const auto name = s.getName();
        return s.getLabel() + (name.isNotEmpty() ? "  " + name : juce::String());
    }

} // namespace

juce::String outputPortName (int port)
{
    return juce::String (routing::info (routing::groupOfOutput (port)).prefix) + " " + juce::String (routing::outputNumber (port));
}

std::vector<int> outputPorts (MixerEngine& engine)
{
    // The profile's groups of interface outputs, in port order: the local
    // outputs always, the others when the interface reaches them (the WING's
    // AES50 B and C past 48 and 96; an X32's aux out, AES50, card and P16).
    const int outs = engine.getHardwareOutputNames().size();
    const auto& groups = routing::portGroups();
    std::vector<std::pair<int, routing::Group>> found;
    for (size_t i = 0; i < groups.size(); ++i)
    {
        const auto& p = groups[i];
        const auto g = (routing::Group) i;
        if (p.output != 0 && p.first >= 0 && p.first < 0x10000 && g != routing::Group::Recorder
            && (g == routing::Group::LocalOut || p.first < outs))
            found.emplace_back (p.first, g);
    }
    std::sort (found.begin(), found.end());
    std::vector<int> ports;
    for (const auto& [first, g] : found)
        for (int i = 0; i < routing::tileCount (g, outs); ++i)
            if (const int port = routing::outputAt (g, i); port >= 0)
                ports.push_back (port);
    return ports;
}

RoutingView::RoutingView (UiContext& c, const Theme& t)
    : inputsTab ("INPUTS", t.accent, t), outputsTab ("OUTPUTS", t.accent, t), interfaceTab ("INTERFACE", t.accent, t), oneToOneKey ("PATCH 1:1", t.accent, t),
      group (t), grid (t), ctx (c), theme (t)
{
    for (auto* k : { &inputsTab, &outputsTab, &interfaceTab })
        k->tab = true;
    inputsTab.isOn = [this] { return page == Page::Inputs; };
    inputsTab.onClick = [this] { showPage (Page::Inputs); };
    outputsTab.isOn = [this] { return page == Page::Outputs; };
    outputsTab.onClick = [this] { showPage (Page::Outputs); };
    interfaceTab.isOn = [this] { return page == Page::Interface; };
    interfaceTab.onClick = [this] { showPage (Page::Interface); };
    addAndMakeVisible (interfaceTab);
    group.caption = "GROUP";
    group.device = devices::wing; // the WING's port groups
    group.items = [this]
    {
        juce::StringArray names;
        for (auto g : groups())
            names.add (routing::info (g).name);
        return names;
    };
    group.current = [this]
    {
        const auto list = groups();
        const auto it = std::find (list.begin(), list.end(), getGroup());
        return it != list.end() ? (int) (it - list.begin()) : -1;
    };
    group.choose = [this] (int i)
    {
        const auto list = groups();
        if (juce::isPositiveAndBelow (i, (int) list.size()))
            setGroup (list[(size_t) i]);
    };
    oneToOneKey.setTooltip ("Patch the channels and aux in order from this group's sources (or drag across the points with the mouse)");
    oneToOneKey.onClick = [this]
    {
        juce::Component::SafePointer<RoutingView> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withTitle ("PATCH 1:1")
                                          .withMessage ("Patch the channels in order from " + juce::String (routing::info (getGroup()).name) + "? Their inputs are replaced.")
                                          .withButton ("PATCH")
                                          .withButton ("CANCEL")
                                          .withAssociatedComponent (this),
                                      [safe] (int result)
                                      {
                                          if (safe != nullptr && result == 1)
                                              safe->patchOneToOne();
                                      });
    };
    for (auto* child : std::initializer_list<juce::Component*> { &inputsTab, &outputsTab, &group, &oneToOneKey, &grid })
        addAndMakeVisible (child);
    rebuild();
    startTimerHz (10);
}

RoutingView::~RoutingView()
{
    stopTimer();
}

std::vector<routing::Group> RoutingView::groups() const
{
    using G = routing::Group;
    if (page == Page::Outputs)
    {
        const int outs = ctx.engine.getHardwareOutputNames().size();
        std::vector<G> list { G::LocalOut };
        if (outs > routing::kBlock)
            list.push_back (G::Aes50B);
        if (outs > 2 * routing::kBlock)
            list.push_back (G::Aes50C);
        return list;
    }
    const int ins = ctx.engine.getHardwareInputNames().size();
    std::vector<G> list { G::LocalIn };
    if (ins > routing::kBlock)
        list.push_back (G::Aes50B);
    if (ins > 2 * routing::kBlock)
        list.push_back (G::Aes50C);
    for (auto g : { G::Oscillator, G::WlivePlay, G::UsbPlayer, G::Bus, G::Main, G::Matrix, G::UserSignal })
        list.push_back (g);
    return list;
}

void RoutingView::showPage (Page p)
{
    page = p;
    inputsTab.refresh();
    outputsTab.refresh();
    interfaceTab.refresh();
    // The interface's page (Sidecar's own device) in place of the group and the grid.
    if (p == Page::Interface && interfacePage == nullptr)
    {
        interfacePage = std::make_unique<InterfacePage> (ctx);
        addChildComponent (*interfacePage);
    }
    if (interfacePage != nullptr)
        interfacePage->setVisible (p == Page::Interface);
    group.setVisible (p != Page::Interface);
    grid.setVisible (p != Page::Interface);
    oneToOneKey.setVisible (p == Page::Inputs);
    resized();
    if (p != Page::Interface)
        rebuild();
}

void RoutingView::setGroup (routing::Group g)
{
    (page == Page::Inputs ? inputGroup : outputGroup) = g;
    rebuild();
}

std::atomic<int32_t>* RoutingView::outputOf (const Signal& s) const
{
    if (s.strip != nullptr)
        return s.side == 0 ? &s.strip->params.outputLeft : &s.strip->params.outputRight;
    switch (s.side)
    {
        case 0:  return &ctx.engine.monitorOutputLeft;
        case 1:  return &ctx.engine.monitorOutputRight;
        case 2:  return &ctx.engine.phones.outputLeft;
        default: return &ctx.engine.phones.outputRight;
    }
}

void RoutingView::patchInput (Strip& s, int source)
{
    // As ROUTING > CHANNELS patches (RoutingPage): a strip's signal or a stereo
    // (or mid/side) source takes the pair, in stereo; a mono source the one input.
    auto& p = s.params;
    if (MixerEngine::isStripSource (source))
    {
        const int first = source - (source - ld::kStripSourceBase) % 2;
        p.inputLeft = first;
        p.inputRight = first + 1;
        p.stereo = true;
    }
    else if (auto* src = ctx.engine.getSource (MixerEngine::sourceIndex (source)); src != nullptr && src->mode.load() != ld::kSourceMono)
    {
        const int first = MixerEngine::sourcePort (MixerEngine::sourceIndex (source) & ~1);
        p.inputLeft = first;
        p.inputRight = first + 1;
        p.stereo = true;
    }
    else
    {
        p.inputLeft = source;
        p.inputRight = -1; // a stereo channel takes the next input
    }
}

void RoutingView::patchOneToOne()
{
    if (page != Page::Inputs)
        return;
    for (size_t i = 0; i < juce::jmin (inputStrips.size(), sources.size()); ++i)
        patchInput (*inputStrips[i], sources[i]);
    grid.refresh();
}

void RoutingView::rebuild()
{
    structureVersion = ctx.engine.getStructureVersion();
    inputStrips.clear();
    sources.clear();
    ports.clear();
    signals.clear();

    PatchGrid::Model model;
    if (page == Page::Inputs)
    {
        for (auto kind : { StripKind::Channel, StripKind::Aux })
            for (auto& s : ctx.engine.getStrips (kind))
                inputStrips.push_back (s.get());
        const int ins = ctx.engine.getHardwareInputNames().size();
        for (int i = 0; i < routing::tileCount (inputGroup, ins); ++i)
            if (const int src = routing::sourceAt (inputGroup, i); src >= 0)
                sources.push_back (src);

        model.numRows = [this] { return (int) inputStrips.size(); };
        model.numColumns = [this] { return (int) sources.size(); };
        model.rowName = [this] (int r) { return stripName (*inputStrips[(size_t) r]); };
        model.columnName = [this] (int c) { return routing::sourceLabel (sources[(size_t) c]); };
        model.state = [this] (int r, int c)
        {
            const auto& p = inputStrips[(size_t) r]->params;
            const int src = sources[(size_t) c], left = p.inputLeft.load(), right = p.inputRight.load();
            if (left == src)
                return PatchGrid::On;
            if (p.stereo.load() && left >= 0 && (right == src || (right < 0 && src == left + 1)))
                return PatchGrid::Pair;
            return PatchGrid::Off;
        };
        // A source's level over its column, and a channel's input level beside its name: whether the patch brings signal.
        model.columnLevel = [this] (int c)
        {
            const int src = sources[(size_t) c];
            return MixerEngine::isStripSource (src) ? 0.0f : fader::dbToPos (dsp::gainToDb (ctx.meters.getSource (MixerEngine::sourceIndex (src))));
        };
        model.rowLevel = [this] (int r)
        {
            auto* s = inputStrips[(size_t) r];
            return fader::dbToPos (dsp::gainToDb (juce::jmax (ctx.meters.get (s, MeterPoint::Trim, 0), ctx.meters.get (s, MeterPoint::Trim, 1))));
        };
        model.toggle = [this] (int r, int c)
        {
            auto& s = *inputStrips[(size_t) r];
            if (s.params.inputLeft.load() == sources[(size_t) c])
            {
                s.params.inputLeft = -1;
                s.params.inputRight = -1;
            }
            else
            {
                patchInput (s, sources[(size_t) c]);
            }
        };
    }
    else
    {
        const int outs = ctx.engine.getHardwareOutputNames().size();
        for (int i = 0; i < routing::tileCount (outputGroup, outs); ++i)
            if (const int port = routing::outputAt (outputGroup, i); port >= 0)
                ports.push_back (port);
        for (auto kind : { StripKind::Bus, StripKind::Main, StripKind::Matrix })
            for (auto& s : ctx.engine.getStrips (kind))
                for (int side = 0; side < 2; ++side)
                    signals.push_back ({ s.get(), side });
        for (int side = 0; side < 4; ++side)
            signals.push_back ({ nullptr, side }); // SPEAKERS L / R, PHONES L / R

        model.numRows = [this] { return (int) ports.size(); };
        model.numColumns = [this] { return (int) signals.size(); };
        model.rowName = [this] (int r) { return outputPortName (ports[(size_t) r]); };
        model.columnName = [this] (int c)
        {
            const auto& s = signals[(size_t) c];
            if (s.strip != nullptr)
                return s.strip->getLabel() + (s.side == 0 ? " L" : " R");
            static const char* monitor[] = { "SPK L", "SPK R", "PH L", "PH R" };
            return juce::String (monitor[s.side]);
        };
        // A bus, main or matrix side's output level over its column, and the monitor paths'.
        model.columnLevel = [this] (int c)
        {
            const auto& s = signals[(size_t) c];
            const float level = s.strip != nullptr ? ctx.meters.get (s.strip, MeterPoint::Output, juce::jmin (s.side, s.strip->getNumChannels() - 1))
                                : s.side < 2       ? ctx.meters.getMonitor (s.side)
                                                   : ctx.meters.getPhones (s.side - 2);
            return fader::dbToPos (dsp::gainToDb (level));
        };
        model.state = [this] (int r, int c) { return outputOf (signals[(size_t) c])->load() == ports[(size_t) r] ? PatchGrid::On : PatchGrid::Off; };
        model.toggle = [this] (int r, int c)
        {
            auto* out = outputOf (signals[(size_t) c]);
            const int port = ports[(size_t) r];
            if (out->load() == port)
            {
                out->store (-1);
            }
            else
            {
                ctx.engine.clearOutput (port); // one signal per port
                out->store (port);
            }
        };
    }
    grid.setModel (std::move (model));
    group.refresh();
}

void RoutingView::refresh()
{
    // Another console (a show load, a bus added): other rows and columns.
    if (ctx.engine.getStructureVersion() != structureVersion)
        rebuild();
    else
        grid.refresh();
    if (page != Page::Interface)
        grid.refreshLevels();
}

void RoutingView::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
}

void RoutingView::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop (sizes::kFilterBar).reduced (sizes::kGap, sizes::kGapSmall);
    inputsTab.setBounds (bar.removeFromLeft (140));
    bar.removeFromLeft (sizes::kGapSmall);
    outputsTab.setBounds (bar.removeFromLeft (140));
    bar.removeFromLeft (sizes::kGapSmall);
    interfaceTab.setButtonText (interfaceLabel (ctx.engine));
    interfaceTab.setBounds (bar.removeFromLeft (160));
    bar.removeFromLeft (sizes::kGapLarge);
    if (interfacePage != nullptr)
        interfacePage->setBounds (area.reduced (sizes::kGap, 0));
    group.setBounds (bar.removeFromLeft (300));
    bar.removeFromLeft (sizes::kGapSmall);
    oneToOneKey.setBounds (bar.removeFromLeft (130));
    grid.setBounds (area.reduced (sizes::kGap, 0).withTrimmedBottom (sizes::kGapSmall));
}

} // namespace livedaw::ui::universal
