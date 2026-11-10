#include "engine/MidiProtocol.h"

#include "engine/Dm2000Midi.h"
#include "engine/MidiRemote.h"
#include "engine/ProMixMidi.h"
#include "engine/SqMidi.h"
#include "engine/Y02RMidi.h"
#include "engine/X32Midi.h"
#include "engine/Y01VMidi.h"

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
        case AhSq:
            return std::make_unique<SqMidi> (engine, show, useDevices);
        case Yamaha01V:
            return std::make_unique<Y01VMidi> (engine, show, useDevices);
        case Dm2000:
            return std::make_unique<Dm2000Midi> (engine, show, useDevices);
        default:
            return nullptr;
    }
}

} // namespace livedaw
