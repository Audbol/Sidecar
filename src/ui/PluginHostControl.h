#pragma once

#include "engine/MidiPorts.h"
#include "engine/PluginChain.h"
#include "ui/UiContext.h"

#include <array>
#include <functional>
#include <map>
#include <vector>

namespace livedaw::ui::ph
{

using ParamRef = PluginChain::ParamRef;
using MidiSource = PluginChain::MidiSource;

/** A plug-in of a channel's rack: the block's uid on the channel, and its place in the block's chain. */
struct PluginRef
{
    int channel = -1; // 0-based
    int uid = -1;
    int member = 0;
    /** A strip that is not one of the channels (a PLUGIN HOST rack of the Universal Mixer): it takes the place of `channel`. */
    Strip* strip = nullptr;

    bool isValid() const noexcept { return (channel >= 0 || strip != nullptr) && uid >= 0; }
    bool operator== (const PluginRef& o) const noexcept { return channel == o.channel && uid == o.uid && member == o.member && strip == o.strip; }
};

/** What a MIDI learn, or a mapping, aims at. */
struct Target
{
    enum class Kind
    {
        None,
        Param,     // a parameter of a plug-in
        Hot,       // a channel's hot knob (whatever plug-in the knob follows)
        Select,    // a channel's SELECT key
        Bypass,    // a channel's BYPASS key
        SceneGo,
        ScenePrev,
        SceneNext
    };
    Kind kind = Kind::None;
    int channel = -1;  // Param, Hot, Select, Bypass
    PluginRef plugin;  // Param
    ParamRef param;    // Param
    int hot = 0;       // Hot: 0 or 1

    bool operator== (const Target& o) const noexcept;
    static Target ofParam (const PluginRef&, const ParamRef&);
    static Target ofHot (int channel, int hot);
    static Target of (Kind kind, int channel = -1);
};

/** The PLUGIN HOST's control: what the hot knobs follow and MIDI learn (docs/PLUGIN_HOST.md, page 5).

    Hot knobs. A plug-in has up to two parameters assigned as hot (PluginChain::Control, saved with the plug-in).
    A channel's two knobs control the hot parameters of its focused plug-in: the one selected on RACK while
    that channel is the one RACK shows, else the first plug-in of the chain that has hot parameters. (A selected
    plug-in that has none gives way to the first that has.)

    MIDI. The devices are Sidecar's MidiPorts (the same layer every console's MIDI uses): their callbacks only
    queue the messages, which arrive here on the message thread, so everything below runs on it and sets
    parameters with setValueNotifyingHost. Nothing touches a plug-in from the MIDI thread. The inputs to listen
    to are an app setting. A mapping is a MIDI control (a controller, absolute or relative, or a note that
    toggles) and a target: a parameter of one plug-in is kept in that plug-in (PluginChain::Control: it
    follows the plug-in when the rack is reordered and goes with it when it is removed, and is saved with the
    show); a hot knob, a channel's SELECT or BYPASS and the scene keys are kept in the show (`Show::setHostControlXml`).
    A scene recall leaves all of them alone. Message thread only. */
class Control final : private juce::Timer
{
public:
    /** `useDevices` false: no MIDI device is opened (tests feed handleMidi()). */
    explicit Control (UiContext&, bool useDevices = true);
    ~Control() override;

    // What the surface lends it.
    std::function<PluginRef()> getFocus;               // the plug-in selected on RACK (uid < 0: none)
    std::function<void (int channel)> selectChannel;
    std::function<void (int delta)> sceneStep;
    std::function<void()> sceneGo;
    /** Something changed that the CONTROL page shows. */
    std::function<void()> onChanged;

    //==============================================================================
    // The racks

    /** Every plug-in of the channel's rack, in signal order. */
    std::vector<PluginRef> pluginsOf (int channel) const;
    juce::AudioPluginInstance* pluginOf (const PluginRef&) const;
    /** "EQ-ONE" ("" for a plug-in that is gone). */
    juce::String nameOf (const PluginRef&) const;
    PluginChain::Control controlOf (const PluginRef&) const;
    void setControlOf (const PluginRef&, PluginChain::Control);
    /** The parameters the plug-in offers (names, in order). */
    juce::StringArray parameterNames (const PluginRef&) const;
    /** Whether the plug-in runs in the sandbox (its parameters are mirrored from the helper's). */
    bool isSandboxed (const PluginRef&) const;

    //==============================================================================
    // Hot knobs

    /** The plug-in the channel's knobs follow (uid < 0: none). */
    PluginRef knobPlugin (int channel) const;

    struct Hot
    {
        bool assigned = false;
        float value = 0.0f;
        juce::String name;     // short: "CUTOFF"
        juce::String fullName;
        juce::String text;     // the value as the plug-in shows it
    };
    /** Both knobs of `channel`. */
    std::array<Hot, 2> getHots (int channel) const;
    Hot getHot (int channel, int knob) const { return getHots (channel)[(size_t) juce::jlimit (0, 1, knob)]; }
    /** A turn of the knob: `delta` in 0..1 of the parameter's range (a stepped parameter moves a step once the
        turns add up to half of one). False when the knob has no parameter. */
    bool turnHot (int channel, int knob, float delta);
    bool setHot (int channel, int knob, float value01);
    /** Assigns parameter `param` of the plug-in as its hot knob `knob`; the knob it was on (if the other) is freed. */
    bool assignHot (const PluginRef&, int knob, const ParamRef& param);
    void clearHot (const PluginRef&, int knob);

    //==============================================================================
    // MIDI devices

    static constexpr const char* inputsKey = "pluginhost.midi.inputs";
    static constexpr int kMaxInputs = 8;
    /** The MIDI inputs of this computer, and the ones switched on (by name; one that is gone stays listed). */
    static juce::StringArray availableInputs() { return MidiPorts::getInputDevices(); }
    juce::StringArray getEnabledInputs() const;
    void setInputEnabled (const juce::String& name, bool enabled);
    bool isInputEnabled (const juce::String& name) const { return getEnabledInputs().contains (name); }
    /** Whether an enabled input is open now. */
    bool isInputOpen (const juce::String& name) const;

    //==============================================================================
    // Learn and the mappings

    enum class Learn
    {
        Off,
        WaitTarget, // LEARN is on: waiting for a parameter, a hot knob, a key
        WaitMidi    // the target is taken: waiting for a MIDI control
    };
    Learn getLearn() const noexcept { return learn; }
    bool isLearning() const noexcept { return learn != Learn::Off; }
    const Target& getLearnTarget() const noexcept { return learnTarget; }
    /** LEARN on (or off again). */
    void setLearning (bool);
    /** The surface offers what was tapped or moved: taken (true) when LEARN waits for a target. */
    bool captureTarget (const Target&);
    /** A message from a device (the ports queue theirs here; tests call it). */
    void handleMidi (const juce::MidiMessage&);

    struct Row
    {
        MidiSource source;
        Target target;
        juce::String sourceText, targetText;
    };
    /** Every mapping: the keys' and hot knobs' first, then each channel's plug-ins' in rack order. */
    std::vector<Row> getMappings() const;
    int getNumMappings() const { return (int) getMappings().size(); }
    bool deleteMapping (int row);
    /** A controller's mode: absolute or relative (an endless encoder). */
    bool setMappingMode (int row, MidiSource::Mode);
    /** Deletes every mapping (those of the plug-ins too). */
    void clearAll();
    /** The note the page shows ("2 mappings went with EQ-ONE"), empty when there is none. */
    const juce::String& getNote() const noexcept { return note; }
    void clearNote();
    /** What LEARN says now ("MOVE A MIDI CONTROL FOR CH 3 HOT 1"). */
    juce::String describeLearn() const;
    juce::String describeTarget (const Target&) const;

    /** Reads the show's mappings again when they are not what this last wrote (a show was loaded). */
    void loadFromShow();
    /** Looks at the racks for plug-ins that went with their mappings (the timer does this too). */
    void poll();

    /** Applies a mapped control's value (a controller's 0..127; a note toggles) to a parameter. Message thread only. */
    static void applyTo (juce::AudioProcessorParameter&, const MidiSource&, int value);

private:
    struct Global
    {
        Target target;
        MidiSource source;
    };

    void timerCallback() override { poll(); }
    Strip* stripAt (int channel) const;
    juce::AudioProcessorParameter* hotParam (int channel, int knob) const;
    int slotOf (const PluginRef&) const;
    void finishLearn (const MidiSource&);
    void dispatch (const MidiSource&, int value);
    void actOn (const Target&, const MidiSource&, int value, bool rising);
    void addMapping (const Target&, const MidiSource&);
    void saveGlobal();
    void applyDevices();
    void changed();

    UiContext& ctx;
    MidiPorts ports;
    std::vector<Global> globals;
    Learn learn = Learn::Off;
    Target learnTarget;
    juce::String note;
    std::array<std::array<int, 128>, 16> lastCc {};
    std::map<int, float> carried; // stepped hot parameters: what the turns have added up to
    struct Seen
    {
        juce::String name;
        int maps = 0;
    };
    std::map<const void*, Seen> seen; // plug-ins that have mappings, as of the last poll
    juce::String savedText;           // the show's mappings as last written or read

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Control)
};

} // namespace livedaw::ui::ph
