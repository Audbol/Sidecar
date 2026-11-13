#include "ui/ChainSummary.h"

#include "engine/FxSlots.h"
#include "engine/Models.h"
#include "ui/DelayUnits.h"

namespace livedaw::ui
{

namespace
{
    bool filterOn (const FilterParams& f) { return f.lowCut.load() || f.highCut.load() || f.tool.load(); }
    bool insertOn (const InsertParams& i) { return i.on.load() && i.fx.load() >= 0; }
} // namespace

juce::String stepName (const Strip& strip, const ChainStep& s)
{
    if (s.isBlock() && s.getBlockKind() == BlockKind::Plugin)
    {
        // The plugin's name (a plugin that could not be loaded keeps the one it was saved with).
        const auto info = FxSlots (strip.getEngineHandle()).describeBlock (strip.getHandle(), s.uid);
        if (strip.isBlockHostRack (s.uid)) // a PLUGIN HOST rack (the Universal Mixer's device)
            return info.name.isNotEmpty() ? "RACK: " + info.name.toUpperCase() : juce::String ("PLUGIN HOST RACK");
        return info.name.isNotEmpty() ? "PLUG-IN: " + info.name.toUpperCase() : stepName (s);
    }
    return s.isBlock() && s.uid == ld::ld_strip_preamp_block (strip.getHandle()) ? juce::String ("PREAMP") : stepName (s);
}

juce::String stepName (const ChainStep& s)
{
    switch (s.kind)
    {
        case ld::kStepInput:      return "INPUT";
        case ld::kStepDelay:      return "DELAY";
        case ld::kStepFilter:     return "FILTER";
        case ld::kStepAttenuator: return "ATT";
        case ld::kStepGate:       return "GATE";
        case ld::kStepEq:         return "EQ";
        case ld::kStepDynamics:   return "DYNAMICS";
        case ld::kStepPreInsert:  return "INSERT 1";
        case ld::kStepPostInsert: return "INSERT 2";
        case ld::kStepPfl:        return "PRE-FADER";
        case ld::kStepFader:      return "FADER";
        case ld::kStepPost:       return "POST";
        case ld::kStepTapShaper:  return "TAP EQ";
        case ld::kStepTap:        return s.isUserTap() ? "TAP " + juce::String (s.arg - ld::kFirstUserTap + 1) : juce::String ("TAP");
        case ld::kStepBlock:
            switch (s.getBlockKind())
            {
                case BlockKind::Filter:   return "FILTER BLOCK";
                case BlockKind::Eq:       return "EQ BLOCK";
                case BlockKind::Dynamics: return "DYNAMICS BLOCK";
                case BlockKind::Insert:   return "INSERT BLOCK";
                case BlockKind::Delay:    return "DELAY BLOCK";
                case BlockKind::Plugin:   return "PLUG-IN BLOCK";
            }
            break;
        case ld::kStepSlot:
        case ld::kStepRta:
            break;
    }
    return {};
}

bool stepOn (const Strip& strip, const ChainStep& s)
{
    auto& p = strip.params;
    switch (s.kind)
    {
        case ld::kStepFilter:     return filterOn (p.filter());
        case ld::kStepGate:       return p.gate().on.load();
        case ld::kStepEq:         return p.eq().on.load();
        case ld::kStepDynamics:   return p.dynamics().on.load();
        case ld::kStepPreInsert:  return insertOn (p.preInsert());
        case ld::kStepPostInsert: return insertOn (p.postInsert());
        case ld::kStepDelay:      return p.input.delayOn.load();
        case ld::kStepBlock:
        {
            const auto b = strip.getBlock (s.uid);
            if (auto* f = b.filter())
                return filterOn (*f);
            if (auto* e = b.eq())
                return e->on.load();
            if (auto* d = b.dynamics())
                return d->on.load();
            if (auto* i = b.insert())
                return insertOn (*i);
            if (auto* d = b.delay())
                return d->on.load();
            if (auto* p = b.plugin())
                return p->load();
            return false;
        }
        case ld::kStepInput:
        case ld::kStepSlot:
        case ld::kStepPfl:
        case ld::kStepFader:
        case ld::kStepPost:
        case ld::kStepTapShaper:
        case ld::kStepTap:
        case ld::kStepRta:
            break;
    }
    return false;
}

bool isMarker (const ChainStep& s) noexcept
{
    return s.kind == ld::kStepPfl || s.kind == ld::kStepFader || s.kind == ld::kStepPost;
}

bool isShown (const ChainStep& s) noexcept
{
    switch (s.kind)
    {
        case ld::kStepTap:
            return s.isUserTap();
        case ld::kStepRta:
        case ld::kStepSlot:
        case ld::kStepTapShaper:
            return false;
        case ld::kStepInput:
        case ld::kStepDelay:
        case ld::kStepFilter:
        case ld::kStepGate:
        case ld::kStepEq:
        case ld::kStepDynamics:
        case ld::kStepPreInsert:
        case ld::kStepPostInsert:
        case ld::kStepPfl:
        case ld::kStepFader:
        case ld::kStepPost:
        case ld::kStepBlock:
            break;
    }
    return true;
}

juce::String blockDetail (const Strip& strip, const ChainStep& s)
{
    const auto modelName = [] (models::Table table, int model) { return models::model (table, model).name.toUpperCase(); };
    const auto filterText = [&modelName] (const FilterParams& f)
    {
        juce::StringArray parts;
        if (f.lowCut.load())
            parts.add ("LO CUT " + juce::String (juce::roundToInt (f.lowCutFreq.load())) + " Hz");
        if (f.highCut.load())
            parts.add ("HI CUT " + juce::String (juce::roundToInt (f.highCutFreq.load())) + " Hz");
        if (f.tool.load())
            parts.add (modelName (models::FilterTools, f.toolModel.load()));
        return parts.isEmpty() ? juce::String ("NO CUT") : parts.joinIntoString ("  ");
    };
    const auto eqTable = static_cast<models::Table> (strip.getEqTable());
    const auto insertText = [] (const InsertParams& i) { return i.fx.load() >= 0 ? "FX " + juce::String (i.fx.load() + 1) : juce::String ("NO FX"); };

    auto& p = strip.params;
    switch (s.kind)
    {
        case ld::kStepFilter:     return filterText (p.filter());
        case ld::kStepGate:       return modelName (models::Dynamics, p.gate().model.load());
        case ld::kStepEq:         return modelName (eqTable, p.eq().model.load());
        case ld::kStepDynamics:   return modelName (models::Dynamics, p.dynamics().model.load());
        case ld::kStepPreInsert:  return insertText (p.preInsert());
        case ld::kStepPostInsert: return insertText (p.postInsert());
        case ld::kStepDelay:      return delayText (p.input.delay.load(), p.input.delayMode.load());
        case ld::kStepBlock:
        {
            const auto b = strip.getBlock (s.uid);
            if (auto* f = b.filter())
                return filterText (*f);
            if (auto* e = b.eq())
                return modelName (eqTable, e->model.load());
            if (auto* d = b.dynamics())
                return modelName (models::Dynamics, d->model.load());
            if (auto* i = b.insert())
                return insertText (*i);
            if (auto* d = b.delay())
                return delayText (d->delay.load(), d->mode.load());
            return {};
        }
        case ld::kStepInput:
        case ld::kStepSlot:
        case ld::kStepPfl:
        case ld::kStepFader:
        case ld::kStepPost:
        case ld::kStepTapShaper:
        case ld::kStepTap:
        case ld::kStepRta:
            break;
    }
    return {};
}

int countExtras (const Strip& strip)
{
    int n = 0;
    for (const auto& s : strip.getChain())
        n += isExtra (s) ? 1 : 0;
    return n;
}

} // namespace livedaw::ui
