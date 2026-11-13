#include "ui/PluginHostControl.h"

#include "engine/FxSlots.h"
#include "engine/SandboxedPlugin.h"

#include <algorithm>

namespace livedaw::ui::ph
{

//==============================================================================
bool Target::operator== (const Target& o) const noexcept
{
    if (kind != o.kind)
        return false;
    switch (kind)
    {
        case Kind::Param:  return plugin == o.plugin && param == o.param;
        case Kind::Hot:    return channel == o.channel && hot == o.hot;
        case Kind::Select:
        case Kind::Bypass: return channel == o.channel;
        default:           return true;
    }
}

Target Target::ofParam (const PluginRef& plugin, const ParamRef& param)
{
    Target t;
    t.kind = Kind::Param;
    t.channel = plugin.channel;
    t.plugin = plugin;
    t.param = param;
    return t;
}

Target Target::ofHot (int channel, int hot)
{
    Target t;
    t.kind = Kind::Hot;
    t.channel = channel;
    t.hot = juce::jlimit (0, 1, hot);
    return t;
}

Target Target::of (Kind kind, int channel)
{
    Target t;
    t.kind = kind;
    t.channel = channel;
    return t;
}

//==============================================================================
namespace
{
    juce::StringArray portNames()
    {
        juce::StringArray names;
        for (int i = 0; i < Control::kMaxInputs; ++i)
            names.add ("PLUGIN HOST " + juce::String (i + 1));
        return names;
    }

    /** The names of the target kinds in the show's XML (index = Target::Kind; the first two are never saved). */
    const char* const kKindNames[] = { "", "param", "hot", "select", "bypass", "go", "prev", "next" };
} // namespace

Control::Control (UiContext& c, bool useDevices) : ctx (c), ports (portNames(), useDevices)
{
    ports.onMessage = [this] (int port, const juce::MidiMessage& m)
    {
        ports.noteReceived (port);
        handleMidi (m);
    };
    loadFromShow();
    applyDevices();
    startTimerHz (4);
}

Control::~Control()
{
    stopTimer();
    ports.onMessage = nullptr;
    ports.closeAll();
}

void Control::changed()
{
    if (onChanged)
        onChanged();
}

Strip* Control::stripAt (int channel) const
{
    const auto& list = ctx.engine.getChannels();
    return juce::isPositiveAndBelow (channel, (int) list.size()) ? list[(size_t) channel].get() : nullptr;
}

//==============================================================================
// The racks

std::vector<PluginRef> Control::pluginsOf (int channel) const
{
    std::vector<PluginRef> out;
    auto* s = stripAt (channel);
    if (s == nullptr)
        return out;
    auto& fx = ctx.engine.getFx();
    for (const auto& step : s->getChain())
    {
        if (! step.isBlock() || step.getBlockKind() != BlockKind::Plugin)
            continue;
        const int slot = FxSlots::blockSlotOf (s->getHandle(), step.uid);
        if (slot < 0)
            continue;
        for (int i = 0, n = fx.getNumChainMembers (slot); i < n; ++i)
            out.push_back ({ channel, step.uid, i });
    }
    return out;
}

int Control::slotOf (const PluginRef& ref) const
{
    auto* s = ref.strip != nullptr ? ref.strip : stripAt (ref.channel);
    return s != nullptr && ref.uid >= 0 ? FxSlots::blockSlotOf (s->getHandle(), ref.uid) : -1;
}

juce::AudioPluginInstance* Control::pluginOf (const PluginRef& ref) const
{
    const int slot = slotOf (ref);
    auto& fx = ctx.engine.getFx();
    return slot >= 0 && juce::isPositiveAndBelow (ref.member, fx.getNumChainMembers (slot)) ? fx.getChainMember (slot, ref.member) : nullptr;
}

juce::String Control::nameOf (const PluginRef& ref) const
{
    auto* p = pluginOf (ref);
    return p != nullptr ? p->getName() : juce::String();
}

PluginChain::Control Control::controlOf (const PluginRef& ref) const
{
    const int slot = slotOf (ref);
    return slot >= 0 ? ctx.engine.getFx().getChainMemberControl (slot, ref.member) : PluginChain::Control {};
}

void Control::setControlOf (const PluginRef& ref, PluginChain::Control control)
{
    const int slot = slotOf (ref);
    if (slot < 0)
        return;
    ctx.engine.getFx().setChainMemberControl (slot, ref.member, std::move (control));
    changed();
}

juce::StringArray Control::parameterNames (const PluginRef& ref) const
{
    juce::StringArray names;
    if (auto* p = pluginOf (ref))
        for (auto* parameter : p->getParameters())
            names.add (parameter->getName (64));
    return names;
}

bool Control::isSandboxed (const PluginRef& ref) const
{
    return dynamic_cast<SandboxedPlugin*> (pluginOf (ref)) != nullptr;
}

//==============================================================================
// Hot knobs

PluginRef Control::knobPlugin (int channel) const
{
    const auto all = pluginsOf (channel);
    if (getFocus)
    {
        const auto focus = getFocus();
        if (focus.channel == channel && focus.uid >= 0)
            for (const auto& r : all)
                if (r == focus && controlOf (r).hasHot())
                    return r;
    }
    for (const auto& r : all)
        if (controlOf (r).hasHot())
            return r;
    return {};
}

juce::AudioProcessorParameter* Control::hotParam (int channel, int knob) const
{
    const auto ref = knobPlugin (channel);
    auto* plugin = ref.isValid() ? pluginOf (ref) : nullptr;
    return plugin != nullptr ? controlOf (ref).hot[(size_t) knob].find (*plugin) : nullptr;
}

std::array<Control::Hot, 2> Control::getHots (int channel) const
{
    std::array<Hot, 2> out;
    const auto ref = knobPlugin (channel);
    auto* plugin = ref.isValid() ? pluginOf (ref) : nullptr;
    if (plugin == nullptr)
        return out;
    const auto control = controlOf (ref);
    for (size_t k = 0; k < 2; ++k)
        if (auto* p = control.hot[k].find (*plugin))
            out[k] = { true, p->getValue(), p->getName (8).toUpperCase(), p->getName (64), p->getCurrentValueAsText() };
    return out;
}

bool Control::turnHot (int channel, int knob, float delta)
{
    knob = juce::jlimit (0, 1, knob);
    auto* p = hotParam (channel, knob);
    if (p == nullptr)
        return false;
    if (p->isDiscrete() && p->getNumSteps() > 1)
    {
        const float step = 1.0f / (float) (p->getNumSteps() - 1);
        auto& carry = carried[channel * 2 + knob];
        carry += delta;
        if (std::abs (carry) < step * 0.5f)
            return true;
        const int steps = juce::roundToInt (carry / step);
        carry -= (float) steps * step;
        delta = (float) steps * step;
    }
    p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + delta));
    return true;
}

bool Control::setHot (int channel, int knob, float value)
{
    knob = juce::jlimit (0, 1, knob);
    auto* p = hotParam (channel, knob);
    if (p == nullptr)
        return false;
    carried.erase (channel * 2 + knob);
    p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value));
    return true;
}

bool Control::assignHot (const PluginRef& ref, int knob, const ParamRef& param)
{
    knob = juce::jlimit (0, 1, knob);
    auto* plugin = pluginOf (ref);
    auto* found = plugin != nullptr && param.isSet() ? param.find (*plugin) : nullptr;
    if (found == nullptr)
        return false;
    const auto fixed = ParamRef::of (*plugin, found->getParameterIndex());
    auto control = controlOf (ref);
    const auto other = (size_t) (1 - knob);
    if (control.hot[other] == fixed)
        control.hot[other] = {}; // a parameter is on one knob only
    control.hot[(size_t) knob] = fixed;
    setControlOf (ref, std::move (control));
    return true;
}

void Control::clearHot (const PluginRef& ref, int knob)
{
    auto control = controlOf (ref);
    control.hot[(size_t) juce::jlimit (0, 1, knob)] = {};
    setControlOf (ref, std::move (control));
}

//==============================================================================
// Devices

juce::StringArray Control::getEnabledInputs() const
{
    juce::StringArray names;
    names.addLines (ctx.settings.getValue (inputsKey));
    names.removeEmptyStrings();
    return names;
}

void Control::setInputEnabled (const juce::String& name, bool enabled)
{
    auto names = getEnabledInputs();
    if (enabled)
    {
        if (names.contains (name) || names.size() >= kMaxInputs)
            return;
        names.add (name);
    }
    else
        names.removeString (name);
    ctx.settings.setValue (inputsKey, names.joinIntoString ("\n"));
    ctx.settings.saveIfNeeded();
    applyDevices();
    changed();
}

bool Control::isInputOpen (const juce::String& name) const
{
    const int port = getEnabledInputs().indexOf (name);
    return port >= 0 && ports.getStatus (port).inputOpen;
}

void Control::applyDevices()
{
    const auto names = getEnabledInputs();
    std::vector<MidiPorts::Want> wants ((size_t) kMaxInputs);
    for (int i = 0; i < juce::jmin (names.size(), kMaxInputs); ++i)
        wants[(size_t) i] = { true, names[i], {} };
    ports.open (std::move (wants));
}

//==============================================================================
// Learn

void Control::setLearning (bool on)
{
    learn = on ? Learn::WaitTarget : Learn::Off;
    learnTarget = {};
    note.clear();
    changed();
}

bool Control::captureTarget (const Target& t)
{
    if (learn == Learn::Off || t.kind == Target::Kind::None)
        return false;
    learnTarget = t;
    learn = Learn::WaitMidi;
    note.clear();
    changed();
    return true;
}

void Control::handleMidi (const juce::MidiMessage& m)
{
    MidiSource src;
    int value = 127;
    if (m.isController())
    {
        src.kind = MidiSource::Kind::Cc;
        src.number = m.getControllerNumber();
        value = m.getControllerValue();
    }
    else if (m.isNoteOn())
    {
        src.kind = MidiSource::Kind::Note;
        src.number = m.getNoteNumber();
    }
    else
        return;
    src.channel = juce::jlimit (1, 16, m.getChannel());

    if (learn == Learn::WaitMidi)
        finishLearn (src);
    else if (learn == Learn::Off)
        dispatch (src, value);
}

void Control::finishLearn (const MidiSource& src)
{
    const auto target = learnTarget;
    learn = Learn::Off;
    learnTarget = {};
    addMapping (target, src);
    note = src.describe() + "  ->  " + describeTarget (target);
    changed();
}

void Control::addMapping (const Target& target, const MidiSource& src)
{
    // One control, one target; and one control for a target.
    for (int ch = 0, n = (int) ctx.engine.getChannels().size(); ch < n; ++ch)
        for (const auto& ref : pluginsOf (ch))
        {
            auto control = controlOf (ref);
            const auto before = control.maps.size();
            control.maps.erase (std::remove_if (control.maps.begin(), control.maps.end(),
                                                [&] (const PluginChain::ParamMap& m)
                                                {
                                                    return m.source.sameControl (src) || (target.kind == Target::Kind::Param && target.plugin == ref && m.param == target.param);
                                                }),
                                control.maps.end());
            if (control.maps.size() != before)
                setControlOf (ref, std::move (control));
        }
    globals.erase (std::remove_if (globals.begin(), globals.end(), [&] (const Global& g) { return g.source.sameControl (src) || g.target == target; }), globals.end());

    if (target.kind == Target::Kind::Param)
    {
        auto control = controlOf (target.plugin);
        control.maps.push_back ({ src, target.param });
        setControlOf (target.plugin, std::move (control));
    }
    else
    {
        globals.push_back ({ target, src });
        saveGlobal();
    }
}

//==============================================================================
// Mapped controls at work

void Control::applyTo (juce::AudioProcessorParameter& p, const MidiSource& src, int value)
{
    if (src.kind == MidiSource::Kind::Note)
    {
        p.setValueNotifyingHost (p.getValue() < 0.5f ? 1.0f : 0.0f);
        return;
    }
    if (src.mode == MidiSource::Mode::Absolute)
    {
        p.setValueNotifyingHost ((float) juce::jlimit (0, 127, value) / 127.0f);
        return;
    }
    const int ticks = value < 64 ? value : value - 128;
    const bool stepped = p.isDiscrete() && p.getNumSteps() > 1;
    const float amount = stepped ? (ticks > 0 ? 1.0f : ticks < 0 ? -1.0f : 0.0f) / (float) (p.getNumSteps() - 1) : (float) ticks * 0.01f;
    p.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p.getValue() + amount));
}

void Control::dispatch (const MidiSource& src, int value)
{
    bool rising = false;
    if (src.kind == MidiSource::Kind::Cc)
    {
        auto& last = lastCc[(size_t) (src.channel - 1)][(size_t) src.number];
        rising = value >= 64 && last < 64;
        last = value;
    }

    const auto copy = globals;
    for (const auto& g : copy)
        if (g.source.sameControl (src))
            actOn (g.target, g.source, value, rising);

    for (int ch = 0, n = (int) ctx.engine.getChannels().size(); ch < n; ++ch)
        for (const auto& ref : pluginsOf (ch))
        {
            auto* plugin = pluginOf (ref);
            if (plugin == nullptr)
                continue;
            for (const auto& m : controlOf (ref).maps)
                if (m.source.sameControl (src))
                    if (auto* p = m.param.find (*plugin))
                        applyTo (*p, m.source, value);
        }
}

void Control::actOn (const Target& t, const MidiSource& src, int value, bool rising)
{
    const bool press = src.kind == MidiSource::Kind::Note || rising;
    switch (t.kind)
    {
        case Target::Kind::Select:
            if (press && selectChannel)
                selectChannel (t.channel);
            break;
        case Target::Kind::Bypass:
            if (auto* s = stripAt (t.channel))
                s->params.input.bypass = src.kind == MidiSource::Kind::Note ? ! s->params.input.bypass.load() : value >= 64;
            break;
        case Target::Kind::SceneGo:
            if (press && sceneGo)
                sceneGo();
            break;
        case Target::Kind::ScenePrev:
        case Target::Kind::SceneNext:
            if (press && sceneStep)
                sceneStep (t.kind == Target::Kind::ScenePrev ? -1 : 1);
            break;
        case Target::Kind::Hot:
            if (src.kind == MidiSource::Kind::Note)
            {
                if (auto* p = hotParam (t.channel, t.hot))
                    setHot (t.channel, t.hot, p->getValue() < 0.5f ? 1.0f : 0.0f);
            }
            else if (src.mode == MidiSource::Mode::Absolute)
                setHot (t.channel, t.hot, (float) value / 127.0f);
            else
                turnHot (t.channel, t.hot, (float) (value < 64 ? value : value - 128) * 0.01f);
            break;
        case Target::Kind::Param:
        case Target::Kind::None:
            break;
    }
}

//==============================================================================
// The mappings

juce::String Control::describeTarget (const Target& t) const
{
    const auto ch = "CH " + juce::String (t.channel + 1) + "  ";
    switch (t.kind)
    {
        case Target::Kind::Param:     return ch + (nameOf (t.plugin).isNotEmpty() ? nameOf (t.plugin) : juce::String ("plug-in")) + ": " + t.param.name;
        case Target::Kind::Hot:       return ch + "HOT " + juce::String (t.hot + 1);
        case Target::Kind::Select:    return ch + "SELECT";
        case Target::Kind::Bypass:    return ch + "BYPASS";
        case Target::Kind::SceneGo:   return "SCENE GO";
        case Target::Kind::ScenePrev: return "SCENE PREV";
        case Target::Kind::SceneNext: return "SCENE NEXT";
        case Target::Kind::None:      break;
    }
    return {};
}

juce::String Control::describeLearn() const
{
    switch (learn)
    {
        case Learn::Off:        return {};
        case Learn::WaitTarget: return "LEARN: TAP OR MOVE A PLUG-IN PARAMETER, A HOT KNOB, SELECT, BYPASS OR A SCENE KEY";
        case Learn::WaitMidi:   return "LEARN: NOW MOVE A MIDI CONTROL FOR  " + describeTarget (learnTarget);
    }
    return {};
}

std::vector<Control::Row> Control::getMappings() const
{
    std::vector<Row> rows;
    for (const auto& g : globals)
        rows.push_back ({ g.source, g.target, g.source.describe(), describeTarget (g.target) });
    for (int ch = 0, n = (int) ctx.engine.getChannels().size(); ch < n; ++ch)
        for (const auto& ref : pluginsOf (ch))
            for (const auto& m : controlOf (ref).maps)
            {
                const auto target = Target::ofParam (ref, m.param);
                rows.push_back ({ m.source, target, m.source.describe(), describeTarget (target) });
            }
    return rows;
}

bool Control::deleteMapping (int row)
{
    const auto rows = getMappings();
    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return false;
    const auto& r = rows[(size_t) row];
    if (r.target.kind == Target::Kind::Param)
    {
        auto control = controlOf (r.target.plugin);
        const auto at = std::find_if (control.maps.begin(), control.maps.end(), [&] (const auto& m) { return m.source == r.source && m.param == r.target.param; });
        if (at == control.maps.end())
            return false;
        control.maps.erase (at);
        setControlOf (r.target.plugin, std::move (control));
    }
    else
    {
        const auto at = std::find_if (globals.begin(), globals.end(), [&] (const Global& g) { return g.target == r.target && g.source == r.source; });
        if (at == globals.end())
            return false;
        globals.erase (at);
        saveGlobal();
    }
    changed();
    return true;
}

bool Control::setMappingMode (int row, MidiSource::Mode mode)
{
    const auto rows = getMappings();
    if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].source.kind != MidiSource::Kind::Cc)
        return false;
    const auto& r = rows[(size_t) row];
    if (r.target.kind == Target::Kind::Param)
    {
        auto control = controlOf (r.target.plugin);
        for (auto& m : control.maps)
            if (m.source == r.source && m.param == r.target.param)
                m.source.mode = mode;
        setControlOf (r.target.plugin, std::move (control));
    }
    else
    {
        for (auto& g : globals)
            if (g.target == r.target && g.source == r.source)
                g.source.mode = mode;
        saveGlobal();
    }
    changed();
    return true;
}

void Control::clearAll()
{
    globals.clear();
    saveGlobal();
    for (int ch = 0, n = (int) ctx.engine.getChannels().size(); ch < n; ++ch)
        for (const auto& ref : pluginsOf (ch))
            if (auto control = controlOf (ref); ! control.maps.empty())
            {
                control.maps.clear();
                setControlOf (ref, std::move (control));
            }
    seen.clear();
    note = "All mappings deleted";
    changed();
}

void Control::clearNote()
{
    if (note.isNotEmpty())
    {
        note.clear();
        changed();
    }
}

//==============================================================================
// Saved with the show

void Control::saveGlobal()
{
    juce::String text;
    if (! globals.empty())
    {
        juce::XmlElement xml ("PluginHostControl");
        for (const auto& g : globals)
        {
            auto* e = xml.createNewChildElement ("MAP");
            g.source.writeTo (*e);
            e->setAttribute ("target", kKindNames[(int) g.target.kind]);
            if (g.target.channel >= 0)
                e->setAttribute ("channel", g.target.channel);
            if (g.target.kind == Target::Kind::Hot)
                e->setAttribute ("hot", g.target.hot);
        }
        text = xml.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    }
    savedText = text;
    ctx.show.setHostControlXml (text);
}

void Control::loadFromShow()
{
    const auto text = ctx.show.getHostControlXml();
    if (text == savedText)
        return;
    savedText = text;
    globals.clear();
    lastCc = {};
    carried.clear();
    seen.clear();
    learn = Learn::Off;
    learnTarget = {};
    if (auto xml = juce::parseXML (text))
        for (auto* e : xml->getChildWithTagNameIterator ("MAP"))
        {
            Target t;
            const auto name = e->getStringAttribute ("target");
            for (int k = 2; k < 8; ++k)
                if (name == kKindNames[k])
                    t.kind = static_cast<Target::Kind> (k);
            if (t.kind == Target::Kind::None)
                continue;
            t.channel = e->getIntAttribute ("channel", -1);
            t.hot = juce::jlimit (0, 1, e->getIntAttribute ("hot"));
            globals.push_back ({ t, MidiSource::readFrom (*e) });
        }
    changed();
}

void Control::poll()
{
    // A plug-in that went (removed, replaced, or taken out by a scene) takes its mappings with it: say so.
    std::map<const void*, Seen> now;
    auto& fx = ctx.engine.getFx();
    for (int ch = 0, n = (int) ctx.engine.getChannels().size(); ch < n; ++ch)
    {
        auto* s = stripAt (ch);
        for (const auto& step : s->getChain())
        {
            if (! step.isBlock() || step.getBlockKind() != BlockKind::Plugin)
                continue;
            const int slot = FxSlots::blockSlotOf (s->getHandle(), step.uid);
            auto* chain = slot >= 0 ? fx.getChain (slot) : nullptr;
            for (int i = 0; chain != nullptr && i < chain->size(); ++i)
                if (const auto count = (int) chain->getControl (i).maps.size(); count > 0)
                    now[chain->getMembers()[(size_t) i].get()] = { chain->getMember (i).getName(), count };
        }
    }
    for (const auto& [key, was] : seen)
        if (now.find (key) == now.end())
        {
            note = juce::String (was.maps) + (was.maps == 1 ? " mapping" : " mappings") + " went with " + was.name + ", which was removed";
            changed();
        }
    seen = std::move (now);
}

} // namespace livedaw::ui::ph
