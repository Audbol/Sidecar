#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace livedaw
{

/** The audio interface (ASIO, CoreAudio, ALSA, JACK, WASAPI ...) as a device of its
    own: which of its inputs feeds each of the console's input ports (LOCAL IN, AES50
    B and C: 3 x 48) and which console output port (LOCAL OUT, AES50 B and C) each of
    its outputs plays, like a stagebox patch. Straight through by default: interface
    input n is input port n, output n plays output port n, as before.

    The message thread sets it; the audio callback reads it (atomics). MixerEngine
    applies it in its device callback; processBlock stays on the console's ports. */
class InterfacePatch
{
public:
    static constexpr int kPorts = 144;       // the console's ports each way (3 groups of 48)
    static constexpr int kMaxChannels = 256; // the interface's channels each way

    InterfacePatch() { reset(); }

    /** The interface input feeding console input port `port`, or -1 (none: silence). */
    int inputFor (int port) const noexcept
    {
        return juce::isPositiveAndBelow (port, kPorts) ? inputOf[(size_t) port].load (std::memory_order_relaxed) : -1;
    }
    /** Patches interface input `input` (-1: none) to console input port `port`. */
    void setInputFor (int port, int input) noexcept;

    /** The console output port interface output `output` plays, or -1 (none: silence). */
    int outputSource (int output) const noexcept
    {
        return juce::isPositiveAndBelow (output, kMaxChannels) ? outputOf[(size_t) output].load (std::memory_order_relaxed) : -1;
    }
    /** Makes interface output `output` play console output port `port` (-1: none). */
    void setOutputSource (int output, int port) noexcept;

    /** The console input ports interface input `input` feeds, in order. */
    std::vector<int> portsFedBy (int input) const;

    /** Straight through (the default): the callback skips the patch altogether. */
    bool isStraight() const noexcept { return straight.load (std::memory_order_relaxed); }
    void reset() noexcept;

    /** Saved with the show (nothing when straight). */
    std::unique_ptr<juce::XmlElement> toXml() const;
    /** Loads `xml` (an <InterfacePatch>), straight without one. */
    void fromXml (const juce::XmlElement* xml);

private:
    void updateStraight() noexcept;

    std::array<std::atomic<int>, kPorts> inputOf;
    std::array<std::atomic<int>, kMaxChannels> outputOf;
    std::atomic<bool> straight { true };
};

} // namespace livedaw
