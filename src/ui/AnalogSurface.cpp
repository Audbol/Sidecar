#include "ui/AnalogSurface.h"

#include "ui/RoutingGroups.h"
#include "ui/universal/RoutingView.h"

namespace livedaw::ui
{

namespace analog
{
    const hw::Look& defaultLook()
    {
        static const hw::Look l;
        return l;
    }

    // ---- Knob -----------------------------------------------------------------------------

    Knob::Knob (juce::Colour c, KnobBinding b) : cap (c), binding (std::move (b)) {}
    void Knob::setBinding (KnobBinding b) { binding = std::move (b); shown = -1.0f; repaint(); }

    void Knob::refresh()
    {
        const float now = binding.isValid() ? binding.get() : 0.0f;
        if (now != shown)
        {
            shown = now;
            repaint();
        }
    }

    void Knob::paint (juce::Graphics& g)
    {
        hw::knob (g, getLocalBounds().toFloat().reduced (1.0f), binding.isValid() ? binding.get() : 0.0f, getLook(), hw::Look::Knob::Plain, cap);
    }

    void Knob::mouseDown (const juce::MouseEvent&) { dragStart = binding.isValid() ? binding.get() : 0.0f; }
    void Knob::mouseDrag (const juce::MouseEvent& e)
    {
        if (binding.isValid())
            binding.set (juce::jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() / 160.0f));
    }
    void Knob::mouseDoubleClick (const juce::MouseEvent&) { if (binding.reset) binding.reset(); }
    void Knob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (binding.isValid())
            binding.set (juce::jlimit (0.0f, 1.0f, binding.get() + w.deltaY * 0.15f));
    }

    // ---- Key ------------------------------------------------------------------------------

    Key::Key (juce::Colour f, juce::Colour l) : face (f), led (l) {}

    void Key::refresh()
    {
        const bool now = isOn && isOn();
        if (now != shownOn)
        {
            shownOn = now;
            repaint();
        }
    }

    void Key::paint (juce::Graphics& g)
    {
        hw::key (g, getLocalBounds().toFloat().reduced (1.0f), {}, shownOn, down, led, getLook(), face, true);
    }

    void Key::mouseDown (const juce::MouseEvent&) { down = true; repaint(); }
    void Key::mouseUp (const juce::MouseEvent& e)
    {
        down = false;
        repaint();
        if (getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }

    // ---- Fader (the MAIN MIX's, horizontal) -----------------------------------------------

    Fader::Fader (juce::Colour c, KnobBinding b) : cap (c), binding (std::move (b)) {}
    void Fader::setBinding (KnobBinding b) { binding = std::move (b); shown = -1.0f; repaint(); }

    float Fader::capX (float position) const noexcept
    {
        constexpr float half = 44.0f;
        return half + juce::jlimit (0.0f, 1.0f, position) * ((float) juce::jmax (getWidth(), getHeight()) - 2.0f * half);
    }

    void Fader::refresh()
    {
        const float now = binding.isValid() ? binding.get() : 0.0f;
        if (now != shown)
        {
            shown = now;
            repaint();
        }
    }

    void Fader::paint (juce::Graphics& g)
    {
        // Drawn lying down in a space L long and T wide; stood up (the panel's fader runs front to back), -oo at the bottom.
        const float L = (float) juce::jmax (getWidth(), getHeight()), T = (float) juce::jmin (getWidth(), getHeight());
        if (getHeight() > getWidth())
            g.addTransform (juce::AffineTransform (0.0f, 1.0f, 0.0f, -1.0f, 0.0f, L));
        const float y = T * 0.5f;
        g.setColour (juce::Colour (0xff0b0b0b));
        g.fillRoundedRectangle (4.0f, y - 5.0f, L - 8.0f, 10.0f, 5.0f);
        g.setColour (juce::Colour (0xff3a3a3a));
        g.drawRoundedRectangle (4.0f, y - 5.0f, L - 8.0f, 10.0f, 5.0f, 1.0f);
        const float x = capX (binding.isValid() ? binding.get() : 0.0f);
        const juce::Rectangle<float> r (x - 44.0f, y - 25.0f, 88.0f, 50.0f);
        g.setGradientFill (juce::ColourGradient (cap.brighter (0.25f), r.getX(), r.getY(), cap.darker (0.3f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (int i = 1; i < 10; ++i)
            g.drawVerticalLine ((int) (r.getX() + 8.0f * (float) i + 4.0f), r.getY() + 4.0f, r.getBottom() - 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawVerticalLine ((int) x, r.getY() + 2.0f, r.getBottom() - 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    void Fader::mouseDown (const juce::MouseEvent& e)
    {
        const float along = getHeight() > getWidth() ? (float) getHeight() - e.position.y : e.position.x;
        if (binding.isValid())
            binding.set ((along - capX (0.0f)) / (capX (1.0f) - capX (0.0f)));
    }
    void Fader::mouseDrag (const juce::MouseEvent& e) { mouseDown (e); }
    void Fader::mouseDoubleClick (const juce::MouseEvent&) { if (binding.reset) binding.reset(); }
    void Fader::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (binding.isValid())
            binding.set (juce::jlimit (0.0f, 1.0f, binding.get() + w.deltaY * 0.1f));
    }
} // namespace analog

class AnalogSurface::Panel final : public juce::Component
{
public:
    explicit Panel (AnalogSurface& s) : owner (s) {}
    void paint (juce::Graphics& g) override { owner.paintPanel (g); }

private:
    AnalogSurface& owner;
};

AnalogSurface::AnalogSurface (UiContext& c, int w, int h)
    : ctx (c), theme (universal::Theme::dark()), panelWidth (w), panelHeight (h), universalKey ("UNIVERSAL MIXER", theme.accent, theme),
      routingKey ("ROUTING", theme.accent, theme)
{
    setOpaque (true);
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
    routingKey.isOn = [this] { return isRoutingShown(); };
    routingKey.onClick = [this] { showRouting (! isRoutingShown()); };
    routingKey.setTooltip ("The routing: the interface's inputs to the channels, the outputs to the interface");
    addAndMakeVisible (routingKey);
    panel = std::make_unique<Panel> (*this);
    addAndMakeVisible (*panel);
    for (auto* b : { &prevKey, &nextKey, &storeKey, &newKey })
        addAndMakeVisible (*b);
    addAndMakeVisible (sceneBox);
    addAndMakeVisible (sceneTitle);
    sceneTitle.setColour (juce::Label::textColourId, juce::Colour (0xffb8bcc2));
    sceneTitle.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    sceneBox.setTextWhenNothingSelected ("no scene");
    sceneBox.setTooltip ("A scene of the show: choosing one recalls it");
    prevKey.setTooltip ("Recall the previous scene");
    nextKey.setTooltip ("Recall the next scene");
    storeKey.setTooltip ("Store the console in the current scene (a new one if there is none)");
    newKey.setTooltip ("Add the console to the show as a new scene");
    prevKey.onClick = [this] { ctx.show.recallPrevious(); fillScenes(); };
    nextKey.onClick = [this] { ctx.show.recallNext(); fillScenes(); };
    storeKey.onClick = [this] { storeScene(); };
    newKey.onClick = [this] { newScene(); };
    sceneBox.onChange = [this] { recallSelected(); };
}

AnalogSurface::~AnalogSurface() { stopTimer(); }

void AnalogSurface::start()
{
    refreshAll();
    startTimerHz (30);
}


/** The routing page: two patch grids in the panel's colours. INPUTS: the interface's inputs (across) to the
    surface's rows (down); OUTPUTS: the interface's outputs (down) fed by the mix's signals (across). The rows
    and signals are the surface's (`routingSpec`). */
class AnalogSurface::RoutingOverlay final : public juce::Component
{
public:
    RoutingOverlay (AnalogSurface& o, const universal::Theme& t)
        : owner (o), theme (t), inputsKey ("INPUTS", t.accent, t), outputsKey ("OUTPUTS", t.accent, t), inGrid (t), outGrid (t)
    {
        inputsKey.isOn = [this] { return ! outputs; };
        outputsKey.isOn = [this] { return outputs; };
        inputsKey.onClick = [this] { showOutputs (false); };
        outputsKey.onClick = [this] { showOutputs (true); };
        for (auto* k : { &inputsKey, &outputsKey })
            addAndMakeVisible (*k);
        addAndMakeVisible (inGrid);
        addChildComponent (outGrid);
        build();
    }

    void showOutputs (bool shown)
    {
        outputs = shown;
        inGrid.setVisible (! shown);
        outGrid.setVisible (shown);
        inputsKey.refresh();
        outputsKey.refresh();
        repaint();
    }

    bool showsOutputs() const noexcept { return outputs; }
    universal::PatchGrid& inputsGrid() noexcept { return inGrid; }
    universal::PatchGrid& outputsGrid() noexcept { return outGrid; }

    void build()
    {
        auto& e = owner.ctx.engine;
        spec = owner.routingSpec();
        sources.clear();
        for (int i = 0; i < routing::tileCount (routing::Group::LocalIn, e.getHardwareInputNames().size()); ++i)
            if (const int src = routing::sourceAt (routing::Group::LocalIn, i); src >= 0)
                sources.push_back (src);
        ports.clear();
        for (int i = 0; i < routing::tileCount (routing::Group::LocalOut, e.getHardwareOutputNames().size()); ++i)
            if (const int port = routing::outputAt (routing::Group::LocalOut, i); port >= 0)
                ports.push_back (port);
        universal::PatchGrid::Model in;
        in.numRows = [this] { return (int) spec.rows.size(); };
        in.numColumns = [this] { return (int) sources.size(); };
        in.rowName = [this] (int r) { const auto& row = spec.rows[(size_t) r]; return row.name.isNotEmpty() ? row.name : row.strip->getLabel(); };
        in.columnName = [this] (int c) { return routing::sourceLabel (sources[(size_t) c]); };
        in.state = [this] (int r, int c)
        {
            const auto& p = spec.rows[(size_t) r].strip->params;
            const int src = sources[(size_t) c], left = p.inputLeft.load(), right = p.inputRight.load();
            if (left == src)
                return universal::PatchGrid::On;
            return p.stereo.load() && left >= 0 && (right == src || (right < 0 && src == left + 1)) ? universal::PatchGrid::Pair
                                                                                                    : universal::PatchGrid::Off;
        };
        in.toggle = [this] (int r, int c)
        {
            auto& p = spec.rows[(size_t) r].strip->params;
            if (p.inputLeft.load() == sources[(size_t) c])
            {
                p.inputLeft = -1;
                p.inputRight = -1;
                return;
            }
            p.inputLeft = sources[(size_t) c];
            p.inputRight = spec.rows[(size_t) r].stereoReturn ? sources[(size_t) c] + 1 : -1; // a stereo return takes a pair
            if (spec.rows[(size_t) r].stereoReturn)
                p.stereo = true;
        };
        inGrid.setModel (std::move (in));

        universal::PatchGrid::Model out;
        out.numRows = [this] { return (int) ports.size(); };
        out.numColumns = [this] { return (int) spec.signals.size(); };
        out.rowName = [this] (int r) { return universal::outputPortName (ports[(size_t) r]); };
        out.columnName = [this] (int c) { return spec.signals[(size_t) c].name; };
        out.state = [this] (int r, int c)
        {
            return spec.signals[(size_t) c].out->load() == ports[(size_t) r] ? universal::PatchGrid::On : universal::PatchGrid::Off;
        };
        out.toggle = [this] (int r, int c)
        {
            auto* o = spec.signals[(size_t) c].out;
            if (o->load() == ports[(size_t) r])
            {
                o->store (-1);
                return;
            }
            owner.ctx.engine.clearOutput (ports[(size_t) r]); // one signal per port
            o->store (ports[(size_t) r]);
        };
        outGrid.setModel (std::move (out));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff151618));
        g.setColour (juce::Colour (0xff3b3e42));
        g.drawRect (getLocalBounds());
        const auto head = getLocalBounds().removeFromTop (46).withTrimmedLeft (16);
        g.setColour (theme.text);
        g.setFont (universal::type::font (universal::type::kTitle, true));
        g.drawText ("ROUTING", head, juce::Justification::centredLeft);
        g.setColour (theme.textDim);
        g.setFont (universal::type::font (universal::type::kCaption));
        g.drawText (outputs ? spec.outputsHint : spec.inputsHint,
                    head.withTrimmedLeft (120).withTrimmedRight (270), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto head = r.removeFromTop (34);
        outputsKey.setBounds (head.removeFromRight (120).reduced (2));
        inputsKey.setBounds (head.removeFromRight (120).reduced (2));
        r.removeFromTop (8);
        inGrid.setBounds (r);
        outGrid.setBounds (r);
    }

private:
    AnalogSurface& owner;
    const universal::Theme& theme;
    universal::ToggleKey inputsKey, outputsKey;
    universal::PatchGrid inGrid, outGrid;
    AnalogRoutingSpec spec;
    std::vector<int> sources, ports;
    bool outputs = false;
};

void AnalogSurface::showRouting (bool shown)
{
    if (shown && routingOverlay == nullptr)
    {
        routingOverlay = std::make_unique<RoutingOverlay> (*this, theme);
        addChildComponent (*routingOverlay);
    }
    if (routingOverlay != nullptr)
    {
        if (shown)
            routingOverlay->build();
        routingOverlay->setVisible (shown);
        if (shown)
            routingOverlay->toFront (false);
    }
    routingKey.refresh();
    resized();
}

bool AnalogSurface::isRoutingShown() const noexcept { return routingOverlay != nullptr && routingOverlay->isVisible(); }
juce::Component* AnalogSurface::getRoutingPage() noexcept { return routingOverlay.get(); }

universal::PatchGrid& AnalogSurface::getRoutingGrid (bool outputs)
{
    if (routingOverlay == nullptr)
        showRouting (true);
    return outputs ? routingOverlay->outputsGrid() : routingOverlay->inputsGrid();
}

Strip* AnalogSurface::channel (int i) const
{
    const auto& l = ctx.engine.getChannels();
    return i >= 0 && i < (int) l.size() ? l[(size_t) i].get() : nullptr;
}
Strip* AnalogSurface::aux (int i) const
{
    const auto& l = ctx.engine.getAux();
    return i >= 0 && i < (int) l.size() ? l[(size_t) i].get() : nullptr;
}
Strip* AnalogSurface::bus (int i) const
{
    const auto& l = ctx.engine.getBuses();
    return i >= 0 && i < (int) l.size() ? l[(size_t) i].get() : nullptr;
}
Strip* AnalogSurface::main() const
{
    const auto& l = ctx.engine.getMains();
    return l.empty() ? nullptr : l[0].get();
}

analog::Knob& AnalogSurface::addKnob (const juce::String& id, juce::Colour cap, juce::Rectangle<int> r)
{
    auto k = std::make_unique<analog::Knob> (cap, KnobBinding {});
    auto& ref = *k;
    ref.setComponentID (id);
    ref.setLook (panelLook());
    ref.setBounds (r);
    panel->addAndMakeVisible (ref);
    controls.push_back (std::move (k));
    return ref;
}
analog::Key& AnalogSurface::addKey (const juce::String& id, juce::Colour face, juce::Colour led, juce::Rectangle<int> r)
{
    auto k = std::make_unique<analog::Key> (face, led);
    auto& ref = *k;
    ref.setComponentID (id);
    ref.setLook (panelLook());
    ref.setBounds (r);
    panel->addAndMakeVisible (ref);
    controls.push_back (std::move (k));
    return ref;
}
analog::Fader& AnalogSurface::addFader (const juce::String& id, juce::Colour cap, juce::Rectangle<int> r)
{
    auto f = std::make_unique<analog::Fader> (cap, KnobBinding {});
    auto& ref = *f;
    ref.setComponentID (id);
    ref.setLook (panelLook());
    ref.setBounds (r);
    panel->addAndMakeVisible (ref);
    controls.push_back (std::move (f));
    return ref;
}

ConsoleTape& AnalogSurface::addTape (int ch, int seed, const juce::String& id, juce::Rectangle<int> r)
{
    if ((int) tapes.size() <= ch)
        tapes.resize ((size_t) ch + 1);
    auto t = std::make_unique<ConsoleTape> (seed);
    t->setComponentID (id);
    t->setBounds (r);
    t->getText = [this, ch] { auto* s = channel (ch); return s != nullptr ? s->getOwnName() : juce::String(); };
    t->setText = [this, ch] (const juce::String& text) { if (auto* s = channel (ch)) s->setName (text); };
    panel->addAndMakeVisible (*t);
    tapes[(size_t) ch] = std::move (t);
    return *tapes[(size_t) ch];
}

juce::Component& AnalogSurface::getPanel() noexcept { return *panel; }

analog::Control* AnalogSurface::findControl (const juce::String& id)
{
    for (auto& c : controls)
        if (c->getComponentID() == id)
            return c.get();
    return nullptr;
}

void AnalogSurface::refreshAll()
{
    bindControls();
    fillScenes();
    for (auto& c : controls)
        c->refresh();
    for (auto& t : tapes)
        if (t != nullptr)
            t->refresh();
    repaint();
}

void AnalogSurface::showChanged() { fillScenes(); }

void AnalogSurface::fillScenes()
{
    const int n = ctx.show.getNumScenes();
    shownScenes = n;
    sceneBox.clear (juce::dontSendNotification);
    for (int i = 0; i < n; ++i)
        if (const auto s = ctx.show.getScene (i))
            sceneBox.addItem (juce::String (i + 1) + "  " + s->name, i + 1);
    const int current = ctx.show.getCurrentSceneIndex();
    sceneBox.setSelectedId (current >= 0 && current < n ? current + 1 : 0, juce::dontSendNotification);
}

void AnalogSurface::recallSelected()
{
    const int index = sceneBox.getSelectedId() - 1;
    if (index < 0)
        return;
    ctx.show.recallScene (index);
    ctx.log.info ("Recalled scene " + juce::String (index + 1));
    refreshAll();
}

void AnalogSurface::newScene()
{
    const int index = ctx.show.addScene ("Scene " + juce::String (ctx.show.getNumScenes() + 1));
    ctx.log.info ("Stored the console as scene " + juce::String (index + 1));
    fillScenes();
}

void AnalogSurface::storeScene()
{
    const int current = ctx.show.getCurrentSceneIndex();
    if (current < 0 || current >= ctx.show.getNumScenes())
        return newScene();
    ctx.show.storeScene (current);
    ctx.log.info ("Stored the console in scene " + juce::String (current + 1));
    fillScenes();
}

void AnalogSurface::timerCallback()
{
    if (ctx.show.getNumScenes() != shownScenes)
        fillScenes();
    for (auto& c : controls)
        c->refresh();
    for (auto& t : tapes)
        if (t != nullptr)
            t->refresh();
    repaintLive();
}

void AnalogSurface::resized()
{
    auto bar = getLocalBounds().removeFromTop (kBarHeight).reduced (10, 9);
    sceneTitle.setBounds (bar.removeFromLeft (80));
    prevKey.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (6);
    sceneBox.setBounds (bar.removeFromLeft (320));
    bar.removeFromLeft (6);
    nextKey.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (18);
    storeKey.setBounds (bar.removeFromLeft (80));
    bar.removeFromLeft (6);
    newKey.setBounds (bar.removeFromLeft (70));
    universalKey.setBounds (getLocalBounds().removeFromTop (kBarHeight).removeFromRight (190).reduced (universal::sizes::kGap, 7));
    routingKey.setBounds (getLocalBounds().removeFromTop (kBarHeight).withTrimmedRight (190).removeFromRight (110).reduced (universal::sizes::kGap, 7));
    if (routingOverlay != nullptr)
        routingOverlay->setBounds (getLocalBounds().withTrimmedTop (kBarHeight).reduced (8));

    const auto area = getLocalBounds().withTrimmedTop (kBarHeight).reduced (8).toFloat();
    const float s = juce::jmin (area.getWidth() / (float) panelWidth, area.getHeight() / (float) panelHeight);
    panel->setBounds (0, 0, panelWidth, panelHeight);
    panel->setTransform (juce::AffineTransform::scale (s).translated (area.getCentreX() - (float) panelWidth * s * 0.5f,
                                                                      area.getCentreY() - (float) panelHeight * s * 0.5f));
}

void AnalogSurface::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1c1e), 0.0f, 0.0f, juce::Colour (0xff101112), 0.0f, (float) getHeight(), false));
    g.fillAll();
    g.setColour (juce::Colour (0xff2a2c2f));
    g.fillRect (0, 0, getWidth(), kBarHeight);
    g.setColour (juce::Colour (0xff3b3e42));
    g.drawHorizontalLine (kBarHeight - 1, 0.0f, (float) getWidth());
}

} // namespace livedaw::ui
