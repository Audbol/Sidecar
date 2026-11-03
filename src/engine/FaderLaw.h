#pragma once

#include "livedaw_engine.h"

#include <utility>
#include <vector>

namespace livedaw
{

/** A fader law: where a level in dB sits on a fader's travel (0..1), as straight
    segments between (position, dB) breakpoints, rising. Below the first point is
    silence; a stepped fader (steps > 1) lands on its steps. The console's comes
    from its profile (MixerEngine::getFaderLaw, livedaw_dsp::fader::FaderLaw); a
    console's protocol uses its own (the WING's MIDI and OSC: wing()). */
struct FaderLaw
{
    /** Silence: the level at the bottom of the travel. */
    static constexpr float silenceDb = -144.0f;

    std::vector<std::pair<float, float>> points; // (position, dB)
    int steps = 0;                                 // 0: continuous

    /** The WING's (and the X32's): +10 dB at the top, 0 dB at 3/4, -10 dB at 1/2,
        -30 dB at 1/4, -60 dB at 1/16, -90 dB at the bottom. */
    static const FaderLaw& wing();
    static FaderLaw of (const ld::FaderLawC&);

    /** The level at the top of the travel; the lowest level above silence. */
    float maxDb() const noexcept;
    float minDb() const noexcept;
    /** The nearest position the fader has. */
    float snap (float position) const noexcept;
    /** The position of a level (the lowest point and below: 0). */
    float toPosition (float db) const noexcept;
    /** The level at a position (the bottom: silenceDb). */
    float toDb (float position) const noexcept;

    bool operator== (const FaderLaw& o) const noexcept { return points == o.points && steps == o.steps; }
};

} // namespace livedaw
