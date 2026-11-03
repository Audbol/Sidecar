#include "engine/MidiProtocol.h"

#include "engine/MidiRemote.h"

namespace livedaw
{

std::unique_ptr<MidiProtocol> MidiProtocol::create (int id, MixerEngine& engine, Show& show, bool useDevices)
{
    switch (id)
    {
        case Wing:
            return std::make_unique<MidiRemote> (engine, show, useDevices);
        default:
            return nullptr;
    }
}

} // namespace livedaw
