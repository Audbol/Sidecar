#include "engine/Benchmark.h"

#include "engine/AppPaths.h"
#include "engine/Models.h"

#include <algorithm>
#include <cmath>

namespace livedaw::bench
{

namespace
{
    constexpr const char* kProfileNames[] = { "Universal", "WING", "X32", "ProMix 01", "02R", "SQ-5", "01V", "dLive", "DM2000", "PRO2", "Q1202USB" };

    double nowSeconds() { return juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks()); }

    double percentile99 (std::vector<float>& v)
    {
        if (v.empty())
            return 0;
        std::sort (v.begin(), v.end());
        return v[(size_t) juce::jlimit (0, (int) v.size() - 1, (int) std::ceil (0.99 * (double) v.size()) - 1)];
    }

    /** Up to `n` entries of `all`, evenly spread, the first and the last among them (n <= 0: all). */
    std::vector<int> sample (const std::vector<int>& all, int n)
    {
        if (n <= 0 || (int) all.size() <= n)
            return all;
        std::vector<int> out;
        for (int i = 0; i < n; ++i)
            out.push_back (all[(size_t) juce::roundToInt ((double) i * (double) (all.size() - 1) / (double) juce::jmax (1, n - 1))]);
        out.erase (std::unique (out.begin(), out.end()), out.end());
        return out;
    }

    juce::String modelName (int table, int model)
    {
        return takeString (ld::ld_model_text (table, model, 1));
    }

    std::vector<int> dynamicsModels (int slot)
    {
        std::vector<int> v;
        for (int m = 0, n = ld::ld_model_count (models::Dynamics); m < n; ++m)
            if (ld::ld_dyn_model_allowed (m, slot))
                v.push_back (m);
        return v;
    }

    std::vector<int> fxModels (int slot)
    {
        std::vector<int> v;
        for (int m = 0, n = ld::ld_model_count (models::Fx); m < n; ++m)
        {
            const auto token = takeString (ld::ld_model_text (models::Fx, m, 0));
            if (token != "NONE" && token != "EXT" && ld::ld_model_fx_allowed (m, slot))
                v.push_back (m);
        }
        return v;
    }

    void wireInputs (MixerEngine& e, int count)
    {
        const auto& chs = e.getChannels();
        for (int i = 0; i < (int) chs.size(); ++i)
            chs[(size_t) i]->params.inputLeft = i < count ? i : -1;
    }

    /** What a console's channel usually runs: low cut, gate, EQ and compressor. */
    void typicalProcessing (MixerEngine& e, int count, int gateModel, int dynModel)
    {
        const auto& chs = e.getChannels();
        for (int i = 0; i < count && i < (int) chs.size(); ++i)
        {
            auto& s = *chs[(size_t) i];
            s.params.filter().lowCut = true;
            if (gateModel >= 0)
                s.setModel (ModelBlock::Gate, gateModel);
            s.params.gate().on = gateModel >= 0;
            s.params.eq().on = true;
            if (dynModel >= 0)
                s.setModel (ModelBlock::Dynamics, dynModel);
            s.params.dynamics().on = dynModel >= 0;
        }
    }

    juce::String fixed (double v, int places = 1) { return places == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, places); }
} // namespace

const char* profileName (ld::ProfileId p)
{
    return (int) p >= 0 && (int) p < (int) std::size (kProfileNames) ? kProfileNames[(int) p] : "Console";
}

//==============================================================================
const Measure* Result::find (const juce::String& group, const juce::String& name, int blockSize) const
{
    for (const auto& m : measures)
        if (m.group == group && m.name == name && (blockSize == 0 || m.blockSize == blockSize))
            return &m;
    return nullptr;
}

juce::String Result::title() const
{
    return when.formatted ("%Y-%m-%d %H:%M") + "  " + juce::String (juce::roundToInt (settings.sampleRate)) + " Hz / " + juce::String (settings.blockSize);
}

namespace
{
    // The reference figures: a full run on the development PC (Intel i5-9600K, 6 cores, 32 GB, Windows 11), which counts as 1000.
    constexpr double kRefChannels = 73.5;     // channels of processing at 128 samples / 48 kHz, geometric mean over the consoles
    constexpr double kRefEffects = 196.3;     // one effect's cost as a share of the block budget, inverted: how many fit (geometric mean over the effects tested)
    constexpr double kRefTracks = 64.0;       // tracks recorded at once
    constexpr double kRefFps = 34.7;           // interface frames per second
    constexpr double kWeights[4] = { 0.6, 0.2, 0.1, 0.1 }; // DSP, effects, recording, interface
}

juce::String Score::rating() const
{
    if (! valid())
        return {};
    return mark >= 1200 ? "Plenty for a large show" : mark >= 500 ? "Fine for small to medium shows" : "Use larger buffers";
}

Score computeScore (const Result& r)
{
    Score s;
    s.estimate = r.settings.quick;
    const double rateScale = r.settings.sampleRate / 48000.0; // a higher rate leaves less time per block
    double logSum = 0;
    int n = 0;
    for (const auto& m : r.measures)
        if (m.group == "Console" && m.blockSize == 128 && m.value > 0)
        {
            logSum += std::log (m.value);
            ++n;
        }
    if (n > 0)
        s.raw[0] = std::exp (logSum / n) * rateScale;
    logSum = 0;
    n = 0;
    for (const auto& m : r.measures)
        if (m.group == "Effect" && m.blockSize > 0 && m.meanMs > 0)
        {
            logSum += std::log (m.blockSize / r.settings.sampleRate * 1000.0 / m.meanMs);
            ++n;
        }
    if (n > 0)
        s.raw[1] = std::exp (logSum / n) * rateScale;
    for (const auto& m : r.measures)
    {
        if (m.group == "Recording")
            s.raw[2] = juce::jmax (s.raw[2], m.value);
        if (m.group == "Interface" && m.name.startsWith ("Universal"))
            s.raw[3] = m.value;
    }
    const double ref[4] = { kRefChannels, kRefEffects, kRefTracks, kRefFps };
    double part[4] = { 0, 0, 0, 0 };
    double weighted = 0, weights = 0;
    for (int i = 0; i < 4; ++i)
        if (s.raw[i] > 0)
        {
            const auto ratio = juce::jlimit (0.001, 1000.0, s.raw[i] / ref[i]);
            part[i] = 1000.0 * ratio;
            weighted += kWeights[i] * std::log (ratio);
            weights += kWeights[i];
        }
    s.dsp = part[0];
    s.effects = part[1];
    s.recording = part[2];
    s.interface = part[3];
    if (s.dsp > 0) // without the DSP part there is no Mark
        s.mark = 1000.0 * std::exp (weighted / weights);
    return s;
}

void Result::finish()
{
    score = computeScore (*this);
    summary.clear();
    recommendations.clear();
    const auto bs = settings.blockSize;
    const auto budget = bs / settings.sampleRate * 1000.0;
    const auto at = juce::String (bs) + " samples (" + fixed (budget, 2) + " ms)";

    for (const auto& m : measures)
        if (m.group == "Console" && m.blockSize == bs)
        {
            const auto more = m.note.startsWith ("at least") ? "at least " : "about ";
            summary.add ("This PC can run " + juce::String (more) + juce::String ((int) m.value) + " channels of " + m.name + " processing at " + at + ".");
        }

    // The current console with all its effects: the smallest tested size that holds it.
    const juce::String current = profileName (settings.currentProfile);
    std::vector<const Measure*> withFx;
    for (const auto& m : measures)
        if (m.group == "Console + effects" && m.name == current)
            withFx.push_back (&m);
    std::sort (withFx.begin(), withFx.end(), [] (auto* a, auto* b) { return a->blockSize < b->blockSize; });
    if (! withFx.empty())
    {
        const Measure* ok = nullptr;
        for (auto* m : withFx)
            if (m->p99Pct <= settings.limitPct)
            {
                ok = m;
                break;
            }
        if (ok != nullptr && ok->blockSize <= bs)
            summary.add ("For the " + current + " with all its effects, " + juce::String (bs) + " samples is fine.");
        else if (ok != nullptr)
            summary.add ("For the " + current + " with all its effects, use " + juce::String (ok->blockSize) + " samples.");
        else
            summary.add ("For the " + current + " with all its effects, none of the tested buffer sizes stays below " + juce::String ((int) settings.limitPct) + "% of the budget.");
    }

    int bestTracks = 0;
    bool recorded = false, recordFailed = false;
    for (const auto& m : measures)
        if (m.group == "Recording")
        {
            recorded = true;
            bestTracks = juce::jmax (bestTracks, (int) m.value);
            recordFailed = recordFailed || m.value <= 0;
        }
    if (recorded)
        summary.add (bestTracks > 0 ? "Recording " + juce::String (bestTracks) + " tracks is fine." + (recordFailed ? " More than that did not keep up." : juce::String())
                                    : "Recording to this disk did not keep up even with the fewest tracks tested.");

    for (const auto& m : measures)
        if (m.group == "Interface" && m.name.startsWith ("Universal"))
            summary.add ("The interface draws " + fixed (m.value, 0) + " frames per second" + (m.value >= 30 ? "." : ": it will look choppy."));
    for (const auto& m : measures)
        if (m.group == "Plug-in" && m.p99Pct > 50)
            summary.add ("The plug-in " + m.name + " takes " + fixed (m.p99Pct, 0) + "% of a block.");
    if (cancelled)
        summary.insert (0, "The run was cancelled: only the tests that finished are shown.");
    if (summary.isEmpty())
        summary.add ("No test finished.");

    // Recommendations.
    if (! withFx.empty())
    {
        const auto* ok = withFx.front();
        for (auto* m : withFx)
            if (m->p99Pct <= settings.limitPct)
            {
                ok = m;
                break;
            }
        if (ok->p99Pct <= settings.limitPct)
            recommendations.add ("Buffer size: " + juce::String (ok->blockSize) + " samples (" + fixed (ok->blockSize / settings.sampleRate * 1000.0, 2) + " ms) is the smallest tested that runs the " + current
                                 + " with all its effects with room to spare. Smaller sizes may glitch; larger ones add delay.");
        else
            recommendations.add ("Buffer size: use the largest size tested, run fewer channels, or switch off effects you do not need.");
    }
    for (const auto& m : measures)
        if (m.group == "Console" && m.blockSize == bs && m.note.isEmpty() && m.value < 32)
            recommendations.add ("Channels: " + m.name + " fits only about " + juce::String ((int) m.value) + " channels here; use only the channels you need or a larger buffer.");
    std::vector<const Measure*> heavy;
    for (const auto& m : measures)
        if ((m.group == "Block" || m.group == "Effect") && m.blockSize == bs)
            heavy.push_back (&m);
    std::sort (heavy.begin(), heavy.end(), [] (auto* a, auto* b) { return a->p99Pct > b->p99Pct; });
    if (! heavy.empty())
    {
        juce::StringArray names;
        for (size_t i = 0; i < heavy.size() && i < 3; ++i)
            names.add (heavy[i]->name + " (" + heavy[i]->group + ", " + fixed (heavy[i]->p99Pct, 2) + "% of a block each)");
        recommendations.add ("What to switch off first: the heaviest processing is " + names.joinIntoString ("; ") + ". Leave these off on strips that do not need them.");
    }
    if (recorded && recordFailed)
        recommendations.add ("Recording: record fewer tracks at once, or to a faster drive (SSD) in SETUP.");
    for (const auto& m : measures)
        if (m.group == "Interface" && m.value < 30)
            recommendations.add ("Interface: " + m.name + " runs at " + fixed (m.value, 0) + " frames per second; use a smaller window or close other programs.");
    recommendations.add ("Close other programs and plug-ins you do not use while mixing; the figures above were measured with this PC as it was during the run.");
}

juce::String Result::toText() const
{
    juce::String t;
    t << "Sidecar benchmark, " << when.toString (true, true) << "\n";
    t << "Sample rate " << settings.sampleRate << " Hz, buffer " << settings.blockSize << " samples, run took " << fixed (seconds, 0) << " s\n\n";
    for (int i = 0; i < machine.size(); ++i)
        t << machine.getAllKeys()[i] << ": " << machine.getAllValues()[i] << "\n";
    if (score.valid())
        t << "\nSIDECAR MARK" << (score.estimate ? " (estimate, from the quick test)" : "") << ": " << juce::roundToInt (score.mark) << "  (" << score.rating() << ")\n"
          << "DSP " << juce::roundToInt (score.dsp) << ", Effects " << juce::roundToInt (score.effects) << ", Recording " << juce::roundToInt (score.recording)
          << ", Interface " << juce::roundToInt (score.interface) << "\n";
    t << "\nSUMMARY\n";
    for (const auto& s : summary)
        t << s << "\n";
    t << "\nRECOMMENDATIONS\n";
    for (const auto& s : recommendations)
        t << "- " << s << "\n";
    t << "\nDETAILS (% of the block's time budget; per strip where strips is above 1)\n";
    juce::String group;
    for (const auto& m : measures)
    {
        if (m.group != group)
            t << "\n[" << (group = m.group) << "]\n";
        t << m.name.paddedRight (' ', 34) << " buf " << juce::String (m.blockSize).paddedLeft (' ', 5) << "  mean " << fixed (m.meanPct, 3).paddedLeft (' ', 9) << "  p99 "
          << fixed (m.p99Pct, 3).paddedLeft (' ', 9) << "  worst " << fixed (m.worstPct, 3).paddedLeft (' ', 9) << "  value " << fixed (m.value, 1) << "  " << m.note << "\n";
    }
    return t;
}

juce::String Result::toCsv() const
{
    juce::String c = "group,name,block_size,strips,mean_pct,p99_pct,worst_pct,mean_ms,value,note\n";
    if (score.valid())
    {
        c << "\"Score\",\"Sidecar Mark" << (score.estimate ? " (estimate)" : "") << "\",0,1,0,0,0,0," << juce::roundToInt (score.mark) << ",\"" << score.rating() << "\"\n";
        const std::pair<const char*, double> parts[] = { { "DSP", score.dsp }, { "Effects", score.effects }, { "Recording", score.recording }, { "Interface", score.interface } };
        for (auto& p : parts)
            c << "\"Score\",\"" << p.first << "\",0,1,0,0,0,0," << juce::roundToInt (p.second) << ",\"\"\n";
    }
    for (const auto& m : measures)
        c << "\"" << m.group << "\",\"" << m.name.replace ("\"", "'") << "\"," << m.blockSize << "," << m.count << "," << m.meanPct << "," << m.p99Pct << "," << m.worstPct << "," << m.meanMs
          << "," << m.value << ",\"" << m.note.replace ("\"", "'") << "\"\n";
    return c;
}

juce::var Result::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("when", when.toMilliseconds());
    o->setProperty ("sampleRate", settings.sampleRate);
    o->setProperty ("blockSize", settings.blockSize);
    o->setProperty ("profile", (int) settings.currentProfile);
    o->setProperty ("limitPct", settings.limitPct);
    o->setProperty ("deviceInfo", settings.deviceInfo);
    o->setProperty ("cancelled", cancelled);
    o->setProperty ("seconds", seconds);
    o->setProperty ("score", juce::Array<juce::var> { score.mark, score.dsp, score.effects, score.recording, score.interface, score.estimate, score.raw[0], score.raw[1], score.raw[2], score.raw[3] });
    juce::Array<juce::var> mach, ms, sum, rec;
    for (int i = 0; i < machine.size(); ++i)
        mach.add (juce::StringArray { machine.getAllKeys()[i], machine.getAllValues()[i] }.joinIntoString ("\t"));
    for (const auto& m : measures)
    {
        auto* e = new juce::DynamicObject();
        e->setProperty ("g", m.group);
        e->setProperty ("n", m.name);
        e->setProperty ("b", m.blockSize);
        e->setProperty ("c", m.count);
        e->setProperty ("mean", m.meanPct);
        e->setProperty ("p99", m.p99Pct);
        e->setProperty ("worst", m.worstPct);
        e->setProperty ("ms", m.meanMs);
        e->setProperty ("v", m.value);
        e->setProperty ("note", m.note);
        ms.add (juce::var (e));
    }
    for (const auto& s : summary)
        sum.add (s);
    for (const auto& s : recommendations)
        rec.add (s);
    o->setProperty ("machine", mach);
    o->setProperty ("measures", ms);
    o->setProperty ("summary", sum);
    o->setProperty ("recommendations", rec);
    return juce::var (o);
}

Result Result::fromVar (const juce::var& v)
{
    Result r;
    r.when = juce::Time ((juce::int64) v.getProperty ("when", 0));
    r.settings.sampleRate = v.getProperty ("sampleRate", 48000.0);
    r.settings.blockSize = v.getProperty ("blockSize", 128);
    r.settings.currentProfile = (ld::ProfileId) (int) v.getProperty ("profile", 0);
    r.settings.limitPct = v.getProperty ("limitPct", 70.0);
    r.settings.deviceInfo = v.getProperty ("deviceInfo", {}).toString();
    r.cancelled = v.getProperty ("cancelled", false);
    r.seconds = v.getProperty ("seconds", 0.0);
    if (auto* a = v.getProperty ("score", {}).getArray(); a != nullptr && a->size() == 10)
    {
        r.score.mark = (*a)[0];
        r.score.dsp = (*a)[1];
        r.score.effects = (*a)[2];
        r.score.recording = (*a)[3];
        r.score.interface = (*a)[4];
        r.score.estimate = (bool) (*a)[5];
        for (int i = 0; i < 4; ++i)
            r.score.raw[i] = (*a)[6 + i];
    }
    if (auto* a = v.getProperty ("machine", {}).getArray())
        for (auto& s : *a)
        {
            juce::StringArray kv;
            kv.addTokens (s.toString(), "\t", "");
            if (kv.size() == 2)
                r.machine.set (kv[0], kv[1]);
        }
    if (auto* a = v.getProperty ("measures", {}).getArray())
        for (auto& e : *a)
        {
            Measure m;
            m.group = e.getProperty ("g", {}).toString();
            m.name = e.getProperty ("n", {}).toString();
            m.blockSize = e.getProperty ("b", 0);
            m.count = e.getProperty ("c", 1);
            m.meanPct = e.getProperty ("mean", 0.0);
            m.p99Pct = e.getProperty ("p99", 0.0);
            m.worstPct = e.getProperty ("worst", 0.0);
            m.meanMs = e.getProperty ("ms", 0.0);
            m.value = e.getProperty ("v", 0.0);
            m.note = e.getProperty ("note", {}).toString();
            r.measures.push_back (m);
        }
    for (auto& l : { std::make_pair ("summary", &r.summary), std::make_pair ("recommendations", &r.recommendations) })
        if (auto* a = v.getProperty (l.first, {}).getArray())
            for (auto& s : *a)
                l.second->add (s.toString());
    return r;
}

juce::File runsFolder()
{
    return paths::appData().getChildFile ("Benchmarks");
}

juce::File save (const Result& r, const juce::File& folder)
{
    folder.createDirectory();
    auto file = folder.getChildFile (r.when.formatted ("%Y%m%d-%H%M%S") + ".json");
    for (int i = 2; file.exists(); ++i)
        file = folder.getChildFile (r.when.formatted ("%Y%m%d-%H%M%S") + "-" + juce::String (i) + ".json");
    return file.replaceWithText (juce::JSON::toString (r.toVar())) ? file : juce::File();
}

std::vector<Result> loadAll (const juce::File& folder)
{
    std::vector<Result> out;
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.json");
    files.sort();
    for (auto& f : files)
    {
        const auto v = juce::JSON::parse (f);
        if (v.isObject())
            out.push_back (Result::fromVar (v));
    }
    return out;
}

juce::StringPairArray machineDetails (const juce::String& deviceInfo)
{
    juce::StringPairArray m;
    m.set ("CPU", juce::SystemStats::getCpuModel());
    m.set ("Cores", juce::String (juce::SystemStats::getNumPhysicalCpus()) + " cores, " + juce::String (juce::SystemStats::getNumCpus()) + " threads");
    const auto mhz = juce::SystemStats::getCpuSpeedInMegahertz();
    m.set ("Clock", mhz > 0 ? fixed (mhz / 1000.0, 2) + " GHz" : juce::String ("not reported"));
    m.set ("Memory", fixed (juce::SystemStats::getMemorySizeInMegabytes() / 1024.0, 1) + " GB");
    m.set ("System", juce::SystemStats::getOperatingSystemName());
    m.set ("Audio device", deviceInfo.isNotEmpty() ? deviceInfo : juce::String ("none open"));
    return m;
}

//==============================================================================
Runner::Runner() : juce::Thread ("Sidecar benchmark") {}

Runner::~Runner()
{
    stopThread (20000);
}

void Runner::start (const Settings& s)
{
    stopThread (20000);
    settings = s;
    blockSize = s.blockSize;
    if (settings.profiles.empty())
        for (int p = 1; p <= (int) ld::kProfileXenyx1202; ++p)
            settings.profiles.push_back ((ld::ProfileId) p);
    result = Result();
    result.when = juce::Time::getCurrentTime();
    result.settings = settings;
    result.machine = machineDetails (settings.deviceInfo);
    done = false;
    progress = 0;
    sub = 0;
    graphHead = 0;
    graphTaken = 0;
    pendingLog.clear();
    currentTest = "Starting";
    startThread (Priority::low);
}

Runner::Live Runner::poll()
{
    Live l;
    {
        const juce::ScopedLock sl (lock);
        l.test = currentTest;
        l.newLog = pendingLog;
        pendingLog.clear();
    }
    l.progress = juce::jlimit (0.0, 1.0, progress.load() + sub.load());
    l.budgetMs = graphBudget.load();
    const int head = graphHead.load();
    const int n = juce::jmin (head, kGraphLength);
    for (int i = head - n; i < head; ++i)
        l.blockMs.push_back (graph[(size_t) (i % kGraphLength)].load());
    return l;
}

void Runner::setTest (const juce::String& t)
{
    const juce::ScopedLock sl (lock);
    currentTest = t;
}

void Runner::logLine (const juce::String& s)
{
    const juce::ScopedLock sl (lock);
    pendingLog.add (s);
}

Runner::Stats Runner::timeBlocks (MixerEngine& e, int numIn, int blocks, int warmup)
{
    const int ins = juce::jmax (1, numIn);
    std::vector<std::vector<float>> in ((size_t) ins, std::vector<float> ((size_t) blockSize));
    juce::Random rnd (7);
    for (auto& ch : in)
        for (auto& x : ch)
            x = (rnd.nextFloat() - 0.5f) * 0.5f;
    std::vector<const float*> inPtr;
    for (auto& ch : in)
        inPtr.push_back (ch.data());
    std::vector<float> l ((size_t) blockSize), r ((size_t) blockSize);
    float* outs[] = { l.data(), r.data() };
    std::vector<float> times;
    times.reserve ((size_t) blocks);
    graphBudget = budgetMs (blockSize);
    for (int b = 0; b < warmup + blocks && ! threadShouldExit(); ++b)
    {
        const auto t0 = juce::Time::getHighResolutionTicks();
        e.processBlock (inPtr.data(), ins, outs, 2, blockSize);
        const auto ms = (float) (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1000.0);
        if (b >= warmup)
        {
            times.push_back (ms);
            graph[(size_t) (graphHead.load() % kGraphLength)] = ms;
            graphHead.fetch_add (1);
        }
    }
    Stats s;
    if (times.empty())
        return s;
    double sum = 0;
    for (auto t : times)
        sum += t;
    s.mean = sum / (double) times.size();
    s.worst = *std::max_element (times.begin(), times.end());
    s.p99 = percentile99 (times);
    return s;
}

Measure Runner::toMeasure (const juce::String& group, const juce::String& name, int bs, const Stats& s, const Stats& base, int count) const
{
    const auto budget = budgetMs (bs);
    auto pct = [&] (double v, double b) { return juce::jmax (0.0, v - b) / (double) juce::jmax (1, count) / budget * 100.0; };
    Measure m;
    m.group = group;
    m.name = name;
    m.blockSize = bs;
    m.count = count;
    m.meanMs = juce::jmax (0.0, s.mean - base.mean) / (double) juce::jmax (1, count);
    m.meanPct = pct (s.mean, base.mean);
    m.p99Pct = pct (s.p99, base.p99);
    m.worstPct = pct (s.worst, base.worst);
    return m;
}

//==============================================================================
void Runner::blockTests (ld::ProfileId p)
{
    MixerEngine eng (0);
    eng.configureProfile (p);
    ld::ld_models_show_profile (p);
    const int k = juce::jmin (16, (int) eng.getChannels().size());
    if (k < 1)
        return;
    eng.prepareOffline (settings.sampleRate, blockSize, k, 2);
    wireInputs (eng, k);
    const juce::String pre = juce::String (profileName (p)) + " ";
    const auto base = timeBlocks (eng, k, settings.blocks, settings.warmup);
    const auto& chs = eng.getChannels();
    auto each = [&] (auto fn) { for (int i = 0; i < k; ++i) fn (*chs[(size_t) i]); };
    auto report = [&] (const juce::String& group, const juce::String& name)
    {
        const auto s = timeBlocks (eng, k, settings.blocks, settings.warmup);
        auto m = toMeasure (group, pre + name, blockSize, s, base, k);
        logLine (m.name + ": mean " + fixed (m.meanPct, 3) + "%  p99 " + fixed (m.p99Pct, 3) + "%  worst " + fixed (m.worstPct, 3) + "% per strip");
        result.add (m);
    };

    setTest (pre + "input and filter");
    each ([] (Strip& s) { s.params.input.invert = true; s.params.input.delayOn = true; s.params.filter().lowCut = true; s.params.filter().highCut = true; });
    report ("Block", "preamp / input + filter");
    each ([] (Strip& s) { s.params.input.invert = false; s.params.input.delayOn = false; s.params.filter().lowCut = false; s.params.filter().highCut = false; });

    for (int slot = 0; slot < 2 && ! threadShouldExit(); ++slot)
        for (int m : sample (dynamicsModels (slot), settings.modelSamples))
        {
            const auto name = modelName (models::Dynamics, m);
            setTest (pre + (slot == 0 ? "gate " : "dynamics ") + name);
            each ([&] (Strip& s) { s.setModel (slot == 0 ? ModelBlock::Gate : ModelBlock::Dynamics, m); (slot == 0 ? s.params.gate() : s.params.dynamics()).on = true; });
            report ("Block", juce::String (slot == 0 ? "gate " : "dynamics ") + name);
            each ([&] (Strip& s) { (slot == 0 ? s.params.gate() : s.params.dynamics()).on = false; });
        }

    std::vector<int> eqs;
    for (int m = 0, n = ld::ld_model_count (models::ChannelEq); m < n; ++m)
        eqs.push_back (m);
    for (int m : sample (eqs, settings.modelSamples))
    {
        const auto name = modelName (models::ChannelEq, m);
        setTest (pre + "EQ " + name);
        each ([&] (Strip& s) { s.setModel (ModelBlock::Eq, m); s.params.eq().on = true; });
        report ("Block", "EQ " + name);
        each ([&] (Strip& s) { s.params.eq().on = false; });
    }
}

void Runner::effectTests (ld::ProfileId p)
{
    MixerEngine eng (0);
    eng.configureProfile (p);
    ld::ld_models_show_profile (p);
    if (eng.getChannels().empty() || eng.getFx().getNumSlots() < 1)
        return;
    eng.prepareOffline (settings.sampleRate, blockSize, 1, 2);
    wireInputs (eng, 1);
    auto& strip = *eng.getChannels()[0];
    const juce::String pre = juce::String (profileName (p)) + " ";
    strip.params.preInsert().fx = 0;
    strip.params.preInsert().on = false;
    const auto base = timeBlocks (eng, 1, settings.blocks, settings.warmup);
    for (int m : fxModels (0))
    {
        if (threadShouldExit())
            break;
        const auto name = modelName (models::Fx, m);
        setTest (pre + "effect " + name);
        if (! eng.getFx().setModel (0, m))
            continue;
        strip.params.preInsert().on = true;
        const auto s = timeBlocks (eng, 1, settings.blocks, settings.warmup);
        strip.params.preInsert().on = false;
        auto meas = toMeasure ("Effect", pre + name, blockSize, s, base, 1);
        logLine (meas.name + ": mean " + fixed (meas.meanPct, 3) + "%  p99 " + fixed (meas.p99Pct, 3) + "%  worst " + fixed (meas.worstPct, 3) + "%");
        result.add (meas);
    }
}

double Runner::rampStep (ld::ProfileId p, int channels, int bs, bool effects, Stats& out, int& actual)
{
    const int saved = blockSize;
    blockSize = bs;
    MixerEngine eng (-1);
    eng.configureProfile (p);
    if (channels > 0)
        eng.setNumChannels (channels);
    actual = (int) eng.getChannels().size();
    eng.prepareOffline (settings.sampleRate, bs, actual, 2);
    wireInputs (eng, actual);
    const auto gates = dynamicsModels (0), dyns = dynamicsModels (1);
    typicalProcessing (eng, actual, gates.empty() ? -1 : gates.front(), dyns.empty() ? -1 : dyns.front());
    if (effects)
    {
        const int slots = eng.getFx().getNumSlots();
        for (int slot = 0; slot < slots && slot < actual; ++slot)
        {
            const auto list = fxModels (slot);
            if (list.empty() || ! eng.getFx().setModel (slot, list[(size_t) (slot * 7) % list.size()]))
                continue;
            auto& ins = eng.getChannels()[(size_t) slot]->params.preInsert();
            ins.fx = slot;
            ins.on = true;
        }
    }
    out = timeBlocks (eng, actual, settings.blocks, settings.warmup);
    blockSize = saved;
    return out.p99 / budgetMs (bs) * 100.0;
}

void Runner::consoleTests (ld::ProfileId p, int bs, bool withEffects)
{
    ld::ld_models_show_profile (p);
    const juce::String name = profileName (p);
    Stats s;
    int actual = 0, lo = 0, hi = 0, capped = 0;
    Stats loStats;
    // Double the channels until the limit is passed, then narrow it down.
    for (int n = settings.firstChannels; ! threadShouldExit();)
    {
        setTest (name + " " + juce::String (n) + " channels at " + juce::String (bs) + " samples");
        const auto pct = rampStep (p, n, bs, false, s, actual);
        logLine (name + ", " + juce::String (actual) + " channels, " + juce::String (bs) + " samples: p99 " + fixed (pct, 1) + "% of the budget");
        if (actual < n)
            capped = actual; // the console takes no more channels
        if (pct <= settings.limitPct && actual >= n)
        {
            lo = n;
            loStats = s;
            if (n >= settings.maxChannels)
            {
                capped = n;
                break;
            }
            n = juce::jmin (settings.maxChannels, n * 2);
        }
        else
        {
            hi = n;
            if (capped > 0 && pct <= settings.limitPct)
            {
                lo = capped;
                loStats = s;
            }
            break;
        }
        if (capped > 0)
            break;
    }
    for (int i = 0; i < 3 && capped == 0 && hi - lo > 4 && ! threadShouldExit(); ++i)
    {
        const int mid = (lo + hi) / 2;
        const auto pct = rampStep (p, mid, bs, false, s, actual);
        logLine (name + ", " + juce::String (mid) + " channels: p99 " + fixed (pct, 1) + "%");
        if (pct <= settings.limitPct)
        {
            lo = mid;
            loStats = s;
        }
        else
            hi = mid;
    }
    if (threadShouldExit())
        return;
    Measure m;
    m.group = "Console";
    m.name = name;
    m.blockSize = bs;
    m.count = lo;
    m.value = lo;
    m.meanPct = loStats.mean / budgetMs (bs) * 100.0;
    m.p99Pct = loStats.p99 / budgetMs (bs) * 100.0;
    m.worstPct = loStats.worst / budgetMs (bs) * 100.0;
    m.meanMs = loStats.mean;
    m.note = capped > 0 ? "at least (the most this console or the test takes)" : juce::String();
    if (lo == 0)
        m.note = "even the fewest channels tested was over the limit";
    result.add (m);
    logLine ("=> " + name + " at " + juce::String (bs) + " samples: about " + juce::String (lo) + " channels");

    if (withEffects)
    {
        setTest (name + " with all effects, " + juce::String (bs) + " samples");
        const auto pct = rampStep (p, 0, bs, true, s, actual);
        Measure f;
        f.group = "Console + effects";
        f.name = name;
        f.blockSize = bs;
        f.count = actual;
        f.value = actual;
        f.meanPct = s.mean / budgetMs (bs) * 100.0;
        f.p99Pct = pct;
        f.worstPct = s.worst / budgetMs (bs) * 100.0;
        f.meanMs = s.mean;
        f.note = juce::String (actual) + " channels, every effects slot loaded";
        result.add (f);
        logLine ("=> " + name + " with all effects (" + juce::String (actual) + " channels): p99 " + fixed (pct, 1) + "%");
    }
}

void Runner::pluginTests()
{
    for (auto& pl : settings.plugins)
    {
        if (threadShouldExit())
            break;
        setTest ("Plug-in " + pl.name);
        auto* inst = pl.instance;
        const int ch = juce::jmax (2, inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels());
        inst->prepareToPlay (settings.sampleRate, blockSize);
        juce::AudioBuffer<float> buf (ch, blockSize);
        juce::MidiBuffer midi;
        juce::Random rnd (3);
        std::vector<float> times;
        graphBudget = budgetMs (blockSize);
        for (int b = 0; b < settings.warmup + settings.blocks && ! threadShouldExit(); ++b)
        {
            for (int c = 0; c < ch; ++c)
                for (int i = 0; i < blockSize; ++i)
                    buf.setSample (c, i, (rnd.nextFloat() - 0.5f) * 0.5f);
            const auto t0 = juce::Time::getHighResolutionTicks();
            inst->processBlock (buf, midi);
            const auto ms = (float) (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1000.0);
            if (b >= settings.warmup)
            {
                times.push_back (ms);
                graph[(size_t) (graphHead.load() % kGraphLength)] = ms;
                graphHead.fetch_add (1);
            }
        }
        inst->releaseResources();
        if (times.empty())
            continue;
        Stats s;
        for (auto t : times)
            s.mean += t / (double) times.size();
        s.worst = *std::max_element (times.begin(), times.end());
        s.p99 = percentile99 (times);
        auto m = toMeasure ("Plug-in", pl.name, blockSize, s, {}, 1);
        logLine (m.name + ": mean " + fixed (m.meanPct, 2) + "%  p99 " + fixed (m.p99Pct, 2) + "%  worst " + fixed (m.worstPct, 2) + "%");
        result.add (m);
    }
}

void Runner::recordingTests()
{
    auto base = settings.recordFolder;
    if (base == juce::File() || ! base.createDirectory().wasOk())
        base = juce::File::getSpecialLocation (juce::File::tempDirectory);
    const auto folder = base.getChildFile ("sidecar-benchmark-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));
    folder.createDirectory();
    juce::WavAudioFormat wav;
    juce::Random rnd (5);
    juce::AudioBuffer<float> buf (1, blockSize);
    for (int i = 0; i < blockSize; ++i)
        buf.setSample (0, i, (rnd.nextFloat() - 0.5f) * 0.5f);
    const float* chan[] = { buf.getReadPointer (0) };
    const int blocks = juce::jmax (8, (int) (settings.recordSeconds * settings.sampleRate / blockSize));
    for (int tracks : { 8, 16, 32, 48, 64, 96, 128 })
    {
        if (tracks > settings.recordMaxTracks || threadShouldExit())
            break;
        setTest ("Recording " + juce::String (tracks) + " tracks");
        std::vector<std::unique_ptr<juce::AudioFormatWriter>> writers;
        for (int t = 0; t < tracks; ++t)
            if (auto stream = folder.getChildFile ("t" + juce::String (t) + ".wav").createOutputStream())
                if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream.get(), settings.sampleRate, 1, 24, {}, 0)))
                {
                    stream.release();
                    writers.push_back (std::move (w));
                }
        std::vector<float> times;
        const auto start = nowSeconds();
        graphBudget = budgetMs (blockSize);
        for (int b = 0; b < blocks && ! threadShouldExit(); ++b)
        {
            const auto t0 = nowSeconds();
            for (auto& w : writers)
                w->writeFromFloatArrays (chan, 1, blockSize);
            const auto ms = (float) ((nowSeconds() - t0) * 1000.0);
            times.push_back (ms);
            graph[(size_t) (graphHead.load() % kGraphLength)] = ms;
            graphHead.fetch_add (1);
        }
        for (auto& w : writers)
            w->flush();
        const auto elapsed = nowSeconds() - start;
        writers.clear();
        if (times.empty())
            break;
        Stats s;
        for (auto t : times)
            s.mean += t / (double) times.size();
        s.worst = *std::max_element (times.begin(), times.end());
        s.p99 = percentile99 (times);
        const double speed = (double) blocks * blockSize / settings.sampleRate / juce::jmax (1.0e-6, elapsed);
        auto m = toMeasure ("Recording", juce::String (tracks) + " tracks", blockSize, s, {}, 1);
        const bool ok = speed >= 2.0 && m.p99Pct <= 100.0 && (int) writers.size() == 0 && s.p99 < budgetMs (blockSize) * 4.0;
        m.value = ok ? tracks : 0;
        m.count = tracks;
        m.note = fixed (speed, 1) + "x real time, " + (ok ? "keeps up" : "does not keep up");
        logLine (m.name + ": " + m.note + " (p99 " + fixed (m.p99Pct, 1) + "% of a block)");
        result.add (m);
        if (! ok)
            break;
    }
    folder.deleteRecursively();
}

void Runner::run()
{
    const auto started = nowSeconds();
    std::vector<std::function<void()>> tasks;
    const auto cur = settings.currentProfile;
    if (settings.doBlocks)
        for (auto p : settings.profiles)
            tasks.push_back ([this, p] { blockTests (p); });
    if (settings.doEffects)
        for (auto p : settings.profiles)
            tasks.push_back ([this, p] { effectTests (p); });
    if (settings.doConsoles)
    {
        // Every console at 128 samples whatever the buffer: that is what the Sidecar Mark is taken from.
        for (auto p : settings.profiles)
            tasks.push_back ([this, p] { consoleTests (p, 128, p == settings.currentProfile && settings.blockSize == 128); });
        if (settings.blockSize != 128)
            tasks.push_back ([this, cur] { consoleTests (cur, settings.blockSize, true); });
        for (int size : settings.extraSizes)
            if (size != settings.blockSize && size != 128)
                tasks.push_back ([this, size, cur] { consoleTests (cur, size, true); });
    }
    if (settings.doPlugins && ! settings.plugins.empty())
        tasks.push_back ([this] { pluginTests(); });
    if (settings.doRecording)
        tasks.push_back ([this] { recordingTests(); });
    logLine ("Benchmark: " + juce::String (tasks.size()) + " steps, " + juce::String (juce::roundToInt (settings.sampleRate)) + " Hz, " + juce::String (blockSize) + " samples ("
             + fixed (budgetMs (blockSize), 2) + " ms per block). No sound is made.");
    for (size_t i = 0; i < tasks.size() && ! threadShouldExit(); ++i)
    {
        progress = (double) i / (double) tasks.size();
        sub = 0;
        tasks[i]();
    }
    ld::ld_models_show_profile (settings.restoreShownProfile);
    result.cancelled = threadShouldExit();
    result.seconds = nowSeconds() - started;
    progress = 1.0;
    sub = 0;
    setTest (result.cancelled ? "Cancelled" : "Finished");
    done = true;
}

} // namespace livedaw::bench
