#pragma once

#include "ui/universal/BlockEditor.h"

#include <vector>

namespace livedaw::ui::universal
{

/** The picture of one block, whatever console it comes from: an EQ, filter or graphic EQ's magnitude
    curve (GEQ as bars too), a dynamics block's transfer curve with its gain reduction, the input's trim
    scale and polarity, the delay's time, an insert's send/return with its device, an FX device's name,
    key values and category glyph. All the engine's: the curves from the block's own DSP as set
    (the strip's ld_strip_response / ld_strip_block_response, the models' ld_model_response and
    ld_dyn_static_curve). Display only: it holds what it last took and paints that. */
class BlockPicture
{
public:
    /** Takes what the block holds now, curves from the engine at `freqs`. True if the picture changed. */
    bool update (UiContext&, const BlockTarget&, const std::vector<double>& freqs);
    /** Paints it into `area`; `reduction` is the block's gain reduction in dB (positive). */
    void paint (juce::Graphics&, juce::Rectangle<float> area, const BlockTarget&, const Theme&, bool on, float reduction) const;

    /** A device's category, as the glyph shows it. */
    enum class Glyph
    {
        None,
        Reverb,
        Delay,
        Modulation,
        Pitch,
        Dynamics,
        Eq,
        Amp
    };
    /** The glyph for a model's name and category. */
    static Glyph glyphFor (const juce::String& name, models::Category);
    static void drawGlyph (juce::Graphics&, juce::Rectangle<float>, Glyph, juce::Colour);

    /** What it drew: nothing at all if the block gave it nothing (the tests ask). */
    bool hasContent() const noexcept { return ! curve.empty() || ! transfer.empty() || ! live.empty() || name.isNotEmpty(); }

private:
    std::vector<double> curve;    // an EQ's, a filter's, a key filter's, an FX device's response (dB)
    std::vector<float> transfer;  // a dynamics block's output level for the inputs -60..0 dB
    std::vector<float> live;      // the scalars shown (trim, balance, polarity, delay, ...)
    bool graphic = false;         // a graphic EQ: bars too
    juce::String name;            // an FX device's or insert slot's
    juce::StringArray values;     // its key parameters
    Glyph glyph = Glyph::None;
    int delayMode = 2;
};

} // namespace livedaw::ui::universal
