#include "ui/universal/FxRackView.h"

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kTileW = 176, kTileH = 76;
    /** The WING's front panels are about this wide for their height. */
    constexpr float kPanelAspect = 2.9f;

    juce::String deviceName (FxSlots& fx, int slot)
    {
        const int model = fx.getModel (slot);
        if (model == ld::kFxNone && ! fx.hasPlugin (slot))
            return "EMPTY";
        // describe(): "FX 3: Hall Reverb", or the plugin's name.
        const auto text = fx.describe (slot);
        return text.containsChar (':') ? text.fromFirstOccurrenceOf (":", false, false).trim() : text;
    }
} // namespace

/** The slots' tiles; a tap selects one. */
class FxRackView::Tiles final : public juce::Component
{
public:
    explicit Tiles (FxRackView& o) : owner (o) {}

    struct Info
    {
        juce::String name, device;
        int users = 0;
        float level = 0.0f;
        bool operator!= (const Info& o) const { return name != o.name || device != o.device || users != o.users || std::abs (level - o.level) > 0.01f; }
    };
    std::vector<Info> infos;

    int perRow() const { return juce::jmax (1, getWidth() / (kTileW + sizes::kGap)); }
    int heightFor (int count) const { return ((count + perRow() - 1) / perRow()) * (kTileH + sizes::kGap); }
    juce::Rectangle<int> tile (int i) const
    {
        return { (i % perRow()) * (kTileW + sizes::kGap), (i / perRow()) * (kTileH + sizes::kGap), kTileW, kTileH };
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.theme;
        for (int i = 0; i < (int) infos.size(); ++i)
        {
            const auto& info = infos[(size_t) i];
            const auto r = tile (i).toFloat();
            const bool sel = i == owner.selected;
            g.setColour (sel ? t.raised.brighter (0.1f) : t.raised);
            g.fillRoundedRectangle (r, sizes::kRadius);
            g.setColour (sel ? t.accent : t.outline);
            g.drawRoundedRectangle (r.reduced (0.5f), sizes::kRadius, sel ? 2.0f : 1.0f);
            auto inner = r.reduced (10.0f, 6.0f);
            auto top = inner.removeFromTop (18.0f);
            g.setColour (t.textDim);
            g.setFont (type::font (type::kCaption, true));
            g.drawText ("FX " + juce::String (i + 1), top, juce::Justification::centredLeft);
            // A built-in device is the WING's FX rack's.
            drawDeviceTag (g, top, info.device, t);
            g.setColour (info.name == "EMPTY" ? t.textDim : t.text);
            g.setFont (type::font (type::kLabel, true));
            g.drawFittedText (info.name, inner.removeFromTop (22.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (t.textDim);
            g.setFont (type::font (type::kCaption));
            g.drawText (info.users == 0 ? juce::String ("not inserted") : juce::String (info.users) + (info.users == 1 ? " insert" : " inserts"),
                        inner.removeFromTop (16.0f), juce::Justification::centredLeft);
            // Its output level.
            const auto bar = juce::Rectangle<float> (r.getX() + 10.0f, r.getBottom() - 6.0f, r.getWidth() - 20.0f, 3.0f);
            g.setColour (t.well);
            g.fillRect (bar);
            g.setColour (t.meterLow);
            g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, info.level)));
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown())
            return;
        for (int i = 0; i < (int) infos.size(); ++i)
            if (tile (i).contains (e.getPosition()))
                owner.selectSlot (i);
    }

private:
    FxRackView& owner;
};

FxRackView::FxRackView (UiContext& c, const Theme& t)
    : device (t), pluginKey ("PLUGIN", t.accent, t), panelKey ("FRONT PANEL", t.accent, t), params (t), ctx (c), theme (t),
      tiles (std::make_unique<Tiles> (*this))
{
    device.caption = "DEVICE";
    device.device = devices::wing; // the WING's FX devices (and EXT)
    device.items = [this]
    {
        juce::StringArray names;
        for (const auto& m : models::table (models::Fx))
            names.add (m.name + (FxSlots::isAllowed (m.index, selected) ? juce::String() : juce::String (" (FX 1-8)")));
        return names;
    };
    device.current = [this] { return ctx.engine.getFx().getModel (selected); };
    device.choose = [this] (int model)
    {
        // EXT is a plugin's slot: the plugin menu loads one.
        if (model == ld::kFxExt && ! ctx.engine.getFx().hasPlugin (selected))
            showFxSlotMenu (ctx, selected, &device);
        else if (FxSlots::isAllowed (model, selected))
            ctx.engine.getFx().setModel (selected, model);
        refresh();
    };
    // The slot's plugins: load one, open its window, bypass, remove.
    pluginKey.onClick = [this] { showFxSlotMenu (ctx, selected, &pluginKey); };
    panelKey.isOn = [this] { return isShowingPanel(); };
    panelKey.onClick = [this]
    {
        panelWanted = ! panelWanted;
        resized();
        panelKey.refresh();
    };

    tilesView.setViewedComponent (tiles.get(), false);
    tilesView.setScrollBarsShown (true, false);
    tilesView.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    listView.setViewedComponent (&params, false);
    listView.setScrollBarsShown (true, false);
    listView.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    for (auto* v : { &tilesView, &listView })
        v->getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme.outline);
    for (auto* child : std::initializer_list<juce::Component*> { &tilesView, &device, &pluginKey, &panelKey })
        addAndMakeVisible (child);
    addChildComponent (listView);
    addChildComponent (faceplate);
    refresh();
    startTimerHz (15);
}

FxRackView::~FxRackView()
{
    stopTimer();
    faceplate.unbind();
}

bool FxRackView::isShowingPanel() const noexcept
{
    return panelWanted && faceplate.hasDesign();
}

void FxRackView::selectSlot (int slot)
{
    const int s = juce::jlimit (0, juce::jmax (0, numSlots - 1), slot);
    if (s == selected && boundModel != -2)
        return;
    selected = s;
    bindSlot();
    tiles->repaint();
}

void FxRackView::bindSlot()
{
    auto& fx = ctx.engine.getFx();
    auto& p = fx.params (selected);
    boundModel = p.model.load();
    params.bind (models::Fx, &p.model, p.values, ld::kMaxFxParams);
    const int slot = selected;
    faceplate.bind (models::Fx, boundModel, p.values, [&fx, slot] { return fx.getMeter (slot); },
                    [&fx, slot] (int band) { return fx.getBandGain (slot, band); },
                    [&fx, slot] (float* out, int n) { return fx.getReadouts (slot, out, n); });
    resized();
    device.refresh();
    panelKey.setEnabled (faceplate.hasDesign());
    panelKey.refresh();
}

void FxRackView::refresh()
{
    auto& fx = ctx.engine.getFx();
    const int n = fx.getNumSlots();
    if (n != numSlots)
    {
        numSlots = n;
        tiles->setSize (tilesView.getWidth() - tilesView.getScrollBarThickness(), 0);
        tiles->setSize (tiles->getWidth(), tiles->heightFor (numSlots));
    }
    if (selected >= numSlots)
        selectSlot (0);
    // Another device in the selected slot (here, a scene, a remote): its controls.
    if (fx.params (selected).model.load() != boundModel)
        bindSlot();

    std::vector<Tiles::Info> infos ((size_t) numSlots);
    float levels[ld::kFxLevels] {};
    for (int i = 0; i < numSlots; ++i)
    {
        auto& info = infos[(size_t) i];
        info.name = deviceName (fx, i);
        info.device = fx.getModel (i) > ld::kFxExt ? devices::wing : juce::String();
        info.users = fx.getNumUsers (i);
        const int got = fx.getReadouts (i, levels, ld::kFxLevels);
        info.level = got >= 4 ? juce::jmax (levels[2], levels[3]) : 0.0f;
    }
    bool changed = infos.size() != tiles->infos.size();
    for (size_t i = 0; ! changed && i < infos.size(); ++i)
        changed = infos[i] != tiles->infos[i];
    if (changed)
    {
        tiles->infos = std::move (infos);
        tiles->repaint();
    }
    device.refresh();
    if (listView.isVisible())
        params.refresh();
}

void FxRackView::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    if (! isShowingPanel() && params.getNumControls() == 0)
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel));
        g.drawText (boundModel == ld::kFxExt ? "A plugin slot: PLUGIN opens its window, bypasses or removes it"
                                             : boundModel == ld::kFxNone ? "An empty slot: choose a device, or a plugin with PLUGIN" : "",
                    listView.getBounds(), juce::Justification::centredTop);
    }
}

void FxRackView::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap, sizes::kGapSmall);
    const int tilesHeight = juce::jmin (tiles->heightFor (juce::jmax (1, numSlots)), 2 * (kTileH + sizes::kGap) + kTileH / 2);
    tilesView.setBounds (area.removeFromTop (tilesHeight));
    tiles->setSize (tilesView.getWidth() - tilesView.getScrollBarThickness(), 0);
    tiles->setSize (tiles->getWidth(), tiles->heightFor (numSlots));
    area.removeFromTop (sizes::kGap);

    auto bar = area.removeFromTop (sizes::kTouch);
    device.setBounds (bar.removeFromLeft (juce::jmin (420, bar.getWidth() / 2)));
    bar.removeFromLeft (sizes::kGap);
    pluginKey.setBounds (bar.removeFromLeft (170));
    bar.removeFromLeft (sizes::kGap);
    panelKey.setBounds (bar.removeFromLeft (150));
    area.removeFromTop (sizes::kGap);

    const bool panel = isShowingPanel();
    faceplate.setVisible (panel);
    listView.setVisible (! panel);
    if (panel)
    {
        const int w = juce::jmin (area.getWidth(), juce::roundToInt ((float) area.getHeight() * kPanelAspect));
        faceplate.setBounds (area.withSizeKeepingCentre (w, juce::roundToInt ((float) w / kPanelAspect)).withY (area.getY()));
    }
    listView.setBounds (area.withWidth (juce::jmin (area.getWidth(), 900))); // bars too long to read across a wide window
    const int width = juce::jmax (0, listView.getWidth() - listView.getScrollBarThickness() - sizes::kGapSmall);
    params.setSize (width, params.getHeightFor (width));
}

} // namespace livedaw::ui::universal
