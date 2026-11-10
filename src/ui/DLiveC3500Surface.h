#pragma once

#include "ui/DLiveScreens.h"
#include "ui/DLiveSurface.h"

#include <array>
#include <memory>
#include <vector>

namespace livedaw::ui
{

/** The Allen & Heath dLive's C3500 surface (the owner's photographs of a real one, kept
    outside the repository, and the C-Series Getting Started Guide): 24 motor faders, two
    banks of 12 in groups of four with six layers each, two touchscreens, the left one the
    selected channel's processing and the right one the console's pages, two blocks of
    three soft rotaries between them, the PEQ's rotaries under the left screen and the
    scene keys under the right. No maker's artwork or logo is used. */
class DLiveC3500Surface final : public Skin,
                                public dl::Desk,
                                private juce::Timer
{
public:
    explicit DLiveC3500Surface (UiContext&);
    ~DLiveC3500Surface() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshAll() override;
    void selectionChanged() override;
    void showChanged() override;

    static constexpr int kStrips = 24, kBankStrips = 12, kBanks = 2, kLayers = 6;
    static constexpr int kDesignWidth = 1920, kDesignHeight = 1080;

    /** What the right screen shows (the keys beside it; none lit: the status page). */
    enum class Page
    {
        Status,
        Meters,
        Fx,
        Io,
        Scenes,
        Ganging,
        MixRack,
        Surface,
        Util,
        Routing,
        Library,
        Setup,
        Listen
    };
    void showPage (Page);
    Page getPage() const noexcept { return page; }

    /** Layer A-F (0-5) of bank 0 or 1. */
    void setLayer (int bank, int layer);
    int getLayer (int bank) const noexcept { return layers[(size_t) bank]; }

    void setRotaryFunction (dl::RotaryFunction);
    dl::RotaryFunction getRotaryFunction() const noexcept override { return rotaryFunction; }
    void setDisplayFunction (dl::DisplayFunction f) { displayFunction = f; }
    dl::DisplayFunction getDisplayFunction() const noexcept override { return displayFunction; }
    void setMixOnFaders (int bus) override;
    int getMixOnFaders() const noexcept override { return mixBus; }

    dl::SurfaceSetup& getSetup() noexcept override { return assignments; }
    void sceneGo() override { scenes.goNext(); }
    void sceneStep (int delta) override { scenes.stepNext (delta); }

    /** Home: the default layers, screens, active Mix, rotary function and LCD view. */
    void pressHome();

    dl::StripUnit& getStrip (int i) { return *strips[(size_t) juce::jlimit (0, kStrips - 1, i)]; }
    dl::Key* findKey (const juce::String& name) const;
    dl::Rotary& getPeqRotary (int band, bool freq) { return *peq[(size_t) (band * 2 + (freq ? 1 : 0))]; }
    dl::Rotary& getSoftRotary (int i) { return *soft[(size_t) i]; }
    dl::Rotary& getDataRotary (bool right) { return right ? *dataRight : *dataLeft; }
    dl::ProcessingScreen& getProcessing() noexcept { return processing; }
    dl::SceneScreen& getScenes() noexcept { return scenes; }
    dl::MixRackScreen& getMixRack() noexcept { return mixRack; }
    dl::MetersScreen& getMeters() noexcept { return metersScreen; }
    dl::IoScreen& getIo() noexcept { return ioScreen; }
    dl::SurfaceScreen& getSurfaceScreen() noexcept { return surfaceScreen; }
    dl::StatusScreen& getStatus() noexcept { return status; }
    dl::BottomBand& getBottomBand() noexcept { return bottomBand; }
    dl::HelpPanel& getHelp (bool right) noexcept { return right ? helpRight : helpLeft; }
    /** What opens the assignment menus (tests replace them). */
    std::function<void (int softKey, juce::Component*)> assignSoftKey;
    std::function<void (int customRotary, juce::Component*)> assignRotary;

    /** A layer's strips as a new console has them (Sidecar's reading: strips are freely
        assignable on a dLive). */
    static std::vector<dl::Slot> defaultLayer (int bank, int layer);

private:
    void timerCallback() override;
    void bindStrips();
    void bindPeq();
    void bindSoft();
    void softKeyPressed (int index);
    juce::String helpKey (bool right) const;
    dl::Key& key (const juce::String& name, juce::Colour idle, juce::Colour lit, juce::Rectangle<int>, dl::Key::Shape shape = dl::Key::Shape::Block, bool led = true);
    void silk (float x, float y, float w, float h, const juce::String& text, float size, juce::Justification j = juce::Justification::centred, bool dim = false);

    struct Silk
    {
        juce::Rectangle<float> area;
        juce::String text;
        float size;
        juce::Justification justification;
        bool dim;
    };

    juce::AffineTransform design;
    UiContext& ctx;
    const universal::Theme& theme;
    universal::ToggleKey universalKey, c1500Key;
    dl::SurfaceSetup assignments;
    juce::Component leftHost, rightHost;
    dl::ProcessingScreen processing;
    dl::SceneScreen scenes;
    dl::MixRackScreen mixRack;
    dl::MetersScreen metersScreen;
    dl::IoScreen ioScreen;
    dl::SurfaceScreen surfaceScreen;
    dl::StatusScreen status;
    dl::BottomBand bottomBand;
    dl::HelpPanel helpLeft, helpRight;
    universal::RoutingView routingView;
    universal::FxRackView fxRack;
    universal::MonitorView monitoring;
    universal::LibraryView library;
    universal::SetupView setup;
    std::array<std::unique_ptr<dl::StripUnit>, kStrips> strips;
    std::vector<std::pair<juce::String, std::unique_ptr<dl::Key>>> keys;
    std::array<std::unique_ptr<dl::Rotary>, 8> peq;
    std::array<std::unique_ptr<dl::Rotary>, 6> soft;
    std::unique_ptr<dl::Rotary> dataLeft, dataRight, phones;
    std::vector<Silk> silks;
    std::array<int, kBanks> layers { 0, 0 };
    Page page = Page::Scenes;
    juce::String litPageKey { "Page Scenes" };
    dl::RotaryFunction rotaryFunction = dl::RotaryFunction::Gain;
    dl::DisplayFunction displayFunction = dl::DisplayFunction::Name;
    int mixBus = -1, softLayer = 0;
    bool widthMode = false;
    Strip* boundStrip = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DLiveC3500Surface)
};

} // namespace livedaw::ui
