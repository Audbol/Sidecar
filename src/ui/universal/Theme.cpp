#include "ui/universal/Theme.h"

#include <cmath>
#include <map>

namespace livedaw::ui::universal
{

juce::Colour Theme::kindColour (StripKind kind) const noexcept
{
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
    }, brushedSilver());
    return theme;
}

const Theme& Theme::named (const juce::String& name)
{
    return name.trim().equalsIgnoreCase ("light") ? light() : charcoal();
}

const Theme& Theme::fromSettings (juce::PropertiesFile& settings)
{
    return named (settings.getValue (kSetting, "dark"));
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
