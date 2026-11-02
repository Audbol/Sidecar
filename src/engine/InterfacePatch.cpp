#include "engine/InterfacePatch.h"

namespace livedaw
{

void InterfacePatch::setInputFor (int port, int input) noexcept
{
    if (! juce::isPositiveAndBelow (port, kPorts))
        return;
    inputOf[(size_t) port].store (juce::isPositiveAndBelow (input, kMaxChannels) ? input : -1, std::memory_order_relaxed);
    updateStraight();
}

void InterfacePatch::setOutputSource (int output, int port) noexcept
{
    if (! juce::isPositiveAndBelow (output, kMaxChannels))
        return;
    outputOf[(size_t) output].store (juce::isPositiveAndBelow (port, kPorts) ? port : -1, std::memory_order_relaxed);
    updateStraight();
}

std::vector<int> InterfacePatch::portsFedBy (int input) const
{
    std::vector<int> ports;
    for (int p = 0; p < kPorts; ++p)
        if (inputFor (p) == input)
            ports.push_back (p);
    return ports;
}

void InterfacePatch::reset() noexcept
{
    for (int p = 0; p < kPorts; ++p)
        inputOf[(size_t) p].store (p, std::memory_order_relaxed);
    for (int o = 0; o < kMaxChannels; ++o)
        outputOf[(size_t) o].store (o < kPorts ? o : -1, std::memory_order_relaxed);
    straight.store (true);
}

void InterfacePatch::updateStraight() noexcept
{
    bool s = true;
    for (int p = 0; p < kPorts && s; ++p)
        s = inputFor (p) == p;
    for (int o = 0; o < kMaxChannels && s; ++o)
        s = outputSource (o) == (o < kPorts ? o : -1);
    straight.store (s);
}

std::unique_ptr<juce::XmlElement> InterfacePatch::toXml() const
{
    if (isStraight())
        return nullptr;
    auto xml = std::make_unique<juce::XmlElement> ("InterfacePatch");
    juce::StringArray ins, outs;
    for (int p = 0; p < kPorts; ++p)
        ins.add (juce::String (inputFor (p)));
    for (int o = 0; o < kMaxChannels; ++o)
        outs.add (juce::String (outputSource (o)));
    xml->setAttribute ("inputs", ins.joinIntoString (","));
    xml->setAttribute ("outputs", outs.joinIntoString (","));
    return xml;
}

void InterfacePatch::fromXml (const juce::XmlElement* xml)
{
    reset();
    if (xml == nullptr || ! xml->hasTagName ("InterfacePatch"))
        return;
    const auto ins = juce::StringArray::fromTokens (xml->getStringAttribute ("inputs"), ",", {});
    for (int p = 0; p < juce::jmin (kPorts, ins.size()); ++p)
        inputOf[(size_t) p].store (juce::jlimit (-1, kMaxChannels - 1, ins[p].getIntValue()), std::memory_order_relaxed);
    const auto outs = juce::StringArray::fromTokens (xml->getStringAttribute ("outputs"), ",", {});
    for (int o = 0; o < juce::jmin (kMaxChannels, outs.size()); ++o)
        outputOf[(size_t) o].store (juce::jlimit (-1, kPorts - 1, outs[o].getIntValue()), std::memory_order_relaxed);
    updateStraight();
}

} // namespace livedaw
