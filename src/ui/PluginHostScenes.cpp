#include "ui/PluginHostScenes.h"

#include "engine/FxSlots.h"
#include "ui/WingPopups.h"

namespace livedaw::ui::ph
{
using namespace universal;

namespace
{
    constexpr int kArmMs = 4000;
    constexpr int kSafeKeyWidth = 120;
    constexpr int kDragPixels = 8;

    /** A key's name on the page: all capitals. */
    void style (ToggleKey& key, const juce::String& tip)
    {
        key.setTooltip (tip);
    }
} // namespace

ScenesPage::ScenesPage (UiContext& c, const universal::Theme& t)
    : ctx (c), theme (t), recallKey ("RECALL", t.accent, t), storeKey ("STORE NEW", t.accent, t), overwriteKey ("OVERWRITE", t.accent, t),
      renameKey ("RENAME", t.accent, t), deleteKey ("DELETE", t.accent, t), upKey ("MOVE UP", t.accent, t), downKey ("MOVE DOWN", t.accent, t),
      snapKey ("SNAP", t.accent, t), glideKey ("GLIDE", t.accent, t), timeDownKey ("-", t.accent, t), timeUpKey ("+", t.accent, t)
{
    recallKey.onClick = [this] { if (go) go(); };
    storeKey.onClick = [this] { storeNew(); };
    overwriteKey.onClick = [this] { pressOverwrite(); };
    renameKey.onClick = [this] { askRename(); };
    deleteKey.onClick = [this] { pressDelete(); };
    upKey.onClick = [this] { moveSelected (-1); };
    downKey.onClick = [this] { moveSelected (1); };
    snapKey.onClick = [this] { setGlide (false); };
    glideKey.onClick = [this] { setGlide (true); };
    timeDownKey.onClick = [this] { stepTime (-1); };
    timeUpKey.onClick = [this] { stepTime (1); };
    overwriteKey.isOn = [this] { return armed == Armed::Overwrite; };
    deleteKey.isOn = [this] { return armed == Armed::Delete; };
    snapKey.isOn = [this] { return cue() >= 0 && ! getSceneRow (cue()).glide; };
    glideKey.isOn = [this] { return cue() >= 0 && getSceneRow (cue()).glide; };

    style (recallKey, "Recalls the cued scene: the same as GO");
    style (storeKey, "Stores the console as a new scene at the end of the list");
    style (overwriteKey, "Stores the console into the cued scene. Press twice.");
    style (renameKey, "Names the cued scene");
    style (deleteKey, "Deletes the cued scene. Press twice.");
    style (upKey, "Moves the cued scene up the list");
    style (downKey, "Moves the cued scene down the list");
    style (snapKey, "A recall applies the plug-ins' states at once");
    style (glideKey, "A recall moves the plug-ins' continuous parameters to the scene's values over the fade time");
    style (timeDownKey, "A shorter fade time");
    style (timeUpKey, "A longer fade time");

    for (auto* k : { &recallKey, &storeKey, &overwriteKey, &renameKey, &deleteKey, &upKey, &downKey, &snapKey, &glideKey, &timeDownKey, &timeUpKey })
        addAndMakeVisible (*k);
    rebuildSafeRows();
    refresh();
    startTimerHz (4);
}

ScenesPage::~ScenesPage()
{
    stopTimer();
}

//==============================================================================
int ScenesPage::getNumScenes() const
{
    return ctx.show.getNumScenes();
}

ScenesPage::SceneRow ScenesPage::getSceneRow (int index) const
{
    SceneRow row;
    row.number = index + 1;
    if (const auto scene = ctx.show.getScene (index))
    {
        row.name = scene->name;
        row.glide = scene->glide;
        row.seconds = scene->fadeSeconds;
    }
    row.active = index == ctx.show.getCurrentSceneIndex();
    row.cued = index == cue();
    return row;
}

void ScenesPage::selectScene (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumScenes()))
        return;
    disarm();
    if (setCue)
        setCue (index);
    refresh();
}

int ScenesPage::storeNew()
{
    disarm();
    const int n = getNumScenes();
    const int index = ctx.show.addScene ("Scene " + juce::String (n + 1));
    if (index >= 0 && setCue)
        setCue (index);
    message = index >= 0 ? "Stored as scene " + juce::String (index + 1) : juce::String ("The scene could not be stored");
    refresh();
    return index;
}

void ScenesPage::pressOverwrite()
{
    const int at = cue();
    const auto scene = ctx.show.getScene (at);
    if (! scene.has_value() || scene->item != Show::ItemKind::Snapshot)
        return;
    if (armed != Armed::Overwrite)
    {
        arm (Armed::Overwrite);
        return;
    }
    disarm();
    ctx.show.storeScene (at);
    message = "Scene " + juce::String (at + 1) + " now holds the console as it is";
    refresh();
}

void ScenesPage::askRename()
{
    const int at = cue();
    const auto scene = ctx.show.getScene (at);
    if (! scene.has_value())
        return;
    disarm();
    popups::TextOptions options;
    options.maxLength = 24;
    popups::editText (*this, "RENAME SCENE " + juce::String (at + 1), scene->name,
                      [safe = juce::Component::SafePointer<ScenesPage> (this), at] (const juce::String& text)
                      {
                          if (safe != nullptr)
                              safe->rename (at, text);
                      },
                      options);
}

void ScenesPage::rename (int index, const juce::String& name)
{
    const auto trimmed = name.trim();
    if (trimmed.isEmpty() || ! juce::isPositiveAndBelow (index, getNumScenes()))
        return;
    ctx.show.setSceneName (index, trimmed);
    refresh();
}

void ScenesPage::pressDelete()
{
    const int at = cue();
    if (! juce::isPositiveAndBelow (at, getNumScenes()))
        return;
    if (armed != Armed::Delete)
    {
        arm (Armed::Delete);
        return;
    }
    disarm();
    ctx.show.removeScene (at);
    const int left = getNumScenes();
    if (setCue)
        setCue (left > 0 ? juce::jmin (at, left - 1) : -1);
    message = "Scene " + juce::String (at + 1) + " deleted";
    refresh();
}

void ScenesPage::moveSelected (int delta)
{
    const int at = cue(), to = at + delta;
    if (! juce::isPositiveAndBelow (at, getNumScenes()) || ! juce::isPositiveAndBelow (to, getNumScenes()))
        return;
    disarm();
    ctx.show.moveScene (at, to);
    if (setCue)
        setCue (to); // the cue goes with the scene
    refresh();
}

void ScenesPage::setGlide (bool glide)
{
    const int at = cue();
    if (! juce::isPositiveAndBelow (at, getNumScenes()))
        return;
    ctx.show.setSceneGlide (at, glide);
    message = glide && getSceneRow (at).seconds <= 0.0 ? "GLIDE needs a fade time above 0: until then the states snap" : juce::String();
    refresh();
}

const std::vector<double>& ScenesPage::timeSteps()
{
    static const std::vector<double> steps { 0.0, 0.5, 1.0, 2.0, 3.0, 5.0, 8.0, 10.0, 15.0, 20.0, 30.0, 45.0, 60.0 };
    return steps;
}

void ScenesPage::stepTime (int delta)
{
    const int at = cue();
    if (! juce::isPositiveAndBelow (at, getNumScenes()))
        return;
    const auto& steps = timeSteps();
    const double now = getSceneRow (at).seconds;
    int i = 0;
    while (i + 1 < (int) steps.size() && steps[(size_t) i] < now - 1.0e-6)
        ++i;
    ctx.show.setSceneFadeSeconds (at, steps[(size_t) juce::jlimit (0, (int) steps.size() - 1, i + delta)]);
    refresh();
}

juce::String ScenesPage::describeTime (double seconds)
{
    if (seconds <= 0.0)
        return "NO FADE";
    return juce::String (seconds, std::abs (seconds - std::round (seconds)) < 1.0e-6 ? 0 : 1) + " s";
}

//==============================================================================
void ScenesPage::arm (Armed what)
{
    armed = what;
    armedAt = juce::Time::getMillisecondCounter();
    overwriteKey.setButtonText (what == Armed::Overwrite ? "CONFIRM" : "OVERWRITE");
    deleteKey.setButtonText (what == Armed::Delete ? "CONFIRM" : "DELETE");
    message = what == Armed::Overwrite ? "Press OVERWRITE again to replace scene " + juce::String (cue() + 1) + " with the console as it is"
                                       : "Press DELETE again to delete scene " + juce::String (cue() + 1);
    overwriteKey.refresh();
    deleteKey.refresh();
    repaint();
}

void ScenesPage::disarm()
{
    if (armed == Armed::None)
        return;
    armed = Armed::None;
    overwriteKey.setButtonText ("OVERWRITE");
    deleteKey.setButtonText ("DELETE");
    message.clear();
    overwriteKey.refresh();
    deleteKey.refresh();
    repaint();
}

int ScenesPage::visibleSceneRows() const
{
    return juce::jmax (1, (sceneList().getHeight() - kHead) / kRow);
}

int ScenesPage::visibleSafeRows() const
{
    return juce::jmax (1, (safeList().getHeight() - kHead) / kRow);
}

juce::Rectangle<int> ScenesPage::sceneList() const
{
    return { 8, 0, kLeft, kHead + 7 * kRow };
}

juce::Rectangle<int> ScenesPage::safeList() const
{
    const int x = 8 + kLeft + kGap;
    return { x, 0, getWidth() - x - 8, getHeight() - 28 };
}

void ScenesPage::clampScroll()
{
    sceneTop = juce::jlimit (0, juce::jmax (0, getNumScenes() - visibleSceneRows()), sceneTop);
    safeTop = juce::jlimit (0, juce::jmax (0, getNumSafeRows() - visibleSafeRows()), safeTop);
}

void ScenesPage::scrollScenes (int firstRow)
{
    sceneTop = firstRow;
    clampScroll();
    repaint();
}

void ScenesPage::scrollSafes (int firstRow)
{
    safeTop = firstRow;
    clampScroll();
    repaint();
}

void ScenesPage::showCue()
{
    const int at = cue();
    if (at < 0)
        return;
    if (at < sceneTop)
        sceneTop = at;
    else if (at >= sceneTop + visibleSceneRows())
        sceneTop = at - visibleSceneRows() + 1;
    clampScroll();
}

//==============================================================================
void ScenesPage::rebuildSafeRows()
{
    auto& fx = ctx.engine.getFx();
    std::vector<SafeRow> rows;
    juce::String signature;
    const auto& channels = ctx.engine.getChannels();
    for (int ch = 0; ch < (int) channels.size(); ++ch)
    {
        auto& strip = *channels[(size_t) ch];
        rows.push_back ({ ch, -1, strip.getName() });
        signature << ch << ":" << rows.back().name << (strip.params.recallSafe.load() ? "S" : "-");
        for (const auto& step : strip.getChain())
        {
            if (! step.isBlock() || step.getBlockKind() != BlockKind::Plugin)
                continue;
            const auto info = fx.describeBlock (strip.getHandle(), step.uid);
            rows.push_back ({ ch, step.uid, info.name.isNotEmpty() ? info.name : juce::String ("EMPTY BLOCK") });
            signature << "," << step.uid << rows.back().name << (strip.isBlockSafe (step.uid) ? "S" : "-");
        }
        signature << ";";
    }
    if (signature != safeSignature)
    {
        safeSignature = signature;
        safeRows = std::move (rows);
        clampScroll();
        repaint();
    }
}

bool ScenesPage::isSafe (int row) const
{
    if (! juce::isPositiveAndBelow (row, getNumSafeRows()))
        return false;
    const auto& r = safeRows[(size_t) row];
    const auto& channels = ctx.engine.getChannels();
    if (! juce::isPositiveAndBelow (r.channel, (int) channels.size()))
        return false;
    const auto& strip = *channels[(size_t) r.channel];
    return r.isChannel() ? strip.params.recallSafe.load() : strip.isBlockSafe (r.uid);
}

void ScenesPage::toggleSafe (int row)
{
    if (! juce::isPositiveAndBelow (row, getNumSafeRows()))
        return;
    const auto r = safeRows[(size_t) row];
    const auto& channels = ctx.engine.getChannels();
    if (! juce::isPositiveAndBelow (r.channel, (int) channels.size()))
        return;
    auto& strip = *channels[(size_t) r.channel];
    if (r.isChannel())
        strip.params.recallSafe = ! strip.params.recallSafe.load();
    else
        strip.setBlockSafe (r.uid, ! strip.isBlockSafe (r.uid));
    rebuildSafeRows();
    repaint();
}

void ScenesPage::refresh()
{
    rebuildSafeRows();
    const int at = cue(), n = getNumScenes();
    const bool have = juce::isPositiveAndBelow (at, n);
    const auto scene = have ? ctx.show.getScene (at) : std::nullopt;
    recallKey.setEnabled (have);
    renameKey.setEnabled (have);
    deleteKey.setEnabled (have);
    overwriteKey.setEnabled (scene.has_value() && scene->item == Show::ItemKind::Snapshot);
    upKey.setEnabled (have && at > 0);
    downKey.setEnabled (have && at + 1 < n);
    snapKey.setEnabled (scene.has_value() && scene->item == Show::ItemKind::Snapshot);
    glideKey.setEnabled (scene.has_value() && scene->item == Show::ItemKind::Snapshot);
    timeDownKey.setEnabled (have);
    timeUpKey.setEnabled (have);
    if (armed != Armed::None && ! have)
        disarm();
    for (auto* k : { &overwriteKey, &deleteKey, &snapKey, &glideKey })
        k->refresh();
    showCue();
    shownCue = at;
    shownCurrent = ctx.show.getCurrentSceneIndex();
    shownScenes = n;
    repaint();
}

void ScenesPage::timerCallback()
{
    if (armed != Armed::None && juce::Time::getMillisecondCounter() - armedAt > (juce::uint32) kArmMs)
        disarm();
    // The surface's keys move the cue, a recall moves the current scene, and the rack changes with the show.
    if (cue() != shownCue || ctx.show.getCurrentSceneIndex() != shownCurrent || getNumScenes() != shownScenes)
        refresh();
    else
        rebuildSafeRows();
}

//==============================================================================
juce::Rectangle<int> ScenesPage::getSceneRowBounds (int index) const
{
    const auto list = sceneList();
    return { list.getX(), list.getY() + kHead + (index - sceneTop) * kRow, list.getWidth(), kRow };
}

juce::Rectangle<int> ScenesPage::safeKey (juce::Rectangle<int> row) const
{
    return row.removeFromRight (kSafeKeyWidth + 8).withTrimmedRight (8).withSizeKeepingCentre (kSafeKeyWidth, kRow - 4);
}

juce::Rectangle<int> ScenesPage::getSafeKeyBounds (int row) const
{
    const auto list = safeList();
    return safeKey ({ list.getX(), list.getY() + kHead + (row - safeTop) * kRow, list.getWidth(), kRow });
}

void ScenesPage::resized()
{
    const auto list = sceneList();
    int y = list.getBottom() + 8;
    const int w = (list.getWidth() - 3 * 8) / 4;
    int x = list.getX();
    for (auto* k : { &recallKey, &storeKey, &overwriteKey, &renameKey })
    {
        k->setBounds (x, y, w, kKey);
        x += w + 8;
    }
    y += kKey + 8;
    const int w3 = (list.getWidth() - 2 * 8) / 3;
    x = list.getX();
    for (auto* k : { &deleteKey, &upKey, &downKey })
    {
        k->setBounds (x, y, w3, kKey);
        x += w3 + 8;
    }
    // The fade: its mode, then its time.
    y += kKey + 8;
    x = list.getX() + 96;
    snapKey.setBounds (x, y, 120, kKey);
    glideKey.setBounds (x + 128, y, 120, kKey);
    timeDownKey.setBounds (x + 336, y, 72, kKey);
    timeUpKey.setBounds (x + 336 + 72 + 160, y, 72, kKey);
    clampScroll();
}

void ScenesPage::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    const auto labelFont = type::font (type::kLabel, true);

    // The scenes.
    {
        const auto list = sceneList();
        auto head = juce::Rectangle<int> (list.getX(), list.getY(), list.getWidth(), kHead);
        g.setColour (theme.textDim);
        g.setFont (labelFont);
        g.drawText ("SCENES", head.withTrimmedLeft (12), juce::Justification::centredLeft);
        g.setFont (type::font (type::kCaption, true));
        g.drawText ("NAME", head.withTrimmedLeft (82).withWidth (200), juce::Justification::centredLeft);
        g.drawText ("FADE", head.withTrimmedLeft (480).withWidth (200), juce::Justification::centredLeft);

        const int n = getNumScenes();
        g.saveState();
        g.reduceClipRegion (list.withTrimmedTop (kHead));
        if (n == 0)
        {
            g.setColour (theme.textDim);
            g.setFont (type::font (type::kValue));
            g.drawFittedText ("No scenes yet. STORE NEW keeps the console as it is: every rack, its plug-ins' states, bypass, keys and the patch.",
                              list.withTrimmedTop (kHead).reduced (40, 20).withHeight (90), juce::Justification::centred, 3);
        }
        for (int i = sceneTop; i < n && i < sceneTop + visibleSceneRows() + 1; ++i)
        {
            const auto row = getSceneRow (i);
            const auto b = getSceneRowBounds (i).reduced (0, 2);
            g.setColour (row.cued ? theme.raised.brighter (0.08f) : theme.panel);
            g.fillRoundedRectangle (b.toFloat(), 6.0f);
            if (row.cued)
            {
                g.setColour (theme.accent);
                g.drawRoundedRectangle (b.toFloat().reduced (1.0f), 6.0f, 2.0f);
            }
            g.setColour (row.cued ? theme.accent : theme.textDim);
            g.setFont (type::font (type::kHeading, true));
            g.drawText (juce::String (row.number).paddedLeft ('0', 2), b.withTrimmedLeft (12).withWidth (56), juce::Justification::centredLeft);
            g.setColour (theme.text);
            g.drawText (row.name, b.withTrimmedLeft (82).withWidth (390), juce::Justification::centredLeft);
            g.setFont (type::font (type::kLabel, true));
            g.setColour (row.glide ? theme.accent : theme.textDim);
            g.drawText (row.glide ? "GLIDE" : "SNAP", b.withTrimmedLeft (480).withWidth (90), juce::Justification::centredLeft);
            g.setColour (row.seconds > 0.0 ? theme.text : theme.textDim);
            g.drawText (describeTime (row.seconds), b.withTrimmedLeft (580).withWidth (110), juce::Justification::centredLeft);

            auto tags = b.withTrimmedLeft (700).withTrimmedRight (10).reduced (0, 9);
            if (row.active)
            {
                const auto tag = tags.removeFromLeft (92);
                g.setColour (theme.meterLow);
                g.fillRoundedRectangle (tag.toFloat(), 5.0f);
                g.setColour (juce::Colours::black.withAlpha (0.8f));
                g.setFont (type::font (type::kCaption, true));
                g.drawText ("ACTIVE", tag, juce::Justification::centred);
                tags.removeFromLeft (8);
            }
            if (row.cued)
            {
                const auto tag = tags.removeFromLeft (76);
                g.setColour (theme.accent);
                g.drawRoundedRectangle (tag.toFloat(), 5.0f, 1.5f);
                g.setFont (type::font (type::kCaption, true));
                g.drawText ("CUED", tag, juce::Justification::centred);
            }
        }
        g.restoreState();

        // The fade of the cued scene, beside the keys.
        const int at = cue();
        const int y = snapKey.getY();
        g.setColour (theme.textDim);
        g.setFont (labelFont);
        g.drawText ("FADE", juce::Rectangle<int> (list.getX() + 12, y, 80, kKey), juce::Justification::centredLeft);
        g.setColour (juce::isPositiveAndBelow (at, n) && getSceneRow (at).seconds > 0.0 ? theme.text : theme.textDim);
        g.setFont (type::font (type::kHeading, true));
        g.drawText (juce::isPositiveAndBelow (at, n) ? describeTime (getSceneRow (at).seconds) : juce::String ("--"),
                    juce::Rectangle<int> (timeDownKey.getRight(), y, timeUpKey.getX() - timeDownKey.getRight(), kKey), juce::Justification::centred);

        g.setColour (armed != Armed::None ? theme.accent : theme.textDim);
        g.setFont (type::font (type::kCaption));
        g.drawFittedText (message, juce::Rectangle<int> (list.getX(), getHeight() - 36, list.getWidth(), 34), juce::Justification::centredLeft, 2);
    }

    // The safes.
    {
        const auto list = safeList();
        g.setColour (theme.textDim);
        g.setFont (labelFont);
        g.drawText ("SAFES", juce::Rectangle<int> (list.getX() + 12, 0, 120, kHead), juce::Justification::centredLeft);
        g.setFont (type::font (type::kCaption));
        g.drawFittedText ("A safe channel, or plug-in, keeps its state and its place through a recall.",
                          juce::Rectangle<int> (list.getX() + 130, 0, list.getWidth() - 130, kHead), juce::Justification::centredLeft, 2);

        g.saveState();
        g.reduceClipRegion (list.withTrimmedTop (kHead));
        for (int i = safeTop; i < getNumSafeRows() && i < safeTop + visibleSafeRows() + 1; ++i)
        {
            const auto& r = safeRows[(size_t) i];
            const auto b = juce::Rectangle<int> (list.getX(), list.getY() + kHead + (i - safeTop) * kRow, list.getWidth(), kRow).reduced (0, 2);
            const bool safe = isSafe (i);
            g.setColour (r.isChannel() ? theme.raised : theme.panel);
            g.fillRoundedRectangle (b.toFloat(), 6.0f);
            g.setFont (type::font (type::kValue, r.isChannel()));
            if (r.isChannel())
            {
                g.setColour (theme.accent);
                g.drawText ("CH " + juce::String (r.channel + 1), b.withTrimmedLeft (12).withWidth (70), juce::Justification::centredLeft);
                g.setColour (theme.text);
                g.drawText (r.name, b.withTrimmedLeft (86).withTrimmedRight (kSafeKeyWidth + 20), juce::Justification::centredLeft);
            }
            else
            {
                g.setColour (theme.outline);
                g.drawLine ((float) b.getX() + 34.0f, (float) b.getY(), (float) b.getX() + 34.0f, (float) b.getCentreY(), 1.5f);
                g.drawLine ((float) b.getX() + 34.0f, (float) b.getCentreY(), (float) b.getX() + 50.0f, (float) b.getCentreY(), 1.5f);
                g.setColour (theme.text);
                g.drawText (r.name, b.withTrimmedLeft (58).withTrimmedRight (kSafeKeyWidth + 20), juce::Justification::centredLeft);
            }

            const auto key = safeKey (b.expanded (0, 2)).toFloat();
            const bool covered = ! r.isChannel() && ctx.engine.getChannels()[(size_t) r.channel]->params.recallSafe.load();
            g.setColour (safe ? theme.accent : theme.well);
            g.fillRoundedRectangle (key, 6.0f);
            g.setColour (safe ? theme.accent : theme.outline);
            g.drawRoundedRectangle (key, 6.0f, 1.5f);
            g.setColour (safe ? theme.textOnAccent : (covered ? theme.text : theme.textDim));
            g.setFont (type::font (type::kLabel, true));
            g.drawText (safe ? "SAFE" : (covered ? "(SAFE)" : "SAFE"), key, juce::Justification::centred);
        }
        if (getNumSafeRows() == 0)
        {
            g.setColour (theme.textDim);
            g.setFont (type::font (type::kValue));
            g.drawFittedText ("No channels.", list.withTrimmedTop (kHead), juce::Justification::centred, 1);
        }
        g.restoreState();
    }
}

//==============================================================================
void ScenesPage::mouseDown (const juce::MouseEvent& e)
{
    dragged = false;
    dragFrom = e.y;
    drag = sceneList().withTrimmedTop (kHead).contains (e.getPosition()) ? Drag::Scenes
           : safeList().withTrimmedTop (kHead).contains (e.getPosition()) ? Drag::Safes
                                                                          : Drag::None;
}

void ScenesPage::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::None || (! dragged && std::abs (e.y - dragFrom) < kDragPixels))
        return;
    dragged = true;
    const int rows = (dragFrom - e.y) / kRow; // a drag up shows later rows
    if (rows == 0)
        return;
    dragFrom = e.y;
    if (drag == Drag::Scenes)
        scrollScenes (sceneTop + rows);
    else
        scrollSafes (safeTop + rows);
}

void ScenesPage::mouseUp (const juce::MouseEvent& e)
{
    const bool wasDrag = dragged;
    dragged = false;
    if (wasDrag || e.mouseWasDraggedSinceMouseDown())
        return;
    const auto at = e.getPosition();
    if (drag == Drag::Scenes)
    {
        const int index = sceneTop + (at.y - kHead) / kRow;
        if (juce::isPositiveAndBelow (index, getNumScenes()))
            selectScene (index);
    }
    else if (drag == Drag::Safes)
    {
        const int row = safeTop + (at.y - kHead) / kRow;
        if (juce::isPositiveAndBelow (row, getNumSafeRows()) && getSafeKeyBounds (row).expanded (6, 0).contains (at))
            toggleSafe (row);
    }
    drag = Drag::None;
}

void ScenesPage::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int rows = wheel.deltaY > 0.0f ? -1 : wheel.deltaY < 0.0f ? 1 : 0;
    if (sceneList().contains (e.getPosition()))
        scrollScenes (sceneTop + rows);
    else if (safeList().contains (e.getPosition()))
        scrollSafes (safeTop + rows);
}

} // namespace livedaw::ui::ph
