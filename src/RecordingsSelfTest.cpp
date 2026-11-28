#include "AudioEngine.h"

#include <cmath>
#include <functional>

// Checks for the virtual soundcheck and the recordings on the drive (28 Nov 2026), run by the
// automatic builds (GigRig --self-test): that a recording keeps a raw copy of every socket and says
// which is which, that a soundcheck plays those files into the sockets they came from sample for
// sample, goes round and round or stops by itself, leaves other sockets live, changes nothing
// while it is not playing, that the engine really hears it (a recording made of a soundcheck holds
// the same samples), the change of sample rate, and keeping, copying (checked), deleting and
// clearing recordings without ever touching one that is kept or in use.

namespace gigrig
{

namespace
{
    using Check = std::function<void (bool, const juce::String&)>;
    constexpr int block = 64;
    constexpr double twoPi = 6.283185307179586;

    struct Bench
    {
        AudioEngine engine;
        double rate;
        std::vector<std::vector<float>> ins, outs;
        std::vector<const float*> inPtrs;
        std::vector<float*> outPtrs;
        juce::int64 t = 0;
        std::function<float (int socket, juce::int64 time)> source = [] (int, juce::int64) { return 0.0f; };

        explicit Bench (double sampleRate) : rate (sampleRate)
        {
            engine.openSimulated (rate, block, 16, 16);
            ins.assign (16, std::vector<float> (block, 0.0f));
            outs.assign (16, std::vector<float> (block, 0.0f));
            for (auto& v : ins)  inPtrs.push_back (v.data());
            for (auto& v : outs) outPtrs.push_back (v.data());
        }

        void run (int blocks)
        {
            for (int b = 0; b < blocks; ++b)
            {
                for (int s = 0; s < 16; ++s)
                    for (int i = 0; i < block; ++i)
                        ins[(size_t) s][(size_t) i] = source (s, t + i);

                t += block;
                engine.soundcheck().fillNow();
                engine.processBlock (inPtrs.data(), 16, outPtrs.data(), 16, block);

                if (b % 16 == 15)
                    engine.mixer().tick();
            }
        }
    };

    std::vector<float> wholeFile (const juce::File& f, double* rate = nullptr)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (f.existsAsFile() ? formats.createReaderFor (f) : nullptr);

        if (r == nullptr)
            return {};

        juce::AudioBuffer<float> buf (1, (int) r->lengthInSamples);
        r->read (&buf, 0, (int) r->lengthInSamples, 0, true, false);

        if (rate != nullptr)
            *rate = r->sampleRate;

        return { buf.getReadPointer (0), buf.getReadPointer (0) + buf.getNumSamples() };
    }

    double strength (const float* x, int n, double hz, double rate)
    {
        double re = 0.0, im = 0.0;

        for (int i = 0; i < n; ++i)
        {
            re += x[i] * std::cos (twoPi * hz * i / rate);
            im += x[i] * std::sin (twoPi * hz * i / rate);
        }

        return 2.0 * std::sqrt (re * re + im * im) / juce::jmax (1, n);
    }

    /** Plays a soundcheck straight from its queue (no engine), with silence plugged in, and keeps what each socket was given. */
    std::vector<std::vector<float>> playOut (Soundcheck& sc, int blocks, int sockets, bool* everLive = nullptr)
    {
        std::vector<std::vector<float>> silent ((size_t) sockets, std::vector<float> (block, 0.0f)), got ((size_t) sockets);
        std::vector<const float*> ptrs;
        for (auto& v : silent) ptrs.push_back (v.data());

        for (int b = 0; b < blocks; ++b)
        {
            sc.fillNow();
            const auto* fed = sc.feed (ptrs.data(), sockets, block);

            if (everLive != nullptr && fed == ptrs.data())
                *everLive = true;

            for (int s = 0; s < sockets; ++s)
                got[(size_t) s].insert (got[(size_t) s].end(), fed[s], fed[s] + block);
        }

        return got;
    }
}

void runRecordingsSelfTest (const Check& check)
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gigrig-recordings-test-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
    root.createDirectory();
    Mixer::recordingFolderOverride = root.getChildFile ("Recordings");
    const auto tone = [] (double rate) { return [rate] (int s, juce::int64 t) { return 0.2f * (float) std::sin (twoPi * 110.0 * (s + 1) * (double) t / rate); }; };

    // ---- at 48 kHz: the raw copy, and playing it back sample for sample ----
    {
        auto bench = std::make_unique<Bench> (48000.0);
        auto& b = *bench;
        auto& mixer = b.engine.mixer();
        auto& sc = b.engine.soundcheck();
        b.source = tone (b.rate);

        const auto* same = sc.feed (b.inPtrs.data(), 16, block);
        check (same == b.inPtrs.data() && ! sc.isPlaying(), "soundcheck: while nothing is playing the engine is handed the very inputs it was given, so no sample changes");

        b.run (20);
        const auto startAt = b.t;
        const auto error = mixer.startRecording (Mixer::recordingFolder());
        b.run (750);        // one second
        mixer.stopRecording();
        const auto folder = mixer.recorder().folder();
        const auto sources = Soundcheck::sourcesIn (folder);
        bool filesRight = ! sources.empty();
        std::vector<std::vector<float>> files;
        juce::StringArray named;

        for (const auto& s : sources)
        {
            files.push_back (wholeFile (s.file));
            named.add (s.file.getFileName() + " = socket " + juce::String (s.socket + 1));
            const auto& f = files.back();
            filesRight = filesRight && f.size() == 48000;

            for (size_t i = 0; i < f.size() && filesRight; i += 7)
                filesRight = std::abs (f[i] - b.source (s.socket, startAt + (juce::int64) i)) < 1.0e-6f;
        }

        check (error.isEmpty() && filesRight && folder.getChildFile ("About this recording.txt").loadFileAsString().contains ("soundcheck: "),
               "soundcheck: a recording keeps a raw copy of every socket in use, from the first sample, and its note says which socket each file came from (" + named.joinIntoString ("; ") + ")");

        // Played straight from the queue: after the 10 ms fade in, each socket gets its file sample for sample, round and round.
        const auto started = sc.start (folder, true, false);
        bool everLive = false;
        const auto got = playOut (sc, 1600, 16, &everLive);
        bool exact = started.isEmpty() && ! sources.empty(), othersLive = true;
        unsigned used = 0;

        for (size_t k = 0; k < sources.size() && exact; ++k)
        {
            const auto& g = got[(size_t) sources[k].socket];
            const auto& f = files[k];
            used |= 1u << sources[k].socket;
            exact = std::abs (g[0]) < 1.0e-3f;

            for (size_t i = 480; i < g.size() && exact; ++i)
                exact = g[i] == f[i % f.size()];
        }

        for (int s = 0; s < 16; ++s)
            if ((used & (1u << s)) == 0)
                for (const auto v : got[(size_t) s])
                    othersLive = othersLive && v == 0.0f;

        check (exact && ! everLive && othersLive && sc.sockets() == used && sc.isPlaying() && std::abs (sc.lengthSeconds() - 1.0) < 1.0e-6
                 && sc.positionSeconds() < 1.0 && sc.missedBlocks() == 0,
               "soundcheck: each socket is given its own file sample for sample after a 10 ms fade in, the end runs straight into the start, and no other socket is touched");

        // Through the whole engine: a recording made while the soundcheck plays holds the same samples, so the rig really hears it.
        b.source = [] (int, juce::int64) { return 0.0f; };      // nothing is plugged in
        const auto playedBefore = (juce::int64) 1600 * block;
        const auto second = mixer.startRecording (Mixer::recordingFolder().getChildFile ("again"));
        b.run (400);
        mixer.stopRecording();
        const auto again = Soundcheck::sourcesIn (mixer.recorder().folder());
        bool heard = second.isEmpty() && again.size() == sources.size() && ! again.empty();

        for (size_t k = 0; k < again.size() && heard; ++k)
        {
            const auto f2 = wholeFile (again[k].file);
            const auto& f1 = files[k];
            heard = again[k].socket == sources[k].socket && f2.size() == (size_t) 400 * block;

            for (size_t i = 0; i < f2.size() && heard; ++i)
                heard = f2[i] == f1[(size_t) ((playedBefore + (juce::int64) i) % (juce::int64) f1.size())];
        }

        check (heard, "soundcheck: the engine hears the recording at its sockets (a new recording made while it plays holds exactly the same samples, so an old take can be mixed again)");

        sc.stop();
        check (! sc.isPlaying() && sc.feed (b.inPtrs.data(), 16, block) == b.inPtrs.data() && sc.sockets() == 0u,
               "soundcheck: Stop hands every socket back to what is plugged into it");

        // Once through, it stops by itself.
        const auto once = sc.start (folder, false, false);
        const auto tail = playOut (sc, 760, 16);
        bool silentAfter = true;
        const auto& g0 = tail[(size_t) sources[0].socket];

        for (size_t i = 48000; i < g0.size(); ++i)
            silentAfter = silentAfter && g0[i] == 0.0f;

        check (once.isEmpty() && ! sc.isPlaying() && silentAfter && g0[47000] == files[0][47000],
               "soundcheck: played once, a recording plays to its last sample and then stops by itself");
        sc.stop();

        // With its own thread, as on the rig.
        const auto name = folder.getFileName();
        const auto viaEngine = b.engine.startSoundcheck (name);
        const bool ready = sc.waitUntilReady (3000);
        float peak = 0.0f;

        for (int i = 0; i < 40; ++i)
        {
            const auto* fed = sc.feed (b.inPtrs.data(), 16, block);
            for (int k = 0; k < block; ++k)
                peak = juce::jmax (peak, std::abs (fed[sources[0].socket][k]));
        }

        const bool refusesGone = b.engine.startSoundcheck ("no such recording").isNotEmpty() && b.engine.startSoundcheck ("../" + name).isNotEmpty();
        check (viaEngine.isEmpty() && ready && peak > 0.1f && refusesGone && sc.reportText().contains ("VIRTUAL SOUNDCHECK"),
               "soundcheck: the engine starts one by the recording's name with its own reader thread, and a name that is not a recording is refused");

        // ---- the recordings on the drive ----
        auto& lib = b.engine.recordings();
        const auto busyDelete = lib.remove (name);
        b.engine.soundcheck().stop();
        const auto usb = root.getChildFile ("USB STICK");
        usb.createDirectory();
        RecordingLibrary::drivesOverride = { usb };
        RecordingLibrary::useDrivesOverride = true;

        // A third recording with no raw copy: it cannot be played back.
        auto set = mixer.setup().rec;
        set.checkCopy = false;
        mixer.setRecord (set);
        juce::Thread::sleep (1100);     // a folder is named by the second it started
        const auto third = mixer.startRecording (Mixer::recordingFolder());
        b.run (100);
        const auto liveCopy = lib.copy ({}, "USB STICK");
        const auto liveDelete = lib.remove (mixer.recorder().folder().getFileName());
        mixer.stopRecording();
        const auto plain = mixer.recorder().folder().getFileName();
        const auto noRaw = b.engine.startSoundcheck (plain);
        auto list = lib.list();
        const auto find = [&list] (const juce::String& n) { return std::find_if (list.begin(), list.end(), [&] (const RecordingLibrary::Info& i) { return i.name == n; }); };

        check (third.isEmpty() && list.size() == 2 && list[0].name == plain && list[1].name == name && list[1].soundcheckSockets == (int) sources.size() && list[0].soundcheckSockets == 0
                 && std::abs (list[1].seconds - 1.0) < 0.01 && list[1].files > (int) sources.size() && list[1].bytes > 48000 * 3 && ! list[1].kept && ! list[1].copied
                 && noRaw.contains ("raw") && busyDelete.contains ("soundcheck") && liveCopy.contains ("recording") && liveDelete.contains ("recorded"),
               "recordings: the list is newest first with each one's length, files and whether it can be played back; one with no raw files, one that is playing and one being recorded are each refused with the reason");

        // Keep, and a copy that is checked.
        const auto kept = lib.setKept (name, true);
        const auto keptDelete = lib.remove (name);
        const auto copyError = lib.copy ({ name }, "USB STICK");
        const bool finished = lib.waitForCopy (20000);
        const auto copied = usb.getChildFile (RecordingLibrary::driveFolder).getChildFile (name);
        bool identical = finished && copied.isDirectory();
        int compared = 0;

        for (const auto& f : folder.findChildFiles (juce::File::findFiles, false))
        {
            if (f.getFileName() == RecordingLibrary::copiedFile)
                continue;

            identical = identical && copied.getChildFile (f.getFileName()).hasIdenticalContentTo (f);
            ++compared;
        }

        list = lib.list();
        const auto p = lib.progress();
        check (kept.isEmpty() && keptDelete.contains ("kept") && copyError.isEmpty() && identical && compared > 2 && find (name)->copied && find (name)->kept && ! find (plain)->copied
                 && ! p.running && ! p.failed && p.result.contains ("USB STICK") && lib.copy ({ name }, "NO SUCH DRIVE").isNotEmpty()
                 && lib.setKept ("../" + name, true).isNotEmpty() && lib.remove ("..").isNotEmpty(),
               "recordings: a kept recording cannot be deleted, a copy to USB holds every file byte for byte and is marked copied only after the check, and a wrong drive or a path instead of a name is refused");

        // Clearing: copied ones only, never a kept one.
        const auto clearedCopied = lib.clear (true);
        const bool bothStill = lib.list().size() == 2;      // one is kept, the other is not copied
        lib.setKept (name, false);
        const auto clearedCopied2 = lib.clear (true);
        list = lib.list();
        const bool onlyPlain = list.size() == 1 && list[0].name == plain && copied.isDirectory();
        lib.setKept (plain, true);
        const auto clearedAll = lib.clear (false);
        const bool keptStays = lib.list().size() == 1;
        lib.setKept (plain, false);
        const auto gone = lib.remove (plain);
        check (bothStill && onlyPlain && keptStays && gone.isEmpty() && lib.list().empty() && clearedCopied.contains ("Cleared 0") && clearedCopied2.contains ("Cleared 1") && clearedAll.contains ("1 kept")
                 && lib.reportText().contains ("RECORDINGS ON THE DRIVE") && lib.reportText().contains ("USB STICK"),
               "recordings: clearing the copied ones leaves the kept and the not yet copied, clearing all leaves only the kept, and the copy on the USB drive is never touched");

        RecordingLibrary::useDrivesOverride = false;
        RecordingLibrary::drivesOverride.clear();
    }

    // ---- at 96 kHz: the files are 48 kHz and are brought back up to the rig's rate ----
    {
        auto bench = std::make_unique<Bench> (96000.0);
        auto& b = *bench;
        auto& mixer = b.engine.mixer();
        auto& sc = b.engine.soundcheck();
        b.source = [] (int, juce::int64 t) { return 0.25f * (float) std::sin (twoPi * 1000.0 * (double) t / 96000.0); };
        juce::Thread::sleep (1100);
        const auto error = mixer.startRecording (Mixer::recordingFolder());
        b.run (750);        // half a second
        mixer.stopRecording();
        const auto folder = mixer.recorder().folder();
        const auto sources = Soundcheck::sourcesIn (folder);
        double fileRate = 0.0;
        const auto f = sources.empty() ? std::vector<float>() : wholeFile (sources[0].file, &fileRate);
        const auto started = sc.start (folder, true, false);
        const auto got = playOut (sc, 600, 16);
        bool right = error.isEmpty() && started.isEmpty() && sources.size() >= 2 && fileRate == 48000.0 && f.size() == 24000 && std::abs (sc.lengthSeconds() - 0.5) < 1.0e-6;
        double level = 0.0;

        if (right)
        {
            const auto& a = got[(size_t) sources[0].socket];
            const auto& c = got[(size_t) sources[1].socket];
            level = strength (a.data() + 9600, 19200, 1000.0, 96000.0);
            right = std::abs (level - 0.25) < 0.0025 && a == c;
        }

        check (right, "soundcheck at 96 kHz: the 48 kHz files are played at the rig's rate, at the pitch and level they were recorded (1 kHz at " + juce::String (level, 4)
                        + " for 0.2500), and every socket stays in step with the others");
        sc.stop();
    }

    Mixer::recordingFolderOverride = juce::File();
    root.deleteRecursively();
}

} // namespace gigrig
