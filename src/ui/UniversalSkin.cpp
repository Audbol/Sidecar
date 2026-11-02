#include "ui/UniversalSkin.h"

namespace livedaw::ui
{

namespace
{
    // The Universal Mixer's first colours (its theme comes with its widgets, Phase 2 step 3).
    const juce::Colour background { 0xff15171c }, text { 0xffe6e8ee }, dim { 0xff9aa0ad }, accent { 0xff4c8dff };
} // namespace

UniversalSkin::UniversalSkin (UiContext& c) : ctx (c)
{
    wingButton.setColour (juce::TextButton::buttonColourId, accent);
    wingButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    // Switching rebuilds the window's skin: let the click return first.
    wingButton.onClick = [&c = ctx]
    {
        juce::MessageManager::callAsync ([&c]
        {
            if (c.setInterface)
                c.setInterface (InterfaceKind::Wing);
        });
    };
    addAndMakeVisible (wingButton);
}

void UniversalSkin::paint (juce::Graphics& g)
{
    g.fillAll (background);
    auto area = getLocalBounds().reduced (48).withSizeKeepingCentre (juce::jmin (getWidth() - 96, 760), juce::jmin (getHeight() - 96, 420));
    g.setColour (text);
    g.setFont (juce::FontOptions (34.0f, juce::Font::bold));
    g.drawText ("Universal Mixer", area.removeFromTop (48), juce::Justification::centredLeft);
    g.setColour (accent);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("PREVIEW", area.removeFromTop (26), juce::Justification::centredLeft);
    area.removeFromTop (18);

    const auto n = ctx.engine.getCounts();
    const auto counts = juce::String (n.channels) + " channels, " + juce::String (n.aux) + " aux, " + juce::String (n.buses) + " buses, "
                        + juce::String (n.mains) + (n.mains == 1 ? " main, " : " mains, ") + juce::String (n.matrices) + " matrices, "
                        + juce::String (n.dcas) + " DCAs, " + juce::String (n.muteGroups) + " mute groups and " + juce::String (n.fx) + " FX slots.";
    g.setColour (text);
    g.setFont (juce::FontOptions (19.0f));
    g.drawFittedText ("The console is running: " + counts, area.removeFromTop (60), juce::Justification::topLeft, 3);
    g.setColour (dim);
    g.setFont (juce::FontOptions (17.0f));
    g.drawFittedText ("This interface is being built (Phase 2 of the Universal Mixer: mixer, chains, routing, FX, monitoring, "
                      "scenes, recorder and setup views). Shows, scenes, the recorder, plugins and the remotes keep working meanwhile.",
                      area.removeFromTop (80), juce::Justification::topLeft, 4);
}

void UniversalSkin::resized()
{
    // Touch-sized (at least 48 px tall), under the text.
    auto area = getLocalBounds().reduced (48).withSizeKeepingCentre (juce::jmin (getWidth() - 96, 760), juce::jmin (getHeight() - 96, 420));
    wingButton.setBounds (area.removeFromBottom (56).removeFromLeft (juce::jmin (area.getWidth(), 300)));
}

} // namespace livedaw::ui
