#include "ui/universal/ChannelPanel.h"

#include "ui/ChainSummary.h"
#include "ui/DelayUnits.h"

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kTitle = 24, kKeys = 36;

    /** The sections' widths at 1920 px (SENDS takes the rest); narrower windows scale them. */
    constexpr int kWidths[] = { 210, 200, 210, 380, 220, 180 };
} // namespace

ChannelPanel::ChannelPanel (UiContext& c, const Theme& t)
    : ctx (c), theme (t), trim (t), balance (t), delay (t), invert ("INVERT", t.accent, t), delayOn ("DELAY", t.accent, t),
      lowCut ("LOW CUT", t.accent, t), highCut ("HIGH CUT", t.accent, t), gateOn ("ON", t.accent, t), eqOn ("ON", t.accent, t),
      dynOn ("ON", t.accent, t), filterView (t), eqView (t), gateView (t), dynView (t), sends (c, t)
{
    for (juce::Component* comp : { (juce::Component*) &trim, (juce::Component*) &balance, (juce::Component*) &delay, (juce::Component*) &invert,
                                   (juce::Component*) &delayOn, (juce::Component*) &lowCut, (juce::Component*) &highCut,
                                   (juce::Component*) &gateOn, (juce::Component*) &eqOn, (juce::Component*) &dynOn,
                                   (juce::Component*) &filterView, (juce::Component*) &eqView, (juce::Component*) &gateView,
                                   (juce::Component*) &dynView, (juce::Component*) &sends })
        addChildComponent (comp);
    eqView.rangeDb = 15.0f;

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
    sends.setCompact (true);
    gateView.reductionDb = [this] { return strip != nullptr ? strip->meters.gateReductionDb.load() : 0.0f; };
    dynView.reductionDb = [this] { return strip != nullptr ? strip->meters.dynReductionDb.load() : 0.0f; };
}

ChannelPanel::~ChannelPanel() = default;

const char* ChannelPanel::sectionName (int s)
{
    static const char* names[] = { "INPUT", "FILTER", "GATE", "EQ", "DYNAMICS", "CHAIN", "SENDS" };
    return names[juce::jlimit (0, (int) NumSections - 1, s)];
}

double ChannelPanel::sampleRate() const
{
    const double sr = ctx.engine.getSampleRate();
    return sr > 0.0 ? sr : 48000.0;
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
    const bool shown = strip != nullptr;
    for (auto* c : getChildren())
        c->setVisible (shown);
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
    if (strip == nullptr)
        return;
    for (auto* bar : { &trim, &balance, &delay })
        bar->refresh();
    for (auto* key : { &invert, &delayOn, &lowCut, &highCut, &gateOn, &eqOn, &dynOn })
        key->repaint();
    for (auto* view : { &filterView, &eqView })
        view->refresh();
    gateView.refresh();
    dynView.refresh();
    sends.refresh();
    repaint (areas[ChainSection]);
}

void ChannelPanel::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap, sizes::kGapSmall);
    const float scale = juce::jmin (1.0f, (float) area.getWidth() / 1904.0f);
    for (int s = 0; s < NumSections; ++s)
    {
        areas[(size_t) s] = s < (int) std::size (kWidths) ? area.removeFromLeft (juce::roundToInt ((float) kWidths[s] * scale)) : area;
        area.removeFromLeft (sizes::kGapSmall);
    }
    const auto body = [this] (int s) { return areas[(size_t) s].withTrimmedTop (kTitle).reduced (sizes::kGapSmall); };

    {
        auto b = body (InputSection);
        const int row = juce::jmin (sizes::kTouch, b.getHeight() / 4);
        trim.setBounds (b.removeFromTop (row).reduced (0, 2));
        balance.setBounds (b.removeFromTop (row).reduced (0, 2));
        auto keys = b.removeFromTop (row).reduced (0, 2);
        invert.setBounds (keys.removeFromLeft (keys.getWidth() / 2).reduced (2, 0));
        delayOn.setBounds (keys.reduced (2, 0));
        delay.setBounds (b.removeFromTop (row).reduced (0, 2));
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
        key->setBounds (b.removeFromBottom (kKeys).withWidth (juce::jmin (120, b.getWidth())));
        view->setBounds (b.withTrimmedBottom (sizes::kGapSmall));
    }
    chainList = body (ChainSection);
    sends.setBounds (body (SendsSection));
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
    for (int s = 0; s < NumSections; ++s)
    {
        const auto r = areas[(size_t) s].toFloat();
        g.setColour (theme.panel);
        g.fillRoundedRectangle (r, sizes::kRadius);
        auto title = r.withHeight ((float) kTitle).reduced ((float) sizes::kGap, 0.0f);
        juce::String text (sectionName (s));
        if (s == InputSection)
            text = strip->getLabel().toUpperCase() + "  " + strip->getName();
        // The strip's own blocks are its WING template's (the tag, at the right first, so
        // the name keeps clear of it in a slim section); a dynamics block's model after its name.
        const bool tagged = s == FilterSection || s == GateSection || s == EqSection || s == DynamicsSection;
        if (tagged)
            title.removeFromRight (drawDeviceTag (g, title, devices::wing, theme) + (float) sizes::kGapSmall);
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kCaption, true));
        g.drawFittedText (text, title.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        if (tagged)
        {
            // The name's width in the font it was drawn with, and a gap.
            title.removeFromLeft (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) * 1.04f + (float) sizes::kGapSmall + 2.0f);
            if (s == GateSection || s == DynamicsSection)
            {
                const auto& slot = s == GateSection ? strip->params.gate() : strip->params.dynamics();
                g.setColour (theme.text);
                g.setFont (type::font (type::kCaption - 2.0f, true)); // a size under the section's name
                // Narrowed rather than cut ("WING CO..."): the sections are slim. Too long
                // even so, it drops "WING " (the section's tag says it) and gets a little smaller.
                auto modelName = models::model (models::Dynamics, slot.model.load()).name.toUpperCase();
                const auto fits = [&g, &title] (const juce::String& s) { return juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), s) * 0.7f <= title.getWidth(); };
                if (! fits (modelName) && modelName.startsWith ("WING "))
                    modelName = modelName.substring (5);
                // Squashed to 0.7, or a size smaller squashed to 0.55; if it doesn't fit even
                // so, it is left out (a few cut letters say less than none: the editor names it).
                if (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), modelName) * 0.7f <= title.getWidth())
                    g.drawFittedText (modelName, title.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
                else if (const auto smaller = type::font (type::kCaption - 3.0f, true);
                         juce::GlyphArrangement::getStringWidth (smaller, modelName) * 0.55f <= title.getWidth())
                {
                    g.setFont (smaller);
                    g.drawFittedText (modelName, title.toNearestInt(), juce::Justification::centredLeft, 1, 0.55f);
                }
            }
        }
        if ((s == FilterSection || s == GateSection) && ! channel)
        {
            g.setColour (theme.textDim);
            g.drawText ("CHANNELS ONLY", r, juce::Justification::centred, false);
        }
    }

    // CHAIN: the strip's processing in order; the Universal core's own in the accent.
    auto list = chainList.toFloat();
    std::vector<ChainStep> steps;
    for (const auto& step : strip->getChain())
        if (isShown (step))
            steps.push_back (step);
    const float row = juce::jmin (20.0f, list.getHeight() / juce::jmax (1.0f, (float) steps.size()));
    g.setFont (type::font (juce::jmin (type::kCaption, row * 0.75f), true));
    for (const auto& step : steps)
    {
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
        g.drawText (stepName (*strip, step), r.withTrimmedLeft (14.0f), juce::Justification::centredLeft, false);
        if (! on)
        {
            g.setColour (theme.background);
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ r.getX() + 5.0f, r.getCentreY() }));
        }
    }
}

} // namespace livedaw::ui::universal
