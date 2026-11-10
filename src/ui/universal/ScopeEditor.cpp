#include "ui/universal/ScopeEditor.h"

#include "ui/universal/Groups.h"

#include <algorithm>

namespace livedaw::ui::universal
{

namespace
{
    constexpr int kHeader = 30, kKey = 44, kKeyMin = 58, kGroupGap = 6;

    /** The groups of keys after the strips' (one per strip kind). */
    constexpr int kDcas = kNumStripKinds, kMuteGroups = kDcas + 1, kFx = kDcas + 2, kConsole = kDcas + 3, kNumGroups = kDcas + 4;

    const char* kindTitle (int kind)
    {
        switch (kind)
        {
            case (int) StripKind::Channel: return "CHANNELS";
            case (int) StripKind::Aux:     return "AUX";
            case (int) StripKind::Bus:     return "BUSES";
            case (int) StripKind::Main:    return "MAINS";
            case (int) StripKind::Matrix:  return "MATRICES";
            case kDcas:                    return "DCAS";
            case kMuteGroups:              return "MUTE GROUPS";
            case kFx:                      return "FX SLOTS";
            default:                       break;
        }
        return "CONSOLE";
    }

    const std::pair<juce::uint32, const char*> kSections[] = { { state::Name, "NAME" },       { state::Routing, "ROUTING" }, { state::Preamp, "INPUT" },
                                                               { state::Gate, "GATE" },       { state::Eq, "EQ" },           { state::Dynamics, "DYNAMICS" },
                                                               { state::Inserts, "INSERTS" }, { state::Sends, "SENDS" },     { state::Fader, "FADER" },
                                                               { state::Groups, "GROUPS" } };

    int totalStrips (MixerEngine& engine)
    {
        int n = 0;
        for (int k = 0; k < kNumStripKinds; ++k)
            n += (int) engine.getStrips (static_cast<StripKind> (k)).size();
        return n;
    }

    int countBits (juce::uint64 v) noexcept
    {
        int n = 0;
        for (; v != 0; v &= v - 1)
            ++n;
        return n;
    }

    juce::String outOf (int n, int total, const char* one, const char* many)
    {
        return juce::String (n) + " of " + juce::String (total) + " " + (total == 1 ? one : many);
    }
} // namespace

//==============================================================================
void applyScopeChange (state::Scope& scope, const state::Scope& items, bool in, MixerEngine& engine)
{
    if (in)
    {
        scope.sections |= items.sections;
        scope.dcas |= items.dcas;
        scope.muteGroups |= items.muteGroups;
        scope.fx |= items.fx;
        scope.console = scope.console || items.console;
    }
    else
    {
        scope.sections &= ~items.sections;
        scope.dcas &= ~items.dcas;
        scope.muteGroups &= ~items.muteGroups;
        scope.fx &= ~items.fx;
        scope.console = scope.console && ! items.console;
    }
    scope.sections &= state::AllSections;
    if (items.strips.empty() || (in && scope.allStrips))
        return;
    if (scope.allStrips)
    {
        // Every strip but these: a list of the others.
        scope.allStrips = false;
        scope.strips.clear();
        for (int k = 0; k < kNumStripKinds; ++k)
            for (auto& s : engine.getStrips (static_cast<StripKind> (k)))
                scope.strips.push_back (state::StripRef::of (*s));
    }
    for (const auto& r : items.strips)
    {
        scope.strips.erase (std::remove (scope.strips.begin(), scope.strips.end(), r), scope.strips.end());
        if (in)
            scope.strips.push_back (r);
    }
    // Every strip of the console in the list: every strip, those added later too.
    bool every = totalStrips (engine) > 0;
    for (int k = 0; every && k < kNumStripKinds; ++k)
        for (auto& s : engine.getStrips (static_cast<StripKind> (k)))
            if (! scope.includes (state::StripRef::of (*s)))
            {
                every = false;
                break;
            }
    if (every)
    {
        scope.allStrips = true;
        scope.strips.clear();
    }
}

juce::String describeScope (const state::Scope& scope, MixerEngine& engine, ScopeRole role, const std::function<bool (const Strip&)>& stripOn)
{
    const auto counts = engine.getCounts();
    const int strips = totalStrips (engine);
    int stripsIn = 0;
    for (int k = 0; k < kNumStripKinds; ++k)
        for (auto& s : engine.getStrips (static_cast<StripKind> (k)))
            stripsIn += (stripOn ? stripOn (*s) : scope.includes (state::StripRef::of (*s))) ? 1 : 0;
    const int dcas = (scope.dcas & GroupBits::first (counts.dcas)).count();
    const int groups = (scope.muteGroups & GroupBits::first (counts.muteGroups)).count();
    const int fx = countBits (scope.fx & state::Scope::allFx (counts.fx));
    const int sections = countBits (scope.sections & state::AllSections);

    // The sections by name while they are few, or while few are missing.
    const auto named = [&scope] (bool in)
    {
        juce::StringArray names;
        for (const auto& [bit, name] : kSections)
            if (((scope.sections & bit) != 0) == in)
                names.add (name);
        return names.joinIntoString (" ");
    };

    if (role == ScopeRole::Safe)
    {
        // Only what is safe.
        juce::StringArray parts;
        if (stripsIn > 0)
            parts.add (juce::String (stripsIn) + (stripsIn == 1 ? " strip" : " strips"));
        if (sections > 0)
            parts.add (sections <= 4 ? named (true) : outOf (sections, state::kNumSections, "section", "sections"));
        if (dcas > 0)
            parts.add (juce::String (dcas) + (dcas == 1 ? " DCA" : " DCAs"));
        if (groups > 0)
            parts.add (juce::String (groups) + (groups == 1 ? " mute group" : " mute groups"));
        if (fx > 0)
            parts.add (juce::String (fx) + " FX");
        if (scope.console)
            parts.add ("console settings");
        return parts.isEmpty() ? juce::String ("Nothing") : parts.joinIntoString (", ");
    }

    const bool everything = sections == state::kNumSections && stripsIn == strips && dcas == counts.dcas && groups == counts.muteGroups && fx == counts.fx
                            && scope.console;
    if (everything)
        return "Everything";
    if (sections == 0 && stripsIn == 0 && dcas == 0 && groups == 0 && fx == 0 && ! scope.console)
        return "Nothing";
    juce::StringArray parts;
    if (sections == state::kNumSections)
        parts.add ("every section");
    else if (sections == 0)
        parts.add ("no section");
    else if (sections <= 3)
        parts.add ("only " + named (true));
    else if (sections >= state::kNumSections - 3)
        parts.add ("all but " + named (false));
    else
        parts.add (outOf (sections, state::kNumSections, "section", "sections"));
    parts.add (stripsIn == strips ? juce::String ("every strip") : outOf (stripsIn, strips, "strip", "strips"));
    if (counts.dcas > 0)
        parts.add (outOf (dcas, counts.dcas, "DCA", "DCAs"));
    if (counts.muteGroups > 0)
        parts.add (outOf (groups, counts.muteGroups, "mute group", "mute groups"));
    if (counts.fx > 0)
        parts.add (outOf (fx, counts.fx, "FX", "FX"));
    parts.add (scope.console ? "console settings" : "no console settings");
    return parts.joinIntoString (", ");
}

bool isPartialScope (const state::Scope& scope, MixerEngine& engine)
{
    const auto counts = engine.getCounts();
    const auto usual = (juce::uint32) state::AllSections & ~(juce::uint32) state::Routing;
    if ((scope.sections & usual) != usual || ! scope.console)
        return true;
    for (int k = 0; k < kNumStripKinds; ++k)
        for (auto& s : engine.getStrips (static_cast<StripKind> (k)))
            if (! scope.includes (state::StripRef::of (*s)))
                return true;
    return (scope.dcas & GroupBits::first (counts.dcas)).count() != counts.dcas || (scope.muteGroups & GroupBits::first (counts.muteGroups)).count() != counts.muteGroups
           || countBits (scope.fx & state::Scope::allFx (counts.fx)) != counts.fx;
}

//==============================================================================
/** The keys under their headings, scrolled. */
class ScopeEditor::Body final : public juce::Component
{
public:
    explicit Body (ScopeEditor& o) : owner (o) {}

    struct Group
    {
        int id = 0, count = 0, lit = 0, y = 0;
    };

    int count (int group) const
    {
        auto& engine = owner.ctx.engine;
        if (group < kNumStripKinds)
            return (int) engine.getStrips (static_cast<StripKind> (group)).size();
        switch (group)
        {
            case kDcas:       return engine.getNumDcas();
            case kMuteGroups: return engine.getNumMuteGroups();
            case kFx:         return engine.getFx().getNumSlots();
            default:          break;
        }
        return 1;
    }

    bool isOn (int group, int i) const
    {
        const auto& scope = owner.shown;
        if (group < kNumStripKinds)
        {
            const auto& list = owner.ctx.engine.getStrips (static_cast<StripKind> (group));
            return juce::isPositiveAndBelow (i, (int) list.size()) && owner.hasStrip (*list[(size_t) i]);
        }
        switch (group)
        {
            case kDcas:       return scope.dcas.contains (i);
            case kMuteGroups: return scope.muteGroups.contains (i);
            case kFx:         return i < 64 && ((scope.fx >> i) & 1u) != 0;
            default:          break;
        }
        return scope.console;
    }

    /** A key's two lines: what it is, and its name. */
    std::pair<juce::String, juce::String> text (int group, int i) const
    {
        auto& engine = owner.ctx.engine;
        if (group < kNumStripKinds)
        {
            const auto& s = *engine.getStrips (static_cast<StripKind> (group))[(size_t) i];
            const auto name = s.getName();
            return { s.getLabel(), name != s.getLabel() ? name : juce::String() };
        }
        switch (group)
        {
            case kDcas:       return { juce::String (i + 1), customGroupName (engine.getDcaName (i), i) };
            case kMuteGroups: return { juce::String (i + 1), customGroupName (engine.getMuteGroupName (i), i) };
            case kFx:
            {
                const auto d = engine.getFx().describe (i);
                return { juce::String (i + 1), d.containsChar (':') ? d.fromFirstOccurrenceOf (":", false, false).trim() : juce::String() };
            }
            default: break;
        }
        return { "SETTINGS", "monitor, input select, automix" };
    }

    int columns (int width) const { return juce::jmax (1, width / kKeyMin); }

    /** The groups shown (those with keys), laid out at `width`; returns the height. */
    int layout (int width, std::vector<Group>& out) const
    {
        out.clear();
        const int cols = columns (width);
        int y = 0;
        for (int g = 0; g < kNumGroups; ++g)
        {
            const int n = count (g);
            if (n == 0)
                continue;
            Group group;
            group.id = g;
            group.count = n;
            group.y = y;
            out.push_back (group);
            y += kHeader + (g == kConsole ? 1 : (n + cols - 1) / cols) * kKey + kGroupGap;
        }
        return y;
    }

    juce::Rectangle<int> keyArea (const Group& group, int i) const
    {
        if (group.id == kConsole)
            return { 0, group.y + kHeader, juce::jmin (getWidth(), 300), kKey };
        const int cols = columns (getWidth());
        const int w = getWidth() / cols;
        return { (i % cols) * w, group.y + kHeader + (i / cols) * kKey, w, kKey };
    }

    juce::Rectangle<int> stripKeyArea (const Strip& s) const
    {
        for (const auto& group : groups)
            if (group.id == (int) s.getKind())
            {
                const auto& list = owner.ctx.engine.getStrips (s.getKind());
                for (int i = 0; i < (int) list.size(); ++i)
                    if (list[(size_t) i].get() == &s)
                        return keyArea (group, i);
            }
        return {};
    }

    /** Takes the console and the scope again; repaints if anything it draws changed. */
    void refresh()
    {
        std::vector<Group> now;
        const int height = layout (getWidth(), now);
        juce::String drawn;
        for (auto& group : now)
        {
            drawn << group.id << ":";
            for (int i = 0; i < group.count; ++i)
            {
                const bool on = isOn (group.id, i);
                group.lit += on ? 1 : 0;
                const auto t = text (group.id, i);
                drawn << (on ? "+" : "-") << t.first << "/" << t.second << "|";
            }
        }
        groups = std::move (now);
        if (height != getHeight())
            setSize (getWidth(), height);
        if (drawn != shownText)
        {
            shownText = drawn;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = owner.theme;
        const auto clip = g.getClipBounds();
        const auto on = owner.onColour();
        for (const auto& group : groups)
        {
            // The heading: what the keys are, how many are lit; a tap does them all.
            auto header = juce::Rectangle<int> (0, group.y, getWidth(), kHeader).reduced (2, 0);
            if (header.getBottom() >= clip.getY() && header.getY() <= clip.getBottom())
            {
                g.setColour (group.id < kNumStripKinds ? t.kindColour (static_cast<StripKind> (group.id)) : t.text);
                g.setFont (type::font (type::kCaption, true));
                g.drawText (kindTitle (group.id), header, juce::Justification::centredLeft);
                g.setColour (group.lit > 0 ? on : t.textDim);
                g.drawText (group.id == kConsole ? juce::String (group.lit > 0 ? "IN" : "OUT") : juce::String (group.lit) + " / " + juce::String (group.count), header,
                            juce::Justification::centredRight);
                g.setColour (t.outline);
                g.fillRect (header.removeFromBottom (1));
            }
            for (int i = 0; i < group.count; ++i)
            {
                const auto area = keyArea (group, i);
                if (area.getBottom() < clip.getY() || area.getY() > clip.getBottom())
                    continue;
                const auto r = area.toFloat().reduced (2.0f);
                const bool lit = isOn (group.id, i);
                g.setColour (lit ? on : t.raised);
                g.fillRoundedRectangle (r, sizes::kRadius);
                if (! lit)
                {
                    g.setColour (t.outline);
                    g.drawRoundedRectangle (r.reduced (0.5f), sizes::kRadius, 1.0f);
                }
                const auto words = text (group.id, i);
                auto inner = r.reduced (3.0f, 2.0f);
                g.setColour (lit ? t.textOnAccent : t.text);
                g.setFont (type::font (type::kLabel, true));
                if (words.second.isEmpty())
                {
                    g.drawFittedText (words.first, inner.toNearestInt(), juce::Justification::centred, 1, 0.7f);
                    continue;
                }
                g.drawFittedText (words.first, inner.removeFromTop (inner.getHeight() * 0.55f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
                g.setColour (lit ? t.textOnAccent : t.textDim);
                g.setFont (type::font (type::kMicro));
                g.drawFittedText (words.second, inner.toNearestInt(), juce::Justification::centred, 1, 0.7f);
            }
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown())
            return;
        for (const auto& group : groups)
        {
            if (juce::Rectangle<int> (0, group.y, getWidth(), kHeader).contains (e.getPosition()))
            {
                owner.toggleGroup (group.id);
                return;
            }
            for (int i = 0; i < group.count; ++i)
                if (keyArea (group, i).contains (e.getPosition()))
                {
                    owner.toggleItem (group.id, i);
                    return;
                }
        }
    }

    std::vector<Group> groups;

private:
    ScopeEditor& owner;
    juce::String shownText;
};

//==============================================================================
ScopeEditor::ScopeEditor (UiContext& c, const Theme& t, ScopeRole r)
    : allKey ("ALL", t.accent, t), noneKey ("NONE", t.accent, t), ctx (c), theme (t), role (r), body (std::make_unique<Body> (*this))
{
    allKey.onClick = [this] { setAll (true); };
    noneKey.onClick = [this] { setAll (false); };
    addAndMakeVisible (allKey);
    addAndMakeVisible (noneKey);
    for (const auto& [bit, name] : kSections)
    {
        auto key = std::make_unique<ToggleKey> (name, onColour(), t);
        key->isOn = [this, b = bit] { return (shown.sections & b) != 0; };
        key->onClick = [this, b = bit] { toggleSection (b); };
        addAndMakeVisible (*key);
        sectionKeys.push_back (std::move (key));
    }
    viewport.setViewedComponent (body.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme.outline);
    addAndMakeVisible (viewport);
    startTimerHz (5);
}

ScopeEditor::~ScopeEditor()
{
    stopTimer();
}

juce::Colour ScopeEditor::onColour() const noexcept
{
    // The safes in the solo colour: they hold a recall back, where a scope's keys let it through.
    return role == ScopeRole::Safe ? theme.solo : theme.accent;
}

void ScopeEditor::setSectionsShown (bool show)
{
    sectionsShown = show;
    allKey.setVisible (show);
    noneKey.setVisible (show);
    for (auto& k : sectionKeys)
        k->setVisible (show);
    resized();
}

bool ScopeEditor::hasStrip (const Strip& s) const
{
    return stripOn ? stripOn (s) : shown.includes (state::StripRef::of (s));
}

void ScopeEditor::send (const state::Scope& items, bool in)
{
    if (change)
        change (items, in);
    refresh();
}

void ScopeEditor::toggleSection (juce::uint32 section)
{
    refresh();
    send (state::Scope::only (section, {}), (shown.sections & section) == 0);
}

void ScopeEditor::toggleStrip (const Strip& s)
{
    refresh();
    send (state::Scope::only (0, { state::StripRef::of (s) }), ! hasStrip (s));
}

void ScopeEditor::toggleKind (StripKind kind)
{
    refresh();
    std::vector<state::StripRef> refs;
    bool all = true;
    for (auto& s : ctx.engine.getStrips (kind))
    {
        refs.push_back (state::StripRef::of (*s));
        all = all && hasStrip (*s);
    }
    if (! refs.empty())
        send (state::Scope::only (0, std::move (refs)), ! all);
}

void ScopeEditor::toggleDca (int index)
{
    refresh();
    auto items = state::Scope::none();
    items.dcas = GroupBits::single (index);
    send (items, ! shown.dcas.contains (index));
}

void ScopeEditor::toggleMuteGroup (int index)
{
    refresh();
    auto items = state::Scope::none();
    items.muteGroups = GroupBits::single (index);
    send (items, ! shown.muteGroups.contains (index));
}

void ScopeEditor::toggleFx (int slot)
{
    if (! juce::isPositiveAndBelow (slot, 64))
        return;
    refresh();
    auto items = state::Scope::none();
    items.fx = juce::uint64 (1) << slot;
    send (items, (shown.fx & items.fx) == 0);
}

void ScopeEditor::toggleConsole()
{
    refresh();
    auto items = state::Scope::none();
    items.console = true;
    send (items, ! shown.console);
}

void ScopeEditor::toggleItem (int group, int index)
{
    if (group < kNumStripKinds)
    {
        const auto& list = ctx.engine.getStrips (static_cast<StripKind> (group));
        if (juce::isPositiveAndBelow (index, (int) list.size()))
            toggleStrip (*list[(size_t) index]);
    }
    else if (group == kDcas)
        toggleDca (index);
    else if (group == kMuteGroups)
        toggleMuteGroup (index);
    else if (group == kFx)
        toggleFx (index);
    else
        toggleConsole();
}

void ScopeEditor::toggleGroup (int group)
{
    if (group < kNumStripKinds)
    {
        toggleKind (static_cast<StripKind> (group));
        return;
    }
    if (group == kConsole)
    {
        toggleConsole();
        return;
    }
    // Every DCA, mute group or FX slot of the console in, unless they all are.
    refresh();
    const auto counts = ctx.engine.getCounts();
    auto items = state::Scope::none();
    bool all = false;
    if (group == kDcas)
    {
        items.dcas = GroupBits::first (counts.dcas);
        all = (shown.dcas & items.dcas) == items.dcas;
    }
    else if (group == kMuteGroups)
    {
        items.muteGroups = GroupBits::first (counts.muteGroups);
        all = (shown.muteGroups & items.muteGroups) == items.muteGroups;
    }
    else
    {
        items.fx = state::Scope::allFx (counts.fx);
        all = (shown.fx & items.fx) == items.fx;
    }
    send (items, ! all);
}

void ScopeEditor::setAll (bool in)
{
    // Everything there can be (groups and slots a bigger console would have too), the strips as a list.
    auto items = state::Scope::everything();
    items.allStrips = false;
    for (int k = 0; k < kNumStripKinds; ++k)
        for (auto& s : ctx.engine.getStrips (static_cast<StripKind> (k)))
            items.strips.push_back (state::StripRef::of (*s));
    send (items, in);
}

juce::String ScopeEditor::getSummary() const
{
    return describeScope (get ? get() : shown, ctx.engine, role, stripOn);
}

void ScopeEditor::refresh()
{
    auto now = get ? get() : state::Scope::none();
    const bool sectionsChanged = now.sections != shown.sections;
    shown = std::move (now);
    if (sectionsChanged)
        for (auto& k : sectionKeys)
            k->refresh();
    body->refresh();
}

int ScopeEditor::getBodyHeight (int width) const
{
    std::vector<Body::Group> groups;
    return body->layout (width, groups);
}

juce::Rectangle<int> ScopeEditor::getStripKeyArea (const Strip& s) const
{
    return body->stripKeyArea (s);
}

void ScopeEditor::paint (juce::Graphics&) {}

void ScopeEditor::resized()
{
    auto area = getLocalBounds();
    if (sectionsShown)
    {
        // ALL and NONE, then the sections in two rows of five.
        auto top = area.removeFromTop (sizes::kTouch);
        const int half = (top.getWidth() - sizes::kGapSmall) / 2;
        allKey.setBounds (top.removeFromLeft (half));
        noneKey.setBounds (top.withTrimmedLeft (sizes::kGapSmall));
        const int w = (area.getWidth() - 4 * sizes::kGapSmall) / 5;
        for (int i = 0; i < (int) sectionKeys.size(); ++i)
        {
            if (i % 5 == 0)
                area.removeFromTop (sizes::kGapSmall);
            sectionKeys[(size_t) i]->setBounds (area.withHeight (sizes::kTouch).withTrimmedLeft ((i % 5) * (w + sizes::kGapSmall)).withWidth (w));
            if (i % 5 == 4)
                area.removeFromTop (sizes::kTouch);
        }
        area.removeFromTop (sizes::kGap);
    }
    viewport.setBounds (area);
    body->setSize (juce::jmax (0, viewport.getWidth() - viewport.getScrollBarThickness()), body->getHeight());
    body->refresh();
}

} // namespace livedaw::ui::universal
