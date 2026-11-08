#pragma once

#include "ui/UiContext.h"

#include <memory>

namespace livedaw::ui::x32
{

/** X32 scene files (`.scn`, Phase 4 step 7) for the X32 surface's LIBRARY and
    the Universal Mixer's SCENES: IMPORT sets the console from one and keeps it
    as a new scene of the show (named as the file's header names it); EXPORT
    writes the console as one, an X32 can load it (what Sidecar doesn't emulate,
    such as the outputs and the head amps, is left out). */
class SceneFiles
{
public:
    explicit SceneFiles (UiContext& c) : ctx (c) {}

    /** Asks for a file, then imports or exports it; `done` gets what happened. */
    void importScene (std::function<void (const juce::String&)> done);
    void exportScene (std::function<void (const juce::String&)> done);

    /** Without asking (and for tests): what happened, as `done` gets it. */
    juce::String importFile (const juce::File&);
    juce::String exportFile (const juce::File&);

    /** X32 channel presets (`.chn`) for the LIBRARY's channel tab: IMPORT loads
        one onto the strip at an X32 address ("/ch/01"...; its source stays),
        EXPORT writes that strip as one, named `name`. */
    void importChannel (const juce::String& strip, std::function<void (const juce::String&)> done);
    void exportChannel (const juce::String& strip, const juce::String& name, std::function<void (const juce::String&)> done);
    juce::String importChannelFile (const juce::File&, const juce::String& strip);
    juce::String exportChannelFile (const juce::File&, const juce::String& strip, const juce::String& name);

private:
    UiContext& ctx;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace livedaw::ui::x32
