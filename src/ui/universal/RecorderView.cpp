#include "ui/universal/RecorderView.h"

#include "ui/UsbRecorderPage.h"
#include "ui/daw/RecorderPage.h"

namespace livedaw::ui::universal
{

RecorderView::RecorderView (UiContext& c, const Theme& t)
    : multitrackTab ("MULTITRACK", t.accent, t), usbTab ("USB", t.accent, t), editorKey ("MULTITRACK EDITOR", t.accent, t), ctx (c), theme (t)
{
    // Both are the WING display's pages.
    multitrackTab.device = devices::wing;
    usbTab.device = devices::wing;
    multitrackTab.isOn = [this] { return page == Page::Multitrack; };
    multitrackTab.onClick = [this] { showPage (Page::Multitrack); };
    usbTab.isOn = [this] { return page == Page::Usb; };
    usbTab.onClick = [this] { showPage (Page::Usb); };
    editorKey.onClick = [this]
    {
        if (ctx.showEditor)
            ctx.showEditor (true);
    };
    editorKey.setEnabled (ctx.recording != nullptr);
    if (ctx.recording != nullptr)
        multitrack = std::make_unique<RecorderPage> (ctx, *ctx.recording);
    usb = std::make_unique<UsbRecorderPage> (ctx);
    for (auto* k : { &multitrackTab, &usbTab, &editorKey })
        addAndMakeVisible (k);
    if (multitrack != nullptr)
        addChildComponent (*multitrack);
    addChildComponent (*usb);
    showPage (Page::Multitrack);
}

RecorderView::~RecorderView() = default;

juce::Component* RecorderView::getPageComponent() const noexcept
{
    return page == Page::Usb ? usb.get() : multitrack.get();
}

void RecorderView::showPage (Page p)
{
    page = p;
    if (multitrack != nullptr)
        multitrack->setVisible (p == Page::Multitrack);
    usb->setVisible (p == Page::Usb);
    multitrackTab.refresh();
    usbTab.refresh();
    repaint();
}

void RecorderView::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    if (page == Page::Multitrack && multitrack == nullptr)
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel));
        g.drawText ("No recording session", getLocalBounds(), juce::Justification::centred);
    }
}

void RecorderView::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap, sizes::kGapSmall);
    auto bar = area.removeFromTop (sizes::kTouch);
    multitrackTab.setBounds (bar.removeFromLeft (170));
    bar.removeFromLeft (sizes::kGapSmall);
    usbTab.setBounds (bar.removeFromLeft (170));
    editorKey.setBounds (bar.removeFromRight (220));
    area.removeFromTop (sizes::kGap);
    if (multitrack != nullptr)
        multitrack->setBounds (area);
    usb->setBounds (area);
}

} // namespace livedaw::ui::universal
