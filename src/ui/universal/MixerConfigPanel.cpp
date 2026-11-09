#include "ui/universal/MixerConfigPanel.h"

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kRow = 44;
    /** The LR main's share of the 64 busses. */
    constexpr int kMainBusses = 2;
    constexpr int kStereoPairs = 64, kPairColumns = 8;

    const char* nameOf (MixerConfigPanel::Kind k)
    {
        static const char* names[] = { "GROUPS", "STEREO GROUPS", "AUX", "STEREO AUX", "FX SENDS", "STEREO FX SENDS", "MATRICES", "STEREO MATRICES" };
        return names[(size_t) k];
    }

    /** Stereo kinds take two busses; mono ones change in pairs. */
    bool isStereo (MixerConfigPanel::Kind k) noexcept { return ((int) k & 1) != 0; }
} // namespace

/** A kind of mix: its name, what the console has, what APPLY makes it, and keys to change it. */
class MixerConfigPanel::Row final : public juce::Component
{
public:
    Row (MixerConfigPanel& o, Kind k) : owner (o), kind (k), minus ("-", o.theme.accent, o.theme), plus ("+", o.theme.accent, o.theme)
    {
        const int step = isStereo (kind) ? 1 : 2;
        minus.onClick = [this, step] { owner.setPending (kind, owner.getPending (kind) - step); };
        plus.onClick = [this, step] { owner.setPending (kind, owner.getPending (kind) + step); };
        addAndMakeVisible (minus);
        addAndMakeVisible (plus);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.theme;
        g.setColour (t.raised.withAlpha (0.5f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), sizes::kRadius);
        auto area = getLocalBounds().reduced (sizes::kGapLarge, 0);
        g.setColour (t.text);
        g.setFont (type::font (type::kLabel, true));
        g.drawText (nameOf (kind), area.removeFromLeft (220), juce::Justification::centredLeft);
        const int now = owner.getCurrent (kind), next = owner.getPending (kind);
        g.setFont (type::font (type::kTitle, true));
        g.setColour (next == now ? t.text : t.accent);
        g.drawText (next == now ? juce::String (now) : juce::String (now) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + juce::String (next),
                    area.removeFromLeft (200), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto keys = getLocalBounds().reduced (sizes::kGapSmall).removeFromRight (2 * 64 + sizes::kGapSmall);
        minus.setBounds (keys.removeFromLeft (64));
        keys.removeFromLeft (sizes::kGapSmall);
        plus.setBounds (keys.removeFromLeft (64));
    }

private:
    MixerConfigPanel& owner;
    Kind kind;
    ToggleKey minus, plus;
};

MixerConfigPanel::MixerConfigPanel (UiContext& c, const Theme& t)
    : applyKey ("APPLY", t.accent, t), revertKey ("REVERT", t.accent, t), mixesTab ("MIXER CONFIG", t.accent, t), inputsTab ("IP STEREO CONFIG", t.accent, t), ammTab ("AMM", t.accent, t),
      nomKey ("NOM", t.accent, t), dClassicKey ("D-CLASSIC", t.accent, t), ctx (c), theme (t)
{
    mixesTab.isOn = [this] { return page == Page::Mixes; };
    mixesTab.onClick = [this] { showPage (Page::Mixes); };
    inputsTab.isOn = [this] { return page == Page::Inputs; };
    inputsTab.onClick = [this] { showPage (Page::Inputs); };
    ammTab.isOn = [this] { return page == Page::Amm; };
    ammTab.onClick = [this] { showPage (Page::Amm); };
    addAndMakeVisible (mixesTab);
    addAndMakeVisible (inputsTab);
    addAndMakeVisible (ammTab);
    {
        auto* amm = ld::ld_engine_amm (ctx.engine.getHandle());
        nomKey.isOn = [amm] { return amm->mode.load() == 0; };
        nomKey.onClick = [this, amm]
        {
            amm->mode = 0;
            showAmmControls();
        };
        dClassicKey.isOn = [amm] { return amm->mode.load() == 1; };
        dClassicKey.onClick = [this, amm]
        {
            amm->mode = 1;
            showAmmControls();
        };
        addChildComponent (nomKey);
        addChildComponent (dClassicKey);
        const int counts[] = { 1, 2, 4 };
        for (size_t i = 0; i < ammCountKeys.size(); ++i)
        {
            const int count = counts[i];
            auto key = std::make_unique<ToggleKey> (juce::String (count), t.accent, t);
            key->isOn = [amm, count] { return amm->count.load() == count; };
            key->onClick = [this, amm, count]
            {
                amm->count = count;
                showAmmControls();
            };
            addChildComponent (*key);
            ammCountKeys[i] = std::move (key);
        }
        for (size_t i = 0; i < bestMicKeys.size(); ++i)
        {
            auto key = std::make_unique<ToggleKey> ("AMM " + juce::String ((int) i + 1), t.accent, t);
            key->isOn = [amm, i] { return amm->bestMic[i].load(); };
            key->onClick = [this, amm, i]
            {
                amm->bestMic[i] = ! amm->bestMic[i].load();
                refresh();
            };
            addChildComponent (*key);
            bestMicKeys[i] = std::move (key);
        }
        addAmmBar (ValueBar::linear ("OFF ATTEN", amm->offAttenDb, -90.0f, -10.0f, -40.0f, " dB", 0), true);
        addAmmBar (ValueBar::linear ("ON HOLD", amm->onHoldS, 1.0f, 5.0f, 2.0f, " s", 1), true);
        addAmmBar (ValueBar::linear ("NOM ATTEN", amm->nomAttenDb, 3.0f, 6.0f, 3.0f, " dB", 1), true);
        addAmmBar (ValueBar::linear ("CHAIR SENS", amm->chairSensitivity, 1.0f, 10.0f, 5.0f, "", 0), true);
        addAmmBar (ValueBar::linear ("CHAIR DUCK", amm->chairDuckDb, -40.0f, -3.0f, -20.0f, " dB", 0), true);
        addAmmBar (ValueBar::frequency ("KEY HPF", amm->keyHpfHz, 20.0f, 2000.0f, 100.0f), false);
        addAmmBar (ValueBar::frequency ("KEY LPF", amm->keyLpfHz, 1000.0f, 20000.0f, 10000.0f), false);
    }
    for (int pair = 0; pair < kStereoPairs; ++pair)
    {
        auto key = std::make_unique<ToggleKey> (juce::String (2 * pair + 1) + "/" + juce::String (2 * pair + 2), t.accent, t);
        key->isOn = [this, pair] { return isStereoPending (pair); };
        key->onClick = [this, pair] { setStereoPending (pair, ! isStereoPending (pair)); };
        addChildComponent (*key);
        pairKeys.push_back (std::move (key));
    }
    for (int k = 0; k < NumKinds; ++k)
    {
        rows.push_back (std::make_unique<Row> (*this, static_cast<Kind> (k)));
        addAndMakeVisible (*rows.back());
    }
    applyKey.onClick = [this]
    {
        juce::Component::SafePointer<MixerConfigPanel> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withTitle ("MIXER CONFIG")
                                          .withMessage ("This lays the mixes out anew and resets them, with every send to them. An input "
                                                        "pair that changes between mono and stereo starts on its defaults. The other channels, "
                                                        "FX, DCAs and the LR stay. Scenes saved before this keep their old mix numbers.")
                                          .withButton ("APPLY")
                                          .withButton ("CANCEL")
                                          .withAssociatedComponent (this),
                                      [safe] (int result)
                                      {
                                          if (safe != nullptr && result == 1)
                                              safe->apply();
                                      });
    };
    revertKey.onClick = [this] { revert(); };
    addAndMakeVisible (applyKey);
    addAndMakeVisible (revertKey);
    revert();
}

MixerConfigPanel::~MixerConfigPanel() = default;

void MixerConfigPanel::addAmmBar (ValueBar::Binding binding, bool nomOnly)
{
    auto bar = std::make_unique<ValueBar> (theme);
    bar->bind (std::move (binding));
    addChildComponent (*bar);
    ammBars.push_back (std::move (bar));
    ammBarIsNom.push_back (nomOnly);
}

void MixerConfigPanel::showAmmControls()
{
    const bool on = page == Page::Amm;
    const auto* amm = ld::ld_engine_amm (ctx.engine.getHandle());
    const bool nom = amm->mode.load() == 0;
    nomKey.setVisible (on);
    dClassicKey.setVisible (on);
    for (auto& k : ammCountKeys)
        k->setVisible (on);
    for (size_t i = 0; i < ammBars.size(); ++i)
        ammBars[i]->setVisible (on && (nom || ! ammBarIsNom[i]));
    for (size_t i = 0; i < bestMicKeys.size(); ++i)
        bestMicKeys[i]->setVisible (on && nom && (int) i < amm->count.load());
    refresh();
}

int MixerConfigPanel::getCurrent (Kind k) const
{
    int32_t layout[NumKinds] {};
    return ld::ld_engine_dlive_layout (ctx.engine.getHandle(), layout) ? layout[(size_t) k] : 0;
}

void MixerConfigPanel::setPending (Kind k, int count)
{
    pending[(size_t) k] = juce::jlimit (0, isStereo (k) ? kBusses / 2 : kBusses, count);
    status.clear();
    refresh();
}

int MixerConfigPanel::getBussesUsed() const noexcept
{
    int used = kMainBusses;
    for (int k = 0; k < NumKinds; ++k)
        used += pending[(size_t) k] * (isStereo (static_cast<Kind> (k)) ? 2 : 1);
    return used;
}

void MixerConfigPanel::setStereoPending (int pair, bool stereo)
{
    const auto bit = (uint64_t) 1 << pair;
    pendingStereo = stereo ? pendingStereo | bit : pendingStereo & ~bit;
    status.clear();
    refresh();
}

void MixerConfigPanel::showPage (Page p)
{
    page = p;
    for (auto& r : rows)
        r->setVisible (p == Page::Mixes);
    for (auto& k : pairKeys)
        k->setVisible (p == Page::Inputs);
    applyKey.setVisible (p != Page::Amm);
    revertKey.setVisible (p != Page::Amm);
    showAmmControls();
}

bool MixerConfigPanel::hasChanges() const
{
    if (pendingStereo != ld::ld_engine_dlive_stereo_inputs (ctx.engine.getHandle()))
        return true;
    for (int k = 0; k < NumKinds; ++k)
        if (pending[(size_t) k] != getCurrent (static_cast<Kind> (k)))
            return true;
    return false;
}

void MixerConfigPanel::apply()
{
    if (! fits())
        return;
    int32_t layout[NumKinds];
    for (int k = 0; k < NumKinds; ++k)
        layout[(size_t) k] = pending[(size_t) k];
    const auto result = ctx.show.applyDLiveLayout (layout, pendingStereo);
    status = result.wasOk() ? juce::String ("Applied.") : result.getErrorMessage();
    ctx.log.info ("Mixer Config: " + status);
    if (ctx.structureChanged)
        ctx.structureChanged();
    revert();
}

void MixerConfigPanel::revert()
{
    for (int k = 0; k < NumKinds; ++k)
        pending[(size_t) k] = getCurrent (static_cast<Kind> (k));
    pendingStereo = ld::ld_engine_dlive_stereo_inputs (ctx.engine.getHandle());
    refresh();
}

void MixerConfigPanel::refresh()
{
    const bool changes = hasChanges();
    applyKey.setEnabled (changes && fits());
    revertKey.setEnabled (changes);
    mixesTab.refresh();
    inputsTab.refresh();
    ammTab.refresh();
    nomKey.refresh();
    dClassicKey.refresh();
    for (auto& k : ammCountKeys)
        k->refresh();
    for (auto& k : bestMicKeys)
        k->refresh();
    for (auto& b : ammBars)
        b->refresh();
    for (auto& k : pairKeys)
        k->refresh();
    for (auto& r : rows)
        r->repaint();
    repaint();
}

void MixerConfigPanel::paint (juce::Graphics& g)
{
    if (page == Page::Amm)
    {
        g.setFont (type::font (type::kLabel, true));
        g.setColour (theme.text);
        const auto label = [&g] (const juce::Component& first, const char* text)
        {
            g.drawText (text, first.getBounds().withWidth (first.getX() - 0).withX (0).withTrimmedRight (sizes::kGapSmall), juce::Justification::centredRight);
        };
        label (nomKey, "MODE");
        label (*ammCountKeys[0], "AMMS");
        if (ld::ld_engine_amm (ctx.engine.getHandle())->mode.load() == 0)
            label (*bestMicKeys[0], "BEST MIC");
        return;
    }
    const auto line = getLocalBounds().withTrimmedTop (applyKey.getY()).withHeight (applyKey.getHeight()).withTrimmedLeft (revertKey.getRight() + sizes::kGapLarge);
    const auto used = getBussesUsed(), sends = getFxSendsUsed();
    g.setFont (type::font (type::kLabel, true));
    g.setColour (fits() ? theme.text : theme.mute);
    juce::String text;
    text << "BUSSES " << used << " OF " << kBusses << "   FX SENDS " << sends << " OF " << kMaxFxSends;
    if (status.isNotEmpty())
        text << "   " << status;
    g.drawText (text, line, juce::Justification::centredLeft);
}

void MixerConfigPanel::resized()
{
    auto area = getLocalBounds();
    auto tabs = area.removeFromTop (sizes::kTouch);
    mixesTab.setBounds (tabs.removeFromLeft (200));
    tabs.removeFromLeft (sizes::kGapSmall);
    inputsTab.setBounds (tabs.removeFromLeft (200));
    tabs.removeFromLeft (sizes::kGapSmall);
    ammTab.setBounds (tabs.removeFromLeft (120));
    area.removeFromTop (sizes::kGapSmall);
    const auto rowsArea = area;
    for (auto& r : rows)
    {
        r->setBounds (area.removeFromTop (kRow));
        area.removeFromTop (sizes::kGapSmall);
    }
    {
        // The pairs, in the same space as the rows.
        const int w = rowsArea.getWidth() / kPairColumns, h = (area.getY() - rowsArea.getY()) / (kStereoPairs / kPairColumns);
        for (int i = 0; i < (int) pairKeys.size(); ++i)
            pairKeys[(size_t) i]->setBounds (juce::Rectangle<int> (rowsArea.getX() + (i % kPairColumns) * w, rowsArea.getY() + (i / kPairColumns) * h, w, h).reduced (2));
    }
    {
        // AMM: the mode and how many; the settings in two columns; each one's Best Mic.
        auto page2 = rowsArea;
        const int labelW = 90, keyW = 110;
        auto top = page2.removeFromTop (kRow);
        top.removeFromLeft (labelW);
        nomKey.setBounds (top.removeFromLeft (keyW));
        top.removeFromLeft (sizes::kGapSmall);
        dClassicKey.setBounds (top.removeFromLeft (keyW));
        top.removeFromLeft (labelW);
        for (auto& k : ammCountKeys)
        {
            k->setBounds (top.removeFromLeft (64));
            top.removeFromLeft (sizes::kGapSmall);
        }
        page2.removeFromTop (sizes::kGap);
        auto bottom = page2.removeFromBottom (kRow);
        bottom.removeFromLeft (labelW);
        for (auto& k : bestMicKeys)
        {
            k->setBounds (bottom.removeFromLeft (keyW));
            bottom.removeFromLeft (sizes::kGapSmall);
        }
        page2.removeFromBottom (sizes::kGap);
        const int columns = 2, rowsDown = 4, barH = page2.getHeight() / rowsDown, barW = page2.getWidth() / columns;
        for (int i = 0; i < (int) ammBars.size(); ++i)
            ammBars[(size_t) i]->setBounds (juce::Rectangle<int> (page2.getX() + (i / rowsDown) * barW, page2.getY() + (i % rowsDown) * barH, barW, barH).reduced (sizes::kGapSmall));
    }
    area.removeFromTop (sizes::kGap);
    auto keys = area.removeFromTop (sizes::kTouch);
    applyKey.setBounds (keys.removeFromLeft (170));
    keys.removeFromLeft (sizes::kGapSmall);
    revertKey.setBounds (keys.removeFromLeft (170));
}

} // namespace livedaw::ui::universal
