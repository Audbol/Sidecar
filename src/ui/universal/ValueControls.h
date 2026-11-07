#pragma once

#include "engine/Models.h"
#include "engine/Parameters.h"
#include "ui/universal/Widgets.h"

#include <functional>
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
    /** Draws item `index`'s picture (a model's front panel): the list then opens as a
        grid of pictures (PictureGrid) instead of a menu of names. */
    std::function<void (juce::Graphics&, juce::Rectangle<float>, int index)> paintItem;

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

/** A list of pictures to choose from (a model's front panel each, with its name
    under it), the current one outlined; a tap chooses. In a viewport when long. */
class PictureGrid final : public juce::Component
{
public:
    PictureGrid (const Theme&, juce::StringArray names, int current, std::function<void (juce::Graphics&, juce::Rectangle<float>, int)> paintItem,
                 std::function<void (int)> chosen);

    static constexpr int kColumns = 5, kTileWidth = 150, kTileHeight = 112;
    int getNumItems() const noexcept { return names.size(); }
    juce::Rectangle<int> tileBounds (int index) const;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    const Theme& theme;
    juce::StringArray names;
    int current;
    std::function<void (juce::Graphics&, juce::Rectangle<float>, int)> paintItem;
    std::function<void (int)> chosen;
};

/** A frequency response, 20 Hz to 20 kHz over +-`rangeDb`, from `response` (dB
    at the frequencies asked), with handles to set it right on the curve: an EQ's
    bands (drag: frequency and gain; the wheel: Q; a double tap: back to its
    default) and a filter's cuts (drag sideways). */
class ResponseView final : public juce::Component
{
public:
    explicit ResponseView (const Theme&);

    std::function<bool (const double* freqs, double* outDb, int count)> response;
    float rangeDb = 18.0f;

    /** A handle on the curve: an EQ band or a filter's cut (no gain: sideways only). */
    struct Handle
    {
        juce::String label;
        std::atomic<float>* freq = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        float minFreq = 20.0f, maxFreq = 20000.0f, defaultFreq = 1000.0f;
        float minGain = -15.0f, maxGain = 15.0f, defaultGain = 0.0f;
        float minQ = 0.4f, maxQ = 10.0f, defaultQ = 1.0f;
        /** Shown and movable (a cut that is on); always when unset. */
        std::function<bool()> active;
    };
    /** The handles, taken again at every refresh (a block given another model has other bands). */
    std::function<std::vector<Handle>()> handleSource;
    /** Called after a handle moved something. */
    std::function<void()> onEdited;

    /** The handles of an EQ model's bands: its <band>f, <band>g and <band>q parameters. */
    static std::vector<Handle> eqHandles (const models::Model&, std::atomic<float>* values);

    /** The curve's dB at its points (empty without a response). */
    const std::vector<double>& getCurve() const noexcept { return curve; }
    const std::vector<Handle>& getHandles() const noexcept { return handles; }
    /** Where handle `i` is drawn (for the tests: press there). */
    juce::Point<float> handlePosition (int i) const;
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> plotArea() const;
    float xAt (double hz) const;
    float yAt (double db) const;
    double hzAt (float x) const;
    double dbAt (float y) const;
    bool isActive (const Handle&) const;
    int handleAt (juce::Point<float>) const;

    const Theme& theme;
    std::vector<double> freqs, curve;
    std::vector<Handle> handles;
    juce::String shownActive;
    int dragging = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResponseView)
};

/** A dynamics block's static curve (output against input level, -60 to 0 dB) as
    its model works, with handles: the threshold (drag it along the curve) and the
    ratio, or a gate's range (drag the handle at 0 dB up or down). The gain
    reduction shows as a bar at the right. */
class TransferView final : public juce::Component
{
public:
    explicit TransferView (const Theme&);

    /** The block (a model of the dynamics table and its values); nullptr: none. */
    void bind (DynSlotParams*);
    std::function<float()> reductionDb;
    std::function<void()> onEdited;

    void refresh();
    /** Where the threshold (0) and ratio / range (1) handles are drawn; empty if the model has none. */
    juce::Point<float> handlePosition (int which) const;
    float getCurveAt (float inDb) const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plotArea() const;
    float xAt (float db) const;
    float yAt (float db) const;
    float dbAtX (float x) const;
    float outAt (float inDb) const;
    void computeCurve();

    const Theme& theme;
    DynSlotParams* params = nullptr;
    int thrIndex = -1, slopeIndex = -1; // the threshold's, and the ratio's or range's parameter
    int curveModel = -1;                // the model the curve and those indexes were made for
    /** The ratio's or range's parameter of curveModel (none if it has none). */
    const models::Param* slopeParam() const;
    std::vector<float> inDb, outDb;
    float reduction = 0.0f;
    int dragging = -1;
    float dragStartY = 0.0f, dragStartPos = 0.0f;
    std::vector<float> shownValues;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransferView)
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
