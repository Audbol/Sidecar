#include "ui/universal/Theme.h"

#include <cmath>
#include <map>

namespace livedaw::ui::universal
{

namespace
{
    // The same colour, lightened or darkened (hue kept) until it reads on `bg`.
    juce::Colour readableOn (juce::Colour c, juce::Colour bg) noexcept
    {
        const auto toward = contrastRatio (juce::Colours::white, bg) >= contrastRatio (juce::Colours::black, bg) ? juce::Colours::white : juce::Colours::black;
        for (int i = 0; i < 20 && contrastRatio (c, bg) < 4.5f; ++i)
            c = c.interpolatedWith (toward, 0.08f);
        return c;
    }
}

juce::Colour Theme::kindColour (StripKind kind) const noexcept
{
    return readableOn (rawKindColour (kind), raised);
}

juce::Colour Theme::rawKindColour (StripKind kind) const noexcept
{
    if (look >= Look::MidnightBlue && look <= Look::StudioOlive)
    {
        // Channel, aux, bus, main, matrix: bright on the dark looks, dark on Daylight.
        static const juce::uint32 tints[7][5] = {
            { 0xff7fb8ff, 0xff5fe0d0, 0xffc4a8ff, 0xfff0f6ff, 0xffffc07a },  // Midnight Blue
            { 0xff8fb8d8, 0xff8fcfa0, 0xffd8a0c8, 0xfffff0d0, 0xffffb860 },  // Vintage Warm
            { 0xff0033a8, 0xff006a5e, 0xff5a1fa8, 0xff000000, 0xff8a4800 },  // Daylight
            { 0xff5ab0ff, 0xff00ffd0, 0xffd09cff, 0xffffffff, 0xffffa030 },  // High Contrast
            { 0xff8fc8ff, 0xff7fe0c8, 0xffcfb4ff, 0xfff4f7fa, 0xffffc885 },  // Studio Slate
            { 0xff8fc0f0, 0xff86d8c0, 0xffd0b0f0, 0xfff6f4ee, 0xffffc070 },  // Studio Graphite
            { 0xff94c4f0, 0xff8adcc0, 0xffd4b4f0, 0xfff6f4e6, 0xffffc474 } }; // Studio Olive
        return juce::Colour (tints[(int) look - (int) Look::MidnightBlue][(int) kind]);
    }
    if (isLight)
    {
        // The same hues, dark enough to read on a light surface.
        switch (kind)
        {
            case StripKind::Channel: return juce::Colour (0xff2456c4);
            case StripKind::Aux:     return juce::Colour (0xff0b7a6e);
            case StripKind::Bus:     return juce::Colour (0xff7a3fc0);
            case StripKind::Main:    return juce::Colour (0xff20242b);
            case StripKind::Matrix:  return juce::Colour (0xffa35a00);
        }
        return text;
    }
    if (isCharcoal)
    {
        // Quieter: a strip's own colour is what should catch the eye.
        switch (kind)
        {
            case StripKind::Channel: return juce::Colour (0xff8ab4f0);
            case StripKind::Aux:     return juce::Colour (0xff79c9be);
            case StripKind::Bus:     return juce::Colour (0xffb9a4ea);
            case StripKind::Main:    return juce::Colour (0xffeceef0);
            case StripKind::Matrix:  return juce::Colour (0xffe3b07a);
        }
        return text;
    }
    switch (kind)
    {
        case StripKind::Channel: return juce::Colour (0xff5b9bff);
        case StripKind::Aux:     return juce::Colour (0xff4fc3b5);
        case StripKind::Bus:     return juce::Colour (0xffc28bff);
        case StripKind::Main:    return juce::Colour (0xfff2f4f8);
        case StripKind::Matrix:  return juce::Colour (0xffffb35c);
    }
    return text;
}

juce::Colour Theme::blockColour (BlockHue h) const noexcept
{
    return readableOn (rawBlockColour (h), raised);
}

juce::Colour Theme::rawBlockColour (BlockHue h) const noexcept
{
    static const juce::uint32 others[7][8] = {
        { 0xffffd060, 0xff5fe0d0, 0xffff8a70, 0xff7fb8ff, 0xffb49cff, 0xffff8ab4, 0xffa0e070, 0xffd6b8ff },  // Midnight Blue
        { 0xffffc040, 0xff8fcfa0, 0xffff8a6a, 0xff8fb8d8, 0xffc8a0e0, 0xffe890a8, 0xffb4d870, 0xffe0b8f0 },  // Vintage Warm
        { 0xff805800, 0xff006a5e, 0xffa82a10, 0xff0033a8, 0xff5a1fa8, 0xffa0104c, 0xff2f6a10, 0xff6a1f98 },  // Daylight
        { 0xffffe000, 0xff00ffd0, 0xffff8a6a, 0xff5ab0ff, 0xffc8a0ff, 0xffff7ab8, 0xff80ff40, 0xffe0b8ff },  // High Contrast
        { 0xfff8d466, 0xff90ecd6, 0xffffaa92, 0xff9fd2ff, 0xffccb8ff, 0xffffaccc, 0xffb8ec90, 0xffe6d2ff },  // Studio Slate
        { 0xffffc548, 0xff86d8c0, 0xffff9a80, 0xff8fc0f0, 0xffc4a8f0, 0xffff9cbc, 0xffaadc80, 0xffdcc4f4 },  // Studio Graphite
        { 0xfff0cc60, 0xff94ecd6, 0xffffaa92, 0xffa0d0ff, 0xffd4b8ff, 0xffffaccc, 0xffbcec90, 0xffe8d4ff } }; // Studio Olive
    if (look >= Look::MidnightBlue && look <= Look::StudioOlive)
        return juce::Colour (others[(int) look - (int) Look::MidnightBlue][(size_t) h]);
    static const juce::uint32 dark[] = { 0xffe8b84a, 0xff4fc3b5, 0xffe07a5f, 0xff5b9bff, 0xff9b7bea, 0xffe0709a, 0xff8bc46a, 0xffc9a0ff };
    static const juce::uint32 light[] = { 0xff9a6b00, 0xff0b7a6e, 0xffb23a1f, 0xff2456c4, 0xff6a3fc0, 0xffb02060, 0xff3f7a1f, 0xff7a3fa8 };
    return juce::Colour ((isLight ? light : dark)[(size_t) h]);
}

juce::Colour Theme::fill() const noexcept
{
    for (float amount = 0.5f; amount < 1.0f; amount += 0.1f)
    {
        const auto c = well.interpolatedWith (accent, amount);
        if (contrastRatio (c, panel) >= 3.0f && contrastRatio (c, well) >= 3.0f)
            return c;
    }
    return accent;
}

juce::Colour Theme::textOn (juce::Colour c) noexcept
{
    return contrastRatio (c, juce::Colours::black) >= contrastRatio (c, juce::Colours::white) ? juce::Colours::black : juce::Colours::white;
}

namespace
{
    Theme::Hardware blackAnodised()
    {
        Theme::Hardware h;
        h.plateTop = juce::Colour (0xff2a2c30);
        h.plateBottom = juce::Colour (0xff1b1d20);
        h.trayTop = juce::Colour (0xff26282c);
        h.trayBottom = juce::Colour (0xff17191c);
        h.knobLight = juce::Colour (0xff6b6f76);
        h.knobDark = juce::Colour (0xff17191c);
        h.pointer = juce::Colour (0xff7fb2ff);
        h.tick = juce::Colour (0xff6a707a);
        h.ink = juce::Colour (0xffbfc5ce);
        h.inkShadow = juce::Colours::black;
        h.keyTop = juce::Colour (0xff3b3e44);
        h.keyBottom = juce::Colour (0xff222428);
        h.keyText = juce::Colour (0xffdfe3e9);
        h.nameTab = juce::Colour (0xff0d0f12);
        h.nameText = juce::Colour (0xffd7e2f2);
        h.capTop = juce::Colour (0xff55585e);
        h.capUpper = juce::Colour (0xff25272b);
        h.capLower = juce::Colour (0xff1a1c1f);
        h.capBottom = juce::Colour (0xff3b3e43);
        h.capLine = juce::Colour (0xff7fb2ff);
        h.capEdge = juce::Colours::black.withAlpha (0.7f);
        h.capShine = juce::Colours::white.withAlpha (0.2f);
        h.ledOk = juce::Colour (0xff3ccf74);
        h.ledWarn = juce::Colour (0xffeec13f);
        h.ledHot = juce::Colour (0xffee4638);
        h.grainLight = 0.1f;
        h.grainDark = 0.22f;
        return h;
    }

    Theme::Hardware brushedSilver()
    {
        Theme::Hardware h;
        h.plateTop = juce::Colour (0xffe6e8eb);
        h.plateBottom = juce::Colour (0xffc9ccd2);
        h.trayTop = juce::Colour (0xffd7dade);
        h.trayBottom = juce::Colour (0xffb9bcc2);
        h.knobLight = juce::Colour (0xfff5f6f8);
        h.knobDark = juce::Colour (0xff888d95);
        h.pointer = juce::Colour (0xff1f5fd0);
        h.tick = juce::Colour (0xff5f656f);
        h.ink = juce::Colour (0xff343a43);
        h.inkShadow = juce::Colours::white.withAlpha (0.8f);
        h.keyTop = juce::Colour (0xfff6f7f9);
        h.keyBottom = juce::Colour (0xffcfd2d7);
        h.keyText = juce::Colour (0xff20242a);
        h.nameTab = juce::Colour (0xff2a2d32);
        h.nameText = juce::Colour (0xffe8ebf0);
        h.capTop = juce::Colour (0xffeceef1);
        h.capUpper = juce::Colour (0xffbcc0c7);
        h.capLower = juce::Colour (0xff9da1a9);
        h.capBottom = juce::Colour (0xffd6d9de);
        h.capLine = juce::Colour (0xff1f5fd0);
        h.capEdge = juce::Colours::black.withAlpha (0.35f);
        h.capShine = juce::Colours::white.withAlpha (0.75f);
        h.ledOk = juce::Colour (0xff13a04a);
        h.ledWarn = juce::Colour (0xffc8960a);
        h.ledHot = juce::Colour (0xffd92626);
        h.grainLight = 0.3f;
        h.grainDark = 0.1f;
        return h;
    }

    /** The black anodised finish in another colour way: the plates, knobs, keys and caps. */
    Theme::Hardware tinted (juce::uint32 plateTop, juce::uint32 plateBottom, juce::uint32 trayTop, juce::uint32 trayBottom, juce::uint32 knobLight, juce::uint32 knobDark,
                            juce::uint32 pointer, juce::uint32 tick, juce::uint32 ink, juce::uint32 keyTop, juce::uint32 keyBottom, juce::uint32 keyText,
                            juce::uint32 nameTab, juce::uint32 nameText, juce::uint32 capTop, juce::uint32 capUpper, juce::uint32 capLower, juce::uint32 capBottom)
    {
        auto h = blackAnodised();
        h.plateTop = juce::Colour (plateTop); h.plateBottom = juce::Colour (plateBottom);
        h.trayTop = juce::Colour (trayTop); h.trayBottom = juce::Colour (trayBottom);
        h.knobLight = juce::Colour (knobLight); h.knobDark = juce::Colour (knobDark);
        h.pointer = juce::Colour (pointer); h.capLine = juce::Colour (pointer);
        h.tick = juce::Colour (tick); h.ink = juce::Colour (ink);
        h.keyTop = juce::Colour (keyTop); h.keyBottom = juce::Colour (keyBottom); h.keyText = juce::Colour (keyText);
        h.nameTab = juce::Colour (nameTab); h.nameText = juce::Colour (nameText);
        h.capTop = juce::Colour (capTop); h.capUpper = juce::Colour (capUpper); h.capLower = juce::Colour (capLower); h.capBottom = juce::Colour (capBottom);
        return h;
    }

    Theme::Hardware daylightHardware()
    {
        auto h = brushedSilver();
        h.plateTop = juce::Colour (0xfff4f4f2); h.plateBottom = juce::Colour (0xffd8d8d4);
        h.trayTop = juce::Colour (0xffe6e6e2); h.trayBottom = juce::Colour (0xffc8c8c4);
        h.knobLight = juce::Colour (0xffffffff); h.knobDark = juce::Colour (0xff7a7f88);
        h.pointer = juce::Colour (0xff0040c0); h.capLine = juce::Colour (0xff0040c0);
        h.tick = juce::Colour (0xff2a2f38); h.ink = juce::Colours::black;
        h.keyTop = juce::Colours::white; h.keyBottom = juce::Colour (0xffdcdcd8); h.keyText = juce::Colours::black;
        h.nameTab = juce::Colour (0xff10141a); h.nameText = juce::Colours::white;
        h.ledOk = juce::Colour (0xff0a7a2e); h.ledWarn = juce::Colour (0xff9a7a00); h.ledHot = juce::Colour (0xffc80000);
        h.grainLight = 0.15f; h.grainDark = 0.05f;
        return h;
    }

    /** Studio Slate: a blue-grey slate plate, satin aluminium trays, silver ribbed caps, a cream name tab; no grain. */
    Theme::Hardware slateHardware()
    {
        auto h = tinted (0xff3f4a54, 0xff2e373f, 0xffc6c9cc, 0xffa7abaf, 0xff8d97a1, 0xff2c3238, 0xffffffff, 0xff333a40, 0xffd7dee5, 0xff5a646e, 0xff343c44, 0xffeaeef1,
                         0xffe8e4d8, 0xff1b1b1b, 0xffeceded, 0xffa8acb0, 0xff83878b, 0xffc8cbce);
        h.capLine = juce::Colour (0xff111111);
        h.ledOk = juce::Colour (0xff46d36b); h.ledWarn = juce::Colour (0xfff0c542); h.ledHot = juce::Colour (0xfff0483a);
        h.grainLight = 0.0f; h.grainDark = 0.0f;
        return h;
    }

    /** Studio Graphite: charcoal plate, warmer aluminium trays, an off-white name tab. */
    Theme::Hardware graphiteHardware()
    {
        auto h = tinted (0xff4a4c50, 0xff303235, 0xffd0cdc6, 0xffb0aca4, 0xff9a9892, 0xff2a2b2d, 0xfff1f0ee, 0xff3a3a3a, 0xffdcd9d2, 0xff5c5e62, 0xff36383b, 0xfff1f0ee,
                         0xfff2efe6, 0xff1b1b1b, 0xffeceae6, 0xffaca9a2, 0xff86837c, 0xffcdcac4);
        h.capLine = juce::Colour (0xff111111);
        h.ledOk = juce::Colour (0xff46d36b); h.ledWarn = juce::Colour (0xfff0c542); h.ledHot = juce::Colour (0xfff0483a);
        h.grainLight = 0.0f; h.grainDark = 0.0f;
        return h;
    }

    /** Studio Olive: sage plate like a vintage broadcast desk, pale grey trays, brass caps and knobs. */
    Theme::Hardware oliveHardware()
    {
        auto h = tinted (0xff5a6446, 0xff3d4630, 0xffd2d4d0, 0xffb4b7b3, 0xffd8b860, 0xff3a3a22, 0xffe0b84a, 0xff3c4030, 0xffe8e6d0, 0xff6a7454, 0xff414a33, 0xfff2f0e0,
                         0xffeee6c8, 0xff1f1c10, 0xfff0d890, 0xffc8a850, 0xff9a7c32, 0xffd6bb6c);
        h.capLine = juce::Colour (0xff2a2010);
        h.ledOk = juce::Colour (0xff46d36b); h.ledWarn = juce::Colour (0xfff0c542); h.ledHot = juce::Colour (0xfff0483a);
        h.grainLight = 0.0f; h.grainDark = 0.0f;
        return h;
    }

    Theme::Hardware highContrastHardware()
    {
        auto h = tinted (0xff1c1c1c, 0xff0a0a0a, 0xff161616, 0xff000000, 0xffffffff, 0xff404040, 0xff00e5ff, 0xffe0e0e0, 0xffffffff, 0xff303030, 0xff141414, 0xffffffff,
                         0xffffffff, 0xff000000, 0xffe0e0e0, 0xff606060, 0xff202020, 0xff909090);
        h.ledOk = juce::Colour (0xff00ff40); h.ledWarn = juce::Colour (0xffffff00); h.ledHot = juce::Colour (0xffff0000);
        h.grainLight = 0.0f; h.grainDark = 0.0f;
        return h;
    }

    Theme withHardware (Theme t, const Theme::Hardware& hardware)
    {
        t.hw = hardware;
        return t;
    }
} // namespace

const Theme& Theme::charcoal()
{
    // Black anodised plate.
    static const Theme theme = withHardware ({
        juce::Colour (0xff0d0e10), // background
        juce::Colour (0xff1b1d20), // panel
        juce::Colour (0xff2a2c30), // raised
        juce::Colour (0xff0a0b0d), // well
        juce::Colour (0xff34373c), // outline
        juce::Colour (0xffdfe3e9), // text
        juce::Colour (0xff9aa0a9), // textDim
        juce::Colour (0xff0a0b0d), // textOnAccent
        juce::Colour (0xff7fb2ff), // accent
        juce::Colour (0xffee5a4c), // mute
        juce::Colour (0xffeec13f), // solo
        juce::Colour (0xff3ccf74), // meterLow
        juce::Colour (0xffeec13f), // meterMid
        juce::Colour (0xfff0913a), // meterHigh
        juce::Colour (0xffee4638), // meterClip
        false,                     // isLight
        true,                      // isCharcoal
    }, blackAnodised());
    return theme;
}

const Theme& Theme::dark()
{
    static const Theme theme = withHardware ({
        juce::Colour (0xff111318), // background
        juce::Colour (0xff1a1d24), // panel
        juce::Colour (0xff252a33), // raised
        juce::Colour (0xff0b0c10), // well
        juce::Colour (0xff343a46), // outline
        juce::Colour (0xffe9ecf2), // text
        juce::Colour (0xffa3aab8), // textDim
        juce::Colour (0xff0b0c10), // textOnAccent
        juce::Colour (0xff4c8dff), // accent
        juce::Colour (0xffff5a5f), // mute
        juce::Colour (0xffffc83d), // solo
        juce::Colour (0xff3ecf70), // meterLow
        juce::Colour (0xffd8d23a), // meterMid
        juce::Colour (0xffff9a3c), // meterHigh
        juce::Colour (0xffff3b3b), // meterClip
    }, blackAnodised());
    return theme;
}

juce::Colour Theme::lifted (juce::Colour colour, float amount) const noexcept
{
    return isLight ? colour.darker (amount * 0.6f) : colour.brighter (amount);
}

const Theme& Theme::light()
{
    static const Theme theme = withHardware ({
        juce::Colour (0xffd9dbdf), // background
        juce::Colour (0xffe6e8eb), // panel
        juce::Colour (0xffffffff), // raised
        juce::Colour (0xffd9dce1), // well
        juce::Colour (0xffc3c8cf), // outline
        juce::Colour (0xff14171c), // text
        juce::Colour (0xff4a5362), // textDim
        juce::Colour (0xffffffff), // textOnAccent
        juce::Colour (0xff1a56c0), // accent
        juce::Colour (0xffb32a2f), // mute
        juce::Colour (0xff7e4f00), // solo
        juce::Colour (0xff13803d), // meterLow
        juce::Colour (0xffa88f00), // meterMid
        juce::Colour (0xffd9730d), // meterHigh
        juce::Colour (0xffd92626), // meterClip
        true,                      // isLight
        false,                     // isCharcoal
        Theme::Look::BrushedSilver,
    }, brushedSilver());
    return theme;
}

const Theme& Theme::midnightBlue()
{
    static const Theme theme = withHardware ({
        juce::Colour (0xff070d1a), // background
        juce::Colour (0xff0e1930), // panel
        juce::Colour (0xff17264a), // raised
        juce::Colour (0xff050a14), // well
        juce::Colour (0xff2a3d66), // outline
        juce::Colour (0xffdbe9ff), // text
        juce::Colour (0xff8fa8d0), // textDim
        juce::Colour (0xff04101f), // textOnAccent
        juce::Colour (0xff3fd0ff), // accent
        juce::Colour (0xffff6b78), // mute
        juce::Colour (0xffffd04a), // solo
        juce::Colour (0xff35d68a), // meterLow
        juce::Colour (0xffe6d84a), // meterMid
        juce::Colour (0xffff9a3c), // meterHigh
        juce::Colour (0xffff4455), // meterClip
        false, // isLight
        false,
        Theme::Look::MidnightBlue,
    }, tinted (0xff1c2a4a, 0xff101a33, 0xff182442, 0xff0d1630, 0xff6b7fa8, 0xff0d1630, 0xff3fd0ff, 0xff6a82ad, 0xffbcd0f0, 0xff2c3d66, 0xff1a2644, 0xffe0ecff, 0xff050a14, 0xffd0e4ff, 0xff4a5f8c, 0xff1b2a4d, 0xff111b36, 0xff33466f));
    return theme;
}

const Theme& Theme::vintageWarm()
{
    static const Theme theme = withHardware ({
        juce::Colour (0xff1a110b), // background
        juce::Colour (0xff2a1b12), // panel
        juce::Colour (0xff3b281a), // raised
        juce::Colour (0xff130c07), // well
        juce::Colour (0xff5a4128), // outline
        juce::Colour (0xfff3e6c8), // text
        juce::Colour (0xffc4aa84), // textDim
        juce::Colour (0xff1a0f05), // textOnAccent
        juce::Colour (0xffffb02e), // accent
        juce::Colour (0xffff6a4a), // mute
        juce::Colour (0xffffd54a), // solo
        juce::Colour (0xff7fc84a), // meterLow
        juce::Colour (0xffe8c43a), // meterMid
        juce::Colour (0xfff08a2e), // meterHigh
        juce::Colour (0xffe8412e), // meterClip
        false, // isLight
        false,
        Theme::Look::VintageWarm,
    }, tinted (0xff4a3220, 0xff32200f, 0xff3e2a1a, 0xff28190c, 0xffd9c9a0, 0xff6b4e2e, 0xffffb02e, 0xffb89a6a, 0xfff0e0bc, 0xff6b4a2e, 0xff432c19, 0xfffff0d0, 0xfff0e0bc, 0xff2a1b12, 0xffd8c8a0, 0xff8a6a40, 0xff5a3e22, 0xff9a7a4e));
    return theme;
}

const Theme& Theme::daylight()
{
    static const Theme theme = withHardware ({
        juce::Colour (0xfffbfbfa), // background
        juce::Colour (0xffffffff), // panel
        juce::Colour (0xfff1f1ee), // raised
        juce::Colour (0xffe4e4e0), // well
        juce::Colour (0xff8a8f98), // outline
        juce::Colour (0xff0a0c10), // text
        juce::Colour (0xff3a414d), // textDim
        juce::Colour (0xffffffff), // textOnAccent
        juce::Colour (0xff0040c0), // accent
        juce::Colour (0xffb00020), // mute
        juce::Colour (0xff6a4400), // solo
        juce::Colour (0xff0a7a2e), // meterLow
        juce::Colour (0xff9a7a00), // meterMid
        juce::Colour (0xffc85a00), // meterHigh
        juce::Colour (0xffc80000), // meterClip
        true, // isLight
        false,
        Theme::Look::Daylight,
    }, daylightHardware());
    return theme;
}

const Theme& Theme::highContrast()
{
    static const Theme theme = withHardware ({
        juce::Colour (0xff000000), // background
        juce::Colour (0xff0a0a0a), // panel
        juce::Colour (0xff1c1c1c), // raised
        juce::Colour (0xff000000), // well
        juce::Colour (0xffffffff), // outline
        juce::Colour (0xffffffff), // text
        juce::Colour (0xffe0e0e0), // textDim
        juce::Colour (0xff000000), // textOnAccent
        juce::Colour (0xff00e5ff), // accent
        juce::Colour (0xffff6e6e), // mute
        juce::Colour (0xffffe000), // solo
        juce::Colour (0xff00ff40), // meterLow
        juce::Colour (0xffffff00), // meterMid
        juce::Colour (0xffff8000), // meterHigh
        juce::Colour (0xffff0000), // meterClip
        false, // isLight
        false,
        Theme::Look::HighContrast,
    }, highContrastHardware());
    return theme;
}

const Theme& Theme::studioSlate()
{
    static const Theme theme = withHardware ({
        juce::Colour (0xff1f262c), // background
        juce::Colour (0xff2e373f), // panel
        juce::Colour (0xff3f4a54), // raised
        juce::Colour (0xff151a1e), // well
        juce::Colour (0xff56626d), // outline
        juce::Colour (0xffeef2f5), // text
        juce::Colour (0xffb6c0c9), // textDim
        juce::Colour (0xff0e1215), // textOnAccent
        juce::Colour (0xff7cc4ff), // accent
        juce::Colour (0xffff8074), // mute
        juce::Colour (0xfff0c542), // solo
        juce::Colour (0xff46d36b), // meterLow
        juce::Colour (0xfff0c542), // meterMid
        juce::Colour (0xfff08a3a), // meterHigh
        juce::Colour (0xfff0483a), // meterClip
        false, // isLight
        false,
        Theme::Look::StudioSlate,
    }, slateHardware());
    return theme;
}

const Theme& Theme::studioGraphite()
{
    static const Theme theme = withHardware ({
        juce::Colour (0xff1c1d1f), // background
        juce::Colour (0xff2b2d30), // panel
        juce::Colour (0xff3b3e42), // raised
        juce::Colour (0xff131415), // well
        juce::Colour (0xff53575c), // outline
        juce::Colour (0xfff1f0ee), // text
        juce::Colour (0xffc0bdb8), // textDim
        juce::Colour (0xff140e04), // textOnAccent
        juce::Colour (0xffffb340), // accent
        juce::Colour (0xffff7a6e), // mute
        juce::Colour (0xffffd54a), // solo
        juce::Colour (0xff46d36b), // meterLow
        juce::Colour (0xfff0c542), // meterMid
        juce::Colour (0xfff08a3a), // meterHigh
        juce::Colour (0xfff0483a), // meterClip
        false, // isLight
        false,
        Theme::Look::StudioGraphite,
    }, graphiteHardware());
    return theme;
}

const Theme& Theme::studioOlive()
{
    static const Theme theme = withHardware ({
        juce::Colour (0xff1d2118), // background
        juce::Colour (0xff2f3626), // panel
        juce::Colour (0xff434b36), // raised
        juce::Colour (0xff161a11), // well
        juce::Colour (0xff5f6a4c), // outline
        juce::Colour (0xfff2f0e0), // text
        juce::Colour (0xffcdd0b8), // textDim
        juce::Colour (0xff16140a), // textOnAccent
        juce::Colour (0xffe0b84a), // accent
        juce::Colour (0xffff7f70), // mute
        juce::Colour (0xfff0d060), // solo
        juce::Colour (0xff46d36b), // meterLow
        juce::Colour (0xfff0c542), // meterMid
        juce::Colour (0xfff08a3a), // meterHigh
        juce::Colour (0xfff0483a), // meterClip
        false, // isLight
        false,
        Theme::Look::StudioOlive,
    }, oliveHardware());
    return theme;
}

namespace
{
    /** A console's theme: the dark (or, for a light console, the silver) base with the console's surface colours. */
    Theme consoleTheme (Theme::Look look, bool light, juce::uint32 background, juce::uint32 panel, juce::uint32 raised, juce::uint32 well, juce::uint32 outline,
                        juce::uint32 accent)
    {
        Theme t = light ? Theme::light() : Theme::charcoal();
        t.look = look;
        t.isLight = light;
        t.isCharcoal = ! light;
        t.background = juce::Colour (background);
        t.panel = juce::Colour (panel);
        t.raised = juce::Colour (raised);
        t.well = juce::Colour (well);
        t.outline = juce::Colour (outline);
        // The console's colour, moved (towards white on a dark theme, black on a light one) until it reads as text on the panel and the raised surface.
        auto a = juce::Colour (accent);
        for (int i = 0; i < 20 && (contrastRatio (a, t.panel) < 4.5f || contrastRatio (a, t.raised) < 4.5f); ++i)
            a = light ? a.darker (0.15f) : a.brighter (0.15f);
        t.accent = a;
        t.textOnAccent = Theme::textOn (a);
        t.hw.plateTop = t.raised;
        t.hw.plateBottom = t.panel;
        t.hw.trayTop = t.panel;
        t.hw.trayBottom = t.background;
        t.hw.pointer = a;
        t.hw.capLine = a;
        return t;
    }
} // namespace

std::optional<Theme::Look> Theme::consoleLook (int profile) noexcept
{
    if (profile < 1 || profile > kNumConsoleLooks)
        return std::nullopt;
    return (Look) ((int) Look::ConsoleWing + profile - 1);
}

const Theme& Theme::forConsole (juce::PropertiesFile& settings, int profile)
{
    if (const auto kept = settings.getValue (consoleSetting (profile)); kept.isNotEmpty())
        return named (kept);
    if (const auto look = consoleLook (profile))
        return of (*look);
    return fromSettings (settings);
}

const Theme& Theme::of (Look l)
{
    if (l >= Look::ConsoleWing)
    {
        // The colours of each console's surface (src/ui/*Surface.cpp).
        static const Theme consoles[kNumConsoleLooks] = {
            consoleTheme (Look::ConsoleWing, false, 0xff0a0a0b, 0xff16171a, 0xff26282c, 0xff060607, 0xff33363b, 0xff3fb6e8),
            consoleTheme (Look::ConsoleX32, false, 0xff17191c, 0xff1f2226, 0xff2e3237, 0xff0f1113, 0xff3a3d42, 0xffff9a3c),
            consoleTheme (Look::ConsoleProMix, false, 0xff1b1a19, 0xff242220, 0xff35322f, 0xff121110, 0xff45413d, 0xffe89030),
            consoleTheme (Look::Console02R, false, 0xff131415, 0xff1c1d1f, 0xff2c2e31, 0xff0d0e0f, 0xff46484b, 0xffebe5cd),
            consoleTheme (Look::ConsoleSq5, false, 0xff0b0b0c, 0xff17181a, 0xff27292c, 0xff070708, 0xff35383c, 0xffffa52e),
            consoleTheme (Look::Console01V, false, 0xff141413, 0xff1e1d1c, 0xff2f2d2b, 0xff0d0d0c, 0xff434140, 0xff4ef07a),
            consoleTheme (Look::ConsoleDLive, false, 0xff0b0b0c, 0xff1a1315, 0xff2b2023, 0xff070708, 0xff3a2020, 0xffff5a50),
            consoleTheme (Look::ConsoleDm2000, false, 0xff0d0e12, 0xff1c1710, 0xff2c241a, 0xff08090b, 0xff3a2a14, 0xffffc83a),
            consoleTheme (Look::ConsolePro2, false, 0xff0d0e12, 0xff181a1f, 0xff282b31, 0xff08090b, 0xff3a3d42, 0xff2a86c8),
            consoleTheme (Look::ConsoleXenyx, true, 0xffe8e9e6, 0xffd2d4d0, 0xffe5e6e3, 0xfff4f4f4, 0xffb4b5b3, 0xffe5352c),
            consoleTheme (Look::ConsolePluginHost, false, 0xff0c0e11, 0xff15181c, 0xff222730, 0xff08090b, 0xff323945, 0xff39d0c8)
        };
        return consoles[(int) l - (int) Look::ConsoleWing];
    }
    switch (l)
    {
        case Look::BrushedSilver: return light();
        case Look::MidnightBlue:  return midnightBlue();
        case Look::VintageWarm:   return vintageWarm();
        case Look::Daylight:      return daylight();
        case Look::HighContrast:  return highContrast();
        case Look::StudioSlate:   return studioSlate();
        case Look::StudioGraphite: return studioGraphite();
        case Look::StudioOlive:   return studioOlive();
        case Look::FlagshipBlack: break;
    }
    return charcoal();
}

const char* Theme::idOf (Look l)
{
    static const char* ids[] = { "flagship", "silver", "midnight", "vintage", "daylight", "contrast", "slate", "graphite", "olive",
                                 "c-wing", "c-x32", "c-promix", "c-02r", "c-sq5", "c-01v", "c-dlive", "c-dm2000", "c-pro2", "c-xenyx", "c-pluginhost" };
    return ids[(int) l];
}

const char* Theme::titleOf (Look l)
{
    static const char* titles[] = { "Flagship Black", "Brushed Silver", "Midnight Blue", "Vintage Warm", "Daylight", "High Contrast", "Studio Slate", "Studio Graphite", "Studio Olive",
                                    "WING", "X32", "ProMix 01", "02R", "SQ-5", "01V", "dLive", "DM2000", "PRO2", "Q1202USB", "PLUGIN HOST" };
    return titles[(int) l];
}

const Theme& Theme::named (const juce::String& name)
{
    const auto id = name.trim();
    if (id.equalsIgnoreCase ("light"))
        return light();
    for (int i = 0; i < kNumLooks + kNumConsoleLooks; ++i)
        if (id.equalsIgnoreCase (idOf ((Look) i)))
            return of ((Look) i);
    return charcoal();
}

const Theme& Theme::fromSettings (juce::PropertiesFile& settings)
{
    return named (settings.getValue (kSetting, "flagship"));
}

namespace type
{
    juce::Font font (float size, bool bold)
    {
        static std::map<std::pair<float, bool>, juce::Font> made;
        const auto key = std::make_pair (size, bold);
        auto it = made.find (key);
        if (it == made.end())
            it = made.emplace (key, juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain))).first;
        return it->second;
    }
} // namespace type

float contrastRatio (juce::Colour a, juce::Colour b) noexcept
{
    const auto luminance = [] (juce::Colour c)
    {
        const auto channel = [] (juce::uint8 v)
        {
            const double s = v / 255.0;
            return s <= 0.03928 ? s / 12.92 : std::pow ((s + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel (c.getRed()) + 0.7152 * channel (c.getGreen()) + 0.0722 * channel (c.getBlue());
    };
    const double la = luminance (a), lb = luminance (b);
    return (float) ((juce::jmax (la, lb) + 0.05) / (juce::jmin (la, lb) + 0.05));
}

} // namespace livedaw::ui::universal
