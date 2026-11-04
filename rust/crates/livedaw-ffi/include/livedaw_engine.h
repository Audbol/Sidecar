// C++ view of the Sidecar Rust engine (crate livedaw-ffi).
//
// The structs below mirror the #[repr(C)] blocks in livedaw-engine/src/params.rs
// and sends.rs field for field. The Rust side owns the memory; C++ reads and
// writes the atomics directly. std::atomic<float/bool/int> are lock-free and have
// the same size and bit layout as Rust's AtomicU32 (f32 bits) / AtomicBool /
// AtomicI32. ld::verifyLayout() compares both sides at startup.
//
// The layout follows the Behringer WING: every strip has an INPUT block, a
// FILTER, a GATE slot and a DYNAMICS slot (each holding any of the 32 dynamics
// models), an EQ (one of the EQ models), PRE and POST inserts (FX slots),
// sends to buses, the 4 mains and the 8 matrices, DCA and mute group masks.
// Model parameters are stored by index in the model's table; ld_model_* below
// describes the tables (names, ranges, units, choices) for the UI. A strip's
// chain can also hold extra blocks and user taps (ABI 28, ld_strip_chain).
//
// If you change a struct here, change it in Rust too (and bump LD_ABI_VERSION).

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ld
{

static_assert (sizeof (std::atomic<float>) == 4 && std::atomic<float>::is_always_lock_free);
static_assert (sizeof (std::atomic<bool>) == 1 && std::atomic<bool>::is_always_lock_free);
static_assert (sizeof (std::atomic<int32_t>) == 4 && std::atomic<int32_t>::is_always_lock_free);

constexpr int kAbiVersion = 45;
/** ABI 23: ld_fx_readouts gives the slot levels (in L, in R, out L, out R) then the device's readouts. */
constexpr int kFxLevels = 4;
constexpr int kFxReadouts = 48;
constexpr int kMaxBuses = 16384;
/** The WING's 4 mains, 8 matrices and 8 aux (its surface and screens). ABI 26: a
    console has any number of mains and matrices, up to kMaxMains / kMaxMatrices
    (ld_engine_counts). */
constexpr int kWingMains = 4;
constexpr int kWingMatrices = 8;
constexpr int kWingAux = 8;
constexpr int kMaxMains = 256;
constexpr int kMaxMatrices = 256;
/** ABI 25: DCAs and mute groups the engine holds; how many the console uses is
    set at run time (ld_engine_counts, ld_engine_set_num_dcas, ld_engine_set_num_mute_groups). */
constexpr int kGroupWords = 4;
constexpr int kMaxGroups = kGroupWords * 64;
constexpr int kMaxDcas = kMaxGroups;
constexpr int kMaxMuteGroups = kMaxGroups;
/** The WING's 16 DCAs and 8 mute groups (its surface, screens and remote). */
constexpr int kWingDcas = 16;
constexpr int kWingMuteGroups = 8;
/** FX slots the engine holds; ld_engine_num_fx of them are in use (the WING's 16 by default). */
constexpr int kMaxFx = 64;
/** The WING's FX slots: the default number in use and the ones the WING remote sees. */
constexpr int kWingFx = 16;
constexpr int kWingChannels = 40;
constexpr int kWingBuses = 16;
constexpr int kMaxDynParams = 20;
constexpr int kMaxEqParams = 32;
constexpr int kMaxToolParams = 2;
constexpr int kMaxFxParams = 64;
/** The WING colour (1..18) of a new channel, source and DCA: Dark Blue. */
constexpr int kDefaultColour = 1;
/** FX slot models (indices into model table 4): an empty slot, and EXT (a
    plugin, or with SOURCE OUTBOARD the console's outboard insert). */
constexpr int kFxNone = 0;
constexpr int kFxExt = 1;
/** The EXT device's parameters (FxParams::values): the WING's /fx/N/EXT parameters, then SOURCE. */
namespace ext
{
    constexpr int kGroup = 0;   // RETURN group: OFF, LCL, AUX, A, B, C, SC, USB, CRD, MOD, PLAY, AES
    constexpr int kInput = 1;   // RETURN's first input, 1-64
    constexpr int kMode = 2;    // FX MODE: MONO, STEREO, MID/SIDE
    constexpr int kLatency = 3; // LATENCY, samples (beyond one block and the interface's latency)
    constexpr int kTrim = 4;    // TRIM, dB (the send level)
    constexpr int kSource = 5;  // SOURCE: 0 PLUGIN, 1 OUTBOARD
} // namespace ext
/** The recorder's mono playback channels: input index kPlaybackSourceBase + n
    (in inputLeft/Right, altInputLeft/Right, record ports) is playback channel n. */
constexpr int kNumPlaybackChannels = 64;
constexpr int32_t kPlaybackSourceBase = 0x10000;
constexpr int kMaxRecordTracks = 64;
/** The USB recorder / player: USB PLAYER n is input kUsbPlayerSourceBase + n
    (n < kUsbChannels); the RECORDER output group has kUsbChannels ports. */
constexpr int kUsbChannels = 4;
constexpr int32_t kUsbPlayerSourceBase = 0x20000;
/** The oscillators (the OSC source group): OSC n is input kOscSourceBase + n
    (n < kNumOscillators); ld_engine_oscillators holds their settings. */
constexpr int kNumOscillators = 2;
constexpr int32_t kOscSourceBase = 0x30000;
/** ABI 19, the strip sources (manual 2.6): a bus, main or matrix as a
    channel's source, input kStripSourceBase + n: the WING's $BUS (n 0-31, a
    bus slot * 2 + side), $MAIN (32-39) and $MTX (40-55) signals
    (ld_strip_source_input). A channel hears them one block late. */
constexpr int32_t kStripSourceBase = 0x40000;
constexpr int kNumStripSources = 2 * (kWingBuses + kWingMains + kWingMatrices);
/** ABI 19, the USER SIGNAL group (USR): USR n is input kUsrSourceBase + n - 1;
    USR 1-24 user signals (ld_engine_user_signals), USR 25-56 user patches
    (ld_engine_user_patches). */
constexpr int32_t kUsrSourceBase = 0x50000;
constexpr int kNumUserSignals = 24;
constexpr int kNumUserPatches = 32;
constexpr int kNumUsrSources = kNumUserSignals + kNumUserPatches;
/** The FX returns (Phase 4 step 9c, an X32's FX 1-4 on its FX return
    channels): FX slot n's stereo output as a send effect is input
    kFxReturnSourceBase + 2n (L) and + 2n + 1 (R), n < kNumSendFx. */
constexpr int32_t kFxReturnSourceBase = 0x60000;
constexpr int kNumSendFx = 8;
/** DYNAMICS KEY SOURCE of aux, buses, mains and matrices (DynSlotParams::scSource):
    kKeyStripBase * kind (StripKindId) + index; they take one of kNumOutputKeyTaps taps. */
constexpr int32_t kKeyStripBase = 0x10000;
constexpr int kNumOutputKeyTaps = 6;
/** RECORDER tap codes (ld_usb_rec_tap): -1 off, 0.. an input index (a source),
    kUsbTapStripBase + kind * 0x100000 + (number - 1) * 2 + channel a strip. */
constexpr int32_t kUsbTapStripBase = 0x01000000;
/** Direct outs (ROUTING > OUTPUTS CH / BUS DIRECT, RECORDER ports): kDirectSourceBase +
    ((kind * 0x10000 + number - 1) * 2 + side) * 2 + (POST FADER ? 1 : 0), for input channels, aux
    and buses (Rust: routing::DirectSource). As an output's source or a RECORDER tap. */
constexpr int32_t kDirectSourceBase = 0x03000000;
constexpr int kSamplesPerPeak = 256;
/** The longest plugin delay compensation (samples); paths needing more stay late by the rest. */
constexpr int kMaxPdc = 8192;
/** TAP EQ bands (low shelf, peak, high shelf). */
constexpr int kTapEqBands = 3;
/** RTA bands: one per semitone, E0 (20.6 Hz) .. D#10 (19.9 kHz), see ld_rta_band_hz. */
constexpr int kRtaBands = 120;
/** ld_wing_sync_surface flags: a client changed the show's layers / custom controls. */
constexpr uint32_t kWingSurfaceLayers = 1;
constexpr uint32_t kWingSurfaceControls = 2;
/** ABI 28, a strip's chain: up to kMaxExtraBlocks extra blocks, and user taps with
    the ids kFirstUserTap .. kFirstUserTap + kMaxUserTaps - 1 (the WING's taps are 0-9). */
constexpr int kMaxExtraBlocks = 32;
constexpr int kFirstUserTap = 16;
constexpr int kMaxUserTaps = 32;

/** ABI 28: what a step of a strip's chain runs (ChainStepC::kind): a node of the
    strip's template, or an extra block. */
enum StepKind : int32_t
{
    kStepInput = 0,
    kStepDelay = 1,
    kStepFilter = 2,
    kStepGate = 3,
    kStepEq = 4,
    kStepDynamics = 5,
    kStepPreInsert = 6,  // INS 1
    kStepPostInsert = 7, // INS 2
    kStepSlot = 8,       // a process order slot that runs no block
    kStepPfl = 9,
    kStepFader = 10,
    kStepPost = 11,
    kStepTapShaper = 12, // TAP EQ and TAP WIDTH
    kStepTap = 13,       // a tap point (arg: its id)
    kStepRta = 14,       // an RTA point (arg: its index)
    kStepBlock = 15      // an extra block (arg: its kind, uid: its id)
};

/** ABI 28: the kinds of extra block (ld_engine_insert_block, BlockC::kind). */
enum ChainBlockKind : int32_t
{
    kBlockFilter = 0,   // FilterParams
    kBlockEq = 1,       // EqBlockParams (a new one is off)
    kBlockDynamics = 2, // DynSlotParams (a new one is off: COMP)
    kBlockInsert = 3,   // InsertParams
    kBlockDelay = 4     // DelayBlockParams
};

/// ABI 30: the console a configuration emulates (ld_engine_profile).
enum ProfileId : int32_t
{
    kProfileUniversal = 0, // no console's limits: the Universal Mixer's own
    kProfileWing = 1,
    kProfileX32 = 2,
    kProfileProMix01 = 3, // ABI 43, Phase 5
    kProfileYamaha02R = 4 // ABI 45, Phase 6: its processing, effects and MIDI follow
};

enum StripKindId : int32_t
{
    kChannel = 0,
    kAux = 1,
    kBus = 2,
    kMain = 3,
    kMatrix = 4
};

/// Fader reference kinds beyond the strip kinds (0..4): DCAs, FX parameters,
/// MIDI CCs and blank layer slots.
constexpr int32_t kFaderDca = 5;
constexpr int32_t kFaderFxParam = 6;
constexpr int32_t kFaderMidiCc = 7;
constexpr int32_t kFaderEmpty = 8; // a blank layer slot (ABI 14)
constexpr int32_t kFaderSelSend = 9; // the selected channel's send to toKind / toId (ABI 20)
constexpr int32_t kFaderBusCustom = 1; // FaderRefC.flags: a send fader with its destination's customization (ABI 20)

/// Tap points (index into list 1). WING names: INPUT, FILTER, TAP 3-5, PRE FDR,
/// POST FDR, POST PROC. Only input channels choose theirs; aux, buses, mains and
/// matrices always tap at kTapPfl (after INS 1, pre-fader).
enum TapPoint : int32_t
{
    kTapIn = 0,
    kTapFilter = 1,
    kTapPfl = 5,
    kTapAfl = 6,
    kTapPost = 7
};

/// MAIN LINK (EngineControls::mainLink, list 12): Main 1's fader also moves Mains 2..N.
enum MainLinkMode : int32_t
{
    kMainLinkOff = 0,
    kMainLink2 = 1,   // 1>2
    kMainLink2_3 = 2, // 1>2-3
    kMainLink2_4 = 3  // 1>2-4
};

/** ABI 25: a set of groups (DCAs or mute groups), 0-based: bit n of word w is
    group 64w + n. livedaw_engine::GroupSet. */
struct GroupBits
{
    uint64_t words[kGroupWords] {};

    /** Groups 0..63 from a mask (bit n = group n). */
    static constexpr GroupBits mask (uint64_t low) noexcept { GroupBits b; b.words[0] = low; return b; }
    /** Groups 0..n-1. */
    static constexpr GroupBits first (int n) noexcept
    {
        GroupBits b;
        for (int w = 0; w < kGroupWords; ++w)
        {
            const int bits = n - w * 64;
            b.words[w] = bits >= 64 ? ~uint64_t (0) : bits <= 0 ? 0 : (uint64_t (1) << bits) - 1;
        }
        return b;
    }
    static constexpr GroupBits all() noexcept { return first (kMaxGroups); }
    static constexpr GroupBits single (int i) noexcept { GroupBits b; b.set (i, true); return b; }

    constexpr bool contains (int i) const noexcept { return i >= 0 && i < kMaxGroups && ((words[i / 64] >> (i % 64)) & 1u) != 0; }
    constexpr void set (int i, bool on) noexcept
    {
        if (i < 0 || i >= kMaxGroups)
            return;
        const uint64_t bit = uint64_t (1) << (i % 64);
        words[i / 64] = on ? (words[i / 64] | bit) : (words[i / 64] & ~bit);
    }
    constexpr bool any() const noexcept { return (words[0] | words[1] | words[2] | words[3]) != 0; }
    /** Groups 0..63 as a mask (the WING's groups fit). */
    constexpr uint64_t low() const noexcept { return words[0]; }
    int count() const noexcept
    {
        int n = 0;
        for (auto w : words)
            for (; w != 0; w &= w - 1)
                ++n;
        return n;
    }

    constexpr GroupBits operator& (const GroupBits& o) const noexcept { GroupBits b; for (int w = 0; w < kGroupWords; ++w) b.words[w] = words[w] & o.words[w]; return b; }
    constexpr GroupBits operator| (const GroupBits& o) const noexcept { GroupBits b; for (int w = 0; w < kGroupWords; ++w) b.words[w] = words[w] | o.words[w]; return b; }
    constexpr GroupBits operator~() const noexcept { GroupBits b; for (int w = 0; w < kGroupWords; ++w) b.words[w] = ~words[w]; return b; }
    constexpr GroupBits& operator&= (const GroupBits& o) noexcept { return *this = *this & o; }
    constexpr GroupBits& operator|= (const GroupBits& o) noexcept { return *this = *this | o; }
    constexpr bool operator== (const GroupBits& o) const noexcept
    {
        for (int w = 0; w < kGroupWords; ++w)
            if (words[w] != o.words[w])
                return false;
        return true;
    }
    constexpr bool operator!= (const GroupBits& o) const noexcept { return ! (*this == o); }
};

/** ABI 25: a GroupBits shared with the audio thread (livedaw_engine::AtomicGroupSet). */
struct GroupMask
{
    std::atomic<uint64_t> words[kGroupWords];

    GroupBits load() const noexcept
    {
        GroupBits b;
        for (int w = 0; w < kGroupWords; ++w)
            b.words[w] = words[w].load (std::memory_order_relaxed);
        return b;
    }
    void store (const GroupBits& b) noexcept
    {
        for (int w = 0; w < kGroupWords; ++w)
            words[w].store (b.words[w], std::memory_order_relaxed);
    }
    bool contains (int i) const noexcept
    {
        return i >= 0 && i < kMaxGroups && ((words[i / 64].load (std::memory_order_relaxed) >> (i % 64)) & 1u) != 0;
    }
    /** Adds or removes one group (atomically for that group). */
    void set (int i, bool on) noexcept
    {
        if (i < 0 || i >= kMaxGroups)
            return;
        const uint64_t bit = uint64_t (1) << (i % 64);
        if (on)
            words[i / 64].fetch_or (bit, std::memory_order_relaxed);
        else
            words[i / 64].fetch_and (~bit, std::memory_order_relaxed);
    }
    void toggle (int i) noexcept { set (i, ! contains (i)); }
    bool any() const noexcept { return load().any(); }
    void clear() noexcept { store ({}); }
};

static_assert (sizeof (std::atomic<uint64_t>) == 8 && std::atomic<uint64_t>::is_always_lock_free);

struct InputParams
{
    std::atomic<float> trimDb, balanceDb;
    std::atomic<bool> invert, delayOn;
    std::atomic<int32_t> delayMode; // list 5: M, FT, MS, SMP
    std::atomic<float> delay;
};

struct FilterParams
{
    std::atomic<bool> lowCut, highCut, tool;
    std::atomic<float> lowCutFreq;
    std::atomic<int32_t> lowCutSlope; // list 8
    std::atomic<float> highCutFreq;
    std::atomic<int32_t> highCutSlope; // list 9
    std::atomic<int32_t> toolModel;    // table 3
    std::atomic<float> toolParams[kMaxToolParams];
};

struct DynSlotParams
{
    std::atomic<bool> on;
    std::atomic<int32_t> model; // table 0 (use ld_strip_set_model to change it)
    std::atomic<float> values[kMaxDynParams];
    std::atomic<float> mix, gain;
    std::atomic<int32_t> scFilter; // list 2
    std::atomic<float> scFreq, scQ;
    // scSource: -1 = self, else a channel index (input channels' list), or
    // kKeyStripBase * kind + index for an aux, bus, main or matrix (the list
    // of aux, buses, mains and matrices). scTap: list 1 from a channel, else
    // BUS, DYN, PFL, AFL, EQ, INS2 (kNumOutputKeyTaps).
    std::atomic<int32_t> scSource, scTap;
    std::atomic<int32_t> xoKind; // list 3
    std::atomic<float> xoFreq, xoQ, xoDepth;
    const void* modelTable_[2]; // ABI 36: the engine's (its console's models); not read here
};

struct EqBlockParams
{
    std::atomic<bool> on;
    std::atomic<int32_t> model; // table 1 (channels, aux) or 2 (buses, mains, matrices)
    std::atomic<float> mix;
    std::atomic<float> values[kMaxEqParams];
    const void* modelTable_[2]; // ABI 36: the engine's (its console's models); not read here
};

struct InsertParams
{
    std::atomic<bool> on;
    std::atomic<int32_t> fx;   // FX slot, -1 = none
    std::atomic<int32_t> mode; // post insert: list 6 (FX, AUTO_X, AUTO_Y)
    std::atomic<float> weight; // automix weight, dB
};

/** A strip's send to a main (ABI 26: ld_strip_main_send, by the main's index). */
struct MainSendParams
{
    std::atomic<bool> on, pre; // pre: from the tap point (the strip's mute still cuts it)
    std::atomic<float> levelDb;
    std::atomic<int32_t> tap; // ABI 27: a user tap of the source (16..47) to take, -1 = none
    float lastGain_[2]; // owned by the audio thread: do not touch
};

struct MatrixSendParams
{
    std::atomic<bool> on, pre;
    std::atomic<bool> ignoreMute; // IGNORE CHANNEL MUTE (input/aux channels, pre/TAP mode only)
    std::atomic<bool> panLink;    // PAN LINK (input/aux channels): the pan follows the channel's
    std::atomic<bool> panReversed; // with panLink: mirrored (LINK REVERSED)
    std::atomic<float> levelDb, pan;
    std::atomic<int32_t> tap; // ABI 27: a user tap of the source (16..47) to take, -1 = none
    float lastGain_[2]; // owned by the audio thread: do not touch
};

/** ABI 28: a delay block's settings (Rust: blocks::DelayParams), as the strip's own delay. */
struct DelayBlockParams
{
    std::atomic<bool> on;
    std::atomic<int32_t> mode; // list 5: M, FT, MS, SMP
    std::atomic<float> delay;  // in `mode` units
};

/** ABI 28: what an extra block measures (written by the audio thread). */
struct BlockMeters
{
    std::atomic<float> reductionDb; // dynamics: gain reduction in the latest block, dB
    std::atomic<bool> active;       // dynamics: working (the DYN LED)
    std::atomic<float> outPeak;     // output peak since the last read (linear): exchange it with 0
};

/** ABI 28: a step of a strip's chain (ld_strip_chain). */
struct ChainStepC
{
    int32_t kind; // StepKind; for a process order slot, the block it runs now
    int32_t arg;  // a tap's id, an RTA point, an extra block's ChainBlockKind; else -1
    int32_t uid;  // an extra block's id (unique on the strip); else -1
    int32_t slot; // the process order slot (0-3, input channels) the step is; else -1
};

/** ABI 28: an extra block of a strip's chain (ld_strip_block). Its pointers stay
    valid until the block is removed and ld_engine_collect has run. */
struct BlockC
{
    int32_t uid;
    int32_t kind; // ChainBlockKind
    void* params; // by kind: FilterParams, EqBlockParams, DynSlotParams, InsertParams, DelayBlockParams
    BlockMeters* meters;
};

/// TAP EQ (WING /ch/n/peq): band 0 low shelf, 1 peak, 2 high shelf.
struct TapEqBand
{
    std::atomic<float> gainDb; // -15..15
    std::atomic<float> freq;   // 20..20000 Hz
    std::atomic<float> q;      // 0.44..10
};

struct TapEqParams
{
    std::atomic<bool> on;
    TapEqBand bands[kTapEqBands];
};

struct StripParams
{
    InputParams input;
    // ABI 32: the strip's own blocks are chain blocks (Phase 3 step 5), owned by
    // the engine; reached through filter(), gate(), eq(), dynamics(), preInsert()
    // and postInsert(). Aux channels have no post insert (INS 2).
    FilterParams* filter_;
    DynSlotParams* gate_;
    EqBlockParams* eq_;
    DynSlotParams* dynamics_;
    InsertParams *preInsert_, *postInsert_;
    const void* ownBlocks_[6]; // the engine's: what the pointers point into

    FilterParams& filter() { return *filter_; }
    const FilterParams& filter() const { return *filter_; }
    DynSlotParams& gate() { return *gate_; }
    const DynSlotParams& gate() const { return *gate_; }
    EqBlockParams& eq() { return *eq_; }
    const EqBlockParams& eq() const { return *eq_; }
    DynSlotParams& dynamics() { return *dynamics_; }
    const DynSlotParams& dynamics() const { return *dynamics_; }
    InsertParams& preInsert() { return *preInsert_; }
    const InsertParams& preInsert() const { return *preInsert_; }
    InsertParams& postInsert() { return *postInsert_; }
    const InsertParams& postInsert() const { return *postInsert_; }

    std::atomic<int32_t> processOrder; // list 0 (input channels only)
    std::atomic<int32_t> tap;          // list 1 (input channels only)

    std::atomic<float> faderDb;
    std::atomic<float> pan;   // -100..100
    std::atomic<float> width; // -150..150 %
    std::atomic<bool> mute, solo, soloSafe;
    std::atomic<int32_t> monitorBus; // list 7

    std::atomic<bool> stereo, mono;
    std::atomic<int32_t> inputLeft, inputRight;
    std::atomic<int32_t> outputLeft, outputRight;

    GroupMask dcaMask, muteGroupMask; // ABI 25: the DCAs and mute groups the strip is a member of

    // Matrices: DIRECT IN (manual 6.4). ABI 19: directSource is a
    // MatrixDirectInput (before, an input channel), no PRE.
    std::atomic<bool> directOn, directInvert;
    std::atomic<int32_t> directSource;
    std::atomic<float> directLevelDb;

    std::atomic<int32_t> colour; // WING colour 1..18 (ld_colour_rgb)
    std::atomic<int32_t> icon;
    std::atomic<bool> led;
    std::atomic<bool> recallSafe;

    // ALT input (WING INPUT SELECT; input and aux channels): used instead of
    // inputLeft/Right while ALT is selected (-1 = none; right used when stereo).
    std::atomic<int32_t> altInputLeft, altInputRight;
    std::atomic<bool> altSelected; // the strip's MAIN / ALT switch (/ch/n/in/set/altsrc)
    // ABI 22: INPUT SELECT GLOBAL, in GLOBAL INPUT SELECT ENABLE (/ch/n/in/set/srcauto):
    // the global MAIN / ALT switch (ld_engine_set_global_alt) and AUTO INPUT SELECT switch it.
    std::atomic<bool> srcAuto;

    // ABI 7, input channels: the signal TAP-mode sends take.
    std::atomic<float> tapWidth; // TAP WIDTH, -150..150 % (stereo channels)
    TapEqParams tapEq;           // TAP EQ

    // ABI 12, input and aux channels: LINK CUSTOMIZATION TO SOURCE (show the
    // source's name, colour and icon instead of the strip's own).
    std::atomic<bool> custLink;
};

struct StripMeters
{
    std::atomic<float> inputPeak[2];
    std::atomic<float> outputPeak[2];
    std::atomic<float> gateReductionDb, dynReductionDb;
    std::atomic<bool> gateActive, dynActive;
    std::atomic<int32_t> currentWidth;
    std::atomic<int32_t> dspFaults;
    std::atomic<float> automixDb;
    std::atomic<float> trimPeak[2]; // after trim/balance/polarity ("post TRIM")
    std::atomic<float> pflPeak[2];  // pre-fader, at the PFL point (METERS screen)
    // ABI 9: the GATE / DYNAMICS slot's detector level in the latest block, dB on
    // its static curve's input scale (the transfer graph's dot; -120 when off),
    // and the slots' output peaks (max since the last read, linear).
    std::atomic<float> gateKeyDb, dynKeyDb;
    std::atomic<float> gateOutPeak, dynOutPeak;
};

struct SendParams
{
    std::atomic<bool> enabled, panLink;
    std::atomic<bool> ignoreMute;  // IGNORE CHANNEL MUTE (input/aux channels, PRE/TAP mode only)
    std::atomic<bool> panReversed; // with panLink: LINK REVERSED (mirrored channel pan)
    std::atomic<int32_t> mode; // list 4: PRE (TAP), POST, GRP (GROUP: no send level)
    std::atomic<float> levelDb, pan;
    std::atomic<int32_t> tap; // ABI 27: a user tap of the source (16..47) to take, -1 = none
    float lastGain_[2]; // owned by the audio thread: do not touch
};

struct DcaParams
{
    std::atomic<float> faderDb;
    std::atomic<bool> mute, solo;
    std::atomic<bool> led; // scribble strip light
    std::atomic<int32_t> colour;
    std::atomic<int32_t> icon; // WING icon number (0 none)
    std::atomic<int32_t> mon;  // ABI 13, SOLO BUS (/dca/n/mon): 0 A (phones), 1 B (speakers), 2 A+B
};

struct FxParams
{
    std::atomic<bool> bypass;
    std::atomic<float> mix;
    /** Index into the FX table (kFxNone, kFxExt or a built-in device). */
    std::atomic<int32_t> model;
    std::atomic<float> values[kMaxFxParams];
    /** ABI 21: the hardware outputs fed by the slot's FX SEND (WING FX SEND 2n-1 / 2n; -1 none). */
    std::atomic<int32_t> sendLeft, sendRight;
    /** ABI 41: a send effect's sources (an X32's FX 1-4 SOURCE: strip signals,
        kStripSourceBase + n; -1 = an insert), returned on kFxReturnSourceBase. */
    std::atomic<int32_t> sourceLeft, sourceRight;
};

// ---- console level (ABI 7) ---------------------------------------------------

/** SOLO MODE (EngineControls::soloMode). */
enum SoloMode : int32_t
{
    kSoloLive = 0,   // each strip's SOLO BUS (monitorBus) chooses the path
    kSoloStudio = 1, // solos go to the selected path (EngineControls::soloBus)
    kSoloSip = 2     // as STUDIO, and inputs neither soloed nor solo safe are muted
};

/** A strip's MUTE as the surface shows it (ld_strip_mute_state). */
enum MuteState : int32_t
{
    kMuteOff = 0,
    kMuteOn = 1,         // its own MUTE
    kMuteGroup = 2,      // muted by a mute group, a DCA mute or SIP (the key flashes)
    kMuteOverridden = 3  // such a mute, unmuted by MUTEGROUP/SIP OVERRIDE
};

/** Talk key SWITCH MODE (TalkPathParams::mode). */
enum TalkMode : int32_t
{
    kTalkAuto = 0, // held = push, a short press latches
    kTalkPush = 1,
    kTalkLatch = 2
};

/** HOME > TAGS talk tags (ld_strip_talk_tags bits). */
enum TalkTag : uint32_t
{
    kTalkSolo = 1u << 0,
    kTalkOn = 1u << 1,
    kTalkASolo = 1u << 2,
    kTalkAOn = 1u << 3,
    kTalkBSolo = 1u << 4,
    kTalkBOn = 1u << 5
};

/** Source pairing (SourceParams::mode). */
enum SourceMode : int32_t
{
    kSourceMono = 0,
    kSourceStereo = 1,
    kSourceMidSide = 2
};

/** Sources: hardware ports 0..kNumHwSources-1, the playback channels, the USB
    PLAYER, the oscillators, then the USR group (ABI 19). */
constexpr int kNumHwSources = 576;
constexpr int kNumSources = kNumHwSources + kNumPlaybackChannels + kUsbChannels + kNumOscillators + kNumUsrSources;
/** ABI 19: hardware outputs the output patch can feed (ld_engine_output_patch). */
constexpr int kNumPatchOutputs = kNumHwSources;

/** ABI 19: a matrix's DIRECT IN (StripParams::directSource, /mtx/n/dir/in). */
enum MatrixDirectInput : int32_t
{
    kDirectOff = 0,
    kDirectAes = 1,      // the AES/EBU input: not on Sidecar (silent)
    kDirectPhones = 2,   // MON.PH
    kDirectSpeakers = 3, // MON.SPK
    kDirectSoloBus = 4,  // MON.BUS
    kNumMatrixDirectInputs = 5
};

/** ABI 19, a user signal (USR 1-24; ld_engine_user_signals): a mono copy of a
    strip. group: 0 OFF, 1 CH, 2 AUX, 3 BUS, 4 MAIN, 5 MTX; number 1-based (a
    bus by its number); side 0 L+R, 1 L, 2 R; post: POST (else PRE, the tap point). */
struct UserSignalParams
{
    std::atomic<int32_t> group, number, side;
    std::atomic<bool> post;
};

/** ABI 19, a user patch (USR 25-56; ld_engine_user_patches): the external
    source it copies (a port, playback channel or USB PLAYER input), -1 OFF. */
struct UserPatchParams
{
    std::atomic<int32_t> source;
};
/** OscParams::mode. */
enum OscMode : int32_t
{
    kOscSine = 0,
    kOscPink = 1,
    kOscWhite = 2
};
/** Monitor SOURCE choices: MAIN.1-4, MTX.1-8, BUS.1-16, AUX.1-8. */
constexpr int kNumMonitorSources = kWingMains + kWingMatrices + kWingBuses + kWingAux;
/** TalkParams::assignKind without a talkback channel. */
constexpr int32_t kTalkOff = -1;

/** One talk path (TALK A or TALK B). */
struct TalkPathParams
{
    std::atomic<bool> on;         // the key is on (the host applies AUTO / PUSH / LATCH)
    std::atomic<bool> individual; // USE SEND LEVELS: the talkback channel's send levels
    std::atomic<int32_t> mode;    // TalkMode
    std::atomic<float> monDimDb, busDimDb; // AUTO DIM (attenuations, dB)
    std::atomic<uint32_t> buses, matrices, mains; // TALK TO BUS 1-16 / MATRIX 1-8 / MAIN 1-4 (bit n = n+1)
};

/** Talkback: the talkback channel (kTalkOff, or kChannel / kAux and its 0-based
    number: CH40 = (kChannel, 39), AUX8 = (kAux, 7)), TALK LEVEL and the paths. */
struct TalkParams
{
    std::atomic<int32_t> assignKind, assignIndex;
    std::atomic<float> levelDb;
    TalkPathParams paths[2];
};

/** A monitor path's SOURCE (0..kNumMonitorSources-1), SRC LEVEL, SRC MIX
    (the source's level while a solo reaches the path; -144 = off) and PFL DIM. */
struct MonitorSourceParams
{
    std::atomic<int32_t> source;
    std::atomic<float> sourceLevelDb, sourceMixDb, pflDimDb;
};

/** The PHONES monitor path. The SPEAKERS path is monitorLevelDb, monitorDimDb,
    monitorOutputLeft/Right, monitorPeak plus EngineControls::speakers. */
struct MonitorPathParams
{
    MonitorSourceParams source;
    std::atomic<float> levelDb, dimDb;
    std::atomic<int32_t> outputLeft, outputRight;
    std::atomic<float> peak[2];
};

/** A source (ROUTING > SOURCES): gain, mute, polarity, +48 V (stored only),
    pairing with its neighbour (a pair shares the first source's settings),
    colour and icon, and its peak since the last read. ld_engine_sources. */
struct SourceParams
{
    std::atomic<float> gainDb;
    std::atomic<bool> mute, invert, phantom;
    std::atomic<int32_t> mode; // SourceMode
    std::atomic<int32_t> colour, icon;
    std::atomic<float> peak;
    GroupMask muteGroups; // ABI 8, SOURCES > TAGS: the mute groups that mute the source (ABI 25: a group set)
};

/** An oscillator (the OSC source group; ld_engine_oscillators): MODE (OscMode),
    LEVEL in dBFS (-40 .. -6: a sine's peak; the noises have its RMS) and FREQ in
    Hz (20 .. 20000). On / off is its source's mute; a STEREO / MID-SIDE OSC 1 / 2
    pair runs on OSC 1's settings. */
struct OscParams
{
    std::atomic<int32_t> mode;
    std::atomic<float> levelDb, freqHz;
};

// ---- monitor path processing, SOURCE SOLO (ABI 8; livedaw-engine monitor.rs) ----

/** Monitor EQ bands: L (low shelf), 1-6 (PEQ), H (high shelf). */
constexpr int kMonitorEqBands = 8;
/** DIRECT IN choices: CH.1-40, AUX.1-8, BUS.1-16, MAIN.1-4, MTX.1-8 (-1 = OFF). */
constexpr int kNumDirectSources = kWingChannels + kWingAux + kWingBuses + kWingMains + kWingMatrices;

/** One monitor EQ band (the shelves have no Q). */
struct MonitorEqBand
{
    std::atomic<float> gainDb; // -15..15
    std::atomic<float> freq;   // L 20..2000, 1-6 20..20000, H 200..20000 Hz
    std::atomic<float> q;      // 0.44..10
};

/** A monitor path's processing (MONITORS > PHONES / SPEAKERS SETTINGS, the
    MONITORS EQ and LEVEL): solo bus + SOURCE + DIRECT IN -> EQ -> PAN / WIDTH
    -> INVERT -> DELAY -> level, DIM, MUTE, MONO / SWAP -> LIMITER. */
struct MonitorDspParams
{
    std::atomic<int32_t> directSource; // DIRECT IN (0..kNumDirectSources-1, -1 = OFF), ld_monitor_direct_name
    std::atomic<float> directLevelDb;  // its level
    std::atomic<float> bandSoloDb;     // BAND SOLO boost of an EQ band LISTEN on this path, 0..24 (default 0)
    std::atomic<bool> eqOn, invert, delayOn;
    MonitorEqBand eq[kMonitorEqBands];
    std::atomic<float> pan;         // -100..100
    std::atomic<float> width;       // -150..150 %
    std::atomic<float> delayM;      // DIST 0.1..100 m (ld_monitor_delay_ms)
    std::atomic<float> limiterDb;   // LIMITER threshold -40..0 dB
    std::atomic<float> limiterGrDb; // meter: largest gain reduction since the last read (exchange with 0)
};

/** SOURCE SOLO (MonitorControls::sourceSolo): the channel hosting a soloed source. */
enum SourceSoloHost : int32_t
{
    kSourceSoloOff = 0,
    kSourceSoloCh39 = 1,
    kSourceSoloAux7 = 2
};

/** EngineControls::monitor: both paths' processing and SOURCE SOLO. */
struct MonitorControls
{
    MonitorDspParams paths[2];             // 0 = SPEAKERS, 1 = PHONES
    std::atomic<int32_t> sourceSolo;       // SourceSoloHost (SETUP > AUDIO)
    std::atomic<bool> sourceSoloOn;        // SRC SOLO on ROUTING > SOURCES (not saved)
    std::atomic<int32_t> sourceSoloPort;   // the soloed source's input port
};

struct EngineControls
{
    std::atomic<bool> soloAfl; // CHANNEL SOLO (input and aux channels): AFL
    std::atomic<bool> anySoloActive;
    std::atomic<bool> outputProtection;
    std::atomic<float> monitorLevelDb;
    std::atomic<int32_t> monitorOutputLeft, monitorOutputRight;
    std::atomic<float> outputCeilingDb;
    std::atomic<int32_t> outputClipEvents;
    std::atomic<float> monitorPeak[2];
    std::atomic<float> dspLoad;
    std::atomic<int32_t> engineFaults;
    GroupMask muteGroups; // ABI 25: the engaged mute groups
    DcaParams dcas[kMaxDcas]; // ABI 25: the console uses the first ld_engine_counts().dcas
    FxParams fx[kMaxFx];

    std::atomic<bool> busSoloAfl, mainSoloAfl, matrixSoloAfl; // BUS / MAIN / MATRIX SOLO: AFL (default)
    std::atomic<bool> monitorDim, monitorMute, monitorMono, monitorSwap;
    std::atomic<bool> dcaMuteGroups;   // DCA MUTEGROUPS (default on)
    std::atomic<bool> startupMainMute; // STARTUP MAIN MUTE (ld_engine_apply_startup_main_mute)
    std::atomic<bool> automixEnable[2]; // X ENABLE, Y ENABLE
    std::atomic<float> monitorDimDb;    // DIM amount (attenuation in dB, default 20)
    std::atomic<int32_t> mainLink;      // MainLinkMode
    // INPUT SELECT. GLOBAL INPUT SELECT OVERRIDE (/io/autoaltovr): a GLOBAL
    // strip's own MAIN / ALT switch still works. globalAlt: the global MAIN /
    // ALT switch (/io/altsw); set it with ld_engine_set_global_alt.
    std::atomic<bool> inputSelectOverride;
    std::atomic<bool> globalAlt;

    // ABI 7: console level
    std::atomic<int32_t> soloMode; // SoloMode
    /** The solo bus / selected monitoring path, as list 7 (0 = A PHONES,
        1 = B SPEAKERS, 2 = A+B): STUDIO and SIP solo there; DIM, MUTE, MONO,
        SWAP and the talk MON DIM act on it. */
    std::atomic<int32_t> soloBus;
    std::atomic<bool> soloBusOverride; // LIVE: soloBus overrides the strips until no solo is left
    std::atomic<bool> muteOverride;    // MUTEGROUP/SIP OVERRIDE
    std::atomic<bool> sipActive;       // status: SIP is muting (the SIP badge)
    TalkParams talk;
    MonitorSourceParams speakers; // the SPEAKERS path's SOURCE etc.
    MonitorPathParams phones;     // the PHONES path

    // ABI 8: monitor path processing and SOURCE SOLO
    MonitorControls monitor;
};

/// Where the RTA shows (AnalysisControls::view): its tap and settings follow.
enum RtaView : int32_t
{
    kRtaViewHome = 0,  // HOME > OVERVIEW: RTA SETTINGS (source IN | EQ | POST)
    kRtaViewEq = 1,    // EQ screen: EQ/RTA SETTINGS (source PRE | POST the EQ)
    kRtaViewFilter = 2, // FILTER SETUP: source PRE | POST the filter (filterTap), the EQ view's settings
    kRtaViewMonitor = 3 // MONITORS EQ: a monitor path (kAnalyseMonitor), PRE | POST its EQ, the EQ view's settings
};

/// AnalysisControls::stripKind for the RTA of a monitor path: stripId is the
/// path (0 SPEAKERS, 1 PHONES, as MonitorControls::paths).
constexpr int32_t kAnalyseMonitor = -2;

/// LISTEN (AnalysisControls::listenMode): replaces the solo bus, one strip at a time.
enum ListenMode : int32_t
{
    kListenOff = 0,
    kListenBand = 1,    // BAND SOLO / LISTEN: the selected WING EQ band (listenBand)
    kListenGateKey = 2, // KEY SOLO of the GATE slot (after the key filter)
    kListenDynKey = 3,  // KEY SOLO of the DYNAMICS slot
    kListenXover = 4    // XOVER SOLO: the DYNAMICS slot's crossover band
};

/// RTA SETTINGS of one view (the WING's cfg/rta nodes).
struct RtaViewSettings
{
    std::atomic<int32_t> display;  // SIZE/MODE. HOME: OFF, 1/3, FULL. EQ: OFF, 1/4, 1/3, 1/2, OVL/3, OVL
    std::atomic<int32_t> colour;   // COLOR: RD25, RD50, RD75, AM25, AM50, AM75, BL25, BL50, BL75
    std::atomic<int32_t> tap;      // RTA SOURCE. HOME: IN, EQ, POST. EQ: PRE, POST
    std::atomic<int32_t> range;    // RTA RANGE: 30 dB, 60 dB
    std::atomic<int32_t> decay;    // RTA DECAY: SLOW, MED, FAST
    std::atomic<int32_t> detector; // RTA DETECTOR: PEAK, RMS, AVG
    std::atomic<bool> autogain;    // RTA AUTOGAIN (applied by the UI)
    std::atomic<float> gainDb;     // FIXED GAIN, -5..50 dB
};

/// RTA, LR vector scope and LISTEN (ld_engine_analysis). Strips are named by
/// kind and id (Strip::getId()); -1 = none.
struct AnalysisControls
{
    std::atomic<int32_t> stripKind, stripId; // the analysed (selected) strip
    std::atomic<bool> rtaOn;                 // an RTA is shown (ld_rta_read starts the analysis thread)
    std::atomic<bool> rtaFreeze;             // FREEZE
    std::atomic<bool> scopeOn;               // the LR VECTOR SCOPE is shown (ld_scope_read)
    std::atomic<int32_t> view;               // RtaView
    std::atomic<int32_t> listenMode;         // ListenMode
    std::atomic<int32_t> listenKind, listenId;
    std::atomic<int32_t> listenBand;         // index into the WING EQ's bands (l, 1..n, h)
    std::atomic<float> bandSoloTrimDb;       // unused since ABI 8: MonitorDspParams::bandSoloDb per path
    RtaViewSettings home, eq;
    std::atomic<int32_t> filterTap;          // FILTER SETUP's RTA SOURCE: PRE, POST
};

struct LayoutInfo
{
    int32_t abiVersion, stripParams, input, filter, dynSlot, eqBlock, insert, mainSend, matrixSend, meters, send, dca,
        fx, controls, offFilter, offGate, offEq, offDynamics, offPostInsert, offFader, offStereo, offMainSendLevel, offMatrixSendPan,
        offDcaMask, offDirectLevel, offColour, offRecallSafe, offDynValues, offDynXoDepth, offEqValues, offSendPan,
        offMetersAutomix, offControlsDcas, offControlsFx, offFxValues, offSendMode, offMatrixSendLevel,
        offMetersTrimPeak, offMetersPflPeak, offControlsBusSoloAfl, offControlsAutomixEnable, offControlsDimDb,
        offControlsMainLink,
        // ABI 6
        offAltInputLeft, offAltSelected, offControlsInputSelectOverride, offControlsGlobalAlt, recClip,
        recPlaybackTrack, recRecordTrack, recTake,
        // ABI 7
        offTapWidth, offTapEq, tapEq, analysis, offAnalysisEq, offAnalysisFilterTap,
        talk, talkPath, monitorSource, monitorPath, source, offControlsSoloMode, offControlsTalk, offControlsSpeakers,
        offControlsPhones, offTalkPaths, offMonitorPathPeak, offSourcePeak,
        // ABI 8 (monitors)
        monitorDsp, monitorControls, offControlsMonitor, offMonitorDspEq, offMonitorDspPan, offMonitorControlsSourceSolo,
        offSourceMuteGroups,
        // ABI 11 (matrix send PAN LINK, DCA icon and light)
        offMatrixSendPanReversed, offDcaIcon,
        // ABI 12 (LINK CUSTOMIZATION TO SOURCE)
        offCustLink,
        // ABI 13 (DCA SOLO BUS)
        offDcaMon,
        // ABI 16 (the console model)
        consoleModelInfo,
        // ABI 19 (routing: matrix DIRECT IN, USR sources)
        offDirectSource, userSignal, offUserSignalPost, userPatch,
        // ABI 20 (FaderRefC.flags, kFaderSelSend)
        faderRef,
        // ABI 21 (FX SEND outputs)
        offFxSendLeft,
        // ABI 22 (GLOBAL INPUT SELECT ENABLE)
        offSrcAuto,
        // ABI 25 (group sets, the console's counts)
        groupSet, offMuteGroupMask, scope, offScopeFx, counts,
        // ABI 28 (a strip's chain)
        chainStep, block, offBlockMeters, blockMeters, offBlockMetersOutPeak, delayBlock;
};

/** ABI 25: how many of each kind a console has (ld_engine_counts, ld_engine_configure). */
struct CountsC
{
    int32_t channels, aux, buses, mains, matrices, dcas, muteGroups, fx;
};

/** ABI 31: a fader law (ld_engine_fader_law): where a level sits on a fader's travel,
    as straight segments between (position, dB) breakpoints, rising. Below the first
    point is silence; a stepped fader (steps > 0) lands on its steps. */
constexpr int kMaxFaderLawPoints = 8;
struct FaderLawC
{
    int32_t numPoints, steps;
    float position[kMaxFaderLawPoints];
    float db[kMaxFaderLawPoints];
};
static_assert (sizeof (FaderLawC) == 72);

/** ABI 35: a profile's slot range (ld_profile_slot_ranges): strips of `kind`
    (StripKindId) from slot `first` (0-based) for `count` run their own chain and go by
    `label` (NUL-terminated), numbered from 1 again: the X32's FX returns, aux 9-16. */
struct SlotRangeC
{
    int32_t kind, first, count;
    char label[8];
};
static_assert (sizeof (SlotRangeC) == 20);

/** ABI 33: the remote protocols a console speaks (ld_engine_remotes): its MIDI
    protocol (-1 none, 0 the WING's, 1 the X32's, 2 the ProMix 01's: MidiProtocol::Id) and its network protocols, a set
    of bits (1 WING discovery, 2 WING OSC, 4 WING native: the WING remote's servers). */
struct RemotesC
{
    int32_t midi;
    uint32_t network;
};
static_assert (sizeof (RemotesC) == 8);

/** ABI 31: a port group (ld_engine_port_groups): a console's sources (input), output
    ports (output) or both as its routing pages group them, a range of the engine's
    numbering: port n is source and output port first + n (n < maxSize; the console has `size`; the
    local ports grow with the interface). first = -1: the engine has no such ports
    (n/a tiles, or the app's own groups). Names are NUL-terminated. */
struct PortGroupC
{
    char name[24];
    char prefix[8];
    int32_t input, output, first, size, maxSize;
};
static_assert (sizeof (PortGroupC) == 52);

/** A console model's surface and local I/O (ld_console_model_info). Models:
    0 the full-size WING, 1 the WING COMPACT (2 is kept for the RACK). */
struct ConsoleModelInfo
{
    int32_t faders;       // motor faders (the COMPACT's fader 13 included)
    int32_t layerKeys;    // layer keys of the (left) fader section
    int32_t localPreamps; // local MIDAS PRO preamps
    int32_t localOutputs;
    int32_t auxInputs, auxOutputs;
    int32_t gpios;
};

struct ParamInfo
{
    int32_t kind; // 0 linear, 1 log, 2 choice, 3 values, 4 toggle, 5 integer
    float min, max;
    int32_t steps;
    float defaultValue;
    int32_t numChoices;
};

/// Processes numChannels (1 or 2) channels in place; return 0 on success.
using InsertProcessFn = int32_t (*) (void* ctx, float* const* channels, int32_t numChannels, int32_t numSamples);
/// Called on the message thread once the engine can no longer call process.
using InsertReleaseFn = void (*) (void* ctx);

struct InsertCallbacks
{
    InsertProcessFn process;
    InsertReleaseFn release;
    void* ctx;
};

struct Engine;    // opaque
struct Strip;     // opaque
struct LdString;  // opaque owned UTF-8 string: ld_string_data/len, free with ld_string_free
struct LdSink;    // opaque string the host appends to (ld_sink_append) from callbacks
struct Show;      // opaque
struct LayerSet;  // opaque
struct LayerList; // opaque
struct LdAudioWriter; // opaque: an audio file being written (ld_afw_*)
struct LdAudioReader; // opaque: an audio file being read (ld_afr_*)

/** Audio file formats (recorders, export): FileFormat codes. */
enum AudioFileFormat : int32_t
{
    kFormatWav = 0,
    kFormatFlac = 1,
    kFormatAiff = 2,
    kFormatMp3 = 3
};
struct LdWing;    // opaque WING remote (servers + snapshot files), see ld_wing_*
struct LdX32;     // ABI 38: opaque X32 OSC server, see ld_x32_*
struct LdProMix;  // ABI 44: opaque ProMix 01 state and MIDI, see ld_promix_*
struct LdWeb;     // opaque web remote (HTTP + WebSocket screen mirror), see ld_web_*

/** An input event from a browser (ld_web_take_input). */
struct LdWebInput
{
    int32_t kind;   // 0 move, 1 down, 2 up, 3 leave, 4 wheel, 5 key down, 6 key up
    float x, y;     // fractions of the screen (0..1)
    float dx, dy;   // wheel deltas (JUCE units)
    int32_t buttons; // held after the event: 1 left, 2 right, 4 middle
    int32_t key;    // a character, or a negative special key (WebRemote::Key)
    uint32_t mods;  // 1 shift, 2 ctrl, 4 alt, 8 meta
    uint32_t client;
};

struct StripRefC
{
    int32_t kind, id;
};

/// A fader on the surface: kind 0..4 = a strip, or with toKind 2..4 the send
/// level from it to bus / main / matrix `toId`; kFaderDca = DCA (id 0..15);
/// kFaderFxParam = FX slot `id`'s parameter `toId`; kFaderMidiCc = CC `toId`
/// (0-127) on MIDI channel `id` (0-15); kFaderEmpty = a blank slot of a
/// layer (ABI 14); kFaderSelSend = the selected channel's send to `toKind` /
/// `toId` (ABI 20). toKind is -1 when unused. `flags` kFaderBusCustom: a send
/// fader shows its destination's name, colour and icon (ABI 20).
struct FaderRefC
{
    int32_t kind, id;
    int32_t toKind = -1, toId = 0;
    int32_t flags = 0;
};

/// A spill: kind 0 = DCA `index`, 1 = the sources sending to bus slot `index`,
/// 2 = FX slot `index`'s parameters, 3 = automix group `index` (0 X, 1 Y),
/// 4 = the sends of `strip`, 5 = the strips feeding main `index` (ABI 17),
/// 6 = the strips sending to matrix `index` (ABI 22).
struct SpillC
{
    int32_t kind, index;
    StripRefC strip;
};

/// A LIBRARY preset file's header: kind 0 = not a preset, 1 = FX, 2 = channel;
/// `name` and `detail` (the FX device or the strip kind) are the caller's to free.
struct PresetInfoC
{
    int32_t kind;
    uint32_t scope;
    int32_t stripKind;
    LdString* name;
    LdString* detail;
};

/// A LIBRARY scene / snippet file's header: kind 0 = not one, 1 = scene (.lsnap),
/// 2 = snippet (.lsnip); count = a scene's snippets or a snippet's parameters;
/// `name` is the caller's to free.
struct LibraryInfoC
{
    int32_t kind, count;
    LdString* name;
};

struct ApplyResultC
{
    int32_t stripsApplied, stripsSkippedSafe, stripsMissing;
    LdString* warnings; // '\n'-separated, or null; caller frees
};

/// A recall scope: strip sections, strips (allStrips: every strip), DCAs, mute
/// groups and FX slots (bit n = item n+1) and the console-wide settings. Going
/// in, `strips` points at `numStrips` refs; coming out it is null and the
/// strips go to a separate buffer.
struct ScopeC
{
    uint32_t sections;
    GroupBits dcas, muteGroups; // ABI 25: group sets
    uint64_t fx;
    bool console, allStrips;
    const StripRefC* strips;
    int32_t numStrips;
};

/** Writes the <PLUGIN> description XML and base64 state of FX slot `slot`; false if empty. */
using CaptureFxFn = bool (*) (void* ctx, int32_t slot, LdSink* description, LdSink* state);
/** mode 0 = restore state into the plugin already loaded, 1 = create the plugin.
    Returns 0 ok, 1 different plugin loaded (mode 0), 2 failed (message in `error`). */
using RestoreFxFn = int32_t (*) (void* ctx, int32_t slot, int32_t mode, const char* description, size_t descriptionLen,
                                 const char* state, size_t stateLen, LdSink* error);
/** Empties FX slot `slot` (show load). */
using ClearFxFn = void (*) (void* ctx, int32_t slot);

struct PluginBridge
{
    void* ctx;
    CaptureFxFn capture;
    RestoreFxFn restore;
    ClearFxFn clear;
};

// Recorder (ld_rec_*)

/** A clip of the playlist: file frame fileOffset plays at timeline `start`. */
struct RecClipC
{
    const char* path;
    size_t pathLen;
    int64_t fileOffset, start, length, fadeIn, fadeOut;
    float gain;
    int32_t fadeInShape, fadeOutShape; // 0 Linear, 1 EqualPower, 2 SCurve
};

/** A playback track; its clips are clips[firstClip .. firstClip + numClips). */
struct RecPlaybackTrackC
{
    int32_t channel, numChannels;
    float gain;
    bool mute;
    int32_t firstClip, numClips;
};

struct RecRecordTrackC
{
    int32_t track;
    int32_t sourceKind; // 0 hardware input (port), 1 strip pre-fader, 2 strip post-fader
    int32_t port;
    int32_t stripKind, stripNumber; // as shown: CH 3 = (kChannel, 3), BUS 12 = (kBus, 12)
    int32_t numChannels;
    const char* path;
    size_t pathLen;
};

/** A finished take; free `path` with ld_string_free. */
struct RecTakeC
{
    int32_t track, numChannels;
    int64_t start, length;
    LdString* path;
};

extern "C"
{
    void ld_layout_info (LayoutInfo* out);

    Engine* ld_engine_create (int32_t numWorkers);
    void ld_engine_destroy (Engine*);
    int32_t ld_engine_num_workers (const Engine*);
    void ld_engine_run_worker (const Engine*, int32_t index);
    void ld_engine_shutdown_workers (const Engine*);

    void ld_engine_prepare (const Engine*, double sampleRate, int32_t blockSize,
                            const int32_t* inMap, int32_t numHwInputs, const int32_t* outMap, int32_t numHwOutputs);
    void ld_engine_set_device_running (const Engine*, bool running);
    void ld_engine_process (const Engine*, const float* const* ins, int32_t numIns,
                            float* const* outs, int32_t numOuts, int32_t numSamples);
    double ld_engine_sample_rate (const Engine*);
    int32_t ld_engine_block_size (const Engine*);
    EngineControls* ld_engine_controls (const Engine*);
    /** Once at start-up: STARTUP MAIN MUTE mutes every main if the preference is on. */
    bool ld_engine_apply_startup_main_mute (const Engine*);

    void ld_engine_collect (const Engine*);
    void ld_engine_collect_all_now (const Engine*);
    int32_t ld_engine_pending_garbage (const Engine*);

    // Structure (message thread)
    int32_t ld_engine_num_strips (const Engine*, int32_t kind);
    Strip* ld_engine_strip_at (const Engine*, int32_t kind, int32_t index);
    Strip* ld_engine_add_channel (const Engine*, int32_t requestedId);
    void ld_engine_set_num_channels (const Engine*, int32_t count);
    /** ABI 25: what the console has now (strips by kind; DCAs, mute groups and FX slots in use). */
    void ld_engine_counts (const Engine*, CountsC* out);
    /** The counts the console was configured with (the WING's by default). */
    void ld_engine_configured_counts (const Engine*, CountsC* out);
    /** Makes the engine a fresh console of `counts` (null: the WING): a WING if they are
        the WING's, else the Universal. */
    void ld_engine_configure (const Engine*, const CountsC* counts);
    /** ABI 30: the console emulated (ProfileId). It is the Universal once the counts leave
        the profile's (ld_engine_set_num_strips) or a show of another console is loaded. */
    int32_t ld_engine_profile (const Engine*);
    /** ABI 35: makes the engine a fresh console of `profile` (ProfileId; an unknown one:
        the WING) with that console's counts. */
    void ld_engine_configure_profile (const Engine*, int32_t profile);
    /** ABI 37: the model calls (ld_model_*) describe `profile`'s models (ProfileId; the
        console the app shows; an unknown one: the WING's). */
    void ld_models_show_profile (int32_t profile);
    /** ABI 31: the console's fader law (its profile's; the WING's for the Universal). */
    void ld_engine_fader_law (const Engine*, FaderLawC* out);
    /** ABI 45: the fader law of a strip of `kind` (StripKindId): the 02R's STEREO
        has its own (0 to -120 dB); else ld_engine_fader_law's. */
    void ld_engine_fader_law_of (const Engine*, int32_t kind, FaderLawC* out);
    /** ABI 31: the console's port groups (its profile's), in its menus' order: writes up
        to `max` to `out` (may be null) and returns how many there are. */
    int32_t ld_engine_port_groups (const Engine*, PortGroupC* out, int32_t max);
    /** ABI 34: a profile's port groups (ProfileId; an unknown one: the WING's), as
        ld_engine_port_groups writes them, without an engine. */
    int32_t ld_profile_port_groups (int32_t profile, PortGroupC* out, int32_t max);
    /** ABI 34: a profile's colours (ProfileId; an unknown one: the WING's): how many,
        colour `index` (1-based) as 0xRRGGBB (grey for one it hasn't) and its name
        (free with ld_string_free), and the index at place `position` of its colour
        picker (0 outside it). */
    int32_t ld_profile_colour_count (int32_t profile);
    uint32_t ld_profile_colour_rgb (int32_t profile, int32_t index);
    LdString* ld_profile_colour_name (int32_t profile, int32_t index);
    int32_t ld_profile_colour_at_grid (int32_t profile, int32_t position);
    /** ABI 34: what a profile calls a strip of `kind` (StripKindId), before its number
        ("CH" for "CH 1"). Free with ld_string_free. */
    LdString* ld_profile_strip_label (int32_t profile, int32_t kind);
    /** ABI 35: a profile's slot ranges: writes up to `max` to `out` (may be null) and
        returns how many there are. */
    int32_t ld_profile_slot_ranges (int32_t profile, SlotRangeC* out, int32_t max);
    /** ABI 33: the console's remote protocols (its profile's): the app runs these only. */
    void ld_engine_remotes (const Engine*, RemotesC* out);
    /** ABI 28: makes the engine a fresh console of the structure of `counts` (null: the
        WING's) without its channels or FX slot count, as a show load does before it adds
        the channels; writes the counts configured (within what the engine holds) to `out`
        (may be null). */
    void ld_engine_set_config (const Engine*, const CountsC* counts, CountsC* out);
    /** ABI 29: the number of aux channels, buses, mains or matrices (`kind`, StripKindId),
        changed without a reset: new strips at the end; strips removed from the end, but
        never a bus, main or matrix something feeds (nor those before it). Returns the
        number set (kChannel: the channels, unchanged: ld_engine_set_num_channels). */
    int32_t ld_engine_set_num_strips (const Engine*, int32_t kind, int32_t count);
    /** Sets the DCAs in use (up to kMaxDcas, never below a DCA with a member); returns the number set. */
    int32_t ld_engine_set_num_dcas (const Engine*, int32_t count);
    /** Sets the mute groups in use (up to kMaxMuteGroups, never below one engaged or with a member). */
    int32_t ld_engine_set_num_mute_groups (const Engine*, int32_t count);
    /** The FX slots in use (kWingFx to kMaxFx). */
    int32_t ld_engine_num_fx (const Engine*);
    /** Sets the FX slots in use (clamped to kWingFx..kMaxFx and never below a slot
        in use, ld_engine_fx_in_use); returns the number set. */
    int32_t ld_engine_set_num_fx (const Engine*, int32_t count);
    /** The FX slot holds something (any model but NONE) or a strip's insert is assigned to it. */
    bool ld_engine_fx_in_use (const Engine*, int32_t slot);
    /** Adds a bus beyond the console's 16 (null if no slot is free). */
    Strip* ld_engine_add_bus (const Engine*, int32_t requestedSlot);
    Strip* ld_engine_bus_in_slot (const Engine*, int32_t slot);
    /** Removes a channel or an added bus (the console's 16 buses stay). */
    bool ld_engine_remove_strip (const Engine*, const Strip*);
    void ld_engine_reset (const Engine*);
    int32_t ld_engine_total_dsp_faults (const Engine*);
    int32_t ld_engine_num_faulted_fx (const Engine*);
    /** group: 0 = DCA, 1 = mute group. */
    LdString* ld_engine_group_name (const Engine*, int32_t group, int32_t index);
    void ld_engine_set_group_name (const Engine*, int32_t group, int32_t index, const char* utf8, size_t len);

    // FX slots (message thread)
    void ld_fx_set (const Engine*, int32_t slot, const InsertCallbacks* callbacks);
    /** As ld_fx_set with a processor holding the same plugins (a chain rearranged): a faulted slot stays faulted. */
    void ld_fx_replace (const Engine*, int32_t slot, const InsertCallbacks* callbacks);
    void* ld_fx_ctx (const Engine*, int32_t slot);
    /** what: 0 = a plugin is loaded, 1 = faulted (auto-bypassed), 2 = double-assigned,
        3 = a built-in device is running, 4 = an outboard insert (EXT, SOURCE OUTBOARD,
        no plugin), 5 = an outboard insert with a RETURN. */
    bool ld_fx_state (const Engine*, int32_t slot, int32_t what);
    /** Selects a slot's device (FX table index), creating it now; `defaults` resets its
        settings. False if not allowed there (premium devices need slots 1-8). */
    bool ld_fx_set_model (const Engine*, int32_t slot, int32_t model, bool defaults);
    /** Gain reduction of a slot's device in dB (<= 0). */
    float ld_fx_meter (const Engine*, int32_t slot);
    /** Live gain of a dynamic band of a slot's device (TRIPLE DEQ bands 0-2),
        dB, signed (cut < 0, boost > 0); 0 for other devices. */
    float ld_fx_band_gain (const Engine*, int32_t slot, int32_t band);
    /** Any thread (ABI 23): the slot's live meters for its device's screen into `out`
        (up to `n`; returns how many): kFxLevels levels (input L, input R, output L,
        output R: what its device or plugin gets and gives back before FX MIX, or the
        outboard insert's FX SEND after TRIM and its RETURN; linear peaks falling at
        60 dB/s), then kFxReadouts device readouts (FxAlgo::readouts, laid out per
        device: gain reductions in dB, band levels, a scope...; 0 = idle). */
    int32_t ld_fx_readouts (const Engine*, int32_t slot, float* out, int32_t n);
    void ld_fx_clear_flags (const Engine*, int32_t slot);
    float ld_fx_load (const Engine*, int32_t slot);
    int32_t ld_fx_num_users (const Engine*, int32_t slot);

    // Plugin delay compensation (ABI 10): every bus, main and matrix reads its
    // sources delayed so that paths through plugins of different latencies
    // arrive together (capped at kMaxPdc samples).
    /** Any thread: the latency (samples) slot `slot`'s plugin reports; reset by ld_fx_set. */
    void ld_fx_set_latency (const Engine*, int32_t slot, int32_t samples);
    /** The slot's latency: its plugin's, its outboard insert's round trip plus LATENCY, or
        (ABI 23) its built-in device's (the PRECISION LIMITER's look-ahead). */
    int32_t ld_fx_latency (const Engine*, int32_t slot);
    /** Any thread (ABI 21): the interface's reported input + output latency in samples
        (an outboard insert's round trip is one block plus this). */
    void ld_engine_set_io_latency (const Engine*, int32_t samples);
    /** Recall safes (ABI 21, GLOBAL SAFE): kind 0 = a source (its table index), 1 = a
        hardware output port; index -1 asks whether any of the kind is safe. */
    bool ld_engine_io_safe (const Engine*, int32_t kind, int32_t index);
    void ld_engine_set_io_safe (const Engine*, int32_t kind, int32_t index, bool safe);
    /** DELAY COMPENSATION on / off (default on). */
    void ld_engine_set_pdc (const Engine*, bool on);
    bool ld_engine_pdc (const Engine*);
    /** Message thread: works out the plan now (ld_engine_collect also does). */
    void ld_engine_update_pdc (const Engine*);
    /** The longest path latency (samples) the plan aligns to. */
    int32_t ld_engine_pdc_total (const Engine*);
    /** A path needs more than kMaxPdc samples of compensation. */
    bool ld_engine_pdc_limited (const Engine*);
    /** The plugin latency of a strip's output (its inserts plus what its sources were aligned to). */
    int32_t ld_strip_latency (const Strip*);

    // Strips
    int32_t ld_strip_kind (const Strip*);
    int32_t ld_strip_id (const Strip*);
    StripParams* ld_strip_params (const Strip*);
    StripMeters* ld_strip_meters (const Strip*);
    /** Live gain of band 0-1 of the GATE (gate) or DYNAMICS slot's dynamic EQ
        (DEQ, DEQ2), dB, signed (cut < 0, boost > 0); 0 for other models. */
    float ld_strip_dyn_band_gain (const Strip*, bool gate, int32_t band);
    int32_t ld_strip_num_channels (const Strip*);
    bool ld_strip_is_idle (const Strip*);
    SendParams* ld_strip_send (const Strip*, int32_t slot);
    SendParams* ld_strip_find_send (const Strip*, int32_t slot);
    /** ABI 26: the strip's send to main / matrix `index` (created if needed; null past 256). */
    MainSendParams* ld_strip_main_send (const Strip*, int32_t index);
    MatrixSendParams* ld_strip_matrix_send (const Strip*, int32_t index);
    LdString* ld_strip_name (const Strip*);
    void ld_strip_set_name (const Strip*, const char* utf8, size_t len);
    /** HOME > TAGS: the strip's free tags, comma-separated. */
    LdString* ld_strip_tags (const Strip*);
    void ld_strip_set_tags (const Strip*, const char* utf8, size_t len);
    /** block: 0 = gate slot, 1 = EQ, 2 = dynamics slot, 3 = filter tool. Loads the model's defaults. */
    void ld_strip_set_model (const Strip*, int32_t block, int32_t model);
    /** Response in dB of the EQ (block 1), FILTER (block 3), TAP EQ (block 4), the GATE key filter (5),
        the DYNAMICS key filter (6) or the DYNAMICS crossover (7) at `count` frequencies. */
    void ld_strip_response (const Strip*, int32_t block, const double* freqs, double* outDb, int32_t count, double sampleRate);

    // ABI 28: a strip's chain (message thread); `at` is a step index in processing order.
    /** The steps: the first `max` into `out` (may be null); returns how many there are. */
    int32_t ld_strip_chain (const Strip*, ChainStepC* out, int32_t max);
    /** Extra block `uid` into `out`; false if there's none. */
    bool ld_strip_block (const Strip*, int32_t uid, BlockC* out);
    /** Selects a model for block `uid` and loads its defaults: an EQ block's (table 1, or 2
        on a bus, main or matrix), a dynamics block's (table 0), a filter block's tool (table 3). */
    bool ld_strip_set_block_model (const Strip*, int32_t uid, int32_t model);
    /** Response in dB of block `uid` at `count` frequencies: an EQ or filter block's curve
        (part 0), a dynamics block's key filter (0) or crossover (1). False if it has none. */
    bool ld_strip_block_response (const Strip*, int32_t uid, int32_t part, const double* freqs, double* outDb, int32_t count,
                                  double sampleRate);
    /** Adds a block of `kind` (ChainBlockKind) at step `at` (< 0 or past the end: last);
        returns its id, or -1 (an unknown kind, or the strip has kMaxExtraBlocks). */
    int32_t ld_engine_insert_block (const Engine*, const Strip*, int32_t kind, int32_t at);
    /** Moves block `uid` to step `at` of the chain without it (it keeps its settings and state). */
    bool ld_engine_move_block (const Engine*, const Strip*, int32_t uid, int32_t at);
    bool ld_engine_remove_block (const Engine*, const Strip*, int32_t uid);
    /** Back to the strip's template: no extra blocks, no user taps. */
    void ld_engine_clear_chain (const Engine*, const Strip*);
    /** Places a user tap at step `at` (< 0 or past the end: last); returns its id, or -1 when all are placed. */
    int32_t ld_engine_insert_tap (const Engine*, const Strip*, int32_t at);
    /** Moves user tap `id` to step `at` of the chain without it. */
    bool ld_engine_move_tap (const Engine*, const Strip*, int32_t id, int32_t at);
    /** Takes user tap `id` out of the chain (sends taking it go silent). */
    bool ld_engine_remove_tap (const Engine*, const Strip*, int32_t id);

    // Model tables: 0 = dynamics models, 1 = channel EQ models, 2 = bus EQ models, 3 = filter tools.
    int32_t ld_model_count (int32_t table);
    /** what: 0 = token ("COMP"), 1 = display name. */
    LdString* ld_model_text (int32_t table, int32_t model, int32_t what);
    /** 0 gate, 1 compressor, 2 special, 3 EQ, 4 filter. */
    int32_t ld_model_category (int32_t table, int32_t model);
    int32_t ld_model_find (int32_t table, const char* utf8, size_t len);
    int32_t ld_model_num_params (int32_t table, int32_t model);
    bool ld_model_param_info (int32_t table, int32_t model, int32_t param, ParamInfo* out);
    /** what: 0 = id ("thr"), 1 = label ("THR"), 2 = unit ("dB"). */
    LdString* ld_model_param_text (int32_t table, int32_t model, int32_t param, int32_t what);
    LdString* ld_model_param_choice (int32_t table, int32_t model, int32_t param, int32_t index);
    LdString* ld_model_param_display (int32_t table, int32_t model, int32_t param, float value);
    float ld_model_param_to_normalised (int32_t table, int32_t model, int32_t param, float value);
    float ld_model_param_from_normalised (int32_t table, int32_t model, int32_t param, float position);
    /** Static curve of a dynamics model (its transfer graph): the output level for each input
        level in `inDb` (dB; peak or RMS as the model detects). False for models without one. */
    bool ld_dyn_static_curve (int32_t model, const float* values, int32_t numValues, const float* inDb, float* outDb, int32_t count);
    /** Frequency response (dB at `freqs`) of a model as set, for its front panel's graph: the
        EQ models (tables 1, 2) and the FX devices that have one (EQ devices and the channel
        strips' EQ sections, the graphic EQs, the speaker manager). False for the others. */
    bool ld_model_response (int32_t table, int32_t model, const float* values, int32_t numValues, const float* freqs, float* outDb,
                            int32_t count);

    // Analysis: RTA, LR vector scope, LISTEN
    AnalysisControls* ld_engine_analysis (const Engine*);
    int32_t ld_rta_num_bands();
    /** Centre frequency of an RTA band (a piano key), 0 if out of range. */
    float ld_rta_band_hz (int32_t band);
    /** Message thread: the RTA bands (dB) and peak holds (either may be null); returns the bands
        written. Starts the analysis thread while rtaOn. `frame` (may be null) moves on new bands. */
    int32_t ld_rta_read (const Engine*, float* levels, float* peaks, int32_t count, uint32_t* frame);
    /** The newest L/R pairs of the LR vector scope (interleaved); returns how many. */
    int32_t ld_scope_read (const Engine*, float* lr, int32_t maxPairs);
    /** Scope pairs per second. */
    float ld_scope_rate (const Engine*);

    // Lists: 0 process orders, 1 tap points, 2 sidechain filters, 3 crossover types, 4 send modes,
    // 5 delay units, 6 post insert modes, 7 monitor buses, 8 low-cut slopes, 9 high-cut slopes,
    // 10 aux dynamics models, 11 colours, 12 MAIN LINK modes.
    int32_t ld_list_count (int32_t list);
    LdString* ld_list_item (int32_t list, int32_t index);
    uint32_t ld_colour_rgb (int32_t index);
    /** ICON/COLOR's grid: the colour index at place 0..17 (row by row, three to a row). */
    int32_t ld_colour_at_grid (int32_t position);

    // Strings
    const char* ld_string_data (const LdString*);
    size_t ld_string_len (const LdString*);
    void ld_string_free (LdString*);
    void ld_sink_append (LdSink*, const char* utf8, size_t len);

    // Shows (message thread)
    Show* ld_show_create (const PluginBridge*);
    void ld_show_destroy (Show*);
    LdString* ld_show_name (Show*);
    void ld_show_set_name (Show*, const char* utf8, size_t len);
    int32_t ld_show_num_scenes (Show*);
    int32_t ld_show_current_scene (Show*);
    int32_t ld_show_add_scene (Show*, const Engine*, const char* name, size_t len, int32_t index);
    void ld_show_store_scene (Show*, const Engine*, int32_t index);
    void ld_show_remove_scene (Show*, int32_t index);
    void ld_show_move_scene (Show*, int32_t from, int32_t to);
    /** which: 0 = scene `index`, 1 = next (GO NEXT), 2 = previous (GO PREV), 3 = the first scene
        tagged `index` (#1..#128). Skipped scenes are passed over; a linked scene also recalls the
        one below it. */
    void ld_show_recall_scene (Show*, const Engine*, int32_t which, int32_t index, ApplyResultC* out);
    /** The first non-skipped scene tagged `tag`, or -1. */
    int32_t ld_show_find_scene_by_tag (Show*, int32_t tag);
    /** SEND MIDI of the scenes the last ld_show_recall_scene recalled, one per line (empty if
        none); taking it clears it. */
    LdString* ld_show_take_midi (Show*);

    bool ld_scene_exists (Show*, int32_t scene);
    LdString* ld_scene_name (Show*, int32_t scene);
    LdString* ld_scene_notes (Show*, int32_t scene);
    uint32_t ld_scene_scope (Show*, int32_t scene);
    double ld_scene_fade_seconds (Show*, int32_t scene);
    int32_t ld_scene_num_snippets (Show*, int32_t scene);
    void ld_scene_set_name (Show*, int32_t scene, const char* utf8, size_t len);
    void ld_scene_set_notes (Show*, int32_t scene, const char* utf8, size_t len);
    void ld_scene_set_scope (Show*, int32_t scene, uint32_t scope);
    void ld_scene_set_fade_seconds (Show*, int32_t scene, double seconds);
    /** EDIT TAG #1..#128 (0 = none; out-of-range values clear it), SKIP, LINK (to the scene below). */
    int32_t ld_scene_tag (Show*, int32_t scene);
    bool ld_scene_skip (Show*, int32_t scene);
    bool ld_scene_link (Show*, int32_t scene);
    void ld_scene_set_tag (Show*, int32_t scene, int32_t tag);
    void ld_scene_set_skip (Show*, int32_t scene, bool skip);
    void ld_scene_set_link (Show*, int32_t scene, bool link);
    /** SEND MIDI: the MIDI data (EDIT MIDI TEXT STRING) the scene sends when recalled. */
    LdString* ld_scene_midi (Show*, int32_t scene);
    void ld_scene_set_midi (Show*, int32_t scene, const char* utf8, size_t len);

    int32_t ld_show_add_snippet (Show*, const Engine*, int32_t scene, const char* name, size_t len,
                                 uint32_t sections, const StripRefC* strips, int32_t numStrips);
    void ld_show_store_snippet (Show*, const Engine*, int32_t scene, int32_t snippet);
    void ld_show_remove_snippet (Show*, int32_t scene, int32_t snippet);
    void ld_show_recall_snippet (Show*, const Engine*, int32_t scene, int32_t snippet, ApplyResultC* out);
    bool ld_snippet_exists (Show*, int32_t scene, int32_t snippet);
    LdString* ld_snippet_name (Show*, int32_t scene, int32_t snippet);
    uint32_t ld_snippet_sections (Show*, int32_t scene, int32_t snippet);
    bool ld_snippet_fire_with_scene (Show*, int32_t scene, int32_t snippet);
    int32_t ld_snippet_strips (Show*, int32_t scene, int32_t snippet, StripRefC* out, int32_t capacity);
    void ld_snippet_set_name (Show*, int32_t scene, int32_t snippet, const char* utf8, size_t len);
    void ld_snippet_set_sections (Show*, int32_t scene, int32_t snippet, uint32_t sections);
    void ld_snippet_set_fire_with_scene (Show*, int32_t scene, int32_t snippet, bool fire);
    void ld_snippet_set_strips (Show*, int32_t scene, int32_t snippet, const StripRefC* strips, int32_t n);

    // Scopes, GLOBAL safes, PARTIAL LOAD / UPDATE, RESTORE, snippet REC
    /** A scene's recall scope; returns the strip count (strips copied up to `capacity`). */
    int32_t ld_scene_get_scope (Show*, int32_t scene, ScopeC* out, StripRefC* strips, int32_t capacity);
    void ld_scene_set_recall_scope (Show*, int32_t scene, const ScopeC*);
    /** PARTIAL UPDATE: stores only `scope` into the scene. */
    void ld_show_store_scene_partial (Show*, const Engine*, int32_t index, const ScopeC* scope);
    /** PARTIAL LOAD (`scope`; ignoreSafes = NO GLOB) or RESTORE (scope null: all of it, safes included). */
    void ld_show_recall_scene_with (Show*, const Engine*, int32_t index, const ScopeC* scope, bool ignoreSafes, ApplyResultC* out);
    int32_t ld_show_partial_scope (Show*, ScopeC* out, StripRefC* strips, int32_t capacity);
    /** GLOBAL safes beyond strips (no strips; the strips' own recall safe stays on them). */
    void ld_show_global_safe (Show*, ScopeC* out);
    void ld_show_set_global_safe (Show*, const ScopeC*);
    int32_t ld_snippet_get_scope (Show*, int32_t scene, int32_t snippet, ScopeC* out, StripRefC* strips, int32_t capacity);
    int32_t ld_snippet_parameter_count (Show*, int32_t scene, int32_t snippet);
    int32_t ld_show_add_snippet_scoped (Show*, const Engine*, int32_t scene, const char* name, size_t len, const ScopeC* scope);
    /** ADD ITEMS (add) or REMOVE ITEMS; no strips = every strip of the snippet. */
    void ld_show_snippet_items (Show*, const Engine*, int32_t scene, int32_t snippet, bool add, const ScopeC* items);
    /** PARTIAL LOAD of a snippet (limit null: all of it). */
    void ld_show_recall_snippet_with (Show*, const Engine*, int32_t scene, int32_t snippet, const ScopeC* limit, ApplyResultC* out);
    /** op: 0 REC, 1 STOP (returns the changed parameters), 2 CLEAR, 3 count, 4 recording?, 5 recorded and not saved? */
    int32_t ld_show_snippet_rec (Show*, const Engine*, int32_t op);
    /** SAVE after STOP: a snippet of the recorded parameters within `focus` (null: all); -1 if none. */
    int32_t ld_show_add_recorded_snippet (Show*, const Engine*, int32_t scene, const char* name, size_t len, const ScopeC* focus);

    // LIBRARY presets: FX (.lfx) and channel (.lchn) files
    bool ld_preset_info (const char* path, size_t len, PresetInfoC* out);
    bool ld_show_save_fx_preset (Show*, const Engine*, int32_t slot, const char* path, size_t len, const char* name, size_t nameLen,
                                 LdString** message);
    /** Warnings (a plugin that isn't loaded there, a premium device in 9-16) in `message`. */
    bool ld_show_load_fx_preset (Show*, const Engine*, int32_t slot, const char* path, size_t len, LdString** message);
    bool ld_show_save_channel_preset (Show*, const Engine*, StripRefC strip, uint32_t scope, const char* path, size_t len,
                                      const char* name, size_t nameLen, LdString** message);
    /** useScope false: the preset's own scope. */
    bool ld_show_load_channel_preset (Show*, const Engine*, StripRefC strip, const char* path, size_t len, bool useScope,
                                      uint32_t scope, LdString** message);
    /** UPDATE SCOPE of a channel preset file. */
    bool ld_preset_set_scope (const char* path, size_t len, uint32_t scope);

    // LIBRARY scene (.lsnap) and snippet (.lsnip) files
    bool ld_library_info (const char* path, size_t len, LibraryInfoC* out);
    /** Scene `scene` with its snippets (snippet < 0), or one of its snippets, to `path`. */
    bool ld_show_export_library (Show*, int32_t scene, int32_t snippet, const char* path, size_t len, LdString** message);
    /** A scene file inserted at scene `index` (-1: appended), or a snippet file added to scene `index`;
        the new index, or -1 with `message`. */
    int32_t ld_show_import_library (Show*, const char* path, size_t len, int32_t index, LdString** message);

    // Show items (ABI 18): files added to a show as scenes (manual 5.6 p. 52)
    /** 0 = a console snapshot, 1 = a snippet, 2 = a clip, 3 = an FX preset, 4 = a channel preset; -1 = no such scene. */
    int32_t ld_scene_item_kind (Show*, int32_t scene);
    /** what 0: the file name it was added as; 1: where it loads (the clip's path, "FX 3", "ch:4"). */
    LdString* ld_scene_item_text (Show*, int32_t scene, int32_t what);
    /** ADD ITEM TO SHOW: a scene, snippet, clip, FX preset (into `fxSlot`) or channel preset (onto
        `strip`) file as a scene at `index` (-1: appended); its index, or -1 with `message`. */
    int32_t ld_show_add_item (Show*, const char* path, size_t len, int32_t index, int32_t fxSlot, StripRefC strip, LdString** message);
    /** SAVE + ADD on SNIP: a snippet scene at `index` (recorded: the recorded parameters within `scope`,
        null = all; else what `scope` covers). -1 if there is nothing to save. */
    int32_t ld_show_add_snippet_scene (Show*, const Engine*, const char* name, size_t len, const ScopeC* scope, bool recorded, int32_t index);
    /** The clips of the clip scenes the last recall recalled, one path per line; taking them clears them. */
    LdString* ld_show_take_clips (Show*);
    /** STARTUP: a scene or snippet file loaded onto the console without joining the show. */
    bool ld_show_apply_library (Show*, const Engine*, const char* path, size_t len, ApplyResultC* out, LdString** message);

    bool ld_show_is_fading (Show*);
    void ld_show_cancel_fade (Show*, const Engine*);
    bool ld_show_tick (Show*, const Engine*);

    LdString* ld_show_to_xml (Show*, const Engine*);
    /** False if nothing was loaded (error in *message); true may still set *message (warnings). */
    bool ld_show_load_xml (Show*, const Engine*, const char* utf8, size_t len, bool loadPlugins, LdString** message);
    bool ld_show_save_file (Show*, const Engine*, const char* path, size_t len, LdString** error);
    bool ld_show_load_file (Show*, const Engine*, const char* path, size_t len, bool loadPlugins, LdString** message);
    void ld_show_clear (Show*, const Engine*, int32_t numChannels);
    /** The CUSTOM CONTROLS assignments as a <CustomControls> element, and back. */
    LdString* ld_show_custom_controls (Show*);
    bool ld_show_set_custom_controls (Show*, const char* utf8, size_t len);

    // Layers (message thread). Sections: 0 = left (12 faders), 1 = center (8), 2 = right (4).
    LayerSet* ld_show_layers (Show*);
    LayerSet* ld_layers_create();
    void ld_layers_destroy (LayerSet*);
    /** 0 = Auto (fit the window). */
    int32_t ld_layers_width (LayerSet*, int32_t section);
    void ld_layers_set_width (LayerSet*, int32_t section, int32_t width);
    void ld_layers_set_fitted_width (LayerSet*, int32_t section, int32_t width);
    int32_t ld_layers_effective_width (LayerSet*, int32_t section);
    /** The console model the surface shows (0 full size, 1 COMPACT: the left section is the
        COMPACT's 12 faders with its nine layer keys). Kept by resets and show loads. */
    void ld_layers_set_model (LayerSet*, int32_t model);
    int32_t ld_layers_model (LayerSet*);
    /** A console model's surface and I/O (unknown models: the full-size console's). */
    void ld_console_model_info (int32_t model, ConsoleModelInfo* out);
    LdString* ld_layers_selected (LayerSet*, int32_t section);
    void ld_layers_set_selected (LayerSet*, int32_t section, const char* utf8, size_t len);
    int32_t ld_layers_offset (LayerSet*, int32_t section);
    void ld_layers_set_offset (LayerSet*, int32_t section, int32_t offset);
    /** The section a spill from `origin` goes to (origin -1: none). */
    int32_t ld_layers_spill_target (int32_t origin);
    void ld_layers_start_spill (LayerSet*, int32_t section, SpillC spill);
    bool ld_layers_end_spill (LayerSet*, int32_t section);
    bool ld_layers_spill (LayerSet*, int32_t section, SpillC* out);
    LdString* ld_layers_find_default_containing (LayerSet*, const Engine*, int32_t section, FaderRefC ref);
    /** which: 0 default, 1 default + user, 2 user only, 3 the layer `id` (or the section's first). */
    LayerList* ld_layers_list (LayerSet*, const Engine*, int32_t section, int32_t which, const char* id, size_t idLen);
    void ld_layer_list_free (LayerList*);
    int32_t ld_layer_list_len (const LayerList*);
    LdString* ld_layer_list_id (const LayerList*, int32_t i);
    LdString* ld_layer_list_name (const LayerList*, int32_t i);
    bool ld_layer_list_is_user (const LayerList*, int32_t i);
    int32_t ld_layer_list_faders (const LayerList*, int32_t i, FaderRefC* out, int32_t capacity);
    LdString* ld_layers_add_user (LayerSet*, const char* name, size_t len, const FaderRefC* faders, int32_t n);
    void ld_layers_remove_user (LayerSet*, const char* id, size_t len);
    void ld_layers_rename_user (LayerSet*, const char* id, size_t len, const char* name, size_t nameLen);
    void ld_layers_move_user (LayerSet*, const char* id, size_t len, int32_t delta);
    /** Another copy when the layer already holds `ref`; false when it is full. */
    bool ld_layers_add_to_user (LayerSet*, const char* id, size_t len, FaderRefC ref);
    /** Blanks slot `index` if it holds `ref` (-1: the first slot holding it); other copies stay (ABI 15). */
    void ld_layers_remove_from_user (LayerSet*, const char* id, size_t len, FaderRefC ref, int32_t index);
    /** Inserts `n` faders (blanks too) before slot `index`, moving the rest right;
        false (nothing changed) when a filled slot would go past ld_layers_max_slots(). */
    bool ld_layers_insert_user_slots (LayerSet*, const char* id, size_t len, int32_t index, const FaderRefC* faders, int32_t n);
    int32_t ld_layers_max_slots();
    void ld_layers_move_within_user (LayerSet*, const char* id, size_t len, int32_t index, int32_t delta);
    void ld_layers_set_user_faders (LayerSet*, const char* id, size_t len, const FaderRefC* faders, int32_t n);
    void ld_layers_set_user_slot (LayerSet*, const char* id, size_t len, int32_t index, FaderRefC ref);
    void ld_layers_clear_user_slot (LayerSet*, const char* id, size_t len, int32_t index);
    void ld_layers_swap_user_slots (LayerSet*, const char* id, size_t len, int32_t a, int32_t b);
    void ld_layers_set_override (LayerSet*, int32_t section, const char* id, size_t len, const FaderRefC* faders, int32_t n);
    void ld_layers_clear_override (LayerSet*, int32_t section, const char* id, size_t len);
    bool ld_layers_has_override (LayerSet*, int32_t section, const char* id, size_t len);
    LdString* ld_layers_to_xml (LayerSet*);
    bool ld_layers_from_xml (LayerSet*, const char* utf8, size_t len);
    void ld_layers_reset (LayerSet*);

    // Recorder (message thread; the audio side runs in ld_engine_process)
    /** 0 Stopped, 1 Starting, 2 Playing, 3 Recording (commands not yet taken by the
        audio thread already show: play reads Starting at once). */
    int32_t ld_rec_state (const Engine*);
    int64_t ld_rec_position (const Engine*);
    void ld_rec_play (const Engine*);
    void ld_rec_stop (const Engine*);
    void ld_rec_locate (const Engine*, int64_t position);
    void ld_rec_set_loop (const Engine*, int64_t start, int64_t end, bool enabled);
    /** MODE (ABI 18): 0 = PLAY, 1 = A->B (playback stops at `end`), 2 = LOOP. */
    void ld_rec_set_range (const Engine*, int64_t start, int64_t end, int32_t mode);
    /** Writes the loop range (pointers may be null); returns whether it is enabled. */
    bool ld_rec_loop (const Engine*, int64_t* start, int64_t* end);
    /** Copies the playlist (the arrays may be freed afterwards). */
    void ld_rec_set_playlist (const Engine*, const RecPlaybackTrackC* tracks, int32_t numTracks,
                              const RecClipC* clips, int32_t numClips);
    /** The format of the next recordings (AudioFileFormat; bits 16, 24 or 32 = float for
        WAV / AIFF; kbps for MP3). Take files should carry the format's extension. */
    void ld_rec_set_record_format (const Engine*, int32_t format, int32_t bits, int32_t kbps);
    void ld_rec_record_format (const Engine*, int32_t* format, int32_t* bits, int32_t* kbps);
    /** Creates the files and starts recording; false sets *error (may be null; free it). */
    bool ld_rec_record (const Engine*, const RecRecordTrackC* tracks, int32_t numTracks, LdString** error);
    /** The next finalized take; false if none. */
    bool ld_rec_pop_take (const Engine*, RecTakeC* out);
    /** Linear peak of a recording track's channel since the last call. */
    float ld_rec_record_level (const Engine*, int32_t track, int32_t channel);
    /** minMax holds maxPeaks * 2 * ld_rec_record_track_channels() floats. */
    int32_t ld_rec_recording_peaks (const Engine*, int32_t track, int64_t firstPeak, int32_t maxPeaks, float* minMax);
    int32_t ld_rec_record_track_channels (const Engine*, int32_t track);
    int64_t ld_rec_recorded_length (const Engine*);
    int32_t ld_rec_dropouts (const Engine*);
    float ld_rec_disk_load (const Engine*);
    /** Waits until the disk threads caught up (offline use, faster than real time). */
    bool ld_rec_wait_for_disk (const Engine*, int32_t timeoutMs);

    // ---- USB recorder / player (ld_usb_*): message thread; the audio side runs in
    // ld_engine_process. Scalars and strings only (no shared layout).
    /** REC: records to `path` (created now; channels 2 or 4, bits 16 or 24, format an
        AudioFileFormat, kbps the MP3 bit rate) or resumes a paused recording; false sets
        *error (may be null; free it). */
    bool ld_usb_rec_start (const Engine*, const char* path, size_t len, int32_t channels, int32_t bits, int32_t format,
                           int32_t kbps, LdString** error);
    void ld_usb_rec_pause (const Engine*);
    void ld_usb_rec_stop (const Engine*);
    /** NEW FILE: the recording continues in `path` from the next sample. */
    bool ld_usb_rec_new_file (const Engine*, const char* path, size_t len, LdString** error);
    /** 0 STOP, 1 REC, 2 PAUSE, 3 ERROR (commands not yet taken already show). */
    int32_t ld_usb_rec_state (const Engine*);
    /** Frames in the current file / since REC. */
    int64_t ld_usb_rec_time (const Engine*);
    int64_t ld_usb_rec_total (const Engine*);
    LdString* ld_usb_rec_file (const Engine*);
    /** Linear peak of RECORDER port `port` since the last call (also while not recording). */
    float ld_usb_rec_level (const Engine*, int32_t port);
    /** What RECORDER port 0-3 records (kUsbTapStripBase codes). */
    int32_t ld_usb_rec_tap (const Engine*, int32_t port);
    void ld_usb_set_rec_tap (const Engine*, int32_t port, int32_t code);
    void ld_usb_play (const Engine*);
    void ld_usb_pause (const Engine*);
    void ld_usb_stop (const Engine*);
    /** Cues (play false) or plays queue entry `entry` (0: the first) from frame `pos`. */
    void ld_usb_load (const Engine*, uint64_t entry, int64_t pos, bool play);
    void ld_usb_seek (const Engine*, int64_t pos);
    /** The > and < keys (false: nothing to go to). */
    bool ld_usb_next (const Engine*);
    bool ld_usb_prev (const Engine*);
    /** 0 STOP, 1 PLAY, 2 PAUSE, 3 ERROR. */
    int32_t ld_usb_play_state (const Engine*);
    uint64_t ld_usb_play_entry (const Engine*);
    /** Position and length in frames of the engine's rate. */
    int64_t ld_usb_play_position (const Engine*);
    int64_t ld_usb_play_length (const Engine*);
    /** The file of the clip playing or cued, once opened (empty if not). */
    LdString* ld_usb_play_path (const Engine*);
    float ld_usb_play_level (const Engine*, int32_t channel);
    /** A clip the player opened (pointers may be null); false if unknown. */
    bool ld_usb_play_info (const Engine*, uint64_t entry, int32_t* channels, int32_t* rate, int32_t* bits,
                           int64_t* length, bool* rateMismatch);
    /** A WAV file's header; false if it can't be read. */
    bool ld_usb_wav_info (const char* path, size_t len, int32_t* channels, int32_t* rate, int32_t* bits, int64_t* frames);
    /** Any audio file's header (WAV, FLAC, AIFF, MP3; pointers may be null). */
    bool ld_usb_file_info (const char* path, size_t len, int32_t* format, int32_t* channels, int32_t* rate, int32_t* bits,
                           int64_t* frames, int32_t* kbps);
    /** The format and MP3 bit rate of a queue entry once the player opened it. */
    bool ld_usb_play_format (const Engine*, uint64_t entry, int32_t* format, int32_t* kbps);
    int32_t ld_usb_dropouts (const Engine*);
    bool ld_usb_wait_for_disk (const Engine*, int32_t timeoutMs);
    /** The queue: a folder's audio files (kind 0) or a playlist (kind 1). */
    int32_t ld_usb_queue_len (const Engine*);
    uint64_t ld_usb_queue_version (const Engine*);
    bool ld_usb_queue_entry (const Engine*, int32_t index, uint64_t* id, bool* skip, bool* stop);
    LdString* ld_usb_queue_path (const Engine*, int32_t index);
    int32_t ld_usb_queue_index_of (const Engine*, uint64_t id);
    /** The browser's folder: the queue's files (or, with a playlist open, the folder CLOSE returns to). */
    void ld_usb_queue_set_folder (const Engine*, const char* folder, size_t len);
    void ld_usb_queue_refresh (const Engine*);
    /** RENAME moved `from` (a file or folder) to `to`: the queue's entries under it follow. */
    void ld_usb_queue_rename (const Engine*, const char* from, size_t fromLen, const char* to, size_t toLen);
    int32_t ld_usb_queue_kind (const Engine*);
    LdString* ld_usb_queue_playlist_file (const Engine*);
    bool ld_usb_queue_dirty (const Engine*);
    void ld_usb_playlist_new (const Engine*);
    void ld_usb_playlist_close (const Engine*);
    bool ld_usb_playlist_open (const Engine*, const char* path, size_t len, LdString** error);
    bool ld_usb_playlist_save (const Engine*, const char* path, size_t len, LdString** error);
    /** ADD (a folder queue becomes a new playlist): the new entry's id. */
    uint64_t ld_usb_queue_add (const Engine*, const char* path, size_t len);
    int32_t ld_usb_queue_add_all (const Engine*, const char* folder, size_t len);
    bool ld_usb_queue_remove (const Engine*, int32_t index);
    /** UP (-1) / DOWN (+1): the new index or -1. */
    int32_t ld_usb_queue_move (const Engine*, int32_t index, int32_t delta);
    void ld_usb_queue_set_skip (const Engine*, int32_t index, bool skip);
    void ld_usb_queue_set_stop (const Engine*, int32_t index, bool stop);
    void ld_usb_queue_set_stop_all (const Engine*, bool stop);
    void ld_usb_queue_set_repeat (const Engine*, bool repeat);
    bool ld_usb_queue_repeat (const Engine*);

    // ---- Audio files (any thread): the recorders' encoders and decoders, for export and
    // for reading takes as the recorder plays them. format: AudioFileFormat; bits 16, 24
    // or 32 (float, WAV / AIFF); kbps: MP3 bit rate.
    /** Creates (or truncates) the file; null on failure (*error set; may be null; free it). */
    LdAudioWriter* ld_afw_create (const char* path, size_t len, int32_t channels, int32_t rate, int32_t format, int32_t bits,
                                  int32_t kbps, LdString** error);
    bool ld_afw_write (LdAudioWriter*, const float* const* channels, int32_t numChannels, int32_t frames);
    /** Finalizes, closes and frees the writer; the frames written or -1. */
    int64_t ld_afw_finalize (LdAudioWriter*);
    /** Closes, deletes the file and frees the writer. */
    void ld_afw_abort (LdAudioWriter*);
    /** Opens a WAV, FLAC, AIFF or MP3 file (MP3: gapless, as the recorder plays it); null if unreadable. */
    LdAudioReader* ld_afr_open (const char* path, size_t len);
    bool ld_afr_info (const LdAudioReader*, int32_t* format, int32_t* channels, int32_t* rate, int32_t* bits, int64_t* frames,
                      int32_t* kbps);
    /** Reads frames into planar buffers (extra buffers repeat the last channel; zeros outside the file). */
    bool ld_afr_read (LdAudioReader*, int64_t start, float* const* channels, int32_t numChannels, int32_t frames);
    void ld_afr_close (LdAudioReader*);
    /** Why `channels` at `rate` can't be written in the format (an empty string if they can). */
    LdString* ld_audio_format_check (int32_t format, int32_t bits, int32_t kbps, int32_t channels, int32_t rate);

    // Web remote. Start, stop and status: message thread; the screen and input
    // functions: any one thread at a time.
    LdWeb* ld_web_create();
    void ld_web_destroy (LdWeb*);
    /** Port 0 = any free port; pin: 4-8 digits or empty. Restarts if running; false if
        the port could not be bound (see ld_web_status). */
    bool ld_web_start (LdWeb*, int32_t port, const char* pin, size_t pinLen, bool viewOnly);
    void ld_web_stop (LdWeb*);
    bool ld_web_running (LdWeb*);
    /** Bound port, or -1. */
    int32_t ld_web_port (LdWeb*);
    /** Tab-separated lines: "error\tmsg", "client\taddr\twatching\tseconds", "traffic\tbytes\tframes". */
    LdString* ld_web_status (LdWeb*);
    /** Whether any browser watches (snapshot only then). */
    bool ld_web_watched (LdWeb*);
    /** New screen size: all tiles unknown. Returns the generation for ld_web_put_tile. */
    uint32_t ld_web_reset_screen (LdWeb*, int32_t width, int32_t height, int32_t tile);
    /** Writes the candidate tiles whose pixels changed to `changed` (room for `count`); returns how many. */
    size_t ld_web_changed_tiles (LdWeb*, const uint8_t* pixels, int32_t stride, int32_t height, int32_t bpp,
                                 const uint32_t* candidates, size_t count, uint32_t* changed);
    /** A tile's image (format 0 PNG, 1 JPEG); false if `generation` is out of date. */
    bool ld_web_put_tile (LdWeb*, uint32_t generation, uint32_t index, int32_t format, const uint8_t* data, size_t len);
    /** Sends the tiles put since the last call. */
    void ld_web_publish (LdWeb*);
    bool ld_web_take_input (LdWeb*, LdWebInput* out);

    // ABI 38: the X32 OSC server (message thread; destroy it before the engine).
    LdX32* ld_x32_create (const Engine*);
    void ld_x32_destroy (LdX32*);
    /** (Re)starts it as console `name` on UDP `port` (0: any free port); false if the
        port couldn't be bound. */
    bool ld_x32_start (LdX32*, const char* name, size_t nameLen, int32_t port);
    void ld_x32_stop (LdX32*);
    /** Its UDP port, or -1 when it isn't running. */
    int32_t ld_x32_port (const LdX32*);

    // ABI 39: X32 scene files (.scn). What the X32 has and the engine doesn't comes
    // from (and goes to) the running server, or the handle while it's stopped.
    struct LdX32SceneRead
    {
        int32_t applied, skipped; // lines set; lines with paths Sidecar doesn't have
        LdString* name;           // the header's; caller frees
        LdString* note;
    };
    /** The console as an X32 scene file's text, or null if it isn't an X32. */
    LdString* ld_x32_scene_write (const LdX32*, const char* name, size_t nameLen, const char* note, size_t noteLen);
    /** Sets the console from a scene file's text; false if it isn't an X32. */
    bool ld_x32_scene_read (const LdX32*, const char* text, size_t len, LdX32SceneRead* out);
    /** ABI 42: runs an OSC command's text from a MIDI SysEx message (the
        framing stripped): the answer to a query (an address alone), else
        null; `set` gets how many values were set, -1 for an unknown address. */
    LdString* ld_x32_sysex (const LdX32*, const char* text, size_t len, int32_t* set);

    // ABI 44: the ProMix 01 (livedaw-promix): its own state, program libraries and
    // MIDI over the engine (message thread; destroy it before the engine). Every
    // call does nothing (or answers none) while the engine isn't a ProMix 01.
    LdProMix* ld_promix_create (const Engine*);
    void ld_promix_destroy (LdProMix*);
    /** One received MIDI message: a channel message, or a SysEx from F0 to F7. */
    void ld_promix_midi_in (LdProMix*, const uint8_t* bytes, size_t len);
    /** Queues the console's changes as control changes (the first call sends none). */
    void ld_promix_midi_poll (LdProMix*);
    /** The next poll sends every value. */
    void ld_promix_midi_resend (LdProMix*);
    /** Scene `scene` (0-50, 0 scene 00) was recalled: its program change goes out. */
    void ld_promix_scene_recalled (LdProMix*, int32_t scene);
    /** The next message to send: its length, 0 for none, minus its length if `max` is too small. */
    int32_t ld_promix_midi_out (LdProMix*, uint8_t* out, size_t max);
    struct LdProMixEvent
    {
        int32_t kind; // 0 recall scene `value` (0-50), 1 request for scene `value`'s dump,
                      // 2 dump of scene `value` (`size` bytes), 3 Key Remote button `value`
        int32_t value, data, size;
    };
    bool ld_promix_event (LdProMix*, LdProMixEvent* out, uint8_t* bytes, size_t max);
    /** The Edit Buffer as scene bytes (408); how many were written. */
    int32_t ld_promix_scene_bytes (LdProMix*, uint8_t* out, size_t max);
    void ld_promix_load_scene_bytes (LdProMix*, const uint8_t* data, size_t len);
    /** Queues a bulk dump of scene `scene` (1-50) holding `data`. */
    void ld_promix_send_scene_dump (LdProMix*, int32_t scene, const uint8_t* data, size_t len);
    struct LdProMixSetup
    {
        int32_t txChannel, rxChannel;  // 0-15
        uint8_t switches[3][4];        // program, control, bulk: TX, RX, OMNI, ECHO
        uint8_t registerMode, interval; // interval in 100 ms
    };
    void ld_promix_get_setup (const LdProMix*, LdProMixSetup* out);
    void ld_promix_set_setup (LdProMix*, const LdProMixSetup*);
    /** Parameter `n` (0-530, the CC table's): its internal value (-1 none), its
        number of values, set as the panel does, its name (caller frees). */
    int32_t ld_promix_param (LdProMix*, int32_t n);
    int32_t ld_promix_param_steps (LdProMix*, int32_t n);
    void ld_promix_set_param (LdProMix*, int32_t n, int32_t value);
    LdString* ld_promix_param_name (int32_t n);
    /** Parameter `n`'s value as the ProMix's LCD shows it ("+3dB", "L12", "ON"...),
        and its short label there ("CH3", "LOW F", "THRESHOLD"). */
    LdString* ld_promix_param_text (LdProMix*, int32_t n);
    LdString* ld_promix_param_label (LdProMix*, int32_t n);
    /** Parameter `n`'s value `value` as the LCD would show it (a choice waiting for ENTER). */
    LdString* ld_promix_value_text (LdProMix*, int32_t n, int32_t value);
    /** Program `n`'s name in library `kind` (0 effects 1-40, 1 dynamics 1-20, 2 EQ
        1-50; 0-based), null past its end; an EQ program recalled onto EQ strip
        `strip` (0-15 CH, 16 ST IN, 17-18 RTN, 19 ST OUT). */
    LdString* ld_promix_program_name (const LdProMix*, int32_t kind, int32_t n);
    void ld_promix_recall_eq (LdProMix*, int32_t strip, int32_t n);
    /** The console's own state as text (pairs, groups, processors...), and back. */
    LdString* ld_promix_state_text (LdProMix*);
    void ld_promix_set_state_text (LdProMix*, const char* text, size_t len);
    /** Recalls scene 00, the initial data: every mix setting as a new ProMix 01 has it. */
    void ld_promix_recall_initial (LdProMix*);

    // WING remote (message thread). Destroy it before the engine.
    LdWing* ld_wing_create (const Engine*);
    void ld_wing_destroy (LdWing*);
    /** Ports: 0 = any free port. enable: 1 discovery, 2 OSC, 4 Native. Restarts if running;
        false if an enabled server could not start (the others run; see ld_wing_status). */
    bool ld_wing_start (LdWing*, const char* name, size_t len, int32_t discoveryPort, int32_t oscPort,
                        int32_t nativePort, uint32_t enable);
    void ld_wing_stop (LdWing*);
    bool ld_wing_running (LdWing*);
    void ld_wing_set_name (LdWing*, const char* name, size_t len);
    /** The model the console reports (0 full size: "ngc-full", 1 COMPACT: "wing-compact"),
        at once and across restarts. */
    void ld_wing_set_model (LdWing*, int32_t model);
    /** The full-size console is the black WING: it reports "wing-bk" instead of "ngc-full"
        and "wing-fullsize" (the COMPACT stays "wing-compact"), at once and across restarts (ABI 17). */
    void ld_wing_set_black_finish (LdWing*, bool black);
    /** Bound port of server 0 discovery, 1 OSC, 2 Native; -1 if not running. */
    int32_t ld_wing_port (LdWing*, int32_t which);
    /** Tab-separated lines: "error\tmsg", "client\tnative|osc\taddr\tsubscribed\tseconds",
        "traffic\toscMessages\tnativeBytes". */
    LdString* ld_wing_status (LdWing*);
    /** Applies a WING .snap; *report gets the summary and what was not applied (or the error). */
    bool ld_wing_import_snap (LdWing*, const char* path, size_t len, LdString** report);
    /** Writes a WING .snap (experimental); false sets *message. */
    bool ld_wing_export_snap (LdWing*, const char* path, size_t len, LdString** message);
    /** Adds a WING .show's snapshot scenes to the show (the console only grows to 40 channels). */
    bool ld_wing_import_show (LdWing*, Show*, const char* path, size_t len, LdString** report);
    LdString* ld_wing_coverage();
    /** Publishes the show for /$ctl/lib (scene index -1 / tag 0: none); the host then runs the
        scene recalls clients ask for. ld_wing_clear_library: no host any more. */
    void ld_wing_set_library (LdWing*, const char* show, size_t showLen, int32_t numScenes, int32_t current,
                              const char* scene, size_t sceneLen, int32_t tag);
    void ld_wing_clear_library (LdWing*);
    /** The next scene recall a client asked for: action 1 GO (index: the scene, 0-based),
        2 GO NEXT, 3 GO PREV, 4 GO TAG (index: the tag). False if none. */
    bool ld_wing_take_library_action (LdWing*, int32_t* action, int32_t* index);
    /** Publishes the USB recorder's settings for /rec (the storage folder, REC PATH relative
        to it with "/", channels 2 / 4, bits 16 / 24); clients' REC, NEW FILE and PLAYFILE then
        reach the host (ld_wing_take_usb_action). ld_wing_clear_usb: no host any more. */
    void ld_wing_set_usb (LdWing*, const char* storage, size_t storageLen, const char* recPath, size_t recPathLen,
                          int32_t channels, int32_t bits);
    void ld_wing_clear_usb (LdWing*);
    /** The settings a client changed (recPath may be null). False if none. */
    bool ld_wing_take_usb_settings (LdWing*, int32_t* channels, int32_t* bits, LdString** recPath);
    /** The next USB action a client asked for: 1 REC (a new recording), 2 NEW FILE,
        3 PLAYFILE (*file: relative to the storage folder; may be null). False if none. */
    bool ld_wing_take_usb_action (LdWing*, int32_t* action, LdString** file);
    /** Publishes the show's global safes for /$ctl/safes (bit n = DCA / mute group / FX n+1);
        ld_wing_take_safes: the masks a client changed (false if none). */
    void ld_wing_set_safes (LdWing*, uint32_t dcas, uint32_t muteGroups, uint32_t fx);
    void ld_wing_clear_safes (LdWing*);
    bool ld_wing_take_safes (LdWing*, uint32_t* dcas, uint32_t* muteGroups, uint32_t* fx);
    /** The app settings that are WING parameters (SETUP's surface, metering, solo, MIDI and
        DAW options): their keys, one per line. The host publishes each key's text from its
        settings file (ld_wing_set_host_setting; keys it doesn't have read Sidecar's default)
        and stores what clients change (ld_wing_take_host_setting: false if nothing; either
        pointer may be null). ld_wing_clear_host_settings: no host any more. */
    LdString* ld_wing_host_setting_keys();
    void ld_wing_set_host_setting (LdWing*, const char* key, size_t keyLen, const char* value, size_t valueLen);
    bool ld_wing_take_host_setting (LdWing*, LdString** key, LdString** value);
    void ld_wing_clear_host_settings (LdWing*);
    /** Surface control (/$ctl/layer, /$ctl/user): applies the layer and custom control changes
        clients made to the show, then publishes its layers and custom controls with the upper
        custom control layer ccBank (0-15). Returns kWingSurface* flags; *requestedBank is the
        layer a client chose (-1 none). ld_wing_clear_surface: no host any more. */
    uint32_t ld_wing_sync_surface (LdWing*, Show*, int32_t ccBank, int32_t* requestedBank);
    void ld_wing_clear_surface (LdWing*);
    /** MIDI SYSEX CONTROL on the WING tree: cmd 2 executes a node string
        ("/ch.1.fdr=-1,mute=0"), 3 dumps a node, 5 describes one. True with
        *out the data (empty for 2), false with *out the error text. */
    bool ld_wing_sysex (LdWing*, int32_t cmd, const char* text, size_t len, LdString** out);

    // Console level: sources, talk tags, MUTE state (message thread unless noted)
    /** The source table (*count entries: hardware ports, playback channels, USB PLAYER, oscillators). */
    SourceParams* ld_engine_sources (const Engine*, int32_t* count);
    /** Source table index of an input port (hardware input or playback source), -1 if none. */
    int32_t ld_source_index (int32_t port);
    int32_t ld_source_port (int32_t index);
    /** A source's GAIN range and step (dB), its WING group's (LOCAL IN -2.5 .. +45 in 2.5 dB
        steps, AES50 -3 .. +45.5 in 0.5 dB steps); false (all 0) when the group has no GAIN. */
    bool ld_source_gain_range (int32_t index, float* min, float* max, float* step);
    LdString* ld_source_name (const Engine*, int32_t index);
    void ld_source_set_name (const Engine*, int32_t index, const char* utf8, size_t len);
    /** SourceMode for the source and its pair partner (a new pair takes the first one's settings);
        channels and aux patched to the pair's first source follow it (stereo or mono). */
    void ld_source_set_mode (const Engine*, int32_t index, int32_t mode);
    /** INITIALIZE (the pair's settings and names; the pairing stays). */
    void ld_source_initialize (const Engine*, int32_t index);
    /** The oscillators (*count entries: OSC 1, OSC 2). */
    OscParams* ld_engine_oscillators (const Engine*, int32_t* count);
    /** Any thread: TalkTag bits. */
    uint32_t ld_strip_talk_tags (const Strip*);
    void ld_strip_set_talk_tags (const Strip*, uint32_t tags);
    /** Any thread: MuteState of the last block. */
    int32_t ld_strip_mute_state (const Strip*);
    /** Any thread: soloed in the last block (own solo, a soloed DCA or TALK SOLO). */
    bool ld_strip_is_soloed (const Strip*);
    /** The MUTE key, MUTEGROUP/SIP OVERRIDE aware. */
    void ld_engine_press_mute (const Engine*, const Strip*);
    /** ABI 22: the global MAIN / ALT switch; switches the channels in GLOBAL INPUT SELECT ENABLE. */
    void ld_engine_set_global_alt (const Engine*, bool alt);
    /** Monitor paths (bit 0 = SPEAKERS, bit 1 = PHONES) DCA `dca` controls (named MON A: PHONES, MON B: SPEAKERS, MON LVL). */
    uint32_t ld_engine_dca_monitor_paths (const Engine*, int32_t dca);

    // Routing (ABI 19; livedaw-engine routing.rs, sources.rs)
    /** USR 1-24: what each user signal copies (*count entries, shared memory). */
    UserSignalParams* ld_engine_user_signals (const Engine*, int32_t* count);
    /** USR 25-56: the external source each user patch copies (*count entries). */
    UserPatchParams* ld_engine_user_patches (const Engine*, int32_t* count);
    /** User patch n (0-based) to an external source (an input index); anything else is OFF. */
    void ld_source_set_user_patch (const Engine*, int32_t n, int32_t source);
    /** The source each hardware output plays besides the strips routed there (-1 none; *count entries). */
    std::atomic<int32_t>* ld_engine_output_patch (const Engine*, int32_t* count);
    /** Message thread: `source` (an input index; a bus, main or matrix signal goes to that
        strip's output) to output `port`, replacing what fed it. False when it can't. */
    bool ld_engine_route_output_source (const Engine*, int32_t port, int32_t source);
    /** Message thread: nothing feeds output `port` (strips, monitor paths and sources are cleared). */
    void ld_engine_clear_output (const Engine*, int32_t port);
    /** The input index of a bus, main or matrix signal (StripKind, 0-based number (a bus's slot),
        side 0 L / 1 R); -1 for none (a bus past the WING's 16, a channel). */
    int32_t ld_strip_source_input (int32_t kind, int32_t number, int32_t side);

    // Monitor path processing and SOURCE SOLO (ABI 8; settings in EngineControls::monitor)
    /** DIRECT IN name ("CH.12", "AUX.6", "MTX.3"; "OFF"). */
    LdString* ld_monitor_direct_name (int32_t index);
    /** Delay in ms for a distance in meters. */
    float ld_monitor_delay_ms (float meters);
    /** dB response of path `path`'s EQ (0 SPEAKERS, 1 PHONES): the whole EQ (band < 0, flat while off) or one band. */
    void ld_monitor_eq_response (const Engine*, int32_t path, int32_t band, const float* freqs, float* outDb, int32_t n);
    /** The SOURCE SOLO host (CH39 / AUX7), nullptr when OFF. */
    const Strip* ld_engine_source_solo_host (const Engine*);
}

/// Returns nullptr if the C++ and Rust layouts agree, else a description of the mismatch.
inline const char* verifyLayout()
{
    LayoutInfo info {};
    ld_layout_info (&info);

    struct Check
    {
        int32_t rust;
        size_t cpp;
        const char* what;
    };

    const Check checks[] = {
        { info.stripParams, sizeof (StripParams), "StripParams size" },
        { info.input, sizeof (InputParams), "InputParams size" },
        { info.filter, sizeof (FilterParams), "FilterParams size" },
        { info.dynSlot, sizeof (DynSlotParams), "DynSlotParams size" },
        { info.eqBlock, sizeof (EqBlockParams), "EqBlockParams size" },
        { info.insert, sizeof (InsertParams), "InsertParams size" },
        { info.mainSend, sizeof (MainSendParams), "MainSendParams size" },
        { info.matrixSend, sizeof (MatrixSendParams), "MatrixSendParams size" },
        { info.meters, sizeof (StripMeters), "StripMeters size" },
        { info.send, sizeof (SendParams), "SendParams size" },
        { info.dca, sizeof (DcaParams), "DcaParams size" },
        { info.fx, sizeof (FxParams), "FxParams size" },
        { info.controls, sizeof (EngineControls), "EngineControls size" },
        { info.offFilter, offsetof (StripParams, filter_), "StripParams.filter" },
        { info.offGate, offsetof (StripParams, gate_), "StripParams.gate" },
        { info.offEq, offsetof (StripParams, eq_), "StripParams.eq" },
        { info.offDynamics, offsetof (StripParams, dynamics_), "StripParams.dynamics" },
        { info.offPostInsert, offsetof (StripParams, postInsert_), "StripParams.postInsert" },
        { info.offFader, offsetof (StripParams, faderDb), "StripParams.faderDb" },
        { info.offStereo, offsetof (StripParams, stereo), "StripParams.stereo" },
        { info.offMainSendLevel, offsetof (MainSendParams, levelDb), "MainSendParams.levelDb" },
        { info.offMatrixSendPan, offsetof (MatrixSendParams, pan), "MatrixSendParams.pan" },
        { info.offDcaMask, offsetof (StripParams, dcaMask), "StripParams.dcaMask" },
        { info.offDirectLevel, offsetof (StripParams, directLevelDb), "StripParams.directLevelDb" },
        { info.offColour, offsetof (StripParams, colour), "StripParams.colour" },
        { info.offRecallSafe, offsetof (StripParams, recallSafe), "StripParams.recallSafe" },
        { info.offDynValues, offsetof (DynSlotParams, values), "DynSlotParams.values" },
        { info.offDynXoDepth, offsetof (DynSlotParams, xoDepth), "DynSlotParams.xoDepth" },
        { info.offEqValues, offsetof (EqBlockParams, values), "EqBlockParams.values" },
        { info.offSendPan, offsetof (SendParams, pan), "SendParams.pan" },
        { info.offMetersAutomix, offsetof (StripMeters, automixDb), "StripMeters.automixDb" },
        { info.offControlsDcas, offsetof (EngineControls, dcas), "EngineControls.dcas" },
        { info.offControlsFx, offsetof (EngineControls, fx), "EngineControls.fx" },
        { info.offFxValues, offsetof (FxParams, values), "FxParams.values" },
        { info.offSendMode, offsetof (SendParams, mode), "SendParams.mode" },
        { info.offMatrixSendLevel, offsetof (MatrixSendParams, levelDb), "MatrixSendParams.levelDb" },
        { info.offMetersTrimPeak, offsetof (StripMeters, trimPeak), "StripMeters.trimPeak" },
        { info.offMetersPflPeak, offsetof (StripMeters, pflPeak), "StripMeters.pflPeak" },
        { info.offControlsBusSoloAfl, offsetof (EngineControls, busSoloAfl), "EngineControls.busSoloAfl" },
        { info.offControlsAutomixEnable, offsetof (EngineControls, automixEnable), "EngineControls.automixEnable" },
        { info.offControlsDimDb, offsetof (EngineControls, monitorDimDb), "EngineControls.monitorDimDb" },
        { info.offControlsMainLink, offsetof (EngineControls, mainLink), "EngineControls.mainLink" },
        { info.offAltInputLeft, offsetof (StripParams, altInputLeft), "StripParams.altInputLeft" },
        { info.offAltSelected, offsetof (StripParams, altSelected), "StripParams.altSelected" },
        { info.offControlsInputSelectOverride, offsetof (EngineControls, inputSelectOverride), "EngineControls.inputSelectOverride" },
        { info.offControlsGlobalAlt, offsetof (EngineControls, globalAlt), "EngineControls.globalAlt" },
        { info.recClip, sizeof (RecClipC), "RecClipC size" },
        { info.recPlaybackTrack, sizeof (RecPlaybackTrackC), "RecPlaybackTrackC size" },
        { info.recRecordTrack, sizeof (RecRecordTrackC), "RecRecordTrackC size" },
        { info.recTake, sizeof (RecTakeC), "RecTakeC size" },
        { info.offTapWidth, offsetof (StripParams, tapWidth), "StripParams.tapWidth" },
        { info.offTapEq, offsetof (StripParams, tapEq), "StripParams.tapEq" },
        { info.tapEq, sizeof (TapEqParams), "TapEqParams size" },
        { info.analysis, sizeof (AnalysisControls), "AnalysisControls size" },
        { info.offAnalysisEq, offsetof (AnalysisControls, eq), "AnalysisControls.eq" },
        { info.offAnalysisFilterTap, offsetof (AnalysisControls, filterTap), "AnalysisControls.filterTap" },
        { info.talk, sizeof (TalkParams), "TalkParams size" },
        { info.talkPath, sizeof (TalkPathParams), "TalkPathParams size" },
        { info.monitorSource, sizeof (MonitorSourceParams), "MonitorSourceParams size" },
        { info.monitorPath, sizeof (MonitorPathParams), "MonitorPathParams size" },
        { info.source, sizeof (SourceParams), "SourceParams size" },
        { info.offControlsSoloMode, offsetof (EngineControls, soloMode), "EngineControls.soloMode" },
        { info.offControlsTalk, offsetof (EngineControls, talk), "EngineControls.talk" },
        { info.offControlsSpeakers, offsetof (EngineControls, speakers), "EngineControls.speakers" },
        { info.offControlsPhones, offsetof (EngineControls, phones), "EngineControls.phones" },
        { info.offTalkPaths, offsetof (TalkParams, paths), "TalkParams.paths" },
        { info.offMonitorPathPeak, offsetof (MonitorPathParams, peak), "MonitorPathParams.peak" },
        { info.offSourcePeak, offsetof (SourceParams, peak), "SourceParams.peak" },
        { info.monitorDsp, sizeof (MonitorDspParams), "MonitorDspParams size" },
        { info.monitorControls, sizeof (MonitorControls), "MonitorControls size" },
        { info.offControlsMonitor, offsetof (EngineControls, monitor), "EngineControls.monitor" },
        { info.offMonitorDspEq, offsetof (MonitorDspParams, eq), "MonitorDspParams.eq" },
        { info.offMonitorDspPan, offsetof (MonitorDspParams, pan), "MonitorDspParams.pan" },
        { info.offMonitorControlsSourceSolo, offsetof (MonitorControls, sourceSolo), "MonitorControls.sourceSolo" },
        { info.offSourceMuteGroups, offsetof (SourceParams, muteGroups), "SourceParams.muteGroups" },
        { info.offMatrixSendPanReversed, offsetof (MatrixSendParams, panReversed), "MatrixSendParams.panReversed" },
        { info.offDcaIcon, offsetof (DcaParams, icon), "DcaParams.icon" },
        { info.offCustLink, offsetof (StripParams, custLink), "StripParams.custLink" },
        { info.offDcaMon, offsetof (DcaParams, mon), "DcaParams.mon" },
        { info.consoleModelInfo, sizeof (ConsoleModelInfo), "ConsoleModelInfo size" },
        { info.offDirectSource, offsetof (StripParams, directSource), "StripParams.directSource" },
        { info.userSignal, sizeof (UserSignalParams), "UserSignalParams size" },
        { info.offUserSignalPost, offsetof (UserSignalParams, post), "UserSignalParams.post" },
        { info.userPatch, sizeof (UserPatchParams), "UserPatchParams size" },
        { info.faderRef, sizeof (FaderRefC), "FaderRefC size" },
        { info.offFxSendLeft, offsetof (FxParams, sendLeft), "FxParams.sendLeft" },
        { info.offSrcAuto, offsetof (StripParams, srcAuto), "StripParams.srcAuto" },
        { info.groupSet, sizeof (GroupMask), "GroupMask size" },
        { info.offMuteGroupMask, offsetof (StripParams, muteGroupMask), "StripParams.muteGroupMask" },
        { info.scope, sizeof (ScopeC), "ScopeC size" },
        { info.offScopeFx, offsetof (ScopeC, fx), "ScopeC.fx" },
        { info.counts, sizeof (CountsC), "CountsC size" },
        { info.chainStep, sizeof (ChainStepC), "ChainStepC size" },
        { info.block, sizeof (BlockC), "BlockC size" },
        { info.offBlockMeters, offsetof (BlockC, meters), "BlockC.meters" },
        { info.blockMeters, sizeof (BlockMeters), "BlockMeters size" },
        { info.offBlockMetersOutPeak, offsetof (BlockMeters, outPeak), "BlockMeters.outPeak" },
        { info.delayBlock, sizeof (DelayBlockParams), "DelayBlockParams size" },
    };

    if (info.abiVersion != kAbiVersion)
        return "ABI version";
    for (const auto& c : checks)
        if (c.rust != (int32_t) c.cpp)
            return c.what;
    return nullptr;
}

} // namespace ld
