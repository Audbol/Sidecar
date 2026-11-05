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
        Effects = 2,
        /** The channel library (V2 MAN pp.48-50): programs 0-63 and 64 its UNDO, an input strip's settings. */
        Channel = 3
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
    /** CLEAR a channel program (into its UNDO); false if it held none. */
    bool clearChannel (int n) { return ld::ld_y02r_clear_channel (handle, n); }
    juce::String getLibraryText() const { return takeString (ld::ld_y02r_library_text (handle)); }
    /** PROGRAM CHANGE ASSIGN: the scene program `pc` (0-127) recalls (-1 none), and setting it. */
    int pcScene (int pc) const { return ld::ld_y02r_pc_scene (handle, pc); }
    void setPcScene (int pc, int scene) { ld::ld_y02r_set_pc_scene (handle, pc, scene); }
    /** CONTROL CHANGE ASSIGN: entry `n`'s code (controller n: kind | extra << 3, strip << 8; 0 none), and setting it; the whole table as text, and back. */
    int ccCode (int n) const { return ld::ld_y02r_cc_target (handle, n); }
    void setCcCode (int n, int code) { ld::ld_y02r_set_cc_target (handle, n, code); }
    juce::String getCcText() const
    {
        juce::StringArray v;
        for (int n = 0; n < 128; ++n)
            v.add (juce::String (ccCode (n)));
        return v.joinIntoString (" ");
    }
    void setCcText (const juce::String& text)
    {
        juce::StringArray v;
        v.addTokens (text, " ", "");
        if (v.size() == 128)
            for (int n = 0; n < 128; ++n)
                setCcCode (n, v[n].getIntValue());
    }
    /** The whole table as text (128 numbers, -1 for none), for the app settings, and back. */
    juce::String getPcText() const
    {
        juce::StringArray v;
        for (int pc = 0; pc < 128; ++pc)
            v.add (juce::String (pcScene (pc)));
        return v.joinIntoString (" ");
    }
    void setPcText (const juce::String& text)
    {
        juce::StringArray v;
        v.addTokens (text, " ", "");
        if (v.size() == 128)
            for (int pc = 0; pc < 128; ++pc)
                setPcScene (pc, v[pc].getIntValue());
    }
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
        OscAssign,
        Ms // ABI 50: a channel pair's MS decoding (on pairs it)
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
