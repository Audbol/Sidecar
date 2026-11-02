#pragma once

#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <memory>

namespace livedaw::ui::universal
{

/** The RECORDER view: the WING display's recorder pages, embedded as they
    are (tagged WING): MULTITRACK, the multitrack recorder (Sidecar's, on the
    show's recording session), and USB, the WING's USB recorder and player;
    and a key that opens the multitrack editor over the window. */
class RecorderView final : public juce::Component
{
public:
    RecorderView (UiContext&, const Theme&);
    ~RecorderView() override;

    enum class Page
    {
        Multitrack,
        Usb
    };
    void showPage (Page);
    Page getPage() const noexcept { return page; }
    /** The page shown (nullptr: no recording session for the multitrack page). */
    juce::Component* getPageComponent() const noexcept;

    ToggleKey multitrackTab, usbTab, editorKey;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    UiContext& ctx;
    const Theme& theme;
    Page page = Page::Multitrack;
    std::unique_ptr<juce::Component> multitrack, usb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecorderView)
};

} // namespace livedaw::ui::universal
