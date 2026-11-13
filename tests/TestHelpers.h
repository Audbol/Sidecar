#pragma once

#include "engine/MixerEngine.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace livedaw::test
{
/** The sends of Sidecar before it took the WING's factory sends: bus sends
    (to the console's 16 buses) and matrix sends at 0 dB with PAN LINK on, bus
    sends PRE (TAP), matrix sends post-fader; a matrix's direct input at 0 dB;
    an input channel's TAP point at PFL. */
inline void legacySends (Strip& s)
{
    if (s.getKind() != StripKind::Main && s.getKind() != StripKind::Matrix)
        for (int bus = 0; bus < kWingBuses; ++bus)
        {
            auto& send = s.sends[(size_t) bus];
            send.levelDb = 0.0f;
            send.panLink = true;
            send.mode = 0;
        }
    for (int i = 0; i < kWingMatrices; ++i)
    {
        auto& m = s.matrix[(size_t) i];
        m.levelDb = 0.0f;
        m.panLink = true;
        m.pre = false;
    }
    s.params.directLevelDb = 0.0f;
    s.params.tap = 5; // PFL
}

/** The fixed strips (aux, buses, mains, matrices) and the DCAs at 0 dB with
    their EQ on, buses not feeding the mains, and the monitoring of Sidecar
    before it took the WING's factory monitor settings (SOLO MODE STUDIO to
    PH+SPK, mains and matrices AFL, no PFL DIM, BAND SOLO +12 dB, talkback AUTO
    DIM 9 dB), the sends of legacySends, AUX 2 not patched, INPUT SELECT per
    strip and MUTEGROUP/SIP OVERRIDE off: the console the audio tests were
    written for (a factory WING has the faders down and the EQs off, solos LIVE
    to the phones). */
inline void unityConsole (MixerEngine& engine)
{
    for (auto kind : { StripKind::Aux, StripKind::Bus, StripKind::Main, StripKind::Matrix })
        for (auto& s : engine.getStrips (kind))
        {
            s->params.faderDb = 0.0f;
            s->params.eq().on = true;
            if (kind == StripKind::Bus)
                s->mains[0].on = false;
            if (kind == StripKind::Aux)
            {
                s->params.stereo = false;
                s->params.inputLeft = -1;
                s->params.inputRight = -1;
            }
            legacySends (*s);
        }
    for (auto& s : engine.getChannels())
        legacySends (*s);
    engine.muteOverride = false;
    for (int i = 0; i < kWingDcas; ++i)
        engine.getDca (i).faderDb = 0.0f;
    engine.soloMode = ld::kSoloStudio;
    engine.soloBus = 2;
    engine.mainSoloAfl = true;
    engine.matrixSoloAfl = true;
    engine.speakers.pflDimDb = 0.0f;
    engine.phones.source.pflDimDb = 0.0f;
    for (auto& path : engine.monitor.paths)
        path.bandSoloDb = 12.0f;
    for (auto& talk : engine.talk.paths)
    {
        talk.monDimDb = 9.0f;
        talk.busDimDb = 9.0f;
    }
}

/** Sets the number of channels; new channels get their fader at 0 dB (a new
    channel's fader is down, as on the WING). */
inline void setChannels (MixerEngine& engine, int count)
{
    const int before = (int) engine.getChannels().size();
    engine.setNumChannels (count);
    const auto& channels = engine.getChannels();
    for (int i = before; i < (int) channels.size(); ++i)
    {
        channels[(size_t) i]->params.faderDb = 0.0f;
        legacySends (*channels[(size_t) i]);
    }
}

/** Drives a MixerEngine with constant (DC) inputs and captures its outputs
    (`unity`: on the console of unityConsole; false: the console as it is). */
struct OfflineRig
{
    static constexpr int kBlock = 128;

    OfflineRig (MixerEngine& e, int numIns, int numOuts, bool unity = true) : engine (e)
    {
        if (unity)
            unityConsole (engine);
        engine.prepareOffline (48000.0, kBlock, numIns, numOuts);
        inputs.assign ((size_t) numIns, std::vector<float> (kBlock, 0.0f));
        outputs.assign ((size_t) numOuts, std::vector<float> (kBlock, 0.0f));
    }

    void setInput (int ch, float value) { std::fill (inputs[(size_t) ch].begin(), inputs[(size_t) ch].end(), value); }

    void run (int numBlocks = 20)
    {
        std::vector<const float*> in;
        std::vector<float*> out;
        for (auto& v : inputs)
            in.push_back (v.data());
        for (auto& v : outputs)
            out.push_back (v.data());

        for (int b = 0; b < numBlocks; ++b)
            engine.processBlock (in.data(), (int) in.size(), out.data(), (int) out.size(), kBlock);
    }

    float out (int ch) const { return outputs[(size_t) ch].back(); }

    MixerEngine& engine;
    std::vector<std::vector<float>> inputs, outputs;
};

inline constexpr float kCentre = 1.0f; // a mono channel at pan centre: unity on each side (the WING pan law)
inline constexpr float kEdge = 1.41421356f; // a mono channel panned hard: +3 dB on the near side

/** A plug-in with a side-chain input bus: it passes its main input and records how loud its side chain was in
    its last block (a DC key's level). */
class SidechainPlugin final : public juce::AudioPluginInstance
{
public:
    SidechainPlugin()
        : juce::AudioPluginInstance (BusesProperties()
                                         .withInput ("In", juce::AudioChannelSet::stereo())
                                         .withInput ("Sidechain", juce::AudioChannelSet::stereo())
                                         .withOutput ("Out", juce::AudioChannelSet::stereo()))
    {
    }

    std::atomic<float> keyLevel { 0.0f };
    juce::String shownName, shownMaker; // the screenshots give their plug-ins real-looking names

    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        d.name = getName();
        d.pluginFormatName = "Fake";
        d.fileOrIdentifier = "fake-keyed";
        d.uniqueId = d.deprecatedUid = juce::String ("fake-keyed").hashCode();
        d.manufacturerName = shownMaker.isNotEmpty() ? shownMaker : juce::String ("Tests");
        d.numInputChannels = 4;
        d.numOutputChannels = 2;
    }

    const juce::String getName() const override { return shownName.isNotEmpty() ? shownName : juce::String ("Fake Keyed"); }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    using juce::AudioPluginInstance::processBlock;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        keyLevel = buffer.getNumChannels() > 2 ? buffer.getMagnitude (2, 0, buffer.getNumSamples()) : -1.0f;
    }
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
               && (l.inputBuses.size() < 2 || l.inputBuses[1].isDisabled() || l.inputBuses[1] == juce::AudioChannelSet::stereo());
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

/** A plug-in with parameters and a state of its own: a continuous LEVEL, a stepped MODE (three choices) and a
    TAG that only its state holds. Its state is the three values, so a scene recall that applies it sets all of them. */
class ParameterPlugin final : public juce::AudioPluginInstance
{
public:
    ParameterPlugin()
        : juce::AudioPluginInstance (BusesProperties()
                                         .withInput ("In", juce::AudioChannelSet::stereo())
                                         .withOutput ("Out", juce::AudioChannelSet::stereo()))
    {
        juce::AudioProcessor::addParameter (level = new juce::AudioParameterFloat (juce::ParameterID { "level", 1 }, "Level", 0.0f, 1.0f, 0.5f));
        juce::AudioProcessor::addParameter (mode = new juce::AudioParameterChoice (juce::ParameterID { "mode", 1 }, "Mode", juce::StringArray { "A", "B", "C" }, 0));
    }

    juce::AudioParameterFloat* level = nullptr;
    juce::AudioParameterChoice* mode = nullptr;
    std::atomic<int> tag { 0 };
    juce::String shownName, shownMaker; // the screenshots give their plug-ins real-looking names

    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        d.name = getName();
        d.pluginFormatName = "Fake";
        d.fileOrIdentifier = "fake-params";
        d.uniqueId = d.deprecatedUid = juce::String ("fake-params").hashCode();
        d.manufacturerName = shownMaker.isNotEmpty() ? shownMaker : juce::String ("Tests");
        d.numInputChannels = d.numOutputChannels = 2;
    }

    const juce::String getName() const override { return shownName.isNotEmpty() ? shownName : juce::String ("Fake Params"); }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    using juce::AudioPluginInstance::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& data) override
    {
        const float values[3] = { level->get(), (float) mode->getIndex(), (float) tag.load() };
        data.replaceAll (values, sizeof (values));
    }

    void setStateInformation (const void* data, int size) override
    {
        if (size != 3 * (int) sizeof (float))
            return;
        const auto* values = static_cast<const float*> (data);
        *level = values[0];
        *mode = (int) values[1];
        tag = (int) values[2];
    }
};
} // namespace livedaw::test

#include <juce_events/juce_events.h>

#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
#else
namespace juce::detail
{
bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages); // juce_events, platform code
} // namespace juce::detail
#endif

namespace livedaw::test
{
/** Runs the message loop for `ms` (timers, async updates, plugins' latency
    changes and the sandbox's replies are delivered through it). */
inline void pumpMessages (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
    {
#if JUCE_MAC
        CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.002, true);
#else
        if (! juce::detail::dispatchNextMessageOnSystemQueue (true))
            juce::Thread::sleep (2);
#endif
    }
}

/** False where windows can't be opened: Linux without an X display (CI). Tests that
    put a window on the desktop skip without one (JUCE dereferences the display). */
inline bool hasDisplay()
{
#if JUCE_LINUX || JUCE_BSD
    return juce::SystemStats::getEnvironmentVariable ("DISPLAY", {}).isNotEmpty();
#else
    return true;
#endif
}
} // namespace livedaw::test

#include <juce_audio_processors/juce_audio_processors.h>

namespace livedaw::test
{
/** A minimal in-process "plugin" for tests: a gain stage that can be told to
    misbehave (emit NaN or throw). Its gain is its saved state. */
class FakePlugin final : public juce::AudioPluginInstance
{
public:
    enum class Mode
    {
        Gain,
        EmitNaN,
        Throw
    };

    explicit FakePlugin (float g = 1.0f, Mode m = Mode::Gain, juce::String id = "fake-gain")
        : juce::AudioPluginInstance (BusesProperties()
                                         .withInput ("In", juce::AudioChannelSet::stereo())
                                         .withOutput ("Out", juce::AudioChannelSet::stereo())),
          gain (g), mode (m), identifier (std::move (id))
    {
    }

    std::atomic<float> gain;
    std::atomic<Mode> mode;
    juce::String shownName, shownMaker; // the screenshots give their plug-ins real-looking names

    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        d.name = getName();
        d.pluginFormatName = "Fake";
        d.fileOrIdentifier = identifier;
        d.uniqueId = d.deprecatedUid = identifier.hashCode();
        d.manufacturerName = shownMaker.isNotEmpty() ? shownMaker : juce::String ("Tests");
        d.numInputChannels = d.numOutputChannels = 2;
    }

    const juce::String getName() const override { return shownName.isNotEmpty() ? shownName : "Fake " + identifier; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    using juce::AudioPluginInstance::processBlock;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        switch (mode.load())
        {
            case Mode::Gain:
                buffer.applyGain (gain.load());
                break;
            case Mode::EmitNaN:
                buffer.setSample (0, 0, std::numeric_limits<float>::quiet_NaN());
                break;
            case Mode::Throw:
                throw std::runtime_error ("plugin bug");
        }
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& data) override
    {
        const float g = gain.load();
        data.replaceAll (&g, sizeof (g));
    }

    void setStateInformation (const void* data, int size) override
    {
        if (size == sizeof (float))
            gain.store (*static_cast<const float*> (data));
    }

private:
    juce::String identifier;
};
} // namespace livedaw::test
