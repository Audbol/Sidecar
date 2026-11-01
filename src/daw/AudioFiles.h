#pragma once

#include "daw/Project.h"
#include "engine/AudioFileFormat.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <functional>

namespace livedaw::daw
{

/** "*.wav;*.aif;..." for file choosers (`formats` holds the formats a project
    can import: registerBasicFormats() gives WAV, AIFF, FLAC, MP3 and the
    platform's codecs). */
juce::String importWildcard (juce::AudioFormatManager& formats);
bool canImport (juce::AudioFormatManager& formats, const juce::File&);

/** A file name inside `folder` that doesn't exist yet: "<base><ext>",
    "<base>-2<ext>", ... (characters that aren't allowed in file names removed;
    `extension` with its dot). */
juce::File uniqueAudioFile (const juce::File& folder, const juce::String& base, const juce::String& extension = ".wav");

/** A 32-bit float WAV writer for `file` (replacing it), or nullptr. */
std::unique_ptr<juce::AudioFormatWriter> createWavWriter (const juce::File& file, double sampleRate, int numChannels);

struct ImportResult
{
    juce::Result result = juce::Result::ok();
    juce::File file;        // the WAV written in the project's Audio folder (or the source, referenced)
    int64 length = 0;       // samples at the project's rate
    int numChannels = 1;
    juce::String name;      // the source's name
    bool referenced = false;   // the clip plays the source where it is (KEEP IN PLACE)
    juce::String copyReason;   // KEEP IN PLACE fell back to a copy: why ("resampled copy: ...")
    int channel = 0;           // a channel (1, 2, ...) of a multichannel recording split into mono files
};

/** What a drop or a chosen folder imports: a folder gives the audio files in
    it (not its subfolders) in name order, so an X32 X-LIVE or WING-LIVE
    session folder gives its 00000001.WAV, 00000002.WAV, ...; other files stay
    as they are, unless the formats can't read them. */
juce::Array<juce::File> filesToImport (juce::AudioFormatManager& formats, const juce::Array<juce::File>&);

/** One import: a file, or a multichannel recording in one or more parts. */
struct ImportGroup
{
    juce::Array<juce::File> files;   // the parts, in playing order
    bool multichannel = false;       // more than two channels: importMultichannel()
    juce::String name;               // the tracks' name ("<name> 1", ...)
};

/** Groups the files of an import. A file with more than two channels is a
    multichannel recording; consoles that record to SD cards (X32 X-LIVE,
    WING-LIVE) split a take at 4 GB into 00000001.WAV, 00000002.WAV, ... in
    its session folder, so files there with consecutive numbers, the same
    channels and rate are parts of one recording, named after the folder. Any
    other file is an import of its own (order kept; the parts of a recording
    in number order, where its first one was). Opens every file's header. */
std::vector<ImportGroup> groupImports (juce::AudioFormatManager& formats, const juce::Array<juce::File>&);

/** Imports a multichannel recording (`parts` one after another) as a mono
    32-bit float WAV per channel at `sampleRate` in `folder`: a result per
    channel ("<name> 1", "<name> 2", ...), or a single failed one. The first
    result's copyReason says what happened ("32-channel recording split into
    mono files, 3 parts joined"). Fails before writing when the drive hasn't
    room for the copies. Any thread; `cancel` stops it, `progress` gets 0..1. */
std::vector<ImportResult> importMultichannel (juce::AudioFormatManager& formats, const juce::Array<juce::File>& parts, const juce::String& name,
                                              const juce::File& folder, double sampleRate, const std::atomic<bool>& cancel,
                                              std::function<void (float)> progress = {});

/** "44.1", "48", "88.2" (kHz, for messages). */
juce::String khzText (double sampleRate);

/** Whether a project at `sampleRate` can play `source` where it is (IMPORT:
    KEEP IN PLACE): the recorder's player reads it as it is (WAV, AIFF or FLAC,
    seeking sample-accurately; MP3 and other compressed formats are decoded
    into a copy), at the project's rate, with one or two channels, and the
    editor's waveforms read the same length. Returns empty if it can (filling
    `length` and `numChannels`), otherwise why it gets a converted copy:
    "resampled copy: 44.1 kHz file in a 48 kHz project". */
juce::String whyNotInPlace (juce::AudioFormatManager& formats, const juce::File& source, double sampleRate,
                            int64* length = nullptr, int* numChannels = nullptr);

/** IMPORT with KEEP IN PLACE (`keepInPlace`): references `source` when
    whyNotInPlace() allows it, otherwise (and always without `keepInPlace`)
    converts it into `folder` with importAudioFile() (copyReason says why). */
ImportResult importOrReference (juce::AudioFormatManager& formats, const juce::File& source, const juce::File& folder, double sampleRate,
                                bool keepInPlace, const std::atomic<bool>& cancel, std::function<void (float)> progress = {});

/** Copies a file byte for byte (COLLECT FILES), replacing `dest`; a failed or
    cancelled copy leaves no `dest`. Any thread. */
juce::Result copyFile (const juce::File& source, const juce::File& dest, const std::atomic<bool>& cancel,
                       std::function<void (float)> progress = {});

/** Converts an audio file to a 32-bit float WAV at `sampleRate` in `folder`
    (resampled with a windowed sinc; more than two channels keep the first
    two, importMultichannel() keeps them all). Runs on any thread (`formats` is only read); `cancel` stops it,
    `progress` gets 0..1. */
ImportResult importAudioFile (juce::AudioFormatManager& formats, const juce::File& source, const juce::File& folder, double sampleRate,
                              const std::atomic<bool>& cancel, std::function<void (float)> progress = {});

/** A clip to render, with its file resolved. */
struct RenderClip
{
    juce::File file;
    ClipData clip;
};

/** Renders a track's clips (clip gain, fades and the track gain; overlaps
    summed as in playback) from the timeline's start to `length` into a file
    in `format` (32-bit float WAV by default; WAV / AIFF / FLAC / MP3), so
    exported tracks line up. Runs on any thread. */
juce::Result renderTrack (juce::AudioFormatManager& formats, const std::vector<RenderClip>& clips, float trackGainDb, int numChannels, double sampleRate, int64 length,
                          const juce::File& dest, const std::atomic<bool>& cancel, std::function<void (float)> progress = {},
                          AudioFileFormat format = AudioFileFormat::wav (32));

} // namespace livedaw::daw
