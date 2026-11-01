#include "daw/AudioFiles.h"

namespace livedaw::daw
{

juce::String importWildcard (juce::AudioFormatManager& formats)
{
    juce::StringArray patterns;
    for (auto* f : formats)
        for (auto ext : f->getFileExtensions())
            patterns.addIfNotAlreadyThere ("*" + ext);
    return patterns.joinIntoString (";");
}

bool canImport (juce::AudioFormatManager& formats, const juce::File& f)
{
    return formats.findFormatForFileExtension (f.getFileExtension()) != nullptr;
}

juce::File uniqueAudioFile (const juce::File& folder, const juce::String& base, const juce::String& extension)
{
    auto clean = juce::File::createLegalFileName (base.trim()).replaceCharacters (" ", "_");
    if (clean.isEmpty())
        clean = "audio";
    auto f = folder.getChildFile (clean + extension);
    for (int i = 2; f.exists(); ++i)
        f = folder.getChildFile (clean + "-" + juce::String (i) + extension);
    return f;
}

std::unique_ptr<juce::AudioFormatWriter> createWavWriter (const juce::File& file, double sampleRate, int numChannels)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
    if (out == nullptr)
        return nullptr;
    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions()
                             .withSampleRate (sampleRate)
                             .withNumChannels (numChannels)
                             .withBitsPerSample (32)
                             .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    return wav.createWriterFor (out, options);
}

//==============================================================================
namespace
{
    /** Files read one after another as one stream (the same channels and rate). */
    class PartsReader
    {
    public:
        void add (std::unique_ptr<juce::AudioFormatReader> reader, const juce::File& file)
        {
            length += reader->lengthInSamples;
            readers.push_back (std::move (reader));
            files.add (file);
        }

        int64 getLength() const noexcept { return length; }
        juce::String currentName() const { return files[(int) juce::jmin (part, readers.size() - 1)].getFileName(); }

        /** The next `n` frames of the first `numChannels` channels (zeros after the end). */
        bool readNext (float* const* dest, int numChannels, int n)
        {
            std::vector<float*> at (dest, dest + numChannels);
            for (int done = 0; done < n;)
            {
                if (part >= readers.size())
                {
                    for (auto* d : at)
                        juce::FloatVectorOperations::clear (d, n - done);
                    return true;
                }
                auto& reader = *readers[part];
                const int count = (int) juce::jmin<int64> (n - done, reader.lengthInSamples - pos);
                if (count <= 0)
                {
                    ++part;
                    pos = 0;
                    continue;
                }
                if (! reader.read (at.data(), numChannels, pos, count))
                    return false;
                for (auto& d : at)
                    d += count;
                pos += count;
                done += count;
            }
            return true;
        }

    private:
        std::vector<std::unique_ptr<juce::AudioFormatReader>> readers;
        juce::Array<juce::File> files;
        int64 length = 0, pos = 0;
        size_t part = 0;
    };

    /** Streams the first `channels` of `source` into `write` at `ratio` (input
        samples per output sample; resampled with windowed-sinc interpolators).
        The output's length goes to `length`. */
    juce::Result convert (PartsReader& source, int channels, double ratio, const std::atomic<bool>& cancel,
                          const std::function<void (float)>& progress, const std::function<bool (const float* const*, int)>& write,
                          int64& length)
    {
        const int64 inLength = source.getLength();
        const int64 outLength = (int64) std::ceil ((double) inLength / ratio);
        constexpr int kBlock = 8192;
        juce::AudioBuffer<float> in (channels, kBlock), out (channels, kBlock);
        auto readError = [&source] { return juce::Result::fail ("Read error in " + source.currentName()); };
        const auto writeError = juce::Result::fail ("Write error (disk full?)");

        if (std::abs (ratio - 1.0) < 1.0e-9)
        {
            for (int64 pos = 0; pos < inLength; pos += kBlock)
            {
                if (cancel.load())
                    return juce::Result::fail ("Cancelled");
                const int n = (int) juce::jmin<int64> (kBlock, inLength - pos);
                if (! source.readNext (in.getArrayOfWritePointers(), channels, n))
                    return readError();
                if (! write (in.getArrayOfReadPointers(), n))
                    return writeError;
                if (progress)
                    progress ((float) pos / (float) inLength);
            }
            length = inLength;
            return juce::Result::ok();
        }

        // Stream through windowed-sinc interpolators, one per channel. Input is
        // kept in a FIFO; after the end of the file zeros flush the filter.
        std::vector<juce::WindowedSincInterpolator> interpolators ((size_t) channels);
        std::vector<std::vector<float>> fifo ((size_t) channels);
        std::vector<const float*> view ((size_t) channels);
        size_t fifoStart = 0;
        int64 readPos = 0, written = 0;
        const auto latency = (double) juce::WindowedSincInterpolator::getBaseLatency();
        int64 toSkip = (int64) std::round (latency / ratio);
        while (written < outLength)
        {
            if (cancel.load())
                return juce::Result::fail ("Cancelled");
            const int numOut = (int) juce::jmin<int64> (kBlock, outLength - written + toSkip);
            const auto needed = (size_t) std::ceil ((double) numOut * ratio) + 64;
            while (fifo[0].size() - fifoStart < needed)
            {
                const int n = (int) juce::jmin<int64> (kBlock, inLength - readPos);
                if (n > 0)
                {
                    if (! source.readNext (in.getArrayOfWritePointers(), channels, n))
                        return readError();
                    readPos += n;
                    for (int ch = 0; ch < channels; ++ch)
                        fifo[(size_t) ch].insert (fifo[(size_t) ch].end(), in.getReadPointer (ch), in.getReadPointer (ch) + n);
                }
                else
                {
                    for (auto& f : fifo)
                        f.insert (f.end(), (size_t) kBlock, 0.0f);
                }
            }
            int used = 0;
            for (int ch = 0; ch < channels; ++ch)
                used = interpolators[(size_t) ch].process (ratio, fifo[(size_t) ch].data() + fifoStart, out.getWritePointer (ch), numOut);
            fifoStart += (size_t) used;
            if (fifoStart > (size_t) kBlock * 4)
            {
                for (auto& f : fifo)
                    f.erase (f.begin(), f.begin() + (std::ptrdiff_t) fifoStart);
                fifoStart = 0;
            }
            // Drop the interpolator's latency from the start.
            const int skip = (int) juce::jmin<int64> (toSkip, numOut);
            toSkip -= skip;
            const int keep = (int) juce::jmin<int64> (numOut - skip, outLength - written);
            if (keep > 0)
            {
                for (int ch = 0; ch < channels; ++ch)
                    view[(size_t) ch] = out.getReadPointer (ch, skip);
                if (! write (view.data(), keep))
                    return writeError;
                written += keep;
            }
            if (progress)
                progress ((float) written / (float) outLength);
        }
        length = outLength;
        return juce::Result::ok();
    }

    /** "00000001.WAV" (an SD card recorder's part of a take): 1; otherwise -1. */
    int64 partNumber (const juce::File& f)
    {
        const auto stem = f.getFileNameWithoutExtension();
        return stem.isNotEmpty() && stem.length() <= 12 && stem.containsOnly ("0123456789") ? stem.getLargeIntValue() : -1;
    }
} // namespace

ImportResult importAudioFile (juce::AudioFormatManager& formats, const juce::File& source, const juce::File& folder, double sampleRate,
                              const std::atomic<bool>& cancel, std::function<void (float)> progress)
{
    ImportResult r;
    r.name = source.getFileNameWithoutExtension();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
    if (reader == nullptr)
    {
        r.result = juce::Result::fail ("Can't read " + source.getFileName() + " (unknown or damaged format)");
        return r;
    }
    if (reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
    {
        r.result = juce::Result::fail (source.getFileName() + " holds no audio");
        return r;
    }

    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    r.numChannels = channels;
    r.file = uniqueAudioFile (folder, r.name);
    auto writer = createWavWriter (r.file, sampleRate, channels);
    if (writer == nullptr)
    {
        r.result = juce::Result::fail ("Can't write " + r.file.getFullPathName());
        return r;
    }

    const double ratio = reader->sampleRate / sampleRate; // input samples per output sample
    PartsReader parts;
    parts.add (std::move (reader), source);
    auto converted = convert (parts, channels, ratio, cancel, progress, [&writer, channels] (const float* const* data, int n)
    {
        return writer->writeFromFloatArrays (data, channels, n);
    }, r.length);
    if (converted.wasOk() && ! writer->flush())
        converted = juce::Result::fail ("Write error (disk full?)");
    writer.reset();
    if (converted.failed())
    {
        r.file.deleteFile();
        r.result = converted;
        return r;
    }
    if (progress)
        progress (1.0f);
    return r;
}

//==============================================================================
juce::Array<juce::File> filesToImport (juce::AudioFormatManager& formats, const juce::Array<juce::File>& items)
{
    juce::Array<juce::File> files;
    for (const auto& item : items)
    {
        if (item.isDirectory())
        {
            auto children = item.findChildFiles (juce::File::findFiles | juce::File::ignoreHiddenFiles, false);
            std::sort (children.begin(), children.end(), [] (const juce::File& a, const juce::File& b)
            {
                return a.getFileName().compareNatural (b.getFileName()) < 0;
            });
            for (const auto& f : children)
                if (! f.getFileName().startsWith ("._") && canImport (formats, f)) // "._00000001.WAV": a Mac's metadata on the card
                    files.add (f);
        }
        else if (canImport (formats, item))
        {
            files.add (item);
        }
    }
    return files;
}

std::vector<ImportGroup> groupImports (juce::AudioFormatManager& formats, const juce::Array<juce::File>& files)
{
    struct Info
    {
        juce::File file;
        int channels = 0;
        double rate = 0.0;
        int64 number = -1;
        bool used = false;
    };
    std::vector<Info> infos;
    for (const auto& f : files)
    {
        Info info;
        info.file = f;
        info.number = partNumber (f);
        if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f)); reader != nullptr)
        {
            info.channels = (int) reader->numChannels;
            info.rate = reader->sampleRate;
        }
        infos.push_back (info);
    }

    std::vector<ImportGroup> groups;
    for (size_t i = 0; i < infos.size(); ++i)
    {
        auto& first = infos[i];
        if (first.used)
            continue;
        first.used = true;
        if (first.channels <= 2 || first.number < 0)
        {
            groups.push_back ({ { first.file }, first.channels > 2, first.file.getFileNameWithoutExtension() });
            continue;
        }

        // An SD card take: the numbered files beside it with the same channels
        // and rate, in number order; a gap starts another take.
        std::vector<Info*> parts { &first };
        for (size_t j = i + 1; j < infos.size(); ++j)
        {
            auto& other = infos[j];
            if (! other.used && other.number >= 0 && other.channels == first.channels && std::abs (other.rate - first.rate) < 0.5
                && other.file.getParentDirectory() == first.file.getParentDirectory())
            {
                other.used = true;
                parts.push_back (&other);
            }
        }
        std::sort (parts.begin(), parts.end(), [] (const Info* a, const Info* b) { return a->number < b->number; });
        auto folderName = first.file.getParentDirectory().getFileName();
        if (folderName.isEmpty())
            folderName = parts.front()->file.getFileNameWithoutExtension();
        const size_t firstGroup = groups.size();
        for (size_t k = 0; k < parts.size(); ++k)
        {
            if (k == 0 || parts[k]->number != parts[k - 1]->number + 1)
                groups.push_back ({ {}, true, folderName });
            groups.back().files.addIfNotAlreadyThere (parts[k]->file);
        }
        // A take with a gap: name each run after its first part ("4B2A1C3D-3").
        if (groups.size() - firstGroup > 1)
            for (size_t g = firstGroup; g < groups.size(); ++g)
                groups[g].name = folderName + "-" + juce::String (partNumber (groups[g].files[0]));
    }
    return groups;
}

std::vector<ImportResult> importMultichannel (juce::AudioFormatManager& formats, const juce::Array<juce::File>& parts, const juce::String& name,
                                              const juce::File& folder, double sampleRate, const std::atomic<bool>& cancel,
                                              std::function<void (float)> progress)
{
    auto failed = [&name] (const juce::String& message)
    {
        ImportResult r;
        r.name = name;
        r.result = juce::Result::fail (message);
        return std::vector<ImportResult> { r };
    };

    PartsReader source;
    int channels = 0;
    double rate = 0.0;
    for (const auto& f : parts)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        if (reader == nullptr)
            return failed ("Can't read " + f.getFileName() + " (unknown or damaged format)");
        if (reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
            return failed (f.getFileName() + " holds no audio");
        if (channels == 0)
        {
            channels = (int) reader->numChannels;
            rate = reader->sampleRate;
        }
        else if ((int) reader->numChannels != channels || std::abs (reader->sampleRate - rate) > 0.5)
        {
            return failed (f.getFileName() + " doesn't continue " + parts[0].getFileName() + " (other channels or sample rate)");
        }
        source.add (std::move (reader), f);
    }
    if (channels == 0)
        return failed ("Nothing to import");

    // Room for the mono copies (32-bit float) and a margin.
    const double ratio = rate / sampleRate;
    const auto outLength = (juce::int64) std::ceil ((double) source.getLength() / ratio);
    const auto bytes = (juce::int64) channels * outLength * 4 + ((juce::int64) 64 << 20);
    folder.createDirectory();
    if (const auto available = folder.getBytesFreeOnVolume(); available > 0 && available < bytes)
        return failed (name + ": not enough space on the project's drive (" + juce::File::descriptionOfSizeInBytes (bytes) + " needed, "
                       + juce::File::descriptionOfSizeInBytes (available) + " free)");

    std::vector<ImportResult> results ((size_t) channels);
    std::vector<std::unique_ptr<juce::AudioFormatWriter>> writers;
    auto removeAll = [&]
    {
        writers.clear();
        for (auto& r : results)
            if (r.file != juce::File())
                r.file.deleteFile();
    };
    for (int ch = 0; ch < channels; ++ch)
    {
        auto& r = results[(size_t) ch];
        r.name = name + " " + juce::String (ch + 1);
        r.channel = ch + 1;
        r.numChannels = 1;
        r.file = uniqueAudioFile (folder, r.name);
        auto writer = createWavWriter (r.file, sampleRate, 1);
        if (writer == nullptr)
        {
            const auto path = r.file.getFullPathName();
            removeAll();
            return failed ("Can't write " + path);
        }
        writers.push_back (std::move (writer));
    }

    int64 length = 0;
    auto converted = convert (source, channels, ratio, cancel, progress, [&writers] (const float* const* data, int n)
    {
        for (size_t ch = 0; ch < writers.size(); ++ch)
            if (! writers[ch]->writeFromFloatArrays (data + ch, 1, n))
                return false;
        return true;
    }, length);
    for (auto& w : writers)
        if (converted.wasOk() && ! w->flush())
            converted = juce::Result::fail ("Write error (disk full?)");
    if (converted.failed())
    {
        removeAll();
        return failed (converted.getErrorMessage());
    }
    writers.clear();

    for (auto& r : results)
        r.length = length;
    auto& what = results[0].copyReason;
    what << channels << "-channel recording split into mono files";
    if (parts.size() > 1)
        what << ", " << parts.size() << " parts joined";
    if (std::abs (rate - sampleRate) > 0.5)
        what << ", resampled from " << khzText (rate) << " kHz";
    if (progress)
        progress (1.0f);
    return results;
}

//==============================================================================
juce::String khzText (double sampleRate)
{
    const double k = sampleRate / 1000.0;
    return std::abs (k - std::round (k)) < 1.0e-6 ? juce::String ((int) std::round (k)) : juce::String (k, 1);
}

juce::String whyNotInPlace (juce::AudioFormatManager& formats, const juce::File& source, double sampleRate, int64* length, int* numChannels)
{
    // The recorder's player (the engine's readers) must read it as it is.
    const auto ext = source.getFileExtension().trimCharactersAtStart (".").toUpperCase();
    const auto engine = AudioFileReader::open (source);
    if (engine == nullptr || engine->getLength() <= 0 || engine->getSampleRate() <= 0.0)
        return "converted copy: the player can't read " + (ext.isNotEmpty() ? ext + " files" : juce::String ("this file")) + " as they are";
    if (engine->getType() == AudioFileFormat::Type::Mp3)
        return "decoded copy: MP3 is decoded once, so it plays sample-accurately and light on the disk thread";

    const bool resample = std::abs (engine->getSampleRate() - sampleRate) > 0.5;
    const bool manyChannels = engine->getNumChannels() > 2;
    if (resample || manyChannels)
    {
        juce::String what = resample ? "resampled copy" : "copy";
        if (manyChannels)
            what << " of the first two channels";
        juce::String file;
        if (resample)
            file << khzText (engine->getSampleRate()) << " kHz ";
        if (manyChannels)
            file << engine->getNumChannels() << "-channel ";
        file << "file";
        if (resample)
            file << " in a " << khzText (sampleRate) << " kHz project";
        return what + ": " + file;
    }

    // The editor's waveforms and the export read it through JUCE: they must agree.
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
    if (reader == nullptr || (int64) reader->lengthInSamples != engine->getLength() || (int) reader->numChannels != engine->getNumChannels())
        return "converted copy: the waveform reader and the player disagree about this " + (ext.isNotEmpty() ? ext + " " : juce::String()) + "file";

    if (length != nullptr)
        *length = engine->getLength();
    if (numChannels != nullptr)
        *numChannels = engine->getNumChannels();
    return {};
}

ImportResult importOrReference (juce::AudioFormatManager& formats, const juce::File& source, const juce::File& folder, double sampleRate,
                                bool keepInPlace, const std::atomic<bool>& cancel, std::function<void (float)> progress)
{
    if (! keepInPlace)
        return importAudioFile (formats, source, folder, sampleRate, cancel, std::move (progress));
    int64 length = 0;
    int channels = 1;
    const auto why = whyNotInPlace (formats, source, sampleRate, &length, &channels);
    if (why.isNotEmpty())
    {
        auto r = importAudioFile (formats, source, folder, sampleRate, cancel, std::move (progress));
        r.copyReason = why;
        return r;
    }
    ImportResult r;
    r.name = source.getFileNameWithoutExtension();
    r.file = source;
    r.length = length;
    r.numChannels = channels;
    r.referenced = true;
    if (progress)
        progress (1.0f);
    return r;
}

juce::Result copyFile (const juce::File& source, const juce::File& dest, const std::atomic<bool>& cancel, std::function<void (float)> progress)
{
    juce::FileInputStream in (source);
    if (! in.openedOk())
        return juce::Result::fail ("Can't read " + source.getFullPathName());
    dest.getParentDirectory().createDirectory();
    dest.deleteFile();
    auto fail = [&dest] (const juce::String& message)
    {
        dest.deleteFile();
        return juce::Result::fail (message);
    };
    {
        juce::FileOutputStream out (dest);
        if (! out.openedOk())
            return fail ("Can't write " + dest.getFullPathName());
        const auto total = juce::jmax<juce::int64> (1, in.getTotalLength());
        juce::HeapBlock<char> buffer (1 << 20);
        juce::int64 done = 0;
        for (;;)
        {
            if (cancel.load())
                return fail ("Cancelled");
            const int n = in.read (buffer.get(), 1 << 20);
            if (n < 0)
                return fail ("Read error in " + source.getFullPathName());
            if (n == 0)
                break;
            if (! out.write (buffer.get(), (size_t) n))
                return fail ("Write error (disk full?)");
            done += n;
            if (progress)
                progress ((float) ((double) done / (double) total));
        }
        out.flush();
        if (out.getStatus().failed())
            return fail ("Write error (disk full?)");
    }
    if (dest.getSize() != source.getSize())
        return fail ("The copy of " + source.getFileName() + " is incomplete");
    return juce::Result::ok();
}

//==============================================================================
juce::Result renderTrack (juce::AudioFormatManager& formats, const std::vector<RenderClip>& clips, float trackGainDb, int numChannels,
                          double sampleRate, int64 length, const juce::File& dest, const std::atomic<bool>& cancel,
                          std::function<void (float)> progress, AudioFileFormat format)
{
    numChannels = juce::jlimit (1, 2, numChannels);
    dest.getParentDirectory().createDirectory();
    juce::String error;
    auto writer = AudioFileWriter::create (dest, numChannels, sampleRate, format, &error);
    if (writer == nullptr)
        return juce::Result::fail (error.isNotEmpty() ? error : "Can't write " + dest.getFullPathName());

    std::vector<std::unique_ptr<juce::AudioFormatReader>> readers;
    for (auto& c : clips)
        readers.emplace_back (formats.createReaderFor (c.file));

    const float trackGain = trackGainDb <= -100.0f ? 0.0f : std::pow (10.0f, trackGainDb / 20.0f);
    constexpr int kBlock = 8192;
    juce::AudioBuffer<float> mix (numChannels, kBlock), part (2, kBlock);
    for (int64 pos = 0; pos < length; pos += kBlock)
    {
        if (cancel.load())
        {
            writer->abort();
            return juce::Result::fail ("Cancelled");
        }
        const int n = (int) juce::jmin<int64> (kBlock, length - pos);
        mix.clear();
        for (size_t i = 0; i < clips.size(); ++i)
        {
            const auto& c = clips[i].clip;
            auto* reader = readers[i].get();
            if (reader == nullptr || c.mute)
                continue;
            const int64 from = juce::jmax (pos, c.start), to = juce::jmin (pos + n, c.end());
            if (to <= from)
                continue;
            const int count = (int) (to - from);
            const int offset = (int) (from - pos);
            part.clear();
            reader->read (&part, 0, count, c.fileOffset + (from - c.start), true, reader->numChannels > 1);
            if (reader->numChannels < 2)
                part.copyFrom (1, 0, part, 0, 0, count);
            const float clipGain = (c.gainDb <= -100.0f ? 0.0f : std::pow (10.0f, c.gainDb / 20.0f)) * trackGain;
            for (int s = 0; s < count; ++s)
            {
                const int64 t = from + s - c.start; // position inside the clip
                float g = clipGain;
                if (t < c.fadeIn)
                    g *= clipmath::fadeGain (c.fadeInShape, (float) ((double) t / (double) c.fadeIn));
                if (t >= c.length - c.fadeOut && c.fadeOut > 0)
                    g *= clipmath::fadeGain (c.fadeOutShape, (float) ((double) (c.length - t) / (double) c.fadeOut));
                for (int ch = 0; ch < numChannels; ++ch)
                    mix.addSample (ch, offset + s, part.getSample (numChannels == 1 ? 0 : ch, s) * g);
            }
        }
        if (! writer->write (mix.getArrayOfReadPointers(), numChannels, n))
        {
            writer->abort();
            return juce::Result::fail ("Write error (disk full?)");
        }
        if (progress)
            progress ((float) ((double) pos / (double) juce::jmax<int64> (1, length)));
    }
    if (writer->finalize() < 0)
        return juce::Result::fail ("Write error (disk full?)");
    return juce::Result::ok();
}

} // namespace livedaw::daw
