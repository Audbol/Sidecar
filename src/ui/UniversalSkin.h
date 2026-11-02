#pragma once

#include "ui/Skin.h"
#include "ui/UiContext.h"

namespace livedaw::ui
{

/** The Universal Mixer's interface (docs/UNIVERSAL_MIXER.md, Phase 2): a
    preview while it is being built. For now it shows the console it runs and
    the way back to the WING surface; the mixer, chain, routing, FX, monitoring,
    scenes, recorder and setup views follow step by step. */
class UniversalSkin final : public Skin
{
public:
    explicit UniversalSkin (UiContext&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshAll() override { repaint(); }

private:
    UiContext& ctx;
    juce::TextButton wingButton { "Back to the WING surface" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UniversalSkin)
};

} // namespace livedaw::ui
