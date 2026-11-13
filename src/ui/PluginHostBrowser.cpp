#include "ui/PluginHostBrowser.h"

#include "engine/PluginScanner.h"
#include "ui/WingPopups.h"

#include <algorithm>

namespace livedaw::ui::ph
{
using namespace universal;

namespace
{
    constexpr int kDragPixels = 8;

    /** The identifiers kept under `key`, one a line. */
    juce::StringArray lines (juce::PropertiesFile& settings, const char* key)
    {
        juce::StringArray out;
        out.addLines (settings.getValue (key));
        out.removeEmptyStrings();
        return out;
    }

    void keep (juce::PropertiesFile& settings, const char* key, const juce::StringArray& values)
    {
        settings.setValue (key, values.joinIntoString ("\n"));
        settings.saveIfNeeded();
    }
} // namespace

//==============================================================================
// Library

Library::Library (juce::PropertiesFile& s) : settings (s) {}

bool Library::isFavourite (const juce::String& id) const
{
    return lines (settings, favouritesKey).contains (id);
}

void Library::setFavourite (const juce::String& id, bool favourite)
{
    auto list = lines (settings, favouritesKey);
    list.removeString (id);
    if (favourite)
        list.add (id);
    keep (settings, favouritesKey, list);
}

juce::StringArray Library::parseTags (const juce::String& text)
{
    juce::StringArray tags;
    for (auto tag : juce::StringArray::fromTokens (text.replaceCharacters ("\t\r\n", "   "), ",", {}))
    {
        tag = tag.trim().substring (0, kMaxTagLength).trim();
        if (tag.isNotEmpty() && ! tags.contains (tag, true) && tags.size() < kMaxTags)
            tags.add (tag);
    }
    return tags;
}

juce::StringArray Library::getTags (const juce::String& id) const
{
    for (const auto& line : lines (settings, tagsKey))
        if (line.upToFirstOccurrenceOf ("\t", false, false) == id)
            return parseTags (line.fromFirstOccurrenceOf ("\t", false, false));
    return {};
}

void Library::setTags (const juce::String& id, const juce::StringArray& tags)
{
    juce::StringArray list;
    for (const auto& line : lines (settings, tagsKey))
        if (line.upToFirstOccurrenceOf ("\t", false, false) != id)
            list.add (line);
    const auto clean = parseTags (tags.joinIntoString (","));
    if (! clean.isEmpty())
        list.add (id + "\t" + clean.joinIntoString (","));
    keep (settings, tagsKey, list);
}

juce::StringArray Library::getRecent() const
{
    return lines (settings, recentKey);
}

void Library::addRecent (const juce::String& id)
{
    auto list = lines (settings, recentKey);
    list.removeString (id);
    list.insert (0, id);
    while (list.size() > kMaxRecent)
        list.remove (list.size() - 1);
    keep (settings, recentKey, list);
}

//==============================================================================
// BrowserPage

BrowserPage::BrowserPage (UiContext& c, const Theme& t, Library& l, Guard& g)
    : ctx (c), theme (t), library (l), guard (g), searchKey ("SEARCH", t.accent, t), clearKey ("CLEAR", t.accent, t), allKey ("ALL", t.accent, t),
      favouritesKey ("FAVOURITES", t.accent, t), recentKey ("RECENT", t.accent, t), scanKey ("SCAN", t.accent, t), rescanKey ("RESCAN", t.accent, t)
{
    searchKey.onClick = [this] { askSearch(); };
    clearKey.onClick = [this] { setSearch ({}); };
    allKey.onClick = [this] { setFilter (Filter::All); };
    favouritesKey.onClick = [this] { setFilter (Filter::Favourites); };
    recentKey.onClick = [this] { setFilter (Filter::Recent); };
    scanKey.onClick = [this] { scan(); };
    rescanKey.onClick = [this] { rescan(); };
    allKey.isOn = [this] { return filter == Filter::All; };
    favouritesKey.isOn = [this] { return filter == Filter::Favourites; };
    recentKey.isOn = [this] { return filter == Filter::Recent; };
    searchKey.setTooltip ("Searches the names, makers, formats and tags: opens the on-screen keyboard");
    clearKey.setTooltip ("Clears the search");
    allKey.setTooltip ("Every scanned plug-in");
    favouritesKey.setTooltip ("The plug-ins with a star");
    recentKey.setTooltip ("The last 10 plug-ins added to a rack");
    scanKey.setTooltip ("Opens the plug-in scanner: it looks for new plug-ins");
    rescanKey.setTooltip ("Forgets the plug-ins that failed, so that they are tried again, and opens the scanner");
    for (auto* k : { &searchKey, &clearKey, &allKey, &favouritesKey, &recentKey, &scanKey, &rescanKey })
        addAndMakeVisible (*k);
    ctx.host.getKnownPlugins().addChangeListener (this);
    refresh();
    startTimerHz (2);
}

BrowserPage::~BrowserPage()
{
    stopTimer();
    ctx.host.getKnownPlugins().removeChangeListener (this);
}

//==============================================================================
void BrowserPage::setSearch (const juce::String& text)
{
    search = text.trim();
    rowTop = 0;
    refresh();
}

void BrowserPage::askSearch()
{
    popups::TextOptions options;
    options.maxLength = 40;
    popups::editText (*this, "SEARCH PLUG-INS", search, [safe = juce::Component::SafePointer<BrowserPage> (this)] (const juce::String& text)
                      {
                          if (safe != nullptr)
                              safe->setSearch (text);
                      },
                      options);
}

void BrowserPage::setFilter (Filter f)
{
    filter = f;
    rowTop = 0;
    refresh();
}

void BrowserPage::toggleFavourite (int row)
{
    if (! juce::isPositiveAndBelow (row, getNumRows()))
        return;
    library.setFavourite (rows[(size_t) row].id, ! rows[(size_t) row].favourite);
    refresh();
}

void BrowserPage::askTags (int row)
{
    if (! juce::isPositiveAndBelow (row, getNumRows()))
        return;
    const auto& r = rows[(size_t) row];
    popups::TextOptions options;
    options.maxLength = 80;
    popups::editText (*this, "TAGS FOR " + r.description.name.toUpperCase() + " (SEPARATED BY COMMAS)", r.tags.joinIntoString (", "),
                      [safe = juce::Component::SafePointer<BrowserPage> (this), id = r.id] (const juce::String& text)
                      {
                          if (safe == nullptr)
                              return;
                          safe->library.setTags (id, Library::parseTags (text));
                          safe->refresh();
                      },
                      options);
}

void BrowserPage::setTags (int row, const juce::String& text)
{
    if (! juce::isPositiveAndBelow (row, getNumRows()))
        return;
    library.setTags (rows[(size_t) row].id, Library::parseTags (text));
    refresh();
}

bool BrowserPage::addRow (int row)
{
    if (! juce::isPositiveAndBelow (row, getNumRows()) || ! onAdd)
        return false;
    const auto description = rows[(size_t) row].description;
    const bool added = onAdd (description);
    const int channel = getChannel ? getChannel() : 0;
    note = added ? "ADDED " + description.name.toUpperCase() + " TO CH " + juce::String (channel + 1) : "COULD NOT ADD " + description.name.toUpperCase();
    refresh();
    repaint();
    return added;
}

juce::String BrowserPage::getMessage() const
{
    auto text = note;
    if (guard.getNotice().isNotEmpty())
        text << (text.isNotEmpty() ? "   " : "") << guard.getNotice();
    return text;
}

void BrowserPage::scan()
{
    note = "THE SCANNER IS OPEN: IT LOOKS FOR NEW PLUG-INS";
    if (ctx.showPluginScanner)
        ctx.showPluginScanner();
    refresh();
    repaint();
}

void BrowserPage::rescan()
{
    auto& known = ctx.host.getKnownPlugins();
    const int failed = known.getBlacklistedFiles().size();
    for (const auto& file : known.getBlacklistedFiles())
        ScanFailures::forget (file);
    known.clearBlacklistedFiles();
    note = failed > 0 ? juce::String (failed) + (failed == 1 ? " PLUG-IN THAT FAILED WILL" : " PLUG-INS THAT FAILED WILL") + " BE TRIED AGAIN"
                      : juce::String ("NOTHING HAD FAILED: THE SCANNER LOOKS FOR NEW PLUG-INS");
    if (ctx.showPluginScanner)
        ctx.showPluginScanner();
    refresh();
    repaint();
}

//==============================================================================
void BrowserPage::refresh()
{
    const auto types = ctx.host.getKnownPlugins().getTypes();
    numPlugins = types.size();

    std::vector<Row> all;
    all.reserve ((size_t) types.size());
    for (const auto& d : types)
    {
        Row r;
        r.description = d;
        r.id = Library::idOf (d);
        r.favourite = library.isFavourite (r.id);
        r.tags = library.getTags (r.id);
        all.push_back (std::move (r));
    }

    // The search: every word has to be in the name, maker, format or a tag.
    const auto words = juce::StringArray::fromTokens (search, " ", {});
    const auto matches = [&] (const Row& r)
    {
        const auto hay = r.description.name + " " + r.description.manufacturerName + " " + r.description.pluginFormatName + " " + r.tags.joinIntoString (" ");
        for (const auto& w : words)
            if (w.isNotEmpty() && ! hay.containsIgnoreCase (w))
                return false;
        return true;
    };

    rows.clear();
    if (filter == Filter::Recent)
    {
        for (const auto& id : library.getRecent())
            for (const auto& r : all)
                if (r.id == id && matches (r))
                    rows.push_back (r);
    }
    else
    {
        for (const auto& r : all)
            if ((filter == Filter::All || r.favourite) && matches (r))
                rows.push_back (r);
        std::stable_sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b)
                          {
                              const int byName = a.description.name.compareIgnoreCase (b.description.name);
                              return byName != 0 ? byName < 0 : a.description.pluginFormatName < b.description.pluginFormatName;
                          });
    }

    failures.clear();
    for (const auto& file : ctx.host.getKnownPlugins().getBlacklistedFiles())
    {
        Failure f;
        f.file = file;
        // The plug-in's file name without its extension (either separator: a path is as the scanner found it); an identifier as it is.
        const auto leaf = file.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);
        f.name = file.containsAnyOf ("/\\") && leaf.contains (".") ? leaf.upToLastOccurrenceOf (".", false, false) : leaf;
        f.reason = ScanFailures::reasonFor (file);
        if (f.reason.isEmpty())
            f.reason = "it crashed, hung or could not be loaded when it was last scanned or loaded; no reason was kept";
        failures.push_back (std::move (f));
    }
    clampScroll();

    searchKey.setButtonText (search.isEmpty() ? juce::String ("SEARCH...") : "SEARCH: " + search.toUpperCase());
    for (auto* k : { &allKey, &favouritesKey, &recentKey })
        k->refresh();

    auto now = search + "|" + juce::String ((int) filter) + "|" + juce::String (numPlugins) + "|" + juce::String (rowTop) + "|" + juce::String (failureTop) + "|" + getMessage();
    for (const auto& r : rows)
        now << "|" << r.id << (r.favourite ? "*" : "") << r.tags.joinIntoString (",");
    for (const auto& f : failures)
        now << "|" << f.file << f.reason;
    if (now != signature)
    {
        signature = now;
        repaint();
    }
}

//==============================================================================
juce::Rectangle<int> BrowserPage::listRect() const
{
    return { 8, kTop, kListWidth, getHeight() - kTop - kMessageHeight };
}

juce::Rectangle<int> BrowserPage::reportRect() const
{
    return { kReportX, 0, getWidth() - kReportX - 8, getHeight() };
}

juce::Rectangle<int> BrowserPage::failuresRect() const
{
    return reportRect().withTrimmedTop (150).withTrimmedBottom (8);
}

int BrowserPage::visibleRows() const
{
    return juce::jmax (1, listRect().getHeight() / kRow);
}

int BrowserPage::visibleFailures() const
{
    return juce::jmax (1, failuresRect().getHeight() / kRow);
}

void BrowserPage::clampScroll()
{
    rowTop = juce::jlimit (0, juce::jmax (0, getNumRows() - visibleRows()), rowTop);
    failureTop = juce::jlimit (0, juce::jmax (0, (int) failures.size() - visibleFailures()), failureTop);
}

void BrowserPage::scrollRows (int firstRow)
{
    rowTop = firstRow;
    clampScroll();
    repaint();
}

juce::Rectangle<int> BrowserPage::getRowBounds (int row) const
{
    const auto list = listRect();
    return { list.getX(), list.getY() + (row - rowTop) * kRow, list.getWidth(), kRow };
}

juce::Rectangle<int> BrowserPage::getStarBounds (int row) const
{
    return getRowBounds (row).removeFromLeft (kStarWidth);
}

juce::Rectangle<int> BrowserPage::getTagsBounds (int row) const
{
    return getRowBounds (row).removeFromRight (kTagsWidth);
}

void BrowserPage::resized()
{
    searchKey.setBounds (8, 6, 560, kKey);
    clearKey.setBounds (576, 6, 110, kKey);
    allKey.setBounds (704, 6, 120, kKey);
    favouritesKey.setBounds (832, 6, 190, kKey);
    recentKey.setBounds (1030, 6, 140, kKey);
    const auto r = reportRect();
    scanKey.setBounds (r.getX(), 6, (r.getWidth() - 8) / 2, kKey);
    rescanKey.setBounds (r.getX() + (r.getWidth() - 8) / 2 + 8, 6, (r.getWidth() - 8) / 2, kKey);
    clampScroll();
}

//==============================================================================
void BrowserPage::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    // The count and what a tap does.
    {
        const auto line = juce::Rectangle<int> (8, 6 + kKey, kListWidth, kTop - 6 - kKey);
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kCaption, true));
        const int ch = getChannel ? getChannel() : 0;
        g.drawText (juce::String (getNumRows()) + " OF " + juce::String (numPlugins) + " PLUG-INS   -   TAP ONE TO ADD IT TO CH " + juce::String (ch + 1)
                        + "   -   THE STAR MAKES IT A FAVOURITE",
                    line.withTrimmedLeft (4), juce::Justification::centredLeft);
    }

    // The list.
    {
        const auto list = listRect();
        g.saveState();
        g.reduceClipRegion (list);
        if (rows.empty())
        {
            g.setColour (theme.textDim);
            g.setFont (type::font (type::kValue));
            const auto text = numPlugins == 0 ? juce::String ("No plug-ins have been scanned yet. SCAN (at the right) looks for them.")
                              : filter == Filter::Favourites && search.isEmpty() ? juce::String ("No favourites yet: tap the star of a plug-in.")
                              : filter == Filter::Recent && search.isEmpty()     ? juce::String ("Nothing has been added yet: the last 10 plug-ins you add are here.")
                                                                                 : juce::String ("No plug-in matches the search.");
            g.drawFittedText (text, list.reduced (24, 20).withHeight (90), juce::Justification::centredTop, 3);
        }
        for (int i = rowTop; i < getNumRows() && i < rowTop + visibleRows() + 1; ++i)
        {
            const auto& r = rows[(size_t) i];
            const auto b = getRowBounds (i).reduced (0, 2).toFloat();
            g.setColour (theme.panel);
            g.fillRoundedRectangle (b, 6.0f);
            const auto star = getStarBounds (i).toFloat().withSizeKeepingCentre (26.0f, 26.0f);
            juce::Path p;
            p.addStar (star.getCentre(), 5, star.getWidth() * 0.22f, star.getWidth() * 0.5f);
            if (r.favourite)
            {
                g.setColour (theme.accent);
                g.fillPath (p);
            }
            else
            {
                g.setColour (theme.outline);
                g.strokePath (p, juce::PathStrokeType (1.5f));
            }
            auto area = b.toNearestInt().withTrimmedLeft (kStarWidth).withTrimmedRight (kTagsWidth + 8);
            auto tagsArea = area.removeFromRight (440);
            g.setColour (theme.text);
            g.setFont (type::font (type::kValue, true));
            g.drawFittedText (r.description.name, area.removeFromTop (28).withTrimmedTop (4), juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (theme.textDim);
            g.setFont (type::font (type::kCaption));
            g.drawFittedText (r.description.manufacturerName + (r.description.manufacturerName.isNotEmpty() ? "   " : "") + r.description.pluginFormatName
                                  + (r.description.isInstrument ? "   INSTRUMENT" : ""),
                              area, juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (theme.accent);
            g.setFont (type::font (type::kLabel));
            g.drawFittedText (r.tags.isEmpty() ? juce::String() : r.tags.joinIntoString ("   "), tagsArea, juce::Justification::centredRight, 1, 0.8f);
            const auto key = getTagsBounds (i).reduced (4, 4).toFloat();
            g.setColour (theme.well);
            g.fillRoundedRectangle (key, 6.0f);
            g.setColour (theme.outline);
            g.drawRoundedRectangle (key.reduced (0.5f), 6.0f, 1.5f);
            g.setColour (theme.textDim);
            g.setFont (type::font (type::kLabel, true));
            g.drawText ("TAGS", key, juce::Justification::centred);
        }
        g.restoreState();
        g.setColour (guard.getNotice().isNotEmpty() ? theme.mute : theme.textDim);
        g.setFont (type::font (type::kCaption, guard.getNotice().isNotEmpty()));
        g.drawFittedText (getMessage(), juce::Rectangle<int> (list.getX() + 4, list.getBottom() + 4, list.getWidth() - 8, kMessageHeight - 4), juce::Justification::centredLeft, 3);
    }

    // The scan report.
    {
        const auto r = reportRect();
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kLabel, true));
        g.drawText ("SCAN REPORT", juce::Rectangle<int> (r.getX() + 4, 6 + kKey + 4, r.getWidth(), 28), juce::Justification::centredLeft);
        g.setColour (theme.text);
        g.setFont (type::font (type::kValue));
        g.drawText (juce::String (numPlugins) + (numPlugins == 1 ? " PLUG-IN FOUND" : " PLUG-INS FOUND"), juce::Rectangle<int> (r.getX() + 4, 6 + kKey + 34, r.getWidth(), 26),
                    juce::Justification::centredLeft);
        g.setColour (failures.empty() ? theme.meterLow : theme.mute);
        g.drawText (failures.empty() ? juce::String ("NOTHING FAILED") : juce::String ((int) failures.size()) + (failures.size() == 1 ? " FAILED" : " FAILED, AND WHY"),
                    juce::Rectangle<int> (r.getX() + 4, 6 + kKey + 62, r.getWidth(), 26), juce::Justification::centredLeft);
        const auto list = failuresRect();
        g.saveState();
        g.reduceClipRegion (list);
        for (int i = failureTop; i < (int) failures.size() && i < failureTop + visibleFailures() + 1; ++i)
        {
            const auto b = juce::Rectangle<int> (list.getX(), list.getY() + (i - failureTop) * kRow, list.getWidth(), kRow).reduced (0, 2);
            g.setColour (theme.panel);
            g.fillRoundedRectangle (b.toFloat(), 6.0f);
            auto area = b.reduced (10, 2);
            g.setColour (theme.text);
            g.setFont (type::font (type::kLabel, true));
            g.drawFittedText (failures[(size_t) i].name, area.removeFromTop (22), juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (theme.mute);
            g.setFont (type::font (type::kCaption));
            g.drawFittedText (failures[(size_t) i].reason, area, juce::Justification::centredLeft, 2, 0.8f);
        }
        g.restoreState();
    }
}

//==============================================================================
void BrowserPage::mouseDown (const juce::MouseEvent& e)
{
    dragged = false;
    dragFrom = e.y;
    drag = listRect().contains (e.getPosition()) ? Area::List : failuresRect().contains (e.getPosition()) ? Area::Failures : Area::None;
}

void BrowserPage::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Area::None || (! dragged && std::abs (e.y - dragFrom) < kDragPixels))
        return;
    dragged = true;
    const int moved = (dragFrom - e.y) / kRow;
    if (moved == 0)
        return;
    dragFrom = e.y;
    (drag == Area::List ? rowTop : failureTop) += moved;
    clampScroll();
    repaint();
}

void BrowserPage::mouseUp (const juce::MouseEvent& e)
{
    const bool wasDrag = dragged || e.mouseWasDraggedSinceMouseDown();
    const auto area = drag;
    dragged = false;
    drag = Area::None;
    if (wasDrag || area != Area::List)
        return;
    const int row = rowTop + (e.y - listRect().getY()) / kRow;
    if (! juce::isPositiveAndBelow (row, getNumRows()))
        return;
    if (getStarBounds (row).contains (e.getPosition()))
        toggleFavourite (row);
    else if (getTagsBounds (row).contains (e.getPosition()))
        askTags (row);
    else
        addRow (row);
}

void BrowserPage::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int moved = wheel.deltaY > 0.0f ? -1 : wheel.deltaY < 0.0f ? 1 : 0;
    if (listRect().contains (e.getPosition()))
        rowTop += moved;
    else if (failuresRect().contains (e.getPosition()))
        failureTop += moved;
    else
        return;
    clampScroll();
    repaint();
}

} // namespace livedaw::ui::ph
