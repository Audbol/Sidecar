// SidecarMonkey: a monkey test of the whole program. It starts Sidecar's real UI
// (MainComponent and every screen, window and pop-up it opens) on the virtual
// audio device, and for a while sends it random clicks, right-clicks, double
// clicks, drags, long presses, wheel turns and keys through the windows' own
// event handling (ComponentPeer), the way the OS would. It checks that the audio
// keeps running and stays finite and that the message thread doesn't stall, and
// logs every action so a crash can be followed and replayed (same seed, same
// actions, as long as the timing allows).
//
// It runs with SIDECAR_HOME set to a folder of its own (settings, shows,
// recordings, screenshots go there), no audio hardware, no network servers and
// no file manager windows (paths::externalActionsBlocked); keys that would act
// outside the app (SHUTDOWN, scanning the computer's plugins) aren't pressed and
// native file dialogs are cancelled.
//
//   SidecarMonkey [--seed N] [--seconds S] [--buffer SAMPLES] [--home DIR] [--idle] [--interface wing|universal|x32|promix|02r]
//                 [--press "KEY TEXT"]
//
// --idle sends no input: a baseline of what the audio does with the app just running.
// --interface x32 / promix / 02r starts on that console and its surface; --press clicks the
// key whose text it gives two seconds in (a user's report replayed: the X32 surface's
// UNIVERSAL MIXER key).
// --interface universal starts on the Universal Mixer instead of the WING surface.
//
// It also watches for dropouts: blocks that take longer to process than they
// last (overruns), callbacks the device runs late, blocks the engine silences
// after a fault, and the device's xruns (128-sample blocks by default).
//
// Exit code 0: no problem found; 1: a dropout, the audio stopped or went non-finite, or the
// message thread stalled; 3: a crash (Windows: the log has the exception, or for an abort
// or an uncaught exception, its stack; the monkey's build has symbols).

#include "engine/AppPaths.h"
#include "engine/EventLog.h"
#include "engine/MixerEngine.h"
#include "engine/PluginHost.h"
#include "engine/PluginScanner.h"
#include "engine/Show.h"
#include "engine/VirtualAudioDevice.h"
#include "sandbox/TestPlugins.h"
#include "ui/LookAndFeel.h"
#include "ui/MainComponent.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <exception>
#include <map>
#include <set>
#include <typeinfo>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

using namespace livedaw;

namespace
{
    /** The action log: one line per action, flushed, so a crash leaves the trail. */
    struct ActionLog
    {
        std::unique_ptr<juce::FileOutputStream> out;
        juce::StringArray recent; // the last actions, for the crash report
        juce::CriticalSection lock;

        void open (const juce::File& f)
        {
            f.deleteFile();
            out = std::make_unique<juce::FileOutputStream> (f);
        }

        void write (const juce::String& line)
        {
            const juce::ScopedLock sl (lock);
            const auto stamped = juce::Time::getCurrentTime().formatted ("%H:%M:%S ") + line;
            if (out != nullptr)
            {
                out->writeText (stamped + "\n", false, false, nullptr);
                out->flush();
            }
            recent.add (stamped);
            if (recent.size() > 40)
                recent.remove (0);
        }

        juce::String last()
        {
            const juce::ScopedLock sl (lock);
            return recent.isEmpty() ? juce::String ("(start)") : recent[recent.size() - 1];
        }
    };
    ActionLog actionLog;

   #if JUCE_WINDOWS
    LONG WINAPI onCrash (EXCEPTION_POINTERS* info)
    {
        juce::String report ("CRASH: exception 0x" + juce::String::toHexString ((juce::int64) info->ExceptionRecord->ExceptionCode) + " at 0x"
                             + juce::String::toHexString ((juce::pointer_sized_int) info->ExceptionRecord->ExceptionAddress)
                             + ". Stack:\n" + juce::SystemStats::getStackBacktrace() + "The last actions:");
        actionLog.write (report);
        for (auto& line : actionLog.recent)
            actionLog.write ("  " + line);
        std::fputs ((report + "\n" + actionLog.recent.joinIntoString ("\n") + "\n").toRawUTF8(), stderr);
        std::fflush (stderr);
        TerminateProcess (GetCurrentProcess(), 3);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    /** abort() (std::terminate after an uncaught exception), an invalid parameter
        to a C runtime function or a pure virtual call: the process would end in the
        runtime's fail-fast (0xC0000409), which no exception filter sees. Logs `why`,
        the stack and the last actions instead, and ends with the crash code. */
    [[noreturn]] void onAbortWith (const juce::String& why)
    {
        actionLog.write ("CRASH: " + why + ". Stack:\n" + juce::SystemStats::getStackBacktrace() + "The last actions:");
        for (auto& line : actionLog.recent)
            actionLog.write ("  " + line);
        std::fputs (("CRASH: " + why + "\n").toRawUTF8(), stderr);
        std::fflush (stderr);
        TerminateProcess (GetCurrentProcess(), 3);
        std::_Exit (3);
    }

    void onAbort (int) { onAbortWith ("abort"); }
    void onInvalidParameter (const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
    {
        onAbortWith ("an invalid parameter to a C runtime function");
    }
    void onPureCall() { onAbortWith ("a pure virtual function call"); }

    [[noreturn]] void onTerminate()
    {
        juce::String why ("std::terminate");
        if (auto e = std::current_exception())
        {
            try
            {
                std::rethrow_exception (e);
            }
            catch (const std::exception& x)
            {
                why << ": uncaught " << typeid (x).name() << " (" << x.what() << ")";
            }
            catch (...)
            {
                why << ": an uncaught exception of an unknown type";
            }
        }
        onAbortWith (why);
    }

    /** Cancels the native dialogs (file choosers) of this process. */
    BOOL CALLBACK cancelDialog (HWND hwnd, LPARAM closed)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId (hwnd, &pid);
        wchar_t cls[64] {};
        GetClassNameW (hwnd, cls, 64);
        if (pid == GetCurrentProcessId() && IsWindowVisible (hwnd) && wcscmp (cls, L"#32770") == 0)
        {
            PostMessageW (hwnd, WM_COMMAND, IDCANCEL, 0);
            ++*reinterpret_cast<int*> (closed);
        }
        return TRUE;
    }
   #endif

    void storeMax (std::atomic<double>& m, double v) noexcept
    {
        for (auto old = m.load(); v > old && ! m.compare_exchange_weak (old, v);)
        {
        }
    }

    /** The engine as the device's callback: counts blocks, checks the outputs are
        finite, and watches for dropouts. An overrun is a block whose processing
        took longer than the block lasts (on hardware: a dropout); a late callback
        came more than two blocks after the one before (the device thread
        wasn't run in time). */
    struct CheckedCallback final : public juce::AudioIODeviceCallback
    {
        explicit CheckedCallback (MixerEngine& e) : engine (e) {}

        void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                               const juce::AudioIODeviceCallbackContext& context) override
        {
            const auto start = juce::Time::getHighResolutionTicks();
            engine.audioDeviceIOCallbackWithContext (in, numIn, out, numOut, n, context);
            const auto end = juce::Time::getHighResolutionTicks();
            for (int c = 0; c < numOut; ++c)
                if (out[c] != nullptr)
                    for (int i = 0; i < n; ++i)
                        if (! std::isfinite (out[c][i]))
                        {
                            notFinite = true;
                            break;
                        }

            const double budgetMs = 1000.0 * n / juce::jmax (1.0, sampleRate.load());
            const double tookMs = juce::Time::highResolutionTicksToSeconds (end - start) * 1000.0;
            storeMax (maxLoad, tookMs / budgetMs);
            if (tookMs > budgetMs && blocks.load() < kWarmUpBlocks)
                ++warmUpOverruns; // the first blocks after the device starts (threads and memory warming up)
            else if (tookMs > budgetMs)
            {
                ++overruns;
                storeMax (worstOverrunMs, tookMs);
            }
            if (const auto last = lastStart.exchange (start); last != 0)
            {
                const double gapMs = juce::Time::highResolutionTicksToSeconds (start - last) * 1000.0;
                storeMax (maxGapMs, gapMs);
                if (gapMs > 2.0 * budgetMs)
                    ++lateCallbacks;
            }
            ++blocks;
        }
        void audioDeviceAboutToStart (juce::AudioIODevice* d) override
        {
            sampleRate = d->getCurrentSampleRate();
            bufferSize = d->getCurrentBufferSizeSamples();
            device = d;
            lastStart = 0;
            engine.audioDeviceAboutToStart (d);
        }
        void audioDeviceStopped() override { engine.audioDeviceStopped(); }
        void audioDeviceError (const juce::String& e) override { engine.audioDeviceError (e); }

        MixerEngine& engine;
        static constexpr juce::int64 kWarmUpBlocks = 200; // about half a second
        std::atomic<juce::int64> blocks { 0 }, overruns { 0 }, warmUpOverruns { 0 }, lateCallbacks { 0 }, lastStart { 0 };
        std::atomic<double> maxLoad { 0.0 }, worstOverrunMs { 0.0 }, maxGapMs { 0.0 }, sampleRate { 48000.0 };
        std::atomic<int> bufferSize { 0 };
        std::atomic<bool> notFinite { false };
        std::atomic<juce::AudioIODevice*> device { nullptr };
    };

    /** Keys whose action reaches outside the app or ends it: never pressed. */
    bool isBlocked (juce::Component* c)
    {
        static const char* const blocked[] = { "SHUTDOWN", "DATA FOLDER", "SHOW FOLDER", "SCAN", "OPTIONS..." };
        for (; c != nullptr; c = c->getParentComponent())
            if (auto* b = dynamic_cast<juce::Button*> (c))
                for (auto* text : blocked)
                    if (b->getButtonText().containsIgnoreCase (text))
                        return true;
        return false;
    }

    void collectShowing (juce::Component& c, std::vector<juce::Component*>& out)
    {
        for (auto* child : c.getChildren())
            if (child->isShowing() && child->getWidth() > 1 && child->getHeight() > 1)
            {
                out.push_back (child);
                collectShowing (*child, out);
            }
    }
} // namespace

//==============================================================================
class Monkey final : private juce::Timer
{
public:
    Monkey (juce::int64 seedToUse, int secondsToRun, juce::DocumentWindow& main, CheckedCallback& audio)
        : seed (seedToUse), rng (seedToUse), seconds (secondsToRun), mainWindow (main), callback (audio)
    {
        endAt = juce::Time::getMillisecondCounter() + (juce::uint32) seconds * 1000u;
        lastTick = juce::Time::getMillisecondCounterHiRes();
        lastBlocks = 0;
        lastHealth = juce::Time::getMillisecondCounter();
        faultsAtStart = callback.engine.getEngineFaults();
        if (auto* d = callback.device.load())
            xrunsAtStart = d->getXRunCount();
        startTimer (4);
    }

    bool isRunning() const noexcept { return isTimerRunning(); }
    /** --idle: no input, only the checks (what the audio does with the app just running). */
    bool idle = false;
    std::function<void()> onFinished;

    juce::String summary() const
    {
        juce::String s;
        s << "seed " << seed << ", " << actions << " actions in " << seconds << " s (";
        for (auto& [kind, n] : counts)
            s << kind << " " << n << ", ";
        s << "blocked " << blockedCount << ", dialogs cancelled " << dialogsClosed << ", windows seen " << (int) windowNames.size() << "); audio "
          << callback.blocks.load() << " blocks of " << callback.bufferSize.load() << " at " << juce::roundToInt (callback.sampleRate.load()) << " Hz"
          << (callback.notFinite.load() ? ", NON-FINITE OUTPUT" : "") << (audioStopped ? ", AUDIO STOPPED" : "") << "; dropouts: "
          << callback.overruns.load() << " overrun(s)" << (callback.overruns.load() > 0 ? " (worst " + juce::String (callback.worstOverrunMs.load(), 2) + " ms)" : juce::String())
          << " (+ " << callback.warmUpOverruns.load() << " in the first " << CheckedCallback::kWarmUpBlocks << " blocks after start-up)"
          << ", peak block load " << juce::roundToInt (100.0 * callback.maxLoad.load()) << " %, " << callback.lateCallbacks.load()
          << " late callback(s) (longest gap " << juce::String (callback.maxGapMs.load(), 1) << " ms), " << engineFaults() << " engine fault(s), "
          << deviceXruns() << " device xrun(s); longest message-thread stall " << juce::roundToInt (longestStallMs) << " ms";
        return s;
    }

    /** Dropouts count as failures: an overrun, a block the engine silenced or a device xrun. */
    bool failed() const
    {
        return callback.notFinite.load() || audioStopped || longestStallMs > kStallMs || callback.overruns.load() > 0 || engineFaults() > 0
               || deviceXruns() > 0;
    }

private:
    static constexpr double kStallMs = 3000.0;

    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const double gap = now - lastTick;
        if (gap > longestStallMs)
            longestStallMs = gap;
        if (gap > kStallMs)
            actionLog.write ("STALL: the message thread was away " + juce::String (juce::roundToInt (gap)) + " ms");
        lastTick = now;

        cancelDialogs();
        checkHealth();
        if (pendingUp)
        {
            // The second half of a long press or a drag.
            finishPress();
            return;
        }
        if (juce::Time::getMillisecondCounter() >= endAt)
        {
            stopTimer();
            actionLog.write ("DONE: " + summary());
            if (onFinished)
                onFinished();
            return;
        }
        act();
    }

    void cancelDialogs()
    {
       #if JUCE_WINDOWS
        int closed = 0;
        EnumWindows (cancelDialog, reinterpret_cast<LPARAM> (&closed));
        if (closed > 0)
        {
            dialogsClosed += closed;
            actionLog.write ("cancelled " + juce::String (closed) + " native dialog(s)");
        }
       #endif
    }

    int engineFaults() const { return callback.engine.getEngineFaults() - faultsAtStart; }
    int deviceXruns() const
    {
        auto* d = callback.device.load();
        return d != nullptr ? juce::jmax (0, d->getXRunCount() - xrunsAtStart) : 0;
    }

    void checkHealth()
    {
        // Dropouts, as soon as they happen, with what the monkey was doing.
        const auto lastAction = actionLog.last();
        if (const auto n = callback.overruns.load(); n > lastOverruns)
        {
            actionLog.write ("DROPOUT: " + juce::String (n - lastOverruns) + " block(s) took longer than they last (worst so far "
                             + juce::String (callback.worstOverrunMs.load(), 2) + " ms) during: " + lastAction);
            lastOverruns = n;
        }
        if (const auto n = callback.lateCallbacks.load(); n > lastLate)
        {
            actionLog.write ("LATE CALLBACK: " + juce::String (n - lastLate) + " (longest gap " + juce::String (callback.maxGapMs.load(), 1)
                             + " ms) during: " + lastAction);
            lastLate = n;
        }
        if (const auto n = engineFaults(); n > lastFaults)
        {
            actionLog.write ("ENGINE FAULT: " + juce::String (n - lastFaults) + " block(s) silenced during: " + lastAction);
            lastFaults = n;
        }
        if (const auto n = deviceXruns(); n > lastXruns)
        {
            actionLog.write ("DEVICE XRUN: " + juce::String (n - lastXruns) + " during: " + lastAction);
            lastXruns = n;
        }

        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastHealth < 2000)
            return;
        const auto blocks = callback.blocks.load();
        if (blocks == lastBlocks && ! audioStopped)
        {
            audioStopped = true;
            actionLog.write ("AUDIO STOPPED: no block in 2 s");
        }
        if (callback.notFinite.load() && ! reportedNonFinite)
        {
            reportedNonFinite = true;
            actionLog.write ("NON-FINITE OUTPUT");
        }
        lastBlocks = blocks;
        lastHealth = now;
    }

    /** The windows on screen (the main one, plugin windows, the overview, pop-ups),
        but not those the OS sends no mouse input to (a drop shadow's, a tooltip, a
        drag image): a click there reaches the window under them, which the monkey
        clicks anyway. Sent straight to a drop shadow's window, a press went to the
        window under it, dismissed the menu that cast the shadow and so deleted the
        window it had been sent to (tests/WindowTests.cpp). */
    std::vector<juce::Component*> windows()
    {
        std::vector<juce::Component*> list;
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* c = desktop.getComponent (i); c != nullptr && c->isShowing() && c->getPeer() != nullptr && ! c->getPeer()->isMinimised()
                                                    && (c->getPeer()->getStyleFlags() & juce::ComponentPeer::windowIgnoresMouseClicks) == 0)
                list.push_back (c);
        return list;
    }

    void act()
    {
        if (! idle)
            actRandomly();
    }

    void actRandomly()
    {
        auto list = windows();
        if (list.empty())
            return;
        // The main window most of the time; a pop-up or another window otherwise
        // (the newest first: menus and dialogs sit on top).
        juce::Component* window = &mainWindow;
        if (list.size() > 1 && rng.nextInt (100) < 45)
            window = list[(size_t) rng.nextInt ((int) list.size())];
        if (! window->isShowing())
            window = list.front();
        windowNames.insert (window->getName().isNotEmpty() ? window->getName() : juce::String ("(unnamed)"));

        ++actions;
        const int kind = rng.nextInt (100);
        if (kind < 8)
        {
            key();
            return;
        }
        if (kind < 10 && window == &mainWindow)
        {
            const int w = 1280 + rng.nextInt (1400), h = 640 + rng.nextInt (800);
            count ("resize");
            actionLog.write ("resize main window to " + juce::String (w) + " x " + juce::String (h));
            mainWindow.setSize (w, h);
            return;
        }

        // A point: on a random showing component (small keys get their turn) or anywhere.
        std::vector<juce::Component*> targets;
        collectShowing (*window, targets);
        if (window == &mainWindow)
        {
            // Locked (Ctrl+L, HOME held): unlock after a few tries, or the rest of the run tests the lock screen.
            const bool locked = std::any_of (targets.begin(), targets.end(), [] (auto* c) { return juce::String (typeid (*c).name()).contains ("LockOverlay"); });
            if (locked && ++lockedActions > 6)
            {
                lockedActions = 0;
                count ("unlock");
                actionLog.write ("unlock (Ctrl+L)");
                if (auto* peer = mainWindow.getPeer())
                    peer->handleKeyPress (juce::KeyPress ('l', juce::ModifierKeys::commandModifier, 0));
                return;
            }
            // The main display (its screens, tabs, pop-ups) gets a good share.
            if (! locked && rng.nextInt (100) < 40)
            {
                std::vector<juce::Component*> display;
                for (auto* c : targets)
                    for (auto* p = c; p != nullptr; p = p->getParentComponent())
                        if (juce::String (typeid (*p).name()).endsWith ("::MainDisplay"))
                        {
                            display.push_back (c);
                            break;
                        }
                if (! display.empty())
                    targets = std::move (display);
                else
                    targets.clear();
            }
        }
        juce::Component* target = window;
        juce::Point<int> local;
        if (! targets.empty() && rng.nextBool())
        {
            target = targets[(size_t) rng.nextInt ((int) targets.size())];
            local = { rng.nextInt (target->getWidth()), rng.nextInt (target->getHeight()) };
        }
        else
            local = { rng.nextInt (juce::jmax (1, window->getWidth())), rng.nextInt (juce::jmax (1, window->getHeight())) };
        const auto screen = target->localPointToGlobal (local);
        auto* peer = window->getPeer();
        if (peer == nullptr)
            return;
        auto* hit = window->getComponentAt (window->getLocalPoint (nullptr, screen));
        if (isBlocked (hit))
        {
            ++blockedCount;
            actionLog.write ("skipped (blocked key) at " + describe (hit));
            return;
        }
        const auto at = peer->globalToLocal (screen.toFloat());
        const auto what = describe (hit) + " in " + describe (window);

        if (kind < 55)
            press (*peer, at, juce::ModifierKeys::leftButtonModifier, "click", what, 0);
        else if (kind < 63)
            press (*peer, at, juce::ModifierKeys::rightButtonModifier, "right-click", what, 0);
        else if (kind < 70)
        {
            press (*peer, at, juce::ModifierKeys::leftButtonModifier, "double-click", what, 0);
            press (*peer, at, juce::ModifierKeys::leftButtonModifier, "", what, 0);
        }
        else if (kind < 76)
            press (*peer, at, juce::ModifierKeys::leftButtonModifier, "long press", what, 650);
        else if (kind < 88)
        {
            // A drag: down here, moves, up a little later somewhere near.
            const auto to = at + juce::Point<float> ((float) rng.nextInt (401) - 200.0f, (float) rng.nextInt (401) - 200.0f);
            count ("drag");
            actionLog.write ("drag " + what + " by " + (to - at).toString());
            peer->handleMouseEvent (mouse, at, juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, juce::Time::currentTimeMillis());
            for (int i = 1; i <= 6; ++i)
            {
                if (! stillOpen (peer))
                    return;
                peer->handleMouseEvent (mouse, at + (to - at) * ((float) i / 6.0f), juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f,
                                        juce::Time::currentTimeMillis());
            }
            if (! stillOpen (peer))
                return;
            pendingPeer = peer;
            pendingAt = to;
            pendingUp = true;
            pendingUpAt = juce::Time::getMillisecondCounter() + 30;
        }
        else
        {
            juce::MouseWheelDetails wheel {};
            wheel.deltaY = (rng.nextFloat() - 0.5f) * 2.0f;
            wheel.deltaX = rng.nextInt (5) == 0 ? (rng.nextFloat() - 0.5f) : 0.0f;
            count ("wheel");
            actionLog.write ("wheel " + juce::String (wheel.deltaY, 2) + " on " + what);
            peer->handleMouseEvent (mouse, at, {}, 0.0f, 0.0f, juce::Time::currentTimeMillis());
            if (stillOpen (peer))
                peer->handleMouseWheel (mouse, at, juce::Time::currentTimeMillis(), wheel);
        }
    }

    void press (juce::ComponentPeer& peer, juce::Point<float> at, juce::ModifierKeys button, const juce::String& label, const juce::String& what, int holdMs)
    {
        auto mods = button;
        if (rng.nextInt (12) == 0)
            mods = mods.withFlags (juce::ModifierKeys::shiftModifier);
        else if (rng.nextInt (16) == 0)
            mods = mods.withFlags (juce::ModifierKeys::ctrlModifier);
        if (label.isNotEmpty())
        {
            count (label);
            actionLog.write (label + (mods.isShiftDown() ? " +shift" : mods.isCtrlDown() ? " +ctrl" : "") + " " + what);
        }
        const auto t = juce::Time::currentTimeMillis();
        peer.handleMouseEvent (mouse, at, {}, 0.0f, 0.0f, t);
        if (! stillOpen (&peer))
            return;
        peer.handleMouseEvent (mouse, at, mods, 1.0f, 0.0f, t);
        if (! stillOpen (&peer))
            return;
        if (holdMs > 0)
        {
            pendingPeer = &peer;
            pendingAt = at;
            pendingUp = true;
            pendingUpAt = juce::Time::getMillisecondCounter() + (juce::uint32) holdMs;
            return;
        }
        peer.handleMouseEvent (mouse, at, mods.withoutMouseButtons(), 0.0f, 0.0f, t + 40);
    }

    void finishPress()
    {
        if (juce::Time::getMillisecondCounter() < pendingUpAt)
            return;
        pendingUp = false;
        // The window may have closed meanwhile.
        if (juce::ComponentPeer::isValidPeer (pendingPeer))
            pendingPeer->handleMouseEvent (mouse, pendingAt, {}, 0.0f, 0.0f, juce::Time::currentTimeMillis());
    }

    /** An event can close the window it went to. The OS sends nothing more to a
        window that is gone, so neither does the monkey: the rest of a click once
        went to a deleted window, a crash of the test (0xC0000409). */
    static bool stillOpen (const juce::ComponentPeer* peer)
    {
        if (juce::ComponentPeer::isValidPeer (peer))
            return true;
        actionLog.write ("  (its window closed: the rest of the gesture isn't sent)");
        return false;
    }

    void key()
    {
        static const int codes[] = { juce::KeyPress::leftKey,   juce::KeyPress::rightKey,  juce::KeyPress::upKey,     juce::KeyPress::downKey,
                                     juce::KeyPress::escapeKey, juce::KeyPress::returnKey, juce::KeyPress::tabKey,    juce::KeyPress::spaceKey,
                                     juce::KeyPress::deleteKey, juce::KeyPress::backspaceKey, juce::KeyPress::homeKey, juce::KeyPress::endKey,
                                     juce::KeyPress::pageUpKey, juce::KeyPress::pageDownKey, juce::KeyPress::F10Key,  juce::KeyPress::F11Key };
        static const char shortcuts[] = { 'e', 'g', 'l', 'p', 'z', 'a', 'c', 'v', 'x', 'd' };
        juce::KeyPress k;
        const int r = rng.nextInt (10);
        if (r < 4)
            k = juce::KeyPress (codes[rng.nextInt ((int) std::size (codes))]);
        else if (r < 6)
        {
            // Ctrl shortcuts (Ctrl+S / Ctrl+O open dialogs, which get cancelled).
            const char c = rng.nextInt (6) == 0 ? (rng.nextBool() ? 's' : 'o') : shortcuts[rng.nextInt ((int) std::size (shortcuts))];
            k = juce::KeyPress (c, juce::ModifierKeys (juce::ModifierKeys::commandModifier | (rng.nextInt (3) == 0 ? juce::ModifierKeys::shiftModifier : 0)), 0);
        }
        else
        {
            // Typing (names, values).
            const juce::String chars ("abcXYZ0123456789-. ");
            const auto c = chars[rng.nextInt (chars.length())];
            k = juce::KeyPress (c, {}, c);
        }
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        auto* target = focused != nullptr ? focused : mainWindow.getContentComponent();
        auto* peer = target != nullptr ? target->getPeer() : nullptr;
        if (peer == nullptr)
            return;
        count ("key");
        actionLog.write ("key " + k.getTextDescription() + " to " + describe (target));
        peer->handleKeyPress (k);
    }

    void count (const juce::String& kind) { ++counts[kind]; }

    static juce::String describe (juce::Component* c)
    {
        if (c == nullptr)
            return "nothing";
        juce::String s;
        if (auto* b = dynamic_cast<juce::Button*> (c); b != nullptr && b->getButtonText().isNotEmpty())
            s << "\"" << b->getButtonText() << "\" ";
        else if (c->getName().isNotEmpty())
            s << "\"" << c->getName() << "\" ";
        s << typeid (*c).name();
        return s.replace ("class ", "");
    }

    const juce::int64 seed;
    juce::Random rng;
    const int seconds;
    juce::DocumentWindow& mainWindow;
    CheckedCallback& callback;
    const juce::MouseInputSource::InputSourceType mouse = juce::MouseInputSource::InputSourceType::mouse;

    juce::uint32 endAt = 0, lastHealth = 0, pendingUpAt = 0;
    double lastTick = 0.0, longestStallMs = 0.0;
    juce::int64 lastBlocks = 0;
    int actions = 0, blockedCount = 0, dialogsClosed = 0, lockedActions = 0;
    juce::int64 lastOverruns = 0, lastLate = 0;
    int lastFaults = 0, lastXruns = 0, faultsAtStart = 0, xrunsAtStart = 0;
    bool audioStopped = false, reportedNonFinite = false, pendingUp = false;
    juce::ComponentPeer* pendingPeer = nullptr;
    juce::Point<float> pendingAt;
    std::map<juce::String, int> counts;
    std::set<juce::String> windowNames;
};

//==============================================================================
class MonkeyApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "SidecarMonkey"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        if (auto worker = PluginScannerWorker::tryToStart (commandLine))
        {
            scannerWorker = std::move (worker);
            return;
        }
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        auto option = [&args] (const char* name, const juce::String& fallback)
        {
            const int i = args.indexOf (name);
            return i >= 0 && i + 1 < args.size() ? args[i + 1].unquoted() : fallback;
        };
        const juce::int64 seed = option ("--seed", juce::String (juce::Time::currentTimeMillis() % 1000000)).getLargeIntValue();
        const int seconds = juce::jlimit (1, 24 * 3600, option ("--seconds", "120").getIntValue());
        home = juce::File (option ("--home", juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                 .getChildFile ("SidecarMonkey-" + juce::String (seed)).getFullPathName()));

        // Nothing of the user's: a home of its own, no outside actions.
        home.deleteRecursively();
        home.createDirectory();
       #if JUCE_WINDOWS
        SetEnvironmentVariableW (L"SIDECAR_HOME", home.getFullPathName().toWideCharPointer());
        SetUnhandledExceptionFilter (onCrash);
        // The runtime's own fail-fasts are crashes the log names too.
        std::set_terminate (onTerminate);
        std::signal (SIGABRT, onAbort);
        _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _set_invalid_parameter_handler (onInvalidParameter);
        _set_purecall_handler (onPureCall);
       #else
        setenv ("SIDECAR_HOME", home.getFullPathName().toRawUTF8(), 1);
       #endif
        jassert (paths::appData().isAChildOf (home));
        paths::externalActionsBlocked() = true;
        actionLog.open (home.getChildFile ("monkey.log"));
        actionLog.write ("SidecarMonkey seed " + juce::String (seed) + ", " + juce::String (seconds) + " s, home " + home.getFullPathName());
        std::printf ("SidecarMonkey: seed %lld, %d s, home %s\n", (long long) seed, seconds, home.getFullPathName().toRawUTF8());
        std::fflush (stdout);

        juce::PropertiesFile::Options options;
        options.applicationName = "SidecarMonkey";
        settings = std::make_unique<juce::PropertiesFile> (paths::appData().getChildFile ("Sidecar.settings"), options);
        settings->setValue ("pluginOverview.open", true); // the plugin overview opens with the app
        const auto startOn = option ("--interface", "wing");
        if (startOn != "wing")
            settings->setValue ("ui.interface", startOn); // ui::uiprefs::interfaceKind

        pluginHost = std::make_unique<PluginHost> (settings.get());
        // Plugins to load: Sidecar's own test plugin (a gain), never the computer's.
        pluginHost->getFormatManager().addFormat (std::make_unique<sandbox::TestPluginFormat>());
        pluginHost->getKnownPlugins().addType (sandbox::TestPluginFormat::describe (sandbox::TestPluginFormat::Kind::gain));
        engine = std::make_unique<MixerEngine>();
        engine->setNumChannels (kWingChannels);
        // A start with plugins: FX 1, 4 (a chain of two) and 21 (32 slots), on inserts.
        engine->getFx().setNumSlots (32);
        for (int slot : { 0, 3, 20 })
            engine->getFx().setPlugin (slot, sandbox::TestPluginFormat::create (sandbox::TestPluginFormat::Kind::gain));
        engine->getFx().addToChain (3, sandbox::TestPluginFormat::create (sandbox::TestPluginFormat::Kind::gain));
        engine->getChannels()[0]->params.preInsert().on = true;
        engine->getChannels()[0]->params.preInsert().fx = 0;
        engine->getChannels()[1]->params.postInsert().on = true;
        engine->getChannels()[1]->params.postInsert().fx = 20;
        if (startOn == "x32" || startOn == "promix" || startOn == "02r")
            engine->configureProfile (startOn == "x32" ? ld::kProfileX32 : startOn == "promix" ? ld::kProfileProMix01 : ld::kProfileYamaha02R);
        show = std::make_unique<Show> (*engine, *pluginHost);
        log.info ("Monkey test");

        // Only the virtual device: no hardware is opened.
        deviceManager.addAudioDeviceType (std::make_unique<VirtualAudioIODeviceType>());
        deviceManager.setCurrentAudioDeviceType (VirtualAudioIODeviceType::kTypeName, true);
        if (const auto error = deviceManager.initialise (64, 64, nullptr, true); error.isNotEmpty())
            actionLog.write ("audio device: " + error);
        // A live-use buffer (dropouts show where they would on stage).
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.bufferSize = juce::jlimit (16, 4096, option ("--buffer", "128").getIntValue());
        setup.sampleRate = 48000.0;
        if (const auto error = deviceManager.setAudioDeviceSetup (setup, true); error.isNotEmpty())
            actionLog.write ("audio device setup: " + error);
        if (auto* device = deviceManager.getCurrentAudioDevice())
            actionLog.write ("audio device: " + device->getName() + ", " + juce::String (device->getCurrentBufferSizeSamples()) + " samples at "
                             + juce::String (device->getCurrentSampleRate()) + " Hz");
        callback = std::make_unique<CheckedCallback> (*engine);
        deviceManager.addAudioCallback (callback.get());

        auto* main = new ui::MainComponent (deviceManager, *engine, *pluginHost, *show, log, *settings);
        window = std::make_unique<juce::DocumentWindow> ("Sidecar (monkey test, seed " + juce::String (seed) + ")", ui::Colours::background,
                                                         juce::DocumentWindow::allButtons);
        window->setUsingNativeTitleBar (true);
        window->setContentOwned (main, true);
        window->setResizable (true, false);
        window->centreWithSize (1600, 900);
        window->setVisible (true);

        monkey = std::make_unique<Monkey> (seed, seconds, *window, *callback);
        monkey->idle = args.contains ("--idle");
        if (const auto key = option ("--press", {}); key.isNotEmpty())
            juce::Timer::callAfterDelay (2000, [this, key]
            {
                std::function<juce::Button*(juce::Component&)> find = [&find, &key] (juce::Component& c) -> juce::Button*
                {
                    if (auto* b = dynamic_cast<juce::Button*> (&c); b != nullptr && b->isShowing() && b->getButtonText() == key)
                        return b;
                    for (auto* child : c.getChildren())
                        if (auto* b = find (*child))
                            return b;
                    return nullptr;
                };
                auto* button = window != nullptr ? find (*window) : nullptr;
                actionLog.write ("press \"" + key + "\": " + (button != nullptr ? "clicked" : "not found"));
                if (button != nullptr)
                    button->triggerClick();
            });
        monkey->onFinished = [this]
        {
            const auto summary = monkey->summary();
            std::printf ("%s\n%s\n", summary.toRawUTF8(), monkey->failed() ? "FAILED" : "PASSED");
            std::fflush (stdout);
            setApplicationReturnValue (monkey->failed() ? 1 : 0);
            juce::MessageManager::callAsync ([] { quit(); });
        };
    }

    void shutdown() override
    {
        if (scannerWorker != nullptr)
            return;
        monkey.reset();
        window.reset();
        deviceManager.removeAudioCallback (callback.get());
        deviceManager.closeAudioDevice();
        show.reset();
        engine.reset();
        pluginHost.reset();
        settings.reset();
        actionLog.write ("shut down cleanly");
    }

    // SHUTDOWN (should a key get through) or the OS: only once the monkey is done.
    void systemRequestedQuit() override
    {
        if (monkey != nullptr && monkey->isRunning())
            actionLog.write ("quit requested: ignored while the monkey runs");
        else
            quit();
    }
    void anotherInstanceStarted (const juce::String&) override {}

private:
    std::unique_ptr<PluginScannerWorker> scannerWorker;
    juce::File home;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<PluginHost> pluginHost;
    std::unique_ptr<MixerEngine> engine;
    std::unique_ptr<Show> show;
    EventLog log;
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<CheckedCallback> callback;
    std::unique_ptr<juce::DocumentWindow> window;
    std::unique_ptr<Monkey> monkey;
};

START_JUCE_APPLICATION (MonkeyApplication)
