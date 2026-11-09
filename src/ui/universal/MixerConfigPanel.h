#pragma once

#include "ui/UiContext.h"
#include "ui/universal/ValueControls.h"
#include "ui/universal/Widgets.h"

#include <array>
#include <memory>
#include <vector>

namespace livedaw::ui::universal
{

/** A dLive's Mixer Config (Firmware Reference Guide, MixRack Setup), in SETUP's place for
    the generic counts: how its 64 mix busses are laid out. A stereo mix takes two busses,
    mono ones come and go in pairs. APPLY, as on the console, resets the mixes and keeps
    everything else. */
class MixerConfigPanel final : public juce::Component
{
public:
    MixerConfigPanel (UiContext&, const Theme&);
    ~MixerConfigPanel() override;

    enum Kind
    {
        Groups,
        StereoGroups,
        Aux,
        StereoAux,
        FxSends,
        StereoFxSends,
        Matrices,
        StereoMatrices,
        NumKinds
    };

    static constexpr int kBusses = 64, kMaxFxSends = 16;

    /** What the console has now, and what APPLY would make it. */
    int getCurrent (Kind) const;
    int getPending (Kind k) const noexcept { return pending[(size_t) k]; }
    void setPending (Kind, int count);
    /** How many of the 64 busses the pending layout takes (the LR's two included). */
    int getBussesUsed() const noexcept;
    int getFxSendsUsed() const noexcept { return pending[FxSends] + pending[StereoFxSends]; }
    bool fits() const noexcept { return getBussesUsed() <= kBusses && getFxSendsUsed() <= kMaxFxSends; }
    bool hasChanges() const;

    /** IP STEREO CONFIG: whether inputs 2n+1 and 2n+2 are to be one stereo channel. */
    bool isStereoPending (int pair) const noexcept { return (pendingStereo >> pair) & 1; }
    void setStereoPending (int pair, bool stereo);
    enum class Page
    {
        Mixes,
        Inputs,
        Amm
    };
    void showPage (Page);
    Page getPage() const noexcept { return page; }

    /** Lays the mixes out as pending (without asking). */
    void apply();
    void revert();
    void refresh();
    const juce::String& getStatus() const noexcept { return status; }

    ToggleKey applyKey, revertKey, mixesTab, inputsTab, ammTab;
    std::vector<std::unique_ptr<ToggleKey>> pairKeys;
    /** AMM's settings (DEEP Processing, MixRack Setup): they take effect at once. */
    ToggleKey nomKey, dClassicKey;
    std::array<std::unique_ptr<ToggleKey>, 3> ammCountKeys; // 1, 2 or 4 AMMs
    std::array<std::unique_ptr<ToggleKey>, 4> bestMicKeys;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row;

    UiContext& ctx;
    const Theme& theme;
    std::array<int, NumKinds> pending {};
    uint64_t pendingStereo = 0;
    Page page = Page::Mixes;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<std::unique_ptr<ValueBar>> ammBars;
    std::vector<bool> ammBarIsNom; // shown in NOM mode only
    void addAmmBar (ValueBar::Binding, bool nomOnly);
    void showAmmControls();
    juce::String status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerConfigPanel)
};

} // namespace livedaw::ui::universal
