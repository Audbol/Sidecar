#include "ui/universal/LayerEditor.h"

#include "engine/Layers.h"

#include <algorithm>

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
    bankKey = key ("BANK", [this] { showBankMenu(); }, "The console bank the layer occupies on a console with user layers (SQ-5, dLive)");
    key ("ADD SELECTED", [this] { addSelectedStrip(); }, "Add the selected strip to the layer");
    dcaKey = key ("ADD DCA", [this] { showDcaMenu(); }, "Add a DCA to the layer");
    key ("SPACER", [this] { addSpacer(); }, "A gap before the selected entry");
    key ("REMOVE", [this] { removeEntry(); }, "Take the selected entry out of the layer");
    key ("UP", [this] { moveEntry (-1); }, "Move the entry up");
    key ("DOWN", [this] { moveEntry (1); }, "Move the entry down");
    pickup.canPick = [this] (const juce::MouseEvent& e)
    {
        const int row = entries.getRowContainingPosition (e.getEventRelativeTo (&entries).x, e.getEventRelativeTo (&entries).y);
        return juce::isPositiveAndBelow (row, getNumEntries()) && ! entryText (row).startsWith ("(spacer)");
    };
    pickup.onPick = [this] (const juce::MouseEvent& e)
    {
        const auto local = e.getEventRelativeTo (&entries).getPosition();
        beginDrag (entries.getRowContainingPosition (local.x, local.y), e.getEventRelativeTo (this).getPosition());
    };
    pickup.onMove = [this] (const juce::MouseEvent& e) { moveDrag (e.getEventRelativeTo (this).getPosition()); };
    pickup.onDrop = [this] (const juce::MouseEvent& e) { endDrag (e.getEventRelativeTo (this).getPosition()); };
    entries.addMouseListener (&pickup, true);
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

bool LayerEditor::addDca (int index)
{
    if (layerId.isEmpty() || ! juce::isPositiveAndBelow (index, ctx.engine.getNumDcas()))
        return false;
    auto faders = entriesOf (ctx, layerId);
    if ((int) faders.size() >= layerslots::maxSlots())
        return false;
    faders.push_back (state::FaderRef::ofDca (index)); // at the end (a spacer stays a spacer)
    ctx.show.layers.setUserLayerFaders (layerId, faders);
    changed();
    return true;
}

bool LayerEditor::moveEntryTo (int from, int to)
{
    auto faders = entriesOf (ctx, layerId);
    if (! layerslots::moveEntry (faders, from, to))
        return false;
    if (faders != entriesOf (ctx, layerId))
        ctx.show.layers.setUserLayerFaders (layerId, faders);
    changed();
    return true;
}

bool LayerEditor::addEntryToLayer (int row, const juce::String& otherLayerId)
{
    const auto faders = entriesOf (ctx, layerId);
    const auto other = entriesOf (ctx, otherLayerId);
    if (otherLayerId == layerId || ! juce::isPositiveAndBelow (row, (int) faders.size()) || faders[(size_t) row].isEmpty()
        || std::find (other.begin(), other.end(), faders[(size_t) row]) != other.end())
        return false;
    if ((int) other.size() >= layerslots::maxSlots())
        return false;
    auto grown = other;
    grown.push_back (faders[(size_t) row]);
    ctx.show.layers.setUserLayerFaders (otherLayerId, grown);
    changed();
    return true;
}

bool LayerEditor::removeEntryAt (int row)
{
    auto faders = entriesOf (ctx, layerId);
    if (! juce::isPositiveAndBelow (row, (int) faders.size()))
        return false;
    faders.erase (faders.begin() + row);
    ctx.show.layers.setUserLayerFaders (layerId, faders);
    changed();
    return true;
}

void LayerEditor::setSelectedBank (const juce::String& console, const juce::String& bank)
{
    if (layerId.isNotEmpty())
        layerlink::assign (ctx.show.layers, layerId, console, bank);
    changed();
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

void LayerEditor::showDcaMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeelFor (theme));
    for (int d = 0; d < ctx.engine.getNumDcas(); ++d)
        menu.addItem (d + 1, "DCA " + juce::String (d + 1) + "  " + ctx.engine.getDcaName (d));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (dcaKey).withStandardItemHeight (sizes::kTouch - 8),
                        [safe = juce::Component::SafePointer<LayerEditor> (this)] (int item)
                        {
                            if (safe != nullptr && item > 0)
                                safe->addDca (item - 1);
                        });
}

void LayerEditor::showBankMenu()
{
    // Each console that has user or custom layers, and its layers; the one the layer occupies is ticked.
    struct Console
    {
        const char* key;
        const char* label;
        int banks, layers;
    };
    const Console consoles[] = { { "sq5", "SQ-5", 1, 6 }, { "c1500", "dLive C1500", 1, 6 }, { "c3500", "dLive C3500", 2, 6 } };
    const auto* l0 = ctx.show.layers.getUserLayer (layerId);
    if (l0 == nullptr)
        return;
    const auto layerBank = l0->bank; // a copy: getUserLayers() below refreshes the snapshot
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeelFor (theme));
    std::vector<std::pair<juce::String, juce::String>> choices; // console, bank; item = index + 1
    for (auto& c : consoles)
    {
        juce::PopupMenu sub;
        sub.addItem ((int) choices.size() + 1, "Not on this console", true, layerlink::bankOf (layerBank, c.key).isEmpty());
        choices.push_back ({ c.key, {} });
        for (int b = 0; b < c.banks; ++b)
            for (int i = 0; i < c.layers; ++i)
            {
                const auto bank = c.banks == 1 ? juce::String (i) : juce::String (b) + "." + juce::String (i);
                const auto taken = layerlink::layerFor (ctx.show.layers.getUserLayers(), c.key, bank);
                const auto name = (c.banks == 1 ? juce::String() : "Bank " + juce::String (b + 1) + " ") + "Layer " + juce::String::charToString ((juce::juce_wchar) ('A' + i))
                                  + (taken != nullptr && taken->id != layerId ? "  (now " + taken->name + ")" : juce::String());
                sub.addItem ((int) choices.size() + 1, name, true, layerlink::bankOf (layerBank, c.key) == bank);
                choices.push_back ({ c.key, bank });
            }
        menu.addSubMenu (c.label, sub);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (bankKey).withStandardItemHeight (sizes::kTouch - 8),
                        [safe = juce::Component::SafePointer<LayerEditor> (this), choices] (int item)
                        {
                            if (safe != nullptr && item > 0)
                                safe->setSelectedBank (choices[(size_t) (item - 1)].first, choices[(size_t) (item - 1)].second);
                        });
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
    const auto banks = layerlink::describe (l.bank);
    g.drawText (l.name, 18, 0, banks.isEmpty() ? w - 22 : (w - 22) * 3 / 5, h, juce::Justification::centredLeft, true);
    if (banks.isNotEmpty())
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel));
        g.drawText (banks, 18 + (w - 22) * 3 / 5, 0, (w - 22) * 2 / 5, h, juce::Justification::centredRight, true);
    }
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
    g.drawText ("LAYERS (the same on the WING, SQ-5 and dLive)", getLocalBounds().reduced (sizes::kGap).removeFromTop (24), juce::Justification::centredLeft);
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
    layers.setBounds (place (left, 0, 8, 4));
    entries.setBounds (place (area, 8, 14, 3));
}

//==============================================================================
// Drag editing
void LayerEditor::beginDrag (int row, juce::Point<int> at)
{
    if (! juce::isPositiveAndBelow (row, getNumEntries()))
        return;
    dragRow = row;
    dragAt = at;
    repaint();
}

void LayerEditor::moveDrag (juce::Point<int> at)
{
    if (dragRow < 0)
        return;
    dragAt = at;
    repaint();
}

LayerEditor::Drop LayerEditor::dropFor (juce::Point<int> p) const
{
    Drop d;
    if (layers.getBounds().contains (p))
    {
        const int row = layers.getRowContainingPosition (p.x - layers.getX(), p.y - layers.getY());
        const auto& all = ctx.show.layers.getUserLayers();
        if (juce::isPositiveAndBelow (row, (int) all.size()) && all[(size_t) row].id != layerId)
        {
            d.kind = Drop::Add;
            d.layerId = all[(size_t) row].id;
        }
        return d;
    }
    if (entries.getBounds().contains (p))
    {
        const auto local = p - entries.getPosition();
        const int row = entries.getRowContainingPosition (local.x, local.y);
        d.kind = Drop::Move;
        d.to = row < 0 ? getNumEntries() : (local.y > entries.getRowPosition (row, true).getCentreY() ? row + 1 : row);
        return d;
    }
    d.kind = Drop::Remove; // out of the list
    return d;
}

bool LayerEditor::endDrag (juce::Point<int> at)
{
    if (dragRow < 0)
        return false;
    const int row = dragRow;
    const auto d = dropFor (at);
    dragRow = -1;
    repaint();
    switch (d.kind)
    {
        case Drop::Move:   return moveEntryTo (row, d.to);
        case Drop::Add:    return addEntryToLayer (row, d.layerId);
        case Drop::Remove: return removeEntryAt (row);
        case Drop::None:   break;
    }
    return false;
}

void LayerEditor::paintOverChildren (juce::Graphics& g)
{
    if (dragRow < 0)
        return;
    const auto d = dropFor (dragAt);
    if (d.kind == Drop::Move)
    {
        const int n = getNumEntries();
        const int y = entries.getY() + (d.to < n ? entries.getRowPosition (d.to, true).getY() : n > 0 ? entries.getRowPosition (n - 1, true).getBottom() : 0);
        g.setColour (theme.accent);
        g.fillRect (entries.getX(), juce::jlimit (entries.getY(), entries.getBottom() - 3, y) - 1, entries.getWidth(), 3);
    }
    const auto label = (d.kind == Drop::Remove ? "REMOVE  " : "") + entryText (dragRow);
    g.setFont (type::font (type::kValue, true));
    const int w = juce::jmax (80, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), label) + 20);
    const juce::Rectangle<int> ghost (dragAt.x - w / 2, dragAt.y - 14, w, 28);
    g.setColour ((d.kind == Drop::Remove ? juce::Colour (0xffd04040) : theme.accent).withAlpha (0.85f));
    g.fillRoundedRectangle (ghost.toFloat(), sizes::kRadius);
    g.setColour (juce::Colours::white);
    g.drawText (label, ghost, juce::Justification::centred, true);
}

} // namespace livedaw::ui::universal
