#pragma once

#include "ui/Skin.h"
#include "ui/UiContext.h"
#include "ui/universal/Inspector.h"
#include "ui/universal/MixerView.h"

namespace livedaw::ui
{

/** The Universal Mixer's interface (docs/UNIVERSAL_MIXER.md, Phase 2), a
    preview while it is being built: a top bar (the console it runs, the way
    back to the WING surface) over the MIXER view, and the inspector of the
    selected strip beside it (src/ui/universal). The routing, FX, monitoring,
    scenes, recorder and setup views follow step by step. */
class UniversalSkin final : public Skin
{
public:
    explicit UniversalSkin (UiContext&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshAll() override;
    void selectionChanged() override;

    universal::MixerView& getMixer() noexcept { return mixer; }
    universal::Inspector& getInspector() noexcept { return inspector; }
    bool isInspectorOpen() const noexcept { return inspectorOpen; }

private:
    UiContext& ctx;
    const universal::Theme& theme;
    universal::ToggleKey wingKey;
    universal::MixerView mixer;
    universal::Inspector inspector;
    bool inspectorOpen = false;
    juce::String summary;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UniversalSkin)
};

} // namespace livedaw::ui
