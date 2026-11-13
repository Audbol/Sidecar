#include "ui/PluginHostControlPage.h"

namespace livedaw::ui::ph
{
using namespace universal;

namespace
{
    constexpr int kArmMs = 4000;
    constexpr int kDragPixels = 8;
    constexpr int kMessageHeight = 52;
    constexpr int kNameWidth = 236, kHotKeyWidth = 214;
    constexpr int kPickNone = 1, kPickBase = 100, kGroup = 32;
} // namespace

ControlPage::ControlPage (UiContext& c, const Theme& t, Control& ctl)
    : ctx (c), theme (t), control (ctl), learnKey ("LEARN", t.accent, t), modeKey ("MODE", t.accent, t), deleteKey ("DELETE", t.accent, t),
      clearKey ("CLEAR ALL", t.accent, t)
{
    learnKey.isOn = [this] { return control.isLearning(); };
    learnKey.onClick = [this]
    {
        disarm();
        control.setLearning (! control.isLearning());
        refresh();
    };
    modeKey.onClick = [this] { pressMode(); };
    deleteKey.onClick = [this] { pressDelete(); };
    clearKey.onClick = [this] { pressClearAll(); };
    clearKey.isOn = [this] { return clearArmed; };
    learnKey.setTooltip ("MIDI learn: tap or move a plug-in parameter, a hot knob, a SELECT or BYPASS key or a scene key, then move a MIDI control");
    modeKey.setTooltip ("The selected controller: absolute, or relative (an endless encoder)");
    deleteKey.setTooltip ("Deletes the selected mapping");
    clearKey.setTooltip ("Deletes every mapping. Press twice.");
    for (auto* k : { &learnKey, &modeKey, &deleteKey, &clearKey })
        addAndMakeVisible (*k);
    refresh();
    startTimerHz (4);
}

ControlPage::~ControlPage()
{
    stopTimer();
}

//==============================================================================
void ControlPage::setChannel (int index)
{
    channel = index;
    refresh();
}

bool ControlPage::isDeviceOn (int row) const
{
    return juce::isPositiveAndBelow (row, getNumDevices()) && control.isInputEnabled (devices[row]);
}

void ControlPage::toggleDevice (int row)
{
    if (! juce::isPositiveAndBelow (row, getNumDevices()))
        return;
    control.setInputEnabled (devices[row], ! control.isInputEnabled (devices[row]));
    refresh();
}

void ControlPage::selectMapping (int row)
{
    disarm();
    selectedMapping = juce::isPositiveAndBelow (row, getNumMappings()) ? row : -1;
    refresh();
    repaint();
}

void ControlPage::pressDelete()
{
    disarm();
    if (control.deleteMapping (selectedMapping))
        selectedMapping = juce::jmin (selectedMapping, getNumMappings() - 2);
    refresh();
}

void ControlPage::pressMode()
{
    disarm();
    if (juce::isPositiveAndBelow (selectedMapping, getNumMappings()))
        control.setMappingMode (selectedMapping, mappings[(size_t) selectedMapping].source.mode == MidiSource::Mode::Absolute ? MidiSource::Mode::Relative
                                                                                                                         : MidiSource::Mode::Absolute);
    refresh();
}

void ControlPage::pressClearAll()
{
    if (getNumMappings() == 0)
        return;
    if (! clearArmed)
    {
        clearArmed = true;
        armedAt = juce::Time::getMillisecondCounter();
        clearKey.setButtonText ("CONFIRM");
        clearKey.refresh();
        repaint();
        return;
    }
    disarm();
    control.clearAll();
    selectedMapping = -1;
    refresh();
}

void ControlPage::disarm()
{
    if (! clearArmed)
        return;
    clearArmed = false;
    clearKey.setButtonText ("CLEAR ALL");
    clearKey.refresh();
    repaint();
}

//==============================================================================
void ControlPage::pickHot (int row, int knob)
{
    if (! juce::isPositiveAndBelow (row, getNumHotRows()))
        return;
    const auto& r = hotRows[(size_t) row];
    const auto names = control.parameterNames (r.ref);
    juce::PopupMenu menu;
    menu.addItem (kPickNone, "NONE", ! control.isLearning());
    menu.addSeparator();
    if (names.isEmpty())
        menu.addItem (0, r.sandboxed ? "THIS PLUG-IN OFFERS NO PARAMETERS (SANDBOX)" : "THIS PLUG-IN OFFERS NO PARAMETERS", false);
    if (names.size() <= kGroup + 8)
        for (int i = 0; i < names.size(); ++i)
            menu.addItem (kPickBase + i, names[i]);
    else
        for (int first = 0; first < names.size(); first += kGroup)
        {
            juce::PopupMenu sub;
            const int last = juce::jmin (names.size(), first + kGroup);
            for (int i = first; i < last; ++i)
                sub.addItem (kPickBase + i, names[i]);
            menu.addSubMenu (juce::String (first + 1) + " - " + juce::String (last), sub);
        }
    menu.setLookAndFeel (&lookAndFeelFor (theme));
    juce::Component::SafePointer<ControlPage> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (getHotKeyBounds (row, knob))).withStandardItemHeight (sizes::kTouch),
                        [safe, row, knob] (int result)
                        {
                            if (safe != nullptr && result >= kPickNone)
                                safe->chooseHot (row, knob, result == kPickNone ? -1 : result - kPickBase);
                        });
}

void ControlPage::chooseHot (int row, int knob, int paramIndex)
{
    if (! juce::isPositiveAndBelow (row, getNumHotRows()))
        return;
    const auto ref = hotRows[(size_t) row].ref;
    if (paramIndex < 0)
        control.clearHot (ref, knob);
    else if (auto* plugin = control.pluginOf (ref))
    {
        const auto param = ParamRef::of (*plugin, paramIndex);
        if (control.isLearning())
            control.captureTarget (Target::ofParam (ref, param));
        else
            control.assignHot (ref, knob, param);
    }
    refresh();
}

//==============================================================================
juce::Rectangle<int> ControlPage::devicesRect() const
{
    return { 8, 0, kDevicesWidth, getHeight() };
}

juce::Rectangle<int> ControlPage::mappingsRect() const
{
    return { kMapX, 0, kMapWidth, getHeight() };
}

juce::Rectangle<int> ControlPage::hotRect() const
{
    return { kHotX, 0, getWidth() - kHotX - 8, getHeight() };
}

juce::Rectangle<int> ControlPage::listOf (Panel p) const
{
    switch (p)
    {
        case Panel::Devices:  return devicesRect().withTrimmedTop (kHead).withTrimmedBottom (8);
        case Panel::Mappings: return mappingsRect().withTrimmedTop (kHead).withTrimmedBottom (kMessageHeight + kKey + 24);
        case Panel::Hot:      return hotRect().withTrimmedTop (kHead).withTrimmedBottom (8);
        case Panel::None:     break;
    }
    return {};
}

int ControlPage::visibleRows (Panel p) const
{
    return juce::jmax (1, listOf (p).getHeight() / kRow);
}

int& ControlPage::topOf (Panel p)
{
    return p == Panel::Devices ? deviceTop : p == Panel::Mappings ? mapTop : hotTop;
}

int ControlPage::numRows (Panel p) const
{
    return p == Panel::Devices ? getNumDevices() : p == Panel::Mappings ? getNumMappings() : getNumHotRows();
}

void ControlPage::clampScroll()
{
    for (auto p : { Panel::Devices, Panel::Mappings, Panel::Hot })
        topOf (p) = juce::jlimit (0, juce::jmax (0, numRows (p) - visibleRows (p)), topOf (p));
}

void ControlPage::scrollMappings (int firstRow)
{
    mapTop = firstRow;
    clampScroll();
    repaint();
}

juce::Rectangle<int> ControlPage::getDeviceRowBounds (int row) const
{
    const auto list = listOf (Panel::Devices);
    return { list.getX(), list.getY() + (row - deviceTop) * kRow, list.getWidth(), kRow };
}

juce::Rectangle<int> ControlPage::getMappingRowBounds (int row) const
{
    const auto list = listOf (Panel::Mappings);
    return { list.getX(), list.getY() + (row - mapTop) * kRow, list.getWidth(), kRow };
}

juce::Rectangle<int> ControlPage::getHotKeyBounds (int row, int knob) const
{
    const auto list = listOf (Panel::Hot);
    const int x = list.getX() + kNameWidth + knob * (kHotKeyWidth + 6);
    return { x, list.getY() + (row - hotTop) * kRow + 2, kHotKeyWidth, kRow - 4 };
}

void ControlPage::resized()
{
    const auto m = mappingsRect();
    const int w = (m.getWidth() - 3 * 8) / 4;
    int x = m.getX();
    const int y = m.getBottom() - kKey - 4;
    for (auto* k : { &learnKey, &modeKey, &deleteKey, &clearKey })
    {
        k->setBounds (x, y, w, kKey);
        x += w + 8;
    }
    clampScroll();
}

//==============================================================================
juce::String ControlPage::messageText() const
{
    if (control.isLearning())
        return control.describeLearn();
    if (clearArmed)
        return "Press CLEAR ALL again to delete all " + juce::String (getNumMappings()) + " mappings";
    if (control.getNote().isNotEmpty())
        return control.getNote();
    if (getNumMappings() == 0)
        return "No mappings yet. LEARN, tap a parameter on the RACK page, then move a MIDI control.";
    return {};
}

void ControlPage::refresh()
{
    devices = Control::availableInputs();
    for (auto& name : control.getEnabledInputs())
        devices.addIfNotAlreadyThere (name); // switched on but not there: it stays listed
    mappings = control.getMappings();
    if (selectedMapping >= getNumMappings())
        selectedMapping = getNumMappings() - 1;

    hotRows.clear();
    for (const auto& ref : control.pluginsOf (channel))
    {
        HotRow r;
        r.ref = ref;
        r.name = control.nameOf (ref);
        r.sandboxed = control.isSandboxed (ref);
        const auto names = control.parameterNames (ref);
        r.numParameters = names.size();
        if (auto* plugin = control.pluginOf (ref))
        {
            const auto c = control.controlOf (ref);
            for (size_t k = 0; k < 2; ++k)
                if (auto* p = c.hot[k].find (*plugin))
                    r.hot[k] = p->getName (64);
        }
        hotRows.push_back (std::move (r));
    }
    clampScroll();

    if (clearArmed && juce::Time::getMillisecondCounter() - armedAt > (juce::uint32) kArmMs)
        disarm();
    if (getNumMappings() == 0)
        disarm();

    const bool relative = juce::isPositiveAndBelow (selectedMapping, getNumMappings())
                          && mappings[(size_t) selectedMapping].source.mode == MidiSource::Mode::Relative;
    modeKey.setButtonText (juce::isPositiveAndBelow (selectedMapping, getNumMappings()) && mappings[(size_t) selectedMapping].source.kind == MidiSource::Kind::Cc
                               ? (relative ? "RELATIVE" : "ABSOLUTE")
                               : "MODE");
    modeKey.isOn = [relative] { return relative; };
    for (auto* k : { &learnKey, &modeKey, &deleteKey, &clearKey })
        k->refresh();

    juce::String now = messageText() + "|" + juce::String (selectedMapping) + "|" + juce::String (channel) + "|" + juce::String (deviceTop) + juce::String (mapTop) + juce::String (hotTop);
    for (auto& d : devices)
        now << "|" << d << (control.isInputEnabled (d) ? "+" : "-") << (control.isInputOpen (d) ? "o" : "x");
    for (auto& m : mappings)
        now << "|" << m.sourceText << ">" << m.targetText;
    for (auto& r : hotRows)
        now << "|" << r.name << r.hot[0] << "," << r.hot[1];
    if (now != signature)
    {
        signature = now;
        repaint();
    }
}

void ControlPage::timerCallback()
{
    refresh();
}

//==============================================================================
void ControlPage::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);
    const auto labelFont = type::font (type::kLabel, true);

    const auto panelTitle = [&] (juce::Rectangle<int> panel, const juce::String& title, const juce::String& hint)
    {
        const auto head = panel.withHeight (kHead);
        g.setColour (theme.textDim);
        g.setFont (labelFont);
        g.drawText (title, head.withTrimmedLeft (12), juce::Justification::centredLeft);
        g.setFont (type::font (type::kCaption));
        g.drawFittedText (hint, head.withTrimmedLeft (150), juce::Justification::centredRight, 1);
    };
    const auto row = [&] (juce::Rectangle<int> b, bool selected)
    {
        b = b.reduced (0, 2);
        g.setColour (selected ? theme.raised.brighter (0.08f) : theme.panel);
        g.fillRoundedRectangle (b.toFloat(), 6.0f);
        if (selected)
        {
            g.setColour (theme.accent);
            g.drawRoundedRectangle (b.toFloat().reduced (1.0f), 6.0f, 2.0f);
        }
        return b;
    };
    const auto empty = [&] (juce::Rectangle<int> list, const juce::String& text)
    {
        g.setColour (theme.textDim);
        g.setFont (type::font (type::kValue));
        g.drawFittedText (text, list.reduced (24, 20).withHeight (90), juce::Justification::centredTop, 3);
    };

    // MIDI DEVICES.
    {
        panelTitle (devicesRect(), "MIDI DEVICES", "tap to listen to an input");
        const auto list = listOf (Panel::Devices);
        g.saveState();
        g.reduceClipRegion (list);
        if (devices.isEmpty())
            empty (list, "No MIDI inputs were found. Plug a controller in: it appears here.");
        for (int i = deviceTop; i < getNumDevices() && i < deviceTop + visibleRows (Panel::Devices) + 1; ++i)
        {
            const bool on = isDeviceOn (i), open = control.isInputOpen (devices[i]);
            const auto b = row (getDeviceRowBounds (i), on);
            g.setColour (on ? theme.accent : theme.outline);
            g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ (float) b.getX() + 26.0f, (float) b.getCentreY() }));
            g.setColour (theme.text);
            g.setFont (type::font (type::kValue, on));
            g.drawFittedText (devices[i], b.withTrimmedLeft (50).withTrimmedRight (110), juce::Justification::centredLeft, 1, 0.8f);
            g.setFont (type::font (type::kCaption, true));
            g.setColour (on && ! open ? theme.mute : on ? theme.meterLow : theme.textDim);
            g.drawText (on ? (open ? "LISTENING" : "NOT FOUND") : "OFF", b.withTrimmedRight (10), juce::Justification::centredRight);
        }
        g.restoreState();
    }

    // MAPPINGS.
    {
        panelTitle (mappingsRect(), "MAPPINGS", juce::String (getNumMappings()) + (getNumMappings() == 1 ? " mapping" : " mappings"));
        const auto list = listOf (Panel::Mappings);
        g.saveState();
        g.reduceClipRegion (list);
        for (int i = mapTop; i < getNumMappings() && i < mapTop + visibleRows (Panel::Mappings) + 1; ++i)
        {
            const auto b = row (getMappingRowBounds (i), i == selectedMapping);
            g.setColour (theme.accent);
            g.setFont (type::font (type::kLabel, true));
            g.drawFittedText (mappings[(size_t) i].sourceText, b.withTrimmedLeft (12).withWidth (190), juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (theme.textDim);
            g.drawText ("->", b.withTrimmedLeft (206).withWidth (30), juce::Justification::centred);
            g.setColour (theme.text);
            g.setFont (type::font (type::kValue));
            g.drawFittedText (mappings[(size_t) i].targetText, b.withTrimmedLeft (242).withTrimmedRight (8), juce::Justification::centredLeft, 1, 0.8f);
        }
        g.restoreState();
        g.setColour (control.isLearning() ? theme.accent : theme.textDim);
        g.setFont (type::font (type::kCaption, control.isLearning()));
        g.drawFittedText (messageText(), juce::Rectangle<int> (list.getX() + 4, list.getBottom() + 6, list.getWidth() - 8, kMessageHeight), juce::Justification::centredLeft, 2);
    }

    // HOT KNOBS.
    {
        panelTitle (hotRect(), "HOT KNOBS", "CH " + juce::String (channel + 1) + "  -  tap a parameter to change it");
        const auto list = listOf (Panel::Hot);
        g.saveState();
        g.reduceClipRegion (list);
        if (hotRows.empty())
            empty (list, "No plug-ins on this channel. Add one on the RACK page, then give its hot knobs a parameter each here, or with ASSIGN HOT on the RACK page.");
        for (int i = hotTop; i < getNumHotRows() && i < hotTop + visibleRows (Panel::Hot) + 1; ++i)
        {
            const auto& r = hotRows[(size_t) i];
            const auto b = getHotKeyBounds (i, 0).withX (list.getX()).withWidth (kNameWidth - 6).withHeight (kRow - 4);
            g.setColour (theme.text);
            g.setFont (type::font (type::kValue, true));
            g.drawFittedText (juce::String (r.ref.member + 1) + "  " + r.name + (r.sandboxed ? "  (SANDBOX)" : ""), b.withTrimmedLeft (8), juce::Justification::centredLeft, 1, 0.8f);
            for (int k = 0; k < 2; ++k)
            {
                const auto key = getHotKeyBounds (i, k).toFloat();
                const bool assigned = r.hot[(size_t) k].isNotEmpty();
                g.setColour (assigned ? theme.raised.brighter (0.08f) : theme.well);
                g.fillRoundedRectangle (key, 6.0f);
                g.setColour (assigned ? theme.accent : theme.outline);
                g.drawRoundedRectangle (key.reduced (0.5f), 6.0f, 1.5f);
                g.setColour (assigned ? theme.text : theme.textDim);
                g.setFont (type::font (type::kLabel, true));
                g.drawFittedText ("HOT " + juce::String (k + 1) + "  " + (assigned ? r.hot[(size_t) k] : juce::String ("-")), key.toNearestInt().reduced (8, 0),
                                  juce::Justification::centredLeft, 1, 0.8f);
            }
        }
        g.restoreState();
    }
}

//==============================================================================
void ControlPage::mouseDown (const juce::MouseEvent& e)
{
    dragged = false;
    dragFrom = e.y;
    drag = Panel::None;
    for (auto p : { Panel::Devices, Panel::Mappings, Panel::Hot })
        if (listOf (p).contains (e.getPosition()))
            drag = p;
}

void ControlPage::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Panel::None || (! dragged && std::abs (e.y - dragFrom) < kDragPixels))
        return;
    dragged = true;
    const int rows = (dragFrom - e.y) / kRow;
    if (rows == 0)
        return;
    dragFrom = e.y;
    topOf (drag) += rows;
    clampScroll();
    repaint();
}

void ControlPage::mouseUp (const juce::MouseEvent& e)
{
    const bool wasDrag = dragged;
    dragged = false;
    const auto panel = drag;
    drag = Panel::None;
    if (wasDrag || e.mouseWasDraggedSinceMouseDown())
        return;
    const auto at = e.getPosition();
    const auto index = [&] (Panel p) { return topOf (p) + (at.y - listOf (p).getY()) / kRow; };
    if (panel == Panel::Devices)
        toggleDevice (index (panel));
    else if (panel == Panel::Mappings)
        selectMapping (index (panel));
    else if (panel == Panel::Hot)
    {
        const int r = index (panel);
        if (juce::isPositiveAndBelow (r, getNumHotRows()))
            for (int k = 0; k < 2; ++k)
                if (getHotKeyBounds (r, k).expanded (3, 0).contains (at))
                    pickHot (r, k);
    }
}

void ControlPage::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int rows = wheel.deltaY > 0.0f ? -1 : wheel.deltaY < 0.0f ? 1 : 0;
    for (auto p : { Panel::Devices, Panel::Mappings, Panel::Hot })
        if (listOf (p).contains (e.getPosition()))
        {
            topOf (p) += rows;
            clampScroll();
            repaint();
        }
}

} // namespace livedaw::ui::ph
