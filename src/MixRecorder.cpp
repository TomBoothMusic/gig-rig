#include "MixRecorder.h"

namespace gigrig
{

MixRecorder::MixRecorder()
{
    thread.startThread();
}

MixRecorder::~MixRecorder()
{
    stop();
    thread.stopThread (4000);
}

double MixRecorder::seconds() const noexcept
{
    return engineRateNow > 0.0 ? (double) samples.load() / engineRateNow : 0.0;
}

juce::String MixRecorder::start (const juce::File& folder, const std::vector<Track>& tracks, double engineRate, int maxBlock)
{
    stop();

    if (tracks.empty())
        return "Nothing is set to be recorded: give at least one channel a record point.";

    if (! folder.createDirectory())
        return "Could not make the folder " + folder.getFullPathName();

    const int newFactor = engineRate >= 88000.0 ? 2 : 1;
    const double newRate = engineRate / newFactor;
    std::array<Slot, maxTracks> fresh;
    juce::StringArray files;
    int number = 0;

    for (const auto& t : tracks)
    {
        if (t.slot < 0 || t.slot >= maxTracks || fresh[(size_t) t.slot].writer != nullptr)
            continue;

        const auto file = folder.getChildFile (juce::String (++number).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (t.name) + ".wav");
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

        if (stream == nullptr)
            return "Could not write " + file.getFullPathName();

        juce::WavAudioFormat wav;
        auto made = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                     .withSampleRate (newRate)
                                                     .withNumChannels (t.channels)
                                                     .withBitsPerSample (24));
        if (made == nullptr)
            return "Could not start a WAV file at " + juce::String (newRate) + " Hz";

        auto& s = fresh[(size_t) t.slot];
        s.channels = juce::jlimit (1, 2, t.channels);
        s.writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (made.release(), thread, 1 << 17);     // well over a second of room

        const auto coefs = fx::HalfBand::design (0.04);
        for (int c = 0; c < 2; ++c)
        {
            s.down[(size_t) c].setCoefs (coefs);
            s.down[(size_t) c].clear();
            s.half[(size_t) c].assign ((size_t) juce::jmax (16, maxBlock), 0.0f);
        }

        files.add (file.getFileName());
    }

    {
        const juce::SpinLock::ScopedLockType hold (lock);

        for (int i = 0; i < maxTracks; ++i)
        {
            slots[(size_t) i] = std::move (fresh[(size_t) i]);
            active[(size_t) i].store (slots[(size_t) i].writer != nullptr);
        }

        factor = newFactor;
        engineRateNow = engineRate;
        writtenRate = newRate;
        samples.store (0);
        lost.store (0);
    }

    lastFolder = folder;
    lastFiles = files;
    recording.store (true);
    return {};
}

void MixRecorder::stop()
{
    recording.store (false);
    std::array<std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>, maxTracks> old;

    {
        const juce::SpinLock::ScopedLockType hold (lock);

        for (int i = 0; i < maxTracks; ++i)
        {
            old[(size_t) i] = std::move (slots[(size_t) i].writer);
            active[(size_t) i].store (false);
        }
    }

    for (auto& w : old)
        w.reset();      // writes out the rest and closes the file
}

void MixRecorder::write (int slot, const float* left, const float* right, int n) noexcept
{
    if (! wants (slot) || left == nullptr || n <= 0)
        return;

    // The lock is only ever held by the message thread for a moment while it starts or stops;
    // if it is busy, this block is simply not recorded.
    const juce::SpinLock::ScopedTryLockType hold (lock);

    if (! hold.isLocked())
        return;

    auto& s = slots[(size_t) slot];

    if (s.writer == nullptr)
        return;

    if (right == nullptr)
        right = left;

    const float* source[2] { left, right };

    if (factor == 1)
    {
        if (! s.writer->write (source, n))
            lost.fetch_add (n);
        return;
    }

    if (n > (int) s.half[0].size() * 2)
        return;

    // Two samples in, one out, through a half-band filter so nothing above the new top folds back down.
    int made = 0;
    bool have = s.haveCarry;

    for (int c = 0; c < s.channels; ++c)
    {
        made = 0;
        have = s.haveCarry;
        float carry = s.carry[(size_t) c];

        for (int i = 0; i < n; ++i)
        {
            if (! have) { carry = source[c][i]; have = true; }
            else        { s.half[(size_t) c][(size_t) made++] = (float) s.down[(size_t) c].down (carry, source[c][i]); have = false; }
        }

        s.carry[(size_t) c] = carry;
    }

    s.haveCarry = have;

    const float* out[2] { s.half[0].data(), s.half[s.channels > 1 ? 1 : 0].data() };

    if (made > 0 && ! s.writer->write (out, made))
        lost.fetch_add (made * 2);
}

void MixRecorder::endBlock (int n) noexcept
{
    if (recording.load (std::memory_order_relaxed))
        samples.fetch_add (n, std::memory_order_relaxed);
}

juce::String MixRecorder::statusText() const
{
    if (lastFiles.isEmpty())
        return "Recording: nothing recorded yet.";

    juce::String t;
    t << (isRecording() ? "Recording now: " : "Last recording: ") << juce::String (seconds(), 1) << " s, " << lastFiles.size()
      << " files at " << juce::String (writtenRate / 1000.0, 1) << " kHz, 24-bit, in " << lastFolder.getFullPathName() << ".";

    if (lostSamples() > 0)
        t << " WARNING: " << lostSamples() << " samples were lost because the drive could not keep up.";
    else
        t << " Nothing lost.";

    return t;
}

} // namespace gigrig
