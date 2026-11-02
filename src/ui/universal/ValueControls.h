#pragma once

#include "engine/Models.h"
#include "ui/universal/Widgets.h"

#include <memory>
#include <vector>

namespace livedaw::ui::universal
{

/** A value as a bar: its caption at the left, its value at the right, filled to
    where it stands. A drag moves it by how far it travels (Shift: a quarter as
    far), a double tap sets its default, the wheel steps it. */
class ValueBar final : public juce::Component
{
public:
    explicit ValueBar (const Theme&);

    /** What the bar shows and moves: a value, its scale (to and from 0..1) and its text. */
    struct Binding
    {
        juce::String caption;
        std::atomic<float>* value = nullptr;
        std::function<float (float value)> toPosition;
        std::function<float (float position)> fromPosition;
        std::function<juce::String (float value)> text;
        float defaultValue = 0.0f;
    };

    /** Linear from `min` to `max`, its text with `decimals` and `unit`. */
    static Binding linear (const juce::String& caption, std::atomic<float>&, float min, float max, float defaultValue,
                           const juce::String& unit, int decimals);
    /** A frequency, on a log scale. */
    static Binding frequency (const juce::String& caption, std::atomic<float>&, float min, float max, float defaultValue);
    /** A level on the fader law (-oo to +10 dB). */
    static Binding level (const juce::String& caption, std::atomic<float>&);
    /** A model parameter, scaled and printed as its table says. */
    static Binding param (std::atomic<float>&, const models::Param&);

    void bind (Binding);
    void unbind();
    float getPosition() const noexcept { return position; }
    juce::String getText() const { return shownText; }
    void setPositionFromUser (float newPosition);
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    const Theme& theme;
    Binding binding;
    float position = 0.0f, dragStart = 0.0f;
    juce::String shownText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValueBar)
};

/** A choice: its caption and the chosen item; a tap lists the items. */
class ChoiceBar final : public juce::Component
{
public:
    explicit ChoiceBar (const Theme&);

    juce::String caption;
    /** The console the items come from (devices::wing), shown as a tag after the caption. */
    juce::String device;
    std::function<juce::StringArray()> items;
    /** The chosen item's index (-1: none). */
    std::function<int()> current;
    std::function<void (int index)> choose;

    /** Chooses item `index`, as a tap on it in the list does. */
    void chooseItem (int index);
    juce::String getText() const { return shownText; }
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    const Theme& theme;
    juce::String shownText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoiceBar)
};

/** A frequency response, 20 Hz to 20 kHz over +-`rangeDb`, from `response` (dB
    at the frequencies asked). */
class ResponseView final : public juce::Component
{
public:
    explicit ResponseView (const Theme&);

    std::function<bool (const double* freqs, double* outDb, int count)> response;
    float rangeDb = 18.0f;
    /** The curve's dB at its points (empty without a response). */
    const std::vector<double>& getCurve() const noexcept { return curve; }
    void refresh();

    void paint (juce::Graphics&) override;

private:
    const Theme& theme;
    std::vector<double> freqs, curve;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResponseView)
};

/** The parameters of the model a block holds, from the model tables the DSP
    runs: bars for values, keys for switches, lists for choices. It rebuilds
    itself when the block gets another model. */
class ParamPanel final : public juce::Component
{
public:
    explicit ParamPanel (const Theme&);

    /** The block's model and its parameter values (nullptr: none). */
    void bind (models::Table, std::atomic<int32_t>* model, std::atomic<float>* values, int numValues);
    int getNumControls() const noexcept { return (int) controls.size(); }
    juce::Component* getControl (int index) const noexcept;
    /** Its height for the controls it holds at `width`. */
    int getHeightFor (int width) const noexcept;
    void refresh();

    void resized() override;

private:
    void rebuild();

    const Theme& theme;
    models::Table table = models::Dynamics;
    std::atomic<int32_t>* model = nullptr;
    std::atomic<float>* values = nullptr;
    int numValues = 0, builtModel = -1;
    std::vector<std::unique_ptr<juce::Component>> controls;
    std::vector<std::function<void()>> refreshers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamPanel)
};

/** Rows of controls at their heights, top to bottom (an editor's layout). */
int layoutRows (juce::Rectangle<int> area, const std::vector<std::pair<juce::Component*, int>>& rows, int gap);

} // namespace livedaw::ui::universal
