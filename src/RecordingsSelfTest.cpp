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

    /** A song with the vocal's socket on track 1 (switch 1 records, plays, overdubs) and nothing waiting for a bar. */
    SongSetup loopSong (const char* name)
    {
        SongSetup song;
        juce::String (name).copyToUTF8 (song.name, sizeof (song.name));
        song.quantise = false;
        song.track[0].rcInputs = rcMic1;
        song.bank[0][0] = 1;        // T1 REC/PLAY
        return song;
    }

    PedalEvent foot (int sw, PedalEvent::Kind kind)
    {
        PedalEvent e;
        e.sw = sw;
        e.kind = kind;
        return e;
    }

    /** What the looper did, with the sample it did it on. */
    std::vector<LoopLogEntry> logOf (Looper& looper)
    {
        std::vector<LoopLogEntry> all (512), kept;
        all.resize ((size_t) looper.takeLog (all.data(), (int) all.size()));

        for (const auto& e : all)
            if (e.kind != LoopLogEntry::Kind::cleared && e.kind != LoopLogEntry::Kind::songLoaded)      // clearing before a soundcheck is not part of the performance
                kept.push_back (e);

        return kept;
    }

    /** Runs the bench one block at a time and keeps everything every return was sent. */
    void runKeeping (Bench& b, int blocks, std::vector<float>& kept)
    {
        for (int i = 0; i < blocks; ++i)
        {
            b.run (1);

            for (int k = 0; k < block; ++k)
            {
                float sum = 0.0f;

                for (int r = 0; r < 16; ++r)
                    sum += b.outs[(size_t) r][(size_t) k] * (float) (r + 1);

                kept.push_back (sum);
            }
        }
    }
}

static void runLoopReplaySelfTest (const Check& check, const juce::File& root)
{
    const auto base = root.getChildFile ("Loop recordings");
    Mixer::recordingFolderOverride = base;

    // ---- the looper acts on a press at its own sample, wherever the blocks fall ----
    {
        const auto run = [] (int firstBlock, bool timed, std::vector<float>& heard, std::vector<LoopLogEntry>& log)
        {
            Looper looper;
            looper.prepare (48000.0, 10.0);
            looper.setSong (loopSong ("Timing"));
            const juce::int64 presses[] = { 6400, 6464, 30400, 30464, 54400, 54464 };     // record, play, overdub: all on a 64-sample grid
            std::vector<float> in ((size_t) block), out ((size_t) block);
            juce::int64 t = 0;
            int next = 0;

            while (t < 96000)
            {
                const int n = t == 0 ? firstBlock : block;
                const float* ins[16] = {};
                float* outs[numLoopOutputs] = {};

                for (int i = 0; i < n; ++i)
                    in[(size_t) i] = 0.3f * (float) std::sin (twoPi * 331.0 * (double) (t + i) / 48000.0) * (float) (1 + ((t + i) / 4800) % 3);

                ins[5] = in.data();
                std::fill (out.begin(), out.end(), 0.0f);
                outs[4] = out.data();
                std::array<Looper::Timed, 8> list {};
                int count = 0;

                while (next < 6 && presses[next] < t + n)
                {
                    const auto e = foot (0, next % 2 == 0 ? PedalEvent::Kind::down : PedalEvent::Kind::up);

                    if (timed)
                    {
                        list[(size_t) count].offset = (int) (presses[next] - t);
                        list[(size_t) count].event = e;
                        ++count;
                    }
                    else
                        looper.pedal (e);       // lands at the start of this block, which is its own sample on the grid

                    ++next;
                }

                looper.setTimedPresses (list.data(), count);
                looper.process (ins, 16, outs, n);
                heard.insert (heard.end(), out.begin(), out.begin() + n);
                t += n;
            }

            std::vector<LoopLogEntry> all (512);
            all.resize ((size_t) looper.takeLog (all.data(), (int) all.size()));
            log = all;
        };

        std::vector<float> onGrid, offGrid;
        std::vector<LoopLogEntry> logA, logB;
        run (block, false, onGrid, logA);       // every press arrives at the start of a block, as a foot's does
        run (27, true, offGrid, logB);          // the same presses, now 37 samples into a block
        bool same = onGrid.size() >= 96000 && offGrid.size() >= 96000 && logA.size() == logB.size() && logA.size() >= 4;
        float loud = 0.0f;

        for (size_t i = 0; i < 96000 && same; ++i)
        {
            same = onGrid[i] == offGrid[i];
            loud = juce::jmax (loud, std::abs (onGrid[i]));
        }

        for (size_t i = 0; i < logA.size() && same; ++i)
            same = logA[i].kind == logB[i].kind && logA[i].when == logB[i].when && logA[i].a == logB[i].a;

        check (same && loud > 0.1f, "loops in a soundcheck: a press handed to the looper with its sample does exactly what a foot at that sample does (the same loop sample for sample, "
                                      + juce::String ((int) logA.size()) + " looper events at the same samples, with the press 37 samples into a block)");
    }

    // ---- through the whole engine at 48 kHz: noted while recording, made again in the soundcheck ----
    {
        auto bench = std::make_unique<Bench> (48000.0);
        auto& b = *bench;
        auto& looper = b.engine.looper();
        auto& replay = b.engine.loopReplay();
        juce::StringArray loaded;
        bool haveSong = true;
        replay.loadSong = [&] (const juce::String& name) { loaded.add (name); if (haveSong) looper.setSong (loopSong (name.toRawUTF8())); return haveSong; };
        looper.setSong (loopSong ("Soundcheck song"));
        b.source = [] (int s, juce::int64 t) { return s == 5 ? 0.25f * (float) std::sin (twoPi * 220.0 * (double) t / 48000.0) * (float) (1 + (t / 6000) % 4) : 0.0f; };

        // Presses with nothing recording are noted nowhere.
        b.run (20);
        looper.pedal (foot (0, PedalEvent::Kind::down));
        b.run (4);
        looper.clearAll();
        b.run (4);
        replay.poll();
        logOf (looper);

        const auto first = looper.snapshot().now;
        const auto error = b.engine.mixer().startRecording (Mixer::recordingFolder());
        std::vector<float> live;
        runKeeping (b, 150, live);
        looper.pedal (foot (0, PedalEvent::Kind::down));    runKeeping (b, 5, live);
        looper.pedal (foot (0, PedalEvent::Kind::up));      runKeeping (b, 495, live);
        looper.pedal (foot (0, PedalEvent::Kind::down));    runKeeping (b, 5, live);        // the loop closes and plays
        looper.pedal (foot (0, PedalEvent::Kind::up));      runKeeping (b, 695, live);
        looper.pedal (foot (0, PedalEvent::Kind::down));    runKeeping (b, 5, live);        // an overdub
        looper.pedal (foot (0, PedalEvent::Kind::up));      runKeeping (b, 300, live);
        looper.pedal (foot (0, PedalEvent::Kind::down));    runKeeping (b, 5, live);        // the overdub ends
        looper.pedal (foot (0, PedalEvent::Kind::up));      runKeeping (b, 590, live);
        b.engine.mixer().stopRecording();
        const auto folder = b.engine.mixer().recorder().folder();
        const auto liveLog = logOf (looper);
        replay.poll();

        const auto text = folder.getChildFile (LoopReplay::fileName).loadFileAsString();
        const auto tape = LoopReplay::parse (text);
        const juce::int64 expected[] = { 150, 155, 650, 655, 1350, 1355, 1655, 1660 };
        bool noted = error.isEmpty() && tape.found && tape.steps.size() == 8 && std::abs (tape.rate - 48000.0) < 0.5 && tape.startSong == "Soundcheck song" && ! tape.loopsGoing;

        for (size_t i = 0; i < tape.steps.size() && noted; ++i)
            noted = tape.steps[i].at == expected[i] * block && tape.steps[i].event.sw == 0
                      && tape.steps[i].event.kind == (i % 2 == 0 ? PedalEvent::Kind::down : PedalEvent::Kind::up);

        check (noted, "loops in a soundcheck: while recording, every looper press is noted in the recording's folder with its sample and the song that was loaded ("
                        + juce::String ((int) tape.steps.size()) + " presses in \"" + LoopReplay::fileName + "\"; presses made while nothing records are noted nowhere)");

        // Nothing is plugged in now. The soundcheck plays the raw file and makes the presses again.
        b.source = [] (int, juce::int64) { return 0.0f; };
        b.run (40);
        const auto name = folder.getFileName();
        const auto started = b.engine.startSoundcheck (name, false, true, false);
        const bool armed = replay.isArmed();
        logOf (looper);
        std::vector<float> again;
        juce::int64 second = -1;

        for (int i = 0; i < 40 && second < 0; ++i)
        {
            const auto before = looper.snapshot().now;
            b.run (1);

            if (b.engine.soundcheck().blockStart() == 0)
            {
                second = before;

                for (int k = 0; k < block; ++k)
                {
                    float sum = 0.0f;
                    for (int r = 0; r < 16; ++r) sum += b.outs[(size_t) r][(size_t) k] * (float) (r + 1);
                    again.push_back (sum);
                }
            }
        }

        runKeeping (b, 2250 - 1 - 10, again);
        const auto againLog = logOf (looper);
        bool sameMoments = started.isEmpty() && armed && second >= 0 && ! liveLog.empty() && liveLog.size() == againLog.size() && replay.pressesMade() == 8;

        for (size_t i = 0; i < liveLog.size() && sameMoments; ++i)
            sameMoments = liveLog[i].kind == againLog[i].kind && liveLog[i].when - first == againLog[i].when - second && liveLog[i].a == againLog[i].a;

        double worst = 0.0, loud = 0.0;

        for (size_t i = 0; i < again.size() && i < live.size(); ++i)
        {
            worst = juce::jmax (worst, (double) std::abs (again[i] - live[i]));
            loud = juce::jmax (loud, (double) std::abs (live[i]));
        }

        check (sameMoments && loud > 0.2 && worst < 2.0e-5 && again.size() > 100000,
               "loops in a soundcheck: the soundcheck makes every press again on its own sample, so the looper does the same " + juce::String ((int) liveLog.size())
                 + " things at the same samples and sends the returns the same loops as on the night (worst difference " + juce::String (worst, 8) + " against a sound of " + juce::String (loud, 2) + ")");

        // Played once it ends, and the loops it built go with it.
        runKeeping (b, 40, again);
        replay.poll();
        b.run (4);
        bool empty = ! b.engine.soundcheck().isPlaying() && ! replay.isArmed();

        for (const auto& tr : looper.snapshot().track)
            empty = empty && tr.state == TrackState::empty && ! tr.hasAudio;

        check (empty, "loops in a soundcheck: when the soundcheck stops, the loops it built are cleared and no more presses are made");

        // Round and round: each time the recording comes back to its start the loops go and are built again.
        const auto round = b.engine.startSoundcheck (name, true, true, false);
        logOf (looper);
        juce::int64 start2 = -1;

        for (int i = 0; i < 40 && start2 < 0; ++i)
        {
            const auto before = looper.snapshot().now;
            b.run (1);

            if (b.engine.soundcheck().blockStart() == 0)
                start2 = before;
        }

        b.run (2250 * 2 + 700);
        const auto roundLog = logOf (looper);
        const auto pass = b.engine.soundcheck().passFrames();
        std::vector<juce::int64> closed;

        for (const auto& e : roundLog)
            if (e.kind == LoopLogEntry::Kind::loopClosed)
                closed.push_back (e.when - start2);

        juce::int64 liveClosed = -1;

        for (const auto& e : liveLog)
            if (e.kind == LoopLogEntry::Kind::loopClosed)
                liveClosed = e.when - first;

        check (round.isEmpty() && start2 >= 0 && pass == 2250 * block && closed.size() == 3 && liveClosed > 0
                 && closed[0] == liveClosed && closed[1] == liveClosed + pass && closed[2] == liveClosed + 2 * pass && replay.pressesMade() >= 18,
               "loops in a soundcheck: played round and round, the loops are cleared where the recording comes back to its start and built again at the same samples of every pass");

        b.engine.soundcheck().stop();
        b.run (40);
        replay.poll();

        // Without its loops: the tick box off.
        const auto plain = b.engine.startSoundcheck (name, true, false, false);
        b.run (800);
        bool untouched = plain.isEmpty() && ! replay.isArmed();

        for (const auto& tr : looper.snapshot().track)
            untouched = untouched && tr.state == TrackState::empty;

        check (untouched, "loops in a soundcheck: with \"With its loops\" unticked the soundcheck plays and the looper is left alone");
        b.engine.soundcheck().stop();
        b.run (40);

        // The song: loaded if another is loaded now, and no presses at all if it cannot be found.
        looper.setSong (loopSong ("Another song"));
        b.run (4);
        loaded.clear();
        const auto other = b.engine.startSoundcheck (name, true, true, false);
        const bool loadedIt = other.isEmpty() && replay.isArmed() && loaded.size() == 1 && loaded[0] == "Soundcheck song";
        b.engine.soundcheck().stop();
        b.run (40);
        replay.poll();
        looper.setSong (loopSong ("Another song"));
        b.run (4);
        haveSong = false;
        const auto missing = b.engine.startSoundcheck (name, true, true, false);
        b.run (800);
        bool leftAlone = missing.isEmpty() && ! replay.isArmed() && replay.statusLine().contains ("Soundcheck song") && b.engine.soundcheck().isPlaying();

        for (const auto& tr : looper.snapshot().track)
            leftAlone = leftAlone && tr.state == TrackState::empty;

        check (loadedIt && leftAlone, "loops in a soundcheck: the song the recording was made with is loaded first if another is loaded, and if it is not in the list the soundcheck plays without presses and says why (\""
                                        + replay.statusLine() + "\")");
        b.engine.soundcheck().stop();
        b.run (40);
        haveSong = true;

        // A song change in the middle of a recording, and loops that were already going when it began.
        looper.setSong (loopSong ("First"));
        b.source = [] (int s, juce::int64 t) { return s == 5 ? 0.2f * (float) std::sin (twoPi * 330.0 * (double) t / 48000.0) : 0.0f; };
        b.run (10);
        const auto err2 = b.engine.mixer().startRecording (Mixer::recordingFolder().getChildFile ("songs"));
        b.run (100);
        looper.pedal (foot (0, PedalEvent::Kind::down));
        b.run (50);
        looper.setSong (loopSong ("Second"));
        b.run (300);
        b.engine.mixer().stopRecording();
        replay.poll();
        const auto folder2 = b.engine.mixer().recorder().folder();
        const auto tape2 = LoopReplay::read (folder2);
        bool songNoted = err2.isEmpty() && tape2.found && tape2.startSong == "First" && tape2.steps.size() == 2
                           && tape2.steps[1].kind == Looper::Timed::Kind::song && tape2.steps[1].song == "Second" && tape2.steps[1].at == 150 * block;

        Mixer::recordingFolderOverride = base.getChildFile ("songs");
        looper.setSong (loopSong ("First"));
        b.source = [] (int, juce::int64) { return 0.0f; };
        b.run (10);
        loaded.clear();
        const auto err3 = b.engine.startSoundcheck (folder2.getFileName(), false, true, false);
        b.run (200);
        replay.poll();
        b.run (10);
        const bool songMade = err3.isEmpty() && loaded.size() == 1 && loaded[0] == "Second" && looper.song().title() == "Second";
        b.engine.soundcheck().stop();
        b.run (40);
        replay.poll();

        looper.setSong (loopSong ("First"));
        b.source = [] (int s, juce::int64 t) { return s == 5 ? 0.2f * (float) std::sin (twoPi * 330.0 * (double) t / 48000.0) : 0.0f; };
        b.run (10);
        looper.pedal (foot (0, PedalEvent::Kind::down));
        b.run (40);
        const auto err4 = b.engine.mixer().startRecording (Mixer::recordingFolder().getChildFile ("going"));
        b.run (200);
        b.engine.mixer().stopRecording();
        replay.poll();
        const auto folder4 = b.engine.mixer().recorder().folder();
        const auto tape4 = LoopReplay::read (folder4);
        Mixer::recordingFolderOverride = base.getChildFile ("songs").getChildFile ("going");
        looper.clearAll();
        b.run (10);
        const auto err5 = b.engine.startSoundcheck (folder4.getFileName(), false, true, false);
        const bool refused = err4.isEmpty() && err5.isEmpty() && tape4.found && tape4.loopsGoing && ! replay.isArmed() && replay.statusLine().contains ("already going");
        b.engine.soundcheck().stop();
        b.run (40);
        Mixer::recordingFolderOverride = root.getChildFile ("Recordings");

        check (songNoted && songMade && refused,
               "loops in a soundcheck: a song loaded during the recording is noted and loaded again at the same place, and a recording that began with loops already going is played without presses, since those loops cannot be built again");
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

    runLoopReplaySelfTest (check, root);

    Mixer::recordingFolderOverride = juce::File();
    root.deleteRecursively();
}

} // namespace gigrig
