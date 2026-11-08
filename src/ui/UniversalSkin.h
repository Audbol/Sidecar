#pragma once

#include "ui/Skin.h"
#include "ui/UiContext.h"
#include "ui/universal/FxRackView.h"
#include "ui/universal/Inspector.h"
#include "ui/universal/LibraryView.h"
#include "ui/universal/MixerView.h"
#include "ui/universal/MonitorView.h"
#include "ui/universal/RecorderView.h"
#include "ui/universal/RoutingView.h"
#include "ui/universal/ScenesView.h"
#include "ui/universal/SetupView.h"

namespace livedaw::ui
{

/** The Universal Mixer's interface (docs/UNIVERSAL_MIXER.md, Phase 2), a
    preview while it is being built: a top bar (the console it runs, the show
    and its SHOW menu: NEW, OPEN, SAVE, SAVE AS; the way back to the WING
    surface) and the view keys over the view shown (MIXER,
    ROUTING, FX RACK, MONITORING, SCENES, LIBRARY, RECORDER, SETUP), with the inspector
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
        Library,
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
    universal::LibraryView& getLibrary() noexcept { return library; }
    universal::RecorderView& getRecorder() noexcept { return recorder; }
    universal::SetupView& getSetup() noexcept { return setup; }
    void showChanged() override
    {
        scenes.refresh();
        repaint(); // the show's name
    }
    universal::Inspector& getInspector() noexcept { return inspector; }
    bool isInspectorOpen() const noexcept { return inspectorOpen; }
    /** The show's name in the top bar (its file's, or UNTITLED SHOW). */
    juce::String getShowTitle() const;
    /** The SHOW menu's items, as its key offers them: 1 NEW, 2 OPEN, 3 SAVE, 4 SAVE AS. */
    void runShowCommand (int item);
    universal::ToggleKey& getShowKey() noexcept { return showKey; }

private:
    UiContext& ctx;
    const universal::Theme& theme;
    universal::ToggleKey wingKey, showKey;
    std::vector<std::unique_ptr<universal::ToggleKey>> viewKeys;
    universal::MixerView mixer;
    universal::RoutingView routingView;
    universal::FxRackView fxRack;
    universal::MonitorView monitoring;
    universal::ScenesView scenes;
    universal::LibraryView library;
    universal::RecorderView recorder;
    universal::SetupView setup;
    View view = View::Mixer;
    universal::Inspector inspector;
    bool inspectorOpen = false;
    juce::String summary;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UniversalSkin)
};

} // namespace livedaw::ui
