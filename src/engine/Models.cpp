#include "engine/Models.h"
#include "engine/Strip.h"

#include <array>
#include <atomic>
#include <cstring>
#include <map>

namespace livedaw::models
{

juce::String Param::display (float value) const
{
    return takeString (ld::ld_model_param_display (table, model, index, value));
}

float Param::toNormalised (float value) const
{
    return ld::ld_model_param_to_normalised (table, model, index, value);
}

float Param::fromNormalised (float position) const
{
    return ld::ld_model_param_from_normalised (table, model, index, position);
}

bool dynStaticCurve (int model, const float* values, int numValues, const float* inDb, float* outDb, int count)
{
    return ld::ld_dyn_static_curve (model, values, numValues, inDb, outDb, count);
}

bool modelResponse (Table t, int model, const float* values, int numValues, const float* freqs, float* outDb, int count)
{
    return ld::ld_model_response (t, model, values, numValues, freqs, outDb, count);
}

int Model::find (const juce::String& id) const noexcept
{
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].id == id)
            return (int) i;
    return -1;
}

juce::String consoleOf (const juce::String& token)
{
    // livedaw_dsp::universal::console_of: the console whose tables hold the token (the X32's,
    // the ProMix 01's, the 02R's, the SQ-5's, the 01V's, the dLive's, the DM2000's, the PRO2's,
    // the Q1202USB's); empty for the WING's. Kept per token: the models are a fixed set.
    static std::map<juce::String, juce::String> known;
    if (const auto it = known.find (token); it != known.end())
        return it->second;
    const auto utf8 = token.toRawUTF8();
    return known[token] = takeString (ld::ld_model_console (utf8, std::strlen (utf8)));
}

/** Table `t` as the shown console holds it (`foreign` false), or with every other console's
    models after its own (`foreign`: the tables' numbers + 10, which the Rust model calls take). */
static std::vector<Model> build (Table t, bool foreign = false)
{
    const int ownCount = foreign ? ld::ld_model_count (t) : 0;
    const int tableId = foreign ? (int) t + 10 : (int) t;
    std::vector<Model> models;
    const int n = ld::ld_model_count (tableId);
    models.reserve ((size_t) n);

    for (int m = 0; m < n; ++m)
    {
        Model model;
        model.table = t;
        model.index = m;
        model.token = takeString (ld::ld_model_text (t, m, 0));
        model.name = takeString (ld::ld_model_text (t, m, 1));
        // A Universal console holds every console's models: name the console
        // of those that aren't the WING's ("X32 Gate" beside "Wing Gate").
        // (On another console, the models past its own are the foreign ones: named by their console.)
        if (const auto console = consoleOf (model.token); console.isNotEmpty() && shownProfile() == ld::kProfileUniversal)
            model.name = console + " " + model.name;
        else if (foreign && m >= ownCount)
        {
            const auto prefix = console.isNotEmpty() ? console : juce::String ("WING");
            if (! model.name.startsWithIgnoreCase (prefix))
                model.name = prefix + " " + model.name; // ("WING EQ" already says it)
        }
        model.foreign = foreign && m >= ownCount;
        model.category = static_cast<Category> (juce::jlimit (0, 9, (int) ld::ld_model_category (t, m)));

        const int np = ld::ld_model_num_params (t, m);
        for (int p = 0; p < np; ++p)
        {
            ld::ParamInfo info {};
            ld::ld_model_param_info (t, m, p, &info);

            Param param;
            param.table = t;
            param.model = m;
            param.index = p;
            param.id = takeString (ld::ld_model_param_text (t, m, p, 0));
            param.label = takeString (ld::ld_model_param_text (t, m, p, 1));
            param.unit = takeString (ld::ld_model_param_text (t, m, p, 2));
            param.kind = static_cast<ParamKind> (juce::jlimit (0, 5, (int) info.kind));
            param.min = info.min;
            param.max = info.max;
            param.steps = info.steps;
            param.defaultValue = info.defaultValue;
            for (int c = 0; c < info.numChoices; ++c)
                param.choices.add (takeString (ld::ld_model_param_choice (t, m, p, c)));
            model.params.push_back (std::move (param));
        }

        models.push_back (std::move (model));
    }

    return models;
}

static const std::vector<Model>& tableOf (Table t, bool all)
{
    // The shown console's models (setShownProfile; the Rust side's model calls
    // follow it), built once per console profile.
    struct Set
    {
        bool built = false;
        std::array<std::vector<Model>, 6> tables;
        std::array<std::vector<Model>, 6> all; // with the other consoles' models after
    };
    static std::map<int, Set> sets;
    auto& set = sets[(int) shownProfile()];
    if (! set.built)
    {
        for (int i = 0; i < (int) set.tables.size(); ++i)
        {
            set.tables[(size_t) i] = build ((Table) i);
            // The strip's table is the same on every console: no foreign models follow it.
            set.all[(size_t) i] = shownProfile() == ld::kProfileUniversal || i == UfxStrip ? set.tables[(size_t) i] : build ((Table) i, true);
        }
        set.built = true;
    }
    return (all ? set.all : set.tables)[(size_t) juce::jlimit (0, 5, (int) t)];
}

const std::vector<Model>& table (Table t)
{
    return tableOf (t, false);
}

const std::vector<Model>& tableAll (Table t)
{
    return tableOf (t, true);
}

static std::atomic<bool> remoteHold { false };

void setRemoteHeld (bool held)
{
    remoteHold = held;
    ld::ld_models_hold_remote (held);
}

bool remoteHeld() noexcept
{
    return remoteHold;
}

const std::vector<Model>& offered (Table t)
{
    return remoteHeld() ? table (t) : tableAll (t);
}

const Model& model (Table t, int index)
{
    // A block may hold another console's model (past the console's own table).
    const auto& own = table (t);
    jassert (! own.empty());
    const auto& models = index >= (int) own.size() ? tableAll (t) : own;
    return models[(size_t) juce::jlimit (0, (int) models.size() - 1, index)];
}

int findModel (Table t, const juce::String& token)
{
    const auto& models = table (t);
    for (size_t i = 0; i < models.size(); ++i)
        if (models[i].token == token)
            return (int) i;
    return -1;
}

const std::vector<FxMenuTab>& fxMenuTabs()
{
    static const std::vector<FxMenuTab> tabs = []
    {
        using Rows = std::vector<std::vector<const char*>>;
        auto tab = [] (const char* title, const char* subtitle, bool premium, const Rows& rows)
        {
            FxMenuTab t { title, subtitle, premium, {} };
            for (const auto& row : rows)
            {
                auto& r = t.rows.emplace_back();
                for (const auto* token : row)
                    if (const int i = findModel (Fx, token); i >= 0)
                        r.push_back (i);
            }
            return t;
        };
        // The console's order: reverbs, the vintage units, special reverbs,
        // modulation, delays and pitch; then the standard units by family.
        return std::vector<FxMenuTab> {
            tab ("FX1-8", "", true,
                 { { "HALL", "ROOM", "CHAMBER", "PLATE", "CONCERT", "AMBI" },
                   { "VSS3", "V-ROOM", "V-REV", "V-PLATE", "BPLATE" },
                   { "GATED", "REVERSE", "DEL/REV", "SHIMMER", "SPRING" },
                   { "DIMCRS", "CHORUS", "FLANGER" },
                   { "ST-DL", "TAP-DL", "TAPE-DL", "OILCAN", "BBD-DL" },
                   { "PITCH", "D-PITCH" } }),
            tab ("STD", "ALL FX", false,
                 { { "GEQ", "PIA", "SPKMAN" },
                   { "DEQ3", "C5-CMB", "LIMITER" },
                   { "DE-S2", "ENHANCE", "EXCITER", "P-BASS", "SUB", "SUB-M" },
                   { "V-IMG", "DOUBLE", "PCORR" },
                   { "ROTARY", "PHASER", "PANNER", "TAPE", "MOOD" },
                   { "BODY", "RACKAMP", "UKROCK", "ANGEL", "JAZZC", "DELUXE" } }),
            tab ("CH", "ALL FX", false,
                 { { "SOUL", "E88", "E84", "F110", "PULSAR", "MACH4" },
                   { "*EVEN*", "*SOUL*", "*VINTAGE*", "*BUS*", "*MASTER*" } }),
        };
    }();
    return tabs;
}

juce::StringArray list (List l)
{
    juce::StringArray items;
    const int n = ld::ld_list_count (l);
    for (int i = 0; i < n; ++i)
        items.add (takeString (ld::ld_list_item (l, i)));
    return items;
}

} // namespace livedaw::models
