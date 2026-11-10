// The SQ-5 touchscreen's other screens: Routing (Reference Guide pp.45-51), Meters
// (pp.52-59), FX (pp.60-65), Scenes (pp.66-79), Setup (pp.84-110), Utility (pp.12, 80-83,
// 111-125) and I/O (pp.23-28). A control Sidecar doesn't model is drawn dim and does nothing.

#include "ui/Sq5Display.h"

#include "daw/Session.h"
#include "engine/MixerEngine.h"
#include "engine/Models.h"
#include "engine/SqMidi.h"
#include "ui/MeterBank.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <tuple>

namespace livedaw::ui::sq5
{

using namespace universal;

namespace
{
    const juce::Colour kWell (0xff071019), kLine (0xff8fbcd9), kColumn (0xff3f82b3), kColumnSelected (0xff4aa56f), kPink (0xffe05cc8), kGreenBar (0xff7ee02e),
        kCurrent (0xff8fd14f), kNext (0xffb4c24a), kOffCell (0xff3a5a82), kOnCell (0xffe4ebf2);
    constexpr float kTabWidth = 70.0f;
    /** I/O's Outputs: LR's sides, then Mix 1-12 (their first side). */
    constexpr int outputRows = 2 + kMixes;
    int outputRowSource (int row) { return row < 2 ? MixerEngine::stripSource (StripKind::Main, 0, row) : MixerEngine::stripSource (StripKind::Bus, row - 2, 0); }

    void text (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, float size, juce::Colour c,
               juce::Justification j = juce::Justification::centred, bool bold = false)
    {
        g.setColour (c);
        g.setFont (type::font (size, bold));
        g.drawFittedText (t, r.getSmallestIntegerContainer(), j, 2, 0.75f);
    }

    juce::Colour inkOn (juce::Colour background) { return background.getPerceivedBrightness() > 0.35f ? juce::Colours::black : juce::Colours::white; }

    /** The body beside the side tabs. */
    juce::Rectangle<float> bodyArea() { return { kTabWidth + 6.0f, (float) Display::kTitleBar + 4.0f, (float) Display::kWidth - kTabWidth - 10.0f, (float) Display::kHeight - Display::kTitleBar - 8.0f }; }

    void meterBar (juce::Graphics& g, juce::Rectangle<float> r, float gain)
    {
        g.setColour (juce::Colours::black);
        g.fillRect (r);
        // The main meters show -40 dB to +10 dB (p.52).
        const float db = juce::Decibels::gainToDecibels (gain, -60.0f), f = juce::jlimit (0.0f, 1.0f, (db + 40.0f) / 50.0f);
        g.setColour (db > 3.0f ? juce::Colours::red : db > -6.0f ? juce::Colours::yellow : kGreenBar);
        g.fillRect (r.withTop (r.getBottom() - r.getHeight() * f));
    }

    /** A send's bar in the Routing bank view: filled if assigned, outlined if not, pink
        post-fade and green pre-fade (p.45). */
    void sendBar (juce::Graphics& g, juce::Rectangle<float> r, float db, bool assigned, bool pre)
    {
        const float f = (float) fader::dbToPos (db);
        const auto c = pre ? kGreenBar : kPink;
        const auto bar = r.withTop (r.getBottom() - r.getHeight() * f);
        if (assigned)
        {
            g.setColour (c);
            g.fillRect (bar);
        }
        else
        {
            g.setColour (c.withAlpha (0.8f));
            g.drawRect (bar, 0.8f);
        }
    }

    /** A value whose steps are numbered, its words from a function. */
    KnobBinding choiceBinding (const juce::String& label, int count, std::function<int()> get, std::function<void (int)> set, std::function<juce::String (int)> words)
    {
        KnobBinding b;
        b.label = label;
        b.steps = (float) juce::jmax (1, count);
        b.get = [get, count] { return count > 1 ? (float) get() / (float) (count - 1) : 0.0f; };
        b.set = [set, count] (float v) { set (juce::jlimit (0, juce::jmax (0, count - 1), juce::roundToInt (v * (float) (count - 1)))); };
        b.text = [get, words] { return words (get()); };
        return b;
    }

    /** A time as the SQ-Drive pages print it: 00:00:15. */
    juce::String clockText (double seconds)
    {
        const int s = juce::jmax (0, (int) seconds);
        return juce::String (s / 3600).paddedLeft ('0', 2) + ":" + juce::String (s / 60 % 60).paddedLeft ('0', 2) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }

    /** "t:o1:o2" as an assignment. */
    soft::Assign assignOf (const juce::String& item)
    {
        const auto parts = juce::StringArray::fromTokens (item, ":", {});
        return { juce::jlimit (0, soft::kNumTypes - 1, parts[0].getIntValue()), parts[1].getIntValue(), parts[2].getIntValue() };
    }

    /** A ganged value: a level, a switch or a choice of the engine's, read and set as a number. */
    struct Linked
    {
        std::atomic<float>* f = nullptr;
        std::atomic<bool>* b = nullptr;
        std::atomic<int32_t>* i = nullptr;
        /** A model's parameter: the model, as only like models follow each other. */
        int model = 0;

        float get() const { return f != nullptr ? f->load() : b != nullptr ? (b->load() ? 1.0f : 0.0f) : (float) i->load(); }
        void set (float v) const
        {
            if (f != nullptr)
                f->store (v);
            else if (b != nullptr)
                b->store (v > 0.5f);
            else
                i->store (juce::roundToInt (v));
        }
    };

    /** What a gang links of a channel (p.105's list; Direct Out has nothing here). */
    void collectLinked (Strip& s, int attributes, std::vector<Linked>& out)
    {
        auto& p = s.params;
        const auto has = [attributes] (int a) { return (attributes >> a & 1) != 0; };
        const auto value = [&out] (std::atomic<float>& v, int model = 0)
        {
            Linked l;
            l.f = &v;
            l.model = model;
            out.push_back (l);
        };
        const auto flag = [&out] (std::atomic<bool>& v)
        {
            Linked l;
            l.b = &v;
            out.push_back (l);
        };
        const auto choice = [&out] (std::atomic<int32_t>& v)
        {
            Linked l;
            l.i = &v;
            out.push_back (l);
        };
        const auto dynamics = [&] (ld::DynSlotParams& d)
        {
            flag (d.on);
            for (int n = 0; n < ld::kMaxDynParams; ++n)
                value (d.values[n], d.model.load());
        };
        if (has (gangs::Trim))
            value (p.input.trimDb);
        if (has (gangs::Filter))
        {
            flag (p.filter().lowCut);
            value (p.filter().lowCutFreq);
        }
        if (has (gangs::Gate))
            dynamics (p.gate());
        if (has (gangs::Peq))
        {
            flag (p.eq().on);
            for (int n = 0; n < ld::kMaxEqParams; ++n)
                value (p.eq().values[n], p.eq().model.load());
        }
        if (has (gangs::Compressor))
            dynamics (p.dynamics());
        if (has (gangs::Delay))
        {
            flag (p.input.delayOn);
            value (p.input.delay);
        }
        if (has (gangs::Fader))
            value (p.faderDb);
        if (has (gangs::Mute))
            flag (p.mute);
        if (has (gangs::Pan))
            value (p.pan);
        if (has (gangs::Routing))
        {
            flag (s.mains[0].on);
            for (int m = 0; m < kMixes + kFxSends; ++m)
            {
                auto& send = s.sends[(size_t) m];
                flag (send.enabled);
                value (send.levelDb);
                choice (send.mode);
                value (send.pan);
            }
        }
    }
} // namespace

namespace prefs
{
    bool get (juce::PropertiesFile& settings, const char* key, bool fallback) { return settings.getBoolValue (key, fallback); }
    void set (juce::PropertiesFile& settings, const char* key, bool value) { settings.setValue (key, value); }
} // namespace prefs

namespace soft
{
    juce::StringArray typeNames()
    {
        return { "Unassigned", "AMM Channel On/Off", "AMM In/Out", "Channel Mute", "Channel PAFL", "DAW Control", "DCA Mute", "DCA PAFL", "DCA Spill",
                 "MIDI Note On/Off", "MIDI Program Change", "Mix Select", "MMC", "Mute Group", "PAFL Clear", "Recall Scene", "Scene Next", "Scene Previous",
                 "Scene Recall Go", "Scene Store Current", "Select", "SQ Drive Multitrack", "SQ Drive Stereo Play", "SQ Drive Stereo Record", "Talk",
                 "Tap Tempo", "Global Tap Tempo" };
    }

    bool isModelled (int type) { return type != DawControl; }

    int option1Count (int type)
    {
        switch (type)
        {
            case MidiNote:
            case MidiProgram: return 16;
            case SceneGo:     return 2;
            case Select:      return 6;
            case TapTempo:    return 3;
            default:          return 0;
        }
    }

    int option2Count (int type, int numScenes)
    {
        switch (type)
        {
            case AmmChannel:  return 48;
            case AmmInOut:    return 2;
            case ChannelMute:
            case ChannelPafl: return kChannels;
            case DawControl:  return 2;
            case DcaMute:
            case DcaPafl:     return 8;
            case DcaSpill:    return 9;
            case MidiNote:
            case MidiProgram: return 128;
            case MixSelect:   return 1 + kMixes + kFxSends;
            case Mmc:         return 6;
            case MuteGroup:   return 8;
            case RecallScene: return juce::jmax (1, numScenes);
            case Select:      return 16;
            case DriveMultitrack: return 6;
            case DrivePlay:
            case DriveRecord: return 4;
            case TapTempo:    return 8;
            default:          return 0;
        }
    }

    juce::String option1Text (int type, int v)
    {
        switch (type)
        {
            case MidiNote:
            case MidiProgram: return "Channel " + juce::String (v + 1);
            case SceneGo:     return v == 0 ? "None" : "Auto Increment";
            case Select:      return "Layer " + juce::String::charToString ((juce::juce_wchar) ('A' + v));
            case TapTempo:    return juce::StringArray ({ "Left", "Right", "Left & Right" })[v];
            default:          return {};
        }
    }

    Slot channelAt (int i)
    {
        if (i < 48)
            return { Slot::Input, i };
        i -= 48;
        if (i < 8)
            return { Slot::FxReturn, i };
        i -= 8;
        if (i < kMixes)
            return { Slot::Mix, i };
        i -= kMixes;
        if (i < kFxSends)
            return { Slot::FxSend, i };
        i -= kFxSends;
        return i == 0 ? Slot { Slot::Main, 0 } : Slot { Slot::Matrix, i - 1 };
    }

    juce::String option2Text (int type, int v, MixerEngine& e)
    {
        switch (type)
        {
            case AmmChannel:  return "Ip" + juce::String (v + 1);
            case AmmInOut:    return "AMM " + juce::String (v + 1);
            case ChannelMute:
            case ChannelPafl:
            {
                auto* s = stripOf (e, channelAt (v));
                return s != nullptr ? s->getName() : juce::String();
            }
            case DawControl:  return v == 0 ? "Bank Up" : "Bank Down";
            case DcaMute:
            case DcaPafl:     return "DCA " + juce::String (v + 1);
            case DcaSpill:    return v == 0 ? juce::String ("Spill On Sel") : "DCA " + juce::String (v);
            case MidiNote:    return juce::MidiMessage::getMidiNoteName (v, true, true, 3);
            case MidiProgram: return juce::String (v);
            case MixSelect:   return mixName (e, v);
            case Mmc:         return juce::StringArray ({ "Rewind", "Play", "Pause", "Stop", "FFwd", "Record" })[v];
            case MuteGroup:   return "Mute Group " + juce::String (v + 1);
            case RecallScene: return "Scene " + juce::String (v + 1);
            case Select:      return "Strip " + juce::String (v + 1);
            // p.89's lists, in its order.
            case DriveMultitrack: return juce::StringArray ({ "Previous", "Stop", "Start/Pause", "Arm recording", "Next", "Arm/Rec/Stop" })[v];
            case DrivePlay:   return juce::StringArray ({ "Previous", "Stop", "Start/Pause", "Next" })[v];
            case DriveRecord: return juce::StringArray ({ "Stop", "Start/Pause", "Arm recording", "Arm/Rec/Stop" })[v];
            case TapTempo:    return "FX " + juce::String (v + 1);
            default:          return {};
        }
    }

    Assign get (juce::PropertiesFile& settings, int key)
    {
        const auto list = juce::StringArray::fromTokens (settings.getValue (settingsKey), ",", {});
        if (! juce::isPositiveAndBelow (key, list.size()))
        {
            // A new console's, as Reset Mix Settings: Standard has them (p.123).
            return key < 6 ? Assign { MuteGroup, 0, key } : key == 7 ? Assign { GlobalTap, 0, 0 } : Assign {};
        }
        const auto item = list[key];
        if (! item.containsChar (':'))
        {
            // The list of an earlier version of Sidecar.
            const int old = item.getIntValue();
            return old == 1 ? Assign { SceneNext, 0, 0 } : old == 2 ? Assign { ScenePrevious, 0, 0 } : old == 3 ? Assign { Talk, 0, 0 } : old >= 4 ? Assign { MuteGroup, 0, juce::jlimit (0, 7, old - 4) } : Assign {};
        }
        return assignOf (item);
    }

    void set (juce::PropertiesFile& settings, int key, const Assign& a)
    {
        juce::StringArray list;
        for (int k = 0; k < 8; ++k)
        {
            const auto v = k == key ? a : get (settings, k);
            list.add (juce::String (v.type) + ":" + juce::String (v.option1) + ":" + juce::String (v.option2));
        }
        settings.setValue (settingsKey, list.joinIntoString (","));
    }

    Assign getFootswitch (juce::PropertiesFile& settings, int which)
    {
        const auto list = juce::StringArray::fromTokens (settings.getValue (footswitchKey), ",", {});
        return juce::isPositiveAndBelow (which, list.size()) ? assignOf (list[which]) : Assign {};
    }

    void setFootswitch (juce::PropertiesFile& settings, int which, const Assign& a)
    {
        juce::StringArray list;
        for (int w = 0; w < 2; ++w)
        {
            const auto v = w == which ? a : getFootswitch (settings, w);
            list.add (juce::String (v.type) + ":" + juce::String (v.option1) + ":" + juce::String (v.option2));
        }
        settings.setValue (footswitchKey, list.joinIntoString (","));
    }
} // namespace soft

namespace gangs
{
    const char* attributeName (int attribute)
    {
        static const char* const names[] = { "Trim", "Filter", "Gate", "PEQ", "Compressor", "Delay", "Fader", "Mute", "Pan", "Routing", "Direct Out" };
        return names[juce::jlimit (0, kNumAttributes - 1, attribute)];
    }

    bool isModelled (int attribute) { return attribute != DirectOut; }

    int gangOf (const Set& set, int channel)
    {
        for (int g = 0; g < kGangs; ++g)
            for (int m : set[(size_t) g].members)
                if (m == channel)
                    return g;
        return -1;
    }

    Set load (juce::PropertiesFile& settings)
    {
        // A gang each, "attributes:member member ...", between semicolons.
        Set set;
        const auto list = juce::StringArray::fromTokens (settings.getValue (settingsKey), ";", {});
        for (int g = 0; g < kGangs && g < list.size(); ++g)
        {
            auto& gang = set[(size_t) g];
            gang.attributes = list[g].upToFirstOccurrenceOf (":", false, false).getIntValue() & ((1 << kNumAttributes) - 1);
            for (const auto& m : juce::StringArray::fromTokens (list[g].fromFirstOccurrenceOf (":", false, false), " ", {}))
                if (m.isNotEmpty() && (int) gang.members.size() < kMembers && juce::isPositiveAndBelow (m.getIntValue(), 48) && gangOf (set, m.getIntValue()) < 0)
                    gang.members.push_back (m.getIntValue());
        }
        return set;
    }

    void save (juce::PropertiesFile& settings, const Set& set)
    {
        juce::StringArray list;
        for (const auto& gang : set)
        {
            juce::StringArray members;
            for (int m : gang.members)
                members.add (juce::String (m));
            list.add (juce::String (gang.attributes) + ":" + members.joinIntoString (" "));
        }
        settings.setValue (settingsKey, list.joinIntoString (";"));
    }

    void Follower::follow (MixerEngine& e, const Set& set)
    {
        const auto& channels = e.getChannels();
        seen.resize ((size_t) kGangs);
        std::vector<std::vector<Linked>> values;
        for (int g = 0; g < kGangs; ++g)
        {
            const auto& gang = set[(size_t) g];
            auto& last = seen[(size_t) g];
            if (gang.members.size() < 2 || gang.attributes == 0)
            {
                last.clear();
                continue;
            }
            values.clear();
            for (int m : gang.members)
            {
                values.emplace_back();
                if (juce::isPositiveAndBelow (m, (int) channels.size()))
                    collectLinked (*channels[(size_t) m], gang.attributes, values.back());
            }
            const size_t count = values.front().size();
            bool alike = true, known = last.size() == values.size();
            for (size_t m = 0; m < values.size(); ++m)
            {
                alike = alike && values[m].size() == count;
                known = known && last[m].size() == values[m].size();
            }
            if (! alike)
            {
                last.clear();
                continue;
            }
            if (known)
            {
                // A value one member changed goes to the others. Several changed at once is
                // something that set them all (a scene, a show): each keeps what it got.
                for (size_t v = 0; v < count; ++v)
                {
                    size_t changed = 0;
                    int howMany = 0;
                    for (size_t m = 0; m < values.size(); ++m)
                        if (! juce::exactlyEqual (values[m][v].get(), last[m][v]))
                        {
                            changed = m;
                            ++howMany;
                        }
                    if (howMany != 1)
                        continue;
                    const float now = values[changed][v].get();
                    for (size_t o = 0; o < values.size(); ++o)
                        if (o != changed && values[o][v].model == values[changed][v].model)
                            values[o][v].set (now);
                }
            }
            last.resize (values.size());
            for (size_t m = 0; m < values.size(); ++m)
            {
                last[m].resize (count);
                for (size_t v = 0; v < count; ++v)
                    last[m][v] = values[m][v].get();
            }
        }
    }
} // namespace gangs

ld::SourceParams* Display::preampSource (Strip& s) const
{
    const int in = s.params.inputLeft.load();
    return in >= 0 && in < 128 ? ctx.engine.getSource (in) : nullptr;
}

//==============================================================================
Field& Display::addTab (juce::Rectangle<float> area, const juce::String& title, int& tab, int value)
{
    auto& f = add (Field::Tab, area, title);
    auto* t = &tab;
    f.lit = [t, value] { return *t == value; };
    f.press = [this, t, value]
    {
        *t = value;
        rebuild();
    };
    return f;
}

void Display::addSideTabs (const juce::StringArray& names, int& tab)
{
    const float top = (float) kTitleBar + 4.0f, h = ((float) kHeight - top - 4.0f) / (float) juce::jmax (6, names.size());
    for (int i = 0; i < names.size(); ++i)
        addTab ({ 2.0f, top + (float) i * h, kTabWidth, h - 3.0f }, names[i], tab, i);
}

//==============================================================================
// Routing

void Display::buildRoutingBank()
{
    const float cw = (float) kWidth / kColumns;
    const float top = (float) kTitleBar + 2.0f;
    for (int c = 0; c < kColumns; ++c)
    {
        const int at = bankFirst + c;
        if (at >= (int) layer.size())
            break;
        const auto slot = layer[(size_t) at];
        if (slot.kind == Slot::None)
            continue;
        const float x = (float) c * cw + 1.5f, w = cw - 3.0f;
        const auto choose = [this, slot] { if (onSelectSlot) onSelectSlot (slot); };
        add (Field::Area, { x, top, w, 22.0f }, {}).press = choose;
        if (slot.kind == Slot::Dca)
            continue;
        const auto open = [this, choose] (Block b)
        {
            return [this, choose, b]
            {
                choose();
                showBlock (b);
            };
        };
        add (Field::Area, { x, top + 24.0f, w, 22.0f }, {}).press = open (slot.kind == Slot::Input ? Block::DirOut : Block::MixSettings);
        add (Field::Area, { x, top + 50.0f, w, 66.0f }, {}).press = open (Block::DcaMute);
        add (Field::Area, { x, top + 120.0f, w, (float) kHeight - top - 120.0f }, {}).press = open (Block::Sends);
    }
}

void Display::paintRoutingBank (juce::Graphics& g)
{
    const float cw = (float) kWidth / kColumns;
    const float top = (float) kTitleBar + 2.0f, height = (float) kHeight - top;
    const auto sel = getSelectedSlot ? getSelectedSlot() : Slot {};
    for (int c = 0; c < kColumns; ++c)
    {
        const int at = bankFirst + c;
        if (at >= (int) layer.size())
            break;
        const auto slot = layer[(size_t) at];
        paintRoutingColumn (g, juce::Rectangle<float> ((float) c * cw + 1.5f, top, cw - 3.0f, height), slot, slot == sel && slot.kind != Slot::None);
    }
}

void Display::paintRoutingColumn (juce::Graphics& g, juce::Rectangle<float> col, const Slot& slot, bool isSelected)
{
    g.setColour (isSelected ? kColumnSelected : kColumn);
    g.fillRoundedRectangle (col, 3.0f);
    if (slot.kind == Slot::None)
        return;
    auto* s = stripOf (ctx.engine, slot);
    auto r = col;
    auto name = r.removeFromTop (22.0f);
    const auto colour = s != nullptr ? s->getColour() : consoleColour (ctx.engine.getDca (slot.index).colour.load());
    g.setColour (colour);
    g.fillRect (name.reduced (1.0f));
    const auto label = s != nullptr ? s->getName() : ctx.engine.getDcaName (slot.index).isNotEmpty() ? ctx.engine.getDcaName (slot.index) : "DCA" + juce::String (slot.index + 1);
    text (g, name, label, 12.0f, inkOn (colour), juce::Justification::centred, true);
    if (s == nullptr)
        return;
    // Dir Out, or the mix's type, its button's marker at the right (p.45).
    auto button = r.removeFromTop (24.0f).reduced (6.0f, 3.0f);
    g.setColour (juce::Colour (0xff24598a));
    g.fillRoundedRectangle (button, 3.0f);
    g.setColour (kLine);
    g.drawRoundedRectangle (button, 3.0f, 0.8f);
    auto mark = button.removeFromRight (button.getHeight()).reduced (3.5f);
    g.setColour (juce::Colours::white);
    g.drawEllipse (mark, 1.2f);
    text (g, button, slot.kind == Slot::Input ? "Dir Out" : slot.kind == Slot::Mix ? "Aux Snd" : slot.kind == Slot::FxSend ? "FX Snd" : "Main Snd", 10.5f, juce::Colours::white, juce::Justification::centred, true);

    // DCA and mute group assignments, the numbers filled when assigned (p.45).
    const auto group = [&] (const char* title, ld::GroupMask& mask)
    {
        text (g, r.removeFromTop (12.0f), title, 9.5f, juce::Colours::white, juce::Justification::centred, true);
        auto grid = r.removeFromTop (20.0f).reduced (5.0f, 1.0f);
        const float w = grid.getWidth() / 4.0f, h = grid.getHeight() * 0.5f;
        for (int i = 0; i < 8; ++i)
        {
            juce::Rectangle<float> cell (grid.getX() + (float) (i % 4) * w, grid.getY() + (float) (i / 4) * h, w, h);
            const bool on = mask.contains (i);
            g.setColour (on ? juce::Colours::white : juce::Colour (0xff24598a));
            g.fillRect (cell.reduced (0.6f));
            if (on)
                text (g, cell, juce::String (i + 1), 8.5f, juce::Colours::black, juce::Justification::centred, true);
        }
    };
    group ("DCA Assign", s->params.dcaMask);
    group ("Mute Assign", s->params.muteGroupMask);

    if (s->isOutput() && slot.kind != Slot::Mix)
        return;
    // Main, Mix and FX sends (only the channels send to them).
    const bool sends = s->isInput();
    text (g, r.removeFromTop (12.0f), "Main Sends", 9.5f, juce::Colours::white, juce::Justification::centred, true);
    {
        auto bar = r.removeFromTop (32.0f).reduced (24.0f, 1.0f);
        g.setColour (kWell);
        g.fillRect (bar);
        const auto& lr = s->mains[0];
        sendBar (g, bar.reduced (2.0f, 1.0f), s->params.faderDb.load(), lr.on.load(), lr.pre.load());
        text (g, { bar.getX(), bar.getBottom() - 11.0f, bar.getWidth(), 10.0f }, "LR", 8.5f, juce::Colours::white);
    }
    if (! sends)
        return;
    const auto sendRow = [&] (const char* title, int first, int count)
    {
        text (g, r.removeFromTop (12.0f), title, 9.5f, juce::Colours::white, juce::Justification::centred, true);
        auto bars = r.removeFromTop (70.0f).reduced (5.0f, 1.0f);
        g.setColour (kWell);
        g.fillRect (bars);
        const float w = bars.getWidth() / (float) count;
        for (int i = 0; i < count; ++i)
        {
            auto& send = s->sends[(size_t) (first + i)];
            sendBar (g, juce::Rectangle<float> (bars.getX() + (float) i * w + 0.5f, bars.getY() + 1.0f, w - 1.0f, bars.getHeight() - 2.0f),
                     send.levelDb.load(), send.enabled.load(), send.mode.load() == (int) SendMode::Pre);
        }
    };
    sendRow ("Mix Sends", 0, kMixes);
    sendRow ("FX Sends", kMixes, kFxSends);
}

void Display::buildDirOut (Strip& s)
{
    // As p.46: the channel's own level in a box at the left, the global settings in a
    // panel, all Sidecar's fixed (no direct out modelled yet).
    juce::ignoreUnused (s);
    add (Field::Header, { 120, 164, 90, 18 }, "Direct Out");
    add (Field::Value, { 124, 186, 82, 48 }, "Level").enabled = false;
    add (Field::Header, { 230, 56, 416, 20 }, "Global Direct Out Settings");
    add (Field::Button, { 312, 106, 136, 32 }, "Post Compressor").enabled = false;
    for (auto [title, x, y] : { std::tuple ("Follow Fader", 288, 186), std::tuple ("Follow Mute", 413, 186), std::tuple ("Follow DCA Fader", 288, 252),
                                std::tuple ("Follow DCA Mute", 413, 252), std::tuple ("Follow Mute Group", 413, 318) })
    {
        auto& f = add (Field::Button, { (float) x, (float) y, 60, 26 }, title);
        f.enabled = false;
        f.caption = "Off";
    }
}

void Display::buildMixSettings (Strip& s)
{
    const auto slot = getSelectedSlot ? getSelectedSlot() : Slot {};
    const int bus = slot.kind == Slot::Mix ? slot.index : slot.kind == Slot::FxSend ? kMixes + slot.index : -1;
    add (Field::Header, { 230, 56, 304, 20 }, slot.kind == Slot::Main ? "Main Settings" : slot.kind == Slot::Matrix ? "Matrix Settings" : typeOf (slot) + " Send Settings");
    addValue ({ 120, 178, 92, 48 }, "Output Bal", bindPan ("Output Bal", s.params.pan));
    if (bus < 0)
        return;
    add (Field::Button, { 276, 96, 210, 30 }, "Post Compressor").enabled = false; // the channel source point
    const auto allOf = [this, bus] (std::function<void (ld::SendParams&)> action)
    {
        return [this, bus, action]
        {
            for (const auto* list : { &ctx.engine.getChannels(), &ctx.engine.getAux() })
                for (const auto& c : *list)
                    action (c->sends[(size_t) bus]);
        };
    };
    add (Field::Button, { 276, 160, 84, 34 }, "All On").press = allOf ([] (ld::SendParams& p) { p.enabled = true; });
    add (Field::Button, { 402, 160, 84, 34 }, "All Off").press = allOf ([] (ld::SendParams& p) { p.enabled = false; });
    add (Field::Button, { 276, 214, 84, 34 }, "All Pre").press = allOf ([] (ld::SendParams& p) { p.mode = (int) SendMode::Pre; });
    add (Field::Button, { 402, 214, 84, 34 }, "All Post").press = allOf ([] (ld::SendParams& p) { p.mode = (int) SendMode::Post; });
}

void Display::buildDcaMute (Strip& s)
{
    // As p.49: a column per DCA and mute group, its name, Assign, then View.
    const float x0 = 88.0f, cw = 590.0f / 8.0f;
    const auto row = [&] (ld::GroupMask& mask, float y, const juce::String& prefix, const juce::String& name)
    {
        for (int i = 0; i < 8; ++i)
        {
            const float x = x0 + (float) i * cw;
            auto* m = &mask;
            add (Field::Header, { x + 2, y, cw - 4, 20 }, name + juce::String (i + 1));
            auto& f = add (Field::Button, { x + 3, y + 28, cw - 6, 40 }, prefix + juce::String (i + 1) + " Assign");
            f.caption = "Assign";
            f.colour = juce::Colours::white;
            f.lit = [m, i] { return m->contains (i); };
            f.press = [m, i] { m->set (i, ! m->contains (i)); };
            auto& view = add (Field::Button, { x + 3, y + 76, cw - 6, 32 }, prefix + juce::String (i + 1) + " View");
            view.caption = "View";
            view.enabled = false;
        }
    };
    row (s.params.dcaMask, 50.0f, "DCA", "DCA");
    add (Field::Header, { x0, 184.0f, 590.0f, 20.0f }, "Mute Groups");
    row (s.params.muteGroupMask, 210.0f, "MGrp", "MGrp");
}

void Display::buildSends (Strip& s)
{
    // The selected channel's send to every mix (p.50): drag left or right for all of them.
    const float x0 = 92.0f, cw = 544.0f / 7.0f, top = 40.0f;
    const int total = 1 + kMixes + kFxSends;
    sendFirst = juce::jlimit (0, juce::jmax (0, total - 7), sendFirst);
    for (int c = 0; c < 7; ++c)
    {
        const int mix = sendFirst + c;
        if (mix >= total)
            break;
        const float x = x0 + (float) c * cw;
        add (Field::Header, { x + 1, top, cw - 2, 20 }, mix == 0 ? juce::String ("MainLR") : mixName (ctx.engine, mix));
        if (! s.isInput())
            continue;
        if (mix == 0)
        {
            auto* send = &s.mains[0];
            auto& pre = add (Field::Button, { x + 8, top + 44, cw - 16, 32 }, "Post");
            pre.enabled = false;
            auto& on = add (Field::Button, { x + 8, top + 110, cw - 16, 34 }, "On");
            on.captionOff = "Off";
            on.colour = juce::Colours::white;
            on.lit = [send] { return send->on.load(); };
            on.press = [send] { send->on = ! send->on.load(); };
            addValue ({ x + 6, top + 240, cw - 12, 50 }, "Level", bindFader ("Level", s.params.faderDb));
            continue;
        }
        auto* send = &s.sends[(size_t) (mix - 1)];
        if (mix <= kMixes && isGroupMix (mix - 1))
        {
            // A Group is an assignment: the channel's own fader and pan feed it (p.45).
            auto& on = add (Field::Button, { x + 8, top + 110, cw - 16, 34 }, "On");
            on.captionOff = "Off";
            on.colour = juce::Colours::white;
            on.lit = [send] { return send->enabled.load(); };
            on.press = [send] { send->enabled = ! send->enabled.load(); };
            continue;
        }
        auto& pre = add (Field::Button, { x + 8, top + 44, cw - 16, 32 }, send->mode.load() == (int) SendMode::Pre ? "Pre" : "Post");
        pre.colour = kGreenBar;
        pre.lit = [send] { return send->mode.load() == (int) SendMode::Pre; };
        pre.press = [this, send]
        {
            send->mode = send->mode.load() == (int) SendMode::Pre ? (int) SendMode::Post : (int) SendMode::Pre;
            rebuild();
        };
        auto& on = add (Field::Button, { x + 8, top + 110, cw - 16, 34 }, "On");
        on.captionOff = "Off";
        on.colour = juce::Colours::white;
        on.lit = [send] { return send->enabled.load(); };
        on.press = [send] { send->enabled = ! send->enabled.load(); };
        addValue ({ x + 6, top + 174, cw - 12, 50 }, "Pan", bindPan ("Pan", send->pan));
        addValue ({ x + 6, top + 240, cw - 12, 50 }, "Level", bindFader ("Level", send->levelDb));
    }
}

//==============================================================================
// Meters

void Display::buildMeters()
{
    addSideTabs ({ "Input Meters", "FX Meters", "Mix Meters", "USB Meters", "RTA", "Channel Meters" }, meterTab);
    for (int i = 3; i < 6; ++i)
        fields[(size_t) i].enabled = false; // USB, RTA and the chromatic thresholds: not yet
    // The meter source point, for Input and Mix (p.53): Post Compressor is what's drawn.
    if (meterTab == 0 || meterTab == 2)
    {
        const auto body = bodyArea();
        add (Field::Button, { body.getX() + 64, body.getY(), 150, 24 }, meterTab == 0 ? "Post Compressor" : "Post Delay/Fader").enabled = false;
    }
}

void Display::paintMeters (juce::Graphics& g)
{
    auto body = bodyArea();
    if (meterTab == 0 || meterTab == 2)
        text (g, body.removeFromTop (28.0f).removeFromLeft (62.0f), meterTab == 0 ? "Input Meter Source" : "Mix Meter Source", 8.5f, juce::Colours::white, juce::Justification::centredLeft, true);
    const std::vector<Strip*> list = [this]
    {
        std::vector<Strip*> v;
        const auto add_ = [&v] (const MixerEngine::StripList& l, int first, int count) { for (int i = first; i < first + count && i < (int) l.size(); ++i) v.push_back (l[(size_t) i].get()); };
        if (meterTab == 0)
            add_ (ctx.engine.getChannels(), 0, 48);
        else if (meterTab == 1)
        {
            // FX sends, then the FX returns (p.53).
            add_ (ctx.engine.getBuses(), kMixes, kFxSends);
            add_ (ctx.engine.getAux(), 0, 8);
        }
        else
        {
            add_ (ctx.engine.getBuses(), 0, kMixes);
            add_ (ctx.engine.getMains(), 0, 1);
            add_ (ctx.engine.getMatrices(), 0, 3);
        }
        return v;
    }();
    // 16 meters a row (p.52), two rows at once; the rest by dragging up and down.
    const int perRow = 16, shown = meterTab == 0 ? 2 : 1;
    const int total = ((int) list.size() + perRow - 1) / perRow;
    meterFirst = juce::jlimit (0, juce::jmax (0, total - shown), meterFirst);
    const float rowH = body.getHeight() / (float) shown;
    const float scale = 18.0f, cw = (body.getWidth() - 2.0f * scale) / (float) perRow;
    for (int row = 0; row < shown; ++row)
    {
        auto band = body.withY (body.getY() + (float) row * rowH).withHeight (rowH);
        // The scale, both sides: 10, 0, -10 ... -40 (p.52).
        for (float side : { band.getX(), band.getRight() - scale })
            for (int db = 10; db >= -40; db -= 10)
            {
                const float y = band.getBottom() - 40.0f - (band.getHeight() - 44.0f) * (float) (db + 40) / 50.0f;
                text (g, { side, y - 5.0f, scale, 10.0f }, db > 0 ? "+" + juce::String (db) : juce::String (db), 8.0f, juce::Colours::white.withAlpha (0.75f), juce::Justification::centred);
            }
    }
    for (int i = perRow * meterFirst; i < (int) list.size(); ++i)
    {
        auto* s = list[(size_t) i];
        const int col = i % perRow, row = i / perRow - meterFirst;
        if (row >= shown)
            break;
        juce::Rectangle<float> cell (body.getX() + scale + (float) col * cw, body.getY() + (float) row * rowH, cw, rowH);
        auto r = cell.reduced (1.5f, 3.0f);
        g.setColour (kWell);
        g.fillRect (r);
        auto label = r.removeFromBottom (32.0f);
        auto bars = r.reduced (2.0f, 2.0f);
        const float w = bars.getWidth() * 0.33f;
        meterBar (g, bars.withWidth (w), ctx.meters.get (s, meterTab == 0 ? MeterPoint::Trim : MeterPoint::Output, 0));
        // Gain reduction from the top, red while the compressor's in, grey-white while not;
        // the gate's dot likewise (p.52).
        auto gr = bars.withX (bars.getX() + w + 1.0f).withWidth (w * 0.8f);
        const bool compIn = s->getKind() != StripKind::Aux && s->params.dynamics().on.load();
        g.setColour (compIn ? juce::Colour (0xffff3b30) : juce::Colours::white.withAlpha (0.45f));
        g.fillRect (gr.withHeight (gr.getHeight() * juce::jlimit (0.0f, 1.0f, s->meters.dynReductionDb.load() / 30.0f)));
        if (s->getKind() == StripKind::Channel && s->meters.gateActive.load())
        {
            g.setColour (s->params.gate().on.load() ? juce::Colour (0xffff3b30) : juce::Colours::white.withAlpha (0.6f));
            g.fillEllipse (bars.getRight() - 4.0f, bars.getY(), 4.0f, 4.0f);
        }
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, label.getCentreX(), label.getCentreY()));
        const auto rotated = juce::Rectangle<float> (label.getCentreX() - label.getHeight() * 0.5f, label.getCentreY() - label.getWidth() * 0.5f, label.getHeight(), label.getWidth());
        text (g, rotated, s->getName(), 9.0f, juce::Colours::white, juce::Justification::centredLeft);
    }
}

//==============================================================================
// FX

void Display::pressLibrary()
{
    if (hasLibrary())
    {
        libraryOpen = ! libraryOpen;
        rebuild();
    }
}

bool Display::hasLibrary() const noexcept
{
    if (screen == Screen::Fx)
        return true;
    if (screen != Screen::Processing)
        return false;
    if (block == Block::Comp || block == Block::Geq)
        return true;
    auto* strip = selectedStrip();
    return block == Block::Preamp && strip != nullptr && ld::ld_strip_preamp_block (strip->getHandle()) >= 0;
}

void Display::stepSceneSelection (int delta)
{
    sceneSelected = juce::jlimit (0, juce::jmax (0, ctx.show.getNumScenes() - 1), sceneSelected + delta);
    rebuild();
}

void Display::recallSelectedScene (bool autoIncrementAfter)
{
    if (ctx.show.getNumScenes() == 0)
        return;
    ctx.show.recallScene (sceneSelected);
    if (autoIncrementAfter)
        sceneSelected = juce::jmin (sceneSelected + 1, juce::jmax (0, ctx.show.getNumScenes() - 1));
    rebuild();
}

void Display::buildFx()
{
    auto& fx = ctx.engine.getFx();
    const auto& models_ = models::table (models::Fx);
    const float top = (float) kTitleBar + 4.0f, h = ((float) kHeight - top - 4.0f) / 8.0f;
    for (int i = 0; i < 8; ++i)
    {
        const auto& m = models_[(size_t) juce::jlimit (0, (int) models_.size() - 1, fx.getModel (i))];
        addTab ({ 2.0f, top + (float) i * h, kTabWidth, h - 3.0f }, m.name.isNotEmpty() && fx.getModel (i) != 0 ? m.name : "Empty", fxSlot, i);
    }

    if (libraryOpen)
    {
        // The FX Library's list: the SQ's RackFX (a unit type, by recalling a preset of it).
        add (Field::Header, { 92, 40, 540, 18 }, "FX Library");
        float x = 96.0f, y = 66.0f;
        for (const auto& m : models_)
        {
            if (models::consoleOf (m.token) != "SQ-5" || m.params.empty())
                continue;
            auto& f = add (Field::Button, { x, y, 170, 40 }, m.name);
            const int index = m.index;
            f.lit = [this, index, &fx] { return fx.getModel (fxSlot) == index; };
            f.press = [this, index, &fx]
            {
                fx.setModel (fxSlot, index);
                libraryOpen = false;
                rebuild();
            };
            x += 178.0f;
            if (x > 500.0f)
            {
                x = 96.0f;
                y += 48.0f;
            }
        }
        return;
    }

    const int model = fx.getModel (fxSlot);
    const auto& m = models_[(size_t) juce::jlimit (0, (int) models_.size() - 1, model)];
    add (Field::Header, { 88, 40, 200, 20 }, "FX Unit " + juce::String (fxSlot + 1) + (model != 0 ? ": " + m.name : juce::String()));
    // Global Tap Tempo, Front Panel and Back Panel (p.60): not modelled.
    add (Field::Button, { 420, 38, 58, 26 }, "Tap").enabled = false;
    add (Field::Button, { 552, 38, 60, 26 }, "Front Panel").enabled = false;
    add (Field::Button, { 614, 38, 60, 26 }, "Back Panel").enabled = false;
    auto& params = fx.params (fxSlot);
    const float cellW = 88.0f, cellH = 54.0f;
    // The bigger units' controls in pages of 18 (the Page key of Expert mode, p.61).
    const int total = (int) m.params.size(), pages = (total + 17) / 18;
    fxPage = juce::jlimit (0, juce::jmax (0, pages - 1), fxPage);
    if (pages > 1)
    {
        auto& next = add (Field::Button, { 484, 38, 62, 26 }, "Page");
        next.caption = "Page " + juce::String (fxPage + 1) + "/" + juce::String (pages);
        next.press = [this, pages]
        {
            fxPage = (fxPage + 1) % pages;
            rebuild();
        };
    }
    for (int at = fxPage * 18; at < juce::jmin (total, (fxPage + 1) * 18); ++at)
    {
        const auto& p = m.params[(size_t) at];
        auto binding = bindModelParam (params.values[p.index], p);
        const int i = at - fxPage * 18;
        const float x = 96.0f + (float) (i % 6) * (cellW + 2.0f), y = 66.0f + (float) (i / 6) * (cellH + 2.0f);
        addValue ({ x, y, cellW, cellH }, p.label, std::move (binding));
    }

    // The return's level and mute, with the FX send's (p.60); the PEQ on its return.
    const auto& returns = ctx.engine.getAux();
    const auto& buses = ctx.engine.getBuses();
    if (juce::isPositiveAndBelow (fxSlot, (int) returns.size()))
    {
        auto* r = returns[(size_t) fxSlot].get();
        if (fxSlot < kFxSends && kMixes + fxSlot < (int) buses.size())
        {
            auto* send = buses[(size_t) (kMixes + fxSlot)].get();
            add (Field::Header, { 92, 318, 230, 16 }, send->getName());
            add (Field::Button, { 100, 342, 44, 36 }, "PAFL").enabled = false;
            addToggle (Field::Button, { 190, 344, 60, 32 }, "Send Mute", send->params.mute).colour = juce::Colours::red;
            fields.back().caption = "Mute";
            addValue ({ 262, 338, 62, 46 }, "Send Level", bindFader ("Send Level", send->params.faderDb)).caption = "Level";
        }
        add (Field::Header, { 342, 318, 84, 16 }, "FX Unit Output");
        auto& peq = add (Field::Button, { 346, 344, 76, 34 }, "PEQ");
        peq.press = [this, r]
        {
            if (ctx.selectStrip)
                ctx.selectStrip (r);
            showBlock (Block::Peq);
        };
        add (Field::Header, { 436, 318, 232, 16 }, r->getName());
        add (Field::Button, { 446, 342, 44, 36 }, "PAFL").enabled = false;
        addToggle (Field::Button, { 534, 344, 64, 32 }, "Return Mute", r->params.mute).colour = juce::Colours::red;
        fields.back().caption = "Mute";
        addValue ({ 606, 338, 62, 46 }, "Return Level", bindFader ("Return Level", r->params.faderDb)).caption = "Level";
    }
}

//==============================================================================
// Scenes

void Display::buildScenes()
{
    addSideTabs ({ "Scene Manager", "Cue List Editor", "Global Filter", "Safes" }, sceneTab);
    fields[1].enabled = false; // the Cue List Editor and Global Filter: not yet
    fields[2].enabled = false;
    if (sceneTab == 3)
    {
        buildSafes();
        return;
    }

    // All Scenes as p.67: All Scenes / Cue List over the list; Recall and Modify at its
    // right, with the name field over them.
    auto& show = ctx.show;
    const int count = show.getNumScenes();
    const auto body = bodyArea();
    const auto list = body.withTrimmedTop (32.0f).withWidth (284.0f);
    sceneSelected = juce::jlimit (0, juce::jmax (0, count - 1), sceneSelected);
    const int visible = (int) ((list.getHeight() - 4.0f) / 30.0f);
    sceneFirst = juce::jlimit (0, juce::jmax (0, count - visible), sceneFirst);
    addTab ({ body.getX() + 34, body.getY(), 106, 26 }, "All Scenes", sceneTab, 0);
    auto& cue = add (Field::Tab, { body.getX() + 142, body.getY(), 106, 26 }, "Cue List");
    cue.enabled = false;
    for (int i = 0; i < visible && sceneFirst + i < count; ++i)
    {
        const int at = sceneFirst + i;
        auto& f = add (Field::Area, { list.getX(), list.getY() + 2.0f + (float) i * 30.0f, list.getWidth(), 28.0f }, {});
        f.press = [this, at]
        {
            sceneSelected = at;
            rebuild();
        };
    }
    const float x = body.getX() + 300.0f, w = body.getWidth() - 300.0f;
    {
        // The scene name field, across the top (p.67).
        const auto scene = show.getScene (sceneSelected);
        add (Field::Button, { x, body.getY(), w, 26 }, scene.has_value() ? scene->name : juce::String ("No Scene")).enabled = false;
    }
    add (Field::Header, { x, body.getY() + 40, w, 18 }, "Recall");
    auto& go = add (Field::Button, { x + 14, body.getY() + 66, 88, 34 }, "Go");
    go.enabled = count > 0;
    go.press = [this, &show]
    {
        show.recallScene (sceneSelected);
        if (autoIncrement)
            sceneSelected = juce::jmin (sceneSelected + 1, juce::jmax (0, show.getNumScenes() - 1));
        rebuild();
    };
    auto& inc = add (Field::Button, { x + 170, body.getY() + 66, 110, 34 }, "Auto Increment");
    inc.lit = [this] { return autoIncrement; };
    inc.press = [this] { autoIncrement = ! autoIncrement; };
    add (Field::Button, { x + 14, body.getY() + 116, 88, 34 }, "Recall Filter").enabled = false;
    add (Field::Value, { x + 186, body.getY() + 108, 80, 44 }, "Crossfade Time").enabled = false;
    add (Field::Header, { x, body.getY() + 176, w, 18 }, "Modify");
    auto& store = add (Field::Button, { x + 14, body.getY() + 208, 80, 34 }, "Store");
    store.enabled = count > 0;
    store.press = [this, &show] { show.storeScene (sceneSelected); };
    auto& addNew = add (Field::Button, { x + 108, body.getY() + 208, 80, 34 }, "Add New");
    addNew.press = [this, &show]
    {
        sceneSelected = show.addScene ("Scene " + juce::String (show.getNumScenes() + 1));
        rebuild();
    };
    auto& del = add (Field::Button, { x + 202, body.getY() + 208, 80, 34 }, "Delete");
    del.enabled = count > 0;
    del.press = [this, &show]
    {
        show.removeScene (sceneSelected);
        rebuild();
    };
}

void Display::paintScenes (juce::Graphics& g)
{
    if (sceneTab != 0)
        return;
    auto& show = ctx.show;
    const auto body = bodyArea();
    const auto list = body.withTrimmedTop (32.0f).withWidth (284.0f);
    const int count = show.getNumScenes(), current = show.getCurrentSceneIndex();
    const int visible = (int) ((list.getHeight() - 4.0f) / 30.0f);
    g.setColour (kWell.withAlpha (0.55f));
    g.fillRoundedRectangle (list.expanded (2.0f, 0.0f), 3.0f);
    for (int i = 0; i < visible && sceneFirst + i < count; ++i)
    {
        const int at = sceneFirst + i;
        juce::Rectangle<float> row (list.getX(), list.getY() + 2.0f + (float) i * 30.0f, list.getWidth(), 28.0f);
        const auto colour = at == sceneSelected ? kNext : at == current ? kCurrent.withAlpha (0.7f) : juce::Colour (0xff102a40);
        g.setColour (colour);
        g.fillRoundedRectangle (row, 3.0f);
        const auto scene = show.getScene (at);
        text (g, row.reduced (8.0f, 0.0f), juce::String (at + 1) + ")  " + (scene.has_value() ? scene->name : juce::String()), 12.0f,
              at == sceneSelected ? juce::Colours::black : juce::Colours::white, juce::Justification::centredLeft, true);
    }
    if (count == 0)
        text (g, list.withHeight (40.0f).translated (0.0f, 40.0f), "No scenes: touch Add New", 13.0f, juce::Colours::white);
}

void Display::buildSafes()
{
    // Input, FX and Mix safes (p.78): a grid of Safe keys, per channel.
    const auto body = bodyArea();
    addTab ({ body.getX(), body.getY(), 180, 22 }, "Inputs", safesTab, 0);
    addTab ({ body.getX() + 184, body.getY(), 180, 22 }, "FX", safesTab, 1);
    addTab ({ body.getX() + 368, body.getY(), 180, 22 }, "Mix", safesTab, 2);
    std::vector<Strip*> list;
    const auto append = [&list] (const MixerEngine::StripList& l) { for (const auto& s : l) list.push_back (s.get()); };
    if (safesTab == 0)
        append (ctx.engine.getChannels());
    else if (safesTab == 1)
        append (ctx.engine.getAux());
    else
    {
        append (ctx.engine.getBuses());
        append (ctx.engine.getMains());
        append (ctx.engine.getMatrices());
    }
    const int cols = 8, rows = juce::jmax (6, ((int) list.size() + cols - 1) / cols);
    const float cw = body.getWidth() / (float) cols, ch = (body.getHeight() - 28.0f) / (float) rows;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        juce::Rectangle<float> cell (body.getX() + (float) (i % cols) * cw + 1.0f, body.getY() + 28.0f + (float) (i / cols) * ch + 1.0f, cw - 2.0f, ch - 2.0f);
        auto& f = addToggle (Field::Button, cell, list[(size_t) i]->getName() + " Safe", list[(size_t) i]->params.recallSafe);
        f.colour = juce::Colours::white;
    }
}

//==============================================================================
// Setup

void Display::addFlag (juce::Rectangle<float> label, juce::Rectangle<float> key, const juce::String& text_, bool enabled, std::function<bool()> get, std::function<void (bool)> set)
{
    add (Field::Label, label, {}).caption = text_;
    auto& f = add (Field::Button, key, text_ + " Key");
    f.caption = "On";
    f.captionOff = "Off";
    f.colour = juce::Colours::white;
    f.enabled = enabled;
    f.lit = get;
    f.press = [this, get, set]
    {
        set (! get());
        rebuild();
    };
}

void Display::addMixGrid (juce::Rectangle<float> area, std::function<bool (int)> get, std::function<void (int, bool)> set)
{
    // As pp.96-97: the mixes four to a row, Groups first with All after them, then the
    // Auxes; below them MainLR and the matrices. Cells: 0-11 the mixes, 12 LR, 13-15 Mtx.
    const float cw = area.getWidth() / 4.0f, ch = area.getHeight() / 5.0f;
    const auto cell = [&] (int at, const juce::String& name, int index)
    {
        const juce::Rectangle<float> r (area.getX() + (float) (at % 4) * cw + 1.0f, area.getY() + (float) (at / 4) * ch + 1.0f, cw - 2.0f, ch - 2.0f);
        add (Field::Header, r.withHeight (15.0f), name);
        auto& f = add (Field::Button, r.withTrimmedTop (17.0f).reduced (2.0f, 1.0f), name + " Assign");
        f.caption = "On";
        f.captionOff = "Off";
        f.colour = juce::Colours::white;
        if (index < 0)
        {
            const auto all = [get] { for (int m = 0; m < kMixes; ++m) if (! get (m)) return false; return true; };
            f.lit = all;
            f.press = [set, all] { const bool on = ! all(); for (int m = 0; m < kMixes; ++m) set (m, on); };
        }
        else
        {
            f.lit = [get, index] { return get (index); };
            f.press = [get, set, index] { set (index, ! get (index)); };
        }
    };
    const int groups = groupCount(), auxes = kMixes - groups;
    int at = 0;
    for (int g = 0; g < groups; ++g)
        cell (at++, "Grp" + juce::String (g + 1), auxes + g);
    cell (at++, "All", -1);
    for (int a = 0; a < auxes; ++a)
        cell (at++, "Aux" + juce::String (a + 1), a);
    cell (16, "MainLR", 12);
    for (int m = 0; m < 3; ++m)
        cell (17 + m, "Mtx" + juce::String (m + 1), 13 + m);
}

void Display::buildSetup()
{
    addSideTabs ({ "Audio", "Surface", "Mixer Config", "Ganging", "Network Setup", "Users" }, setupTab);

    switch (setupTab)
    {
        case 0:  buildAudioSetup(); break;
        case 2:  buildMixerConfig(); break;
        case 3:  buildGanging(); break;
        case 4:  buildNetwork(); break;
        case 5:  buildUsers(); break;
        default: buildSurfaceSetup(); break;
    }
}

void Display::buildAudioSetup()
{
    const auto body = bodyArea();
    const float tabW = (body.getWidth() - 16.0f) / 5.0f;
    {
        const char* const names[] = { "PAFL", "Talkback", "SigGen", "Digital I/O", "I/O Port" };
        for (int t = 0; t < 5; ++t)
            addTab ({ body.getX() + (float) t * (tabW + 4.0f), body.getY(), tabW, 24 }, names[t], audioTab, t);
    }
    auto& e = ctx.engine;
    const float top = body.getY() + 30.0f, right = body.getRight();
    if (audioTab == 0)
    {
        // As p.94: the options at the left, Ext In above the source, trim, delay and level.
        const auto pref = [this] (const char* key, bool fallback)
        {
            return std::pair<std::function<bool()>, std::function<void (bool)>> { [this, key, fallback] { return prefs::get (ctx.settings, key, fallback); },
                                                                                  [this, key] (bool v) { prefs::set (ctx.settings, key, v); } };
        };
        const float lx = body.getX() + 2.0f, kx = lx + 124.0f;
        const auto row = [&] (int i) { return top + 2.0f + (float) i * 34.0f; };
        const auto option = [&] (int i, const juce::String& name, const char* key, bool fallback)
        {
            const auto [get, set] = pref (key, fallback);
            addFlag ({ lx, row (i), 120, 28 }, { kx, row (i) + 2.0f, 54, 24 }, name, true, get, set);
        };
        option (0, "Additive", prefs::additive, false);
        option (1, "Sel Follow PAFL", prefs::selFollow, true);
        option (2, "PAFL Follow Mix", prefs::followMix, true);
        option (3, "Input Override Mix", prefs::overrideMix, true);
        addFlag ({ lx, row (4), 120, 28 }, { kx, row (4) + 2.0f, 54, 24 }, "LR to PAFL", true,
                 [&e] { return e.speakers.sourceLevelDb.load() > -100.0f; },
                 [&e] (bool on) { const float db = on ? 0.0f : -144.0f; e.speakers.sourceLevelDb = db; e.phones.source.sourceLevelDb = db; });
        addFlag ({ lx, row (6), 120, 28 }, { kx, row (6) + 2.0f, 54, 24 }, "Output AFL", true,
                 [&e] { return e.isSoloAfl (StripKind::Bus); },
                 [&e] (bool on) { for (auto k : { StripKind::Bus, StripKind::Main, StripKind::Matrix }) e.setSoloAfl (k, on); });
        addFlag ({ lx, row (7), 120, 28 }, { kx, row (7) + 2.0f, 54, 24 }, "Input AFL", true,
                 [&e] { return e.isSoloAfl (StripKind::Channel); },
                 [&e] (bool on) { e.setSoloAfl (StripKind::Channel, on); });

        const float px = kx + 72.0f, pw = right - px;
        add (Field::Header, { px, top, pw * 0.5f - 2.0f, 18 }, "Ext In Source Select");
        add (Field::Header, { px + pw * 0.5f + 2.0f, top, pw * 0.5f - 2.0f, 18 }, "Ext In Preamp/Trim");
        add (Field::Button, { px + 30, top + 30, 130, 28 }, "Ext In to PAFL").enabled = false;
        add (Field::Button, { px + 30, top + 72, 130, 28 }, "Unassigned").enabled = false;
        add (Field::Value, { px + pw * 0.5f + 40, top + 30, 74, 44 }, "Gain").enabled = false;
        add (Field::Value, { px + pw * 0.5f + 40, top + 84, 74, 44 }, "Trim").enabled = false;

        const float y = body.getBottom() - 56.0f, w = pw / 4.0f;
        add (Field::Value, { px, y, w * 1.4f - 2.0f, 48 }, "Input to PAFL Source").enabled = false; // the engine's PFL point is fixed
        KnobBinding trim;
        trim.label = "PFL Trim";
        trim.get = [&e] { return (24.0f - e.speakers.pflDimDb.load()) / 24.0f; };
        trim.set = [&e] (float v) { const float dim = 24.0f * (1.0f - v); e.speakers.pflDimDb = dim; e.phones.source.pflDimDb = dim; };
        trim.text = [&e] { return juce::String (-e.speakers.pflDimDb.load(), 1) + " dB"; };
        addValue ({ px + w * 1.4f, y, w * 0.8f - 2.0f, 48 }, "PFL Trim", std::move (trim));
        add (Field::Value, { px + w * 2.2f, y, w * 0.8f - 2.0f, 48 }, "Delay").enabled = false;
        addValue ({ px + w * 3.0f, y, w - 2.0f, 48 }, "Listen Level", bindFader ("Listen Level", e.monitorLevelDb));
        return;
    }
    if (audioTab == 1)
    {
        // As p.96. Sidecar's talkback comes from a channel, so Gain and 48V are that
        // channel's socket (as when a socket is shared, p.96); Trim is the talkback's own.
        auto& talk = e.talk;
        const float px = body.getX(), pw = 336.0f;
        add (Field::Header, { px, top, pw, 18 }, "Source Select");
        const auto& channels = e.getChannels();
        const int inputs = (int) channels.size();
        const auto current = [&e, inputs]
        {
            const auto* s = e.getTalkStrip();
            for (int i = 0; i < inputs; ++i)
                if (e.getChannels()[(size_t) i].get() == s)
                    return i + 1;
            return 0;
        };
        addValue ({ px + 18, top + 22, 170, 44 }, "Source",
                  choiceBinding ("Source", inputs + 1, current,
                                 [this, &e] (int v)
                                 {
                                     e.setTalkStrip (v > 0 ? e.getChannels()[(size_t) (v - 1)].get() : nullptr);
                                     rebuild();
                                 },
                                 [&e] (int v) { return v > 0 ? e.getChannels()[(size_t) (v - 1)]->getName() : juce::String ("Unassigned"); }));
        add (Field::Header, { px, top + 74, pw, 18 }, "Socket Preamp");
        auto* strip = e.getTalkStrip();
        auto* src = strip != nullptr ? preampSource (*strip) : nullptr;
        if (src != nullptr)
        {
            addToggle (Field::Button, { px + 18, top + 100, 66, 30 }, "48V", src->phantom).colour = juce::Colours::red;
            auto gain = bindLinear ("Gain", src->gainDb, 0.0f, 60.0f, " dB", 0, 0.0f);
            gain.steps = 61.0f;
            addValue ({ px + 240, top + 92, 80, 44 }, "Gain", std::move (gain));
        }
        else
        {
            add (Field::Button, { px + 18, top + 100, 66, 30 }, "48V").enabled = false;
            add (Field::Value, { px + 240, top + 92, 80, 44 }, "Gain").enabled = false;
        }
        add (Field::Button, { px + 112, top + 100, 66, 30 }, "Pad").enabled = false;
        const float y = top + 146.0f;
        add (Field::Header, { px, y, 106, 34 }, "HPF");
        add (Field::Value, { px + 14, y + 40, 80, 44 }, "Frequency").enabled = false; // an HPF of the talkback's own: none
        add (Field::InKey, { px + 34, y + 96, 40, 40 }, "HPF In").enabled = false;
        add (Field::Header, { px + 112, y, 108, 34 }, "Momentary + Latch");
        auto& latch = add (Field::Button, { px + 126, y + 56, 80, 34 }, "Latch");
        latch.caption = "On";
        latch.captionOff = "Off";
        latch.colour = juce::Colours::white;
        auto* path = &talk.paths[0];
        latch.lit = [path] { return path->mode.load() == ld::kTalkAuto; };
        latch.press = [path] { path->mode = path->mode.load() == ld::kTalkAuto ? (int) ld::kTalkPush : (int) ld::kTalkAuto; };
        add (Field::Header, { px + 226, y, 110, 34 }, "Trim");
        addValue ({ px + 240, y + 50, 80, 44 }, "Trim", bindLinear ("Trim", talk.levelDb, -24.0f, 24.0f, " dB", 1, 0.0f));
        addMixGrid ({ px + pw + 8.0f, top, right - px - pw - 8.0f, body.getBottom() - top },
                    [&e, path] (int c)
                    {
                        juce::ignoreUnused (e);
                        return c < kMixes ? path->buses.contains (c) : c == kMixes ? path->mains.contains (0) : path->matrices.contains (c - kMixes - 1);
                    },
                    [path] (int c, bool on)
                    {
                        if (c < kMixes)
                            path->buses.set (c, on);
                        else if (c == kMixes)
                            path->mains.set (0, on);
                        else
                            path->matrices.set (c - kMixes - 1, on);
                    });
        return;
    }
    if (audioTab == 2)
    {
        // As p.97. Band-pass noise has no oscillator here: the list has the other three.
        const float px = body.getX() + 40.0f;
        addValue ({ px, top + 22, 180, 44 }, "Signal Type", bindChoice ("Signal Type", sigType, { "Sine", "White noise", "Pink noise" }, 0));
        addToggle (Field::Button, { px + 50, top + 96, 80, 36 }, "Mute", sigMute).colour = juce::Colours::red;
        addValue ({ px, top + 190, 86, 48 }, "Frequency", bindFreq ("Frequency", sigFreq, 20.0f, 20000.0f, 1000.0f)).enabled = sigType.load() == 0;
        addValue ({ px + 96, top + 190, 86, 48 }, "Level", bindLinear ("Level", sigLevel, -72.0f, 18.0f, " dB", 1, 0.0f));
        addMixGrid ({ body.getX() + 344.0f, top, right - body.getX() - 344.0f, body.getBottom() - top },
                    [this] (int c) { return (sigAssign.load() >> c & 1) != 0; },
                    [this] (int c, bool on)
                    {
                        const int bit = 1 << c;
                        sigAssign = on ? sigAssign.load() | bit : sigAssign.load() & ~bit;
                        applySigGen();
                    });
        return;
    }
    if (audioTab == 3)
    {
        // As p.98. Sidecar runs at the audio device's rate, on its clock: there is nothing to
        // choose, so the boxes say what is, and Apply and Cancel stay dim.
        const auto rate = [this]
        {
            auto* device = ctx.deviceManager.getCurrentAudioDevice();
            return device != nullptr ? juce::String (device->getCurrentSampleRate() / 1000.0, 1).replace (".0", "") + "kHz" : juce::String ("-");
        };
        add (Field::Value, { 194, 162, 136, 44 }, "Audio Clock Source").live = [] { return juce::String ("Internal"); };
        add (Field::Label, { 174, 210, 176, 30 }, {}).caption = "The audio device's clock";
        fields.back().centred = true;
        auto& aes = add (Field::Value, { 434, 162, 136, 44 }, "Local AES out Sample Rate"); // no AES socket here
        aes.enabled = false;
        aes.live = [] { return juce::String ("-"); };
        add (Field::Value, { 434, 238, 136, 44 }, "USB Sample Rate").live = rate;
        add (Field::Button, { 264, 360, 84, 30 }, "Apply").enabled = false;
        add (Field::Button, { 416, 360, 84, 30 }, "Cancel").enabled = false;
        return;
    }
    // I/O Port (p.100): no option card is fitted, so there is nothing to bridge.
    add (Field::Label, { 92, 70, 62, 30 }, {}).caption = "I/O Card";
    auto& card = add (Field::Value, { 160, 73, 136, 24 }, "I/O Card");
    card.hideTitle = true;
    card.live = [] { return juce::String ("None"); };
    add (Field::Label, { 410, 70, 150, 30 }, {}).caption = "Control Network Bridge";
    auto& bridge = add (Field::Button, { 566, 73, 46, 24 }, "Control Network Bridge");
    bridge.caption = "Off";
    bridge.enabled = false;
    add (Field::Label, { 120, 180, 520, 60 }, {}).caption = "No option card is fitted. Sidecar's sockets are the audio device's: the I/O screen patches them.";
    fields.back().centred = true;
}

void Display::buildSurfaceSetup()
{
    const auto body = bodyArea();
    {
        const float w = (body.getWidth() - 12.0f) / 4.0f;
        const char* const names[] = { "Strip Assign", "Soft Controls", "Footswitch", "Surface Prefs" };
        for (int t = 0; t < 4; ++t)
            addTab ({ body.getX() + (float) t * (w + 4.0f), body.getY(), w, 24 }, names[t], surfaceTab, t);
    }
    if (surfaceTab == 1)
    {
        buildSoftControls();
        return;
    }
    if (surfaceTab == 2)
    {
        buildFootswitch();
        return;
    }
    if (surfaceTab == 3)
    {
        // As p.92. Lit keys act; the dim ones have nothing to act on yet.
        const float x0 = body.getX() + 2.0f, cw = 118.0f, top = body.getY() + 36.0f;
        const auto cell = [&] (int c, int r, const char* name, const char* key, bool fallback, bool enabled)
        {
            const float x = x0 + (float) c * cw, y = top + (float) r * 84.0f;
            add (Field::Label, { x, y, cw - 4.0f, 28.0f }, {}).caption = name;
            fields.back().centred = true;
            auto& f = add (Field::Button, { x + 26, y + 32, 60, 26 }, juce::String (name) + " Key");
            f.caption = "On";
            f.captionOff = "Off";
            f.colour = juce::Colours::white;
            f.enabled = enabled;
            f.lit = [this, key, fallback] { return prefs::get (ctx.settings, key, fallback); };
            f.press = [this, key, fallback]
            {
                prefs::set (ctx.settings, key, ! prefs::get (ctx.settings, key, fallback));
                if (onSurfaceChanged)
                    onSurfaceChanged();
            };
        };
        cell (0, 0, "Confirm Scene Operations", "sq5.prefs.confirmScene", true, false);
        cell (1, 0, "Preamp On Surface", prefs::preampOnSurface, true, true);
        cell (2, 0, "Safe I/O Patching", "sq5.prefs.safePatch", true, false);
        cell (0, 1, "Processing Screens Follow Surface", "sq5.prefs.followSurface", false, false);
        cell (1, 1, "PEQ Band Follow Screen", "sq5.prefs.peqFollow", true, false);
        cell (2, 1, "FX Screen Follow Sel", prefs::fxFollowSel, true, true);
        add (Field::Value, { x0 + 20.0f, top + 180, 130, 44 }, "Delay Units").enabled = false; // Sidecar shows ms
        add (Field::Value, { x0 + 190.0f, top + 180, 90, 44 }, "Temperature").enabled = false;
        const float bx = x0 + 370.0f;
        add (Field::Header, { bx, top - 4, body.getRight() - bx, 18 }, "Brightness");
        int r = 0;
        for (const char* name : { "Touchscreen", "LEDs", "Channel Strip LCDs", "Light Bar Brightness", "Light Bar Balance" })
            add (Field::Value, { bx + 34.0f, top + 20.0f + (float) r++ * 56.0f, 150, 46 }, name).enabled = false;
        return;
    }

    // Strip Assign (p.84): the channels above, the layer's strips below; touch a
    // channel, then a strip (the console drags one to the other); the channel types and
    // layers in keys at the right.
    const float keysW = 108.0f, listW = body.getWidth() - keysW - 6.0f, keysX = body.getX() + listW + 6.0f;
    const float cw = listW / 12.0f, top = body.getY() + 30.0f;
    const Slot::Kind kinds[] = { Slot::Input, Slot::FxReturn, Slot::Mix, Slot::FxSend, Slot::Main, Slot::Matrix, Slot::Dca };
    const char* const names[] = { "IP", "FX Rtn", "Mix", "FX", "LR", "Mtx", "DCA" };
    const int counts[] = { 48, 8, kMixes, kFxSends, 1, 3, 8 };
    for (int k = 0; k < 7; ++k)
        addTab ({ keysX + (float) (k % 2) * (keysW * 0.5f + 1.0f), top + (float) (k / 2) * 28.0f, keysW * 0.5f - 1.0f, 26 }, names[k], assignKind, k);
    assignFirst = juce::jlimit (0, juce::jmax (0, counts[assignKind] - 12), assignFirst);
    for (int i = 0; i < 12 && assignFirst + i < counts[assignKind]; ++i)
    {
        const Slot slot { kinds[assignKind], assignFirst + i };
        auto* strip = stripOf (ctx.engine, slot);
        const auto title = strip != nullptr ? strip->getName() : "DCA" + juce::String (slot.index + 1);
        auto& f = add (Field::Button, { body.getX() + (float) i * cw + 1, top, cw - 2, 150 }, title);
        f.lit = [this, slot] { return assignPicked && assignChoice == slot; };
        f.press = [this, slot]
        {
            assignChoice = slot;
            assignPicked = true;
        };
    }
    if (getLayerSlots == nullptr || assignSlot == nullptr)
        return;
    for (int l = 0; l < 6; ++l)
    {
        const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + l));
        addTab ({ keysX + (float) (l % 2) * (keysW * 0.5f + 1.0f), top + 160.0f + (float) (l / 2) * 48.0f, keysW * 0.5f - 1.0f, 46 }, letter, assignLayer, l);
    }
    const auto& slots = getLayerSlots (assignLayer);
    const float sw = listW / 16.0f;
    for (int i = 0; i < (int) slots.size(); ++i)
    {
        const auto slot = slots[(size_t) i];
        auto* strip = stripOf (ctx.engine, slot);
        const auto title = slot.kind == Slot::None ? juce::String ("-") : strip != nullptr ? strip->getName() : "DCA" + juce::String (slot.index + 1);
        auto& f = add (Field::Button, { body.getX() + (float) i * sw + 1, top + 160, sw - 2, 150 }, title);
        f.press = [this, i]
        {
            // A strip touched with no channel chosen is emptied.
            assignSlot (assignLayer, i, assignPicked ? assignChoice : Slot {});
            assignPicked = false;
            rebuild();
        };
    }
}

void Display::buildTypePicker (juce::Rectangle<float> area)
{
    // The control type's list (p.89): the types with no counterpart are dim. DAW Control is
    // Bank Up / Down of the app's DAW remote, where the console has one.
    const auto names = soft::typeNames();
    const float cw = area.getWidth() / 3.0f, ch = area.getHeight() / 9.0f;
    for (int t = 0; t < soft::kNumTypes; ++t)
    {
        auto& f = add (Field::Button, { area.getX() + (float) (t % 3) * cw + 2.0f, area.getY() + (float) (t / 3) * ch + 1.0f, cw - 4.0f, ch - 2.0f }, names[t]);
        f.enabled = soft::isModelled (t) || (t == soft::DawControl && ctx.midi != nullptr);
        f.colour = juce::Colours::white;
        f.lit = [this, t] { return softEdit.type == t; };
        f.press = [this, t]
        {
            softEdit = { t, 0, 0 };
            softPicking = false;
            rebuild();
        };
    }
}

void Display::buildAssignValues (juce::Rectangle<float> typeArea, juce::Rectangle<float> option1, juce::Rectangle<float> option2)
{
    auto& type = add (Field::Button, typeArea, "Control Type");
    type.caption = soft::typeNames()[softEdit.type];
    type.press = [this]
    {
        softPicking = true;
        rebuild();
    };
    const int count1 = soft::option1Count (softEdit.type), count2 = soft::option2Count (softEdit.type, ctx.show.getNumScenes());
    {
        auto binding = choiceBinding ("Option 1", juce::jmax (1, count1), [this] { return softEdit.option1; }, [this] (int v) { softEdit.option1 = v; }, [this] (int v) { return soft::option1Text (softEdit.type, v); });
        auto& f = addValue (option1, "Option 1", std::move (binding));
        f.enabled = count1 > 0;
        f.hideTitle = option1.getHeight() < 40.0f;
    }
    {
        auto binding = choiceBinding ("Option 2", juce::jmax (1, count2), [this] { return softEdit.option2; }, [this] (int v) { softEdit.option2 = v; }, [this] (int v) { return soft::option2Text (softEdit.type, v, ctx.engine); });
        auto& f = addValue (option2, "Option 2", std::move (binding));
        f.enabled = count2 > 0;
        f.hideTitle = option2.getHeight() < 40.0f;
    }
}

void Display::buildSoftControls()
{
    const auto body = bodyArea();
    const float top = body.getY() + 30.0f;
    softKeySelected = juce::jlimit (0, 7, softKeySelected);
    if (assignEditing != softKeySelected)
    {
        softEdit = soft::get (ctx.settings, softKeySelected);
        assignEditing = softKeySelected;
        softPicking = false;
    }
    if (softPicking)
    {
        buildTypePicker (body.withTop (top));
        return;
    }
    // As p.88: SoftKeys 1-8 in two columns, what's assigned at the right. The SQ-5 has no
    // SoftRotaries (p.88: SQ-6, SQ-7 and SQ-Rack).
    for (int k = 0; k < 8; ++k)
    {
        const float x = body.getX() + 30.0f + (float) (k / 4) * 170.0f, y = top + 10.0f + (float) (k % 4) * 68.0f;
        auto& tab = add (Field::Tab, { x, y, 76, 32 }, "Soft " + juce::String (k + 1));
        tab.lit = [this, k] { return softKeySelected == k; };
        tab.press = [this, k]
        {
            softKeySelected = k;
            assignEditing = -1;
            rebuild();
        };
        const auto assigned = soft::get (ctx.settings, k);
        add (Field::Label, { x - 16.0f, y + 33.0f, 130, 18 }, {}).caption = assigned.type == soft::Unassigned ? juce::String ("-") : soft::typeNames()[assigned.type];
        fields.back().centred = true;
    }
    const float px = body.getRight() - 196.0f, pw = 196.0f;
    add (Field::Header, { px, top, pw, 18 }, "Soft Key Controls");
    buildAssignValues ({ px + 8, top + 26, pw - 16, 34 }, { px + 8, top + 72, pw - 16, 44 }, { px + 8, top + 128, pw - 16, 44 });
    auto& apply = add (Field::Button, { px + 8, top + 196, pw - 16, 34 }, "Apply");
    apply.press = [this]
    {
        soft::set (ctx.settings, softKeySelected, softEdit);
        rebuild();
    };
    auto& cancel = add (Field::Button, { px + 8, top + 240, pw - 16, 34 }, "Cancel");
    cancel.press = [this]
    {
        assignEditing = -1;
        rebuild();
    };
}

void Display::buildFootswitch()
{
    // As p.91: Tip and Ring at the left of the plug's picture, what the chosen switch does
    // at its right. Sidecar has no footswitch socket: the two assignments are kept, and
    // Sq5Surface::pressFootswitch runs them for whatever stands in for a pedal. Calibrate
    // (the switch's kind) has nothing to measure.
    const auto body = bodyArea();
    footswitchSelected = juce::jlimit (0, 1, footswitchSelected);
    if (assignEditing != 100 + footswitchSelected)
    {
        softEdit = soft::getFootswitch (ctx.settings, footswitchSelected);
        assignEditing = 100 + footswitchSelected;
        softPicking = false;
    }
    if (softPicking)
    {
        buildTypePicker (body.withTop (body.getY() + 30.0f));
        return;
    }
    for (int w = 0; w < 2; ++w)
    {
        auto& tab = add (Field::Tab, { 176.0f, w == 0 ? 112.0f : 156.0f, 60, 32 }, w == 0 ? "Tip" : "Ring");
        tab.lit = [this, w] { return footswitchSelected == w; };
        tab.press = [this, w]
        {
            footswitchSelected = w;
            assignEditing = -1;
            rebuild();
        };
    }
    add (Field::Button, { 168, 284, 78, 32 }, "Calibrate").enabled = false;
    buildAssignValues ({ 340, 122, 150, 32 }, { 340, 164, 150, 26 }, { 340, 200, 150, 26 });
    add (Field::Label, { 500, 118, 172, 110 }, {}).caption = "No footswitch socket here: the assignment is kept for what stands in for one.";
    auto& apply = add (Field::Button, { 264, 360, 84, 30 }, "Apply");
    apply.press = [this]
    {
        soft::setFootswitch (ctx.settings, footswitchSelected, softEdit);
        rebuild();
    };
    auto& cancel = add (Field::Button, { 416, 360, 84, 30 }, "Cancel");
    cancel.press = [this]
    {
        assignEditing = -1;
        rebuild();
    };
}

void Display::buildMixerConfig()
{
    const auto body = bodyArea();
    {
        const float w = (body.getWidth() - 12.0f) / 4.0f;
        const char* const names[] = { "Input Stereo", "Mix Stereo", "Bus Config", "Role" };
        for (int t = 0; t < 4; ++t)
            addTab ({ body.getX() + (float) t * (w + 4.0f), body.getY(), w, 24 }, names[t], configTab, t);
    }
    const float top = body.getY() + 32.0f, bottom = body.getBottom() - 40.0f;
    const auto applyKeys = [&] (std::function<void()> apply, std::function<void()> cancel, bool enabled = true)
    {
        auto& a = add (Field::Button, { body.getX() + 180, bottom + 4, 100, 32 }, "Apply");
        a.press = std::move (apply);
        a.enabled = enabled;
        auto& c = add (Field::Button, { body.getX() + 320, bottom + 4, 100, 32 }, "Cancel");
        c.press = std::move (cancel);
        c.enabled = enabled;
    };
    if (configTab == 0)
    {
        // As p.85: an odd/even pair is one stereo channel: the odd strip takes both
        // sockets, the even strip goes off the surface.
        const auto& channels = ctx.engine.getChannels();
        if (! stereoPendingValid)
        {
            for (int p = 0; p < 24; ++p)
                stereoPending[(size_t) p] = channels[(size_t) (2 * p)]->params.stereo.load();
            stereoPendingValid = true;
        }
        const float cw = body.getWidth() / 8.0f, ch = (bottom - top) / 3.0f;
        for (int p = 0; p < 24; ++p)
        {
            juce::Rectangle<float> cell (body.getX() + (float) (p % 8) * cw, top + (float) (p / 8) * ch, cw, ch);
            add (Field::Header, cell.withHeight (18.0f).reduced (2.0f, 0.0f), juce::String (2 * p + 1) + "/" + juce::String (2 * p + 2));
            auto& f = add (Field::Button, cell.withTrimmedTop (28.0f).withHeight (38.0f).reduced (4.0f, 0.0f), "Pair " + juce::String (p + 1));
            f.caption = "Stereo";
            f.captionOff = "Mono";
            f.colour = juce::Colours::white;
            f.lit = [this, p] { return stereoPending[(size_t) p]; };
            f.press = [this, p] { stereoPending[(size_t) p] = ! stereoPending[(size_t) p]; };
        }
        applyKeys ([this]
                   {
                       applyInputStereo();
                       rebuild();
                   },
                   [this]
                   {
                       stereoPendingValid = false;
                       rebuild();
                   });
        return;
    }
    if (configTab == 1)
    {
        // As p.86: Groups, All, then the Auxes; the matrices on a row of their own. A mono mix
        // is the engine's mono output: both sides summed. The three extra mono matrices a
        // mono matrix frees on the console (p.86) aren't here.
        const auto& buses = ctx.engine.getBuses();
        const auto& matrices = ctx.engine.getMatrices();
        const auto stripAt = [&buses, &matrices] (int i) -> Strip*
        {
            const auto& list = i < kMixes ? buses : matrices;
            const int at = i < kMixes ? i : i - kMixes;
            return at < (int) list.size() ? list[(size_t) at].get() : nullptr;
        };
        if (! mixStereoPendingValid)
        {
            for (int i = 0; i < kMixes + 3; ++i)
            {
                auto* s = stripAt (i);
                mixStereoPending[(size_t) i] = s == nullptr || ! s->params.mono.load();
            }
            mixStereoPendingValid = true;
        }
        const float cw = body.getWidth() / 6.0f, ch = (bottom - top) / 4.0f;
        const auto cell = [&] (int at, const juce::String& name, int index)
        {
            juce::Rectangle<float> r (body.getX() + (float) (at % 6) * cw, top + (float) (at / 6) * ch, cw, ch);
            add (Field::Header, r.withHeight (18.0f).reduced (2.0f, 0.0f), name);
            auto& key = add (Field::Button, r.withTrimmedTop (24.0f).withHeight (34.0f).reduced (8.0f, 0.0f), name + " Stereo");
            key.caption = "Stereo";
            key.captionOff = "Mono";
            key.colour = juce::Colours::white;
            key.enabled = index < 0 || stripAt (index) != nullptr;
            if (index < 0)
            {
                const auto all = [this] { for (int m = 0; m < kMixes; ++m) if (! mixStereoPending[(size_t) m]) return false; return true; };
                key.lit = all;
                key.press = [this, all] { const bool stereo = ! all(); for (int m = 0; m < kMixes; ++m) mixStereoPending[(size_t) m] = stereo; };
            }
            else
            {
                key.lit = [this, index] { return mixStereoPending[(size_t) index]; };
                key.press = [this, index] { mixStereoPending[(size_t) index] = ! mixStereoPending[(size_t) index]; };
            }
        };
        const int groups = groupCount(), auxes = kMixes - groups;
        int at = 0;
        for (int g = 0; g < groups; ++g)
            cell (at++, "Grp" + juce::String (g + 1), auxes + g);
        cell (at++, "All", -1);
        for (int a = 0; a < auxes; ++a)
            cell (at++, "Aux" + juce::String (a + 1), a);
        for (int m = 0; m < 3; ++m)
            cell (18 + m, "Mtx" + juce::String (m + 1), kMixes + m);
        applyKeys ([this]
                   {
                       applyMixStereo();
                       rebuild();
                   },
                   [this]
                   {
                       mixStereoPendingValid = false;
                       rebuild();
                   });
        return;
    }
    if (configTab == 2)
    {
        // As p.87: one number sets the other.
        if (pendingGroups < 0)
            pendingGroups = groupCount();
        const float cx = body.getCentreX();
        const auto number = [this] (bool groups)
        {
            return choiceBinding (groups ? "Number of Groups" : "Number of Auxes", kMixes + 1,
                                  [this, groups] { return groups ? pendingGroups : kMixes - pendingGroups; },
                                  [this, groups] (int v) { pendingGroups = groups ? v : kMixes - v; },
                                  [] (int v) { return juce::String (v); });
        };
        addValue ({ cx - 190, top + 50, 130, 48 }, "Number of Groups", number (true));
        addValue ({ cx + 60, top + 50, 130, 48 }, "Number of Auxes", number (false));
        applyKeys ([this]
                   {
                       applyBusConfig (pendingGroups);
                       pendingGroups = -1;
                       rebuild();
                   },
                   [this]
                   {
                       pendingGroups = -1;
                       rebuild();
                   });
        return;
    }
    // Role, as p.103: the two resets, and what the master fader does.
    const float x = body.getX() + 10.0f;
    add (Field::Header, { x, top, 340, 18 }, "Reset Mix Settings");
    for (int r = 0; r < 2; ++r)
    {
        auto& f = addTab ({ x + 14, top + 30.0f + (float) r * 46.0f, 120, 36 }, r == 0 ? "Standard" : "Monitor", resetRoleChoice, r);
        juce::ignoreUnused (f);
    }
    auto& reset = add (Field::Button, { x + 14, top + 150, 120, 36 }, "Reset");
    reset.caption = resetArmed ? "Confirm Reset" : "Reset";
    reset.colour = juce::Colours::orange;
    reset.lit = [this] { return resetArmed; };
    reset.press = [this]
    {
        if (resetArmed)
            resetRole (resetRoleChoice);
        resetArmed = ! resetArmed;
        rebuild();
    };
    add (Field::Label, { x + 146, top + 30, 196, 150 }, {}).caption = resetRoleChoice == 0
        ? "Standard: inputs 1-40 mono, 4 stereo. Aux 1-8, Groups 1-4. LR assigned; channel faders -inf. Auxes pre-fade, post-EQ. Master fader: Mix Master."
        : "Monitor: 48 mono inputs. Auxes 1-12. LR not assigned; channels 0 dB. Auxes post-fade, post-delay. Master fader: Listen Level.";
    const float mx = x + 370.0f;
    add (Field::Header, { mx, top, body.getRight() - mx, 18 }, "Master Fader Mode");
    const auto mode = [&] (int r, const char* name, bool listen)
    {
        auto& f = add (Field::Tab, { mx + 30, top + 40.0f + (float) r * 60.0f, 130, 40 }, name);
        f.lit = [this, listen] { return prefs::get (ctx.settings, prefs::listen, false) == listen; };
        f.press = [this, listen]
        {
            prefs::set (ctx.settings, prefs::listen, listen);
            if (onSurfaceChanged)
                onSurfaceChanged();
        };
    };
    mode (0, "Mix Master", false);
    mode (1, "Listen Level", true);
}

//==============================================================================
// Setup's Ganging, Network Setup and Users

void Display::buildGanging()
{
    // As p.105: the gangs' numbers across the top, the input channels under them (the
    // arrows, or a drag, for the rest), then the chosen gang's twelve places, then what it
    // links. The console drags a channel to a place: here a touch puts a channel in the
    // gang and a touch on a member takes it out. Apply makes the gangs.
    if (! gangPendingValid)
    {
        gangPending = gangSet;
        gangPendingValid = true;
    }
    const auto body = bodyArea();
    gangSelected = juce::jlimit (0, gangs::kGangs - 1, gangSelected);
    {
        const float w = (body.getWidth() - 21.0f) / 8.0f;
        for (int g = 0; g < gangs::kGangs; ++g)
            addTab ({ body.getX() + (float) g * (w + 3.0f), body.getY(), w, 24 }, "Gang " + juce::String (g + 1), gangSelected, g).caption = juce::String (g + 1);
    }
    const auto& channels = ctx.engine.getChannels();
    const int inputs = juce::jmin (48, (int) channels.size());
    constexpr int shown = 10;
    gangFirst = juce::jlimit (0, juce::jmax (0, inputs - shown), gangFirst);
    lists.push_back ({ { 108.0f, 70.0f, 538.0f, 88.0f }, &gangFirst, true });
    const auto page = [this] (const char* name, juce::Rectangle<float> r, int delta)
    {
        auto& key = add (Field::Button, r, name);
        key.press = [this, delta]
        {
            gangFirst += delta;
            rebuild();
        };
    };
    page ("<", { 78.0f, 70.0f, 26.0f, 88.0f }, -shown);
    page (">", { 650.0f, 70.0f, 26.0f, 88.0f }, shown);
    for (int i = 0; i < shown && gangFirst + i < inputs; ++i)
    {
        const int channel = gangFirst + i;
        const int in = gangs::gangOf (gangPending, channel);
        const auto name = channels[(size_t) channel]->getName();
        auto& key = add (Field::Button, { 108.0f + (float) i * 53.8f + 1.0f, 70.0f, 51.8f, 88.0f }, name);
        key.colour = kGreenBar;
        // A channel is in one gang: one that's in another says which, and can't be taken.
        key.enabled = in < 0 || in == gangSelected;
        if (in >= 0 && in != gangSelected)
            key.caption = name + "\nGang " + juce::String (in + 1);
        key.lit = [this, channel] { return gangs::gangOf (gangPending, channel) == gangSelected; };
        key.press = [this, channel]
        {
            auto& members = gangPending[(size_t) gangSelected].members;
            if (const auto at = std::find (members.begin(), members.end(), channel); at != members.end())
                members.erase (at);
            else if ((int) members.size() < gangs::kMembers)
                members.push_back (channel);
            rebuild();
        };
    }
    const auto& gang = gangPending[(size_t) gangSelected];
    for (int m = 0; m < gangs::kMembers; ++m)
    {
        auto& key = add (Field::Button, { body.getX() + (float) m * 50.0f + 1.0f, 164.0f, 48.0f, 86.0f }, "Member " + juce::String (m + 1));
        key.colour = kGreenBar;
        if (m >= (int) gang.members.size())
        {
            key.enabled = false;
            continue;
        }
        const int channel = gang.members[(size_t) m];
        key.caption = juce::isPositiveAndBelow (channel, (int) channels.size()) ? channels[(size_t) channel]->getName() : "Ip" + juce::String (channel + 1);
        key.lit = [] { return true; };
        key.press = [this, m]
        {
            auto& members = gangPending[(size_t) gangSelected].members;
            if (m < (int) members.size())
                members.erase (members.begin() + m);
            rebuild();
        };
    }
    // The attributes, two rows of six: a white key is linked.
    const int places[] = { 0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11 };
    for (int a = 0; a < gangs::kNumAttributes; ++a)
    {
        const float cx = 138.0f + (float) (places[a] % 6) * 72.0f, y = places[a] < 6 ? 266.0f : 326.0f;
        add (Field::Label, { cx - 35.0f, y, 70.0f, 14.0f }, {}).caption = gangs::attributeName (a);
        fields.back().centred = true;
        fields.back().enabled = gangs::isModelled (a);
        auto& key = add (Field::Button, { cx - 22.0f, y + 16.0f, 44.0f, 26.0f }, juce::String (gangs::attributeName (a)) + " Link");
        key.caption = " ";
        key.colour = juce::Colours::white;
        key.enabled = gangs::isModelled (a);
        key.lit = [this, a] { return (gangPending[(size_t) gangSelected].attributes >> a & 1) != 0; };
        key.press = [this, a] { gangPending[(size_t) gangSelected].attributes ^= 1 << a; };
    }
    auto& apply = add (Field::Button, { 556.0f, 276.0f, 104.0f, 32.0f }, "Apply");
    apply.press = [this]
    {
        gangSet = gangPending;
        gangs::save (ctx.settings, gangSet);
        gangFollower.reset();
        rebuild();
    };
    auto& cancel = add (Field::Button, { 556.0f, 330.0f, 104.0f, 32.0f }, "Cancel");
    cancel.press = [this]
    {
        gangPendingValid = false;
        rebuild();
    };
}

void Display::buildNetwork()
{
    // As p.106. Sidecar is on the computer's network, with the address the computer has:
    // the static settings and DHCP are the computer's to set, so they're dim, and the
    // Dynamic Address shows the address clients reach Sidecar at. The Unit Name is the
    // app's console name (it has no keyboard here to change it). Between the addresses and
    // the name, Sidecar's own line: its MIDI over TCP/IP server as it is now.
    for (int side = 0; side < 2; ++side)
    {
        const float x = side == 0 ? 132.0f : 408.0f;
        add (Field::Header, { x, 70.0f, 226.0f, 18.0f }, side == 0 ? "Static Address" : "Dynamic Address");
        const char* const names[] = { "IP Address", "Subnet Mask", "Gateway" };
        for (int row = 0; row < 3; ++row)
        {
            auto& value = add (Field::Value, { x + 30.0f, 96.0f + (float) row * 54.0f, 166.0f, 44.0f }, juce::String (side == 0 ? "Static " : "Dynamic ") + names[row]);
            value.caption = names[row];
            if (side == 1 && row == 0)
                value.live = [] { return juce::IPAddress::getLocalAddress().toString(); };
            else
                value.enabled = false; // the computer's own settings
        }
    }
    add (Field::Header, { 132.0f, 262.0f, 502.0f, 18.0f }, "MIDI over TCP/IP");
    auto& state = add (Field::Label, { 132.0f, 282.0f, 502.0f, 20.0f }, "MIDI over TCP/IP State");
    state.centred = true;
    state.live = [this]
    {
        auto& remote = ctx.engine.getWing();
        const auto port = "Port " + juce::String (WingRemote::ahMixRackPort);
        if (! remote.isAhMidiRunning())
            return port + "     Not running";
        const int clients = remote.getAhMidiClients();
        return port + "     Running     " + juce::String (clients) + (clients == 1 ? " client" : " clients");
    };
    add (Field::Header, { 132.0f, 310.0f, 84.0f, 18.0f }, "DHCP");
    auto& dhcp = add (Field::Button, { 147.0f, 340.0f, 54.0f, 26.0f }, "DHCP Key");
    dhcp.caption = "On";
    dhcp.enabled = false;
    add (Field::Header, { 250.0f, 310.0f, 264.0f, 18.0f }, "Unit Name");
    auto& name = add (Field::Value, { 264.0f, 340.0f, 236.0f, 26.0f }, "Unit Name");
    name.hideTitle = true;
    name.live = [this] { return ctx.settings.getValue (WingRemote::Settings::nameKey, "SIDECAR"); };
    add (Field::Button, { 550.0f, 310.0f, 86.0f, 34.0f }, "Apply").enabled = false;
    add (Field::Button, { 550.0f, 354.0f, 86.0f, 34.0f }, "Cancel").enabled = false;
}

void Display::buildUsers()
{
    // As pp.107-109. Sidecar has one user, the Admin, who can't be blocked (p.107): the ten
    // other users stay inactive, and every permission reads Allow. Drawn to be looked at.
    const auto body = bodyArea();
    {
        const char* const names[] = { "User Settings", "Setup", "Parameters", "Control" };
        const float w = (body.getWidth() - 12.0f) / 4.0f;
        for (int t = 0; t < 4; ++t)
            addTab ({ body.getX() + (float) t * (w + 4.0f), body.getY(), w, 24 }, names[t], usersTab, t);
    }
    add (Field::Header, { body.getX(), 68.0f, 240.0f, 18.0f }, "Users");
    addList ({ body.getX(), 90.0f, 240.0f, 304.0f }, "User Row ", 11, userFirst, userSelected,
             [] (int i) { return i == 0 ? juce::String ("Admin      (active)") : "User " + juce::String (i); });
    const bool admin = userSelected == 0;
    const float px = 324.0f;
    if (usersTab == 0)
    {
        auto& name = add (Field::Value, { px + 26.0f, 94.0f, 220.0f, 26.0f }, "User Name");
        name.hideTitle = true;
        name.enabled = admin;
        name.live = [this] { return userSelected == 0 ? juce::String ("Admin") : "User " + juce::String (userSelected); };
        auto& active = add (Field::Button, { px + 258.0f, 92.0f, 72.0f, 28.0f }, "Active");
        active.captionOff = "Inactive";
        active.colour = juce::Colours::white;
        active.lit = [this] { return userSelected == 0; };
        active.enabled = false;
        add (Field::Button, { px + 116.0f, 138.0f, 120.0f, 30.0f }, "Set Password").enabled = false;
        const auto sceneName = [this] (int i)
        {
            const auto scene = ctx.show.getScene (i);
            return juce::String (i + 1) + ") " + (scene.has_value() ? scene->name : juce::String());
        };
        const auto box = [&] (float y, const juce::String& title, std::function<juce::String()> words)
        {
            auto& v = add (Field::Value, { px + 66.0f, y, 220.0f, 26.0f }, title);
            v.hideTitle = true;
            v.enabled = admin;
            v.live = std::move (words);
        };
        const auto label = [&] (float y, const char* words)
        {
            add (Field::Label, { px + 66.0f, y, 220.0f, 14.0f }, {}).caption = words;
            fields.back().centred = true;
            fields.back().enabled = admin;
        };
        label (184.0f, "Scene Range");
        box (200.0f, "Scene Range From", [this, sceneName] { return ctx.show.getNumScenes() > 0 ? sceneName (0) : juce::String ("No Scene"); });
        label (228.0f, "to");
        box (244.0f, "Scene Range To", [this, sceneName] { return ctx.show.getNumScenes() > 0 ? sceneName (ctx.show.getNumScenes() - 1) : juce::String ("No Scene"); });
        label (290.0f, "User Scene");
        box (306.0f, "User Scene", [] { return juce::String ("No Scene"); });
        return;
    }
    // The permissions' names (p.109), ten to a page.
    static const char* const setup[] = { "USB Data Recall", "USB Data Store", "USB Data Modify", "Sync Push", "Sync Pull", "Scene Recall", "Scene Store", "Scene Modify",
                                         "Cue List Modify", "Global Filter", "Channel Safes", "Scene Preferences", "Library Recall", "Library Store", "Library Modify",
                                         "FX Library Recall", "FX Library Store", "FX Library Modify", "Input Stereo", "Mix Stereo", "Bus Config", "Reset Mix Settings",
                                         "PAFL/Talkback Settings", "Signal Generator", "Audio Sync Options", "USB", "Surface Strip Assign", "Master Fader Mode",
                                         "Soft Control Setup", "Meter Settings", "MIDI Settings", "Network Settings", "I/O Port Settings", "USB Format" };
    static const char* const parameters[] = { "Name/Colour", "Input/Group Preamp", "Input/Group EQ", "Input/Group Other Processing", "Mix Preamp", "Mix EQ",
                                              "Mix Other Processing", "FX Parameters", "DirOut and Mix Settings", "Mix Assign and Pre/Post", "DCA/Mute Group Assign",
                                              "Input Patching/Library", "Output/Insert/FX Patching", "AMM Setup/Assign", "AMM Parameters", "Ganging Setup" };
    static const char* const control[] = { "SQ MixPad App Access", "SQ4You App Access", "SQ Control App Access", "Layers B-F", "Groups 1-12", "Auxes 1-12", "FX Sends 1-4",
                                           "Matrices 1-6", "SQ-Drive Playback", "SQ-Drive Stereo Record", "SQ-Drive Multitrack Record", "RTA Settings", "RTA Follow" };
    const char* const* names = usersTab == 1 ? setup : usersTab == 2 ? parameters : control;
    const int count = usersTab == 1 ? (int) std::size (setup) : usersTab == 2 ? (int) std::size (parameters) : (int) std::size (control);
    constexpr int perPage = 10;
    const int pages = (count + perPage - 1) / perPage;
    userPage = juce::jlimit (0, pages - 1, userPage);
    for (int i = userPage * perPage; i < juce::jmin (count, (userPage + 1) * perPage); ++i)
    {
        const float y = 70.0f + (float) (i - userPage * perPage) * 29.0f;
        add (Field::Label, { px + 8.0f, y, 250.0f, 27.0f }, {}).caption = names[i];
        auto& allow = add (Field::Button, { px + 276.0f, y + 2.0f, 64.0f, 23.0f }, juce::String (names[i]) + " Allow");
        allow.caption = "Allow";
        allow.enabled = false;
    }
    if (pages > 1)
    {
        auto& next = add (Field::Button, { px + 252.0f, 366.0f, 88.0f, 26.0f }, "Page");
        next.caption = "Page " + juce::String (userPage + 1) + "/" + juce::String (pages);
        next.press = [this, pages]
        {
            userPage = (userPage + 1) % pages;
            rebuild();
        };
    }
}

void Display::paintSetup (juce::Graphics& g)
{
    if (setupTab == 0 && audioTab == 3)
    {
        // The tick beside a clock source that's locked (p.98).
        juce::Path tick;
        tick.startNewSubPath (338.0f, 190.0f);
        tick.lineTo (343.0f, 196.0f);
        tick.lineTo (353.0f, 182.0f);
        g.setColour (kGreenBar);
        g.strokePath (tick, juce::PathStrokeType (2.2f));
    }
    else if (setupTab == 1 && surfaceTab == 2 && ! softPicking)
    {
        // The footswitch's plug, in outline (p.91): tip, ring, sleeve, its foot; a line from
        // each key to its contact.
        const float cx = 306.0f;
        juce::Path plug;
        plug.startNewSubPath (cx - 8.0f, 118.0f);
        plug.lineTo (cx - 8.0f, 96.0f);
        plug.lineTo (cx, 84.0f);
        plug.lineTo (cx + 8.0f, 96.0f);
        plug.lineTo (cx + 8.0f, 118.0f);
        plug.addRectangle (cx - 14.0f, 118.0f, 28.0f, 212.0f);
        plug.addRoundedRectangle (cx - 28.0f, 330.0f, 56.0f, 12.0f, 3.0f);
        g.setColour (juce::Colours::white);
        g.strokePath (plug, juce::PathStrokeType (1.6f));
        g.fillRect (cx - 14.0f, 144.0f, 28.0f, 5.0f);
        g.fillRect (cx - 14.0f, 190.0f, 28.0f, 5.0f);
        g.fillRect (236.0f, 127.5f, cx - 14.0f - 236.0f, 1.2f);
        g.fillRect (236.0f, 171.5f, cx - 14.0f - 236.0f, 1.2f);
    }
    else if (setupTab == 3)
    {
        // The attributes' panel, its name up its left edge (p.105).
        const juce::Rectangle<float> panel (76.0f, 258.0f, 464.0f, 136.0f);
        g.setColour (kWell.withAlpha (0.35f));
        g.fillRoundedRectangle (panel, 3.0f);
        auto strip = panel.withWidth (20.0f);
        g.setColour (juce::Colour (0xff10283e));
        g.fillRect (strip);
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, strip.getCentreX(), strip.getCentreY()));
        text (g, juce::Rectangle<float> (strip.getHeight(), strip.getWidth()).withCentre (strip.getCentre()), "Attributes", 10.5f, juce::Colours::white, juce::Justification::centred, true);
    }
}

//==============================================================================
// What changes the console

void Display::applySigGen()
{
    const auto* e = ctx.engine.getHandle();
    const int mask = sigAssign.load();
    for (int m = 0; m < kMixes; ++m)
    {
        const float g = (mask >> m & 1) != 0 ? 1.0f : 0.0f;
        ld::ld_engine_console_osc_assign (e, (int) StripKind::Bus, m, g, g);
    }
    const float lr = (mask >> kMixes & 1) != 0 ? 1.0f : 0.0f;
    ld::ld_engine_console_osc_assign (e, (int) StripKind::Main, 0, lr, lr);
    for (int m = 0; m < 3; ++m)
    {
        const float g = (mask >> (kMixes + 1 + m) & 1) != 0 ? 1.0f : 0.0f;
        ld::ld_engine_console_osc_assign (e, (int) StripKind::Matrix, m, g, g);
    }
    const int type = sigType.load();
    ld::ld_engine_console_osc_set (e, ! sigMute.load(), type == 0 ? ld::kOscSine : type == 1 ? ld::kOscWhite : ld::kOscPink, sigFreq.load(), sigLevel.load(), false, false);
    sigShown = { type, sigMute.load() ? 1.0f : 0.0f, sigFreq.load(), sigLevel.load(), mask };
    ctx.settings.setValue ("sq5.siggen", juce::String (type) + " " + juce::String (sigMute.load() ? 1 : 0) + " " + juce::String (sigFreq.load()) + " " + juce::String (sigLevel.load()) + " " + juce::String (mask));
}

void Display::loadSigGen()
{
    const auto v = juce::StringArray::fromTokens (ctx.settings.getValue ("sq5.siggen"), " ", {});
    if (v.size() >= 5)
    {
        sigType = juce::jlimit (0, 2, v[0].getIntValue());
        sigMute = v[1].getIntValue() != 0;
        sigFreq = juce::jlimit (20.0f, 20000.0f, v[2].getFloatValue());
        sigLevel = juce::jlimit (-72.0f, 18.0f, v[3].getFloatValue());
        sigAssign = v[4].getIntValue();
    }
    applySigGen();
}

void Display::applyInputStereo()
{
    // A pair's two sockets go to the odd channel, which takes both; the even channel
    // stops hearing them (its socket comes back when the pair goes mono).
    const auto& channels = ctx.engine.getChannels();
    for (int p = 0; p < 24; ++p)
    {
        auto& a = channels[(size_t) (2 * p)]->params;
        auto& b = channels[(size_t) (2 * p + 1)]->params;
        const bool stereo = stereoPending[(size_t) p];
        if (stereo && ! a.stereo.load())
        {
            a.inputRight = b.inputLeft.load();
            b.inputLeft = -1;
            b.mute = true;
            a.stereo = true;
        }
        else if (! stereo && a.stereo.load())
        {
            a.stereo = false;
            b.inputLeft = a.inputRight.load();
            b.mute = false;
        }
    }
    if (onSurfaceChanged)
        onSurfaceChanged();
}

void Display::applyMixStereo()
{
    // A mono mix: the engine sums the bus's two sides at its output (StripParams::mono).
    const auto& buses = ctx.engine.getBuses();
    const auto& matrices = ctx.engine.getMatrices();
    for (int i = 0; i < kMixes + 3; ++i)
    {
        const auto& list = i < kMixes ? buses : matrices;
        const int at = i < kMixes ? i : i - kMixes;
        if (at < (int) list.size())
            list[(size_t) at]->params.mono = ! mixStereoPending[(size_t) i];
    }
    mixStereoPendingValid = false;
    if (onSurfaceChanged)
        onSurfaceChanged();
}

void Display::applyBusConfig (int groups)
{
    // p.87: Auxes on the first mixes, Groups after them. A mix that changes kind starts
    // as the guide's Reset has it (p.123): a Group takes unassigned channels post-fade at
    // unity and its master from zero; an Aux pre-fade sends and no LR.
    groups = juce::jlimit (0, kMixes, groups);
    const int was = groupCount(), auxes = kMixes - groups;
    const auto minDb = (float) fader::posToDb (0.0f);
    auto& e = ctx.engine;
    const auto& buses = e.getBuses();
    for (int m = 0; m < kMixes; ++m)
    {
        auto* bus = buses[(size_t) m].get();
        const bool nowGroup = m >= auxes, wasGroup = m >= kMixes - was;
        const auto name = bus->getName();
        if (name.matchesWildcard ("Aux*", true) || name.matchesWildcard ("Grp*", true))
            if (name.substring (3).containsOnly ("0123456789"))
                bus->setName (nowGroup ? "Grp" + juce::String (m - auxes + 1) : "Aux" + juce::String (m + 1));
        if (nowGroup == wasGroup)
            continue;
        bus->mains[0].on = nowGroup;
        for (const auto* list : { &e.getChannels(), &e.getAux() })
            for (const auto& c : *list)
            {
                auto& send = c->sends[(size_t) m];
                send.enabled = false;
                send.mode = nowGroup ? (int) SendMode::Post : (int) SendMode::Pre;
                send.levelDb = nowGroup ? 0.0f : minDb;
            }
        bus->params.faderDb = nowGroup ? minDb : 0.0f;
    }
    setGroupCount (groups);
    ctx.settings.setValue ("sq5.groups", groups);
    if (auto* midi = SqMidi::current())
        midi->setGroups (groups);
    if (onSurfaceChanged)
        onSurfaceChanged();
}

void Display::resetRole (int role)
{
    // Reset Mix Settings, as pp.123-124. Scenes and libraries stay; every safe clears.
    // The output patch stays too: Sidecar's own, for now.
    auto& e = ctx.engine;
    const bool standard = role == 0;
    const auto minDb = (float) fader::posToDb (0.0f);
    const int comp = models::findModel (models::Dynamics, "SQCOMP"), gate = models::findModel (models::Dynamics, "SQGATE");
    const auto setParam = [] (ld::DynSlotParams& d, const juce::String& id, float value)
    {
        const auto& m = models::model (models::Dynamics, d.model.load());
        if (const int i = m.find (id); i >= 0)
            d.values[i] = value;
    };
    // The buses' configuration first: it sets which mixes are which.
    applyBusConfig (standard ? 4 : 0);
    const auto& channels = e.getChannels();
    for (int i = 0; i < (int) channels.size(); ++i)
    {
        auto& s = *channels[(size_t) i];
        auto& p = s.params;
        p.recallSafe = false;
        p.mute = false;
        p.solo = false;
        p.pan = 0.0f;
        p.faderDb = standard ? minDb : 0.0f;
        p.stereo = false;
        if (i < 16)
            p.inputLeft = i;
        else if (standard && i >= 40 && i < 46)
            p.inputLeft = 16 + i - 40;
        else if (! standard && i < 22)
            p.inputLeft = i;
        else
            p.inputLeft = -1;
        if (standard && i >= 40 && (i & 1) == 0)
        {
            p.stereo = true;
            p.inputRight = i < 46 ? 16 + i + 1 - 40 : -1;
        }
        if (auto* src = preampSource (s))
        {
            src->gainDb = 28.0f;
            src->phantom = false;
        }
        p.filter().lowCut = false;
        p.filter().lowCutFreq = 100.0f;
        s.setModel (ModelBlock::Gate, gate);
        p.gate().on = false;
        setParam (p.gate(), "thr", -30.0f);
        s.setModel (ModelBlock::Eq, 0);
        p.eq().on = true;
        s.setModel (ModelBlock::Dynamics, comp);
        p.dynamics().on = false;
        setParam (p.dynamics(), "thr", -9.0f);
        setParam (p.dynamics(), "ratio", 8.0f); // 3:1
        s.mains[0].on = standard;
        for (int m = 0; m < kMixes + kFxSends; ++m)
        {
            auto& send = s.sends[(size_t) m];
            const bool group = m < kMixes && isGroupMix (m);
            send.enabled = ! group && m < kMixes;
            send.levelDb = group ? 0.0f : (standard ? minDb : 0.0f);
            send.mode = group || ! standard ? (int) SendMode::Post : (int) SendMode::Pre;
        }
    }
    const auto& returns = e.getAux();
    for (const auto& s : returns)
    {
        s->params.recallSafe = false;
        s->params.mute = false;
        s->params.faderDb = 0.0f;
        s->mains[0].on = standard;
    }
    const auto& buses = e.getBuses();
    for (int m = 0; m < (int) buses.size(); ++m)
    {
        auto& s = *buses[(size_t) m];
        s.params.recallSafe = false;
        s.params.mute = false;
        s.params.pan = 0.0f;
        s.params.faderDb = m < kMixes && isGroupMix (m) ? minDb : 0.0f;
        s.setModel (ModelBlock::Eq, 0);
        s.params.eq().on = true;
        if (m < kMixes)
            for (const auto& step : s.getChain())
                if (step.isBlock() && step.getBlockKind() == BlockKind::Eq)
                {
                    s.setBlockModel (step.uid, models::findModel (models::BusEq, "SQGEQ"));
                    if (auto* geq = s.getBlock (step.uid).eq())
                        geq->on = true;
                }
    }
    for (const auto* list : { &e.getMains(), &e.getMatrices() })
        for (const auto& s : *list)
        {
            s->params.recallSafe = false;
            s->params.mute = false;
            s->params.faderDb = 0.0f;
            s->params.pan = 0.0f;
        }
    // The first FX units (p.123-124).
    {
        auto& fx = e.getFx();
        const juce::StringArray tokens = standard ? juce::StringArray { "SQSMRE", "SQSMRH", "SQTAP", "SQADT" } : juce::StringArray { "SQSMRE", "SQSMRE", "SQSMRE", "SQSMRE" };
        for (int i = 0; i < 8; ++i)
            fx.setModel (i, i < 4 ? models::findModel (models::Fx, tokens[i]) : 0);
    }
    // SoftKeys and the master fader.
    for (int k = 0; k < 8; ++k)
        soft::set (ctx.settings, k, standard ? (k < 6 ? soft::Assign { soft::MuteGroup, 0, k } : k == 6 ? soft::Assign {} : soft::Assign { soft::GlobalTap, 0, 0 })
                                             : soft::Assign { soft::MuteGroup, 0, k });
    prefs::set (ctx.settings, prefs::listen, ! standard);
    stereoPendingValid = false;
    mixStereoPendingValid = false;
    assignEditing = -1;
    if (onSurfaceChanged)
        onSurfaceChanged();
}

//==============================================================================
// Utility

void Display::buildUtility()
{
    addSideTabs ({ "General", "Data", "USB Utility", "SQ Drive", "Auto Mic Mixer" }, utilityTab);
    switch (utilityTab)
    {
        case 1:  buildData(); return;
        case 2:  buildUsbUtility(); return;
        case 3:  buildDrive(); return;
        case 4:  buildAmm(); return;
        default: break;
    }
    const auto body = bodyArea();
    {
        const float w = (body.getWidth() - 12.0f) / 4.0f;
        addTab ({ body.getX(), body.getY(), w, 24 }, "Diagnostics", generalTab, 0);
        addTab ({ body.getX() + w + 4.0f, body.getY(), w, 24 }, "Calibration", generalTab, 3);
        addTab ({ body.getX() + 2.0f * (w + 4.0f), body.getY(), w, 24 }, "MIDI", generalTab, 1);
        addTab ({ body.getX() + 3.0f * (w + 4.0f), body.getY(), w, 24 }, "Add-ons", generalTab, 2);
    }
    if (generalTab == 3)
    {
        buildCalibration();
        return;
    }
    if (generalTab == 2)
    {
        buildAddons();
        return;
    }
    if (generalTab != 1)
        return;
    // MIDI (p.116): the channels and the NRPN fader law; MMC above them.
    const auto setup = juce::StringArray::fromTokens (ctx.settings.getValue (SqMidi::setupKey, "1 0"), " ", {});
    const int channel = juce::jlimit (1, 16, setup[0].getIntValue()), audioTaper = setup[1].getIntValue();
    const auto write = [this] (int c, int law)
    {
        const auto text_ = juce::String (c) + " " + juce::String (law);
        ctx.settings.setValue (SqMidi::setupKey, text_);
        if (auto* midi = SqMidi::current())
            midi->setSetupText (text_);
        rebuild();
    };
    {
        add (Field::Header, { body.getX(), body.getY() + 36, body.getWidth(), 18 }, "MMC Controls");
        const std::tuple<const char*, juce::MidiMessage::MidiMachineControlCommand> mmc[] = {
            { "Back", juce::MidiMessage::mmc_rewind }, { "Stop", juce::MidiMessage::mmc_stop }, { "Play", juce::MidiMessage::mmc_play },
            { "Pause", juce::MidiMessage::mmc_pause }, { "Rec/Arm", juce::MidiMessage::mmc_recordStart }, { "Fwd", juce::MidiMessage::mmc_fastforward }
        };
        const float w = (body.getWidth() - 20.0f) / 6.0f;
        for (int i = 0; i < 6; ++i)
        {
            auto& f = add (Field::Button, { body.getX() + 10.0f + (float) i * w, body.getY() + 66, w - 8.0f, 44 }, std::get<0> (mmc[i]));
            f.press = [c = std::get<1> (mmc[i])]
            {
                if (auto* midi = SqMidi::current())
                    midi->sendMessage (juce::MidiMessage::midiMachineControlCommand (c));
            };
        }
    }
    auto& ch = add (Field::Button, { body.getX() + 12, body.getY() + 164, 180, 40 }, "MIDI Channel " + juce::String (channel));
    ch.press = [write, channel, audioTaper] { write (channel % 16 + 1, audioTaper); };
    add (Field::Button, { body.getX() + 204, body.getY() + 164, 150, 40 }, "DAW Channel " + juce::String (channel % 16 + 1)).enabled = false; // always the next (p.116)
    auto& law = add (Field::Button, { body.getX() + 366, body.getY() + 164, 180, 40 }, audioTaper != 0 ? "NRPN: Audio Taper" : "NRPN: Linear Taper");
    law.press = [write, channel, audioTaper] { write (channel, audioTaper != 0 ? 0 : 1); };
}

void Display::addList (juce::Rectangle<float> area, const juce::String& prefix, int count, int& first, int& picked, std::function<juce::String (int)> words)
{
    constexpr float pitch = 34.0f;
    const int visible = juce::jmax (1, (int) (area.getHeight() / pitch));
    picked = juce::jlimit (0, juce::jmax (0, count - 1), picked);
    first = juce::jlimit (0, juce::jmax (0, count - visible), first);
    lists.push_back ({ area, &first, false });
    auto* chosen = &picked;
    for (int i = 0; i < visible && first + i < count; ++i)
    {
        const int at = first + i;
        auto& row = add (Field::Button, { area.getX() + 2.0f, area.getY() + 2.0f + (float) i * pitch, area.getWidth() - 4.0f, pitch - 4.0f }, prefix + juce::String (at + 1));
        row.row = true;
        row.caption = words (at);
        row.lit = [chosen, at] { return *chosen == at; };
        row.press = [this, chosen, at]
        {
            *chosen = at;
            dataArmed = 0;
            rebuild();
        };
    }
}

void Display::addTransport (juce::Rectangle<float> area, const juce::String& prefix, std::function<void (int action)> run, std::function<bool (int action)> lit, bool withRecord, bool withSkip)
{
    // Left to right as pp.111-116 draw them: previous, stop, play / pause, record, next.
    struct Key
    {
        int action, glyph;
        const char* name;
    };
    std::vector<Key> keys;
    if (withSkip)
        keys.push_back ({ 0, 9, "Previous" });
    keys.push_back ({ 1, 6, "Stop" });
    keys.push_back ({ 2, 7, "Play" });
    if (withRecord)
        keys.push_back ({ 3, 8, "Record" });
    if (withSkip)
        keys.push_back ({ 4, 10, "Next" });
    const float w = area.getWidth() / (float) keys.size();
    for (size_t i = 0; i < keys.size(); ++i)
    {
        const int action = keys[i].action;
        auto& key = add (Field::Button, { area.getX() + (float) i * w + 2.0f, area.getY(), w - 4.0f, area.getHeight() }, prefix + " " + keys[i].name);
        key.glyph = keys[i].glyph;
        key.colour = action == 2 ? kGreenBar : juce::Colours::white;
        key.lit = [lit, action] { return lit (action); };
        key.press = [run, action] { run (action); };
    }
}

juce::Array<juce::File> Display::dataFiles (const char* extension) const
{
    auto files = getDataFolder().findChildFiles (juce::File::findFiles, false, "*" + juce::String (extension));
    files.sort();
    return files;
}

//==============================================================================
// Utility's Data (pp.80-83)

void Display::buildData()
{
    // The console keeps shows, scenes and libraries on a USB drive. Sidecar's "USB" is a
    // folder: its Library folder, where the other surfaces' LIBRARY keeps show files
    // (.lshow) and scenes (.lsnap). What replaces or removes something asks again: the key
    // turns into Confirm (the console asks in a pop-up).
    const auto body = bodyArea();
    {
        const char* const names[] = { "Shows", "Scenes", "Libraries", "Sync" };
        const float w = (body.getWidth() - 12.0f) / 4.0f;
        for (int t = 0; t < 4; ++t)
            addTab ({ body.getX() + (float) t * (w + 4.0f), body.getY(), w, 24 }, names[t], dataTab, t).press = [this, t]
            {
                dataTab = t;
                dataArmed = 0;
                dataFileFirst = dataFilePicked = 0;
                rebuild();
            };
    }
    auto& show = ctx.show;
    const juce::Rectangle<float> left (76.0f, 90.0f, 240.0f, 304.0f), right (436.0f, 90.0f, 240.0f, 304.0f);
    const auto armed = [this] (Field& key, int what, const juce::String& name)
    {
        key.caption = dataArmed == what ? juce::String ("Confirm") : name;
        key.colour = juce::Colours::orange;
        key.lit = [this, what] { return dataArmed == what; };
    };
    if (dataTab == 0)
    {
        // As p.80: the actions at the left, the chosen show's name, the shows at the right.
        const auto files = dataFiles (Show::fileExtension);
        add (Field::Header, right.withY (68.0f).withHeight (18.0f), "USB Shows");
        addList (right, "USB Show ", files.size(), dataFileFirst, dataFilePicked, [&files] (int i) { return files[i].getFileNameWithoutExtension(); });
        const bool any = ! files.isEmpty();
        const auto picked = any ? files[dataFilePicked] : juce::File();
        const auto caption = [this] (float y, float height, const char* words)
        {
            add (Field::Label, { 78.0f, y, 94.0f, height }, {}).caption = words;
            fields.back().centred = true;
        };
        caption (72.0f, 14.0f, "From USB");
        auto& recall = add (Field::Button, { 82.0f, 90.0f, 86.0f, 32.0f }, "Recall");
        armed (recall, 1, "Recall");
        recall.enabled = any && ctx.openShowFile != nullptr;
        recall.press = [this, picked]
        {
            if (dataArmed != 1)
            {
                dataArmed = 1;
                rebuild();
                return;
            }
            dataArmed = 0;
            rebuild();
            // Last: a show can bring another console, and with it another surface.
            const auto openShow = ctx.openShowFile;
            openShow (picked);
        };
        caption (160.0f, 14.0f, "To USB");
        auto& store = add (Field::Button, { 82.0f, 178.0f, 86.0f, 32.0f }, "Store New");
        store.enabled = ctx.saveShowTo != nullptr;
        store.press = [this]
        {
            const auto folder = getDataFolder();
            folder.createDirectory();
            const auto name = juce::File::createLegalFileName (ctx.show.getName());
            ctx.saveShowTo (folder.getNonexistentChildFile (name.isNotEmpty() ? name : juce::String ("Show"), Show::fileExtension, false));
            dataArmed = 0;
            rebuild();
        };
        caption (244.0f, 28.0f, "Existing USB Show");
        auto& overwrite = add (Field::Button, { 82.0f, 276.0f, 86.0f, 32.0f }, "Overwrite");
        armed (overwrite, 2, "Overwrite");
        overwrite.enabled = any && ctx.saveShowTo != nullptr;
        overwrite.press = [this, picked]
        {
            if (dataArmed == 2)
                ctx.saveShowTo (picked);
            dataArmed = dataArmed == 2 ? 0 : 2;
            rebuild();
        };
        add (Field::Header, { 180.0f, 68.0f, 248.0f, 18.0f }, "Name");
        add (Field::Label, { 180.0f, 148.0f, 248.0f, 14.0f }, {}).caption = "Name:";
        fields.back().centred = true;
        // Read only: the screen has no keyboard to rename with (the console's has).
        auto& name = add (Field::Value, { 190.0f, 166.0f, 228.0f, 26.0f }, "Show Name");
        name.hideTitle = true;
        name.live = [text_ = picked.getFileNameWithoutExtension()] { return text_; };
        add (Field::Label, { 186.0f, 286.0f, 236.0f, 100.0f }, {}).caption = "A show stores the current settings, all scenes and all libraries. USB here is Sidecar's Library folder.";
        fields.back().centred = true;
        return;
    }
    if (dataTab == 3)
    {
        // As p.83: Sync Mode is SQ-MixPad's way in, and there's no MixPad link here.
        add (Field::Label, { body.getX(), 284.0f, body.getWidth(), 16.0f }, {}).caption = "Connect to a device";
        fields.back().centred = true;
        add (Field::Button, { 334.0f, 322.0f, 84.0f, 32.0f }, "Sync Mode").enabled = false;
        return;
    }
    // Scenes (p.81) and Libraries (p.82): the console's at the left, the drive's at the
    // right, the keys that move one to the other between them.
    const bool scenes = dataTab == 1;
    add (Field::Header, left.withY (68.0f).withHeight (18.0f), scenes ? "SQ Scenes" : "SQ Libraries");
    add (Field::Header, right.withY (68.0f).withHeight (18.0f), scenes ? "USB Scenes" : "USB Libraries");
    // The four keys first, then by their places: a reference to a field is good only until the next is added.
    const size_t keysAt = fields.size();
    add (Field::Button, { 324.0f, 100.0f, 104.0f, 32.0f }, "To SQ");
    add (Field::Button, { 324.0f, 144.0f, 104.0f, 32.0f }, "Overwrite SQ");
    add (Field::Button, { 324.0f, 206.0f, 104.0f, 32.0f }, "To USB");
    add (Field::Button, { 324.0f, 320.0f, 104.0f, 32.0f }, "Delete");
    if (! scenes)
    {
        // Sidecar's SQ has no user libraries yet (its Library key recalls the factory ones).
        for (size_t k = 0; k < 4; ++k)
            fields[keysAt + k].enabled = false;
        add (Field::Label, { left.getX(), 200.0f, left.getWidth(), 30.0f }, {}).caption = "No user libraries";
        fields.back().centred = true;
        fields.back().enabled = false;
        return;
    }
    const auto files = dataFiles (Show::sceneFileExtension);
    const int count = show.getNumScenes();
    const bool anyFile = ! files.isEmpty(), anyScene = count > 0;
    dataFilePicked = juce::jlimit (0, juce::jmax (0, files.size() - 1), dataFilePicked);
    dataScenePicked = juce::jlimit (0, juce::jmax (0, count - 1), dataScenePicked);
    const auto picked = anyFile ? files[dataFilePicked] : juce::File();
    auto& toSq = fields[keysAt];
    auto& overwrite = fields[keysAt + 1];
    auto& toUsb = fields[keysAt + 2];
    auto& removeKey = fields[keysAt + 3];
    toSq.enabled = anyFile;
    toSq.press = [this, picked]
    {
        // A new scene after the others (the console wants an empty slot chosen: Sidecar's
        // scene list has none).
        juce::String error;
        if (const int at = ctx.show.importScene (picked, -1, error); at >= 0)
            dataScenePicked = at;
        else
            ctx.log.warning (error);
        dataArmed = 0;
        rebuild();
    };
    armed (overwrite, 4, "Overwrite SQ");
    overwrite.enabled = anyFile && anyScene;
    overwrite.press = [this, picked]
    {
        if (dataArmed == 4)
        {
            // In the chosen scene's place: the file's scene goes in before it, then it goes.
            juce::String error;
            if (const int at = ctx.show.importScene (picked, dataScenePicked, error); at >= 0)
                ctx.show.removeScene (at + 1);
            else
                ctx.log.warning (error);
        }
        dataArmed = dataArmed == 4 ? 0 : 4;
        rebuild();
    };
    toUsb.enabled = anyScene;
    toUsb.press = [this]
    {
        const auto scene = ctx.show.getScene (dataScenePicked);
        auto name = juce::File::createLegalFileName (scene.has_value() ? scene->name : juce::String());
        if (name.isEmpty())
            name = "Scene " + juce::String (dataScenePicked + 1);
        const auto folder = getDataFolder();
        folder.createDirectory();
        if (const auto result = ctx.show.exportScene (dataScenePicked, folder.getChildFile (name + Show::sceneFileExtension)); result.failed())
            ctx.log.warning (result.getErrorMessage());
        dataArmed = 0;
        rebuild();
    };
    armed (removeKey, 3, "Delete");
    removeKey.enabled = anyFile;
    removeKey.press = [this, picked]
    {
        // To the system's bin, not for good.
        if (dataArmed == 3 && ! picked.moveToTrash())
            ctx.log.warning ("Could not delete " + picked.getFileName());
        dataArmed = dataArmed == 3 ? 0 : 3;
        rebuild();
    };
    addList (left, "SQ Scene ", count, dataSceneFirst, dataScenePicked, [&show] (int i)
             {
                 const auto scene = show.getScene (i);
                 return juce::String (i + 1) + ") " + (scene.has_value() ? scene->name : juce::String());
             });
    addList (right, "USB Scene ", files.size(), dataFileFirst, dataFilePicked, [&files] (int i) { return files[i].getFileNameWithoutExtension(); });
}

//==============================================================================
// Utility's USB Utility (pp.8, 20) and SQ-Drive (pp.110-114)

void Display::buildUsbUtility()
{
    // The guide has no picture of this page: it says there are two status windows that read
    // "Ready", a Format key, and a Firmware Update tab (p.8). Sidecar's two drives are
    // folders: the USB recorder's (SQ-Drive's stereo files) and the multitrack recorder's
    // projects. There's nothing to format and no firmware file to load.
    const auto body = bodyArea();
    {
        const float w = (body.getWidth() - 4.0f) / 2.0f;
        addTab ({ body.getX(), body.getY(), w, 24 }, "Status/Format", usbTab, 0);
        addTab ({ body.getX() + w + 4.0f, body.getY(), w, 24 }, "Firmware Update", usbTab, 1);
    }
    if (usbTab == 1)
    {
        add (Field::Label, { 120.0f, 150.0f, 520.0f, 60.0f }, {}).caption = "Sidecar is updated as an app: there is no firmware file to load from here.";
        fields.back().centred = true;
        add (Field::Button, { 326.0f, 240.0f, 100.0f, 34.0f }, "Update").enabled = false;
        return;
    }
    const auto window = [this] (float x, const juce::String& title, std::function<juce::String()> status, const juce::File& folder, const juce::String& note)
    {
        add (Field::Header, { x, 72.0f, 288.0f, 18.0f }, title);
        auto& state = add (Field::Value, { x + 64.0f, 100.0f, 160.0f, 26.0f }, title + " Status");
        state.hideTitle = true;
        state.live = std::move (status);
        add (Field::Label, { x + 8.0f, 136.0f, 272.0f, 70.0f }, {}).caption = folder.getFullPathName();
        fields.back().centred = true;
        add (Field::Label, { x + 8.0f, 214.0f, 272.0f, 60.0f }, {}).caption = note;
        fields.back().centred = true;
    };
    auto& usb = ctx.engine.getUsbRecorder();
    window (84.0f, "SQ-Drive Stereo", [&usb]
            {
                if (! usb.getStorageFolder().isDirectory())
                    return juce::String ("Not found");
                return usb.isRecording() ? juce::String ("Recording") : usb.getPlayState() == UsbRecorder::PlayState::Playing ? juce::String ("Playing") : juce::String ("Ready");
            },
            usb.getStorageFolder(), "Sidecar's USB recorder: stereo files in a folder.");
    auto* session = ctx.recording;
    window (380.0f, "SQ-Drive Multitrack", [session]
            {
                if (session == nullptr)
                    return juce::String ("Not here");
                return session->isRecording() ? juce::String ("Recording") : session->isRolling() ? juce::String ("Playing") : juce::String ("Ready");
            },
            session != nullptr ? session->getProjectsFolder() : juce::File(), "Sidecar's multitrack recorder: a project a folder.");
    add (Field::Button, { 326.0f, 340.0f, 100.0f, 34.0f }, "Format").enabled = false;
}

void Display::driveStereoPlay (int action)
{
    auto& usb = ctx.engine.getUsbRecorder();
    switch (action)
    {
        case 0:  usb.previous(); break;
        case 1:  usb.stop(); break;
        case 2:
            if (usb.getPlayState() == UsbRecorder::PlayState::Stopped)
                usb.play();
            else
                usb.togglePlayPause();
            break;
        case 4:  usb.next(); break;
        default: break;
    }
    repaint();
}

void Display::driveStereoRecord (int action)
{
    // The console arms first, then Play starts the recording. Sidecar's USB recorder has no
    // armed state: Arm starts recording, and Start / Pause pauses it or goes on.
    auto& usb = ctx.engine.getUsbRecorder();
    const auto start = [this, &usb]
    {
        if (const auto result = usb.record(); result.failed())
            ctx.log.warning (result.getErrorMessage());
    };
    switch (action)
    {
        case 1: usb.stopRecording(); break;
        case 2:
            if (usb.isRecording())
                usb.toggleRecPause();
            else
                start();
            break;
        case 3:
            if (! usb.isRecording())
                start();
            break;
        case 5:
            if (usb.isRecording())
                usb.stopRecording();
            else
                start();
            break;
        default: break;
    }
    repaint();
}

void Display::driveMultitrack (int action)
{
    // Sidecar's multitrack recorder: Previous and Next go to its markers (the console's go
    // to another recording), Arm records its armed tracks.
    auto* session = ctx.recording;
    if (session == nullptr)
        return;
    const auto start = [this, session]
    {
        if (const auto result = session->record(); result.failed())
            ctx.log.warning (result.getErrorMessage());
    };
    switch (action)
    {
        case 0: session->jumpToMarker (false); break;
        case 1: session->stop(); break;
        case 2: session->togglePlay(); break;
        case 3:
            if (! session->isRecording())
                start();
            break;
        case 4: session->jumpToMarker (true); break;
        case 5:
            if (session->isRecording())
                session->stop();
            else
                start();
            break;
        default: break;
    }
    repaint();
}

void Display::buildDrive()
{
    const auto body = bodyArea();
    {
        const float w = (body.getWidth() - 4.0f) / 2.0f;
        addTab ({ body.getX(), body.getY(), w, 24 }, "Stereo", driveTab, 0);
        addTab ({ body.getX() + w + 4.0f, body.getY(), w, 24 }, "Multitrack", driveTab, 1);
    }
    const auto caption = [this] (juce::Rectangle<float> r, const char* words)
    {
        add (Field::Label, r, {}).caption = words;
        fields.back().centred = true;
    };
    const auto box = [this] (juce::Rectangle<float> r, const juce::String& title, std::function<juce::String()> words)
    {
        auto& v = add (Field::Value, r, title);
        v.hideTitle = true;
        v.live = std::move (words);
    };
    const auto readout = [this] (juce::Rectangle<float> r, std::function<juce::String()> words) { add (Field::Label, r, {}).live = std::move (words); };
    if (driveTab == 0)
    {
        // As p.111: recording in the top half, playback in the bottom half. Sidecar's USB
        // recorder is the drive: it records its RECORDER ports (the USB outputs 1 and 2 of
        // the console) and plays the files of its folder.
        auto& usb = ctx.engine.getUsbRecorder();
        const auto seconds = [&usb] (int64_t samples) { return (double) samples / juce::jmax (1.0, usb.getSampleRate()); };
        caption ({ 90.0f, 96.0f, 150.0f, 14.0f }, "Stereo Record");
        box ({ 90.0f, 112.0f, 150.0f, 26.0f }, "Stereo Record", [&usb]
             {
                 const auto file = usb.getRecFile();
                 return file != juce::File() ? file.getFileNameWithoutExtension() : juce::String ("-");
             });
        addTransport ({ 392.0f, 104.0f, 204.0f, 40.0f }, "Record", [this] (int a) { driveStereoRecord (a); },
                      [&usb] (int a) { return a == 3 ? usb.isRecording() : a == 2 && usb.getRecState() == UsbRecorder::RecState::Recording; }, true, false);
        readout ({ 336.0f, 160.0f, 190.0f, 14.0f }, [&usb, seconds] { return "Elapsed:   " + clockText (seconds (usb.getRecTime())); });
        readout ({ 536.0f, 160.0f, 138.0f, 14.0f }, [&usb]
                 {
                     const auto s = usb.getRecState();
                     return juce::String (s == UsbRecorder::RecState::Recording ? "Recording" : s == UsbRecorder::RecState::Paused ? "Paused" : s == UsbRecorder::RecState::Error ? "Error" : "Stopped");
                 });
        caption ({ 90.0f, 236.0f, 150.0f, 14.0f }, "Stereo Playback");
        box ({ 90.0f, 252.0f, 150.0f, 26.0f }, "Stereo Playback", [&usb]
             {
                 const auto file = usb.getCurrentFile();
                 return file != juce::File() ? file.getFileNameWithoutExtension() : juce::String();
             });
        // Play All, Play Single and Repeat All: the recorder's REPEAT and its "stop after
        // every file". It has no Repeat Single (p.112's fourth mode).
        const auto single = [&usb]
        {
            const auto queue = usb.getQueue();
            return ! queue.empty() && std::all_of (queue.begin(), queue.end(), [] (const UsbRecorder::Entry& e) { return e.stop; });
        };
        addValue ({ 90.0f, 290.0f, 150.0f, 44.0f }, "Playback Mode",
                  choiceBinding ("Playback Mode", 3, [&usb, single] { return usb.getRepeat() ? 2 : single() ? 1 : 0; },
                                 [&usb] (int v)
                                 {
                                     usb.setRepeat (v == 2);
                                     usb.setStopAll (v == 1);
                                 },
                                 [] (int v) { return juce::StringArray ({ "Play All", "Play Single", "Repeat All" })[v]; }));
        addTransport ({ 330.0f, 246.0f, 272.0f, 40.0f }, "Playback", [this] (int a) { driveStereoPlay (a); },
                      [&usb] (int a) { return a == 2 && usb.getPlayState() == UsbRecorder::PlayState::Playing; }, false, true);
        readout ({ 336.0f, 302.0f, 190.0f, 14.0f }, [&usb, seconds] { return "Elapsed:   " + clockText (seconds (usb.getPosition())); });
        readout ({ 536.0f, 302.0f, 138.0f, 14.0f }, [&usb, seconds] { return "Remaining:   " + clockText (seconds (juce::jmax ((int64_t) 0, usb.getLength() - usb.getPosition()))); });
        return;
    }
    // As p.113: the tracks' meters above, the recording and the transport below. Sidecar's
    // multitrack recorder is the drive: its project's tracks are the meters.
    auto* session = ctx.recording;
    if (session == nullptr)
    {
        caption ({ body.getX(), 190.0f, body.getWidth(), 30.0f }, "No multitrack recorder here");
        return;
    }
    const auto seconds = [session] (double samples) { return samples / juce::jmax (1.0, session->getRecorder().getSampleRate()); };
    caption ({ 90.0f, 240.0f, 150.0f, 14.0f }, "Multitrack Project");
    box ({ 90.0f, 256.0f, 150.0f, 26.0f }, "Multitrack Project", [session] { return session->getProject().getName(); });
    // The recorder's own modes (the console's four are for a folder of recordings).
    addValue ({ 90.0f, 292.0f, 150.0f, 44.0f }, "Playback Mode",
              choiceBinding ("Playback Mode", 3, [session] { return (int) session->getPlayMode(); }, [session] (int v) { session->setPlayMode ((daw::Session::PlayMode) v); },
                             [] (int v) { return juce::StringArray ({ "Play", "A > B", "Loop" })[v]; }));
    addTransport ({ 330.0f, 246.0f, 330.0f, 40.0f }, "Multitrack", [this] (int a) { driveMultitrack (a); },
                  [session] (int a) { return a == 3 ? session->isRecording() : a == 2 && session->isRolling(); }, true, true);
    readout ({ 336.0f, 302.0f, 190.0f, 14.0f }, [session, seconds] { return "Elapsed:   " + clockText (seconds ((double) session->getPosition())); });
    readout ({ 536.0f, 302.0f, 138.0f, 14.0f }, [session] { return session->getNumArmed() > 0 ? "Remaining:   " + clockText (session->getRecordSecondsLeft()) : juce::String ("Remaining:   -"); });
    readout ({ 336.0f, 322.0f, 330.0f, 14.0f }, [session] { return "Armed tracks:   " + juce::String (session->getNumArmed()); });
}

void Display::paintDrive (juce::Graphics& g)
{
    const auto meter = [&g] (juce::Rectangle<float> r, float db, const juce::String& name)
    {
        g.setColour (juce::Colour (0xff24598a));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (kLine.withAlpha (0.7f));
        g.drawRoundedRectangle (r, 2.0f, 0.8f);
        meterBar (g, r.reduced (r.getWidth() * 0.5f - 2.5f, 4.0f).withTrimmedBottom (r.getHeight() * 0.42f), juce::Decibels::decibelsToGain (db, -100.0f));
        juce::Graphics::ScopedSaveState state (g);
        const auto label = r.withTop (r.getBottom() - r.getHeight() * 0.42f);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, label.getCentreX(), label.getCentreY()));
        text (g, juce::Rectangle<float> (label.getHeight(), label.getWidth()).withCentre (label.getCentre()), name, 8.5f, juce::Colours::white, juce::Justification::centred);
    };
    if (driveTab == 0)
    {
        auto& usb = ctx.engine.getUsbRecorder();
        for (int half = 0; half < 2; ++half)
        {
            const float top = half == 0 ? 70.0f : 232.0f;
            // The halves' panels (p.111).
            g.setColour (kWell.withAlpha (0.25f));
            g.fillRoundedRectangle ({ 78.0f, top, 596.0f, half == 0 ? 156.0f : 162.0f }, 3.0f);
            for (int side = 0; side < 2; ++side)
                meter ({ 252.0f + (float) side * 36.0f, top + 4.0f, 32.0f, 146.0f }, half == 0 ? usb.getRecLevel (side) : usb.getPlayLevel (side),
                       half == 0 ? (side == 0 ? "USB 1" : "USB 2") : (side == 0 ? "USB1&2 L" : "USB1&2 R"));
        }
        // The playback position, along the bottom (p.111): to look at, not to drag.
        const juce::Rectangle<float> bar (336.0f, 372.0f, 330.0f, 5.0f);
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (bar, 2.5f);
        if (const auto length = usb.getLength(); length > 0)
        {
            const float at = juce::jlimit (0.0f, 1.0f, (float) ((double) usb.getPosition() / (double) length));
            g.setColour (juce::Colours::white);
            g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre ({ bar.getX() + bar.getWidth() * at, bar.getCentreY() }));
        }
        return;
    }
    auto* session = ctx.recording;
    if (session == nullptr)
        return;
    // 32 meters in two rows of 16 (p.113): the project's first 32 tracks.
    const auto tracks = session->getProject().getTracks();
    const float w = 596.0f / 16.0f;
    for (int i = 0; i < 32; ++i)
    {
        const juce::Rectangle<float> r (78.0f + (float) (i % 16) * w + 1.0f, 70.0f + (float) (i / 16) * 80.0f, w - 2.0f, 76.0f);
        if (i < (int) tracks.size())
        {
            meter (r, session->getTrackLevel (tracks[(size_t) i].id, 0), tracks[(size_t) i].name);
            continue;
        }
        g.setColour (juce::Colour (0xff24598a).withAlpha (0.4f));
        g.fillRoundedRectangle (r, 2.0f);
        text (g, r.withTop (r.getBottom() - 11.0f), juce::String (i + 1), 8.0f, juce::Colours::white.withAlpha (0.6f), juce::Justification::centred);
    }
}

void Display::buildCalibration()
{
    // p.125: the motor faders are set at five places. Sidecar's faders are drawn, so the
    // key is here to be seen.
    const auto body = bodyArea();
    add (Field::Header, { body.getX(), body.getY() + 36.0f, body.getWidth(), 18.0f }, "Fader Calibration");
    add (Field::Label, { 120.0f, 112.0f, 520.0f, 60.0f }, {}).caption = "The console sets its motor faders at +10dB, 0dB, -10dB, -30dB and -inf. Sidecar's faders are drawn: there is nothing to calibrate.";
    fields.back().centred = true;
    add (Field::Button, { 326.0f, 200.0f, 100.0f, 36.0f }, "Calibrate").enabled = false;
}

void Display::buildAddons()
{
    // As p.121: the list at the left, details at the right. Sidecar has every add-on on.
    static const char* const items[][2] = {
        { "MultiBD Compressor 4", "A 4-band compressor for an input or a mix." },
        { "MultiBD Compressor 3", "A 3-band compressor for an input or a mix." },
        { "DynEQ4", "A 4-band dynamic equaliser." },
        { "De-Esser", "De-essing with an automatic threshold." },
        { "Bucket Brigade", "A stereo analogue delay." },
        { "Echo", "A tape echo." },
        { "Hypabass", "A sub-harmonic synthesizer." },
        { "Opto", "Compressor library: an optical compressor." },
        { "16T", "Compressor library: a VCA RMS compressor." },
        { "16VU", "Compressor library: a VCA RMS compressor." },
        { "OptTronik", "Compressor library: an electro-optical levelling amplifier." },
        { "Mighty", "Compressor library: a transistor array compressor." },
        { "Peak Limiter 76", "Compressor library: a FET levelling amplifier." },
        { "Bus", "Compressor library: a VCA bus compressor." },
        { "Tube Stage", "Preamp library: a valve stage." },
        { "Proportional-Q", "GEQ library: wide bands narrowing towards full cut or boost." },
        { "Hybrid GEQ", "GEQ library: smooth boosts and clinical cuts." },
        { "Digi GEQ", "GEQ library: less interaction between bands." },
    };
    const auto body = bodyArea();
    const int count = (int) (sizeof (items) / sizeof (items[0]));
    addonSelected = juce::jlimit (0, count - 1, addonSelected);
    const int perColumn = 9;
    const float top = body.getY() + 32.0f, w = 148.0f, h = 30.0f;
    for (int i = 0; i < count; ++i)
    {
        auto& f = add (Field::Tab, { body.getX() + (float) (i / perColumn) * (w + 4.0f), top + (float) (i % perColumn) * h, w, h - 2.0f }, items[i][0]);
        f.lit = [this, i] { return addonSelected == i; };
        f.press = [this, i]
        {
            addonSelected = i;
            rebuild();
        };
    }
    const float px = body.getX() + 2.0f * (w + 4.0f) + 8.0f;
    auto& info = add (Field::Label, { px, top + 6, body.getRight() - px, 110 }, {});
    info.caption = juce::String (items[addonSelected][0]) + ": " + items[addonSelected][1] + " Enabled: Sidecar has every add-on.";
    add (Field::Button, { px, body.getBottom() - 42, 160, 32 }, "Enter Add-on Key").enabled = false;
}

void Display::buildAmm()
{
    // As pp.118-120: an AMM of up to 24 channels each, or one of 48 (the engine's limit is
    // higher). Sidecar's "Overview" also carries Input Assign's touch: one touch steps a
    // channel through the AMMs.
    auto* amm = ld::ld_engine_amm (ctx.engine.getHandle());
    amm->mode = 1; // D-Classic gain sharing (p.118)
    const auto body = bodyArea();
    {
        const float w = (body.getWidth() - 8.0f) / 3.0f;
        const char* const names[] = { "Overview", "Input Assign", "Setup" };
        for (int t = 0; t < 3; ++t)
            addTab ({ body.getX() + (float) t * (w + 4.0f), body.getY(), w, 24 }, names[t], ammTab, t);
    }
    const auto& channels = ctx.engine.getChannels();
    const float top = body.getY() + 32.0f;
    const int count = juce::jlimit (1, 2, amm->count.load() > 1 ? 2 : 1);
    const auto ammIn = [&channels] (int which)
    {
        for (const auto& c : channels)
            if (auto* a = ld::ld_strip_amm (c->getHandle()); a != nullptr && a->amm.load() == which && a->on.load())
                return true;
        return false;
    };
    const auto ammSet = [&channels] (int which, bool on)
    {
        for (const auto& c : channels)
            if (auto* a = ld::ld_strip_amm (c->getHandle()); a != nullptr && a->amm.load() == which)
                a->on = on;
    };
    if (ammTab == 2)
    {
        for (int i = 0; i < count; ++i)
        {
            auto& f = add (Field::Button, { body.getX() + 20.0f + (float) i * 120.0f, top + 12, 100, 34 }, "AMM " + juce::String (i + 1) + " In");
            f.colour = juce::Colours::white;
            f.lit = [ammIn, i] { return ammIn (i); };
            f.press = [ammIn, ammSet, i] { ammSet (i, ! ammIn (i)); };
        }
        add (Field::Button, { body.getX() + 20, top + 76, 120, 34 }, "Follow Fader").enabled = false; // the key follows nothing here
        addValue ({ body.getX() + 20, top + 136, 110, 48 }, "High Pass", bindFreq ("High Pass", amm->keyHpfHz, 20.0f, 2000.0f, 100.0f));
        addValue ({ body.getX() + 146, top + 136, 110, 48 }, "Low Pass", bindFreq ("Low Pass", amm->keyLpfHz, 1000.0f, 20000.0f, 10000.0f));
        return;
    }
    const float cw = body.getWidth() / 8.0f, ch = (body.getBottom() - top - 44.0f) / 6.0f;
    for (int i = 0; i < juce::jmin (48, (int) channels.size()); ++i)
    {
        juce::Rectangle<float> cell (body.getX() + (float) (i % 8) * cw, top + (float) (i / 8) * ch, cw, ch);
        auto* a = ld::ld_strip_amm (channels[(size_t) i]->getHandle());
        if (a == nullptr)
            continue;
        const auto name = channels[(size_t) i]->getName();
        auto& f = add (Field::Button, cell.reduced (2.0f, 2.0f), name);
        if (ammTab == 0)
        {
            f.caption = a->amm.load() >= 0 ? name + " (" + juce::String (a->amm.load() + 1) + ")" : name;
            f.colour = juce::Colours::lightgreen;
            f.enabled = a->amm.load() >= 0;
            f.lit = [a] { return a->on.load(); };
            f.press = [this, a, i]
            {
                a->on = ! a->on.load();
                ammPick = i;
                rebuild();
            };
        }
        else
        {
            f.caption = a->amm.load() >= 0 ? "AMM " + juce::String (a->amm.load() + 1) : name;
            f.colour = juce::Colours::lightgreen;
            f.lit = [a] { return a->amm.load() >= 0; };
            f.press = [this, a, count]
            {
                const int now = a->amm.load();
                a->amm = now + 1 >= count ? -1 : now + 1;
                a->on = true;
                rebuild();
            };
        }
    }
    const float y = body.getBottom() - 42.0f;
    if (ammTab == 0)
    {
        for (int i = 0; i < count; ++i)
        {
            auto& f = add (Field::Button, { body.getX() + 8.0f + (float) i * 96.0f, y, 88, 34 }, "AMM " + juce::String (i + 1) + " In");
            f.colour = juce::Colours::white;
            f.lit = [ammIn, i] { return ammIn (i); };
            f.press = [ammIn, ammSet, i] { ammSet (i, ! ammIn (i)); };
        }
        ammPick = juce::jlimit (0, 47, ammPick);
        if (auto* a = ld::ld_strip_amm (channels[(size_t) ammPick]->getHandle()))
            addValue ({ body.getRight() - 150, y - 8, 140, 44 }, channels[(size_t) ammPick]->getName() + " Priority", bindLinear ("Priority", a->priorityDb, -15.0f, 15.0f, " dB", 1, 0.0f));
        return;
    }
    addValue ({ body.getX() + 8, y - 8, 140, 44 }, "Number of AMMs",
              choiceBinding ("Number of AMMs", 2, [amm] { return amm->count.load() > 1 ? 1 : 0; },
                             [this, amm, &channels] (int v)
                             {
                                 amm->count = v == 1 ? 2 : 1;
                                 if (v == 0)
                                     for (const auto& c : channels)
                                         if (auto* a = ld::ld_strip_amm (c->getHandle()); a != nullptr && a->amm.load() > 0)
                                             a->amm = 0;
                                 rebuild();
                             },
                             [] (int v) { return v == 1 ? juce::String ("2 x 24") : juce::String ("1 x 48"); }));
    auto& clear = add (Field::Button, { body.getRight() - 110, y, 100, 34 }, "Clear");
    clear.press = [this, &channels]
    {
        for (const auto& c : channels)
            if (auto* a = ld::ld_strip_amm (c->getHandle()))
                a->amm = -1;
        rebuild();
    };
}

void Display::paintUtility (juce::Graphics& g)
{
    if (utilityTab == 3)
    {
        paintDrive (g);
        return;
    }
    if (utilityTab == 1 && dataTab == 3)
    {
        // The console and a tablet, in outline, arrows between them (p.83).
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        juce::Path console;
        console.startNewSubPath (196.0f, 240.0f);
        console.lineTo (222.0f, 152.0f);
        console.lineTo (348.0f, 152.0f);
        console.lineTo (374.0f, 240.0f);
        console.closeSubPath();
        console.addRectangle (248.0f, 164.0f, 74.0f, 22.0f);
        console.addRectangle (216.0f, 198.0f, 138.0f, 32.0f);
        g.strokePath (console, juce::PathStrokeType (1.4f));
        g.drawRoundedRectangle (526.0f, 152.0f, 60.0f, 88.0f, 4.0f, 1.4f);
        g.drawRect (532.0f, 160.0f, 48.0f, 68.0f, 1.0f);
        for (int i = 0; i < 4; ++i)
        {
            const float x = 416.0f + (float) i * 18.0f;
            juce::Path left, right;
            left.addTriangle (x + 8.0f, 168.0f, x, 173.0f, x + 8.0f, 178.0f);
            right.addTriangle (x, 216.0f, x + 8.0f, 221.0f, x, 226.0f);
            g.fillPath (left);
            g.fillPath (right);
        }
        return;
    }
    if (utilityTab != 0 || generalTab != 0)
        return;
    const auto body = bodyArea().withTrimmedTop (32.0f);
    g.setColour (kWell.withAlpha (0.6f));
    g.fillRoundedRectangle (body, 3.0f);
    auto r = body.reduced (14.0f, 10.0f);
    const auto counts = ctx.engine.getCounts();
    auto* device = ctx.deviceManager.getCurrentAudioDevice();
    const std::pair<juce::String, juce::String> lines[] = {
        { "Firmware Version:", "Sidecar (SQ-5 emulation)" },
        { "Sample Rate:", device != nullptr ? juce::String (device->getCurrentSampleRate(), 0) + " Hz" : juce::String ("no audio device") },
        { "Channels:", juce::String (counts.channels) + " inputs, " + juce::String (counts.buses) + " mixes, " + juce::String (counts.matrices) + " matrices" },
        { "DCAs / Mute Groups:", juce::String (counts.dcas) + " / " + juce::String (counts.muteGroups) },
        { "Bus Config:", juce::String (kMixes - groupCount()) + " Auxes, " + juce::String (groupCount()) + " Groups" },
    };
    for (const auto& [k, v] : lines)
    {
        auto line = r.removeFromTop (22.0f);
        text (g, line.removeFromLeft (180.0f), k, 13.0f, juce::Colours::white, juce::Justification::centredLeft, true);
        text (g, line, v, 13.0f, juce::Colours::white, juce::Justification::centredLeft);
    }
}

//==============================================================================
// I/O

void Display::buildIo()
{
    addSideTabs ({ "Inputs", "Outputs", "Tie Lines" }, ioTab);
    fields[2].enabled = false;

    // A grid of crosspoints: 14 sockets across, 11 rows, drag the rows up and down (p.23).
    const auto r = matrixArea();
    const int rows = ioTab == 0 ? 48 : outputRows, cols = ioTab == 0 ? 22 : 16;
    ioFirst = juce::jlimit (0, juce::jmax (0, rows - kIoRows), ioFirst);
    ioSocketFirst = juce::jlimit (0, juce::jmax (0, cols - kIoCols), ioSocketFirst);
    const float cw = r.getWidth() / (float) kIoCols, ch = r.getHeight() / (float) kIoRows;
    for (int row = 0; row < kIoRows && ioFirst + row < rows; ++row)
        for (int col = 0; col < kIoCols && ioSocketFirst + col < cols; ++col)
        {
            const int y = ioFirst + row, x = ioSocketFirst + col;
            auto& f = add (Field::Area, { r.getX() + (float) col * cw, r.getY() + (float) row * ch, cw, ch }, {});
            f.press = [this, x, y]
            {
                if (ioTab == 0)
                {
                    auto* s = ctx.engine.getChannels()[(size_t) y].get();
                    s->params.inputLeft = s->params.inputLeft.load() == x ? -1 : x;
                }
                else
                {
                    const int source = outputRowSource (y);
                    if (ctx.engine.getOutputSource (x) == source)
                        ctx.engine.clearOutput (x);
                    else
                        ctx.engine.routeOutputSource (x, source);
                }
            };
        }
}

juce::Rectangle<float> Display::matrixArea() const
{
    const auto body = bodyArea();
    return { body.getX() + 76.0f, body.getY() + 46.0f, body.getWidth() - 76.0f, body.getHeight() - 46.0f };
}

void Display::paintIo (juce::Graphics& g)
{
    const auto r = matrixArea();
    const float cw = r.getWidth() / (float) kIoCols, ch = r.getHeight() / (float) kIoRows;
    const auto body = bodyArea();
    // The socket types along the top: Local chosen, the others not modelled (p.24), and
    // the Patch key at the matrix's corner.
    {
        const juce::StringArray types = ioTab == 0 ? juce::StringArray { "Local", "SLink", "USB", "I/O Port" } : juce::StringArray { "Local", "SLink", "ME", "USB", "I/O Port" };
        const float tw = (r.getWidth() + 76.0f) / (float) types.size();
        for (int t = 0; t < types.size(); ++t)
        {
            juce::Rectangle<float> cell (body.getX() + (float) t * tw + 1.0f, body.getY(), tw - 2.0f, 20.0f);
            g.setColour (t == 0 ? juce::Colours::white : juce::Colour (0xff24598a).withAlpha (0.8f));
            g.fillRoundedRectangle (cell, 3.0f);
            text (g, cell, types[t], 12.0f, t == 0 ? juce::Colours::black : juce::Colours::white.withAlpha (0.45f), juce::Justification::centred, true);
        }
        juce::Rectangle<float> patch (body.getX(), r.getY() - 22.0f, 74.0f, 20.0f);
        g.setColour (kWell);
        g.fillRoundedRectangle (patch, 3.0f);
        text (g, patch, "Patch", 11.0f, juce::Colours::white.withAlpha (0.8f), juce::Justification::centred, true);
    }
    const int rows = ioTab == 0 ? 48 : outputRows;
    const int cols = ioTab == 0 ? 22 : 16;
    for (int col = 0; col < kIoCols && ioSocketFirst + col < cols; ++col)
    {
        juce::Rectangle<float> head (r.getX() + (float) col * cw, r.getY() - 22.0f, cw, 20.0f);
        text (g, head, juce::String (ioSocketFirst + col + 1), 11.0f, juce::Colours::white, juce::Justification::centred, true);
    }
    for (int row = 0; row < kIoRows && ioFirst + row < rows; ++row)
    {
        const int y = ioFirst + row;
        juce::Rectangle<float> label (body.getX(), r.getY() + (float) row * ch, 74.0f, ch);
        g.setColour (juce::Colour (0xff7d96bf));
        g.fillRect (label.reduced (0.0f, 0.6f));
        const auto name = ioTab == 0 ? ctx.engine.getChannels()[(size_t) y]->getName() : y < 2 ? juce::String (y == 0 ? "LR L" : "LR R") : ctx.engine.getBuses()[(size_t) (y - 2)]->getName();
        text (g, label, name, 11.0f, juce::Colours::white, juce::Justification::centred, true);
        const int source = ioTab == 1 ? outputRowSource (y) : -1;
        for (int col = 0; col < kIoCols && ioSocketFirst + col < cols; ++col)
        {
            const int x = ioSocketFirst + col;
            const bool on = ioTab == 0 ? ctx.engine.getChannels()[(size_t) y]->params.inputLeft.load() == x : ctx.engine.getOutputSource (x) == source;
            juce::Rectangle<float> cell (r.getX() + (float) col * cw, r.getY() + (float) row * ch, cw, ch);
            g.setColour (on ? kOnCell : kOffCell);
            g.fillRect (cell.reduced (0.7f));
        }
    }
}

} // namespace livedaw::ui::sq5
