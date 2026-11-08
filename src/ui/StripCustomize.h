#pragma once

#include "engine/MixerEngine.h"

/** Naming and colouring a strip as the console does it, for every interface
    (the WING's HOME > NAME and ICON/COLOR, the Universal Mixer's STRIP page):
    while LINK CUSTOMIZATION TO SOURCE is on (an input or aux channel with a
    source), the source is renamed or recoloured, which the strip then shows;
    otherwise the strip itself. */
namespace livedaw::ui::customize
{

/** Names the strip, or its source while linked. */
inline void rename (MixerEngine& engine, Strip& strip, const juce::String& name)
{
    if (const int index = strip.linkedSourceIndex(); index >= 0)
        engine.setSourceName (index, name);
    else
        strip.setName (name);
}

/** Colours the strip (a palette index, 1-based), or its source while linked. */
inline void setColour (Strip& strip, int colour)
{
    if (auto* src = strip.linkedSource())
        src->colour = palette::clamp (colour);
    else
        strip.setColourIndex (colour);
}

/** Whether a change goes to the strip's source (the link is on and it has one). */
inline bool goesToSource (const Strip& strip) { return strip.linkedSourceIndex() >= 0; }

} // namespace livedaw::ui::customize
