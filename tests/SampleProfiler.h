// A sampling profiler for the tests' hotspot hunts (Windows only; elsewhere it does nothing).
// While it runs, a thread suspends the thread that started it every millisecond or so and
// notes its call stack (no allocation while it is suspended); `report` then names the
// functions the samples were in, from the symbols of the executable's PDB.
//
//   SampleProfiler profiler;     // starts sampling the calling thread
//   ... the work ...
//   profiler.report (logger);    // the functions most often on the stack

#pragma once

#include <juce_core/juce_core.h>

#include <functional>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <dbghelp.h>

 #include <algorithm>
 #include <atomic>
 #include <map>
 #include <set>
 #include <thread>
 #include <vector>

namespace livedaw::test
{
class SampleProfiler
{
public:
    SampleProfiler()
    {
        DuplicateHandle (GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &target, 0, FALSE, DUPLICATE_SAME_ACCESS);
        frames.assign (kMaxSamples * kDepth, 0);
        lengths.assign (kMaxSamples, 0);
        sampler = std::thread ([this] { run(); });
    }

    ~SampleProfiler()
    {
        stop();
        CloseHandle (target);
    }

    /** Samples only while not paused (to take one part of the work out of a longer run). */
    void setPaused (bool shouldPause) { paused = shouldPause; }

    void stop()
    {
        running = false;
        if (sampler.joinable())
            sampler.join();
    }

    /** Prints the `top` functions by samples they were anywhere on the stack, and by samples they were
        at the top of it. */
    void report (const std::function<void (const juce::String&)>& log, int top = 40)
    {
        stop();
        const int n = count;
        SymSetOptions (SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        SymInitialize (GetCurrentProcess(), nullptr, TRUE);
        std::map<DWORD64, juce::String> names;
        auto nameOf = [&names] (DWORD64 address) -> juce::String
        {
            if (auto it = names.find (address); it != names.end())
                return it->second;
            alignas (SYMBOL_INFO) char buffer[sizeof (SYMBOL_INFO) + 256] {};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*> (buffer);
            symbol->SizeOfStruct = sizeof (SYMBOL_INFO);
            symbol->MaxNameLen = 255;
            DWORD64 displacement = 0;
            juce::String name = SymFromAddr (GetCurrentProcess(), address, &displacement, symbol) ? juce::String (symbol->Name)
                                                                                                    : "0x" + juce::String::toHexString ((juce::int64) address);
            return names[address] = name;
        };

        std::map<juce::String, int> inclusive, self;
        for (int s = 0; s < n; ++s)
        {
            std::set<juce::String> seen;
            for (int f = 0; f < lengths[(size_t) s]; ++f)
            {
                // The return address points after the call: back up one byte into it.
                const auto name = nameOf (frames[(size_t) s * kDepth + (size_t) f] - (f == 0 ? 0 : 1));
                if (f == 0)
                    ++self[name];
                if (seen.insert (name).second)
                    ++inclusive[name];
            }
        }
        auto print = [&] (const char* title, const std::map<juce::String, int>& counts)
        {
            std::vector<std::pair<int, juce::String>> sorted;
            for (auto& [name, c] : counts)
                sorted.push_back ({ c, name });
            std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.first > b.first; });
            log (juce::String ("PROFILE ") + title + " (" + juce::String (n) + " samples)");
            for (int i = 0; i < top && i < (int) sorted.size(); ++i)
                log ("  " + juce::String (100.0 * sorted[(size_t) i].first / juce::jmax (1, n), 1).paddedLeft (' ', 5) + " %  " + sorted[(size_t) i].second.substring (0, 150));
        };
        print ("inclusive", inclusive);
        print ("self", self);
        SymCleanup (GetCurrentProcess());
    }

private:
    static constexpr int kMaxSamples = 60000, kDepth = 64;

    void run()
    {
        while (running && count < kMaxSamples)
        {
            const auto from = GetTickCount64();
            LARGE_INTEGER start, now, frequency;
            QueryPerformanceFrequency (&frequency);
            QueryPerformanceCounter (&start);
            do
            {
                Sleep (0);
                QueryPerformanceCounter (&now);
            } while (running && (now.QuadPart - start.QuadPart) * 1000 < frequency.QuadPart);
            juce::ignoreUnused (from);
            if (paused)
                continue;
            if (SuspendThread (target) == (DWORD) -1)
                continue;
            CONTEXT context {};
            context.ContextFlags = CONTEXT_FULL;
            int length = 0;
            if (GetThreadContext (target, &context))
            {
                auto* out = &frames[(size_t) count * kDepth];
                // Walks the stack with the unwind tables: no allocation, so it can't wait for a lock
                // the suspended thread holds.
                for (; length < kDepth && context.Rip != 0; ++length)
                {
                    out[length] = context.Rip;
                    DWORD64 base = 0;
                    auto* entry = RtlLookupFunctionEntry (context.Rip, &base, nullptr);
                    if (entry == nullptr)
                    {
                        if (context.Rsp == 0)
                            break;
                        context.Rip = *reinterpret_cast<DWORD64*> (context.Rsp);
                        context.Rsp += 8;
                        continue;
                    }
                    void* handlerData = nullptr;
                    DWORD64 frame = 0;
                    RtlVirtualUnwind (UNW_FLAG_NHANDLER, base, context.Rip, entry, &context, &handlerData, &frame, nullptr);
                }
            }
            ResumeThread (target);
            lengths[(size_t) count] = length;
            ++count;
        }
    }

    HANDLE target = nullptr;
    std::thread sampler;
    std::atomic<bool> running { true }, paused { false };
    std::atomic<int> count { 0 };
    std::vector<DWORD64> frames;
    std::vector<int> lengths;
};
} // namespace livedaw::test

#else

namespace livedaw::test
{
class SampleProfiler
{
public:
    void setPaused (bool) {}
    void stop() {}
    void report (const std::function<void (const juce::String&)>&, int = 40) {}
};
} // namespace livedaw::test

#endif
