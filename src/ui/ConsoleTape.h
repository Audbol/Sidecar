#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace livedaw::ui
{

/** A strip of console tape: the white tape an engineer sticks under a channel and
    writes its name on in marker. Sidecar's own addition for analog mixers (the real
    mixers have none); built to be reused by every one of them. The tape is a little
    crooked and its ends are uneven, differently for each `seed` (the channel's
    number), so a row of them reads as stuck on by hand. The name is drawn in a
    handwriting font (Patrick Hand, OFL); a click opens it for editing, Return or
    leaving the box keeps it, Escape drops the edit. The tape holds no name of its
    own: `getText` and `setText` reach the strip's (its normal name, so it is saved
    with the show). */
class ConsoleTape final : public juce::Component
{
public:
    explicit ConsoleTape (int seed);

    std::function<juce::String()> getText;
    std::function<void (const juce::String&)> setText;

    /** Reads the name again; repaints when it changed. */
    void refresh();
    /** Opens the name for editing. */
    void beginEdit();
    /** Keeps (or, `keep` false, drops) the edit. */
    void endEdit (bool keep);
    bool isEditing() const noexcept { return editor.isVisible(); }
    /** The editor, for tests. */
    juce::TextEditor& getEditor() noexcept { return editor; }

    /** The tape's tilt in degrees: always a little, never the same for two seeds in a row. */
    float getTiltDegrees() const noexcept { return tilt; }
    /** The handwriting, `height` high. */
    static juce::Font hand (float height);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Path outline() const;

    juce::TextEditor editor;
    juce::String shown;
    float tilt = 0.0f, textTilt = 0.0f;
    float jitter[8] {}; // the ends: how far each corner and torn notch sits from square
};

} // namespace livedaw::ui
