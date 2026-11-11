#pragma once

#include "ui/universal/Widgets.h"

namespace livedaw::ui
{

/** The top bar a console's surface shares with the Universal Mixer's swap: the console's
    name at the left, the generic "not affiliated" line, and the UNIVERSAL MIXER key at the
    right. One click, no dialog: the Universal Mixer takes the console as it is. */
namespace surfacetopbar
{
    constexpr int kHeight = 40;

    inline void paint (juce::Graphics& g, const universal::Theme& theme, juce::Rectangle<int> area, const juce::String& name)
    {
        const auto top = area.removeFromTop (kHeight).toFloat();
        g.setColour (theme.panel);
        g.fillRect (top);
        g.setColour (theme.text);
        g.setFont (universal::type::font (universal::type::kTitle, true));
        g.drawText (name, top.reduced ((float) universal::sizes::kGapLarge, 0.0f), juce::Justification::centredLeft);
        g.setColour (theme.textDim);
        g.setFont (universal::type::font (universal::type::kCaption));
        g.drawText ("Not affiliated with or endorsed by any console maker; product names are their owners' trademarks.",
                    top.reduced ((float) universal::sizes::kGapLarge, 0.0f).withTrimmedLeft (juce::jmax (150.0f, (float) name.length() * 17.0f + 40.0f)).withTrimmedRight (210.0f), juce::Justification::centredLeft);
    }

    inline void place (juce::Component& key, int width)
    {
        key.setBounds (juce::Rectangle<int> (width - 190, 0, 190, kHeight).reduced (universal::sizes::kGap, 5));
    }
}

} // namespace livedaw::ui
