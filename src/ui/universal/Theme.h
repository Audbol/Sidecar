#pragma once

#include "engine/Strip.h"

#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace livedaw::ui::universal
{

/** The Universal Mixer's design tokens (docs/UNIVERSAL_MIXER.md, Phase 2): its
    views draw with these colours, sizes and type only, so another theme is
    another set of values: dark (the default) and light, chosen in the top
    bar's MENU and kept in the app settings (Theme::kSetting). A view takes its
    theme when it is made, so the skin is made again when the theme changes.
    The consoles' own surfaces don't follow it: they are drawn as the hardware. */
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
    /** Light surfaces with dark text (the strip kinds' tints are then the darker ones). */
    bool isLight = false;
    /** The Universal Mixer's calm charcoal (its kinds' tints are the quieter ones). */
    bool isCharcoal = false;
    /** The six themes the Universal Mixer offers (Theme::all(), in this order). */
    enum class Look { FlagshipBlack, BrushedSilver, MidnightBlue, VintageWarm, Daylight, HighContrast, StudioSlate, StudioGraphite, StudioOlive,
                      /** A theme per console (consoleLook()), in the order of the profiles from the WING on. */
                      ConsoleWing, ConsoleX32, ConsoleProMix, Console02R, ConsoleSq5, Console01V, ConsoleDLive, ConsoleDm2000, ConsolePro2, ConsoleXenyx };
    Look look = Look::FlagshipBlack;

    /** The physical parts' finish: a look is another set of these. */
    struct Hardware
    {
        juce::Colour plateTop, plateBottom, trayTop, trayBottom; // brushed plates
        juce::Colour knobLight, knobDark, pointer, tick;
        juce::Colour ink, inkShadow; // engraved legends
        juce::Colour keyTop, keyBottom, keyText;
        juce::Colour nameTab, nameText;
        juce::Colour capTop, capUpper, capLower, capBottom, capLine, capEdge, capShine;
        juce::Colour ledOk, ledWarn, ledHot;
        float grainLight = 0.0f, grainDark = 0.0f;
    } hw;

    /** The light and the dark edge of something raised off a panel. */
    juce::Colour edgeLight() const noexcept { return juce::Colours::white.withAlpha (isLight ? 0.6f : 0.05f); }
    juce::Colour edgeDark() const noexcept { return juce::Colours::black.withAlpha (isLight ? 0.08f : 0.4f); }
    /** The shadow under a cap or a knob. */
    juce::Colour capShadow() const noexcept { return juce::Colours::black.withAlpha (isLight ? 0.22f : 0.55f); }

    /** The kinds of block on a strip, each with its own colour (same on every strip and console). */
    enum class BlockHue { Input, Filter, Gate, Eq, Dynamics, Insert, Delay, Fx };
    juce::Colour blockColour (BlockHue) const noexcept;
    /** The colour of a level's fill (a send's bar): the accent toned down towards the well, but
        at least 3:1 against both the panel and the well it sits on, on every theme. */
    juce::Colour fill() const noexcept;
    /** Black or white, whichever reads better on `background`. */
    static juce::Colour textOn (juce::Colour background) noexcept;

    /** A strip kind's tint (its column's label). */
    juce::Colour kindColour (StripKind kind) const noexcept;
    juce::Colour rawKindColour (StripKind kind) const noexcept;
    juce::Colour rawBlockColour (BlockHue) const noexcept;
    /** `colour` a step towards the text: brighter on the dark theme, darker on the light one
        (a pressed key, a selected row, a hovered outline). */
    juce::Colour lifted (juce::Colour colour, float amount) const noexcept;

    /** The Universal Mixer's dark theme. */
    static const Theme& charcoal();
    /** The palette the consoles' surfaces were built with, for the little of their chrome
        drawn with tokens: it stays as it is so that a design pass here doesn't move them. */
    static const Theme& dark();
    static const Theme& light();
    static const Theme& midnightBlue();
    static const Theme& vintageWarm();
    static const Theme& daylight();
    static const Theme& highContrast();
    static const Theme& studioSlate();
    static const Theme& studioGraphite();
    static const Theme& studioOlive();
    static constexpr int kNumLooks = 9;
    /** The consoles' themes (Look::ConsoleWing on). */
    static constexpr int kNumConsoleLooks = 10;
    /** The theme a console takes when it is loaded (a profile id: 1 WING ... 10 XENYX Q1202USB); none for the Universal profile. */
    static std::optional<Look> consoleLook (int profile) noexcept;
    /** The theme for a console: the user's choice kept for it (consoleSetting), else its own theme, else (Universal) the app's choice. */
    static const Theme& forConsole (juce::PropertiesFile&, int profile);
    /** The setting that keeps the choice made while `profile` is loaded. */
    static juce::String consoleSetting (int profile) { return juce::String (kSetting) + ".console" + juce::String (profile); }
    /** The theme of a look. */
    static const Theme& of (Look);
    /** A look's id (what the setting keeps) and its name in the menu. */
    static const char* idOf (Look);
    static const char* titleOf (Look);
    /** An id ("flagship", "silver", "midnight", "vintage", "daylight", "contrast", "slate", "graphite", "olive"), or the old
        "dark" (Flagship Black) or "light" (Brushed Silver); anything else: Flagship Black. */
    static const Theme& named (const juce::String& name);
    juce::String getName() const { return idOf (look); }
    juce::String getTitle() const { return titleOf (look); }
    /** The app setting that keeps the Universal Mixer's theme (a look's id; "dark" and "light" from before are the first two). */
    static constexpr const char* kSetting = "ui.universalTheme";
    /** The theme the app settings name. */
    static const Theme& fromSettings (juce::PropertiesFile&);
};

/** Sizes in px. The spacing scale is 4, 8, 16, the one radius kRadius; touch
    targets are at least kTouch on a side. */
namespace sizes
{
    constexpr int kTouch = 44;
    constexpr int kGapSmall = 4, kGap = 8, kGapLarge = 16;
    constexpr float kRadius = 2.0f;
    /** A mixer strip column, and the gap between strip kinds. */
    constexpr int kColumnWidth = 88, kKindGap = 12;
    constexpr int kTopBar = 56, kTabBar = 44, kFilterBar = 52;
} // namespace sizes

/** The type scale, and the only sizes the views use: micro (a scale, a tag), caption,
    label, value, heading (a panel's title), title (the app's). */
namespace type
{
    constexpr float kMicro = 11.0f, kCaption = 12.0f, kLabel = 14.0f, kValue = 15.0f, kHeading = 16.0f, kTitle = 22.0f;
    /** The font of a size and weight (message thread: made once each, as making one
        looks its typeface up). */
    juce::Font font (float size, bool bold = false);
} // namespace type

/** The contrast ratio of two colours (WCAG 2: 1 to 21), for checking a theme. */
float contrastRatio (juce::Colour a, juce::Colour b) noexcept;

} // namespace livedaw::ui::universal
