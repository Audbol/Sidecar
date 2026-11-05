#pragma once

#include "engine/RustStrings.h"
#include "livedaw_engine.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace livedaw
{

/** The Yamaha 02R over the engine (Phase 6, livedaw-02r through ld_y02r_*):
    what the console has and the engine doesn't (its pairs, groups, keys...),
    its libraries and its MIDI state. The MixerEngine holds one; Y02RMidi
    speaks its MIDI, the 02R surface works its panel. Its values are its edit
    buffer's bytes at Sidecar's addresses (livedaw_02r::params). Every call does
    nothing (or answers -1 / none) while the console isn't an 02R. Message
    thread only. */
class Yamaha02R
{
public:
    explicit Yamaha02R (const ld::Engine* engine) : handle (ld::ld_y02r_create (engine)) {}
    ~Yamaha02R() { ld::ld_y02r_destroy (handle); }

    ld::LdY02R* getHandle() const noexcept { return handle; }

    /** The edit buffer: 2016 bytes, 55 strips of 28 then the inputs' aux sends. */
    static constexpr int size = 2016;
    static constexpr int perStrip = 28;
    static constexpr int sends = 55 * perStrip;

    /** The byte at address `a` (-1: none), and setting it as the console's
        control does (`panel`: its fader and mute groups and gangs follow). */
    int get (int a) const { return ld::ld_y02r_get (handle, a); }
    void set (int a, int value, bool panel = true) { ld::ld_y02r_set (handle, a, value, panel); }
    static juce::String addressName (int a) { return takeString (ld::ld_y02r_address_name (a)); }

    /** The libraries: 0 EQ, 1 dynamics, 2 effects (128 programs each). */
    enum Library
    {
        Eq = 0,
        Dynamics = 1,
        Effects = 2
    };
    juce::String programName (int library, int n) const { return takeString (ld::ld_y02r_program_name (handle, library, n)); }
    /** Recalls program `n` onto strip `strip` (the edit buffer's numbering) or Effect `strip`. */
    bool recall (int library, int n, int strip) { return ld::ld_y02r_recall (handle, library, n, strip); }
    /** STORE onto program `n` what's on strip `strip` (or Effect `strip`), titled `name`;
        false for a read-only preset. The user programs as text (the app keeps them), and back. */
    bool store (int library, int n, int strip, const juce::String& name)
    {
        return ld::ld_y02r_store (handle, library, n, strip, name.toRawUTF8(), name.getNumBytesAsUTF8());
    }
    juce::String getLibraryText() const { return takeString (ld::ld_y02r_library_text (handle)); }
    void setLibraryText (const juce::String& text) { ld::ld_y02r_set_library_text (handle, text.toRawUTF8(), text.getNumBytesAsUTF8()); }
    /** Recalls scene 0, the initial data. */
    void recallInitial() { ld::ld_y02r_recall_initial (handle); }

    /** The console's own state as numbered values (livedaw_02r::panel::What):
        pairs, groups, gangs, keys, recall safe, fade times, the oscillator. */
    enum State
    {
        Pair = 0,
        AuxPair,
        BusPair,
        Gang,
        FaderGroup,
        MuteGroup,
        GroupOn,
        Direct,
        KeyIn,
        StLink,
        InMode,
        RecallSafe,
        RecallSafeOn,
        FadeTime,
        OscOn,
        OscLevel,
        OscWave,
        OscAssign
    };
    int getState (State what, int i) const { return ld::ld_y02r_state (handle, (int) what, i); }
    void setState (State what, int i, int value) { ld::ld_y02r_set_state (handle, (int) what, i, value); }

    /** The console's own state as text (a show keeps it), and back. */
    juce::String getStateText() const { return takeString (ld::ld_y02r_state_text (handle)); }
    void setStateText (const juce::String& text)
    {
        ld::ld_y02r_set_state_text (handle, text.toRawUTF8(), text.getNumBytesAsUTF8());
    }

    /** The edit buffer as scene bytes, and back. */
    std::vector<uint8_t> getSceneBytes() const
    {
        std::vector<uint8_t> out ((size_t) size);
        out.resize ((size_t) juce::jmax (0, ld::ld_y02r_scene_bytes (handle, out.data(), out.size())));
        return out;
    }
    void loadSceneBytes (const std::vector<uint8_t>& data) { ld::ld_y02r_load_scene_bytes (handle, data.data(), data.size()); }

private:
    ld::LdY02R* handle;

    JUCE_DECLARE_NON_COPYABLE (Yamaha02R)
};

} // namespace livedaw
