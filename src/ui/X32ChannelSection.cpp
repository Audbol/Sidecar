#include "ui/X32ChannelSection.h"

#include "engine/Models.h"

namespace livedaw::ui::x32
{

using namespace universal;

namespace
{
    const char* const kBandNames4[] = { "LOW", "LO MID", "HI MID", "HIGH" };
    const char* const kBandNames6[] = { "LOW", "LOW 2", "LO MID", "HI MID", "HIGH 2", "HIGH" };
    constexpr int kKey = 30;

    /** A parameter of a model held by a block (`values` its values), as a knob. */
    KnobBinding bindParam (models::Table table, int model, std::atomic<float>* values, const juce::String& id, const juce::String& label)
    {
        const auto& m = models::model (table, model);
        const int i = m.find (id);
        if (i < 0)
            return {};
        auto b = bindModelParam (values[i], m.params[(size_t) i]);
        b.label = label;
        return b;
    }
} // namespace

ChannelSection::ChannelSection (UiContext& c, const Theme& t)
    : ctx (c), theme (t), lowCutOn ("LOW CUT", t.accent, t), invert (juce::String::fromUTF8 ("\xc3\x98"), t.accent, t), gateOn ("GATE", t.accent, t),
      dynOn ("COMP", t.accent, t), eqOn ("EQ", t.accent, t), lrOn ("LR", t.accent, t), mcOn ("M/C", t.accent, t)
{
    for (auto* k : { &gain, &lowCut, &gateThr, &dynThr, &dynRatio, &eqType, &eqFreq, &eqGain, &eqQ, &pan, &mcLevel })
    {
        k->setCompact (true);
        addAndMakeVisible (*k);
    }
    for (auto& k : sends)
    {
        k.setCompact (true);
        addAndMakeVisible (k);
    }
    // The section keys switch a block (or a send) on and off.
    lowCutOn.onClick = [this] { if (strip != nullptr) strip->params.filter().lowCut = ! strip->params.filter().lowCut.load(); };
    lowCutOn.isOn = [this] { return strip != nullptr && strip->params.filter().lowCut.load(); };
    invert.onClick = [this] { if (strip != nullptr) strip->params.input.invert = ! strip->params.input.invert.load(); };
    invert.isOn = [this] { return strip != nullptr && strip->params.input.invert.load(); };
    gateOn.onClick = [this] { if (strip != nullptr) strip->params.gate().on = ! strip->params.gate().on.load(); };
    gateOn.isOn = [this] { return strip != nullptr && strip->params.gate().on.load(); };
    dynOn.onClick = [this] { if (strip != nullptr) strip->params.dynamics().on = ! strip->params.dynamics().on.load(); };
    dynOn.isOn = [this] { return strip != nullptr && strip->params.dynamics().on.load(); };
    eqOn.onClick = [this] { if (strip != nullptr) strip->params.eq().on = ! strip->params.eq().on.load(); };
    eqOn.isOn = [this] { return strip != nullptr && strip->params.eq().on.load(); };
    lrOn.onClick = [this] { if (strip != nullptr) strip->mains[0].on = ! strip->mains[0].on.load(); };
    lrOn.isOn = [this] { return strip != nullptr && strip->mains[0].on.load(); };
    mcOn.onClick = [this] { if (strip != nullptr && ctx.engine.getMains().size() > 1) strip->mains[1].on = ! strip->mains[1].on.load(); };
    mcOn.isOn = [this] { return strip != nullptr && ctx.engine.getMains().size() > 1 && strip->mains[1].on.load(); };
    for (auto* k : { &lowCutOn, &invert, &gateOn, &dynOn, &eqOn, &lrOn, &mcOn })
        addAndMakeVisible (*k);

    for (int b = 0; b < 6; ++b)
    {
        auto key = std::make_unique<ToggleKey> (kBandNames6[b], t.accent, t);
        key->isOn = [this, b] { return band == b; };
        key->onClick = [this, b] { setBand (b); };
        addAndMakeVisible (*key);
        bandKeys.push_back (std::move (key));
    }
    for (int b = 0; b < 4; ++b)
    {
        auto key = std::make_unique<ToggleKey> (juce::String (4 * b + 1) + "-" + juce::String (4 * b + 4), t.accent, t);
        key->isOn = [this, b] { return sendBank == b; };
        key->onClick = [this, b] { setSendBank (b); };
        addAndMakeVisible (*key);
        bankKeys.push_back (std::move (key));
    }
    sections = { { { "CONFIG / PREAMP", {} }, { "GATE", {} }, { "DYNAMICS", {} }, { "EQUALIZER", {} }, { "BUS SENDS", {} }, { "MAIN BUS", {} } } };
    for (size_t s = 0; s < sections.size(); ++s)
    {
        auto key = std::make_unique<ToggleKey> ("VIEW", t.accent, t);
        key->onClick = [this] { if (onView) onView(); };
        addAndMakeVisible (*key);
        viewKeys.push_back (std::move (key));
    }
    setStrip (nullptr);
    startTimerHz (20);
}

ChannelSection::~ChannelSection()
{
    stopTimer();
}

int ChannelSection::numBands() const noexcept
{
    return strip != nullptr && strip->isOutput() ? 6 : 4;
}

void ChannelSection::setStrip (Strip* s)
{
    strip = s;
    band = juce::jmin (band, numBands() - 1);
    // Bands are named by how many the EQ has.
    for (int b = 0; b < 6; ++b)
    {
        auto& key = *bandKeys[(size_t) b];
        key.setVisible (b < numBands());
        key.setButtonText (numBands() == 4 ? (b < 4 ? kBandNames4[b] : "") : kBandNames6[b]);
    }
    bind();
    resized();
    repaint();
}

void ChannelSection::setBand (int b)
{
    band = juce::jlimit (0, numBands() - 1, b);
    bind();
}

void ChannelSection::setSendBank (int b)
{
    sendBank = juce::jlimit (0, 3, b);
    bind();
}

void ChannelSection::bind()
{
    if (strip == nullptr)
    {
        for (auto* k : { &gain, &lowCut, &gateThr, &dynThr, &dynRatio, &eqType, &eqFreq, &eqGain, &eqQ, &pan, &mcLevel })
            k->setBinding ({});
        for (auto& k : sends)
            k.setBinding ({});
        timerCallback();
        return;
    }
    auto& p = strip->params;
    gain.setBinding (bindLinear ("GAIN", p.input.trimDb, -18.0f, 18.0f, " dB", 1, 0.0f));
    lowCut.setBinding (bindFreq ("LOW CUT", p.filter().lowCutFreq, 20.0f, 400.0f, 20.0f));
    gateThr.setBinding (bindParam (models::Dynamics, p.gate().model.load(), p.gate().values, "thr", "THRESHOLD"));
    dynThr.setBinding (bindParam (models::Dynamics, p.dynamics().model.load(), p.dynamics().values, "thr", "THRESHOLD"));
    dynRatio.setBinding (bindParam (models::Dynamics, p.dynamics().model.load(), p.dynamics().values, "ratio", "RATIO"));
    const auto table = strip->isOutput() ? models::BusEq : models::ChannelEq;
    const auto n = juce::String (band + 1);
    eqType.setBinding (bindParam (table, p.eq().model.load(), p.eq().values, n + "t", "TYPE"));
    eqFreq.setBinding (bindParam (table, p.eq().model.load(), p.eq().values, n + "f", "FREQ"));
    eqGain.setBinding (bindParam (table, p.eq().model.load(), p.eq().values, n + "g", "GAIN"));
    eqQ.setBinding (bindParam (table, p.eq().model.load(), p.eq().values, n + "q", "Q"));
    pan.setBinding (bindPan ("PAN", p.pan));
    if (ctx.engine.getMains().size() > 1)
        mcLevel.setBinding (bindFader ("M/C LEVEL", strip->mains[1].levelDb));
    else
        mcLevel.setBinding ({});
    // Bus sends: inputs (and buses to matrices aren't here: an X32's bus sends
    // section turns its sends to buses).
    const bool hasSends = strip->getKind() == StripKind::Channel || strip->getKind() == StripKind::Aux;
    for (int i = 0; i < 4; ++i)
    {
        const int slot = 4 * sendBank + i;
        if (hasSends && slot < (int) ctx.engine.getBuses().size())
            sends[(size_t) i].setBinding (bindFader ("BUS " + juce::String (slot + 1), strip->sends[(size_t) slot].levelDb));
        else
            sends[(size_t) i].setBinding ({});
    }
    timerCallback();
}

DisplayKnob* ChannelSection::knob (const juce::String& section, const juce::String& label)
{
    const std::pair<const char*, std::vector<DisplayKnob*>> map[] = {
        { "CONFIG / PREAMP", { &gain, &lowCut } }, { "GATE", { &gateThr } }, { "DYNAMICS", { &dynThr, &dynRatio } },
        { "EQUALIZER", { &eqType, &eqFreq, &eqGain, &eqQ } }, { "MAIN BUS", { &pan, &mcLevel } },
        { "BUS SENDS", { &sends[0], &sends[1], &sends[2], &sends[3] } }
    };
    for (auto& [name, knobs] : map)
        if (section == name)
            for (auto* k : knobs)
                if (k->getBinding().label == label)
                    return k;
    return nullptr;
}

void ChannelSection::timerCallback()
{
    for (auto* k : { &lowCutOn, &invert, &gateOn, &dynOn, &eqOn, &lrOn, &mcOn })
        k->refresh();
    for (auto* keys : { &bandKeys, &bankKeys })
        for (auto& k : *keys)
            k->refresh();
    // The gain reduction bars.
    repaint (sections[1].area);
    repaint (sections[2].area);
}

void ChannelSection::paint (juce::Graphics& g)
{
    for (auto& s : sections)
    {
        g.setColour (juce::Colour (0xff1b1d21));
        g.fillRoundedRectangle (s.area.toFloat(), 6.0f);
        g.setColour (juce::Colour (0xff8b9099));
        g.setFont (type::font (type::kCaption, true));
        g.drawText (s.title, s.area.reduced (8, 6).removeFromTop (16), juce::Justification::centredLeft);
    }
    // GATE and DYNAMICS: their gain reduction, as the X32's LED bars.
    auto reduction = [&g, this] (const Section& s, float db)
    {
        auto bar = s.area.reduced (10).removeFromBottom (14).toFloat();
        g.setColour (juce::Colour (0xff101113));
        g.fillRoundedRectangle (bar, 3.0f);
        const float amount = juce::jlimit (0.0f, 1.0f, db / 30.0f);
        g.setColour (theme.solo);
        g.fillRoundedRectangle (bar.removeFromRight (bar.getWidth() * amount), 3.0f);
        g.setColour (juce::Colour (0xff8b9099));
        g.setFont (type::font (type::kCaption));
        g.drawText ("GR", bar.withX ((float) s.area.getX() + 10.0f).withWidth (24.0f), juce::Justification::centredLeft);
    };
    const float gateGr = strip != nullptr ? strip->meters.gateReductionDb.load() : 0.0f;
    const float dynGr = strip != nullptr ? strip->meters.dynReductionDb.load() : 0.0f;
    reduction (sections[1], std::abs (gateGr));
    reduction (sections[2], std::abs (dynGr));
}

void ChannelSection::resized()
{
    auto area = getLocalBounds();
    // Six sections: the EQ wider (its band keys and four knobs).
    const float weights[] = { 1.2f, 0.8f, 1.0f, 1.8f, 1.55f, 1.0f };
    float total = 0.0f;
    for (auto w : weights)
        total += w;
    const int gap = sizes::kGap;
    const int width = area.getWidth() - gap * 5;
    for (size_t s = 0; s < sections.size(); ++s)
    {
        sections[s].area = area.removeFromLeft (juce::roundToInt ((float) width * weights[s] / total));
        area.removeFromLeft (gap);
    }

    auto inside = [] (const Section& s) { return s.area.reduced (8).withTrimmedTop (20); };
    auto keysRow = [] (juce::Rectangle<int>& r) { return r.removeFromBottom (kKey); };
    auto placeKnobs = [] (juce::Rectangle<int> r, std::initializer_list<DisplayKnob*> knobs, int perRow)
    {
        const int rows = ((int) knobs.size() + perRow - 1) / perRow;
        const int h = r.getHeight() / juce::jmax (1, rows);
        int i = 0;
        for (auto* k : knobs)
        {
            auto row = r.withHeight (h).translated (0, h * (i / perRow));
            const int w = row.getWidth() / perRow;
            k->setBounds (row.withWidth (w).translated (w * (i % perRow), 0).reduced (2));
            ++i;
        }
    };

    for (size_t s = 0; s < sections.size(); ++s)
    {
        auto r = inside (sections[s]);
        viewKeys[s]->setBounds (r.removeFromTop (kKey).removeFromRight (64));
    }
    {
        auto r = inside (sections[0]);
        r.removeFromTop (kKey + 4);
        auto keys = keysRow (r);
        invert.setBounds (keys.removeFromRight (40).reduced (2, 0)); // polarity
        lowCutOn.setBounds (keys.reduced (2, 0));
        placeKnobs (r, { &gain, &lowCut }, 1);
    }
    {
        auto r = inside (sections[1]);
        r.removeFromTop (kKey + 4);
        r.removeFromBottom (22); // the GR bar
        gateOn.setBounds (keysRow (r).reduced (2, 0));
        placeKnobs (r, { &gateThr }, 1);
    }
    {
        auto r = inside (sections[2]);
        r.removeFromTop (kKey + 4);
        r.removeFromBottom (22);
        dynOn.setBounds (keysRow (r).reduced (2, 0));
        placeKnobs (r, { &dynThr, &dynRatio }, 1);
    }
    {
        auto r = inside (sections[3]);
        r.removeFromTop (kKey + 4);
        eqOn.setBounds (keysRow (r).removeFromRight (80).reduced (2, 0));
        auto bands = r.removeFromTop (kKey);
        const int n = numBands();
        const int w = bands.getWidth() / juce::jmax (1, n);
        for (int b = 0; b < 6; ++b)
            if (b < n)
                bandKeys[(size_t) b]->setBounds (bands.removeFromLeft (w).reduced (2, 0));
        r.removeFromTop (4);
        placeKnobs (r, { &eqType, &eqFreq, &eqGain, &eqQ }, 2);
    }
    {
        auto r = inside (sections[4]);
        r.removeFromTop (kKey + 4);
        auto banks = r.removeFromBottom (kKey);
        const int w = banks.getWidth() / 4;
        for (auto& k : bankKeys)
            k->setBounds (banks.removeFromLeft (w).reduced (2, 0));
        placeKnobs (r, { &sends[0], &sends[1], &sends[2], &sends[3] }, 2);
    }
    {
        auto r = inside (sections[5]);
        r.removeFromTop (kKey + 4);
        auto keys = keysRow (r);
        lrOn.setBounds (keys.removeFromLeft (keys.getWidth() / 2).reduced (2, 0));
        mcOn.setBounds (keys.reduced (2, 0));
        placeKnobs (r, { &pan, &mcLevel }, 1);
    }
}

} // namespace livedaw::ui::x32
