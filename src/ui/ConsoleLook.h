#pragma once

#include "engine/ConsoleModel.h"

#include <juce_data_structures/juce_data_structures.h>

namespace livedaw::ui
{

/** The control surface's finish: Behringer makes the full-size WING in
    silver and in black (WING BK). Only the surface view's hardware colours
    differ; the layout, the print and the touchscreen are the same. */
enum class SurfaceFinish
{
    Silver,
    Black
};

/** How the console looks (SETUP > GENERAL > CONSOLE), kept in the app
    settings rather than in the show: a preference of the user, not of the mix.
    The MODEL is the full-size WING, the WING COMPACT or the WING RACK; the
    COMPACT and the RACK come in black only, so they show the black finish
    whatever the finish setting says. */
struct ConsoleLook
{
    static constexpr const char* finishKey = "ui.surfaceFinish"; // "SILVER" (default) or "BLACK"
    static constexpr const char* modelKey = "ui.consoleModel";   // "FULL" (default), "COMPACT" or "RACK"

    SurfaceFinish finish = SurfaceFinish::Silver;
    ConsoleModel model = ConsoleModel::Full;

    static juce::String finishName (SurfaceFinish f) { return f == SurfaceFinish::Black ? "BLACK" : "SILVER"; }

    /** The finish the surface shows: the COMPACT's and the RACK's is black. */
    SurfaceFinish shownFinish() const noexcept { return isSmallModel (model) ? SurfaceFinish::Black : finish; }

    static ConsoleLook load (const juce::PropertiesFile& settings)
    {
        ConsoleLook look;
        look.finish = settings.getValue (finishKey) == finishName (SurfaceFinish::Black) ? SurfaceFinish::Black : SurfaceFinish::Silver;
        look.model = consoleModelFromName (settings.getValue (modelKey));
        return look;
    }

    void save (juce::PropertiesFile& settings) const
    {
        settings.setValue (finishKey, finishName (finish));
        settings.setValue (modelKey, consoleModelName (model));
    }
};

} // namespace livedaw::ui
