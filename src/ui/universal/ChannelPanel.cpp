#include "ui/universal/ChannelPanel.h"

#include "ui/ChainSummary.h"
#include "ui/DelayUnits.h"
#include "ui/HomeScreens.h"

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kTitle = 28, kKeys = 44;

    /** The sections' widths at 1920 px (SENDS takes the rest); narrower windows scale them. */
    constexpr int kWidths[] = { 210, 200, 210, 380, 220, 180 };
    /** Under this width (the inspector open, a small window) CHAIN and SENDS give their room to the
        curves: the inspector has both, each in full. */
    constexpr int kSlimBelow = 1560;
} // namespace

ChannelPanel::ChannelPanel (UiContext& c, const Theme& t)
    : ctx (c), theme (t), trim (t), balance (t), delay (t), invert ("INVERT", t.accent, t), delayOn ("DELAY", t.accent, t),
      lowCut ("LOW CUT", t.accent, t), highCut ("HIGH CUT", t.accent, t), muteKey ("MUTE", t.mute, t), soloKey ("SOLO", t.solo, t), sofKey ("SOF", t.accent, t), copyKey ("COPY", t.accent, t),
      pasteKey ("PASTE", t.accent, t), maxKey ("MAXIMISE", t.accent, t), editKey ("EDIT", t.accent, t), rtaKey ("RTA", t.accent, t), routeKey ("SOURCE / ROUTING", t.accent, t),
      gateOn ("ON", t.accent, t), eqOn ("ON", t.accent, t),
      dynOn ("ON", t.accent, t), filterView (t), eqView (t), gateView (t), dynView (t), sends (c, t)
{
    for (juce::Component* comp : { (juce::Component*) &trim, (juce::Component*) &balance, (juce::Component*) &delay, (juce::Component*) &invert,
                                   (juce::Component*) &delayOn, (juce::Component*) &lowCut, (juce::Component*) &highCut,
                                   (juce::Component*) &gateOn, (juce::Component*) &eqOn, (juce::Component*) &dynOn,
                                   (juce::Component*) &filterView, (juce::Component*) &eqView, (juce::Component*) &gateView,
                                   (juce::Component*) &dynView, (juce::Component*) &sends, (juce::Component*) &muteKey,
                                   (juce::Component*) &soloKey, (juce::Component*) &sofKey, (juce::Component*) &copyKey,
                                   (juce::Component*) &pasteKey, (juce::Component*) &maxKey, (juce::Component*) &editKey, (juce::Component*) &routeKey, (juce::Component*) &rtaKey })
        addChildComponent (comp);
    eqView.rangeDb = 15.0f;
    height = juce::jlimit (kMinHeight, 4000, ctx.settings.getIntValue (kHeightSetting, kDefaultHeight));
    maxKey.isOn = [this] { return maximised; };
    maxKey.onClick = [this] { setMaximised (! maximised); };
    rtaKey.isOn = [this] { return ctx.settings.getBoolValue (kRtaSetting, true); };
    rtaKey.onClick = [this]
    {
        ctx.settings.setValue (kRtaSetting, ! ctx.settings.getBoolValue (kRtaSetting, true));
        updateRta();
        rtaKey.repaint();
    };

    // The keys switch what the strip holds; they read it back as they paint.
    const auto bindKey = [this] (ToggleKey& key, auto member)
    {
        key.isOn = [this, member] { return strip != nullptr && member (strip->params).load(); };
        key.onClick = [this, member]
        {
            if (strip != nullptr)
            {
                auto& value = member (strip->params);
                value = ! value.load();
            }
        };
    };
    bindKey (invert, [] (StripParams& p) -> std::atomic<bool>& { return p.input.invert; });
    bindKey (delayOn, [] (StripParams& p) -> std::atomic<bool>& { return p.input.delayOn; });
    bindKey (lowCut, [] (StripParams& p) -> std::atomic<bool>& { return p.filter().lowCut; });
    bindKey (highCut, [] (StripParams& p) -> std::atomic<bool>& { return p.filter().highCut; });
    bindKey (gateOn, [] (StripParams& p) -> std::atomic<bool>& { return p.gate().on; });
    bindKey (eqOn, [] (StripParams& p) -> std::atomic<bool>& { return p.eq().on; });
    bindKey (dynOn, [] (StripParams& p) -> std::atomic<bool>& { return p.dynamics().on; });

    // The curves, as the engine runs them (1 EQ, 3 FILTER).
    const auto curve = [this] (int block)
    {
        return [this, block] (const double* f, double* db, int n)
        {
            if (strip == nullptr)
                return false;
            ld::ld_strip_response (strip->getHandle(), block, f, db, n, sampleRate());
            return true;
        };
    };
    filterView.response = curve (3);
    eqView.response = curve (1);
    filterView.handleSource = [this]
    {
        std::vector<ResponseView::Handle> handles;
        if (strip == nullptr)
            return handles;
        auto& f = strip->params.filter();
        ResponseView::Handle low, high;
        low.label = "L";
        low.freq = &f.lowCutFreq;
        low.minFreq = 20.0f;
        low.maxFreq = 2000.0f;
        low.defaultFreq = kLowCutDefaultHz;
        low.active = [&f] { return f.lowCut.load(); };
        high.label = "H";
        high.freq = &f.highCutFreq;
        high.minFreq = 50.0f;
        high.maxFreq = 20000.0f;
        high.defaultFreq = kHighCutDefaultHz;
        high.active = [&f] { return f.highCut.load(); };
        handles = { low, high };
        return handles;
    };
    eqView.handleSource = [this]
    {
        if (strip == nullptr)
            return std::vector<ResponseView::Handle>();
        auto& eq = strip->params.eq();
        return ResponseView::eqHandles (models::model (static_cast<models::Table> (strip->getEqTable()), eq.model.load()), eq.values);
    };
    muteKey.onClick = [this]
    {
        if (strip != nullptr)
            ctx.engine.pressMute (*strip);
    };
    muteKey.isOn = [this] { return strip != nullptr && strip->params.mute.load(); };
    muteKey.isHalfOn = [this] { return strip != nullptr && strip->getMuteState() == ld::kMuteGroup; };
    soloKey.onClick = [this]
    {
        if (strip != nullptr)
            strip->params.solo = ! strip->params.solo.load();
    };
    soloKey.isOn = [this] { return strip != nullptr && strip->params.solo.load(); };
    sofKey.isOn = [this] { return ctx.sendsOnFader; };
    sofKey.onClick = [this]
    {
        if (onSendsOnFader)
            onSendsOnFader();
    };
    copyKey.onClick = [this]
    {
        if (strip != nullptr)
            copied = state::StripRef::of (*strip);
        refresh();
    };
    pasteKey.onClick = [this]
    {
        auto* from = copied.has_value() ? state::findStrip (ctx.engine, *copied) : nullptr;
        if (from != nullptr && strip != nullptr && from != strip)
            copyStripSections (ctx.engine, *from, *strip, kCopyAll);
    };
    editKey.onClick = [this]
    {
        const int kinds[] = { ld::kStepInput, ld::kStepFilter, ld::kStepGate, ld::kStepEq, ld::kStepDynamics };
        if (onOpenStep && juce::isPositiveAndBelow (expanded, 5))
            onOpenStep (kinds[expanded], -1);
    };
    routeKey.onClick = [this]
    {
        if (onOpenStep)
            onOpenStep (ld::kStepInput, -1);
    };
    sends.setCompact (true);
    gateView.reductionDb = [this] { return strip != nullptr ? strip->meters.gateReductionDb.load() : 0.0f; };
    dynView.reductionDb = [this] { return strip != nullptr ? strip->meters.dynReductionDb.load() : 0.0f; };
}

ChannelPanel::~ChannelPanel()
{
    stopTimer();
    if (rtaRunning && strip != nullptr)
        Analysis (*strip).hideRta();
}

void ChannelPanel::visibilityChanged()
{
    updateRta();
}

void ChannelPanel::updateRta()
{
    // The analyser runs only while this panel can be seen, a strip is picked and the RTA key is on.
    bool seen = strip != nullptr && eqView.isVisible();
    for (auto* c = (juce::Component*) this; seen && c != nullptr; c = c->getParentComponent())
        seen = c->isVisible();
    const bool want = seen && ctx.settings.getBoolValue (kRtaSetting, true);
    if (want)
    {
        Analysis analysis (*strip);
        if (! rtaRunning || ! analysis.isAnalysed (*strip) || ! analysis.controls.rtaOn.load())
        {
            analysis.showRta (strip, ld::kRtaViewEq);
            rtaRunning = true;
        }
        analysis.read (rtaBands);
        eqView.setRta (&rtaBands);
        return;
    }
    if (rtaRunning && strip != nullptr)
        Analysis (*strip).hideRta();
    rtaRunning = false;
    eqView.setRta (nullptr);
}

void ChannelPanel::setExpanded (int section, bool animate)
{
    expanded = juce::isPositiveAndBelow (section, (int) ChainSection) ? section : -1;
    if (expanded >= 0)
        shownExpanded = expanded;
    if (! animate)
    {
        expandAmount = expanded >= 0 ? 1.0f : 0.0f;
        if (expanded < 0)
            shownExpanded = -1;
        stopTimer();
        resized();
        repaint();
        return;
    }
    startTimerHz (60);
}

void ChannelPanel::timerCallback()
{
    // A smooth open and close: eased towards where it is going.
    const float target = expanded >= 0 ? 1.0f : 0.0f;
    expandAmount += (target - expandAmount) * 0.3f;
    if (std::abs (target - expandAmount) < 0.01f)
    {
        expandAmount = target;
        if (expanded < 0)
            shownExpanded = -1;
        stopTimer();
    }
    resized();
    repaint();
}

juce::Colour ChannelPanel::cardColour (int s) const
{
    using H = Theme::BlockHue;
    static const H hues[] = { H::Input, H::Filter, H::Gate, H::Eq, H::Dynamics, H::Insert, H::Fx };
    return theme.blockColour (hues[juce::jlimit (0, (int) NumSections - 1, s)]);
}

const char* ChannelPanel::sectionName (int s)
{
    static const char* names[] = { "INPUT / ROUTING", "FILTER", "GATE", "EQ", "DYNAMICS", "CHAIN", "SENDS" };
    return names[juce::jlimit (0, (int) NumSections - 1, s)];
}

double ChannelPanel::sampleRate() const
{
    const double sr = ctx.engine.getSampleRate();
    return sr > 0.0 ? sr : 48000.0;
}

int ChannelPanel::getPanelHeight() const
{
    const int most = juce::jmax (kMinHeight, viewHeight - kCompactFaders);
    return maximised ? most : juce::jlimit (kMinHeight, most, height);
}

void ChannelPanel::setMaximised (bool on)
{
    maximised = on;
    maxKey.setButtonText (on ? "RESTORE" : "MAXIMISE");
    maxKey.repaint();
    if (onSizeChanged)
        onSizeChanged();
}

void ChannelPanel::setPanelHeight (int h)
{
    maximised = false;
    maxKey.setButtonText ("MAXIMISE");
    height = juce::jlimit (kMinHeight, 4000, h);
    ctx.settings.setValue (kHeightSetting, height);
    if (onSizeChanged)
        onSizeChanged();
}

void ChannelPanel::mouseDown (const juce::MouseEvent& e)
{
    draggingBar = getDragBar().contains (e.getPosition());
    dragStartY = e.getScreenY();
    dragStartHeight = getHeight();
}

void ChannelPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingBar)
        setPanelHeight (dragStartHeight + e.getScreenY() - dragStartY);
}

void ChannelPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (getDragBar().contains (e.getPosition()))
        setMaximised (! maximised);
}

void ChannelPanel::setStrip (Strip* s)
{
    if (s == strip)
        return;
    strip = s;
    bindStrip();
    resized();
    refresh();
    repaint();
}

void ChannelPanel::bindStrip()
{
    thumbs.clear();
    extras.clear();
    const bool shown = strip != nullptr;
    for (auto* c : getChildren())
        c->setVisible (shown && c != &editKey);
    if (shown)
        rebuildExtras();
    if (strip == nullptr)
    {
        trim.unbind();
        balance.unbind();
        delay.unbind();
        gateView.bind (nullptr);
        dynView.bind (nullptr);
        sends.setStrip (nullptr);
        return;
    }
    auto& p = strip->params;
    trim.bind (ValueBar::linear ("TRIM", p.input.trimDb, -18.0f, 18.0f, 0.0f, " dB", 1));
    balance.bind (ValueBar::linear ("BALANCE", p.input.balanceDb, -9.0f, 9.0f, 0.0f, " dB", 1));
    const int mode = p.input.delayMode.load();
    const auto range = delayRange (mode, (float) sampleRate());
    auto d = ValueBar::linear ("DELAY", p.input.delay, range.start, range.end, 0.0f, {}, 1);
    d.text = [mode] (float v) { return delayText (v, mode); };
    delay.bind (d);
    gateView.bind (&p.gate());
    dynView.bind (&p.dynamics());
    sends.setStrip (strip);
    // Inputs only: aux have no filter or gate (their sections stay, empty).
    const bool channel = strip->getKind() == StripKind::Channel;
    for (auto* c : { (juce::Component*) &lowCut, (juce::Component*) &highCut, (juce::Component*) &filterView, (juce::Component*) &gateOn,
                     (juce::Component*) &gateView })
        c->setVisible (channel);
}

void ChannelPanel::refresh()
{
    updateRta();
    if (strip == nullptr)
        return;
    for (auto* bar : { &trim, &balance, &delay })
        bar->refresh();
    for (auto* key : { &invert, &delayOn, &lowCut, &highCut, &gateOn, &eqOn, &dynOn, &muteKey, &soloKey, &sofKey })
        key->repaint();
    auto* source = copied.has_value() ? state::findStrip (ctx.engine, *copied) : nullptr;
    pasteKey.setEnabled (source != nullptr && source != strip);
    pasteKey.setButtonText (source != nullptr ? "PASTE " + source->getLabel() : juce::String ("PASTE"));
    for (auto* view : { &filterView, &eqView })
        view->refresh();
    gateView.refresh();
    dynView.refresh();
    sends.refresh();
    rebuildExtras();
    for (auto& x : extras)
    {
        if (x->eqView != nullptr)
            x->eqView->refresh();
        if (x->dynView != nullptr)
            x->dynView->refresh();
        if (x->onKey != nullptr)
            x->onKey->repaint();
    }
    updateThumbs();
    repaint (areas[ChainSection]);
}

void ChannelPanel::resized()
{
    auto all = getLocalBounds().reduced (sizes::kGap, sizes::kGapSmall);
    all.removeFromBottom (kBarHeight - sizes::kGapSmall); // the drag bar's
    {
        // The header: the strip name is painted at the left, its keys at the right.
        auto head = all.removeFromTop (kHeader);
        all.removeFromTop (sizes::kGapSmall);
        const int w = juce::jmax (60, juce::jmin (96, head.getWidth() / 12));
        for (auto* key : { &maxKey, &pasteKey, &copyKey, &sofKey, &invert, &soloKey, &muteKey })
            key->setBounds (head.removeFromRight (key == &pasteKey ? w + 40 : w).reduced (2, 0));
    }
    auto area = all;
    const bool slim = area.getWidth() < kSlimBelow;
    // Cards share the row by weight; the one opened takes most of it (slim: the five). The strip's
    // extra EQ and dynamics blocks follow its own dynamics card.
    struct Slot
    {
        int section;
        int extra;
        float weight = 0.0f;
    };
    std::vector<Slot> slots;
    for (int s = 0; s < (slim ? (int) ChainSection : (int) NumSections); ++s)
    {
        slots.push_back ({ s, -1 });
        if (s == DynamicsSection)
            for (int i = 0; i < (int) extras.size(); ++i)
                slots.push_back ({ -1, i });
    }
    float sum = 0.0f;
    for (auto& slot : slots)
    {
        if (slot.section < 0)
            slot.weight = 300.0f;
        else
        {
            const int s = slot.section;
            slot.weight = s < (int) std::size (kWidths) ? (float) kWidths[s] : 480.0f;
            if (s == shownExpanded)
                slot.weight *= 1.0f + 3.0f * expandAmount;
            else if (shownExpanded >= 0)
                slot.weight *= 1.0f - 0.45f * expandAmount;
        }
        sum += slot.weight;
    }
    const float usable = (float) (area.getWidth() - ((int) slots.size() - 1) * sizes::kGapSmall);
    for (auto& a : areas)
        a = {};
    for (size_t k = 0; k < slots.size(); ++k)
    {
        const auto r = k + 1 == slots.size() ? area : area.removeFromLeft (juce::roundToInt (usable * slots[k].weight / sum));
        (slots[k].section >= 0 ? areas[(size_t) slots[k].section] : extras[(size_t) slots[k].extra]->area) = r;
        area.removeFromLeft (sizes::kGapSmall);
    }
    sends.setVisible (strip != nullptr && ! slim);
    const auto body = [this] (int s) { return areas[(size_t) s].withTrimmedTop (kTitle).reduced (sizes::kGapSmall); };

    {
        auto b = body (InputSection);
        const int row = juce::jmin (sizes::kTouch, b.getHeight() / 5);
        trim.setBounds (b.removeFromTop (row).reduced (0, 2));
        balance.setBounds (b.removeFromTop (row).reduced (0, 2));
        delay.setBounds (b.removeFromTop (row).reduced (0, 2));
        delayOn.setBounds (b.removeFromTop (row).reduced (2, 2));
        routeKey.setBounds (b.removeFromTop (row).reduced (2, 2));
    }
    {
        auto b = body (FilterSection);
        auto keys = b.removeFromBottom (kKeys);
        lowCut.setBounds (keys.removeFromLeft (keys.getWidth() / 2).reduced (2, 0));
        highCut.setBounds (keys.reduced (2, 0));
        filterView.setBounds (b.withTrimmedBottom (sizes::kGapSmall));
    }
    for (auto [s, key, view] : { std::tuple<int, ToggleKey*, juce::Component*> { GateSection, &gateOn, &gateView },
                                 { EqSection, &eqOn, &eqView }, { DynamicsSection, &dynOn, &dynView } })
    {
        auto b = body (s);
        auto keyRow = b.removeFromBottom (kKeys);
        key->setBounds (keyRow.withWidth (juce::jmin (120, keyRow.getWidth())));
        if (s == EqSection)
            rtaKey.setBounds (keyRow.withTrimmedLeft (juce::jmin (120, keyRow.getWidth()) + sizes::kGapSmall).withWidth (juce::jmax (0, juce::jmin (72, keyRow.getWidth() - 124))));
        view->setBounds (b.withTrimmedBottom (sizes::kGapSmall));
    }
    for (auto& x : extras)
    {
        auto b = x->area.withTrimmedTop (kTitle).reduced (sizes::kGapSmall);
        if (x->plugin != nullptr)
        {
            x->plugin->setBounds (b);
            continue;
        }
        auto keyRow = b.removeFromBottom (kKeys);
        x->onKey->setBounds (keyRow.withWidth (juce::jmin (120, keyRow.getWidth())));
        auto* view = x->eqView != nullptr ? (juce::Component*) x->eqView.get() : (juce::Component*) x->dynView.get();
        view->setBounds (b.withTrimmedBottom (sizes::kGapSmall));
    }
    sends.setCompact (getHeight() < 420);
    editKey.setVisible (strip != nullptr && shownExpanded >= 0 && expandAmount > 0.9f);
    if (shownExpanded >= 0)
        editKey.setBounds (areas[(size_t) shownExpanded].withHeight (kTitle).removeFromRight (64).reduced (0, 2));
    chainList = body (ChainSection);
    sends.setBounds (body (SendsSection));
}

void ChannelPanel::rebuildExtras()
{
    // The extra EQ and dynamics blocks, in signal order; the chain changing (add, remove, reorder) rebuilds.
    std::vector<ChainStep> want;
    if (strip != nullptr)
        for (const auto& step : strip->getChain())
            if (step.isBlock() && (step.getBlockKind() == BlockKind::Eq || step.getBlockKind() == BlockKind::Dynamics || step.getBlockKind() == BlockKind::Plugin))
                want.push_back (step);
    const bool same = want.size() == extras.size() && std::equal (want.begin(), want.end(), extras.begin(), [] (const ChainStep& a, const std::unique_ptr<ExtraCard>& b)
    {
        return a.uid == b->step.uid && a.arg == b->step.arg;
    });
    if (same)
        return;
    extras.clear();
    int eqCount = 1, dynCount = 1, plugCount = 0; // the strip's own is the first of its kind
    for (const auto& step : want)
    {
        auto x = std::make_unique<ExtraCard>();
        x->step = step;
        x->target = BlockTarget::of (ctx, *strip, step);
        if (x->target.kind == BlockTarget::Kind::Plugin)
        {
            // A hosted plugin: its name, vendor, latency and the keys ON, EDIT and PLUG-IN.
            x->name = "PLUG-IN " + juce::String (++plugCount);
            x->plugin = std::make_unique<PluginBlockCard> (ctx, theme);
            x->plugin->setBlock (strip, step.uid, x->target.pluginOn);
            addAndMakeVisible (*x->plugin);
            extras.push_back (std::move (x));
            continue;
        }
        if (x->target.kind != BlockTarget::Kind::Eq && x->target.kind != BlockTarget::Kind::Dynamics)
            continue;
        const bool isEq = x->target.kind == BlockTarget::Kind::Eq;
        x->name = isEq ? "EQ " + juce::String (++eqCount) : "DYNAMICS " + juce::String (++dynCount);
        x->onKey = std::make_unique<ToggleKey> ("ON", theme.accent, theme);
        auto& target = x->target;
        auto* on = isEq ? &target.eq->on : &target.dynamics->on;
        x->onKey->isOn = [on] { return on->load(); };
        x->onKey->onClick = [on] { *on = ! on->load(); };
        addAndMakeVisible (*x->onKey);
        if (isEq)
        {
            x->eqView = std::make_unique<ResponseView> (theme);
            x->eqView->rangeDb = 15.0f;
            x->eqView->response = target.response;
            x->eqView->handleSource = [t = &target]
            {
                return ResponseView::eqHandles (models::model (t->table, t->eq->model.load()), t->eq->values);
            };
            addAndMakeVisible (*x->eqView);
        }
        else
        {
            x->dynView = std::make_unique<TransferView> (theme);
            x->dynView->reductionDb = target.reductionDb;
            x->dynView->bind (target.dynamics);
            addAndMakeVisible (*x->dynView);
        }
        extras.push_back (std::move (x));
    }
    resized();
    repaint();
}

void ChannelPanel::updateThumbs()
{
    if (thumbFreqs.empty())
        for (int i = 0; i < 32; ++i)
            thumbFreqs.push_back (20.0 * std::pow (1000.0, (double) i / 31.0));
    const auto steps = shownSteps();
    const bool same = steps.size() == thumbs.size() && std::equal (steps.begin(), steps.end(), thumbs.begin(), [] (const ChainStep& a, const Thumb& b)
    {
        return a.kind == b.step.kind && a.arg == b.step.arg && a.uid == b.step.uid && a.slot == b.step.slot;
    });
    if (! same)
    {
        thumbs.clear();
        for (const auto& step : steps)
        {
            Thumb t;
            t.step = step;
            t.target = BlockTarget::of (ctx, *strip, step);
            thumbs.push_back (std::move (t));
        }
    }
    if (! same || (thumbTick++ % 6) == 0)
        for (auto& t : thumbs)
            t.picture.update (ctx, t.target, thumbFreqs);
}

std::vector<ChainStep> ChannelPanel::shownSteps() const
{
    std::vector<ChainStep> steps;
    if (strip != nullptr)
        for (const auto& step : strip->getChain())
            if (isShown (step))
                steps.push_back (step);
    return steps;
}

void ChannelPanel::mouseUp (const juce::MouseEvent& e)
{
    const bool wasBar = draggingBar;
    draggingBar = false;
    if (wasBar || strip == nullptr || e.mouseWasDraggedSinceMouseDown())
        return;
    if (chainList.contains (e.getPosition()) && onOpenStep)
    {
        const auto steps = shownSteps();
        const float row = juce::jmin (36.0f, (float) chainList.getHeight() / juce::jmax (1.0f, (float) steps.size()));
        const int i = (int) ((float) (e.y - chainList.getY()) / row);
        if (juce::isPositiveAndBelow (i, (int) steps.size()) && ! isMarker (steps[(size_t) i]))
            onOpenStep (steps[(size_t) i].kind, steps[(size_t) i].isBlock() ? steps[(size_t) i].uid : -1);
        return;
    }
    for (const auto& x : extras)
        if (x->area.withHeight (kTitle).contains (e.getPosition()) && onOpenStep)
        {
            onOpenStep (x->step.kind, x->step.uid);
            return;
        }
    // A card title: it opens in place (again: closes); EDIT in it opens the block in the inspector.
    for (int s = 0; s < (int) ChainSection; ++s)
        if (areas[(size_t) s].withHeight (kTitle).contains (e.getPosition()))
            setExpanded (expanded == s ? -1 : s);
}

void ChannelPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    if (strip == nullptr)
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kValue));
        g.drawText ("Tap a strip's name to show all of it here.", getLocalBounds(), juce::Justification::centred);
        return;
    }
    const bool channel = strip->getKind() == StripKind::Channel;
    {
        // The header: the strip colour, number and name.
        auto head = getLocalBounds().reduced (sizes::kGap, sizes::kGapSmall).removeFromTop (kHeader).toFloat();
        const auto tint = stripDisplayColour (*strip);
        g.setColour (tint);
        g.fillRoundedRectangle (head.removeFromLeft (10.0f), 3.0f);
        head.removeFromLeft ((float) sizes::kGap);
        g.setColour (theme.text);
        g.setFont (type::font (type::kValue, true));
        const auto number = strip->getLabel().toUpperCase();
        const float nw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), number) + 12.0f;
        g.drawText (number, head.removeFromLeft (nw), juce::Justification::centredLeft, false);
        g.setColour (theme.textDim);
        g.drawFittedText (strip->getName(), head.withRight (invert.getX() - 8.0f - (float) sizes::kGap).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }
    for (int s = 0; s < NumSections; ++s)
    {
        if (areas[(size_t) s].isEmpty())
            continue;
        const auto r = areas[(size_t) s].toFloat();
        const auto hue = cardColour (s);
        g.setColour (theme.panel);
        g.fillRoundedRectangle (r, sizes::kRadius);
        g.setColour (hue.withAlpha (s == shownExpanded ? 0.16f : 0.09f));
        g.fillRoundedRectangle (r, sizes::kRadius);
        g.setColour (hue);
        g.fillRoundedRectangle (r.withHeight (4.0f), 2.0f);
        if (s == shownExpanded)
            g.drawRoundedRectangle (r.reduced (0.5f), sizes::kRadius, 1.5f);
        auto title = r.withHeight ((float) kTitle).reduced ((float) sizes::kGap, 0.0f);
        juce::String text (sectionName (s));
        // The strip's own blocks are its WING template's (the tag, at the right first, so
        // the name keeps clear of it in a slim section); a dynamics block's model after its name.
        const bool tagged = s == FilterSection || s == GateSection || s == EqSection || s == DynamicsSection;
        if (tagged)
        {
            // A gate or dynamics model of another console names that console (as the editor's tag does).
            auto tag = devices::shown();
            if (s == GateSection || s == DynamicsSection)
            {
                const auto& m = models::model (models::Dynamics, (s == GateSection ? strip->params.gate() : strip->params.dynamics()).model.load());
                tag = devices::of (m.token, m.foreign, tag);
            }
            title.removeFromRight (drawDeviceTag (g, title, tag, theme) + (float) sizes::kGapSmall);
        }
        g.setColour (theme.text);
        g.setFont (type::font (type::kCaption, true));
        g.drawFittedText (text, title.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        if (tagged)
        {
            // The name's width in the font it was drawn with, and a gap.
            title.removeFromLeft (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) * 1.04f + (float) sizes::kGapSmall + 2.0f);
            if (s == GateSection || s == DynamicsSection)
            {
                const auto& slot = s == GateSection ? strip->params.gate() : strip->params.dynamics();
                // The model without "WING " (the section's tag says it), at a size that stays
                // readable; left out where it doesn't fit (the editor names it).
                const auto modelName = models::model (models::Dynamics, slot.model.load()).name.toUpperCase().replace ("WING ", "");
                const auto modelFont = type::font (type::kMicro, true);
                if (modelName != text && juce::GlyphArrangement::getStringWidth (modelFont, modelName) <= title.getWidth()) // not "GATE GATE"
                {
                    g.setColour (theme.text);
                    g.setFont (modelFont);
                    g.drawText (modelName, title, juce::Justification::centredLeft, false);
                }
            }
        }
        if ((s == FilterSection || s == GateSection) && ! channel)
        {
            g.setColour (theme.textDim);
            g.drawText ("CHANNELS ONLY", r, juce::Justification::centred, false);
        }
    }

    for (const auto& x : extras)
    {
        const auto r = x->area.toFloat();
        const auto hue = x->plugin != nullptr ? theme.blockColour (Theme::BlockHue::Fx) : cardColour (x->eqView != nullptr ? (int) EqSection : (int) DynamicsSection);
        g.setColour (theme.panel);
        g.fillRoundedRectangle (r, sizes::kRadius);
        g.setColour (hue.withAlpha (0.09f));
        g.fillRoundedRectangle (r, sizes::kRadius);
        g.setColour (hue);
        g.fillRoundedRectangle (r.withHeight (4.0f), 2.0f);
        g.setColour (theme.text);
        g.setFont (type::font (type::kCaption, true));
        g.drawFittedText (x->name, r.withHeight ((float) kTitle).reduced ((float) sizes::kGap, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }
    {
        // The drag bar: a grip along the bottom edge.
        const auto bar = getDragBar().toFloat();
        g.setColour (theme.outline);
        g.fillRoundedRectangle (bar.withSizeKeepingCentre (96.0f, 6.0f), 3.0f);
    }
    if (areas[ChainSection].isEmpty())
        return;
    // CHAIN: the strip's processing in order; the Universal core's own in the accent.
    auto list = chainList.toFloat();
    const auto steps = shownSteps();
    const float row = juce::jmin (36.0f, list.getHeight() / juce::jmax (1.0f, (float) steps.size()));
    const bool pictures = row >= 16.0f && thumbs.size() == steps.size();
    int stepIndex = -1;
    g.setFont (type::font (juce::jmin (type::kCaption, row * 0.75f), true));
    for (const auto& step : steps)
    {
        ++stepIndex;
        auto r = list.removeFromTop (row);
        if (isMarker (step))
        {
            g.setColour (theme.outline);
            g.fillRect (r.withSizeKeepingCentre (r.getWidth(), 1.0f));
            continue;
        }
        const bool on = stepOn (*strip, step);
        g.setColour (isExtra (step) ? theme.accent : (on ? theme.text : theme.textDim));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ r.getX() + 5.0f, r.getCentreY() }));
        const auto kind = pictures ? thumbs[(size_t) stepIndex].target.kind : BlockTarget::Kind::None;
        // The curves read small; the others (input, delay, insert) need a taller row for their text.
        if (kind == BlockTarget::Kind::Eq || kind == BlockTarget::Kind::Filter || kind == BlockTarget::Kind::Dynamics || (kind != BlockTarget::Kind::None && row >= 26.0f))
        {
            const auto picture = r.removeFromRight (juce::jmin (96.0f, r.getWidth() * 0.55f)).reduced (1.0f);
            thumbs[(size_t) stepIndex].picture.paint (g, picture, thumbs[(size_t) stepIndex].target, theme, on, 0.0f);
        }
        g.drawText (stepName (*strip, step), r.withTrimmedLeft (14.0f), juce::Justification::centredLeft, false);
        if (! on)
        {
            g.setColour (theme.background);
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ r.getX() + 5.0f, r.getCentreY() }));
        }
    }
}

} // namespace livedaw::ui::universal
