#include "ui/MixerChooser.h"

#include "ui/LookAndFeel.h"

namespace livedaw::ui::mixerChooser
{

const std::array<Mixer, 11>& mixers()
{
    static const std::array<Mixer, 11> list { { { InterfaceKind::Wing, "WING", "(experimental)" },
                                               { InterfaceKind::Universal, "UNIVERSAL", "" },
                                               { InterfaceKind::X32, "X32", "(very experimental)" },
                                               { InterfaceKind::ProMix, "PROMIX 01", "(very experimental)" },
                                               { InterfaceKind::Yamaha02R, "02R", "(very experimental)" },
                                               { InterfaceKind::Sq5, "SQ-5", "(very experimental)" },
                                               { InterfaceKind::Yamaha01V, "01V", "(very experimental)" },
                                               { InterfaceKind::DLive, "DLIVE", "(very experimental)" },
                                               { InterfaceKind::Dm2000, "DM2000", "(very experimental)" },
                                               { InterfaceKind::Pro2, "PRO2", "(very experimental)" },
                                               { InterfaceKind::Xenyx1202, "XENYX Q1202USB", "(very experimental)" } } };
    return list;
}

juce::String label (InterfaceKind kind)
{
    for (const auto& m : mixers())
        if (m.kind == kind)
            return (juce::String (m.name) + " " + m.note).trimEnd();
    return {};
}

juce::StringArray keyLabels()
{
    juce::StringArray labels;
    for (const auto& m : mixers())
        labels.add ((juce::String (m.name) + "\n" + m.note).trimEnd());
    return labels;
}

namespace
{
    /** A row of the window: its name, what it says beneath. */
    class Row final : public juce::Button
    {
    public:
        Row (const juce::String& nameText, const juce::String& noteText, bool isPrimary)
            : juce::Button ((nameText + " " + noteText).trimEnd()), name (nameText), note (noteText), primary (isPrimary)
        {
        }

        float cap = 12.0f;

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            auto fill = primary ? Colours::gold : Colours::screenKey;
            if (highlighted || down)
                fill = fill.brighter (down ? 0.25f : 0.12f);
            const auto ink = primary ? juce::Colours::black : Colours::caption;
            g.setColour (fill);
            g.fillRect (getLocalBounds());
            auto r = getLocalBounds().reduced (juce::roundToInt (cap * 1.2f), 0);
            g.setColour (ink);
            g.setFont (wingFontPx (cap * 1.3f, WingWeight::SemiBold));
            if (note.isEmpty())
            {
                g.drawFittedText (name, r, juce::Justification::centredLeft, 1, 0.8f);
                return;
            }
            if (primary)
            {
                g.drawFittedText (name, r.removeFromTop (getHeight() * 11 / 20), juce::Justification::bottomLeft, 1, 0.8f);
                g.setFont (wingFontPx (cap));
                g.setColour (ink.withAlpha (0.75f));
                g.drawFittedText (note, r, juce::Justification::topLeft, 1, 0.7f);
                return;
            }
            g.drawFittedText (name, r.removeFromLeft (juce::roundToInt (cap * 12.0f)), juce::Justification::centredLeft, 1, 0.8f);
            g.setFont (wingFontPx (cap));
            g.setColour (ink.withAlpha (0.7f));
            g.drawFittedText (note, r, juce::Justification::centredRight, 1, 0.7f);
        }

    private:
        const juce::String name, note;
        const bool primary;
    };

    class Window final : public juce::Component
    {
    public:
        explicit Window (Options o) : options (std::move (o))
        {
            setWantsKeyboardFocus (true);
            if (options.continueText.isNotEmpty())
            {
                continueRow = std::make_unique<Row> ("CONTINUE WHERE YOU LEFT OFF", options.continueText, true);
                continueRow->onClick = [this] { keep(); };
                addAndMakeVisible (*continueRow);
            }
            for (const auto& m : mixers())
            {
                auto row = std::make_unique<Row> (m.name, m.note, false);
                row->onClick = [this, kind = m.kind]
                {
                    if (options.onMixer)
                        options.onMixer (kind);
                };
                addAndMakeVisible (*row);
                rows.push_back (std::move (row));
            }
        }

        void visibilityChanged() override
        {
            if (isShowing())
                grabKeyboardFocus();
        }

        void parentHierarchyChanged() override
        {
            if (isShowing())
                grabKeyboardFocus();
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::escapeKey)
                keep();
            return true; // the console under it stays out of it
        }

        void resized() override
        {
            const auto b = getLocalBounds();
            cap = juce::jlimit (10.0f, screenCap (24.0f), juce::jmin ((float) b.getHeight() * 0.018f, (float) b.getWidth() * 0.016f));
            const int rowH = juce::roundToInt (cap * 3.4f), gap = juce::roundToInt (cap * 0.4f);
            const int margin = juce::roundToInt (cap * 1.4f);
            const int caption = juce::roundToInt (cap * 2.6f);
            const int continueH = continueRow != nullptr ? juce::roundToInt (cap * 4.8f) + margin : 0;
            const int height = juce::roundToInt (cap * 3.2f) + margin * 2 + continueH + caption + (rowH + gap) * (int) rows.size();
            window = b.withSizeKeepingCentre (juce::jmin (b.getWidth() - 16, juce::roundToInt (cap * 46.0f)), juce::jmin (b.getHeight() - 8, height));
            auto r = window;
            titleBar = r.removeFromTop (juce::roundToInt (cap * 3.2f));
            panel = r;
            r = r.reduced (margin);
            if (continueRow != nullptr)
            {
                continueRow->cap = cap;
                continueRow->setBounds (r.removeFromTop (juce::roundToInt (cap * 4.8f)));
                r.removeFromTop (margin);
            }
            captionArea = r.removeFromTop (caption);
            for (auto& row : rows)
            {
                row->cap = cap;
                row->setBounds (r.removeFromTop (rowH));
                r.removeFromTop (gap);
            }
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (Colours::scrim);
            g.fillRect (getLocalBounds());
            g.setColour (juce::Colours::black);
            g.fillRect (window.expanded (2));
            g.setColour (Colours::screenHeader);
            g.fillRect (titleBar);
            g.setColour (Colours::caption);
            g.setFont (wingFontPx (cap * 1.15f, WingWeight::SemiBold));
            g.drawFittedText ("START SIDECAR", titleBar.reduced (juce::roundToInt (cap)), juce::Justification::centred, 1, 0.8f);
            g.setColour (Colours::screenPanel);
            g.fillRect (panel);
            g.setColour (Colours::caption);
            g.setFont (wingFontPx (cap));
            g.drawFittedText (continueRow != nullptr ? "OR START A NEW MIXER (CLEARS THE CONSOLE AND ITS SCENES)" : "START A NEW MIXER", captionArea,
                              juce::Justification::centredLeft, 1, 0.7f);
        }

    private:
        void keep()
        {
            if (options.onContinue)
                options.onContinue();
        }

        Options options;
        std::unique_ptr<Row> continueRow;
        std::vector<std::unique_ptr<Row>> rows;
        juce::Rectangle<int> window, titleBar, panel, captionArea;
        float cap = 12.0f;
    };
} // namespace

std::unique_ptr<juce::Component> create (Options options)
{
    return std::make_unique<Window> (std::move (options));
}

} // namespace livedaw::ui::mixerChooser
