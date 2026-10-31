#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <vector>

namespace livedaw::ui
{

/** Pictures of the plugins' editors for the plugin overview
    (PluginOverviewWindow). A plugin draws its editor only while it is open, so
    its picture is the last one taken: the plugin windows (PluginChainWindow)
    take one of each plugin they show shortly after opening and every few
    seconds after that, and CAPTURE ALL opens each slot's window long enough to
    take them.

    grab() reads what an editor shows. On Windows it asks the window to draw
    itself into a bitmap (PrintWindow with PW_RENDERFULLCONTENT), which takes in
    native plugin views (their own child windows, GPU-drawn ones included) and
    works while other windows cover it; elsewhere, and if that fails, it takes
    a component snapshot (JUCE-drawn editors). Message thread only. */
class PluginPictures
{
public:
    struct Picture
    {
        juce::Image image;
        juce::Time taken;
        juce::String name; // the plugin's, when taken
    };

    /** Keeps `image` as `plugin`'s picture (an empty image is ignored). */
    void store (const juce::AudioProcessor& plugin, juce::Image image);
    /** `plugin`'s last picture, or nullptr (also when a plugin of another name
        now lives at the address of the one pictured). */
    const Picture* find (const juce::AudioProcessor* plugin) const;
    /** `plugin` is about to be destroyed (its address may be reused). */
    void forget (const juce::AudioProcessor* plugin);
    /** Forgets the pictures of every plugin but `live` (the ones in the FX slots). */
    void retainOnly (const std::vector<const juce::AudioProcessor*>& live);
    /** Goes up whenever a picture is stored or forgotten. */
    int getGeneration() const noexcept { return generation; }
    int size() const noexcept { return (int) pictures.size(); }

    /** What `editor` shows on screen (only its visible part), at most `maxWidth`
        px wide; empty if it isn't showing. */
    static juce::Image grab (juce::Component& editor, int maxWidth = 720);

private:
    std::map<const juce::AudioProcessor*, Picture> pictures;
    int generation = 0;
};

} // namespace livedaw::ui
