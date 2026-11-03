#include "engine/X32Midi.h"

#include "engine/MixerEngine.h"
#include "engine/Show.h"
#include "engine/Strip.h"

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
void X32Midi::handleMessage (int port, const juce::MidiMessage& m)
{
    ports.noteReceived (port);
    const int ch = m.getChannel();
    if (m.isProgramChange() && ch == kFaderChannel)
    {
        const int scene = m.getProgramChangeNumber();
        if (juce::isPositiveAndBelow (scene, show.getNumScenes()))
            show.recallScene (scene);
        return;
    }
    if (! m.isController() || ch < kFaderChannel || ch > kPanChannel)
        return;
    const int cc = m.getControllerNumber(), value = m.getControllerValue();
    applyValue (ch, cc, value);
    // Not echoed: what it holds now is what was received (or where it landed).
    last[(size_t) ch - 1][(size_t) cc] = currentValue (ch, cc);
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
                if (primed)
                    send (juce::MidiMessage::controllerEvent (ch, cc, v));
            }
        }
    primed = true;
}

void X32Midi::resendAll()
{
    for (auto& a : last)
        a.fill (-1);
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
