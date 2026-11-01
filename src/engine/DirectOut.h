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
    The code is the engine's (ld::kDirectSourceBase, Rust routing::DirectSource). */
struct DirectOut
{
    StripKind kind = StripKind::Channel;
    int number = 1; // as shown: CH 3 = 3, BUS 12 = 12
    int side = 0;   // 0 left, 1 right (a mono channel gives its signal on both)
    bool post = false;

    static constexpr int kNumbers = 0x10000;

    static constexpr bool hasDirectOuts (StripKind k) noexcept { return k == StripKind::Channel || k == StripKind::Aux || k == StripKind::Bus; }

    /** -1 for a strip without direct outs or a number out of range. */
    constexpr int code() const noexcept
    {
        if (! hasDirectOuts (kind) || number < 1 || number > kNumbers || side < 0 || side > 1)
            return -1;
        return ld::kDirectSourceBase + (((int) kind * kNumbers + number - 1) * 2 + side) * 2 + (post ? 1 : 0);
    }

    static constexpr std::optional<DirectOut> fromCode (int code) noexcept
    {
        const int c = code - ld::kDirectSourceBase;
        if (code < ld::kDirectSourceBase || c < 0)
            return std::nullopt;
        const int strip = c / 4, kind = strip / kNumbers;
        if (kind < 0 || kind >= kNumStripKinds || ! hasDirectOuts (static_cast<StripKind> (kind)))
            return std::nullopt;
        return DirectOut { static_cast<StripKind> (kind), strip % kNumbers + 1, (c / 2) % 2, c % 2 == 1 };
    }

    static constexpr bool isDirectOut (int code) noexcept { return fromCode (code).has_value(); }

    bool operator== (const DirectOut&) const = default;
};

} // namespace livedaw
