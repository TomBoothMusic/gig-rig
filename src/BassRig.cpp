#include "BassRig.h"

namespace gigrig
{

namespace
{
    // Counts as "playing" for the level match: the dry guitar above about -50 dBFS.
    constexpr double playingLevel = 0.003;

    float peakOf (const float* data, int n) noexcept
    {
        float p = 0.0f;
        for (int i = 0; i < n; ++i)
            p = juce::jmax (p, std::abs (data[i]));
        return p;
    }

    void raise (std::atomic<float>& slot, float value) noexcept
    {
        if (value > slot.load (std::memory_order_relaxed))
            slot.store (value, std::memory_order_relaxed);
    }

    void addTo (std::atomic<double>& slot, double value) noexcept
    {
        slot.store (slot.load (std::memory_order_relaxed) + value, std::memory_order_relaxed);
    }
}

BassRig::BassRig()
{
    writerThread.startThread();
}

BassRig::~BassRig()
{
    stopCapture();
    writerThread.stopThread (2000);
}

void BassRig::setSettings (const BassSettings& s)
{
    const juce::SpinLock::ScopedLockType hold (settingsLock);
    shared = s;
    settingsChanged.store (true);
}

BassSettings BassRig::getSettings() const
{
    const juce::SpinLock::ScopedLockType hold (settingsLock);
    return shared;
}

void BassRig::toggle() noexcept
{
    const auto now = listen();

    if (now == Listen::real)        setListen (Listen::app);
    else if (now == Listen::app)    setListen (Listen::real);
}

BassRig::Levels BassRig::takeLevels() noexcept
{
    Levels l;
    const auto n = playedSamples.exchange (0);
    const auto app = appSum.exchange (0.0), real = realSum.exchange (0.0);

    if (n > 0)
    {
        l.appDb  = juce::Decibels::gainToDecibels (std::sqrt (app / (double) n), -100.0);
        l.realDb = juce::Decibels::gainToDecibels (std::sqrt (real / (double) n), -100.0);
        l.playedSeconds = (double) n / rate;
    }

    return l;
}

double BassRig::captureSeconds() const noexcept
{
    return (double) capturedSamples.load() / rate;
}

juce::String BassRig::startCapture (const juce::File& folder)
{
    stopCapture();

    if (! folder.createDirectory())
        return "Could not make the folder " + folder.getFullPathName();

    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    auto file = folder.getNonexistentChildFile ("Bass9 capture " + stamp, ".wav", false);

    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

    if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
        return "Could not write to " + file.getFullPathName();

    juce::WavAudioFormat wav;
    auto made = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                 .withSampleRate (rate)
                                                 .withNumChannels (3)
                                                 .withBitsPerSample (24));
    if (made == nullptr)
        return "Could not start a WAV file at " + juce::String (rate) + " Hz";

    // What the app's bass was set to while this was recorded, and which channel is which.
    file.withFileExtension ("txt").replaceWithText (
        "# GigRig Bass9 capture. WAV channels: 1 = dry guitar (Bass9 DRY OUT), 2 = real Bass9 (EFFECT OUT), 3 = app bass.\n"
        "# Build " + juce::String (GIGRIG_BUILD_LABEL) + ", " + juce::String (rate) + " Hz.\n"
        + juce::String (getSettings().toText()));

    capturedSamples.store (0);
    auto threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (made.release(), writerThread, 1 << 17);

    {
        const juce::SpinLock::ScopedLockType hold (writerLock);
        writer = std::move (threaded);
    }

    lastFile = file;
    capturing.store (true);
    return {};
}

void BassRig::stopCapture()
{
    capturing.store (false);
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> old;

    {
        const juce::SpinLock::ScopedLockType hold (writerLock);
        old = std::move (writer);
    }

    old.reset();    // flushes the rest to disk and closes the file
}

void BassRig::prepare (double sampleRate, int maxBlock)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    machine.prepare (rate);
    machine.setSettings (getSettings());
    appBuffer.assign ((size_t) juce::jmax (maxBlock, 16384), 0.0f);
    silence.assign (appBuffer.size(), 0.0f);
    envCoeff = 1.0 - std::exp (-1.0 / (0.05 * rate));
    dryEnv = 0.0;
}

void BassRig::process (const float* const* inputs, int numIns, float* outA, float* outB, int numSamples) noexcept
{
    if (numSamples <= 0 || numSamples > (int) appBuffer.size())
        return;

    if (settingsChanged.load())
    {
        const juce::SpinLock::ScopedTryLockType hold (settingsLock);

        if (hold.isLocked())
        {
            machine.setSettings (shared);
            settingsChanged.store (false);
        }
    }

    const auto pick = [&] (int index) -> const float*
    {
        return index >= 0 && index < numIns && inputs[index] != nullptr ? inputs[index] : silence.data();
    };

    const float* dry = pick (dryIn.load());
    const float* real = pick (realIn.load());
    const auto mode = listen();
    const bool recording = capturing.load();

    if (mode == Listen::off && ! recording)
        return;

    machine.process (dry, appBuffer.data(), numSamples);

    raise (peaks[0], peakOf (dry, numSamples));
    raise (peaks[1], peakOf (real, numSamples));
    raise (peaks[2], peakOf (appBuffer.data(), numSamples));

    // Level figures, counted only while the guitar is actually being played.
    double app = 0.0, rl = 0.0;
    juce::int64 played = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        dryEnv += envCoeff * (std::abs (dry[i]) - dryEnv);

        if (dryEnv > playingLevel)
        {
            app += (double) appBuffer[(size_t) i] * appBuffer[(size_t) i];
            rl += (double) real[i] * real[i];
            ++played;
        }
    }

    if (played > 0)
    {
        addTo (appSum, app);
        addTo (realSum, rl);
        playedSamples.fetch_add (played);
    }

    // The chosen bass, mono on both returns of the pair.
    if (mode != Listen::off)
    {
        const float* source = mode == Listen::app ? appBuffer.data() : real;

        for (auto* out : { outA, outB })
            if (out != nullptr)
                juce::FloatVectorOperations::add (out, source, numSamples);
    }

    if (recording && capturedSamples.load() < (juce::int64) (maxCaptureSeconds * rate))
    {
        const juce::SpinLock::ScopedTryLockType hold (writerLock);

        if (hold.isLocked() && writer != nullptr)
        {
            const float* channels[3] { dry, real, appBuffer.data() };
            if (writer->write (channels, numSamples))
                capturedSamples.fetch_add (numSamples);
        }
    }
}

} // namespace gigrig
