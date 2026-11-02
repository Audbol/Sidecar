#include "engine/PluginScanner.h"
#include "sandbox/TestPlugins.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <typeinfo>
#include <utility>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

// The sanitizers catch faults themselves, with better reports: leave their handlers be.
#if defined (__SANITIZE_ADDRESS__) || defined (__SANITIZE_THREAD__)
 #define LIVEDAW_TESTS_SANITIZED 1
#elif defined (__has_feature)
 #if __has_feature (address_sanitizer) || __has_feature (thread_sanitizer)
  #define LIVEDAW_TESTS_SANITIZED 1
 #endif
#endif

namespace
{
    juce::UnitTestRunner* runningTests = nullptr;

    /** A crash of the tests (a fault, an abort, an uncaught exception, a runtime
        fail-fast) prints what it was, the test it happened in and the stack, so a
        CI log shows where (the tests are built with symbols). */
    void reportCrash (const juce::String& why)
    {
        juce::String test ("(before the tests)");
        if (runningTests != nullptr && runningTests->getNumResults() > 0)
            if (auto* r = runningTests->getResult (runningTests->getNumResults() - 1))
                test = r->unitTestName + " / " + r->subcategoryName;
        std::cout.flush();
        std::fputs (("\nCRASH in " + test + ": " + why + ". Stack:\n" + juce::SystemStats::getStackBacktrace()).toRawUTF8(), stderr);
        std::fflush (stderr);
    }

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
        reportCrash (why);
        std::signal (SIGABRT, SIG_DFL);
        std::abort();
    }

   #if JUCE_WINDOWS
    LONG WINAPI onException (EXCEPTION_POINTERS* info)
    {
        reportCrash ("exception 0x" + juce::String::toHexString ((juce::int64) info->ExceptionRecord->ExceptionCode));
        TerminateProcess (GetCurrentProcess(), info->ExceptionRecord->ExceptionCode);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // abort(), an invalid parameter to a C runtime function or a pure virtual call end
    // in the runtime's fail-fast (0xC0000409), which no exception filter sees.
    [[noreturn]] void failFast (const char* why)
    {
        reportCrash (why);
        TerminateProcess (GetCurrentProcess(), 3);
        std::_Exit (3);
    }

    void installCrashReport()
    {
        SetUnhandledExceptionFilter (onException);
        std::set_terminate (onTerminate);
        std::signal (SIGABRT, [] (int) { failFast ("abort"); });
        _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _set_invalid_parameter_handler ([] (const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
                                        { failFast ("an invalid parameter to a C runtime function"); });
        _set_purecall_handler ([] { failFast ("a pure virtual function call"); });
    }
   #else
    void onSignal (int sig)
    {
        reportCrash ("signal " + juce::String (sig) + (sig == SIGSEGV ? " (SIGSEGV)" : sig == SIGBUS ? " (SIGBUS)" : sig == SIGABRT ? " (SIGABRT)" : ""));
        // Ends as it would have, so CTest names the signal.
        std::signal (sig, SIG_DFL);
        std::raise (sig);
    }

    void installCrashReport()
    {
        std::set_terminate (onTerminate);
       #if ! LIVEDAW_TESTS_SANITIZED
        for (int sig : { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT })
            std::signal (sig, onSignal);
       #endif
    }
   #endif
} // namespace

int main (int argc, char** argv)
{
    installCrashReport();

#if JUCE_LINUX || JUCE_BSD
    // JUCE installs an X error handler only in GUI apps; without one, Xlib's default
    // ends this console runner on any X error, such as JUCE asking a bare Xvfb (no
    // window manager) for _NET_WM_STATE while a plugin window test runs. Report and go on.
    static juce::DynamicLibrary x11 ("libX11.so.6");
    using Handler = int (*) (void*, void*);
    if (auto* setHandler = reinterpret_cast<Handler (*) (Handler)> (x11.getFunction ("XSetErrorHandler")))
        setHandler ([] (void*, void*)
        {
            static bool reported = false;
            if (! std::exchange (reported, true))
                std::cerr << "(X errors are ignored)" << std::endl;
            return 0;
        });
#endif

    juce::ScopedJuceInitialiser_GUI init; // engine uses the message manager / timers

    // The out-of-process plugin scanner relaunches this executable as its worker.
    {
        juce::StringArray args;
        for (int i = 1; i < argc; ++i)
            args.add (argv[i]);

        const auto addTestFormat = [] (juce::AudioPluginFormatManager& formats)
        {
            if (livedaw::sandbox::TestPluginFormat::isEnabledByEnvironment())
                formats.addFormat (std::make_unique<livedaw::sandbox::TestPluginFormat>());
        };

        if (auto worker = livedaw::PluginScannerWorker::tryToStart (args.joinIntoString (" "), addTestFormat))
        {
            juce::MessageManager::getInstance()->runDispatchLoop();
            return 0;
        }
    }

    // JUCE's runner logs through juce::Logger, which on Windows goes to the
    // debugger only; print to stdout so a CI log shows what failed.
    struct StdoutRunner final : juce::UnitTestRunner
    {
        void logMessage (const juce::String& message) override { std::cout << message << std::endl; }
    } runner;
    runner.setAssertOnFailure (false);
    runningTests = &runner;

    // An argument names a category, or one test ("Faceplates").
    juce::Array<juce::UnitTest*> named;
    if (argc > 1)
        for (auto* t : juce::UnitTest::getAllTests())
            if (t->getName() == juce::String (argv[1]))
                named.add (t);
    if (! named.isEmpty())
        runner.runTests (named);
    else if (argc > 1)
        runner.runTestsInCategory (argv[1]);
    else
        runner.runTestsInCategory ("LiveDAW");

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (auto* r = runner.getResult (i))
            failures += r->failures;

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED: " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
