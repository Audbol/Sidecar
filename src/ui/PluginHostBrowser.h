#pragma once

#include "ui/PluginHostSystem.h"
#include "ui/UiContext.h"
#include "ui/universal/Widgets.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace livedaw::ui::ph
{

/** What the BROWSER remembers about plug-ins, in the app settings (not in the show: it is the user's, not the show's).
    A plug-in is known by its identifier string (format, name, unique id), so it is found again after a rescan.

    - Favourites: a plug-in with a star.
    - Tags: the user's own words for a plug-in ("drums", "reverb"), kept as typed, at most 8 of up to 20 characters.
    - Recent: the last 10 plug-ins added to a rack, newest first; adding one again moves it to the front. */
class Library
{
public:
    static constexpr const char* favouritesKey = "pluginhost.browser.favourites";
    static constexpr const char* tagsKey = "pluginhost.browser.tags";
    static constexpr const char* recentKey = "pluginhost.browser.recent";
    static constexpr int kMaxRecent = 10, kMaxTags = 8, kMaxTagLength = 20;

    explicit Library (juce::PropertiesFile&);

    static juce::String idOf (const juce::PluginDescription& d) { return d.createIdentifierString(); }

    bool isFavourite (const juce::String& id) const;
    void setFavourite (const juce::String& id, bool favourite);
    juce::StringArray getTags (const juce::String& id) const;
    void setTags (const juce::String& id, const juce::StringArray& tags);
    /** "drums, Reverb ,, drums" as tags: trimmed, no empty ones, no repeats (case aside), no commas. */
    static juce::StringArray parseTags (const juce::String& text);
    /** The identifiers of the plug-ins added last, newest first. */
    juce::StringArray getRecent() const;
    void addRecent (const juce::String& id);

private:
    juce::PropertiesFile& settings;
};

/** The BROWSER page of the PLUGIN HOST's screen (docs/PLUGIN_HOST.md, page 6).

    The list (left) shows every scanned plug-in: a star (favourite), its name, maker and format, its tags and a TAGS key
    that opens the on-screen keyboard. A tap on the rest of a row adds the plug-in to the selected channel's rack (the
    CPU guard warns first when the engine's CPU is high, and never stops it). SEARCH (the on-screen keyboard) matches
    every word against the name, maker, format and tags; ALL, FAVOURITES and RECENT choose what the list holds.
    The SCAN REPORT (right): SCAN opens the plug-in scanner, RESCAN forgets the plug-ins that failed so that they are
    tried again and then opens it; below, each plug-in that failed and why. The scanner keeps JUCE's blacklist, which is
    saved, and a reason for the failures of this run; an older failure says no reason was kept.

    Rows are 52 px, keys 52 px (touch sized in the 1920 x 1080 design space). Message thread only. */
class BrowserPage final : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    BrowserPage (UiContext&, const universal::Theme&, Library&, Guard&);
    ~BrowserPage() override;

    /** The surface adds the plug-in to the selected channel's rack (false: it could not be loaded). */
    std::function<bool (const juce::PluginDescription&)> onAdd;
    /** The channel the rack shows (0-based), for the messages. */
    std::function<int()> getChannel;

    enum class Filter
    {
        All,
        Favourites,
        Recent
    };
    struct Row
    {
        juce::PluginDescription description;
        juce::String id;
        bool favourite = false;
        juce::StringArray tags;
    };

    /** Reads the scanned plug-ins, the search and the filter again (the timer and the scanner's changes do this too). */
    void refresh();
    int getNumRows() const noexcept { return (int) rows.size(); }
    const Row& getRow (int row) const { return rows[(size_t) row]; }
    /** How many plug-ins have been scanned, whatever the search and the filter. */
    int getNumPlugins() const noexcept { return numPlugins; }

    void setSearch (const juce::String&);
    const juce::String& getSearch() const noexcept { return search; }
    /** Opens the on-screen keyboard for the search. */
    void askSearch();
    void setFilter (Filter);
    Filter getFilter() const noexcept { return filter; }
    universal::ToggleKey& getFilterKey (Filter f) noexcept { return f == Filter::All ? allKey : f == Filter::Favourites ? favouritesKey : recentKey; }
    universal::ToggleKey& getSearchKey() noexcept { return searchKey; }
    universal::ToggleKey& getClearKey() noexcept { return clearKey; }

    void toggleFavourite (int row);
    /** Opens the on-screen keyboard for the row's tags. */
    void askTags (int row);
    void setTags (int row, const juce::String& text);
    /** A tap on the row: adds the plug-in to the selected channel's rack. */
    bool addRow (int row);
    /** What the page last did, or the guard's warning ("ADDED ..."). */
    juce::String getMessage() const;

    // The scan report.
    struct Failure
    {
        juce::String file, name, reason;
    };
    const std::vector<Failure>& getFailures() const noexcept { return failures; }
    void scan();
    void rescan();
    universal::ToggleKey& getScanKey() noexcept { return scanKey; }
    universal::ToggleKey& getRescanKey() noexcept { return rescanKey; }

    /** The rows' and zones' places in this component (tests, screenshots). */
    juce::Rectangle<int> getRowBounds (int row) const;
    juce::Rectangle<int> getStarBounds (int row) const;
    juce::Rectangle<int> getTagsBounds (int row) const;
    void scrollRows (int firstRow);
    int getFirstRow() const noexcept { return rowTop; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int kRow = 52, kKey = 52, kListWidth = 1280, kReportX = 1304, kTop = 92, kMessageHeight = 56, kStarWidth = 64, kTagsWidth = 132;

private:
    void timerCallback() override { refresh(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    juce::Rectangle<int> listRect() const;
    juce::Rectangle<int> reportRect() const;
    juce::Rectangle<int> failuresRect() const;
    int visibleRows() const;
    int visibleFailures() const;
    void clampScroll();

    UiContext& ctx;
    const universal::Theme& theme;
    Library& library;
    Guard& guard;
    universal::ToggleKey searchKey, clearKey, allKey, favouritesKey, recentKey, scanKey, rescanKey;
    std::vector<Row> rows;
    std::vector<Failure> failures;
    juce::String search, signature, note;
    Filter filter = Filter::All;
    int numPlugins = 0, rowTop = 0, failureTop = 0;

    enum class Area
    {
        None,
        List,
        Failures
    };
    Area drag = Area::None;
    int dragFrom = 0;
    bool dragged = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserPage)
};

} // namespace livedaw::ui::ph
