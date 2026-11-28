#pragma once

#include "engine/Strip.h"

#include <optional>

namespace livedaw
{

/** A direct out (ROUTING > OUTPUTS, source groups CH DIRECT and BUS DIRECT): an
    input channel's, aux's or bus's signal PRE FADER (after INS 1, where PFL
    listens) or POST FADER (after the fader, INS 2 and the post-fader delay,
    before pan), one side, on a hardware output or a RECORDER port. Sidecar's
    own (the WING makes direct outs with user signals); not delay compensated.
    Or at one of the strip's tap points (`tap` 0-15): a DM2000's PRE EQ Direct Outs take
    its Input Channels' tap 2, before the attenuator and the EQ.
    The code is the engine's (ld::kDirectSourceBase, Rust routing::DirectSource; with a tap
    ld::kDirectTapOffset, routing::DirectTap). */
struct DirectOut
{
    StripKind kind = StripKind::Channel;
    int number = 1; // as shown: CH 3 = 3, BUS 12 = 12
    int side = 0;   // 0 left, 1 right (a mono channel gives its signal on both)
    bool post = false;
    int tap = -1;   // a tap point instead of PRE or POST FADER (-1: none)

    static constexpr int kNumbers = 0x10000, kTaps = 16;

    static constexpr bool hasDirectOuts (StripKind k) noexcept { return k == StripKind::Channel || k == StripKind::Aux || k == StripKind::Bus; }

    /** -1 for a strip without direct outs or a number out of range. */
    constexpr int code() const noexcept
    {
        if (! hasDirectOuts (kind) || number < 1 || number > kNumbers || side < 0 || side > 1 || tap >= kTaps)
            return -1;
        const int strip = ((int) kind * kNumbers + number - 1) * 2 + side;
        if (tap >= 0)
            return ld::kDirectSourceBase + ld::kDirectTapOffset + strip * kTaps + tap;
        return ld::kDirectSourceBase + strip * 2 + (post ? 1 : 0);
    }

    static constexpr std::optional<DirectOut> fromCode (int code) noexcept
    {
        if (code < ld::kDirectSourceBase)
            return std::nullopt;
        const bool atTap = code - ld::kDirectSourceBase >= ld::kDirectTapOffset;
        const int c = code - ld::kDirectSourceBase - (atTap ? ld::kDirectTapOffset : 0);
        const int perStrip = atTap ? kTaps : 2;
        const int strip = c / perStrip / 2, kind = strip / kNumbers;
        if (kind < 0 || kind >= kNumStripKinds || ! hasDirectOuts (static_cast<StripKind> (kind)))
            return std::nullopt;
        DirectOut d { static_cast<StripKind> (kind), strip % kNumbers + 1, (c / perStrip) % 2, false };
        if (atTap)
            d.tap = c % kTaps;
        else
            d.post = c % 2 == 1;
        return d;
    }

    static constexpr bool isDirectOut (int code) noexcept { return fromCode (code).has_value(); }

    bool operator== (const DirectOut&) const = default;
};

} // namespace livedaw
