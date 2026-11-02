#include "ui/universal/Theme.h"

#include <cmath>
#include <map>

namespace livedaw::ui::universal
{

juce::Colour Theme::kindColour (StripKind kind) const noexcept
{
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

const Theme& Theme::dark()
{
    static const Theme theme {
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
    };
    return theme;
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
