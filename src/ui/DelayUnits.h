#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

namespace livedaw::ui
{

// Delay units: the engine stores the delay in the unit selected (M, FT, MS, SMP).
// The WING turns distance into time at 0.3 m per ms (livedaw_dsp::delay):
// 0.1 m reads 0.3 ft, 0.3 ms and 16 samples at 48 kHz.
constexpr float kSpeedOfSound = 300.0f;
// The WING's range (`/ch/n/in/set/dly`: DIST 0.1 .. 150 m) in any unit; SAMPLES
// starts at 16 at every rate.
constexpr float kMinDelayM = 0.1f, kMaxDelayM = 150.0f, kMinDelaySamples = 16.0f;

inline float delayToMs (float value, int mode, float sr)
{
    switch (mode)
    {
        case 0:  return value / kSpeedOfSound * 1000.0f;
        case 1:  return value * 0.3048f / kSpeedOfSound * 1000.0f;
        case 2:  return value;
        default: return value / sr * 1000.0f;
    }
}

inline float msToDelay (float ms, int mode, float sr)
{
    switch (mode)
    {
        case 0:  return ms * kSpeedOfSound / 1000.0f;
        case 1:  return ms * kSpeedOfSound / 1000.0f / 0.3048f;
        case 2:  return ms;
        default: return juce::jmax (kMinDelaySamples, std::round (ms * sr / 1000.0f));
    }
}

/** The delay range in `mode`'s unit. */
inline juce::NormalisableRange<float> delayRange (int mode, float sr)
{
    auto inUnit = [mode, sr] (float metres) { return msToDelay (delayToMs (metres, 0, sr), mode, sr); };
    return { inUnit (kMinDelayM), inUnit (kMaxDelayM) };
}

inline juce::String delayText (float value, int mode)
{
    static const char* units[] = { " m", " ft", " ms", "" };
    return (mode == 3 ? juce::String (juce::roundToInt (value)) : juce::String (value, 1)) + units[juce::jlimit (0, 3, mode)];
}

} // namespace livedaw::ui
