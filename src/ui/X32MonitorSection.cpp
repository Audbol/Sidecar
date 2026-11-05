#include "ui/X32MonitorSection.h"
#include "ui/HardwareLook.h"

namespace livedaw::ui::x32
{

using namespace universal;

namespace
{
    constexpr int kKey = 34; // a key and its name under it
    const juce::Colour kLit (0xffffc83a);
} // namespace

MonitorSection::MonitorSection (UiContext& c, const Theme& t)
    : ctx (c), theme (t), dim ("DIM", kLit, t), mono ("MONO", kLit, t), mute ("MUTE", t.mute, t), talkA ("TALK A", kLit, t),
      talkB ("TALK B", kLit, t)
{
    auto& controls = ctx.engine; // its monitor and talk controls
    monitor.setBinding (bindFader ("MONITOR", controls.monitorLevelDb));
    phones.setBinding (bindFader ("PHONES", controls.phones.levelDb));
    talk.setBinding (bindFader ("TALK", controls.talk.levelDb));
    // Pots, their names printed under them.
    for (auto* k : { &monitor, &phones, &talk })
    {
        k->setHardware (true);
        addAndMakeVisible (*k);
    }
    auto toggle = [] (ToggleKey& key, std::atomic<bool>& flag)
    {
        key.onClick = [&flag] { flag = ! flag.load(); };
        key.isOn = [&flag] { return flag.load(); };
    };
    toggle (dim, controls.monitorDim);
    toggle (mono, controls.monitorMono);
    toggle (mute, controls.monitorMute);
    // The talk keys latch (the X32's default; AUTO and PUSH are the WING surface's).
    toggle (talkA, controls.talk.paths[0].on);
    toggle (talkB, controls.talk.paths[1].on);
    for (auto* k : { &dim, &mono, &talkA, &talkB })
        addAndMakeVisible (*k);
    addChildComponent (mute); // not on the X32's panel
    startTimerHz (10);
}

MonitorSection::~MonitorSection()
{
    stopTimer();
}

DisplayKnob* MonitorSection::knob (const juce::String& label)
{
    for (auto* k : { &monitor, &phones, &talk })
        if (k->getBinding().label == label)
            return k;
    return nullptr;
}

ToggleKey* MonitorSection::key (const juce::String& text)
{
    for (auto* k : { &dim, &mono, &mute, &talkA, &talkB })
        if (k->getButtonText() == text)
            return k;
    return nullptr;
}

void MonitorSection::timerCallback()
{
    for (auto* k : { &dim, &mono, &mute, &talkA, &talkB })
        k->refresh();
}

void MonitorSection::paint (juce::Graphics& g)
{
    // Two sections, as on the panel: MONITOR, and TALKBACK with the EXT MIC socket.
    const auto& look = hw::Look::x32();
    hw::section (g, monitorArea.toFloat().withTrimmedTop (6.0f), "MONITOR", look);
    hw::section (g, talkArea.toFloat().withTrimmedTop (6.0f), "TALKBACK", look);
    const auto name = [&g, &look] (const juce::Component& k, const juce::String& text)
    {
        hw::silk (g, k.getBounds().toFloat().withY ((float) k.getBottom()).withHeight (12.0f).expanded (20.0f, 0.0f), text, look, 8.5f);
    };
    name (monitor, "MONITOR LEVEL");
    name (phones, "PHONES LEVEL");
    name (talk, "TALK LEVEL");
    // The socket: drawn, not a control.
    if (! socket.isEmpty())
    {
        auto s = socket.toFloat();
        g.setColour (juce::Colour (0xff0e0f10));
        g.fillEllipse (s);
        g.setColour (juce::Colour (0xffb9bcbe));
        g.drawEllipse (s.reduced (1.0f), 2.0f);
        g.setColour (juce::Colour (0xff3a3d40));
        for (int k = 0; k < 3; ++k)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) k / 3.0f - juce::MathConstants<float>::halfPi;
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (s.getCentre() + juce::Point<float> (std::cos (a), std::sin (a)) * s.getWidth() * 0.22f));
        }
        hw::silk (g, s.withY (s.getBottom() + 1.0f).withHeight (12.0f).expanded (20.0f, 0.0f), "EXT MIC", look, 8.5f);
    }
}

void MonitorSection::resized()
{
    auto r = getLocalBounds();
    monitorArea = r.removeFromLeft (r.getWidth() / 2 - 3);
    r.removeFromLeft (6);
    talkArea = r;
    const auto column = [] (juce::Rectangle<int> a) { return a.reduced (8, 8).withTrimmedTop (18); };
    const auto knobAt = [] (DisplayKnob& k, juce::Rectangle<int>& col, int size)
    {
        k.setBounds (col.removeFromTop (size).withSizeKeepingCentre (size, size));
        col.removeFromTop (18);
    };
    {
        auto col = column (monitorArea);
        const int size = juce::jmin (col.getWidth() - 10, 56);
        knobAt (monitor, col, size);
        knobAt (phones, col, size);
        mono.setBounds (col.removeFromTop (kKey).withSizeKeepingCentre (48, kKey));
        col.removeFromTop (6);
        dim.setBounds (col.removeFromTop (kKey).withSizeKeepingCentre (48, kKey));
    }
    {
        auto col = column (talkArea);
        const int size = juce::jmin (col.getWidth() - 10, 56);
        socket = col.removeFromTop (size - 8).withSizeKeepingCentre (size - 12, size - 12);
        col.removeFromTop (18);
        knobAt (talk, col, size);
        talkA.setBounds (col.removeFromTop (kKey).withSizeKeepingCentre (48, kKey));
        col.removeFromTop (6);
        talkB.setBounds (col.removeFromTop (kKey).withSizeKeepingCentre (48, kKey));
    }
    mute.setBounds ({});
}

} // namespace livedaw::ui::x32
