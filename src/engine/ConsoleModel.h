#pragma once

#include "livedaw_engine.h"

#include <juce_core/juce_core.h>

namespace livedaw
{

/** The console model Sidecar shows (SETUP > GENERAL > CONSOLE): the
    full-size WING, the WING COMPACT or the WING RACK. The values cross the
    C ABI (livedaw-session/model.rs). */
enum class ConsoleModel
{
    Full = 0,
    Compact = 1,
    Rack = 2
};

/** The settings' and SETUP's name of a model: "FULL", "COMPACT", "RACK". */
inline juce::String consoleModelName (ConsoleModel m)
{
    return m == ConsoleModel::Compact ? "COMPACT" : m == ConsoleModel::Rack ? "RACK" : "FULL";
}

/** Unknown names (a later model) read as the full-size console. */
inline ConsoleModel consoleModelFromName (const juce::String& name)
{
    const auto n = name.trim();
    return n.equalsIgnoreCase ("COMPACT") ? ConsoleModel::Compact : n.equalsIgnoreCase ("RACK") ? ConsoleModel::Rack : ConsoleModel::Full;
}

/** A model's value from the C ABI (unknown values: the full-size console). */
inline ConsoleModel consoleModelFromInt (int v) noexcept
{
    return v == 1 ? ConsoleModel::Compact : v == 2 ? ConsoleModel::Rack : ConsoleModel::Full;
}

/** The COMPACT and the RACK: one section of their own on the surface (the
    left section stands for it), a VIRTUAL section on the screens only (WING
    manual 5.7 p. 61), no local aux inputs or outputs (manual chapter 12),
    and a black panel. */
inline bool isSmallModel (ConsoleModel m) noexcept
{
    return m != ConsoleModel::Full;
}

/** A model's surface and local I/O: the full-size WING has 8 local MIDAS PRO
    preamps, 8 aux inputs and outputs and 2x2 GPIO; the COMPACT 24 preamps, no
    local aux I/O and 1x2 GPIO; the RACK 24 preamps, no local aux I/O, 2x2
    GPIO and no faders (WING remote protocols, "Wing, a family"; WING manual
    chapter 12). */
inline ld::ConsoleModelInfo consoleModelInfo (ConsoleModel m)
{
    ld::ConsoleModelInfo info {};
    ld::ld_console_model_info ((int32_t) m, &info);
    return info;
}

} // namespace livedaw
