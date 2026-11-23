#include "TestHelpers.h"
#include "engine/AudioWatch.h"
#include "engine/VirtualAudioDevice.h"

#include <juce_audio_devices/juce_audio_devices.h>

/** Checks that the platform's pro audio driver types are compiled in. CI runs
    this on Windows, macOS and Linux runners. */
class DeviceTests final : public juce::UnitTest
{
public:
    DeviceTests() : juce::UnitTest ("Devices", "LiveDAW") {}

    void runTest() override
    {
        beginTest ("Pro audio driver types are available");

        juce::AudioDeviceManager manager;
        juce::OwnedArray<juce::AudioIODeviceType> types;
        manager.createAudioDeviceTypes (types);

        juce::StringArray names;
        for (auto* t : types)
            names.add (t->getTypeName());
        logMessage ("Driver types: " + names.joinIntoString (", "));

       #if JUCE_WINDOWS
        expect (names.contains ("ASIO"), "ASIO support is not compiled in");
        expect (names.contains ("Windows Audio"), "WASAPI support is missing");
       #elif JUCE_MAC
        expect (names.contains ("CoreAudio"), "CoreAudio support is missing");
       #elif JUCE_LINUX
        expect (names.contains ("ALSA"), "ALSA support is missing");
        expect (names.contains ("JACK"), "JACK support is missing");
       #endif

        using namespace livedaw;
        using audiowatch::Callbacks;
        beginTest ("The watchdog gives a starting device time, and leaves one that never starts alone (issue #29)");
        {
            const juce::uint32 t0 = 0xfffff000u; // just before the millisecond counter wraps
            expect (audiowatch::check (false, false, t0, t0, 0) == Callbacks::idle);
            expect (audiowatch::check (true, false, t0 + 1000, t0, 0) == Callbacks::starting,
                    "no block yet a second after it started: not stalled (it was restarted at once before)");
            expect (audiowatch::check (true, false, t0 + 5000, t0, 0) == Callbacks::starting);
            expect (audiowatch::check (true, false, t0 + 7000, t0, 0) == Callbacks::silent);
            expect (audiowatch::check (true, true, t0 + 7000, t0, t0 + 6000) == Callbacks::running);
            expect (audiowatch::check (true, true, t0 + 9000, t0, t0 + 6000) == Callbacks::stalled);
        }

        beginTest ("A first start passes over the drivers of a computer's own sound");
        {
            expectEquals (audiowatch::firstStartDevice ({}), -1);
            expectEquals (audiowatch::firstStartDevice ({ "Realtek ASIO" }), -1);
            expectEquals (audiowatch::firstStartDevice ({ "Realtek ASIO", "Generic Low Latency ASIO Driver" }), -1);
            expectEquals (audiowatch::firstStartDevice ({ "Realtek ASIO", "USB Audio Interface ASIO" }), 1);
            expectEquals (audiowatch::firstStartDevice ({ "Studio Interface", "Realtek ASIO" }), 0);
            expect (! audiowatch::isStandInDriver ("system"), "JACK's own device");
        }

        beginTest ("The engine knows whether a device has sent a block since it started");
        {
            VirtualAudioIODeviceType type;
            std::unique_ptr<juce::AudioIODevice> device (type.createDevice (VirtualAudioIODeviceType::kDeviceName,
                                                                            VirtualAudioIODeviceType::kDeviceName));
            expect (device != nullptr);
            if (device == nullptr)
                return;
            juce::BigInteger two;
            two.setRange (0, 2, true);
            expectEquals (device->open (two, two, 48000.0, 256), juce::String());

            MixerEngine engine (0);
            test::setChannels (engine, 2);
            const auto before = juce::Time::getMillisecondCounter();
            engine.audioDeviceAboutToStart (device.get());
            expect (! engine.hasCalledBackSinceStart());
            expect (engine.getDeviceStartTime() - before < 1000);

            juce::AudioBuffer<float> in (2, 256), out (2, 256);
            in.clear();
            engine.audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, 256, {});
            expect (engine.hasCalledBackSinceStart());

            engine.audioDeviceStopped();
            engine.audioDeviceAboutToStart (device.get());
            expect (! engine.hasCalledBackSinceStart(), "a restart waits for a new first block");
            engine.audioDeviceStopped();
            device->close();
        }
    }
};

static DeviceTests deviceTests;
