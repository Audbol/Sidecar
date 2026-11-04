#pragma once

#include "engine/RustStrings.h"
#include "livedaw_engine.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace livedaw
{

/** The Yamaha ProMix 01 over the engine (Phase 5, livedaw-promix through
    ld_promix_*): what the console has and the engine doesn't (its dynamics
    processors, pairs, groups...), its program libraries and its MIDI state.
    The MixerEngine holds one; ProMixMidi speaks its MIDI, the ProMix surface
    works its panel. Every call does nothing (or answers -1 / none) while the
    console isn't a ProMix 01. Message thread only. */
class ProMix
{
public:
    explicit ProMix (const ld::Engine* engine) : handle (ld::ld_promix_create (engine)) {}
    ~ProMix() { ld::ld_promix_destroy (handle); }

    ld::LdProMix* getHandle() const noexcept { return handle; }

    /** Parameter `n` (0-530, the User's Guide's Control Change table): its
        internal value (-1: none), how many values it has, and setting it as the
        panel does (a pair's partner and a group's faders follow). */
    int get (int n) const { return ld::ld_promix_param (handle, n); }
    int steps (int n) const { return ld::ld_promix_param_steps (handle, n); }
    void set (int n, int value) { ld::ld_promix_set_param (handle, n, value); }
    static juce::String name (int n) { return takeString (ld::ld_promix_param_name (n)); }
    /** Its value as the ProMix's LCD shows it ("+3dB", "L12", "ON"...), and its
        short label there ("CH3", "LOW F", "THRESHOLD"). */
    juce::String text (int n) const { return takeString (ld::ld_promix_param_text (handle, n)); }
    juce::String label (int n) const { return takeString (ld::ld_promix_param_label (handle, n)); }
    /** Its value `v` as the LCD would show it (a choice waiting for ENTER). */
    juce::String text (int n, int v) const { return takeString (ld::ld_promix_value_text (handle, n, v)); }

    /** The libraries: 0 effects (40 programs), 1 dynamics (20), 2 EQ (50). */
    enum Library
    {
        Effects = 0,
        Dynamics = 1,
        Eq = 2
    };
    juce::String programName (int library, int n) const { return takeString (ld::ld_promix_program_name (handle, library, n)); }
    /** Recalls EQ program `n` (0-49) onto EQ strip `strip` (0-15 CH, 16 ST IN, 17-18 RTN, 19 ST OUT). */
    void recallEq (int strip, int n) { ld::ld_promix_recall_eq (handle, strip, n); }
    /** Recalls scene 00, the initial data. */
    void recallInitial() { ld::ld_promix_recall_initial (handle); }

    /** The console's own state as text (a show keeps it), and back. */
    juce::String getStateText() const { return takeString (ld::ld_promix_state_text (handle)); }
    void setStateText (const juce::String& text)
    {
        ld::ld_promix_set_state_text (handle, text.toRawUTF8(), text.getNumBytesAsUTF8());
    }

    /** The Edit Buffer as scene bytes (408), and back. */
    std::vector<uint8_t> getSceneBytes() const
    {
        std::vector<uint8_t> out (512);
        out.resize ((size_t) juce::jmax (0, ld::ld_promix_scene_bytes (handle, out.data(), out.size())));
        return out;
    }
    void loadSceneBytes (const std::vector<uint8_t>& data) { ld::ld_promix_load_scene_bytes (handle, data.data(), data.size()); }

private:
    ld::LdProMix* handle;

    JUCE_DECLARE_NON_COPYABLE (ProMix)
};

} // namespace livedaw
