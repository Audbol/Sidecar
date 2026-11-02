#pragma once

// Strip parameters are owned by the Rust engine; these are its C++ views
// (see rust/crates/livedaw-ffi/include/livedaw_engine.h). The UI writes the
// atomics directly; the audio thread reads them once per block.

#include "livedaw_engine.h"

namespace livedaw
{

/** Upper bound on bus slots (send storage grows on demand; CPU is the real limit). */
constexpr int kMaxBuses = ld::kMaxBuses;

constexpr int kWingMains = ld::kWingMains;
constexpr int kWingMatrices = ld::kWingMatrices;
constexpr int kWingAux = ld::kWingAux;
/** The WING's 16 DCAs and 8 mute groups (its surface, screens and remotes). The
    console uses MixerEngine::getNumDcas() / getNumMuteGroups() of them, up to
    kMaxDcas / kMaxMuteGroups. */
constexpr int kWingDcas = ld::kWingDcas;
constexpr int kWingMuteGroups = ld::kWingMuteGroups;
constexpr int kMaxDcas = ld::kMaxDcas;
constexpr int kMaxMuteGroups = ld::kMaxMuteGroups;
/** FX slots the engine holds; FxSlots::getNumSlots() of them are in use (SETUP > GENERAL > FX SLOTS). */
constexpr int kMaxFx = ld::kMaxFx;
/** The WING's 16 FX slots: the default number in use, and the ones the WING and MIDI remotes see. */
constexpr int kWingFx = ld::kWingFx;
constexpr int kWingChannels = ld::kWingChannels;
constexpr int kWingBuses = ld::kWingBuses;
/** Bus slot `from` can send to bus slot `to`: any other bus (manual 5.10,
    BUS → BUS "from Bus 1-16 to BUS 1-16"; livedaw-engine routing.rs). */
constexpr bool busFeedsBus (int from, int to) noexcept { return from != to; }
/** The filter frequencies of a new or INITed channel (the engine's defaults),
    which the LO CUT / HI CUT controls reset to: 100 Hz and 10 kHz on the
    console's steps (100.2 Hz, 10k02 Hz). */
constexpr float kLowCutDefaultHz = 100.2374f;
constexpr float kHighCutDefaultHz = 10018.26f;
/** The key filters' and the crossover's FREQ of a new channel (1k0 Hz on the
    console's steps), which their controls reset to. */
constexpr float kKeyFilterDefaultHz = 1002.374f;

using InputParams = ld::InputParams;
using FilterParams = ld::FilterParams;
using DynSlotParams = ld::DynSlotParams;
using EqBlockParams = ld::EqBlockParams;
using InsertParams = ld::InsertParams;
using MainSendParams = ld::MainSendParams;
using MatrixSendParams = ld::MatrixSendParams;
using StripParams = ld::StripParams;
using SendParams = ld::SendParams;
using DcaParams = ld::DcaParams;
using FxParams = ld::FxParams;
/** An extra delay block's settings and an extra block's meters (ABI 28, a strip's chain). */
using DelayBlockParams = ld::DelayBlockParams;
using BlockMeters = ld::BlockMeters;
/** A set of DCAs or mute groups (0-based), and its atomic form shared with the engine. */
using GroupBits = ld::GroupBits;
using GroupMask = ld::GroupMask;
using Counts = ld::CountsC;

/** Bus send modes (list 4). Pre is the WING's TAP mode on input and aux
    channels (taken at the tap point; cut by the channel mute unless the send's
    ignoreMute is set) and pre-fader on buses. Group is post-fader and post-pan
    with no send level. */
enum class SendMode : int32_t
{
    Pre = 0,
    Post = 1,
    Group = 2
};

/** MAIN LINK (EngineControls::mainLink, list 12). */
enum class MainLink : int32_t
{
    Off = ld::kMainLinkOff,
    To2 = ld::kMainLink2,    // 1>2
    To3 = ld::kMainLink2_3,  // 1>2-3
    To4 = ld::kMainLink2_4   // 1>2-4
};

/** Post insert modes (list 6). */
enum class PostInsertMode : int32_t
{
    Fx = 0,
    AutomixX = 1,
    AutomixY = 2
};

} // namespace livedaw
