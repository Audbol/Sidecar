#pragma once

#include "engine/Strip.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace livedaw::ui::universal
{

/** The Universal Mixer's design tokens (docs/UNIVERSAL_MIXER.md, Phase 2): its
    views draw with these colours, sizes and type only, so another theme (a
    light one) is another set of values. Dark first. */
struct Theme
{
    // Surfaces, back to front.
    juce::Colour background, panel, raised, well, outline;
    // Text on the surfaces.
    juce::Colour text, textDim, textOnAccent;
    // Roles: the accent (the active tab, the selection), mute, solo.
    juce::Colour accent, mute, solo;
    // Meters: the signal up to -18 dB, to -6 dB, above, and at full scale.
    juce::Colour meterLow, meterMid, meterHigh, meterClip;

    /** A strip kind's tint (its column's label). */
    juce::Colour kindColour (StripKind kind) const noexcept;

    static const Theme& dark();
};

/** Sizes in px. Touch targets are at least kTouch on a side. */
namespace sizes
{
    constexpr int kTouch = 44;
    constexpr int kGapSmall = 4, kGap = 8, kGapLarge = 16;
    constexpr float kRadius = 6.0f;
    /** A mixer strip column, and the gap between strip kinds. */
    constexpr int kColumnWidth = 88, kKindGap = 12;
    constexpr int kTopBar = 56, kTabBar = 44, kFilterBar = 52;
} // namespace sizes

/** The type scale: captions, labels, values, titles. */
namespace type
{
    constexpr float kCaption = 12.0f, kLabel = 14.0f, kValue = 15.0f, kTitle = 22.0f;
    /** The font of a size and weight (message thread: made once each, as making one
        looks its typeface up). */
    juce::Font font (float size, bool bold = false);
} // namespace type

/** The contrast ratio of two colours (WCAG 2: 1 to 21), for checking a theme. */
float contrastRatio (juce::Colour a, juce::Colour b) noexcept;

} // namespace livedaw::ui::universal
