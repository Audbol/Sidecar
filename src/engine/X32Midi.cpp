#include "engine/X32Midi.h"

#include "engine/MixerEngine.h"
#include "engine/Show.h"
#include "engine/Strip.h"
#include "engine/WingRemote.h"

namespace livedaw
{

namespace
{
    constexpr int kFaderChannel = 1, kMuteChannel = 2, kPanChannel = 3;
    constexpr int kFirstMuteGroupCc = 80;
}

X32Midi::X32Midi (MixerEngine& e, Show& s, bool useDevices)
    : engine (e), show (s), ports (juce::StringArray ("X32"), useDevices)
{
    for (auto& a : last)
        a.fill (-1);
    ports.onMessage = [this] (int port, const juce::MidiMessage& m) { handleMessage (port, m); };
    startTimer (50);
}

X32Midi::~X32Midi()
{
    stopTimer();
    ports.onMessage = nullptr;
    ports.closeAll();
}

void X32Midi::loadSettings (juce::PropertiesFile& p)
{
    setEnabled (p.getIntValue (enableKey, All));
    ports.open ({ { true, p.getValue (inputKey, MidiPorts::virtualDevice), p.getValue (outputKey, MidiPorts::virtualDevice) } });
}

//==============================================================================
int X32Midi::faderToCc (float position) noexcept
{
    // 95 is 0 dB, which the X32's law puts at 3/4 of the travel.
    return juce::jlimit (0, 127, juce::roundToInt (position * 95.0f / 0.75f));
}

float X32Midi::ccToFader (int value) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (float) value * 0.75f / 95.0f);
}

int X32Midi::panToCc (float pan) noexcept
{
    return juce::jlimit (1, 127, 64 + juce::roundToInt (juce::jlimit (-100.0f, 100.0f, pan) * 0.63f));
}

float X32Midi::ccToPan (int value) noexcept
{
    return juce::jlimit (-100.0f, 100.0f, (float) (value - 64) / 0.63f);
}

//==============================================================================
Strip* X32Midi::stripAt (int cc) const
{
    auto pick = [] (const MixerEngine::StripList& list, int i) -> Strip*
    { return juce::isPositiveAndBelow (i, (int) list.size()) ? list[(size_t) i].get() : nullptr; };
    if (cc < 32)
        return pick (engine.getChannels(), cc);
    if (cc < 48)
        return pick (engine.getAux(), cc - 32); // aux in 1-8, then the FX returns
    if (cc < 64)
        return pick (engine.getBuses(), cc - 48);
    if (cc < 70)
        return pick (engine.getMatrices(), cc - 64);
    if (cc < 72)
        return pick (engine.getMains(), cc - 70); // LR, M/C
    return nullptr;
}

DcaParams* X32Midi::dcaAt (int cc) const
{
    const int d = cc - 72;
    return juce::isPositiveAndBelow (d, juce::jmin (8, engine.getNumDcas())) ? &engine.getDca (d) : nullptr;
}

int X32Midi::currentValue (int channel, int cc) const
{
    const auto& law = engine.getFaderLaw();
    if (channel == kMuteChannel && cc >= kFirstMuteGroupCc && cc < kFirstMuteGroupCc + kNumMuteGroups)
        return engine.muteGroups.contains (cc - kFirstMuteGroupCc) ? 127 : 0;
    if (cc >= kNumStrips)
        return -1;
    if (auto* d = dcaAt (cc))
    {
        if (channel == kFaderChannel)
            return faderToCc (law.toPosition (d->faderDb.load()));
        if (channel == kMuteChannel)
            return d->mute.load() ? 127 : 0;
        return -1; // DCAs have no pan
    }
    auto* s = stripAt (cc);
    if (s == nullptr)
        return -1;
    switch (channel)
    {
        case kFaderChannel: return faderToCc (law.toPosition (s->params.faderDb.load()));
        case kMuteChannel:  return s->params.mute.load() ? 127 : 0;
        case kPanChannel:   return panToCc (s->params.pan.load());
        default:            return -1;
    }
}

void X32Midi::applyValue (int channel, int cc, int value)
{
    const auto& law = engine.getFaderLaw();
    const bool on = value >= 64;
    if (channel == kMuteChannel && cc >= kFirstMuteGroupCc && cc < kFirstMuteGroupCc + kNumMuteGroups)
    {
        engine.muteGroups.set (cc - kFirstMuteGroupCc, on);
        return;
    }
    if (cc >= kNumStrips)
        return;
    if (auto* d = dcaAt (cc))
    {
        if (channel == kFaderChannel)
            d->faderDb.store (law.toDb (law.snap (ccToFader (value))));
        else if (channel == kMuteChannel)
            d->mute.store (on);
        return;
    }
    auto* s = stripAt (cc);
    if (s == nullptr)
        return;
    if (channel == kFaderChannel)
        s->params.faderDb.store (law.toDb (law.snap (ccToFader (value))));
    else if (channel == kMuteChannel)
        s->params.mute.store (on);
    else if (channel == kPanChannel)
        s->params.pan.store (ccToPan (value));
}

//==============================================================================
int X32Midi::ccBit (int channel, bool tx) noexcept
{
    const int rx = channel == kFaderChannel ? RxFader : channel == kMuteChannel ? RxMute : RxPan;
    return tx ? rx << 1 : rx;
}

void X32Midi::handleMessage (int port, const juce::MidiMessage& m)
{
    ports.noteReceived (port);
    if (m.isSysEx())
    {
        handleSysEx (m);
        return;
    }
    const int ch = m.getChannel();
    if (m.isProgramChange())
    {
        if ((enabled & RxProgram) == 0)
            return;
        const int n = m.getProgramChangeNumber();
        if (ch == kFaderChannel && juce::isPositiveAndBelow (n, show.getNumScenes()))
        {
            show.recallScene (n);
            lastScene = show.getCurrentSceneIndex(); // not sent back
        }
        else if (ch == kMuteChannel)
        {
            const int scene = show.getCurrentSceneIndex();
            if (show.getSnippet (scene, n).has_value())
                show.recallSnippet (scene, n);
        }
        return;
    }
    if (! m.isController() || ch < kFaderChannel || ch > kPanChannel)
        return;
    const int cc = m.getControllerNumber(), value = m.getControllerValue();
    if ((enabled & ccBit (ch, false)) == 0)
        return;
    applyValue (ch, cc, value);
    // Not echoed: what it holds now is what was received (or where it landed).
    last[(size_t) ch - 1][(size_t) cc] = currentValue (ch, cc);
}

void X32Midi::handleSysEx (const juce::MidiMessage& m)
{
    // F0 00 20 32 32 <text> F7: getSysExData() is what lies between F0 and F7.
    const auto* data = m.getSysExData();
    const int size = m.getSysExDataSize();
    if ((enabled & SysEx) == 0 || size < 4 || data[0] != 0x00 || data[1] != 0x20 || data[2] != 0x32 || data[3] != 0x32)
        return;
    int end = size;
    while (end > 4 && data[end - 1] == 0) // some senders end the text with a zero
        --end;
    const auto text = juce::String::fromUTF8 (reinterpret_cast<const char*> (data + 4), end - 4);
    int set = 0;
    const auto answer = engine.getWing().runX32Text (text, set);
    if (answer.isEmpty())
        return;
    juce::MemoryBlock reply;
    const juce::uint8 header[] = { 0x00, 0x20, 0x32, 0x32 };
    reply.append (header, sizeof (header));
    reply.append (answer.toRawUTF8(), answer.getNumBytesAsUTF8());
    send (juce::MidiMessage::createSysExMessage (reply.getData(), (int) reply.getSize()));
}

void X32Midi::sendFeedback()
{
    for (int ch = kFaderChannel; ch <= kPanChannel; ++ch)
        for (int cc = 0; cc < 128; ++cc)
        {
            const int v = currentValue (ch, cc);
            auto& seen = last[(size_t) ch - 1][(size_t) cc];
            if (v >= 0 && v != seen)
            {
                seen = v;
                if (primed && (enabled & ccBit (ch, true)) != 0)
                    send (juce::MidiMessage::controllerEvent (ch, cc, v));
            }
        }
    // A scene recalled on the console: its program change on channel 1.
    const int scene = show.getCurrentSceneIndex();
    if (scene != lastScene)
    {
        if (primed && scene >= 0 && scene < 128 && (enabled & TxProgram) != 0)
            send (juce::MidiMessage::programChange (kFaderChannel, scene));
        lastScene = scene;
    }
    primed = true;
}

void X32Midi::resendAll()
{
    for (auto& a : last)
        a.fill (-1);
    lastScene = -1;
    primed = true;
    sendFeedback();
}

void X32Midi::send (const juce::MidiMessage& m)
{
    if (onSend)
        onSend (m);
    ports.send (0, m);
}

} // namespace livedaw
