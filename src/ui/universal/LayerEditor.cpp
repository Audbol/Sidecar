#include "ui/universal/LayerEditor.h"

#include "engine/Layers.h"

namespace livedaw::ui::universal
{

namespace
{
    /** The colours a layer can take (0xAARRGGBB); the first clears it. */
    const std::pair<const char*, juce::uint32> kColours[] = {
        { "None", 0 },          { "Red", 0xffe05a5a },  { "Orange", 0xffe0953c }, { "Yellow", 0xffd8c846 }, { "Green", 0xff5cc46a },
        { "Cyan", 0xff46bcd0 }, { "Blue", 0xff5a8fe0 }, { "Purple", 0xff9a6ae0 }, { "Pink", 0xffe070b0 },
    };

    std::vector<state::FaderRef> entriesOf (UiContext& ctx, const juce::String& id)
    {
        if (const auto* l = ctx.show.layers.getUserLayer (id))
            return l->faders;
        return {};
    }
} // namespace

LayerEditor::LayerEditor (UiContext& c, const Theme& t) : ctx (c), theme (t)
{
    for (auto* lb : { &layers, &entries })
    {
        lb->setRowHeight (28);
        lb->setColour (juce::ListBox::backgroundColourId, theme.well);
        lb->setColour (juce::ListBox::outlineColourId, theme.outline);
        lb->setOutlineThickness (1);
        addAndMakeVisible (*lb);
    }
    const auto key = [this] (const juce::String& label, std::function<void()> fn, const juce::String& tip)
    {
        auto k = std::make_unique<ToggleKey> (label, theme.accent, theme);
        k->onClick = std::move (fn);
        k->setTooltip (tip);
        addAndMakeVisible (*k);
        keys.push_back (std::move (k));
        return keys.back().get();
    };
    key ("NEW", [this] { newLayer(); }, "A new, empty layer");
    key ("RENAME", [this]
         {
             if (const auto* l = ctx.show.layers.getUserLayer (layerId))
                 askName ("Rename layer", l->name, [this] (juce::String n) { renameSelected (n); });
         }, "Rename the layer");
    colourKey = key ("COLOUR", [this] { showColourMenu(); }, "The layer's colour");
    key ("DELETE", [this] { deleteSelected(); }, "Delete the layer");
    key ("DUPLICATE", [this] { duplicateSelected(); }, "Copy the layer");
    key ("EXPORT", [this]
         {
             chooser = std::make_shared<juce::FileChooser> ("Export the layers", juce::File(), "*.sidecarlayers");
             chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                                   [this] (const juce::FileChooser& fc)
                                   {
                                       if (fc.getResult() != juce::File())
                                           exportTo (fc.getResult().withFileExtension ("sidecarlayers"));
                                   });
         }, "Save every layer to a file, to open in another show");
    key ("IMPORT", [this]
         {
             chooser = std::make_shared<juce::FileChooser> ("Import layers", juce::File(), "*.sidecarlayers");
             chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [this] (const juce::FileChooser& fc)
                                   {
                                       if (fc.getResult() != juce::File())
                                           importFrom (fc.getResult());
                                   });
         }, "Add the layers of an exported file");
    key ("ADD SELECTED", [this] { addSelectedStrip(); }, "Add the selected strip to the layer");
    key ("SPACER", [this] { addSpacer(); }, "A gap before the selected entry");
    key ("REMOVE", [this] { removeEntry(); }, "Take the selected entry out of the layer");
    key ("UP", [this] { moveEntry (-1); }, "Move the entry up");
    key ("DOWN", [this] { moveEntry (1); }, "Move the entry down");
    refresh();
}

//==============================================================================
void LayerEditor::changed()
{
    refresh();
    if (onChanged)
        onChanged();
}

juce::String LayerEditor::newLayer()
{
    layerId = ctx.show.layers.addUserLayer ("Layer " + juce::String ((int) ctx.show.layers.getUserLayers().size() + 1));
    changed();
    return layerId;
}

void LayerEditor::renameSelected (const juce::String& name)
{
    if (layerId.isNotEmpty() && name.trim().isNotEmpty())
        ctx.show.layers.renameUserLayer (layerId, name.trim());
    changed();
}

void LayerEditor::setSelectedColour (juce::uint32 argb)
{
    if (layerId.isNotEmpty())
        ctx.show.layers.setUserLayerColour (layerId, argb);
    changed();
}

void LayerEditor::deleteSelected()
{
    if (layerId.isEmpty())
        return;
    ctx.show.layers.removeUserLayer (layerId);
    layerId = {};
    changed();
}

juce::String LayerEditor::duplicateSelected()
{
    const auto* l = ctx.show.layers.getUserLayer (layerId);
    if (l == nullptr)
        return {};
    const auto name = l->name + " copy";
    const auto faders = l->faders;
    const auto colour = l->colour;
    layerId = ctx.show.layers.addUserLayer (name, faders);
    ctx.show.layers.setUserLayerColour (layerId, colour);
    changed();
    return layerId;
}

bool LayerEditor::addSelectedStrip()
{
    auto* strip = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    if (strip == nullptr || layerId.isEmpty())
        return false;
    auto faders = entriesOf (ctx, layerId);
    faders.push_back (state::FaderRef::of (state::StripRef::of (*strip)));
    ctx.show.layers.setUserLayerFaders (layerId, faders);
    changed();
    return true;
}

void LayerEditor::addSpacer()
{
    const int row = entries.getSelectedRow();
    if (layerId.isEmpty() || row < 0)
        return;
    ctx.show.layers.insertUserLayerSlots (layerId, row, { state::FaderRef::blank() });
    entries.selectRow (row + 1);
    changed();
}

void LayerEditor::removeEntry()
{
    const int row = entries.getSelectedRow();
    auto faders = entriesOf (ctx, layerId);
    if (! juce::isPositiveAndBelow (row, (int) faders.size()))
        return;
    faders.erase (faders.begin() + row);
    ctx.show.layers.setUserLayerFaders (layerId, faders);
    changed();
}

void LayerEditor::moveEntry (int delta)
{
    const int row = entries.getSelectedRow();
    const int count = getNumEntries();
    if (! juce::isPositiveAndBelow (row, count) || ! juce::isPositiveAndBelow (row + delta, count))
        return;
    ctx.show.layers.moveWithinUserLayer (layerId, row, delta);
    entries.selectRow (row + delta);
    changed();
}

bool LayerEditor::exportTo (const juce::File& file) const
{
    return file.replaceWithText (ctx.show.layers.exportUserLayers());
}

int LayerEditor::importFrom (const juce::File& file)
{
    const int n = file.existsAsFile() ? ctx.show.layers.importUserLayers (file.loadFileAsString()) : 0;
    if (n > 0)
        changed();
    return n;
}

void LayerEditor::selectLayer (juce::String id)
{
    layerId = std::move (id);
    refresh();
}

int LayerEditor::getNumEntries() const
{
    return (int) entriesOf (ctx, layerId).size();
}

juce::String LayerEditor::entryText (int row) const
{
    const auto faders = entriesOf (ctx, layerId);
    if (! juce::isPositiveAndBelow (row, (int) faders.size()))
        return {};
    const auto& f = faders[(size_t) row];
    if (f.empty)
        return "(spacer)";
    if (f.dca)
        return "DCA " + juce::String (f.dcaIndex + 1) + "  " + ctx.engine.getDcaName (f.dcaIndex);
    if (f.isStrip())
        if (auto* s = state::findStrip (ctx.engine, f.strip))
            return s->getLabel() + "  " + s->getName();
    return "(other fader)";
}

//==============================================================================
void LayerEditor::askName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done = std::move (done)] (int result)
                                                                  {
                                                                      if (result == 1)
                                                                          done (w->getTextEditorContents ("name"));
                                                                  }),
                        true);
}

void LayerEditor::showColourMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeelFor (theme));
    int i = 1;
    for (auto& entry : kColours)
        menu.addItem (i++, entry.first);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (colourKey).withStandardItemHeight (sizes::kTouch - 8),
                        [safe = juce::Component::SafePointer<LayerEditor> (this)] (int item)
                        {
                            if (safe != nullptr && item > 0)
                                safe->setSelectedColour (kColours[item - 1].second);
                        });
}

//==============================================================================
int LayerEditor::getNumRows()
{
    return (int) ctx.show.layers.getUserLayers().size();
}

void LayerEditor::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    const auto& all = ctx.show.layers.getUserLayers();
    if (! juce::isPositiveAndBelow (row, (int) all.size()))
        return;
    const auto& l = all[(size_t) row];
    if (selected || l.id == layerId)
        g.fillAll (theme.accent.withAlpha (0.3f));
    g.setColour (l.colour != 0 ? juce::Colour (l.colour) : theme.outline);
    g.fillRect (4, 5, 8, h - 10);
    g.setColour (theme.text);
    g.setFont (type::font (type::kValue));
    g.drawText (l.name, 18, 0, w - 22, h, juce::Justification::centredLeft, true);
}

void LayerEditor::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    const auto& all = ctx.show.layers.getUserLayers();
    if (juce::isPositiveAndBelow (row, (int) all.size()))
    {
        layerId = all[(size_t) row].id;
        refresh();
        if (onChanged)
            onChanged();
    }
}

void LayerEditor::Model::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    const auto text = owner.entryText (row);
    if (selected)
        g.fillAll (owner.theme.accent.withAlpha (0.3f));
    g.setColour (text == "(spacer)" ? owner.theme.textDim : owner.theme.text);
    g.setFont (type::font (type::kValue));
    g.drawText (juce::String (row + 1) + "  " + text, 6, 0, w - 8, h, juce::Justification::centredLeft, true);
}

void LayerEditor::refresh()
{
    layers.updateContent();
    entries.updateContent();
    layers.repaint();
    entries.repaint();
    repaint();
}

void LayerEditor::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (theme.panel);
    g.fillRoundedRectangle (area, sizes::kRadius);
    g.setColour (theme.accent);
    g.drawRoundedRectangle (area.reduced (0.75f), sizes::kRadius, 1.5f);
    g.setColour (theme.text);
    g.setFont (type::font (type::kLabel, true));
    g.drawText ("LAYERS (shared with the WING's user layers)", getLocalBounds().reduced (sizes::kGap).removeFromTop (24), juce::Justification::centredLeft);
}

void LayerEditor::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap);
    area.removeFromTop (24 + sizes::kGapSmall);
    auto left = area.removeFromLeft (area.getWidth() * 2 / 5);
    area.removeFromLeft (sizes::kGap);
    const int keyH = 34;
    // The keys in rows under a list, `perRow` across; returns the room left for the list.
    const auto place = [this, keyH] (juce::Rectangle<int> r, int from, int to, int perRow)
    {
        const int n = to - from;
        const int rows = (n + perRow - 1) / perRow;
        const auto strip = r.removeFromBottom (rows * (keyH + sizes::kGapSmall));
        const int w = (strip.getWidth() - (perRow - 1) * sizes::kGapSmall) / perRow;
        for (int i = 0; i < n; ++i)
            keys[(size_t) (from + i)]->setBounds (strip.getX() + (i % perRow) * (w + sizes::kGapSmall),
                                                  strip.getY() + (i / perRow) * (keyH + sizes::kGapSmall) + sizes::kGapSmall, w, keyH);
        return r;
    };
    layers.setBounds (place (left, 0, 7, 3));
    entries.setBounds (place (area, 7, 12, 3));
}

} // namespace livedaw::ui::universal
