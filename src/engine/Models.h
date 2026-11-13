#pragma once

#include "engine/RustStrings.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace livedaw::models
{

/** Model tables (the same ones the Rust DSP uses), for building the UI. */
enum Table : int
{
    Dynamics = 0,    // the 32 gate/dynamics models (both slots)
    ChannelEq = 1,   // channel and aux EQ models
    BusEq = 2,       // bus, main and matrix EQ models
    FilterTools = 3, // the FILTER block's tool filters
    Fx = 4,          // the FX rack's devices (NONE, EXT = plugin, premium, standard, channel strips)
    UfxStrip = 5     // a dLive RackUltra engine's channel strip (one model; every console holds the same)
};

/** Choice lists. */
enum List : int
{
    ProcessOrders = 0,
    TapPoints = 1,
    SidechainFilters = 2,
    XoverTypes = 3,
    SendModes = 4,
    DelayUnits = 5,
    PostInsertModes = 6,
    MonitorBuses = 7,
    LowCutSlopes = 8,
    HighCutSlopes = 9,
    AuxDynModels = 10,
    Colours = 11,
    MainLinkModes = 12
};

enum class ParamKind
{
    Linear,
    Log,
    Choice,
    Values,
    Toggle,
    Integer
};

enum class Category
{
    Gate,
    Compressor,
    Special,
    Eq,
    Filter,
    FxNone,
    FxExternal,
    FxPremium,  // FX slots 1-8 only
    FxStandard,
    FxChannel
};

struct Param
{
    int table = 0, model = 0, index = 0;
    juce::String id, label, unit;
    ParamKind kind = ParamKind::Linear;
    float min = 0, max = 1, defaultValue = 0;
    int steps = 0;
    juce::StringArray choices;

    bool isContinuous() const noexcept { return kind == ParamKind::Linear || kind == ParamKind::Log; }
    bool isStepped() const noexcept { return ! isContinuous(); }
    /** Console-style value text ("-20.0 dB", "1:4", "8k"). */
    juce::String display (float value) const;
    /** Knob position 0..1 (log taper for log parameters) and back. */
    float toNormalised (float value) const;
    float fromNormalised (float position) const;
};

struct Model
{
    int table = 0, index = 0;
    juce::String token, name;
    Category category = Category::Gate;
    std::vector<Param> params;
    /** Another console's model on the shown one (the Universal Mixer's swap; `tableAll` only). */
    bool foreign = false;

    /** Index of the parameter `id` ("thr"), or -1. */
    int find (const juce::String& id) const noexcept;
};

/** All models of a table (built once, message thread). */
const std::vector<Model>& table (Table);

/** The shown console's models of a table, then every other console's (named by their console:
    "WING Gate", "X32 Gate"); the shown console's keep their indices. On a Universal
    console, `table`. What the Universal Mixer's block editor offers, unless a real console
    is under remote control (the hardware can't run them). */
const std::vector<Model>& tableAll (Table);

/** A real console is under remote control (RemoteSession): its hardware can't run another
    console's models, so `offered` gives the shown console's alone and the engine refuses the rest. */
void setRemoteHeld (bool held);
bool remoteHeld() noexcept;

/** The models a block's model list offers: `tableAll`, or `table` while a real console is under
    remote control. */
const std::vector<Model>& offered (Table);

/** Model `index` of a table (clamped to it); past the shown console's own models, another
    console's (a block may hold one). */
const Model& model (Table, int index);

/** Index of the model with this token ("COMP"), or -1. */
int findModel (Table, const juce::String& token);

/** The console a model comes from, by its token ("X32" for "X32GATE",
    "PROMIX 01" for "PMCOMP", "02R" for "Y2EQ"); empty for the WING's. On a Universal console, which holds every console's
    models, the others' names start with it ("X32 Gate"). */
juce::String consoleOf (const juce::String& token);

/** A tab of the FX TYPE list (firmware 3.1): its devices in the console's
    order, a row per family. */
struct FxMenuTab
{
    juce::String title, subtitle; // "STD" over "ALL FX"
    bool premium = false;         // FX1-8: the premium devices (FX slots 1-8 only)
    std::vector<std::vector<int>> rows; // indices into table (Fx)
};

/** The FX TYPE list's tabs: FX1-8 (the premium devices), STD ALL FX and
    CH ALL FX (the EQs and the channel strips). Every device but NONE and
    EXT is on one of them, once. */
const std::vector<FxMenuTab>& fxMenuTabs();

juce::StringArray list (List);

/** The static curve of a dynamics model (its transfer graph), from the DSP's
    own configuration: the output level for each input level in `inDb` (dB;
    peak or RMS as the model detects). False for models without a level curve. */
bool dynStaticCurve (int model, const float* values, int numValues, const float* inDb, float* outDb, int count);

/** The frequency response (dB at `freqs`) of a model as set, for its front
    panel's graph: the EQ models and the FX devices that have one (EQ devices,
    the channel strips' EQ sections, the graphic EQs, the speaker manager).
    False for the others. */
bool modelResponse (Table, int model, const float* values, int numValues, const float* freqs, float* outDb, int count);

} // namespace livedaw::models
