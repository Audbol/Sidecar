#pragma once

#include "ui/UiContext.h"
#include "ui/universal/MixerConfigPanel.h"
#include "ui/universal/RemotePanel.h"
#include "ui/universal/ValueControls.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <memory>

namespace livedaw::ui::universal
{

/** The SETUP view. CONSOLE: the configuration editor, how many channels,
    aux, buses, mains, matrices, DCAs, mute groups and FX slots the console
    has, changed without a reset (docs/UNIVERSAL_MIXER.md): APPLY adds at the
    end and keeps everything else; it removes from the end, asking first,
    channels and aux whatever they hold, but never a bus, main or matrix that
    something feeds, a DCA or mute group with members, or an FX slot in use
    (those stay, and it says so). AUDIO: the audio device. REMOTE: the WING,
    web and MIDI remotes' settings (RemotePanel). PREFERENCES: the interface,
    delay compensation and output protection. On a dLive CONSOLE is its Mixer Config
    (MixerConfigPanel) instead: its counts follow from how its busses are laid out. */
class SetupView final : public juce::Component, private juce::Timer
{
public:
    SetupView (UiContext&, const Theme&);
    ~SetupView() override;

    enum class Page
    {
        Console,
        Audio,
        Remote,
        Preferences
    };
    void showPage (Page);
    Page getPage() const noexcept { return page; }

    /** The configuration editor's kinds, in its order. */
    enum Kind
    {
        Channels,
        Aux,
        Buses,
        Mains,
        Matrices,
        Dcas,
        MuteGroups,
        FxSlots,
        NumKinds
    };
    /** What the console has now, and what APPLY would make it. */
    int getCurrent (Kind) const;
    int getPending (Kind k) const noexcept { return pending[(size_t) k]; }
    void setPending (Kind, int count);
    /** What APPLY would take away (empty: nothing), one line per kind. */
    juce::StringArray describeRemovals() const;
    /** Applies the pending counts (without asking) and says what stayed. */
    void apply();
    /** Back to what the console has. */
    void revert();
    const juce::String& getStatus() const noexcept { return status; }
    void refresh();

    ToggleKey consoleTab, audioTab, remoteTab, prefsTab, applyKey, revertKey;
    ChoiceBar interfaceChoice;
    ToggleKey compensationKey, protectionKey;
    RemotePanel remote;
    MixerConfigPanel mixerConfig;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row;
    /** Which of the two CONSOLE pages shows. */
    void showConsolePage();
    void timerCallback() override
    {
        if (isVisible())
            refresh();
    }

    UiContext& ctx;
    const Theme& theme;
    Page page = Page::Console;
    std::array<int, NumKinds> pending {};
    std::vector<std::unique_ptr<Row>> rows;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> device;
    juce::String status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetupView)
};

} // namespace livedaw::ui::universal
