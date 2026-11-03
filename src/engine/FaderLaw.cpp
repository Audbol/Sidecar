#include "engine/FaderLaw.h"

#include <algorithm>
#include <cmath>

namespace livedaw
{

const FaderLaw& FaderLaw::wing()
{
    static const FaderLaw law { { { 0.0f, -90.0f }, { 0.0625f, -60.0f }, { 0.25f, -30.0f }, { 0.5f, -10.0f }, { 1.0f, 10.0f } }, 0 };
    return law;
}

FaderLaw FaderLaw::of (const ld::FaderLawC& c)
{
    FaderLaw law;
    for (int i = 0; i < std::clamp (c.numPoints, 0, ld::kMaxFaderLawPoints); ++i)
        law.points.emplace_back (c.position[i], c.db[i]);
    law.steps = std::max (0, c.steps);
    return law.points.empty() ? wing() : law;
}

float FaderLaw::maxDb() const noexcept
{
    return points.empty() ? 10.0f : points.back().second;
}

float FaderLaw::minDb() const noexcept
{
    return points.empty() ? -90.0f : points.front().second;
}

float FaderLaw::snap (float position) const noexcept
{
    if (steps <= 1)
        return position;
    const auto n = (float) (steps - 1);
    return std::round (position * n) / n;
}

float FaderLaw::toPosition (float db) const noexcept
{
    if (points.empty() || ! (db > points.front().second))
        return 0.0f;
    float pos = 1.0f;
    for (size_t i = 1; i < points.size(); ++i)
        if (db <= points[i].second)
        {
            const auto [p0, d0] = points[i - 1];
            const auto [p1, d1] = points[i];
            pos = p0 + (db - d0) / (d1 - d0) * (p1 - p0);
            break;
        }
    return snap (std::clamp (pos, 0.0f, 1.0f));
}

float FaderLaw::toDb (float position) const noexcept
{
    const float p = snap (std::clamp (position, 0.0f, 1.0f));
    if (points.empty() || p <= points.front().first)
        return silenceDb;
    for (size_t i = 1; i < points.size(); ++i)
        if (p <= points[i].first)
        {
            const auto [p0, d0] = points[i - 1];
            const auto [p1, d1] = points[i];
            return d0 + (p - p0) / (p1 - p0) * (d1 - d0);
        }
    return maxDb();
}

} // namespace livedaw
