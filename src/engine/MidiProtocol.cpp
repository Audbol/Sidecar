#include "engine/MidiProtocol.h"

#include "engine/MidiRemote.h"
#include "engine/ProMixMidi.h"
#include "engine/Y02RMidi.h"
#include "engine/X32Midi.h"

namespace livedaw
{

std::unique_ptr<MidiProtocol> MidiProtocol::create (int id, MixerEngine& engine, Show& show, bool useDevices)
{
    switch (id)
    {
        case Wing:
            return std::make_unique<MidiRemote> (engine, show, useDevices);
        case X32:
            return std::make_unique<X32Midi> (engine, show, useDevices);
        case ProMix01:
            return std::make_unique<ProMixMidi> (engine, show, useDevices);
        case Yamaha02R:
            return std::make_unique<Y02RMidi> (engine, show, useDevices);
        default:
            return nullptr;
    }
}

} // namespace livedaw
