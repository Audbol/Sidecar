#pragma once

#include "ui/Skin.h"
#include "ui/UiContext.h"
#include "ui/universal/FxRackView.h"
#include "ui/universal/Inspector.h"
#include "ui/universal/MixerView.h"
#include "ui/universal/MonitorView.h"
#include "ui/universal/RecorderView.h"
#include "ui/universal/RoutingView.h"
#include "ui/universal/ScenesView.h"
#include "ui/universal/SetupView.h"

namespace livedaw::ui
{

/** The Universal Mixer's interface (docs/UNIVERSAL_MIXER.md, Phase 2), a
    preview while it is being built: a top bar (the console it runs, the way
    back to the WING surface) and the view keys over the view shown (MIXER,
    ROUTING, FX RACK, MONITORING, SCENES, RECORDER, SETUP), with the inspector
    of the selected strip beside it (src/ui/universal). */
class UniversalSkin final : public Skin
{
public:
    explicit UniversalSkin (UiContext&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshAll() override;
    void selectionChanged() override;

    enum class View
    {
        Mixer,
        Routing,
        Fx,
        Monitoring,
        Scenes,
        Recorder,
        Setup
    };
    void showView (View);
    View getView() const noexcept { return view; }

    universal::MixerView& getMixer() noexcept { return mixer; }
    universal::RoutingView& getRouting() noexcept { return routingView; }
    universal::FxRackView& getFxRack() noexcept { return fxRack; }
    universal::MonitorView& getMonitoring() noexcept { return monitoring; }
    universal::ScenesView& getScenes() noexcept { return scenes; }
    universal::RecorderView& getRecorder() noexcept { return recorder; }
    universal::SetupView& getSetup() noexcept { return setup; }
    void showChanged() override { scenes.refresh(); }
    universal::Inspector& getInspector() noexcept { return inspector; }
    bool isInspectorOpen() const noexcept { return inspectorOpen; }

private:
    UiContext& ctx;
    const universal::Theme& theme;
    universal::ToggleKey wingKey;
    std::vector<std::unique_ptr<universal::ToggleKey>> viewKeys;
    universal::MixerView mixer;
    universal::RoutingView routingView;
    universal::FxRackView fxRack;
    universal::MonitorView monitoring;
    universal::ScenesView scenes;
    universal::RecorderView recorder;
    universal::SetupView setup;
    View view = View::Mixer;
    universal::Inspector inspector;
    bool inspectorOpen = false;
    juce::String summary;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UniversalSkin)
};

} // namespace livedaw::ui
