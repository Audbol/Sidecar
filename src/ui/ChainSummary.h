#pragma once

#include "engine/Strip.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace livedaw::ui
{

/** What a skin needs to show a strip's chain (the Universal Mixer's CHAIN and the
    WING surface's HOME > CHAIN): each step's name, whether it is on, and which steps
    are the Universal core's own (extra blocks, user taps) rather than the console
    emulation's. Shared by every skin, so a new console shows the same chain. */

/** A step's name: INPUT, EQ, INSERT 1, PRE-FADER, EQ BLOCK, TAP 3 ... */
juce::String stepName (const ChainStep&);

/** Whether the step processes now: the block's ON (a filter: any of its cuts or its
    tool; an insert: on with an FX slot). Markers, taps and the input are never "on". */
bool stepOn (const Strip&, const ChainStep&);

/** PRE-FADER, FADER and POST: places in the chain, not blocks. */
bool isMarker (const ChainStep&) noexcept;

/** An extra block or a user tap: the Universal core's, not the emulation's. */
inline bool isExtra (const ChainStep& s) noexcept { return s.isBlock() || s.isUserTap(); }

/** A step a skin shows: not the RTA points, the empty order slots, TAP EQ or the
    emulation's own tap points. */
bool isShown (const ChainStep&) noexcept;

/** An extra block's model or setting in a few words ("EQ: WING EQ", "DELAY 12.0 ms",
    "FX 3"); empty for the other steps. */
juce::String blockDetail (const Strip&, const ChainStep&);

/** How many extra blocks and user taps the strip has. */
int countExtras (const Strip&);

} // namespace livedaw::ui
