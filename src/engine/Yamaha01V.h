#pragma once

#include "engine/RustStrings.h"
#include "livedaw_engine.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace livedaw
{

/** The Yamaha 01V over the engine (livedaw-01v through ld_y01v_*): its MIDI
    setup, tables and edit buffer. The MixerEngine holds one; Y01VMidi speaks
    its MIDI, the 01V surface works its MIDI pages. Every call does nothing (or
    answers -1 / none) while the console isn't a 01V. Message thread only. */
class Yamaha01V
{
public:
    explicit Yamaha01V (const ld::Engine* engine) : handle (ld::ld_y01v_create (engine)) {}
    ~Yamaha01V() { ld::ld_y01v_destroy (handle); }

    ld::LdY01V* getHandle() const noexcept { return handle; }

    /** MIDI SETUP (p.224) as numbers, by position: Tx CH, Rx CH (0-15), then the switches. */
    enum Setup
    {
        TxChannel = 0,
        RxChannel,
        PcTx,
        PcRx,
        PcOmni,
        PcEcho,
        CcTx,
        CcRx,
        CcOmni,
        CcEcho,
        ParamTx,
        ParamRx,
        ParamEcho,
        BulkRx,
        Interval, // tens of ms
        kSetupValues
    };
    int getSetup (Setup i) const { return ld::ld_y01v_setup_value (handle, (int) i); }
    void setSetup (Setup i, int value) { ld::ld_y01v_set_setup_value (handle, (int) i, value); }

    /** MIDI SETUP as a line of numbers (the settings' value), and back. */
    juce::String getSetupText() const
    {
        juce::StringArray v;
        for (int i = 0; i < kSetupValues; ++i)
            v.add (juce::String (getSetup ((Setup) i)));
        return v.joinIntoString (" ");
    }
    void setSetupText (const juce::String& text)
    {
        juce::StringArray v;
        v.addTokens (text, " ", "");
        v.removeEmptyStrings();
        if (v.size() == kSetupValues)
            for (int i = 0; i < kSetupValues; ++i)
                setSetup ((Setup) i, v[i].getIntValue());
    }

    /** Table of MIDI page 2: the scene program change `pc` (0-127) recalls (-1 none), and setting it. */
    int pcScene (int pc) const { return ld::ld_y01v_pc_scene (handle, pc); }
    void setPcScene (int pc, int scene) { ld::ld_y01v_set_pc_scene (handle, pc, scene); }
    /** Table of MIDI page 3: controller `cc`'s target as a code (0: NO ASSIGN), and setting it. */
    int ccCode (int cc) const { return ld::ld_y01v_cc_target (handle, cc); }
    void setCcCode (int cc, int code) { ld::ld_y01v_set_cc_target (handle, cc, code); }
    /** Its three columns as MIDI page 3 prints them ("FADER", "CHANNEL", "CH1"). */
    static juce::String ccLabel (int code, int column) { return takeString (ld::ld_y01v_cc_label (code, column)); }
    /** INITIALIZE: the program change table. */
    void initProgramTable() { ld::ld_y01v_init_table (handle, 0); }
    /** INITIALIZE: the control change table (0 DEFAULT, 1 P.MIX 01, 2 03D). */
    void initControlTable (int arrangement) { ld::ld_y01v_init_table (handle, 1 + arrangement); }

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
    juce::String getCcText() const
    {
        juce::StringArray v;
        for (int cc = 0; cc < 120; ++cc)
            v.add (juce::String (ccCode (cc)));
        return v.joinIntoString (" ");
    }
    void setCcText (const juce::String& text)
    {
        juce::StringArray v;
        v.addTokens (text, " ", "");
        if (v.size() == 120)
            for (int cc = 0; cc < 120; ++cc)
                setCcCode (cc, v[cc].getIntValue());
    }

    /** Scene bytes of the edit buffer's size (the manual's 1014, Sidecar's layout). */
    static constexpr int sceneBytes = 1014;

private:
    ld::LdY01V* handle;

    JUCE_DECLARE_NON_COPYABLE (Yamaha01V)
};

} // namespace livedaw
