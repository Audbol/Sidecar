#include "ui/universal/LibraryView.h"

#include "engine/Show.h"
#include "ui/ShowPanel.h"
#include "ui/universal/Groups.h"

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kRow = 44;

    const char* extensionOf (LibraryView::Filter f)
    {
        switch (f)
        {
            case LibraryView::Filter::Scenes:   return Show::sceneFileExtension;
            case LibraryView::Filter::Snippets: return Show::snippetFileExtension;
            case LibraryView::Filter::Channel:  return Show::channelPresetExtension;
            case LibraryView::Filter::Fx:       return Show::fxPresetExtension;
            case LibraryView::Filter::All:      break;
        }
        return nullptr;
    }
} // namespace

/** The files, drawn; a tap selects one. */
class LibraryView::List final : public juce::Component
{
public:
    explicit List (LibraryView& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.theme;
        if (owner.files.empty())
        {
            g.setColour (t.textDim);
            g.setFont (type::font (type::kLabel));
            g.drawFittedText ("No files here yet: SAVE SCENE, and the STRIP page's and FX RACK's SAVE PRESET, put them in the Library folder",
                              getLocalBounds().reduced (8), juce::Justification::centredTop, 2, 0.8f);
            return;
        }
        for (int i = 0; i < (int) owner.files.size(); ++i)
        {
            const auto& e = owner.files[(size_t) i];
            const auto r = juce::Rectangle<int> (0, i * (kRow + sizes::kGapSmall), getWidth(), kRow).toFloat();
            const bool sel = i == owner.selected;
            g.setColour (sel ? t.lifted (t.raised, 0.1f) : t.raised);
            g.fillRoundedRectangle (r, sizes::kRadius);
            g.setColour (sel ? t.accent : t.outline);
            g.drawRoundedRectangle (r.reduced (0.5f), sizes::kRadius, sel ? 2.0f : 1.0f);
            auto inner = r.reduced (12.0f, 0.0f);
            g.setColour (t.textDim);
            g.setFont (type::font (type::kCaption, true));
            auto kind = inner.removeFromLeft (130.0f);
            g.drawText (e.kind, kind, juce::Justification::centredLeft);
            const auto detail = e.detail + "   " + e.file.getLastModificationTime().formatted ("%d %b %Y %H:%M");
            const float detailW = juce::jmin (inner.getWidth() * 0.45f, juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), detail) + 8.0f);
            g.drawText (detail, inner.removeFromRight (detailW), juce::Justification::centredRight);
            g.setColour (t.text);
            g.setFont (type::font (type::kLabel, true));
            g.drawFittedText (e.file.getFileNameWithoutExtension(), inner.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasDraggedSinceMouseDown())
            owner.select (e.y / (kRow + sizes::kGapSmall));
    }

private:
    LibraryView& owner;
};

LibraryView::LibraryView (UiContext& c, const Theme& t)
    : allKey ("ALL", t.accent, t), scenesKey ("SCENES", t.accent, t), snippetsKey ("SNIPPETS", t.accent, t), channelKey ("CHANNEL", t.accent, t),
      fxKey ("FX", t.accent, t), loadKey ("LOAD", t.accent, t), addKey ("ADD TO SHOW", t.accent, t), renameKey ("RENAME", t.accent, t),
      deleteKey ("DELETE", t.mute, t), saveSceneKey ("SAVE SCENE", t.accent, t), slotChoice (t), ctx (c), theme (t), list (std::make_unique<List> (*this))
{
    const std::pair<ToggleKey*, Filter> filters[] = { { &allKey, Filter::All },
                                                      { &scenesKey, Filter::Scenes },
                                                      { &snippetsKey, Filter::Snippets },
                                                      { &channelKey, Filter::Channel },
                                                      { &fxKey, Filter::Fx } };
    for (auto [key, f] : filters)
    {
        key->isOn = [this, f = f] { return filter == f; };
        key->onClick = [this, f = f] { setFilter (f); };
        addAndMakeVisible (*key);
    }
    loadKey.onClick = [this] { said (load()); };
    addKey.onClick = [this] { said (addToShow()); };
    saveSceneKey.onClick = [this] { said (saveScene()); };
    renameKey.onClick = [this]
    {
        const auto file = getFile (selected);
        if (file == juce::File())
            return said ("Select a file");
        juce::Component::SafePointer<LibraryView> safe (this);
        askForText (*this, "RENAME", "New name:", file.getFileNameWithoutExtension(), [safe] (const juce::String& name)
        {
            if (safe != nullptr)
                safe->said (safe->rename (name));
        });
    };
    deleteKey.onClick = [this]
    {
        const auto file = getFile (selected);
        if (file == juce::File())
            return said ("Select a file");
        juce::Component::SafePointer<LibraryView> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withTitle ("DELETE")
                                          .withMessage ("Move " + file.getFileName() + " to the bin?")
                                          .withButton ("OK")
                                          .withButton ("CANCEL")
                                          .withAssociatedComponent (this),
                                      [safe] (int result)
                                      {
                                          if (safe != nullptr && result == 1)
                                              safe->said (safe->remove());
                                      });
    };
    for (auto* k : { &loadKey, &addKey, &renameKey, &deleteKey, &saveSceneKey })
        addAndMakeVisible (*k);

    // The FX slot an FX preset loads into.
    slotChoice.caption = "FX SLOT";
    slotChoice.items = [this]
    {
        juce::StringArray items;
        for (int s = 0; s < ctx.engine.getFx().getNumSlots(); ++s)
            items.add ("FX " + juce::String (s + 1));
        return items;
    };
    slotChoice.current = [this] { return fxSlot; };
    slotChoice.choose = [this] (int i) { fxSlot = i; };
    addAndMakeVisible (slotChoice);

    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    addAndMakeVisible (viewport);
    refresh();
}

LibraryView::~LibraryView()
{
    stopTimer();
}

void LibraryView::visibilityChanged()
{
    if (isVisible())
    {
        refresh();
        startTimer (2000);
    }
    else
        stopTimer();
}

void LibraryView::setFilter (Filter f)
{
    filter = f;
    const auto keep = getFile (selected);
    refresh();
    selectFile (keep);
    for (auto* k : { &allKey, &scenesKey, &snippetsKey, &channelKey, &fxKey })
        k->refresh();
}

void LibraryView::refresh()
{
    slotChoice.refresh();
    const auto keep = getFile (selected);
    std::vector<Entry> found;
    const auto folder = ShowPanel::libraryFolder();
    for (const auto f : { Filter::Scenes, Filter::Snippets, Filter::Channel, Filter::Fx })
    {
        if (filter != Filter::All && filter != f)
            continue;
        for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + extensionOf (f)))
        {
            Entry e { file, {}, {} };
            if (f == Filter::Scenes || f == Filter::Snippets)
            {
                const auto info = Show::readLibraryFile (file);
                e.kind = f == Filter::Scenes ? "SCENE" : "SNIPPET";
                e.detail = f == Filter::Scenes ? juce::String (info.count) + (info.count == 1 ? " SNIPPET" : " SNIPPETS")
                                               : juce::String (info.count) + (info.count == 1 ? " PARAMETER" : " PARAMETERS");
            }
            else
            {
                const auto info = Show::readPreset (file);
                e.kind = f == Filter::Channel ? "CHANNEL PRESET" : "FX PRESET";
                e.detail = info.detail.toUpperCase();
            }
            found.push_back (std::move (e));
        }
    }
    std::sort (found.begin(), found.end(), [] (const Entry& a, const Entry& b)
    {
        return a.file.getFileName().compareNatural (b.file.getFileName()) < 0;
    });
    // Repaint only when what is listed changed.
    juce::String now;
    for (const auto& e : found)
        now << e.file.getFullPathName() << e.detail << e.file.getLastModificationTime().toMilliseconds() << "\n";
    files = std::move (found);
    if (now != shown)
    {
        shown = now;
        selected = -1;
        selectFile (keep);
        resized();
        list->repaint();
    }
}

juce::File LibraryView::getFile (int index) const
{
    return juce::isPositiveAndBelow (index, (int) files.size()) ? files[(size_t) index].file : juce::File();
}

void LibraryView::select (int index)
{
    selected = juce::isPositiveAndBelow (index, (int) files.size()) ? index : -1;
    list->repaint();
}

void LibraryView::selectFile (const juce::File& file)
{
    int index = -1;
    for (int i = 0; i < (int) files.size(); ++i)
        if (files[(size_t) i].file == file)
            index = i;
    select (index);
}

void LibraryView::said (const juce::String& what)
{
    status = what;
    if (what.isNotEmpty())
        ctx.log.info ("Library: " + what);
    repaint (statusArea);
}

juce::String LibraryView::load()
{
    const auto file = getFile (selected);
    if (file == juce::File())
        return "Select a file";
    const auto ext = file.getFileExtension().toLowerCase();
    if (ext == Show::sceneFileExtension || ext == Show::snippetFileExtension)
    {
        juce::String error;
        const auto r = ctx.show.applyLibraryFile (file, error);
        if (error.isNotEmpty())
            return error;
        logRecall (ctx, file.getFileNameWithoutExtension(), r);
        return "Loaded " + file.getFileNameWithoutExtension() + (r.warnings.isEmpty() ? juce::String() : " (" + r.warnings.joinIntoString ("; ") + ")");
    }
    if (ext == Show::channelPresetExtension)
    {
        auto* strip = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
        if (strip == nullptr)
            return "Select the strip to load it onto";
        const auto r = ctx.show.loadChannelPreset (state::StripRef::of (*strip), file);
        return r.wasOk() ? "Loaded " + file.getFileNameWithoutExtension() + " onto " + strip->getLabel() : r.getErrorMessage();
    }
    if (ext == Show::fxPresetExtension)
    {
        const auto r = ctx.show.loadFxPreset (fxSlot, file);
        return r.wasOk() ? "Loaded " + file.getFileNameWithoutExtension() + " into FX " + juce::String (fxSlot + 1) : r.getErrorMessage();
    }
    return "Sidecar can't load " + file.getFileName();
}

juce::String LibraryView::addToShow()
{
    const auto file = getFile (selected);
    if (file == juce::File())
        return "Select a file";
    auto* strip = ctx.getSelectedStrip ? ctx.getSelectedStrip() : nullptr;
    const bool channel = file.getFileExtension().toLowerCase() == Show::channelPresetExtension;
    if (channel && strip == nullptr)
        return "Select the strip the preset's scene loads onto";
    juce::String error;
    const int index = ctx.show.addItem (file, -1, fxSlot, strip != nullptr ? state::StripRef::of (*strip) : state::StripRef {}, error);
    if (index < 0)
        return error.isNotEmpty() ? error : "Couldn't add " + file.getFileName();
    return "Added " + file.getFileNameWithoutExtension() + " as scene " + juce::String (index + 1);
}

juce::String LibraryView::rename (const juce::String& newName)
{
    const auto file = getFile (selected);
    const auto legal = juce::File::createLegalFileName (newName.trim());
    if (file == juce::File() || legal.isEmpty())
        return "Select a file and give it a name";
    const auto to = file.getSiblingFile (legal + file.getFileExtension());
    if (to == file)
        return {};
    if (to.exists())
        return to.getFileName() + " is there already";
    if (! file.moveFileTo (to))
        return "Couldn't rename " + file.getFileName();
    refresh();
    selectFile (to);
    return "Renamed to " + to.getFileNameWithoutExtension();
}

juce::String LibraryView::remove()
{
    const auto file = getFile (selected);
    if (file == juce::File())
        return "Select a file";
    if (! file.moveToTrash() && ! file.deleteFile())
        return "Couldn't delete " + file.getFileName();
    refresh();
    return "Deleted " + file.getFileNameWithoutExtension();
}

juce::String LibraryView::saveScene()
{
    auto& show = ctx.show;
    const int current = show.getCurrentSceneIndex();
    const auto scene = current >= 0 ? show.getScene (current) : std::nullopt;
    if (! scene.has_value())
        return "Recall a scene first: SAVE SCENE writes the current one";
    const auto folder = ShowPanel::libraryFolder();
    folder.createDirectory();
    const auto name = juce::File::createLegalFileName (scene->name.isNotEmpty() ? scene->name : "Scene " + juce::String (current + 1));
    const auto file = folder.getNonexistentChildFile (name, Show::sceneFileExtension, false);
    const auto r = show.exportScene (current, file);
    if (! r.wasOk())
        return r.getErrorMessage();
    refresh();
    selectFile (file);
    return "Saved scene " + juce::String (current + 1) + " as " + file.getFileNameWithoutExtension();
}

void LibraryView::paint (juce::Graphics& g)
{
    // The folder, its start dropped while it doesn't fit (its end says the most).
    g.setColour (theme.textDim);
    const auto font = type::font (type::kCaption, true);
    g.setFont (font);
    const auto sep = juce::File::getSeparatorString();
    const auto ellipsis = juce::String::fromUTF8 ("\xe2\x80\xa6");
    auto parts = juce::StringArray::fromTokens (ShowPanel::libraryFolder().getFullPathName(), sep, {});
    auto path = parts.joinIntoString (sep);
    while (parts.size() > 1 && juce::GlyphArrangement::getStringWidth (font, path) > (float) folderArea.getWidth())
    {
        parts.remove (0);
        path = ellipsis + sep + parts.joinIntoString (sep);
    }
    g.drawText (path, folderArea, juce::Justification::centredRight);
    if (status.isNotEmpty())
    {
        g.setColour (theme.text);
        g.setFont (type::font (type::kLabel));
        g.drawFittedText (status, statusArea, juce::Justification::centredLeft, 1, 0.8f);
    }
}

void LibraryView::resized()
{
    auto area = getLocalBounds().reduced (sizes::kGap, sizes::kGapSmall);
    // The filters, the folder at their right.
    auto top = area.removeFromTop (sizes::kTouch);
    const int fw = juce::jmin (130, (top.getWidth() - 4 * sizes::kGapSmall) / 6);
    for (auto* k : { &allKey, &scenesKey, &snippetsKey, &channelKey, &fxKey })
    {
        k->setBounds (top.removeFromLeft (fw));
        top.removeFromLeft (sizes::kGapSmall);
    }
    folderArea = top.withTrimmedLeft (sizes::kGap);
    area.removeFromTop (sizes::kGapSmall);
    // The keys for the file selected, the FX slot at their right.
    auto keys = area.removeFromTop (sizes::kTouch);
    const int kw = juce::jmin (150, (keys.getWidth() - 5 * sizes::kGapSmall) / 6);
    for (auto* k : { &loadKey, &addKey, &renameKey, &deleteKey, &saveSceneKey })
    {
        k->setBounds (keys.removeFromLeft (kw));
        keys.removeFromLeft (sizes::kGapSmall);
    }
    slotChoice.setBounds (keys.removeFromLeft (juce::jmin (keys.getWidth(), 200)));
    statusArea = area.removeFromTop (26);
    area.removeFromTop (sizes::kGapSmall);
    viewport.setBounds (area);
    list->setSize (viewport.getWidth() - viewport.getScrollBarThickness(),
                   juce::jmax (kRow * 2, (int) files.size() * (kRow + sizes::kGapSmall)));
}

} // namespace livedaw::ui::universal
