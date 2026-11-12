#pragma once

#include "ui/UiContext.h"

#include <array>
#include <functional>
#include <memory>

/** The window Sidecar opens with: continue where the user left off (the console
    the autosave restored), or start a fresh console of one of the mixers. The
    mixers' names and how finished they are, here and in SETUP > SIDECAR > INTERFACE. */
namespace livedaw::ui::mixerChooser
{

struct Mixer
{
    InterfaceKind kind;
    const char* name;
    /** How finished it is (empty: nothing to say). */
    const char* note;
};

/** The mixers, in InterfaceKind's order. */
const std::array<Mixer, 12>& mixers();

/** A mixer's name with its note: "X32". */
juce::String label (InterfaceKind);

/** The INTERFACE keys' labels (ChoiceKeys: the name, the note on a second line). */
juce::StringArray keyLabels();

/** What the window does. */
struct Options
{
    /** Offers to continue (the autosave restored a console), with what it was
        in `continueText`. Empty: there is nothing to continue. */
    juce::String continueText;
    /** CONTINUE or Esc: the console stays as it is. */
    std::function<void()> onContinue;
    /** A picture of a console's surface for its tile, at most `maxWidth` wide (a null image: draw
        a plain tile with the name). Asked once per console, then cached. Optional. */
    std::function<juce::Image (InterfaceKind, int maxWidth)> picture;
    /** A mixer was chosen. */
    std::function<void (InterfaceKind)> onMixer;
};

/** The window, to lay over the whole console: a grid of tiles (CONTINUE above it), scrolling when they
    do not fit. Arrow keys move, Enter picks, Esc continues. It takes the keyboard when shown. */
std::unique_ptr<juce::Component> create (Options);

} // namespace livedaw::ui::mixerChooser
