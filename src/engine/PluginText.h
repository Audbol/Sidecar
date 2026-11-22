#pragma once

#include <juce_core/juce_core.h>

#include <cstring>

namespace livedaw
{

/** Text a plugin reports (its name, maker, program and parameter names) as
    valid UTF-8. A plugin built with MSVC without /utf-8 hands a non-ASCII name
    (Trøkk) over in the Windows code page; juce::String keeps those bytes as they
    are, and JUCE's byte counts and stream writes then disagree about the text's
    length, so a scan result or a sandbox reply is read out of step. Text that
    isn't valid UTF-8 is read as Windows-1252 instead, which is what such a build
    produces on most systems (elsewhere the letters may be wrong, never the
    length). */
inline juce::String cleanPluginText (const juce::String& text)
{
    const auto* raw = text.toRawUTF8();
    const auto numBytes = (int) std::strlen (raw);
    if (juce::CharPointer_UTF8::isValidString (raw, numBytes))
        return text;

    // Windows-1252's 0x80-0x9F; the rest of its upper half is Latin-1.
    static constexpr juce::juce_wchar cp1252[32] = { 0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                                     0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
                                                     0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                                     0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178 };
    juce::String out;
    out.preallocateBytes ((size_t) numBytes * 3);
    for (int i = 0; i < numBytes; ++i)
    {
        const auto b = (unsigned char) raw[i];
        out += juce::String::charToString (b >= 0x80 && b < 0xa0 ? cp1252[b - 0x80] : (juce::juce_wchar) b);
    }
    return out;
}

/** Writes `text` as its exact bytes and a terminating zero, which
    InputStream::readString reads back unchanged. OutputStream::writeString
    counts the bytes by decoding them, which for text that isn't valid UTF-8
    (see cleanPluginText) writes more or fewer bytes than there are, so
    whatever follows is read out of step. For a plugin's identity (its name
    must stay byte for byte what the plugin reports: JUCE finds the plugin in
    its file by it). */
inline void writeExactString (juce::OutputStream& out, const juce::String& text)
{
    const auto* raw = text.toRawUTF8();
    out.write (raw, std::strlen (raw) + 1);
}

} // namespace livedaw
