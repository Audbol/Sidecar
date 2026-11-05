#include "ui/X32MonitorSection.h"
#include "ui/HardwareLook.h"

namespace livedaw::ui::x32
{

using namespace universal;

namespace
{
    constexpr int kKey = 30;
} // namespace

MonitorSection::MonitorSection (UiContext& c, const Theme& t)
    : ctx (c), theme (t), dim ("DIM", t.accent, t), mono ("MONO", t.accent, t), mute ("MUTE", t.mute, t), talkA ("TALK A", t.solo, t),
      talkB ("TALK B", t.solo, t)
{
    auto& controls = ctx.engine; // its monitor and talk controls
    monitor.setBinding (bindFader ("MONITOR", controls.monitorLevelDb));
    phones.setBinding (bindFader ("PHONES", controls.phones.levelDb));
    talk.setBinding (bindFader ("TALK", controls.talk.levelDb));
    for (auto* k : { &monitor, &phones, &talk })
    {
        k->setCompact (true);
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
    for (auto* k : { &dim, &mono, &mute, &talkA, &talkB })
        addAndMakeVisible (*k);
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
    hw::section (g, getLocalBounds().toFloat().reduced (1.0f).withTrimmedTop (6.0f), "MONITOR", hw::Look::x32());
    g.setColour (hw::Look::x32().silk);
    g.setFont (type::font (type::kCaption, true));
    // TALKBACK's caption above its knob (the lower half).
    const auto lower = getLocalBounds().reduced (8).withTrimmedTop (getHeight() / 2);
    g.drawText ("TALKBACK", lower.withHeight (16), juce::Justification::centredLeft);
}

void MonitorSection::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto upper = r.removeFromTop (r.getHeight() / 2);
    upper.removeFromTop (20);
    // DIM and MONO, then MUTE, under the two knobs.
    auto muteRow = upper.removeFromBottom (kKey);
    upper.removeFromBottom (4);
    auto keys = upper.removeFromBottom (kKey);
    const int w = upper.getWidth() / 2;
    monitor.setBounds (upper.removeFromLeft (w).reduced (2));
    phones.setBounds (upper.reduced (2));
    dim.setBounds (keys.removeFromLeft (keys.getWidth() / 2).reduced (2, 0));
    mono.setBounds (keys.reduced (2, 0));
    mute.setBounds (muteRow.reduced (2, 0));

    r.removeFromTop (20);
    auto talkKeys = r.removeFromBottom (kKey);
    talk.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), w), r.getHeight()).reduced (2));
    talkA.setBounds (talkKeys.removeFromLeft (talkKeys.getWidth() / 2).reduced (2, 0));
    talkB.setBounds (talkKeys.reduced (2, 0));
}

} // namespace livedaw::ui::x32
