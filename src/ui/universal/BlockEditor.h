#pragma once

#include "ui/UiContext.h"
#include "ui/universal/ValueControls.h"

namespace livedaw::ui::universal
{

/** What a block editor edits: one of a strip's blocks, its own (the WING
    template's) or an extra one, with what it needs to show it. */
struct BlockTarget
{
    enum class Kind
    {
        None,
        Input,
        Filter,
        Eq,
        Dynamics,
        Insert,
        Delay
    };

    Kind kind = Kind::None;
    juce::String title;
    /** The console the block comes from: the strip's own blocks are its WING
        template's (devices::shown()); an extra block, none (the Universal core). */
    juce::String device;
    /** An extra block's id, else -1. */
    int uid = -1;

    // The block's values, by kind.
    InputParams* input = nullptr;
    FilterParams* filter = nullptr;
    EqBlockParams* eq = nullptr;
    DynSlotParams* dynamics = nullptr;
    InsertParams* insert = nullptr;
    bool postInsert = false; // INS 2: its mode (FX, AUTO X, AUTO Y) too
    std::atomic<bool>* delayOn = nullptr;
    std::atomic<int32_t>* delayMode = nullptr;
    std::atomic<float>* delay = nullptr;

    /** Its model table and how to load a model with its defaults (EQ, dynamics, the filter's tool). */
    models::Table table = models::Dynamics;
    std::function<void (int model)> setModel;
    /** A dynamics block's place, for what it may hold (ld_dyn_model_allowed: 0 the gate slot, 1 a
        dynamics slot or block, 2 a dLive's preamp block); -1: any model. */
    int modelSlot = -1;
    /** Its model is the strip's for good (a 01V's EQs: 4-band, or 2-band on channels 17-24; a dLive's PEQ). */
    bool fixedModel = false;
    /** A compressor that goes before or after the PEQ, the strip's process order (a dLive input's). */
    std::atomic<int32_t>* compressorOrder = nullptr;
    /** Its curve: an EQ's or a filter's, a dynamics block's key filter. */
    std::function<bool (const double* freqs, double* outDb, int count)> response;
    /** A dynamics block's gain reduction, dB. */
    std::function<float()> reductionDb;

    /** The console tag to show for the block now: its model's console when that is not the shown
        console's (a WING EQ on a ProMix; `devices::of`), else `device`. */
    juce::String tag() const;

    /** A step of `strip`'s chain as a target (Kind::None for a step without an editor). */
    static BlockTarget of (UiContext&, Strip&, const ChainStep&);
};

/** The editor of one of a strip's blocks: its switches, its model and its
    parameters (from the model tables), its curve and gain reduction. */
class BlockEditor final : public juce::Component
{
public:
    BlockEditor (UiContext&, const Theme&);

    void setTarget (BlockTarget);
    const BlockTarget& getTarget() const noexcept { return target; }
    /** Takes the block's values (the inspector's timer calls it). */
    void refresh();

    // What it shows (for the tests): the keys, bars and lists, in order.
    int getNumControls() const noexcept { return (int) rows.size(); }
    juce::Component* getControl (int index) const noexcept;
    ResponseView* getResponse() const noexcept { return response; }
    /** A dynamics block's transfer curve (nullptr for other blocks). */
    TransferView* getTransfer() const noexcept { return transfer; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void build();
    template <typename C>
    C& add (std::unique_ptr<C>, int height);
    ToggleKey& addKey (const juce::String& label, std::atomic<bool>&);
    ValueBar& addBar (ValueBar::Binding);
    ChoiceBar& addChoice (const juce::String& caption, std::function<juce::StringArray()>, std::function<int()>, std::function<void (int)>,
                          const juce::String& device = {});
    void addModel (std::atomic<int32_t>& model);
    void addParams (std::atomic<int32_t>& model, std::atomic<float>* values, int numValues);
    /** `height`: -1 for the block's main curve, which takes more room when there is some. */
    void addResponse (std::function<std::vector<ResponseView::Handle>()> handles = {}, int height = -1);
    /** Puts the last `count` rows side by side on one line: the first of them `widths` wide
        (px; 0: an equal share of what's left), the last an equal share too. */
    void joinLast (int count, std::vector<int> widths);
    int heightOf (int row, int width) const;
    void addTransfer (DynSlotParams&);

    UiContext& ctx;
    const Theme& theme;
    BlockTarget target;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::pair<std::unique_ptr<juce::Component>, int>> rows;
    struct Line
    {
        int first = 0, count = 0;
        std::vector<int> widths;
    };
    std::vector<Line> lines;
    std::vector<std::function<void()>> refreshers;
    ResponseView* response = nullptr; // one of the rows
    TransferView* transfer = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockEditor)
};

} // namespace livedaw::ui::universal
