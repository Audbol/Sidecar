#include "ui/MixerChooser.h"

#include "ui/LookAndFeel.h"

#include <BinaryData.h>

namespace livedaw::ui::mixerChooser
{

const std::array<Mixer, 12>& mixers()
{
    static const std::array<Mixer, 12> list { { { InterfaceKind::Wing, "WING", "" },
                                               { InterfaceKind::Universal, "UNIVERSAL", "" },
                                               { InterfaceKind::X32, "X32", "" },
                                               { InterfaceKind::ProMix, "PROMIX 01", "" },
                                               { InterfaceKind::Yamaha02R, "02R", "" },
                                               { InterfaceKind::Sq5, "SQ-5", "" },
                                               { InterfaceKind::Yamaha01V, "01V", "" },
                                               { InterfaceKind::DLive, "DLIVE", "" },
                                               { InterfaceKind::Dm2000, "DM2000", "" },
                                               { InterfaceKind::Pro2, "PRO2", "" },
                                               { InterfaceKind::Xenyx1202, "XENYX Q1202USB", "" },
                                               { InterfaceKind::PluginHost, "PLUGIN HOST", "" } } };
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
    /** A console's tile picture, drawn by the screenshot tests and scaled by scripts/make_chooser_pictures.py. */
    juce::Image stockPicture (InterfaceKind kind)
    {
        const auto load = [] (const void* data, int size) { return juce::ImageCache::getFromMemory (data, size); };
        switch (kind)
        {
            case InterfaceKind::Wing:      return load (BinaryData::wing_png, BinaryData::wing_pngSize);
            case InterfaceKind::Universal: return load (BinaryData::universal_png, BinaryData::universal_pngSize);
            case InterfaceKind::X32:       return load (BinaryData::x32_png, BinaryData::x32_pngSize);
            case InterfaceKind::ProMix:    return load (BinaryData::promix_png, BinaryData::promix_pngSize);
            case InterfaceKind::Yamaha02R: return load (BinaryData::y02r_png, BinaryData::y02r_pngSize);
            case InterfaceKind::Sq5:       return load (BinaryData::sq5_png, BinaryData::sq5_pngSize);
            case InterfaceKind::Yamaha01V: return load (BinaryData::y01v_png, BinaryData::y01v_pngSize);
            case InterfaceKind::DLive:     return load (BinaryData::dlive_png, BinaryData::dlive_pngSize);
            case InterfaceKind::Dm2000:    return load (BinaryData::dm2000_png, BinaryData::dm2000_pngSize);
            case InterfaceKind::Pro2:      return load (BinaryData::pro2_png, BinaryData::pro2_pngSize);
            case InterfaceKind::Xenyx1202: return load (BinaryData::xenyx1202_png, BinaryData::xenyx1202_pngSize);
            case InterfaceKind::PluginHost: return load (BinaryData::pluginhost_png, BinaryData::pluginhost_pngSize);
            default:                       return {};
        }
    }

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

    /** A tile: the console's picture (or a plain panel), its name, its note. */
    class Tile final : public juce::Button
    {
    public:
        Tile (const juce::String& nameText, const juce::String& noteText)
            : juce::Button ((nameText + " " + noteText).trimEnd()), name (nameText), note (noteText)
        {
            setMouseClickGrabsKeyboardFocus (false);
        }

        float cap = 12.0f;
        bool selected = false;
        juce::Image picture;

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            auto fill = Colours::screenKey;
            if (highlighted || down)
                fill = fill.brighter (down ? 0.25f : 0.12f);
            g.setColour (fill);
            g.fillRect (getLocalBounds());
            auto r = getLocalBounds().reduced (juce::roundToInt (cap * 0.4f));
            auto text = r.removeFromBottom (juce::roundToInt (cap * 4.2f));
            g.setColour (juce::Colours::black);
            g.fillRect (r);
            if (picture.isValid())
                g.drawImage (picture, r.toFloat(), juce::RectanglePlacement::centred);
            g.setColour (Colours::caption);
            g.setFont (wingFontPx (cap * 1.0f, WingWeight::SemiBold));
            g.drawFittedText (name, text.removeFromTop (text.getHeight() * 11 / 20), juce::Justification::bottomLeft, 1, 0.8f);
            g.setFont (wingFontPx (cap));
            g.setColour (Colours::caption.withAlpha (0.7f));
            g.drawFittedText (note, text, juce::Justification::topLeft, 1, 0.7f);
            if (selected)
            {
                g.setColour (Colours::gold);
                g.drawRect (getLocalBounds(), 2);
            }
        }

    private:
        const juce::String name, note;
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
            auto* content = new juce::Component();
            for (const auto& m : mixers())
            {
                auto tile = std::make_unique<Tile> (m.name, m.note);
                tile->onClick = [this, kind = m.kind]
                {
                    if (options.onMixer)
                        options.onMixer (kind);
                };
                content->addAndMakeVisible (*tile);
                tiles.push_back (std::move (tile));
            }
            pictured.assign (tiles.size(), false);
            tiles[0]->selected = true;
            viewport.setViewedComponent (content, true);
            viewport.setScrollBarsShown (true, false);
            viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
            addAndMakeVisible (viewport);
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
            const int n = (int) tiles.size();
            int next = sel;
            if (key == juce::KeyPress::escapeKey)
                keep();
            else if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
                tiles[(size_t) sel]->onClick();
            else if (key == juce::KeyPress::leftKey)
                next = sel - 1;
            else if (key == juce::KeyPress::rightKey)
                next = sel + 1;
            else if (key == juce::KeyPress::upKey)
                next = sel - columns;
            else if (key == juce::KeyPress::downKey)
                next = sel + columns;
            next = juce::jlimit (0, n - 1, next);
            if (next != sel)
            {
                tiles[(size_t) sel]->selected = false;
                tiles[(size_t) sel]->repaint();
                sel = next;
                tiles[(size_t) sel]->selected = true;
                tiles[(size_t) sel]->repaint();
                const auto t = tiles[(size_t) sel]->getBounds();
                const auto view = viewport.getViewArea();
                if (t.getY() < view.getY())
                    viewport.setViewPosition (0, t.getY() - tileGap);
                else if (t.getBottom() > view.getBottom())
                    viewport.setViewPosition (0, t.getBottom() + tileGap - view.getHeight());
            }
            return true; // the console under it stays out of it
        }

        void resized() override
        {
            const auto b = getLocalBounds();
            cap = juce::jlimit (10.0f, screenCap (24.0f), juce::jmin ((float) b.getHeight() * 0.018f, (float) b.getWidth() * 0.016f));
            const int margin = juce::roundToInt (cap * 1.4f);
            tileGap = juce::roundToInt (cap * 0.7f);
            const int caption = juce::roundToInt (cap * 2.6f);
            window = b.withSizeKeepingCentre (juce::jmin (b.getWidth() - 16, 1280), b.getHeight() - 8);
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
            viewport.setBounds (r);
            layoutTiles (r.getWidth() - viewport.getScrollBarThickness());
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
        void layoutTiles (int width)
        {
            // 280 px a tile where it fits, up to 4 across; a narrow window gets fewer (a tile is never under 160 px).
            columns = juce::jlimit (1, 4, (width + tileGap) / (280 + tileGap));
            const int w = juce::jmax (160, (width - tileGap * (columns - 1)) / columns);
            const int h = w * 9 / 16 + juce::roundToInt (cap * 4.6f);
            for (size_t i = 0; i < tiles.size(); ++i)
            {
                auto& t = *tiles[i];
                t.cap = cap;
                if (! pictured[i])
                {
                    pictured[i] = true;
                    t.picture = stockPicture (mixers()[i].kind);
                    if (options.picture)
                        if (auto live = options.picture (mixers()[i].kind, juce::jmin (w, 480)); live.isValid())
                            t.picture = live;
                }
                t.setBounds ((int) i % columns * (w + tileGap), (int) i / columns * (h + tileGap), w, h);
            }
            const int rows = ((int) tiles.size() + columns - 1) / columns;
            viewport.getViewedComponent()->setSize (columns * w + (columns - 1) * tileGap, rows * (h + tileGap));
        }

        void keep()
        {
            if (options.onContinue)
                options.onContinue();
        }

        Options options;
        std::unique_ptr<Row> continueRow;
        juce::Viewport viewport;
        std::vector<std::unique_ptr<Tile>> tiles;
        std::vector<bool> pictured; // one per tile: its picture has been drawn
        juce::Rectangle<int> window, titleBar, panel, captionArea;
        float cap = 12.0f;
        int columns = 1, sel = 0, tileGap = 8;
    };
} // namespace

std::unique_ptr<juce::Component> create (Options options)
{
    return std::make_unique<Window> (std::move (options));
}

} // namespace livedaw::ui::mixerChooser
