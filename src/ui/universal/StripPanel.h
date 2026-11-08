#pragma once

#include "ui/universal/Groups.h"

namespace livedaw::ui::universal
{

/** The inspector's STRIP page: the selected strip's NAME (RENAME), its COLOUR
    (the console's palette), the DCAs and the mute groups it is a member of.
    While LINK CUSTOMIZATION TO SOURCE is on, the name and colour are its
    source's, as on the console (the page says so). Scrolls when a console has
    many groups. */
class StripPanel final : public juce::Component, private juce::Timer
{
public:
    StripPanel (UiContext&, const Theme&);

    void setStrip (Strip*);
    Strip* getStrip() const noexcept { return strip; }
    void refresh();

    /** Renames the strip (or its source while linked), as RENAME does. */
    void rename (const juce::String&);
    /** Colours the strip (or its source while linked): a palette index, 1-based. */
    void chooseColour (int colour);

    ToggleKey renameKey;
    GroupGrid dcas, muteGroups;

    /** The height the page needs at a width (its scroller sizes it so). */
    int heightFor (int width) const;
    /** The colour swatch of palette index `colour` (1-based), in this component. */
    juce::Rectangle<int> swatchArea (int colour) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override { refresh(); }

    UiContext& ctx;
    const Theme& theme;
    Strip* strip = nullptr;
    juce::String name;
    int colour = 0;
    bool linked = false;
    juce::Rectangle<int> nameArea, colourCaption, coloursArea, dcaCaption, muteCaption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripPanel)
};

} // namespace livedaw::ui::universal
