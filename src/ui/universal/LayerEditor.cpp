#include "ui/universal/LayerEditor.h"

#include "engine/Layers.h"
#include "ui/WingPopups.h"

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

    /** A row is a touch target tall, with a gap between rows that the row's fill leaves bare. */
    constexpr int kRow = sizes::kTouch + sizes::kGapSmall;

    std::vector<state::FaderRef> entriesOf (UiContext& ctx, const juce::String& id)
    {
        if (const auto* l = ctx.show.layers.getUserLayer (id))
            return l->faders;
        return {};
    }
} // namespace

//==============================================================================
ColourGrid::ColourGrid (const Theme& t) : theme (t)
{
    setSize (getIdealSize().x, getIdealSize().y);
}

int ColourGrid::getNumColours() { return (int) std::size (kColours); }
juce::uint32 ColourGrid::colourAt (int index) { return kColours[index].second; }
juce::String ColourGrid::nameAt (int index) { return kColours[index].first; }

juce::Point<int> ColourGrid::getIdealSize() const
{
    const int rows = (getNumColours() + kColumns - 1) / kColumns;
    return { kColumns * kSwatch + (kColumns + 1) * sizes::kGap, rows * kSwatch + (rows + 1) * sizes::kGap };
}

juce::Rectangle<int> ColourGrid::getSwatchBounds (int index) const
{
    return { sizes::kGap + (index % kColumns) * (kSwatch + sizes::kGap), sizes::kGap + (index / kColumns) * (kSwatch + sizes::kGap), kSwatch, kSwatch };
}

void ColourGrid::paint (juce::Graphics& g)
{
    g.fillAll (theme.panel);
    for (int i = 0; i < getNumColours(); ++i)
    {
        const auto r = getSwatchBounds (i).toFloat();
        if (colourAt (i) == 0)
        {
            // None: an empty swatch with a slash.
            g.setColour (theme.well);
            g.fillRoundedRectangle (r, sizes::kRadius);
            g.setColour (theme.outline);
            g.drawRoundedRectangle (r, sizes::kRadius, 1.0f);
            g.drawLine (r.getX() + 8.0f, r.getBottom() - 8.0f, r.getRight() - 8.0f, r.getY() + 8.0f, 2.0f);
        }
        else
        {
            g.setColour (juce::Colour (colourAt (i)));
            g.fillRoundedRectangle (r, sizes::kRadius);
        }
        g.setColour (colourAt (i) == 0 ? theme.text : juce::Colours::black.withAlpha (0.75f));
        g.setFont (type::font (type::kCaption));
        g.drawText (nameAt (i), r.toNearestInt().removeFromBottom (16), juce::Justification::centred, false);
    }
}

void ColourGrid::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < getNumColours(); ++i)
        if (getSwatchBounds (i).contains (e.getPosition()))
        {
            if (onPick)
                onPick (colourAt (i));
            return;
        }
}

//==============================================================================
LayerEditor::LayerEditor (UiContext& c, const Theme& t) : ctx (c), theme (t)
{
    for (auto* lb : { &layers, &entries })
    {
        lb->setRowHeight (kRow);
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
        // A tooltip shows on hover only: a touched key says what it does in the title bar too.
        k->onStateChange = [this, kp = k.get()]
        {
            if (kp->isDown())
            {
                hint = kp->getTooltip();
                repaint();
            }
        };
        addAndMakeVisible (*k);
        keys.push_back (std::move (k));
        return keys.back().get();
    };
    key ("NEW", [this] { newLayer(); }, "A new, empty layer");
    key ("RENAME", [this]
         {
             beginRename();
         }, "Rename the layer");
    colourKey = key ("COLOUR", [this] { showColourMenu (colourKey->getScreenBounds()); }, "The layer's colour");
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
    bankKey = key ("BANK", [this] { showBankMenu (bankKey->getScreenBounds()); }, "The console bank the layer occupies on a console with user layers (SQ-5, dLive)");
    key ("ADD SELECTED", [this] { addSelectedStrip(); }, "Add the selected strip to the layer");
    dcaKey = key ("ADD DCA", [this] { showDcaMenu(); }, "Add a DCA to the layer");
    key ("SPACER", [this] { addSpacer(); }, "A gap before the selected entry");
    key ("REMOVE", [this] { removeEntry(); }, "Take the selected entry out of the layer");
    key ("UP", [this] { moveEntry (-1); }, "Move the entry up");
    key ("DOWN", [this] { moveEntry (1); }, "Move the entry down");
    const auto rowOf = [this] (const juce::MouseEvent& e)
    {
        // The row the finger went down on (it may have moved on before the drag began).
        const auto local = entries.getLocalPoint (e.eventComponent, e.getMouseDownPosition());
        return entries.getRowContainingPosition (local.x, local.y);
    };
    pickup.canPick = [this, rowOf] (const juce::MouseEvent& e) { return juce::isPositiveAndBelow (rowOf (e), getNumEntries()); };
    // A finger on the handle (the row's left edge) drags at once; on the rest of the row it scrolls the list.
    pickup.movePicks = [this] (const juce::MouseEvent& e) { return e.getEventRelativeTo (&entries).x < sizes::kTouch; };
    pickup.onPick = [this, rowOf] (const juce::MouseEvent& e)
    {
        const int row = rowOf (e);
        if (! entryText (row).startsWith ("(spacer)")) // a spacer isn't picked up: lifted, it opens its menu
            beginDrag (row, e.getEventRelativeTo (this).getPosition());
    };
    pickup.onMove = [this] (const juce::MouseEvent& e) { moveDrag (e.getEventRelativeTo (this).getPosition()); };
    pickup.onDrop = [this] (const juce::MouseEvent& e) { endDrag (e.getEventRelativeTo (this).getPosition()); };
    pickup.onMenu = [this, rowOf] (const juce::MouseEvent& e)
    {
        const int row = rowOf (e);
        dragRow = -1;
        edgeScroll.stop();
        repaint();
        if (juce::isPositiveAndBelow (row, getNumEntries()))
            showEntryMenu (row, localAreaToGlobal (getEntryRowBounds (row)));
    };
    entries.addMouseListener (&pickup, true);
    edgeScroll.scrollBy = [this] (int dy)
    {
        if (auto* vp = entries.getViewport())
            vp->setViewPosition (vp->getViewPositionX(), vp->getViewPositionY() + dy);
        repaint();
    };
    layerPress.onPress = [this] (const juce::MouseEvent& e)
    {
        const auto local = e.getEventRelativeTo (&layers).getPosition();
        const int row = layers.getRowContainingPosition (local.x, local.y);
        const auto& all = ctx.show.layers.getUserLayers();
        if (juce::isPositiveAndBelow (row, (int) all.size()))
            showLayerMenu (all[(size_t) row].id, localAreaToGlobal (getLayerRowBounds (row)));
    };
    layers.addMouseListener (&layerPress, true);
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

void LayerEditor::addSpacerAt (int index)
{
    if (layerId.isEmpty() || ! juce::isPositiveAndBelow (index, getNumEntries()))
        return;
    ctx.show.layers.insertUserLayerSlots (layerId, index, { state::FaderRef::blank() });
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
    // The on-screen keyboard: a touchscreen has no other.
    popups::TextOptions options;
    options.maxLength = 24;
    popups::editText (*this, title, initial,
                      [safe = juce::Component::SafePointer<LayerEditor> (this), done = std::move (done)] (const juce::String& text)
                      {
                          if (safe != nullptr)
                              done (text.trim());
                      },
                      options);
}

void LayerEditor::beginRename()
{
    if (const auto* l = ctx.show.layers.getUserLayer (layerId))
        askName ("RENAME LAYER", l->name, [this] (juce::String n) { renameSelected (n); });
}

void LayerEditor::showMenu (juce::PopupMenu menu, juce::Rectangle<int> screenArea, std::function<void (int)> chosen)
{
    ++menusShown;
    // With no display at all (a headless test machine) JUCE can't place a menu and would crash.
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        return;
    menu.setLookAndFeel (&lookAndFeelFor (theme));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea).withStandardItemHeight (sizes::kTouch).withMinimumWidth (200),
                        [safe = juce::Component::SafePointer<LayerEditor> (this), chosen = std::move (chosen)] (int item)
                        {
                            if (safe != nullptr && item > 0)
                                chosen (item);
                        });
}

void LayerEditor::showLayerMenu (juce::String id, juce::Rectangle<int> area)
{
    if (ctx.show.layers.getUserLayer (id) == nullptr)
        return;
    selectLayer (id);
    juce::PopupMenu menu;
    menu.addItem (1, "Rename");
    menu.addItem (2, "Colour");
    menu.addItem (3, "Duplicate");
    menu.addItem (4, "Bank");
    menu.addItem (5, "Delete");
    showMenu (menu, area, [this, area] (int item)
              {
                  switch (item)
                  {
                      case 1: beginRename(); break;
                      case 2: showColourMenu (area); break;
                      case 3: duplicateSelected(); break;
                      case 4: showBankMenu (area); break;
                      case 5: deleteSelected(); break;
                      default: break;
                  }
              });
}

void LayerEditor::showEntryMenu (int row, juce::Rectangle<int> area)
{
    const int n = getNumEntries();
    if (! juce::isPositiveAndBelow (row, n))
        return;
    entries.selectRow (row);
    juce::PopupMenu menu;
    menu.addItem (1, "Spacer before");
    menu.addItem (2, "Spacer after", row + 1 < n);
    menu.addItem (3, "Move up", row > 0);
    menu.addItem (4, "Move down", row + 1 < n);
    menu.addItem (5, "Remove");
    showMenu (menu, area, [this, row] (int item)
              {
                  entries.selectRow (row);
                  switch (item)
                  {
                      case 1: addSpacerAt (row); entries.selectRow (row + 1); break;
                      case 2: addSpacerAt (row + 1); break;
                      case 3: moveEntry (-1); break;
                      case 4: moveEntry (1); break;
                      case 5: removeEntryAt (row); break;
                      default: break;
                  }
              });
}

void LayerEditor::showDcaMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeelFor (theme));
    for (int d = 0; d < ctx.engine.getNumDcas(); ++d)
        menu.addItem (d + 1, "DCA " + juce::String (d + 1) + "  " + ctx.engine.getDcaName (d));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (dcaKey).withStandardItemHeight (sizes::kTouch).withMinimumWidth (200),
                        [safe = juce::Component::SafePointer<LayerEditor> (this)] (int item)
                        {
                            if (safe != nullptr && item > 0)
                                safe->addDca (item - 1);
                        });
}

void LayerEditor::showBankMenu (juce::Rectangle<int> screenArea)
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
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea).withStandardItemHeight (sizes::kTouch).withMinimumWidth (200),
                        [safe = juce::Component::SafePointer<LayerEditor> (this), choices] (int item)
                        {
                            if (safe != nullptr && item > 0)
                                safe->setSelectedBank (choices[(size_t) (item - 1)].first, choices[(size_t) (item - 1)].second);
                        });
}

void LayerEditor::showColourMenu (juce::Rectangle<int> screenArea)
{
    // A grid of swatches in a call-out: a list of names is hard to hit with a finger.
    auto grid = std::make_unique<ColourGrid> (theme);
    grid->onPick = [safe = juce::Component::SafePointer<LayerEditor> (this), g = grid.get()] (juce::uint32 argb)
    {
        const juce::Component::SafePointer<juce::Component> box (g->findParentComponentOfClass<juce::CallOutBox>());
        if (safe != nullptr)
            safe->setSelectedColour (argb);
        if (auto* b = dynamic_cast<juce::CallOutBox*> (box.getComponent()))
        {
            b->exitModalState (0);
            b->setVisible (false);
        }
    };
    auto* parent = getTopLevelComponent();
    juce::CallOutBox::launchAsynchronously (std::move (grid), parent->getLocalArea (nullptr, screenArea), parent);
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
    const auto face = juce::Rectangle<int> (w, h).reduced (2, sizes::kGapSmall / 2);
    if (selected || l.id == layerId)
    {
        g.setColour (theme.accent.withAlpha (0.3f));
        g.fillRoundedRectangle (face.toFloat(), sizes::kRadius);
    }
    g.setColour (l.colour != 0 ? juce::Colour (l.colour) : theme.outline);
    g.fillRect (face.getX() + 4, face.getY() + 6, 8, face.getHeight() - 12);
    g.setColour (theme.text);
    g.setFont (type::font (type::kValue));
    const auto banks = layerlink::describe (l.bank);
    g.drawText (l.name, 20, 0, banks.isEmpty() ? w - 24 : (w - 24) * 3 / 5, h, juce::Justification::centredLeft, true);
    if (banks.isNotEmpty())
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel));
        g.drawText (banks, 20 + (w - 24) * 3 / 5, 0, (w - 24) * 2 / 5, h, juce::Justification::centredRight, true);
    }
}

void LayerEditor::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (layerPress.wasFired() || e.mods.isPopupMenu())
        return; // the press that opened the menu
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
    const auto face = juce::Rectangle<int> (w, h).reduced (2, sizes::kGapSmall / 2);
    if (selected)
    {
        g.setColour (owner.theme.accent.withAlpha (0.3f));
        g.fillRoundedRectangle (face.toFloat(), sizes::kRadius);
    }
    // The handle: a finger on it drags the entry (six dots; the whole left edge is a touch target wide).
    if (text != "(spacer)")
    {
        g.setColour (owner.theme.textDim);
        for (int i = 0; i < 6; ++i)
            g.fillEllipse ((float) (sizes::kTouch / 2 - 6 + (i % 2) * 8), (float) (h / 2 - 10 + (i / 2) * 8), 3.0f, 3.0f);
    }
    g.setColour (text == "(spacer)" ? owner.theme.textDim : owner.theme.text);
    g.setFont (type::font (type::kValue));
    g.drawText (juce::String (row + 1) + "  " + text, sizes::kTouch, 0, w - sizes::kTouch - 4, h, juce::Justification::centredLeft, true);
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
    auto title = getLocalBounds().reduced (sizes::kGap).removeFromTop (24);
    g.drawText ("LAYERS (the same on the WING, SQ-5 and dLive)", title.removeFromLeft (title.getWidth() / 2), juce::Justification::centredLeft, true);
    g.setColour (theme.textDim);
    g.setFont (type::font (type::kCaption));
    g.drawText (hint, title, juce::Justification::centredRight, true);
}

void LayerEditor::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap);
    area.removeFromTop (24 + sizes::kGapSmall);
    auto left = area.removeFromLeft (area.getWidth() * 2 / 5);
    area.removeFromLeft (sizes::kGap);
    // The keys in rows under a list, `perRow` across (fewer when a touch target wouldn't fit); returns the room left for the list.
    const auto place = [this] (juce::Rectangle<int> r, int from, int to, int perRow)
    {
        const int n = to - from;
        perRow = juce::jlimit (1, perRow, (r.getWidth() + sizes::kGap) / (sizes::kTouch + sizes::kGap));
        const int rows = (n + perRow - 1) / perRow;
        const auto strip = r.removeFromBottom (rows * (sizes::kTouch + sizes::kGap));
        const int w = (strip.getWidth() - (perRow - 1) * sizes::kGap) / perRow;
        for (int i = 0; i < n; ++i)
            keys[(size_t) (from + i)]->setBounds (strip.getX() + (i % perRow) * (w + sizes::kGap),
                                                  strip.getY() + (i / perRow) * (sizes::kTouch + sizes::kGap) + sizes::kGap, w, sizes::kTouch);
        return r;
    };
    layers.setBounds (place (left, 0, 8, 2));
    entries.setBounds (place (area, 8, 14, 3));
}

juce::Rectangle<int> LayerEditor::getEntryRowBounds (int row) const
{
    return entries.getRowPosition (row, true) + entries.getPosition();
}

juce::Rectangle<int> LayerEditor::getLayerRowBounds (int row) const
{
    return layers.getRowPosition (row, true) + layers.getPosition();
}

juce::Rectangle<int> LayerEditor::getEntryHandleBounds (int row) const
{
    return getEntryRowBounds (row).withWidth (sizes::kTouch);
}

std::vector<juce::Rectangle<int>> LayerEditor::getKeyBounds() const
{
    std::vector<juce::Rectangle<int>> all;
    for (auto& k : keys)
        all.push_back (k->getBounds());
    return all;
}

int LayerEditor::getEntriesScroll() const
{
    return entries.getViewport() != nullptr ? entries.getViewport()->getViewPositionY() : 0;
}

//==============================================================================
// Drag editing
void LayerEditor::beginDrag (int row, juce::Point<int> at)
{
    if (! juce::isPositiveAndBelow (row, getNumEntries()))
        return;
    dragRow = row;
    dragAt = at;
    updateEdgeScroll();
    repaint();
}

void LayerEditor::moveDrag (juce::Point<int> at)
{
    if (dragRow < 0)
        return;
    dragAt = at;
    updateEdgeScroll();
    repaint();
}

void LayerEditor::updateEdgeScroll()
{
    // Held near the top or bottom of the entries (inside them: past them is "take out"), the list scrolls.
    if (dragRow >= 0 && dragAt.x >= entries.getX() && dragAt.x < entries.getRight())
        edgeScroll.update (dragAt.y, entries.getY(), entries.getBottom());
    else
        edgeScroll.stop();
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
    edgeScroll.stop();
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
        // The line where it lands, with a notch at each end so that it shows past a finger's shadow.
        const int n = getNumEntries();
        const int y = entries.getY() + (d.to < n ? entries.getRowPosition (d.to, true).getY() : n > 0 ? entries.getRowPosition (n - 1, true).getBottom() : 0);
        const int line = juce::jlimit (entries.getY() + 2, entries.getBottom() - 2, y);
        g.setColour (theme.accent);
        g.fillRect (entries.getX(), line - 2, entries.getWidth(), 4);
        g.fillRect (entries.getX(), line - 7, 4, 14);
        g.fillRect (entries.getRight() - 4, line - 7, 4, 14);
    }
    else if (d.kind == Drop::Add)
    {
        // The layer it is added to is outlined.
        const auto& all = ctx.show.layers.getUserLayers();
        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].id == d.layerId)
            {
                g.setColour (theme.accent);
                g.drawRoundedRectangle (getLayerRowBounds (i).toFloat().reduced (1.0f), sizes::kRadius, 3.0f);
            }
    }
    // The label rides above the finger, not under it.
    const auto label = (d.kind == Drop::Remove ? "REMOVE  " : "") + entryText (dragRow);
    g.setFont (type::font (type::kValue, true));
    const int w = juce::jmax (80, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), label) + 20);
    const juce::Rectangle<int> ghost (dragAt.x - w / 2, dragAt.y - 28 - sizes::kTouch / 2, w, 32);
    g.setColour ((d.kind == Drop::Remove ? juce::Colour (0xffd04040) : theme.accent).withAlpha (0.85f));
    g.fillRoundedRectangle (ghost.toFloat(), sizes::kRadius);
    g.setColour (juce::Colours::white);
    g.drawText (label, ghost, juce::Justification::centred, true);
}

} // namespace livedaw::ui::universal
