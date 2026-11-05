#include "ui/X32ChannelSection.h"
#include "ui/HardwareLook.h"

#include "engine/Models.h"
#include "ui/MeterBank.h"

#include <tuple>

namespace livedaw::ui::x32
{

using namespace universal;

namespace
{
    // The band keys top to bottom, and the band each selects (four bands; six: a
    // second press of HIGH or LOW for HIGH 2 or LOW 2).
    const char* const kBandKeys[] = { "HIGH", "HIGH MID", "LOW MID", "LOW" };
    constexpr int kBand4[] = { 3, 2, 1, 0 }, kBand6[] = { 5, 3, 2, 0 };
    constexpr int kKey = 34; // a key and its name under it
    const juce::Colour kKeyLit (0xffffc83a);
    // The EQ types' LEDs, top to bottom (types 5 ... 0: X32 EQ_TYPES).
    const char* const kTypeNames[] = { "HCUT", "HSHV", "VEQ", "PEQ", "LSHV", "LCUT" };

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
    : ctx (c), theme (t), lowCutOn ("LOW CUT", kKeyLit, t), invert (juce::String::fromUTF8 ("\xc3\x98"), kKeyLit, t), phantom ("48V", kKeyLit, t),
      mode ("MODE", kKeyLit, t), gateOn ("GATE / DUCKER", kKeyLit, t), dynOn ("COMP / EXP", kKeyLit, t), eqOn ("EQUALIZER", kKeyLit, t),
      lrOn ("STEREO BUS", kKeyLit, t), mcOn ("MONO BUS", kKeyLit, t)
{
    // The X32's encoders: their names and scales are printed on the panel.
    for (auto* k : { &gain, &lowCut, &gateThr, &dynThr, &eqFreq, &eqGain, &eqQ, &pan, &mcLevel })
    {
        k->setHardware (true);
        addAndMakeVisible (*k);
    }
    for (auto& k : sends)
    {
        k.setHardware (true);
        addAndMakeVisible (k);
    }
    // RATIO and TYPE aren't on the panel (the screen's); TYPE steps by MODE.
    addChildComponent (dynRatio);
    addChildComponent (eqType);
    phantom.onClick = [this] { if (auto* s = source()) s->phantom = ! s->phantom.load(); };
    phantom.isOn = [this] { auto* s = source(); return s != nullptr && s->phantom.load(); };
    mode.onClick = [this]
    {
        const auto& b = eqType.getBinding();
        if (! b.isValid() || b.steps < 2.0f)
            return;
        const int n = (int) b.steps;
        b.set ((float) ((typeIndex() + 1) % n) / (float) (n - 1));
    };
    mode.isOn = [] { return false; };
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
    for (auto* k : { &lowCutOn, &invert, &phantom, &mode, &gateOn, &dynOn, &eqOn, &lrOn, &mcOn })
        addAndMakeVisible (*k);

    for (int k = 0; k < 4; ++k)
    {
        auto key = std::make_unique<ToggleKey> (kBandKeys[k], kKeyLit, t);
        key->isOn = [this, k]
        {
            const bool six = numBands() == 6;
            return band == (six ? kBand6[k] : kBand4[k]) || (six && ((k == 0 && band == 4) || (k == 3 && band == 1)));
        };
        key->onClick = [this, k]
        {
            if (numBands() == 6)
            {
                // HIGH and LOW: again for HIGH 2 and LOW 2.
                if (k == 0)
                    setBand (band == 5 ? 4 : 5);
                else if (k == 3)
                    setBand (band == 0 ? 1 : 0);
                else
                    setBand (kBand6[k]);
            }
            else
                setBand (kBand4[k]);
        };
        addAndMakeVisible (*key);
        bandKeys.push_back (std::move (key));
    }
    for (int b = 0; b < 4; ++b)
    {
        auto key = std::make_unique<ToggleKey> (juce::String (4 * b + 1) + "-" + juce::String (4 * b + 4), kKeyLit, t);
        key->isOn = [this, b] { return sendBank == b; };
        key->onClick = [this, b] { setSendBank (b); };
        addAndMakeVisible (*key);
        bankKeys.push_back (std::move (key));
    }
    sections = { { { "CONFIG / PREAMP", {} }, { "GATE", {} }, { "DYNAMICS", {} }, { "EQUALIZER", {} }, { "BUS SENDS", {} }, { "MAIN BUS", {} } } };
    for (size_t s = 0; s < sections.size(); ++s)
    {
        auto key = std::make_unique<ToggleKey> ("VIEW", kKeyLit, t);
        key->onClick = [this, s] { if (onView) onView ((int) s); };
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
    // GAIN: the head amp of the input it takes (interface inputs 1-128, the
    // X32's local and AES50 inputs: -12..+60 dB), else the digital trim.
    boundInput = p.inputLeft.load();
    if (auto* src = boundInput >= 0 && boundInput < 128 ? ctx.engine.getSource (boundInput) : nullptr)
    {
        auto b = bindLinear ("GAIN", src->gainDb, -12.0f, 60.0f, " dB", 1, 0.0f);
        b.steps = 145.0f;
        gain.setBinding (b);
    }
    else
        gain.setBinding (bindLinear ("TRIM", p.input.trimDb, -18.0f, 18.0f, " dB", 1, 0.0f));
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
    if (strip != nullptr && strip->params.inputLeft.load() != boundInput)
        bind(); // patched elsewhere: GAIN follows the input
    for (auto* k : { &lowCutOn, &invert, &phantom, &mode, &gateOn, &dynOn, &eqOn, &lrOn, &mcOn })
        k->refresh();
    for (auto* keys : { &bandKeys, &bankKeys })
        for (auto& k : *keys)
            k->refresh();
    // The meters and the type LEDs.
    repaint (levelMeter);
    repaint (grMeter);
    repaint (typeLeds);
}

ld::SourceParams* ChannelSection::source() const
{
    const int input = strip != nullptr ? strip->params.inputLeft.load() : -1;
    return input >= 0 && input < 128 ? ctx.engine.getSource (input) : nullptr;
}

int ChannelSection::typeIndex() const
{
    const auto& b = eqType.getBinding();
    if (! b.isValid() || b.steps < 2.0f)
        return -1;
    return juce::roundToInt (b.get() * (b.steps - 1.0f));
}

void ChannelSection::paint (juce::Graphics& g)
{
    const auto& look = hw::Look::x32();
    // Each section on its slate panel, outlined in silkscreen, its title in the outline.
    for (auto& s : sections)
        hw::section (g, s.area.toFloat().withTrimmedTop (6.0f), s.title, look);

    // The encoders' names, printed under them.
    const auto name = [&g, &look] (const juce::Component& k, const juce::String& text)
    {
        if (k.isVisible())
            hw::silk (g, k.getBounds().toFloat().withY ((float) k.getBottom() - 2.0f).withHeight (12.0f).expanded (30.0f, 0.0f), text, look, 9.5f);
    };
    name (gain, "GAIN");
    name (lowCut, "FREQUENCY");
    name (gateThr, "THRESHOLD");
    name (dynThr, "THRESHOLD");
    name (eqQ, "Q");
    name (eqFreq, "FREQ");
    name (eqGain, "GAIN");
    name (mcLevel, "LEVEL");
    name (pan, "PAN / BAL");
    for (int i = 0; i < 4; ++i)
    {
        const auto& k = sends[(size_t) i];
        hw::silk (g, juce::Rectangle<float> ((float) k.getRight(), (float) k.getY(), 16.0f, (float) k.getHeight()), juce::String (i + 1), look, 10.0f);
    }

    // An LED column with its marks at the right: `lit` how many from the bottom.
    const auto ledColumn = [&g, &look] (juce::Rectangle<float> r, const juce::StringArray& marks, int lit, bool fromTop,
                                        std::function<juce::Colour (int)> colourOf)
    {
        const float rowH = r.getHeight() / (float) marks.size();
        g.setFont (universal::type::font (8.5f, true));
        for (int k = 0; k < marks.size(); ++k)
        {
            const float y = r.getY() + rowH * (float) k;
            const bool on = fromTop ? k < lit : marks.size() - 1 - k < lit;
            const auto c = colourOf (k);
            g.setColour (on ? c : c.withAlpha (0.14f));
            g.fillRoundedRectangle (juce::Rectangle<float> (r.getX(), y + rowH * 0.25f, 7.0f, rowH * 0.5f), 1.0f);
            g.setColour (look.silk);
            g.drawText (marks[k], juce::Rectangle<float> (r.getX() + 10.0f, y, r.getWidth() - 10.0f, rowH), juce::Justification::centredLeft);
        }
    };
    const auto green = juce::Colour (0xff3ddc5a), red = juce::Colour (0xffff3b30), orange = juce::Colour (0xffff9a1f);

    // CONFIG / PREAMP's LEVEL: the input after the trim.
    if (! levelMeter.isEmpty())
    {
        const float db = strip != nullptr ? juce::Decibels::gainToDecibels (ctx.meters.get (strip, MeterPoint::Trim, 0), -144.0f) : -144.0f;
        static constexpr float steps[] = { -60.0f, -30.0f, -18.0f, -12.0f, -9.0f, -6.0f, -3.0f, 0.0f };
        int lit = 0;
        for (float s : steps)
            lit += db >= s ? 1 : 0;
        auto r = levelMeter.toFloat();
        hw::silk (g, r.removeFromBottom (12.0f), "LEVEL / dB", look, 8.5f);
        ledColumn (r, { "CLIP", "-3", "-6", "-9", "-12", "-18", "-30", "SIG" }, lit, false,
                   [=] (int k) { return k == 0 ? red : k < 3 ? orange : green; });
    }
    // The GR meter between GATE and DYNAMICS: the compressor's from the top, the gate's below.
    if (! grMeter.isEmpty())
    {
        const float dynGr = strip != nullptr ? std::abs (strip->meters.dynReductionDb.load()) : 0.0f;
        const float gateGr = strip != nullptr ? std::abs (strip->meters.gateReductionDb.load()) : 0.0f;
        static constexpr float steps[] = { 2.0f, 4.0f, 6.0f, 10.0f, 18.0f, 30.0f };
        int lit = 0;
        for (float s : steps)
            lit += dynGr >= s ? 1 : 0;
        auto r = grMeter.toFloat();
        hw::silk (g, r.removeFromBottom (12.0f), "GR / dB", look, 8.5f);
        ledColumn (r, { "COMP", "2", "4", "6", "10", "18", "30", "GATE" }, 0, true, [=] (int) { return orange; });
        // Lit: COMP while it compresses, the steps down, GATE while the gate closes.
        const float rowH = r.getHeight() / 8.0f;
        for (int k = 0; k < 8; ++k)
        {
            const bool on = k == 0 ? dynGr >= 0.5f : k == 7 ? gateGr >= 0.5f : k <= lit;
            if (! on)
                continue;
            g.setColour (k == 7 ? green : orange);
            g.fillRoundedRectangle (juce::Rectangle<float> (r.getX(), r.getY() + rowH * (float) k + rowH * 0.25f, 7.0f, rowH * 0.5f), 1.0f);
        }
    }
    // EQUALIZER: the band's type, its LED lit; HIGH 2 and LOW 2 beside their keys.
    if (! typeLeds.isEmpty())
    {
        const int type = typeIndex();
        ledColumn (typeLeds.toFloat(), { "HCUT", "HSHV", "VEQ", "PEQ", "LSHV", "LCUT" }, 0, true, [=] (int) { return red; });
        const float rowH = (float) typeLeds.getHeight() / 6.0f;
        if (type >= 0 && type < 6)
        {
            g.setColour (red);
            g.fillRoundedRectangle (juce::Rectangle<float> ((float) typeLeds.getX(), (float) typeLeds.getY() + rowH * (float) (5 - type) + rowH * 0.25f, 7.0f,
                                                            rowH * 0.5f),
                                    1.0f);
        }
        juce::ignoreUnused (kTypeNames);
        if (numBands() == 6)
        {
            const auto beside = [&] (const juce::Component& k, const char* text)
            {
                hw::silk (g, k.getBounds().toFloat().withX ((float) k.getX() - 52.0f).withWidth (48.0f).withHeight ((float) k.getHeight() - 10.0f), text, look,
                          9.0f, juce::Justification::centredRight);
            };
            beside (*bandKeys[1], "HIGH 2");
            beside (*bandKeys[2], "LOW 2");
        }
    }
}

void ChannelSection::resized()
{
    auto area = getLocalBounds();
    const int gap = sizes::kGap;
    // As on the panel: CONFIG / PREAMP over GATE and DYNAMICS (the GR meter
    // between), then EQUALIZER, BUS SENDS and MAIN BUS, each the full height.
    const float weights[] = { 3.2f, 3.4f, 1.75f, 1.3f };
    const int width = area.getWidth() - gap * 3;
    float total = 0.0f;
    for (auto w : weights)
        total += w;
    auto left = area.removeFromLeft (juce::roundToInt ((float) width * weights[0] / total));
    area.removeFromLeft (gap);
    sections[3].area = area.removeFromLeft (juce::roundToInt ((float) width * weights[1] / total));
    area.removeFromLeft (gap);
    sections[4].area = area.removeFromLeft (juce::roundToInt ((float) width * weights[2] / total));
    area.removeFromLeft (gap);
    sections[5].area = area;
    sections[0].area = left.removeFromTop (left.getHeight() / 2 - gap / 2);
    left.removeFromTop (gap);
    sections[1].area = left.removeFromLeft (left.getWidth() / 2 - 2);
    left.removeFromLeft (4);
    sections[2].area = left;

    const auto inside = [] (const Section& s) { return s.area.reduced (10, 8).withTrimmedTop (16); };
    // VIEW: a small key at the section's bottom right.
    for (size_t s = 0; s < sections.size(); ++s)
    {
        auto r = inside (sections[s]);
        viewKeys[s]->setBounds (r.removeFromRight (34).removeFromBottom (30));
    }
    const auto knobAt = [] (DisplayKnob& k, juce::Point<int> centre, int size) { k.setBounds (juce::Rectangle<int> (size, size).withCentre (centre)); };
    const auto keyAt = [] (juce::Component& k, juce::Point<int> centre, int w) { k.setBounds (juce::Rectangle<int> (w, kKey).withCentre (centre)); };

    {
        // CONFIG / PREAMP: GAIN, the LEVEL meter, FREQUENCY; 48V, polarity, LOW CUT under them.
        auto r = inside (sections[0]).withTrimmedRight (38);
        auto keys = r.removeFromBottom (kKey);
        const int size = juce::jmin (64, r.getHeight() - 14);
        const int w = r.getWidth() / 3;
        knobAt (gain, { r.getX() + w / 2, r.getY() + size / 2 + 2 }, size);
        levelMeter = juce::Rectangle<int> (r.getX() + w + 4, r.getY(), w - 8, r.getHeight());
        knobAt (lowCut, { r.getX() + 2 * w + w / 2, r.getY() + size / 2 + 2 }, size);
        keyAt (phantom, { r.getX() + w / 4, keys.getCentreY() }, 40);
        keyAt (invert, { r.getX() + w - w / 6, keys.getCentreY() }, 40);
        keyAt (lowCutOn, { r.getX() + 2 * w + w / 2, keys.getCentreY() }, 56);
    }
    {
        // GATE and DYNAMICS: THRESHOLD, the key under it; the GR meter between them.
        auto g = inside (sections[1]).withTrimmedRight (20);
        auto d = inside (sections[2]).withTrimmedRight (36);
        auto meterColumn = d.removeFromLeft (40);
        grMeter = meterColumn.withTrimmedBottom (4);
        for (auto [r, k, key] : { std::tuple { g, &gateThr, &gateOn }, std::tuple { d, &dynThr, &dynOn } })
        {
            auto keys = r.removeFromBottom (kKey);
            const int size = juce::jmin (60, r.getHeight() - 14, r.getWidth());
            knobAt (*k, { r.getCentreX(), r.getY() + size / 2 + 2 }, size);
            keyAt (*key, { r.getCentreX(), keys.getCentreY() }, juce::jmin (70, r.getWidth()));
        }
    }
    {
        // EQUALIZER: the type LEDs and MODE at the left, Q over FREQ over GAIN, the band keys at the right, EQUALIZER under.
        auto r = inside (sections[3]).withTrimmedRight (36);
        auto keys = r.removeFromBottom (kKey);
        keyAt (eqOn, { r.getX() + r.getWidth() / 2, keys.getCentreY() }, 70);
        auto bands = r.removeFromRight (70);
        const int bh = bands.getHeight() / 4;
        for (int k = 0; k < 4; ++k)
            keyAt (*bandKeys[(size_t) k], { bands.getCentreX(), bands.getY() + bh * k + bh / 2 }, 44);
        auto types = r.removeFromLeft (64);
        typeLeds = types.removeFromTop (types.getHeight() * 3 / 5).reduced (0, 4);
        keyAt (mode, { types.getCentreX(), types.getY() + kKey / 2 + 4 }, 40);
        const int size = juce::jmin (62, r.getHeight() / 3 - 6);
        knobAt (eqQ, { r.getCentreX() - 20, r.getY() + size / 2 + 2 }, size);
        knobAt (eqFreq, { r.getCentreX() + 30, r.getCentreY() + 4 }, size);
        knobAt (eqGain, { r.getCentreX() - 20, r.getBottom() - size / 2 - 12 }, size);
    }
    {
        // BUS SENDS: four encoders down the section, their bank keys beside them.
        auto r = inside (sections[4]).withTrimmedBottom (32);
        auto keys = r.removeFromRight (48);
        const int h = r.getHeight() / 4;
        const int size = juce::jmin (h - 8, r.getWidth() - 20, 60);
        for (int i = 0; i < 4; ++i)
        {
            knobAt (sends[(size_t) i], { r.getX() + size / 2 + 4, r.getY() + h * i + h / 2 }, size);
            keyAt (*bankKeys[(size_t) i], { keys.getCentreX(), r.getY() + h * i + h / 2 }, 42);
        }
    }
    {
        // MAIN BUS: LEVEL, MONO BUS, PAN / BAL, STEREO BUS, top to bottom.
        auto r = inside (sections[5]).withTrimmedBottom (28);
        const int h = r.getHeight() / 2;
        auto top = r.removeFromTop (h);
        for (auto [part, k, key] : { std::tuple { top, &mcLevel, &mcOn }, std::tuple { r, &pan, &lrOn } })
        {
            auto keys = part.removeFromBottom (kKey);
            const int size = juce::jmin (60, part.getHeight() - 14, part.getWidth());
            knobAt (*k, { part.getCentreX(), part.getY() + size / 2 + 2 }, size);
            keyAt (*key, { part.getCentreX(), keys.getCentreY() }, 70);
        }
    }
    eqType.setBounds ({});
    dynRatio.setBounds ({});
}

} // namespace livedaw::ui::x32
