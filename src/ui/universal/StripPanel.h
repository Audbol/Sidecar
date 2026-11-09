#pragma once

#include "ui/universal/Groups.h"
#include "ui/universal/ValueControls.h"

namespace livedaw::ui::universal
{

/** The inspector's STRIP page: the selected strip's NAME (RENAME), its COLOUR
    (the console's palette), the DCAs and the mute groups it is a member of,
    and its channel preset (SAVE PRESET / LOAD PRESET: .lchn files in the
    Library folder, as the WING's LIBRARY > CHAN keeps them).
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

    /** Saves the strip as a channel preset (every section but routing, as the
        WING's LIBRARY does), named after the file; loads one onto it. */
    juce::Result savePreset (const juce::File&);
    juce::Result loadPreset (const juce::File&);

    ToggleKey renameKey, savePresetKey, loadPresetKey;
    GroupGrid dcas, muteGroups;
    /** A dLive mix's Ext In (its socket, trim, polarity) and, on its Groups, Aux and FX
        sends, Mix Source. Hidden on anything else. */
    ChoiceBar extIn, extTrim, mixSource;
    ToggleKey extPolarity;
    bool showsMixInput() const;
    /** A dLive input channel's AMM: which it's in, ON, CHAIR, SOLO (NOM) or PRIORITY (D-Classic). */
    ChoiceBar amm;
    ToggleKey ammOn, ammChair, ammSolo;
    ValueBar ammPriority;
    bool showsAmm() const;

    /** The height the page needs at a width (its scroller sizes it so). */
    int heightFor (int width) const;
    /** The colour swatch of palette index `colour` (1-based), in this component. */
    juce::Rectangle<int> swatchArea (int colour) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override { refresh(); }
    std::unique_ptr<juce::FileChooser> chooser;

    UiContext& ctx;
    const Theme& theme;
    Strip* strip = nullptr;
    juce::String name;
    int colour = 0;
    bool linked = false;
    juce::Rectangle<int> nameArea, colourCaption, coloursArea, dcaCaption, muteCaption, presetCaption, mixCaption, ammCaption;
    float shownAmmGain = 0.0f;
    bool shownAmmNom = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripPanel)
};

} // namespace livedaw::ui::universal
