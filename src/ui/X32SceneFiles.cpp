#include "ui/X32SceneFiles.h"

#include "engine/EventLog.h"
#include "engine/MixerEngine.h"
#include "engine/Show.h"
#include "engine/WingRemote.h"

namespace livedaw::ui::x32
{

juce::String SceneFiles::importFile (const juce::File& file)
{
    juce::String text;
    if (! file.existsAsFile() || (text = file.loadFileAsString()).isEmpty())
        return "Couldn't read " + file.getFileName();
    const auto read = ctx.engine.getWing().readX32Scene (text);
    if (! read.ok)
        return "The console isn't an X32";
    if (read.applied == 0)
        return file.getFileName() + " has nothing an X32 scene has";
    const auto name = read.name.isNotEmpty() ? read.name : file.getFileNameWithoutExtension();
    const int index = ctx.show.addScene (name);
    juce::String status = "Imported " + file.getFileName() + " as scene " + juce::String (index + 1) + " (" + name + ")";
    if (read.skipped > 0)
        status << "; " << read.skipped << " line(s) Sidecar doesn't emulate were skipped";
    ctx.log.info ("X32 scene: " + status);
    return status;
}

juce::String SceneFiles::exportFile (const juce::File& file)
{
    auto& show = ctx.show;
    const int current = show.getCurrentSceneIndex();
    const auto scene = current >= 0 ? show.getScene (current) : std::nullopt;
    const auto name = scene.has_value() ? scene->name : file.getFileNameWithoutExtension();
    const auto text = ctx.engine.getWing().writeX32Scene (name, {});
    if (text.isEmpty())
        return "The console isn't an X32";
    if (! file.replaceWithText (text, false, false, "\n"))
        return "Couldn't write " + file.getFullPathName();
    ctx.log.info ("X32 scene exported: " + file.getFullPathName());
    return "Exported " + file.getFileName();
}

void SceneFiles::importScene (std::function<void (const juce::String&)> done)
{
    chooser = std::make_unique<juce::FileChooser> ("Import an X32 scene", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                   "*.scn");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, done] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file != juce::File() && done)
                                  done (importFile (file));
                          });
}

void SceneFiles::exportScene (std::function<void (const juce::String&)> done)
{
    const auto current = ctx.show.getCurrentSceneIndex();
    const auto scene = current >= 0 ? ctx.show.getScene (current) : std::nullopt;
    const auto name = juce::File::createLegalFileName (scene.has_value() && scene->name.isNotEmpty() ? scene->name : juce::String ("Sidecar"));
    chooser = std::make_unique<juce::FileChooser> ("Export an X32 scene",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name + ".scn"),
                                                   "*.scn");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, done] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file != juce::File() && done)
                                  done (exportFile (file.withFileExtension ("scn")));
                          });
}

} // namespace livedaw::ui::x32
