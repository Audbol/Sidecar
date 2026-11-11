#include "ui/universal/BlockEditor.h"

#include "ui/Faceplate.h"
#include "ui/universal/PluginBlockCard.h"

#include "ui/DelayUnits.h"

#include <algorithm>

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kTitle = 36, kRow = sizes::kTouch, kKey = sizes::kTouch, kCurve = 120, kMaxCurve = 260;
    /** A switch beside what it switches. */
    constexpr int kSwitch = 88;

    double sampleRateOf (UiContext& ctx)
    {
        const double sr = ctx.engine.getSampleRate();
        return sr > 0.0 ? sr : 48000.0;
    }

    /** A dynamics block's gain reduction as a bar from the right. */
    class ReductionMeter final : public juce::Component
    {
    public:
        explicit ReductionMeter (const Theme& t) : theme (t) { setInterceptsMouseClicks (false, false); }

        void setReduction (float db)
        {
            db = juce::jlimit (0.0f, 30.0f, std::abs (db));
            if (std::abs (db - shown) > 0.1f)
            {
                shown = db;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            const auto area = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (theme.well);
            g.fillRoundedRectangle (area, sizes::kRadius);
            g.setColour (theme.solo.withAlpha (0.7f));
            const float w = area.getWidth() * shown / 30.0f;
            g.fillRoundedRectangle (area.withLeft (area.getRight() - w), sizes::kRadius);
            auto text = area.reduced (10.0f, 0.0f);
            g.setColour (theme.textDim);
            g.setFont (type::font (type::kCaption, true));
            g.drawText ("GAIN REDUCTION", text, juce::Justification::centredLeft);
            g.setColour (theme.text);
            g.setFont (type::font (type::kValue));
            g.drawText (juce::String (-shown, 1) + " dB", text, juce::Justification::centredRight);
        }

    private:
        const Theme& theme;
        float shown = 0.0f;
    };
} // namespace

//==============================================================================
juce::String BlockTarget::tag() const
{
    const auto of = [this] (int model)
    {
        const auto& m = models::model (table, model);
        return devices::of (m.token, m.foreign, device);
    };
    if (eq != nullptr)
        return of (eq->model.load());
    if (dynamics != nullptr)
        return of (dynamics->model.load());
    return device;
}

BlockTarget BlockTarget::of (UiContext& ctx, Strip& strip, const ChainStep& step)
{
    BlockTarget t;
    auto& p = strip.params;
    const double sr = sampleRateOf (ctx);
    // The strip's own blocks: their curves from ld_strip_response (1 EQ, 3 FILTER, 5 / 6 the key filters).
    const auto own = [&strip, sr] (int block)
    {
        return [&strip, sr, block] (const double* f, double* db, int n)
        {
            ld::ld_strip_response (strip.getHandle(), block, f, db, n, sr);
            return true;
        };
    };
    const auto withModel = [&strip] (ModelBlock block) { return [&strip, block] (int m) { strip.setModel (block, m); }; };

    // The strip's own blocks are its WING template's; an extra block (below) is the core's.
    t.device = devices::shown();
    switch (step.kind)
    {
        case ld::kStepInput:
            t.kind = Kind::Input;
            t.title = "INPUT";
            t.input = &p.input;
            break;
        case ld::kStepDelay:
            t.kind = Kind::Delay;
            t.title = "DELAY";
            t.delayOn = &p.input.delayOn;
            t.delayMode = &p.input.delayMode;
            t.delay = &p.input.delay;
            break;
        case ld::kStepFilter:
            t.kind = Kind::Filter;
            t.title = "FILTER";
            t.filter = &p.filter();
            t.table = models::FilterTools;
            t.setModel = withModel (ModelBlock::FilterTool);
            t.response = own (3);
            break;
        case ld::kStepGate:
            t.kind = Kind::Dynamics;
            t.title = "GATE";
            t.dynamics = &p.gate();
            t.setModel = withModel (ModelBlock::Gate);
            t.modelSlot = 0;
            t.response = own (5);
            t.reductionDb = [&strip] { return strip.meters.gateReductionDb.load(); };
            break;
        case ld::kStepEq:
            t.kind = Kind::Eq;
            t.title = "EQ";
            t.eq = &p.eq();
            t.table = (models::Table) strip.getEqTable();
            t.setModel = withModel (ModelBlock::Eq);
            t.fixedModel = ctx.engine.getProfile() == ld::kProfileYamaha01V || ctx.engine.getProfile() == ld::kProfileDLive;
            t.response = own (1);
            break;
        case ld::kStepDynamics:
            t.kind = Kind::Dynamics;
            t.title = "DYNAMICS";
            t.dynamics = &p.dynamics();
            t.setModel = withModel (ModelBlock::Dynamics);
            t.modelSlot = 1;
            t.response = own (6);
            t.reductionDb = [&strip] { return strip.meters.dynReductionDb.load(); };
            if (ctx.engine.getProfile() == ld::kProfileDLive && strip.getKind() == StripKind::Channel)
                t.compressorOrder = &p.processOrder;
            break;
        case ld::kStepPreInsert:
            t.kind = Kind::Insert;
            t.title = "INSERT 1";
            t.insert = &p.preInsert();
            break;
        case ld::kStepPostInsert:
            t.kind = Kind::Insert;
            t.title = "INSERT 2";
            t.insert = &p.postInsert();
            t.postInsert = strip.hasAutomix();
            break;
        case ld::kStepBlock:
        {
            const auto block = strip.getBlock (step.uid);
            if (! block.isValid())
                break;
            const int uid = step.uid;
            t.uid = uid;
            t.device = {};
            const auto curve = [&strip, sr, uid] (int part)
            {
                return [&strip, sr, uid, part] (const double* f, double* db, int n) { return strip.getBlockResponse (uid, part, f, db, n, sr); };
            };
            const auto setBlockModel = [&strip, uid] (int m) { strip.setBlockModel (uid, m); };
            switch (block.getKind())
            {
                case BlockKind::Filter:
                    t.kind = Kind::Filter;
                    t.title = "FILTER BLOCK";
                    t.filter = block.filter();
                    t.table = models::FilterTools;
                    t.setModel = setBlockModel;
                    t.response = curve (0);
                    break;
                case BlockKind::Eq:
                    t.kind = Kind::Eq;
                    t.title = "EQ BLOCK";
                    t.eq = block.eq();
                    t.table = (models::Table) strip.getEqTable();
                    t.setModel = setBlockModel;
                    t.response = curve (0);
                    break;
                case BlockKind::Dynamics:
                {
                    t.kind = Kind::Dynamics;
                    // A dLive input's preamp is a dynamics block of its own (DEEP Processing).
                    const bool preamp = uid == ld::ld_strip_preamp_block (strip.getHandle());
                    t.title = preamp ? "PREAMP" : "DYNAMICS BLOCK";
                    t.modelSlot = preamp ? 2 : 1;
                    t.dynamics = block.dynamics();
                    t.setModel = setBlockModel;
                    t.response = curve (0);
                    auto* meters = block.getMeters();
                    t.reductionDb = [meters] { return meters != nullptr ? meters->reductionDb.load() : 0.0f; };
                    break;
                }
                case BlockKind::Insert:
                    t.kind = Kind::Insert;
                    t.title = "INSERT BLOCK";
                    t.insert = block.insert();
                    break;
                case BlockKind::Delay:
                    t.kind = Kind::Delay;
                    t.title = "DELAY BLOCK";
                    t.delayOn = &block.delay()->on;
                    t.delayMode = &block.delay()->mode;
                    t.delay = &block.delay()->delay;
                    break;
                case BlockKind::Plugin:
                    t.kind = Kind::Plugin;
                    t.title = "PLUG-IN BLOCK";
                    t.pluginOn = block.plugin();
                    t.strip = &strip;
                    break;
            }
            break;
        }
        case ld::kStepSlot:
        case ld::kStepPfl:
        case ld::kStepFader:
        case ld::kStepPost:
        case ld::kStepTapShaper:
        case ld::kStepTap:
        case ld::kStepRta:
            t.device = {};
            break;
    }
    return t;
}

//==============================================================================
BlockEditor::BlockEditor (UiContext& c, const Theme& t) : ctx (c), theme (t)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme.outline);
    addAndMakeVisible (viewport);
}

void BlockEditor::setTarget (BlockTarget newTarget)
{
    target = std::move (newTarget);
    build();
}

juce::Component* BlockEditor::getControl (int index) const noexcept
{
    return juce::isPositiveAndBelow (index, (int) rows.size()) ? rows[(size_t) index].first.get() : nullptr;
}

template <typename C>
C& BlockEditor::add (std::unique_ptr<C> c, int height)
{
    auto& ref = *c;
    content.addAndMakeVisible (ref);
    rows.emplace_back (std::move (c), height);
    return ref;
}

ToggleKey& BlockEditor::addKey (const juce::String& label, std::atomic<bool>& value)
{
    auto& key = add (std::make_unique<ToggleKey> (label, theme.accent, theme), kKey);
    key.isOn = [&value] { return value.load(); };
    key.onClick = [&value] { value.store (! value.load()); };
    refreshers.push_back ([&key] { key.refresh(); });
    return key;
}

ValueBar& BlockEditor::addBar (ValueBar::Binding binding)
{
    auto& bar = add (std::make_unique<ValueBar> (theme), kRow);
    bar.bind (std::move (binding));
    refreshers.push_back ([&bar] { bar.refresh(); });
    return bar;
}

ChoiceBar& BlockEditor::addChoice (const juce::String& caption, std::function<juce::StringArray()> items, std::function<int()> current,
                                   std::function<void (int)> choose, const juce::String& device)
{
    auto& choice = add (std::make_unique<ChoiceBar> (theme), kRow);
    choice.caption = caption;
    choice.device = device;
    choice.items = std::move (items);
    choice.current = std::move (current);
    choice.choose = std::move (choose);
    refreshers.push_back ([&choice] { choice.refresh(); });
    return choice;
}

void BlockEditor::addModel (std::atomic<int32_t>& model)
{
    const auto table = target.table;
    // What this block may hold: on a dLive the preamp models go in their block alone.
    std::vector<int> ids;
    juce::StringArray names;
    // Every console's models of the kind, the shown console's first, each console together (the
    // tables hold them so); a real console under remote control offers its own alone.
    for (const auto& m : models::offered (table))
        if (table != models::Dynamics || target.modelSlot < 0 || ld::ld_dyn_model_allowed (m.index, target.modelSlot))
        {
            ids.push_back (m.index);
            names.add (m.name);
        }
    auto& choice = addChoice ("MODEL", [names] { return names; },
                              [ids, &model]
                              {
                                  const auto found = std::find (ids.begin(), ids.end(), model.load());
                                  return found == ids.end() ? -1 : (int) std::distance (ids.begin(), found);
                              },
                              [ids, set = target.setModel] (int i)
                              {
                                  if (set && juce::isPositiveAndBelow (i, (int) ids.size()))
                                      set (ids[(size_t) i]);
                              },
                              devices::shown()); // the WING's model tables
    refreshers.push_back ([&choice, &model, table]
    {
        // The MODEL tag follows the model: another console's names that console.
        const auto& m = models::model (table, model.load());
        const auto tag = devices::of (m.token, m.foreign, devices::shown());
        if (tag != choice.device)
        {
            choice.device = tag;
            choice.repaint();
        }
    });
    // Chosen by their pictures: the WING's front panels.
    choice.paintItem = [ids, table] (juce::Graphics& g, juce::Rectangle<float> r, int i) { Faceplate::paintThumbnail (g, r, table, ids[(size_t) i], {}); };
}

void BlockEditor::addParams (std::atomic<int32_t>& model, std::atomic<float>* values, int numValues)
{
    auto panel = std::make_unique<ParamPanel> (theme);
    panel->bind (target.table, &model, values, numValues);
    auto& ref = add (std::move (panel), 0);
    refreshers.push_back ([this, &ref]
    {
        const int before = ref.getNumControls();
        ref.refresh();
        if (ref.getNumControls() != before)
            resized(); // another model: other parameters
    });
}

void BlockEditor::joinLast (int count, std::vector<int> widths)
{
    lines.push_back ({ (int) rows.size() - count, count, std::move (widths) });
}

void BlockEditor::addResponse (std::function<std::vector<ResponseView::Handle>()> handles, int height)
{
    auto view = std::make_unique<ResponseView> (theme);
    view->response = target.response;
    view->handleSource = std::move (handles);
    view->onEdited = [this] { refresh(); }; // the bars follow a handle
    response = view.get();
    add (std::move (view), height);
    refreshers.push_back ([this] { response->refresh(); });
}

void BlockEditor::addTransfer (DynSlotParams& d)
{
    auto view = std::make_unique<TransferView> (theme);
    view->bind (&d);
    view->reductionDb = target.reductionDb;
    view->onEdited = [this] { refresh(); };
    transfer = view.get();
    add (std::move (view), -1);
    refreshers.push_back ([this] { transfer->refresh(); });
}

void BlockEditor::build()
{
    rows.clear();
    lines.clear();
    refreshers.clear();
    response = nullptr;
    transfer = nullptr;
    const double sr = sampleRateOf (ctx);

    switch (target.kind)
    {
        case BlockTarget::Kind::Input:
        {
            auto& in = *target.input;
            addBar (ValueBar::linear ("TRIM", in.trimDb, -18.0f, 18.0f, 0.0f, " dB", 1));
            addBar (ValueBar::linear ("BALANCE", in.balanceDb, -9.0f, 9.0f, 0.0f, " dB", 1));
            addKey ("INVERT", in.invert);
            break;
        }
        case BlockTarget::Kind::Filter:
        {
            auto& f = *target.filter;
            addResponse ([&f]
            {
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
                return std::vector<ResponseView::Handle> { low, high };
            });
            addKey ("LOW CUT", f.lowCut);
            addBar (ValueBar::frequency ("LOW CUT", f.lowCutFreq, 20.0f, 2000.0f, kLowCutDefaultHz));
            joinLast (2, { kSwitch + 24 });
            addChoice ("SLOPE", [] { return models::list (models::LowCutSlopes); }, [&f] { return f.lowCutSlope.load(); },
                       [&f] (int i) { f.lowCutSlope = i; }, devices::shown());
            addKey ("HIGH CUT", f.highCut);
            addBar (ValueBar::frequency ("HIGH CUT", f.highCutFreq, 50.0f, 20000.0f, kHighCutDefaultHz));
            joinLast (2, { kSwitch + 24 });
            addChoice ("SLOPE", [] { return models::list (models::HighCutSlopes); }, [&f] { return f.highCutSlope.load(); },
                       [&f] (int i) { f.highCutSlope = i; }, devices::shown());
            addKey ("TOOL", f.tool);
            addModel (f.toolModel);
            joinLast (2, { kSwitch });
            addParams (f.toolModel, f.toolParams, ld::kMaxToolParams);
            break;
        }
        case BlockTarget::Kind::Eq:
        {
            auto& eq = *target.eq;
            addKey ("ON", eq.on);
            if (! target.fixedModel)
            {
                addModel (eq.model);
                joinLast (2, { kSwitch });
            }
            addResponse ([&eq, table = target.table] { return ResponseView::eqHandles (models::model (table, eq.model.load()), eq.values); });
            addBar (ValueBar::linear ("MIX", eq.mix, 0.0f, 125.0f, 100.0f, " %", 0));
            addParams (eq.model, eq.values, ld::kMaxEqParams);
            break;
        }
        case BlockTarget::Kind::Dynamics:
        {
            auto& d = *target.dynamics;
            addKey ("ON", d.on);
            if (auto* order = target.compressorOrder)
            {
                auto& key = add (std::make_unique<ToggleKey> ("PRE PEQ", theme.accent, theme), kKey);
                key.isOn = [order] { return order->load() == 1; };
                key.onClick = [order] { order->store (order->load() == 1 ? 0 : 1); };
                refreshers.push_back ([&key] { key.refresh(); });
            }
            addModel (d.model);
            if (target.compressorOrder != nullptr)
                joinLast (3, { kSwitch, kSwitch });
            else
                joinLast (2, { kSwitch });
            addTransfer (d);
            auto& meter = add (std::make_unique<ReductionMeter> (theme), kRow);
            refreshers.push_back ([&meter, gr = target.reductionDb] { meter.setReduction (gr ? gr() : 0.0f); });
            addBar (ValueBar::linear ("MIX", d.mix, 0.0f, 100.0f, 100.0f, " %", 0));
            addBar (ValueBar::linear ("GAIN", d.gain, -6.0f, 12.0f, 0.0f, " dB", 1));
            joinLast (2, { 0 });
            addParams (d.model, d.values, ld::kMaxDynParams);
            addResponse ({}, kCurve); // the key filter
            break;
        }
        case BlockTarget::Kind::Insert:
        {
            auto& ins = *target.insert;
            addKey ("ON", ins.on);
            addChoice ("FX SLOT", [this]
            {
                juce::StringArray items { "NONE" };
                for (int i = 0; i < ctx.engine.getCounts().fx; ++i)
                    items.add ("FX " + juce::String (i + 1));
                return items;
            },
                       [&ins] { return juce::jmax (0, ins.fx.load() + 1); }, [&ins] (int i) { ins.fx = i - 1; });
            joinLast (2, { kSwitch });
            if (target.postInsert)
                addChoice ("MODE", [] { return models::list (models::PostInsertModes); }, [&ins] { return ins.mode.load(); },
                           [&ins] (int i) { ins.mode = i; }, devices::shown());
            break;
        }
        case BlockTarget::Kind::Delay:
        {
            auto& on = *target.delayOn;
            auto& mode = *target.delayMode;
            auto& value = *target.delay;
            addKey ("ON", on);
            // Another unit keeps the delay's time.
            addChoice ("UNIT", [] { return models::list (models::DelayUnits); }, [&mode] { return mode.load(); },
                       [&mode, &value, sr] (int i)
                       {
                           const int from = juce::jlimit (0, 3, (int) mode.load());
                           const float ms = delayToMs (value.load(), from, (float) sr);
                           mode = juce::jlimit (0, 3, i);
                           value = delayRange (mode.load(), (float) sr).snapToLegalValue (msToDelay (ms, mode.load(), (float) sr));
                       },
                       devices::shown());
            joinLast (2, { kSwitch });
            ValueBar::Binding b;
            b.caption = "DELAY";
            b.value = &value;
            b.toPosition = [&mode, sr] (float v) { return delayRange (juce::jlimit (0, 3, (int) mode.load()), (float) sr).convertTo0to1 (v); };
            b.fromPosition = [&mode, sr] (float p)
            {
                const int m = juce::jlimit (0, 3, (int) mode.load());
                const float v = delayRange (m, (float) sr).convertFrom0to1 (p);
                return m == 3 ? std::round (v) : v; // whole samples
            };
            b.text = [&mode] (float v) { return delayText (v, juce::jlimit (0, 3, (int) mode.load())); };
            b.defaultValue = msToDelay (delayToMs (kMinDelayM, 0, (float) sr), juce::jlimit (0, 3, (int) mode.load()), (float) sr);
            addBar (std::move (b));
            break;
        }
        case BlockTarget::Kind::Plugin:
        {
            auto card = std::make_unique<PluginBlockCard> (ctx, theme);
            card->setBlock (target.strip, target.uid, target.pluginOn);
            add (std::move (card), 150);
            break;
        }
        case BlockTarget::Kind::None:
            break;
    }
    resized();
    refresh();
    repaint();
}

void BlockEditor::refresh()
{
    for (auto& r : refreshers)
        r();
}

void BlockEditor::paint (juce::Graphics& g)
{
    auto title = getLocalBounds().removeFromTop (kTitle).reduced (sizes::kGap, 0).toFloat();
    g.setColour (theme.text);
    const auto font = type::font (type::kHeading, true);
    g.setFont (font);
    const auto text = target.kind == BlockTarget::Kind::None ? juce::String ("Select a block to edit it") : target.title;
    g.drawText (text, title, juce::Justification::centredLeft);
    drawDeviceTag (g, title.withTrimmedLeft (juce::GlyphArrangement::getStringWidth (font, text) + 10.0f),
                   target.kind == BlockTarget::Kind::None ? juce::String() : target.tag(), theme, true);
}

void BlockEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (kTitle);
    viewport.setBounds (area);
    const int width = juce::jmax (0, viewport.getWidth() - viewport.getScrollBarThickness() - sizes::kGapSmall);
    int y = 0;
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto line = std::find_if (lines.begin(), lines.end(), [i] (const Line& l) { return l.first == i; });
        if (line == lines.end())
        {
            const int height = heightOf (i, width);
            rows[(size_t) i].first->setBounds (0, y, width, height);
            y += height + sizes::kGapSmall;
            continue;
        }
        // Side by side, as tall as the tallest.
        int height = 0, shares = 1, rest = width - (line->count - 1) * sizes::kGapSmall;
        for (int k = 0; k < line->count; ++k)
            height = juce::jmax (height, heightOf (i + k, width));
        for (int w : line->widths)
        {
            if (w > 0)
                rest -= w;
            else
                ++shares;
        }
        int x = 0;
        for (int k = 0; k < line->count; ++k)
        {
            const int w = k == line->count - 1 ? width - x : (k < (int) line->widths.size() && line->widths[(size_t) k] > 0 ? line->widths[(size_t) k] : rest / shares);
            rows[(size_t) (i + k)].first->setBounds (x, y, w, height);
            x += w + sizes::kGapSmall;
        }
        y += height + sizes::kGapSmall;
        i += line->count - 1;
    }
    content.setSize (width, y);
}

int BlockEditor::heightOf (int row, int width) const
{
    const auto& [c, h] = rows[(size_t) row];
    if (auto* panel = dynamic_cast<ParamPanel*> (c.get()))
        return panel->getHeightFor (width);
    return h >= 0 ? h : juce::jlimit (kCurve, kMaxCurve, viewport.getHeight() * 2 / 5);
}

} // namespace livedaw::ui::universal
